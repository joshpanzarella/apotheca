// THE MOD TAB (app/mod_screen.hpp; docs/plans/modulation-plan.md, section 7, batch 5): a tab after FX
// with both of a track's MODs. The tabs and the top bar's room; the list and its settings; the list
// of targets (A held: only tries, let go aims, B calls it off, another track or screen calls it
// off); each setting turned, its ends, B's usual value; a STEPPER's row written by hand in one grip,
// SLEW and CHANCE; the dice; hold B deletes; L1 / R1; X held runs a MOD while stopped; undo; the
// hints at 0 over in every place; and what is DRAWN (tests/canvas.hpp): the dot at now on the shape
// while the song plays and none when stopped, the step playing lit, the cursor's step.
//
//   modtab_test            all of it
//   modtab_test --print    also the top bar's figures

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "mod_knob.hpp"
#include "mod_screen.hpp"
#include "mods.hpp"

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

bool printing = false;

const SDL_Keycode X = SDLK_s, A = SDLK_x, B = SDLK_z, Y = SDLK_a, L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode StickUp = SDLK_i, StickDown = SDLK_k;
using S = ModScreen;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Canvas canvas;
    int phazed = -1, qmod = -1;

    Rig() {
        CHECK(app.session().addTrack(TrackKind::Phazed));
        phazed = app.project().trackCount() - 1;
        CHECK(app.session().addTrack(TrackKind::Qmod));
        qmod = app.project().trackCount() - 1;
        frames(2);
    }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    ModScreen& tab() { return app.mods(); }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) key(code, true), frames(1), key(code, false), frames(1);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    // The MOD tab on `track`, the cursor at (row, column).
    ModScreen& at(int track, int row, int column) {
        project().track = track;
        session().view = View::Mods;
        frames(1);
        tab().goTo(row, column);
        frames(1);
        return tab();
    }
    const ModSettings& mod(int track, int m) { return session().modData(track, m); }
    void draw() {
        canvas.clear();
        app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
    }
    void undo() { key(SELECT, true), tap(B), key(SELECT, false), frames(2); }
    // How many pixels of a colour in a part of the screen.
    int count(int x, int y, int w, int h, SDL_Color c) const {
        int n = 0;
        for (int py = y; py < y + h; ++py)
            for (int px = x; px < x + w; ++px) n += canvas.is(px, py, c);
        return n;
    }
};

ModSettings lfo(int target, float depth = 0.5f) {
    ModSettings m = ModKnob::fresh(target);
    m.depth = depth;
    return m;
}

ModSettings stepper(int target) {
    ModSettings m = ModKnob::fresh(target);
    m.kind = kModStepper, m.depth = 0.5f, m.rate = kModRateStep, m.length = 4;
    for (int i = 0; i < kModSteps; ++i) m.steps[i] = i % 2 ? 0.5f : -0.5f;
    return m;
}

// ---- the tab and the top bar -----------------------------------------------------------------

