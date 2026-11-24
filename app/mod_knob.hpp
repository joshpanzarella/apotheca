#pragma once
// X HELD ON A KNOB: its MOD (docs/plans/modulation-plan.md, section 7; batches 3 and 4). The same
// gesture on DEVICE MAIN's macros and on the FX screen's slot settings and SENDS, so it lives here
// once and each screen hands it the knob, a place to draw and its own button order.
//
//   While X is held on a knob that can take a MOD (mods.hpp: an amount, not a choice):
//     UP / DOWN       DEPTH, by 1%. The first UP on a knob with no MOD makes one: LFO, SINE,
//                     1 BAR, +25% (owner's call C), in the first MOD that is free. Both taken:
//                     MODS FULL (call D), and nothing is made.
//     the stick       up / down turns DEPTH smoothly (a big move); left / right clicks RATE
//     LEFT / RIGHT    RATE, a click a push (the ends stop: an LFO's last is FREE, a STEPPER's 8 BARS)
//     A               SHAPE: SINE ... DRIFT, then STEPPER, then SINE again
//     L1 / R1         an LFO's PHASE (a FREE one's SPEED, a click of mod_rule.hpp's list), a
//                     STEPPER's LENGTH (the ends stop)
//     Y               the dice (call F): a STEPPER's row; an LFO's shape, rate and depth
//     hold B          deletes the MOD (a bar fills for a second; undo brings it back)
//   X held while the song is stopped runs the MOD (call E: Session::hearMods), and the screen's
//   own note is held as long as X is, so the wobble is heard. Everything X held changes is a step
//   of undo through Session::setMod (soundMark sees it).
//
// The fields are drawn in a row laid out to the pixel (a TIGHT row, fonts.fixed(): the same at
// every TEXT SIZE), and each button's chip in the hints bar stands under the field it sets: the
// field and its chip take their x from ONE constant (ModPlaces), never written twice.

#include <SDL.h>

#include "apotheca/mod_rule.hpp"
#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// Where a screen draws the fields: each one's x (its name above, its value under), the two rows,
// and the NOW bar (what the MOD adds this moment, from the middle). DEVICE puts them all in one
// row in LIVE OUTPUT's place; FX in two rows in the heading's place (the knob and NOW above).
struct ModPlaces {
    float lead, shape, depth, rate, more; // the knob and its MOD; A; UP / DOWN; LEFT / RIGHT; L1 / R1
    float leadY, fieldsY;                 // the top of the knob's name; of the fields' names (values 16 under)
    int nowX, nowY, nowW;                 // NOW: its name at (nowX, nowY), its bar 16 under, nowW wide
};
constexpr float kModValueDrop = 16.f; // a value's top under its name's

// DEVICE MAIN: in the LIVE OUTPUT strip's place (ui::kScopeY, 36 tall), one row. The dice stand at
// the bar's left end, so the knob's field starts past them.
// (NOW from 528: a FREE LFO's slowest SPEED, "0.025 HZ", stands clear of it.)
constexpr ModPlaces kDeviceModPlaces = {56.f, 166.f, 284.f, 352.f, 444.f,
                                        static_cast<float>(ui::kScopeY + 4), static_cast<float>(ui::kScopeY + 4),
                                        528, ui::kScopeY + 4, 96};
// FX: in the heading's place (FxScreen::kPaneX 222, kPaneY 34, kHeadH 93): the knob and NOW on the
// first row, the four fields on the second. Given as offsets from the panel's top (`top` is added).
// (RATE from 465 and the last field from 550: a FREE LFO's "0.025 HZ" ends inside the panel, and
// "1/2 STEP" before it.)
constexpr ModPlaces kFxModPlaces = {234.f, 289.f, 402.f, 465.f, 550.f, 10.f, 50.f, 470, 10, 150};

class ModKnob {
public:
    // `owner`: the screen, so the App can tell whose audition it is (Session::keepModsHeard).
    ModKnob(Session& session, const void* owner) : session_(session), owner_(owner) {}

    // X went down on a knob: `target` is its automation lane (mod_rule.hpp). False, and nothing held,
    // where the knob cannot take a MOD (a choice, a bus's or the master's setting): X only hears there.
    bool hold(int track, int target, const Input& input);
    void letGo();
    bool held() const { return track_ >= 0; }
    int track() const { return track_; }
    int target() const { return target_; }
    int mod() const;   // the MOD aimed at this knob (0, 1), or -1
    bool full() const; // none aimed here, and both MODs are taken: MODS FULL
    // A button while held. True: it was the MOD's (the screen does nothing with it). START is never
    // taken; X is the screen's (its let go ends the hold).
    bool onButton(const ButtonEvent& event, Uint32 nowMs);
    // Every frame while held: the stick, a held D-pad, B's bar, and the audition while stopped.
    void update(const Input& input, Uint32 nowMs, float dt);
    // B held: how far its bar has filled, 0..1. Full, the MOD is deleted.
    static constexpr Uint32 kDeleteHoldMs = 1000;
    float deletePart() const { return deletePart_; }

