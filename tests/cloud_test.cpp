// CLOUD (docs/plans/granular-plan.md), the engine part: the rule header (batch 0) and the synth
// (batch 1), through the synth and through the engine, and the house rules (blocks, helpers, Play
// from the top, no allocation, no number too small to hold, NaN, silence, a recording swapped).
//
// Every figure CLOUD holds is SELF (the plan's own choice). A green test only says the code does
// what the plan chose. NOBODY HAS HEARD ANY OF IT.
//
//   cloud_test                  all of them
//   cloud_test testRule ...     just those
//   cloud_test --measure        the figures (and the cost)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "apotheca/cloud.hpp"
#include "apotheca/cloud_rule.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/physic_rule.hpp"
#include "apotheca/envelope.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::cloud;

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
static const double kRate = 48000.0;

// ---- batch 0: the rule ----------------------------------------------------------------------

// The settings table: ids, ranges, usual values (the plan's section 1).
static void testSettings() {
    CHECK(kSettings == 17 && kSettingRoom >= kSettings);
    CHECK(Position == 0 && Scan == 10 && Root == 11 && Velocity == 16);
    for (int id = 0; id < kSettings; ++id) {
        CHECK(settingMin(id) <= settingUsual(id) && settingUsual(id) <= settingMax(id));
        CHECK(settingHeld(id, -1000) == settingMin(id) && settingHeld(id, 1000) == settingMax(id));
    }
    CHECK(settingMin(Tune) == -100 && settingMax(Tune) == 100);
    CHECK(settingMax(PitchSteps) == 2 && settingMax(Root) == 127 && settingMax(Level) == 100);
    CHECK(settingUsual(Root) == 60 && settingUsual(Scan) == 50 && settingUsual(PitchSteps) == kStepsFree);
    CHECK(std::strcmp(stepsName(kStepsFree), "FREE") == 0 && std::strcmp(stepsName(kStepsOctaves), "OCTAVES") == 0 &&
          std::strcmp(stepsName(kStepsFifths), "FIFTHS") == 0);
    // The macros: eight, none a choice, each its setting.
    const int want[kMacros] = {Position, Spray, Size, Density, Scan, PitchSpray, Shape, Reverse};
    for (int m = 0; m < kMacros; ++m) CHECK(macroSetting(m) == want[m]);
    CHECK(macroBent(30.f, 0.5f, 0.f, 100.f) == 30.f && macroBent(30.f, 0.f, 0.f, 100.f) == 0.f &&
          macroBent(30.f, 1.f, 0.f, 100.f) == 100.f && near(macroBent(30.f, 0.75f, 0.f, 100.f), 65.f, 1e-4));
}

// The readings at both ends and at their usual values (the plan's section 1).
static void testReadings() {
    CHECK(positionShare(0) == 0.0 && positionShare(100) == 1.0 && positionShare(25) == 0.25 && positionShare(150) == 1.0);
    CHECK(sprayShare(0) == 0.0 && near(sprayShare(100), 0.5, 1e-12) && near(sprayShare(10), 0.005, 1e-12));
    CHECK(near(spraySeconds(100, 4.0), 2.0, 1e-12) && near(spraySeconds(50, 4.0), 0.5, 1e-12));
    CHECK(near(sizeSeconds(0), 0.005, 1e-12) && near(sizeSeconds(100), 1.0, 1e-12));
    CHECK(near(sizeSeconds(settingUsual(Size)), 0.120, 0.001)); // reads 120 MS
    CHECK(near(densityHz(0), 1.0, 1e-12) && near(densityHz(100), 200.0, 1e-9));
    CHECK(near(densityHz(settingUsual(Density)), 16.0, 0.3)); // reads 16 HZ
    CHECK(sizeSeconds(-5) == sizeSeconds(0) && densityHz(500) == densityHz(100));
    CHECK(pitchSprayCents(0) == 0.0 && near(pitchSprayCents(100), 1200.0, 1e-9) && near(pitchSprayCents(50), 300.0, 1e-9));
    CHECK(scanTimes(50) == 0.0 && scanTimes(100) == 2.0 && scanTimes(0) == -2.0 && scanTimes(75) == 1.0);
    CHECK(levelDb(100) == 0.0 && near(levelDb(50), -6.0206, 1e-3) && std::isinf(levelDb(0)) && levelDb(0) < 0.0);
    CHECK(velocityGain(0, 0.2) == 1.0 && near(velocityGain(100, 0.2), 0.2, 1e-12) && near(velocityGain(50, 0.2), 0.6, 1e-12));
    CHECK(velocityGain(100, 2.0) == 1.0 && velocityGain(100, -1.0) == 0.0);
    CHECK(near(envelope::attackSeconds(static_cast<float>(settingUsual(Attack))), 0.002, 0.0002)); // reads 2 MS
    CHECK(near(envelope::fallSeconds(static_cast<float>(settingUsual(Release))), 0.29, 0.01));     // reads 290 MS
    if (gPrint)
        std::printf("  usual: SIZE %.1f ms, DENSITY %.2f Hz, ATTACK %.2f ms, RELEASE %.0f ms\n", 1000 * sizeSeconds(settingUsual(Size)),
                    densityHz(settingUsual(Density)), 1000 * envelope::attackSeconds(static_cast<float>(settingUsual(Attack))),
                    1000 * envelope::fallSeconds(static_cast<float>(settingUsual(Release))));
}

// grainUnit: in 0..1, a different number for each grain, each roll and each seed; the same for the
// same three; spread evenly (the mean and the quarters of 40000 rolls).
static void testRolls() {
    CHECK(grainUnit(7, 3, kGrainPlace) == grainUnit(7, 3, kGrainPlace));
    CHECK(grainUnit(7, 3, kGrainPlace) != grainUnit(7, 4, kGrainPlace));
    CHECK(grainUnit(7, 3, kGrainPlace) != grainUnit(8, 3, kGrainPlace));
    CHECK(grainUnit(7, 3, kGrainPlace) != grainUnit(7, 3, kGrainTime));
    double sum = 0.0;
    int quarter[4] = {0, 0, 0, 0}, same = 0;
    for (std::uint32_t n = 0; n < 40000; ++n) {
        const double u = grainUnit(12345u, n, kGrainTime);
        CHECK(u >= 0.0 && u < 1.0);
        sum += u;
        ++quarter[static_cast<int>(u * 4.0)];
        if (u == grainUnit(12345u, n, kGrainSide)) ++same;
    }
    CHECK(near(sum / 40000.0, 0.5, 0.01));
    for (int q : quarter) CHECK(q > 9500 && q < 10500);
    CHECK(same < 5);
    CHECK(grainSigned(1, 2, kGrainBack) == 2.0 * grainUnit(1, 2, kGrainBack) - 1.0);
    CHECK(liveSeed(0) != liveSeed(1));
}

// The onset wait: whole samples, the mean gap at SCATTER 0 exactly, stretched by SCATTER, at least 1.
static void testOnsets() {
    CHECK(onsetWait(kRate, 16.0, 0.0, 0.9) == 3000 && onsetWait(kRate, 16.0, 0.0, 0.1) == 3000);
    CHECK(onsetWait(kRate, 16.0, 1.0, 0.0) == 1);    // the shortest: at least a sample
    CHECK(onsetWait(kRate, 16.0, 1.0, 0.75) == 4500);
    CHECK(onsetWait(kRate, 16.0, 0.5, 0.0) == 1500);
    CHECK(onsetWait(kRate, 200.0, 0.0, 0.5) == 240 && onsetWait(kRate, 1.0, 0.0, 0.5) == 48000);
    CHECK(onsetWait(kRate, 7.0, 0.0, 0.5) == 6857); // 6857.14 rounded
    // The mean over many rolls is the gap, at any SCATTER.
    for (double scatter : {0.0, 0.5, 1.0}) {
        double sum = 0.0;
        for (std::uint32_t n = 0; n < 20000; ++n) sum += static_cast<double>(onsetWait(kRate, 50.0, scatter, grainUnit(99u, n, kGrainTime)));
        CHECK(near(sum / 20000.0, 960.0, 960.0 * 0.01));
    }
}

