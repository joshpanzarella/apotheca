// MOD (docs/plans/modulation-plan.md, batches 0 and 1): the rule (mod_rule.hpp) against values
// worked out by hand; the one way a lane's value goes out of the engine (giveLane); and the engine
// running MODs: the tick, the macros' tick, Play twice, a tempo change, any block length and any
// helpers to the bit, the chunk cuts only while one runs, a scene move, a bounce against a live
// loop, the FX screen's hold, choices, a track taken away, X held while stopped, and what the old
// FILTER's stairs and a newer effect's glide do to a MOD (measured).
//   mod_test                            every check
//   mod_test [--measure] --only NAME... just those tests (--measure: print what is measured)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/effect.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/mod_rule.hpp"

#include "../engine/src/fx/glide.hpp" // a newer effect's Glide, by itself

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool near(double a, double b, double within = 1e-9) { return std::fabs(a - b) <= within; }
static const double kPi = 3.14159265358979323846;

// ---- the rule ---------------------------------------------------------------------------------

// The numbers are in songs: these may never move.
static void testTheNumbers() {
    CHECK(kModsPerTrack == 2 && kModSteps == 16);
    CHECK(kModOff == 0 && kModLfo == 1 && kModStepper == 2 && kModKinds == 3);
    CHECK(modKind(-1) == kModOff && modKind(3) == kModOff && modKind(2) == kModStepper);
    CHECK(std::string(modKindName(kModLfo)) == "LFO" && std::string(modKindName(kModStepper)) == "STEPPER" && std::string(modKindName(9)) == "OFF");
    CHECK(kModSine == 0 && kModTriangle == 1 && kModRampUp == 2 && kModRampDown == 3 && kModSquare == 4 && kModRandom == 5 && kModDrift == 6 && kModShapes == 7);
    CHECK(modShape(7) == kModSine && modShape(-2) == kModSine && modShape(6) == kModDrift);
    CHECK(modPhase(4) == 0 && modPhase(3) == 3 && modRestart(1) == kModRestartPlay && modLength(0) == 1 && modLength(17) == 16 && modLength(5) == 5);
    CHECK(kRollMod == 6); // appended to ChordRoll after GLIDE FROM's 5
    CHECK(kModTick == 64 && kModMacroTicks == 8);
    // Targets: the automation lanes, but 22 and 23 (the MODs' DEPTH: batch 7); LEVEL and PAN (batch 8).
    CHECK(kModFirstEffect == 8 && kModFirstSend == kAutoSendLane && kModTargetLanes == 22 && kAutoLanes == 24);
    for (int t = -1; t < 40; ++t) CHECK(modTargetKnown(t) == ((t >= 0 && t <= 21) || t == 32 || t == 33));
    for (int t = -1; t < 40; ++t) CHECK(modTargetIsMix(t) == (t == 32 || t == 33));
    CHECK(modTargetIsMacro(0) && modTargetIsMacro(7) && !modTargetIsMacro(8));
    CHECK(modTargetIsEffect(8) && modTargetIsEffect(19) && !modTargetIsEffect(20) && modTargetIsSend(20) && modTargetIsSend(21) && !modTargetIsSend(22));
    for (int slot = 0; slot < 3; ++slot)
        for (int setting = 0; setting < 4; ++setting) {
            const int t = autoLaneOfEffect(slot, setting);
            CHECK(modTargetSlot(t) == slot && modTargetSetting(t) == setting);
        }
    CHECK(modTargetSlot(5) == -1 && modTargetSetting(20) == -1 && kModTargetLevel == 32 && kModTargetPan == 33);
    // Rates, in steps (a sixteenth), by number.
    // FREE (appended, 2026-11-15) is last: an LFO's SPEED; a STEPPER that reads it plays 1 BAR's 16 steps.
    const double steps[kModRates] = {0.5, 1.0, 4.0 / 3.0, 2.0, 8.0 / 3.0, 4.0, 6.0, 8.0, 16.0, 32.0, 64.0, 128.0, 16.0};
    const char* names[kModRates] = {"1/2 STEP", "1 STEP", "1/8T", "1/8", "1/4T", "1/4", "1/4.", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "FREE"};
    CHECK(kModRates == 13 && kModRateFree == 12 && kModTempoRates == 12);
    for (int r = 0; r < kModRates; ++r) {
        CHECK(modRateSteps(r) == steps[r]);
        CHECK(std::string(modRateInfo(r).name) == names[r]);
        CHECK(modRateAt(modRatePlace(r)) == r);
    }
    CHECK(modRate(13) == kModRateBar && modRate(12) == kModRateFree && modRate(-1) == kModRateBar && kModRateUsual == kModRateBar);
    CHECK(modRateAt(-3) == 0 && modRateAt(99) == kModRates - 1);
}

// The song's position from the engine's step clock, read before the chunk is taken off: samples
// since Play over samples a step, on a step that is not a whole number of samples. The clock is
// the engine's own (render: a step fires on the first sample at or after its time; a chunk ends
// on the next step and is at most 64).
static void testThePosition() {
    for (double bpm : {120.0, 133.0, 97.3, 40.0}) {
        const double perStep = 48000.0 * 60.0 / (bpm * 4.0);
        long fired = 0, sample = 0;
        double until = 0.0;
        double worst = 0.0;
        while (sample < 48000L * 20) {
            if (until <= 0.0) {
                ++fired;
                until += perStep;
            }
            const double pos = modPosition(fired, perStep, until);
            worst = std::fmax(worst, std::fabs(pos - sample / perStep));
            CHECK(static_cast<long>(std::floor(pos + 1e-9)) == fired - 1); // in the step that is playing
            int chunk = 64;
            if (until < chunk) chunk = std::max(1, static_cast<int>(std::ceil(until)));
            until -= chunk;
            sample += chunk;
        }
        CHECK(worst < 1e-8);
    }
    CHECK(modPosition(1, 6000.0, 6000.0) == 0.0 && modPosition(3, 6000.0, 1500.0) == 2.75);
}

// Each LFO shape, at places worked out by hand.
static void testTheShapes() {
    ModSettings m;
    m.kind = kModLfo;
    m.rate = kModRateBar; // 16 steps a cycle
    const struct { int shape; double at, value; } cases[] = {
        {kModSine, 0.0, 0.0}, {kModSine, 4.0, 1.0}, {kModSine, 8.0, 0.0}, {kModSine, 12.0, -1.0}, {kModSine, 2.0, std::sqrt(0.5)},
        {kModTriangle, 0.0, 0.0}, {kModTriangle, 2.0, 0.5}, {kModTriangle, 4.0, 1.0}, {kModTriangle, 8.0, 0.0}, {kModTriangle, 12.0, -1.0}, {kModTriangle, 14.0, -0.5},
        {kModRampUp, 0.0, -1.0}, {kModRampUp, 8.0, 0.0}, {kModRampUp, 12.0, 0.5},
        {kModRampDown, 0.0, 1.0}, {kModRampDown, 4.0, 0.5}, {kModRampDown, 8.0, 0.0},
        {kModSquare, 0.0, 1.0}, {kModSquare, 7.9, 1.0}, {kModSquare, 8.0, -1.0}, {kModSquare, 15.9, -1.0},
    };
    for (const auto& c : cases) {
        m.shape = c.shape;
        CHECK(near(lfoValue(m, c.at, 1, 0), c.value, 1e-12));
        CHECK(near(lfoValue(m, c.at + 16.0, 1, 0), c.value, 1e-9)); // the next cycle, the same
        CHECK(near(lfoValue(m, c.at - 32.0, 1, 0), c.value, 1e-9)); // and before Play's first step
    }
    // A rate's cycle: a sine at 1/8T is back at 0 after 4/3 steps, at its top a quarter in.
    m.shape = kModSine;
    m.rate = kModRateEighthTriplet;
    CHECK(near(lfoValue(m, 1.0 / 3.0, 1, 0), 1.0, 1e-12) && near(lfoValue(m, 4.0, 1, 0), 0.0, 1e-9));
    m.rate = kModRateHalfStep;
    CHECK(near(lfoValue(m, 0.125, 1, 0), 1.0, 1e-12));
    // PHASE moves the cycle on by quarters.
    m.rate = kModRateBar;
    for (int phase = 0; phase < 4; ++phase) {
        m.phase = phase;
        CHECK(near(lfoValue(m, 3.0, 1, 0), std::sin(2.0 * kPi * (3.0 / 16.0 + 0.25 * phase)), 1e-12));
    }
    m.phase = 0;
    // Every shape stays in -1..1.
    for (int shape = 0; shape < kModShapes; ++shape) {
        m.shape = shape;
        for (double at = -20.0; at < 100.0; at += 0.37) {
            const double v = lfoValue(m, at, 3, 1);
            CHECK(v >= -1.0 && v <= 1.0);
        }
    }
}

// RANDOM holds one value a cycle, rolled from the track, the MOD and which cycle: never walked.
// DRIFT glides from each cycle's value to the next's, smoothly, through both.
static void testRandomAndDrift() {
    ModSettings m;
    m.kind = kModLfo;
    m.rate = kModRateQuarter; // 4 steps
    m.shape = kModRandom;
    for (long cycle = -2; cycle < 40; ++cycle) {
        const double held = chordSigned(chordSeed(2, 1, 0, cycle), kRollMod, 1 * 32 + kModRollValue); // by hand
        CHECK(lfoValue(m, cycle * 4.0, 2, 1) == held && lfoValue(m, cycle * 4.0 + 3.99, 2, 1) == held);
    }
    int differs = 0, otherMod = 0, otherTrack = 0;
    for (long cycle = 0; cycle < 64; ++cycle) {
        differs += lfoValue(m, cycle * 4.0, 2, 1) != lfoValue(m, cycle * 4.0 + 4.0, 2, 1);
        otherMod += lfoValue(m, cycle * 4.0, 2, 1) != lfoValue(m, cycle * 4.0, 2, 0);
        otherTrack += lfoValue(m, cycle * 4.0, 2, 1) != lfoValue(m, cycle * 4.0, 3, 1);
    }
    CHECK(differs > 60 && otherMod > 60 && otherTrack > 60);
    // Spread over -1..1, not a corner of it.
    double low = 1.0, high = -1.0, sum = 0.0;
    for (long cycle = 0; cycle < 4000; ++cycle) {
        const double v = lfoValue(m, cycle * 4.0, 5, 0);
        low = std::fmin(low, v);
        high = std::fmax(high, v);
        sum += v;
    }
    CHECK(low < -0.99 && high > 0.99 && std::fabs(sum / 4000.0) < 0.05);
    // DRIFT: at a cycle's start it is RANDOM's value; halfway, halfway to the next; never a jump.
    ModSettings d = m;
    d.shape = kModDrift;
    for (long cycle = 0; cycle < 20; ++cycle) {
        const double here = lfoValue(m, cycle * 4.0, 2, 1), next = lfoValue(m, cycle * 4.0 + 4.0, 2, 1);
        CHECK(near(lfoValue(d, cycle * 4.0, 2, 1), here, 1e-12));
        CHECK(near(lfoValue(d, cycle * 4.0 + 2.0, 2, 1), 0.5 * (here + next), 1e-12));
        CHECK(near(lfoValue(d, cycle * 4.0 + 1.0, 2, 1), here + (next - here) * 0.5 * (1.0 - std::cos(kPi / 4.0)), 1e-12)); // a cosine: a quarter in, 15% of the way
        CHECK(std::fabs(lfoValue(d, cycle * 4.0 + 0.04, 2, 1) - here) < 1e-3); // it leaves each value gently: no corner
    }
    double biggest = 0.0;
    for (double at = 0.0; at < 80.0; at += 0.01) biggest = std::fmax(biggest, std::fabs(lfoValue(d, at + 0.01, 2, 1) - lfoValue(d, at, 2, 1)));
    CHECK(biggest < 2.0 * kPi / 2.0 * 0.01 / 4.0 * 1.01 + 1e-9); // at most the cosine's steepest over a 0.01-step move
}

