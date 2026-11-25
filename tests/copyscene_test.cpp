// COPY A SCENE: R1 on a scene's REPEATS box copies the whole scene, R1 on another row pastes
// it there (the SONG screen). The Session's side (Session::copyScene / pasteScene): every track's
// cells (bars, drum hits with every field, notes and chords, automation), the repeats and the
// steps; a track whose instrument changed since is skipped; the same scene twice is no step of
// undo; one step otherwise. The gesture through the real screen: tap to copy, tap to paste (the
// paste on R1's let go, so a panic on the way pastes nothing), the copy kept after a paste and
// cleared by R1 on the copied scene (G23),
// the AT END row appends, the cell brush as it was, the copied row brass-marked, the hints, and
// the engine hears the pasted scene: the song played beside the scene it came from.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

const SDL_Keycode A = SDLK_x, B = SDLK_z, L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2, DOWN = SDLK_DOWN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode UP = SDLK_UP, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const int kStep = 6000; // 120 BPM at 48 kHz

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().view = View::Song;
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    void settle() { frames(50); } // the burst of edits is over
    void undo() {
        key(SELECT, true);
        tap(B);
        key(SELECT, false);
        frames(1);
    }
    void redo() {
        key(SELECT, true);
        tap(A);
        key(SELECT, false);
        frames(1);
    }
    std::string said() { return app.undoSaid(); }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    TrackData& track(int t) { return project().tracks[t]; }
    // The scene played from the top, `steps` of it, both sides.
    std::vector<float> play(int scene, int steps) {
        project().scene = scene;
        frames(30);
        session().togglePlay(false);
        std::vector<float> all, out(2 * 250);
        while (all.size() < static_cast<std::size_t>(2 * steps * kStep)) {
            engine->render(out.data(), 250);
            all.insert(all.end(), out.begin(), out.end());
        }
        session().togglePlay(false);
        frames(4);
        return all;
    }
};

// Is `words` drawn in the hints bar with the chip `key` just before it (its left edge where that chip's would be)?
bool hintKeyed(const Canvas& canvas, Canvas& scratch, const char* key, const char* words) {
    const Fonts& fonts = *canvas.fonts;
    const float textY = ui::kScreenH - ui::kHintsH + (ui::kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    const int chipY = ui::kScreenH - ui::kHintsH + 6; // the chip's top row, above its letters
    for (float x = 4.f; x < ui::kScreenW - 40.f; x += 0.0625f) {
        if (!inked(canvas, scratch, 0, x, textY, words, ui::kDim)) continue;
        const int left = static_cast<int>(x - 5.f - (ui::keyWidth(fonts, key) + 8.f));
        for (int at = left - 1; at <= left + 1; ++at)
            if (canvas.is(at, chipY, ui::kText) && !canvas.is(at - 1, chipY, ui::kText)) return true;
    }
    return false;
}

float peakOf(const std::vector<float>& out) {
    float peak = 0.f;
    for (float v : out) {
        const float at = v < 0.f ? -v : v;
        if (at > peak) peak = at;
    }
    return peak;
}

// Two scenes of one track the same, cell and automation, by the project's own equality.
bool sameCell(const TrackData& t, int a, int b) {
    return t.bars[a] == t.bars[b] && t.drums.same(a, t.drums, b) && t.notes.same(a, t.notes, b) && t.automation.same(a, t.automation, b);
}

// A scene with something in every corner: two bars, a hit with every field, a note and a chord,
// automation, three repeats, cut short of its bars.
void makeScene(Rig& rig, int scene) {
    Session& session = rig.session();
    TrackData& drums = rig.track(0);
    drums.bars[scene] = 2;
    DrumPattern& bar = drums.drums[scene][1];
    bar.level[2][9] = 0.75f;
    bar.chance[2][9] = 0.5f;
    bar.nudge[2][9] = 0.25f;
    bar.start[2][9] = 0.5f;
    bar.fx[2][9].pitch = -4;
    bar.fx[2][9].ratchet = 3;
    bar.fx[2][9].reverse = 1;
    bar.fx[2][9].cut = 2;
    NoteEvent note;
    note.step = 5;
    note.pitch = 64;
    note.length = 2.5f;
    note.nudge = -0.125f;
    note.velocity = 0.8f;
    note.ratchet = 2;
    TrackData& synth = rig.track(1);
    synth.bars[scene] = 1;
    synth.notes[scene][0].notes[7] = note;
    ChordEvent chord;
    chord.step = 8;
    chord.length = 4.f;
    chord.root = 48;
    chord.inversion = 1;
    chord.count = 3;
    chord.intervals[0] = 0;
    chord.intervals[1] = 4;
    chord.intervals[2] = 7;
    chord.velocity = 0.7f;
    std::strcpy(chord.name, "MAJ");
    chord.ratchet = 4;
    synth.notes[scene][0].chords[2] = chord;
    session.setAutomation(0, scene, 3, 20, 0.6f);
    session.setAutomation(1, scene, 5, 7, 0.4f);
    rig.project().repeats[scene] = 3;
    rig.project().steps[scene] = 20;
    session.sendDrumPattern(0, scene);
    session.sendNotePattern(1, scene);
    rig.settle();
}

void testCopyPasteScene() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.copiedScene() == 0);
    CHECK(session.pasteScene(2) == Session::SceneCopy::Done);
    for (int t = 0; t < 2; ++t) CHECK(sameCell(rig.track(t), 0, 2));
    CHECK(rig.project().repeats[2] == 3 && rig.project().steps[2] == 20);
    CHECK(rig.project().repeats[0] == 3 && rig.project().steps[0] == 20); // the scene copied from, untouched
    // Not blank, either: what was written is there.
    CHECK(rig.track(0).drums[2][1].level[2][9] == 0.75f && rig.track(0).drums[2][1].fx[2][9].ratchet == 3);
    CHECK(rig.track(1).notes[2][0].notes[7].pitch == 64 && std::string(rig.track(1).notes[2][0].chords[2].name) == "MAJ");
}

