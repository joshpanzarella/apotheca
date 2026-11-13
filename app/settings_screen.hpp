#pragma once
// The SETUP screen: the last tab. Things about this song, things about this
// handheld, and the way back into the controls setup.
//
//   up / down (stick or D-pad)   choose a row
//   left / right                 change it (a choice, such as TEXT SIZE, clicks once a push)
//   A                            on WIFI PAGE: on / off
//                                on TRACK NAME: type a name (below)
//                                on BOUNCE: write the song (or its tracks) out as WAV files
//                                on BUTTONS: the button test
//   hold A for a second          on CONTROLS: set the controls up again
//   START                        play / stop
//
// Typing a track's name: a grid of letters and numbers. The stick or D-pad
// moves, A types the one under the cursor, B deletes the last (B with
// nothing typed leaves without changing the name), Y or START keeps it.
// Ten characters at most. While it is open every button belongs to it.
//
// The button test shows what Apotheca hears from each button and the stick.
// While it is open every button goes to it and does nothing else. It closes
// by itself four seconds after the last press or let-go, even if a button
// is stuck down.

#include "screen.hpp"
#include <SDL.h>

#include <string>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class SettingsScreen : public Screen {
public:
    enum Row { kTempo, kPack, kTrackName, kBounce, kVolume, kStick, kTextSize, kOctaves, kMidi, kWifi, kTest, kControls, kRows };

    explicit SettingsScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // Where the rows are drawn, for the tests: a row's box starts at rowY and is kRowH - 6 tall;
    // its name starts at kNameX, its value at kValueX, and what is said about it ends at kNoteRight.
    static constexpr int kRowH = 32, kRowsTop = 50, kHandheldGap = 22, kRowX = 16, kRowW = 640 - 32;
    static constexpr float kNameX = 26.f, kValueX = 170.f, kNoteRight = kRowX + kRowW - 10.f;
    static constexpr int rowY(int row) { return kRowsTop + row * kRowH + (row >= kVolume ? kHandheldGap : 0); }

    void goTo(int row) { row_ = row < 0 ? 0 : (row >= kRows ? kRows - 1 : row); }
    int cursorRow() const { return row_; }
    bool testing() const { return testing_; }
    bool typing() const { return typing_; }
    const std::string& typed() const { return typed_; }
    static const char* const kKeys; // the grid's characters, row after row
    static constexpr int kKeyColumns = 10, kNameLength = 10;
    // True once after A was held a second on CONTROLS. The program then runs
    // the controls setup again (main.cpp); asking clears it.
    bool takeRelearn() { const bool was = relearn_; relearn_ = false; return was; }
    float holdPart() const { return holdPart_; } // how far through that second, 0..1

private:
    void change(int by);

    Session& session_;
    Project& project_;
    int row_ = 0;
    mutable char bounced_[64] = "";
    int bounceWhat_ = 0; // 0 the song, 1 each track, 2 this track
    bool typing_ = false;
    bool fromSong_ = false; // the keyboard was asked for from the SONG screen, and goes back there
    std::string typed_;
    int key_ = 0; // which character of the grid the cursor is on
    void typeButton(Button b);
    void moveKey(Button direction);
    bool testing_ = false, testTimed_ = false;
    Uint32 testUntil_ = 0;
    Button lastPressed_ = Button::None;
    bool holding_ = false, holdTimed_ = false;
    Uint32 holdSince_ = 0;
    float holdPart_ = 0.f;
    bool relearn_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f; // for the test's picture
    float testStickX_ = 0.f, testStickY_ = 0.f; // where the stick was when it last counted as moved
    bool held_[static_cast<int>(Button::Count)] = {};
};

} // namespace apotheca
