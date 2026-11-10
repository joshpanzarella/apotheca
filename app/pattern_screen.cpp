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

constexpr float kNewHitLevel = 0.8f;
constexpr float kMinHitLevel = 0.05f; // B deletes; the stick cannot

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

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

} // namespace

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
}

void PatternScreen::setHitLevel(float level) {
    if (pattern_[lane_][step_] <= 0.f) return; // no hit here to set
    level = level < kMinHitLevel ? kMinHitLevel : (level > 1.f ? 1.f : level);
    if (level != pattern_[lane_][step_]) setStep(lane_, step_, level);
}

void PatternScreen::setLaneLevel(float level) {
    level = clamp01(level);
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
    tempoExact_ = bpm < kMinTempo ? kMinTempo : (bpm > kMaxTempo ? kMaxTempo : bpm);
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

// ---- turning the value under the cursor --------------------------------------

void PatternScreen::turn(float amount) {
    if (amount == 0.f) return;
    if (onTempo_) setTempo(tempoExact_ + amount * (feel.tempoPerSecond / feel.fillPerSecond));
    else if (step_ == kLaneName) setLaneLevel(laneLevel_[lane_] + amount);
    else setHitLevel(pattern_[lane_][step_] + amount);
}

void PatternScreen::nudge(Button direction) {
    const float sign = direction == Button::Up || direction == Button::Right ? 1.f : -1.f;
    const bool vertical = direction == Button::Up || direction == Button::Down;
    if (onTempo_) {
        setTempo(tempo_ + sign * (vertical ? 1.f : 10.f));
    } else if (vertical) {
        // To the next tenth in that direction: 0.05 goes up to 0.1, 0.5 to 0.6.
        const float now = step_ == kLaneName ? laneLevel_[lane_] : pattern_[lane_][step_];
        const float tenths = now * 10.f;
        const float next = (sign > 0.f ? std::floor(tenths + 1.001f) : std::ceil(tenths - 1.001f)) / 10.f;
        if (step_ == kLaneName) setLaneLevel(next);
        else setHitLevel(next);
    }
    // Left and right do nothing on a hit or a lane, so the cursor cannot slide
    // off the thing being set.
}

// ---- buttons -------------------------------------------------------------------

void PatternScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (b == Button::A) {
        if (event.down && !onTempo_) {
            if (step_ == kLaneName) {
                audition(lane_, kNewHitLevel);
            } else if (pattern_[lane_][step_] <= 0.f) {
                setStep(lane_, step_, kNewHitLevel);
                if (!playing_) audition(lane_, kNewHitLevel);
            }
        }
        adjusting_ = event.down;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (input.held(Button::A)) nudge(b);
        else moveCursor(b);
        repeating_ = b;
        repeatAt_ = 0; // update() starts the wait, on its own clock
        return;
    }

    if (b == Button::B && !input.held(Button::A)) {
        if (!onTempo_ && step_ != kLaneName && pattern_[lane_][step_] > 0.f) setStep(lane_, step_, 0.f);
    } else if (b == Button::X && !onTempo_) {
        const float here = step_ != kLaneName ? pattern_[lane_][step_] : 0.f;
        audition(lane_, here > 0.f ? here : kNewHitLevel);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        playing_ = !playing_;
        Command c;
        c.type = playing_ ? Command::Play : Command::Stop;
        engine_.send(c);
    }
}

void PatternScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f; // a stalled frame must not fling the cursor
    lastMs_ = nowMs;

    if (adjusting_) {
        // The right stick turns the value. Squared, so a small push is a fine
        // adjustment and a full push is fast.
        const float push = -input.rightY();
        turn(push * std::fabs(push) * feel.fillPerSecond * dt);
    } else {
        // The left stick runs the cursor: one move at once, then at a speed
        // set by how far it is pushed.
        const float x = input.leftX(), y = input.leftY();
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
        if (input.held(Button::A)) nudge(repeating_);
        else moveCursor(repeating_);
    }
}

// ---- drawing ---------------------------------------------------------------------

void PatternScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64];
    const bool onName = !onTempo_ && step_ == kLaneName;
    const bool onStep = !onTempo_ && step_ != kLaneName;
    const SDL_Color cursorColor = adjusting_ ? kBrass : kTeal;

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

    // ---- lanes and steps -------------------------------------------------------
    const int playStep = engine_.playStep();
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kLaneH;
        const bool nameHere = onName && lane == lane_;

        // The lane's name is also its volume: the box is filled as far as the
        // drum is turned up.
        box(r, kNameX, y, kNameW, kCellH, kBg, kLine);
        const int filled = static_cast<int>(std::round((kNameW - 2) * laneLevel_[lane]));
        if (nameHere && adjusting_) {
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
            const int cx = kGridLeft + s * kStepW;
            const bool cursor = onStep && lane == lane_ && s == step_;
            box(r, cx, y, kCellW, kCellH, s % 4 == 0 ? kPanel2 : kPanel, kLine);

            // The hit is a block that stands as tall as it is loud.
            const float level = pattern_[lane][s];
            if (level > 0.f) {
                const int full = kCellH - 8;
                const int h = static_cast<int>(std::round(level * full));
                const bool live = cursor && adjusting_;
                if (live) outline(r, cx + 4, y + 4, kCellW - 8, full, kDim); // the vessel being filled
                fill(r, cx + 4, y + 4 + full - h, kCellW - 8, h < 1 ? 1 : h, live ? kTeal : kText);
            }
            if (cursor) outline(r, cx - 1, y - 1, kCellW + 2, kCellH + 2, cursorColor, 2);
        }
    }
    if (playStep >= 0) {
        SDL_Color glow = kPurple;
        glow.a = 76;
        fill(r, kGridLeft + playStep * kStepW, kGridTop, kCellW, kDrumLanes * kLaneH - 3, glow);
    }

    // ---- what is under the cursor, spelled out -----------------------------------
    const int stripY = kGridTop + kDrumLanes * kLaneH + 6;
    const char* valueName = "LEVEL";
    float value = -1.f; // 0..1, or -1 for "nothing here"
    if (onTempo_) {
        fonts.small.draw(r, 8.f, stripY, "SONG", kDim);
        fonts.large.draw(r, 8.f, stripY + 16.f, "TEMPO", kText);
        valueName = "BPM";
        value = (tempoExact_ - kMinTempo) / (kMaxTempo - kMinTempo);
        std::snprintf(text, sizeof text, "%d", static_cast<int>(tempo_));
    } else {
        fonts.small.draw(r, 8.f, stripY, "LANE", kDim);
        fonts.large.draw(r, 8.f, stripY + 16.f, DrumKit::laneName(lane_), kText);
        if (onName) {
            valueName = "DRUM VOLUME";
            value = laneLevel_[lane_];
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(value * 100.f)));
        } else {
            fonts.small.draw(r, 134.f, stripY, "STEP", kDim);
            char number[16];
            std::snprintf(number, sizeof number, "%d", step_ + 1);
            fonts.large.draw(r, 134.f, stripY + 16.f, number, kText);
            if (pattern_[lane_][step_] > 0.f) {
                value = pattern_[lane_][step_];
                std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(value * 100.f)));
            } else {
                std::snprintf(text, sizeof text, "-");
            }
        }
    }
    const float valueX = onStep ? 260.f : 134.f;
    fonts.small.draw(r, valueX, stripY, valueName, kDim);
    fonts.large.draw(r, valueX, stripY + 16.f, text, value < 0.f ? kDim : cursorColor);
    // The same value as a bar, so the fill reads at a glance.
    const int barX = static_cast<int>(valueX) + 110;
    fill(r, barX, stripY + 20, 180, 6, kLine);
    if (value > 0.f) fill(r, barX, stripY + 20, static_cast<int>(std::round(180 * value)), 6, cursorColor);

    // ---- hints -------------------------------------------------------------------
    const int hintsY = kScreenH - kHintsH;
    fill(r, 0, hintsY, kScreenW, kHintsH, kPanel);
    fill(r, 0, hintsY, kScreenW, 1, kLine);
    const char* act = onTempo_ ? nullptr : onName ? "play drum" : "place";
    const char* turned = onTempo_ ? "tempo" : onName ? "volume" : "level";
    const char* hints[6][2] = {{"D-PAD / L-STICK", "move"}, {"A", act}, {"HOLD A + R-STICK", turned},
                               {"B", onStep ? "delete" : nullptr}, {"X", onTempo_ ? nullptr : "hear"},
                               {"START", playing_ ? "stop" : "play"}};
    const float hintTextY = hintsY + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    x = 8.f;
    for (const auto& hint : hints) {
        if (!hint[1]) continue; // this button does nothing here
        const float keyW = fonts.small.width(hint[0]) + 8.f;
        fill(r, static_cast<int>(x), hintsY + 5, static_cast<int>(keyW), kHintsH - 9, kText);
        fonts.small.draw(r, x + 4.f, hintTextY, hint[0], kBg);
        x += keyW + 5.f;
        fonts.small.draw(r, x, hintTextY, hint[1], kDim);
        x += fonts.small.width(hint[1]) + 11.f;
    }
}

} // namespace apotheca
