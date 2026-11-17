// DRUM SYNTH (docs/plans/drumsynth-plan.md): the rule header (batch 0) and the twelve models
// (batch 1). The engine never plays a voice: the app makes its sound and hands it over as a
// recording, so what is tested here is the sound made.
//
// Every figure a recipe holds is SELF (the plan's own). A green test says the sound does what the
// plan chose: its pitch, its decay, its peak, its end, its bits. NOBODY HAS HEARD ANY OF IT.
//
//   drumsynth_test               all of them
//   drumsynth_test testPitch ... just those
//   drumsynth_test --measure     the figures, and what a voice costs to make
//   drumsynth_test --print       the twelve usual voices' checksums (remade only from code known right)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apotheca/denormals.hpp"
#include "apotheca/drum_synth.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::drumsynth;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool gMeasure = false, gPrint = false;
static const float kRate = 48000.f;

// A voice of a model with every shown place rolled (by a hash: the same rolls every run).
static Voice rolled(int model, std::uint32_t k) {
    Voice v = usualVoice(model);
    for (int p = 0; p < kPlaces; ++p)
        if (shown(model, p)) v.dial[p] = static_cast<std::uint8_t>(static_cast<std::uint32_t>((noiseAt(0x5eed0000u + static_cast<std::uint32_t>(model), k * 8u + static_cast<std::uint32_t>(p)) + 1.f) * 50.5f) % 101u);
    return v;
}
static Voice with(Voice v, int place, int dial) {
    v.dial[place] = static_cast<std::uint8_t>(dial);
    return v;
}
static double hzOf(const Voice& v) { return knobValue(v.model, kTune, v.dial[kTune]); }
static std::size_t at(double seconds) { return static_cast<std::size_t>(seconds * kRate); }

// ---- batch 0: the rule -----------------------------------------------------------------------------

static void testModels() {
    for (int m = kNone + 1; m < kModels; ++m) {
        CHECK(knownModel(m));
        CHECK(std::strlen(modelName(m)) >= 3 && std::strlen(modelName(m)) <= 7);
        CHECK(modelFromWord(modelWord(m)) == m);
        for (int o = kNone + 1; o < m; ++o) CHECK(std::strcmp(modelWord(o), modelWord(m)) != 0 && std::strcmp(modelName(o), modelName(m)) != 0);
        CHECK(shownCount(m) >= 4 && shownCount(m) <= kPlaces);
        CHECK(shown(m, kTune) && shown(m, kDecay)); // every model has its pitch and its length
        CHECK(std::strcmp(knob(m, kTune).name, "TUNE") == 0 && std::strcmp(knob(m, kDecay).name, "DECAY") == 0);
        CHECK(knob(m, kTune).unit == kHz && knob(m, kDecay).unit == kSeconds);
        for (int p = 0; p < kPlaces; ++p) {
            const Knob& k = knob(m, p);
            if (!k.name) continue;
            CHECK(k.low < k.high);
            CHECK(k.usual >= k.low && k.usual <= k.high);
            CHECK(std::strlen(k.name) <= 6);
            // The usual dial reads within half a step of the usual value.
            const int d = dialFor(m, p, k.usual);
            const double step = k.log ? std::pow(k.high / k.low, 0.5 / kDialMost) : (k.high - k.low) * 0.5 / kDialMost;
            const double got = knobValue(m, p, d);
            CHECK(k.log ? (got / k.usual <= step * 1.0001 && k.usual / got <= step * 1.0001) : std::fabs(got - k.usual) <= step * 1.0001);
        }
    }
    CHECK(!knownModel(kNone) && !knownModel(kModels) && !knownModel(-1));
    CHECK(modelFromWord("snares") == kNone && modelFromWord("") == kNone && modelFromWord(nullptr) == kNone && modelFromWord("KICK") == kNone);
    CHECK(std::strcmp(modelName(kModels), "") == 0 && std::strcmp(modelWord(kNone), "") == 0);
    CHECK(!shown(kNone, kTune) && !shown(kKick, kPlaces) && !shown(kKick, -1));
    // The places a model does not use (the plan's table).
    CHECK(!shown(kClap, kNoise) && !shown(kClap, kSweep) && shown(kClap, kOwn) && std::strcmp(knob(kClap, kOwn).name, "SPREAD") == 0);
    CHECK(!shown(kClave, kTone) && !shown(kClave, kOwn) && shownCount(kClave) == 4);
    CHECK(shownCount(kKick) == 8 && std::strcmp(knob(kKick, kOwn).name, "BEND") == 0 && std::strcmp(knob(kSnare, kOwn).name, "BODY") == 0);
    CHECK(std::strcmp(modelName(kClHat), "CL HAT") == 0 && std::strcmp(modelWord(kNoiseBurst), "noise") == 0);
}

