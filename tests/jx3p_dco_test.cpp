// The JX-3P's DCOs (plan batch 2, engine/src/jx3p/dco.hpp): aliasing first,
// then the plan's DCO table: in tune, the saw's height, PULSE's 10%, TUNE,
// FINE, SYNC's width, METAL's resets, NOISE, SOURCE MIX, and ENV on the DCOs.
//
// Each test says FAITHFUL (held to a published figure: its source named) or
// SELF (held to the plan's own guess or the house's bar). A green SELF test
// only says the code does what the plan guessed.
//
//   jx3p_dco_test                  all of them
//   jx3p_dco_test testAliasing ... just those
//   JX3P_PRINT=1 jx3p_dco_test     with the figures

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "apotheca/jx3p.hpp"
#include "jx3p/dco.hpp"
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
static double keyHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// ---- driving the DCOs alone, at exact frequencies -----------------------------------------------

struct Pair {
    int wave1 = WaveSaw, wave2 = WaveSaw, cross = CrossOff;
    double hz1 = 440.0, hz2 = 440.0;
    float mix = 0.f;
};
static std::vector<float> run(const Pair& how, std::size_t frames, DcoPair* keep = nullptr) {
    DcoPair d;
    d.one.start(0.123);
    d.two.start(0.456);
    d.one.wave = how.wave1;
    d.two.wave = how.wave2;
    d.cross = how.cross;
    d.one.dt = how.hz1 / 48000.0;
    d.two.dt = how.hz2 / 48000.0;
    d.noise.seed(77u);
    std::vector<float> out(frames);
    for (float& x : out) x = d.next(how.mix);
    if (keep) *keep = d;
    return out;
}

// ---- through the synth ----------------------------------------------------------------------

struct Set { int id, value; };
static std::unique_ptr<Jx3p> synth(std::vector<Set> sets) {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->prepare(48000.f);
    const Set plain[] = {{Attack, 0}, {Decay, 0}, {Sustain, 100}, {Release, 0}, {VcaMode, 0}, {VcaLevel, 50}, {Dco1Range, 1}, {Dco2Range, 1}, {SourceMix, 0}, {Chorus, 0}};
    for (const Set& s : plain) j->setSetting(s.id, s.value);
    for (const Set& s : sets) j->setSetting(s.id, s.value);
    return j;
}
static std::vector<float> play(Jx3p& j, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    j.render(l.data(), r.data(), frames);
    return l;
}
// A held note's sound from 0.1 s in (the hold has landed), `frames` long.
static std::vector<float> note(const std::vector<Set>& sets, int key, int frames, double* hz1 = nullptr, double* hz2 = nullptr) {
    std::unique_ptr<Jx3p> j = synth(sets);
    j->noteOn(key, 1.f);
    play(*j, 4800);
    if (hz1) *hz1 = j->voiceHz(0);
    if (hz2) *hz2 = j->voiceHz2(0);
    return play(*j, frames);
}
// The pitch of a sound near `hz`: by its zero crossings, from 200 Hz up after a band-pass about `hz`
// (so a wave's own ringing near its jumps cannot cross zero by itself: the pulse's top is only a
// tenth over zero).
static double pitchNear(const std::vector<float>& v, double hz) {
    if (hz < 200.0) return pitchOf(v, 4800, v.size()); // (a band-pass this low has not settled in a second)
    const std::vector<float> band = bandOf(v, hz);
    return pitchOf(band, 4800, band.size());
}

// ---- the tables -----------------------------------------------------------------------------

// THE CORRECTIONS (SELF: what a minimum-phase BLEP and BLAMP are): the jump's correction starts at
// -1 (nothing of the jump has come through yet) and ends at exactly 0; the bend's ends at exactly 0;
// the bend's is the jump's summed, and what the jump's adds up to is kBlampCarry, the part of a bend
// that stays (so the two tables agree with each other).
static void testTables() {
    CHECK(kBlep[0] < -0.999f && kBlep[0] >= -1.f && kBlep[kBlepPoints - 1] == 0.f && kBlamp[kBlepPoints - 1] == 0.f);
    double sum = 0.0;
    for (int i = 1; i < kBlepPoints; ++i) sum += 0.5 * (static_cast<double>(kBlep[i]) + kBlep[i - 1]) / kBlepPhases;
    CHECK(std::fabs(sum - kBlampCarry) < 0.01);               // (the faded tail is its only difference)
    CHECK(std::fabs(kBlamp[0] - (0.0 - kBlampCarry)) < 1e-5); // the bend's correction at 0 is "none yet", less the carry
    double worst = 0.0; // the bend's slope is the jump's correction (in the middle, away from the faded tail)
    for (int i = 1; i < kBlepPoints - 3 * kBlepPhases; ++i)
        worst = std::fmax(worst, std::fabs((kBlamp[i] - kBlamp[i - 1]) * kBlepPhases - 0.5 * (kBlep[i] + kBlep[i - 1])));
    CHECK(worst < 1e-3);
    CHECK(kBlampCarry < -1.0 && kBlampCarry > -4.0);
}