// FREE (owner, 2026-11-15): an LFO at RATE FREE runs at its SPEED in Hz, its position seconds since
// Play, with no PHASE. A STEPPER has no FREE (one that reads it plays 1 BAR). SPEED's clicks, held
// ends, and its log scale.
static void testTheFreeRule() {
    ModSettings m;
    m.kind = kModLfo;
    m.rate = kModRateFree;
    m.speed = 0.5f;
    CHECK(modFree(m) && modPositionFor(m, 7.0, 3.25) == 3.25);
    for (int k : {kModOff, kModStepper}) {
        ModSettings other = m;
        other.kind = k;
        CHECK(!modFree(other) && modPositionFor(other, 7.0, 3.25) == 7.0);
    }
    ModSettings synced = m;
    synced.rate = kModRateBar;
    CHECK(!modFree(synced) && modPositionFor(synced, 7.0, 3.25) == 7.0);
    // Cycles are seconds x SPEED: half a cycle a second. PHASE does nothing to a free LFO.
    for (int phase = 0; phase < kModPhases; ++phase) {
        m.phase = phase;
        CHECK(near(lfoCycles(m, 3.0), 1.5, 1e-12) && near(lfoPositionAt(m, 1.5), 3.0, 1e-12));
        m.shape = kModSine;
        CHECK(near(lfoValue(m, 0.5, 1, 0), 1.0, 1e-12) && near(lfoValue(m, 1.5, 1, 0), -1.0, 1e-12) && near(lfoValue(m, 2.0, 1, 0), 0.0, 1e-12));
    }
    m.phase = 0;
    m.speed = 7.f;
    CHECK(near(lfoCycles(m, 0.25), 1.75, 1e-12) && near(lfoPositionAt(m, 1.75), 0.25, 1e-12));
    // At 120 BPM (8 steps a second) a free LFO at 0.5 Hz is a synced one at 1 BAR, in every shape,
    // RANDOM and DRIFT included: the same cycle's count, the same roll.
    m.speed = 0.5f;
    for (int shape = 0; shape < kModShapes; ++shape) {
        m.shape = synced.shape = shape;
        for (double t = -3.0; t < 30.0; t += 0.173) CHECK(near(lfoValue(m, t, 4, 1), lfoValue(synced, t * 8.0, 4, 1), 1e-9));
    }
    // A STEPPER at FREE (a song written by hand) plays as one at 1 BAR, and its place too.
    ModSettings s, bar;
    s.kind = bar.kind = kModStepper;
    s.rate = kModRateFree;
    bar.rate = kModRateBar;
    for (int i = 0; i < kModSteps; ++i) s.steps[i] = bar.steps[i] = static_cast<float>(i % 5) / 4.f - 0.5f;
    for (double at = 0.0; at < 200.0; at += 3.7) CHECK(stepValue(s, at, 1, 0) == stepValue(bar, at, 1, 0) && stepperPlace(s, at) == stepperPlace(bar, at));
    // SPEED: 31 clicks, 0.02 to 20, rising by a fifth to a third, 1 Hz among them; held ends.
    CHECK(modSpeedClick(0) == 0.02f && modSpeedClick(kModSpeedClickCount - 1) == 20.f && kModSpeedClickCount == 31);
    CHECK(kModSpeedLeast == 0.02f && kModSpeedMost == 20.f && modSpeedClick(-1) == 0.02f && modSpeedClick(99) == 20.f && modSpeedClick(17) == kModSpeedUsual);
    for (int i = 1; i < kModSpeedClickCount; ++i) {
        const float ratio = modSpeedClick(i) / modSpeedClick(i - 1);
        CHECK(ratio >= 1.19f && ratio <= 1.34f);
        CHECK(modSpeedStep(modSpeedClick(i - 1), 1) == modSpeedClick(i) && modSpeedStep(modSpeedClick(i), -1) == modSpeedClick(i - 1));
    }
    CHECK(modSpeedStep(20.f, 1) == 20.f && modSpeedStep(0.02f, -1) == 0.02f && modSpeedStep(1.f, 0) == 1.f); // the ends stop
    CHECK(modSpeedStep(1.1f, 1) == 1.2f && modSpeedStep(1.1f, -1) == 1.f && modSpeedStep(0.0234f, -1) == 0.02f); // between two: to the next
    CHECK(modSpeedStep(99.f, -1) == 15.f && modSpeedStep(0.f, 1) == 0.025f);
    CHECK(modSpeed(0.f) == 0.02f && modSpeed(1e9f) == 20.f && modSpeed(std::nanf("")) == 0.02f && modSpeed(3.3f) == 3.3f);
    CHECK(modSpeedPart(0.02f) == 0.f && near(modSpeedPart(20.f), 1.0, 1e-6) && near(modSpeedPart(std::sqrt(0.02f * 20.f)), 0.5, 1e-6));
    CHECK(near(modSpeedTurned(1.f, 1.0), 2.0, 1e-6) && near(modSpeedTurned(1.f, -2.0), 0.25, 1e-6) && modSpeedTurned(10.f, 5.0) == 20.f && modSpeedTurned(0.05f, -5.0) == 0.02f);
}

// A STEPPER: its row as written, round and round LENGTH long; CHANCE plays fresh rolls; SLEW glides.
static void testTheStepper() {
    ModSettings m;
    m.kind = kModStepper;
    m.rate = kModRateStep;
    m.length = 5;
    for (int i = 0; i < kModSteps; ++i) m.steps[i] = 0.1f * (i + 1) * (i % 2 ? -1.f : 1.f);
    for (long index = -10; index < 60; ++index) {
        const int place = static_cast<int>(((index % 5) + 5) % 5);
        CHECK(stepValue(m, index + 0.0, 4, 0) == static_cast<double>(m.steps[place]));
        CHECK(stepValue(m, index + 0.999, 4, 0) == static_cast<double>(m.steps[place]));
    }
    // A rate of 1/4: each value held four steps.
    m.rate = kModRateQuarter;
    CHECK(stepValue(m, 3.9, 4, 0) == static_cast<double>(m.steps[0]) && stepValue(m, 4.0, 4, 0) == static_cast<double>(m.steps[1]));
    CHECK(stepValue(m, 20.0, 4, 0) == static_cast<double>(m.steps[0])); // round again after five
    m.rate = kModRateStep;
    // A written value beyond -1..1, or not a number, is held to it.
    m.steps[0] = 3.f;
    m.steps[1] = std::nanf("");
    CHECK(stepValue(m, 0.5, 4, 0) == 1.0 && stepValue(m, 1.5, 4, 0) == 0.0);
    m.steps[0] = -7.f;
    CHECK(stepValue(m, 0.5, 4, 0) == -1.0);
    m.steps[0] = 0.1f;
    m.steps[1] = -0.2f;
    // CHANCE 1: every value a fresh roll, from where and which time round (by hand).
    m.chance = 1.f;
    int fresh = 0;
    for (long index = 0; index < 50; ++index) {
        const int place = static_cast<int>(index % 5);
        const long round = index / 5;
        const double roll = chordSigned(chordSeed(4, 0, place, round), kRollMod, 0 * 32 + kModRollValue);
        CHECK(stepValue(m, index + 0.5, 4, 0) == roll);
        fresh += roll != static_cast<double>(m.steps[place]);
    }
    CHECK(fresh == 50);
    // CHANCE 0: the row, always. In between: about that share fresh, the same each time asked.
    m.chance = 0.f;
    for (long index = 0; index < 50; ++index) CHECK(stepValue(m, index + 0.5, 4, 0) == static_cast<double>(m.steps[index % 5]));
    m.chance = 0.3f;
    int rolled = 0;
    for (long index = 0; index < 5000; ++index) {
        const double v = stepValue(m, index + 0.5, 4, 1);
        CHECK(v == stepValue(m, index + 0.5, 4, 1));
        const std::uint32_t seed = chordSeed(4, 0, static_cast<int>(index % 5), index / 5);
        const bool roll = chordUnit(seed, kRollMod, 1 * 32 + kModRollChance) < 0.3;
        CHECK(v == (roll ? chordSigned(seed, kRollMod, 1 * 32 + kModRollValue) : static_cast<double>(m.steps[index % 5])));
        rolled += roll;
    }
    CHECK(rolled > 1350 && rolled < 1650);
    m.chance = 0.f;
    // SLEW: a quarter of each value's time spent gliding in from the one before, in a straight line.
    m.slew = 0.25f;
    for (long index = 1; index < 30; ++index) {
        const double from = m.steps[(index - 1) % 5], to = m.steps[index % 5];
        CHECK(near(stepValue(m, index + 0.0, 4, 0), from, 1e-12));
        CHECK(near(stepValue(m, index + 0.125, 4, 0), 0.5 * (from + to), 1e-6));
        CHECK(stepValue(m, index + 0.25, 4, 0) == to && stepValue(m, index + 0.9, 4, 0) == to);
    }
    CHECK(stepValue(m, 0.0, 4, 0) == static_cast<double>(m.steps[0])); // the first value after Play does not glide in
    // SLEW from a rolled value glides from that roll, worked out again, not remembered.
    m.chance = 1.f;
    const double before = stepValue(ModSettings(m), 6.9, 4, 0);
    CHECK(near(stepValue(m, 7.0, 4, 0), before, 1e-12));
}

// What a MOD adds, and the combine: centre plus every offset, one clamp; nothing added is the
// centre exactly as it is.
static void testTheCombine() {
    ModSettings m;
    m.kind = kModLfo;
    m.target = 3;
    m.depth = -0.5f;
    m.shape = kModSquare;
    CHECK(m.on() && modOffsetAt(m, 1.0, 0, 0) == -0.5f && modOffsetAt(m, 9.0, 0, 0) == 0.5f);
    m.depth = 4.f;
    CHECK(modOffsetAt(m, 1.0, 0, 0) == 1.f); // depth held to 100%
    m.kind = kModOff;
    CHECK(!m.on() && modOffsetAt(m, 1.0, 0, 0) == 0.f);
    m.kind = kModStepper;
    m.target = 22; // a spare lane is no target
    CHECK(!m.on() && modOffsetAt(m, 1.0, 0, 0) == 0.f);
    m.target = 21;
    m.depth = 0.f;
    CHECK(!m.on());
    m.depth = 0.5f;
    m.steps[0] = -1.f;
    CHECK(m.on() && modOffsetAt(m, 0.5, 0, 0) == -0.5f);
    // The combine.
    CHECK(modGiven(0.4f, 0.f) == 0.4f && modGiven(1.7f, 0.f) == 1.7f && modGiven(-0.3f, 0.f) == -0.3f); // as it is: not held
    CHECK(modGiven(0.4f, std::nanf("")) == 0.4f);
    CHECK(modGiven(0.4f, 0.25f) == 0.65f && modGiven(0.4f, -0.25f) == 0.4f - 0.25f);
    CHECK(modGiven(0.9f, 0.3f) == 1.f && modGiven(0.1f, -0.3f) == 0.f && modGiven(0.5f, -0.5f) == 0.f && modGiven(0.5f, 0.5f) == 1.f);
    CHECK(modGiven(std::nanf(""), 0.2f) == 0.f);
    CHECK(modGiven(0.5f, 0.3f + 0.4f) == 1.f && modGiven(0.5f, 0.3f - 0.4f) == 0.5f + (0.3f - 0.4f)); // two on one target add, then one clamp
}

// One field at a time, as SetMod carries it: held to what it can be, and read back the same.
static void testTheFields() {
    CHECK(kModFieldKind == 0 && kModFieldTarget == 1 && kModFieldDepth == 2 && kModFieldRate == 3 && kModFieldShape == 4 && kModFieldRestart == 5 &&
          kModFieldPhase == 6 && kModFieldLength == 7 && kModFieldSlew == 8 && kModFieldChance == 9 && kModFieldSpeed == 10 && kModFields == 11);
    ModSettings m;
    m.steps[3] = 0.25f;
    const float values[kModFields] = {2.f, 13.f, -0.4f, 5.f, 6.f, 0.f, 3.f, 7.f, 0.3f, 0.6f, 2.5f};
    for (int f = 0; f < kModFields; ++f) modSet(m, f, values[f]);
    for (int f = 0; f < kModFields; ++f) CHECK(modGet(m, f) == values[f]);
    CHECK(m.on());
    // Held: a number nobody knows, not a number, too far out.
    modSet(m, kModFieldKind, 3.f);
    CHECK(m.kind == kModOff && !m.on());
    modSet(m, kModFieldKind, std::nanf(""));
    CHECK(m.kind == kModOff);
    modSet(m, kModFieldKind, 1.4f);
    CHECK(m.kind == kModLfo);
    modSet(m, kModFieldKind, 1.6f); // the nearest whole number: a float sent as 2 that arrives as 1.9999 is still 2
    CHECK(m.kind == kModStepper);
    modSet(m, kModFieldKind, 1.f);
    modSet(m, kModFieldTarget, 22.f);
    CHECK(m.target == kModTargetNone);
    modSet(m, kModFieldTarget, 1e30f);
    CHECK(m.target == kModTargetNone);
    modSet(m, kModFieldTarget, 21.f);
    CHECK(m.target == 21);
    modSet(m, kModFieldDepth, 3.f);
    CHECK(m.depth == 1.f);
    modSet(m, kModFieldDepth, -3.f);
    CHECK(m.depth == -1.f);
    modSet(m, kModFieldDepth, std::nanf(""));
    CHECK(m.depth == 0.f);
    modSet(m, kModFieldRate, 13.f);
    CHECK(m.rate == kModRateBar);
    modSet(m, kModFieldRate, 12.f);
    CHECK(m.rate == kModRateFree);
    modSet(m, kModFieldSpeed, 99.f);
    CHECK(m.speed == kModSpeedMost);
    modSet(m, kModFieldSpeed, 0.f);
    CHECK(m.speed == kModSpeedLeast);
    modSet(m, kModFieldSpeed, std::nanf(""));
    CHECK(m.speed == kModSpeedLeast);
    modSet(m, kModFieldSpeed, 0.37f);
    CHECK(m.speed == 0.37f && ModSettings().speed == kModSpeedUsual && kModSpeedUsual == 1.f);
    modSet(m, kModFieldShape, -1.f);
    CHECK(m.shape == kModSine);
    modSet(m, kModFieldPhase, 4.f);
    CHECK(m.phase == 0);
    modSet(m, kModFieldLength, 40.f);
    CHECK(m.length == 16);
    modSet(m, kModFieldLength, -40.f);
    CHECK(m.length == 1);
    modSet(m, kModFieldSlew, 2.f);
    modSet(m, kModFieldChance, std::nanf(""));
    CHECK(m.slew == 1.f && m.chance == 0.f);
    modSetStep(m, 3, 2.f);
    modSetStep(m, 4, std::nanf(""));
    modSetStep(m, 16, 0.5f); // no such place: nothing
    modSetStep(m, -1, 0.5f);
    CHECK(m.steps[3] == 1.f && m.steps[4] == 0.f && m.steps[15] == 0.f && m.steps[0] == 0.f);
    // A STEPPER's place: its row round and round from Play, at its rate.
    ModSettings s;
    s.kind = kModStepper;
    s.rate = kModRateEighth; // two steps a value
    s.length = 3;
    CHECK(stepperPlace(s, 0.0) == 0 && stepperPlace(s, 1.99) == 0 && stepperPlace(s, 2.0) == 1 && stepperPlace(s, 5.5) == 2 && stepperPlace(s, 6.0) == 0);
    CHECK(stepperPlace(s, -0.5) == 2); // before Play: the place before the first
    s.kind = kModLfo;
    CHECK(stepperPlace(s, 3.0) == -1);
}

// ---- the engine ---------------------------------------------------------------------------------