static void testReadings() {
    char t[32];
    const auto reads = [&](int m, int p, int d, const char* want) {
        knobText(m, p, d, t, sizeof t);
        if (std::strcmp(t, want) != 0) std::printf("  %s %s at %d reads \"%s\", not \"%s\"\n", modelName(m), knob(m, p).name ? knob(m, p).name : "-", d, t, want);
        return std::strcmp(t, want) == 0;
    };
    CHECK(reads(kKick, kTune, 0, "30 HZ"));
    CHECK(reads(kKick, kTune, 100, "150 HZ"));
    CHECK(reads(kKick, kTune, -5, "30 HZ") && reads(kKick, kTune, 300, "150 HZ")); // held to the dial
    CHECK(reads(kNoiseBurst, kTune, 100, "14.0 KHZ"));
    CHECK(reads(kRim, kTune, 0, "800 HZ"));
    CHECK(reads(kKick, kDecay, 0, "50 MS") && reads(kKick, kDecay, 100, "2.00 S"));
    CHECK(reads(kClave, kDecay, 0, "5.0 MS"));
    CHECK(reads(kCymbal, kDecay, 100, "6.00 S"));
    CHECK(reads(kZap, kSweep, 100, "+60 ST") && reads(kZap, kSweep, 0, "+0 ST") && reads(kKick, kSweep, 50, "+24 ST"));
    CHECK(reads(kKick, kTone, 0, "0%") && reads(kKick, kTone, 100, "100%") && reads(kKick, kTone, 37, "37%"));
    CHECK(reads(kClap, kOwn, 0, "3.0 MS") && reads(kClap, kOwn, 100, "30 MS"));
    CHECK(reads(kClave, kTone, 50, "")); // not used
    CHECK(reads(kNone, kTune, 50, ""));
    // The ends are the ends exactly, and every shown place climbs with its dial.
    for (int m = kNone + 1; m < kModels; ++m)
        for (int p = 0; p < kPlaces; ++p) {
            if (!shown(m, p)) continue;
            CHECK(knobValue(m, p, 0) == knob(m, p).low && knobValue(m, p, kDialMost) == knob(m, p).high);
            for (int d = 1; d <= kDialMost; ++d) CHECK(knobValue(m, p, d) > knobValue(m, p, d - 1));
            for (int d = 0; d <= kDialMost; d += 7) CHECK(dialFor(m, p, knobValue(m, p, d)) == d);
            // The widest reading fits the VOICE view's box (the plan: "14.0 KHZ", "1.50 S").
            for (int d = 0; d <= kDialMost; ++d) {
                knobText(m, p, d, t, sizeof t);
                CHECK(std::strlen(t) >= 2 && std::strlen(t) <= 8);
            }
        }
}

static void testVoices() {
    for (int m = kNone + 1; m < kModels; ++m) {
        const Voice v = usualVoice(m);
        CHECK(v.on() && v.model == m);
        CHECK(voiceHeld(v) == v);
        for (int p = 0; p < kPlaces; ++p) {
            if (!shown(m, p)) CHECK(v.dial[p] == 0);
            else CHECK(v.dial[p] == dialFor(m, p, knob(m, p).usual));
        }
    }
    CHECK(usualVoice(kKick).dial[kSweep] == 50); // +24 of 48
    CHECK(!usualVoice(kNone).on() && !usualVoice(kModels).on() && usualVoice(kModels) == Voice());
    Voice odd;
    odd.model = 200;
    odd.dial[0] = 50;
    CHECK(voiceHeld(odd) == Voice());
    Voice big = usualVoice(kClave);
    big.dial[kTune] = 250;
    big.dial[kTone] = 40; // CLAVE has no TONE
    const Voice held = voiceHeld(big);
    CHECK(held.dial[kTune] == 100 && held.dial[kTone] == 0 && held.dial[kDecay] == big.dial[kDecay]);
    CHECK(usualVoice(kKick) != usualVoice(kTom) && with(usualVoice(kKick), kTone, 41) != usualVoice(kKick));
}

