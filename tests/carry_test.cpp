// R1 held on the roll carries a note or a chord anywhere in the cell (owner, 2026-11-28: "I want to hold R1 and move
// it to wherever I want, not just transpose or shorten/lengthen the notes"; docs/dev/notes-and-mods.md).
//   both       a chord carried by key and by step in one hold, every key and its shape, length, level and effects kept
//   stick      the stick both ways in one push, whole steps, never off the grid; L1 an octave once a push
//   edges      the cell's end (the whole of it inside, never cut), its start, the keyboard's ends (shape kept)
//   bars       across the bar lines of the cell, the bar shown follows, the engine plays it there, undo, no room,
//              the cursor kept on it, an undo with R1 held
//   over       over another chord, over a note on the same key, both ways
//   heard      a key changed is heard with the song stopped
//   back       B with R1 held puts it back as R1 found it (across bars too); it deleted it
//   copy       L1 then R1 let go unmoved still copies the bar; moved (to another bar, same step) it does not
//   fill       on the FILL it stays on the FILL
//   loose      a loose note is carried by itself (a stack of loose notes is not one chord)
//   hints      the bar while carrying fits, at LARGE and XL
//
// `carry_test NAME...` runs just those.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

const SDL_Keycode A = SDLK_x, B = SDLK_z, Y = SDLK_a, L1 = SDLK_q, R1 = SDLK_w, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT,
                  RIGHT = SDLK_RIGHT, SELECT = SDLK_BACKSPACE, START = SDLK_RETURN;
const SDL_Keycode STICK_UP = SDLK_i, STICK_DOWN = SDLK_k, STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l;
const int kStep = 6000; // at 120 BPM, 48 kHz

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    // A Phazed track on the roll (track 1), `bars` long, the song stopped.
    explicit Rig(int bars = 1) {
        app.session().view = View::Pattern;
        frames(2);
        CHECK(app.session().addTrack(TrackKind::Phazed));
        project().track = 1;
        project().tracks[1].bars[0] = bars;
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
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    std::vector<float> render(int audioFrames) {
        std::vector<float> out(2 * audioFrames);
        engine->render(out.data(), audioFrames);
        return out;
    }
    static float peak(const std::vector<float>& v) {
        float p = 0.f;
        for (float s : v) p = std::fabs(s) > p ? std::fabs(s) : p;
        return p;
    }
    void undo() {
        key(SELECT, true);
        tap(B);
        key(SELECT, false);
        frames(1);
    }
    Project& project() { return app.project(); }
    NoteScreen& roll() { return app.notes(); }
    NotePattern& bar(int b) { return project().tracks[1].notes[0][b]; }
    void told() { app.session().sendNotePattern(1, 0); }
};

ChordEvent chord(int step, float length, int root) {
    ChordEvent c;
    c.step = step, c.length = length, c.root = root, c.count = 3;
    c.intervals[0] = 0, c.intervals[1] = 4, c.intervals[2] = 7;
    c.velocity = 0.7f;
    std::snprintf(c.name, sizeof c.name, "C");
    return c;
}
NoteEvent note(int step, int pitch, float length) {
    NoteEvent n;
    n.step = step, n.pitch = pitch, n.length = length, n.velocity = 0.6f;
    return n;
}
int chordsIn(const NotePattern& p) {
    int n = 0;
    for (const ChordEvent& c : p.chords) n += c.velocity > 0.f ? 1 : 0;
    return n;
}
int notesIn(const NotePattern& p) {
    int n = 0;
    for (const NoteEvent& e : p.notes) n += e.velocity > 0.f ? 1 : 0;
    return n;
}
int firstChord(const NotePattern& p) {
    for (int i = 0; i < kMaxChords; ++i)
        if (p.chords[i].velocity > 0.f) return i;
    return -1;
}
int firstNote(const NotePattern& p) {
    for (int i = 0; i < kMaxNotes; ++i)
        if (p.notes[i].velocity > 0.f) return i;
    return -1;
}

// ---- both: a chord by key and by step in one hold --------------------------------------------------------------

