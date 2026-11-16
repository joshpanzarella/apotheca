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

#include "apotheca/cloud_rule.hpp"
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
    // The table read between its points is the half cosine within 1e-5.
    const WindowTable t;
    double worst = 0.0;
    for (int i = 0; i <= 10000; ++i) {
        const double x = i / 10000.0;
        worst = std::max(worst, std::fabs(t.at(static_cast<float>(x)) - halfCosine(x)));
    }
    CHECK(worst < 2e-5);
    CHECK(t.at(0.f) == 0.f && t.at(1.f) == 1.f && t.at(-1.f) == 0.f && t.at(2.f) == 1.f);
    // Either piece's mean square is 3/8.
    double ms = 0.0;
    for (int i = 0; i < 100000; ++i) { const double w = halfCosine((i + 0.5) / 100000.0); ms += w * w; }
    CHECK(near(ms / 100000.0, kWindowPower, 1e-6));
    if (gPrint) std::printf("  the window's table: %.2g at worst from the half cosine\n", worst);
}

// The overlap gain and the sides.
static void testLevel() {
    CHECK(near(overlapGain(16.0, 0.120), 1.0 / std::sqrt(1.92), 1e-12)); // 1.92 grains
    CHECK(near(overlapGain(16.0, 0.125), 1.0 / std::sqrt(2.0), 1e-12));
    CHECK(overlapGain(1.0, 0.005) == 1.0 && overlapGain(200.0, 0.001) == 1.0); // sparse: full
    CHECK(near(overlapGain(200.0, 1.0), 1.0 / std::sqrt(200.0), 1e-12));
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
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("cloud tests passed\n");
    return 0;
}
