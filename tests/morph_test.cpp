// MORPH (docs/plans/wavetable-plan.md): the rule header (batch 0), the tables and the voice (batch 1),
// through the synth and through the engine, and the house rules (blocks, Play from the top, no
// allocation, no number too small to hold, NaN, silence).
//
// Every figure MORPH holds is SELF (the plan's own choice: morph_rule.hpp says why). A green test
// only says the code does what the plan chose. NOBODY HAS HEARD ANY OF IT.
//
//   morph_test                  all of them
//   morph_test testSizes ...    just those
//   morph_test --measure        the figures (and the cost: morph_test --measure cost, alone)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// (As the synth will be: no multiply and add joined.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "apotheca/engine.hpp"
#include "apotheca/envelope.hpp"
#include "apotheca/morph.hpp"
#include "apotheca/morph_rule.hpp"
#include "measure.hpp"
#include "morph/tables.hpp"

using namespace apotheca;
using namespace apotheca::morph;

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
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool gPrint = false; // --measure: say the figures
static bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }
static const double kRate = 48000.0;

// The settings table: ids, ranges, usual values, names (the plan's section 1).
static void testSettings() {
    CHECK(kSettings == 29 && kSettingRoom >= kSettings);
    CHECK(TableA == 0 && PositionA == 1 && TableB == 2 && Mix == 4 && Unison == 10 && Filter == 13 && Cutoff == 14);
    CHECK(Attack == 17 && EnvAttack == 21 && EnvPosition == 25 && Velocity == 27 && Level == 28);
    for (int id = 0; id < kSettings; ++id) {
        CHECK(settingMin(id) <= settingUsual(id) && settingUsual(id) <= settingMax(id));
        CHECK(settingHeld(id, -1000) == settingMin(id) && settingHeld(id, 1000) == settingMax(id));
        CHECK(settingHeld(id, settingUsual(id)) == settingUsual(id));
    }
    CHECK(settingMax(TableA) == 15 && settingMax(TableB) == 16 && settingMin(TableB) == 0);
    CHECK(settingMin(BSemi) == -24 && settingMax(BSemi) == 24 && settingMin(BFine) == -50 && settingMax(BFine) == 50);
    CHECK(settingMin(Octave) == -2 && settingMax(Octave) == 2 && settingMin(Tune) == -100 && settingMax(Tune) == 100);
    CHECK(settingMin(Unison) == 1 && settingMax(Unison) == 4 && settingMax(Filter) == kHigh);
    CHECK(settingMin(EnvPosition) == -100 && settingMin(EnvCutoff) == -100 && settingMax(EnvCutoff) == 100);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6 && settingMax(Cutoff) == 100);
    // A new track's sound: ANALOG alone, B off, LOW 12 wide open, the envelopes as the plan says.
    CHECK(settingUsual(TableA) == Analog && settingUsual(TableB) == 0 && settingUsual(Unison) == 1);
    CHECK(settingUsual(Filter) == kLow12 && settingUsual(Cutoff) == 100 && settingUsual(Mix) == 50);
    CHECK(settingUsual(Sustain) == 100 && settingUsual(EnvSustain) == 0 && settingUsual(Level) == 0);
    CHECK(near(envelope::attackSeconds(static_cast<float>(settingUsual(Attack))), 0.002, 0.0002));
    CHECK(near(envelope::fallSeconds(static_cast<float>(settingUsual(Release))), 0.2, 0.001));
    CHECK(near(envelope::fallSeconds(static_cast<float>(settingUsual(EnvDecay))), 0.5, 0.01));
    // Only the three choices that change what a key is made of wait for a strike.
    for (int id = 0; id < kSettings; ++id) CHECK(readAtStrike(id) == (id == TableA || id == TableB || id == Unison));
    // Names.
    CHECK(std::strcmp(tableName(Analog), "ANALOG") == 0 && std::strcmp(tableName(Glass), "GLASS") == 0);
    CHECK(std::strcmp(tableName(-3), "ANALOG") == 0 && std::strcmp(tableName(99), "ANALOG") == 0);
    for (int t = 0; t < kTables; ++t) {
        CHECK(std::strlen(tableName(t)) <= 8); // each fits a box at XL (HARMONIC is the longest)
        for (int u = 0; u < t; ++u) CHECK(std::strcmp(tableName(t), tableName(u)) != 0);
        CHECK(tableOfB(t + 1) == t && std::strcmp(tableBName(t + 1), tableName(t)) == 0);
    }
    CHECK(tableOfB(0) == -1 && tableOfB(-1) == -1 && tableOfB(kTables + 1) == -1 && std::strcmp(tableBName(0), "OFF") == 0);
    CHECK(std::strcmp(filterName(kFilterOff), "OFF") == 0 && std::strcmp(filterName(kLow24), "LOW 24") == 0 &&
          std::strcmp(filterName(kHigh), "HIGH") == 0 && std::strcmp(filterName(9), "OFF") == 0);
}

// The macros: eight, none a choice, POSITION bending A's and B's; the house rule; the slide.
static void testMacros() {
    const int want[kMacros] = {PositionA, Mix, Cutoff, Resonance, EnvPosition, EnvCutoff, Detune, Release};
    for (int m = 0; m < kMacros; ++m) {
        CHECK(macroSetting(m) == want[m]);
        CHECK(!readAtStrike(macroSetting(m)) && macroSetting(m) != Filter); // a macro moves while a key sounds
        CHECK(macroAlsoBends(m) == (m == MacroPosition ? PositionB : -1));
    }
    CHECK(macroSetting(-1) == PositionA && macroSetting(8) == PositionA);
    CHECK(macroBent(30.f, 0.5f, 0.f, 100.f) == 30.f && macroBent(30.f, 0.f, 0.f, 100.f) == 0.f &&
          macroBent(30.f, 1.f, 0.f, 100.f) == 100.f && near(macroBent(30.f, 0.75f, 0.f, 100.f), 65.f, 1e-4) &&
          near(macroBent(30.f, 0.25f, 0.f, 100.f), 15.f, 1e-4));
    CHECK(macroBent(0.f, 0.f, -100.f, 100.f) == -100.f); // ENV > POSITION bends both ways
    // The slide: from, then a straight line, exactly `to` from 512 samples on.
    CHECK(kMacroSlideSamples == 512);
    CHECK(macroSlid(0.2f, 0.8f, 0) == 0.2f && macroSlid(0.2f, 0.8f, -5) == 0.2f);
    CHECK(macroSlid(0.2f, 0.8f, 512) == 0.8f && macroSlid(0.2f, 0.8f, 100000) == 0.8f);
    CHECK(near(macroSlid(0.2f, 0.8f, 256), 0.5, 1e-6) && near(macroSlid(0.2f, 0.8f, 128), 0.35, 1e-6));
    float last = macroSlid(0.2f, 0.8f, 0), biggest = 0.f;
    for (long s = 1; s <= 600; ++s) {
        const float v = macroSlid(0.2f, 0.8f, s);
        CHECK(v >= last);
        biggest = std::max(biggest, v - last);
        last = v;
    }
    CHECK(biggest < 0.6f / 512.f * 1.01f); // a line: no step past 1/512 of the move
}

