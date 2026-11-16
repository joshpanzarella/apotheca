// PHYSIC (docs/plans/physic-plan.md), the engine part: the rule header's tables and sums, the
// modes, the string, the strike, the voices and the dice, through the synth and through the
// engine, and the house rules (blocks, Play from the top, no allocation, no number too small to
// hold, NaN, silence).
//
// Each test says FAITHFUL (held to a published figure or formula: its source named, as the plan's
// section 0 names them) or SELF (held to the plan's own choice). A green SELF test only says the
// code does what the plan chose. NOBODY HAS HEARD ANY OF IT.
//
//   physic_test                  all of them
//   physic_test testTuning ...   just those
//   physic_test --measure        the figures: the cost of 8 notes of each model, the tuning's worst
//   physic_test --print          PHYSIC's own song's checksum (remade only from code known to be right)

// (As physic.cpp: no multiply and add joined, so this program's sums are the desk's on the handheld.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include <algorithm>
#include <cfloat>
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

#include "apotheca/denormals.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/physic.hpp"
#include "apotheca/physic_rule.hpp"
#include "measure.hpp"
#include "physic/body.hpp"
#include "physic/strike.hpp"

using namespace apotheca;
using namespace apotheca::physic;

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

static const double kRate = 48000.0;
// (The readers, phaseAt, centsOff, sizeAt and t60At, are measure.hpp's.)

struct Set { int id, value; };

// A PHYSIC with `sets` on preset `preset` (-1: as made).
static std::unique_ptr<Physic> synth(const std::vector<Set>& sets = {}, int preset = -1) {
    std::unique_ptr<Physic> p(new Physic);
    p->prepare(48000.f);
    if (preset >= 0) p->selectPreset(preset);
    for (const Set& s : sets) p->setSetting(s.id, s.value);
    return p;
}
// `frames` of its sound, in blocks of `block`.
static std::vector<float> play(Physic& p, std::size_t frames, std::vector<float>* right = nullptr, int block = 256) {
    std::vector<float> l(frames, 99.f), r(frames, 99.f); // 99: an unwritten sample shows
    for (std::size_t at = 0; at < frames; at += static_cast<std::size_t>(block)) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), frames - at));
        p.render(l.data() + at, r.data() + at, n);
    }
    if (right) *right = r;
    return l;
}

// A plain sound to measure a model's modes on: METAL (every mode rings for DECAY), DECAY 100
// (30 s), struck at 40 (where every mode of every shape is awake), velocity ignored, no dice, no
// width, LEVEL 0.
static std::vector<Set> plain(int model, std::vector<Set> more = {}) {
    std::vector<Set> s = {{Model, model}, {Strike, 1}, {Hardness, 80}, {Position, 40}, {Decay, 100}, {Material, 100}, {Stretch, 50},
                          {Release, 100}, {Velocity, 0}, {Octave, 0}, {Fine, 0}, {Width, 0}, {Level, 0}, {Dice, 0}};
    s.insert(s.end(), more.begin(), more.end());
    return s;
}

// ---- reading a sound -----------------------------------------------------------------------------

// ---- the tables (batch 0) --------------------------------------------------------------------------

// A root of f in [lo, hi] (where f changes sign), halved down to nothing.
static double rootOf(const std::function<double(double)>& f, double lo, double hi) {
    double flo = f(lo);
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi), fm = f(mid);
        if ((fm < 0.0) == (flo < 0.0)) lo = mid, flo = fm;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

// FAITHFUL: each table is the equation it names, worked out again here.
static void testTables() {
    // The free-free bar: cos k cosh k = 1 (FR 2.15), the n-th root near (n + 1/2) pi.
    for (int n = 0; n < 6; ++n) {
        const double guess = (n + 1.5) * kPi;
        const double k = rootOf([](double x) { return std::cos(x) * std::cosh(x) - 1.0; }, guess - 0.5, guess + 0.5);
        CHECK(std::fabs(k - kFreeBar[n]) < 1e-9);
    }
    // The clamped-free bar: cos k cosh k = -1.
    for (int n = 0; n < 4; ++n) {
        const double guess = n == 0 ? 1.875 : (n + 0.5) * kPi;
        const double k = rootOf([](double x) { return std::cos(x) * std::cosh(x) + 1.0; }, guess - 0.4, guess + 0.4);
        CHECK(std::fabs(k - kTineBar[n]) < 1e-9);
    }
    const ModelRow glock = modelRow(kGlock), kalimba = modelRow(kKalimba), bell = modelRow(kBell), drum = modelRow(kDrum);
    const double glockAsPublished[6] = {1.0, 2.7565, 5.4039, 8.9330, 13.3443, 18.6379}; // to 4 places (the plan wrote 18.6372 for the last: a slip; the root gives 18.63789)
    for (int k = 0; k < 6; ++k) CHECK(std::fabs(glock.ratio[k] - glockAsPublished[k]) < 6e-5);
    CHECK(std::fabs(kalimba.ratio[1] - 5.6) < 1e-12); // Ch: real tines 5.3 to 5.9 (the ideal bar 6.267)
    CHECK(std::fabs(kTineBar[1] * kTineBar[1] / (kTineBar[0] * kTineBar[0]) - 6.267) < 1e-3);
    CHECK(std::fabs(kalimba.ratio[2] - 17.55) < 6e-3 && std::fabs(kalimba.ratio[3] - 34.39) < 6e-3);
    // BELL, from Hibbert's cents (not a rounded 1.2): the strike note an octave under the nominal.
    CHECK(bell.played == 1 && bell.ratio[1] == 1.0 && bell.ratio[0] == 0.5 && bell.ratio[4] == 2.0);
    CHECK(std::fabs(bell.ratio[2] - 1.18921) < 5e-6 && std::fabs(bell.ratio[3] - 1.49831) < 5e-6);
    CHECK(bell.ratio[2] != 1.2);
    // The round skin: each mode a zero of J_n (AS table 9.5), found again in the series.
    for (int k = 0; k < drum.modes; ++k) {
        const int n = drum.beam[k];
        const double z = rootOf([n](double x) { return besselJ(n, x); }, drum.zero[k] - 0.2, drum.zero[k] + 0.2);
        CHECK(std::fabs(z - drum.zero[k]) < 1e-9);
        CHECK(std::fabs(drum.ratio[k] - drum.zero[k] / drum.zero[0]) < 1e-15);
    }
    const double drumAsPublished[8] = {1.0, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173, 3.1555, 3.5001};
    for (int k = 0; k < 8; ++k) CHECK(std::fabs(drum.ratio[k] - drumAsPublished[k]) < 6e-5);
    // The rest are the published figures as written.
    const ModelRow marimba = modelRow(kMarimba), xylo = modelRow(kXylo), vibes = modelRow(kVibes), chime = modelRow(kChime), glass = modelRow(kGlass);
    CHECK(marimba.ratio[1] == 4.0 && marimba.ratio[2] == 10.0);     // YR; FR ch. 19
    CHECK(xylo.ratio[1] == 3.0);                                     // R00
    CHECK(vibes.ratio[1] == 4.0 && vibes.ratio[2] == 10.0);         // R00
    const double chimeHib[6] = {0.65, 1.25, 1.99, 2.86, 3.81, 4.86}; // Hib
    for (int k = 0; k < 6; ++k) CHECK(chime.ratio[k] == chimeHib[k]);
    CHECK(chime.played == -1);
    const double glassCs[5] = {1.0, 2.32, 4.25, 6.63, 9.38};         // Cs after Rossing
    for (int k = 0; k < 5; ++k) CHECK(glass.ratio[k] == glassCs[k]);
    // Every row: ascending, at most the cap, the played mode (where there is one) at ratio 1.
    for (int m = 1; m < kModels; ++m) {
        const ModelRow r = modelRow(m);
        CHECK(r.modes >= 3 && r.modes <= kModesMost);
        for (int k = 1; k < r.modes; ++k) CHECK(r.ratio[k] > r.ratio[k - 1]);
        if (r.played >= 0) CHECK(r.ratio[r.played] == 1.0);
    }
}

// FAITHFUL: where a thing is struck. The free bar's mode 2 has a node at its centre, mode 1 at
// 0.224 of its length (FR 2.15); a skin's J_n(0) is 0 for every n >= 1 (AS).
static void testShapes() {
    for (int n = 1; n < 6; n += 2) CHECK(std::fabs(freeBarShape(kFreeBar[n], 0.5)) < 1e-9);
    for (int n = 0; n < 6; ++n) CHECK(std::fabs(freeBarShape(kFreeBar[n], 0.0) - 1.0) < 1e-9);
    const double node = rootOf([](double x) { return freeBarShape(kFreeBar[0], x); }, 0.15, 0.3);
    CHECK(std::fabs(node - 0.2242) < 1e-4);
    for (int n = 1; n < 5; ++n) CHECK(besselJ(n, 0.0) == 0.0);
    CHECK(besselJ(0, 0.0) == 1.0);
    CHECK(std::fabs(besselJ(0, 1.0) - 0.7651976865579666) < 1e-14); // AS table 9.1
    CHECK(std::fabs(besselJ(1, 2.0) - 0.5767248077568734) < 1e-14);
    for (int n = 0; n < 4; ++n) CHECK(std::fabs(tineShape(kTineBar[n], 0.0) - 1.0) < 1e-9);
    CHECK(positionAlong(0) == 0.01 && positionAlong(100) == 0.5);
}

// The sums of the rule: the damping law's ends, Hertz, the mallet's spectrum, STRETCH.
static void testRule() {
    CHECK(std::fabs(decaySeconds(0) - 0.05) < 1e-12 && std::fabs(decaySeconds(100) - 30.0) < 1e-9);
    CHECK(materialB(100) == 0.0 && materialB(0) == kWoodB);
    CHECK(releaseRings(100) && !releaseRings(99));
    CHECK(std::fabs(releaseSeconds(0) - 0.01) < 1e-12 && std::fabs(releaseSeconds(100) - 3.0) < 1e-9);
    // FAITHFUL (Hz): contact time against velocity on a log-log line, slope -1/5 at VELOCITY 100.
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const int n = 10;
        for (int i = 0; i < n; ++i) {
            const double v = 0.1 + 0.09 * i, x = std::log(v), y = std::log(contactSeconds(50, v, 1.0));
            sx += x, sy += y, sxx += x * x, sxy += x * y;
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        CHECK(std::fabs(slope + 0.2) < 0.01);
        CHECK(contactSeconds(50, 0.2, 0.0) == contactSeconds(50, 1.0, 0.0)); // VELOCITY 0: velocity changes nothing
    }
    CHECK(std::fabs(contactSeconds(0, 1, 1) - 0.008) < 1e-12 && std::fabs(contactSeconds(100, 1, 1) - 0.0003) < 1e-12);
    CHECK(velocityLevel(0.3, 0.0) == 1.0 && std::fabs(velocityLevel(0.3, 1.0) - 0.3) < 1e-12);
    // The bump's spectrum: 1 at DC, falling all the way (no zero: a key on a half-sine's zero was
    // silent), 12 dB an octave far above its corner.
    {
        const double q = std::exp(-1.0 / 10.0);
        CHECK(std::fabs(bumpSpectrum(q, 0.0) - 1.0) < 1e-12);
        double last = 1.0;
        for (double w = 0.01; w < 3.1; w += 0.01) {
            const double h = bumpSpectrum(q, w);
            CHECK(h < last && h > 0.0);
            last = h;
        }
        CHECK(std::fabs(dB(bumpSpectrum(q, 0.8) / bumpSpectrum(q, 0.4)) + 12.0) < 1.5);
    }
    // STRETCH 50 is the table to the bit (FAITHFUL); 0 a whole number; the played mode never moves.
    for (int m = 1; m < kModels; ++m) {
        const ModelRow r = modelRow(m);
        for (int k = 0; k < r.modes; ++k) CHECK(stretched(r.ratio[k], 50) == r.ratio[k]);
        if (r.played >= 0) CHECK(stretched(1.0, 0) == 1.0 && stretched(1.0, 100) == 1.0);
    }
    CHECK(stretched(4.3, 0) == 4.0 && stretched(1.18921, 0) == 1.0 && stretched(0.65, 0) == 0.5);
    CHECK(std::fabs(stretched(4.0, 100) - std::pow(4.0, 1.3)) < 1e-12);
    CHECK(stiffnessOf(0) == 0.0 && std::fabs(stiffnessOf(100) - 2e-3) < 1e-15);
    // The held pitch.
    CHECK(heldPitch(130, false) == 127 && heldPitch(115, true) == 112 && heldPitch(10, true) == 24 && heldPitch(10, false) == 10);
    CHECK(nyquistFade(0.40 * kRate, kRate) == 1.0 && nyquistFade(0.45 * kRate, kRate) == 0.0 && nyquistFade(0.425 * kRate, kRate) > 0.49);
    // The tuning solve: the loop's whole delay at f0 is SR / f0.
    for (double hz : {33.0, 261.6, 1000.0, 4186.0}) {
        const double w = 2.0 * kPi * hz / kRate;
        for (double pole : {0.0, 0.5}) {
            const double disp = dispersionOf(1e-3, hz, kRate);
            const Tuning t = tuningOf(hz, kRate, pole, disp);
            CHECK(t.part >= 0.5 && t.part < 1.5);
            const double total = t.whole + allpassDelay(t.eta, w) + lowpassDelay(pole, w) + dispersionDelay(disp, w) + dcDelay(dcPole(hz, kRate), w);
            CHECK(std::fabs(total - kRate / hz) < 1e-9 * kRate / hz);
        }
    }
    // The loop filter keeps exp(-a / f0) each pass at the fundamental, exactly, whatever MATERIAL.
    for (double hz : {65.0, 261.6, 2000.0}) {
        const double a = 0.5, b = materialB(30);
        const LoopFilter lf = loopFilterOf(a, b, hz, kRate);
        const double w = 2.0 * kPi * hz / kRate;
        CHECK(std::fabs(lf.gain * lowpassSize(lf.pole, w) * dcSize(dcPole(hz, kRate), w) - std::exp(-a / hz)) < 1e-12);
        // The DC blocker passes the fundamental all but whole (it is in the sum above), and DC not at all.
        CHECK(dcSize(dcPole(hz, kRate), w) > 0.9999 && dcSize(dcPole(hz, kRate), 1e-12) < 1e-6);
        CHECK(lf.pole > 0.0);
        CHECK(lf.gain < 1.0 && lf.pole >= 0.0 && lf.pole <= kLoopPoleMost);
    }
}

// ---- the modes ---------------------------------------------------------------------------------------

static double soundingHz(double pitch) { return hzOf(pitch); }

