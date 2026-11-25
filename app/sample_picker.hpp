#pragma once
// The recordings, for picking one quickly. The kit's lanes and the sampler
// both open it: a tap of A on SOUND.
//
// Three columns, as the file manager yazi (owner, 2026-11-14): where this
// folder is, this folder with the cursor in it, and what is under the cursor
// (a folder's contents, or a recording's picture and length). Folders go two
// levels deep. The columns stand still (owner, 2026-11-15: "start out with the
// sound preview open to the right of the 2 levels ... that way it doesn't need
// to expand"): on SAMPLES the cursor is in the FIRST column, what is under it in
// the second and the preview, open from the start, in the third; going into a
// folder the cursor steps right into the second column and nothing else moves.
// Inside a folder the cursor's column is the middle one (two folders down, the
// folder it is in is the first column). With no folders at all the one list is
// the middle column, as it always was.
//
//   up / down      the next in the column; the ends stop. A recording under
//                  the cursor goes onto the lane AT ONCE, so with the song
//                  playing it is heard in its place in the beat; with the
//                  song stopped it is played, half of it at least (from where
//                  it plays from: the sampler's START), and stopped as soon
//                  as the cursor moves on. Held: the list's pace
//                  (ui::kListEveryMs), faster after kFasterAfterMs, and after
//                  kFastestAfterMs as fast as kFastestEveryMs (a long list's end).
//   the stick      big moves: up to kStickRowsPerSecond, by the SQUARE of how far it
//                  is pushed (a little push is slow enough to read). Moving faster than
//                  one row every kAtOnceMs, a recording goes on when the
//                  cursor rests, not each one passed (each is a file read).
//   left / right   out to the folder this one is in / into the folder under
//                  the cursor
//   L1 / R1        a page up / down, and held: page after page at the list's pace. (R1 on a
//                  recording that can be carried: one page, when it is let go, since held it carries.)
//   hold R1        on a recording: CARRY it (hold R1 drags, on every screen). After kCarryHoldMs,
//                  or at once with a direction: steer with up / down / left / right, the stick
//                  and L1, and let go to put it in the folder under the cursor, or else in the
//                  folder showing (SAMPLES itself: out of every folder). The top line says where
//                  it would go. Nothing goes on the lane meanwhile. B with R1 held calls it off.
//                  Every saved song and rev that names it follows (Session::moveSample).
//   A              on a recording: keep it. On a folder: into it. On + NEW FOLDER (the last row
//                  of SAMPLES and of a first-level folder): the name keys (name_keys.hpp: A types,
//                  Y deletes, B or START makes it; nothing typed, B leaves).
//   B              a tap: put back what was there before the list was opened. Where holding B does
//                  something (a recording, a folder), only a TAP: let go after kTapMs, or after
//                  another button meanwhile, and nothing happens (gesture audit G42: a delete let go
//                  early closed the list). Elsewhere (the first row, + NEW FOLDER) B always goes back
//   hold B         for a second: DELETE the recording under the cursor from the
//                  handheld, for good (a bad catch, a take not wanted), or an
//                  EMPTY folder (nothing at all in it). Not the first row. Hold B is "delete for good" on
//                  every screen. Held on a folder that is NOT empty: after kHoldSaysMs the top line
//                  says so.
//   Y, R1          with no recordings and no folders at all: the list closes (as B's tap) and Y
//                  catches the last bar, R1 records, as the words in the list say (Jobs::catchBar,
//                  Jobs::record)
//   X              while the preview plays: it plays on to its end (owner,
//                  2026-11-15). Nothing playing: heard again, for as long as X
//                  is held (to its end); a tap is heard for a moment
//
// The first column (SAMPLES) holds the first row ("THE BUILT-IN DRUM"), then
// UNSORTED (the loose recordings, if there are any) and the folders. With no
// folders at all there is nothing to go into: the loose recordings are listed
// there themselves, as the list always was.
//
// The list knows nothing of lanes or samplers: it is given the names and
// the things it can do (use this one, play it, tell me about it).