// The window: two half cosines, exactly 0 at the grain's first sample and the sample after its
// last, 1 at its peak, each piece at least half a millisecond, at SHAPE 0, 50 and 100.
static void testWindow() {
    CHECK(windowFloor(kRate) == 24 && windowFloor(44100.0) == 22 && windowFloor(10.0) == 1);
    CHECK(grainSamples(0.120, kRate) == 5760 && grainSamples(0.0001, kRate) == 48);
    for (double share : {0.0, 0.5, 1.0}) {
        const long n = grainSamples(sizeSeconds(0), kRate); // the shortest grain, 5 ms
        const long rise = riseSamples(n, share, kRate), fall = n - rise;
        CHECK(rise >= 24 && fall >= 24);
        CHECK(windowAt(0, rise, fall) == 0.0 && windowAt(n, rise, fall) == 0.0);
        CHECK(windowAt(rise, rise, fall) == 1.0);
        double most = 0.0;
        float jump = 0.f, last = 0.f;
        for (long k = 0; k <= n; ++k) {
            const double w = windowAt(k, rise, fall);
            CHECK(w >= 0.0 && w <= 1.0);
            most = std::max(most, w);
            jump = std::max(jump, std::fabs(static_cast<float>(w) - last));
            last = static_cast<float>(w);
        }
        CHECK(most == 1.0);
        CHECK(jump < 0.07f); // a half cosine over 24 samples steps at most pi/48 a sample
    }
    CHECK(riseSamples(5760, 0.0, kRate) == 24 && riseSamples(5760, 1.0, kRate) == 5736 && riseSamples(5760, 0.5, kRate) == 2880);
    // The synth's polynomial is the half cosine within 4e-6, exactly 0 and 1 at its ends, and does not
    // go down on the way up (but by a float's step).
    double worst = 0.0;
    float before = -1.f;
    bool rises = true;
    for (int i = 0; i <= 100000; ++i) {
        const double x = i / 100000.0;
        const float w = windowPoly(static_cast<float>(x));
        worst = std::max(worst, std::fabs(w - halfCosine(x)));
        rises = rises && w >= before - 1e-7f; // (a float's step at the top, once)
        before = w;
    }
    CHECK(worst < 4e-6 && rises);
    CHECK(windowPoly(0.f) == 0.f && windowPoly(1.f) == 1.f);
    // Either piece's mean square is 3/8.
    double ms = 0.0;
    for (int i = 0; i < 100000; ++i) { const double w = halfCosine((i + 0.5) / 100000.0); ms += w * w; }
    CHECK(near(ms / 100000.0, kWindowPower, 1e-6));
    if (gPrint) std::printf("  the window's polynomial: %.2g at worst from the half cosine\n", worst);
}

// The overlap gain and the sides.
static void testLevel() {
    CHECK(near(overlapGain(16.0, 0.120), 1.0 / std::sqrt(1.92), 1e-12)); // 1.92 grains
    CHECK(near(overlapGain(16.0, 0.125), 1.0 / std::sqrt(2.0), 1e-12));
    CHECK(overlapGain(1.0, 0.005) == 1.0 && overlapGain(200.0, 0.001) == 1.0); // sparse: full
    CHECK(near(overlapGain(200.0, 1.0), 1.0 / std::sqrt(12.0), 1e-12) && near(overlapGain(100.0, 0.1), 1.0 / std::sqrt(10.0), 1e-12)); // a key sounds 12 at most
    double l = 0, r = 0;
    sideGains(0.0, 0.9, &l, &r);
    CHECK(near(l, std::sqrt(0.5), 1e-12) && near(r, std::sqrt(0.5), 1e-12));
    sideGains(1.0, 0.0, &l, &r);
    CHECK(near(l, 1.0, 1e-12) && near(r, 0.0, 1e-12));
    sideGains(1.0, 0.75, &l, &r);
    CHECK(near(l * l + r * r, 1.0, 1e-12) && r > l);
}

// PITCH STEPS' grid, the pitch roll and a grain's ratio.
static void testPitchRule() {
    CHECK(steppedSemitones(3.3, kStepsFree) == 3.3);
    CHECK(steppedSemitones(5.9, kStepsOctaves) == 0.0 && steppedSemitones(6.1, kStepsOctaves) == 12.0 && steppedSemitones(-6.1, kStepsOctaves) == -12.0);
    CHECK(steppedSemitones(3.4, kStepsFifths) == 0.0 && steppedSemitones(3.6, kStepsFifths) == 7.0 && steppedSemitones(9.4, kStepsFifths) == 7.0);
    CHECK(steppedSemitones(9.6, kStepsFifths) == 12.0 && steppedSemitones(-8.0, kStepsFifths) == -7.0 && steppedSemitones(-11.0, kStepsFifths) == -12.0);
    CHECK(steppedSemitones(18.0, kStepsFifths) == 19.0 && steppedSemitones(-23.0, kStepsFifths) == -24.0);
    CHECK(pitchRoll(0.0, kStepsFree, 0.9) == 0.0 && near(pitchRoll(1200.0, kStepsFree, 1.0), 12.0, 1e-12) && near(pitchRoll(1200.0, kStepsFree, 0.0), -12.0, 1e-12));
    CHECK(pitchRoll(1200.0, kStepsOctaves, 0.8) == 12.0 && pitchRoll(1200.0, kStepsOctaves, 0.6) == 0.0);
    // Every roll lands on the grid.
    for (std::uint32_t n = 0; n < 1000; ++n) {
        const double s = pitchRoll(1200.0, kStepsFifths, grainUnit(5u, n, kGrainPitch));
        CHECK(s == 0.0 || s == 7.0 || s == -7.0 || s == 12.0 || s == -12.0);
    }
    CHECK(grainRatio(60, 60, 0.0, 0.0, 0.0) == 1.0);
    CHECK(grainRatio(72, 60, 0.0, 0.0, 0.0) == 2.0 && grainRatio(48, 60, 0.0, 0.0, 0.0) == 0.5);
    CHECK(near(grainRatio(60, 60, 100.0, 0.0, 0.0), std::pow(2.0, 1.0 / 12.0), 1e-12));
    CHECK(near(grainRatio(60, 60, 0.0, -12.0, 0.0), 0.5, 1e-12) && near(grainRatio(60, 60, 0.0, 0.0, 12.0), 2.0, 1e-12));
    CHECK(grainRatio(60, 48, 0.0, 0.0, 0.0) == 2.0);
}

// The wrap and SCAN's place.
static void testPlace() {
    CHECK(wrapPlace(5.0, 10.0) == 5.0 && wrapPlace(12.5, 10.0) == 2.5 && wrapPlace(-2.5, 10.0) == 7.5);
    CHECK(wrapPlace(10.0, 10.0) == 0.0 && wrapPlace(-10.0, 10.0) == 0.0 && wrapPlace(-1e-20, 10.0) < 10.0);
    CHECK(wrapPlace(31.0, 10.0) == 1.0 && wrapPlace(3.0, 0.0) == 0.0);
    for (int i = -50; i < 50; ++i) {
        const double w = wrapPlace(i * 0.731, 3.0);
        CHECK(w >= 0.0 && w < 3.0);
    }
    CHECK(scanned(1.0, 48000) == 48000.0 && scanned(-0.5, 1000) == -500.0 && scanned(0.0, 99999) == 0.0);
    CHECK(grainPlace(0.25, 0.0, 0.0, 0.9, 1000.0) == 250.0);
    CHECK(grainPlace(0.25, 100.0, 0.0, 0.9, 1000.0) == 350.0);
    CHECK(grainPlace(0.25, 0.0, 0.5, 1.0, 1000.0) == 750.0 && grainPlace(0.25, 0.0, 0.5, 0.0, 1000.0) == 750.0); // -250 wraps to 750
    CHECK(grainPlace(0.9, 300.0, 0.0, 0.5, 1000.0) == 200.0);
}


// ---- batch 1: the synth -------------------------------------------------------------------