// A corrected jump leaves nothing behind when a voice starts again (SELF): a DCO started afresh
// owes nothing, so its first value is its wave's own.
static void testAFreshStartOwesNothing() {
    Dco d;
    d.wave = WaveSquare;
    d.dt = 0.01;
    d.start(0.49);
    for (int i = 0; i < 3; ++i) d.run(), d.take(); // its rising edge, corrected over the next 16 samples
    d.start(0.2);
    d.run();
    CHECK(d.take() == -1.f);
}

// AN EDGE ON THE SAMPLE'S VERY END IS STILL CORRECTED (SELF; found by a mutant, 2026-11-14): a square
// at 0.49 a step of 0.01 reaches its rising edge exactly at the sample's end. Its time, worked out,
// came to a hair past the end, the edge was passed by, and the square rose raw: +1 at once.
static void testAnEdgeOnTheSamplesEnd() {
    Dco d;
    d.wave = WaveSquare;
    d.dt = 0.01;
    d.start(0.49);
    d.run();
    const float first = d.take();
    CHECK(first < -0.99f && d.r == 0.5); // the edge is there, and none of it heard yet
    for (int i = 0; i < kBlepLength; ++i) d.run(), d.take();
    d.run();
    CHECK(std::fabs(d.take() - 1.f) < 1e-6f); // and it has come through whole
}

// THE SAW RISES (FAITHFUL: SN p.6, a capacitor charged and reset): between its resets it goes up.
static void testSawRises() {
    Pair p;
    p.hz1 = 220.0;
    const std::vector<float> out = run(p, 48000);
    int up = 0;
    for (std::size_t i = 1; i < out.size(); ++i) up += out[i] > out[i - 1] ? 1 : 0;
    CHECK(up > 0.95 * out.size());
}

// A WAVE TURNED IS CORRECTED (SELF): a saw turned to a square half way up its cycle (where the square
// is low: a jump of nearly 1) does not land in one sample, as a raw jump would, and has landed on the
// square once the correction is through.
static void testAWaveTurnedIsCorrected() {
    Dco d;
    d.dt = 0.0005;
    d.start(0.49);
    d.run();
    const float before = d.take();
    d.setWave(WaveSquare);
    d.run();
    const float at = d.take();
    // (Before the turn: the saw 0.4905 up its ramp, -0.019, and its slope's carry, kBlampCarry x 2 x dt.)
    CHECK(std::fabs(before - static_cast<float>(-0.019 + kBlampCarry * 2.0 * d.dt)) < 0.0005f && std::fabs(at - before) < 0.1f * std::fabs(-1.f - before));
    for (int i = 0; i < kBlepLength; ++i) d.run(), d.take();
    d.run();
    CHECK(std::fabs(d.take() + 1.f) < 1e-6f);
}

// NO OFFSET (SELF: what a saw, a square and a 10% pulse with its mean taken off are): each wave's
// mean is 0 at every pitch, and a synced saw's is the mean of the shape it makes. Each DCO runs a whole
// number of its own cycles (its period a whole number of samples), so the mean is exact but for the
// corrections. Found in review (2026-11-14): the saw carried nothing (kBlampCarry), and its mean was
// 4.47 x dt over 0 (0.006 at 65 Hz, 0.39 at 4186 Hz: 19% of the output's peak at key 96, and it
// stepped with the pitch at the tick under ENV). Measured with the carry: under 1e-5 everywhere.
static void testNoOffset() {
    double worst = 0.0;
    for (int wave : {WaveSaw, WavePulse, WaveSquare}) {
        for (int period : {734, 183, 46, 12, 6}) { // 65 Hz to 8 kHz
            Dco d;
            d.dt = 1.0 / period;
            d.wave = wave;
            d.start(0.3);
            const int cycles = 48000 / period + 1;
            for (int i = 0; i < kBlepLength; ++i) d.run(), d.take(); // (past the first sample's corrections settling in)
            double sum = 0.0;
            for (int i = 0; i < cycles * period; ++i) d.run(), sum += d.take();
            const double mean = sum / (cycles * period);
            worst = std::fmax(worst, std::fabs(mean));
            CHECK(std::fabs(mean) < 1e-4);
            if (std::fabs(mean) >= 1e-4 && printing()) std::printf("  wave %d, a period of %d: mean %.6f\n", wave, period, mean);
        }
    }
    // A synced saw, DCO-2 at 1.7 times DCO-1 (2 kHz): its ramp runs a whole cycle and 0.7 of one, so
    // its mean is -0.3 x 0.7 / 1.7.
    DcoPair p;
    p.one.dt = 1.0 / 24.0;
    p.two.dt = 1.7 / 24.0;
    p.one.start(0.0);
    p.two.start(0.0);
    p.cross = CrossSync;
    for (int i = 0; i < kBlepLength; ++i) p.next(1.f);
    double sum = 0.0;
    const int frames = 24 * 2000;
    for (int i = 0; i < frames; ++i) sum += p.next(1.f);
    const double synced = sum / frames, ideal = -0.3 * 0.7 / 1.7;
    if (printing()) std::printf("  no offset: worst mean %.7f; a synced saw %.5f (its shape's %.5f)\n", worst, synced, ideal);
    CHECK(std::fabs(synced - ideal) < 1e-3);
    // Through the synth (the chorus off, the VCF open): a saw's mean against its peak, low and at the
    // top of the keys. It was 1.0% at key 48, 19.5% at key 96 and 37.3% at key 96 at 4'.
    double worstShare = 0.0;
    for (const auto& at : {std::pair<int, int>{48, 1}, {96, 1}, {96, 2}}) {
        std::unique_ptr<Jx3p> j = synth({{Dco1Range, at.second}, {Cutoff, 100}, {KeyFollow, 100}});
        j->noteOn(at.first, 1.f);
        play(*j, 4800);
        const std::vector<float> out = play(*j, 48000);
        double mean = 0.0, peak = 0.0;
        for (float x : out) mean += x, peak = std::fmax(peak, std::fabs(x));
        const double share = std::fabs(mean / out.size()) / peak;
        worstShare = std::fmax(worstShare, share);
        CHECK(peak > 0.01 && share < 0.003);
    }
    if (printing()) std::printf("  through the synth: a saw's mean %.4f of its peak at worst\n", worstShare);
}