static bool measuring = false; // --measure: print what the measuring tests find

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
// One MOD's settings: an LFO (a SQUARE eight bars long adds its depth, whole, for a minute at 120).
static ModSettings lfo(int target, float depth, int shape = kModSquare, int rate = kModRateEightBars) {
    ModSettings m;
    m.kind = kModLfo;
    m.target = target;
    m.depth = depth;
    m.shape = shape;
    m.rate = rate;
    return m;
}
static ModSettings stepper(int target, float depth, int rate, float chance, float slew = 0.f, int length = 8) {
    ModSettings m;
    m.kind = kModStepper;
    m.target = target;
    m.depth = depth;
    m.rate = rate;
    m.chance = chance;
    m.slew = slew;
    m.length = length;
    for (int i = 0; i < kModSteps; ++i) m.steps[i] = static_cast<float>((i * 7) % 11) / 5.f - 1.f;
    return m;
}
// A synth and an effect that make no sound and write down what they are given.
struct Given {
    int what;
    float value;
    bool operator==(const Given& o) const { return what == o.what && value == o.value; }
};
struct SpySynth : Instrument {
    std::vector<Given>* log;
    float now[8]; // each macro as the synth has it: a preset puts them back to the middle, as Phazed's and FM-6's do
    explicit SpySynth(std::vector<Given>* to) : log(to) {
        for (float& m : now) m = 0.5f;
    }
    void prepare(float) override {}
    void selectPreset(int) override {
        for (float& m : now) m = 0.5f;
    }
    void setSetting(int, int) override {}
    void setMacro(int macro, float value) override {
        log->push_back({macro, value});
        if (macro >= 0 && macro < 8) now[macro] = value;
    }
    void noteOn(int, float) override {}
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    }
};
struct SpyEffect : Effect {
    std::vector<Given>* log;
    explicit SpyEffect(std::vector<Given>* to) : log(to) {}
    void setParam(int index, float value) override { log->push_back({index, value}); }
    void process(float*, float*, int) override {}
};
// Track 1 a synth that writes down its macros, its slot 2 an effect that writes down its settings.
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<Given> macros, params;
    SpySynth* synth = nullptr; // the engine's (read only, between renders)
    Rig() {
        macros.reserve(400000);
        params.reserve(400000);
        engine->prepare(48000.f);
        Command c = make(Command::InstallSynth, 1);
        c.pointer = synth = new SpySynth(&macros);
        CHECK(engine->send(c));
        frames(64);
        engine->setTrackEffect(1, 1, std::unique_ptr<Effect>(new SpyEffect(&params)));
        macros.clear();
        params.clear();
    }
    void frames(int n, int block = 512) {
        std::vector<float> out(2 * static_cast<std::size_t>(block));
        for (; n > 0; n -= block) engine->render(out.data(), n < block ? n : block);
    }
    void send(const Command& c) {
        CHECK(engine->send(c));
        frames(1);
    }
    void mod(int m, const ModSettings& s, int track = 1) {
        CHECK(engine->setMod(track, m, s));
        frames(1);
    }
    // Stopped: X held on the knob runs its MODs from now (the tick, macros too, comes at once).
    void hear(unsigned mask, int track = 1) { send(make(Command::ModAudition, track, static_cast<int>(mask))); }
    // A MOD set, and heard from now.
    void modHeard(int m, const ModSettings& s, int track = 1) {
        CHECK(engine->setMod(track, m, s));
        hear(3, track);
    }
    // A lane written on every step of the first bar of scene 1.
    void write(int lane, float value) {
        for (int step = 0; step < 16; ++step) {
            Command c = make(Command::SetAutomation, 1, lane, step, value);
            c.scene = 0;
            CHECK(engine->send(c));
        }
        frames(1);
    }
    float lastMacro(int m) const {
        for (auto it = macros.rbegin(); it != macros.rend(); ++it)
            if (it->what == m) return it->value;
        return -99.f;
    }
    float lastParam(int p) const {
        for (auto it = params.rbegin(); it != params.rend(); ++it)
            if (it->what == p) return it->value;
        return -99.f;
    }
    int count(const std::vector<Given>& log, int what) const {
        int n = 0;
        for (const Given& g : log) n += g.what == what;
        return n;
    }
};

// A knob's own command (SetMacro, SetEffectParam, SetSend): with nothing added, the value goes out
// as it is (out of 0..1 too, as before there were MODs); with a MOD, centre plus what it adds held
// to 0..1, and the centre is the knob's value, never the wobble.
static void testAKnobGoesOutThroughTheOneWay() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 2), echo = autoLaneOfSend(0), space = autoLaneOfSend(1);
    // Nothing added: as before.
    rig.send(make(Command::SetMacro, 1, 2, 0, 0.4f));
    CHECK(rig.macros.size() == 1 && rig.lastMacro(2) == 0.4f && rig.engine->laneCentre(1, 2) == 0.4f);
    rig.send(make(Command::SetMacro, 1, 2, 0, 1.6f));
    CHECK(rig.lastMacro(2) == 1.6f && rig.engine->laneCentre(1, 2) == 1.6f); // not held here: the synth holds it, as it always did
    rig.send(make(Command::SetMacro, 1, 9, 0, 0.3f)); // no lane of its own: straight to the synth, as before
    CHECK(rig.lastMacro(9) == 0.3f);
    Command p = make(Command::SetEffectParam, 1, 1, 2, 0.3f);
    rig.send(p);
    CHECK(rig.params.size() == 1 && rig.lastParam(2) == 0.3f && rig.engine->laneCentre(1, fx) == 0.3f);
    p.b = 5; // a setting no lane has
    p.value = 0.7f;
    rig.send(p);
    CHECK(rig.lastParam(5) == 0.7f);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.6f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.6f && rig.engine->laneCentre(1, echo) == 0.6f);
    rig.send(make(Command::SetSend, 1, 1, 0, 1.4f)); // a send is held to 0..1, centre and all, as before
    CHECK(rig.engine->sendGiven(1, 1) == 1.f && rig.engine->laneCentre(1, space) == 1.f);
    // A MOD on, but stopped and nothing heard: nothing moves.
    rig.mod(0, lfo(2, 0.25f));
    rig.frames(2000);
    CHECK(rig.lastMacro(2) == 1.6f && rig.engine->modOffsetForTests(1, 2) == 0.f && rig.engine->modShown(1, 0) == 0.f);

    // Heard (X held on the knob): given at once about the centre, which stays.
    rig.hear(1);
    CHECK(rig.lastMacro(2) == 1.f && rig.engine->laneCentre(1, 2) == 1.6f && rig.engine->modShown(1, 0) == 0.25f);
    rig.send(make(Command::SetMacro, 1, 2, 0, 0.4f));
    CHECK(rig.lastMacro(2) == 0.65f && rig.engine->laneCentre(1, 2) == 0.4f);
    rig.modHeard(0, lfo(2, -0.5f));
    CHECK(rig.lastMacro(2) == 0.f && rig.engine->laneCentre(1, 2) == 0.4f);
    rig.modHeard(1, lfo(fx, 0.2f));
    CHECK(rig.lastParam(2) == 0.3f + 0.2f && rig.engine->laneCentre(1, fx) == 0.3f);
    p.b = 2;
    p.value = 0.9f;
    rig.send(p);
    CHECK(rig.lastParam(2) == 1.f && rig.engine->laneCentre(1, fx) == 0.9f);
    // A send: its given value held, its centre kept (automation and Stop compare with the centre).
    // MOD 2 aimed at it now: what it added to the effect's setting goes, at once.
    rig.mod(1, lfo(echo, 0.3f));
    CHECK(rig.lastParam(2) == 0.9f && rig.engine->modOffsetForTests(1, fx) == 0.f);
    rig.frames(64); // its next tick
    rig.send(make(Command::SetSend, 1, 0, 0, 0.9f));
    CHECK(rig.engine->sendGiven(1, 0) == 1.f && rig.engine->laneCentre(1, echo) == 0.9f);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.2f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f + 0.3f && rig.engine->laneCentre(1, echo) == 0.2f);
    rig.modHeard(1, lfo(echo, -0.5f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.f && rig.engine->laneCentre(1, echo) == 0.2f);
    // Turned off (depth 0): nothing added, the centre exactly.
    rig.mod(1, lfo(echo, 0.f));
    rig.mod(0, lfo(2, 0.f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f && rig.lastMacro(2) == 0.4f);
    CHECK(rig.engine->modOffsetForTests(1, echo) == 0.f && rig.engine->modOffsetForTests(1, 2) == 0.f);
    CHECK(rig.engine->modShown(1, 0) == 0.f && rig.engine->modShown(1, 1) == 0.f);
}

// A centre not known yet (an effect put in a slot before its settings are told) is not moved
// about: nothing is given until the knob's value comes.
static void testAnUnknownCentreIsLeftAlone() {
    Rig rig;
    const int lane = autoLaneOfEffect(0, 1); // slot 1: no effect told anything yet
    CHECK(rig.engine->laneCentre(1, lane) == kAutoNone);
    rig.engine->setTrackEffect(1, 0, std::unique_ptr<Effect>(new SpyEffect(&rig.params)));
    rig.params.clear();
    rig.modHeard(0, lfo(lane, 0.3f));
    rig.frames(256);
    CHECK(rig.params.empty());
    rig.send(make(Command::SetEffectParam, 1, 0, 1, 0.25f));
    CHECK(rig.params.size() == 1 && rig.lastParam(1) == 0.25f + 0.3f);
    // Let go (X up, Stop) of a MOD whose effect has still said nothing: nothing given, not the
    // unknown centre (-1) either.
    std::vector<Given> other;
    rig.engine->setTrackEffect(1, 2, std::unique_ptr<Effect>(new SpyEffect(&other)));
    const int third = autoLaneOfEffect(2, 0);
    rig.mod(1, lfo(third, 0.4f));
    rig.hear(3);
    rig.frames(256);
    CHECK(rig.engine->modOffsetForTests(1, third) == 0.4f && other.empty());
    rig.hear(0);
    CHECK(rig.engine->modOffsetForTests(1, third) == 0.f && other.empty());
    rig.hear(3);
    rig.frames(256);
    rig.send(make(Command::Play));
    rig.frames(256);
    rig.send(make(Command::Stop));
    CHECK(other.empty());
}

// Automation moves the centre; the MOD rides on it while the song plays; Stop gives every knob its
// centre with nothing added. The centre is what automation and Stop compare: written equal to the
// knob, nothing glides, whatever is added.
static void testAutomationMovesTheCentre() {
    Rig rig;
    rig.send(make(Command::SetMacro, 1, 3, 0, 0.5f));
    rig.mod(0, lfo(3, 0.1f));
    rig.write(3, 0.8f);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000); // a step: the glide takes its first half
    CHECK(rig.engine->laneCentre(1, 3) == 0.8f);
    CHECK(near(rig.lastMacro(3), 0.9, 1e-6) && rig.engine->modShown(1, 0) == 0.1f);
    rig.send(make(Command::Stop));
    CHECK(rig.engine->laneCentre(1, 3) == 0.5f && rig.lastMacro(3) == 0.5f && rig.engine->modShown(1, 0) == 0.f);
    CHECK(rig.engine->modOffsetForTests(1, 3) == 0.f);
    // A lane written at the knob's value, no MOD: nothing glides, so nothing is given while it plays.
    rig.mod(0, lfo(3, 0.f));
    rig.write(3, 0.5f);
    rig.macros.clear();
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000 * 4);
    CHECK(rig.macros.empty());
    rig.send(make(Command::Stop));
    CHECK(rig.macros.empty());
    // A send's automation: the centre glides, the given value is the centre plus the offset, held.
    const int echo = autoLaneOfSend(0);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.1f));
    rig.mod(0, lfo(echo, 0.4f));
    rig.write(echo, 0.9f);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000);
    CHECK(rig.engine->laneCentre(1, echo) == 0.9f && rig.engine->sendGiven(1, 0) == 1.f);
    rig.send(make(Command::Stop));
    CHECK(rig.engine->laneCentre(1, echo) == 0.1f && rig.engine->sendGiven(1, 0) == 0.1f);
    // A panic stops the song as Stop does: nothing added (it does not put automation back, as before).
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000);
    CHECK(rig.engine->sendGiven(1, 0) == 1.f);
    rig.send(make(Command::Panic, 0, 1));
    CHECK(rig.engine->sendGiven(1, 0) == 0.9f && rig.engine->modShown(1, 0) == 0.f);
    rig.send(make(Command::Panic, 0, 0));
}

// The tick: every kModTick samples from each step's first sample; a synth's macros on every
// kModMacroTicks'th, an effect's settings and the sends on every one. The value given is the
// rule's at the position of the tick's first sample.
static void testTheTick() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f));
    const ModSettings onMacro = lfo(3, 0.4f, kModSine, kModRateHalfStep), onFx = lfo(fx, 0.4f, kModSine, kModRateHalfStep);
    rig.mod(0, onMacro);
    rig.mod(1, onFx);
    rig.macros.clear();
    rig.params.clear();
    rig.send(make(Command::Play, 0, 0, 1)); // its first sample: a tick, macros too
    CHECK(rig.macros.size() == 0 && rig.params.size() == 0); // a SINE starts at 0: the centre is what each has
    rig.frames(512);                                         // 513 samples since Play: the ticks at 64 ... 512
    CHECK(rig.count(rig.params, 0) == 8 && rig.count(rig.macros, 3) == 1);
    CHECK(rig.lastMacro(3) == modGiven(0.5f, modOffsetAt(onMacro, 512.0 / 6000.0, 1, 0)));
    CHECK(rig.lastParam(0) == modGiven(0.5f, modOffsetAt(onFx, 512.0 / 6000.0, 1, 1)));
    CHECK(rig.engine->modShown(1, 1) == modOffsetAt(onFx, 512.0 / 6000.0, 1, 1));
    // Four whole steps: 94 ticks a step (93 of 64 and one of 48, counted from each step's start),
    // the macros on 12 of them.
    rig.macros.clear();
    rig.params.clear();
    rig.frames(4 * 6000);
    CHECK(rig.count(rig.params, 0) == 4 * 94);
    CHECK(rig.count(rig.macros, 3) == 4 * 12);
    if (measuring) std::printf("ticks in four steps at 120: %d effect, %d macro\n", rig.count(rig.params, 0), rig.count(rig.macros, 3));
    // A STEPPER held: nothing is given while its value stays.
    rig.mod(1, stepper(fx, 0.5f, kModRateBar, 0.f));
    rig.frames(6000);
    CHECK(rig.lastParam(0) == 0.5f - 0.5f); // its first written value, -100%
    rig.params.clear();
    rig.frames(6000 * 4);
    CHECK(rig.params.empty() && rig.engine->modPlace(1, 1) == 0 && rig.engine->modShown(1, 1) == 0.5f * -1.f);
    rig.frames(6000 * 12);
    CHECK(rig.engine->modPlace(1, 1) == 1 && rig.engine->modShown(1, 1) == 0.5f * (7.f / 5.f - 1.f)); // the next bar: the next
}