struct Set { int id, value; };
struct Ev {
    long at;
    int note;
    float velocity; // 0: let go
    int from = -1;  // a glide from this key
    float seconds = 0.f;
    std::uint32_t seed = 0; // handed over first when not 0
};
static Ev on(long at, int note, float v = 0.9f, std::uint32_t seed = 0) { Ev e{at, note, v}; e.seed = seed; return e; }
static Ev off(long at, int note) { return Ev{at, note, 0.f}; }
static Ev glide(long at, int note, int from, float seconds) { Ev e{at, note, 0.9f}; e.from = from; e.seconds = seconds; return e; }

// Recordings the tests read.
static std::vector<float> sineRecording(double hz, double seconds, double amp = 0.5) { return sineOf(hz, amp, static_cast<std::size_t>(seconds * kRate)); }
static std::vector<float> noiseRecording(double seconds) {
    std::vector<float> v(static_cast<std::size_t>(seconds * kRate));
    Noise noise(7);
    for (float& x : v) x = 0.5f * noise.next();
    return v;
}

static std::unique_ptr<Cloud> synth(const std::vector<Set>& sets = {}, const std::vector<float>* recording = nullptr) {
    std::unique_ptr<Cloud> c(new Cloud);
    c->prepare(static_cast<float>(kRate));
    for (const Set& x : sets) c->setSetting(x.id, x.value);
    if (recording) c->setSample(recording->data(), static_cast<int>(recording->size()));
    return c;
}

// Plays the events (each on its own sample) and `frames` in all, in blocks of `block`: the left
// side then the right, one after the other.
static std::vector<float> play(Cloud& c, const std::vector<Ev>& events, long frames, int block = 256) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    long frame = 0;
    std::size_t next = 0;
    while (frame < frames) {
        while (next < events.size() && events[next].at <= frame) {
            const Ev& e = events[next++];
            if (e.velocity <= 0.f) { c.noteOff(e.note); continue; }
            if (e.seed) c.noteSeed(e.seed);
            if (e.from >= 0) c.noteOnFrom(e.note, e.velocity, e.from, e.seconds);
            else c.noteOn(e.note, e.velocity);
        }
        long until = frames;
        if (next < events.size() && events[next].at < until) until = events[next].at;
        const int n = static_cast<int>(std::min<long>(block, until - frame));
        c.render(&l[static_cast<std::size_t>(frame)], &r[static_cast<std::size_t>(frame)], n);
        frame += n;
    }
    l.insert(l.end(), r.begin(), r.end());
    return l;
}
static std::vector<float> leftOf(const std::vector<float>& both) { return std::vector<float>(both.begin(), both.begin() + both.size() / 2); }
static bool allFinite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

// GRAIN TIMING TO THE SAMPLE: a recording of one click, read at each grain's peak, so each grain is
// one sample out, exactly where the rule puts its onset plus its rise.
static void testTiming() {
    std::vector<float> click(48000, 0.f);
    click[24000] = 1.f;
    // SIZE 60: 5760 samples, SHAPE 50: its peak 2880 in, so POSITION 44 (21120) reads the click there.
    std::unique_ptr<Cloud> c = synth({{Position, 44}, {Spray, 0}, {Scatter, 50}, {Attack, 0}, {Release, 100}}, &click);
    const std::uint32_t seed = 0xC10D;
    const long strike = 1000, frames = 3 * 48000;
    const std::vector<float> l = leftOf(play(*c, {on(strike, 60, 0.9f, seed)}, frames, 100));
    std::vector<long> want;
    long at = strike;
    for (std::uint32_t n = 0; at + 2880 < frames; ++n) {
        want.push_back(at + 2880);
        at += onsetWait(kRate, densityHz(settingUsual(Density)), 0.5, grainUnit(seed, n, kGrainTime));
    }
    std::vector<long> heard;
    for (long i = 0; i < frames; ++i)
        if (l[static_cast<std::size_t>(i)] != 0.f) heard.push_back(i);
    CHECK(heard == want);
    CHECK(want.size() > 40);
    if (gPrint) std::printf("  timing: %zu grains, each on its sample (%zu heard)\n", want.size(), heard.size());
}

// DENSITY measured: onsets over 10 s exactly the rate at SCATTER 0 (the first at the strike), and
// over 60 s at SCATTER 50 and 100 within 1% where there are thousands (each gap is rolled: the mean
// is the rate).
static void testDensity() {
    for (int dial : {10, 52, 80, 100})
        for (int scatter : {0, 50, 100}) {
            const int seconds = scatter == 0 ? 10 : 60;
            std::unique_ptr<Cloud> c = synth({{Density, dial}, {Scatter, scatter}, {Size, 0}, {Release, 100}});
            play(*c, {on(0, 60, 0.9f, 77u + static_cast<std::uint32_t>(dial))}, seconds * 48000L, 4096);
            const double onsets = static_cast<double>(c->grainsStarted() + c->grainsSkipped());
            const double want = seconds * densityHz(dial);
            // Rolled gaps: a count of N is N within sqrt(N / 3) x SCATTER (one sigma); 1% where that is
            // under a third of it, three sigma where it is not (a few dozen onsets).
            const double sigma = scatterShare(scatter) * std::sqrt(want / 3.0);
            if (scatter == 0) CHECK(std::fabs(onsets - want) <= 1.0);
            else CHECK(std::fabs(onsets - want) <= std::max(0.01 * want, 3.0 * sigma));
            if (want > 4000.0 && scatter > 0) CHECK(std::fabs(onsets - want) <= 0.01 * want);
            if (gPrint && scatter > 0) std::printf("  density %d (%.1f Hz), SCATTER %d: %.0f onsets in %d s (%.0f)\n", dial, densityHz(dial), scatter, onsets, seconds, want);
        }
}

// PITCH TO THE CENT: a sine recording, one long grain at a time, keys 36 to 84; PITCH STEPS on its
// grid (every grain's speed an octave from the key's).
static void testPitch() {
    const std::vector<float> sine = sineRecording(440.0, 2.0);
    double worst = 0.0;
    for (int key : {36, 43, 48, 55, 60, 67, 72, 79, 84}) {
        std::unique_ptr<Cloud> c = synth({{Size, 100}, {Density, 0}, {Scatter, 0}, {Attack, 0}, {Shape, 50}}, &sine);
        const std::vector<float> l = leftOf(play(*c, {on(0, key)}, 48000));
        const double hz = 440.0 * std::pow(2.0, (key - 60) / 12.0);
        const double cents = centsOff(l, hz, 0.25, 30.0, 16.0);
        worst = std::max(worst, std::fabs(cents));
        CHECK(std::fabs(cents) < 1.0);
    }
    // TUNE moves it by its cents.
    {
        std::unique_ptr<Cloud> c = synth({{Size, 100}, {Density, 0}, {Scatter, 0}, {Tune, 50}}, &sine);
        const std::vector<float> l = leftOf(play(*c, {on(0, 60)}, 48000));
        CHECK(std::fabs(centsOff(l, 440.0 * std::pow(2.0, 50.0 / 1200.0), 0.25, 30.0, 16.0)) < 1.0);
    }
    // PITCH SPRAY on OCTAVES and FIFTHS: every grain's speed on the grid.
    for (int steps : {kStepsOctaves, kStepsFifths}) {
        std::unique_ptr<Cloud> c = synth({{PitchSpray, 100}, {PitchSteps, steps}, {Density, 90}, {Size, 20}}, &sine);
        play(*c, {on(0, 60)}, 48000);
        Cloud::Started s[64];
        const int n = c->lastStarted(s, 64);
        int off = 0, moved = 0;
        for (int i = 0; i < n; ++i) {
            const double semis = 12.0 * std::log2(std::fabs(s[i].step));
            const double grid = steps == kStepsOctaves ? std::fmod(std::fabs(semis), 12.0) : std::fabs(semis);
            const bool on = steps == kStepsOctaves ? (grid < 1e-9 || grid > 12.0 - 1e-9)
                                                   : (std::fabs(grid) < 1e-9 || std::fabs(grid - 7) < 1e-9 || std::fabs(grid - 12) < 1e-9);
            off += on ? 0 : 1;
            moved += std::fabs(semis) > 0.5 ? 1 : 0;
        }
        CHECK(n == 64 && off == 0 && moved > 10);
    }
    if (gPrint) std::printf("  pitch: keys 36 to 84, %.2g cents at worst\n", worst);
}