void testTheEngineHearsIt() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    rig.track(0).bars[0] = 1;
    rig.app.drums().setStep(0, 0, 0.9f);
    NoteEvent note;
    note.step = 0;
    note.pitch = 60;
    note.length = 1.f;
    note.velocity = 0.8f;
    rig.track(1).notes[0][0].notes[0] = note;
    session.sendDrumPattern(0, 0);
    session.sendNotePattern(1, 0);
    rig.settle();
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.pasteScene(1) == Session::SceneCopy::Done);
    // The drums, bit for bit...
    session.setMuted(1, true);
    rig.frames(30);
    CHECK(rig.play(0, 16) == rig.play(1, 16));
    // ...and the synth's note sounds on the pasted scene (its chorus runs on, so two plays are never to the bit).
    session.setMuted(1, false);
    session.setMuted(0, true);
    rig.frames(30);
    CHECK(peakOf(rig.play(1, 16)) > 0.05f);
}

void testOneUndoStep() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    // Round values, exactly as a song file keeps them.
    rig.track(0).drums[0][0].level[0][0] = 0.5f;
    rig.track(1).notes[0][0].notes[1].velocity = 0.5f;
    rig.track(1).notes[0][0].notes[1].step = 2;
    rig.track(1).notes[0][0].notes[1].pitch = 62;
    session.setAutomation(0, 0, 1, 3, 0.5f);
    session.sendDrumPattern(0, 0);
    session.sendNotePattern(1, 0);
    rig.settle();
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.pasteScene(3) == Session::SceneCopy::Done);
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.5f && rig.project().repeats[3] == 3);
    CHECK(rig.track(0).automation[3][1][3] == 0.5f);
    rig.undo();
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.f); // the target as it was: nothing
    CHECK(rig.track(1).notes[3][0].notes[1].velocity == 0.f);
    CHECK(rig.track(0).automation[3][1][3] < 0.f);
    CHECK(rig.project().repeats[3] == 1 && rig.project().steps[3] == 0 && rig.track(0).bars[3] == 1);
    CHECK(rig.track(0).drums[0][0].level[0][0] == 0.5f);  // the scene copied from, untouched
    rig.redo();
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.5f && rig.project().repeats[3] == 3);
    bool back = false; // (in whatever slot undo put it back in)
    for (const NoteEvent& n : rig.track(1).notes[3][0].notes)
        if (n.velocity == 0.5f && n.pitch == 62 && n.step == 2) back = true;
    CHECK(back);
    CHECK(rig.track(0).automation[3][1][3] == 0.5f);
}

void testTheSameIsNoStep() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.pasteScene(1) == Session::SceneCopy::Done);
    CHECK(session.copiedScene() == 0); // kept for another paste (G23, 2026-11-25: a paste spent it)
    // An edit, then a paste that changes nothing: undo takes the edit back, not the paste.
    session.setAutomation(0, 5, 0, 0, 0.5f);
    rig.settle();
    CHECK(session.copyScene(0) == Session::SceneCopy::Done); // (a fresh copy: the same, so far)
    CHECK(session.pasteScene(1) == Session::SceneCopy::Same);
    CHECK(session.pasteScene(0) == Session::SceneCopy::Same); // onto itself, too
    rig.undo();
    CHECK(rig.track(0).automation[5][0][0] < 0.f);
    CHECK(sameCell(rig.track(0), 0, 1)); // the paste before stands: the no-op pastes stepped nowhere
}

