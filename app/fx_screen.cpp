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
    const int last = columns(row_) - 1;
    column_ = column < 0 ? 0 : (column > last ? last : column);
}

float FxScreen::meterOf(int row) const {
    if (row < 0 || row >= kRows || sends(row)) return 0.f;
    return session_.engine.effectMeter(trackOf(row), slotOf(row));
}

int FxScreen::meterPixels(float dB, int width) {
    if (!(dB > 0.f) || width <= 0) return 0; // (and "not a number" draws nothing)
    if (dB >= kMeterFullDb) return width;
    const int px = static_cast<int>(std::round(static_cast<float>(width) * dB / kMeterFullDb));
    return px < 1 && dB >= 0.1f ? 1 : px; // a tenth of a dB and up is seen
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
    const bool upDown = b == Button::Up || b == Button::Down;
    const int by = b == Button::Up || b == Button::Right ? 1 : -1;
    if (sends(row_)) {
        // A send: a twentieth at a push.
        if (!upDown) return;
        const float notches = send(column_) / kNotch;
        session_.setSend(project_.track, column_, (by > 0 ? std::floor(notches + 1.001f) : std::ceil(notches - 1.001f)) * kNotch);
        return;
    }
    const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
    if (column_ == 0) {
        // The next one in the list, which has its own order: an effect's number is not its place.
        // Left and right go a whole group at once. A bus has its own short list, all of one group.
        const int bus = busOf(row_);
        EffectKind next = kind;
        if (bus >= 0) {
            const int to = busEffectPlace(bus, kind) + by;
            if (!upDown || to < 0 || to >= busListSize(bus)) return; // the ends stop
            next = busEffectAt(bus, to);
        } else {
            const int from = effectPlace(kind), to = upDown ? from + by : effectGroupJump(from, by);
            if (to == from || to < 0 || to >= effectListSize()) return;
            next = effectAt(to);
        }
        session_.setEffect(trackOf(row_), slotOf(row_), next);
        if (!project_.playing) hear();
        return;
    }
    if (!upDown) return;
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
    if (amount == 0.f) return;
    if (sends(row_)) {
        session_.setSend(project_.track, column_, send(column_) + amount);
        return;
    }
    if (column_ == 0) return;
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
        if (sends(row_)) {
            // Nothing sent, and whatever automation the send had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false);
            session_.clearAutomation(project_.track, project_.scene, autoLaneOfSend(column_));
            session_.setSend(project_.track, column_, 0.f);
            session_.setWriting(writing);
        } else if (column_ == 0) {
            session_.setEffect(trackOf(row_), slotOf(row_), EffectKind::None);
        } else {
            const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
            // Its default value (on a bus, LEVEL's is full), and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false);
            if (trackOf(row_) >= 0) session_.clearAutomation(trackOf(row_), project_.scene, autoLaneOfEffect(slotOf(row_), column_ - 1));
            session_.setEffectParam(trackOf(row_), slotOf(row_), column_ - 1, effectDefault(kind, column_ - 1, busOf(row_) >= 0));
            session_.setWriting(writing);
        }
    } else if (b == Button::Y && !holding_) {
        goTo(row_ + kPageRows, column_); // the other page, on the same row of it
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
    const bool onSend = sends(row_);
    const bool choice = !onSend && column_ > 0 && effectChoices(static_cast<EffectKind>(data(row_).kind), column_ - 1) > 0;
    const bool picking = holding_ && !onSend && column_ == 0; // an effect: up / down steps, left / right jumps a group
    if (holding_ && (onSend || (column_ > 0 && !choice))) {
        // A setting or a send: the stick turns it. Squared, so a small push is fine.
        turn(-y * std::fabs(y) * kTurnPerSecond * dt);
    } else {
        // Moving, or stepping through the effects: one at once, then steadily.
        const bool sideways = (!holding_ || picking) && std::fabs(x) > std::fabs(y);
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
        repeatAt_ = nowMs + (holding_ && column_ == 0 && !sends(row_) ? 200u : kRepeatEveryMs);
        press(repeating_);
    }
}

void FxScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    const bool all = page() == 1;
    if (all) std::snprintf(text, sizeof text, "EVERYTHING / EFFECTS");
    else std::snprintf(text, sizeof text, "%s / EFFECTS", name);
    topBar(r, fonts, View::Effects, text, project_);

    const int colW = 122, colGap = 4, boxH = 62, rowH = 100, top = 34;
    // A line and a little room before the rows that are another kind of thing: the sends, the master.
    const int apart = all ? kMasterRow : kSendsRow;
    for (int row = page() * kPageRows; row < (page() + 1) * kPageRows; ++row) {
        const int y = top + (row % kPageRows) * rowH + (row >= apart ? 6 : 0);
        const bool here = row == row_;
        const int bus = busOf(row);
        if (row == apart) fill(r, 8, y - 6, kScreenW - 16, 1, kLine);

        if (sends(row)) {
            std::snprintf(text, sizeof text, "%s, SENDS", name);
            fonts.small.draw(r, 8.f, y + 2.f, text, here ? kText : kDim);
            for (int b = 0; b < kBuses; ++b) {
                const int lane = autoLaneOfSend(b), x = 8 + b * (colW + colGap);
                // While the song plays an automated send shows where the pattern has it.
                const float shown = session_.automated(project_.track, lane, send(b));
                std::snprintf(text, sizeof text, "%d%%", static_cast<int>(shown * 100.f + 0.5f));
                valueBox(r, fonts, x, y + 18, colW, boxH, b == 0 ? "ECHO" : "SPACE", text, shown, here && column_ == b, holding_);
                if (session_.hasAutomation(project_.track, project_.scene, lane)) fill(r, x + 1, y + 18 + boxH - 3, colW - 2, 2, kPurple);
            }
            continue;
        }

        const EffectData& d = data(row);
        const EffectKind kind = static_cast<EffectKind>(d.kind);
        const EffectInfo& info = effectInfo(kind);
        if (bus >= 0) std::snprintf(text, sizeof text, "%s (WHAT THE TRACKS SEND)", bus == 0 ? "ECHO" : "SPACE");
        else if (all) std::snprintf(text, sizeof text, "MASTER (EVERYTHING), EFFECT %d", slotOf(row) + 1);
        else std::snprintf(text, sizeof text, "%s, EFFECT %d", name, slotOf(row) + 1);
        fonts.small.draw(r, 8.f, y + 2.f, text, here ? kText : kDim);

        valueBox(r, fonts, 8, y + 18, colW, boxH, "EFFECT", info.name, -1.f, here && column_ == 0, holding_);
        const bool onTrack = trackOf(row) >= 0;
        for (int p = 0; p < info.params; ++p) {
            const int lane = autoLaneOfEffect(slotOf(row), p);
            // While the song plays an automated setting shows where the pattern has it.
            float shown[kEffectParams];
            for (int i = 0; i < kEffectParams; ++i) shown[i] = onTrack ? session_.automated(trackOf(row), autoLaneOfEffect(slotOf(row), i), d.param[i]) : d.param[i];
            effectParamText(kind, p, shown[p], text, sizeof text, shown);
            // On a bus the MIX is how loud the bus comes back: it reads LEVEL.
            const char* label = bus >= 0 && p == effectMixParam(kind) ? "LEVEL" : info.paramNames[p];
            // A choice has no bar: it is not somewhere along a range.
            valueBox(r, fonts, 8 + (p + 1) * (colW + colGap), y + 18, colW, boxH, label, text, effectChoices(kind, p) > 0 ? -1.f : shown[p],
                     here && column_ == p + 1, holding_);
            if (onTrack && session_.hasAutomation(trackOf(row), project_.scene, lane)) fill(r, 8 + (p + 1) * (colW + colGap) + 1, y + 18 + boxH - 3, colW - 2, 2, kPurple);
            if (p == effectMeterParam(kind)) {
                // What the effect is doing to the sound just now (COMP: how far it is turning it
                // down): a strip over the box's own bar, growing from the right as a meter of
                // this kind does. The bar's track is 7 in from each side of the box (valueBox).
                const int bw = colW - 14, px = meterPixels(meterOf(row), bw);
                if (px > 0) fill(r, 8 + (p + 1) * (colW + colGap) + 7 + bw - px, y + 18 + boxH - 14, px, 2, kBrass);
            }
        }
        if (kind == EffectKind::None) fonts.small.draw(r, 8.f + colW + 14.f, y + 18.f + 24.f, "Nothing here. Hold A and push up to put an effect in.", kDim);
    }

    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    const bool effect = !sends(row_) && column_ == 0;
    if (holding_) {
        hint("UP / DOWN", effect ? "next / previous effect" : "turn it");
        if (effect && busOf(row_) < 0) hint("LEFT / RIGHT", "next / previous group");
    } else {
        hint("HOLD A", effect ? "pick the effect" : "turn");
        hint("B", effect || sends(row_) ? "none" : "default value");
        hint("Y", all ? "track" : "buses");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
