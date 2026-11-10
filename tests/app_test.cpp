// Tests for the whole app: the Song screen, the note screen, Phazed's
// screens and moving between them. No window and no sound card: buttons are
// pressed through the keyboard stand-in, and the engine is rendered by hand
// to hear what the screens told it.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"

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

const int kStep = 6000, kBar = 16 * kStep; // at 120 BPM, 48 kHz

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    std::vector<float> render(int audioFrames) {
        std::vector<float> out(2 * audioFrames);
        engine->render(out.data(), audioFrames);
        return out;
    }
    float listen(int audioFrames = 4000) { return peak(render(audioFrames)); }
    static float peak(const std::vector<float>& v, int from = 0, int to = -1) {
        float p = 0.f;
        const std::size_t end = to < 0 ? v.size() : static_cast<std::size_t>(2 * to);
        for (std::size_t i = 2 * static_cast<std::size_t>(from); i < end; ++i) p = std::fabs(v[i]) > p ? std::fabs(v[i]) : p;
        return p;
    }
    Project& project() { return app.project(); }
    View view() { return app.session().view; }
};

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;
const SDL_Keycode STICK_UP = SDLK_i, STICK_DOWN = SDLK_k, STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// A rig on the Song screen with a Phazed track added as track 2.
void addPhazed(Rig& rig) {
    rig.app.session().view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A); // + TRACK
    rig.tap(A); // PHAZED is the one offered first
}

// ---- moving between screens ------------------------------------------------------

void testTabs() {
    Rig rig;
    CHECK(rig.view() == View::Pattern);
    rig.tap(L2);
    CHECK(rig.view() == View::Song);
    rig.tap(L2);
    CHECK(rig.view() == View::Song); // the row of tabs has ends
    rig.tap(R2, 2);
    CHECK(rig.view() == View::Device);
    rig.tap(R2);
    CHECK(rig.view() == View::Device);
    // Not while shaping something: a slip of the finger must not change screens.
    rig.tap(L2);
    rig.key(A, true);
    rig.tap(L2);
    CHECK(rig.view() == View::Pattern);
    rig.key(A, false);
    // START works on the kit's page too.
    rig.tap(R2);
    rig.tap(START);
    CHECK(rig.project().playing);
}

// ---- the Song screen ------------------------------------------------------------------

void testANewProjectIsOneDrumTrack() {
    Rig rig;
    CHECK(rig.project().trackCount() == 1);
    CHECK(rig.project().tracks[0].kind == TrackKind::Drums);
    CHECK(rig.project().sceneCount() == 1);
}

void testAddingTracks() {
    Rig rig;
    rig.tap(L2);
    rig.tap(RIGHT); // onto + TRACK
    CHECK(rig.app.song().cursorColumn() == 1);
    rig.tap(A);
    CHECK(rig.app.song().choosing());
    rig.tap(B); // cancel
    CHECK(!rig.app.song().choosing() && rig.project().trackCount() == 1);
    rig.tap(A);
    rig.tap(A); // Phazed, offered first
    CHECK(rig.project().trackCount() == 2);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed);
    CHECK(rig.app.song().cursorColumn() == 1); // the cursor is on the new track
    // The other choice is a second drum track.
    rig.tap(RIGHT);
    rig.tap(A);
    rig.tap(UP);
    rig.tap(A);
    CHECK(rig.project().trackCount() == 3 && rig.project().tracks[2].kind == TrackKind::Drums);

    // Both make sound, each from its own track.
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(A); // open the Phazed track
    CHECK(rig.view() == View::Pattern && rig.project().track == 1);
    rig.tap(A); // a note
    rig.listen();
    CHECK(rig.engine->trackPeak(1) > 0.f);
    rig.tap(L2);
    rig.app.song().moveCursorTo(0, 2);
    rig.tap(A);
    rig.tap(A); // a kick on the second drum track
    rig.listen();
    CHECK(rig.engine->trackPeak(2) > 0.05f && rig.engine->trackPeak(0) == 0.f);

    // Twelve is the limit, and then there is no + TRACK to land on.
    rig.tap(L2);
    for (int i = 0; i < 20; ++i) {
        rig.app.song().moveCursorTo(0, rig.project().trackCount());
        rig.tap(A);
        rig.tap(A);
        if (rig.app.song().choosing()) rig.tap(B);
        if (rig.view() != View::Song) rig.tap(L2);
    }
    CHECK(rig.project().trackCount() == kNumTracks);
    rig.app.song().moveCursorTo(0, 99);
    CHECK(rig.app.song().cursorColumn() == kNumTracks - 1);
}

