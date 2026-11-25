// COPY A BAR: hold L1, R1 copies the bar on screen, A pastes it over the bar on screen (the drum
// grid, its slice view, the piano roll). The Session's side (Session::copyBar / pasteBar): every
// field of a hit, notes and chords with a length over the bar line held to the cell's end, the
// automation on the same track only, the refusals, one step of undo, the same bar no step, a paste
// spending the copy (a Same and a refusal keep it). The
// gesture through the real screens: R1 paints and carries nothing, A places nothing, L1 alone
// still opens the slices and zooms (on its let go) and does not when a copy or a paste was made,
// X held keeps L1 / R1 for the step effects, a panic begun with L1 then R1 leaves the copy as it
// was. The engine hears the pasted bar: the song played beside one with the bar written by hand.

#include <SDL.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "samples.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2, UP = SDLK_UP, RIGHT = SDLK_RIGHT, LEFT = SDLK_LEFT, DOWN = SDLK_DOWN,
                  SELECT = SDLK_BACKSPACE;
const SDL_Keycode STICK_DOWN = SDLK_k, STICK_RIGHT = SDLK_l, Y = SDLK_a;
const int kStep = 6000; // 120 BPM at 48 kHz

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    explicit Rig(const std::string& sampleFolder = "") {
        if (!sampleFolder.empty()) app.session().useSampleFolder(sampleFolder);
        app.session().view = View::Pattern;
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
    // Hold L1, tap `code`, let L1 go: the gesture.
    void withL1(SDL_Keycode code) {
        key(L1, true);
        frames(1);
        tap(code);
        key(L1, false);
        frames(1);
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
    std::vector<float> play(int steps) {
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

bool sameDrums(const DrumPattern& a, const DrumPattern& b) {
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) {
            const StepFx &x = a.fx[lane][step], &y = b.fx[lane][step];
            if (a.level[lane][step] != b.level[lane][step] || a.nudge[lane][step] != b.nudge[lane][step] || a.chance[lane][step] != b.chance[lane][step] ||
                a.start[lane][step] != b.start[lane][step] || x.pitch != y.pitch || x.ratchet != y.ratchet || x.reverse != y.reverse || x.cut != y.cut ||
                x.throwTo != y.throwTo)
                return false;
        }
    return true;
}

// Bar 0 of track 0, scene 0: hits with every field set, on a cell two bars long; a send written in its automation.
void writeDrumBar(Rig& rig) {
    rig.session().setCellBars(0, 0, 2); // (while empty: nothing is filled in)
    rig.project().track = 0;
    rig.project().scene = 0;
    rig.project().bar = 0;
    rig.frames(1);
    PatternScreen& drums = rig.app.drums();
    drums.setStep(0, 0, 1.f);
    drums.setStep(0, 8, 0.7f);
    drums.setStep(2, 4, 0.5f);
    drums.setNudge(2, 4, -0.2f);
    drums.setStep(1, 6, 0.8f);
    drums.setChance(1, 6, 0.5f);
    drums.setStep(5, 10, 0.6f);
    drums.setStart(5, 10, 0.25f);
    StepFx fx;
    fx.pitch = -3, fx.ratchet = 3, fx.reverse = 1, fx.cut = 2, fx.throwTo = kThrowEcho;
    drums.setStep(3, 12, 0.9f);
    drums.setFx(3, 12, fx);
    rig.session().setAutomation(0, 0, 20, 3, 0.7f);        // bar 1's step 4: the ECHO send
    rig.session().setAutomation(0, 0, 20, kSteps + 5, 0.2f); // bar 2's own, kept out of the copy
    rig.frames(1);
}

// ---- drums -------------------------------------------------------------------------------------

void testDrums() {
    Rig rig;
    writeDrumBar(rig);
    PatternScreen& drums = rig.app.drums();
    rig.settle();
    // Nothing copied yet: A with L1 held places nothing and says so.
    rig.project().bar = 1;
    drums.moveCursorTo(4, 2);
    rig.withL1(A);
    CHECK(rig.said() == "NOTHING COPIED YET");
    CHECK(rig.track(0).drums[0][1].empty() && !rig.session().barClip().has);
    rig.project().bar = 0;
    rig.frames(1);

    // COPY: hold L1, R1 on an empty step. R1 paints nothing; L1 let go opens no slices (and does not say why not).
    drums.moveCursorTo(4, 2);
    CHECK(drums.level(4, 2) == 0.f);
    rig.key(L1, true);
    rig.frames(1);
    CHECK(drums.barKeyHeld());
    rig.key(R1, true);
    rig.frames(1);
    CHECK(!drums.painting() && drums.level(4, 2) == 0.f);
    CHECK(rig.said() == "BAR 1 COPIED");
    rig.key(R1, false);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(!drums.slicing() && drums.said()[0] == 0 && !drums.barKeyHeld());
    const Session::BarClip& clip = rig.session().barClip();
    CHECK(clip.has && clip.drums && clip.track == 0 && clip.scene == 0 && clip.bar == 0);
    CHECK(sameDrums(clip.drumBar, rig.track(0).drums[0][0]));
    CHECK(clip.automation[20][3] == 0.7f && clip.automation[20][5] < 0.f);

    // PASTE on bar 2: hold L1, A on an empty step. Every field comes over; A placed nothing of its own.
    rig.settle();
    const int stepsBefore = rig.session().undoSteps();
    rig.project().bar = 1;
    rig.frames(1);
    drums.moveCursorTo(4, 2);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(A, true);
    rig.frames(1);
    CHECK(rig.said() == "PASTED ON BAR 2");
    CHECK(sameDrums(rig.track(0).drums[0][1], rig.track(0).drums[0][0]));
    CHECK(!rig.session().barClip().has); // spent: a paste clears the copy (testAPasteSpendsTheCopy)
    CHECK(drums.level(4, 2) == 0.f);
    // A's hold is not A's: the D-pad does not nudge or level anything while it is down.
    const float level = drums.level(0, 8);
    CHECK(level == 0.7f);
    drums.moveCursorTo(0, 8);
    rig.tap(UP);
    CHECK(drums.level(0, 8) == level);
    rig.key(A, false);
    rig.key(L1, false);
    rig.frames(1);
    // Its automation came with it (the same track), bar 2's own step 6 replaced (it was not in bar 1).
    CHECK(rig.track(0).automation[0][20][kSteps + 3] == 0.7f && rig.track(0).automation[0][20][kSteps + 5] < 0.f);
    // One step of undo, and back.
    rig.settle();
    CHECK(rig.session().undoSteps() == stepsBefore + 1);
    rig.undo();
    CHECK(rig.track(0).drums[0][1].empty() && rig.track(0).automation[0][20][kSteps + 5] == 0.2f && rig.track(0).automation[0][20][kSteps + 3] < 0.f);
    CHECK(rig.session().undoSteps() == stepsBefore);
    rig.redo();
    CHECK(sameDrums(rig.track(0).drums[0][1], rig.track(0).drums[0][0]) && rig.track(0).automation[0][20][kSteps + 3] == 0.7f);
    rig.settle();
    // Pasted again on the same: nothing changes, no step of undo (a fresh copy: the paste spent it).
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1);
    CHECK(rig.said() == "BAR 1 COPIED");
    rig.project().bar = 1;
    rig.frames(1);
    const int stepsNow = rig.session().undoSteps();
    rig.withL1(A);
    CHECK(rig.said() == "BAR 2 IS THE SAME ALREADY");
    rig.settle();
    CHECK(rig.session().undoSteps() == stepsNow);
    // ...and back on the bar it came from, nothing changed: no step either.
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(A);
    rig.settle();
    CHECK(rig.said() == "BAR 1 IS THE SAME ALREADY" && rig.session().undoSteps() == stepsNow);
    // A bar that differs only in a hit's start, or only in its automation, is not the same: the paste puts it back.
    rig.project().bar = 1;
    rig.frames(1);
    drums.setStart(5, 10, 0.5f);
    rig.settle();
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 2" && rig.track(0).drums[0][1].start[5][10] == 0.25f);
    rig.session().setAutomation(0, 0, 20, kSteps + 9, 0.4f);
    rig.settle();
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    rig.project().bar = 1;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 2" && rig.track(0).automation[0][20][kSteps + 9] < 0.f);
    rig.settle();
    rig.project().bar = 0;
    rig.frames(1);
    // L1 pressed after A (A held: shaping) is not the bar key: R1 copies nothing.
    {
        const unsigned copies = rig.session().barCopies();
        drums.moveCursorTo(0, 0);
        rig.key(A, true);
        rig.frames(1);
        rig.key(L1, true);
        rig.frames(1);
        CHECK(!drums.barKeyHeld());
        rig.tap(R1);
        rig.key(L1, false);
        rig.key(A, false);
        rig.frames(1);
        CHECK(rig.session().barCopies() == copies);
        // L1 let go while another screen is up: back on the grid it is not held.
        rig.key(L1, true);
        rig.frames(1);
        CHECK(drums.barKeyHeld());
        rig.session().view = View::Song;
        rig.frames(1);
        rig.key(L1, false);
        rig.frames(1);
        rig.session().view = View::Pattern;
        rig.frames(1);
        CHECK(!drums.barKeyHeld());
    }

    // A paste and an edit straight after are two steps, not one burst.
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    rig.project().bar = 1;
    rig.frames(1);
    drums.setStep(0, 0, 0.f);
    rig.settle();
    const int beforePaste = rig.session().undoSteps();
    rig.withL1(A);
    drums.setStep(7, 15, 1.f);
    rig.settle();
    CHECK(rig.session().undoSteps() == beforePaste + 2);
    rig.undo();
    CHECK(sameDrums(rig.track(0).drums[0][1], rig.track(0).drums[0][0]));
    drums.setStep(7, 15, 0.f);
    rig.settle();

    // An edit and a paste straight after it (no still moment between): two steps too.
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    rig.project().bar = 1;
    rig.frames(1);
    const int beforeBoth = rig.session().undoSteps();
    drums.setStep(0, 0, 0.f);
    rig.frames(1);
    rig.withL1(A);
    rig.settle();
    CHECK(rig.session().undoSteps() == beforeBoth + 2);
    rig.undo();
    CHECK(rig.track(0).drums[0][1].level[0][0] == 0.f && rig.track(0).drums[0][1].level[2][4] > 0.f);
    rig.redo();
    rig.settle();

    // X held on an empty step (it hears the drum): L1 and R1 copy nothing.
    {
        const unsigned copies = rig.session().barCopies();
        drums.moveCursorTo(6, 14);
        rig.key(X, true);
        rig.frames(1);
        rig.withL1(R1);
        rig.key(X, false);
        rig.frames(1);
        CHECK(rig.session().barCopies() == copies);
    }

    // Another scene, another drum track: the hits; automation only on the same track.
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    rig.project().scene = 1;
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(A);
    CHECK(sameDrums(rig.track(0).drums[1][0], rig.track(0).drums[0][0]) && rig.track(0).automation[1][20][3] == 0.7f);
    rig.project().track = 0;
    rig.project().scene = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    CHECK(rig.session().addTrack(TrackKind::Drums));
    rig.project().track = 1;
    rig.project().scene = 0;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 1");
    CHECK(sameDrums(rig.track(1).drums[0][0], rig.track(0).drums[0][0]) && rig.track(1).automation[0][20][3] < 0.f);
    // Onto a synth track: refused, nothing placed.
    rig.project().track = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it (the refusal keeps it)
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 2;
    rig.frames(1);
    rig.app.notes().moveCursorTo(60, 3);
    rig.withL1(A);
    CHECK(rig.said() == "DRUMS GO ON A DRUM TRACK" && rig.track(2).notes[0][0].empty());
    // The Session's own refusals: no track there, a bar the cell does not have.
    CHECK(rig.session().pasteBar(5, 0, 0) == Session::BarCopy::NoTrack && rig.session().pasteBar(-1, 0, 0) == Session::BarCopy::NoTrack);
    CHECK(rig.session().pasteBar(0, 0, 2) == Session::BarCopy::NoTrack && rig.session().copyBar(5, 0, 0) == Session::BarCopy::NoTrack);
    CHECK(rig.session().pasteBar(0, kMaxScenes, 0) == Session::BarCopy::NoTrack && rig.session().copyBar(0, 0, kMaxBars) == Session::BarCopy::NoTrack);
    // A new song keeps what was copied (it is the program's, not the song's).
    rig.session().newSong();
    rig.frames(4);
    CHECK(rig.session().barClip().has);
}

// A paste spends the bar's copy (the owner, 2026-11-25: "you copy, clipboard has it, another R1 pastes and
// then the clipboard is clear"; gesture audit G23 had kept it that morning). L1 + A twice pastes once: the
// second says NOTHING COPIED YET and places nothing. (L1 + R1 always copied afresh, G23 included: a bar has
// its own copy key, so the spend is for one rule with SONG's cell and scene.) A Same and a refusal paste
// nothing, so they keep the copy. One Session::pasteBar
// for the drum grid, its slice view and the roll (testTheSlicesAndL1 and testNotes take it through those).
void testAPasteSpendsTheCopy() {
    Rig rig;
    writeDrumBar(rig);
    Session& session = rig.session();
    PatternScreen& drums = rig.app.drums();
    rig.settle();
    // The Session's side: a paste spends it, and the next finds nothing.
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done);
    CHECK(session.pasteBar(0, 1, 0) == Session::BarCopy::Done && !session.barClip().has);
    CHECK(sameDrums(rig.track(0).drums[1][0], rig.track(0).drums[0][0]));
    CHECK(session.pasteBar(0, 2, 0) == Session::BarCopy::NothingCopied && rig.track(0).drums[2][0].empty());
    // A Same and a refusal keep it: nothing was pasted.
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done);
    CHECK(session.pasteBar(0, 1, 0) == Session::BarCopy::Same && session.barClip().has);
    CHECK(session.pasteBar(0, 0, 2) == Session::BarCopy::NoTrack && session.barClip().has); // (a two-bar cell)
    CHECK(session.addTrack(TrackKind::Phazed));
    CHECK(session.pasteBar(1, 0, 0) == Session::BarCopy::DrumsOnDrums && session.barClip().has);
    // Through the drum grid: hold L1, A on bar 2 pastes; L1 + A again, on scene 3, places nothing.
    rig.project().track = 0;
    rig.project().scene = 0;
    rig.project().bar = 1;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 2" && sameDrums(rig.track(0).drums[0][1], rig.track(0).drums[0][0]) && !session.barClip().has);
    rig.project().scene = 2;
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "NOTHING COPIED YET" && rig.track(0).drums[2][0].empty());
    // L1 + R1 copies what is under the cursor, as it always did: scene 3's own bar, onto scene 4, spent again.
    drums.setStep(4, 2, 0.6f);
    rig.frames(1);
    rig.withL1(R1);
    CHECK(rig.said() == "BAR 1 COPIED" && session.barClip().has && session.barClip().scene == 2);
    rig.project().scene = 3;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 1" && rig.track(0).drums[3][0].level[4][2] == 0.6f && rig.track(0).drums[3][0].level[0][0] == 0.f);
    CHECK(!session.barClip().has);
}

