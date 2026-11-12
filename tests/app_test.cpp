// Tests for the whole app: the Song screen, the note screen, Phazed's
// screens and moving between them. No window and no sound card: buttons are
// pressed through the keyboard stand-in, and the engine is rendered by hand
// to hear what the screens told it.

#include <SDL.h>
#include <sys/stat.h>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "battery.hpp"
#include "controls.hpp"
#include "song_file.hpp"
#include "synth_spec.hpp"

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
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
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
    CHECK(rig.view() == View::Effects);
    rig.tap(R2);
    CHECK(rig.view() == View::Settings);
    rig.tap(R2);
    CHECK(rig.view() == View::Settings);
    // Not while shaping something: a slip of the finger must not change screens.
    rig.tap(L2, 3);
    CHECK(rig.view() == View::Pattern);
    rig.key(A, true);
    rig.tap(L2);
    CHECK(rig.view() == View::Pattern);
    rig.key(A, false);
    // On the drum grid R2 is the next screen AT ONCE, on the press: no waiting
    // to see whether it is being held.
    rig.app.drums().moveCursorTo(3, 5);
    rig.key(R2, true);
    CHECK(rig.view() == View::Device);
    rig.key(R2, false);
    CHECK(rig.view() == View::Device && rig.app.drums().level(3, 5) == 0.f);
    rig.tap(L2);
    // With R1 painting, R2 is not the next screen: it turns the brush random.
    rig.key(R1, true);
    rig.key(R2, true);
    CHECK(rig.view() == View::Pattern && rig.app.drums().paintingRandom());
    rig.tap(RIGHT);
    rig.key(R2, false);
    CHECK(rig.view() == View::Pattern && rig.app.drums().painting() && !rig.app.drums().paintingRandom());
    rig.key(R1, false);
    CHECK(rig.app.drums().level(3, 5) >= 0.25f && rig.app.drums().level(3, 6) >= 0.25f);
    // R1 let go first: R2 coming up after it changes no screen.
    rig.key(R1, true);
    rig.key(R2, true);
    rig.key(R1, false);
    rig.key(R2, false);
    CHECK(rig.view() == View::Pattern);
    // With Y held R2 does nothing.
    rig.app.drums().moveCursorTo(4, 2);
    rig.key(Y, true);
    rig.tap(R2);
    rig.key(Y, false);
    CHECK(rig.view() == View::Pattern);
    // START works on the kit's page and the effects page too.
    rig.tap(R2);
    rig.tap(START);
    CHECK(rig.project().playing);
    rig.tap(R2);
    rig.tap(START);
    CHECK(!rig.project().playing && rig.view() == View::Effects);
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
    CHECK(rig.project().tracks[0].drums[2][0].level[0][0] > 0.f);
    CHECK(rig.project().tracks[0].drums[0][0].empty());
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
    CHECK(!rig.project().tracks[1].notes[2][0].empty());
    rig.app.song().moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.tap(DOWN, 2);
    rig.tap(RIGHT); // across onto the synth: a drum bar cannot go there
    rig.key(R1, false);
    CHECK(!rig.project().tracks[1].notes[2][0].empty()); // and it did not wipe the synth's bar
    for (int scene = 1; scene <= 2; ++scene) {
        const DrumPattern& p = rig.project().tracks[0].drums[scene][0];
        CHECK(near(p.level[0][0], 0.9f) && near(p.level[2][5], 0.4f));
        CHECK(near(p.nudge[2][5], 0.25f) && near(p.chance[2][5], 0.5f));
    }
    CHECK(rig.project().tracks[0].drums[3][0].empty());
    // Moving with R1 let go copies nothing.
    rig.app.song().moveCursorTo(2, 0);
    rig.tap(DOWN);
    CHECK(rig.project().tracks[0].drums[3][0].empty());
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
    CHECK(!synth.project().tracks[1].notes[1][0].empty());
    CHECK(synth.project().tracks[1].notes[1][0].notes[0].pitch == synth.project().tracks[1].notes[0][0].notes[0].pitch);
}

void testBEmptiesACell() {
    Rig rig;
    markScenes(rig, 2);
    rig.tap(L2);
    rig.tap(B); // scene 1
    CHECK(rig.project().tracks[0].drums[0][0].empty());
    CHECK(!rig.project().tracks[0].drums[1][0].empty());
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
    CHECK(!rig.project().tracks[0].drums[1][0].empty());
    for (int scene = 2; scene <= 4; ++scene) CHECK(rig.project().tracks[0].drums[scene][0].empty());
    // R1 on a repeat count or on + TRACK picks nothing up.
    rig.app.song().moveCursorTo(0, -1);
    rig.key(R1, true);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    rig.key(R1, false);
    CHECK(rig.project().tracks[0].drums[2][0].empty());
}