    // The fields, at `places` (FX: moved down by `top`), in a box x..x+w, y..y+h the screen has drawn.
    // `knob` is the knob's name as its own box says it ("TONE", "CUTOFF", "ECHO").
    void drawFields(SDL_Renderer* r, const Fonts& fonts, const ModPlaces& places, float top, int x, int y, int w, int h, const char* knob) const;
    // The hints bar: each button's chip under its field, the dice at the left. Only the buttons that
    // do something: on a knob with no MOD, UP alone; MODS FULL, none.
    void drawHints(SDL_Renderer* r, const Fonts& fonts, const ModPlaces& places) const;

    // What the fields say, for the tests and the drawing: "SINE" ... "STEPPER", "+25%", "1 BAR",
    // "1/4" or "16" (MORE's value) and its name, "PHASE" or "LENGTH".
    static void shapeText(const ModSettings& m, char* out, int size);
    static void depthText(float depth, char* out, int size);
    static void moreText(const ModSettings& m, char* out, int size);
    static const char* moreName(const ModSettings& m) { return modKind(m.kind) == kModStepper ? "LENGTH" : (modFree(m) ? "SPEED" : "PHASE"); }
    static void speedText(float hz, char* out, int size); // "1 HZ", "0.025 HZ", "15 HZ"
    // How many places RATE has for this MOD: an LFO's run to FREE, a STEPPER's stop at 8 BARS.
    static int rates(const ModSettings& m) { return modKind(m.kind) == kModStepper ? kModTempoRates : kModRates; }
    // The SPEEDs the dice roll a FREE LFO between: the clicks 0.1 Hz to 5 Hz.
    static constexpr int kSpeedRollFirst = 7, kSpeedRollLast = 24;
    // The first MOD UP makes (owner's call C).
    static ModSettings fresh(int target);
    // SHAPE as one list, as A steps it here and UP / DOWN on the MOD tab: SINE ... DRIFT (an LFO's
    // shapes, places 0 to 6), then STEPPER (7). `shapeAt`: the MOD at that place, everything else
    // kept; a STEPPER whose row was never written comes in rolled, so it moves (a row of nothing
    // moves nothing), from a seed of the track and the target.
    static constexpr int kShapePlaces = kModShapes + 1;
    static int shapePlace(const ModSettings& m) { return modKind(m.kind) == kModStepper ? kModShapes : modShape(m.shape); }
    static ModSettings shapeAt(const ModSettings& m, int place, int track);
    // The dice (owner's call F), from a seed: a STEPPER's row rolled, LENGTH, SLEW, CHANCE and
    // DEPTH kept; an LFO's SHAPE, RATE and DEPTH, never all three as they were (a FREE one's SHAPE,
    // SPEED and DEPTH: it stays free).
    static ModSettings rolled(const ModSettings& m, std::uint32_t seed);
    // The words where there is no MOD to show.
    static constexpr const char* kNoModWords = "UP: A NEW LFO HERE";
    static constexpr const char* kFullWords = "BOTH MODS ARE IN USE";

private:
    void set(const ModSettings& m) { session_.setMod(track_, mod(), m); }
    void depthBy(float by);   // the first UP makes one
    void rateBy(int by);
    void moreBy(int by);
    void nextShape();
    void dice(Uint32 nowMs);
    void press(Button b, Uint32 nowMs);

    Session& session_;
    const void* owner_;
    int track_ = -1, target_ = -1;
    bool deleting_ = false, deleteTimed_ = false;
    Uint32 deleteSince_ = 0;
    float deletePart_ = 0.f;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    bool stickWait_ = false; // the stick was already pushed when X went down: it must come back first
    int stickAxis_ = 0;      // 0 none, 1 up / down (DEPTH), 2 left / right (RATE)
    float stickSaved_ = 0.f;
    bool stickWas_ = false;
    unsigned rolls_ = 0;
};

// ---- the brass mark (batch 6): a thin mark on a target's own bar where the value is NOW ----------
// While a MOD aimed at a knob runs (the song playing, or X held on it while stopped), the knob's own
// bar on DEVICE or FX shows the value the setting is given this moment: the bar's centre (`shown`,
// what the bar itself is drawn at) plus what the MODs aimed there add (Engine::modShown, the two
// added), one clamp, as the engine gives it (mod_rule.hpp's modGiven). Brass, as a meter is: it
// moves by itself. Not a number where no MOD aimed at that lane runs: nothing is drawn.
float modMarkAt(const Session& session, int track, int lane, float shown);
// The mark: kModMarkW wide, standing kModMarkOver above and below a bar at (x, y), w by h.
constexpr int kModMarkW = 2, kModMarkOver = 3;
int modMarkX(int x, int w, float at); // the mark's left edge
void drawModMark(SDL_Renderer* r, int x, int y, int w, int h, float at);

} // namespace apotheca