void testRefusals() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.pasteScene(0) == Session::SceneCopy::NothingCopied && session.copiedScene() < 0);
    CHECK(session.copyScene(-1) == Session::SceneCopy::NoScene && session.copyScene(kMaxScenes) == Session::SceneCopy::NoScene);
    CHECK(session.pasteScene(kMaxScenes) == Session::SceneCopy::NoScene && session.copiedScene() < 0);
    // A track whose instrument changed since the copy keeps its own cell; the rest paste.
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    CHECK(session.setTrackKind(0, TrackKind::Phazed));
    CHECK(session.pasteScene(2) == Session::SceneCopy::Done);
    CHECK(rig.track(0).drums[2][0].level[2][9] == 0.f && rig.track(0).bars[2] == 1); // skipped: drums will not go on a synth
    CHECK(sameCell(rig.track(1), 0, 2));                                            // the rest pasted
    CHECK(rig.project().repeats[2] == 3 && rig.project().steps[2] == 20);
}

void testScenePasteAcrossChangedSynths() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    // The synth changed since: its notes come over anyway, as a cell's do.
    CHECK(session.setTrackKind(1, TrackKind::Fm6));
    CHECK(session.pasteScene(2) == Session::SceneCopy::Done);
    CHECK(rig.track(1).notes[2][0].notes[7].pitch == 64 && rig.track(1).notes[2][0].chords[2].root == 48 && rig.track(1).bars[2] == 1);
    CHECK(rig.track(1).automation[2][5][7] == 0.4f); // its automation too (setTrackKind cleared scene 0's own lanes)
    // ...but changed the other way, drums onto it stay skipped.
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    CHECK(session.setTrackKind(1, TrackKind::Drums));
    CHECK(session.pasteScene(3) == Session::SceneCopy::Done);
    CHECK(rig.track(1).drums[3][0].empty() && rig.track(1).bars[3] == 1);
}

void testTheGesture() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    // R1 on a scene's box: copied, and said.
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0 && rig.said() == "SCENE 1 COPIED");
    // R1 on another row: pasted there, on R1's let go, and said.
    song.moveCursorTo(1, -1);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.f); // not yet: on its let go
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.9f && rig.said() == "PASTED ON SCENE 2");
    CHECK(session.copiedScene() == 0); // kept for another paste (G23, 2026-11-25: a paste spent it)
    // The copied scene again: the copy is cleared, not pasted over itself (G23: it copied it again).
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() < 0 && rig.said() == "COPY CLEARED");
    rig.tap(R1); // ...and R1 again copies it afresh
    CHECK(session.copiedScene() == 0 && rig.said() == "SCENE 1 COPIED");
    // The AT END row pastes too: the copy becomes the song's new last scene.
    song.moveCursorTo(song.songEndRow(), -1);
    CHECK(song.songEndRow() == 2);
    rig.tap(R1);
    CHECK(rig.track(0).drums[2][0].level[0][0] == 0.9f && rig.said() == "PASTED ON SCENE 3");
    CHECK(song.songEndRow() == 3);
    CHECK(session.copiedScene() == 0); // (still there: the cell work below proves the scene clipboard untouched)
    // R1 on a cell copies it, as R1 on a repeat box copies its scene; R1 on
    // another cell pastes there, on its let go. The scene clipboard is untouched.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    song.moveCursorTo(3, 0);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.f); // not yet: on its let go
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.9f && rig.said() == "CELL PASTED");
    CHECK(session.copiedScene() == 0);
    // Pasted, the copy stays (G23): R1 on another cell pastes there again, on its let go.
    song.moveCursorTo(4, 0);
    rig.key(R1, true);
    rig.frames(1);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(0).drums[4][0].level[0][0] == 0.9f && rig.said() == "CELL PASTED");
    // ...and R1 on the source clears it.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "COPY CLEARED");
    // Each paste is a step of undo of its own: one takes back the latest paste, another the one before.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    song.moveCursorTo(4, 0);
    rig.tap(R1);
    CHECK(rig.track(0).drums[4][0].level[0][0] == 0.9f);
    rig.undo();
    CHECK(rig.track(0).drums[4][0].level[0][0] == 0.9f && rig.track(0).drums[0][0].level[0][0] == 0.9f);
    rig.undo();
    CHECK(rig.track(0).drums[4][0].level[0][0] == 0.f && rig.track(0).drums[0][0].level[0][0] == 0.9f);
}