// THE TWO DCOS START APART (SELF: plan section 2, a seeded phase stands for the free-running
// counters): at unison, MIX 5, the two saws' sum is as loud as one in phase only by chance; over eight
// keys it differs from key to key, and some key is well under the in-phase sum.
static void testTheDcosStartApart() {
    double quietest = 1e9, loudest = 0.0;
    std::unique_ptr<Jx3p> j = synth({{SourceMix, 50}}), one = synth({});
    for (int k = 0; k < 8; ++k) { // the same key eight times, each let go and over before the next
        for (Jx3p* x : {j.get(), one.get()}) x->noteOn(57, 1.f), play(*x, 4800);
        const double level = rmsOf(play(*j, 9600)), alone = rmsOf(play(*one, 9600)); // alone: one saw at full, as the two in phase at MIX 5
        quietest = std::fmin(quietest, level / alone);
        loudest = std::fmax(loudest, level / alone);
        for (Jx3p* x : {j.get(), one.get()}) x->noteOff(57), play(*x, 4800);
    }
    if (printing()) std::printf("  two DCOs at unison: %.3f to %.3f of the two in phase\n", quietest, loudest);
    CHECK(quietest < 0.8 && loudest - quietest > 0.1);
}

// ---- aliasing -------------------------------------------------------------------------------

// The plan's two-sample polynomial BLEP (Twin's, batch 1's saw), for the record and to show the
// measure can see aliasing.
static std::vector<float> polySaw(double hz, std::size_t frames) {
    std::vector<float> out(frames);
    double p = 0.123;
    const double dt = hz / 48000.0;
    for (float& x : out) {
        double s = 2.0 * p - 1.0;
        if (p < dt) {
            const double u = p / dt;
            s -= u + u - u * u - 1.0;
        } else if (p > 1.0 - dt) {
            const double u = (p - 1.0) / dt;
            s -= u * u + u + u + 1.0;
        }
        x = static_cast<float>(s);
        p += dt;
        if (p >= 1.0) p -= 1.0;
    }
    return out;
}

