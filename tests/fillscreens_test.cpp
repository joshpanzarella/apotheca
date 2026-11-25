// The FILL's screens (docs/plans/fills-ideas.md, design 1; the engine and the Session's calls: fillapp_test).
// Through the real App: hold L1 + Y on the drum grid and the roll makes the FILL as a copy of the bar shown and
// goes onto it, from another bar goes to it, on it turns it off and on (its bar kept); an eight-bar cell says why
// it has none. Y's let go after it rolls no dice, steps no chance, places no maybe hit and no chord; L1's let go
// after it opens no slices and zooms nothing. The ring of bars (hold L1 + UP / DOWN, the BARS stop, the cursor run
// off a bar's end): 1 ... the last bar, the FILL, 1. The view stays on the FILL (the guards), copy and paste a bar
// go onto it and from it, the top bar reads DRUMS / FILL / SCENE 1, the hints say what Y does and fit, the playhead
// shows on the FILL while it plays (and not on the bar it plays instead of), the step numbers and the shading are
// the scene's last bar's, a note on the FILL runs to the FILL's end, a slice placed on it is the bar it stands
// for's, and undoing a NEXT SCENE keeps the view on it.

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/fill_rule.hpp"
#include "apotheca/glide_rule.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "pattern_layout.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, L1 = SDLK_q, R1 = SDLK_w, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT,
                  RIGHT = SDLK_RIGHT, SELECT = SDLK_BACKSPACE;

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
    // Hold L1, tap `code`, let L1 go: the bar key's gestures.
    void withL1(SDL_Keycode code) {
        key(L1, true);
        frames(1);
        tap(code);
        key(L1, false);
        frames(1);
    }
    // A frame of the program and 16 ms of sound, as on the handheld.
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 768);
            engine->render(out.data(), 768);
        }
    }
    void undo() {
        key(SELECT, true);
        tap(B);
        key(SELECT, false);
        frames(1);
    }
    std::string said() { return app.undoSaid(); }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    TrackData& track(int t) { return project().tracks[t]; }
};

