// The JX-3P's VCF and HPF (plan batch 3, engine/src/jx3p/ir3109.hpp): the
// plan's VCF table (the slope, singing, Organ I, KEY FOLLOW, the polarity, the
// low end, staying bounded), the HPF, BRILLIANCE, and the CV's steps; and what
// the owner's report on the resonance (2026-11-15) changed: the singing on its
// cutoff at one level up to 10 kHz, the edge at 9.5, the low end kept, the
// singing beside a played note (testSingsBesideTheNote).
//
// Each test says FAITHFUL (held to a published figure: its source named) or
// SELF (held to the plan's own guess or the house's bar). A green SELF test
// only says the code does what the plan guessed.
//
//   jx3p_vcf_test                  all of them
//   jx3p_vcf_test testSlope ...    just those
//   JX3P_PRINT=1 jx3p_vcf_test     with the figures

#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/jx3p.hpp"
#include "jx3p/ir3109.hpp"
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

// ---- the filter alone -------------------------------------------------------------------------

// `frames` of the filter at `hz` and RESONANCE `res` (its dial), fed `in` (or silence, kicked once).
static std::vector<float> through(const std::vector<float>& in, double hz, int res, bool kick = false) {
    Ir3109 f;
    if (kick) f.kick();
    VcfCoef g;
    g.set(hz, 48000.0);
    const float k = resFeedback(res), given = bassGiven(k, kBassKept);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) out[i] = f.run(in[i], g, k, given);
    return out;
}
static std::vector<float> silence(std::size_t n) { return std::vector<float>(n, 0.f); }
// A sine's size after the filter, against its size going in (a small sine: the filter is straight there).
static double gainAt(double sineHz, double hz, int res) {
    const std::vector<float> out = through(sineOf(sineHz, 0.01, 96000), hz, res);
    return partNear(out.data() + 48000, 48000, sineHz) / 0.01;
}
static double peakFrom(const std::vector<float>& v, std::size_t from) {
    double p = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) p = std::fmax(p, std::fabs(v[i]));
    return p;
}

// ---- through the synth ------------------------------------------------------------------------

struct Set { int id, value; };
static std::unique_ptr<Jx3p> synth(const std::vector<Set>& sets) {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->prepare(48000.f);
    const Set plain[] = {{Attack, 0}, {Decay, 0}, {Sustain, 100}, {Release, 0}, {VcaMode, 0}, {VcaLevel, 20}, {Dco1Range, 1}, {SourceMix, 0},
                         {Cutoff, 100}, {Resonance, 0}, {KeyFollow, 0}, {VcfEnv, 0}, {Hpf, 0}, {Chorus, 0}};
    for (const Set& s : plain) j->setSetting(s.id, s.value);
    for (const Set& s : sets) j->setSetting(s.id, s.value);
    return j;
}
static std::vector<float> play(Jx3p& j, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    j.render(l.data(), r.data(), frames);
    return l;
}
// The pitch of a sound near `hz`, from `from` on, through a band-pass about it (the DCO's own
// partials, far from it, cannot cross zero by themselves there).
static double pitchNear(const std::vector<float>& v, double hz, std::size_t from) {
    const std::vector<float> band = bandOf(v, hz);
    return pitchOf(band, from, band.size());
}
// The CUTOFF a voice's CV gives with nothing else moving it: kVcfLowHz up CUTOFF's octaves.
static double cutoffHz(int dial) { return kVcfLowHz * std::pow(2.0, cutoffOctaves(dial)); }

// ---- the pieces -------------------------------------------------------------------------------

// THE POLYNOMIALS (SELF: the plan's "short polynomials, not the maths library"): 2^x within 2e-9 of
// the library's from -10 to 15 octaves, and a stage's coefficient within 1e-6 of tan's from 1 Hz to
// the top.
static void testPolynomials() {
    double worst = 0.0;
    for (double x = -10.0; x <= 15.0; x += 0.0137) worst = std::fmax(worst, std::fabs(exp2Poly(x) / std::exp2(x) - 1.0));
    CHECK(worst < 2e-9);
    double worstG = 0.0;
    for (double hz = 1.0; hz <= 0.45 * 48000.0; hz *= 1.01) {
        const double g = std::tan(kPi * hz / 48000.0);
        worstG = std::fmax(worstG, std::fabs(onePoleG(hz, 48000.0) - g / (1.0 + g)));
    }
    CHECK(worstG < 1e-6);
    CHECK(cornerHz(100.0, kVcfLowHz, 48000.0) == kVcfTopHz && cornerHz(100.0, kVcfLowHz, 32000.0) == 0.45 * 32000.0); // the top
    // WHERE THE RANGE SITS (the owner's call, 2026-11-15; it was 15 Hz to 15 kHz): 49.84 Hz to 49.84 kHz,
    // the same span, so Organ I's CUTOFF 2.4 is middle C at key 60 (testOrganI, and the presets' test).
    // CUTOFF 10 is over the top now: through the synth the cutoff is held at 20 kHz from CUTOFF 8.7 up
    // at key 60 (8.6 is 18.9 kHz), and with everything that raises it full up (KEY FOLLOW, ENV MOD,
    // BRILLIANCE, the top key) it is still exactly the top: the polynomials are never asked past it.
    CHECK(kVcfLowHz == 49.84 && kVcfHighHz == 1000.0 * kVcfLowHz && kVcfTopHz == 20000.0 && kVcfTopOfRate == 0.45);
    CHECK(std::fabs(cents(cutoffHz(24), 440.0 * std::pow(2.0, (60 - 69) / 12.0))) < 0.5);
    for (int dial : {86, 87, 100}) {
        std::unique_ptr<Jx3p> j = synth({{Cutoff, dial}});
        j->noteOn(60, 1.f);
        play(*j, 4800);
        CHECK(dial == 86 ? j->voiceCutoffHz(0) > 18800.0 && j->voiceCutoffHz(0) < 19000.0 : j->voiceCutoffHz(0) == kVcfTopHz);
    }
    {
        std::unique_ptr<Jx3p> j = synth({{Cutoff, 100}, {KeyFollow, 100}, {VcfEnv, 100}, {Brilliance, 100}, {Resonance, 100}});
        j->noteOn(127, 1.f);
        const std::vector<float> out = play(*j, 4800);
        CHECK(j->voiceCutoffHz(0) == kVcfTopHz);
        for (float x : out) CHECK(std::isfinite(x));
    }
    if (printing()) std::printf("  polynomials: 2^x within %.2g, G within %.2g\n", worst, worstG);
}

