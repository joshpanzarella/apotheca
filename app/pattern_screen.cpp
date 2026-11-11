#include "pattern_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

namespace {

using namespace ui;

// The layout from the same file, in pixels on the 640x480 screen.
constexpr int kGridLeft = 116, kGridTop = 54, kStepW = 32, kLaneH = 41;
constexpr int kCellW = kStepW - 2, kCellH = kLaneH - 3;
constexpr int kNameX = 8, kNameW = 104;
constexpr int kBlockW = kCellW - 8, kBlockFull = kCellH - 8;

constexpr float kNewHitLevel = 0.8f;
constexpr float kMinHitLevel = 0.05f; // B deletes; the stick cannot
constexpr float kMinChance = 0.05f;   // likewise
constexpr float kMaybe = 0.5f;        // the chance of a hit placed with Y
constexpr float kRandomLow = 0.25f;   // R2 paints levels from here up to full
constexpr int kFirstRoll = 4;         // hits a tap of Y gives an empty lane
constexpr float kDiceHitsPerSecond = 24.f; // stick fully over
constexpr int kStartSlices = 16;          // the D-pad and the lane roll move a start by sixteenths of the sound
constexpr float kMaxStart = 15.f / 16.f;  // the last sixteenth is always left to hear
constexpr Uint32 kHearStartEveryMs = 90;  // a sliding start is played this often

bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }

float clampTo(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// To the next tenth in a direction: 0.05 goes up to 0.1, 0.5 to 0.6.
float nextTenth(float now, float sign) {
    const float tenths = now * 10.f;
    return (sign > 0.f ? std::floor(tenths + 1.001f) : std::ceil(tenths - 1.001f)) / 10.f;
}

} // namespace

PatternScreen::PatternScreen(Session& session)
    : session_(session), engine_(session.engine), project_(session.project), tempoExact_(session.project.tempo) {}

// ---- writing values --------------------------------------------------------

void PatternScreen::setStep(int lane, int step, float level) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    pat().fromDice[lane][step] = false; // any write makes the hit the player's; the dice re-mark their own
    pat().level[lane][step] = level;
    Command c;
    c.type = Command::SetStep;
    c.track = project_.track;
    c.scene = project_.scene;
    c.bar = project_.bar;
    c.a = lane;
    c.b = step;
    c.value = level;
    engine_.send(c);
    // A removed hit leaves nothing behind for the next one placed there.
    if (level <= 0.f) {
        setNudge(lane, step, 0.f);
        setChance(lane, step, 1.f);
        setStart(lane, step, 0.f);
    }
}

void PatternScreen::setNudge(int lane, int step, float nudge) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    nudge = clampTo(nudge, -0.5f, 0.5f);
    if (nudge == pat().nudge[lane][step]) return;
    pat().fromDice[lane][step] = false;
    pat().nudge[lane][step] = nudge;
    Command c;
    c.type = Command::SetStepNudge;
    c.track = project_.track;
    c.scene = project_.scene;
    c.bar = project_.bar;
    c.a = lane;
    c.b = step;
    c.value = nudge;
    engine_.send(c);
}

void PatternScreen::setChance(int lane, int step, float chance) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    chance = clampTo(chance, kMinChance, 1.f);
    if (chance == pat().chance[lane][step]) return;
    pat().fromDice[lane][step] = false;
    pat().chance[lane][step] = chance;
    Command c;
    c.type = Command::SetStepChance;
    c.track = project_.track;
    c.scene = project_.scene;
    c.bar = project_.bar;
    c.a = lane;
    c.b = step;
    c.value = chance;
    engine_.send(c);
}

void PatternScreen::setLaneLevel(float level) {
    level = clampTo(level, 0.f, 1.f);
    if (level == trk().laneLevel[lane_]) return;
    trk().laneLevel[lane_] = level;
    Command c;
    c.type = Command::SetLaneLevel;
    c.track = project_.track;
    c.a = lane_;
    c.value = level;
    engine_.send(c);
}

void PatternScreen::setTempo(float bpm) {
    tempoExact_ = clampTo(bpm, kMinTempo, kMaxTempo);
    const float whole = std::round(tempoExact_);
    if (whole == project_.tempo) return;
    project_.tempo = whole;
    Command c;
    c.type = Command::SetTempo;
    c.value = project_.tempo;
    engine_.send(c);
}

