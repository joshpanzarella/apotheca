// X HELD ON A KNOB: its MOD (app/mod_knob.hpp; docs/plans/modulation-plan.md, section 7, batches 3
// and 4). On DEVICE MAIN's macros and on the FX screen's slot settings and SENDS: the gesture (the
// first UP makes one, DEPTH, RATE, SHAPE, PHASE / LENGTH, the dice, hold B), MODS FULL, a choice
// that takes none, the order of the checks (X held before Y's ROLL, the stick's cursor, L1 / R1's
// track), the MOD heard while stopped (Session::hearMods) and let go, undo, and what is DRAWN
// (tests/canvas.hpp): the fields in SIGNAL's place and in the heading's, each chip's left edge
// its field's, and the plain bar back, pixel for pixel, when X is let go.
//
//   modknob_test            all of it
//   modknob_test --print    also print how far each plain bar is short (gHintsOver)

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "mod_knob.hpp"
#include "mods.hpp"
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

bool printing = false;

const SDL_Keycode X = SDLK_s, A = SDLK_x, B = SDLK_z, Y = SDLK_a, L1 = SDLK_q, R1 = SDLK_w;
const SDL_Keycode StickUp = SDLK_i, StickRight = SDLK_l;
const int kBarTop = ui::kScreenH - ui::kHintsH, kChipTop = kBarTop + 5;
using Chips = std::vector<std::pair<int, int>>; // (left edge, width)

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Canvas canvas, scratch;
    int phazed = -1, fm6 = -1;

    Rig() {
        CHECK(app.session().addTrack(TrackKind::Phazed));
        phazed = app.project().trackCount() - 1;
        CHECK(app.session().addTrack(TrackKind::Fm6));
        fm6 = app.project().trackCount() - 1;
        frames(2);
    }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
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
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    // DEVICE MAIN of a synth track, the cursor on macro `m`.
    SynthScreen& device(int track, int m) {
        project().track = track;
        session().view = View::Device;
        frames(1);
        SynthScreen& s = project().tracks[track].kind == TrackKind::Fm6 ? app.fm6() : app.phazed();
        s.goTo(0, SynthScreen::kFirstMacro + m);
        frames(1);
        return s;
    }
    FxScreen& fx(int track, int row, int column) {
        project().track = track;
        session().view = View::Effects;
        frames(1);
        app.fx().goTo(row, column);
        frames(1);
        return app.fx();
    }
    const ModSettings& mod(int track, int m) { return session().modData(track, m); }
    void draw() {
        frames(1);
        canvas.clear();
        app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
    }
    Chips chips() const {
        Chips found;
        for (int x = 0; x < ui::kScreenW;) {
            const int w = canvas.run(x, kChipTop, ui::kText);
            if (w > 0) found.push_back({x, w});
            x += w > 0 ? w : 1;
        }
        return found;
    }
    int chipW(const char* key) const { return static_cast<int>(ui::keyWidth(*canvas.fonts, key) + 8.f); }
    bool chipIs(float x, const char* key) {
        const ui::Hint one[] = {{key, ""}};
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, one, 1);
        return canvas.part(static_cast<int>(x), kBarTop, chipW(key) + 1, ui::kHintsH) == scratch.part(8, kBarTop, chipW(key) + 1, ui::kHintsH);
    }
    bool plainBar(const ui::Hint* list, int count) {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, list, count);
        return canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH) == scratch.part(0, kBarTop, ui::kScreenW, ui::kHintsH);
    }
    // Is `text` drawn at (x, y) in this lettering and colour? Drawn there on the second screen, and
    // wherever that has the full colour the screen looked at must have it too.
    bool drawn(float x, float y, const char* text, bool medium = false, SDL_Color colour = ui::kDim) {
        const Font& font = medium ? scratch.fonts->medium : scratch.fonts->small;
        const int x0 = static_cast<int>(x) - 2, x1 = static_cast<int>(x + font.width(text)) + 3, y0 = static_cast<int>(y) - 4, y1 = static_cast<int>(y) + 20;
        ui::fill(scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255});
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
};

ModSettings lfoOn(int target, float depth = 0.5f) {
    ModSettings m = ModKnob::fresh(target);
    m.depth = depth;
    return m;
}

// ---- the rule's pieces -------------------------------------------------------------------------

void testTheWords() {
    char text[16];
    ModSettings m = ModKnob::fresh(3);
    CHECK(m.kind == kModLfo && m.target == 3 && m.shape == kModSine && m.rate == kModRateBar && m.depth == 0.25f && m.phase == 0);
    ModKnob::shapeText(m, text, sizeof text);
    CHECK(std::strcmp(text, "SINE") == 0);
    m.kind = kModStepper;
    ModKnob::shapeText(m, text, sizeof text);
    CHECK(std::strcmp(text, "STEPPER") == 0);
    ModKnob::moreText(m, text, sizeof text);
    CHECK(std::strcmp(text, "16") == 0 && std::strcmp(ModKnob::moreName(m), "LENGTH") == 0);
    m.kind = kModLfo, m.phase = 3;
    ModKnob::moreText(m, text, sizeof text);
    CHECK(std::strcmp(text, "3/4") == 0 && std::strcmp(ModKnob::moreName(m), "PHASE") == 0);
    ModKnob::depthText(0.25f, text, sizeof text);
    CHECK(std::strcmp(text, "+25%") == 0);
    ModKnob::depthText(-1.f, text, sizeof text);
    CHECK(std::strcmp(text, "-100%") == 0);
    ModKnob::depthText(0.f, text, sizeof text);
    CHECK(std::strcmp(text, "0%") == 0);
    // The dice: an LFO's shape, rate and depth, never all three as they were; a STEPPER's row.
    for (std::uint32_t seed = 0; seed < 300; ++seed) {
        ModSettings lfo = ModKnob::fresh(8);
        lfo.depth = -0.37f;
        const ModSettings r = ModKnob::rolled(lfo, seed);
        CHECK(r.kind == kModLfo && r.target == 8 && (r.shape != lfo.shape || r.rate != lfo.rate || r.depth != lfo.depth));
        CHECK(r.rate >= kModRateEighth && r.rate <= kModRateFourBars);
        const float percent = r.depth * 100.f; // whole percents, 20 to 100 either way
        CHECK(std::fabs(percent) >= 20.f && std::fabs(percent) <= 100.f && std::fabs(percent - std::round(percent)) < 1e-3f);
    }
    int shapes = 0, rates = 0, signs = 0, depths = 0;
    float lastDepth = 999.f;
    for (std::uint32_t seed = 0; seed < 300; ++seed) {
        const ModSettings r = ModKnob::rolled(ModKnob::fresh(8), seed);
        shapes |= 1 << r.shape, rates |= 1 << r.rate;
        signs |= r.depth > 0.f ? 1 : 2;
        if (r.depth != lastDepth) ++depths;
        lastDepth = r.depth;
    }
    CHECK(shapes == (1 << kModShapes) - 1); // every shape comes up
    CHECK(rates == 0xff << kModRateEighth); // and the eight rates
    CHECK(signs == 3 && depths > 200);      // depth both ways, and rarely twice the same
    ModSettings stepper = ModKnob::fresh(20);
    stepper.kind = kModStepper, stepper.length = 5, stepper.slew = 0.3f, stepper.chance = 0.2f;
    const ModSettings a = ModKnob::rolled(stepper, 1), b = ModKnob::rolled(stepper, 2);
    CHECK(a.length == 5 && a.slew == 0.3f && a.chance == 0.2f && a.depth == stepper.depth && a.rate == stepper.rate);
    bool differ = false, whole = true, within = true;
    for (int i = 0; i < kModSteps; ++i) {
        differ = differ || a.steps[i] != b.steps[i];
        whole = whole && std::fabs(a.steps[i] * 100.f - std::round(a.steps[i] * 100.f)) < 1e-3f;
        within = within && a.steps[i] >= -1.f && a.steps[i] <= 1.f;
    }
    CHECK(differ && whole && within);
}

// ---- FREE (owner, 2026-11-15) -------------------------------------------------------------------