void testTheSongScreenScrolls() {
    Rig rig;
    rig.tap(L2);
    rig.tap(DOWN, 40);
    CHECK(rig.app.song().cursorScene() == kMaxScenes - 1);
    rig.tap(UP, 40);
    CHECK(rig.app.song().cursorScene() == 0);
    rig.tap(DOWN); // (the last of those presses went up onto the track's head: back down off it)
    CHECK(rig.app.song().cursorScene() == 0 && !rig.app.song().cursorOnHead());
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
    // The stick is free: the end goes to any point in a step.
    rig->key(STICK_DOWN, true);
    rig->frames(5);
    rig->key(STICK_DOWN, false);
    rig->frames(1);
    const float free = notes.noteHere()->length;
    CHECK(free > 1.5f && free < 2.f && free != std::floor(free)); // about ten steps a second, fully pushed
    CHECK(notes.cursorStep() == 4);
    // The D-pad puts it back on a step line: the next one, or the one before.
    rig->tap(DOWN);
    CHECK(notes.noteHere()->length == 2.f);
    rig->key(STICK_DOWN, true);
    rig->frames(3);
    rig->key(STICK_DOWN, false);
    rig->frames(1);
    CHECK(notes.noteHere()->length > 2.f && notes.noteHere()->length < 3.f);
    rig->tap(UP);
    CHECK(notes.noteHere()->length == 2.f);
    rig->tap(UP);
    CHECK(notes.noteHere()->length == 1.f);
    // Shorter than a step, with the stick only; never to nothing.
    rig->key(STICK_UP, true);
    rig->frames(3);
    rig->key(STICK_UP, false);
    rig->frames(1);
    const float shortNote = notes.noteHere()->length;
    CHECK(shortNote > 0.3f && shortNote < 0.8f);
    rig->tap(UP); // there is no whole step below it: nothing happens
    CHECK(notes.noteHere()->length == shortNote);
    rig->key(STICK_UP, true);
    rig->frames(60);
    rig->key(STICK_UP, false);
    rig->frames(1);
    CHECK(near(notes.noteHere()->length, 0.1f));
    rig->tap(DOWN);
    CHECK(notes.noteHere()->length == 1.f);
    // A small push is a fine adjustment: a quarter of the way over moves a sixteenth as fast.
    rig->key(STICK_DOWN, true);
    rig->frames(100);
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
    for (int step = 0; step <= 3; ++step) CHECK(rig->project().tracks[1].notes[0][0].at(60, step) >= 0);
    CHECK(rig->project().tracks[1].notes[0][0].at(61, 3) >= 0 && rig->project().tracks[1].notes[0][0].at(61, 4) >= 0);
    rig->tap(DOWN);
    CHECK(notes.noteCount() == 6); // the brush is up
    // Started on a note, R1 is not the brush: it takes hold of the note, and the note goes with the cursor.
    rig->app.notes().moveCursorTo(72, 0);
    rig->tap(A);
    rig->key(A, true);
    rig->tap(LEFT, 5);
    rig->key(A, false);
    rig->key(R1, true);
    rig->tap(DOWN, 2);
    rig->key(R1, false);
    {
        const NotePattern& p = rig->project().tracks[1].notes[0][0];
        CHECK(p.at(72, 0) < 0 && p.at(72, 1) < 0 && p.at(72, 2) >= 0 && near(p.notes[p.at(72, 2)].velocity, 0.3f));
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

// ---- cells longer than a bar --------------------------------------------------------------

void testCellsOfSeveralBars() {
    Rig rig;
    Session& session = rig.app.session();
    SongScreen& song = rig.app.song();
    PatternScreen& drums = rig.app.drums();
    TrackData& track = rig.project().tracks[0];
    session.view = View::Song;
    song.moveCursorTo(0, 0);

    // Hold A on a cell and go right: 2, 4, 8 bars, and no further. Letting go then does not open it.
    rig.key(A, true);
    rig.tap(RIGHT);
    CHECK(track.bars[0] == 2);
    rig.tap(RIGHT, 5);
    CHECK(track.bars[0] == 8);
    rig.tap(LEFT);
    CHECK(track.bars[0] == 4);
    rig.tap(UP); // up and down are nothing here
    rig.key(A, false);
    CHECK(rig.view() == View::Song && track.bars[0] == 4 && song.cursorScene() == 0 && song.cursorColumn() == 0);
    // The stick does the same, one at once and then slowly.
    rig.key(A, true);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    CHECK(track.bars[0] == 2);
    rig.frames(5);
    CHECK(track.bars[0] == 2);
    rig.frames(40);
    CHECK(track.bars[0] == 1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(A, false);
    CHECK(track.bars[0] == 2 && rig.view() == View::Song);
    // A plain tap still opens the cell, on its first bar.
    rig.project().bar = 1;
    rig.tap(A);
    CHECK(rig.view() == View::Pattern && rig.project().bar == 0 && rig.project().scene == 0);

    // ---- the drum grid: off the right of step 16 is the next bar ---------------------
    drums.moveCursorTo(0, 14);
    rig.tap(RIGHT, 2);
    CHECK(rig.project().bar == 1 && drums.cursorStep() == 0);
    rig.tap(A); // a kick on step 1 of bar 2
    CHECK(track.drums[0][1].level[0][0] == 0.8f && track.drums[0][0].level[0][0] == 0.f);
    CHECK(drums.level(0, 0) == 0.8f); // the grid shows the bar it is on
    // Left of step 1 is the lane's name, in whichever bar; left of that, the bar before.
    rig.tap(LEFT);
    CHECK(drums.cursorStep() == -1 && rig.project().bar == 1);
    rig.tap(LEFT);
    CHECK(drums.cursorStep() == 15 && rig.project().bar == 0);
    rig.tap(RIGHT);
    CHECK(drums.cursorStep() == 0 && rig.project().bar == 1);
    // Off the end of the last bar: the name, and round to the first bar.
    rig.tap(RIGHT, 16);
    CHECK(drums.cursorStep() == -1 && rig.project().bar == 0);
    rig.tap(RIGHT);
    CHECK(drums.cursorStep() == 0 && rig.project().bar == 0);
    rig.tap(LEFT, 2);
    CHECK(drums.cursorStep() == 15 && rig.project().bar == 1); // and back the other way
    // The engine plays it there: a kick at the start of every second bar.
    rig.render(100000);
    rig.tap(START);
    std::vector<float> out = rig.render(4 * kBar);
    CHECK(Rig::peak(out, 0, kBar - 100) == 0.f && Rig::peak(out, kBar, kBar + 2000) > 0.2f);
    CHECK(Rig::peak(out, 2 * kBar, 3 * kBar - 100) < 0.01f && Rig::peak(out, 3 * kBar, 3 * kBar + 2000) > 0.2f);
    rig.frames(1);
    CHECK(rig.project().playBar == 1 && rig.project().sceneBars(0) == 2);
    rig.tap(START);
    // Gestures work in the bar being shown: nudge, chance, delete.
    drums.moveCursorTo(0, 0);
    rig.key(A, true);
    rig.tap(R2);
    rig.key(A, false);
    CHECK(near(track.drums[0][1].nudge[0][0], 0.1f) && track.drums[0][0].nudge[0][0] == 0.f);
    drums.moveCursorTo(3, 5);
    rig.tap(A);
    rig.tap(B);
    CHECK(track.drums[0][1].level[3][5] == 0.f);

    // ---- a second track, one bar long, goes round under it ---------------------------
    session.addTrack(TrackKind::Phazed);
    CHECK(rig.project().tracks[1].bars[0] == 1 && rig.project().sceneBars(0) == 2);
    // Made longer on the Song screen, it makes the scene longer.
    session.view = View::Song;
    song.moveCursorTo(0, 1);
    rig.key(A, true);
    rig.tap(RIGHT, 2);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].bars[0] == 4 && rig.project().sceneBars(0) == 4);
    CHECK(rig.project().sceneBars(1) == 1); // each scene is its own

    // ---- the piano roll: off the bottom is the next bar --------------------------------
    rig.tap(A);
    NoteScreen& notes = rig.app.notes();
    NotePattern* bars = rig.project().tracks[1].notes[0];
    notes.moveCursorTo(60, 15);
    rig.tap(DOWN);
    CHECK(rig.project().bar == 1 && notes.cursorStep() == 0);
    rig.tap(A);
    CHECK(bars[1].at(60, 0) >= 0 && bars[0].empty());
    rig.tap(UP);
    CHECK(rig.project().bar == 0 && notes.cursorStep() == 15);
    rig.tap(UP, 16);
    CHECK(rig.project().bar == 3 && notes.cursorStep() == 15); // round the top to the last bar
    rig.tap(DOWN);
    CHECK(rig.project().bar == 0 && notes.cursorStep() == 0);
    // A note may run on over the bar lines, to the end of the cell and no further.
    notes.moveCursorTo(64, 8);
    rig.key(A, true);
    rig.tap(DOWN, 80);
    rig.key(A, false);
    CHECK(bars[0].notes[bars[0].at(64, 8)].length == 56.f); // four bars less the eight steps before it
    // In bar 3 the same note placed late can only reach the end of the cell.
    rig.project().bar = 2;
    notes.moveCursorTo(67, 12);
    rig.key(A, true);
    rig.tap(DOWN, 80);
    rig.key(A, false);
    CHECK(bars[2].notes[bars[2].at(67, 12)].length == 20.f);
    // A chord placed near the end of a bar runs on into the next; the cursor follows it there.
    rig.project().bar = 0;
    notes.moveCursorTo(60, 14);
    rig.tap(Y);
    CHECK(bars[0].chords[bars[0].chordAt(14)].length == 4.f && rig.project().bar == 1 && notes.cursorStep() == 2);

    // Shortened while a later bar is showing: the screen comes back to a bar the cell has.
    rig.project().bar = 3;
    session.setCellBars(1, 0, 2);
    CHECK(rig.project().bar == 1);
    session.setCellBars(1, 0, 4);
    rig.project().bar = 3;
    rig.project().tracks[1].bars[0] = 1; // as when another cell is opened
    rig.frames(1);
    CHECK(rig.project().bar == 0);
    rig.project().tracks[1].bars[0] = 4;
    // The same on the drum grid.
    rig.project().track = 0;
    rig.project().bar = 1;
    track.bars[0] = 1;
    rig.frames(1);
    CHECK(rig.project().bar == 0);
    track.bars[0] = 2;
    rig.project().track = 1;
    // A track with no instrument does not make a scene longer.
    rig.project().tracks[7].bars[2] = 8;
    CHECK(rig.project().sceneBars(2) == 1);
    rig.project().tracks[7].bars[2] = 1;
    // Lengths are 1, 2, 4 or 8, whatever is asked.
    session.setCellBars(1, 5, 3);
    CHECK(rig.project().tracks[1].bars[5] == 2);
    session.setCellBars(1, 5, 99);
    CHECK(rig.project().tracks[1].bars[5] == 8);
    session.setCellBars(1, 5, -4);
    CHECK(rig.project().tracks[1].bars[5] == 1);
    session.setCellBars(12, 0, 4);
    session.setCellBars(0, 16, 4);

    // ---- copying and emptying take the whole cell ------------------------------------------
    session.view = View::Song;
    song.moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.tap(DOWN);
    rig.key(R1, false);
    CHECK(track.bars[1] == 2 && track.drums[1][1].level[0][0] == 0.8f && near(track.drums[1][1].nudge[0][0], 0.1f));
    rig.tap(B); // empty the copy
    CHECK(track.bars[1] == 1 && track.drums[1][1].empty() && track.sceneEmpty(1));
    CHECK(track.bars[0] == 2 && track.drums[0][1].level[0][0] == 0.8f);
    // A cell with something only in a later bar is not "empty": the song runs to it.
    CHECK(!track.sceneEmpty(0));
    track.bars[3] = 2;
    track.drums[3][1].level[2][2] = 0.5f;
    CHECK(!track.sceneEmpty(3) && rig.project().sceneCount() == 4);
    track.bars[3] = 1; // that bar is past the cell's end now: kept, not played, not counted
    CHECK(track.sceneEmpty(3));
    track.drums[3][1].level[2][2] = 0.f;

    // ---- the song file ------------------------------------------------------------------------
    const std::string text = songText(rig.project());
    CHECK(text.find("track drums\nlanes 1 1 1 1 1 1 1 1\nscene 1\nbars 2\nbar 2\nseeds") != std::string::npos);
    CHECK(text.find("hit 1 1 0.8 0.1 1 0\n") != std::string::npos);
    CHECK(text.find("scene 1\nbars 4\nnote 9 64 56 0.8\n") != std::string::npos);
    CHECK(text.find("\nbar 2\nnote 1 60 1 0.8\n") != std::string::npos && text.find("\nbar 3\nnote 13 67 20 0.8\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].bars[0] == 2 && back->tracks[1].bars[0] == 4 && back->tracks[0].drums[0][1].level[0][0] == 0.8f);
    // Opened on another handheld it sounds the same as one whose engine was told bar by bar.
    Rig opened, told;
    opened.project() = *back;
    opened.app.session().applyProject();
    told.app.session().addTrack(TrackKind::Phazed);
    told.project().tracks[0] = back->tracks[0];
    told.project().tracks[1] = back->tracks[1];
    told.app.session().sendDrumPattern(0, 0);
    told.app.session().sendNotePattern(1, 0);
    opened.app.session().view = told.app.session().view = View::Pattern;
    opened.tap(START);
    told.tap(START);
    const std::vector<float> a = opened.render(4 * kBar), b = told.render(4 * kBar);
    CHECK(Rig::peak(a) > 0.05f && a == b);
    // Lines that make no sense are left out: a bar the cell does not have, a length that is not one.
    CHECK(parseSong("apotheca-song 1\ntrack drums\nscene 1\nbars 3\nbar 2\nhit 1 1 1\nbar 3\nhit 2 2 1\nbar 9\nhit 3 3 1\nbar 0\nhit 4 4 1\nscene 2\nhit 5 5 1\n", *back));
    CHECK(back->tracks[0].bars[0] == 2 && back->tracks[0].drums[0][1].level[0][0] == 1.f);
    CHECK(back->tracks[0].drums[0][2].empty() && back->tracks[0].drums[0][0].empty()); // bars 3, 9 and 0 are not there
    CHECK(back->tracks[0].drums[1][0].level[4][4] == 1.f && back->tracks[0].bars[1] == 1); // a new scene starts at its first bar
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nscene 1\nnote 9 60 40 1\nscene 2\nbars 2\nnote 9 60 40 1\n", *back));
    CHECK(back->tracks[0].notes[0][0].notes[0].length == 8.f && back->tracks[0].notes[1][0].notes[0].length == 24.f);
}

void testNudgingWithTheOuterShoulderButtons() {
    // ---- the piano roll -----------------------------------------------------------
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    notes.moveCursorTo(60, 4);
    rig->key(A, true); // places the note and takes hold of it
    CHECK(notes.noteHere() && notes.noteHere()->nudge == 0.f);
    rig->tap(R2);
    CHECK(near(notes.noteHere()->nudge, 0.1f));
    CHECK(rig->view() == View::Pattern); // not a change of screen while A is held
    rig->tap(R2, 9);
    CHECK(near(notes.noteHere()->nudge, 0.5f)); // half a step is the most
    rig->tap(L2, 7);
    CHECK(near(notes.noteHere()->nudge, -0.2f));
    rig->tap(L2, 9);
    CHECK(near(notes.noteHere()->nudge, -0.5f));
    rig->tap(R2, 5);
    CHECK(notes.noteHere()->nudge == 0.f); // back on the line exactly, not a rounding away from it
    rig->tap(R2, 3);
    CHECK(notes.noteHere()->length == 1.f && notes.cursorStep() == 4 && notes.cursorPitch() == 60); // nothing else moved
    rig->key(A, false);
    // With A let go they are the tabs again.
    rig->tap(R2);
    CHECK(rig->view() == View::Device && near(notes.noteHere()->nudge, 0.3f));
    rig->tap(L2);
    CHECK(rig->view() == View::Pattern);
    // The engine plays it late: compare with the same note on its line.
    const auto firstSound = [](Rig& r) {
        r.frames(60);
        r.render(200000);
        r.tap(START);
        const std::vector<float> out = r.render(8 * kStep);
        r.tap(START);
        for (std::size_t i = 0; i < out.size(); i += 2)
            if (std::fabs(out[i]) > 1e-4f) return static_cast<int>(i / 2);
        return -1;
    };
    const int late = firstSound(*rig);
    rig->key(A, true);
    rig->tap(L2, 3);
    rig->key(A, false);
    const int onLine = firstSound(*rig);
    CHECK(onLine > 4 * kStep - 10 && onLine < 4 * kStep + 200);
    CHECK(std::abs((late - onLine) - 3 * kStep / 10) <= 4);
    // A chord is nudged as one thing, and its notes go to the engine nudged.
    notes.moveCursorTo(60, 8);
    rig->tap(Y);
    notes.moveCursorTo(60, 9);
    rig->key(A, true);
    rig->tap(L2, 2);
    rig->key(A, false);
    const NotePattern& p = rig->project().tracks[1].notes[0][0];
    CHECK(near(p.chords[p.chordAt(8)].nudge, -0.2f));
    NoteEvent out[kMaxNotes];
    p.render(out);
    int nudged = 0;
    for (const NoteEvent& n : out) nudged += n.velocity > 0.f && n.step == 8 && near(n.nudge, -0.2f);
    CHECK(nudged == 3);
    // On an empty key there is nothing to nudge. (A placed a note there; take it off first.)
    notes.moveCursorTo(70, 12);
    rig->key(A, true);
    rig->tap(B);
    rig->tap(R2);
    rig->key(A, false);
    // While Y is choosing a chord, A and the shoulder buttons are not nudging anything.
    notes.moveCursorTo(60, 4);
    const float before = notes.noteHere()->nudge;
    rig->key(Y, true);
    rig->key(A, true);
    rig->tap(R2, 2);
    rig->key(A, false);
    rig->tap(B); // call the chord off
    rig->key(Y, false);
    CHECK(notes.noteHere()->nudge == before);
    // The song file keeps it; a note on its line has nothing extra written.
    const std::string text = songText(rig->project());
    CHECK(text.find("note 5 60 1 0.8\n") != std::string::npos);
    CHECK(text.find(" nudge -0.2\n") != std::string::npos);
    notes.moveCursorTo(60, 4);
    rig->key(A, true);
    rig->tap(R2, 2);
    rig->key(A, false);
    const std::string nudgedText = songText(rig->project());
    CHECK(nudgedText.find("note 5 60 1 0.8 0.2\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(nudgedText, *back) && songText(*back) == nudgedText);
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nscene 1\nnote 1 60 1 0.8 9\nchord 5 2 0.8 0 60 C 0 4 7 nudge -9\nchord 9 2 0.8 0 60 C 0 4 7 nudge\n", *back));
    CHECK(back->tracks[0].notes[0][0].notes[0].nudge == 0.5f && back->tracks[0].notes[0][0].chords[0].nudge == -0.5f && back->tracks[0].notes[0][0].chords[1].nudge == 0.f);

    // ---- the drum grid: the same two buttons ------------------------------------------
    Rig drumsRig;
    PatternScreen& drums = drumsRig.app.drums();
    drumsRig.app.session().view = View::Pattern;
    drums.moveCursorTo(2, 6);
    drumsRig.key(A, true);
    drumsRig.tap(R2, 2);
    CHECK(near(drums.nudge(2, 6), 0.2f) && drumsRig.view() == View::Pattern);
    drumsRig.tap(L2, 5);
    CHECK(near(drums.nudge(2, 6), -0.3f));
    drumsRig.tap(L2, 9);
    CHECK(near(drums.nudge(2, 6), -0.5f));
    CHECK(drums.level(2, 6) == 0.8f && drums.cursorStep() == 6);
    drumsRig.key(A, false);
    drumsRig.tap(L2);
    CHECK(drumsRig.view() == View::Song && near(drums.nudge(2, 6), -0.5f));
    // On a lane's name or the tempo, with A held, they do nothing (and do not change screen).
    drumsRig.app.session().view = View::Pattern;
    drums.moveCursorTo(2, -1);
    drumsRig.key(A, true);
    drumsRig.tap(R2, 2);
    drumsRig.key(A, false);
    CHECK(drumsRig.view() == View::Pattern && near(drums.nudge(2, 6), -0.5f) && drums.laneLevel(2) == 1.f);
    // ...nor on the tempo, where sideways would otherwise be ten beats a minute.
    drums.moveCursorTo(0, 0);
    drumsRig.tap(UP);
    CHECK(drums.cursorOnTempo());
    const float tempo = drumsRig.project().tempo;
    drumsRig.key(A, true);
    drumsRig.tap(R2, 2);
    drumsRig.tap(L2);
    drumsRig.key(A, false);
    CHECK(drumsRig.project().tempo == tempo && drumsRig.view() == View::Pattern);
    // On the other screens A + L2 / R2 is still "not a change of screen", and nothing else.
    drumsRig.app.session().view = View::Device;
    drumsRig.key(A, true);
    drumsRig.tap(R2);
    drumsRig.key(A, false);
    CHECK(drumsRig.view() == View::Device);
}

void testAFreeLengthReachesTheEngine() {
    // Three fresh handhelds with one note each: two and a half steps, two
    // steps, and two and a half again but loaded as a whole song.
    const auto bar = [](float length, bool asASong) {
        std::unique_ptr<Rig> rig(new Rig);
        rig->app.session().addTrack(TrackKind::Phazed);
        NoteEvent& n = rig->project().tracks[1].notes[0][0].notes[0];
        n.step = 1;
        n.pitch = 60;
        n.length = length;
        n.velocity = 0.8f;
        if (asASong) rig->app.session().applyProject();
        else rig->app.session().sendNotePattern(1, 0);
        rig->app.session().view = View::Pattern;
        rig->tap(START);
        return rig->render(8 * kStep);
    };
    const std::vector<float> free = bar(2.5f, false), whole = bar(2.f, false), three = bar(3.f, false);
    CHECK(Rig::peak(free) > 0.01f);
    CHECK(free != whole && free != three);
    CHECK(bar(2.5f, true) == free);
    // With the cursor inside a long note, the stick cannot pull its end back past the cursor.
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    notes.moveCursorTo(60, 4);
    rig->key(A, true);
    rig->tap(DOWN, 3);
    rig->key(A, false);
    notes.moveCursorTo(60, 6);
    CHECK(notes.noteHere() && notes.noteHere()->length == 4.f);
    rig->key(A, true);
    rig->key(STICK_UP, true);
    rig->frames(80);
    rig->key(STICK_UP, false);
    rig->frames(1);
    rig->key(A, false);
    CHECK(notes.noteHere() && near(notes.noteHere()->length, 2.1f)); // a tenth of the cursor's step is left
}

void testAllFourShoulderButtonsIsPanic() {
    Rig rig;
    for (int s = 0; s < kSteps; ++s) rig.app.drums().setStep(s % 2 == 0 ? 0 : 7, s, 1.f);
    rig.app.session().view = View::Pattern;
    rig.app.drums().moveCursorTo(4, 6);
    rig.tap(START);
    CHECK(rig.listen(3 * kStep) > 0.2f && rig.project().playing);

    // Three of the four is not a panic.
    rig.key(L2, true);
    rig.key(R2, true);
    rig.key(L1, true);
    rig.frames(1);
    CHECK(rig.project().playing && !rig.project().silenced);
    CHECK(rig.listen(2 * kStep) > 0.2f);
    // The fourth is: silence within a few milliseconds, and the song stopped.
    rig.key(R1, true);
    CHECK(!rig.project().playing && rig.project().silenced);
    rig.render(300);
    CHECK(rig.listen(48000) == 0.f);
    rig.frames(1);
    CHECK(rig.project().silenced && rig.project().playStep == -1);
    // It did nothing else: the same screen, and no hit painted under the cursor.
    CHECK(rig.view() == View::Pattern);
    CHECK(rig.app.drums().level(4, 6) == 0.f && !rig.app.drums().painting());
    // Held: still nothing, whatever is pressed. And a notice says what happened.
    CHECK(rig.app.panicNoteShowing());
    rig.tap(A);
    CHECK(rig.listen(24000) == 0.f && rig.app.panicNoteShowing());
    CHECK(rig.app.drums().level(4, 6) > 0.f); // A still did its job on the grid: the notice is not in the way while held
    rig.app.drums().setStep(4, 6, 0.f);
    // Let go: it opens again, and START plays as before.
    rig.key(R1, false);
    rig.key(L1, false);
    rig.key(R2, false);
    rig.key(L2, false);
    rig.render(48000);
    rig.frames(1);
    CHECK(!rig.project().silenced && rig.view() == View::Pattern);
    // The notice is still up. The next press puts it away and does nothing else.
    CHECK(rig.app.panicNoteShowing());
    rig.tap(A);
    CHECK(!rig.app.panicNoteShowing() && rig.app.drums().level(4, 6) == 0.f);
    rig.tap(A);
    CHECK(rig.app.drums().level(4, 6) > 0.f); // the one after that is an ordinary press
    rig.app.drums().setStep(4, 6, 0.f);
    rig.render(48000);
    rig.tap(START);
    CHECK(rig.project().playing && rig.listen(3 * kStep) > 0.2f);

    // Any order, and from any screen. From SONG the triggers would have
    // turned the tab on the way; it is put back.
    rig.app.session().view = View::Song;
    rig.key(R1, true);
    rig.key(R2, true);
    rig.key(L1, true);
    rig.key(L2, true);
    CHECK(!rig.project().playing && rig.view() == View::Song);
    rig.render(300);
    CHECK(rig.listen(24000) == 0.f);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.render(48000);
    // Left alone, the notice goes by itself after four seconds, and not before.
    rig.frames(1);
    CHECK(rig.app.panicNoteShowing());
    rig.tap(L1); // a shoulder button alone is not "any button": fingers are still coming off them
    CHECK(rig.app.panicNoteShowing());
    rig.frames(240); // 3.8 s
    CHECK(rig.app.panicNoteShowing());
    rig.frames(15);
    CHECK(!rig.app.panicNoteShowing());
    // A shoulder button by itself does not put it away early (it is how the panic is let go).
    for (SDL_Keycode k : {L2, R2, L1, R1}) rig.key(k, true);
    rig.key(L1, false);
    CHECK(rig.app.panicNoteShowing());
    rig.key(L1, true); // back to all four: a panic again, still showing
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.tap(START);
    CHECK(!rig.app.panicNoteShowing() && !rig.project().playing);
    rig.render(4 * 48000);
    // A synth note being heard is let go too.
    rig.app.session().addTrack(TrackKind::Phazed);
    rig.app.session().audition(1, 60, 1.f);
    CHECK(rig.listen(4800) > 0.01f);
    for (SDL_Keycode k : {L2, R2, L1, R1}) rig.key(k, true);
    rig.render(300);
    CHECK(rig.listen(24000) == 0.f);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.frames(200); // the audition's own timer runs out: nothing odd happens
    rig.render(4 * 48000);
    rig.frames(1);
    CHECK(!rig.project().silenced && rig.listen(4800) < 1e-4f);
}

// ---- SETUP: the last tab ---------------------------------------------------------------

// ---- mute, solo, names; erasing by dragging ---------------------------------------------------

void testMuteSoloAndNames() {
    Rig rig;
    addPhazed(rig);
    Session& session = rig.app.session();
    for (int s = 0; s < kSteps; s += 4) rig.app.drums().setStep(0, s, 1.f);
    // The Phazed track holds one long note, so it sounds all through the bar.
    rig.project().track = 1;
    session.view = View::Pattern;
    rig.app.notes().moveCursorTo(60, 0);
    rig.project().playing = true; // as if playing, so that placing is silent
    rig.tap(A);
    rig.project().playing = false;
    rig.project().tracks[1].notes[0][0].notes[0].length = 16.f;
    session.sendNotePattern(1, 0);
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(START);
    rig.render(kBar);
    const auto levels = [&](float& drums, float& synth) {
        rig.frames(1);
        rig.render(4 * kStep);
        drums = rig.engine->trackPeak(0);
        synth = rig.engine->trackPeak(1);
        rig.render(200);
    };
    float d = 0.f, p = 0.f;
    levels(d, p);
    CHECK(d > 0.05f && p > 0.01f && session.heard(0) && session.heard(1));
    // Y on a track: muted, at once. Again: back.
    rig.tap(Y);
    CHECK(rig.project().tracks[0].muted && !session.heard(0) && session.heard(1));
    rig.render(2000); // the fade
    levels(d, p);
    CHECK(d == 0.f && p > 0.01f);
    rig.tap(Y);
    rig.render(2000);
    levels(d, p);
    CHECK(d > 0.05f && !rig.project().tracks[0].muted);
    // L1: soloed. Only it is heard, and a muted track that is soloed is heard too.
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(L1);
    CHECK(rig.project().tracks[1].solo && session.heard(1) && !session.heard(0));
    rig.render(2000);
    levels(d, p);
    CHECK(d == 0.f && p > 0.01f);
    rig.tap(Y); // muted as well: solo still wins while it is on
    CHECK(session.heard(1));
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(L1); // both soloed: both heard
    CHECK(session.heard(0) && session.heard(1));
    rig.tap(L1);
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(L1); // no solo left: the mute shows again
    CHECK(session.heard(0) && !session.heard(1));
    rig.render(2000);
    levels(d, p);
    CHECK(d > 0.05f && p == 0.f);
    rig.tap(Y);
    // Not on the repeat counts or + TRACK: nothing to mute there, and nothing breaks.
    rig.app.song().moveCursorTo(0, -1);
    rig.tap(Y);
    rig.tap(L1);
    rig.tap(X);
    rig.app.song().moveCursorTo(0, 2);
    rig.tap(Y);
    rig.tap(L1);
    CHECK(session.heard(0) && session.heard(1) && !rig.app.song().listingSongs());
    rig.tap(START);
    rig.render(96000);
    rig.frames(1);
    // X hears the track under the cursor: its kick, or a note.
    CHECK(rig.listen(2000) == 0.f);
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(X);
    CHECK(rig.listen(2000) > 0.05f && !rig.project().tracks[0].muted);
    rig.render(96000);
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(X);
    CHECK(rig.listen(4000) > 0.01f);

    // Names: made safe, ten letters at most, shown in place of the instrument's; empty puts that back.
    char text[24];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 PHAZED");
    session.setTrackName(1, "bass line!! and more");
    CHECK(std::string(rig.project().tracks[1].name) == "BASS LINE");
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 BASS LINE");
    session.setTrackName(5, "NOBODY"); // no such track yet
    CHECK(rig.project().tracks[5].name[0] == 0);
    // The song keeps the name and the mute, and not the solo.
    rig.project().tracks[0].muted = true;
    rig.project().tracks[1].solo = true;
    std::unique_ptr<Project> again(new Project), odd(new Project); // large: not on the stack
    CHECK(parseSong(songText(rig.project()), *again));
    CHECK(std::string(again->tracks[1].name) == "BASS LINE" && again->tracks[0].muted && !again->tracks[1].muted && !again->tracks[1].solo && again->tracks[0].name[0] == 0);
    CHECK(parseSong("apotheca-song 1\ntrack drums\nname   lower ../x  \nmuted\n", *odd));
    CHECK(odd->tracks[0].muted && odd->tracks[0].name[0] == 0); // nothing in that name is allowed, so there is none
    session.setTrackName(1, "");
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 PHAZED");
}

void testHoldBAndDragErases() {
    Rig rig;
    addPhazed(rig);
    rig.project().track = 1;
    rig.app.session().view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    const NotePattern& bar = rig.project().tracks[1].notes[0][0];
    rig.project().playing = true;
    notes.moveCursorTo(60, 0);
    rig.key(R1, true); // paint a row of eight
    rig.tap(DOWN, 7);
    rig.key(R1, false);
    notes.moveCursorTo(62, 3);
    rig.tap(A); // one off to the side
    const auto count = [&] { int n = 0; for (const NoteEvent& e : bar.notes) n += e.velocity > 0.f ? 1 : 0; return n; };
    CHECK(count() == 9);
    // A tap of B: the one under the cursor, as before.
    rig.tap(B);
    CHECK(count() == 8 && bar.at(62, 3) < 0);
    // B held and dragged down the row: each one crossed goes; ones not crossed stay.
    notes.moveCursorTo(60, 1);
    rig.key(B, true);
    CHECK(bar.at(60, 1) < 0 && count() == 7);
    rig.tap(DOWN, 3);
    CHECK(count() == 4 && bar.at(60, 0) >= 0 && bar.at(60, 4) < 0 && bar.at(60, 5) >= 0);
    rig.tap(RIGHT); // over empty steps: nothing to do, nothing goes wrong
    rig.tap(LEFT);
    CHECK(count() == 4);
    rig.key(B, false);
    // Let go: moving deletes nothing more.
    rig.tap(DOWN);
    CHECK(count() == 4 && bar.at(60, 5) >= 0);
    // The stick drags it too.
    rig.key(B, true);
    rig.key(STICK_DOWN, true);
    rig.frames(30);
    rig.key(STICK_DOWN, false);
    rig.key(B, false);
    CHECK(bar.at(60, 5) < 0 && bar.at(60, 6) < 0 && bar.at(60, 7) < 0);
    // With A held, B is not the eraser (A has hold of a note).
    notes.moveCursorTo(60, 0);
    rig.key(A, true);
    rig.key(B, true);
    rig.key(B, false);
    rig.key(A, false);
    CHECK(bar.at(60, 0) >= 0);
    rig.tap(DOWN);
    CHECK(bar.at(60, 0) >= 0);
    rig.project().playing = false;
}

// A button let go while another screen is showing must not stay "held" on the screen it went down on.
// (Found on the handheld: after a song was deleted and a new one begun, the grid behaved as if A were held.)
void testNothingStaysHeld() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Pattern;
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(2, 4);
    rig.frames(1);
    // A goes down on the grid; the screen changes under it (as when a song is opened); A comes up elsewhere.
    rig.key(A, true);
    session.view = View::Song;
    rig.frames(1);
    rig.key(A, false);
    session.view = View::Pattern;
    rig.frames(1);
    drums.setStep(2, 4, 0.f);
    drums.setStep(2, 5, 0.f);
    rig.tap(RIGHT); // with A "held" this would nudge the hit; it must simply move the cursor
    CHECK(drums.cursorStep() == 5 && drums.level(2, 4) == 0.f && drums.level(2, 5) == 0.f);
    // The same for the brush (R1) and the eraser (B).
    rig.key(R1, true);
    session.view = View::Song;
    rig.frames(1);
    rig.key(R1, false);
    session.view = View::Pattern;
    rig.frames(1);
    CHECK(!drums.painting());
    drums.setStep(2, 5, 0.f);
    rig.tap(RIGHT);
    CHECK(drums.level(2, 6) == 0.f);
    drums.setStep(2, 7, 0.9f);
    rig.key(B, true);
    session.view = View::Song;
    rig.frames(1);
    rig.key(B, false);
    session.view = View::Pattern;
    rig.frames(1);
    rig.tap(RIGHT);
    CHECK(drums.cursorStep() == 7 && drums.level(2, 7) > 0.f); // not rubbed out in passing
    // And on the piano roll: A, R1, X and B.
    addPhazed(rig);
    rig.project().track = 1;
    session.view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    const NotePattern& bar = rig.project().tracks[1].notes[0][0];
    rig.project().playing = true;
    notes.moveCursorTo(60, 0);
    rig.frames(1);
    for (SDL_Keycode held : {A, R1, X, B}) {
        notes.moveCursorTo(60, 0);
        rig.key(held, true);
        session.view = View::Song;
        rig.frames(1);
        rig.key(held, false);
        session.view = View::Pattern;
        rig.frames(1);
        const int before = static_cast<int>(std::count_if(std::begin(bar.notes), std::end(bar.notes), [](const NoteEvent& n) { return n.velocity > 0.f; }));
        rig.tap(DOWN, 2);
        const int after = static_cast<int>(std::count_if(std::begin(bar.notes), std::end(bar.notes), [](const NoteEvent& n) { return n.velocity > 0.f; }));
        CHECK(notes.cursorStep() == 2 && after == before && !notes.carrying()); // the cursor moved, and nothing was painted, carried or erased
    }
    rig.project().playing = false;
    // A whole new song: the grid is not "holding" anything when it comes up.
    session.view = View::Pattern;
    rig.project().track = 0;
    rig.key(A, true);
    session.newSong();
    session.view = View::Song;
    rig.frames(1);
    rig.key(A, false);
    session.view = View::Pattern;
    rig.frames(1);
    drums.moveCursorTo(1, 1);
    rig.tap(RIGHT, 3);
    CHECK(drums.cursorStep() == 4);
    for (int step = 0; step < kSteps; ++step) CHECK(drums.level(1, step) == 0.f);
}

void testDeletingATrack() {
    Rig rig;
    Session& session = rig.app.session();
    // Four tracks, each with something of its own: drums, Phazed (named), Twin, QMOD.
    rig.app.drums().setStep(0, 0, 1.f);
    session.addTrack(TrackKind::Phazed);
    session.addTrack(TrackKind::Twin);
    session.addTrack(TrackKind::Qmod);
    session.setTrackName(1, "BASS");
    rig.project().tracks[2].notes[0][0].notes[0] = NoteEvent{4, 62, 2.f, 0.f, 0.8f};
    rig.project().tracks[3].notes[1][0].notes[0] = NoteEvent{0, 50, 1.f, 0.f, 0.7f};
    session.setSynthSetting(3, qmod::Sound, 4321);
    session.setAutomation(2, 0, 3, 5, 0.25f);
    session.setMuted(3, true);
    rig.project().track = 3;
    CHECK(rig.project().trackCount() == 4);
    // A tap of B on a cell empties the cell and no more.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(B);
    rig.frames(200);
    CHECK(rig.project().trackCount() == 4 && rig.app.song().trackDeletePart() == 0.f);
    // Held: nothing for half a second, then the warning fills; let go before it is full and the track stays.
    rig.key(B, true);
    rig.frames(25);
    CHECK(rig.app.song().trackDeletePart() == 0.f);
    rig.frames(60);
    CHECK(rig.app.song().trackDeletePart() > 0.4f && rig.project().trackCount() == 4);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 4 && rig.app.song().trackDeletePart() == 0.f);
    // Held for the whole two seconds: the track goes, and the ones after it move down with everything they had.
    rig.key(B, true);
    rig.frames(130);
    rig.key(B, false);
    CHECK(rig.project().trackCount() == 3);
    CHECK(rig.project().tracks[0].kind == TrackKind::Drums && rig.project().tracks[1].kind == TrackKind::Twin && rig.project().tracks[2].kind == TrackKind::Qmod && rig.project().tracks[3].kind == TrackKind::None);
    CHECK(rig.project().tracks[1].name[0] == 0 && rig.project().tracks[1].notes[0][0].at(62, 4) >= 0 && near(rig.project().tracks[1].automation[0][3][5], 0.25f));
    CHECK(rig.project().tracks[2].notes[1][0].at(50, 0) >= 0 && rig.project().tracks[2].qmod.value[qmod::Sound] == 4321 && rig.project().tracks[2].muted);
    CHECK(rig.project().tracks[0].drums[0][0].level[0][0] == 1.f && rig.project().track == 2 && !rig.project().playing);
    for (float v : rig.project().tracks[3].automation[0][3]) CHECK(v < 0.f); // the freed place is clean
    // The engine has them in their new places: the Twin note sounds from track 2, and the kick still plays.
    rig.render(96000);
    session.setMuted(2, false);
    session.view = View::Pattern;
    rig.project().track = 0;
    rig.tap(START);
    rig.render(4 * kStep);
    CHECK(rig.engine->trackPeak(0) > 0.05f);
    rig.render(3 * kStep);
    CHECK(rig.engine->trackPeak(1) > 0.005f && rig.engine->trackPeak(3) == 0.f);
    rig.tap(START);
    rig.render(96000);
    // Not the last one left, and not one that is not there.
    CHECK(session.removeTrack(2) && session.removeTrack(1) && rig.project().trackCount() == 1);
    CHECK(!session.removeTrack(0) && !session.removeTrack(1) && !session.removeTrack(-1) && rig.project().trackCount() == 1);
    CHECK(rig.project().track == 0 && rig.project().tracks[0].kind == TrackKind::Drums);
    // A new track can be added in the freed place and plays.
    CHECK(session.addTrack(TrackKind::Phazed));
    session.audition(1, 60, 0.8f);
    CHECK(rig.listen(4000) > 0.01f);
}

void testStepsInAScene() {
    Rig rig;
    Session& session = rig.app.session();
    CHECK(rig.project().steps[0] == 0 && rig.project().sceneSteps(0) == kSteps); // 0 is "all of them"
    // On a scene's repeat count, with A held: left and right are how many steps it lasts.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, -1);
    rig.key(A, true);
    rig.tap(LEFT, 3);
    CHECK(rig.project().steps[0] == 13 && rig.project().repeats[0] == 1);
    rig.tap(RIGHT, 9);
    CHECK(rig.project().sceneSteps(0) == 16 && rig.project().steps[0] == 0); // every step of its one bar is the most
    rig.tap(LEFT, 40);
    CHECK(rig.project().steps[0] == 1);  // and one the fewest
    rig.tap(RIGHT, 12);
    rig.tap(UP); // up and down are still the repeat count
    rig.key(A, false);
    CHECK(rig.project().steps[0] == 13 && rig.project().repeats[0] == 2);
    // The engine has it: a kick on step 1 comes round every thirteen steps.
    rig.app.drums().setStep(0, 0, 1.f);
    session.view = View::Pattern;
    rig.tap(START);
    const std::vector<float> out = rig.render(28 * kStep);
    const auto starts = [&](int s) { float p = 0.f; for (int i = 0; i < 400; ++i) p = std::fmax(p, std::fabs(out[2 * (s * kStep + i)])); return p > 0.3f; };
    CHECK(starts(0) && starts(13) && starts(26) && !starts(16));
    rig.tap(START);
    rig.render(96000);
    // The song keeps it; undo takes it back; a bounce is as long as the scene really is.
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(songText(rig.project()), *again));
    CHECK(again->steps[0] == 13 && again->steps[1] == 0 && songText(rig.project()).find("\nlength 13 0 ") != std::string::npos);
    // A song kept before a scene had a length said "steps", to a bar: its bars but the last are whole.
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong("apotheca-song 1\nsteps 0 99 7\ntrack drums\nscene 3\nbars 2\n", *odd));
    CHECK(odd->steps[0] == 1 && odd->steps[1] == 0 && odd->steps[2] == 16 + 7 && odd->steps[3] == 0);
    // A length of every step, or nonsense, is "all of them".
    std::unique_ptr<Project> whole(new Project);
    CHECK(parseSong("apotheca-song 1\nlength 16 999 -4 5\ntrack drums\n", *whole));
    CHECK(whole->steps[0] == 0 && whole->steps[1] == 0 && whole->steps[2] == 0 && whole->steps[3] == 5);
    CHECK(songText(*odd).find("steps") == std::string::npos);
    CHECK(songFrames(rig.project(), 48000.f) == 2 * 13 * kStep);
    rig.frames(60);
    const int stepsBefore = session.undoSteps();
    session.setSceneSteps(0, 9);
    rig.frames(60);
    CHECK(session.undoSteps() == stepsBefore + 1 && session.undo() && rig.project().steps[0] == 13);
    session.setSceneSteps(kMaxScenes, 4);
    session.setSceneSteps(-1, 4);
    CHECK(rig.project().steps[0] == 13);
}

void testTheBattery() {
    char pattern[] = "/tmp/apotheca-battery-XXXXXX";
    const std::string root = mkdtemp(pattern);
    bool charging = true;
    CHECK(batteryPercent(&charging, root) == -1 && !charging);           // nothing there: a desk computer
    CHECK(batteryPercent(nullptr, root + "/no-such-folder") == -1);
    mkdir((root + "/ac").c_str(), 0755);                                  // a supply with no "capacity" is not a battery
    writeTextFile(root + "/ac/online", "1\n");
    CHECK(batteryPercent(&charging, root) == -1);
    mkdir((root + "/battery").c_str(), 0755);
    writeTextFile(root + "/battery/capacity", "73\n");
    writeTextFile(root + "/battery/status", "Discharging\n");
    CHECK(batteryPercent(&charging, root) == 73 && !charging);
    writeTextFile(root + "/battery/status", "Charging\n");
    CHECK(batteryPercent(&charging, root) == 73 && charging);
    writeTextFile(root + "/battery/capacity", "nonsense\n");
    CHECK(batteryPercent(&charging, root) == -1);
    writeTextFile(root + "/battery/capacity", "250\n");
    CHECK(batteryPercent(&charging, root) == -1);
    writeTextFile(root + "/battery/capacity", "0\n");
    CHECK(batteryPercent(nullptr, root) == 0);
}

// Bouncing: the song, or its tracks, written out as WAV files by a second engine.
void testBouncing() {
    char pattern[] = "/tmp/apotheca-bounce-XXXXXX";
    const std::string folder = mkdtemp(pattern);
    const auto wavsIn = [&] {
        std::vector<std::string> names;
        if (DIR* dir = opendir(folder.c_str())) {
            while (const dirent* e = readdir(dir)) {
                const std::string n = e->d_name;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".wav") names.push_back(n);
            }
            closedir(dir);
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    struct Wav { int rate = 0, channels = 0, bits = 0; std::vector<float> left; };
    const auto read = [&](const std::string& name) {
        Wav w;
        std::string bytes;
        if (!readTextFile(folder + "/" + name, bytes) || bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 8, "WAVEfmt ") != 0) return w;
        const auto u = [&](std::size_t at, int n) { unsigned v = 0; for (int i = 0; i < n; ++i) v |= static_cast<unsigned>(static_cast<unsigned char>(bytes[at + i])) << (8 * i); return v; };
        w.channels = static_cast<int>(u(22, 2));
        w.rate = static_cast<int>(u(24, 4));
        w.bits = static_cast<int>(u(34, 2));
        const std::size_t data = u(40, 4);
        if (bytes.compare(36, 4, "data") != 0 || data + 44 != bytes.size()) return w;
        for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) w.left.push_back(static_cast<float>(static_cast<std::int16_t>(u(at, 2))) / 32767.f);
        return w;
    };
    const auto peakIn = [](const Wav& w, std::size_t from, std::size_t to) { float p = 0.f; for (std::size_t i = from; i < to && i < w.left.size(); ++i) p = std::fmax(p, std::fabs(w.left[i])); return p; };

    Rig rig;
    Session& session = rig.app.session();
    // Nowhere to put it: no bounce.
    CHECK(!session.startBounce(Session::Bounce::Song) && !session.bouncing() && session.bounceFiles() == 0);
    session.useBounceFolder(folder);
    // A song: scene 1 (two bars' worth: it plays twice) with a kick on each beat, and a Twin track with one long note.
    for (int step = 0; step < kSteps; step += 4) rig.app.drums().setStep(0, step, 1.f);
    rig.project().repeats[0] = 2;
    session.addTrack(TrackKind::Twin);
    NotePattern& bar = rig.project().tracks[1].notes[0][0];
    bar.notes[0].step = 8;
    bar.notes[0].pitch = 57;
    bar.notes[0].length = 4.f;
    bar.notes[0].velocity = 0.8f;
    session.sendNotePattern(1, 0);
    session.setTrackName(1, "LEAD");
    const int oversample = rig.project().tracks[1].twin.value[twin::Oversample];
    rig.frames(2);

    // The whole song: one file, as long as the song and three seconds over, with sound where the song has it.
    CHECK(session.startBounce(Session::Bounce::Song));
    CHECK(!session.startBounce(Session::Bounce::Song)); // one at a time
    session.finishBounce();
    CHECK(!session.bouncing() && session.bounceProgress() == 1.f && session.bounceFiles() == 1);
    std::vector<std::string> names = wavsIn();
    CHECK(names.size() == 1 && names[0] == session.bounceLast());
    const std::size_t body = static_cast<std::size_t>(2 * kBar), tail = 3 * 48000;
    Wav song = read(names[0]);
    CHECK(song.rate == 48000 && song.channels == 2 && song.bits == 16 && song.left.size() == body + tail);
    CHECK(peakIn(song, 0, 2000) > 0.2f);                                  // the first kick, at the very start
    CHECK(peakIn(song, kBar, kBar + 2000) > 0.2f);                        // and the scene's second time round
    CHECK(peakIn(song, 8 * kStep + 500, 11 * kStep) > 0.01f);             // the Twin note
    CHECK(peakIn(song, body + 2 * 48000, body + tail) < 0.001f);          // the tail has died away: nothing started again
    CHECK(peakIn(song, body + 4000, body + 24000) < peakIn(song, 0, 2000)); // no kick after the song's end
    // The song itself was only read: Twin's own oversampling setting is as it was, and nothing is playing.
    CHECK(rig.project().tracks[1].twin.value[twin::Oversample] == oversample && !rig.project().playing);

    // Each track: a file apiece, named for its track, and each has only its own sound.
    sleep(1); // the files are named by the second
    CHECK(session.startBounce(Session::Bounce::EachTrack));
    session.finishBounce();
    CHECK(session.bounceFiles() == 2);
    names = wavsIn();
    CHECK(names.size() == 3);
    std::string drums, lead;
    for (const std::string& n : names) {
        if (n.find(" - 1 DRUMS.wav") != std::string::npos) drums = n;
        if (n.find(" - 2 LEAD.wav") != std::string::npos) lead = n;
    }
    CHECK(!drums.empty() && !lead.empty());
    const Wav d = read(drums), l = read(lead);
    CHECK(d.left.size() == body + tail && l.left.size() == body + tail);
    CHECK(peakIn(d, 0, 2000) > 0.2f && peakIn(l, 0, 2000) == 0.f);                 // the kick is in the drums' file only
    CHECK(peakIn(l, 8 * kStep + 500, 11 * kStep) > 0.01f && peakIn(l, 4 * kStep, 4 * kStep + 2000) == 0.f); // the note is in the lead's file, and no kick on the second beat either
    // A muted track is left out of a song bounce, and still gets its own file when asked for.
    session.setMuted(0, true);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    Wav quiet = read(session.bounceLast());
    CHECK(quiet.left.size() == body + tail && peakIn(quiet, 0, 2000) == 0.f && peakIn(quiet, 8 * kStep + 500, 11 * kStep) > 0.01f);
    rig.project().track = 0;
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::ThisTrack));
    session.finishBounce();
    CHECK(session.bounceFiles() == 1 && session.bounceLast().find(" - 1 DRUMS.wav") != std::string::npos);
    CHECK(peakIn(read(session.bounceLast()), 0, 2000) > 0.2f && rig.project().tracks[0].muted);
    // The handheld's own engine was never touched: it is silent, and plays as before when asked.
    CHECK(rig.listen(4000) == 0.f);
    // From the SET screen: the row picks what, A starts it, and the row says how far along it is.
    session.setMuted(0, false);
    session.view = View::Settings;
    SettingsScreen& setup = rig.app.settings();
    setup.goTo(SettingsScreen::kBounce);
    rig.tap(RIGHT, 2);
    rig.tap(LEFT);
    sleep(1);
    rig.tap(A); // EACH TRACK
    CHECK(session.bouncing() || session.bounceFiles() == 2);
    session.finishBounce();
    CHECK(session.bounceFiles() == 2);
    // Cancelled part-way (as when Apotheca is closed): it stops, and no half-written file is left.
    const std::size_t before = wavsIn().size();
    rig.project().repeats[0] = 16;
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.cancelBounce();
    CHECK(!session.bouncing());
    for (const std::string& n : wavsIn()) CHECK(read(n).left.size() >= body + tail); // every file there is whole
    CHECK(wavsIn().size() <= before + 1);
}