// FAITHFUL: every modal model in tune within 0.5 cent of equal temperament, at the sounding pitch
// (key, OCTAVE and FINE together), keys 12 to 120, read by phase over 60 periods. The pitch is
// the mode at the played note (BELL: the prime). CHIME has none: each of its partials is held to
// its table ratio times the note.
static double gWorstModal = 0.0, gWorstString = 0.0;
static void testTuning() {
    double worst = 0.0;
    for (int model = 1; model < kModels; ++model) {
        const ModelRow row = modelRow(model);
        struct Way { int key, octave, fine; };
        std::vector<Way> ways;
        for (int key = 12; key <= 120; key += 9) ways.push_back({key, 0, 0});
        ways.push_back({36, -2, 0});  // sounding 12
        ways.push_back({96, 2, 0});   // sounding 120
        ways.push_back({60, 0, 50});
        ways.push_back({61, 1, -37});
        for (const Way& w : ways) {
            const double pitch = w.key + 12.0 * w.octave + w.fine / 100.0;
            auto p = synth(plain(model, {{Octave, w.octave}, {Fine, w.fine}}));
            p->noteOn(w.key, 0.8f);
            const double f = soundingHz(pitch);
            const double longest = (60.0 + 32.0 + 2.0) / (f * row.ratio[0]) + 0.1;
            const std::vector<float> v = play(*p, static_cast<std::size_t>(longest * kRate) + 4800);
            for (int k = 0; k < row.modes; ++k) {
                if (row.played >= 0 && k != row.played) continue;
                const double hz = f * row.ratio[k];
                if (hz > 0.38 * kRate) continue;
                const double off = std::fabs(centsOff(v, hz, 0.05));
                worst = std::max(worst, off);
                if (off > 0.5) std::printf("  %s key %d oct %d fine %d mode %d: %.3f cents\n", modelName(model), w.key, w.octave, w.fine, k, off);
                CHECK(off < 0.5);
            }
        }
    }
    gWorstModal = worst;
    if (gPrint) std::printf("  tuning, every modal model, keys 12 to 120: worst %.4f cents\n", worst);
}

// FAITHFUL (each table, through the sound): each model's modes ring at the table's ratios within
// 0.5 cent at STRETCH 50 (WOOD's table is SELF), at key 48.
static void testModeRatios() {
    for (int model = 1; model < kModels; ++model) {
        const ModelRow row = modelRow(model);
        auto p = synth(plain(model));
        p->noteOn(48, 0.8f);
        const double f = soundingHz(48);
        const std::vector<float> v = play(*p, static_cast<std::size_t>(((60.0 + 32.0 + 2.0) / (f * row.ratio[0]) + 0.1) * kRate) + 4800);
        double hz[kModesMost];
        CHECK(p->modesOf(0, hz) == row.modes);
        for (int k = 0; k < row.modes; ++k) {
            CHECK(hz[k] == f * row.ratio[k]); // STRETCH 50: the table to the bit
            if (hz[k] > 0.38 * kRate) continue;
            const double off = std::fabs(centsOff(v, hz[k], 0.05));
            if (off > 0.5) std::printf("  %s mode %d (%.4f): %.3f cents\n", modelName(model), k, row.ratio[k], off);
            CHECK(off < 0.5);
        }
    }
}

// The pitch is held where the core can play it: a modal model at 127, STRING at 112 and 24.
static void testHeldAtTheEdge() {
    {
        auto p = synth(plain(kMarimba, {{Octave, 2}}));
        p->noteOn(120, 0.8f); // sounding 144: held at 127
        play(*p, 256);
        double hz[kModesMost];
        CHECK(p->modesOf(0, hz) > 0 && std::fabs(hz[0] - hzOf(127)) < 1e-9);
        const std::vector<float> v = play(*p, 9600);
        CHECK(std::fabs(centsOff(v, hzOf(127), 0.01)) < 0.5);
    }
    // A mode's share fades out between 0.40 and 0.45 of the sample rate, and above it is silent.
    {
        // GLOCK's third mode (5.4039) at 0.425 of the sample rate: key 106 and 15 cents.
        auto p = synth(plain(kGlock, {{Fine, 15}}));
        p->noteOn(106, 1.f);
        const std::vector<float> v = play(*p, 9600);
        double hz[kModesMost];
        float heard[kModesMost];
        CHECK(p->modesOf(0, hz, heard) == 6);
        for (int k = 0; k < 6; ++k) CHECK(heard[k] == static_cast<float>(nyquistFade(hz[k], kRate)));
        CHECK(heard[2] > 0.4f && heard[2] < 0.6f && heard[3] == 0.f);
        // Above: nothing where its fold would land.
        const double folded = kRate - hz[3];
        CHECK(sizeAt(v, folded, 0.01, 64.0) < 1e-6);
    }
    for (int key : {12, 115}) {
        auto p = synth(plain(kString));
        p->noteOn(key, 0.8f);
        const double held = key < 24 ? 24.0 : 112.0;
        const std::vector<float> v = play(*p, 48000 * 4);
        CHECK(std::fabs(centsOff(v, hzOf(held), 0.05)) < 1.0);
    }
}

// FAITHFUL: a part's place decides which modes wake. A bar struck at its centre: mode 2 (which has a
// node there) at least 30 dB under mode 1. At 0.224: mode 1 (a node) at least 30 dB under mode 2. A
// skin at its centre: every (n >= 1) mode at least 30 dB under (0, 1). A string plucked at 1/5:
// harmonics 5 and 10 at least 30 dB under their neighbours; at 1/2, the even ones.
static void testPosition() {
    const auto modeSizes = [](int model, float positionMacro, int position, std::vector<double>& size) {
        auto p = synth(plain(model, {{Position, position}, {Hardness, 100}}));
        if (positionMacro >= 0.f) p->setMacro(MacroPosition, positionMacro);
        p->noteOn(48, 1.f);
        const std::vector<float> v = play(*p, 48000);
        double hz[kModesMost];
        const int n = p->modesOf(0, hz);
        size.assign(static_cast<std::size_t>(n), 0.0);
        for (int k = 0; k < n; ++k) size[static_cast<std::size_t>(k)] = sizeAt(v, hz[k], 0.02, 64.0);
    };
    std::vector<double> s;
    modeSizes(kMarimba, -1.f, 100, s); // 50% along: the centre
    CHECK(dB(s[1] / s[0]) < -30.0);
    // 0.2242 along: POSITION 43.71, from the macro over a patch at 0.
    modeSizes(kGlock, static_cast<float>(0.5 + (0.2242 - 0.01) / 0.49 / 2.0), 0, s);
    CHECK(dB(s[0] / s[1]) < -30.0);
    modeSizes(kDrum, -1.f, 100, s);
    const ModelRow drum = modelRow(kDrum);
    for (int k = 1; k < drum.modes; ++k)
        if (drum.beam[k] >= 1) CHECK(dB(s[static_cast<std::size_t>(k)] / s[0]) < -30.0);
    CHECK(dB(s[3] / s[0]) > -30.0); // (0, 2) is not
    // The string: plucked at 1/5 (POSITION 38.78, by the macro) and at 1/2.
    const auto harmonics = [](float positionMacro, int position, std::vector<double>& size) {
        auto p = synth(plain(kString, {{Strike, 0}, {Position, position}, {Hardness, 100}, {Stretch, 0}}));
        if (positionMacro >= 0.f) p->setMacro(MacroPosition, positionMacro);
        p->noteOn(45, 1.f); // 110 Hz
        const std::vector<float> v = play(*p, 48000);
        size.assign(12, 0.0);
        for (int n = 1; n < 12; ++n) size[static_cast<std::size_t>(n)] = sizeAt(v, 110.0 * n, 0.05, 64.0);
    };
    harmonics(static_cast<float>(0.5 + (0.2 - 0.01) / 0.49 / 2.0), 0, s);
    if (gPrint)
        for (int n = 1; n < 12; ++n) std::printf("  plucked at 1/5: harmonic %d %.1f dB\n", n, dB(s[static_cast<std::size_t>(n)]));
    // (Against the power of the harmonics that are not nulled: a pluck is noise, so any one
    // neighbour may be faint by chance.)
    const auto around = [&s](int skip) {
        double e = 0.0;
        int n = 0;
        for (int k = 1; k < 12; ++k)
            if (k % skip) e += s[static_cast<std::size_t>(k)] * s[static_cast<std::size_t>(k)], ++n;
        return std::sqrt(e / n);
    };
    CHECK(dB(s[5] / around(5)) < -30.0);
    CHECK(dB(s[10] / around(5)) < -30.0);
    CHECK(dB(s[3] / around(5)) > -30.0);
    harmonics(-1.f, 100, s);
    CHECK(dB(s[2] / around(2)) < -30.0 && dB(s[4] / around(2)) < -30.0 && dB(s[10] / around(2)) < -30.0);
}

// SELF (the law FAITHFUL, CD): on METAL every mode's T60 is DECAY's seconds within 5%, at five
// settings and five keys; toward WOOD no mode rings longer than DECAY. RELEASE the same after let go;
// RING ignores let go.
static void testDecay() {
    for (int decay : {20, 40, 60, 80, 95}) {
        const double t60 = decaySeconds(decay);
        for (int key : {36, 48, 60, 72, 84}) {
            auto p = synth(plain(kMarimba, {{Decay, decay}}));
            p->noteOn(key, 0.8f);
            const double f = hzOf(key);
            const double span = std::min(t60 * 0.5, 2.0);
            const std::vector<float> v = play(*p, static_cast<std::size_t>((0.05 + span + 0.3) * kRate));
            for (int k = 0; k < 3; ++k) {
                const double hz = f * modelRow(kMarimba).ratio[k];
                if (hz > 0.38 * kRate) continue;
                const double got = t60At(v, hz, 0.03, span, std::max(6.0, std::min(32.0, span * hz / 3.0)));
                if (std::fabs(got / t60 - 1.0) > 0.05) std::printf("  DECAY %d key %d mode %d: T60 %.3f s, want %.3f\n", decay, key, k, got, t60);
                CHECK(std::fabs(got / t60 - 1.0) < 0.05);
            }
        }
    }
    // Toward WOOD: shorter, never longer.
    for (int material : {0, 30, 70}) {
        auto p = synth(plain(kMarimba, {{Decay, 60}, {Material, material}}));
        p->noteOn(60, 0.8f);
        const std::vector<float> v = play(*p, 48000 * 2);
        for (int k = 0; k < 3; ++k) {
            const double hz = hzOf(60) * modelRow(kMarimba).ratio[k];
            const double got = t60At(v, hz, 0.02, 0.1, std::max(32.0, hz * 0.01));
            CHECK(got < decaySeconds(60) * 1.03);
        }
    }
    // RELEASE: after let go, its T60 (reached over 5 ms); RING ignores let go.
    for (int release : {30, 60}) {
        auto p = synth(plain(kVibes, {{Release, release}}));
        p->noteOn(60, 0.8f);
        play(*p, 4800);
        p->noteOff(60);
        play(*p, 480); // past the 5 ms
        const std::vector<float> v = play(*p, 48000);
        const double want = releaseSeconds(release);
        const double got = t60At(v, hzOf(60), 0.0, want * 0.4, 16.0);
        if (std::fabs(got / want - 1.0) > 0.1) std::printf("  RELEASE %d: T60 %.3f s, want %.3f\n", release, got, want);
        CHECK(std::fabs(got / want - 1.0) < 0.1);
    }
    {
        auto ring = synth(plain(kVibes, {{Decay, 60}}));
        auto held = synth(plain(kVibes, {{Decay, 60}}));
        ring->noteOn(60, 0.8f);
        held->noteOn(60, 0.8f);
        play(*ring, 4800), play(*held, 4800);
        ring->noteOff(60);
        Sum a, b;
        a.add(play(*ring, 48000)), b.add(play(*held, 48000));
        CHECK(a.h == b.h); // RING: let go does nothing, to the bit
    }
    // The STRING's fundamental: DECAY's T60 at every MATERIAL (JS's decay stretching), every key.
    for (int material : {0, 50, 100})
        for (int key : {36, 60, 84}) {
            auto p = synth(plain(kString, {{Strike, 0}, {Decay, 60}, {Material, material}, {Stretch, 0}}));
            p->noteOn(key, 0.8f);
            const double t60 = decaySeconds(60), f = hzOf(key);
            const std::vector<float> v = play(*p, static_cast<std::size_t>((0.1 + t60 * 0.5 + 0.4) * kRate));
            const double got = t60At(v, f, 0.1, t60 * 0.5, 32.0);
            if (std::fabs(got / t60 - 1.0) > 0.05) std::printf("  STRING MATERIAL %d key %d: T60 %.3f s, want %.3f\n", material, key, got, t60);
            CHECK(std::fabs(got / t60 - 1.0) < 0.05);
        }
}

// The law (CD, FAITHFUL as a law; its values SELF): each mode's fitted 1/tau = a + b f^2 within 5%.
static void testMaterial() {
    for (int material : {0, 40}) {
        auto p = synth(plain(kGlock, {{Decay, 80}, {Material, material}}));
        p->noteOn(60, 0.8f);
        const std::vector<float> v = play(*p, 48000 * 2);
        const double a = rateOfT60(decaySeconds(80)), b = materialB(material);
        for (int k = 0; k < 4; ++k) {
            const double hz = hzOf(60) * modelRow(kGlock).ratio[k];
            const double want = a + b * hz * hz;
            const double t60 = t60At(v, hz, 0.02, std::min(0.5, 3.0 / want), std::max(32.0, hz * 0.005));
            const double got = rateOfT60(t60);
            if (std::fabs(got / want - 1.0) > 0.05) std::printf("  MATERIAL %d mode %d: 1/tau %.3f, want %.3f\n", material, k, got, want);
            CHECK(std::fabs(got / want - 1.0) < 0.05);
        }
    }
}

// ...and a glide on WOOD takes each mode's damping with it: landed an octave up, the modes die as the
// law says at their new frequencies.
static void testMaterialGlides() {
    auto p = synth(plain(kGlock, {{Decay, 80}, {Material, 30}}));
    p->noteOnFrom(60, 0.8f, 48, 0.05f);
    const std::vector<float> v = play(*p, 48000 * 2);
    const double a = rateOfT60(decaySeconds(80)), b = materialB(30);
    for (int k = 0; k < 3; ++k) {
        const double hz = hzOf(60) * modelRow(kGlock).ratio[k];
        const double want = a + b * hz * hz;
        const double got = rateOfT60(t60At(v, hz, 0.1, std::min(0.5, 3.0 / want), std::max(32.0, hz * 0.005)));
        CHECK(std::fabs(got / want - 1.0) < 0.05);
    }
}

