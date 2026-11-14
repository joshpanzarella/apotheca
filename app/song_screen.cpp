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
constexpr int kNumberW = 26, kRepeatW = 34, kColumnsLeft = 8 + kNumberW + kRepeatW;
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kStickCellsPerSecond = 24.f;
constexpr float kRepeatsPerSecond = 20.f;
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
    onHead_ = onMaster_ = false;
    scene_ = scene < 0 ? 0 : (scene >= kMaxScenes ? kMaxScenes - 1 : scene);
    column_ = column < kRepeatColumn ? kRepeatColumn : (column > lastColumn() ? lastColumn() : column);
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

// The scenes from `from` on are one place further down the song (by = 1) or back up it (-1).
void SongScreen::scenesMoved(int from, int by) {
    if (scene_ < from || scene_ + by < 0 || scene_ + by >= kMaxScenes) return;
    scene_ += by;
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

void SongScreen::moveCursor(Button direction, bool pressed) {
    if (listing_) {
        int& cursor = revsOpen_ ? revCursor_ : listCursor_;
        const int last = static_cast<int>(revsOpen_ ? revs_.size() : songs_.size());
        if (direction == Button::Up && cursor > 0) --cursor;
        if (direction == Button::Down && cursor < last) ++cursor;
        return;
    }
    if (choosing_) {
        if (direction == Button::Up) choice_ = (choice_ + kInstrumentCount - 1) % kInstrumentCount;
        if (direction == Button::Down) choice_ = (choice_ + 1) % kInstrumentCount;
        return;
    }
    // MASTER, the head at the right-hand end: back left to + TRACK, or the last track's head.
    if (onMaster_) {
        if (direction == Button::Left) {
            if (addColumn() >= 0) moveCursorTo(scene_, addColumn());
            else {
                onMaster_ = false;
                onHead_ = true;
                column_ = project_.trackCount() - 1;
            }
        }
        return;
    }
    // Up from the top scene is the track's head: its name. A there types a new one.
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
            else if (addColumn() >= 0) moveCursorTo(scene_, addColumn());
            else moveToMaster(); // twelve tracks: no + TRACK between
        }
        return;
    }
    // Right of + TRACK (drawn at the heads' height) is MASTER.
    if (direction == Button::Right && column_ == addColumn() && !copying_) {
        moveToMaster();
        return;
    }
    // Only by a press of the D-pad: the stick, or a held D-pad, running up the scenes stops at the top one.
    if (direction == Button::Up && scene_ == 0 && onCell() && !copying_ && pressed) {
        onHead_ = true;
        return;
    }
    if (direction == Button::Up) moveCursorTo(scene_ - 1, column_);
    if (direction == Button::Down) moveCursorTo(scene_ + 1, column_);
    if (direction == Button::Left) moveCursorTo(scene_, column_ - 1);
    if (direction == Button::Right) moveCursorTo(scene_, column_ + 1);
    if (copying_) pasteHere();
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
    TrackData& t = project_.tracks[track];
    // Every bar of it, and it is one bar long again.
    t.bars[scene] = 1;
    if (track == project_.track && scene == project_.scene) project_.bar = 0;
    if (t.kind == TrackKind::Drums) {
        for (DrumPattern& bar : t.drums[scene]) bar = DrumPattern();
        session_.sendDrumPattern(track, scene);
    } else if (isSynth(t.kind)) {
        for (NotePattern& bar : t.notes[scene]) bar = NotePattern();
        session_.sendNotePattern(track, scene);
    }
}

void SongScreen::copyCell(int fromTrack, int fromScene, int toTrack, int toScene) {
    TrackData& from = project_.tracks[fromTrack];
    TrackData& to = project_.tracks[toTrack];
    if (from.kind != to.kind || (fromTrack == toTrack && fromScene == toScene)) return; // a drum bar cannot go on a synth
    to.bars[toScene] = from.bars[fromScene]; // the whole cell: its length and every bar
    if (toTrack == project_.track && toScene == project_.scene) project_.bar = 0;
    if (to.kind == TrackKind::Drums) {
        for (int bar = 0; bar < kMaxBars; ++bar) to.drums[toScene][bar] = from.drums[fromScene][bar];
        session_.sendDrumPattern(toTrack, toScene);
    } else if (isSynth(to.kind)) {
        for (int bar = 0; bar < kMaxBars; ++bar) to.notes[toScene][bar] = from.notes[fromScene][bar];
        session_.sendNotePattern(toTrack, toScene);
    }
}

void SongScreen::pasteHere() {
    if (onCell()) copyCell(copyTrack_, copyScene_, column_, scene_);
}