void PatternScreen::audition(int lane, float level, float start) {
    Command c;
    c.type = Command::Trigger;
    c.track = project_.track;
    c.a = lane;
    c.b = static_cast<int>(std::round(start * 10000.f));
    c.value = level;
    engine_.send(c);
}

// Where in the lane's sound a hit begins. Never so late that nothing is left.
void PatternScreen::setStart(int lane, int step, float start) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    start = clampTo(start, 0.f, kMaxStart);
    if (start == pat().start[lane][step]) return;
    pat().start[lane][step] = start;
    Command c;
    c.type = Command::SetStepStart;
    c.track = project_.track;
    c.scene = project_.scene;
    c.bar = project_.bar;
    c.a = lane;
    c.b = step;
    c.value = start;
    engine_.send(c);
}

// With Y, sideways. On a hit: slide its start, hearing it on the way. On a
// lane's name: right rolls a new start for every hit on the lane, on the
// sixteenths of the sound; left puts them all back at the beginning.
void PatternScreen::turnStart(float amount) {
    if (amount == 0.f || onTempo_) return;
    if (step_ == kLaneName) {
        if (rolled_) return; // one roll per push
        rolled_ = true;
        yDidSomething_ = true;
        for (int step = 0; step < kSteps; ++step) {
            if (pat().level[lane_][step] <= 0.f) continue;
            dice_ = dice_ * 1664525u + 1013904223u;
            setStart(lane_, step, amount > 0.f ? static_cast<float>((dice_ >> 12) % kStartSlices) / kStartSlices : 0.f);
        }
        return;
    }
    if (!hitHere()) return;
    const float before = pat().start[lane_][step_];
    setStart(lane_, step_, before + amount);
    turning_ = Turning::Start;
    yDidSomething_ = true;
    if (pat().start[lane_][step_] != before && !project_.playing && lastMs_ - heardStartAt_ >= kHearStartEveryMs) {
        heardStartAt_ = lastMs_;
        audition(lane_, pat().level[lane_][step_], pat().start[lane_][step_]);
    }
}

const char* PatternScreen::laneLabel(int lane) const {
    const char* file = trk().laneSample[lane];
    if (!file[0]) return DrumKit::laneName(lane);
    static char labels[kDrumLanes][12];
    int n = 0;
    for (; file[n] && n < 9; ++n) labels[lane][n] = static_cast<char>(file[n] >= 'a' && file[n] <= 'z' ? file[n] - 32 : file[n]);
    labels[lane][n] = 0;
    return labels[lane];
}

void PatternScreen::placeHit(float chance) {
    setStep(lane_, step_, kNewHitLevel);
    setChance(lane_, step_, chance);
    if (!project_.playing) audition(lane_, kNewHitLevel);
}

// ---- painting ----------------------------------------------------------------

float PatternScreen::randomLevel() {
    dice_ = dice_ * 1664525u + 1013904223u;
    const float roll = static_cast<float>(dice_ >> 8) * (1.f / 16777216.f); // 0..1
    return kRandomLow + (1.f - kRandomLow) * roll;
}

// Put a hit under the cursor, or re-level the one that is there.
void PatternScreen::paintHere() {
    if (paint_ == Paint::None || !onStep()) return; // the brush skips the lane names and the tempo
    const float level = paint_ == Paint::OneLevel ? brush_ : randomLevel();
    setStep(lane_, step_, level);
    if (!project_.playing) audition(lane_, level);
}

// ---- lane dice ---------------------------------------------------------------

int PatternScreen::diceHits(int lane) const {
    int count = 0;
    for (int step = 0; step < kSteps; ++step) count += pat().fromDice[lane][step] ? 1 : 0;
    return count;
}

// The order hits arrive in: a shuffle of the sixteen steps that leans toward
// the beat. Quarter notes are four times as likely to come early as the
// off-sixteenths, eighths twice. The same seed always gives the same order,
// so turning up and back down retraces its steps.
void PatternScreen::diceOrder(int lane, int order[kSteps]) const {
    std::uint32_t state = pat().diceSeed[lane];
    int weight[kSteps], total = 0;
    for (int step = 0; step < kSteps; ++step) total += weight[step] = step % 4 == 0 ? 4 : (step % 2 == 0 ? 2 : 1);
    for (int i = 0; i < kSteps; ++i) {
        state = state * 1664525u + 1013904223u;
        int pick = static_cast<int>((state >> 8) % static_cast<std::uint32_t>(total));
        int step = 0;
        while (weight[step] == 0 || pick >= weight[step]) pick -= weight[step++];
        order[i] = step;
        total -= weight[step];
        weight[step] = 0;
    }
}