void testSceneTempo() {
    Rig rig;
    Session& session = rig.session();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    rig.track(0).drums[1][0].level[0][0] = 0.9f;
    session.setTempo(120.f);
    session.setSceneTempo(1, 240.f);
    CHECK(rig.project().sceneTempo[0] == 0.f && rig.project().sceneTempo[1] == 240.f);
    // The song body counts each scene at its own tempo (16 steps each here).
    CHECK(songFrames(rig.project(), 48000.f) == 16 * 6000 + 16 * 3000);
    // ...and it plays that way: scene 0 in 96000 frames, scene 1 in 48000.
    rig.project().scene = 0;
    rig.frames(30);
    session.togglePlay(true);
    std::vector<float> out(2 * 1024);
    long at = 0;
    const auto renderTo = [&](int scene, long limit) {
        while (at < limit) {
            rig.engine->render(out.data(), 1024);
            at += 1024;
            if (rig.engine->playScene() == scene) return at;
        }
        return -1L;
    };
    const long s1 = renderTo(1, 200000);
    CHECK(s1 > 0 && std::abs(s1 - 96000) < 2048);
    const long back = renderTo(0, s1 + 100000);
    CHECK(back > 0 && std::abs(back - s1 - 48000) < 2048);
    session.togglePlay(true); // stop
    // A scene with none of its own follows the song's.
    session.setTempo(60.f);
    rig.project().scene = 0;
    rig.frames(30);
    session.togglePlay(true);
    at = 0;
    CHECK(std::abs(renderTo(1, 300000) - 192000) < 2048);
    session.togglePlay(true);
}

// A cell copy carries its automation, and is a snapshot: edits after COPY never leak into the
// paste. Emptying a cell clears its automation too. The paste is its own undo step.
void testCellCopyCarriesAutomation() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    TrackData& track = rig.track(0);
    track.drums[0][0].level[0][0] = 0.9f;
    const int lane = autoLaneOfEffect(0, 0);
    session.setAutomation(0, 0, lane, 3, 0.5f);
    rig.settle();
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    track.drums[0][0].level[1][1] = 0.8f; // edited after COPY: not in the paste
    session.setAutomation(0, 0, lane, 4, 0.7f);
    song.moveCursorTo(1, 0);
    rig.key(R1, true);
    rig.frames(1);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.9f && rig.track(0).drums[1][0].level[1][1] == 0.f);
    CHECK(rig.track(0).automation[1][lane][3] == 0.5f && rig.track(0).automation[1][lane][4] < 0.f);
    CHECK(rig.said() == "CELL PASTED");
    // The paste was its own step: undo takes back the paste, not the source edits.
    rig.settle();
    rig.undo();
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.f && !session.hasAutomation(0, 1, lane));
    CHECK(rig.track(0).drums[0][0].level[1][1] == 0.8f && rig.track(0).automation[0][lane][4] == 0.7f);
    // Emptying clears automation too.
    song.moveCursorTo(0, 0);
    rig.tap(B);
    CHECK(!session.hasAutomation(0, 0, lane));
}

// A scene copied, then a track deleted: the paste stays aligned with the tracks it came from.
void testScenePasteAfterTrackDelete() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    CHECK(session.addTrack(TrackKind::Drums));
    rig.track(1).drums[0][0].level[1][1] = 0.8f;
    rig.settle();
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0 && rig.said() == "SCENE 1 COPIED");
    CHECK(session.removeTrack(0)); // track 1 (the 0.8 hit) moves to 0
    song.moveCursorTo(1, -1);
    rig.tap(R1);
    CHECK(rig.track(0).drums[1][0].level[1][1] == 0.8f && rig.track(0).drums[1][0].level[0][0] == 0.f);
    CHECK(rig.said() == "PASTED ON SCENE 2");
}

// A scene copied, then a track duplicated: the paste stays aligned too (the copy, like its track,
// is its source's).
void testScenePasteAfterTrackDuplicate() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    CHECK(session.addTrack(TrackKind::Drums));
    rig.track(1).drums[0][0].level[4][4] = 0.8f;
    rig.settle();
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0);
    CHECK(session.duplicateTrack(0) == 1); // [old0, copy, old1]
    song.moveCursorTo(1, -1);
    rig.tap(R1);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.9f);
    CHECK(rig.track(1).drums[1][0].level[0][0] == 0.9f);
    CHECK(rig.track(2).drums[1][0].level[4][4] == 0.8f);
}

void testSceneTempoUI() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.setTempo(120.f);
    rig.track(0).drums[0][0].level[0][0] = 0.9f; // scene 0 exists: the cursor stands on it, not AT END
    song.moveCursorTo(0, -1);
    rig.key(L1, true);
    rig.tap(UP);
    CHECK(rig.project().sceneTempo[0] == 121.f && rig.said() == "SCENE TEMPO 121");
    rig.tap(UP);
    CHECK(rig.project().sceneTempo[0] == 122.f);
    rig.tap(DOWN);
    CHECK(rig.project().sceneTempo[0] == 121.f);
    // B with L1 held puts tempo and meter back.
    rig.tap(B);
    CHECK(rig.project().sceneTempo[0] == 0.f && rig.said() == "TEMPO FOLLOWS THE SONG, 16 STEPS");
    // The hints while L1 is held fit the bar.
    Canvas canvas;
    CHECK(canvas.ok);
    if (canvas.ok) {
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
    }
    rig.key(L1, false);
    // Left and right are the steps in its bars.
    rig.key(L1, true);
    rig.tap(LEFT);
    CHECK(rig.project().meterOf(0) == 15 && rig.said() == "15 STEPS IN A BAR");
    rig.tap(RIGHT);
    CHECK(rig.project().meterOf(0) == 16);
    rig.key(L1, false);
    session.setSceneMeter(0, 99);
    CHECK(rig.project().meterOf(0) == 16);
    session.setSceneMeter(0, -5);
    CHECK(rig.project().meterOf(0) == 1);
    session.setSceneMeter(0, 16);
    // ...and B alone is still repeats back to 1.
    song.moveCursorTo(0, -1);
    rig.tap(B);
    CHECK(rig.project().repeats[0] == 1);
}