// ---- notes -------------------------------------------------------------------------------------

NoteEvent note(int step, int pitch, float length, float velocity) {
    NoteEvent n;
    n.step = step, n.pitch = pitch, n.length = length, n.velocity = velocity;
    return n;
}

void writeNoteBar(TrackData& t) {
    NotePattern& p = t.notes[0][0];
    p = NotePattern();
    p.notes[0] = note(0, 60, 2.f, 0.9f);
    p.notes[1] = note(4, 64, 1.5f, 0.6f);
    p.notes[1].ratchet = 3, p.notes[1].glide = 2, p.notes[1].throwTo = kThrowSpace, p.notes[1].nudge = 0.1f;
    p.notes[2] = note(12, 67, 10.f, 0.8f); // over the bar line, six steps into the next bar
    ChordEvent& c = p.chords[0];
    c.step = 8, c.length = 12.f, c.root = 48, c.count = 3, c.velocity = 0.7f;
    c.intervals[0] = 0, c.intervals[1] = 4, c.intervals[2] = 7;
    std::snprintf(c.name, sizeof c.name, "C");
    c.play.strum = 1, c.play.time = 50;
}

void testNotes() {
    Rig rig;
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.session().setCellBars(1, 0, 2);
    rig.project().track = 1;
    writeNoteBar(rig.track(1));
    rig.session().sendNotePattern(1, 0);
    rig.session().setAutomation(1, 0, 0, 2, 0.3f); // macro 1 on step 3
    rig.settle();
    NoteScreen& roll = rig.app.notes();
    const int zoom = rig.session().octavesShown();

    // COPY on a note: R1 takes hold, and letting go unmoved copies the bar. L1 let go does not zoom.
    roll.moveCursorTo(60, 0);
    CHECK(roll.noteHere() != nullptr);
    rig.key(L1, true);
    rig.frames(1);
    CHECK(roll.barKeyHeld());
    rig.key(R1, true);
    rig.frames(1);
    CHECK(roll.carrying());
    rig.key(R1, false);
    CHECK(rig.said() == "BAR 1 COPIED");
    rig.key(L1, false);
    rig.frames(1);
    CHECK(rig.session().octavesShown() == zoom && !roll.barKeyHeld());
    CHECK(rig.session().barClip().has && !rig.session().barClip().drums);
    // ...but moved first, it was a carry: nothing copies (a note the paste below does not compare).
    const unsigned copiesBefore = rig.session().barCopies();
    roll.moveCursorTo(67, 12);
    rig.key(L1, true);
    rig.key(R1, true);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 79); // an octave, L1 held
    rig.key(R1, false);
    rig.key(L1, false);
    CHECK(rig.session().barCopies() == copiesBefore);

    // PASTE on bar 2, on an empty place: A places no note of its own. The long note is held to the cell's end.
    rig.project().bar = 1;
    rig.frames(1);
    roll.moveCursorTo(72, 7);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(A, true);
    rig.frames(1);
    rig.key(A, false);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(rig.said() == "PASTED ON BAR 2" && rig.session().octavesShown() == zoom && !rig.session().barClip().has); // (spent)
    const NotePattern& two = rig.track(1).notes[0][1];
    const NotePattern& one = rig.track(1).notes[0][0];
    CHECK(two.at(72, 7) < 0);
    CHECK(std::memcmp(&two.notes[0], &one.notes[0], sizeof(NoteEvent)) == 0 && std::memcmp(&two.notes[1], &one.notes[1], sizeof(NoteEvent)) == 0);
    CHECK(two.notes[2].velocity > 0.f && two.notes[2].length == 4.f && one.notes[2].length == 10.f); // to the end of the cell
    CHECK(two.chords[0].length == 8.f && one.chords[0].length == 12.f && two.chords[0].play.strum == 1); // the chord too
    {
        ChordEvent held = one.chords[0];
        held.length = 8.f;
        CHECK(std::memcmp(&two.chords[0], &held, sizeof(ChordEvent)) == 0);
    }
    CHECK(rig.track(1).automation[0][0][kSteps + 2] == 0.3f);
    rig.settle();
    rig.undo();
    CHECK(rig.track(1).notes[0][1].empty() && rig.track(1).automation[0][0][kSteps + 2] < 0.f);
    rig.redo();
    CHECK(rig.track(1).notes[0][1].notes[2].length == 4.f);

    // Another synth track, a cell of four bars: the long note keeps its length. No automation.
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    CHECK(rig.session().addTrack(TrackKind::Jx3p));
    rig.session().setCellBars(2, 0, 4);
    rig.project().track = 2;
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.track(2).notes[0][0].notes[2].length == 10.f && rig.track(2).automation[0][0][2] < 0.f);
    // ...which spent it: L1 + A on its bar 2 places nothing.
    rig.project().bar = 1;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "NOTHING COPIED YET" && rig.track(2).notes[0][1].empty());
    // A sampler takes notes too.
    rig.project().track = 1;
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it
    CHECK(rig.session().addTrack(TrackKind::Sampler));
    rig.project().track = 3;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "PASTED ON BAR 1" && rig.track(3).notes[0][0].notes[2].length == 16.f - 12.f);
    // A drum track does not.
    rig.project().track = 1;
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(R1); // again: the paste spent it (the refusal keeps it)
    rig.project().track = 0;
    rig.frames(1);
    rig.withL1(A);
    CHECK(rig.said() == "NOTES GO ON A NOTE TRACK" && rig.track(0).drums[0][0].empty());

    // L1 still zooms tapped alone (on its let go); held with a direction it steps bars: up / down
    // the bar shown, round the ends, left / right the cell's count. The cursor stays put.
    rig.project().track = 1;
    rig.frames(1);
    rig.session().setOctavesShown(2);
    rig.key(L1, true);
    rig.frames(1);
    CHECK(rig.session().octavesShown() == 2); // not on the press
    rig.key(L1, false);
    rig.frames(1);
    CHECK(rig.session().octavesShown() == 1);
    rig.session().setOctavesShown(2);
    rig.session().setCellBars(1, 0, 1);
    roll.moveCursorTo(72, 7);
    rig.withL1(RIGHT);
    CHECK(rig.session().octavesShown() == 2 && rig.track(1).bars[0] == 2 && roll.cursorStep() == 7); // bars, not zoomed
    rig.withL1(LEFT);
    CHECK(rig.track(1).bars[0] == 1);
    rig.withL1(DOWN); // one bar: round to it
    CHECK(rig.project().bar == 0 && roll.cursorStep() == 7);
    rig.session().setCellBars(1, 0, 4);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == 1 && roll.cursorStep() == 7);
    rig.withL1(UP);
    CHECK(rig.project().bar == 0);
    rig.withL1(UP);
    CHECK(rig.project().bar == 3); // round the ends
    // With X held on a note, L1 / R1 are its GLIDE FROM, never a copy.
    roll.moveCursorTo(64, 4);
    rig.project().bar = 0;
    rig.frames(1);
    const unsigned copies = rig.session().barCopies();
    const int from = rig.track(1).glideFrom;
    rig.key(X, true);
    rig.frames(1);
    rig.tap(L1);
    rig.tap(R1);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.session().barCopies() == copies && rig.track(1).glideFrom != from);
    // X held on an empty place (it hears the key): L1 and R1 copy nothing.
    roll.moveCursorTo(80, 14);
    CHECK(roll.noteHere() == nullptr);
    rig.key(X, true);
    rig.frames(1);
    rig.withL1(R1);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.session().barCopies() == copies);
    roll.moveCursorTo(64, 4);
    rig.frames(1);
    // R1 first, then L1: R1 carries the note and L1 + A does not paste (L1 was not pressed first).
    rig.session().setBarClip(Session::BarClip());
    rig.key(R1, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    CHECK(!roll.barKeyHeld());
    rig.key(L1, false);
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.session().barCopies() == copies);
}

