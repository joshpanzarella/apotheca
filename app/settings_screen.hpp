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
    // The rows, in the order they stand, under five headings (owner, 2026-11-17: "enough rows now"):
    //   SONG      TEMPO, CHORD PACK, KEY, SCALE, TRACK NAME, BOUNCE   (kept in the song, or about it)
    //   PLAYING   CLICK, COUNT IN, USB KEYBOARD
    //   LOOK      TEXT SIZE, THEME, PIANO ROLL
    //   HANDHELD  VOLUME, BRIGHTNESS, STICK LIGHT, LIGHT COLOUR, STICK SPEED
    //   SETUP     WIFI PAGE, BUTTONS, CONTROLS
    // The numbers are kept nowhere (a setting is kept by its word in settings.txt): the order is free.
    enum Row { kTempo, kPack, kKey, kScale, kTrackName, kBounce, kClick, kCountIn, kMidi, kTextSize, kTheme, kOctaves,
               kVolume, kBrightness, kLight, kLightColour, kStick, kWifi, kTest, kControls, kRows };
    enum Group { kSong, kPlaying, kLook, kHandheld, kSetup, kGroups };
    static constexpr int groupOf(int row) { return row < kClick ? kSong : row < kTextSize ? kPlaying : row < kVolume ? kLook : row < kWifi ? kHandheld : kSetup; }
    static const char* groupName(int group);

    explicit SettingsScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { return testing_ ? kStirButtonTest : 0u; } // it closes by itself (redraw.hpp)

    // Where the rows are drawn, for the tests: a row's box starts at rowY and is kRowH - 6 tall;
    // its name starts at kNameX, its value at kValueX, and what is said about it ends at kNoteRight.
    // The page shows as many rows as fit between the bars and moves one row at a time when the cursor
    // goes past either end. The heading of the first row shown stands at kHeadingY, above the rows;
    // every other heading in view takes kHeadingGap before its group's first row, its words kHeadingUp
    // over that row. So twelve rows are in view with one heading among them (kRowsShown, the most),
    // eleven with two, ten with three.
    // A row's PLACE is its place among the rows here (a row not here has none: `here`); `placeY` takes
    // places (`first`: the place shown at the top). A group's first row is always here (only
    // BRIGHTNESS, STICK LIGHT and LIGHT COLOUR can be missing), so every heading has a row to stand on.
    // A small arrow after the top heading says a row is out of sight above, one after the last heading
    // in view that one is out of sight below.
    static constexpr int kRowH = 32, kRowsTop = 50, kHeadingGap = 22, kHeadingUp = 18, kHeadingY = 34, kRowX = 16, kRowW = 640 - 32;
    static constexpr int kRowsShown = 12;
    static constexpr float kArrowsApart = 18.f; // an UP and a DOWN arrow after one heading
    static constexpr int kPageFoot = kRowsTop + kRowsShown * kRowH + kHeadingGap - 6; // where the last row's box may end (450)
    static constexpr float kNameX = 26.f, kValueX = 170.f, kNoteRight = kRowX + kRowW - 10.f;
    static constexpr float kNoteGap = 8.f; // the least there is between a value and what is said about it
    int placeY(int place, int first) const; // a place's top with `first` at the top of the page
    bool fits(int place, int first) const { return place >= first && placeY(place, first) + kRowH - 6 <= kPageFoot; }
    bool headsGroup(int place) const { return place > 0 && groupOf(rowAt(place)) != groupOf(rowAt(place - 1)); } // a heading stands over it
    int rowNowY(int row) const { return placeY(placeOf(row), first_); } // where it is drawn just now
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
    bool shown(int row) const { return here(row) && fits(placeOf(row), first_); }

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