void testOpeningACellWorksOnThatScene() {
    Rig rig;
    rig.tap(L2);
    rig.tap(DOWN, 2); // scene 3
    rig.tap(A);
    CHECK(rig.view() == View::Pattern && rig.project().scene == 2 && rig.project().track == 0);
    rig.tap(A); // a kick on step 1 of scene 3
    CHECK(rig.project().tracks[0].drums[2].level[0][0] > 0.f);
    CHECK(rig.project().tracks[0].drums[0].empty());
    CHECK(rig.project().sceneCount() == 3);
    rig.listen(40000);
    // START on the Pattern screen loops the scene being worked on.
    rig.tap(START);
    for (int bar = 0; bar < 3; ++bar) {
        CHECK(rig.listen(2000) > 0.05f);
        rig.listen(kBar - 2000);
    }
    CHECK(rig.engine->playScene() == 2);
}

void testRepeatCounts() {
    Rig rig;
    rig.tap(L2);
    rig.tap(LEFT); // the repeat count
    CHECK(rig.app.song().cursorColumn() == -1);
    CHECK(rig.project().repeats[0] == 1);
    rig.tap(A);
    CHECK(rig.project().repeats[0] == 2);
    rig.tap(A);
    rig.tap(A);
    CHECK(rig.project().repeats[0] == 8);
    rig.tap(A);
    CHECK(rig.project().repeats[0] == 1);
    // Hold A and step it: exact, and letting go is not another tap.
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(rig.project().repeats[0] == 3);
    // Hold A and push the stick: fast, and it stops at sixteen and at one.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(60);
    rig.key(STICK_UP, false);
    rig.key(A, false);
    CHECK(rig.project().repeats[0] == kMaxRepeats);
    rig.key(A, true);
    rig.key(STICK_DOWN, true);
    rig.frames(4);
    rig.key(STICK_DOWN, false);
    rig.key(A, false);
    CHECK(rig.project().repeats[0] < kMaxRepeats && rig.project().repeats[0] > 8);
    rig.tap(B);
    CHECK(rig.project().repeats[0] == 1);
    CHECK(rig.app.song().cursorScene() == 0); // none of that moved the cursor
    // Each scene has its own.
    rig.tap(DOWN);
    rig.tap(A);
    CHECK(rig.project().repeats[1] == 2 && rig.project().repeats[0] == 1);
}

// Scene n gets one rim hit on step n + 1, so a bar tells which scene it was.
void markScenes(Rig& rig, int scenes) {
    for (int scene = 0; scene < scenes; ++scene) {
        rig.project().scene = scene;
        rig.app.drums().setStep(6, scene, 1.f);
    }
    rig.project().scene = 0;
    rig.frames(1); // the song's length reaches the engine
}
int sceneHeard(Rig& rig) {
    const std::vector<float> bar = rig.render(kBar);
    int found = -1, count = 0;
    for (int step = 0; step < 16; ++step) {
        if (Rig::peak(bar, step * kStep, step * kStep + 400) > 0.05f) { found = step; ++count; }
    }
    return count == 1 ? found : -1 - count;
}

void testStartOnTheSongScreenPlaysTheSong() {
    Rig rig;
    markScenes(rig, 3);
    rig.tap(L2);
    rig.tap(LEFT);
    rig.tap(A); // scene 1 plays twice
    rig.tap(RIGHT);
    rig.tap(START);
    const int expected[] = {0, 0, 1, 2, 0, 0, 1};
    for (int scene : expected) CHECK(sceneHeard(rig) == scene);
    rig.tap(START);
    CHECK(!rig.project().playing);
    // From the cursor's scene.
    rig.listen(20000);
    rig.tap(DOWN, 2);
    rig.tap(START);
    CHECK(sceneHeard(rig) == 2);
    CHECK(sceneHeard(rig) == 0);
}