// ALIASING, FIRST (SELF: the house's bar; the real DCO is analog and does not alias): every wave on
// each DCO, SYNC and METAL, notes 36 to 96 at 4', 1.4 s through a window: every part that is not a
// harmonic and falls under 15 kHz is at least 50 dB under the fundamental (for SYNC and METAL,
// DCO-1's fundamental). The plan's polynomial BLEP was tried first, as it said: it does not reach it.
static void testAliasing() {
    const std::size_t n = 65536;
    const char* names[3] = {"saw", "pulse", "square"};
    const Aside poly = asideOf(polySaw(2.0 * keyHz(96), n).data(), n, 2.0 * keyHz(96), 15000.0, 2.0 * keyHz(96));
    CHECK(poly.dB > -50.0); // the polynomial BLEP fails the bar (measured -35.6 dB at 4186 Hz): the table's job
    double worst = -600.0;
    const char* where = "";
    int whereNote = 0;
    const auto hold = [&](const Aside& a, const char* what, int key) {
        CHECK(a.dB < -50.0);
        if (a.dB > worst) worst = a.dB, where = what, whereNote = key;
        if (a.dB >= -50.0) std::printf("  aliasing: %s at note %d: %.1f dB at %.0f Hz\n", what, key, a.dB, a.hz);
    };
    for (int key = 36; key <= 96; key += 12) {
        const double f = 2.0 * keyHz(key); // 4'
        for (int wave = 0; wave < 3; ++wave) {
            Pair p;
            p.wave1 = p.wave2 = wave;
            p.hz1 = f;
            p.hz2 = f * 1.0123;
            hold(asideOf(run(p, n).data(), n, f, 15000.0, f), names[wave], key); // DCO-1
            p.mix = 1.f;
            hold(asideOf(run(p, n).data(), n, p.hz2, 15000.0, p.hz2), names[wave], key); // DCO-2
            // SYNC: DCO-2 a fifth up and a fourth down, reset by DCO-1.
            for (double semis : {7.0, -5.0}) {
                p.cross = CrossSync;
                p.hz2 = f * std::pow(2.0, semis / 12.0);
                hold(asideOf(run(p, n).data(), n, f, 15000.0, f), wave == 0 ? "sync saw" : wave == 1 ? "sync pulse" : "sync square", key);
            }
            // METAL: DCO-2 an exact fifth up (so the whole repeats every two of DCO-1's cycles).
            p.cross = CrossMetal;
            p.hz2 = 1.5 * f;
            hold(asideOf(run(p, n).data(), n, 0.5 * f, 15000.0, f), wave == 0 ? "metal saw" : wave == 1 ? "metal pulse" : "metal square", key);
        }
    }
    // Through the synth (the counter's own pitches, the VCA under its clip): note 96 at 4'.
    for (int wave = 0; wave < 3; ++wave) {
        double hz = 0.0;
        const std::vector<float> out = note({{Dco1Range, 2}, {Dco1Wave, wave}}, 96, static_cast<int>(n), &hz);
        hold(asideOf(out.data(), n, hz, 15000.0, hz), names[wave], 96);
    }
    if (printing()) std::printf("  aliasing: the worst %.1f dB (%s, note %d at 4'); the polynomial BLEP's saw at note 96 %.1f dB at %.0f Hz\n", worst, where, whereNote, poly.dB, poly.hz);
}

// ---- the DCO table --------------------------------------------------------------------------

// THE SAW'S HEIGHT (FAITHFUL: SN p.6, the DCO CV "keeps the sawtooth amplitude constant over the
// frequency range"): its fundamental, which is 2 / pi of its height, at notes 24, 60 and 96 (8')
// through the synth, the same within 0.2 dB; and as high as DCO-1's square's is 4 / pi of its.
static void testSawHeight() {
    std::vector<double> parts;
    for (int key : {24, 60, 96}) {
        double hz = 0.0;
        const std::vector<float> out = note({}, key, 48000, &hz);
        parts.push_back(partNear(out.data(), out.size(), hz));
    }
    for (double p : parts) CHECK(std::fabs(dB(p / parts[0])) < 0.2);
    double hz = 0.0;
    const std::vector<float> sq = note({{Dco1Wave, WaveSquare}}, 60, 48000, &hz);
    CHECK(std::fabs(dB(partNear(sq.data(), sq.size(), hz) / parts[1]) - dB(2.0)) < 0.1); // the square's fundamental is twice the saw's
    if (printing()) std::printf("  saw height: fundamental %.2f / %.2f dB at notes 60 / 96 against note 24\n", dB(parts[1] / parts[0]), dB(parts[2] / parts[0]));
}

// PULSE IS 10% (FAITHFUL: WP, "pulse at 50% and 10%"; reading it as the share of the cycle the
// over-charged saw is off its top is the plan's). Two ways:
//  - by its harmonics: the second to the eighth under 15 kHz at notes 36, 60 and 84 (8') are those
//    of a ramp that takes 10% of the cycle and then holds, each against the fundamental, within
//    0.05 dB (a ramp of 9% or 11% is more than 0.3 dB off on one of them, at every note);
//  - by time, at notes 36 and 60 (long cycles): twice the share of samples under the middle of its
//    height (the ramp is half under it), 10% within 1. Not at 84: a band-limited edge 46 samples a
//    cycle long reads 8.5 that way, with the harmonics right.
static double rampHarmonic(double width, int k) {
    double re = 0.0, im = 0.0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        const double p = (i + 0.5) / n, f = p < width ? -1.0 + 2.0 * p / width : 1.0;
        re += f * std::cos(2.0 * kPi * k * p);
        im += f * std::sin(2.0 * kPi * k * p);
    }
    return 2.0 * std::sqrt(re * re + im * im) / n;
}
static void testPulseIsTenPercent() {
    double worst = 0.0, apart = 1e9;
    for (int key : {36, 60, 84}) {
        double off9 = 0.0, off11 = 0.0;
        Pair p;
        p.wave1 = WavePulse;
        p.hz1 = keyHz(key);
        const std::vector<float> out = run(p, 48000);
        const double first = partNear(out.data(), out.size(), p.hz1);
        for (int k = 2; k <= 8 && k * p.hz1 < 15000.0; ++k) {
            const double got = dB(partNear(out.data(), out.size(), k * p.hz1) / first);
            worst = std::fmax(worst, std::fabs(got - dB(rampHarmonic(0.1, k) / rampHarmonic(0.1, 1))));
            off9 = std::fmax(off9, std::fabs(got - dB(rampHarmonic(0.09, k) / rampHarmonic(0.09, 1))));
            off11 = std::fmax(off11, std::fabs(got - dB(rampHarmonic(0.11, k) / rampHarmonic(0.11, 1))));
        }
        apart = std::fmin(apart, std::fmin(off9, off11));
        double mean = 0.0;
        for (float x : out) mean += x;
        CHECK(std::fabs(mean / out.size()) < 0.01); // its DC is taken off
        if (key > 60) continue;
        const float top = static_cast<float>(1.0 - kPulseMean), bottom = static_cast<float>(-1.0 - kPulseMean);
        int under = 0;
        for (float x : out) under += x < 0.5f * (top + bottom) ? 1 : 0;
        const double share = 2.0 * 100.0 * under / static_cast<double>(out.size());
        CHECK(std::fabs(share - 10.0) <= 1.0);
        if (printing()) std::printf("  pulse at note %d: %.2f%% off its top\n", key, share);
    }
    CHECK(worst < 0.05 && apart > 0.3);
    if (printing()) std::printf("  pulse's harmonics: within %.3f dB of a 10%% ramp's; 9%% or 11%% at least %.2f dB off\n", worst, apart);
    CHECK(kPulseGain == 10.0);
}

