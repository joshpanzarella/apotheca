#include "settings_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRelearnHoldMs = 1000; // A held this long on CONTROLS
constexpr Uint32 kTestQuietMs = 4000;   // the button test closes this long after the last press
constexpr Uint32 kRepeatDelayMs = 350, kRepeatEveryMs = 90;
constexpr float kMovesPerSecond = 7.f, kChangesPerSecond = 9.f;
bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }
}

void SettingsScreen::change(int by) {
    switch (row_) {
    case kTempo: session_.setTempo(project_.tempo + static_cast<float>(by)); break;
    case kPack: session_.choosePack(session_.packIndex() + by); break;
    case kVolume: session_.setVolume(session_.volume() + 0.05f * static_cast<float>(by)); break;
    case kStick: session_.setStickSpeed(session_.stickSpeed() + 0.05f * static_cast<float>(by)); break;
    case kOctaves: session_.setOctavesShown(session_.octavesShown() + by); break;
    default: break;
    }
}

void SettingsScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (testing_) {
        // Everything is only shown.
        if (event.down) lastPressed_ = b, testTimed_ = false;
        return;
    }
    if (b == Button::A) {
        holding_ = event.down && row_ == kControls;
        holdTimed_ = false;
        holdPart_ = 0.f;
        if (!event.down) return;
        if (row_ == kWifi) session_.toggleTransfer(lastMs_ * 2654435761u + 977u);
        if (row_ == kTest) testing_ = true, testTimed_ = false, lastPressed_ = Button::None;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        if (b == Button::Up) goTo(row_ - 1);
        if (b == Button::Down) goTo(row_ + 1);
        if (b == Button::Left) change(-1);
        if (b == Button::Right) change(1);
        holding_ = false; // moving off lets go
        holdPart_ = 0.f;
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void SettingsScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed;
    lastMs_ = nowMs;
    stickX_ = input.stickX();
    stickY_ = input.stickY();
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) held_[i] = input.held(static_cast<Button>(i));

    if (testing_) {
        bool any = stickX_ != 0.f || stickY_ != 0.f;
        for (bool h : held_) any = any || h;
        if (!testTimed_ || any) testTimed_ = true, testUntil_ = nowMs + kTestQuietMs;
        else if (nowMs >= testUntil_) testing_ = false;
        saved_ = 0.f;
        stickWas_ = false;
        repeating_ = Button::None;
        return;
    }
    if (holding_) {
        if (!holdTimed_) holdTimed_ = true, holdSince_ = nowMs;
        holdPart_ = static_cast<float>(nowMs - holdSince_) / static_cast<float>(kRelearnHoldMs);
        if (holdPart_ >= 1.f) {
            holding_ = false;
            holdPart_ = 0.f;
            relearn_ = true;
        }
        return;
    }
    // The stick: up and down choose, sideways changes. One at once, then steadily.
    const float x = stickX_, y = stickY_;
    const bool sideways = std::fabs(x) > std::fabs(y);
    const float push = sideways ? x : y;
    if (push == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? std::fabs(push) * (sideways ? kChangesPerSecond : kMovesPerSecond) * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) {
            if (sideways) change(push > 0.f ? 1 : -1);
            else goTo(row_ + (push > 0.f ? 1 : -1));
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (repeating_ == Button::Up) goTo(row_ - 1);
        if (repeating_ == Button::Down) goTo(row_ + 1);
        if (repeating_ == Button::Left) change(-1);
        if (repeating_ == Button::Right) change(1);
    }
}

void SettingsScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    topBar(r, fonts, View::Settings, "", project_);
    char text[96];

    if (testing_) {
        fonts.medium.draw(r, 24.f, 44.f, "BUTTON TEST", kText);
        fonts.small.draw(r, 24.f, 66.f, "Press each button. It should light up here, and only while you hold it.", kDim);
        fonts.small.draw(r, 24.f, 82.f, "One that stays lit, or never lights, was learned wrongly: set the controls up again.", kDim);
        const Button order[15] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::A, Button::B, Button::X, Button::Y,
                                  Button::L1, Button::R1, Button::L2, Button::R2, Button::Start, Button::Select, Button::Menu};
        for (int i = 0; i < 15; ++i) {
            const int x = 24 + (i % 4) * 112, y = 112 + (i / 4) * 58;
            const bool on = held_[static_cast<int>(order[i])];
            box(r, x, y, 104, 48, on ? kTeal : kPanel, on ? kTeal : kLine);
            fonts.medium.drawCentered(r, x + 52.f, y + (48.f - fonts.medium.capHeight()) / 2.f, buttonName(order[i]), on ? kBg : kDim);
        }
        // The stick: a dot in a square.
        const int sx = 500, sy = 140, size = 110;
        box(r, sx, sy, size, size, kPanel, kLine);
        fill(r, sx + size / 2, sy + 1, 1, size - 2, kLine);
        fill(r, sx + 1, sy + size / 2, size - 2, 1, kLine);
        const int dx = sx + size / 2 + static_cast<int>(stickX_ * (size / 2 - 8)), dy = sy + size / 2 + static_cast<int>(stickY_ * (size / 2 - 8));
        fill(r, dx - 5, dy - 5, 10, 10, stickX_ != 0.f || stickY_ != 0.f ? kTeal : kDim);
        fonts.small.drawCentered(r, sx + size / 2.f, sy - 18.f, "THE STICK", kDim);
        std::snprintf(text, sizeof text, "LAST PRESSED: %s", lastPressed_ == Button::None ? "nothing yet" : buttonName(lastPressed_));
        fonts.medium.draw(r, 24.f, 360.f, text, kText);
        fonts.small.draw(r, 24.f, 386.f, "Buttons do nothing else while this is open. It closes four seconds after you stop pressing.", kDim);
        hints(r, fonts, nullptr, 0);
        return;
    }

    const char* names[kRows] = {"TEMPO", "CHORD PACK", "VOLUME", "STICK SPEED", "PIANO ROLL SHOWS", "WIFI PAGE", "BUTTONS", "CONTROLS"};
    const int rowH = 46, top = 52, w = kScreenW - 32;
    fonts.small.draw(r, 16.f, 34.f, "THIS SONG", kDim);
    for (int row = 0; row < kRows; ++row) {
        // The first two belong to the song; the rest to the handheld.
        const int y = top + row * rowH + (row >= kVolume ? 22 : 0);
        if (row == kVolume) fonts.small.draw(r, 16.f, y - 18.f, "THIS HANDHELD", kDim);
        const bool here = row == row_;
        float part = -1.f;
        const char* note = "";
        switch (row) {
        case kTempo: std::snprintf(text, sizeof text, "%d BPM", static_cast<int>(project_.tempo)); break;
        case kPack: std::snprintf(text, sizeof text, "%s", session_.packs[session_.packIndex()].name.c_str()); break;
        case kVolume:
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(session_.volume() * 100.f)));
            part = session_.volume();
            note = "also SELECT + L1 / R1, anywhere";
            break;
        case kStick:
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(session_.stickSpeed() * 100.f)));
            part = (session_.stickSpeed() - Session::kStickSlowest) / (Session::kStickFastest - Session::kStickSlowest);
            note = "how fast the stick moves and turns things";
            break;
        case kOctaves:
            std::snprintf(text, sizeof text, "%d OCTAVE%s", session_.octavesShown(), session_.octavesShown() == 1 ? "" : "S");
            note = "L1 on the piano roll does the same";
            break;
        case kWifi:
            if (session_.transferOn()) std::snprintf(text, sizeof text, "ON   %s", session_.transferAddress().c_str());
            else std::snprintf(text, sizeof text, "OFF");
            note = session_.transferOn() ? "" : "files and updates from a browser";
            break;
        case kTest:
            std::snprintf(text, sizeof text, "TEST");
            note = "see what Apotheca hears from each button";
            break;
        default:
            std::snprintf(text, sizeof text, "SET UP AGAIN");
            note = "asks for every button in turn";
            break;
        }
        box(r, 16, y, w, rowH - 6, kPanel, kLine);
        fonts.small.draw(r, 26.f, y + (rowH - 6 - fonts.small.capHeight()) / 2.f, names[row], kDim);
        fonts.large.draw(r, 170.f, y + (rowH - 6 - fonts.large.capHeight()) / 2.f, text, here ? kTeal : kText);
        if (note[0]) fonts.small.drawRight(r, 16.f + w - 10.f, y + (rowH - 6 - fonts.small.capHeight()) / 2.f, note, kDim);
        if (part >= 0.f) {
            fill(r, 250, y + rowH - 16, 110, 4, kLine);
            fill(r, 250, y + rowH - 16, static_cast<int>(std::round(110.f * (part > 1.f ? 1.f : part))), 4, here ? kTeal : kDim);
        }
        if (here) outline(r, 15, y - 1, w + 2, rowH - 4, holdPart_ > 0.f ? kBrass : kTeal, 2);
        if (here && row == kControls && holdPart_ > 0.f) fill(r, 16, y + rowH - 10, static_cast<int>(w * (holdPart_ > 1.f ? 1.f : holdPart_)), 4, kBrass);
    }

    Hint list[5];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    hint("UP / DOWN", "choose");
    if (row_ <= kOctaves) hint("LEFT / RIGHT", "change");
    if (row_ == kWifi) hint("A", session_.transferOn() ? "turn off" : "turn on");
    if (row_ == kTest) hint("A", "open the test");
    if (row_ == kControls) hint("HOLD A", "set up again");
    hint("SELECT", "to SONG");
    hints(r, fonts, list, count);
}

} // namespace apotheca
