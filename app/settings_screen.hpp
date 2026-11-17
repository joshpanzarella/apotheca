#pragma once
// The SETUP screen: the last tab. Things about this song, things about this
// handheld, and the way back into the controls setup.
//
//   up / down (stick or D-pad)   choose a row
//   left / right                 change it (a choice, such as TEXT SIZE or THEME, clicks once a push)
//
// BRIGHTNESS and STICK LIGHT are shown only where the system gives the program the screen's
// backlight and the light round the stick (our own firmware: lights.hpp). Elsewhere the page is
// as it was, and the cursor passes them by.
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

#include "redraw.hpp"
#include "screen.hpp"
#include <cmath>
#include <SDL.h>

#include <string>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class SettingsScreen : public Screen {
public:
    enum Row { kTempo, kPack, kTrackName, kBounce, kVolume, kBrightness, kLight, kStick, kTextSize, kTheme, kOctaves, kMidi, kClick, kCountIn, kWifi, kTest, kControls, kRows };

    explicit SettingsScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { return testing_ ? kStirButtonTest : 0u; } // it closes by itself (redraw.hpp)

    // Where the rows are drawn, for the tests: a row's box starts at rowY and is kRowH - 6 tall;
    // its name starts at kNameX, its value at kValueX, and what is said about it ends at kNoteRight.
    // Twelve rows fit between the bars (thirteen since THEME, fifteen with BRIGHTNESS and STICK
    // LIGHT): the page shows twelve and moves one row at a time when the cursor goes past either end.
    // A row's PLACE is its place among the rows here (a row not here has none: `here`); `rowY` takes
    // places (`first`: the place shown at the top). The rows that can be missing come after VOLUME,
    // so VOLUME's place is its number and the HANDHELD gap starts there either way.
    // A small arrow beside PROJECT or HANDHELD says a row is out of sight that way.
    static constexpr int kRowH = 32, kRowsTop = 50, kHandheldGap = 22, kRowX = 16, kRowW = 640 - 32;
    static constexpr int kRowsShown = 12;
    static constexpr float kNameX = 26.f, kValueX = 170.f, kNoteRight = kRowX + kRowW - 10.f;
    static constexpr float kNoteGap = 8.f; // the least there is between a value and what is said about it
    static constexpr int rowY(int row, int first = 0) { return kRowsTop + (row - first) * kRowH + (row >= kVolume ? kHandheldGap : 0); }
    int rowNowY(int row) const { return rowY(placeOf(row), first_); } // where it is drawn just now
    bool here(int row) const;      // shown on this handheld at all
    int placeOf(int row) const;    // among the rows here
    int rowAt(int place) const;    // the row at a place
    int places() const { return placeOf(kRows); } // how many rows are here
    // A row's bar (VOLUME, STICK, a BOUNCE under way): from kBarX, kBarW long, unless the value drawn
    // `valueW` wide would reach it: then kBarGap after the value, and never closer than kNoteGap to what
    // is said (`noteW` wide, at the right; 0: nothing said).
    static constexpr int kBarX = 250, kBarW = 110, kBarGap = 12;
    static void barPlace(float valueW, float noteW, int* x, int* w) {
        const float after = kValueX + valueW + kBarGap, limit = kNoteRight - (noteW > 0.f ? noteW + kNoteGap : 0.f);
        *x = after > kBarX ? static_cast<int>(std::ceil(after)) : kBarX;
        const int room = static_cast<int>(std::floor(limit)) - *x;
        *w = room < kBarW ? (room > 0 ? room : 0) : kBarW;
    }
    int firstShown() const { return first_; } // the place at the top
    bool shown(int row) const { return here(row) && placeOf(row) >= first_ && placeOf(row) < first_ + kRowsShown; }

    // To a row; one that is not here gives the next that is (or the one before, at the end).
    void goTo(int row);
    void step(int by) { goTo(rowAt(placeOf(row_) + by)); } // up or down a place; the list has ends
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
    int first_ = 0; // the row at the top of the page
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