// Automation: with WRITE on and the song playing, a knob moved is written into the pattern.
void testRecordingAutomation() {
    Rig rig;
    addPhazed(rig);
    rig.project().track = 1;
    Session& session = rig.app.session();
    TrackData& track = rig.project().tracks[1];
    session.view = View::Device;
    rig.frames(1);
    const auto written = [&](int lane, int scene = 0) { int n = 0; for (float v : track.automation[scene][lane]) n += v >= 0.f ? 1 : 0; return n; };
    // SELECT + X turns WRITE on and off, and says so.
    CHECK(!session.writing());
    rig.key(SELECT, true);
    rig.tap(X);
    rig.key(SELECT, false);
    rig.frames(1);
    CHECK(session.writing() && rig.project().writing && std::string(rig.app.undoSaid()) == "WRITE ON: MOVES ARE RECORDED" && rig.view() == View::Device);
    // Stopped, a move is only a move: nothing is written, however WRITE stands.
    session.setSynthMacro(1, 0, 0.8f);
    CHECK(near(track.macro[0], 0.8f) && written(0) == 0 && !session.hasAutomation(1, 0, 0));
    session.setSynthMacro(1, 0, 0.5f);
    // Playing: the value lands on the step that is playing, in the cell that is playing.
    rig.tap(START);
    rig.render(3 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playStep == 3);
    session.setSynthMacro(1, 0, 0.9f);
    CHECK(written(0) == 1 && near(track.automation[0][0][3], 0.9f) && session.hasAutomation(1, 0, 0) && !session.hasAutomation(1, 0, 1));
    rig.render(kStep);
    rig.frames(1);
    session.setSynthMacro(1, 0, 0.7f);
    session.setSynthMacro(1, 0, 0.6f); // twice in one step: the last word stands
    CHECK(written(0) == 2 && near(track.automation[0][0][4], 0.6f));
    // The screen's own way of turning a macro records too (hold A, D-pad up, on a macro).
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro + 2);
    rig.render(kStep);
    rig.frames(1);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(written(2) == 1 && track.automation[0][2][5] > 0.5f);
    // While it plays, an automated macro reads as the pattern has it on this step; one that is not reads as its knob.
    CHECK(near(session.automated(1, 2, 0.123f), track.automation[0][2][5]) && near(session.automated(1, 4, 0.123f), 0.123f));
    // B while it plays with WRITE on: the macro goes back to the middle and its lane is cleared, and that
    // reset is not itself written down as a move.
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro + 2);
    rig.tap(B);
    CHECK(written(2) == 0 && near(track.macro[2], 0.5f) && session.writing());
    rig.render(kStep);
    rig.frames(1);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(written(2) == 1);
    // WRITE off: moves are only moves again, even while playing.
    rig.key(SELECT, true);
    rig.tap(X);
    rig.key(SELECT, false);
    CHECK(!session.writing() && std::string(rig.app.undoSaid()) == "WRITE OFF");
    rig.render(kStep);
    rig.frames(1);
    session.setSynthMacro(1, 0, 0.2f);
    CHECK(written(0) == 2 && near(track.macro[0], 0.2f));
    // An effect's setting on the track is recorded the same way; the master's effects are not automated.
    session.setWriting(true);
    session.setEffect(1, 0, EffectKind::Filter);
    session.setEffect(kMasterTrack, 0, EffectKind::Filter);
    rig.render(kStep);
    rig.frames(1);
    const int step = rig.project().playStep;
    session.setEffectParam(1, 0, 0, 0.33f);
    session.setEffectParam(kMasterTrack, 0, 0, 0.33f);
    const int cutoff = autoLaneOfEffect(0, 0);
    CHECK(written(cutoff) == 1 && near(track.automation[0][cutoff][step], 0.33f));
    for (int t = 0; t < kNumTracks; ++t)
        if (t != 1)
            for (int lane = 0; lane < kAutoLanes; ++lane)
                for (float v : rig.project().tracks[t].automation[0][lane]) CHECK(v < 0.f);
    rig.tap(START);
    rig.render(96000);
    rig.frames(1);
    CHECK(near(session.automated(1, 0, 0.5f), 0.5f)); // stopped: every reading is the knob's

    // B on a macro: back to the middle, and its automation in this cell goes (and that reset is not itself recorded).
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro);
    rig.tap(B);
    CHECK(written(0) == 0 && near(track.macro[0], 0.5f) && written(2) == 1 && session.writing());
    // B on an effect's setting: its default, and its automation goes.
    session.view = View::Effects;
    rig.app.fx().goTo(0, 1);
    rig.frames(1);
    rig.tap(B);
    CHECK(written(cutoff) == 0 && near(session.effectData(1, 0).param[0], effectInfo(EffectKind::Filter).defaults[0]));
    // Another effect put in the slot: what was written for the old one's settings is dropped, in every scene.
    session.setAutomation(1, 0, autoLaneOfEffect(0, 1), 2, 0.5f);
    session.setAutomation(1, 5, autoLaneOfEffect(0, 2), 9, 0.5f);
    session.setAutomation(1, 5, autoLaneOfEffect(1, 2), 9, 0.5f); // the other slot's: kept
    session.setEffect(1, 0, EffectKind::Delay);
    CHECK(written(autoLaneOfEffect(0, 1)) == 0 && written(autoLaneOfEffect(0, 2), 5) == 0 && written(autoLaneOfEffect(1, 2), 5) == 1);

    // Out of range is nothing; values are held to 0..1; below zero unwrites.
    session.setAutomation(1, 0, kAutoLanes, 0, 0.5f);
    session.setAutomation(1, 0, 3, kAutoSteps, 0.5f);
    session.setAutomation(99, 0, 3, 0, 0.5f);
    session.setAutomation(1, kMaxScenes, 3, 0, 0.5f);
    session.setAutomation(1, 0, 3, 0, std::nanf(""));
    CHECK(written(3) == 0);
    session.setAutomation(1, 0, 3, 0, 7.f);
    CHECK(track.automation[0][3][0] == 1.f);
    session.setAutomation(1, 0, 3, 0, -3.f);
    CHECK(written(3) == 0);

    // The song keeps it, and a song without any is none.
    session.setAutomation(1, 2, 7, 40, 0.25f);
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(songText(rig.project()), *again));
    CHECK(near(again->tracks[1].automation[0][2][5], track.automation[0][2][5]) && near(again->tracks[1].automation[2][7][40], 0.25f));
    int kept = 0, here = 0;
    for (const auto& scene : again->tracks[1].automation) for (const auto& lane : scene) for (float v : lane) kept += v >= 0.f ? 1 : 0;
    for (const auto& scene : track.automation) for (const auto& lane : scene) for (float v : lane) here += v >= 0.f ? 1 : 0;
    CHECK(kept == here && kept >= 3);
    for (const auto& scene : again->tracks[0].automation) for (const auto& lane : scene) for (float v : lane) CHECK(v < 0.f);
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nauto 1 2 3 0.5 999 0.5 4 nonsense\nauto 99 2 3 0.5\nauto 1 99 3 0.5\nauto\n", *odd));
    CHECK(near(odd->tracks[0].automation[0][2][3], 0.5f));
    int oddOnes = 0;
    for (const auto& scene : odd->tracks[0].automation) for (const auto& lane : scene) for (float v : lane) oddOnes += v >= 0.f ? 1 : 0;
    CHECK(oddOnes == 1);

    // Undo covers it: a recorded pass is a step, and comes back.
    rig.frames(60);
    const int steps = session.undoSteps();
    session.clearAutomation(1, 0, 2);
    rig.frames(60);
    CHECK(written(2) == 0 && session.undoSteps() == steps + 1);
    CHECK(session.undo() && written(2) == 1);

    // Writing is itself a step of undo: one recorded move, taken back.
    rig.frames(60);
    const int beforeMove = session.undoSteps();
    session.setAutomation(1, 0, 6, 3, 0.4f);
    rig.frames(60);
    CHECK(session.undoSteps() == beforeMove + 1 && written(6) == 1);
    CHECK(session.undo() && written(6) == 0);

    // And the engine plays it: OCTAVE (macro 7) written up for steps 9 to 12 is heard there and not before.
    // (Phazed's octave macro jumps at three quarters; a long plain note makes it countable.)
    Rig heard;
    heard.app.session().addTrack(TrackKind::Qmod);
    heard.project().track = 1;
    Session& hs = heard.app.session();
    hs.setSynthSetting(1, qmod::Tension, 0);
    hs.setSynthSetting(1, qmod::Sustain, 100);
    NotePattern& bar = heard.project().tracks[1].notes[0][0];
    bar.notes[0].step = 0;
    bar.notes[0].pitch = 45;
    bar.notes[0].length = 16.f;
    bar.notes[0].velocity = 0.8f;
    hs.sendNotePattern(1, 0);
    for (int i = 8; i < 12; ++i) hs.setAutomation(1, 0, qmod::MacroOctave, i, 1.f);
    hs.view = View::Pattern;
    heard.tap(START);
    const auto count = [&] { const std::vector<float> got = heard.render(kStep); int n = 0; for (int i = 1; i < kStep; ++i) n += got[2 * (i - 1)] < 0.f && got[2 * i] >= 0.f ? 1 : 0; return n; };
    std::vector<int> waves;
    for (int i = 0; i < 16; ++i) waves.push_back(count());
    CHECK(waves[4] >= 12 && std::abs(waves[10] - 2 * waves[4]) <= 2 && std::abs(waves[14] - waves[4]) <= 1);
    // Taken away and brought back by undo, it is the engine that gets it back, not only the screen.
    heard.tap(START);
    heard.render(96000);
    heard.frames(60);
    hs.clearAutomation(1, 0, qmod::MacroOctave);
    heard.frames(60);
    heard.tap(START);
    waves.clear();
    for (int i = 0; i < 16; ++i) waves.push_back(count());
    CHECK(std::abs(waves[10] - waves[4]) <= 1); // cleared: no octave
    heard.tap(START);
    heard.render(96000);
    CHECK(hs.undo());
    heard.tap(START);
    waves.clear();
    for (int i = 0; i < 16; ++i) waves.push_back(count());
    CHECK(std::abs(waves[10] - 2 * waves[4]) <= 2 && std::abs(waves[14] - waves[4]) <= 1);
}

// A setting that is a choice clicks from one to the next: one per push of the stick, as with the D-pad.
void testChoicesClick() {
    Rig rig;
    Session& session = rig.app.session();
    session.setEffect(0, 0, EffectKind::Filter);
    session.view = View::Effects;
    rig.app.fx().goTo(0, 3); // the filter's TYPE
    rig.frames(1);
    const auto type = [&] { return filterType(session.effectData(0, 0).param[2]); };
    CHECK(type() == FilterType::LowPass2);
    // One push of the stick, however brief: the next type, exactly.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(type() == FilterType::LowPass4);
    rig.key(STICK_UP, true);
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(type() == FilterType::BandPass);
    // Held over: on through them steadily, a few a second, and it stops at the end.
    rig.key(STICK_UP, true);
    rig.frames(12); // a fifth of a second: not yet a second click
    CHECK(type() == FilterType::Dj);
    rig.frames(120);
    CHECK(type() == FilterType::HighPass);
    rig.key(STICK_UP, false);
    rig.frames(1); // the stick passes through the middle on its way down
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(type() == FilterType::Dj);
    rig.tap(DOWN); // the D-pad: the same click
    CHECK(type() == FilterType::BandPass);
    // A setting that is an amount still turns smoothly under the stick.
    rig.app.fx().goTo(0, 1); // CUTOFF
    const float before = session.effectData(0, 0).param[0];
    rig.key(STICK_UP, true);
    rig.frames(10);
    rig.key(STICK_UP, false);
    const float moved = session.effectData(0, 0).param[0] - before;
    CHECK(moved > 0.02f && moved < 0.3f);
    rig.key(A, false);

    // On a synth's pages: a wave is a choice, a level is a range.
    addPhazed(rig);
    rig.project().track = 1;
    session.view = View::Device;
    SynthScreen& device = rig.app.phazed();
    const SynthSpec& spec = phazedSpec();
    int wavePage = -1, waveItem = -1, waveId = -1;
    for (int page = 1; page < spec.pages && wavePage < 0; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = 0;
            const char* name = "";
            spec.item(page, i, id, name);
            if (std::string(name) == "WAVE") { wavePage = page; waveItem = i; waveId = id; break; }
        }
    CHECK(wavePage > 0);
    device.goTo(wavePage, waveItem);
    rig.frames(1);
    const int wave = rig.project().tracks[1].patch.value[waveId];
    const int next = wave < spec.settingMax(waveId) ? 1 : -1;
    rig.key(A, true);
    rig.key(next > 0 ? STICK_UP : STICK_DOWN, true);
    rig.frames(1);
    rig.key(next > 0 ? STICK_UP : STICK_DOWN, false);
    rig.frames(1);
    CHECK(rig.project().tracks[1].patch.value[waveId] == wave + next); // one, not a sweep
    rig.key(next > 0 ? STICK_UP : STICK_DOWN, true);
    rig.frames(8);
    rig.key(next > 0 ? STICK_UP : STICK_DOWN, false);
    rig.frames(1);
    const int after = rig.project().tracks[1].patch.value[waveId];
    CHECK(std::abs(after - (wave + next)) <= 1); // an eighth of a second more is one click at most
    rig.key(A, false);
    // Letting go of A and taking hold again: the next push is a click again, at once.
    rig.key(A, true);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].patch.value[waveId] == after - 1 || after == spec.settingMin(waveId));
}

void testTheHelpCard() {
    const SDL_Keycode MENU = SDLK_TAB;
    Rig rig;
    rig.app.session().view = View::Pattern;
    rig.frames(2);
    CHECK(!rig.app.helpShowing() && std::string(ui::gHelpHint) == "MENU: HELP");
    // MENU held: the card, at once, for as long as it is held. Nothing else happens.
    rig.key(MENU, true);
    rig.frames(1);
    CHECK(rig.app.helpShowing());
    rig.frames(200);
    CHECK(rig.app.helpShowing() && rig.view() == View::Pattern && !rig.project().playing);
    rig.key(MENU, false);
    rig.frames(1);
    CHECK(!rig.app.helpShowing() && rig.view() == View::Pattern);
    // SELECT is left to its other jobs: held however long, no card, and let go it is still a tap.
    rig.key(SELECT, true);
    rig.frames(100);
    CHECK(!rig.app.helpShowing());
    rig.key(SELECT, false);
    rig.frames(1);
    CHECK(rig.view() == View::Song && !rig.app.helpShowing());
    // The buttons go on working under the card (it only covers the screen).
    rig.app.session().view = View::Pattern;
    rig.key(MENU, true);
    rig.frames(1);
    rig.key(SELECT, true);
    rig.tap(L1);
    rig.key(SELECT, false);
    CHECK(near(rig.app.session().volume(), 0.9f) && rig.app.helpShowing());
    rig.key(MENU, false);
    rig.frames(1);
    // Not over the button test or the name keys, which have every button to themselves.
    rig.app.session().view = View::Settings;
    rig.app.settings().goTo(SettingsScreen::kTest);
    rig.tap(A);
    rig.key(MENU, true);
    rig.frames(5);
    CHECK(!rig.app.helpShowing() && rig.app.settings().testing());
    rig.key(MENU, false);

    // A handheld whose firmware keeps MENU: it was left out of the controls setup. There,
    // SELECT held for four tenths of a second is the card, and the hint bar says so.
    Rig other;
    ControlMap map; // every button unset: only the keyboard stand-ins work, and there is no MENU
    other.input.useMap(&map);
    CHECK(!other.input.has(Button::Menu) && rig.input.has(Button::Menu));
    other.app.session().view = View::Device;
    other.frames(2);
    CHECK(std::string(ui::gHelpHint) == "HOLD SELECT: HELP");
    other.key(SELECT, true);
    other.frames(24);
    CHECK(!other.app.helpShowing());
    other.frames(3);
    CHECK(other.app.helpShowing());
    other.key(SELECT, false);
    other.frames(1);
    CHECK(!other.app.helpShowing() && other.view() == View::Device); // having shown it, letting go is not a tap
    other.tap(SELECT); // a quick tap still is
    CHECK(other.view() == View::Song);
    rig.frames(1); // back to a handheld with MENU, for the tests after this one
    CHECK(std::string(ui::gHelpHint) == "MENU: HELP");
}

void testUndoAndRedo() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Pattern;
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    CHECK(session.undoSteps() == 0 && !session.undo() && !session.redo());
    // Nothing to undo says so, and changes nothing.
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    CHECK(std::string(rig.app.undoSaid()) == "NOTHING TO UNDO" && rig.view() == View::Pattern);
    const auto hits = [&] { int n = 0; for (int l = 0; l < kDrumLanes; ++l) for (int s = 0; s < kSteps; ++s) n += drums.level(l, s) > 0.f ? 1 : 0; return n; };
    const auto settle = [&] { rig.frames(50); }; // 0.8 s with nothing changing: the burst is over

    // One burst of editing is one step: a painted row of eight.
    drums.moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.tap(RIGHT, 7);
    rig.key(R1, false);
    settle();
    CHECK(hits() == 8 && session.undoSteps() == 1);
    // A second burst: three hits on another lane, each a moment apart but inside the same burst.
    drums.moveCursorTo(2, 4);
    rig.tap(A);
    rig.frames(10);
    drums.moveCursorTo(2, 8);
    rig.tap(A);
    rig.frames(10);
    drums.moveCursorTo(2, 12);
    rig.tap(A);
    settle();
    CHECK(hits() == 11 && session.undoSteps() == 2);
    // Then the accident: the whole cell emptied from the SONG screen.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(B);
    settle();
    CHECK(hits() == 0 && session.undoSteps() == 3);
    session.view = View::Pattern;

    // SELECT + B: the beat is back, with every level as it was.
    rig.key(SELECT, true);
    rig.tap(B);
    CHECK(hits() == 11 && near(drums.level(0, 3), 0.8f) && drums.level(2, 8) > 0.f);
    CHECK(std::string(rig.app.undoSaid()) == "UNDONE  (2 MORE)");
    // Again: the three hits go. Again: the painted row goes.
    rig.tap(B);
    CHECK(hits() == 8);
    rig.tap(B);
    CHECK(hits() == 0 && session.undoSteps() == 0 && session.redoSteps() == 3);
    rig.tap(B);
    CHECK(std::string(rig.app.undoSaid()) == "NOTHING TO UNDO" && hits() == 0);
    // SELECT + A puts them forward again, one at a time.
    rig.tap(A);
    CHECK(hits() == 8 && std::string(rig.app.undoSaid()) == "PUT BACK  (2 MORE)");
    rig.tap(A);
    CHECK(hits() == 11);
    rig.key(SELECT, false);
    rig.frames(1);
    CHECK(rig.view() == View::Pattern && !rig.app.song().listingSongs()); // it was not a tap of SELECT
    // The notice goes after a moment.
    rig.frames(120);
    CHECK(rig.app.undoSaid()[0] == 0);
    // A new change after going back: there is no "forward" any more.
    CHECK(session.redoSteps() == 1);
    drums.moveCursorTo(5, 5);
    rig.tap(A);
    settle();
    CHECK(session.redoSteps() == 0 && hits() == 12);

    // The engine is told, and the song is not stopped: playing, undo takes a hit out of the bar that is going round.
    rig.tap(START);
    rig.render(100);
    rig.frames(1);
    CHECK(rig.project().playing);
    CHECK(session.undo());
    CHECK(rig.project().playing && hits() == 11 && drums.level(5, 5) == 0.f);
    rig.tap(START);
    rig.render(96000);
    rig.frames(1);
    // What plays is what is shown: lane 6 is silent now, lane 1 still has its row.
    for (int l = 0; l < kDrumLanes; ++l)
        for (int s = 0; s < kSteps; ++s)
            if (l != 2) drums.setStep(l, s, 0.f);
    settle();
    rig.tap(START);
    const std::vector<float> early = rig.render(4 * kStep - 100), then = rig.render(kStep);
    CHECK(Rig::peak(early) == 0.f && Rig::peak(then) > 0.05f); // lane 3's first hit is on step 5
    rig.tap(START);
    rig.render(96000);

    // Notes and chords, cell lengths and repeat counts come back too.
    addPhazed(rig);
    rig.project().track = 1;
    session.view = View::Pattern;
    rig.frames(1);
    settle();
    const int before = session.undoSteps();
    NoteScreen& notes = rig.app.notes();
    rig.project().playing = true;
    notes.moveCursorTo(60, 0);
    rig.key(R1, true);
    rig.tap(DOWN, 3);
    rig.key(R1, false);
    rig.project().playing = false;
    settle();
    session.setCellBars(1, 0, 4);
    settle();
    rig.project().repeats[0] = 3;
    session.noteEdit();
    settle();
    const NotePattern& bar = rig.project().tracks[1].notes[0][0];
    CHECK(session.undoSteps() == before + 3 && !bar.empty() && rig.project().tracks[1].bars[0] == 4);
    CHECK(session.undo() && rig.project().repeats[0] == 1);
    CHECK(session.undo() && rig.project().tracks[1].bars[0] == 1);
    CHECK(session.undo() && bar.empty());
    CHECK(session.redo() && bar.at(60, 2) >= 0 && bar.at(60, 3) >= 0);
    // Sounds and settings are not part of it: a preset chosen since stays chosen.
    rig.app.session().chooseSynthPreset(1, 4);
    CHECK(session.undo() && rig.project().tracks[1].preset == 4 && bar.empty());

    // Twenty deep and no deeper: the oldest steps fall off the end.
    session.view = View::Pattern;
    rig.project().track = 0;
    rig.frames(1);
    settle();
    for (int i = 0; i < 30; ++i) {
        drums.moveCursorTo(7, i % kSteps);
        rig.tap(A);
        settle();
    }
    CHECK(session.undoSteps() == Session::kUndoLevels);
    int undone = 0;
    while (session.undo()) ++undone;
    CHECK(undone == Session::kUndoLevels);
    // On the SONG screen too, SELECT + B is undo and not a tap of SELECT: the list of songs stays shut.
    drums.moveCursorTo(6, 1);
    rig.tap(A);
    settle();
    drums.moveCursorTo(6, 2);
    rig.tap(A);
    settle();
    session.view = View::Song;
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    rig.frames(1);
    CHECK(drums.level(6, 2) == 0.f && drums.level(6, 1) > 0.f && !rig.app.song().listingSongs() && session.undoSteps() == 1 && session.redoSteps() == 1);
    // Another song: nothing of this one to go back to, or forward to.
    session.newSong();
    rig.frames(2);
    CHECK(session.undoSteps() == 0 && session.redoSteps() == 0 && !session.undo());
}

void testHoldBErasesDrumHits() {
    Rig rig;
    rig.app.session().view = View::Pattern;
    PatternScreen& drums = rig.app.drums();
    for (int s = 0; s < kSteps; ++s) drums.setStep(3, s, 0.7f);
    drums.setStep(4, 5, 0.7f);
    // A tap: the one under the cursor.
    drums.moveCursorTo(3, 2);
    rig.tap(B);
    CHECK(drums.level(3, 2) == 0.f && drums.level(3, 3) > 0.f);
    // Held and dragged: every hit crossed, on whichever lane.
    drums.moveCursorTo(3, 4);
    rig.key(B, true);
    rig.tap(RIGHT, 3);
    CHECK(drums.level(3, 4) == 0.f && drums.level(3, 5) == 0.f && drums.level(3, 7) == 0.f && drums.level(3, 8) > 0.f);
    rig.tap(LEFT, 2); // back over the gap: nothing to do
    rig.tap(DOWN);    // onto the lane below, step 6: empty there
    rig.tap(LEFT);    // step 5 on that lane: its hit goes
    CHECK(drums.level(4, 5) == 0.f);
    rig.key(B, false);
    // Let go: moving deletes nothing.
    drums.moveCursorTo(3, 8);
    rig.tap(RIGHT, 3);
    CHECK(drums.level(3, 9) > 0.f && drums.level(3, 11) > 0.f);
    // The stick drags it too.
    rig.key(B, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(20);
    rig.key(STICK_RIGHT, false);
    rig.key(B, false);
    CHECK(drums.level(3, 12) == 0.f && drums.level(3, 13) == 0.f);
    // With A or Y held, B is not the eraser.
    drums.moveCursorTo(3, 0);
    rig.key(A, true);
    rig.tap(B);
    rig.key(A, false);
    CHECK(drums.level(3, 0) > 0.f);
}

void testQmodOnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    // + TRACK: QMOD is the fifth instrument, after the sampler.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 3);
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Qmod);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[24];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 QMOD" && isSynth(track.kind));
    // It starts on its first preset, and plays.
    const QmodPatch first = Qmod::presetPatch(0);
    CHECK(track.preset == 0 && std::equal(first.value, first.value + qmod::kSettings, track.qmod.value));
    session.audition(1, 60, 0.8f);
    CHECK(rig.listen(4000) > 0.02f);
    rig.render(96000);
    // The device screen: Y rolls a new sound, at once, and it is heard. Only the sound's numbers change.
    session.view = View::Device;
    rig.frames(2);
    const int before = track.qmod.value[qmod::Sound];
    rig.tap(Y);
    CHECK(track.qmod.value[qmod::Sound] != before && track.qmod.value[qmod::Sound] >= 1 && track.qmod.value[qmod::Sound] <= 9999);
    CHECK(track.qmod.value[qmod::Tension] == first.value[qmod::Tension] && track.qmod.value[qmod::Release] == first.value[qmod::Release]);
    CHECK(rig.listen(4000) > 0.02f);
    rig.render(96000);
    const int second = track.qmod.value[qmod::Sound];
    rig.tap(Y);
    CHECK(track.qmod.value[qmod::Sound] != second); // each roll is another
    // The engine has the rolled sound: it is what a fresh QMOD given the same numbers plays.
    {
        Qmod same;
        same.prepare(48000.f);
        for (int id = 0; id < qmod::kSettings; ++id) same.setSetting(id, track.qmod.value[id]);
        same.noteOn(60, 0.8f);
        std::vector<float> l(4000), r(4000);
        for (int at = 0; at < 4000; at += 64) same.render(l.data() + at, r.data() + at, 4000 - at < 64 ? 4000 - at : 64);
        rig.frames(200); // long enough for the note the roll played to be let go
        rig.render(96000);
        rig.render(96000);
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 60;
        c.value = 0.8f;
        rig.engine->send(c);
        const std::vector<float> heard = rig.render(4000);
        // The same wave, at whatever level the engine's own mix gives it: the two keep one ratio throughout.
        float top = 0.f, scale = 0.f, worst = 0.f;
        for (int i = 0; i < 4000; ++i)
            if (std::fabs(l[i]) > top) top = std::fabs(l[i]), scale = heard[2 * i] / l[i];
        for (int i = 0; i < 4000; ++i) worst = std::fmax(worst, std::fabs(heard[2 * i] - scale * l[i]));
        CHECK(top > 0.02f && scale > 0.2f && scale < 1.f && worst < 0.02f * top * scale);
        c.type = Command::NoteOff;
        rig.engine->send(c);
        rig.render(96000);
    }
    // Y does not roll on a synth that is not rolled (Phazed), nor while A has hold of something.
    addPhazed(rig);
    rig.project().track = 2;
    session.view = View::Device;
    const PhazedPatch phazed = rig.project().tracks[2].patch;
    rig.tap(Y);
    CHECK(std::equal(phazed.value, phazed.value + phazed::kSettings, rig.project().tracks[2].patch.value));
    CHECK(!session.rollSynth(2, 5) && !session.rollSynth(0, 5) && !session.rollSynth(99, 5));
    rig.project().track = 1;
    rig.frames(1);
    const int kept = track.qmod.value[qmod::Sound];
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    CHECK(track.qmod.value[qmod::Sound] == kept);
    // Its settings read as what they are.
    const SynthSpec& spec = qmodSpec();
    spec.settingText(qmod::Family, 0, text, sizeof text);
    CHECK(std::string(text) == "AUTO");
    spec.settingText(qmod::Family, 1, text, sizeof text);
    CHECK(std::string(text) == qmod::familyName(1) && text[0]);
    spec.settingText(qmod::Release, 100, text, sizeof text);
    CHECK(std::string(text) == "20.0 S");
    spec.macroText(qmod::MacroVariation, 1.f, text, sizeof text);
    CHECK(std::string(text) == "+16");
    // Every setting is on some page, once.
    int seen[qmod::kSettings] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < qmod::kSettings && name[0]);
            if (id >= 0 && id < qmod::kSettings) ++seen[id];
        }
    for (int count : seen) CHECK(count == 1);
    // A song keeps a QMOD track: its kind and every number of its sound.
    track.qmod.value[qmod::Tension] = 77;
    track.macro[qmod::MacroEvolve] = 0.9f;
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(songText(rig.project()), *again));
    CHECK(again->tracks[1].kind == TrackKind::Qmod && std::equal(track.qmod.value, track.qmod.value + qmod::kSettings, again->tracks[1].qmod.value));
    CHECK(near(again->tracks[1].macro[qmod::MacroEvolve], 0.9f) && again->tracks[2].kind == TrackKind::Phazed);
}