// ---- the slice view, L1's tap, X held and a panic -------------------------------------------------

void testTheSlicesAndL1() {
    const std::string folder = tempFolder("copybar");
    CHECK(writeWav(folder + "/break.wav", std::vector<float>(48000, 0.25f), 48000));
    Rig rig(folder);
    rig.session().setLaneSample(0, 2, "break");
    rig.frames(2);
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(2, 3);
    CHECK(drums.canSlice(2));
    // L1 tapped: the slices, on its let go.
    rig.key(L1, true);
    rig.frames(1);
    CHECK(!drums.slicing());
    rig.key(L1, false);
    rig.frames(1);
    CHECK(drums.slicing());
    // In the slice view the gesture works the same, and L1 stays in it.
    drums.setStep(2, 3, 0.8f);
    drums.setStart(2, 3, 5.f / 16.f);
    rig.withL1(R1);
    CHECK(drums.slicing() && rig.said() == "BAR 1 COPIED" && !drums.painting());
    CHECK(rig.session().barClip().drumBar.start[2][3] == 5.f / 16.f);
    rig.session().addTrack(TrackKind::Drums);
    rig.project().track = 1;
    rig.frames(1);
    CHECK(!drums.slicing()); // (the other kit's lane has no recording)
    rig.withL1(A);
    CHECK(rig.track(1).drums[0][0].start[2][3] == 5.f / 16.f && rig.track(1).drums[0][0].level[2][3] == 0.8f);
    rig.project().track = 0;
    rig.frames(1);
    rig.key(L1, true);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(drums.slicing());
    // That paste spent the copy: L1 + A in the slice view finds nothing, and the view stays.
    rig.withL1(A);
    CHECK(drums.slicing() && rig.said() == "NOTHING COPIED YET" && rig.track(0).drums[0][0].level[2][3] == 0.8f);
    // A paste made in the slice view spends the copy too: bar 1 copied, pasted on bar 2, the view up throughout.
    rig.session().setCellBars(0, 0, 2); // (bar 2 a repeat of bar 1: a hit more on bar 1, so the paste is no Same)
    drums.setStep(2, 5, 0.4f);
    rig.frames(1);
    CHECK(drums.slicing());
    rig.withL1(R1);
    CHECK(drums.slicing() && rig.said() == "BAR 1 COPIED" && rig.session().barClip().has);
    rig.project().bar = 1;
    rig.frames(1);
    rig.withL1(A);
    CHECK(drums.slicing() && rig.said() == "PASTED ON BAR 2" && !rig.session().barClip().has);
    CHECK(rig.track(0).drums[0][1].level[2][5] == 0.4f && rig.track(0).drums[0][1].start[2][3] == 5.f / 16.f);
    rig.project().bar = 0;
    rig.frames(1);
    rig.tap(L1);
    CHECK(!drums.slicing());

    // L1 held with a direction steps bars, not the cursor or the slices: up / down the bar
    // shown, left / right the cell's count.
    rig.session().setCellBars(0, 0, 2);
    drums.moveCursorTo(2, 3);
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == 1 && drums.cursorLane() == 2 && drums.cursorStep() == 3 && !drums.slicing());
    rig.withL1(UP);
    CHECK(rig.project().bar == 0);
    rig.withL1(RIGHT);
    CHECK(rig.track(0).bars[0] == 4);
    rig.withL1(LEFT);
    CHECK(rig.track(0).bars[0] == 2);

    // X held on a hit: L1 and R1 are its REVERSE and CUT, not a copy.
    drums.moveCursorTo(2, 3);
    const unsigned copies = rig.session().barCopies();
    rig.key(X, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(R1);
    rig.key(L1, false);
    rig.frames(1);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.session().barCopies() == copies && drums.fx(2, 3).reverse == 1 && drums.fx(2, 3).cut == 1 && !drums.slicing());

    // A panic begun with L1 then R1: the copy that made is taken back, and the slices are as they were.
    drums.setStep(0, 0, 1.f);
    rig.frames(1);
    const DrumPattern before = rig.session().barClip().drumBar;
    CHECK(before.level[0][0] == 0.f);
    rig.key(L1, true);
    rig.key(R1, true);
    CHECK(rig.session().barCopies() == copies + 1); // (on the way, R1 copied)
    rig.key(L2, true);
    rig.key(R2, true);
    CHECK(rig.session().barClip().drumBar.level[0][0] == 0.f && rig.said().empty());
    for (SDL_Keycode k : {L1, L2, R2, R1}) rig.key(k, false);
    rig.frames(1);
    CHECK(!drums.slicing());
    rig.tap(A); // puts the notice away
    rig.frames(2);
    // A copy spent on the way stays spent: L1, A (pastes, and the copy goes), R1 (copies again), L2, R2. The panic
    // takes back R1's copy alone, the clipboard as it was just before R1: empty, not the copy L1 went down with.
    drums.moveCursorTo(2, 3);
    rig.frames(1);
    rig.withL1(R1);
    CHECK(rig.said() == "BAR 1 COPIED" && rig.session().barClip().has);
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(A);
    CHECK(rig.said() == "BAR 1 IS THE SAME ALREADY" && rig.session().barClip().has); // (a Same keeps it)
    rig.project().bar = 1;
    rig.frames(1);
    rig.tap(A);
    CHECK(rig.said() == "PASTED ON BAR 2" && !rig.session().barClip().has);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.session().barClip().has && rig.session().barClip().bar == 1); // (on the way, R1 copied bar 2)
    rig.key(L2, true);
    rig.key(R2, true);
    CHECK(!rig.session().barClip().has);
    for (SDL_Keycode k : {L1, L2, R2, R1}) rig.key(k, false);
    rig.frames(1);
    rig.tap(A);
    rig.frames(2);
    rig.project().bar = 0;
    // ...and a copy made on purpose before the panic's R1 stays: L1, R1 tapped (copies bar 1), R1 again (copies
    // bar 1 again), L2, R2: bar 1's copy, as the first R1 made it.
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(R1);
    CHECK(rig.said() == "BAR 1 COPIED" && rig.session().barClip().has && rig.session().barClip().bar == 0);
    const unsigned made = rig.session().barCopies();
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.session().barCopies() == made + 1);
    rig.key(L2, true);
    rig.key(R2, true);
    CHECK(rig.session().barClip().has && rig.session().barClip().bar == 0);
    for (SDL_Keycode k : {L1, L2, R2, R1}) rig.key(k, false);
    rig.frames(1);
    rig.tap(A);
    rig.frames(2);
    // ...and begun L1, L2, R2, R1 (the triggers turn the tabs on the way, R1 makes it a panic): L1 let
    // go after it opens no slices either.
    drums.moveCursorTo(2, 3);
    rig.frames(1);
    rig.key(L1, true);
    rig.key(L2, true);
    rig.key(R2, true);
    rig.key(R1, true);
    for (SDL_Keycode k : {L1, L2, R2, R1}) rig.key(k, false);
    rig.frames(1);
    CHECK(!drums.slicing() && rig.session().view == View::Pattern);
    rig.tap(A);
    rig.frames(2);
}