static void testNoise() {
    const std::uint32_t seed = seedOf(usualVoice(kSnare));
    double sum = 0, squares = 0, most = 0;
    const int count = 200000;
    for (int i = 0; i < count; ++i) {
        const float x = noiseAt(seed, static_cast<std::uint32_t>(i));
        CHECK(x >= -1.f && x < 1.f);
        CHECK(x == noiseAt(seed, static_cast<std::uint32_t>(i)));
        sum += x;
        squares += static_cast<double>(x) * x;
        most = std::max(most, static_cast<double>(std::fabs(x)));
    }
    CHECK(std::fabs(sum / count) < 0.01);
    CHECK(std::fabs(std::sqrt(squares / count) - std::sqrt(1.0 / 3.0)) < 0.01);
    CHECK(most > 0.999);
    // Next samples are not alike (no short pattern), and another voice is another noise.
    double together = 0;
    for (int i = 0; i < count; ++i) together += static_cast<double>(noiseAt(seed, static_cast<std::uint32_t>(i))) * noiseAt(seed, static_cast<std::uint32_t>(i + 1));
    CHECK(std::fabs(together / count) < 0.01);
    int same = 0;
    for (int i = 0; i < 1000; ++i) same += noiseAt(seed, static_cast<std::uint32_t>(i)) == noiseAt(seedOf(with(usualVoice(kSnare), kTone, 36)), static_cast<std::uint32_t>(i)) ? 1 : 0;
    CHECK(same < 5);
    // The seed: every dial and the model count.
    const Voice v = usualVoice(kKick);
    for (int p = 0; p < kPlaces; ++p) CHECK(seedOf(with(v, p, v.dial[p] + 1)) != seedOf(v));
    Voice other = v;
    other.model = kTom;
    CHECK(seedOf(other) != seedOf(v));
}

static void testSineAndSoft() {
    double worst = 0;
    for (int i = 0; i < 200000; ++i) {
        const double x = i / 200000.0;
        worst = std::max(worst, std::fabs(sineTurn(x) - std::sin(2.0 * 3.141592653589793 * x)));
    }
    if (gMeasure) std::printf("  the sine: %.2g from the library's at worst\n", worst);
    CHECK(worst < 2e-7);
    CHECK(sineTurn(0.0) == 0.0 && sineTurn(0.5) == 0.0 && sineTurn(0.25) > 0.9999999 && sineTurn(0.75) < -0.9999999);
    CHECK(soft(3.0) == 1.0 && soft(10.0) == 1.0 && soft(-3.0) == -1.0 && soft(0.0) == 0.0);
    for (int i = 1; i <= 300; ++i) {
        const double x = i / 100.0;
        CHECK(soft(x) > soft(x - 0.01));
        CHECK(soft(-x) == -soft(x));
        CHECK(soft(x) <= 1.0);
    }
    CHECK(std::fabs(soft(0.01) - 0.01) < 1e-5); // straight near 0
    CHECK(std::fabs(soft(1.0) - std::tanh(1.0)) < 0.02);
}

// ---- batch 1: the models -----------------------------------------------------------------------------

static bool tooSmall(float x) { return x != 0.f && std::fabs(x) < 1.1754944e-38f; }

// What every sound keeps: its length, its peak, its end, no number too small, the same bits twice
// and with flush-to-zero on.
static void holds(const Voice& v, const char* what) {
    const std::vector<float> s = make(v, kRate);
    const std::size_t n = lengthOf(v, kRate);
    bool ok = s.size() == n && n >= at(kShortestSeconds) - 1 && n <= at(kLongestSeconds) + 1;
    float peak = 0.f;
    bool finite = true, small = false;
    for (float x : s) {
        finite = finite && std::isfinite(x);
        small = small || tooSmall(x);
        peak = std::max(peak, std::fabs(x));
    }
    ok = ok && finite && !small && peak <= kPeak && peak >= kPeak * 0.999f;
    ok = ok && !s.empty() && s.back() == 0.f;
    // The end: the last 5 ms are under -40 dB of the peak and fall to the exact zero.
    float endLoudest = 0.f;
    const std::size_t tail = at(kTailSeconds);
    for (std::size_t i = s.size() - tail; i < s.size(); ++i) endLoudest = std::max(endLoudest, std::fabs(s[i]));
    ok = ok && endLoudest < kPeak * 0.01f;
    const std::vector<float> again = make(v, kRate);
    ok = ok && again.size() == s.size() && std::memcmp(again.data(), s.data(), s.size() * sizeof(float)) == 0;
    std::vector<float> flushed;
    {
        FlushDenormals on(true);
        flushed = make(v, kRate);
    }
    ok = ok && flushed.size() == s.size() && std::memcmp(flushed.data(), s.data(), s.size() * sizeof(float)) == 0;
    if (!ok) {
        std::printf("  %s %s:", modelName(v.model), what);
        for (int p = 0; p < kPlaces; ++p) std::printf(" %d", v.dial[p]);
        std::printf("  n %zu of %zu, peak %.6f, finite %d, small %d, last %g, end %g\n", s.size(), n, peak, finite, small, s.empty() ? -1.0 : s.back(), endLoudest);
    }
    CHECK(ok);
}