bool sameDrums(const DrumPattern& a, const DrumPattern& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// ---- the ring and the bars of the scene, by themselves ----------------------------------------------

void testTheRing() {
    Project p;
    TrackData& t = p.tracks[0];
    // No FILL: the cell's own bars, round the ends.
    t.bars[0] = 2;
    CHECK(t.barAfter(0, 0, 1) == 1 && t.barAfter(0, 1, 1) == 0 && t.barAfter(0, 0, -1) == 1);
    CHECK(!t.onFill(0, kFillSlot));
    // Made: after the last bar, the FILL; after the FILL, bar 1; before bar 1, the FILL. On or off alike.
    for (std::uint8_t state : {kFillOn, kFillOff}) {
        t.fill[0] = state;
        CHECK(t.barAfter(0, 1, 1) == kFillSlot && t.barAfter(0, kFillSlot, 1) == 0);
        CHECK(t.barAfter(0, 0, -1) == kFillSlot && t.barAfter(0, kFillSlot, -1) == 1);
        CHECK(t.barAfter(0, 0, 2) == kFillSlot && t.barAfter(0, kFillSlot, 3) == kFillSlot); // (several at once: round)
        CHECK(t.onFill(0, kFillSlot) && !t.onFill(0, 1));
    }
    t.bars[0] = 1;
    CHECK(t.barAfter(0, 0, 1) == kFillSlot && t.barAfter(0, kFillSlot, 1) == 0 && t.barAfter(0, kFillSlot, -1) == 0);
    // Eight bars: the eighth is its own; no FILL in the ring.
    t.bars[0] = 8;
    CHECK(t.barAfter(0, 6, 1) == 7 && t.barAfter(0, 7, 1) == 0 && !t.onFill(0, kFillSlot));
    // The scene's last bar, and where a cell's bar is counted.
    t.bars[0] = 1;
    p.tracks[1].kind = TrackKind::Drums;
    p.tracks[1].bars[0] = 4;
    CHECK(p.lastBar(0) == 3 && p.sceneBarOf(0, 0, kFillSlot) == 3 && p.sceneBarOf(0, 0, 0) == 0);
    CHECK(p.ownBarOf(0, 0, kFillSlot) == 0 && p.ownBarOf(0, 0, 0) == 0);
    t.bars[0] = 2;
    CHECK(p.ownBarOf(0, 0, kFillSlot) == 1 && p.sceneBarOf(0, 0, 1) == 1);
    p.steps[0] = 20; // cut short: two bars and four steps, the last bar the second
    CHECK(p.lastBar(0) == 1 && p.sceneBarOf(0, 0, kFillSlot) == 1 && p.ownBarOf(0, 0, kFillSlot) == 1);
    p.steps[0] = 16;
    CHECK(p.lastBar(0) == 0 && p.ownBarOf(0, 0, kFillSlot) == 0);
    p.steps[0] = 0;
    p.sceneMeter[0] = 12; // 3/4: four bars of twelve
    CHECK(p.lastBar(0) == 3);
    t.fill[0] = kFillNone;
    CHECK(p.sceneBarOf(0, 0, kFillSlot) == kFillSlot); // (no FILL: the bar as it is)
}

// ---- the gesture on the drum grid ------------------------------------------------------------------

void testTheGestureOnTheGrid() {
    using F = Session::Fill;
    Rig rig;
    Session& session = rig.session();
    TrackData& t = rig.track(0);
    PatternScreen& drums = rig.app.drums();
    session.setCellBars(0, 0, 2);
    drums.setStep(1, 2, 0.8f); // bar 1: a hit on lane 2's third step
    rig.project().bar = 1;
    rig.frames(1);
    drums.setStep(3, 9, 0.6f); // bar 2: lane 4, step 10
    drums.moveCursorTo(5, 5);  // (an empty step)
    const int steps = session.undoSteps();
    // L1 + Y on bar 2: the FILL made as a copy of bar 2 (the bar shown), and the view on it.
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOn && rig.project().bar == kFillSlot);
    CHECK(sameDrums(t.drums[0][kFillSlot], t.drums[0][1]) && t.drums[0][kFillSlot].level[1][2] == 0.f);
    CHECK(rig.said() == "FILL MADE" && session.undoSteps() == steps + 1);
    // Y's press placed no maybe hit, its let go nothing; L1's let go opened no slices (nothing was said).
    CHECK(drums.level(5, 5) == 0.f && t.drums[0][1].level[5][5] == 0.f && std::string(drums.said()).empty());
    // The view stays on it, frame after frame (the guards).
    rig.frames(10);
    CHECK(rig.project().bar == kFillSlot);
    // On it, L1 + Y turns it off: kept, the view stays.
    drums.setStep(0, 15, 0.9f);
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOff && rig.project().bar == kFillSlot && rig.said() == "FILL OFF");
    CHECK(t.drums[0][kFillSlot].level[0][15] == 0.9f && t.drums[0][kFillSlot].level[3][9] == 0.6f);
    rig.frames(10);
    CHECK(rig.project().bar == kFillSlot); // (off, it is still a bar to stand on)
    // ...and on again, as it was.
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOn && rig.said() == "FILL ON" && t.drums[0][kFillSlot].level[0][15] == 0.9f);
    // From another bar L1 + Y goes to it, and changes nothing.
    rig.project().bar = 0;
    rig.frames(1);
    const int before = session.undoSteps();
    rig.withL1(Y);
    CHECK(rig.project().bar == kFillSlot && t.fill[0] == kFillOn && session.undoSteps() == before);
    CHECK(session.fillKeyHere() == F::Off); // (on it: off, as the gesture)
    {
        // makeFill given a bar the cell does not play (a hidden one): the bar it plays instead of, F1's default.
        Rig other;
        TrackData& o = other.track(0);
        o.drums[0][0].level[0][0] = 0.9f;
        o.drums[0][1].level[1][1] = 0.9f; // (hidden: the cell is one bar)
        CHECK(other.session().makeFill(0, 0, 1) == F::Made && o.drums[0][kFillSlot].level[0][0] == 0.9f && o.drums[0][kFillSlot].level[1][1] == 0.f);
    }
    rig.project().bar = 1;
    CHECK(session.fillKeyHere() == F::Shown && rig.project().bar == kFillSlot && t.fill[0] == kFillOff); // (off, gone to: still off)
    // Undo takes the making back: no FILL, the view off it.
    for (int i = 0; i < 12 && session.undoSteps() > steps; ++i) rig.undo();
    rig.frames(2);
    CHECK(t.fill[0] == kFillNone && rig.project().bar != kFillSlot);

    // A plain Y is as ever: on an empty step a maybe hit.
    drums.moveCursorTo(5, 5);
    rig.project().bar = 0;
    rig.frames(1);
    rig.tap(Y);
    CHECK(drums.level(5, 5) > 0.f);
    // L1 held first only: Y with L1 pressed after it is Y's own.
    drums.moveCursorTo(5, 6);
    rig.key(Y, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(Y, false);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(t.fill[0] == kFillNone && drums.level(5, 6) > 0.f);
    // ...and with A held on a hit first (shaping it), L1 then Y is not the FILL either.
    rig.key(A, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(Y);
    rig.key(L1, false);
    rig.key(A, false);
    rig.frames(1);
    CHECK(t.fill[0] == kFillNone);
    // With X held on a hit, Y is the dice for it, not the FILL; on an empty step (X only hears), not the FILL
    // either, as L1 + R1 / A are not with X held.
    for (int step : {5, 9}) {
        drums.moveCursorTo(5, step);
        rig.key(L1, true);
        rig.frames(1);
        rig.key(X, true);
        rig.frames(1);
        rig.tap(Y);
        rig.key(X, false);
        rig.key(L1, false);
        rig.frames(1);
        CHECK(t.fill[0] == kFillNone);
    }
}

// Y's let go after L1 + Y: no lane dice on a name, no chance step on a hit; its hold is not the dice.
void testYIsSwallowedOnTheGrid() {
    Rig rig;
    TrackData& t = rig.track(0);
    PatternScreen& drums = rig.app.drums();
    drums.setStep(2, 4, 0.8f);
    drums.setStep(2, 12, 0.8f);
    // On a lane's name: a tap of Y rolls the dice there (the lane's hits change); after L1 + Y nothing.
    drums.moveCursorTo(2, -1);
    rig.frames(1);
    const DrumPattern was = t.drums[0][0];
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOn && rig.project().bar == kFillSlot);
    CHECK(sameDrums(t.drums[0][kFillSlot], was) && sameDrums(t.drums[0][0], was));
    // Its hold is not the dice: Y kept down after it, UP on the name adds no hit.
    rig.key(L1, true);
    rig.frames(1);
    rig.key(Y, true); // (on the FILL: turns it off)
    rig.frames(1);
    rig.key(L1, false);
    rig.frames(1);
    rig.tap(UP);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(t.fill[0] == kFillOff && sameDrums(t.drums[0][kFillSlot], was));
    CHECK(drums.cursorLane() == 1); // (UP moved the cursor, as with nothing held)
    // On a hit: a tap of Y steps its chance down; after L1 + Y it is as it was.
    drums.moveCursorTo(2, 4);
    rig.frames(1);
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOn && t.drums[0][kFillSlot].chance[2][4] == 1.f);
    // (and the plain tap does step it: the check above can see a step)
    rig.tap(Y);
    CHECK(t.drums[0][kFillSlot].chance[2][4] < 1.f);
}