void testAChordGoesBothWaysInOneHold() {
    Rig rig;
    rig.project().playing = true; // (silent moves)
    ChordEvent c = chord(4, 2.f, 60);
    c.ratchet = 3, c.throwTo = 2, c.nudge = 0.2f, c.inversion = 1, c.play.strum = 2;
    rig.bar(0).chords[5] = c;
    rig.told();
    rig.roll().moveCursorTo(67, 5); // its top key, its second step
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.roll().carrying());
    int was[kChordNotes];
    const int keys = c.pitches(was);
    // Right, down, down, left, left, left, up, in any order: it goes where each says, all its keys together.
    rig.tap(RIGHT);
    rig.tap(DOWN, 2);
    rig.tap(LEFT, 3);
    rig.tap(UP);
    const ChordEvent& now = rig.bar(0).chords[5];
    CHECK(now.step == 5 && now.root == 58 && rig.roll().cursorPitch() == 65 && rig.roll().cursorStep() == 6);
    int is[kChordNotes];
    CHECK(now.pitches(is) == keys);
    for (int k = 0; k < keys; ++k) CHECK(is[k] == was[k] - 2);
    CHECK(now.length == 2.f && now.velocity == 0.7f && now.count == 3 && now.inversion == 1 && now.ratchet == 3 && now.throwTo == 2 &&
          now.play.strum == 2 && now.nudge == 0.2f && std::strcmp(now.name, "C") == 0);
    // Let go: it lands there, and the cursor moves alone again.
    rig.key(R1, false);
    rig.frames(1);
    CHECK(!rig.roll().carrying() && chordsIn(rig.bar(0)) == 1);
    rig.tap(DOWN);
    CHECK(rig.bar(0).chords[5].step == 5 && rig.roll().cursorStep() == 7);
    // R1 on any key of it, on any step it has, takes the whole of it.
    rig.roll().moveCursorTo(is[0], 6);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(UP, 2);
    CHECK(rig.bar(0).chords[5].step == 3 && rig.roll().cursorStep() == 4);
    rig.key(R1, false);
    rig.frames(1);
    // On the chord's step but on no key of it, R1 is the brush (it paints a note), not a carry.
    rig.roll().moveCursorTo(is[0] + 1, 3);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(!rig.roll().carrying() && notesIn(rig.bar(0)) == 1);
    rig.key(R1, false);
    rig.frames(1);
}

// ---- stick ------------------------------------------------------------------------------------------------------