// Two MODs on one setting add, then one clamp; each shows what it adds by itself.
static void testTwoOnOneSettingAdd() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.4f));
    rig.mod(0, lfo(fx, 0.3f));
    rig.mod(1, lfo(fx, -0.1f));
    rig.send(make(Command::Play, 0, 0, 1));
    CHECK(rig.lastParam(0) == 0.4f + (0.3f + -0.1f) && rig.engine->modShown(1, 0) == 0.3f && rig.engine->modShown(1, 1) == -0.1f);
    rig.mod(1, lfo(fx, 0.5f));
    rig.frames(64);
    CHECK(rig.lastParam(0) == 1.f);
    rig.mod(0, lfo(fx, 0.f)); // MOD 1 off: MOD 2 alone, from its next tick
    rig.frames(64);
    CHECK(rig.lastParam(0) == 0.4f + 0.5f);
}

// Play twice gives the same: every value given, in order, whatever the blocks.
static void testPlayTwiceGivesTheSame() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 3);
    rig.send(make(Command::SetEffectParam, 1, 1, 3, 0.5f));
    rig.mod(0, lfo(4, 0.6f, kModRandom, kModRateQuarterTriplet));
    rig.mod(1, stepper(fx, 0.7f, kModRateEighth, 0.6f, 0.4f, 5));
    std::vector<Given> macros[2], params[2];
    for (int round = 0; round < 2; ++round) {
        rig.macros.clear();
        rig.params.clear();
        rig.send(make(Command::Play, 0, 0, 1));
        rig.frames(6000 * 40, round == 0 ? 512 : 333);
        macros[round] = rig.macros;
        params[round] = rig.params;
        rig.send(make(Command::Stop));
    }
    if (measuring) std::printf("play twice: %zu macros and %zu settings given in forty steps\n", macros[0].size(), params[0].size());
    CHECK(macros[0].size() > 10 && params[0].size() > 50);
    CHECK(macros[0] == macros[1] && params[0] == params[1]);
}

// A tempo change keeps the MOD on the beat: the position is the step clock's, which SetTempo keeps
// in its place. A RAMP one bar long, the tempo moved in the bar's fourth step: at the next bar
// line it starts again from its bottom.
static void testATempoChangeKeepsThePhase() {
    for (float to : {90.f, 171.f}) {
        Rig rig;
        const int echo = autoLaneOfSend(0);
        rig.mod(0, lfo(echo, 0.5f, kModRampUp, kModRateBar));
        rig.send(make(Command::Play, 0, 0, 1));
        rig.frames(3 * 6000 + 2500);
        rig.send(make(Command::SetTempo, 0, 0, 0, to));
        int samples = 0;
        while (rig.engine->playStep() != 0 && samples < 200000) rig.frames(1), ++samples;
        CHECK(rig.engine->playStep() == 0);
        CHECK(near(rig.engine->modShown(1, 0), -0.5, 1e-4)); // a sample into the bar: the ramp's start
        if (measuring) std::printf("tempo to %.0f: at the bar line the ramp gives %.6f\n", to, rig.engine->modShown(1, 0));
    }
}

// What a song puts out, in blocks of `block`, with `helpers`: drums through the old FILTER and
// VOWEL, a Phazed through CHORUS, both sent to PING on ECHO; MODs on a macro, the old FILTER's
// and VOWEL's settings and a send, of each kind and several shapes (`gliding`: on SATURATE's
// DRIVE and CHORUS's DEPTH instead, which glide a call at a time). No HALL (it is not
// the same cut into other blocks), no automation (its glide moves at chunks).
static std::vector<float> song(int block, int helpers, bool mods, int frames, bool phazedToo = true, bool gliding = false, float bpm = 133.f, bool free = false, std::uint32_t take = 0) {
    std::unique_ptr<Engine> e(new Engine);
    e->setTake(take); // (a bounce's: Engine::setTake)
    e->prepare(48000.f);
    std::vector<float> out(2 * static_cast<std::size_t>(frames)), scratch(1024);
    e->send(make(Command::SetTempo, 0, 0, 0, bpm));
    if (phazedToo) e->addSynthTrack(2, SynthKind::Phazed);
    e->render(scratch.data(), 64);
    e->setEffect(0, 0, EffectKind::Filter);
    e->setEffect(0, 1, gliding ? EffectKind::Saturate : EffectKind::Vowel);
    if (phazedToo) e->setEffect(2, 0, EffectKind::Chorus);
    e->setEffect(kEchoBus, 0, EffectKind::Ping);
    for (int lane = 0; lane < 4; ++lane)
        for (int step = lane % 2; step < 16; step += 2) {
            Command c = make(Command::SetStep, 0, lane, step, 0.8f);
            e->send(c);
        }
    for (int n = 0; n < 4; ++n) {
        Command c = make(Command::SetNote, 2, n, n * 4);
        c.c = 48 + 5 * n;
        c.d = 2;
        c.value = 0.8f;
        e->send(c);
    }
    e->send(make(Command::SetSend, 0, 0, 0, 0.3f));
    e->send(make(Command::SetSend, 2, 0, 0, 0.2f));
    e->render(scratch.data(), 256);
    if (mods && !gliding) {
        e->setMod(0, 0, lfo(autoLaneOfEffect(0, 0), 0.3f, kModSine, kModRateEighth));      // the old FILTER's CUTOFF
        e->setMod(0, 1, lfo(autoLaneOfEffect(1, 1), 0.4f, kModTriangle, kModRateHalfStep)); // VOWEL's SHIFT
        e->setMod(2, 0, lfo(phazed::Tone, 0.5f, kModDrift, kModRateQuarterTriplet));
        e->setMod(2, 1, stepper(autoLaneOfSend(0), 0.4f, kModRateQuarter, 0.5f, 0.3f, 6));
        if (!phazedToo) e->setMod(0, 1, stepper(autoLaneOfSend(0), 0.4f, kModRateQuarter, 0.5f, 0.3f, 6));
        if (free) { // FREE: the CUTOFF's at 3.7 Hz, TONE's at 0.6 Hz (each its own count of seconds)
            ModSettings f = lfo(autoLaneOfEffect(0, 0), 0.3f, kModSine, kModRateFree);
            f.speed = 3.7f;
            e->setMod(0, 0, f);
            f = lfo(phazed::Tone, 0.5f, kModDrift, kModRateFree);
            f.speed = 0.6f;
            if (phazedToo) e->setMod(2, 0, f);
        }
    }
    if (mods && gliding) {
        e->setMod(0, 0, lfo(autoLaneOfEffect(1, 0), 0.3f, kModSine, kModRateEighth));      // SATURATE's DRIVE
        e->setMod(2, 0, lfo(autoLaneOfEffect(0, 1), 0.4f, kModTriangle, kModRateHalfStep)); // CHORUS's DEPTH
    }
    e->render(scratch.data(), 256);
    if (helpers) e->useWorkers(helpers);
    e->send(make(Command::Play, 0, 0, 1));
    for (int at = 0; at < frames; at += block) e->render(out.data() + 2 * at, frames - at < block ? frames - at : block);
    return out;
}

// The same to the bit, cut into any blocks, on any number of helpers: every value is worked out
// from where the song is, on ticks counted from each step's start. (At 133 BPM a step is not a
// whole number of samples, 5413.5, and a song with notes that end or wait is then not the same in
// blocks of ONE sample, MOD or none, as on main: so blocks of 1 there without the Phazed track,
// and with it at 120, where a step is 6000 samples. Found in review, 2026-11-14: it is the tempo,
// not Phazed.)
static void testTheSameInAnyBlocks() {
    const int frames = 2 * 16 * 5414; // two bars at 133
    for (bool mods : {false, true}) {
        const std::vector<float> plain = song(64, 0, mods, frames);
        for (int block : {7, 48, 333, 512}) {
            const bool same = song(block, 0, mods, frames) == plain;
            CHECK(same);
            if (!same) std::printf("  blocks of %d differ (%s)\n", block, mods ? "MODs on" : "no MOD");
        }
        CHECK(song(256, 3, mods, frames) == plain);
        CHECK(song(64, 3, mods, frames) == plain);
        if (mods) CHECK(plain != song(64, 0, false, frames)); // (the MODs do something)
        const std::vector<float> drums = song(64, 0, mods, frames, false);
        CHECK(song(1, 0, mods, frames, false) == drums && song(333, 3, mods, frames, false) == drums);
        const int whole = 2 * 16 * 6000; // two bars at 120
        const std::vector<float> at120 = song(64, 0, mods, whole, true, false, 120.f);
        CHECK(song(1, 0, mods, whole, true, false, 120.f) == at120 && song(333, 3, mods, whole, true, false, 120.f) == at120);
        // A bounce's TAKE: DRIFT and the STEPPER's CHANCE rolled again, the same take the same in any blocks. With no
        // MOD this song has no roll in it, and every take is the song as made.
        const std::vector<float> take1 = song(64, 0, mods, frames, true, false, 133.f, false, 1);
        CHECK(song(333, 3, mods, frames, true, false, 133.f, false, 1) == take1);
        CHECK(mods ? take1 != plain && take1 != song(64, 0, mods, frames, true, false, 133.f, false, 2) : take1 == plain);
    }
}

// A bounce's TAKE in the rule (takeSeed, chord_rule.hpp): RANDOM, DRIFT and a STEPPER's rolls (its CHANCE, its
// value, and the value its SLEW glides from) are the take's own; take 0 is the rule as it always was.
static void testTakes() {
    ModSettings m = lfo(autoLaneOfEffect(0, 0), 1.f, kModRandom, kModRateQuarter); // (4 steps a cycle)
    const auto randomAt = [](long cycle, std::uint32_t take) { return chordSigned(takeSeed(chordSeed(2, 1, 0, cycle), take), kRollMod, 1 * 32 + kModRollValue); };
    ModSettings d = m;
    d.shape = kModDrift;
    int otherTake = 0;
    for (long cycle = 0; cycle < 40; ++cycle) {
        CHECK(lfoValue(m, cycle * 4.0 + 1.0, 2, 1, 0) == lfoValue(m, cycle * 4.0 + 1.0, 2, 1) && lfoValue(m, cycle * 4.0 + 1.0, 2, 1, 5) == randomAt(cycle, 5));
        CHECK(near(lfoValue(d, cycle * 4.0 + 2.0, 2, 1, 5), 0.5 * (randomAt(cycle, 5) + randomAt(cycle + 1, 5)), 1e-12)); // from this cycle's to the next's
        CHECK(modOffsetAt(m, cycle * 4.0 + 1.0, 2, 1, 1.f, 5) == static_cast<float>(randomAt(cycle, 5)));
        otherTake += randomAt(cycle, 5) != randomAt(cycle, 0);
    }
    CHECK(otherTake > 35);
    ModSettings st = stepper(autoLaneOfEffect(0, 0), 1.f, kModRateStep, 1.f, 0.f, 5);
    const auto rollAt = [](long index, std::uint32_t take) { return chordSigned(takeSeed(chordSeed(4, 0, static_cast<int>(index % 5), index / 5), take), kRollMod, 0 * 32 + kModRollValue); };
    for (long index = 1; index < 50; ++index) {
        CHECK(stepValue(st, index + 0.5, 4, 0, 0) == rollAt(index, 0) && stepValue(st, index + 0.5, 4, 0, 5) == rollAt(index, 5));
        CHECK(modOffsetAt(st, index + 0.5, 4, 0, 1.f, 5) == static_cast<float>(rollAt(index, 5)));
    }
    st.slew = 0.5f; // a quarter in: halfway from the value before, the take's too
    for (long index = 1; index < 50; ++index) CHECK(near(stepValue(st, index + 0.25, 4, 0, 5), rollAt(index - 1, 5) + (rollAt(index, 5) - rollAt(index - 1, 5)) * 0.5, 1e-12));
    // CHANCE 0.5: which places roll is the take's too.
    st.slew = 0.f;
    st.chance = 0.5f;
    for (int i = 0; i < kModSteps; ++i) st.steps[i] = 0.f;
    std::string zero, five;
    for (long index = 0; index < 60; ++index) {
        zero += stepValue(st, index + 0.5, 4, 0, 0) == 0.0 ? '.' : 'x';
        five += stepValue(st, index + 0.5, 4, 0, 5) == 0.0 ? '.' : 'x';
    }
    CHECK(zero != five);
}

// Known, not mended: an effect that glides a setting a CALL at a time (fx::Glide begun in each
// process(): CHORUS, COMP, DUCK, HALL, PING, PLATE, SATURATE, SPRING, TAPE) is the same in other
// blocks only once its settings are at rest, and a MOD never lets them rest. Cut otherwise, its
// sound under a MOD differs a little (measured: under 0.003 on a peak of 0.5). The TICK effects
// (COMB, VOWEL, EQ, ...), the old FILTER, the sends and the macros do not. Helpers never change it.
static void testAGlidingEffectUnderAModIsCloseInOtherBlocks() {
    const int frames = 2 * 16 * 5414;
    const std::vector<float> a = song(64, 0, true, frames, true, true);
    CHECK(song(64, 3, true, frames, true, true) == a);
    for (int block : {7, 333}) {
        const std::vector<float> b = song(block, 0, true, frames, true, true);
        double most = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) most = std::max(most, static_cast<double>(std::fabs(a[i] - b[i])));
        CHECK(most > 0.0 && most < 0.005);
        if (measuring) std::printf("SATURATE DRIVE and CHORUS DEPTH under a MOD, blocks of %d against 64: %.2g apart at most\n", block, most);
    }
}

// The chunks are cut on the tick only while a MOD runs: a song with none, a MOD that is OFF, and a
// MOD on with the song stopped are cut as they always were.
static void testChunksAreCutOnlyWhileAModRuns() {
    auto chunks = [](int state) { // 0 none, 1 OFF (its KIND; at depth 0 a MOD runs: its DEPTH lane may be written), 2 on but stopped, 3 on and playing
        Rig rig;
        if (state >= 1) {
            ModSettings m = lfo(autoLaneOfSend(0), 0.4f, kModSine, kModRateBar);
            if (state == 1) m.kind = kModOff;
            rig.mod(0, m);
        }
        if (state != 2) rig.send(make(Command::Play, 0, 0, 1));
        const std::uint64_t before = rig.engine->chunksRendered();
        rig.frames(6000 * 16);
        return rig.engine->chunksRendered() - before;
    };
    const std::uint64_t none = chunks(0), off = chunks(1), stopped = chunks(2), on = chunks(3);
    CHECK(none == off && off > 0);
    CHECK(stopped == 96000 / 64);
    CHECK(on > none);
    if (measuring) std::printf("chunks in a bar at 120, blocks of 512: %llu with no MOD, %llu with one on\n", static_cast<unsigned long long>(none), static_cast<unsigned long long>(on));
}