// RATE's last is FREE (an LFO's only), and L1 / R1 is then SPEED, a click of its list; the dice keep
// it free; A to a STEPPER puts it at 1 BAR, whose RATE stops at 8 BARS. Drawn: RATE says FREE, the
// last field SPEED and its Hz, each chip still under its field, and the longest Hz clear of what is
// beside it, on DEVICE and on FX.
void testFreeOnTheKnob() {
    char text[16];
    ModSettings m = ModKnob::fresh(3);
    m.rate = kModRateFree, m.speed = 0.25f, m.phase = 2;
    ModKnob::moreText(m, text, sizeof text);
    CHECK(std::strcmp(text, "0.25 HZ") == 0 && std::strcmp(ModKnob::moreName(m), "SPEED") == 0 && ModKnob::rates(m) == kModRates);
    m.kind = kModStepper;
    CHECK(std::strcmp(ModKnob::moreName(m), "LENGTH") == 0 && ModKnob::rates(m) == kModTempoRates);
    // The dice on a FREE LFO: it stays free; SHAPE, SPEED and DEPTH, never all three as they were, a click 0.1 to 5 Hz.
    int shapes = 0, speeds = 0;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        ModSettings f = ModKnob::fresh(8);
        f.rate = kModRateFree, f.depth = -0.4f, f.speed = 1.f;
        const ModSettings r = ModKnob::rolled(f, seed);
        CHECK(r.rate == kModRateFree && r.kind == kModLfo && (r.shape != f.shape || r.speed != f.speed || r.depth != f.depth));
        CHECK(r.speed >= 0.1f && r.speed <= 5.f && modSpeedStep(modSpeedStep(r.speed, -1), 1) == r.speed && r.phase == f.phase);
        const float percent = r.depth * 100.f; // whole percents, 20 to 100 either way
        CHECK(std::fabs(percent) >= 20.f && std::fabs(percent) <= 100.f && std::fabs(percent - std::round(percent)) < 1e-3f);
        shapes |= 1 << r.shape;
        for (int i = ModKnob::kSpeedRollFirst; i <= ModKnob::kSpeedRollLast; ++i) speeds |= (r.speed == modSpeedClick(i)) << (i - ModKnob::kSpeedRollFirst);
    }
    CHECK(shapes == (1 << kModShapes) - 1 && speeds == (1 << (ModKnob::kSpeedRollLast - ModKnob::kSpeedRollFirst + 1)) - 1);
    CHECK(modSpeedClick(ModKnob::kSpeedRollFirst) == 0.1f && modSpeedClick(ModKnob::kSpeedRollLast) == 5.f);

    Rig rig;
    const int t = rig.phazed;
    rig.device(t, 0);
    rig.key(X, true);
    rig.tap(SDLK_UP); // a fresh LFO: 1 BAR
    rig.tap(SDLK_RIGHT, 3);
    CHECK(rig.mod(t, 0).rate == kModRateEightBars);
    rig.tap(SDLK_RIGHT);
    CHECK(modFree(rig.mod(t, 0)) && rig.mod(t, 0).speed == 1.f);
    rig.tap(SDLK_RIGHT, 2);
    CHECK(rig.mod(t, 0).rate == kModRateFree); // the end stops
    // L1 / R1: SPEED, a click; the ends stop; PHASE is left as it was.
    rig.tap(R1);
    CHECK(rig.mod(t, 0).speed == 1.2f && rig.mod(t, 0).phase == 0);
    rig.tap(L1, 3);
    CHECK(rig.mod(t, 0).speed == 0.6f);
    rig.tap(R1, 40);
    CHECK(rig.mod(t, 0).speed == 20.f);
    rig.tap(L1, 40);
    CHECK(rig.mod(t, 0).speed == 0.02f);
    rig.tap(R1, 17);
    CHECK(rig.mod(t, 0).speed == 1.f);
    // The stick sideways: RATE clicks, and stops at FREE too.
    rig.key(StickRight, true), rig.frames(30), rig.key(StickRight, false), rig.frames(1);
    CHECK(rig.mod(t, 0).rate == kModRateFree);
    // Y: still free.
    rig.tap(Y);
    CHECK(modFree(rig.mod(t, 0)));
    const float rolled = rig.mod(t, 0).speed;
    // A to a STEPPER: at 1 BAR, its RATE stopping at 8 BARS; back to an LFO it is at 1 BAR, its SPEED kept.
    rig.tap(A, kModShapes - rig.mod(t, 0).shape);
    CHECK(rig.mod(t, 0).kind == kModStepper && rig.mod(t, 0).rate == kModRateBar && rig.mod(t, 0).speed == rolled);
    rig.tap(SDLK_RIGHT, 6);
    CHECK(rig.mod(t, 0).rate == kModRateEightBars);
    rig.tap(A);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).rate == kModRateEightBars);
    rig.tap(SDLK_RIGHT);
    CHECK(modFree(rig.mod(t, 0)) && rig.mod(t, 0).speed == rolled);
    rig.key(X, false);
    rig.frames(2);

    // What is drawn.
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    ModSettings f = lfoOn(kModFirstMacro);
    f.rate = kModRateFree, f.speed = 0.025f;
    rig.session().setMod(t, 0, f);
    rig.device(t, 0);
    rig.key(X, true);
    rig.draw();
    const ModPlaces& p = kDeviceModPlaces;
    CHECK(rig.drawn(p.rate, p.fieldsY, "RATE") && rig.drawn(p.rate, p.fieldsY + kModValueDrop, "FREE", true, ui::kText));
    CHECK(rig.drawn(p.more, p.fieldsY, "SPEED") && rig.drawn(p.more, p.fieldsY + kModValueDrop, "0.025 HZ", true, ui::kText));
    const Chips all = {{8, rig.chipW("Y")},
                       {static_cast<int>(p.lead), rig.chipW("HOLD B")},
                       {static_cast<int>(p.shape), rig.chipW("A")},
                       {static_cast<int>(p.depth), rig.chipW("UP / DOWN")},
                       {static_cast<int>(p.rate), rig.chipW("LEFT / RIGHT")},
                       {static_cast<int>(p.more), rig.chipW("L1 / R1")}};
    CHECK(rig.chips() == all && rig.chipIs(p.more, "L1 / R1"));
    rig.key(X, false);
    // The widest SPEED there can be (the stick reaches any): clear of NOW on DEVICE, inside the panel on FX.
    const Font& medium = rig.canvas.fonts->medium;
    float widest = 0.f;
    for (int i = 0; i <= 3000; ++i) {
        ModKnob::speedText(kModSpeedLeast * std::pow(1000.f, static_cast<float>(i) / 3000.f), text, sizeof text);
        widest = std::max(widest, medium.width(text));
    }
    if (printing) std::printf("the widest SPEED: %.1f pixels (\"0.025 HZ\" %.1f); 1/2 STEP %.1f, -100%% %.1f, RAMP DOWN %.1f\n", static_cast<double>(widest), static_cast<double>(medium.width("0.025 HZ")), static_cast<double>(medium.width("1/2 STEP")), static_cast<double>(medium.width("-100%")), static_cast<double>(medium.width("RAMP DOWN")));
    CHECK(widest >= medium.width("0.025 HZ") && p.more + widest + 6.f <= p.nowX);
    CHECK(kFxModPlaces.more + widest <= FxScreen::kPaneX + FxScreen::kPaneW - 6 && kFxModPlaces.rate + medium.width("1/2 STEP") + 6.f <= kFxModPlaces.more);
    // On FX: the same two fields.
    rig.session().setEffect(t, 0, EffectKind::Filter);
    f.target = kModFirstEffect;
    rig.session().setMod(t, 1, f);
    rig.fx(t, 0, 1);
    rig.key(X, true);
    rig.draw();
    const float top = static_cast<float>(FxScreen::kPaneY);
    const ModPlaces& q = kFxModPlaces;
    CHECK(rig.drawn(q.rate, top + q.fieldsY + kModValueDrop, "FREE", true, ui::kText) && rig.drawn(q.more, top + q.fieldsY, "SPEED") && rig.drawn(q.more, top + q.fieldsY + kModValueDrop, "0.025 HZ", true, ui::kText));
    CHECK(rig.chipIs(q.more, "L1 / R1") && rig.chipIs(q.rate, "LEFT / RIGHT"));
    rig.key(X, false);
}

// ---- DEVICE MAIN ------------------------------------------------------------------------------