void testTheStickGoesBothWays() {
    Rig rig;
    rig.project().playing = true;
    rig.bar(0).chords[0] = chord(2, 2.f, 60);
    rig.told();
    rig.roll().moveCursorTo(60, 2);
    rig.key(R1, true);
    rig.frames(1);
    // Down and right in one push: both at once, by whole steps and keys.
    rig.key(STICK_DOWN, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    const ChordEvent& c = rig.bar(0).chords[0];
    CHECK(c.step == 3 && c.root == 61 && c.nudge == 0.f); // one of each at once
    rig.frames(4);
    rig.key(STICK_DOWN, false);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(c.step > 3 && c.root > 61 && c.step - 2 == c.root - 60 && c.nudge == 0.f && c.length == 2.f);
    CHECK(rig.roll().cursorPitch() == c.root && rig.roll().cursorStep() == c.step);
    // Up alone: time only. Left alone: keys only.
    const int step = c.step, root = c.root;
    rig.key(STICK_UP, true);
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(c.step == step - 1 && c.root == root);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(c.step == step - 1 && c.root == root - 1);
    // Held, the stick runs to the end of the cell and stops there with all of it inside.
    rig.key(STICK_DOWN, true);
    rig.frames(200);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(c.step == 14 && c.length == 2.f && rig.roll().cursorStep() == 14);
    // With L1 held, sideways is an octave once a push, however long it is held; up / down still steps.
    rig.key(L1, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(30);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(c.root == root - 1 + 12 && c.step == 14);
    rig.key(STICK_UP, true);
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(c.step == 13);
    rig.key(L1, false);
    rig.key(R1, false);
    rig.frames(1);
}

// ---- edges ------------------------------------------------------------------------------------------------------

void testItStopsAtTheEdgesWhole() {
    Rig rig;
    rig.project().playing = true;
    // A chord four long in a one-bar cell: its last start is 12 (it used to go on to 15 with three steps cut off).
    rig.bar(0).chords[0] = chord(10, 4.f, 60);
    // A note 2.5 long: its last start is 13 (13 + 2.5 is inside; 14 + 2.5 is not).
    rig.bar(0).notes[0] = note(3, 80, 2.5f);
    rig.told();
    rig.roll().moveCursorTo(64, 11); // the chord's second step
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 6);
    const ChordEvent& c = rig.bar(0).chords[0];
    CHECK(c.step == 12 && c.length == 4.f && rig.roll().cursorStep() == 13);
    rig.tap(UP, 20);
    CHECK(c.step == 0 && c.length == 4.f && rig.roll().cursorStep() == 1);
    // The top of the keyboard: it stops with its top key on C8, its shape kept; an octave that would pass it is refused.
    rig.tap(RIGHT, 60);
    int keys[kChordNotes];
    CHECK(c.pitches(keys) == 3 && keys[2] == NoteScreen::kHighest && keys[1] == keys[2] - 3 && keys[0] == keys[2] - 7);
    rig.key(L1, true);
    rig.tap(RIGHT);
    CHECK(c.pitches(keys) == 3 && keys[2] == NoteScreen::kHighest);
    rig.key(L1, false);
    // The bottom: its lowest key on C0.
    rig.tap(LEFT, 120);
    CHECK(c.pitches(keys) == 3 && keys[0] == NoteScreen::kLowest && keys[2] == keys[0] + 7 && c.root == NoteScreen::kLowest);
    rig.key(R1, false);
    rig.frames(1);
    rig.roll().moveCursorTo(80, 3);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 20);
    CHECK(rig.bar(0).notes[0].step == 13 && rig.bar(0).notes[0].length == 2.5f);
    rig.key(R1, false);
    rig.frames(1);
}

// ---- bars -------------------------------------------------------------------------------------------------------

void testAcrossTheBarsOfTheCell() {
    Rig rig(2);
    rig.bar(0).chords[0] = chord(12, 2.f, 60);
    rig.told();
    rig.frames(60); // (settled: the chord at 13 is where undo comes back to)
    rig.project().playing = true;
    rig.roll().moveCursorTo(60, 12);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 6); // over the bar line: bar 2, step 3
    CHECK(chordsIn(rig.bar(0)) == 0 && chordsIn(rig.bar(1)) == 1);
    int slot = firstChord(rig.bar(1));
    CHECK(slot >= 0 && rig.bar(1).chords[slot].step == 2 && rig.bar(1).chords[slot].length == 2.f && rig.bar(1).chords[slot].root == 60);
    CHECK(rig.project().bar == 1 && rig.roll().cursorStep() == 2 && rig.roll().carrying()); // the bar shown is the one it went to
    // Sideways there, then down to the cell's end (its last bar: 14, all of it inside).
    rig.tap(RIGHT, 2);
    rig.tap(DOWN, 30);
    CHECK(rig.bar(1).chords[slot].step == 14 && rig.bar(1).chords[slot].root == 62 && rig.project().bar == 1);
    // ...and back up over the line to the cell's start.
    rig.tap(UP, 40);
    CHECK(chordsIn(rig.bar(1)) == 0 && chordsIn(rig.bar(0)) == 1 && rig.bar(0).chords[firstChord(rig.bar(0))].step == 0 && rig.project().bar == 0);
    rig.tap(DOWN, 18); // bar 2, step 3
    slot = firstChord(rig.bar(1));
    CHECK(slot >= 0 && rig.bar(1).chords[slot].step == 2);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(!rig.roll().carrying() && rig.roll().chordHere() == &rig.bar(1).chords[slot]); // let go on it: the cursor is on it
    // The engine plays it there: silent through bar 1 and bar 2's first two steps, then the chord.
    rig.project().playing = false;
    rig.frames(200);
    rig.render(96000);
    rig.tap(START);
    const float early = Rig::peak(rig.render(18 * kStep - 1500));
    const float then = Rig::peak(rig.render(2 * kStep));
    CHECK(early < 0.002f && then > 0.005f);
    if (early >= 0.002f || then <= 0.005f) std::printf("  early %g, then %g\n", static_cast<double>(early), static_cast<double>(then));
    rig.tap(START);
    rig.render(96000);
    // Undo brings it back to bar 1, step 13, both bars at once.
    rig.frames(200); // (the carry settles into one step of undo)
    rig.undo();
    CHECK(chordsIn(rig.bar(1)) == 0 && chordsIn(rig.bar(0)) == 1 && firstChord(rig.bar(0)) >= 0 && rig.bar(0).chords[firstChord(rig.bar(0))].step == 12);
}

