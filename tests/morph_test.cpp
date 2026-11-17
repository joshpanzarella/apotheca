// MORPH (docs/plans/wavetable-plan.md), batch 0: the rule header. No sound is made yet.
//
// Every figure MORPH holds is SELF (the plan's own choice: morph_rule.hpp says why). A green test
// only says the code does what the plan chose. NOBODY HAS HEARD ANY OF IT.
//
//   morph_test                  all of them
//   morph_test testSizes ...    just those
//   morph_test --measure        the figures

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// (As the synth will be: no multiply and add joined.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "apotheca/envelope.hpp"
#include "apotheca/morph_rule.hpp"

using namespace apotheca;
using namespace apotheca::morph;

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
    CHECK(TableA == 0 && PositionA == 1 && TableB == 2 && Mix == 4 && Unison == 10 && FilterType == 13 && Cutoff == 14);
    CHECK(Attack == 17 && EnvAttack == 21 && EnvPosition == 25 && Velocity == 27 && Level == 28);
    for (int id = 0; id < kSettings; ++id) {
        CHECK(settingMin(id) <= settingUsual(id) && settingUsual(id) <= settingMax(id));
        CHECK(settingHeld(id, -1000) == settingMin(id) && settingHeld(id, 1000) == settingMax(id));
        CHECK(settingHeld(id, settingUsual(id)) == settingUsual(id));
    }
    CHECK(settingMax(TableA) == 15 && settingMax(TableB) == 16 && settingMin(TableB) == 0);
    CHECK(settingMin(BSemi) == -24 && settingMax(BSemi) == 24 && settingMin(BFine) == -50 && settingMax(BFine) == 50);
    CHECK(settingMin(Octave) == -2 && settingMax(Octave) == 2 && settingMin(Tune) == -100 && settingMax(Tune) == 100);
    CHECK(settingMin(Unison) == 1 && settingMax(Unison) == 4 && settingMax(FilterType) == kHigh);
    CHECK(settingMin(EnvPosition) == -100 && settingMin(EnvCutoff) == -100 && settingMax(EnvCutoff) == 100);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6 && settingMax(Cutoff) == 100);
    // A new track's sound: ANALOG alone, B off, LOW 12 wide open, the envelopes as the plan says.
    CHECK(settingUsual(TableA) == Analog && settingUsual(TableB) == 0 && settingUsual(Unison) == 1);
    CHECK(settingUsual(FilterType) == kLow12 && settingUsual(Cutoff) == 100 && settingUsual(Mix) == 50);
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
        CHECK(!readAtStrike(macroSetting(m)) && macroSetting(m) != FilterType); // a macro moves while a key sounds
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
        CHECK(len >= std::min(kLengthMost, 8 * h) && (len == kLengthLeast || len / 2 < 8 * h)); // the least that holds 8 a harmonic
        CHECK(2 * h < len); // every harmonic under the size's own half: it holds them
        floats += len;
    }
    CHECK(floats == 12800);                      // the plan's 50 KB a frame
    CHECK(frameFloats() == 12800 + 3 * kSizes);  // with the guard points
    CHECK(sizeLength(0) == 2048 && sizeLength(3) == 2048 && sizeLength(4) == 1024 && sizeLength(5) == 1024 && sizeLength(9) == 256 &&
          sizeLength(10) == 128 && sizeLength(17) == 128);
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
    std::printf("morph tests passed\n");
    return 0;
}