// ---- the engine hears it ---------------------------------------------------------------------------

void testTheEngineHearsIt() {
    // Drums: bar 2 pasted, beside a song with bar 2 written hit by hand.
    std::unique_ptr<Rig> pasted(new Rig), hand(new Rig), plain(new Rig);
    for (Rig* rig : {pasted.get(), hand.get(), plain.get()}) writeDrumBar(*rig);
    pasted->withL1(R1);
    pasted->project().bar = 1;
    pasted->frames(1);
    pasted->withL1(A);
    {
        hand->project().bar = 1;
        hand->frames(1);
        PatternScreen& drums = hand->app.drums();
        const DrumPattern& p = hand->track(0).drums[0][0];
        const DrumPattern copy = p;
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = 0; step < kSteps; ++step) {
                if (copy.level[lane][step] <= 0.f) continue;
                drums.setStep(lane, step, copy.level[lane][step]);
                drums.setNudge(lane, step, copy.nudge[lane][step]);
                drums.setChance(lane, step, copy.chance[lane][step]);
                drums.setStart(lane, step, copy.start[lane][step]);
                drums.setFx(lane, step, copy.fx[lane][step]);
            }
        hand->session().setAutomation(0, 0, 20, kSteps + 5, -1.f);
        hand->session().setAutomation(0, 0, 20, kSteps + 3, 0.7f);
        hand->frames(8);
        plain->frames(9);
    }
    // (The chance hit rolls the engine's own dice: the three are fresh, so they roll alike.)
    const std::vector<float> a = pasted->play(32), b = hand->play(32), c = plain->play(32);
    CHECK(a == b);
    CHECK(a != c);
    float loud = 0.f;
    for (std::size_t i = static_cast<std::size_t>(2 * 16 * kStep); i < a.size(); ++i) loud = std::fmax(loud, std::fabs(a[i]));
    CHECK(loud > 0.01f); // bar 2 sounds

    // Notes: a long note held to the cell's end, a chord strummed.
    std::unique_ptr<Rig> notesPasted(new Rig), notesHand(new Rig);
    for (Rig* rig : {notesPasted.get(), notesHand.get()}) {
        CHECK(rig->session().addTrack(TrackKind::Phazed));
        rig->session().setCellBars(1, 0, 2);
        rig->project().track = 1;
        writeNoteBar(rig->track(1));
        rig->session().sendNotePattern(1, 0);
        rig->frames(2);
    }
    notesPasted->withL1(R1);
    notesPasted->project().bar = 1;
    notesPasted->frames(1);
    notesPasted->withL1(A);
    {
        TrackData& t = notesHand->track(1);
        t.notes[0][1] = t.notes[0][0];
        t.notes[0][1].notes[2].length = 4.f;
        t.notes[0][1].chords[0].length = 8.f;
        notesHand->session().sendNoteBar(1, 0, 1);
        notesHand->frames(9); // (as many blocks as the gestures took: Phazed's own clocks run on in silence)
    }
    const std::vector<float> np = notesPasted->play(32), nh = notesHand->play(32);
    CHECK(np == nh);
    if (np != nh) {
        std::size_t first = 0;
        double most = 0;
        for (std::size_t i = 0; i < np.size(); ++i) { if (np[i] != nh[i] && !first) first = i; most = std::fmax(most, std::fabs(np[i] - nh[i])); }
        std::printf("  notes differ from frame %zu (step %.2f), most %g\n", first / 2, first / 2.0 / kStep, most);
    }
}

