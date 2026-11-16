// ACID (docs/plans/acid-plan.md), the engine part: the rule header (batch 0), the voice (batch 1),
// accent and slide (batch 2), through the synth and through the engine, and the house rules
// (blocks, Play from the top, no allocation, no number too small to hold, NaN, silence).
//
// Every figure ACID holds is SELF (the plan's own choice: acid_rule.hpp says why). A green test
// only says the code does what the plan chose. NOBODY HAS HEARD ANY OF IT.
//
//   acid_test                  all of them
//   acid_test testRule ...     just those
//   acid_test --measure        the figures (pitch, slope, singing, slide, accent, cost)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

// (As acid.cpp: no multiply and add joined.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "acid/ladder.hpp"
#include "apotheca/acid.hpp"
#include "apotheca/acid_rule.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/glide_rule.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::acid;

// ---- counted new: nothing on the audio thread allocates -------------------------------------------

#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
static long gNews = 0;
static void* countedNew(std::size_t size) {
    ++gNews;
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t size) { return countedNew(size); }
void* operator new[](std::size_t size) { return countedNew(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool gPrint = false; // --measure: say the figures
static bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }

// ---- batch 0: the rule ----------------------------------------------------------------------

// The settings table: ids, ranges, usual values (the plan's section 1).
static void testSettings() {
    CHECK(kSettings == 14 && kSettingRoom >= kSettings);
    CHECK(Level == 13 && Wave == 0 && acid::Slide == 11);
    for (int id = 0; id < kSettings; ++id) {
        CHECK(settingMin(id) <= settingUsual(id) && settingUsual(id) <= settingMax(id));
        CHECK(settingHeld(id, -1000) == settingMin(id) && settingHeld(id, 1000) == settingMax(id));
    }
    CHECK(settingMin(Octave) == -2 && settingMax(Octave) == 1);
    CHECK(settingMin(Tune) == -100 && settingMax(Tune) == 100);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6);
    CHECK(settingMax(Wave) == 1 && settingMax(acid::Slide) == 1 && settingMax(Velocity) == 1);
    CHECK(settingUsual(Cutoff) == 30 && settingUsual(Resonance) == 60 && settingUsual(EnvMod) == 50 && settingUsual(Decay) == 40);
    CHECK(settingUsual(Accent) == 50 && settingUsual(Sweep) == 50 && settingUsual(Drive) == 0 && settingUsual(AmpDecay) == 80);
    CHECK(std::strcmp(waveName(kSaw), "SAW") == 0 && std::strcmp(waveName(kSquare), "SQUARE") == 0);
    CHECK(std::strcmp(slideName(kSlideNote), "NOTE") == 0 && std::strcmp(slideName(kSlideFixed), "FIXED") == 0);
    // The macros, lane by lane (the plan's section 3).
    const int lanes[kMacros] = {Cutoff, Resonance, EnvMod, Decay, Accent, Drive, Sweep, Tune};
    for (int m = 0; m < kMacros; ++m) CHECK(macroSetting(m) == lanes[m]);
    CHECK(macroBent(30.f, 0.5f, 0.f, 100.f) == 30.f);
    CHECK(macroBent(30.f, 0.f, 0.f, 100.f) == 0.f && macroBent(30.f, 1.f, 0.f, 100.f) == 100.f);
    CHECK(near(macroBent(30.f, 0.25f, 0.f, 100.f), 15.f, 1e-5) && near(macroBent(30.f, 0.75f, 0.f, 100.f), 65.f, 1e-5));
}

// ACCENT and the tie, case by case (the plan's section 2).
static void testRule() {
    CHECK(kAccentAt == 0.95f);
    CHECK(!accented(0.9f, 50.f));   // a note placed (kPlacedLevel 0.9) is plain
    CHECK(accented(1.f, 50.f));     // one click of hold A + RIGHT: 100%, an accent
    CHECK(accented(0.95f, 50.f));   // at the line
    CHECK(!accented(0.9499f, 50.f));
    CHECK(!accented(1.f, 0.f));     // ACCENT 0: no accent at all
    CHECK(accented(1.f, 0.01f));
    CHECK(!accented(1.f, -1.f));
    // A keyboard's hard hit: 121 of 127 accents, 120 does not.
    CHECK(accented(121.f / 127.f, 50.f) && !accented(120.f / 127.f, 50.f));

    // The tie: GLIDE with the gate open, or shut under 5 ms ago.
    CHECK(kTieMs == 5.0);
    CHECK(slides(true, true, 0.0));        // the note before still held: end to end, its off after
    CHECK(slides(true, false, 0.0));       // shut this very sample: end to end, its off first
    CHECK(slides(true, false, 0.0049));
    CHECK(!slides(true, false, 0.005));    // a rest: starts again (and slides in pitch)
    CHECK(!slides(true, false, 1.0));
    CHECK(!slides(false, true, 0.0));      // no GLIDE: starts again, overlap or not
    CHECK(!slides(false, false, 0.0));
    CHECK(slides(true, true, 100.0));      // the gate open: however long ago it last shut
}