void testCopyingABarByDragging() {
    Rig rig;
    rig.app.drums().setStep(0, 0, 0.9f);
    rig.app.drums().setStep(2, 5, 0.4f);
    rig.app.drums().setNudge(2, 5, 0.25f);
    rig.app.drums().setChance(2, 5, 0.5f);
    addPhazed(rig);
    // The synth has a note in scene 3, which the drag will cross.
    rig.app.song().moveCursorTo(2, 1);
    rig.tap(A);
    rig.tap(A);
    rig.tap(L2);
    CHECK(!rig.project().tracks[1].notes[2].empty());
    rig.app.song().moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.tap(DOWN, 2);
    rig.tap(RIGHT); // across onto the synth: a drum bar cannot go there
    rig.key(R1, false);
    CHECK(!rig.project().tracks[1].notes[2].empty()); // and it did not wipe the synth's bar
    for (int scene = 1; scene <= 2; ++scene) {
        const DrumPattern& p = rig.project().tracks[0].drums[scene];
        CHECK(near(p.level[0][0], 0.9f) && near(p.level[2][5], 0.4f));
        CHECK(near(p.nudge[2][5], 0.25f) && near(p.chance[2][5], 0.5f));
    }
    CHECK(rig.project().tracks[0].drums[3].empty());
    // Moving with R1 let go copies nothing.
    rig.app.song().moveCursorTo(2, 0);
    rig.tap(DOWN);
    CHECK(rig.project().tracks[0].drums[3].empty());
    // The engine has the copies: the kick opens all three bars of the song.
    rig.frames(1);
    rig.tap(START);
    for (int bar = 0; bar < 3; ++bar) {
        CHECK(rig.listen(2000) > 0.1f);
        rig.listen(kBar - 2000);
    }

    // The same for a synth's bar.
    Rig synth;
    addPhazed(synth);
    synth.tap(A); // open it
    synth.tap(A); // a note
    synth.tap(L2);
    synth.key(R1, true);
    synth.tap(DOWN);
    synth.key(R1, false);
    CHECK(!synth.project().tracks[1].notes[1].empty());
    CHECK(synth.project().tracks[1].notes[1].notes[0].pitch == synth.project().tracks[1].notes[0].notes[0].pitch);
}

void testBEmptiesACell() {
    Rig rig;
    markScenes(rig, 2);
    rig.tap(L2);
    rig.tap(B); // scene 1
    CHECK(rig.project().tracks[0].drums[0].empty());
    CHECK(!rig.project().tracks[0].drums[1].empty());
    CHECK(rig.project().sceneCount() == 2);
    rig.frames(1);
    rig.tap(START);
    // The engine dropped it too: scene 1's bar is silent, scene 2's is not.
    CHECK(rig.listen(kBar) == 0.f);
    CHECK(sceneHeard(rig) == 1);
    rig.tap(START);
    // Emptying the last scene shortens the song.
    rig.tap(DOWN);
    rig.tap(B);
    CHECK(rig.project().sceneCount() == 1);
}

void testTheCopyBrushLifts() {
    Rig rig;
    rig.app.drums().setStep(0, 0, 0.9f);
    rig.tap(L2);
    rig.key(R1, true);
    rig.tap(DOWN);
    rig.key(R1, false); // let go on a drum cell
    rig.tap(DOWN, 3);
    CHECK(!rig.project().tracks[0].drums[1].empty());
    for (int scene = 2; scene <= 4; ++scene) CHECK(rig.project().tracks[0].drums[scene].empty());
    // R1 on a repeat count or on + TRACK picks nothing up.
    rig.app.song().moveCursorTo(0, -1);
    rig.key(R1, true);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    rig.key(R1, false);
    CHECK(rig.project().tracks[0].drums[2].empty());
}

void testTheSongScreenScrolls() {
    Rig rig;
    rig.tap(L2);
    rig.tap(DOWN, 40);
    CHECK(rig.app.song().cursorScene() == kMaxScenes - 1);
    rig.tap(UP, 40);
    CHECK(rig.app.song().cursorScene() == 0);
    rig.key(STICK_DOWN, true);
    rig.frames(40);
    rig.key(STICK_DOWN, false);
    CHECK(rig.app.song().cursorScene() == kMaxScenes - 1);
}

// ---- the note screen ---------------------------------------------------------------------

Rig* onNotes() {
    Rig* rig = new Rig;
    addPhazed(*rig);
    rig->tap(A); // open the Phazed track's first scene
    rig->listen(48000);
    return rig;
}

void testPlacingAndDeletingNotes() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    CHECK(rig->view() == View::Pattern && notes.cursorPitch() == 60 && notes.cursorStep() == 0);
    CHECK(notes.noteCount() == 0);
    rig->tap(A);
    CHECK(notes.noteCount() == 1);
    CHECK(notes.noteHere() && notes.noteHere()->length == 1 && near(notes.noteHere()->velocity, 0.8f));
    CHECK(rig->listen() > 0.f); // heard as it lands
    rig->tap(A);                // A on a note that is there places nothing more
    CHECK(notes.noteCount() == 1);
    // The audition lets go by itself.
    rig->frames(40);
    rig->listen(48000 * 4);
    CHECK(rig->listen() < 0.001f);
    rig->tap(B);
    CHECK(notes.noteCount() == 0 && !notes.noteHere());
    // X hears the key and places nothing.
    rig->tap(X);
    CHECK(rig->listen() > 0.f && notes.noteCount() == 0);
}