// The readings at both ends and in the middle.
static void testReadings() {
    CHECK(positionPlace(0, 32) == 0.0 && positionPlace(100, 32) == 31.0 && positionPlace(50, 32) == 15.5);
    CHECK(positionPlace(-4, 32) == 0.0 && positionPlace(140, 32) == 31.0 && positionPlace(NAN, 32) == 0.0);
    CHECK(positionFrame(0, 32) == 1.0 && positionFrame(100, 32) == 32.0 && positionPlace(100, 64) == 63.0);
    CHECK(envPositionFrames(100, 32) == 31.0 && envPositionFrames(-100, 32) == -31.0 && envPositionFrames(-500, 32) == -31.0);
    CHECK(mixShare(0) == 0.0 && mixShare(100) == 1.0 && mixShare(30) == 0.3);
    CHECK(bCents(-24, 0) == -2400.0 && bCents(7, -3) == 697.0 && bRatio(12, 0) == 2.0 && bRatio(0, 0) == 1.0);
    CHECK(near(bRatio(-24, 0), 0.25, 1e-15) && near(bRatio(7, 0), 1.4983070768766815, 1e-12));
    CHECK(subGain(0) == 0.0 && subGain(100) == 1.0 && std::isinf(subDb(0)) && subDb(0) < 0.0 && subDb(100) == 0.0 &&
          near(subDb(50), -6.0206, 1e-3));
    CHECK(detuneCents(0) == 0.0 && detuneCents(100) == 50.0 && near(detuneCents(20), 2.0, 1e-12) && near(detuneCents(50), 12.5, 1e-12));
    CHECK(widthShare(0) == 0.0 && widthShare(100) == 1.0);
    CHECK(near(cutoffHz(0), 20.0, 1e-12) && near(cutoffHz(100), 20000.0, 1e-9) && near(cutoffHz(50), 632.455532, 1e-5));
    CHECK(cutoffHz(-10) == cutoffHz(0) && cutoffHz(1000) == cutoffHz(100));
    CHECK(near(resonanceQ(0), 0.5, 1e-12) && near(resonanceQ(100), 20.0, 1e-12) && near(resonanceQ(50), std::sqrt(10.0), 1e-12));
    CHECK(keyTrackOctaves(100, 72) == 1.0 && keyTrackOctaves(100, 48) == -1.0 && keyTrackOctaves(50, 84) == 1.0 &&
          keyTrackOctaves(0, 100) == 0.0 && keyTrackOctaves(100, 60) == 0.0);
    CHECK(envCutoffOctaves(100) == 6.0 && envCutoffOctaves(-100) == -6.0 && envCutoffOctaves(50) == 3.0);
    // Where the cutoff is: the three added in octaves, held to 20 Hz and 0.45 of the rate.
    CHECK(near(cutoffAt(50, 0, 60, 0, 0, kRate), cutoffHz(50), 1e-9));
    CHECK(near(cutoffAt(50, 100, 72, 0, 0, kRate), 2 * cutoffHz(50), 1e-9));
    CHECK(near(cutoffAt(50, 0, 60, 50, 1.0, kRate), 8 * cutoffHz(50), 1e-7));
    CHECK(near(cutoffAt(50, 0, 60, -50, 0.5, kRate), cutoffHz(50) / std::exp2(1.5), 1e-9));
    CHECK(cutoffAt(100, 100, 127, 100, 1, kRate) == 0.45 * kRate && cutoffAt(0, 100, 0, -100, 1, kRate) == 20.0);
    CHECK(cutoffAt(NAN, 0, 60, 0, 0, kRate) == 20.0);
    CHECK(levelGain(0) == 1.0 && near(levelGain(-6), 0.501187, 1e-6) && near(levelGain(6), 1.995262, 1e-6) &&
          levelGain(-100) == levelGain(-24) && levelGain(40) == levelGain(6));
    CHECK(velocityGain(0, 0.2) == 1.0 && near(velocityGain(100, 0.2), 0.2, 1e-12) && near(velocityGain(50, 0.2), 0.6, 1e-12));
    CHECK(velocityGain(100, 2.0) == 1.0 && velocityGain(100, -1.0) == 0.0 && velocityGain(100, NAN) == 0.0);
    CHECK(near(keyHz(69, 0, 0, 0), 440.0, 1e-12) && near(keyHz(57, 1, 0, 0), 440.0, 1e-12) && near(keyHz(69, 0, 100, 0), keyHz(70, 0, 0, 0), 1e-9));
    CHECK(near(keyHz(69, -2, 0, 0), 110.0, 1e-12) && near(keyHz(64, 0, 0, 5), 440.0, 1e-12));
    CHECK(tuneCents(-100) == -100.0);
}

// The sizes: each one's harmonics and length, 8 points a harmonic where 2048 allows, the memory a
// frame takes (the plan's 50 KB), and which size each pitch reads.
static void testSizes() {
    CHECK(kSizes == 18 && sizeHarmonics(0) == 512 && sizeHarmonics(kSizes - 1) == 1);
    long floats = 0;
    for (int m = 0; m < kSizes; ++m) {
        const int h = sizeHarmonics(m), len = sizeLength(m);
        CHECK(h == static_cast<int>(std::floor(512.0 / std::pow(2.0, m / 2.0) + 1e-9)));
        if (m > 0) CHECK(h <= sizeHarmonics(m - 1) && len <= sizeLength(m - 1));
        CHECK((len & (len - 1)) == 0 && len >= kLengthLeast && len <= kLengthMost);
        CHECK((1 << sizeBits(m)) == len);
        CHECK(len >= std::min(kLengthMost, kPointsPerHarmonic * h) && (len == kLengthLeast || len / 2 < kPointsPerHarmonic * h)); // the least that holds 32 a harmonic
        CHECK(2 * h < len); // every harmonic under the size's own half: it holds them
        floats += len;
    }
    CHECK(floats == 20480);                      // 80 KB a frame (batch 1: 32 points a harmonic; the plan had 50 KB at 8)
    CHECK(frameFloats() == 20480 + 3 * kSizes);  // with the guard points
    CHECK(sizeLength(0) == 2048 && sizeLength(7) == 2048 && sizeLength(8) == 1024 && sizeLength(11) == 512 && sizeLength(13) == 256 &&
          sizeLength(14) == 128 && sizeLength(17) == 128);
    CHECK(sizeHarmonics(-1) == 512 && sizeHarmonics(40) == 1);
    CHECK(foldCeiling(48000.0) == 28000.0);
    // Which size: the first whose top harmonic is at or under the ceiling, at every key.
    for (int rateIndex = 0; rateIndex < 2; ++rateIndex) {
        const double rate = rateIndex ? 44100.0 : 48000.0;
        int lastSize = 0;
        for (int key = 0; key <= 127; ++key) {
            const double hz = keyHz(key, 0, 0, 0);
            const int m = sizeFor(hz, rate);
            CHECK(m >= lastSize); // higher keys never read a richer size
            lastSize = m;
            if (hz * sizeHarmonics(kSizes - 1) <= foldCeiling(rate)) {
                CHECK(sizeHarmonics(m) * hz <= foldCeiling(rate));
                if (m > 0) CHECK(sizeHarmonics(m - 1) * hz > foldCeiling(rate));
            } else {
                CHECK(m == kSizes - 1);
            }
        }
    }
    CHECK(sizeFor(20.0, kRate) == 0 && sizeFor(54.0, kRate) == 0 && sizeFor(55.0, kRate) == 1 && sizeFor(30000.0, kRate) == kSizes - 1);
    CHECK(sizeFor(0.0, kRate) == 0);
    CHECK(sizeFor(54.6875, kRate) == 0 && sizeFor(7000.0, kRate) == 14); // a top exactly on the ceiling (28000 Hz) is held
    // At 48 kHz a size changing drops only harmonics over 19.8 kHz: at the pitch where size m-1
    // stops holding (its top just over the ceiling), the first harmonic m does not hold.
    double lowestDropped = 1e9;
    for (int m = 1; m < kSizes; ++m) {
        if (sizeHarmonics(m) == sizeHarmonics(m - 1)) continue; // the same harmonics: nothing dropped
        const double hz = foldCeiling(kRate) / sizeHarmonics(m - 1) * (1.0 + 1e-9);
        CHECK(sizeFor(hz, kRate) == m || sizeHarmonics(sizeFor(hz, kRate)) == sizeHarmonics(m));
        lowestDropped = std::min(lowestDropped, (sizeHarmonics(m) + 1) * hz);
    }
    CHECK(lowestDropped >= 19800.0);
    if (gPrint) std::printf("  at 48 kHz a size change drops harmonics from %.0f Hz up; a frame is %ld floats (%.1f KB)\n", lowestDropped,
                            frameFloats(), frameFloats() * 4 / 1024.0);
}