// FAITHFUL (Hz, through the sound): a harder hit is brighter as well as louder. The spectral centroid
// of a MARIMBA's first 0.2 s rises with velocity (VELOCITY 100), and not at VELOCITY 0.
static double centroidOf(int velocityAmount, float velocity) {
    auto p = synth(plain(kMarimba, {{Velocity, velocityAmount}, {Hardness, 40}, {Position, 20}}));
    p->noteOn(48, velocity);
    const std::vector<float> v = play(*p, 9600);
    double num = 0.0, den = 0.0;
    double hz[kModesMost];
    const int n = p->modesOf(0, hz);
    for (int k = 0; k < n; ++k) {
        const double s = sizeAt(v, hz[k], 0.0, 64.0);
        num += hz[k] * s * s, den += s * s;
    }
    return num / den;
}
static void testHertz() {
    const double soft = centroidOf(100, 0.2f), mid = centroidOf(100, 0.5f), hard = centroidOf(100, 1.f);
    CHECK(soft < mid && mid < hard);
    CHECK(centroidOf(0, 0.2f) == centroidOf(0, 1.f));
    if (gPrint) std::printf("  Hertz: a MARIMBA's centroid at velocity 0.2 / 0.5 / 1: %.0f / %.0f / %.0f Hz\n", soft, mid, hard);
}

// STRETCH turned retunes ringing modes at once (on the tick); 0 is whole numbers, 100 further apart.
static void testStretch() {
    auto p = synth(plain(kBell));
    p->noteOn(60, 0.8f);
    play(*p, 256);
    double hz[kModesMost];
    p->modesOf(0, hz);
    CHECK(hz[2] == hzOf(60) * modelRow(kBell).ratio[2]);
    p->setSetting(Stretch, 0);
    play(*p, 32);
    p->modesOf(0, hz);
    CHECK(std::fabs(hz[2] / hzOf(60) - 1.0) < 1e-12 && std::fabs(hz[0] / hzOf(60) - 0.5) < 1e-12);
    p->setSetting(Stretch, 100);
    play(*p, 32);
    p->modesOf(0, hz);
    CHECK(std::fabs(hz[4] / hzOf(60) - std::pow(2.0, 1.3)) < 1e-9);
}

// SELF: a macro stores a number and returns (early on the value it has); nothing is worked out until
// each voice's next tick, and then once however many times it was turned (a MOD gives values often).
static void testMacrosCheap() {
    for (int model : {kMarimba, kString}) {
        auto p = synth(plain(model, {{Strike, model == kString ? 0 : 1}}));
        p->noteOn(60, 0.8f);
        p->noteOn(64, 0.8f);
        play(*p, 1600);
        const long rest = p->tunings();
        play(*p, 1600);
        CHECK(p->tunings() == rest); // ringing at rest: nothing worked out again
        p->setMacro(MacroDecay, 0.5f);
        p->setMacro(MacroStretch, 0.5f);
        play(*p, 160);
        CHECK(p->tunings() == rest); // the value it had: nothing
        for (int i = 0; i < 50; ++i) p->setMacro(MacroMaterial, 0.5f + 0.001f * i);
        CHECK(p->tunings() == rest); // nothing on the call
        play(*p, 16);
        CHECK(p->tunings() == rest + 2); // once a voice, on its tick
        p->setMacro(MacroTone, 0.9f);   // HARDNESS: the next strike's
        p->setMacro(MacroPosition, 0.1f);
        p->setMacro(MacroDice, 0.9f);
        play(*p, 16);
        CHECK(p->tunings() <= rest + 2 + 2 + 2); // (the voices look once; nothing they ring with moved)
    }
    // OCTAVE's macro: a threshold, down under a quarter and up over three.
    auto p = synth(plain(kMarimba));
    double hz[kModesMost];
    p->setMacro(MacroOctave, 0.2f);
    p->noteOn(60, 0.8f);
    play(*p, 16);
    CHECK(p->modesOf(0, hz) > 0 && std::fabs(hz[0] - hzOf(48)) < 1e-9);
    p->setMacro(MacroOctave, 0.5f);
    p->noteOn(62, 0.8f);
    p->setMacro(MacroOctave, 0.8f);
    p->noteOn(64, 0.8f);
    play(*p, 16);
    CHECK(p->modesOf(1, hz) > 0 && std::fabs(hz[0] - hzOf(62)) < 1e-9);
    CHECK(p->modesOf(2, hz) > 0 && std::fabs(hz[0] - hzOf(76)) < 1e-9);
}

// ---- the string ---------------------------------------------------------------------------------------

// FAITHFUL (JS, PASP): the string in tune within 0.5 cent, keys 24 to 108 (sounding), at STRETCH 0
// and 100, at MATERIAL 0, 50 and 100, read by phase over 60 periods.
static void testStringTuning() {
    double worst = 0.0;
    for (int stretch : {0, 100})
        for (int material : {0, 50, 100})
            for (int key = 24; key <= 108; key += (material == 50 ? 3 : 12)) {
                auto p = synth(plain(kString, {{Strike, 0}, {Stretch, stretch}, {Material, material}, {Hardness, 30}}));
                p->noteOn(key, 0.8f);
                const double f = hzOf(key);
                const std::vector<float> v = play(*p, static_cast<std::size_t>(((60.0 + 32.0) / f + 0.15) * kRate) + 2000);
                const double off = std::fabs(centsOff(v, f, 0.1));
                worst = std::max(worst, off);
                if (off > 0.5) std::printf("  STRING key %d STRETCH %d MATERIAL %d: %.3f cents\n", key, stretch, material, off);
                CHECK(off < 0.5);
            }
    gWorstString = worst;
    if (gPrint) std::printf("  tuning, STRING keys 24 to 108: worst %.4f cents\n", worst);
}

// FAITHFUL to the formula where the allpasses reach it (F64): at STRETCH 100 (B 2e-3) partial 6, the
// one the stiffness is fitted to, within 3 cents of n f0 sqrt(1 + B n^2) / sqrt(1 + B), on keys 48 to
// 72; the partials around it sharp, in order. Said for the rest (--measure).
static void testStiffness() {
    const double b = stiffnessOf(100);
    for (int key : {48, 60, 72}) {
        auto p = synth(plain(kString, {{Strike, 0}, {Stretch, 100}, {Hardness, 100}, {Position, 10}}));
        p->noteOn(key, 1.f);
        const double f = hzOf(key);
        const std::vector<float> v = play(*p, 48000 * 2);
        double last = 0.0;
        for (int n = 2; n <= 8; ++n) {
            const double want = f * stiffPartial(b, n);
            const double off = centsOff(v, want, 0.1, 60.0, 32.0);
            const double sharp = 1200.0 * std::log2(want / (n * f)) + off; // how sharp it is over a plain harmonic
            if (gPrint) std::printf("  STRING key %d B %.4f partial %d: %+.2f cents from F64 (%+.1f over the harmonic)\n", key, b, n, off, sharp);
            if (n == kDispersionPartial) CHECK(std::fabs(off) < 3.0);
            CHECK(sharp > last);
            last = sharp;
        }
    }
}

// A low string struck by a short mallet: for its first period its sound has not come round to where
// the loop is read, so 64 samples of silence do not mean it is asleep (its whole line is looked at).
static void testStringMallet() {
    auto p = synth(plain(kString, {{Strike, 1}, {Hardness, 100}, {Position, 0}}));
    p->noteOn(28, 0.9f); // 41 Hz: a period of 1168 samples
    const std::vector<float> first = play(*p, 960);
    CHECK(peakOf(first) == 0.f); // nothing has come round yet
    CHECK(p->awake() == 1);
    const std::vector<float> v = play(*p, 48000 * 3);
    CHECK(peakOf(v) > 0.05f);
    CHECK(std::fabs(centsOff(v, hzOf(28), 0.1)) < 0.5);
}

// SELF: a struck note never peaks over kModalGain (0.3) at velocity 1 and LEVEL 0: the modes' sizes
// add up to it, however they line up, whatever the model, key, mallet or place.
static void testPeakBound() {
    float most = 0.f;
    for (int model = 1; model < kModels; ++model)
        for (int key : {36, 60, 84, 100})
            for (int hardness : {0, 50, 100})
                for (int position : {0, 50, 100}) {
                    auto p = synth(plain(model, {{Hardness, hardness}, {Position, position}, {Velocity, 100}}));
                    p->noteOn(key, 1.f);
                    const float peak = peakOf(play(*p, 4800));
                    most = std::max(most, peak);
                    CHECK(peak <= 0.3f * 1.0001f);
                    if (position == 0 && key <= 60 && hardness >= 50) CHECK(peak > 0.1f); // and it is not quiet
                }
    if (gPrint) std::printf("  the loudest single note: %.4f (bound 0.3)\n", most);
}

// SELF: a ringing string struck again is damped 2 ms first (down to 30%), then plucked; a ringing bar
// struck again keeps its voice and the hit adds to the ringing.
static void testRestrike() {
    {
        auto p = synth(plain(kString, {{Strike, 0}, {Decay, 80}}));
        p->noteOn(57, 0.8f);
        const std::vector<float> before = play(*p, 9600);
        const double was = rmsOf(before, 9600 - 480);
        p->noteOff(57);
        p->noteSeed(99u);
        p->noteOn(57, 0.8f);
        const std::vector<float> damped = play(*p, 96);
        CHECK(p->strikes() == 2 && p->awake() == 1);
        CHECK(rmsOf(damped, 72, 96) < 0.55 * was); // (the last of the 2 ms: towards 30%)
        const std::vector<float> after = play(*p, 4800);
        CHECK(rmsOf(after, 960) > 0.5 * was);
        // The pluck after the damping is all there: as loud as a fresh one (with what is left of the
        // old under it).
        auto fresh = synth(plain(kString, {{Strike, 0}, {Decay, 80}}));
        fresh->noteSeed(99u); // (the same pluck)
        fresh->noteOn(57, 0.8f);
        const std::vector<float> f = play(*fresh, 96 + 4800);
        CHECK(rmsOf(after, 960, 4800) > 0.95 * rmsOf(f, 96 + 960, 96 + 4800));
        if (gPrint) std::printf("  re-struck string: %.4f, a fresh pluck %.4f, before %.4f\n", rmsOf(after, 960, 4800), rmsOf(f, 96 + 960, 96 + 4800), was);
    }
    {
        auto p = synth(plain(kMarimba, {{Decay, 70}}));
        auto fresh = synth(plain(kMarimba, {{Decay, 70}}));
        p->noteOn(60, 0.8f);
        play(*p, 4800);
        p->noteOn(60, 0.8f);
        fresh->noteOn(60, 0.8f);
        CHECK(p->awake() == 1);
        const std::vector<float> a = play(*p, 4800), b = play(*fresh, 4800);
        CHECK(p->awake() == 1 && p->strikes() == 2);
        bool differ = false;
        for (std::size_t i = 0; i < a.size(); ++i) differ = differ || a[i] != b[i];
        CHECK(differ); // the ringing goes on under the new hit
        CHECK(std::fabs(a[0]) > 0.f); // ...from the first sample (a fresh hit starts at 0)
        CHECK(b[0] == 0.f);
    }
}

// SELF: an octave glide does not click (worst second difference over its RMS at most 5; the plan's
// naive switch was 9.5), on a string and on a bar; the glide lands in tune.
static double secondDifference(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double worst = 0.0, e = 0.0;
    for (std::size_t i = from + 2; i < to; ++i) {
        const double d = std::fabs(static_cast<double>(v[i]) - 2.0 * v[i - 1] + v[i - 2]);
        worst = std::max(worst, d), e += d * d;
    }
    return worst / std::sqrt(e / static_cast<double>(to - from - 2)); // the worst over its own RMS
}
static void testGlideClicks() {
    for (int model : {kString, kMarimba}) {
        // (A yarn mallet on a dark string: a pluck's noise is all sharp corners, and would hide a click.)
        auto p = synth(plain(model, {{Strike, 1}, {Hardness, 0}, {Material, 0}}));
        p->noteOnFrom(45, 0.8f, 33, 0.5f); // up an octave over half a second
        const std::vector<float> v = play(*p, 48000 * 2);
        // The plain sound's own second difference, as the bar or the string rings at the top note.
        auto still = synth(plain(model, {{Strike, 1}, {Hardness, 0}, {Material, 0}}));
        still->noteOn(45, 0.8f);
        const std::vector<float> s = play(*still, 48000 * 2);
        const double gliding = secondDifference(v, 2400, 24000), at = secondDifference(s, 2400, 24000);
        if (gPrint) std::printf("  %s octave glide: worst second difference %.2f of its RMS (the note held: %.2f)\n", modelName(model), gliding, at);
        CHECK(gliding <= 5.0);
        CHECK(std::fabs(centsOff(v, hzOf(45), 0.7)) < 0.5);
    }
}

// SELF: a lone glide from a loud ringing voice takes it over (no new strike); a 4-key chord naming
// one `from`: all four strike; from a voice under -40 dB: a new strike; a taken-over note's level
// follows its velocity.
static void testTakeOver() {
    {
        auto p = synth(plain(kString, {{Strike, 0}}));
        p->noteOn(57, 0.8f);
        play(*p, 9600);
        p->noteOnFrom(64, 0.8f, 57, 0.2f);
        play(*p, 256);
        CHECK(p->strikes() == 1 && p->awake() == 1);
        if (p->awake() != 1) std::printf("  awake %d\n", p->awake());
        const std::vector<float> v = play(*p, 48000);
        CHECK(std::fabs(centsOff(v, hzOf(64), 0.3)) < 0.5);
    }
    {
        auto p = synth(plain(kMarimba));
        p->noteOn(57, 0.8f);
        play(*p, 9600);
        for (int key : {60, 64, 67, 72}) p->noteOnFrom(key, 0.8f, 57, 0.2f);
        play(*p, 256);
        CHECK(p->strikes() == 5 && p->awake() == 5);
    }
    {
        auto p = synth(plain(kMarimba, {{Decay, 10}})); // 0.09 s: under -40 dB in a tenth of a second, still awake
        p->noteOn(57, 0.8f);
        const std::vector<float> v = play(*p, 5760);
        CHECK(p->awake() == 1 && peakOf(std::vector<float>(v.end() - 64, v.end())) < 0.01f);
        p->noteOnFrom(60, 0.8f, 57, 0.2f);
        play(*p, 256);
        CHECK(p->strikes() == 2);
    }
    { // from a voice still inside its strike: a new strike
        auto p = synth(plain(kMarimba, {{Hardness, 0}}));
        p->noteOn(57, 0.8f);
        play(*p, 128);
        p->noteOnFrom(60, 0.8f, 57, 0.2f);
        play(*p, 256);
        CHECK(p->strikes() == 2);
    }
    {
        auto p = synth(plain(kVibes, {{Velocity, 100}}));
        p->noteOn(57, 1.f);
        const double loud = rmsOf(play(*p, 4800), 2400);
        p->noteOnFrom(60, 0.25f, 57, 0.01f);
        const std::vector<float> v = play(*p, 4800);
        CHECK(p->strikes() == 1);
        CHECK(rmsOf(v, 2400) < 0.35 * loud); // (a quarter of the level, the ring a little down)
    }
}

