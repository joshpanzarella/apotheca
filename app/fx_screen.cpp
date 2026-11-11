#include "fx_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kMovesPerSecond = 14.f, kKindsPerSecond = 5.f, kTurnPerSecond = 0.9f, kNotch = 0.05f;
}

// Nothing here spells an effect out: its name, its settings, which of them are
// choices and how each reads all come from the one table (apotheca/effects.hpp).

void FxScreen::goTo(int row, int column) {
    row_ = ((row % kRows) + kRows) % kRows;
    const int last = effectInfo(static_cast<EffectKind>(data(row_).kind)).params;
    column_ = column < 0 ? 0 : (column > last ? last : column);
}

void FxScreen::hear() {
    const TrackKind kind = project_.current().kind;
    if (kind == TrackKind::Drums) {
        Command c;
        c.type = Command::Trigger;
        c.track = project_.track;
        c.value = 0.8f;
        session_.engine.send(c);
    } else if (isSynth(kind)) {
        session_.audition(project_.track, 60, 0.8f);
    }
}

// With A held: change the thing under the cursor by one notch.
void FxScreen::press(Button b) {
    if (!holding_) {
        if (b == Button::Up) goTo(row_ - 1, column_);
        if (b == Button::Down) goTo(row_ + 1, column_);
        if (b == Button::Left) goTo(row_, column_ - 1);
        if (b == Button::Right) goTo(row_, column_ + 1);
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    const int by = b == Button::Up ? 1 : -1;
    const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
    if (column_ == 0) {
        // The next one in the list, which has its own order: an effect's number is not its place.
        const int to = effectPlace(kind) + by;
        if (to < 0 || to >= effectListSize()) return; // the ends stop
        session_.setEffect(trackOf(row_), slotOf(row_), effectAt(to));
        if (!project_.playing) hear();
        return;
    }
    const int index = column_ - 1;
    const float now = data(row_).param[index];
    if (const int count = effectChoices(kind, index)) {
        // The next choice: the middle of its part of the range.
        int which = static_cast<int>(now * static_cast<float>(count));
        which = (which >= count ? count - 1 : which) + by;
        if (which < 0 || which >= count) return;
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, (static_cast<float>(which) + 0.5f) / static_cast<float>(count));
    } else {
        const float notches = now / kNotch;
        const float to = (by > 0 ? std::floor(notches + 1.001f) : std::ceil(notches - 1.001f)) * kNotch;
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, to);
    }
}

// The stick with A held on a setting: turn it smoothly.
void FxScreen::turn(float amount) {
    if (column_ == 0 || amount == 0.f) return;
    const int index = column_ - 1;
    session_.setEffectParam(trackOf(row_), slotOf(row_), index, data(row_).param[index] + amount);
}

void FxScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (b == Button::A) {
        holding_ = event.down;
        saved_ = 0.f;
        stickWas_ = false;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::B && !holding_) {
        if (column_ == 0) {
            session_.setEffect(trackOf(row_), slotOf(row_), EffectKind::None);
        } else {
            const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
            // Its default value, and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false);
            if (trackOf(row_) != kMasterTrack) session_.clearAutomation(trackOf(row_), project_.scene, autoLaneOfEffect(slotOf(row_), column_ - 1));
            session_.setEffectParam(trackOf(row_), slotOf(row_), column_ - 1, effectInfo(kind).defaults[column_ - 1]);
            session_.setWriting(writing);
        }
    } else if (b == Button::X) {
        hear();
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void FxScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    goTo(row_, column_); // the track may have changed under the cursor
    const float x = input.stickX(), y = input.stickY();

    // A setting that is a choice (the filter's TYPE, the delay's TIME) is not turned: it clicks
    // from one choice to the next, a push at a time, with the stick as with the D-pad.
    const bool choice = column_ > 0 && effectChoices(static_cast<EffectKind>(data(row_).kind), column_ - 1) > 0;
    if (holding_ && column_ > 0 && !choice) {
        // A setting: the stick turns it. Squared, so a small push is fine.
        turn(-y * std::fabs(y) * kTurnPerSecond * dt);
    } else {
        // Moving, or stepping through the effects: one at once, then steadily.
        const bool sideways = !holding_ && std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * (holding_ ? kKindsPerSecond : kMovesPerSecond) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + (holding_ && column_ == 0 ? 200u : kRepeatEveryMs);
        press(repeating_);
    }
}

void FxScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / EFFECTS", name);
    topBar(r, fonts, View::Effects, text, project_);

    const int colW = 122, colGap = 4, boxH = 62, rowH = 100, top = 34;
    for (int row = 0; row < kRows; ++row) {
        const int y = top + row * rowH + (row >= kMasterSlots ? 6 : 0);
        const EffectData& d = data(row);
        const EffectKind kind = static_cast<EffectKind>(d.kind);
        const EffectInfo& info = effectInfo(kind);
        if (row < kMasterSlots) std::snprintf(text, sizeof text, "%s, EFFECT %d", name, slotOf(row) + 1);
        else std::snprintf(text, sizeof text, "MASTER (EVERYTHING), EFFECT %d", slotOf(row) + 1);
        fonts.small.draw(r, 8.f, y + 2.f, text, row == row_ ? kText : kDim);
        if (row == kMasterSlots) fill(r, 8, y - 6, kScreenW - 16, 1, kLine);

        const bool here = row == row_;
        valueBox(r, fonts, 8, y + 18, colW, boxH, "EFFECT", info.name, -1.f, here && column_ == 0, holding_);
        for (int p = 0; p < info.params; ++p) {
            const bool onTrack = trackOf(row) != kMasterTrack;
            const int lane = autoLaneOfEffect(slotOf(row), p);
            // While the song plays an automated setting shows where the pattern has it.
            float shown[kEffectParams];
            for (int i = 0; i < kEffectParams; ++i) shown[i] = onTrack ? session_.automated(trackOf(row), autoLaneOfEffect(slotOf(row), i), d.param[i]) : d.param[i];
            effectParamText(kind, p, shown[p], text, sizeof text, shown);
            // A choice has no bar: it is not somewhere along a range.
            valueBox(r, fonts, 8 + (p + 1) * (colW + colGap), y + 18, colW, boxH, info.paramNames[p], text, effectChoices(kind, p) > 0 ? -1.f : shown[p],
                     here && column_ == p + 1, holding_);
            if (onTrack && session_.hasAutomation(trackOf(row), project_.scene, lane)) fill(r, 8 + (p + 1) * (colW + colGap) + 1, y + 18 + boxH - 3, colW - 2, 2, kPurple);
        }
        if (kind == EffectKind::None) fonts.small.draw(r, 8.f + colW + 14.f, y + 18.f + 24.f, "Nothing here. Hold A and push up to put an effect in.", kDim);
    }

    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (holding_) {
        hint("UP / DOWN", column_ == 0 ? "next / previous effect" : "turn it");
    } else {
        hint("HOLD A", column_ == 0 ? "pick the effect" : "turn");
        hint("B", column_ == 0 ? "none" : "default value");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