// ---- the words and the bar ---------------------------------------------------------------------------

void testTheWordsFit() {
    Canvas canvas;
    if (!canvas.ok) return;
    // Every line said fits the App's box (260 wide, 6 either side) in the lettering it is held to at XL: LARGE.
    using BC = Session::BarCopy;
    for (BC answer : {BC::Done, BC::Same, BC::NothingCopied, BC::DrumsOnDrums, BC::NotesOnNotes, BC::NoTrack})
        for (bool paste : {false, true}) {
            const std::string words = Session::barCopyWords(answer, paste, kMaxBars - 1);
            CHECK(!words.empty() && canvas.fonts->medium.width(words.c_str()) <= 248.f);
        }
    // The bar with L1 held says what R1 and A do, and it fits; on the BARS stop it says the
    // bar's own: left / right the bar, hold A more or fewer.
    Canvas scratch;
    const int top = ui::kScreenH - ui::kHintsH;
    const auto barIs = [&](const ui::Hint* list, int count) {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, list, count);
        return canvas.part(0, top, ui::kScreenW, ui::kHintsH) == scratch.part(0, top, ui::kScreenW, ui::kHintsH);
    };
    Rig rig;
    const auto drawn = [&] {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
    };
    const ui::Hint held[] = {{"UP / DOWN", "change bar"}, {"LEFT / RIGHT", "bar count"}, {"R1", "copy bar"}, {"A", "paste bar"}};
    const ui::Hint grid[] = {{"LEFT / RIGHT", "bar"}, {"HOLD A", "more / fewer"}, {"START", "play"}};
    const ui::Hint roll[] = {{"LEFT / RIGHT", "bar"}, {"HOLD A", "more / fewer"}};
    drawn();
    CHECK(!barIs(held, 4));
    rig.key(L1, true);
    rig.frames(1);
    drawn();
    CHECK(barIs(held, 4) && ui::gHintsOver == 0);
    rig.key(L1, false);
    rig.frames(1);
    rig.app.drums().moveCursorTo(0, 0);
    rig.tap(UP);
    rig.tap(UP);
    drawn();
    CHECK(rig.app.drums().cursorOnBars() && barIs(grid, 3) && ui::gHintsOver == 0);
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.frames(1);
    rig.app.notes().moveCursorTo(60, 0);
    rig.tap(UP);
    drawn();
    CHECK(rig.app.notes().cursorOnBars() && barIs(roll, 2) && ui::gHintsOver == 0);
    rig.key(L1, true);
    rig.frames(1);
    drawn();
    CHECK(barIs(roll, 2) && ui::gHintsOver == 0); // on BARS the bar's own hints stay, L1 held or not
    rig.key(L1, false);
    rig.frames(1);
    drawn();
    CHECK(barIs(roll, 2));
    // The line said is drawn in the App's box above the bar.
    rig.withL1(R1);
    drawn();
    CHECK(rig.said() == "BAR 1 COPIED");
    rig.frames(100); // (1.5 s: gone)
    CHECK(rig.said().empty());
    // From the grid, UP advertises BARS exactly where up leads to it.
    const ui::Hint tempo[] = {{"A", "tap"}, {"HOLD A", "tempo"}, {"START", "play"}, {"UP", "bars"}};
    rig.project().track = 0;
    rig.app.drums().moveCursorTo(0, 0);
    rig.tap(UP);
    drawn();
    CHECK(rig.app.drums().cursorOnTempo() && barIs(tempo, 4) && ui::gHintsOver == 0);
    rig.tap(DOWN);
    drawn();
    CHECK(!barIs(tempo, 4)); // back on the lanes: no UP bars
    const ui::Hint cell[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"UP", "bars"}};
    const ui::Hint cellDown[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"L1", "zoom"}};
    rig.project().track = 1;
    rig.app.notes().moveCursorTo(60, 0);
    rig.project().bar = 0;
    rig.frames(1);
    drawn();
    CHECK(barIs(cell, 5) && ui::gHintsOver == 0);
    rig.tap(DOWN);
    drawn();
    CHECK(barIs(cellDown, 5));
    // Two bars: up from the second bar's top step says where it goes, as down from an end does.
    rig.session().setCellBars(1, 0, 2);
    const ui::Hint prevBar[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"UP", "prev bar"}};
    rig.app.notes().moveCursorTo(60, 0);
    rig.project().bar = 1;
    rig.frames(1);
    drawn();
    CHECK(barIs(prevBar, 5) && ui::gHintsOver == 0);
    const ui::Hint nextBar[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"DOWN", "next bar"}};
    rig.app.notes().moveCursorTo(60, 15);
    rig.project().bar = 0;
    rig.frames(1);
    drawn();
    CHECK(barIs(nextBar, 5) && ui::gHintsOver == 0);
    // On a note at the very top step the way to BARS is still said, and fits.
    rig.app.notes().moveCursorTo(60, 0);
    rig.project().bar = 0;
    rig.tap(A);
    drawn();
    const ui::Hint onNote[] = {{"A", "level / length"}, {"B", "delete"}, {"HOLD Y", "chord"}, {"HOLD R1", "move"}, {"HOLD X", "effects"}, {"UP", "bars"}};
    CHECK(barIs(onNote, 6)); // all six show; the help yields, as on a crowded bar
}

} // namespace