// A scene put in while a MOD runs: the same, to the bit, as the song built with the scene in it
// (the position counts from Play and holds no scene's number).
static void testASceneMoveEqualsTheSongBuilt() {
    auto hits = [](Engine& e, int scene, int lane, int first) {
        for (int step = first; step < 16; step += 4) {
            Command c = make(Command::SetStep, 0, lane, step, 0.9f);
            c.scene = scene;
            e.send(c);
        }
    };
    std::unique_ptr<Engine> moved(new Engine), built(new Engine);
    std::vector<float> scratch(1024);
    for (Engine* e : {moved.get(), built.get()}) {
        e->prepare(48000.f);
        e->setEffect(0, 0, EffectKind::Saturate);
        e->setEffect(kEchoBus, 0, EffectKind::Ping);
        e->send(make(Command::SetSend, 0, 0, 0, 0.3f));
        const bool three = e == built.get();
        e->send(make(Command::SetSceneCount, 0, three ? 3 : 2));
        hits(*e, 0, 0, 0);
        hits(*e, 0, 1, 2);
        if (three) hits(*e, 1, 0, 0), hits(*e, 1, 1, 2);
        hits(*e, three ? 2 : 1, 2, 1);
        e->render(scratch.data(), 256);
        e->setMod(0, 0, lfo(autoLaneOfSend(0), 0.4f, kModSine, kModRateTwoBars));
        e->setMod(0, 1, stepper(autoLaneOfEffect(0, 0), 0.5f, kModRateEighth, 0.5f));
        e->render(scratch.data(), 256);
        e->send(make(Command::Play));
    }
    const int frames = 5 * 16 * 6000;
    std::vector<float> a(2 * frames), b(2 * frames);
    int at = 0;
    const int moveAt = 6000 * 9 + 123;
    for (; at < moveAt; at += 256) {
        const int n = moveAt - at < 256 ? moveAt - at : 256;
        moved->render(a.data() + 2 * at, n);
        built->render(b.data() + 2 * at, n);
    }
    at = moveAt;
    std::unique_ptr<SceneImage> images[kNumTracks];
    images[0].reset(new SceneImage);
    for (int step = 0; step < 16; step += 4) images[0]->level[0][0][step] = 0.9f;
    for (int step = 2; step < 16; step += 4) images[0]->level[0][1][step] = 0.9f;
    CHECK(moved->moveScenes(1, 1, images, 3, 1, 0));
    for (; at < frames; at += 256) {
        const int n = frames - at < 256 ? frames - at : 256;
        moved->render(a.data() + 2 * at, n);
        built->render(b.data() + 2 * at, n);
    }
    CHECK(a == b);
    CHECK(moved->playScene() == built->playScene());
}

// Play counts from the scene it starts on, a bounce from the first: a MOD longer than the scenes
// before is in another phase in the bounce than in the scene looped live (the plan's call 22).
// One bar long it is the same; four bars long it is not. Held both ways.
static void testABounceAndALiveLoop() {
    for (int rate : {kModRateBar, kModRateFourBars}) {
        float shown[2] = {};
        for (int bounce = 0; bounce < 2; ++bounce) {
            Rig rig;
            rig.send(make(Command::SetSceneCount, 0, 3));
            rig.mod(0, lfo(autoLaneOfSend(0), 0.5f, kModRampUp, rate));
            Command play = make(Command::Play, 0, bounce ? 0 : 2, bounce ? 0 : 1);
            play.c = bounce;
            CHECK(rig.engine->send(play));
            if (bounce) rig.frames(2 * 16 * 6000 + 1);
            else rig.frames(1);
            CHECK(rig.engine->playScene() == 2 && rig.engine->playStep() == 0);
            shown[bounce] = rig.engine->modShown(1, 0);
        }
        if (rate == kModRateBar) CHECK(shown[0] == -0.5f && shown[1] == -0.5f);
        else CHECK(shown[0] == -0.5f && shown[1] == 0.f); // scene 3's top: the ramp's start live, its middle in the bounce
    }
}

// The FX screen trying effects in a slot (HoldEffectAutomation): the slot's MOD waits too, with
// nothing added, and comes back at its next tick when the hold is let go.
static void testAHeldSlotWaits() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.4f));
    rig.mod(0, lfo(fx, 0.3f));
    rig.send(make(Command::Play, 0, 0, 1));
    CHECK(rig.lastParam(0) == 0.4f + 0.3f);
    rig.send(make(Command::HoldEffectAutomation, 1, 1, 1));
    CHECK(rig.lastParam(0) == 0.4f && rig.engine->modOffsetForTests(1, fx) == 0.f);
    rig.params.clear();
    rig.frames(6000 * 2);
    CHECK(rig.params.empty());
    rig.send(make(Command::HoldEffectAutomation, 1, 1, 0));
    rig.frames(64);
    CHECK(rig.lastParam(0) == 0.4f + 0.3f);
}

// A setting that is a choice (FILTER's TYPE) is never moved; the same lane on an effect where it is
// an amount (SATURATE's TONE) is. A MOD is never carried over to an effect put in the slot.
static void testAChoiceIsNotMoved() {
    std::unique_ptr<Engine> e(new Engine);
    std::vector<float> out(1024);
    e->setEffect(0, 0, EffectKind::Filter);
    e->render(out.data(), 64);
    const int type = autoLaneOfEffect(0, 2), cutoff = autoLaneOfEffect(0, 0);
    e->setMod(0, 0, lfo(type, 0.3f));
    e->setMod(0, 1, lfo(cutoff, 0.3f));
    e->send(make(Command::Play));
    for (int i = 0; i < 20; ++i) e->render(out.data(), 256);
    CHECK(e->modOffsetForTests(0, type) == 0.f && e->modOffsetForTests(0, cutoff) == 0.3f);
    e->setEffect(0, 0, EffectKind::Saturate);
    e->render(out.data(), 1);
    CHECK(e->laneCentre(0, type) == 0.8f);
    e->render(out.data(), 128);
    CHECK(e->modOffsetForTests(0, type) == 0.3f && e->modOffsetForTests(0, cutoff) == 0.3f);
    const int second = autoLaneOfEffect(0, 1);
    e->setMod(0, 1, lfo(second, 0.3f)); // SATURATE's TYPE: a choice
    for (int i = 0; i < 4; ++i) e->render(out.data(), 256);
    CHECK(e->modOffsetForTests(0, second) == 0.f);
    // FILTER again: there it is RESONANCE, an amount, and moved. SATURATE again: what was added
    // to RESONANCE is not carried on to TYPE.
    e->setEffect(0, 0, EffectKind::Filter);
    for (int i = 0; i < 4; ++i) e->render(out.data(), 256);
    CHECK(e->modOffsetForTests(0, second) == 0.3f);
    e->setEffect(0, 0, EffectKind::Saturate);
    for (int i = 0; i < 4; ++i) e->render(out.data(), 256);
    CHECK(e->modOffsetForTests(0, second) == 0.f && e->laneCentre(0, second) == 0.f);
}

// A track taken away: its MODs go OFF (as GLIDE FROM goes back to TOP), what they added comes off
// the effects and sends it keeps, and an instrument put there next runs none.
static void testATrackTakenAwayTurnsItsModsOff() {
    std::unique_ptr<Engine> e(new Engine);
    std::vector<float> out(1024);
    e->addSynthTrack(2, SynthKind::Phazed);
    e->render(out.data(), 64);
    e->send(make(Command::SetSend, 2, 0, 0, 0.3f));
    e->setMod(2, 0, lfo(autoLaneOfSend(0), 0.4f));
    e->setMod(2, 1, lfo(phazed::Tone, 0.2f, kModSine, kModRateHalfStep));
    e->send(make(Command::Play));
    for (int i = 0; i < 20; ++i) e->render(out.data(), 256);
    CHECK(e->sendGiven(2, 0) == 0.3f + 0.4f && e->modShown(2, 0) == 0.4f);
    CHECK(e->removeTrack(2));
    for (int i = 0; i < 4; ++i) e->render(out.data(), 256); // its 5 ms fade, if it sounded
    CHECK(e->sendGiven(2, 0) == 0.3f && e->modOffsetForTests(2, autoLaneOfSend(0)) == 0.f && e->modShown(2, 0) == 0.f && e->modShown(2, 1) == 0.f);
    CHECK(e->laneCentre(2, modDepthLane(0)) == 0.5f && e->laneCentre(2, modDepthLane(1)) == 0.5f); // their DEPTH lanes at no depth (batch 7)
    e->addSynthTrack(2, SynthKind::Phazed);
    for (int i = 0; i < 20; ++i) e->render(out.data(), 256);
    CHECK(e->modShown(2, 0) == 0.f && e->modShown(2, 1) == 0.f && e->modOffsetForTests(2, autoLaneOfSend(0)) == 0.f);
}

// ---- batch 7: lanes 22 and 23 are MOD 1's and MOD 2's DEPTH ------------------------------------

// A lane holds 0..1: 0.5 no depth, 0 -100%, 1 +100% (the song file's DEPTH scale).
static void testTheDepthScale() {
    CHECK(modDepthLane(0) == 22 && modDepthLane(1) == 23 && kModDepthLane == kModTargetLanes);
    CHECK(modDepthToLane(0.f) == 0.5f && modDepthToLane(-1.f) == 0.f && modDepthToLane(1.f) == 1.f && modDepthToLane(0.5f) == 0.75f);
    CHECK(modDepthToLane(3.f) == 1.f && modDepthToLane(-3.f) == 0.f && modDepthToLane(std::nanf("")) == 0.5f);
    CHECK(modDepthFromLane(0.5f) == 0.f && modDepthFromLane(0.f) == -1.f && modDepthFromLane(1.f) == 1.f && modDepthFromLane(0.75f) == 0.5f);
    CHECK(modDepthFromLane(2.f) == 1.f && modDepthFromLane(-1.f) == -1.f && modDepthFromLane(std::nanf("")) == 0.f);
    for (int p = -100; p <= 100; ++p) CHECK(near(modDepthFromLane(modDepthToLane(p / 100.f)), p / 100.0, 1e-6));
    // The engine runs a MOD at any depth (its lane may be written); the app's "on" still wants one.
    ModSettings m = lfo(autoLaneOfSend(0), 0.f);
    CHECK(m.runs() && !m.on() && modOffsetAt(m, 1.0, 1, 0) == 0.f && modOffsetAt(m, 0.0, 1, 0, 0.5f) == 0.5f);
    m.kind = kModOff;
    CHECK(!m.runs() && modOffsetAt(m, 0.0, 1, 0, 0.5f) == 0.f);
    m = lfo(kModTargetNone, 0.5f);
    CHECK(!m.runs());
    CHECK(modOffsetAt(lfo(0, 0.5f), 0.0, 1, 0, std::nanf("")) == 0.f && modOffsetAt(lfo(0, 0.5f), 0.0, 1, 0, 3.f) == 1.f);
}

// A scene's DEPTH lane moves its MOD's depth, as a macro's lane moves the macro: written, the depth
// is the lane's (gliding there in the first half of the step); a scene with nothing written goes
// back to the MOD's own DEPTH, as every lane goes back to its knob; Stop puts the knob back. MOD 1's
// lane is MOD 1's only.
static void testTheDepthLanes() {
    Rig rig;
    CHECK(rig.engine->laneCentre(2, modDepthLane(0)) == 0.5f && rig.engine->laneCentre(2, modDepthLane(1)) == 0.5f); // a new engine: at no depth
    rig.send(make(Command::SetSceneCount, 0, 2));
    const int echo = autoLaneOfSend(0), fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.1f));
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f)); // (slot 2's first setting: its centre told)
    rig.mod(0, lfo(echo, 0.f)); // MOD 1 at no depth: it wobbles only where its lane says (a SQUARE, 8 bars: +depth whole)
    rig.mod(1, lfo(fx, 0.2f));  // MOD 2 at +20%, its lane never written
    CHECK(rig.engine->laneCentre(1, modDepthLane(0)) == 0.5f && rig.engine->laneCentre(1, modDepthLane(1)) == modDepthToLane(0.2f));
    rig.write(modDepthLane(0), modDepthToLane(0.6f)); // scene 1, every step of its bar: +60%
    rig.send(make(Command::Play));
    rig.frames(3000 + 512); // past the glide
    CHECK(near(rig.engine->modShown(1, 0), 0.6, 1e-6) && near(rig.engine->sendGiven(1, 0), 0.7, 1e-6));
    CHECK(rig.engine->modShown(1, 1) == 0.2f && rig.lastParam(0) == 0.5f + 0.2f); // MOD 2 at its own, exactly
    CHECK(rig.engine->laneCentre(1, modDepthLane(0)) == modDepthToLane(0.6f));
    {   // Aimed elsewhere (one field, as the Session sends it) at 0% of its own and +60% from its lane:
        // what it added to the send goes at once.
        CHECK(rig.engine->send(make(Command::SetMod, 1, 0, kModFieldTarget, static_cast<float>(autoLaneOfSend(1)))));
        rig.frames(1);
        CHECK(rig.engine->sendGiven(1, 0) == 0.1f && rig.engine->modOffsetForTests(1, echo) == 0.f);
        CHECK(rig.engine->send(make(Command::SetMod, 1, 0, kModFieldTarget, static_cast<float>(echo))));
        rig.frames(512);
        CHECK(near(rig.engine->sendGiven(1, 0), 0.7, 1e-6));
    }
    rig.frames(16 * 6000 - 513); // scene 2's first step, past the glide: nothing written, MOD 1 back to no depth
    CHECK(rig.engine->playScene() == 1);
    CHECK(rig.engine->modShown(1, 0) == 0.f && rig.engine->sendGiven(1, 0) == 0.1f && rig.engine->modOffsetForTests(1, echo) == 0.f);
    CHECK(rig.engine->modShown(1, 1) == 0.2f);
    rig.frames(16 * 6000); // scene 1 again
    CHECK(rig.engine->playScene() == 0 && near(rig.engine->modShown(1, 0), 0.6, 1e-6));
    // The knob's own DEPTH turned while the lane is written: where it rests in scene 2 from now on.
    ModSettings deeper = lfo(echo, -0.3f);
    rig.mod(0, deeper);
    rig.frames(16 * 6000);
    CHECK(rig.engine->playScene() == 1 && near(rig.engine->modShown(1, 0), -0.3, 1e-6) && near(rig.engine->sendGiven(1, 0), 0.f, 1e-6));
    // MOD 2's lane written on scene 2's next step at -100%: MOD 2 turns over there, MOD 1 is not
    // moved by it. (Still in the SQUARE's first half: it is four bars long.)
    Command c = make(Command::SetAutomation, 1, modDepthLane(1), 1, 0.f);
    c.scene = 1;
    CHECK(rig.engine->send(c));
    rig.frames(6000);
    CHECK(rig.engine->playScene() == 1 && near(rig.engine->modShown(1, 1), -1.0, 1e-6) && near(rig.lastParam(0), 0.f, 1e-6));
    CHECK(near(rig.engine->modShown(1, 0), -0.3, 1e-6));
    // Stopped: every MOD let go, and the DEPTH lanes back on their knobs.
    rig.send(make(Command::Stop));
    CHECK(rig.engine->laneCentre(1, modDepthLane(0)) == modDepthToLane(-0.3f) && rig.engine->laneCentre(1, modDepthLane(1)) == modDepthToLane(0.2f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.1f && rig.engine->modShown(1, 0) == 0.f);
    // Heard while stopped (X held): at the knob's depth, not a scene's.
    rig.hear(3);
    rig.frames(512);
    CHECK(rig.engine->modShown(1, 0) == -0.3f && rig.engine->modShown(1, 1) == 0.2f);
    rig.hear(0);
}