void testSceneTempoFile() {
    Project song;
    song.tracks[0].kind = TrackKind::Drums;
    song.tracks[0].drums[0][0].level[0][0] = 0.9f;
    song.sceneTempo[1] = 150.f;
    const std::string text = songText(song);
    CHECK(text.find("\nstempi 0 150") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(back->sceneTempo[0] == 0.f && back->sceneTempo[1] == 150.f);
    // A song from before has no such line: every scene follows.
    Project plain;
    plain.tracks[0].kind = TrackKind::Drums;
    const std::string old = songText(plain);
    CHECK(old.find("stempi") == std::string::npos);
    std::unique_ptr<Project> older(new Project);
    older->sceneTempo[1] = 150.f;
    CHECK(parseSong(old, *older) && older->sceneTempo[1] == 0.f);
}

void testSceneMeter() {
    Rig rig;
    Session& session = rig.session();
    session.setSceneMeter(0, 12);
    session.setSceneMeter(1, 16);
    CHECK(rig.project().meterOf(0) == 12 && rig.project().sceneSteps(0) == 12);
    CHECK(rig.project().barSteps(0, 0) == 12 && rig.project().barSteps(0, 1) == 0);
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    rig.track(0).drums[0][0].level[0][14] = 0.9f; // past the meter: kept, not played
    rig.track(0).drums[1][0].level[0][0] = 0.9f;
    session.setTempo(120.f);
    // Scene 0 plays 12 steps, then scene 1 plays 16.
    rig.project().scene = 0;
    rig.frames(30);
    session.togglePlay(true);
    std::vector<float> out(2 * 1024);
    long at = 0;
    int topStep = -1;
    const auto renderTo = [&](int scene, long limit) {
        while (at < limit) {
            rig.engine->render(out.data(), 1024);
            at += 1024;
            if (rig.engine->playScene() == 0) topStep = rig.engine->playStep() > topStep ? rig.engine->playStep() : topStep;
            if (rig.engine->playScene() == scene) return at;
        }
        return -1L;
    };
    const long s1 = renderTo(1, 200000);
    CHECK(s1 > 0 && std::abs(s1 - 12 * 6000) < 2048);
    CHECK(topStep == 11); // never past the meter's last step
    const long back = renderTo(0, s1 + 200000);
    CHECK(back > 0 && std::abs(back - s1 - 16 * 6000) < 2048);
    session.togglePlay(true); // stop
    CHECK(rig.track(0).drums[0][0].level[0][14] == 0.9f); // ...and the unplayed hit is still there
}

void testSceneMeterCarry() {
    Rig rig;
    Session& session = rig.session();
    makeScene(rig, 0);
    session.setSceneMeter(0, 12);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.pasteScene(2) == Session::SceneCopy::Done);
    CHECK(rig.project().meterOf(2) == 12); // pasted with the scene
    CHECK(session.clearScene(2) == Session::SceneClear::Done && rig.project().meterOf(2) == 16); // cleared with it
    session.setSceneMeter(3, 12);
    CHECK(session.clearScene(3) == Session::SceneClear::Done); // a meter alone is something to clear
    CHECK(rig.project().meterOf(3) == 16);
}

void testSceneMeterFile() {
    Project song;
    song.tracks[0].kind = TrackKind::Drums;
    song.tracks[0].drums[0][0].level[0][0] = 0.9f;
    song.sceneMeter[1] = 12;
    const std::string text = songText(song);
    CHECK(text.find("\nmeter 16 12") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(back->meterOf(0) == 16 && back->meterOf(1) == 12);
    Project plain;
    plain.tracks[0].kind = TrackKind::Drums;
    CHECK(songText(plain).find("meter") == std::string::npos);
    std::unique_ptr<Project> older(new Project);
    CHECK(parseSong(songText(plain), *older) && older->meterOf(1) == 16);
}

void testCellCopyCleared() {
    // A new song clears the copy: its cells are gone with the old one.
    Rig rig;
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    rig.tap(SELECT); // the list of songs
    rig.tap(A);      // its first row: a new song
    song.moveCursorTo(1, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED"); // copies: nothing pastes from the old song
    // A scene put in or taken out clears it too: its cells moved.
    Rig moved;
    SongScreen& scrolled = moved.app.song();
    moved.track(0).drums[0][0].level[0][0] = 0.9f;
    scrolled.moveCursorTo(0, 0);
    moved.tap(R1);
    CHECK(moved.said() == "CELL COPIED");
    scrolled.scenesMoved(0, 1);
    scrolled.moveCursorTo(2, 0);
    moved.tap(R1);
    CHECK(moved.said() == "CELL COPIED");
}

void testCellCopyRefusals() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    // A drum cell does not paste on a synth track, and the copy is kept.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    song.moveCursorTo(0, 1);
    rig.tap(R1);
    CHECK(rig.said() == "NO PASTE: OTHER KIND");
    song.moveCursorTo(1, 0);
    rig.tap(R1);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.9f && rig.said() == "CELL PASTED");
}