void testTheTabAndTheTopBar() {
    Rig rig;
    rig.session().view = View::Effects;
    rig.tap(R2);
    CHECK(rig.session().view == View::Mods);
    rig.tap(R2);
    CHECK(rig.session().view == View::Settings);
    rig.tap(L2);
    CHECK(rig.session().view == View::Mods);
    rig.tap(L2);
    CHECK(rig.session().view == View::Effects);

    // Six tabs in the top bar, at LARGE: the bar is a tight row, the same at every TEXT SIZE (textviews holds
    // that for every still). The tabs and the chips end where the screen's words begin, and those words keep room.
    Canvas c;
    const char* tabs[] = {"SONG", "PATTERN", "DEVICE", "FX", "MOD", "SET"};
    for (int v = 0; v <= static_cast<int>(View::Settings); ++v) {
        c.clear();
        const ui::TopBar bar = ui::topBar(c.r, *c.fonts, static_cast<View>(v), "", rig.project());
        // The tab that is up is filled in teal, under its word.
        float x = 4.f + 8.f + c.fonts->small.width("L2") + 8.f + 4.f;
        for (int i = 0; i < v; ++i) x += c.fonts->small.width(tabs[i]) + 2.f * ui::kTabPad + 2.f;
        CHECK(c.is(static_cast<int>(x) + 1, 3, ui::kTeal));
        CHECK(bar.wordsX > x && bar.wordsRoom > 0.f);
    }
    // The room the words have: stopped, playing (the scene's blocks), and with WRITE on. A track's name
    // ("12 PHAZED") must fit stopped and playing; the longest a player can type is cut, as before.
    Project& p = rig.project();
    c.clear();
    const ui::TopBar stopped = ui::topBar(c.r, *c.fonts, View::Mods, "", p);
    p.playing = true, p.playScene = 0;
    c.clear();
    const ui::TopBar playing = ui::topBar(c.r, *c.fonts, View::Mods, "", p);
    p.writing = true;
    c.clear();
    const ui::TopBar writing = ui::topBar(c.r, *c.fonts, View::Mods, "", p);
    p.playing = false, p.writing = false;
    const float name = c.fonts->small.width("12 PHAZED"), mod = c.fonts->small.width("MOD") + 12.f;
    if (printing)
        std::printf("top bar: the words start at %.0f; room stopped %.0f, playing %.0f, playing with WRITE %.0f (\"12 PHAZED\" is %.0f; the tab took %.0f)\n",
                    static_cast<double>(stopped.wordsX), static_cast<double>(stopped.wordsRoom), static_cast<double>(playing.wordsRoom),
                    static_cast<double>(writing.wordsRoom), static_cast<double>(name), static_cast<double>(mod));
    CHECK(stopped.wordsRoom >= name && playing.wordsRoom >= name);
}

// ---- the list and its settings ------------------------------------------------------------------

void testTheListAndItsSettings() {
    Rig rig;
    const int t = rig.phazed;
    ModScreen& tab = rig.at(t, 0, 0);
    // Up / down go round the four lines; an OFF MOD has TARGET only, and its STEPS nothing.
    rig.tap(UP);
    CHECK(tab.cursorRow() == 3 && tab.cursorColumn() == 0);
    rig.tap(DOWN, 2);
    CHECK(tab.cursorRow() == 1);
    rig.tap(RIGHT);
    CHECK(tab.cursorColumn() == 0); // (an OFF MOD's STEPS: nothing to go into)
    rig.tap(UP);
    rig.tap(RIGHT);
    CHECK(tab.cursorColumn() == S::kTarget && tab.settings(0) == 1);
    rig.tap(DOWN);
    CHECK(tab.cursorColumn() == S::kTarget);
    rig.tap(LEFT);
    CHECK(tab.cursorColumn() == 0);
    // With a MOD on: five rows, the ends stop, RIGHT comes back to the one last used.
    CHECK(rig.session().setMod(t, 0, lfo(kModFirstMacro)));
    rig.tap(RIGHT);
    rig.tap(DOWN, 6);
    CHECK(tab.cursorColumn() == S::kMore);
    rig.tap(UP, 2);
    CHECK(tab.cursorColumn() == S::kDepth);
    rig.tap(LEFT);
    rig.tap(DOWN), rig.tap(UP);
    rig.tap(RIGHT);
    CHECK(tab.cursorColumn() == S::kDepth && tab.remembered(0) == S::kDepth);
    // The MOD goes OFF under the cursor (deleted elsewhere): the cursor is pulled in to TARGET.
    rig.session().setMod(t, 0, ModSettings());
    rig.frames(1);
    CHECK(tab.cursorColumn() == S::kTarget);
}