void testNoRoomInTheNextBar() {
    Rig rig(2);
    rig.project().playing = true;
    // Bar 2 has all sixty-four notes it can hold: a note carried down stops at the end of bar 1.
    for (int i = 0; i < kMaxNotes; ++i) rig.bar(1).notes[i] = note(i % 16, 20 + i / 16, 1.f);
    rig.bar(0).notes[0] = note(13, 70, 1.f);
    rig.told();
    rig.roll().moveCursorTo(70, 13);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 5);
    CHECK(rig.bar(0).notes[0].step == 15 && rig.bar(0).notes[0].velocity > 0.f && notesIn(rig.bar(1)) == kMaxNotes && rig.project().bar == 0);
    rig.key(R1, false);
    rig.frames(1);
    // A chord's three keys do not fit beside sixty-two notes either.
    rig.bar(1).notes[0] = NoteEvent(), rig.bar(1).notes[1] = NoteEvent();
    rig.bar(0).chords[0] = chord(14, 1.f, 40);
    rig.told();
    rig.roll().moveCursorTo(40, 14);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 3);
    CHECK(rig.bar(0).chords[0].step == 15 && chordsIn(rig.bar(1)) == 0);
    rig.key(R1, false);
    rig.frames(1);
    // Nor does a chord go into a bar with all sixteen chords it can hold (one key each: notes to spare): every step
    // there is a chord's.
    for (int i = 0; i < kMaxNotes; ++i) rig.bar(1).notes[i] = NoteEvent();
    for (int i = 0; i < kMaxChords; ++i) {
        rig.bar(1).chords[i] = chord(i, 1.f, 30);
        rig.bar(1).chords[i].count = 1;
    }
    rig.told();
    rig.roll().moveCursorTo(40, 15);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 3);
    CHECK(rig.bar(0).chords[0].step == 15 && chordsIn(rig.bar(1)) == kMaxChords && rig.project().bar == 0);
    rig.key(R1, false);
    rig.frames(1);
}

// The cursor stays on what it carries, in the bar its start is in: taken three steps into a chord four long, carried
// to step 14 of bar 1, the cursor is on its step 15 (its last there), not over the line in bar 2; and back up it is
// three steps in again.
void testTheCursorStaysOnIt() {
    Rig rig(2);
    rig.project().playing = true;
    rig.bar(0).chords[0] = chord(4, 4.f, 60);
    rig.told();
    rig.roll().moveCursorTo(60, 6);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 10);
    CHECK(rig.bar(0).chords[0].step == 14 && rig.project().bar == 0 && rig.roll().cursorStep() == 15 && rig.roll().chordHere() == &rig.bar(0).chords[0]);
    rig.tap(UP, 10);
    CHECK(rig.bar(0).chords[0].step == 4 && rig.roll().cursorStep() == 6);
    rig.key(R1, false);
    rig.frames(1);
}