// POSITION to the two frames read: the first frame exactly at 0, the last exactly at the end, the
// crossfade going up evenly between, never past the table.
static void testFramePlace() {
    int j = -1;
    double t = -1;
    framePlace(0.0, 32, &j, &t);
    CHECK(j == 0 && t == 0.0);
    framePlace(31.0, 32, &j, &t);
    CHECK(j == 30 && t == 1.0); // the last frame: the pair before it, all the way
    framePlace(15.25, 32, &j, &t);
    CHECK(j == 15 && t == 0.25);
    framePlace(3.0, 32, &j, &t);
    CHECK(j == 3 && t == 0.0);
    framePlace(-2.0, 32, &j, &t);
    CHECK(j == 0 && t == 0.0);
    framePlace(99.0, 32, &j, &t);
    CHECK(j == 30 && t == 1.0);
    framePlace(NAN, 32, &j, &t);
    CHECK(j == 0 && t == 0.0);
    framePlace(0.5, 2, &j, &t);
    CHECK(j == 0 && t == 0.5);
    framePlace(0.5, 1, &j, &t); // a one-frame table is read as two (held to kFramesLeast)
    CHECK(j == 0 && t == 0.5);
    // Along the whole table the place read back is the place given.
    for (int frames : {2, 7, 32, 64}) {
        for (int k = 0; k <= 1000; ++k) {
            const double place = (frames - 1) * k / 1000.0;
            framePlace(place, frames, &j, &t);
            CHECK(j >= 0 && j <= frames - 2 && t >= 0.0 && t <= 1.0);
            CHECK(near(j + t, place, 1e-12));
        }
    }
}

// The phase: the step for a pitch, exact to a 2^32th of a turn; the point and the part read back.
static void testPhase() {
    CHECK(phaseStep(0.0, kRate) == 0 && phaseStep(-5.0, kRate) == 0 && phaseStep(NAN, kRate) == 0 && phaseStep(440, 0) == 0);
    CHECK(phaseStep(12000.0, kRate) == 1073741824u); // a quarter of the rate: a quarter turn
    CHECK(phaseStep(30000.0, kRate) == 2147483647u); // held under half a turn
    CHECK(near(phaseStep(440.0, kRate) / 4294967296.0 * kRate, 440.0, 1e-5));
    // A sample's step at 440 Hz, run a second: within a sample's 2^32th of 440 turns.
    std::uint32_t phase = 0;
    const std::uint32_t step = phaseStep(440.0, kRate);
    unsigned long long turns = 0;
    for (int s = 0; s < 48000; ++s) {
        const std::uint32_t before = phase;
        phase += step;
        if (phase < before) ++turns;
    }
    CHECK(turns == 440 || turns == 439);
    // The point and the part: put back together they are the phase, at every size.
    for (int m = 0; m < kSizes; ++m) {
        const int bits = sizeBits(m);
        for (std::uint32_t p : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x12345678u}) {
            const std::uint32_t i = readIndex(p, bits);
            const float f = readFraction(p, bits);
            CHECK(i < static_cast<std::uint32_t>(sizeLength(m)) && f >= 0.f && f < 1.f);
            CHECK(near((i + static_cast<double>(f)) / sizeLength(m), p / 4294967296.0, 1e-6));
        }
    }
    CHECK(readIndex(0x80000000u, 11) == 1024 && readFraction(0x80000000u, 11) == 0.f && readIndex(0xFFFFFFFFu, 7) == 127);
}

// The pool: the keys each UNISON leaves (the plan's section 3), never more than six, never none.
static void testPool() {
    CHECK(kStreams == 24 && kKeys == 6 && kKeySlots == 12);
    CHECK(streamsPerKey(1, false) == 1 && streamsPerKey(1, true) == 2 && streamsPerKey(4, true) == 8 && streamsPerKey(3, false) == 3);
    CHECK(streamsPerKey(0, true) == 2 && streamsPerKey(9, false) == 4);
    CHECK(keysFor(1, true) == 6 && keysFor(2, true) == 6 && keysFor(3, true) == 4 && keysFor(4, true) == 3);
    CHECK(keysFor(1, false) == 6 && keysFor(3, false) == 6 && keysFor(4, false) == 6);
    for (int u = 1; u <= kUnisonMost; ++u)
        for (int b = 0; b < 2; ++b) {
            const int keys = keysFor(u, b != 0);
            CHECK(keys >= 1 && keys <= kKeys && keys * streamsPerKey(u, b != 0) <= kStreams);
            if (keys < kKeys) CHECK((keys + 1) * streamsPerKey(u, b != 0) > kStreams); // as many as fit
        }
}

// UNISON's spread: even, the ends at +-DETUNE and +-WIDTH, none for one stream; the sides equal power.
static void testUnison() {
    CHECK(streamSpread(0, 1) == 0.0 && streamCents(0, 1, 30.0) == 0.0 && streamSide(0, 1, 1.0) == 0.0);
    CHECK(streamSpread(0, 2) == -1.0 && streamSpread(1, 2) == 1.0);
    CHECK(streamSpread(0, 3) == -1.0 && streamSpread(1, 3) == 0.0 && streamSpread(2, 3) == 1.0);
    CHECK(near(streamSpread(1, 4), -1.0 / 3.0, 1e-15) && near(streamSpread(2, 4), 1.0 / 3.0, 1e-15));
    CHECK(streamCents(0, 4, 50.0) == -50.0 && streamCents(3, 4, 50.0) == 50.0 && streamSide(3, 4, 0.5) == 0.5);
    for (int u = 2; u <= kUnisonMost; ++u) {
        double sum = 0;
        for (int i = 0; i < u; ++i) sum += streamCents(i, u, 20.0);
        CHECK(near(sum, 0.0, 1e-12)); // the mean pitch is the key's
    }
    double l = 0, r = 0;
    sideGains(0.0, &l, &r);
    CHECK(near(l, std::sqrt(0.5), 1e-12) && near(r, std::sqrt(0.5), 1e-12));
    sideGains(-1.0, &l, &r);
    CHECK(near(l, 1.0, 1e-12) && near(r, 0.0, 1e-12));
    sideGains(1.0, &l, &r);
    CHECK(near(l, 0.0, 1e-12) && near(r, 1.0, 1e-12));
    sideGains(-7.0, &l, &r);
    CHECK(near(l, 1.0, 1e-12));
    sideGains(NAN, &l, &r);
    CHECK(near(l, 1.0, 1e-12));
    for (int k = -10; k <= 10; ++k) {
        sideGains(k / 10.0, &l, &r);
        CHECK(near(l * l + r * r, 1.0, 1e-12));
    }
}

// The rolls: a stream alone at 0; streams together each their own start, the same for the same
// four numbers, spread over the turn.
static void testRolls() {
    for (int osc = 0; osc < 2; ++osc) CHECK(streamPhase(1234u, osc, 0, 1) == 0);
    CHECK(streamPhase(7, 0, 1, 3) == streamPhase(7, 0, 1, 3));
    CHECK(streamPhase(7, 0, 1, 3) != streamPhase(7, 0, 2, 3));
    CHECK(streamPhase(7, 0, 1, 3) != streamPhase(7, 1, 1, 3));
    CHECK(streamPhase(7, 0, 1, 3) != streamPhase(8, 0, 1, 3));
    CHECK(streamPhase(7, 0, 1, 2) == streamPhase(7, 0, 1, 4)); // a stream's start does not hang on how many there are
    CHECK(liveSeed(0) != liveSeed(1) && liveSeed(5) == liveSeed(5));
    int quarter[4] = {0, 0, 0, 0};
    double sum = 0;
    const int n = 40000;
    for (int k = 0; k < n; ++k) {
        const std::uint32_t p = streamPhase(liveSeed(static_cast<std::uint32_t>(k)), k & 1, k % 4, 4);
        ++quarter[p >> 30];
        sum += p / 4294967296.0;
    }
    for (int q = 0; q < 4; ++q) CHECK(std::abs(quarter[q] - n / 4) < n / 50);
    CHECK(near(sum / n, 0.5, 0.01));
    if (gPrint) std::printf("  unison starts over 40000 rolls: quarters %d %d %d %d, mean %.4f of a turn\n", quarter[0], quarter[1], quarter[2],
                            quarter[3], sum / n);
}

// ---- batch 1: the tables ---------------------------------------------------------------------------