void testTurningEachSetting() {
    Rig rig;
    const int t = rig.phazed;
    rig.session().setMod(t, 0, lfo(kModFirstMacro, 0.25f));
    ModScreen& tab = rig.at(t, 0, S::kDepth);
    const auto turn = [&](SDL_Keycode d, int times) { rig.key(A, true), rig.frames(1), rig.tap(d, times), rig.key(A, false), rig.frames(1); };
    turn(UP, 3);
    CHECK(std::fabs(rig.mod(t, 0).depth - 0.28f) < 1e-5f);
    turn(DOWN, 130);
    CHECK(rig.mod(t, 0).depth == -1.f); // the end stops
    rig.tap(B);
    CHECK(rig.mod(t, 0).depth == 0.25f); // the usual
    // The stick with A held: smoothly, squared.
    rig.key(A, true), rig.key(StickUp, true), rig.frames(10), rig.key(StickUp, false), rig.key(A, false), rig.frames(1);
    const float d = rig.mod(t, 0).depth;
    CHECK(d > 0.4f && d < 0.7f);
    // RATE: a click a push, in the list's order, the ends stop; B: 1 BAR.
    tab.goTo(0, S::kRate);
    turn(UP, 1);
    CHECK(rig.mod(t, 0).rate == kModRateTwoBars);
    turn(UP, 20);
    CHECK(rig.mod(t, 0).rate == kModRateEightBars);
    turn(DOWN, 30);
    CHECK(rig.mod(t, 0).rate == kModRateHalfStep);
    rig.tap(B);
    CHECK(rig.mod(t, 0).rate == kModRateBar);
    // The stick with A held on a choice clicks, at the list's pace, never slides past.
    rig.key(A, true), rig.key(StickDown, true), rig.frames(1), rig.key(StickDown, false), rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 0).rate == kModRateHalf);
    rig.tap(B);
    // PHASE: quarters, the ends stop.
    tab.goTo(0, S::kMore);
    turn(UP, 6);
    CHECK(rig.mod(t, 0).phase == 3);
    rig.tap(B);
    CHECK(rig.mod(t, 0).phase == 0);
    // SHAPE: SINE ... DRIFT, STEPPER: the ends stop. A STEPPER whose row was never written comes in rolled.
    tab.goTo(0, S::kShape);
    turn(DOWN, 1);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).shape == kModSine);
    turn(UP, 6);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).shape == kModDrift);
    turn(UP, 3);
    CHECK(rig.mod(t, 0).kind == kModStepper);
    bool any = false;
    for (int i = 0; i < kModSteps; ++i) any = any || rig.mod(t, 0).steps[i] != 0.f;
    CHECK(any);
    // A STEPPER's fifth row is LENGTH, 1 to 16.
    tab.goTo(0, S::kMore);
    turn(DOWN, 20);
    CHECK(rig.mod(t, 0).length == 1);
    rig.tap(B);
    CHECK(rig.mod(t, 0).length == kModSteps);
    tab.goTo(0, S::kShape);
    rig.tap(B);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).shape == kModSine);
    // B on TARGET does nothing (hold B on the line deletes).
    tab.goTo(0, S::kTarget);
    rig.tap(B);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).target == kModFirstMacro);
}

// ---- the list of targets ------------------------------------------------------------------------

