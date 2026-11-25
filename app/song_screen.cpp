#include "song_screen.hpp"

#include <algorithm>
#include "instruments.hpp"
#include "synth_spec.hpp"

#include <cstring>

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kDeleteHoldMs = 1000; // B held this long on a song deletes it

constexpr int kGridTop = 34, kHeadH = 40, kRowH = 44, kRowsTop = kGridTop + kHeadH + 6;
constexpr int kNumberW = 26, kRepeatW = 38, kColumnsLeft = 8 + kNumberW + kRepeatW; // (kRepeatW was 34: the column's word, REPEATS, is 67 wide at LARGE)
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kStickCellsPerSecond = 24.f;
constexpr float kRepeatsPerSecond = 20.f;
constexpr float kSceneTempoPerSecond = 90.f; // L1 held on a repeat count, the stick fully over: BPM a second (the drum grid's tempo row's)
constexpr float kRepeatSidePerSecond = 3.f;  // ...sideways, after the first click: the scene's steps (A) or its meter (L1)
constexpr float kMixDbPerSecond = 24.f, kMixPanPerSecond = 60.f; // the stick, with A held on a head, at full push


// The columns share the width of the screen: few tracks, wide columns. The
// "+ TRACK" column (there until all twelve are used) is one of them, and so is MASTER, last.
} // namespace

const char* const kTrackNames[] = {"(ITS INSTRUMENT)", "DRUMS", "KICK", "SNARE", "HATS", "PERC", "BREAK", "BASS", "SUB", "LEAD", "MELODY", "CHORDS", "PAD",
                                   "KEYS",             "PLUCK", "ARP",  "BELL",  "STRINGS", "BRASS", "ORGAN", "VOX", "FX",  "NOISE", "TEXTURE", "DRONE", "HOOK"};
const int kTrackNameCount = static_cast<int>(sizeof kTrackNames / sizeof kTrackNames[0]);

namespace {
int columnWidth(int tracks) {
    const int columns = tracks + (tracks < kNumTracks ? 1 : 0) + 1; // (+ 1: MASTER)
    return (kScreenW - 8 - kColumnsLeft) / (columns < 1 ? 1 : columns) - 1;
}

} // namespace

void mixLevelWords(int db, char* out, int size) { std::snprintf(out, static_cast<std::size_t>(size), db > 0 ? "+%d DB" : "%d DB", db); }
void mixPanWords(int pan, char* out, int size) {
    if (pan == 0) std::snprintf(out, static_cast<std::size_t>(size), "MIDDLE");
    else std::snprintf(out, static_cast<std::size_t>(size), "%c %d", pan < 0 ? 'L' : 'R', pan < 0 ? -pan : pan);
}

int SongScreen::masterX() const {
    const int tracks = project_.trackCount();
    return kColumnsLeft + (tracks + (addColumn() >= 0 ? 1 : 0)) * (columnWidth(tracks) + 1);
}

SDL_Rect SongScreen::headRect(int track) const {
    const int colW = columnWidth(project_.trackCount());
    return {track == kMasterTrack ? masterX() : kColumnsLeft + track * (colW + 1), kGridTop, colW, kHeadH};
}

SongScreen::Place SongScreen::mixCard() const {
    const int colW = columnWidth(project_.trackCount());
    const int x = onMaster_ ? masterX() : kColumnsLeft + column_ * (colW + 1);
    int left = x + colW / 2 - kMixCardW / 2;
    if (left < 4) left = 4;
    if (left + kMixCardW > kScreenW - 4) left = kScreenW - 4 - kMixCardW;
    return {left, kGridTop + kHeadH + 3};
}

void SongScreen::mixStep(Button direction) {
    const int track = mixTrack();
    int db = session_.mixDb(track), pan = session_.mixPan(track);
    if (direction == Button::Up) ++db;
    if (direction == Button::Down) --db;
    if (direction == Button::Right) ++pan;
    if (direction == Button::Left) --pan;
    session_.setMix(track, db, pan);
    mixTurned_ = true;
}

