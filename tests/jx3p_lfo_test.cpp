// The JX-3P's LFO (plan batch 4, engine/src/jx3p/lfo.hpp): its table, its
// shapes, its rate, its delay, its steps, its routes (LFO DEPTH and each
// DCO's switch, LFO MOD on the VCF, the LFO macro), and what allNotesOff
// puts back.
//
// Each test says FAITHFUL (held to a published figure: its source named) or
// SELF (held to the plan's own guess). Nothing published gives the LFO's
// rate, delay or depth, so nearly all of these are SELF: a green one only
// says the code does what the plan guessed.
//
//   jx3p_lfo_test                  all of them
//   jx3p_lfo_test testRate ...     just those
//   JX3P_PRINT=1 jx3p_lfo_test     with the figures

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/jx3p.hpp"
#include "jx3p/lfo.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::jx3p;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool printing() { return failures || std::getenv("JX3P_PRINT"); }
static const int kTick = 480; // 10 ms at 48 kHz
static double keyHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static double counterHz(double hz) { return kCounterClockHz / std::floor(kCounterClockHz / hz + 0.5); }

struct Set { int id, value; };
// A synth with a held, plain patch (no envelope on anything, the filter open, KEY FOLLOW 0) and
// the LFO's settings all at 0 but what `sets` says.
static std::unique_ptr<Jx3p> synth(const std::vector<Set>& sets = {}) {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->prepare(48000.f);
    const Set plain[] = {{Attack, 0}, {Decay, 0}, {Sustain, 100}, {Release, 0}, {VcaMode, 0}, {VcaLevel, 50}, {Dco1Range, 1}, {Dco2Range, 1},
                         {SourceMix, 0}, {Cutoff, 40}, {Resonance, 0}, {KeyFollow, 0}, {VcfEnv, 0}, {Hpf, 0}, {DcoEnvDepth, 0},
                         {LfoWave, LfoSine}, {LfoRate, 0}, {LfoDelay, 0}, {DcoLfoDepth, 0}, {VcfLfo, 0}, {Dco1Lfo, 0}, {Dco2Lfo, 0}};
    for (const Set& s : plain) j->setSetting(s.id, s.value);
    for (const Set& s : sets) j->setSetting(s.id, s.value);
    return j;
}
static std::vector<float> play(Jx3p& j, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    j.render(l.data(), r.data(), frames);
    return l;
}
// The LFO alone, `ticks` ticks of it: its value after each.
static std::vector<float> lfoTicks(int wave, double dial, int ticks, int delayDial = 0) {
    Lfo lfo;
    std::vector<float> out;
    const double step = lfoRateHz(dial) * kTickSeconds;
    for (int t = 0; t < ticks; ++t) {
        lfo.tick(wave, step, lfoDelayTicks(delayDial));
        out.push_back(lfo.out);
    }
    return out;
}

// THE TABLE (SELF: every figure here is the plan's guess, section 2 "LFO"): RATE 0.1 to 20 Hz even in
// octaves; DELAY 0 to 5 s as the dial squared, in whole ticks; LFO DEPTH 10 is 12 semitones, as the
// dial squared; LFO MOD 10 is 4 octaves, straight.
static void testTheTable() {
    CHECK(std::fabs(lfoRateHz(0) - 0.1) < 1e-12 && std::fabs(lfoRateHz(100) - 20.0) < 1e-12);
    CHECK(std::fabs(lfoRateHz(50) - std::sqrt(0.1 * 20.0)) < 1e-12);
    for (int d = 0; d < 100; d += 10) CHECK(std::fabs(std::log2(lfoRateHz(d + 10) / lfoRateHz(d)) - std::log2(200.0) / 10.0) < 1e-12);
    CHECK(lfoRateHz(-5) == lfoRateHz(0) && lfoRateHz(150) == lfoRateHz(100));
    CHECK(lfoDelayTicks(0) == 0 && lfoDelayTicks(100) == 500 && lfoDelayTicks(50) == 125 && lfoDelayTicks(10) == 5);
    CHECK(std::fabs(dcoLfoSemitones(100) - 12.0) < 1e-12 && std::fabs(dcoLfoSemitones(20) - 0.48) < 1e-12 && dcoLfoSemitones(0) == 0.0);
    CHECK(std::fabs(vcfLfoOctaves(100) - 4.0) < 1e-12 && std::fabs(vcfLfoOctaves(50) - 2.0) < 1e-12 && vcfLfoOctaves(0) == 0.0);
}