// NO CLICK AT GRAIN EDGES: a recording that is all one level (so the sound is the windows
// themselves), the shortest grains, every SHAPE, many overlapping: no sample steps further than
// the windows' own steepest slope allows.
static void testNoClick() {
    const std::vector<float> flat(48000, 0.5f);
    for (int shape : {0, 50, 100})
        for (int density : {30, 100}) {
            std::unique_ptr<Cloud> c = synth({{Size, 0}, {Shape, shape}, {Density, density}, {Scatter, 100}, {Stereo, 0}, {Attack, 0}, {Release, 0}}, &flat);
            const std::vector<float> l = leftOf(play(*c, {on(0, 60), off(30000, 60)}, 36000));
            // Each grain moves at most pi/2 / 24 of its gain a sample (its piece is 24 samples at
            // the least); twelve may move at once, each at 1/sqrt(the overlap).
            const double gain = std::sqrt(0.5) * 0.8 * overlapGain(densityHz(density), sizeSeconds(0)) * velocityGain(50, 0.9);
            const double most = 0.5 * gain * (3.14159265 / 2.0 / 24.0) * std::min(12.0, std::ceil(densityHz(density) * 0.005) + 1.0);
            CHECK(largestJump(l) <= most * 1.01);
            CHECK(l[0] == 0.f && l.back() == 0.f);
        }
    // A full pool skips grains, and never cuts one: the same, with every key held.
    std::unique_ptr<Cloud> c = synth({{Size, 40}, {Density, 100}, {Scatter, 100}, {Stereo, 0}, {Attack, 0}}, &flat);
    std::vector<Ev> chord;
    for (int k = 0; k < 6; ++k) chord.push_back(on(0, 48 + k));
    const std::vector<float> l = leftOf(play(*c, chord, 24000));
    CHECK(c->grainsSkipped() > 100 && c->grainsSounding() >= kPool - 2);
    const double gain = std::sqrt(0.5) * 0.8 * overlapGain(densityHz(100), sizeSeconds(40)) * velocityGain(50, 0.9);
    const double steepest = 0.5 * gain * 3.14159265 / 2.0 / static_cast<double>(riseSamples(grainSamples(sizeSeconds(40), kRate), 0.5, kRate));
    CHECK(largestJump(l, 4800) <= steepest * kPool * 1.01);
    if (gPrint) std::printf("  a full pool: %ld grains skipped of %ld; the largest step %.4f (the windows allow %.4f)\n", c->grainsSkipped(),
                            c->grainsSkipped() + c->grainsStarted(), largestJump(l, 4800), steepest * kPool);
}

// LEVEL within +-3 dB over a DENSITY x SIZE grid (a noise recording; the cloud's RMS against the
// middle of the grid), where the cloud is not sparse.
static void testCloudLevel() {
    const std::vector<float> noise = noiseRecording(4.0);
    double middle = 0.0, lo = 1e9, hi = -1e9;
    std::vector<double> got;
    for (int density : {40, 55, 70, 85, 100})
        for (int size : {50, 65, 80, 95}) {
            if (densityHz(density) * sizeSeconds(size) < 1.0) continue;
            std::unique_ptr<Cloud> c = synth({{Density, density}, {Size, size}, {Spray, 100}, {Attack, 0}}, &noise);
            const std::vector<float> l = leftOf(play(*c, {on(0, 60)}, 4 * 48000));
            const double level = dB(rmsOf(l, 48000));
            got.push_back(level);
            if (density == 70 && size == 65) middle = level;
        }
    for (double g : got) lo = std::min(lo, g - middle), hi = std::max(hi, g - middle);
    CHECK(got.size() >= 14 && lo >= -3.0 && hi <= 3.0);
    if (gPrint) std::printf("  level over %zu settings: %.2f to %+.2f dB about the middle (%.1f dB under the recording)\n", got.size(), lo, hi,
                            dB(rmsOf(noise)) - middle);
}



// A busy cloud: every roll at work (SPRAY, SCATTER, PITCH SPRAY, REVERSE, STEREO, SCAN), a chord,
// a glide, a key struck again, a let-go, a key stolen.
static const std::vector<Set> kBusy = {{Spray, 60}, {Scatter, 100}, {PitchSpray, 40}, {Reverse, 50}, {Stereo, 100}, {Scan, 70}, {Density, 75}, {Size, 55}, {Shape, 20}, {Release, 40}};
static std::vector<Ev> busyLine() {
    return {on(0, 48, 0.9f, 11), on(0, 55, 0.7f, 12), on(300, 60, 1.f, 13), glide(4000, 67, 60, 0.2f), on(9000, 48, 0.5f, 14), off(12000, 55),
            on(15000, 50), on(15001, 52), on(15002, 53), on(15003, 57), off(30000, 48), off(30000, 60), off(40000, 67)};
}