void testCellCopyAcrossSynths() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.addTrack(TrackKind::Phazed);
    session.addTrack(TrackKind::Fm6);
    rig.settle();
    NoteEvent note;
    note.step = 5;
    note.pitch = 64;
    note.length = 2.f;
    note.velocity = 0.8f;
    rig.track(1).notes[0][0].notes[3] = note;
    // A piano roll pastes on another synth: its notes go with it.
    song.moveCursorTo(0, 1);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    song.moveCursorTo(0, 2);
    rig.key(R1, true);
    rig.frames(1);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(2).notes[0][0].notes[3].pitch == 64 && rig.said() == "CELL PASTED");
    // The copy stays (G23): R1 there again pastes again, and does not copy that cell.
    rig.tap(R1);
    CHECK(rig.said() == "CELL PASTED");
    // A synth cell does not paste on drums either, and the copy is kept.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "NO PASTE: OTHER KIND");
    song.moveCursorTo(1, 1);
    rig.tap(R1);
    CHECK(rig.track(1).notes[1][0].notes[3].pitch == 64 && rig.said() == "CELL PASTED");
    // A panic begun with R1 held for a paste: let go, nothing pastes. (The synth cell's copy is cleared
    // on its own cell first, and the drum cell copied.)
    song.moveCursorTo(0, 1);
    rig.tap(R1);
    CHECK(rig.said() == "COPY CLEARED");
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    song.moveCursorTo(2, 0);
    rig.key(R1, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(L2, true);
    rig.frames(1);
    rig.key(R2, true);
    rig.frames(2);
    rig.key(R1, false);
    rig.frames(1);
    rig.key(L1, false);
    rig.key(L2, false);
    rig.key(R2, false);
    rig.frames(2);
    CHECK(rig.track(0).drums[2][0].level[0][0] == 0.f && session.view == View::Song);
}

void testTheGesturePanic() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0);
    // A panic begun with R1 held for a paste: let go, nothing pastes and the copy is kept.
    song.moveCursorTo(1, -1);
    rig.key(R1, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(L2, true);
    rig.frames(1);
    rig.key(R2, true);
    rig.frames(2);
    rig.key(R1, false);
    rig.frames(1);
    rig.key(L1, false);
    rig.key(L2, false);
    rig.key(R2, false);
    rig.frames(2);
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.f && session.copiedScene() == 0 && session.view == View::Song);
}

void testTheMarkAndHints() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    const auto draw = [&] {
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return ui::gHintsOver == 0.f;
    };
    song.moveCursorTo(0, -1);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "copy"));
    CHECK(hintKeyed(canvas, scratch, "HOLD B", "empty")); // (the scene is emptied, one step of undo: G10)
    rig.tap(R1);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "clear")); // on the copied scene, R1 clears it (G23)
    song.moveCursorTo(1, -1);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "paste"));
    const SDL_Rect box = song.repeatBox(0);
    CHECK(box.w > 0 && canvas.is(box.x + 1, box.y + 1, ui::kBrass)); // the copied row, brass-marked
    const SDL_Rect other = song.repeatBox(1);
    CHECK(canvas.is(other.x + 1, other.y + 1, ui::kBg)); // the rest, not
}

