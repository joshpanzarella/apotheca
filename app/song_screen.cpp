#include "song_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {

constexpr int kGridTop = 34, kHeadH = 40, kRowH = 44, kRowsTop = kGridTop + kHeadH + 6;
constexpr int kNumberW = 26, kRepeatW = 34, kColumnsLeft = 8 + kNumberW + kRepeatW;
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 30;
constexpr float kStickCellsPerSecond = 24.f;
constexpr float kRepeatsPerSecond = 20.f;

bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }

int columnWidth(int tracks) { return tracks <= 4 ? 96 : 46; }

} // namespace

void SongScreen::moveCursorTo(int scene, int column) {
    scene_ = scene < 0 ? 0 : (scene >= kMaxScenes ? kMaxScenes - 1 : scene);
    column_ = column < kRepeatColumn ? kRepeatColumn : (column > lastColumn() ? lastColumn() : column);
    if (scene_ < top_) top_ = scene_;
    if (scene_ >= top_ + kVisibleRows) top_ = scene_ - kVisibleRows + 1;
}

void SongScreen::moveCursor(Button direction) {
    if (choosing_) {
        if (direction == Button::Up) choice_ = (choice_ + 2) % 3;
        if (direction == Button::Down) choice_ = (choice_ + 1) % 3;
        return;
    }
    if (direction == Button::Up) moveCursorTo(scene_ - 1, column_);
    if (direction == Button::Down) moveCursorTo(scene_ + 1, column_);
    if (direction == Button::Left) moveCursorTo(scene_, column_ - 1);
    if (direction == Button::Right) moveCursorTo(scene_, column_ + 1);
    if (copying_) pasteHere();
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
    if (t.kind == TrackKind::Drums) {
        t.drums[scene] = DrumPattern();
        session_.sendDrumPattern(track, scene);
    } else if (isSynth(t.kind)) {
        t.notes[scene] = NotePattern();
        session_.sendNotePattern(track, scene);
    }
}

void SongScreen::copyCell(int fromTrack, int fromScene, int toTrack, int toScene) {
    TrackData& from = project_.tracks[fromTrack];
    TrackData& to = project_.tracks[toTrack];
    if (from.kind != to.kind || (fromTrack == toTrack && fromScene == toScene)) return; // a drum bar cannot go on a synth
    if (to.kind == TrackKind::Drums) {
        to.drums[toScene] = from.drums[fromScene];
        session_.sendDrumPattern(toTrack, toScene);
    } else if (isSynth(to.kind)) {
        to.notes[toScene] = from.notes[fromScene];
        session_.sendNotePattern(toTrack, toScene);
    }
}

void SongScreen::pasteHere() {
    if (onCell()) copyCell(copyTrack_, copyScene_, column_, scene_);
}

void SongScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (choosing_) {
        if (!event.down) return;
        if (isDirection(b)) moveCursor(b);
        if (b == Button::B) choosing_ = false;
        if (b == Button::A) {
            choosing_ = false;
            if (session_.addTrack(choice_ == 0 ? TrackKind::Drums : choice_ == 1 ? TrackKind::Phazed : TrackKind::Twin)) {
                moveCursorTo(scene_, project_.trackCount() - 1); // onto the new track
            }
        }
        return;
    }

    if (b == Button::A) {
        if (event.down) {
            if (onCell()) {
                project_.track = column_;
                project_.scene = scene_;
                session_.view = View::Pattern;
            } else if (column_ == addColumn()) {
                choosing_ = true;
                choice_ = 1; // Phazed: the likelier second track
            } else if (column_ == kRepeatColumn) {
                holding_ = true;
                turned_ = false;
                saved_ = 0.f;
            }
        } else {
            // A plain tap on a repeat count: 1, 2, 4, 8, and round again.
            if (holding_ && !turned_) {
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
        if (holding_) {
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
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();

    if (holding_) {
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
    char text[48];
    const int tracks = project_.trackCount();
    const int songScenes = project_.sceneCount();
    std::snprintf(text, sizeof text, "%d / %d TRACKS, %d SCENE%s", tracks, kNumTracks, songScenes, songScenes == 1 ? "" : "S");
    topBar(r, fonts, View::Song, text, project_);

    const int colW = columnWidth(tracks);
    const bool held = holding_ || copying_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;
    const int playScene = session_.engine.playScene();

    // ---- column heads ----------------------------------------------------------
    fonts.small.drawRight(r, kColumnsLeft - 7.f, kGridTop + 14.f, "PLAYS", kDim);
    for (int t = 0; t < tracks; ++t) {
        const int x = kColumnsLeft + t * (colW + 1);
        box(r, x, kGridTop, colW, kHeadH, kPanel2, kLine);
        std::snprintf(text, sizeof text, "%d", t + 1);
        fonts.small.drawCentered(r, x + colW / 2.f, kGridTop + 5.f, text, kDim);
        const TrackKind kind = project_.tracks[t].kind;
        const char* name = kind == TrackKind::Drums ? "DRUMS" : kind == TrackKind::Twin ? "TWIN" : (colW > 60 ? "PHAZED" : "PHZD");
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
        if (playingHere) {
            const int done = session_.engine.playRepeat() + 1;
            fill(r, rx + 1, y + 1, (kRepeatW - 6) * done / repeats, kRowH - 6, faded(kPurple, 90));
        }
        std::snprintf(text, sizeof text, "x%d", repeats);
        fonts.small.drawCentered(r, rx + (kRepeatW - 4) / 2.f, y + 15.f, text, inSong ? kText : kDim);
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

            // A small picture of the bar.
            const float stepW = (colW - 6) / 16.f;
            if (track.kind == TrackKind::Drums) {
                const DrumPattern& p = track.drums[scene];
                for (int lane = 0; lane < kDrumLanes; ++lane)
                    for (int step = 0; step < kSteps; ++step)
                        if (p.level[lane][step] > 0.f) {
                            fill(r, x + 3 + static_cast<int>(step * stepW), y + 4 + lane * 4, 2, 2,
                                 faded(kText, 90 + static_cast<int>(165 * p.level[lane][step])));
                        }
            } else {
                const NotePattern& p = track.notes[scene];
                int low = 127, high = 0;
                for (const NoteEvent& n : p.notes)
                    if (n.velocity > 0.f) { low = n.pitch < low ? n.pitch : low; high = n.pitch > high ? n.pitch : high; }
                const int span = high - low < 7 ? 7 : high - low;
                for (const NoteEvent& n : p.notes) {
                    if (n.velocity <= 0.f) continue;
                    const int ny = y + 4 + (kRowH - 14) * (high - n.pitch) / span;
                    const int w = static_cast<int>(n.length * stepW) - 1;
                    fill(r, x + 3 + static_cast<int>(n.step * stepW), ny, w < 2 ? 2 : w, 2, kText);
                }
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

    // ---- the instrument list -----------------------------------------------------
    if (choosing_) {
        const int w = 300, h = 202, x = (kScreenW - w) / 2, y = 124;
        fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 200));
        box(r, x, y, w, h, kPanel, kLine);
        std::snprintf(text, sizeof text, "TRACK %d: PICK AN INSTRUMENT", tracks + 1);
        fonts.small.draw(r, x + 12.f, y + 12.f, text, kDim);
        const char* names[3] = {"DRUMS", "PHAZED", "TWIN ANALOG"};
        char synthAbout[40];
        std::snprintf(synthAbout, sizeof synthAbout, "the synth, %d presets", Phazed::presetCount());
        char twinAbout[48];
        std::snprintf(twinAbout, sizeof twinAbout, "two morphing oscillators, %d presets", Twin::presetCount());
        const char* about[3] = {"8 lanes, the built-in kit", synthAbout, twinAbout};
        for (int i = 0; i < 3; ++i) {
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
        hint("STICK", "move");
        hint("A", "open");
        hint("B", "empty");
        hint("HOLD R1", "copy by dragging");
        hint("START", project_.playing ? "stop" : "play song");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