// BLOCKS 1 TO 256 TO THE BIT, through the synth: the same sound however it is cut.
static void testBlocks() {
    const std::vector<float> noise = noiseRecording(1.5);
    for (int linearDial : {0, 55}) {
        std::vector<Set> sets = kBusy;
        sets.push_back({Size, linearDial}); // (0: 5 ms grains, read straight between samples)
        std::uint64_t first = 0;
        for (int block : {256, 1, 7, 64, 333}) {
            std::unique_ptr<Cloud> c = synth(sets, &noise);
            Sum s;
            s.add(play(*c, busyLine(), 60000, block));
            if (block == 256) first = s.h;
            CHECK(s.h == first);
        }
    }
    // The pool full while keys end and others start (a grain freed on the very sample its key ends).
    {
        std::uint64_t first = 0;
        std::vector<Ev> line;
        for (int k = 0; k < 6; ++k) line.push_back(on(37 * k, 48 + k, 0.9f, 100 + k));
        for (int k = 0; k < 6; ++k) line.push_back(off(3000 + 1111 * k, 48 + k));
        for (int k = 0; k < 6; ++k) line.push_back(on(3500 + 1111 * k, 60 + k, 0.9f, 200 + k));
        std::sort(line.begin(), line.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
        for (int block : {256, 1, 7, 64, 333}) {
            std::unique_ptr<Cloud> c = synth({{Density, 100}, {Size, 80}, {Scatter, 100}, {Release, 0}, {PitchSpray, 20}}, &noise);
            Sum s;
            s.add(play(*c, line, 20000, block));
            if (block == 256) first = s.h;
            CHECK(s.h == first);
        }
    }
    // On the TONE and at the recording's own pitch (whole samples) too.
    std::uint64_t first = 0;
    for (int block : {256, 1, 5}) {
        std::unique_ptr<Cloud> c = synth({{Scatter, 100}, {Spray, 100}, {Reverse, 50}});
        Sum s;
        s.add(play(*c, {on(0, 60), on(100, 72), off(20000, 60)}, 40000, block));
        if (block == 256) first = s.h;
        CHECK(s.h == first);
    }
}

// Through the engine: three CLOUD tracks (a recording, the TONE, a recording at its own pitch),
// notes with glides, at 120 BPM: the same to the bit in blocks of 1 to 256 and on 0 to 3 helpers;
// Stop, a rest, Play again: the same again.
static Command noteAt(int track, int slot, int stepIndex, int pitch, float length, int effects = 0, float velocity = 0.9f) {
    Command c;
    c.type = Command::SetNote;
    c.track = track;
    c.a = slot;
    c.b = stepIndex;
    c.value = velocity;
    c.c = pitch;
    c.d = static_cast<int>(length);
    c.extra = length - static_cast<float>(c.d);
    c.e = effects;
    return c;
}
static std::vector<float> engineSong(int helpers, int block, bool twice = false) {
    std::unique_ptr<Engine> engine(new Engine);
    if (helpers) engine->useWorkers(helpers);
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, SynthKind::Cloud));
        if (t != 2) {
            std::unique_ptr<SampleData> rec(new SampleData);
            rec->data = t == 1 ? noiseRecording(1.0) : sineRecording(330.0, 1.0);
            CHECK(engine->setTrackSample(t, std::move(rec)));
        }
        Command set;
        set.type = Command::SetSetting, set.track = t;
        for (const Set& x : kBusy)
            if (t != 3 || (x.id != PitchSpray)) set.a = x.id, set.b = x.value, engine->send(set);
        for (int step = 0; step < 16; step += 2) engine->send(noteAt(t, step, step, 48 + (step * 5 + t * 7) % 24, 1.5f, step % 4 == 2 ? 256 : 0));
    }
    Command p;
    p.type = Command::Play;
    engine->send(p);
    const long frames = 16 * 6000L;
    std::vector<float> out(static_cast<std::size_t>(2 * frames));
    for (long at = 0; at < frames;) {
        const int n = static_cast<int>(std::min<long>(block, frames - at));
        engine->render(out.data() + 2 * at, n);
        at += n;
    }
    if (twice) {
        Command stop;
        stop.type = Command::Stop;
        engine->send(stop);
        std::vector<float> rest(2 * 48000);
        for (int i = 0; i < 4; ++i) engine->render(rest.data(), 12000);
        engine->send(p);
        for (long at = 0; at < frames;) {
            const int n = static_cast<int>(std::min<long>(block, frames - at));
            engine->render(out.data() + 2 * at, n);
            at += n;
        }
    }
    return out;
}
static void testThroughTheEngine() {
    const std::vector<float> first = engineSong(0, 256);
    double power = 0.0;
    for (float v : first) power += static_cast<double>(v) * v;
    CHECK(allFinite(first) && power / first.size() > 1e-5);
    for (int block : {1, 7, 64}) CHECK(engineSong(0, block) == first);
    for (int helpers = 1; helpers <= 3; ++helpers) CHECK(engineSong(helpers, 256) == first);
    CHECK(engineSong(3, 7) == first);
    CHECK(engineSong(0, 256, true) == first); // Play from the top: the same cloud
    // The kind makes a CLOUD, and the engine keeps its grains for the picture.
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(4, SynthKind::Cloud) && engine->addSynthTrack(5, SynthKind::Sampler));
    Command c;
    c.type = Command::NoteOn, c.track = 4, c.a = 60, c.value = 0.9f;
    engine->send(c);
    std::vector<float> out(2 * 4800);
    engine->render(out.data(), 4800);
    CHECK(dynamic_cast<const Cloud*>(engine->synthForTests(4)) != nullptr);
    float places[kGrainsShown];
    const int n = engine->grainPlaces(4, places, kGrainsShown);
    CHECK(n >= 1 && n <= kGrainsShown && engine->grainPlaces(5, places, kGrainsShown) == -1 && engine->grainPlaces(0, places, kGrainsShown) == -1);
    for (int i = 0; i < n; ++i) CHECK(places[i] >= 0.f && places[i] < 1.f);
    CHECK(engine->grainPlaces(4, places, 0) == 0);
}

// The places for the picture: each sounding grain, near POSITION with no SPRAY (the TONE).
static void testGrainPlaces() {
    std::unique_ptr<Cloud> c = synth({{Spray, 0}, {Position, 40}, {Density, 80}, {Size, 70}});
    float places[kPool];
    CHECK(c->grainPlaces(places, kPool) == 0);
    play(*c, {on(0, 60)}, 12000);
    const int n = c->grainPlaces(places, kPool);
    CHECK(n == c->grainsSounding() && n > 4);
    // Each read on from 40% at its own speed (at most SIZE's 0.24 s into the 2 s TONE).
    for (int i = 0; i < n; ++i) CHECK(places[i] >= 0.4f && places[i] <= 0.4f + 0.24f / 2.f + 0.001f);
    CHECK(c->grainPlaces(places, 2) == 2);
}

// NO ALLOCATION on the audio thread: notes, settings, macros, presets, a recording handed over.
static void testNoAllocation() {
    const std::vector<float> noise = noiseRecording(1.0), sine = sineRecording(220.0, 1.0);
    std::unique_ptr<Cloud> c = synth(kBusy, &noise);
    std::vector<float> l(256), r(256);
    const long before = gNews;
    for (int k = 0; k < 8; ++k) c->noteOn(48 + k, 0.9f);
    for (int b = 0; b < 40; ++b) c->render(l.data(), r.data(), 256);
    c->setMacro(MacroDensity, 0.9f);
    c->setSetting(Size, 90);
    c->noteOnFrom(70, 1.f, 50, 0.05f);
    c->setSample(sine.data(), static_cast<int>(sine.size()));
    c->render(l.data(), r.data(), 256);
    c->noteOff(70);
    c->stopNotes();
    c->allNotesOff();
    c->selectPreset(1);
    c->setSample(nullptr, 0);
    c->noteOn(60, 0.5f);
    float places[kPool];
    c->grainPlaces(places, kPool);
    for (int b = 0; b < 40; ++b) c->render(l.data(), r.data(), 256);
    CHECK(gNews == before);
}

// NO NUMBER TOO SMALL TO HOLD: a minute held, then let go; every level held is 1e-4 or over (the
// envelope's end) or 0, and when the last key has gone, nothing sounds and nothing is held.
static void testTooSmall() {
    const std::vector<float> noise = noiseRecording(1.0);
    std::unique_ptr<Cloud> c = synth({{Release, 100}, {Attack, 100}}, &noise);
    float least = 1.f;
    play(*c, {on(0, 48), on(0, 60)}, 4800);
    for (int i = 0; i < 60; ++i) {
        const std::vector<float> out = play(*c, {}, 48000, 4096);
        CHECK(allFinite(out));
        const float s = c->smallestState();
        if (s > 0.f) least = std::min(least, s);
    }
    c->noteOff(48);
    c->noteOff(60);
    for (int i = 0; i < 30; ++i) { // RELEASE 100: 20 s to 60 dB down, 26.7 s to the envelope's end (80 dB)
        play(*c, {}, 48000, 4096);
        const float s = c->smallestState();
        if (s > 0.f) least = std::min(least, s);
    }
    CHECK(least >= envelope::kSilent * 0.99f);
    CHECK(c->keysSounding() == 0 && c->grainsSounding() == 0 && c->smallestState() == 0.f);
    CHECK(peakOf(play(*c, {}, 4800)) == 0.f);
    if (gPrint) std::printf("  the smallest level held: %.3g\n", least);
}

// NOT A NUMBER: a note with none is not played, a macro with none is not taken, a glide with no
// time is a plain note, and nothing sticks.
static void testNaN() {
    const float nan = std::nanf("");
    std::unique_ptr<Cloud> c = synth();
    c->noteOn(48, nan);
    c->noteOn(200, 0.9f);
    c->noteOn(-1, 0.9f);
    CHECK(c->keysSounding() == 0);
    c->setMacro(MacroPosition, nan);
    c->setMacro(MacroDensity, std::numeric_limits<float>::infinity()); // held to 1
    c->setSetting(99, 5);
    c->setSetting(-1, 5);
    c->noteOnFrom(48, 0.9f, 36, nan);
    c->noteOn(52, std::numeric_limits<float>::infinity()); // held to 1
    const std::vector<float> out = play(*c, {}, 9600);
    CHECK(allFinite(out) && peakOf(out) > 0.f && c->keysSounding() == 2);
    // A recording with a NaN or an infinity in it is the app's to mend (readWav); the synth is
    // handed what the app made. A length of 0 or under is no recording: the TONE.
    std::vector<float> one(1, 0.25f);
    c->setSample(one.data(), 1);
    CHECK(allFinite(play(*c, {on(0, 60), on(0, 73)}, 4800)) && !c->onTone()); // a recording one sample long, at any pitch
    c->setSample(one.data(), 0);
    CHECK(c->onTone());
    c->setSample(one.data(), -5);
    CHECK(c->onTone());
}