void testNoteLevelAndLength() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    rig->tap(DOWN, 4); // step 5
    rig->key(A, true);
    rig->tap(DOWN, 3);
    CHECK(notes.noteHere()->length == 4);
    CHECK(notes.cursorStep() == 4); // the cursor stays at the note's start
    rig->tap(UP);
    CHECK(notes.noteHere()->length == 3);
    rig->tap(UP, 9);
    CHECK(notes.noteHere()->length == 1); // never to nothing
    rig->tap(LEFT, 3);
    CHECK(near(notes.noteHere()->velocity, 0.5f));
    rig->tap(RIGHT);
    CHECK(near(notes.noteHere()->velocity, 0.6f));
    // The stick does the same, fast, one job per push.
    rig->key(STICK_DOWN, true);
    rig->frames(40);
    rig->key(STICK_DOWN, false);
    CHECK(notes.noteHere()->length == 12); // to the end of the bar and no further
    CHECK(near(notes.noteHere()->velocity, 0.6f));
    // Drifting up during a sideways push does not also change the level.
    rig->key(STICK_DOWN, true);
    rig->frames(2);
    rig->key(STICK_RIGHT, true);
    rig->frames(10);
    rig->key(STICK_RIGHT, false);
    rig->key(STICK_DOWN, false);
    CHECK(near(notes.noteHere()->velocity, 0.6f));
    rig->frames(1);
    rig->key(STICK_RIGHT, true);
    rig->frames(40);
    rig->key(STICK_RIGHT, false);
    CHECK(notes.noteHere()->velocity == 1.f && notes.noteHere()->length == 12);
    rig->key(STICK_LEFT, true);
    rig->frames(100);
    rig->key(STICK_LEFT, false);
    CHECK(near(notes.noteHere()->velocity, 0.05f)); // quiet, never gone
    rig->key(A, false);

    // The cursor anywhere inside a long note is on that note.
    rig->tap(DOWN, 5);
    CHECK(notes.noteHere() && notes.noteHere()->step == 4);
    rig->tap(B);
    CHECK(notes.noteCount() == 0);

    // A note cannot grow over the next one on the same key.
    rig->app.notes().moveCursorTo(60, 0);
    rig->tap(A);
    rig->app.notes().moveCursorTo(60, 3);
    rig->tap(A);
    rig->app.notes().moveCursorTo(60, 0);
    rig->key(A, true);
    rig->tap(DOWN, 9);
    rig->key(A, false);
    CHECK(notes.noteHere()->length == 3 && notes.noteCount() == 2);
}

void testTheEnginePlaysNotesAtTheirStepAndLength() {
    std::unique_ptr<Rig> rig(onNotes());
    // An organ: it stops when the key does.
    int organ = 0;
    while (std::strcmp(Phazed::presetCategory(organ), "Organs") != 0) ++organ;
    Command c;
    c.type = Command::SelectPreset;
    c.track = 1;
    c.a = organ;
    rig->engine->send(c);
    rig->project().playing = true; // as if the song were running, so placing is silent
    rig->tap(DOWN, 4);
    rig->key(A, true);
    rig->tap(DOWN, 3); // step 5, four steps long
    rig->key(A, false);
    rig->project().playing = false;
    rig->listen(48000);
    rig->tap(START);
    const std::vector<float> bar = rig->render(kBar);
    CHECK(Rig::peak(bar, 0, 4 * kStep - 10) == 0.f);
    const float sounding = Rig::peak(bar, 5 * kStep, 6 * kStep);
    CHECK(sounding > 0.005f);
    CHECK(Rig::peak(bar, 7 * kStep, 8 * kStep) > 0.7f * sounding); // still held in its fourth step
    CHECK(Rig::peak(bar, 12 * kStep, kBar) < 0.2f * sounding);     // and let go after it

    // Delete it, and the engine stops playing it.
    rig->tap(START);
    rig->listen(48000);
    rig->tap(B);
    CHECK(rig->app.notes().noteCount() == 0);
    rig->tap(START);
    CHECK(rig->listen(kBar) < 0.001f);
}

void testPaintingNotes() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    rig->key(R1, true);
    rig->tap(DOWN, 3);
    rig->tap(RIGHT); // and up a key: an arpeggio
    rig->tap(DOWN);
    rig->key(R1, false);
    CHECK(notes.noteCount() == 6);
    for (int step = 0; step <= 3; ++step) CHECK(rig->project().tracks[1].notes[0].at(60, step) >= 0);
    CHECK(rig->project().tracks[1].notes[0].at(61, 3) >= 0 && rig->project().tracks[1].notes[0].at(61, 4) >= 0);
    rig->tap(DOWN);
    CHECK(notes.noteCount() == 6); // the brush is up
    // The brush copies the level of the note it starts on.
    rig->app.notes().moveCursorTo(72, 0);
    rig->tap(A);
    rig->key(A, true);
    rig->tap(LEFT, 5);
    rig->key(A, false);
    rig->key(R1, true);
    rig->tap(DOWN, 2);
    rig->key(R1, false);
    for (int step = 0; step <= 2; ++step) {
        const int slot = rig->project().tracks[1].notes[0].at(72, step);
        CHECK(slot >= 0 && near(rig->project().tracks[1].notes[0].notes[slot].velocity, 0.3f));
    }
}