#include <SDL.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "redraw.hpp"
#include "input.hpp"
#include "name_keys.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class SamplePicker {
public:
    // How what is on is to be heard (Jobs::hear).
    enum class Hear {
        Preview, // the cursor came to it: half of it at least (Session::kAuditionMs at least), then it stops
        Held,    // X went down with nothing playing: until X is let go (to its end at most)
        Rest,    // X went down while the preview plays: on to its end, not begun again
        Quiet,   // the cursor moved on: what the list started stops now
    };
    struct Jobs {
        std::function<void(const std::string&)> use;       // put this recording on ("" is the first row: none)
        std::function<void(Hear)> hear;                    // play what is on now (or stop it)
        std::function<bool()> sounding;                    // what the list started is still heard (Session::hearingOn); may be empty
        std::function<const Session::LaneSound*()> sound;  // what is on now, for its picture; may be empty
        std::function<bool(const std::string&)> remove;    // delete this recording's file; true if it is gone
        std::function<const ui::Playhead*()> playhead;     // where the sound being heard has got to, for a line across its picture
        std::function<int(const std::string&)> used;       // how many OTHER places in the open song have this recording on; may be empty
        std::function<Session::FileJob(const std::string& from, const std::string& to)> move; // carry: the recording `from` becomes `to`
        std::function<Session::FileJob(const std::string&)> makeFolder;   // a new, empty folder of this whole name
        std::function<Session::FileJob(const std::string&)> removeFolder; // delete this folder, if it is empty
        std::function<bool()> following;                   // saved songs are still following a move (Session::following); may be empty
        std::function<int()> followFailures;               // Session::followFailures; may be empty
        std::function<bool()> copying;                     // a carry is being copied (Session::carryCopying); may be empty
        std::function<Session::FileJob()> copied;          // how the copy's whole move went, once it is not copying (Session::carryResult)
        // With nothing on the card, the list says where recordings come from: Y and R1 there close it (as B's tap)
        // and do these, as Y and R1 do on the screen that opened it. May be empty.
        std::function<void()> catchBar;                    // Y: the last bar the song played, caught as a recording
        std::function<void()> record;                      // R1: record from the input (again: stop)
        // B's tap, after use() has put back the recording that was on (where another is on now): what a pick takes
        // off besides the name goes back too (a lane's cut: each recording put on gives its lane the whole, the
        // one it had as well). Given that recording's name as it is now ("": deleted meanwhile). May be empty.
        std::function<void(const std::string&)> putBack;
    };
    // A row of a column.
    enum class Kind { First, Folder, Recording, NewFolder };
    struct Row {
        Kind kind;
        std::string name; // a recording's whole name ("drums/kicks/808"), or a folder's ("drums/kicks"; "" is UNSORTED)
        int count;        // a folder: the recordings in it, those of the folders inside it too
    };
    static constexpr int kRows = 11, kPage = kRows - 1;
    // A held D-pad: the list's pace first, then this; held longer, this: the far end of a long list (named in ui.hpp,
    // the one exception to its paces).
    static constexpr Uint32 kFasterAfterMs = ui::kRecordingsFasterAfterMs, kFasterEveryMs = ui::kRecordingsFasterEveryMs;
    static constexpr Uint32 kFastestAfterMs = ui::kRecordingsFastestAfterMs, kFastestEveryMs = ui::kRecordingsFastestEveryMs;
    static constexpr float kStickRowsPerSecond = 80.f;                  // the stick pushed all the way (times ui::gStickSpeed); by the square of the push
    static constexpr Uint32 kHoldSaysMs = 400;                          // B held this long on a folder that is not empty: it says why not
    static constexpr Uint32 kTapMs = 250;                               // B let go sooner on a row it deletes (nothing else pressed) is a tap; later, nothing
    static constexpr Uint32 kAtOnceMs = 90;                             // rows arrived at closer together than this: the one rested on goes on
    static constexpr Uint32 kDeleteHoldMs = ui::kHoldForGoodMs; // (a recording or a folder on the card: for good)
    static constexpr Uint32 kCarryHoldMs = 300;  // R1 held this long on a recording carries it; let go sooner, it is a page
    static constexpr Uint32 kSaidMs = 2500;      // how long the top line says what was done, or why not
    static constexpr int kFolderNameLength = 12; // the longest a folder's name is typed
    static constexpr const char* kKeys = NameKeys::kKeys; // the keys a name is typed with (name_keys.hpp: the one set)

    // `folders`: the folders there are (Session::sampleFolders), the empty ones too. `first` names
    // the row before everything ("THE BUILT-IN DRUM").
    void open(std::vector<std::string> names, std::vector<std::string> folders, const std::string& current, const char* title, const char* first, Jobs jobs);
    void rescan(std::vector<std::string> names, std::vector<std::string> folders); // the card's lists read again (WiFi): the rows again, the cursor kept
    bool isOpen() const { return open_; }
    void onButton(const ButtonEvent& event, bool playing);
    void update(const Input& input, Uint32 nowMs, bool playing);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;
    // What changes here with no button (redraw.hpp): the row rested on about to go on, a line said for a moment.
    unsigned stirring() const { return !open_ ? 0u : (waiting_ ? kStirPicker : 0u) | (!said_.empty() ? kStirScreenSaid : 0u); }

    int cursor() const { return cursor_; }                          // which row of the middle column, from 0
    int count() const { return static_cast<int>(rows_.size()); }    // how many rows it has
    int depth() const { return depth_; }                            // 0: SAMPLES. 1: a folder (or UNSORTED). 2: a folder in a folder
    const std::string& folderName() const { return here_; }         // the folder showing: "" is SAMPLES, or UNSORTED at depth 1
    const std::vector<Row>& rows() const { return rows_; }
    const Row* under() const { return cursor_ >= 0 && cursor_ < count() ? &rows_[static_cast<std::size_t>(cursor_)] : nullptr; }
    bool flat() const { return dirs_.empty(); }                     // no folders at all: one list
    float deletePart() const { return deletePart_; }
    // B held on a recording or a folder, its let go never seen (L2 / R2 turned the tab): called off, so the bar
    // does not come back full (the screen's callOffDelete, from App::update).
    void callOffDelete() { deleting_ = refusing_ = false, deletePart_ = 0.f; }
    const std::string& carried() const { return carried_; }         // the recording being carried ("": none)
    std::string carryTo() const;                                    // the folder it would go to if R1 were let go now ("": out of every folder)
    bool typing() const { return keys_.isOpen(); }                  // a new folder's name is being typed: every button is its
    const std::string& typed() const { return keys_.typed(); }
    const std::string& said() const { return said_; }               // what the top line says just now ("": the title)
    bool moving() const { return moving_; }                         // a move's saved songs are following: the top line says MOVING TO
    bool copying() const { return copying_; }                       // a move is being copied (a card that will not rename): MOVING TO too
    // The rows of a place, as a column lists them.
    std::vector<Row> rowsAt(int depth, const std::string& folder) const;
    int rowsMade() const { return rowsMade_; }                 // how many times a column's rows were worked out (draw() does not, frame after frame)

    // Where things are drawn, for the tests that read the screen.
    static constexpr int kX = 8, kW = ui::kScreenW - 16, kY = 34, kH = ui::kScreenH - ui::kHintsH - kY - 8;
    static constexpr int kGap = 8, kLeftW = 150, kMidW = 254, kRightW = kW - 24 - 2 * kGap - kLeftW - kMidW;
    static constexpr int kLeftX = kX + 12, kMidX = kLeftX + kLeftW + kGap, kRightX = kMidX + kMidW + kGap;
    static constexpr int kHeadY = kY + 30, kRowsY = kHeadY + 22, kRowH = 30, kPictureH = 70;
    // Which column the cursor is in: the first on SAMPLES (folders there), else the middle one.
    bool cursorFirst() const { return depth_ == 0 && !flat(); }
    int cursorX() const { return cursorFirst() ? kLeftX : kMidX; }
    int cursorW() const { return cursorFirst() ? kLeftW : kMidW; }