// IN TUNE (SELF: A440 is the program's): DCO-2 alone (MIX 10), every wave, every RANGE, by its sound
// against the counter's pitch, notes 24 to 96.
static void testDco2InTune() {
    double worst = 0.0;
    for (int range = 0; range < 3; ++range)
        for (int wave = 0; wave < 3; ++wave)
            for (int key = 24; key <= 96; key += 9) {
                double hz2 = 0.0;
                const std::vector<float> out = note({{SourceMix, 100}, {Dco2Range, range}, {Dco2Wave, wave}}, key, 48000, nullptr, &hz2);
                const double want = keyHz(key) * (range == 0 ? 0.5 : range == 2 ? 2.0 : 1.0);
                worst = std::fmax(worst, std::fabs(cents(pitchNear(out, hz2), want)));
                CHECK(std::fabs(cents(hz2, want)) < 1.0);
            }
    CHECK(worst < 1.0);
    if (printing()) std::printf("  DCO-2 in tune: worst %.3f cents\n", worst);
}

// TUNE (FAITHFUL: OM and NAM: 5.0 unison, 7.9 a fifth (Fat Fifth), 10 an octave up (Accordion), 0 an
// octave down; in semitone steps, the plug-out's manual): DCO-2 against DCO-1 at TUNE 0, 2.5, 5,
// 7.9, 10: -12, -6, 0, +7, +12 semitones within 1 cent, by the counters and by the sound.
static void testTune() {
    const int dials[5] = {0, 25, 50, 79, 100}, want[5] = {-12, -6, 0, 7, 12};
    for (int i = 0; i < 5; ++i) {
        double hz1 = 0.0, hz2 = 0.0;
        const std::vector<float> two = note({{SourceMix, 100}, {Dco2Tune, dials[i]}}, 57, 48000, &hz1, &hz2);
        CHECK(std::fabs(cents(hz2, hz1) - 100.0 * want[i]) < 1.0);
        CHECK(std::fabs(cents(pitchNear(two, hz2), 220.0) - 100.0 * want[i]) < 1.0);
    }
    // In steps: every tenth of the dial is a whole number of semitones, and they climb by one.
    int last = -13;
    for (int d = 0; d <= 100; ++d) {
        const double s = tuneSemitones(d, true, kTuneSemitonesPerDial);
        CHECK(s == std::floor(s) && (s == last || s == last + 1));
        last = static_cast<int>(s);
    }
    CHECK(kTuneStepped && last == 12 && tuneSemitones(0, true, 2.4) == -12.0 && tuneSemitones(35, true, 2.4) == -4.0 && tuneSemitones(45, true, 2.4) == -1.0);
    CHECK(std::fabs(tuneSemitones(55, false, 2.4) - 1.2) < 1e-12); // (smooth, if the owner chooses it)
}

// FINE (FAITHFUL: OM, plus or minus 50 cents, smooth): FINE 0, 2.5, 5, 10.
static void testFine() {
    const int dials[4] = {0, 25, 50, 100};
    const double want[4] = {-50.0, -25.0, 0.0, 50.0};
    for (int i = 0; i < 4; ++i) {
        double hz1 = 0.0, hz2 = 0.0;
        note({{Dco2Fine, dials[i]}}, 57, 480, &hz1, &hz2);
        CHECK(std::fabs(cents(hz2, hz1) - want[i]) < 1.0);
    }
}