// The slide's time and shape: an RC of three time constants, landing exactly.
static void testSlideShape() {
    CHECK(slideSeconds(kSlideNote, 0.0625) == 0.0625);
    CHECK(slideSeconds(kSlideFixed, 0.0625) == kSlideFixedSeconds && kSlideFixedSeconds == 0.060);
    CHECK(slideLeft(0.0, 0.1) == 1.0);
    CHECK(slideLeft(0.1, 0.1) == 0.0 && slideLeft(0.2, 0.1) == 0.0);
    CHECK(slideLeft(0.05, 0.0) == 0.0); // no time: there at once
    // At one time constant (time / 3) an RC has gone 1 - 1/e of the way; bent to land, a hair less.
    const double e3 = std::exp(-3.0), tau = 0.1 / 3.0;
    CHECK(near(slideLeft(tau, 0.1), (std::exp(-1.0) - e3) / (1.0 - e3), 1e-12));
    CHECK(near(slideLeft(2.0 * tau, 0.1), (std::exp(-2.0) - e3) / (1.0 - e3), 1e-12));
    // It only falls, and nearly at the end it is nearly there (no jump at the landing).
    double last = 1.0;
    for (int i = 1; i <= 1000; ++i) {
        const double v = slideLeft(0.1 * i / 1000.0, 0.1);
        CHECK(v <= last);
        last = v;
    }
    CHECK(slideLeft(0.1 * 0.999, 0.1) < 0.002);
    // The step a sample: (time / 3) seconds of them is 1/e.
    CHECK(near(std::pow(slideStep(0.06, 48000.0), 0.02 * 48000.0), std::exp(-1.0), 1e-9));
    CHECK(slideSamples(0.0625, 48000.0) == 3000 && slideSamples(0.06001, 48000.0) == 2881);
}

// The dials at both ends (the plan's section 1).
static void testReadings() {
    CHECK(near(cutoffHz(0), 50.0, 1e-9) && near(cutoffHz(100), 6400.0, 1e-6));
    CHECK(near(cutoffHz(30), 50.0 * std::pow(2.0, 2.1), 1e-9));
    CHECK(near(decaySeconds(0), 0.2, 1e-12) && near(decaySeconds(100), 2.0, 1e-12));
    CHECK(near(ampDecaySeconds(0), 0.2, 1e-12) && near(ampDecaySeconds(100), 6.0, 1e-9));
    CHECK(near(ampDecaySeconds(80), 3.0, 0.05)); // the usual: the plan's guess for the original
    CHECK(accentDecaySeconds() == decaySeconds(0));
    CHECK(near(std::pow(fallStep(0.5, 48000.0), 0.5 * 48000.0), 0.1, 1e-9));
    CHECK(envOctaves(0) == 0.0 && envOctaves(100) == kEnvOctaves);
    CHECK(accentDb(0) == 0.0 && accentDb(100) == kAccentMostDb && near(accentGain(100), std::pow(10.0, 0.4), 1e-12));
    CHECK(accentGain(0) == 1.0);
    CHECK(resShare(0) == 0.0 && resShare(kResEdgeDial) == 1.0 && near(resShare(100), kResTop, 1e-12));
    CHECK(near(resShare(47.5), 0.5, 1e-12) && resShare(-5) == 0.0 && resShare(500) == resShare(100));
    CHECK(driveGain(0) == 1.0 && near(driveGain(100), 8.0, 1e-12));
    CHECK(tuneCents(-100) == -100.0 && levelDb(-24) == -24.0);
    CHECK(sweepSeconds(0, 100) == 0.0 && near(sweepSeconds(100, 0), kSweepMostSeconds, 1e-12) && near(sweepSeconds(100, 100), 2.0 * kSweepMostSeconds, 1e-12));
    // One accent alone: the capacitor's peak, against a sum by hand (the two time constants apart
    // and the same), and none at SWEEP 0.
    CHECK(sweepPeak(0.1, 0.0) == 1.0);
    CHECK(near(sweepPeak(0.1, 0.1), std::exp(-1.0), 1e-12));
    double best = 0.0, y = 0.0;
    const double T = 0.0869, c = 0.075, dt = 1e-6;
    for (double t = 0; t < 1.0; t += dt) {
        y += dt * (std::exp(-t / T) - y) / c;
        best = std::max(best, y);
    }
    CHECK(near(sweepPeak(T, c), best, 1e-4));
    if (gPrint) std::printf("  one accent alone: the capacitor rises to %.4f of the envelope (T %.4f, tau %.3f)\n", best, T, c);
}

// ---- batch 1: the voice ---------------------------------------------------------------------

static const double kRate = 48000.0;
struct Set { int id, value; };

// An ACID on `preset` with `sets` on it.
static std::unique_ptr<Acid> synth(const std::vector<Set>& sets = {}, int preset = 0) {
    std::unique_ptr<Acid> a(new Acid);
    a->prepare(48000.f);
    a->selectPreset(preset);
    for (const Set& s : sets) a->setSetting(s.id, s.value);
    return a;
}
// The plain voice the measurements start from: wide open, no resonance, no envelope on the cutoff.
static std::vector<Set> plainSets() { return {{Cutoff, 100}, {Resonance, 0}, {EnvMod, 0}, {AmpDecay, 100}, {Accent, 0}}; }