void testTheKeysScrollAndStopAtTheEnds() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    rig->tap(RIGHT, 200);
    CHECK(notes.cursorPitch() == NoteScreen::kHighest);
    rig->tap(A); // a note up there
    CHECK(notes.noteHere() && notes.noteHere()->pitch == NoteScreen::kHighest);
    rig->tap(LEFT, 200);
    CHECK(notes.cursorPitch() == NoteScreen::kLowest);
    rig->tap(UP);
    CHECK(notes.cursorStep() == kSteps - 1); // sideways comes round
}

void testAPatternHoldsSixtyFourNotes() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    rig->project().playing = true; // silent placing
    for (int pitch = 40; pitch < 45; ++pitch) {
        for (int step = 0; step < kSteps; ++step) {
            notes.moveCursorTo(pitch, step);
            rig->tap(A);
        }
    }
    CHECK(notes.noteCount() == kMaxNotes); // 80 asked for, 64 kept
    notes.moveCursorTo(50, 0);
    rig->tap(A);
    CHECK(!notes.noteHere());
    // Deleting one makes room for one.
    notes.moveCursorTo(40, 0);
    rig->tap(B);
    notes.moveCursorTo(50, 0);
    rig->tap(A);
    CHECK(notes.noteHere() && notes.noteCount() == kMaxNotes);
}

// ---- Phazed's screens ------------------------------------------------------------------------

Rig* onPhazed() {
    Rig* rig = onNotes();
    rig->tap(R2);
    return rig;
}

void testPresetsAndFolders() {
    std::unique_ptr<Rig> rig(onPhazed());
    PhazedScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    CHECK(rig->view() == View::Device && screen.page() == 0 && screen.cursor() == PhazedScreen::kPreset);
    CHECK(track.preset == 0);
    // Hold A and step: the next preset, and it is played at once.
    rig->key(A, true);
    rig->tap(UP);
    CHECK(track.preset == 1);
    CHECK(track.patch.value[phazed::Level] == Phazed::presetPatch(1).value[phazed::Level]);
    CHECK(rig->listen() > 0.f);
    rig->tap(DOWN, 2);
    CHECK(track.preset == Phazed::presetCount() - 1); // round the end
    rig->tap(UP);
    rig->key(A, false);
    CHECK(track.preset == 0);

    // The folder steps a folder at a time, to its first preset.
    rig->tap(LEFT);
    CHECK(screen.cursor() == PhazedScreen::kFolder);
    const char* first = Phazed::presetCategory(0);
    rig->key(A, true);
    rig->tap(UP);
    const int second = track.preset;
    CHECK(std::strcmp(Phazed::presetCategory(second), first) != 0);
    CHECK(std::strcmp(Phazed::presetCategory(second - 1), first) == 0); // the first of its folder
    rig->tap(UP);
    CHECK(std::strcmp(Phazed::presetCategory(track.preset), Phazed::presetCategory(second)) != 0);
    rig->tap(DOWN);
    CHECK(track.preset == second);
    rig->tap(DOWN);
    CHECK(track.preset == 0);
    rig->tap(DOWN); // back round to the last folder's first preset
    CHECK(std::strcmp(Phazed::presetCategory(track.preset), Phazed::presetCategory(Phazed::presetCount() - 1)) == 0);
    CHECK(track.preset == 0 + Phazed::presetCount() - 1 || std::strcmp(Phazed::presetCategory(track.preset - 1), Phazed::presetCategory(track.preset)) != 0);
    rig->key(A, false);

    // The stick runs through presets quickly.
    rig->tap(RIGHT);
    const int before = track.preset;
    rig->key(A, true);
    rig->key(STICK_UP, true);
    rig->frames(30);
    rig->key(STICK_UP, false);
    rig->key(A, false);
    CHECK(track.preset != before);
}