// The knob's own DEPTH set while its lane glides (a written step, its first half): the glide stops
// and the knob's is heard until the next step, as a macro set by hand.
static void testTheDepthKnobStopsTheLanesGlide() {
    Rig rig;
    const int echo = autoLaneOfSend(0);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.1f));
    rig.mod(0, lfo(echo, 0.f));
    for (int step = 0; step < 2; ++step) {
        Command c = make(Command::SetAutomation, 1, modDepthLane(0), step, modDepthToLane(0.6f));
        c.scene = 0;
        CHECK(rig.engine->send(c));
    }
    rig.send(make(Command::Play));
    rig.frames(1000); // a sixth into the first step: gliding to +60%
    CHECK(rig.engine->playStep() == 0 && rig.engine->modShown(1, 0) > 0.f && rig.engine->modShown(1, 0) < 0.6f);
    CHECK(rig.engine->setMod(1, 0, lfo(echo, 0.1f)));
    rig.frames(3000); // past where the glide would have ended
    CHECK(rig.engine->playStep() == 0 && rig.engine->modShown(1, 0) == 0.1f);
    rig.frames(6000); // the next step: written, +60% again
    CHECK(rig.engine->playStep() == 1 && near(rig.engine->modShown(1, 0), 0.6, 1e-6));
}

// A MOD turned to no depth lets go at once, not at its next tick; it still runs (a written lane
// moves it), so the chunks are still cut while it plays.
static void testNoDepthLetsGoAtOnce() {
    Rig rig;
    const int echo = autoLaneOfSend(0);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.2f));
    rig.mod(0, lfo(echo, 0.4f));
    rig.send(make(Command::Play));
    rig.frames(512);
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f + 0.4f);
    CHECK(rig.engine->setMod(1, 0, lfo(echo, 0.f)));
    rig.frames(1); // (one sample: not to the next tick)
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f && rig.engine->modShown(1, 0) == 0.f && !std::isnan(rig.engine->modNow(1, 0)));
    // Still cut on the tick, as at 40% and more than with no MOD (blocks of 333, 120 BPM, a bar).
    auto chunks = [](float depth, bool any) {
        Rig r;
        if (any) r.mod(0, lfo(autoLaneOfSend(0), depth, kModSine, kModRateBar));
        r.send(make(Command::Play));
        const std::uint64_t before = r.engine->chunksRendered();
        r.frames(16 * 6000, 333);
        return r.engine->chunksRendered() - before;
    };
    const std::uint64_t none = chunks(0.f, false), zero = chunks(0.f, true), some = chunks(0.4f, true);
    CHECK(zero == some && zero > none);
}

// A scene put in with a DEPTH lane written in the scene it copies: the same, to the bit, as the
// song built with the scene in it (the lanes are the scene's, carried with its automation).
static void testASceneMoveCarriesTheDepthLane() {
    const int depthLane = modDepthLane(0);
    auto depth = [&](Engine& e, int scene) {
        for (int step = 0; step < 16; step += 2) {
            Command c = make(Command::SetAutomation, 0, depthLane, step, step % 4 ? 0.9f : 0.2f);
            c.scene = scene;
            e.send(c);
        }
    };
    std::unique_ptr<Engine> moved(new Engine), built(new Engine);
    std::vector<float> scratch(1024);
    for (Engine* e : {moved.get(), built.get()}) {
        e->prepare(48000.f);
        e->setEffect(0, 0, EffectKind::Saturate);
        e->setEffect(kEchoBus, 0, EffectKind::Ping);
        e->send(make(Command::SetSend, 0, 0, 0, 0.3f));
        const bool three = e == built.get();
        e->send(make(Command::SetSceneCount, 0, three ? 3 : 2));
        for (int step = 0; step < 16; step += 4) {
            for (int scene = 0; scene < (three ? 2 : 1); ++scene) {
                Command hit = make(Command::SetStep, 0, 0, step, 0.9f);
                hit.scene = scene;
                e->send(hit);
            }
        }
        depth(*e, 0);
        if (three) depth(*e, 1);
        e->render(scratch.data(), 256);
        e->setMod(0, 0, lfo(autoLaneOfSend(0), 0.1f, kModSine, kModRateHalf));
        e->render(scratch.data(), 256);
        e->send(make(Command::Play));
    }
    const int frames = 4 * 16 * 6000;
    std::vector<float> a(2 * frames), b(2 * frames);
    std::vector<float> shownA, shownB;
    int at = 0;
    const int moveAt = 6000 * 9 + 77;
    for (; at < moveAt; at += 256) {
        const int n = moveAt - at < 256 ? moveAt - at : 256;
        moved->render(a.data() + 2 * at, n);
        built->render(b.data() + 2 * at, n);
    }
    at = moveAt;
    std::unique_ptr<SceneImage> images[kNumTracks];
    images[0].reset(new SceneImage);
    for (int step = 0; step < 16; step += 4) images[0]->level[0][0][step] = 0.9f;
    for (int step = 0; step < 16; step += 2) images[0]->automation[depthLane * kAutoSteps + step] = step % 4 ? 0.9f : 0.2f;
    CHECK(moved->moveScenes(1, 1, images, 3, 1, 0));
    for (; at < frames; at += 256) {
        const int n = frames - at < 256 ? frames - at : 256;
        moved->render(a.data() + 2 * at, n);
        built->render(b.data() + 2 * at, n);
        shownA.push_back(moved->modShown(0, 0));
        shownB.push_back(built->modShown(0, 0));
    }
    CHECK(a == b && shownA == shownB);
    bool turned = false; // (the copy's lane did move it: a value only the written DEPTH gives)
    for (float v : shownA) turned = turned || std::fabs(v) > 0.1f + 1e-6f;
    CHECK(turned);
}

// ---- batch 8: LEVEL and PAN as targets ----------------------------------------------------------

// The rule: LEVEL times (1 + added), held to 0..2; PAN plus added, held to -1..1; nothing added
// (or not a number) gives the mixer's value exactly.
static void testTheMixRule() {
    CHECK(modLevelGiven(0.65f, 0.f) == 0.65f && modLevelGiven(0.65f, std::nanf("")) == 0.65f);
    CHECK(modLevelGiven(0.65f, 0.5f) == 0.65f * 1.5f && modLevelGiven(0.65f, -1.f) == 0.f && modLevelGiven(0.65f, 1.f) == 1.3f);
    CHECK(modLevelGiven(0.65f, 3.f) == 1.3f && modLevelGiven(0.65f, -3.f) == 0.f);
    CHECK(modPanGiven(0.2f, 0.f) == 0.2f && modPanGiven(0.2f, std::nanf("")) == 0.2f && modPanGiven(0.2f, -0.5f) == 0.2f - 0.5f);
    CHECK(modPanGiven(0.5f, 0.9f) == 1.f && modPanGiven(-0.5f, -0.9f) == -1.f);
}

// On the engine: given at the tick, about the mixer's own (which a SetTrackLevel or SetTrackPan
// moves under it), two on LEVEL add, and Stop, a MOD aimed elsewhere and a track taken away give
// the mixer's exactly again.
static void testLevelAndPan() {
    Rig rig;
    rig.send(make(Command::SetTrackLevel, 1, 0, 0, 0.65f));
    rig.send(make(Command::SetTrackPan, 1, 0, 0, 0.2f));
    float l = 0.f, r = 0.f, wantL = 0.f, wantR = 0.f;
    rig.mod(0, lfo(kModTargetLevel, 0.5f)); // a SQUARE: +depth whole
    rig.mod(1, lfo(kModTargetPan, -0.5f));
    CHECK(rig.engine->levelGiven(1) == 0.65f); // stopped: nothing runs
    rig.send(make(Command::Play));
    rig.frames(64);
    CHECK(rig.engine->levelGiven(1) == 0.65f * 1.5f);
    rig.engine->panGiven(1, l, r);
    panGains(0.2f - 0.5f, wantL, wantR);
    CHECK(l == wantL && r == wantR);
    // The mixer turned under it: about the new level at once.
    rig.send(make(Command::SetTrackLevel, 1, 0, 0, 0.4f));
    CHECK(rig.engine->levelGiven(1) == 0.4f * 1.5f);
    rig.send(make(Command::SetTrackPan, 1, 0, 0, -0.8f));
    rig.engine->panGiven(1, l, r);
    panGains(-1.f, wantL, wantR); // (-0.8 - 0.5, held)
    CHECK(l == wantL && r == wantR);
    // Two on LEVEL add: +50% and -100%, half the gain.
    rig.mod(1, lfo(kModTargetLevel, -1.f));
    rig.frames(64);
    CHECK(rig.engine->levelGiven(1) == 0.2f);
    rig.engine->panGiven(1, l, r);
    panGains(-0.8f, wantL, wantR); // (MOD 2 left PAN: the mixer's again)
    CHECK(l == wantL && r == wantR);
    // Stop: the mixer's exactly.
    rig.send(make(Command::Stop));
    CHECK(rig.engine->levelGiven(1) == 0.4f);
    rig.send(make(Command::Play));
    rig.frames(64);
    CHECK(rig.engine->levelGiven(1) == 0.2f);
    // MOD 2 turned to 0%: what both added goes at once (the mixer's), MOD 1's back at its next tick.
    CHECK(rig.engine->setMod(1, 1, lfo(kModTargetLevel, 0.f)));
    rig.frames(1);
    CHECK(rig.engine->levelGiven(1) == 0.4f);
    rig.frames(64);
    CHECK(rig.engine->levelGiven(1) == 0.4f * 1.5f);
    // Held at the mixer's most (4) however far a MOD takes it.
    rig.send(make(Command::SetTrackLevel, 1, 0, 0, 3.f));
    CHECK(rig.engine->setMod(1, 1, lfo(kModTargetLevel, 1.f)));
    rig.frames(64);
    CHECK(rig.engine->levelGiven(1) == 4.f);
    rig.send(make(Command::SetTrackLevel, 1, 0, 0, 0.4f));
    // A track taken away: its usual level and the middle, nothing added, when it comes back; and the
    // mixer's own forgotten with it (the next level or pan sent does not bring the old other back).
    rig.send(make(Command::RemoveInstrument, 1));
    rig.frames(512);
    CHECK(rig.engine->levelGiven(1) == kUsualTrackLevel);
    rig.engine->panGiven(1, l, r);
    CHECK(l == 1.f && r == 1.f);
    rig.send(make(Command::SetTrackLevel, 1, 0, 0, 0.5f));
    rig.engine->panGiven(1, l, r);
    CHECK(l == 1.f && r == 1.f && rig.engine->levelGiven(1) == 0.5f);
    rig.send(make(Command::RemoveInstrument, 1));
    rig.frames(512);
    rig.send(make(Command::SetTrackPan, 1, 0, 0, 0.f));
    CHECK(rig.engine->levelGiven(1) == kUsualTrackLevel);
}