// BARS IN THE ROLL: up from the top step is the BARS stop: hold A with up / down steps the
// cell's bars along 1, 2, 4, 8 (as the Song screen's hold A); left / right turn to the bar
// before or after, round the ends. L1 is never held: no zoom.
void testRollBarsLength() {
    Rig rig;
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.settle();
    rig.project().track = 1;
    NoteScreen& roll = rig.app.notes();
    CHECK(rig.track(1).bars[0] == 1);
    const int zoom = rig.session().octavesShown();
    roll.moveCursorTo(60, 0);
    rig.tap(UP);
    CHECK(roll.cursorOnBars());
    rig.key(A, true);
    rig.tap(UP);
    CHECK(rig.track(1).bars[0] == 2);
    rig.tap(UP);
    CHECK(rig.track(1).bars[0] == 4);
    rig.tap(DOWN);
    rig.tap(DOWN);
    CHECK(rig.track(1).bars[0] == 1);
    rig.tap(DOWN);
    CHECK(rig.track(1).bars[0] == 1); // the ends stop
    CHECK(roll.cursorOnBars() && roll.cursorPitch() == 60 && roll.cursorStep() == 0); // the cursor never moved
    rig.key(A, false);
    CHECK(rig.session().octavesShown() == zoom);
    // ...but not while shaping a note on the grid: A held there keeps up / down for its length.
    roll.moveCursorTo(60, 0);
    rig.key(A, true); // a note placed, a step long
    rig.tap(DOWN);
    CHECK(rig.track(1).bars[0] == 1 && roll.noteHere() && roll.noteHere()->length == 2.f);
    rig.key(A, false);
    // Left / right turn the bar shown: round the ends, the cursor keeping its place.
    rig.session().setCellBars(1, 0, 4);
    rig.project().bar = 0;
    roll.moveCursorTo(60, 3);
    rig.tap(UP, 4);
    CHECK(roll.cursorOnBars());
    rig.tap(RIGHT);
    CHECK(rig.project().bar == 1 && roll.cursorPitch() == 60 && roll.cursorStep() == 0);
    rig.tap(RIGHT, 3);
    CHECK(rig.project().bar == 0); // round the end
    rig.tap(LEFT);
    CHECK(rig.project().bar == 3); // ...and back round the start
    CHECK(rig.session().octavesShown() == zoom);
    // Down from BARS is back on the grid; up from BARS rounds to the bottom.
    rig.tap(DOWN);
    CHECK(!roll.cursorOnBars() && rig.project().bar == 3 && roll.cursorStep() == 0);
    roll.moveCursorTo(60, 0);
    rig.project().bar = 0;
    rig.tap(UP);
    CHECK(roll.cursorOnBars());
    rig.tap(UP);
    CHECK(!roll.cursorOnBars() && rig.project().bar == 3 && roll.cursorStep() == 15);
    // Shortening under the bar shown puts the roll back on its first bar.
    rig.session().setCellBars(1, 0, 1);
    rig.frames(1);
    CHECK(rig.project().bar == 0);
}