// A dice hit's level, 50% to 100%, fixed by the shuffle and the step.
float PatternScreen::diceLevel(int lane, int step) const {
    std::uint32_t h = pat().diceSeed[lane] ^ (0x9E3779B9u * static_cast<std::uint32_t>(step + 1));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return 0.5f + 0.5f * static_cast<float>(h >> 8) * (1.f / 16777216.f);
}

// One more hit: the first step in the order that is still empty.
bool PatternScreen::diceAdd() {
    int order[kSteps];
    diceOrder(lane_, order);
    for (int i = 0; i < kSteps; ++i) {
        const int step = order[i];
        if (pat().level[lane_][step] > 0.f) continue;
        const float level = diceLevel(lane_, step);
        setStep(lane_, step, level);
        pat().fromDice[lane_][step] = true;
        if (!project_.playing) audition(lane_, level);
        return true;
    }
    return false; // the lane is full
}

// One fewer: the last of the dice's own hits in the order. Never the player's.
bool PatternScreen::diceRemove() {
    int order[kSteps];
    diceOrder(lane_, order);
    for (int i = kSteps - 1; i >= 0; --i) {
        if (!pat().fromDice[lane_][order[i]]) continue;
        pat().fromDice[lane_][order[i]] = false; // first, so this can never find the same hit twice
        setStep(lane_, order[i], 0.f);
        return true;
    }
    return false;
}

void PatternScreen::diceTurn(float hits) {
    diceSaved_ += hits;
    while (diceSaved_ >= 1.f) {
        diceSaved_ -= 1.f;
        if (!diceAdd()) { diceSaved_ = 0.f; break; } // full: nothing saved up against the stop
    }
    while (diceSaved_ <= -1.f) {
        diceSaved_ += 1.f;
        if (!diceRemove()) { diceSaved_ = 0.f; break; }
    }
}

// The same number of dice hits, in a new shuffle.
void PatternScreen::diceReshuffle() {
    int count = diceHits(lane_);
    while (diceRemove()) {}
    pat().diceSeed[lane_] = pat().diceSeed[lane_] * 1664525u + 1013904223u;
    if (count == 0) count = kFirstRoll;
    const bool wasPlaying = project_.playing;
    project_.playing = true; // sound only the last of the new hits, not all of them at once
    for (int i = 0; i < count; ++i) {
        if (i == count - 1) project_.playing = wasPlaying;
        if (!diceAdd()) break;
    }
    project_.playing = wasPlaying;
}

// ---- the cursor ------------------------------------------------------------

void PatternScreen::moveCursorTo(int lane, int step) {
    onTempo_ = false;
    lane_ = (lane % kDrumLanes + kDrumLanes) % kDrumLanes;
    // Seventeen stops across: the lane's name, then the sixteen steps.
    step_ = ((step + 1) % (kSteps + 1) + (kSteps + 1)) % (kSteps + 1) - 1;
}

void PatternScreen::moveCursor(Button direction) {
    if (onTempo_) {
        // The tempo sits above the top lane; up from it comes round to the bottom.
        if (direction == Button::Down) onTempo_ = false, lane_ = 0;
        if (direction == Button::Up) onTempo_ = false, lane_ = kDrumLanes - 1;
        return;
    }
    if (direction == Button::Up) {
        if (lane_ == 0) onTempo_ = true;
        else --lane_;
    }
    if (direction == Button::Down) moveCursorTo(lane_ + 1, step_);
    // Sideways: the lane's name, then the bar's sixteen steps. Off the right
    // of step 16 is the next bar of the cell; after the last bar, the name
    // again. The name is always one left of step 1, whichever bar is showing.
    const int bars = trk().bars[project_.scene];
    if (direction == Button::Right) {
        if (step_ < kSteps - 1) ++step_; // from the name too: it is -1
        else if (project_.bar + 1 < bars) ++project_.bar, step_ = 0;
        else project_.bar = 0, step_ = kLaneName;
    }
    if (direction == Button::Left) {
        if (step_ >= 0) --step_; // step 1 goes to the name
        else project_.bar = project_.bar > 0 ? project_.bar - 1 : bars - 1, step_ = kSteps - 1;
    }
}