// An undo with R1 still held takes what it carried from under it: it carries nothing more.
void testAnUndoTakesItFromUnderR1() {
    Rig rig(2);
    rig.bar(0).chords[0] = chord(12, 2.f, 60);
    rig.told();
    rig.frames(60);
    rig.project().playing = true;
    rig.roll().moveCursorTo(60, 12);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 6); // into bar 2
    CHECK(chordsIn(rig.bar(1)) == 1);
    rig.frames(60);
    rig.undo();
    CHECK(chordsIn(rig.bar(1)) == 0 && chordsIn(rig.bar(0)) == 1 && !rig.roll().carrying());
    rig.key(R1, false);
    rig.frames(1);
}

// ---- over -------------------------------------------------------------------------------------------------------

void testItGoesOverWhatIsInTheWay() {
    Rig rig;
    rig.project().playing = true;
    rig.bar(0).chords[0] = chord(2, 2.f, 60);
    rig.bar(0).chords[1] = chord(6, 3.f, 65);
    rig.told();
    rig.roll().moveCursorTo(60, 2);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 2); // 4: its end on the other's start
    CHECK(rig.bar(0).chords[0].step == 4);
    rig.tap(DOWN); // 5 would run into it: over it, to 9 (the first step it clears)
    CHECK(rig.bar(0).chords[0].step == 9 && rig.bar(0).chords[1].step == 6 && rig.roll().cursorStep() == 9);
    rig.tap(UP); // back over it, to 4
    CHECK(rig.bar(0).chords[0].step == 4 && rig.roll().cursorStep() == 4);
    rig.key(R1, false);
    rig.frames(1);
    // A chord after it to the cell's end: nowhere free past it, so it stays.
    rig.bar(0).chords[1] = chord(6, 10.f, 65);
    rig.told();
    rig.roll().moveCursorTo(60, 4);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN);
    CHECK(rig.bar(0).chords[0].step == 4);
    rig.key(R1, false);
    rig.frames(1);
    // Over one in the next bar of the cell: chords on both sides of the bar line are seen.
    Rig cell(2);
    cell.project().playing = true;
    cell.bar(0).chords[0] = chord(10, 4.f, 60);
    cell.bar(1).chords[0] = chord(1, 2.f, 70);
    cell.told();
    cell.roll().moveCursorTo(60, 10);
    cell.key(R1, true);
    cell.frames(1);
    cell.tap(DOWN, 3); // 13: it runs over the bar line, its end on the other's start
    CHECK(cell.bar(0).chords[0].step == 13);
    cell.tap(DOWN); // 14 would run into bar 2's at its step 2: over it, to bar 2's step 4
    CHECK(chordsIn(cell.bar(0)) == 0 && chordsIn(cell.bar(1)) == 2 && cell.project().bar == 1 && cell.roll().cursorStep() == 3);
    cell.key(R1, false);
    cell.frames(1);
    // A note: over another on its key (a span, not only its start: 50 at 8, 3 long, covers 8, 9, 10).
    Rig two;
    two.project().playing = true;
    two.bar(0).notes[0] = note(5, 50, 2.f);
    two.bar(0).notes[1] = note(8, 50, 3.f);
    two.bar(0).notes[2] = note(5, 51, 1.f); // and one beside it, for sideways
    two.told();
    two.roll().moveCursorTo(50, 5);
    two.key(R1, true);
    two.frames(1);
    two.tap(DOWN); // 6: its end on the other's start
    CHECK(two.bar(0).notes[0].step == 6);
    two.tap(DOWN); // 7 would overlap 8: over it, to 11
    CHECK(two.bar(0).notes[0].step == 11 && two.roll().cursorStep() == 11);
    two.tap(UP);
    CHECK(two.bar(0).notes[0].step == 6);
    two.tap(UP);
    CHECK(two.bar(0).notes[0].step == 5);
    two.tap(RIGHT); // 51 is taken at step 5: over it, to 52
    CHECK(two.bar(0).notes[0].pitch == 52 && two.roll().cursorPitch() == 52);
    two.tap(LEFT);
    CHECK(two.bar(0).notes[0].pitch == 50 && two.bar(0).notes[2].pitch == 51);
    two.key(R1, false);
    two.frames(1);
}