void testTypingATrackName() {
    Rig rig;
    rig.app.session().view = View::Settings;
    SettingsScreen& setup = rig.app.settings();
    setup.goTo(SettingsScreen::kTrackName);
    const char* name = rig.project().tracks[0].name;
    // A on TRACK NAME opens the keys, on A, with nothing typed.
    rig.tap(A);
    CHECK(setup.typing() && setup.typed().empty());
    // A types the key under the cursor; the D-pad moves over the grid and comes round at its edges.
    rig.tap(A); // A
    rig.tap(RIGHT);
    rig.tap(A); // B
    rig.tap(LEFT, 2); // off the left edge: round to J
    rig.tap(A);
    CHECK(setup.typed() == "ABJ");
    rig.tap(DOWN, 3); // the bottom row, under J: the last key, a space
    rig.tap(A);
    rig.tap(A); // two spaces together: only one is taken
    CHECK(setup.typed() == "ABJ ");
    rig.tap(DOWN); // off the bottom: round to the top
    rig.tap(A);
    CHECK(setup.typed() == "ABJ J");
    // B deletes the last one.
    rig.tap(B, 2);
    CHECK(setup.typed() == "ABJ" && setup.typing());
    // Ten at most.
    rig.tap(A, 20);
    CHECK(setup.typed() == "ABJJJJJJJJ");
    // While it is open nothing else happens: no tab, no SONG screen, no row moves; the name is not changed yet.
    rig.tap(L2);
    rig.tap(R2);
    rig.tap(SELECT);
    rig.tap(X);
    CHECK(rig.view() == View::Settings && setup.typing() && setup.cursorRow() == SettingsScreen::kTrackName && name[0] == 0);
    // Y keeps it.
    rig.tap(Y);
    CHECK(!setup.typing() && std::string(name) == "ABJJJJJJJJ");
    char shown[24];
    ui::trackName(rig.project(), 0, shown, sizeof shown);
    CHECK(std::string(shown) == "1 ABJJJJJJJJ");
    // Opened again it starts from the name there is. START keeps too (and does not play).
    rig.tap(A);
    CHECK(setup.typed() == "ABJJJJJJJJ");
    rig.tap(B, 8);
    rig.tap(START);
    CHECK(!setup.typing() && std::string(name) == "AB" && !rig.project().playing);
    // B with nothing typed leaves, and the name stays as it was.
    rig.tap(A);
    rig.tap(B, 2);
    CHECK(setup.typing() && setup.typed().empty());
    rig.tap(B);
    CHECK(!setup.typing() && std::string(name) == "AB");
    // Emptied and kept: the instrument's own name again. A space cannot come first.
    rig.tap(A);
    rig.tap(B, 2);
    rig.tap(UP); // the bottom row
    rig.tap(LEFT); // its last key: space
    rig.tap(A);
    CHECK(setup.typed().empty());
    rig.tap(Y);
    CHECK(name[0] == 0);
    // The stick moves over the keys too; left and right on the row still pick from the list when the keys are closed.
    rig.tap(A);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.tap(A);
    CHECK(setup.typed() == "B");
    rig.tap(B, 2);
    rig.tap(RIGHT);
    CHECK(std::string(name) == kTrackNames[1]);
    // The digits and marks are all there, and every key is one a name may have.
    const std::string keys = SettingsScreen::kKeys;
    CHECK(keys.size() == 40 && keys.find('Z') != std::string::npos && keys.find('9') != std::string::npos && keys.find('-') != std::string::npos);
    for (char c : keys) CHECK((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_');
}

void testHoldXCarriesANote() {
    Rig rig;
    addPhazed(rig);
    rig.project().track = 1;
    rig.app.session().view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    NotePattern& bar = rig.project().tracks[1].notes[0][0];
    rig.project().playing = true; // so that placing and moving are silent
    notes.moveCursorTo(60, 4);
    rig.tap(A);
    bar.notes[bar.at(60, 4)].length = 3.f;
    bar.notes[bar.at(60, 4)].velocity = 0.6f;
    notes.moveCursorTo(62, 5);
    rig.tap(A);
    rig.app.session().sendNotePattern(1, 0);
    // A tap of X only hears: nothing moves.
    notes.moveCursorTo(60, 5); // on the long note's second step
    rig.tap(X);
    rig.tap(RIGHT);
    CHECK(bar.at(60, 4) >= 0 && bar.at(61, 4) < 0 && notes.cursorPitch() == 61 && !notes.carrying());
    // Held on a note: it goes where the cursor goes, whole, with its length and level.
    notes.moveCursorTo(60, 5);
    rig.key(R1, true);
    CHECK(notes.carrying());
    rig.tap(LEFT, 2); // two keys down
    CHECK(bar.at(60, 4) < 0 && bar.at(58, 4) >= 0 && notes.cursorPitch() == 58 && notes.cursorStep() == 5);
    rig.tap(DOWN, 3); // three steps later
    int slot = bar.at(58, 7);
    CHECK(slot >= 0 && bar.notes[slot].step == 7 && bar.notes[slot].length == 3.f && near(bar.notes[slot].velocity, 0.6f) && notes.cursorStep() == 8);
    CHECK(bar.at(58, 4) < 0 && bar.at(58, 6) < 0);
    // It stops at the top and bottom of the bar; the cursor stays with it.
    rig.tap(UP, 20);
    CHECK(bar.notes[slot].step == 0 && notes.cursorStep() == 1);
    rig.tap(DOWN, 30);
    CHECK(bar.notes[slot].step == 14 && notes.cursorStep() == 15 && rig.project().bar == 0); // the cursor, a step along the note, is at the bar's end
    rig.tap(UP, 9);
    CHECK(bar.notes[slot].step == 5);
    // It will not land on another note: 62 at step 5 is in the way, so it stops beside it.
    rig.tap(RIGHT, 10);
    CHECK(bar.notes[slot].pitch == 61 && bar.at(62, 5) >= 0 && bar.at(62, 5) != slot);
    // Let go: the cursor moves alone again.
    rig.key(R1, false);
    CHECK(!notes.carrying());
    rig.tap(LEFT);
    CHECK(bar.notes[slot].pitch == 61 && notes.cursorPitch() == 60);
    // The stick, up and down, slides it FREELY in time, off the grid: a short push is a small move...
    // (The engine is run as the frames go by, as the sound card runs it: each frame of sliding tells it the bar again.)
    const auto hold = [&](int count) { for (int f = 0; f < count; ++f) { rig.frames(1); rig.render(16); } };
    notes.moveCursorTo(61, 5);
    rig.key(R1, true);
    rig.key(STICK_UP, true);
    hold(3);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(bar.notes[slot].step == 5 && bar.notes[slot].nudge < -0.01f && bar.notes[slot].nudge > -0.4f && notes.cursorStep() == 5);
    // ...and a longer one carries it on through the steps, the cursor with it, still between the lines.
    rig.key(STICK_DOWN, true);
    hold(40); // about four tenths of a second at the stick's speed: a few steps
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(bar.notes[slot].step >= 7 && bar.notes[slot].step <= 9 && notes.cursorStep() == bar.notes[slot].step);
    CHECK(std::fabs(bar.notes[slot].nudge) <= 0.5f && bar.notes[slot].pitch == 61);
    const float where = bar.notes[slot].step + bar.notes[slot].nudge;
    CHECK(std::fabs(where - std::round(where)) > 0.001f); // not on a line
    // The D-pad, with the same hold: a whole step, and what it had off the grid it keeps.
    const float off = bar.notes[slot].nudge;
    const int at = bar.notes[slot].step;
    rig.tap(UP);
    CHECK(bar.notes[slot].step == at - 1 && bar.notes[slot].nudge == off);
    // At the end of the bar it stops half a step late and goes no further; at the top, half a step early.
    rig.key(STICK_DOWN, true);
    hold(300);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(bar.notes[slot].step == 15 && near(bar.notes[slot].nudge, 0.5f) && notes.cursorStep() == 15);
    rig.key(STICK_UP, true);
    hold(300);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(bar.notes[slot].step == 0 && near(bar.notes[slot].nudge, -0.5f));
    // The stick sideways with X held is still key by key.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(bar.notes[slot].pitch == 62 && notes.cursorPitch() == 62 && bar.notes[slot].step == 0);
    rig.tap(LEFT);
    rig.key(R1, false);
    // Back on the grid at step 5 of key 61, for what follows.
    bar.notes[slot].step = 4;
    bar.notes[slot].nudge = 0.f;
    rig.app.session().sendNotePattern(1, 0);
    CHECK(bar.notes[slot].pitch == 61 && bar.notes[slot].step == 4);
    // X on a note does not carry it (it used to): held, it is the note's effects, and the note stays where it is.
    notes.moveCursorTo(61, 4);
    rig.key(X, true);
    CHECK(!notes.carrying() && notes.effecting());
    rig.tap(DOWN);
    rig.key(X, false);
    CHECK(notes.cursorStep() == 4 && bar.notes[slot].step == 4 && bar.notes[slot].pitch == 61);
    // R1 on an empty step is the brush, not a carry: it paints where it goes.
    notes.moveCursorTo(70, 0);
    rig.key(R1, true);
    CHECK(!notes.carrying());
    rig.tap(DOWN);
    rig.key(R1, false);
    CHECK(notes.cursorStep() == 1 && bar.at(70, 0) >= 0 && bar.at(70, 1) >= 0);
    rig.tap(B);
    rig.tap(UP);
    rig.tap(B);
    CHECK(bar.at(70, 0) < 0 && bar.at(70, 1) < 0);
    // Off the bottom of the keyboard it stops at the lowest key (and comes back).
    notes.moveCursorTo(61, 4);
    rig.key(R1, true);
    rig.tap(LEFT, 200);
    CHECK(bar.notes[slot].pitch == NoteScreen::kLowest && notes.cursorPitch() == NoteScreen::kLowest);
    rig.tap(RIGHT, 49);
    CHECK(bar.notes[slot].pitch == 61);
    rig.key(R1, false);
    // The engine is told of each move. The other note is taken away first, and
    // carrying is the last thing done: the bar then plays the note where it was put.
    notes.moveCursorTo(62, 5);
    rig.tap(B);
    notes.moveCursorTo(61, 4);
    rig.key(R1, true);
    rig.tap(DOWN, 6);
    rig.key(R1, false);
    CHECK(bar.notes[slot].step == 10);
    rig.project().playing = false;
    rig.frames(200); // the keys X played along the way are let go
    rig.render(96000);
    rig.tap(START);
    const std::vector<float> early = rig.render(10 * kStep - 200);
    const std::vector<float> then = rig.render(2 * kStep);
    CHECK(Rig::peak(early) < 0.002f && Rig::peak(then) > 0.005f);
    rig.tap(START);
    rig.render(96000);

    // A chord: the whole of it goes, by key and by step, and not onto another chord's steps.
    rig.project().playing = true;
    ChordEvent& one = bar.chords[0];
    one = ChordEvent();
    one.step = 8;
    one.length = 2.f;
    one.root = 48;
    one.count = 3;
    one.intervals[0] = 0, one.intervals[1] = 4, one.intervals[2] = 7;
    one.velocity = 0.7f;
    ChordEvent& two = bar.chords[1];
    two = one;
    two.step = 12;
    notes.moveCursorTo(52, 9); // on its middle note, second step
    rig.key(R1, true);
    CHECK(notes.carrying());
    rig.tap(RIGHT, 2);
    CHECK(one.root == 50 && notes.cursorPitch() == 54);
    rig.tap(DOWN, 5); // step 10, then 11 would run into the chord at 12
    CHECK(one.step == 10 && two.step == 12 && notes.cursorStep() == 11);
    rig.tap(UP, 20);
    CHECK(one.step == 0);
    rig.tap(LEFT, 200); // down the keyboard until its lowest note is the lowest key
    CHECK(one.root == NoteScreen::kLowest);
    rig.key(R1, false);
    CHECK(one.count == 3 && near(one.velocity, 0.7f) && one.length == 2.f);
    rig.project().playing = false;
}

void testSelectGoesToTheSongScreen() {
    Rig rig;
    for (View from : {View::Pattern, View::Device, View::Effects, View::Settings}) {
        rig.app.session().view = from;
        rig.tap(SELECT);
        CHECK(rig.view() == View::Song && !rig.app.song().listingSongs()); // there, and the list is not open
    }
    // On the SONG screen the same tap is the list of songs, as before.
    rig.tap(SELECT);
    CHECK(rig.view() == View::Song && rig.app.song().listingSongs());
    rig.tap(SELECT);
    CHECK(!rig.app.song().listingSongs());
    // SELECT used with something else is not a tap: the volume, the tempo.
    rig.app.session().view = View::Pattern;
    rig.key(SELECT, true);
    rig.tap(R1);
    rig.key(SELECT, false);
    CHECK(rig.view() == View::Pattern);
    rig.key(SELECT, true);
    rig.tap(UP);
    rig.key(SELECT, false);
    CHECK(rig.view() == View::Pattern);
    // Nor is it one when it only comes up (it went down somewhere else).
    rig.app.session().view = View::Song;
    rig.key(SELECT, true);
    rig.app.session().view = View::Device;
    rig.key(A, true);
    rig.key(A, false);
    rig.key(SELECT, false);
    CHECK(rig.view() == View::Device);
}

void testTheSetupScreen() {
    char pattern[] = "/tmp/apotheca-setup-XXXXXX";
    const std::string folder = mkdtemp(pattern);
    const float stickBefore = ui::gStickSpeed;
    {
        Rig rig;
        Session& session = rig.app.session();
        session.useProgramFolder(folder);
        session.loadSettings(); // nothing kept yet: as built
        CHECK(session.octavesShown() == 2 && near(session.stickSpeed(), stickBefore));
        // The last tab, one past FX; the row of tabs still has an end.
        session.view = View::Effects;
        rig.tap(R2);
        CHECK(rig.view() == View::Settings);
        rig.tap(R2);
        CHECK(rig.view() == View::Settings);
        SettingsScreen& setup = rig.app.settings();
        CHECK(setup.cursorRow() == SettingsScreen::kTempo);
        rig.tap(UP);
        CHECK(setup.cursorRow() == SettingsScreen::kTempo); // the list has ends too
        // TEMPO
        rig.tap(RIGHT, 3);
        rig.tap(LEFT);
        CHECK(rig.project().tempo == 122.f);
        // CHORD PACK: only with more than the built-in one does it move; it does not crash with one.
        rig.tap(DOWN);
        const int pack = session.packIndex();
        rig.tap(RIGHT);
        rig.tap(LEFT);
        CHECK(session.packIndex() == pack);
        // TRACK NAME: through the list, round and round; the first is its instrument's own.
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kTrackName && rig.project().current().name[0] == 0);
        rig.tap(RIGHT);
        CHECK(std::string(rig.project().current().name) == kTrackNames[1]);
        rig.tap(RIGHT);
        CHECK(std::string(rig.project().current().name) == kTrackNames[2]);
        rig.tap(LEFT, 2);
        CHECK(rig.project().current().name[0] == 0);
        rig.tap(LEFT);
        CHECK(std::string(rig.project().current().name) == kTrackNames[kTrackNameCount - 1]);
        rig.tap(RIGHT);
        CHECK(rig.project().current().name[0] == 0);
        // BOUNCE: left and right pick what is bounced (A starts it: tested with bouncing itself).
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kBounce);
        rig.tap(RIGHT);
        rig.tap(LEFT);
        // VOLUME, a twentieth at a time
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kVolume);
        rig.tap(LEFT, 4);
        CHECK(near(session.volume(), 0.8f));
        rig.tap(RIGHT);
        CHECK(near(session.volume(), 0.85f));
        // STICK SPEED, a twentieth at a time, between a quarter and one and a half
        rig.tap(DOWN);
        ui::gStickSpeed = 0.75f;
        rig.tap(RIGHT, 2);
        CHECK(near(session.stickSpeed(), 0.85f) && near(ui::gStickSpeed, 0.85f));
        rig.tap(LEFT, 30);
        CHECK(near(session.stickSpeed(), 0.25f));
        rig.tap(RIGHT, 40);
        CHECK(near(session.stickSpeed(), 1.5f));
        rig.tap(LEFT, 14);
        CHECK(near(session.stickSpeed(), 0.8f));
        // PIANO ROLL SHOWS: 1, 2 or 3
        rig.tap(DOWN);
        rig.tap(RIGHT, 5);
        CHECK(session.octavesShown() == 3);
        rig.tap(LEFT, 5);
        CHECK(session.octavesShown() == 1);
        // USB KEYBOARD: play only, or play and record
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kMidi && !session.midiRecording() && session.midiKeyboards() == 0);
        rig.tap(RIGHT);
        CHECK(session.midiRecording());
        rig.tap(RIGHT);
        CHECK(session.midiRecording());
        rig.tap(LEFT);
        CHECK(!session.midiRecording());
        rig.tap(RIGHT);
        // WIFI PAGE: A turns it on and off; left and right do nothing here.
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kWifi && !session.transferOn());
        rig.tap(RIGHT);
        CHECK(!session.transferOn() && session.octavesShown() == 1);
        session.useSongFolder(folder + "/songs", 7u);
        session.useSampleFolder(folder + "/samples");
        rig.tap(A);
        CHECK(session.transferOn());
        rig.tap(A);
        CHECK(!session.transferOn());
        // The stick does the same: down to choose, sideways to change.
        setup.goTo(SettingsScreen::kTempo);
        rig.key(STICK_DOWN, true);
        rig.frames(1);
        rig.key(STICK_DOWN, false);
        rig.frames(1);
        CHECK(setup.cursorRow() == SettingsScreen::kPack);
        setup.goTo(SettingsScreen::kTempo);
        rig.key(STICK_RIGHT, true);
        rig.frames(1);
        rig.key(STICK_RIGHT, false);
        rig.frames(1);
        CHECK(rig.project().tempo == 123.f);
        // START plays from here too.
        rig.tap(START);
        CHECK(rig.project().playing);
        rig.tap(START);
    }
    // Kept beside the program, and there again at the next start.
    {
        ui::gStickSpeed = 0.75f;
        Rig later;
        later.app.session().useProgramFolder(folder);
        later.app.session().loadSettings();
        CHECK(near(later.app.session().stickSpeed(), 0.8f) && later.app.session().octavesShown() == 1 && later.app.session().midiRecording());
        // Nonsense in the file is not believed.
        std::string kept;
        CHECK(readTextFile(folder + "/settings.txt", kept) && kept == "stick 80\noctaves 1\nmidirec 1\n");
        writeTextFile(folder + "/settings.txt", "stick 900\noctaves 7\nmidirec 5\nrubbish\n");
        later.app.session().loadSettings();
        CHECK(near(later.app.session().stickSpeed(), 0.8f) && later.app.session().octavesShown() == 1 && later.app.session().midiRecording());
        // Loading writes nothing.
        CHECK(readTextFile(folder + "/settings.txt", kept) && kept == "stick 900\noctaves 7\nmidirec 5\nrubbish\n");
        writeTextFile(folder + "/settings.txt", "octaves 3\nstick 60\nmidirec 0\nlater 5\n");
        later.app.session().loadSettings();
        CHECK(near(later.app.session().stickSpeed(), 0.6f) && later.app.session().octavesShown() == 3 && !later.app.session().midiRecording());
        CHECK(readTextFile(folder + "/settings.txt", kept) && kept == "octaves 3\nstick 60\nmidirec 0\nlater 5\n");
    }
    ui::gStickSpeed = stickBefore;
}

void testTheButtonTestAndSettingUpAgain() {
    Rig rig;
    rig.app.session().view = View::Settings;
    SettingsScreen& setup = rig.app.settings();
    setup.goTo(SettingsScreen::kTest);
    rig.tap(A);
    CHECK(setup.testing());
    // Every button is only shown: nothing plays, no tab turns, no row moves, SELECT does not leave.
    rig.frames(1);
    rig.tap(START);
    rig.tap(L2);
    rig.tap(R2);
    rig.tap(DOWN);
    rig.tap(SELECT);
    rig.tap(A);
    rig.key(SELECT, true);
    rig.tap(R1); // not the volume either
    rig.key(SELECT, false);
    CHECK(!rig.project().playing && rig.view() == View::Settings && setup.cursorRow() == SettingsScreen::kTest && setup.testing());
    CHECK(rig.app.session().volume() == 1.f);
    // It closes four seconds after the last press or let-go. A button held
    // (or stuck) all the while does not keep it open: that would be a trap.
    rig.key(X, true);
    rig.frames(240); // 3.8 s
    CHECK(setup.testing());
    rig.frames(20);
    CHECK(!setup.testing());
    rig.key(X, false);
    rig.tap(A); // open again
    CHECK(setup.testing());
    rig.key(STICK_LEFT, true); // the stick moving counts; the stick leaning does not
    rig.frames(240);
    CHECK(setup.testing());
    rig.frames(20);
    CHECK(!setup.testing());
    rig.key(STICK_LEFT, false);
    rig.tap(A);
    rig.frames(200);
    rig.key(X, true);
    rig.frames(200); // a press part-way starts the wait again
    CHECK(setup.testing());
    rig.key(X, false); // and so does letting go
    rig.frames(240); // 3.8 s
    CHECK(setup.testing());
    rig.tap(B); // a press starts the wait again
    rig.frames(240);
    CHECK(setup.testing());
    rig.frames(20);
    CHECK(!setup.testing());
    // Closed: the buttons are themselves again.
    rig.tap(UP);
    CHECK(setup.cursorRow() == SettingsScreen::kWifi);
    // A panic still works while the test is open.
    setup.goTo(SettingsScreen::kTest);
    rig.tap(A);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, true);
    CHECK(rig.project().silenced);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.frames(300);
    CHECK(!setup.testing());

    // CONTROLS: A held for a second asks for the setup; a tap does not.
    setup.goTo(SettingsScreen::kControls);
    rig.tap(A);
    rig.frames(100);
    CHECK(!setup.takeRelearn());
    rig.key(A, true);
    rig.frames(60); // 0.96 s from the first frame
    CHECK(!setup.takeRelearn() && setup.holdPart() > 0.8f);
    rig.frames(4);
    CHECK(setup.takeRelearn() && !setup.takeRelearn()); // once
    rig.frames(100);
    CHECK(!setup.takeRelearn()); // and not again while A stays down
    rig.key(A, false);
    // Moving off the row lets go.
    rig.key(A, true);
    rig.frames(30);
    rig.tap(UP);
    rig.frames(100);
    CHECK(!setup.takeRelearn() && setup.holdPart() == 0.f);
    rig.key(A, false);
    // Held on another row, nothing.
    setup.goTo(SettingsScreen::kVolume);
    rig.key(A, true);
    rig.frames(100);
    rig.key(A, false);
    CHECK(!setup.takeRelearn());
}

// ---- the piano roll's zoom ---------------------------------------------------------------

void testThePianoRollZooms() {
    Rig rig;
    addPhazed(rig);
    rig.project().track = 1;
    rig.app.session().view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    Session& session = rig.app.session();
    // Two octaves to begin with, C3 to B4, with middle C in the middle.
    CHECK(notes.visibleKeys() == 24 && notes.lowestKeyShown() == 48);
    notes.moveCursorTo(64, 0); // E4
    // A tap of L1 goes closer: one octave, the cursor's own, from its C.
    rig.tap(L1);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 12 && notes.lowestKeyShown() == 60 && notes.cursorPitch() == 64);
    // Again: round to the widest. The cursor's octave is the middle one.
    rig.tap(L1);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 36 && notes.lowestKeyShown() == 48);
    rig.tap(L1);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 24 && notes.lowestKeyShown() == 48); // E is in the lower half of its octave: the octave below shows too
    notes.moveCursorTo(69, 0); // A4: the upper half
    rig.tap(L1, 3);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 24 && notes.lowestKeyShown() == 60);
    // Held, with up and down: closer and further, stopping at the ends; and that is not also a tap.
    rig.key(L1, true);
    rig.tap(UP);
    CHECK(session.octavesShown() == 1);
    rig.tap(UP);
    CHECK(session.octavesShown() == 1 && notes.cursorStep() == 0);
    rig.tap(DOWN, 2);
    CHECK(session.octavesShown() == 3);
    rig.tap(DOWN);
    CHECK(session.octavesShown() == 3);
    rig.key(L1, false);
    CHECK(session.octavesShown() == 3);
    // The stick with L1 held: one step of zoom for each push.
    rig.key(L1, true);
    rig.key(STICK_UP, true);
    rig.frames(40);
    CHECK(session.octavesShown() == 2 && notes.cursorPitch() == 69 && notes.cursorStep() == 0);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(2);
    rig.key(STICK_UP, false);
    rig.key(L1, false);
    CHECK(session.octavesShown() == 1);
    // The cursor still scrolls the keys, one octave wide.
    rig.frames(1);
    notes.moveCursorTo(72, 0);
    CHECK(notes.lowestKeyShown() == 61);
    // At the top of the keyboard the keys stop at the last one.
    notes.moveCursorTo(108, 0);
    rig.key(L1, true);
    rig.tap(DOWN, 2);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 36 && notes.lowestKeyShown() == 108 - 35);
    notes.moveCursorTo(12, 0);
    rig.tap(L1);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 24 && notes.lowestKeyShown() == 12);
    // Changed from SETUP, the roll follows.
    session.setOctavesShown(3);
    rig.frames(1);
    CHECK(notes.visibleKeys() == 36 && notes.lowestKeyShown() == 12);
    session.setOctavesShown(1);
    rig.frames(1);
    notes.moveCursorTo(108, 0);
    CHECK(notes.lowestKeyShown() == 97);
    session.setOctavesShown(3); // as SETUP does it: the keys must be moved to fit
    rig.frames(1);
    CHECK(notes.lowestKeyShown() == 73);
    // L1 while a chord is being picked is still "another pack", not the zoom.
    rig.key(Y, true);
    rig.tap(L1);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(session.octavesShown() == 3);
}