// The sum of a frame's first `n` harmonics (as made) at a place x of the cycle, read back from size 0
// by a DFT: what the tests below hold the tables to.
static double partOf(const float* d, int length, int n, double* phase = nullptr) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < length; ++i) {
        const double w = 2.0 * 3.14159265358979323846 * n * i / length;
        re += d[i] * std::cos(w);
        im += d[i] * std::sin(w);
    }
    if (phase) *phase = std::atan2(im, re);
    return 2.0 * std::sqrt(re * re + im * im) / length;
}

// Every built-in table: 32 frames, each frame's top size peaking at 1, no DC, its guard points, each
// size holding its harmonics as made and none past them, and how far a crossfade half way dips.
static void testTables() {
    const double took = makeBuiltInTables();
    (void)took;
    for (int t = 0; t < kTables; ++t) {
        const Table& table = builtInTable(t);
        CHECK(table.frames == kTableFrames && table.data.size() == static_cast<std::size_t>(kTableFrames) * frameFloats());
        CHECK(&builtInTable(t) == &table); // made once
        for (int f = 0; f < table.frames; ++f) {
            float peak = 0.f;
            const float* top = table.at(f, 0);
            for (int i = 0; i < sizeLength(0); ++i) peak = std::max(peak, std::fabs(top[i]));
            CHECK(near(peak, 1.0, 1e-6));
            for (int m = 0; m < kSizes; ++m) {
                const float* d = table.at(f, m);
                const int length = sizeLength(m);
                double mean = 0.0;
                for (int i = 0; i < length; ++i) mean += d[i];
                CHECK(std::fabs(mean / length) < 1e-5);
                CHECK(d[-1] == d[length - 1] && d[length] == d[0] && d[length + 1] == d[1]);
                CHECK(std::isfinite(d[0]));
            }
        }
        // A frame in the middle: each size's harmonics as made, up to its top, and nothing past it.
        const int f = 13;
        for (int m : {0, 4, 9, 15}) {
            const float* d = table.at(f, m);
            const int length = sizeLength(m), h = sizeHarmonics(m);
            for (int n : {1, 2, h / 2 > 0 ? h / 2 : 1, h}) CHECK(near(partOf(d, length, n), table.harmonic(f, n), 2e-5));
            for (int n : {h + 1, h + 2, std::min(2 * h, length / 2 - 1)})
                if (n > h && n < length / 2) CHECK(partOf(d, length, n) < 2e-5);
        }
    }
    // The table's own sine is the library's to 2e-9 (so the tables need no library call).
    double worst = 0.0;
    for (int k = -4000; k <= 4000; ++k) {
        const double x = k * 0.000731;
        worst = std::max(worst, std::fabs(turnSin(x) - std::sin(2.0 * 3.14159265358979323846 * x)));
        worst = std::max(worst, std::fabs(turnCos(x) - std::cos(2.0 * 3.14159265358979323846 * x)));
    }
    CHECK(worst < 1e-8);
    if (gPrint) std::printf("  the tables' own sine and cosine against the library's: %.2g at worst\n", worst);
    // Half way between two frames the crossfade dips at most 2 dB under the two ends' mean level;
    // HARMONIC's frames share no harmonic (one sine leaving, the next arriving), so it dips 3.
    for (int t = 0; t < kTables; ++t) {
        const Table& table = builtInTable(t);
        double dip = 0.0;
        for (int f = 0; f + 1 < table.frames; ++f) {
            const float* a = table.at(f, 0);
            const float* b = table.at(f + 1, 0);
            double ra = 0, rb = 0, rm = 0;
            for (int i = 0; i < sizeLength(0); ++i) {
                ra += a[i] * a[i], rb += b[i] * b[i];
                const double mid = 0.5 * (a[i] + b[i]);
                rm += mid * mid;
            }
            dip = std::max(dip, 10.0 * std::log10(0.5 * (ra + rb) / rm));
        }
        CHECK(dip <= (t == Harmonic ? 3.02 : 2.0));
        if (gPrint) std::printf("  %-9s the worst dip half way between two frames %.2f dB\n", tableName(t), dip);
    }
    if (gPrint) std::printf("  the 16 built-in tables were made in %.0f ms (desk)\n", 1000.0 * Morph::tablesSeconds());
}

// A table made from harmonics given: a saw's first 512, every size the sum of its own share.
static void testTableFromHarmonics() {
    std::vector<double> c(2 * (kTopHarmonics + 1), 0.0), s(c.size(), 0.0);
    for (int n = 1; n <= kTopHarmonics; ++n) s[n] = 1.0 / n, s[kTopHarmonics + 1 + n] = (n == 3 ? 1.0 : 0.0);
    const Table table = tableFromHarmonics(2, c, s);
    CHECK(table.frames == 2);
    // Frame 2 is one sine at the third harmonic: every size that holds it is that sine, peak 1.
    for (int m = 0; m < kSizes; ++m) {
        const float* d = table.at(1, m);
        const int length = sizeLength(m);
        double worst = 0.0;
        for (int i = 0; i < length; ++i)
            worst = std::max(worst, std::fabs(d[i] - (sizeHarmonics(m) >= 3 ? std::sin(2.0 * 3.14159265358979323846 * 3 * i / length) : 0.0)));
        CHECK(worst < 2e-6);
    }
    // Frame 1, a saw: its fundamental's phase is the sine's, and its third is a third of it.
    double phase = 0.0;
    const double one = partOf(table.at(0, 0), sizeLength(0), 1, &phase);
    CHECK(near(phase, 3.14159265358979323846 / 2, 1e-6)); // a sine: its part against the cosine is a quarter turn on
    CHECK(near(partOf(table.at(0, 0), sizeLength(0), 3) / one, 1.0 / 3.0, 1e-6));
    CHECK(near(table.harmonic(0, 3) / table.harmonic(0, 1), 1.0 / 3.0, 1e-6));
}

// ---- batch 1: the voice -------------------------------------------------------------------------------

struct Set {
    int id, value;
};
static std::unique_ptr<Morph> synth(std::initializer_list<Set> sets, int preset = 0) {
    std::unique_ptr<Morph> m(new Morph);
    m->prepare(static_cast<float>(kRate));
    m->selectPreset(preset);
    for (const Set& x : sets) m->setSetting(x.id, x.value);
    return m;
}
// A plain voice: no filter, no MOD ENV on POSITION, the AMP at full, LEVEL 0, VELOCITY OFF.
static std::unique_ptr<Morph> plain(std::initializer_list<Set> more) {
    std::unique_ptr<Morph> m = synth({{Filter, kFilterOff}, {EnvPosition, 0}, {EnvCutoff, 0}, {Attack, 0}, {Sustain, 100}, {Velocity, 0}, {Level, 0}});
    for (const Set& x : more) m->setSetting(x.id, x.value);
    return m;
}
// Render `frames` into one side (left) and the other.
static std::vector<float> run(Morph& m, long frames, std::vector<float>* right = nullptr, int block = 256) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    for (long at = 0; at < frames;) {
        const int n = static_cast<int>(std::min<long>(block, frames - at));
        m.render(l.data() + at, r.data() + at, n);
        at += n;
    }
    if (right) *right = r;
    return l;
}
static double keyHzOf(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }
static bool allFinite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

// PITCH: a sine frame (ANALOG's first) sings the key, keys 24 to 96; B a fifth and an octave over A;
// OCTAVE and TUNE.
static void testPitch() {
    double worst = 0.0;
    for (int key = 24; key <= 96; key += 6) {
        std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}});
        m->noteOn(key, 1.f);
        const std::vector<float> v = run(*m, 3 * 48000); // (60 cycles of key 24 are nearly two seconds)
        const double off = centsOff(v, keyHzOf(key), 0.1);
        worst = std::max(worst, std::fabs(off));
    }
    CHECK(worst < 0.05);
    struct Case { std::initializer_list<Set> sets; double cents; };
    const Case cases[] = {{{{TableB, 1 + Analog}, {Mix, 100}, {BSemi, 7}}, 700.0},
                          {{{TableB, 1 + Analog}, {Mix, 100}, {BSemi, 12}, {BFine, -30}}, 1170.0},
                          {{{Octave, -1}}, -1200.0},
                          {{{Tune, 37}}, 37.0}};
    for (const Case& c : cases) {
        std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}});
        for (const Set& x : c.sets) m->setSetting(x.id, x.value);
        m->noteOn(57, 1.f);
        const std::vector<float> v = run(*m, 48000);
        const double hz = 220.0 * std::pow(2.0, c.cents / 1200.0);
        CHECK(std::fabs(centsOff(v, hz, 0.1)) < 0.05 && sizeAt(v, hz, 0.2) > 0.1);
    }
    if (gPrint) std::printf("  a sine frame keys 24 to 96: %.4f cents off at worst\n", worst);
}

