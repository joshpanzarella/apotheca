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
#include <functional>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "presets.hpp"
#include "app.hpp"
#include "scales.hpp"
#include "battery.hpp"
#include "bench_run.hpp"
#include "canvas.hpp"
#include "controls.hpp"
#include "instruments.hpp"
#include "mod_knob.hpp"
#include "mods.hpp"
#include "power_off.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "synth_spec.hpp"
#include "icons.hpp"
#include "temp_folder.hpp"

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
    // The stick pushed part of the way (-1..1 as the handheld sends it, before the dead zone), as a gamepad does.
    void stick(float x, float y) {
        for (int axis = 0; axis < 2; ++axis) {
            SDL_Event event = {};
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.axis = static_cast<Uint8>(axis == 0 ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_LEFTY);
            event.caxis.value = static_cast<Sint16>((axis == 0 ? x : y) * 32767.f);
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
    }
};

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;
const SDL_Keycode STICK_UP = SDLK_i, STICK_DOWN = SDLK_k, STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// B held long enough to delete for good (an effect: a second; the rig's frames are 16 ms).
void holdB(Rig& rig) {
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    rig.frames(1);
}

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
    CHECK(rig.view() == View::Settings); // the row of tabs goes round (G35): SONG and SET are one press apart
    rig.tap(R2);
    CHECK(rig.view() == View::Song); // ...both ways
    rig.tap(R2, 2);
    CHECK(rig.view() == View::Device);
    rig.tap(R2);
    CHECK(rig.view() == View::Effects);
    rig.tap(R2);
    CHECK(rig.view() == View::Mods); // the MOD tab, after FX
    rig.tap(R2);
    CHECK(rig.view() == View::Settings);
    rig.tap(R2);
    CHECK(rig.view() == View::Song);
    rig.tap(L2);
    CHECK(rig.view() == View::Settings);
    // Not while shaping something: a slip of the finger must not change screens.
    rig.tap(L2, 4);
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
    CHECK(!rig.project().playing && rig.view() == View::Effects);    rig.tap(R2); // and on the MOD tab
    rig.tap(START);
    CHECK(rig.project().playing && rig.view() == View::Mods);
    rig.tap(START);
    CHECK(!rig.project().playing);
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
    // Each scene has its own. (Scene 2 given a hit: the row just past the last scene is the song's end, AT END, not a count.)
    rig.project().tracks[0].drums[1][0].level[0][0] = 0.9f;
    rig.tap(DOWN);
    rig.tap(A);
    CHECK(rig.project().repeats[1] == 2 && rig.project().repeats[0] == 1);
    // A change of REPEATS is a step of undo of its own, and undo puts the count back.
    rig.frames(60);
    Session& session = rig.app.session();
    const int steps = session.undoSteps();
    rig.tap(A);
    CHECK(rig.project().repeats[1] == 4);
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 1);
    CHECK(session.undo() && rig.project().repeats[1] == 2);
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

void testCopyingABar() {
    Rig rig;
    rig.app.drums().setStep(0, 0, 0.9f);
    rig.app.drums().setStep(2, 5, 0.4f);
    rig.app.drums().setNudge(2, 5, 0.25f);
    rig.app.drums().setChance(2, 5, 0.5f);
    addPhazed(rig);
    // The synth has a note in scene 3, which a paste will be tried on.
    rig.app.song().moveCursorTo(2, 1);
    rig.tap(A);
    rig.tap(A);
    rig.tap(L2);
    CHECK(!rig.project().tracks[1].notes[2][0].empty());
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(R1); // copy
    rig.app.song().moveCursorTo(1, 0);
    rig.tap(R1); // paste: the copy is spent (the owner, 2026-11-25)
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(R1); // copy again
    rig.app.song().moveCursorTo(2, 0);
    rig.tap(R1); // paste
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(R1); // copy the drum cell again
    rig.app.song().moveCursorTo(2, 1);
    rig.tap(R1); // onto the synth: a drum bar cannot go there
    CHECK(!rig.project().tracks[1].notes[2][0].empty()); // and it did not wipe the synth's bar
    for (int scene = 1; scene <= 2; ++scene) {
        const DrumPattern& p = rig.project().tracks[0].drums[scene][0];
        CHECK(near(p.level[0][0], 0.9f) && near(p.level[2][5], 0.4f));
        CHECK(near(p.nudge[2][5], 0.25f) && near(p.chance[2][5], 0.5f));
    }
    CHECK(rig.project().tracks[0].drums[3][0].empty());
    // Moving without R1 pastes nothing.
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
    synth.app.song().moveCursorTo(0, 1);
    synth.tap(R1); // copy
    synth.app.song().moveCursorTo(1, 1);
    synth.tap(R1); // paste
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

void testTheCellCopyStaysPut() {
    Rig rig;
    rig.app.drums().setStep(0, 0, 0.9f);
    rig.tap(L2);
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(R1); // copy
    rig.app.song().moveCursorTo(1, 0);
    rig.tap(R1); // paste
    rig.tap(DOWN, 3); // moving on pastes nothing more
    CHECK(!rig.project().tracks[0].drums[1][0].empty());
    for (int scene = 2; scene <= 4; ++scene) CHECK(rig.project().tracks[0].drums[scene][0].empty());
    // R1 on a repeat count or on + TRACK leaves the cell copy alone.
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(R1); // copy again: the paste before spent it
    rig.app.song().moveCursorTo(0, -1);
    rig.tap(R1); // that copies the scene, another clipboard
    rig.tap(RIGHT, 2); // onto + TRACK
    rig.tap(R1); // + TRACK: nothing at all
    rig.app.song().moveCursorTo(2, 0);
    rig.tap(R1); // the cell copy is still there
    CHECK(near(rig.project().tracks[0].drums[2][0].level[0][0], 0.9f));
}

void testTheSongScreenScrolls() {
    Rig rig;
    rig.tap(L2);
    rig.tap(DOWN, kMaxScenes + 4);
    CHECK(rig.app.song().cursorScene() == kMaxScenes - 1 && rig.app.song().topScene() == kMaxScenes - 8);
    rig.tap(UP, kMaxScenes + 4);
    CHECK(rig.app.song().cursorScene() == 0);
    rig.tap(DOWN); // (the last of those presses went up onto the track's head: back down off it)
    CHECK(rig.app.song().cursorScene() == 0 && !rig.app.song().cursorOnHead());
    rig.key(STICK_DOWN, true);
    rig.frames(200); // (24 rows a second at full: 64 rows in under three)
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
    CHECK(notes.noteHere() && notes.noteHere()->length == 1 && near(notes.noteHere()->velocity, 0.9f));
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

// A hit placed starts at 90% (kPlacedLevel; owner, 2026-11-15: it was 80%), and the KIT page's X
// hears the drum at that level too: what is heard is what is placed.
void testHeardAsPlaced() {
    Rig rig;
    Session& session = rig.app.session();
    PatternScreen& drums = rig.app.drums();
    rig.project().track = 0;
    session.view = View::Pattern;
    drums.moveCursorTo(0, 0);
    rig.listen(48000);
    rig.tap(A); // a kick, heard as it lands
    CHECK(drums.level(0, 0) == kPlacedLevel && kPlacedLevel == 0.9f);
    const float placed = rig.listen(12000);
    rig.listen(96000);
    session.view = View::Device;
    rig.app.kit().goTo(0, KitScreen::kSound);
    rig.frames(2);
    rig.tap(X); // a tap: a moment of the same drum
    rig.frames(1);
    const float heard = rig.listen(12000);
    if (!(placed > 0.05f && std::fabs(heard - placed) < 1e-4f)) std::printf("  placed %.5f, heard %.5f\n", placed, heard);
    CHECK(placed > 0.05f && std::fabs(heard - placed) < 1e-4f);
    rig.listen(96000);
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
    CHECK(near(notes.noteHere()->velocity, 0.6f));
    rig->tap(RIGHT);
    CHECK(near(notes.noteHere()->velocity, 0.7f));
    // The stick is free: the end goes to any point in a step, at twice the held D-pad's pace (66.7 steps a second
    // at 100%; at STICK SPEED 50%, as here, a frame is 8/15 of a step), however far it is pushed.
    ui::gStickSpeed = 0.5f;
    rig->stick(0.f, 0.25f); // just past the dead zone
    rig->frames(1);
    rig->stick(0.f, 0.f);
    rig->frames(1);
    const float free = notes.noteHere()->length;
    CHECK(near(free, 1.f + 8.f / 15.f) && free != std::floor(free)); // (it was ten steps a second fully pushed, by the square)
    CHECK(notes.cursorStep() == 4);
    // The D-pad puts it back on a step line: the next one, or the one before.
    rig->tap(DOWN);
    CHECK(notes.noteHere()->length == 2.f);
    rig->key(STICK_DOWN, true);
    rig->frames(1);
    rig->key(STICK_DOWN, false);
    rig->frames(1);
    CHECK(near(notes.noteHere()->length, 2.f + 8.f / 15.f));
    rig->tap(UP);
    CHECK(notes.noteHere()->length == 2.f);
    rig->tap(UP);
    CHECK(notes.noteHere()->length == 1.f);
    // Shorter than a step, with the stick only; never to nothing.
    rig->key(STICK_UP, true);
    rig->frames(1);
    rig->key(STICK_UP, false);
    rig->frames(1);
    ui::gStickSpeed = 1.f;
    const float shortNote = notes.noteHere()->length;
    CHECK(near(shortNote, 1.f - 8.f / 15.f));
    rig->tap(UP); // there is no whole step below it: nothing happens
    CHECK(notes.noteHere()->length == shortNote);
    rig->key(STICK_UP, true);
    rig->frames(60);
    rig->key(STICK_UP, false);
    rig->frames(1);
    CHECK(near(notes.noteHere()->length, 0.1f));
    rig->tap(DOWN);
    CHECK(notes.noteHere()->length == 1.f);
    // Pushed down: to the end of the bar.
    rig->key(STICK_DOWN, true);
    rig->frames(100);
    rig->key(STICK_DOWN, false);
    CHECK(notes.noteHere()->length == 12); // to the end of the bar and no further
    CHECK(near(notes.noteHere()->velocity, 0.7f));
    // Drifting up during a sideways push does not also change the level.
    rig->key(STICK_DOWN, true);
    rig->frames(2);
    rig->key(STICK_RIGHT, true);
    rig->frames(10);
    rig->key(STICK_RIGHT, false);
    rig->key(STICK_DOWN, false);
    CHECK(near(notes.noteHere()->velocity, 0.7f));
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
        CHECK(p.at(72, 0) < 0 && p.at(72, 1) < 0 && p.at(72, 2) >= 0 && near(p.notes[p.at(72, 2)].velocity, 0.4f));
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
    CHECK(notes.cursorOnBars()); // up from the top step: BARS
    rig->tap(UP);
    CHECK(!notes.cursorOnBars() && notes.cursorStep() == kSteps - 1); // up again: round to the bottom
    rig->tap(DOWN);
    CHECK(!notes.cursorOnBars() && notes.cursorStep() == 0);
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
    CHECK(track.drums[0][1].level[0][0] == 0.9f && track.drums[0][0].level[0][0] == 0.f);
    CHECK(drums.level(0, 0) == 0.9f); // the grid shows the bar it is on
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
    rig.tap(UP, 15);
    CHECK(rig.project().bar == 0 && notes.cursorStep() == 0);
    rig.tap(UP);
    CHECK(notes.cursorOnBars()); // up past the top step: BARS
    rig.tap(LEFT);
    CHECK(notes.cursorOnBars() && rig.project().bar == 3); // round to the last bar
    rig.tap(RIGHT);
    CHECK(notes.cursorOnBars() && rig.project().bar == 0);
    rig.tap(DOWN);
    CHECK(!notes.cursorOnBars() && rig.project().bar == 0 && notes.cursorStep() == 0);
    rig.tap(UP);
    CHECK(notes.cursorOnBars());
    rig.tap(UP);
    CHECK(!notes.cursorOnBars() && rig.project().bar == 3 && notes.cursorStep() == 15); // up again: round to the bottom
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
    rig.tap(R1); // copy, as a scene is copied
    song.moveCursorTo(1, 0);
    rig.key(R1, true);
    CHECK(track.bars[1] == 1); // not yet: the paste is on its let go
    rig.key(R1, false);
    CHECK(track.bars[1] == 2 && track.drums[1][1].level[0][0] == 0.9f && near(track.drums[1][1].nudge[0][0], 0.1f));
    rig.tap(B); // empty the copy
    CHECK(track.bars[1] == 1 && track.drums[1][1].empty() && track.sceneEmpty(1));
    CHECK(track.bars[0] == 2 && track.drums[0][1].level[0][0] == 0.9f);
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
    CHECK(text.find("hit 1 1 0.9 0.1 1 0\n") != std::string::npos);
    CHECK(text.find("scene 1\nbars 4\nnote 9 64 56 0.9\n") != std::string::npos);
    CHECK(text.find("\nbar 2\nnote 1 60 1 0.9\n") != std::string::npos && text.find("\nbar 3\nnote 13 67 20 0.9\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].bars[0] == 2 && back->tracks[1].bars[0] == 4 && back->tracks[0].drums[0][1].level[0][0] == 0.9f);
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
    // Lines that make no sense are left out: a bar there is not, a length that is not one. A bar past
    // the cell's end is one it hides (made shorter), and is kept, to come back if it is made longer.
    CHECK(parseSong("apotheca-song 1\ntrack drums\nscene 1\nbars 3\nbar 2\nhit 1 1 1\nbar 3\nhit 2 2 1\nbar 9\nhit 3 3 1\nbar 0\nhit 4 4 1\nscene 2\nhit 5 5 1\n", *back));
    CHECK(back->tracks[0].bars[0] == 2 && back->tracks[0].drums[0][1].level[0][0] == 1.f);
    CHECK(back->tracks[0].drums[0][2].level[1][1] == 1.f && back->tracks[0].drums[0][0].empty()); // bar 3 hidden; bars 9 and 0 are not there
    for (int bar = 3; bar < kMaxBars; ++bar) CHECK(back->tracks[0].drums[0][bar].empty());
    CHECK(back->tracks[0].drums[1][0].level[4][4] == 1.f && back->tracks[0].bars[1] == 1); // a new scene starts at its first bar
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nscene 1\nnote 9 60 40 1\nscene 2\nbars 2\nnote 9 60 40 1\n", *back));
    CHECK(back->tracks[0].notes[0][0].notes[0].length == 8.f && back->tracks[0].notes[1][0].notes[0].length == 24.f);
}

// The strip counts steps as the grid numbers do (bar * meter + step), and on BARS it shows the bar.
void testStripCountsWithTheGrid() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    Rig rig;
    Session& session = rig.app.session();
    addPhazed(rig);
    const Fonts& fonts = *canvas.fonts;
    const auto stripHas = [&](const char* text, SDL_Color color, float x0, float x1, float y0, float y1) {
        for (float y = y0; y <= y1; y += 1.f)
            for (float x = x0; x <= x1; x += 1.f)
                if (inked(canvas, scratch, 2, x, y, text, color)) return true; // values are large lettering
        return false;
    };
    session.setSceneMeter(0, 12);
    session.setCellBars(1, 0, 2);
    session.setCellBars(0, 0, 2);
    // The roll, bar 1 step 0: STEP reads 13, as its number does (not 17).
    rig.project().track = 1;
    session.view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    notes.moveCursorTo(60, 0);
    rig.project().bar = 1;
    rig.frames(1);
    canvas.clear();
    rig.app.draw(canvas.r, fonts);
    CHECK(stripHas("13", ui::kText, 99.f, 135.f, 400.f, 445.f) && !stripHas("17", ui::kText, 99.f, 135.f, 400.f, 445.f));
    // BARS: CELL/BARS and BAR n OF n.
    notes.moveCursorTo(60, 0);
    rig.project().bar = 0;
    rig.tap(UP);
    canvas.clear();
    rig.app.draw(canvas.r, fonts);
    CHECK(notes.cursorOnBars() && stripHas("BARS", ui::kText, 7.f, 60.f, 400.f, 445.f) && stripHas("1 OF 2", ui::kTeal, 99.f, 160.f, 400.f, 445.f));
    // The drums, bar 1 step 0: STEP reads 13 too.
    rig.project().track = 0;
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(0, 0);
    rig.project().bar = 1;
    rig.frames(1);
    canvas.clear();
    rig.app.draw(canvas.r, fonts);
    CHECK(stripHas("13", ui::kText, 119.f, 155.f, 380.f, 452.f) && !stripHas("17", ui::kText, 119.f, 155.f, 380.f, 452.f));
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
    CHECK(text.find("note 5 60 1 0.9\n") != std::string::npos);
    CHECK(text.find(" nudge -0.2\n") != std::string::npos);
    notes.moveCursorTo(60, 4);
    rig->key(A, true);
    rig->tap(R2, 2);
    rig->key(A, false);
    const std::string nudgedText = songText(rig->project());
    CHECK(nudgedText.find("note 5 60 1 0.9 0.2\n") != std::string::npos);
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
    CHECK(drums.level(2, 6) == 0.9f && drums.cursorStep() == 6);
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
    // With the cursor inside a long note, the stick pulls its end back past the cursor, which comes
    // back with it (owner, 2026-11-17: shorten from anywhere, not only from the start).
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
    CHECK(notes.noteHere() && near(notes.noteHere()->length, 0.1f) && notes.cursorStep() == 4); // never to nothing
}

// L3 (the stick pressed in), from any screen: SONG with MASTER under the cursor; hold A + up turns
// the output up; L3 again goes back where it came from (owner, 2026-11-17).
void testL3IsTheMaster() {
    std::unique_ptr<Rig> rig(onNotes());
    const SDL_Keycode L3 = SDLK_3;
    App& app = rig->app;
    app.session().view = View::Effects;
    rig->frames(1);
    rig->tap(L3);
    rig->frames(1);
    CHECK(app.session().view == View::Song && app.song().cursorOnMaster());
    const int db = app.project().masterDb;
    rig->key(A, true);
    rig->frames(25); // (the card comes up)
    rig->tap(SDLK_UP);
    rig->key(A, false);
    rig->frames(1);
    CHECK(app.project().masterDb == db + 1);
    CHECK(app.session().view == View::Song && app.song().cursorOnMaster());
    rig->tap(L3); // still on MASTER: back to FX
    rig->frames(1);
    CHECK(app.session().view == View::Effects);
    // Moved off MASTER, L3 brings it back there rather than leaving.
    rig->tap(L3);
    rig->frames(1);
    rig->tap(SDLK_LEFT);
    rig->frames(1);
    CHECK(app.session().view == View::Song && !app.song().cursorOnMaster());
    rig->tap(L3);
    rig->frames(1);
    CHECK(app.session().view == View::Song && app.song().cursorOnMaster());
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
    // X tapped on a track: soloed, at once (owner, 2026-11-15; it was L1 held). Only it is heard, and a
    // muted track that is soloed is heard too. L1 does nothing on a track now, tapped or held.
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(L1);
    CHECK(!rig.project().tracks[1].solo && session.heard(0));
    rig.key(L1, true);
    rig.frames(60);
    rig.key(L1, false);
    rig.frames(2);
    CHECK(!rig.project().tracks[0].solo && !rig.project().tracks[1].solo && session.heard(0) && session.heard(1));
    rig.tap(X);
    CHECK(rig.project().tracks[1].solo && session.heard(1) && !session.heard(0)); // at the press
    rig.render(2000);
    levels(d, p);
    CHECK(d == 0.f && p > 0.01f);
    rig.tap(Y); // muted as well: solo still wins while it is on
    CHECK(session.heard(1));
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(X); // both soloed: both heard
    CHECK(session.heard(0) && session.heard(1));
    rig.tap(X);
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(X); // no solo left: the mute shows again
    CHECK(session.heard(0) && !session.heard(1) && !rig.project().tracks[1].solo);
    rig.render(2000);
    levels(d, p);
    CHECK(d > 0.05f && p == 0.f);
    rig.tap(Y);
    // On a track's head X solos too.
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(UP);
    CHECK(rig.app.song().cursorOnHead());
    rig.tap(X);
    CHECK(rig.project().tracks[0].solo && !session.heard(1));
    rig.tap(X);
    CHECK(!rig.project().tracks[0].solo && session.heard(1));
    rig.tap(DOWN);
    // Not on the repeat counts or + TRACK: nothing to mute or solo there, and nothing breaks.
    rig.app.song().moveCursorTo(0, -1);
    rig.tap(Y);
    rig.tap(L1);
    rig.tap(X);
    rig.app.song().moveCursorTo(0, 2);
    rig.tap(Y);
    rig.tap(L1);
    rig.tap(X);
    CHECK(session.heard(0) && session.heard(1) && !rig.project().tracks[0].solo && !rig.project().tracks[1].solo && !rig.app.song().listingSongs());
    rig.tap(START);
    rig.render(96000);
    rig.frames(1);
    // X no longer hears the track on SONG (its DEVICE page does): stopped, a tap of X there is silent.
    CHECK(rig.listen(2000) == 0.f);
    rig.app.song().moveCursorTo(0, 0);
    rig.tap(X);
    CHECK(rig.listen(4000) == 0.f && rig.project().tracks[0].solo);
    rig.tap(X);
    rig.render(96000);
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(X);
    CHECK(rig.listen(4000) == 0.f);
    rig.tap(X);
    CHECK(!rig.project().tracks[1].solo);

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
    // Four tracks, each with something of its own: drums, Phazed (named), JAX-EP, FM-6.
    rig.app.drums().setStep(0, 0, 1.f);
    session.addTrack(TrackKind::Phazed);
    session.addTrack(TrackKind::Jx3p);
    session.addTrack(TrackKind::Fm6);
    session.setTrackName(1, "BASS");
    rig.project().tracks[2].notes[0][0].notes[0] = NoteEvent{4, 62, 2.f, 0.f, 0.8f};
    rig.project().tracks[3].notes[1][0].notes[0] = NoteEvent{0, 50, 1.f, 0.f, 0.7f};
    session.setSynthSetting(3, fm6::Algorithm, 21);
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
    CHECK(rig.project().tracks[0].kind == TrackKind::Drums && rig.project().tracks[1].kind == TrackKind::Jx3p && rig.project().tracks[2].kind == TrackKind::Fm6 && rig.project().tracks[3].kind == TrackKind::None);
    CHECK(rig.project().tracks[1].name[0] == 0 && rig.project().tracks[1].notes[0][0].at(62, 4) >= 0 && near(rig.project().tracks[1].automation[0][3][5], 0.25f));
    CHECK(rig.project().tracks[2].notes[1][0].at(50, 0) >= 0 && rig.project().tracks[2].fm6.value[fm6::Algorithm] == 21 && rig.project().tracks[2].muted);
    CHECK(rig.project().tracks[0].drums[0][0].level[0][0] == 1.f && rig.project().track == 2 && !rig.project().playing);
    for (float v : rig.project().tracks[3].automation[0][3]) CHECK(v < 0.f); // the freed place is clean
    // The engine has them in their new places: the JAX-EP note sounds from track 2, and the kick still plays.
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

// Hold B on a track deletes THAT track, the one where B went down (gesture audit G02: it deleted whichever
// the cursor was on when the bar filled, and the cursor still moved). A move (the D-pad, the stick) or any
// other press calls the delete off, as on FX. A tap of B empties the cell on its LET GO, so a delete called
// off or let go of once its bar shows changes nothing at all. Before the bar shows, B was a tap however
// it ends (its let go, a move, another press; not A), so a quick B overlapping the next press still empties.
void testTheTrackHoldStaysOnItsTrack() {
    Rig rig;
    Session& session = rig.app.session();
    SongScreen& song = rig.app.song();
    rig.app.drums().setStep(0, 0, 1.f);
    session.addTrack(TrackKind::Phazed);
    session.addTrack(TrackKind::Jx3p);
    for (int t = 1; t <= 2; ++t) rig.project().tracks[t].notes[0][0].notes[0] = NoteEvent{0, 60, 1.f, 0.f, 0.8f};
    const auto full = [&](int t) { return t == 0 ? !rig.project().tracks[0].drums[0][0].empty() : rig.project().tracks[t].notes[0][0].notes[0].velocity > 0.f; };
    const auto allThere = [&] { return rig.project().trackCount() == 3 && full(0) && full(1) && full(2); };
    session.view = View::Song;
    rig.frames(2);
    CHECK(allThere());

    // Held on track 1, RIGHT pressed with the bar nearly full: called off, and track 2 is not deleted in its
    // place however long B stays down.
    song.moveCursorTo(0, 0);
    rig.key(B, true);
    rig.frames(112);
    CHECK(song.trackDeletePart() > 0.8f && allThere());
    rig.tap(RIGHT);
    CHECK(song.trackDeletePart() == 0.f && song.cursorColumn() == 1);
    rig.frames(160);
    CHECK(allThere() && song.trackDeletePart() == 0.f);
    rig.key(B, false);
    rig.frames(1);
    CHECK(allThere()); // ...and letting go empties nothing
    // The stick, the same: it moves the cursor with no press.
    rig.key(B, true);
    rig.frames(60);
    CHECK(song.trackDeletePart() > 0.f);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(160);
    CHECK(allThere() && song.cursorColumn() == 2 && song.trackDeletePart() == 0.f);
    rig.key(B, false);
    rig.frames(1);
    CHECK(allThere());
    // ...down the track's own column too: it is the place B went down on, not only the track.
    song.moveCursorTo(0, 2);
    rig.key(B, true);
    rig.frames(60);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(160);
    CHECK(allThere() && song.cursorColumn() == 2 && song.cursorScene() == 1);
    rig.key(B, false);
    rig.frames(1);
    // ...and along the heads.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(B, true);
    rig.frames(60);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(160);
    CHECK(allThere() && song.cursorOnHead() && song.cursorColumn() == 0);
    rig.key(B, false);
    rig.frames(1);
    // A tapped on a head while B is held: called off too, and that A is no tap (no list of instruments).
    rig.key(B, true);
    rig.frames(40);
    rig.tap(A);
    rig.frames(160);
    rig.key(B, false);
    rig.frames(1);
    CHECK(allThere() && !song.choosing());
    // Any other press: L1 (which does nothing on a track), START.
    for (SDL_Keycode other : {L1, START}) {
        song.moveCursorTo(0, 2);
        rig.key(B, true);
        rig.frames(60);
        rig.tap(other);
        CHECK(song.trackDeletePart() == 0.f);
        rig.frames(160);
        rig.key(B, false);
        rig.frames(1);
        CHECK(allThere());
    }
    CHECK(rig.project().playing);
    rig.tap(START);
    // Moved off and let go before the screen's next frame: B was a tap, so the cell left is emptied, and only
    // that one (review of G02: a quick B overlapping the next move lost its empty).
    song.moveCursorTo(0, 0);
    rig.key(B, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.key(B, false);
    CHECK(song.cursorColumn() == 1 && !full(0) && full(1) && full(2) && rig.project().trackCount() == 3);
    rig.frames(1);
    CHECK(session.undo() && allThere()); // (one step of undo, as a tap's)
    // ...and off the first track onto its scene's REPEATS box (column -1), whose own B is read on the let go too.
    song.moveCursorTo(0, 0);
    rig.key(B, true);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.key(B, false);
    CHECK(song.cursorColumn() == -1 && !full(0) && full(1) && rig.project().repeats[0] == 1);
    rig.frames(1);
    CHECK(session.undo() && allThere());
    // B, DOWN, B, DOWN down a column with two thumbs: the D-pad goes down before B comes up. The cell B went
    // down on is emptied on that press, the cell come to is kept.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(5);
    rig.key(RIGHT, true);
    CHECK(song.cursorColumn() == 2 && !full(1) && full(0) && full(2));
    rig.key(B, false);
    rig.key(RIGHT, false);
    rig.frames(1);
    CHECK(!full(1) && full(0) && full(2) && rig.project().trackCount() == 3);
    CHECK(session.undo() && allThere());
    // ...the stick, the same, B kept down: the move is seen a frame later, and the cell is emptied then.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(5);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(2);
    CHECK(song.cursorColumn() == 2 && !full(1) && full(0) && full(2) && song.trackDeletePart() == 0.f);
    rig.frames(160);
    rig.key(B, false);
    rig.frames(1);
    CHECK(!full(1) && full(2) && rig.project().trackCount() == 3);
    CHECK(session.undo() && allThere());
    // ...and another press (L1 does nothing on a cell).
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(5);
    rig.tap(L1);
    CHECK(!full(1) && full(0) && full(2));
    rig.frames(160);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 3);
    CHECK(session.undo() && allThere());
    // ...and a tab turned (a press the screen never sees) and turned back.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(5);
    rig.tap(R2);
    rig.frames(2);
    rig.key(B, false);
    rig.tap(L2);
    rig.frames(2);
    CHECK(rig.view() == View::Song && !full(1) && full(0) && full(2) && rig.project().trackCount() == 3);
    CHECK(session.undo() && allThere());
    // On a head a tap of B empties nothing: B down on track 2's head, the stick DOWN off it (no press), B let
    // go before half a second. The cell under the head is kept.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    CHECK(song.cursorOnHead());
    rig.key(B, true);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    CHECK(!song.cursorOnHead() && song.cursorColumn() == 1 && song.cursorScene() == 0);
    rig.key(B, false); // (before the screen's next frame: the let go itself must know it went down on a head)
    rig.frames(1);
    CHECK(allThere());
    // ...and kept held, the stick off a head calls the delete off: the track stays.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(B, true);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(160);
    CHECK(allThere() && song.trackDeletePart() == 0.f && !song.cursorOnHead());
    rig.key(B, false);
    rig.frames(1);
    CHECK(allThere());
    // Held past the half second the bar waits, then let go: nothing, the cell neither.
    song.moveCursorTo(0, 2);
    rig.key(B, true);
    rig.frames(40);
    CHECK(song.trackDeletePart() > 0.f);
    rig.key(B, false);
    rig.frames(1);
    CHECK(allThere());
    // A tap: the press alone changes nothing, the let go empties the cell, and only that.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    CHECK(allThere());
    rig.frames(5);
    rig.key(B, false);
    CHECK(!full(1) && full(0) && full(2) && rig.project().trackCount() == 3);
    // B held and left alone still deletes the track it went down on.
    song.moveCursorTo(0, 2);
    rig.key(B, true);
    rig.frames(130);
    rig.key(B, false);
    CHECK(rig.project().trackCount() == 2 && full(0) && rig.project().tracks[1].kind == TrackKind::Phazed);
    // A tab turned and turned back with B held: the tabs are a press the screen never sees, and the delete
    // is called off while another screen is up. Coming back, nothing is deleted.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(60);
    rig.tap(R2);
    rig.frames(200);
    rig.tap(L2);
    rig.frames(200);
    CHECK(rig.view() == View::Song && rig.project().trackCount() == 2 && song.trackDeletePart() == 0.f);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 2 && full(0));
}

// START plays and stops with SONG's lists open too, as on every screen (gesture audit G39; the list of
// recordings already did). With SELECT held it does not: SELECT + START quits.
void testStartWithTheSongListsOpen() {
    Rig rig;
    markScenes(rig, 3);
    SongScreen& song = rig.app.song();
    rig.tap(L2);
    rig.tap(DOWN); // scene 2
    rig.tap(SELECT); // the list of songs
    CHECK(song.listingSongs());
    rig.tap(START);
    CHECK(rig.project().playing && song.listingSongs() && sceneHeard(rig) == 1); // the song, from the cursor's scene
    rig.tap(START);
    CHECK(!rig.project().playing && song.listingSongs());
    rig.key(SELECT, true);
    rig.tap(START);
    rig.key(SELECT, false);
    CHECK(!rig.project().playing && song.listingSongs());
    rig.tap(B);
    CHECK(!song.listingSongs());
    // The list of instruments (A on + TRACK).
    rig.listen(20000);
    song.moveCursorTo(2, rig.project().trackCount());
    rig.tap(A);
    CHECK(song.choosing());
    rig.tap(START);
    CHECK(rig.project().playing && song.choosing() && sceneHeard(rig) == 2);
    rig.tap(START);
    CHECK(!rig.project().playing && song.choosing());
    rig.key(SELECT, true);
    rig.tap(START);
    rig.key(SELECT, false);
    CHECK(!rig.project().playing && song.choosing() && rig.project().trackCount() == 1);
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
    const std::string root = tempFolder("battery");
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
    // Its voltage: the supply with a "capacity", in microvolts.
    CHECK(batteryMicrovolts(root + "/no-such-folder") == -1 && batteryMicrovolts(root) == -1);
    writeTextFile(root + "/battery/voltage_now", "3800000\n");
    CHECK(batteryMicrovolts(root) == 3800000);
    writeTextFile(root + "/ac/voltage_now", "5000000\n"); // (the supply with no battery: not read)
    CHECK(batteryMicrovolts(root) == 3800000);
    writeTextFile(root + "/battery/voltage_now", "nonsense\n");
    CHECK(batteryMicrovolts(root) == -1);
}

void testBatteryLog() {
    Rig rig;
    const std::string root = tempFolder("batterylog");
    mkdir((root + "/battery").c_str(), 0755);
    writeTextFile(root + "/battery/capacity", "61\n");
    writeTextFile(root + "/battery/status", "Discharging\n");
    writeTextFile(root + "/battery/voltage_now", "3800000\n");
    const std::string log = root + "/battery.log";
    rig.app.useBatteryFolder(root);
    rig.app.useBatteryLog(log);
    // The first read writes a line at once.
    rig.frames(1);
    std::string text;
    CHECK(readTextFile(log, text) && text == "[1.0] 61% 3.80V discharging\n");
    // Under a minute and unmoved: nothing more.
    rig.clock += 59000;
    rig.frames(1);
    CHECK(readTextFile(log, text) && text == "[1.0] 61% 3.80V discharging\n");
    // A minute on: another line (on the next ten-second read past the minute).
    rig.clock += 10000;
    rig.frames(1);
    CHECK(readTextFile(log, text) && text == "[1.0] 61% 3.80V discharging\n[70.0] 61% 3.80V discharging\n");
    // Down five: a line at once. The charger on: one too.
    writeTextFile(root + "/battery/capacity", "55\n");
    rig.clock += 11000;
    rig.frames(1);
    CHECK(readTextFile(log, text) && text.size() > 60 && text.substr(text.size() - 29) == "[81.1] 55% 3.80V discharging\n");
    writeTextFile(root + "/battery/status", "Charging\n");
    rig.clock += 11000;
    rig.frames(1);
    CHECK(readTextFile(log, text) && text.size() > 60 && text.substr(text.size() - 26) == "[92.1] 55% 3.80V charging\n");
    // No voltage file: a "?" where the volts go.
    std::remove((root + "/battery/voltage_now").c_str());
    rig.clock += 61000;
    rig.frames(1);
    CHECK(readTextFile(log, text) && text.size() > 60 && text.substr(text.size() - 23) == "[153.1] 55% ? charging\n");
    // No battery at all: nothing kept.
    Rig bare;
    const std::string root2 = tempFolder("batterylogbare");
    bare.app.useBatteryFolder(root2);
    bare.app.useBatteryLog(root2 + "/battery.log");
    bare.frames(10);
    CHECK(!readTextFile(root2 + "/battery.log", text));
    // Over the cap: back to the lines kept, the new line last.
    std::string big;
    for (int i = 0; i < 10000; ++i) big += "[0.0] 61% 3.80V discharging\n";
    writeTextFile(log, big);
    rig.clock += 61000;
    rig.frames(1);
    CHECK(readTextFile(log, text));
    int lines = 0;
    for (char c : text) lines += c == '\n';
    CHECK(lines == 3000 && text.size() > 23 && text.substr(text.size() - 23) == "[214.1] 55% ? charging\n");
}

// ---- bouncing: tools ---------------------------------------------------------------------
struct Wav { int rate = 0, channels = 0, bits = 0; std::vector<float> left, right; };
static std::vector<std::string> filesEnding(const std::string& folder, const std::string& ending) {
    std::vector<std::string> names;
    if (DIR* dir = opendir(folder.c_str())) {
        while (const dirent* e = readdir(dir)) {
            const std::string n = e->d_name;
            if (n.size() > ending.size() && n.substr(n.size() - ending.size()) == ending) names.push_back(n);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}
static Wav readWav(const std::string& path) {
    Wav w;
    std::string bytes;
    if (!readTextFile(path, bytes) || bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 8, "WAVEfmt ") != 0) return w;
    const auto u = [&](std::size_t at, int n) { unsigned v = 0; for (int i = 0; i < n; ++i) v |= static_cast<unsigned>(static_cast<unsigned char>(bytes[at + i])) << (8 * i); return v; };
    w.channels = static_cast<int>(u(22, 2));
    w.rate = static_cast<int>(u(24, 4));
    w.bits = static_cast<int>(u(34, 2));
    const std::size_t data = u(40, 4);
    if (bytes.compare(36, 4, "data") != 0 || data + 44 != bytes.size() || u(4, 4) != data + 36) return w;
    for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) {
        w.left.push_back(static_cast<float>(static_cast<std::int16_t>(u(at, 2))) / 32767.f);
        w.right.push_back(static_cast<float>(static_cast<std::int16_t>(u(at + 2, 2))) / 32767.f);
    }
    return w;
}
// The loudest sample on either side, from `from` up to `to`.
static float wavPeak(const Wav& w, std::size_t from, std::size_t to) {
    float p = 0.f;
    for (std::size_t i = from; i < to && i < w.left.size(); ++i) p = std::fmax(p, std::fmax(std::fabs(w.left[i]), std::fabs(w.right[i])));
    return p;
}
static const float kWavStep = 1.f / 32767.f; // the smallest thing a 16-bit file can hold
static const std::size_t kAfter = 24000;     // half a second: what a bounce keeps after its last sound
// A bounce ends where it should: nothing but the last flicker in its last half second, and sound just before that.
static bool endsOnItsLastSound(const Wav& w, std::size_t body) {
    if (w.left.size() < body + kAfter) return false;
    const std::size_t end = w.left.size();
    if (wavPeak(w, end - kAfter + 1, end) > 1.01f * kWavStep) return false;
    return end == body + kAfter || wavPeak(w, end - kAfter - 2400, end - kAfter + 1) >= 0.99f * kWavStep;
}

// Bouncing: the song, or its tracks, written out as WAV files by a second engine.
void testBouncing() {
    const std::string folder = tempFolder("bounce");
    const auto wavsIn = [&] { return filesEnding(folder, ".wav"); };
    const auto read = [&](const std::string& name) { return readWav(folder + "/" + name); };
    const auto peakIn = [](const Wav& w, std::size_t from, std::size_t to) { float p = 0.f; for (std::size_t i = from; i < to && i < w.left.size(); ++i) p = std::fmax(p, std::fabs(w.left[i])); return p; };

    Rig rig;
    Session& session = rig.app.session();
    // Nowhere to put it: no bounce.
    CHECK(!session.startBounce(Session::Bounce::Song) && !session.bouncing() && session.bounceFiles() == 0);
    // WHAT A KILLED BOUNCE LEFT (the power went, or the program was killed: nothing was there to
    // remove its half-written file, and nothing lists one). It goes when the program opens...
    const auto plant = [&](const char* name) { std::FILE* f = std::fopen((folder + "/" + name).c_str(), "wb"); std::fputs("half", f); std::fclose(f); };
    const auto there = [&](const char* name) { struct stat st; return stat((folder + "/" + name).c_str(), &st) == 0; };
    plant("old 261112-104548.wav.part");
    plant("old 261112-104548.mid.part");
    plant("notes.part"); // not a bounce's: left alone
    session.useBounceFolder(folder);
    CHECK(!there("old 261112-104548.wav.part") && !there("old 261112-104548.mid.part") && there("notes.part"));
    plant("old 261112-104549.wav.part"); // ...and when the next bounce starts
    // A song: scene 1 (two bars' worth: it plays twice) with a kick on each beat, and a JAX-EP track with one long note.
    for (int step = 0; step < kSteps; step += 4) rig.app.drums().setStep(0, step, 1.f);
    rig.project().repeats[0] = 2;
    session.addTrack(TrackKind::Jx3p);
    NotePattern& bar = rig.project().tracks[1].notes[0][0];
    bar.notes[0].step = 8;
    bar.notes[0].pitch = 57;
    bar.notes[0].length = 4.f;
    bar.notes[0].velocity = 0.8f;
    session.sendNotePattern(1, 0);
    session.setTrackName(1, "LEAD");
    const Jx3pPatch asItWas = rig.project().tracks[1].jx3p;
    rig.frames(2);

    // The whole song: one file, as long as the song and its tail (the last kick ringing out, and half a second), with sound where the song has it.
    CHECK(session.startBounce(Session::Bounce::Song));
    CHECK(!session.startBounce(Session::Bounce::Song)); // one at a time
    session.finishBounce();
    CHECK(!session.bouncing() && session.bounceProgress() == 1.f && session.bounceFiles() == 1 && session.bounceTail() < 0.f);
    std::vector<std::string> names = wavsIn();
    CHECK(names.size() == 1 && names[0] == session.bounceLast() && filesEnding(folder, ".wav.part").empty() && there("notes.part"));
    const std::size_t body = static_cast<std::size_t>(2 * kBar);
    Wav song = read(names[0]);
    CHECK(song.rate == 48000 && song.channels == 2 && song.bits == 16);
    // A dry song: it ends half a second after its last sound, which is well inside the three seconds every bounce used to get.
    CHECK(endsOnItsLastSound(song, body) && song.left.size() < body + 3 * 48000);
    const std::size_t tail = song.left.size() - body;
    CHECK(peakIn(song, 0, 2000) > 0.2f);                                  // the first kick, at the very start
    CHECK(peakIn(song, kBar, kBar + 2000) > 0.2f);                        // and the scene's second time round
    CHECK(peakIn(song, 8 * kStep + 500, 11 * kStep) > 0.01f);             // the JAX-EP note
    CHECK(peakIn(song, body + tail - kAfter, body + tail) < 0.001f);      // the tail has died away: nothing started again
    CHECK(peakIn(song, body + 4000, body + 24000) < peakIn(song, 0, 2000)); // no kick after the song's end
    // The song itself was only read: the synth's settings are as they were, and nothing is playing.
    CHECK(std::equal(asItWas.value, asItWas.value + jx3p::kSettings, rig.project().tracks[1].jx3p.value) && !rig.project().playing);

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
    // Both as long as the longer of the two, so they line up: one of them ends on its own last sound.
    CHECK(d.left.size() == l.left.size() && d.left.size() >= body + kAfter && (endsOnItsLastSound(d, body) || endsOnItsLastSound(l, body)));
    CHECK(peakIn(d, 0, 2000) > 0.2f && peakIn(l, 0, 2000) == 0.f);                 // the kick is in the drums' file only
    CHECK(peakIn(l, 8 * kStep + 500, 11 * kStep) > 0.01f && peakIn(l, 4 * kStep, 4 * kStep + 2000) == 0.f); // the note is in the lead's file, and no kick on the second beat either
    // A muted track is left out of a song bounce, and still gets its own file when asked for.
    session.setMuted(0, true);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    Wav quiet = read(session.bounceLast());
    CHECK(endsOnItsLastSound(quiet, body) && peakIn(quiet, 0, 2000) == 0.f && peakIn(quiet, 8 * kStep + 500, 11 * kStep) > 0.01f);
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
    while (session.bouncing() && session.bounceProgress() <= 0.f) usleep(200); // its file is open and filling
    CHECK(session.bouncing() && session.bounceProgress() < 1.f && filesEnding(folder, ".wav.part").size() == 1 && wavsIn().size() == before);
    // Asking for another while it works is refused, and does not take the working one's file away.
    CHECK(!session.startBounce(Session::Bounce::Song) && (!session.bouncing() || filesEnding(folder, ".wav.part").size() == 1));
    // Nor does naming the folder again.
    session.useBounceFolder(folder);
    CHECK(!session.bouncing() || filesEnding(folder, ".wav.part").size() == 1);
    session.cancelBounce();
    CHECK(!session.bouncing() && wavsIn().size() == before);
    for (const std::string& n : wavsIn()) CHECK(read(n).left.size() >= body + kAfter); // every file there is whole
    CHECK(wavsIn().size() <= before + 1 && filesEnding(folder, ".wav.part").empty());
    // An EACH TRACK cut short, its first track whole and the second under way: nothing of it is kept,
    // the whole one neither (it was a part of the stems, never padded to their one length).
    {
        const std::size_t had = wavsIn().size();
        sleep(1);
        CHECK(session.startBounce(Session::Bounce::EachTrack));
        while (session.bouncing() && session.bounceProgress() <= 0.55f) usleep(200);
        CHECK(session.bouncing() && session.bounceProgress() > 0.5f);
        session.cancelBounce();
        CHECK(!session.bouncing() && session.bounceFiles() == 0 && session.bounceLast().empty());
        CHECK(wavsIn().size() == had && filesEnding(folder, ".wav.part").empty());
    }
    // Whole files are never taken for half-written ones: every bounce made is still there after one more starts.
    const std::size_t whole = wavsIn().size();
    rig.project().repeats[0] = 1;
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    CHECK(wavsIn().size() == whole + 1);
}

// WHERE A BOUNCE ENDS, by itself: made-up tails, no engine and no file.
void testWhereABounceEnds() {
    const int block = 256;
    // Feeds `seconds` of a steady `level` and then nothing, until it says stop. Returns the frames it took.
    const auto run = [&](BounceTail& tail, double seconds, float level) {
        std::vector<float> io(2 * block);
        const long long sounding = static_cast<long long>(seconds * 48000.0);
        for (int guard = 0; guard < 4000000; ++guard) {
            for (int i = 0; i < block; ++i) io[2 * i] = io[2 * i + 1] = tail.seen() + i < sounding ? level : 0.f;
            if (!tail.take(io.data(), block)) break;
        }
        return tail.seen();
    };
    // Nothing at all (a dry song that ended on its last step): seven seconds are listened to, half a second is kept.
    {
        BounceTail tail(48000.f);
        const long long took = run(tail, 0.0, 0.f);
        CHECK(took >= 7 * 48000 && took < 7 * 48000 + block && tail.keep() == kAfter && !tail.capped());
        CHECK(kBounceQuietSeconds == 7.f && kBounceAfterSeconds == 0.5f && kBounceFloor == 3.1e-5f);
    }
    // A long tail: all of it, and half a second.
    for (double seconds : {0.001, 2.75, 10.0, 100.0}) {
        BounceTail tail(48000.f);
        const long long sounding = static_cast<long long>(seconds * 48000.0), took = run(tail, seconds, 0.01f);
        CHECK(took >= sounding + 7 * 48000 && took < sounding + 7 * 48000 + block && tail.keep() == sounding + static_cast<long long>(kAfter) && !tail.capped());
    }
    // As it goes, what belongs in the file never runs ahead of what has been heard, and never goes back.
    {
        BounceTail tail(48000.f);
        std::vector<float> io(2 * block, 0.01f);
        long long before = 0;
        for (int i = 0; i < 400; ++i) {
            if (i == 200) io.assign(2 * block, 0.f);
            CHECK(tail.take(io.data(), block));
            CHECK(tail.keep() >= before && tail.keep() <= tail.seen());
            if (i < 200) CHECK(tail.keep() == tail.seen()); // while it sounds: all of it, at once
            before = tail.keep();
        }
        CHECK(tail.keep() == 200 * block + static_cast<long long>(kAfter));
    }
    // What counts as sound: one step of a 16-bit file, on either side, either way up. Less is not; nor is not-a-number.
    struct One { float left, right; bool sound; };
    for (const One& one : {One{3.2e-5f, 0.f, true}, One{0.f, -3.2e-5f, true}, One{-1.f, 0.f, true}, One{3.0e-5f, -3.0e-5f, false}, One{std::nanf(""), 0.f, false}, One{0.f, std::nanf(""), false}}) {
        BounceTail tail(48000.f);
        std::vector<float> io(2 * block, 0.f);
        CHECK(tail.take(io.data(), block));
        io[2 * 100] = one.left;
        io[2 * 100 + 1] = one.right;
        CHECK(tail.take(io.data(), block));
        CHECK(tail.keep() == (one.sound ? block + 101 + static_cast<long long>(kAfter) : static_cast<long long>(kAfter)) || tail.keep() == tail.seen());
        io.assign(2 * block, 0.f);
        while (tail.take(io.data(), block)) {}
        CHECK(tail.seen() >= (one.sound ? block + 101 : 0) + 7 * 48000 && tail.seen() < (one.sound ? block + 101 : 0) + 7 * 48000 + block);
        CHECK(tail.keep() == (one.sound ? block + 101 : 0) + static_cast<long long>(kAfter));
    }
    // ECHOES FAR APART are one tail: a click every 6.9 seconds is not ended by the gaps. The longest
    // gap a delay can leave is 6 seconds (REVERSE: two half-note windows at the slowest tempo), and seven is more.
    {
        CHECK(kBounceQuietSeconds > kLongestEchoGapSeconds + 0.5f && kLongestEchoGapSeconds == 6.f);
        BounceTail tail(48000.f);
        std::vector<float> io(2 * block, 0.f);
        const long long gap = 331200; // 6.9 s
        long long last = 0;
        bool more = true;
        while (more) {
            io.assign(2 * block, 0.f);
            for (int i = 0; i < block; ++i)
                if ((tail.seen() + i) % gap == 0 && tail.seen() + i <= 5 * gap) io[2 * i] = 0.5f, last = tail.seen() + i;
            more = tail.take(io.data(), block);
        }
        CHECK(last == 5 * gap && tail.keep() == last + 1 + static_cast<long long>(kAfter) && tail.seen() >= last + 7 * 48000);
    }
    // REVERSE'S LAST ECHO IS IN THE FILE: TIME 1/2, FEEDBACK 0, 40 BPM (three-second windows). Hits on steps 0 and 4,
    // the song ending after step 5: the hit on step 4 is at a window's start, and comes back two windows later, six
    // seconds of nothing after it. With four seconds of quiet the bounce ended 1.62 s after the song and lost it.
    {
        std::unique_ptr<Effect> rev = makeEffect(EffectKind::Reverse, 48000.f, kMinTempo);
        rev->setParam(0, 1.f), rev->setParam(1, 0.f), rev->setParam(2, 1.f);
        const std::size_t step = 18000, songEnd = 5 * step;
        std::vector<float> l(songEnd, 0.f);
        for (std::size_t k : {std::size_t(0), std::size_t(4)})
            for (std::size_t j = 0; j < 2400; ++j) l[k * step + j] = 0.8f * std::exp(-static_cast<float>(j) / 300.f);
        std::vector<float> r = l;
        rev->process(l.data(), r.data(), static_cast<int>(songEnd));
        BounceTail tail(48000.f);
        std::vector<float> lb(block), rb(block), io(2 * block);
        long long lastLoud = -1;
        for (bool more = true; more;) {
            std::fill(lb.begin(), lb.end(), 0.f), std::fill(rb.begin(), rb.end(), 0.f);
            rev->process(lb.data(), rb.data(), block);
            for (int i = 0; i < block; ++i) {
                io[2 * i] = lb[i], io[2 * i + 1] = rb[i];
                if (std::fabs(lb[i]) > 0.1f) lastLoud = tail.seen() + i;
            }
            more = tail.take(io.data(), block);
        }
        // What the effect still gives once the bounce has stopped: nothing.
        float after = 0.f;
        for (int b = 0; b < 20 * 48000 / block; ++b) {
            std::fill(lb.begin(), lb.end(), 0.f), std::fill(rb.begin(), rb.end(), 0.f);
            rev->process(lb.data(), rb.data(), block);
            for (int i = 0; i < block; ++i) after = std::fmax(after, std::fabs(lb[i]));
        }
        std::printf("bounce   REVERSE 1/2 at 40 BPM: the last echo %.2f s after the song, the file ends %.2f s after it, then %g\n", lastLoud / 48000.0, tail.keep() / 48000.0, after);
        CHECK(lastLoud > 5 * 48000 && tail.keep() > lastLoud && after < kBounceFloor);
    }
    // THE CAP: a tail that never ends is cut there, and faded out over its last second so that it is no click.
    for (float cap : {3.f, 10.f}) {
        BounceTail tail(48000.f, cap);
        const long long frames = static_cast<long long>(cap * 48000.f);
        std::vector<float> all;
        std::vector<float> io(2 * block);
        bool more = true;
        while (more) {
            for (int i = 0; i < block; ++i) io[2 * i] = 0.5f, io[2 * i + 1] = -0.25f;
            more = tail.take(io.data(), block);
            all.insert(all.end(), io.begin(), io.end());
        }
        CHECK(tail.capped() && tail.seen() >= frames && tail.seen() < frames + block && tail.keep() == frames);
        const auto leftAt = [&](long long frame) { return all[static_cast<std::size_t>(2 * frame)]; };
        CHECK(leftAt(frames - 48000 - 1) == 0.5f && all[static_cast<std::size_t>(2 * (frames - 48000 - 1) + 1)] == -0.25f); // untouched up to the last second
        CHECK(std::fabs(leftAt(frames - 24000) - 0.25f) < 1e-4f && std::fabs(all[static_cast<std::size_t>(2 * (frames - 24000) + 1)] + 0.125f) < 1e-4f); // half way down, both sides
        CHECK(leftAt(frames - 1) > 0.f && leftAt(frames - 1) < 2e-5f); // all but nothing on the last frame kept...
        for (long long f = frames; f < tail.seen(); ++f) CHECK(leftAt(f) == 0.f && all[static_cast<std::size_t>(2 * f + 1)] == 0.f); // ...and nothing past it
        float step = 0.f;
        for (long long f = frames - 48000; f < frames; ++f) step = std::fmax(step, std::fabs(leftAt(f) - leftAt(f - 1)));
        CHECK(step < 0.5f / 48000.f * 1.01f); // a straight line down
    }
    // A cap shorter than the fade is all fade; no cap at all is no tail.
    {
        BounceTail small(48000.f, 0.25f);
        std::vector<float> io(2 * block, 0.5f);
        CHECK(small.take(io.data(), block));
        CHECK(io[0] < 0.5f && io[0] > 0.49f && io[2 * (block - 1)] < io[0]);
        BounceTail none(48000.f, 0.f);
        io.assign(2 * block, 0.5f);
        CHECK(!none.take(io.data(), block) && io[0] == 0.f && none.keep() == 0 && none.capped());
    }
    // Left alone the cap is eight minutes: longer than any delay or reverb takes to die away (the engine's own number).
    {
        CHECK(kBounceCapSeconds == 480.f && kBounceFadeSeconds == 1.f && kBounceCapSeconds > kLongestTailSeconds && kLongestTailSeconds == 450.f);
        BounceTail tail(48000.f);
        std::vector<float> io(2 * 4096);
        long long blocks = 0;
        for (bool more = true; more; ++blocks) {
            io.assign(2 * 4096, 0.1f);
            more = tail.take(io.data(), 4096);
        }
        CHECK(tail.capped() && tail.keep() == 480ll * 48000 && blocks == 480ll * 48000 / 4096); // 5625 blocks, the last of which says stop
    }
    // Another sample rate: the same times.
    {
        BounceTail tail(44100.f, 2.f);
        std::vector<float> io(2 * 100, 0.f);
        while (tail.take(io.data(), 100)) {}
        CHECK(tail.seen() == 88200 && tail.keep() == 22050); // (the cap came before four seconds of quiet could)
    }
}

// A BOUNCE RUNS UNTIL ITS TAIL ENDS (owner, 2026-11-12): it used to stop three seconds after the last step.
void testABounceRunsUntilTheTailEnds() {
    const std::string folder = tempFolder("tail");
    Rig rig;
    Session& session = rig.app.session();
    session.useBounceFolder(folder);
    // One kick, on the first step, sent to SPACE, where HALL is set to its longest (20 seconds).
    rig.app.drums().setStep(0, 0, 1.f);
    session.setSend(0, 1, 1.f);
    CHECK(static_cast<EffectKind>(session.effectData(kSpaceBus, 0).kind) == EffectKind::Hall);
    session.setEffectParam(kSpaceBus, 0, 1, 1.f);
    rig.frames(2);
    const std::size_t body = static_cast<std::size_t>(kBar), second = 48000;
    CHECK(session.startBounce(Session::Bounce::Song));
    // While it works: the percentage is of the song's own playing, so it is all done by the time
    // there is a tail to speak of; and the tail's seconds only go up.
    int inTail = 0;
    float tailBefore = -1.f, progressBefore = 0.f;
    bool told = true;
    while (session.bouncing()) {
        const float tail = session.bounceTail(), progress = session.bounceProgress();
        if (tail >= 0.f) {
            ++inTail;
            told = told && progress == 1.f && tail >= tailBefore;
            tailBefore = tail;
        }
        told = told && progress >= progressBefore && progress <= 1.f;
        progressBefore = progress;
        usleep(100);
    }
    CHECK(told && inTail > 0 && tailBefore > 10.f);
    session.finishBounce();
    CHECK(session.bounceFiles() == 1);
    const Wav hall = readWav(folder + "/" + session.bounceLast());
    // Far longer than three seconds, still sounding where it used to be cut, falling all the way, and ended on its last sound.
    CHECK(hall.left.size() > body + 15 * second && hall.left.size() < body + 60 * second);
    CHECK(wavPeak(hall, body + 3 * second, body + 3 * second + 4800) > 100.f * kWavStep);
    CHECK(wavPeak(hall, body + 10 * second, body + 11 * second) < wavPeak(hall, body + 3 * second, body + 4 * second));
    CHECK(endsOnItsLastSound(hall, body));
    // THE CAP, set short: the same song is cut there, and faded so that it does not click.
    session.useBounceCap(5.f);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const Wav cut = readWav(folder + "/" + session.bounceLast());
    CHECK(cut.left.size() == body + 5 * second);
    CHECK(wavPeak(cut, body + 3 * second, body + 3 * second + 4800) == wavPeak(hall, body + 3 * second, body + 3 * second + 4800)); // the same sound up to the fade
    CHECK(wavPeak(cut, body + 5 * second - 24000 - 2400, body + 5 * second - 24000 + 2400) < 0.6f * wavPeak(hall, body + 5 * second - 24000 - 2400, body + 5 * second - 24000 + 2400));
    CHECK(wavPeak(cut, body + 5 * second - 48, body + 5 * second) <= kWavStep);
    session.useBounceCap(kBounceCapSeconds);
    // TAPE HOWLING, its FEEDBACK at 120%: this never ended. It sings for thirty seconds after the
    // last sound, then falls, and the bounce ends by itself, far short of the cap.
    session.setSend(0, 1, 0.f);
    session.setSend(0, 0, 1.f);
    CHECK(static_cast<EffectKind>(session.effectData(kEchoBus, 0).kind) == EffectKind::Tape);
    session.setEffectParam(kEchoBus, 0, 0, 0.f); // a sixteenth
    session.setEffectParam(kEchoBus, 0, 1, 1.f);
    rig.frames(2);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const Wav howl = readWav(folder + "/" + session.bounceLast());
    CHECK(howl.left.size() > body + 36 * second && howl.left.size() < body + 60 * second && endsOnItsLastSound(howl, body));
    CHECK(wavPeak(howl, body + 25 * second, body + 26 * second) > 0.1f && wavPeak(howl, body + 25 * second, body + 26 * second) > 0.8f * wavPeak(howl, body + 5 * second, body + 6 * second));
    session.setSend(0, 0, 0.f);
    session.setEffectParam(kEchoBus, 0, 1, 0.4f);
    // ECHOES FAR APART: at the slowest tempo a DELAY set to a half note repeats every three seconds.
    // Every echo that is in a 16-bit file at all is in the bounce.
    session.setSend(0, 1, 0.f);
    rig.project().tempo = kMinTempo;
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 0, 1.f);  // 1/2
    session.setEffectParam(0, 0, 1, 0.6f); // each echo about half the last
    session.setEffectParam(0, 0, 3, 1.f);
    rig.frames(2);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const Wav echoes = readWav(folder + "/" + session.bounceLast());
    const std::size_t slowBar = 3 * static_cast<std::size_t>(kBar), every = 3 * second; // a bar at 40 BPM is 6 seconds
    CHECK(echoes.left.size() > slowBar + 8 * every && endsOnItsLastSound(echoes, slowBar));
    const std::size_t last = (echoes.left.size() - kAfter) / every * every; // where the last echo that was kept began
    CHECK(wavPeak(echoes, last, last + 2400) >= kWavStep && wavPeak(echoes, last - every, last - every + 2400) > 1.5f * wavPeak(echoes, last, last + 2400));
    CHECK(wavPeak(echoes, last - every + second, last - 4800) <= kWavStep); // and between two echoes there is nothing: the gap did not end it
    // EACH TRACK: one track has a long tail and the other none. Both files are as long as the longer.
    rig.project().tempo = 120.f;
    session.setEffect(0, 0, EffectKind::None);
    session.setSend(0, 1, 1.f);
    session.addTrack(TrackKind::Drums);
    rig.project().tracks[1].drums[0][0].level[2][4] = 1.f; // a hat, dry
    rig.frames(2);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::EachTrack));
    session.finishBounce();
    CHECK(session.bounceFiles() == 2);
    Wav wet, dry;
    for (const std::string& n : filesEnding(folder, ".wav")) {
        if (n.find(" - 1 ") != std::string::npos) wet = readWav(folder + "/" + n);
        if (n.find(" - 2 ") != std::string::npos) dry = readWav(folder + "/" + n);
    }
    CHECK(wet.left.size() > body + 15 * second && dry.left.size() == wet.left.size() && wet.rate == 48000 && dry.rate == 48000);
    CHECK(endsOnItsLastSound(wet, body) && wavPeak(dry, 4 * kStep, 4 * kStep + 2400) > 0.05f && wavPeak(dry, body + 2 * second, dry.left.size()) == 0.f);
    CHECK(filesEnding(folder, ".part").empty());
    // What the screen is told. A bounce's percentage is of the song's playing; in a tail there is none to give, and it says so.
    CHECK(session.bounceTail() < 0.f && !session.bouncing());
    session.pretendBounce(0.37f, -1.f);
    CHECK(session.bouncing() && session.bounceProgress() == 0.37f && session.bounceTail() < 0.f);
    session.pretendBounce(1.f, 12.4f);
    CHECK(session.bouncing() && session.bounceProgress() == 1.f && session.bounceTail() == 12.4f);
    session.cancelBounce();
    for (const std::string& n : filesEnding(folder, ".wav")) std::remove((folder + "/" + n).c_str()); // (minutes of sound: not left lying about)
    rmdir(folder.c_str());
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
    heard.app.session().addTrack(TrackKind::Phazed);
    heard.project().track = 1;
    Session& hs = heard.app.session();
    NotePattern& bar = heard.project().tracks[1].notes[0][0];
    bar.notes[0].step = 0;
    bar.notes[0].pitch = 45;
    bar.notes[0].length = 16.f;
    bar.notes[0].velocity = 0.8f;
    hs.sendNotePattern(1, 0);
    for (int i = 8; i < 12; ++i) hs.setAutomation(1, 0, phazed::MacroOctave, i, 1.f);
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
    hs.clearAutomation(1, 0, phazed::MacroOctave);
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
    // Held over: on through them at twice a held D-pad's pace (the cursor's, which FX's choice dials keep: 66.7 a
    // second at STICK SPEED 100%), however far it is pushed, and it stops at the end.
    rig.key(STICK_UP, true);
    rig.frames(1); // one at once
    CHECK(type() == FilterType::Dj);
    rig.frames(1); // 1.07 more: the next (it was five a second: none)
    CHECK(type() == FilterType::HighPass);
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
    rig.frames(2);
    rig.key(next > 0 ? STICK_UP : STICK_DOWN, false);
    rig.frames(1);
    const int after = rig.project().tracks[1].patch.value[waveId];
    // A click at once, then twice a held D-pad's pace (the cursor's here: 1.07 a frame at STICK SPEED 100%): two more.
    const int clicked = wave + 3 * next, most = spec.settingMax(waveId), least = spec.settingMin(waveId);
    CHECK(after == (clicked > most ? most : clicked < least ? least : clicked));
    rig.key(A, false);
    // Letting go of A and taking hold again: the next push is a click again, at once.
    rig.key(A, true);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].patch.value[waveId] == after - 1 || after == spec.settingMin(waveId));
}

// The help card, looked at: its boxes hold the buttons' names, and SELECT's reads SEL
// (the card writes "SELECT, tapped" in full and draws it through the bar's rule).
void testTheHelpCardDrawsSel() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    rig.app.session().view = View::Pattern;
    rig.frames(2);
    rig.key(SDLK_TAB, true); // MENU held: the card
    rig.frames(1);
    CHECK(rig.app.helpShowing());
    rig.app.draw(canvas.r, fonts);
    rig.key(SDLK_TAB, false);
    // The boxes stand in one column, cream, 17 high. Find each one's top.
    const int left = 58;
    std::vector<int> tops;
    for (int y = 1; y < ui::kScreenH; ++y)
        if (canvas.is(left, y, ui::kText) && !canvas.is(left, y - 1, ui::kText) && !canvas.is(left - 1, y, ui::kText)) tops.push_back(y);
    CHECK(tops.size() == 16); // seven on every screen, eight with it held, one to stop all sound
    if (tops.size() != 16) return;
    // (A box's words are drawn one at a time, a space apart, as the bar draws them.)
    const auto holds = [&](int box, const char* first, const char* second) {
        const float gap = fonts.small.width(first) + (second ? fonts.small.width(" ") : 0.f);
        const int top = tops[box], w = canvas.run(left, top, ui::kText);
        if (w != static_cast<int>(gap + (second ? fonts.small.width(second) : 0.f) + 8.f)) return false; // as wide as its words
        const std::vector<Uint32> seen = canvas.part(left, top, w, 17);
        Canvas plain;
        ui::fill(plain.r, left, top, w, 17, ui::kText);
        plain.fonts->small.draw(plain.r, left + 4.f, top + 3.f, first, ui::kBg);
        if (second) plain.fonts->small.draw(plain.r, left + 4.f + gap, top + 3.f, second, ui::kBg);
        return seen == plain.part(left, top, w, 17);
    };
    CHECK(holds(0, "X", nullptr) && holds(4, "START", nullptr));
    CHECK(holds(6, "SEL,", "tapped"));
    CHECK(!holds(6, "SELECT,", "tapped") && !holds(6, "SEL,", "tappee") && !holds(6, "SEL", "tapped"));
    // X's line says WHERE held X shows effects (a hit or a note: on SONG it does not) and that on a
    // knob it is its MOD (owner's call G: said here, on the card, where the bars have no room),
    // and every line ends inside the card's margin.
    const auto says = [&](int box, const char* does) {
        const int top = tops[box] + 3;
        Canvas plain;
        ui::fill(plain.r, 190, top - 3, 410, 17, ui::kPanel);
        plain.fonts->small.draw(plain.r, 190.f, static_cast<float>(top), does, ui::kText);
        return canvas.part(190, top - 3, 408, 17) == plain.part(190, top - 3, 408, 17);
    };
    CHECK(says(0, "hear it (SONG: solo). Held: a step's effects, a knob's MOD")); // (SONG: owner, 2026-11-15)
    CHECK(says(3, "delete, empty, or go back. X held: empty the effects") && !says(0, "delete, empty, or go back. X held: empty the effects")); // (the check can tell)
    CHECK(!says(3, "delete, empty, or go back")); // (X held + B: on the card, as no strip has room for its chip)
    for (int top : tops)
        for (Uint32 px : canvas.part(583, top - 3, 15, 17)) CHECK(px == Canvas::colour(ui::kPanel));
}

// Above the battery the card says how hard the program works (owner, 2026-11-15): the sound's share
// of its own time over the last second, its worst call, and the whole program's share of the
// processor (read from /proc/self/stat; here a made-up one). Only while the card is up.
void testTheHelpCardSaysTheLoad() {
    const int none[3] = {-1, -1, -1}, all[3] = {43, 88, 37}, sound[3] = {43, 88, -1}, program[3] = {-1, -1, 37};
    CHECK(App::loadWords(none).empty());
    CHECK(App::loadWords(all) == "SOUND 43%  CPU 37%"); // WORST is not shown (owner, 2026-11-17)
    CHECK(App::loadWords(sound) == "SOUND 43%");
    CHECK(App::loadWords(program) == "CPU 37%");
    const std::string folder = tempFolder("cpu"), stat = folder + "/stat";
    const auto ticks = [&](long utime) {
        std::FILE* f = std::fopen(stat.c_str(), "w");
        std::fprintf(f, "9 (apotheca) S 1 2 3 4 5 6 7 8 9 10 %ld 0 0 0\n", utime);
        std::fclose(f);
    };
    Rig rig;
    rig.app.useCpuFile(stat, 100.0, 4);
    rig.app.session().view = View::Pattern;
    ticks(1000);
    rig.frames(2);
    CHECK(rig.app.loadShown()[0] == -1 && rig.app.loadShown()[2] == -1); // shut: nothing read
    rig.key(SDLK_TAB, true); // MENU held: the card
    rig.frames(1);
    CHECK(rig.app.loadShown()[0] == -1 && rig.app.loadShown()[2] == -1); // no second of sound yet; the program's first reading
    rig.render(48000 * 11 / 10);
    ticks(1100); // a whole core of four for the next second
    rig.frames(30);
    CHECK(rig.app.loadShown()[0] >= 0 && rig.app.loadShown()[1] >= rig.app.loadShown()[0] && rig.app.loadShown()[2] == -1);
    rig.frames(33); // a second after it opened
    CHECK(rig.app.loadShown()[2] == 25);
    // Shut and opened again: the program's share is read afresh, a second later.
    rig.key(SDLK_TAB, false);
    rig.frames(1);
    CHECK(rig.app.loadShown()[0] == -1 && rig.app.loadShown()[2] == -1);
    rig.key(SDLK_TAB, true);
    rig.frames(1);
    CHECK(rig.app.loadShown()[0] >= 0 && rig.app.loadShown()[2] == -1);
    // As drawn: right of the card's words, above where the battery stands, inside the card's margin.
    Canvas canvas;
    CHECK(canvas.ok);
    if (canvas.ok) {
        rig.app.pretendLoad(43, 88, 37);
        rig.frames(1);
        rig.app.draw(canvas.r, *canvas.fonts);
        const char* words = "SOUND 43%  CPU 37%";
        const int x = (ui::kScreenW - 560) / 2, y = ui::kTopBarH + 4, w = 560, h = 440;
        const int right = x + w - 18, top = y + h - 64, wide = static_cast<int>(canvas.fonts->small.width(words)) + 2;
        Canvas plain;
        ui::fill(plain.r, right - wide, top - 3, wide + 2, 17, ui::kPanel);
        plain.fonts->small.drawRight(plain.r, static_cast<float>(right), static_cast<float>(top), words, ui::kText);
        CHECK(canvas.part(right - wide, top - 3, wide + 2, 17) == plain.part(right - wide, top - 3, wide + 2, 17));
        CHECK(right - wide > x + 150 + static_cast<int>(canvas.fonts->small.width("quit Apotheca")) + 8); // clear of the line to its left
    }
    rig.key(SDLK_TAB, false);
    std::remove(stat.c_str());
    rmdir(folder.c_str());
}

// On our own firmware for the RG40XXV quitting turns the handheld off (power_off.hpp; firmware plan
// batch 4): the system sets two environment variables, the program writes a mark once the song is
// saved. The port sets neither, and quitting quits.
void testQuittingTurnsTheHandheldOff() {
    const auto nothing = [](const char*) -> const char* { return nullptr; };
    const PowerOff port = PowerOff::from(nothing);
    CHECK(!port.turnsOff() && port.idleMs == 0 && !port.idleDue(4000000000u) && !port.writeMark("quit"));
    const std::string folder = tempFolder("poweroff"), mark = folder + "/apotheca.off";
    const char* minutes = "15";
    const char* markAt = mark.c_str();
    const auto firmware = [&](const char* name) -> const char* {
        if (std::strcmp(name, "APOTHECA_OFF_MARK") == 0) return markAt;
        if (std::strcmp(name, "APOTHECA_IDLE_OFF_MINUTES") == 0) return minutes;
        return nullptr;
    };
    PowerOff off = PowerOff::from(firmware);
    CHECK(off.turnsOff() && off.idleMs == 15u * 60000u);
    off.busy(1000);
    CHECK(!off.idleDue(1000) && !off.idleDue(1000 + 899999) && off.idleDue(1000 + 900000));
    off.busy(0xFFFFFF00u); // the clock going round (49 days) is no harm
    CHECK(!off.idleDue(0xFFFFFF00u + 100u) && off.idleDue(0xFFFFFF00u + 900000u));
    // Whole minutes, a day at most; anything else is never.
    const char* never[] = {"0", "-3", "abc", "", "nan", "1e300", "0.5", "1441"};
    for (const char* n : never) {
        minutes = n;
        CHECK(PowerOff::from(firmware).idleMs == 0);
    }
    minutes = "2.5";
    CHECK(PowerOff::from(firmware).idleMs == 2u * 60000u);
    minutes = "1440";
    CHECK(PowerOff::from(firmware).idleMs == 1440u * 60000u);
    // No mark, nobody to turn it off: never ends by itself, whatever the minutes.
    markAt = nullptr;
    minutes = "15";
    CHECK(!PowerOff::from(firmware).turnsOff() && PowerOff::from(firmware).idleMs == 0);
    markAt = mark.c_str();
    // The mark says why.
    CHECK(off.writeMark("idle"));
    std::string text;
    CHECK(readTextFile(mark, text) && text == "idle\n");
    CHECK(off.writeMark("quit") && readTextFile(mark, text) && text == "quit\n");
    std::remove(mark.c_str());
    PowerOff nowhere = off;
    nowhere.mark = folder + "/no such folder/apotheca.off";
    CHECK(!nowhere.writeMark("quit"));
    rmdir(folder.c_str());

    // The words: the help card's START line and the update notice say what SEL + START does.
    CHECK(std::string(App::quitWords(false)) == "quit Apotheca" && std::string(App::quitWords(true)) == "turn off");
    CHECK(std::string(App::updateNowWords(false)) == "To update now: SEL + START quits; then start it again.");
    CHECK(std::string(App::updateNowWords(true)) == "To update now: SEL + START turns off; then turn it on.");
    Canvas canvas;
    CHECK(canvas.ok);
    if (canvas.ok) {
        // No longer than what it replaces: the load line beside it keeps its room, and the notice its width.
        CHECK(canvas.fonts->small.width(App::quitWords(true)) <= canvas.fonts->small.width(App::quitWords(false)));
        CHECK(canvas.fonts->small.width(App::updateNowWords(true)) <= canvas.fonts->small.width(App::updateNowWords(false)));
        // Drawn: the card with the firmware's words differs from the port's, and only in the START line's words.
        Rig rig;
        rig.app.session().view = View::Pattern;
        rig.frames(2);
        rig.key(SDLK_TAB, true); // MENU held: the card
        rig.frames(1);
        rig.app.draw(canvas.r, *canvas.fonts);
        const std::vector<Uint32> quits = canvas.all();
        CHECK(!rig.app.quitTurnsOff());
        rig.app.setQuitTurnsOff(true);
        rig.frames(1);
        rig.app.draw(canvas.r, *canvas.fonts);
        const std::vector<Uint32> offs = canvas.all();
        CHECK(quits != offs);
        int first = ui::kScreenH, last = -1;
        for (int i = 0; i < ui::kScreenW * ui::kScreenH; ++i)
            if (quits[i] != offs[i]) first = std::min(first, i / ui::kScreenW), last = std::max(last, i / ui::kScreenW);
        CHECK(last >= first && last - first < 20); // one line of the card
        rig.app.setQuitTurnsOff(false);
        rig.frames(1);
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(canvas.all() == quits);
        rig.key(SDLK_TAB, false);
    }
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
    CHECK(std::string(ui::gHelpHint) == "HLD SEL: HELP");
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
    // SELECT held with another button is a combo, not the card (G38): SEL + B, B, B undoing shows each
    // UNDONE in the box the card would cover, however long SELECT stays down.
    other.app.session().view = View::Device;
    other.frames(1);
    other.key(SELECT, true);
    other.frames(5);
    other.tap(B);
    CHECK(std::string(other.app.undoSaid()).find("UNDO") != std::string::npos);
    other.frames(60);
    CHECK(!other.app.helpShowing());
    other.tap(B);
    other.frames(60);
    CHECK(!other.app.helpShowing() && std::string(other.app.undoSaid()).find("UNDO") != std::string::npos);
    other.key(SELECT, false);
    other.frames(1);
    CHECK(!other.app.helpShowing() && other.view() == View::Device); // (used: no tap either)
    // Held by itself, the card; another button then puts it away until SELECT is let go.
    other.key(SELECT, true);
    other.frames(27);
    CHECK(other.app.helpShowing());
    other.tap(UP); // SEL + UP: a beat a minute faster, and the card goes so the screen is seen
    other.frames(1);
    CHECK(!other.app.helpShowing() && other.project().tempo == 121.f);
    other.frames(60);
    CHECK(!other.app.helpShowing());
    other.key(SELECT, false);
    other.frames(1);
    other.key(SELECT, true); // a new hold: the card again
    other.frames(27);
    CHECK(other.app.helpShowing());
    other.key(SELECT, false);
    other.frames(1);
    CHECK(!other.app.helpShowing() && other.view() == View::Device);
    rig.frames(1); // back to a handheld with MENU, for the tests after this one; but it is in the button test,
    CHECK(rig.app.settings().testing() && ui::gHelpHint[0] == 0); // ...where the card does not come up, so the bar does not offer it
    rig.app.session().view = View::Pattern;
    rig.frames(1);
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
    CHECK(hits() == 11 && near(drums.level(0, 3), 0.9f) && drums.level(2, 8) > 0.f);
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
    // A sound is part of it too (owner, 2026-11-14): a preset chosen since is a step of its own.
    const int presetWas = rig.project().tracks[1].preset;
    rig.app.session().chooseSynthPreset(1, 4);
    settle();
    CHECK(session.undo() && rig.project().tracks[1].preset == presetWas && !bar.empty());
    CHECK(session.undo() && bar.empty());

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
    {
        // A cell that is NOT the one open, emptied from the SONG screen: a step of undo all the same
        // (review 2026-11-23: none was made, the cell's mark only watching the open cell), and an
        // empty cell emptied makes none.
        TrackData& t = rig.project().tracks[0];
        t.drums[2][0].level[1][4] = 0.7f;
        session.sendTrackImage(0);
        session.noteEdit(); // (as the grid marks a hit it writes)
        settle();
        const int steps = session.undoSteps();
        session.view = View::Song;
        rig.app.song().moveCursorTo(2, 0); // scene 3, track 1: not the cell open (scene 1)
        rig.tap(B);
        settle();
        CHECK(static_cast<const TrackData&>(t).drums[2][0].empty() && session.undoSteps() == steps + 1);
        rig.tap(B); // empty already
        settle();
        CHECK(session.undoSteps() == steps + 1);
        CHECK(session.undo());
        CHECK(static_cast<const TrackData&>(rig.project().tracks[0]).drums[2][0].level[1][4] == 0.7f);
        session.view = View::Pattern;
    }
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

void testFm6OnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    // + TRACK: FM-6 is the fifth instrument, after the sampler, where QMOD was.
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 3);
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Fm6);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[24];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 FM-6" && isSynth(track.kind));
    // It starts on its first preset, every operator on, and plays.
    const Fm6Patch first = Fm6::presetPatch(0);
    CHECK(track.preset == 0 && std::equal(first.value, first.value + fm6::kSettings, track.fm6.value) && track.fm6.value[fm6::OpsOn] == 63);
    session.audition(1, 60, 0.8f);
    CHECK(rig.listen(4000) > 0.02f);
    rig.render(96000);
    // The device screen: ADVANCED, then R1 to OPERATOR 1. Its first box is its ON: turned off, it is one
    // bit of OPERATORS ON (OP1 is bit 5), and the engine is told that whole setting, not a bit.
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.fm6();
    // FOLDER turned: a bank at a time (ROM 1B is the 33rd), and back. (With ROM 1A alone it froze the
    // handheld once, 2026-11-15: stepFolder's one-folder guard. Since the banks were added, 2026-11-16,
    // no synth has one folder, and no test reaches the guard.)
    screen.goTo(0, SynthScreen::kFolder);
    rig.key(A, true), rig.frames(1);
    rig.tap(UP);
    CHECK(track.preset == 32);
    rig.tap(DOWN);
    CHECK(track.preset == 0);
    rig.tap(LEFT);
    CHECK(track.preset >= 4 * 32); // round to the last folder: ROM 4B, or one of the built-ins added after it
    rig.tap(RIGHT);
    rig.key(A, false), rig.frames(1);
    CHECK(track.preset == 0);
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.pageCount() == 9);
    rig.tap(R1);
    CHECK(screen.page() == 2 && screen.cursor() == 0 && screen.settingHere() == fm6::kSettings); // OPERATOR 1: ON
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 32);
    rig.key(A, true);
    rig.tap(DOWN); // the end stops: OFF stays OFF, and no other operator is touched
    rig.key(A, false);
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 32);
    // R1 again, OPERATOR 2: its ON is bit 4. On again for OP1 after.
    rig.tap(R1);
    CHECK(screen.page() == 3 && screen.settingHere() == fm6::kSettings + 1);
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 32 - 16);
    rig.tap(L1);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 16);
    // A setting of an operator: OPERATOR 1's OUTPUT LEVEL, the next box, is OP1's own (id 121: OP6 comes first).
    rig.tap(RIGHT);
    CHECK(screen.settingHere() == fm6::opSetting(1, fm6::OpLevel) && fm6::opSetting(1, fm6::OpLevel) == 121);
    const int level = track.fm6.value[121];
    rig.key(A, true);
    rig.tap(DOWN, 3);
    rig.key(A, false);
    CHECK(track.fm6.value[121] == level - 3);
    // The engine has the sound as the track has it: what a fresh FM-6 given the same numbers plays. (The LFO
    // and the waves' places run on in the engine's synth: they are stilled, and Play and Stop start its voices
    // afresh, so the two can be held sample to sample.)
    session.setSynthSetting(1, fm6::LfoPmd, 0);
    session.setSynthSetting(1, fm6::LfoAmd, 0);
    session.setSynthSetting(1, fm6::KeySync, 1);
    session.togglePlay(false);
    rig.render(4800);
    session.togglePlay(false);
    rig.render(96000);
    {
        Fm6 same;
        same.prepare(48000.f);
        for (int id = 0; id < fm6::kSettings; ++id) same.setSetting(id, track.fm6.value[id]);
        same.noteOn(60, 0.8f);
        std::vector<float> l(4000), r(4000);
        for (int at = 0; at < 4000; at += 64) same.render(l.data() + at, r.data() + at, 4000 - at < 64 ? 4000 - at : 64);
        rig.frames(200);
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
    // Y does nothing on FM-6's DEVICE screen (ROLL went with QMOD; it is PHYSIC's alone now).
    const Fm6Patch before = track.fm6;
    screen.goTo(0, SynthScreen::kPreset);
    rig.tap(Y);
    CHECK(std::equal(before.value, before.value + fm6::kSettings, track.fm6.value) && track.preset == 0);
    // Its settings read as what they are.
    const SynthSpec& spec = fm6Spec();
    const auto reads = [&](int id, int value) { spec.settingText(id, value, text, sizeof text); return std::string(text); };
    CHECK(reads(fm6::Algorithm, 0) == "1" && reads(fm6::Algorithm, 31) == "32");
    CHECK(reads(fm6::Transpose, 24) == "+0" && reads(fm6::Transpose, 36) == "+12" && reads(fm6::Transpose, 0) == "-24");
    CHECK(reads(fm6::LfoWave, 0) == "TRIANGLE" && reads(fm6::LfoWave, 5) == "RANDOM" && reads(fm6::KeySync, 1) == "ON");
    CHECK(reads(fm6::opSetting(3, fm6::OpBreakPoint), 39) == "C3" && reads(fm6::opSetting(3, fm6::OpBreakPoint), 0) == "A-1" && reads(fm6::opSetting(3, fm6::OpBreakPoint), 99) == "C8");
    CHECK(reads(fm6::opSetting(6, fm6::OpLeftCurve), 0) == "-LIN" && reads(fm6::opSetting(1, fm6::OpRightCurve), 2) == "+EXP");
    CHECK(reads(fm6::opSetting(2, fm6::OpDetune), 7) == "+0" && reads(fm6::opSetting(2, fm6::OpDetune), 0) == "-7");
    CHECK(reads(fm6::opSetting(2, fm6::OpMode), 1) == "FIXED" && reads(fm6::kSettings + 5, 0) == "OFF");
    CHECK(reads(fm6::opSetting(4, fm6::OpCoarse), 0) == "0.50" && reads(fm6::opSetting(4, fm6::OpCoarse), 3) == "3.00");
    // COARSE and FINE on the device: a ratio, or under FIXED a pitch, read with the rest of the operator.
    Fm6Patch p = Fm6::startPatch();
    const auto readsIn = [&](int id) { settingReading(spec, p.value, id, text, sizeof text); return std::string(text); };
    p.value[fm6::opSetting(2, fm6::OpCoarse)] = 2;
    p.value[fm6::opSetting(2, fm6::OpFine)] = 50;
    CHECK(readsIn(fm6::opSetting(2, fm6::OpCoarse)) == "2.00" && readsIn(fm6::opSetting(2, fm6::OpFine)) == "3.00");
    p.value[fm6::opSetting(2, fm6::OpMode)] = 1;
    p.value[fm6::opSetting(2, fm6::OpFine)] = 51;
    CHECK(readsIn(fm6::opSetting(2, fm6::OpCoarse)) == "100 HZ" && readsIn(fm6::opSetting(2, fm6::OpFine)) == "324 HZ");
    p.value[fm6::opSetting(2, fm6::OpCoarse)] = 4; // only its last two bits count: 1 Hz
    CHECK(readsIn(fm6::opSetting(2, fm6::OpCoarse)) == "1.00 HZ" && readsIn(fm6::opSetting(2, fm6::OpFine)) == "3.24 HZ");
    p.value[fm6::opSetting(2, fm6::OpCoarse)] = 1;
    CHECK(readsIn(fm6::opSetting(2, fm6::OpFine)) == "32.4 HZ" && readsIn(fm6::opSetting(1, fm6::OpFine)) == "1.00");
    p.value[fm6::OpsOn] = 63 - 4; // OP4 off: its ON reads OFF, the others ON
    CHECK(readsIn(fm6::kSettings + 3) == "OFF" && readsIn(fm6::kSettings + 2) == "ON" && readsIn(fm6::kSettings + 4) == "ON");
    CHECK(readsIn(fm6::Algorithm) == "1"); // (one with no reading of its own reads as alone)
    CHECK(readsIn(fm6::opSetting(2, fm6::OpLevel)) == std::to_string(p.value[fm6::opSetting(2, fm6::OpLevel)]) && readsIn(fm6::opSetting(2, fm6::OpR1)) == "99"); // nor an operator's other settings
    CHECK(reads(fm6::kSettings, 1) == "ON" && reads(fm6::kSettings, 0) == "OFF"); // OPERATOR 1's ON, the first past the synth's own
    spec.macroText(fm6::MacroBright, 1.f, text, sizeof text);
    CHECK(std::string(text) == "+100");
    CHECK(std::string(spec.macroNames[0]) == "BRIGHT" && std::string(spec.macroNames[7]) == "PITCH ENV");
    // Every setting is on some page, once: OPERATORS ON as its six ONs, never as itself.
    CHECK(shownSettingCount(TrackKind::Fm6) == fm6::kSettings + 6 && settingCount(TrackKind::Fm6) == fm6::kSettings);
    int seen[fm6::kSettings + 6] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < fm6::kSettings + 6 && name[0]);
            if (id >= 0 && id < fm6::kSettings + 6) ++seen[id];
        }
    for (int id = 0; id < fm6::kSettings + 6; ++id) CHECK(seen[id] == (id == fm6::OpsOn ? 0 : 1));
    CHECK(spec.itemCount(0) == 0 && spec.itemCount(9) == 0 && spec.itemCount(2) == 22 && spec.itemCount(1) == 11 && spec.itemCount(8) == 8);
    // A page's ON is a bit, set and read by the helpers alone.
    int whole = -1, bit = -1;
    CHECK(spec.bitOf(fm6::kSettings, whole, bit) && whole == fm6::OpsOn && bit == 5);
    CHECK(spec.bitOf(fm6::kSettings + 5, whole, bit) && bit == 0);
    CHECK(!spec.bitOf(fm6::OpsOn, whole, bit) && !spec.bitOf(fm6::kSettings + 6, whole, bit) && !spec.bitOf(0, whole, bit));
    CHECK(spec.settingMin(fm6::kSettings + 2) == 0 && spec.settingMax(fm6::kSettings + 2) == 1 && spec.settingMax(fm6::kSettings + 6) == 0);
    int realId = -1, realValue = -1;
    settingWrite(spec, p.value, fm6::kSettings + 3, 1, realId, realValue);
    CHECK(realId == fm6::OpsOn && realValue == 63);
    settingWrite(spec, p.value, fm6::kSettings + 3, 7, realId, realValue); // held to 1 first
    CHECK(realId == fm6::OpsOn && realValue == 63);
    settingWrite(spec, p.value, fm6::kSettings, 0, realId, realValue);
    CHECK(realValue == 63 - 4 - 32);
    settingWrite(spec, p.value, fm6::Algorithm, 99, realId, realValue);
    CHECK(realId == fm6::Algorithm && realValue == 31);
    // The Session takes an ON too (the WiFi page sets it by its id).
    session.setSynthSetting(1, fm6::kSettings + 5, 0);
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 16 - 1);
    session.setSynthSetting(1, fm6::kSettings + 6, 0); // past the six: nothing
    CHECK(track.fm6.value[fm6::OpsOn] == 63 - 16 - 1);
    // ...and tells the engine OPERATORS ON whole: all six off through the Session, a key is silence;
    // one back on, it sounds.
    for (int op = 0; op < 6; ++op) session.setSynthSetting(1, fm6::kSettings + op, 0);
    CHECK(track.fm6.value[fm6::OpsOn] == 0);
    rig.render(96000);
    {
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 60;
        c.value = 0.8f;
        rig.engine->send(c);
        CHECK(rig.listen(4000) == 0.f);
        c.type = Command::NoteOff;
        rig.engine->send(c);
        rig.render(96000);
        session.setSynthSetting(1, fm6::kSettings, 1); // OP1, a carrier in every algorithm
        c.type = Command::NoteOn;
        rig.engine->send(c);
        CHECK(rig.listen(4000) > 0.01f);
        c.type = Command::NoteOff;
        rig.engine->send(c);
        rig.render(96000);
    }
    // A song keeps an FM-6 track: its kind and every number of its sound.
    track.fm6.value[fm6::Algorithm] = 21;
    track.macro[fm6::MacroDecay] = 0.9f;
    addPhazed(rig);
    std::unique_ptr<Project> again(new Project);
    const std::string written = songText(rig.project());
    CHECK(written.find("\ntrack fm6\n") != std::string::npos);
    CHECK(parseSong(written, *again));
    CHECK(again->tracks[1].kind == TrackKind::Fm6 && std::equal(track.fm6.value, track.fm6.value + fm6::kSettings, again->tracks[1].fm6.value));
    CHECK(near(again->tracks[1].macro[fm6::MacroDecay], 0.9f) && again->tracks[2].kind == TrackKind::Phazed);
}

// A song written with QMOD (the synth that went, 2026-11-15) opens with an FM-6 track in its place: on
// FM-6's first preset, its knobs in the middle, none of QMOD's sound read (its preset, macros, settings,
// the macros' automation, a MOD on a macro), and everything else as it was.
void testAQmodSongOpensOnFm6() {
    const std::string song =
        "apotheca-song 1\ntempo 120\nscenes 1\n"
        "track drums\n"
        "track qmod\nname LEAD\npreset 5\nmacros 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.9\nsettings 4321 7 8 9\n"
        "auto 1 3 0 0.9 1 0.8\nauto 1 20 0 0.6\nsend 1 0.4\nmod 1 1 6 0.75\nmod 2 1 20 0.75\nmuted\n"
        "scene 1\nnote 1 60 2 0.8\n"
        "track phazed\npreset 2\n";
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(song, *back));
    const TrackData& t = back->tracks[1];
    const Fm6Patch first = Fm6::presetPatch(0);
    CHECK(back->trackCount() == 3 && t.kind == TrackKind::Fm6 && back->tracks[2].kind == TrackKind::Phazed);
    CHECK(t.preset == 0 && std::equal(first.value, first.value + fm6::kSettings, t.fm6.value));
    for (float m : t.macro) CHECK(m == 0.5f);
    CHECK(t.automation[0][3][0] == kAutoNone && near(t.automation[0][20][0], 0.6f)); // a macro's lane goes, a send's stays
    CHECK(t.mods[0].kind == kModOff && t.mods[1].kind == kModLfo && t.mods[1].target == 20); // the same
    CHECK(near(t.send[0], 0.4f) && std::string(t.name) == "LEAD" && t.muted && t.notes[0][0].at(60, 0) >= 0);
    CHECK(back->tracks[2].preset == 1); // (the next track's own sound is read again)
    // And it is written back as FM-6's own word.
    const std::string again = songText(*back);
    CHECK(again.find("track fm6") != std::string::npos && again.find("qmod") == std::string::npos);
}

// SET acts on A's let go on BOUNCE and TRACK NAME, and only if nothing else went down while A was held
// (G15: hold A + RIGHT, the habit of every other screen, started a bounce on the press). Hold B a second
// on a bounce under way stops it, nothing kept.
void testSetActsOnTheLetGo() {
    const std::string folder = tempFolder("setbounce");
    const auto wavs = [&] { return filesEnding(folder, ".wav").size() + filesEnding(folder, ".mid").size(); };
    Rig rig;
    Session& session = rig.app.session();
    session.useBounceFolder(folder);
    // A long song: 24 scenes of a 16-times bar at the slowest tempo, a kick on each beat (about 38 minutes).
    for (int step = 0; step < kSteps; step += 4) rig.app.drums().setStep(0, step, 1.f);
    rig.project().repeats[0] = kMaxRepeats;
    for (int i = 0; i < 23; ++i) CHECK(session.nextScene(i) == i + 1);
    for (int i = 0; i < 24; ++i) rig.project().repeats[i] = kMaxRepeats;
    session.setTempo(kMinTempo);
    session.view = View::Settings;
    SettingsScreen& setup = rig.app.settings();
    setup.goTo(SettingsScreen::kBounce);
    rig.frames(2);
    // Hold A + RIGHT: RIGHT picks EACH TRACK, and the let go starts nothing.
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.frames(3);
    rig.key(A, false);
    rig.frames(2);
    CHECK(!session.bouncing() && wavs() == 0);
    // Hold A and the stick sideways: the same.
    rig.key(A, true);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    rig.key(A, false);
    rig.frames(2);
    CHECK(!session.bouncing() && wavs() == 0);
    // Hold A, any other button (B, X): no tap either.
    rig.key(A, true);
    rig.tap(X);
    rig.key(A, false);
    CHECK(!session.bouncing());
    // A down here, carried to another row and let go there: nothing.
    rig.key(A, true);
    setup.goTo(SettingsScreen::kTrackName);
    rig.key(A, false);
    CHECK(!session.bouncing() && !setup.typing());
    setup.goTo(SettingsScreen::kBounce);
    // A tapped: the song, on the let go.
    rig.key(A, true);
    rig.frames(3);
    CHECK(!session.bouncing());
    rig.key(A, false);
    CHECK(session.bouncing());
    // Hold B a second: stopped, nothing kept. Most of a second, or another button with it, is not enough.
    rig.key(B, true);
    rig.frames(50);
    CHECK(session.bouncing() && setup.stopPart() > 0.7f);
    rig.key(B, false);
    rig.frames(1);
    CHECK(session.bouncing() && setup.stopPart() == 0.f);
    rig.key(B, true);
    rig.frames(30);
    rig.tap(X); // any other press calls it off
    rig.frames(40);
    CHECK(session.bouncing() && setup.stopPart() == 0.f);
    rig.key(B, false);
    rig.key(B, true);
    rig.frames(30);
    rig.tap(DOWN); // ...and the cursor off the row
    rig.frames(40);
    CHECK(session.bouncing() && setup.stopPart() == 0.f && setup.cursorRow() == SettingsScreen::kClick);
    rig.key(B, false);
    setup.goTo(SettingsScreen::kBounce);
    {
        Canvas canvas; // its hint says how
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
    }
    rig.key(B, true);
    rig.frames(61);
    CHECK(session.bouncing());
    rig.frames(4);
    // Told to stop and not waited for (review of G15: the screen joined the bounce's thread, and froze while it
    // loaded the song); it stops within a block, nothing kept.
    CHECK(session.bounceStopping() || !session.bouncing());
    CHECK(std::string(rig.app.undoSaid()) == SettingsScreen::kBounceStoppedWords);
    session.finishBounce();
    CHECK(!session.bouncing() && !session.bounceStopping() && session.bounceFiles() == 0 && wavs() == 0 && filesEnding(folder, ".wav.part").empty());
    rig.key(B, false);
    rig.frames(2);
    // Until it has stopped it is still under way (pretended here: no thread, so it never ends by itself). A starts
    // nothing, B has nothing more to stop, and closing Apotheca (cancelBounce) waits for it and ends it.
    session.pretendBounce(0.37f, -1.f);
    rig.key(B, true);
    rig.frames(65);
    CHECK(session.bouncing() && session.bounceStopping() && setup.stopPart() == 0.f);
    rig.key(B, false);
    rig.frames(1);
    rig.key(B, true);
    rig.frames(30);
    CHECK(setup.stopPart() == 0.f && session.bounceStopping());
    rig.key(B, false);
    rig.tap(A);
    rig.frames(2);
    CHECK(session.bounceStopping() && wavs() == 0);
    session.cancelBounce();
    CHECK(!session.bouncing() && !session.bounceStopping() && session.bounceFiles() == 0);
    // B with no bounce under way does nothing.
    rig.tap(B);
    CHECK(!session.bouncing() && rig.view() == View::Settings);

    // TRACK NAME: hold A + RIGHT is the next name from the list, and no keys.
    setup.goTo(SettingsScreen::kTrackName);
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(!setup.typing() && std::string(rig.project().tracks[0].name) == kTrackNames[1]);
    rig.key(A, true);
    CHECK(!setup.typing()); // (not on the press)
    rig.key(A, false);
    CHECK(setup.typing() && setup.typed() == kTrackNames[1]);
    rig.tap(B);
    CHECK(!setup.typing());
    // TEMPO keeps its press: tap tempo is A in time.
    setup.goTo(SettingsScreen::kTempo);
    rig.frames(120);
    rig.tap(A);
    rig.frames(30);
    rig.key(A, true);
    CHECK(std::string(rig.app.undoSaid()).find("TAPPED") == 0); // the second tap's tempo, on its press
    rig.key(A, false);
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
    // Y deletes the last one (owner, 2026-11-15; it was B).
    rig.tap(Y, 2);
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
    // B leaves, keeping it (owner, 2026-11-15: B is what a player presses to back out).
    rig.tap(B);
    CHECK(!setup.typing() && std::string(name) == "ABJJJJJJJJ");
    char shown[24];
    ui::trackName(rig.project(), 0, shown, sizeof shown);
    CHECK(std::string(shown) == "1 ABJJJJJJJJ");
    // Opened again it starts from the name there is. START keeps too (and does not play).
    rig.tap(A);
    CHECK(setup.typed() == "ABJJJJJJJJ");
    rig.tap(Y, 8);
    rig.tap(START);
    CHECK(!setup.typing() && std::string(name) == "AB" && !rig.project().playing);
    // B with nothing typed leaves, and the name stays as it was. Y with nothing typed does nothing.
    rig.tap(A);
    rig.tap(Y, 3);
    CHECK(setup.typing() && setup.typed().empty());
    rig.tap(B);
    CHECK(!setup.typing() && std::string(name) == "AB");
    // Emptied and kept with START: the instrument's own name again. A space cannot come first.
    rig.tap(A);
    rig.tap(Y, 2);
    rig.tap(UP); // the bottom row
    rig.tap(LEFT); // its last key: space
    rig.tap(A);
    CHECK(setup.typed().empty());
    rig.tap(START);
    CHECK(name[0] == 0 && !setup.typing() && !rig.project().playing);
    // The stick moves over the keys too; left and right on the row still pick from the list when the keys are closed.
    rig.tap(A);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.tap(A);
    CHECK(setup.typed() == "B");
    rig.tap(Y);
    rig.tap(B);
    CHECK(!setup.typing() && name[0] == 0);
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
    // L1 held while carrying: an octave at a time, up and down.
    notes.moveCursorTo(61, 5);
    rig.key(R1, true);
    CHECK(notes.carrying());
    const int octaves = rig.app.session().octavesShown();
    Canvas carried;
    CHECK(carried.ok);
    rig.key(L1, true);
    if (carried.ok) {
        rig.frames(1);
        carried.clear();
        rig.app.draw(carried.r, *carried.fonts);
        CHECK(ui::gHintsOver == 0.f); // the octave hint fits the bar
    }
    rig.tap(RIGHT);
    CHECK(bar.notes[slot].pitch == 73 && notes.cursorPitch() == 73);
    rig.tap(LEFT);
    CHECK(bar.notes[slot].pitch == 61);
    rig.key(L1, false);
    rig.key(R1, false);
    CHECK(rig.app.session().octavesShown() == octaves); // L1 was a carry, not a zoom: nothing zoomed
    // ...and a chord goes by the octave too.
    bar.chords[0] = makeChord(builtInPack().chords[0], 48, 10, 3.f, 0.7f, 0);
    int keys[kChordNotes];
    bar.chords[0].pitches(keys);
    notes.moveCursorTo(keys[0], 10);
    rig.key(R1, true);
    CHECK(notes.carrying());
    rig.key(L1, true);
    rig.tap(RIGHT);
    CHECK(bar.chords[0].root == 60 && notes.cursorPitch() == keys[0] + 12);
    rig.tap(LEFT);
    CHECK(bar.chords[0].root == 48);
    rig.key(L1, false);
    rig.key(R1, false);
    CHECK(rig.app.session().octavesShown() == octaves);
    // L1 first or R1 first, it carries all the same (and a tap still copies the bar).
    rig.key(L1, true);
    rig.key(R1, true);
    CHECK(notes.carrying());
    rig.tap(LEFT);
    CHECK(bar.chords[0].root == 36 && notes.cursorPitch() == keys[0] - 12);
    rig.key(R1, false);
    rig.key(L1, false);
    const unsigned barCopies = rig.app.session().barCopies();
    bar.chords[0] = ChordEvent(); // put away: later checks want no chords
    notes.moveCursorTo(61, 5);
    rig.key(L1, true);
    rig.tap(R1);
    rig.key(L1, false);
    CHECK(rig.app.session().barCopies() == barCopies + 1); // ...unmoved: copied
    // The stick, up and down, slides it FREELY in time, off the grid: a short push is a small move...
    // (The engine is run as the frames go by, as the sound card runs it: each frame of sliding tells it the bar again.)
    const auto hold = [&](int count) { for (int f = 0; f < count; ++f) { rig.frames(1); rig.render(16); } };
    // (At twice the held D-pad's pace, 66.7 steps a second at STICK SPEED 100%: here at 25%, 0.27 of a step a frame.)
    notes.moveCursorTo(61, 5);
    ui::gStickSpeed = 0.25f;
    rig.key(R1, true);
    rig.key(STICK_UP, true);
    hold(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(bar.notes[slot].step == 5 && near(bar.notes[slot].nudge, -4.f / 15.f) && notes.cursorStep() == 5);
    // ...and a longer one carries it on through the steps, the cursor with it, still between the lines.
    rig.key(STICK_DOWN, true);
    hold(12); // 3.2 steps: from 4.73 to 7.93
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    ui::gStickSpeed = 1.f;
    CHECK(bar.notes[slot].step == 8 && notes.cursorStep() == 8 && near(bar.notes[slot].nudge, -1.f / 15.f));
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

// settings.txt and volume.txt (G43): never written for a value that is what the card has (a held RIGHT
// at an end of THEME wrote a file and two fsyncs nine times a second), and otherwise once what they
// would say has stood still a second, and on the way out (keepSettingsNow; the Session's end too).
void testSettingsWaitToStandStill() {
    const std::string folder = tempFolder("keep");
    const std::string settings = folder + "/settings.txt", volume = folder + "/volume.txt";
    const auto there = [](const std::string& path) { struct stat st; return stat(path.c_str(), &st) == 0; };
    std::string kept;
    {
        Rig rig;
        Session& session = rig.app.session();
        session.useProgramFolder(folder);
        session.loadVolume(0.4f);
        session.loadSettings();
        rig.frames(2);
        // What it is already: nothing written, however often.
        for (int i = 0; i < 20; ++i) {
            session.setTheme(session.theme());
            session.setVolume(0.4f);
            CHECK(!session.settingsOwed()); // (nothing owed: no file, and no waiting either)
            rig.frames(7);
        }
        rig.frames(100);
        CHECK(!there(settings) && !there(volume) && !session.settingsOwed());
        // A held run (THEME, nine a second): nothing on the card until it has stood still a second.
        session.view = View::Settings;
        rig.app.settings().goTo(SettingsScreen::kTheme);
        rig.key(RIGHT, true);
        rig.frames(60); // (about five steps, and on to the end)
        CHECK(!there(settings) && session.settingsOwed());
        rig.key(RIGHT, false);
        // The last repeat (at the end, still a change asked for) was due 200 + 6 x 110 ms after the first frame,
        // and came on the frame 880 ms after the press: a second after it is 920 ms after the let go.
        rig.frames(57);
        CHECK(!there(settings));
        rig.frames(8);
        CHECK(readTextFile(settings, kept) && kept.find("theme " + std::to_string(session.theme()) + "\n") != std::string::npos && !session.settingsOwed());
        // Held on at the end: what the card has already. Nothing more is written.
        std::remove(settings.c_str());
        rig.key(RIGHT, true);
        rig.frames(60);
        rig.key(RIGHT, false);
        rig.frames(100);
        CHECK(!there(settings) && !session.settingsOwed());
        // Changed, and back again before it was written: nothing.
        session.setTheme(0);
        rig.frames(10);
        session.setTheme(Session::kThemeCount - 1);
        rig.frames(100);
        CHECK(!there(settings));
        // The volume the same way (SEL + R1, louder).
        rig.key(SELECT, true);
        rig.tap(R1);
        rig.tap(R1);
        rig.key(SELECT, false);
        rig.frames(30);
        CHECK(!there(volume) && near(session.volume(), 0.6f));
        rig.frames(40);
        CHECK(readTextFile(volume, kept) && kept == "60\n");
        std::remove(volume.c_str());
        rig.key(SELECT, true);
        rig.tap(R1);
        rig.tap(L1); // (back to 60: what the card had)
        rig.key(SELECT, false);
        rig.frames(100);
        CHECK(!there(volume));
        // On the way out: written at once (main.cpp), and by the Session's end too.
        session.setVolume(0.9f);
        session.keepSettingsNow();
        CHECK(readTextFile(volume, kept) && kept == "90\n");
        session.setOctavesShown(3);
        CHECK(!there(settings));
    }
    CHECK(readTextFile(settings, kept) && kept.find("octaves 3\n") != std::string::npos);
}

void testTheSetupScreen() {
    const std::string folder = tempFolder("setup");
    const float stickBefore = ui::gStickSpeed;
    {
        Rig rig;
        Session& session = rig.app.session();
        session.useProgramFolder(folder);
        session.loadSettings(); // nothing kept yet: as built
        CHECK(session.octavesShown() == 2 && near(session.stickSpeed(), stickBefore));
        // The last tab, one past MOD; R2 there goes round to SONG (G35), L2 back.
        session.view = View::Mods;
        rig.tap(R2);
        CHECK(rig.view() == View::Settings);
        rig.tap(R2);
        CHECK(rig.view() == View::Song);
        rig.tap(L2);
        CHECK(rig.view() == View::Settings);
        SettingsScreen& setup = rig.app.settings();
        CHECK(setup.cursorRow() == SettingsScreen::kTempo);
        rig.tap(UP);
        CHECK(setup.cursorRow() == SettingsScreen::kControls); // a press at an end goes round (G35)...
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kTempo); // ...both ways
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
        // KEY and SCALE (easywins batch 4: easy_test scale has the rest)
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kKey);
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kScale && rig.project().scaleKind == kScaleOff);
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
        setup.goTo(SettingsScreen::kVolume); // (the rows are under headings now: gone to by name)
        CHECK(setup.cursorRow() == SettingsScreen::kVolume);
        rig.tap(LEFT, 4);
        CHECK(near(session.volume(), 0.8f));
        rig.tap(RIGHT);
        CHECK(near(session.volume(), 0.85f));
        // STICK SPEED, a twentieth at a time, between a quarter and one and a half
        setup.goTo(SettingsScreen::kStick); // (the rows are under headings now: gone to by name)
        ui::gStickSpeed = 0.75f;
        rig.tap(RIGHT, 2);
        CHECK(near(session.stickSpeed(), 0.85f) && near(ui::gStickSpeed, 0.85f));
        rig.tap(LEFT, 30);
        CHECK(near(session.stickSpeed(), 0.25f));
        rig.tap(RIGHT, 40);
        CHECK(near(session.stickSpeed(), 1.5f));
        rig.tap(LEFT, 14);
        CHECK(near(session.stickSpeed(), 0.8f));
        // TEXT SIZE: four of them, a click a push, with ends (tests/textsize_test.cpp has the rest)
        setup.goTo(SettingsScreen::kTextSize); // (the rows are under headings now: gone to by name)
        CHECK(setup.cursorRow() == SettingsScreen::kTextSize && session.textSize() == Session::kTextSizeAsBuilt);
        rig.tap(LEFT);
        CHECK(session.textSize() == 1);
        // THEME: six of them, a click a push (tests/theme_test.cpp has the rest)
        setup.goTo(SettingsScreen::kTheme); // (the rows are under headings now: gone to by name)
        CHECK(setup.cursorRow() == SettingsScreen::kTheme && session.theme() == Session::kThemeAsBuilt);
        rig.tap(RIGHT);
        CHECK(session.theme() == 1);
        rig.tap(LEFT);
        CHECK(session.theme() == 0);
        // PIANO ROLL SHOWS: 1, 2 or 3
        setup.goTo(SettingsScreen::kOctaves); // (the rows are under headings now: gone to by name)
        rig.tap(RIGHT, 5);
        CHECK(session.octavesShown() == 3);
        rig.tap(LEFT, 5);
        CHECK(session.octavesShown() == 1);
        // USB KEYBOARD: play only, or play and record
        setup.goTo(SettingsScreen::kMidi); // (the rows are under headings now: gone to by name)
        CHECK(setup.cursorRow() == SettingsScreen::kMidi && !session.midiRecording() && session.midiKeyboards() == 0);
        rig.tap(RIGHT);
        CHECK(session.midiRecording());
        rig.tap(RIGHT);
        CHECK(session.midiRecording());
        rig.tap(LEFT);
        CHECK(!session.midiRecording());
        rig.tap(RIGHT);
        // CLICK: OFF, ON, RECORDING, a click a push with ends; COUNT IN: OFF or ONE BAR (easywins batch 2)
        setup.goTo(SettingsScreen::kClick); // (the rows are under headings now: gone to by name)
        CHECK(setup.cursorRow() == SettingsScreen::kClick && session.click() == Session::kClickOff);
        rig.tap(RIGHT, 3);
        CHECK(session.click() == Session::kClickRecording);
        rig.tap(LEFT);
        CHECK(session.click() == Session::kClickOn);
        rig.tap(LEFT, 2);
        CHECK(session.click() == Session::kClickOff);
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == SettingsScreen::kCountIn && !session.countIn());
        rig.tap(RIGHT);
        CHECK(session.countIn());
        rig.tap(LEFT);
        CHECK(!session.countIn());
        // WIFI PAGE: A turns it on and off; left and right do nothing here.
        setup.goTo(SettingsScreen::kWifi); // (the rows are under headings now: gone to by name)
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
        CHECK(readTextFile(folder + "/settings.txt", kept) && kept == "stick 80\noctaves 1\nmidirec 1\ntext 1\ntheme 0\nbrightness 70\nlight 1\nclick 0\ncountin 0\n");
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

// L1's tap zooms the roll on its let go, and only if nothing else was pressed while it was held, as
// the drum grid's slices (review 2026-11-23: B, X, Y or START with L1 held still zoomed). On an empty
// place and on a note.
void testTheRollsZoomIsATapOnly() {
    Rig rig;
    addPhazed(rig);
    rig.project().track = 1;
    rig.app.session().view = View::Pattern;
    NoteScreen& notes = rig.app.notes();
    Session& session = rig.app.session();
    rig.project().tracks[1].notes[0][0].notes[0] = NoteEvent{4, 64, 2.f, 0.f, 0.8f};
    for (const int onNote : {0, 1}) {
        notes.moveCursorTo(onNote ? 64 : 70, onNote ? 4 : 1);
        for (const SDL_Keycode other : {B, X, Y, START}) {
            const int before = session.octavesShown();
            rig.key(L1, true);
            rig.tap(other);
            if (other == START) rig.tap(START); // (playing: stopped again)
            rig.key(L1, false);
            rig.frames(2);
            if (session.octavesShown() != before) std::printf("  L1 with key %d pressed zoomed (%s)\n", static_cast<int>(other), onNote ? "on a note" : "empty");
            CHECK(session.octavesShown() == before);
        }
    }
    const int before = session.octavesShown();
    rig.tap(L1);
    rig.frames(1);
    CHECK(session.octavesShown() != before); // (a tap alone still zooms)
}

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
    // BARS, up from the top step: hold A with up and down: the cell's bars, more or fewer,
    // stopping at the ends.
    rig.tap(UP);
    CHECK(notes.cursorOnBars());
    rig.key(A, true);
    rig.tap(UP);
    CHECK(rig.project().tracks[1].bars[0] == 2);
    rig.tap(UP);
    CHECK(rig.project().tracks[1].bars[0] == 4 && notes.cursorOnBars());
    rig.tap(DOWN, 2);
    CHECK(rig.project().tracks[1].bars[0] == 1);
    rig.tap(DOWN);
    CHECK(rig.project().tracks[1].bars[0] == 1);
    rig.key(A, false);
    CHECK(session.octavesShown() == 2); // (L1 was never held: no zoom)
    // Left and right on BARS: the bar before or after, round the ends.
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].bars[0] == 4 && rig.project().bar == 0);
    rig.tap(LEFT);
    CHECK(notes.cursorOnBars() && rig.project().bar == 3);
    rig.tap(RIGHT, 2);
    CHECK(notes.cursorOnBars() && rig.project().bar == 1);
    // The stick with A held on BARS: one step of bars for each push.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(40);
    CHECK(rig.project().tracks[1].bars[0] == 8 && notes.cursorOnBars() && notes.cursorPitch() == 69 && notes.cursorStep() == 0);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(2);
    rig.key(STICK_UP, false);
    rig.key(A, false);
    CHECK(rig.project().tracks[1].bars[0] == 8); // (at the end: stops)
    session.setOctavesShown(1); // the rest zooms from one octave, as it did
    rig.frames(1);
    // The cursor still scrolls the keys, one octave wide.
    rig.frames(1);
    notes.moveCursorTo(72, 0);
    CHECK(notes.lowestKeyShown() == 61);
    // At the top of the keyboard the keys stop at the last one.
    notes.moveCursorTo(108, 0);
    rig.tap(L1); // one octave to three: a tap is the only zoom now
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

// B on a setting's row on SET gives its usual value (gesture audit G17, 2026-11-25: B did nothing there), as B
// does on an effect's dial or a MOD. The SONG rows are the song's: B leaves them (BOUNCE's B is its stop). The bar
// says "B default" on a setting's row, and fits at XL.
void testBOnASettingIsItsUsual() {
    const float stickWas = ui::gStickSpeed;
    ui::gStickSpeed = ui::kStickSpeedAsBuilt; // (the tests before set it as they need)
    Rig rig;
    Session& session = rig.app.session();
    SettingsScreen& set = rig.app.settings();
    session.view = View::Settings;
    rig.frames(1);
    struct Row { int row; std::function<int()> now; int usual; };
    const std::vector<Row> rows = {
        {SettingsScreen::kClick, [&] { return session.click(); }, Session::kClickOff},
        {SettingsScreen::kCountIn, [&] { return session.countIn() ? 1 : 0; }, 0},
        {SettingsScreen::kMutes, [&] { return session.mutes(); }, Session::kMutesAtOnce},
        {SettingsScreen::kMidi, [&] { return session.midiRecording() ? 1 : 0; }, 0},
        {SettingsScreen::kTextSize, [&] { return session.textSize(); }, Session::kTextSizeAsBuilt},
        {SettingsScreen::kTheme, [&] { return session.theme(); }, Session::kThemeAsBuilt},
        {SettingsScreen::kOctaves, [&] { return session.octavesShown(); }, Session::kOctavesAsBuilt},
        {SettingsScreen::kVolume, [&] { return static_cast<int>(std::lround(session.volume() * 100.f)); }, 100},
        {SettingsScreen::kBrightness, [&] { return session.brightness(); }, kBrightUsual},
        {SettingsScreen::kLight, [&] { return session.stickLight(); }, kRingOn},
        {SettingsScreen::kLightColour, [&] { return session.lightColour(); }, kLightTheme},
        {SettingsScreen::kRingLevel, [&] { return session.ringLevel(); }, kBrightUsual},
        {SettingsScreen::kStick, [&] { return static_cast<int>(std::lround(session.stickSpeed() * 100.f)); }, 75},
    };
    int tried = 0;
    for (const Row& r : rows) {
        if (!set.here(r.row)) continue; // (the lights' rows only where the handheld has them)
        ++tried;
        set.goTo(r.row);
        rig.frames(1);
        CHECK(r.now() == r.usual);
        rig.tap(RIGHT, 2);
        if (r.now() == r.usual) rig.tap(LEFT, 4); // (at the top end already: the other way)
        CHECK(r.now() != r.usual);
        rig.tap(B);
        CHECK(r.now() == r.usual);
        if (r.now() != r.usual) std::printf("    row %d: B left %d, its usual is %d\n", r.row, r.now(), r.usual);
        CHECK(set.cursorRow() == r.row); // (B moves nothing)
    }
    CHECK(tried >= 9);
    // The SONG rows keep what they are.
    set.goTo(SettingsScreen::kTempo);
    rig.tap(RIGHT, 3);
    const float tempo = rig.project().tempo;
    rig.tap(B);
    CHECK(rig.project().tempo == tempo && tempo != 120.f);
    set.goTo(SettingsScreen::kScale);
    rig.tap(RIGHT);
    const int scale = rig.project().scaleKind;
    rig.tap(B);
    CHECK(rig.project().scaleKind == scale && scale != 0);
    // The bar: "B default" on a setting's row (fitting at XL), not on a song's.
    Canvas canvas, scratch;
    if (canvas.ok && scratch.ok) {
        set.goTo(SettingsScreen::kVolume);
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(barSays(canvas, scratch, "default"));
        const std::vector<unsigned char> ttf = loadFontFile();
        Fonts xl;
        xl.fixedSet = canvas.fonts.get();
        CHECK(xl.load(canvas.r, ttf, kTextXl));
        for (int row = SettingsScreen::kClick; row < SettingsScreen::kWifi; ++row) {
            if (!set.here(row)) continue;
            set.goTo(row);
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, xl);
            CHECK(ui::gHintsOver == 0.f);
        }
        set.goTo(SettingsScreen::kTempo);
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(!barSays(canvas, scratch, "default"));
    }
    ui::gStickSpeed = stickWas;
}

void testTheStickSpeedAndTheVolume() {
    // ---- one number slows everything the stick does -----------------------------------
    const auto travel = [](float speed) {
        ui::gStickSpeed = speed;
        Rig rig;
        rig.app.session().view = View::Song;
        rig.key(STICK_DOWN, true);
        rig.frames(24); // the first move is at once; the rest at the stick's speed
        rig.key(STICK_DOWN, false);
        ui::gStickSpeed = 1.f;
        return rig.app.song().cursorScene();
    };
    // (Twice the held D-pad's pace on a cursor, times STICK SPEED (owner, 2026-11-25): 1 at once, then 23 frames of 16 ms
    // at 66.7, 50 and 33.3 a second. It was 24 a second, a full push held climbing to three times it: G07.)
    const int full = travel(1.f), slower = travel(0.75f), half = travel(0.5f);
    CHECK(full == 25);
    CHECK(slower == 19); // a quarter off
    CHECK(half == 13);
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
    // An empty slot has no settings to go into.
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 0 && fx.settings(0) == 0);

    // Hold A: the list of effects is open. Down it: the effects in turn, each at its usual settings, heard at once.
    rig.render(100000);
    CHECK(!fx.picking());
    rig.key(A, true);
    CHECK(fx.picking());
    rig.tap(DOWN);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Filter) && fx.onLine(0).param[0] == effectInfo(EffectKind::Filter).defaults[0] && fx.onLine(0).param[3] == 1.f);
    CHECK(rig.listen(4000) > 0.05f);
    // (Counted from the list, which has its own order and grows: an effect's place is not its number. A line is a FAMILY.)
    const int last = familyListSize() - 1, delayAt = familyPlace(EffectKind::Delay);
    CHECK(familyPlace(EffectKind::Filter) == 1 && delayAt > 1 && familyAt(last) == EffectKind::Comp);
    rig.tap(DOWN, delayAt - 1);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay)); // the family goes in as its first type
    rig.tap(DOWN, last + 3);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Comp)); // the last one (LEVEL, as COMP): it stops
    rig.tap(UP, last - delayAt);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay));
    rig.tap(UP, delayAt);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::None)); // up past the first effect: empty
    rig.tap(UP, 2);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::None)); // and it stops there
    rig.tap(LEFT);
    rig.tap(RIGHT); // nothing has no TYPE
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::None) && fx.cursorColumn() == 0);
    rig.tap(DOWN, delayAt);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay));
    // Sideways with A held is the TYPE: the cursor stays on the line, and the ends stop.
    rig.tap(LEFT);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay) && fx.cursorColumn() == 0);
    rig.render(100000);
    rig.tap(RIGHT);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Tape) && fx.cursorColumn() == 0);
    CHECK(rig.listen(4000) > 0.05f); // heard at once, as a new effect is
    rig.tap(RIGHT, 6);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Stutter)); // the family's last type: it stops there
    rig.tap(LEFT, 4);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Tape));
    rig.tap(LEFT);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay));
    // Out of the family and back: it is tried afresh, at its usual settings. All the while the slot itself holds what it held.
    rig.tap(UP);
    rig.tap(DOWN);
    CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::Delay) && fx.onLine(0).param[0] == effectInfo(EffectKind::Delay).defaults[0]);
    CHECK(first.kind == static_cast<int>(EffectKind::None) && fx.trying());
    rig.key(A, false);
    CHECK(!fx.picking() && !fx.trying());
    CHECK(first.kind == static_cast<int>(EffectKind::Delay) && first.param[0] == effectInfo(EffectKind::Delay).defaults[0] && first.param[3] == effectInfo(EffectKind::Delay).defaults[3]); // let go: in

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

    // Right: into its settings. TIME is a choice: the D-pad steps to the next one.
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 1 && fx.cursorRow() == 0);
    rig.tap(RIGHT); // there is nothing further right
    CHECK(fx.cursorColumn() == 1);
    rig.key(A, true);
    CHECK(!fx.picking()); // on a setting A turns it: no list
    rig.tap(UP);
    CHECK(delaySteps(first.param[0]) == 6);
    rig.tap(UP, 5);
    CHECK(delaySteps(first.param[0]) == 8);
    rig.tap(DOWN, 9);
    CHECK(delaySteps(first.param[0]) == 1 && first.param[0] > 0.05f && first.param[0] < 0.12f); // the middle of its part of the range
    rig.key(A, false);
    // FEEDBACK, the next one down, is an amount: the D-pad moves it a twentieth, the stick turns it.
    rig.tap(DOWN);
    CHECK(fx.cursorColumn() == 2);
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
    CHECK(first.param[1] == 0.f && fx.cursorColumn() == 2);
    // B needs A let go. On a setting: its usual value. On the line: empty.
    rig.tap(B);
    CHECK(first.param[1] == 0.f);
    rig.key(A, false);
    rig.tap(B);
    CHECK(first.param[1] == effectInfo(EffectKind::Delay).defaults[1]);
    rig.tap(DOWN, 9);
    CHECK(fx.cursorColumn() == 4 && fx.cursorRow() == 0); // four settings, no more: the end stops
    rig.tap(UP, 9);
    CHECK(fx.cursorColumn() == 1 && fx.cursorRow() == 0); // and the top
    // Left: back to the list, on the same line. Right: back to the setting last used there.
    rig.tap(DOWN, 2);
    CHECK(fx.cursorColumn() == 3);
    rig.tap(LEFT);
    CHECK(fx.cursorColumn() == 0 && fx.cursorRow() == 0 && fx.remembered(0) == 3);
    rig.tap(LEFT); // nothing further left
    CHECK(fx.cursorColumn() == 0 && fx.cursorRow() == 0);
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 3);
    rig.tap(LEFT);

    // Down the list: the track's second and third, its sends, the buses and the master's two, and round.
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 1 && fx.cursorColumn() == 0);
    rig.key(A, true);
    rig.tap(DOWN, familyPlace(EffectKind::Reverb));
    rig.key(A, false);
    CHECK(rig.project().tracks[0].effects[1].kind == static_cast<int>(EffectKind::Reverb) && first.kind == static_cast<int>(EffectKind::Delay));
    rig.tap(RIGHT);
    rig.tap(DOWN, 9);
    CHECK(fx.cursorColumn() == 3 && fx.cursorRow() == 1); // the room reverb has three
    rig.tap(LEFT);
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 2 && fx.cursorColumn() == 0);
    rig.tap(DOWN, 4);
    CHECK(fx.cursorRow() == FxScreen::kMasterRow && fx.cursorColumn() == 0);
    rig.key(A, true);
    rig.tap(DOWN, last);
    rig.key(A, false);
    CHECK(rig.project().masterEffects[0].kind == static_cast<int>(EffectKind::Comp));
    rig.tap(DOWN, 2);
    CHECK(fx.cursorRow() == 0);
    rig.tap(UP);
    CHECK(fx.cursorRow() == FxScreen::kRows - 1);
    // The stick moves the cursor too: down the list, right into the settings, left back.
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 0);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 3);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 0);
    rig.key(B, true); // held: deleted when the bar is full
    rig.frames(70);
    rig.key(B, false);
    CHECK(first.kind == 0 && first.param[0] == 0.f);

    // The first lines are whichever track is being worked on; the master's stay.
    session.addTrack(TrackKind::Phazed);
    session.setEffect(0, 0, EffectKind::Delay);
    fx.goTo(0, 4);
    CHECK(fx.cursorColumn() == 4 && fx.remembered(0) == 4);
    rig.project().track = 1;
    rig.frames(1);
    CHECK(fx.cursorColumn() == 0 && fx.cursorRow() == 0); // the new track's slot is empty: there is no setting to be on. The line is kept
    session.setEffect(0, 0, EffectKind::None);
    rig.key(A, true);
    rig.tap(DOWN, familyPlace(EffectKind::Drive));
    rig.key(A, false);
    CHECK(rig.project().tracks[1].effects[0].kind == static_cast<int>(EffectKind::Drive) && first.kind == 0);
    CHECK(rig.project().masterEffects[0].kind == static_cast<int>(EffectKind::Comp));
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 4); // what was last used on the line is not forgotten by a change of track
    rig.tap(LEFT);
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
        CHECK(std::string(text) == "LOW 60");
        effectParamText(EffectKind::Filter, 0, 1.f, text, sizeof text, dj);
        CHECK(std::string(text) == "HIGH 8.0K");
        effectParamText(EffectKind::Filter, 0, 0.f, text, sizeof text, low); // any other type: the corner itself
        CHECK(std::string(text) == "30 HZ");
    }
    effectParamText(EffectKind::Filter, 2, 0.5f, text, sizeof text);
    CHECK(std::string(text) == "BAND PASS");
    effectParamText(EffectKind::Delay, 0, 0.4f, text, sizeof text);
    CHECK(std::string(text) == "1/8 DOT");
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
    // Every effect there is goes into a song file by its word and comes back, settings and all.
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        std::unique_ptr<Project> out(new Project), back(new Project);
        out->masterEffects[1].kind = k;
        out->tracks[0].effects[0].kind = k;
        for (int i = 0; i < kEffectParams; ++i) out->masterEffects[1].param[i] = out->tracks[0].effects[0].param[i] = i < effectInfo(kind).params ? 0.125f * (i + 1) : 0.f;
        const std::string text = songText(*out);
        CHECK(text.find(std::string("fx 2 ") + effectInfo(kind).word + " ") != std::string::npos && text.find(std::string("fx 1 ") + effectInfo(kind).word + " ") != std::string::npos);
        CHECK(parseSong(text, *back) && back->masterEffects[1].kind == k && back->tracks[0].effects[0].kind == k);
        for (int i = 0; i < effectInfo(kind).params; ++i) CHECK(back->masterEffects[1].param[i] == 0.125f * (i + 1) && back->tracks[0].effects[0].param[i] == 0.125f * (i + 1));
    }
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
        CHECK(kNewSongBus[0] == EffectKind::Tape && kNewSongBus[1] == EffectKind::Hall); // the plan: TAPE on ECHO, HALL on SPACE
        CHECK(text.find("\nbusfx 1 tape 0.5625 0.4 0.35 1\nbusfx 2 hall 0.6 0.45 0.5 1\n") != std::string::npos && text.find("send") == std::string::npos);
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
    // ECHO takes TAPE or DELAY, SPACE takes HALL or REVERB, and neither takes the other's.
    const int tape = static_cast<int>(EffectKind::Tape), hall = static_cast<int>(EffectKind::Hall);
    CHECK(session.effectData(kEchoBus, 0).kind == tape && session.effectData(kSpaceBus, 0).kind == hall);
    session.setEffect(kEchoBus, 0, EffectKind::Hall);
    session.setEffect(kSpaceBus, 0, EffectKind::Tape);
    session.setEffect(kEchoBus, 0, EffectKind::Chorus);
    session.setEffect(kSpaceBus, 0, EffectKind::Comp);
    CHECK(project.busEffects[0].kind == tape && project.busEffects[1].kind == hall);
    CHECK(project.busEffects[0].param[3] == 1.f && project.busEffects[1].param[3] == 1.f); // LEVEL at full
    session.setEffect(kEchoBus, 0, EffectKind::Delay);
    session.setEffect(kSpaceBus, 0, EffectKind::Reverb);
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

    // The smallest send a player can set by the stick, and smaller: saved as it is, not rounded away.
    for (float amount : {0.01f, 0.001f, 0.05f, 0.999f, 1.f}) {
        std::unique_ptr<Project> song(new Project), back(new Project);
        song->tracks[0].send[0] = amount;
        song->tracks[0].send[1] = amount * 0.5f;
        const std::string text = songText(*song);
        CHECK(text.find("\nsend 1 ") != std::string::npos && text.find("\nsend 2 ") != std::string::npos);
        CHECK(parseSong(text, *back));
        CHECK(std::fabs(back->tracks[0].send[0] - amount) < 1e-6f * amount + 1e-9f && std::fabs(back->tracks[0].send[1] - amount * 0.5f) < 1e-6f * amount + 1e-9f);
        CHECK(back->tracks[0].send[0] > 0.f && back->tracks[1].send[0] == 0.f);
    }
    // An old song: no busfx, no send. Its buses are the usual two and nothing is sent, and it is the
    // same to the bit as with no buses at all.
    {
        const std::string old = "apotheca-song 1\ntempo 120\nfx 1 drive 0.6 0.7 1 0.5\ntrack drums\nfx 1 delay 0.5 0.2 0.6 0.35\nfx 2 filter 0.4 0.2 0 1\n"
                                "scene 1\nhit 1 1 1\nhit 3 5 0.8\nhit 7 9 1\n";
        std::unique_ptr<Project> song(new Project);
        CHECK(parseSong(old, *song));
        CHECK(song->busEffects[0].kind == static_cast<int>(kNewSongBus[0]) && song->busEffects[1].kind == static_cast<int>(kNewSongBus[1]));
        CHECK(song->busEffects[0].kind == static_cast<int>(EffectKind::Tape) && song->busEffects[1].kind == static_cast<int>(EffectKind::Hall)); // as a new song: TAPE and HALL
        for (int i = 0; i < kEffectParams; ++i) {
            CHECK(song->busEffects[0].param[i] == effectDefault(EffectKind::Tape, i, true));
            CHECK(song->busEffects[1].param[i] == effectDefault(EffectKind::Hall, i, true));
        }
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
        // (None of those bus lines can be used: the buses keep what a new song has, TAPE and HALL, LEVEL full.)
        CHECK(song->busEffects[0].kind == static_cast<int>(EffectKind::Tape) && song->busEffects[0].param[0] == effectInfo(EffectKind::Tape).defaults[0]);
        CHECK(song->busEffects[0].param[3] == 1.f && song->busEffects[1].param[3] == 1.f);
        CHECK(song->busEffects[1].kind == static_cast<int>(EffectKind::Hall) && song->busEffects[1].param[0] == effectInfo(EffectKind::Hall).defaults[0]);
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


// EVERY SETTING HAS AN ICON (owner, 2026-11-24): every synth's macros and its pages' settings, and every effect's
// settings (and LEVEL, a bus's MIX), take a drawing by the rules in icons.cpp. A new name no rule takes fails here.
void testEverySettingHasAnIcon() {
    int names = 0;
    const auto check = [&](const char* name, const char* where) {
        ++names;
        CHECK(iconFor(name) != nullptr);
        if (!iconFor(name)) std::printf("    %s: [%s] has no icon (icons.cpp: a rule or a drawing)\n", where, name);
    };
    for (int k = 0; k < kInstrumentCount; ++k) {
        const SynthSpec* spec = specFor(kInstruments[k].kind);
        if (!spec) continue;
        for (int m = 0; m < 8; ++m) check(spec->macroNames[m], spec->name);
        for (int page = 1; page < spec->pages; ++page)
            for (int i = 0; i < spec->itemCount(page); ++i) {
                int id = 0;
                const char* name = nullptr;
                spec->item(page, i, id, name);
                check(name, spec->name);
            }
    }
    for (int k = 1; k < kEffectKinds; ++k)
        for (int p = 0; p < effectInfo(static_cast<EffectKind>(k)).params; ++p) check(effectInfo(static_cast<EffectKind>(k)).paramNames[p], effectInfo(static_cast<EffectKind>(k)).name);
    check("LEVEL", "a bus");
    CHECK(names > 300);
    // The rules read a name's last word first: "LFO DEPTH" is a depth, "AMP DECAY" a decay, and digits are passed over.
    CHECK(iconFor("LFO DEPTH") == iconFor("DEPTH") && iconFor("AMP DECAY") == iconFor("DECAY") && iconFor("RATE 2") == iconFor("RATE"));
    CHECK(iconFor("CHORUS") != nullptr && iconFor("CHORUS") != iconFor("TONE") && iconFor("NOTHING LIKE IT") == nullptr);
}

// The FX screen's picture (design B, 2026-11-24): every effect draws one, inside its box, in the theme's colours, and it
// follows the settings: for each effect at least one of its settings moved from its usual changes the picture.
void testTheFxPictureFollowsTheSettings() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    typedef FxScreen F;
    const auto picture = [&](EffectKind kind, const float* shown, float dB, const FxLive& live = FxLive()) {
        canvas.clear();
        drawEffectPicture(canvas.r, F::kPaneX, F::kPicY, F::kPaneW, F::kPicH, kind, shown, dB, live);
        return canvas.part(F::kPaneX, F::kPicY, F::kPaneW, F::kPicH);
    };
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        float usual[kEffectParams] = {0.f, 0.f, 0.f, 0.f};
        for (int p = 0; p < info.params; ++p) usual[p] = effectDefault(kind, p, false);
        const std::vector<Uint32> plain = picture(kind, usual, 0.f);
        // Something is drawn inside the box (more than its edge), and nothing outside it.
        int inked = 0;
        for (int y = F::kPicY + 2; y < F::kPicY + F::kPicH - 2; ++y)
            for (int x = F::kPaneX + 2; x < F::kPaneX + F::kPaneW - 2; ++x) inked += canvas.is(x, y, ui::kPanel) ? 0 : 1;
        CHECK(inked > 200);
        if (inked <= 200) std::printf("    %s draws %d pixels\n", info.name, inked);
        bool outside = false;
        for (int x = F::kPaneX; x < F::kPaneX + F::kPaneW; x += 2) outside = outside || !canvas.is(x, F::kPicY - 1, ui::kBg) || !canvas.is(x, F::kPicY + F::kPicH, ui::kBg);
        CHECK(!outside);
        // Only the theme's colours.
        bool theme = true;
        for (int y = F::kPicY; y < F::kPicY + F::kPicH; ++y)
            for (int x = F::kPaneX; x < F::kPaneX + F::kPaneW; ++x) {
                bool known = false;
                for (const SDL_Color* c : {&ui::kPanel, &ui::kPanel2, &ui::kLine, &ui::kText, &ui::kDim, &ui::kTeal, &ui::kBrass, &ui::kBg}) known = known || canvas.is(x, y, *c);
                theme = theme && known;
            }
        CHECK(theme);
        int moved = 0;
        for (int p = 0; p < info.params; ++p) {
            float other[kEffectParams];
            for (int i = 0; i < kEffectParams; ++i) other[i] = usual[i];
            other[p] = usual[p] < 0.5f ? 0.95f : 0.05f;
            moved += picture(kind, other, 0.f) != plain ? 1 : 0;
        }
        CHECK(moved >= (info.params > 1 ? 2 : 1));
        if (moved < 2) std::printf("    %s: %d of its %d settings move its picture\n", info.name, moved, info.params);
        // An effect with a meter shows it: more brass as it works harder.
        if (effectMeterParam(kind) >= 0) {
            const auto brass = [&](float dB) {
                picture(kind, usual, dB);
                int n = 0;
                for (int y = F::kPicY; y < F::kPicY + F::kPicH; ++y)
                    for (int x = F::kPaneX; x < F::kPaneX + F::kPaneW; ++x) n += canvas.is(x, y, ui::kBrass) ? 1 : 0;
                return n;
            };
            CHECK(brass(12.f) > brass(2.f) && brass(24.f) > brass(12.f));
        }
    }
    // PLAYING, it moves with the song (2026-11-24). Stopped: no purple line anywhere in it.
    const auto purpleAt = [&](int x) {
        for (int y = F::kPicY + 12; y < F::kPicY + F::kPicH - 12; ++y)
            if (canvas.is(x, y, ui::kPurple)) return true;
        return false;
    };
    const auto anyPurple = [&] {
        for (int x = F::kPaneX; x < F::kPaneX + F::kPaneW; ++x)
            if (purpleAt(x)) return true;
        return false;
    };
    const int plotX = F::kPaneX + 12, plotW = F::kPaneW - 24;
    for (EffectKind kind : {EffectKind::Tape, EffectKind::Chorus, EffectKind::Tremolo, EffectKind::Duck, EffectKind::Stutter}) {
        float usual[kEffectParams] = {0.f, 0.f, 0.f, 0.f};
        for (int p = 0; p < effectInfo(kind).params; ++p) usual[p] = effectDefault(kind, p, false);
        picture(kind, usual, 0.f);
        CHECK(!anyPurple());
        FxLive live;
        live.playing = true;
        live.steps = 8.0; // a quarter of the way across two bars (a DELAY's), half of one (DUCK's)
        live.level = 0.5f;
        picture(kind, usual, 0.f, live);
        CHECK(anyPurple());
        if (kind == EffectKind::Tape || kind == EffectKind::Stutter) CHECK(purpleAt(plotX + static_cast<int>(std::round((plotW - 1) * 0.25f))));
        if (kind == EffectKind::Duck) CHECK(purpleAt(plotX + static_cast<int>(std::round((plotW - 22 - 14 - 1) * 0.5f))));
        if (kind == EffectKind::Chorus || kind == EffectKind::Tremolo) { // a second later the line has moved on, and a brass dot rides with it
            const auto first = canvas.part(F::kPaneX, F::kPicY, F::kPaneW, F::kPicH);
            live.steps = 16.0;
            CHECK(picture(kind, usual, 0.f, live) != first);
        }
    }
    // The track's level is in a DRIVE's wave, on COMP's curve and in where WAH's corner is.
    for (EffectKind kind : {EffectKind::Drive, EffectKind::Comp, EffectKind::Wah}) {
        float usual[kEffectParams] = {0.f, 0.f, 0.f, 0.f};
        for (int p = 0; p < effectInfo(kind).params; ++p) usual[p] = effectDefault(kind, p, false);
        FxLive live;
        live.playing = true;
        live.level = 0.01f; // (at its usual SENSE, WAH is all the way open from about 0.1)
        const auto quiet = picture(kind, usual, 0.f, live);
        live.level = 0.9f;
        CHECK(picture(kind, usual, 0.f, live) != quiet);
    }
    // WAH's curve opens with the playing: loud, the area under it reaches far further up the notes than quiet.
    {
        const EffectKind wah = EffectKind::Wah;
        float usual[kEffectParams] = {0.f, 0.f, 0.f, 0.f};
        for (int p = 0; p < effectInfo(wah).params; ++p) usual[p] = effectDefault(wah, p, false);
        const auto under = [&](float level) {
            FxLive live;
            live.playing = true;
            live.level = level;
            picture(wah, usual, 0.f, live);
            int n = 0;
            for (int y = F::kPicY + 12; y < F::kPicY + F::kPicH - 12; ++y)
                for (int x = F::kPaneX + 12; x < F::kPaneX + F::kPaneW - 12; ++x) n += canvas.is(x, y, ui::kPanel2) || canvas.is(x, y, ui::kText) ? 1 : 0;
            return n;
        };
        const int shut = under(0.f), open = under(0.9f);
        CHECK(open > shut * 3 / 2);
        if (open <= shut * 3 / 2) std::printf("    WAH: %d pixels under its curve shut, %d open\n", shut, open);
    }
    // BAND draws its shape where FREQ puts it (2026-11-24): a BELL +12 dB peaks 12 dB over the 0 dB line at FREQ and is
    // on it far off; a NOTCH dips to the floor there; a LOW SHELF +12 dB is 12 dB up far under FREQ, on the line far over.
    {
        const int px = F::kPaneX + 12, pw = F::kPaneW - 24, py = F::kPicY + 12, ph = F::kPicH - 24;
        const auto yOfDb = [&](float db) { return py + ph - 1 - static_cast<int>(std::round((db + 30.f) / 48.f * static_cast<float>(ph - 1))); };
        const auto topAt = [&](float t) { // the curve's highest pixel in the column t of the way along (FREQ's brass mark sits on it)
            const int x = px + static_cast<int>(std::round(t * static_cast<float>(pw - 1)));
            for (int y = py; y < py + ph; ++y)
                for (int dx = 0; dx < 2; ++dx)
                    if (canvas.is(x + dx, y, ui::kText) || canvas.is(x + dx, y, ui::kBrass)) return y;
            return py + ph;
        };
        const auto bottomAt = [&](float t) { // ...and its lowest: a dip is drawn as a line down to it
            const int x = px + static_cast<int>(std::round(t * static_cast<float>(pw - 1)));
            for (int y = py + ph - 1; y >= py; --y)
                for (int dx = 0; dx < 2; ++dx)
                    if (canvas.is(x + dx, y, ui::kText) || canvas.is(x + dx, y, ui::kBrass)) return y;
            return py;
        };
        const auto drawn = [&](float db, int shape) {
            const float shown[kEffectParams] = {0.5f, (db / 12.f + 1.f) / 2.f, 0.6f, (static_cast<float>(shape) + 0.5f) / 4.f};
            picture(EffectKind::Band, shown, 0.f);
        };
        drawn(12.f, 0);
        const int bellPeak = topAt(0.5f), bellFar = topAt(0.05f);
        drawn(0.f, 1);
        const int notchHole = bottomAt(0.5f), notchFar = topAt(0.95f);
        drawn(12.f, 2);
        const int shelfLow = topAt(0.05f), shelfHigh = topAt(0.95f);
        const bool ok = std::abs(bellPeak - yOfDb(12.f)) <= 3 && std::abs(bellFar - yOfDb(0.f)) <= 2 && notchHole >= yOfDb(-25.f) &&
                        std::abs(notchFar - yOfDb(0.f)) <= 2 && std::abs(shelfLow - yOfDb(12.f)) <= 2 && std::abs(shelfHigh - yOfDb(0.f)) <= 2;
        CHECK(ok);
        if (!ok)
            std::printf("    BAND's picture: bell %d (want %d), far %d; notch %d (under %d), far %d; shelf %d / %d (0 dB at %d)\n", bellPeak, yOfDb(12.f), bellFar,
                        notchHole, yOfDb(-25.f), notchFar, shelfLow, shelfHigh, yOfDb(0.f));
    }
    // PITCH draws the shifted sound at its own speed (2026-11-24): along the middle of the waves, an octave up
    // crosses it twice as often as no shift, an octave down half as often. Under them the heads: at no shift one
    // stands still at full (its line along the top of the strip); otherwise one plays alone and the other takes over
    // in 5 ms (the matched splices, 2026-11-24: the top of the strip is always inked but at a crossing, and each head
    // is at full about half the time), an octave up twice as often as an octave down (a piece a GRAIN, against two).
    {
        const int px = F::kPaneX + 12, pw = F::kPaneW - 24, py = F::kPicY + 12, ph = F::kPicH - 24, wh = ph * 7 / 10;
        const int mid = py + wh - 1 - static_cast<int>(std::round(0.5f * static_cast<float>(wh - 1)));
        const int fy = py + ph * 4 / 5; // the strip's top row
        struct Read { int crossings, peaks, full; };
        const auto read = [&](int semis) {
            float shown[kEffectParams] = {(static_cast<float>(semis + 12) + 0.5f) / 25.f, 0.5f, 0.4f, 0.5f};
            picture(EffectKind::Pitch, shown, 0.f);
            Read got{0, 0, 0};
            bool in = false, top = false;
            for (int x = px; x < px + pw; ++x) {
                const bool t = canvas.is(x, mid, ui::kText), inked = canvas.is(x, fy, ui::kText) || canvas.is(x, fy, ui::kDim);
                got.crossings += t && !in ? 1 : 0;
                got.peaks += inked && !top ? 1 : 0;
                in = t, top = inked;
                got.full += canvas.is(x, fy, ui::kDim) ? 1 : 0;
            }
            return got;
        };
        const Read same = read(0), up = read(12), down = read(-12);
        const bool crossings = same.crossings >= 4 && same.crossings <= 6 && up.crossings >= 8 && up.crossings <= 10 && down.crossings >= 2 && down.crossings <= 4;
        const bool heads = same.full > pw * 9 / 10 && up.full > pw / 2 && up.full < pw * 2 / 3 && up.peaks == 5 && down.peaks == 3;
        CHECK(crossings && heads);
        if (!crossings || !heads)
            std::printf("    PITCH crosses %d / %d / %d times; the strip's top: %d / %d of %d inked, %d / %d peaks\n", same.crossings, up.crossings,
                        down.crossings, same.full, up.full, pw, up.peaks, down.peaks);
    }
    // EACH THEME'S STYLE: VINTAGE engraves a scale outside the ring, NEON lights a glow ring outside the lit arc, CLEAN neither.
    for (int theme : {static_cast<int>(kThemeBrown), static_cast<int>(kThemeDracula), static_cast<int>(kThemeNord), static_cast<int>(kThemeSolarized)}) {
        ui::useTheme(theme);
        canvas.clear();
        const int cx = 100, cy = 100;
        drawDial(canvas.r, cx, cy, F::kDialR, F::kDialRing, 0.6f, 0, 0, ui::kTeal, std::nanf(""));
        const SDL_Point scale = dialPoint(cx, cy, F::kDialR + 4.f, 0.f), glow = dialPoint(cx, cy, F::kDialR + 2.f, 0.3f), past = dialPoint(cx, cy, F::kDialR + 2.f, 0.8f);
        const FxStyle style = fxStyle();
        CHECK(style == (theme == kThemeBrown ? FxStyle::Vintage : theme == kThemeDracula ? FxStyle::Neon : FxStyle::Clean));
        CHECK(canvas.is(scale.x, scale.y, ui::kLine) == (style == FxStyle::Vintage));
        CHECK(canvas.is(glow.x, glow.y, ui::kTeal) == (style == FxStyle::Neon) && !canvas.is(past.x, past.y, ui::kTeal));
        const SDL_Point lit = dialPoint(cx, cy, F::kDialR - 2.f, 0.5f), unlit = dialPoint(cx, cy, F::kDialR - 2.f, 0.7f);
        CHECK(canvas.is(lit.x, lit.y, ui::kTeal) && canvas.is(unlit.x, unlit.y, ui::kLine)); // the ring reads the same in every style
    }
    ui::useTheme(kThemeBrown);
}

// COMP's meter on the FX screen: the row asks the engine for its own slot, and the strip's length.
void testTheFxScreenShowsCompAtWork() {
    CHECK(effectMeterParam(EffectKind::Comp) == 0 && std::string(effectInfo(EffectKind::Comp).paramNames[0]) == "THRESHOLD");
    for (int k = 0; k < kEffectKinds; ++k)
        if (static_cast<EffectKind>(k) != EffectKind::Comp && static_cast<EffectKind>(k) != EffectKind::Duck && static_cast<EffectKind>(k) != EffectKind::Limiter) CHECK(effectMeterParam(static_cast<EffectKind>(k)) == -1);
    CHECK(effectMeterParam(EffectKind::Limiter) == 0 && std::string(effectInfo(EffectKind::Limiter).paramNames[0]) == "GAIN"); // LIMITER: how far it is turning down, on GAIN
    CHECK(effectMeterParam(EffectKind::Duck) == 0 && std::string(effectInfo(EffectKind::Duck).paramNames[0]) == "AMOUNT"); // DUCK: how far it is dipping, on AMOUNT
    CHECK(kMeterFullDb == 24.f);
    CHECK(FxScreen::meterPixels(0.f, 108) == 0 && FxScreen::meterPixels(-3.f, 108) == 0 && FxScreen::meterPixels(std::nanf(""), 108) == 0);
    CHECK(FxScreen::meterPixels(24.f, 108) == 108 && FxScreen::meterPixels(60.f, 108) == 108 && FxScreen::meterPixels(1e30f, 108) == 108);
    CHECK(FxScreen::meterPixels(12.f, 108) == 54 && FxScreen::meterPixels(6.f, 108) == 27 && FxScreen::meterPixels(2.f, 108) == 9);
    CHECK(FxScreen::meterPixels(0.1f, 108) == 1 && FxScreen::meterPixels(0.05f, 108) == 0 && FxScreen::meterPixels(12.f, 0) == 0);

    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    const auto hit = [&] {
        Command c;
        c.type = Command::Trigger;
        c.track = 0;
        c.value = 1.f;
        rig.engine->send(c);
        rig.frames(1);
        rig.render(3000);
    };
    hit();
    for (int row = 0; row < FxScreen::kRows; ++row) CHECK(fx.meterOf(row) == 0.f); // no COMP anywhere: nothing to show
    CHECK(fx.meterOf(-1) == 0.f && fx.meterOf(FxScreen::kRows) == 0.f);
    // On the track's first slot, set to squash hard. (The first: the SENDS row is not a slot, and must not show slot 1's.)
    session.setEffect(0, 0, EffectKind::Comp);
    session.setEffectParam(0, 0, 0, 0.f);
    rig.frames(1);
    rig.render(2000);
    hit();
    const float track = fx.meterOf(0);
    CHECK(track > 3.f && track == rig.engine->effectMeter(0, 0));
    for (int row = 1; row < FxScreen::kRows; ++row) CHECK(fx.meterOf(row) == 0.f);
    // On the master's second slot too: each row shows its own.
    session.setEffect(kMasterTrack, 1, EffectKind::Comp);
    session.setEffectParam(kMasterTrack, 1, 0, 0.5f);
    rig.frames(1);
    rig.render(2000);
    hit();
    const float master = fx.meterOf(FxScreen::kMasterRow + 1);
    CHECK(master > 0.f && master == rig.engine->effectMeter(kMasterTrack, 1) && master != fx.meterOf(0));
    CHECK(fx.meterOf(0) > 3.f && fx.meterOf(FxScreen::kMasterRow) == 0.f && fx.meterOf(1) == 0.f && fx.meterOf(2) == 0.f);
    CHECK(fx.meterOf(FxScreen::kSendsRow) == 0.f && fx.meterOf(FxScreen::kBusRow) == 0.f && fx.meterOf(FxScreen::kBusRow + 1) == 0.f);
    // DRAWN, three ways: in words in the heading, as a strip under THRESHOLD's bar from the right, and as a mark on the list's line from the top.
    {
        Canvas canvas, scratch;
        CHECK(canvas.ok && scratch.ok);
        typedef FxScreen F;
        const Fonts& fonts = *canvas.fonts;
        const auto shown = [&](int row) {
            fx.goTo(row, 1);
            canvas.clear();
            fx.draw(canvas.r, fonts);
            const float dB = fx.meterOf(row);
            char words[32];
            FxScreen::meterWords(EffectKind::Comp, dB, words, sizeof words);
            bool ok = words[0] != 0 && inked(canvas, scratch, 0, F::kPaneX + F::kPaneW - 12.f - fonts.small.width(words), F::kPaneY + 36.f, words, ui::kBrass);
            // The strip in THRESHOLD's dial (the first), from the right, kMeterH high.
            const int px = FxScreen::meterPixels(dB, F::kMeterW), y = F::kMeterY, right = F::dialX(0) + F::kMeterX + F::kMeterW;
            ok = ok && px > 0 && canvas.run(right - px, y, ui::kBrass) == px && !canvas.is(right - px - 1, y, ui::kBrass);
            ok = ok && canvas.is(right - 1, y + F::kMeterH - 1, ui::kBrass) && !canvas.is(right - 1, y + F::kMeterH, ui::kBrass) && !canvas.is(right - 1, y - 1, ui::kBrass);
            // No strip in the dials beside it.
            for (int p = 1; p < 4; ++p) ok = ok && !canvas.is(F::dialX(p) + F::kMeterX + F::kMeterW - 1, y, ui::kBrass);
            // The mark on the line: its own track, lit from the top as far as the meter says.
            const int mh = F::kLineH - 14, lit = FxScreen::meterPixels(dB, mh), top = F::lineY(row) + 7;
            int brass = 0;
            for (int py = top; py < top + mh; ++py) brass += canvas.is(F::kMarkX - 7, py, ui::kBrass) ? 1 : 0;
            ok = ok && lit > 0 && brass == lit && canvas.is(F::kMarkX - 7, top, ui::kBrass) && canvas.is(F::kMarkX - 7, top + mh - 1, lit == mh ? ui::kBrass : ui::kLine);
            return ok;
        };
        if (canvas.ok && scratch.ok) {
            CHECK(shown(0));
            CHECK(shown(FxScreen::kMasterRow + 1));
            // Every line's mark is its own effect's, wherever the cursor is.
            const auto markLit = [&](int row) {
                int brass = 0;
                for (int py = F::lineY(row) + 7; py < F::lineY(row) + F::kLineH - 7; ++py) brass += canvas.is(F::kMarkX - 7, py, ui::kBrass) ? 1 : 0;
                return brass;
            };
            const int mine = FxScreen::meterPixels(fx.meterOf(0), F::kLineH - 14), masters = FxScreen::meterPixels(fx.meterOf(FxScreen::kMasterRow + 1), F::kLineH - 14);
            CHECK(mine != masters && markLit(0) == mine && markLit(FxScreen::kMasterRow + 1) == masters);
            CHECK(FxScreen::meterPixels(fx.meterOf(0), F::kMeterW) != FxScreen::meterPixels(fx.meterOf(FxScreen::kMasterRow + 1), F::kMeterW));
            // A line whose effect has no meter has no such mark, and the heading says nothing.
            for (int row : {FxScreen::kBusRow, FxScreen::kBusRow + 1})
                for (int py = F::lineY(row) + 7; py < F::lineY(row) + F::kLineH - 7; ++py) CHECK(canvas.is(F::kMarkX - 7, py, ui::kPanel));
            fx.goTo(FxScreen::kBusRow, 1);
            canvas.clear();
            fx.draw(canvas.r, fonts);
            for (int px = F::kPaneX + 200; px < F::kPaneX + F::kPaneW - 2; ++px) CHECK(canvas.is(px, F::kPaneY + 42, ui::kPanel));
            fx.goTo(0, 0);
        }
    }
    // Another effect in the slot: it has nothing to show.
    session.setEffect(0, 0, EffectKind::Filter);
    rig.frames(1);
    rig.render(2000);
    hit();
    CHECK(fx.meterOf(0) == 0.f && fx.meterOf(FxScreen::kMasterRow + 1) > 0.f); // a FILTER shows nothing
}

// The FX screen's list: eight lines, all in view, walked with up and down alone; the sends; the buses.
// TRYING AN EFFECT. With A held on a slot's line the list of effects only TRIES what the cursor passes over:
// heard, but the slot's own effect, its settings and its automation are kept aside. Let go on the line it
// started on, press B, leave the screen or the track: it is as it was, to the bit. Let go elsewhere: in.
namespace {
struct Tuned {
    EffectData data;
    std::vector<float> lanes; // the slot's four lanes, every scene
};
Tuned tunedOf(Rig& rig, int track, int slot) {
    Tuned t;
    t.data = rig.app.session().effectData(track, slot);
    if (track >= 0)
        for (int scene = 0; scene < kMaxScenes; ++scene)
            for (int p = 0; p < 4; ++p)
                for (int step = 0; step < kAutoSteps; ++step) t.lanes.push_back(rig.project().tracks[track].automation[scene][autoLaneOfEffect(slot, p)][step]);
    return t;
}
bool same(const Tuned& a, const Tuned& b) {
    if (a.data.kind != b.data.kind || a.lanes != b.lanes) return false;
    for (int i = 0; i < kEffectParams; ++i)
        if (a.data.param[i] != b.data.param[i]) return false;
    return true;
}
// A TAPE in the track's first slot, turned, with automation in three scenes.
void tuneTape(Rig& rig) {
    Session& session = rig.app.session();
    session.setEffect(0, 0, EffectKind::Tape);
    session.setEffectParam(0, 0, 0, 0.3125f); // 1/8: a quarter of a 120 BPM bar, 12000 samples
    session.setEffectParam(0, 0, 1, 0.4123f);
    session.setEffectParam(0, 0, 2, 0.0777f);
    session.setEffectParam(0, 0, 3, 0.8f);
    session.setAutomation(0, 0, autoLaneOfEffect(0, 1), 3, 0.2f);
    session.setAutomation(0, 4, autoLaneOfEffect(0, 2), 17, 0.6f);
    session.setAutomation(0, kMaxScenes - 1, autoLaneOfEffect(0, 3), kAutoSteps - 1, 0.1f);
    rig.frames(1);
    rig.render(48000);
}
// A rim shot through track 1 and how loud its first echo is (TAPE's TIME, a quarter of a bar on).
float echoOfAHit(Rig& rig) {
    rig.render(96000); // anything tried has long gone
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    rig.engine->send(hit);
    const std::vector<float> out = rig.render(30000);
    return Rig::peak(out, 11000, 16000);
}
// Which of track 1's slots the engine holds the automation of (a bit a slot), once what was sent has arrived.
// (512 frames: after a whole song sent again, what follows waits for a sounding track's 5 ms fade.)
int heldOf(Rig& rig) {
    rig.render(512);
    return rig.engine->automationHeld(0);
}
} // namespace

void testTryingAnEffectKeepsIt() {
    const int tapeLine = familyPlace(EffectKind::Tape), lines = familyListSize();
    // The sound of the TAPE never browsed past, to hold the browsed one to.
    float before = 0.f;
    {
        Rig rig;
        tuneTape(rig);
        rig.app.session().view = View::Effects;
        rig.frames(1);
        before = echoOfAHit(rig);
        CHECK(before > 0.01f);
    }
    {
        // Down the whole list and back up it, let go on the line it started on: the slot is as it was, and so is its sound.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        tuneTape(rig);
        const Tuned was = tunedOf(rig, 0, 0);
        session.view = View::Effects;
        fx.goTo(0, 0);
        rig.frames(1);
        rig.key(A, true);
        CHECK(fx.picking() && !fx.trying());
        for (int i = 0; i < lines; ++i) {
            rig.tap(DOWN);
            rig.frames(1);
            rig.render(2000);
            CHECK(same(tunedOf(rig, 0, 0), was)); // passing over each: the slot itself untouched
            CHECK(heldOf(rig) == 1);              // ...and its automation does not move what is tried
        }
        CHECK(fx.onLine(0).kind == static_cast<int>(familyAt(lines - 1)) && fx.trying());
        rig.tap(UP, lines);
        CHECK(fx.onLine(0).kind == static_cast<int>(EffectKind::None));
        rig.tap(DOWN, tapeLine);
        CHECK(!fx.trying() && fx.onLine(0).kind == static_cast<int>(EffectKind::Tape)); // back on its family's line: itself, as it was
        rig.tap(RIGHT); // PING tried, and back
        CHECK(fx.trying() && fx.onLine(0).kind == static_cast<int>(EffectKind::Ping));
        rig.tap(LEFT);
        CHECK(!fx.trying());
        rig.key(A, false);
        rig.frames(1);
        CHECK(same(tunedOf(rig, 0, 0), was) && !fx.picking());
        CHECK(heldOf(rig) == 0);
        const float after = echoOfAHit(rig);
        CHECK(std::fabs(after - before) <= 0.01f * before); // heard as before
        if (std::fabs(after - before) > 0.01f * before) std::printf("    an echo of %.4f, as before %.4f\n", static_cast<double>(after), static_cast<double>(before));

        // B with A held calls it off, wherever the cursor is in the list; A does nothing more until it is pressed again.
        rig.key(A, true);
        rig.tap(DOWN, 2);
        CHECK(fx.trying() && fx.onLine(0).kind == static_cast<int>(familyAt(tapeLine + 2)));
        CHECK(heldOf(rig) == 1);
        rig.tap(B);
        CHECK(!fx.trying() && !fx.picking() && same(tunedOf(rig, 0, 0), was));
        CHECK(heldOf(rig) == 0);
        rig.key(A, false);
        rig.frames(1);
        CHECK(same(tunedOf(rig, 0, 0), was));
        CHECK(std::fabs(echoOfAHit(rig) - before) <= 0.01f * before);

        // Another screen up mid-hold: called off.
        fx.goTo(0, 0);
        rig.key(A, true);
        rig.tap(UP);
        CHECK(fx.trying());
        session.view = View::Pattern;
        rig.frames(1);
        CHECK(!fx.trying() && same(tunedOf(rig, 0, 0), was) && heldOf(rig) == 0);
        rig.key(A, false);
        session.view = View::Effects;
        rig.frames(1);
        CHECK(same(tunedOf(rig, 0, 0), was));
        // The track changed under it mid-hold: called off, the first track's slot as it was.
        addPhazed(rig);
        session.view = View::Effects;
        rig.project().track = 0;
        fx.goTo(0, 0);
        rig.frames(1);
        rig.key(A, true);
        rig.tap(UP);
        CHECK(fx.trying());
        rig.project().track = 1;
        rig.frames(1);
        CHECK(!fx.trying() && same(tunedOf(rig, 0, 0), was) && rig.project().tracks[1].effects[0].kind == 0 && heldOf(rig) == 0);
        rig.key(A, false);
        rig.frames(1);
        CHECK(same(tunedOf(rig, 0, 0), was) && rig.project().tracks[1].effects[0].kind == 0);
        rig.project().track = 0;
        rig.frames(1);
        const float last = echoOfAHit(rig);
        CHECK(std::fabs(last - before) <= 0.01f * before);
        if (std::fabs(last - before) > 0.01f * before) std::printf("    an echo of %.4f, as before %.4f\n", static_cast<double>(last), static_cast<double>(before));
    }
    {
        // Let go on another family: it goes in, at its usual settings, and the old automation goes, in every scene.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        tuneTape(rig);
        session.view = View::Effects;
        fx.goTo(0, 0);
        rig.frames(1);
        rig.key(A, true);
        rig.tap(UP, 2);
        const EffectData tried = fx.onLine(0);
        CHECK(tried.kind == static_cast<int>(familyAt(tapeLine - 2)) && tried.kind != 0);
        rig.key(A, false);
        rig.frames(1);
        const Tuned now = tunedOf(rig, 0, 0);
        CHECK(now.data.kind == tried.kind);
        for (int i = 0; i < kEffectParams; ++i) CHECK(now.data.param[i] == tried.param[i] && now.data.param[i] == effectDefault(static_cast<EffectKind>(tried.kind), i));
        CHECK(std::count(now.lanes.begin(), now.lanes.end(), kAutoNone) == static_cast<long>(now.lanes.size()));
        CHECK(heldOf(rig) == 0); // what goes in is moved by its automation again (none yet, but written later, it is)
        // A whole song sent again in the middle of trying (a full queue): the engine holds nothing then, and the
        // next effect tried holds it again.
        rig.key(A, true);
        rig.tap(UP);
        CHECK(fx.trying() && heldOf(rig) == 1);
        session.applyProject();
        rig.frames(1);
        CHECK(fx.trying() && heldOf(rig) == 0);
        rig.tap(UP);
        CHECK(fx.trying() && heldOf(rig) == 1);
        rig.tap(B);
        rig.key(A, false);
        rig.frames(1);
        CHECK(heldOf(rig) == 0 && session.effectData(0, 0).kind == tried.kind);
        // Another family's second type: what was heard goes in (its first type's usual settings, carried).
        rig.key(A, true);
        rig.tap(UP, lines);
        rig.tap(DOWN, familyPlace(EffectKind::Drive));
        rig.tap(RIGHT);
        const EffectData saturate = fx.onLine(0);
        CHECK(saturate.kind == static_cast<int>(EffectKind::Saturate));
        rig.key(A, false);
        rig.frames(1);
        CHECK(session.effectData(0, 0).kind == static_cast<int>(EffectKind::Saturate) && heldOf(rig) == 0);
        for (int i = 0; i < kEffectParams; ++i) CHECK(session.effectData(0, 0).param[i] == saturate.param[i]);
        // EMPTY, let go: the slot is empty.
        rig.key(A, true);
        rig.tap(UP, lines);
        rig.key(A, false);
        rig.frames(1);
        CHECK(session.effectData(0, 0).kind == 0);
    }
    {
        // Let go on another TYPE of its family: in, with the settings the two share as they read, and their automation moved.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        tuneTape(rig);
        const Tuned was = tunedOf(rig, 0, 0);
        session.view = View::Effects;
        fx.goTo(0, 0);
        rig.frames(1);
        rig.key(A, true);
        rig.tap(UP);
        rig.tap(DOWN); // through another family and back: still carried from the TAPE as it is
        rig.tap(RIGHT);
        const EffectData tried = fx.onLine(0);
        CHECK(tried.kind == static_cast<int>(EffectKind::Ping) && same(tunedOf(rig, 0, 0), was));
        rig.key(A, false);
        rig.frames(1);
        const Tuned now = tunedOf(rig, 0, 0);
        CHECK(now.data.kind == static_cast<int>(EffectKind::Ping));
        for (int i = 0; i < kEffectParams; ++i) CHECK(now.data.param[i] == tried.param[i]); // what was heard is what went in
        CHECK(now.data.param[0] == was.data.param[0] && now.data.param[3] == was.data.param[3]); // TIME and MIX: one scale
        CHECK(now.data.param[2] == effectDefault(EffectKind::Ping, 2)); // AGE is not TONE
        const auto lane = [&](const Tuned& t, int scene, int p, int step) { return t.lanes[static_cast<std::size_t>((scene * 4 + p) * kAutoSteps + step)]; };
        CHECK(lane(now, 0, 1, 3) >= 0.f && lane(now, 4, 2, 17) == kAutoNone && lane(now, kMaxScenes - 1, 3, kAutoSteps - 1) == 0.1f); // FEEDBACK moved, AGE gone, MIX kept
        CHECK(lane(was, 4, 2, 17) == 0.6f);
        CHECK(heldOf(rig) == 0);
        // ...and the engine plays it: MIX turned up, written to 0 on every step, no echo comes back.
        session.setEffectParam(0, 0, 3, 0.9f);
        for (int step = 0; step < kAutoSteps; ++step) session.setAutomation(0, 0, autoLaneOfEffect(0, 3), step, 0.f);
        rig.frames(1);
        if (!rig.project().playing) session.togglePlay(false);
        rig.frames(1);
        rig.render(48000);
        const float echo = echoOfAHit(rig);
        CHECK(echo < 0.001f);
        if (echo >= 0.001f) std::printf("    an echo of %.5f with MIX written to 0\n", static_cast<double>(echo));
    }
    {
        // A bus and the master: the same.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.setEffectParam(kSpaceBus, 0, 0, 0.31f);
        session.setEffectParam(kSpaceBus, 0, 3, 0.6f);
        session.setEffect(kMasterTrack, 1, EffectKind::Comp);
        session.setEffectParam(kMasterTrack, 1, 0, 0.27f);
        const Tuned bus = tunedOf(rig, kSpaceBus, 0), master = tunedOf(rig, kMasterTrack, 1);
        session.view = View::Effects;
        for (int row : {FxScreen::kBusRow + 1, FxScreen::kMasterRow + 1}) {
            fx.goTo(row, 0);
            rig.frames(1);
            rig.key(A, true);
            rig.tap(UP, 8);
            CHECK(fx.trying() && fx.onLine(row).kind == 0);
            rig.tap(DOWN, 8);
            rig.tap(UP, 8);
            rig.tap(B);
            rig.key(A, false);
            rig.frames(1);
            rig.key(A, true);
            rig.tap(UP, 8);
            rig.tap(DOWN, row == FxScreen::kBusRow + 1 ? 1 : familyPlace(EffectKind::Comp));
            rig.key(A, false);
            rig.frames(1);
        }
        CHECK(same(tunedOf(rig, kSpaceBus, 0), bus) && same(tunedOf(rig, kMasterTrack, 1), master));
        fx.goTo(FxScreen::kBusRow + 1, 0);
        rig.key(A, true);
        rig.tap(RIGHT);
        rig.key(A, false);
        rig.frames(1);
        CHECK(session.effectData(kSpaceBus, 0).kind == static_cast<int>(effectTypeAt(EffectKind::Hall, effectTypeIndex(EffectKind::Hall) + 1)) && session.effectData(kSpaceBus, 0).param[effectMixParam(static_cast<EffectKind>(session.effectData(kSpaceBus, 0).kind))] == 0.6f); // LEVEL carried
    }
}

void testTheFxScreensList() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    rig.frames(1);
    const int delay = static_cast<int>(EffectKind::Delay), tape = static_cast<int>(EffectKind::Tape), reverb = static_cast<int>(EffectKind::Reverb), hall = static_cast<int>(EffectKind::Hall);
    const int filter = static_cast<int>(EffectKind::Filter);
    CHECK(FxScreen::kRows == 8 && FxScreen::kSendsRow == 3 && FxScreen::kBusRow == 4 && FxScreen::kMasterRow == 6);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 0);

    // Down the list with nothing but DOWN: every line in turn, none hidden, and round; UP the same way back.
    for (int line = 1; line <= FxScreen::kRows; ++line) {
        rig.tap(DOWN);
        CHECK(fx.cursorRow() == line % FxScreen::kRows && fx.cursorColumn() == 0);
    }
    for (int line = FxScreen::kRows - 1; line >= 0; --line) {
        rig.tap(UP);
        CHECK(fx.cursorRow() == line && fx.cursorColumn() == 0);
    }
    // Each line is drawn where the list says, one under the other, clear of the bar at the top and the hints.
    for (int line = 0; line < FxScreen::kRows; ++line) {
        CHECK(FxScreen::lineY(line) >= ui::kTopBarH + 19 && FxScreen::lineY(line) + FxScreen::kLineH <= ui::kScreenH - ui::kHintsH);
        if (line) CHECK(FxScreen::lineY(line) >= FxScreen::lineY(line - 1) + FxScreen::kLineH + FxScreen::kLineGap);
    }
    CHECK(FxScreen::lineY(FxScreen::kBusRow) - FxScreen::lineY(FxScreen::kBusRow - 1) > FxScreen::kLineH + 19); // room for the BUSES heading
    CHECK(FxScreen::lineY(FxScreen::kMasterRow) - FxScreen::lineY(FxScreen::kMasterRow - 1) > FxScreen::kLineH + 19);
    // Y is not a page any more: it does nothing here, held or not.
    fx.goTo(1, 0);
    rig.tap(Y);
    CHECK(fx.cursorRow() == 1 && fx.cursorColumn() == 0);
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    CHECK(fx.cursorRow() == 1 && fx.cursorColumn() == 0 && project.tracks[0].effects[1].kind == 0);

    // How many settings each line has: two sends; an effect's own; none beside an empty slot.
    CHECK(fx.settings(0) == 0 && fx.settings(3) == 2 && fx.settings(4) == 4 && fx.settings(5) == 4 && fx.settings(6) == 0);
    fx.goTo(3, 0);
    rig.tap(RIGHT);
    CHECK(fx.cursorRow() == 3 && fx.cursorColumn() == 1); // the ECHO send
    rig.tap(DOWN, 3);
    CHECK(fx.cursorRow() == 3 && fx.cursorColumn() == 2); // the SPACE send, and the end
    rig.tap(UP, 3);
    CHECK(fx.cursorColumn() == 1);
    rig.tap(LEFT);
    CHECK(fx.cursorRow() == 3 && fx.cursorColumn() == 0);
    // Each line remembers its own last setting.
    fx.goTo(4, 3); // ECHO's TAPE: AGE
    rig.tap(LEFT);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(fx.cursorRow() == 5 && fx.cursorColumn() == 1); // SPACE's: never used, its first
    rig.tap(DOWN, 9);
    CHECK(fx.cursorColumn() == 4); // HALL has four
    rig.tap(LEFT);
    rig.tap(UP);
    rig.tap(RIGHT);
    CHECK(fx.cursorRow() == 4 && fx.cursorColumn() == 3);
    rig.tap(LEFT);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(fx.cursorRow() == 5 && fx.cursorColumn() == 4 && fx.remembered(5) == 4 && fx.remembered(4) == 3 && fx.remembered(0) == 1);
    CHECK(fx.remembered(-1) == 1 && fx.remembered(FxScreen::kRows) == 1);
    // An effect with fewer settings there now: the nearest it has, and back in the list if it has none.
    session.setEffect(kSpaceBus, 0, EffectKind::Reverb); // the room reverb has three
    rig.frames(1);
    CHECK(fx.cursorRow() == 5 && fx.cursorColumn() == 3);
    session.setEffect(kSpaceBus, 0, EffectKind::None);
    rig.frames(1);
    CHECK(fx.cursorRow() == 5 && fx.cursorColumn() == 0);
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 0);
    session.setEffect(kSpaceBus, 0, EffectKind::Hall);
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 4); // (what the line last used is still known)
    rig.tap(LEFT);
    // goTo keeps to what there is.
    fx.goTo(6, 3);
    CHECK(fx.cursorRow() == 6 && fx.cursorColumn() == 0);
    fx.goTo(3, 9);
    CHECK(fx.cursorColumn() == 2);
    fx.goTo(-1, -4);
    CHECK(fx.cursorRow() == 7 && fx.cursorColumn() == 0);
    fx.goTo(8, 0);
    CHECK(fx.cursorRow() == 0);

    // The sends. Hold A + the D-pad: a twentieth at a push, stopping at the ends.
    fx.goTo(3, 0);
    rig.key(A, true);
    CHECK(!fx.picking()); // the sends line holds no effect: A opens no list there, and turns nothing from the list
    rig.tap(UP, 2);
    rig.tap(DOWN);
    rig.tap(LEFT);
    rig.tap(RIGHT);
    CHECK(project.tracks[0].send[0] == 0.f && project.tracks[0].send[1] == 0.f && fx.cursorRow() == 3 && fx.cursorColumn() == 0);
    rig.key(A, false);
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 2); // where the line was last (goTo, above)
    rig.tap(UP);
    rig.key(A, true);
    rig.tap(UP, 3);
    CHECK(near(project.tracks[0].send[0], 0.15f) && project.tracks[0].send[1] == 0.f);
    rig.tap(DOWN);
    CHECK(near(project.tracks[0].send[0], 0.10f));
    rig.tap(DOWN, 5);
    CHECK(project.tracks[0].send[0] == 0.f);
    rig.tap(LEFT);
    rig.tap(RIGHT); // sideways with A held does nothing to a send, and does not move off it
    CHECK(project.tracks[0].send[0] == 0.f && project.tracks[0].send[1] == 0.f && fx.cursorColumn() == 1);
    rig.key(A, false);
    rig.tap(DOWN);
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
    rig.tap(UP);
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(10);
    rig.key(STICK_UP, false);
    rig.frames(1);
    const float echo = project.tracks[0].send[0];
    CHECK(echo > 0.02f && echo < 0.3f && std::fabs(echo / 0.05f - std::round(echo / 0.05f)) > 0.02f);
    rig.key(A, false);
    session.setSend(0, 0, 0.f);
    rig.tap(DOWN);
    // It reaches the engine: a hit now wakes the SPACE bus and not the ECHO bus.
    Command hit;
    hit.type = Command::Trigger;
    hit.a = 6;
    hit.value = 1.f;
    rig.engine->send(hit);
    rig.render(4800);
    CHECK(rig.engine->busAwake(1) && !rig.engine->busAwake(0));
    // B on a send: nothing sent, and its automation in this cell goes; the other send's stays, and so does another scene's.
    session.setSend(0, 0, 0.4f);
    session.setAutomation(0, 0, autoLaneOfSend(0), 3, 0.5f);
    session.setAutomation(0, 0, autoLaneOfSend(1), 3, 0.5f);
    session.setAutomation(0, 2, autoLaneOfSend(1), 3, 0.5f);
    rig.tap(B);
    CHECK(project.tracks[0].send[1] == 0.f && project.tracks[0].send[0] == 0.4f && fx.cursorColumn() == 2);
    CHECK(!session.hasAutomation(0, 0, autoLaneOfSend(1)) && session.hasAutomation(0, 0, autoLaneOfSend(0)) && session.hasAutomation(0, 2, autoLaneOfSend(1)));
    rig.tap(UP);
    rig.tap(B);
    CHECK(project.tracks[0].send[0] == 0.f && !session.hasAutomation(0, 0, autoLaneOfSend(0)));
    // B on the SENDS line itself: nothing sent to either, and both automations of this cell go.
    session.setSend(0, 0, 0.4f);
    session.setSend(0, 1, 0.7f);
    session.setAutomation(0, 0, autoLaneOfSend(0), 3, 0.5f);
    session.setAutomation(0, 0, autoLaneOfSend(1), 3, 0.5f);
    rig.tap(LEFT);
    rig.key(A, true);
    rig.tap(B); // not while A is held
    rig.key(A, false);
    CHECK(project.tracks[0].send[0] == 0.4f && project.tracks[0].send[1] == 0.7f);
    rig.tap(B);
    CHECK(project.tracks[0].send[0] == 0.f && project.tracks[0].send[1] == 0.f && fx.cursorRow() == 3 && fx.cursorColumn() == 0);
    CHECK(!session.hasAutomation(0, 0, autoLaneOfSend(0)) && !session.hasAutomation(0, 0, autoLaneOfSend(1)) && session.hasAutomation(0, 2, autoLaneOfSend(1)));
    rig.tap(RIGHT);
    CHECK(fx.cursorColumn() == 1);
    // With WRITE on and the song playing, a send turned is written down, and B's reset is not.
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
    // ...and so is an effect's setting, from its row on the right.
    session.setEffect(0, 0, EffectKind::Filter);
    fx.goTo(0, 2); // RESONANCE
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(session.hasAutomation(0, 0, autoLaneOfEffect(0, 1)) && near(project.tracks[0].automation[0][autoLaneOfEffect(0, 1)][1], 0.25f) && !session.hasAutomation(0, 0, autoLaneOfEffect(0, 0)));
    rig.tap(B); // B: its usual value, the automation of this cell gone, and that not written either
    CHECK(!session.hasAutomation(0, 0, autoLaneOfEffect(0, 1)) && project.tracks[0].effects[0].param[1] == effectInfo(EffectKind::Filter).defaults[1]);
    // Picking an effect and changing a TYPE are not knobs: WRITE records nothing of them.
    rig.tap(LEFT);
    rig.key(A, true);
    rig.tap(DOWN, familyPlace(EffectKind::Delay) - familyPlace(EffectKind::Filter)); // DELAY
    rig.tap(RIGHT); // its TAPE
    rig.key(A, false);
    CHECK(project.tracks[0].effects[0].kind == tape && session.writing());
    for (int p = 0; p < kEffectParams; ++p) CHECK(!session.hasAutomation(0, 0, autoLaneOfEffect(0, p)));
    session.setEffect(0, 0, EffectKind::None);
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
    fx.goTo(3, 1);
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(near(project.tracks[1].send[0], 0.1f) && project.tracks[0].send[0] == 0.f);
    project.track = 0;

    // The third slot is a slot like the others.
    fx.goTo(2, 0);
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(project.tracks[0].effects[2].kind == filter && project.tracks[0].effects[0].kind == 0 && project.tracks[0].effects[1].kind == 0);
    holdB(rig); // held: deleted when its bar is full
    CHECK(project.tracks[0].effects[2].kind == 0);

    // A bus offers its own family only: EMPTY and DELAY on ECHO, EMPTY and REVERB on SPACE; left and right its types.
    CHECK(project.busEffects[0].kind == tape && project.busEffects[1].kind == hall); // a new song's
    fx.goTo(4, 0);
    rig.key(A, true);
    CHECK(fx.picking());
    rig.tap(DOWN, 7);
    CHECK(project.busEffects[0].kind == tape); // on its family already, the end of its list: nothing is put in afresh
    rig.tap(UP);
    CHECK(fx.onLine(4).kind == 0 && project.busEffects[1].kind == hall);
    rig.tap(UP, 3);
    rig.tap(RIGHT); // nothing has no TYPE
    CHECK(fx.onLine(4).kind == 0 && fx.cursorRow() == 4 && fx.cursorColumn() == 0);
    rig.tap(DOWN);
    // Back on the family it started in: what it started with, as it was (TAPE, not the family's first type).
    CHECK(fx.onLine(4).kind == tape && !fx.trying());
    for (int i = 0; i < kEffectParams; ++i) CHECK(fx.onLine(4).param[i] == project.busEffects[0].param[i]);
    for (int i = 0; i < 4; ++i) {
        rig.tap(DOWN);
        rig.tap(RIGHT);
        CHECK(effectBus(static_cast<EffectKind>(fx.onLine(4).kind)) == 0);
    }
    CHECK(fx.onLine(4).kind == static_cast<int>(EffectKind::Stutter) && fx.onLine(4).param[3] == 1.f); // the DELAY's last type
    rig.tap(LEFT, 5);
    CHECK(fx.onLine(4).kind == delay);
    rig.key(A, false);
    fx.goTo(5, 0);
    holdB(rig);
    CHECK(project.busEffects[1].kind == 0 && project.busEffects[0].kind == delay);
    rig.key(A, true);
    rig.tap(DOWN);
    CHECK(fx.onLine(5).kind == reverb && fx.onLine(5).param[2] == 1.f);
    rig.tap(DOWN, 4);
    rig.tap(RIGHT, effectTypes(EffectKind::Reverb) - 1);
    CHECK(fx.onLine(5).kind == static_cast<int>(effectTypeAt(EffectKind::Reverb, effectTypes(EffectKind::Reverb) - 1)) && fx.onLine(5).param[3] == 1.f); // the last of its types, and no further
    rig.tap(LEFT, effectTypes(EffectKind::Reverb) - 2);
    CHECK(fx.onLine(5).kind == hall && fx.onLine(5).param[3] == 1.f);
    rig.key(A, false);
    // The Session refuses the other family on a bus, whoever asks.
    session.setEffect(kEchoBus, 0, EffectKind::Hall);
    session.setEffect(kSpaceBus, 0, EffectKind::Tape);
    session.setEffect(kEchoBus, 0, EffectKind::Filter);
    CHECK(project.busEffects[0].kind == delay && project.busEffects[1].kind == hall);
    CHECK(!changeEffectType(session, kEchoBus, 0, EffectKind::Hall) && !changeEffectType(session, kSpaceBus, 0, EffectKind::Delay) && project.busEffects[0].kind == delay && project.busEffects[1].kind == hall);
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
    CHECK(project.busEffects[0].param[1] == effectInfo(EffectKind::Delay).defaults[1]);
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 3, 0.9f);
    fx.goTo(0, 4);
    rig.tap(B);
    CHECK(project.tracks[0].effects[0].param[3] == effectInfo(EffectKind::Delay).defaults[3]);
    session.setEffect(0, 0, EffectKind::None);

    // The master's two slots are the last two lines.
    fx.goTo(6, 0);
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(project.masterEffects[0].kind == filter && project.masterEffects[1].kind == 0);
    fx.goTo(7, 0);
    rig.key(A, true);
    rig.tap(DOWN, familyPlace(EffectKind::Delay));
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(project.masterEffects[1].kind == tape && project.masterEffects[0].kind == filter); // the master takes any family, and its types
    fx.goTo(6, 0);
    holdB(rig);
    fx.goTo(7, 0);
    holdB(rig);
    CHECK(project.masterEffects[0].kind == 0 && project.masterEffects[1].kind == 0);

    // The stick with A held on a line: one push, one step of the list; sideways, one type.
    fx.goTo(0, 0);
    const auto kindNow = [&] { return static_cast<EffectKind>(fx.onLine(0).kind); };
    const auto flick = [&](SDL_Keycode way) {
        rig.key(way, true);
        rig.frames(1);
        rig.key(way, false);
        rig.frames(1);
    };
    rig.key(A, true);
    flick(STICK_DOWN);
    CHECK(kindNow() == EffectKind::Filter);
    flick(STICK_DOWN);
    CHECK(kindNow() == EffectKind::Drive);
    flick(STICK_RIGHT);
    CHECK(kindNow() == EffectKind::Saturate && fx.cursorColumn() == 0);
    flick(STICK_LEFT);
    CHECK(kindNow() == EffectKind::Drive);
    flick(STICK_UP);
    CHECK(kindNow() == EffectKind::Filter);
    // Stopping at an end is doing nothing: the effect there is not tried afresh.
    rig.tap(DOWN, familyListSize());
    CHECK(kindNow() == EffectKind::Comp); // LEVEL, as its first type
    rig.tap(DOWN);
    rig.tap(LEFT); // (COMP is LEVEL's first type: that end too)
    CHECK(kindNow() == EffectKind::Comp);
    rig.tap(RIGHT, 4); // DUCK, LIMITER, SNAP, and at the other end it stops
    CHECK(kindNow() == EffectKind::Snap);
    rig.tap(LEFT);
    CHECK(kindNow() == EffectKind::Limiter && fx.onLine(0).param[0] == effectDefault(EffectKind::Limiter, 0)); // (nothing shared with COMP: its usual values)
    rig.tap(LEFT, 2);
    CHECK(kindNow() == EffectKind::Comp && fx.onLine(0).param[0] == effectDefault(EffectKind::Comp, 0));
    // A held D-pad steps through the list slowly enough to see each one: the list's pace (gesture audit G32: it was
    // a bare 200 ms, five a second), after the cursor's wait it always had, each counted from when it was due.
    rig.tap(UP, familyListSize());
    CHECK(kindNow() == EffectKind::None);
    rig.key(DOWN, true);
    rig.frames(1);
    CHECK(familyPlace(kindNow()) == 1);
    rig.frames(40); // two thirds of a second: the wait, then one every 110 ms (short of the list's end)
    CHECK(familyPlace(kindNow()) == static_cast<int>(1 + 1 + (41 * 16 - 16 - ui::kCursorDelayMs) / ui::kListEveryMs) && familyPlace(kindNow()) < familyListSize() - 1);
    rig.key(DOWN, false);
    rig.key(A, false);
    // On a setting, left and right with A held switch the slot's TYPE (testFxSwitchesType), the cursor kept;
    // back again it is the FILTER as it was left.
    session.setEffect(0, 0, EffectKind::Filter);
    fx.goTo(0, 1);
    const float cutoff = project.tracks[0].effects[0].param[0];
    rig.key(A, true);
    rig.tap(RIGHT);
    CHECK(project.tracks[0].effects[0].kind == static_cast<int>(effectTypeAt(EffectKind::Filter, 1)) && fx.cursorColumn() == 1);
    rig.tap(LEFT);
    CHECK(project.tracks[0].effects[0].kind == static_cast<int>(EffectKind::Filter) && project.tracks[0].effects[0].param[0] == cutoff && fx.cursorColumn() == 1);
    rig.tap(LEFT);
    CHECK(project.tracks[0].effects[0].kind == filter && project.tracks[0].effects[0].param[0] == cutoff && fx.cursorColumn() == 1);
    rig.key(A, false);
    // Without A, left goes back to the list, as ever.
    rig.tap(LEFT);
    CHECK(fx.cursorColumn() == 0 && project.tracks[0].effects[0].kind == filter);
}

// ---- the FX screen as it is DRAWN (tests/canvas.hpp) -----------------------------------------

// Is `text` drawn at (x, y) in `colour`? It is drawn there on a second screen, and wherever that
// has a pixel of the full colour (the body of a letter, not its soft edge) the screen looked at
// must have it too.

void testTheFxScreenAsDrawn() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    using namespace ui;
    typedef FxScreen F;
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    session.setEffect(0, 0, EffectKind::Tape);
    session.setEffect(0, 2, EffectKind::Filter);
    rig.frames(1);
    rig.render(2000);
    const Fonts& fonts = *canvas.fonts;
    const auto draw = [&] {
        canvas.clear();
        fx.draw(canvas.r, fonts);
    };
    const float smallMid = (F::kLineH - fonts.small.capHeight()) / 2.f - 1.f, mediumMid = (F::kLineH - fonts.medium.capHeight()) / 2.f - 1.f;
    const auto lineName = [&](int line, const char* name, SDL_Color colour) { return inked(canvas, scratch, 1, static_cast<float>(F::kNameX), F::lineY(line) + mediumMid, name, colour); };
    const auto pickY = [&](int place) { return F::pickLineY(place); };
    const auto pickName = [&](int place, const char* name, SDL_Color colour) {
        return inked(canvas, scratch, 1, static_cast<float>(F::kPickNameX), pickY(place) + (F::kPickLineH - fonts.medium.capHeight()) / 2.f - 1.f, name, colour);
    };
    const auto heading = [&](const char* name, SDL_Color colour) { return inked(canvas, scratch, 2, F::kPaneX + 12.f, F::kPaneY + 30.f, name, colour); };

    // THE LIST, at rest on an empty slot: every line is there, each saying what it holds.
    fx.goTo(1, 0);
    draw();
    CHECK(lineName(0, "DELAY", kText) && lineName(1, "EMPTY", kLine) && lineName(2, "FILTER", kText));
    CHECK(lineName(4, "DELAY", kText) && lineName(5, "REVERB", kText) && lineName(6, "EMPTY", kLine) && lineName(7, "EMPTY", kLine));
    CHECK(!lineName(1, "DELAY", kText) && !lineName(0, "EMPTY", kLine)); // (the check can tell)
    // ...the type beside the family, dimmer: TAPE on the track's delay and on ECHO, HALL on SPACE.
    CHECK(inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("DELAY") + 7.f, F::lineY(0) + smallMid + 1.f, "TAPE", kDim));
    CHECK(inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("DELAY") + 7.f, F::lineY(4) + smallMid + 1.f, "TAPE", kDim));
    CHECK(inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("REVERB") + 7.f, F::lineY(5) + smallMid + 1.f, "HALL", kDim));
    {
        // A type too long to stand 7 after its family keeps the 7 and draws the family small too: REVERB SPRING.
        session.setEffect(kSpaceBus, 0, EffectKind::Spring);
        draw();
        const float at = F::kNameX + fonts.small.width("REVERB") + 7.f;
        CHECK(inked(canvas, scratch, 0, static_cast<float>(F::kNameX), F::lineY(5) + smallMid + 1.f, "REVERB", kText) && inked(canvas, scratch, 0, at, F::lineY(5) + smallMid + 1.f, "SPRING", kDim));
        CHECK(at + fonts.small.width("SPRING") <= F::kMarkX - 3.f && !lineName(5, "REVERB", kText));
        // ...so the two read as two words, off the screen: the empty columns between them are more than any
        // between two letters of either (it once closed up to 2.9: 4 empty columns, as many as inside REVERB).
        const auto blank = [&](int x) {
            for (int y = F::lineY(5) + 3; y < F::lineY(5) + F::kLineH - 3; ++y)
                if (!canvas.is(x, y, kPanel)) return false;
            return true;
        };
        int widest = 0, next = 0, run = 0;
        bool begun = false;
        for (int x = F::kNameX; x < F::kMarkX; ++x) {
            if (blank(x)) {
                ++run;
                continue;
            }
            if (begun && run > 0) {
                if (run > widest) next = widest, widest = run;
                else if (run > next) next = run;
            }
            begun = true, run = 0;
        }
        std::printf("    REVERB SPRING in the list: %d empty columns between the words, at most %d between two letters\n", widest, next);
        CHECK(widest >= 7 && widest >= next + 3);
        session.setEffect(kSpaceBus, 0, EffectKind::Hall);
        draw();
    }
    {
        // DRIVE SATURATE is too long for a line even with both small: the line says SATURATE, in the family's lettering.
        // A type that is its family's own word is not said twice: FILTER, DRIVE.
        session.setEffect(0, 1, EffectKind::Saturate);
        draw();
        CHECK(lineName(1, "SATURATE", kText) && !lineName(1, "DRIVE", kText) && !inked(canvas, scratch, 0, static_cast<float>(F::kNameX), F::lineY(1) + smallMid + 1.f, "DRIVE", kText));
        session.setEffect(0, 1, EffectKind::Drive);
        draw();
        CHECK(lineName(1, "DRIVE", kText) && !inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("DRIVE") + 7.f, F::lineY(1) + smallMid + 1.f, "DRIVE", kDim));
        CHECK(lineName(2, "FILTER", kText) && !inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("FILTER") + 7.f, F::lineY(2) + smallMid + 1.f, "FILTER", kDim));
        session.setEffect(0, 1, EffectKind::None);
        draw();
    }
    // ...the tags and the three headings.
    const char* tags[8] = {"1", "2", "3", "SENDS", "ECHO", "SPACE", "1", "2"};
    for (int line = 0; line < 8; ++line) CHECK(inked(canvas, scratch, 0, F::kListX + 8.f, F::lineY(line) + smallMid, tags[line], line == 1 ? kText : kDim));
    CHECK(inked(canvas, scratch, 0, F::kListX + 1.f, F::lineY(0) - 16.f, "1 DRUMS", kText) && inked(canvas, scratch, 0, F::kListX + 1.f, F::lineY(4) - 16.f, "BUSES", kText));
    CHECK(inked(canvas, scratch, 0, F::kListX + 1.f, F::lineY(6) - 16.f, "MASTER", kText));
    CHECK(inked(canvas, scratch, 0, F::kListX + F::kListW - 1.f - fonts.small.width("THIS TRACK"), F::lineY(0) - 16.f, "THIS TRACK", kDim));
    CHECK(inked(canvas, scratch, 0, F::kListX + F::kListW - 1.f - fonts.small.width("EVERYTHING"), F::lineY(6) - 16.f, "EVERYTHING", kDim));
    // THE PAGE GOES DOWN TO THE HINTS BAR (owner, 2026-11-13: it stopped 40 short): the list's last line ends at the
    // page's foot, 7 above the bar, as the page's top is 7 under the top bar. The same layout, taller.
    CHECK(F::kPageBottom == kScreenH - kHintsH - 7 && F::kPageBottom == 449 && F::kLineH == 41 && F::kDialH == 168 && F::kPicH == 140 && F::kSendH == 193 && F::kPickLineH == 38 && F::kHeadH == 93);
    CHECK(canvas.is(F::kListX + 50, F::kPageBottom - 1, kLine) && canvas.is(F::kListX + 50, F::kPageBottom, kBg) && canvas.is(F::kListX + 50, F::lineY(7), kLine));
    CHECK(canvas.is(F::kListX + 50, F::kPageBottom - 2, kPanel)); // (inside the box: the check can tell)
    for (int y = F::kPageBottom; y < kScreenH - kHintsH; ++y) CHECK(canvas.is(F::kListX + 50, y, kBg) && canvas.is(F::kPaneX + 50, y, kBg)); // nothing between the page and the bar
    // The cursor is the teal box round its line, and the tongue to the right.
    CHECK(canvas.is(F::kListX - 1, F::lineY(1) + 10, kTeal) && !canvas.is(F::kListX - 1, F::lineY(0) + 10, kTeal) && canvas.is(F::kListX + F::kListW + 6, F::lineY(1) + F::kLineH / 2, kTeal));
    // On the right: the slot is EMPTY, and says how to fill it. No list of effects.
    CHECK(heading("EMPTY", kDim) && inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kPaneY + 10.f, "1 DRUMS, EFFECT 2", kDim));
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kSetY + 24.f, "Nothing here.", kText) && inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kSetY + 48.f, "Hold A to see the effects and pick one.", kDim));
    CHECK(!pickName(1, "FILTER", kText));

    // HOLD A: the list of effects is open, EMPTY and the seven families, each on its own line naming its types (or, for a
    // family whose one type is its own word, its words), the cursor on what the slot holds. No group headings.
    rig.key(A, true);
    draw();
    CHECK(familyListSize() == 8 && familyListSize() == F::kPickLines);
    const float pickSmallMid = (F::kPickLineH - fonts.small.capHeight()) / 2.f - 1.f;
    // A family's types on its line, each in its place, the one lit (kBrass) or none (-1).
    const auto typesOn = [&](int place, int lit, int bus = -1) {
        const std::vector<std::string> words = F::typeWords(familyAt(place, bus));
        // In one row or two, as the screen splits it (FxScreen::pickRows; MOD's six since PITCH are two at LARGE).
        const F::PickRows rows = F::pickRows(fonts, words);
        const float capH = rows.set->small.capHeight(), rowsH = rows.rows * capH + (rows.rows - 1) * F::kPickRowGap;
        float x = static_cast<float>(F::kPickTypesX), y = pickY(place) + (F::kPickLineH - rowsH) / 2.f - 1.f;
        bool all = true;
        for (int t = 0; t < static_cast<int>(words.size()); ++t) {
            if (t == rows.split && t > 0) x = static_cast<float>(F::kPickTypesX), y += capH + F::kPickRowGap;
            all = all && inked(canvas, scratch, 0, x, y, words[t].c_str(), t == lit ? kBrass : kDim);
            x += fonts.small.width(words[t].c_str());
            if (t + 1 < static_cast<int>(words.size())) x += fonts.small.width(", "); // (a comma has no pixel of full ink to look for)
        }
        return all;
    };
    static const char* const familyNames[8] = {"EMPTY", "FILTER", "DRIVE", "CRUSH", "MOD", "DELAY", "REVERB", "LEVEL"};
    for (int place = 0; place < familyListSize(); ++place) {
        const EffectKind family = familyAt(place);
        CHECK(pickName(place, familyNames[place], place == 0 ? kBrass : kText) && std::string(place == 0 ? "EMPTY" : effectFamily(family)) == familyNames[place]);
        if (effectFamilyNamesTypes(family)) {
            CHECK(typesOn(place, -1));
        } else {
            char says[64];
            effectFamilySays(family, says, sizeof says);
            CHECK(inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(place) + pickSmallMid, says, kDim));
        }
        CHECK(canvas.is(F::kPaneX + 3, pickY(place) + 10, kBrass) == (place == 0));
        CHECK(pickY(place) + F::kPickLineH <= F::kPageBottom); // clear of the hints
        CHECK(canvas.is(F::kPaneX + 20, pickY(place), kLine) == (place > 0)); // a line between each two, EMPTY's too
    }
    CHECK(typesOn(2, -1) && typesOn(4, -1) && typesOn(5, -1) && typesOn(6, -1) && typesOn(7, -1) && !typesOn(5, 1)); // (the check can tell)
    // FILTER names its types since COMB, VOWEL and EQ (it said "low, band, high, DJ"); CRUSH since RING (it said "fewer bits").
    CHECK(typesOn(1, -1) && inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(1) + pickSmallMid, "filter", kDim) && inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(3) + pickSmallMid, "crush", kDim));
    CHECK(inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(5) + pickSmallMid, "clean", kDim)); // (MOD's "chorus" is on the first of two rows since PITCH: typesOn above)
    // The box goes down to the page's foot, the last line in it.
    CHECK(canvas.is(F::kPaneX + 50, F::kPageBottom - 1, kLine) && canvas.is(F::kPaneX + 50, F::kPageBottom, kBg) && pickY(7) + F::kPickLineH + F::kPickPad == F::kPageBottom);
    CHECK(heading("EMPTY", kBrass) && canvas.is(F::kPaneX - 1, F::kPaneY + 20, kBrass) && canvas.is(F::kListX - 1, F::lineY(1) + 10, kBrass)); // A held: brass
    CHECK(!inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kSetY + 24.f, "Nothing here.", kText));
    // Down four: MOD goes in (CHORUS), the cursor is on its line, its one type lit; the heading and the list on the left say so.
    rig.tap(DOWN, 4);
    draw();
    CHECK(fx.onLine(1).kind == static_cast<int>(EffectKind::Chorus));
    CHECK(pickName(4, "MOD", kBrass) && pickName(0, "EMPTY", kText) && canvas.is(F::kPaneX + 3, pickY(4) + 10, kBrass) && !canvas.is(F::kPaneX + 3, pickY(0) + 10, kBrass));
    CHECK(typesOn(4, 0) && typesOn(5, -1));
    CHECK(heading("MOD", kBrass) && lineName(1, "MOD", kText) && inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("MOD") + 7.f, F::lineY(1) + smallMid + 1.f, "CHORUS", kDim));
    {
        // MOD's first type is named in the heading's TYPE row, on its plate (CHORUS; FLANGER, ENSEMBLE, PHASER and TREMOLO since batch c1).
        const float x = F::kPaneX + 12.f + fonts.small.width("TYPE") + 12.f;
        CHECK(inked(canvas, scratch, 0, x, F::kPaneY + 64.f, "CHORUS", kBg) && canvas.is(static_cast<int>(x) - 5, F::kPaneY + 60, kBrass));
    }
    // The LEFT / RIGHT key at the TYPE row's end: there for MOD (five types; none when it had one), and for DELAY (below).
    const auto keyInk = [&] {
        int ink = 0;
        const int kx = static_cast<int>(F::kPaneX + F::kPaneW - 12.f - ui::keyWidth(fonts, "LEFT / RIGHT")) - 2;
        for (int y = F::kPaneY + 58; y < F::kPaneY + 64 + static_cast<int>(fonts.small.capHeight()) + 6; ++y)
            for (int x = kx; x < F::kPaneX + F::kPaneW - 8; ++x) ink += canvas.is(x, y, kText) ? 1 : 0;
        return ink;
    };
    CHECK(keyInk() > 0);
    // Down one: DELAY, a family with types. Its TYPE row is in the heading, the one it is on a brass plate.
    rig.tap(DOWN);
    draw();
    const float typeX = F::kPaneX + 12.f + fonts.small.width("TYPE") + 12.f, typeY = F::kPaneY + 64.f;
    const float tapeX = typeX + fonts.small.width("CLEAN") + 20.f;
    CHECK(fx.onLine(1).kind == static_cast<int>(EffectKind::Delay) && heading("DELAY", kBrass) && pickName(5, "DELAY", kBrass) && typesOn(5, 0));
    CHECK(keyInk() > 10);
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, typeY, "TYPE", kDim) && inked(canvas, scratch, 0, typeX, typeY, "CLEAN", kBg) && inked(canvas, scratch, 0, tapeX, typeY, "TAPE", kDim));
    CHECK(canvas.is(static_cast<int>(typeX) - 5, static_cast<int>(typeY) - 4, kBrass) && !canvas.is(static_cast<int>(tapeX) - 5, static_cast<int>(typeY) - 4, kBrass));
    rig.tap(RIGHT); // the TYPE: TAPE
    draw();
    CHECK(fx.onLine(1).kind == static_cast<int>(EffectKind::Tape) && heading("DELAY", kBrass) && pickName(5, "DELAY", kBrass) && typesOn(5, 1) && !typesOn(5, 0));
    CHECK(inked(canvas, scratch, 0, typeX, typeY, "CLEAN", kDim) && inked(canvas, scratch, 0, tapeX, typeY, "TAPE", kBg) && canvas.is(static_cast<int>(tapeX) - 5, static_cast<int>(typeY) - 4, kBrass));
    CHECK(inked(canvas, scratch, 0, F::kNameX + fonts.medium.width("DELAY") + 7.f, F::lineY(1) + smallMid + 1.f, "TAPE", kDim));
    rig.key(A, false);
    rig.frames(1);
    draw();
    // A let go: the list shuts and the settings are back; the plate is cream.
    CHECK(!pickName(5, "DELAY", kBrass) && !pickName(5, "DELAY", kText) && heading("DELAY", kText) && canvas.is(static_cast<int>(tapeX) - 5, static_cast<int>(typeY) - 4, kText));
    CHECK(inked(canvas, scratch, 0, F::dialCX(0) - fonts.small.width("TIME") / 2.f, static_cast<float>(F::kDialNameY), "TIME", kDim));

    // A BUS holds one family: its list is two lines, EMPTY and REVERB, and nothing under them.
    fx.goTo(F::kBusRow + 1, 0);
    rig.key(A, true);
    draw();
    CHECK(pickName(0, "EMPTY", kText) && pickName(1, "REVERB", kBrass) && canvas.is(F::kPaneX + 3, pickY(1) + 10, kBrass));
    CHECK(typesOn(1, 1, 1) && inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(0) + pickSmallMid, "nothing here", kDim)); // the bus's HALL lit
    CHECK(inked(canvas, scratch, 0, F::kPickTypesX + fonts.small.width("room") + fonts.small.width(", "), pickY(1) + pickSmallMid, "hall", kBrass) && inked(canvas, scratch, 0, static_cast<float>(F::kPickTypesX), pickY(1) + pickSmallMid, "room", kDim));
    for (int place = 2; place < F::kPickLines; ++place)
        for (int x = F::kPaneX; x < F::kPaneX + F::kPaneW; x += 3) CHECK(canvas.is(x, pickY(place) + 14, kBg));
    CHECK(heading("REVERB", kBrass) && inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kPaneY + 10.f, "SPACE BUS: WHAT THE TRACKS SEND TO SPACE", kDim));
    rig.key(A, false);
    rig.frames(1);

    // THE SETTINGS, as dials under the picture (owner, 2026-11-24, design B). On TAPE's FEEDBACK: its name, its
    // reading, a ring lit as far round as the setting.
    fx.goTo(0, 2);
    draw();
    const auto ring = [&](int p, float part, SDL_Color c) { const SDL_Point at = F::dialAt(p, part); return canvas.is(at.x, at.y, c); };
    const auto named = [&](int p, const char* name, SDL_Color c) { return inked(canvas, scratch, 0, F::dialCX(p) - fonts.small.width(name) / 2.f, static_cast<float>(F::kDialNameY), name, c); };
    const auto valued = [&](int p, const char* value, SDL_Color c) {
        const Font& font = fonts.fit(value, static_cast<float>(F::kValueRoom));
        const int size = &font == &fonts.large ? 2 : (&font == &fonts.medium ? 1 : 0);
        return inked(canvas, scratch, size, F::dialCX(p) - font.width(value) / 2.f, static_cast<float>(F::kDialValueY), value, c);
    };
    const char* labels[4] = {"TIME", "FEEDBACK", "AGE", "MIX"};
    const char* values[4] = {"1/8 DOT", "48%", "35%", "35%"};
    for (int p = 0; p < 4; ++p) {
        CHECK(named(p, labels[p], p == 1 ? kText : kDim));
        CHECK(valued(p, values[p], p == 1 ? kTeal : kText));
        CHECK(canvas.is(F::dialX(p) - 1, F::kDialY + 20, kTeal) == (p == 1)); // the cursor's box
    }
    CHECK(ring(1, 0.36f, kTeal) && ring(1, 0.44f, kLine) && ring(1, 0.f, kTeal) && ring(1, 1.f, kLine));
    CHECK(ring(2, 0.30f, kText) && ring(2, 0.40f, kLine));
    CHECK(F::kDialY + F::kDialH == F::kPageBottom && canvas.is(F::kPaneX + 50, F::kPageBottom - 1, kLine) && canvas.is(F::kPaneX + 50, F::kPageBottom, kBg)); // to the page's foot
    // The ring's foot is open: nothing lit or unlit straight under the centre.
    CHECK(!canvas.is(F::dialCX(1), F::kDialCY + F::kDialR - 3, kTeal) && !canvas.is(F::dialCX(1), F::kDialCY + F::kDialR - 3, kLine));
    // TIME is a CHOICE: no ring to be somewhere along, but a dot for each of its eight, the fifth lit.
    for (int c = 0; c < 8; ++c) CHECK(ring(0, c / 7.f, c == 4 ? kText : kLine));
    // The cursor on the choice lights its dot teal; turned to the last, the last is lit.
    fx.goTo(0, 1);
    session.setEffectParam(0, 0, 0, 0.99f);
    draw();
    CHECK(ring(0, 1.f, kTeal) && ring(0, 0.f, kLine));
    session.setEffectParam(0, 0, 0, 0.f);
    draw();
    CHECK(ring(0, 0.f, kTeal) && ring(0, 1.f, kLine));
    // With A held the cursor and what it turns are brass.
    rig.key(A, true);
    draw();
    CHECK(ring(0, 0.f, kBrass) && canvas.is(F::dialX(0) - 1, F::kDialY + 20, kBrass) && !pickName(1, "FILTER", kText));
    rig.key(A, false);
    rig.frames(1);
    // On a bus the MIX reads LEVEL; on a track it reads MIX.
    fx.goTo(F::kBusRow, 4);
    draw();
    CHECK(named(3, "LEVEL", kText) && !named(3, "MIX", kText));
    CHECK(valued(3, "100%", kTeal) && ring(3, 1.f, kTeal));
    // The room reverb has three settings: the fourth place is empty.
    session.setEffect(kSpaceBus, 0, EffectKind::Reverb);
    fx.goTo(F::kBusRow + 1, 3);
    draw();
    CHECK(named(2, "LEVEL", kText) && canvas.is(F::dialCX(3), F::kDialCY, kBg) && canvas.is(F::dialX(3), F::kDialY + 20, kPanel2));

    // WHAT MOVES WITH THE PATTERN is marked in purple: in its dial, its name and a line at the foot, and on its line of the list.
    fx.goTo(0, 2);
    draw();
    const auto autoLine = [&](int p) { return canvas.is(F::dialCX(p), F::kAutoY, kPurple); };
    CHECK(!autoLine(1) && !canvas.is(F::kListX + 50, F::lineY(0) + F::kLineH - 3, kPurple));
    session.setAutomation(0, 0, autoLaneOfEffect(0, 1), 5, 0.3f);
    draw();
    CHECK(autoLine(1) && !autoLine(0) && !autoLine(2));
    CHECK(named(1, "FEEDBACK", kPurple) && named(2, "AGE", kDim));
    CHECK(canvas.is(F::kListX + 50, F::lineY(0) + F::kLineH - 3, kPurple) && !canvas.is(F::kListX + 50, F::lineY(2) + F::kLineH - 3, kPurple) && !canvas.is(F::kListX + 50, F::lineY(3) + F::kLineH - 3, kPurple));
    // ...in the scene being worked on, not another.
    rig.project().scene = 1;
    draw();
    CHECK(!autoLine(1) && !canvas.is(F::kListX + 50, F::lineY(0) + F::kLineH - 3, kPurple));
    rig.project().scene = 0;
    // ...and not under a line trying another effect: that one's lanes are not what it has.
    fx.goTo(0, 0);
    rig.key(A, true);
    rig.tap(DOWN);
    CHECK(fx.trying());
    draw();
    CHECK(!canvas.is(F::kListX + 50, F::lineY(0) + F::kLineH - 3, kPurple));
    rig.tap(B);
    rig.key(A, false);
    draw();
    CHECK(canvas.is(F::kListX + 50, F::lineY(0) + F::kLineH - 3, kPurple));

    // THE SENDS, large: two rows, where each goes, and the automated one marked.
    session.setSend(0, 0, 0.35f);
    session.setSend(0, 1, 0.1f);
    session.setAutomation(0, 0, autoLaneOfSend(1), 0, 0.5f);
    fx.goTo(F::kSendsRow, 1);
    draw();
    const auto sendY = [&](int b) { return F::kSendY + b * (F::kSendH + F::kSendGap); };
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 1.f, F::kPaneY + 2.f, "1 DRUMS, SENDS", kDim));
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, sendY(0) + 12.f, "ECHO", kDim) && inked(canvas, scratch, 0, F::kPaneX + 12.f, sendY(1) + 12.f, "SPACE", kDim));
    CHECK(inked(canvas, scratch, 2, F::kPaneX + 12.f, sendY(0) + static_cast<float>(F::kSendValueY), "35%", kTeal) && inked(canvas, scratch, 2, F::kPaneX + 12.f, sendY(1) + static_cast<float>(F::kSendValueY), "10%", kText));
    CHECK(canvas.run(F::kPaneX + 12, sendY(0) + F::kSendBarY + 6, kTeal) == static_cast<int>(std::round((F::kPaneW - 24) * 0.35f)) && canvas.run(F::kPaneX + 12, sendY(1) + F::kSendBarY + 6, kDim) == static_cast<int>(std::round((F::kPaneW - 24) * 0.1f)));
    CHECK(canvas.run(F::kPaneX + 12, sendY(0) + F::kSendBarY - 1, kTeal) == 0 && canvas.run(F::kPaneX + 12, sendY(0) + F::kSendBarY + 14, kTeal) == 0); // the bar: 14 high, where the constant says
    const char* toEcho = "to the ECHO bus: DELAY, TAPE";
    const char* toSpace = "to the SPACE bus: REVERB, ROOM";
    CHECK(inked(canvas, scratch, 0, F::kPaneX + F::kPaneW - 12.f - fonts.small.width(toEcho), sendY(0) + 12.f, toEcho, kDim));
    CHECK(inked(canvas, scratch, 0, F::kPaneX + F::kPaneW - 12.f - fonts.small.width(toSpace), sendY(1) + 12.f, toSpace, kDim));
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, sendY(1) + static_cast<float>(F::kSendWordsY), "moves with the pattern", kPurple) && !inked(canvas, scratch, 0, F::kPaneX + 12.f, sendY(0) + static_cast<float>(F::kSendWordsY), "moves with the pattern", kPurple));
    CHECK(canvas.is(F::kPaneX + 50, sendY(1) + F::kSendLineY, kPurple) && !canvas.is(F::kPaneX + 50, sendY(0) + F::kSendLineY, kPurple) && canvas.is(F::kListX + 50, F::lineY(3) + F::kLineH - 3, kPurple));
    CHECK(canvas.is(F::kPaneX - 1, sendY(0) + 20, kTeal) && !canvas.is(F::kPaneX - 1, sendY(1) + 20, kTeal));
    CHECK(sendY(1) + F::kSendH == F::kPageBottom && canvas.is(F::kPaneX + 50, F::kPageBottom - 1, kLine) && canvas.is(F::kPaneX + 50, F::kPageBottom, kBg)); // to the page's foot
    {
        // In the taller row the value stands about midway between the row's name and its bar (within 10 pixels).
        const float between = (12.f + fonts.small.capHeight() + F::kSendBarY) / 2.f, value = F::kSendValueY + fonts.large.capHeight() / 2.f;
        CHECK(std::fabs(value - between) <= 10.f);
    }
    // The two little bars on the list's line say the same.
    CHECK(canvas.run(F::kNameX + 14, F::lineY(3) + F::kLineH / 2, kText) == static_cast<int>(std::round(38 * 0.35f)) && canvas.run(F::kNameX + 62 + 14, F::lineY(3) + F::kLineH / 2, kText) == static_cast<int>(std::round(38 * 0.1f)));
    // An empty bus says so, where the send would go.
    session.setEffect(kEchoBus, 0, EffectKind::None);
    draw();
    const char* noEcho = "the ECHO bus is EMPTY";
    CHECK(inked(canvas, scratch, 0, F::kPaneX + F::kPaneW - 12.f - fonts.small.width(noEcho), sendY(0) + 12.f, noEcho, kDim));
    // The level mark on a line: as tall as the MIX (or LEVEL) is, from the bottom.
    session.setEffect(kEchoBus, 0, EffectKind::Tape);
    session.setEffectParam(kEchoBus, 0, 3, 0.5f);
    draw();
    const int markH = F::kLineH - 14, half = static_cast<int>(std::round(markH * 0.5f));
    CHECK(canvas.is(F::kMarkX, F::lineY(4) + 7 + markH - 1, kDim) && canvas.is(F::kMarkX, F::lineY(4) + 7 + markH - half, kDim) && canvas.is(F::kMarkX, F::lineY(4) + 7 + markH - half - 1, kLine) && canvas.is(F::kMarkX, F::lineY(4) + 7, kLine));
    CHECK(canvas.is(F::kMarkX, F::lineY(2) + 7, kDim)); // the FILTER's MIX is full
    CHECK(!canvas.is(F::kMarkX, F::lineY(1 + 5) + 7 + 5, kLine) && !canvas.is(F::kMarkX, F::lineY(6) + 7 + 5, kDim)); // an empty slot has no mark

    // THE HINTS say what the buttons do where the cursor is. (Their words; the bar's drawing is ui_test's.)
    const auto hinted = [&](const char* words) {
        // somewhere along the bar, in the hint's own colour
        for (float x = 4.f; x < kScreenW - 40.f; x += 0.0625f)
            if (inked(canvas, scratch, 0, x, kScreenH - kHintsH + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f, words, kDim)) return true;
        return false;
    };
    fx.goTo(0, 0);
    draw();
    const bool barFound = hinted("effects");
    CHECK(barFound);
    if (barFound) {
        CHECK(hinted("settings") && hinted("delete") && hinted("move") && !hinted("play") && !hinted("default") && !hinted("turn"));
        fx.goTo(1, 0);
        session.setEffect(0, 1, EffectKind::None);
        draw();
        CHECK(!hinted("settings") && hinted("effects")); // an empty slot has none to go into
        fx.goTo(F::kSendsRow, 0);
        draw();
        CHECK(hinted("sends") && hinted("send nothing") && !hinted("effects"));
        fx.goTo(F::kSendsRow, 2);
        draw();
        CHECK(hinted("list") && hinted("turn") && hinted("none") && !hinted("default"));
        fx.goTo(0, 2);
        draw();
        CHECK(hinted("list") && hinted("turn, type") && hinted("default")); // (DELAY has types: HOLD A turns and switches)
        rig.key(A, true);
        draw();
        CHECK(hinted("turn it") && !hinted("list") && !hinted("effect"));
        fx.goTo(0, 0);
        draw();
        CHECK(hinted("effect") && hinted("type") && !hinted("turn it")); // DELAY has types
        fx.goTo(2, 0);
        draw();
        CHECK(hinted("effect") && hinted("type")); // FILTER has COMB, VOWEL and EQ too
        rig.key(A, false);
        rig.frames(1);
        session.setEffect(0, 2, EffectKind::Chorus);
        rig.key(A, true);
        draw();
        CHECK(hinted("effect") && hinted("type")); // MOD has five since batch c1: every family has types now
        rig.key(A, false);
        rig.frames(1);
    }
}

// CHANGING TYPE through the Session: the settings the types share are kept as they read, and the
// automation written for them goes with them; what the new type does not have is cleared.
void testChangingTypeKeepsWhatIsShared() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    TrackData& track = project.tracks[0];
    const auto written = [&](int lane, int scene = 0) { int n = 0; for (float v : track.automation[scene][lane]) n += v >= 0.f ? 1 : 0; return n; };
    const auto reads = [&](EffectKind kind, int index, float v) { char text[32]; effectParamText(kind, index, v, text, sizeof text); return std::string(text); };
    const auto lane = [](int slot, int p) { return autoLaneOfEffect(slot, p); };
    const EffectKind D = EffectKind::Delay, T = EffectKind::Tape, R = EffectKind::Reverb, H = EffectKind::Hall;
    const EffectData& slot = track.effects[0];

    // TAPE on the track, turned, with automation on three of its settings and on its neighbours.
    session.setEffect(0, 0, T);
    session.setEffect(0, 1, EffectKind::Filter);
    session.setEffectParam(0, 0, 0, 4.5f / 8.f); // 1/8 DOT
    session.setEffectParam(0, 0, 1, 0.4f);       // 48%
    session.setEffectParam(0, 0, 2, 0.8f);       // AGE
    session.setEffectParam(0, 0, 3, 0.7f);       // MIX
    session.setAutomation(0, 0, lane(0, 1), 2, 0.4f);  // FEEDBACK 48%
    session.setAutomation(0, 0, lane(0, 1), 5, 1.f);   // ...and 120%
    session.setAutomation(0, 3, lane(0, 1), 100, 0.25f); // another scene, a later bar: 30%
    session.setAutomation(0, 3, lane(0, 3), 7, 0.25f); // MIX
    session.setAutomation(0, 0, lane(0, 2), 1, 0.5f);  // AGE: DELAY has none
    session.setAutomation(0, 6, lane(0, 2), 9, 0.5f);
    session.setAutomation(0, 0, lane(0, 0), 4, 1.5f / 8.f); // TIME: 1/8 TRIP
    session.setAutomation(0, 4, lane(0, 3), 8, 0.f);   // MIX swept right down to 0%: a step that is written, though it is nothing
    session.setAutomation(0, 4, lane(0, 1), 8, 0.f);   // ...and FEEDBACK to 0%
    session.setAutomation(0, 0, lane(1, 1), 1, 0.6f);  // the next slot's
    session.setAutomation(0, 0, autoLaneOfSend(0), 1, 0.6f);
    session.setAutomation(0, 0, 2, 1, 0.6f);           // a macro's
    rig.frames(1);
    rig.render(2000);
    CHECK(changeEffectType(session, 0, 0, D));
    CHECK(slot.kind == static_cast<int>(D));
    CHECK(reads(D, 0, slot.param[0]) == "1/8 DOT" && reads(D, 1, slot.param[1]) == "48%" && slot.param[2] == effectInfo(D).defaults[2] && slot.param[3] == 0.7f);
    // The automation: FEEDBACK reads as it did (and 120% is DELAY's most), MIX is as it was, TIME is the nearest DELAY has; AGE's is gone, everywhere.
    CHECK(written(lane(0, 1)) == 2 && reads(D, 1, track.automation[0][lane(0, 1)][2]) == "48%" && track.automation[0][lane(0, 1)][5] == 1.f);
    CHECK(written(lane(0, 1), 3) == 1 && reads(D, 1, track.automation[3][lane(0, 1)][100]) == "30%" && std::fabs(track.automation[3][lane(0, 1)][100] - 0.3f) < 0.005f);
    CHECK(written(lane(0, 3), 3) == 1 && track.automation[3][lane(0, 3)][7] == 0.25f);
    CHECK(written(lane(0, 0)) == 1 && reads(D, 0, track.automation[0][lane(0, 0)][4]) == "1/16");
    CHECK(written(lane(0, 3), 4) == 1 && track.automation[4][lane(0, 3)][8] == 0.f && written(lane(0, 1), 4) == 1 && track.automation[4][lane(0, 1)][8] == 0.f); // a 0 is a step too
    CHECK(written(lane(0, 2)) == 0 && written(lane(0, 2), 6) == 0); // DELAY's TONE starts with nothing written
    CHECK(written(lane(1, 1)) == 1 && written(autoLaneOfSend(0)) == 1 && written(2) == 1 && track.effects[1].kind == static_cast<int>(EffectKind::Filter));
    // The engine has the new type and its settings: the sound is a clean delay's (a hit comes back three steps on, at 48% of ... loud enough to hear).
    rig.frames(1);
    rig.render(20000);
    // And back: 48% is 48% again, the TIME DELAY had is on the TAPE, AGE starts at its usual value.
    CHECK(changeEffectType(session, 0, 0, T) && slot.kind == static_cast<int>(T));
    CHECK(reads(T, 0, slot.param[0]) == "1/8 DOT" && reads(T, 1, slot.param[1]) == "48%" && slot.param[2] == effectInfo(T).defaults[2] && slot.param[3] == 0.7f);
    CHECK(reads(T, 1, track.automation[0][lane(0, 1)][2]) == "48%" && reads(T, 1, track.automation[0][lane(0, 1)][5]) == "100%" && reads(T, 0, track.automation[0][lane(0, 0)][4]) == "1/16");
    CHECK(written(lane(0, 1)) == 2 && written(lane(0, 1), 3) == 1 && written(lane(0, 3), 3) == 1 && written(lane(0, 2)) == 0 && written(lane(1, 1)) == 1);
    CHECK(track.automation[4][lane(0, 3)][8] == 0.f && track.automation[4][lane(0, 1)][8] == 0.f);

    // REVERB and HALL: MIX is their third and fourth setting, so its automation moves a lane; TONE's does not become MIX's.
    session.setEffect(0, 2, H);
    session.setEffectParam(0, 2, 0, 0.9f); // SIZE
    session.setEffectParam(0, 2, 3, 0.2f); // MIX
    session.setAutomation(0, 0, lane(2, 0), 3, 0.4f); // SIZE
    session.setAutomation(0, 0, lane(2, 1), 3, 0.4f); // DECAY
    session.setAutomation(0, 0, lane(2, 2), 4, 0.9f); // TONE
    session.setAutomation(0, 0, lane(2, 3), 6, 0.3f); // MIX
    session.setAutomation(0, 9, lane(2, 3), 60, 0.8f);
    CHECK(changeEffectType(session, 0, 2, R));
    const EffectData& third = track.effects[2];
    CHECK(third.kind == static_cast<int>(R) && third.param[0] == 0.9f && third.param[1] == effectInfo(R).defaults[1] && third.param[2] == 0.2f);
    CHECK(written(lane(2, 0)) == 1 && track.automation[0][lane(2, 0)][3] == 0.4f && written(lane(2, 1)) == 0); // SIZE kept; DAMPING is not DECAY
    CHECK(written(lane(2, 2)) == 1 && track.automation[0][lane(2, 2)][6] == 0.3f && track.automation[0][lane(2, 2)][4] < 0.f); // MIX's, not TONE's
    CHECK(written(lane(2, 2), 9) == 1 && track.automation[9][lane(2, 2)][60] == 0.8f && written(lane(2, 3)) == 0 && written(lane(2, 3), 9) == 0);
    CHECK(changeEffectType(session, 0, 2, H) && third.kind == static_cast<int>(H) && third.param[0] == 0.9f && third.param[3] == 0.2f && third.param[1] == effectInfo(H).defaults[1] && third.param[2] == effectInfo(H).defaults[2]);
    CHECK(written(lane(2, 3)) == 1 && track.automation[0][lane(2, 3)][6] == 0.3f && written(lane(2, 3), 9) == 1 && written(lane(2, 2)) == 0 && written(lane(2, 2), 9) == 0 && written(lane(2, 0)) == 1);
    // The first slot's were not touched by any of that.
    CHECK(written(lane(0, 1)) == 2 && written(lane(0, 0)) == 1 && written(lane(1, 1)) == 1);

    // With WRITE on and the song playing, none of it is written down as a move.
    session.setWriting(true);
    rig.tap(START);
    rig.render(kStep * 9 + kStep / 2);
    rig.frames(1);
    CHECK(project.playing && session.writing());
    CHECK(changeEffectType(session, 0, 0, D) && session.writing());
    CHECK(written(lane(0, 1)) == 2 && written(lane(0, 0)) == 1 && written(lane(0, 3)) == 0 && written(lane(0, 2)) == 0 && track.automation[0][lane(0, 1)][9] < 0.f);
    rig.tap(START);
    session.setWriting(false);
    rig.frames(1);

    // What is not a change of type is refused, and nothing is touched.
    const EffectData before = slot;
    CHECK(!changeEffectType(session, 0, 0, D));                 // what it is already
    CHECK(!changeEffectType(session, 0, 0, EffectKind::None) && !changeEffectType(session, 0, 0, EffectKind::Filter) && !changeEffectType(session, 0, 0, H));
    CHECK(!changeEffectType(session, 0, 0, static_cast<EffectKind>(77)) && !changeEffectType(session, 0, 0, static_cast<EffectKind>(-2)));
    CHECK(!changeEffectType(session, 0, 3, T) && !changeEffectType(session, 0, -1, T) && !changeEffectType(session, kMasterTrack, 2, T) && !changeEffectType(session, kEchoBus, 1, D));
    CHECK(slot.kind == before.kind && slot.param[0] == before.param[0] && slot.param[1] == before.param[1] && slot.param[3] == before.param[3] && written(lane(0, 1)) == 2);
    session.setEffect(0, 1, EffectKind::None);
    CHECK(!changeEffectType(session, 0, 1, T) && track.effects[1].kind == 0); // nothing in the slot: no type to change
    CHECK(!changeEffectType(session, 0, 1, EffectKind::Filter));

    // On a bus: LEVEL (its MIX) is kept as it was turned, not put back to full; what is new starts as it does on a bus.
    CHECK(project.busEffects[0].kind == static_cast<int>(T));
    session.setEffectParam(kEchoBus, 0, 3, 0.8f);
    session.setEffectParam(kEchoBus, 0, 1, 0.5f); // 60%
    CHECK(changeEffectType(session, kEchoBus, 0, D));
    CHECK(project.busEffects[0].kind == static_cast<int>(D) && project.busEffects[0].param[3] == 0.8f && reads(D, 1, project.busEffects[0].param[1]) == "60%" && project.busEffects[0].param[2] == effectInfo(D).defaults[2]);
    CHECK(project.busEffects[1].kind == static_cast<int>(H));
    session.setEffectParam(kSpaceBus, 0, 3, 0.6f);
    CHECK(changeEffectType(session, kSpaceBus, 0, R) && project.busEffects[1].kind == static_cast<int>(R) && project.busEffects[1].param[2] == 0.6f);
    // On the master, where nothing is automated.
    session.setEffect(kMasterTrack, 1, R);
    session.setEffectParam(kMasterTrack, 1, 2, 0.15f);
    CHECK(changeEffectType(session, kMasterTrack, 1, H) && project.masterEffects[1].kind == static_cast<int>(H) && project.masterEffects[1].param[3] == 0.15f && project.masterEffects[1].param[1] == effectInfo(H).defaults[1]);
    // The engine was told each time, and nothing was lost on the way.
    rig.frames(2);
    rig.render(4000);
    rig.frames(2);
    CHECK(rig.listen(2000) < 4.f);

    // A lane written full, in every scene: all of it goes with the setting (and the engine is sent the whole song if its queue fills).
    session.setEffect(0, 0, T);
    for (int scene = 0; scene < kMaxScenes; ++scene)
        for (int step = 0; step < kAutoSteps; ++step) track.automation[scene][lane(0, 1)][step] = 0.1f + 0.5f * static_cast<float>(step) / kAutoSteps;
    CHECK(changeEffectType(session, 0, 0, D));
    for (int scene = 0; scene < kMaxScenes; scene += 5) {
        CHECK(written(lane(0, 1), scene) == kAutoSteps);
        for (int step = 0; step < kAutoSteps; step += 17) CHECK(reads(D, 1, track.automation[scene][lane(0, 1)][step]) == reads(T, 1, 0.1f + 0.5f * static_cast<float>(step) / kAutoSteps));
    }
    for (int i = 0; i < 40; ++i) {
        rig.frames(1);
        rig.render(256);
    }
}

// MOVING an effect a slot up or down: kind, settings, automation, MODs and remembered types,
// all with it. The ends stop; off a track's three slots it is refused and nothing moves. One
// step of undo, and the engine plays the new order.
void testMovingEffectsKeepsEverythingWithThem() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    TrackData& track = project.tracks[0];
    const auto lane = [](int slot, int p) { return autoLaneOfEffect(slot, p); };
    const auto written = [&](int slot, int scene = 0) { int n = 0; for (int p = 0; p < 4; ++p) for (float v : track.automation[scene][lane(slot, p)]) n += v >= 0.f ? 1 : 0; return n; };
    // TAPE in slot 0 (DELAY remembered, as it was), FILTER in slot 1, each turned; automation on
    // both, in two scenes; a MOD on the TAPE's FEEDBACK.
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 1, 0.3f);
    CHECK(switchEffectType(session, 0, 0, EffectKind::Tape));
    session.setEffect(0, 1, EffectKind::Filter);
    session.setEffectParam(0, 0, 1, 0.4f);
    session.setEffectParam(0, 1, 0, 0.9f);
    session.setAutomation(0, 0, lane(0, 1), 2, 0.5f);
    session.setAutomation(0, 3, lane(1, 0), 9, 0.25f);
    CHECK(session.setMod(0, 0, ModKnob::fresh(kModFirstEffect + 0 * 4 + 1)));
    rig.frames(60); // settled: the setup is no step of undo
    // Down a slot: everything with it.
    CHECK(moveEffectSlot(session, 0, 0, 1));
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Filter) && track.effects[0].param[0] == 0.9f);
    CHECK(track.effects[1].kind == static_cast<int>(EffectKind::Tape) && track.effects[1].param[1] == 0.4f);
    CHECK(track.automation[0][lane(1, 1)][2] == 0.5f && track.automation[3][lane(0, 0)][9] == 0.25f);
    CHECK(written(0) == 0 && written(1, 3) == 0); // ...and nothing left on the old lanes
    CHECK(session.modData(0, 0).target == kModFirstEffect + 1 * 4 + 1);
    float back[kEffectParams];
    CHECK(session.rememberedType(0, 1, EffectKind::Delay, back) && back[1] == 0.3f);
    CHECK(!session.rememberedType(0, 0, EffectKind::Delay, back));
    // The engine plays the new order.
    rig.render(4000);
    CHECK(rig.engine->effectKindIn(0, 0) == static_cast<int>(EffectKind::Filter) && rig.engine->effectKindIn(0, 1) == static_cast<int>(EffectKind::Tape));
    // One step of undo puts it all back; redo puts it forward again.
    rig.frames(60);
    CHECK(session.undo());
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Tape) && track.effects[1].kind == static_cast<int>(EffectKind::Filter));
    CHECK(track.automation[0][lane(0, 1)][2] == 0.5f && session.modData(0, 0).target == kModFirstEffect + 0 * 4 + 1);
    CHECK(session.redo());
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Filter) && track.effects[1].kind == static_cast<int>(EffectKind::Tape));
    // The ends stop, and off the track's slots it is refused: nothing moves.
    CHECK(!moveEffectSlot(session, 0, 0, -1) && !moveEffectSlot(session, 0, 2, 1));
    CHECK(!moveEffectSlot(session, 0, 3, 1) && !moveEffectSlot(session, 0, -1, 1) && !moveEffectSlot(session, 0, 1, 0));
    CHECK(!moveEffectSlot(session, -1, 0, 1) && !moveEffectSlot(session, kNumTracks, 0, 1));
    CHECK(!moveEffectSlot(session, kMasterTrack, 0, 1) && !moveEffectSlot(session, kEchoBus, 0, 1));
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Filter) && track.effects[1].kind == static_cast<int>(EffectKind::Tape) && written(1) == 1);
    // A busy track: four lanes of each slot written at every step of 40 scenes (40960 steps). The move
    // is one image of the track, not a command a step, so nothing is lost from the engine's queue
    // (review 2026-11-23: past 16384 commands the whole song was sent again).
    for (int scene = 0; scene < 40; ++scene)
        for (int slot = 0; slot < 2; ++slot)
            for (int p = 0; p < 4; ++p)
                for (int step = 0; step < kAutoSteps; ++step) track.automation[scene][lane(slot, p)][step] = 0.25f + 0.5f * static_cast<float>(slot);
    session.sendTrackImage(0);
    rig.frames(2);
    rig.render(512);
    const unsigned dropped = rig.engine->dropped(), images = session.imagesSent();
    CHECK(moveEffectSlot(session, 0, 0, 1));
    CHECK(rig.engine->dropped() == dropped && session.imagesSent() == images + 1);
    CHECK(track.automation[39][lane(0, 3)][kAutoSteps - 1] == 0.75f && track.automation[39][lane(1, 3)][kAutoSteps - 1] == 0.25f);
}

// A scene's tempo and meter are steps of undo of their own: no later undo silently reverts them.
void testSceneTempoMeterUndo() {
    Rig rig;
    Session& session = rig.app.session();
    rig.frames(60); // settled: undo starts counting here
    session.setSceneTempo(0, 100);
    rig.frames(60);
    session.setSceneMeter(0, 12);
    rig.frames(60);
    CHECK(rig.project().sceneTempo[0] == 100.f && rig.project().meterOf(0) == 12);
    CHECK(session.undo());
    CHECK(rig.project().meterOf(0) == kSteps && rig.project().sceneTempo[0] == 100.f);
    CHECK(session.undo());
    CHECK(rig.project().sceneTempo[0] == 0.f);
}

// Kit VOLUME/FIT/one-slice and the sampler's CHOP undo and redo like any sound change.
void testLaneAndChopUndo() {
    Rig rig;
    Session& session = rig.app.session();
    CHECK(session.addTrack(TrackKind::Sampler));
    rig.frames(60); // settled: undo starts counting here
    session.setLaneLevel(0, 0, 0.5f);
    session.setLaneFit(0, 0, 8);
    session.setLaneOneSlice(0, 0, true);
    session.setSamplerChop(1, true);
    rig.frames(60);
    CHECK(session.undo());
    CHECK(rig.project().tracks[0].laneLevel[0] == 1.f && rig.project().tracks[0].laneFit[0] == 0);
    CHECK(!rig.project().tracks[0].laneOneSlice[0] && !rig.project().tracks[1].samplerChop);
    CHECK(session.redo());
    CHECK(rig.project().tracks[0].laneLevel[0] == 0.5f && rig.project().tracks[0].laneFit[0] == 8);
    CHECK(rig.project().tracks[0].laneOneSlice[0] && rig.project().tracks[1].samplerChop);
}

// Saving a preset retargets the number without minting an undo step.
void testSavePresetMakesNoStep() {
    Rig rig;
    Session& session = rig.app.session();
    addPhazed(rig);
    session.usePresetFolder(tempFolder("presetnostep"));
    rig.frames(60);
    const int steps = session.undoSteps();
    CHECK(session.savePreset(1, "MINE"));
    rig.frames(60);
    CHECK(session.undoSteps() == steps);
    session.usePresetFolder(tempFolder("savepreset-clean")); // the registry is global: leave it empty for later tests
}

// Setting the cell's bars to what they are mints no step and sends nothing new.
void testSetCellBarsNoOpMakesNoStep() {
    Rig rig;
    Session& session = rig.app.session();
    rig.frames(60);
    const int steps = session.undoSteps();
    session.setCellBars(0, 0, 1);
    rig.frames(60);
    CHECK(session.undoSteps() == steps && rig.project().tracks[0].bars[0] == 1);
}

// The cell mark watches automation: a lone direct write still makes a step, and undoes.
void testAutomationMarkCatchesDirectWrites() {
    Rig rig;
    Session& session = rig.app.session();
    rig.frames(60);
    const int steps = session.undoSteps();
    rig.project().tracks[0].automation[0][autoLaneOfEffect(0, 0)][2] = 0.5f; // past every setter
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 1);
    CHECK(session.undo());
    CHECK(rig.project().tracks[0].automation[0][autoLaneOfEffect(0, 0)][2] < 0.f);
}

// Loading a kit is one step that puts the whole kit back, recordings with it.
void testKitLoadRestoresRecordings() {
    Rig rig;
    Session& session = rig.app.session();
    rig.frames(60); // settled: undo starts counting here
    TrackData kit;
    kit.kind = TrackKind::Drums;
    std::strcpy(kit.laneSample[0], "break");
    kit.laneEdit[0].pitch = 7;
    kit.laneLevel[0] = 0.5f;
    CHECK(session.putKit(0, kit));
    rig.frames(60);
    CHECK(std::strcmp(rig.project().tracks[0].laneSample[0], "break") == 0);
    CHECK(session.undo());
    CHECK(std::strcmp(rig.project().tracks[0].laneSample[0], "break") != 0);
    CHECK(rig.project().tracks[0].laneLevel[0] == 1.f);
    CHECK(session.redo());
    CHECK(std::strcmp(rig.project().tracks[0].laneSample[0], "break") == 0);
}

// EVERY TEXT FITS ITS BOX, at every value: measured with the program's lettering, and looked for on the drawn screen.
void testFxTextsFit() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    using namespace ui;
    typedef FxScreen F;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    // The longest a track's name can be, and the last track's number.
    for (int t = 1; t < kNumTracks; ++t) session.addTrack(TrackKind::Drums);
    project.track = kNumTracks - 1;
    std::snprintf(project.tracks[kNumTracks - 1].name, sizeof project.tracks[kNumTracks - 1].name, "WWWWWWWWWW");
    char name[24], text[96];
    ui::trackName(project, project.track, name, sizeof name);
    CHECK(std::strlen(name) >= 12);
    CHECK(fonts.small.width(name) <= F::kListW - 2.f);
    for (const char* more : {"THIS TRACK", "ALL TRACKS", "EVERYTHING"}) CHECK(fonts.small.width("MASTER") + fonts.small.width(more) + 10.f <= F::kListW - 2.f);
    const float headRoom = F::kPaneW - 24.f;
    std::snprintf(text, sizeof text, "%s, EFFECT 3", name);
    CHECK(fonts.small.width(text) <= headRoom);
    std::snprintf(text, sizeof text, "%s, SENDS", name);
    CHECK(fonts.small.width(text) <= headRoom);
    CHECK(fonts.small.width("SPACE BUS: WHAT THE TRACKS SEND TO SPACE") <= headRoom && fonts.small.width("MASTER (EVERYTHING), EFFECT 2") <= headRoom);
    CHECK(fonts.small.width("Hold A to see what this bus can hold.") <= headRoom && fonts.small.width("Hold A to see the effects and pick one.") <= headRoom);
    CHECK(F::kValueRoom > 80 && F::kValueRoom <= F::kDialW - 8 && F::dialX(3) + F::kDialW <= F::kPaneX + F::kPaneW);

    int values = 0, smaller = 0;
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        // In the list: the family, its type, and room before the marks at the line's end.
        // (A type stands 7 after its family; one too long for that draws its family small too, REVERB SPRING: FxScreen::lineFonts.)
        float gap = 0.f;
        bool familySmall = false;
        bool typeAlone = true;
        CHECK(&F::lineFonts(fonts, kind, &gap, &familySmall, &typeAlone) == &fonts && gap == 7.f);
        CHECK(familySmall == (kind == EffectKind::Spring || kind == EffectKind::Saturate || kind == EffectKind::Limiter || kind == EffectKind::Reverse || kind == EffectKind::Stutter || kind == EffectKind::Bloom || kind == EffectKind::Shimmer)); // the others as they were (LEVEL LIMITER: its meter's mark keeps 7 more; DELAY REVERSE, DELAY STUTTER and REVERB BLOOM as REVERB SPRING)
        CHECK(typeAlone == (kind == EffectKind::Saturate || kind == EffectKind::Shimmer)); // DRIVE SATURATE and REVERB SHIMMER, both small, are too long: SATURATE, SHIMMER alone
        const float nameEnds = typeAlone ? F::kNameX + fonts.medium.width(effectTypeShown(kind))
                                         : F::kNameX + (familySmall ? fonts.small : fonts.medium).width(effectFamily(kind)) + (effectTypeShown(kind)[0] ? gap + fonts.small.width(effectTypeShown(kind)) : 0.f);
        CHECK(nameEnds <= F::kMarkX - (effectMeterParam(kind) >= 0 ? 7 : 0) - 3 + 0.01f);
        if (nameEnds > F::kMarkX - 3) std::printf("    %s %s ends at %.0f, the mark is at %d\n", effectFamily(kind), effectTypeShown(kind), static_cast<double>(nameEnds), F::kMarkX);
        CHECK(F::kListX + 8.f + fonts.small.width("SPACE") <= F::kNameX - 4);
        // In the heading: the family, large, and what its meter says beside it at its longest; its types in a row, and the arrows after them.
        FxScreen::meterWords(kind, 99.f, text, sizeof text);
        CHECK(fonts.large.width(effectFamily(kind)) + 16.f + fonts.small.width(text) <= headRoom);
        float types = fonts.small.width("TYPE") + 12.f;
        std::vector<std::string> typeNames;
        for (int t = 0; t < effectTypes(kind); ++t) types += fonts.small.width(effectTypeName(effectTypeAt(kind, t))) + 20.f, typeNames.push_back(effectTypeName(effectTypeAt(kind, t)));
        // (MOD's, DELAY's, REVERB's and, since BAND, FILTER's six do not all fit: its row shows as many as fit at LARGE, "..." for the rest; textsize_test has it.)
        const bool crowded = effectInfo(kind).family == EffectFamily::Mod || effectInfo(kind).family == EffectFamily::Delay || effectInfo(kind).family == EffectFamily::Reverb ||
                             effectInfo(kind).family == EffectFamily::Filter;
        if (!crowded) CHECK(types + ui::keyWidth(fonts, "LEFT / RIGHT") <= headRoom);
        {
            int from = -1, to = -1; // today's types are all in view, but MOD's, DELAY's and REVERB's
            const Fonts& drawn = F::typeRowView(fonts, typeNames, effectTypeIndex(kind), &from, &to);
            CHECK(F::typeRowWidth(drawn.small, typeNames, from, to) <= F::typeRowRoom(drawn, effectTypes(kind)) && from <= effectTypeIndex(kind) && effectTypeIndex(kind) < to);
            CHECK((from == 0 && to == effectTypes(kind)) == !crowded);
        }
        // In the list of effects: the family, then its types or its words, each in its own column.
        CHECK(F::kPickNameX + fonts.medium.width(effectFamily(kind)) <= F::kPickTypesX - 8);
        effectFamilySays(kind, text, sizeof text);
        CHECK(fonts.small.width(text) <= F::kPickTypesRoom);
        // (MOD's six, since PITCH, take two rows at LARGE: the list's own way for a line too long, pickRows.)
        if (effectFamilyNamesTypes(kind)) {
            const std::vector<std::string> words = F::typeWords(kind);
            const F::PickRows rows = F::pickRows(fonts, words);
            const int types = effectTypes(kind), split = rows.rows == 1 ? types : rows.split;
            CHECK(rows.rows == 1 || effectInfo(kind).family == EffectFamily::Mod);
            CHECK(F::typesWidth(rows.set->small, words, 0, split) <= F::kPickTypesRoom && F::typesWidth(rows.set->small, words, split, types) <= F::kPickTypesRoom);
        }
        // Where a send goes.
        if (effectBus(kind) >= 0) {
            std::snprintf(text, sizeof text, "to the SPACE bus: %s, %s", effectFamily(kind), effectTypeShown(kind));
            CHECK(fonts.small.width("SPACE") + 16.f + fonts.small.width(text) <= headRoom);
        }
        // Its settings: each label, and each reading at 33 places along its range, in the largest lettering that fits the room.
        for (int onBus = 0; onBus < (effectBus(kind) >= 0 ? 2 : 1); ++onBus) {
            const int track = onBus ? busTrack(effectBus(kind)) : project.track, row = onBus ? F::kBusRow + effectBus(kind) : 0;
            session.setEffect(track, 0, kind);
            CHECK(session.effectData(track, 0).kind == k);
            for (int p = 0; p < info.params; ++p) {
                CHECK(fonts.small.width(info.paramNames[p]) <= F::kValueRoom && fonts.small.width("LEVEL") <= F::kValueRoom);
                for (int other = 0; other < 2; ++other) {
                    // (A reading can depend on another setting: the DJ filter's cutoff. Every choice of every other setting is too many; the first and the DJ's are tried.)
                    if (other == 1 && kind != EffectKind::Filter) continue;
                    if (kind == EffectKind::Filter) session.setEffectParam(track, 0, 2, other ? 0.7f : 0.1f);
                    for (int i = 0; i <= 32; ++i) {
                        const float v = static_cast<float>(i) / 32.f;
                        if (kind == EffectKind::Filter && p == 2 && other == 1) continue;
                        session.setEffectParam(track, 0, p, v);
                        const EffectData& d = session.effectData(track, 0);
                        effectParamText(kind, p, v, text, sizeof text, d.param);
                        ++values;
                        CHECK(text[0] != 0);
                        const Font& font = fonts.fit(text, static_cast<float>(F::kValueRoom));
                        CHECK(font.width(text) <= F::kValueRoom);
                        if (font.width(text) > F::kValueRoom) std::printf("    %s %s reads %s: %.0f wide in a room of %d\n", info.name, info.paramNames[p], text, static_cast<double>(font.width(text)), F::kValueRoom);
                        smaller += &font != &fonts.large ? 1 : 0;
                        // ...and on the screen: nothing of it out of its dial's box, nothing past the pane, nothing over the hints.
                        fx.goTo(row, p + 1);
                        canvas.clear();
                        fx.draw(canvas.r, fonts);
                        const int left = F::dialX(p) + 2, right = F::dialX(p) + F::kDialW - 2;
                        bool clean = true;
                        for (int py = F::kDialValueY - 4; py < F::kMeterY - 2 && clean; ++py) {
                            clean = clean && canvas.is(left, py, kPanel) && canvas.is(right - 1, py, kPanel);
                        }
                        for (int py = kTopBarH + 2; py < kScreenH - kHintsH - 1 && clean; py += 2)
                            for (int px = F::kPaneX + F::kPaneW + 1; px < kScreenW; ++px) clean = clean && canvas.is(px, py, kBg);
                        CHECK(clean);
                        if (!clean) std::printf("    %s %s at %.3f (%s) is drawn out of its room\n", info.name, info.paramNames[p], static_cast<double>(v), text);
                        // The value is there, in the cursor's colour.
                        int ink = 0;
                        for (int py = F::kDialValueY; py < F::kMeterY - 2; ++py)
                            for (int px = left; px < right; ++px) ink += canvas.is(px, py, kTeal) ? 1 : 0;
                        CHECK(ink > 6);
                    }
                    session.setEffectParam(track, 0, p, effectDefault(kind, p, onBus != 0));
                }
            }
            rig.frames(1);
            rig.render(512);
        }
        // With A held on its line (the list open): the line of the list it is on is drawn inside the pane.
        fx.goTo(0, 0);
        session.setEffect(project.track, 0, kind);
        rig.key(A, true);
        canvas.clear();
        fx.draw(canvas.r, fonts);
        rig.key(A, false);
        bool inside = true;
        for (int py = kTopBarH + 2; py < kScreenH - kHintsH - 1; ++py)
            for (int px = F::kPaneX + F::kPaneW + 1; px < kScreenW; ++px) inside = inside && canvas.is(px, py, kBg);
        CHECK(inside);
        rig.frames(1);
    }
    CHECK(values > 1300);
    CHECK(smaller < values / 10); // nearly every reading is in the large lettering
    // What a meter says, and when.
    FxScreen::meterWords(EffectKind::Comp, 7.2f, text, sizeof text);
    CHECK(std::string(text) == "TURNING DOWN 7 DB");
    FxScreen::meterWords(EffectKind::Duck, 3.5f, text, sizeof text);
    CHECK(std::string(text) == "DIPPING 4 DB");
    FxScreen::meterWords(EffectKind::Comp, 0.5f, text, sizeof text);
    CHECK(std::string(text) == "TURNING DOWN 1 DB");
    FxScreen::meterWords(EffectKind::Comp, 0.49f, text, sizeof text);
    CHECK(text[0] == 0);
    FxScreen::meterWords(EffectKind::Comp, 1e9f, text, sizeof text);
    CHECK(std::string(text) == "TURNING DOWN 99 DB");
    std::snprintf(text, sizeof text, "x");
    FxScreen::meterWords(EffectKind::Comp, std::nanf(""), text, sizeof text);
    CHECK(text[0] == 0);
    std::snprintf(text, sizeof text, "x");
    FxScreen::meterWords(EffectKind::Filter, 12.f, text, sizeof text);
    CHECK(text[0] == 0);
    FxScreen::meterWords(EffectKind::Comp, -3.f, text, sizeof text);
    CHECK(text[0] == 0);
    char two[4] = "zzz";
    FxScreen::meterWords(EffectKind::Comp, 7.f, two, sizeof two);
    CHECK(std::string(two) == "TUR");
    FxScreen::meterWords(EffectKind::Comp, 7.f, nullptr, 8);
    FxScreen::meterWords(EffectKind::Comp, 7.f, two, 0);
    CHECK(std::string(two) == "TUR");
}

// What the checks above could not tell: each of these was a change to the screen's code that none of them noticed.
void testTheFxScreenHoldsUp() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    using namespace ui;
    typedef FxScreen F;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    const int filter = static_cast<int>(EffectKind::Filter);
    session.setEffect(0, 0, EffectKind::Filter);
    rig.frames(1);
    rig.render(2000);
    const auto draw = [&] {
        canvas.clear();
        fx.draw(canvas.r, fonts);
    };
    const auto ring = [&](int p, float part, SDL_Color c) { const SDL_Point at = F::dialAt(p, part); return canvas.is(at.x, at.y, c); };

    // The SENDS line is not a slot: A held on it picks nothing, in any direction (not into the track's first slot either).
    fx.goTo(F::kSendsRow, 0);
    rig.key(A, true);
    CHECK(!fx.picking());
    for (int b : {DOWN, UP, LEFT, RIGHT}) {
        rig.tap(b);
        CHECK(project.tracks[0].effects[0].kind == filter);
    }
    rig.key(A, false);
    CHECK(project.tracks[0].effects[0].kind == filter && project.tracks[0].effects[1].kind == 0 && project.tracks[0].effects[2].kind == 0);
    CHECK(project.tracks[0].send[0] == 0.f && project.tracks[0].send[1] == 0.f && fx.cursorRow() == F::kSendsRow && fx.cursorColumn() == 0);
    // On that line, still in the list, the two sends are already drawn on the right.
    draw();
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kSendY + 12.f, "ECHO", kDim) && inked(canvas, scratch, 2, F::kPaneX + 12.f, F::kSendY + static_cast<float>(F::kSendValueY), "0%", kText));
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kSendY + F::kSendH + F::kSendGap + 12.f, "SPACE", kDim));

    // A let go while another screen was showing: the list of effects is not left open, and DOWN moves.
    fx.goTo(1, 0);
    rig.key(A, true);
    CHECK(fx.picking());
    session.view = View::Pattern;
    rig.frames(1);
    rig.key(A, false);
    session.view = View::Effects;
    rig.frames(1);
    CHECK(!fx.picking());
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == 2 && project.tracks[0].effects[1].kind == 0);

    // A choice at the very top of its range (a song file can hold 1) lights its last piece, not none.
    session.setEffectParam(0, 0, 2, 1.f);
    fx.goTo(0, 1);
    draw();
    CHECK(project.tracks[0].effects[0].param[2] == 1.f && effectChoices(EffectKind::Filter, 2) == 5);
    CHECK(ring(2, 1.f, kText) && ring(2, 0.f, kLine));

    // While the song plays, an automated setting is drawn where the pattern has it, not where the knob is.
    session.setEffectParam(0, 0, 0, 0.25f);
    for (int step = 0; step < kAutoSteps; ++step) session.setAutomation(0, 0, autoLaneOfEffect(0, 0), step, 0.75f);
    draw();
    fx.goTo(0, 2);
    draw();
    CHECK(ring(0, 0.22f, kText) && ring(0, 0.28f, kLine)); // stopped: the knob
    rig.tap(START);
    rig.render(kStep + kStep / 2);
    rig.frames(1);
    draw();
    CHECK(project.playing && ring(0, 0.72f, kText) && ring(0, 0.78f, kLine));
    rig.tap(START);
    rig.render(2000);
    rig.frames(1);

    // A type is changed only in a slot there is.
    session.setEffect(0, 2, EffectKind::Tape);
    CHECK(!changeEffectType(session, 0, kTrackSlots, EffectKind::Delay) && !changeEffectType(session, 0, -1, EffectKind::Delay));
    CHECK(!changeEffectType(session, kEchoBus, 1, EffectKind::Delay) && !changeEffectType(session, kMasterTrack, kMasterSlots, EffectKind::Delay));
    CHECK(project.tracks[0].effects[2].kind == static_cast<int>(EffectKind::Tape) && project.busEffects[0].kind == static_cast<int>(EffectKind::Tape));
}

// FOUND IN THE SECOND REVIEW, 2026-11-13.
void testTheFxScreenSecondReview() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    using namespace ui;
    typedef FxScreen F;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    const EffectData& slot = project.tracks[0].effects[0];
    const auto reads = [&](int index) { char text[32]; effectParamText(static_cast<EffectKind>(slot.kind), index, slot.param[index], text, sizeof text); return std::string(text); };
    const auto type = [&](SDL_Keycode way) { rig.key(A, true); rig.tap(way); rig.key(A, false); };

    // NO DEAD PUSH after a change of TYPE. FEEDBACK at 45%, to the TAPE and back: it came back as 0.4452 (the low
    // edge of what reads 45%), and the first push up went to 0.45: still 45%, nothing seen to move.
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 1, 0.45f);
    rig.frames(1);
    rig.render(2000);
    fx.goTo(0, 0);
    type(RIGHT);
    CHECK(slot.kind == static_cast<int>(EffectKind::Tape) && reads(1) == "45%" && slot.param[1] == 0.375f);
    type(LEFT);
    CHECK(slot.kind == static_cast<int>(EffectKind::Delay) && reads(1) == "45%" && slot.param[1] == 0.45f); // as it was, to the bit: the song file too
    fx.goTo(0, 2);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(reads(1) == "50%");
    rig.tap(DOWN, 2);
    CHECK(reads(1) == "40%");
    rig.key(A, false);
    // The same from the TAPE's usual 48%, and on the TAPE itself every push is seen. (Emptied first: the slot
    // would otherwise give back the DELAY it remembers, as it was left, testFxSwitchesType.)
    session.setEffect(0, 0, EffectKind::None);
    session.setEffect(0, 0, EffectKind::Tape);
    CHECK(slot.param[1] == 0.4f && reads(1) == "48%");
    fx.goTo(0, 0);
    type(LEFT);
    CHECK(reads(1) == "48%" && slot.param[1] == 0.48f);
    type(RIGHT);
    CHECK(slot.kind == static_cast<int>(EffectKind::Tape) && slot.param[1] == 0.4f);
    session.setEffect(0, 0, EffectKind::Delay);
    for (int k = 0; k <= 20; ++k) {
        // From every twentieth the D-pad leaves DELAY's FEEDBACK on: over to the TAPE, and one push each way is a new reading.
        for (SDL_Keycode way : {UP, DOWN}) {
            if (slot.kind != static_cast<int>(EffectKind::Delay)) { fx.goTo(0, 0); type(LEFT); }
            session.setEffectParam(0, 0, 1, static_cast<float>(k) / 20.f);
            session.forgetTypes(0, 0); // (carried, not the TAPE the slot remembers)
            fx.goTo(0, 0);
            type(RIGHT);
            const std::string was = reads(1);
            fx.goTo(0, 2);
            rig.key(A, true);
            rig.tap(way);
            rig.key(A, false);
            CHECK(reads(1) != was || (way == DOWN && k == 0));
        }
        rig.frames(1);
        rig.render(500);
    }
    session.setEffect(0, 0, EffectKind::None);
    rig.frames(2);
    rig.render(48000);

    // PICKING AN EFFECT OR A TYPE IS HEARD ONLY WHILE THE SONG IS STOPPED: playing, no hit is put on top of the song.
    CHECK(!project.playing && rig.listen(2000) == 0.f);
    fx.goTo(0, 0);
    rig.key(A, true);
    rig.tap(DOWN); // the first of the list goes in, and the track is heard through it
    rig.key(A, false);
    rig.frames(1);
    CHECK(slot.kind != 0 && rig.listen(2000) > 0.01f);
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 3, 0.f); // MIX 0: nothing comes back
    rig.frames(2);
    rig.render(48000 * 4);
    CHECK(rig.listen(2000) < 1e-4f);
    type(RIGHT); // stopped: the TAPE is heard
    rig.frames(1);
    CHECK(slot.kind == static_cast<int>(EffectKind::Tape) && rig.listen(2000) > 0.01f);
    rig.frames(1);
    rig.render(48000 * 4);
    for (auto& bar : project.tracks[0].drums[0]) bar = DrumPattern(); // (an empty scene: the song itself is silence)
    session.applyProject();
    rig.frames(1);
    rig.render(48000);
    rig.tap(START);
    rig.frames(1);
    CHECK(project.playing && rig.listen(2000) < 1e-4f);
    type(LEFT); // playing: CLEAN goes in, and no hit with it
    rig.frames(1);
    CHECK(slot.kind == static_cast<int>(EffectKind::Delay) && rig.listen(4000) < 1e-4f);
    session.setEffect(0, 1, EffectKind::None);
    fx.goTo(1, 0);
    rig.key(A, true);
    rig.tap(DOWN); // ...and an effect picked: none either
    rig.key(A, false);
    rig.frames(1);
    CHECK(project.tracks[0].effects[1].kind != 0 && rig.listen(4000) < 1e-4f);
    rig.tap(START);
    rig.frames(1);
    rig.render(4000);

    // A SEND THAT MOVES WITH THE PATTERN IS DRAWN WHERE THE PATTERN HAS IT while the song plays: the big row and the list's small bar.
    const auto draw = [&] {
        canvas.clear();
        fx.draw(canvas.r, fonts);
    };
    session.setSend(0, 0, 0.25f);
    session.setSend(0, 1, 0.5f);
    for (int step = 0; step < kAutoSteps; ++step) session.setAutomation(0, 0, autoLaneOfSend(0), step, 0.75f);
    rig.frames(1);
    const int wide = F::kPaneW - 24, small = 38;
    const int bigY = F::kSendY + F::kSendBarY + 3, bigY2 = bigY + F::kSendH + F::kSendGap, smallY = F::lineY(F::kSendsRow) + F::kLineH / 2 - 1, smallX = F::kNameX + 14;
    const auto part = [](int width, float v) { return static_cast<int>(std::round(static_cast<float>(width) * v)); };
    fx.goTo(F::kSendsRow, 0);
    draw();
    CHECK(canvas.run(F::kPaneX + 12, bigY, kDim) == part(wide, 0.25f) && canvas.run(smallX, smallY, kText) == part(small, 0.25f)); // stopped: the knob
    CHECK(inked(canvas, scratch, 2, F::kPaneX + 12.f, F::kSendY + static_cast<float>(F::kSendValueY), "25%", kText));
    rig.tap(START);
    rig.render(kStep + kStep / 2);
    rig.frames(1);
    draw();
    CHECK(project.playing && canvas.run(F::kPaneX + 12, bigY, kDim) == part(wide, 0.75f) && canvas.run(smallX, smallY, kText) == part(small, 0.75f));
    CHECK(inked(canvas, scratch, 2, F::kPaneX + 12.f, F::kSendY + static_cast<float>(F::kSendValueY), "75%", kText));
    CHECK(canvas.run(F::kPaneX + 12, bigY2, kDim) == part(wide, 0.5f) && canvas.run(smallX + 62, smallY, kText) == part(small, 0.5f)); // SPACE's is not automated: the knob
    fx.goTo(0, 0); // the cursor elsewhere: the small bar is dim, and still the pattern's
    draw();
    CHECK(canvas.run(smallX, smallY, kDim) == part(small, 0.75f));
    rig.tap(START);
    rig.render(2000);
    rig.frames(1);
    CHECK(project.tracks[0].send[0] == 0.25f); // the knob itself was never moved
}


// A SONG FILE IS WRITTEN AS IT WAS before there were families: every effect by its own word, as main wrote it.
#include "everykind.inc"
void testSongFilesAreAsTheyWere() {
    static const char* const asMainWroteIt =
#include "everykind_main.inc"
        ;
    std::unique_ptr<Project> project(new Project);
    fillWithEveryKind(*project);
    const std::string text = songText(*project);
    CHECK(text == asMainWroteIt);
    // ...and read back it is the same song, and written again the same file.
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(songText(*back) == asMainWroteIt);
    for (int k = 1; k < kKindsMainWrote; ++k) {
        const EffectData& a = project->tracks[(k - 1) / kTrackSlots].effects[(k - 1) % kTrackSlots];
        const EffectData& b = back->tracks[(k - 1) / kTrackSlots].effects[(k - 1) % kTrackSlots];
        CHECK(b.kind == k && a.kind == k);
        for (int i = 0; i < effectInfo(static_cast<EffectKind>(k)).params; ++i) CHECK(near(a.param[i], b.param[i]));
    }
    CHECK(back->busEffects[0].kind == static_cast<int>(EffectKind::Delay) && back->busEffects[1].kind == static_cast<int>(EffectKind::Reverb));
    CHECK(back->masterEffects[0].kind == static_cast<int>(EffectKind::Hall) && back->masterEffects[1].kind == static_cast<int>(EffectKind::Tape));
    // An effect newer than that file (PING, SPRING, PLATE, and any to come): written by its own word, in a slot and on its bus, and read back the same.
    CHECK(std::string(effectInfo(EffectKind::Ping).word) == "ping" && std::string(effectInfo(EffectKind::Spring).word) == "spring" && std::string(effectInfo(EffectKind::Plate).word) == "plate");
    for (int k = kKindsMainWrote; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        std::unique_ptr<Project> newer(new Project), read(new Project);
        newer->tracks[0].kind = TrackKind::Drums;
        EffectData& d = newer->tracks[0].effects[1];
        d.kind = k;
        for (int i = 0; i < effectInfo(kind).params; ++i) d.param[i] = 0.07f * static_cast<float>(k % 10 + 1) + 0.031f * static_cast<float>(i);
        if (effectBus(kind) >= 0) newer->busEffects[effectBus(kind)] = d;
        const std::string wrote = songText(*newer);
        CHECK(wrote.find(std::string("\nfx 2 ") + effectInfo(kind).word + " ") != std::string::npos);
        if (effectBus(kind) >= 0) CHECK(wrote.find("\nbusfx " + std::to_string(effectBus(kind) + 1) + " " + effectInfo(kind).word + " ") != std::string::npos);
        CHECK(parseSong(wrote, *read) && songText(*read) == wrote && read->tracks[0].effects[1].kind == k);
        for (int i = 0; i < effectInfo(kind).params; ++i) CHECK(near(read->tracks[0].effects[1].param[i], d.param[i]));
        if (effectBus(kind) >= 0) CHECK(read->busEffects[effectBus(kind)].kind == k);
    }
    // A change of TYPE on the screen writes the other type's own word, and nothing else about the line is new.
    Rig rig;
    rig.app.session().view = View::Effects;
    rig.app.session().setEffect(0, 0, EffectKind::Delay);
    CHECK(songText(rig.project()).find("\nfx 1 delay 0.5 0.45 0.6 0.35\n") != std::string::npos);
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(songText(rig.project()).find("\nfx 1 tape ") != std::string::npos && songText(rig.project()).find("fx 1 delay") == std::string::npos);
    CHECK(songText(rig.project()).find("\nbusfx 1 tape ") != std::string::npos && songText(rig.project()).find("\nbusfx 2 hall ") != std::string::npos);
    // ...and one put there on the screen (PLATE) is written by its own word, and read back as itself.
    rig.app.session().setEffect(0, 1, EffectKind::Plate);
    CHECK(songText(rig.project()).find("\nfx 2 plate 0.1 0.45 0.6 0.3\n") != std::string::npos);
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(songText(rig.project()), *again) && again->tracks[0].effects[1].kind == static_cast<int>(EffectKind::Plate) && again->tracks[0].effects[1].param[1] == 0.45f);
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

// A built-in sound picked after the macros were turned: the engine must rest the macros where the screen shows
// them (the middle). It kept the old sound's knobs, and a song that wrapped from a scene with macro automation
// into one without played the old sound's macros there (owner, 2026-11-14, "went back to a previous sound").
// The song, through the Session, beside one whose knobs were never turned: the same to the sample, after the wrap.
void testABuiltInPresetRestsTheMacrosWhereTheyShow() {
    const TrackKind kinds[] = {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6};
    for (TrackKind kind : kinds) {
        const auto song = [kind](bool turned) {
            Rig* rig = new Rig;
            Session& s = rig->app.session();
            Project& p = rig->project();
            s.addTrack(kind);
            rig->frames(2);
            s.setEffect(1, 0, EffectKind::Filter);
            s.setEffectParam(1, 0, 0, 0.3f);
            TrackData& synth = p.tracks[1];
            synth.notes[0][0].notes[0] = NoteEvent{0, 48, 6.f, 0.f, 0.8f, 0, 0, 0, 0};
            synth.notes[1][0].notes[0] = NoteEvent{8, 48, 16.f, 0.f, 0.8f, 0, 0, 0, 0}; // rings over the wrap
            for (int m = 0; m < kSynthMacros; ++m)
                for (int step = 0; step < 16; ++step) synth.automation[1][m][step] = 0.3f; // the second scene only
            p.scene = 0;
            p.track = 1;
            s.applyProject();
            rig->frames(2);
            if (turned) {
                for (int m = 0; m < kSynthMacros; ++m) s.setSynthMacro(1, m, m % 2 ? 0.95f : 0.05f);
                rig->frames(2);
            }
            s.chooseSynthPreset(1, 1 % specFor(kind)->presetCount()); // (the JX-3P has one until the plan's batch 7: the first again)
            rig->frames(2);
            CHECK(p.tracks[1].macro[0] == 0.5f && p.sceneCount() == 2);
            s.togglePlay(true);
            std::vector<float> out;
            for (int b = 0; b < 48 * 6000 / 768; ++b) { // scene 1, scene 2 and scene 1 again, a frame at a time
                const std::vector<float> got = rig->render(768);
                out.insert(out.end(), got.begin(), got.end());
                rig->frames(1);
            }
            delete rig;
            return out;
        };
        const std::vector<float> plain = song(false), turned = song(true);
        CHECK(Rig::peak(plain, 32 * 6000) > 0.005f);
        CHECK(plain == turned);
    }
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
    CHECK(track.preset == builtInPresets(TrackKind::Phazed) - 1); // round the end
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
    CHECK(std::strcmp(phazedSpec().presetCategory(track.preset), phazedSpec().presetCategory(builtInPresets(TrackKind::Phazed) - 1)) == 0);
    CHECK(track.preset == 0 + builtInPresets(TrackKind::Phazed) - 1 || std::strcmp(phazedSpec().presetCategory(track.preset - 1), phazedSpec().presetCategory(track.preset)) != 0);
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

void testSavePresetOnDevice() {
    std::unique_ptr<Rig> rig(onPhazed());
    Session& session = rig->app.session();
    SynthScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    const std::string folder = tempFolder("savepreset");
    session.usePresetFolder(folder);
    // R1 on PRESET opens the name keys, on a built-in with nothing typed; A types, Y deletes. With nothing
    // typed B leaves, and nothing is saved.
    CHECK(screen.cursor() == SynthScreen::kPreset);
    rig->tap(R1);
    CHECK(screen.typing() && screen.typed().empty());
    rig->tap(A);
    CHECK(screen.typed() == "A");
    rig->tap(RIGHT);
    rig->tap(A);
    CHECK(screen.typed() == "AB");
    rig->tap(Y);
    CHECK(screen.typed() == "A");
    rig->tap(Y);
    rig->tap(B);
    CHECK(!screen.typing() && userPresets(TrackKind::Phazed).empty() && screen.said().empty());
    // B keeps what is typed (owner, 2026-11-15, "a new one does too"; gesture audit G01): the file, the list, the track on it.
    rig->tap(R1);
    rig->tap(A);
    rig->tap(RIGHT);
    rig->tap(A);
    CHECK(screen.typed() == "AB");
    rig->tap(B);
    CHECK(!screen.typing() && screen.said() == "SAVED AS AB");
    std::string file;
    CHECK(readTextFile(folder + "/AB.phazed", file));
    CHECK(userPresets(TrackKind::Phazed).size() == 1 && track.preset == builtInPresets(TrackKind::Phazed));
    // On one of the player's own, R1 opens with its name typed in: R1 then B saves over it, and says REPLACED.
    const std::string before = file;
    session.setSynthSetting(1, phazed::Level, track.patch.value[phazed::Level] == 3 ? 4 : 3);
    rig->tap(R1);
    CHECK(screen.typing() && screen.typed() == "AB" && screen.keyAt() == 0);
    rig->tap(B);
    CHECK(!screen.typing() && screen.said() == "REPLACED AB" && userPresets(TrackKind::Phazed).size() == 1);
    CHECK(readTextFile(folder + "/AB.phazed", file) && file != before);
    // START keeps too (and does not play): a new name is SAVED AS, a name taken REPLACED.
    rig->tap(R1);
    rig->tap(A);
    CHECK(screen.typed() == "ABA");
    rig->tap(START);
    CHECK(!screen.typing() && screen.said() == "SAVED AS ABA" && userPresets(TrackKind::Phazed).size() == 2 && !rig->project().playing);
    CHECK(track.preset == builtInPresets(TrackKind::Phazed) + 1);
    rig->tap(R1);
    CHECK(screen.typed() == "ABA");
    rig->tap(Y);
    rig->tap(START);
    CHECK(!screen.typing() && screen.said() == "REPLACED AB" && userPresets(TrackKind::Phazed).size() == 2 && track.preset == builtInPresets(TrackKind::Phazed));
    // Empty: START does nothing, B leaves.
    rig->tap(R1);
    rig->tap(Y, 2);
    CHECK(screen.typed().empty());
    rig->tap(START);
    CHECK(screen.typing() && userPresets(TrackKind::Phazed).size() == 2);
    rig->tap(B);
    CHECK(!screen.typing() && userPresets(TrackKind::Phazed).size() == 2);
    // Every synth keeps presets the same way.
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Physic, TrackKind::Acid, TrackKind::Cloud, TrackKind::Morph}) {
        if (track.kind != kind) CHECK(session.setTrackKind(1, kind));
        CHECK(session.savePreset(1, "T"));
        const InstrumentInfo* info = instrument(kind);
        CHECK(info && info->presetEnding);
        bool found = false;
        for (const UserPreset& p : userPresets(kind)) found = found || p.name == "T";
        CHECK(found && readTextFile(folder + "/T" + info->presetEnding, file));
    }
    session.usePresetFolder(tempFolder("savepreset-clean")); // the registry is global: leave it empty for later tests
}

void testSamplerPresetsOnDevice() {
    Rig rig;
    Session& session = rig.app.session();
    SamplerScreen& screen = rig.app.sampler();
    const std::string folder = tempFolder("savesampler");
    session.useSampleFolder(folder + "/samples");
    mkdir((folder + "/samples").c_str(), 0755);
    CHECK(writeWav(folder + "/samples/long.wav", std::vector<float>(4800, 0.25f), 48000));
    session.usePresetFolder(folder + "/presets");
    CHECK(session.addTrack(TrackKind::Sampler)); // track 1
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    session.setTrackSample(1, "long");
    session.setSamplerSetting(1, sampler::Level, 33);
    session.view = View::Device;
    rig.frames(2);
    // The row is below the settings; down from the bottom row reaches it, up goes back.
    screen.goTo(5);
    rig.tap(DOWN);
    CHECK(screen.cursor() == SamplerScreen::kPresetStop);
    rig.tap(UP);
    CHECK(screen.cursor() == 7);
    screen.goTo(SamplerScreen::kPresetStop);
    // R1 on PRESET opens the name keys; A types, Y deletes. With nothing typed B leaves, and nothing is saved.
    rig.tap(R1);
    CHECK(screen.typing() && screen.typed().empty());
    rig.tap(A);
    CHECK(screen.typed() == "A");
    rig.tap(RIGHT);
    rig.tap(A);
    CHECK(screen.typed() == "AB");
    rig.tap(Y);
    CHECK(screen.typed() == "A");
    rig.tap(Y);
    rig.tap(B);
    CHECK(!screen.typing() && session.samplerPresets().empty());
    // B keeps what is typed (gesture audit G01): the file, the list, the track on it.
    rig.tap(R1);
    rig.tap(A);
    rig.tap(RIGHT);
    rig.tap(A);
    rig.tap(B);
    CHECK(!screen.typing() && screen.said() == "SAVED AS AB");
    rig.frames(200);
    CHECK(screen.said().empty()); // the line clears: not only recording synths say things
    std::string file;
    CHECK(readTextFile(folder + "/presets/AB.sampler", file));
    CHECK(session.samplerPresets().size() == 1 && session.samplerPresetOf(1) == 0);
    // Turned off it, the track is on none; under a name taken, it replaces that one.
    session.setSamplerSetting(1, sampler::Tune, track.sampler[sampler::Tune] == 50 ? 51 : 50);
    CHECK(session.samplerPresetOf(1) == -1);
    const std::string before = file;
    rig.tap(R1);
    CHECK(screen.typed().empty()); // (on none: nothing typed in)
    rig.tap(A);
    rig.tap(RIGHT);
    rig.tap(A);
    rig.tap(START);
    CHECK(session.samplerPresets().size() == 1 && screen.said() == "REPLACED AB");
    CHECK(readTextFile(folder + "/presets/AB.sampler", file) && file != before);
    CHECK(session.samplerPresetOf(1) == 0);
    // On one of the player's own, R1 opens with its name typed in: R1 then B saves over it.
    rig.tap(R1);
    CHECK(screen.typing() && screen.typed() == "AB" && screen.keyAt() == 0);
    rig.tap(B);
    CHECK(!screen.typing() && screen.said() == "REPLACED AB" && session.samplerPresets().size() == 1 && session.samplerPresetOf(1) == 0);
    // Empty: START does nothing, B leaves.
    rig.tap(R1);
    rig.tap(Y, 2);
    rig.tap(START);
    CHECK(screen.typing() && session.samplerPresets().size() == 1);
    rig.tap(B);
    CHECK(!screen.typing());
    // A second, picked with hold A + up / down; the ends stop.
    session.setSamplerSetting(1, sampler::Level, 10);
    rig.tap(R1);
    rig.tap(RIGHT, 2);
    rig.tap(A);
    rig.tap(START);
    CHECK(!screen.typing() && screen.said() == "SAVED AS C" && session.samplerPresets().size() == 2);
    // Each is heard as it is stepped to, as a synth's preset and a kit are (gesture audit G22); past an end
    // nothing is put on and nothing heard; playing, the song is heard instead.
    int heard = screen.timesHeard();
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(session.samplerPresetOf(1) == 0 && track.sampler[sampler::Level] == 33);
    CHECK(screen.timesHeard() == heard + 1);
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(session.samplerPresetOf(1) == 0); // (past the first: stops)
    CHECK(screen.timesHeard() == heard + 1);
    rig.project().playing = true; // as if playing
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(session.samplerPresetOf(1) == 1 && screen.timesHeard() == heard + 1);
    rig.project().playing = false;
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(session.samplerPresetOf(1) == 0 && screen.timesHeard() == heard + 2);
    // B held deletes the file (the bar says CANNOT BE UNDONE: testDeletingAPresetSaysItCannotBeUndone); another
    // button calls it off; the track keeps its sound.
    rig.key(B, true);
    rig.frames(30);
    CHECK(screen.presetDeletePart() > 0.3f && screen.presetDeletePart() < 0.7f);
    rig.tap(LEFT);
    rig.frames(1);
    CHECK(screen.presetDeletePart() == 0.f);
    rig.frames(60);
    rig.key(B, false);
    rig.frames(1);
    CHECK(session.samplerPresets().size() == 2);
    rig.key(B, true);
    rig.frames(80);
    rig.key(B, false);
    CHECK(session.samplerPresets().size() == 1 && screen.said() == "AB DELETED. THE TRACK KEEPS ITS SOUND");
    CHECK(session.samplerPresetOf(1) == -1 && track.sampler[sampler::Level] == 33);
    // The backend refuses the rest: no sampler track, no name, nothing there.
    CHECK(session.addTrack(TrackKind::Drums)); // track 2
    CHECK(!session.saveSamplerPreset(2, "X") && !session.saveSamplerPreset(1, "  ") && !session.saveSamplerPreset(1, "???"));
    CHECK(!session.loadSamplerPreset(2, 0) && !session.loadSamplerPreset(1, 99) && !session.deleteSamplerPreset(99));
    // Another folder: the old folder's presets do not follow.
    session.usePresetFolder(folder + "/elsewhere");
    CHECK(session.samplerPresets().empty() && session.samplerPresetOf(1) == -1);
    // Leaving the keys with the stick pushed: the next push starts over, with its free step (audit #43).
    screen.goTo(SamplerScreen::kPresetStop);
    rig.tap(R1);
    CHECK(screen.typing());
    rig.key(STICK_UP, true);
    rig.frames(3);
    rig.tap(B);
    CHECK(!screen.typing());
    rig.frames(1);
    CHECK(screen.cursor() == 7); // moved at once, not after the keys' leftover
    rig.key(STICK_UP, false);
}

// Hold B on one of the player's own synth presets: a second, and its file is deleted (gesture audit G30: only
// the SD card could). The track keeps its sound; every track of that synth stays on its own preset (the numbers
// past it move up one); it is not a step of undo (a file, as a kit's or a sampler preset's).
void testDeletingOwnSynthPresets() {
    std::unique_ptr<Rig> rig(onPhazed());
    Session& session = rig->app.session();
    SynthScreen& screen = rig->app.phazed();
    TrackData& track = rig->project().tracks[1];
    const std::string folder = tempFolder("deletepreset");
    session.usePresetFolder(folder);
    const int first = builtInPresets(TrackKind::Phazed);
    // On a built-in, hold B does nothing.
    CHECK(screen.cursor() == SynthScreen::kPreset && !screen.onOwnPreset());
    rig->key(B, true);
    rig->frames(80);
    rig->key(B, false);
    rig->frames(1);
    CHECK(screen.presetDeletePart() == 0.f && track.preset == 0 && screen.said().empty());
    // Three of the player's own, AA, BB and CC; a second PHAZED track on CC, this one on BB.
    for (int i = 0; i < 3; ++i) {
        session.setSynthSetting(1, phazed::Level, 3 + i);
        CHECK(session.savePreset(1, std::string(2, static_cast<char>('A' + i))));
    }
    CHECK(session.addTrack(TrackKind::Phazed)); // track 2
    session.chooseSynthPreset(2, first + 2);
    session.chooseSynthPreset(1, first + 1);
    rig->project().track = 1;
    rig->frames(2);
    CHECK(track.preset == first + 1 && track.patch.value[phazed::Level] == 4 && screen.onOwnPreset());
    const PhazedPatch sound = track.patch;
    // Held: the bar fills. Any other button calls it off; let go before it is full, it stays.
    rig->key(B, true);
    rig->frames(30);
    CHECK(screen.presetDeletePart() > 0.3f && screen.presetDeletePart() < 0.7f);
    rig->tap(Y);
    rig->frames(1);
    CHECK(screen.presetDeletePart() == 0.f);
    rig->frames(60);
    rig->key(B, false);
    rig->frames(1);
    rig->key(B, true);
    rig->frames(40);
    rig->key(B, false);
    rig->frames(40);
    CHECK(userPresets(TrackKind::Phazed).size() == 3 && screen.presetDeletePart() == 0.f);
    // Another track of this synth up with B still held (the two share this screen): called off, CC stays.
    rig->key(B, true);
    rig->frames(30);
    rig->project().track = 2;
    rig->frames(60);
    rig->key(B, false);
    rig->frames(1);
    CHECK(userPresets(TrackKind::Phazed).size() == 3 && screen.said().empty());
    rig->project().track = 1;
    // Off PRESET (a macro), B held is the macro's and deletes nothing.
    rig->tap(DOWN);
    rig->key(B, true);
    rig->frames(70);
    rig->key(B, false);
    rig->tap(UP);
    CHECK(screen.cursor() == SynthScreen::kPreset && userPresets(TrackKind::Phazed).size() == 3);
    // L2 / R2 turn the tab without this screen seeing a press, and it is not looked at while away: with B still
    // held, back on it, the bar must start again, not come back full (the file went at once).
    rig->key(B, true);
    rig->frames(30);
    rig->tap(R2);
    rig->frames(70);
    rig->tap(L2);
    rig->frames(10);
    CHECK(rig->app.session().view == View::Device && screen.presetDeletePart() == 0.f);
    rig->key(B, false);
    rig->frames(1);
    CHECK(userPresets(TrackKind::Phazed).size() == 3 && screen.said().empty());
    rig->frames(2);
    const int steps = session.undoSteps();
    // Held the whole second: BB's file goes, and the screen says so. The track keeps its sound, named by the
    // preset before it (AA); track 2 stays on CC, a place earlier now.
    rig->key(B, true);
    rig->frames(70);
    rig->key(B, false);
    rig->frames(2);
    std::string file;
    CHECK(!readTextFile(folder + "/BB.phazed", file) && readTextFile(folder + "/AA.phazed", file) && readTextFile(folder + "/CC.phazed", file));
    CHECK(userPresets(TrackKind::Phazed).size() == 2 && screen.said() == "BB DELETED. THE TRACK KEEPS ITS SOUND");
    CHECK(track.preset == first && std::memcmp(track.patch.value, sound.value, sizeof sound.value) == 0);
    CHECK(rig->project().tracks[2].preset == first + 1 && std::string(phazedSpec().presetName(first + 1)) == "CC");
    CHECK(session.undoSteps() == steps); // (no step of undo: a file went, the song did not change)
    // On the first of the player's own, the preset before is the last built-in.
    rig->key(B, true);
    rig->frames(70);
    rig->key(B, false);
    rig->frames(2);
    CHECK(userPresets(TrackKind::Phazed).size() == 1 && screen.said() == "AA DELETED. THE TRACK KEEPS ITS SOUND");
    CHECK(track.preset == first - 1 && !screen.onOwnPreset() && rig->project().tracks[2].preset == first);
    // Its file gone by other means (WiFi, the card): NOT DELETED, and nothing moves.
    session.chooseSynthPreset(1, first);
    CHECK(std::remove((folder + "/CC.phazed").c_str()) == 0);
    rig->key(B, true);
    rig->frames(70);
    rig->key(B, false);
    rig->frames(2);
    CHECK(screen.said() == "NOT DELETED" && track.preset == first && rig->project().tracks[2].preset == first);
    session.usePresetFolder(tempFolder("deletepreset-clean")); // the registry is global: leave it empty for later tests
}

// While a preset's name is typed every button is the keys' (gesture audit G01), as on SET and in the list of
// recordings: no tab turns, no SONG screen, no L3, no undo, WRITE, NEXT SCENE, scene length or tempo, no
// help card. The four shoulders' panic still works, and the keys are still up after it.
void testThePresetKeysHaveEveryButton() {
    const SDL_Keycode MENU = SDLK_TAB, L3 = SDLK_3;
    for (int sampler = 0; sampler < 2; ++sampler) {
        Rig rig;
        Session& session = rig.app.session();
        Project& project = rig.project();
        CHECK(session.addTrack(sampler ? TrackKind::Sampler : TrackKind::Phazed)); // track 1
        project.track = 1;
        session.view = View::Device;
        if (sampler) rig.app.sampler().goTo(SamplerScreen::kPresetStop);
        else rig.app.phazed().goTo(0, SynthScreen::kPreset);
        rig.frames(2);
        // A change for undo to take back.
        TrackData& track = project.tracks[1];
        if (sampler) session.setSamplerSetting(1, sampler::Level, track.sampler[sampler::Level] == 33 ? 34 : 33);
        else session.setSynthSetting(1, phazed::Level, track.patch.value[phazed::Level] == 3 ? 4 : 3);
        rig.frames(2);
        if (session.undoSteps() == 0) std::printf("    %s: no step of undo for the change\n", sampler ? "sampler" : "phazed");
        const int steps = session.undoSteps(), scenes = project.sceneCount(), sceneSteps = project.sceneSteps(0);
        const float tempo = project.tempo;
        const bool writing = session.writing();
        CHECK(steps > 0);
        const auto typing = [&] { return sampler ? rig.app.sampler().typing() : rig.app.phazed().typing(); };
        const auto typed = [&] { return sampler ? rig.app.sampler().typed() : rig.app.phazed().typed(); };
        rig.tap(R1);
        rig.tap(A);
        CHECK(typing() && typed() == "A");
        rig.tap(L2);
        rig.tap(R2);
        rig.tap(L3);
        rig.tap(SELECT);
        rig.frames(2);
        CHECK(session.view == View::Device && typing());
        rig.key(SELECT, true);
        rig.tap(UP); // (the keys' cursor, not the tempo)
        rig.tap(X);
        rig.tap(Y);  // (the keys' Y: the last letter goes, no NEXT SCENE)
        rig.tap(L2);
        rig.key(SELECT, false);
        rig.frames(2);
        CHECK(project.tempo == tempo && session.writing() == writing && project.sceneCount() == scenes && project.sceneSteps(0) == sceneSteps);
        CHECK(session.view == View::Device && typing() && typed().empty() && (sampler ? rig.app.sampler().keyAt() : rig.app.phazed().keyAt()) == 30);
        rig.tap(A);
        CHECK(typed() == "4");
        // No help card over the keys (MENU held, or SELECT held by itself).
        rig.key(MENU, true);
        rig.frames(5);
        CHECK(!rig.app.helpShowing());
        rig.key(MENU, false);
        rig.frames(1);
        // The panic: silence, the note, and the keys still up with what was typed.
        for (SDL_Keycode k : {L1, R1, L2, R2}) rig.key(k, true), rig.frames(1);
        CHECK(rig.app.panicNoteShowing() && session.view == View::Device && typing());
        for (SDL_Keycode k : {L1, R1, L2, R2}) rig.key(k, false);
        rig.frames(1);
        rig.tap(Y); // (puts the note away, and does nothing else)
        CHECK(!rig.app.panicNoteShowing() && typing() && typed() == "4");
        // SELECT + B is the keys' B: it keeps the name and leaves; nothing is undone.
        rig.key(SELECT, true);
        rig.tap(B);
        rig.key(SELECT, false);
        rig.frames(2);
        CHECK(!typing() && session.undoSteps() == steps && session.view == View::Device);
    }
}

// The four shoulders' panic begun with R1 on PRESET (R1, then L1, L2, R2): R1 opens the name keys on its way, on
// one of the player's own with its name typed in, and the next B would replace its file. The panic closes keys
// it opened, wherever (R2 first turned the tab to DEVICE, and the panic turned it back); keys that were up
// before it stay up (testThePresetKeysHaveEveryButton).
void testAPanicClosesTheKeysItOpened() {
    for (int sampler = 0; sampler < 2; ++sampler) {
        Rig rig;
        Session& session = rig.app.session();
        const std::string folder = tempFolder("panickeys");
        session.usePresetFolder(folder);
        CHECK(session.addTrack(sampler ? TrackKind::Sampler : TrackKind::Phazed)); // track 1
        rig.project().track = 1;
        session.view = View::Device;
        const auto typing = [&] { return sampler ? rig.app.sampler().typing() : rig.app.phazed().typing(); };
        const auto said = [&] { return sampler ? rig.app.sampler().said() : rig.app.phazed().said(); };
        // One of the player's own, the track on it (a synth turned since: B would write the turn over the file).
        std::string before, file;
        if (sampler) {
            CHECK(session.saveSamplerPreset(1, "AA") && session.samplerPresetOf(1) == 0);
            rig.app.sampler().goTo(SamplerScreen::kPresetStop);
            CHECK(readTextFile(folder + "/AA.sampler", before));
        } else {
            CHECK(session.savePreset(1, "AA"));
            session.setSynthSetting(1, phazed::Level, rig.project().tracks[1].patch.value[phazed::Level] == 3 ? 4 : 3);
            rig.app.phazed().goTo(0, SynthScreen::kPreset);
            CHECK(readTextFile(folder + "/AA.phazed", before));
        }
        rig.frames(2);
        for (int first = 0; first < 2; ++first) {
            // R1 first on DEVICE; then R2 first, from the roll's tab to DEVICE, where R1 comes next.
            if (first == 1) session.view = View::Pattern, rig.frames(2);
            const std::vector<SDL_Keycode> order = first == 0 ? std::vector<SDL_Keycode>{R1, L1, L2, R2} : std::vector<SDL_Keycode>{R2, R1, L1, L2};
            for (SDL_Keycode k : order) rig.key(k, true), rig.frames(1);
            CHECK(rig.app.panicNoteShowing() && session.view == (first == 0 ? View::Device : View::Pattern));
            for (SDL_Keycode k : order) rig.key(k, false);
            rig.frames(1);
            rig.tap(B); // (puts the note away)
            session.view = View::Device;
            rig.frames(2);
            CHECK(!typing());
            rig.tap(B); // (on PRESET, the keys shut: a tap of B, nothing written)
            rig.frames(2);
            CHECK(!typing() && said().empty() && readTextFile(folder + (sampler ? "/AA.sampler" : "/AA.phazed"), file) && file == before);
        }
        session.usePresetFolder(tempFolder("panickeys-clean")); // the registry is global: leave it empty for later tests
    }
}

// A hold of B on one of the player's own (the sampler's preset, a kit on the KIT stop) is called off by a tab turned
// with B still held, as a synth's is (testDeletingOwnSynthPresets): back on the screen, the bar starts again.
void testATabCallsOffAHoldOfB() {
    for (int place = 0; place < 2; ++place) {
        Rig rig;
        Session& session = rig.app.session();
        session.usePresetFolder(tempFolder("tabcallsoff"));
        session.view = View::Device;
        std::size_t was = 0;
        const auto count = [&] { return place == 0 ? session.samplerPresets().size() : session.kits().size(); };
        if (place == 0) {
            CHECK(session.addTrack(TrackKind::Sampler));
            rig.project().track = 1;
            CHECK(session.saveSamplerPreset(1, "AA"));
            rig.app.sampler().goTo(SamplerScreen::kPresetStop);
        } else {
            rig.project().track = 0;
            CHECK(rig.project().tracks[0].kind == TrackKind::Drums && !session.saveKit(0).empty());
            rig.app.kit().goToKit();
        }
        was = count();
        rig.frames(2);
        const auto part = [&] { return place == 0 ? rig.app.sampler().presetDeletePart() : rig.app.kit().kitDeletePart(); };
        rig.key(B, true);
        rig.frames(30);
        CHECK(part() > 0.3f);
        rig.tap(R2);
        rig.frames(70);
        rig.tap(L2);
        rig.frames(10);
        CHECK(session.view == View::Device && part() == 0.f && count() == was);
        rig.key(B, false);
        rig.frames(2);
        CHECK(count() == was);
        session.usePresetFolder(tempFolder("tabcallsoff-clean")); // the registry is global: leave it empty for later tests
    }
}

// The name keys run at a list's pace wherever a name is typed (gesture audit G01: the preset keys ran at a
// cursor's, 170 then every 30 ms): RIGHT held 30 frames (480 ms) is one key at once and three more.
void testTheNameKeysRunAtAListsPace() {
    for (int place = 0; place < 3; ++place) {
        Rig rig;
        Session& session = rig.app.session();
        if (place == 0) {
            session.view = View::Settings;
            rig.app.settings().goTo(SettingsScreen::kTrackName);
            rig.tap(A);
        } else {
            CHECK(session.addTrack(place == 1 ? TrackKind::Phazed : TrackKind::Sampler));
            rig.project().track = 1;
            session.view = View::Device;
            if (place == 1) rig.app.phazed().goTo(0, SynthScreen::kPreset);
            else rig.app.sampler().goTo(SamplerScreen::kPresetStop);
            rig.frames(1);
            rig.tap(R1);
        }
        const auto keyAt = [&] { return place == 0 ? rig.app.settings().keyAt() : place == 1 ? rig.app.phazed().keyAt() : rig.app.sampler().keyAt(); };
        const bool typing = place == 0 ? rig.app.settings().typing() : place == 1 ? rig.app.phazed().typing() : rig.app.sampler().typing();
        CHECK(typing && keyAt() == 0);
        rig.key(RIGHT, true);
        rig.frames(13); // 208 ms: still the first (a cursor's pace, 170 ms, had gone on)
        CHECK(keyAt() == 1);
        rig.frames(17);
        rig.key(RIGHT, false);
        CHECK(keyAt() == 4);
        if (keyAt() != 4) std::printf("    place %d: RIGHT held 480 ms went %d keys\n", place, keyAt());
        rig.frames(30);
        CHECK(keyAt() == 4); // (let go: it stops)
    }
}

// Y held on the name keys deletes on at a list's pace (gesture audit G48, 2026-11-25): one letter at once,
// then one each kListEveryMs after kListDelayMs, so an old name goes in one hold. A tap is still one. The
// track's name and both preset keys (the folder's is picker_test's, the password's wifi_test's).
void testAHeldYDeletesOn() {
    for (int place = 0; place < 3; ++place) {
        Rig rig;
        Session& session = rig.app.session();
        if (place == 0) {
            session.view = View::Settings;
            rig.app.settings().goTo(SettingsScreen::kTrackName);
            rig.tap(A);
        } else {
            CHECK(session.addTrack(place == 1 ? TrackKind::Phazed : TrackKind::Sampler));
            rig.project().track = 1;
            session.view = View::Device;
            if (place == 1) rig.app.phazed().goTo(0, SynthScreen::kPreset);
            else rig.app.sampler().goTo(SamplerScreen::kPresetStop);
            rig.frames(1);
            rig.tap(R1);
        }
        const auto typed = [&]() -> std::string { return place == 0 ? rig.app.settings().typed() : place == 1 ? rig.app.phazed().typed() : rig.app.sampler().typed(); };
        rig.tap(Y, 12); // (the track's own name, if any)
        rig.tap(A, 10);
        CHECK(typed() == "AAAAAAAAAA");
        rig.tap(Y);
        CHECK(typed() == "AAAAAAAAA"); // a tap: one
        rig.key(Y, true);
        CHECK(typed().size() == 8u); // one at once
        rig.frames(13); // 208 ms: still that one
        CHECK(typed().size() == 8u);
        rig.frames(17); // 480 ms in all: three more at the list's pace
        rig.key(Y, false);
        CHECK(typed().size() == 5u);
        if (typed().size() != 5u) std::printf("    place %d: Y held 480 ms left %d letters\n", place, static_cast<int>(typed().size()));
        rig.frames(30);
        CHECK(typed().size() == 5u); // (let go: it stops)
        rig.key(Y, true);
        rig.frames(60); // held on: every letter goes, and nothing more happens at none
        rig.key(Y, false);
        CHECK(typed().empty());
        const bool typing = place == 0 ? rig.app.settings().typing() : place == 1 ? rig.app.phazed().typing() : rig.app.sampler().typing();
        CHECK(typing); // (still typing: a held Y never leaves)
        // A held direction, then Y: Y's run, not the direction's.
        rig.tap(A, 3);
        rig.key(RIGHT, true);
        rig.key(Y, true);
        rig.frames(20);
        rig.key(Y, false);
        rig.key(RIGHT, false);
        CHECK(typed().size() == 1u);
    }
}

// Hold A on SOUND and step while WiFi deletes a recording: the step goes through what is there
// now, and nothing missing goes on the track (audit #42).
void testHeldAStepsThroughLiveSamples() {
    Rig rig;
    Session& session = rig.app.session();
    SamplerScreen& screen = rig.app.sampler();
    const std::string folder = tempFolder("livesamples");
    session.useSampleFolder(folder + "/samples");
    mkdir((folder + "/samples").c_str(), 0755);
    CHECK(writeWav(folder + "/samples/a.wav", std::vector<float>(480, 0.25f), 48000));
    CHECK(writeWav(folder + "/samples/b.wav", std::vector<float>(480, 0.25f), 48000));
    CHECK(session.addTrack(TrackKind::Sampler)); // track 1
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    session.setTrackSample(1, "a");
    session.view = View::Device;
    rig.frames(2);
    screen.goTo(0); // SOUND
    rig.key(A, true);
    rig.frames(1);
    CHECK(std::remove((folder + "/samples/b.wav").c_str()) == 0);
    session.transfer().noteChange("b.wav", "");
    rig.frames(1);
    rig.tap(UP);
    CHECK(std::string(track.sample) == "a"); // the end stops; b (gone) never goes on
    rig.key(A, false);
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

// B with A held puts the sound back in the engine too (gesture audit G12): after stepping through presets,
// a macro turned before A went down, the note is the one an engine never moved off that sound plays.
void testAPutBackSoundIsTheEngines() {
    std::unique_ptr<Rig> still(onPhazed()), back(onPhazed());
    still->app.session().setSynthMacro(1, phazed::MacroOctave, 0.8f);
    back->app.session().setSynthMacro(1, phazed::MacroOctave, 0.8f);
    const std::vector<float> wanted = noteNow(*still);
    back->project().playing = true; // (as if the song ran: the steps are not heard)
    SynthScreen& screen = back->app.phazed();
    screen.goTo(0, SynthScreen::kPreset);
    back->key(A, true);
    back->tap(UP, 5);
    CHECK(back->project().tracks[1].preset == 5);
    back->tap(B);
    back->key(A, false);
    back->project().playing = false;
    CHECK(back->project().tracks[1].preset == 0 && back->project().tracks[1].macro[phazed::MacroOctave] == 0.8f);
    CHECK(noteNow(*back) == wanted);
}

// ADVANCED keeps its place (gesture audit G13): each track comes back to the page and stop it was last on
// (in memory, as FX's last_), and L1 / R1 keep the cursor's row and column, so on FM-6 the same knob of the
// next operator is under it. Where the next page has no such place, the nearest.
void testAdvancedKeepsItsPlace() {
    Rig rig;
    Session& session = rig.app.session();
    CHECK(session.addTrack(TrackKind::Fm6) && session.addTrack(TrackKind::Fm6)); // tracks 2 and 3
    rig.project().track = 1;
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.fm6();
    // The first time: page 1, its first stop.
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.cursor() == 0);
    // OPERATOR 1's OUTPUT LEVEL (its second box); R1: OPERATOR 2's, R1 again OPERATOR 3's, L1 back.
    rig.tap(R1);
    rig.tap(RIGHT);
    CHECK(screen.page() == 2 && screen.settingHere() == fm6::opSetting(1, fm6::OpLevel));
    rig.tap(R1);
    CHECK(screen.page() == 3 && screen.cursor() == 1 && screen.settingHere() == fm6::opSetting(2, fm6::OpLevel));
    rig.tap(R1);
    CHECK(screen.page() == 4 && screen.settingHere() == fm6::opSetting(3, fm6::OpLevel));
    rig.tap(L1);
    CHECK(screen.page() == 3 && screen.settingHere() == fm6::opSetting(2, fm6::OpLevel));
    // Down a row: the place is a row and a column, kept the same way.
    rig.tap(DOWN, 2);
    const int deep = screen.cursor();
    CHECK(deep == 9);
    rig.tap(R1);
    CHECK(screen.page() == 4 && screen.cursor() == deep);
    // B to MAIN, A on ADVANCED: back on page 4 at that stop, not page 1.
    rig.tap(B);
    CHECK(screen.page() == 0 && screen.cursor() == SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 4 && screen.cursor() == deep);
    rig.tap(B);
    // Another track of FM-6: its own place (none yet: page 1); then the first track's is still its own.
    rig.project().track = 2;
    rig.frames(2);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.cursor() == 0);
    rig.tap(R1, 6); // (OPERATOR 6)
    CHECK(screen.page() == 7);
    rig.tap(B);
    rig.project().track = 1;
    rig.frames(2);
    rig.tap(A);
    CHECK(screen.page() == 4 && screen.cursor() == deep);
    rig.tap(B);
    rig.project().track = 2;
    rig.frames(2);
    rig.tap(A);
    CHECK(screen.page() == 7 && screen.cursor() == 0);
    // A place the next page has not: the nearest. The last box of an operator page (its 22nd), L1 to the
    // voice's page: its last stop.
    screen.goTo(3, 21);
    rig.frames(1);
    const int voiceStops = [&] { screen.goTo(1, 99); return screen.cursor() + 1; }();
    screen.goTo(3, 21);
    CHECK(voiceStops < 22);
    rig.tap(L1);
    CHECK(screen.page() == 2 && screen.cursor() == 21);
    rig.tap(L1);
    CHECK(screen.page() == 1 && screen.cursor() == voiceStops - 1);
    // Left by another track rather than B (picked on SONG, the screen on page 5): the place is still the track's,
    // and the other track, up on it now, shows ITS own place (page 1, its last stop there), which it keeps. (It
    // was shown page 5, and its own place was written over with it.)
    rig.project().track = 1;
    rig.frames(1);
    screen.goTo(5, 2);
    rig.frames(1);
    rig.project().track = 2;
    rig.frames(1);
    CHECK(screen.page() == 1 && screen.cursor() == voiceStops - 1);
    rig.tap(B);
    rig.project().track = 1;
    rig.frames(1);
    rig.tap(A);
    CHECK(screen.page() == 5 && screen.cursor() == 2);
    rig.project().track = 2; // (on page 5 for track 1: track 2's own place again)
    rig.frames(1);
    CHECK(screen.page() == 1 && screen.cursor() == voiceStops - 1);
    rig.tap(B);
    rig.project().track = 1;
    rig.frames(1);
    CHECK(screen.page() == 0); // (B left it on MAIN: MAIN is every track's, and stays)
    rig.tap(A);
    CHECK(screen.page() == 5 && screen.cursor() == 2);
    rig.tap(B);
    // From an envelope (eight to a row) to a page of four: a column past the fourth is the row's fourth.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 3;
    rig.frames(1);
    SynthScreen& phazed = rig.app.phazed();
    phazed.goTo(4, 6);
    rig.tap(L1);
    CHECK(phazed.page() == 3 && phazed.cursor() == 3);
    rig.tap(R1);
    CHECK(phazed.page() == 4 && phazed.cursor() == 3);
    // ...and on the second row, from the last envelope round to SOUND (thirteen boxes): the row's fourth
    // (7), not a sixth column it has not (9, a row further on).
    phazed.goTo(9, 13);
    rig.tap(R1);
    CHECK(phazed.page() == 1 && phazed.cursor() == 7);
    // A track that has not opened the pages arrives on page 1 at its first stop; on CLOUD that is not RECORDING
    // (where R1 records), but the stop after it.
    for (TrackKind kind : {TrackKind::Fm6, TrackKind::Cloud}) {
        CHECK(session.addTrack(kind) && session.addTrack(kind));
        const int a = rig.project().trackCount() - 2, b = a + 1;
        SynthScreen& s = kind == TrackKind::Cloud ? rig.app.cloud() : rig.app.fm6();
        rig.project().track = a;
        rig.frames(1);
        s.goTo(3, 2);
        rig.frames(1);
        rig.project().track = b;
        rig.frames(1);
        CHECK(s.page() == 1 && s.cursor() == s.firstStop(1) && (kind != TrackKind::Cloud || (s.cursor() == 1 && !s.onRecording())));
    }
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

// ---- JAX-EP, through the same screens ------------------------------------------

Rig* onJaxep() {
    Rig* rig = new Rig;
    rig->app.session().view = View::Song;
    rig->app.song().moveCursorTo(0, rig->project().trackCount());
    rig->tap(A);    // + TRACK
    rig->tap(DOWN); // from PHAZED down to JAX-EP
    rig->tap(A);
    return rig;
}

void testAddingJaxep() {
    std::unique_ptr<Rig> rig(onJaxep());
    CHECK(rig->project().trackCount() == 2);
    CHECK(rig->project().tracks[1].kind == TrackKind::Jx3p);
    // JAX-EP (owner, 2026-11-14; jx3p-plan.md): the name on the device, a new word in a song file, and
    // the old Twin Analog is not in the list to pick from (its word is read, never offered or written).
    // No maker's or model's name where a player reads.
    const InstrumentInfo* info = instrument(TrackKind::Jx3p);
    CHECK(info && std::string(info->listName) == "JAX-EP" && std::string(info->name) == "JAX-EP" && std::string(info->word) == "jaxep" &&
          std::string(info->presetEnding) == ".jaxep" && info->synth == SynthKind::Jx3p);
    CHECK(kInstrumentCount == 9);
    for (int i = 0; i < kInstrumentCount; ++i) CHECK(std::string(kInstruments[i].word) != "twin");
    bool old = false;
    CHECK(trackKindFromWord("twin", &old) == TrackKind::Jx3p && old);
    old = true;
    CHECK(trackKindFromWord("jaxep", &old) == TrackKind::Jx3p && !old && trackKindFromWord("twins") == TrackKind::None);
    CHECK(trackKindFromWord("jx3p") == TrackKind::None); // the plan's first word: never written by anything released
    CHECK(std::string(info->shortName) == "JAX" && std::string(info->about) == "virtual analog, after a popular DCO synth" && !info->countPresets);
    for (int i = 0; i < kInstrumentCount; ++i)
        for (const char* text : {kInstruments[i].name, kInstruments[i].shortName, kInstruments[i].listName, kInstruments[i].about})
            for (const char* maker : {"TWIN", "Twin", "JX", "Roland", "ROLAND", "PG-200", "Juno", "JUNO"}) CHECK(std::string(text).find(maker) == std::string::npos);
    CHECK(std::string(jx3pSpec().name) == "JAX-EP");
    // Its first preset is loaded into the project, as the engine has it: VELOCITY on and BRILLIANCE
    // in the middle, a new track's.
    const Jx3pPatch first = Jx3p::presetPatch(0);
    for (int id = 0; id < jx3p::kSettings; ++id) CHECK(rig->project().tracks[1].jx3p.value[id] == first.value[id]);
    CHECK(first.value[jx3p::Velocity] == 1 && first.value[jx3p::Brilliance] == 50);
    // The chooser goes round all seven.
    rig->tap(RIGHT);
    rig->tap(A);
    CHECK(rig->app.song().choosing());
    rig->tap(DOWN, 8); // PHAZED > JAX-EP > SAMPLER > FM-6 > PHYSIC > ACID > CLOUD > MORPH > DRUMS
    rig->tap(A);
    CHECK(rig->project().tracks[2].kind == TrackKind::Drums);
    rig->tap(RIGHT);
    rig->tap(A);
    rig->tap(UP, 7); // PHAZED > DRUMS > MORPH > CLOUD > ACID > PHYSIC > FM-6 > SAMPLER
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

void testJaxepsDeviceScreen() {
    std::unique_ptr<Rig> rig(onJaxep());
    rig->tap(A);  // open its pattern
    rig->tap(R2); // and its device
    SynthScreen& screen = rig->app.jaxep();
    TrackData& track = rig->project().tracks[1];
    CHECK(rig->view() == View::Device && screen.pageCount() == 6);
    CHECK(rig->app.phazed().page() == 0); // Phazed's own screen is a separate one

    // Presets, and round: the factory 32 (the plan's batch 7).
    const int presets = builtInPresets(TrackKind::Jx3p);
    rig->key(A, true);
    rig->tap(UP);
    CHECK(track.preset == 1 % presets);
    for (int id = 0; id < jx3p::kSettings; ++id) CHECK(track.jx3p.value[id] == Jx3p::presetPatch(1 % presets).value[id]);
    rig->tap(DOWN, 2);
    CHECK(track.preset == presets - 1);
    rig->tap(UP);
    rig->key(A, false);
    CHECK(track.preset == 0);
    CHECK(presets == 32 && std::string(jx3pSpec().presetName(0)) == "String I" && std::string(jx3pSpec().presetCategory(0)) == "BANK A");

    // A folder jump is a bank jump (the plan, section 4): on FOLDER, A held, up is B1; up again goes round
    // to A1; down from A1 is B1 again. The patch goes with it.
    screen.goTo(0, SynthScreen::kFolder);
    rig->key(A, true);
    rig->tap(UP);
    CHECK(track.preset == 16 && std::string(jx3pSpec().presetName(track.preset)) == "Violin" && std::string(jx3pSpec().presetCategory(track.preset)) == "BANK B");
    for (int id = 0; id < jx3p::kPatchSettings; ++id) CHECK(track.jx3p.value[id] == Jx3p::presetPatch(16).value[id]);
    rig->tap(UP);
    CHECK(track.preset == 0);
    rig->tap(DOWN);
    CHECK(track.preset == 16);
    rig->tap(DOWN);
    rig->key(A, false);
    CHECK(track.preset == 0);
    screen.goTo(0, SynthScreen::kPreset);

    // A macro.
    screen.goTo(0, SynthScreen::kFirstMacro + jx3p::MacroMix);
    rig->key(A, true);
    rig->tap(UP, 3);
    rig->key(A, false);
    CHECK(near(track.macro[jx3p::MacroMix], 0.65f));
    CHECK(std::string(jx3pSpec().macroNames[jx3p::MacroCutoff]) == "CUTOFF" && std::string(jx3pSpec().macroNames[jx3p::MacroRelease]) == "RELEASE");

    // Five advanced pages hold all thirty-four settings, each once.
    int seen[jx3p::kSettings] = {};
    for (int page = 1; page < screen.pageCount(); ++page) {
        for (int stop = 0; stop < 40; ++stop) {
            screen.goTo(page, stop);
            if (screen.cursor() == stop) ++seen[screen.settingHere()];
        }
    }
    for (int count : seen) CHECK(count == 1);
    screen.goTo(9, 0);
    CHECK(screen.page() == 5); // there is no page nine here
    rig->tap(R1);
    CHECK(screen.page() == 1); // round
    rig->tap(L1);
    CHECK(screen.page() == 5);

    // A setting changes the project and stops at its limits.
    screen.goTo(1, 0);
    CHECK(screen.settingHere() == jx3p::Dco1Range);
    rig->key(A, true);
    rig->tap(UP, 9);
    CHECK(track.jx3p.value[jx3p::Dco1Range] == 2);
    rig->tap(DOWN, 9);
    CHECK(track.jx3p.value[jx3p::Dco1Range] == 0);
    rig->key(A, false);
    CHECK(track.patch.value[phazed::Octave] == 0); // and never touches the other synth's patch
}

// HOW JAX-EP READS (jx3p-plan.md, section 1 and batch 6b): a knob as its dial, in tenths, 0.0 to 10.0; a switch
// by its positions' names; a macro as how far from the patch it is; a folder by what is known of its sounds.
void testHowJaxepReads() {
    const SynthSpec& spec = jx3pSpec();
    const auto reads = [&](int id, int value) {
        char text[32];
        spec.settingText(id, value, text, sizeof text);
        return std::string(text);
    };
    const int switches[] = {jx3p::Dco1Range, jx3p::Dco1Wave, jx3p::Dco1Lfo, jx3p::Dco1Env, jx3p::Dco2Range, jx3p::Dco2Wave, jx3p::Dco2CrossMod, jx3p::Dco2Lfo,
                            jx3p::Dco2Env, jx3p::DcoEnvPolarity, jx3p::VcfEnvPolarity, jx3p::VcaMode, jx3p::LfoWave, jx3p::Chorus, jx3p::Velocity};
    int knobs = 0;
    for (int id = 0; id < jx3p::kSettings; ++id) {
        bool isSwitch = false;
        for (int s : switches) isSwitch = isSwitch || s == id;
        if (isSwitch) {
            CHECK(spec.settingMax(id) <= 3);
            continue;
        }
        ++knobs;
        CHECK(spec.settingMin(id) == 0 && spec.settingMax(id) == 100);
        CHECK(reads(id, 0) == "0.0" && reads(id, 100) == "10.0" && reads(id, 1) == "0.1" && reads(id, 9) == "0.9" && reads(id, 10) == "1.0" && reads(id, 53) == "5.3" && reads(id, 79) == "7.9" && reads(id, 98) == "9.8");
    }
    CHECK(knobs == 19);
    const auto every = [&](int id) {
        std::string all;
        for (int v = spec.settingMin(id); v <= spec.settingMax(id); ++v) all += (v ? " " : "") + reads(id, v);
        return all;
    };
    CHECK(every(jx3p::Dco1Range) == "16' 8' 4'" && every(jx3p::Dco2Range) == "16' 8' 4'");
    CHECK(every(jx3p::Dco1Wave) == "SAW PULSE SQUARE" && every(jx3p::Dco2Wave) == "SAW PULSE SQUARE NOISE");
    CHECK(every(jx3p::Dco2CrossMod) == "OFF SYNC METAL" && every(jx3p::LfoWave) == "SINE SQUARE RANDOM" && every(jx3p::VcaMode) == "ENV GATE");
    CHECK(every(jx3p::DcoEnvPolarity) == "NORMAL INVERTED" && every(jx3p::VcfEnvPolarity) == "NORMAL INVERTED");
    const int onOff[] = {jx3p::Dco1Lfo, jx3p::Dco1Env, jx3p::Dco2Lfo, jx3p::Dco2Env, jx3p::Chorus, jx3p::Velocity};
    for (int id : onOff) CHECK(every(id) == "OFF ON");
    // A value out of its range (a file, a later program) reads as the nearest position, never past the list.
    CHECK(reads(jx3p::Dco1Wave, 3) == "SQUARE" && reads(jx3p::Dco2Wave, 9) == "NOISE" && reads(jx3p::Chorus, -1) == "OFF" && reads(jx3p::LfoWave, 7) == "RANDOM");
    // A knob too: held to the dial (it read "0.-5" and "10.5": the review, 2026-11-15).
    CHECK(reads(jx3p::Cutoff, -5) == "0.0" && reads(jx3p::Cutoff, 105) == "10.0" && reads(jx3p::Brilliance, -100) == "0.0" && reads(jx3p::Release, 1000) == "10.0");

    // The pages' own words, in the panel's order.
    const char* pages[] = {"MAIN", "DCO-1", "DCO-2", "VCF", "VCA AND ENVELOPE", "LFO AND CHORUS"};
    CHECK(spec.pages == 6);
    for (int p = 0; p < 6; ++p) CHECK(std::string(spec.pageNames[p]) == pages[p]);
    const auto names = [&](int page) {
        std::string all;
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = 0;
            const char* name = "";
            spec.item(page, i, id, name);
            all += (i ? ", " : "") + std::string(name);
        }
        return all;
    };
    CHECK(names(1) == "RANGE, WAVE, LFO, ENV, LFO DEPTH, ENV DEPTH, ENV POLARITY");
    CHECK(names(2) == "RANGE, WAVE, CROSS MOD, TUNE, FINE, LFO, ENV");
    CHECK(names(3) == "MIX, HPF, CUTOFF, RESONANCE, LFO MOD, ENV MOD, ENV POLARITY, KEY FOLLOW");
    CHECK(names(4) == "MODE, LEVEL, ATTACK, DECAY, SUSTAIN, RELEASE");
    CHECK(names(5) == "WAVE, DELAY, RATE, CHORUS, BRILLIANCE, VELOCITY");
    CHECK(spec.itemCount(0) == 0 && spec.itemCount(6) == 0 && !spec.envelopePage(1));

    // The eight macros, by lane (they are automation lanes 0 to 7), and how far each is from the patch.
    const char* macros[kSynthMacros] = {"CUTOFF", "RESO", "ENV", "TUNE", "MIX", "LFO", "ATTACK", "RELEASE"};
    for (int m = 0; m < kSynthMacros; ++m) {
        CHECK(std::string(spec.macroNames[m]) == macros[m]);
        char text[16];
        spec.macroText(m, 0.5f, text, sizeof text);
        CHECK(std::string(text) == "+0");
        spec.macroText(m, 0.f, text, sizeof text);
        CHECK(std::string(text) == "-100");
        spec.macroText(m, 1.f, text, sizeof text);
        CHECK(std::string(text) == "+100");
        spec.macroText(m, 0.65f, text, sizeof text);
        CHECK(std::string(text) == "+30");
    }

    // What a folder says: the two banks are from the 1983 booklet; every built-in sound's speeds are guesses;
    // the player's own say nothing; the other synths have nothing to say. No maker's name in either.
    CHECK(std::string(jaxepFolderNote("BANK A")) == "1983 BOOKLET, SPEEDS GUESSED" && jaxepFolderNote("BANK B") == kJaxepBankNote);
    CHECK(std::string(jaxepFolderNote("START")) == "SPEEDS GUESSED" && jaxepFolderNote("BANKS") == kJaxepGuessNote && jaxepFolderNote("USER") == kJaxepGuessNote && jaxepFolderNote("") == kJaxepGuessNote);
    CHECK(spec.folderNote && !phazedSpec().folderNote && !fm6Spec().folderNote && spec.pageReadsAsDevice && !phazedSpec().pageReadsAsDevice && !fm6Spec().pageReadsAsDevice);
    for (int i = 0; i < builtInPresets(TrackKind::Jx3p); ++i) CHECK(spec.folderNote(i) == jaxepFolderNote(spec.presetCategory(i))); // (an added built-in's folder: SPEEDS GUESSED)
    UserPreset own;
    own.name = "MINE";
    own.values.assign(jx3p::kPatchSettings, 0);
    own.macros.assign(kSynthMacros, 0.5f);
    setUserPresets(TrackKind::Jx3p, {own});
    CHECK(spec.presetCount() == builtInPresets(TrackKind::Jx3p) + 1 && spec.folderNote(builtInPresets(TrackKind::Jx3p)) == nullptr && spec.folderNote(0) != nullptr);
    setUserPresets(TrackKind::Jx3p, {});

    // On the screen: MAIN's PRESET row says it, and does not for the player's own.
    std::unique_ptr<Rig> rig(onJaxep());
    rig->tap(A);
    rig->tap(R2);
    SynthScreen& screen = rig->app.jaxep();
    CHECK(rig->view() == View::Device && screen.folderNote() == kJaxepBankNote); // (batch 7: a new track is on A1, in BANK A)
    screen.folderNoteAs(kJaxepGuessNote);
    CHECK(screen.folderNote() == kJaxepGuessNote);
    screen.folderNoteAs(nullptr);
    rig->app.session().chooseSynthPreset(1, 31); // B16, in BANK B
    CHECK(screen.folderNote() == kJaxepBankNote && std::string(spec.presetCategory(31)) == "BANK B" && std::string(spec.presetCategory(15)) == "BANK A" && std::string(spec.presetCategory(16)) == "BANK B");
    setUserPresets(TrackKind::Jx3p, {own});
    rig->app.session().chooseSynthPreset(1, builtInPresets(TrackKind::Jx3p));
    CHECK(screen.folderNote() == nullptr);
    rig->project().track = 0; // (and a drum track is not asked)
    setUserPresets(TrackKind::Jx3p, {});
    CHECK(rig->app.phazed().folderNote() == nullptr && rig->app.fm6().folderNote() == nullptr);
}

// How loud a note at `velocity` is on the Rig's track 2, the JX-3P, as it stands.
float jxLoud(Rig& rig, float velocity) {
    rig.render(48000); // whatever sounded before has rung out
    Command c;
    c.type = Command::NoteOn;
    c.track = 1;
    c.a = 57;
    c.value = velocity;
    rig.engine->send(c);
    rig.render(9600);
    const float peak = Rig::peak(rig.render(9600));
    c.type = Command::NoteOff;
    rig.engine->send(c);
    return peak;
}

void testTheEngineHearsJaxepsSettings() {
    // The VCA's LEVEL: the gain follows the dial straight (the owner's call, 2026-11-15), so LEVEL 1.6 is half of 3.2.
    const auto loudness = [](int down) {
        std::unique_ptr<Rig> rig(onJaxep());
        rig->tap(A);
        rig->tap(R2);
        rig->app.jaxep().goTo(4, 1);
        CHECK(rig->app.jaxep().settingHere() == jx3p::VcaLevel);
        rig->app.session().setSynthSetting(1, jx3p::VcaLevel, 32); // (the gain LEVEL 3.5 gave with the old taper; nowhere near the clip)
        rig->key(A, true);
        rig->tap(DOWN, down);
        rig->key(A, false);
        const int level = rig->project().tracks[1].jx3p.value[jx3p::VcaLevel];
        CHECK(down == 0 ? level == 32 : level < 32);
        const float loud = jxLoud(*rig, 1.f);
        return std::pair<float, int>(loud, level);
    };
    const std::pair<float, int> full = loudness(0), down = loudness(6);
    CHECK(full.first > 0.01f);
    const float expected = static_cast<float>(down.second) / static_cast<float>(full.second);
    CHECK(down.second < full.second && std::fabs(down.first / full.first - expected) < 0.05f * expected);
}

// A PRESET PICKED KEEPS BRILLIANCE AND VELOCITY (the plan, section 1): they are the track's, as
// BRILLIANCE is a knob on the real one's panel. Everything else is the preset's.
void testAPresetKeepsTheTracksOwnValues() {
    std::unique_ptr<Rig> rig(onJaxep());
    Session& session = rig->app.session();
    TrackData& track = rig->project().tracks[1];
    CHECK(jx3p::kPatchSettings == 32 && presetSettingCount(TrackKind::Jx3p) == 32 && settingCount(TrackKind::Jx3p) == 34);
    CHECK(presetSettingCount(TrackKind::Phazed) == settingCount(TrackKind::Phazed) && presetSettingCount(TrackKind::Fm6) == settingCount(TrackKind::Fm6));
    // With VELOCITY on, as a new track has it, a soft note is quieter than a full one.
    const float fullOn = jxLoud(*rig, 1.f), softOn = jxLoud(*rig, 0.3f);
    CHECK(fullOn > 0.01f && softOn < 0.3f * fullOn);
    // Both turned, and a setting of the patch with them.
    session.setSynthSetting(1, jx3p::Velocity, 0);
    session.setSynthSetting(1, jx3p::Brilliance, 23);
    session.setSynthSetting(1, jx3p::Resonance, 77);
    session.setSynthSetting(1, jx3p::Chorus, 0);
    rig->frames(1);
    // A built-in preset, picked on the screen.
    rig->tap(A);
    rig->tap(R2);
    rig->app.jaxep().goTo(0, SynthScreen::kPreset);
    rig->key(A, true);
    rig->tap(UP, builtInPresets(TrackKind::Jx3p)); // all the way round, back to the first
    rig->key(A, false);
    const Jx3pPatch first = Jx3p::presetPatch(0);
    CHECK(track.preset == 0);
    for (int id = 0; id < jx3p::kPatchSettings; ++id) CHECK(track.jx3p.value[id] == first.value[id]); // RESONANCE and CHORUS are the preset's again
    CHECK(track.jx3p.value[jx3p::Velocity] == 0 && track.jx3p.value[jx3p::Brilliance] == 23);          // ...and these two the track's still
    // The engine too: with VELOCITY off a soft note is as loud as a full one. (Within a quarter: the
    // chorus is at another place in its sweep for each note, and a peak moves 8% with it. With
    // VELOCITY on the soft note is a tenth of the full one.)
    const float fullOff = jxLoud(*rig, 1.f), softOff = jxLoud(*rig, 0.3f);
    CHECK(softOff > 0.75f * fullOff && softOff < 1.33f * fullOff);
    // One of the player's own, saved with them turned another way: its file does not hold them...
    const std::string keeps = tempFolder("keeps");
    const char* folder = keeps.c_str();
    session.usePresetFolder(folder);
    session.setSynthSetting(1, jx3p::Resonance, 41);
    session.setSynthSetting(1, jx3p::Velocity, 1);
    session.setSynthSetting(1, jx3p::Brilliance, 88);
    CHECK(session.savePreset(1, "MINE"));
    std::string file;
    CHECK(readTextFile(std::string(folder) + "/MINE.jaxep", file) && file.find("synth jaxep\n") != std::string::npos);
    {
        std::istringstream in(file.substr(file.find("settings") + 8));
        int v = 0, count = 0, at17 = -1;
        while (in >> v) {
            if (count == jx3p::Resonance) at17 = v;
            ++count;
        }
        CHECK(count == jx3p::kPatchSettings && at17 == 41);
    }
    CHECK(track.preset == builtInPresets(TrackKind::Jx3p) && userPresets(TrackKind::Jx3p).size() == 1);
    // ...and picking it, with the track's two turned again, leaves them be.
    session.setSynthSetting(1, jx3p::Velocity, 0);
    session.setSynthSetting(1, jx3p::Brilliance, 12);
    session.chooseSynthPreset(1, 0);
    CHECK(track.jx3p.value[jx3p::Resonance] == first.value[jx3p::Resonance]);
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Jx3p));
    CHECK(track.jx3p.value[jx3p::Resonance] == 41 && track.jx3p.value[jx3p::Velocity] == 0 && track.jx3p.value[jx3p::Brilliance] == 12);
    const float fullMine = jxLoud(*rig, 1.f), softMine = jxLoud(*rig, 0.3f);
    CHECK(softMine > 0.75f * fullMine && softMine < 1.33f * fullMine);
    // A file written by hand with 34 numbers: the last two are not a preset's, and are not taken.
    std::string hand = "apotheca-preset 1\nsynth jaxep\nmacros 0.5 0.5 0.5 0.5 0.5 0.5 0.5 0.5\nsettings";
    for (int id = 0; id < jx3p::kSettings; ++id) hand += id == jx3p::Velocity ? " 1" : id == jx3p::Brilliance ? " 99" : id == jx3p::Resonance ? " 9" : " 0";
    CHECK(writeTextFile(std::string(folder) + "/HAND.jaxep", hand + "\n"));
    // An old Twin Analog preset on the card is left there and not listed.
    CHECK(writeTextFile(std::string(folder) + "/OLD.twin", "apotheca-preset 1\nsynth twin\nmacros 0.5\nsettings 1 2 3\n"));
    session.usePresetFolder(folder);
    CHECK(userPresets(TrackKind::Jx3p).size() == 2 && userPresets(TrackKind::Jx3p)[0].name == "HAND" && userPresets(TrackKind::Phazed).empty() && userPresets(TrackKind::Fm6).empty());
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Jx3p));
    CHECK(track.jx3p.value[jx3p::Resonance] == 9 && track.jx3p.value[jx3p::Velocity] == 0 && track.jx3p.value[jx3p::Brilliance] == 12);
    std::string still;
    CHECK(readTextFile(std::string(folder) + "/OLD.twin", still) && still.find("synth twin") != std::string::npos);
    // A file written by hand with numbers no dial has: each is held to its own setting's range (the
    // review, 2026-11-15: the hold could be taken out with every test green), and one too large for
    // an int is held as a double first, so it is the setting's most on every machine.
    {
        std::string wild = "apotheca-preset 1\nsynth jaxep\nmacros 7 -3 1e300 0.5 0.5 0.5 0.5 0.5\nsettings";
        for (int id = 0; id < jx3p::kPatchSettings; ++id) wild += id == jx3p::Cutoff ? " 1e300" : id == jx3p::Resonance ? " -1e300" : id % 2 ? " 250" : " -7";
        CHECK(writeTextFile(std::string(folder) + "/WILD.jaxep", wild + "\n"));
        session.usePresetFolder(folder);
        CHECK(userPresets(TrackKind::Jx3p).size() == 3 && userPresets(TrackKind::Jx3p)[2].name == "WILD");
        session.chooseSynthPreset(1, builtInPresets(TrackKind::Jx3p) + 2);
        for (int id = 0; id < jx3p::kPatchSettings; ++id) {
            const int most = jx3pSpec().settingMax(id), least = jx3pSpec().settingMin(id);
            CHECK(track.jx3p.value[id] == (id == jx3p::Cutoff ? most : id == jx3p::Resonance ? least : id % 2 ? most : least));
        }
        CHECK(track.macro[0] == 1.f && track.macro[1] == 0.f && track.macro[2] == 1.f && track.jx3p.value[jx3p::Velocity] == 0 && track.jx3p.value[jx3p::Brilliance] == 12);
        // ...and the engine plays it: every sample a number.
        const std::vector<float> heard = (jxLoud(*rig, 1.f), rig->render(9600));
        for (float v : heard) CHECK(std::isfinite(v));
        session.chooseSynthPreset(1, builtInPresets(TrackKind::Jx3p));
        std::remove((std::string(folder) + "/WILD.jaxep").c_str());
    }
    // The track made another instrument and back: the two are the track's still.
    CHECK(session.setTrackKind(1, TrackKind::Phazed) && session.setTrackKind(1, TrackKind::Jx3p));
    CHECK(track.jx3p.value[jx3p::Velocity] == 0 && track.jx3p.value[jx3p::Brilliance] == 12 && track.jx3p.value[jx3p::Resonance] == first.value[jx3p::Resonance]);
    // A track taken away leaves a new track's values behind it for the next one.
    session.removeTrack(1);
    CHECK(session.addTrack(TrackKind::Jx3p) && rig->project().tracks[1].jx3p.value[jx3p::Velocity] == 1 && rig->project().tracks[1].jx3p.value[jx3p::Brilliance] == 50);
    setUserPresets(TrackKind::Jx3p, {});
    std::remove((std::string(folder) + "/MINE.jaxep").c_str());
    std::remove((std::string(folder) + "/HAND.jaxep").c_str());
    std::remove((std::string(folder) + "/OLD.twin").c_str());
    rmdir(folder);
}

// UNDO PUTS BACK THE TRACK'S OWN TWO, IN THE ENGINE TOO (the review, 2026-11-15). BRILLIANCE and
// VELOCITY are ids 32 and 33: past a preset's 32, so the three places that go through a synth's
// settings for undo (soundMark sees a change, restoreSounds puts it back, sendSynthPatch tells the
// engine) must each count a SONG's settings, not a preset's. Counting a preset's in any of the
// three passed every test.
void testUndoPutsBackTheTracksOwnTwo() {
    std::unique_ptr<Rig> rig(onJaxep());
    Session& session = rig->app.session();
    TrackData& track = rig->project().tracks[1];
    const auto still = [&] {
        for (int i = 0; i < 12; ++i) {
            rig->frames(25);
            rig->render(256);
        }
    };
    // A soft note against a full one, in dB: about 0 with VELOCITY off, far under with it on.
    const auto softUnderFull = [&] {
        still();
        const float soft = jxLoud(*rig, 0.3f);
        still();
        const float full = jxLoud(*rig, 1.f);
        still();
        return 20.f * std::log10(soft / full);
    };
    // A plain sound, so a note's peak does not hang on where the two saws' beat is (CLAUDE.md).
    session.chooseSynthPreset(1, 5);
    session.setSynthSetting(1, jx3p::SourceMix, 0);
    session.setSynthSetting(1, jx3p::Attack, 0);
    session.setSynthSetting(1, jx3p::Chorus, 0);
    session.setSynthSetting(1, jx3p::Brilliance, 12);
    still();
    CHECK(track.jx3p.value[jx3p::Velocity] == 1 && softUnderFull() < -6.f);
    const int stepsWere = session.undoSteps();
    // BRILLIANCE turned: a step of its own (soundMark saw id 33 move)...
    session.setSynthSetting(1, jx3p::Brilliance, 99);
    still();
    CHECK(session.undoSteps() == stepsWere + 1);
    // ...then VELOCITY off: another, and the engine plays a soft note as loud as a full one.
    session.setSynthSetting(1, jx3p::Velocity, 0);
    still();
    CHECK(session.undoSteps() == stepsWere + 2 && std::fabs(softUnderFull()) < 1.f);
    // Undo: VELOCITY is on again in the project AND in the sound (the patch was sent with all 34).
    CHECK(session.undo());
    still();
    CHECK(track.jx3p.value[jx3p::Velocity] == 1 && track.jx3p.value[jx3p::Brilliance] == 99 && softUnderFull() < -6.f);
    // Undo: BRILLIANCE is back where it was. Redo, twice: both as they were turned.
    CHECK(session.undo());
    still();
    CHECK(track.jx3p.value[jx3p::Brilliance] == 12 && track.jx3p.value[jx3p::Velocity] == 1 && track.preset == 5);
    CHECK(session.redo() && track.jx3p.value[jx3p::Brilliance] == 99 && track.jx3p.value[jx3p::Velocity] == 1);
    CHECK(session.redo() && track.jx3p.value[jx3p::Velocity] == 0);
    still();
    CHECK(std::fabs(softUnderFull()) < 1.f);
    // BRILLIANCE in the engine: undone to 1.2 from 9.9, the same note is duller (less of it over 3 kHz
    // is not measured here: its peak through a filter two octaves lower is smaller).
    session.setSynthSetting(1, jx3p::Velocity, 1);
    session.setSynthSetting(1, jx3p::Cutoff, 40);
    session.setSynthSetting(1, jx3p::Brilliance, 0);
    still();
    session.setSynthSetting(1, jx3p::Brilliance, 100);
    still();
    still();
    const float bright = jxLoud(*rig, 1.f);
    CHECK(session.undo());
    still();
    still();
    const float dull = jxLoud(*rig, 1.f);
    CHECK(track.jx3p.value[jx3p::Brilliance] == 0 && dull < 0.8f * bright);
}

// JAX-EP'S HINTS FIT THE BAR (the review, 2026-11-15: a hint of sixty letters passed every test,
// since the bar drops what does not fit and nothing then overlaps). MAIN and the five pages, the
// cursor on every stop, A held and not, with either help: nothing over.
void testJaxepsHintsFit() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(onJaxep());
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.jaxep();
    CHECK(screen.pageCount() == 6 && rig->project().tracks[rig->project().track].kind == TrackKind::Jx3p);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu) {
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : jx3pSpec().itemCount(page);
            for (int cursor = 0; cursor < stops; ++cursor) {
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP"; // (App sets it each frame from the controls)
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL JAX-EP's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
            }
        }
    }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + jx3p::kSettings));
}

// FM-6's bar holds every hint on MAIN and on its eight pages, at every stop, A held and not, with
// either help (owner: hints must fit; a page of six rows has the same bar as one of two).
void testFm6sHintsFit() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    CHECK(rig->app.session().addTrack(TrackKind::Fm6));
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.fm6();
    CHECK(screen.pageCount() == 9);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu) {
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : fm6Spec().itemCount(page);
            for (int cursor = 0; cursor < stops; ++cursor) {
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL FM-6's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
            }
        }
    }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + fm6::kSettings + 5)); // every setting, OPERATORS ON as six
}

} // namespace

// The SPEED TEST plays the busiest song with the helpers the program uses, as well as without
// (the handheld cannot otherwise tell whether they carry it). Briefly here: that it plays, and
// that the helpers asked for are the ones at work.
void testTheSpeedTestPlaysTheBusiestSongWithHelpers() {
    const WholeSongTimes alone = timeWholeSong(0, 0.3), helped = timeWholeSong(2, 0.3);
    CHECK(alone.helpers == 0 && helped.helpers == 2);
    CHECK(alone.blocks == 56 && helped.blocks == 56); // 0.3 s in blocks of 256 frames
    CHECK(alone.peak > 0.05f && helped.peak > 0.05f); // a song playing, not silence
    CHECK(alone.meanMs > 0.0 && alone.worstMs >= alone.p99Ms && alone.p99Ms >= 0.0 && helped.worstMs >= helped.p99Ms);
    CHECK(!alone.realtime && !helped.realtime); // not asked for outside the SPEED TEST itself
}

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

    // Held over, the D-pad runs: clicks, eight a second every way (ui::kClickEveryMs; gesture audit G32: sideways
    // was four). Not the cursor's 33.
    rig.key(X, true);
    rig.key(UP, true);
    rig.frames(62); // a second: the press, then a click each 125 ms after the 170 wait, counted from when due
    rig.key(UP, false);
    CHECK(drums.fx(0, 4).pitch == static_cast<int>(1 + 1 + (62 * 16 - 16 - ui::kClickDelayMs) / ui::kClickEveryMs));
    rig.tap(DOWN, 12);
    rig.key(RIGHT, true);
    rig.frames(20); // a third of a second: the press, and two more after the wait (at four a second: one)
    rig.key(RIGHT, false);
    CHECK(drums.fx(0, 4).hits() == 4);
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
    rig.frames(8); // 38 frames in all at twice the held D-pad's clicks (16 a second at STICK SPEED 100%): 1 + 9.7
    CHECK(drums.fx(0, 4).pitch == 10 && drums.fx(0, 4).hits() == 1);
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
    // B with X held empties the hit's effects and keeps the hit (gesture audit G11: easy_test xempty).
    rig.key(X, true);
    CHECK(drums.effecting());
    rig.tap(B);
    rig.frames(1);
    CHECK(drums.effecting() && drums.level(0, 4) == 0.8f);
    // If the hit goes while X is held (deleted, here from outside), the hold is over.
    drums.setStep(0, 4, 0.f);
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
    CHECK(song.cursorColumn() == 1 && song.cursorOnHead()); // back along the heads (owner, 2026-11-14), not into the cell
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
    // R1: the keyboard, for that track. Type, keep, and the SONG screen is back with the name on the head.
    rig.tap(R1);
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
    rig.tap(R1);
    rig.frames(1);
    rig.tap(Y, 2);
    rig.tap(B);
    CHECK(rig.view() == View::Song && kept == rig.project().tracks[1].name);
    // B with a name typed keeps it and comes back (owner, 2026-11-15).
    rig.tap(R1);
    rig.frames(1);
    rig.tap(Y);
    CHECK(rig.app.settings().typed() == kept.substr(0, 1));
    rig.tap(B);
    CHECK(rig.view() == View::Song && std::string(rig.project().tracks[1].name) == kept.substr(0, 1) && song.cursorOnHead());
    // R1 let go after the cursor left the heads names nothing.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.view() == View::Song);
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

// On a track's head, A changes its instrument.
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
    // A: the list, on the instrument the track has. B puts it away with nothing changed.
    rig.tap(A);
    rig.tap(B);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().trackCount() == 2);
    // A on the same one: nothing changes either (the sound is not put back to its first).
    rig.tap(A);
    rig.tap(A);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().tracks[1].macro[3] == 0.9f);
    // Down one and A: it is a JAX-EP now. No track was added.
    rig.tap(A);
    rig.tap(DOWN);
    rig.tap(A);
    TrackData& t = rig.project().tracks[1];
    CHECK(t.kind == TrackKind::Jx3p && rig.project().trackCount() == 2 && song.cursorOnHead() && song.cursorColumn() == 1);
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
    CHECK(session.setTrackKind(1, TrackKind::Fm6) && t.notes[0][0].notes[0].velocity == 0.8f);
    // Nonsense is refused.
    CHECK(!session.setTrackKind(1, TrackKind::None) && !session.setTrackKind(1, TrackKind::Fm6) && !session.setTrackKind(5, TrackKind::Drums) && !session.setTrackKind(-1, TrackKind::Drums));
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
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Phazed && rig.project().tracks[0].kind == TrackKind::Drums); // the FM-6 went; the one after it moved up
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
    // On every other tab too, and the tab does not turn (gesture audit G03: it was PATTERN's alone, and
    // elsewhere turned the tab). On SONG it is the scene under the cursor: nextscene_test.
    rig.key(SELECT, true);
    rig.tap(R2);
    CHECK(rig.view() == View::Device && rig.project().sceneSteps(0) == 24);
    rig.tap(L2);
    rig.key(SELECT, false);
    CHECK(rig.view() == View::Device && rig.project().sceneSteps(0) == 23);
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
    const int last = builtInPresets(TrackKind::Phazed) - 1;
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
    session.addTrack(TrackKind::Fm6);
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
        CHECK(way == STICK_RIGHT ? p.notes[slot].ratchet == 2 : p.notes[slot].glide == kGlideQuarter);
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
    // 3. Broken into notes (hold Y, A; G26: it was B), each note plays as the chord did: the ratchet and the nudge go with them.
    int keys[kChordNotes];
    const int count = p.chords[0].pitches(keys);
    rig.key(Y, true);
    rig.frames(2);
    rig.tap(A);
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
    session.addTrack(TrackKind::Fm6);
    rig.project().track = 1;
    session.setSynthMacro(1, fm6::MacroRelease, 0.f); // a note let go is gone at once, so a gap between two notes can be heard
    session.setSynthMacro(1, fm6::MacroAttack, 0.f);
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
    // Up: its glide one length longer, to FULL and no further. Down: one shorter, to OFF and no further.
    {
        const int longer[] = {kGlideQuarter, kGlideHalf, kGlideOne, kGlideTwo, kGlideFull, kGlideFull};
        for (int want : longer) {
            rig.tap(UP);
            CHECK(p.notes[slot].glide == want && roll.cursorStep() == 4);
        }
        const int shorter[] = {kGlideTwo, kGlideOne, kGlideHalf, kGlideQuarter, kGlideOff, kGlideOff};
        for (int want : shorter) {
            rig.tap(DOWN);
            CHECK(p.notes[slot].glide == want && roll.cursorStep() == 4);
        }
    }
    rig.tap(UP, 2); // half a step: what "on" was, and is 1 in a song
    rig.tap(RIGHT, 2);
    rig.key(X, false);
    CHECK(!roll.effecting() && p.notes[slot].ratchet == 3 && p.notes[slot].glide == 1);
    // With X let go the D-pad is the cursor again.
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 61 && p.notes[slot].ratchet == 3);
    rig.tap(LEFT);
    // The stick clicks too: one at once, and one way per push.
    rig.key(X, true);
    rig.tap(DOWN, 2); // the glide off, to see that the push below leaves it off
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
    rig.tap(UP, 2);
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
    rig.tap(DOWN, 2);
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
        int lengths[kGlideKinds] = {};
        for (int i = 0; i < 200; ++i) {
            const int ratchetWas = p.notes[at].ratchet, glideWas = p.notes[at].glide;
            rig.tap(Y);
            const NoteEvent& n = p.notes[at];
            CHECK(n.ratchet >= 2 || n.glide >= 1);
            CHECK(n.ratchet != ratchetWas || n.glide != glideWas);
            CHECK((n.ratchet == 0 || (n.ratchet >= 2 && n.ratchet <= 4)) && n.glide < kGlideKinds);
            ++lengths[n.glide < kGlideKinds ? n.glide : 0];
            ratchets = ratchets || n.ratchet >= 2;
            glides = glides || n.glide;
            both = both || (n.ratchet >= 2 && n.glide);
            plainRatchet = plainRatchet || (n.ratchet >= 2 && !n.glide);
        }
        CHECK(ratchets && glides && both && plainRatchet && roll.effecting() && !roll.picking());
        for (int kind = 0; kind < kGlideKinds; ++kind) CHECK(lengths[kind] >= 5); // every length comes up, and no glide at all does too
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
        // A chord: one or more of its own six (the odds are held in tests/chordfxapp_test.cpp),
        // never nothing, and never what it was.
        const auto same = [](const ChordEvent& a, const ChordEvent& b) { return a.ratchet == b.ratchet && std::memcmp(&a.play, &b.play, sizeof a.play) == 0; };
        roll.moveCursorTo(48, 10);
        rig.key(X, true);
        CHECK(roll.effecting());
        for (int i = 0; i < 12; ++i) {
            const ChordEvent was = p.chords[0];
            rig.tap(Y);
            CHECK((p.chords[0].ratchet >= 2 || !p.chords[0].play.atRest()) && p.chords[0].ratchet <= 4 && !same(p.chords[0], was));
        }
        rig.key(X, false);
        // A roll is undone like any other change.
        rig.frames(60);
        const ChordEvent kept = p.chords[0];
        rig.key(X, true);
        rig.tap(Y);
        rig.key(X, false);
        CHECK(!same(p.chords[0], kept));
        rig.frames(60);
        rig.key(SELECT, true);
        rig.tap(B);
        rig.key(SELECT, false);
        CHECK(same(p.chords[0], kept));
    }
}

// ---- undo kept where the song is the same song ----------------------------------------------

// A hit placed on a track by the code, as an edit is: a step of undo, then still for a while.
void placeAndSettle(Rig& rig, int track, int lane, int step) {
    rig.project().tracks[track].drums[0][0].level[lane][step] = 0.8f;
    rig.app.session().sendDrumPattern(track, 0);
    rig.frames(60); // a second: the burst of edits is over
    rig.render(512);
}

// A full queue makes the song be sent again (a resync). It is the same song: undo stays.
void testAResyncKeepsUndo() {
    Rig rig;
    Session& session = rig.app.session();
    rig.frames(2);
    placeAndSettle(rig, 0, 2, 4);
    CHECK(session.undoSteps() == 1);
    Command c;
    c.type = Command::SetTempo;
    c.value = 120.f;
    for (int i = 0; i < 20000; ++i) rig.engine->send(c); // more than the queue holds
    CHECK(rig.engine->dropped() > 0);
    rig.render(512);
    rig.frames(2);
    CHECK(session.resyncs() == 1);
    CHECK(session.undoSteps() == 1);
    CHECK(session.undo() && rig.project().tracks[0].drums[0][0].level[2][4] == 0.f);
    CHECK(session.redo() && rig.project().tracks[0].drums[0][0].level[2][4] > 0.f);
}

// Deleting a track, or giving one another instrument, keeps what undo knew of the other tracks.
void testATrackDeletedOrChangedKeepsUndo() {
    {
        Rig rig;
        Session& session = rig.app.session();
        CHECK(session.addTrack(TrackKind::Drums) && session.addTrack(TrackKind::Drums));
        rig.project().tracks[1].drums[0][0].level[5][12] = 0.8f; // the track that goes: its hit must not land on the one after it
        session.sendDrumPattern(1, 0);
        rig.frames(2);
        placeAndSettle(rig, 0, 1, 0);
        placeAndSettle(rig, 2, 3, 8);
        CHECK(session.undoSteps() == 2);
        CHECK(session.removeTrack(1) && rig.project().trackCount() == 2);
        rig.frames(60);
        CHECK(session.undoSteps() == 2);
        // An edit after the delete: its step is the song as it was just after it.
        placeAndSettle(rig, 0, 6, 6);
        CHECK(session.undoSteps() == 3);
        CHECK(session.undo() && rig.project().tracks[0].drums[0][0].level[6][6] == 0.f && rig.project().tracks[1].drums[0][0].level[3][8] > 0.f);
        CHECK(rig.project().tracks[1].drums[0][0].level[5][12] == 0.f);
        // The third track is the second now, and its step of undo went with it.
        CHECK(session.undo() && rig.project().tracks[1].drums[0][0].level[3][8] == 0.f && rig.project().tracks[0].drums[0][0].level[1][0] > 0.f);
        CHECK(rig.project().tracks[1].drums[0][0].level[5][12] == 0.f);
        CHECK(session.undo() && rig.project().tracks[0].drums[0][0].level[1][0] == 0.f);
        CHECK(rig.project().tracks[1].drums[0][0].level[5][12] == 0.f);
        CHECK(rig.project().trackCount() == 2); // the delete itself is not undone
        CHECK(session.redo() && session.redo() && rig.project().tracks[1].drums[0][0].level[3][8] > 0.f);
    }
    {
        Rig rig;
        Session& session = rig.app.session();
        CHECK(session.addTrack(TrackKind::Drums));
        rig.frames(2);
        placeAndSettle(rig, 0, 1, 0);
        placeAndSettle(rig, 1, 3, 8);
        CHECK(session.setTrackKind(1, TrackKind::Phazed));
        rig.frames(60);
        CHECK(session.undoSteps() == 2);
        CHECK(session.undo() && rig.project().tracks[1].kind == TrackKind::Phazed); // passed by: another kind now
        CHECK(session.undo() && rig.project().tracks[0].drums[0][0].level[1][0] == 0.f);
    }
}

// From silence: a hit on track 0's lane 1, or a note on track 1 let go after 20000 frames. 150000 frames.
std::vector<float> heardFromSilence(Rig& rig, bool note) {
    rig.render(500000); // every tail out first, and the buses at rest (ten seconds of nothing)
    Command c;
    c.track = 1;
    c.a = note ? 57 : 0;
    c.value = 0.8f;
    c.type = note ? Command::NoteOn : Command::Trigger;
    if (!note) c.track = 0;
    rig.engine->send(c);
    std::vector<float> out = rig.render(20000);
    c.type = Command::NoteOff;
    if (note) rig.engine->send(c);
    const std::vector<float> rest = rig.render(130000);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

// Undo covers a track's effects, sends, sound and GLIDE FROM, the master's effects and the buses'
// (owner, 2026-11-14): each change is a step (a burst is one), and undo and redo put it back in the
// project and in the engine. Nothing on the FX screen says so: the session sees the change itself.
// The engine is held to the project by sound, beside a song opened from the same text: a drum hit
// through the effects, sends and buses, sample for sample (to 1e-4 of its peak: the old FILTER's
// state does not quite reach zero). The synth's note is held to itself before the changes, by its
// loudness in four windows (its own clocks run apart; and a sound set live is not heard the same as
// one opened from text, on main too: its attack differs), where the sound changed here is far more
// than the 25% allowed.
void testUndoCoversSounds() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    addPhazed(rig);
    session.view = View::Effects;
    rig.frames(60);
    rig.render(512);
    const auto settle = [&] { rig.frames(50); };
    const auto windows = [](const std::vector<float>& a) {
        const int edges[] = {0, 4000, 30000, 60000, 150000};
        std::vector<double> e(4, 0.0);
        for (int w = 0; w < 4; ++w)
            for (int i = 2 * edges[w]; i < 2 * edges[w + 1]; ++i) e[static_cast<std::size_t>(w)] += double(a[i]) * a[i];
        return e;
    };
    const auto alike = [](const std::vector<double>& a, const std::vector<double>& b) {
        bool same = true;
        for (std::size_t w = 0; w < a.size(); ++w) same = same && ((a[w] < 1e-6 && b[w] < 1e-6) || std::fabs(a[w] - b[w]) <= 0.25 * std::max(a[w], b[w]));
        return same;
    };
    const auto sameHit = [](const std::vector<float>& a, const std::vector<float>& b) {
        float most = 0.f;
        for (std::size_t i = 0; i < a.size(); ++i) most = std::max(most, std::fabs(a[i] - b[i]));
        return Rig::peak(a) > 0.01f && most <= 1e-4f * Rig::peak(a);
    };
    const auto heardAsTheProject = [&] {
        Rig opened;
        std::unique_ptr<Project> copy(new Project);
        CHECK(parseSong(songText(project), *copy));
        opened.project() = *copy;
        opened.app.session().applyProject();
        opened.frames(2);
        return sameHit(heardFromSilence(rig, false), heardFromSilence(opened, false));
    };
    CHECK(heardAsTheProject());
    // A setting turned on an effect that stays (the master's FILTER): undo tells the engine the old
    // value. (Here, before any TAPE or HALL: they are not the same from one hit to the next.)
    {
        const int before = session.undoSteps();
        session.setEffect(kMasterTrack, 0, EffectKind::Filter);
        session.setEffectParam(kMasterTrack, 0, 0, 0.3f);
        settle();
        const std::vector<float> asSet = heardFromSilence(rig, false);
        CHECK(sameHit(heardFromSilence(rig, false), asSet)); // (the same twice: the check can be trusted)
        session.setEffectParam(kMasterTrack, 0, 0, 0.8f);
        settle();
        CHECK(!sameHit(heardFromSilence(rig, false), asSet));
        CHECK(session.undo() && project.masterEffects[0].param[0] == 0.3f);
        CHECK(sameHit(heardFromSilence(rig, false), asSet) && heardAsTheProject());
        CHECK(session.undo() && project.masterEffects[0].kind == 0 && session.undoSteps() == before);
        CHECK(heardAsTheProject());
    }
    const std::string start = songText(project);
    const std::vector<float> hitAtStart = heardFromSilence(rig, false);
    const std::vector<double> noteAtStart = windows(heardFromSilence(rig, true));
    const int steps = session.undoSteps();

    // An effect put in, turned (one burst: one step), a send, a bus's LEVEL, the master, GLIDE FROM, a sound.
    session.setEffect(0, 0, EffectKind::Tape);
    session.setEffectParam(0, 0, 1, 0.3f);
    session.setEffectParam(0, 0, 1, 0.35f);
    settle();
    CHECK(session.undoSteps() == steps + 1);
    session.setSend(0, 1, 0.6f);
    settle();
    session.setEffectParam(kEchoBus, 0, 3, 0.4f); // the ECHO bus's LEVEL
    session.setSend(0, 0, 0.5f);
    settle();
    session.setEffect(kMasterTrack, 1, EffectKind::Filter);
    session.setEffectParam(kMasterTrack, 1, 0, 0.3f);
    settle();
    session.setGlideFrom(1, kFromPaired);
    settle();
    session.chooseSynthPreset(1, 3);
    session.setSynthMacro(1, 2, 0.8f);
    settle();
    session.setSynthSetting(1, 5, project.tracks[1].patch.value[5] + 1);
    settle();
    CHECK(session.undoSteps() == steps + 7);
    const std::string end = songText(project);
    CHECK(heardAsTheProject());
    CHECK(!sameHit(heardFromSilence(rig, false), hitAtStart) && !alike(windows(heardFromSilence(rig, true)), noteAtStart)); // (the checks can tell)

    // Back, one at a time; and the engine with it.
    const unsigned images = session.imagesSent();
    CHECK(session.undo() && session.undo()); // the setting, then the preset and macro
    CHECK(project.tracks[1].preset != 3 && project.tracks[1].macro[2] == 0.5f);
    CHECK(session.undo() && project.tracks[1].glideFrom == kFromTop);
    CHECK(session.undo() && project.masterEffects[1].kind == 0);
    CHECK(session.undo() && project.busEffects[0].param[3] == 1.f && project.tracks[0].send[0] == 0.f);
    CHECK(session.undo() && project.tracks[0].send[1] == 0.f);
    CHECK(session.undo() && project.tracks[0].effects[0].kind == 0);
    CHECK(session.undoSteps() == steps && songText(project) == start);
    CHECK(session.imagesSent() == images); // none of it is a pattern: no track sent whole
    rig.frames(60);
    CHECK(session.undoSteps() == steps); // putting it back is not itself an edit
    CHECK(heardAsTheProject());
    CHECK(sameHit(heardFromSilence(rig, false), hitAtStart) && alike(windows(heardFromSilence(rig, true)), noteAtStart));
    // ...and forward again.
    for (int i = 0; i < 7; ++i) CHECK(session.redo());
    CHECK(songText(project) == end && session.imagesSent() == images);
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 7 && session.redoSteps() == 0);
    CHECK(heardAsTheProject());

    // A TYPE change taken back: the old type, its settings and its automation lanes, together. (HALL is
    // not the same to the sample from one hit to the next, as it is not from one way of cutting the
    // sound into blocks to another (CLAUDE.md): it is held by loudness, as the note is.)
    TrackData& track = project.tracks[0];
    session.setEffect(0, 2, EffectKind::Hall);
    session.setEffectParam(0, 2, 0, 0.9f);
    session.setEffectParam(0, 2, 3, 0.6f); // HALL's MIX: its fourth
    session.setAutomation(0, 0, autoLaneOfEffect(2, 3), 6, 0.3f);
    settle();
    const EffectData hall = track.effects[2];
    const std::vector<double> asHall = windows(heardFromSilence(rig, false));
    const int typeSteps = session.undoSteps();
    CHECK(changeEffectType(session, 0, 2, EffectKind::Reverb));
    settle();
    CHECK(track.effects[2].kind == static_cast<int>(EffectKind::Reverb) && track.automation[0][autoLaneOfEffect(2, 2)][6] == 0.3f && track.automation[0][autoLaneOfEffect(2, 3)][6] < 0.f);
    CHECK(session.undoSteps() == typeSteps + 1);
    const std::vector<double> asReverb = windows(heardFromSilence(rig, false));
    CHECK(!alike(asHall, asReverb));
    CHECK(session.undo());
    CHECK(track.effects[2].kind == static_cast<int>(EffectKind::Hall) && track.effects[2].param[0] == hall.param[0] && track.effects[2].param[3] == hall.param[3]);
    CHECK(track.automation[0][autoLaneOfEffect(2, 3)][6] == 0.3f && track.automation[0][autoLaneOfEffect(2, 2)][6] < 0.f); // MIX's lane is HALL's again
    CHECK(alike(windows(heardFromSilence(rig, false)), asHall)); // and the engine has HALL again
    CHECK(session.redo() && track.effects[2].kind == static_cast<int>(EffectKind::Reverb) && track.automation[0][autoLaneOfEffect(2, 2)][6] == 0.3f);
    CHECK(alike(windows(heardFromSilence(rig, false)), asReverb));
    // An effect emptied comes back, settings and all.
    session.setEffect(0, 2, EffectKind::None);
    settle();
    CHECK(!alike(windows(heardFromSilence(rig, false)), asReverb));
    CHECK(session.undo() && track.effects[2].kind == static_cast<int>(EffectKind::Reverb) && track.automation[0][autoLaneOfEffect(2, 2)][6] == 0.3f);
    CHECK(alike(windows(heardFromSilence(rig, false)), asReverb));
}

// What undo covers besides the patterns is compared as the song file writes it too (review,
// 2026-11-14): a third read back from the step's text is 0.333332986, not 0.333333343, and an undo of
// one hit rounded every such effect setting, send and macro and told the engine (a synth's whole
// patch; the master and the buses on every undo). Now each is left alone, to the last bit.
void testUndoLeavesWhatTheTextRounds() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    addPhazed(rig); // track 1
    CHECK(session.addTrack(TrackKind::Phazed)); // track 2
    rig.frames(60);
    rig.render(512);
    const float third = 1.f / 3.f;
    session.setEffect(0, 0, EffectKind::Filter);
    session.setEffectParam(0, 0, 0, third);
    session.setSend(0, 0, third);
    session.setEffect(kMasterTrack, 0, EffectKind::Filter);
    session.setEffectParam(kMasterTrack, 0, 0, third);
    session.setEffectParam(kEchoBus, 0, 0, third);
    session.setSynthMacro(1, 2, third);
    rig.frames(60);
    rig.render(512);
    placeAndSettle(rig, 0, 1, 0);
    project.tracks[2].notes[0][0].notes[0] = NoteEvent{0, 60, 1.f, 0.f, 0.8f};
    session.sendNotePattern(2, 0);
    rig.frames(60);
    rig.render(512);
    const unsigned images = session.imagesSent();
    CHECK(session.undo() && session.undo());
    CHECK(project.tracks[0].drums[0][0].level[1][0] == 0.f && project.tracks[2].notes[0][0].notes[0].velocity == 0.f);
    CHECK(session.imagesSent() - images == 2);
    CHECK(project.tracks[0].effects[0].param[0] == third); // to the last bit: not read back from the text
    CHECK(project.tracks[0].send[0] == third);
    CHECK(project.masterEffects[0].param[0] == third);
    CHECK(project.busEffects[0].param[0] == third);
    CHECK(project.tracks[1].macro[2] == third);
    // ...and a value that did change is still put back (as the text holds it).
    session.setSynthMacro(1, 2, 0.9f);
    session.setEffectParam(kMasterTrack, 0, 0, 0.9f);
    rig.frames(60);
    CHECK(session.undo() && std::fabs(project.tracks[1].macro[2] - third) < 1e-6f && std::fabs(project.masterEffects[0].param[0] - third) < 1e-6f);
}

// A bus's effect or LEVEL changed by itself is a step of undo, and a Sampler track's settings are
// one too, put back on undo and on redo (the review's surviving mutants: each shared a burst with
// something else, or was not tried).
void testUndoCoversABusAndASampler() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Sampler)); // track 1
    rig.frames(60);
    rig.render(512);
    const int steps = session.undoSteps();
    session.setEffectParam(kEchoBus, 0, 3, 0.4f); // the ECHO bus's LEVEL, alone
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 1);
    const int kind = project.busEffects[1].kind;
    session.setEffect(kSpaceBus, 0, EffectKind::None); // the SPACE bus emptied, alone
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 2);
    const int was = project.tracks[1].sampler[sampler::Tune];
    session.setSamplerSetting(1, sampler::Tune, was + 37);
    session.setSamplerSetting(1, sampler::Level, 20);
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 3);
    CHECK(session.undo() && project.tracks[1].sampler[sampler::Tune] == was && project.tracks[1].sampler[sampler::Level] == sampler::settingDefault(sampler::Level));
    CHECK(session.undo() && project.busEffects[1].kind == kind && kind != 0);
    CHECK(session.undo() && project.busEffects[0].param[3] == 1.f && session.undoSteps() == steps);
    CHECK(session.redo() && session.redo() && session.redo());
    CHECK(project.busEffects[0].param[3] == 0.4f && project.busEffects[1].kind == 0 && project.tracks[1].sampler[sampler::Tune] == was + 37 && project.tracks[1].sampler[sampler::Level] == 20);
}

// An undo of one hit sends the one track it changed. The others are compared as the song file
// writes them: a keyboard's 90/127 or a setting written with WRITE on is not the number the text
// reads back to the last bit, and each such track was sent whole and its values rounded.
void testUndoSendsOnlyTheTrackItChanged() {
    Rig rig;
    Session& session = rig.app.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Drums) && session.addTrack(TrackKind::Drums));
    rig.frames(2);
    project.tracks[1].drums[0][0].level[0][0] = 90.f / 127.f; // as a keyboard plays it
    session.sendDrumPattern(1, 0);
    session.setAutomation(2, 0, 9, 0, 0.123456f); // as WRITE writes it
    rig.frames(60);
    rig.render(512);
    const float hit = project.tracks[1].drums[0][0].level[0][0], written = project.tracks[2].automation[0][9][0];
    placeAndSettle(rig, 0, 3, 3);
    const unsigned sent = session.imagesSent();
    CHECK(session.undo() && project.tracks[0].drums[0][0].level[3][3] == 0.f);
    CHECK(session.imagesSent() - sent == 1);
    CHECK(project.tracks[1].drums[0][0].level[0][0] == hit && project.tracks[2].automation[0][9][0] == written); // to the last bit
    // A track that did change is still put back and sent.
    const unsigned again = session.imagesSent();
    CHECK(session.redo() && project.tracks[0].drums[0][0].level[3][3] > 0.f && session.imagesSent() - again == 1);
    // What is compared is every part of the patterns: automation, a cell's length, notes.
    session.setAutomation(2, 0, 9, 4, 0.5f);
    rig.frames(60);
    CHECK(session.undo() && project.tracks[2].automation[0][9][4] < 0.f);
    session.setCellBars(1, 0, 2);
    rig.frames(60);
    CHECK(session.undo() && project.tracks[1].bars[0] == 1);
    session.setCellBars(2, 1, 2); // an empty cell: its length is all that changes
    rig.frames(60);
    CHECK(session.undo() && project.tracks[2].bars[1] == 1);
}


// A busy song of twelve tracks: drums and Phazed by turns, `scenes` scenes of `bars` bars, and
// values as a keyboard and WRITE leave them (that the text rounds).
void fillBusySong(Rig& rig, int scenes, int bars) {
    Session& session = rig.app.session();
    Project& p = rig.project();
    for (int t = 1; t < kNumTracks; ++t) session.addTrack(t % 2 ? TrackKind::Drums : TrackKind::Phazed);
    for (int t = 0; t < kNumTracks; ++t)
        for (int sc = 0; sc < scenes; ++sc) {
            p.tracks[t].bars[sc] = bars;
            for (int b = 0; b < bars; ++b) {
                if (p.tracks[t].kind == TrackKind::Drums) {
                    DrumPattern& d = p.tracks[t].drums[sc][b];
                    for (int lane = 0; lane < kDrumLanes; ++lane)
                        for (int st = lane % 2; st < kSteps; st += 2) d.level[lane][st] = 90.f / 127.f;
                } else {
                    NotePattern& n = p.tracks[t].notes[sc][b];
                    for (int k = 0; k < 32; ++k) n.notes[k] = NoteEvent{k / 2, 48 + k, 1.f, 0.f, 100.f / 127.f};
                }
                for (int i = 0; i < kSteps; ++i) p.tracks[t].automation[sc][0][b * kSteps + i] = 0.123456f;
            }
        }
    session.applyProject();
    rig.frames(60);
    rig.render(512);
}

// The end of a burst of edits to one track, and an undo and a redo of it, write out as text and
// read back that track, not the song. They did the whole song each time, on the screen's thread:
// on the desk, at 2.5 MB of text, 87 ms at the end of an edit and 160 ms an undo.
void testUndoWorksOnTheTrackItChanged() {
    Rig rig;
    Session& session = rig.app.session();
    Project& p = rig.project();
    fillBusySong(rig, 4, 2);
    const std::size_t size = songText(p).size();
    std::uint64_t written = tracksWritten();
    p.tracks[3].drums[1][1].level[1][1] = 0.5f;
    session.sendDrumPattern(3, 1);
    session.noteEdit();
    rig.frames(60);
    CHECK(session.undoSteps() == 1 && tracksWritten() - written == 1);
    written = tracksWritten();
    std::uint64_t read = bytesRead();
    CHECK(session.undo() && std::fabs(p.tracks[3].drums[1][1].level[1][1] - 90.f / 127.f) < 1e-6f);
    CHECK(tracksWritten() - written <= 2); // (the track compared with the step, as text: two)
    CHECK(bytesRead() - read < size / 6);
    written = tracksWritten();
    read = bytesRead();
    CHECK(session.redo() && p.tracks[3].drums[1][1].level[1][1] == 0.5f);
    CHECK(tracksWritten() - written <= 3); // (and the track as it was, for the step of undo)
    CHECK(bytesRead() - read < size / 6);
    CHECK(songText(p).find("hit 2 2 0.5") != std::string::npos);
    // A scene's length, where the longest cell in it is on a track the undo does not read: still its length.
    session.setCellBars(5, 0, 8);
    session.setSceneSteps(0, 100);
    rig.frames(60);
    p.tracks[3].drums[0][0].level[0][1] = 1.f;
    session.sendDrumPattern(3, 0);
    session.noteEdit();
    rig.frames(60);
    CHECK(p.steps[0] == 100 && session.undo() && p.steps[0] == 100 && p.tracks[3].drums[0][0].level[0][1] == 0.f && p.tracks[5].bars[0] == 8);
    // That cell made a bar long again: the length stays 100, a whole scene and more, and the text
    // writes it so. Read back, such a length is "every step" (0), whole or a part at a time.
    session.setCellBars(5, 0, 1);
    rig.frames(60);
    CHECK(p.steps[0] == 100);
    p.tracks[3].drums[0][0].level[0][2] = 1.f;
    session.sendDrumPattern(3, 0);
    session.noteEdit();
    rig.frames(60);
    CHECK(session.undo() && p.steps[0] == 0 && std::fabs(p.tracks[3].drums[0][0].level[0][2] - 90.f / 127.f) < 1e-6f);
}

// NEXT SCENE taken back reads the step whole: the sixteenth scene comes from it. A track that the
// move left as it was (the same in every scene from the one copied on) keeps its sixteenth.
void testNextSceneTakenBackReadsTheStepWhole() {
    Rig rig;
    Session& session = rig.app.session();
    Project& p = rig.project();
    CHECK(session.addTrack(TrackKind::Drums));
    rig.frames(2);
    for (int scene = 0; scene < kMaxScenes; ++scene) session.setAutomation(1, scene, 2, 0, 0.25f);
    placeAndSettle(rig, 0, 1, 1);
    CHECK(session.nextScene() == 1);
    rig.frames(60);
    CHECK(session.undo() && p.tracks[1].automation[kMaxScenes - 1][2][0] == 0.25f && p.tracks[0].drums[1][0].level[1][1] == 0.f);
}

// An undo that reads back only the tracks that changed does what it did reading the step whole:
// two of the same song, the same edits, undos and redos, one of them reading every step whole.
void testUndoIsAsItWasReadWhole() {
    Rig a, b;
    b.app.session().readUndoWhole(true);
    Rig* both[2] = {&a, &b};
    for (Rig* rig : both) fillBusySong(*rig, 3, 2);
    std::uint32_t seed = std::getenv("UNDO_SEED") ? static_cast<std::uint32_t>(std::atoi(std::getenv("UNDO_SEED"))) : 777u;
    const auto roll = [&](int n) { seed = seed * 1664525u + 1013904223u; return static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(n)); };
    int checked = 0, wrong = 0, undone = 0;
    for (int round = 0; round < 200; ++round) {
        const int op = roll(14), t = roll(a.project().trackCount()), scene = roll(4), bar = roll(kMaxBars), lane = roll(kDrumLanes), step = roll(kSteps);
        // (A level the program can make: 0.05 and up. One under it, that no gesture makes, the file holds
        // as 0.05, so a track with one is not its text read back. Read whole, such a track was put back
        // as the text has it on an undo of any other track; read in parts, it is left as it is.)
        const float level = roll(3) ? static_cast<float>(7 + roll(121)) / 127.f : 0.f;
        const float value = static_cast<float>(roll(1000)) / 997.f;
        const int kindRoll = roll(2), bars = 1 << roll(4), steps = roll(3) ? 0 : 1 + roll(40), repeats = 1 + roll(4), autoLane = roll(2);
        const bool settle = roll(2) == 0 && op < 7; // the burst of edits over, or more of it to come
        bool didUndo = false;
        for (Rig* rig : both) {
            Session& session = rig->app.session();
            Project& p = rig->project();
            TrackData& track = p.tracks[t];
            switch (op) {
            case 0: case 1: case 2:
                if (track.kind == TrackKind::Drums) {
                    track.drums[scene][bar].level[lane][step] = level;
                    session.sendDrumPattern(t, scene);
                } else {
                    track.notes[scene][bar].notes[lane * 8 + step % 8] = NoteEvent{step, 40 + lane, 1.f, 0.f, level};
                    session.sendNotePattern(t, scene);
                }
                session.noteEdit();
                break;
            case 3: session.setAutomation(t, scene, autoLane, bar * kSteps + step, value); break;
            case 4: session.setCellBars(t, scene, bars); break;
            case 5: session.setSceneSteps(scene, steps); break;
            case 6: p.repeats[scene] = repeats; session.noteEdit(); break;
            case 7: case 8: case 9: didUndo = session.undo(); break;
            case 10: case 11: session.redo(); break;
            case 12: if (t > 0) session.setTrackKind(t, kindRoll ? TrackKind::Drums : TrackKind::Phazed); break;
            default: if (t > 0 && p.trackCount() > 8) session.removeTrack(t); else session.addTrack(kindRoll ? TrackKind::Drums : TrackKind::Phazed); break;
            }
            rig->frames(settle ? 60 : 3);
            rig->render(256);
            rig->engine->collectGarbage();
        }
        undone += didUndo;
        const Project& x = a.project();
        const Project& y = b.project();
        bool same = std::memcmp(x.repeats, y.repeats, sizeof x.repeats) == 0 && std::memcmp(x.steps, y.steps, sizeof x.steps) == 0;
        for (int i = 0; i < kNumTracks; ++i) {
            const TrackData& u = x.tracks[i];
            const TrackData& v = y.tracks[i];
            same = same && u.kind == v.kind && std::memcmp(u.bars, v.bars, sizeof u.bars) == 0 && u.drums.sameAs(v.drums) &&
                   u.notes.sameAs(v.notes) && u.automation.sameAs(v.automation);
        }
        same = same && a.app.session().undoSteps() == b.app.session().undoSteps() && a.app.session().redoSteps() == b.app.session().redoSteps() &&
               a.app.session().imagesSent() == b.app.session().imagesSent();
        ++checked;
        if (!same) {
            ++wrong;
            if (wrong == 1) {
                std::printf("    undo read in parts differs from read whole after round %d (op %d)\n", round, op);
                if (std::getenv("UNDO_DEBUG")) {
                    for (int i = 0; i < kNumTracks; ++i) {
                        const TrackData& u = x.tracks[i];
                        const TrackData& v = y.tracks[i];
                        if (u.kind != v.kind) std::printf("     track %d kind %d %d\n", i, int(u.kind), int(v.kind));
                        if (std::memcmp(u.bars, v.bars, sizeof u.bars)) std::printf("     track %d bars\n", i);
                        if (!u.drums.sameAs(v.drums)) std::printf("     track %d drums\n", i);
                        if (!u.notes.sameAs(v.notes)) std::printf("     track %d notes\n", i);
                        if (!u.automation.sameAs(v.automation)) std::printf("     track %d auto\n", i);
                    }
                    std::printf("     steps %d repeats %d undo %d %d redo %d %d images %u %u text %d\n", std::memcmp(x.steps, y.steps, sizeof x.steps), std::memcmp(x.repeats, y.repeats, sizeof x.repeats),
                                a.app.session().undoSteps(), b.app.session().undoSteps(), a.app.session().redoSteps(), b.app.session().redoSteps(), a.app.session().imagesSent(), b.app.session().imagesSent(), songText(x) == songText(y));
                }
            }
        }
    }
    CHECK(checked == 200 && wrong == 0 && undone > 20);
    CHECK(songText(a.project()) == songText(b.project()));
}

// ---- the pit stop: the button standards ------------------------------------------

// Is this text drawn in the hints bar, in the hint's own colour, somewhere along it?
bool hintedOn(const Canvas& canvas, Canvas& scratch, const char* words) {
    const Fonts& fonts = *canvas.fonts;
    for (float x = 4.f; x < ui::kScreenW - 40.f; x += 0.0625f)
        if (inked(canvas, scratch, 0, x, ui::kScreenH - ui::kHintsH + (ui::kHintsH - fonts.small.capHeight()) / 2.f + 1.f, words, ui::kDim)) return true;
    return false;
}

// HOLD B deletes an effect, with a bar that fills and says DELETING THIS EFFECT. A tap does nothing. Effects are
// in undo (since 2026-11-14), so SEL + B brings it back, settings and automation too: the words do not say
// CANNOT BE UNDONE (gesture audit G10; they did).
// One hold length a kind (gesture audit G29, 2026-11-25; each screen had its own second): half a second where
// undo brings it back (an effect, a MOD, EDIT's and VOICE's every box back, a scene emptied), a second where it
// cannot (a recording or folder, a song, an own preset or kit, a network forgotten, a bounce stopped). A track
// keeps its two seconds, red only after a quiet half second. An effect held 400 ms stays; at 560 ms it is gone.
void testTheHoldLengths() {
    CHECK(ui::kHoldUndoneMs == 500 && ui::kHoldForGoodMs == 1000);
    CHECK(FxScreen::kDeleteHoldMs == ui::kHoldUndoneMs && ModScreen::kDeleteHoldMs == ui::kHoldUndoneMs && ModKnob::kDeleteHoldMs == ui::kHoldUndoneMs);
    CHECK(KitScreen::kDeleteHoldMs == ui::kHoldUndoneMs && SongScreen::kSceneHoldMs == ui::kHoldUndoneMs);
    CHECK(SamplePicker::kDeleteHoldMs == ui::kHoldForGoodMs && KitScreen::kKitDeleteMs == ui::kHoldForGoodMs);
    CHECK(SynthScreen::kPresetDeleteMs == ui::kHoldForGoodMs); // (the sampler's is private: the same constant)
    CHECK(SettingsScreen::kForgetHoldMs == ui::kHoldForGoodMs && SettingsScreen::kStopHoldMs == ui::kHoldForGoodMs);
    CHECK(SongScreen::kTrackHoldWaitMs == 500 && SongScreen::kTrackHoldMs == 2000);
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Effects;
    session.setEffect(0, 0, EffectKind::Tape);
    rig.app.fx().goTo(0, 0);
    rig.frames(1);
    const EffectData& first = rig.project().tracks[0].effects[0];
    rig.key(B, true);
    rig.frames(26); // 400 ms (its clock starts on the frame after the press)
    CHECK(first.kind == static_cast<int>(EffectKind::Tape));
    rig.frames(10); // 560 ms
    CHECK(first.kind == 0);
    rig.key(B, false);
    rig.frames(1);
}

void testHoldBDeletesAnEffect() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    typedef FxScreen F;
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    const Fonts& fonts = *canvas.fonts;
    session.view = View::Effects;
    session.setEffect(0, 0, EffectKind::Tape);
    session.setEffectParam(0, 0, 1, 0.9f);
    session.setEffect(0, 1, EffectKind::Filter);
    session.setAutomation(0, 2, autoLaneOfEffect(0, 1), 5, 0.25f);
    const EffectData& first = rig.project().tracks[0].effects[0];
    const auto draw = [&] {
        canvas.clear();
        fx.draw(canvas.r, fonts);
    };
    fx.goTo(0, 0);
    rig.frames(1);
    draw();
    CHECK(hintedOn(canvas, scratch, "delete") && !hintedOn(canvas, scratch, "empty it"));
    // A tap: nothing goes.
    rig.tap(B);
    rig.frames(100);
    CHECK(first.kind == static_cast<int>(EffectKind::Tape) && near(first.param[1], 0.9f) && fx.deletePart() == 0.f);
    // Held: the bar fills, and the heading says so; let go before it is full and it stays.
    CHECK(std::string(F::kDeletingWords) == "DELETING THIS EFFECT");
    rig.key(B, true);
    rig.frames(20); // 320 ms of the half second (G29: undo brings an effect back; it was a second)
    CHECK(fx.deletePart() > 0.5f && fx.deletePart() < 1.f && first.kind == static_cast<int>(EffectKind::Tape));
    draw();
    CHECK(inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kPaneY + 10.f, F::kDeletingWords, ui::kText));
    CHECK(!canvas.is(F::kListX + 4, F::lineY(0) + F::kLineH / 2, ui::kPanel2)); // the line fills too
    rig.key(B, false);
    rig.frames(1);
    CHECK(fx.deletePart() == 0.f && first.kind == static_cast<int>(EffectKind::Tape));
    draw();
    CHECK(!inked(canvas, scratch, 0, F::kPaneX + 12.f, F::kPaneY + 10.f, F::kDeletingWords, ui::kText) && canvas.is(F::kListX + 4, F::lineY(0) + F::kLineH / 2, ui::kPanel2));
    // Held and moved off the line, by the D-pad or by the stick: nothing goes.
    rig.key(B, true);
    rig.frames(15);
    rig.tap(DOWN);
    rig.frames(60);
    rig.key(B, false);
    rig.tap(UP);
    CHECK(first.kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 0);
    rig.key(B, true);
    rig.frames(15);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(60);
    rig.key(B, false);
    CHECK(fx.cursorRow() == 1 && rig.project().tracks[0].effects[1].kind == static_cast<int>(EffectKind::Filter)); // nor the line it is on now
    rig.tap(UP);
    CHECK(first.kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 0);
    // Any other button pressed while it fills calls it off (X: heard, not deleted).
    rig.key(B, true);
    rig.frames(15);
    rig.tap(X);
    rig.frames(60);
    CHECK(first.kind == static_cast<int>(EffectKind::Tape) && fx.deletePart() == 0.f);
    rig.key(B, false);
    // Held the whole half second: the effect, its settings and its automation go.
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    rig.frames(1);
    CHECK(first.kind == 0 && !session.hasAutomation(0, 2, autoLaneOfEffect(0, 1)));
    // SEL + B brings it back, its settings and its automation with it; SEL + A deletes it again.
    rig.frames(2);
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    rig.frames(2);
    CHECK(first.kind == static_cast<int>(EffectKind::Tape) && near(first.param[1], 0.9f) && session.hasAutomation(0, 2, autoLaneOfEffect(0, 1)));
    rig.key(SELECT, true);
    rig.tap(A);
    rig.key(SELECT, false);
    rig.frames(2);
    CHECK(first.kind == 0 && !session.hasAutomation(0, 2, autoLaneOfEffect(0, 1)));
    // An empty line says nothing of B, and holding it there does nothing.
    draw();
    CHECK(!hintedOn(canvas, scratch, "delete"));
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    CHECK(fx.deletePart() == 0.f && first.kind == 0);
    // On a setting a tap still gives its usual value.
    session.setEffect(0, 0, EffectKind::Delay);
    session.setEffectParam(0, 0, 1, 0.9f);
    fx.goTo(0, 2);
    rig.tap(B);
    CHECK(near(first.param[1], effectInfo(EffectKind::Delay).defaults[1]) && first.kind == static_cast<int>(EffectKind::Delay));
    // A bus's line: the same.
    const EffectData& echo = session.effectData(kEchoBus, 0);
    CHECK(echo.kind != 0);
    fx.goTo(F::kBusRow, 0);
    rig.tap(B);
    rig.frames(100);
    CHECK(echo.kind != 0);
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    CHECK(echo.kind == 0);
    // SENDS: a tap still sends nothing.
    session.setSend(0, 0, 0.5f);
    fx.goTo(F::kSendsRow, 0);
    rig.tap(B);
    CHECK(rig.project().tracks[0].send[0] == 0.f);
}

// HOLD Y on one of the track's own lines: up / down moves it there, the cursor with it; the
// ends stop. Anywhere else Y moves nothing. The bar says HOLD Y where it moves.
bool hintKeyed(const Canvas& canvas, Canvas& scratch, const char* key, const char* words);
void testHoldYMovesAnEffect() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    typedef FxScreen F;
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    const Fonts& fonts = *canvas.fonts;
    session.view = View::Effects;
    session.setEffect(0, 0, EffectKind::Tape);
    session.setEffect(0, 1, EffectKind::Filter);
    const TrackData& track = rig.project().tracks[0];
    const auto draw = [&] {
        canvas.clear();
        fx.draw(canvas.r, fonts);
    };
    fx.goTo(0, 0);
    rig.frames(1);
    draw();
    CHECK(hintKeyed(canvas, scratch, "HOLD Y", "move"));
    rig.key(Y, true);
    rig.frames(1);
    draw();
    CHECK(hintKeyed(canvas, scratch, "UP / DOWN", "move it"));
    rig.tap(DOWN);
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Filter) && track.effects[1].kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 1);
    rig.tap(DOWN);
    CHECK(track.effects[2].kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 2);
    rig.tap(DOWN);
    CHECK(track.effects[2].kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 2); // the end stops
    rig.tap(UP, 2);
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 0);
    rig.tap(UP);
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Tape) && fx.cursorRow() == 0);
    rig.key(Y, false);
    // Y elsewhere moves nothing: the sends' line, a setting, a bus.
    fx.goTo(F::kSendsRow, 0);
    rig.key(Y, true);
    rig.tap(DOWN);
    CHECK(fx.cursorRow() == F::kSendsRow + 1); // (plain cursor moves)
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Tape) && track.effects[1].kind == static_cast<int>(EffectKind::Filter));
    rig.key(Y, false);
    fx.goTo(F::kBusRow, 0);
    const int busKind = session.effectData(kEchoBus, 0).kind;
    rig.key(Y, true);
    rig.tap(UP);
    rig.key(Y, false);
    CHECK(session.effectData(kEchoBus, 0).kind == busKind && fx.cursorRow() == F::kBusRow - 1);
    fx.goTo(0, 1);
    rig.key(Y, true);
    rig.tap(DOWN);
    rig.key(Y, false);
    CHECK(track.effects[0].kind == static_cast<int>(EffectKind::Tape));
    // A held while trying, the screen left and back (the try called off, the hold kept), then Y:
    // directions try again, not move the slot (audit #41).
    fx.goTo(0, 0);
    rig.key(A, true);
    rig.tap(DOWN);
    CHECK(fx.trying());
    session.view = View::Song;
    rig.frames(1);
    session.view = View::Effects;
    rig.frames(1);
    rig.key(Y, true);
    rig.tap(DOWN);
    CHECK(fx.trying() && fx.cursorRow() == 0 && session.effectData(0, 0).kind == static_cast<int>(EffectKind::Tape));
    rig.key(Y, false);
    rig.key(A, false);
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

// Every hint the SONG screen's cell and the drum grid list is drawn, and the help at the end, with
// either help ("MENU: HELP", "HLD SEL: HELP"). A tap of B on a hit deletes it, and the bar says B,
// not HOLD B, as the piano roll does on a note; on an empty step HOLD B (dragged over) on both.
void testTheHintsFitAndSayWhatTheyDo() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    for (int menu = 0; menu < 2; ++menu) {
        Rig rig;
        Session& session = rig.app.session();
        addPhazed(rig);
        rig.app.drums().setStep(0, 0, 1.f);
        const auto draw = [&] {
            rig.frames(1);
            ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP"; // (App sets it each frame from the controls)
            canvas.clear();
            rig.app.draw(canvas.r, fonts);
            return ui::gHintsOver == 0.f;
        };
        // SONG, on a cell: the way to the songs is in it; so on a muted, soloed track.
        session.view = View::Song;
        rig.app.song().moveCursorTo(0, 0);
        CHECK(draw());
        CHECK(hintedOn(canvas, scratch, "songs") && hintKeyed(canvas, scratch, "HOLD B", "delete track") && hintKeyed(canvas, scratch, "B", "empty"));
        session.setMuted(0, true);
        session.setSolo(0, true);
        CHECK(draw());
        session.setMuted(0, false);
        session.setSolo(0, false);
        // ...on a head (mute and solo are said there), on + TRACK, on a repeat count.
        rig.tap(UP);
        CHECK(rig.app.song().cursorOnHead() && draw() && hintedOn(canvas, scratch, "mute") && hintedOn(canvas, scratch, "solo"));
        CHECK(hintKeyed(canvas, scratch, "X", "solo") && hintKeyed(canvas, scratch, "R1", "name") && hintKeyed(canvas, scratch, "A", "instrument"));
        // ...typing its name (R1 on the head): Y deletes, B keeps, with something typed; with nothing, B leaves
        // and START gives the track its instrument's own name (owner, 2026-11-15).
        rig.tap(R1);
        rig.frames(1);
        CHECK(session.view == View::Settings && rig.app.settings().typing());
        rig.tap(A);
        CHECK(draw() && hintKeyed(canvas, scratch, "A", "type") && hintKeyed(canvas, scratch, "Y", "delete") && hintKeyed(canvas, scratch, "B", "keep") && !hintedOn(canvas, scratch, "its own name"));
        rig.tap(Y);
        CHECK(rig.app.settings().typed().empty() && draw() && hintKeyed(canvas, scratch, "B", "leave") && hintKeyed(canvas, scratch, "START", "its own name") && !hintedOn(canvas, scratch, "delete"));
        rig.tap(B);
        CHECK(session.view == View::Song && rig.app.song().cursorOnHead());
        rig.tap(DOWN);
        rig.app.song().moveCursorTo(0, rig.project().trackCount());
        CHECK(draw());
        rig.app.song().moveCursorTo(0, -1);
        CHECK(draw() && hintKeyed(canvas, scratch, "HOLD B", "empty") && !hintKeyed(canvas, scratch, "HOLD B", "delete")); // (it empties the scene: one step of undo)
        // ...on the PLAYS box where the song ends (AT END): what it does there, LOOP or STOP.
        rig.app.song().moveCursorTo(rig.app.song().songEndRow(), -1);
        CHECK(rig.app.song().onSongEnd() && draw() && hintKeyed(canvas, scratch, "A", "song end: loop"));
        rig.tap(A);
        CHECK(draw() && hintKeyed(canvas, scratch, "A", "song end: stop") && !hintKeyed(canvas, scratch, "A", "song end: loop"));
        rig.tap(B);
        CHECK(rig.project().songEnd == kSongLoop);
        // The drum grid, on a hit and on an empty step, on a lane of the kit.
        session.view = View::Pattern;
        rig.project().track = 0;
        rig.app.drums().moveCursorTo(0, 0);
        CHECK(draw());
        CHECK(hintKeyed(canvas, scratch, "B", "delete") && !hintKeyed(canvas, scratch, "HOLD B", "delete") && hintKeyed(canvas, scratch, "HOLD X", "fx"));
        CHECK(hintKeyed(canvas, scratch, "HOLD R1", "paint")); // R1 held on a hit paints at its level, as it did on main
        rig.app.drums().moveCursorTo(0, 1);
        CHECK(draw());
        CHECK(hintKeyed(canvas, scratch, "HOLD B", "delete") && !hintKeyed(canvas, scratch, "B", "delete") && hintedOn(canvas, scratch, "random") && hintedOn(canvas, scratch, "paint"));
        // What they say is what they do: a tap of B on the hit deletes it.
        rig.app.drums().moveCursorTo(0, 0);
        rig.tap(B);
        CHECK(rig.project().tracks[0].drums[0][0].level[0][0] == 0.f);
        // The piano roll says it the same way.
        rig.project().track = 1;
        session.view = View::Pattern;
        rig.app.notes().moveCursorTo(60, 0);
        rig.tap(A);
        draw(); // (the roll on a note does not hold its help yet: found in the pit stop, not one of its items)
        CHECK(hintKeyed(canvas, scratch, "B", "delete") && !hintKeyed(canvas, scratch, "HOLD B", "delete"));
        rig.app.notes().moveCursorTo(60, 8);
        CHECK(draw());
        CHECK(hintKeyed(canvas, scratch, "HOLD B", "delete") && !hintKeyed(canvas, scratch, "B", "delete"));
    }
    ui::gHelpHint = "MENU: HELP";
}

// L1 / R1 on the FX screen: the track before / after, stopping at the first and the last (as the SONG screen's
// columns do). The cursor stays on its line and setting; an effect being tried is called off. The bar says so and fits.
// SWITCHING TYPE FROM THE SETTINGS (owner, 2026-11-14, option A). In a slot's settings RIGHT is the next TYPE of
// its family and LEFT the one before (LEFT on the first setting is the list, as it was); the ends stop and the
// cursor keeps its row. The slot remembers each type's own settings: COMP -> DUCK -> COMP gives the COMP back as it
// was left, to the bit. Forgotten on another family or EMPTY, another song, another instrument, a track deleted.
namespace {
bool sameParams(const EffectData& a, const EffectData& b) {
    if (a.kind != b.kind) return false;
    for (int i = 0; i < kEffectParams; ++i)
        if (a.param[i] != b.param[i]) return false;
    return true;
}
// What the engine runs in a slot, once what was sent has arrived.
int engineKind(Rig& rig, int track, int slot) {
    rig.render(1024);
    return rig.engine->effectKindIn(track, slot);
}
// A COMP in the track's first slot, every setting turned to a value no carry would make.
EffectData turnedComp(Rig& rig) {
    Session& session = rig.app.session();
    session.setEffect(0, 0, EffectKind::Comp);
    const float odd[kEffectParams] = {0.3137f, 0.7771f, 0.1234f, 0.6543f};
    for (int i = 0; i < effectInfo(EffectKind::Comp).params; ++i) session.setEffectParam(0, 0, i, odd[i]);
    return session.effectData(0, 0);
}
} // namespace

// The slot's TYPE switched from its settings: A held, LEFT or RIGHT (owner, 2026-11-14: one grip turns the
// value and switches the type; plain LEFT is only ever the list).
void typeTap(Rig& rig, SDL_Keycode way, int times = 1) {
    rig.key(A, true);
    rig.tap(way, times);
    rig.key(A, false);
}

void testFxSwitchesType() {
    const int comp = static_cast<int>(EffectKind::Comp), duck = static_cast<int>(EffectKind::Duck);
    const int levels = effectTypes(EffectKind::Comp);
    CHECK(effectTypeAt(EffectKind::Comp, 0) == EffectKind::Comp && effectTypeAt(EffectKind::Comp, 1) == EffectKind::Duck && levels >= 3);
    CHECK(effectInfo(EffectKind::Comp).params >= 2 && effectInfo(EffectKind::Duck).params >= 2);
    {
        // The gesture both ways, the memory, the engine's slot after each step.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        const EffectData compWas = turnedComp(rig);
        rig.frames(1);
        fx.goTo(0, 2);
        typeTap(rig, RIGHT);
        const EffectData& slot = session.effectData(0, 0);
        float carried[kEffectParams];
        effectCarry(EffectKind::Comp, EffectKind::Duck, compWas.param, false, carried);
        CHECK(slot.kind == duck && fx.cursorRow() == 0 && fx.cursorColumn() == 2); // the row stays
        for (int i = 0; i < kEffectParams; ++i) CHECK(slot.param[i] == carried[i]); // never DUCKed here: carried, as changeEffectType
        CHECK(engineKind(rig, 0, 0) == duck);
        session.setEffectParam(0, 0, 1, 0.8642f); // the DUCK turned
        const EffectData duckWas = slot;
        typeTap(rig, LEFT);
        CHECK(sameParams(slot, compWas) && fx.cursorColumn() == 2); // the COMP as it was left, to the bit
        CHECK(engineKind(rig, 0, 0) == comp);
        typeTap(rig, RIGHT);
        CHECK(sameParams(slot, duckWas)); // and the DUCK as it was left
        CHECK(engineKind(rig, 0, 0) == duck);
        // The ends stop: LEFT on the first type, RIGHT on the last.
        typeTap(rig, LEFT);
        typeTap(rig, LEFT);
        CHECK(sameParams(slot, compWas) && fx.cursorColumn() == 2);
        typeTap(rig, RIGHT, levels + 3);
        const EffectKind lastType = effectTypeAt(EffectKind::Comp, levels - 1);
        CHECK(slot.kind == static_cast<int>(lastType) && engineKind(rig, 0, 0) == static_cast<int>(lastType));
        typeTap(rig, LEFT, levels - 1);
        CHECK(sameParams(slot, compWas));
        // The first setting is as any other: A held, LEFT / RIGHT switch, the cursor kept.
        fx.goTo(0, 1);
        typeTap(rig, RIGHT);
        CHECK(fx.cursorColumn() == 1 && sameParams(slot, duckWas));
        typeTap(rig, LEFT);
        CHECK(fx.cursorColumn() == 1 && sameParams(slot, compWas));
        // WITHOUT A: LEFT is the list from any setting, the slot untouched; RIGHT there does nothing at all,
        // tapped or held (owner, 2026-11-14: LEFT doing two jobs was awkward).
        for (int column : {1, 2, 3}) {
            fx.goTo(0, column);
            rig.tap(RIGHT, 3);
            rig.key(RIGHT, true);
            rig.frames(40);
            rig.key(RIGHT, false);
            CHECK(fx.cursorColumn() == column && sameParams(slot, compWas));
            rig.tap(LEFT);
            CHECK(fx.cursorColumn() == 0 && sameParams(slot, compWas));
        }
        // ...nor does the stick switch, with A up (it is the list, or nothing) or with A held (it turns values).
        fx.goTo(0, 2);
        rig.key(STICK_RIGHT, true);
        rig.frames(20);
        rig.key(STICK_RIGHT, false);
        rig.frames(1);
        CHECK(slot.kind == comp && fx.cursorColumn() == 2);
        rig.key(A, true);
        rig.key(STICK_RIGHT, true);
        rig.frames(20);
        rig.key(STICK_RIGHT, false);
        rig.frames(1);
        rig.key(A, false);
        CHECK(sameParams(slot, compWas) && fx.cursorColumn() == 2);
        // A and the direction held: at the pace of a list (a TYPE is a choice), not a cursor's. A third of a
        // second is the press and one repeat, not the family's end.
        rig.key(A, true);
        rig.key(RIGHT, true);
        rig.frames(14);
        rig.key(RIGHT, false);
        CHECK(slot.kind != comp && slot.kind != static_cast<int>(lastType));
        rig.tap(LEFT, levels);
        rig.key(A, false);
        CHECK(sameParams(slot, compWas));
        // In one grip: A held, UP turns the value, RIGHT switches, LEFT comes back to the COMP as just turned.
        fx.goTo(0, 2);
        rig.key(A, true);
        rig.tap(UP);
        const EffectData turned = slot;
        CHECK(turned.kind == comp && !sameParams(turned, compWas));
        rig.tap(RIGHT);
        CHECK(slot.kind == duck);
        rig.tap(LEFT);
        CHECK(sameParams(slot, turned) && fx.cursorColumn() == 2);
        rig.tap(DOWN);
        rig.key(A, false);
        // A type with fewer settings: the cursor is pulled in to its last. (Any family's pair that has one.)
        for (int place = 1; place + 1 < effectListSize(); ++place) {
            const EffectKind k = effectAt(place), next = effectTypeAt(k, effectTypeIndex(k) + 1);
            if (next == EffectKind::None || effectInfo(next).params >= effectInfo(k).params) continue;
            session.setEffect(0, 0, EffectKind::None);
            session.setEffect(0, 0, k);
            fx.goTo(0, effectInfo(k).params);
            typeTap(rig, RIGHT);
            CHECK(session.effectData(0, 0).kind == static_cast<int>(next) && fx.cursorColumn() == effectInfo(next).params);
            break;
        }
        // The sends switch nothing: LEFT is the list from either, RIGHT does nothing.
        fx.goTo(FxScreen::kSendsRow, 2);
        typeTap(rig, RIGHT);
        typeTap(rig, LEFT);
        rig.tap(RIGHT);
        CHECK(fx.cursorColumn() == 2 && fx.cursorRow() == FxScreen::kSendsRow);
        rig.tap(LEFT);
        CHECK(fx.cursorColumn() == 0 && fx.cursorRow() == FxScreen::kSendsRow);
        // An empty slot has no settings to be in.
        session.setEffect(0, 1, EffectKind::None);
        fx.goTo(1, 2);
        typeTap(rig, RIGHT);
        CHECK(fx.cursorColumn() == 0 && session.effectData(0, 1).kind == 0);
    }
    {
        // Forgotten: another family, EMPTY, another song, another instrument; a deleted track takes its own.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        const auto switchedAway = [&] { // a turned COMP to the DUCK: the COMP is remembered
            turnedComp(rig);
            fx.goTo(0, 2);
            typeTap(rig, RIGHT);
            float back[kEffectParams];
            return session.effectData(0, 0).kind == duck && session.rememberedType(0, 0, EffectKind::Comp, back);
        };
        float back[kEffectParams];
        CHECK(switchedAway());
        session.setEffect(0, 0, EffectKind::Limiter); // another type put in by hand: the same family, still known (the DUCK left so is not a switch)
        CHECK(session.rememberedType(0, 0, EffectKind::Comp, back) && !session.rememberedType(0, 0, EffectKind::Duck, back));
        session.setEffect(0, 0, EffectKind::Filter); // another family
        CHECK(!session.rememberedType(0, 0, EffectKind::Comp, back) && !session.rememberedType(0, 0, EffectKind::Duck, back));
        session.setEffect(0, 0, EffectKind::Duck);
        fx.goTo(0, 2);
        typeTap(rig, LEFT); // the COMP comes from the DUCK's usual settings, carried: not the one turned before
        float carried[kEffectParams], usual[kEffectParams];
        for (int i = 0; i < kEffectParams; ++i) usual[i] = effectDefault(EffectKind::Duck, i, false);
        effectCarry(EffectKind::Duck, EffectKind::Comp, usual, false, carried);
        for (int i = 0; i < kEffectParams; ++i) CHECK(session.effectData(0, 0).param[i] == carried[i]);
        CHECK(switchedAway());
        fx.goTo(0, 0);
        holdB(rig); // EMPTY
        CHECK(session.effectData(0, 0).kind == 0 && !session.rememberedType(0, 0, EffectKind::Comp, back));
        CHECK(switchedAway());
        session.applyProject(); // another song
        CHECK(!session.rememberedType(0, 0, EffectKind::Comp, back));
        // The master's and the buses' slots remember apart from the tracks', and are forgotten the same way.
        session.rememberType(kMasterTrack, 1, session.effectData(0, 0));
        session.rememberType(kSpaceBus, 0, session.effectData(0, 0));
        CHECK(session.rememberedType(kMasterTrack, 1, EffectKind::Duck, back) && !session.rememberedType(kMasterTrack, 0, EffectKind::Duck, back));
        CHECK(session.rememberedType(kSpaceBus, 0, EffectKind::Duck, back) && !session.rememberedType(kEchoBus, 0, EffectKind::Duck, back) && !session.rememberedType(0, 0, EffectKind::Duck, back));
        EffectData hall, tape; // a bus's own, and neither of the master's two
        hall.kind = static_cast<int>(EffectKind::Hall), tape.kind = static_cast<int>(EffectKind::Tape);
        session.rememberType(kSpaceBus, 0, hall);
        session.rememberType(kEchoBus, 0, tape);
        CHECK(session.rememberedType(kSpaceBus, 0, EffectKind::Hall, back) && session.rememberedType(kEchoBus, 0, EffectKind::Tape, back));
        CHECK(!session.rememberedType(kMasterTrack, 0, EffectKind::Hall, back) && !session.rememberedType(kMasterTrack, 1, EffectKind::Hall, back));
        CHECK(!session.rememberedType(kMasterTrack, 0, EffectKind::Tape, back) && !session.rememberedType(kMasterTrack, 1, EffectKind::Tape, back));
        CHECK(!session.rememberedType(kEchoBus, 0, EffectKind::Hall, back) && !session.rememberedType(kSpaceBus, 0, EffectKind::Tape, back));
        session.applyProject();
        CHECK(!session.rememberedType(kMasterTrack, 1, EffectKind::Duck, back) && !session.rememberedType(kSpaceBus, 0, EffectKind::Duck, back));
        // Tracks: another instrument forgets; a deleted track's go with it, and the ones after move up.
        CHECK(session.addTrack(TrackKind::Drums) && session.addTrack(TrackKind::Drums)); // tracks 1 and 2
        EffectData a, b;
        a.kind = comp, a.param[0] = 0.11f;
        b.kind = comp, b.param[0] = 0.22f;
        session.rememberType(1, 2, a);
        session.rememberType(2, 2, b);
        CHECK(session.removeTrack(1));
        CHECK(session.rememberedType(1, 2, EffectKind::Comp, back) && back[0] == 0.22f && !session.rememberedType(2, 2, EffectKind::Comp, back));
        session.rememberType(0, 1, a);
        CHECK(session.setTrackKind(0, TrackKind::Phazed));
        CHECK(!session.rememberedType(0, 1, EffectKind::Comp, back) && session.rememberedType(1, 2, EffectKind::Comp, back));
        session.rememberType(2, 2, a); // (no track there: nothing in the program does this, but a new track must not find it)
        CHECK(session.addTrack(TrackKind::Drums)); // a new track starts with nothing remembered
        CHECK(!session.rememberedType(2, 2, EffectKind::Comp, back));
    }
    {
        // The buses and the master: the same gesture, the same memory, the engine's slot after each step.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        const EffectKind next = effectTypeAt(EffectKind::Tape, effectTypeIndex(EffectKind::Tape) + 1);
        CHECK(session.effectData(kEchoBus, 0).kind == static_cast<int>(EffectKind::Tape) && next != EffectKind::None);
        session.setEffectParam(kEchoBus, 0, 1, 0.5432f);
        session.setEffectParam(kEchoBus, 0, 3, 0.6f); // its LEVEL
        const EffectData tapeWas = session.effectData(kEchoBus, 0);
        fx.goTo(FxScreen::kBusRow, 2);
        typeTap(rig, RIGHT);
        CHECK(session.effectData(kEchoBus, 0).kind == static_cast<int>(next) && engineKind(rig, kEchoBus, 0) == static_cast<int>(next));
        typeTap(rig, LEFT);
        CHECK(sameParams(session.effectData(kEchoBus, 0), tapeWas) && engineKind(rig, kEchoBus, 0) == static_cast<int>(EffectKind::Tape));
        session.setEffect(kMasterTrack, 1, EffectKind::Comp);
        session.setEffectParam(kMasterTrack, 1, 0, 0.2468f);
        const EffectData masterWas = session.effectData(kMasterTrack, 1);
        fx.goTo(FxScreen::kMasterRow + 1, 2);
        typeTap(rig, RIGHT);
        CHECK(session.effectData(kMasterTrack, 1).kind == duck && engineKind(rig, kMasterTrack, 1) == duck);
        CHECK(session.effectData(0, 0).kind == 0); // (the track's own slot untouched)
        typeTap(rig, LEFT);
        CHECK(sameParams(session.effectData(kMasterTrack, 1), masterWas) && engineKind(rig, kMasterTrack, 1) == comp);
    }
    {
        // Undo: a switch is a step, and undo puts back the type it left with its settings; the engine follows.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        const EffectData compWas = turnedComp(rig);
        rig.frames(60);
        const int steps = session.undoSteps();
        fx.goTo(0, 2);
        typeTap(rig, RIGHT);
        rig.frames(60);
        CHECK(session.undoSteps() == steps + 1 && session.effectData(0, 0).kind == duck);
        CHECK(session.undo() && sameParams(session.effectData(0, 0), compWas) && engineKind(rig, 0, 0) == comp);
        CHECK(session.redo() && session.effectData(0, 0).kind == duck && engineKind(rig, 0, 0) == duck);
    }
    {
        // Automation: what changeEffectType does with it, both ways, whatever is remembered; WRITE writes nothing.
        Rig rig, second;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        for (Rig* r : {&rig, &second}) {
            turnedComp(*r);
            for (int p = 0; p < kEffectParams; ++p) r->app.session().setAutomation(0, p % 3, autoLaneOfEffect(0, p), 2 + p, 0.1f + 0.2f * static_cast<float>(p));
            r->frames(1);
        }
        session.setWriting(true);
        rig.tap(START);
        rig.render(kStep * 2);
        rig.frames(1);
        CHECK(rig.project().playing);
        fx.goTo(0, 2);
        typeTap(rig, RIGHT);
        CHECK(changeEffectType(second.app.session(), 0, 0, EffectKind::Duck));
        CHECK(same(tunedOf(rig, 0, 0), tunedOf(second, 0, 0)));
        typeTap(rig, LEFT); // the COMP's settings are remembered; its lanes are those the DUCK's moved back
        CHECK(changeEffectType(second.app.session(), 0, 0, EffectKind::Comp));
        CHECK(tunedOf(rig, 0, 0).lanes == tunedOf(second, 0, 0).lanes && session.writing());
    }
    {
        // The heading's TYPE row shows the key where it switches (RIGHT on the first setting, LEFT / RIGHT below
        // it), not in the list with A up nor while A turns a setting; the bar says it, and still fits.
        Canvas canvas, scratch;
        CHECK(canvas.ok && scratch.ok);
        if (canvas.ok && scratch.ok) {
            using F = FxScreen;
            const Fonts& fonts = *canvas.fonts;
            Rig rig;
            Session& session = rig.app.session();
            FxScreen& fx = rig.app.fx();
            session.view = View::Effects;
            turnedComp(rig);
            const auto draw = [&] {
                rig.frames(1);
                canvas.clear();
                rig.app.draw(canvas.r, fonts);
                return ui::gHintsOver == 0.f;
            };
            const int keyX = static_cast<int>(F::kPaneX + F::kPaneW - 12.f - ui::keyWidth(fonts, "LEFT / RIGHT"));
            const auto keyInk = [&] { // pixels that are not the heading's panel, where the key stands
                int n = 0;
                for (int y = F::kTypeRowY - 4; y < F::kTypeRowY + fonts.small.capHeight() + 4; ++y)
                    for (int x = keyX; x < F::kPaneX + F::kPaneW - 12; ++x) n += canvas.is(x, y, ui::kPanel) ? 0 : 1;
                return n;
            };
            // In the list, and on a setting with A up: no key in the heading; the bar says LEFT is the list and
            // that HOLD A turns and switches.
            fx.goTo(0, 0);
            CHECK(draw() && keyInk() == 0 && fx.typeKey() == nullptr);
            for (int column : {1, 2}) {
                fx.goTo(0, column);
                CHECK(draw() && keyInk() == 0 && fx.typeKey() == nullptr);
                CHECK(hintKeyed(canvas, scratch, "LEFT", "list") && hintKeyed(canvas, scratch, "HOLD A", "turn, type") && !hintKeyed(canvas, scratch, "RIGHT", "type"));
            }
            // A held on a setting: the key that switches. The family's FIRST type: RIGHT alone, on any setting.
            rig.key(A, true);
            CHECK(draw() && keyInk() > 0 && fx.typeKey() && std::strcmp(fx.typeKey(), "RIGHT") == 0);
            CHECK(hintKeyed(canvas, scratch, "RIGHT", "type") && hintKeyed(canvas, scratch, "UP / DOWN", "turn it") && !hintKeyed(canvas, scratch, "LEFT / RIGHT", "type"));
            const int right = keyInk();
            rig.tap(RIGHT); // the DUCK, a type in the middle: both ways
            CHECK(draw() && keyInk() > right && fx.typeKey() && std::strcmp(fx.typeKey(), "LEFT / RIGHT") == 0 && hintKeyed(canvas, scratch, "LEFT / RIGHT", "type"));
            rig.tap(RIGHT, levels); // the family's LAST type: LEFT alone (the two arrows are one width and one ink: typeKey says which)
            CHECK(draw() && keyInk() == right && fx.typeKey() && std::strcmp(fx.typeKey(), "LEFT") == 0 && hintKeyed(canvas, scratch, "LEFT", "type") && !hintKeyed(canvas, scratch, "LEFT / RIGHT", "type"));
            rig.key(A, false);
            CHECK(draw() && keyInk() == 0 && fx.typeKey() == nullptr && hintKeyed(canvas, scratch, "LEFT", "list"));
            // SENDS have no types: no key held or not, and HOLD A only turns.
            fx.goTo(F::kSendsRow, 2);
            CHECK(draw() && hintKeyed(canvas, scratch, "LEFT", "list") && hintKeyed(canvas, scratch, "HOLD A", "turn") && !hintKeyed(canvas, scratch, "HOLD A", "turn, type"));
            rig.key(A, true);
            CHECK(draw() && fx.typeKey() == nullptr && !hintKeyed(canvas, scratch, "RIGHT", "type"));
            rig.key(A, false);
        }
    }
    {
        // With A held in the list, LEFT / RIGHT try a remembered type as it was left, and that is what goes in.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        const EffectData compWas = turnedComp(rig);
        fx.goTo(0, 2);
        typeTap(rig, RIGHT);
        session.setEffectParam(0, 0, 1, 0.8642f);
        const EffectData duckWas = session.effectData(0, 0);
        fx.goTo(0, 0);
        rig.key(A, true);
        rig.tap(LEFT);
        CHECK(fx.trying() && sameParams(fx.tried(), compWas) && session.effectData(0, 0).kind == duck);
        CHECK(engineKind(rig, 0, 0) == comp);
        rig.key(A, false);
        rig.frames(1);
        CHECK(sameParams(session.effectData(0, 0), compWas)); // what was heard went in
        rig.key(A, true);
        rig.tap(RIGHT, 2); // past the DUCK to the third: let go there, the COMP is remembered as it is
        rig.key(A, false);
        rig.frames(1);
        CHECK(session.effectData(0, 0).kind == static_cast<int>(effectTypeAt(EffectKind::Comp, 2)));
        fx.goTo(0, 2);
        typeTap(rig, LEFT);
        CHECK(sameParams(session.effectData(0, 0), duckWas)); // the DUCK, as it was left two switches ago
        typeTap(rig, LEFT);
        CHECK(sameParams(session.effectData(0, 0), compWas));
    }
}

// What the review of the TYPE switch found (2026-11-14).
void testFxSwitchMends() {
    const int comp = static_cast<int>(EffectKind::Comp), duck = static_cast<int>(EffectKind::Duck);
    const int levels = effectTypes(EffectKind::Comp);
    {
        // RIGHT held on from the list goes into the settings and switches NOTHING, however long it is held (the
        // D-pad's repeat and the stick alike): a slightly long press once made a COMP a DUCK, a LIMITER, a SNAP.
        // Since the switch is A's (owner, 2026-11-14) a plain RIGHT never switches at all.
        for (const SDL_Keycode right : {RIGHT, STICK_RIGHT}) {
            Rig rig;
            Session& session = rig.app.session();
            FxScreen& fx = rig.app.fx();
            session.view = View::Effects;
            const EffectData compWas = turnedComp(rig);
            rig.frames(1);
            for (const int held : {8, 12, 20, 40, 120}) { // 128 ms to two seconds
                fx.goTo(0, 0);
                rig.key(right, true);
                rig.frames(held);
                CHECK(fx.cursorColumn() >= 1 && sameParams(session.effectData(0, 0), compWas));
                rig.key(right, false);
                rig.frames(1);
                rig.key(right, true); // and a push of its own, there: nothing either
                rig.frames(20);
                rig.key(right, false);
                rig.frames(1);
                CHECK(sameParams(session.effectData(0, 0), compWas) && engineKind(rig, 0, 0) == comp);
            }
        }
    }
    {
        // A track's three slots each remember apart, and one track's are not another's.
        Rig rig;
        Session& session = rig.app.session();
        CHECK(session.addTrack(TrackKind::Drums));
        EffectData a;
        a.kind = comp, a.param[0] = 0.11f;
        float back[kEffectParams];
        for (int track = 0; track < 2; ++track)
            for (int slot = 0; slot < kTrackSlots; ++slot) {
                session.rememberType(track, slot, a);
                for (int t = 0; t < 2; ++t)
                    for (int s = 0; s < kTrackSlots; ++s) CHECK(session.rememberedType(t, s, EffectKind::Comp, back) == (t == track && s == slot));
                session.forgetTypes(track, slot);
                CHECK(!session.rememberedType(track, slot, EffectKind::Comp, back));
            }
        // Through the gesture too: a COMP switched away in slot 1 is not what slot 2's DUCK goes back to.
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        rig.project().track = 0;
        const EffectData compWas = turnedComp(rig);
        fx.goTo(0, 2);
        typeTap(rig, RIGHT);
        session.setEffect(0, 1, EffectKind::Duck);
        fx.goTo(1, 2);
        typeTap(rig, LEFT);
        CHECK(session.effectData(0, 1).kind == comp && !sameParams(session.effectData(0, 1), compWas));
    }
    {
        // A switch is heard when the song is stopped (the track is sounded through the new type), a switch that
        // an end stops is not, and nothing is sounded by hand while the song plays.
        Rig rig;
        Session& session = rig.app.session();
        FxScreen& fx = rig.app.fx();
        session.view = View::Effects;
        session.setEffect(0, 0, EffectKind::Comp);
        rig.frames(30);
        rig.render(96000);
        fx.goTo(0, 2);
        CHECK(rig.listen(8000) == 0.f);
        typeTap(rig, RIGHT);
        CHECK(session.effectData(0, 0).kind == duck && rig.listen(8000) > 0.01f);
        rig.frames(30);
        rig.render(96000);
        typeTap(rig, LEFT);
        CHECK(session.effectData(0, 0).kind == comp && rig.listen(8000) > 0.01f);
        rig.frames(30);
        rig.render(96000);
        typeTap(rig, LEFT); // the first type: stopped, nothing switched, nothing sounded
        CHECK(session.effectData(0, 0).kind == comp && rig.listen(8000) == 0.f);
        // Playing (an empty pattern, so anything heard would be a hit by hand).
        Rig playing;
        Session& ps = playing.app.session();
        ps.view = View::Effects;
        ps.setEffect(0, 0, EffectKind::Comp);
        playing.frames(1);
        playing.tap(START);
        playing.frames(1);
        CHECK(playing.project().playing && playing.listen(48000) == 0.f);
        playing.app.fx().goTo(0, 2);
        typeTap(playing, RIGHT);
        playing.frames(1);
        CHECK(ps.effectData(0, 0).kind == duck && playing.listen(48000) == 0.f);
    }
    {
        // A family of one (none today) switches nothing and shows no key. Asked with numbers, since no family
        // can be put in a slot to try it. The first setting is as any other.
        using F = FxScreen;
        CHECK(F::typeKeyFor(1, 1, 0) == nullptr && F::typeKeyFor(2, 1, 0) == nullptr && F::typeKeyFor(4, 1, 0) == nullptr);
        CHECK(F::typeKeyFor(0, 4, 1) == nullptr); // the list
        CHECK(std::strcmp(F::typeKeyFor(1, 2, 0), "RIGHT") == 0 && std::strcmp(F::typeKeyFor(1, 2, 1), "LEFT") == 0);
        CHECK(std::strcmp(F::typeKeyFor(2, 2, 0), "RIGHT") == 0 && std::strcmp(F::typeKeyFor(2, 2, 1), "LEFT") == 0);
        CHECK(std::strcmp(F::typeKeyFor(1, 3, 1), "LEFT / RIGHT") == 0 && std::strcmp(F::typeKeyFor(3, 3, 1), "LEFT / RIGHT") == 0);
        // Below the first setting only a family of one has no key: every real type there has one.
        for (int place = 1; place < effectListSize(); ++place) {
            const EffectKind k = effectAt(place);
            CHECK(effectTypes(k) >= 2 && F::typeKeyFor(2, effectTypes(k), effectTypeIndex(k)) != nullptr);
        }
    }
}

void testFxStepsTheTrack() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    addPhazed(rig);
    addPhazed(rig);
    CHECK(rig.project().trackCount() == 3);
    session.setEffect(0, 1, EffectKind::Filter);
    session.setEffect(1, 1, EffectKind::Tape);
    session.setEffect(2, 1, EffectKind::Comp);
    session.view = View::Effects;
    rig.project().track = 0;
    fx.goTo(1, 3);
    rig.frames(1);
    rig.tap(L1);
    CHECK(rig.project().track == 0 && fx.cursorRow() == 1 && fx.cursorColumn() == 3); // the first: it stops
    rig.tap(R1);
    rig.frames(1);
    CHECK(rig.project().track == 1 && fx.cursorRow() == 1 && fx.cursorColumn() == 3 && session.view == View::Effects);
    rig.tap(R1);
    rig.frames(1);
    CHECK(rig.project().track == 2 && fx.cursorRow() == 1 && fx.cursorColumn() == 3);
    rig.tap(R1);
    rig.frames(1);
    CHECK(rig.project().track == 2); // the last: it stops
    // A line with fewer settings on the next track: the nearest; the one last used is not forgotten.
    session.setEffect(1, 1, EffectKind::Crush);
    fx.goTo(1, 4);
    rig.tap(L1);
    rig.frames(1);
    CHECK(rig.project().track == 1 && fx.cursorColumn() == effectInfo(EffectKind::Crush).params && fx.remembered(1) == 4);
    // The sends and the buses: the same line.
    fx.goTo(FxScreen::kSendsRow, 2);
    rig.tap(L1);
    CHECK(rig.project().track == 0 && fx.cursorRow() == FxScreen::kSendsRow && fx.cursorColumn() == 2);
    // Trying an effect: called off, the old track's slot as it was.
    const EffectData was = session.effectData(0, 1);
    fx.goTo(1, 0);
    rig.key(A, true);
    rig.tap(DOWN);
    CHECK(fx.trying());
    rig.tap(R1);
    CHECK(!fx.trying() && !fx.picking() && rig.project().track == 1 && session.effectData(0, 1).kind == was.kind && session.effectData(1, 1).kind == static_cast<int>(EffectKind::Crush));
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.effectData(0, 1).kind == was.kind && session.effectData(1, 1).kind == static_cast<int>(EffectKind::Crush));
    // The top bar and the list's heading name the new track at once.
    char name[24];
    ui::trackName(rig.project(), 1, name, sizeof name);
    canvas.clear();
    rig.app.draw(canvas.r, fonts);
    CHECK(inked(canvas, scratch, 0, FxScreen::kListX + 1.f, FxScreen::lineY(0) - 19 + 3.f, name, ui::kText));
    // The bar says it, and fits, in every state of the screen, with either help.
    for (int menu = 0; menu < 2; ++menu) {
        const auto draw = [&] {
            rig.frames(1);
            ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
            canvas.clear();
            rig.app.draw(canvas.r, fonts);
            return ui::gHintsOver == 0.f;
        };
        for (int row = 0; row < FxScreen::kRows; ++row)
            for (int column = 0; column < 3; ++column) {
                fx.goTo(row, column);
                CHECK(draw() && hintKeyed(canvas, scratch, "L1 / R1", "track"));
                if (!draw()) std::printf("    row %d column %d: %.0f pixels over\n", row, column, static_cast<double>(ui::gHintsOver));
                rig.key(A, true);
                CHECK(draw());
                rig.key(A, false);
            }
        fx.goTo(1, 0);
        rig.key(A, true);
        rig.tap(DOWN);
        CHECK(draw() && hintKeyed(canvas, scratch, "B", "as it was"));
        rig.key(A, false);
    }
    ui::gHelpHint = "MENU: HELP";
    // The four shoulders for a panic are seldom down at once: L1 or R1 first steps the track on the way, and the
    // panic puts it back, as it puts back the tab the triggers turned.
    for (int first = 0; first < 2; ++first) {
        session.view = View::Effects;
        rig.project().track = 1;
        fx.goTo(1, 0);
        rig.frames(1);
        const SDL_Keycode order[2][4] = {{R1, L2, R2, L1}, {L2, L1, R2, R1}};
        for (SDL_Keycode k : order[first]) rig.key(k, true), rig.frames(1);
        CHECK(rig.app.panicNoteShowing() && rig.project().track == 1 && session.view == View::Effects);
        for (SDL_Keycode k : order[first]) rig.key(k, false);
        rig.frames(1);
        CHECK(rig.project().track == 1 && session.view == View::Effects);
        if (rig.project().track != 1) std::printf("    a panic left track %d\n", rig.project().track + 1);
    }
}

// L1 / R1 with A held on a setting (gesture audit G44): A is kept where the new track's slot holds the same effect, so
// one dial is turned across the tracks in one grip. Another effect there (even another type with a FEEDBACK of its
// own), or none, lets A go: nothing is turned there, and on an empty slot a push tries no effect. The sends are the
// same two on every track. At the first or the last track nothing changes, and A stays held.
void testFxHoldKeptAcrossTracks() {
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    addPhazed(rig);
    addPhazed(rig);
    addPhazed(rig);
    CHECK(rig.project().trackCount() == 4);
    session.setEffect(0, 1, EffectKind::Tape);
    session.setEffect(1, 1, EffectKind::Tape);
    session.setEffect(2, 1, EffectKind::Delay); // CLEAN: the same family, a FEEDBACK in the same place
    session.setEffect(3, 1, EffectKind::Tape);
    session.view = View::Effects;
    rig.project().track = 0;
    fx.goTo(1, 2); // TAPE's FEEDBACK
    rig.frames(1);
    const auto feedback = [&](int track) { return session.effectData(track, 1).param[1]; };
    const float usual = effectDefault(EffectKind::Tape, 1);
    rig.key(A, true);
    rig.tap(L1); // the first: nothing changes, A is still held
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 0 && fx.cursorColumn() == 2 && feedback(0) > usual);
    rig.tap(R1);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 1 && fx.cursorColumn() == 2 && feedback(1) > usual);
    // The stick too, in the same grip.
    const float pushed = feedback(1);
    rig.key(STICK_UP, true);
    rig.frames(10);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(feedback(1) > pushed && fx.cursorColumn() == 2);
    // Another type on the next track: A is let go. UP moves the cursor; CLEAN's FEEDBACK is not turned.
    const EffectData clean = session.effectData(2, 1);
    rig.tap(R1);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 2 && fx.cursorColumn() == 1 && session.effectData(2, 1).param[1] == clean.param[1] && session.effectData(2, 1).param[0] == clean.param[0]);
    // ...and it stays let go, back on a TAPE, until A is pressed again.
    fx.goTo(1, 2);
    rig.tap(R1);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 3 && fx.cursorColumn() == 1 && feedback(3) == usual);
    rig.key(A, false);
    // An empty slot on the next track: A is let go, the cursor comes back to the list, and DOWN tries nothing.
    session.setEffect(2, 1, EffectKind::None);
    fx.goTo(1, 2);
    rig.frames(1);
    rig.key(A, true);
    rig.tap(L1);
    rig.frames(1);
    CHECK(rig.project().track == 2 && fx.cursorColumn() == 0 && !fx.picking() && !fx.trying());
    rig.tap(DOWN);
    rig.frames(1);
    CHECK(!fx.trying() && fx.cursorRow() == 2);
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.effectData(2, 1).kind == static_cast<int>(EffectKind::None) && session.effectData(2, 2).kind == static_cast<int>(EffectKind::None));
    // The sends: the same two on every track, so A is kept. At the last track nothing changes either.
    rig.project().track = 2;
    fx.goTo(FxScreen::kSendsRow, 1);
    rig.frames(1);
    rig.key(A, true);
    rig.tap(R1);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 3 && rig.project().tracks[3].send[0] > 0.f);
    const float sent = rig.project().tracks[3].send[0];
    rig.tap(R1);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.project().track == 3 && rig.project().tracks[3].send[0] > sent && fx.cursorColumn() == 1);
    rig.key(A, false);
    rig.frames(1);
}

// BAND's FREQ (gesture audit G19): a D-pad push is a thirtieth of the range, a third of an octave, so 50 Hz is a
// push and the start (1.0 KHZ) is one: the D-pad comes back to it to the bit, and B's usual value is the same number.
// GAIN beside it, and FILTER's CUTOFF, still move a twentieth (effects_test holds every other row to twenty).
void testBandFreqStepsAThirdOfAnOctave() {
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    session.setEffect(0, 1, EffectKind::Band);
    session.setEffect(0, 2, EffectKind::Filter);
    session.view = View::Effects;
    rig.project().track = 0;
    fx.goTo(1, 1);
    rig.frames(1);
    const auto freq = [&] { return session.effectData(0, 1).param[0]; };
    const auto reads = [&](float v) {
        char text[32];
        effectParamText(EffectKind::Band, 0, v, text, sizeof text);
        return std::string(text);
    };
    const float start = freq();
    CHECK(start == 17.f / 30.f && start == effectDefault(EffectKind::Band, 0) && reads(start) == "1.0 KHZ");
    rig.key(A, true);
    rig.tap(DOWN);
    CHECK(freq() == 16.f / 30.f);
    rig.tap(DOWN, 12);
    CHECK(freq() == 4.f / 30.f && reads(freq()) == "50 HZ");
    rig.tap(UP);
    CHECK(reads(freq()) == "63 HZ");
    rig.tap(UP, 12);
    CHECK(freq() == start); // back on the start, to the bit
    rig.tap(UP);
    CHECK(freq() == 18.f / 30.f);
    // From between two points (the stick put it there), a push goes to the next one.
    session.setEffectParam(0, 1, 0, 0.51f);
    rig.tap(UP);
    CHECK(freq() == 16.f / 30.f);
    session.setEffectParam(0, 1, 0, 0.51f);
    rig.tap(DOWN);
    CHECK(freq() == 15.f / 30.f);
    rig.tap(DOWN, 40);
    CHECK(freq() == 0.f);
    rig.tap(UP, 40);
    CHECK(freq() == 1.f);
    rig.key(A, false);
    rig.tap(B);
    CHECK(freq() == start);
    // GAIN: a twentieth, 1.2 dB.
    rig.tap(DOWN);
    CHECK(fx.cursorColumn() == 2);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(session.effectData(0, 1).param[1] == 11.f * 0.05f);
    rig.key(A, false);
    // FILTER's CUTOFF: a twentieth, as it always was.
    session.setEffectParam(0, 2, 0, 0.5f);
    fx.goTo(2, 1);
    rig.frames(1);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(session.effectData(0, 2).param[0] == 11.f * 0.05f);
    rig.tap(DOWN, 2);
    CHECK(session.effectData(0, 2).param[0] == 9.f * 0.05f);
    rig.key(A, false);
}

// Y held on BAND's FREQ, GAIN or Q moves the bell two ways at once (gesture audit G18): LEFT / RIGHT is FREQ, UP /
// DOWN GAIN (on the Q dial, Q), the D-pad a push as A gives, the stick both at once; the cursor stays. Not on
// SHAPE, not on another effect, and with A held A's jobs stand (LEFT / RIGHT the TYPE). WRITE records both.
void testHoldYMovesABandTwoWays() {
    Rig rig;
    Session& session = rig.app.session();
    FxScreen& fx = rig.app.fx();
    session.setEffect(0, 0, EffectKind::Band);
    session.setEffect(0, 1, EffectKind::Filter);
    session.view = View::Effects;
    rig.project().track = 0;
    CHECK(std::string(effectInfo(EffectKind::Band).paramNames[FxScreen::kBandFreq]) == "FREQ" &&
          std::string(effectInfo(EffectKind::Band).paramNames[FxScreen::kBandGain]) == "GAIN" &&
          std::string(effectInfo(EffectKind::Band).paramNames[FxScreen::kBandQ]) == "Q");
    const auto p = [&](int slot, int i) { return session.effectData(0, slot).param[i]; };
    const float q0 = p(0, 2), shape0 = p(0, 3);
    const auto still = [&](int column) { return fx.cursorRow() == 0 && fx.cursorColumn() == column; };
    // On FREQ: across FREQ (a third of an octave a push), up / down GAIN (a twentieth).
    fx.goTo(0, 1);
    rig.frames(1);
    CHECK(fx.bandGrip() == -1);
    rig.key(Y, true);
    rig.frames(1);
    CHECK(fx.bandGrip() == FxScreen::kBandGain);
    rig.tap(RIGHT);
    CHECK(p(0, 0) == 18.f / 30.f && p(0, 1) == 0.5f);
    rig.tap(UP);
    CHECK(p(0, 1) == 11.f * 0.05f && p(0, 0) == 18.f / 30.f);
    rig.tap(LEFT, 2);
    rig.tap(DOWN, 2);
    CHECK(p(0, 0) == 16.f / 30.f && p(0, 1) == 9.f * 0.05f && p(0, 2) == q0 && still(1));
    // On GAIN: the same.
    rig.key(Y, false);
    rig.tap(DOWN);
    rig.key(Y, true);
    CHECK(still(2) && fx.bandGrip() == FxScreen::kBandGain);
    rig.tap(UP);
    rig.tap(RIGHT);
    CHECK(p(0, 1) == 10.f * 0.05f && p(0, 0) == 17.f / 30.f && still(2));
    // On Q: up / down is Q.
    rig.key(Y, false);
    rig.tap(DOWN);
    rig.key(Y, true);
    CHECK(still(3) && fx.bandGrip() == FxScreen::kBandQ);
    rig.tap(UP);
    rig.tap(LEFT);
    CHECK(p(0, 2) == 7.f * 0.05f && p(0, 1) == 10.f * 0.05f && p(0, 0) == 16.f / 30.f && still(3));
    // The bar says it while Y is held there (on Q, "q"; on FREQ and GAIN, "gain").
    {
        Canvas canvas, scratch;
        CHECK(canvas.ok && scratch.ok);
        if (canvas.ok && scratch.ok) {
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0.f && hintKeyed(canvas, scratch, "LEFT / RIGHT", "freq") && hintKeyed(canvas, scratch, "UP / DOWN", "q"));
            rig.key(Y, false);
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(!hintKeyed(canvas, scratch, "LEFT / RIGHT", "freq") && hintKeyed(canvas, scratch, "LEFT", "list"));
            rig.tap(UP);
            rig.key(Y, true);
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0.f && hintKeyed(canvas, scratch, "UP / DOWN", "gain"));
            rig.key(Y, false);
            rig.tap(DOWN);
            rig.key(Y, true);
        }
    }
    // SHAPE, a choice, is no grip: Y held, the D-pad is the cursor's, and up is the Q dial again (then Q's).
    rig.key(Y, false);
    rig.tap(DOWN);
    rig.key(Y, true);
    CHECK(still(4) && fx.bandGrip() == -1);
    rig.tap(UP);
    CHECK(still(3) && p(0, 3) == shape0 && p(0, 2) == 7.f * 0.05f);
    rig.key(Y, false);
    // Another effect: Y held does what it always did on a dial (nothing): the D-pad moves the cursor.
    fx.goTo(1, 1);
    rig.frames(1);
    const float cutoff = p(1, 0);
    rig.key(Y, true);
    CHECK(fx.bandGrip() == -1);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(fx.cursorRow() == 1 && fx.cursorColumn() == 2 && p(1, 0) == cutoff);
    rig.key(Y, false);
    // The stick: across FREQ and up / down GAIN, both at once, squared as a turn with A held.
    fx.goTo(0, 1);
    rig.frames(1);
    const float f1 = p(0, 0), g1 = p(0, 1);
    rig.key(Y, true);
    rig.key(STICK_RIGHT, true);
    rig.key(STICK_UP, true);
    rig.frames(10);
    rig.key(STICK_RIGHT, false);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(p(0, 0) > f1 + 0.1f && p(0, 0) < f1 + 0.2f && p(0, 1) > g1 + 0.1f && p(0, 1) < g1 + 0.2f && still(1));
    const float f2 = p(0, 0), g2 = p(0, 1);
    rig.key(STICK_LEFT, true);
    rig.frames(10);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(p(0, 0) < f2 - 0.1f && p(0, 1) == g2 && still(1));
    rig.key(Y, false);
    // A direction that brought the cursor onto the dials with Y held is a move, not a turn (review of G18, as the
    // roll's X): from BAND's line, RIGHT held past the repeat's wait, or the stick pushed across, leaves FREQ as it
    // was until a fresh push; so does UP held from SHAPE onto Q.
    rig.tap(LEFT);
    CHECK(still(0));
    const float f3 = p(0, 0), g3 = p(0, 1), q3 = p(0, 2);
    rig.key(Y, true);
    rig.key(RIGHT, true);
    rig.frames(30); // 0.48 s: the cursor's repeats would have come since 0.17
    rig.key(RIGHT, false);
    rig.frames(1);
    CHECK(still(1) && p(0, 0) == f3 && p(0, 1) == g3);
    rig.tap(RIGHT);
    const float f4 = p(0, 0);
    CHECK(still(1) && f4 > f3);
    rig.key(Y, false);
    rig.tap(LEFT);
    rig.key(Y, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(10);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(still(1) && p(0, 0) == f4 && p(0, 1) == g3);
    rig.key(STICK_RIGHT, true);
    rig.frames(5);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(still(1) && p(0, 0) > f4);
    rig.key(Y, false);
    fx.goTo(0, 4);
    rig.frames(1);
    rig.key(Y, true);
    rig.key(UP, true);
    rig.frames(30);
    rig.key(UP, false);
    rig.frames(1);
    CHECK(still(3) && p(0, 2) == q3);
    rig.key(Y, false);
    fx.goTo(0, 1);
    rig.frames(1);
    // WRITE records both, on the step playing.
    session.setWriting(true);
    rig.tap(START);
    rig.render(3 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playStep == 3);
    rig.key(Y, true);
    rig.tap(RIGHT);
    rig.tap(UP);
    rig.key(Y, false);
    const TrackData& track = rig.project().tracks[0];
    CHECK(track.automation[0][autoLaneOfEffect(0, FxScreen::kBandFreq)][3] == p(0, 0) && track.automation[0][autoLaneOfEffect(0, FxScreen::kBandGain)][3] == p(0, 1));
    rig.tap(START);
    session.setWriting(false);
    rig.frames(1);
    // With A held too, A's jobs stand: A + LEFT is the TYPE, not FREQ.
    rig.key(Y, true);
    rig.key(A, true);
    CHECK(fx.bandGrip() == -1);
    rig.tap(LEFT);
    rig.key(A, false);
    rig.key(Y, false);
    CHECK(session.effectData(0, 0).kind != static_cast<int>(EffectKind::Band));
}

// B held on a track: the hints bar says it cannot be undone while the head fills (the head has room for one word).
void testDeletingATrackSaysItCannotBeUndone() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    rig.app.session().addTrack(TrackKind::Phazed);
    rig.app.session().view = View::Song;
    for (int head = 0; head < 2; ++head) {
        rig.app.song().moveCursorTo(0, 1);
        if (head) rig.tap(UP);
        CHECK(rig.app.song().cursorOnHead() == (head == 1));
        const auto draw = [&] {
            canvas.clear();
            rig.app.draw(canvas.r, fonts);
            return ui::gHintsOver == 0.f;
        };
        rig.frames(1);
        CHECK(draw() && !hintedOn(canvas, scratch, SongScreen::kTrackDeletingWords));
        rig.key(B, true);
        rig.frames(20); // the wait: the head is as it was, and so is the bar
        CHECK(draw() && !hintedOn(canvas, scratch, SongScreen::kTrackDeletingWords));
        rig.frames(40);
        CHECK(rig.app.song().trackDeletePart() > 0.f && draw() && hintKeyed(canvas, scratch, "HOLD B", SongScreen::kTrackDeletingWords));
        rig.key(B, false);
        rig.frames(1);
        CHECK(rig.project().trackCount() == 2 && draw() && !hintedOn(canvas, scratch, SongScreen::kTrackDeletingWords));
        if (head) rig.tap(DOWN);
    }
}

// The preset keys say what B does (gesture audit G01): leave with nothing typed, save, or replace where the
// name is one of the player's own already; START is not said (it does what B does). On one of the player's own
// the bar says HOLD B delete this preset, and while B is held that it CANNOT BE UNDONE (a file: G10 for the
// sampler, which said nothing, G30 for the synths), as the kit's. Every bar fits, PHYSIC's dice and all.
void testDeletingAPresetSaysItCannotBeUndone() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Physic, TrackKind::Sampler}) {
        Rig rig;
        Session& session = rig.app.session();
        session.usePresetFolder(tempFolder("presetwords"));
        CHECK(session.addTrack(kind)); // track 1
        rig.project().track = 1;
        session.view = View::Device;
        const bool sampler = kind == TrackKind::Sampler;
        SynthScreen& synth = kind == TrackKind::Phazed ? rig.app.phazed() : rig.app.physic();
        if (sampler) rig.app.sampler().goTo(SamplerScreen::kPresetStop);
        else synth.goTo(0, SynthScreen::kPreset);
        const char* deleting = sampler ? SamplerScreen::kPresetDeletingWords : SynthScreen::kPresetDeletingWords;
        const auto draw = [&] {
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, fonts);
            if (ui::gHintsOver != 0.f) std::printf("    the hints are %.0f pixels over\n", static_cast<double>(ui::gHintsOver));
            return ui::gHintsOver == 0.f;
        };
        // The keys' title: what they do, and where B (or START) would write over a file, that it cannot be undone.
        const char* replacing = sampler ? SamplerScreen::kPresetReplacingWords : SynthScreen::kPresetReplacingWords;
        CHECK(std::string(SynthScreen::kPresetReplacingWords) == SamplerScreen::kPresetReplacingWords);
        const auto title = [&](const char* words, SDL_Color colour) { return inked(canvas, scratch, 0, 40.f, 46.f, words, colour); };
        CHECK(draw() && !hintKeyed(canvas, scratch, "HOLD B", "delete this preset"));
        CHECK(ui::gHelpHint[0] != 0); // (the help card can be had here: the bar offers it)
        rig.tap(R1);
        CHECK(draw() && hintKeyed(canvas, scratch, "B", "leave") && !hintedOn(canvas, scratch, "save") && !hintKeyed(canvas, scratch, "Y", "delete"));
        CHECK(title("SAVE AS PRESET", ui::kDim) && !title(replacing, ui::kBrass));
        CHECK(ui::gHelpHint[0] == 0); // (every button is the keys': no help card, so the bar does not offer one)
        rig.tap(A, 2);
        CHECK(draw() && hintKeyed(canvas, scratch, "B", "save") && hintKeyed(canvas, scratch, "Y", "delete") && !hintedOn(canvas, scratch, "replace") && !hintKeyed(canvas, scratch, "START", "save"));
        rig.tap(B); // saved as AA: the track is on it
        rig.frames(1);
        CHECK(ui::gHelpHint[0] != 0);
        rig.tap(R1); // ...so its name is typed in: B would replace it
        CHECK(draw() && hintKeyed(canvas, scratch, "B", "replace") && !hintedOn(canvas, scratch, "save"));
        CHECK(title(replacing, ui::kBrass) && !title("SAVE AS PRESET", ui::kDim));
        rig.tap(Y);
        CHECK(draw() && hintKeyed(canvas, scratch, "B", "save") && title("SAVE AS PRESET", ui::kDim));
        rig.tap(Y);
        rig.tap(B);
        CHECK(sampler ? !rig.app.sampler().typing() : !synth.typing());
        // On it: HOLD B delete this preset; held, the bar says it cannot be undone; let go, it does not.
        CHECK(draw() && hintKeyed(canvas, scratch, "HOLD B", "delete this preset") && !hintedOn(canvas, scratch, deleting));
        rig.key(B, true);
        rig.frames(30);
        CHECK((sampler ? rig.app.sampler().presetDeletePart() : synth.presetDeletePart()) > 0.f);
        CHECK(draw() && hintKeyed(canvas, scratch, "HOLD B", deleting));
        rig.key(B, false);
        CHECK(draw() && !hintedOn(canvas, scratch, deleting));
        session.usePresetFolder(tempFolder("presetwords-clean")); // the registry is global: leave it empty for later tests
    }
}

// Another instrument for a track: the list says what goes with the old one, and that it cannot be
// undone; not when the cursor is on the one it has (A there changes nothing), nor when adding a track.
void testChangingAnInstrumentSaysWhatGoes() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Phazed);
    session.view = View::Song;
    const int w = 340, x = (ui::kScreenW - w) / 2, top = SongScreen::listTop(kInstrumentCount, true), ly = top + 36 + kInstrumentCount * SongScreen::listPitch(kInstrumentCount, true) + 4;
    const auto says = [&] {
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, fonts);
        const bool first = inked(canvas, scratch, 0, x + 12.f, static_cast<float>(ly), SongScreen::kChangeLostWords[0], ui::kBrass);
        const bool second = inked(canvas, scratch, 0, x + 12.f, ly + 20.f, SongScreen::kChangeLostWords[1], ui::kBrass);
        CHECK(first == second);
        return first;
    };
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(UP);
    rig.tap(A); // the list, on PHAZED: the one it has
    CHECK(!says());
    rig.tap(DOWN); // JAX-EP
    CHECK(says());
    // Inside the list's box, clear of the hints bar.
    // (Six instruments and the words under them reach past the bar from the usual top: the box goes up.)
    CHECK(top + SongScreen::listH(kInstrumentCount, true) <= ui::kScreenH - ui::kHintsH && top > ui::kTopBarH && top < SongScreen::kListY);
    CHECK(ly + 20 + fonts.small.capHeight() < top + SongScreen::listH(kInstrumentCount, true));
    // Adding a track: seven rows (ACID's) stand higher too, at the usual pitch; the words make them close up.
    CHECK(SongScreen::listTop(kInstrumentCount, false) == SongScreen::kListBottom - SongScreen::listH(kInstrumentCount, false) && SongScreen::listTop(kInstrumentCount, false) > ui::kTopBarH);
    // (Eight since CLOUD: they close up adding a track too, to 46, and to 41 with the words, each box a pixel short.
    // Nine since MORPH: 41 and 36, and a box that short draws its name in the medium lettering, both lines
    // placed by their heights: SongScreen::kListLineGap, kListTailRoom.)
    CHECK(SongScreen::listPitch(7, false) == SongScreen::kInstrumentPitch && SongScreen::listPitch(7, true) == 47 && SongScreen::listPitch(6, true) == SongScreen::kInstrumentPitch);
    CHECK(SongScreen::listPitch(8, false) == 46 && SongScreen::listPitch(8, true) == 41 && SongScreen::rowH(8, true) == 40 && SongScreen::rowH(6, false) == 46);
    CHECK(SongScreen::listPitch(kInstrumentCount, false) == 41 && SongScreen::listPitch(kInstrumentCount, true) == 36 && SongScreen::rowH(kInstrumentCount, true) == 35);
    // Both lines at LARGE (what XL is held at) fit the shortest box, the tails and the outline too.
    CHECK(fonts.fixed().medium.capHeight() + SongScreen::kListLineGap + fonts.fixed().small.capHeight() <= SongScreen::rowH(kInstrumentCount, true) - SongScreen::kListTailRoom);
    rig.tap(B);
    CHECK(!says() && rig.project().tracks[1].kind == TrackKind::Phazed);
    // Adding a track loses nothing: nothing said.
    rig.tap(DOWN);
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    CHECK(!says());
    rig.tap(DOWN);
    CHECK(!says());
    rig.tap(B);
}

// B held on a song in the list: its row says it cannot be undone (nothing on the handheld brings it back).
void testDeletingASongSaysItCannotBeUndone() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    const std::string folder = tempFolder("delsong");
    {
        Rig rig;
        Session& session = rig.app.session();
        session.useSongFolder(folder + "/songs", 7u);
        rig.app.drums().setStep(0, 0, 0.8f); // (a song with nothing in it is never written)
        session.view = View::Song;
        rig.frames(2);
        rig.tap(SELECT);
        CHECK(rig.app.song().listingSongs());
        rig.tap(DOWN); // the song
        const int w = 360, x = (ui::kScreenW - w) / 2, by = 60 + 32 + 34;
        const float right = x + w - 22.f, left = right - fonts.small.width(SongScreen::kSongDeletingWords);
        const auto says = [&] {
            canvas.clear();
            rig.app.draw(canvas.r, fonts);
            return inked(canvas, scratch, 0, left, by + 10.f, SongScreen::kSongDeletingWords, ui::kBrass);
        };
        rig.frames(1);
        CHECK(!says());
        rig.key(B, true);
        rig.frames(30);
        CHECK(says() && session.songNames().size() == 1);
        rig.key(B, false);
        rig.frames(1);
        CHECK(session.songNames().size() == 1);
    }
    const int gone = std::system(("rm -rf '" + folder + "'").c_str());
    (void)gone;
}

// PHYSIC (physic-plan.md, sections 3, 7 and 8): the sixth instrument, last in the list. A new track is on
// MARIMBA; the engine plays what the track holds; its four pages, its readings, Y's roll, the song file,
// undo, and a player's own preset over the first preset's sound.
void testPhysicOnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 4); // PHAZED > JAX-EP > SAMPLER > FM-6 > PHYSIC
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Physic);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[32];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 PHYSIC" && isSynth(track.kind));
    const InstrumentInfo* info = instrument(TrackKind::Physic);
    CHECK(info && std::string(info->word) == "physic" && std::string(info->presetEnding) == ".physic" && info->synth == SynthKind::Physic);
    CHECK(&kInstruments[kInstrumentCount - 4] == info && trackKindFromWord("physic") == TrackKind::Physic); // (ACID, CLOUD and MORPH after it)
    // On MARIMBA, its first preset, and it plays.
    const PhysicPatch first = Physic::presetPatch(0);
    CHECK(track.preset == 0 && std::string(physicSpec().presetName(0)) == "Marimba" && std::equal(first.value, first.value + physic::kSettings, track.physic.value));
    CHECK(track.physic.value[physic::Model] == physic::kMarimba && settingCount(TrackKind::Physic) == 23 && presetSettingCount(TrackKind::Physic) == 23);
    CHECK(shownSettingCount(TrackKind::Physic) == 23 && builtInPresets(TrackKind::Physic) >= Physic::presetCount());
    const auto presetNamed = [](const char* name) {
        for (int i = 0; i < Physic::presetCount(); ++i)
            if (std::string(Physic::presetName(i)) == name) return i;
        return -1;
    };
    session.audition(1, 60, 0.8f);
    CHECK(rig.listen(4000) > 0.01f);
    rig.render(96000 * 3);

    // The engine has the sound as the track has it: what a fresh PHYSIC given the same numbers plays. The
    // dice are put at 0 first (a note's roll is the engine's own), and Play and Stop start its voices afresh.
    session.setSynthSetting(1, physic::Dice, 0);
    session.setSynthSetting(1, physic::Decay, 40);
    session.setSynthSetting(1, physic::Material, 70);
    const auto soundsAsTheTrack = [&] {
        session.togglePlay(false);
        rig.render(4800);
        session.togglePlay(false);
        rig.render(96000 * 3);
        Physic same;
        same.prepare(48000.f);
        for (int id = 0; id < physic::kSettings; ++id) same.setSetting(id, track.physic.value[id]);
        same.noteOn(60, 0.8f);
        std::vector<float> l(4000), r(4000);
        for (int at = 0; at < 4000; at += 64) same.render(l.data() + at, r.data() + at, 4000 - at < 64 ? 4000 - at : 64);
        rig.frames(200);
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 60;
        c.value = 0.8f;
        rig.engine->send(c);
        const std::vector<float> heard = rig.render(4000);
        float top = 0.f, scale = 0.f, worst = 0.f;
        for (int i = 0; i < 4000; ++i)
            if (std::fabs(l[i]) > top) top = std::fabs(l[i]), scale = heard[2 * i] / l[i];
        for (int i = 0; i < 4000; ++i) worst = std::fmax(worst, std::fabs(heard[2 * i] - scale * l[i]));
        c.type = Command::NoteOff;
        rig.engine->send(c);
        session.togglePlay(false); // (Play damps it: nothing rings into what comes next)
        rig.render(4800);
        session.togglePlay(false);
        rig.render(4800);
        return top > 0.01f && scale > 0.2f && scale < 1.f && worst < 0.02f * top * scale;
    };
    CHECK(soundsAsTheTrack());

    // The device screen: MAIN, then ADVANCED: five pages, SOUND first, MODEL its first box.
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.physic();
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.pageCount() == 6 && screen.settingHere() == physic::Model);
    rig.key(A, true);
    rig.tap(DOWN); // MARIMBA > STRING: a choice, a click a press
    rig.key(A, false);
    CHECK(track.physic.value[physic::Model] == physic::kString);
    rig.key(A, true);
    rig.tap(UP, 2);
    rig.key(A, false);
    CHECK(track.physic.value[physic::Model] == physic::kXylo);
    rig.tap(R1);
    CHECK(screen.page() == 2 && screen.settingHere() == physic::Decay);
    rig.tap(R1, 4);
    CHECK(screen.page() == 1); // round: RING, PLAY, DICE, BODY, SOUND
    rig.tap(L1);
    CHECK(screen.page() == 5 && screen.settingHere() == physic::Pair);
    rig.tap(RIGHT);
    CHECK(screen.settingHere() == physic::Motor);
    rig.tap(L1); // (the cursor keeps its place: the second box, G13)
    CHECK(screen.page() == 4 && screen.cursor() == 1);
    rig.tap(LEFT);
    CHECK(screen.settingHere() == physic::Dice);
    // Every setting is on one page, once; MAIN has none; the names are as the plan's.
    const SynthSpec& spec = physicSpec();
    int seen[physic::kSettings] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < physic::kSettings && name[0]);
            if (id >= 0 && id < physic::kSettings) ++seen[id];
        }
    for (int id = 0; id < physic::kSettings; ++id) CHECK(seen[id] == 1);
    CHECK(spec.itemCount(0) == 0 && spec.itemCount(1) == 4 && spec.itemCount(2) == 4 && spec.itemCount(3) == 5 && spec.itemCount(4) == 5 && spec.itemCount(5) == 5 && spec.itemCount(6) == 0);
    CHECK(std::string(spec.pageNames[1]) == "SOUND" && std::string(spec.pageNames[2]) == "RING" && std::string(spec.pageNames[3]) == "PLAY" && std::string(spec.pageNames[4]) == "DICE");
    CHECK(std::string(spec.pageNames[5]) == "BODY" && spec.pages == 6);
    CHECK(std::string(spec.macroNames[0]) == "TONE" && std::string(spec.macroNames[1]) == "POSITION" && std::string(spec.macroNames[5]) == "RELEASE" && std::string(spec.macroNames[7]) == "OCTAVE");
    for (int id = 0; id < physic::kSettings; ++id) CHECK(spec.settingMin(id) == physic::settingMin(id) && spec.settingMax(id) == physic::settingMax(id));

    // Its settings read as what they are, in their units.
    const auto reads = [&](int id, int value) { spec.settingText(id, value, text, sizeof text); return std::string(text); };
    CHECK(reads(physic::Model, 0) == "STRING" && reads(physic::Model, 1) == "MARIMBA" && reads(physic::Model, 6) == "BELL" && reads(physic::Model, 10) == "WOOD");
    CHECK(reads(physic::Strike, 0) == "PLUCK" && reads(physic::Strike, 1) == "MALLET" && reads(physic::Strike, 2) == "NOISE");
    // Batch 10's: PAIR in cents, MOTOR in hertz, BODY's names, BODY MIX a share, BUZZ in dB under the loudest.
    CHECK(reads(physic::Pair, 0) == "OFF" && reads(physic::Pair, 7) == "7 CENTS" && reads(physic::Pair, 20) == "20 CENTS");
    CHECK(reads(physic::Motor, 0) == "OFF" && reads(physic::Motor, 1) == "0.20 HZ" && reads(physic::Motor, 85) == "4.6 HZ" && reads(physic::Motor, 100) == "8.0 HZ");
    CHECK(reads(physic::Body, 0) == "NONE" && reads(physic::Body, 1) == "GUITAR" && reads(physic::Body, 2) == "BOX" && reads(physic::Body, 3) == "TUBES");
    CHECK(reads(physic::BodyMix, 0) == "OFF" && reads(physic::BodyMix, 60) == "60%" && reads(physic::Buzz, 0) == "OFF" && reads(physic::Buzz, 1) == "6 DB" && reads(physic::Buzz, 100) == "36 DB");
    CHECK(reads(physic::Decay, 0) == "50 MS" && reads(physic::Decay, 100) == "30 S" && reads(physic::Decay, 58) == "2.0 S" && reads(physic::Decay, 40) == "646 MS");
    CHECK(reads(physic::Release, 0) == "10 MS" && reads(physic::Release, 99) == "2.8 S" && reads(physic::Release, 100) == "RING");
    CHECK(reads(physic::Position, 100) == "1/2" && reads(physic::Position, 39) == "1/5" && reads(physic::Position, 0) == "1.0%" && reads(physic::Position, 50) == "25.5%");
    CHECK(reads(physic::Position, 38) == "19.6%" && reads(physic::Position, 40) == "20.6%"); // only the nearest reads as the fraction
    CHECK(reads(physic::Material, 0) == "WOOD" && reads(physic::Material, 100) == "METAL" && reads(physic::Material, 30) == "30");
    CHECK(reads(physic::Stretch, 50) == "AS MADE" && reads(physic::Stretch, 70) == "+20" && reads(physic::Stretch, 0) == "-50");
    CHECK(reads(physic::Octave, 0) == "+0" && reads(physic::Octave, -2) == "-2" && reads(physic::Fine, -50) == "-50 CENTS" && reads(physic::Level, -24) == "-24 DB" && reads(physic::Level, 6) == "+6 DB");
    CHECK(reads(physic::Velocity, 0) == "OFF" && reads(physic::Velocity, 70) == "70%" && reads(physic::Width, 30) == "30%" && reads(physic::Hardness, 45) == "45%");
    CHECK(reads(physic::Dice, 0) == "OFF" && reads(physic::DicePosition, 25) == "25%" && reads(physic::DiceTune, 100) == "15.0 CENTS" && reads(physic::DiceTune, 60) == "9.0 CENTS" && reads(physic::DiceDecay, 0) == "OFF");
    // Beside the rest of the sound: HARDNESS under MALLET is the mallet's contact; STRETCH on STRING is B.
    PhysicPatch p = Physic::presetPatch(0);
    const auto readsIn = [&](int id) { settingReading(spec, p.value, id, text, sizeof text); return std::string(text); };
    p.value[physic::Hardness] = 0;
    CHECK(readsIn(physic::Hardness) == "8.0 MS");
    p.value[physic::Hardness] = 100;
    CHECK(readsIn(physic::Hardness) == "0.30 MS");
    p.value[physic::Strike] = 2; // NOISE: the burst's length
    CHECK(readsIn(physic::Hardness) == "2 MS");
    p.value[physic::Hardness] = 0;
    CHECK(readsIn(physic::Hardness) == "2.0 S");
    p.value[physic::Hardness] = 50;
    CHECK(readsIn(physic::Hardness) == "63 MS");
    p.value[physic::Hardness] = 100;
    p.value[physic::Strike] = 0;
    CHECK(readsIn(physic::Hardness) == "100%");
    p.value[physic::Stretch] = 50;
    CHECK(readsIn(physic::Stretch) == "AS MADE");
    p.value[physic::Model] = physic::kString;
    CHECK(readsIn(physic::Stretch) == "B 0.0005");
    p.value[physic::Stretch] = 100;
    CHECK(readsIn(physic::Stretch) == "B 0.0020");
    p.value[physic::Stretch] = 20;
    CHECK(readsIn(physic::Stretch) == "B 8.0E-05");
    p.value[physic::Stretch] = 0;
    CHECK(readsIn(physic::Stretch) == "B 0" && readsIn(physic::Decay) == reads(physic::Decay, p.value[physic::Decay]));
    // The macros: OCTAVE a threshold (as Phazed's), the others -100 to +100. OCTAVE takes no MOD.
    spec.macroText(physic::MacroOctave, 0.1f, text, sizeof text);
    CHECK(std::string(text) == "DOWN");
    spec.macroText(physic::MacroOctave, 0.5f, text, sizeof text);
    CHECK(std::string(text) == "AS PRESET");
    spec.macroText(physic::MacroOctave, 0.9f, text, sizeof text);
    CHECK(std::string(text) == "UP");
    spec.macroText(physic::MacroDecay, 1.f, text, sizeof text);
    CHECK(std::string(text) == "+100");
    CHECK(macroIsChoice(TrackKind::Physic, physic::MacroOctave) && !modTargetFits(track, kModFirstMacro + physic::MacroOctave));
    for (int m = 0; m < physic::MacroOctave; ++m) CHECK(!macroIsChoice(TrackKind::Physic, m) && modTargetFits(track, kModFirstMacro + m));

    // Y on MAIN: a new sound on the same MODEL, heard; STRIKE, HARDNESS, POSITION, DECAY, MATERIAL,
    // STRETCH, PAIR, MOTOR, BODY MIX and BUZZ rolled, the rest as it was (BODY too). A step of undo, which
    // puts it back.
    rig.tap(B); // MAIN
    screen.goTo(0, SynthScreen::kPreset);
    rig.frames(60); // (a step of undo is a burst of edits and 0.7 s of none)
    const PhysicPatch before = track.physic;
    const int stepsWere = session.undoSteps();
    rig.tap(Y);
    rig.frames(60);
    bool moved = false;
    for (int id : {physic::Strike, physic::Hardness, physic::Position, physic::Decay, physic::Material, physic::Stretch}) moved = moved || track.physic.value[id] != before.value[id];
    CHECK(moved && session.undoSteps() == stepsWere + 1);
    for (int id : {physic::Model, physic::Release, physic::Velocity, physic::Octave, physic::Fine, physic::Width, physic::Level, physic::Dice,
                   physic::DicePosition, physic::DiceHardness, physic::DiceTune, physic::DiceDecay, physic::Body})
        CHECK(track.physic.value[id] == before.value[id]);
    CHECK(rig.listen(4000) > 0.f); // heard at once (stopped)
    CHECK(soundsAsTheTrack()); // ...and the engine was told the whole new sound
    const PhysicPatch rolled = track.physic;
    rig.tap(Y); // again: another
    rig.frames(60);
    CHECK(!std::equal(rolled.value, rolled.value + physic::kSettings, track.physic.value));
    CHECK(session.undo());
    rig.frames(2);
    CHECK(std::equal(rolled.value, rolled.value + physic::kSettings, track.physic.value));
    CHECK(session.undo());
    rig.frames(2);
    CHECK(std::equal(before.value, before.value + physic::kSettings, track.physic.value));
    // Y with A held does nothing, nor on a page; nor on another synth.
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    CHECK(std::equal(before.value, before.value + physic::kSettings, track.physic.value));
    screen.goTo(2, 0);
    rig.tap(Y);
    CHECK(std::equal(before.value, before.value + physic::kSettings, track.physic.value));
    CHECK(!phazedSpec().roll && !jx3pSpec().roll && !fm6Spec().roll && !session.rollSynth(0, 7));

    // The roll by itself: a seed is a sound, the same twice; every rolled value inside its setting; a
    // string plucked mostly and a modal model struck mostly.
    {
        TrackData* one = new TrackData;
        TrackData* two = new TrackData;
        int plucked[2] = {}, ofModels[2] = {}, noise = 0, paired = 0, motor = 0, buzz = 0;
        for (unsigned seed = 0; seed < 400; ++seed) {
            for (TrackData* t : {one, two}) {
                t->kind = TrackKind::Physic;
                t->physic = Physic::presetPatch(seed % 2 ? presetNamed("Steel") : 0); // a string, MARIMBA
                t->physic.value[physic::Body] = seed % 3; // (BODY is left as it is)
            }
            spec.roll(*one, seed);
            spec.roll(*two, seed);
            CHECK(std::equal(one->physic.value, one->physic.value + physic::kSettingRoom, two->physic.value));
            for (int id = 0; id < physic::kSettings; ++id) CHECK(one->physic.value[id] >= physic::settingMin(id) && one->physic.value[id] <= physic::settingMax(id));
            const int string = one->physic.value[physic::Model] == physic::kString ? 1 : 0;
            plucked[string] += one->physic.value[physic::Strike] == 0 ? 1 : 0;
            noise += one->physic.value[physic::Strike] == physic::kNoise;
            paired += one->physic.value[physic::Pair] > 0, motor += one->physic.value[physic::Motor] > 0, buzz += one->physic.value[physic::Buzz] > 0;
            CHECK(one->physic.value[physic::Body] == static_cast<int>(seed % 3) && one->physic.value[physic::Pair] <= 12 && one->physic.value[physic::Buzz] <= 45);
            CHECK(one->physic.value[physic::BodyMix] >= 20 && one->physic.value[physic::BodyMix] <= 90);
            ++ofModels[string];
            CHECK(one->physic.value[physic::Model] == (seed % 2 ? physic::kString : physic::kMarimba));
            if (string) CHECK(one->physic.value[physic::Stretch] <= 50 && one->physic.value[physic::Decay] <= 85);
            spec.roll(*two, seed + 1);
            CHECK(!std::equal(one->physic.value, one->physic.value + physic::kSettings, two->physic.value));
        }
        CHECK(ofModels[0] == 200 && ofModels[1] == 200 && plucked[1] > 120 && plucked[1] < 190 && plucked[0] < 80 && plucked[0] > 10); // (both, each way)
        CHECK(noise > 25 && noise < 80 && paired > 150 && paired < 250 && motor > 60 && motor < 140 && buzz > 40 && buzz < 120); // (an eighth, a half, a quarter, a fifth)
        delete one;
        delete two;
    }

    // A player's own preset goes over the FIRST preset's sound, never over what the track had: a value it
    // does not hold (a file saved before a setting was appended) is the factory's.
    setUserPresets(TrackKind::Physic, {UserPreset{"SHORT ONE", {physic::kBell, 1, 12, 34}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}}});
    session.chooseSynthPreset(1, presetNamed("Vibes")); // Vibes first: another sound under it, batch 10's settings on
    CHECK(track.physic.value[physic::Motor] > 0 && track.physic.value[physic::Body] == physic::kBodyTubes);
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Physic));
    CHECK(track.preset == builtInPresets(TrackKind::Physic) && std::string(physicSpec().presetCategory(track.preset)) == "USER");
    CHECK(track.physic.value[physic::Model] == physic::kBell && track.physic.value[physic::Hardness] == 12 && track.physic.value[physic::Position] == 34);
    for (int id = physic::Decay; id < physic::kSettingRoom; ++id) CHECK(track.physic.value[id] == first.value[id]);
    setUserPresets(TrackKind::Physic, {});

    // A song keeps a PHYSIC track: its kind, its preset and every number of its sound.
    session.chooseSynthPreset(1, presetNamed("Koto"));
    session.setSynthSetting(1, physic::Buzz, 33); // (a batch 10 setting kept in the song too)
    session.setSynthSetting(1, physic::Width, 66);
    track.macro[physic::MacroMaterial] = 0.8f;
    addPhazed(rig);
    std::unique_ptr<Project> again(new Project);
    const std::string written = songText(rig.project());
    CHECK(written.find("\ntrack physic\n") != std::string::npos);
    CHECK(parseSong(written, *again));
    CHECK(again->tracks[1].kind == TrackKind::Physic && again->tracks[1].preset == presetNamed("Koto") && again->tracks[1].physic.value[physic::Buzz] == 33 && std::equal(track.physic.value, track.physic.value + physic::kSettingRoom, again->tracks[1].physic.value));
    CHECK(near(again->tracks[1].macro[physic::MacroMaterial], 0.8f) && again->tracks[2].kind == TrackKind::Phazed);
    // An older file's settings line, shorter: what it does not hold keeps its preset's value.
    std::string shortLine = written;
    const std::size_t at = shortLine.find("\nsettings ", shortLine.find("\ntrack physic\n"));
    CHECK(at != std::string::npos);
    const std::size_t end = shortLine.find('\n', at + 1);
    shortLine.replace(at, end - at, "\nsettings 0 0 77");
    CHECK(parseSong(shortLine, *again));
    const PhysicPatch koto = Physic::presetPatch(presetNamed("Koto"));
    CHECK(again->tracks[1].physic.value[physic::Model] == 0 && again->tracks[1].physic.value[physic::Hardness] == 77 && again->tracks[1].physic.value[physic::Width] == koto.value[physic::Width]);
}

// PHYSIC's hints fit the bar at 0 over, on MAIN and every page, A held and not, with either help; MAIN
// says Y where it has room (off the macros), and HOLD X on every macro but OCTAVE, a choice.
void testPhysicsHintsFit() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    CHECK(rig->app.session().addTrack(TrackKind::Physic));
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.physic();
    CHECK(screen.pageCount() == 6);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu) {
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : physicSpec().itemCount(page);
            for (int cursor = 0; cursor < stops; ++cursor) {
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL PHYSIC's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (!held && page == 0) {
                        const bool macro = cursor >= SynthScreen::kFirstMacro && cursor < SynthScreen::kAdvanced;
                        CHECK(hintKeyed(canvas, scratch, "Y", "new sound") == !macro);
                        if (macro) CHECK(hintKeyed(canvas, scratch, "HOLD X", "mod") == (cursor - SynthScreen::kFirstMacro != physic::MacroOctave));
                    }
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
            }
        }
    }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + physic::kSettings));
    // Another synth's MAIN says no Y.
    CHECK(rig->app.session().addTrack(TrackKind::Fm6));
    rig->project().track = 2;
    rig->app.fm6().goTo(0, SynthScreen::kPreset);
    rig->frames(1);
    canvas.clear();
    rig->app.draw(canvas.r, fonts);
    CHECK(!hintKeyed(canvas, scratch, "Y", "new sound") && hintKeyed(canvas, scratch, "START", "play"));
}

// ACID (acid-plan.md, sections 1 to 3; batch 3): the seventh instrument, last in the list. A new track
// is on Classic Saw; the engine plays what the track holds; its two pages, its readings, its macros
// (none a choice), Y doing nothing, the song file, undo, and a player's own preset over the first.
void testAcidOnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 5); // PHAZED > JAX-EP > SAMPLER > FM-6 > PHYSIC > ACID
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Acid);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[32];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 ACID" && isSynth(track.kind));
    const InstrumentInfo* info = instrument(TrackKind::Acid);
    CHECK(info && std::string(info->word) == "acid" && std::string(info->presetEnding) == ".acid" && info->synth == SynthKind::Acid);
    CHECK(&kInstruments[kInstrumentCount - 3] == info && trackKindFromWord("acid") == TrackKind::Acid); // (CLOUD and MORPH after it)
    CHECK(std::string(info->about) == "mono bass, after a classic bass line box");
    const AcidPatch first = Acid::presetPatch(0);
    CHECK(track.preset == 0 && std::string(acidSpec().presetName(0)) == "Classic Saw" && std::equal(first.value, first.value + acid::kSettings, track.acid.value));
    CHECK(settingCount(TrackKind::Acid) == 14 && presetSettingCount(TrackKind::Acid) == 14 && shownSettingCount(TrackKind::Acid) == 14);
    CHECK(builtInPresets(TrackKind::Acid) >= Acid::presetCount());
    session.audition(1, 36, 0.9f);
    CHECK(rig.listen(4000) > 0.01f);
    rig.render(48000);

    // The engine has the sound as the track has it: what a fresh ACID given the same numbers plays
    // (Play and Stop start its voice afresh).
    session.setSynthSetting(1, acid::Cutoff, 55);
    session.setSynthSetting(1, acid::Resonance, 80);
    session.setSynthSetting(1, acid::Wave, acid::kSquare);
    const auto soundsAsTheTrack = [&] {
        session.togglePlay(false);
        rig.render(4800);
        session.togglePlay(false);
        rig.render(48000);
        Acid same;
        same.prepare(48000.f);
        for (int id = 0; id < acid::kSettings; ++id) same.setSetting(id, track.acid.value[id]);
        for (int m = 0; m < kSynthMacros; ++m) same.setMacro(m, track.macro[m]);
        same.noteOn(36, 1.f);
        std::vector<float> l(4000), r(4000);
        for (int at = 0; at < 4000; at += 64) same.render(l.data() + at, r.data() + at, 4000 - at < 64 ? 4000 - at : 64);
        rig.frames(200);
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 36;
        c.value = 1.f;
        rig.engine->send(c);
        const std::vector<float> heard = rig.render(4000);
        float top = 0.f, scale = 0.f, worst = 0.f;
        for (int i = 0; i < 4000; ++i)
            if (std::fabs(l[i]) > top) top = std::fabs(l[i]), scale = heard[2 * i] / l[i];
        for (int i = 0; i < 4000; ++i) worst = std::fmax(worst, std::fabs(heard[2 * i] - scale * l[i]));
        c.type = Command::NoteOff;
        rig.engine->send(c);
        session.togglePlay(false);
        rig.render(4800);
        session.togglePlay(false);
        rig.render(4800);
        if (!(worst < 0.01f * top * scale)) std::printf("  ACID against a fresh one: top %g scale %g worst %g\n", top, scale, worst);
        return top > 0.01f && scale > 0.2f && scale < 1.f && worst < 0.01f * top * scale;
    };
    CHECK(soundsAsTheTrack());

    // The device screen: MAIN, then ADVANCED: SOUND (WAVE first), FILTER (CUTOFF first).
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.acid();
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.pageCount() == 3 && screen.settingHere() == acid::Wave);
    rig.key(A, true);
    rig.tap(DOWN); // SQUARE > SAW: a choice, a click a press
    rig.key(A, false);
    CHECK(track.acid.value[acid::Wave] == acid::kSaw);
    rig.tap(R1);
    CHECK(screen.page() == 2 && screen.settingHere() == acid::Cutoff);
    rig.tap(R1);
    CHECK(screen.page() == 1); // round
    const SynthSpec& spec = acidSpec();
    int seen[acid::kSettings] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < acid::kSettings && name[0]);
            if (id >= 0 && id < acid::kSettings) ++seen[id];
        }
    for (int id = 0; id < acid::kSettings; ++id) CHECK(seen[id] == 1);
    CHECK(spec.itemCount(1) == 6 && spec.itemCount(2) == 8 && spec.pages == 3 && std::string(spec.pageNames[1]) == "SOUND" && std::string(spec.pageNames[2]) == "FILTER");
    const char* const lanes[kSynthMacros] = {"CUTOFF", "RESO", "ENV MOD", "DECAY", "ACCENT", "DRIVE", "SWEEP", "TUNE"}; // the engine's lanes (acid::Macro)
    for (int m = 0; m < kSynthMacros; ++m) CHECK(std::string(spec.macroNames[m]) == lanes[m]);
    CHECK(acid::macroSetting(acid::MacroDrive) == acid::Drive && acid::macroSetting(acid::MacroSweep) == acid::Sweep);
    for (int id = 0; id < acid::kSettings; ++id) CHECK(spec.settingMin(id) == acid::settingMin(id) && spec.settingMax(id) == acid::settingMax(id));

    // Its settings read in their units.
    const auto reads = [&](int id, int value) { spec.settingText(id, value, text, sizeof text); return std::string(text); };
    CHECK(reads(acid::Wave, 0) == "SAW" && reads(acid::Wave, 1) == "SQUARE" && reads(acid::Slide, 0) == "NOTE" && reads(acid::Slide, 1) == "60 MS");
    CHECK(reads(acid::Velocity, 0) == "OFF" && reads(acid::Velocity, 1) == "ON" && reads(acid::Octave, -2) == "-2" && reads(acid::Octave, 0) == "+0");
    CHECK(reads(acid::Tune, -100) == "-100 CENTS" && reads(acid::Level, 6) == "+6 DB" && reads(acid::Level, -24) == "-24 DB");
    CHECK(reads(acid::Cutoff, 0) == "50 HZ" && reads(acid::Cutoff, 30) == "214 HZ" && reads(acid::Cutoff, 100) == "6400 HZ");
    CHECK(reads(acid::Decay, 0) == "200 MS" && reads(acid::Decay, 40) == "502 MS" && reads(acid::Decay, 100) == "2.0 S");
    CHECK(reads(acid::AmpDecay, 0) == "200 MS" && reads(acid::AmpDecay, 80) == "3.0 S" && reads(acid::AmpDecay, 100) == "6.0 S");
    CHECK(reads(acid::Accent, 0) == "OFF" && reads(acid::Accent, 50) == "50%" && reads(acid::Drive, 0) == "OFF" && reads(acid::Drive, 30) == "30%");
    CHECK(reads(acid::Resonance, 0) == "0%" && reads(acid::EnvMod, 50) == "50%" && reads(acid::Sweep, 100) == "100%");
    // The macros: none a choice, each -100 to +100, each takes a MOD.
    spec.macroText(acid::MacroCutoff, 1.f, text, sizeof text);
    CHECK(std::string(text) == "+100");
    for (int m = 0; m < kSynthMacros; ++m) CHECK(!macroIsChoice(TrackKind::Acid, m) && modTargetFits(track, kModFirstMacro + m));
    // Y on MAIN does nothing (no roll).
    rig.tap(B);
    screen.goTo(0, SynthScreen::kPreset);
    rig.frames(60);
    const AcidPatch before = track.acid;
    rig.tap(Y);
    rig.frames(2);
    CHECK(!spec.roll && std::equal(before.value, before.value + acid::kSettingRoom, track.acid.value) && !session.rollSynth(1, 7));
    // A setting turned is a step of undo.
    const int stepsWere = session.undoSteps();
    session.setSynthSetting(1, acid::Sweep, 90);
    rig.frames(60);
    CHECK(session.undoSteps() == stepsWere + 1 && session.undo());
    rig.frames(2);
    CHECK(track.acid.value[acid::Sweep] == before.value[acid::Sweep]);

    // A player's own preset goes over the FIRST preset's sound.
    setUserPresets(TrackKind::Acid, {UserPreset{"SHORT ONE", {acid::kSquare, -1, 12}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}}});
    session.chooseSynthPreset(1, 1); // Classic Square first: another sound under it
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Acid));
    CHECK(std::string(acidSpec().presetCategory(track.preset)) == "USER");
    CHECK(track.acid.value[acid::Wave] == acid::kSquare && track.acid.value[acid::Octave] == -1 && track.acid.value[acid::Tune] == 12);
    for (int id = acid::Cutoff; id < acid::kSettingRoom; ++id) CHECK(track.acid.value[id] == first.value[id]);
    setUserPresets(TrackKind::Acid, {});

    // A song keeps an ACID track: its kind, its preset and every number of its sound.
    session.chooseSynthPreset(1, 1);
    session.setSynthSetting(1, acid::Drive, 33);
    session.setSynthSetting(1, acid::Slide, acid::kSlideFixed);
    track.macro[acid::MacroSweep] = 0.8f;
    std::unique_ptr<Project> again(new Project);
    const std::string written = songText(rig.project());
    CHECK(written.find("\ntrack acid\n") != std::string::npos);
    CHECK(parseSong(written, *again));
    CHECK(again->tracks[1].kind == TrackKind::Acid && again->tracks[1].preset == 1 && std::equal(track.acid.value, track.acid.value + acid::kSettingRoom, again->tracks[1].acid.value));
    CHECK(near(again->tracks[1].macro[acid::MacroSweep], 0.8f));
}


// CLOUD (granular-plan.md, batch 2): the eighth instrument, last in the list. A new track is on Soft
// Cloud and plays the built-in TONE; it takes a recording as the sampler does (the list, the song
// file's `sound` line, its uses, a delete, a rename, CATCH), its three pages and readings, its macros
// (none a choice), ROLL on Y, undo, MAIN's picture with the grains, a player's own preset.
void testCloudOnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    const std::string folder = tempFolder("cloud");
    session.useSongFolder(folder + "/songs", 7u);
    session.useSampleFolder(folder + "/samples");
    mkdir((folder + "/samples").c_str(), 0755);
    std::vector<float> wave(48000);
    for (std::size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<float>(0.5 * std::sin(6.283185307 * 220.0 * i / 48000.0));
    CHECK(writeWav(folder + "/samples/hum.wav", wave, 48000) && writeWav(folder + "/samples/buzz.wav", wave, 48000));

    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 6); // PHAZED > JAX-EP > SAMPLER > FM-6 > PHYSIC > ACID > CLOUD
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Cloud);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[48];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 CLOUD" && isSynth(track.kind) && holdsRecording(track.kind) && holdsRecording(TrackKind::Sampler) && !holdsRecording(TrackKind::Acid));
    const InstrumentInfo* info = instrument(TrackKind::Cloud);
    CHECK(info && std::string(info->word) == "cloud" && std::string(info->presetEnding) == ".cloud" && info->synth == SynthKind::Cloud);
    CHECK(&kInstruments[kInstrumentCount - 2] == info && trackKindFromWord("cloud") == TrackKind::Cloud); // (MORPH after it)
    CHECK(std::string(info->about) == "clouds of grains from your recordings");
    const CloudPatch first = Cloud::presetPatch(0);
    CHECK(track.preset == 0 && std::string(cloudSpec().presetName(0)) == "Soft Cloud" && std::equal(first.value, first.value + cloud::kSettings, track.cloud.value));
    CHECK(settingCount(TrackKind::Cloud) == 17 && presetSettingCount(TrackKind::Cloud) == 17 && shownSettingCount(TrackKind::Cloud) == 17);
    CHECK(builtInPresets(TrackKind::Cloud) >= Cloud::presetCount() && !track.sample[0]);
    // No recording: the TONE plays, and its grains are there for the picture.
    session.audition(1, 60, 0.9f);
    CHECK(rig.listen(9600) > 0.01f);
    float places[kGrainsShown];
    CHECK(rig.engine->grainPlaces(1, places, kGrainsShown) > 0);
    rig.render(48000);

    // A recording, as the sampler takes one: kept, read, counted, renamed, deleted.
    session.setTrackSample(1, "hum");
    CHECK(std::string(track.sample) == "hum" && session.trackSound(1).seconds > 0.9f && !session.trackSound(1).missing);
    CHECK(session.sampleUses("hum") == 1 && session.sampleUses("buzz") == 0);
    session.audition(1, 60, 0.9f);
    CHECK(rig.listen(9600) > 0.01f);
    rig.render(48000);
    // The song file says it with the sampler's line, before the synth's.
    std::unique_ptr<Project> again(new Project);
    std::string written = songText(rig.project());
    CHECK(written.find("\ntrack cloud\nsound hum\npreset 1\n") != std::string::npos);
    CHECK(parseSong(written, *again) && again->tracks[1].kind == TrackKind::Cloud && std::string(again->tracks[1].sample) == "hum");
    // A rename follows it; a delete takes it away (the TONE again).
    session.recordingChanged("hum", "drone");
    CHECK(std::string(track.sample) == "drone");
    session.recordingChanged("drone", "hum");
    CHECK(session.deleteSample("hum") && !track.sample[0] && session.sampleUses("hum") == 0);

    // The device screen: MAIN, then SOURCE (RECORDING first), GRAINS, PLAY. The first visit opens on
    // POSITION, not RECORDING (where R1 records: G13); LEFT is RECORDING.
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.cloud();
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.pageCount() == 4 && !screen.onRecording() && screen.settingHere() == cloud::Position);
    rig.tap(LEFT);
    CHECK(screen.onRecording() && screen.settingHere() == SynthScreen::kRecordingId);
    // Hold A + up: the next recording ("buzz", the one left), heard; down: none again.
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(std::string(track.sample) == "buzz" && !screen.picker().isOpen());
    rig.key(A, true);
    rig.tap(DOWN);
    rig.key(A, false);
    CHECK(!track.sample[0]);
    // A tap: the list of recordings; A on a recording puts it on and closes it.
    rig.tap(A);
    CHECK(screen.picker().isOpen());
    while (screen.picker().under() && screen.picker().under()->name != "buzz") rig.tap(DOWN);
    rig.tap(A);
    rig.frames(2);
    CHECK(!screen.picker().isOpen() && std::string(track.sample) == "buzz");
    // Y on the page: CATCH, the last bar the engine played (the keys heard above) as this track's recording.
    rig.tap(Y);
    CHECK(screen.said().find("CAUGHT THE LAST BAR AS") == 0 && std::string(track.sample).find("catches/") == 0 && session.trackSound(1).seconds > 0.f);
    rig.frames(200);
    CHECK(screen.said().empty());
    session.setTrackSample(1, "buzz");
    // RIGHT: POSITION; R1 there turns the page (on RECORDING it records: no input here, said so).
    rig.tap(RIGHT);
    CHECK(screen.settingHere() == cloud::Position);
    rig.tap(R1); // (the cursor keeps its place: the second box, G13)
    CHECK(screen.page() == 2 && screen.settingHere() == cloud::Density);
    rig.tap(LEFT);
    CHECK(screen.settingHere() == cloud::Size);
    rig.tap(R1);
    CHECK(screen.page() == 3 && screen.settingHere() == cloud::PitchSpray);
    // Round to SOURCE from the first box: the place after RECORDING, never RECORDING itself (G13).
    rig.tap(R1);
    CHECK(screen.page() == 1 && !screen.onRecording() && screen.settingHere() == cloud::Position);
    rig.tap(LEFT);
    rig.tap(R1);
    CHECK(screen.page() == 1 && screen.said().find("NO INPUT FOUND") == 0);
    rig.frames(200);
    // (The review, batch 5.) SOURCE has six stops, TUNE the last.
    screen.goTo(1, 5);
    CHECK(screen.settingHere() == cloud::Tune);
    screen.goTo(1, 0);
    // Hold A + UP stops at the last recording; the stick with A held steps one at a time.
    const std::vector<std::string> names = session.sampleNames();
    CHECK(names.size() >= 2);
    session.setTrackSample(1, names.back());
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(track.sample == names.back() && !screen.picker().isOpen());
    session.setTrackSample(1, names.front());
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(8);
    rig.key(STICK_UP, false);
    rig.key(A, false);
    CHECK(track.sample == names[1] && !screen.picker().isOpen());
    // R1 with A held does not record (it turns the page, as on every synth); Y off SOURCE catches nothing.
    rig.key(A, true);
    rig.tap(R1);
    rig.key(A, false);
    CHECK(screen.page() == 2 && screen.said().empty());
    rig.tap(Y);
    CHECK(screen.said().empty() && track.sample == names[1]);
    // The list's preview: heard for a moment and stopped; X while it plays rests on it, on a while.
    CHECK(writeWav(folder + "/samples/fizz.wav", wave, 48000));
    session.setTrackSample(1, "buzz");
    screen.goTo(1, 0);
    rig.tap(A);
    CHECK(screen.picker().isOpen());
    for (int i = 0; i < 6 && screen.picker().under() && screen.picker().under()->name != "fizz"; ++i) rig.tap(DOWN);
    CHECK(track.sample == std::string("fizz") && session.hearingOn(&screen));
    rig.frames(30);
    CHECK(session.hearingOn(&screen)); // (X is not held: the preview is its own time)
    rig.frames(80); // 1.76 s in all
    CHECK(!session.hearingOn(&screen));
    rig.tap(UP);
    rig.tap(DOWN);
    CHECK(session.hearingOn(&screen));
    rig.tap(X);
    rig.frames(150); // 2.4 s
    CHECK(session.hearingOn(&screen));
    rig.frames(150);
    CHECK(!session.hearingOn(&screen));
    rig.tap(B);
    rig.frames(2);
    CHECK(!screen.picker().isOpen());
    const SynthSpec& spec = cloudSpec();
    int seen[cloud::kSettings] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < cloud::kSettings && name[0]);
            if (id >= 0 && id < cloud::kSettings) ++seen[id];
        }
    for (int id = 0; id < cloud::kSettings; ++id) CHECK(seen[id] == 1);
    CHECK(spec.pages == 4 && std::string(spec.pageNames[1]) == "SOURCE" && std::string(spec.pageNames[2]) == "GRAINS" && std::string(spec.pageNames[3]) == "PLAY" && spec.recording);
    const char* const lanes[kSynthMacros] = {"POSITION", "SPRAY", "SIZE", "DENSITY", "SCAN", "PITCH", "SHAPE", "REVERSE"}; // the engine's lanes (cloud::Macro)
    for (int m = 0; m < kSynthMacros; ++m) CHECK(std::string(spec.macroNames[m]) == lanes[m]);
    CHECK(cloud::macroSetting(cloud::MacroScan) == cloud::Scan && cloud::macroSetting(cloud::MacroPitchSpray) == cloud::PitchSpray);
    for (int id = 0; id < cloud::kSettings; ++id) CHECK(spec.settingMin(id) == cloud::settingMin(id) && spec.settingMax(id) == cloud::settingMax(id));

    // Its settings read in their units.
    const auto reads = [&](int id, int value) { spec.settingText(id, value, text, sizeof text); return std::string(text); };
    CHECK(reads(cloud::Position, 25) == "25%" && reads(cloud::Spray, 0) == "OFF" && reads(cloud::Spray, 10) == "+-0.50%" && reads(cloud::Spray, 1) == "+-0.005%" && reads(cloud::Spray, 100) == "+-50.0%");
    CHECK(reads(cloud::Size, 0) == "5.0 MS" && reads(cloud::Size, 60) == "120 MS" && reads(cloud::Size, 100) == "1.00 S");
    CHECK(reads(cloud::Density, 0) == "1.00 HZ" && reads(cloud::Density, 52) == "15.7 HZ" && reads(cloud::Density, 100) == "200 HZ");
    CHECK(reads(cloud::Shape, 0) == "PLUCK" && reads(cloud::Shape, 50) == "SMOOTH" && reads(cloud::Shape, 100) == "SWELL" && reads(cloud::Shape, 30) == "PEAK 30%");
    CHECK(reads(cloud::PitchSpray, 0) == "OFF" && reads(cloud::PitchSpray, 50) == "+-300 CENTS" && reads(cloud::PitchSpray, 2) == "+-0.5 CENTS" && reads(cloud::PitchSpray, 100) == "+-1200 CENTS");
    CHECK(reads(cloud::PitchSteps, 0) == "FREE" && reads(cloud::PitchSteps, 1) == "OCTAVES" && reads(cloud::PitchSteps, 2) == "FIFTHS");
    CHECK(reads(cloud::Scan, 50) == "FREEZE" && reads(cloud::Scan, 75) == "+1.00X" && reads(cloud::Scan, 0) == "-2.00X");
    CHECK(reads(cloud::Root, 60) == "0" && reads(cloud::Root, 69) == "+9" && reads(cloud::Root, 48) == "-12" && reads(cloud::Tune, -100) == "-100 CENTS");
    CHECK(reads(cloud::Attack, 8) == "2.1 MS" && reads(cloud::Release, 54) == "289 MS" && reads(cloud::Level, 100) == "0.00 DB" && reads(cloud::Level, 80) == "-1.9 DB" && reads(cloud::Level, 0) == "OFF");
    CHECK(reads(cloud::Reverse, 0) == "OFF" && reads(cloud::Velocity, 50) == "50%" && reads(cloud::Stereo, 40) == "40%");
    // Every click of the D-pad reads another way (SPRAY 1 to 3 read +-0.0%, as if off, until the review).
    for (int id = 0; id < cloud::kSettings; ++id)
        for (int v = cloud::settingMin(id) + 1; v <= cloud::settingMax(id); ++v)
            if (reads(id, v) == reads(id, v - 1)) {
                std::printf("    CLOUD %d reads %s at %d and %d\n", id, text, v - 1, v);
                CHECK(false);
            }
    // The macros: none a choice, each -100 to +100, each takes a MOD.
    for (int m = 0; m < kSynthMacros; ++m) CHECK(!macroIsChoice(TrackKind::Cloud, m) && modTargetFits(track, kModFirstMacro + m));

    // Y on MAIN rolls a new cloud: the grains' settings, never the recording, POSITION, SCAN, ROOT,
    // TUNE, the envelope or the levels. One step of undo.
    rig.tap(B);
    screen.goTo(0, SynthScreen::kPreset);
    rig.frames(60);
    const CloudPatch before = track.cloud;
    const int stepsWere = session.undoSteps();
    rig.tap(Y);
    rig.frames(60);
    bool moved = false;
    for (int id : {cloud::Spray, cloud::Size, cloud::Density, cloud::Scatter, cloud::Shape, cloud::Stereo}) moved = moved || track.cloud.value[id] != before.value[id];
    CHECK(moved && spec.roll && std::string(track.sample) == "buzz");
    for (int id : {cloud::Position, cloud::Scan, cloud::Root, cloud::Tune, cloud::Attack, cloud::Release, cloud::Level, cloud::Velocity}) CHECK(track.cloud.value[id] == before.value[id]);
    for (unsigned seed = 0; seed < 200; ++seed) {
        CHECK(session.rollSynth(1, seed));
        for (int id = 0; id < cloud::kSettings; ++id) CHECK(track.cloud.value[id] >= cloud::settingMin(id) && track.cloud.value[id] <= cloud::settingMax(id));
    }
    rig.frames(60);
    CHECK(session.undoSteps() >= stepsWere + 1 && session.undo());

    // A player's own preset goes over the FIRST preset's sound, and keeps the track's recording.
    setUserPresets(TrackKind::Cloud, {UserPreset{"MY FOG", {70, 30}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}}});
    session.chooseSynthPreset(1, 1);
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Cloud));
    CHECK(std::string(cloudSpec().presetCategory(track.preset)) == "USER" && track.cloud.value[cloud::Position] == 70 && track.cloud.value[cloud::Spray] == 30);
    for (int id = cloud::Size; id < cloud::kSettingRoom; ++id) CHECK(track.cloud.value[id] == first.value[id]);
    CHECK(std::string(track.sample) == "buzz");
    setUserPresets(TrackKind::Cloud, {});

    // A song keeps a CLOUD track: its kind, its recording, its preset and every number of its sound.
    session.chooseSynthPreset(1, 2);
    session.setSynthSetting(1, cloud::Scan, 80);
    track.macro[cloud::MacroDensity] = 0.8f;
    written = songText(rig.project());
    CHECK(parseSong(written, *again));
    CHECK(again->tracks[1].kind == TrackKind::Cloud && again->tracks[1].preset == 2 && std::string(again->tracks[1].sample) == "buzz");
    CHECK(std::equal(track.cloud.value, track.cloud.value + cloud::kSettingRoom, again->tracks[1].cloud.value) && near(again->tracks[1].macro[cloud::MacroDensity], 0.8f));
    // A sampler given the CLOUD keeps the recording, and the other way.
    CHECK(session.setTrackKind(1, TrackKind::Sampler) && std::string(track.sample) == "buzz");
    CHECK(session.setTrackKind(1, TrackKind::Cloud) && std::string(track.sample) == "buzz" && session.trackSound(1).seconds > 0.9f);
    // Given an instrument with no recording, it lets the recording go (the file would not keep it, and
    // it came back unseen on the way back to CLOUD: the review, batch 5).
    CHECK(session.setTrackKind(1, TrackKind::Phazed) && !track.sample[0] && session.sampleUses("buzz") == 0);
    CHECK(session.setTrackKind(1, TrackKind::Cloud) && !track.sample[0]);
}

// CLOUD's hints fit the bar at 0 over, on MAIN and its three pages, A held and not, with either help;
// MAIN says HOLD X on every macro and Y off them; RECORDING says its list, R1 and Y. MAIN's picture
// has a brass POSITION line and, while a key sounds, teal ticks; SIGNAL still below the macros.
void testCloudsHintsFit() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    CHECK(rig->app.session().addTrack(TrackKind::Cloud));
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.cloud();
    CHECK(screen.pageCount() == 4);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu)
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : cloudSpec().itemCount(page) + (page == 1 ? 1 : 0);
            for (int cursor = 0; cursor < stops; ++cursor)
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL CLOUD's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (!held && page == 0) {
                        const bool macro = cursor >= SynthScreen::kFirstMacro && cursor < SynthScreen::kAdvanced;
                        CHECK(hintKeyed(canvas, scratch, "Y", "new sound") == !macro);
                        if (macro) CHECK(hintKeyed(canvas, scratch, "HOLD X", "mod"));
                    }
                    if (!held && page == 1 && cursor == 0) CHECK(hintKeyed(canvas, scratch, "A", "list") && hintKeyed(canvas, scratch, "R1", "record") && hintKeyed(canvas, scratch, "Y", "catch"));
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
        }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + cloud::kSettings + 1));
    // MAIN's picture: POSITION's brass line where POSITION (25%) puts it; the ticks of a key sounding.
    screen.goTo(0, SynthScreen::kPreset);
    const int px = 10, pw = ui::kScreenW - 20, y = SynthScreen::kPictureY;
    const auto drawn = [&] { rig->frames(1); canvas.clear(); rig->app.draw(canvas.r, fonts); };
    drawn();
    const int brassX = px + static_cast<int>(0.25f * (pw - 2));
    CHECK(canvas.is(brassX, y + SynthScreen::kPictureH / 2, ui::kBrass));
    int ticks = 0;
    for (int x = px; x < px + pw; ++x) ticks += canvas.is(x, y + 4, ui::kTeal) ? 1 : 0;
    CHECK(ticks == 0);
    Command c;
    c.type = Command::NoteOn, c.track = 1, c.a = 60, c.value = 0.9f;
    rig->engine->send(c);
    rig->render(9600);
    drawn();
    for (int x = px; x < px + pw; ++x) ticks += canvas.is(x, y + 4, ui::kTeal) ? 1 : 0;
    CHECK(ticks >= 2);
    // (The review, batch 5.) The band goes round the recording's ends, and on the TONE it is lit dim:
    // POSITION 1%, SPRAY 20 (2% either way) lights the last columns too, not the middle.
    TrackData& t = rig->project().tracks[1];
    t.cloud.value[cloud::Position] = 1;
    t.cloud.value[cloud::Spray] = 20;
    const int ph = SynthScreen::kPictureH - 4 - 2 * SynthScreen::kTickH, midY = y + 2 + SynthScreen::kTickH + (ph - 1) / 2;
    const auto column = [&](int i) { return px + pw * i / 512; };
    drawn();
    CHECK(canvas.is(column(510), midY, ui::kDim) && canvas.is(column(256), midY, ui::kLine));
    t.cloud.value[cloud::Position] = 99; // ...and the first columns from the far end
    drawn();
    CHECK(canvas.is(column(1), midY, ui::kDim) && canvas.is(column(256), midY, ui::kLine));
    t.cloud.value[cloud::Position] = 1;
    // A recording that is missing: the TONE's outline all the same, dim.
    rig->app.session().setTrackSample(1, "nothere");
    drawn();
    CHECK(canvas.is(column(510), midY, ui::kDim) && canvas.is(column(256), midY, ui::kLine));
    CHECK(SynthScreen::kPicAdvancedY + SynthScreen::kAdvancedH <= ui::kAboveScope && SynthScreen::kPicMacroH >= 60);
}

// MORPH (wavetable-plan.md, batch 2): the ninth instrument, last in the list. A new track is on Analog
// Sweep; its five pages and their readings (every click of a setting reads another way), UNISON's keys,
// its macros (none a choice), ROLL on Y, undo, a player's own preset, the song file.
void testMorphOnATrack() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, rig.project().trackCount());
    rig.tap(A);
    rig.tap(DOWN, 7); // PHAZED > JAX-EP > SAMPLER > FM-6 > PHYSIC > ACID > CLOUD > MORPH
    rig.tap(A);
    CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == TrackKind::Morph);
    TrackData& track = rig.project().tracks[1];
    rig.project().track = 1;
    char text[48];
    ui::trackName(rig.project(), 1, text, sizeof text);
    CHECK(std::string(text) == "2 MORPH" && isSynth(track.kind) && !holdsRecording(track.kind));
    const InstrumentInfo* info = instrument(TrackKind::Morph);
    CHECK(info && std::string(info->word) == "morph" && std::string(info->presetEnding) == ".morph" && info->synth == SynthKind::Morph);
    CHECK(&kInstruments[kInstrumentCount - 1] == info && trackKindFromWord("morph") == TrackKind::Morph);
    CHECK(std::string(info->about) == "wavetables, swept from wave to wave" && std::string(info->shortName) == "MRPH");
    const MorphPatch first = Morph::presetPatch(0);
    CHECK(track.preset == 0 && std::string(morphSpec().presetName(0)) == "Analog Sweep" && std::equal(first.value, first.value + morph::kSettings, track.morph.value));
    CHECK(settingCount(TrackKind::Morph) == 29 && presetSettingCount(TrackKind::Morph) == 29 && shownSettingCount(TrackKind::Morph) == 29);
    CHECK(builtInPresets(TrackKind::Morph) >= Morph::presetCount());
    session.audition(1, 60, 0.9f);
    CHECK(rig.listen(4000) > 0.01f);
    rig.render(48000);
    // A key X holds is struck again when TABLE A, TABLE B, UNISON or WIDTH changes (each read at the strike):
    // UNISON 2 heard on the held key at once, as two streams. Any other setting changes the key as it sounds.
    {
        const Morph* synth = dynamic_cast<const Morph*>(rig.engine->synthForTests(1));
        CHECK(synth != nullptr);
        session.auditionHeld(1, 60, kPlacedLevel, &rig);
        rig.render(2048);
        CHECK(synth && synth->streamsInUse() == 1);
        session.setSynthSetting(1, morph::Unison, 2);
        rig.render(2048);
        CHECK(synth && synth->streamsInUse() == 2 && synth->sounds(60) && session.hearingHeld());
        session.setSynthSetting(1, morph::TableB, 1 + morph::Pulse); // B on: two streams more
        rig.render(2048);
        CHECK(synth && synth->streamsInUse() == 4);
        const long struck = synth ? synth->strikes() : 0;
        session.setSynthSetting(1, morph::Cutoff, 40); // not read at the strike: no new strike
        rig.render(2048);
        CHECK(synth && synth->streamsInUse() == 4 && synth->keysSounding() == 1 && synth->strikes() == struck);
        session.setSynthSetting(1, morph::Width, 100); // read at the strike: struck again
        rig.render(2048);
        CHECK(synth && synth->strikes() == struck + 1 && synth->keysSounding() == 1 && session.hearingHeld());
        session.letGoHeard();
        session.setSynthSetting(1, morph::Unison, 1);
        session.setSynthSetting(1, morph::TableB, 0);
        session.setSynthSetting(1, morph::Width, first.value[morph::Width]);
        session.setSynthSetting(1, morph::Cutoff, first.value[morph::Cutoff]);
        rig.frames(1), rig.render(48000);
    }

    // The device screen: MAIN, then ADVANCED: OSC (TABLE A first), VOICE, FILTER, AMP, ENV.
    session.view = View::Device;
    rig.frames(2);
    SynthScreen& screen = rig.app.morph();
    screen.goTo(0, SynthScreen::kAdvanced);
    rig.tap(A);
    CHECK(screen.page() == 1 && screen.pageCount() == 6 && screen.settingHere() == morph::TableA);
    rig.key(A, true);
    rig.tap(UP); // ANALOG > PULSE: a choice, a click a press
    rig.key(A, false);
    CHECK(track.morph.value[morph::TableA] == morph::Pulse);
    rig.tap(R1);
    CHECK(screen.page() == 2 && screen.settingHere() == morph::Octave);
    const SynthSpec& spec = morphSpec();
    int seen[morph::kSettings] = {};
    for (int page = 1; page < spec.pages; ++page)
        for (int i = 0; i < spec.itemCount(page); ++i) {
            int id = -1;
            const char* name = "";
            spec.item(page, i, id, name);
            CHECK(id >= 0 && id < morph::kSettings && name[0]);
            if (id >= 0 && id < morph::kSettings) ++seen[id];
        }
    for (int id = 0; id < morph::kSettings; ++id) CHECK(seen[id] == 1);
    CHECK(spec.pages == 6 && std::string(spec.pageNames[1]) == "OSC" && std::string(spec.pageNames[5]) == "ENV" && spec.table && !spec.recording);
    const char* const lanes[kSynthMacros] = {"POSITION", "MIX", "CUTOFF", "RESO", "ENV POS", "ENV CUT", "DETUNE", "RELEASE"}; // the engine's lanes (morph::Macro)
    for (int m = 0; m < kSynthMacros; ++m) CHECK(std::string(spec.macroNames[m]) == lanes[m]);
    CHECK(morph::macroSetting(morph::MacroEnvCutoff) == morph::EnvCutoff && morph::macroSetting(morph::MacroRelease) == morph::Release);
    for (int id = 0; id < morph::kSettings; ++id) CHECK(spec.settingMin(id) == morph::settingMin(id) && spec.settingMax(id) == morph::settingMax(id));

    // Its settings read in their units, and every click of a setting reads another way.
    const auto reads = [&](int id, int value) { spec.settingText(id, value, text, sizeof text); return std::string(text); };
    CHECK(reads(morph::TableA, morph::Analog) == "ANALOG" && reads(morph::TableA, morph::Glass) == "GLASS" && reads(morph::TableB, 0) == "OFF" && reads(morph::TableB, 1) == "ANALOG");
    CHECK(reads(morph::PositionA, 0) == "FRAME 1.0" && reads(morph::PositionA, 100) == "FRAME 32.0" && reads(morph::PositionB, 50) == "FRAME 16.5");
    CHECK(reads(morph::Mix, 0) == "A ONLY" && reads(morph::Mix, 100) == "B ONLY" && reads(morph::Mix, 30) == "A 70 / B 30");
    CHECK(reads(morph::BSemi, -24) == "-24 ST" && reads(morph::BFine, 7) == "+7 CENTS" && reads(morph::Sub, 0) == "OFF" && reads(morph::Sub, 100) == "0.00 DB");
    CHECK(reads(morph::Cutoff, 0) == "20 HZ" && reads(morph::Cutoff, 100) == "20.0 KHZ" && reads(morph::Detune, 0) == "OFF" && reads(morph::Detune, 100) == "+-50.0 CENTS");
    CHECK(reads(morph::Filter, morph::kLow24) == "LOW 24" && reads(morph::EnvPosition, 0) == "OFF" && reads(morph::EnvPosition, -100) == "-31.0 FRAMES");
    CHECK(reads(morph::EnvCutoff, 50) == "+3.00 OCT" && reads(morph::Level, -24) == "-24 DB" && reads(morph::Velocity, 0) == "OFF" && reads(morph::Attack, 8) == "2.1 MS");
    CHECK(reads(morph::Decay, 50) == "200 MS" && reads(morph::Release, 80) == "3.2 S" && reads(morph::EnvAttack, 60) == "220 MS");
    for (int id = 0; id < morph::kSettings; ++id) {
        std::string last;
        for (int v = morph::settingMin(id); v <= morph::settingMax(id); ++v) {
            const std::string now = reads(id, v);
            CHECK(!now.empty() && now != last);
            if (now == last) std::printf("  MORPH setting %d reads %s at %d and %d\n", id, now.c_str(), v - 1, v);
            last = now;
        }
    }
    // UNISON reads with the keys the pool leaves: B on takes twice the streams.
    const int* v = track.morph.value;
    spec.readingIn(v, morph::Unison, 1, text, sizeof text);
    CHECK(std::string(text) == "1, 6 KEYS");
    track.morph.value[morph::TableB] = 1 + morph::Fm;
    spec.readingIn(v, morph::Unison, 4, text, sizeof text);
    CHECK(std::string(text) == "4, 3 KEYS");
    spec.readingIn(v, morph::Unison, 3, text, sizeof text);
    CHECK(std::string(text) == "3, 4 KEYS");
    track.morph.value[morph::TableB] = 0;
    // The macros: none a choice, each -100 to +100, each takes a MOD.
    spec.macroText(morph::MacroPosition, 1.f, text, sizeof text);
    CHECK(std::string(text) == "+100");
    for (int m = 0; m < kSynthMacros; ++m) CHECK(!macroIsChoice(TrackKind::Morph, m) && modTargetFits(track, kModFirstMacro + m));

    // Y on MAIN rolls a new sound: the AMP envelope, OCTAVE, TUNE and the levels stay, every roll in
    // range; one step of undo.
    std::unique_ptr<TrackData> rolled(new TrackData(track)); // (a megabyte: never on the stack)
    for (unsigned seed = 0; seed < 200; ++seed) {
        spec.roll(*rolled, seed);
        for (int id = 0; id < morph::kSettings; ++id) CHECK(rolled->morph.value[id] >= morph::settingMin(id) && rolled->morph.value[id] <= morph::settingMax(id));
        for (int id : {morph::Octave, morph::Tune, morph::Attack, morph::Decay, morph::Sustain, morph::Release, morph::KeyTrack, morph::Velocity, morph::Level})
            CHECK(rolled->morph.value[id] == track.morph.value[id]);
    }
    rig.tap(B);
    screen.goTo(0, SynthScreen::kPreset);
    rig.frames(60);
    const MorphPatch before = track.morph;
    const int stepsWere = session.undoSteps();
    rig.tap(Y);
    rig.frames(60);
    CHECK(!std::equal(before.value, before.value + morph::kSettingRoom, track.morph.value) && session.undoSteps() == stepsWere + 1);
    CHECK(session.undo());
    rig.frames(2);
    CHECK(std::equal(before.value, before.value + morph::kSettingRoom, track.morph.value));

    // A player's own preset goes over the FIRST preset's sound.
    setUserPresets(TrackKind::Morph, {UserPreset{"SHORT ONE", {morph::Glass, 70, 1 + morph::Fm}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}}});
    session.chooseSynthPreset(1, 3);
    session.chooseSynthPreset(1, builtInPresets(TrackKind::Morph));
    CHECK(std::string(morphSpec().presetCategory(track.preset)) == "USER");
    CHECK(track.morph.value[morph::TableA] == morph::Glass && track.morph.value[morph::PositionA] == 70 && track.morph.value[morph::TableB] == 1 + morph::Fm);
    for (int id = morph::PositionB; id < morph::kSettingRoom; ++id) CHECK(track.morph.value[id] == first.value[id]);
    setUserPresets(TrackKind::Morph, {});

    // A song keeps a MORPH track: its kind, its preset and every number of its sound.
    session.chooseSynthPreset(1, 2);
    session.setSynthSetting(1, morph::Unison, 3);
    session.setSynthSetting(1, morph::EnvPosition, -40);
    track.macro[morph::MacroPosition] = 0.8f;
    std::unique_ptr<Project> again(new Project);
    const std::string written = songText(rig.project());
    CHECK(written.find("\ntrack morph\n") != std::string::npos);
    CHECK(parseSong(written, *again));
    CHECK(again->tracks[1].kind == TrackKind::Morph && again->tracks[1].preset == 2 && std::equal(track.morph.value, track.morph.value + morph::kSettingRoom, again->tracks[1].morph.value));
    CHECK(near(again->tracks[1].macro[morph::MacroPosition], 0.8f));
}

// MORPH's hints fit the bar at 0 over on MAIN and every page, A held and not, with either help; MAIN
// says Y new sound off the macros and HOLD X on every macro. MAIN's picture: the wave in teal, B's in
// purple when B is on, POSITION in brass on the strip; a key sounding puts its teal tick there.
void testMorphsHintsAndPicture() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    CHECK(rig->app.session().addTrack(TrackKind::Morph));
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.morph();
    CHECK(screen.pageCount() == 6);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu)
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : morphSpec().itemCount(page);
            for (int cursor = 0; cursor < stops; ++cursor)
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL MORPH's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (!held && page == 0) {
                        const bool macro = cursor >= SynthScreen::kFirstMacro && cursor < SynthScreen::kAdvanced;
                        CHECK(hintKeyed(canvas, scratch, "Y", "new sound") == !macro);
                        if (macro) CHECK(hintKeyed(canvas, scratch, "HOLD X", "mod"));
                    }
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
        }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + morph::kSettings));

    // The picture: POSITION A's brass mark on the strip where the knob puts it (Analog Sweep: 30, frame
    // 10.3 of 32); the wave teal somewhere in each column's room; no purple with B off, purple with it on.
    TrackData& t = rig->project().tracks[1];
    screen.goTo(0, SynthScreen::kPreset);
    const auto drawn = [&] { rig->frames(1); canvas.clear(); rig->app.draw(canvas.r, fonts); };
    const auto inRoom = [&](SDL_Color c) {
        int n = 0;
        const int y0 = SynthScreen::kPictureY + 3, y1 = SynthScreen::kPictureY + SynthScreen::kPictureH - 3 - SynthScreen::kFrameStripH;
        for (int x = 10; x < ui::kScreenW - 10; x += 3)
            for (int y = y0; y < y1; ++y)
                if (canvas.is(x, y, c)) { ++n; break; }
        return n;
    };
    drawn();
    const int sx = 10, sw = ui::kScreenW - 20, stripY = SynthScreen::kPictureY + SynthScreen::kPictureH - 2 - SynthScreen::kFrameStripH;
    const double place = morph::positionPlace(t.morph.value[morph::PositionA], 32);
    CHECK(canvas.is(sx + static_cast<int>(place / 31.0 * (sw - 2)), stripY + SynthScreen::kFrameStripH / 2, ui::kBrass));
    CHECK(inRoom(ui::kTeal) > 150 && inRoom(ui::kPurple) == 0);
    rig->app.session().setSynthSetting(1, morph::TableB, 1 + morph::Vowel);
    drawn();
    CHECK(inRoom(ui::kPurple) > 40);
    // A key sounding: its place a teal tick on the strip (the readout through the engine).
    int ticks = 0;
    const auto countTicks = [&] {
        int n = 0;
        for (int x = sx; x < sx + sw; ++x) n += canvas.is(x, stripY + 1, ui::kTeal) ? 1 : 0;
        return n;
    };
    ticks = countTicks();
    CHECK(ticks == 0);
    Command c;
    c.type = Command::NoteOn, c.track = 1, c.a = 60, c.value = 0.9f;
    rig->engine->send(c);
    rig->render(4800);
    drawn();
    CHECK(countTicks() >= 2);
    // The waves follow POSITION: another place, other pixels.
    rig->app.session().setSynthSetting(1, morph::TableB, 0);
    c.type = Command::NoteOff;
    rig->engine->send(c);
    rig->render(48000 * 3);
    t.morph.value[morph::PositionA] = 0; // a sine: the wave crosses the middle at the box's middle
    drawn();
    const int midX = 10 + (ui::kScreenW - 20) / 2, midY = SynthScreen::kPictureY + 3 + (SynthScreen::kPictureH - 6 - SynthScreen::kFrameStripH) / 2;
    bool nearMiddle = false;
    for (int dy = -2; dy <= 2; ++dy) nearMiddle = nearMiddle || canvas.is(midX, midY + dy, ui::kTeal);
    CHECK(nearMiddle);
}

// ACID's hints fit the bar at 0 over, on MAIN and both pages, A held and not, with either help; MAIN
// says HOLD X on every macro (none is a choice) and no Y.
void testAcidsHintsFit() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    CHECK(rig->app.session().addTrack(TrackKind::Acid));
    rig->project().track = 1;
    rig->app.session().view = View::Device;
    SynthScreen& screen = rig->app.acid();
    CHECK(screen.pageCount() == 3);
    int states = 0;
    for (int menu = 0; menu < 2; ++menu)
        for (int page = 0; page < screen.pageCount(); ++page) {
            const int stops = page == 0 ? SynthScreen::kMainStops : acidSpec().itemCount(page);
            for (int cursor = 0; cursor < stops; ++cursor)
                for (int held = 0; held < 2; ++held) {
                    screen.goTo(page, cursor);
                    if (held) rig->key(A, true);
                    rig->frames(1);
                    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP";
                    canvas.clear();
                    rig->app.draw(canvas.r, fonts);
                    ++states;
                    if (ui::gHintsOver != 0.f) {
                        std::printf("FAIL ACID's hints: page %d stop %d, A %s: %.0f pixels over\n", page, cursor, held ? "held" : "up", static_cast<double>(ui::gHintsOver));
                        ++failures;
                    }
                    if (!held && page == 0) {
                        const bool macro = cursor >= SynthScreen::kFirstMacro && cursor < SynthScreen::kAdvanced;
                        CHECK(!hintKeyed(canvas, scratch, "Y", "new sound"));
                        if (macro) CHECK(hintKeyed(canvas, scratch, "HOLD X", "mod"));
                    }
                    if (held) rig->key(A, false);
                    rig->frames(1);
                }
        }
    CHECK(states == 2 * 2 * (SynthScreen::kMainStops + acid::kSettings));
}

// The roll on an ACID track marks an accent (a note at 100%): a small brass corner at the block's top
// right, drawn off the screen in BROWN and PAPER, and nothing on a placed note (90%) or on another synth.
void testAcidsAccentMark() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Fonts& fonts = *canvas.fonts;
    std::unique_ptr<Rig> rig(new Rig);
    Session& session = rig->app.session();
    CHECK(session.addTrack(TrackKind::Acid) && session.addTrack(TrackKind::Physic));
    for (int t : {1, 2})
        for (bool ratchet : {false, true}) {
            rig->project().track = t;
            session.view = View::Pattern;
            NotePattern& p = rig->project().tracks[t].notes[0][0];
            p = NotePattern();
            p.notes[0] = NoteEvent{3, 60, 1.f, 0.f, 0.9f};
            p.notes[0].ratchet = ratchet ? 2 : 0;
            session.sendNoteBar(t, 0, 0);
            rig->app.notes().moveCursorTo(62, 10);
            rig->frames(1);
            for (int theme : {kThemeBrown, kThemeSolarized}) {
                session.setTheme(theme);
                p.notes[0].velocity = 0.9f;
                rig->frames(1);
                canvas.clear();
                rig->app.draw(canvas.r, fonts);
                const std::vector<Uint32> plain = canvas.all();
                p.notes[0].velocity = 1.f;
                rig->frames(1);
                canvas.clear();
                rig->app.draw(canvas.r, fonts);
                const std::vector<Uint32> accented = canvas.all();
                int brass = 0, other = 0;
                for (std::size_t i = 0; i < plain.size(); ++i) {
                    // (the block itself is filled as far as it is loud: only what turned brass is the mark)
                    const int y = static_cast<int>(i) / ui::kScreenW;
                    if (y >= ui::kScreenH - 70) continue; // (the strip under the roll says the level: 90% / 100%)
                    if (accented[i] == Canvas::colour(ui::kBrass) && plain[i] != Canvas::colour(ui::kBrass)) ++brass;
                    if (plain[i] == Canvas::colour(ui::kBrass) && accented[i] != Canvas::colour(ui::kBrass)) ++other;
                    if (plain[i] == Canvas::colour(ui::kPurple) && accented[i] != Canvas::colour(ui::kPurple)) ++other; // (a ratchet's purple edge stays whole)
                }
                if (brass != (t == 1 ? 21 : 0)) std::printf("  accent mark: track %d ratchet %d theme %d: %d brass, %d other\n", t, ratchet ? 1 : 0, theme, brass, other);
                if (t == 1) CHECK(brass == 21 && other == 0); // a corner of 6 across
                else CHECK(brass == 0 && other == 0);
            }
        }
    session.setTheme(kThemeBrown);
}

// A 16-step line written on the roll with the buttons, counted (the plan's section 5): A places (held,
// RIGHT once makes it an accent), hold X + UP twice is a slide of half a step, the D-pad walks. Then the
// line plays: its slides tie, its accents are accents.
void testAnAcidLine() {
    std::unique_ptr<Rig> rig(new Rig);
    Session& session = rig->app.session();
    CHECK(session.addTrack(TrackKind::Acid));
    rig->project().track = 1;
    session.view = View::Pattern;
    rig->frames(2);
    NoteScreen& roll = rig->app.notes();
    roll.moveCursorTo(36, 0);
    const int keys[16] = {36, 36, 48, 36, 39, 36, 51, 43, 36, 48, 36, 46, 36, 34, 41, 36};
    int presses = 0, pitch = 36;
    for (int s = 0; s < 16; ++s) {
        if (s > 0) rig->tap(DOWN), ++presses;
        while (pitch != keys[s]) {
            rig->tap(keys[s] > pitch ? RIGHT : LEFT), ++presses;
            pitch += keys[s] > pitch ? 1 : -1;
        }
        rig->key(A, true), ++presses; // places
        if (s % 4 == 0) rig->tap(RIGHT), ++presses; // 90% > 100%: an accent
        rig->key(A, false);
        if (s % 4 == 2) { // a slide: GLIDE OFF > 1/4 > 1/2
            rig->key(X, true), ++presses;
            rig->tap(UP, 2), presses += 2;
            rig->key(X, false);
        }
        rig->frames(2);
    }
    const NotePattern& p = rig->project().tracks[1].notes[0][0];
    int notes = 0, accents = 0, slides = 0;
    for (const NoteEvent& n : p.notes) {
        if (n.velocity <= 0.f) continue;
        ++notes;
        CHECK(n.pitch == keys[n.step] && n.length == 1.f);
        accents += n.velocity >= acid::kAccentAt;
        slides += n.glide == kGlideHalf;
        CHECK((n.velocity >= acid::kAccentAt) == (n.step % 4 == 0) && (n.glide == kGlideHalf) == (n.step % 4 == 2));
    }
    CHECK(notes == 16 && accents == 4 && slides == 4);
    std::printf("  an ACID line of 16 steps, 4 accents and 4 slides: %d presses (%d for the notes and the walk)\n", presses, presses - 4 - 12);
    // Played: the four slides tie (notes a step long, end to end), the twelve others strike.
    rig->render(48000); // (what placing them let be heard rings out)
    const Acid* acid = dynamic_cast<const Acid*>(rig->engine->synthForTests(1));
    CHECK(acid != nullptr);
    if (!acid) return;
    const long ties = acid->ties(), strikes = acid->strikes(); // (each note placed was heard)
    session.togglePlay(false);
    rig->frames(2);
    rig->render(16 * 6000 - 100); // one bar, not its first note again
    if (acid->ties() - ties != 4 || acid->strikes() - strikes != 12) std::printf("  played: %ld ties, %ld strikes\n", acid->ties() - ties, acid->strikes() - strikes);
    CHECK(acid->ties() - ties == 4 && acid->strikes() - strikes == 12);
}

// ---- gesture audit, batch A5: going round, the headings, SONG's cell; the stick ------------------

// G35: SET's rows go round on a PRESS of the D-pad at either end; a held D-pad or the stick stops there.
void testTheEndsOfSetsRows() {
    using S = SettingsScreen;
    Rig rig;
    S& setup = rig.app.settings();
    rig.app.session().view = View::Settings;
    setup.goTo(S::kTempo);
    rig.tap(UP);
    CHECK(setup.cursorRow() == S::kControls && setup.shown(S::kControls));
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kTempo && setup.firstShown() == 0);
    // A held D-pad runs at the list's pace and stops at the end, however long it is held.
    rig.key(DOWN, true);
    rig.frames(300);
    rig.key(DOWN, false);
    CHECK(setup.cursorRow() == S::kControls);
    rig.key(UP, true);
    rig.frames(300);
    rig.key(UP, false);
    CHECK(setup.cursorRow() == S::kTempo);
    // The stick is a run too: it never goes round.
    rig.key(STICK_UP, true);
    rig.frames(30);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(setup.cursorRow() == S::kTempo);
    setup.goTo(S::kControls);
    rig.key(STICK_DOWN, true);
    rig.frames(30);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(setup.cursorRow() == S::kControls);
}

// G28: on SET, L1 / R1 go to the first row of the heading before / after, round the ends. Typing a name
// keeps them; a hold of A on CONTROLS lets go; the panic puts the row back; the bar says so and fits.
void testSetJumpsHeadings() {
    using S = SettingsScreen;
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    Rig rig;
    S& setup = rig.app.settings();
    rig.app.session().view = View::Settings;
    const int firsts[S::kGroups] = {S::kTempo, S::kClick, S::kTextSize, S::kVolume, S::kWifi};
    for (int i = 1; i <= S::kGroups; ++i) {
        rig.tap(R1);
        CHECK(setup.cursorRow() == firsts[i % S::kGroups] && setup.shown(setup.cursorRow()));
    }
    // L1: the heading BEFORE the cursor's, from anywhere in its group; round the start.
    setup.goTo(S::kTheme);
    rig.tap(L1);
    CHECK(setup.cursorRow() == S::kClick);
    rig.tap(L1);
    CHECK(setup.cursorRow() == S::kTempo);
    rig.tap(L1);
    CHECK(setup.cursorRow() == S::kWifi && setup.shown(S::kWifi));
    setup.goTo(S::kStick); // the last of HANDHELD: R1 is SETUP's first, not a row down
    rig.tap(R1);
    CHECK(setup.cursorRow() == S::kWifi);
    rig.tap(R1);
    CHECK(setup.cursorRow() == S::kTempo && setup.firstShown() == 0);
    // The manual's example: THEME is three presses from TEMPO (R1, R1, DOWN; eleven DOWNs before), CONTROLS one (UP).
    rig.tap(R1, 2);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kTheme);
    setup.goTo(S::kTempo);
    rig.tap(UP);
    CHECK(setup.cursorRow() == S::kControls);
    // A held on CONTROLS sets the controls up again at a second: R1 on the way lets go of it, as a move does.
    setup.goTo(S::kControls);
    rig.key(A, true);
    rig.frames(20);
    rig.tap(R1);
    rig.frames(60);
    rig.key(A, false);
    rig.frames(1);
    CHECK(!setup.takeRelearn() && setup.cursorRow() == S::kTempo);
    // Typing a track's name: every button is the keys', L1 / R1 too.
    setup.goTo(S::kTrackName);
    rig.tap(A);
    CHECK(setup.typing());
    rig.tap(R1);
    rig.tap(L1);
    CHECK(setup.typing() && setup.cursorRow() == S::kTrackName);
    rig.tap(B);
    CHECK(!setup.typing());
    // The panic: L1 first jumped a heading; all four shoulders put the row back (and the tab).
    setup.goTo(S::kKey);
    rig.key(L1, true);
    CHECK(setup.cursorRow() == S::kWifi);
    rig.key(L2, true);
    rig.key(R1, true);
    rig.key(R2, true);
    CHECK(rig.view() == View::Settings && setup.cursorRow() == S::kKey);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.frames(1);
    rig.tap(X); // (the first press puts the panic's notice away)
    // Every row's bar says L1 / R1 and holds all of its hints.
    if (!canvas.ok || !scratch.ok) return;
    for (int row = 0; row < S::kRows; ++row) {
        if (!setup.here(row)) continue;
        setup.goTo(row);
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        if (ui::gHintsOver != 0.f) std::printf("FAIL SET's row %d: the hints are %.1f over\n", row, static_cast<double>(ui::gHintsOver));
        CHECK(ui::gHintsOver == 0.f && hintKeyed(canvas, scratch, "L1 / R1", "page"));
    }
}

// The stick (owner, 2026-11-25: "I also wanted the stick to be fast in comparison to the d-pad. As soon as you move
// it, it goes as fast as you set the speed to. Stick for macro moves and d-pad for detailed ones."; it replaced
// gesture audit G07's climb). However far it is pushed past the dead zone it runs at its full rate at once: twice
// the held D-pad's pace for the same thing, times STICK SPEED (50 a second on a cursor at 75%, 13.6 on a list); at
// 50% it is the D-pad's own pace. A slight drift off one axis is not the other; a real diagonal moves both.
void testTheStickIsTwiceTheDpad() {
    using S = SettingsScreen;
    using ui::Pace;
    // The rule.
    {
        CHECK(ui::kStickSpeedAsBuilt == 0.75f);
        CHECK(near(ui::stickPerSecond(Pace::Cursor) * 0.75f, 50.f) && near(ui::stickPerSecond(Pace::List) * 0.75f, 1500.f / 110.f) &&
              near(ui::stickPerSecond(Pace::Click) * 0.75f, 12.f));
        for (Pace pace : {Pace::Cursor, Pace::List, Pace::Click}) CHECK(near(ui::stickPerSecond(pace) * 0.5f, 1000.f / static_cast<float>(ui::paceEveryMs(pace))));
        CHECK(ui::stickWay(0.0625f) == 1.f && ui::stickWay(-1.f) == -1.f && ui::stickWay(0.f) == 0.f);
        // Which axes count: the larger always, the smaller from half of it.
        const ui::StickAxes drift = ui::stickAxes(1.f, 0.49f), diagonal = ui::stickAxes(-1.f, 0.5f), up = ui::stickAxes(0.3f, -0.7f), none = ui::stickAxes(0.f, 0.f);
        CHECK(drift.x == 1.f && drift.y == 0.f);
        CHECK(diagonal.x == -1.f && diagonal.y == 0.5f);
        CHECK(up.x == 0.f && up.y == -0.7f);
        CHECK(none.x == 0.f && none.y == 0.f);
        CHECK(ui::stickAxes(0.4f, 0.8f).x == 0.4f && ui::stickAxes(0.39f, 0.8f).x == 0.f); // half exactly counts
    }
    // Every screen's stick cursor and list, each from its own call.
    const float cursor = 2000.f / 30.f, list = 2000.f / 110.f; // twice the held D-pad's pace (ui.hpp), at STICK SPEED 100%
    struct Runner {
        const char* name;
        std::function<void(Rig&)> open;  // the screen, the cursor on its first stop
        std::function<int(Rig&)> where;  // the cursor's place along the way it is pushed (right or down)
        int ring;                        // the row goes round after this many (0: its ends stop)
        int room;                        // ...or how many moves until an end stops it
        bool sideways;
        float perSecond;                 // the stick's full rate at 100%
        SDL_Keycode dpad;                // the D-pad the same way
        std::function<int(Rig&)> across; // where a screen reads both axes: the place along the other (else none)
        int acrossRing;
        int diagonalFrames = 3;          // ...and how long a diagonal is pushed there without reaching an end
    };
    const auto device = [](Rig& rig, int track) {
        rig.project().track = track;
        rig.app.session().view = View::Device;
    };
    const auto synthRow = [](int c) { return c < SynthScreen::kFirstMacro ? 0 : c == SynthScreen::kAdvanced ? 3 : 1 + (c - SynthScreen::kFirstMacro) / 4; };
    const auto setRows = [] {
        Rig rig;
        return rig.app.settings().places() - 1 - rig.app.settings().placeOf(S::kTempo);
    }();
    const std::vector<Runner> runners = {
        {"SET's rows", [](Rig& rig) { rig.app.session().view = View::Settings; rig.app.settings().goTo(S::kTempo); },
         [](Rig& rig) { return rig.app.settings().placeOf(rig.app.settings().cursorRow()); }, 0, setRows, false, list, DOWN, nullptr, 0},
        {"SONG's cells", [](Rig& rig) { for (int i = 0; i < 3; ++i) rig.app.session().addTrack(TrackKind::Drums); rig.app.session().view = View::Song; rig.app.song().moveCursorTo(0, 0); },
         [](Rig& rig) { return rig.app.song().cursorScene(); }, 0, kMaxScenes - 1, false, cursor, DOWN, [](Rig& rig) { return rig.app.song().cursorColumn(); }, 0},
        {"the drum grid", [](Rig& rig) { rig.app.session().view = View::Pattern; rig.app.drums().moveCursorTo(0, 0); },
         [](Rig& rig) { return rig.app.drums().cursorStep() + 1; }, kSteps + 1, 0, true, cursor, RIGHT, [](Rig& rig) { return rig.app.drums().cursorLane(); }, 0},
        {"the roll", [](Rig& rig) { addPhazed(rig); rig.project().track = 1; rig.app.session().view = View::Pattern; rig.app.notes().moveCursorTo(24, 0); },
         [](Rig& rig) { return rig.app.notes().cursorPitch(); }, 0, NoteScreen::kHighest - 24, true, cursor, RIGHT, [](Rig& rig) { return rig.app.notes().cursorStep(); }, 0},
        {"FX's lines", [](Rig& rig) { rig.app.session().view = View::Effects; rig.app.fx().goTo(0, 0); },
         [](Rig& rig) { return rig.app.fx().cursorRow(); }, FxScreen::kRows, 0, false, cursor, DOWN, nullptr, 0},
        {"MOD's lines", [](Rig& rig) { rig.app.session().view = View::Mods; rig.app.mods().goTo(0, 0); },
         [](Rig& rig) { return rig.app.mods().cursorRow(); }, ModScreen::kRows, 0, false, cursor, DOWN, nullptr, 0},
        {"the sampler's stops", [&](Rig& rig) { rig.app.session().addTrack(TrackKind::Sampler); device(rig, 1); rig.app.sampler().goTo(0); },
         [](Rig& rig) { return rig.app.sampler().cursor(); }, 0, SamplerScreen::kPresetStop, true, cursor, RIGHT, nullptr, 0},
        {"a synth's stops", [&](Rig& rig) { addPhazed(rig); device(rig, 1); rig.app.phazed().goTo(0, SynthScreen::kFirstMacro); },
         [](Rig& rig) { return rig.app.phazed().cursor() - SynthScreen::kFirstMacro; }, 4, 0, true, cursor, RIGHT,
         [&](Rig& rig) { return synthRow(rig.app.phazed().cursor()); }, 4, 2}, // (two rows of macros: down twice is ADVANCED)
        {"a synth's MAIN, down", [&](Rig& rig) { addPhazed(rig); device(rig, 1); rig.app.phazed().goTo(0, SynthScreen::kFirstMacro); },
         [&](Rig& rig) { return synthRow(rig.app.phazed().cursor()); }, 4, 0, false, cursor, DOWN, nullptr, 0},
        {"the kit's lanes", [&](Rig& rig) { device(rig, 0); rig.app.kit().goTo(0, KitScreen::kSound); },
         [](Rig& rig) { return rig.app.kit().cursorLane(); }, kDrumLanes, 0, false, cursor, DOWN, nullptr, 0},
        {"EDIT's boxes", [&](Rig& rig) { device(rig, 0); rig.app.kit().goTo(0, KitScreen::kEdit); rig.app.kit().openEdit(0); },
         [](Rig& rig) { return rig.app.kit().editRow(); }, 0, 3, true, cursor, RIGHT, nullptr, 0},
        {"VOICE's boxes", [&](Rig& rig) { device(rig, 0); rig.project().tracks[0].laneSynth[0].model = drumsynth::kKick; rig.app.kit().goTo(0, KitScreen::kSound); rig.app.kit().openVoice(drumsynth::kTune); },
         [](Rig& rig) { return rig.app.kit().voicePlace(); }, 0, 3, true, cursor, RIGHT, nullptr, 0},
        {"the name keys", [](Rig& rig) { rig.app.session().view = View::Settings; rig.app.session().wantTrackName = true; rig.frames(1); },
         [](Rig& rig) { return rig.app.settings().keyAt() % NameKeys::kColumns; }, NameKeys::kColumns, 0, true, list, RIGHT, nullptr, 0},
    };
    // What the stick moves in `frames` frames at `speed`, `perSecond` at 100% (and its ends, `room`): one at once, the rest
    // counted as every screen counts them (seconds of 16 ms frames, times STICK SPEED).
    const auto expected = [](float perSecond, float speed, int frames, int room) {
        const float dt = 0.016f * speed;
        float saved = 1.f;
        int moves = 0;
        for (int f = 0; f < frames; ++f) {
            if (f > 0) saved += perSecond * dt;
            for (; saved >= 1.f; saved -= 1.f) ++moves;
        }
        return room > 0 && moves > room ? room : moves;
    };
    // The most frames, up to `most`, that do not reach an end that stops.
    const auto within = [&](const Runner& r, float speed, int most) {
        int frames = most;
        while (r.room > 0 && frames > 1 && expected(r.perSecond, speed, frames, 0) > r.room) --frames;
        return frames;
    };
    // The moves along (and across), frame by frame, from a fresh rig: the stick pushed `along` and `across` (raw, before
    // the dead zone), or the D-pad held.
    const auto run = [](const Runner& r, float along, float across, float speed, int frames, bool dpad, std::vector<int>* each = nullptr) {
        ui::gStickSpeed = speed;
        Rig rig;
        r.open(rig);
        rig.frames(2);
        int was = r.where(rig), wasAcross = r.across ? r.across(rig) : 0;
        std::pair<int, int> moved{0, 0};
        const auto count = [&] {
            const int now = r.where(rig);
            moved.first += r.ring > 0 ? ((now - was) % r.ring + r.ring) % r.ring : now - was;
            was = now;
            if (r.across) {
                const int there = r.across(rig);
                moved.second += r.acrossRing > 0 ? ((there - wasAcross) % r.acrossRing + r.acrossRing) % r.acrossRing : there - wasAcross;
                wasAcross = there;
            }
            if (each) each->push_back(moved.first);
        };
        if (dpad) rig.key(r.dpad, true);
        else rig.stick(r.sideways ? along : across, r.sideways ? across : along);
        count();
        for (int f = 0; f < frames; ++f) {
            rig.frames(1);
            count();
        }
        if (dpad) rig.key(r.dpad, false);
        else rig.stick(0.f, 0.f);
        ui::gStickSpeed = 1.f;
        return moved;
    };
    for (const Runner& r : runners) {
        // A push just past the dead zone (raw 0.25: 0.0625 of the way) moves as far as a full push, at the full rate at once.
        const int frames = within(r, 0.75f, 62);
        const int full = run(r, 1.f, 0.f, 0.75f, frames, false).first, part = run(r, 0.25f, 0.f, 0.75f, frames, false).first;
        const int want = expected(r.perSecond, 0.75f, frames, r.room), dpad = run(r, 0.f, 0.f, 0.75f, frames, true).first;
        std::printf("  the stick at 75%% on %s, %d frames: full %d, just past the dead zone %d (%d wanted); the D-pad held %d\n", r.name, frames, full, part, want, dpad);
        if (full != want || part != want) std::printf("FAIL %s: the stick at 75%%\n", r.name);
        CHECK(full == want && part == want);
        // At 75% further than a held D-pad on the same thing.
        if (full <= dpad) std::printf("FAIL %s: the stick is not faster than the D-pad\n", r.name);
        CHECK(full > dpad);
        // STICK SPEED scales it: 25% and 150%.
        for (float speed : {0.25f, 1.5f}) {
            const int f = within(r, speed, 62), got = run(r, 1.f, 0.f, speed, f, false).first, wanted = expected(r.perSecond, speed, f, r.room);
            if (got != wanted) std::printf("FAIL %s at %.0f%%: %d moves in %d frames, %d wanted\n", r.name, static_cast<double>(speed * 100.f), got, f, wanted);
            CHECK(got == wanted);
        }
        // At 50% the stick is the D-pad's own pace, to the move: over 960 ms of a cursor (32 moves) or 880 ms of a list
        // (8), each counted once it runs steadily. (Where an end would stop it first, the count is not steady.)
        const int window = r.perSecond == cursor ? 60 : 55, from = 20;
        if (r.room == 0 || expected(r.perSecond, 0.5f, from + window, 0) <= r.room) {
            std::vector<int> stick, held;
            run(r, 1.f, 0.f, 0.5f, from + window, false, &stick);
            run(r, 0.f, 0.f, 0.5f, from + window, true, &held);
            const int byStick = stick[static_cast<std::size_t>(from + window)] - stick[static_cast<std::size_t>(from)];
            const int byDpad = held[static_cast<std::size_t>(from + window)] - held[static_cast<std::size_t>(from)];
            if (byStick != byDpad || byDpad != (window == 60 ? 32 : 8)) std::printf("FAIL %s at 50%%: the stick %d, the D-pad %d\n", r.name, byStick, byDpad);
            CHECK(byStick == byDpad && byDpad == (window == 60 ? 32 : 8));
        }
        // Both axes read: a drift smaller than half the push moves nothing the other way; a real diagonal moves both.
        if (r.across) {
            const int f = r.diagonalFrames, both = expected(cursor, 0.75f, f, 0); // (3 frames: 1 + 1)
            const std::pair<int, int> drift = run(r, 1.f, 0.5f, 0.75f, f, false), diagonal = run(r, 1.f, 0.7f, 0.75f, f, false);
            if (drift.second != 0 || diagonal.second != both || diagonal.first != both) std::printf("FAIL %s: a drift moved across %d, a diagonal %d along and %d across\n", r.name, drift.second, diagonal.first, diagonal.second);
            CHECK(drift.first == both && drift.second == 0); // (raw 0.5 is 0.375 of the way, under half)
            CHECK(diagonal.first == both && diagonal.second == both && both == (f == 3 ? 2 : 1)); // (raw 0.7 is 0.625)
        }
    }
    // Lists step at twice the list's pace, not the cursor's, however far the stick is pushed: the kit's sounds with A
    // held (in 44 frames at 75%, 1 + 9: the old 8 a second gave 1 + 4) and SONG's list of instruments (30 frames: 1 + 6).
    {
        ui::gStickSpeed = 0.75f;
        Rig rig;
        rig.app.session().view = View::Device;
        rig.app.kit().goTo(0, KitScreen::kSound);
        rig.frames(2);
        rig.key(A, true);
        rig.stick(0.f, 0.3f);
        rig.frames(44);
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        ui::gStickSpeed = 1.f;
        CHECK(expected(list, 0.75f, 44, 0) == 10);
        CHECK(rig.project().tracks[0].laneSynth[0].on() && rig.project().tracks[0].laneSynth[0].model == drumsynth::kClave); // the tenth
    }
    {
        ui::gStickSpeed = 0.75f;
        Rig rig;
        rig.app.session().view = View::Song;
        rig.app.song().moveCursorTo(0, 1); // + TRACK
        rig.tap(A);
        CHECK(rig.app.song().choosing());
        rig.stick(0.f, 0.3f);
        rig.frames(30);
        rig.stick(0.f, 0.f);
        rig.frames(1);
        rig.tap(A);
        ui::gStickSpeed = 1.f;
        const int moves = expected(list, 0.75f, 30, 0); // 1 + 6
        CHECK(moves == 7 && rig.project().trackCount() == 2 && rig.project().tracks[1].kind == kInstruments[(1 + moves) % kInstrumentCount].kind);
    }
}

// The stick with a button held (owner, 2026-11-25): a value turned goes at its own rate however far it is pushed;
// what a held D-pad also runs (BARS, the dice, MODEL, the roll's BARS) at twice the D-pad's pace. At STICK SPEED 75%.
void testTheStickWithAButtonHeld() {
    const float speedWas = ui::gStickSpeed;
    ui::gStickSpeed = 0.75f;
    // A held on a hit: its level fills at three a second (times 75%), as fast just past the dead zone as all the way.
    const auto filled = [](float raw) {
        Rig rig;
        rig.app.session().view = View::Pattern;
        rig.app.drums().setStep(0, 0, 0.2f);
        rig.frames(2);
        rig.key(A, true);
        rig.stick(0.f, -raw);
        rig.frames(5);
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        return rig.app.drums().level(0, 0);
    };
    const float part = filled(0.25f), full = filled(1.f);
    CHECK(near(part, full) && near(full, 0.2f + 3.f * 0.012f * 5.f));
    // The drum grid: A on BARS (up from the top lane: the tempo, then BARS) and Y on a lane's name (the dice) step at
    // twice the held D-pad's pace, the cursor's: 0.8 a frame (no step at once: they were turned as fills).
    {
        Rig rig;
        rig.app.session().view = View::Pattern;
        rig.app.drums().moveCursorTo(0, 0);
        rig.frames(2);
        rig.tap(UP, 2);
        rig.key(A, true);
        rig.stick(0.f, -0.3f);
        rig.frames(3); // 2.4: two steps, 1 to 2 to 4 bars (it was 2.25 bars a second: none)
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        CHECK(rig.project().tracks[0].bars[0] == 4);
    }
    {
        Rig rig;
        rig.app.session().view = View::Pattern;
        rig.app.drums().moveCursorTo(2, -1); // lane 3's name, an empty lane
        rig.frames(2);
        rig.key(Y, true);
        rig.stick(0.f, -0.3f);
        rig.frames(3); // 2.4 hits (it was 18 a second: 0.86)
        rig.stick(0.f, 0.f);
        rig.frames(1);
        int hits = 0;
        for (int step = 0; step < kSteps; ++step) hits += rig.app.drums().level(2, step) > 0.f ? 1 : 0;
        rig.key(Y, false);
        CHECK(hits == 2);
    }
    // The roll: A on BARS runs (it was once a push past half): one at once, then 0.8 a frame.
    {
        Rig rig;
        addPhazed(rig);
        rig.project().track = 1;
        rig.app.session().view = View::Pattern;
        rig.app.notes().moveCursorTo(60, 0);
        rig.frames(2);
        rig.tap(UP); // up from the top step: BARS
        rig.key(A, true);
        rig.stick(0.f, -0.3f); // just past the dead zone (it was under half: nothing)
        rig.frames(1);
        CHECK(rig.project().tracks[1].bars[0] == 2);
        rig.frames(2); // 1.6 more: one
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        CHECK(rig.project().tracks[1].bars[0] == 4);
    }
    // VOICE's MODEL with A held, a list: one at once, then twice the held D-pad's list pace (13.6 a second); it was
    // once a push past half.
    {
        Rig rig;
        rig.app.session().view = View::Device;
        rig.project().tracks[0].laneSynth[0] = drumsynth::usualVoice(drumsynth::kKick);
        rig.app.kit().goTo(0, KitScreen::kSound);
        rig.app.kit().openVoice();
        rig.frames(2);
        CHECK(rig.app.kit().voicing() && rig.app.kit().voicePlace() == KitScreen::kVoiceModel);
        rig.key(A, true);
        rig.stick(0.f, -0.3f);
        rig.frames(10); // 1 + 1.96
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        CHECK(rig.project().tracks[0].laneSynth[0].model == drumsynth::kTom);
    }
    ui::gStickSpeed = speedWas;
}

// Held keys (gesture audit G08, G32, 2026-11-25): one repeat for every screen, ui::Repeat, each counted from when
// the one before was DUE; each screen names its pace (a cursor, a list, a click).
void testHeldKeysRunAtTheirPace() {
    using ui::Pace;
    // The rule.
    {
        ui::Repeat repeat;
        Uint32 t = 1000;
        CHECK(!repeat.due(t, true, Pace::Cursor)); // the first ask starts the wait
        CHECK(!repeat.due(t + ui::kCursorDelayMs - 1, true, Pace::Cursor) && repeat.due(t + ui::kCursorDelayMs, true, Pace::Cursor));
        // A second of 16 ms frames at the cursor's pace: 27 after the wait, one each 30 ms. Counted from the frame
        // that saw each, a repeat waited two frames (32 ms): 26.
        repeat.restart();
        int runs = 0;
        for (int frame = 0; frame < 62; ++frame) {
            t += 16;
            while (repeat.due(t, true, Pace::Cursor)) ++runs;
        }
        CHECK(runs == 1 + static_cast<int>((61 * 16 - ui::kCursorDelayMs) / ui::kCursorEveryMs) && runs == 27);
        // Let go: nothing, however long.
        CHECK(!repeat.due(t + 500, false, Pace::Cursor));
        // A frame held up (here 400 ms): one repeat, not the dozen it missed.
        t += 400;
        runs = 0;
        while (repeat.due(t, true, Pace::Cursor)) ++runs;
        CHECK(runs == 1);
        t += 16;
        CHECK(!repeat.due(t, true, Pace::Cursor)); // ...and the next 30 ms on, from that frame
        CHECK(repeat.due(t + 14, true, Pace::Cursor));
        // A pace faster than the frames: several in one frame (the list of recordings' 20 ms, the far end).
        repeat.restart();
        CHECK(!repeat.due(t, true, 0, 5));
        runs = 0;
        while (repeat.due(t + 16, true, 0, 5)) ++runs;
        CHECK(runs == 4);
        // The paces, and the third for clicks: none slower than it was (8 a second, after the cursor's wait).
        CHECK(ui::paceDelayMs(Pace::Cursor) == 170 && ui::paceEveryMs(Pace::Cursor) == 30 && ui::paceDelayMs(Pace::List) == 200 && ui::paceEveryMs(Pace::List) == 110);
        CHECK(ui::paceDelayMs(Pace::Click) == 170 && ui::paceEveryMs(Pace::Click) == 125 && ui::stickPerSecond(Pace::Click) == 16.f);
        // Waits as one, runs as another.
        repeat.restart();
        CHECK(!repeat.due(t, true, Pace::Cursor, Pace::List) && repeat.due(t + 170, true, Pace::Cursor, Pace::List));
        CHECK(!repeat.due(t + 279, true, Pace::Cursor, Pace::List) && repeat.due(t + 280, true, Pace::Cursor, Pace::List));
    }
    // The roll's keys held a second: the press and 27 (it was 25 and the press).
    {
        Rig rig;
        addPhazed(rig);
        rig.project().track = 1;
        rig.app.session().view = View::Pattern;
        rig.app.notes().moveCursorTo(24, 0);
        rig.frames(2);
        rig.key(RIGHT, true);
        rig.frames(62);
        rig.key(RIGHT, false);
        CHECK(rig.app.notes().cursorPitch() == 24 + 1 + 27);
    }
    // The roll with X held on a note: the ratchet clicks at the clicks' pace (G32: it was 220 ms, after 200).
    {
        Rig rig;
        rig.app.session().addTrack(TrackKind::Fm6);
        rig.project().track = 1;
        rig.frames(2);
        NoteScreen& roll = rig.app.notes();
        NotePattern& p = rig.project().tracks[1].notes[0][0];
        roll.moveCursorTo(60, 4);
        rig.tap(A);
        const int slot = p.at(60, 4);
        rig.key(X, true);
        rig.frames(1);
        rig.key(RIGHT, true);
        rig.frames(20); // 320 ms: the press, and two clicks after the 170 wait (at 220 after 200: one)
        rig.key(RIGHT, false);
        CHECK(slot >= 0 && p.notes[slot].ratchet == 4 && roll.cursorPitch() == 60);
        // The stick the same (it was four a second, then eight): a click at once, then twice the held D-pad's clicks,
        // sixteen a second (STICK SPEED 100% here).
        rig.key(STICK_UP, true);
        rig.frames(10); // the click at once, and 9 frames at 16 a second: two more (at eight: one)
        rig.key(STICK_UP, false);
        rig.key(X, false);
        rig.frames(1);
        CHECK(slot >= 0 && p.notes[slot].glide == glideAt(3) && roll.cursorPitch() == 60);
    }
    // SONG's list of instruments runs when held, at the list's pace (it did not run at all): 20 frames is the press
    // and one more.
    {
        Rig rig;
        rig.app.session().view = View::Song;
        rig.app.song().moveCursorTo(0, 1); // + TRACK: the list opens on PHAZED, the second
        rig.tap(A);
        CHECK(rig.app.song().choosing());
        rig.key(DOWN, true);
        rig.frames(20);
        rig.key(DOWN, false);
        rig.frames(10);
        rig.tap(A);
        CHECK(rig.project().trackCount() == 2 && rig.project().tracks[1].kind == kInstruments[(1 + 2) % kInstrumentCount].kind);
        // A or B with the D-pad still held closes the list, and the run stops there: it does not go on over the cells.
        for (SDL_Keycode closer : {A, B}) {
            const int tracks = rig.project().trackCount();
            rig.app.song().moveCursorTo(0, tracks); // + TRACK
            rig.tap(A);
            CHECK(rig.app.song().choosing());
            rig.key(DOWN, true);
            rig.tap(closer);
            rig.frames(30);
            rig.key(DOWN, false);
            CHECK(!rig.app.song().choosing() && rig.project().trackCount() == tracks + (closer == A ? 1 : 0) && rig.app.song().cursorScene() == 0);
        }
    }
    // The sampler's LOOP with A held clicks once a push of the stick, however far: up ON, down OFF (it was turned as
    // a range of one: 1.3 s of push to flip it).
    {
        Rig rig;
        rig.app.session().addTrack(TrackKind::Sampler);
        rig.project().track = 1;
        rig.app.session().view = View::Device;
        int loopStop = -1;
        for (int place = 0; place < sampler::kSettings; ++place)
            if (SamplerScreen::settingAt(place) == sampler::Loop) loopStop = place + 1;
        rig.app.sampler().goTo(loopStop);
        rig.frames(2);
        const auto loop = [&] { return rig.project().tracks[1].sampler[sampler::Loop]; };
        const int was = loop();
        rig.key(A, true);
        rig.frames(1);
        rig.stick(0.f, was ? 0.7f : -0.7f); // a push past half (0.62 past the dead zone), away from what it is: it clicks at once
        rig.frames(1);
        CHECK(loop() == (was ? 0 : 1));
        rig.frames(60); // held there: nothing more
        CHECK(loop() == (was ? 0 : 1));
        rig.stick(0.f, was ? -1.f : 1.f); // straight across the middle the other way: a push, a click
        rig.frames(1);
        CHECK(loop() == was);
        rig.stick(0.f, 0.f);
        rig.frames(1);
        rig.stick(0.f, was ? 0.25f : -0.25f); // just past the dead zone (0.06): a push, a click (owner, 2026-11-25: it was past half)
        rig.frames(1);
        CHECK(loop() == (was ? 0 : 1));
        rig.stick(0.f, 0.f);
        rig.frames(1);
        rig.stick(was ? 1.f : -1.f, was ? -0.5f : 0.5f); // a sideways push that drifts back (0.37, under half of it): nothing
        rig.frames(30);
        CHECK(loop() == (was ? 0 : 1));
        rig.stick(0.f, 0.f);
        rig.key(A, false);
        rig.frames(1);
        CHECK(rig.app.sampler().cursor() == loopStop); // the cursor did not move
    }
}

// G46: SONG opens with its cursor on the cell being worked on; a tab off SONG takes the cell the cursor was
// moved to on this visit (bar 1), and nothing else: a glance, the same cell (its bar kept), a head.
void testSongOpensOnTheCellWorkedOn() {
    Rig rig;
    Session& session = rig.app.session();
    SongScreen& song = rig.app.song();
    Project& p = rig.project();
    CHECK(session.addTrack(TrackKind::Phazed) && session.addTrack(TrackKind::Phazed));
    for (int t = 0; t < 3; ++t)
        for (int s = 0; s < 3; ++s) p.tracks[t].bars[s] = 2;
    p.track = 2, p.scene = 1, p.bar = 1;
    rig.tap(L2);
    CHECK(rig.view() == View::Song && song.cursorScene() == 1 && song.cursorColumn() == 2 && !song.cursorOnHead() && !song.cursorOnMaster());
    rig.tap(R2); // a glance: nothing changes, the bar kept
    CHECK(rig.view() == View::Pattern && p.track == 2 && p.scene == 1 && p.bar == 1);
    rig.tap(L2);
    rig.tap(LEFT);
    rig.tap(DOWN);
    rig.tap(R2); // moved: that cell, from its first bar
    CHECK(rig.view() == View::Pattern && p.track == 1 && p.scene == 2 && p.bar == 0);
    // Along the heads and down is a move too (no cell was passed on the way).
    p.track = 1, p.scene = 0;
    rig.tap(L2);
    rig.tap(UP);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    rig.tap(LEFT);
    rig.tap(DOWN);
    CHECK(!song.cursorOnHead() && song.cursorColumn() == 0 && song.cursorScene() == 0);
    rig.tap(R2);
    CHECK(p.track == 0 && p.scene == 0 && p.bar == 0);
    p.track = 1, p.scene = 2;
    p.bar = 1;
    rig.tap(L2);
    CHECK(song.cursorScene() == 2 && song.cursorColumn() == 1);
    rig.tap(RIGHT);
    rig.tap(LEFT);
    rig.tap(R2); // away and back to the same cell: its bar kept
    CHECK(p.track == 1 && p.scene == 2 && p.bar == 1);
    // START on another scene's cell plays from there and makes it the scene worked on; the cursor brought
    // back to the cell it opened on is still a move this visit, and R2 opens that cell (a bar 1: it changed).
    rig.tap(L2);
    rig.tap(UP);
    rig.tap(START);
    CHECK(p.playing && p.scene == 1);
    rig.tap(DOWN);
    rig.tap(R2);
    CHECK(p.track == 1 && p.scene == 2 && p.bar == 0);
    session.togglePlay(false);
    rig.frames(1);
    p.bar = 1;
    // Not moved, the cursor takes nothing, even where the work moved elsewhere meanwhile (here by hand).
    rig.tap(L2);
    p.track = 0, p.scene = 0;
    rig.tap(R2);
    CHECK(p.track == 0 && p.scene == 0 && p.bar == 1);
    p.track = 1, p.scene = 2;
    // A trip to a head to mute: nothing is taken (nor on REPEATS).
    rig.tap(L2);
    rig.tap(UP, 3);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    rig.tap(Y);
    rig.tap(R2);
    CHECK(p.tracks[1].muted && p.track == 1 && p.scene == 2 && p.bar == 1);
    rig.tap(L2);
    song.moveCursorTo(0, -1);
    rig.tap(R2);
    CHECK(p.track == 1 && p.scene == 2 && p.bar == 1);
    // L2 off SONG goes round to SET, and takes the cell too (its TRACK NAME is that track's).
    rig.tap(L2);
    song.moveCursorTo(0, 0);
    rig.tap(L2);
    CHECK(rig.view() == View::Settings && p.track == 0 && p.scene == 0 && p.bar == 0);
    // FX steps the track (L1 / R1): a look at SONG and back keeps that track.
    session.view = View::Effects;
    rig.tap(R1);
    CHECK(p.track == 1);
    rig.tap(L2, 3);
    CHECK(rig.view() == View::Song && song.cursorColumn() == 1 && song.cursorScene() == 0);
    rig.tap(R2, 3);
    CHECK(rig.view() == View::Effects && p.track == 1 && p.scene == 0);
    // SELECT's tap from another tab opens SONG on the cell too.
    song.moveCursorTo(5, 2);
    session.view = View::Device;
    rig.tap(SELECT);
    CHECK(rig.view() == View::Song && song.cursorScene() == 0 && song.cursorColumn() == 1);
    rig.tap(R2);
    CHECK(p.track == 1 && p.scene == 0);
    // L3 opens SONG on MASTER: any cell reached from there is a move.
    rig.tap(L2);
    song.moveCursorTo(2, 2);
    rig.tap(R2);
    CHECK(p.track == 2 && p.scene == 2);
    p.track = 0, p.scene = 0;
    rig.tap(SDLK_3); // L3
    CHECK(rig.view() == View::Song && song.cursorOnMaster());
    song.moveCursorTo(2, 2);
    rig.tap(R2);
    CHECK(p.track == 2 && p.scene == 2 && p.bar == 0);
    // The panic: R2 first took a moved cell (and L2 on the way opened SONG again); all four put it all back.
    p.bar = 1;
    rig.tap(L2);
    song.moveCursorTo(0, 0);
    rig.key(R2, true);
    CHECK(rig.view() == View::Pattern && p.track == 0 && p.scene == 0);
    rig.key(L1, true);
    rig.key(L2, true);
    rig.key(R1, true);
    CHECK(rig.view() == View::Song && p.track == 2 && p.scene == 2 && p.bar == 1 && song.cursorScene() == 0 && song.cursorColumn() == 0);
    for (SDL_Keycode k : {L1, L2, R1, R2}) rig.key(k, false);
    rig.frames(1);
    rig.tap(R2); // the cursor is still where it was moved to on that visit: R2 takes it, as it would have
    CHECK(rig.view() == View::Pattern && p.track == 0 && p.scene == 0 && p.bar == 0);
    // The panic puts SONG's cursor back where SONG reopening on the way moved it from: L2 first (to SET, nothing
    // taken off a head, REPEATS or MASTER), R2 (SONG again, on the cell worked on), L1, R1. On a track's head...
    const auto panicFromSong = [&] {
        for (SDL_Keycode k : {L2, R2, L1, R1}) rig.key(k, true);
        const bool back = rig.view() == View::Song && p.track == 0 && p.scene == 0;
        for (SDL_Keycode k : {L2, R2, L1, R1}) rig.key(k, false);
        rig.frames(1);
        rig.tap(X); // (the first press puts the panic's notice away)
        return back;
    };
    rig.tap(X); // (the panic above left its notice up: the first press puts it away)
    rig.tap(L2);
    rig.tap(RIGHT);
    rig.tap(UP); // up from the top scene: track 2's head
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    CHECK(panicFromSong() && song.cursorOnHead() && song.cursorColumn() == 1);
    // ...on REPEATS a page down (the scenes shown from the 14th)...
    song.moveCursorTo(20, -1);
    CHECK(song.topScene() == 13);
    CHECK(panicFromSong() && song.cursorScene() == 20 && song.cursorColumn() == -1 && song.topScene() == 13 && !song.cursorOnHead());
    // ...and on MASTER.
    song.moveToMaster();
    CHECK(panicFromSong() && song.cursorOnMaster());
}

int main(int argc, char** argv) {
    // The engine's own presets only: the tests here are of their tables. The presets the app's data
    // adds (and any track sound it gives a built-in) are presets_test's.
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Physic, TrackKind::Acid}) useBuiltInDataForTests(kind, PresetData());
    ui::gStickSpeed = 1.f; // the checks below are of each screen's own speeds; the overall scale has its own check
    if (argc > 1 && std::string(argv[1]) == "--bounce") { // just the bouncing (for trying a change; ctest runs everything)
        testBouncing();
        testWhereABounceEnds();
        testABounceRunsUntilTheTailEnds();
        if (failures == 0) std::printf("app tests passed (bouncing only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--jaxep") { // just JAX-EP's own (for trying a change; ctest runs everything)
        testHowJaxepReads();
        testAPresetKeepsTheTracksOwnValues();
        testUndoPutsBackTheTracksOwnTwo();
        testJaxepsHintsFit();
        testFm6sHintsFit();
        std::printf(failures ? "%d check(s) failed\n" : "JAX-EP's tests passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--physic") { // just PHYSIC's own
        testPhysicOnATrack();
        testPhysicsHintsFit();
        std::printf(failures ? "%d check(s) failed\n" : "PHYSIC's tests passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--acid") { // just ACID's own
        testAcidOnATrack();
        testAcidsHintsFit();
        testAcidsAccentMark();
        testAnAcidLine();
        std::printf(failures ? "%d check(s) failed\n" : "ACID's tests passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--morph") { // just MORPH's own
        testMorphOnATrack();
        testMorphsHintsAndPicture();
        std::printf(failures ? "%d check(s) failed\n" : "MORPH's tests passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--cloud") { // just CLOUD's own
        testCloudOnATrack();
        testCloudsHintsFit();
        std::printf(failures ? "%d check(s) failed\n" : "CLOUD's tests passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--undo") { // just undo kept through a resync, a delete, another instrument
        testAResyncKeepsUndo();
        testATrackDeletedOrChangedKeepsUndo();
        testUndoSendsOnlyTheTrackItChanged();
        testUndoWorksOnTheTrackItChanged();
        testNextSceneTakenBackReadsTheStepWhole();
        testUndoIsAsItWasReadWhole();
        testUndoCoversSounds();
        testUndoLeavesWhatTheTextRounds();
        testUndoCoversABusAndASampler();
        if (failures == 0) std::printf("app tests passed (undo kept only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--fx") { // just the FX screen and its list of effects (for trying a change)
        testTheEffectsScreen();
        testTryingAnEffectKeepsIt();
        testFxStepsTheTrack();
        testFxHoldKeptAcrossTracks();
        testBandFreqStepsAThirdOfAnOctave();
        testHoldYMovesABandTwoWays();
        testFxSwitchesType();
        testFxSwitchMends();
        testTheFxScreensList();
        testTheFxScreenAsDrawn();
        testChangingTypeKeepsWhatIsShared();
        testMovingEffectsKeepsEverythingWithThem();
        testFxTextsFit();
        testTheFxScreenHoldsUp();
        testTheFxScreenSecondReview();
        testSongFilesAreAsTheyWere();
        testTheFxScreenShowsCompAtWork();
        testTheFxPictureFollowsTheSettings();
        testEverySettingHasAnIcon();
        if (failures == 0) std::printf("app tests passed (FX only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--keys") { // just solo on SONG and typing a name (owner, 2026-11-15)
        testMuteSoloAndNames();
        testTypingATrackName();
        testNamingATrackFromTheSongScreen();
        testTheHintsFitAndSayWhatTheyDo();
        if (failures == 0) std::printf("app tests passed (keys only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--gestures") { // just SONG's gestures of the gesture audit (for trying a change)
        testDeletingATrack();
        testTheTrackHoldStaysOnItsTrack();
        testStartWithTheSongListsOpen();
        testASceneOfAnyLength();
        testTheHelpCardDrawsSel();
        if (failures == 0) std::printf("app tests passed (gestures only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--setup") { // just the gesture audit's batch A2: the help card, SET's let go, the setup screen
        testSettingsWaitToStandStill();
        testTheHelpCard();
        testSetActsOnTheLetGo();
        testTheSetupScreen();
        testTypingATrackName();
        testBouncing();
        if (failures == 0) std::printf("app tests passed (setup only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--naming") { // just the gesture audit's batch A3: the name keys, deleting presets, the words
        testTypingATrackName();
        testNamingATrackFromTheSongScreen();
        testSavePresetOnDevice();
        testSamplerPresetsOnDevice();
        testDeletingOwnSynthPresets();
        testThePresetKeysHaveEveryButton();
        testAPanicClosesTheKeysItOpened();
        testATabCallsOffAHoldOfB();
        testTheNameKeysRunAtAListsPace();
        testAHeldYDeletesOn();
        testDeletingAPresetSaysItCannotBeUndone();
        testHoldBDeletesAnEffect();
        testTheHintsFitAndSayWhatTheyDo();
        testTheHelpCard();
        if (failures == 0) std::printf("app tests passed (naming only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--pages") { // just the gesture audit's batch A4 here: ADVANCED's place, the sound put back
        testAdvancedKeepsItsPlace();
        testAdvancedPages();
        testAPutBackSoundIsTheEngines();
        if (failures == 0) std::printf("app tests passed (pages only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--navigation") { // just the gesture audit's batch A5 (going round, headings, SONG's cell) and the stick
        testTabs();
        testTheEndsOfSetsRows();
        testSetJumpsHeadings();
        testTheStickIsTwiceTheDpad();
        testTheStickWithAButtonHeld();
        testSongOpensOnTheCellWorkedOn();
        testTheSetupScreen();
        testTheStickSpeedAndTheVolume();
        testBOnASettingIsItsUsual();
        if (failures == 0) std::printf("app tests passed (navigation only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--held") { // just the gesture audit's batch C1: held keys' paces
        testHeldKeysRunAtTheirPace();
        testTheFxScreensList();
        testStepEffects();
        testSettingsWaitToStandStill();
        if (failures == 0) std::printf("app tests passed (held keys only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--holds") { // just the gesture audit's batch C3: held Y, B on SET, the hold lengths, the copy (spent by a paste again)
        testAHeldYDeletesOn();
        testBOnASettingIsItsUsual();
        testTheHoldLengths();
        testHoldBDeletesAnEffect();
        testCopyingABar();
        testTheCellCopyStaysPut();
        if (failures == 0) std::printf("app tests passed (holds only)\n");
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--hands") { // just the button standards of the pit stop
        testHoldBDeletesAnEffect();
        testTheHintsFitAndSayWhatTheyDo();
        testDeletingATrackSaysItCannotBeUndone();
        testChangingAnInstrumentSaysWhatGoes();
        testDeletingASongSaysItCannotBeUndone();
        if (failures == 0) std::printf("app tests passed (hands only)\n");
        return failures == 0 ? 0 : 1;
    }
    testAResyncKeepsUndo();
    testATrackDeletedOrChangedKeepsUndo();
    testUndoSendsOnlyTheTrackItChanged();
    testUndoWorksOnTheTrackItChanged();
    testNextSceneTakenBackReadsTheStepWhole();
    testUndoIsAsItWasReadWhole();
    testUndoCoversSounds();
    testUndoLeavesWhatTheTextRounds();
    testUndoCoversABusAndASampler();
    testHoldBDeletesAnEffect();
    testTheHoldLengths();
    testHoldYMovesAnEffect();
    testTheHintsFitAndSayWhatTheyDo();
    testDeletingATrackSaysItCannotBeUndone();
    testChangingAnInstrumentSaysWhatGoes();
    testDeletingASongSaysItCannotBeUndone();
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
    testTheSpeedTestPlaysTheBusiestSongWithHelpers();
    testANewProjectIsOneDrumTrack();
    testAddingTracks();
    testOpeningACellWorksOnThatScene();
    testRepeatCounts();
    testStartOnTheSongScreenPlaysTheSong();
    testCopyingABar();
    testBEmptiesACell();
    testTheCellCopyStaysPut();
    testTheSongScreenScrolls();
    testPlacingAndDeletingNotes();
    testHeardAsPlaced();
    testNoteLevelAndLength();
    testTheEnginePlaysNotesAtTheirStepAndLength();
    testPaintingNotes();
    testTheKeysScrollAndStopAtTheEnds();
    testCellsOfSeveralBars();
    testStripCountsWithTheGrid();
    testNudgingWithTheOuterShoulderButtons();
    testAFreeLengthReachesTheEngine();
    testAllFourShoulderButtonsIsPanic();
    testL3IsTheMaster();
    testMuteSoloAndNames();
    testHoldBAndDragErases();
    testNothingStaysHeld();
    testDeletingATrack();
    testTheTrackHoldStaysOnItsTrack();
    testStartWithTheSongListsOpen();
    testStepsInAScene();
    testTheBattery();
    testBatteryLog();
    testBouncing();
    testWhereABounceEnds();
    testABounceRunsUntilTheTailEnds();
    testRecordingAutomation();
    testChoicesClick();
    testTheHelpCard();
    testSetActsOnTheLetGo();
    testSettingsWaitToStandStill();
    testTheHelpCardDrawsSel();
    testTheHelpCardSaysTheLoad();
    testQuittingTurnsTheHandheldOff();
    testUndoAndRedo();
    testHoldBErasesDrumHits();
    testFm6OnATrack();
    testAQmodSongOpensOnFm6();
    testPhysicOnATrack();
    testTypingATrackName();
    testHoldXCarriesANote();
    testSelectGoesToTheSongScreen();
    testTheSetupScreen();
    testTheButtonTestAndSettingUpAgain();
    testThePianoRollZooms();
    testTheRollsZoomIsATapOnly();
    testTheStickSpeedAndTheVolume();
    testBOnASettingIsItsUsual();
    testTheEndsOfSetsRows();
    testSetJumpsHeadings();
    testTheStickIsTwiceTheDpad();
    testTheStickWithAButtonHeld();
    testHeldKeysRunAtTheirPace();
    testSongOpensOnTheCellWorkedOn();
    testTheTempoFromAnyScreen();
    testTheEffectsScreen();
    testTryingAnEffectKeepsIt();
    testFxStepsTheTrack();
    testFxHoldKeptAcrossTracks();
    testBandFreqStepsAThirdOfAnOctave();
    testHoldYMovesABandTwoWays();
    testFxSwitchesType();
    testFxSwitchMends();
    testEffectsThroughTheSession();
    testSendsThroughTheSession();
    testTheFxScreensList();
    testTheFxScreenAsDrawn();
    testChangingTypeKeepsWhatIsShared();
    testMovingEffectsKeepsEverythingWithThem();
    testSceneTempoMeterUndo();
    testLaneAndChopUndo();
    testSavePresetMakesNoStep();
    testSetCellBarsNoOpMakesNoStep();
    testAutomationMarkCatchesDirectWrites();
    testKitLoadRestoresRecordings();
    testFxTextsFit();
    testTheFxScreenHoldsUp();
    testTheFxScreenSecondReview();
    testSongFilesAreAsTheyWere();
    testTheFxScreenShowsCompAtWork();
    testTheFxPictureFollowsTheSettings(); // (only --fx ran these two until 2026-11-24)
    testEverySettingHasAnIcon();
    testTheKeysShownFollowTheCursor();
    testTheScreensKnowWhereTheSongIs();
    testAPatternHoldsSixtyFourNotes();
    testPresetsAndFolders();
    testSavePresetOnDevice();
    testSamplerPresetsOnDevice();
    testDeletingOwnSynthPresets();
    testThePresetKeysHaveEveryButton();
    testAPanicClosesTheKeysItOpened();
    testATabCallsOffAHoldOfB();
    testTheNameKeysRunAtAListsPace();
    testAHeldYDeletesOn();
    testDeletingAPresetSaysItCannotBeUndone();
    testHeldAStepsThroughLiveSamples();
    testABuiltInPresetRestsTheMacrosWhereTheyShow();
    testMacros();
    testTheEngineHearsMacrosAndSettings();
    testAdvancedPages();
    testAdvancedKeepsItsPlace();
    testAPutBackSoundIsTheEngines();
    testEveryPatchSettingIsOnSomePage();
    testAddingJaxep();
    testJaxepsDeviceScreen();
    testHowJaxepReads();
    testTheEngineHearsJaxepsSettings();
    testAPresetKeepsTheTracksOwnValues();
    testUndoPutsBackTheTracksOwnTwo();
    testJaxepsHintsFit();
    testFm6sHintsFit();
    testPhysicsHintsFit();
    testAcidOnATrack();
    testAcidsHintsFit();
    testMorphOnATrack();
    testMorphsHintsAndPicture();
    testAcidsAccentMark();
    testAnAcidLine();
    testCloudOnATrack();
    testCloudsHintsFit();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("app tests passed\n");
    return 0;
}