void testTheStickSpeedAndTheVolume() {
    // ---- one number slows everything the stick does -----------------------------------
    const auto travel = [](float speed) {
        ui::gStickSpeed = speed;
        Rig rig;
        rig.app.session().view = View::Song;
        rig.key(STICK_DOWN, true);
        rig.frames(26); // the first move is at once; the rest at the stick's speed
        rig.key(STICK_DOWN, false);
        ui::gStickSpeed = 1.f;
        return rig.app.song().cursorScene();
    };
    const int full = travel(1.f), slower = travel(0.75f), half = travel(0.5f);
    CHECK(full >= 9 && full <= 11);
    CHECK(slower >= 7 && slower <= 8);   // a quarter off
    CHECK(half >= 5 && half <= 6);
    // ...and what it turns with a button held, the same.
    const auto turned = [](float speed) {
        ui::gStickSpeed = speed;
        Rig rig;
        rig.app.session().view = View::Pattern;
        rig.app.drums().setStep(0, 0, 0.2f);
        rig.key(A, true);
        rig.key(STICK_UP, true);
        rig.frames(10);
        rig.key(STICK_UP, false);
        rig.key(A, false);
        ui::gStickSpeed = 1.f;
        return rig.app.drums().level(0, 0) - 0.2f;
    };
    CHECK(std::fabs(turned(0.75f) / turned(1.f) - 0.75f) < 0.02f);

    // ---- the volume: SELECT with L1 and R1 -------------------------------------------
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Pattern;
    rig.app.drums().setStep(0, 0, 1.f);
    CHECK(session.volume() == 1.f && !rig.app.volumeShowing());
    const auto kick = [&] {
        rig.render(100000);
        Command c;
        c.type = Command::Trigger;
        c.value = 1.f;
        rig.engine->send(c);
        return rig.listen(4000);
    };
    const float loud = kick();
    rig.key(SELECT, true);
    rig.tap(L1, 5);
    rig.key(SELECT, false);
    CHECK(near(session.volume(), 0.5f) && rig.app.volumeShowing());
    CHECK(!rig.app.song().listingSongs() && !rig.app.drums().painting()); // it was not a tap of SELECT, nor L1's painting
    CHECK(rig.app.drums().level(0, 1) == 0.f);
    const float halfWay = kick();
    CHECK(std::fabs(halfWay / loud - 0.25f) < 0.02f); // half-way on the knob is a quarter of the level: a good deal quieter
    rig.frames(100);
    CHECK(!rig.app.volumeShowing()); // the bar goes after a moment
    rig.key(SELECT, true);
    rig.tap(L1, 9);
    CHECK(session.volume() == 0.f);
    rig.key(SELECT, false);
    CHECK(kick() == 0.f);
    rig.key(SELECT, true);
    rig.tap(R1, 30);
    rig.key(SELECT, false);
    CHECK(session.volume() == 1.f && std::fabs(kick() / loud - 1.f) < 0.01f);
    // On the Song screen too, where a tap of SELECT would open the song list.
    session.view = View::Song;
    rig.key(SELECT, true);
    rig.tap(L1);
    rig.key(SELECT, false);
    CHECK(!rig.app.song().listingSongs() && near(session.volume(), 0.9f));
    session.view = View::Pattern;
    // What Catch takes is the sound before the volume: a quiet handheld still catches at full level.
    session.setVolume(0.2f);
    rig.render(100000);
    rig.tap(START);
    const std::vector<float> out = rig.render(kBar + 4 * kStep);
    rig.tap(START);
    float heard = 0.f, caught = 0.f;
    for (int i = 0; i < 2000; ++i) heard = std::fabs(out[2 * static_cast<std::size_t>(i)]) > heard ? std::fabs(out[2 * static_cast<std::size_t>(i)]) : heard;
    for (float v : rig.engine->lastBar()) caught = std::fabs(v) > caught ? std::fabs(v) : caught;
    CHECK(heard < 0.06f * loud && caught > 0.9f * loud);
    session.setVolume(std::nanf(""));
    CHECK(near(session.volume(), 0.2f));
    session.setVolume(7.f);
    CHECK(session.volume() == 1.f);
}

void testTheTempoFromAnyScreen() {
    Rig rig;
    rig.app.session().addTrack(TrackKind::Phazed);
    CHECK(rig.project().tempo == 120.f);
    const View views[] = {View::Song, View::Pattern, View::Device, View::Effects};
    float want = 120.f;
    for (int track = 0; track < 2; ++track) {
        rig.project().track = track;
        for (View view : views) {
            rig.app.session().view = view;
            rig.key(SELECT, true);
            rig.tap(UP);
            rig.key(SELECT, false);
            want += 1.f;
            CHECK(rig.project().tempo == want);
            CHECK(!rig.app.song().listingSongs()); // SELECT did something: letting go is not a tap
            CHECK(rig.view() == view);
        }
    }
    rig.app.session().view = View::Pattern;
    rig.project().track = 0;
    rig.key(SELECT, true);
    rig.tap(RIGHT, 2);
    rig.tap(DOWN, 3);
    rig.tap(LEFT);
    CHECK(rig.project().tempo == want + 20.f - 3.f - 10.f);
    CHECK(rig.app.drums().cursorStep() == 0 && rig.app.drums().cursorLane() == 0); // the cursor did not move
    rig.tap(RIGHT, 40);
    CHECK(rig.project().tempo == kMaxTempo);
    rig.tap(LEFT, 40);
    CHECK(rig.project().tempo == kMinTempo);
    rig.key(SELECT, false);
    // The engine follows: a bar at 40 BPM is six seconds.
    rig.app.drums().setStep(0, 0, 1.f);
    rig.tap(START);
    rig.render(288000 - 2000);
    CHECK(rig.listen(1900) < 0.01f); // nothing just before the bar comes round
    CHECK(rig.listen(4000) > 0.2f);  // the kick, on the next bar line
    rig.tap(START);
    // The drum grid's own tempo box starts from there, not from where it last left it.
    rig.frames(1);
    rig.app.drums().moveCursorTo(0, 0);
    rig.tap(UP);
    CHECK(rig.app.drums().cursorOnTempo());
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(2);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(A, false);
    CHECK(rig.project().tempo > kMinTempo && rig.project().tempo < kMinTempo + 8.f);
    rig.app.session().setTempo(kMinTempo + 1.f);
    // A plain SELECT tap on the Song screen still opens the list.
    rig.app.session().view = View::Song;
    rig.tap(SELECT);
    CHECK(rig.app.song().listingSongs());
    rig.tap(SELECT);
    rig.app.session().setTempo(std::nanf(""));
    CHECK(rig.project().tempo == kMinTempo + 1.f);
}

// ---- the effects screen ------------------------------------------------------------------

void testTheEffectsScreen() {
    Rig rig;
    FxScreen& fx = rig.app.fx();
    Session& session = rig.app.session();
    session.view = View::Effects;
    const EffectData& first = rig.project().tracks[0].effects[0];
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 0 && first.kind == 0);
    // An empty slot has nothing to move along to.
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 0);

    // Hold A and go up: the effects in turn, each at its usual settings, heard at once.
    rig.render(100000);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(first.kind == static_cast<int>(EffectKind::Filter) && first.param[0] == effectInfo(EffectKind::Filter).defaults[0] && first.param[3] == 1.f);
    CHECK(rig.listen(4000) > 0.05f);
    // (Where an effect is in the list is the list's business: effectPlace says.)
    const int delayAt = effectPlace(EffectKind::Delay), lastAt = effectListSize() - 1;
    CHECK(effectPlace(EffectKind::Filter) == 1 && delayAt > 1 && delayAt < lastAt && effectAt(lastAt) == EffectKind::Crush);
    rig.tap(UP, delayAt - 1);
    CHECK(first.kind == static_cast<int>(EffectKind::Delay));
    rig.tap(UP, lastAt + 3);
    CHECK(first.kind == static_cast<int>(EffectKind::Crush)); // the last one: it stops
    rig.tap(DOWN, lastAt - delayAt);
    CHECK(first.kind == static_cast<int>(EffectKind::Delay));
    rig.tap(DOWN, delayAt);
    CHECK(first.kind == static_cast<int>(EffectKind::None)); // down past the first effect: none
    rig.tap(DOWN, 2);
    CHECK(first.kind == static_cast<int>(EffectKind::None)); // and it stops there
    rig.tap(UP, delayAt);
    CHECK(first.kind == static_cast<int>(EffectKind::Delay));
    rig.tap(LEFT); // sideways with A held jumps a group of the list: the cursor stays on the effect
    CHECK(effectPlace(static_cast<EffectKind>(first.kind)) == effectGroupJump(delayAt, -1) && fx.cursorColumn() == 0);
    rig.tap(RIGHT);
    CHECK(effectInfo(static_cast<EffectKind>(first.kind)).group == EffectGroup::Delay && fx.cursorColumn() == 0);
    rig.tap(UP, delayAt - effectPlace(static_cast<EffectKind>(first.kind))); // the first of the delays need not be DELAY
    CHECK(first.kind == static_cast<int>(EffectKind::Delay));
    rig.key(A, false);

    // The delay is on the track: a rim shot comes back a quarter note later.
    rig.render(200000);
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    rig.engine->send(hit);
    std::vector<float> out = rig.render(40000);
    CHECK(Rig::peak(out, 24000, 28000) > 0.02f);
    CHECK(Rig::peak(out, 18000, 23000) < 0.5f * Rig::peak(out, 24000, 28000)); // quiet before it

    // Along to its settings. TIME is a choice: the D-pad steps to the next one.
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 1);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(delaySteps(first.param[0]) == 6);
    rig.tap(UP, 5);
    CHECK(delaySteps(first.param[0]) == 8);
    rig.tap(DOWN, 9);
    CHECK(delaySteps(first.param[0]) == 1 && first.param[0] > 0.05f && first.param[0] < 0.12f); // the middle of its part of the range
    rig.key(A, false);
    // FEEDBACK is an amount: the D-pad moves it a twentieth, the stick turns it.
    rig.tap(RIGHT);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(near(first.param[1], 0.5f));
    rig.tap(DOWN, 3);
    CHECK(near(first.param[1], 0.35f));
    rig.key(STICK_UP, true);
    rig.frames(10);
    const float turned = first.param[1];
    CHECK(turned > 0.4f && turned < 0.6f);
    rig.frames(120);
    CHECK(first.param[1] == 1.f);
    rig.key(STICK_UP, false);
    rig.key(STICK_DOWN, true);
    rig.frames(200);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(first.param[1] == 0.f);
    // B needs A let go. On a setting: its usual value. On the effect: none.
    rig.tap(B);
    CHECK(first.param[1] == 0.f);
    rig.key(A, false);
    rig.tap(B);
    CHECK(first.param[1] == effectInfo(EffectKind::Delay).defaults[1]);
    rig.tap(RIGHT, 9);
    CHECK(fx.cursorColumn() == 4); // four settings, no more
    rig.tap(LEFT, 9);
    CHECK(fx.cursorColumn() == 0);

    // Down through the slots: the track's second and third, its sends, then (the other page) the buses and the master's two, and round.
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 1);
    rig.key(A, true);
    rig.tap(UP, effectPlace(EffectKind::Reverb));
    rig.key(A, false);
    CHECK(rig.project().tracks[0].effects[1].kind == static_cast<int>(EffectKind::Reverb) && first.kind == static_cast<int>(EffectKind::Delay));
    rig.tap(RIGHT, 9);
    CHECK(fx.cursorColumn() == 3); // the reverb has three
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 2 && fx.cursorColumn() == 0); // an empty slot below: the cursor comes back to the effect
    rig.tap(DOWN, 4);
    CHECK(fx.cursorRow() == FxScreen::kMasterRow && fx.cursorColumn() == 0);
    rig.key(A, true);
    rig.tap(UP, effectPlace(EffectKind::Crush));
    rig.key(A, false);
    CHECK(rig.project().masterEffects[0].kind == static_cast<int>(EffectKind::Crush));
    rig.tap(DOWN, 2);
    CHECK(fx.cursorRow() == 0);
    rig.tap(UP);
    CHECK(fx.cursorRow() == FxScreen::kRows - 1);
    // The stick moves the cursor too.
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(fx.cursorRow() == 0);
    rig.tap(B);
    CHECK(first.kind == 0 && first.param[0] == 0.f);

    // The first page is whichever track is being worked on; the master's stay.
    session.addTrack(TrackKind::Phazed);
    session.setEffect(0, 0, EffectKind::Delay);
    fx.goTo(0, 4);
    CHECK(fx.cursorColumn() == 4);
    rig.project().track = 1;
    rig.frames(1);
    CHECK(fx.cursorColumn() == 0); // the new track's slot is empty: there is no fourth setting to be on
    session.setEffect(0, 0, EffectKind::None);
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].effects[0].kind == static_cast<int>(EffectKind::Drive) && first.kind == 0);
    CHECK(rig.project().masterEffects[0].kind == static_cast<int>(EffectKind::Crush));
    // X hears a synth track by a note.
    rig.render(200000);
    rig.tap(X);
    CHECK(rig.listen(4000) > 0.005f);

    // How settings read.
    char text[32];
    effectParamText(EffectKind::Filter, 0, 0.f, text, sizeof text);
    CHECK(std::string(text) == "30 HZ");
    effectParamText(EffectKind::Filter, 0, 1.f, text, sizeof text);
    CHECK(std::string(text) == "21.7 KHZ");
    {
        // The five filter types, and the DJ filter's cutoff, which reads as what it is doing.
        const char* names[5] = {"LOW 12 DB", "LOW 24 DB", "BAND PASS", "DJ", "HIGH PASS"};
        for (int i = 0; i < 5; ++i) {
            effectParamText(EffectKind::Filter, 2, (i + 0.5f) / 5.f, text, sizeof text);
            CHECK(std::string(text) == names[i]);
        }
        const float dj[4] = {0.f, 0.f, 0.7f, 1.f}, low[4] = {0.f, 0.f, 0.1f, 1.f};
        effectParamText(EffectKind::Filter, 0, 0.5f, text, sizeof text, dj);
        CHECK(std::string(text) == "OPEN");
        effectParamText(EffectKind::Filter, 0, 0.f, text, sizeof text, dj);
        CHECK(std::string(text) == "LOW 60 HZ");
        effectParamText(EffectKind::Filter, 0, 1.f, text, sizeof text, dj);
        CHECK(std::string(text) == "HIGH 8.0 KHZ");
        effectParamText(EffectKind::Filter, 0, 0.f, text, sizeof text, low); // any other type: the corner itself
        CHECK(std::string(text) == "30 HZ");
    }
    effectParamText(EffectKind::Filter, 2, 0.5f, text, sizeof text);
    CHECK(std::string(text) == "BAND PASS");
    effectParamText(EffectKind::Delay, 0, 0.4f, text, sizeof text);
    CHECK(std::string(text) == "1/8 DOTTED");
    effectParamText(EffectKind::Crush, 0, 1.f, text, sizeof text);
    CHECK(std::string(text) == "3 BITS");
    effectParamText(EffectKind::Crush, 1, 0.f, text, sizeof text);
    CHECK(std::string(text) == "FULL");
    effectParamText(EffectKind::Drive, 3, 0.5f, text, sizeof text);
    CHECK(std::string(text) == "100%");
    effectParamText(EffectKind::Reverb, 2, 0.3f, text, sizeof text);
    CHECK(std::string(text) == "30%");
}

void testEffectsThroughTheSession() {
    // Two fresh handhelds: one sets effects up by hand, the other opens the
    // same thing as a song. A hit must sound the same on both.
    Rig made, opened;
    Session& session = made.app.session();
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 1, 0.2f);
    session.setEffect(0, 1, EffectKind::Filter);
    session.setEffectParam(0, 1, 0, 0.4f);
    session.setEffect(kMasterTrack, 1, EffectKind::Drive);
    session.setEffectParam(kMasterTrack, 1, 0, 0.9f);
    // Nonsense is refused or held in range.
    session.setEffectParam(0, 0, 4, 0.5f);
    session.setEffectParam(0, 0, -1, 0.5f);
    session.setEffectParam(0, 1, 0, 7.f);
    CHECK(made.project().tracks[0].effects[1].param[0] == 1.f);
    session.setEffectParam(0, 1, 0, 0.4f);
    session.setEffect(0, 3, EffectKind::Delay); // a track has three slots
    session.setEffect(kMasterTrack, 2, EffectKind::Delay); // ...and the master two
    session.setEffect(12, 0, EffectKind::Delay);
    session.setEffect(3, 0, static_cast<EffectKind>(42));
    CHECK(made.project().tracks[3].effects[0].kind == 0);
    session.setEffectParam(3, 0, 0, 0.5f); // nothing in the slot: nothing to set
    CHECK(made.project().tracks[3].effects[0].param[0] == 0.f);

    const std::string text = songText(made.project());
    CHECK(text.find("\nfx 2 drive 0.9 0.7 1 0.5\nbusfx 1 ") != std::string::npos); // the master's, then the two buses'
    CHECK(text.find("\ntrack drums\nfx 1 delay 0.5 0.2 0.6 0.35\nfx 2 filter 0.4 0.2 0 1\n") != std::string::npos);
    CHECK(text.find("fx 3") == std::string::npos && text.find("send") == std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    opened.project() = *back;
    opened.app.session().applyProject();
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    made.engine->send(hit);
    opened.engine->send(hit);
    const std::vector<float> a = made.render(60000), b = opened.render(60000);
    CHECK(Rig::peak(a) > 0.02f && a == b);
    // And not like a handheld with no effects.
    Rig plain;
    plain.engine->send(hit);
    CHECK(plain.render(60000) != a);
    // A new song after it: the effects are gone from the engine too.
    {
        std::unique_ptr<Project> fresh(new Project);
        opened.project() = *fresh;
    }
    opened.app.session().applyProject();
    opened.render(400000);
    plain.render(400000);
    opened.engine->send(hit);
    plain.engine->send(hit);
    CHECK(opened.render(60000) == plain.render(60000));

    // Lines in a song file that make no sense are left out.
    std::unique_ptr<Project> song(new Project);
    {
        // An effect this version does not have (a song from a newer one): the line is skipped, and what the slot held stays.
        std::unique_ptr<Project> newer(new Project);
        CHECK(parseSong("apotheca-song 1\nfx 1 delay 0.2\nfx 1 nosuchecho 0.9 0.9 0.9 0.9\ntrack drums\nfx 2 crush 0.25\nfx 2 nosuchroom 1 1\n", *newer));
        CHECK(newer->masterEffects[0].kind == static_cast<int>(EffectKind::Delay) && newer->masterEffects[0].param[0] == 0.2f);
        CHECK(newer->tracks[0].effects[1].kind == static_cast<int>(EffectKind::Crush) && newer->tracks[0].effects[1].param[0] == 0.25f);
    }
    {
        // The newer effects are words in a song file like the old ones, and come back as they went.
        std::unique_ptr<Project> made(new Project), back(new Project);
        CHECK(parseSong("apotheca-song 1\nfx 2 tape 0.9 0.5 0.25 0.125\ntrack drums\nfx 1 tape 0.0625\n", *made));
        CHECK(made->masterEffects[1].kind == static_cast<int>(EffectKind::Tape) && made->masterEffects[1].param[0] == 0.9f && made->masterEffects[1].param[3] == 0.125f);
        CHECK(made->tracks[0].effects[0].kind == static_cast<int>(EffectKind::Tape) && made->tracks[0].effects[0].param[0] == 0.0625f);
        const std::string text = songText(*made);
        CHECK(text.find("fx 2 tape 0.9 0.5 0.25 0.125") != std::string::npos);
        CHECK(parseSong(text, *back) && back->masterEffects[1].kind == static_cast<int>(EffectKind::Tape) && back->masterEffects[1].param[2] == 0.25f);
        CHECK(back->tracks[0].effects[0].kind == static_cast<int>(EffectKind::Tape));
        CHECK(parseSong("apotheca-song 1\nfx 1 hall 0.75 0.5 0.25 0.125\ntrack drums\nfx 2 hall 1\n", *made));
        CHECK(made->masterEffects[0].kind == static_cast<int>(EffectKind::Hall) && made->masterEffects[0].param[1] == 0.5f && made->tracks[0].effects[1].kind == static_cast<int>(EffectKind::Hall));
        CHECK(made->tracks[0].effects[1].param[0] == 1.f && made->tracks[0].effects[1].param[1] == effectInfo(EffectKind::Hall).defaults[1]);
        CHECK(songText(*made).find("fx 1 hall 0.75 0.5 0.25 0.125") != std::string::npos && parseSong(songText(*made), *back) && back->masterEffects[0].kind == static_cast<int>(EffectKind::Hall) && back->masterEffects[0].param[3] == 0.125f);
    }
    CHECK(parseSong("apotheca-song 1\nfx 1 reverb 0.9\nfx 3 delay\nfx 0 delay\nfx 2 warble 1 1\ntrack drums\nfx 2 crush 5 -5 nonsense\nfx 1\n", *song));
    CHECK(song->masterEffects[0].kind == static_cast<int>(EffectKind::Reverb) && song->masterEffects[0].param[0] == 0.9f);
    CHECK(song->masterEffects[0].param[1] == effectInfo(EffectKind::Reverb).defaults[1]); // not given: its usual value
    CHECK(song->masterEffects[1].kind == 0 && song->tracks[0].effects[0].kind == 0);
    CHECK(song->tracks[0].effects[1].kind == static_cast<int>(EffectKind::Crush) && song->tracks[0].effects[1].param[0] == 1.f && song->tracks[0].effects[1].param[1] == 0.f);
}