// BARS ON THE GRID: up from the tempo is the BARS stop: hold A with up / down steps the cell's
// bars; left / right turn the bar shown before or after, round the ends. L1 is never held.
void testDrumBarsLength() {
    Rig rig;
    PatternScreen& grid = rig.app.drums();
    CHECK(rig.track(0).bars[0] == 1);
    grid.moveCursorTo(0, 0);
    rig.tap(UP);
    CHECK(grid.cursorOnTempo());
    rig.tap(UP);
    CHECK(grid.cursorOnBars());
    rig.key(A, true);
    rig.tap(UP);
    CHECK(rig.track(0).bars[0] == 2);
    rig.tap(DOWN);
    CHECK(rig.track(0).bars[0] == 1);
    rig.key(A, false);
    CHECK(grid.cursorOnBars() && grid.cursorLane() == 0 && grid.cursorStep() == 0); // the cursor never moved
    // Left / right turn the bar shown: round the ends, the cursor keeping its place.
    rig.session().setCellBars(0, 0, 4);
    rig.project().bar = 1;
    rig.tap(LEFT);
    CHECK(rig.project().bar == 0 && grid.cursorLane() == 0 && grid.cursorStep() == 0);
    rig.tap(LEFT);
    CHECK(rig.project().bar == 3); // round the start
    rig.tap(RIGHT);
    CHECK(rig.project().bar == 0); // ...and back round the end
    // Held, with A or Y held too, it runs on as the press did (gesture audit G21: the press turned the bar and
    // the held repeat did nothing). 13 frames: the press, and one repeat after the cursor's 170 wait.
    for (SDL_Keycode with : {A, Y}) {
        rig.project().bar = 0;
        rig.key(with, true);
        rig.frames(1);
        rig.key(RIGHT, true);
        rig.frames(13);
        rig.key(RIGHT, false);
        CHECK(rig.project().bar == 2);
        rig.key(LEFT, true);
        rig.frames(15); // the press and two repeats: round the start
        rig.key(LEFT, false);
        rig.key(with, false);
        rig.frames(1);
        CHECK(rig.project().bar == 3 && rig.track(0).bars[0] == 4 && grid.cursorOnBars());
    }
    // Down from BARS is back through the tempo to the lanes; up from BARS rounds to the bottom.
    rig.tap(DOWN);
    CHECK(!grid.cursorOnBars() && grid.cursorOnTempo());
    rig.tap(DOWN);
    CHECK(!grid.cursorOnTempo() && grid.cursorLane() == 0);
    rig.tap(UP);
    CHECK(grid.cursorOnTempo());
    rig.tap(UP);
    CHECK(grid.cursorOnBars());
    rig.tap(UP);
    CHECK(!grid.cursorOnBars() && !grid.cursorOnTempo() && grid.cursorLane() == kDrumLanes - 1);
}