// SYNC'S WIDTH (FAITHFUL: OM p.23 and NAM sweep a pulse's width by turning DCO-2's TUNE under
// SYNC, and SN p.6 has DCO-1 reset DCO-2): DCO-2 on SQUARE, SYNC, TUNE from 5 down; the share of
// each of DCO-1's cycles the square is low is DCO-1's frequency over twice DCO-2's, within 1%.
// And synced, DCO-2 sounds at DCO-1's pitch.
static void testSyncWidth() {
    for (int dial : {45, 40, 30, 20, 10}) {
        double hz1 = 0.0, hz2 = 0.0;
        const std::vector<float> out = note({{SourceMix, 100}, {Dco2Wave, WaveSquare}, {Dco2CrossMod, CrossSync}, {Dco2Tune, dial}}, 45, 48000, &hz1, &hz2);
        int low = 0;
        for (float x : out) low += x < 0.f ? 1 : 0;
        const double share = low / static_cast<double>(out.size()), want = hz1 / (2.0 * hz2);
        CHECK(std::fabs(share - want) < 0.01);
        if (printing()) std::printf("  sync at TUNE %.1f: low %.4f of the time, DCO-1 / 2 DCO-2 is %.4f\n", dial / 10.0, share, want);
    }
    double hz1 = 0.0;
    const std::vector<float> up = note({{SourceMix, 100}, {Dco2CrossMod, CrossSync}, {Dco2Tune, 79}}, 45, 48000, &hz1);
    CHECK(std::fabs(cents(pitchNear(up, hz1), hz1)) < 1.0);
    const std::vector<float> off = note({{SourceMix, 100}, {Dco2Tune, 79}}, 45, 48000);
    CHECK(partNear(off.data(), off.size(), hz1) < 0.001 && partNear(up.data(), up.size(), hz1) > 0.05); // DCO-1's pitch is there only synced
}

// METAL (FAITHFUL: SN p.6, DCO-2's counter "does not sync to DCO-1", its saw "is clocked on every
// negative edge of both DCO-1 and 2 rectangulars"): DCO-2 a tempered fifth up, one second: its ramp
// starts again exactly as many times as both counters fall, and its counter keeps its own time.
static void testMetal() {
    Pair p;
    p.cross = CrossMetal;
    p.hz1 = 220.0;
    p.hz2 = 220.0 * std::pow(2.0, 7.0 / 12.0);
    p.mix = 1.f;
    DcoPair d;
    const std::vector<float> out = run(p, 48000, &d);
    // How many times each counter fell in 48000 samples, from where it started.
    const long falls1 = static_cast<long>(std::floor(0.123 + 48000 * (p.hz1 / 48000.0))), falls2 = static_cast<long>(std::floor(0.456 + 48000 * (p.hz2 / 48000.0)));
    CHECK(static_cast<long>(d.two.resets) == falls1 + falls2);
    CHECK(static_cast<long>(d.one.resets) == falls1);
    CHECK(std::fabs(d.two.p - std::fmod(0.456 + 48000 * (p.hz2 / 48000.0), 1.0)) < 1e-6); // not synced: where its own count puts it
    // Plain, DCO-2's ramp starts again only at its own edges.
    Pair q = p;
    q.cross = CrossOff;
    DcoPair e;
    run(q, 48000, &e);
    CHECK(static_cast<long>(e.two.resets) == falls2);
    // Through the synth: METAL is heard (another sound from OFF) and SYNC is a third.
    const std::vector<float> metal = note({{SourceMix, 100}, {Dco2CrossMod, CrossMetal}, {Dco2Tune, 79}}, 57, 9600),
                             plain = note({{SourceMix, 100}, {Dco2Tune, 79}}, 57, 9600),
                             sync = note({{SourceMix, 100}, {Dco2CrossMod, CrossSync}, {Dco2Tune, 79}}, 57, 9600);
    CHECK(metal != plain && metal != sync && sync != plain);
    CHECK(rmsOf(out) > 0.2);
}

// NOISE (FAITHFUL: SN p.6, NOISE stops DCO-2's clock; the level is SELF): DCO-2's counter does not
// move and gives METAL no edges; the noise is as loud as the saw (white, -1..1, RMS 0.577), spread
// over the whole band, another for every key, the same from fresh.
static void testNoise() {
    Pair p;
    p.wave2 = WaveNoise;
    p.cross = CrossMetal;
    p.mix = 1.f;
    p.hz2 = 1000.0;
    DcoPair d;
    const std::vector<float> out = run(p, 48000, &d);
    CHECK(d.two.p == 0.456 && d.two.resets == 0);
    CHECK(std::fabs(dB(rmsOf(out) / std::sqrt(1.0 / 3.0))) < 0.2);
    const std::vector<double> size = spectrumOf(out.data(), 32768);
    double low = 0.0, high = 0.0;
    for (std::size_t i = 100; i < 6000; ++i) low += size[i] * size[i];
    for (std::size_t i = 10000; i < 15900; ++i) high += size[i] * size[i];
    CHECK(std::fabs(dB(std::sqrt(high / low))) < 0.5); // as much at the top as at the bottom: white
    // Through the synth: two keys differ, the same key from fresh is the same.
    const std::vector<Set> noise = {{SourceMix, 100}, {Dco2Wave, WaveNoise}};
    const std::vector<float> a = note(noise, 60, 4800), b = note(noise, 60, 4800);
    CHECK(a == b && peakOf(a) > 0.05f);
    std::unique_ptr<Jx3p> j = synth(noise);
    j->noteOn(60, 1.f);
    play(*j, 4800);
    const std::vector<float> first = play(*j, 4800);
    j->noteOn(64, 1.f);
    play(*j, 4800);
    CHECK(play(*j, 4800) != first);
}