// The two send buses, the sends and a track's third slot: through the Session and the song file.
void testSendsThroughTheSession() {
    const int delay = static_cast<int>(EffectKind::Delay), reverb = static_cast<int>(EffectKind::Reverb);
    // A new song: the buses hold what kNewSongBus says, at their usual settings but for LEVEL (the MIX), which is full.
    {
        std::unique_ptr<Project> fresh(new Project);
        for (int bus = 0; bus < kBuses; ++bus) {
            const EffectKind kind = kNewSongBus[bus];
            CHECK(kind != EffectKind::None && effectBus(kind) == bus && fresh->busEffects[bus].kind == static_cast<int>(kind));
            const int mix = effectMixParam(kind);
            CHECK(mix >= 0);
            for (int i = 0; i < effectInfo(kind).params; ++i)
                CHECK(fresh->busEffects[bus].param[i] == (i == mix ? 1.f : effectInfo(kind).defaults[i]));
            for (const TrackData& t : fresh->tracks) CHECK(t.send[bus] == 0.f);
        }
        const std::string text = songText(*fresh);
        CHECK(text.find("\nbusfx 1 delay 0.5 0.45 0.6 1\nbusfx 2 reverb 0.6 0.5 1\n") != std::string::npos && text.find("send") == std::string::npos);
    }

    Rig made, opened, plain;
    Session& session = made.app.session();
    Project& project = made.project();
    // A send: held in range, nonsense refused.
    session.setSend(0, 0, 0.35f);
    session.setSend(0, 1, 7.f);
    CHECK(project.tracks[0].send[0] == 0.35f && project.tracks[0].send[1] == 1.f);
    session.setSend(0, 1, -3.f);
    CHECK(project.tracks[0].send[1] == 0.f);
    session.setSend(0, 1, 0.6f);
    session.setSend(0, 1, std::nanf(""));
    CHECK(project.tracks[0].send[1] == 0.f);
    session.setSend(0, 1, 0.25f);
    session.setSend(0, 2, 0.9f);
    session.setSend(0, -1, 0.9f);
    session.setSend(12, 0, 0.9f);
    session.setSend(-1, 0, 0.9f);
    CHECK(project.tracks[0].send[0] == 0.35f && project.tracks[0].send[1] == 0.25f && project.tracks[1].send[0] == 0.f);
    CHECK(project.tracks[0].laneLevel[0] == 1.f); // (what lies after the two sends: a third bus would have written there)
    // A bus holds one effect, of its own kind: a delay on ECHO, a reverb on SPACE.
    CHECK(session.effectData(kEchoBus, 0).kind == delay && session.effectData(kSpaceBus, 0).kind == reverb);
    CHECK(&session.effectData(kEchoBus, 0) == &project.busEffects[0] && &session.effectData(kSpaceBus, 0) == &project.busEffects[1]);
    session.setEffect(kEchoBus, 0, EffectKind::Reverb);
    session.setEffect(kEchoBus, 0, EffectKind::Filter);
    session.setEffect(kSpaceBus, 0, EffectKind::Delay);
    session.setEffect(kEchoBus, 1, EffectKind::None);
    CHECK(project.busEffects[0].kind == delay && project.busEffects[1].kind == reverb);
    session.setEffectParam(kEchoBus, 0, 1, 0.7f);
    session.setEffectParam(kEchoBus, 0, 3, 0.5f); // LEVEL
    session.setEffectParam(kEchoBus, 1, 1, 0.1f);
    session.setEffectParam(kEchoBus, 0, 4, 0.1f);
    CHECK(project.busEffects[0].param[1] == 0.7f && project.busEffects[0].param[3] == 0.5f);
    // Put in afresh, it is at its usual settings again, LEVEL at full.
    session.setEffect(kEchoBus, 0, EffectKind::None);
    CHECK(project.busEffects[0].kind == 0 && project.busEffects[0].param[3] == 0.f);
    session.setEffect(kEchoBus, 0, EffectKind::Delay);
    CHECK(project.busEffects[0].param[1] == effectInfo(EffectKind::Delay).defaults[1] && project.busEffects[0].param[3] == 1.f);
    session.setEffectParam(kEchoBus, 0, 1, 0.7f);
    session.setEffectParam(kSpaceBus, 0, 0, 0.3f);
    // The third slot.
    session.setEffect(0, 2, EffectKind::Crush);
    session.setEffectParam(0, 2, 0, 0.3f);
    CHECK(project.tracks[0].effects[2].kind == static_cast<int>(EffectKind::Crush) && project.tracks[0].effects[2].param[0] == 0.3f);

    // In the song file, and back the same.
    const std::string text = songText(project);
    CHECK(text.find("\nbusfx 1 delay 0.5 0.7 0.6 1\nbusfx 2 reverb 0.3 ") != std::string::npos);
    CHECK(text.find("\nfx 3 crush 0.3") != std::string::npos);
    CHECK(text.find("\nsend 1 0.35\nsend 2 0.25\n") != std::string::npos);
    CHECK(text.find("busfx") < text.find("track "));
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].send[0] == 0.35f && back->tracks[0].send[1] == 0.25f && back->busEffects[0].param[1] == 0.7f);
    CHECK(back->tracks[0].effects[2].kind == static_cast<int>(EffectKind::Crush));

    // One handheld set up by hand, one that opened the song: a hit sounds the same on both, tail and all...
    opened.project() = *back;
    opened.app.session().applyProject();
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    made.engine->send(hit);
    opened.engine->send(hit);
    plain.engine->send(hit);
    const std::vector<float> a = made.render(96000), b = opened.render(96000), dry = plain.render(96000);
    CHECK(Rig::peak(a) > 0.02f && a == b && a != dry);
    CHECK(made.engine->busAwake(0) && made.engine->busAwake(1) && opened.engine->busAwake(0) && !plain.engine->busAwake(0) && !plain.engine->busAwake(1));
    // ...and each part of it is heard: either send, the bus's settings, the third slot.
    const auto heardWithout = [&](void (*change)(Session&)) {
        Rig other;
        other.project() = *back;
        change(other.app.session());
        other.app.session().applyProject();
        other.engine->send(hit);
        return other.render(96000);
    };
    CHECK(heardWithout([](Session&) {}) == a);
    CHECK(heardWithout([](Session& s) { s.project.tracks[0].send[0] = 0.f; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.tracks[0].send[1] = 0.f; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.busEffects[0].param[1] = 0.2f; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.busEffects[1].param[0] = 0.9f; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.busEffects[0].kind = 0; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.busEffects[1].kind = 0; }) != a);
    CHECK(heardWithout([](Session& s) { s.project.tracks[0].effects[2].kind = 0; }) != a);
    // The same to the bit with helper threads rendering the tracks.
    for (int helpers = 1; helpers <= 3; helpers += 2) {
        Rig other;
        other.engine->useWorkers(helpers);
        other.project() = *back;
        other.app.session().applyProject();
        other.engine->send(hit);
        CHECK(other.render(96000) == a);
    }
    // A send turned by hand reaches the engine at once, and so does a bus's setting.
    {
        Rig one, two;
        one.app.session().setSend(0, 0, 0.5f);
        one.engine->send(hit);
        two.engine->send(hit);
        const std::vector<float> wet = one.render(96000);
        CHECK(wet != two.render(96000));
        Rig three;
        three.app.session().setSend(0, 0, 0.5f);
        three.app.session().setEffectParam(kEchoBus, 0, 3, 0.2f);
        three.engine->send(hit);
        CHECK(three.render(96000) != wet);
        // Sends at nothing and the buses emptied: the same to the bit as sends at nothing alone. A bus not sent to adds nothing.
        Rig bare;
        bare.app.session().setEffect(kEchoBus, 0, EffectKind::None);
        bare.app.session().setEffect(kSpaceBus, 0, EffectKind::None);
        CHECK(bare.project().busEffects[0].kind == 0 && songText(bare.project()).find("\nbusfx 1 none\nbusfx 2 none\n") != std::string::npos);
        Rig usual;
        bare.engine->send(hit);
        usual.engine->send(hit);
        CHECK(bare.render(96000) == usual.render(96000));
    }
    // A new song after it: the sends and the third slot are gone from the engine too.
    {
        std::unique_ptr<Project> fresh(new Project);
        opened.project() = *fresh;
    }
    opened.app.session().applyProject();
    opened.render(1200000); // the buses ring out and go to rest
    plain.render(1200000);
    CHECK(!opened.engine->busAwake(0) && !opened.engine->busAwake(1));
    opened.engine->send(hit);
    plain.engine->send(hit);
    CHECK(opened.render(60000) == plain.render(60000));

    // An old song: no busfx, no send. Its buses are the usual two and nothing is sent, and it is the
    // same to the bit as with no buses at all.
    {
        const std::string old = "apotheca-song 1\ntempo 120\nfx 1 drive 0.6 0.7 1 0.5\ntrack drums\nfx 1 delay 0.5 0.2 0.6 0.35\nfx 2 filter 0.4 0.2 0 1\n"
                                "scene 1\nhit 1 1 1\nhit 3 5 0.8\nhit 7 9 1\n";
        std::unique_ptr<Project> song(new Project);
        CHECK(parseSong(old, *song));
        CHECK(song->busEffects[0].kind == static_cast<int>(kNewSongBus[0]) && song->busEffects[1].kind == static_cast<int>(kNewSongBus[1]));
        CHECK(song->busEffects[0].param[effectMixParam(kNewSongBus[0])] == 1.f && song->tracks[0].send[0] == 0.f && song->tracks[0].send[1] == 0.f);
        // Saved by this program, it is what it was, plus the two busfx lines.
        const std::string saved = songText(*song);
        std::string without;
        std::istringstream in(saved);
        int busLines = 0;
        for (std::string l; std::getline(in, l);) {
            if (l.compare(0, 6, "busfx ") == 0) ++busLines;
            else without += l + "\n";
        }
        std::unique_ptr<Project> emptied(new Project);
        *emptied = *song;
        emptied->busEffects[0] = EffectData();
        emptied->busEffects[1] = EffectData();
        std::string withoutToo;
        std::istringstream in2(songText(*emptied));
        for (std::string l; std::getline(in2, l);)
            if (l.compare(0, 6, "busfx ") != 0) withoutToo += l + "\n";
        CHECK(busLines == 2 && without == withoutToo && without.find("send") == std::string::npos && without.find("fx 3") == std::string::npos);
        Rig with, none;
        with.project() = *song;
        with.app.session().applyProject();
        none.project() = *emptied;
        none.app.session().applyProject();
        Command go;
        go.type = Command::Play;
        with.engine->send(go);
        none.engine->send(go);
        const std::vector<float> x = with.render(2 * kBar), y = none.render(2 * kBar);
        CHECK(Rig::peak(x) > 0.02f && x == y && !with.engine->busAwake(0) && !with.engine->busAwake(1));
    }

    // Lines that make no sense are left out; what is given is held in range.
    {
        std::unique_ptr<Project> song(new Project);
        CHECK(parseSong("apotheca-song 1\nsend 1 0.9\nbusfx 1 reverb 0.1 0.1 0.1 0.1\nbusfx 2 delay 0.1\nbusfx 3 delay 0.1\nbusfx 0 delay 0.1\nbusfx 1 warble 1 1\n"
                        "busfx 2\nbusfx 1 filter 0.2\nfx 3 delay\ntrack drums\nsend 0 0.5\nsend 3 0.5\nsend 1 -0.5\nsend 2 nonsense\nsend\nfx 4 crush 0.5\nfx 3 crush 0.25\n", *song));
        CHECK(song->busEffects[0].kind == delay && song->busEffects[0].param[0] == effectInfo(EffectKind::Delay).defaults[0]);
        CHECK(song->busEffects[0].param[3] == 1.f && song->busEffects[1].param[2] == 1.f);
        CHECK(song->busEffects[1].kind == reverb && song->busEffects[1].param[0] == effectInfo(EffectKind::Reverb).defaults[0]);
        CHECK(song->masterEffects[0].kind == 0 && song->masterEffects[1].kind == 0);
        CHECK(song->tracks[0].send[0] == 0.f && song->tracks[0].send[1] == 0.f && song->tracks[1].send[0] == 0.f);
        CHECK(song->tracks[0].effects[2].kind == static_cast<int>(EffectKind::Crush) && song->tracks[0].effects[2].param[0] == 0.25f);
        CHECK(song->tracks[0].effects[0].kind == 0 && song->tracks[0].effects[1].kind == 0);
        // Given: the bus's effect with only some settings (the rest at their usual, LEVEL full), "none", sends over the top.
        CHECK(parseSong("apotheca-song 1\nbusfx 1 delay 0.25 5\nbusfx 2 none\ntrack drums\nsend 1 7\nsend 2 0.125\ntrack drums\nsend 2 1\n", *song));
        CHECK(song->busEffects[0].kind == delay && song->busEffects[0].param[0] == 0.25f && song->busEffects[0].param[1] == 1.f);
        CHECK(song->busEffects[0].param[2] == effectInfo(EffectKind::Delay).defaults[2] && song->busEffects[0].param[3] == 1.f);
        CHECK(song->busEffects[1].kind == 0);
        CHECK(song->tracks[0].send[0] == 1.f && song->tracks[0].send[1] == 0.125f && song->tracks[1].send[0] == 0.f && song->tracks[1].send[1] == 1.f);
        // A bus line after the tracks is still the song's (an older program wrote none; a hand-edited file may put it anywhere).
        CHECK(parseSong("apotheca-song 1\ntrack drums\nbusfx 2 reverb 0.75\n", *song) && song->busEffects[1].param[0] == 0.75f);
        // Automation on the new lanes loads and is written back: slot 3's (16 to 19), the sends' (20, 21), the spare two.
        CHECK(parseSong("apotheca-song 1\ntrack drums\nauto 1 16 0 0.5\nauto 1 19 3 0.25\nauto 2 20 1 0.75\nauto 1 21 2 1\nauto 1 23 0 0.5\nauto 1 24 0 0.5\n", *song));
        const TrackData& t = song->tracks[0];
        CHECK(t.automation[0][16][0] == 0.5f && t.automation[0][19][3] == 0.25f && t.automation[1][20][1] == 0.75f && t.automation[0][21][2] == 1.f && t.automation[0][23][0] == 0.5f);
        const std::string again = songText(*song);
        CHECK(again.find("auto 1 16 0 0.5\n") != std::string::npos && again.find("auto 2 20 1 0.75\n") != std::string::npos && again.find("auto 1 24") == std::string::npos);
    }

    // WRITE: a send moved while the song plays is written on its lane (20 ECHO, 21 SPACE), and the pattern then moves it.
    {
        Rig rig;
        Session& s = rig.app.session();
        TrackData& track = rig.project().tracks[0];
        const auto written = [&](int lane) { int n = 0; for (float v : track.automation[0][lane]) n += v >= 0.f ? 1 : 0; return n; };
        s.setSend(0, 1, 0.4f);
        CHECK(written(21) == 0);
        rig.key(SELECT, true);
        rig.tap(X);
        rig.key(SELECT, false);
        rig.frames(1);
        CHECK(s.writing());
        rig.tap(START);
        rig.render(2 * kStep + kStep / 2);
        rig.frames(1);
        CHECK(rig.project().playStep == 2);
        s.setSend(0, 0, 0.8f);
        s.setSend(0, 1, 0.4f); // not a change, but a move all the same: it is written
        CHECK(written(20) == 1 && track.automation[0][20][2] == 0.8f && written(21) == 1 && track.automation[0][21][2] == 0.4f);
        CHECK(s.hasAutomation(0, 0, autoLaneOfSend(0)) && autoLaneOfSend(0) == 20 && autoLaneOfSend(1) == 21);
        // The third slot's settings are lanes 16 to 19; another effect in the slot clears them, and only them.
        s.setEffect(0, 2, EffectKind::Filter);
        rig.render(kStep);
        rig.frames(1);
        s.setEffectParam(0, 2, 1, 0.9f);
        CHECK(written(17) == 1 && track.automation[0][17][3] == 0.9f && autoLaneOfEffect(2, 1) == 17);
        // A bus's setting and the master's are not automated: nothing is written anywhere.
        int before = 0;
        for (int lane = 0; lane < kAutoLanes; ++lane) before += written(lane);
        s.setEffectParam(kEchoBus, 0, 1, 0.3f);
        s.setEffectParam(kSpaceBus, 0, 1, 0.3f);
        int after = 0;
        for (int lane = 0; lane < kAutoLanes; ++lane) after += written(lane);
        CHECK(before == after && before == 3);
        s.setEffect(0, 2, EffectKind::Crush);
        CHECK(written(17) == 0 && written(20) == 1 && written(21) == 1);
        // Changing what a bus holds leaves every track's automation alone.
        s.setEffect(kEchoBus, 0, EffectKind::None);
        s.setEffect(kSpaceBus, 0, EffectKind::None);
        CHECK(written(20) == 1 && written(21) == 1);
    }

    // Deleting a track: the sends go with their tracks, in the song and in the engine.
    {
        Rig rig, same;
        addPhazed(rig);
        addPhazed(same);
        rig.app.session().setSend(1, 0, 0.6f);
        rig.app.session().setSend(0, 1, 0.9f);
        CHECK(rig.app.session().removeTrack(0));
        CHECK(rig.project().tracks[0].send[0] == 0.6f && rig.project().tracks[0].send[1] == 0.f && rig.project().tracks[1].send[0] == 0.f);
        CHECK(same.app.session().removeTrack(0));
        same.app.session().setSend(0, 0, 0.6f);
        rig.render(1200000);
        same.render(1200000);
        rig.app.session().audition(0, 60, 0.8f);
        same.app.session().audition(0, 60, 0.8f);
        const std::vector<float> x = rig.render(48000), y = same.render(48000);
        CHECK(Rig::peak(x) > 0.01f && x == y && rig.engine->busAwake(0) && !rig.engine->busAwake(1));
    }
}

// The FX screen: two pages of four rows, the sends, the buses and the group jump.
void testTheFxScreensTwoPages() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    rig.frames(1);
    const int delay = static_cast<int>(EffectKind::Delay), reverb = static_cast<int>(EffectKind::Reverb), filter = static_cast<int>(EffectKind::Filter);
    CHECK(FxScreen::kRows == 8 && FxScreen::kSendsRow == 3 && FxScreen::kBusRow == 4 && FxScreen::kMasterRow == 6);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 0 && fx.page() == 0);

    // Down the track's page: three slots, the sends, and on to the other page; up comes back.
    rig.tap(DOWN, 3);
    CHECK(fx.cursorRow() == 3 && fx.page() == 0);
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 4 && fx.page() == 1);
    rig.tap(DOWN, 3);
    CHECK(fx.cursorRow() == 7 && fx.page() == 1);
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 0 && fx.page() == 0);
    rig.tap(UP);
    CHECK(fx.cursorRow() == 7);
    rig.tap(UP, 4);
    CHECK(fx.cursorRow() == 3);
    // Y: the other page, the same row of it; and back.
    rig.tap(Y);
    CHECK(fx.cursorRow() == 7 && fx.page() == 1);
    rig.tap(Y);
    CHECK(fx.cursorRow() == 3 && fx.page() == 0);
    fx.goTo(1, 0);
    rig.tap(Y);
    CHECK(fx.cursorRow() == 5);
    rig.tap(Y);
    CHECK(fx.cursorRow() == 1);
    // Not while A is held: A is turning something.
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    CHECK(fx.cursorRow() == 1);
    // The cursor keeps to what the row has: two sends; an effect and its settings; nothing beside an empty slot.
    fx.goTo(3, 0);
    rig.tap(RIGHT, 3);
    CHECK(fx.cursorColumn() == 1);
    rig.tap(DOWN); // ECHO's row: DELAY and its four settings
    CHECK(fx.cursorRow() == 4 && fx.cursorColumn() == 1);
    rig.tap(RIGHT, 9);
    CHECK(fx.cursorColumn() == 4);
    rig.tap(DOWN); // SPACE's: REVERB has three
    CHECK(fx.cursorColumn() == 3);
    rig.tap(DOWN); // the master's first slot: empty
    CHECK(fx.cursorRow() == 6 && fx.cursorColumn() == 0);

    // The sends. Hold A + the D-pad: a twentieth at a push, stopping at the ends.
    fx.goTo(3, 0);
    rig.key(A, true);
    rig.tap(UP, 3);
    CHECK(near(project.tracks[0].send[0], 0.15f) && project.tracks[0].send[1] == 0.f);
    rig.tap(DOWN);
    CHECK(near(project.tracks[0].send[0], 0.10f));
    rig.tap(DOWN, 5);
    CHECK(project.tracks[0].send[0] == 0.f);
    rig.tap(LEFT);
    rig.tap(RIGHT); // sideways with A held does nothing to a send, and does not move off it
    CHECK(project.tracks[0].send[0] == 0.f && project.tracks[0].send[1] == 0.f && fx.cursorColumn() == 0);
    rig.key(A, false);
    rig.tap(RIGHT);
    rig.key(A, true);
    rig.tap(UP, 25);
    CHECK(project.tracks[0].send[1] == 1.f && project.tracks[0].send[0] == 0.f);
    rig.tap(DOWN, 2);
    CHECK(near(project.tracks[0].send[1], 0.9f));
    // The stick turns it smoothly.
    rig.key(STICK_DOWN, true);
    rig.frames(10);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    const float turned = 0.9f - project.tracks[0].send[1];
    CHECK(turned > 0.02f && turned < 0.3f && project.tracks[0].send[0] == 0.f);
    CHECK(std::fabs(turned / 0.05f - std::round(turned / 0.05f)) > 0.02f); // smoothly: not in twentieths
    // ...the first send as the second.
    rig.key(A, false);
    rig.tap(LEFT);
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(10);
    rig.key(STICK_UP, false);
    rig.frames(1);
    const float echo = project.tracks[0].send[0];
    CHECK(echo > 0.02f && echo < 0.3f && std::fabs(echo / 0.05f - std::round(echo / 0.05f)) > 0.02f);
    rig.key(A, false);
    session.setSend(0, 0, 0.f);
    rig.tap(RIGHT);
    // It reaches the engine: a hit now wakes the SPACE bus and not the ECHO bus.
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    rig.engine->send(hit);
    rig.render(4800);
    CHECK(rig.engine->busAwake(1) && !rig.engine->busAwake(0));
    // B: nothing sent, and its automation in this cell goes; the other send's stays, and so does another scene's.
    session.setSend(0, 0, 0.4f);
    session.setAutomation(0, 0, autoLaneOfSend(0), 3, 0.5f);
    session.setAutomation(0, 0, autoLaneOfSend(1), 3, 0.5f);
    session.setAutomation(0, 2, autoLaneOfSend(1), 3, 0.5f);
    rig.tap(B);
    CHECK(project.tracks[0].send[1] == 0.f && project.tracks[0].send[0] == 0.4f);
    CHECK(!session.hasAutomation(0, 0, autoLaneOfSend(1)) && session.hasAutomation(0, 0, autoLaneOfSend(0)) && session.hasAutomation(0, 2, autoLaneOfSend(1)));
    rig.tap(LEFT);
    rig.tap(B);
    CHECK(project.tracks[0].send[0] == 0.f && !session.hasAutomation(0, 0, autoLaneOfSend(0)));
    // With WRITE on and the song playing, B's reset is not itself written down.
    rig.key(SELECT, true);
    rig.tap(X);
    rig.key(SELECT, false);
    rig.tap(START);
    rig.render(kStep + kStep / 2);
    rig.frames(1);
    CHECK(session.writing() && project.playing);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(session.hasAutomation(0, 0, autoLaneOfSend(0)) && near(project.tracks[0].automation[0][20][1], 0.05f));
    rig.tap(B);
    CHECK(!session.hasAutomation(0, 0, autoLaneOfSend(0)) && session.writing());
    rig.tap(START);
    rig.key(SELECT, true);
    rig.tap(X);
    rig.key(SELECT, false);
    rig.frames(1);
    CHECK(!session.writing() && !project.playing);
    // The sends are the track's being worked on.
    addPhazed(rig);
    project.track = 1;
    session.view = View::Effects;
    fx.goTo(3, 0);
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(near(project.tracks[1].send[0], 0.1f) && project.tracks[0].send[0] == 0.f);
    project.track = 0;

    // The third slot is a slot like the others.
    fx.goTo(2, 0);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(project.tracks[0].effects[2].kind == filter && project.tracks[0].effects[0].kind == 0 && project.tracks[0].effects[1].kind == 0);
    rig.tap(B);
    CHECK(project.tracks[0].effects[2].kind == 0);

    // A bus offers its own kind only: NONE and the delays on ECHO, NONE and the reverbs on SPACE.
    // (Which delay and which reverb come first is the list's business.)
    const EffectKind echoKind = busEffectAt(0, 1), spaceKind = busEffectAt(1, 1);
    const int echo1 = static_cast<int>(echoKind), space1 = static_cast<int>(spaceKind);
    const int echoMix = effectMixParam(echoKind), spaceMix = effectMixParam(spaceKind);
    CHECK(echo1 != 0 && space1 != 0 && echoMix == 3 && spaceMix >= 0);
    fx.goTo(4, 0);
    rig.key(A, true);
    for (int i = 0; i < 7; ++i) {
        rig.tap(UP);
        CHECK(project.busEffects[0].kind != 0 && effectBus(static_cast<EffectKind>(project.busEffects[0].kind)) == 0);
    }
    CHECK(project.busEffects[0].kind == static_cast<int>(busEffectAt(0, busListSize(0) - 1))); // the end of its list stops
    rig.tap(DOWN, 8);
    CHECK(project.busEffects[0].kind == 0 && project.busEffects[1].kind == static_cast<int>(kNewSongBus[1]));
    rig.tap(RIGHT); // no groups to jump on a bus
    CHECK(project.busEffects[0].kind == 0 && fx.cursorRow() == 4 && fx.cursorColumn() == 0);
    rig.tap(UP);
    rig.tap(LEFT);
    CHECK(project.busEffects[0].kind == echo1 && fx.cursorColumn() == 0);
    rig.tap(DOWN);
    CHECK(project.busEffects[0].kind == 0);
    rig.tap(UP);
    CHECK(project.busEffects[0].kind == echo1 && project.busEffects[0].param[echoMix] == 1.f); // in at its usual settings, LEVEL full
    rig.key(A, false);
    fx.goTo(5, 0);
    rig.tap(B);
    CHECK(project.busEffects[1].kind == 0 && project.busEffects[0].kind == echo1);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(project.busEffects[1].kind == space1 && project.busEffects[1].param[spaceMix] == 1.f);
    rig.tap(UP, 4);
    CHECK(effectBus(static_cast<EffectKind>(project.busEffects[1].kind)) == 1);
    rig.key(A, false);
    // LEVEL (the echo's MIX, its fourth setting): turned like any setting; B puts it back to full, not to the MIX an effect on a track starts at.
    fx.goTo(4, 4);
    rig.key(A, true);
    rig.tap(DOWN, 4);
    rig.key(A, false);
    CHECK(near(project.busEffects[0].param[3], 0.8f));
    rig.tap(B);
    CHECK(project.busEffects[0].param[3] == 1.f);
    fx.goTo(4, 2);
    session.setEffectParam(kEchoBus, 0, 1, 0.9f);
    rig.tap(B);
    CHECK(project.busEffects[0].param[1] == effectInfo(echoKind).defaults[1]);
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 3, 0.9f);
    fx.goTo(0, 4);
    rig.tap(B);
    CHECK(project.tracks[0].effects[0].param[3] == effectInfo(EffectKind::Delay).defaults[3]);
    session.setEffect(0, 0, EffectKind::None);

    // The master's two slots are the last two rows.
    fx.goTo(6, 0);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(project.masterEffects[0].kind == filter && project.masterEffects[1].kind == 0);
    fx.goTo(7, 0);
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(project.masterEffects[1].kind == static_cast<int>(effectAt(2)) && project.masterEffects[0].kind == filter);
    fx.goTo(6, 0);
    rig.tap(B);
    fx.goTo(7, 0);
    rig.tap(B);
    CHECK(project.masterEffects[0].kind == 0 && project.masterEffects[1].kind == 0);

    // Hold A + left / right on an effect: a whole group of the list at once.
    fx.goTo(0, 0);
    const auto kindNow = [&] { return static_cast<EffectKind>(project.tracks[0].effects[0].kind); };
    rig.key(A, true);
    int place = 0;
    for (int i = 0; i < effectListSize() + 2; ++i) {
        const int to = effectGroupJump(place, 1);
        rig.tap(RIGHT);
        CHECK(effectPlace(kindNow()) == to && (to == place || effectInfo(effectAt(to)).group != effectInfo(effectAt(place)).group));
        place = to;
    }
    CHECK(place > 0 && effectInfo(effectAt(place)).group == effectInfo(effectAt(effectListSize() - 1)).group); // the last group: it stops
    // ...and stopping is doing nothing: the effect there is not put in afresh, so its settings stay as they were turned.
    if (effectInfo(kindNow()).params > 0) {
        session.setEffectParam(0, 0, 0, 0.123f);
        rig.tap(RIGHT);
        CHECK(project.tracks[0].effects[0].param[0] == 0.123f);
    }
    for (int i = 0; i < effectListSize() + 2; ++i) {
        const int to = effectGroupJump(place, -1);
        rig.tap(LEFT);
        CHECK(effectPlace(kindNow()) == to && to <= place);
        place = to;
    }
    CHECK(place == 0 && kindNow() == EffectKind::None && fx.cursorColumn() == 0);
    // The stick: one push, one jump.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(effectPlace(kindNow()) == effectGroupJump(0, 1) && kindNow() != EffectKind::None);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(kindNow() == EffectKind::None);
    // On a setting, left and right with A held do nothing at all.
    session.setEffect(0, 0, EffectKind::Filter);
    fx.goTo(0, 1);
    const float cutoff = project.tracks[0].effects[0].param[0];
    rig.tap(RIGHT);
    CHECK(project.tracks[0].effects[0].param[0] == cutoff && fx.cursorColumn() == 1);
    rig.tap(LEFT);
    CHECK(project.tracks[0].effects[0].kind == filter && project.tracks[0].effects[0].param[0] == cutoff && fx.cursorColumn() == 1);
    rig.key(A, false);
    // Without A they move the cursor, as ever.
    rig.tap(LEFT);
    CHECK(fx.cursorColumn() == 0 && project.tracks[0].effects[0].kind == filter);
}

void testTheKeysShownFollowTheCursor() {
    std::unique_ptr<Rig> rig(onNotes());
    NoteScreen& notes = rig->app.notes();
    notes.moveCursorTo(60, 0);
    const int low = notes.lowestKeyShown();
    CHECK(low <= 60 && low + notes.visibleKeys() > 60);
    notes.moveCursorTo(low + notes.visibleKeys() - 1, 0); // the last key showing: no scroll
    CHECK(notes.lowestKeyShown() == low);
    rig->tap(RIGHT); // one past it: the keys move one along
    CHECK(notes.lowestKeyShown() == low + 1);
    notes.moveCursorTo(100, 0);
    CHECK(notes.lowestKeyShown() == 100 - (notes.visibleKeys() - 1));
    notes.moveCursorTo(30, 0);
    CHECK(notes.lowestKeyShown() == 30);
    rig->tap(RIGHT, 5);
    CHECK(notes.lowestKeyShown() == 30); // inside what shows: nothing moves
}