// A FRAME AS MADE: its first ten harmonics' sizes as the table holds them, read off the sound; and
// a place between two frames is the straight crossfade of the two, sample for sample.
static void testFramesAndMorph() {
    const int key = 48;
    const double hz = keyHzOf(key);
    for (int t : {Analog, Vowel, Fm, Organ}) {
        std::unique_ptr<Morph> m = plain({{TableA, t}, {PositionA, 100}});
        m->noteOn(key, 1.f);
        const std::vector<float> v = run(*m, 48000);
        const Table& table = builtInTable(t);
        const double one = sizeAt(v, hz, 0.2, 64), made = table.harmonic(table.frames - 1, 1);
        for (int n = 2; n <= 10; ++n) {
            const double want = table.harmonic(table.frames - 1, n) / made;
            if (want < 1e-3) continue;
            CHECK(std::fabs(dB(sizeAt(v, n * hz, 0.2, 64) / one) - dB(want)) < 0.1);
        }
    }
    // The macro sets POSITION exactly for the place p: d = 100 (2m - 1), place = d / 100 x 31.
    auto at = [&](double place) {
        std::unique_ptr<Morph> m = plain({{TableA, Vowel}, {PositionA, 0}, {PositionB, 0}});
        m->setMacro(MacroPosition, static_cast<float>((place / 31.0 + 1.0) / 2.0));
        m->noteOn(key, 1.f);
        return run(*m, 9600);
    };
    const std::vector<float> a = at(10.0), b = at(11.0), mid = at(10.25);
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::fabs(mid[i] - (0.75 * a[i] + 0.25 * b[i])));
    CHECK(worst < 2e-4);
    if (gPrint) std::printf("  a place a quarter between two frames: %.2g from the straight crossfade at worst\n", worst);
}

// ALIASING: every table at its middle and last frame, keys 36 to 96, a sixteenth of a second: the
// loudest part that is no harmonic, under 10 kHz, against the fundamental.
static void testAliasing() {
    double worst = -600.0;
    int worstTable = -1, worstKey = 0;
    for (int t = 0; t < kTables; ++t)
        for (int pos : {50, 100})
            for (int key : {36, 60, 84, 96}) {
                std::unique_ptr<Morph> m = plain({{TableA, t}, {PositionA, pos}});
                m->noteOn(key, 1.f);
                std::vector<float> v = run(*m, 4800 + 32768);
                v.erase(v.begin(), v.begin() + 4800);
                // The reference: the loudest harmonic under 20 kHz (a frame may have little fundamental).
                const double hz = keyHzOf(key);
                double ref = 0.0, refHz = hz;
                for (int n = 1; n * hz < 20000.0; ++n)
                    if (n * hz < 20000.0 && partNear(v.data(), v.size(), n * hz) > ref) ref = partNear(v.data(), v.size(), n * hz), refHz = n * hz;
                const Aside a = asideOf(v.data(), v.size(), hz, 10000.0, refHz);
                if (a.dB > worst) worst = a.dB, worstTable = t, worstKey = key;
            }
    CHECK(worst < -55.0); // (at 8 points a harmonic it was -39 at FOLD's key 96: morph_rule.hpp's sizes)
    if (gPrint) std::printf("  aliasing under 10 kHz, every table, keys 36 to 96: %.1f dB at worst (%s, key %d)\n", worst, tableName(worstTable), worstKey);
}

// THE FILTER: LOW 12 and LOW 24 take a saw's harmonics two octaves over CUTOFF down by about 24 and
// 48 dB, HIGH takes those under it away, BAND both; KEY TRACK moves the cutoff with the key.
static void testFilter() {
    const int key = 36; // 65.4 Hz
    const double hz = keyHzOf(key);
    auto play = [&](int filter, int cutoff, int track, int k) {
        std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 50}, {Filter, filter}, {Cutoff, cutoff}, {KeyTrack, track}, {Resonance, 0}});
        m->noteOn(k, 1.f);
        return run(*m, 48000);
    };
    // CUTOFF 57 is 1047 Hz, the 16th harmonic.
    const std::vector<float> open = play(kFilterOff, 57, 0, key), low = play(kLow12, 57, 0, key), low24 = play(kLow24, 57, 0, key), high = play(kHigh, 57, 0, key),
                             band = play(kBand, 57, 0, key);
    auto drop = [&](const std::vector<float>& v, int n) { return dB(sizeAt(v, n * hz, 0.3, 64) / sizeAt(open, n * hz, 0.3, 64)); };
    CHECK(drop(low, 2) > -0.5 && drop(low, 64) < -20.0 && drop(low, 64) > -28.0);
    CHECK(drop(low24, 2) > -0.5 && drop(low24, 64) < -42.0);
    CHECK(drop(high, 64) > -0.5 && drop(high, 2) < -30.0); // three octaves under: 12 dB an octave
    CHECK(drop(band, 2) < -12.0 && drop(band, 64) < -9.0 && drop(band, 16) > -7.0); // its middle at 1 / k: -6 dB at Q 0.5
    // KEY TRACK 100: an octave up, the cutoff an octave up, so the harmonic at the cutoff loses as much.
    const std::vector<float> mid = play(kLow12, 57, 100, 60), midOpen = play(kFilterOff, 57, 100, 60);
    const std::vector<float> up = play(kLow12, 57, 100, 72), upOpen = play(kFilterOff, 57, 100, 72);
    const double here = dB(sizeAt(mid, 4 * keyHzOf(60), 0.3, 64) / sizeAt(midOpen, 4 * keyHzOf(60), 0.3, 64));
    const double there = dB(sizeAt(up, 4 * keyHzOf(72), 0.3, 64) / sizeAt(upOpen, 4 * keyHzOf(72), 0.3, 64));
    CHECK(std::fabs(here - there) < 0.3 && here < -1.0);
    if (gPrint)
        std::printf("  a saw at key 36, CUTOFF 1047 Hz: LOW 12 4 kHz %.1f dB, LOW 24 %.1f dB; HIGH 131 Hz %.1f dB; BAND %.1f / %.1f / %.1f dB;"
                    " KEY TRACK %.2f against %.2f dB\n", drop(low, 64), drop(low24, 64), drop(high, 2), drop(band, 2), drop(band, 16), drop(band, 64), here, there);
}