private:
    void moveTo(int row, bool playing, bool atOnce);
    void arrive(bool playing, bool atOnce);  // the cursor has come to a row: a recording goes on
    void put(bool playing);                  // what the cursor is on goes on now
    void show(int depth, const std::string& folder);
    void into(bool playing);                 // the folder under the cursor
    void out();                              // to the folder this one is in
    int step(Button b) const;
    void refresh(const std::string& onto = "");  // the rows again, after the card changed; the cursor on its row (or the row of this key)
    void drop(bool playing);                 // R1 let go while carrying
    void moved(const std::string& from, const std::string& to, const std::string& folder, Session::FileJob did, int failuresBefore); // a move's end
    void typeButton(Button b);
    void keepFolder();
    void say(const std::string& words) { said_ = words; saidUntil_ = lastMs_ + kSaidMs; }
    void putBack();                          // B's tap: what was on before goes back on, and the list closes
    bool nothing() const { return all_.empty() && dirs_.empty(); } // no recording and no folder on the card
    void quiet() { if (jobs_.hear) jobs_.hear(Hear::Quiet); } // what the list started stops (the cursor moved on)
    void drawPreview(SDL_Renderer* r, const Fonts& fonts, const Row* row) const; // the third column: a recording's picture
    bool emptyFolder(const Row& row) const;  // a folder hold B deletes
    bool carries(const Row* row) const { return row && row->kind == Kind::Recording && jobs_.move && !flat(); } // hold R1 carries it
    void index();                            // all_ or dirs_ changed: the counts again
    void setLists(std::vector<std::string> names, std::vector<std::string> folders); // all_ and dirs_ new: the folders implied too, then the counts
    const std::vector<Row>& side(std::vector<Row>& rows, std::string& key, int depth, const std::string& folder) const;
    void drawTyping(SDL_Renderer* r, const Fonts& fonts) const;
    void drawColumn(SDL_Renderer* r, const Fonts& fonts, int x, int w, const std::vector<Row>& rows, int lit, bool cursor, int first) const;

    bool open_ = false;
    std::vector<std::string> all_;   // every recording, by its whole name
    std::vector<std::string> dirs_;  // every folder: "drums", "drums/kicks"
    int depth_ = 0;
    std::string here_;
    std::vector<Row> rows_;          // the middle column
    std::map<std::string, int> counts_; // how many recordings are under each folder, its inner folders' too
    int loose_ = 0, changes_ = 0;       // the recordings in no folder; how many times the card's lists changed
    mutable int rowsMade_ = 0;
    mutable std::vector<Row> leftRows_, rightRows_; // the side columns as last drawn: made again only when the place or the card changes
    mutable std::string leftKey_, rightKey_;
    std::map<std::string, std::string> last_; // where the cursor was in a folder left, by the row's name (this opening only)
    std::string original_, on_, title_, first_;
    Jobs jobs_;
    int cursor_ = 0;
    bool waiting_ = false;           // the row under the cursor has not gone on yet (moving fast)
    Uint32 arrivedAt_ = 0;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    ui::Repeat repeat_; // the held direction's repeats (ui.hpp)
    Uint32 repeatSince_ = 0, lastMs_ = 0;
    bool bDown_ = false;                          // B went down while the list was open (so its let-go is the list's), and is still a tap
    bool bHolds_ = false;                         // ...on a row where holding it does something (a bar fills, or a folder says why not)
    Uint32 bSince_ = 0;                           // when it went down
    bool deleting_ = false, deleteTimed_ = false; // B is held on a recording
    bool refusing_ = false;                       // B is held on a folder that is not empty
    Uint32 deleteSince_ = 0;
    float deletePart_ = 0.f;
    bool r1Down_ = false, r1Timed_ = false;       // R1 went down on a recording: a page when let go, a carry if held
    Uint32 r1Since_ = 0;
    std::string carried_;
    NameKeys keys_;                               // a new folder's name being typed
    std::string typingIn_;                        // the folder it will be in ("": SAMPLES)
    std::string said_;
    Uint32 saidUntil_ = 0;
    std::string movingTo_;           // a move whose saved songs are still following: said MOVING until they are
    int failuresBefore_ = 0;         // Session::followFailures when it began
    bool moving_ = false;
    // A move being copied: nothing has changed on the card yet, so the list shows it where it was until
    // the copy ends (update), then as any move (moved).
    bool copying_ = false;
    std::string copyFrom_, copyTo_, copyFolder_;
    int copyFailuresBefore_ = 0;
};

} // namespace apotheca
