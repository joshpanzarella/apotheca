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
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "apotheca/acid_rule.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::acid;

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
    CHECK(Level == 13 && Wave == 0 && Slide == 11);
    for (int id = 0; id < kSettings; ++id) {
        CHECK(settingMin(id) <= settingUsual(id) && settingUsual(id) <= settingMax(id));
        CHECK(settingHeld(id, -1000) == settingMin(id) && settingHeld(id, 1000) == settingMax(id));
    }
    CHECK(settingMin(Octave) == -2 && settingMax(Octave) == 1);
    CHECK(settingMin(Tune) == -100 && settingMax(Tune) == 100);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6);
    CHECK(settingMax(Wave) == 1 && settingMax(Slide) == 1 && settingMax(Velocity) == 1);
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

// ---- the program ---------------------------------------------------------------------------

struct Named { const char* name; void (*run)(); };

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testSettings", testSettings},
        {"testRule", testRule},
        {"testSlideShape", testSlideShape},
        {"testReadings", testReadings},
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
    std::printf("acid tests passed\n");
    return 0;
}