// THE RATE (SELF: plan section 2, "rate within 1% of its table"): the LFO alone, its upward crossings
// of 0 over at least twenty cycles, at RATE 0, 2.5, 5, 7.5 and 10, each wave that crosses.
static void testRate() {
    for (int dial : {0, 25, 50, 75, 100})
        for (int wave : {LfoSine, LfoSquare}) {
            const double hz = lfoRateHz(dial);
            const double cycles = wave == LfoSine ? 22.0 : 210.0; // (a square's crossing is known to a tick only)
            const int ticks = static_cast<int>(cycles / hz / kTickSeconds) + 10;
            const std::vector<float> v = lfoTicks(wave, dial, ticks);
            double first = -1.0, last = -1.0;
            int crossings = 0;
            for (int t = 1; t < ticks; ++t)
                if (v[t - 1] < 0.f && v[t] >= 0.f) {
                    const double at = t - 1 + (wave == LfoSine ? -v[t - 1] / (v[t] - v[t - 1]) : 1.0);
                    if (first < 0.0) first = at;
                    last = at, ++crossings;
                }
            const double measured = (crossings - 1) / ((last - first) * kTickSeconds);
            CHECK(crossings >= 20);
            CHECK(std::fabs(measured / hz - 1.0) < 0.01);
            if (printing()) std::printf("  RATE %.1f %s: %.4f Hz (table %.4f)\n", dial / 10.0, wave == LfoSine ? "sine" : "square", measured, hz);
        }
}

// ...AND THROUGH THE SYNTH (SELF): the LFO the voices read, RATE 5.0 and 8.0, at 48 and 44.1 kHz (the
// tick is whole samples, 480 and 441, so each is 10 ms): its upward crossings over twenty cycles.
static void testRateInTheSynth() {
    for (float rate : {48000.f, 44100.f})
        for (int dial : {50, 80}) {
            std::unique_ptr<Jx3p> j(new Jx3p);
            j->prepare(rate);
            j->setSetting(LfoWave, LfoSine), j->setSetting(LfoRate, dial), j->setSetting(LfoDelay, 0);
            const int tick = static_cast<int>(rate / 100.f), ticks = static_cast<int>(20.5 / lfoRateHz(dial) / kTickSeconds);
            std::vector<float> v;
            for (int t = 0; t < ticks; ++t) play(*j, tick), v.push_back(j->lfoNow());
            double first = -1.0, last = -1.0;
            int crossings = 0;
            for (int t = 1; t < ticks; ++t)
                if (v[t - 1] < 0.f && v[t] >= 0.f) {
                    const double at = t - 1 - v[t - 1] / (v[t] - v[t - 1]);
                    if (first < 0.0) first = at;
                    last = at, ++crossings;
                }
            const double measured = (crossings - 1) / ((last - first) * kTickSeconds);
            CHECK(crossings >= 19 && std::fabs(measured / lfoRateHz(dial) - 1.0) < 0.01);
            if (printing()) std::printf("  in the synth at %.0f Hz, RATE %.1f: %.4f Hz (table %.4f)\n", rate, dial / 10.0, measured, lfoRateHz(dial));
        }
}

