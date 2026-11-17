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
//                      or 8 bars. Hold A + UP / DOWN: TRANSPOSE its notes a semitone
//                      (the stick up / down: an octave; Session::transposeCell). The scene lasts as long as its longest
//                      cell; a shorter one goes round under it, and is
//                      drawn once with a LOOPS mark.
//                      Hold R1 and drag: copy it into every cell crossed.
//   on a repeat count  (the column left of the tracks) hold A + stick up /
//                      down: how many times the scene plays. Tap A: 1, 2, 4, 8.
//   on + TRACK         A, then pick the instrument: drums, Phazed, JAX-EP...
//   on a track's head  (up from the top scene) tap A: its name. R1: its instrument. Hold B: delete it.
//                      Hold A + UP / DOWN: its LEVEL, a dB a press; + LEFT / RIGHT: its PAN.
//                      The stick, with A held, for big moves. B with A held: back to the usual
//                      level and pan (0 DB, the middle). A changed one has a brass mark on its head.
//   on MASTER          (the head at the right-hand end) the same: hold A, the master's level and pan.
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

// What the mixer card says: "-6 DB", "0 DB", "+3 DB"; "L 30", "MIDDLE", "R 12".
void mixLevelWords(int db, char* out, int size);
void mixPanWords(int pan, char* out, int size);

class SongScreen : public Screen {
public:
    // A head not at its usual level or pan: a brass square this big, this far in from its top left corner.
    static constexpr int kMixMarkSize = 6, kMixMarkInset = 3;
    // LIGHT COLOUR at TRACK (Session::trackColoursShown): a bar of the track's ring colour along the foot of
    // its head, kHueMarkH high and ending kHueMarkFoot above the head's lower edge, kHueMarkInset in from each
    // side, a line of the dim lettering over it (so it has an edge on a light theme). Under the name, clear
    // of the M / S mark and the mixer's mark, which are at the head's top.
    static constexpr int kHueMarkH = 2, kHueMarkFoot = 1, kHueMarkInset = 3;
    // B held this long on a track deletes it; the warning bar starts after the wait.
    static constexpr Uint32 kTrackHoldWaitMs = 500, kTrackHoldMs = 2000;
    // A held this long on a head is the mixer, not a tap: its card comes up, and letting go then never
    // names the track, turned or not (owner, 2026-11-14: a look at the level opened the keyboard).
    static constexpr Uint32 kMixCardMs = 300;
    // What the hints bar says while the head fills (the head has room for one word).
    static constexpr const char* kTrackDeletingWords = "deleting the track: cannot be undone";
    // Under the list of instruments when a track's is being changed: what goes with the old one.
    static constexpr const char* kChangeLostWords[2] = {"ITS SOUND AND ITS KNOB MOVES GO.", "THIS CANNOT BE UNDONE."};
    static constexpr int kChangeLostH = 44;
    // The list of instruments: a box of rows kInstrumentPitch apart, its top at kListY, or higher where it
    // would reach the hints bar (six instruments since PHYSIC, with what goes said under them: 46 higher).
    static constexpr int kListY = 70, kInstrumentPitch = 52, kListBottom = ui::kScreenH - ui::kHintsH - 8;
    // Seven instruments (ACID's, 2026-11-16) and the words under them do not fit at the usual pitch even
    // standing as high as the top bar lets: only then the rows close up (47, a pixel between boxes).
    static constexpr int kListRoom = kListBottom - ui::kTopBarH - 1;
    static constexpr int listPitch(int instruments, bool changing) {
        return 46 + instruments * kInstrumentPitch + (changing ? kChangeLostH : 0) <= kListRoom ? kInstrumentPitch : (kListRoom - 46 - (changing ? kChangeLostH : 0)) / instruments;
    }
    // A row's box: 46 high, or a pixel short of the pitch where the rows close up (eight since CLOUD:
    // 46 adding a track, 41 with the words under them), its two lines placed within it.
    // Between a closed-up row's two lines (the name, in capitals, and what it is), and the room the second
    // line's tails and the outline take (3 and a pixel either end): both lines fit a box of their sum.
    static constexpr float kListLineGap = 4.f;
    static constexpr float kListTailRoom = 5.f;
    static constexpr int rowH(int instruments, bool changing) { return listPitch(instruments, changing) - 1 < 46 ? listPitch(instruments, changing) - 1 : 46; }
    static constexpr int listH(int instruments, bool changing) { return 46 + instruments * listPitch(instruments, changing) + (changing ? kChangeLostH : 0); }
    static constexpr int listTop(int instruments, bool changing) {
        return kListBottom - listH(instruments, changing) < kListY ? kListBottom - listH(instruments, changing) : kListY;
    }
    // On a song's row while B is held on it.
    static constexpr const char* kSongDeletingWords = "CANNOT BE UNDONE"; // (short: the song's name shares the row)
    float trackDeletePart() const { return trackHoldPart_; }
    explicit SongScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills.
    void moveCursorTo(int scene, int column); // column -1 is the repeat count; trackCount() is + TRACK
    int cursorScene() const { return scene_; }
    int topScene() const { return top_; } // the first scene shown
    int cursorColumn() const { return column_; }
    bool cursorOnHead() const { return onHead_; }
    bool cursorOnMaster() const { return onMaster_; }
    static int headMarkWidth(const Fonts& fonts, const char* letter); // a head's M or S mark, made to its letter
    bool mixing() const { return mixing_; } // A is held on a head (a track's or MASTER): the directions are the mixer's
    void showMixCardForStill() { mixShown_ = mixTimed_ = true; } // (a still is taken before kMixCardMs has gone by)
    bool mixCardUp() const { return mixing_ && (mixShown_ || mixTurned_); } // ...and its card is drawn: held kMixCardMs, or something turned
    void moveToMaster() { onHead_ = false, onMaster_ = true; }
    // The mixer card while A is held on a head: placed under the head (for the tests to read).
    static constexpr int kMixCardW = 136, kMixCardH = 62;
    struct Place { int x, y; };
    Place mixCard() const;
    SDL_Rect headRect(int track) const; // a track's head (or MASTER's, kMasterTrack) as it is drawn
    bool choosing() const { return choosing_; }
    bool listingSongs() const { return listing_; }
    bool listingRevs() const { return listing_ && revsOpen_; }
    int listCursor() const { return revsOpen_ ? revCursor_ : listCursor_; }
    const std::vector<std::string>& listedSongs() const { return songs_; }
    void scenesMoved(int from, int by); // a scene put in or taken out (NEXT SCENE): the cursor stays on the scene it was on
    void selectWasUsed() { cancelSelectTap(); } // SELECT did something else: letting go is not a tap
    // AT END: the PLAYS box of the first empty row, just past the song's last scene, is where the
    // song ends. It shows a loop (LOOP) or a stop square (STOP), and A there switches them.
    int songEndRow() const { return project_.sceneCount() < kMaxScenes ? project_.sceneCount() : -1; } // -1: every row is a scene
    bool onSongEnd() const { return !onMaster_ && column_ == kRepeatColumn && scene_ == songEndRow(); }
    SDL_Rect repeatBox(int scene) const; // a scene's PLAYS box as it is drawn (w 0: not in view)
    static void drawSongEnd(SDL_Renderer* r, int cx, int cy, int end, SDL_Color color); // the mark, centred on (cx, cy)

private:
    static constexpr int kRepeatColumn = -1;
    static constexpr int kVisibleRows = 8;