// SOURCE MIX (FAITHFUL: OM, NAM: 0 is DCO-1, 10 DCO-2, a straight crossfade; NAM has 6.8 as "1:2"
// and 7.4 to 7.8 as "1:3"): DCO-1's saw and DCO-2's a fifth up, each by its own fundamental. A
// straight crossfade gives 1:2 at 6.67 and 1:3 at 7.5, so NAM's 6.8 reads 0.53 dB off 1:2 (the
// plan's 0.3 dB was tighter than its own straight line allows): held to NAM within a tenth of the
// dial, and to the straight line within 0.05 dB.
static void testSourceMix() {
    for (int mix : {0, 50, 68, 75, 76, 100}) {
        double hz1 = 0.0, hz2 = 0.0;
        const std::vector<float> out = note({{SourceMix, mix}, {Dco2Tune, 79}}, 45, 48000, &hz1, &hz2);
        const double a = partNear(out.data(), out.size(), hz1), b = partNear(out.data(), out.size(), hz2);
        if (mix == 0) CHECK(dB(b / a) < -80.0);
        else if (mix == 100) CHECK(dB(a / b) < -80.0);
        else CHECK(std::fabs(dB(b / a) - dB(mix / (100.0 - mix))) < 0.05);
        if (mix == 50) CHECK(std::fabs(dB(b / a)) < 0.3);                // 1:1
        if (mix == 68) CHECK(std::fabs(dB(b / a) - dB(2.0)) < 0.6);      // NAM's 1:2 (0.53 dB over it: a tenth of the dial)
        if (mix == 76) CHECK(std::fabs(dB(b / a) - dB(3.0)) < 0.6);      // NAM's 1:3, 7.4 to 7.8
        if (mix == 75) CHECK(std::fabs(dB(b / a) - dB(3.0)) < 0.05);     // the straight line's 1:3
        if (printing() && mix > 0 && mix < 100) std::printf("  mix %.1f: DCO-2 %.2f dB over DCO-1\n", mix / 10.0, dB(b / a));
    }
}

// ENV ON THE DCOS (FAITHFUL that it steps: SN p.6, the counters are written once a loop; the depth
// is SELF, kDcoEnvSemitones): ENV DEPTH 10 on DCO-1 only, a slow attack: DCO-1's pitch moves only on
// the voice's ticks and is 24 semitones up at the top; DCO-2's does not move. Half the depth, half
// the semitones; INVERTED, down.
static void testEnvOnTheDcos() {
    std::unique_ptr<Jx3p> j = synth({{Dco1Env, 1}, {DcoEnvDepth, 100}, {Attack, 50}});
    j->noteOn(45, 1.f);
    const double start1 = j->voiceHz(0), start2 = j->voiceHz2(0);
    double last = start1;
    int moves = 0, offTick = 0;
    for (int i = 0; i < 24000; ++i) {
        play(*j, 1);
        if (j->voiceHz(0) != last) {
            ++moves;
            if (i % 480 != 0) ++offTick;
            last = j->voiceHz(0);
        }
        CHECK(j->voiceHz2(0) == start2);
    }
    CHECK(moves > 5 && offTick == 0);
    CHECK(std::fabs(cents(j->voiceHz(0), keyHz(45)) - 2400.0) < 1.0 && j->voiceEnv(0) < 1.f + 1e-6f);
    // Freed with its envelope still up (GATE: the VCA shuts at once, RELEASE 10 is long), the voice
    // takes another key at that key's own pitch from the first sample, not at the last note's ENV.
    std::unique_ptr<Jx3p> g = synth({{Dco1Env, 1}, {DcoEnvDepth, 100}, {VcaMode, 1}, {Release, 100}});
    g->noteOn(45, 1.f);
    play(*g, 4800);
    g->noteOff(45);
    play(*g, 4800);
    CHECK(g->voiceNote(0) == -1);
    g->allNotesOff(); // (so the next key takes voice 0 again: every voice is as long silent)
    g->noteOn(50, 1.f);
    CHECK(g->voiceNote(0) == 50 && std::fabs(cents(g->voiceHz(0), keyHz(50))) < 1.0);
    for (int depth : {50, 100})
        for (int inverted = 0; inverted < 2; ++inverted) {
            std::unique_ptr<Jx3p> k = synth({{Dco2Env, 1}, {DcoEnvDepth, depth}, {DcoEnvPolarity, inverted}, {Decay, 100}, {Sustain, 0}});
            k->noteOn(45, 1.f);
            play(*k, 1);
            const double want = (inverted ? -1.0 : 1.0) * kDcoEnvSemitones * depth / 100.0;
            CHECK(std::fabs(cents(k->voiceHz2(0), keyHz(45)) - 100.0 * want) < 1.0);
            CHECK(std::fabs(cents(k->voiceHz(0), keyHz(45))) < 1.0); // DCO-1's switch is off
        }
}