// SELF: 12 overlapping notes: the quietest goes, never a voice inside its strike; the step at a
// steal is no larger than the 3 ms fade allows.
static void testSteal() {
    const auto sounds = [](Physic& p, int key) {
        int keys[kSlots];
        const int n = p.keysSounding(keys);
        return std::find(keys, keys + n, key) != keys + n;
    };
    auto p = synth(plain(kMarimba, {{Decay, 70}, {Velocity, 100}, {Hardness, 0}}));
    std::vector<float> all;
    for (int n = 0; n < 12; ++n) {
        p->noteOn(40 + 2 * n, n == 3 ? 0.1f : 0.9f); // the fourth is quietest: it goes first
        const std::vector<float> v = play(*p, 2400);
        all.insert(all.end(), v.begin(), v.end());
        CHECK(p->awake() <= kSlots);
        if (n == 7) CHECK(sounds(*p, 46));
        if (n == 8) CHECK(!sounds(*p, 46) && sounds(*p, 40) && sounds(*p, 56)); // the quietest went, not the oldest
    }
    CHECK(p->strikes() == 12);
    // No step larger than a plain mode's, a few times over (a voice cut dead would be a jump).
    CHECK(largestJump(all) < 0.2f);
    // A voice inside its strike is never taken, though it reads quietest (its peak is still coming):
    // seven ringing, an eighth struck with a yarn mallet, a ninth at once.
    {
        auto r = synth(plain(kMarimba, {{Decay, 70}, {Hardness, 100}}));
        for (int n = 0; n < 7; ++n) r->noteOn(50 + n, 0.8f);
        play(*r, 4800);
        r->setSetting(Hardness, 0);
        r->noteOn(70, 0.8f);
        play(*r, 16);
        r->noteOn(72, 0.8f);
        play(*r, 16);
        CHECK(sounds(*r, 70) && sounds(*r, 72));
    }
    // ...unless all eight are: eight strikes at once, then a ninth.
    auto q = synth(plain(kMarimba, {{Hardness, 0}}));
    for (int n = 0; n < 8; ++n) q->noteOn(50 + n, 0.8f);
    play(*q, 16);
    q->noteOn(70, 0.8f);
    play(*q, 16);
    CHECK(q->awake() == 9); // one of the eight fading (all were inside their strike: the rule gives way)
}

// ---- the house rules ---------------------------------------------------------------------------------

// A phrase on one PHYSIC, events at fixed samples: glides, macros moved, a re-strike, a steal, a
// sleep and a waking, strings and bars.
struct Event {
    long at;
    std::function<void(Physic&)> act;
};
static std::vector<Event> phrase(int model) {
    std::vector<Event> e;
    e.push_back({0, [](Physic& p) { p.noteOn(60, 0.8f); }});
    e.push_back({1000, [](Physic& p) { p.noteOn(64, 0.6f); p.noteOn(67, 0.7f); }});
    e.push_back({4100, [](Physic& p) { p.setMacro(MacroDecay, 0.2f); p.setMacro(MacroMaterial, 0.8f); }});
    e.push_back({5003, [](Physic& p) { p.noteOff(64); p.noteOn(64, 0.9f); }}); // struck again
    e.push_back({7777, [](Physic& p) { p.noteOnFrom(72, 0.8f, 67, 0.1f); }});  // a take-over
    e.push_back({9000, [](Physic& p) { p.setMacro(MacroStretch, 0.9f); p.setSetting(Level, -6); }});
    e.push_back({11111, [](Physic& p) { p.noteOnFrom(48, 0.8f, 36, 0.3f); }});
    for (int n = 0; n < 9; ++n) e.push_back({13000 + 701 * n, [n](Physic& p) { p.noteOn(50 + 3 * n, 0.5f + 0.05f * n); }}); // steals
    e.push_back({22000, [](Physic& p) { p.noteSeed(12345u); p.noteOn(61, 0.8f); }});
    e.push_back({25000, [](Physic& p) { p.stopNotes(); }});
    e.push_back({30000, [](Physic& p) { p.allNotesOff(); }});
    e.push_back({30000 + 48000 * 4, [](Physic& p) { p.noteOn(62, 0.8f); }}); // after the sleep: woken
    e.push_back({30000 + 48000 * 4 + 3000, [](Physic& p) { p.setMacro(MacroTone, 0.1f); p.noteOn(66, 0.8f); }});
    (void)model;
    return e;
}
static std::uint64_t phraseSum(int model, int block, int preset = -1, std::vector<float>* out = nullptr, const std::vector<Event>& more = {}) {
    auto p = synth({}, preset);
    if (preset < 0) {
        p->setSetting(Model, model);
        p->setSetting(Strike, model == kString ? 0 : 1);
        p->setSetting(Decay, 50);
        p->setSetting(Release, 50);
        p->setSetting(Dice, 60);
        p->setSetting(DiceTune, 50);
        p->setSetting(DicePosition, 50);
    }
    std::vector<Event> events = phrase(model);
    events.insert(events.end(), more.begin(), more.end());
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    const long total = 30000 + 48000 * 5;
    std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
    long at = 0;
    std::size_t next = 0;
    while (at < total) {
        while (next < events.size() && events[next].at == at) events[next++].act(*p);
        long n = block;
        if (next < events.size() && events[next].at - at < n) n = events[next].at - at;
        if (total - at < n) n = total - at;
        p->render(l.data() + at, r.data() + at, static_cast<int>(n));
        at += n;
    }
    Sum s;
    s.add(l), s.add(r);
    if (out) *out = l;
    return s.h;
}
// The 24 version 1 presets, by name: the phrase's checksum of each (its re-strikes, take-overs,
// steals, macros, Stop, sleep and waking), taken from the code BEFORE batch 10.
static const char* const kVersionOne[24] = {"Marimba", "Soft Marimba", "Xylophone", "Vibes Still", "Glockenspiel", "Steel", "Harp", "Koto",
                                            "Pizz", "Plucked Bass", "Music Box", "Church Bell", "Handbell", "Tubular Bell", "Wine Glass", "Crystal",
                                            "Woodblock", "Temple Blocks", "Log Drum", "Tom", "Rain", "Wind Chime", "Loose Strings", "Drunk Marimba"};
static int presetNamed(const char* name) {
    for (int i = 0; i < Physic::presetCount(); ++i)
        if (std::strcmp(Physic::presetName(i), name) == 0) return i;
    return -1;
}
static void printVersionOneSums() {
    for (const char* name : kVersionOne) std::printf("    0x%016llxull, // %s\n", static_cast<unsigned long long>(phraseSum(0, 64, presetNamed(name))), name);
}

// Batch 10's settings turned while the phrase plays: MOTOR on and off, a BODY and its MIX, BUZZ, a
// NOISE strike, PAIR for the strikes after.
static std::vector<Event> batchTenTurned() {
    std::vector<Event> e;
    e.push_back({6000, [](Physic& p) { p.setSetting(Motor, 60); }});
    e.push_back({8500, [](Physic& p) { p.setSetting(Pair, 9); }});
    e.push_back({15000, [](Physic& p) { p.setSetting(Body, kBodyGuitar); p.setSetting(BodyMix, 80); }});
    e.push_back({18000, [](Physic& p) { p.setSetting(Body, kBodyBox); }});
    e.push_back({20000, [](Physic& p) { p.setSetting(Buzz, 50); p.setSetting(Motor, 0); }});
    e.push_back({23000, [](Physic& p) { p.setSetting(Strike, kNoise); p.setSetting(Hardness, 40); p.noteOn(57, 0.9f); }});
    e.push_back({24000, [](Physic& p) { p.setSetting(Body, kBodyTubes); }});
    return e;
}
// The same to the bit in blocks of 1, 7, 17, 48, 64, 256 and 333, for each core; and so with batch
// 10's settings turned as it plays, and for each preset that uses them.
static void testBlocks() {
    for (int model : {kMarimba, kString, kBell, kDrum}) {
        const std::uint64_t first = phraseSum(model, 64);
        for (int block : {1, 7, 17, 48, 256, 333}) CHECK(phraseSum(model, block) == first);
        const std::uint64_t turned = phraseSum(model, 64, -1, nullptr, batchTenTurned());
        CHECK(turned != first);
        for (int block : {1, 7, 17, 48, 256, 333}) CHECK(phraseSum(model, block, -1, nullptr, batchTenTurned()) == turned);
    }
    for (const char* name : {"Vibes", "Twelve", "Kalimba", "Hand Drum", "Dulcimer"}) {
        const int at = presetNamed(name);
        const std::uint64_t first = phraseSum(0, 64, at);
        for (int block : {1, 17, 333}) {
            const bool same = phraseSum(0, block, at) == first;
            if (!same) {
                std::vector<float> a, b;
                phraseSum(0, 64, at, &a), phraseSum(0, block, at, &b);
                std::size_t i = 0;
                while (i < a.size() && a[i] == b[i]) ++i;
                std::printf("  %s in blocks of %d: not the same from sample %zu (%g, %g)\n", name, block, i, i < a.size() ? a[i] : 0.f, i < a.size() ? b[i] : 0.f);
            }
            CHECK(same);
        }
    }
}