// The ring on the drum grid: hold L1 + UP / DOWN, the BARS stop's LEFT / RIGHT, the cursor off a bar's end.
void testTheRingOnTheGrid() {
    Rig rig;
    Session& session = rig.session();
    PatternScreen& drums = rig.app.drums();
    session.setCellBars(0, 0, 2);
    rig.frames(1);
    CHECK(session.makeFill(0, 0) == Session::Fill::Made);
    rig.project().bar = 0;
    rig.frames(1);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == 1);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == kFillSlot);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == 0);
    rig.withL1(UP);
    CHECK(rig.project().bar == kFillSlot);
    rig.withL1(UP);
    CHECK(rig.project().bar == 1);
    // The cursor off the right of the last bar's step 16: the FILL's step 1; off the FILL's: bar 1's name.
    drums.moveCursorTo(0, 15);
    rig.tap(RIGHT);
    CHECK(rig.project().bar == kFillSlot && drums.cursorStep() == 0);
    drums.moveCursorTo(0, 15);
    rig.tap(RIGHT);
    CHECK(rig.project().bar == 0 && drums.cursorStep() == -1);
    rig.tap(LEFT); // left of bar 1's name: the FILL's step 16
    CHECK(rig.project().bar == kFillSlot && drums.cursorStep() == 15);
    drums.moveCursorTo(0, -1);
    rig.tap(LEFT); // left of the FILL's name: the last bar's step 16
    CHECK(rig.project().bar == 1 && drums.cursorStep() == 15);
    // The BARS stop (up from the tempo): LEFT / RIGHT the same ring.
    drums.moveCursorTo(0, 3);
    rig.tap(UP);
    rig.tap(UP);
    CHECK(drums.cursorOnBars());
    rig.tap(RIGHT);
    CHECK(rig.project().bar == kFillSlot);
    rig.tap(RIGHT);
    CHECK(rig.project().bar == 0);
    rig.tap(LEFT);
    CHECK(rig.project().bar == kFillSlot);
    // L1 + LEFT / RIGHT on the FILL: the cell's count, the view kept on it (four bars: it stays a FILL).
    drums.moveCursorTo(0, 3);
    rig.withL1(RIGHT);
    CHECK(rig.track(0).bars[0] == 4 && rig.project().bar == kFillSlot && rig.track(0).fillOn(0));
    rig.frames(3);
    CHECK(rig.project().bar == kFillSlot);
}

// Copy a bar onto the FILL and from it, with the bar key, as onto any bar.
void testCopyAndPasteOnTheFill() {
    Rig rig;
    TrackData& t = rig.track(0);
    PatternScreen& drums = rig.app.drums();
    rig.session().setCellBars(0, 0, 2);
    drums.setStep(4, 4, 0.7f);
    rig.withL1(Y); // made from bar 1
    CHECK(rig.project().bar == kFillSlot && t.drums[0][kFillSlot].level[4][4] == 0.7f);
    rig.project().bar = 1;
    rig.frames(1);
    drums.setStep(6, 6, 0.5f);
    rig.withL1(R1); // bar 2 copied
    rig.withL1(DOWN);
    CHECK(rig.project().bar == kFillSlot);
    rig.withL1(A); // ...pasted on the FILL
    CHECK(sameDrums(t.drums[0][kFillSlot], t.drums[0][1]) && rig.said() == "PASTED ON THE FILL");
    rig.withL1(A);
    CHECK(rig.said() == "THE FILL IS THE SAME");
    drums.setStep(7, 0, 1.f);
    rig.withL1(R1); // the FILL copied
    CHECK(rig.said() == "FILL COPIED");
    rig.withL1(DOWN); // bar 1
    rig.withL1(A);
    CHECK(rig.project().bar == 0 && t.drums[0][0].level[7][0] == 1.f && t.fill[0] == kFillOn);
}