// A note carried to another key is heard there with the song stopped (a chord too, all its keys); nothing is heard
// for a step later, where the song will play it.
void testItIsHeard() {
    Rig rig;
    rig.bar(0).notes[0] = note(4, 60, 1.f);
    rig.bar(0).chords[0] = chord(8, 2.f, 48);
    rig.told();
    rig.frames(200);
    rig.render(96000);
    for (int what = 0; what < 2; ++what) {
        rig.roll().moveCursorTo(what ? 48 : 60, what ? 8 : 4);
        rig.key(R1, true);
        rig.frames(1);
        rig.render(48000);
        rig.tap(DOWN);
        const float later = Rig::peak(rig.render(4000));
        rig.tap(RIGHT);
        const float there = Rig::peak(rig.render(4000));
        CHECK(later < 0.002f && there > 0.005f);
        rig.key(R1, false);
        rig.frames(200);
        rig.render(96000);
    }
}

// ---- back -------------------------------------------------------------------------------------------------------

void testBPutsItBack() {
    Rig rig(2);
    rig.project().playing = true;
    rig.bar(0).chords[3] = chord(10, 2.f, 60);
    rig.bar(0).notes[0] = note(1, 72, 1.f);
    rig.told();
    rig.roll().moveCursorTo(64, 11);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(RIGHT, 3);
    rig.tap(DOWN, 9); // into bar 2
    CHECK(chordsIn(rig.bar(1)) == 1 && rig.project().bar == 1);
    rig.tap(B);
    // As R1 found it: its bar, its slot, its step and its keys; the cursor where it was; nothing deleted.
    CHECK(chordsIn(rig.bar(1)) == 0 && chordsIn(rig.bar(0)) == 1 && rig.bar(0).chords[3].step == 10 && rig.bar(0).chords[3].root == 60 &&
          rig.bar(0).chords[3].length == 2.f);
    CHECK(rig.project().bar == 0 && rig.roll().cursorPitch() == 64 && rig.roll().cursorStep() == 11 && !rig.roll().carrying());
    // R1 still down carries nothing more: the D-pad moves the cursor, and let go nothing changes.
    rig.tap(DOWN);
    CHECK(rig.bar(0).chords[3].step == 10 && rig.roll().cursorStep() == 12);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(chordsIn(rig.bar(0)) == 1 && rig.bar(0).chords[3].step == 10 && notesIn(rig.bar(0)) == 1);
    // A note the same, in its own bar.
    rig.roll().moveCursorTo(72, 1);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(LEFT, 2);
    rig.tap(UP);
    rig.tap(B);
    CHECK(rig.bar(0).notes[0].pitch == 72 && rig.bar(0).notes[0].step == 1 && rig.roll().cursorPitch() == 72 && rig.roll().cursorStep() == 1);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(notesIn(rig.bar(0)) == 1);
}

// ---- copy -------------------------------------------------------------------------------------------------------

void testATapStillCopiesTheBar() {
    Rig rig(2);
    rig.project().playing = true;
    rig.bar(0).chords[0] = chord(4, 2.f, 60);
    rig.told();
    Session& session = rig.app.session();
    const unsigned copies = session.barCopies();
    rig.roll().moveCursorTo(60, 4);
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(R1); // unmoved: copied
    rig.key(L1, false);
    rig.frames(1);
    CHECK(session.barCopies() == copies + 1 && rig.bar(0).chords[0].step == 4);
    // Moved to the same step of the next bar: a carry, no copy.
    rig.key(L1, true);
    rig.frames(1);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 16);
    rig.key(R1, false);
    rig.frames(1);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(session.barCopies() == copies + 1 && chordsIn(rig.bar(1)) == 1 && rig.roll().cursorStep() == 4 && rig.project().bar == 1);
}

// ---- fill -------------------------------------------------------------------------------------------------------