void testTheGestureOnAMacro() {
    Rig rig;
    const int t = rig.phazed;
    SynthScreen& screen = rig.device(t, 0); // TONE
    // X held on a knob with no MOD: the strip says so, and nothing is made yet.
    rig.key(X, true);
    rig.frames(1);
    CHECK(screen.modKnob().held() && screen.modKnob().target() == kModFirstMacro && screen.modKnob().mod() < 0);
    CHECK(rig.mod(t, 0).kind == kModOff && rig.mod(t, 1).kind == kModOff);
    // The first UP makes one: LFO, SINE, 1 BAR, +25% (owner's call C), in MOD 1.
    rig.tap(SDLK_DOWN); // (DOWN makes nothing)
    CHECK(rig.mod(t, 0).kind == kModOff);
    rig.tap(SDLK_UP);
    const ModSettings made = rig.mod(t, 0);
    CHECK(made.kind == kModLfo && made.target == kModFirstMacro && made.shape == kModSine && made.rate == kModRateBar && made.depth == 0.25f);
    CHECK(screen.modKnob().mod() == 0);
    // UP / DOWN: DEPTH by 1%, on whole percents.
    rig.tap(SDLK_UP, 2);
    CHECK(std::fabs(rig.mod(t, 0).depth - 0.27f) < 1e-6f);
    rig.tap(SDLK_DOWN);
    CHECK(std::fabs(rig.mod(t, 0).depth - 0.26f) < 1e-6f);
    // LEFT / RIGHT: RATE, the ends stop.
    rig.tap(SDLK_RIGHT);
    CHECK(rig.mod(t, 0).rate == kModRateTwoBars);
    rig.tap(SDLK_RIGHT, 5);
    CHECK(rig.mod(t, 0).rate == kModRateFree); // (an LFO's last: FREE)
    rig.tap(SDLK_LEFT, 20);
    CHECK(rig.mod(t, 0).rate == kModRateHalfStep);
    // L1 / R1: PHASE, the ends stop.
    rig.tap(R1, 2);
    CHECK(rig.mod(t, 0).phase == 2);
    rig.tap(R1, 4);
    CHECK(rig.mod(t, 0).phase == 3);
    rig.tap(L1, 9);
    CHECK(rig.mod(t, 0).phase == 0);
    // A: the next shape, and after DRIFT a STEPPER, its row rolled (one of nothing moves nothing); then SINE.
    rig.tap(A);
    CHECK(rig.mod(t, 0).shape == kModTriangle && rig.mod(t, 0).kind == kModLfo);
    rig.tap(A, 5);
    CHECK(rig.mod(t, 0).shape == kModDrift);
    rig.tap(A);
    CHECK(rig.mod(t, 0).kind == kModStepper);
    bool any = false;
    for (int i = 0; i < kModSteps; ++i) any = any || rig.mod(t, 0).steps[i] != 0.f;
    CHECK(any);
    // A STEPPER's L1 / R1 is its LENGTH.
    rig.tap(L1, 3);
    CHECK(rig.mod(t, 0).length == 13 && rig.mod(t, 0).phase == 0);
    rig.tap(R1, 9);
    CHECK(rig.mod(t, 0).length == 16);
    // Y: the dice. A STEPPER's row; DEPTH, RATE and LENGTH kept.
    const ModSettings before = rig.mod(t, 0);
    rig.tap(Y);
    CHECK(rig.mod(t, 0).kind == kModStepper && rig.mod(t, 0).depth == before.depth && rig.mod(t, 0).rate == before.rate);
    CHECK(std::memcmp(rig.mod(t, 0).steps, before.steps, sizeof before.steps) != 0);
    rig.tap(A); // back to an LFO, SINE: its row is kept
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).shape == kModSine);
    rig.tap(Y);
    CHECK(rig.mod(t, 0).kind == kModLfo && (rig.mod(t, 0).shape != kModSine || rig.mod(t, 0).rate != before.rate || rig.mod(t, 0).depth != before.depth));
    CHECK(std::fabs(rig.mod(t, 0).depth) >= 0.2f && std::fabs(rig.mod(t, 0).depth) <= 1.f);
    // The stick: up turns DEPTH smoothly, and the cursor stays on the knob.
    const float depth = rig.mod(t, 0).depth;
    rig.key(StickUp, true);
    rig.frames(20);
    rig.key(StickUp, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).depth > depth + 0.1f && screen.cursor() == SynthScreen::kFirstMacro);
    // ...and right clicks RATE, once a push, then steadily.
    const int rate = rig.mod(t, 0).rate;
    rig.key(StickRight, true);
    rig.frames(1);
    rig.key(StickRight, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).rate == rate + 1 && screen.cursor() == SynthScreen::kFirstMacro);
    // Held over: one at once, then twice a held D-pad's list pace (owner, 2026-11-25: 13.6 a second at STICK SPEED
    // 75%; it was five times 75%): in 10 frames, 1 + 1.96.
    rig.key(StickRight, true);
    rig.frames(10);
    rig.key(StickRight, false);
    rig.frames(1);
    CHECK(ui::gStickSpeed == 0.75f && rate + 3 < kModRateFree && rig.mod(t, 0).rate == rate + 3);
    // A held D-pad: DEPTH runs on, a percent at a time.
    const float runFrom = rig.mod(t, 0).depth;
    rig.key(SDLK_DOWN, true);
    rig.frames(30);
    rig.key(SDLK_DOWN, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).depth < runFrom - 0.1f);
    // Hold B: let go before the second, nothing; held through it, the MOD is deleted.
    rig.key(B, true);
    rig.frames(30);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).kind == kModLfo);
    rig.key(B, true);
    rig.frames(20);
    rig.key(A, true); // another press calls it off
    rig.frames(50);
    rig.key(B, false), rig.key(A, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).kind == kModLfo);
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.mod(t, 0).kind == kModOff);
    // B's own job (back to the middle) was not done meanwhile.
    CHECK(rig.project().tracks[t].macro[0] == 0.5f);
    rig.key(X, false);
    rig.frames(1);
    CHECK(!screen.modKnob().held());
    // Undo puts the MOD back (a step of undo through soundMark).
    rig.frames(60);
    CHECK(rig.session().undo());
    rig.frames(2);
    CHECK(rig.mod(t, 0).kind == kModLfo);
}

void testFullAndChoices() {
    Rig rig;
    const int t = rig.phazed;
    // Both MODs on other knobs: X held on a third says MODS FULL, and UP makes nothing.
    CHECK(rig.session().setMod(t, 0, lfoOn(kModFirstMacro + 1)));
    CHECK(rig.session().setMod(t, 1, lfoOn(kModFirstSend)));
    SynthScreen& screen = rig.device(t, 0);
    rig.key(X, true);
    rig.frames(1);
    CHECK(screen.modKnob().held() && screen.modKnob().full() && screen.modKnob().mod() < 0);
    rig.tap(SDLK_UP);
    rig.key(StickUp, true);
    rig.frames(5);
    rig.key(StickUp, false);
    rig.tap(A), rig.tap(Y), rig.tap(R1), rig.tap(SDLK_RIGHT);
    CHECK(rig.mod(t, 0).target == kModFirstMacro + 1 && rig.mod(t, 1).target == kModFirstSend);
    CHECK(rig.mod(t, 0).depth == 0.5f && rig.mod(t, 0).shape == kModSine && rig.mod(t, 0).rate == kModRateBar);
    rig.key(X, false);
    rig.frames(1);
    // On the knob that has MOD 1: that one, not MOD 2.
    screen.goTo(0, SynthScreen::kFirstMacro + 1);
    rig.key(X, true);
    rig.frames(1);
    CHECK(screen.modKnob().mod() == 0 && !screen.modKnob().full());
    rig.key(X, false);
    // One freed: the next UP takes it (the first free: MOD 1).
    rig.session().setMod(t, 0, ModSettings());
    screen.goTo(0, SynthScreen::kFirstMacro + 2);
    rig.key(X, true);
    rig.tap(SDLK_UP);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).target == kModFirstMacro + 2);
    rig.key(X, false);
    // A choice (Phazed CHORUS, OCTAVE) takes no MOD: X only hears there, and the SIGNAL strip stays.
    screen.goTo(0, SynthScreen::kFirstMacro + 6);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!screen.modKnob().held());
    rig.tap(SDLK_UP); // the cursor moves: no MOD gesture here
    CHECK(screen.cursor() != SynthScreen::kFirstMacro + 6);
    rig.key(X, false);
    // Not on FOLDER, PRESET or ADVANCED, nor with A held.
    screen.goTo(0, SynthScreen::kPreset);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!screen.modKnob().held());
    rig.key(X, false);
    screen.goTo(0, SynthScreen::kFirstMacro + 3);
    rig.key(A, true);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!screen.modKnob().held());
    rig.key(X, false), rig.key(A, false);
    rig.frames(1);
}

// The order of the checks (plan, section 7): with X held, Y is the dice. Y alone does nothing on a
// synth's DEVICE screen (it was QMOD's ROLL until QMOD went), pressed before X or not. The stick does
// not move the cursor while X is held.
void testTheOrderOnDevice() {
    Rig rig;
    const int t = rig.fm6;
    SynthScreen& screen = rig.device(t, 0);
    const SynthSpec& spec = *specFor(TrackKind::Fm6);
    const auto sound = [&]() {
        const int* v = spec.values(rig.project().tracks[t]);
        return std::vector<int>(v, v + settingCount(TrackKind::Fm6));
    };
    const std::vector<int> first = sound();
    rig.tap(Y); // Y alone: nothing
    CHECK(sound() == first);
    // Y held before X, X pressed after: X is the MOD's hold from then.
    rig.key(Y, true);
    rig.frames(1);
    const std::vector<int> rolledAgain = sound();
    CHECK(rolledAgain == first);
    rig.key(X, true);
    rig.frames(1);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(sound() == rolledAgain); // (letting go of Y rolls nothing)
    // X held: Y is the dice (no MOD yet: nothing).
    rig.tap(Y);
    CHECK(sound() == rolledAgain);
    rig.tap(SDLK_UP);
    CHECK(rig.mod(t, 0).kind == kModLfo);
    const ModSettings before = rig.mod(t, 0);
    rig.tap(Y);
    CHECK(sound() == rolledAgain && (rig.mod(t, 0).shape != before.shape || rig.mod(t, 0).rate != before.rate));
    // The stick held over, every way: the cursor stays on the knob.
    for (SDL_Keycode stick : {SDLK_i, SDLK_k, SDLK_j, SDLK_l}) {
        rig.key(stick, true);
        rig.frames(25);
        rig.key(stick, false);
        rig.frames(1);
        CHECK(screen.cursor() == SynthScreen::kFirstMacro);
    }
    // ...and the D-pad's sideways is RATE, not the cursor.
    rig.tap(SDLK_RIGHT);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro);
    // B with X held is not B's own job (the macro back to the middle).
    rig.project().tracks[t].macro[0] = 0.7f;
    rig.tap(B);
    CHECK(rig.project().tracks[t].macro[0] == 0.7f);
    rig.key(X, false);
    rig.frames(1);
    // A stick pushed while X is let go moves nothing until it has come back.
    rig.key(X, true);
    rig.frames(1);
    rig.key(SDLK_k, true);
    rig.frames(3);
    rig.key(X, false);
    rig.frames(3); // (held on, the stick goes round the four rows: a few frames show it)
    CHECK(screen.cursor() == SynthScreen::kFirstMacro);
    rig.key(SDLK_k, false);
    rig.frames(1);
    rig.key(SDLK_k, true);
    rig.frames(1);
    rig.key(SDLK_k, false);
    rig.frames(1);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 4); // pushed again: it moves
}