// THE FEEDBACK'S BEND (SELF: its shape is the plan's guess, a ratio of polynomials): what it lets
// through, x times its gain, rises all the way, has no corner (its gain at 0 is 1), reaches the knee
// at three knees with a slope of 0 there, and goes on rising past it, slowly (a ninth of its first
// slope far out), with no flat top (2026-11-15: it was held flat past three knees, a clip); the same on
// both sides.
static void testTheBend() {
    const auto bent = [](float x) {
        float over, under;
        bendGain(x, over, under);
        return x * over / under;
    };
    float last = -1e9f;
    bool rising = true;
    for (float x = -20.f; x <= 20.f; x += 0.01f) {
        const float y = bent(x);
        rising = rising && y >= last - 1e-6f;
        last = y;
        CHECK(std::fabs(y + bent(-x)) < 1e-5f);
    }
    CHECK(rising);
    CHECK(std::fabs(bent(1e-3f) / 1e-3f - 1.f) < 1e-6f);
    CHECK(std::fabs(bent(3.f * kFeedbackKnee) - kFeedbackKnee) < 1e-5f);                  // the knee at three knees...
    CHECK(std::fabs(bent(3.01f * kFeedbackKnee) - bent(2.99f * kFeedbackKnee)) < 1e-6f);  // ...flat there for a moment...
    for (float x : {3.5f, 5.f, 10.f, 100.f}) CHECK(bent(x * kFeedbackKnee) > bent((x - 0.25f) * kFeedbackKnee) + 1e-4f); // ...and rising on past it
    const float far = (bent(1000.f * kFeedbackKnee) - bent(999.f * kFeedbackKnee)) / kFeedbackKnee;
    CHECK(std::fabs(far - 1.f / 9.f) < 1e-3f);
}

// A 24 dB SLOPE (FAITHFUL: SN p.6, "a 4-pole LPF"): at RESONANCE 0, two to four octaves over the
// cutoff, 24 dB an octave within 2 dB, by small sines through the filter alone; and by noise through
// the synth (DCO-2's NOISE, CUTOFF 2.3: 244 Hz, where 4.0 was before the range moved), from the spectrum, within 2 dB too.
static void testSlope() {
    const double fc = 250.0;
    const double two = dB(gainAt(4.0 * fc, fc, 0)), four = dB(gainAt(16.0 * fc, fc, 0));
    const double slope = (two - four) / 2.0;
    CHECK(std::fabs(slope - 24.0) < 2.0);
    CHECK(std::fabs(dB(gainAt(fc / 32.0, fc, 0))) < 0.05); // the pass band is flat
    CHECK(std::fabs(dB(gainAt(fc, fc, 0)) - 4.0 * dB(std::sqrt(0.5))) < 0.1); // four poles each 3 dB down at the cutoff
    // Noise through the synth.
    std::unique_ptr<Jx3p> j = synth({{Dco2Wave, 3}, {SourceMix, 100}, {Cutoff, 23}});
    j->noteOn(60, 1.f);
    play(*j, 4800);
    const std::vector<float> out = play(*j, 65536);
    const std::vector<double> size = spectrumOf(out.data(), 65536);
    const double bin = 48000.0 / 65536.0, at = cutoffHz(23);
    const auto band = [&](double hz) { // the mean power over a third of an octave about `hz`
        double sum = 0.0;
        int n = 0;
        for (std::size_t i = 0; i < size.size(); ++i)
            if (i * bin > hz * 0.89 && i * bin < hz * 1.12) sum += size[i] * size[i], ++n;
        return 10.0 * std::log10(sum / n);
    };
    const double noiseSlope = (band(4.0 * at) - band(16.0 * at)) / 2.0;
    CHECK(std::fabs(noiseSlope - 24.0) < 2.0);
    if (printing()) std::printf("  slope: %.2f dB an octave two to four octaves up (sines), %.2f (noise through the synth, cutoff %.0f Hz)\n", slope, noiseSlope, at);
}