// Something done to the synth at a frame.
struct Ev {
    long at;
    std::function<void(Acid&)> act;
};
// The synth played `frames` long, in blocks of `block`, each event at its frame.
static std::vector<float> play(Acid& a, const std::vector<Ev>& evs, long frames, int block = 256) {
    std::vector<float> out(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(block));
    long done = 0;
    std::size_t next = 0;
    while (done < frames) {
        while (next < evs.size() && evs[next].at <= done) evs[next++].act(a);
        long until = frames;
        if (next < evs.size() && evs[next].at < until) until = evs[next].at;
        const long n = std::min<long>(block, until - done);
        a.render(out.data() + done, right.data(), static_cast<int>(n));
        done += n;
    }
    return out;
}
static Ev on(long at, int key, float vel = 0.9f) { return {at, [key, vel](Acid& a) { a.noteOn(key, vel); }}; }
static Ev from(long at, int key, int fromKey, float seconds, float vel = 0.9f) {
    return {at, [=](Acid& a) { a.noteOnFrom(key, vel, fromKey, seconds); }};
}
static Ev off(long at, int key) { return {at, [key](Acid& a) { a.noteOff(key); }}; }
static double hzOf(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

// The ladder's edge, found once on the analog model: where the loop turns half a cycle and the
// feedback there (checked against the same sums done apart, in numpy, 2026-11-16: 1.531, 24.49).
static void testEdge() {
    const Edge e = ladderEdge();
    CHECK(near(e.w180, 1.531, 0.002) && near(e.k, 24.49, 0.05));
    CHECK(near(std::fabs(std::arg(ladderLoop(e.w180))), 3.141592653589793, 1e-6));
    if (gPrint) std::printf("  the ladder sings at %.5f of its stages' corner, at a feedback of %.4f\n", e.w180, e.k);
}

// The oscillator's pitch: within 0.5 cent, keys 24 to 72, OCTAVE and TUNE (SELF: the plan's bound).
static void testPitch() {
    double worst = 0.0;
    for (int wave : {kSaw, kSquare})
        for (int key = 24; key <= 72; key += 6) {
            std::vector<Set> sets = plainSets();
            sets.push_back({Wave, wave});
            std::unique_ptr<Acid> a = synth(sets);
            const std::vector<float> out = play(*a, {on(0, key)}, 96000);
            const double hz = hzOf(key), c = centsOff(bandOf(out, hz), hz, 0.3, 30.0, 16.0);
            worst = std::max(worst, std::fabs(c));
            CHECK(std::fabs(c) < 0.5);
        }
    // OCTAVE -1 and TUNE +37 on key 60.
    std::vector<Set> sets = plainSets();
    sets.push_back({Octave, -1});
    sets.push_back({Tune, 37});
    std::unique_ptr<Acid> a = synth(sets);
    const std::vector<float> out = play(*a, {on(0, 60)}, 96000);
    const double hz = hzOf(48) * std::pow(2.0, 37.0 / 1200.0), c = centsOff(bandOf(out, hz), hz, 0.3, 30.0, 16.0);
    CHECK(std::fabs(c) < 0.5);
    if (gPrint) std::printf("  pitch: keys 24 to 72, saw and square, %.4f cents at worst; OCTAVE -1 TUNE +37: %.4f\n", worst, c);
}

// A sine through the ladder (no feedback) at `hz`, its size out over the size in, in dB.
static double through(double singHz, double hz, float k = 0.f) {
    const Edge e = ladderEdge();
    LadderCoef c;
    c.set(singHz, kRate, e.w180);
    Ladder f;
    std::vector<float> out(48000);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = f.run(static_cast<float>(0.01 * std::sin(2.0 * kPi * hz * i / kRate)), c, k);
    return dB(partNear(out.data() + 24000, 24000, hz) / 0.01);
}

// The slope (SELF, the plan's "near 18 dB an octave"): measured against the singing pitch at three
// cutoffs, the octave over it and the next, and the analog model's figures (13.9, 18.4: numpy).
static void testSlope() {
    for (double f : {250.0, 500.0, 1000.0}) {
        const double at = through(f, f), o1 = through(f, 2 * f), o2 = through(f, 4 * f), low = through(f, f / 64), under = through(f, f / 16);
        CHECK(near(o1 - at, -13.9, 0.8));
        CHECK(near(o2 - o1, -18.4, 0.8));
        CHECK(near(low, 0.0, 0.3)); // the pass band at RESONANCE 0: as it came in
        CHECK(near(under, -2.1, 0.3)); // the coupled poles spread: four octaves under, already 2 dB down
        if (gPrint) std::printf("  slope at %4.0f Hz: %.1f dB at the cutoff, %.1f the octave over, %.1f the next; %.2f dB four octaves under, %.2f six\n", f, at, o1 - at, o2 - o1, under, low);
    }
}

// The ladder alone at feedback k, struck by one small sample and left: what it sings.
static std::vector<float> sung(double hz, double res, double seconds = 2.0) {
    const Edge e = ladderEdge();
    LadderCoef c;
    c.set(hz, kRate, e.w180);
    Ladder f;
    const float k = static_cast<float>(resShare(res) * e.k);
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = f.run(i == 0 ? 1e-3f : 0.f, c, k);
    return out;
}

// RESONANCE 100 sings at the cutoff it reads (the plan: within 10 cents; SELF) at one level, and
// under the edge it dies away. The read-ahead is what holds the pitch (the JX-3P's lesson).
static void testSinging() {
    double worstCents = 0.0, lo = 1e9, hi = -1e9;
    for (double hz : {80.0, 250.0, 1000.0, 4000.0, 10000.0}) {
        const std::vector<float> out = sung(hz, 100.0);
        const double c = centsOff(out, hz, 1.2, 60.0), a = sizeAt(out, hz, 1.2), b = sizeAt(out, hz, 1.8);
        worstCents = std::max(worstCents, std::fabs(c));
        lo = std::min(lo, dB(a)), hi = std::max(hi, dB(a));
        CHECK(std::fabs(c) < 10.0);
        CHECK(std::fabs(dB(a) - dB(b)) < 0.2); // it stands
        if (gPrint) std::printf("  sings at %5.0f Hz: %+.3f cents, %.1f dB (of v4)\n", hz, c, dB(a));
        const std::vector<float> under = sung(hz, 90.0, 1.0);
        CHECK(peakOf(std::vector<float>(under.end() - 4800, under.end())) < 1e-6f);
    }
    CHECK(hi - lo < 3.0); // one level, 80 Hz to 10 kHz
    // Through the synth: RESONANCE 100 against the same note at 0 (CUTOFF 30, no envelope).
    std::vector<Set> sets = {{Resonance, 100}, {EnvMod, 0}, {AmpDecay, 100}};
    std::unique_ptr<Acid> a = synth(sets), plain = synth({{Resonance, 0}, {EnvMod, 0}, {AmpDecay, 100}});
    const std::vector<float> o = play(*a, {on(0, 36)}, 48000), p = play(*plain, {on(0, 36)}, 48000);
    const double sing = rmsOf(o, 24000, 48000), flat = rmsOf(p, 24000, 48000);
    CHECK(peakOf(o) < 1.f && a->samplesClipped() == 0);
    if (gPrint) std::printf("  singing at most %.2f cents off, levels %.1f to %.1f dB; through the synth key 36 at RESONANCE 100: %.1f dB against 0 (peak %.2f)\n", worstCents, lo, hi, dB(sing / flat), peakOf(o));
}

// The oscillator's aliasing under 10 kHz (the plan's bound: -60 dB at key 72, or the table).
static void testAliasing() {
    for (int wave : {kSaw, kSquare})
        for (int key : {72, 84}) {
            std::vector<Set> sets = plainSets();
            sets.push_back({Wave, wave});
            std::unique_ptr<Acid> a = synth(sets);
            const std::vector<float> out = play(*a, {on(0, key)}, 4800 + 32768);
            const Aside s = asideOf(out.data() + 4800, 32768, hzOf(key), 10000.0, hzOf(key));
            if (key == 72) CHECK(s.dB < -60.0);
            if (gPrint) std::printf("  aliasing, %s key %d: %.1f dB at %.0f Hz (under 10 kHz)\n", waveName(wave), key, s.dB, s.hz);
        }
}

// The two decays fall to a tenth in their time; the gate shuts in 3 ms and the filter's runs on.
static void testEnvelopes() {
    std::unique_ptr<Acid> a = synth({{Decay, 40}, {AmpDecay, 50}, {Accent, 0}});
    const long d = static_cast<long>(std::lround(decaySeconds(40) * kRate));
    play(*a, {on(0, 48)}, d);
    CHECK(near(a->now().filterEnv, 0.1, 0.001));
    std::unique_ptr<Acid> b = synth({{Decay, 40}, {AmpDecay, 50}, {Accent, 0}});
    const long attack = static_cast<long>(std::ceil(kRate * 0.003)), t = static_cast<long>(std::lround(ampDecaySeconds(50) * kRate));
    play(*b, {on(0, 48)}, attack + t);
    CHECK(near(b->now().amp, 0.1, 0.001));
    // A strike works its cutoff out on its own first sample, off the tick (here 3 samples past one).
    std::unique_ptr<Acid> e = synth({{Cutoff, 0}, {EnvMod, 100}, {Decay, 100}, {Accent, 0}});
    play(*e, {on(0, 48)}, 16 * 50 + 3);
    const double fallen = e->now().filterEnv;
    play(*e, {on(0, 50)}, 1);
    CHECK(fallen < 0.99 && near(std::log2(e->now().cutoffHz / cutoffHz(0)), kEnvOctaves * e->now().filterEnv, 1e-4));
    // Let go: silent after 3 ms (exact zeros), the filter's decay still running, asleep after the tie's 5 ms.
    std::unique_ptr<Acid> c = synth({{Decay, 100}});
    play(*c, {on(0, 48)}, 4800);
    const double before = c->now().filterEnv;
    const std::vector<float> out = play(*c, {off(0, 48)}, 200);
    CHECK(peakOf(std::vector<float>(out.begin() + 145, out.end())) == 0.f && peakOf(std::vector<float>(out.begin(), out.begin() + 100)) > 0.f);
    CHECK(c->awake() && c->now().filterEnv < before && c->now().filterEnv > 0.5 * before);
    play(*c, {}, 100);
    CHECK(!c->awake());
}

// One voice: the newest key wins, and letting it go does not go back to a key still held.
static void testNewestWins() {
    std::unique_ptr<Acid> a = synth();
    play(*a, {on(0, 48), on(1000, 60)}, 2000);
    CHECK(a->key() == 60 && a->strikes() == 2 && a->ties() == 0);
    play(*a, {off(0, 48)}, 100);
    CHECK(a->gateOpen() && a->key() == 60); // a key not sounding: nothing
    play(*a, {off(0, 60)}, 100);
    CHECK(!a->gateOpen() && a->key() == 60); // no going back to 48
}

// allNotesOff: the note to the tail, faded out in 3 ms, the voice fresh at once.
static void testTail() {
    std::unique_ptr<Acid> a = synth();
    play(*a, {on(0, 48)}, 9600);
    a->allNotesOff();
    CHECK(!a->awake() && a->tailAwake());
    const std::vector<float> out = play(*a, {}, 400);
    CHECK(!a->tailAwake() && peakOf(std::vector<float>(out.begin() + 144, out.end())) == 0.f);
    CHECK(largestJump(out) < 0.05f);
}

// A line with everything in it: plain notes, ties (both orders), accents, a rest long enough to
// sleep, macros turned, allNotesOff.
static std::vector<Ev> busyLine() {
    return {on(0, 45), from(3000, 57, 45, 0.0625f, 1.f), off(6000, 45), off(9000, 57), from(9001, 52, 57, 0.06f),
            {15000, [](Acid& a) { a.setMacro(MacroCutoff, 0.8f); }}, on(15000, 40, 1.f), off(20000, 40), on(30000, 43, 1.f),
            {35000, [](Acid& a) { a.setMacro(MacroResonance, 1.f); }}, from(36000, 55, 43, 0.2f, 1.f), {40000, [](Acid& a) { a.allNotesOff(); }},
            on(40100, 48), {41000, [](Acid& a) { a.setMacro(MacroTune, 0.2f); }}, on(47000, 36, 1.f)};
}

// The same sound in blocks of 1, 7, 64, 256 and 333, to the bit.
static void testBlocks() {
    for (int wave : {kSaw, kSquare}) {
        std::uint64_t first = 0;
        for (int block : {256, 1, 7, 64, 333}) {
            std::unique_ptr<Acid> a = synth({{Wave, wave}, {Drive, 50}, {Resonance, 90}});
            Sum s;
            s.add(play(*a, busyLine(), 60000, block));
            if (block == 256) first = s.h;
            CHECK(s.h == first);
        }
    }
}

// No number too small to hold: every state is a number over 1e-20 or 0, held a minute or let go.
static void testTooSmall() {
    std::unique_ptr<Acid> a = synth({{Resonance, 100}, {Sweep, 100}, {Accent, 100}});
    float least = 1.f;
    play(*a, {on(0, 36, 1.f)}, 48000);
    for (int i = 0; i < 60 * 48; ++i) { // a minute held
        play(*a, {}, 1000);
        const float s = a->smallestState();
        if (s > 0.f) least = std::min(least, s);
    }
    CHECK(least >= 1e-20f && a->awake());
    play(*a, {off(0, 36)}, 48000);
    CHECK(!a->awake() && a->smallestState() == 0.f);
    if (gPrint) std::printf("  the smallest state held over a minute: %.3g\n", least);
}

// Not a number: a note with none is not played, a macro with none is not taken, and nothing sticks.
static void testNaN() {
    const float nan = std::nanf("");
    std::unique_ptr<Acid> a = synth();
    a->noteOn(48, nan);
    CHECK(!a->awake());
    a->noteOn(200, 0.9f);
    a->noteOn(-1, 0.9f);
    CHECK(!a->awake());
    a->setMacro(MacroCutoff, nan);
    a->setSetting(99, 5);
    a->noteOnFrom(48, 0.9f, 36, nan); // no time: plain
    std::vector<float> out = play(*a, {}, 4800);
    bool fine = true;
    for (float v : out) fine = fine && std::isfinite(v);
    CHECK(fine && peakOf(out) > 0.f && a->strikes() == 1);
    a->noteOn(48, std::numeric_limits<float>::infinity()); // held to 1: an accent
    out = play(*a, {}, 4800);
    fine = true;
    for (float v : out) fine = fine && std::isfinite(v);
    CHECK(fine);
}

// Silence in, silence out; a voice that has slept is a fresh one, to the bit.
static void testSilence() {
    std::unique_ptr<Acid> a = synth();
    CHECK(peakOf(play(*a, {}, 4800)) == 0.f && !a->awake());
    play(*a, {on(0, 48), off(4800, 48)}, 48000);
    CHECK(!a->awake());
    const std::vector<float> again = play(*a, {on(0, 52, 1.f)}, 9600);
    std::unique_ptr<Acid> fresh = synth();
    const std::vector<float> first = play(*fresh, {on(0, 52, 1.f)}, 9600);
    CHECK(again == first);
}

// Nothing the audio thread calls allocates.
static void testNoAllocation() {
    std::unique_ptr<Acid> a = synth();
    std::vector<float> l(256), r(256);
    const long before = gNews;
    a->noteOn(48, 0.9f);
    a->render(l.data(), r.data(), 256);
    a->setMacro(MacroCutoff, 0.7f);
    a->setSetting(Resonance, 90);
    a->noteOnFrom(60, 1.f, 48, 0.05f);
    a->render(l.data(), r.data(), 256);
    a->noteOff(60);
    a->allNotesOff();
    a->selectPreset(1);
    a->render(l.data(), r.data(), 256);
    CHECK(gNews == before);
}

// Play from the top is the same line: after allNotesOff (and its 3 ms), the line again is a fresh
// synth's, to the bit.
static void testPlayFromTheTop() {
    std::unique_ptr<Acid> a = synth({{Resonance, 85}});
    play(*a, busyLine(), 46000);
    a->allNotesOff();
    play(*a, {}, 200);
    for (int m = 0; m < kMacros; ++m) a->setMacro(m, 0.5f); // (the song's knobs as they were at the top)
    const std::vector<float> again = play(*a, busyLine(), 60000);
    std::unique_ptr<Acid> fresh = synth({{Resonance, 85}});
    CHECK(again == play(*fresh, busyLine(), 60000));
}

// The cutoff worked out on its tick against every sample: an accent's fastest fall with ENV MOD
// and ACCENT full, the difference in dB under the sound.
static void testTuneTick() {
    const std::vector<Set> sets = {{Accent, 100}, {EnvMod, 100}, {Resonance, 85}, {Sweep, 100}, {Cutoff, 20}};
    std::unique_ptr<Acid> a = synth(sets), b = synth(sets);
    b->setTuneTickForTests(1);
    const std::vector<Ev> line = {on(0, 36, 1.f), on(6000, 36, 1.f), on(12000, 48, 1.f), off(20000, 48)};
    const std::vector<float> x = play(*a, line, 24000), y = play(*b, line, 24000);
    std::vector<float> d(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) d[i] = x[i] - y[i];
    const double under = dB(rmsOf(d) / rmsOf(y));
    CHECK(under < -40.0);
    if (gPrint) std::printf("  the cutoff on its tick against every sample: %.1f dB under the sound\n", under);
}

// ---- batch 2: slide and accent --------------------------------------------------------------

// The slide's time, measured: the pitch at tau, 2 tau, the sample before it lands and the landing,
// against the rule (to a millionth of a cent), and the note sounding on its key after.
static void testSlideTime() {
    for (int kind : {kSlideNote, kSlideFixed}) {
        std::unique_ptr<Acid> a = synth({{AmpDecay, 100}, {acid::Slide, kind}, {EnvMod, 0}, {Cutoff, 100}, {Resonance, 0}}); // (a still filter: its phase does not move the pitch read)
        const float given = 0.0625f;
        const double time = slideSeconds(kind, given);
        const long n = slideSamples(time, kRate);
        play(*a, {on(0, 48)}, 6000);
        a->noteOnFrom(60, 0.9f, 48, given);
        std::vector<double> pitch;
        std::vector<float> l(1), r(1);
        for (long i = 0; i <= n + 10; ++i) {
            a->render(l.data(), r.data(), 1);
            pitch.push_back(a->now().pitchOctaves);
        }
        const double target = (60 - 69) / 12.0;
        const auto want = [&](long i) { return target - 1.0 * slideLeft(i / kRate, time); };
        double worst = 0.0;
        for (long i : {0L, std::lround(time / 3 * kRate), std::lround(2 * time / 3 * kRate), n - 1, n, n + 5}) {
            worst = std::max(worst, std::fabs(pitch[static_cast<std::size_t>(i)] - want(i)) * 1200.0);
        }
        CHECK(worst < 1e-6);
        CHECK(pitch[static_cast<std::size_t>(n)] == target && pitch[static_cast<std::size_t>(n - 1)] != target);
        CHECK(a->ties() == 1 && a->strikes() == 1);
        const std::vector<float> out = play(*a, {}, 48000);
        const double c = centsOff(bandOf(out, hzOf(60)), hzOf(60), 0.2, 60.0);
        CHECK(std::fabs(c) < 0.5);
        if (gPrint)
            std::printf("  SLIDE %s: %.1f ms (%ld samples); at tau %.1f cents to go, at 2 tau %.1f; %.2g cents from the rule at worst; then %.3f cents off the key\n",
                        slideName(kind), time * 1000.0, n, (pitch[static_cast<std::size_t>(std::lround(time / 3 * kRate))] - target) * 1200.0,
                        (pitch[static_cast<std::size_t>(std::lround(2 * time / 3 * kRate))] - target) * 1200.0, worst, c);
    }
}

// The tie: no envelope starts again on a slide, the notes end to end in either order on one
// sample; after a rest the envelopes start again (and the pitch still slides).
static void testTie() {
    const std::vector<Set> sets = {{Decay, 60}, {AmpDecay, 60}};
    // What the envelopes do with no second note at all.
    std::unique_ptr<Acid> alone = synth(sets);
    play(*alone, {on(0, 48)}, 9000);
    // (a) the gate open; (b) the note before let go first on the same sample; (c) after it.
    const std::vector<std::vector<Ev>> lines = {{on(0, 48), from(6000, 60, 48, 0.06f)},
                                                {on(0, 48), off(6000, 48), from(6000, 60, 48, 0.06f)},
                                                {on(0, 48), from(6000, 60, 48, 0.06f), off(6000, 48)},
                                                {on(0, 48), off(5808, 48), from(6000, 60, 48, 0.06f)}}; // shut 4 ms before
    for (const std::vector<Ev>& line : lines) {
        std::unique_ptr<Acid> a = synth(sets);
        play(*a, line, 9000);
        CHECK(a->ties() == 1 && a->strikes() == 1 && a->gateOpen() && a->key() == 60);
        CHECK(a->now().filterEnv == alone->now().filterEnv); // not started again, to the bit
        if (&line != &lines.back()) CHECK(a->now().amp == alone->now().amp);
    }
    // After a rest of 10 ms: the envelopes start again, the pitch still slides.
    std::unique_ptr<Acid> b = synth(sets);
    play(*b, {on(0, 48), off(5520, 48), from(6000, 60, 48, 0.06f)}, 6001);
    CHECK(b->ties() == 0 && b->strikes() == 2 && b->now().filterEnv > 0.99);
    CHECK(near(b->now().pitchOctaves, (48 - 69) / 12.0, 1e-9));
    // Without GLIDE, overlap or not: started again, the pitch at once.
    std::unique_ptr<Acid> c = synth(sets);
    play(*c, {on(0, 48), on(6000, 60)}, 6001);
    CHECK(c->ties() == 0 && c->strikes() == 2 && near(c->now().pitchOctaves, (60 - 69) / 12.0, 1e-12));
}

// ACCENT: louder by its dB, the filter's shortest decay, and the sweep on the cutoff.
static void testAccent() {
    std::unique_ptr<Acid> plain = synth({{Accent, 100}, {Decay, 100}}), acc = synth({{Accent, 100}, {Decay, 100}});
    play(*plain, {on(0, 48, 0.9f)}, 9600);
    play(*acc, {on(0, 48, 1.f)}, 9600);
    CHECK(plain->now().level == 1.0 && acc->now().level == static_cast<float>(accentGain(100)));
    CHECK(near(20.0 * std::log10(acc->now().level / plain->now().level), kAccentMostDb, 1e-5));
    // The decay: 0.2 s to a tenth on the accent, DECAY's own (2 s) on the plain note.
    CHECK(near(acc->now().filterEnv, 0.1, 0.0005));
    CHECK(near(plain->now().filterEnv, 0.1 / std::pow(0.1, 1.0 - 0.1), 0.002)); // 0.2 s of 2 s: 0.1^0.1
    // VELOCITY ON: a plain note's level is its velocity; an accent is ACCENT over it.
    std::unique_ptr<Acid> v = synth({{Velocity, 1}, {Accent, 50}});
    play(*v, {on(0, 48, 0.5f)}, 480);
    CHECK(v->now().level == 0.5f);
    // Heard: the accented note against the plain one, CUTOFF 50, no envelope, RESONANCE 0.
    const std::vector<Set> sets = {{Accent, 100}, {Cutoff, 50}, {EnvMod, 0}, {Resonance, 0}, {Sweep, 0}};
    std::unique_ptr<Acid> p2 = synth(sets), a2 = synth(sets);
    const std::vector<float> po = play(*p2, {on(0, 36, 0.9f)}, 4800), ao = play(*a2, {on(0, 36, 1.f)}, 4800);
    const double louder = dB(rmsOf(ao, 480, 4800) / rmsOf(po, 480, 4800));
    CHECK(louder > kAccentMostDb && louder < kAccentMostDb + 8.0);
    // An accent on a tie: its level glides over 3 ms (no step), and no new sweep is fed.
    std::unique_ptr<Acid> t = synth({{Accent, 100}}), u = synth({{Accent, 100}});
    play(*t, {on(0, 48, 0.9f)}, 6000);
    play(*u, {on(0, 48, 0.9f)}, 6000);
    float last = static_cast<float>(t->now().level), biggest = 0.f;
    t->noteOnFrom(55, 1.f, 48, 0.05f);
    for (int i = 0; i < 200; ++i) {
        play(*t, {}, 1);
        biggest = std::max(biggest, std::fabs(static_cast<float>(t->now().level) - last));
        last = static_cast<float>(t->now().level);
    }
    play(*u, {}, 200);
    CHECK(last == static_cast<float>(accentGain(100)) && biggest < 1.1f * static_cast<float>(accentGain(100) - 1.0) / 144.f);
    CHECK(t->now().sweep == 0.0 && t->now().filterEnv == u->now().filterEnv);
    // A strike over a sounding note glides its level too (no step); from silence it is there at once.
    std::unique_ptr<Acid> w = synth({{Accent, 100}});
    play(*w, {on(0, 48, 0.9f)}, 6000);
    play(*w, {on(0, 50, 1.f)}, 1);
    CHECK(w->now().level > 1.0 && w->now().level < 1.0 + 1.1 * (accentGain(100) - 1.0) / 144.0);
    std::unique_ptr<Acid> z = synth({{Accent, 100}});
    play(*z, {on(0, 50, 1.f)}, 1);
    CHECK(z->now().level == static_cast<float>(accentGain(100)));
    if (gPrint) std::printf("  accent: %.1f dB louder heard (ACCENT 100, CUTOFF 50: its level and its sweep)\n", louder);
}

// The highest the cutoff goes over its base for each of four accents `apart` samples apart.
static std::vector<double> sweepPeaks(long apart, int sweep) {
    std::unique_ptr<Acid> a = synth({{Accent, 100}, {Sweep, sweep}, {Resonance, 60}, {EnvMod, 0}, {Cutoff, 30}});
    std::vector<double> peaks;
    const double base = cutoffHz(30);
    for (int n = 0; n < 4; ++n) {
        play(*a, {on(0, 36, 1.f), off(apart * 4 / 5, 36)}, 1);
        double top = 0.0;
        for (long i = 1; i < apart; ++i) {
            play(*a, {}, 1);
            top = std::max(top, std::log2(a->now().cutoffHz / base));
        }
        peaks.push_back(top);
    }
    return peaks;
}
// The sweep climbs over four accents 125 ms apart, and not over four a second apart; one accent
// alone opens the cutoff kAccentOctaves x ACCENT whatever SWEEP; at SWEEP 0 nothing climbs.
static void testSweepClimbs() {
    const std::vector<double> close = sweepPeaks(6000, 50), apart = sweepPeaks(48000, 50), none = sweepPeaks(6000, 0);
    CHECK(near(close[0], kAccentOctaves, 0.03 * kAccentOctaves) && near(none[0], kAccentOctaves, 0.01));
    CHECK(close[1] > close[0] && close[2] > close[1] && close[3] > close[2] && close[3] > 1.25 * close[0]);
    CHECK(near(apart[3], apart[0], 0.01 * apart[0]));
    CHECK(near(none[3], none[0], 1e-6));
    if (gPrint)
        std::printf("  the sweep, four accents 125 ms apart: %.2f %.2f %.2f %.2f octaves; a second apart: %.2f .. %.2f; SWEEP 0: %.2f .. %.2f\n", close[0], close[1],
                    close[2], close[3], apart[0], apart[3], none[0], none[3]);
}

// ACCENT 0 (by the setting, or by its macro): an accented note is a plain one, to the bit.
static void testAccentZero() {
    const auto line = [](float vel) {
        return std::vector<Ev>{on(0, 36, vel), from(3000, 48, 36, 0.05f, vel), off(6000, 48), on(9000, 43, vel), on(15000, 31, vel)};
    };
    std::unique_ptr<Acid> a = synth({{Accent, 0}}), b = synth({{Accent, 0}});
    CHECK(play(*a, line(1.f), 24000) == play(*b, line(0.9f), 24000));
    std::unique_ptr<Acid> c = synth({{Accent, 70}}), d = synth({{Accent, 70}});
    c->setMacro(MacroAccent, 0.f);
    d->setMacro(MacroAccent, 0.f);
    CHECK(play(*c, line(1.f), 24000) == play(*d, line(0.9f), 24000));
    // (and with ACCENT up they differ)
    std::unique_ptr<Acid> e = synth({{Accent, 70}}), f = synth({{Accent, 70}});
    CHECK(play(*e, line(1.f), 24000) != play(*f, line(0.9f), 24000));
}

// A note written into scene 1, bar 1 of track 1 (engine_test's).
static Command noteAt(int slot, int stepIndex, int pitch, float length, int effects = 0, float velocity = 0.9f) {
    Command c;
    c.type = Command::SetNote;
    c.track = 1;
    c.a = slot;
    c.b = stepIndex;
    c.value = velocity;
    c.c = pitch;
    c.d = static_cast<int>(length);
    c.extra = length - static_cast<float>(c.d);
    c.e = effects;
    return c;
}
// Through the engine: a line end to end with GLIDE ties (whichever comes first on the step's
// sample, the note off or the note), a GLIDE after a rest starts again, under GLIDE FROM TOP and
// PAIRED; and Play again is the same line.
static void testThroughTheEngine() {
    const int kStep = 6000;
    for (int fromKind : {kFromTop, kFromPaired}) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1, SynthKind::Acid));
        Command g;
        g.type = Command::SetGlideFrom, g.track = 1, g.a = fromKind;
        engine->send(g);
        engine->send(noteAt(0, 0, 48, 1.f));
        engine->send(noteAt(1, 1, 60, 1.f, 256, 1.f));  // a slide, an accent
        engine->send(noteAt(2, 2, 55, 1.5f, 256));      // a slide, and held into the next
        engine->send(noteAt(3, 3, 67, 1.f, 256));       // a slide over an open gate
        engine->send(noteAt(4, 5, 72, 1.f, 256));       // after a rest: starts again (and slides)
        engine->send(noteAt(5, 7, 36, 1.f));            // plain
        Command p;
        p.type = Command::Play;
        engine->send(p);
        std::vector<float> first(2 * 8 * kStep), second(2 * 8 * kStep);
        engine->render(first.data(), 8 * kStep);
        const Acid* acid = dynamic_cast<const Acid*>(engine->synthForTests(1));
        CHECK(acid && acid->ties() == 3 && acid->strikes() == 3);
        if (acid && gPrint) std::printf("  through the engine (GLIDE FROM %d): %ld ties, %ld strikes\n", fromKind, acid->ties(), acid->strikes());
        Command s;
        s.type = Command::Stop;
        engine->send(s);
        std::vector<float> rest(2 * 48000);
        engine->render(rest.data(), 48000);
        engine->send(p);
        engine->render(second.data(), 8 * kStep);
        CHECK(first == second);
    }
}