// While stopped, X held on a knob with a MOD runs it, and the note is held: it is heard. Let go, another
// screen, Play: it is let go. Playing, nothing is sent (the song's MODs run).
void testHeardWhileStopped() {
    Rig rig;
    const int t = rig.phazed;
    CHECK(rig.session().setMod(t, 1, lfoOn(kModFirstMacro, 0.8f)));
    rig.device(t, 0);
    CHECK(rig.session().modsHeard(t) == 0);
    rig.key(X, true);
    rig.frames(1);
    CHECK(rig.session().modsHeard(t) == 2u); // MOD 2 is the one on TONE
    CHECK(rig.session().hearingHeld());
    rig.frames(30);
    CHECK(rig.engine->modShown(t, 1) > 0.05f && rig.engine->modShown(t, 0) == 0.f);
    CHECK(rig.engine->modOffsetForTests(t, kModFirstMacro) > 0.05f);
    // Let go: the engine lets go, the knob is given its centre.
    rig.key(X, false);
    rig.frames(2);
    CHECK(rig.session().modsHeard(t) == 0 && !rig.session().hearingHeld());
    CHECK(rig.engine->modShown(t, 1) == 0.f && rig.engine->modOffsetForTests(t, kModFirstMacro) == 0.f);
    // A knob with no MOD: nothing runs.
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro + 1);
    rig.key(X, true);
    rig.frames(2);
    CHECK(rig.session().modsHeard(t) == 0);
    // ...until UP makes one: it runs from then.
    rig.tap(SDLK_UP);
    rig.frames(1);
    CHECK(rig.session().modsHeard(t) == 1u);
    // Another screen up while X is held: let go.
    rig.session().view = View::Song;
    rig.frames(2);
    CHECK(rig.session().modsHeard(t) == 0);
    rig.session().view = View::Device;
    rig.frames(2);
    CHECK(rig.session().modsHeard(t) == 1u); // (back, X still held: it runs again)
    // Play: the song's MODs run, nothing is sent; Stop with X still held: it runs again.
    rig.key(SDLK_RETURN, true), rig.key(SDLK_RETURN, false);
    rig.frames(3);
    CHECK(rig.project().playing && rig.session().modsHeard(t) == 0);
    rig.key(SDLK_RETURN, true), rig.key(SDLK_RETURN, false);
    rig.frames(3);
    CHECK(!rig.project().playing && rig.session().modsHeard(t) == 1u);
    rig.key(X, false);
    rig.frames(2);
    CHECK(rig.session().modsHeard(t) == 0);
    // A tap is still a tap: the note heard kAuditionMs (the Session's rule), not cut by the MOD.
    rig.key(X, true);
    rig.key(X, false);
    rig.frames(2);
    CHECK(rig.session().heardEnds() != 0);
}

// A MOD turned while it is on sends only what changed (Session::setMod): the engine holds what the
// project holds all the same. Read through what it adds: a SQUARE is +-DEPTH exactly, a STEPPER's
// first value DEPTH x that value, while X holds it stopped.
void testOnlyWhatChangedIsSent() {
    Rig rig;
    const int t = rig.phazed;
    ModSettings m = lfoOn(kModFirstMacro, 0.25f);
    m.shape = kModSquare;
    CHECK(rig.session().setMod(t, 0, m));
    rig.device(t, 0);
    rig.key(X, true);
    rig.frames(3);
    CHECK(std::fabs(rig.engine->modOffsetForTests(t, kModFirstMacro)) == 0.25f);
    m.depth = 0.8f; // one field
    CHECK(rig.session().setMod(t, 0, m));
    rig.frames(3);
    CHECK(std::fabs(rig.engine->modOffsetForTests(t, kModFirstMacro)) == 0.8f);
    m.kind = kModStepper, m.rate = kModRateEightBars; // two fields and the row
    m.steps[0] = 0.5f;
    CHECK(rig.session().setMod(t, 0, m));
    rig.key(X, false);
    rig.frames(2);
    rig.key(X, true);
    rig.frames(3);
    CHECK(std::fabs(rig.engine->modOffsetForTests(t, kModFirstMacro) - 0.4f) < 1e-6f);
    // Aimed elsewhere: the knob it left is let go.
    m.target = kModFirstMacro + 1;
    CHECK(rig.session().setMod(t, 0, m));
    rig.frames(3);
    CHECK(rig.engine->modOffsetForTests(t, kModFirstMacro) == 0.f);
    rig.key(X, false);
    rig.frames(1);
    // Panic while X runs one: let go, and X still held after it runs it again.
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro + 1);
    rig.key(X, true);
    rig.frames(3);
    CHECK(rig.session().modsHeard(t) == 1u && rig.engine->modOffsetForTests(t, kModFirstMacro + 1) != 0.f);
    rig.session().panic(true);
    rig.frames(2);
    rig.session().panic(false);
    rig.frames(3);
    CHECK(rig.session().modsHeard(t) == 1u && rig.engine->modOffsetForTests(t, kModFirstMacro + 1) != 0.f);
    rig.key(X, false);
    rig.frames(1);
}

// The stick and the buttons held across another screen: what was pushed or let go out of sight
// does nothing it was not meant to.
void testHeldAcrossThings() {
    Rig rig;
    const int t = rig.phazed;
    SynthScreen& screen = rig.device(t, 0);
    // No MOD: the stick pushed up makes one, as UP does.
    rig.key(X, true);
    rig.frames(1);
    rig.key(StickUp, true);
    rig.frames(1);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).depth >= 0.25f);
    rig.key(StickUp, false);
    rig.key(X, false);
    rig.frames(1);
    // The stick already pushed when X goes down (the cursor on its way to MOTION) clicks nothing until it
    // has come back.
    CHECK(rig.session().setMod(t, 1, lfoOn(kModFirstMacro + 1)));
    rig.key(StickRight, true);
    rig.frames(1);
    CHECK(screen.cursor() == SynthScreen::kFirstMacro + 1);
    rig.key(X, true);
    rig.frames(30);
    CHECK(rig.mod(t, 1).rate == kModRateBar);
    rig.key(StickRight, false);
    rig.frames(1);
    rig.key(StickRight, true);
    rig.frames(1);
    rig.key(StickRight, false);
    rig.frames(1);
    CHECK(rig.mod(t, 1).rate == kModRateTwoBars && screen.cursor() == SynthScreen::kFirstMacro + 1);
    rig.key(X, false);
    rig.frames(1);
    // B held, another screen up, B let go there, back: nothing is deleted.
    screen.goTo(0, SynthScreen::kFirstMacro);
    rig.key(X, true);
    rig.frames(1);
    rig.key(B, true);
    rig.frames(10);
    rig.session().view = View::Song;
    rig.frames(2);
    rig.key(B, false);
    rig.frames(2);
    rig.session().view = View::Device;
    rig.frames(80);
    CHECK(rig.mod(t, 0).kind == kModLfo && screen.modKnob().held());
    // X let go on another screen: back, it is not held.
    rig.session().view = View::Song;
    rig.frames(2);
    rig.key(X, false);
    rig.frames(2);
    rig.session().view = View::Device;
    rig.frames(2);
    CHECK(!screen.modKnob().held());
    // FX: with A held (turning), X is not the MOD's; X held, the stick turns DEPTH and the cursor stays.
    rig.session().setMod(t, 1, ModSettings());
    rig.session().setEffect(t, 0, EffectKind::Filter);
    FxScreen& fx = rig.fx(t, 0, 1);
    rig.key(A, true);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!fx.modKnob().held());
    rig.key(X, false), rig.key(A, false);
    rig.frames(1);
    rig.key(X, true);
    rig.tap(SDLK_UP);
    const float fxDepth = rig.mod(t, 1).depth;
    CHECK(rig.mod(t, 1).target == kModFirstEffect + 0);
    for (SDL_Keycode stick : {SDLK_i, SDLK_k, SDLK_j, SDLK_l}) {
        rig.key(stick, true);
        rig.frames(10);
        rig.key(stick, false);
        rig.frames(1);
        CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 1);
        if (stick == SDLK_i) CHECK(rig.mod(t, 1).depth > fxDepth + 0.05f);
    }
    // X let go with the stick still pushed: the cursor waits for it to come back.
    rig.key(SDLK_k, true);
    rig.frames(1);
    rig.key(X, false);
    rig.frames(3);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 1);
    rig.key(SDLK_k, false);
    rig.frames(1);
    rig.key(SDLK_k, true);
    rig.frames(1);
    rig.key(SDLK_k, false);
    rig.frames(1);
    CHECK(fx.cursorColumn() == 2); // pushed again: it moves
}

// X held across a change of track (picked on SONG, X never let go): the gesture lets go of the old
// track's knob, the old track's MOD stops running, and UP touches neither track (review, 2026-11-15:
// the knob stayed on the old track, its audition began again every frame, and UP changed it).
void pickOnSong(Rig& rig, int track) {
    for (int i = 0; i < 3 && rig.session().view != View::Song; ++i) rig.tap(SDLK_1); // L2 to SONG
    CHECK(rig.session().view == View::Song);
    for (int i = 0; i < kNumTracks && rig.project().track != track; ++i) {
        rig.tap(SDLK_RIGHT);
        rig.tap(A); // a cell: its track, and the PATTERN
        if (rig.project().track != track && rig.session().view != View::Song) rig.tap(SDLK_1);
    }
    CHECK(rig.project().track == track);
}