void SongScreen::openRevList() {
    revsOpen_ = true;
    revs_ = session_.revs();
    revCursor_ = 0;
}

void SongScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

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
        if (isDirection(b)) {
            moveCursor(b);
            repeating_ = b;
            repeatAt_ = 0;
        }
        if (b == Button::L1) {
            session_.toggleTransfer(lastMs_ * 2654435761u + 12345u); // files over WiFi, on or off
            return;
        }
        if (b == Button::X) {
            // Anywhere in the list: keep the song as it is now.
            savedRev_ = session_.saveRev();
            if (revsOpen_) openRevList();
            return;
        }
        if (revsOpen_) {
            if (b == Button::B) revsOpen_ = false; // back to the songs
            if (b == Button::A && revCursor_ == 0) {
                savedRev_ = session_.saveRev();
                openRevList();
            } else if (b == Button::A) {
                if (session_.openRev(revs_[static_cast<std::size_t>(revCursor_ - 1)])) {
                    listing_ = revsOpen_ = false;
                    scene_ = column_ = top_ = 0;
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
        if (b == Button::Y && listCursor_ > 0 && songs_[static_cast<std::size_t>(listCursor_ - 1)] == session_.songName()) {
            const std::string renamed = session_.renameSong();
            songs_ = session_.songNames();
            for (std::size_t i = 0; i < songs_.size(); ++i)
                if (songs_[i] == renamed) listCursor_ = static_cast<int>(i) + 1;
        }
        if (b == Button::A) {
            if (listCursor_ > 0 && songs_[static_cast<std::size_t>(listCursor_ - 1)] == session_.songName()) {
                openRevList(); // the song that is open: its revs
                return;
            }
            listing_ = false;
            if (listCursor_ == 0) session_.newSong();
            else session_.openSong(songs_[static_cast<std::size_t>(listCursor_ - 1)]);
            scene_ = column_ = top_ = 0; // a different song: start at its top
        }
        return;
    }

    if (choosing_) {
        if (!event.down) return;
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
    // on a track's head that opens the keyboard for its name (on the SET tab; it comes back here).
    if ((onHead_ || onMaster_) && b == Button::A) {
        if (event.down) {
            mixing_ = true;
            mixTurned_ = trackHold_ && input.held(Button::B); // A pressed while B was deleting the track calls the delete off (update()), and is no tap
            mixSavedDb_ = mixSavedPan_ = 0.f;
        } else {
            if (mixing_ && !mixTurned_ && onHead_) {
                project_.track = column_;
                session_.wantTrackName = true;
                session_.view = View::Settings;
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
    // Any other button with A held on a head: letting go of A is then no tap (no keyboard for the name),
    // and only START does its job (Y, X, L1 would mute, hear or solo the track under the card).
    if (mixing_ && event.down) mixTurned_ = true;
    if (mixing_ && b != Button::Start) return;
    if (onMaster_ && (b == Button::B || b == Button::R1)) return; // nothing to empty, delete or change there
    if (onHead_ && (b == Button::B || b == Button::R1)) {
        // On a track's head: B held deletes the track, as it does on a cell; a tap of it empties nothing here.
        if (b == Button::B && event.down && !input.held(Button::A)) {
            trackHold_ = true;
            trackHoldTimed_ = false;
            trackHoldPart_ = 0.f;
        }
        // R1: the list of instruments, to change this track's. It opens on the one it has.
        if (b == Button::R1 && event.down && !mixing_) {
            choosing_ = true;
            changing_ = column_;
            for (int i = 0; i < kInstrumentCount; ++i)
                if (kInstruments[i].kind == project_.tracks[column_].kind) choice_ = i;
        }
        return;
    }
    if (b == Button::A) {
        if (event.down) {
            if (onCell()) {
                // Held, the stick sets how many bars long it is. Let go
                // without doing that, and it opens. (Pressed while B was deleting the track, it calls the delete off and opens nothing.)
                holding_ = true;
                turned_ = trackHold_ && input.held(Button::B);
                saved_ = 0.f;
            } else if (column_ == addColumn()) {
                choosing_ = true;
                changing_ = -1;
                choice_ = 1; // Phazed: the likelier second track
            } else if (column_ == kRepeatColumn) {
                holding_ = true;
                turned_ = false;
                saved_ = 0.f;
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
    // On a track: Y mutes it, L1 solos it, X hears it. At once, playing or not.
    if (onCell() && event.down && !holding_ && (b == Button::X || b == Button::Y || b == Button::L1)) {
        if (b == Button::Y) session_.setMuted(column_, !project_.tracks[column_].muted);
        if (b == Button::L1) session_.setSolo(column_, !project_.tracks[column_].solo);
        if (b == Button::X) session_.hearTrack(column_);
        return;
    }
    if (b == Button::R1) {
        copying_ = event.down && onCell();
        if (copying_) {
            copyTrack_ = column_;
            copyScene_ = scene_;
        }
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
        } else if (holding_) {
            // A held on a repeat count: up and down change it by one.
            if (b == Button::Up) setRepeats(project_.repeats[scene_] + 1), turned_ = true;
            if (b == Button::Down) setRepeats(project_.repeats[scene_] - 1), turned_ = true;
            // ...and left and right, how many steps the scene's bars have: 16, or fewer.
            if (b == Button::Right) session_.setSceneSteps(scene_, project_.sceneSteps(scene_) + 1), turned_ = true;
            if (b == Button::Left) session_.setSceneSteps(scene_, project_.sceneSteps(scene_) - 1), turned_ = true;
        } else {
            moveCursor(b, true);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        // A tap empties the cell. Kept held on a track, it goes on to delete the whole track (update()).
        trackHold_ = onCell();
        trackHoldTimed_ = false;
        trackHoldPart_ = 0.f;
        if (onCell()) clearCell(column_, scene_);
        else if (column_ == kRepeatColumn) setRepeats(1);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        project_.scene = scene_;
        session_.togglePlay(true); // the song, from this scene
    }
}

void SongScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;
    if (!input.held(Button::A) || (!onHead_ && !onMaster_) || listing_ || choosing_) mixing_ = false;
    if (!input.held(Button::R1)) copying_ = false;
    if (!onCell()) onHead_ = false; // the track under it went
    if (onMaster_ && (listing_ || choosing_)) onMaster_ = false; // (a song opened: start at its top)
    if (!input.held(Button::B)) bHeld_ = false;
    // B kept held on a track: after a moment a bar begins to fill on its head, and when it is full the track is deleted.
    // A pressed while it fills calls it off, as any other button does on the FX screen.
    if (!input.held(Button::B) || input.held(Button::A) || !onCell() || listing_ || choosing_) trackHold_ = false, trackHoldPart_ = 0.f;
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
        if (wasOpen) scene_ = column_ = top_ = 0;
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
        stickX_ = stickY_ = 0.f;
        stickWasY_ = false;
    } else if (holding_) {
        // The stick turns the repeat count.
        const float push = -y;
        saved_ += push * std::fabs(push) * kRepeatsPerSecond * dt;
        while (saved_ >= 1.f) { saved_ -= 1.f; setRepeats(project_.repeats[scene_] + 1); turned_ = true; }
        while (saved_ <= -1.f) { saved_ += 1.f; setRepeats(project_.repeats[scene_] - 1); turned_ = true; }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else {
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickCellsPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickCellsPerSecond * dt : 1.f;
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
        } else if (holding_) {
            if (repeating_ == Button::Up) setRepeats(project_.repeats[scene_] + 1);
            if (repeating_ == Button::Down) setRepeats(project_.repeats[scene_] - 1);
        } else {
            moveCursor(repeating_);
        }
    }
}

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
    const bool held = holding_ || copying_ || mixing_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;
    const int playScene = session_.playScene(), playStep = session_.engine.playStep();

    // ---- column heads ----------------------------------------------------------
    fonts.small.drawRight(r, kColumnsLeft - 7.f, kGridTop + 14.f, "PLAYS", kDim);
    for (int t = 0; t < tracks; ++t) {
        const int x = kColumnsLeft + t * (colW + 1);
        box(r, x, kGridTop, colW, kHeadH, kPanel2, kLine);
        const bool sounding = session_.heard(t);
        // B held on this track: its head fills red and says what is coming, and
        // nothing else: the number and the name come back if B is let go. The
        // words flash slowly, three times in the two seconds.
        const bool deleting = trackHoldPart_ > 0.f && t == column_;
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
            (colW > 60 ? fonts : fonts.fixed()).small.drawCentered(r, x + colW / 2.f, kGridTop + 5.f, text, kDim);
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
        // Not at its usual level or pan (the mixer): a brass mark in the other corner.
        if (!deleting && (project_.tracks[t].mixDb != 0 || project_.tracks[t].mixPan != 0)) fill(r, x + kMixMarkInset, kGridTop + kMixMarkInset, kMixMarkSize, kMixMarkSize, kBrass);
        if (project_.tracks[t].solo) {
            fill(r, x + colW - 13, kGridTop + 2, 11, 11, kTeal);
            fonts.small.drawCentered(r, x + colW - 7.5f, kGridTop + 3.f, "S", kBg);
            outline(r, x, kGridTop, colW, kHeadH, kTeal);
        } else if (project_.tracks[t].muted) {
            fill(r, x + colW - 13, kGridTop + 2, 11, 11, kBrass);
            fonts.small.drawCentered(r, x + colW - 7.5f, kGridTop + 3.f, "M", kBg);
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
        const bool odd = project_.sceneSteps(scene) != project_.sceneBars(scene) * kSteps; // a scene cut short says how many steps it lasts, under its count
        fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + (playingHere || odd ? 6.f : 15.f), text, inSong ? kText : kDim);
        if (odd && !playingHere) {
            std::snprintf(text, sizeof text, "%d", project_.sceneSteps(scene));
            fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + 22.f, text, kBrass);
        }
        if (playingHere) {
            // One block per pass, filling as the bar goes by.
                sceneProgress(r, rx + 4, y + kRowH - 14, kRepeatW - 12, 6, project_.playLoop ? 1 : repeats, session_.engine.playRepeat(),
                          session_.engine.playBar() * kSteps + playStep, project_.sceneSteps(scene));
        }
        if (column_ == kRepeatColumn && scene == scene_ && !onMaster_) {
            if (holding_) fill(r, rx + 1, y + 1, (kRepeatW - 6) * repeats / kMaxRepeats, kRowH - 6, faded(kBrass, 110));
            outline(r, rx - 1, y - 1, kRepeatW - 2, kRowH - 2, cursorColor, 2);
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
            const int cellBars = track.bars[scene], sceneBars = project_.sceneBars(scene);
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
            for (int at = 0; at < sceneBars && !empty; ++at) {
                const int bar = at % cellBars;
                const bool again = at >= cellBars; // a later pass
                // This pass ends where the cell starts over, or where the scene ends.
                const int passEnd = static_cast<int>(16.f * stepW * std::min(sceneBars, (at / cellBars + 1) * cellBars));
                if (again && bar == 0) fill(r, x + 3 + static_cast<int>(at * kSteps * stepW), y + 3, 1, kRowH - 10, faded(kPurple, 150)); // fainter than the line that shows where it is playing
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
            if (cellBars > 1) { // how long it is, in the corner
                std::snprintf(text, sizeof text, "%d", cellBars);
                fonts.fixed().small.drawRight(r, x + colW - 4.f, y + 3.f, text, kDim); // (over the cell's little picture, to the pixel: always at LARGE)
            }
            // Where the cell has got to: a short one goes round under a long scene.
            if (playingHere && playStep >= 0) {
                const int at = session_.engine.playBar() * kSteps + playStep;
                fill(r, x + 3 + static_cast<int>(at * stepW), y + 2, 2, kRowH - 8, kPurple);
            }
            if (copying_ && t == copyTrack_ && scene == copyScene_) outline(r, x + 1, y + 1, colW - 2, kRowH - 6, kBrass);
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
    if (mixing_ && !listing_ && !choosing_) {
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
        const int w = 360, rowH = 34, shown = 9, x = (kScreenW - w) / 2, y = 60;
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, 80 + shown * rowH, kPanel, kLine);
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
                const std::string open = address[0] == ':' ? "NOT ON A NETWORK. JOIN WIFI IN THE FIRMWARE FIRST." : "OPEN IN A BROWSER:  " + address;
                fonts.within(&Fonts::small, open.c_str(), w - 24.f).draw(r, x + 12.f, ty + 16.f, open.c_str(), kBrass);
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
            const int rows = static_cast<int>(revs_.size()) + 1;
            const int first = revCursor_ < shown ? 0 : revCursor_ - shown + 1;
            for (int i = first; i < rows && i < first + shown; ++i) {
                const int by = y + 32 + (i - first) * rowH;
                box(r, x + 12, by, w - 24, rowH - 4, kPanel2, kLine);
                if (i == 0) {
                    fonts.medium.draw(r, x + 22.f, by + 9.f, "+ SAVE A REV", i == revCursor_ ? kTeal : kText);
                    const char* what = "THE SONG AS IT IS NOW"; // (clear of "+ SAVE A REV" beside it: at XL it is held at LARGE)
                    fonts.within(&Fonts::small, what, w - 44.f - fonts.medium.width("+ SAVE A REV") - 12.f).drawRight(r, x + w - 22.f, by + 10.f, what, kDim);
                } else {
                    const int rev = revs_[static_cast<std::size_t>(i - 1)];
                    std::snprintf(text, sizeof text, "REV %d", rev);
                    fonts.medium.draw(r, x + 22.f, by + 9.f, text, i == revCursor_ ? kTeal : kText);
                    fonts.small.drawRight(r, x + w - 22.f, by + 10.f, session_.revWhen(rev).c_str(), kDim);
                }
                if (i == revCursor_) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
            }
            const Hint list[] = {{"A", revCursor_ == 0 ? "save a rev" : "go back to this rev"}, {"X", "save a rev"}, {"B", "songs"}};
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
        const Hint list[] = {{"A", listCursor_ == 0 ? "new song" : onOpen ? "its revs" : "open"}, {"X", "save a rev"}, {"Y", onOpen ? "new name" : nullptr},
                             {"B", "close"}, {"HOLD B", listCursor_ > 0 ? "delete" : nullptr}};
        hints(r, fonts, list, 5);
        return;
    }

    // ---- the instrument list -----------------------------------------------------
    if (choosing_) {
        // Another instrument for a track that is there: what goes is said under the list (it cannot be undone).
        const int w = 340, h = 46 + kInstrumentCount * 52 + (changing_ >= 0 ? kChangeLostH : 0), x = (kScreenW - w) / 2, y = 70;
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, h, kPanel, kLine);
        if (changing_ >= 0) std::snprintf(text, sizeof text, "TRACK %d: CHANGE ITS INSTRUMENT", changing_ + 1);
        else std::snprintf(text, sizeof text, "TRACK %d: PICK AN INSTRUMENT", tracks + 1);
        fonts.small.draw(r, x + 12.f, y + 12.f, text, kDim);
        for (int i = 0; i < kInstrumentCount; ++i) {
            const InstrumentInfo& info = kInstruments[i];
            const int by = y + 36 + i * 52;
            char about[64];
            if (info.countPresets) std::snprintf(about, sizeof about, "%s, %d presets", info.about, builtInPresets(info.kind));
            else std::snprintf(about, sizeof about, "%s", info.about);
            box(r, x + 12, by, w - 24, 46, kPanel2, kLine);
            fonts.large.draw(r, x + 24.f, by + 9.f, info.listName, i == choice_ ? kTeal : kText);
            fonts.within(&Fonts::small, about, w - 48.f).draw(r, x + 24.f, by + 29.f, about, kDim); // (the longest at XL: held at LARGE)
            if (i == choice_) outline(r, x + 11, by - 1, w - 22, 48, kTeal, 2);
        }
        if (changing_ >= 0 && kInstruments[choice_].kind != project_.tracks[changing_].kind) { // (A on the one it has changes nothing)
            const float ly = y + 36.f + kInstrumentCount * 52.f + 4.f;
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
    } else if (mixing_) {
        hint("UP / DOWN", "level");
        hint("LEFT / RIGHT", "pan");
        hint("B", "back to 0 DB, middle");
    } else if (onMaster_) {
        hint("HOLD A", "level and pan of everything");
        hint("START", project_.playing ? "stop" : "play song");
    } else if (onHead_) {
        hint("A", "name");
        hint("HOLD A", "mix");
        hint("R1", "instrument");
        hint("HOLD B", "delete");
        hint("Y", "mute");
        hint("L1", "solo"); // (X hears it, as everywhere: the help card says so, and the bar has no room since HOLD A)
    } else if (copying_) {
        hint("DRAG", "copy this bar into every cell crossed");
    } else if (holding_ && onCell()) {
        hint("LEFT / RIGHT", "how long this cell is: 1, 2, 4 or 8 bars");
    } else if (holding_) {
        hint("UP / DOWN", "times the scene plays");
        hint("TAP A", "1 > 2 > 4 > 8");
        hint("LEFT / RIGHT", "steps in a bar");
    } else if (column_ == kRepeatColumn) {
        hint("HOLD A", "times it plays");
        hint("TAP A", "1 > 2 > 4 > 8");
        hint("START", project_.playing ? "stop" : "play song");
    } else if (column_ == addColumn()) {
        hint("A", "add a track");
        hint("START", project_.playing ? "stop" : "play song");
    } else {
        // All of these fit, with the help at the end (the bar is not left to drop any). Mute and solo
        // are the head's, where its hints say them; "hold A + a direction changes it" is the help card's.
        hint("A", "open");
        hint("B", "empty");
        hint("HOLD B", "delete track");
        hint("HOLD R1", "copy");
        hint("SELECT", "songs");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