void testTheListOfTargets() {
    Rig rig;
    const int t = rig.phazed;
    rig.session().setEffect(t, 1, EffectKind::Filter);
    ModScreen& tab = rig.at(t, 0, S::kTarget);
    // What is offered on each line: what a MOD may move on this track, in order.
    int targets[8], count = 0;
    ModScreen::pickLineTargets(rig.project().tracks[t], S::kPickMacros, targets, &count);
    CHECK(count == 6 && targets[0] == 0 && targets[5] == 5); // (CHORUS and OCTAVE are choices)
    ModScreen::pickLineTargets(rig.project().tracks[t], S::kPickFx1, targets, &count);
    CHECK(count == 0);
    ModScreen::pickLineTargets(rig.project().tracks[t], S::kPickFx2, targets, &count);
    CHECK(count == 3 && targets[0] == kModFirstEffect + 4); // FILTER: CUTOFF, RESONANCE, MIX (TYPE a choice)
    ModScreen::pickLineTargets(rig.project().tracks[t], S::kPickSends, targets, &count);
    CHECK(count == 2 && targets[1] == kModFirstSend + 1);
    // A held: the list opens on the first line with something; up / down skip what has nothing; nothing kept yet.
    rig.key(A, true), rig.frames(1);
    CHECK(tab.picking() && tab.pickLine() == S::kPickMacros && tab.pickItem() == 0);
    rig.tap(DOWN);
    CHECK(tab.pickLine() == S::kPickFx2 && tab.pickItem() == 0);
    rig.tap(RIGHT, 5);
    CHECK(tab.pickItem() == 2); // the end stops
    rig.tap(UP);
    CHECK(tab.pickLine() == S::kPickMacros && tab.pickItem() == 2); // the place kept where it can be
    rig.tap(DOWN, 4);
    CHECK(tab.pickLine() == S::kPickSends && tab.pickItem() == 1);
    CHECK(rig.mod(t, 0).kind == kModOff); // only tried: nothing in the song yet
    rig.key(A, false), rig.frames(1);
    // Let go: aimed there, an OFF MOD made as the knob's first (call C).
    CHECK(!tab.picking());
    ModSettings made = ModKnob::fresh(kModFirstSend + 1);
    CHECK(std::memcmp(&rig.mod(t, 0), &made, sizeof made) == 0);
    // Opened again: on its own target. Moved and B with A held: as it was, and A does nothing more.
    rig.key(A, true), rig.frames(1);
    CHECK(tab.pickLine() == S::kPickSends && tab.pickItem() == 1);
    rig.tap(UP);
    rig.tap(B);
    CHECK(!tab.picking());
    rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 0).target == kModFirstSend + 1);
    // Another target for a MOD that is on keeps the rest of it.
    ModSettings tuned = rig.mod(t, 0);
    tuned.depth = -0.7f, tuned.rate = kModRateHalf;
    rig.session().setMod(t, 0, tuned);
    rig.key(A, true), rig.frames(1);
    rig.tap(UP);
    rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 0).target == kModFirstEffect + 5 && rig.mod(t, 0).depth == -0.7f && rig.mod(t, 0).rate == kModRateHalf);
    // A held on the MOD's own line in the list opens it too; on its STEPS it does not.
    tab.goTo(0, 0);
    rig.key(A, true), rig.frames(1);
    CHECK(tab.picking());
    // Another screen up while A is held: called off (nothing without a let go that was seen).
    rig.tap(RIGHT);
    rig.session().view = View::Effects;
    rig.frames(1);
    rig.key(A, false), rig.frames(1);
    rig.session().view = View::Mods;
    rig.frames(1);
    CHECK(!tab.picking() && rig.mod(t, 0).target == kModFirstEffect + 5);
    tab.goTo(1, 0);
    rig.key(A, true), rig.frames(1);
    CHECK(!tab.picking());
    rig.key(A, false), rig.frames(1);
    // Another track while A is held (L1 / R1): called off.
    tab.goTo(0, 0);
    rig.key(A, true), rig.frames(1);
    rig.tap(DOWN);
    rig.tap(R1);
    CHECK(!tab.picking() && rig.project().track == t + 1);
    rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 0).target == kModFirstEffect + 5 && rig.mod(t + 1, 0).kind == kModOff);
    // Another track up by some other way while A is held (not L1 / R1): called off too.
    rig.at(t, 0, 0);
    rig.key(A, true), rig.frames(1);
    CHECK(rig.tab().picking());
    rig.project().track = rig.qmod;
    rig.frames(1);
    CHECK(!rig.tab().picking());
    rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(rig.qmod, 0).kind == kModOff);
    // The drum track: no macros, so the list opens on the sends.
    rig.at(0, 2, S::kTarget);
    rig.key(A, true), rig.frames(1);
    CHECK(rig.tab().pickLine() == S::kPickSends);
    rig.tap(UP);
    CHECK(rig.tab().pickLine() == S::kPickSends); // (nothing above it has anything)
    rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(0, 1).kind == kModLfo && rig.mod(0, 1).target == kModFirstSend);
}

// ---- a STEPPER's row ----------------------------------------------------------------------------