    int addColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : -2; } // -2: no such column
    int lastColumn() const { return project_.trackCount() < kNumTracks ? project_.trackCount() : kNumTracks - 1; }
    bool onCell() const { return !onMaster_ && column_ >= 0 && column_ < project_.trackCount(); }
    int masterX() const; // where the MASTER head starts
    int mixTrack() const { return onMaster_ ? kMasterTrack : column_; }
    void mixStep(Button direction); // A held on a head: a dB up or down, a step of pan left or right
    void moveCursor(Button direction, bool pressed = false); // pressed: a press of the D-pad itself, not a run of it or the stick
    void setRepeats(int count);
    void stepBars(int by);
    void clearCell(int track, int scene);
    void copyCell(int fromTrack, int fromScene, int toTrack, int toScene);
    void pasteHere();

    Session& session_;
    Project& project_;
    int changing_ = -1;   // the instrument list is up to change this track's (-1: to add a track)
    bool onHead_ = false; // the cursor is on a track's head (its name), above the top scene
    bool onMaster_ = false; // ...or on MASTER, the head at the right-hand end
    bool addFromHeads_ = false; // on + TRACK, come to along the heads: left goes back to the last head, not its cell
    bool mixing_ = false;   // A is held on a head: the directions turn its level and pan
    bool mixTurned_ = false; // ...and something was turned, so letting go is not a tap (a tap on a track's head names it)
    bool mixShown_ = false, mixTimed_ = false; // ...held kMixCardMs: the card is up, and letting go is not a tap either
    Uint32 mixSince_ = 0;
    float mixSavedDb_ = 0.f, mixSavedPan_ = 0.f; // the stick's part-steps saved up
    int scene_ = 0;
    int column_ = 0;
    int top_ = 0; // first scene shown
    bool choosing_ = false; // the instrument list is open
    int choice_ = 0;        // its place in kInstruments

    bool holding_ = false;  // A is held on a repeat count
    bool turned_ = false;   // ...and the stick or D-pad changed it, so letting go is not a tap
    float saved_ = 0.f;     // part-steps of the repeat count saved up
    // TRANSPOSE (easywins batch 3): A held on a cell, UP / DOWN a semitone, the stick up / down an octave.
    int transposed_ = 0;    // how far this hold has moved the cell, for what is said
    float savedY_ = 0.f;
    bool stickWasYHeld_ = false;
    void transpose(int by);

    bool listing_ = false;  // the list of songs is open
    std::vector<std::string> songs_; // as it was when the list opened, newest first
    int listCursor_ = 0;    // 0 is NEW SONG
    bool bHeld_ = false;    // B is down on a song's row
    Uint32 bDownAt_ = 0;    // ...since when
    int bRow_ = 0;
    void cancelSelectTap() { selectAlone_ = false; }
    bool revsOpen_ = false; // ...and inside it, the open song's revs
    std::vector<int> revs_; // newest first
    int revCursor_ = 0;     // 0 is SAVE A REV, 1 NEW NAME, then the revs
    static constexpr int kNewNameRow = 1, kRevsFirst = 2;
    int savedRev_ = 0;      // the rev Y just saved, to say so
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