// A copy stays after a paste (gesture audit G23, 2026-11-25; commit e7b847d's "a copy clears on paste" is
// reversed): a cell or a scene goes on to the next with one R1 each, its brass mark stays on where it came
// from, and R1 THERE clears it ("clear" in the bar). A bar's copy stays the same way (copybar_test).
void testTheCopyStays() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    const auto draw = [&] {
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return ui::gHintsOver == 0.f;
    };
    // A cell into the next three scenes: one R1 to copy, then one R1 each.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED");
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "clear"));
    for (int scene = 1; scene <= 3; ++scene) {
        song.moveCursorTo(scene, 0);
        CHECK(draw() && hintKeyed(canvas, scratch, "R1", "paste"));
        rig.tap(R1);
        CHECK(rig.track(0).drums[scene][0].level[0][0] == 0.9f && rig.said() == "CELL PASTED");
    }
    CHECK(draw());
    const SDL_Rect cell = song.cellBox(0, 0);
    CHECK(cell.w > 0 && canvas.is(cell.x + 1, cell.y + 1, ui::kBrass)); // still marked: still copied
    // R1 on the copied cell clears it: no mark, and R1 elsewhere copies again.
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(rig.said() == "COPY CLEARED");
    song.moveCursorTo(4, 0);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "copy") && !canvas.is(cell.x + 1, cell.y + 1, ui::kBrass));
    rig.tap(R1);
    CHECK(rig.said() == "CELL COPIED" && rig.track(0).drums[4][0].empty());
    rig.tap(R1);
    CHECK(rig.said() == "COPY CLEARED");
    // A scene the same: copied once, pasted twice, the mark stays; R1 on it clears.
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0);
    for (int scene = 5; scene <= 6; ++scene) {
        song.moveCursorTo(scene, -1);
        rig.tap(R1);
        CHECK(rig.track(0).drums[scene][0].level[0][0] == 0.9f && session.copiedScene() == 0);
    }
    CHECK(draw());
    const SDL_Rect row = song.repeatBox(0);
    CHECK(row.w > 0 && canvas.is(row.x + 1, row.y + 1, ui::kBrass));
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() < 0 && rig.said() == "COPY CLEARED");
    song.moveCursorTo(1, -1);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "copy") && !canvas.is(row.x + 1, row.y + 1, ui::kBrass));
    // A panic begun with R1 on the copied scene: the copy is put back (a clear on the way is not meant).
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(session.copiedScene() < 0); // (cleared on the press)
    rig.key(L1, true);
    rig.frames(1);
    rig.key(L2, true);
    rig.frames(1);
    rig.key(R2, true);
    rig.frames(2);
    rig.key(R1, false);
    rig.key(L1, false);
    rig.key(L2, false);
    rig.key(R2, false);
    rig.frames(2);
    CHECK(session.copiedScene() == 0);
}

void testEmptyCellBars() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    rig.track(1).bars[0] = 2; // empty, two bars long
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    const SDL_Rect c = song.cellBox(1, 0);
    CHECK(c.w > 0);
    // A faint line where its second bar starts (16 steps a bar, as the roll draws them).
    const float stepW = static_cast<float>(c.w - 6) / (16.f * rig.project().sceneBars(0));
    CHECK(canvas.is(c.x + 3 + static_cast<int>(16 * stepW), c.y + c.h / 2, ui::kDim));
    // ...and none in an empty cell one bar long.
    const SDL_Rect one = song.cellBox(1, 1);
    CHECK(one.w > 0 && !canvas.is(one.x + one.w / 2, one.y + one.h / 2, ui::kDim));
}

void testTheWordsFit() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    // Every line said fits the App's box (260 wide, 6 either side) in the lettering it is held to at XL: LARGE.
    using SC = Session::SceneCopy;
    for (SC answer : {SC::Done, SC::Same, SC::NothingCopied, SC::NoScene})
        for (bool paste : {false, true}) {
            const std::string words = Session::sceneCopyWords(answer, paste, kMaxScenes - 1);
            CHECK(!words.empty() && canvas.fonts->medium.width(words.c_str()) <= 248.f);
        }
    using SL = Session::SceneClear;
    for (SL answer : {SL::Done, SL::AlreadyEmpty, SL::NoScene}) {
        const std::string words = Session::sceneClearWords(answer, kMaxScenes - 1);
        CHECK(!words.empty() && canvas.fonts->medium.width(words.c_str()) <= 248.f);
    }
}

void testClearScene() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    makeScene(rig, 1);
    CHECK(session.clearScene(1) == Session::SceneClear::Done);
    // (The pieces stay, blanked, for undo: what is gone is checked, not them.)
    for (int t = 0; t < 2; ++t) CHECK(rig.track(t).bars[1] == 1);
    CHECK(rig.project().repeats[1] == 1 && rig.project().steps[1] == 0);
    CHECK(rig.track(0).drums[1][1].level[2][9] == 0.f && rig.track(0).automation[1][3][20] < 0.f);
    CHECK(rig.track(0).drums[0][1].level[2][9] == 0.75f); // the other scene, untouched
    rig.undo();
    CHECK(rig.project().repeats[1] == 3 && rig.project().steps[1] == 20 && rig.track(0).bars[1] == 2);
    CHECK(rig.track(0).drums[1][1].level[2][9] == 0.75f);
    bool noteBack = false; // (in whatever slot undo put it back in)
    for (const NoteEvent& n : rig.track(1).notes[1][0].notes)
        if (n.pitch == 64 && n.step == 5) noteBack = true;
    CHECK(noteBack);
    CHECK(rig.track(0).automation[1][3][20] == 0.6f);
    rig.redo();
    CHECK(rig.project().repeats[1] == 1 && rig.track(0).drums[1][1].level[2][9] == 0.f);
    CHECK(peakOf(rig.play(1, 32)) == 0.f); // the engine hears nothing there (after the undo checks: playing steps undo)
    // The clipboard survives a clear: copy, clear, paste.
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    CHECK(session.clearScene(0) == Session::SceneClear::Done);
    CHECK(session.pasteScene(0) == Session::SceneCopy::Done);
    CHECK(rig.project().repeats[0] == 3 && rig.track(0).drums[0][1].level[2][9] == 0.75f);
}