void testOnTheFillItStaysOnTheFill() {
    Rig rig(2);
    rig.project().playing = true;
    TrackData& t = rig.project().tracks[1];
    t.fill[0] = kFillOn;
    rig.bar(kFillSlot).chords[0] = chord(12, 2.f, 60);
    rig.told();
    rig.project().bar = kFillSlot;
    rig.frames(1);
    rig.roll().moveCursorTo(60, 12);
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(DOWN, 6);
    CHECK(rig.bar(kFillSlot).chords[0].step == 14 && chordsIn(rig.bar(0)) == 0 && chordsIn(rig.bar(1)) == 0 && rig.project().bar == kFillSlot);
    rig.tap(UP, 20);
    CHECK(rig.bar(kFillSlot).chords[0].step == 0 && rig.project().bar == kFillSlot);
    rig.key(R1, false);
    rig.frames(1);
}

// ---- loose ------------------------------------------------------------------------------------------------------

void testALooseNoteGoesByItself() {
    Rig rig;
    rig.project().playing = true;
    rig.bar(0).notes[0] = note(4, 60, 1.f);
    rig.bar(0).notes[1] = note(4, 64, 2.f);
    rig.bar(0).notes[2] = note(4, 67, 3.f);
    rig.told();
    rig.roll().moveCursorTo(64, 5); // its second step
    rig.key(R1, true);
    rig.frames(1);
    rig.tap(RIGHT);
    rig.tap(DOWN, 2);
    CHECK(rig.bar(0).notes[1].pitch == 65 && rig.bar(0).notes[1].step == 6 && rig.bar(0).notes[1].length == 2.f && rig.roll().cursorStep() == 7);
    CHECK(rig.bar(0).notes[0].pitch == 60 && rig.bar(0).notes[0].step == 4 && rig.bar(0).notes[2].pitch == 67 && rig.bar(0).notes[2].step == 4);
    rig.key(R1, false);
    rig.frames(1);
}

// ---- hints ------------------------------------------------------------------------------------------------------

void testTheHintsFit() {
    Canvas canvas;
    if (!canvas.ok) return;
    Rig rig;
    rig.bar(0).chords[0] = chord(4, 2.f, 60);
    rig.told();
    rig.roll().moveCursorTo(60, 4);
    rig.key(R1, true);
    rig.frames(1);
    const std::vector<unsigned char> ttf = loadFontFile();
    Fonts xl;
    xl.fixedSet = canvas.fonts.get();
    CHECK(xl.load(canvas.r, ttf, kTextXl));
    for (const char* help : {"MENU: HELP", "HLD SEL: HELP"})
        for (bool l1 : {false, true})
            for (const Fonts* fonts : {static_cast<const Fonts*>(canvas.fonts.get()), static_cast<const Fonts*>(&xl)}) {
                rig.key(L1, l1);
                rig.frames(1);
                ui::gHelpHint = help;
                canvas.clear();
                rig.app.draw(canvas.r, *fonts);
                CHECK(ui::gHintsOver == 0.f);
                if (ui::gHintsOver != 0.f) std::printf("  %s, L1 %d: %.0f pixels over\n", help, l1, static_cast<double>(ui::gHintsOver));
            }
    ui::gHelpHint = "MENU: HELP";
    rig.key(L1, false);
    rig.key(R1, false);
    rig.frames(1);
}

struct Section {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const Section sections[] = {
        {"both", testAChordGoesBothWaysInOneHold},
        {"stick", testTheStickGoesBothWays},
        {"edges", testItStopsAtTheEdgesWhole},
        {"bars", [] { testAcrossTheBarsOfTheCell(), testNoRoomInTheNextBar(), testTheCursorStaysOnIt(), testAnUndoTakesItFromUnderR1(); }},
        {"over", testItGoesOverWhatIsInTheWay},
        {"heard", testItIsHeard},
        {"back", testBPutsItBack},
        {"copy", testATapStillCopiesTheBar},
        {"fill", testOnTheFillItStaysOnTheFill},
        {"loose", testALooseNoteGoesByItself},
        {"hints", testTheHintsFit},
    };
    for (const Section& s : sections) {
        bool run = argc < 2;
        for (int i = 1; i < argc; ++i) run = run || std::strcmp(argv[i], s.name) == 0;
        if (run) s.run();
    }
    if (failures == 0) std::printf("carry: all passed\n");
    else std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