// THE SHAPES (SELF, the shapes' names from NAM): SINE reaches +1 and -1 and starts rising from 0;
// SQUARE is only +1 or -1, half the time each; RANDOM holds one value through a cycle, a new one each
// cycle, spread over -1..1, and the same from fresh twice.
static void testShapes() {
    const std::vector<float> sine = lfoTicks(LfoSine, 0, 1000); // 0.1 Hz: 1000 ticks, one cycle
    float top = -2.f, bottom = 2.f;
    for (float x : sine) top = std::fmax(top, x), bottom = std::fmin(bottom, x);
    CHECK(top > 0.9999f && top <= 1.f && bottom < -0.9999f && bottom >= -1.f);
    CHECK(sine[0] == 0.f && sine[1] > 0.f && sine[249] > sine[248] && std::fabs(sine[250] - 1.f) < 1e-6f);
    const std::vector<float> square = lfoTicks(LfoSquare, 37, 20000);
    int up = 0;
    bool onlyTwo = true;
    for (float x : square) up += x == 1.f, onlyTwo = onlyTwo && (x == 1.f || x == -1.f);
    CHECK(onlyTwo && std::fabs(up / 20000.0 - 0.5) < 0.01);
    CHECK(square[0] == 1.f); // a cycle starts on its top half
    // RANDOM, the LFO's own count of cycles beside it.
    Lfo lfo;
    const double step = lfoRateHz(60) * kTickSeconds;
    std::vector<float> perCycle;
    bool heldThrough = true;
    double sum = 0.0;
    int over = 0, under = 0;
    for (int t = 0; t < 30000; ++t) {
        const std::uint32_t cycle = lfo.cycles;
        lfo.tick(LfoRandom, step, 0);
        if (perCycle.size() == cycle) {
            perCycle.push_back(lfo.out);
            sum += lfo.out, over += lfo.out > 0.5f, under += lfo.out < -0.5f;
        } else heldThrough = heldThrough && lfo.out == perCycle[cycle];
        CHECK(lfo.out >= -1.f && lfo.out <= 1.f);
    }
    CHECK(heldThrough && perCycle.size() > 150);
    int same = 0;
    for (std::size_t c = 1; c < perCycle.size(); ++c) same += perCycle[c] == perCycle[c - 1];
    CHECK(same == 0);
    CHECK(std::fabs(sum / perCycle.size()) < 0.15 && over > 20 && under > 20);
    CHECK(lfoTicks(LfoRandom, 60, 5000) == lfoTicks(LfoRandom, 60, 5000));
    // Its numbers are a stated seed (kLfoSeed), pinned: a change to them is a change to every song's
    // RANDOM LFO. (A first seed was 2 x 0x9E3779B9, so cycle 2 hashed 0 and read exactly -1.)
    CHECK(std::fabs(lfoRandomAt(0) - 0.0518282652f) < 1e-7f && std::fabs(lfoRandomAt(1) + 0.216764808f) < 1e-7f);
    CHECK(std::fabs(lfoRandomAt(2) + 0.705382705f) < 1e-7f && std::fabs(lfoRandomAt(3) - 0.236726046f) < 1e-7f);
    lfo.reset();
    CHECK(lfo.phase == 0.0 && lfo.cycles == 0u && lfo.out == 0.f && lfo.sinceKey == 0 && lfo.toTick == 0);
}

// STEPPED (FAITHFUL: SN p.6, the LFO is worked out in the CPU's 10 ms loop and written with the CVs):
// through the synth, the LFO on DCO-1 at RATE 10, a key 137 samples after the start. The LFO moves only
// on its own ticks (counted from the start), and the pitch only on the voice's (counted from its key).
static void testStepped() {
    std::unique_ptr<Jx3p> j = synth({{LfoRate, 100}, {DcoLfoDepth, 100}, {Dco1Lfo, 1}});
    std::vector<float> lfo;
    for (int i = 0; i < 137; ++i) play(*j, 1), lfo.push_back(j->lfoNow());
    j->noteOn(60, 1.f);
    std::vector<double> hz;
    for (int i = 0; i < 48000; ++i) play(*j, 1), hz.push_back(j->voiceHz(0)), lfo.push_back(j->lfoNow());
    int pitchMoves = 0, offGrid = 0, lfoMoves = 0, lfoOffGrid = 0;
    for (std::size_t i = 1; i < hz.size(); ++i)
        if (hz[i] != hz[i - 1]) ++pitchMoves, offGrid += i % kTick != 0;
    for (std::size_t i = 1; i < lfo.size(); ++i)
        if (lfo[i] != lfo[i - 1]) ++lfoMoves, lfoOffGrid += i % kTick != 0;
    CHECK(pitchMoves > 80 && offGrid == 0);
    CHECK(lfoMoves > 80 && lfoOffGrid == 0);
}