void testTrackChangedWhileHeld() {
    for (int device = 0; device < 2; ++device) {
        Rig rig;
        CHECK(rig.session().addTrack(TrackKind::Phazed));
        const int a = rig.phazed, b = device ? rig.project().trackCount() - 1 : rig.fm6; // DEVICE: two Phazed tracks, one screen
        const int target = device ? kModFirstMacro : kModFirstEffect;
        rig.session().setEffect(a, 0, EffectKind::Filter);
        rig.session().setEffect(b, 0, EffectKind::Filter);
        CHECK(rig.session().setMod(a, 0, lfoOn(target, 0.6f)));
        if (device) rig.device(a, 0);
        else rig.fx(a, 0, 1);
        const ModKnob& knob = device ? rig.app.phazed().modKnob() : rig.app.fx().modKnob();
        rig.key(X, true);
        rig.frames(20);
        CHECK(knob.held() && knob.track() == a && rig.session().modsHeard(a) == 1u);
        pickOnSong(rig, b);
        rig.tap(SDLK_2); // R2: PATTERN -> DEVICE
        if (!device) rig.tap(SDLK_2); // -> EFFECTS
        CHECK(rig.session().view == (device ? View::Device : View::Effects));
        rig.frames(3);
        CHECK(!knob.held());
        CHECK(rig.session().modsHeard(a) == 0 && rig.session().modsHeard(b) == 0);
        CHECK(rig.engine->modOffsetForTests(a, target) == 0.f);
        rig.tap(SDLK_UP); // X still held, but not on the knob: nothing is made or turned
        CHECK(rig.mod(a, 0).depth == 0.6f && rig.mod(b, 0).kind == kModOff && rig.mod(b, 1).kind == kModOff);
        rig.key(X, false);
        rig.frames(2);
        // X pressed again on the knob shown: it is that track's.
        if (device) rig.app.phazed().goTo(0, SynthScreen::kFirstMacro);
        else rig.app.fx().goTo(0, 1);
        rig.frames(1);
        rig.key(X, true);
        rig.frames(1);
        CHECK(knob.held() && knob.track() == b);
        rig.key(X, false);
        rig.frames(2);
    }
}

// The Session's own rule, apart from any screen: what runs while stopped is the track shown's. (The
// screens let go first now, so no gesture reaches this; it stays as the second guard.)
void testHeardOnlyForTheTrackShown() {
    Rig rig;
    const int a = rig.phazed, b = rig.fm6;
    CHECK(rig.session().setMod(a, 0, lfoOn(kModFirstMacro, 0.6f)));
    int owner = 0;
    rig.project().track = a;
    rig.session().hearMods(a, 1u, &owner);
    rig.session().keepModsHeard(true, &owner);
    CHECK(rig.session().modsHeard(a) == 1u);
    rig.project().track = b;
    rig.session().keepModsHeard(true, &owner);
    CHECK(rig.session().modsHeard(a) == 0);
}

// An undo with X held (SELECT + B works on every screen) that takes the setting under the cursor away:
// the knob is let go, never left on a setting the slot no longer has.
void testUndoTakesTheKnobAway() {
    Rig rig;
    const int t = rig.phazed;
    rig.session().setEffect(t, 0, EffectKind::Reverb); // three settings (REVERB) ...
    rig.frames(200);
    rig.session().setEffect(t, 0, EffectKind::Filter); // ... then four (FILTER's MIX the fourth)
    CHECK(effectInfo(EffectKind::Reverb).params == 3 && effectInfo(EffectKind::Filter).params == 4);
    rig.frames(200);
    FxScreen& fx = rig.fx(t, 0, 4);
    rig.key(X, true);
    rig.frames(1);
    rig.tap(SDLK_UP);
    rig.frames(200);
    CHECK(fx.modKnob().held() && fx.modKnob().target() == kModFirstEffect + 3 && rig.mod(t, 0).kind == kModLfo);
    for (int u = 0; u < 2; ++u) { // the MOD, then the FILTER
        rig.key(SDLK_BACKSPACE, true), rig.key(B, true);
        rig.frames(1);
        rig.key(B, false), rig.key(SDLK_BACKSPACE, false);
        rig.frames(2);
    }
    CHECK(rig.session().effectData(t, 0).kind == static_cast<int>(EffectKind::Reverb) && fx.cursorColumn() == 3);
    CHECK(!fx.modKnob().held());
    rig.tap(SDLK_UP); // nothing is made for a setting that is not there
    CHECK(rig.mod(t, 0).kind == kModOff && rig.mod(t, 1).kind == kModOff);
    rig.key(X, false);
    rig.frames(2);
}

// X let go while the queue is full: the Session keeps asking until the engine is told, and the MOD
// stops (letGoOfMods forgets only a let go that went).
void testLetGoLostToAFullQueue() {
    Rig rig;
    const int t = rig.phazed;
    CHECK(rig.session().setMod(t, 0, lfoOn(kModFirstMacro, 0.8f)));
    rig.device(t, 0);
    rig.key(X, true);
    rig.frames(20);
    CHECK(rig.session().modsHeard(t) == 1u && rig.engine->modOffsetForTests(t, kModFirstMacro) != 0.f);
    Command c;
    c.type = Command::SetSend;
    c.track = 0;
    c.a = 0;
    c.value = 0.f;
    int sent = 0;
    while (rig.engine->send(c) && sent < 100000) ++sent; // (nothing renders: the queue fills)
    CHECK(sent < 100000);
    rig.key(X, false);
    rig.clock += 16;
    rig.app.update(rig.input, rig.clock); // the let go is asked for, and lost
    CHECK(rig.session().modsHeard(t) == 1u);
    rig.frames(10); // room again: asked again, and it goes
    CHECK(rig.session().modsHeard(t) == 0 && rig.engine->modOffsetForTests(t, kModFirstMacro) == 0.f);
    rig.frames(30);
    CHECK(rig.engine->modOffsetForTests(t, kModFirstMacro) == 0.f);
}

// The fields are a tight row: drawn the same at every TEXT SIZE (the chips stand under them at LARGE).
void testTheSameAtEverySize() {
    Rig rig;
    if (!rig.canvas.ok) return;
    const int t = rig.phazed;
    CHECK(rig.session().setMod(t, 0, lfoOn(kModFirstMacro)));
    rig.session().setEffect(t, 0, EffectKind::Filter);
    CHECK(rig.session().setMod(t, 1, lfoOn(kModFirstEffect)));
    Fonts xl;
    xl.fixedSet = rig.canvas.fonts.get();
    CHECK(xl.load(rig.canvas.r, loadFontFile(), kTextXl));
    for (int screen = 0; screen < 2; ++screen) {
        if (screen == 0) rig.device(t, 0);
        else rig.fx(t, 0, 1);
        rig.key(X, true);
        rig.frames(1);
        const SDL_Rect strip = screen == 0 ? SDL_Rect{ui::kScopeX, ui::kScopeY, ui::kScopeW, ui::kScopeH} : SDL_Rect{FxScreen::kPaneX, FxScreen::kPaneY, FxScreen::kPaneW, FxScreen::kHeadH};
        rig.canvas.clear();
        rig.app.draw(rig.canvas.r, *rig.canvas.fonts);
        const std::vector<Uint32> large = rig.canvas.part(strip.x, strip.y, strip.w, strip.h);
        rig.canvas.clear();
        rig.app.draw(rig.canvas.r, xl);
        CHECK(rig.canvas.part(strip.x, strip.y, strip.w, strip.h) == large);
        rig.key(X, false);
        rig.frames(1);
    }
    xl.unload();
}

// ---- what is drawn: DEVICE ---------------------------------------------------------------------