// An eight-bar cell has no FILL: L1 + Y says why, and nothing changes.
void testEightBars() {
    Rig rig;
    rig.session().setCellBars(0, 0, 8);
    rig.project().bar = 3;
    rig.frames(1);
    const int steps = rig.session().undoSteps();
    rig.withL1(Y);
    CHECK(rig.said() == "NO FILL ON 8 BARS" && rig.track(0).fill[0] == kFillNone && rig.project().bar == 3 && rig.session().undoSteps() == steps);
    CHECK(Session::fillKeyHint(rig.track(0), 0, 3) == nullptr);
    // On the roll too.
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.session().setCellBars(1, 0, 8);
    rig.frames(1);
    rig.withL1(Y);
    CHECK(rig.said() == "NO FILL ON 8 BARS" && rig.track(1).fill[0] == kFillNone && !rig.app.notes().picking());
}

// ---- the roll -----------------------------------------------------------------------------------------

void testTheGestureOnTheRoll() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    session.setCellBars(1, 0, 2);
    rig.frames(2);
    TrackData& t = rig.track(1);
    NoteScreen& roll = rig.app.notes();
    NoteEvent n;
    n.step = 2;
    n.pitch = 62;
    n.length = 2.f;
    n.velocity = 0.8f;
    t.notes[0][0].notes[0] = n;
    session.sendNotePattern(1, 0);
    roll.moveCursorTo(60, 6);
    rig.frames(1);
    const int octaves = session.octavesShown();
    // L1 + Y on bar 1: made from it, the view on it; no chord in hand, none placed; no zoom on L1's let go.
    rig.key(L1, true);
    rig.frames(1);
    rig.key(Y, true);
    rig.frames(2);
    CHECK(!roll.picking());
    rig.key(Y, false);
    rig.frames(1);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(t.fill[0] == kFillOn && rig.project().bar == kFillSlot && rig.said() == "FILL MADE");
    CHECK(t.notes[0][kFillSlot].notes[0].pitch == 62 && t.notes[0][kFillSlot].chordAt(6) < 0 && t.notes[0][0].chordAt(6) < 0);
    CHECK(session.octavesShown() == octaves);
    rig.frames(10);
    CHECK(rig.project().bar == kFillSlot); // (the guard keeps it)
    // A plain Y still picks a chord (and places it on its let go).
    rig.tap(Y);
    CHECK(t.notes[0][kFillSlot].chordAt(6) >= 0);
    // L1 not first (A held on a note, then L1): Y is not the FILL's. Nor with X held on an empty key.
    rig.project().bar = 0;
    roll.moveCursorTo(62, 2);
    rig.frames(1);
    rig.key(A, true);
    rig.frames(1);
    rig.key(L1, true);
    rig.frames(1);
    rig.tap(Y);
    rig.key(L1, false);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.project().bar == 0 && t.fill[0] == kFillOn); // (no jump to it, no change)
    roll.moveCursorTo(70, 9);
    rig.key(L1, true);
    rig.frames(1);
    rig.key(X, true);
    rig.frames(1);
    rig.key(Y, true);
    rig.frames(1);
    CHECK(roll.picking()); // Y's own: a chord in hand
    rig.key(Y, false);
    rig.key(X, false);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(rig.project().bar == 0 && t.fill[0] == kFillOn && t.notes[0][0].chordAt(9) >= 0); // (the chord Y placed)
    rig.project().bar = kFillSlot;
    rig.frames(1);
    // On it: off and on; from another bar: to it.
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOff && rig.project().bar == kFillSlot && rig.said() == "FILL OFF");
    rig.withL1(Y);
    CHECK(t.fill[0] == kFillOn && rig.said() == "FILL ON");
    rig.withL1(DOWN);
    CHECK(rig.project().bar == 0);
    rig.withL1(Y);
    CHECK(rig.project().bar == kFillSlot && t.fill[0] == kFillOn);
    // The ring: L1 + UP / DOWN, and the cursor run down off a bar's end.
    rig.withL1(UP);
    CHECK(rig.project().bar == 1);
    rig.withL1(DOWN);
    CHECK(rig.project().bar == kFillSlot);
    roll.moveCursorTo(60, 15);
    rig.project().bar = 1;
    rig.frames(1);
    rig.tap(DOWN); // off the last bar's end: the FILL's first step
    CHECK(rig.project().bar == kFillSlot && roll.cursorStep() == 0);
    rig.tap(UP); // and back
    CHECK(rig.project().bar == 1 && roll.cursorStep() == 15);
    roll.moveCursorTo(60, 31); // (sixteen steps on from the last bar's step 16: the FILL's last)
    CHECK(rig.project().bar == kFillSlot && roll.cursorStep() == 15);
    rig.tap(DOWN); // off the FILL's end: bar 1's first step
    CHECK(rig.project().bar == 0 && roll.cursorStep() == 0);
    roll.moveCursorTo(60, -1); // before bar 1: the FILL's last step
    CHECK(rig.project().bar == kFillSlot && roll.cursorStep() == 15);
    roll.moveCursorTo(60, 33); // two bars and two steps on from the FILL: round to bar 2's second step
    CHECK(rig.project().bar == 1 && roll.cursorStep() == 1);
    rig.project().bar = kFillSlot;
    roll.moveCursorTo(60, -17); // seventeen before the FILL's first: bar 1's last (past bar 2)
    CHECK(rig.project().bar == 0 && roll.cursorStep() == 15);
    rig.project().bar = kFillSlot;
    roll.moveCursorTo(60, -1); // one before the FILL's first: the last bar's last
    CHECK(rig.project().bar == 1 && roll.cursorStep() == 15);
    // The BARS stop: up from step 1 of bar 1; LEFT / RIGHT the ring; up again comes round to the FILL's end.
    rig.project().bar = 0;
    roll.moveCursorTo(60, 0);
    rig.frames(1);
    rig.tap(UP);
    CHECK(roll.cursorOnBars());
    rig.tap(LEFT);
    CHECK(rig.project().bar == kFillSlot);
    rig.tap(RIGHT);
    CHECK(rig.project().bar == 0);
    rig.tap(UP);
    CHECK(!roll.cursorOnBars() && rig.project().bar == kFillSlot && roll.cursorStep() == 15);
}