void testMacros() {
    std::unique_ptr<Rig> rig(onPhazed());
    PhazedScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    rig->tap(DOWN);
    CHECK(screen.cursor() == PhazedScreen::kFirstMacro + 2); // under PRESET: the right half of the row
    rig->tap(LEFT, 2);
    CHECK(screen.cursor() == PhazedScreen::kFirstMacro); // TONE
    for (float m : track.macro) CHECK(m == 0.5f);
    rig->key(A, true);
    rig->tap(UP);
    CHECK(near(track.macro[phazed::Tone], 0.55f));
    rig->tap(DOWN, 2);
    CHECK(near(track.macro[phazed::Tone], 0.45f));
    rig->key(STICK_UP, true);
    rig->frames(60);
    rig->key(STICK_UP, false);
    CHECK(track.macro[phazed::Tone] == 1.f);
    rig->key(A, false);
    CHECK(track.macro[phazed::Motion] == 0.5f); // only the one under the cursor
    // B puts it back in the middle.
    rig->tap(B);
    CHECK(track.macro[phazed::Tone] == 0.5f);

    // The cursor walks the two rows of four, then ADVANCED.
    rig->tap(RIGHT, 3);
    CHECK(screen.cursor() == PhazedScreen::kFirstMacro + 3);
    rig->tap(DOWN);
    CHECK(screen.cursor() == PhazedScreen::kFirstMacro + 7);
    rig->tap(DOWN);
    CHECK(screen.cursor() == PhazedScreen::kAdvanced);
    rig->tap(UP);
    CHECK(screen.cursor() == PhazedScreen::kFirstMacro + 4);

    // Choosing another preset clears the macros.
    rig->key(A, true);
    rig->tap(UP, 4);
    rig->key(A, false);
    CHECK(track.macro[phazed::Detune] > 0.5f);
    rig->app.phazed().goTo(0, PhazedScreen::kPreset);
    rig->key(A, true);
    rig->tap(UP);
    rig->key(A, false);
    for (float m : track.macro) CHECK(m == 0.5f);
}

// One held note on the Phazed track, rendered, with the project left as it is.
std::vector<float> noteNow(Rig& rig) {
    rig.frames(40); // let any audition end
    rig.render(48000 * 2);
    Command c;
    c.type = Command::NoteOn;
    c.track = 1;
    c.a = 60;
    c.value = 1.f;
    rig.engine->send(c);
    const std::vector<float> out = rig.render(12000);
    c.type = Command::NoteOff;
    rig.engine->send(c);
    return out;
}

void testTheEngineHearsMacrosAndSettings() {
    std::unique_ptr<Rig> plain(onPhazed()), bent(onPhazed()), edited(onPhazed());
    const std::vector<float> base = noteNow(*plain);
    CHECK(Rig::peak(base) > 0.f);
    // Another preset.
    std::unique_ptr<Rig> other(onPhazed());
    int organ = 0;
    while (std::strcmp(Phazed::presetCategory(organ), "Organs") != 0) ++organ;
    other->project().playing = true; // as if the song were running, so stepping through is silent
    other->key(A, true);
    other->tap(UP, organ);
    other->key(A, false);
    other->project().playing = false;
    CHECK(other->project().tracks[1].preset == organ);
    // It must now sound exactly like an engine told directly to use that preset.
    std::unique_ptr<Rig> direct(onPhazed());
    Command choose;
    choose.type = Command::SelectPreset;
    choose.track = 1;
    choose.a = organ;
    direct->engine->send(choose);
    const std::vector<float> wanted = noteNow(*direct);
    CHECK(noteNow(*other) == wanted);
    CHECK(wanted != base);
    // A macro.
    bent->app.phazed().goTo(0, PhazedScreen::kFirstMacro + phazed::MacroOctave);
    bent->key(A, true);
    bent->key(STICK_UP, true);
    bent->frames(60);
    bent->key(STICK_UP, false);
    bent->key(A, false);
    CHECK(noteNow(*bent) != base);
    // A setting on an advanced page: the level, 12 dB down, is a quarter as loud.
    edited->app.phazed().goTo(1, phazed::Level);
    CHECK(edited->app.phazed().settingHere() == phazed::Level);
    const int level = edited->project().tracks[1].patch.value[phazed::Level];
    edited->key(A, true);
    edited->tap(DOWN, 12);
    edited->key(A, false);
    CHECK(edited->project().tracks[1].patch.value[phazed::Level] == level - 12);
    const float ratio = Rig::peak(noteNow(*edited)) / Rig::peak(base);
    CHECK(std::fabs(ratio - 0.251f) < 0.03f);
}