void testDrawnOnDevice() {
    Rig rig;
    CHECK(rig.canvas.ok && rig.scratch.ok);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    const int t = rig.phazed;
    rig.device(t, 0);
    const ModPlaces& p = kDeviceModPlaces;
    // Nothing held: SIGNAL and the plain bar (with HOLD X: MOD, as it fits).
    rig.draw();
    const ui::Hint plain[] = {{"HOLD A", "turn"}, {"B", "back to the middle"}, {"HOLD X", "mod"}, {"START", "play"}};
    CHECK(rig.plainBar(plain, 4) && ui::gHintsOver == 0.f);
    CHECK(rig.drawn(SynthScreen::kMainScopeX + 8.f, SynthScreen::kAdvancedY + 7.f, "SIGNAL"));
    const std::vector<Uint32> scopeBefore = rig.canvas.part(0, ui::kScopeY, ui::kScreenW, ui::kScopeH);
    const std::vector<Uint32> barBefore = rig.canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH);
    // X held, no MOD: the knob, NO MOD, the words, and UP's chip under them (at DEPTH's place).
    rig.key(X, true);
    rig.draw();
    CHECK(rig.canvas.part(0, ui::kScopeY, ui::kScreenW, ui::kScopeH) != scopeBefore); // the fields where SIGNAL's foot was
    CHECK(rig.drawn(p.lead, p.leadY, "TONE") && rig.drawn(p.lead, p.leadY + kModValueDrop, "NO MOD", true));
    CHECK(rig.drawn(p.depth, p.fieldsY + kModValueDrop + 1.f, ModKnob::kNoModWords));
    CHECK((rig.chips() == Chips{{static_cast<int>(p.depth), rig.chipW("UP")}}) && rig.chipIs(p.depth, "UP"));
    // An LFO: every field, a chip under each, the dice at the left.
    rig.tap(SDLK_UP);
    rig.draw();
    CHECK(rig.drawn(p.lead, p.leadY + kModValueDrop, "MOD 1", true, ui::kText));
    CHECK(rig.drawn(p.shape, p.fieldsY, "SHAPE") && rig.drawn(p.shape, p.fieldsY + kModValueDrop, "SINE", true, ui::kText));
    CHECK(rig.drawn(p.depth, p.fieldsY, "DEPTH") && rig.drawn(p.depth, p.fieldsY + kModValueDrop, "+25%", true, ui::kText));
    CHECK(rig.drawn(p.rate, p.fieldsY, "RATE") && rig.drawn(p.rate, p.fieldsY + kModValueDrop, "1 BAR", true, ui::kText));
    CHECK(rig.drawn(p.more, p.fieldsY, "PHASE") && rig.drawn(p.more, p.fieldsY + kModValueDrop, "0", true, ui::kText));
    CHECK(rig.drawn(static_cast<float>(p.nowX), static_cast<float>(p.nowY), "NOW"));
    CHECK(!rig.drawn(p.shape + 1.f, p.fieldsY, "SHAPE")); // (the check can tell)
    const Chips all = {{8, rig.chipW("Y")},
                       {static_cast<int>(p.lead), rig.chipW("HOLD B")},
                       {static_cast<int>(p.shape), rig.chipW("A")},
                       {static_cast<int>(p.depth), rig.chipW("UP / DOWN")},
                       {static_cast<int>(p.rate), rig.chipW("LEFT / RIGHT")},
                       {static_cast<int>(p.more), rig.chipW("L1 / R1")}};
    CHECK(rig.chips() == all);
    CHECK(rig.chipIs(p.lead, "HOLD B") && rig.chipIs(p.shape, "A") && rig.chipIs(p.depth, "UP / DOWN") && rig.chipIs(p.rate, "LEFT / RIGHT") && rig.chipIs(p.more, "L1 / R1"));
    // The widest values stand clear of the next field (RAMP DOWN, -100%, 1/2 STEP, LENGTH; MODS FULL).
    const Font& medium = rig.canvas.fonts->medium;
    CHECK(p.lead + medium.width("MODS FULL") + 6.f <= p.shape && p.shape + medium.width("RAMP DOWN") + 6.f <= p.depth);
    CHECK(p.depth + medium.width("-100%") + 6.f <= p.rate && p.rate + medium.width("1/2 STEP") + 6.f <= p.more);
    CHECK(p.more + rig.canvas.fonts->small.width("LENGTH") + 6.f <= p.nowX && p.nowX + p.nowW <= ui::kScopeX + ui::kScopeW - 4);
    CHECK(p.fieldsY + kModValueDrop + medium.capHeight() <= ui::kScopeY + ui::kScopeH - 1);
    // NOW moves while X is held and the song is stopped: purple in its bar.
    rig.frames(40);
    rig.draw();
    int purple = 0;
    for (int x = p.nowX; x < p.nowX + p.nowW; ++x) purple += rig.canvas.is(x, p.nowY + static_cast<int>(kModValueDrop) + 6, ui::kPurple);
    CHECK(purple > 3);
    // A STEPPER says LENGTH.
    ModSettings s = rig.mod(t, 0);
    s.kind = kModStepper, s.length = 7;
    rig.session().setMod(t, 0, s);
    rig.draw();
    CHECK(rig.drawn(p.shape, p.fieldsY + kModValueDrop, "STEPPER", true, ui::kText) && rig.drawn(p.more, p.fieldsY, "LENGTH") && rig.drawn(p.more, p.fieldsY + kModValueDrop, "7", true, ui::kText));
    // B held: DELETING, the strip filling.
    rig.key(B, true);
    rig.frames(20);
    rig.draw();
    CHECK(rig.drawn(p.lead, p.leadY + kModValueDrop, "DELETING", true, ui::kText));
    rig.key(B, false);
    // Let go: the strip and the bar are back, pixel for pixel.
    rig.key(X, false);
    rig.frames(120); // (the note's release dies away: the SIGNAL line is flat again)
    rig.draw();
    CHECK(rig.canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH) == barBefore);
    CHECK(rig.drawn(SynthScreen::kMainScopeX + 8.f, SynthScreen::kAdvancedY + 7.f, "SIGNAL"));
    // MODS FULL: no chips at all, the words in their place.
    rig.session().setMod(t, 0, lfoOn(kModFirstMacro + 1));
    rig.session().setMod(t, 1, lfoOn(kModFirstMacro + 2));
    rig.key(X, true);
    rig.draw();
    CHECK(rig.chips().empty() && rig.drawn(p.lead, p.leadY + kModValueDrop, "MODS FULL", true, ui::kText) && rig.drawn(p.depth, p.fieldsY + kModValueDrop + 1.f, ModKnob::kFullWords));
    rig.key(X, false);
    rig.device(rig.fm6, 0);
    rig.draw();
    // FM-6's bar says HOLD X: MOD on every macro (each is an amount), and it fits.
    const ui::Hint fm6[] = {{"HOLD A", "turn"}, {"B", "back to the middle"}, {"HOLD X", "mod"}, {"START", "play"}};
    CHECK(rig.plainBar(fm6, 4) && ui::gHintsOver == 0.f);
    // A choice (Phazed CHORUS) takes no MOD: not said there.
    rig.device(t, 6);
    rig.draw();
    const ui::Hint choice[] = {{"HOLD A", "turn"}, {"B", "back to the middle"}, {"START", "play"}};
    CHECK(rig.plainBar(choice, 3));
    // JAX-EP's: it fits there too.
    CHECK(rig.session().addTrack(TrackKind::Jx3p));
    rig.project().track = rig.project().trackCount() - 1;
    rig.frames(1);
    rig.app.jaxep().goTo(0, SynthScreen::kFirstMacro + 3);
    rig.draw();
    if (printing) std::printf("DEVICE MAIN, JAX-EP, on a macro: %.1f over\n", static_cast<double>(ui::gHintsOver));
    CHECK(rig.plainBar(plain, 4) && ui::gHintsOver == 0.f);
}

// ---- the FX screen ------------------------------------------------------------------------------

void testTheGestureOnFx() {
    Rig rig;
    const int t = 0; // the drums: TAPE in its first slot
    rig.session().setEffect(t, 0, EffectKind::Tape);
    FxScreen& fx = rig.fx(t, 0, 2); // FEEDBACK
    CHECK(fx.knobHere() == kModFirstEffect + 1);
    rig.key(X, true);
    rig.frames(1);
    CHECK(fx.modKnob().held() && fx.modKnob().target() == kModFirstEffect + 1);
    rig.tap(SDLK_UP);
    CHECK(rig.mod(t, 0).kind == kModLfo && rig.mod(t, 0).target == kModFirstEffect + 1);
    // L1 / R1 with X held: PHASE, not the track.
    const int track = rig.project().track;
    rig.tap(R1);
    CHECK(rig.mod(t, 0).phase == 1 && rig.project().track == track);
    // UP / DOWN: DEPTH, not the cursor; B: not the setting's default.
    rig.project().tracks[t].effects[0].param[1] = 0.66f;
    rig.tap(SDLK_UP);
    rig.tap(B);
    CHECK(fx.cursorRow() == 0 && fx.cursorColumn() == 2 && std::fabs(rig.mod(t, 0).depth - 0.26f) < 1e-6f);
    CHECK(rig.project().tracks[t].effects[0].param[1] == 0.66f);
    // A with X held: SHAPE, not A's own turn.
    rig.tap(A);
    CHECK(rig.mod(t, 0).shape == kModTriangle);
    rig.key(X, false);
    rig.frames(1);
    CHECK(!fx.modKnob().held());
    // L1 before X still steps the track (the order: X held is checked first, and only while held).
    rig.tap(R1);
    CHECK(rig.project().track == track + 1);
    rig.tap(L1);
    CHECK(rig.project().track == track);
    // TIME is a choice: X only hears there.
    fx.goTo(0, 1);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!fx.modKnob().held());
    rig.key(X, false);
    // The list, a bus and the master take none.
    for (int row : {0, FxScreen::kBusRow, FxScreen::kMasterRow}) {
        fx.goTo(row, row == 0 ? 0 : 1);
        CHECK(fx.knobHere() < 0);
        rig.key(X, true);
        rig.frames(1);
        CHECK(!fx.modKnob().held());
        rig.key(X, false);
    }
    // SENDS: ECHO's and SPACE's knobs.
    fx.goTo(FxScreen::kSendsRow, 2);
    CHECK(fx.knobHere() == kModFirstSend + 1);
    rig.key(X, true);
    rig.tap(SDLK_UP);
    CHECK(rig.mod(t, 1).kind == kModLfo && rig.mod(t, 1).target == kModFirstSend + 1);
    // Both taken: on ECHO, MODS FULL.
    rig.key(X, false);
    fx.goTo(FxScreen::kSendsRow, 1);
    rig.key(X, true);
    rig.frames(1);
    CHECK(fx.modKnob().full());
    rig.tap(SDLK_UP);
    CHECK(rig.mod(t, 0).target == kModFirstEffect + 1 && rig.mod(t, 1).target == kModFirstSend + 1);
    rig.key(X, false);
    // A synth track's note is held while X is (stopped): the MOD is heard.
    const int p = rig.phazed;
    rig.session().setEffect(p, 1, EffectKind::Filter);
    rig.fx(p, 1, 1); // CUTOFF
    rig.key(X, true);
    rig.frames(1);
    CHECK(rig.app.fx().modKnob().held() && rig.session().hearingHeld());
    rig.tap(SDLK_UP);
    rig.frames(1);
    CHECK(rig.session().modsHeard(p) == 1u);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.session().modsHeard(p) == 0);
    // The effect changed under the MOD: another family puts it OFF (Session::setEffect, batch 2's rule).
    rig.session().setEffect(p, 1, EffectKind::Tape);
    CHECK(rig.mod(p, 0).kind == kModOff);
}