void SongScreen::moveCursorTo(int scene, int column) {
    movedThisVisit_ = true;
    onHead_ = onMaster_ = addFromHeads_ = false;
    scene_ = scene < 0 ? 0 : (scene >= kMaxScenes ? kMaxScenes - 1 : scene);
    column_ = column < kRepeatColumn ? kRepeatColumn : (column > lastColumn() ? lastColumn() : column);
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

void SongScreen::openOnWorkingCell() {
    moveCursorTo(project_.scene, project_.track);
    movedThisVisit_ = false;
}

bool SongScreen::movedToCell(int* track, int* scene) const {
    if (!movedThisVisit_ || !onCell() || onHead_) return false;
    *track = column_;
    *scene = scene_;
    return true;
}

// The scenes from `from` on are one place further down the song (by = 1) or back up it (-1).
void SongScreen::scenesMoved(int from, int by) {
    cellCopied_ = false; // a scene put in or taken out: the copied cell moved, so the copy goes
    cellCopy_.has = false;
    if (scene_ < from || scene_ + by < 0 || scene_ + by >= kMaxScenes) return;
    scene_ += by;
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

void SongScreen::moveCursor(Button direction, bool pressed) {
    if (listing_) {
        // A press of the D-pad at either end goes round to the other; a run (held, or the stick) stops there (G35).
        int& cursor = revsOpen_ ? revCursor_ : listCursor_;
        const int last = static_cast<int>(revsOpen_ ? revs_.size() + kRevsFirst - 1 : songs_.size());
        if (direction == Button::Up || direction == Button::Down) cursor = ui::listStep(cursor, direction == Button::Down ? 1 : -1, last + 1, pressed);
        return;
    }
    if (choosing_) {
        if (direction == Button::Up) choice_ = (choice_ + kInstrumentCount - 1) % kInstrumentCount;
        if (direction == Button::Down) choice_ = (choice_ + 1) % kInstrumentCount;
        return;
    }
    movedThisVisit_ = true; // (G46: a tab off SONG takes the cell it is on now)
    // MASTER, the head at the right-hand end: back left to + TRACK, or the last track's head.
    if (onMaster_) {
        if (direction == Button::Left) {
            if (addColumn() >= 0) moveCursorTo(scene_, addColumn()), addFromHeads_ = true;
            else {
                onMaster_ = false;
                onHead_ = true;
                column_ = project_.trackCount() - 1;
            }
        }
        return;
    }
    // Up from the top scene is the track's head: its name. R1 there types a new one.
    if (onHead_) {
        if (direction == Button::Down) onHead_ = false;
        // Along the heads. Off either end is what is drawn beside them: + TRACK on the right
        // (when there is room for another track), the PLAYS column on the left.
        if (direction == Button::Left) {
            if (column_ > 0) --column_;
            else moveCursorTo(scene_, kRepeatColumn);
        }
        if (direction == Button::Right) {
            if (column_ + 1 < project_.trackCount()) ++column_;
            else if (addColumn() >= 0) moveCursorTo(scene_, addColumn()), addFromHeads_ = true;
            else moveToMaster(); // twelve tracks: no + TRACK between
        }
        return;
    }
    // Right of + TRACK (drawn at the heads' height) is MASTER.
    if (direction == Button::Right && column_ == addColumn()) {
        moveToMaster();
        return;
    }
    // Left of + TRACK, come to from the heads (MASTER or the last head): back along the heads, not down into
    // the last track's cell (owner, 2026-11-14). Come to from a cell, left goes back to that cell's row.
    if (direction == Button::Left && column_ == addColumn() && addFromHeads_ && project_.trackCount() > 0) {
        onHead_ = true;
        column_ = project_.trackCount() - 1;
        addFromHeads_ = false;
        return;
    }
    // Only by a press of the D-pad: the stick, or a held D-pad, running up the scenes stops at the top one.
    if (direction == Button::Up && scene_ == 0 && onCell() && pressed) {
        onHead_ = true;
        return;
    }
    if (direction == Button::Up) moveCursorTo(scene_ - 1, column_);
    if (direction == Button::Down) moveCursorTo(scene_ + 1, column_);
    if (direction == Button::Left) moveCursorTo(scene_, column_ - 1);
    if (direction == Button::Right) moveCursorTo(scene_, column_ + 1);
}

// TRANSPOSE: the cell's notes and chords `by` semitones (Session::transposeCell), said in the box above the bar.
void SongScreen::transpose(int by) {
    turned_ = true; // (letting go of A does not open the cell)
    const Session::Transposed done = session_.transposeCell(column_, scene_, by);
    char text[48];
    if (done == Session::kTransposed) {
        transposed_ += by;
        std::snprintf(text, sizeof text, "%s %+d SEMITONE%s", transposed_ == 0 ? "BACK:" : "MOVED", transposed_, transposed_ == 1 || transposed_ == -1 ? "" : "S");
        if (transposed_ == 0) std::snprintf(text, sizeof text, "BACK WHERE IT WAS");
    } else if (done == Session::kOutOfRange) {
        std::snprintf(text, sizeof text, "NO ROOM: C0 TO C8 ONLY");
    } else {
        std::snprintf(text, sizeof text, "%s", project_.tracks[column_].kind == TrackKind::Drums ? "DRUMS DO NOT TRANSPOSE" : "NO NOTES TO MOVE");
    }
    session_.say(text);
}

// A cell's length: 1, 2, 4 or 8 bars. One step along that row each time.
void SongScreen::stepBars(int by) {
    static const int lengths[4] = {1, 2, 4, 8};
    int now = 0;
    for (int i = 0; i < 4; ++i)
        if (lengths[i] <= project_.tracks[column_].bars[scene_]) now = i;
    const int to = now + by;
    if (to < 0 || to > 3) return;
    session_.setCellBars(column_, scene_, lengths[to]);
}

void SongScreen::stepSceneTempo(int scene, int by) {
    const float from = project_.sceneTempo[scene] > 0.f ? project_.sceneTempo[scene] : project_.tempo;
    session_.setSceneTempo(scene, from + by);
    char text[48];
    std::snprintf(text, sizeof text, "SCENE TEMPO %d", static_cast<int>(project_.sceneTempo[scene]));
    session_.say(text);
}

void SongScreen::stepSceneMeter(int scene, int by) {
    session_.setSceneMeter(scene, project_.meterOf(scene) + by);
    char text[48];
    std::snprintf(text, sizeof text, "%d STEPS IN A BAR", project_.meterOf(scene));
    session_.say(text);
}

// A or L1 held on a repeat count: the stick does what the D-pad does there (gesture audit G14, 2026-11-24),
// one job a push. A: up / down the repeat count (squared, as it always was), left / right the scene's
// steps. L1: up / down the scene's own tempo (squared, a beat at a time, as fast as the drum grid's
// tempo row), left / right the steps in its bars. Sideways is a click at once, then slowly: they are counts.
void SongScreen::repeatStick(float x, float y, float dt) {
    if (x == 0.f && y == 0.f) repeatAxis_ = RepeatAxis::None;
    else if (repeatAxis_ == RepeatAxis::None) repeatAxis_ = std::fabs(y) >= std::fabs(x) ? RepeatAxis::UpDown : RepeatAxis::Sideways;
    const bool tempo = !holding_; // (L1 held, not A)
    if (repeatAxis_ == RepeatAxis::UpDown) {
        const float push = -y;
        saved_ += push * std::fabs(push) * (tempo ? kSceneTempoPerSecond : kRepeatsPerSecond) * dt;
        for (; saved_ >= 1.f; saved_ -= 1.f) {
            if (tempo) stepSceneTempo(scene_, 1);
            else setRepeats(project_.repeats[scene_] + 1), turned_ = true;
        }
        for (; saved_ <= -1.f; saved_ += 1.f) {
            if (tempo) stepSceneTempo(scene_, -1);
            else setRepeats(project_.repeats[scene_] - 1), turned_ = true;
        }
    } else {
        saved_ = 0.f;
    }
    if (repeatAxis_ != RepeatAxis::Sideways || x == 0.f) {
        repeatSideSaved_ = 0.f, repeatSideWas_ = false;
    } else {
        repeatSideSaved_ += repeatSideWas_ ? std::fabs(x) * kRepeatSidePerSecond * dt : 1.f;
        repeatSideWas_ = true;
        for (; repeatSideSaved_ >= 1.f; repeatSideSaved_ -= 1.f) {
            const int by = x > 0.f ? 1 : -1;
            if (tempo) stepSceneMeter(scene_, by);
            else session_.setSceneSteps(scene_, project_.sceneSteps(scene_) + by), turned_ = true;
        }
    }
    stickX_ = stickY_ = 0.f;
    stickWasX_ = stickWasY_ = false;
}

void SongScreen::setRepeats(int count) {
    count = count < 1 ? 1 : (count > kMaxRepeats ? kMaxRepeats : count);
    if (count == project_.repeats[scene_]) return;
    project_.repeats[scene_] = count;
    session_.noteEdit(); // a step of undo
    Command c;
    c.type = Command::SetSceneRepeats;
    c.scene = scene_;
    c.a = count;
    session_.engine.send(c);
}

void SongScreen::clearCell(int track, int scene) {
    // Already empty: nothing to do, and no step of undo that does nothing. (Read through a const track:
    // a scene with no pieces is not given any.)
    const TrackData& seen = project_.tracks[track];
    bool any = seen.bars[scene] != 1;
    if (!any && seen.holdsPieces(scene)) {
        for (int bar = 0; bar < kMaxBars && !any; ++bar) any = !seen.drums[scene][bar].empty() || !seen.notes[scene][bar].empty();
        for (const auto& lane : seen.automation[scene])
            for (float v : lane) any = any || v >= 0.f;
    }
    if (!any) return;
    // A step of undo of its own (review 2026-11-23: a cell that was not the one open was emptied with
    // none: the cell's mark only watches the open cell, and the image sent is no edit).
    session_.beginOwnUndoStep();
    TrackData& t = project_.tracks[track];
    // Every bar of it, and it is one bar long again. Its automation goes too: an emptied cell
    // must not keep running lanes (as Session::clearScene).
    t.bars[scene] = 1;
    if (track == project_.track && scene == project_.scene) project_.bar = 0;
    if (t.kind == TrackKind::Drums) {
        for (DrumPattern& bar : t.drums[scene]) bar = DrumPattern();
    } else if (isSynth(t.kind)) {
        for (NotePattern& bar : t.notes[scene]) bar = NotePattern();
    }
    session_.clearAutomation(track, scene, -1);
    session_.sendTrackImage(track); // patterns and automation, in one piece (as pasteBar)
    session_.endOwnUndoStep();
}

void SongScreen::copyCell(const CellCopy& from, int toTrack, int toScene) {
    TrackData& to = project_.tracks[toTrack];
    if (!pasteKindsFit(from.kind, to.kind)) return; // a drum bar cannot go on a synth
    to.bars[toScene] = from.bars; // the whole cell: its length and every bar
    if (toTrack == project_.track && toScene == project_.scene) project_.bar = 0;
    if (to.kind == TrackKind::Drums) {
        for (int bar = 0; bar < kMaxBars; ++bar) to.drums[toScene][bar] = from.drums[bar];
    } else if (isSynth(to.kind)) {
        for (int bar = 0; bar < kMaxBars; ++bar) to.notes[toScene][bar] = from.notes[bar];
    }
    // Its automation with it (as pasteBar's): the destination's stale lanes go first.
    session_.clearAutomation(toTrack, toScene, -1);
    for (int lane = 0; lane < kAutoLanes; ++lane)
        for (int step = 0; step < kAutoSteps; ++step)
            if (from.automation[lane][step] >= 0.f) to.automation[toScene][lane][step] = from.automation[lane][step];
    session_.sendTrackImage(toTrack); // patterns and automation, in one piece (as pasteBar)
}

void SongScreen::pasteCellHere(int toTrack, int toScene) {
    if (!cellCopy_.has) return; // armed, then the copy went (tracks came or went mid-hold)
    if (!pasteKindsFit(cellCopy_.kind, project_.tracks[toTrack].kind)) {
        session_.say("NO PASTE: OTHER KIND"); // the copy is kept: it pastes elsewhere
        return;
    }
    session_.beginOwnUndoStep();
    copyCell(cellCopy_, toTrack, toScene);
    cellCopied_ = false; // spent: a paste clears the copy
    cellCopy_.has = false;
    session_.endOwnUndoStep();
    session_.say("CELL PASTED");
}

void SongScreen::openRevList() {
    revsOpen_ = true;
    revs_ = session_.revs();
    revCursor_ = 0;
}

void SongScreen::armTrackHold() {
    trackHold_ = true;
    trackHoldTimed_ = false;
    trackHoldPart_ = 0.f;
    trackHoldColumn_ = column_;
    trackHoldScene_ = scene_;
}

void SongScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    // B kept held on a track: any other press calls its delete off, as on FX (and A's is then no tap: below).
    const bool deleteCalledOff = event.down && b != Button::B && trackHold_ && input.held(Button::B);
    if (event.down && b != Button::B) trackHold_ = false, trackHoldPart_ = 0.f;

    // SELECT, pressed and let go with nothing else in between, opens and
    // closes the list of songs. (SELECT with START is "quit", elsewhere.)
    if (b == Button::Select) {
        if (event.down) {
            selectAlone_ = true;
        } else if (selectAlone_ && !choosing_) {
            listing_ = !listing_;
            revsOpen_ = false;
            savedRev_ = 0;
            if (listing_) {
                session_.saveNow(); // so a song made a moment ago is in its own list
                songs_ = session_.songNames();
                listCursor_ = 0;
            }
        }
        return;
    }
    if (event.down) selectAlone_ = false;

    if (listing_) {
        if (!event.down) {
            if (b == repeating_) repeating_ = Button::None;
            // B let go before the second was up: only a tap. It closes the list.
            if (b == Button::B && bHeld_) {
                bHeld_ = false;
                listing_ = false;
            }
            return;
        }
        bHeld_ = false; // any other press calls a held B off
        if (b == Button::Start && !input.held(Button::Select)) { // play / stop with the list open, as everywhere (SEL + START quits)
            playFromCursor();
            return;
        }
        if (isDirection(b)) {
            moveCursor(b, true); // (a press: round the ends)
            repeating_ = b;
            repeatAt_ = 0;
        }
        if (b == Button::L1) {
            session_.toggleTransfer(lastMs_ * 2654435761u + 12345u); // files over WiFi, on or off
            return;
        }
        if (b == Button::Y) {
            // Anywhere in the list: keep the song as it is now. (Y, not X: X hears, everywhere.)
            savedRev_ = session_.saveRev();
            if (revsOpen_) openRevList();
            return;
        }
        if (revsOpen_) {
            if (b == Button::B) revsOpen_ = false; // back to the songs
            if (b == Button::A && revCursor_ == 0) {
                savedRev_ = session_.saveRev();
                openRevList();
            } else if (b == Button::A && revCursor_ == kNewNameRow) {
                // Another two words; again for another (the revs go with it). The songs behind say it too.
                const std::string renamed = session_.renameSong();
                songs_ = session_.songNames();
                for (std::size_t i = 0; i < songs_.size(); ++i)
                    if (songs_[i] == renamed) listCursor_ = static_cast<int>(i) + 1;
            } else if (b == Button::A) {
                if (session_.openRev(revs_[static_cast<std::size_t>(revCursor_ - kRevsFirst)])) {
                    listing_ = revsOpen_ = false;
                    scene_ = column_ = top_ = 0;
                    cellCopied_ = false; // another song: its cells are gone with the old one
                    cellCopy_.has = false;
                }
            }
            return;
        }
        if (b == Button::B) {
            // On a song: held for a second it deletes; let go sooner it closes.
            if (listCursor_ > 0) {
                bHeld_ = true;
                bDownAt_ = 0; // update() starts the clock, on its own time
                bRow_ = listCursor_;
            } else {
                listing_ = false;
            }
        }
        if (b == Button::A) {
            if (listCursor_ > 0 && songs_[static_cast<std::size_t>(listCursor_ - 1)] == session_.songName()) {
                openRevList(); // the song that is open: its revs
                return;
            }
            // Not made or not opened (the song open could not be saved: said; or the file would not
            // read): the list stays, and so does the song.
            const bool went = listCursor_ == 0 ? session_.newSong() : session_.openSong(songs_[static_cast<std::size_t>(listCursor_ - 1)]);
            if (!went) return;
            listing_ = false;
            scene_ = column_ = top_ = 0; // a different song: start at its top
            cellCopied_ = false;         // ...and its copied cell is gone with the old one
            cellCopy_.has = false;
        }
        return;
    }

    if (choosing_) {
        if (!event.down) return;
        if (b == Button::Start && !input.held(Button::Select)) playFromCursor(); // as everywhere (SEL + START quits)
        if (isDirection(b)) moveCursor(b);
        if (b == Button::B) choosing_ = false;
        if (b == Button::A) {
            choosing_ = false;
            if (changing_ >= 0) {
                session_.setTrackKind(changing_, kInstruments[choice_].kind); // another instrument for a track that is there
            } else if (session_.addTrack(kInstruments[choice_].kind)) {
                moveCursorTo(scene_, project_.trackCount() - 1); // onto the new track
            }
        }
        return;
    }

    // The mixer: A held on a head (a track's, or MASTER's). The directions turn its level (up, down) and
    // its pan (left, right); B puts both back to the usual. A let go having turned nothing is a tap:
    // on a track's head that opens the list of instruments, to change this track's.
    if ((onHead_ || onMaster_) && b == Button::A) {
        if (event.down) {
            mixing_ = true;
            mixTurned_ = deleteCalledOff; // A pressed while B was deleting the track calls the delete off (above), and is no tap
            mixSavedDb_ = mixSavedPan_ = 0.f;
            mixShown_ = mixTimed_ = false;
        } else {
            if (mixing_ && !mixTurned_ && !mixShown_ && onHead_ && !input.held(Button::R1)) { // a tap: let go before the card came up (R1 held: naming or duplicating is on its way, not this)
                choosing_ = true;
                changing_ = column_;
                for (int i = 0; i < kInstrumentCount; ++i)
                    if (kInstruments[i].kind == project_.tracks[column_].kind) choice_ = i;
            }
            mixing_ = false;
        }
        return;
    }
    if (mixing_ && isDirection(b)) {
        if (event.down) {
            mixStep(b);
            repeating_ = b;
            repeatAt_ = 0;
        } else if (b == repeating_) {
            repeating_ = Button::None;
        }
        return;
    }
    if (mixing_ && b == Button::B) {
        if (event.down) session_.setMix(mixTrack(), 0, 0), mixTurned_ = true; // back to the usual: 0 DB, the middle
        return;
    }
    // Any other button with A held on a head: letting go of A is then no tap (no instrument list),
    // and only START does its job (Y and X would mute or solo the track under the card).
    if (mixing_ && event.down) mixTurned_ = true;
    if (mixing_ && b != Button::Start) return;
    if (onMaster_ && (b == Button::B || b == Button::R1)) return; // nothing to empty, delete or change there
    // DUPLICATE A TRACK: L1 held on its head, R1 pressed and let go.
    if (b == Button::L1 && !mixing_) {
        l1OnHead_ = event.down && onHead_;
        l1OnRepeat_ = event.down && column_ == kRepeatColumn && !onSongEnd();
        if (l1OnRepeat_) saved_ = 0.f, repeatAxis_ = RepeatAxis::None; // a new hold: a new choice of job for the stick
        if (!event.down) dupArmed_ = false;
        return;
    }
    if (onHead_ && b == Button::R1 && l1OnHead_ && input.held(Button::L1)) {
        if (event.down) {
            dupArmed_ = true;
        } else if (dupArmed_) {
            dupArmed_ = false;
            const int made = session_.duplicateTrack(column_);
            char text[48];
            if (made >= 0) {
                std::snprintf(text, sizeof text, "TRACK %d: COPY OF %d", made + 1, made);
                moveCursorTo(scene_, made);
                onHead_ = true;
            } else {
                std::snprintf(text, sizeof text, "NO ROOM: %d TRACKS", kNumTracks);
            }
            session_.say(text);
        }
        return;
    }
    if (onHead_ && (b == Button::B || b == Button::R1)) {
        // On a track's head: B held deletes the track, as it does on a cell; a tap of it empties nothing here.
        if (b == Button::B && event.down && !input.held(Button::A)) armTrackHold();
        // R1: its name. It happens when R1 is LET GO (as DUPLICATE A TRACK's: on the way to a
        // panic nothing is named), if the cursor never left the heads meanwhile. The keyboard is on
        // the SET tab; it comes back here.
        if (b == Button::R1 && !mixing_) {
            if (event.down) nameArmed_ = true;
            else if (nameArmed_) {
                nameArmed_ = false;
                project_.track = column_;
                session_.wantTrackName = true;
                session_.view = View::Settings;
            }
        }
        return;
    }
    if (b == Button::A) {
        if (event.down) {
            if (onCell()) {
                // Held, the stick sets how many bars long it is. Let go
                // without doing that, and it opens. (Pressed while B was deleting the track, it calls the delete off and opens nothing.)
                holding_ = true;
                turned_ = deleteCalledOff;
                saved_ = 0.f;
                savedY_ = 0.f, stickWasYHeld_ = false;
                transposed_ = 0;
            } else if (column_ == addColumn()) {
                choosing_ = true;
                changing_ = -1;
                choice_ = 1; // Phazed: the likelier second track
            } else if (onSongEnd()) {
                session_.setSongEnd(project_.songEnd == kSongStop ? kSongLoop : kSongStop); // at once; nothing held
            } else if (column_ == kRepeatColumn) {
                holding_ = true;
                turned_ = false;
                saved_ = 0.f;
                repeatAxis_ = RepeatAxis::None; // a new hold: a new choice of job for the stick
            }
        } else {
            if (holding_ && !turned_ && onCell()) {
                project_.track = column_;
                project_.scene = scene_;
                project_.bar = 0;
                session_.view = View::Pattern;
            } else if (holding_ && !turned_) {
                // A plain tap on a repeat count: 1, 2, 4, 8, and round again.
                const int now = project_.repeats[scene_];
                setRepeats(now < 2 ? 2 : now < 4 ? 4 : now < 8 ? 8 : 1);
            }
            holding_ = false;
        }
        return;
    }
    // On a track: Y mutes it and X solos it, each a tap, at once, playing or not (owner, 2026-11-15: solo was
    // L1 held, and "Y for mute is just a one tap"). X does not hear the track here: its DEVICE page does.
    // L1 does nothing on a track.
    if (onCell() && event.down && !holding_ && (b == Button::X || b == Button::Y)) {
        if (b == Button::Y) session_.setMuted(column_, !project_.tracks[column_].muted);
        if (b == Button::X) session_.setSolo(column_, !project_.tracks[column_].solo);
        return;
    }
    if (b == Button::R1) {
        if (event.down && onCell() && cellCopied_ && copyTrack_ == column_ && copyScene_ == scene_) {
            // The copied cell again: the copy is cleared.
            cellCopied_ = false;
            cellCopy_.has = false;
            session_.say("COPY CLEARED");
        } else if (event.down && onCell() && !cellCopied_) {
            // COPY A CELL, as a scene is copied: nothing copied, and it copies now. A snapshot:
            // edits after COPY never leak into the paste (as a scene copy's).
            const TrackData& from = project_.tracks[column_];
            copyTrack_ = column_;
            copyScene_ = scene_;
            cellCopy_.has = true;
            cellCopy_.kind = from.kind;
            cellCopy_.bars = from.bars[scene_];
            for (int bar = 0; bar < kMaxBars; ++bar) {
                cellCopy_.drums[bar] = from.drums[scene_][bar];
                cellCopy_.notes[bar] = from.notes[scene_][bar];
            }
            for (int lane = 0; lane < kAutoLanes; ++lane)
                for (int step = 0; step < kAutoSteps; ++step) cellCopy_.automation[lane][step] = kAutoNone;
            if (from.automation.has(scene_))
                for (int lane = 0; lane < kAutoLanes; ++lane)
                    for (int step = 0; step < kAutoSteps; ++step) cellCopy_.automation[lane][step] = from.automation[scene_][lane][step];
            cellCopied_ = true;
            session_.say("CELL COPIED");
        } else if (event.down && onCell()) {
            // ...else the paste waits for R1's let go (as the scene's: on the way to a panic nothing pastes).
            cellPasteArmed_ = true;
            cellPasteTrack_ = column_;
            cellPasteScene_ = scene_;
        } else if (event.down && column_ == kRepeatColumn) {
            // COPY A SCENE: nothing copied, or this one already, and it copies now (the AT END row
            // too: it is an empty scene like any). Else the paste waits for R1's let go (as DUPLICATE
            // A TRACK's: on the way to a panic nothing pastes).
            if (session_.copiedScene() < 0 || session_.copiedScene() == scene_) session_.copySceneHere(scene_);
            else {
                scenePasteArmed_ = true;
                scenePaste_ = scene_;
            }
        } else if (!event.down && cellPasteArmed_) {
            cellPasteArmed_ = false;
            pasteCellHere(cellPasteTrack_, cellPasteScene_);
        } else if (!event.down && scenePasteArmed_) {
            scenePasteArmed_ = false;
            session_.pasteSceneHere(scenePaste_);
        }
        return;
    }
    if (b == Button::B && !input.held(Button::A) && column_ == kRepeatColumn && !onSongEnd()) {
        // On a repeat count: the press arms the hold (update() empties the scene at the second),
        // and letting go first is a tap back to 1. Above the early return for a let go, as R1's paste.
        // With L1 held it puts the scene's own tempo back to the song's instead.
        if (event.down && l1OnRepeat_) {
            session_.setSceneTempo(scene_, 0.f);
            session_.setSceneMeter(scene_, kSteps);
            session_.say("TEMPO FOLLOWS THE SONG, 16 STEPS");
            return;
        }
        if (event.down) {
            sceneHold_ = true;
            sceneHoldTimed_ = false;
            sceneHoldScene_ = scene_;
        } else if (sceneHold_ && scene_ == sceneHoldScene_) {
            setRepeats(1);
        }
        return;
    }
    // B let go on a cell before its bar began to fill: a tap, and the cell is emptied. Let go later, or after
    // a move or another press called the delete off: nothing, so a delete given up changes nothing.
    if (b == Button::B && !event.down && trackHold_) {
        trackHold_ = false;
        if (trackHoldPart_ <= 0.f && atTrackHold()) clearCell(column_, scene_);
        trackHoldPart_ = 0.f;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (holding_ && onCell()) {
            // A held on a cell: left and right make it shorter or longer.
            if (b == Button::Right) stepBars(1), turned_ = true;
            if (b == Button::Left) stepBars(-1), turned_ = true;
            // ...and up and down move its notes a semitone (TRANSPOSE).
            if (b == Button::Up) transpose(1);
            if (b == Button::Down) transpose(-1);
        } else if (holding_) {
            // A held on a repeat count: up and down change it by one.
            if (b == Button::Up) setRepeats(project_.repeats[scene_] + 1), turned_ = true;
            if (b == Button::Down) setRepeats(project_.repeats[scene_] - 1), turned_ = true;
            // ...and left and right, how many steps the scene's bars have: 16, or fewer.
            if (b == Button::Right) session_.setSceneSteps(scene_, project_.sceneSteps(scene_) + 1), turned_ = true;
            if (b == Button::Left) session_.setSceneSteps(scene_, project_.sceneSteps(scene_) - 1), turned_ = true;
        } else if (l1OnRepeat_ && column_ == kRepeatColumn && !onSongEnd()) {
            // L1 held on a repeat count: up and down are the scene's own tempo (from the song's,
            // first), left and right the steps in its bars (a bar of 12 is 3/4).
            if (b == Button::Up) stepSceneTempo(scene_, 1);
            else if (b == Button::Down) stepSceneTempo(scene_, -1);
            else if (b == Button::Right) stepSceneMeter(scene_, 1);
            else if (b == Button::Left) stepSceneMeter(scene_, -1);
        } else {
            moveCursor(b, true);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        // On a cell the press arms the hold: kept held, it deletes the whole track (update()); let go
        // before the bar begins, it is a tap and empties the cell (above).
        if (onCell()) armTrackHold();
        else if (onSongEnd()) session_.setSongEnd(kSongLoop); // back to the usual
        // (A repeat count returns earlier, above: a tap back to 1, a hold the whole scene.)
    } else if (b == Button::Start && !input.held(Button::Select)) {
        playFromCursor();
    }
}

void SongScreen::playFromCursor() {
    project_.scene = scene_;
    session_.togglePlay(true); // the song, from this scene
}

void SongScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;
    if (!input.held(Button::A) || (!onHead_ && !onMaster_) || listing_ || choosing_) mixing_ = false;
    if (mixing_ && !mixTimed_) mixTimed_ = true, mixSince_ = nowMs; // (the hold's clock starts on the frame after A)
    if (mixing_ && mixTimed_ && nowMs - mixSince_ >= kMixCardMs) mixShown_ = true;
    if (!input.held(Button::R1)) cellPasteArmed_ = false; // (one it never saw pastes nothing either)
    if (!input.held(Button::L1) || !onHead_) l1OnHead_ = false;
    if (!input.held(Button::L1) || column_ != kRepeatColumn || onSongEnd()) l1OnRepeat_ = false;
    if (!l1OnHead_ || !input.held(Button::R1)) dupArmed_ = false; // (a let go this screen never saw copies nothing)
    if (!input.held(Button::R1)) scenePasteArmed_ = false; // (one it never saw pastes nothing either)
    if (!input.held(Button::R1) || !onHead_) nameArmed_ = false; // (one it never saw names nothing; nor does one that left the heads)
    if (!onCell()) onHead_ = false; // the track under it went
    if (onMaster_ && (listing_ || choosing_)) onMaster_ = false; // (a song opened: start at its top)
    if (!input.held(Button::B)) bHeld_ = false;
    if (tracksSeen_ < 0) tracksSeen_ = project_.trackCount();
    else if (tracksSeen_ != project_.trackCount()) { // tracks came or went: the copied cell moved, so the copy goes
        cellCopied_ = false;
        cellCopy_.has = false;
        tracksSeen_ = project_.trackCount();
    }
    // B kept held on a track: after a moment a bar begins to fill on its head, and when it is full the track is deleted.
    // It is the track where B went down: the cursor leaving that place (the D-pad, the stick) calls it off, as A
    // or any other press does (onButton), as on the FX screen.
    if (!input.held(Button::B) || input.held(Button::A) || !onCell() || !atTrackHold() || listing_ || choosing_) trackHold_ = false, trackHoldPart_ = 0.f;
    if (trackHold_) {
        if (!trackHoldTimed_) trackHoldTimed_ = true, trackHoldSince_ = nowMs;
        const Uint32 held = nowMs - trackHoldSince_;
        trackHoldPart_ = held < kTrackHoldWaitMs ? 0.f : static_cast<float>(held - kTrackHoldWaitMs) / static_cast<float>(kTrackHoldMs - kTrackHoldWaitMs);
        if (held >= kTrackHoldMs) {
            trackHold_ = false;
            trackHoldPart_ = 0.f;
            if (session_.removeTrack(column_)) moveCursorTo(scene_, column_ >= project_.trackCount() ? project_.trackCount() - 1 : column_);
        }
    }
    // B kept held on a repeat count: the box fills red, and at the second the whole scene is emptied.
    if (!input.held(Button::B) || input.held(Button::A) || column_ != kRepeatColumn || scene_ != sceneHoldScene_ || listing_ || choosing_) sceneHold_ = false, sceneHoldPart_ = 0.f;
    if (sceneHold_) {
        if (!sceneHoldTimed_) sceneHoldTimed_ = true, sceneHoldSince_ = nowMs;
        sceneHoldPart_ = static_cast<float>(nowMs - sceneHoldSince_) / static_cast<float>(kSceneHoldMs);
        if (nowMs - sceneHoldSince_ >= kSceneHoldMs) {
            sceneHold_ = false;
            sceneHoldPart_ = 0.f;
            session_.clearSceneHere(scene_);
        }
    }

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();

    // B held on a song for a second: it is deleted.
    if (!listing_ || bRow_ != listCursor_ || revsOpen_) bHeld_ = false; // the cursor moved off it
    if (bHeld_ && bDownAt_ == 0) bDownAt_ = nowMs;
    if (bHeld_ && nowMs - bDownAt_ >= kDeleteHoldMs) {
        bHeld_ = false;
        const bool wasOpen = songs_[static_cast<std::size_t>(listCursor_ - 1)] == session_.songName();
        session_.deleteSong(songs_[static_cast<std::size_t>(listCursor_ - 1)]);
        songs_ = session_.songNames();
        if (listCursor_ > static_cast<int>(songs_.size())) listCursor_ = static_cast<int>(songs_.size());
        if (wasOpen) {
            scene_ = column_ = top_ = 0;
            cellCopied_ = false; // the open song went with it
            cellCopy_.has = false;
        }
    }

    if (mixing_) {
        // A held on a head: the stick turns its level (up, down) and pan (left, right), for big moves.
        const auto turn = [&](float push, float perSecond, float& saved, Button up, Button down) {
            saved += push * std::fabs(push) * perSecond * dt;
            for (; saved >= 1.f; saved -= 1.f) mixStep(up);
            for (; saved <= -1.f; saved += 1.f) mixStep(down);
        };
        turn(-y, kMixDbPerSecond, mixSavedDb_, Button::Up, Button::Down);
        turn(x, kMixPanPerSecond, mixSavedPan_, Button::Right, Button::Left);
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else if (holding_ && onCell()) {
        // The stick sets the cell's length: one move at once, then slowly (there are only four).
        if (x == 0.f) saved_ = 0.f, stickWasX_ = false;
        else {
            saved_ += stickWasX_ ? std::fabs(x) * 3.f * dt : 1.f;
            stickWasX_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) stepBars(x > 0.f ? 1 : -1), turned_ = true;
        }
        // ...and up / down an octave (TRANSPOSE): one at once, then slowly.
        if (y == 0.f) savedY_ = 0.f, stickWasYHeld_ = false;
        else {
            savedY_ += stickWasYHeld_ ? std::fabs(y) * 2.f * dt : 1.f;
            stickWasYHeld_ = true;
            for (; savedY_ >= 1.f; savedY_ -= 1.f) transpose(y < 0.f ? 12 : -12);
        }
        stickX_ = stickY_ = 0.f;
        stickWasY_ = false;
    } else if (holding_ || (l1OnRepeat_ && column_ == kRepeatColumn && !onSongEnd())) {
        // A held on a repeat count: the stick turns the count, and sideways the scene's steps. L1 held
        // there: its own tempo, and sideways its meter. (L1's stick moved the cursor off the count.)
        repeatStick(x, y, dt);
    } else {
        // The cells are a cursor: a full push held climbs (G07). The lists (songs, instruments) do not.
        const bool cells = !listing_ && !choosing_;
        const float climbX = climbX_.times(cells ? x : 0.f, nowMs), climbY = climbY_.times(cells ? y : 0.f, nowMs);
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickCellsPerSecond * climbX * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickCellsPerSecond * climbY * dt : 1.f;
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (mixing_) {
            mixStep(repeating_);
        } else if (holding_ && onCell()) {
            // (A held on a cell: a D-pad held does not run on. TRANSPOSE is a semitone a press, the stick an octave.)
        } else if (holding_) {
            if (repeating_ == Button::Up) setRepeats(project_.repeats[scene_] + 1);
            if (repeating_ == Button::Down) setRepeats(project_.repeats[scene_] - 1);
        } else if (l1OnRepeat_ && column_ == kRepeatColumn && !onSongEnd()) {
            if (repeating_ == Button::Up) stepSceneTempo(scene_, 1);
            else if (repeating_ == Button::Down) stepSceneTempo(scene_, -1);
            else if (repeating_ == Button::Right) stepSceneMeter(scene_, 1);
            else if (repeating_ == Button::Left) stepSceneMeter(scene_, -1);
        } else {
            moveCursor(repeating_);
        }
    }
}

SDL_Rect SongScreen::repeatBox(int scene) const {
    if (scene < top_ || scene >= top_ + kVisibleRows) return SDL_Rect{0, 0, 0, 0};
    return SDL_Rect{8 + kNumberW, kRowsTop + (scene - top_) * kRowH, kRepeatW - 4, kRowH - 4};
}

SDL_Rect SongScreen::cellBox(int track, int scene) const {
    if (scene < top_ || scene >= top_ + kVisibleRows) return SDL_Rect{0, 0, 0, 0};
    const int colW = columnWidth(project_.trackCount());
    return SDL_Rect{kColumnsLeft + track * (colW + 1), kRowsTop + (scene - top_) * kRowH, colW, kRowH - 4};
}

// LOOP: a ring that goes round to an arrowhead (the song goes round to its top). STOP: a square,
// the stop key's own mark. Plain filled shapes, in the one colour: they read at every theme.
void SongScreen::drawSongEnd(SDL_Renderer* r, int cx, int cy, int end, SDL_Color color) {
    if (end == kSongStop) {
        fill(r, cx - 6, cy - 6, 12, 12, color);
        return;
    }
    outline(r, cx - 9, cy - 5, 18, 12, color, 2);
    fill(r, cx + 1, cy - 5, 6, 2, kBg); // a gap in the ring's top, where it comes round...
    for (int i = 0; i < 5; ++i) fill(r, cx + 1 + i, cy - 8 + i, 1, 9 - 2 * i, color); // ...to an arrowhead pointing on
}

// How wide a head's M or S mark is: its letter, at the lettering the heads were laid out at, 2 pixels clear each side.
int SongScreen::headMarkWidth(const Fonts& fonts, const char* letter) { return static_cast<int>(std::ceil(fonts.fixed().small.width(letter))) + 4; }

void SongScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[96];
    const int tracks = project_.trackCount();
    const int songScenes = project_.sceneCount();
    char title[40];
    std::snprintf(title, sizeof title, "%s", session_.songName().c_str());
    for (char* c = title; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    std::snprintf(text, sizeof text, "%s", title); // the song's name; the tracks can be counted by looking
    topBar(r, fonts, View::Song, text, project_);

    const int colW = columnWidth(tracks);
    const bool held = holding_ || mixCardUp();
    const SDL_Color cursorColor = held ? kBrass : kTeal;
    const int playScene = project_.playScene, playStep = project_.playStep;

    // ---- column heads ----------------------------------------------------------
    // (REPEATS, it was PLAYS: owner, 2026-11-18. Low in the heads' row, nearer the boxes it names; held at LARGE where it would leave the screen.)
    fonts.within(&Fonts::small, kRepeatsWord, kColumnsLeft - 5.f).drawRight(r, kColumnsLeft - 2.f, static_cast<float>(kRepeatsWordY), kRepeatsWord, kDim);
    for (int t = 0; t < tracks; ++t) {
        const int x = kColumnsLeft + t * (colW + 1);
        box(r, x, kGridTop, colW, kHeadH, kPanel2, kLine);
        const bool sounding = session_.heardNow(t); // (QUEUED MUTES: as it still sounds until the bar line)
        // B held on this track: its head fills red and says what is coming, and
        // nothing else: the number and the name come back if B is let go. The
        // words flash slowly, three times in the two seconds.
        const bool deleting = trackHoldPart_ > 0.f && t == column_;
        // The mixer: while A is held on this head, its box fills with the level, as a lane's name
        // does on the drum grid (the card's scale: from the bottom of the dB range).
        if (mixing_ && !onMaster_ && t == column_ && !deleting) {
            const int db = session_.mixDb(t);
            fill(r, x + 1, kGridTop + 1, static_cast<int>(colW - 2) * (db - kMixLeastDb) / (kMixMostDb - kMixLeastDb), kHeadH - 2, faded(kBrass, 110));
        }
        if (deleting) {
            const float part = trackHoldPart_ > 1.f ? 1.f : trackHoldPart_;
            fill(r, x + 1, kGridTop + 1, static_cast<int>((colW - 2) * part), kHeadH - 2, faded(kStop, 170));
            // One word, so it never leaves its column (owner, 2026-11-13); DEL where even that is too wide.
            // (Which one is asked at LARGE, so the word is the same at every size: one too wide at XL is held at LARGE.)
            const char* words = fonts.fixed().small.width("DELETING") <= colW - 8.f ? "DELETING" : "DEL";
            if (std::fmod(part * 3.f, 1.f) < 0.65f) fonts.within(&Fonts::small, words, colW - 8.f).drawCentered(r, x + colW / 2.f, kGridTop + 13.f, words, kText);
        } else {
            std::snprintf(text, sizeof text, "%d", t + 1);
            // (In the narrow columns at LARGE, as the name under it: beside the M or S mark there is no room to grow.)
            // Where the mark would stand on the number (11 and 12 in the narrowest heads), the number is centred
            // in the room left of it instead, clear of the mixer's mark at the other corner.
            const Font& numberFont = (colW > 60 ? fonts : fonts.fixed()).small;
            const char waiting = session_.muteWaits(t); // (a mark that waits for the bar line stands there too)
            const char* letter = project_.tracks[t].solo || waiting == 'S' ? "S" : (project_.tracks[t].muted || waiting == 'M' ? "M" : nullptr);
            float numberX = x + colW / 2.f;
            if (letter) {
                const float markLeft = x + colW - 2 - headMarkWidth(fonts, letter);
                if (numberX + numberFont.width(text) / 2.f + 1.f > markLeft) {
                    const bool mixed = project_.tracks[t].mixDb != 0 || project_.tracks[t].mixPan != 0;
                    const float from = mixed ? x + kMixMarkInset + kMixMarkSize + 1.f : x + 1.f;
                    numberX = (from + markLeft - 1.f) / 2.f;
                }
            }
            numberFont.drawCentered(r, numberX, kGridTop + 5.f, text, kDim);
            const InstrumentInfo* info = instrument(project_.tracks[t].kind);
            const char* name = !info ? "" : colW > 60 ? info->name : info->shortName;
            // The player's own name for it, cut to fit a narrow column.
            char own[12];
            std::snprintf(own, sizeof own, "%s", project_.tracks[t].name);
            // (At XL a name that does not fit whole is held at LARGE, and cut as it is cut at LARGE: never shorter.)
            // In the narrow columns (the short names: nine tracks and up) the heads are side by side with no room to spare: always at LARGE.
            const Font& headFont = fonts.within(&Fonts::small, own[0] ? own : name, colW > 60 ? colW - 6.f : 0.f);
            for (std::size_t n = std::strlen(own); n > 0 && headFont.width(own) > colW - 6; --n) own[n - 1] = 0;
            // An instrument's short name is cut only where it would run past its head's edge into the next
            // (DRUMS in the narrowest columns, since MASTER took one: DRUM).
            char shown[12];
            std::snprintf(shown, sizeof shown, "%s", name);
            for (std::size_t n = std::strlen(shown); n > 0 && colW <= 60 && headFont.width(shown) > colW - 1; --n) shown[n - 1] = 0;
            headFont.drawCentered(r, x + colW / 2.f, kGridTop + 22.f, own[0] ? own : shown, sounding ? kText : kDim);
        }
        if (onHead_ && t == column_ && !listing_ && !choosing_) outline(r, x - 1, kGridTop - 1, colW + 2, kHeadH + 2, cursorColor, 2);
        // Muted, or soloed: a mark in the corner of the head, and a silent track's head goes dark.
        if (!sounding && !deleting) fill(r, x + 1, kGridTop + 1, colW - 2, kHeadH - 2, faded(kBg, 150));
        // LIGHT COLOUR at TRACK: the track's ring colour along the head's foot (drawn after the dark wash: a muted track's is as bright).
        if (!deleting && session_.trackColoursShown()) {
            const Rgb hue = ringTrackColour(t);
            const int my = kGridTop + kHeadH - kHueMarkFoot - kHueMarkH;
            fill(r, x + kHueMarkInset, my - 1, colW - 2 * kHueMarkInset, 1, kDim);
            fill(r, x + kHueMarkInset, my, colW - 2 * kHueMarkInset, kHueMarkH, SDL_Color{hue.r, hue.g, hue.b, 255});
        }
        // Not at its usual level or pan (the mixer): a brass mark in the other corner.
        if (!deleting && (project_.tracks[t].mixDb != 0 || project_.tracks[t].mixPan != 0)) fill(r, x + kMixMarkInset, kGridTop + kMixMarkInset, kMixMarkSize, kMixMarkSize, kBrass);
        // The mark's box is made to its letter (2 pixels clear all round), in the lettering every head was
        // laid out at: at a fixed 11 by 11 the letter stood out of it, below and at the sides (owner, 2026-11-14).
        // QUEUED MUTES: a mark that waits for the bar line is hollow (its box an outline, its letter in the
        // colour), whether it is going on or coming off.
        const auto mark = [&](const char* letter, SDL_Color colour, bool waits) {
            const Font& font = fonts.fixed().small;
            const int w = headMarkWidth(fonts, letter), h = static_cast<int>(std::ceil(font.capHeight())) + 4;
            const int left = x + colW - 2 - w;
            fill(r, left, kGridTop + 2, w, h, waits ? kBg : colour);
            if (waits) outline(r, left, kGridTop + 2, w, h, colour);
            font.drawCentered(r, left + w / 2.f, kGridTop + 4.f, letter, waits ? colour : kBg);
        };
        const char waits = session_.muteWaits(t);
        if (project_.tracks[t].solo || waits == 'S') {
            mark("S", kTeal, waits == 'S');
            if (project_.tracks[t].solo) outline(r, x, kGridTop, colW, kHeadH, kTeal);
        } else if (project_.tracks[t].muted || waits == 'M') {
            mark("M", kBrass, waits == 'M');
        }
    }
    if (addColumn() >= 0) {
        const int x = kColumnsLeft + tracks * (colW + 1);
        outline(r, x, kGridTop, colW, kHeadH, kLine);
        fonts.within(&Fonts::small, colW > 60 ? "+ TRACK" : "+", colW - 4.f).drawCentered(r, x + colW / 2.f, kGridTop + 14.f, colW > 60 ? "+ TRACK" : "+", kDim);
        if (column_ == addColumn() && !onMaster_) outline(r, x - 1, kGridTop - 1, colW + 2, kHeadH + 2, cursorColor, 2);
    }
    {
        // MASTER, at the right-hand end: everything, after the tracks are mixed. A head and no cells.
        const int x = masterX();
        box(r, x, kGridTop, colW, kHeadH, kPanel2, kLine);
        if (mixing_ && onMaster_) {
            const int db = session_.mixDb(kMasterTrack);
            fill(r, x + 1, kGridTop + 1, static_cast<int>(colW - 2) * (db - kMixLeastDb) / (kMixMostDb - kMixLeastDb), kHeadH - 2, faded(kBrass, 110));
        }
        const char* name = fonts.fixed().small.width("MASTER") <= colW - 6.f ? "MASTER" : "MSTR"; // (as a head's name: the narrow columns at LARGE)
        fonts.within(&Fonts::small, name, colW > 60 ? colW - 6.f : 0.f).drawCentered(r, x + colW / 2.f, kGridTop + 14.f, name, kText);
        if (project_.masterDb != 0 || project_.masterPan != 0) fill(r, x + kMixMarkInset, kGridTop + kMixMarkInset, kMixMarkSize, kMixMarkSize, kBrass);
        if (onMaster_ && !listing_ && !choosing_) outline(r, x - 1, kGridTop - 1, colW + 2, kHeadH + 2, cursorColor, 2);
    }

    // ---- rows ---------------------------------------------------------------------
    for (int row = 0; row < kVisibleRows; ++row) {
        const int scene = top_ + row;
        const int y = kRowsTop + row * kRowH;
        const bool inSong = scene < songScenes;
        const bool playingHere = scene == playScene;

        std::snprintf(text, sizeof text, "%d", scene + 1);
        fonts.small.drawRight(r, 8.f + kNumberW - 6.f, y + 15.f, text, playingHere ? kPurple : (inSong ? kText : kDim));

        // How many times the scene plays, and how far through it is.
        const int rx = 8 + kNumberW;
        box(r, rx, y, kRepeatW - 4, kRowH - 4, kBg, inSong ? kLine : kPanel);
        const int repeats = project_.repeats[scene];
        std::snprintf(text, sizeof text, "x%d", repeats);
        const bool odd = project_.sceneSteps(scene) != project_.sceneBars(scene) * project_.meterOf(scene); // a scene cut short says how many steps it lasts, under its count
        const bool end = scene == songEndRow(); // where the song ends: what it does there, not a count
        if (end) drawSongEnd(r, rx + (kRepeatW - 4) / 2, y + (kRowH - 4) / 2, project_.songEnd, project_.songEnd == kSongStop ? kBrass : kDim);
        else fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + (playingHere || odd ? 6.f : 15.f), text, inSong ? kText : kDim);
        if (odd && !playingHere && !end) {
            std::snprintf(text, sizeof text, "%d", project_.sceneSteps(scene));
            fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + 22.f, text, kBrass);
        }
        if (playingHere) {
            // One block per pass, filling as the bar goes by.
                sceneProgress(r, rx + 4, y + kRowH - 14, kRepeatW - 12, 6, project_.playLoop ? 1 : repeats, project_.playRepeat,
                          project_.playBar * project_.meterOf(scene) + playStep, project_.sceneSteps(scene));
        }
        if (column_ == kRepeatColumn && scene == scene_ && !onMaster_) {
            if (holding_) fill(r, rx + 1, y + 1, (kRepeatW - 6) * repeats / kMaxRepeats, kRowH - 6, faded(kBrass, 110));
            outline(r, rx - 1, y - 1, kRepeatW - 2, kRowH - 2, cursorColor, 2);
        }
        if (session_.copiedScene() == scene && !end) outline(r, rx + 1, y + 1, kRepeatW - 6, kRowH - 6, kBrass); // the copied scene
        if (sceneHold_ && scene == scene_ && sceneHoldPart_ > 0.f) { // emptying it: red, as a track being deleted
            const float part = sceneHoldPart_ > 1.f ? 1.f : sceneHoldPart_;
            fill(r, rx + 1, y + 1, static_cast<int>((kRepeatW - 6) * part), kRowH - 6, faded(kStop, 170));
        }

        for (int t = 0; t < tracks; ++t) {
            const TrackData& track = project_.tracks[t];
            const int x = kColumnsLeft + t * (colW + 1);
            const bool empty = track.sceneEmpty(scene);
            if (empty) outline(r, x, y, colW, kRowH - 4, inSong ? kLine : kPanel);
            else box(r, x, y, colW, kRowH - 4, kPanel, playingHere ? kPurple : kLine);

            // A small picture of the cell across the whole scene. A cell shorter than
            // the scene goes round, so it is drawn again for each pass, a little
            // dimmer, with a line where it starts over.
            const int cellBars = track.bars[scene] < 1 ? 1 : track.bars[scene], sceneBars = project_.sceneBars(scene);
            const float stepW = static_cast<float>(colW - 6) / (16.f * sceneBars);
            int low = 127, high = 0; // the notes' range, for the piano-roll kind
            if (track.kind != TrackKind::Drums) {
                for (int bar = 0; bar < cellBars; ++bar) {
                    NoteEvent notes[kMaxNotes]; // chords drawn as their notes
                    track.notes[scene][bar].render(notes);
                    for (const NoteEvent& n : notes)
                        if (n.velocity > 0.f) { low = n.pitch < low ? n.pitch : low; high = n.pitch > high ? n.pitch : high; }
                }
            }
            const int span = high - low < 7 ? 7 : high - low;
            for (int at = 0; at < sceneBars; ++at) {
                const int bar = at % cellBars;
                const bool again = at >= cellBars; // a later pass
                // This pass ends where the cell starts over, or where the scene ends.
                const int passEnd = static_cast<int>(16.f * stepW * std::min(sceneBars, (at / cellBars + 1) * cellBars));
                if (again && bar == 0) fill(r, x + 3 + static_cast<int>(at * kSteps * stepW), y + 3, 1, kRowH - 10, faded(kPurple, 150)); // fainter than the line that shows where it is playing
                if (empty) {
                    // No content, but the bars still read: a faint line where each bar of the
                    // first pass starts (past the first: the box's own edge says that one).
                    if (!again && bar > 0) fill(r, x + 3 + static_cast<int>(at * kSteps * stepW), y + 3, 1, kRowH - 10, kDim);
                    continue;
                }
                if (track.kind == TrackKind::Drums) {
                    const DrumPattern& p = track.drums[scene][bar];
                    for (int lane = 0; lane < kDrumLanes; ++lane)
                        for (int step = 0; step < kSteps; ++step)
                            if (p.level[lane][step] > 0.f) {
                                const int alpha = 90 + static_cast<int>(165 * p.level[lane][step]);
                                fill(r, x + 3 + static_cast<int>((at * kSteps + step) * stepW), y + 4 + lane * 4, 2, 2, faded(kText, again ? alpha * 6 / 10 : alpha));
                            }
                } else {
                    NoteEvent notes[kMaxNotes];
                    track.notes[scene][bar].render(notes);
                    for (const NoteEvent& n : notes) {
                        if (n.velocity <= 0.f) continue;
                        const int ny = y + 4 + (kRowH - 14) * (high - n.pitch) / span;
                        const int from = static_cast<int>((at * kSteps + n.step) * stepW);
                        int w = static_cast<int>(n.length * stepW) - 1;
                        if (from + w > passEnd) w = passEnd - from; // it runs to the end of the pass at most
                        fill(r, x + 3 + from, ny, w < 2 ? 2 : w, 2, again ? faded(kText, 150) : kText);
                    }
                }
            }
            // (No bar count in the corner: it hid the notes, and the bar lines say it. Owner, 2026-11-14.)
            // Where the cell has got to: a short one goes round under a long scene.
            if (playingHere && playStep >= 0) {
                const int at = project_.playBar * kSteps + playStep; // 16-coords, as the cell is drawn
                fill(r, x + 3 + static_cast<int>(at * stepW), y + 2, 2, kRowH - 8, kPurple);
            }
            if (cellCopied_ && t == copyTrack_ && scene == copyScene_) outline(r, x + 1, y + 1, colW - 2, kRowH - 6, kBrass); // the copied cell
            if (t == column_ && scene == scene_ && !onHead_ && !onMaster_) outline(r, x - 1, y - 1, colW + 2, kRowH - 2, cursorColor, 2);
        }
    }
    // Where the song ends and comes round.
    if (songScenes > top_ && songScenes <= top_ + kVisibleRows) {
        const int y = kRowsTop + (songScenes - top_) * kRowH - 3;
        fill(r, 8, y, kColumnsLeft + tracks * (colW + 1) - 10, 1, kPurple);
    }
    if (top_ > 0) fonts.small.draw(r, 10.f, kGridTop + 28.f, "^", kDim);

    // ---- the mixer card: A held on a head --------------------------------------------
    // Under the head, over the top scene: the level and the pan, in words and as two bars. In the
    // fixed lettering (it sits on the grid's rows, to the pixel), brass where it is not the usual.
    if (mixCardUp() && !listing_ && !choosing_) {
        const Place card = mixCard();
        const Fonts& tight = fonts.fixed();
        const int track = mixTrack();
        const int db = session_.mixDb(track), pan = session_.mixPan(track);
        box(r, card.x, card.y, kMixCardW, kMixCardH, kPanel, kBrass);
        const float left = card.x + 8.f, right = card.x + kMixCardW - 8.f;
        const int barX = card.x + 8, barW = kMixCardW - 16;
        mixLevelWords(db, text, sizeof text);
        tight.small.draw(r, left, card.y + 6.f, "LEVEL", kDim);
        tight.small.drawRight(r, right, card.y + 6.f, text, db != 0 ? kBrass : kText);
        // LEVEL: filled from the bottom of its range, with a mark where the usual is.
        const int usualAt = barW * (0 - kMixLeastDb) / (kMixMostDb - kMixLeastDb);
        fill(r, barX, card.y + 24, barW, 5, kLine);
        fill(r, barX, card.y + 24, barW * (db - kMixLeastDb) / (kMixMostDb - kMixLeastDb), 5, db != 0 ? kBrass : kDim);
        fill(r, barX + usualAt, card.y + 22, 1, 9, kText);
        mixPanWords(pan, text, sizeof text);
        tight.small.draw(r, left, card.y + 34.f, "PAN", kDim);
        tight.small.drawRight(r, right, card.y + 34.f, text, pan != 0 ? kBrass : kText);
        // PAN: a line with the middle marked, filled from the middle out to where it stands.
        const int middle = barX + barW / 2, at = middle + (barW / 2) * pan / kPanMost;
        fill(r, barX, card.y + 52, barW, 5, kLine);
        if (pan != 0) fill(r, at < middle ? at : middle, card.y + 52, at < middle ? middle - at : at - middle, 5, kBrass);
        fill(r, middle, card.y + 50, 1, 9, kText);
    }

    // ---- the list of songs --------------------------------------------------------
    if (listing_) {
        // (Our firmware names the page on the network, apotheca.local: it is said as well as the
        // address, on a line of its own, and the list shows a row fewer for it. The box is as it was.)
        const std::string pageName = session_.transferName();
        const int w = 360, rowH = 34, room = 9, shown = pageName.empty() ? room : room - 1, x = (kScreenW - w) / 2, y = 60;
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, 80 + room * rowH, kPanel, kLine);
        {
            // Files over WiFi: off, or the address to type into a browser.
            const int ty = y + 42 + shown * rowH;
            fill(r, x + 12, ty - 6, w - 24, 1, kLine);
            const bool on = session_.transferOn();
            fonts.small.draw(r, x + 12.f, static_cast<float>(ty), "L1", kDim);
            fonts.small.draw(r, x + 34.f, static_cast<float>(ty), on ? "FILES OVER WIFI: ON" : "FILES OVER WIFI: OFF", on ? kTeal : kText);
            if (on) {
                const std::string address = session_.transferAddress();
                // (These run the panel's width: one too wide at XL is held at LARGE.)
                const std::string open = address[0] == ':' ? (session_.wifiHere() ? "NOT ON A NETWORK. JOIN ONE: SET, NETWORK." : "NOT ON A NETWORK. JOIN WIFI IN THE FIRMWARE FIRST.")
                                                               : pageName.empty() ? "OPEN IN A BROWSER:  " + address : "OPEN  " + pageName;
                fonts.within(&Fonts::small, open.c_str(), w - 24.f).draw(r, x + 12.f, ty + 16.f, open.c_str(), kBrass);
                if (!pageName.empty() && address[0] != ':') {
                    const std::string also = "OR  " + address;
                    fonts.within(&Fonts::small, also.c_str(), w - 24.f).draw(r, x + 12.f, ty + 32.f, also.c_str(), kBrass);
                }
            } else {
                const char* from = "Recordings, chord packs, songs: from a browser.";
                fonts.within(&Fonts::small, from, w - 24.f).draw(r, x + 12.f, ty + 16.f, from, kDim);
            }
        }
        char saved[32] = "";
        if (savedRev_ > 0) std::snprintf(saved, sizeof saved, "REV %d SAVED", savedRev_);
        if (revsOpen_) {
            char upper[48];
            std::snprintf(upper, sizeof upper, "REVS OF %s", session_.songName().c_str());
            for (char* c = upper; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
            fonts.small.draw(r, x + 12.f, y + 12.f, upper, kDim);
            fonts.small.drawRight(r, x + w - 12.f, y + 12.f, saved, kBrass);
            const int rows = static_cast<int>(revs_.size()) + kRevsFirst;
            const int first = revCursor_ < shown ? 0 : revCursor_ - shown + 1;
            for (int i = first; i < rows && i < first + shown; ++i) {
                const int by = y + 32 + (i - first) * rowH;
                box(r, x + 12, by, w - 24, rowH - 4, kPanel2, kLine);
                if (i == 0) {
                    fonts.medium.draw(r, x + 22.f, by + 9.f, "+ SAVE A REV", i == revCursor_ ? kTeal : kText);
                    const char* what = "THE SONG AS IT IS NOW"; // (clear of "+ SAVE A REV" beside it: at XL it is held at LARGE)
                    fonts.within(&Fonts::small, what, w - 44.f - fonts.medium.width("+ SAVE A REV") - 12.f).drawRight(r, x + w - 22.f, by + 10.f, what, kDim);
                } else if (i == kNewNameRow) {
                    fonts.medium.draw(r, x + 22.f, by + 9.f, "+ NEW NAME", i == revCursor_ ? kTeal : kText);
                    const char* what = "TWO OTHER WORDS"; // (as THE SONG AS IT IS NOW beside SAVE A REV)
                    fonts.within(&Fonts::small, what, w - 44.f - fonts.medium.width("+ NEW NAME") - 12.f).drawRight(r, x + w - 22.f, by + 10.f, what, kDim);
                } else {
                    const int rev = revs_[static_cast<std::size_t>(i - kRevsFirst)];
                    std::snprintf(text, sizeof text, "REV %d", rev);
                    fonts.medium.draw(r, x + 22.f, by + 9.f, text, i == revCursor_ ? kTeal : kText);
                    fonts.small.drawRight(r, x + w - 22.f, by + 10.f, session_.revWhen(rev).c_str(), kDim);
                }
                if (i == revCursor_) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
            }
            const Hint list[] = {{"A", revCursor_ == 0 ? "save a rev" : revCursor_ == kNewNameRow ? "new name" : "go back to this rev"}, {"Y", "save a rev"}, {"B", "songs"}};
            hints(r, fonts, list, 3);
            return;
        }
        const int rows = static_cast<int>(songs_.size()) + 1;
        const int first = listCursor_ < shown ? 0 : listCursor_ - shown + 1;
        // (The heading and what is said beside it share a line: where they would meet at XL, what is said is held at LARGE.)
        const char* said = saved[0] ? saved : "KEPT AS YOU GO. NOTHING TO SAVE.";
        fonts.small.draw(r, x + 12.f, y + 12.f, "SONGS", kDim);
        fonts.within(&Fonts::small, said, w - 24.f - fonts.small.width("SONGS") - 8.f).drawRight(r, x + w - 12.f, y + 12.f, said, saved[0] ? kBrass : kDim);
        for (int i = first; i < rows && i < first + shown; ++i) {
            const int by = y + 32 + (i - first) * rowH;
            box(r, x + 12, by, w - 24, rowH - 4, kPanel2, kLine);
            if (i == 0) {
                fonts.medium.draw(r, x + 22.f, by + 9.f, "+ NEW SONG", i == listCursor_ ? kTeal : kText);
            } else {
                const std::string& name = songs_[static_cast<std::size_t>(i - 1)];
                char upper[40];
                std::snprintf(upper, sizeof upper, "%s", name.c_str());
                for (char* c = upper; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
                // The open song's name shares its row with OPEN NOW > REVS. Where the two would meet at XL
                // (MERCURY WINDOW: a name the program gives), both are held at LARGE (Fonts::within).
                const bool open = name == session_.songName();
                const char* const openSays = "OPEN NOW  >  REVS";
                const Font& nameFont = fonts.within(&Fonts::medium, upper, w - 44.f - (open ? fonts.small.width(openSays) + 8.f : 0.f));
                nameFont.draw(r, x + 22.f, by + 9.f, upper, i == listCursor_ ? kTeal : kText);
                const bool deleting = bHeld_ && bDownAt_ != 0 && i == bRow_;
                if (open && !deleting) (&nameFont == &fonts.medium ? fonts : fonts.fixed()).small.drawRight(r, x + w - 22.f, by + 10.f, openSays, kPurple);
                // B held on it: the row fills, and at full the song is deleted.
                if (bHeld_ && bDownAt_ != 0 && i == bRow_) {
                    const float part = static_cast<float>(lastMs_ - bDownAt_) / static_cast<float>(kDeleteHoldMs);
                    fill(r, x + 13, by + 1, static_cast<int>((w - 26) * (part > 1.f ? 1.f : part)), rowH - 6, faded(kBrass, 120));
                    // On a patch of its own, so it is never written over the song's name. It says it cannot be
                    // undone: nothing on the handheld brings it back (it goes to "deleted" on the card).
                    const Font& font = fonts.within(&Fonts::small, kSongDeletingWords, w - 44.f);
                    const float tw = font.width(kSongDeletingWords);
                    fill(r, static_cast<int>(x + w - 22.f - tw - 8.f), by + 3, static_cast<int>(tw + 14.f), rowH - 10, kPanel);
                    font.drawRight(r, x + w - 22.f, by + 10.f, kSongDeletingWords, kBrass);
                }
            }
            if (i == listCursor_) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
        }
        const bool onOpen = listCursor_ > 0 && songs_[static_cast<std::size_t>(listCursor_ - 1)] == session_.songName();
        const Hint list[] = {{"A", listCursor_ == 0 ? "new song" : onOpen ? "its revs" : "open"}, {"Y", "save a rev"},
                             {"B", "close"}, {"HOLD B", listCursor_ > 0 ? "delete" : nullptr}};
        hints(r, fonts, list, 4);
        return;
    }

    // ---- the instrument list -----------------------------------------------------
    if (choosing_) {
        // Another instrument for a track that is there: what goes is said under the list (it cannot be undone).
        const int w = 340, h = listH(kInstrumentCount, changing_ >= 0), x = (kScreenW - w) / 2, y = listTop(kInstrumentCount, changing_ >= 0);
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, h, kPanel, kLine);
        if (changing_ >= 0) std::snprintf(text, sizeof text, "TRACK %d: CHANGE ITS INSTRUMENT", changing_ + 1);
        else std::snprintf(text, sizeof text, "TRACK %d: PICK AN INSTRUMENT", tracks + 1);
        fonts.small.draw(r, x + 12.f, y + 12.f, text, kDim);
        for (int i = 0; i < kInstrumentCount; ++i) {
            const InstrumentInfo& info = kInstruments[i];
            const int by = y + 36 + i * listPitch(kInstrumentCount, changing_ >= 0), bh = rowH(kInstrumentCount, changing_ >= 0);
            char about[64];
            if (info.countPresets) std::snprintf(about, sizeof about, "%s, %d presets", info.about, builtInPresets(info.kind));
            else std::snprintf(about, sizeof about, "%s", info.about);
            box(r, x + 12, by, w - 24, bh, kPanel2, kLine);
            if (bh >= 44) { // (the usual box: as it always was)
                fonts.large.draw(r, x + 24.f, by + 9.f * bh / 46.f, info.listName, i == choice_ ? kTeal : kText);
                fonts.within(&Fonts::small, about, w - 48.f).draw(r, x + 24.f, by + bh - 17.f, about, kDim); // (the longest at XL: held at LARGE)
            } else {
                // Nine instruments (MORPH's) close the rows up to 40 adding a track and 35 with the words under
                // them: the name in the medium lettering, both lines placed by their own heights in the box's
                // middle, each held at LARGE's where the size chosen would not fit (the box is a tight row).
                const Font& about2 = fonts.within(&Fonts::small, about, w - 48.f);
                const Font* name = &fonts.medium;
                if (name->capHeight() + about2.capHeight() + kListLineGap > bh - kListTailRoom) name = &fonts.fixed().medium;
                const Font& aboutFont = name == &fonts.medium ? about2 : fonts.fixed().small;
                const float used = name->capHeight() + kListLineGap + aboutFont.capHeight();
                const float top = by + (bh - kListTailRoom - used) / 2.f + 1.f; // (the second line's tails, "g" and "p", stay inside)
                name->draw(r, x + 24.f, top, info.listName, i == choice_ ? kTeal : kText);
                aboutFont.draw(r, x + 24.f, top + name->capHeight() + kListLineGap, about, kDim);
            }
            if (i == choice_) outline(r, x + 11, by - 1, w - 22, bh + 2, kTeal, 2);
        }
        if (changing_ >= 0 && kInstruments[choice_].kind != project_.tracks[changing_].kind) { // (A on the one it has changes nothing)
            const float ly = y + 36.f + kInstrumentCount * static_cast<float>(listPitch(kInstrumentCount, true)) + 4.f;
            for (int line = 0; line < 2; ++line)
                fonts.within(&Fonts::small, kChangeLostWords[line], w - 24.f).draw(r, x + 12.f, ly + line * 20.f, kChangeLostWords[line], kBrass);
        }
        const Hint list[] = {{"A", changing_ >= 0 ? "change to it" : "add the track"}, {"B", "cancel"}};
        hints(r, fonts, list, 2);
        return;
    }

    // ---- hints -------------------------------------------------------------------
    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (trackHoldPart_ > 0.f) {
        // B held on a track, its head filling: what is coming, said where it is confirmed.
        hint("HOLD B", kTrackDeletingWords);
    } else if (mixCardUp()) {
        hint("UP / DOWN", "level");
        hint("LEFT / RIGHT", "pan");
        hint("B", "back to 0 DB, middle");
    } else if (onMaster_) {
        hint("HOLD A", "level and pan of everything");
        hint("START", project_.playing ? "stop" : "play song");
    } else if (onHead_) {
        if (l1OnHead_) {
            hint("R1", project_.trackCount() < kNumTracks ? "copy this track" : "no room for a copy");
            hints(r, fonts, list, count);
            return;
        }
        hint("A", "instrument");
        hint("HOLD A", "mix");
        hint("R1", "name");
        hint("HOLD B", "delete");
        hint("Y", "mute");
        hint("X", "solo"); // (X does not hear the track on SONG: its DEVICE page does)
    } else if (holding_ && onCell()) {
        hint("LEFT / RIGHT", "bars: 1, 2, 4, 8");
        if (isSynth(project_.tracks[column_].kind)) hint("UP / DOWN", "transpose"); // (the stick: an octave)
    } else if (holding_) {
        hint("UP / DOWN", "times the scene plays");
        hint("TAP A", "1 > 2 > 4 > 8");
        hint("LEFT / RIGHT", "steps in a bar");
    } else if (l1OnRepeat_ && column_ == kRepeatColumn && !onSongEnd()) {
        hint("UP / DOWN", "scene tempo");
        hint("LEFT / RIGHT", "steps in a bar");
        hint("B", "usual tempo, 16");
    } else if (onSongEnd() || column_ == kRepeatColumn) {
        if (onSongEnd()) hint("A", project_.songEnd == kSongStop ? "song end: stop" : "song end: loop");
        else {
            hint("HOLD A", "repeats");
            hint("TAP A", "1 > 2 > 4 > 8");
        }
        hint("R1", session_.copiedScene() < 0 ? "copy" : "paste"); // (which scene: its brass mark)
        if (!onSongEnd()) hint("HOLD B", "empty"); // the scene's cells are emptied, one step of undo (no START: with it the bar runs over, as on a cell)
    } else if (column_ == addColumn()) {
        hint("A", "add a track");
        hint("START", project_.playing ? "stop" : "play song");
    } else {
        // All of these fit, with the help at the end (the bar is not left to drop any). Mute and solo
        // are the head's, where its hints say them (X solo here too ran 20 pixels over, 2026-11-15); "hold A + a direction changes it" is the help card's.
        hint("A", "open");
        hint("B", "empty");
        hint("HOLD B", "delete track");
        hint("R1", cellCopied_ ? "paste" : "copy"); // (which cell: its brass mark)
        hint("SELECT", "songs");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