void testClearAlreadyEmpty() {
    Rig rig;
    Session& session = rig.session();
    rig.settle();
    const int steps = session.undoSteps();
    CHECK(session.clearScene(0) == Session::SceneClear::AlreadyEmpty);
    CHECK(session.clearScene(-1) == Session::SceneClear::NoScene && session.clearScene(kMaxScenes) == Session::SceneClear::NoScene);
    CHECK(session.undoSteps() == steps); // nothing stepped
}

void testClearGesture() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    makeScene(rig, 0);
    // Hold B on its box: red fills it, and at half a second it empties, said (G29: undo brings a scene back,
    // so it is the shorter hold; it was a second).
    CHECK(SongScreen::kSceneHoldMs == 500);
    song.moveCursorTo(0, -1);
    rig.key(B, true);
    rig.frames(20);
    {
        Canvas canvas;
        CHECK(canvas.ok);
        if (canvas.ok) {
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            const SDL_Rect box = song.repeatBox(0);
            const int fy = box.y + box.h - 3; // (the foot row: clear of the count)
            const int inner = box.w - 2;
            CHECK(!canvas.is(box.x + 1 + inner * 30 / 100, fy, ui::kBg) && canvas.is(box.x + 1 + inner * 90 / 100, fy, ui::kBg));
        }
    }
    rig.frames(30);
    rig.key(B, false);
    rig.frames(1);
    // (The pieces stay, blanked, for undo: what is gone is checked, not them.)
    CHECK(rig.project().repeats[0] == 1 && rig.project().steps[0] == 0 && rig.track(0).bars[0] == 1);
    CHECK(rig.track(0).drums[0][1].level[2][9] == 0.f && rig.track(0).automation[0][3][20] < 0.f);
    CHECK(rig.said() == "SCENE 1 EMPTIED");
    // A tap still only takes repeats back to 1.
    rig.project().repeats[2] = 4;
    rig.track(0).drums[2][0].level[0][0] = 0.9f;
    song.moveCursorTo(2, -1);
    rig.tap(B);
    CHECK(rig.project().repeats[2] == 1);
    CHECK(rig.track(0).drums[2][0].level[0][0] == 0.9f);
    // The AT END row: holding does nothing (still LOOP).
    const std::string before = rig.said();
    const int endRow = song.songEndRow();
    song.moveCursorTo(endRow, -1);
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    rig.frames(1);
    CHECK(session.view == View::Song && rig.project().songEnd == kSongLoop && song.songEndRow() == endRow && rig.said() == before);
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testCopyPasteScene", testCopyPasteScene}, {"testTheEngineHearsIt", testTheEngineHearsIt}, {"testOneUndoStep", testOneUndoStep},
        {"testTheSameIsNoStep", testTheSameIsNoStep}, {"testRefusals", testRefusals}, {"testScenePasteAcrossChangedSynths", testScenePasteAcrossChangedSynths}, {"testTheGesture", testTheGesture},
        {"testTheGesturePanic", testTheGesturePanic}, {"testTheMarkAndHints", testTheMarkAndHints}, {"testTheCopyStays", testTheCopyStays}, {"testEmptyCellBars", testEmptyCellBars}, {"testTheWordsFit", testTheWordsFit},
        {"testClearScene", testClearScene}, {"testClearAlreadyEmpty", testClearAlreadyEmpty}, {"testClearGesture", testClearGesture},
        {"testCellCopyRefusals", testCellCopyRefusals},
        {"testCellCopyAcrossSynths", testCellCopyAcrossSynths},
        {"testCellCopyCleared", testCellCopyCleared},
        {"testCellCopyCarriesAutomation", testCellCopyCarriesAutomation},
        {"testScenePasteAfterTrackDelete", testScenePasteAfterTrackDelete},
        {"testScenePasteAfterTrackDuplicate", testScenePasteAfterTrackDuplicate},
        {"testSceneTempo", testSceneTempo},
        {"testSceneTempoUI", testSceneTempoUI},
        {"testSceneTempoFile", testSceneTempoFile},
        {"testSceneMeter", testSceneMeter},
        {"testSceneMeterCarry", testSceneMeterCarry},
        {"testSceneMeterFile", testSceneMeterFile},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("copyscene_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