// A WAVE CHANGED UNDER A HELD KEY changes on the voice's next tick (FAITHFUL: SN p.6 reads the panel
// each loop), and its jump is corrected, not left raw (SELF): no step between samples larger than
// the two waves' own.
static void testAWaveTurnedDuringANote() {
    std::unique_ptr<Jx3p> j = synth({});
    j->noteOn(57, 1.f);
    play(*j, 100);
    j->setSetting(Dco1Wave, WaveSquare);
    const std::vector<float> before = play(*j, 379), after = play(*j, 4800);
    const std::vector<float> saw = note({}, 57, 4800), square = note({{Dco1Wave, WaveSquare}}, 57, 4800);
    CHECK(largestJump(after) <= std::fmax(largestJump(saw), largestJump(square)) * 1.05f);
    // Before its tick it is still the saw: no sample sits on the square's flat top.
    int flat = 0;
    for (std::size_t i = 1; i < before.size(); ++i) flat += before[i] == before[i - 1] ? 1 : 0;
    CHECK(flat == 0);
    // After, the square's fundamental (twice the saw's).
    CHECK(std::fabs(dB(partNear(after.data() + 800, 4000, 220.0) / partNear(saw.data() + 800, 4000, 220.0)) - dB(2.0)) < 0.3);
}

// PLAY FROM THE TOP WITH NOISE AND TWO DCOS (SELF: plan section 2, what allNotesOff puts back): a
// voice used, let go and freed, then allNotesOff: the same key plays the same bits as on a fresh
// synth (the noise's seed, both DCOs' start phases and the waves all come from the count of keys).
static void testFromTheTopWithBothDcos() {
    const std::vector<Set> patch = {{SourceMix, 50}, {Dco2Wave, WaveNoise}, {Dco1Wave, WavePulse}, {Release, 10}};
    const std::vector<Set> patch2 = {{SourceMix, 40}, {Dco2Wave, WaveSaw}, {Dco2Tune, 57}, {Dco2Fine, 61}, {Dco2CrossMod, CrossMetal}, {Release, 10}};
    for (const std::vector<Set>* p : {&patch, &patch2}) {
        std::unique_ptr<Jx3p> a = synth(*p), b = synth(*p);
        for (int k : {60, 64, 67}) a->noteOn(k, 1.f);
        play(*a, 9600);
        for (int k : {60, 64, 67}) a->noteOff(k);
        play(*a, 48000); // rung out: no tail is handed over
        a->allNotesOff();
        for (Jx3p* j : {a.get(), b.get()}) j->noteOn(62, 1.f), j->noteOn(69, 1.f);
        CHECK(play(*a, 9600) == play(*b, 9600));
    }
}

// ANOTHER SAMPLE RATE (SELF): the DCOs are in tune at 44.1 kHz as at 48 (the counter's clock is
// counted in steps of the rate the synth was prepared at).
static void testAnotherRate() {
    for (int mix : {0, 100}) {
        std::unique_ptr<Jx3p> j(new Jx3p);
        j->prepare(44100.f);
        const Set plain[] = {{Attack, 0}, {Decay, 0}, {Sustain, 100}, {SourceMix, mix}, {Dco2Tune, 100}, {Chorus, 0}};
        for (const Set& s : plain) j->setSetting(s.id, s.value);
        j->noteOn(57, 1.f);
        play(*j, 4410);
        const std::vector<float> out = play(*j, 44100);
        const double want = mix ? 440.0 : 220.0; // DCO-2 is an octave up
        CHECK(std::fabs(cents(pitchOf(bandOf(out, want, 44100.0), 4410, out.size(), 44100.0), want)) < 1.0);
    }
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testTables", testTables}, {"testAFreshStartOwesNothing", testAFreshStartOwesNothing}, {"testAliasing", testAliasing},
        {"testSawHeight", testSawHeight}, {"testPulseIsTenPercent", testPulseIsTenPercent}, {"testDco2InTune", testDco2InTune},
        {"testTune", testTune}, {"testFine", testFine}, {"testSyncWidth", testSyncWidth}, {"testMetal", testMetal}, {"testNoise", testNoise},
        {"testSourceMix", testSourceMix}, {"testEnvOnTheDcos", testEnvOnTheDcos}, {"testAWaveTurnedDuringANote", testAWaveTurnedDuringANote},
        {"testFromTheTopWithBothDcos", testFromTheTopWithBothDcos}, {"testAnotherRate", testAnotherRate},
        {"testAnEdgeOnTheSamplesEnd", testAnEdgeOnTheSamplesEnd}, {"testSawRises", testSawRises}, {"testAWaveTurnedIsCorrected", testAWaveTurnedIsCorrected},
        {"testTheDcosStartApart", testTheDcosStartApart}, {"testNoOffset", testNoOffset},
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
    std::printf("jx3p dco tests passed\n");
    return 0;
}