// What is heard: a drum track under a SQUARE on LEVEL at -100% is silent once the level has glided
// down (5 ms); on PAN at -100% from the middle its right side is silent. The same in any blocks
// and on helpers (the tick is on the step's grid, the glide a sample at a time).
static std::vector<float> mixSong(int block, int helpers, float levelDepth, float panDepth, int frames) {
    std::unique_ptr<Engine> e(new Engine);
    e->prepare(48000.f);
    e->useWorkers(helpers);
    for (int step = 0; step < 16; ++step) e->send(make(Command::SetStep, 0, step % 3, step, 0.9f));
    std::vector<float> out(2 * static_cast<std::size_t>(frames)), scratch(1024);
    e->render(scratch.data(), 256);
    ModSettings level = lfo(kModTargetLevel, levelDepth, kModSine, kModRateQuarter), pan = lfo(kModTargetPan, panDepth, kModSquare, kModRateEightBars);
    if (levelDepth != 0.f) e->setMod(0, 0, level);
    if (panDepth != 0.f) e->setMod(0, 1, pan);
    e->render(scratch.data(), 256);
    e->send(make(Command::Play));
    for (int at = 0; at < frames; at += block) e->render(out.data() + 2 * at, frames - at < block ? frames - at : block);
    e->useWorkers(0);
    return out;
}
static void testLevelAndPanHeard() {
    const int frames = 16 * 6000;
    const std::vector<float> plain = mixSong(64, 0, 0.f, 0.f, frames);
    // PAN -100% from the middle: hard left, the right side nothing once the pan has glided there.
    const std::vector<float> left = mixSong(64, 0, 0.f, -1.f, frames);
    double rightMost = 0.0, leftMost = 0.0, plainMost = 0.0;
    for (int i = 1000; i < frames; ++i) {
        rightMost = std::max(rightMost, static_cast<double>(std::fabs(left[2 * i + 1])));
        leftMost = std::max(leftMost, static_cast<double>(std::fabs(left[2 * i])));
        plainMost = std::max(plainMost, static_cast<double>(std::fabs(plain[2 * i])));
    }
    CHECK(rightMost == 0.0 && leftMost > plainMost && plainMost > 0.01);
    // LEVEL under a SINE at 100%, a quarter note: loud and quiet by turns, never louder than twice.
    const std::vector<float> wobble = mixSong(64, 0, 1.f, 0.f, frames);
    CHECK(wobble != plain);
    double most = 0.0;
    for (float v : wobble) most = std::max(most, static_cast<double>(std::fabs(v)));
    CHECK(most > plainMost && most <= 2.0 * plainMost + 1e-6);
    // The same in any blocks and on helpers.
    for (int block : {1, 7, 333}) CHECK(mixSong(block, 0, 1.f, -1.f, frames) == mixSong(64, 0, 1.f, -1.f, frames));
    CHECK(mixSong(256, 3, 1.f, -1.f, frames) == mixSong(64, 0, 1.f, -1.f, frames));
}

// Stopped, X held on a modulated knob: its MODs run from the moment X went down, as from Play, and
// the knob is given its centre when X is let go. Play and Stop let go too; while it plays the
// song's MODs run and an audition is not taken.
static void testHeardWhileStopped() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f));
    const ModSettings ramp = lfo(fx, 0.5f, kModRampUp, kModRateHalfStep);
    rig.mod(0, ramp);
    rig.params.clear();
    rig.frames(1000);
    CHECK(rig.params.empty());
    rig.hear(1);
    CHECK(rig.lastParam(0) == 0.f); // the ramp's start, at the moment X went down
    rig.frames(1499);               // 1500 since: the tick at 1472
    CHECK(rig.lastParam(0) == modGiven(0.5f, modOffsetAt(ramp, 1472.0 / 6000.0, 1, 0)));
    rig.hear(0);
    CHECK(rig.lastParam(0) == 0.5f && rig.engine->modShown(1, 0) == 0.f);
    rig.params.clear();
    rig.frames(2000);
    CHECK(rig.params.empty());
    // Only the MODs asked for run.
    rig.mod(1, lfo(autoLaneOfSend(0), 0.3f));
    rig.hear(2);
    CHECK(rig.params.empty() && rig.engine->sendGiven(1, 0) == 0.3f);
    // Play lets go; so does Stop, and an audition sent while it plays is not taken.
    rig.mod(0, lfo(fx, 0.3f)); // (a SQUARE: nothing given while it plays, unless something stills it)
    rig.hear(3);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(3000);
    rig.params.clear();
    rig.send(make(Command::ModAudition, 1, 3)); // not taken: nothing is put back to its centre for a tick
    rig.frames(3000);
    CHECK(rig.params.empty());
    rig.send(make(Command::Stop));
    rig.params.clear();
    rig.frames(3000);
    CHECK(rig.params.empty() && rig.engine->sendGiven(1, 0) == 0.f && rig.engine->modShown(1, 0) == 0.f);
}

// ---- FREE: a free LFO's place is seconds since Play ----------------------------------------------

static ModSettings freeLfo(int target, float depth, int shape, float hz) {
    ModSettings m = lfo(target, depth, shape, kModRateFree);
    m.speed = hz;
    return m;
}

// Its position is samples since Play over the sample rate, a whole number of samples at every tick,
// and its value the rule's there. At 120 a free 0.5 Hz SINE is a synced 1 BAR one, tick for tick; a
// tempo change moves the synced one and leaves the free one where seconds have it.
static void testAFreeLfoCountsSeconds() {
    for (float to : {90.f, 171.f}) {
        Rig rig;
        const ModSettings free = freeLfo(autoLaneOfEffect(1, 0), 0.5f, kModSine, 0.5f), synced = lfo(autoLaneOfSend(0), 0.5f, kModSine, kModRateBar);
        rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f));
        rig.mod(0, free);
        rig.mod(1, synced);
        rig.send(make(Command::Play, 0, 0, 1));
        long since = 1; // samples since Play (rig.send renders one)
        int ticks = 0, same = 0, wrong = 0, apart = 0;
        double before = -1.0;
        for (; since < 48000L * 6; since += 16) {
            if (since == 48000L * 2 + 1) rig.send(make(Command::SetTempo, 0, 0, 0, to)), ++since; // at two seconds
            const double at = rig.engine->modNow(1, 0), steps = rig.engine->modNow(1, 1);
            const double samples = at * 48000.0;
            wrong += !(std::fabs(samples - std::round(samples)) < 1e-6 && samples <= since && samples > since - 64 - 16); // the last tick: a whole sample, at most 64 back
            wrong += rig.engine->modShown(1, 0) != modOffsetAt(free, at, 1, 0);
            if (at != before) {
                ++ticks;
                if (since < 48000L * 2) same += near(rig.engine->modShown(1, 0), rig.engine->modShown(1, 1), 1e-5) && near(steps, at * 8.0, 1e-9);
                else apart += !near(steps, at * 8.0, 1e-3); // the synced one counts steps: from the change on, not seconds x 8
            }
            before = at;
            rig.frames(16, 16);
        }
        CHECK(wrong == 0 && ticks > 4000);
        CHECK(same > 1400 && apart > 2500);
        if (measuring) std::printf("free LFO, tempo to %.0f at 2 s: %d ticks, %d the synced one's before, %d apart after, %d wrong\n", to, ticks, same, apart, wrong);
    }
}

// The same to the bit cut into any blocks and on any helpers (a song whose MODs are free: its tick
// is the step's, its place whole samples), and Play twice gives the same.
static void testAFreeLfoInAnyBlocks() {
    const int whole = 2 * 16 * 6000;
    const std::vector<float> plain = song(64, 0, true, whole, true, false, 120.f, true);
    CHECK(plain != song(64, 0, true, whole, true, false, 120.f)); // (FREE does something)
    CHECK(song(1, 0, true, whole, true, false, 120.f, true) == plain && song(333, 3, true, whole, true, false, 120.f, true) == plain);
    const int frames = 2 * 16 * 5414; // 133: the drums alone, as testTheSameInAnyBlocks
    const std::vector<float> drums = song(64, 0, true, frames, false, false, 133.f, true);
    CHECK(song(1, 0, true, frames, false, false, 133.f, true) == drums && song(512, 3, true, frames, false, false, 133.f, true) == drums);
    Rig rig;
    rig.send(make(Command::SetEffectParam, 1, 1, 3, 0.5f));
    rig.mod(0, freeLfo(4, 0.6f, kModRandom, 3.f));
    rig.mod(1, freeLfo(autoLaneOfEffect(1, 3), 0.7f, kModDrift, 0.8f));
    std::vector<Given> macros[2], params[2];
    for (int round = 0; round < 2; ++round) {
        rig.macros.clear();
        rig.params.clear();
        rig.send(make(Command::Play, 0, 0, 1));
        if (round == 1) rig.send(make(Command::SetTempo, 0, 0, 0, 120.f)); // (told again: nothing moves)
        rig.frames(6000 * 40, round == 0 ? 512 : 333);
        macros[round] = rig.macros;
        params[round] = rig.params;
        rig.send(make(Command::Stop));
    }
    CHECK(macros[0].size() > 10 && params[0].size() > 50);
    CHECK(macros[0] == macros[1] && params[0] == params[1]);
}

// Stopped, X held: from the moment X went down, in seconds.
static void testAFreeLfoHeardWhileStopped() {
    Rig rig;
    const int fx = autoLaneOfEffect(1, 0);
    rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f));
    const ModSettings ramp = freeLfo(fx, 0.5f, kModRampUp, 13.f);
    rig.mod(0, ramp);
    rig.frames(1000);
    rig.hear(1);
    CHECK(rig.lastParam(0) == 0.f && rig.engine->modNow(1, 0) == 0.0); // the ramp's start, at the moment X went down
    rig.frames(1499);                                                     // 1500 since: the tick at 1472
    CHECK(rig.engine->modNow(1, 0) == 1472.0 / 48000.0 && rig.lastParam(0) == modGiven(0.5f, modOffsetAt(ramp, 1472.0 / 48000.0, 1, 0)));
    rig.send(make(Command::SetTempo, 0, 0, 0, 77.f)); // stopped, the tempo turned: the free one does not care
    rig.frames(999);
    CHECK(rig.engine->modNow(1, 0) == 2496.0 / 48000.0 && rig.lastParam(0) == modGiven(0.5f, modOffsetAt(ramp, 2496.0 / 48000.0, 1, 0)));
}

// The old FILTER takes its CUTOFF in stairs, a tick apart (its arithmetic is pinned: no glide may
// be added). The largest stair a SINE at depth 50% makes, at each rate at 120 BPM, is held to
// depth x 2 pi x 64 / the cycle's samples. Nobody has listened to it.
static void testTheOldFiltersStairs() {
    for (int rate = 0; rate <= kModRateBar; ++rate) {
        Rig rig;
        const int fx = autoLaneOfEffect(1, 0);
        rig.send(make(Command::SetEffectParam, 1, 1, 0, 0.5f));
        rig.mod(0, lfo(fx, 0.5f, kModSine, rate));
        rig.params.clear();
        rig.send(make(Command::Play, 0, 0, 1));
        const double cycle = modRateSteps(rate) * 6000.0;
        rig.frames(static_cast<int>(cycle) + 6000);
        double most = 0.0;
        for (std::size_t i = 1; i < rig.params.size(); ++i) most = std::max(most, std::fabs(static_cast<double>(rig.params[i].value) - rig.params[i - 1].value));
        const double bound = 0.5 * 2.0 * kPi * kModTick / cycle;
        CHECK(most <= bound * 1.0001 + 1e-6 && most >= bound * 0.98);
        if (measuring) std::printf("old FILTER CUTOFF, SINE 50%% at %-9s largest stair %.4f of the knob's travel (bound %.4f)\n", modRateInfo(rate).name, most, bound);
    }
}

// A newer effect takes a setting through its own 20 to 30 ms Glide, a block (here a tick) at a
// time: a fast MOD loses depth. What is kept of a SINE's swing, at 120 BPM, a 64-sample tick:
// measured here, the figures in the plan. A 1 BAR SINE keeps all but a hair; a 1/2 STEP loses most.
static double keptThroughAGlide(int rate, float seconds, float hz = 0.f) {
    ModSettings m = lfo(8, 1.f, kModSine, rate);
    if (hz > 0.f) m.rate = kModRateFree, m.speed = hz; // FREE: its position in seconds (6000 samples a step at 120: 8 steps a second)
    fx::Glide g;
    g.snap();
    const double cycle = hz > 0.f ? 48000.0 / hz : modRateSteps(rate) * 6000.0;
    double lo = 1e9, hi = -1e9;
    const long settle = static_cast<long>(4.0 * 48000.0), total = settle + static_cast<long>(2.0 * cycle) + 64;
    for (long at = 0; at < total; at += kModTick) {
        g.to = modOffsetAt(m, static_cast<double>(at) / (hz > 0.f ? 48000.0 : 6000.0), 1, 0);
        g.begin(kModTick, fx::glideK(kModTick, seconds, 48000.f));
        for (int i = 0; i < kModTick; ++i) {
            const float v = g.next();
            if (at >= settle) lo = std::min(lo, static_cast<double>(v)), hi = std::max(hi, static_cast<double>(v));
        }
        g.done();
    }
    return (hi - lo) / 2.0;
}
static void testANewerEffectSoftensAFastMod() {
    double last = 0.0;
    for (int rate = 0; rate < kModTempoRates; ++rate) {
        const double kept20 = keptThroughAGlide(rate, 0.02f), kept30 = keptThroughAGlide(rate, 0.03f);
        CHECK(kept30 <= kept20 + 1e-9 && kept20 >= last - 1e-9); // slower kept more; a longer glide less
        last = kept20;
        if (measuring) std::printf("a newer effect's Glide, SINE at %-9s keeps %5.1f%% (20 ms) %5.1f%% (30 ms)\n", modRateInfo(rate).name, 100.0 * kept20, 100.0 * kept30);
    }
    CHECK(keptThroughAGlide(kModRateBar, 0.03f) > 0.98);
    CHECK(keptThroughAGlide(kModRateHalfStep, 0.03f) < 0.6);
    // FREE: 16 Hz is 1/2 STEP at 120, to the bit; up to 20 Hz, faster keeps less (README's figures).
    CHECK(keptThroughAGlide(0, 0.02f, 16.f) == keptThroughAGlide(kModRateHalfStep, 0.02f));
    last = 1.0;
    for (float hz : {1.f, 2.f, 5.f, 10.f, 20.f}) {
        const double kept20 = keptThroughAGlide(0, 0.02f, hz), kept30 = keptThroughAGlide(0, 0.03f, hz);
        CHECK(kept30 <= kept20 + 1e-9 && kept20 <= last + 1e-9);
        last = kept20;
        if (measuring) std::printf("a newer effect's Glide, SINE FREE at %4.1f Hz keeps %5.1f%% (20 ms) %5.1f%% (30 ms)\n", static_cast<double>(hz), 100.0 * kept20, 100.0 * kept30);
    }
    CHECK(last < 0.4);
}

