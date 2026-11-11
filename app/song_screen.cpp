#include "song_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kDeleteHoldMs = 1000; // B held this long on a song deletes it

constexpr int kGridTop = 34, kHeadH = 40, kRowH = 44, kRowsTop = kGridTop + kHeadH + 6;
constexpr int kNumberW = 26, kRepeatW = 34, kColumnsLeft = 8 + kNumberW + kRepeatW;
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 30;
constexpr float kStickCellsPerSecond = 24.f;
constexpr float kRepeatsPerSecond = 20.f;

bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }

// The columns share the width of the screen: few tracks, wide columns. The
// "+ TRACK" column (there until all twelve are used) is one of them.
int columnWidth(int tracks) {
    const int columns = tracks + (tracks < kNumTracks ? 1 : 0);
    return (kScreenW - 8 - kColumnsLeft) / (columns < 1 ? 1 : columns) - 1;
}

} // namespace

void SongScreen::moveCursorTo(int scene, int column) {
    scene_ = scene < 0 ? 0 : (scene >= kMaxScenes ? kMaxScenes - 1 : scene);
    column_ = column < kRepeatColumn ? kRepeatColumn : (column > lastColumn() ? lastColumn() : column);
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

void SongScreen::moveCursor(Button direction) {
    if (listing_) {
        int& cursor = revsOpen_ ? revCursor_ : listCursor_;
        const int last = static_cast<int>(revsOpen_ ? revs_.size() : songs_.size());
        if (direction == Button::Up && cursor > 0) --cursor;
        if (direction == Button::Down && cursor < last) ++cursor;
        return;
    }
    if (choosing_) {
        if (direction == Button::Up) choice_ = (choice_ + 3) % 4;
        if (direction == Button::Down) choice_ = (choice_ + 1) % 4;
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
            if (session_.addTrack(choice_ == 0 ? TrackKind::Drums : choice_ == 1 ? TrackKind::Phazed : choice_ == 2 ? TrackKind::Twin : TrackKind::Sampler)) {
                moveCursorTo(scene_, project_.trackCount() - 1); // onto the new track
            }
        }
        return;
    }

    if (b == Button::A) {
        if (event.down) {
            if (onCell()) {
                // Held, the stick sets how many bars long it is. Let go
                // without doing that, and it opens.
                holding_ = true;
                turned_ = false;
                saved_ = 0.f;
            } else if (column_ == addColumn()) {
                choosing_ = true;
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
        } else {
            moveCursor(b);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        if (onCell()) clearCell(column_, scene_);
        else if (column_ == kRepeatColumn) setRepeats(1);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        project_.scene = scene_;
        session_.togglePlay(true); // the song, from this scene
    }
}

void SongScreen::update(const Input& input, Uint32 nowMs) {
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

    if (holding_ && onCell()) {
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
        if (holding_) {
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
    const bool held = holding_ || copying_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;
    const int playScene = session_.engine.playScene(), playStep = session_.engine.playStep();

    // ---- column heads ----------------------------------------------------------
    fonts.small.drawRight(r, kColumnsLeft - 7.f, kGridTop + 14.f, "PLAYS", kDim);
    for (int t = 0; t < tracks; ++t) {
        const int x = kColumnsLeft + t * (colW + 1);
        box(r, x, kGridTop, colW, kHeadH, kPanel2, kLine);
        std::snprintf(text, sizeof text, "%d", t + 1);
        fonts.small.drawCentered(r, x + colW / 2.f, kGridTop + 5.f, text, kDim);
        const TrackKind kind = project_.tracks[t].kind;
        const char* name = kind == TrackKind::Drums ? "DRUMS" : kind == TrackKind::Twin ? "TWIN" : kind == TrackKind::Sampler ? (colW > 60 ? "SAMPLER" : "SMPL") : (colW > 60 ? "PHAZED" : "PHZD");
        fonts.small.drawCentered(r, x + colW / 2.f, kGridTop + 22.f, name, kText);
    }
    if (addColumn() >= 0) {
        const int x = kColumnsLeft + tracks * (colW + 1);
        outline(r, x, kGridTop, colW, kHeadH, kLine);
        fonts.small.drawCentered(r, x + colW / 2.f, kGridTop + 14.f, colW > 60 ? "+ TRACK" : "+", kDim);
        if (column_ == addColumn()) outline(r, x - 1, kGridTop - 1, colW + 2, kHeadH + 2, cursorColor, 2);
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
        fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + (playingHere ? 10.f : 15.f), text, inSong ? kText : kDim);
        if (playingHere) {
            // One block per pass, filling as the bar goes by.
                sceneProgress(r, rx + 4, y + kRowH - 14, kRepeatW - 12, 6, project_.playLoop ? 1 : repeats, session_.engine.playRepeat(),
                          session_.engine.playBar() * kSteps + playStep, project_.sceneBars(scene) * kSteps);
        }
        if (column_ == kRepeatColumn && scene == scene_) {
            if (holding_) fill(r, rx + 1, y + 1, (kRepeatW - 6) * repeats / kMaxRepeats, kRowH - 6, faded(kBrass, 110));
            outline(r, rx - 1, y - 1, kRepeatW - 2, kRowH - 2, cursorColor, 2);
        }

        for (int t = 0; t < tracks; ++t) {
            const TrackData& track = project_.tracks[t];
            const int x = kColumnsLeft + t * (colW + 1);
            const bool empty = track.sceneEmpty(scene);
            if (empty) outline(r, x, y, colW, kRowH - 4, inSong ? kLine : kPanel);
            else box(r, x, y, colW, kRowH - 4, kPanel, playingHere ? kPurple : kLine);

            // A small picture of the cell: all its bars, as wide as its share of
            // the scene. A cell shorter than the scene is drawn once, and the
            // room after it says how many times it goes round.
            const int cellBars = track.bars[scene], sceneBars = project_.sceneBars(scene);
            const int pictureW = (colW - 6) * cellBars / sceneBars;
            const float stepW = static_cast<float>(pictureW) / (16.f * cellBars);
            if (track.kind == TrackKind::Drums) {
                for (int bar = 0; bar < cellBars; ++bar) {
                    const DrumPattern& p = track.drums[scene][bar];
                    for (int lane = 0; lane < kDrumLanes; ++lane)
                        for (int step = 0; step < kSteps; ++step)
                            if (p.level[lane][step] > 0.f) {
                                fill(r, x + 3 + static_cast<int>((bar * kSteps + step) * stepW), y + 4 + lane * 4, 2, 2,
                                     faded(kText, 90 + static_cast<int>(165 * p.level[lane][step])));
                            }
                }
            } else {
                int low = 127, high = 0;
                for (int bar = 0; bar < cellBars; ++bar) {
                    NoteEvent notes[kMaxNotes]; // chords drawn as their notes
                    track.notes[scene][bar].render(notes);
                    for (const NoteEvent& n : notes)
                        if (n.velocity > 0.f) { low = n.pitch < low ? n.pitch : low; high = n.pitch > high ? n.pitch : high; }
                }
                const int span = high - low < 7 ? 7 : high - low;
                for (int bar = 0; bar < cellBars; ++bar) {
                    NoteEvent notes[kMaxNotes];
                    track.notes[scene][bar].render(notes);
                    for (const NoteEvent& n : notes) {
                        if (n.velocity <= 0.f) continue;
                        const int ny = y + 4 + (kRowH - 14) * (high - n.pitch) / span;
                        const int from = static_cast<int>((bar * kSteps + n.step) * stepW);
                        int w = static_cast<int>(n.length * stepW) - 1;
                        if (from + w > pictureW) w = pictureW - from; // it runs to the end of the cell at most
                        fill(r, x + 3 + from, ny, w < 2 ? 2 : w, 2, kText);
                    }
                }
            }
            if (!empty && cellBars < sceneBars) {
                // The loops mark: where the cell ends, and how many times it plays in the scene.
                fill(r, x + 3 + pictureW, y + 3, 1, kRowH - 10, kPurple);
                std::snprintf(text, sizeof text, colW - 6 - pictureW >= 60 ? "LOOPS x%d" : "x%d", sceneBars / cellBars);
                fonts.small.drawCentered(r, x + 3 + pictureW + (colW - 6 - pictureW) / 2.f, y + (kRowH - 4 - fonts.small.capHeight()) / 2.f, text, kPurple);
            }
            if (cellBars > 1) { // how long it is, in the corner
                std::snprintf(text, sizeof text, "%d", cellBars);
                fonts.small.drawRight(r, x + colW - 4.f, y + 3.f, text, kDim);
            }
            // Where the cell has got to: a short one goes round under a long scene.
            if (playingHere && playStep >= 0) {
                const int at = (session_.engine.playBar() % cellBars) * kSteps + playStep;
                fill(r, x + 3 + static_cast<int>(at * stepW), y + 2, 2, kRowH - 8, kPurple);
            }
            if (copying_ && t == copyTrack_ && scene == copyScene_) outline(r, x + 1, y + 1, colW - 2, kRowH - 6, kBrass);
            if (t == column_ && scene == scene_) outline(r, x - 1, y - 1, colW + 2, kRowH - 2, cursorColor, 2);
        }
    }
    // Where the song ends and comes round.
    if (songScenes > top_ && songScenes <= top_ + kVisibleRows) {
        const int y = kRowsTop + (songScenes - top_) * kRowH - 3;
        fill(r, 8, y, kColumnsLeft + tracks * (colW + 1) - 10, 1, kPurple);
    }
    if (top_ > 0) fonts.small.draw(r, 10.f, kGridTop + 28.f, "^", kDim);

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
                if (address[0] == ':') fonts.small.draw(r, x + 12.f, ty + 16.f, "NOT ON A NETWORK. JOIN WIFI IN THE FIRMWARE FIRST.", kBrass);
                else fonts.small.draw(r, x + 12.f, ty + 16.f, ("OPEN IN A BROWSER:  " + address).c_str(), kBrass);
            } else {
                fonts.small.draw(r, x + 12.f, ty + 16.f, "Recordings, chord packs and songs, from a browser.", kDim);
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
                    fonts.small.drawRight(r, x + w - 22.f, by + 10.f, "THE SONG AS IT IS NOW", kDim);
                } else {
                    const int rev = revs_[static_cast<std::size_t>(i - 1)];
                    std::snprintf(text, sizeof text, "REV %d", rev);
                    fonts.medium.draw(r, x + 22.f, by + 9.f, text, i == revCursor_ ? kTeal : kText);
                    fonts.small.drawRight(r, x + w - 22.f, by + 10.f, session_.revWhen(rev).c_str(), kDim);
                }
                if (i == revCursor_) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
            }
            const Hint list[] = {{"STICK", "move"}, {"A", revCursor_ == 0 ? "save a rev" : "go back to this rev"}, {"X", "save a rev"}, {"B", "songs"}};
            hints(r, fonts, list, 4);
            return;
        }
        const int rows = static_cast<int>(songs_.size()) + 1;
        const int first = listCursor_ < shown ? 0 : listCursor_ - shown + 1;
        fonts.small.draw(r, x + 12.f, y + 12.f, "SONGS", kDim);
        fonts.small.drawRight(r, x + w - 12.f, y + 12.f, saved[0] ? saved : "KEPT AS YOU GO. NOTHING TO SAVE.", saved[0] ? kBrass : kDim);
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
                fonts.medium.draw(r, x + 22.f, by + 9.f, upper, i == listCursor_ ? kTeal : kText);
                if (name == session_.songName()) fonts.small.drawRight(r, x + w - 22.f, by + 10.f, "OPEN NOW  >  REVS", kPurple);
                // B held on it: the row fills, and at full the song is deleted.
                if (bHeld_ && bDownAt_ != 0 && i == bRow_) {
                    const float part = static_cast<float>(lastMs_ - bDownAt_) / static_cast<float>(kDeleteHoldMs);
                    fill(r, x + 13, by + 1, static_cast<int>((w - 26) * (part > 1.f ? 1.f : part)), rowH - 6, faded(kBrass, 120));
                    fonts.small.drawRight(r, x + w - 22.f, by + 10.f, "KEEP HOLDING TO DELETE", kBrass);
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
        const int w = 320, h = 254, x = (kScreenW - w) / 2, y = 100;
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, h, kPanel, kLine);
        std::snprintf(text, sizeof text, "TRACK %d: PICK AN INSTRUMENT", tracks + 1);
        fonts.small.draw(r, x + 12.f, y + 12.f, text, kDim);
        const char* names[4] = {"DRUMS", "PHAZED", "TWIN ANALOG", "SAMPLER"};
        char synthAbout[40];
        std::snprintf(synthAbout, sizeof synthAbout, "the synth, %d presets", Phazed::presetCount());
        char twinAbout[48];
        std::snprintf(twinAbout, sizeof twinAbout, "two morphing oscillators, %d presets", Twin::presetCount());
        const char* about[4] = {"8 lanes: built-in drums or your recordings", synthAbout, twinAbout, "one recording, played from the keys"};
        for (int i = 0; i < 4; ++i) {
            const int by = y + 36 + i * 52;
            box(r, x + 12, by, w - 24, 46, kPanel2, kLine);
            fonts.large.draw(r, x + 24.f, by + 9.f, names[i], i == choice_ ? kTeal : kText);
            fonts.small.draw(r, x + 24.f, by + 29.f, about[i], kDim);
            if (i == choice_) outline(r, x + 11, by - 1, w - 22, 48, kTeal, 2);
        }
        const Hint list[] = {{"STICK", "move"}, {"A", "add the track"}, {"B", "cancel"}};
        hints(r, fonts, list, 3);
        return;
    }

    // ---- hints -------------------------------------------------------------------
    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (copying_) {
        hint("DRAG WITH THE STICK", "copy this bar into every cell crossed");
    } else if (holding_ && onCell()) {
        hint("STICK LEFT / RIGHT", "how long this cell is: 1, 2, 4 or 8 bars");
    } else if (holding_) {
        hint("STICK UP / DOWN", "times the scene plays");
        hint("TAP A", "1 > 2 > 4 > 8");
    } else if (column_ == kRepeatColumn) {
        hint("STICK", "move");
        hint("HOLD A + STICK", "times it plays");
        hint("TAP A", "1 > 2 > 4 > 8");
        hint("START", project_.playing ? "stop" : "play song");
    } else if (column_ == addColumn()) {
        hint("STICK", "move");
        hint("A", "add a track");
        hint("START", project_.playing ? "stop" : "play song");
    } else {
        hint("A", "open");
        hint("HOLD A + STICK", "bars");
        hint("B", "empty");
        hint("HOLD R1", "copy");
        hint("START", project_.playing ? "stop" : "play song");
        hint("SELECT", "songs");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