void testDrawnOnFx() {
    Rig rig;
    CHECK(rig.canvas.ok && rig.scratch.ok);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    const int t = 0;
    rig.session().setEffect(t, 0, EffectKind::Tape);
    FxScreen& fx = rig.fx(t, 0, 2);
    const ModPlaces& p = kFxModPlaces;
    rig.draw();
    // (A setting's bar has no room for HOLD X: MOD: 22 pixels short with it. The help card says it.)
    const ui::Hint plain[] = {{"LEFT", "list"}, {"HOLD A", "turn, type"}, {"B", "default"}, {"L1 / R1", "track"}, {"START", "play"}};
    CHECK(rig.plainBar(plain, 5) && ui::gHintsOver == 0.f);
    const std::vector<Uint32> barBefore = rig.canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH);
    const std::vector<Uint32> headBefore = rig.canvas.part(FxScreen::kPaneX, FxScreen::kPaneY, FxScreen::kPaneW, FxScreen::kHeadH);
    CHECK(rig.session().setMod(t, 0, lfoOn(kModFirstEffect + 1)));
    rig.key(X, true);
    rig.draw();
    const float top = static_cast<float>(FxScreen::kPaneY);
    CHECK(fx.modPanelY() == FxScreen::kPaneY && fx.modPanelH() == FxScreen::kHeadH);
    CHECK(rig.drawn(p.lead, top + p.leadY, "FEEDBACK") && rig.drawn(p.lead, top + p.leadY + kModValueDrop, "MOD 1", true, ui::kText));
    CHECK(rig.drawn(p.shape, top + p.fieldsY, "SHAPE") && rig.drawn(p.depth, top + p.fieldsY, "DEPTH") && rig.drawn(p.rate, top + p.fieldsY, "RATE") && rig.drawn(p.more, top + p.fieldsY, "PHASE"));
    CHECK(rig.drawn(p.depth, top + p.fieldsY + kModValueDrop, "+50%", true, ui::kText) && rig.drawn(static_cast<float>(p.nowX), top + p.nowY, "NOW"));
    const Chips all = {{8, rig.chipW("Y")},
                       {static_cast<int>(p.lead), rig.chipW("HOLD B")},
                       {static_cast<int>(p.shape), rig.chipW("A")},
                       {static_cast<int>(p.depth), rig.chipW("UP / DOWN")},
                       {static_cast<int>(p.rate), rig.chipW("LEFT / RIGHT")},
                       {static_cast<int>(p.more), rig.chipW("L1 / R1")}};
    CHECK(rig.chips() == all);
    // The widest values stand clear of the next field, and the last ends inside the panel.
    const Font& medium = rig.canvas.fonts->medium;
    CHECK(p.lead + medium.width("MODS FULL") + 6.f <= p.nowX && p.lead + rig.canvas.fonts->small.width("FEEDBACK") + 6.f <= p.nowX);
    CHECK(p.shape + medium.width("RAMP DOWN") + 6.f <= p.depth && p.depth + medium.width("-100%") + 6.f <= p.rate);
    CHECK(p.rate + medium.width("1/2 STEP") + 6.f <= p.more && p.more + rig.canvas.fonts->small.width("LENGTH") <= FxScreen::kPaneX + FxScreen::kPaneW - 6);
    CHECK(p.nowX + p.nowW <= FxScreen::kPaneX + FxScreen::kPaneW - 6 && p.lead > FxScreen::kPaneX + 6 && p.lead >= 8 + rig.chipW("Y") + 5 + 13 + ui::kHintUnderGap);
    // The settings below are as they were: only the heading's place is taken.
    rig.key(X, false);
    rig.draw();
    CHECK(rig.canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH) == barBefore);
    CHECK(rig.canvas.part(FxScreen::kPaneX, FxScreen::kPaneY, FxScreen::kPaneW, FxScreen::kHeadH) == headBefore); // the heading back as it was
    CHECK(rig.drawn(FxScreen::kPaneX + 12.f, top + 10.f, "1 DRUMS, EFFECT 1"));
    // SENDS: the panel in the other send's row; the one held stays in full view.
    rig.session().setMod(t, 1, lfoOn(kModFirstSend));
    fx.goTo(FxScreen::kSendsRow, 1);
    rig.key(X, true);
    rig.draw();
    const float other = static_cast<float>(FxScreen::kSendY + FxScreen::kSendH + FxScreen::kSendGap);
    CHECK(fx.modPanelY() == static_cast<int>(other) && fx.modPanelH() == FxScreen::kSendH);
    CHECK(rig.drawn(p.lead, other + p.leadY, "ECHO") && rig.drawn(p.lead, other + p.leadY + kModValueDrop, "MOD 2", true, ui::kText));
    CHECK(rig.drawn(FxScreen::kPaneX + 12.f, FxScreen::kSendY + 12.f, "ECHO")); // ECHO's own row is there still
    rig.key(X, false);
    fx.goTo(FxScreen::kSendsRow, 2);
    rig.key(X, true);
    rig.draw();
    CHECK(fx.modPanelY() == FxScreen::kSendY && rig.drawn(p.lead, FxScreen::kSendY + p.leadY, "SPACE") && rig.drawn(p.lead, FxScreen::kSendY + p.leadY + kModValueDrop, "MODS FULL", true, ui::kText));
    CHECK(rig.chips().empty());
    rig.key(X, false);
    rig.draw();
    const ui::Hint sendsBar[] = {{"LEFT", "list"}, {"HOLD A", "turn"}, {"B", "none"}, {"HOLD X", "mod"}, {"L1 / R1", "track"}, {"START", "play"}};
    if (printing) std::printf("FX, a send: %.1f over\n", static_cast<double>(ui::gHintsOver));
    CHECK(rig.plainBar(sendsBar, 6) && ui::gHintsOver == 0.f);
    // A bus's setting: no HOLD X: MOD.
    fx.goTo(FxScreen::kBusRow, 1);
    rig.draw();
    const ui::Hint busBar[] = {{"LEFT", "list"}, {"HOLD A", "turn, type"}, {"B", "default"}, {"L1 / R1", "track"}, {"START", "play"}};
    CHECK(rig.plainBar(busBar, 5));
}


// ---- batch 6: the thin brass mark on a target's own bar ---------------------------------------

// The brass run along row `y` from x0 to x1: its left edge and width (the first run), or {-1, 0}.
std::pair<int, int> brassIn(const Rig& rig, int x0, int x1, int y) {
    for (int x = x0; x < x1; ++x)
        if (rig.canvas.is(x, y, ui::kBrass)) return {x, rig.canvas.run(x, y, ui::kBrass)};
    return {-1, 0};
}

// The mark on a bar at (x, y), w by h, at `at`: kModMarkW wide, brass from kModMarkOver above the bar
// to kModMarkOver below it, and nothing of it a row further out. `none`: no brass in the rows above.
bool markAt(const Rig& rig, int x, int y, int w, int h, float at) {
    const int mx = modMarkX(x, w, at);
    const std::pair<int, int> top = brassIn(rig, x, x + w, y - kModMarkOver);
    if (printing) std::printf("mark: want %d, found %d (%d wide)\n", mx, top.first, top.second);
    if (top.first != mx || top.second != kModMarkW) return false;
    for (int py = y - kModMarkOver; py < y + h + kModMarkOver; ++py)
        for (int px = mx; px < mx + kModMarkW; ++px)
            if (!rig.canvas.is(px, py, ui::kBrass)) return false;
    return !rig.canvas.is(mx, y - kModMarkOver - 1, ui::kBrass) && !rig.canvas.is(mx, y + h + kModMarkOver, ui::kBrass);
}
bool noMark(const Rig& rig, int x, int y, int w) { return brassIn(rig, x, x + w, y - kModMarkOver).first < 0; }
// On the FX screen a setting is a dial (design B, 2026-11-24): the mark is a brass tick across its ring, standing out past it.
bool dialMarkAt(const Rig& rig, int p, float at) {
    const int cx = FxScreen::dialCX(p), cy = FxScreen::kDialCY;
    for (float k = FxScreen::kDialR - FxScreen::kDialRing - 2.f; k <= FxScreen::kDialR + 2.f; k += 1.f) {
        const SDL_Point pt = dialPoint(cx, cy, k, at);
        if (!rig.canvas.is(pt.x, pt.y, ui::kBrass)) return false;
    }
    // ...and not a hair round from there.
    const SDL_Point off = dialPoint(cx, cy, FxScreen::kDialR - FxScreen::kDialRing / 2.f, at + 0.06f > 1.f ? at - 0.06f : at + 0.06f);
    return !rig.canvas.is(off.x, off.y, ui::kBrass);
}
bool noDialMark(const Rig& rig, int p) {
    const int cx = FxScreen::dialCX(p), cy = FxScreen::kDialCY, r = FxScreen::kDialR + 4;
    for (int py = cy - r; py <= cy + r; ++py)
        for (int px = cx - r; px <= cx + r; ++px)
            if (rig.canvas.is(px, py, ui::kBrass)) return false;
    return true;
}

