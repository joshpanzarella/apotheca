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

constexpr float kNewHitLevel = 0.8f;
constexpr float kMinLevel = 0.05f; // B deletes; the stick cannot
constexpr Uint32 kRepeatDelayMs = 300, kRepeatEveryMs = 70;

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

} // namespace

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

void PatternScreen::moveCursorTo(int lane, int step) {
    lane_ = (lane % kDrumLanes + kDrumLanes) % kDrumLanes;
    step_ = (step % kSteps + kSteps) % kSteps;
}

void PatternScreen::audition(int lane, float level) {
    Command c;
    c.type = Command::Trigger;
    c.track = 0;
    c.a = lane;
    c.value = level;
    engine_.send(c);
}

void PatternScreen::moveCursor(Button direction) {
    if (direction == Button::Up) moveCursorTo(lane_ - 1, step_);
    if (direction == Button::Down) moveCursorTo(lane_ + 1, step_);
    if (direction == Button::Left) moveCursorTo(lane_, step_ - 1);
    if (direction == Button::Right) moveCursorTo(lane_, step_ + 1);
}

void PatternScreen::changeLevel(float delta) {
    const float now = pattern_[lane_][step_];
    if (now <= 0.f || delta == 0.f) return;
    float next = now + delta;
    next = next < kMinLevel ? kMinLevel : (next > 1.f ? 1.f : next);
    if (next != now) setStep(lane_, step_, next);
}

void PatternScreen::changeTempo(float delta) {
    float next = std::round(tempo_ + delta);
    next = next < kMinTempo ? kMinTempo : (next > kMaxTempo ? kMaxTempo : next);
    if (next == tempo_) return;
    tempo_ = next;
    Command c;
    c.type = Command::SetTempo;
    c.value = tempo_;
    engine_.send(c);
}

void PatternScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (b == Button::A) {
        if (event.down && pattern_[lane_][step_] <= 0.f) {
            setStep(lane_, step_, kNewHitLevel);
            if (!playing_) audition(lane_, kNewHitLevel);
        }
        adjusting_ = event.down;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (input.held(Button::A)) {
            // Holding A: up and down set the level; left and right do nothing,
            // so the cursor cannot slide off the hit being adjusted.
            if (b == Button::Up) changeLevel(0.1f);
            if (b == Button::Down) changeLevel(-0.1f);
        } else if (input.held(Button::Select)) {
            changeTempo(b == Button::Up ? 1.f : b == Button::Down ? -1.f : b == Button::Right ? 10.f : -10.f);
        } else {
            moveCursor(b);
        }
        repeating_ = b;
        repeatAt_ = 0; // update() starts the wait, on its own clock
        return;
    }

    if (b == Button::B && !input.held(Button::A)) {
        if (pattern_[lane_][step_] > 0.f) setStep(lane_, step_, 0.f);
    } else if (b == Button::X) {
        audition(lane_, pattern_[lane_][step_] > 0.f ? pattern_[lane_][step_] : kNewHitLevel);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        playing_ = !playing_;
        Command c;
        c.type = playing_ ? Command::Play : Command::Stop;
        engine_.send(c);
    }
}

void PatternScreen::update(const Input& input, Uint32 nowMs) {
    // The right stick fills or drains the hit while A is held.
    if (adjusting_) changeLevel(-input.rightY() * 0.025f);

    // A held D-pad direction repeats.
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (input.held(Button::A)) {
            if (repeating_ == Button::Up) changeLevel(0.1f);
            if (repeating_ == Button::Down) changeLevel(-0.1f);
        } else if (input.held(Button::Select)) {
            changeTempo(repeating_ == Button::Up ? 1.f : repeating_ == Button::Down ? -1.f
                        : repeating_ == Button::Right ? 10.f : -10.f);
        } else {
            moveCursor(repeating_);
        }
    }
}

void PatternScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64];
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
    const float bpmRight = markX - 12.f;
    fonts.small.drawRight(r, bpmRight, textY, "BPM", kDim);
    fonts.small.drawRight(r, bpmRight - fonts.small.width("BPM") - 5.f, textY, text, kText);

    // ---- step numbers --------------------------------------------------------
    for (int s = 0; s < kSteps; ++s) {
        std::snprintf(text, sizeof text, "%d", s + 1);
        fonts.small.drawCentered(r, kGridLeft + s * kStepW + kCellW / 2.f, 36.f, text, s % 4 == 0 ? kText : kDim);
    }

    // ---- lanes and steps -------------------------------------------------------
    const int playStep = engine_.playStep();
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = kGridTop + lane * kLaneH;
        box(r, 8, y, 104, kCellH, kPanel, kLine);
        fonts.medium.draw(r, 15.f, y + (kCellH - fonts.medium.capHeight()) / 2.f, DrumKit::laneName(lane),
                          lane == lane_ ? kTeal : kText);

        for (int s = 0; s < kSteps; ++s) {
            const int cx = kGridLeft + s * kStepW;
            const bool cursor = lane == lane_ && s == step_;
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
            if (cursor) outline(r, cx - 1, y - 1, kCellW + 2, kCellH + 2, adjusting_ ? kBrass : kTeal, 2);
        }
    }
    if (playStep >= 0) {
        SDL_Color glow = kPurple;
        glow.a = 76;
        fill(r, kGridLeft + playStep * kStepW, kGridTop, kCellW, kDrumLanes * kLaneH - 3, glow);
    }

    // ---- the step under the cursor, spelled out ---------------------------------
    const int stripY = kGridTop + kDrumLanes * kLaneH + 6;
    const float here = pattern_[lane_][step_];
    fonts.small.draw(r, 8.f, stripY, "LANE", kDim);
    fonts.large.draw(r, 8.f, stripY + 16.f, DrumKit::laneName(lane_), kText);
    fonts.small.draw(r, 134.f, stripY, "STEP", kDim);
    std::snprintf(text, sizeof text, "%d", step_ + 1);
    fonts.large.draw(r, 134.f, stripY + 16.f, text, kText);
    fonts.small.draw(r, 260.f, stripY, "LEVEL", kDim);
    if (here > 0.f) std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(here * 100.f)));
    else std::snprintf(text, sizeof text, "-");
    fonts.large.draw(r, 260.f, stripY + 16.f, text, adjusting_ ? kBrass : (here > 0.f ? kTeal : kDim));
    // The same level as a bar, so the fill reads at a glance.
    fill(r, 330, stripY + 20, 180, 6, kLine);
    if (here > 0.f) fill(r, 330, stripY + 20, static_cast<int>(std::round(180 * here)), 6, adjusting_ ? kBrass : kTeal);

    // ---- hints -------------------------------------------------------------------
    const int hintsY = kScreenH - kHintsH;
    fill(r, 0, hintsY, kScreenW, kHintsH, kPanel);
    fill(r, 0, hintsY, kScreenW, 1, kLine);
    const char* hints[][2] = {{"D-PAD", "move"}, {"A", "place"}, {"HOLD A + R-STICK", "level"},
                              {"B", "delete"}, {"X", "hear"}, {"START", playing_ ? "stop" : "play"}};
    const float hintTextY = hintsY + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    x = 8.f;
    for (const auto& hint : hints) {
        const float keyW = fonts.small.width(hint[0]) + 8.f;
        fill(r, static_cast<int>(x), hintsY + 5, static_cast<int>(keyW), kHintsH - 9, kText);
        fonts.small.draw(r, x + 4.f, hintTextY, hint[0], kBg);
        x += keyW + 5.f;
        fonts.small.draw(r, x, hintTextY, hint[1], kDim);
        x += fonts.small.width(hint[1]) + 13.f;
    }
}

} // namespace apotheca