// THE LEVELS AND THE SIDES: SUB is a sine an octave under A at its share; LEVEL -6 dB halves the
// sound; VELOCITY 100 sets a key's level by its velocity, OFF does not; WIDTH puts UNISON's streams
// apart, the lowest on the left, and at 0 both sides are one.
static void testLevelsAndSides() {
    const int key = 57; // 220 Hz
    auto play = [&](std::initializer_list<Set> sets, float velocity, std::vector<float>* right = nullptr) {
        std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}});
        for (const Set& x : sets) m->setSetting(x.id, x.value);
        m->noteOn(key, velocity);
        return run(*m, 48000, right);
    };
    const std::vector<float> base = play({}, 1.f), sub = play({{Sub, 50}}, 1.f), quiet = play({{Level, -6}}, 1.f);
    CHECK(sizeAt(base, 110.0, 0.2) < 1e-4 && near(sizeAt(sub, 110.0, 0.2) / sizeAt(sub, 220.0, 0.2), 0.5, 0.01));
    CHECK(near(sizeAt(quiet, 220.0, 0.2) / sizeAt(base, 220.0, 0.2), std::pow(10.0, -6.0 / 20.0), 1e-3));
    const std::vector<float> soft = play({{Velocity, 100}}, 0.5f), plainSoft = play({}, 0.5f);
    CHECK(near(sizeAt(soft, 220.0, 0.2) / sizeAt(base, 220.0, 0.2), 0.5, 1e-3) && near(sizeAt(plainSoft, 220.0, 0.2) / sizeAt(base, 220.0, 0.2), 1.0, 1e-3));
    // UNISON 2, DETUNE 100 (+-50 cents), WIDTH 100: the stream 50 cents flat is on the left alone.
    std::vector<float> right, rightNarrow;
    const std::vector<float> wide = play({{Unison, 2}, {Detune, 100}, {Width, 100}}, 1.f, &right);
    const double flat = 220.0 * std::pow(2.0, -50.0 / 1200.0), sharp = 220.0 * std::pow(2.0, 50.0 / 1200.0);
    CHECK(dB(sizeAt(wide, flat, 0.2, 128) / sizeAt(wide, sharp, 0.2, 128)) > 40.0);
    CHECK(dB(sizeAt(right, sharp, 0.2, 128) / sizeAt(right, flat, 0.2, 128)) > 40.0);
    const std::vector<float> narrow = play({{Unison, 2}, {Detune, 100}, {Width, 0}}, 1.f, &rightNarrow);
    CHECK(narrow == rightNarrow);
}

// GLIDE: a key slides from the key named in the time given, then sings its own.
static void testGlide() {
    std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}});
    m->noteOnFrom(60, 1.f, 48, 0.5f);
    const std::vector<float> v = run(*m, 48000);
    // Its pitch by the rising zero crossings about 0.1 s: a fifth of the way, 50.4, give or take.
    auto hzAbout = [&](double seconds) {
        const long from = static_cast<long>((seconds - 0.01) * kRate), to = static_cast<long>((seconds + 0.01) * kRate);
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (long i = from; i < to; ++i)
            if (v[i - 1] < 0.f && v[i] >= 0.f) {
                const double at = (i - 1) + v[i - 1] / (v[i - 1] - v[i]);
                if (first < 0.0) first = at;
                else ++crossings;
                last = at;
            }
        return crossings * kRate / (last - first);
    };
    const double early = 12.0 * std::log2(hzAbout(0.1) / 440.0) + 69.0;
    CHECK(std::fabs(early - 50.4) < 0.3);
    if (gPrint) std::printf("  a glide 48 to 60 in 0.5 s: key %.2f at 0.1 s (50.4 a straight slide)\n", early);
    CHECK(std::fabs(centsOff(v, keyHzOf(60), 0.6)) < 0.05);
    const double middle = centsOff(v, keyHzOf(54), 0.24, 4, 4);
    CHECK(std::fabs(middle) < 60.0);
}

// THE MOD ENV moves POSITION (the picture's readout) and a MOD on POSITION is a line.
static void testPositionMoves() {
    std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 10}, {EnvPosition, 60}, {EnvAttack, 0}, {EnvDecay, 50}, {EnvSustain, 0}});
    float places[2 * kKeys];
    m->noteOn(60, 1.f);
    run(*m, 1);
    CHECK(m->grainPlaces(places, 2 * kKeys) == 2 && places[1] == -1.f);
    CHECK(places[0] > 0.1f); // from its first sample where it is going: never slid in from frame 1
    run(*m, 63);
    m->grainPlaces(places, 2 * kKeys);
    const float early = places[0];
    run(*m, 48000);
    m->grainPlaces(places, 2 * kKeys);
    CHECK(early > 0.6f && near(places[0], 0.1, 1e-4)); // up near 10% + 60%, then back to 10%
    // A macro turned from 0.5 to 1 while a key sounds: the place goes up a line over 512 samples.
    std::unique_ptr<Morph> n = plain({{TableA, Analog}, {PositionA, 20}, {TableB, 1 + Vowel}, {PositionB, 50}});
    n->noteOn(60, 1.f);
    run(*n, 512, nullptr, 1);
    n->grainPlaces(places, 2 * kKeys);
    CHECK(near(places[0], 0.2, 1e-5) && near(places[1], 0.5, 1e-5));
    n->setMacro(MacroPosition, 1.f);
    float last = places[0], biggest = 0.f;
    for (int s = 0; s < 700; ++s) {
        run(*n, 1, nullptr, 1);
        n->grainPlaces(places, 2 * kKeys);
        CHECK(places[0] >= last - 1e-6f);
        biggest = std::max(biggest, places[0] - last);
        last = places[0];
    }
    CHECK(near(last, 1.0, 1e-5) && near(places[1], 1.0, 1e-5)); // both A and B at the top
    CHECK(biggest < 0.8f / 512.f * 1.05f);
    if (gPrint) std::printf("  POSITION turned 20%% to 100%%: the most it moved a sample %.6f (a line: %.6f)\n", biggest, 0.8 / 512);
}

// THE POOL: UNISON 4 with B takes 8 streams, so three keys sound; a fourth steals the oldest. Never
// more than 24 streams, never more than six keys.
static void testStreamPool() {
    std::unique_ptr<Morph> m = plain({{TableA, Analog}, {TableB, 1 + Pulse}, {Unison, 4}});
    for (int k = 0; k < 5; ++k) {
        m->noteOn(48 + k, 1.f);
        CHECK(m->streamsInUse() <= kStreams);
        run(*m, 64);
    }
    CHECK(m->streamsInUse() == 24);
    run(*m, 600);
    CHECK(m->keysSounding() == 3);
    std::unique_ptr<Morph> n = plain({{TableA, Analog}});
    for (int k = 0; k < 9; ++k) n->noteOn(48 + k, 1.f), run(*n, 64);
    run(*n, 600);
    CHECK(n->keysSounding() == 6 && n->streamsInUse() == 6);
    // A let-go key goes before a held one.
    std::unique_ptr<Morph> p = plain({{TableA, Analog}, {Release, 100}});
    for (int k = 0; k < 6; ++k) p->noteOn(60 + k, 1.f);
    run(*p, 480); // (let go before it sounds, a key has nothing to release)
    p->noteOff(63);
    run(*p, 64);
    CHECK(p->sounds(63) && p->keysSounding() == 6); // let go, still sounding its release
    p->noteOn(70, 1.f);
    run(*p, 600);
    CHECK(p->keysSounding() == 6 && !p->sounds(63) && p->sounds(60) && p->sounds(70));
    // With none let go, the oldest.
    p->noteOn(71, 1.f);
    run(*p, 600);
    CHECK(!p->sounds(60) && p->sounds(61) && p->sounds(71));
    float places[2 * kKeys];
    CHECK(p->grainPlaces(places, 2 * kKeys) == 12);
    // Keys of other UNISONs share the pool: two at UNISON 1 with B (2 streams each), then UNISON 4
    // with B (8 each).
    std::unique_ptr<Morph> q = plain({{TableA, Analog}, {TableB, 1 + Pulse}});
    q->noteOn(40, 1.f), q->noteOn(41, 1.f);
    q->setSetting(Unison, 4);
    for (int k = 0; k < 3; ++k) {
        q->noteOn(50 + k, 1.f);
        CHECK(q->streamsInUse() <= kStreams);
    }
    // (the third: 20 + 8 is over 24, so 40 goes; 18 + 8 still is, so 41 goes too)
    CHECK(q->streamsInUse() == 24 && !q->sounds(40) && !q->sounds(41) && q->sounds(50) && q->sounds(51) && q->sounds(52));
    // A key struck again: the old one fades, one sounds.
    std::unique_ptr<Morph> r = plain({{TableA, Analog}});
    r->noteOn(60, 1.f);
    run(*r, 480);
    r->noteOn(60, 1.f);
    run(*r, 480);
    CHECK(r->keysSounding() == 1);
}