// What the voice costs: a 16-step line at 130 BPM with accents and slides, RESONANCE 85 (the
// plan's bench row), timed in blocks of 256.
static void measureCost(int envMod = 70, int accent = 50) {
    std::unique_ptr<Acid> a = synth({{Resonance, 85}, {EnvMod, envMod}, {Decay, 30}, {Accent, accent}});
    const double step = 60.0 / 130.0 / 4.0 * kRate;
    std::vector<float> l(256), r(256);
    const int keys[16] = {36, 36, 48, 36, 39, 36, 51, 43, 36, 48, 36, 46, 36, 34, 41, 36};
    double best = 1e9;
    for (int run = 0; run < 5; ++run) {
        long frame = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int s = 0; s < 16 * 20; ++s) {
            const int k = keys[s % 16];
            if (s % 3 == 1) a->noteOnFrom(k, s % 4 == 0 ? 1.f : 0.9f, keys[(s + 15) % 16], 0.06f);
            else a->noteOn(k, s % 4 == 0 ? 1.f : 0.9f);
            const long until = std::lround((s + 1) * step);
            while (frame < until) {
                const int n = static_cast<int>(std::min<long>(256, until - frame));
                a->render(l.data(), r.data(), n);
                frame += n;
            }
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, secs / (frame / kRate));
    }
    std::printf("  cost: a busy line at 130 BPM, %.3f%% of one core (the plan's target 0.15%%), %ld tunings\n", 100.0 * best, a->tunings());
}

