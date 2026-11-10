#include "pattern_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

namespace {

// The palette from docs/screens/screens.html.
const SDL_Color kBg = {20, 17, 14, 255};
const SDL_Color kPanel = {31, 26, 22, 255};
const SDL_Color kPanel2 = {42, 35, 29, 255};
const SDL_Color kLine = {67, 57, 47, 255};
const SDL_Color kText = {224, 213, 191, 255};
const SDL_Color kDim = {143, 131, 112, 255};
const SDL_Color kTeal = {111, 191, 163, 255};
const SDL_Color kPurple = {154, 118, 176, 255};
const SDL_Color kBrass = {201, 161, 90, 255};

// The layout from the same file, in pixels on the 640x480 screen.
constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kTopBarH = 26, kHintsH = 24;
constexpr int kGridLeft = 116, kGridTop = 54, kStepW = 32, kLaneH = 41;
constexpr int kCellW = kStepW - 2, kCellH = kLaneH - 3;
constexpr int kNameX = 8, kNameW = 104;
constexpr int kBlockW = kCellW - 8, kBlockFull = kCellH - 8;

constexpr float kNewHitLevel = 0.8f;
constexpr float kMinHitLevel = 0.05f; // B deletes; the stick cannot
constexpr float kMinChance = 0.05f;   // likewise
constexpr float kMaybe = 0.5f;        // the chance of a hit placed with Y

void fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void outline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, int thickness = 1) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int i = 0; i < thickness; ++i) {
        SDL_Rect rect = {x + i, y + i, w - 2 * i, h - 2 * i};
        SDL_RenderDrawRect(r, &rect);
    }
}

void box(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color inside, SDL_Color edge) {
    fill(r, x, y, w, h, inside);
    outline(r, x, y, w, h, edge);
}

bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }

float clampTo(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// To the next tenth in a direction: 0.05 goes up to 0.1, 0.5 to 0.6.
float nextTenth(float now, float sign) {
    const float tenths = now * 10.f;
    return (sign > 0.f ? std::floor(tenths + 1.001f) : std::ceil(tenths - 1.001f)) / 10.f;
}

} // namespace

PatternScreen::PatternScreen(Engine& engine) : engine_(engine) {
    for (float& level : laneLevel_) level = 1.f;
    for (auto& lane : chance_)
        for (float& c : lane) c = 1.f;
}

// ---- writing values --------------------------------------------------------

void PatternScreen::setStep(int lane, int step, float level) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    pattern_[lane][step] = level;
    Command c;
    c.type = Command::SetStep;
    c.track = 0;
    c.a = lane;
    c.b = step;
    c.value = level;
    engine_.send(c);
    // A removed hit leaves nothing behind for the next one placed there.
    if (level <= 0.f) {
        setNudge(lane, step, 0.f);
        setChance(lane, step, 1.f);
    }
}

void PatternScreen::setNudge(int lane, int step, float nudge) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    nudge = clampTo(nudge, -0.5f, 0.5f);
    if (nudge == nudge_[lane][step]) return;
    nudge_[lane][step] = nudge;
    Command c;
    c.type = Command::SetStepNudge;
    c.track = 0;
    c.a = lane;
    c.b = step;
    c.value = nudge;
    engine_.send(c);
}

void PatternScreen::setChance(int lane, int step, float chance) {
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= kSteps) return;
    chance = clampTo(chance, kMinChance, 1.f);
    if (chance == chance_[lane][step]) return;
    chance_[lane][step] = chance;
    Command c;
    c.type = Command::SetStepChance;
    c.track = 0;
    c.a = lane;
    c.b = step;
    c.value = chance;
    engine_.send(c);
}

void PatternScreen::setLaneLevel(float level) {
    level = clampTo(level, 0.f, 1.f);
    if (level == laneLevel_[lane_]) return;
    laneLevel_[lane_] = level;
    Command c;
    c.type = Command::SetLaneLevel;
    c.track = 0;
    c.a = lane_;
    c.value = level;
    engine_.send(c);
}

void PatternScreen::setTempo(float bpm) {
    tempoExact_ = clampTo(bpm, kMinTempo, kMaxTempo);
    const float whole = std::round(tempoExact_);
    if (whole == tempo_) return;
    tempo_ = whole;
    Command c;
    c.type = Command::SetTempo;
    c.value = tempo_;
    engine_.send(c);
}

void PatternScreen::audition(int lane, float level) {
    Command c;
    c.type = Command::Trigger;
    c.track = 0;
    c.a = lane;
    c.value = level;
    engine_.send(c);
}