void testAdvancedPages() {
    std::unique_ptr<Rig> rig(onPhazed());
    PhazedScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    screen.goTo(0, PhazedScreen::kAdvanced);
    rig->key(A, true);
    CHECK(screen.page() == 1 && screen.cursor() == 0);
    CHECK(screen.settingHere() == phazed::LineSelect);
    // That press opened the pages. Even with A still down, the stick and the
    // D-pad are the cursor, not a knob on the first setting.
    rig->key(STICK_UP, true);
    rig->frames(10);
    rig->key(STICK_UP, false);
    rig->tap(RIGHT);
    rig->key(A, false);
    CHECK(screen.cursor() == 1);
    CHECK(track.patch.value[phazed::LineSelect] == Phazed::presetPatch(0).value[phazed::LineSelect]);
    rig->tap(LEFT);

    // R1 and L1 turn the pages, round and round.
    for (int page = 2; page <= 9; ++page) {
        rig->tap(R1);
        CHECK(screen.page() == page && screen.cursor() == 0);
    }
    rig->tap(R1);
    CHECK(screen.page() == 1);
    rig->tap(L1);
    CHECK(screen.page() == 9);
    CHECK(screen.settingHere() == phazed::envRate(1, phazed::PitchEnv, 0));

    // An envelope page: eight speeds, eight levels, then HOLD AT and LAST STEP.
    rig->tap(RIGHT, 7);
    CHECK(screen.settingHere() == phazed::envRate(1, phazed::PitchEnv, 7));
    rig->tap(RIGHT);
    CHECK(screen.settingHere() == phazed::envLevel(1, phazed::PitchEnv, 0)); // on round to the next row
    rig->tap(UP);
    CHECK(screen.settingHere() == phazed::envRate(1, phazed::PitchEnv, 0));
    rig->tap(DOWN, 2);
    CHECK(screen.settingHere() == phazed::envSustain(1, phazed::PitchEnv));
    rig->tap(RIGHT);
    CHECK(screen.settingHere() == phazed::envEnd(1, phazed::PitchEnv));
    rig->tap(RIGHT);
    CHECK(screen.settingHere() == phazed::envEnd(1, phazed::PitchEnv)); // the last one
    // Settings stop at their limits.
    rig->key(A, true);
    rig->tap(UP, 20);
    CHECK(track.patch.value[phazed::envEnd(1, phazed::PitchEnv)] == 8);
    rig->tap(DOWN, 20);
    CHECK(track.patch.value[phazed::envEnd(1, phazed::PitchEnv)] == 1);
    rig->key(A, false);
    // The stick sweeps a whole range in under a second.
    screen.goTo(4, 0);
    rig->key(A, true);
    rig->key(STICK_DOWN, true);
    rig->frames(60);
    rig->key(STICK_DOWN, false);
    CHECK(track.patch.value[phazed::envRate(0, phazed::AmpEnv, 0)] == 0);
    rig->key(STICK_UP, true);
    rig->frames(60);
    rig->key(STICK_UP, false);
    CHECK(track.patch.value[phazed::envRate(0, phazed::AmpEnv, 0)] == 99);
    rig->key(A, false);

    // B goes back to MAIN, onto ADVANCED, and the pages are a tap away again.
    rig->tap(B);
    CHECK(screen.page() == 0 && screen.cursor() == PhazedScreen::kAdvanced);
    rig->tap(L1); // the page buttons do nothing on MAIN
    rig->tap(R1);
    CHECK(screen.page() == 0);
    // Every page's cursor stops map to settings inside the patch.
    for (int page = 1; page < PhazedScreen::kPages; ++page) {
        for (int stop = 0; stop < 40; ++stop) {
            screen.goTo(page, stop);
            CHECK(screen.settingHere() >= 0 && screen.settingHere() < phazed::kSettings);
        }
    }
}

void testEveryPatchSettingIsOnSomePage() {
    std::unique_ptr<Rig> rig(onPhazed());
    PhazedScreen& screen = rig->app.phazed();
    bool seen[phazed::kSettings] = {};
    for (int page = 1; page < PhazedScreen::kPages; ++page) {
        for (int stop = 0; stop < 40; ++stop) {
            screen.goTo(page, stop);
            if (screen.cursor() == stop) seen[screen.settingHere()] = true;
        }
    }
    for (bool s : seen) CHECK(s);
}

// ---- Twin Analog, through the same screens ------------------------------------------

Rig* onTwin() {
    Rig* rig = new Rig;
    rig->app.session().view = View::Song;
    rig->app.song().moveCursorTo(0, rig->project().trackCount());
    rig->tap(A);    // + TRACK
    rig->tap(DOWN); // from PHAZED down to TWIN ANALOG
    rig->tap(A);
    return rig;
}

void testAddingTwinAnalog() {
    std::unique_ptr<Rig> rig(onTwin());
    CHECK(rig->project().trackCount() == 2);
    CHECK(rig->project().tracks[1].kind == TrackKind::Twin);
    // Its first preset is loaded into the project, as the engine has it.
    const TwinPatch first = Twin::presetPatch(0);
    for (int id = 0; id < twin::kSettings; ++id) CHECK(rig->project().tracks[1].twin.value[id] == first.value[id]);
    // The chooser goes round all three.
    rig->tap(RIGHT);
    rig->tap(A);
    CHECK(rig->app.song().choosing());
    rig->tap(DOWN, 2); // PHAZED > TWIN > DRUMS
    rig->tap(A);
    CHECK(rig->project().tracks[2].kind == TrackKind::Drums);

    // It gets the piano roll, and the engine plays it.
    rig->app.song().moveCursorTo(0, 1);
    rig->tap(A);
    CHECK(rig->view() == View::Pattern && rig->project().track == 1);
    rig->tap(A);
    CHECK(rig->app.notes().noteCount() == 1);
    rig->listen();
    CHECK(rig->engine->trackPeak(1) > 0.f);
    CHECK(rig->engine->trackPeak(0) == 0.f && rig->engine->trackPeak(2) == 0.f);
}