// ---- the program ---------------------------------------------------------------------------

struct Named { const char* name; void (*run)(); };

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testSettings", testSettings},
        {"testRule", testRule},
        {"testSlideShape", testSlideShape},
        {"testReadings", testReadings},
        {"testEdge", testEdge},
        {"testPitch", testPitch},
        {"testSlope", testSlope},
        {"testSinging", testSinging},
        {"testAliasing", testAliasing},
        {"testEnvelopes", testEnvelopes},
        {"testNewestWins", testNewestWins},
        {"testTail", testTail},
        {"testTuneTick", testTuneTick},
        {"testBlocks", testBlocks},
        {"testTooSmall", testTooSmall},
        {"testNaN", testNaN},
        {"testSilence", testSilence},
        {"testNoAllocation", testNoAllocation},
        {"testPlayFromTheTop", testPlayFromTheTop},
        {"testSlideTime", testSlideTime},
        {"testTie", testTie},
        {"testAccent", testAccent},
        {"testSweepClimbs", testSweepClimbs},
        {"testAccentZero", testAccentZero},
        {"testThroughTheEngine", testThroughTheEngine},
    };
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--measure") gPrint = true;
        else only.push_back(a);
    }
    for (const Named& t : tests) {
        if (!only.empty() && std::find(only.begin(), only.end(), t.name) == only.end()) continue;
        t.run();
    }
    if (gPrint && only.empty()) measureCost();
    if (gPrint && only.size() == 1 && only[0] == "cost") measureCost(0, 0), measureCost();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("acid tests passed\n");
    return 0;
}