void PatternScreen::placeHit(float chance) {
    setStep(lane_, step_, kNewHitLevel);
    setChance(lane_, step_, chance);
    if (!playing_) audition(lane_, kNewHitLevel);
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
    if (direction == Button::Left) moveCursorTo(lane_, step_ - 1);
    if (direction == Button::Right) moveCursorTo(lane_, step_ + 1);
}

// ---- the stick, with a button held ---------------------------------------------

void PatternScreen::turnUpDown(float amount) {
    if (amount == 0.f) return;
    if (onTempo_) {
        setTempo(tempoExact_ + amount * (feel.tempoPerSecond / feel.fillPerSecond));
    } else if (step_ == kLaneName) {
        setLaneLevel(laneLevel_[lane_] + amount);
    } else if (hitHere()) {
        setStep(lane_, step_, clampTo(pattern_[lane_][step_] + amount, kMinHitLevel, 1.f));
        turning_ = Turning::Level;
    }
}

void PatternScreen::turnSideways(float amount) {
    if (amount == 0.f || !hitHere()) return;
    setNudge(lane_, step_, nudge_[lane_][step_] + amount);
    turning_ = Turning::Nudge;
}

void PatternScreen::turnChance(float amount) {
    if (amount == 0.f || !hitHere()) return;
    setChance(lane_, step_, chance_[lane_][step_] + amount);
    turning_ = Turning::Chance;
    yDidSomething_ = true;
}

// The D-pad with A or Y held: the same values, in exact steps.
void PatternScreen::nudgeExact(Button direction, bool withY) {
    const float sign = direction == Button::Up || direction == Button::Right ? 1.f : -1.f;
    const bool vertical = direction == Button::Up || direction == Button::Down;
    if (withY) {
        if (vertical && hitHere()) {
            setChance(lane_, step_, nextTenth(chance_[lane_][step_], sign));
            turning_ = Turning::Chance;
            yDidSomething_ = true;
        }
    } else if (onTempo_) {
        setTempo(tempo_ + sign * (vertical ? 1.f : 10.f));
    } else if (step_ == kLaneName) {
        if (vertical) setLaneLevel(nextTenth(laneLevel_[lane_], sign));
    } else if (hitHere()) {
        if (vertical) {
            setStep(lane_, step_, clampTo(nextTenth(pattern_[lane_][step_], sign), kMinHitLevel, 1.f));
            turning_ = Turning::Level;
        } else {
            // Sideways: a tenth of a step, early or late.
            setNudge(lane_, step_, nextTenth(nudge_[lane_][step_] + 0.5f, sign) - 0.5f);
            turning_ = Turning::Nudge;
        }
    }
}

// ---- buttons -------------------------------------------------------------------

void PatternScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

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
        } else if (!yDidSomething_ && hitHere()) {
            // A plain tap on a hit steps its chance down a quarter, and round again.
            const float now = chance_[lane_][step_];
            setChance(lane_, step_, now > 0.76f ? 0.75f : now > 0.51f ? 0.5f : now > 0.26f ? 0.25f : 1.f);
        }
        dicing_ = event.down;
        axis_ = Axis::None;
        if (!event.down && !shaping_) turning_ = Turning::Nothing;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (input.held(Button::Y)) nudgeExact(b, true);
        else if (input.held(Button::A)) nudgeExact(b, false);
        else moveCursor(b);
        repeating_ = b;
        repeatAt_ = 0; // update() starts the wait, on its own clock
        return;
    }

    const bool busy = input.held(Button::A) || input.held(Button::Y);
    if (b == Button::B && !busy) {
        if (hitHere()) setStep(lane_, step_, 0.f);
    } else if (b == Button::X && !onTempo_) {
        audition(lane_, hitHere() ? pattern_[lane_][step_] : kNewHitLevel);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        playing_ = !playing_;
        Command c;
        c.type = playing_ ? Command::Play : Command::Stop;
        engine_.send(c);
    }
}

void PatternScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f; // a stalled frame must not fling anything
    lastMs_ = nowMs;

    const float x = input.stickX(), y = input.stickY();

    if (shaping_ || dicing_) {
        // The stick does one job per push. Whichever way it leaves the centre
        // is the job until it comes back, so setting a level cannot also
        // slide the hit sideways.
        if (x == 0.f && y == 0.f) axis_ = Axis::None;
        else if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;

        // Squared, so a small push is a fine adjustment and a full push is fast.
        const float up = -y * std::fabs(y) * feel.fillPerSecond * dt;
        const float right = x * std::fabs(x) * feel.fillPerSecond * dt;
        if (dicing_) {
            if (axis_ == Axis::UpDown) turnChance(up);
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
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * feel.stickCellsPerSecond * 0.5f * dt : 1.f; // eight lanes: half speed is plenty
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    // A held D-pad direction repeats.
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + feel.repeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + feel.repeatEveryMs;
        if (input.held(Button::Y)) nudgeExact(repeating_, true);
        else if (input.held(Button::A)) nudgeExact(repeating_, false);
        else moveCursor(repeating_);
    }
}