void testTheBrassMark() {
    Rig rig;
    CHECK(rig.canvas.ok);
    if (!rig.canvas.ok) return;
    const int t = rig.phazed;
    // MOTION (macro 1) under a SQUARE at +30%: what it adds is exactly +30% for the first half bar.
    ModSettings square = lfoOn(kModFirstMacro + 1, 0.3f);
    square.shape = kModSquare;
    CHECK(rig.session().setMod(t, 0, square));
    rig.device(t, 0);
    // A macro is a dial (design A, 2026-11-24): the mark is a brass tick across its ring.
    const auto dialOf = [](int m) {
        return ui::macroDialIn(8 + (m % 4) * SynthScreen::kMacroPitchX, SynthScreen::kMacroY + (m / 4) * (SynthScreen::kMacroH + SynthScreen::kMacroGap),
                               SynthScreen::kMacroW, SynthScreen::kMacroH);
    };
    const auto markOn = [&](int m, float at) {
        const ui::MacroDial dial = dialOf(m);
        for (float k = dial.radius - dial.ring - 2.f; k <= dial.radius + 2.f; k += 1.f) {
            const SDL_Point pt = dialPoint(dial.cx, dial.cy, k, at);
            if (!rig.canvas.is(pt.x, pt.y, ui::kBrass)) return false;
        }
        return true;
    };
    const auto noMarkOn = [&](int m) {
        const ui::MacroDial dial = dialOf(m);
        for (int py = dial.cy - dial.radius - 4; py <= dial.cy + dial.radius + 4; ++py)
            for (int px = dial.cx - dial.radius - 4; px <= dial.cx + dial.radius + 4; ++px)
                if (rig.canvas.is(px, py, ui::kBrass)) return false;
        return true;
    };
    rig.draw();
    CHECK(noMarkOn(1)); // stopped: the MOD does not run, nothing moves
    CHECK(std::isnan(modMarkAt(rig.session(), t, kModFirstMacro + 1, 0.5f)));
    rig.tap(SDLK_RETURN); // START
    CHECK(rig.project().playing);
    rig.draw();
    CHECK(rig.session().engine.modShown(t, 0) == 0.3f);
    {   // A MOD set while playing has no position until the engine's next tick: no mark until then.
        CHECK(rig.session().setMod(t, 1, lfoOn(kModFirstMacro + 2)));
        CHECK(std::isnan(modMarkAt(rig.session(), t, kModFirstMacro + 2, 0.5f)));
        rig.frames(3); // (a macro's MOD runs on every 8th tick: 512 samples)
        CHECK(!std::isnan(modMarkAt(rig.session(), t, kModFirstMacro + 2, 0.5f)));
        rig.session().setMod(t, 1, ModSettings());
    }
    CHECK(modMarkAt(rig.session(), t, kModFirstMacro + 1, 0.5f) == 0.8f);
    CHECK(markOn(1, 0.8f) && !markOn(1, 0.6f));
    for (int m = 0; m < 8; ++m) { // the other seven: no mark (TONE has the cursor, the rest nothing aimed)
        if (m == 1) continue;
        CHECK(noMarkOn(m));
    }
    // The second MOD on the same knob adds (one clamp): +30% and +40% from the middle is the top.
    ModSettings more = square;
    more.depth = 0.4f;
    CHECK(rig.session().setMod(t, 1, more));
    rig.frames(40); // (to the next macro tick)
    rig.draw();
    CHECK(modMarkAt(rig.session(), t, kModFirstMacro + 1, 0.5f) == 1.f);
    CHECK(markOn(1, 1.f)); // held to the ring's end
    rig.session().setMod(t, 1, ModSettings());
    // Stopped: gone on the very pass START is pressed.
    rig.key(SDLK_RETURN, true);
    CHECK(std::isnan(modMarkAt(rig.session(), t, kModFirstMacro + 1, 0.5f))); // (before the engine has heard the Stop)
    rig.draw();
    CHECK(!rig.project().playing && noMarkOn(1));
    rig.key(SDLK_RETURN, false);
    // X held on MOTION while stopped: the MOD runs, and the mark is there; let go, gone.
    rig.device(t, 1);
    rig.key(X, true);
    rig.frames(3);
    rig.draw();
    CHECK(markOn(1, 0.8f));
    rig.key(X, false);
    rig.draw();
    CHECK(noMarkOn(1));

    // FX: a TAPE's FEEDBACK (0.4) under a SQUARE at -25%, its MIX under none, and the ECHO send under +50%.
    const int d = 0;
    rig.session().setEffect(d, 0, EffectKind::Tape);
    ModSettings fb = lfoOn(kModFirstEffect + 1, -0.25f);
    fb.shape = kModSquare;
    CHECK(rig.session().setMod(d, 0, fb));
    ModSettings echo = lfoOn(kModFirstSend, 0.5f);
    echo.shape = kModSquare;
    CHECK(rig.session().setMod(d, 1, echo));
    rig.session().setSend(d, 0, 0.3f); // (the mark stands about where the send is, not about nothing)
    rig.fx(d, 0, 2);
    rig.draw();
    for (int p = 0; p < 4; ++p) CHECK(noDialMark(rig, p)); // stopped
    rig.tap(SDLK_RETURN);
    rig.draw();
    const float fbAt = rig.session().project.tracks[d].effects[0].param[1] - 0.25f;
    CHECK(dialMarkAt(rig, 1, fbAt));
    CHECK(noDialMark(rig, 0) && noDialMark(rig, 2) && noDialMark(rig, 3));
    rig.app.fx().goTo(FxScreen::kSendsRow, 1);
    rig.draw();
    const float echoAt = rig.session().project.tracks[d].send[0] + 0.5f;
    CHECK(markAt(rig, FxScreen::kPaneX + 12, FxScreen::kSendY + FxScreen::kSendBarY, FxScreen::kPaneW - 24, 14, echoAt > 1.f ? 1.f : echoAt));
    const int spaceY = FxScreen::kSendY + FxScreen::kSendH + FxScreen::kSendGap + FxScreen::kSendBarY;
    CHECK(noMark(rig, FxScreen::kPaneX + 12, spaceY, FxScreen::kPaneW - 24)); // SPACE: nothing aimed
    // In PAPER too: the mark is the theme's brass.
    ui::useTheme(kThemeSolarized);
    rig.session().setTheme(kThemeSolarized);
    rig.draw();
    CHECK(markAt(rig, FxScreen::kPaneX + 12, FxScreen::kSendY + FxScreen::kSendBarY, FxScreen::kPaneW - 24, 14, echoAt > 1.f ? 1.f : echoAt));
    rig.session().setTheme(kThemeBrown);
    ui::useTheme(kThemeBrown);
}

// Batch 7: while the song plays, the knob's DEPTH field reads where the MOD's DEPTH lane has it.
void testTheKnobReadsTheDepthLane() {
    Rig rig;
    CHECK(rig.canvas.ok);
    if (!rig.canvas.ok) return;
    const int t = rig.phazed;
    CHECK(rig.session().setMod(t, 0, lfoOn(kModFirstMacro, 0.3f)));
    for (int i = 0; i < kSteps; ++i) rig.session().setAutomation(t, rig.project().scene, modDepthLane(0), i, modDepthToLane(-0.5f));
    rig.device(t, 0);
    const ModPlaces& p = kDeviceModPlaces;
    rig.key(X, true);
    rig.draw();
    CHECK(rig.drawn(p.depth, p.fieldsY + kModValueDrop, "+30%", true, ui::kText)); // stopped: its own
    rig.key(X, false);
    rig.tap(SDLK_RETURN);
    rig.frames(20);
    rig.key(X, true);
    rig.draw();
    CHECK(rig.project().playing && rig.drawn(p.depth, p.fieldsY + kModValueDrop, "-50%", true, ui::kText));
    rig.key(X, false);
    // A MOD at 0% of its own, its lane written: it moves, and its knob has the brass mark.
    ModSettings none = lfoOn(kModFirstMacro + 1, 0.f);
    none.shape = kModSquare;
    CHECK(rig.session().setMod(t, 1, none));
    for (int i = 0; i < kSteps; ++i) rig.session().setAutomation(t, rig.project().scene, modDepthLane(1), i, modDepthToLane(0.4f));
    rig.frames(30);
    CHECK(std::fabs(modMarkAt(rig.session(), t, kModFirstMacro + 1, 0.5f) - 0.9f) < 1e-6f);
    rig.tap(SDLK_RETURN);
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (int i = 1; i < argc; ++i) printing = printing || std::strcmp(argv[i], "--print") == 0;
    testTheWords();
    testFreeOnTheKnob();
    testTheGestureOnAMacro();
    testFullAndChoices();
    testTheOrderOnDevice();
    testHeardWhileStopped();
    testOnlyWhatChangedIsSent();
    testHeldAcrossThings();
    testTrackChangedWhileHeld();
    testHeardOnlyForTheTrackShown();
    testUndoTakesTheKnobAway();
    testLetGoLostToAFullQueue();
    testTheSameAtEverySize();
    testDrawnOnDevice();
    testTheGestureOnFx();
    testDrawnOnFx();
    testTheBrassMark();
    testTheKnobReadsTheDepthLane();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("a knob's MOD, X held: tests passed\n");
    return 0;
}