// SILENCE IN, SILENCE OUT: nothing played is exact zeros; at LEVEL 0 a key is exact zeros; a key
// that has ended leaves none of it.
static void testSilence() {
    std::unique_ptr<Cloud> c = synth();
    CHECK(peakOf(play(*c, {}, 4800)) == 0.f);
    play(*c, {on(0, 60), off(4800, 60)}, 48000);
    CHECK(c->keysSounding() == 0 && c->grainsSounding() == 0);
    CHECK(peakOf(play(*c, {}, 4800)) == 0.f);
    std::unique_ptr<Cloud> quiet = synth({{Level, 0}});
    CHECK(peakOf(play(*quiet, {on(0, 60)}, 9600)) == 0.f && quiet->grainsStarted() > 0);
    std::vector<float> zeros(48000, 0.f);
    std::unique_ptr<Cloud> z = synth({}, &zeros);
    CHECK(peakOf(play(*z, {on(0, 60), on(0, 67)}, 9600)) == 0.f);
}

// PLAY TWICE THE SAME: after allNotesOff (5 ms) the line again is a fresh synth's, to the bit; and
// a key played by hand (no seed) after allNotesOff rolls as the first did.
static void testPlayFromTheTop() {
    const std::vector<float> noise = noiseRecording(1.0);
    std::unique_ptr<Cloud> c = synth(kBusy, &noise);
    play(*c, busyLine(), 36000);
    c->allNotesOff();
    play(*c, {}, 480);
    CHECK(c->keysSounding() == 0);
    const std::vector<float> again = play(*c, busyLine(), 60000);
    std::unique_ptr<Cloud> fresh = synth(kBusy, &noise);
    CHECK(again == play(*fresh, busyLine(), 60000));
    // By hand: no seed handed over.
    std::unique_ptr<Cloud> a = synth(kBusy, &noise), b = synth(kBusy, &noise);
    const std::vector<Ev> hand = {on(0, 60), on(500, 64), off(20000, 60)};
    play(*a, hand, 30000);
    a->allNotesOff();
    play(*a, {}, 480);
    CHECK(play(*a, hand, 30000) == play(*b, hand, 30000));
}

// A RECORDING SWAPPED MID-KEY: every grain ends at once (nothing reads the old one, which the
// engine frees next frame), the keys go on with the new one. Here the old one is written over
// with not-a-number the moment it is handed back: a read of it would show.
static void testSwap() {
    std::vector<float>* old = new std::vector<float>(noiseRecording(1.0));
    const std::vector<float> sine = sineRecording(220.0, 0.5);
    std::unique_ptr<Cloud> c = synth({{Density, 100}, {Size, 90}, {PitchSpray, 30}, {Spray, 100}}, old);
    play(*c, {on(0, 48), on(0, 72)}, 9600, 64);
    CHECK(c->grainsSounding() > 10);
    c->setSample(sine.data(), static_cast<int>(sine.size()));
    CHECK(c->grainsSounding() == 0);
    for (float& x : *old) x = std::nanf("");
    delete old;
    const std::vector<float> out = play(*c, {}, 24000, 64);
    CHECK(allFinite(out) && c->grainsSounding() > 10 && c->keysSounding() == 2);
    // Every grain now reads within the shorter recording.
    float places[kPool];
    const int n = c->grainPlaces(places, kPool);
    for (int i = 0; i < n; ++i) CHECK(places[i] >= 0.f && places[i] < 1.f);
    // And back to none: the TONE.
    c->setSample(nullptr, 0);
    CHECK(c->grainsSounding() == 0 && c->onTone());
    CHECK(allFinite(play(*c, {}, 4800)));
    // Through the engine, the hand-over: a recording sent mid-key, the old one through the trash.
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1, SynthKind::Cloud));
    std::unique_ptr<SampleData> a(new SampleData), b(new SampleData);
    a->data = noiseRecording(0.5);
    b->data = sineRecording(440.0, 0.25);
    CHECK(engine->setTrackSample(1, std::move(a)));
    Command note;
    note.type = Command::NoteOn, note.track = 1, note.a = 60, note.value = 0.9f;
    engine->send(note);
    std::vector<float> buf(2 * 4800);
    engine->render(buf.data(), 4800);
    CHECK(engine->setTrackSample(1, std::move(b)));
    engine->render(buf.data(), 256);
    engine->collectGarbage(); // the old one freed here, on this thread
    for (int i = 0; i < 10; ++i) engine->render(buf.data(), 4800);
    CHECK(allFinite(buf) && peakOf(buf) > 0.f);
}

// THE STEAL: six keys sound; a seventh takes the oldest let go (else the oldest), which fades out
// over 5 ms with its grains: no click.
static void testSteal() {
    const std::vector<float> flat(48000, 0.5f);
    std::unique_ptr<Cloud> c = synth({{Stereo, 0}, {Attack, 0}, {Density, 60}, {Size, 60}}, &flat);
    std::vector<Ev> line;
    for (int k = 0; k < 6; ++k) line.push_back(on(10 * k, 48 + k));
    line.push_back(off(5000, 50));
    line.push_back(on(9000, 70));
    line.push_back(on(9500, 71));
    const std::vector<float> l = leftOf(play(*c, line, 24000));
    CHECK(c->keysSounding() == 6);
    // The window's steepest step at this SIZE, times every grain that might be at it, and the
    // 5 ms fade's step on all a key holds: well under a click.
    CHECK(largestJump(l, 4800) < 0.01f);
    if (gPrint) std::printf("  the steal: the largest step %.4f\n", largestJump(l, 4800));
    // A key sounds twelve grains at most, however dense; the pool's last ones are left for others.
    std::unique_ptr<Cloud> dense = synth({{Density, 100}, {Size, 100}}, &flat);
    int most = 0;
    for (int b = 0; b < 40; ++b) {
        play(*dense, b == 0 ? std::vector<Ev>{on(0, 60)} : std::vector<Ev>{}, 1200);
        most = std::max(most, dense->grainsSounding());
    }
    CHECK(most == kGrainsPerKey && dense->grainsSkipped() > 100);
    // The same key struck again stays in its slot: no steal, its grains go on.
    std::unique_ptr<Cloud> d = synth({}, &flat);
    play(*d, {on(0, 60), on(4000, 60), on(8000, 60)}, 12000);
    CHECK(d->keysSounding() == 1);
}

// THE TONE: 2 s, made once per sample rate and shared, at middle C, going round without a seam.
static void testTone() {
    const std::vector<float>& t = Cloud::tone(48000.f);
    CHECK(t.size() == 96000 && &t == &Cloud::tone(48000.f) && &t != &Cloud::tone(44100.f) && Cloud::tone(44100.f).size() == 88200);
    CHECK(near(peakOf(t), 0.5, 1e-6) && allFinite(t));
    CHECK(std::fabs(t.front() - t.back()) < 0.03f && largestJump(t) < 0.06f);
    CHECK(std::fabs(centsOff(t, 261.5, 0.2, 60.0, 32.0)) < 3.0); // (the beating partial beside it moves the reading a little)
    std::unique_ptr<Cloud> c = synth();
    CHECK(c->onTone() && peakOf(play(*c, {on(0, 60)}, 9600)) > 0.05f);
}

// PRESETS: a preset puts its patch in and the macros at the middle, as a fresh synth on it.
static void testPresets() {
    CHECK(Cloud::presetCount() >= 2 && std::strcmp(Cloud::presetName(0), "Soft Cloud") == 0 && std::strcmp(Cloud::presetCategory(0), "STILL") == 0);
    for (int id = 0; id < kSettings; ++id) CHECK(Cloud::presetPatch(0).value[id] == settingUsual(id));
    for (int p = 0; p < Cloud::presetCount(); ++p)
        for (int id = 0; id < kSettings; ++id) CHECK(Cloud::presetPatch(p).value[id] == settingHeld(id, Cloud::presetPatch(p).value[id]));
    const std::vector<float> noise = noiseRecording(1.0);
    std::unique_ptr<Cloud> a = synth({}, &noise);
    for (int m = 0; m < kMacros; ++m) a->setMacro(m, 0.1f * m);
    a->setSetting(Level, 20);
    a->selectPreset(1);
    std::vector<Set> one;
    for (int id = 0; id < kSettings; ++id) one.push_back({id, Cloud::presetPatch(1).value[id]});
    std::unique_ptr<Cloud> b = synth(one, &noise);
    CHECK(play(*a, {on(0, 60, 0.9f, 5)}, 24000) == play(*b, {on(0, 60, 0.9f, 5)}, 24000));
    // A macro bends its setting: DENSITY's at 1 is DENSITY 100.
    std::unique_ptr<Cloud> m = synth({}, &noise), d = synth({{Density, 100}}, &noise);
    m->setMacro(MacroDensity, 1.f);
    play(*m, {on(0, 60)}, 48000);
    play(*d, {on(0, 60)}, 48000);
    CHECK(m->grainsStarted() + m->grainsSkipped() == d->grainsStarted() + d->grainsSkipped());
}