void testTheRowWrittenByHand() {
    Rig rig;
    const int t = rig.phazed;
    ModSettings m = ModKnob::fresh(kModFirstSend);
    m.kind = kModStepper;
    rig.session().setMod(t, 1, m);
    ModScreen& tab = rig.at(t, 3, 0);
    rig.tap(RIGHT);
    CHECK(tab.cursorColumn() == S::kFirstStep);
    // In one grip: UP twice, RIGHT, DOWN once, RIGHT, UP 20 (the end stops at +100%).
    rig.key(A, true), rig.frames(1);
    rig.tap(UP, 2);
    rig.tap(RIGHT);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    rig.tap(UP, 25);
    rig.tap(LEFT, 5);
    CHECK(tab.cursorColumn() == S::kFirstStep); // (A held: the row's end stops, it does not leave it)
    rig.tap(RIGHT, 20);
    CHECK(tab.cursorColumn() == S::kFirstStep + kModSteps - 1); // (nor at the other end)
    rig.tap(LEFT, kModSteps - 1);
    rig.key(A, false), rig.frames(1);
    const ModSettings& s = rig.mod(t, 1);
    CHECK(std::fabs(s.steps[0] - 0.1f) < 1e-5f && std::fabs(s.steps[1] + 0.05f) < 1e-5f && s.steps[2] == 1.f && s.steps[3] == 0.f);
    // B: a step to 0.
    tab.goTo(3, S::kFirstStep + 2);
    rig.tap(B);
    CHECK(rig.mod(t, 1).steps[2] == 0.f);
    // Along the row without A; LEFT from the first is the list; DOWN to LENGTH, UP back to the step left.
    rig.tap(RIGHT, 20);
    CHECK(tab.cursorColumn() == S::kFirstStep + kModSteps - 1);
    rig.tap(DOWN);
    CHECK(tab.cursorColumn() == S::kLength);
    rig.tap(DOWN, 5);
    CHECK(tab.cursorColumn() == S::kChance);
    rig.tap(UP, 2);
    CHECK(tab.cursorColumn() == S::kLength);
    rig.tap(UP);
    CHECK(tab.cursorColumn() == S::kFirstStep + kModSteps - 1);
    tab.goTo(3, S::kFirstStep + 6);
    rig.tap(DOWN);
    rig.tap(UP);
    CHECK(tab.cursorColumn() == S::kFirstStep + 6); // back to the step it came down from
    tab.goTo(3, S::kFirstStep);
    rig.tap(LEFT);
    CHECK(tab.cursorColumn() == 0);
    // LENGTH, SLEW, CHANCE: a click, a twentieth, a twentieth; B the usual.
    tab.goTo(3, S::kLength);
    rig.key(A, true), rig.frames(1), rig.tap(DOWN, 3), rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 1).length == 13);
    tab.goTo(3, S::kSlew);
    rig.key(A, true), rig.frames(1), rig.tap(UP, 3), rig.key(A, false), rig.frames(1);
    CHECK(std::fabs(rig.mod(t, 1).slew - 0.15f) < 1e-5f);
    tab.goTo(3, S::kChance);
    rig.key(A, true), rig.frames(1), rig.tap(UP, 30), rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 1).chance == 1.f);
    rig.tap(B);
    CHECK(rig.mod(t, 1).chance == 0.f && rig.mod(t, 1).length == 13);
    tab.goTo(3, S::kLength);
    rig.tap(B);
    CHECK(rig.mod(t, 1).length == kModSteps);
    // The stick with A held on a step: smoothly.
    tab.goTo(3, S::kFirstStep + 5);
    rig.key(A, true), rig.key(StickUp, true), rig.frames(10), rig.key(StickUp, false), rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 1).steps[5] > 0.2f && rig.mod(t, 1).steps[5] < 0.8f);
    // An LFO's STEPS can be written too (kept for when it is a STEPPER); an OFF MOD's cannot be reached.
    rig.session().setMod(t, 0, lfo(kModFirstMacro));
    tab.goTo(1, S::kFirstStep);
    rig.key(A, true), rig.frames(1), rig.tap(UP), rig.key(A, false), rig.frames(1);
    CHECK(rig.mod(t, 0).kind == kModLfo && std::fabs(rig.mod(t, 0).steps[0] - 0.05f) < 1e-5f);
}