// IT SINGS (FAITHFUL that it does: NAM's Organ I uses RESONANCE 10 as a pitched source; how soon,
// how loud and how pure are SELF): silence in, RESONANCE 10, a key's kick.
//  - At 1 kHz and 4 kHz within 1 dB of its level by 50 ms; 30 s on still there at the same level.
//  - From 80 Hz to 10 kHz: 7.2 dB under the saw's peak within 0.3 dB (SELF: the SN trims it to 600 mV
//    but prints no figure for the saw; it was 6 dB, and 3 dB louder at 10 kHz than at 1 kHz), ON ITS
//    CUTOFF within 1 cent (it sang sharp, 10 cents at 1 kHz, 30 at 4 kHz, while the bend was taken
//    from the sample before: 2026-11-15), every harmonic 45 dB under it (51 measured), and at 10 kHz,
//    where its harmonics are past the top and fold back, nothing that is not a harmonic within 40 dB
//    of it (45.7 measured; it was 40.6).
//  - Not kicked, silence stays exact zeros. THE EDGE is at RESONANCE 9.5 (feedback exactly 4; the
//    owner's report: it was at 8.7, so the top 13% of the knob sang): at 9.4 the kick dies away to
//    exact zeros, at 9.6 it sings.
static void testItSings() {
    for (double hz : {1000.0, 4000.0}) {
        const std::vector<float> out = through(silence(31 * 48000), hz, 100, true);
        const double level = peakFrom(std::vector<float>(out.begin() + 48000, out.begin() + 2 * 48000), 0);
        const double at50 = peakFrom(std::vector<float>(out.begin() + 2400 - 240, out.begin() + 2400 + 240), 0);
        const double late = peakFrom(out, out.size() - 48000);
        CHECK(dB(at50 / level) > -1.0);
        CHECK(std::fabs(dB(late / level)) < 0.1);
        if (printing()) std::printf("  sings at %.0f Hz: %.1f dB at 50 ms of its level\n", hz, dB(at50 / level));
    }
    for (double hz : {80.0, 300.0, 1000.0, 4000.0, 10000.0}) {
        const std::vector<float> out = through(silence(3 * 48000), hz, 100, true);
        const double level = peakFrom(out, 2 * 48000), sung = pitchOf(out, 2 * 48000, out.size());
        CHECK(std::fabs(dB(level) + 7.2) < 0.3);
        CHECK(std::fabs(cents(sung, hz)) < 1.0);
        const int whole = static_cast<int>(sung + 0.5);
        CHECK(whole > 0);
        if (whole <= 0) continue; // (not singing: the harmonics' reader needs a pitch)
        const Harmonics h = harmonicsIn(out.data() + 2 * 48000, 48000, whole, level);
        CHECK(hz > 9000.0 || h.strongest < -45.0);
        const Aside aside = asideOf(out.data() + out.size() - 65536, 65536, sung, 24000.0, sung);
        CHECK(aside.dB < -40.0);
        if (printing())
            std::printf("  sings at %.0f Hz: %.2f dB under the saw's peak, %+.2f cents from its cutoff, its harmonics %.1f dB under, the loudest else %.1f dB under (at %.0f Hz)\n", hz, -dB(level),
                        cents(sung, hz), -h.strongest, -aside.dB, aside.hz);
    }
    CHECK(peakFrom(through(silence(48000), 1000.0, 100, false), 0) == 0.0);
    const std::vector<float> under = through(silence(8 * 48000), 1000.0, 94, true);
    CHECK(peakFrom(under, 7 * 48000) == 0.0);
    const std::vector<float> over = through(silence(8 * 48000), 1000.0, 96, true);
    CHECK(peakFrom(over, 7 * 48000) > 0.05);
    CHECK(resFeedback(95) == 4.f && resFeedback(94) < 4.f && resFeedback(96) > 4.f && resFeedback(100) == static_cast<float>(kResTop));
    CHECK(std::fabs(resFeedback(50) - 2.f * 50.f / 47.5f) < 1e-6f); // straight below the edge
    if (printing()) std::printf("  RESONANCE 9.6 sings at %.3f of a saw's peak, 10 at %.3f\n", peakFrom(over, 7 * 48000), peakFrom(through(silence(3 * 48000), 1000.0, 100, true), 2 * 48000));
}

// IT IS THE SAME EVERY TIME (SELF): the kick is a fixed number, so two filters singing from it give
// the same bits.
static void testTheSameEveryTime() {
    CHECK(through(silence(48000), 700.0, 100, true) == through(silence(48000), 700.0, 100, true));
}

// ORGAN I (SELF: it pins the plan's reading of NAM, KEY FOLLOW 9.8 is 100%): RESONANCE 10, KEY
// FOLLOW 9.8, CUTOFF 2.4, the saw playing; the pitch it sings at, read at its steady level (from
// 0.5 s: at 40 Hz it is still growing at 0.2 s, and reads 13 cents sharp there), moves a semitone for
// a semitone across three octaves (notes 48 to 84) within 5 cents.
static void testOrganI() {
    double worst = 0.0, first = 0.0;
    for (int key = 48; key <= 84; key += 3) {
        std::unique_ptr<Jx3p> j = synth({{Resonance, 100}, {KeyFollow, 98}, {Cutoff, 24}});
        j->noteOn(key, 1.f);
        const std::vector<float> out = play(*j, 48000);
        const double expect = cutoffHz(24) * std::pow(2.0, (key - kKeyFollowNote) / 12.0);
        const double hz = pitchNear(out, expect, 24000);
        if (key == 48) first = hz;
        const double off = cents(hz, first) - 100.0 * (key - 48);
        worst = std::fmax(worst, std::fabs(off));
        if (printing()) std::printf("  Organ I at note %d: sings at %.2f Hz (%.1f cents from its cutoff), %.2f cents off a semitone a semitone\n", key, hz, cents(hz, expect), off);
    }
    CHECK(worst < 5.0);
    CHECK(kKeyFollowFull == 98);
}

