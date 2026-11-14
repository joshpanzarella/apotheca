// MOD's groundwork (docs/plans/modulation-plan.md, batch 0): the rule (mod_rule.hpp) against
// values worked out by hand, and the one way a lane's value goes out of the engine (giveLane),
// with a MOD's offset put in by hand: no MOD runs yet.
//   mod_test                 every check
//   mod_test --only NAME...  just those tests

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
    // Targets: the automation lanes. 22 and 23 are spare and kept for DEPTH; LEVEL and PAN later.
    CHECK(kModFirstEffect == 8 && kModFirstSend == kAutoSendLane && kModTargetLanes == 22 && kAutoLanes == 24);
    for (int t = -1; t < 40; ++t) CHECK(modTargetKnown(t) == (t >= 0 && t <= 21));
    CHECK(modTargetIsMacro(0) && modTargetIsMacro(7) && !modTargetIsMacro(8));
    CHECK(modTargetIsEffect(8) && modTargetIsEffect(19) && !modTargetIsEffect(20) && modTargetIsSend(20) && modTargetIsSend(21) && !modTargetIsSend(22));
    for (int slot = 0; slot < 3; ++slot)
        for (int setting = 0; setting < 4; ++setting) {
            const int t = autoLaneOfEffect(slot, setting);
            CHECK(modTargetSlot(t) == slot && modTargetSetting(t) == setting);
        }
    CHECK(modTargetSlot(5) == -1 && modTargetSetting(20) == -1 && kModTargetLevel == 32 && kModTargetPan == 33);
    // Rates, in steps (a sixteenth), by number.
    const double steps[kModRates] = {0.5, 1.0, 4.0 / 3.0, 2.0, 8.0 / 3.0, 4.0, 6.0, 8.0, 16.0, 32.0, 64.0, 128.0};
    const char* names[kModRates] = {"1/2 STEP", "1 STEP", "1/8T", "1/8", "1/4T", "1/4", "1/4.", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
    CHECK(kModRates == 12);
    for (int r = 0; r < kModRates; ++r) {
        CHECK(modRateSteps(r) == steps[r]);
        CHECK(std::string(modRateInfo(r).name) == names[r]);
        CHECK(modRateAt(modRatePlace(r)) == r);
    }
    CHECK(modRate(12) == kModRateBar && modRate(-1) == kModRateBar && kModRateUsual == kModRateBar);
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

// ---- the engine: one way out for a lane, with an offset put in by hand ----------------------------

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
// A synth and an effect that make no sound and write down what they are given.
struct Given {
    int what;
    float value;
};
struct SpySynth : Instrument {
    std::vector<Given>* log;
    explicit SpySynth(std::vector<Given>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int macro, float value) override { log->push_back({macro, value}); }
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
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<Given> macros, params;
    Rig() {
        macros.reserve(10000);
        params.reserve(10000);
        engine->prepare(48000.f);
        Command c = make(Command::InstallSynth, 1);
        c.pointer = new SpySynth(&macros);
        CHECK(engine->send(c));
        frames(64);
        engine->setTrackEffect(1, 1, std::unique_ptr<Effect>(new SpyEffect(&params)));
        macros.clear();
        params.clear();
    }
    void frames(int n) {
        std::vector<float> out(2 * 512);
        for (; n > 0; n -= 512) engine->render(out.data(), n < 512 ? n : 512);
    }
    void send(const Command& c) {
        CHECK(engine->send(c));
        frames(1);
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
};

// A knob's own command (SetMacro, SetEffectParam, SetSend): with nothing added, the value goes out
// as it is (out of 0..1 too, as before there were MODs); with an offset, centre plus offset held
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

    // With an offset: given at once about the centre, which stays.
    rig.engine->setModOffsetForTests(1, 2, 0.25f);
    CHECK(rig.lastMacro(2) == 1.f && rig.engine->laneCentre(1, 2) == 1.6f);
    rig.send(make(Command::SetMacro, 1, 2, 0, 0.4f));
    CHECK(rig.lastMacro(2) == 0.65f && rig.engine->laneCentre(1, 2) == 0.4f);
    rig.engine->setModOffsetForTests(1, 2, -0.5f);
    CHECK(rig.lastMacro(2) == 0.f && rig.engine->laneCentre(1, 2) == 0.4f);
    rig.engine->setModOffsetForTests(1, fx, 0.2f);
    CHECK(rig.lastParam(2) == 0.3f + 0.2f && rig.engine->laneCentre(1, fx) == 0.3f);
    p.b = 2;
    p.value = 0.9f;
    rig.send(p);
    CHECK(rig.lastParam(2) == 1.f && rig.engine->laneCentre(1, fx) == 0.9f);
    // A send: its given value held, its centre kept (automation and Stop compare with the centre).
    rig.engine->setModOffsetForTests(1, echo, 0.3f);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.9f));
    CHECK(rig.engine->sendGiven(1, 0) == 1.f && rig.engine->laneCentre(1, echo) == 0.9f);
    rig.send(make(Command::SetSend, 1, 0, 0, 0.2f));
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f + 0.3f && rig.engine->laneCentre(1, echo) == 0.2f);
    rig.engine->setModOffsetForTests(1, echo, -0.5f);
    CHECK(rig.engine->sendGiven(1, 0) == 0.f && rig.engine->laneCentre(1, echo) == 0.2f);
    // Back to nothing added: the centre exactly.
    rig.engine->setModOffsetForTests(1, echo, 0.f);
    rig.engine->setModOffsetForTests(1, 2, 0.f);
    CHECK(rig.engine->sendGiven(1, 0) == 0.2f && rig.lastMacro(2) == 0.4f);
}