// NO CLICK at a steal, at a key struck again, or at allNotesOff: no sample steps further than the
// sines sounding could (each key's 2 pi f / rate times its size).
static void testNoClick() {
    std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}});
    double most = 0.0;
    for (int k = 0; k < 6; ++k) {
        m->noteOn(60 + 2 * k, 1.f);
        most += kOutputGain * 2.0 * 3.14159265358979323846 * keyHzOf(60 + 2 * k) / kRate;
    }
    run(*m, 2000);
    m->noteOn(80, 1.f); // a steal
    std::vector<float> v = run(*m, 2000);
    m->noteOn(62, 1.f); // a key struck again
    std::vector<float> w = run(*m, 2000);
    m->allNotesOff();
    std::vector<float> x = run(*m, 2000);
    const double bound = 1.2 * (most + kOutputGain * 2.0 * 3.14159265358979323846 * keyHzOf(80) / kRate);
    CHECK(largestJump(v) < bound && largestJump(w) < bound && largestJump(x) < bound);
    CHECK(m->keysSounding() == 0 && x.back() == 0.f);
    if (gPrint) std::printf("  the largest step at a steal %.4f, struck again %.4f, allNotesOff %.4f (a sine could %.4f)\n", largestJump(v), largestJump(w),
                            largestJump(x), bound / 1.2);
}

// BLOCKS: the same sound to the bit in blocks of 1, 7, 64, 256 and 333, with everything moving: UNISON
// and WIDTH, B, LOW 24, the MOD ENV on POSITION and CUTOFF, a glide, a macro turned, a steal, a key
// let go, a setting turned. What is done between blocks is done at the same samples every time.
static std::vector<float> busySong(int block) {
    std::unique_ptr<Morph> m = synth({{TableA, Fm}, {TableB, 1 + Grit}, {Mix, 40}, {Unison, 3}, {Detune, 40}, {Width, 80}, {Filter, kLow24}, {Cutoff, 60},
                                      {Resonance, 50}, {EnvPosition, 50}, {EnvCutoff, 40}, {Sub, 30}, {Release, 40}});
    struct Event { long at; int what, a; float b; };
    const Event events[] = {{0, 0, 48, 0.f},     {700, 0, 55, 0.f},    {1500, 1, 60, 0.3f}, {2048, 2, MacroPosition, 0.9f}, {3001, 0, 64, 0.f},
                            {4096, 3, 55, 0.f},  {5000, 2, MacroCutoff, 0.2f}, {6100, 4, Detune, 70.f}, {7000, 0, 67, 0.f}, {7300, 0, 69, 0.f},
                            {9000, 5, 0, 0.f},   {12000, 0, 50, 0.f}};
    std::vector<float> l(24000), r(24000);
    long at = 0;
    for (const Event& e : events) {
        while (at < e.at) {
            const int n = static_cast<int>(std::min<long>(block, e.at - at));
            m->render(l.data() + at, r.data() + at, n);
            at += n;
        }
        if (e.what == 0) m->noteOn(e.a, 0.9f);
        if (e.what == 1) m->noteOnFrom(e.a, 0.8f, 48, e.b);
        if (e.what == 2) m->setMacro(e.a, e.b);
        if (e.what == 3) m->noteOff(e.a);
        if (e.what == 4) m->setSetting(e.a, static_cast<int>(e.b));
        if (e.what == 5) m->stopNotes();
    }
    while (at < 24000) {
        const int n = static_cast<int>(std::min<long>(block, 24000 - at));
        m->render(l.data() + at, r.data() + at, n);
        at += n;
    }
    l.insert(l.end(), r.begin(), r.end());
    return l;
}
static void testBlocks() {
    const std::vector<float> first = busySong(256);
    double power = 0.0;
    for (float v : first) power += static_cast<double>(v) * v;
    CHECK(allFinite(first) && power / first.size() > 1e-4);
    for (int block : {1, 7, 64, 333}) CHECK(busySong(block) == first);
}

// THROUGH THE ENGINE: notes on the step clock on three MORPH tracks, the same to the bit with 0 to 3
// helpers and in other blocks; Play from the top the same; the picture's readout kept by the engine.
static Command noteAt(int track, int slot, int stepIndex, int pitch, float length, int effects = 0) {
    Command c;
    c.type = Command::SetNote;
    c.track = track;
    c.a = slot;
    c.b = stepIndex;
    c.value = 0.9f;
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
        CHECK(engine->addSynthTrack(t, SynthKind::Morph));
        Command pick;
        pick.type = Command::SelectPreset, pick.track = t, pick.a = t;
        engine->send(pick);
        Command set;
        set.type = Command::SetSetting, set.track = t, set.a = Unison, set.b = t;
        engine->send(set);
        for (int step = 0; step < 16; step += 2) engine->send(noteAt(t, step, step, 48 + (step * 5 + t * 7) % 24, 1.5f, step % 4 == 2 ? 256 : 0));
    }
    Command p;
    p.type = Command::Play;
    engine->send(p);
    const long frames = 16 * 6000L;
    std::vector<float> out(static_cast<std::size_t>(2 * frames));
    for (int round = 0; round < (twice ? 2 : 1); ++round) {
        if (round == 1) {
            Command stop;
            stop.type = Command::Stop;
            engine->send(stop);
            std::vector<float> rest(2 * 48000);
            for (int i = 0; i < 4; ++i) engine->render(rest.data(), 12000);
            engine->send(p);
        }
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
    CHECK(engineSong(0, 256, true) == first); // Play from the top: the same sound
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(4, SynthKind::Morph) && engine->addSynthTrack(5, SynthKind::Sampler));
    Command c;
    c.type = Command::NoteOn, c.track = 4, c.a = 60, c.value = 0.9f;
    engine->send(c);
    std::vector<float> out(2 * 4800);
    engine->render(out.data(), 4800);
    CHECK(dynamic_cast<const Morph*>(engine->synthForTests(4)) != nullptr);
    float places[kGrainsShown];
    CHECK(engine->grainPlaces(4, places, kGrainsShown) == 2 && places[0] >= 0.f && places[0] <= 1.f && places[1] == -1.f);
    CHECK(engine->grainPlaces(5, places, kGrainsShown) == -1);
}

// NO ALLOCATION on the audio thread.
static void testNoAllocation() {
    std::unique_ptr<Morph> m = synth({{TableB, 1 + Bell}, {Unison, 4}, {Width, 50}, {Filter, kLow24}});
    std::vector<float> l(256), r(256);
    const long before = gNews;
    for (int k = 0; k < 8; ++k) m->noteOn(48 + k, 0.9f);
    for (int b = 0; b < 40; ++b) m->render(l.data(), r.data(), 256);
    m->setMacro(MacroPosition, 0.9f);
    m->setSetting(TableA, Glass);
    m->noteOnFrom(70, 1.f, 50, 0.05f);
    m->render(l.data(), r.data(), 256);
    m->noteOff(70);
    m->stopNotes();
    m->allNotesOff();
    m->selectPreset(3);
    m->noteOn(60, 0.5f);
    float places[2 * kKeys];
    m->grainPlaces(places, 2 * kKeys);
    for (int b = 0; b < 40; ++b) m->render(l.data(), r.data(), 256);
    CHECK(gNews == before);
}

// NOTHING TOO SMALL TO HOLD: a key held a minute on a decay to nothing (both envelopes' SUSTAIN 0,
// RESONANCE up), then let go: every state is 0 or over 1e-30, and then exact zeros.
static void testTooSmall() {
    std::unique_ptr<Morph> m = synth({{Sustain, 0}, {Decay, 40}, {EnvSustain, 0}, {EnvDecay, 30}, {EnvCutoff, 60}, {Filter, kLow24}, {Resonance, 90},
                                      {Unison, 2}, {Width, 60}});
    m->noteOn(40, 1.f);
    float least = 1.f;
    std::vector<float> l(256), r(256);
    for (int b = 0; b < 60 * 48000 / 256; ++b) {
        m->render(l.data(), r.data(), 256);
        const float s = m->smallestState();
        if (s > 0.f) least = std::min(least, s);
    }
    m->noteOff(40);
    for (int b = 0; b < 30 * 48000 / 256; ++b) {
        m->render(l.data(), r.data(), 256);
        const float s = m->smallestState();
        if (s > 0.f) least = std::min(least, s);
    }
    CHECK(least > 1e-30f);
    CHECK(m->keysSounding() == 0 && l[255] == 0.f && r[255] == 0.f);
    if (gPrint) std::printf("  a minute held on a decay to nothing, then let go: the least state held %.2g\n", least);
}