// KEY FOLLOW (SELF): the cutoff's CV between notes 36 and 84 is KEY / 9.8 times the four octaves
// between them, exactly a straight line in KEY (0, 5, 8, 10); and the filter sings at those two
// cutoffs (alone: through the synth the saw's own partials near the cutoff pull the singing to them,
// as they would on any filter) a straight line within 1 cent (it was 17 cents off at KEY 10, allowed
// 20, while the bend was taken from the sample before and it sang sharp, 30 cents at 4 kHz).
static void testKeyFollow() {
    for (int key : {0, 50, 80, 100}) {
        double cv[2], sung[2];
        int n = 0;
        for (int note : {36, 84}) {
            std::unique_ptr<Jx3p> j = synth({{Resonance, 100}, {KeyFollow, key}, {Cutoff, 33}});
            j->noteOn(note, 1.f);
            play(*j, 4800);
            cv[n] = j->voiceCutoffHz(0);
            sung[n] = pitchOf(through(silence(2 * 48000), cv[n], 100, true), 48000, 2 * 48000);
            ++n;
        }
        const double want = 4.0 * key / 98.0;
        CHECK(std::fabs(std::log2(cv[1] / cv[0]) - want) < 1e-5);
        const double off = 1200.0 * (std::log2(sung[1] / sung[0]) - want);
        CHECK(std::fabs(off) < 1.0);
        if (printing()) std::printf("  KEY %.1f: 36 to 84 is %.4f octaves of cutoff (want %.4f); it sings %.1f cents off that\n", key / 10.0, std::log2(cv[1] / cv[0]), want, off);
    }
    // At its reference key KEY FOLLOW adds nothing.
    std::unique_ptr<Jx3p> j = synth({{KeyFollow, 100}, {Cutoff, 33}});
    j->noteOn(kKeyFollowNote, 1.f);
    play(*j, 1);
    CHECK(std::fabs(cents(j->voiceCutoffHz(0), cutoffHz(33))) < 0.01);
}

// THE POLARITY (FAITHFUL: OM p.25): ENV MOD up, NORMAL, the cutoff goes up with the envelope; INVERTED,
// down; by ENV MOD 5 x kVcfEnvOctaves / 10 octaves at the envelope's top (that size SELF). Heard too:
// the note (the VCA on GATE, so it stays) is brighter at the top NORMAL, darker INVERTED.
static void testPolarity() {
    for (int inverted = 0; inverted < 2; ++inverted) {
        std::unique_ptr<Jx3p> j = synth({{VcfEnv, 50}, {VcfEnvPolarity, inverted}, {Cutoff, 33}, {Decay, 100}, {Sustain, 0}, {VcaMode, 1}});
        j->noteOn(60, 1.f);
        play(*j, 480); // the first tick: the envelope is at its top (ATTACK 0)
        const double top = j->voiceCutoffHz(0), want = (inverted ? -0.5 : 0.5) * kVcfEnvOctaves;
        CHECK(std::fabs(std::log2(top / cutoffHz(33)) - want) < 1e-4);
        const std::vector<float> bright = play(*j, 4800);
        play(*j, 15 * 48000); // DECAY 10 to SUSTAIN 0: the envelope down
        CHECK(std::fabs(cents(j->voiceCutoffHz(0), cutoffHz(33))) < 1.0);
        const std::vector<float> after = play(*j, 4800);
        const double hiTop = rmsOf(bandOf(bright, 1000.0)), hiAfter = rmsOf(bandOf(after, 1000.0));
        CHECK(inverted ? hiTop < 0.5 * hiAfter : hiTop > 2.0 * hiAfter);
        if (printing()) std::printf("  polarity %d: at 1 kHz %.3g at the top, %.3g after; cutoff %.1f at the top\n", inverted, hiTop, hiAfter, top);
    }
}

// THE CV STEPS ONCE A TICK (FAITHFUL: SN p.6, "Write the control voltage data into ... VCF" once a
// loop) and is softened by the hold (SELF, 1 ms): with ENV MOD and a slow attack, the cutoff moves
// only in the first part of each tick and is still for the rest of it (its hold lands 1e-3 of an
// octave from the step, kVcfCvLands).
static void testTheCvSteps() {
    std::unique_ptr<Jx3p> j = synth({{VcfEnv, 80}, {Cutoff, 30}, {Attack, 60}});
    j->noteOn(60, 1.f);
    double last = 0.0;
    int movesLate = 0, moves = 0;
    for (int i = 0; i < 48000; ++i) {
        play(*j, 1);
        const double now = j->voiceCutoffHz(0);
        if (i > 0 && now != last) {
            ++moves;
            if (i % 480 > 400) ++movesLate;
        }
        last = now;
    }
    CHECK(moves > 50 && movesLate == 0);
    // A fresh voice's first sample has its cutoff where it is going, not sliding from somewhere.
    std::unique_ptr<Jx3p> k = synth({{Cutoff, 30}});
    k->noteOn(60, 1.f);
    play(*k, 1);
    CHECK(std::fabs(cents(k->voiceCutoffHz(0), cutoffHz(30))) < 0.01);
}