// A note on the FILL runs to the FILL's end (it is a bar of its own), not past it nor short of it.
void testANoteOnTheFill() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.frames(2);
    TrackData& t = rig.track(1);
    NoteScreen& roll = rig.app.notes();
    rig.withL1(Y);
    CHECK(rig.project().bar == kFillSlot);
    roll.moveCursorTo(60, 10);
    rig.tap(A); // a note, one step long
    const int slot = t.notes[0][kFillSlot].at(60, 10);
    CHECK(slot >= 0);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(DOWN, 12); // longer, a step a press: to the FILL's end (six steps), no further
    rig.key(A, false);
    rig.frames(1);
    CHECK(slot >= 0 && t.notes[0][kFillSlot].notes[slot].length == 6.f);
}

// ---- what is drawn ---------------------------------------------------------------------------------

void testTheTopBarAndTheHints() {
    Canvas canvas, scratch;
    if (!canvas.ok) return;
    Rig rig;
    Session& session = rig.session();
    const int top = ui::kScreenH - ui::kHintsH;
    const auto drawn = [&] {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
    };
    // The top bar as the screen drew it is the bar drawn with these words.
    const auto topReads = [&](const char* words) {
        scratch.clear();
        ui::topBar(scratch.r, *scratch.fonts, View::Pattern, words, rig.project());
        return canvas.part(0, 0, ui::kScreenW, ui::kTopBarH) == scratch.part(0, 0, ui::kScreenW, ui::kTopBarH);
    };
    const auto barIs = [&](const ui::Hint* list, int count) {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, list, count);
        return canvas.part(0, top, ui::kScreenW, ui::kHintsH) == scratch.part(0, top, ui::kScreenW, ui::kHintsH);
    };
    char name[24];
    ui::trackName(rig.project(), 0, name, sizeof name);
    const std::string drumsFill = std::string(name) + " / FILL / SCENE 1", drumsOff = std::string(name) + " / FILL OFF / SCENE 1",
                      drumsBar = std::string(name) + " / BAR 1 OF 1 / SCENE 1";
    drawn();
    CHECK(topReads(drumsBar.c_str()));
    // L1 held: Y's hint says what it would do, and the bar fits.
    const auto held = [&](const char* fill) {
        rig.key(L1, true);
        rig.frames(1);
        drawn();
        const ui::Hint list[] = {{"UP / DOWN", "change bar"}, {"LEFT / RIGHT", "bar count"}, {"R1", "copy bar"}, {"A", "paste bar"}, {"Y", fill}};
        const bool is = barIs(list, fill ? 5 : 4) && ui::gHintsOver == 0;
        if (ui::gHintsOver != 0) std::printf("  Y %s: %.0f pixels over\n", fill ? fill : "(none)", static_cast<double>(ui::gHintsOver));
        rig.key(L1, false);
        rig.frames(1);
        return is;
    };
    CHECK(held("make fill"));
    rig.withL1(Y);
    drawn();
    CHECK(topReads(drumsFill.c_str()));
    CHECK(held("fill off"));
    rig.withL1(Y);
    drawn();
    CHECK(topReads(drumsOff.c_str()));
    CHECK(held("fill on"));
    rig.withL1(DOWN);
    CHECK(held("go to fill"));
    session.setCellBars(0, 0, 8);
    rig.frames(1);
    CHECK(held(nullptr)); // eight bars: no Y
    // The roll the same.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.frames(2);
    ui::trackName(rig.project(), 1, name, sizeof name);
    const std::string rollFill = std::string(name) + " / FILL / SCENE 1";
    CHECK(held("make fill"));
    rig.withL1(Y);
    drawn();
    CHECK(topReads(rollFill.c_str()));
    CHECK(held("fill off"));
    rig.withL1(UP);
    CHECK(held("go to fill"));
    // On the last bar's last step (not on a note), DOWN is said to lead on: into the FILL; on the FILL's last, it
    // comes round, and the bar says the zoom instead.
    rig.app.notes().moveCursorTo(40, 15);
    rig.project().bar = 0;
    rig.frames(1);
    drawn();
    const ui::Hint intoFill[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"DOWN", "next bar"}};
    CHECK(barIs(intoFill, 5));
    rig.project().bar = kFillSlot;
    rig.frames(1);
    drawn();
    const ui::Hint round[] = {{"A", "place"}, {"HOLD Y", "chord"}, {"HOLD R1", "paint"}, {"HOLD B", "delete"}, {"L1", "zoom"}};
    CHECK(barIs(round, 5));
    // The words said for a copy or a paste on the FILL fit the App's box (260 wide, 6 either side) at LARGE, what
    // it holds them to at XL (fillapp_test testTheWords holds the FILL's own).
    for (Session::BarCopy answer : {Session::BarCopy::Done, Session::BarCopy::Same})
        for (bool paste : {false, true}) {
            const std::string words = Session::barCopyWords(answer, paste, kFillSlot, true);
            CHECK(words.find("FILL") != std::string::npos && canvas.fonts->medium.width(words.c_str()) <= 248.f);
        }
    CHECK(Session::barCopyWords(Session::BarCopy::Done, false, kFillSlot) == "BAR 8 COPIED"); // (an eighth bar of its own)
}

