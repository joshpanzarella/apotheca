// COPY A SCENE: R1 on a scene's REPEATS box copies the whole scene, R1 on another row pastes
// it there (the SONG screen). The Session's side (Session::copyScene / pasteScene): every track's
// cells (bars, drum hits with every field, notes and chords, automation), the repeats and the
// steps; a track whose instrument changed since is skipped; the same scene twice is no step of
// undo; one step otherwise. The gesture through the real screen: tap to copy, tap to paste (the
// paste on R1's let go, so a panic on the way pastes nothing), the same scene again only copies,
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

// Has a pixel of the full colour (the body of a letter, not its soft edge) the screen looked at
// must have it too.
bool inked(const Canvas& canvas, Canvas& scratch, int size, float x, float y, const char* text, SDL_Color colour) {
    const Font& font = size == 0 ? scratch.fonts->small : (size == 1 ? scratch.fonts->medium : scratch.fonts->large);
    const int x0 = std::max(0, static_cast<int>(x) - 2), x1 = std::min(ui::kScreenW, static_cast<int>(x + font.width(text)) + 3);
    const int y0 = std::max(0, static_cast<int>(y) - 4), y1 = std::min(ui::kScreenH, static_cast<int>(y) + 30);
    ui::fill(scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255}); // a ground no lettering is drawn in
    font.draw(scratch.r, x, y, text, colour);
    int body = 0;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            if (!scratch.is(px, py, colour)) continue;
            ++body;
            if (!canvas.is(px, py, colour)) return false;
        }
    return body >= 6;
}

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
    // An edit, then a paste that changes nothing: undo takes the edit back, not the paste.
    session.setAutomation(0, 5, 0, 0, 0.5f);
    rig.settle();
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
    CHECK(session.copiedScene() == 0); // kept: it pastes again
    // The same scene again: copied, not pasted over itself.
    song.moveCursorTo(0, -1);
    rig.tap(R1);
    CHECK(session.copiedScene() == 0 && rig.said() == "SCENE 1 COPIED");
    // The AT END row pastes too: the copy becomes the song's new last scene.
    song.moveCursorTo(song.songEndRow(), -1);
    CHECK(song.songEndRow() == 2);
    rig.tap(R1);
    CHECK(rig.track(0).drums[2][0].level[0][0] == 0.9f && rig.said() == "PASTED ON SCENE 3");
    CHECK(song.songEndRow() == 3);
    // R1 on a cell is still the brush, and it touches no clipboard.
    song.moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 3);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.track(0).drums[3][0].level[0][0] == 0.9f && session.copiedScene() == 0);
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
    rig.tap(R1);
    CHECK(draw() && hintKeyed(canvas, scratch, "R1", "paste"));
    song.moveCursorTo(1, -1);
    CHECK(draw());
    const SDL_Rect box = song.repeatBox(0);
    CHECK(box.w > 0 && canvas.is(box.x + 1, box.y + 1, ui::kBrass)); // the copied row, brass-marked
    const SDL_Rect other = song.repeatBox(1);
    CHECK(canvas.is(other.x + 1, other.y + 1, ui::kBg)); // the rest, not
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
        {"testTheSameIsNoStep", testTheSameIsNoStep}, {"testRefusals", testRefusals}, {"testTheGesture", testTheGesture},
        {"testTheGesturePanic", testTheGesturePanic}, {"testTheMarkAndHints", testTheMarkAndHints}, {"testTheWordsFit", testTheWordsFit},
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