// A GLIDE MOVES THE CUTOFF WITH THE KEY, EVERY SAMPLE (SELF: the plan, "the DCOs and the VCF's key
// follow both slide"): KEY FOLLOW 9.8, a glide of an octave over 0.1 s: the cutoff moves on samples
// between ticks, is half way (in octaves) half way, and lands on the key's.
static void testGlideMovesTheCutoff() {
    std::unique_ptr<Jx3p> j = synth({{KeyFollow, 98}, {Cutoff, 50}});
    j->noteOnFrom(72, 1.f, 60, 0.1f);
    play(*j, 1);
    const double from = j->voiceCutoffHz(0);
    play(*j, 100);
    const double a = j->voiceCutoffHz(0);
    play(*j, 1);
    CHECK(j->voiceCutoffHz(0) > a); // sample 102: no tick there
    play(*j, 2400 - 102);
    CHECK(std::fabs(std::log2(j->voiceCutoffHz(0) / from) - 0.5) < 0.02);
    play(*j, 4800);
    CHECK(std::fabs(cents(j->voiceCutoffHz(0), cutoffHz(50) * std::pow(2.0, 12.0 / 12.0))) < 0.01);
}

// BRILLIANCE (SELF in its size, kBrillianceOctaves; the knob moves every voice's cutoff): 0, 5 and 10
// put the cutoff 2 octaves down, where CUTOFF has it, and 2 up; turned under a held note, on the
// next tick.
static void testBrilliance() {
    for (int b : {0, 50, 100}) {
        std::unique_ptr<Jx3p> j = synth({{Brilliance, b}, {Cutoff, 50}});
        j->noteOn(60, 1.f);
        play(*j, 4800);
        CHECK(std::fabs(std::log2(j->voiceCutoffHz(0) / cutoffHz(50)) - (b - 50) / 50.0 * kBrillianceOctaves) < 1e-4);
    }
    std::unique_ptr<Jx3p> j = synth({{Cutoff, 50}});
    j->noteOn(60, 1.f);
    play(*j, 100);
    j->setSetting(Brilliance, 100);
    play(*j, 379);
    CHECK(std::fabs(cents(j->voiceCutoffHz(0), cutoffHz(50))) < 0.01); // not before its tick
    play(*j, 480);
    CHECK(std::fabs(std::log2(j->voiceCutoffHz(0) / cutoffHz(50)) - kBrillianceOctaves) < 1e-4);
}

// THE LOW END (SELF: the IR3109 loses it as resonance rises, 1 / (1 + feedback), the Juno-60's
// does; the owner's report, 2026-11-15: "the resonance makes the low end of the signal basically
// disappear"): kBassKept of what the feedback takes away is given back, a share of the input
// low-passed at a quarter of the cutoff (two poles), added outside the loop.
//  - The filter alone, small sines under a 2 kHz cutoff: RESONANCE 0 whole; at RESONANCE 8 a sine at
//    a sixteenth of the cutoff and under is (1 + kept k) / (1 + k) of itself within 0.6 dB (it was
//    1 / (1 + k), 13.4 dB down); the octave under the cutoff keeps its loss, 9 dB and more down at
//    RESONANCE 9 (A3 Organ I's sine is over its 16' DCO by that: testOrganI, the presets' test).
//  - Through the synth, a saw's fundamental at a sixteenth of the cutoff and under (key 45 under
//    CUTOFF 7, key 33 under CUTOFF 5): at RESONANCE 8, 9.5 and 10 (singing) within 5 dB of RESONANCE 0
//    (it was 13 to 14 dB down).
static void testTheLowEnd() {
    CHECK(kBassKept == 0.5f);
    for (int res : {0, 80}) {
        const float k = resFeedback(res);
        const double want = dB((1.0 + kBassKept * k) / (1.0 + k));
        for (double hz : {31.25, 62.5, 125.0}) {
            const double at = dB(gainAt(hz, 2000.0, res));
            CHECK(std::fabs(at - want) < 0.6);
            if (printing()) std::printf("  the low end at RESONANCE %.1f: %.0f Hz under a 2 kHz cutoff %.2f dB (want %.2f; the old loss %.2f)\n", res / 10.0, hz, at, want, -dB(1.0 + k));
        }
    }
    // At RESONANCE 0 nothing is given back, the low end's two poles are not run and wait at 0, so a
    // share given back again starts from nothing (with the feedback's hold), not from where they were.
    {
        Ir3109 f;
        VcfCoef g;
        g.set(2000.0, 48000.0);
        const float k = resFeedback(80);
        for (int i = 0; i < 4800; ++i) f.run(0.5f, g, k, bassGiven(k, kBassKept));
        CHECK(f.l[1] > 0.4f);
        f.run(0.5f, g, 0.f, 0.f);
        CHECK(f.l[0] == 0.f && f.l[1] == 0.f);
    }
    const double octave = dB(gainAt(1000.0, 2000.0, 90));
    CHECK(octave < -9.0);
    if (printing()) std::printf("  the octave under the cutoff at RESONANCE 9: %.2f dB\n", octave);
    // Through the synth.
    const struct { int key, cut; } at[] = {{45, 70}, {33, 50}};
    for (const auto& a : at) {
        double level[4];
        const int res[4] = {0, 80, 95, 100};
        for (int r = 0; r < 4; ++r) {
            std::unique_ptr<Jx3p> j = synth({{Cutoff, a.cut}, {Resonance, res[r]}});
            j->noteOn(a.key, 1.f);
            play(*j, 24000);
            const std::vector<float> out = play(*j, 48000);
            level[r] = partNear(out.data(), out.size(), keyHz(a.key));
        }
        for (int r = 1; r < 4; ++r) CHECK(dB(level[r] / level[0]) > -5.0 && dB(level[r] / level[0]) < 0.5);
        if (printing())
            std::printf("  the low end through the synth, key %d under CUTOFF %.1f (%.0f Hz): RESONANCE 8 %.2f, 9.5 %.2f, 10 %.2f dB\n", a.key, a.cut / 10.0, cutoffHz(a.cut), dB(level[1] / level[0]),
                        dB(level[2] / level[0]), dB(level[3] / level[0]));
    }
}