// Flush-to-zero OFF (as the tests leave it): no number too small to hold in the output or any state,
// scanned on every tick, over a 60 s tail of every model, at MATERIAL 0 on a high key (high modes die
// first while the voice is still loud) and at METAL. Timed beside the same run with it on: at most 3
// times as long.
static double tailRun(int model, int material, int key, bool flush, bool scan, int stretch = 50, const std::vector<Set>& more = {}) {
    FlushDenormals ftz(flush);
    std::vector<Set> sets = {{Material, material}, {Decay, 70}, {Strike, model == kString ? 0 : 1}, {Release, 100}, {Stretch, stretch}};
    sets.insert(sets.end(), more.begin(), more.end());
    auto p = synth(plain(model, sets));
    for (int k = 0; k < 4; ++k) p->noteOn(key + 3 * k, 0.9f);
    float l[16], r[16];
    const auto t0 = std::chrono::steady_clock::now();
    for (long b = 0; b < 48000L * 60 / 16; ++b) {
        p->render(l, r, 16);
        if (scan) {
            for (int i = 0; i < 16; ++i) {
                CHECK(l[i] == 0.f || std::fabs(l[i]) >= FLT_MIN);
                if (!(l[i] == 0.f || std::fabs(l[i]) >= FLT_MIN)) return 0.0;
            }
            const float least = p->smallestState(b % 64 == 0); // (the lines, the slow part, every 64th tick)
            if (least != 0.f && least < FLT_MIN) {
                std::printf("  %s MATERIAL %d key %d: a state of %g at %.3f s\n", modelName(model), material, key, least, b * 16 / kRate);
                CHECK(least == 0.f || least >= FLT_MIN);
                return 0.0;
            }
        }
    }
    if (p->awake() != 0) std::printf("  %s MATERIAL %d key %d: %d awake after 60 s\n", modelName(model), material, key, p->awake());
    CHECK(p->awake() == 0);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
static void testTooSmall() {
    for (int model = 0; model < kModels; ++model)
        for (int material : {0, 100}) tailRun(model, material, model == kString ? 96 : 100, false, true);
    for (int material : {0, 100}) tailRun(kString, material, 96, false, true, 0); // (a string with no stiffness takes the other loop)
    // Batch 10: PAIR's second string, a BODY's resonances, MOTOR, BUZZ, a NOISE strike, TUBES.
    const std::vector<Set> all = {{Pair, 15}, {Motor, 50}, {Body, kBodyGuitar}, {BodyMix, 100}, {Buzz, 60}};
    for (int material : {0, 100}) {
        tailRun(kString, material, 96, false, true, 50, all);
        tailRun(kString, material, 40, false, true, 0, all);
        tailRun(kBell, material, 100, false, true, 50, all);
        tailRun(kMarimba, material, 40, false, true, 50, {{Body, kBodyTubes}, {BodyMix, 100}, {Strike, kNoise}, {Hardness, 30}});
        tailRun(kString, material, 60, false, true, 50, {{Body, kBodyTubes}, {BodyMix, 100}, {Strike, kNoise}, {Hardness, 30}, {Pair, 5}});
    }
    // Timed: the worst case without the scan, off and on.
    for (int model : {kGlock, kString}) {
        double off = 1e9, on = 1e9;
        for (int run = 0; run < 3; ++run) {
            off = std::min(off, tailRun(model, 0, model == kString ? 96 : 100, false, false));
            on = std::min(on, tailRun(model, 0, model == kString ? 96 : 100, true, false));
        }
        if (gPrint) std::printf("  %s 60 s tail at WOOD: %.1f ms with flush-to-zero off, %.1f ms on\n", modelName(model), off * 1e3, on * 1e3);
        CHECK(off < 3.0 * on + 0.002);
    }
}

// NaN or infinite velocity, `from`, glide seconds or macro: ignored or held; the sound stays finite.
static void testNaN() {
    auto p = synth();
    const float nan = std::nanf(""), inf = INFINITY;
    p->noteOn(60, nan);
    p->noteOn(61, inf);
    p->noteOn(62, -inf);
    p->noteOnFrom(63, 0.8f, 40, nan);
    p->noteOnFrom(64, 0.8f, 40, inf);
    p->noteOnFrom(65, 0.8f, -5, 0.1f);
    p->noteOnFrom(66, 0.8f, 400, 0.1f);
    p->noteOn(-3, 0.8f);
    p->noteOn(200, 0.8f);
    p->setMacro(MacroDecay, nan);
    p->setMacro(MacroStretch, inf);
    p->setMacro(MacroOctave, -inf);
    p->setSetting(Model, 99);
    p->setSetting(Decay, -1000);
    const std::vector<float> v = play(*p, 48000);
    bool finite = true;
    for (float s : v) finite = finite && std::isfinite(s);
    CHECK(finite);
    CHECK(peakOf(v) > 0.f && peakOf(v) < 2.f);
}

// No notes: exact zeros out, and no work.
static void testSilence() {
    auto p = synth();
    const std::vector<float> v = play(*p, 48000);
    for (float s : v) CHECK(s == 0.f);
    CHECK(p->ticksWorked() == 0);
    // A struck note, asleep: zeros again, and no more work.
    p->noteOn(60, 0.8f);
    play(*p, 48000 * 12);
    CHECK(p->awake() == 0);
    const long worked = p->ticksWorked();
    const std::vector<float> w = play(*p, 48000);
    for (float s : w) CHECK(s == 0.f);
    CHECK(p->ticksWorked() == worked);
    CHECK(p->smallestState() == 0.f);
}

// Nothing after prepare allocates: note-on, steal, glide, take-over, macros, Stop, allNotesOff; batch
// 10's settings on too, and turned.
static void testNoAllocation() {
    for (int model : {int(kMarimba), int(kString), int(kBell), -1}) {
        const bool all = model < 0;
        if (all) model = kString;
        auto p = synth(plain(model, {{Strike, model == kString ? 0 : 1}, {Dice, 100}}));
        if (all)
            for (const Set& set : std::vector<Set>{{Pair, 12}, {Motor, 70}, {Body, kBodyGuitar}, {BodyMix, 60}, {Buzz, 40}, {Strike, kNoise}}) p->setSetting(set.id, set.value);
        std::vector<float> l(256), r(256);
        const long before = gNews;
        for (int n = 0; n < 12; ++n) {
            p->noteSeed(static_cast<std::uint32_t>(n * 77));
            p->noteOn(48 + n, 0.8f);
            p->render(l.data(), r.data(), 256);
        }
        p->noteOnFrom(80, 0.8f, 59, 0.2f);
        p->noteOnFrom(30, 0.8f, 20, 0.2f);
        p->setMacro(MacroStretch, 0.7f);
        p->setMacro(MacroDecay, 0.2f);
        p->setSetting(Material, 10);
        p->setSetting(Body, kBodyBox);
        p->setSetting(Pair, 3);
        p->noteOn(70, 0.9f);
        for (int b = 0; b < 50; ++b) p->render(l.data(), r.data(), 256);
        p->stopNotes();
        p->render(l.data(), r.data(), 256);
        p->allNotesOff();
        for (int b = 0; b < 400; ++b) p->render(l.data(), r.data(), 256);
        CHECK(gNews == before);
    }
}

// The plain controls: WIDTH (low keys left, high right, equal power), LEVEL (dB, sliding), VELOCITY
// (the level (1 - V) + V v), RELEASE reached over 5 ms, the dice's TUNE heard in the pitch.
static void testPlainControls() {
    { // WIDTH 100: key 36 well left, key 84 well right, key 60 in the middle; WIDTH 0 both sides alike
        for (int key : {36, 60, 84}) {
            auto p = synth(plain(kVibes, {{Width, 100}}));
            p->noteOn(key, 0.8f);
            std::vector<float> r;
            const std::vector<float> l = play(*p, 4800, &r);
            const double side = dB(rmsOf(r) / rmsOf(l));
            if (key == 36) CHECK(side < -10.0); // (pan -2/3: cos and sin of a twelfth of a turn, -11.4 dB)
            if (key == 60) CHECK(std::fabs(side) < 1e-6);
            if (key == 84) CHECK(side > 10.0);
        }
        auto p = synth(plain(kVibes, {{Width, 0}}));
        p->noteOn(84, 0.8f);
        std::vector<float> r;
        const std::vector<float> l = play(*p, 4800, &r);
        CHECK(l == r);
    }
    { // LEVEL -6: half, after a slide of 5 ms
        auto a = synth(plain(kVibes)), b = synth(plain(kVibes));
        a->noteOn(60, 0.8f), b->noteOn(60, 0.8f);
        play(*a, 480), play(*b, 480);
        b->setSetting(Level, -6);
        const std::vector<float> x = play(*a, 4800), y = play(*b, 4800);
        CHECK(std::fabs(rmsOf(y, 480) / rmsOf(x, 480) - std::pow(10.0, -6.0 / 20.0)) < 1e-4);
        CHECK(rmsOf(y, 0, 24) / rmsOf(x, 0, 24) > 0.9); // (slid, not stepped: half a millisecond in, still near where it was)
    }
    { // VELOCITY 100: a quarter of the level at velocity 0.25; VELOCITY 0: the same
        for (int amount : {0, 100}) {
            auto a = synth(plain(kMarimba, {{Velocity, amount}, {Hardness, 100}})), b = synth(plain(kMarimba, {{Velocity, amount}, {Hardness, 100}}));
            a->noteOn(36, 1.f), b->noteOn(36, 0.25f);
            const std::vector<float> x = play(*a, 9600), y = play(*b, 9600);
            const double ratio = sizeAt(y, hzOf(36), 0.05) / sizeAt(x, hzOf(36), 0.05);
            CHECK(std::fabs(ratio - (amount ? 0.25 : 1.0)) < 0.02);
        }
    }
    { // RELEASE 0 (10 ms): reached over 5 ms, so 5 ms after let go it is down about 15 dB, not 30
        auto p = synth(plain(kVibes, {{Release, 0}}));
        p->noteOn(60, 0.8f);
        const std::vector<float> before = play(*p, 4800);
        p->noteOff(60);
        const std::vector<float> after = play(*p, 480);
        const double down = dB(rmsOf(after, 192, 288) / rmsOf(before, 4800 - 96));
        CHECK(down < -8.0 && down > -22.0);
    }
    { // a note let go before it has sounded (both before render()): struck, then let go
        auto p = synth(plain(kVibes, {{Release, 0}}));
        p->noteOn(60, 0.8f);
        p->noteOff(60);
        play(*p, 4800);
        CHECK(p->strikes() == 1 && p->awake() == 0);
    }
    { // DICE TUNE: the note sounds where the roll put it; its row at 0, it does not move
        std::uint32_t seed = 1;
        while (std::fabs(diceRoll(seed, kRollsTune)) < 0.3) ++seed; // (a roll of more than 4.5 cents)
        auto p = synth(plain(kVibes, {{Dice, 100}, {DiceTune, 100}, {DicePosition, 100}}));
        p->noteSeed(seed);
        p->noteOn(60, 0.8f);
        const std::vector<float> v = play(*p, 48000);
        const double cents = p->lastRolled().tuneCents;
        CHECK(std::fabs(cents) > 2.0);
        CHECK(std::fabs(centsOff(v, hzOf(60), 0.05) - cents) < 0.5);
        auto q = synth(plain(kVibes, {{Dice, 100}, {DiceTune, 0}, {DicePosition, 100}}));
        q->noteSeed(seed);
        q->noteOn(60, 0.8f);
        play(*q, 16);
        CHECK(q->lastRolled().tuneCents == 0.0 && q->lastRolled().position != 40.0);
    }
}

// ---- the presets ---------------------------------------------------------------------------------------

// SELF: every preset makes sound, peaks under 1 (a chord of four at full velocity), rings about as
// its DECAY says, and sleeps after its tail. Preset 0 is MARIMBA.
static void testEveryPreset() {
    CHECK(Physic::presetCount() == 32);
    // A folder is the presets side by side under one name: no name comes back after another.
    for (int i = 1; i < Physic::presetCount(); ++i)
        if (std::strcmp(Physic::presetCategory(i), Physic::presetCategory(i - 1)) != 0)
            for (int j = 0; j < i - 1; ++j) CHECK(std::strcmp(Physic::presetCategory(j), Physic::presetCategory(i)) != 0);
    // The 8 of batch 10 use what they were made for (the plan's section 5).
    const auto has = [](const char* name, int id, int value) { const int at = presetNamed(name); return at >= 0 && Physic::presetPatch(at).value[id] == value; };
    CHECK(has("Vibes", Body, kBodyTubes) && !has("Vibes", Motor, 0) && has("Celesta", Body, kBodyBox) && has("Celesta", Model, kGlock));
    CHECK(has("Nylon", Body, kBodyGuitar) && has("Dulcimer", Strike, kMallet) && has("Dulcimer", Pair, 5) && has("Twelve", Pair, 7));
    CHECK(has("Plucked Bass Box", Body, kBodyBox) && has("Kalimba", Body, kBodyBox) && !has("Kalimba", Buzz, 0) && has("Hand Drum", Strike, kNoise));
    CHECK(std::strcmp(Physic::presetCategory(presetNamed("Hand Drum")), "WOOD AND SKIN") == 0 && std::strcmp(Physic::presetCategory(presetNamed("Kalimba")), "TINES") == 0);
    CHECK(Physic::presetPatch(0).value[Model] == kMarimba);
    CHECK(std::strcmp(Physic::presetName(0), "Marimba") == 0);
    for (int i = 0; i < Physic::presetCount(); ++i) {
        const PhysicPatch patch = Physic::presetPatch(i);
        for (int id = 0; id < kSettings; ++id) CHECK(patch.value[id] >= settingMin(id) && patch.value[id] <= settingMax(id));
        for (int id = kSettings; id < kSettingRoom; ++id) CHECK(patch.value[id] == 0); // (the room after BUZZ, at 0)
        auto p = synth({}, i);
        for (int key : {48, 55, 60, 64}) p->noteOn(key, 1.f);
        std::vector<float> right;
        const std::vector<float> v = play(*p, 48000 * 2, &right);
        const float peak = std::max(peakOf(v), peakOf(right));
        if (gPrint) std::printf("  preset %2d %-14s %-13s peak %.3f, the first second %5.1f dB\n", i, Physic::presetName(i), Physic::presetCategory(i), peak, dB(rmsOf(v, 0, 48000)));
        CHECK(peak > 0.05f && peak < 1.f);
        // Asleep within twice DECAY's longest, and some.
        const double longest = decaySeconds(patch.value[Decay] * 1.3 > 100 ? 100 : patch.value[Decay] * 1.3);
        play(*p, static_cast<std::size_t>((2.0 * longest + 1.0) * kRate));
        CHECK(p->awake() == 0);
    }
}

// ---- through the engine ---------------------------------------------------------------------------

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    void send(const Command& c) { CHECK(engine->send(c)); }
    void note(int slot, int step, int pitch, float length, int ratchet = 0, int glide = 0, int track = 1, float velocity = 0.8f, int throwTo = 0) {
        Command c = make(Command::SetNote, track, slot, step, velocity);
        c.c = pitch;
        c.d = static_cast<int>(length);
        c.extra = length - static_cast<float>(c.d);
        c.e = (ratchet >= 2 ? ratchet : 0) + (glide ? 256 + (glide >= 2 ? glide << 11 : 0) : 0) + (throwTo << 9);
        send(c);
    }
    void chord(int slot, int step, const std::vector<int>& keys, float length, ChordPlay play, int ratchet = 0, int track = 1) {
        for (std::size_t k = 0; k < keys.size(); ++k) {
            Command c = make(Command::SetNote, track, slot + static_cast<int>(k), step, 0.8f);
            c.c = keys[k];
            c.d = static_cast<int>(length);
            c.extra = length - static_cast<float>(c.d);
            c.e = ratchet >= 2 ? ratchet : 0;
            c.play = play;
            c.play.index = static_cast<std::uint8_t>(k);
            c.play.count = static_cast<std::uint8_t>(keys.size());
            send(c);
        }
    }
    std::vector<float> run(long frames, int block = 256) {
        std::vector<float> all;
        all.reserve(static_cast<std::size_t>(2 * frames));
        std::vector<float> out(static_cast<std::size_t>(2 * block));
        for (long left = frames; left > 0; left -= block) {
            const int n = left < block ? static_cast<int>(left) : block;
            engine->render(out.data(), n);
            all.insert(all.end(), out.begin(), out.begin() + 2 * n);
        }
        return all;
    }
};

// A listening instrument: the seeds the engine hands it, with the key each was for.
struct SeedSpy : Instrument {
    std::vector<std::pair<int, std::uint32_t>>* log;
    std::uint32_t seed = 0;
    bool seeded = false;
    explicit SeedSpy(std::vector<std::pair<int, std::uint32_t>>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteSeed(std::uint32_t s) override { seed = s, seeded = true; }
    void noteOn(int note, float) override {
        log->push_back({note, seeded ? seed : 0u});
        seeded = false;
    }
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    }
};

// Every seed `noteSeed` hands over, through the engine: each piece of a ratchet its own, each piece of
// a chord's ratchet its own, an arpeggio's notes on one key new each time round, a strum's keys their
// own, and Play twice the same seeds.
static std::vector<std::pair<int, std::uint32_t>> seedsOf(const std::function<void(Rig&)>& write, long frames) {
    std::vector<std::pair<int, std::uint32_t>> log;
    log.reserve(10000);
    Rig rig;
    Command c = make(Command::InstallSynth, 1);
    c.pointer = new SeedSpy(&log);
    rig.send(c);
    write(rig);
    rig.send(make(Command::Play, 0, 0, 1));
    rig.run(frames);
    return log;
}
static void testSeeds() {
    { // a ratchet of 4 on one note
        const auto log = seedsOf([](Rig& r) { r.note(0, 0, 60, 1.f, 4); }, 12000);
        CHECK(log.size() >= 4);
        for (std::size_t i = 0; i < log.size(); ++i) CHECK(log[i].second != 0u);
        for (std::size_t i = 1; i < 4 && i < log.size(); ++i) CHECK(log[i].second != log[i - 1].second);
        for (std::size_t i = 0; i < 4 && i < log.size(); ++i) CHECK(log[i].second == strikeSeed(1, 60, static_cast<double>(static_cast<float>(i) * 0.25f)));
    }
    { // a chord's ratchet: two keys, three pieces each, STRUM so the chord takes the effects' path
        const auto log = seedsOf([](Rig& r) { r.chord(0, 0, {60, 64}, 1.f, chordPlayOf(kStrumTight, 0, 0, 0, 0), 3); }, 12000);
        std::vector<std::uint32_t> of60;
        for (const auto& e : log)
            if (e.first == 60) of60.push_back(e.second);
        CHECK(of60.size() == 3);
        for (std::size_t i = 1; i < of60.size(); ++i) CHECK(of60[i] != of60[i - 1]);
        std::vector<std::uint32_t> all;
        for (const auto& e : log) all.push_back(e.second);
        std::sort(all.begin(), all.end());
        CHECK(std::unique(all.begin(), all.end()) == all.end()); // every strike its own
    }
    { // an arpeggio of one key (a chord of one, at a quarter step), round the loop twice: new each time
        const auto log = seedsOf([](Rig& r) { r.chord(0, 0, {60, 67}, 16.f, chordPlayOf(kStrumQuarter, 0, 0, 0, 0)); }, 48000 * 4 + 2000);
        std::vector<std::uint32_t> of60;
        for (const auto& e : log)
            if (e.first == 60) of60.push_back(e.second);
        CHECK(of60.size() >= 60);
        std::vector<std::uint32_t> sorted = of60;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());
    }
    { // a plain note each bar: Play twice is the same seeds; round the loop, new ones
        const auto write = [](Rig& r) { r.note(0, 0, 60, 1.f); r.note(1, 8, 62, 1.f); };
        const auto a = seedsOf(write, 48000 * 4 + 100), b = seedsOf(write, 48000 * 4 + 100);
        CHECK(a == b);
        CHECK(a.size() >= 4 && a[0].first == 60 && a[2].first == 60 && a[0].second != a[2].second && a[0].second == strikeSeed(1, 60, 0.0) && a[2].second == strikeSeed(1, 60, 16.0));
    }
}