void testTheDiceAndUndo() {
    Rig rig;
    const int t = rig.phazed;
    rig.session().setMod(t, 1, stepper(kModFirstSend));
    rig.at(t, 3, 0);
    const ModSettings before = rig.mod(t, 1);
    rig.frames(60); // (a still moment ends a step of undo: 700 ms)
    rig.tap(Y);
    rig.frames(60);
    const ModSettings rolled = rig.mod(t, 1);
    bool differ = false;
    for (int i = 0; i < kModSteps; ++i) differ = differ || rolled.steps[i] != before.steps[i];
    CHECK(differ && rolled.kind == kModStepper && rolled.length == before.length && rolled.depth == before.depth);
    rig.undo();
    CHECK(std::memcmp(&rig.mod(t, 1), &before, sizeof before) == 0); // the dice are a step of undo
    // On a MOD's line the dice are the knob's: an LFO's shape and rate, DEPTH kept.
    rig.session().setMod(t, 0, lfo(kModFirstMacro, -0.3f));
    rig.tab().goTo(0, 0);
    rig.tap(Y);
    CHECK(rig.mod(t, 0).depth == -0.3f && (rig.mod(t, 0).shape != kModSine || rig.mod(t, 0).rate != kModRateBar));
    // The STEPS of an LFO: its row rolled, the LFO still an LFO.
    rig.tab().goTo(1, 0);
    rig.tap(Y);
    CHECK(rig.mod(t, 0).kind == kModLfo);
    bool any = false;
    for (int i = 0; i < kModSteps; ++i) any = any || rig.mod(t, 0).steps[i] != 0.f;
    CHECK(any);
    // An OFF MOD: the dice do nothing.
    rig.session().setMod(t, 0, ModSettings());
    rig.tap(Y);
    CHECK(rig.mod(t, 0).kind == kModOff);
    // A turn made on the tab is a step of undo too.
    rig.frames(60);
    rig.tab().goTo(2, S::kDepth);
    rig.key(A, true), rig.frames(1), rig.tap(UP, 4), rig.key(A, false), rig.frames(60);
    CHECK(std::fabs(rig.mod(t, 1).depth - 0.54f) < 1e-5f);
    rig.undo();
    CHECK(rig.mod(t, 1).depth == 0.5f);
}

// ---- deleting, L1 / R1, X ------------------------------------------------------------------------

void testHoldBDeletes() {
    Rig rig;
    const int t = rig.phazed;
    rig.session().setMod(t, 1, stepper(kModFirstSend));
    ModScreen& tab = rig.at(t, 2, 0);
    // A tap does nothing; another press calls it off.
    rig.tap(B);
    rig.frames(80);
    CHECK(rig.mod(t, 1).kind == kModStepper);
    rig.key(B, true), rig.frames(30);
    CHECK(tab.deletePart() > 0.2f && tab.deletePart() < 0.8f);
    rig.tap(Y);
    rig.frames(50);
    rig.key(B, false), rig.frames(1);
    CHECK(rig.mod(t, 1).kind == kModStepper && tab.deletePart() == 0.f);
    // Held a second on its STEPS line too: deleted.
    tab.goTo(3, 0);
    rig.key(B, true), rig.frames(70), rig.key(B, false), rig.frames(30);
    CHECK(rig.mod(t, 1).kind == kModOff);
    rig.undo();
    CHECK(rig.mod(t, 1).kind == kModStepper); // undo brings it back
    // Moved off the line while held: nothing goes.
    rig.key(B, true), rig.frames(20);
    rig.tap(UP);
    rig.frames(60);
    rig.key(B, false), rig.frames(1);
    CHECK(rig.mod(t, 1).kind == kModStepper);
    // Moved off by the stick (no press calls it off): nothing goes, on either MOD.
    rig.session().setMod(t, 0, lfo(kModFirstMacro));
    tab.goTo(2, 0);
    rig.key(B, true), rig.frames(10);
    rig.key(StickUp, true), rig.frames(2), rig.key(StickUp, false);
    CHECK(tab.cursorRow() == 1);
    rig.frames(70);
    rig.key(B, false), rig.frames(1);
    CHECK(rig.mod(t, 1).kind == kModStepper && rig.mod(t, 0).kind == kModLfo);
    rig.session().setMod(t, 0, ModSettings());
    // An OFF MOD: no bar.
    tab.goTo(0, 0);
    rig.key(B, true), rig.frames(10);
    CHECK(tab.deletePart() == 0.f);
    rig.key(B, false), rig.frames(1);
}