// SINGING BESIDE A NOTE (SELF; the owner's report, 2026-11-15: "when it is turned up all the way it
// just squeals"): key 48's saw under CUTOFF 5 (1.6 kHz) at RESONANCE 10, KEY FOLLOW 0: the sine the
// filter sings is within 3 dB of the note's fundamental (it was 10 dB over it, the note 13 dB under
// RESONANCE 0), the fundamental within 5 dB of RESONANCE 0; and under CUTOFF 8 (12.5 kHz), where the
// singing was over everything (0.7 dB under the note's fundamental at RESONANCE 0, beaten into
// sidebands 6 dB under itself at the note's frequency), nothing that is not a harmonic of the note is
// within 5 dB of its fundamental (6.3 dB under measured; it was 13 dB OVER).
static void testSingsBesideTheNote() {
    std::unique_ptr<Jx3p> plain = synth({{Cutoff, 50}});
    plain->noteOn(48, 1.f);
    play(*plain, 48000);
    const std::vector<float> was = play(*plain, 65536);
    const double ref = partNear(was.data(), was.size(), keyHz(48));
    {
        std::unique_ptr<Jx3p> j = synth({{Cutoff, 50}, {Resonance, 100}});
        j->noteOn(48, 1.f);
        play(*j, 48000);
        const std::vector<float> out = play(*j, 65536);
        const double note = partNear(out.data(), out.size(), keyHz(48));
        const Aside sung = asideOf(out.data(), out.size(), keyHz(48), 3000.0, keyHz(48)); // the loudest part off the note's harmonics
        // (The sine may sit on a harmonic: it sings where the saw's own partials near the cutoff pull it, as on any
        // filter; read it as the loudest part within a third of an octave of the cutoff.)
        const std::vector<double> size = spectrumOf(out.data(), out.size());
        double top = 0.0;
        const double bin = 48000.0 / out.size(), at = cutoffHz(50);
        for (std::size_t i = 0; i < size.size(); ++i)
            if (i * bin > at * 0.89 && i * bin < at * 1.12) top = std::fmax(top, size[i]);
        const double sine = top / (0.35875 * out.size() / 2.0);
        CHECK(std::fabs(dB(sine / note)) < 3.0);
        CHECK(dB(note / ref) > -5.0);
        if (printing()) std::printf("  RESONANCE 10 under CUTOFF 5: the note's fundamental %.2f dB from RESONANCE 0's, the sine %.2f dB from it (off the harmonics: %.1f dB at %.0f Hz)\n", dB(note / ref), dB(sine / note), sung.dB, sung.hz);
    }
    {
        std::unique_ptr<Jx3p> j = synth({{Cutoff, 80}, {Resonance, 100}});
        j->noteOn(48, 1.f);
        play(*j, 48000);
        const std::vector<float> out = play(*j, 65536);
        const Aside off = asideOf(out.data(), out.size(), keyHz(48), 20000.0, keyHz(48));
        CHECK(off.dB < -5.0);
        if (printing()) std::printf("  RESONANCE 10 under CUTOFF 8: the loudest part off the note's harmonics %.1f dB under its fundamental (at %.0f Hz)\n", -off.dB, off.hz);
    }
}

