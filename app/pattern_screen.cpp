#include "pattern_screen.hpp"
#include "pattern_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

namespace {

using namespace ui;

using namespace pattern_layout;

constexpr float kNewHitLevel = 0.8f;
constexpr float kMinHitLevel = 0.05f; // B deletes; the stick cannot
constexpr float kMinChance = 0.05f;   // likewise
constexpr float kMaybe = 0.5f;        // the chance of a hit placed with Y
constexpr float kRandomLow = 0.25f;   // R2 paints levels from here up to full
constexpr int kFirstRoll = 4;         // hits a tap of Y gives an empty lane
constexpr float kDiceHitsPerSecond = 24.f; // stick fully over
constexpr Uint32 kHearStartEveryMs = 90;  // a sliding start is played this often
constexpr float kPitchClicksPerSecond = 8.f, kRatchetClicksPerSecond = 4.f; // X held, the stick or D-pad held over


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
        setFx(lane, step, StepFx());
    }
}

// A hit's step effects, held to what there is.
void PatternScreen::setFx(int lane, int step, StepFx fx) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    fx.pitch = static_cast<std::int8_t>(fx.pitch < -kMaxStepPitch ? -kMaxStepPitch : (fx.pitch > kMaxStepPitch ? kMaxStepPitch : fx.pitch));
    fx.ratchet = static_cast<std::uint8_t>(fx.ratchet < 2 ? 0 : (fx.ratchet > kMaxRatchet ? kMaxRatchet : fx.ratchet)); // "one hit" is written one way: 0
    fx.reverse = fx.reverse ? 1 : 0;
    fx.cut = static_cast<std::uint8_t>(fx.cut < kCutChoices ? fx.cut : 0);
    fx.throwTo = static_cast<std::uint8_t>(fx.throwTo < kThrowChoices ? fx.throwTo : 0);
    StepFx& now = pat().fx[lane][step];
    if (now.pitch == fx.pitch && now.ratchet == fx.ratchet && now.reverse == fx.reverse && now.cut == fx.cut && now.throwTo == fx.throwTo) return;
    now = fx;
    engine_.send(stepFxCommand(project_.track, project_.scene, project_.bar, lane, step, fx));
}

// With X held on a hit, one click. Up and down: its pitch, a semitone. Left
// and right: how many times it is played inside its step. Heard at once.
void PatternScreen::fxTurn(Button direction) {
    if (!hitHere()) return;
    StepFx fx = pat().fx[lane_][step_];
    if (direction == Button::Up && fx.pitch < kMaxStepPitch) ++fx.pitch;
    if (direction == Button::Down && fx.pitch > -kMaxStepPitch) --fx.pitch;
    if (direction == Button::Right && fx.hits() < kMaxRatchet) fx.ratchet = static_cast<std::uint8_t>(fx.hits() + 1);
    if (direction == Button::Left && fx.hits() > 1) fx.ratchet = static_cast<std::uint8_t>(fx.hits() - 1);
    const StepFx before = pat().fx[lane_][step_];
    setFx(lane_, step_, fx);
    const StepFx& after = pat().fx[lane_][step_];
    if ((after.pitch != before.pitch || after.ratchet != before.ratchet) && !project_.playing) hearHit();
}

// With X held on a hit, A steps its THROW round: OFF, ECHO, SPACE, BOTH. That hit alone is then
// sent in full to the bus, whatever the track's sends say. Heard at once.
void PatternScreen::fxThrow() {
    if (!hitHere()) return;
    StepFx fx = pat().fx[lane_][step_];
    fx.throwTo = static_cast<std::uint8_t>(fx.throwTo + 1); // past the last, setFx makes it "off" again
    setFx(lane_, step_, fx);
    if (!project_.playing) hearHit();
}

