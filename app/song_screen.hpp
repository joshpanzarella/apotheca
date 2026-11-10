#pragma once
// The Song screen: tracks across, scenes down. Each cell is one bar of one
// track. The song plays scene 1 its number of times, then scene 2, and so
// on to the last scene with anything in it, then comes round.
//
// The same rule as everywhere: the stick moves, A acts, hold A and the stick
// turns the thing under the cursor, B removes.
//
//   on a cell          A opens it on the Pattern screen. B empties it.
//                      Hold R1 and drag: copy it into every cell crossed.
//   on a repeat count  (the column left of the tracks) hold A + stick up /
//                      down: how many times the scene plays. Tap A: 1, 2, 4, 8.
//   on + TRACK         A, then pick the instrument.
//   START              play the song from the cursor's scene

#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class SongScreen {
public:
    explicit SongScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For tests and stills.
    void moveCursorTo(int scene, int column); // column -1 is the repeat count; trackCount() is + TRACK
    int cursorScene() const { return scene_; }
    int cursorColumn() const { return column_; }
    bool choosing() const { return choosing_; }

private:
    static constexpr int kRepeatColumn = -1;
    static constexpr int kVisibleRows = 8;

    int addColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : -2; } // -2: no such column
    int lastColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : kNumTracks - 1; }
    bool onCell() const { return column_ >= 0 && column_ < project_.trackCount(); }
    void moveCursor(Button direction);
    void setRepeats(int count);
    void clearCell(int track, int scene);
    void copyCell(int fromTrack, int fromScene, int toTrack, int toScene);
    void pasteHere();

    Session& session_;
    Project& project_;
    int scene_ = 0;
    int column_ = 0;
    int top_ = 0; // first scene shown
    bool choosing_ = false; // the instrument list is open
    int choice_ = 0;        // 0 drums, 1 Phazed

    bool holding_ = false;  // A is held on a repeat count
    bool turned_ = false;   // ...and the stick or D-pad changed it, so letting go is not a tap
    float saved_ = 0.f;     // part-steps of the repeat count saved up

    bool copying_ = false;  // R1 is held: the brush carries a cell
    int copyTrack_ = 0, copyScene_ = 0;

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