// GLIDE: each grain takes the key's pitch where the slide is when the grain starts; at the end
// of the slide, the key's own.
static void testGlide() {
    std::unique_ptr<Cloud> c = synth({{Density, 100}, {Size, 0}, {Scatter, 0}});
    play(*c, {glide(0, 72, 60, 0.1f)}, 9600);
    Cloud::Started s[64];
    const int n = c->lastStarted(s, 64);
    int checked = 0;
    for (int i = 0; i < n; ++i) {
        const double at = static_cast<double>(s[i].at);
        const double want = at < 4800.0 ? std::pow(2.0, (12.0 - 12.0 * (1.0 - at / 4800.0)) / 12.0) : 2.0;
        CHECK(near(s[i].step, want, 1e-9));
        ++checked;
    }
    CHECK(checked == n && n == 40); // 200 a second for 0.2 s
}


// SCAN and SPRAY: with no SPRAY every grain starts at POSITION plus what SCAN has moved since the
// strike (x1: the recording's own speed), wrapped; with SPRAY each within +- its share.
static void testScanAndSpray() {
    const std::vector<float> noise = noiseRecording(1.0);
    for (int scan : {50, 75, 25, 100, 62}) { // x0, x1, x-1, x2, x0.48
        std::unique_ptr<Cloud> c = synth({{Spray, 0}, {Scan, scan}, {Position, 30}, {Density, 70}}, &noise);
        play(*c, {on(0, 60)}, 48000 * 3 / 2);
        Cloud::Started s[64];
        const int n = c->lastStarted(s, 64);
        int right = 0;
        for (int i = 0; i < n; ++i) right += near(s[i].place, wrapPlace(0.3 * 48000.0 + scanTimes(scan) * static_cast<double>(s[i].at), 48000.0), 0.5 + 1e-6) ? 1 : 0; // (whole samples at ROOT: the place rounded)
        if (scan == 62) CHECK(c->lastStarted(s, 1) == 1 && wrapPlace(0.3 * 48000.0 + scanTimes(scan) * static_cast<double>(s[0].at), 48000.0) != s[0].place);
        CHECK(n == 63 && right == n); // 40.8 a second for 1.5 s
        for (int i = 0; i < n; ++i) CHECK(s[i].place == std::floor(s[i].place)); // at ROOT: whole samples, the place rounded
    }
    // A MOD on SCAN moves the place on from where it is (added up grain by grain, each gap at the
    // SCAN read at its end): from the last grain before the turn (11760, 49 gaps of 240 at x1), at
    // x-0.5.
    {
        std::unique_ptr<Cloud> c = synth({{Spray, 0}, {Scan, 75}, {Position, 0}, {Density, 100}, {Scatter, 0}, {Size, 20}}, &noise);
        play(*c, {on(0, 60)}, 12000);
        c->setMacro(MacroScan, 0.f + 0.25f); // SCAN 75 bent down to 37.5: x-0.5
        play(*c, {}, 12000);
        Cloud::Started s[64];
        const int n = c->lastStarted(s, 64);
        int right = 0;
        for (int i = 0; i < n; ++i) {
            const double at = static_cast<double>(s[i].at);
            right += near(s[i].place, at <= 11760.0 ? at : wrapPlace(11760.0 - 0.5 * (at - 11760.0), 48000.0), 0.5 + 1e-6) ? 1 : 0;
        }
        CHECK(n == 64 && right == n && s[n - 1].at > 12000);
    }
    // The same key struck again starts SCAN's way again from POSITION.
    {
        std::unique_ptr<Cloud> c = synth({{Spray, 0}, {Scan, 75}, {Position, 10}, {Density, 52}, {Scatter, 0}}, &noise);
        play(*c, {on(0, 60), on(30000, 60)}, 31000);
        Cloud::Started s[2];
        CHECK(c->lastStarted(s, 2) == 2 && s[0].place > 4800.0 && s[1].at == 30000 && s[1].place == 4800.0);
    }
    for (int spray : {10, 50, 100}) {
        std::unique_ptr<Cloud> c = synth({{Spray, spray}, {Position, 50}, {Density, 100}}, &noise);
        play(*c, {on(0, 60)}, 24000);
        Cloud::Started s[64];
        const int n = c->lastStarted(s, 64);
        double most = 0.0;
        for (int i = 0; i < n; ++i) {
            double off = s[i].place - 24000.0;
            if (off > 24000.0) off -= 48000.0;
            if (off < -24000.0) off += 48000.0;
            most = std::max(most, std::fabs(off));
        }
        CHECK(most <= sprayShare(spray) * 48000.0 + 1e-6 && most > 0.6 * sprayShare(spray) * 48000.0);
    }
}

// REVERSE: the chance a grain plays backwards, read from its far end; STEREO: at 0 both sides are
// the same, at 100 they differ and each grain's two add in power to one; VELOCITY: at 100 a key's
// level is its velocity.
static void testReverseStereoVelocity() {
    const std::vector<float> noise = noiseRecording(1.0);
    for (int reverse : {0, 50, 100}) {
        std::unique_ptr<Cloud> c = synth({{Reverse, reverse}, {Density, 100}}, &noise);
        play(*c, {on(0, 60)}, 24000);
        Cloud::Started s[64];
        const int n = c->lastStarted(s, 64);
        int back = 0;
        for (int i = 0; i < n; ++i) back += s[i].step < 0.0 ? 1 : 0;
        if (reverse == 0) CHECK(back == 0);
        if (reverse == 100) CHECK(back == n);
        if (reverse == 50) CHECK(back > 20 && back < 44);
    }
    // A grain read backwards starts at the far end of the stretch it would have read forwards.
    {
        std::unique_ptr<Cloud> c = synth({{Reverse, 100}, {Spray, 0}, {Position, 20}, {Density, 0}}, &noise);
        play(*c, {on(0, 72)}, 100);
        Cloud::Started s[1];
        CHECK(c->lastStarted(s, 1) == 1 && s[0].step == -2.0 && near(s[0].place, 9600.0 + (s[0].length - 1) * 2.0, 1e-9));
    }
    // A rising ramp read backwards falls: one grain, at its peak.
    std::vector<float> ramp(48000);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i) / 48000.f;
    for (int reverse : {0, 100}) {
        std::unique_ptr<Cloud> c = synth({{Reverse, reverse}, {Spray, 0}, {Position, 40}, {Density, 0}, {Size, 60}, {Attack, 0}}, &ramp);
        const std::vector<float> l = leftOf(play(*c, {on(0, 60)}, 5760));
        const float slope = l[2900] - l[2860]; // about the window's peak (2880)
        CHECK(reverse == 0 ? slope > 0.f : slope < 0.f);
    }
    {
        std::unique_ptr<Cloud> c = synth({{Stereo, 0}}, &noise);
        const std::vector<float> both = play(*c, {on(0, 60)}, 24000);
        CHECK(std::equal(both.begin(), both.begin() + 24000, both.begin() + 24000));
        std::unique_ptr<Cloud> d = synth({{Stereo, 100}}, &noise);
        const std::vector<float> wide = play(*d, {on(0, 60)}, 24000);
        CHECK(!std::equal(wide.begin(), wide.begin() + 24000, wide.begin() + 24000));
        // In power, left and right together are the middle's two.
        const double mid = rmsOf(both), w = std::sqrt(0.5 * (rmsOf(wide, 0, 24000) * rmsOf(wide, 0, 24000) + rmsOf(wide, 24000) * rmsOf(wide, 24000)));
        CHECK(std::fabs(dB(w / mid)) < 1.5);
    }
    for (int velocityDial : {0, 100}) {
        std::unique_ptr<Cloud> loud = synth({{Velocity, velocityDial}}, &noise), soft = synth({{Velocity, velocityDial}}, &noise);
        const double a = rmsOf(play(*loud, {on(0, 60, 1.f, 3)}, 24000)), b = rmsOf(play(*soft, {on(0, 60, 0.5f, 3)}, 24000));
        CHECK(velocityDial == 0 ? a == b : near(b / a, 0.5, 1e-4));
    }
}