// A preset chosen while a MOD runs on a macro (review, 2026-11-14): the synth puts every macro back
// to the middle (Instrument::selectPreset), and so does the app's project. That is the centre now:
// the MOD is given about it at once, not left out until its offset changes (a SQUARE eight bars
// long: bars of nothing) and not moved about the knob the old sound had. With no MOD, no macro is
// given: the synth already has the middle.
static void testAPresetUnderAModMovesAboutTheMiddle() {
    Rig rig;
    rig.send(make(Command::SetMacro, 1, 0, 0, 0.2f));
    rig.send(make(Command::SetMacro, 1, 1, 0, 0.9f));
    rig.mod(0, lfo(0, 0.3f));
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000);
    CHECK(near(rig.synth->now[0], 0.2f + 0.3f, 1e-6));
    rig.macros.clear();
    rig.send(make(Command::SelectPreset, 1, 3));
    CHECK(rig.synth->now[0] == modGiven(0.5f, 0.3f) && rig.engine->laneCentre(1, 0) == 0.5f);
    CHECK(rig.count(rig.macros, 0) == 1 && rig.count(rig.macros, 1) == 0); // macro 1 has no MOD: left at the preset's middle
    CHECK(rig.synth->now[1] == 0.5f && rig.engine->laneCentre(1, 1) == 0.5f);
    rig.frames(6000 * 32);
    CHECK(rig.synth->now[0] == modGiven(0.5f, 0.3f));
    rig.send(make(Command::Stop)); // the centre the preset left, not the old knob
    CHECK(rig.synth->now[0] == 0.5f && rig.synth->now[1] == 0.5f);
    // A STEPPER: after the preset, each value about the middle.
    ModSettings st = stepper(0, 0.25f, kModRateStep, 0.f);
    rig.send(make(Command::SetMacro, 1, 0, 0, 0.2f));
    rig.mod(0, st);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000 * 2 + 100);
    rig.send(make(Command::SelectPreset, 1, 4));
    for (int step = 0; step < 4; ++step) {
        rig.frames(3000);
        CHECK(near(rig.synth->now[0], modGiven(0.5f, rig.engine->modShown(1, 0)), 1e-6));
        rig.frames(3000);
    }
    // No MOD: a preset gives no macro at all.
    rig.mod(0, ModSettings());
    rig.macros.clear();
    rig.send(make(Command::SelectPreset, 1, 5));
    rig.frames(6000);
    CHECK(rig.macros.empty());
}

// A track taken away has its MODs OFF, and when it held the last MOD on, the song is cut as one
// with none again: chunk for chunk as an engine that never had one.
static void testTheLastModTakenAwayStopsTheCuts() {
    auto chunks = [](bool modded) {
        std::unique_ptr<Engine> e(new Engine);
        std::vector<float> out(1024);
        e->addSynthTrack(2, SynthKind::Phazed);
        e->render(out.data(), 64);
        if (modded) e->setMod(2, 0, lfo(autoLaneOfSend(0), 0.4f, kModSine, kModRateBar));
        e->send(make(Command::Play));
        for (int i = 0; i < 20; ++i) e->render(out.data(), 256);
        CHECK(e->removeTrack(2));
        for (int i = 0; i < 4; ++i) e->render(out.data(), 256);
        const std::uint64_t before = e->chunksRendered();
        for (int i = 0; i < 200; ++i) e->render(out.data(), 256);
        return e->chunksRendered() - before;
    };
    CHECK(chunks(true) == chunks(false));
}

// Stopped, the chunks are cut as they always were, whatever was heard before: with a MOD on, after
// an audition let go (X up, or Stop) part way into a tick, and with the audition's track taken away
// while X was held.
static void testStoppedIsCutAsAlways() {
    for (int way = 0; way < 3; ++way) {
        Rig rig;
        rig.mod(0, lfo(autoLaneOfSend(0), 0.4f, kModSine, kModRateBar));
        rig.hear(1);
        rig.frames(100);
        if (way == 0) rig.hear(0);
        else if (way == 1) rig.send(make(Command::Stop)); // X still down: Stop lets go, part way into a tick
        else {
            rig.send(make(Command::RemoveInstrument, 1)); // (the rig put its synth in by hand: removeTrack does not know it)
            rig.frames(512);
        }
        const std::uint64_t before = rig.engine->chunksRendered();
        rig.frames(6400, 64);
        CHECK(rig.engine->chunksRendered() - before == 100);
    }
}

// A MOD told to a track with no instrument does nothing: nothing worked out, nothing shown.
static void testATrackWithNothingRunsNoMod() {
    Rig rig;
    rig.mod(0, lfo(autoLaneOfSend(0), 0.4f), 5);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000 * 2);
    CHECK(rig.engine->modShown(5, 0) == 0.f && rig.engine->modOffsetForTests(5, autoLaneOfSend(0)) == 0.f && rig.engine->modShown(1, 0) == 0.f);
}

// The join with main (2026-11-15): FEEL on the drums, chord effects on a JAX-EP and MODs, all in one
// song, the same to the bit in blocks of 1, 64 and 333 and on 0 or 3 helpers. At 120 BPM, where a
// step is a whole number of samples (6000; the rule above). The MODs are on what is exact under a
// MOD: a JAX-EP macro, the old FILTER's CUTOFF, a send to PING (whose own settings stay at rest).
static std::vector<float> feelChordsMods(int block, int helpers, bool feel, bool chords, bool mods, int frames) {
    std::unique_ptr<Engine> e(new Engine);
    e->prepare(48000.f);
    std::vector<float> out(2 * static_cast<std::size_t>(frames)), scratch(1024);
    e->send(make(Command::SetTempo, 0, 0, 0, 120.f));
    e->addSynthTrack(2, SynthKind::Jx3p);
    e->render(scratch.data(), 64);
    e->setEffect(0, 0, EffectKind::Filter);
    e->setEffect(kEchoBus, 0, EffectKind::Ping);
    for (int lane = 0; lane < 4; ++lane)
        for (int step = lane % 2; step < 16; step += 2) e->send(make(Command::SetStep, 0, lane, step, 0.8f));
    if (feel)
        for (int lane = 0; lane < 4; ++lane) {
            Command c = make(Command::SetLaneFeel, 0, lane, lane == 3 ? 0 : 100); // lane 4: HUMAN LEVEL alone
            c.c = lane == 2 ? 0 : 70;                                             // lane 3: HUMAN TIME alone
            e->send(c);
        }
    // Two chords of three keys, four steps long, and a plain note; chord effects on both chords.
    const int keys[2][3] = {{48, 52, 55}, {50, 53, 57}};
    const ChordPlay plays[2] = {chordPlayOf(kStrumWide, kOrderRandom, 100, 100, 60), chordPlayOf(kStrumQuarter, kOrderUp, 50, 50, 0)};
    for (int chord = 0; chord < 2; ++chord)
        for (int k = 0; k < 3; ++k) {
            Command c = make(Command::SetNote, 2, chord * 3 + k, chord * 8, 0.8f);
            c.c = keys[chord][k];
            c.d = 4;
            c.e = chord == 0 ? 3 : 0; // the first chord ratcheted x3
            if (chords) c.play = plays[chord];
            c.play.index = static_cast<std::uint8_t>(k);
            c.play.count = 3;
            e->send(c);
        }
    Command n = make(Command::SetNote, 2, 6, 13, 0.8f);
    n.c = 60;
    n.d = 2;
    e->send(n);
    e->send(make(Command::SetSend, 0, 0, 0, 0.3f));
    e->send(make(Command::SetSend, 2, 0, 0, 0.2f));
    e->render(scratch.data(), 256);
    if (mods) {
        e->setMod(0, 0, lfo(autoLaneOfEffect(0, 0), 0.3f, kModSine, kModRateEighth)); // the old FILTER's CUTOFF
        e->setMod(0, 1, stepper(autoLaneOfSend(0), 0.4f, kModRateQuarter, 0.5f, 0.3f, 6));
        e->setMod(2, 0, lfo(1, 0.4f, kModTriangle, kModRateHalfStep)); // a JAX-EP macro
        e->setMod(2, 1, lfo(autoLaneOfSend(0), 0.3f, kModDrift, kModRateQuarterTriplet));
    }
    e->render(scratch.data(), 256);
    if (helpers) e->useWorkers(helpers);
    e->send(make(Command::Play, 0, 0, 1));
    for (int at = 0; at < frames; at += block) e->render(out.data() + 2 * at, frames - at < block ? frames - at : block);
    return out;
}

static void testFeelChordsAndModsInAnyBlocks() {
    const int frames = 2 * 16 * 6000; // two bars at 120
    const std::vector<float> plain = feelChordsMods(64, 0, true, true, true, frames);
    for (int helpers : {0, 3})
        for (int block : {1, 64, 333}) {
            if (block == 64 && helpers == 0) continue;
            const bool same = feelChordsMods(block, helpers, true, true, true, frames) == plain;
            CHECK(same);
            if (!same) std::printf("  FEEL, chords and MODs: blocks of %d on %d helpers differ\n", block, helpers);
        }
    // Each of the three does something here.
    CHECK(feelChordsMods(64, 0, false, true, true, frames) != plain);
    CHECK(feelChordsMods(64, 0, true, false, true, frames) != plain);
    CHECK(feelChordsMods(64, 0, true, true, false, frames) != plain);
    double peak = 0.0;
    for (float v : plain) peak = std::max(peak, static_cast<double>(std::fabs(v)));
    CHECK(peak > 0.05 && peak < 4.0);
}

// AT END STOP (main's) ends the song as Stop does, and so lets go of every MOD: the knob is given
// its centre, nothing is added, nothing shown.
static void testASongThatStopsAtItsEndLetsGoOfTheMods() {
    Rig rig;
    const int echo = autoLaneOfSend(0);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.2f));
    rig.send(make(Command::SetMacro, 1, 2, 0, 0.4f));
    rig.mod(0, lfo(echo, 0.3f));
    rig.mod(1, lfo(2, 0.25f));
    rig.send(make(Command::SetSongEnd, 0, 1));
    rig.send(make(Command::Play));
    rig.frames(4000);
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f + 0.3f && rig.lastMacro(2) == 0.65f && rig.engine->modShown(1, 0) == 0.3f);
    int samples = 0;
    while (rig.engine->playStep() != -1 && samples < 48000 * 20) rig.frames(256, 256), samples += 256;
    CHECK(rig.engine->playStep() == -1 && rig.engine->songEnded() != 0);
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f && rig.lastMacro(2) == 0.4f);
    CHECK(rig.engine->modOffsetForTests(1, echo) == 0.f && rig.engine->modOffsetForTests(1, 2) == 0.f);
    CHECK(rig.engine->modShown(1, 0) == 0.f && rig.engine->modShown(1, 1) == 0.f);
}

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheNumbers", testTheNumbers},
        {"testThePosition", testThePosition},
        {"testTheShapes", testTheShapes},
        {"testRandomAndDrift", testRandomAndDrift},
        {"testTakes", testTakes},
        {"testTheFreeRule", testTheFreeRule},
        {"testTheStepper", testTheStepper},
        {"testTheCombine", testTheCombine},
        {"testTheFields", testTheFields},
        {"testAKnobGoesOutThroughTheOneWay", testAKnobGoesOutThroughTheOneWay},
        {"testAnUnknownCentreIsLeftAlone", testAnUnknownCentreIsLeftAlone},
        {"testAutomationMovesTheCentre", testAutomationMovesTheCentre},
        {"testTheTick", testTheTick},
        {"testTwoOnOneSettingAdd", testTwoOnOneSettingAdd},
        {"testPlayTwiceGivesTheSame", testPlayTwiceGivesTheSame},
        {"testATempoChangeKeepsThePhase", testATempoChangeKeepsThePhase},
        {"testTheSameInAnyBlocks", testTheSameInAnyBlocks},
        {"testAGlidingEffectUnderAModIsCloseInOtherBlocks", testAGlidingEffectUnderAModIsCloseInOtherBlocks},
        {"testChunksAreCutOnlyWhileAModRuns", testChunksAreCutOnlyWhileAModRuns},
        {"testASceneMoveEqualsTheSongBuilt", testASceneMoveEqualsTheSongBuilt},
        {"testABounceAndALiveLoop", testABounceAndALiveLoop},
        {"testAHeldSlotWaits", testAHeldSlotWaits},
        {"testAChoiceIsNotMoved", testAChoiceIsNotMoved},
        {"testATrackTakenAwayTurnsItsModsOff", testATrackTakenAwayTurnsItsModsOff},
        {"testHeardWhileStopped", testHeardWhileStopped},
        {"testTheOldFiltersStairs", testTheOldFiltersStairs},
        {"testANewerEffectSoftensAFastMod", testANewerEffectSoftensAFastMod},
        {"testAPresetUnderAModMovesAboutTheMiddle", testAPresetUnderAModMovesAboutTheMiddle},
        {"testTheLastModTakenAwayStopsTheCuts", testTheLastModTakenAwayStopsTheCuts},
        {"testStoppedIsCutAsAlways", testStoppedIsCutAsAlways},
        {"testATrackWithNothingRunsNoMod", testATrackWithNothingRunsNoMod},
        {"testFeelChordsAndModsInAnyBlocks", testFeelChordsAndModsInAnyBlocks},
        {"testASongThatStopsAtItsEndLetsGoOfTheMods", testASongThatStopsAtItsEndLetsGoOfTheMods},
        {"testTheDepthScale", testTheDepthScale},
        {"testTheMixRule", testTheMixRule},
        {"testLevelAndPan", testLevelAndPan},
        {"testLevelAndPanHeard", testLevelAndPanHeard},
        {"testTheDepthLanes", testTheDepthLanes},
        {"testTheDepthKnobStopsTheLanesGlide", testTheDepthKnobStopsTheLanesGlide},
        {"testNoDepthLetsGoAtOnce", testNoDepthLetsGoAtOnce},
        {"testASceneMoveCarriesTheDepthLane", testASceneMoveCarriesTheDepthLane},
        {"testAFreeLfoCountsSeconds", testAFreeLfoCountsSeconds},
        {"testAFreeLfoInAnyBlocks", testAFreeLfoInAnyBlocks},
        {"testAFreeLfoHeardWhileStopped", testAFreeLfoHeardWhileStopped},
    };
    int first = 1;
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) measuring = true, first = 2;
    const bool only = argc > first + 1 && std::strcmp(argv[first], "--only") == 0;
    for (const Test& t : tests) {
        bool wanted = !only;
        for (int i = first + 1; only && i < argc; ++i) wanted = wanted || std::strcmp(argv[i], t.name) == 0;
        if (wanted) t.run();
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mod tests passed\n");
    return 0;
}