// SELF: the same through PHYSIC's sound: two seeds, two rolls; one seed, the same roll.
static void testRollsDiffer() {
    auto p = synth(plain(kMarimba, {{Dice, 100}, {DicePosition, 100}, {DiceHardness, 100}, {DiceTune, 100}, {DiceDecay, 100}}));
    p->noteSeed(1u);
    p->noteOn(60, 0.8f);
    play(*p, 16);
    const Physic::Rolled a = p->lastRolled();
    p->noteSeed(2u);
    p->noteOn(62, 0.8f);
    play(*p, 16);
    const Physic::Rolled b = p->lastRolled();
    CHECK(a.position != b.position && a.tuneCents != b.tuneCents && a.hardness != b.hardness && a.decayScale != b.decayScale);
    CHECK(std::fabs(a.tuneCents) <= kDiceTuneCents && std::fabs(a.decayScale - 1.0) <= kDiceDecayMost);
    p->noteSeed(1u);
    p->noteOn(64, 0.8f);
    play(*p, 16);
    const Physic::Rolled c = p->lastRolled();
    CHECK(c.position == a.position && c.tuneCents == a.tuneCents);
    // A live note (no seed handed over: MIDI, a key heard) rolls by a count of live notes, which
    // allNotesOff puts back: not repeatable in play, but the same after a panic.
    {
        auto live = synth(plain(kMarimba, {{Dice, 100}, {DiceTune, 100}}));
        live->noteOn(60, 0.8f);
        play(*live, 16);
        const std::uint32_t first = live->lastSeed();
        live->noteOn(62, 0.8f);
        play(*live, 16);
        CHECK(live->lastSeed() != first);
        live->allNotesOff();
        live->noteOn(60, 0.8f);
        play(*live, 16);
        CHECK(live->lastSeed() == first);
    }
    // DICE 0: nothing moves.
    auto q = synth(plain(kMarimba, {{Dice, 0}, {DicePosition, 100}, {DiceTune, 100}}));
    q->noteSeed(7u);
    q->noteOn(60, 0.8f);
    play(*q, 16);
    CHECK(q->lastRolled().tuneCents == 0.0 && q->lastRolled().position == 40.0);
    // Through the sound: two strikes of one key with two seeds sound apart.
    auto s1 = synth(plain(kMarimba, {{Dice, 100}, {DiceTune, 100}}));
    auto s2 = synth(plain(kMarimba, {{Dice, 100}, {DiceTune, 100}}));
    s1->noteSeed(11u), s2->noteSeed(12u);
    s1->noteOn(60, 0.8f), s2->noteOn(60, 0.8f);
    Sum x, y;
    x.add(play(*s1, 4800)), y.add(play(*s2, 4800));
    CHECK(x.h != y.h);
}

// A song on PHYSIC tracks: a marimba line with a ratchet and a glide, a strummed chord, a string.
static void physicSong(Rig& rig) {
    rig.send(make(Command::Panic, 0, 0)); // (nothing: the engine starts quiet)
    CHECK(rig.engine->addSynthTrack(1, SynthKind::Physic));
    CHECK(rig.engine->addSynthTrack(2, SynthKind::Physic));
    rig.send(make(Command::SelectPreset, 2, presetNamed("Steel")));
    rig.send(make(Command::SetSetting, 1, Dice, 100));
    rig.note(0, 0, 60, 1.f);
    rig.note(1, 2, 64, 0.5f, 3);
    rig.note(2, 4, 67, 1.f, 0, 1);
    rig.note(3, 8, 72, 2.f, 0, 5);
    rig.chord(4, 12, {48, 55, 60, 64}, 4.f, chordPlayOf(kStrumWide, 0, 50, 50, 0));
    rig.note(0, 0, 45, 4.f, 0, 0, 2);
    rig.note(1, 6, 52, 2.f, 0, 1, 2);
    rig.note(2, 10, 57, 2.f, 2, 0, 2, 0.8f, 1);
}

// Play, Stop, wait until asleep, Play: the same bits. Two fresh engines the same; on 0 and 3 helpers.
// Stop lets the RING notes ring on (a bounce's tail keeps them); Play damps them.
static void testPlayFromTheTop() {
    std::vector<float> first, second, other, helped;
    {
        Rig rig;
        physicSong(rig);
        rig.send(make(Command::Play, 0, 0, 1));
        first = rig.run(48000 * 3);
        rig.send(make(Command::Stop));
        rig.run(48000 * 25);
        rig.send(make(Command::Play, 0, 0, 1));
        second = rig.run(48000 * 3);
        // Play inside a ring damps it in 30 ms.
        rig.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> cut = rig.run(4800 * 2);
        (void)cut;
    }
    CHECK(first == second);
    {
        Rig rig;
        physicSong(rig);
        rig.send(make(Command::Play, 0, 0, 1));
        other = rig.run(48000 * 3, 333);
    }
    CHECK(first == other);
    {
        Rig rig;
        rig.engine->useWorkers(3);
        physicSong(rig);
        rig.send(make(Command::Play, 0, 0, 1));
        helped = rig.run(48000 * 3);
    }
    CHECK(first == helped);
    // Stop with RING (MARIMBA, preset 0): it rings on (a bounce's tail keeps it); damped, as Play
    // damps, it would be 60 dB down in 35 ms.
    {
        Rig rig;
        CHECK(rig.engine->addSynthTrack(1, SynthKind::Physic));
        rig.note(0, 0, 60, 16.f);
        rig.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> before = rig.run(9600);
        rig.send(make(Command::Stop));
        const std::vector<float> after = rig.run(9600);
        CHECK(rmsOf(after, 2 * 7200, 2 * 9600) > 0.3 * rmsOf(before, 2 * 7200, 2 * 9600));
    }
    // Stop with RELEASE: damped; panic and Play: damped in 30 ms.
    {
        Rig rig;
        CHECK(rig.engine->addSynthTrack(1, SynthKind::Physic));
        rig.send(make(Command::SetSetting, 1, Release, 20));
        rig.send(make(Command::SetSetting, 1, Decay, 90));
        rig.note(0, 0, 60, 16.f);
        rig.send(make(Command::Play, 0, 0, 1));
        rig.run(9600);
        rig.send(make(Command::Stop));
        const std::vector<float> v = rig.run(48000);
        const double t60 = releaseSeconds(20);
        CHECK(rmsOf(v, 2 * static_cast<std::size_t>(t60 * kRate * 1.5), 2 * static_cast<std::size_t>(t60 * kRate * 1.5) + 960) < rmsOf(v, 0, 960) * 0.01);
    }
    {
        Rig rig;
        CHECK(rig.engine->addSynthTrack(1, SynthKind::Physic));
        rig.send(make(Command::SetSetting, 1, Decay, 90));
        rig.note(0, 0, 60, 16.f);
        rig.send(make(Command::Play, 0, 0, 1));
        rig.run(9600);
        rig.send(make(Command::Play, 0, 0, 1)); // again, from the top: the note before is damped
        rig.send(make(Command::SetNote, 1, 0, 0, 0.f));
        const std::vector<float> v = rig.run(4800);
        CHECK(rmsOf(v, 2 * 3360, 2 * 4800) < rmsOf(v, 0, 2 * 240) * 1e-3); // 70 ms on: more than 60 dB down
    }
}

// PHYSIC's own song to its own checksum (taken with --print on 2026-11-15, after the FAITHFUL tests
// passed; remade only from code known right). The handheld's ARM gives the same bits (qemu, the
// handheld build's flags): no multiply and add is joined (physic.cpp's pragma; without it the ARM
// sum was 0x3eb9e06d6e428c3e).
static const std::uint64_t kSongSum = 0x45a36ca1ca8d0d2aull;
static void testPhysicSong() {
    Rig rig;
    physicSong(rig);
    rig.send(make(Command::Play, 0, 0, 1));
    const std::vector<float> v = rig.run(48000 * 8);
    Sum s;
    s.add(v);
    if (gPrint) std::printf("  PHYSIC's song: checksum 0x%016llxull, peak %.3f, RMS %.1f dB\n", static_cast<unsigned long long>(s.h), peakOf(v), dB(rmsOf(v)));
    CHECK(peakOf(v) > 0.05f && peakOf(v) < 1.f);
    if (kSongSum != 0ull) CHECK(s.h == kSongSum);
}

// ---- batch 10: PAIR, MOTOR, BODY, NOISE, BUZZ (the plan's sections 2 and 3) ---------------------------

// HOUSE RULE: the 24 version 1 presets play as they did before batch 10, to the bit (each sets every
// batch 10 setting to 0), through the phrase's re-strikes, take-overs, steals, macros, Stop and sleep.
// The sums were taken from the code before batch 10 (6de54c0).
static const std::uint64_t kVersionOneSums[24] = {
    0x0e31d67b2a10d378ull, 0x95e21dd6fb82913full, 0x1c1300b1b1caa6ccull, 0xb781146c8397ec85ull, 0x4d17bb8e0250dc2cull, 0xed6ac47c5eafefcfull,
    0xb8eebd7827e194f3ull, 0xfefc57828075784full, 0x965ecb8a9759e3ebull, 0x9d1efb4cb6a6a7dfull, 0x2a2640fb1120a62bull, 0x3a6d70a1914c3e47ull,
    0x9fb6707807306987ull, 0xed1cb767cf5cb75bull, 0x0a114313bc871783ull, 0x03d765212034f973ull, 0xc0bf6c943939dbe3ull, 0x5352b3f5a9224fa7ull,
    0x88f64d6e00a418dfull, 0xf3a587a8c3eee0f3ull, 0xc353a4750958bc21ull, 0x5bb44a33514e33baull, 0xedae162bc8f70754ull, 0xb46a320ab98e8badull};
static void testVersionOne() {
    for (int i = 0; i < 24; ++i) {
        const int at = presetNamed(kVersionOne[i]);
        CHECK(at >= 0);
        if (at < 0) continue;
        const PhysicPatch patch = Physic::presetPatch(at);
        for (int id = Pair; id < kSettings; ++id) CHECK(patch.value[id] == 0);
        const std::uint64_t sum = phraseSum(0, 64, at);
        if (sum != kVersionOneSums[i]) std::printf("  %s: 0x%016llx, was 0x%016llx\n", kVersionOne[i], static_cast<unsigned long long>(sum), static_cast<unsigned long long>(kVersionOneSums[i]));
        CHECK(sum == kVersionOneSums[i]);
    }
}

// How fast the part near `hz` beats: its size read every 2 ms through a window of 8 of its cycles,
// the times of its nulls (the quietest, under a fifth of its loudest) found to a part of a step, and
// how many of them come a second.
static double beatHzOf(const std::vector<float>& v, double hz, double fromSeconds, double toSeconds) {
    const std::size_t width = static_cast<std::size_t>(8.0 * kRate / hz), step = 96;
    std::vector<double> size, when;
    for (std::size_t at = static_cast<std::size_t>(fromSeconds * kRate); at + width < v.size() && at < toSeconds * kRate; at += step) {
        size.push_back(partNear(v.data() + at, width, hz));
        when.push_back((at + width / 2.0) / kRate);
    }
    double most = 0.0;
    for (double s : size) most = std::max(most, s);
    std::vector<double> nulls;
    for (std::size_t i = 1; i + 1 < size.size(); ++i)
        if (size[i] < size[i - 1] && size[i] <= size[i + 1] && size[i] < 0.2 * most) {
            // (the null is a V: where the two sides' lines meet)
            const double left = size[i - 1] - size[i], right = size[i + 1] - size[i];
            const double shift = 0.5 * (right - left) / std::max(left, right);
            nulls.push_back(when[i] - shift * (step / kRate));
        }
    if (gPrint) std::printf("    %.1f Hz: %zu nulls\n", hz, nulls.size());
    return nulls.size() >= 2 ? (nulls.size() - 1) / (nulls.back() - nulls.front()) : 0.0;
}