void testTheScreensKnowWhereTheSongIs() {
    // What the progress blocks are drawn from.
    Rig rig;
    rig.app.drums().setStep(0, 0, 1.f);
    rig.project().scene = 1;
    rig.app.drums().setStep(0, 0, 1.f);
    rig.project().scene = 0;
    rig.project().repeats[0] = 2;
    {
        Command c;
        c.type = Command::SetSceneRepeats;
        c.scene = 0;
        c.a = 2;
        rig.engine->send(c);
    }
    rig.app.session().view = View::Pattern;
    rig.frames(2);
    CHECK(rig.project().playScene == -1 && rig.project().playStep == -1);
    rig.tap(START); // from the Pattern screen: this scene, round and round
    CHECK(rig.project().playLoop);
    rig.render(kBar + 2 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playScene == 0 && rig.project().playStep == 2);
    rig.tap(START);
    rig.render(100);
    rig.app.session().view = View::Song;
    rig.tap(START); // from the Song screen: the song
    CHECK(!rig.project().playLoop);
    rig.render(kBar + 5 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playScene == 0 && rig.project().playRepeat == 1 && rig.project().playStep == 5);
    rig.render(kBar);
    rig.frames(1);
    CHECK(rig.project().playScene == 1 && rig.project().playRepeat == 0);
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
    SynthScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    CHECK(rig->view() == View::Device && screen.page() == 0 && screen.cursor() == SynthScreen::kPreset);
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
    CHECK(screen.cursor() == SynthScreen::kFolder);
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
    SynthScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    rig->tap(DOWN);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 2); // under PRESET: the right half of the row
    rig->tap(LEFT, 2);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro); // TONE
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
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 3);
    rig->tap(DOWN);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 7);
    rig->tap(DOWN);
    CHECK(screen.cursor() == SynthScreen::kAdvanced);
    rig->tap(UP);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 4);

    // Choosing another preset clears the macros.
    rig->key(A, true);
    rig->tap(UP, 4);
    rig->key(A, false);
    CHECK(track.macro[phazed::Detune] > 0.5f);
    rig->app.phazed().goTo(0, SynthScreen::kPreset);
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
    bent->app.phazed().goTo(0, SynthScreen::kFirstMacro + phazed::MacroOctave);
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
    SynthScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    screen.goTo(0, SynthScreen::kAdvanced);
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
    CHECK(screen.page() == 0 && screen.cursor() == SynthScreen::kAdvanced);
    rig->tap(L1); // the page buttons do nothing on MAIN
    rig->tap(R1);
    CHECK(screen.page() == 0);
    // Every page's cursor stops map to settings inside the patch.
    for (int page = 1; page < SynthScreen::kPages; ++page) {
        for (int stop = 0; stop < 40; ++stop) {
            screen.goTo(page, stop);
            CHECK(screen.settingHere() >= 0 && screen.settingHere() < phazed::kSettings);
        }
    }
}

void testEveryPatchSettingIsOnSomePage() {
    std::unique_ptr<Rig> rig(onPhazed());
    SynthScreen& screen = rig->app.phazed();
    bool seen[phazed::kSettings] = {};
    for (int page = 1; page < SynthScreen::kPages; ++page) {
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
    // The chooser goes round all five.
    rig->tap(RIGHT);
    rig->tap(A);
    CHECK(rig->app.song().choosing());
    rig->tap(DOWN, 4); // PHAZED > TWIN > SAMPLER > QMOD > DRUMS
    rig->tap(A);
    CHECK(rig->project().tracks[2].kind == TrackKind::Drums);
    rig->tap(RIGHT);
    rig->tap(A);
    rig->tap(UP, 3); // PHAZED > DRUMS > QMOD > SAMPLER
    rig->tap(A);
    CHECK(rig->project().tracks[3].kind == TrackKind::Sampler);

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
    SynthScreen& screen = rig->app.twin();
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
    screen.goTo(0, SynthScreen::kFirstMacro + twin::Blend);
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

// A full command queue loses commands (it takes the audio thread standing
// still for seconds). The app notices, waits for the queue to empty, and
// sends the whole song again: what is on the screen is what plays.
void testAFullQueueIsRecoveredFrom() {
    Rig rig;
    rig.frames(2);
    rig.listen(512);
    Session& session = rig.app.session();
    CHECK(session.resyncs() == 0 && session.engine.dropped() == 0 && session.engine.caughtUp());

    // Nothing is rendered, and the queue is filled to the brim.
    Command tempo;
    tempo.type = Command::SetTempo;
    tempo.value = 120.f;
    int sent = 0;
    while (session.engine.send(tempo)) ++sent;
    CHECK(sent > 1000 && session.engine.dropped() == 1 && !session.engine.caughtUp());

    // A kick placed now is on the screen and never reaches the engine.
    rig.tap(A);
    CHECK(rig.project().tracks[0].drums[0][0].level[0][0] > 0.f);
    const unsigned lost = session.engine.dropped();
    CHECK(lost > 1);
    rig.frames(3);
    CHECK(session.resyncs() == 0); // not while the queue is still full: sending more would only lose more

    // The audio thread comes back and empties the queue. The next frame sends the song again.
    rig.listen(512);
    CHECK(session.engine.caughtUp());
    rig.frames(1);
    CHECK(session.resyncs() == 1 && session.engine.dropped() == lost);
    CHECK(std::strlen(rig.app.undoSaid()) > 0); // and says so
    rig.frames(3);
    CHECK(session.resyncs() == 1); // once

    rig.tap(START);
    CHECK(rig.listen(kStep) > 0.05f); // the kick, on the first step
}

// ---- step effects: hold X on a drum hit ---------------------------------------------------

// How long whatever is heard next lasts, in frames (from the first sound to the last).
int soundsFor(Rig& rig, int within = 60000) {
    const std::vector<float> out = rig.render(within);
    int first = -1, last = -1;
    for (int i = 0; i < within; ++i) {
        if (std::fabs(out[2 * static_cast<std::size_t>(i)]) < 1e-5f) continue;
        if (first < 0) first = i;
        last = i;
    }
    return first < 0 ? 0 : last - first + 1;
}

void testStepEffects() {
    Rig rig;
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    drums.moveCursorTo(0, 4);
    rig.tap(A); // a kick on step 5
    rig.frames(2);
    const int plain = (rig.render(60000), rig.tap(X), soundsFor(rig));
    CHECK(plain > 3000 && !drums.fx(0, 4).any() && !drums.effecting());

    // X held on a hit: up and down are its pitch, a semitone a click. The cursor stays where it is.
    rig.key(X, true);
    CHECK(drums.effecting());
    rig.tap(UP);
    CHECK(drums.fx(0, 4).pitch == 1 && drums.cursorLane() == 0 && drums.cursorStep() == 4);
    rig.tap(UP, 20);
    CHECK(drums.fx(0, 4).pitch == 12); // an octave is the most
    rig.render(60000);
    rig.tap(UP);
    CHECK(soundsFor(rig, 6000) == 0); // against the stop nothing changes, so nothing is played
    rig.tap(DOWN, 30);
    CHECK(drums.fx(0, 4).pitch == -12);
    rig.tap(UP, 12);
    CHECK(drums.fx(0, 4).pitch == 0 && !drums.fx(0, 4).any());
    // Left and right: how many times it is played inside the step.
    rig.tap(RIGHT);
    CHECK(drums.fx(0, 4).hits() == 2 && drums.cursorStep() == 4);
    rig.tap(RIGHT, 5);
    CHECK(drums.fx(0, 4).hits() == 4 && drums.fx(0, 4).ratchet == 4);
    rig.tap(LEFT, 9);
    CHECK(drums.fx(0, 4).hits() == 1 && drums.fx(0, 4).ratchet == 0 && !drums.fx(0, 4).any()); // "one" is kept one way
    // L1: backwards, and back. It does not open the slices, and R1 does not paint.
    rig.tap(L1);
    CHECK(drums.fx(0, 4).reverse == 1 && !drums.slicing());
    rig.tap(L1);
    CHECK(drums.fx(0, 4).reverse == 0);
    // R1: the cut, round: a step, a half, a quarter, off.
    for (int expected : {1, 2, 3, 0, 1}) {
        rig.tap(R1);
        CHECK(drums.fx(0, 4).cut == expected && !drums.painting());
    }
    rig.tap(R1, 3);
    CHECK(!drums.fx(0, 4).any() && drums.level(0, 5) == 0.f && drums.level(0, 3) == 0.f);
    rig.key(X, false);
    CHECK(!drums.effecting());
    // With X let go the D-pad is the cursor again, and L1 and R1 their usual selves.
    rig.tap(RIGHT);
    CHECK(drums.cursorStep() == 5 && !drums.fx(0, 4).any());
    rig.tap(LEFT);

    // Held over, the D-pad runs: clicks, some eight a second up and down, four sideways. Not the cursor's 33.
    rig.key(X, true);
    rig.key(UP, true);
    rig.frames(62); // a second
    rig.key(UP, false);
    CHECK(drums.fx(0, 4).pitch >= 5 && drums.fx(0, 4).pitch <= 9);
    rig.tap(DOWN, 12);
    rig.key(RIGHT, true);
    rig.frames(20); // a third of a second: the press, and one more after the wait
    rig.key(RIGHT, false);
    CHECK(drums.fx(0, 4).hits() == 3);
    rig.tap(LEFT, 4);
    // The stick: a click at once, and then it runs. One way per push.
    drums.setFx(0, 4, StepFx());
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(drums.fx(0, 4).pitch == 1);
    rig.key(STICK_RIGHT, true); // pushed across as well, mid-push: it is still the pitch's push
    rig.frames(30);
    rig.key(STICK_RIGHT, false);
    CHECK(drums.fx(0, 4).hits() == 1 && drums.fx(0, 4).pitch > 1);
    rig.frames(32);
    CHECK(drums.fx(0, 4).pitch >= 6 && drums.fx(0, 4).pitch <= 11 && drums.fx(0, 4).hits() == 1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(drums.fx(0, 4).hits() == 2);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    rig.key(X, false);
    const int wasPitch = drums.fx(0, 4).pitch;
    CHECK(drums.fx(0, 4).hits() == 1 && wasPitch >= 5);
    // The stick with X let go moves the cursor, as ever.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(drums.cursorStep() == 5 && drums.fx(0, 4).pitch == wasPitch);
    drums.moveCursorTo(0, 4);
    drums.setFx(0, 4, StepFx());

    // Each change is heard, as the pattern will play it. An octave up: the kick is over in half the time.
    rig.render(60000);
    rig.frames(20);
    rig.key(X, true);
    CHECK(std::abs(soundsFor(rig) - plain) < plain / 20); // X itself: the hit as it is
    rig.tap(UP, 12);
    int lasted = soundsFor(rig);
    CHECK(lasted > plain / 2 - plain / 10 && lasted < plain / 2 + plain / 10);
    // Cut to a quarter of a step: 1500 frames and a fade.
    rig.tap(DOWN, 12);
    rig.render(60000);
    rig.tap(R1, 3);
    lasted = soundsFor(rig);
    CHECK(lasted >= 1500 && lasted <= 1560);
    // A tap of X on it afterwards plays the same.
    rig.key(X, false);
    rig.render(60000);
    rig.tap(X);
    lasted = soundsFor(rig);
    CHECK(lasted >= 1500 && lasted <= 1560);
    // A ratchet of three, heard by hand: three kicks, a third of a step apart (the last ends 4000 frames after the first began).
    rig.key(X, true);
    rig.render(60000);
    rig.tap(RIGHT, 2);
    lasted = soundsFor(rig);
    CHECK(lasted >= 4000 + 1500 && lasted <= 4000 + 1560);
    rig.key(X, false);
    // Nothing is played over the song while it runs.
    rig.render(60000);
    rig.tap(START);
    rig.key(X, true);  // (X itself hears the hit, playing or not: three short kicks, over inside the first step)
    rig.render(kStep);
    rig.tap(UP);
    CHECK(drums.fx(0, 4).pitch == 1);
    rig.tap(DOWN);
    rig.tap(L1);
    rig.tap(L1);
    rig.key(X, false);
    CHECK(soundsFor(rig, 2 * kStep) == 0); // steps 2 and 3 of a bar whose only hit is on step 5: the changes made no sound
    // ...and the pattern plays the hit with its effects: on step 5, three short kicks.
    const std::vector<float> bar = rig.render(13 * kStep); // steps 4 to 16
    CHECK(Rig::peak(bar, 0, kStep - 10) == 0.f && Rig::peak(bar, kStep, kStep + 1400) > 0.05f && Rig::peak(bar, kStep + 1600, kStep + 1950) == 0.f);
    CHECK(Rig::peak(bar, kStep + 2000, kStep + 3400) > 0.05f && Rig::peak(bar, kStep + 4000, kStep + 5400) > 0.05f && Rig::peak(bar, 2 * kStep + 100, 13 * kStep) == 0.f);
    rig.tap(START);
    rig.render(60000);

    // The song keeps them, on a line of their own under the hit; a plain hit has no such line.
    drums.moveCursorTo(2, 9);
    rig.tap(A);
    std::string text = songText(rig.project());
    CHECK(text.find("\nstepfx 1 5 0 3 0 3\n") != std::string::npos && text.find("stepfx 3 10") == std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    const StepFx& kept = back->tracks[0].drums[0][0].fx[0][4];
    CHECK(kept.pitch == 0 && kept.ratchet == 3 && kept.reverse == 0 && kept.cut == 3 && !back->tracks[0].drums[0][0].fx[2][9].any());
    // Nonsense in a file is held to what there is; a line for a step or lane there is not is skipped.
    const std::size_t at = text.find("stepfx 1 5 0 3 0 3");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, 18, "stepfx 1 5 -40 9 7 5\nstepfx 99 5 3 3 1 1\nstepfx 9 5 3 3 1 1\nstepfx 0 5 3 3 1 1\nstepfx 1 99 3 3 1 1\nstepfx 1 17 3 3 1 1\nstepfx 3 10 2");
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(text, *odd));
    const StepFx& held = odd->tracks[0].drums[0][0].fx[0][4];
    CHECK(held.pitch == -12 && held.ratchet == 4 && held.reverse == 1 && held.cut == 0);
    // A lane just past the last did not land on what the pattern keeps after its effects (the dice's seeds), nor a step past the last on the next lane.
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(odd->tracks[0].drums[0][0].diceSeed[lane] == back->tracks[0].drums[0][0].diceSeed[lane]);
    CHECK(!odd->tracks[0].drums[0][0].fx[1][0].any() && !odd->tracks[0].drums[0][0].fx[0][0].any());
    CHECK(odd->tracks[0].drums[0][0].fx[2][9].pitch == 2 && odd->tracks[0].drums[0][0].fx[2][9].hits() == 1); // a short line: the rest as usual
    // Opened afresh, it plays as it did.
    Rig opened;
    opened.project() = *back;
    opened.app.session().applyProject();
    opened.frames(2);
    opened.render(60000);
    opened.tap(START);
    const std::vector<float> again = opened.render(6 * kStep);
    CHECK(Rig::peak(again, 4 * kStep, 4 * kStep + 1400) > 0.05f && Rig::peak(again, 4 * kStep + 1600, 4 * kStep + 1950) == 0.f && Rig::peak(again, 4 * kStep + 2000, 4 * kStep + 3400) > 0.05f);

    // Deleting a hit takes its effects with it: the next hit placed there is plain.
    drums.moveCursorTo(0, 4);
    rig.tap(B);
    CHECK(!drums.fx(0, 4).any());
    rig.tap(A);
    CHECK(drums.level(0, 4) > 0.f && !drums.fx(0, 4).any());
    // The whole cell sent again (as a copy or a cleared cell does) takes each hit's effects with it.
    {
        StepFx quarter;
        quarter.cut = 3;
        drums.setFx(0, 4, quarter);
        rig.app.session().sendDrumPattern(0, 0);
        rig.frames(2);
        rig.render(60000);
        rig.tap(START);
        const std::vector<float> sent = rig.render(6 * kStep);
        rig.tap(START);
        CHECK(Rig::peak(sent, 4 * kStep, 4 * kStep + 1400) > 0.05f && Rig::peak(sent, 4 * kStep + 1600, 6 * kStep) == 0.f);
        drums.setFx(0, 4, StepFx());
        rig.render(60000);
    }
    // Undo takes an effect back: a change to an effect is a step of its own.
    rig.frames(60);
    const int stepsBefore = rig.app.session().undoSteps();
    rig.key(X, true);
    rig.tap(L1);
    rig.key(X, false);
    CHECK(drums.fx(0, 4).reverse == 1);
    rig.frames(60);
    CHECK(rig.app.session().undoSteps() == stepsBefore + 1 || stepsBefore == 20);
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    CHECK(drums.fx(0, 4).reverse == 0 && drums.level(0, 4) > 0.f);

    // X held, Y: the dice, for what the hit does. Every roll is something, and something else than
    // it was; the hit is heard as it now is; the hit's chance (Y's job without X) is left alone.
    {
        drums.moveCursorTo(0, 4);
        drums.setFx(0, 4, StepFx());
        const float chanceWas = drums.chance(0, 4), levelWas = drums.level(0, 4);
        rig.render(60000);
        rig.key(X, true);
        rig.render(60000); // X's own hearing of the hit, gone by
        bool ratchets = false, pitches = false, reverses = false, cuts = false, two = false;
        for (int i = 0; i < 60; ++i) {
            const StepFx was = drums.fx(0, 4);
            rig.key(Y, true);
            rig.frames(2);
            const StepFx now = drums.fx(0, 4);
            rig.key(Y, false); // one roll a press: letting go is not another
            rig.frames(2);
            CHECK(drums.fx(0, 4).pitch == now.pitch && drums.fx(0, 4).hits() == now.hits() && drums.fx(0, 4).reverse == now.reverse && drums.fx(0, 4).cut == now.cut);
            CHECK(now.any());
            CHECK(now.pitch != was.pitch || now.hits() != was.hits() || now.reverse != was.reverse || now.cut != was.cut);
            CHECK(now.pitch >= -12 && now.pitch <= 12 && now.hits() >= 1 && now.hits() <= 4 && now.cut < kCutChoices && now.reverse <= 1);
            if (i == 0) CHECK(soundsFor(rig, 6000) > 0); // heard
            ratchets = ratchets || now.hits() > 1;
            pitches = pitches || now.pitch != 0;
            reverses = reverses || now.reverse;
            cuts = cuts || now.cut;
            two = two || (now.hits() > 1 && now.pitch != 0);
            rig.render(4800);
        }
        CHECK(ratchets && pitches && reverses && cuts && two);
        CHECK(drums.chance(0, 4) == chanceWas && drums.level(0, 4) == levelWas && drums.effecting());
        // A roll is undone like any other change.
        rig.frames(60);
        const StepFx settled = drums.fx(0, 4);
        rig.tap(Y);
        const StepFx rolled = drums.fx(0, 4);
        rig.key(X, false);
        rig.frames(60);
        rig.key(SELECT, true);
        rig.tap(B);
        rig.key(SELECT, false);
        CHECK(drums.fx(0, 4).pitch == settled.pitch && drums.fx(0, 4).hits() == settled.hits() && drums.fx(0, 4).reverse == settled.reverse && drums.fx(0, 4).cut == settled.cut);
        CHECK(rolled.any());
        // With X let go, Y is the chance again.
        rig.tap(Y);
        CHECK(drums.chance(0, 4) < chanceWas);
        drums.setChance(0, 4, chanceWas);
        drums.setFx(0, 4, StepFx());
        rig.frames(60);
        rig.render(60000);
    }

    // X is only a hold on a hit. On an empty step, a lane's name or the tempo it just hears, and the D-pad moves the cursor.
    drums.moveCursorTo(0, 7);
    rig.key(X, true);
    CHECK(!drums.effecting());
    rig.tap(RIGHT);
    CHECK(drums.cursorStep() == 8);
    rig.key(X, false);
    drums.moveCursorTo(0, -1);
    rig.key(X, true);
    CHECK(!drums.effecting());
    rig.key(X, false);
    // Nor while a hit is being shaped (A) or its chance set (Y), or a brush is down.
    drums.moveCursorTo(0, 4);
    for (SDL_Keycode other : {A, Y, R1}) {
        rig.key(other, true);
        rig.key(X, true);
        CHECK(!drums.effecting());
        rig.key(X, false);
        rig.key(other, false);
    }
    drums.setStep(0, 4, 0.8f);
    drums.setFx(0, 4, StepFx());
    // If the hit goes while X is held (deleted), the hold is over.
    rig.key(X, true);
    CHECK(drums.effecting());
    rig.tap(B);
    rig.frames(1);
    CHECK(!drums.effecting() && drums.level(0, 4) == 0.f);
    rig.key(X, false);
    // Setting them from outside holds them to what there is.
    drums.setStep(0, 4, 0.8f);
    StepFx wild;
    wild.pitch = 90;
    wild.ratchet = 77;
    wild.reverse = 9;
    wild.cut = 40;
    drums.setFx(0, 4, wild);
    CHECK(drums.fx(0, 4).pitch == 12 && drums.fx(0, 4).ratchet == 4 && drums.fx(0, 4).reverse == 1 && drums.fx(0, 4).cut == 0);
    wild.pitch = -90;
    wild.ratchet = 1;
    drums.setFx(0, 4, wild);
    CHECK(drums.fx(0, 4).pitch == -12 && drums.fx(0, 4).ratchet == 0);
    drums.setFx(-1, 4, wild);
    drums.setFx(0, 99, wild);
}

// On the SONG screen, up from the top scene is the track's head; A there types its name.
void testNamingATrackFromTheSongScreen() {
    Rig rig;
    rig.app.session().addTrack(TrackKind::Phazed);
    rig.frames(2);
    rig.tap(L2); // SONG
    CHECK(rig.view() == View::Song);
    SongScreen& song = rig.app.song();
    song.moveCursorTo(1, 1);
    rig.tap(UP);
    CHECK(!song.cursorOnHead() && song.cursorScene() == 0);
    rig.tap(UP);
    CHECK(song.cursorOnHead() && song.cursorScene() == 0 && song.cursorColumn() == 1);
    // Left and right along the heads, and no further than the tracks; down is back to the cells.
    rig.tap(LEFT);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 0);
    // Right off the last head is + TRACK, which is drawn beside it; left off the first is the PLAYS column.
    rig.tap(RIGHT, 2);
    CHECK(!song.cursorOnHead() && song.cursorColumn() == 2); // + TRACK
    rig.tap(A);
    rig.tap(B); // the list of instruments came up, and is put away
    CHECK(rig.project().trackCount() == 2);
    rig.tap(LEFT);
    CHECK(song.cursorColumn() == 1 && !song.cursorOnHead());
    rig.tap(UP);
    rig.tap(LEFT, 2);
    CHECK(!song.cursorOnHead() && song.cursorColumn() == -1);
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    rig.tap(DOWN);
    CHECK(!song.cursorOnHead() && song.cursorScene() == 0);
    // The stick running up the scenes stops at the top one: only a press of the D-pad goes onto the head.
    song.moveCursorTo(3, 1);
    rig.key(STICK_UP, true);
    rig.frames(40);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(!song.cursorOnHead() && song.cursorScene() == 0);
    rig.key(UP, true); // and so does a held D-pad that got there by running
    song.moveCursorTo(2, 1);
    rig.frames(60);
    rig.key(UP, false);
    CHECK(!song.cursorOnHead() && song.cursorScene() == 0);
    rig.tap(UP);
    // B there empties nothing, and R1 copies nothing.
    rig.app.session().setTrackName(1, "");
    rig.app.drums().setStep(0, 0, 0.9f); // something in the top cell of track 1, to see it left alone
    rig.tap(LEFT);
    rig.tap(B); // a tap of B there empties nothing
    CHECK(song.cursorOnHead() && rig.view() == View::Song && rig.app.drums().level(0, 0) == 0.9f);
    rig.tap(RIGHT);
    // A: the keyboard, for that track. Type, keep, and the SONG screen is back with the name on the head.
    rig.tap(A);
    rig.frames(1);
    CHECK(rig.view() == View::Settings && rig.app.settings().typing() && rig.project().track == 1);
    rig.tap(A); // the first key
    rig.tap(RIGHT);
    rig.tap(A);
    rig.tap(START);
    CHECK(rig.view() == View::Song && !rig.app.settings().typing() && std::strlen(rig.project().tracks[1].name) == 2 && rig.project().tracks[0].name[0] == 0);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    // B with nothing typed leaves it as it was, and also comes back.
    const std::string kept = rig.project().tracks[1].name;
    rig.tap(A);
    rig.frames(1);
    rig.tap(B, 3);
    CHECK(rig.view() == View::Song && kept == rig.project().tracks[1].name);
    // From the SET tab's own row the keyboard stays on SET when it is done.
    rig.app.session().view = View::Settings;
    rig.app.settings().goTo(SettingsScreen::kTrackName);
    rig.frames(1);
    rig.tap(A);
    CHECK(rig.app.settings().typing());
    rig.tap(START);
    CHECK(rig.view() == View::Settings);
    // Not from the PLAYS column or + TRACK: up there does nothing at the top.
    rig.app.session().view = View::Song;
    song.moveCursorTo(0, -1);
    rig.tap(UP);
    CHECK(!song.cursorOnHead());
}

// On a track's head, R1 changes its instrument.
void testChangingATracksInstrument() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    // Notes on track 2, a hit on track 1, a name, a mute, an effect and some automation of a macro and of the effect.
    rig.project().tracks[1].notes[0][0].notes[0] = NoteEvent{0, 60, 4.f, 0.f, 0.8f};
    session.sendNotePattern(1, 0);
    rig.app.drums().setStep(0, 0, 0.9f);
    session.setTrackName(1, "LEAD");
    session.setEffect(1, 0, EffectKind::Delay);
    session.setAutomation(1, 0, 2, 0, 0.9f);
    session.setAutomation(1, 0, autoLaneOfEffect(0, 1), 0, 0.7f);
    session.setSynthMacro(1, 3, 0.9f);
    rig.frames(2);
    rig.render(4000);
    rig.tap(L2);
    SongScreen& song = rig.app.song();
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    CHECK(song.cursorOnHead());
    // R1: the list, on the instrument the track has. B puts it away with nothing changed.
    rig.tap(R1);
    rig.tap(B);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().trackCount() == 2);
    // A on the same one: nothing changes either (the sound is not put back to its first).
    rig.tap(R1);
    rig.tap(A);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().tracks[1].macro[3] == 0.9f);
    // Down one and A: it is a Twin Analog now. No track was added.
    rig.tap(R1);
    rig.tap(DOWN);
    rig.tap(A);
    TrackData& t = rig.project().tracks[1];
    CHECK(t.kind == TrackKind::Twin && rig.project().trackCount() == 2 && song.cursorOnHead() && song.cursorColumn() == 1);
    // Its notes, name and effect stay; it is on its first sound, with its macros in the middle.
    CHECK(t.notes[0][0].notes[0].velocity == 0.8f && std::string(t.name) == "LEAD" && t.effects[0].kind == static_cast<int>(EffectKind::Delay));
    CHECK(t.preset == 0 && t.macro[3] == 0.5f);
    // The macro's automation went (it is another instrument's knob now); the effect's stayed.
    CHECK(t.automation[0][2][0] < 0.f && t.automation[0][autoLaneOfEffect(0, 1)][0] == 0.7f);
    // The other track is untouched, and the song plays: the drum, and the new instrument's note.
    CHECK(rig.project().tracks[0].kind == TrackKind::Drums && rig.app.drums().level(0, 0) == 0.9f);
    rig.frames(2);
    rig.render(4000);
    session.setMuted(0, true);
    rig.frames(1);
    rig.tap(START);
    CHECK(rig.listen(kStep) > 0.01f);
    rig.tap(START);
    session.setMuted(0, false);
    // To drums and back to a synth: the notes were kept all along.
    CHECK(session.setTrackKind(1, TrackKind::Drums) && t.kind == TrackKind::Drums);
    // The engine has drums there now, with no hits: the synth that was there is gone, and its note with it.
    rig.frames(2);
    rig.render(48000);
    session.setMuted(0, true);
    rig.frames(1);
    rig.render(2000); // the mute fades over a few milliseconds: let it finish before the first hit
    rig.tap(START);
    CHECK(rig.listen(4 * kStep) == 0.f);
    rig.tap(START);
    session.setMuted(0, false);
    rig.frames(1);
    CHECK(session.setTrackKind(1, TrackKind::Qmod) && t.notes[0][0].notes[0].velocity == 0.8f);
    // Nonsense is refused.
    CHECK(!session.setTrackKind(1, TrackKind::None) && !session.setTrackKind(1, TrackKind::Qmod) && !session.setTrackKind(5, TrackKind::Drums) && !session.setTrackKind(-1, TrackKind::Drums));
    // B held on the head deletes the track, as on a cell: two seconds, and let go sooner it does nothing.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(B, true);
    rig.frames(60);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 2 && song.trackDeletePart() == 0.f);
    session.addTrack(TrackKind::Phazed);
    rig.frames(1);
    rig.key(B, true);
    rig.frames(130);
    rig.key(B, false);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().tracks[0].kind == TrackKind::Drums); // the Qmod went; the one after it moved up
    rig.frames(2);
    // + TRACK still adds one (the list is not left thinking it is changing a track).
    song.moveCursorTo(0, 2);
    rig.tap(A);
    rig.tap(A);
    CHECK(rig.project().trackCount() == 3 && rig.project().tracks[1].kind == TrackKind::Phazed);
}