// A bar copied, then tracks coming or going: the copy tracks its track, or goes with it.
void testBarCopyTracksItsTrack() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Drums));
    const int lane = autoLaneOfEffect(0, 0);
    session.setAutomation(1, 0, lane, 2, 0.5f);
    rig.settle();
    CHECK(session.copyBar(1, 0, 0) == Session::BarCopy::Done);
    CHECK(session.removeTrack(0)); // track 1 moves to 0; the copy tracks it
    CHECK(session.pasteBar(0, 1, 0) == Session::BarCopy::Done);
    CHECK(rig.track(0).automation[1][lane][2] == 0.5f); // the lanes land: the same track
    // ...and removing the source track clears the copy.
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done);
    CHECK(session.addTrack(TrackKind::Drums));
    CHECK(session.removeTrack(0));
    CHECK(session.pasteBar(0, 0, 0) == Session::BarCopy::NothingCopied);
    // Duplicating ahead of it shifts it down with its track (pasted back onto that track, its lanes land).
    CHECK(session.addTrack(TrackKind::Drums));
    session.setAutomation(1, 0, lane, 2, 0.5f);
    CHECK(session.copyBar(1, 0, 0) == Session::BarCopy::Done);
    CHECK(session.duplicateTrack(0) == 1);
    CHECK(session.pasteBar(2, 1, 0) == Session::BarCopy::Done);
    CHECK(rig.track(2).automation[1][lane][2] == 0.5f);
}

// The gesture audit's G31 (2026-11-24): a stick move with L1 held is a use of L1, as a button press is.
// Its let go then opens no slices on the drum grid and zooms nothing on the roll; a plain tap still does.
void testAStickMoveIsAUseOfL1() {
    const std::string folder = tempFolder("copybar-stick");
    CHECK(writeWav(folder + "/break.wav", std::vector<float>(48000, 0.25f), 48000));
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, 2, "break");
    rig.frames(2);
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(2, 3);
    CHECK(drums.canSlice(2));
    const auto stickWithL1 = [&](SDL_Keycode way) {
        rig.key(L1, true);
        rig.frames(1);
        rig.key(way, true);
        rig.frames(1);
        rig.key(way, false);
        rig.frames(1);
        rig.key(L1, false);
        rig.frames(1);
    };
    stickWithL1(STICK_RIGHT);
    CHECK(drums.cursorStep() == 4 && !drums.slicing()); // the stick moved the cursor, and that was L1's hold
    rig.key(L1, true);
    rig.frames(1);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(drums.slicing()); // a plain tap still opens them
    stickWithL1(STICK_DOWN);
    CHECK(drums.slicing()); // ...and with them open, a stick move under L1 does not shut them
    rig.key(L1, true);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(!drums.slicing());
    // The roll: the same for its zoom.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    roll.moveCursorTo(60, 4);
    const int octaves = session.octavesShown();
    stickWithL1(STICK_DOWN);
    CHECK(roll.cursorStep() == 5 && session.octavesShown() == octaves);
    stickWithL1(STICK_RIGHT);
    CHECK(roll.cursorPitch() == 61 && session.octavesShown() == octaves);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(session.octavesShown() != octaves); // a plain tap still zooms
}

// A paste is its own step even with an unmarked change in flight: the drift keeps its own step.
void testPasteSeparatesUnmarkedDrift() {
    Rig rig;
    Session& session = rig.session();
    rig.track(0).drums[0][0].level[0][0] = 0.9f;
    rig.settle();
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done);
    rig.project().tracks[0].laneLevel[0] = 0.5f; // unmarked drift, as a direct edit would leave
    CHECK(session.pasteBar(0, 1, 0) == Session::BarCopy::Done);
    rig.settle();
    rig.undo();
    CHECK(rig.track(0).drums[1][0].level[0][0] == 0.f && rig.project().tracks[0].laneLevel[0] == 0.5f);
    rig.undo();
    CHECK(rig.project().tracks[0].laneLevel[0] != 0.5f);
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testDrums", testDrums}, {"testNotes", testNotes}, {"testTheSlicesAndL1", testTheSlicesAndL1}, {"testTheEngineHearsIt", testTheEngineHearsIt}, {"testTheWordsFit", testTheWordsFit},
        {"testRollBarsLength", testRollBarsLength}, {"testDrumBarsLength", testDrumBarsLength},
        {"testBarCopyTracksItsTrack", testBarCopyTracksItsTrack}, {"testPasteSeparatesUnmarkedDrift", testPasteSeparatesUnmarkedDrift},
        {"testAStickMoveIsAUseOfL1", testAStickMoveIsAUseOfL1}, {"testAPasteSpendsTheCopy", testAPasteSpendsTheCopy},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("copybar_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