// The FILL (empty) of a one-bar cell in a scene of two bars, cut short to twenty steps, is drawn as an empty
// second bar would be: its step numbers 17 to 32, the steps past the scene's end shaded, STEP in the strip.
void testTheNumbersAndTheShading() {
    Canvas canvas;
    if (!canvas.ok) return;
    for (bool synth : {false, true}) {
        Rig rig;
        Session& session = rig.session();
        CHECK(session.addTrack(TrackKind::Drums));
        session.setCellBars(1, 0, 2); // the scene: two bars (the other track's)
        session.setSceneSteps(0, 20);
        if (synth) CHECK(session.setTrackKind(0, TrackKind::Phazed));
        rig.frames(2);
        CHECK(session.makeFill(0, 0) == Session::Fill::Made && session.emptyFill(0, 0) != Session::Fill::NoFill);
        CHECK(session.goToFill(0, 0));
        if (synth) rig.app.notes().moveCursorTo(60, 3);
        else rig.app.drums().moveCursorTo(2, 3);
        rig.frames(2);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        const std::vector<Uint32> fill = canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH);
        // The same cell two bars long, on its (empty) second bar.
        session.setCellBars(0, 0, 2);
        rig.track(0).drums[0][1] = DrumPattern();
        rig.track(0).notes[0][1] = NotePattern();
        rig.project().bar = 1;
        if (synth) rig.app.notes().moveCursorTo(60, 3);
        else rig.app.drums().moveCursorTo(2, 3);
        rig.frames(2);
        CHECK(rig.project().bar == 1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(fill == canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH));
        if (fill != canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH)) std::printf("  (%s)\n", synth ? "the roll" : "the drum grid");
    }
}

// The BARS stop's BAR field reads FILL (or FILL OFF) on the FILL: the drum grid's strip and the roll's.
void testTheBarsField() {
    Canvas canvas, scratch;
    if (!canvas.ok) return;
    Rig rig;
    rig.withL1(Y);
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(0, 3);
    rig.tap(UP);
    rig.tap(UP);
    CHECK(drums.cursorOnBars() && rig.project().bar == kFillSlot);
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    CHECK(inked(canvas, scratch, 2, 134.f, pattern_layout::kStripY + 16.f, "FILL", ui::kTeal));
    CHECK(rig.session().fillOff(0, 0) == Session::Fill::Off);
    rig.frames(100); // (the line FILL MADE said, gone: its box lies over the strip's right)
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    CHECK(inked(canvas, scratch, 2, 134.f, pattern_layout::kStripY + 16.f, "FILL OFF", ui::kTeal));
    // The roll: BARS is up from step 1 of bar 1; on it, LEFT is the FILL.
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.project().bar = 0;
    rig.frames(2);
    rig.withL1(Y);
    rig.withL1(DOWN);
    rig.app.notes().moveCursorTo(60, 0);
    rig.tap(UP);
    rig.tap(LEFT);
    CHECK(rig.app.notes().cursorOnBars() && rig.project().bar == kFillSlot);
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    bool found = false; // (the roll's strip sits under its keys, which move with the zoom: look down its BAR column)
    for (int y = 380; y < 450 && !found; ++y) found = inked(canvas, scratch, 2, 100.f, static_cast<float>(y), "FILL", ui::kTeal);
    CHECK(found);
}

