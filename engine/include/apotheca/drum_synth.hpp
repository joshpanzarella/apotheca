#pragma once
// DRUM SYNTH: a drum lane's sound made from a recipe of oscillators and noise, with knobs
// (docs/plans/drumsynth-plan.md; owner, 2026-11-17: "Let's do a drum synth").
//
// The engine never calls this. The app makes the lane's sound with make() on its own thread
// (or the helper thread, for a long one) and hands it to the engine as it hands a recording, so a
// synth lane costs nothing while the song plays, and FIT, slices, EDIT, CHOKE, FEEL, THROW, the
// step effects, Catch and a bounce all take it as they take any sound.
//
// A VOICE is a MODEL and eight DIALS, each a whole number 0..100. The eight places are one set for
// every model (TUNE, DECAY, TONE, SNAP, NOISE, SWEEP, DRIVE and the model's own); a model shows the
// ones it uses, each read in its unit (HZ, MS / S, %, ST). The numbers are in song files by place,
// and a model by its WORD: append, never reorder.
//
// The rules the sound keeps (drum_synth.cpp; tests/drumsynth_test.cpp):
//   - the same voice gives the same bits: noise is a hash of the voice and the sample's number,
//     never a generator walked; no multiply and add joined (fp contract off); the loop uses no
//     maths library (a polynomial sine), only the recipe's numbers before it do
//   - its peak is kPeak (-1 dB) exactly, whatever the knobs; the lane's VOLUME, EDIT's GAIN and the
//     mixer set the level
//   - it ends where every part is kQuietDb down, its last kTailSeconds faded to exact zeros, and is
//     never longer than kLongestSeconds
//   - no number too small to hold: every envelope is snapped to 0 under kTiny, and the filters are
//     fed steady sound (the envelope is put on after them), so flush-to-zero changes no bit
// No SDL, no app: the engine's library, as the built-in kit (drums.hpp) is.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace apotheca {
namespace drumsynth {

// The models, in the order the KIT page steps through them. 0 is "not a synth". Appended, never
// reordered (memory and the screen's order only: a song file says the WORD).
enum Model { kNone = 0, kKick, kSnare, kTom, kClap, kClHat, kOpHat, kCymbal, kRim, kCowbell, kClave, kZap, kNoiseBurst, kModels };
// The eight places. Appended, never reordered (a song file lists the dials in this order).
enum Place { kTune, kDecay, kTone, kSnap, kNoise, kSweep, kDrive, kOwn, kPlaces };

constexpr int kDialMost = 100;
constexpr float kPeak = 0.8912509f;       // -1 dB: every voice's peak
constexpr double kQuietDb = -80.0;        // a part is over when it is this far down
constexpr double kTailSeconds = 0.005;    // the last 5 ms faded to exact zeros
constexpr double kLongestSeconds = 6.0;   // no voice is longer
constexpr double kShortestSeconds = 0.02; // nor shorter
constexpr double kTiny = 1e-9;            // an envelope under this is 0 (180 dB down)

// What a dial reads in.
enum Unit { kHz, kSeconds, kPercent, kSemitones };

// One place of one model: its name (nullptr: the model does not use it), its unit, its range and
// whether the dial goes along it by ratio (log) or by step, and its usual value in the unit.
struct Knob {
    const char* name;
    Unit unit;
    double low, high;
    bool log;
    double usual;
};

struct Voice {
    std::uint8_t model = kNone;
    std::uint8_t dial[kPlaces] = {};
    bool on() const { return model > kNone && model < kModels; }
    bool operator==(const Voice& o) const { return model == o.model && std::memcmp(dial, o.dial, sizeof dial) == 0; }
    bool operator!=(const Voice& o) const { return !(*this == o); }
};
static_assert(sizeof(Voice) == 1 + kPlaces, "a Voice has no bytes that are not set (undo hashes it whole)");

// ---- the table ---------------------------------------------------------------------------------

namespace detail {
constexpr Knob kUnused = {nullptr, kPercent, 0, 1, false, 0};
// Percent places read 0..100% (low 0, high 1).
constexpr Knob pct(const char* name, double usual) { return {name, kPercent, 0, 1, false, usual}; }
constexpr Knob hz(double low, double high, double usual) { return {"TUNE", kHz, low, high, true, usual}; }
constexpr Knob decay(double low, double high, double usual) { return {"DECAY", kSeconds, low, high, true, usual}; }
constexpr Knob sweep(double most, double usual) { return {"SWEEP", kSemitones, 0, most, false, usual}; }
constexpr Knob time(const char* name, double low, double high, double usual) { return {name, kSeconds, low, high, true, usual}; }
// The hats' and the cymbal's TUNE is their lowest square; the others stand at fixed ratios above it.
constexpr Knob kHatTune = hz(100, 600, 205);

// [model][place]: TUNE, DECAY, TONE, SNAP, NOISE, SWEEP, DRIVE, the model's own.
constexpr Knob kTable[kModels][kPlaces] = {
    {kUnused, kUnused, kUnused, kUnused, kUnused, kUnused, kUnused, kUnused},
    // KICK: a sine falling SWEEP semitones to TUNE, 99% of the way in BEND; a beater's click.
    {hz(30, 150, 50), decay(0.05, 2.0, 0.5), pct("TONE", 0.4), pct("SNAP", 0.4), pct("NOISE", 0), sweep(48, 24), pct("DRIVE", 0.3), time("BEND", 0.005, 0.2, 0.04)},
    // SNARE: the shell (two sines) for BODY, the wires (noise through a high pass at TONE) for DECAY, a crack.
    {hz(100, 400, 180), decay(0.05, 1.0, 0.25), pct("TONE", 0.35), pct("SNAP", 0.5), pct("NOISE", 0.6), sweep(24, 7), pct("DRIVE", 0.3), time("BODY", 0.02, 0.5, 0.13)},
    // TOM: the kick's recipe, higher.
    {hz(60, 500, 110), decay(0.05, 2.0, 0.45), pct("TONE", 0.3), pct("SNAP", 0.3), pct("NOISE", 0), sweep(24, 12), pct("DRIVE", 0.2), time("BEND", 0.005, 0.2, 0.06)},
    // CLAP: four bursts of band-passed noise SPREAD apart, then a tail of DECAY.
    {hz(400, 5000, 1200), decay(0.05, 1.5, 0.36), pct("TONE", 0.5), pct("SNAP", 0.7), kUnused, kUnused, pct("DRIVE", 0.3), time("SPREAD", 0.003, 0.03, 0.009)},
    // CL HAT: six squares (metal) and noise through a high pass at TONE.
    {kHatTune, decay(0.01, 0.4, 0.05), pct("TONE", 0.55), pct("SNAP", 0.3), pct("NOISE", 0.5), kUnused, pct("DRIVE", 0.2), kUnused},
    // OP HAT: as CL HAT, rings longer.
    {kHatTune, decay(0.1, 3.0, 0.55), pct("TONE", 0.55), pct("SNAP", 0.3), pct("NOISE", 0.5), kUnused, pct("DRIVE", 0.2), kUnused},
    // CYMBAL: the hats' metal lower and longer, a high pass lower, a crash on the front.
    {hz(100, 600, 270), decay(0.3, 6.0, 1.7), pct("TONE", 0.55), pct("SNAP", 0.4), pct("NOISE", 0.6), kUnused, pct("DRIVE", 0.2), kUnused},
    // RIM: a high sine and a low one (TONE its share), a click.
    {hz(800, 3000, 1720), decay(0.01, 0.2, 0.04), pct("TONE", 0.4), pct("SNAP", 0.7), kUnused, kUnused, pct("DRIVE", 0.6), kUnused},
    // COWBELL: two squares, TUNE and x 1.48, through a band pass TONE raises.
    {hz(300, 1200, 540), decay(0.03, 1.5, 0.35), pct("TONE", 0.5), pct("SNAP", 0.4), kUnused, kUnused, pct("DRIVE", 0.2), kUnused},
    // CLAVE: a sine ping and a click.
    {hz(600, 5000, 2500), decay(0.005, 0.3, 0.035), kUnused, pct("SNAP", 0.3), kUnused, kUnused, pct("DRIVE", 0), kUnused},
    // ZAP: a sine falling far and fast; noise under it.
    {hz(30, 1000, 80), decay(0.02, 1.5, 0.25), kUnused, kUnused, pct("NOISE", 0), sweep(60, 48), pct("DRIVE", 0.2), time("BEND", 0.002, 0.3, 0.06)},
    // NOISE: noise through a band pass at TUNE, TONE its width, SNAP how fast it starts.
    {hz(200, 14000, 5000), decay(0.01, 2.0, 0.15), pct("TONE", 0.6), pct("SNAP", 0.8), kUnused, kUnused, pct("DRIVE", 0), kUnused},
};
constexpr const char* kNames[kModels] = {"", "KICK", "SNARE", "TOM", "CLAP", "CL HAT", "OP HAT", "CYMBAL", "RIM", "COWBELL", "CLAVE", "ZAP", "NOISE"};
constexpr const char* kWords[kModels] = {"", "kick", "snare", "tom", "clap", "clhat", "ophat", "cymbal", "rim", "cowbell", "clave", "zap", "noise"};
} // namespace detail

inline bool knownModel(int model) { return model > kNone && model < kModels; }
inline const Knob& knob(int model, int place) {
    return knownModel(model) && place >= 0 && place < kPlaces ? detail::kTable[model][place] : detail::kUnused;
}
inline bool shown(int model, int place) { return knob(model, place).name != nullptr; }
inline int shownCount(int model) {
    int n = 0;
    for (int p = 0; p < kPlaces; ++p) n += shown(model, p) ? 1 : 0;
    return n;
}
// What the KIT page writes ("KICK"), and the song file's word ("kick"). "" for none.
inline const char* modelName(int model) { return knownModel(model) ? detail::kNames[model] : ""; }
inline const char* modelWord(int model) { return knownModel(model) ? detail::kWords[model] : ""; }
// The model a word names; kNone for a word nobody knows.
inline int modelFromWord(const char* word) {
    for (int m = kNone + 1; m < kModels; ++m)
        if (word && std::strcmp(word, detail::kWords[m]) == 0) return m;
    return kNone;
}

// A dial in the place's unit: HZ, seconds, 0..1, semitones. Held to 0..100 first.
inline double knobValue(int model, int place, int dial) {
    const Knob& k = knob(model, place);
    const double d = (dial < 0 ? 0 : dial > kDialMost ? kDialMost : dial) / static_cast<double>(kDialMost);
    if (d >= 1.0) return k.high; // the top is the top exactly (the bottom is: low times 1)
    return k.log ? k.low * std::pow(k.high / k.low, d) : k.low + (k.high - k.low) * d;
}
// The dial nearest a value in the place's unit (where the usual values come from).
inline int dialFor(int model, int place, double value) {
    const Knob& k = knob(model, place);
    if (!(value > k.low)) return 0;
    if (!(value < k.high)) return kDialMost;
    const double d = k.log ? std::log(value / k.low) / std::log(k.high / k.low) : (value - k.low) / (k.high - k.low);
    const long n = std::lround(d * kDialMost);
    return n < 0 ? 0 : n > kDialMost ? kDialMost : static_cast<int>(n);
}
// What a place reads at a dial: "50 HZ", "1.2 KHZ", "40 MS", "1.70 S", "35%", "+24 ST". "" unused.
inline void knobText(int model, int place, int dial, char* out, int size) {
    if (size <= 0) return;
    out[0] = 0;
    const Knob& k = knob(model, place);
    if (!k.name) return;
    const double v = knobValue(model, place, dial);
    switch (k.unit) {
    case kHz:
        if (v < 1000.0) std::snprintf(out, static_cast<std::size_t>(size), "%.0f HZ", v);
        else std::snprintf(out, static_cast<std::size_t>(size), "%.1f KHZ", v / 1000.0);
        break;
    case kSeconds:
        if (v < 0.0095) std::snprintf(out, static_cast<std::size_t>(size), "%.1f MS", v * 1000.0);
        else if (v < 0.9995) std::snprintf(out, static_cast<std::size_t>(size), "%.0f MS", v * 1000.0);
        else std::snprintf(out, static_cast<std::size_t>(size), "%.2f S", v);
        break;
    case kPercent: std::snprintf(out, static_cast<std::size_t>(size), "%.0f%%", v * 100.0); break;
    case kSemitones: std::snprintf(out, static_cast<std::size_t>(size), "+%.0f ST", v); break;
    }
}
// A model's usual voice: every place at its usual value (an unused place at 0).
inline Voice usualVoice(int model) {
    Voice v;
    if (!knownModel(model)) return v;
    v.model = static_cast<std::uint8_t>(model);
    for (int p = 0; p < kPlaces; ++p) v.dial[p] = static_cast<std::uint8_t>(shown(model, p) ? dialFor(model, p, knob(model, p).usual) : 0);
    return v;
}
// Held to its limits: a model nobody knows is no voice (all zeros), a dial over 100 is 100, and an
// unused place is 0 (so two voices that sound the same are the same bytes).
inline Voice voiceHeld(Voice v) {
    if (!knownModel(v.model)) return Voice();
    for (int p = 0; p < kPlaces; ++p) v.dial[p] = static_cast<std::uint8_t>(!shown(v.model, p) ? 0 : v.dial[p] > kDialMost ? kDialMost : v.dial[p]);
    return v;
}

// ---- the noise ---------------------------------------------------------------------------------

// The voice as one number: its model and its eight dials (FNV-1a).
inline std::uint32_t seedOf(const Voice& v) {
    std::uint32_t h = 2166136261u;
    h = (h ^ v.model) * 16777619u;
    for (std::uint8_t d : v.dial) h = (h ^ d) * 16777619u;
    return h;
}
// White noise, -1..1: a hash of the seed and the sample's number (never a generator walked). Its 24
// top bits make the float exactly.
inline float noiseAt(std::uint32_t seed, std::uint32_t n) {
    std::uint32_t h = seed ^ (n * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (static_cast<float>(h >> 8) - 8388608.f) * (1.f / 8388608.f);
}

// ---- making it -----------------------------------------------------------------------------------

// How long the voice's sound is, in samples at this rate (what make() returns).
std::size_t lengthOf(const Voice& v, float sampleRate);
// About how much work make() is: the sound's samples times the parts it runs (a weight a sample;
// a square counts 1, a filter 2, the sine 3). The app makes one at once under kWorkInAFrame and
// hands a costlier one to its helper thread. Measured: drumsynth_test --measure.
double work(const Voice& v, float sampleRate);
constexpr double kWorkInAFrame = 7.0e5; // about 1.5 ms on the desk (2.1 ns a unit at most), 20 to 25 on the handheld
// The sound: mono, at the rate, peak kPeak, ending in exact zeros. Empty for no voice.
std::vector<float> make(const Voice& v, float sampleRate);

// The sine the voices use: sin(2 pi x) for a phase in turns (any x in [0, 1); a polynomial, the same
// bits everywhere). Within 2e-7 of the library's.
double sineTurn(double x);
// The soft curve DRIVE bends with: x (27 + x^2) / (27 + 9 x^2), 1 from 3 on (odd).
double soft(double x);

} // namespace drumsynth
} // namespace apotheca