// THE READ BETWEEN SAMPLES: a sine read at a pitch between whole samples comes out a sine, what
// is not the sine 70 dB down (Hermite, a long grain). Sample by sample, against the true sine at
// each place read (the same grain on a recording of ones gives its window and gain, to divide by):
// Hermite within 2e-5, the straight read of a grain under 20 ms within its own bound and well over
// Hermite's.
static void testTheRead() {
    const std::vector<float> sine = sineRecording(440.0, 2.0);
    {
        std::unique_ptr<Cloud> c = synth({{Size, 100}, {Density, 0}, {Scatter, 0}, {Tune, 37}, {Attack, 0}, {Spray, 0}}, &sine);
        const std::vector<float> l = leftOf(play(*c, {on(0, 55)}, 48000));
        const double hz = 440.0 * std::pow(2.0, (-5.0 + 0.37) / 12.0);
        const Aside a = asideOf(l.data() + 12000, 16384, hz, 20000.0, hz);
        if (gPrint) std::printf("  the read, a long grain (Hermite): what is not the sine %.1f dB down\n", -a.dB);
        CHECK(a.dB < -70.0);
    }
    // -60 and -61 read forwards and backwards across the end at key 79, -62 and -63 at ROOT (whole
    // samples): the sine recording goes round without a seam, so the true sine is still the sine.
    // Each recording is handed over one short of its vector, the last a not-a-number: a read past
    // the end shows.
    std::vector<float> guarded = sine, ones(96000, 1.f);
    guarded.push_back(std::nanf(""));
    ones.push_back(std::nanf(""));
    for (int size : {20, 30, 60, -60, -61, -62, -63}) { // 14 ms (straight), 25 ms and 120 ms (Hermite)
        const bool across = size < 0, root = size <= -62;
        const int reverse = size == -61 || size == -63 ? 100 : 0;
        if (across) size = 60;
        const std::vector<Set> sets = {{Size, size}, {Density, 0}, {Scatter, 0}, {Tune, root ? 0 : 37}, {Spray, 0}, {Stereo, 0}, {Position, across ? 99 : 0}, {Reverse, reverse}};
        std::unique_ptr<Cloud> c = synth(sets), u = synth(sets);
        c->setSample(guarded.data(), 96000);
        u->setSample(ones.data(), 96000);
        const int key = root ? 60 : 79;
        const std::vector<float> l = leftOf(play(*c, {on(0, key)}, 6000)), w = leftOf(play(*u, {on(0, key)}, 6000));
        Cloud::Started s[1];
        CHECK(c->lastStarted(s, 1) == 1 && s[0].at == 0);
        double worst = 0.0;
        for (long k = 0; k < s[0].length; ++k) {
            if (w[static_cast<std::size_t>(k)] < 1e-2f) continue;
            const double got = l[static_cast<std::size_t>(k)] / w[static_cast<std::size_t>(k)];
            const double want = 0.5 * std::sin(2.0 * kPi * 440.0 * (s[0].place + static_cast<double>(k) * s[0].step) / kRate); // (any place: the sine is whole cycles round)
            const double off = std::fabs(got - want);
            if (!(off <= worst)) worst = off; // (a sample not a number counts: a read past an end)
        }
        const bool straight = sizeSeconds(size) < 0.020;
        // A straight read of a sine of size A, d radians a sample, is off by at most A d^2 / 8.
        const double d = 2.0 * kPi * 440.0 / kRate, bound = 0.5 * d * d / 8.0; // (d a sample of the recording)
        if (straight) CHECK(worst <= bound * 1.05 + 1e-6 && worst > 0.3 * bound);
        else CHECK(worst < 2e-5);
        if (gPrint) std::printf("  the read, SIZE %d (%s): %.2g at worst from the true sine (a straight read's bound %.2g)\n", size, straight ? "straight" : "Hermite", worst, bound);
    }
}

// What it costs, in blocks of 256: one key of preset 0, and the pool full (six keys, 200 grains a
// second each, every grain read between samples).
static double costOf(const std::vector<Set>& sets, int keys, const std::vector<float>& recording, int* sounding = nullptr) {
    std::unique_ptr<Cloud> c = synth(sets, &recording);
    for (int k = 0; k < keys; ++k) c->noteOn(48 + 3 * k, 0.9f);
    std::vector<float> l(256), r(256);
    for (int b = 0; b < 200; ++b) c->render(l.data(), r.data(), 256);
    double best = 1e9;
    long grains = 0, blocks = 0;
    for (int run = 0; run < 5; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 563; ++b) { // 3 s
            c->render(l.data(), r.data(), 256);
            grains += c->grainsSounding();
            ++blocks;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, secs / (563.0 * 256.0 / kRate));
    }
    if (sounding) *sounding = static_cast<int>(grains / blocks);
    return 100.0 * best;
}
static void measureCost() {
    const std::vector<float> noise = noiseRecording(4.0);
    int g1 = 0, g4 = 0, gFull = 0, gPlain = 0;
    const double one = costOf({}, 1, noise, &g1);
    const double chord = costOf({}, 4, noise, &g4);
    const double full = costOf({{Density, 100}, {Size, 80}, {PitchSpray, 30}}, 6, noise, &gFull);
    const double plain = costOf({{Density, 100}, {Size, 80}, {Root, 48}}, 1, noise, &gPlain);
    std::printf("  cost: one key of preset 0 %.3f%% of a core (%d grains; target 0.08%%)\n", one, g1);
    std::printf("  cost: a 4-key chord of preset 0 %.3f%% (%d grains)\n", chord, g4);
    std::printf("  cost: the pool full %.3f%% (%d grains; target 0.5%%, ceiling 0.7%%)\n", full, gFull);
    std::printf("  cost: one key at ROOT, whole samples, %d grains %.3f%%\n", gPlain, plain);
}

// ---- the program ---------------------------------------------------------------------------

struct Named { const char* name; void (*run)(); };

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testSettings", testSettings},
        {"testReadings", testReadings},
        {"testRolls", testRolls},
        {"testOnsets", testOnsets},
        {"testWindow", testWindow},
        {"testLevel", testLevel},
        {"testPitchRule", testPitchRule},
        {"testPlace", testPlace},
        {"testTiming", testTiming},
        {"testDensity", testDensity},
        {"testPitch", testPitch},
        {"testNoClick", testNoClick},
        {"testCloudLevel", testCloudLevel},
        {"testBlocks", testBlocks},
        {"testThroughTheEngine", testThroughTheEngine},
        {"testGrainPlaces", testGrainPlaces},
        {"testNoAllocation", testNoAllocation},
        {"testTooSmall", testTooSmall},
        {"testNaN", testNaN},
        {"testSilence", testSilence},
        {"testPlayFromTheTop", testPlayFromTheTop},
        {"testSwap", testSwap},
        {"testSteal", testSteal},
        {"testTone", testTone},
        {"testPresets", testPresets},
        {"testGlide", testGlide},
        {"testScanAndSpray", testScanAndSpray},
        {"testReverseStereoVelocity", testReverseStereoVelocity},
        {"testTheRead", testTheRead},
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
    if (gPrint && (only.empty() || (only.size() == 1 && only[0] == "cost"))) measureCost();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("cloud tests passed\n");
    return 0;
}