// Notes still sounding into the FILL from the bars before the one it plays instead of are drawn on it, faint:
// a two-bar cell in a scene of two, a note on bar 1 twenty steps long (four into bar 2, so into the FILL too).
void testNotesRunningIntoTheFill() {
    Canvas canvas;
    if (!canvas.ok) return;
    Rig rig;
    Session& session = rig.session();
    CHECK(session.setTrackKind(0, TrackKind::Phazed));
    session.setCellBars(0, 0, 2);
    rig.frames(2);
    CHECK(session.makeFill(0, 0) == Session::Fill::Made); // (empty: a copy of an empty bar)
    CHECK(session.goToFill(0, 0));
    rig.app.notes().moveCursorTo(48, 15);
    rig.frames(1);
    const auto drawn = [&] {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return canvas.part(0, ui::kTopBarH, ui::kScreenW, 300);
    };
    const std::vector<Uint32> without = drawn();
    NoteEvent n;
    n.step = 0;
    n.pitch = 60;
    n.length = 20.f;
    n.velocity = 0.8f;
    rig.track(0).notes[0][0].notes[0] = n;
    session.sendNotePattern(0, 0);
    rig.frames(1);
    CHECK(drawn() != without);
    // ...and a note hidden in an unplayed bar of the cell (bar 7, its FILL's place aside) is not.
    rig.track(0).notes[0][0].notes[0] = NoteEvent();
    n.length = 16.f;
    rig.track(0).notes[0][6].notes[0] = n;
    session.sendNotePattern(0, 0);
    rig.frames(1);
    CHECK(drawn() == without);
}

// The roll draws no glide lines on the FILL (Project::glideLines knows the cell's bars in order): a one-bar cell
// under a scene of eight bars, its note on step 1 gliding in from its note on step 9 (the time round before). On
// bar 1 its line shows; on the FILL (the scene's eighth bar, where the line would reach into from the next
// time round) nothing of it is drawn.
void testNoGlideLinesOnTheFill() {
    Canvas canvas;
    if (!canvas.ok) return;
    Rig rig;
    Session& session = rig.session();
    CHECK(session.setTrackKind(0, TrackKind::Phazed));
    CHECK(session.addTrack(TrackKind::Drums));
    session.setCellBars(1, 0, 8);
    rig.frames(2);
    TrackData& t = rig.track(0);
    NoteEvent a, b;
    a.step = 8, a.pitch = 55, a.length = 2.f, a.velocity = 0.8f;
    b.step = 0, b.pitch = 67, b.length = 2.f, b.velocity = 0.8f;
    t.notes[0][0].notes[0] = a;
    t.notes[0][0].notes[1] = b;
    session.sendNotePattern(0, 0);
    CHECK(session.makeFill(0, 0) == Session::Fill::Made);
    t.notes[0][kFillSlot] = NotePattern(); // (the FILL empty: only bar 1's line could be on it)
    session.sendNotePattern(0, 0);
    rig.project().scene = 0;
    rig.app.notes().moveCursorTo(40, 4);
    const auto drawnOn = [&](int bar) {
        rig.project().bar = bar;
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return canvas.part(0, ui::kTopBarH, ui::kScreenW, 300);
    };
    const std::vector<Uint32> barPlain = drawnOn(0), fillPlain = drawnOn(kFillSlot);
    t.notes[0][0].notes[1].glide = static_cast<std::uint8_t>(glideAt(kGlideKinds - 1)); // FULL
    session.sendNotePattern(0, 0);
    CHECK(drawnOn(0) != barPlain);     // its line, on bar 1
    CHECK(drawnOn(kFillSlot) == fillPlain); // none on the FILL
}

// While the FILL plays, the playhead is drawn on the FILL, not on the bar it plays instead of.
void testThePlayhead() {
    Canvas canvas;
    if (!canvas.ok) return;
    for (bool synth : {false, true}) {
        Rig rig;
        Session& session = rig.session();
        if (synth) CHECK(session.setTrackKind(0, TrackKind::Phazed));
        rig.frames(2);
        CHECK(session.makeFill(0, 0) == Session::Fill::Made); // REPEATS 1, one bar: the FILL plays every time round
        const auto playheadOn = [&](int bar) {
            rig.project().bar = bar;
            if (synth) rig.app.notes().moveCursorTo(60, 0);
            else rig.app.drums().moveCursorTo(0, 0);
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            const std::vector<Uint32> with = canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH);
            const int scene = rig.project().playScene;
            rig.project().playScene = -1; // (no update between: the same frame, with nothing playing)
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            rig.project().playScene = scene;
            return with != canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH);
        };
        session.togglePlay(false);
        rig.frames(20);
        CHECK(rig.project().playing && rig.project().cellBarPlaying(0) == kFillSlot);
        CHECK(playheadOn(kFillSlot));
        CHECK(!playheadOn(0));
        session.togglePlay(false);
        rig.frames(3);
    }
}