// IT STAYS BOUNDED (SELF): the filter alone, fed a square of 10 (twenty times a DCO's) and loud
// noise, at the bottom, the middle and the top of the cutoff, RESONANCE 0 and 10, 30 s each: never not
// a number, never three times what goes in. And the synth with every VCF setting at its ends and the
// HPF at 10, chords of six, 10 s: never not a number, never over 3.2 (the VCA's clip holds a voice under
// 1.5, so six give at most 6 x 1.5 x 0.3 = 2.7; the HPF may lift a peak a little).
static void testStaysBounded() {
    float worst = 0.f;
    const std::size_t n = 30 * 48000;
    std::vector<float> square(n), noise(n);
    std::uint32_t seed = 1u;
    for (std::size_t i = 0; i < n; ++i) {
        square[i] = (i / 37) % 2 ? 10.f : -10.f;
        seed = seed * 1664525u + 1013904223u;
        noise[i] = 10.f * (static_cast<float>(seed >> 8) / 8388608.f - 1.f);
    }
    for (double hz : {kVcfLowHz, 1000.0, kVcfTopHz})
        for (int res : {0, 100})
            for (const std::vector<float>* in : {&square, &noise}) {
                const std::vector<float> out = through(*in, hz, res);
                for (float x : out) {
                    CHECK(std::isfinite(x));
                    if (!std::isfinite(x)) return;
                    worst = std::fmax(worst, std::fabs(x));
                }
            }
    CHECK(worst < 30.f);
    float synthWorst = 0.f;
    for (int cut : {0, 100})
        for (int res : {0, 100})
            for (int env : {0, 100})
                for (int pol : {0, 1}) {
                    std::unique_ptr<Jx3p> j = synth({{Cutoff, cut}, {Resonance, res}, {VcfEnv, env}, {VcfEnvPolarity, pol}, {KeyFollow, 100},
                                                     {Brilliance, 100 - cut}, {Hpf, 100}, {VcaLevel, 100}, {SourceMix, 50}, {Dco2Wave, 1}});
                    for (int k = 0; k < 6; ++k) j->noteOn(24 + 17 * k, 1.f);
                    const std::vector<float> out = play(*j, 10 * 48000);
                    for (float x : out) {
                        CHECK(std::isfinite(x));
                        if (!std::isfinite(x)) return;
                        synthWorst = std::fmax(synthWorst, std::fabs(x));
                    }
                }
    CHECK(synthWorst < 3.2f);
    if (printing()) std::printf("  bounded: the filter alone at most %.2f, the synth %.2f\n", worst, synthWorst);
}

// THE HPF (SELF: one pole, its corner a guess): the part alone at HPF 10 (1 kHz) is 3 dB down at its
// corner and falls 6 dB an octave below it within 0.2 dB (three and four octaves under), and passes
// 8 kHz within 0.3 dB. Through the synth: note 36's saw at 16' (32.7 Hz) has its fundamental
// untouched at HPF 0 (the same as at 8', the saw's height: SN p.6) and plainly down at HPF 2.
static void testHpf() {
    const auto gainHp = [](double sineHz, double corner) {
        HighPass h;
        const std::vector<float> in = sineOf(sineHz, 0.01, 96000);
        std::vector<float> out(in.size());
        const float g = onePoleG(corner, 48000.0);
        for (std::size_t i = 0; i < in.size(); ++i) out[i] = h.run(in[i], g);
        return partNear(out.data() + 48000, 48000, sineHz) / 0.01;
    };
    const double corner = cornerHz(hpfOctaves(100), kHpfLowHz, 48000.0);
    CHECK(std::fabs(corner - kHpfHighHz) < 0.01);
    CHECK(std::fabs(dB(gainHp(corner, corner)) + 3.01) < 0.1);
    const double slope = dB(gainHp(corner / 8.0, corner)) - dB(gainHp(corner / 16.0, corner));
    CHECK(std::fabs(slope - 6.0) < 0.2);
    CHECK(std::fabs(dB(gainHp(8000.0, corner))) < 0.3);
    // What it passes, it passes whole: at the top (a sample up, a sample down) a trapezoidal high-pass
    // is exactly 1 at any corner. A pass level of 0.998 (0.017 dB; a review's mutant, 2026-11-14)
    // reads 0.998 here.
    for (double at : {kHpfLowHz, kHpfHighHz}) {
        HighPass h;
        const float g = onePoleG(at, 48000.0);
        float y = 0.f;
        for (int i = 0; i < 48000; ++i) y = h.run(i % 2 ? -0.5f : 0.5f, g);
        CHECK(std::fabs(std::fabs(y) - 0.5f) < 1e-5f);
    }
    double hz16 = 0.0, hz8 = 0.0;
    const auto fundamental = [](int range, int hpf, double* hz) {
        std::unique_ptr<Jx3p> j = synth({{Dco1Range, range}, {Hpf, hpf}});
        j->noteOn(36, 1.f);
        play(*j, 48000);
        const std::vector<float> out = play(*j, 96000);
        *hz = range == 0 ? keyHz(24) : keyHz(36);
        return partNear(out.data(), out.size(), *hz);
    };
    const double at16 = fundamental(0, 0, &hz16), at8 = fundamental(1, 0, &hz8), at16on = fundamental(0, 20, &hz16);
    CHECK(std::fabs(dB(at16 / at8)) < 0.05);
    CHECK(dB(at16on / at16) < -1.0);
    if (printing()) std::printf("  HPF: %.2f dB an octave under its corner; the 33 Hz fundamental %.2f dB at HPF 0 against 65 Hz, %.2f dB at HPF 2\n", slope, dB(at16 / at8), dB(at16on / at16));
}