// NOT-A-NUMBER and nonsense: held or passed by, never a stuck key or a sound that is not a number.
static void testNaN() {
    std::unique_ptr<Morph> m = synth({});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    m->setMacro(MacroCutoff, nan);
    m->setMacro(-1, 0.3f);
    m->setMacro(99, 0.3f);
    m->setSetting(PositionA, 100000);
    m->setSetting(-3, 5);
    m->noteOn(60, nan);
    m->noteOn(200, 1.f);
    CHECK(m->keysSounding() == 0);
    m->noteOnFrom(60, 1.f, 48, nan);
    m->noteOnFrom(62, 1.f, 48, 1e30f);
    std::vector<float> v = run(*m, 4800);
    CHECK(allFinite(v) && m->keysSounding() == 2);
}

// SILENCE: a synth with no key gives exact zeros; a key let go ends in exact zeros.
static void testSilence() {
    std::unique_ptr<Morph> m = synth({});
    std::vector<float> r;
    std::vector<float> v = run(*m, 4800, &r);
    for (std::size_t i = 0; i < v.size(); ++i) CHECK(v[i] == 0.f && r[i] == 0.f);
    m->noteOn(60, 1.f);
    run(*m, 4800);
    m->noteOff(60);
    v = run(*m, 5 * 48000, &r);
    CHECK(m->keysSounding() == 0 && v.back() == 0.f && r.back() == 0.f);
}

// PLAY FROM THE TOP: after allNotesOff (and its 5 ms) the same keys with the same seeds are the same
// sound to the bit as on a fresh synth: unison streams start where their rolls put them.
static void testPlayFromTheTop() {
    auto phrase = [](Morph& m) {
        std::vector<float> out;
        for (int k = 0; k < 4; ++k) {
            m.noteSeed(1000u + k);
            m.noteOn(50 + 3 * k, 0.8f);
            const std::vector<float> v = run(m, 3000);
            out.insert(out.end(), v.begin(), v.end());
        }
        return out;
    };
    std::unique_ptr<Morph> fresh = synth({{Unison, 3}, {Width, 70}, {TableB, 1 + Sync}});
    const std::vector<float> once = phrase(*fresh);
    std::unique_ptr<Morph> again = synth({{Unison, 3}, {Width, 70}, {TableB, 1 + Sync}});
    phrase(*again);
    again->allNotesOff();
    run(*again, 480);
    CHECK(phrase(*again) == once);
    // One stream alone starts at 0 (a sine frame's first sample is 0); streams together where rolled.
    for (int u : {1, 2, 3}) {
        std::unique_ptr<Morph> m = plain({{TableA, Analog}, {PositionA, 0}, {Unison, u}, {Detune, 0}, {Width, 0}});
        m->noteSeed(99u);
        m->noteOn(60, 1.f);
        const std::vector<float> v = run(*m, 1);
        CHECK(u == 1 ? std::fabs(v[0]) < 1e-6f : std::fabs(v[0]) > 1e-4f);
    }
    // Another seed is another start (the rolls are there), and UNISON 1 has none.
    std::unique_ptr<Morph> other = synth({{Unison, 3}, {Width, 70}, {TableB, 1 + Sync}});
    other->noteSeed(7u);
    other->noteOn(50, 0.8f);
    CHECK(run(*other, 3000) != std::vector<float>(once.begin(), once.begin() + 3000));
}

// THE PRESETS: each plays a chord of four, not too quiet or too loud, nothing past full, and ends.
static void testPresets() {
    CHECK(Morph::presetCount() == 8 && std::string(Morph::presetName(0)) == "Analog Sweep" && std::string(Morph::presetCategory(0)) == "BASIC");
    for (int p = 0; p < Morph::presetCount(); ++p) {
        std::unique_ptr<Morph> m = synth({}, p);
        for (int k : {48, 52, 55, 60}) m->noteOn(k, 0.9f);
        std::vector<float> r;
        const std::vector<float> v = run(*m, 48000, &r);
        const double level = dB(rmsOf(v));
        CHECK(peakOf(v) < 1.f && peakOf(r) < 1.f && level > -35.0 && level < -6.0);
        for (int k : {48, 52, 55, 60}) m->noteOff(k);
        run(*m, 10 * 48000);
        CHECK(m->keysSounding() == 0);
        if (gPrint) std::printf("  %-14s a chord of four: %.1f dB, peak %.2f\n", Morph::presetName(p), level, peakOf(v));
    }
    // A preset puts the macros back in the middle.
    std::unique_ptr<Morph> m = synth({});
    m->setMacro(MacroPosition, 1.f);
    m->selectPreset(0);
    m->setSetting(EnvPosition, 0);
    m->noteOn(60, 1.f);
    run(*m, 64);
    float places[2 * kKeys];
    m->grainPlaces(places, 2 * kKeys);
    CHECK(near(places[0], 0.3, 1e-5)); // Analog Sweep's POSITION 30, the macro at the middle
}

// ---- the cost (morph_test --measure) ---------------------------------------------------------------
static double costOf(std::initializer_list<Set> sets, int keys, bool asleep = false) {
    std::unique_ptr<Morph> m = synth(sets);
    for (int k = 0; k < keys; ++k) m->noteOn(48 + 3 * k, 0.9f);
    if (asleep) {
        for (int k = 0; k < keys; ++k) m->noteOff(48 + 3 * k);
        run(*m, 10 * 48000);
    }
    std::vector<float> l(256), r(256);
    double best = 1e9;
    for (int round = 0; round < 15; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 375; ++b) m->render(l.data(), r.data(), 256); // 2 s
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return 100.0 * best / 2.0;
}
static void measureCost() {
    std::printf("  the cost, one track, the best of 15 runs of 2 s (a share of one desk core):\n");
    std::printf("    6 keys, A and B, SUB, LOW 12, MOD ENV on POSITION: %.3f%%\n",
                costOf({{TableB, 1 + Fm}, {Sub, 30}, {EnvPosition, 50}}, 6));
    std::printf("    6 keys, A alone, LOW 12:                           %.3f%%\n", costOf({}, 6));
    std::printf("    the pool full (UNISON 2, A and B, WIDTH, LOW 24):  %.3f%%\n",
                costOf({{TableB, 1 + Fm}, {Unison, 2}, {Width, 60}, {Filter, kLow24}, {EnvPosition, 50}}, 6));
    std::printf("    the pool full (UNISON 4, A and B: 3 keys):         %.3f%%\n", costOf({{TableB, 1 + Fm}, {Unison, 4}, {Width, 60}, {Filter, kLow24}}, 3));
    std::printf("    asleep:                                            %.3f%%\n", costOf({}, 6, true));
}

struct Named { const char* name; void (*run)(); };

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testSettings", testSettings},
        {"testMacros", testMacros},
        {"testReadings", testReadings},
        {"testSizes", testSizes},
        {"testFramePlace", testFramePlace},
        {"testPhase", testPhase},
        {"testPool", testPool},
        {"testUnison", testUnison},
        {"testRolls", testRolls},
        {"testTables", testTables},
        {"testTableFromHarmonics", testTableFromHarmonics},
        {"testPitch", testPitch},
        {"testFramesAndMorph", testFramesAndMorph},
        {"testAliasing", testAliasing},
        {"testFilter", testFilter},
        {"testLevelsAndSides", testLevelsAndSides},
        {"testGlide", testGlide},
        {"testPositionMoves", testPositionMoves},
        {"testStreamPool", testStreamPool},
        {"testNoClick", testNoClick},
        {"testBlocks", testBlocks},
        {"testThroughTheEngine", testThroughTheEngine},
        {"testNoAllocation", testNoAllocation},
        {"testTooSmall", testTooSmall},
        {"testNaN", testNaN},
        {"testSilence", testSilence},
        {"testPlayFromTheTop", testPlayFromTheTop},
        {"testPresets", testPresets},
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
    std::printf("morph tests passed\n");
    return 0;
}