// A centre not known yet (an effect put in a slot before its settings are told) is not moved
// about: nothing is given until the knob's value comes.
static void testAnUnknownCentreIsLeftAlone() {
    Rig rig;
    const int lane = autoLaneOfEffect(0, 1); // slot 1: no effect told anything yet
    CHECK(rig.engine->laneCentre(1, lane) == kAutoNone);
    rig.engine->setTrackEffect(1, 0, std::unique_ptr<Effect>(new SpyEffect(&rig.params)));
    rig.params.clear();
    rig.engine->setModOffsetForTests(1, lane, 0.3f);
    rig.frames(256);
    CHECK(rig.params.empty());
    rig.send(make(Command::SetEffectParam, 1, 0, 1, 0.25f));
    CHECK(rig.params.size() == 1 && rig.lastParam(1) == 0.25f + 0.3f);
}

// Automation moves the centre; the offset rides on it; Stop puts the centre back to the knob and
// the offset still rides. The centre is what automation and Stop compare: written equal to the
// knob, nothing glides, whatever is added.
static void testAutomationMovesTheCentre() {
    Rig rig;
    rig.send(make(Command::SetMacro, 1, 3, 0, 0.5f));
    rig.engine->setModOffsetForTests(1, 3, 0.1f);
    rig.write(3, 0.8f);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000); // a step: the glide takes its first half
    CHECK(rig.engine->laneCentre(1, 3) == 0.8f);
    CHECK(near(rig.lastMacro(3), 0.9, 1e-6));
    rig.send(make(Command::Stop));
    CHECK(rig.engine->laneCentre(1, 3) == 0.5f && near(rig.lastMacro(3), 0.6, 1e-6));
    // A lane written at the knob's value: nothing glides, so nothing is given while it plays.
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
    rig.engine->setModOffsetForTests(1, echo, 0.4f);
    rig.write(echo, 0.9f);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.frames(6000);
    CHECK(rig.engine->laneCentre(1, echo) == 0.9f && rig.engine->sendGiven(1, 0) == 1.f);
    rig.send(make(Command::Stop));
    CHECK(rig.engine->laneCentre(1, echo) == 0.1f && near(rig.engine->sendGiven(1, 0), 0.5, 1e-6));
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
        {"testTheStepper", testTheStepper},
        {"testTheCombine", testTheCombine},
        {"testAKnobGoesOutThroughTheOneWay", testAKnobGoesOutThroughTheOneWay},
        {"testAnUnknownCentreIsLeftAlone", testAnUnknownCentreIsLeftAlone},
        {"testAutomationMovesTheCentre", testAutomationMovesTheCentre},
    };
    const bool only = argc > 2 && std::strcmp(argv[1], "--only") == 0;
    for (const Test& t : tests) {
        bool wanted = !only;
        for (int i = 2; only && i < argc; ++i) wanted = wanted || std::strcmp(argv[i], t.name) == 0;
        if (wanted) t.run();
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mod tests passed\n");
    return 0;
}