void testL1R1AndX() {
    Rig rig;
    const int t = rig.phazed;
    ModScreen& tab = rig.at(t, 2, S::kTarget);
    rig.tap(R1);
    CHECK(rig.project().track == rig.qmod && tab.cursorRow() == 2);
    rig.tap(R1);
    CHECK(rig.project().track == rig.qmod); // the last stops
    rig.tap(L1, 5);
    CHECK(rig.project().track == 0);
    // X held on a MOD's line while stopped: that MOD runs (and only it); let go, it stops.
    rig.session().setMod(t, 0, lfo(kModFirstMacro));
    rig.session().setMod(t, 1, lfo(kModFirstSend));
    rig.at(t, 3, 0);
    rig.key(X, true), rig.frames(20);
    CHECK(rig.session().modsHeard(t) == 2u);
    CHECK(std::isfinite(rig.engine->modNow(t, 1)) && !std::isfinite(rig.engine->modNow(t, 0)));
    rig.key(X, false), rig.frames(3);
    CHECK(rig.session().modsHeard(t) == 0u && !std::isfinite(rig.engine->modNow(t, 1)));
    // On an OFF MOD: nothing runs. While playing: the song's MODs run, nothing is asked.
    rig.session().setMod(t, 1, ModSettings());
    rig.key(X, true), rig.frames(5);
    CHECK(rig.session().modsHeard(t) == 0u);
    rig.key(X, false), rig.frames(2);
    rig.session().setMod(t, 1, lfo(kModFirstSend));
    rig.tap(START);
    rig.key(X, true), rig.frames(5);
    CHECK(rig.session().modsHeard(t) == 0u && std::isfinite(rig.engine->modNow(t, 1)));
    rig.key(X, false), rig.frames(2);
    rig.tap(START);
    rig.frames(2);
    CHECK(!std::isfinite(rig.engine->modNow(t, 1)) && !std::isfinite(rig.engine->modNow(t, 0)));
    // X held, then another screen: let go (App's keepModsHeard).
    rig.key(X, true), rig.frames(5);
    CHECK(rig.session().modsHeard(t) == 2u);
    rig.session().view = View::Effects;
    rig.frames(3);
    CHECK(rig.session().modsHeard(t) == 0u);
    rig.key(X, false), rig.frames(1);
}

// ---- the hints ---------------------------------------------------------------------------------

void testTheHintsFit() {
    Rig rig;
    for (int t : {0, rig.phazed, rig.qmod}) {
        rig.session().setEffect(t, 0, EffectKind::Filter);
        for (int on = 0; on < 3; ++on) {
            rig.session().setMod(t, 0, on == 0 ? ModSettings() : (on == 1 ? lfo(kModFirstSend) : stepper(kModFirstSend)));
            rig.session().setMod(t, 1, on == 0 ? ModSettings() : stepper(kModFirstEffect));
            for (int row = 0; row < S::kRows; ++row)
                for (int column = 0; column <= S::kStepsColumns; ++column)
                    for (int held = 0; held < 2; ++held) {
                        ModScreen& tab = rig.at(t, row, column);
                        if (tab.cursorColumn() != column) continue;
                        if (held) rig.key(A, true), rig.frames(1);
                        rig.draw();
                        if (ui::gHintsOver > 0.f) std::printf("  over by %.0f: track %d, MODs %d, line %d, column %d, A %d\n", static_cast<double>(ui::gHintsOver), t, on, row, column, held);
                        CHECK(ui::gHintsOver == 0.f);
                        if (held) rig.key(A, false), rig.frames(1);
                    }
        }
    }
}

// ---- what is drawn ------------------------------------------------------------------------------