// ---- drawing ---------------------------------------------------------------------

void PatternScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64];
    const bool onName = !onTempo_ && step_ == kLaneName;
    const bool held = shaping_ || dicing_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);

    // ---- top bar -----------------------------------------------------------
    fill(r, 0, 0, kScreenW, kTopBarH, kPanel);
    fill(r, 0, kTopBarH, kScreenW, 1, kLine);
    const float textY = (kTopBarH - fonts.small.capHeight()) / 2.f;
    float x = 8.f;
    const char* tabs[] = {"SONG", "PATTERN", "DEVICE", "MIX"};
    for (int i = 0; i < 4; ++i) {
        const float w = fonts.small.width(tabs[i]) + 14.f;
        const bool here = i == 1;
        if (here) fill(r, static_cast<int>(x), 2, static_cast<int>(w), kTopBarH - 4, kTeal);
        fonts.small.draw(r, x + 7.f, textY, tabs[i], here ? kBg : kDim);
        x += w + 2.f;
    }
    fonts.small.draw(r, x + 10.f, textY, "1 DRUMS / SCENE 1", kText);

    // Right side: tempo, then a triangle while playing or a square when stopped.
    const int markX = kScreenW - 20;
    SDL_SetRenderDrawColor(r, kPurple.r, kPurple.g, kPurple.b, 255);
    if (playing_) {
        for (int i = 0; i <= 12; ++i) {
            SDL_RenderDrawLine(r, markX, 7 + i, markX + (i <= 6 ? i : 12 - i) * 2, 7 + i);
        }
    } else {
        fill(r, markX, 8, 10, 10, kDim);
    }
    std::snprintf(text, sizeof text, "%d", static_cast<int>(tempo_));
    const float bpmRight = markX - 14.f;
    const float numberRight = bpmRight - fonts.small.width("BPM") - 5.f;
    if (onTempo_) {
        // The tempo fills too: its box is a bar from the slowest to the fastest.
        const int bx = static_cast<int>(numberRight - fonts.small.width("240") - 6.f);
        const int bw = static_cast<int>(bpmRight) - bx + 6;
        const float part = (tempoExact_ - kMinTempo) / (kMaxTempo - kMinTempo);
        fill(r, bx, 3, bw, kTopBarH - 6, kPanel2);
        SDL_Color bar = cursorColor;
        bar.a = 90;
        fill(r, bx, 3, static_cast<int>(std::round(bw * part)), kTopBarH - 6, bar);
        outline(r, bx - 1, 2, bw + 2, kTopBarH - 4, cursorColor, 2);
    }
    fonts.small.drawRight(r, bpmRight, textY, "BPM", onTempo_ ? kText : kDim);
    fonts.small.drawRight(r, numberRight, textY, text, kText);

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
        const int filled = static_cast<int>(std::round((kNameW - 2) * laneLevel_[lane]));
        if (nameHere && shaping_) {
            SDL_Color bar = kBrass;
            bar.a = 110;
            fill(r, kNameX + 1, y + 1, filled, kCellH - 2, bar);
        } else {
            fill(r, kNameX + 1, y + 1, filled, kCellH - 2, kPanel);
        }
        if (nameHere) outline(r, kNameX - 1, y - 1, kNameW + 2, kCellH + 2, cursorColor, 2);
        fonts.medium.draw(r, 15.f, y + (kCellH - fonts.medium.capHeight()) / 2.f, DrumKit::laneName(lane),
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
            const float level = pattern_[lane][s];
            if (level <= 0.f) continue;
            const bool live = held && onStep() && lane == lane_ && s == step_;
            int bx = kGridLeft + s * kStepW + 4 + static_cast<int>(std::round(nudge_[lane][s] * kStepW));
            if (bx < kGridLeft) bx = kGridLeft;
            if (bx + kBlockW > gridRight) bx = gridRight - kBlockW;
            int h = static_cast<int>(std::round(level * kBlockFull));
            if (h < 1) h = 1;

            SDL_Color body = live ? kTeal : kText;
            const float chance = chance_[lane][s];
            body.a = static_cast<Uint8>(std::round(255.f * (0.12f + 0.88f * chance * chance)));
            if (live) outline(r, bx, y + 4, kBlockW, kBlockFull, kDim); // the vessel being filled
            fill(r, bx, y + 4 + kBlockFull - h, kBlockW, h, body);
            // A hit that might not play keeps its edge, so a ghost is still findable.
            if (chance < 1.f) outline(r, bx, y + 4 + kBlockFull - h, kBlockW, h, live ? kTeal : kText);
        }
    }
    if (onStep()) {
        outline(r, kGridLeft + step_ * kStepW - 1, kGridTop + lane_ * kLaneH - 1, kCellW + 2, kCellH + 2, cursorColor, 2);
    }
    const int playStep = engine_.playStep();
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
        std::snprintf(text, sizeof text, "%d", static_cast<int>(tempo_));
        field(134.f, "BPM", text, cursorColor);
        bar(244, (tempoExact_ - kMinTempo) / (kMaxTempo - kMinTempo), cursorColor);
    } else if (onName) {
        field(8.f, "LANE", DrumKit::laneName(lane_), kText);
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(laneLevel_[lane_] * 100.f)));
        field(134.f, "DRUM VOLUME", text, cursorColor);
        bar(244, laneLevel_[lane_], cursorColor);
    } else {
        field(8.f, "LANE", DrumKit::laneName(lane_), kText);
        std::snprintf(text, sizeof text, "%d", step_ + 1);
        field(120.f, "STEP", text, kText);
        if (!hitHere()) {
            field(190.f, "LEVEL", "-", kDim);
            field(330.f, "NUDGE", "-", kDim);
            field(470.f, "CHANCE", "-", kDim);
        } else {
            // The one the stick has hold of is brass; the others are quiet.
            const auto shade = [&](Turning t) { return turning_ == t ? kBrass : (turning_ == Turning::Nothing ? kTeal : kText); };
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(pattern_[lane_][step_] * 100.f)));
            field(190.f, "LEVEL", text, shade(Turning::Level));
            const int n = static_cast<int>(std::round(nudge_[lane_][step_] * 100.f));
            if (n == 0) std::snprintf(text, sizeof text, "ON GRID");
            else std::snprintf(text, sizeof text, "%d%% %s", std::abs(n), n < 0 ? "EARLY" : "LATE");
            field(330.f, "NUDGE", text, shade(Turning::Nudge));
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(chance_[lane_][step_] * 100.f)));
            field(470.f, "CHANCE", text, shade(Turning::Chance));
        }
    }

    // ---- hints -------------------------------------------------------------------
    // Only what does something here, and what the ONE stick does right now.
    const int hintsY = kScreenH - kHintsH;
    fill(r, 0, hintsY, kScreenW, kHintsH, kPanel);
    fill(r, 0, hintsY, kScreenW, 1, kLine);
    const char* hints[6][2] = {};
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { hints[count][0] = key; hints[count][1] = does; ++count; };
    if (shaping_ && onStep()) {
        hint("STICK UP / DOWN", "level");
        hint("STICK LEFT / RIGHT", "nudge early / late");
    } else if (dicing_ && onStep()) {
        hint("STICK UP / DOWN", "chance");
        hint("TAP Y", "100 > 75 > 50 > 25");
    } else if (shaping_) {
        hint("STICK UP / DOWN", onTempo_ ? "tempo" : "drum volume");
    } else if (onTempo_) {
        hint("STICK", "move");
        hint("HOLD A + STICK", "tempo");
        hint("START", playing_ ? "stop" : "play");
    } else if (onName) {
        hint("STICK", "move");
        hint("A", "play drum");
        hint("HOLD A + STICK", "volume");
        hint("START", playing_ ? "stop" : "play");
    } else {
        hint("STICK", "move");
        hint("A", hitHere() ? "hold: level / nudge" : "place");
        hint("Y", hitHere() ? "chance" : "place a maybe");
        if (hitHere()) hint("B", "delete");
        hint("X", "hear");
        hint("START", playing_ ? "stop" : "play");
    }
    const float hintTextY = hintsY + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    x = 8.f;
    for (int i = 0; i < count; ++i) {
        const float keyW = fonts.small.width(hints[i][0]) + 8.f;
        fill(r, static_cast<int>(x), hintsY + 5, static_cast<int>(keyW), kHintsH - 9, kText);
        fonts.small.draw(r, x + 4.f, hintTextY, hints[i][0], kBg);
        x += keyW + 5.f;
        fonts.small.draw(r, x, hintTextY, hints[i][1], kDim);
        x += fonts.small.width(hints[i][1]) + 11.f;
    }
}

} // namespace apotheca