static void testEveryModel() {
    for (int m = kNone + 1; m < kModels; ++m) {
        holds(usualVoice(m), "usual");
        for (std::uint32_t k = 0; k < 12; ++k) holds(rolled(m, k), "rolled");
        Voice lo = usualVoice(m), hi = usualVoice(m);
        for (int p = 0; p < kPlaces; ++p)
            if (shown(m, p)) lo.dial[p] = 0, hi.dial[p] = kDialMost;
        holds(lo, "every dial 0");
        holds(hi, "every dial 100");
    }
    CHECK(make(Voice(), kRate).empty() && lengthOf(Voice(), kRate) == 0 && work(Voice(), kRate) == 0.0);
    Voice odd;
    odd.model = kModels;
    CHECK(make(odd, kRate).empty());
    CHECK(make(usualVoice(kKick), 0.f).empty());
    // The length is what the parts need, and never past the longest.
    CHECK(lengthOf(with(usualVoice(kCymbal), kDecay, 100), kRate) == at(kLongestSeconds));
    CHECK(lengthOf(with(usualVoice(kClave), kDecay, 0), kRate) == at(kShortestSeconds));
    CHECK(lengthOf(with(usualVoice(kKick), kDecay, 80), kRate) > lengthOf(with(usualVoice(kKick), kDecay, 40), kRate));
    // At another rate: as long in seconds.
    CHECK(std::labs(static_cast<long>(lengthOf(usualVoice(kSnare), 44100.f)) - static_cast<long>(lengthOf(usualVoice(kSnare), kRate) * 44100 / 48000)) <= 1);
}

// A voice with nothing over its sine: SNAP, NOISE and DRIVE at 0 where shown.
static Voice plain(int model) {
    Voice v = usualVoice(model);
    for (int p : {kSnap, kNoise, kDrive})
        if (shown(model, p)) v.dial[p] = 0;
    return v;
}

// The pitch of a run, by its zero crossings.
static double pitchIn(const std::vector<float>& s, double from, double to) { return pitchOf(s, at(from), std::min(s.size(), at(to))); }

static void testPitch() {
    // KICK, TOM and ZAP land on TUNE once the sweep is over; CLAVE and RIM (TONE 0) ring at it.
    for (int m : {kKick, kTom, kZap, kClave, kRim}) {
        for (int d : {0, 30, 70, 100}) {
            Voice v = with(plain(m), kTune, d);
            if (m == kRim) v.dial[kTone] = 0;
            if (shown(m, kOwn)) v.dial[kOwn] = 30; // a short BEND: the sweep is over early
            v.dial[kDecay] = 100;
            const std::vector<float> s = make(v, kRate);
            const double want = hzOf(v);
            const double from = m == kClave || m == kRim ? 0.002 : 0.15, to = from + std::min(0.15, std::max(0.03, 40.0 / want));
            const double got = pitchIn(s, from, to);
            const double cents = 1200.0 * std::log2(got / want);
            if (gMeasure || std::fabs(cents) > 2.0) std::printf("  %s TUNE %d: %.2f HZ wanted, %.2f read (%+.2f cents)\n", modelName(m), d, want, got, cents);
            CHECK(std::fabs(cents) < 2.0);
        }
    }
    // SNARE's shell: TUNE and its upper mode (x 1.65) are there, between them is not.
    for (int d : {0, 50, 100}) {
        Voice v = with(plain(kSnare), kTune, d);
        v.dial[kSweep] = 0;
        const std::vector<float> s = make(v, kRate);
        const double f = hzOf(v), low = partNear(s.data(), at(0.08), f), up = partNear(s.data(), at(0.08), f * 1.65), between = partNear(s.data(), at(0.08), f * 1.32);
        if (gMeasure) std::printf("  SNARE TUNE %d: %.0f HZ %.1f dB, x1.65 %.1f dB, between %.1f dB\n", d, f, dB(low), dB(up), dB(between));
        CHECK(low > 4.0 * between && up > 2.0 * between && low > up);
    }
    // COWBELL's two squares: TUNE and x 1.48, over what is between.
    for (int d : {0, 50, 100}) {
        const Voice v = with(plain(kCowbell), kTune, d);
        const std::vector<float> s = make(v, kRate);
        const double f = hzOf(v), low = partNear(s.data(), at(0.1), f), up = partNear(s.data(), at(0.1), f * 1.48), between = partNear(s.data(), at(0.1), f * 1.24);
        if (gMeasure) std::printf("  COWBELL TUNE %d: %.0f HZ %.1f dB, x1.48 %.1f dB, between %.1f dB\n", d, f, dB(low), dB(up), dB(between));
        CHECK(low > 4.0 * between && up > 4.0 * between);
    }
    // The hats' metal: TUNE's square is in it, and moves with TUNE.
    for (int d : {20, 80}) {
        Voice v = with(plain(kOpHat), kTune, d);
        v.dial[kTone] = 0; // the high pass at its lowest (3 kHz): look at an upper square's third harmonic
        const std::vector<float> s = make(v, kRate);
        const double f = hzOf(v) * 3.8967 * 3.0, on = partNear(s.data(), at(0.2), f), off = partNear(s.data(), at(0.2), f * 1.03);
        if (gMeasure) std::printf("  OP HAT TUNE %d: its top square's third (%.0f HZ) %.1f dB, 3%% off %.1f dB\n", d, f, dB(on), dB(off));
        CHECK(on > 3.0 * off);
    }
}

