#pragma once
// The Song screen: tracks across, scenes down. Each cell is one bar of one
// track. The song plays scene 1 its number of times, then scene 2, and so
// on to the last scene with anything in it, then comes round.
//
// The same rule as everywhere: the stick moves, A acts, hold A and the stick
// turns the thing under the cursor, B removes.
//
//   on a cell          A opens it on the Pattern screen. B empties it.
//                      Hold A + stick left / right: how long it is, 1, 2, 4
//                      or 8 bars. The scene lasts as long as its longest
//                      cell; a shorter one goes round under it, and is
//                      drawn once with a LOOPS mark.
//                      Hold R1 and drag: copy it into every cell crossed.
//   on a repeat count  (the column left of the tracks) hold A + stick up /
//                      down: how many times the scene plays. Tap A: 1, 2, 4, 8.
//   on + TRACK         A, then pick the instrument: drums, Phazed or Twin Analog.
//   START              play the song from the cursor's scene
//   SELECT             the list of songs: start a new one, or open another.
//                      There is no "save": the song is kept as you go.
//     L1 in the list   files over WiFi, on or off. On, it shows an address to
//                      open in a browser on the same network.
//     X in the list    save a rev: the song as it is now, to come back to.
//     Y on the open song   a new name for it (two words, rolled again each press)
//     hold B on a song     delete it: hold for a second while the row fills.
//                      It goes to a "deleted" folder on the card, not to nothing.
//                      A tap of B only closes the list.
//     A on the open song   its revs. A on one goes back to it; where the
//                      song was is kept as a rev first.

#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// Names to give a track, picked on the SETUP screen (the handheld has no
// keyboard; any other name can be typed on the sound-designer page). The
// first puts the instrument's own name back.
extern const char* const kTrackNames[];
extern const int kTrackNameCount;

class SongScreen : public Screen {
public:
    // B held this long on a track deletes it; the warning bar starts after the wait.
    static constexpr Uint32 kTrackHoldWaitMs = 500, kTrackHoldMs = 2000;
    float trackDeletePart() const { return trackHoldPart_; }
    explicit SongScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills.
    void moveCursorTo(int scene, int column); // column -1 is the repeat count; trackCount() is + TRACK
    int cursorScene() const { return scene_; }
    int cursorColumn() const { return column_; }
    bool choosing() const { return choosing_; }
    bool listingSongs() const { return listing_; }
    bool listingRevs() const { return listing_ && revsOpen_; }
    int listCursor() const { return revsOpen_ ? revCursor_ : listCursor_; }
    const std::vector<std::string>& listedSongs() const { return songs_; }
    void selectWasUsed() { cancelSelectTap(); } // SELECT did something else: letting go is not a tap

private:
    static constexpr int kRepeatColumn = -1;
    static constexpr int kVisibleRows = 8;

    int addColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : -2; } // -2: no such column
    int lastColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : kNumTracks - 1; }
    bool onCell() const { return column_ >= 0 && column_ < project_.trackCount(); }
    void moveCursor(Button direction);
    void setRepeats(int count);
    void stepBars(int by);
    void clearCell(int track, int scene);
    void copyCell(int fromTrack, int fromScene, int toTrack, int toScene);
    void pasteHere();

    Session& session_;
    Project& project_;
    int scene_ = 0;
    int column_ = 0;
    int top_ = 0; // first scene shown
    bool choosing_ = false; // the instrument list is open
    int choice_ = 0;        // 0 drums, 1 Phazed, 2 Twin Analog

    bool holding_ = false;  // A is held on a repeat count
    bool turned_ = false;   // ...and the stick or D-pad changed it, so letting go is not a tap
    float saved_ = 0.f;     // part-steps of the repeat count saved up

    bool listing_ = false;  // the list of songs is open
    std::vector<std::string> songs_; // as it was when the list opened, newest first
    int listCursor_ = 0;    // 0 is NEW SONG
    bool bHeld_ = false;    // B is down on a song's row
    Uint32 bDownAt_ = 0;    // ...since when
    int bRow_ = 0;
    void cancelSelectTap() { selectAlone_ = false; }
    bool revsOpen_ = false; // ...and inside it, the open song's revs
    std::vector<int> revs_; // newest first
    int revCursor_ = 0;     // 0 is SAVE A REV
    int savedRev_ = 0;      // the rev X just saved, to say so
    void openRevList();
    bool selectAlone_ = false; // SELECT went down and nothing else has since

    bool trackHold_ = false, trackHoldTimed_ = false; // B is being kept held on a track
    Uint32 trackHoldSince_ = 0;
    float trackHoldPart_ = 0.f;
    bool copying_ = false;  // R1 is held: the brush carries a cell
    int copyTrack_ = 0, copyScene_ = 0;

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