// THE ROUTES (SELF for the sizes; that LFO DEPTH reaches each DCO by its own switch and LFO MOD the VCF:
// NAM, SN p.6). A SQUARE at RATE 0: +1 for its first 5 s, -1 for the next.
static void testRoutes() {
    const double c4 = keyHz(60);
    const auto at = [&](std::vector<Set> sets, double seconds, double* hz1, double* hz2, double* cut) {
        sets.push_back({LfoWave, LfoSquare});
        std::unique_ptr<Jx3p> j = synth(sets);
        j->noteOn(60, 1.f);
        play(*j, static_cast<int>(seconds * 48000.0));
        *hz1 = j->voiceHz(0), *hz2 = j->voiceHz2(0), *cut = j->voiceCutoffHz(0);
    };
    double a, b, c, plainCut;
    at({}, 1.0, &a, &b, &plainCut);
    CHECK(a == counterHz(c4) && b == counterHz(c4));
    // DCO-1 alone, at LFO DEPTH 10: an octave up, then an octave down.
    at({{DcoLfoDepth, 100}, {Dco1Lfo, 1}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(2.0 * c4) && b == counterHz(c4) && c == plainCut);
    at({{DcoLfoDepth, 100}, {Dco1Lfo, 1}}, 6.0, &a, &b, &c);
    CHECK(a == counterHz(0.5 * c4) && b == counterHz(c4));
    // DCO-2 alone, and both, at LFO DEPTH 5: 3 semitones.
    at({{DcoLfoDepth, 50}, {Dco2Lfo, 1}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(c4) && b == counterHz(keyHz(63)));
    at({{DcoLfoDepth, 50}, {Dco1Lfo, 1}, {Dco2Lfo, 1}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(keyHz(63)) && b == counterHz(keyHz(63)));
    // A switch on with LFO DEPTH 0, and LFO DEPTH up with both switches off: nothing.
    at({{Dco1Lfo, 1}, {Dco2Lfo, 1}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(c4) && b == counterHz(c4));
    at({{DcoLfoDepth, 100}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(c4) && b == counterHz(c4));
    // LFO MOD on the VCF: 5.0 is two octaves up, then down; the DCOs untouched.
    at({{VcfLfo, 50}}, 1.0, &a, &b, &c);
    CHECK(a == counterHz(c4) && std::fabs(cents(c, 4.0 * plainCut)) < 1.3);
    at({{VcfLfo, 50}}, 6.0, &a, &b, &c);
    CHECK(std::fabs(cents(c, 0.25 * plainCut)) < 1.3);
    // ...with the DCO's ENV on too: the two add (a decay to SUSTAIN 0 leaves only the LFO).
    at({{DcoLfoDepth, 50}, {Dco1Lfo, 1}, {Dco1Env, 1}, {DcoEnvDepth, 50}, {Attack, 100}, {Sustain, 100}}, 1.0, &a, &b, &c);
    CHECK(a > counterHz(keyHz(63)) * 1.01);
    if (printing()) std::printf("  routes: plain cutoff %.1f Hz\n", plainCut);
}

// THE DELAY (SELF: its time and its straight fade are guesses; which keys start it again is not written
// down, plan section 2): DELAY 5.0 is 125 ticks. A key with none held starts it from 0; the LFO is at full
// on the 125th tick after it and not before; a key played while another is held does not start it
// again; a key after every key is up does.
static void testDelay() {
    std::unique_ptr<Jx3p> j = synth({{LfoWave, LfoSquare}, {LfoDelay, 50}, {DcoLfoDepth, 100}, {Dco1Lfo, 1}});
    play(*j, 3 * kTick + 100);
    j->noteOn(60, 1.f);
    CHECK(j->lfoNow() == 0.f); // from the key's own sample
    int full = -1;
    bool straight = true;
    for (int tick = 1; tick <= 200 && full < 0; ++tick) {
        play(*j, kTick);
        const float expect = tick * 1.f / 125.f; // (the LFO's next tick after the key is the fade's 0)
        straight = straight && std::fabs(j->lfoNow() - std::fmin(1.f, expect - 1.f / 125.f)) < 1e-6f;
        if (j->lfoNow() == 1.f) full = tick;
    }
    CHECK(straight);
    CHECK(full == 126); // the 125th tick after the first one the key met
    const double octave = counterHz(2.0 * keyHz(60));
    CHECK(j->voiceHz(0) < octave); // the voice reads it on its own tick, which comes next
    play(*j, kTick);
    CHECK(j->voiceHz(0) == octave);
    // Legato: another key while the first is held. The delay carries on at full.
    j->noteOn(64, 1.f);
    play(*j, 2 * kTick);
    CHECK(j->lfoNow() == 1.f);
    // Both let go, the voices rung out, a new key: from 0 again.
    j->noteOff(60), j->noteOff(64);
    play(*j, 4800);
    j->noteOn(67, 1.f);
    CHECK(j->lfoNow() == 0.f);
    play(*j, 10 * kTick);
    CHECK(j->lfoNow() > 0.f && j->lfoNow() < 0.1f);
    // A key let go and played again with nothing else held starts it again too, though its voice rings.
    std::unique_ptr<Jx3p> k = synth({{LfoWave, LfoSquare}, {LfoDelay, 50}, {Release, 80}});
    k->noteOn(60, 1.f);
    play(*k, 200 * kTick);
    CHECK(k->lfoNow() == 1.f);
    k->noteOff(60);
    play(*k, kTick);
    k->noteOn(60, 1.f);
    CHECK(k->lfoNow() == 0.f);
    // DELAY 0: at full at once.
    std::unique_ptr<Jx3p> n = synth({{LfoWave, LfoSquare}});
    n->noteOn(60, 1.f);
    play(*n, 1);
    CHECK(n->lfoNow() == 1.f);
    // DELAY 10 is 5 s.
    std::unique_ptr<Jx3p> m = synth({{LfoWave, LfoSquare}, {LfoDelay, 100}});
    m->noteOn(60, 1.f);
    play(*m, 500 * kTick);
    CHECK(std::fabs(m->lfoNow()) < 1.f);
    play(*m, kTick);
    CHECK(std::fabs(m->lfoNow()) == 1.f); // (5 s in, the square has turned to its lower half)
}

// IT RUNS FREE (SELF: the 1983 machine has no key trigger; Roland's 2018 plug-out lists it as new): the
// LFO's every value, sample by sample, is the same with keys played as with none (DELAY 0).
static void testFreeRunning() {
    std::unique_ptr<Jx3p> a = synth({{LfoRate, 63}, {LfoWave, LfoSine}}), b = synth({{LfoRate, 63}, {LfoWave, LfoSine}});
    bool same = true;
    for (int i = 0; i < 3 * 48000; ++i) {
        if (i % 7001 == 300) a->noteOn(48 + i % 12, 1.f);
        if (i % 7001 == 3300) a->noteOff(48 + (i - 3000) % 12);
        play(*a, 1), play(*b, 1);
        same = same && a->lfoNow() == b->lfoNow();
    }
    CHECK(same);
}

// THE LFO MACRO (SELF: plan section 4, "VCF LFO MOD and DCO LFO DEPTH together"): with both at 0 in
// the patch, the macro at 1 takes both to 10; at 0.5 it leaves them; at 0 it takes both to 0.
static void testTheMacro() {
    const auto heard = [](float macro, int depth, double* hz, double* cut) {
        std::unique_ptr<Jx3p> j = synth({{LfoWave, LfoSquare}, {Dco1Lfo, 1}, {DcoLfoDepth, depth}, {VcfLfo, depth}});
        j->setMacro(MacroLfo, macro);
        j->noteOn(60, 1.f);
        play(*j, 48000);
        *hz = j->voiceHz(0), *cut = j->voiceCutoffHz(0);
    };
    double hz, cut, plainHz, plainCut;
    heard(0.5f, 0, &plainHz, &plainCut);
    CHECK(plainHz == counterHz(keyHz(60)));
    heard(1.f, 0, &hz, &cut);
    CHECK(hz == counterHz(keyHz(72)) && std::fabs(cents(cut, 16.0 * plainCut)) < 1.3);
    heard(0.f, 50, &hz, &cut);
    CHECK(hz == plainHz && std::fabs(cents(cut, plainCut)) < 1.3);
    heard(0.75f, 0, &hz, &cut); // half way up: 5.0 each, 3 semitones and two octaves
    CHECK(hz == counterHz(keyHz(63)) && std::fabs(cents(cut, 4.0 * plainCut)) < 1.3);
}

// ALLNOTESOFF PUTS IT BACK (SELF: plan section 2, "the LFO's tick, phase, delay and RANDOM seed"): a
// synth played for a while with a RANDOM LFO on the pitch and the cutoff and a DELAY, then
// allNotesOff, gives the fresh synth's LFO, sample for sample, before any key and after; and once the
// tails are gone, the fresh synth's sound.
static void testAllNotesOffPutsItBack() {
    const std::vector<Set> sets = {{LfoWave, LfoRandom}, {LfoRate, 71}, {LfoDelay, 20}, {DcoLfoDepth, 40}, {Dco1Lfo, 1}, {Dco2Lfo, 1}, {VcfLfo, 30}, {Release, 30}};
    std::unique_ptr<Jx3p> a = synth(sets), b = synth(sets);
    a->noteOn(50, 1.f), a->noteOn(57, 0.7f);
    play(*a, 3 * 48000 + 1234);
    a->noteOff(50);
    play(*a, 777);
    a->allNotesOff();
    bool same = true;
    for (int i = 0; i < 3000; ++i) play(*a, 1), play(*b, 1), same = same && a->lfoNow() == b->lfoNow();
    CHECK(same);
    for (Jx3p* x : {a.get(), b.get()}) x->noteOn(62, 1.f), x->noteOn(69, 1.f);
    std::vector<float> sa, sb;
    for (int i = 0; i < 4 * 48000; ++i) {
        sa.push_back(play(*a, 1)[0]), sb.push_back(play(*b, 1)[0]);
        same = same && a->lfoNow() == b->lfoNow();
    }
    CHECK(same);
    CHECK(a->tailsRinging() == 0);
    CHECK(std::vector<float>(sa.end() - 48000, sa.end()) == std::vector<float>(sb.end() - 48000, sb.end()));
    CHECK(peakOf(sb) > 0.05f);
}

// THE SAME HOWEVER THE BLOCKS ARE CUT (SELF: a house rule): six keys with a fast RANDOM LFO on both DCOs
// and the cutoff, a DELAY, keys let go and played again, cut 1, 7, 480, 1000 and at random.
static std::vector<float> cutInto(int block) {
    std::unique_ptr<Jx3p> j = synth({{LfoWave, LfoRandom}, {LfoRate, 88}, {LfoDelay, 30}, {DcoLfoDepth, 60}, {Dco1Lfo, 1}, {Dco2Lfo, 1}, {VcfLfo, 50},
                                     {Resonance, 60}, {SourceMix, 50}, {Release, 40}});
    const long end = 5 * 48000;
    std::vector<float> out(static_cast<std::size_t>(end)), r(static_cast<std::size_t>(end));
    std::uint32_t seed = 5u;
    for (long done = 0; done < end;) {
        if (done % 24000 == 0) for (int k = 0; k < 6; ++k) j->noteOn(48 + 5 * k + static_cast<int>(done / 24000) % 3, 0.9f);
        if (done % 24000 == 18000) for (int k = 0; k < 6; ++k) j->noteOff(48 + 5 * k + static_cast<int>(done / 24000) % 3);
        long n = block;
        if (block == 0) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + static_cast<long>((seed >> 8) % 900u);
        }
        const long edge = (done / 6000 + 1) * 6000; // (the keys come on 6000's)
        if (edge - done < n) n = edge - done;
        if (end - done < n) n = end - done;
        j->render(&out[static_cast<std::size_t>(done)], &r[static_cast<std::size_t>(done)], static_cast<int>(n));
        done += n;
    }
    return out;
}
static void testBlocks() {
    const std::vector<float> whole = cutInto(64);
    CHECK(peakOf(whole) > 0.1f);
    for (int block : {1, 7, 480, 1000, 0}) CHECK(cutInto(block) == whole);
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testTheTable", testTheTable}, {"testRate", testRate}, {"testRateInTheSynth", testRateInTheSynth}, {"testShapes", testShapes}, {"testStepped", testStepped},
        {"testRoutes", testRoutes}, {"testDelay", testDelay}, {"testFreeRunning", testFreeRunning}, {"testTheMacro", testTheMacro},
        {"testAllNotesOffPutsItBack", testAllNotesOffPutsItBack}, {"testBlocks", testBlocks},
    };
    bool any = false;
    for (int a = 1; a < argc; ++a)
        for (const Test& t : tests)
            if (std::string(argv[a]) == t.name) t.run(), any = true;
    if (!any)
        for (const Test& t : tests) t.run();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("jx3p lfo tests passed\n");
    return 0;
}