void testTwinsDeviceScreen() {
    std::unique_ptr<Rig> rig(onTwin());
    rig->tap(A);  // open its pattern
    rig->tap(R2); // and its device
    PhazedScreen& screen = rig->app.twin();
    TrackData& track = rig->project().tracks[1];
    CHECK(rig->view() == View::Device && screen.pageCount() == 4);
    CHECK(rig->app.phazed().page() == 0); // Phazed's own screen is a separate one

    // Presets: sixteen, and round.
    rig->key(A, true);
    rig->tap(UP);
    CHECK(track.preset == 1);
    for (int id = 0; id < twin::kSettings; ++id) CHECK(track.twin.value[id] == Twin::presetPatch(1).value[id]);
    rig->tap(DOWN, 2);
    CHECK(track.preset == Twin::presetCount() - 1);
    rig->tap(UP);
    rig->key(A, false);

    // A macro.
    screen.goTo(0, PhazedScreen::kFirstMacro + twin::Blend);
    rig->key(A, true);
    rig->tap(UP, 3);
    rig->key(A, false);
    CHECK(near(track.macro[twin::Blend], 0.65f));

    // Three advanced pages hold all twenty-three settings, each once.
    int seen[twin::kSettings] = {};
    for (int page = 1; page < screen.pageCount(); ++page) {
        for (int stop = 0; stop < 40; ++stop) {
            screen.goTo(page, stop);
            if (screen.cursor() == stop) ++seen[screen.settingHere()];
        }
    }
    for (int count : seen) CHECK(count == 1);
    screen.goTo(9, 0);
    CHECK(screen.page() == 3); // there is no page nine here
    rig->tap(R1);
    CHECK(screen.page() == 1); // round
    rig->tap(L1);
    CHECK(screen.page() == 3);

    // A setting changes the project and stops at its limits.
    screen.goTo(1, 0);
    CHECK(screen.settingHere() == twin::VOctave);
    rig->key(A, true);
    rig->tap(UP, 9);
    CHECK(track.twin.value[twin::VOctave] == 2);
    rig->tap(DOWN, 9);
    CHECK(track.twin.value[twin::VOctave] == -2);
    rig->key(A, false);
    CHECK(track.patch.value[phazed::Octave] == 0); // and never touches the other synth's patch
}

void testTheEngineHearsTwinsSettings() {
    // The level setting, 12 dB down, is a quarter as loud.
    const auto loudness = [](int steps) {
        std::unique_ptr<Rig> rig(onTwin());
        rig->tap(A);
        rig->tap(R2);
        rig->app.twin().goTo(3, 4);
        CHECK(rig->app.twin().settingHere() == twin::Level);
        rig->key(A, true);
        rig->tap(DOWN, steps);
        rig->key(A, false);
        // Vintage off, so the bus colour is out of the way of the measurement.
        Command off;
        off.type = Command::SetSetting;
        off.track = 1;
        off.a = twin::Vintage;
        off.b = 0;
        rig->engine->send(off);
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 57;
        c.value = 1.f;
        rig->engine->send(c);
        rig->render(9600);
        return Rig::peak(rig->render(9600));
    };
    const float full = loudness(0), down = loudness(12);
    CHECK(full > 0.01f);
    CHECK(std::fabs(down / full - 0.251f) < 0.03f);
}

} // namespace

int main(int, char**) {
    testTabs();
    testANewProjectIsOneDrumTrack();
    testAddingTracks();
    testOpeningACellWorksOnThatScene();
    testRepeatCounts();
    testStartOnTheSongScreenPlaysTheSong();
    testCopyingABarByDragging();
    testBEmptiesACell();
    testTheCopyBrushLifts();
    testTheSongScreenScrolls();
    testPlacingAndDeletingNotes();
    testNoteLevelAndLength();
    testTheEnginePlaysNotesAtTheirStepAndLength();
    testPaintingNotes();
    testTheKeysScrollAndStopAtTheEnds();
    testAPatternHoldsSixtyFourNotes();
    testPresetsAndFolders();
    testMacros();
    testTheEngineHearsMacrosAndSettings();
    testAdvancedPages();
    testEveryPatchSettingIsOnSomePage();
    testAddingTwinAnalog();
    testTwinsDeviceScreen();
    testTheEngineHearsTwinsSettings();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("app tests passed\n");
    return 0;
}