// A note written under a held chord (or a longer note) on the same key is known to be hidden, so it can be drawn on top.
void testANoteUnderAChordIsKnown() {
    Rig rig;
    rig.app.session().addTrack(TrackKind::Phazed);
    rig.project().track = 1;
    rig.frames(1);
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    p.chords[0] = makeChord(builtInPack().chords[0], 60, 2, 8.f, 0.7f, 0); // steps 3 to 10
    int keys[kChordNotes];
    const int count = p.chords[0].pitches(keys);
    CHECK(count >= 3);
    const NoteScreen& roll = rig.app.notes();
    const auto under = [&](int step, int pitch, float length, float nudge = 0.f) {
        p.notes[0] = NoteEvent{step, pitch, length, nudge, 0.8f};
        return roll.isUnderAnother(p.notes[0]);
    };
    CHECK(under(4, keys[1], 2.f));         // inside the chord, on one of its keys
    CHECK(under(2, keys[0], 8.f));         // exactly where the chord's own note is
    CHECK(under(0, keys[2], 3.f));         // begins before it and runs into it
    CHECK(under(9, keys[1], 4.f));         // begins in it and runs out of it
    CHECK(!under(4, keys[1] + 1, 2.f));    // a key the chord does not have
    CHECK(!under(10, keys[1], 2.f));       // just after it has ended
    CHECK(!under(0, keys[1], 2.f));        // ended just as it begins
    CHECK(under(0, keys[1], 2.f, 0.3f));   // ...unless it is pushed late into it
    p.chords[0].velocity = 0.f;            // the chord taken away: nothing is over it
    CHECK(!under(4, keys[1], 2.f));
    // Under a longer note on the same key that began before it. The longer one is not "under" the shorter.
    p.notes[1] = NoteEvent{2, 70, 10.f, 0.f, 0.8f};
    CHECK(under(6, 70, 2.f) && !roll.isUnderAnother(p.notes[1]));
    CHECK(!under(6, 71, 2.f) && !under(12, 70, 2.f) && !under(1, 70, 1.f));
    p.notes[1].velocity = 0.f;
    CHECK(!under(6, 70, 2.f));
}

// A scene's length is any number of steps, set from the PATTERN screen with SELECT + L2 / R2.
void testASceneOfAnyLength() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Phazed);
    session.setCellBars(1, 0, 2); // the scene is two bars: 32 steps
    rig.frames(2);
    CHECK(rig.view() == View::Pattern && rig.project().sceneSteps(0) == 32 && rig.project().barSteps(0, 0) == 16 && rig.project().barSteps(0, 1) == 16);
    // SELECT + L2: a step shorter each press. The tab does not turn.
    rig.key(SELECT, true);
    rig.tap(L2, 9);
    CHECK(rig.project().sceneSteps(0) == 23 && rig.view() == View::Pattern && std::strstr(rig.app.undoSaid(), "23 STEPS OF 32") != nullptr);
    CHECK(rig.project().barSteps(0, 0) == 16 && rig.project().barSteps(0, 1) == 7 && rig.project().barSteps(0, 2) == 0);
    // SELECT + R2: longer, as far as every step, and no further.
    rig.tap(R2, 2);
    CHECK(rig.project().sceneSteps(0) == 25);
    rig.tap(R2, 40);
    CHECK(rig.project().sceneSteps(0) == 32 && rig.project().steps[0] == 0 && std::strstr(rig.app.undoSaid(), "ALL 32 STEPS") != nullptr);
    rig.tap(L2, 60);
    CHECK(rig.project().sceneSteps(0) == 1); // one step is the fewest
    rig.tap(R2, 22);
    rig.key(SELECT, false);
    CHECK(rig.project().sceneSteps(0) == 23);
    // Without SELECT they are the tabs, as ever.
    rig.tap(R2);
    CHECK(rig.view() == View::Device && rig.project().sceneSteps(0) == 23);
    rig.tap(L2);
    // On other tabs SELECT + L2 / R2 leaves the length alone.
    rig.tap(L2);
    rig.key(SELECT, true);
    rig.tap(R2);
    rig.key(SELECT, false);
    CHECK(rig.project().sceneSteps(0) == 23);
    session.view = View::Pattern;
    // It plays 23 steps: a kick on step 1 comes round every 23.
    rig.app.drums().setStep(0, 0, 1.f);
    rig.frames(2);
    rig.render(48000);
    rig.tap(START);
    const std::vector<float> out = rig.render(50 * kStep);
    const auto starts = [&](int s) { float p = 0.f; for (int i = 0; i < 400; ++i) p = std::fmax(p, std::fabs(out[2 * (s * kStep + i)])); return p > 0.3f; };
    CHECK(starts(0) && starts(16) && starts(23) && starts(39) && starts(46) && !starts(32));
    rig.tap(START);
    rig.render(48000);
    // The SONG screen's way does the same, by the same numbers.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, -1);
    rig.key(A, true);
    rig.tap(LEFT);
    CHECK(rig.project().sceneSteps(0) == 22);
    rig.tap(RIGHT, 2);
    rig.key(A, false);
    CHECK(rig.project().sceneSteps(0) == 24);
    // A cell made longer: a scene that was "all of it" grows with it; one cut short keeps its number.
    session.setSceneSteps(0, 999);
    session.setCellBars(1, 0, 4);
    CHECK(rig.project().sceneSteps(0) == 64);
    session.setSceneSteps(0, 40);
    session.setCellBars(1, 0, 2); // shorter than its number now: every step of what there is
    CHECK(rig.project().sceneSteps(0) == 32);
    // The file keeps the number, and a bounce is as long as the scene really is.
    session.setSceneSteps(0, 23);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(songText(rig.project()), *back) && back->steps[0] == 23 && back->sceneSteps(0) == 23);
    CHECK(songFrames(rig.project(), 48000.f) == 23 * kStep);
}

// A cell made longer is filled with what it already plays, so the loop is there twice to be changed.
void testALongerCellIsFilledWithItself() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    TrackData& drums = rig.project().tracks[0];
    TrackData& synth = rig.project().tracks[1];
    rig.app.drums().setStep(0, 0, 1.f);
    rig.app.drums().setStep(1, 4, 0.7f);
    StepFx fx;
    fx.ratchet = 3;
    rig.app.drums().setFx(1, 4, fx);
    synth.notes[0][0].notes[0] = NoteEvent{2, 64, 3.f, 0.f, 0.8f};
    session.sendNotePattern(1, 0);
    session.setAutomation(1, 0, 2, 5, 0.9f);
    // One bar to two: bar 2 is bar 1 again, hits, effects, notes and automation.
    session.setCellBars(0, 0, 2);
    session.setCellBars(1, 0, 2);
    CHECK(drums.drums[0][1].level[0][0] == 1.f && drums.drums[0][1].level[1][4] == 0.7f && drums.drums[0][1].fx[1][4].ratchet == 3);
    CHECK(synth.notes[0][1].notes[0].velocity == 0.8f && synth.notes[0][1].notes[0].pitch == 64 && synth.automation[0][2][16 + 5] == 0.9f);
    CHECK(drums.drums[0][2].empty()); // and no further than it was made
    // The engine has them: played, the kick is on step 1 of both bars.
    rig.frames(2);
    rig.render(48000);
    session.setMuted(1, true);
    rig.frames(1);
    rig.render(2000);
    rig.tap(START);
    const std::vector<float> out = rig.render(32 * kStep);
    rig.tap(START);
    session.setMuted(1, false);
    CHECK(Rig::peak(out, 0, 400) > 0.3f && Rig::peak(out, 16 * kStep, 16 * kStep + 400) > 0.3f);
    // Change bar 2, then two bars to four: bars 3 and 4 are bars 1 and 2.
    drums.drums[0][1].level[0][8] = 0.5f;
    session.setCellBars(0, 0, 4);
    CHECK(drums.drums[0][2].level[0][0] == 1.f && drums.drums[0][2].level[0][8] == 0.f && drums.drums[0][3].level[0][8] == 0.5f);
    // Shortened and made long again: what was written in the hidden bars is still there, not written over.
    drums.drums[0][2].level[3][3] = 0.6f;
    session.setCellBars(0, 0, 1);
    drums.drums[0][0].level[5][5] = 0.9f; // bar 1 changed meanwhile
    session.setCellBars(0, 0, 4);
    CHECK(drums.drums[0][2].level[3][3] == 0.6f && drums.drums[0][2].level[5][5] == 0.f && drums.drums[0][1].level[5][5] == 0.f);
    // Shorter never copies anything; an empty cell made longer stays empty.
    session.setCellBars(1, 1, 2);
    CHECK(synth.notes[1][1].empty() && synth.notes[1][0].empty());
    // From the SONG screen: hold A on the cell and press right.
    session.view = View::Song;
    rig.app.song().moveCursorTo(2, 0);
    rig.app.session().view = View::Song;
    drums.drums[2][0].level[2][2] = 0.8f;
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(drums.bars[2] == 2 && drums.drums[2][1].level[2][2] == 0.8f);
}

// On PRESET with A held, left and right go eight at a time, as in the list of recordings; the ends stop.
void testPresetsEightAtATime() {
    Rig rig;
    rig.app.session().addTrack(TrackKind::Phazed);
    rig.project().track = 1;
    rig.app.session().view = View::Device;
    rig.app.phazed().goTo(0, SynthScreen::kPreset);
    rig.frames(1);
    const TrackData& t = rig.project().tracks[1];
    const int last = Phazed::presetCount() - 1;
    CHECK(t.preset == 0 && last > 20);
    rig.key(A, true);
    rig.tap(RIGHT);
    CHECK(t.preset == 8);
    rig.tap(UP);
    CHECK(t.preset == 9); // up and down are still one
    rig.tap(LEFT);
    CHECK(t.preset == 1);
    rig.tap(LEFT);
    CHECK(t.preset == 0); // the end stops: it does not go round to the last
    rig.tap(RIGHT, 40);
    CHECK(t.preset == last);
    rig.tap(DOWN);
    CHECK(t.preset == last - 1);
    rig.key(A, false);
    // On a macro, left and right are one click as before, not eight.
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro);
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(std::fabs(t.macro[0] - 0.55f) < 1e-4f && t.preset == last - 1);
}

// What a review of the note effects found, each pinned.
void testNoteEffectsHoldUp() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Qmod);
    rig.project().track = 1;
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    NotePattern& p = rig.project().tracks[1].notes[0][0];

    // 1. X tapped to hear a note while the stick is still over (the cursor was on its way): the
    //    note's effects do not change. The stick must be let go and pushed again.
    roll.moveCursorTo(60, 4);
    rig.tap(A);
    const int slot = p.at(60, 4);
    for (SDL_Keycode way : {STICK_RIGHT, STICK_UP}) {
        roll.moveCursorTo(60, 4);
        rig.key(way, true); // (the cursor moves: put it back on the note with the stick still over)
        rig.frames(1);
        roll.moveCursorTo(60, 4);
        const int ratchetWas = p.notes[slot].ratchet, glideWas = p.notes[slot].glide;
        rig.key(X, true);
        rig.frames(40);
        CHECK(roll.effecting() && p.notes[slot].ratchet == ratchetWas && p.notes[slot].glide == glideWas);
        rig.key(way, false);
        rig.frames(2);
        rig.key(way, true); // let go and pushed again: now it is a turn
        rig.frames(1);
        rig.key(way, false);
        rig.frames(2);
        CHECK(way == STICK_RIGHT ? p.notes[slot].ratchet == 2 : p.notes[slot].glide == 1);
        rig.key(X, false);
        rig.frames(2);
    }
    // The D-pad the same: held from a run of the cursor, it does not turn the effect when X goes down.
    p.notes[slot].ratchet = 0;
    roll.moveCursorTo(59, 4);
    rig.key(RIGHT, true); // onto the note
    rig.frames(1);
    CHECK(roll.cursorPitch() == 60);
    rig.key(X, true);
    rig.frames(40);
    CHECK(p.notes[slot].ratchet == 0 && roll.cursorPitch() == 60);
    rig.key(RIGHT, false);
    rig.key(X, false);
    rig.frames(2);
    // On the drum grid as well.
    {
        rig.project().track = 0;
        rig.frames(2);
        PatternScreen& drums = rig.app.drums();
        drums.moveCursorTo(0, 4);
        rig.tap(A);
        drums.setFx(0, 4, StepFx());
        rig.key(STICK_UP, true);
        rig.frames(1);
        drums.moveCursorTo(0, 4);
        rig.key(X, true);
        rig.frames(40);
        CHECK(drums.effecting() && !drums.fx(0, 4).any());
        rig.key(STICK_UP, false);
        rig.frames(2);
        rig.key(STICK_UP, true);
        rig.frames(1);
        rig.key(STICK_UP, false);
        rig.frames(2);
        CHECK(drums.fx(0, 4).pitch == 1);
        rig.key(X, false);
        rig.frames(2);
        rig.project().track = 1;
        rig.frames(2);
    }

    // 2. A chord swapped for another, or turned over, keeps its ratchet and its nudge.
    p = NotePattern();
    p.chords[0] = makeChord(builtInPack().chords[0], 48, 8, 3.f, 0.7f, 0);
    p.chords[0].ratchet = 3;
    p.chords[0].nudge = 0.2f;
    session.sendNoteBar(1, 0, 0);
    roll.moveCursorTo(48, 8);
    const std::string nameWas = p.chords[0].name;
    rig.key(Y, true);
    rig.frames(2);
    rig.tap(RIGHT);
    rig.tap(UP);
    rig.key(Y, false);
    rig.frames(2);
    CHECK(nameWas != p.chords[0].name && p.chords[0].ratchet == 3 && p.chords[0].nudge == 0.2f);
    // 3. Broken into notes, each note plays as the chord did: the ratchet and the nudge go with them.
    int keys[kChordNotes];
    const int count = p.chords[0].pitches(keys);
    rig.key(Y, true);
    rig.frames(2);
    rig.tap(B);
    rig.key(Y, false);
    rig.frames(2);
    CHECK(p.chords[0].velocity <= 0.f && count >= 3);
    for (int i = 0; i < count; ++i) {
        const int at = p.at(keys[i], 8);
        CHECK(at >= 0 && p.notes[at].ratchet == 3 && p.notes[at].nudge == 0.2f && p.notes[at].glide == 0);
    }

    // 4. A note with an effect that lies under a longer note on the same key (the longer one in a
    //    lower slot) keeps its effect through the file, and so through undo.
    p = NotePattern();
    p.notes[0] = NoteEvent{4, 60, 6.f, 0.f, 0.8f}; // steps 5 to 10
    p.notes[1] = NoteEvent{6, 60, 1.f, 0.f, 0.8f}; // step 7, inside it
    p.notes[1].ratchet = 3;
    p.notes[1].glide = 1;
    const std::string text = songText(rig.project());
    CHECK(text.find("\nnotefx 7 60 3 1\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    const NotePattern& kept = back->tracks[1].notes[0][0];
    CHECK(kept.notes[1].step == 6 && kept.notes[1].ratchet == 3 && kept.notes[1].glide == 1);
    CHECK(kept.notes[0].ratchet == 0 && kept.notes[0].glide == 0); // and the long one did not take it
}

// Hold X on a note: a ratchet (left / right) and a glide (up / down).
void testNoteEffects() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Qmod);
    rig.project().track = 1;
    session.setSynthMacro(1, qmod::MacroRelease, 0.f); // a note let go is gone at once, so a gap between two notes can be heard
    session.setSynthMacro(1, qmod::MacroAttack, 0.f);
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    roll.moveCursorTo(60, 4);
    rig.tap(A);
    const int slot = p.at(60, 4);
    CHECK(slot >= 0 && p.notes[slot].ratchet == 0 && p.notes[slot].glide == 0 && !roll.effecting());
    // X held on it: right is a ratchet of 2, 3, 4 and no more; left back to one, kept as 0. The cursor stays.
    rig.key(X, true);
    CHECK(roll.effecting());
    rig.tap(RIGHT);
    CHECK(p.notes[slot].ratchet == 2 && roll.cursorPitch() == 60 && roll.cursorStep() == 4);
    rig.tap(RIGHT, 5);
    CHECK(p.notes[slot].ratchet == 4);
    rig.tap(LEFT, 9);
    CHECK(p.notes[slot].ratchet == 0);
    // Up: it glides. Down: it does not.
    rig.tap(UP);
    CHECK(p.notes[slot].glide == 1 && roll.cursorStep() == 4);
    rig.tap(UP);
    CHECK(p.notes[slot].glide == 1);
    rig.tap(DOWN);
    CHECK(p.notes[slot].glide == 0);
    rig.tap(UP);
    rig.tap(RIGHT, 2);
    rig.key(X, false);
    CHECK(!roll.effecting() && p.notes[slot].ratchet == 3 && p.notes[slot].glide == 1);
    // With X let go the D-pad is the cursor again.
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 61 && p.notes[slot].ratchet == 3);
    rig.tap(LEFT);
    // The stick clicks too: one at once, and one way per push.
    rig.key(X, true);
    rig.tap(DOWN); // the glide off, to see that the push below leaves it off
    CHECK(p.notes[slot].glide == 0);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    CHECK(p.notes[slot].ratchet == 2);
    rig.key(STICK_UP, true); // pushed up as well, and further: still the ratchet's push, not the glide's
    rig.frames(30);
    rig.key(STICK_UP, false);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(p.notes[slot].glide == 0 && p.notes[slot].ratchet <= 2);
    // Back to a glide and a ratchet of three, by the buttons (so the engine is told by them, not by the test).
    rig.tap(UP);
    rig.tap(LEFT, 3);
    rig.tap(RIGHT, 2);
    rig.key(X, false);
    CHECK(p.notes[slot].glide == 1 && p.notes[slot].ratchet == 3);
    // X on an empty step is only "hear": nothing is held, and the D-pad moves the cursor.
    roll.moveCursorTo(70, 9);
    rig.key(X, true);
    CHECK(!roll.effecting());
    rig.tap(DOWN);
    rig.key(X, false);
    CHECK(roll.cursorStep() == 10);
    // Nor while a note is being shaped (A held).
    roll.moveCursorTo(60, 4);
    rig.key(A, true);
    rig.key(X, true);
    CHECK(!roll.effecting());
    rig.key(X, false);
    rig.key(A, false);

    // The engine plays it: three starts inside step 5 (each let go before the next), where a plain note is one long sound.
    rig.frames(30);
    rig.render(96000);
    rig.tap(START);
    const std::vector<float> out = rig.render(8 * kStep);
    rig.tap(START);
    rig.render(96000);
    const float sounding = Rig::peak(out, 4 * kStep + 300, 4 * kStep + 1500);
    CHECK(Rig::peak(out, 0, 4 * kStep - 10) < 0.002f && sounding > 0.005f);
    CHECK(Rig::peak(out, 4 * kStep + 2300, 4 * kStep + 3500) > 0.5f * sounding && Rig::peak(out, 4 * kStep + 4300, 4 * kStep + 5500) > 0.5f * sounding);
    // ...with a gap before the second and the third (each is let go before the next begins). A plain note has none.
    CHECK(Rig::peak(out, 4 * kStep + 1900, 4 * kStep + 1990) < 0.2f * sounding && Rig::peak(out, 4 * kStep + 3900, 4 * kStep + 3990) < 0.2f * sounding);

    // A chord takes a ratchet, and has no glide.
    p.chords[0] = makeChord(builtInPack().chords[0], 48, 10, 3.f, 0.7f, 0);
    session.sendNoteBar(1, 0, 0);
    int keys[kChordNotes];
    p.chords[0].pitches(keys);
    roll.moveCursorTo(keys[0], 10);
    rig.key(X, true);
    CHECK(roll.effecting());
    rig.tap(RIGHT);
    rig.tap(UP);
    rig.key(X, false);
    CHECK(p.chords[0].ratchet == 2);
    NoteEvent made[kMaxNotes];
    p.render(made);
    int ratcheted = 0;
    for (const NoteEvent& n : made) ratcheted += n.velocity > 0.f && n.step == 10 && n.ratchet == 2 && n.glide == 0;
    CHECK(ratcheted >= 3); // every note of the chord carries it

    // The song keeps them, on lines of their own; a plain note has no such line.
    roll.moveCursorTo(72, 0);
    rig.tap(A);
    p.notes[slot].length = 3.f; // held over steps 6 and 7, for the file check below
    std::string text = songText(rig.project());
    CHECK(text.find("\nnotefx 5 60 3 1\n") != std::string::npos && text.find("\nchordfx 11 2\n") != std::string::npos && text.find("notefx 1 72") == std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    const NotePattern& kept = back->tracks[1].notes[0][0];
    CHECK(kept.at(60, 4) >= 0 && kept.notes[kept.at(60, 4)].ratchet == 3 && kept.notes[kept.at(60, 4)].glide == 1 && kept.chords[0].ratchet == 2);
    CHECK(kept.notes[kept.at(72, 0)].ratchet == 0 && kept.notes[kept.at(72, 0)].glide == 0);
    // Nonsense in a file is held to what there is; a line for a note that is not there is skipped.
    const std::size_t at = text.find("notefx 5 60 3 1");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, 15, "notefx 5 60 99 7\nnotefx 9 60 3 1\nnotefx 6 60 2 0\nnotefx 5 200 3 1\nnotefx 99 60 3 1");
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(text, *odd));
    const NotePattern& o = odd->tracks[1].notes[0][0];
    CHECK(o.notes[o.at(60, 4)].ratchet == 4 && o.notes[o.at(60, 4)].glide == 1);
    int withEffects = 0;
    for (const NoteEvent& n : o.notes) withEffects += n.velocity > 0.f && (n.ratchet || n.glide);
    CHECK(withEffects == 1); // only the one note: "step 6" is inside it but is not where it starts

    // Undo takes an effect back, as a step of its own.
    rig.frames(60);
    const int before = session.undoSteps();
    roll.moveCursorTo(60, 4);
    rig.key(X, true);
    rig.tap(DOWN);
    rig.key(X, false);
    CHECK(p.notes[slot].glide == 0);
    rig.frames(60);
    CHECK(session.undoSteps() == before + 1 || before == 20);
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    CHECK(p.at(60, 4) >= 0 && p.notes[p.at(60, 4)].glide == 1);

    // X held, Y: the dice. A note gets a ratchet, a glide or both: always something, and something else.
    {
        const int at = p.at(60, 4);
        rig.key(X, true);
        bool ratchets = false, glides = false, both = false, plainRatchet = false;
        for (int i = 0; i < 40; ++i) {
            const int ratchetWas = p.notes[at].ratchet, glideWas = p.notes[at].glide;
            rig.tap(Y);
            const NoteEvent& n = p.notes[at];
            CHECK(n.ratchet >= 2 || n.glide == 1);
            CHECK(n.ratchet != ratchetWas || n.glide != glideWas);
            CHECK((n.ratchet == 0 || (n.ratchet >= 2 && n.ratchet <= 4)) && n.glide <= 1);
            ratchets = ratchets || n.ratchet >= 2;
            glides = glides || n.glide;
            both = both || (n.ratchet >= 2 && n.glide);
            plainRatchet = plainRatchet || (n.ratchet >= 2 && !n.glide);
        }
        CHECK(ratchets && glides && both && plainRatchet && roll.effecting() && !roll.picking());
        // The engine is told: what plays is what the roll left. Rolled to a plain ratchet of two,
        // there is a gap just before the half of the step, where the ratchet of three it had before sounds on.
        for (int i = 0; i < 400 && !(p.notes[at].ratchet == 2 && p.notes[at].glide == 0); ++i) rig.tap(Y);
        CHECK(p.notes[at].ratchet == 2 && p.notes[at].glide == 0);
        rig.key(X, false);
        rig.tap(START);
        const std::vector<float> out = rig.render(8 * kStep);
        rig.tap(START);
        rig.render(96000);
        const float sounding = Rig::peak(out, 4 * kStep + 300, 4 * kStep + 1500);
        CHECK(sounding > 0.005f && Rig::peak(out, 4 * kStep + 2800, 4 * kStep + 2990) < 0.2f * sounding && Rig::peak(out, 4 * kStep + 3300, 4 * kStep + 4500) > 0.5f * sounding);
        const int hits = p.notes[at].ratchet >= 2 ? p.notes[at].ratchet : 1;
        std::string text = songText(rig.project());
        char line[40];
        std::snprintf(line, sizeof line, "\nnotefx 5 60 %d %d\n", hits, p.notes[at].glide);
        CHECK(text.find(line) != std::string::npos);
        // A chord: a ratchet, never a glide, and never nothing.
        roll.moveCursorTo(48, 10);
        rig.key(X, true);
        CHECK(roll.effecting());
        for (int i = 0; i < 12; ++i) {
            const int was = p.chords[0].ratchet;
            rig.tap(Y);
            CHECK(p.chords[0].ratchet >= 2 && p.chords[0].ratchet <= 4 && p.chords[0].ratchet != was);
        }
        rig.key(X, false);
        // A roll is undone like any other change.
        rig.frames(60);
        const int kept = p.chords[0].ratchet;
        rig.key(X, true);
        rig.tap(Y);
        rig.key(X, false);
        CHECK(p.chords[0].ratchet != kept);
        rig.frames(60);
        rig.key(SELECT, true);
        rig.tap(B);
        rig.key(SELECT, false);
        CHECK(p.chords[0].ratchet == kept);
    }
}

int main(int, char**) {
    ui::gStickSpeed = 1.f; // the checks below are of each screen's own speeds; the overall scale has its own check
    testTabs();
    testNoteEffects();
    testNoteEffectsHoldUp();
    testPresetsEightAtATime();
    testALongerCellIsFilledWithItself();
    testASceneOfAnyLength();
    testANoteUnderAChordIsKnown();
    testChangingATracksInstrument();
    testNamingATrackFromTheSongScreen();
    testStepEffects();
    testAFullQueueIsRecoveredFrom();
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
    testCellsOfSeveralBars();
    testNudgingWithTheOuterShoulderButtons();
    testAFreeLengthReachesTheEngine();
    testAllFourShoulderButtonsIsPanic();
    testMuteSoloAndNames();
    testHoldBAndDragErases();
    testNothingStaysHeld();
    testDeletingATrack();
    testStepsInAScene();
    testTheBattery();
    testBouncing();
    testRecordingAutomation();
    testChoicesClick();
    testTheHelpCard();
    testUndoAndRedo();
    testHoldBErasesDrumHits();
    testQmodOnATrack();
    testTypingATrackName();
    testHoldXCarriesANote();
    testSelectGoesToTheSongScreen();
    testTheSetupScreen();
    testTheButtonTestAndSettingUpAgain();
    testThePianoRollZooms();
    testTheStickSpeedAndTheVolume();
    testTheTempoFromAnyScreen();
    testTheEffectsScreen();
    testEffectsThroughTheSession();
    testSendsThroughTheSession();
    testTheFxScreensTwoPages();
    testTheKeysShownFollowTheCursor();
    testTheScreensKnowWhereTheSongIs();
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