// In the slice view, a hit placed on the FILL plays the slice of the bar it stands for (a one-bar cell with a
// two-bar recording: its own sixteen, not the second sixteen).
void testASliceOnTheFill() {
    const std::string folder = tempFolder("fillscreens");
    CHECK(writeWav(folder + "/break.wav", std::vector<float>(96000, 0.25f), 48000));
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, 2, "break");
    session.setLaneFit(0, 2, 32);
    CHECK(session.addTrack(TrackKind::Drums));
    session.setCellBars(1, 0, 2); // the scene: two bars, so the FILL plays in the second
    rig.frames(2);
    PatternScreen& drums = rig.app.drums();
    CHECK(session.laneSlices(0, 2) == 32);
    drums.moveCursorTo(2, 3);
    rig.tap(L1);
    CHECK(drums.slicing());
    rig.withL1(Y);
    CHECK(rig.project().bar == kFillSlot && drums.slicing());
    rig.tap(A);
    CHECK(rig.track(0).drums[0][kFillSlot].level[2][3] > 0.f && rig.track(0).drums[0][kFillSlot].start[2][3] == 3.f / 32.f);
    // Its step numbers are the scene's last bar's: 17 under the first step. Then, playing with REPEATS 1 (the FILL
    // every time round), its playhead.
    Canvas canvas, scratch;
    if (!canvas.ok) return;
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    const float centre = pattern_layout::kGridLeft + pattern_layout::kCellW / 2.f;
    CHECK(inked(canvas, scratch, 0, centre - scratch.fonts->small.width("17") / 2.f, 248.f + 96.f + 5.f, "17", ui::kText));
    // A scene cut short to twenty steps: the FILL (in its second bar, four steps long) shades its last twelve, not
    // its first step: the slice view's first step column is as with the scene whole.
    const auto column = [&](int step) {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return canvas.part(pattern_layout::kGridLeft + step * pattern_layout::kStepW, 248 - 2, pattern_layout::kCellW, 96 + 20);
    };
    const std::vector<Uint32> first = column(0), ninth = column(8);
    session.setSceneSteps(0, 20);
    rig.frames(1);
    CHECK(column(0) == first && column(8) != ninth);
    session.setSceneSteps(0, 32);
    rig.frames(1);
    session.setCellBars(1, 0, 1); // (the scene one bar: the FILL every time round)
    session.togglePlay(false);
    rig.frames(20);
    CHECK(rig.project().cellBarPlaying(0) == kFillSlot);
    const auto below = [&] { // (under the top bar, which shows the scene playing either way, and over the hints)
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        return canvas.part(0, ui::kTopBarH, ui::kScreenW, ui::kScreenH - ui::kTopBarH - ui::kHintsH);
    };
    const std::vector<Uint32> with = below();
    const int scene = rig.project().playScene;
    rig.project().playScene = -1;
    CHECK(with != below());
    rig.project().playScene = scene;
    session.togglePlay(false);
    rig.frames(3);
}

// Undoing a NEXT SCENE keeps the view on the FILL (Session::standOn: the FILL is a bar to stand on).
void testNextSceneUndoKeepsTheView() {
    Rig rig;
    rig.withL1(Y);
    CHECK(rig.project().bar == kFillSlot);
    rig.key(SELECT, true);
    rig.tap(Y);
    rig.key(SELECT, false);
    rig.frames(2);
    CHECK(rig.project().scene == 1 && rig.project().bar == kFillSlot && rig.track(0).fillOn(1));
    rig.undo();
    rig.frames(2);
    CHECK(rig.project().scene == 0 && rig.project().bar == kFillSlot);
    rig.key(SELECT, true); // ...and redoing it
    rig.tap(A);
    rig.key(SELECT, false);
    rig.frames(2);
    CHECK(rig.project().scene == 1 && rig.project().bar == kFillSlot);
}

} // namespace

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheRing", testTheRing}, {"testTheGestureOnTheGrid", testTheGestureOnTheGrid}, {"testYIsSwallowedOnTheGrid", testYIsSwallowedOnTheGrid},
        {"testTheRingOnTheGrid", testTheRingOnTheGrid}, {"testCopyAndPasteOnTheFill", testCopyAndPasteOnTheFill}, {"testEightBars", testEightBars},
        {"testTheGestureOnTheRoll", testTheGestureOnTheRoll}, {"testANoteOnTheFill", testANoteOnTheFill}, {"testTheTopBarAndTheHints", testTheTopBarAndTheHints},
        {"testTheNumbersAndTheShading", testTheNumbersAndTheShading}, {"testThePlayhead", testThePlayhead}, {"testASliceOnTheFill", testASliceOnTheFill},
        {"testNextSceneUndoKeepsTheView", testNextSceneUndoKeepsTheView}, {"testTheBarsField", testTheBarsField},
        {"testNotesRunningIntoTheFill", testNotesRunningIntoTheFill}, {"testNoGlideLinesOnTheFill", testNoGlideLinesOnTheFill},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("fillscreens_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