void PatternScreen::dragCursor(Button direction) {
    moveCursor(direction);
    paintHere(); // does nothing unless a brush is down
}

// ---- the stick, with a button held ---------------------------------------------

void PatternScreen::turnUpDown(float amount) {
    if (amount == 0.f) return;
    if (onTempo_) {
        setTempo(tempoExact_ + amount * (feel.tempoPerSecond / feel.fillPerSecond));
    } else if (step_ == kLaneName) {
        setLaneLevel(trk().laneLevel[lane_] + amount);
    } else if (hitHere()) {
        setStep(lane_, step_, clampTo(pat().level[lane_][step_] + amount, kMinHitLevel, 1.f));
        turning_ = Turning::Level;
    }
}

void PatternScreen::turnSideways(float amount) {
    if (amount == 0.f || !hitHere()) return;
    setNudge(lane_, step_, pat().nudge[lane_][step_] + amount);
    turning_ = Turning::Nudge;
}

void PatternScreen::turnChance(float amount) {
    if (amount == 0.f) return;
    if (!onTempo_ && step_ == kLaneName) {
        // On a lane's name, Y is the dice: more hits or fewer.
        diceTurn(amount / feel.fillPerSecond * kDiceHitsPerSecond);
        yDidSomething_ = true;
        return;
    }
    if (!hitHere()) return;
    setChance(lane_, step_, pat().chance[lane_][step_] + amount);
    turning_ = Turning::Chance;
    yDidSomething_ = true;
}

// The D-pad with A or Y held: the same values, in exact steps.
void PatternScreen::nudgeExact(Button direction, bool withY) {
    const float sign = direction == Button::Up || direction == Button::Right ? 1.f : -1.f;
    const bool vertical = direction == Button::Up || direction == Button::Down;
    if (withY) {
        if (vertical && !onTempo_ && step_ == kLaneName) {
            if (sign > 0.f) diceAdd(); // exactly one hit more, or one fewer
            else diceRemove();
            yDidSomething_ = true;
        } else if (vertical && hitHere()) {
            setChance(lane_, step_, nextTenth(pat().chance[lane_][step_], sign));
            turning_ = Turning::Chance;
            yDidSomething_ = true;
        } else if (!vertical && !onTempo_ && step_ == kLaneName) {
            rolled_ = false;
            turnStart(sign); // roll the lane's starts, or put them back
            rolled_ = false; // each press of the D-pad is its own roll
        } else if (!vertical && hitHere()) {
            // Sideways: the next sixteenth of the sound, exactly.
            const float now = pat().start[lane_][step_] * kStartSlices;
            const float to = sign > 0.f ? std::floor(now + 1.001f) : std::ceil(now - 1.001f);
            heardStartAt_ = 0;
            turnStart(to / kStartSlices - pat().start[lane_][step_]);
        }
    } else if (onTempo_) {
        setTempo(project_.tempo + sign * (vertical ? 1.f : 10.f));
    } else if (step_ == kLaneName) {
        if (vertical) setLaneLevel(nextTenth(trk().laneLevel[lane_], sign));
    } else if (hitHere()) {
        if (vertical) {
            setStep(lane_, step_, clampTo(nextTenth(pat().level[lane_][step_], sign), kMinHitLevel, 1.f));
            turning_ = Turning::Level;
        } else {
            // Sideways: a tenth of a step, early or late.
            setNudge(lane_, step_, nextTenth(pat().nudge[lane_][step_] + 0.5f, sign) - 0.5f);
            turning_ = Turning::Nudge;
        }
    }
}

// ---- buttons -------------------------------------------------------------------

void PatternScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (b == Button::L2 || b == Button::R2) {
        // The app sends these here only while A is held: nudge, early or late,
        // a tenth of a step. The same as A with the D-pad sideways.
        if (shaping_) {
            if (event.down && hitHere()) nudgeExact(b == Button::R2 ? Button::Right : Button::Left, false);
            return;
        }
        // R2 while R1 is painting: the brush paints random levels for as
        // long as R2 is held too. (R2 by itself is "next screen"; the app
        // only sends it here with R1 down.)
        if (b != Button::R2) return;
        if (event.down && paint_ == Paint::OneLevel) {
            paint_ = Paint::Random;
            paintHere();
        } else if (!event.down && paint_ == Paint::Random) {
            paint_ = Paint::OneLevel;
        }
        return;
    }
    if (b == Button::A) {
        if (event.down && !onTempo_) {
            if (step_ == kLaneName) audition(lane_, kNewHitLevel);
            else if (!hitHere()) placeHit(1.f);
        }
        shaping_ = event.down;
        axis_ = Axis::None; // a new hold is a new choice of job for the stick
        if (!event.down && !dicing_) turning_ = Turning::Nothing;
        return;
    }
    if (b == Button::Y) {
        if (event.down) {
            yDidSomething_ = false;
            if (onStep() && !hitHere()) {
                placeHit(kMaybe); // a "maybe" hit
                yDidSomething_ = true;
            }
        } else if (!yDidSomething_ && !onTempo_ && step_ == kLaneName) {
            diceReshuffle(); // a plain tap on a lane's name rolls the dice
        } else if (!yDidSomething_ && hitHere()) {
            // A plain tap on a hit steps its chance down a quarter, and round again.
            const float now = pat().chance[lane_][step_];
            setChance(lane_, step_, now > 0.76f ? 0.75f : now > 0.51f ? 0.5f : now > 0.26f ? 0.25f : 1.f);
        }
        dicing_ = event.down;
        diceSaved_ = 0.f;
        axis_ = Axis::None;
        if (!event.down && !shaping_) turning_ = Turning::Nothing;
        return;
    }
    if (b == Button::R1) {
        const Paint mine = Paint::OneLevel;
        if (!event.down) {
            paint_ = Paint::None; // whichever brush it was
        } else if (!shaping_ && !dicing_ && paint_ == Paint::None) {
            paint_ = mine;
            // The brush takes the level of the hit it starts on, or 80%.
            brush_ = hitHere() ? pat().level[lane_][step_] : kNewHitLevel;
            paintHere();
        }
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (input.held(Button::Y)) nudgeExact(b, true);
        else if (input.held(Button::A)) nudgeExact(b, false);
        else dragCursor(b);
        repeating_ = b;
        repeatAt_ = 0; // update() starts the wait, on its own clock
        return;
    }

    const bool busy = input.held(Button::A) || input.held(Button::Y);
    if (b == Button::B && !busy) {
        if (hitHere()) setStep(lane_, step_, 0.f);
    } else if (b == Button::X && !onTempo_) {
        audition(lane_, hitHere() ? pat().level[lane_][step_] : kNewHitLevel, hitHere() ? pat().start[lane_][step_] : 0.f);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false); // loop this scene
    }
}

void PatternScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number // a stalled frame must not fling anything
    lastMs_ = nowMs;

    const float x = input.stickX(), y = input.stickY();
    if (project_.bar >= trk().bars[project_.scene]) project_.bar = 0; // the cell was shortened, or it is another cell
    // The tempo can be set from elsewhere (SELECT + D-pad, or a song opened).
    if (std::round(tempoExact_) != project_.tempo) tempoExact_ = project_.tempo;

    if (shaping_ || dicing_) {
        // The stick does one job per push. Whichever way it leaves the centre
        // is the job until it comes back, so setting a level cannot also
        // slide the hit sideways.
        if (x == 0.f && y == 0.f) axis_ = Axis::None, rolled_ = false;
        else if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;

        // Squared, so a small push is a fine adjustment and a full push is fast.
        const float up = -y * std::fabs(y) * feel.fillPerSecond * dt;
        const float right = x * std::fabs(x) * feel.fillPerSecond * dt;
        if (dicing_) {
            if (axis_ == Axis::UpDown) turnChance(up);
            else if (axis_ == Axis::Sideways) turnStart(right * 0.5f);
        } else if (axis_ == Axis::UpDown) {
            turnUpDown(up);
        } else if (axis_ == Axis::Sideways) {
            turnSideways(right * 0.5f); // the whole range is one step wide: half the speed is plenty
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else {
        axis_ = Axis::None;
        // Nothing held: the stick runs the cursor. One move at once, then at
        // a speed set by how far it is pushed.
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * feel.stickCellsPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) dragCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * feel.stickCellsPerSecond * 0.5f * dt : 1.f; // eight lanes: half speed is plenty
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) dragCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    // A held D-pad direction repeats.
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + feel.repeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + feel.repeatEveryMs;
        if (input.held(Button::Y)) nudgeExact(repeating_, true);
        else if (input.held(Button::A)) nudgeExact(repeating_, false);
        else dragCursor(repeating_);
    }
}