static void testSweep() {
    // The first cycle of a long BEND is SWEEP semitones over TUNE.
    for (int m : {kKick, kTom, kZap}) {
        for (int sweep : {0, 25, 50}) {
            Voice v = with(with(plain(m), kSweep, sweep), kOwn, 100);
            v.dial[kTune] = 20;
            const std::vector<float> s = make(v, kRate);
            const double from = hzOf(v) * std::pow(2.0, knobValue(m, kSweep, sweep) / 12.0);
            // The first cycle: from the start (phase 0, rising) to the first upward crossing.
            std::size_t i = 1;
            while (i < s.size() && !(s[i - 1] < 0.f && s[i] >= 0.f)) ++i;
            const double t = (i - 1 + s[i - 1] / (s[i - 1] - s[i])) / kRate;
            const double cents = 1200.0 * std::log2((1.0 / t) / from);
            if (gMeasure) std::printf("  %s SWEEP %d: the first cycle %.1f HZ, %.1f wanted (%+.1f cents; the fall in it makes it flat)\n", modelName(m), sweep, 1.0 / t, from, cents);
            CHECK(cents < 5.0 && cents > (sweep == 0 ? -5.0 : -150.0));
        }
    }
    // A short BEND is over sooner: 20 ms in, the short one is nearer TUNE.
    Voice slow = with(with(plain(kKick), kSweep, 50), kOwn, 100), quick = with(slow, kOwn, 0);
    const std::vector<float> a = make(slow, kRate), b = make(quick, kRate);
    CHECK(pitchIn(a, 0.02, 0.06) > 1.3 * pitchIn(b, 0.02, 0.06));
}

// How long the level takes to fall 60 dB, read from two windows' RMS.
static double t60Of(const std::vector<float>& s, double t1, double t2, double window) {
    const double d1 = dB(rmsOf(s, at(t1), at(t1 + window))), d2 = dB(rmsOf(s, at(t2), at(t2 + window)));
    return 60.0 * (t2 - t1) / (d1 - d2);
}

static void testDecay() {
    struct Case { int model; double window, within; };
    const Case cases[] = {{kKick, 0.01, 0.05}, {kTom, 0.01, 0.05}, {kClave, 0.002, 0.05}, {kRim, 0.002, 0.05}, {kZap, 0.01, 0.05},
                          {kClHat, 0.004, 0.15}, {kOpHat, 0.02, 0.15}, {kCymbal, 0.05, 0.15}, {kCowbell, 0.01, 0.1}, {kNoiseBurst, 0.005, 0.15}, {kSnare, 0.01, 0.15}};
    for (const Case& c : cases) {
        for (int d : {20, 60, 90}) {
            Voice v = with(plain(c.model), kDecay, d);
            if (c.model == kRim) v.dial[kTone] = 0;
            if (c.model == kSnare) v.dial[kNoise] = 100, v.dial[kOwn] = 0; // the wires alone, after the shell
            if (c.model == kNoiseBurst) v.dial[kSnap] = 100;
            const double want = knobValue(c.model, kDecay, d);
            const std::vector<float> s = make(v, kRate);
            if (shown(c.model, kOwn) && c.model != kSnare) v.dial[kOwn] = 0; // the shortest BEND: the sweep over in a few ms
            // Two windows half the decay apart, after the sweep, the front and the shell.
            const double t1 = want * 0.2 + std::min(want * 0.25, c.model == kSnare ? 0.1 : c.model == kCymbal || c.model == kCowbell ? 0.06 : 0.01), t2 = t1 + want * 0.5;
            const double got = t60Of(s, t1, t2, std::min(c.window, want * 0.1));
            if (gMeasure || std::fabs(got / want - 1.0) > c.within) std::printf("  %s DECAY %d: %.0f MS wanted, %.0f read\n", modelName(c.model), d, want * 1000, got * 1000);
            CHECK(std::fabs(got / want - 1.0) < c.within);
        }
    }
    // CLAP: its tail is DECAY, after the bursts.
    for (int d : {40, 80}) {
        const Voice v = with(with(usualVoice(kClap), kDecay, d), kDrive, 0);
        const double want = knobValue(kClap, kDecay, d), start = 3 * knobValue(kClap, kOwn, v.dial[kOwn]) + 0.02;
        const double got = t60Of(make(v, kRate), start, start + want * 0.5, 0.01);
        if (gMeasure) std::printf("  CLAP DECAY %d: %.0f MS wanted, %.0f read\n", d, want * 1000, got * 1000);
        CHECK(std::fabs(got / want - 1.0) < 0.15);
    }
}