// FAITHFUL (two sines PAIR's cents apart beat at the difference of their frequencies): a modal
// model's lowest two modes each split into a pair, half the cents either side; a string doubled,
// the same. The beat in Hz from its cents (pairBeatHz). The pair begins as loud as one alone.
static void testPair() {
    const double f = hzOf(60);
    CHECK(std::fabs(pairBeatHz(f, 20.0) - f * (std::pow(2.0, 10.0 / 1200.0) - std::pow(2.0, -10.0 / 1200.0))) < 1e-12);
    for (int cents : {20, 8}) { // VIBES at key 60: modes 1, 4, 10; 1 and 4 paired
        auto p = synth(plain(kVibes, {{Pair, cents}}));
        p->noteOn(60, 0.8f);
        const std::vector<float> v = play(*p, 48000 * 4);
        double hz[kModesMost];
        CHECK(p->modesOf(0, hz) == 5);
        CHECK(std::fabs(hz[0] / (f * pairRatio(cents, 0)) - 1.0) < 1e-12 && std::fabs(hz[3] / (f * pairRatio(cents, 1)) - 1.0) < 1e-12);
        CHECK(std::fabs(hz[1] / (4.0 * f * pairRatio(cents, 0)) - 1.0) < 1e-12 && std::fabs(hz[4] / (4.0 * f * pairRatio(cents, 1)) - 1.0) < 1e-12);
        CHECK(std::fabs(hz[2] / (10.0 * f) - 1.0) < 1e-12); // the third is not paired
        const double beat = beatHzOf(v, f, 0.02, 4.0), want = pairBeatHz(f, cents);
        if (gPrint) std::printf("  PAIR %d on VIBES at key 60: beats %.3f Hz, %.3f from its cents\n", cents, beat, want);
        CHECK(std::fabs(beat / want - 1.0) < 0.03);
        const double upper = beatHzOf(v, 4.0 * f, 0.02, 2.0);
        CHECK(std::fabs(upper / (4.0 * want) - 1.0) < 0.03); // the second mode beats four times as fast
    }
    { // a string: two strings; the fundamental beats at its pair's rate, the second harmonic twice as fast
        const double g = hzOf(48);
        auto p = synth(plain(kString, {{Strike, kPluck}, {Pair, 20}, {Hardness, 50}}));
        p->noteOn(48, 0.8f);
        const std::vector<float> v = play(*p, 48000 * 6);
        const double beat = beatHzOf(v, g, 0.05, 6.0), want = pairBeatHz(g, 20);
        if (gPrint) std::printf("  PAIR 20 on a STRING at key 48: beats %.3f Hz, %.3f from its cents\n", beat, want);
        CHECK(std::fabs(beat / want - 1.0) < 0.03);
        CHECK(std::fabs(beatHzOf(v, 2.0 * g, 0.05, 4.0) / (2.0 * want) - 1.0) < 0.03);
    }
    // As loud as one, at its start: the played note in the first 40 ms within 0.5 dB. (A bright string's
    // high harmonics go out of step within a few periods, as they must: 20 cents apart, its 20th
    // harmonic's two halves are half a turn apart in 10 ms.)
    for (int model : {kVibes, kBell, kString}) {
        auto one = synth(plain(model, {{Strike, model == kString ? 0 : 1}})), two = synth(plain(model, {{Strike, model == kString ? 0 : 1}, {Pair, 20}}));
        one->noteOn(57, 0.8f), two->noteOn(57, 0.8f);
        const std::vector<float> a = play(*one, 4800), b = play(*two, 4800);
        const double change = dB(sizeAt(b, hzOf(57), 0.0, 8) / sizeAt(a, hzOf(57), 0.0, 8));
        if (gPrint) std::printf("  PAIR 20 on %s: its note at the start %+.2f dB against one\n", modelName(model), change);
        CHECK(std::fabs(change) < 0.5);
    }
    { // BELL: 7 modes, 9 with PAIR. PAIR acts at the next strike, as the object's own make.
        auto p = synth(plain(kBell, {{Pair, 10}}));
        p->noteOn(60, 0.8f);
        play(*p, 64);
        double hz[kModesMost];
        CHECK(p->modesOf(0, hz) == 9);
        p->setSetting(Pair, 0);
        play(*p, 64);
        CHECK(p->modesOf(0, hz) == 9);
        p->noteOn(64, 0.8f);
        play(*p, 64);
        CHECK(p->modesOf(1, hz) == 7);
        p->noteOn(60, 0.8f); // the same key struck again with PAIR off: made again, not added to
        play(*p, 64);
        CHECK(p->modesOf(0, hz) == 7);
    }
    { // a paired string glides as one: half way through a glide its pitch is the lone string's
        auto one = synth(plain(kString, {{Strike, kPluck}})), two = synth(plain(kString, {{Strike, kPluck}, {Pair, 10}}));
        one->noteOnFrom(55, 0.8f, 48, 1.f), two->noteOnFrom(55, 0.8f, 48, 1.f);
        const std::vector<float> a = bandOf(play(*one, 48000 * 2), hzOf(51.5)), b = bandOf(play(*two, 48000 * 2), hzOf(51.5));
        double worst = 0.0;
        for (double t : {0.3, 0.5, 0.7}) {
            const std::size_t from = static_cast<std::size_t>(t * kRate), to = from + 960;
            worst = std::max(worst, std::fabs(1200.0 * std::log2(pitchOf(b, from, to) / pitchOf(a, from, to))));
        }
        if (gPrint) std::printf("  PAIR 10 on a gliding string: within %.2f cents of one string\n", worst);
        CHECK(worst < 5.0);
    }
}

// SELF: MOTOR is the level 1 - 0.6 (1 - cos 2 pi f t) / 2, f from MOTOR (0.2 to 8 Hz), t from the
// strike: the sound with it over the same sound without, sample by sample, is that.
static void testMotor() {
    CHECK(motorHz(0) == 0.0 && std::fabs(motorHz(1) - 0.2) < 1e-12 && std::fabs(motorHz(100) - 8.0) < 1e-12);
    for (int model : {kVibes, kString})
        for (int motor : {1, 40, 85, 100}) {
            auto with = synth(plain(model, {{Strike, model == kString ? 0 : 1}, {Motor, motor}})), plainOne = synth(plain(model, {{Strike, model == kString ? 0 : 1}}));
            play(*with, 777), play(*plainOne, 777); // (struck later than the synth was made: the turn is from the strike)
            with->noteOn(72, 0.8f), plainOne->noteOn(72, 0.8f);
            const std::vector<float> a = play(*with, 48000 * 6), b = play(*plainOne, 48000 * 6);
            const double hz = motorHz(motor);
            double worst = 0.0, least = 1.0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::fabs(b[i]) < 1e-3f) continue;
                const double t = static_cast<double>(i) / kRate, want = 1.0 - kMotorDepth * 0.5 * (1.0 - std::cos(2.0 * kPi * hz * t));
                worst = std::max(worst, std::fabs(a[i] / b[i] - want));
                least = std::min(least, static_cast<double>(a[i] / b[i]));
            }
            if (gPrint) std::printf("  MOTOR %d (%.2f Hz) on %s: the level off its sum by %.1e at worst, down to %.3f\n", motor, hz, modelName(model), worst, least);
            CHECK(worst < 2e-4);
            if (hz * 6.0 >= 0.5) CHECK(std::fabs(least - (1.0 - kMotorDepth)) < 0.01); // (it reached the bottom of its turn)
        }
    { // turned on while a note rings: from full, with no step
        auto with = synth(plain(kVibes)), plainOne = synth(plain(kVibes));
        with->noteOn(72, 0.8f), plainOne->noteOn(72, 0.8f);
        play(*with, 2003), play(*plainOne, 2003);
        with->setSetting(Motor, 100);
        const std::vector<float> a = play(*with, 4800), b = play(*plainOne, 4800);
        float step = 0.f, least = 1.f;
        for (std::size_t i = 1; i < a.size(); ++i)
            if (std::fabs(b[i]) > 1e-3f && std::fabs(b[i - 1]) > 1e-3f) step = std::max(step, std::fabs(a[i] / b[i] - a[i - 1] / b[i - 1])), least = std::min(least, a[i] / b[i]);
        CHECK(std::fabs(a[0] / b[0] - 1.f) < 1e-3f && step < 0.02f && least < 0.45f);
    }
    { // turned off while it turns: back to full within a tick, with no step
        auto with = synth(plain(kVibes, {{Motor, 90}})), plainOne = synth(plain(kVibes));
        with->noteOn(72, 0.8f), plainOne->noteOn(72, 0.8f);
        const std::vector<float> a1 = play(*with, 2000), b1 = play(*plainOne, 2000);
        with->setSetting(Motor, 0);
        const std::vector<float> a2 = play(*with, 4800), b2 = play(*plainOne, 4800);
        CHECK(std::fabs(a1.back() / b1.back() - 1.f) > 0.05f); // (it was down)
        for (std::size_t i = 32; i < a2.size(); ++i) CHECK(a2[i] == b2[i]);
        float step = 0.f;
        for (std::size_t i = 1; i < 32; ++i)
            if (std::fabs(b2[i]) > 1e-3f && std::fabs(b2[i - 1]) > 1e-3f) step = std::max(step, std::fabs(a2[i] / b2[i] - a2[i - 1] / b2[i - 1]));
        CHECK(step < 0.07f);
    }
}

// SELF: a NOISE strike is a burst HARDNESS long (2 s at 0, 2 ms at 100): the voice is awake and fed
// for as long, and its sound follows it on a short DECAY. Each sample is a hash of the note's seed:
// the same seed the same burst, another seed another. Its level: what is likely, at the mallet's.
static void testNoise() {
    CHECK(std::fabs(noiseSeconds(0) - 2.0) < 1e-12 && std::fabs(noiseSeconds(100) - 0.002) < 1e-12 && std::fabs(noiseSeconds(50) - 0.0632455532) < 1e-9);
    for (int model : {kWood, kString})
        for (int hardness : {0, 50, 100}) {
            const double length = noiseSeconds(hardness);
            auto p = synth(plain(model, {{Strike, kNoise}, {Hardness, hardness}, {Decay, 0}, {Material, 0}}));
            p->noteOn(model == kString ? 60 : 72, 0.8f);
            const std::size_t n = static_cast<std::size_t>((length + 0.6) * kRate);
            const std::vector<float> v = play(*p, n);
            // The burst's last loud stretch: the last 5 ms 40 dB under the loudest.
            double most = 0.0, end = 0.0, top = 0.0;
            for (std::size_t at = 0; at + 240 <= n; at += 240) most = std::max(most, rmsOf(v, at, at + 240));
            for (std::size_t at = 0; at + 240 <= n; at += 240) {
                const double r = rmsOf(v, at, at + 240);
                if (r > most * 0.01) end = (at + 240) / kRate;
                if (r == most) top = (at + 120) / kRate;
            }
            if (gPrint) std::printf("  NOISE HARDNESS %d (%.3f s) on %s: heard to %.3f s, loudest at %.3f s\n", hardness, length, modelName(model), end, top);
            CHECK(end > 0.8 * length && end < length + 0.15);
            if (hardness == 0) CHECK(top > 0.6 && top < 1.4); // (a half-sine: loudest in its middle)
            play(*p, 48000);
            CHECK(p->awake() == 0);
        }
    { // awake for its whole length, however short its DECAY
        auto p = synth(plain(kGlass, {{Strike, kNoise}, {Hardness, 0}, {Decay, 0}}));
        p->noteOn(72, 0.8f);
        play(*p, static_cast<std::size_t>(1.99 * kRate));
        CHECK(p->awake() == 1);
    }
    { // a hash: the same seed, the same; another, another
        Sum a, b, c;
        for (std::uint32_t seed : {5u, 5u, 6u}) {
            auto p = synth(plain(kGlass, {{Strike, kNoise}, {Hardness, 40}}));
            p->noteSeed(seed);
            p->noteOn(76, 0.8f);
            (seed == 6u ? c : (a.h == Sum().h ? a : b)).add(play(*p, 9600));
        }
        CHECK(a.h == b.h && a.h != c.h);
    }
    // Its level over 40 notes: never silent, never near full, a long burst into a long ring too.
    for (int model : {kGlass, kDrum, kString, kMarimba})
        for (int hardness : {0, 100}) {
            float least = 1.f, most = 0.f;
            for (std::uint32_t seed = 1; seed <= 40; ++seed) {
                auto p = synth(plain(model, {{Strike, kNoise}, {Hardness, hardness}}));
                p->noteSeed(seed * 7919u);
                p->noteOn(model == kString ? 52 : 64, 1.f);
                const float peak = peakOf(play(*p, static_cast<std::size_t>((noiseSeconds(hardness) + 0.3) * kRate)));
                least = std::min(least, peak), most = std::max(most, peak);
            }
            if (gPrint) std::printf("  NOISE HARDNESS %d on %s: peaks %.3f to %.3f over 40 notes\n", hardness, modelName(model), least, most);
            CHECK(least > 0.02f && most < 0.5f);
        }
    { // into a string through the pick comb: blown at its middle, its even harmonics 20 dB under the odd
        auto p = synth(plain(kString, {{Strike, kNoise}, {Hardness, 0}, {Position, 100}, {Decay, 70}}));
        p->noteOn(45, 0.8f);
        const std::vector<float> v = play(*p, 48000);
        const double f = hzOf(45);
        const double odd = sizeAt(v, f, 0.5, 32) + sizeAt(v, 3 * f, 0.5, 96), even = sizeAt(v, 2 * f, 0.5, 64) + sizeAt(v, 4 * f, 0.5, 128);
        if (gPrint) std::printf("  NOISE on a string at 1/2: the even harmonics %.1f dB under the odd\n", dB(odd / even));
        CHECK(dB(odd / even) > 20.0);
    }
}

// The BODY's resonances by themselves (FR ch. 9's figures SELF): at each, in step with what comes in
// and as large (+6 dB at BODY MIX 100); far from them, nothing that shows. Silence in: it rests.
static void testBodyResonances() {
    for (int body : {kBodyGuitar, kBodyBox}) {
        BodyResonance res[kBodyModesMost];
        const int n = bodyResonances(body, res);
        CHECK(n == (body == kBodyGuitar ? 3 : 2));
        std::vector<double> at;
        for (int k = 0; k < n; ++k) at.push_back(res[k].hz);
        at.push_back(3000.0);
        for (double hz : at) {
            BodyBank bank;
            bank.set(body, kRate);
            std::vector<float> l = sineOf(hz, 0.1, 96000), r = l;
            const std::vector<float> dry = l;
            float mix = 1.f;
            bank.run(l.data(), r.data(), 96000, mix, 1.f, 0.f);
            const double gain = sizeAt(l, hz, 1.5, 32) / sizeAt(dry, hz, 1.5, 32);
            // Nearby resonances add a little to each (they are no further apart than an octave).
            if (gPrint) std::printf("  BODY %s at %.0f Hz: %+.2f dB\n", bodyName(body), hz, dB(gain));
            if (hz == 3000.0) CHECK(std::fabs(dB(gain)) < 0.5);
            else CHECK(dB(gain) > 5.0 && dB(gain) < 7.5);
            CHECK(l == r);
        }
        BodyBank bank;
        bank.set(body, kRate);
        std::vector<float> l = sineOf(200.0, 0.3, 4800), r = l;
        float mix = 1.f;
        bank.run(l.data(), r.data(), 4800, mix, 1.f, 0.f);
        CHECK(!bank.silent());
        std::vector<float> z(48000, 0.f), y(48000, 0.f);
        bank.run(z.data(), y.data(), 48000, mix, 1.f, 0.f); // its ring, then nothing
        CHECK(bank.silent());
        for (float s : z) CHECK(s == 0.f || std::fabs(s) >= FLT_MIN);
    }
}