// With X held on a hit, Y rolls the dice for what it does: each roll is something, and
// something else than it was. Mostly one effect, sometimes two; small pitches more often than big.
// The dice do not throw: a hit's THROW is left as the player set it.
void PatternScreen::fxRoll() {
    if (!hitHere()) return;
    const StepFx before = pat().fx[lane_][step_];
    const auto roll = [&](std::uint32_t outOf) {
        dice_ = dice_ * 1664525u + 1013904223u;
        return (dice_ >> 12) % outOf;
    };
    static const std::int8_t pitches[] = {-12, -7, -5, -3, -2, -1, 1, 2, 3, 5, 7, 12};
    static const std::uint8_t ratchets[] = {2, 2, 3, 4};
    StepFx fx;
    for (;;) { // (there is always another roll to be had, so this ends)
        fx = StepFx();
        if (roll(100) < 45) fx.ratchet = ratchets[roll(4)];
        if (roll(100) < 40) fx.pitch = pitches[roll(12)];
        if (roll(100) < 15) fx.reverse = 1;
        if (roll(100) < 25) fx.cut = static_cast<std::uint8_t>(1 + roll(kCutChoices - 1));
        const bool same = fx.pitch == before.pitch && fx.hits() == before.hits() && fx.reverse == before.reverse && fx.cut == before.cut;
        if (fx.any() && !same) break;
    }
    fx.throwTo = before.throwTo;
    setFx(lane_, step_, fx);
    if (!project_.playing) hearHit();
}