// THE HPF TURNED OFF LEAVES NO TRACE (SELF: "0 passes the sound untouched to the bit"): a synth whose
// HPF was up, turned back to 0 and the note let go, then allNotesOff and a key: the same bits as a
// synth that never had it up. And while it is down its tail rings out on silence, to exact zeros.
static void testHpfOffIsUntouched() {
    std::unique_ptr<Jx3p> a = synth({}), b = synth({});
    a->setSetting(Hpf, 60);
    a->noteOn(40, 1.f);
    play(*a, 9600);
    a->noteOff(40);
    play(*a, 4800);
    a->setSetting(Hpf, 0);
    play(*a, 48000);
    a->allNotesOff();
    for (Jx3p* x : {a.get(), b.get()}) x->noteOn(47, 1.f);
    CHECK(play(*a, 9600) == play(*b, 9600));
    // At HPF 6 its own tail rings on after the last voice is gone, and runs out to exact zeros.
    std::unique_ptr<Jx3p> c = synth({{Hpf, 60}});
    c->noteOn(40, 1.f);
    play(*c, 9600);
    c->noteOff(40);
    int n = 0;
    while (c->voiceNote(0) >= 0 && n < 48000) play(*c, 1), ++n;
    const std::vector<float> tail = play(*c, 48000);
    CHECK(n < 48000 && tail[0] != 0.f && peakFrom(tail, 24000) == 0.0);
}

// THE HPF'S CORNER SLIDES (SELF: its knob goes through a hold of 1 ms, as the voices' CVs): HPF turned
// from 0.1 to 10 at once under a held low saw (the GATE holding the VCA open, the cutoff low so the
// saw's own edges are small): the low part it takes away goes over the hold's millisecond, not in one
// sample. Measured 2026-11-14: the largest step between samples in the next 10 ms is 0.0013 (the saw's
// own 0.0001); with the corner turned in one sample, 0.0074. Held under 0.003.
static void testTheHpfSlides() {
    std::unique_ptr<Jx3p> j = synth({{Hpf, 1}, {Cutoff, 40}, {VcaMode, 1}});
    j->noteOn(36, 1.f);
    play(*j, 9600 + 37);
    j->setSetting(Hpf, 100);
    const float turned = largestJump(play(*j, 480));
    if (printing()) std::printf("  HPF turned at once: largest step %.5f\n", turned);
    CHECK(turned < 0.003f && turned > 0.0005f);
}

// THE HPF'S CORNER LANDS (SELF: the house rules, "every smoother lands exactly"; found in review,
// 2026-11-14): at every HPF, a second after the knob is set, its hold is still, so on silence nothing
// of it runs (render() skips the HPF, and the chorus with it). Over 4 octaves (HPF 6.1 and up) a
// float's step is coarser than the hold's landing, and it stuck 1.1e-5 short for good: an idle track
// at HPF 8 cost 0.14% of a desk core for ever.
static void testTheHpfLands() {
    int stuck = 0;
    for (int hpf = 1; hpf <= 100; ++hpf) {
        std::unique_ptr<Jx3p> j = synth({{Hpf, hpf}});
        j->noteOn(48, 1.f);
        play(*j, 4800);
        j->noteOff(48);
        play(*j, 48000);
        stuck += j->hpfLanded() ? 0 : 1;
        // Turned from there to another, it lands there too.
        j->setSetting(Hpf, 101 - hpf);
        play(*j, 48000);
        stuck += j->hpfLanded() ? 0 : 1;
    }
    if (printing()) std::printf("  the HPF's corner: %d of 200 not landed after a second\n", stuck);
    CHECK(stuck == 0);
}

// A VOICE TAKEN AGAIN STARTS AS A FRESH ONE (SELF: plan section 2, nothing of a silent voice moves):
// six keys played and let go, the voices freed, then one more key: the same bits whichever six went
// before (their filters held other sounds when they were freed).
static void testAVoiceTakenAgainIsFresh() {
    std::vector<float> after[2];
    for (int t = 0; t < 2; ++t) {
        std::unique_ptr<Jx3p> j = synth({{Resonance, 70}, {Cutoff, 60}});
        for (int k = 0; k < 6; ++k) j->noteOn(40 + 5 * k + 13 * t, 1.f);
        play(*j, 4800);
        for (int k = 0; k < 6; ++k) j->noteOff(40 + 5 * k + 13 * t);
        play(*j, 4800);
        for (int v = 0; v < kVoices; ++v) CHECK(j->voiceNote(v) == -1);
        j->noteOn(60, 1.f);
        after[t] = play(*j, 4800);
    }
    CHECK(peakFrom(after[0], 0) > 0.01 && after[0] == after[1]);
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testPolynomials", testPolynomials}, {"testTheBend", testTheBend}, {"testSlope", testSlope}, {"testItSings", testItSings}, {"testTheSameEveryTime", testTheSameEveryTime},
        {"testOrganI", testOrganI}, {"testKeyFollow", testKeyFollow}, {"testPolarity", testPolarity}, {"testTheCvSteps", testTheCvSteps},
        {"testGlideMovesTheCutoff", testGlideMovesTheCutoff}, {"testBrilliance", testBrilliance}, {"testTheLowEnd", testTheLowEnd}, {"testSingsBesideTheNote", testSingsBesideTheNote},
        {"testStaysBounded", testStaysBounded}, {"testHpf", testHpf}, {"testHpfOffIsUntouched", testHpfOffIsUntouched}, {"testAVoiceTakenAgainIsFresh", testAVoiceTakenAgainIsFresh}, {"testTheHpfSlides", testTheHpfSlides},
        {"testTheHpfLands", testTheHpfLands},
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
    std::printf("jx3p vcf tests passed\n");
    return 0;
}