// Through the synth: GUITAR on a string at key 31 (49 Hz) lifts its 2nd harmonic (98 Hz, the air
// mode) about 6 dB and leaves its 20th (980 Hz) as it was; BODY MIX 0 and NONE are the same to the
// bit; NONE turned while it rings slides out with no step; TUBES has no box after the voices.
static void testBody() {
    const auto render = [](std::vector<Set> sets) {
        sets.push_back({Strike, kPluck});
        auto p = synth(plain(kString, sets));
        p->noteOn(31, 0.8f);
        return play(*p, 48000 * 2);
    };
    const std::vector<float> none = render({}), guitar = render({{Body, kBodyGuitar}, {BodyMix, 100}});
    const double f = hzOf(31);
    const double second = dB(sizeAt(guitar, 2 * f, 0.5, 32) / sizeAt(none, 2 * f, 0.5, 32)), twentieth = dB(sizeAt(guitar, 20 * f, 0.5, 64) / sizeAt(none, 20 * f, 0.5, 64));
    if (gPrint) std::printf("  GUITAR on a string at key 31: the 2nd harmonic %+.1f dB, the 20th %+.1f dB\n", second, twentieth);
    CHECK(second > 4.5 && second < 7.5 && std::fabs(twentieth) < 1.0);
    CHECK(render({{Body, kBodyGuitar}, {BodyMix, 0}}) == none && render({{Body, kBodyTubes}, {BodyMix, 0}}) == none && render({{BodyMix, 100}}) == none);
    CHECK(render({{Body, kBodyGuitar}, {BodyMix, 100}, {Body, kBodyBox}}) == render({{Body, kBodyBox}, {BodyMix, 100}})); // another box: its own resonances
    { // half of it is half as much again
        const std::vector<float> half = render({{Body, kBodyGuitar}, {BodyMix, 50}});
        CHECK(std::fabs(sizeAt(half, 2 * f, 0.5, 32) / sizeAt(none, 2 * f, 0.5, 32) - 1.5) < 0.15);
    }
    { // BOX on a kalimba: up at 180 Hz
        auto a = synth(plain(kKalimba)), b = synth(plain(kKalimba, {{Body, kBodyBox}, {BodyMix, 100}}));
        a->noteOn(54, 0.8f), b->noteOn(54, 0.8f); // 185 Hz
        const std::vector<float> x = play(*a, 48000), y = play(*b, 48000);
        CHECK(dB(sizeAt(y, hzOf(54), 0.4) / sizeAt(x, hzOf(54), 0.4)) > 3.0);
    }
    { // turned to NONE while it rings: it slides out over 5 ms, no step
        auto p = synth(plain(kString, {{Strike, kPluck}, {Body, kBodyGuitar}, {BodyMix, 100}}));
        auto q = synth(plain(kString, {{Strike, kPluck}}));
        p->noteOn(31, 0.8f), q->noteOn(31, 0.8f);
        play(*p, 24000), play(*q, 24000);
        p->setSetting(Body, kBodyNone);
        const std::vector<float> a = play(*p, 960), b = play(*q, 960);
        float step = 0.f;
        for (std::size_t i = 1; i < a.size(); ++i) step = std::max(step, std::fabs((a[i] - b[i]) - (a[i - 1] - b[i - 1])));
        CHECK(step < 0.01f);
        for (std::size_t i = 240; i < a.size(); ++i) CHECK(a[i] == b[i]);
    }
}

// SELF: TUBES (BODY MIX 100): the played mode rings 3 times as long and is struck twice as hard
// against the rest; the other modes ring as they did. On a string the fundamental rings 3 times as
// long and the 5th harmonic as it did. CHIME has no played mode: its lowest.
static void testTubes() {
    for (int model : {kMarimba, kChime}) {
        auto a = synth(plain(model, {{Decay, 55}})), b = synth(plain(model, {{Decay, 55}, {Body, kBodyTubes}, {BodyMix, 100}}));
        a->noteOn(60, 0.8f), b->noteOn(60, 0.8f);
        const std::vector<float> x = play(*a, 48000 * 4), y = play(*b, 48000 * 4);
        const double f1 = hzOf(60) * modelRow(model).ratio[0], f2 = hzOf(60) * modelRow(model).ratio[1];
        const double longer = t60At(y, f1, 0.1, 1.0) / t60At(x, f1, 0.1, 1.0), other = t60At(y, f2, 0.1, 0.5) / t60At(x, f2, 0.1, 0.5);
        const double louder = (sizeAt(y, f1, 0.0, 16) / sizeAt(y, f2, 0.0, 32)) / (sizeAt(x, f1, 0.0, 16) / sizeAt(x, f2, 0.0, 32));
        if (gPrint) std::printf("  TUBES on %s: its mode 1 rings %.2f times as long, mode 2 %.2f; mode 1 %.2f times as loud against mode 2\n", modelName(model), longer, other, louder);
        CHECK(std::fabs(longer - 3.0) < 0.15 && std::fabs(other - 1.0) < 0.05 && louder > 1.9 && louder < 2.35); // (over 2: the window sees mode 1's longer ring)
    }
    {
        auto a = synth(plain(kString, {{Strike, kPluck}, {Decay, 55}, {Material, 60}})), b = synth(plain(kString, {{Strike, kPluck}, {Decay, 55}, {Material, 60}, {Body, kBodyTubes}, {BodyMix, 100}}));
        a->noteOn(45, 0.8f), b->noteOn(45, 0.8f);
        const std::vector<float> x = play(*a, 48000 * 4), y = play(*b, 48000 * 4);
        const double f = hzOf(45);
        const double longer = t60At(y, f, 0.1, 1.0) / t60At(x, f, 0.1, 1.0), fifth = t60At(y, 5 * f, 0.1, 0.3, 64) / t60At(x, 5 * f, 0.1, 0.3, 64);
        if (gPrint) std::printf("  TUBES on a string: the fundamental rings %.2f times as long, the 5th harmonic %.2f\n", longer, fifth);
        CHECK(std::fabs(longer - 3.0) < 0.2 && std::fabs(fifth - 1.0) < 0.1);
    }
}

// SELF: BUZZ: the sound with it less the sound without is a sizzle exactly where the note is over its
// gap (BUZZ's dB under its loudest so far), nothing anywhere else; a bigger BUZZ rattles longer; it is
// noise (mostly over 5 kHz).
static void testBuzz() {
    CHECK(buzzDb(0) == 0.0 && buzzGap(0) == 0.0 && std::fabs(buzzDb(1) - 6.0) < 1e-12 && std::fabs(buzzDb(100) - 36.0) < 1e-12);
    double lastWas = 0.0;
    for (int buzz : {1, 50, 100}) {
        auto a = synth(plain(kKalimba, {{Decay, 40}, {Strike, kPluck}})), b = synth(plain(kKalimba, {{Decay, 40}, {Strike, kPluck}, {Buzz, buzz}}));
        a->noteOn(64, 0.8f), b->noteOn(64, 0.8f);
        const std::vector<float> x = play(*a, 48000), y = play(*b, 48000);
        const float gap = static_cast<float>(buzzGap(buzz));
        float most = 0.f;
        long wrong = 0, over = 0;
        double last = 0.0;
        std::vector<float> d(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            d[i] = y[i] - x[i];
            const float s = std::fabs(x[i]);
            most = std::max(most, s);
            const bool shouldBe = s > gap * most * 1.0001f, quiet = s < gap * most * 0.9999f;
            over += shouldBe;
            if ((shouldBe && d[i] == 0.f) || (quiet && d[i] != 0.f)) ++wrong;
            if (d[i] != 0.f) last = i / kRate;
        }
        std::vector<float> high = d;
        for (std::size_t i = 1; i < high.size(); ++i) high[i] = d[i] - d[i - 1]; // (a rough tilt up: the sizzle is bright)
        if (gPrint) std::printf("  BUZZ %d (%.0f dB): rattles to %.3f s, %ld samples over its gap, %ld off\n", buzz, buzzDb(buzz), last, over, wrong);
        CHECK(over > 100 && wrong < over / 200 + 2);
        CHECK(last > lastWas);
        lastWas = last;
        CHECK(rmsOf(high) > 0.9 * rmsOf(d)); // white noise's first difference is 1.41 times it; a note's far under
    }
}

// ---- the cost ------------------------------------------------------------------------------------------

// The bench's way (the plan, section 6): 8 notes struck again every half second, DECAY 100 (none
// sleeps), 256-frame blocks; the best of three runs of 600 blocks. Percent of one core.
static double costOf(int model, int preset, int glide, const std::vector<Set>& more = {}) {
    double best = 1e30;
    for (int run = 0; run < 3; ++run) {
        auto p = synth({}, preset);
        if (preset < 0) {
            for (const Set& s : plain(model, {{Strike, model == kString ? 0 : 1}, {Material, 50}, {Hardness, 50}})) p->setSetting(s.id, s.value);
        }
        for (const Set& s : more) p->setSetting(s.id, s.value);
        p->setSetting(Decay, 100);
        std::vector<float> l(256), r(256);
        const int keys[8] = {48, 52, 55, 59, 62, 65, 69, 72};
        long block = 0;
        int sleeping = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 600; ++b) {
            if (block % 94 == 0)
                for (int k = 0; k < 8; ++k) {
                    if (glide && block > 0) p->noteOnFrom(keys[k] + (block / 94 % 2 ? 2 : 0), 0.8f, keys[k] + (block / 94 % 2 ? 0 : 2), 0.5f);
                    else p->noteOn(keys[k], 0.8f);
                }
            ++block;
            p->render(l.data(), r.data(), 256);
            sleeping += p->awake() < 8;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 600.0;
        CHECK(sleeping == 0);
        best = std::min(best, ms);
    }
    return 100.0 * best / (256.0 / 48.0);
}
static void measureParts() {
    for (int model : {int(kString), int(kBell)})
        for (int glide : {0, 1}) {
            std::printf("  %s glide %d: plain %.2f", modelName(model), glide, costOf(model, -1, glide));
            const std::vector<std::vector<Set>> parts = {{{Pair, 20}}, {{Motor, 60}}, {{Body, kBodyGuitar}, {BodyMix, 80}}, {{Buzz, 50}}, {{Dice, 100}}};
            const char* names[] = {"pair", "motor", "body", "buzz", "dice"};
            for (std::size_t i = 0; i < parts.size(); ++i) std::printf(", %s %.2f", names[i], costOf(model, -1, glide, parts[i]));
            std::printf("\n");
        }
}
static void measureCost() {
    if (std::getenv("PARTS")) return measureParts();
    if (const char* what = std::getenv("PROFILE")) { // (one row, for callgrind)
        const std::vector<Set> all = {{Pair, 20}, {Motor, 60}, {Body, kBodyGuitar}, {BodyMix, 80}, {Buzz, 50}, {Dice, 100}};
        std::printf("%.2f%%\n", costOf(what[0] == 's' ? int(kString) : int(kBell), -1, what[1] == 'g', all));
        return;
    }
    std::printf("  cost, 8 notes struck every half second (desk; the handheld is 13 to 17 times this):\n");
    for (int model = 0; model < kModels; ++model) {
        const double c = costOf(model, -1, 0);
        std::printf("    %-8s %d modes  %.2f%% of one core  (handheld about %.0f to %.0f%%)\n", modelName(model), modelRow(model).modes, c, c * 13, c * 17);
    }
    const double glide = costOf(kBell, -1, 1), stringGlide = costOf(kString, -1, 1);
    std::printf("    BELL, every note gliding all the while %.2f%%; STRING the same %.2f%%\n", glide, stringGlide);
    // Batch 10: PAIR doubles a string; a modal model's two lowest modes; BODY one more small bank a
    // track; MOTOR and BUZZ a sample loop of their own; NOISE a hash a sample while it lasts.
    const std::vector<Set> all = {{Pair, 20}, {Motor, 60}, {Body, kBodyGuitar}, {BodyMix, 80}, {Buzz, 50}, {Dice, 100}};
    std::printf("    Twelve (PAIR on a string) %.2f%%; STRING with PAIR, BODY, MOTOR, BUZZ, gliding %.2f%%\n", costOf(0, presetNamed("Twelve"), 0), costOf(kString, -1, 1, all));
    std::printf("    BELL with PAIR, BODY, MOTOR, BUZZ, DICE 100, gliding (the worst) %.2f%%; DRUM the same %.2f%%\n", costOf(kBell, -1, 1, all), costOf(kDrum, -1, 1, all));
    std::printf("    ...not gliding: BELL %.2f%%, DRUM %.2f%%, STRING %.2f%%, MARIMBA %.2f%%\n", costOf(kBell, -1, 0, all), costOf(kDrum, -1, 0, all), costOf(kString, -1, 0, all), costOf(kMarimba, -1, 0, all));
    std::printf("    NOISE 2 s, GLASS %.2f%%, STRING %.2f%%; Vibes (MOTOR, TUBES) %.2f%%; Kalimba (BOX, BUZZ) %.2f%%\n", costOf(kGlass, -1, 0, {{Strike, kNoise}, {Hardness, 0}}),
                costOf(kString, -1, 0, {{Strike, kNoise}, {Hardness, 0}}), costOf(0, presetNamed("Vibes"), 0), costOf(0, presetNamed("Kalimba"), 0));
}

// ---- the program ---------------------------------------------------------------------------------------

struct Named {
    const char* name;
    void (*run)();
};

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testTables", testTables},
        {"testShapes", testShapes},
        {"testRule", testRule},
        {"testTuning", testTuning},
        {"testModeRatios", testModeRatios},
        {"testHeldAtTheEdge", testHeldAtTheEdge},
        {"testPosition", testPosition},
        {"testDecay", testDecay},
        {"testMaterial", testMaterial},
        {"testMaterialGlides", testMaterialGlides},
        {"testHertz", testHertz},
        {"testStretch", testStretch},
        {"testMacrosCheap", testMacrosCheap},
        {"testStringTuning", testStringTuning},
        {"testStiffness", testStiffness},
        {"testStringMallet", testStringMallet},
        {"testPeakBound", testPeakBound},
        {"testRestrike", testRestrike},
        {"testGlideClicks", testGlideClicks},
        {"testTakeOver", testTakeOver},
        {"testSteal", testSteal},
        {"testBlocks", testBlocks},
        {"testTooSmall", testTooSmall},
        {"testNaN", testNaN},
        {"testSilence", testSilence},
        {"testNoAllocation", testNoAllocation},
        {"testPlainControls", testPlainControls},
        {"testEveryPreset", testEveryPreset},
        {"testSeeds", testSeeds},
        {"testRollsDiffer", testRollsDiffer},
        {"testPlayFromTheTop", testPlayFromTheTop},
        {"testPhysicSong", testPhysicSong},
        {"testVersionOne", testVersionOne},
        {"testPair", testPair},
        {"testMotor", testMotor},
        {"testNoise", testNoise},
        {"testBodyResonances", testBodyResonances},
        {"testBody", testBody},
        {"testTubes", testTubes},
        {"testBuzz", testBuzz},
    };
    std::vector<std::string> only;
    bool measure = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--measure") measure = gPrint = true;
        else if (a == "--print") gPrint = true;
        else only.push_back(a);
    }
    for (const Named& t : tests) {
        if (!only.empty() && std::find(only.begin(), only.end(), t.name) == only.end()) continue;
        if (measure && only.empty() && std::strcmp(t.name, "testTooSmall") != 0 && std::strcmp(t.name, "testTuning") != 0 && std::strcmp(t.name, "testStringTuning") != 0 &&
            std::strcmp(t.name, "testStiffness") != 0 && std::strcmp(t.name, "testHertz") != 0 && std::strcmp(t.name, "testGlideClicks") != 0 && std::strcmp(t.name, "testEveryPreset") != 0 &&
            std::strcmp(t.name, "testPhysicSong") != 0)
            continue;
        const auto t0 = std::chrono::steady_clock::now();
        t.run();
        if (gPrint) std::printf("%s: %.2f s\n", t.name, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    if (measure) measureCost();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("physic tests passed\n");
    return 0;
}