void PatternScreen::hearHit() {
    if (!hitHere()) return;
    audition(lane_, pat().level[lane_][step_], pat().start[lane_][step_], slicing_ ? 1.f / static_cast<float>(slices()) : 0.f, pat().fx[lane_][step_]);
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

void PatternScreen::audition(int lane, float level, float start, float part, const StepFx& fx) {
    Command c;
    c.type = Command::Trigger;
    c.track = project_.track;
    c.a = lane;
    c.b = static_cast<int>(std::round(start * 10000.f));
    c.c = static_cast<int>(std::round(part * 10000.f));
    c.d = fx.pitch; // and its step effects, so it is heard as the pattern will play it
    c.bar = fx.hits() > 1 ? fx.hits() : 0;
    c.scene = fx.reverse ? 1 : 0;
    c.extra = fx.cutAndThrow();
    c.value = level;
    engine_.send(c);
}

// Where in the lane's sound a hit begins. Never so late that nothing is left.
void PatternScreen::setStart(int lane, int step, float start) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    // The last slice is always left to hear: a sixteenth, or less of a recording cut finer.
    const float slices = static_cast<float>(session_.laneSlices(project_.track, lane));
    start = clampTo(start, 0.f, (slices - 1.f) / slices);
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
// slices of the sound; left puts them all back at the beginning (in the
// slice view: back in order, each on its own slice).
void PatternScreen::turnStart(float amount) {
    if (amount == 0.f || onTempo_) return;
    if (step_ == kLaneName) {
        if (rolled_) return; // one roll per push
        rolled_ = true;
        yDidSomething_ = true;
        for (int step = 0; step < kSteps; ++step) {
            if (pat().level[lane_][step] <= 0.f) continue;
            dice_ = dice_ * 1664525u + 1013904223u;
            const std::uint32_t count = static_cast<std::uint32_t>(slices());
            setStart(lane_, step, amount > 0.f ? static_cast<float>((dice_ >> 12) % count) / static_cast<float>(count) : slicing_ ? ownStart(step) : 0.f);
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
        hearHit();
    }
}

// ---- the slice view --------------------------------------------------------------

bool PatternScreen::canSlice(int lane) const {
    if (lane < 0 || lane >= kDrumLanes || trk().kind != TrackKind::Drums) return false;
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane);
    return trk().laneSample[lane][0] && !sound.missing && !sound.peaks.empty();
}

void PatternScreen::setSlicing(bool on) {
    if (!on) {
        slicing_ = false;
        return;
    }
    if (onTempo_) return;
    if (canSlice(lane_)) {
        slicing_ = true;
        said_[0] = 0;
        return;
    }
    std::snprintf(said_, sizeof said_, "LANE %d IS A BUILT-IN DRUM. PUT A RECORDING ON IT TO SLICE IT.", lane_ + 1);
    saidUntil_ = lastMs_ + 3000;
}

float PatternScreen::ownStart(int step) const {
    const int count = slices();
    return static_cast<float>((project_.bar * kSteps + step) % count) / static_cast<float>(count);
}

int PatternScreen::sliceOf(int step) const {
    const int count = slices();
    const int slice = static_cast<int>(pat().start[lane_][step] * static_cast<float>(count)); // exact: there are 16, 32 or 64
    return slice < 0 ? 0 : (slice >= count ? count - 1 : slice);
}

// Up and down in the slice view. On a hit: the next slice, or the one before,
// round and round, heard as it goes. On the lane's name: whether a hit plays
// one slice and stops, or runs on.
void PatternScreen::sliceUpDown(int by) {
    if (step_ == kLaneName) {
        session_.setLaneOneSlice(project_.track, lane_, !trk().laneOneSlice[lane_]);
        return;
    }
    if (!hitHere()) return;
    const int count = slices();
    const float now = pat().start[lane_][step_] * static_cast<float>(count);
    // From between two lines, up is the next line and down is the one just passed.
    int to = static_cast<int>(by > 0 ? std::floor(now + 1.001f) : std::ceil(now - 1.001f));
    to = (to % count + count) % count;
    setStart(lane_, step_, static_cast<float>(to) / static_cast<float>(count));
    if (!project_.playing && lastMs_ - heardStartAt_ >= kHearStartEveryMs) {
        heardStartAt_ = lastMs_;
        hearHit();
    }
}

const char* PatternScreen::laneLabel(int lane) const {
    const char* file = trk().laneSample[lane];
    if (!file[0]) return DrumKit::laneName(lane);
    static char labels[kDrumLanes][12];
    if (const char* slash = std::strrchr(file, '/')) file = slash + 1; // its own name, not the folder it is in
    int n = 0;
    for (; file[n] && n < 9; ++n) labels[lane][n] = static_cast<char>(file[n] >= 'a' && file[n] <= 'z' ? file[n] - 32 : file[n]);
    labels[lane][n] = 0;
    return labels[lane];
}

void PatternScreen::placeHit(float chance) {
    setStep(lane_, step_, kNewHitLevel);
    setChance(lane_, step_, chance);
    // In the slice view a new hit plays its step's own slice: step 5, slice 5.
    if (slicing_) setStart(lane_, step_, ownStart(step_));
    if (!project_.playing) audition(lane_, kNewHitLevel, pat().start[lane_][step_], slicing_ ? 1.f / static_cast<float>(slices()) : 0.f);
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
    if (slicing_) {
        // The slice view's brushes: every step crossed gets a hit on its own slice
        // (the break, in order), or with R2 on a slice picked at random (the break,
        // shuffled). A hit that is there keeps its level.
        const float level = hitHere() ? pat().level[lane_][step_] : brush_;
        const float count = static_cast<float>(slices());
        dice_ = dice_ * 1664525u + 1013904223u;
        const float start = paint_ == Paint::OneLevel ? ownStart(step_) : static_cast<float>((dice_ >> 12) % static_cast<std::uint32_t>(slices())) / count;
        if (!hitHere()) setStep(lane_, step_, level);
        setStart(lane_, step_, start);
        if (!project_.playing) audition(lane_, level, pat().start[lane_][step_], 1.f / count);
        return;
    }
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
    if (slicing_ && (direction == Button::Up || direction == Button::Down)) {
        sliceUpDown(direction == Button::Up ? 1 : -1); // one lane is showing: up and down are the slice
        return;
    }
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
    if (slicing_ && (direction == Button::Up || direction == Button::Down)) {
        moveCursor(direction); // a slice picked: nothing is painted or deleted by that
        return;
    }
    moveCursor(direction);
    if (erasing_ && hitHere()) setStep(lane_, step_, 0.f); // B is held: the cursor rubs hits away
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
            // Sideways: the next slice of the sound, exactly.
            const float count = static_cast<float>(slices());
            const float now = pat().start[lane_][step_] * count;
            const float to = sign > 0.f ? std::floor(now + 1.001f) : std::ceil(now - 1.001f);
            heardStartAt_ = 0;
            turnStart(to / count - pat().start[lane_][step_]);
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
    if (fxHeld_ && (b == Button::L1 || b == Button::R1)) {
        // With X held on a hit: L1 turns it backwards and back, R1 steps its cut round.
        if (event.down && hitHere()) {
            StepFx fx = pat().fx[lane_][step_];
            if (b == Button::L1) fx.reverse = fx.reverse ? 0 : 1;
            else fx.cut = static_cast<std::uint8_t>(fx.cut + 1); // past the last, setFx makes it "off" again
            setFx(lane_, step_, fx);
            if (!project_.playing) hearHit();
        }
        return;
    }
    if (fxHeld_ && b == Button::Y) {
        if (event.down) fxRoll(); // the dice, for what this hit does. (Y's own jobs wait until X is let go.)
        return;
    }
    if (fxHeld_ && b == Button::A) {
        if (event.down) fxThrow(); // THROW, a step round. (A's own jobs, placing and shaping, wait until X is let go.)
        return;
    }
    if (b == Button::X) {
        // Hears what is under the cursor, as everywhere. On a hit it is also a hold: the stick then sets its step effects.
        fxHeld_ = event.down && hitHere() && !shaping_ && !dicing_ && paint_ == Paint::None;
        fxSaved_ = 0.f;
        fxWas_ = false;
        // A direction that was already held (the cursor was on its way here) is not a turn of the
        // effect: X only heard the hit. It must be let go and pushed again.
        fxWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
        if (fxHeld_) repeating_ = Button::None;
        axis_ = Axis::None;
        if (!event.down || onTempo_) return;
        if (hitHere()) hearHit();
        else if (slicing_ && onStep()) audition(lane_, kNewHitLevel, ownStart(step_), 1.f / static_cast<float>(slices())); // the slice a hit placed here would play
        else audition(lane_, kNewHitLevel);
        return;
    }
    if (b == Button::L1) {
        // Into the cursor's lane, as slices; and back out to the whole grid.
        if (event.down && !shaping_ && !dicing_ && paint_ == Paint::None) setSlicing(!slicing_);
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
        if (b == Button::B) erasing_ = false;
        return;
    }

    if (isDirection(b)) {
        if (fxHeld_) fxTurn(b);
        else if (input.held(Button::Y)) nudgeExact(b, true);
        else if (input.held(Button::A)) nudgeExact(b, false);
        else dragCursor(b);
        repeating_ = b;
        repeatAt_ = 0; // update() starts the wait, on its own clock
        return;
    }

    const bool busy = input.held(Button::A) || input.held(Button::Y);
    if (b == Button::B && !busy) {
        // Deletes the hit under the cursor and, held, every hit the cursor is dragged over.
        erasing_ = true;
        if (hitHere()) setStep(lane_, step_, 0.f);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false); // loop this scene
    }
}

void PatternScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) shaping_ = false;
    if (!input.held(Button::Y)) dicing_ = false;
    if (!input.held(Button::R1)) paint_ = Paint::None;
    if (!input.held(Button::B)) erasing_ = false;
    if (!input.held(Button::X) || !hitHere()) fxHeld_ = false;

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number // a stalled frame must not fling anything
    lastMs_ = nowMs;

    const float x = input.stickX(), y = input.stickY();
    if (project_.bar >= trk().bars[project_.scene]) project_.bar = 0; // the cell was shortened, or it is another cell
    // Another screen (the kit's) asked for a lane's slices.
    if (session_.wantSlices >= 0) {
        moveCursorTo(session_.wantSlices, step_ < 0 ? 0 : step_);
        session_.wantSlices = -1;
        setSlicing(true);
    }
    if (slicing_ && !canSlice(lane_)) slicing_ = false; // the recording went, or this is another track
    // The tempo can be set from elsewhere (SELECT + D-pad, or a song opened).
    if (std::round(tempoExact_) != project_.tempo) tempoExact_ = project_.tempo;

    if (fxHeld_) {
        // X is held on a hit: the stick clicks its step effects. One way per push (up / down is the
        // pitch, sideways the ratchet); a click at once, then a steady run while it is held over.
        if (x == 0.f && y == 0.f) {
            axis_ = Axis::None;
            fxSaved_ = 0.f;
            fxWas_ = false;
            fxWait_ = false;
        } else if (!fxWait_) {
            if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
            const float push = axis_ == Axis::UpDown ? -y : x;
            if (push != 0.f) {
                fxSaved_ += fxWas_ ? std::fabs(push) * (axis_ == Axis::UpDown ? kPitchClicksPerSecond : kRatchetClicksPerSecond) * dt : 1.f;
                fxWas_ = true;
                for (; fxSaved_ >= 1.f; fxSaved_ -= 1.f)
                    fxTurn(axis_ == Axis::UpDown ? (push > 0.f ? Button::Up : Button::Down) : (push > 0.f ? Button::Right : Button::Left));
            }
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else if (shaping_ || dicing_) {
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
        if (fxHeld_) {
            // Clicks, not a slide: slower than the cursor runs.
            const bool vertical = repeating_ == Button::Up || repeating_ == Button::Down;
            repeatAt_ = nowMs + static_cast<Uint32>(1000.f / (vertical ? kPitchClicksPerSecond : kRatchetClicksPerSecond));
            fxTurn(repeating_);
        } else if (input.held(Button::Y)) nudgeExact(repeating_, true);
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
    if (trk().bars[project_.scene] > 1) std::snprintf(context, sizeof context, "%s / %sBAR %d OF %d / SCENE %d", name, slicing_ ? "SLICES / " : "", project_.bar + 1, trk().bars[project_.scene], project_.scene + 1);
    else std::snprintf(context, sizeof context, "%s / %sSCENE %d", name, slicing_ ? "SLICES / " : "", project_.scene + 1);
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

    if (slicing_) {
        drawSlices(r, fonts); // one lane, with its recording cut into slices (pattern_slices.cpp)
    } else {
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
            drawFxMarks(r, pat().fx[lane][s], bx, y + 4 + kBlockFull - h, kBlockW, h, y + kCellH / 2, 1.1f);
        }
    }
    if (onStep()) {
        outline(r, kGridLeft + step_ * kStepW - 1, kGridTop + lane_ * kLaneH - 1, kCellW + 2, kCellH + 2, cursorColor, 2);
    }
    // The play line shows when the bar on screen is the one sounding: a short cell goes round under a long scene.
    const int playStep = session_.playScene() == project_.scene && engine_.playBar() % trk().bars[project_.scene] == project_.bar ? engine_.playStep() : -1;
    if (playStep >= 0) {
        SDL_Color glow = kPurple;
        glow.a = 76;
        fill(r, kGridLeft + playStep * kStepW, kGridTop, kCellW, kDrumLanes * kLaneH - 3, glow);
    }

    }

    // ---- what is under the cursor, spelled out -----------------------------------
    // A scene of fewer than sixteen steps: the steps past its end are never played, and are shaded.
    if (const int steps = project_.barSteps(project_.scene, project_.bar); steps < kSteps && !slicing_)
        fill(r, kGridLeft + steps * kStepW - 1, kGridTop - 14, (kSteps - steps) * kStepW, kDrumLanes * kLaneH + 12, faded(kBg, 185));
    const int stripY = kStripY;
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
        fonts.small.draw(r, 8.f, stripY, "LANE", kDim);
        fonts.fit(laneLabel(lane_), 104.f).draw(r, 8.f, stripY + 16.f, laneLabel(lane_), kText);
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(trk().laneLevel[lane_] * 100.f)));
        field(134.f, "DRUM VOLUME", text, dicing_ ? kText : cursorColor);
        bar(244, trk().laneLevel[lane_], dicing_ ? kDim : cursorColor);
        if (slicing_) {
            field(430.f, "EACH HIT PLAYS", trk().laneOneSlice[lane_] ? "ONE SLICE" : "RUNS ON", trk().laneOneSlice[lane_] ? kPurple : kText);
        } else {
            std::snprintf(text, sizeof text, "%d", diceHits(lane_));
            field(430.f, "DICE HITS", text, dicing_ ? kBrass : kText);
        }
    } else {
        fonts.small.draw(r, 8.f, stripY, "LANE", kDim);
        fonts.fit(laneLabel(lane_), 104.f).draw(r, 8.f, stripY + 16.f, laneLabel(lane_), kText);
        std::snprintf(text, sizeof text, "%d", step_ + 1);
        field(120.f, "STEP", text, kText);
        if (!hitHere()) {
            field(190.f, "LEVEL", "-", kDim);
            field(290.f, "NUDGE", "-", kDim);
            field(420.f, "CHANCE", "-", kDim);
            field(520.f, slicing_ ? "SLICE" : "START", "-", kDim);
        } else if (fxHeld_) {
            // X is held: the strip is the hit's step effects, and nothing else.
            const StepFx& fx = pat().fx[lane_][step_];
            if (fx.pitch == 0) std::snprintf(text, sizeof text, "0");
            else std::snprintf(text, sizeof text, "%+d", fx.pitch);
            // (Five of them: closer together than the four a hit has when X is not held.)
            field(190.f, "PITCH", text, fx.pitch ? kBrass : kText);
            std::snprintf(text, sizeof text, fx.hits() > 1 ? "x%d" : "OFF", fx.hits());
            field(260.f, "RATCHET", text, fx.hits() > 1 ? kBrass : kText);
            field(355.f, "REVERSE", fx.reverse ? "ON" : "OFF", fx.reverse ? kBrass : kText);
            field(450.f, "CUT", fx.cut == 1 ? "1 STEP" : fx.cut == 2 ? "1/2 STEP" : fx.cut == 3 ? "1/4 STEP" : "OFF", fx.cut ? kBrass : kText);
            field(555.f, "THROW", throwName(fx.throwTo), fx.throwTo ? kBrass : kText);
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
            if (slicing_) {
                // Which slice, and how far past its cut if the start has been slid off it.
                const float into = start * slices() - sliceOf(step_);
                if (into > 0.01f) std::snprintf(text, sizeof text, "%d + %d%%", sliceOf(step_) + 1, static_cast<int>(std::round(into * 100.f)));
                else std::snprintf(text, sizeof text, "%d OF %d", sliceOf(step_) + 1, slices());
            } else if (start <= 0.f) std::snprintf(text, sizeof text, "THE TOP");
            else std::snprintf(text, sizeof text, "%d%% IN", static_cast<int>(std::round(start * 100.f)));
            field(520.f, slicing_ ? "SLICE" : "START", text, shade(Turning::Start));
            // Its step effects, in a line: what X held would show in full.
            const StepFx& fx = pat().fx[lane_][step_];
            if (fx.any()) {
                std::string summary;
                const auto add = [&](const char* word) { summary += (summary.empty() ? "" : "   ") + std::string(word); };
                if (fx.pitch) std::snprintf(text, sizeof text, "PITCH %+d", fx.pitch), add(text);
                if (fx.hits() > 1) std::snprintf(text, sizeof text, "RATCHET x%d", fx.hits()), add(text);
                if (fx.reverse) add("REVERSE");
                if (fx.cut) add(fx.cut == 1 ? "CUT 1 STEP" : fx.cut == 2 ? "CUT 1/2" : "CUT 1/4");
                if (fx.throwTo) std::snprintf(text, sizeof text, "THROW %s", throwName(fx.throwTo)), add(text);
                fonts.small.drawRight(r, 632.f, stripY + 44.f, summary.c_str(), kPurple);
            }
            const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);
            if (!slicing_ && !sound.peaks.empty() && (turning_ == Turning::Start || start > 0.f)) {
                const int wx = 8, wy = stripY + 38, ww = fx.any() ? 300 : 624, wh = 20; // narrower, to leave the effects their room
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

    // Something said for a moment (why L1 did not open the slices).
    if (said()[0]) fonts.small.draw(r, 8.f, stripY + 44.f, said(), kBrass);

    // ---- hints -------------------------------------------------------------------
    // Only what does something here, and what the ONE stick does right now.
    Hint hints[7];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { hints[count].key = key; hints[count].does = does; ++count; };
    char brushText[40];
    if (fxHeld_) {
        hint("UP / DOWN", "pitch");
        hint("LEFT / RIGHT", "ratchet");
        hint("L1", "reverse");
        hint("R1", "cut");
        hint("A", "throw");
        hint("Y", "dice");
    } else if (slicing_ && paint_ == Paint::OneLevel) {
        hint("DRAG", "lay the break out in order");
        hint("+ R2", "shuffled");
    } else if (slicing_ && paint_ == Paint::Random) {
        hint("DRAG", "a random slice on every step");
    } else if (slicing_ && dicing_ && onName) {
        hint("UP / DOWN", "more / fewer hits");
        hint("RIGHT", "roll the slices");
        hint("LEFT", "back in order");
        hint("TAP Y", "reshuffle");
    } else if (slicing_ && !held && onName) {
        hint("UP / DOWN", "one slice / runs on");
        hint("HOLD A", "volume");
        hint("HOLD Y", "dice");
        hint("L1", "grid");
    } else if (slicing_ && !held) {
        if (hitHere()) hint("UP / DOWN", "slice");
        hint("A", hitHere() ? "level / nudge" : "place");
        if (hitHere()) hint("Y", "chance");
        if (hitHere()) hint("HOLD X", "effects");
        hint("HOLD R1", "in order");
        hint("L1", "grid");
        if (!hitHere()) hint("R1 + R2", "shuffle");
    } else if (paint_ == Paint::OneLevel) {
        std::snprintf(brushText, sizeof brushText, "paint hits at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG", brushText);
        hint("+ R2", "random levels");
    } else if (paint_ == Paint::Random) {
        hint("DRAG", "paint hits at random levels");
    } else if (shaping_ && onStep()) {
        hint("UP / DOWN", "level");
        hint("LEFT / RIGHT", "nudge");
        hint("L2 / R2", "nudge early / late");
    } else if (dicing_ && onStep()) {
        hint("UP / DOWN", "chance");
        hint("LEFT / RIGHT", "where the sound starts");
        hint("TAP Y", "100 > 75 > 50 > 25");
    } else if (dicing_ && onName) {
        hint("UP / DOWN", "more / fewer hits");
        hint("RIGHT", "roll the starts");
        hint("LEFT", "starts to the top");
        hint("TAP Y", "reshuffle");
    } else if (shaping_) {
        hint("UP / DOWN", onTempo_ ? "tempo" : "drum volume");
    } else if (onTempo_) {
        hint("HOLD A", "tempo");
        hint("START", project_.playing ? "stop" : "play");
    } else if (onName) {
        hint("HOLD A", "volume");
        hint("HOLD Y", "dice");
        hint("TAP Y", "roll");
        if (canSlice(lane_)) hint("L1", "slices");
    } else {
        hint("A", hitHere() ? "level / nudge" : "place");
        hint("Y", hitHere() ? "chance / start" : "maybe");
        if (hitHere()) hint("HOLD X", "effects");
        hint("HOLD B", "delete");
        hint("HOLD R1", "paint");
        if (canSlice(lane_)) hint("L1", "slices"); // on a recording, this is worth more room than the random brush
        else hint("R1 + R2", "paint random");
    }
    ui::hints(r, fonts, hints, count);
}

} // namespace apotheca