void testTheDotAndTheStepPlaying() {
    Rig rig;
    if (!rig.canvas.ok) return;
    const int t = rig.phazed;
    ModSettings m = lfo(kModFirstSend, 0.8f);
    m.shape = kModRampUp, m.rate = kModRateBar;
    rig.session().setMod(t, 0, m);
    rig.session().setMod(t, 1, stepper(kModFirstSend + 1));
    rig.at(t, 0, 0);
    // Stopped: no dot on either picture, no step lit.
    rig.draw();
    CHECK(rig.count(S::kPicX - 4, S::kPicY - 4, S::kPicW + 8, S::kPicH + 8, ui::kPurple) == 0);
    CHECK(rig.count(S::kListX, S::lineY(0), S::kListW, S::kLineH, ui::kPurple) == 0);
    CHECK(rig.count(S::kListX, S::lineY(3), S::kListW, S::kLineH, ui::kPurple) == 0);
    // Playing: the dot is where the ramp is now (x by the place in its cycle, y by its value).
    rig.tap(START);
    for (int round = 0; round < 6; ++round) {
        rig.frames(17);
        rig.draw();
        const double at = rig.engine->modNow(t, 0);
        CHECK(std::isfinite(at));
        CHECK(modOffsetAt(m, at, t, 0) == rig.engine->modShown(t, 0)); // the engine's position is the one its value is from
        const double cycles = lfoCycles(m, at), place = cycles - std::floor(cycles);
        const int dotX = S::kPicX + static_cast<int>(place * S::kPicW);
        const int dotY = S::kPicY + S::kPicH / 2 - static_cast<int>(std::lround(lfoValue(m, at, t, 0) * (S::kPicH / 2 - 2)));
        CHECK(rig.canvas.is(dotX, dotY, ui::kPurple) && rig.count(dotX - 3, dotY - 3, 7, 7, ui::kPurple) == 49);
        CHECK(rig.count(S::kPicX - 4, S::kPicY - 4, S::kPicW + 8, S::kPicH + 8, ui::kPurple) == 49); // the one dot
        // The STEPS line in the list: the step playing lit, and only it.
        const int place4 = rig.engine->modPlace(t, 1);
        CHECK(place4 >= 0 && place4 < 4);
        const int px = S::kListX + 10, pw = S::kListW - 20;
        const int from = px + pw * place4 / 4, to = px + pw * (place4 + 1) / 4;
        CHECK(rig.count(from, S::lineY(3), to - from, S::kLineH, ui::kPurple) > 20);
        CHECK(rig.count(S::kListX, S::lineY(3), S::kListW, S::kLineH, ui::kPurple) == rig.count(from, S::lineY(3), to - from, S::kLineH, ui::kPurple));
    }
    // The STEPS pane: the step playing purple, the cursor's outlined.
    rig.tab().goTo(3, S::kFirstStep + 6);
    rig.frames(1);
    rig.draw();
    const int playing = rig.engine->modPlace(t, 1);
    const int stepX = S::kStepX + playing * S::kStepW;
    CHECK(rig.count(stepX, S::kStepsTop, S::kStepBarW, 2 * S::kStepsHalf + 1, ui::kPurple) > 40);
    CHECK(rig.canvas.is(S::kStepX + 6 * S::kStepW - 2, S::kStepsTop + 10, ui::kTeal));
    // Stopped again: the dot goes on the very pass START is pressed (before the engine has heard it).
    rig.tab().goTo(0, 0);
    rig.key(START, true);
    rig.draw();
    CHECK(rig.count(S::kPicX - 4, S::kPicY - 4, S::kPicW + 8, S::kPicH + 8, ui::kPurple) == 0);
    CHECK(rig.engine->modPlace(t, 1) >= 0 && rig.count(S::kListX, S::lineY(3), S::kListW, S::kLineH, ui::kPurple) == 0); // (the engine has not heard it yet)
    rig.key(START, false);
    // X held while stopped: the dot again, from where X went down.
    rig.key(X, true), rig.frames(20);
    rig.draw();
    CHECK(rig.count(S::kPicX - 4, S::kPicY - 4, S::kPicW + 8, S::kPicH + 8, ui::kPurple) == 49);
    rig.key(X, false), rig.frames(2);
    rig.draw();
    CHECK(rig.count(S::kPicX - 4, S::kPicY - 4, S::kPicW + 8, S::kPicH + 8, ui::kPurple) == 0);
    // A negative DEPTH turns the picture over: a ramp up drawn falling.
    m.depth = -0.8f;
    rig.session().setMod(t, 0, m);
    rig.frames(1);
    rig.draw();
    const auto lineAt = [&](int px) {
        for (int py = S::kPicY; py < S::kPicY + S::kPicH; ++py)
            if (rig.canvas.is(px, py, ui::kText)) return py;
        return -1;
    };
    CHECK(lineAt(S::kPicX + 10) >= 0 && lineAt(S::kPicX + 10) < lineAt(S::kPicX + S::kPicW - 10));
    m.depth = 0.8f;
    rig.session().setMod(t, 0, m);
    rig.frames(1);
    rig.draw();
    CHECK(lineAt(S::kPicX + 10) > lineAt(S::kPicX + S::kPicW - 10));
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (int i = 1; i < argc; ++i) printing = printing || std::strcmp(argv[i], "--print") == 0;
    testTheTabAndTheTopBar();
    testTheListAndItsSettings();
    testTurningEachSetting();
    testTheListOfTargets();
    testTheRowWrittenByHand();
    testTheDiceAndUndo();
    testHoldBDeletes();
    testL1R1AndX();
    testTheHintsFit();
    testTheDotAndTheStepPlaying();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("the MOD tab: tests passed\n");
    return 0;
}