// How bright a run is: its first difference's level over its own (about 2 pi f / rate for a sine).
static double brightness(const std::vector<float>& s, double from, double to) {
    double d = 0, e = 0;
    const std::size_t a = at(from) + 1, b = std::min(s.size(), at(to));
    for (std::size_t i = a; i < b; ++i) {
        const double x = s[i], y = static_cast<double>(s[i]) - s[i - 1];
        d += y * y;
        e += x * x;
    }
    return e > 0 ? std::sqrt(d / e) : 0.0;
}

// How often a run crosses zero, a second: rises with what is bright in it.
static double crossings(const std::vector<float>& s, double from, double to) {
    int count = 0;
    const std::size_t a = at(from), b = std::min(s.size(), at(to));
    for (std::size_t i = a + 1; i < b; ++i) count += (s[i - 1] < 0.f) != (s[i] < 0.f) ? 1 : 0;
    return count / (to - from);
}
static double firstShare(const std::vector<float>& s) { return rmsOf(s, 0, at(0.002)) / rmsOf(s, at(0.002), at(0.02)); }
static double crest(const std::vector<float>& s) { return peakOf(s) / rmsOf(s, 0, std::min(s.size(), at(0.3))); }

static void testKnobs() {
    // TONE brightens.
    for (int m : {kSnare, kClHat, kOpHat, kCymbal, kCowbell, kKick, kNoiseBurst}) {
        const double from = m == kKick ? 0.0 : 0.01, to = m == kKick ? 0.004 : 0.04;
        Voice v = usualVoice(m);
        if (m == kKick) v.dial[kSnap] = 100;
        const double dark = brightness(make(with(v, kTone, 0), kRate), from, to), bright = brightness(make(with(v, kTone, 100), kRate), from, to);
        if (gMeasure) std::printf("  %s TONE 0 / 100: brightness %.3f / %.3f\n", modelName(m), dark, bright);
        CHECK(bright > 1.15 * dark);
    }
    // The hats and the cymbal are high passed: what is under 600 HZ is far under the whole, and the
    // snare's wires (NOISE full, the shell all but gone) too.
    for (int m : {kClHat, kOpHat, kCymbal, kSnare}) {
        Voice v = with(plain(m), kTone, 0);
        if (m == kSnare) v.dial[kNoise] = 100;
        const std::vector<float> s = make(v, kRate);
        const std::vector<float> part(s.begin() + at(0.01), s.begin() + std::min(s.size(), at(0.06)));
        const double under = dB(rmsOf(bandOf(part, m == kSnare ? 120.0 : 300.0)) / rmsOf(part));
        if (gMeasure) std::printf("  %s TONE 0: %.1f dB of it low down\n", modelName(m), under);
        CHECK(under < -20.0);
    }
    // RIM's TONE is its low tone's share: the low one comes up.
    {
        const Voice v = plain(kRim);
        const std::vector<float> a = make(with(v, kTone, 0), kRate), b = make(with(v, kTone, 100), kRate);
        const double low = hzOf(v) * 0.283;
        CHECK(partNear(b.data(), at(0.02), low) > 10.0 * partNear(a.data(), at(0.02), low));
    }
    // CLAP's TONE widens: the band's edges come up beside its centre.
    {
        const Voice v = usualVoice(kClap);
        const std::vector<float> a = make(with(v, kTone, 0), kRate), b = make(with(v, kTone, 100), kRate);
        const double f = hzOf(v);
        const std::vector<float> a1 = std::vector<float>(a.begin(), a.begin() + at(0.2)), b1 = std::vector<float>(b.begin(), b.begin() + at(0.2));
        const double narrow = rmsOf(bandOf(a1, f * 3.0)) / rmsOf(bandOf(a1, f)), wide = rmsOf(bandOf(b1, f * 3.0)) / rmsOf(bandOf(b1, f));
        if (gMeasure) std::printf("  CLAP TONE 0 / 100: three times the centre over the centre %.3f / %.3f\n", narrow, wide);
        CHECK(wide > 2.0 * narrow);
    }
    // NOISE: more that is not the pitch (the snare's wires, the hats' noise over their metal).
    for (int m : {kSnare, kClHat, kKick, kZap}) {
        const Voice v = plain(m);
        const std::vector<float> a = make(with(v, kNoise, 0), kRate), b = make(with(v, kNoise, 100), kRate);
        const double none = brightness(a, 0.01, 0.05), full = brightness(b, 0.01, 0.05);
        std::vector<float> d(at(0.05));
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = b[i] - a[i];
        const double added = rmsOf(d, at(0.01)) / rmsOf(a, at(0.01), at(0.05));
        if (gMeasure) std::printf("  %s NOISE 0 / 100: brightness %.3f / %.3f, what it adds %.2f of the level\n", modelName(m), none, full, added);
        CHECK(full > 1.15 * none && added > 0.2);
    }
    // SNAP: more of it in the first 4 ms.
    for (int m : {kKick, kSnare, kTom, kClHat, kCymbal, kRim, kCowbell, kClave, kNoiseBurst, kClap}) {
        const Voice v = usualVoice(m);
        const double soft0 = firstShare(make(with(v, kSnap, 0), kRate)), hard = firstShare(make(with(v, kSnap, 100), kRate));
        if (gMeasure) std::printf("  %s SNAP 0 / 100: the first 2 ms %.2f / %.2f of the next\n", modelName(m), soft0, hard);
        CHECK(hard > 1.15 * soft0);
    }
    // DRIVE: less peak over level.
    for (int m = kNone + 1; m < kModels; ++m) {
        const Voice v = usualVoice(m);
        const double clean = crest(make(with(v, kDrive, 0), kRate)), driven = crest(make(with(v, kDrive, 100), kRate));
        if (gMeasure) std::printf("  %s DRIVE 0 / 100: peak over level %.2f / %.2f\n", modelName(m), clean, driven);
        CHECK(driven < 0.8 * clean);
    }
    // DRIVE at 0 is the sum as it is, only set to the peak: a plain KICK is a sine's decay.
    {
        const Voice v = plain(kKick);
        const std::vector<float> s = make(v, kRate);
        CHECK(std::fabs(s[at(0.3)] / s[at(0.3) + 1]) < 50.f); // (no flat top)
    }
    // CLAP's SPREAD: the bursts stand SPREAD apart.
    for (int d : {20, 80}) {
        const Voice v = with(with(with(usualVoice(kClap), kOwn, d), kSnap, 100), kDrive, 0);
        const std::vector<float> s = make(v, kRate);
        const double gap = knobValue(kClap, kOwn, d);
        // Each burst's start is louder than just before it.
        for (int b = 1; b < 3; ++b) {
            const std::size_t start = static_cast<std::size_t>(std::lround(b * gap * kRate));
            const std::size_t w = std::max<std::size_t>(16, static_cast<std::size_t>(gap * kRate / 8));
            CHECK(rmsOf(s, start, start + w) > 2.0 * rmsOf(s, start - w, start));
        }
    }
    // NOISE's SNAP: at 0 it rises over 40 ms.
    {
        const std::vector<float> s = make(with(usualVoice(kNoiseBurst), kSnap, 0), kRate);
        CHECK(rmsOf(s, 0, at(0.005)) < 0.3 * rmsOf(s, at(0.035), at(0.04)));
        // ...straight up (half way at 20 ms), then the decay from its top.
        const double half = rmsOf(s, at(0.018), at(0.022)) / rmsOf(s, at(0.036), at(0.04));
        CHECK(half > 0.4 && half < 0.65);
        CHECK(rmsOf(s, at(0.001), at(0.003)) > 0.0);
    }
}