// ---- drawing ---------------------------------------------------------------------

void PatternScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64];
    const bool onName = !onTempo_ && step_ == kLaneName;
    const bool held = shaping_ || dicing_;
    const SDL_Color cursorColor = held || paint_ != Paint::None ? kBrass : kTeal;

    // ---- top bar -----------------------------------------------------------
    char context[96], name[24];
    trackName(project_, project_.track, name, sizeof name);
    if (trk().bars[project_.scene] > 1) std::snprintf(context, sizeof context, "%s / BAR %d OF %d / SCENE %d", name, project_.bar + 1, trk().bars[project_.scene], project_.scene + 1);
    else std::snprintf(context, sizeof context, "%s / SCENE %d", name, project_.scene + 1);
    const TopBar top = topBar(r, fonts, View::Pattern, context, project_);
    if (onTempo_) {
        // The tempo fills too: its box is a bar from the slowest to the fastest.
        const int bx = static_cast<int>(top.tempoNumberRight - fonts.small.width("240") - 6.f);
        const int bw = static_cast<int>(top.tempoRight) - bx + 6;
        const float part = (tempoExact_ - kMinTempo) / (kMaxTempo - kMinTempo);
        fill(r, bx, 3, bw, kTopBarH - 6, kPanel2);
        fill(r, bx, 3, static_cast<int>(std::round(bw * part)), kTopBarH - 6, faded(cursorColor, 90));
        outline(r, bx - 1, 2, bw + 2, kTopBarH - 4, cursorColor, 2);
        const float textY = (kTopBarH - fonts.small.capHeight()) / 2.f;
        std::snprintf(text, sizeof text, "%d", static_cast<int>(project_.tempo));
        fonts.small.drawRight(r, top.tempoRight, textY, "BPM", kText);
        fonts.small.drawRight(r, top.tempoNumberRight, textY, text, kText);
    }

    // ---- step numbers --------------------------------------------------------
    for (int s = 0; s < kSteps; ++s) {
        std::snprintf(text, sizeof text, "%d", s + 1);
        fonts.small.drawCentered(r, kGridLeft + s * kStepW + kCellW / 2.f, 36.f, text, s % 4 == 0 ? kText : kDim);
    }

    // ---- lane names and empty cells ----------------------------------------------
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kLaneH;
        const bool nameHere = onName && lane == lane_;

        // The lane's name is also its volume: the box is filled as far as the
        // drum is turned up.
        box(r, kNameX, y, kNameW, kCellH, kBg, kLine);
        const int filled = static_cast<int>(std::round((kNameW - 2) * trk().laneLevel[lane]));
        if (nameHere && shaping_) {
            SDL_Color bar = kBrass;
            bar.a = 110;
            fill(r, kNameX + 1, y + 1, filled, kCellH - 2, bar);
        } else {
            fill(r, kNameX + 1, y + 1, filled, kCellH - 2, kPanel);
        }
        if (nameHere) outline(r, kNameX - 1, y - 1, kNameW + 2, kCellH + 2, cursorColor, 2);
        fonts.medium.draw(r, 15.f, y + (kCellH - fonts.medium.capHeight()) / 2.f, laneLabel(lane),
                          !onTempo_ && lane == lane_ ? kTeal : kText);

        for (int s = 0; s < kSteps; ++s) {
            box(r, kGridLeft + s * kStepW, y, kCellW, kCellH, s % 4 == 0 ? kPanel2 : kPanel, kLine);
        }
    }

    // ---- hits ------------------------------------------------------------------------
    // Drawn after every cell, because a nudged hit sits across a cell's edge.
    // One block says three things: how tall is how loud, where it sits is
    // when it plays, and how solid is how likely.
    const int gridRight = kGridLeft + kSteps * kStepW - 2;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kLaneH;
        for (int s = 0; s < kSteps; ++s) {
            const float level = pat().level[lane][s];
            if (level <= 0.f) continue;
            const bool live = held && onStep() && lane == lane_ && s == step_;
            int bx = kGridLeft + s * kStepW + 4 + static_cast<int>(std::round(pat().nudge[lane][s] * kStepW));
            if (bx < kGridLeft) bx = kGridLeft;
            if (bx + kBlockW > gridRight) bx = gridRight - kBlockW;
            int h = static_cast<int>(std::round(level * kBlockFull));
            if (h < 1) h = 1;

            SDL_Color body = live ? kTeal : kText;
            const float chance = pat().chance[lane][s];
            body.a = static_cast<Uint8>(std::round(255.f * (0.12f + 0.88f * chance * chance)));
            if (live) outline(r, bx, y + 4, kBlockW, kBlockFull, kDim); // the vessel being filled
            fill(r, bx, y + 4 + kBlockFull - h, kBlockW, h, body);
            // A hit that might not play keeps its edge, so a ghost is still findable.
            if (chance < 1.f) outline(r, bx, y + 4 + kBlockFull - h, kBlockW, h, live ? kTeal : kText);
            // On a lane's name, the dice's own hits wear a brass cap: those are
            // the ones the stick will take away again.
            if (onName && lane == lane_ && pat().fromDice[lane][s]) fill(r, bx, y + 4 + kBlockFull - h, kBlockW, 3, kBrass);
            // A hit that starts part-way into its sound: a line under the
            // block for the whole sound, and a mark where this hit comes in.
            const float start = pat().start[lane][s];
            if (start > 0.f) {
                const SDL_Color mark = live && turning_ == Turning::Start ? kBrass : kPurple;
                fill(r, bx, y + kCellH - 3, kBlockW, 1, kDim);
                fill(r, bx + static_cast<int>(std::round(start * (kBlockW - 3))), y + kCellH - 5, 3, 4, mark);
            }
        }
    }
    if (onStep()) {
        outline(r, kGridLeft + step_ * kStepW - 1, kGridTop + lane_ * kLaneH - 1, kCellW + 2, kCellH + 2, cursorColor, 2);
    }
    // The play line shows when the bar on screen is the one sounding: a short cell goes round under a long scene.
    const int playStep = engine_.playScene() == project_.scene && engine_.playBar() % trk().bars[project_.scene] == project_.bar ? engine_.playStep() : -1;
    if (playStep >= 0) {
        SDL_Color glow = kPurple;
        glow.a = 76;
        fill(r, kGridLeft + playStep * kStepW, kGridTop, kCellW, kDrumLanes * kLaneH - 3, glow);
    }

    // ---- what is under the cursor, spelled out -----------------------------------
    const int stripY = kGridTop + kDrumLanes * kLaneH + 6;
    const auto field = [&](float fx, const char* name, const char* value, SDL_Color color) {
        fonts.small.draw(r, fx, stripY, name, kDim);
        fonts.large.draw(r, fx, stripY + 16.f, value, color);
    };
    const auto bar = [&](int bx, float part, SDL_Color color) {
        fill(r, bx, stripY + 20, 150, 6, kLine);
        if (part > 0.f) fill(r, bx, stripY + 20, static_cast<int>(std::round(150 * part)), 6, color);
    };
    if (onTempo_) {
        field(8.f, "SONG", "TEMPO", kText);
        std::snprintf(text, sizeof text, "%d", static_cast<int>(project_.tempo));
        field(134.f, "BPM", text, cursorColor);
        bar(244, (tempoExact_ - kMinTempo) / (kMaxTempo - kMinTempo), cursorColor);
    } else if (onName) {
        field(8.f, "LANE", laneLabel(lane_), kText);
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(trk().laneLevel[lane_] * 100.f)));
        field(134.f, "DRUM VOLUME", text, dicing_ ? kText : cursorColor);
        bar(244, trk().laneLevel[lane_], dicing_ ? kDim : cursorColor);
        std::snprintf(text, sizeof text, "%d", diceHits(lane_));
        field(430.f, "DICE HITS", text, dicing_ ? kBrass : kText);
    } else {
        field(8.f, "LANE", laneLabel(lane_), kText);
        std::snprintf(text, sizeof text, "%d", step_ + 1);
        field(120.f, "STEP", text, kText);
        if (!hitHere()) {
            field(190.f, "LEVEL", "-", kDim);
            field(290.f, "NUDGE", "-", kDim);
            field(420.f, "CHANCE", "-", kDim);
            field(520.f, "START", "-", kDim);
        } else {
            // The one the stick has hold of is brass; the others are quiet.
            const auto shade = [&](Turning t) { return turning_ == t ? kBrass : (turning_ == Turning::Nothing ? kTeal : kText); };
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(pat().level[lane_][step_] * 100.f)));
            field(190.f, "LEVEL", text, shade(Turning::Level));
            const int n = static_cast<int>(std::round(pat().nudge[lane_][step_] * 100.f));
            if (n == 0) std::snprintf(text, sizeof text, "ON GRID");
            else std::snprintf(text, sizeof text, "%d%% %s", std::abs(n), n < 0 ? "EARLY" : "LATE");
            field(290.f, "NUDGE", text, shade(Turning::Nudge));
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(pat().chance[lane_][step_] * 100.f)));
            field(420.f, "CHANCE", text, shade(Turning::Chance));
            // Where in the sound it begins, with a picture of the sound when there is one.
            const float start = pat().start[lane_][step_];
            if (start <= 0.f) std::snprintf(text, sizeof text, "THE TOP");
            else std::snprintf(text, sizeof text, "%d%% IN", static_cast<int>(std::round(start * 100.f)));
            field(520.f, "START", text, shade(Turning::Start));
            const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);
            if (!sound.peaks.empty() && (turning_ == Turning::Start || start > 0.f)) {
                const int wx = 8, wy = stripY + 38, ww = 624, wh = 20;
                const int count = static_cast<int>(sound.peaks.size());
                for (int i = 0; i < count; ++i) {
                    const int px = wx + ww * i / count, ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (wh - 1));
                    const bool played = static_cast<float>(i) / count >= start;
                    fill(r, px, wy + (wh - ph) / 2, ww / count, ph, played ? kText : kLine);
                }
                fill(r, wx + static_cast<int>(start * ww), wy - 2, 2, wh + 4, turning_ == Turning::Start ? kBrass : kPurple);
            }
        }
    }

    // ---- hints -------------------------------------------------------------------
    // Only what does something here, and what the ONE stick does right now.
    Hint hints[7];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { hints[count].key = key; hints[count].does = does; ++count; };
    char brushText[40];
    if (paint_ == Paint::OneLevel) {
        std::snprintf(brushText, sizeof brushText, "paint hits at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG WITH THE STICK", brushText);
        hint("+ R2", "random levels");
    } else if (paint_ == Paint::Random) {
        hint("DRAG WITH THE STICK", "paint hits at random levels");
    } else if (shaping_ && onStep()) {
        hint("STICK UP / DOWN", "level");
        hint("STICK LEFT / RIGHT", "nudge");
        hint("L2 / R2", "nudge early / late");
    } else if (dicing_ && onStep()) {
        hint("STICK UP / DOWN", "chance");
        hint("STICK LEFT / RIGHT", "where the sound starts");
        hint("TAP Y", "100 > 75 > 50 > 25");
    } else if (dicing_ && onName) {
        hint("UP / DOWN", "more / fewer hits");
        hint("RIGHT", "roll the starts");
        hint("LEFT", "starts to the top");
        hint("TAP Y", "reshuffle");
    } else if (shaping_) {
        hint("STICK UP / DOWN", onTempo_ ? "tempo" : "drum volume");
    } else if (onTempo_) {
        hint("HOLD A + STICK", "tempo");
        hint("START", project_.playing ? "stop" : "play");
    } else if (onName) {
        hint("HOLD A", "volume");
        hint("HOLD Y", "dice");
        hint("TAP Y", "roll");
    } else {
        hint("A", hitHere() ? "level / nudge" : "place");
        hint("Y", hitHere() ? "chance / start" : "maybe");
        if (hitHere()) hint("B", "delete");
        hint("HOLD R1", "paint");
        hint("R1 + R2", "paint random");
    }
    ui::hints(r, fonts, hints, count, !onTempo_);
}

} // namespace apotheca