// The twelve usual voices to the bit, and 72 rolled ones as one sum. Taken with --print on 2026-11-17
// (x86-64, gcc, glibc) and the same under qemu-aarch64 with the handheld's build (clang, -O2
// -mcpu=cortex-a53, its sysroot). Remade only from code known right. The recipe's numbers come from
// the maths library (pow and exp, in double; tan is the voice's own, since the handheld's library's
// differs from the desk's in its last bit), so they hold only where that library gives the same last
// bits: the test checks that first (mathsPrint), and elsewhere says so and leaves the sums.
static std::uint64_t mathsPrint() {
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 400; ++i) {
        const double v[] = {std::pow(10.0, -3.0 / (0.01 * (i + 1) * 48000.0)), std::pow(12.0, i / 400.0), std::exp(-1.0 / ((i + 1) * 4.0)), std::sqrt(i + 0.5)};
        for (double one : v) {
            std::uint64_t bits;
            std::memcpy(&bits, &one, sizeof bits);
            for (int b = 0; b < 8; ++b) h = (h ^ ((bits >> (8 * b)) & 0xffu)) * 1099511628211ull;
        }
    }
    return h;
}
static const std::uint64_t kMathsPrint = 0x3429e160a1f87b85ull;
static const std::uint64_t kUsualSums[kModels] = {0ull,
    0xc89277f7c37109e2ull, // KICK
    0x2f3dc69f3c643623ull, // SNARE
    0x49e632bfa82765edull, // TOM
    0x310a338a207ab039ull, // CLAP
    0x378910cf97c03594ull, // CL HAT
    0xdea22689c17f2e1full, // OP HAT
    0xfe1d92b172999900ull, // CYMBAL
    0x75539e6145dfda42ull, // RIM
    0x865de5da99ae1411ull, // COWBELL
    0x94cb3cc7a1a41ae5ull, // CLAVE
    0xb786a3593307277cull, // ZAP
    0x3365f648c1b187eaull, // NOISE
};
static const std::uint64_t kRolledSum = 0xe52248eee98287caull;
static void testChecksums() {
    const bool sameMaths = mathsPrint() == kMathsPrint;
    if (gPrint) std::printf("  maths print 0x%016llxull\n", static_cast<unsigned long long>(mathsPrint()));
    else if (!sameMaths) std::printf("  drumsynth: another maths library here: the twelve sums are not checked\n");
    for (int m = kNone + 1; m < kModels; ++m) {
        Sum s;
        s.add(make(usualVoice(m), kRate));
        if (gPrint) std::printf("    0x%016llxull, // %s\n", static_cast<unsigned long long>(s.h), modelName(m));
        else if (sameMaths) CHECK(s.h == kUsualSums[m]);
    }
    Sum rolledSum;
    for (int m = kNone + 1; m < kModels; ++m)
        for (std::uint32_t k = 100; k < 106; ++k) rolledSum.add(make(rolled(m, k), kRate));
    if (gPrint) std::printf("  rolled 0x%016llxull\n", static_cast<unsigned long long>(rolledSum.h));
    else if (sameMaths) CHECK(rolledSum.h == kRolledSum);
}

// What a voice costs to make (the app's thread, when a knob turns).
static void testCost() {
    if (!gMeasure) return;
    std::printf("  making each model's voice (desk; the handheld about 13 to 17 times as long):\n");
    double perWork = 0;
    for (int m = kNone + 1; m < kModels; ++m) {
        for (int longest = 0; longest < 2; ++longest) {
            const Voice v = longest ? with(usualVoice(m), kDecay, 100) : usualVoice(m);
            double best = 1e9;
            for (int r = 0; r < 5; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                const std::vector<float> s = make(v, kRate);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                best = std::min(best, ms);
                CHECK(!s.empty());
            }
            const double n = static_cast<double>(lengthOf(v, kRate));
            perWork = std::max(perWork, best * 1e6 / work(v, kRate));
            std::printf("    %-8s %-8s %6.2f s  %7.3f ms  %5.1f ns a sample  work %.2g (%s a frame)\n", modelName(m), longest ? "longest" : "usual", n / kRate, best, best * 1e6 / n, work(v, kRate),
                        work(v, kRate) > kWorkInAFrame ? "OVER" : "in");
        }
    }
    std::printf("  at most %.2f ns a unit of work: kWorkInAFrame (%.2g) is %.2f ms on this desk\n", perWork, kWorkInAFrame, perWork * kWorkInAFrame / 1e6);
}

struct Named { const char* name; void (*run)(); };

int main(int argc, char** argv) {
    const Named tests[] = {
        {"testModels", testModels},
        {"testReadings", testReadings},
        {"testVoices", testVoices},
        {"testNoise", testNoise},
        {"testSineAndSoft", testSineAndSoft},
        {"testEveryModel", testEveryModel},
        {"testPitch", testPitch},
        {"testSweep", testSweep},
        {"testDecay", testDecay},
        {"testKnobs", testKnobs},
        {"testChecksums", testChecksums},
        {"testCost", testCost},
    };
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--measure") == 0) gMeasure = true;
        else if (std::strcmp(argv[i], "--print") == 0) gPrint = true;
        else only.push_back(argv[i]);
    }
    for (const Named& t : tests) {
        if (!only.empty() && std::find(only.begin(), only.end(), t.name) == only.end()) continue;
        if (gMeasure || gPrint) std::printf("%s\n", t.name);
        t.run();
    }
    if (failures) std::printf("%d failure(s)\n", failures);
    else std::printf("drumsynth: all passed\n");
    return failures ? 1 : 0;
}
