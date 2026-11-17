#pragma once
// CLOUD's rules (docs/plans/granular-plan.md): the numbers and the sums, with no state, no sound
// and no SDL. The synth (cloud.hpp, engine/src/cloud.cpp) reads them; tests/cloud_test.cpp holds
// them.
//
// CLOUD plays many short pieces of one recording (grains) at once, each from a slightly different
// place, pitch and side: a cloud a key. Every grain's place, pitch, side and direction is a ROLL by
// where and when (grainUnit: the key's strike seed and the grain's number since the strike), never
// a generator walked. Every setting is read when a grain starts, and a grain once started never
// changes.
//
// Every figure is SELF (the plan's own choice; nobody has heard any of it): a test of it only says
// the code does what was chosen.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "chord_rule.hpp"

namespace apotheca {
namespace cloud {

// ---- settings ------------------------------------------------------------------------------
// Ids are appended, never reordered: songs and presets store them by number.
enum Setting {
    Position,    // 0..100: where in the recording, a share of it (positionShare)
    Spray,       // 0..100: how far each grain's place is rolled, read +- seconds (sprayShare)
    Size,        // 0..100: a grain's length as heard, 5 MS to 1.0 S (sizeSeconds)
    Density,     // 0..100: grains a second a key, 1 to 200 HZ (densityHz)
    Scatter,     // 0..100: how far a grain's start time is rolled (0 even, 100 random)
    Shape,       // 0..100: the window's peak, PLUCK 0, SMOOTH 50, SWELL 100 (riseShare)
    PitchSpray,  // 0..100: how far each grain's pitch is rolled, read +- CENTS (pitchSprayCents)
    PitchSteps,  // 0..2: FREE, OCTAVES, FIFTHS
    Reverse,     // 0..100: the chance a grain plays backwards
    Stereo,      // 0..100: how far each grain's side is rolled
    Scan,        // 0..100: x-2.00 to x2.00, x0 at 50 (scanTimes); x0 is FREEZE
    Root,        // 0..127: PITCH, 60 none; up is higher. The key that plays the recording at its own
                 // pitch is kPitchRootAt less it (owner, 2026-11-17: it was that key, and ran the wrong way)
    Tune,        // -100..100 cents
    Attack,      // 0..100: the sampler's scale (envelope::attackSeconds)
    Release,     // 0..100: the sampler's scale (envelope::fallSeconds)
    Level,       // 0..100: read in DB (levelDb)
    Velocity,    // 0..100: how much a key's velocity sets its level, 0 OFF
    kSettings
};
constexpr int kSettingRoom = 24; // a patch has room for seven more, at 0
static_assert(kSettings == 17, "ids are appended, never reordered");
enum StepsKind { kStepsFree, kStepsOctaves, kStepsFifths, kStepsKinds };

inline int settingMin(int id) { return id == Tune ? -100 : 0; }
inline int settingMax(int id) {
    switch (id) {
    case PitchSteps: return kStepsKinds - 1;
    case Root: return 127;
    default: return 100;
    }
}
// The usual value of each (the plan's section 1), on the dials below: SIZE 60 reads 120 MS,
// DENSITY 52 reads 15.7 HZ, ATTACK 8 reads 2.1 MS, RELEASE 54 reads 289 MS.
inline int settingUsual(int id) {
    static const int usual[kSettings] = {25, 10, 60, 52, 50, 50, 0, kStepsFree, 0, 40, 50, 60, 0, 8, 54, 80, 50};
    return id < 0 || id >= kSettings ? 0 : usual[id];
}
inline int settingHeld(int id, int value) {
    const int lo = settingMin(id), hi = settingMax(id);
    return value < lo ? lo : (value > hi ? hi : value);
}
inline const char* stepsName(int s) { return s == kStepsOctaves ? "OCTAVES" : s == kStepsFifths ? "FIFTHS" : "FREE"; }

// ---- macros --------------------------------------------------------------------------------
// Lane by lane (the plan's section 4). No macro is a choice: each takes a MOD.
enum Macro { MacroPosition, MacroSpray, MacroSize, MacroDensity, MacroScan, MacroPitchSpray, MacroShape, MacroReverse, kMacros };
static_assert(kMacros == 8, "every synth has eight macros (kSynthMacros)");
inline int macroSetting(int macro) {
    static const int setting[kMacros] = {Position, Spray, Size, Density, Scan, PitchSpray, Shape, Reverse};
    return setting[macro < 0 || macro >= kMacros ? 0 : macro];
}
// The house rule (PHYSIC's, ACID's): a macro below its middle slides its setting from the patch's
// value down to the setting's least, above it up to its most. 0.5 is the patch exactly.
inline float macroBent(float patch, float macro, float least, float most) {
    if (macro == 0.5f) return patch;
    return macro < 0.5f ? least + (patch - least) * (macro * 2.f) : patch + (most - patch) * ((macro - 0.5f) * 2.f);
}

// ---- the readings --------------------------------------------------------------------------
// Each from the dial (a float: a macro bends it between whole values).
inline double dialHeld(double d) { return d < 0.0 ? 0.0 : (d > 100.0 ? 100.0 : d); }
inline double positionShare(double d) { return dialHeld(d) / 100.0; }
// SPRAY: squared, 0 to half the recording either side.
inline double sprayShare(double d) { const double x = dialHeld(d) / 100.0; return 0.5 * x * x; }
inline double spraySeconds(double d, double recordingSeconds) { return sprayShare(d) * recordingSeconds; }
constexpr double kSizeLeast = 0.005, kSizeMost = 1.0;
inline double sizeSeconds(double d) { return kSizeLeast * std::pow(kSizeMost / kSizeLeast, dialHeld(d) / 100.0); }
constexpr double kDensityLeast = 1.0, kDensityMost = 200.0;
inline double densityHz(double d) { return kDensityLeast * std::pow(kDensityMost / kDensityLeast, dialHeld(d) / 100.0); }
inline double scatterShare(double d) { return dialHeld(d) / 100.0; }
inline double riseShare(double d) { return dialHeld(d) / 100.0; }
// PITCH SPRAY: squared, so the first part of the dial is a chorus and the last an octave either way.
constexpr double kPitchSprayMostCents = 1200.0;
inline double pitchSprayCents(double d) { const double x = dialHeld(d) / 100.0; return kPitchSprayMostCents * x * x; }
inline double reverseChance(double d) { return dialHeld(d) / 100.0; }
inline double stereoWidth(double d) { return dialHeld(d) / 100.0; }
constexpr double kScanMost = 2.0;
inline double scanTimes(double d) { return (dialHeld(d) - 50.0) / 50.0 * kScanMost; }
inline double tuneCents(int value) { return static_cast<double>(value); }
// LEVEL 100 is 0 DB, a share of it below; 0 is silence (-infinity).
inline double levelGain(double d) { return dialHeld(d) / 100.0; }
inline double levelDb(double d) { return d > 0.0 ? 20.0 * std::log10(levelGain(d)) : -INFINITY; }
// VELOCITY: 0 OFF (every key one level), 100 the key's level is its velocity.
inline double velocityGain(double dial, double velocity) {
    const double v = velocity < 0.0 ? 0.0 : (velocity > 1.0 ? 1.0 : velocity);
    return 1.0 - dialHeld(dial) / 100.0 * (1.0 - v);
}

// ---- the rolls ----------------------------------------------------------------------------
// One thing about grain `n` (counted from the key's strike, skipped grains too) of the key struck
// with `seed` (the engine's noteSeed: physic::strikeSeed). 0 up to (not reaching) 1.
enum GrainRoll : std::uint32_t { kGrainTime = 1, kGrainPlace = 2, kGrainPitch = 3, kGrainSide = 4, kGrainBack = 5 };
inline double grainUnit(std::uint32_t seed, std::uint32_t n, GrainRoll what) {
    const std::uint32_t h = chordHash(seed ^ chordHash(n * 0x9E3779B1u + static_cast<std::uint32_t>(what) * 0x85EBCA77u + 0x2F0C1D35u));
    return static_cast<double>(h >> 8) * (1.0 / 16777216.0);
}
inline double grainSigned(std::uint32_t seed, std::uint32_t n, GrainRoll what) { return 2.0 * grainUnit(seed, n, what) - 1.0; }
// A key played by hand (no seed handed over): the count of such keys since allNotesOff.
inline std::uint32_t liveSeed(std::uint32_t n) { return chordHash(n * 0x2545F491u + 0x3C6EF372u); }

// ---- onsets -------------------------------------------------------------------------------
// The wait from one grain's start to the next, in WHOLE samples: the mean gap 1 / DENSITY
// stretched or shrunk by SCATTER's share of the grain's TIME roll `u`, at least one sample.
// u uniform makes the mean exactly the gap, so DENSITY is the rate heard at any SCATTER.
inline long onsetWait(double rate, double hz, double scatter, double u) {
    const long w = std::lround(rate / hz * (1.0 + scatter * (2.0 * u - 1.0)));
    return w < 1 ? 1 : w;
}

// ---- the window ---------------------------------------------------------------------------
// Two half-cosine pieces: a rise over SHAPE's share of the grain and a fall over the rest, each at
// least kWindowFloorSeconds (no click at any SHAPE or SIZE). It is exactly 0 at the grain's first
// sample and at the sample after its last.
constexpr double kWindowFloorSeconds = 0.0005;
inline long windowFloor(double rate) { const long f = std::lround(kWindowFloorSeconds * rate); return f < 1 ? 1 : f; }
inline long grainSamples(double sizeSec, double rate) {
    const long n = std::lround(sizeSec * rate), least = 2 * windowFloor(rate);
    return n < least ? least : n;
}
inline long riseSamples(long length, double share, double rate) {
    const long floor = windowFloor(rate);
    long r = std::lround(share * static_cast<double>(length));
    if (r < floor) r = floor;
    if (r > length - floor) r = length - floor;
    return r;
}
// The half cosine itself: 0 at x = 0, 1 at x = 1.
inline double halfCosine(double x) { return 0.5 - 0.5 * std::cos(3.14159265358979323846 * x); }
// The window at sample k of a grain of `rise` + `fall` samples, exactly.
inline double windowAt(long k, long rise, long fall) {
    if (k <= 0 || k >= rise + fall) return 0.0;
    return k < rise ? halfCosine(static_cast<double>(k) / rise) : halfCosine(static_cast<double>(rise + fall - k) / fall);
}
// What the synth works it out with, a sample at a time: the half cosine as a polynomial, so no
// sample waits on the one before (a table read or a cosine stepped along both cost more: section
// "As built" of the plan). sin(pi y) to its y^9 term about the middle, y = x - 0.5: within 4e-6 of
// the half cosine, a hair past 0 and 1 at the ends, held there, so it is exactly 0 at x = 0 and
// exactly 1 at x = 1.
inline float windowPoly(float x) {
    const float y = x - 0.5f, y2 = y * y;
    const float s = y * (3.14159265f + y2 * (-5.16771278f + y2 * (2.55016404f + y2 * (-0.599264529f + y2 * 0.0821458866f))));
    return std::min(1.f, std::max(0.f, 0.5f + 0.5f * s)); // (min and max: the compiler takes four at a time)
}
// The mean square of either piece (3/8, whatever its length): what the overlap gain stands on.
constexpr double kWindowPower = 0.375;

// ---- level --------------------------------------------------------------------------------
// Grains from different places add in power: a grain's gain is 1 / sqrt(the expected overlap),
// DENSITY x SIZE, at least 1, so the cloud is about as loud at any DENSITY and SIZE. A sparse
// cloud (fewer than one grain sounding) is a grain at a time, each at full. A key sounds at most
// kGrainsPerKey grains at once, so the overlap is taken at most that.
constexpr int kGrainsPerKey = 12;
inline double overlapGain(double hz, double sizeSec) {
    double overlap = hz * sizeSec;
    if (overlap > kGrainsPerKey) overlap = kGrainsPerKey;
    return 1.0 / std::sqrt(overlap > 1.0 ? overlap : 1.0);
}
// STEREO: the grain's side rolled within +-width, equal power (the middle 0.7071 each side).
inline void sideGains(double width, double u, double* left, double* right) {
    const double p = width * (2.0 * u - 1.0);
    const double angle = (p + 1.0) * (3.14159265358979323846 / 4.0);
    *left = std::cos(angle);
    *right = std::sin(angle);
}

// ---- pitch --------------------------------------------------------------------------------
// PITCH STEPS' grid: FREE as rolled, OCTAVES the nearest whole octave, FIFTHS the nearest of
// 0, 7, 12, 19, 24 either way (octaves and fifths over the note, mirrored under it).
inline double steppedSemitones(double semis, int steps) {
    if (steps == kStepsOctaves) return 12.0 * std::floor(semis / 12.0 + 0.5);
    if (steps == kStepsFifths) {
        const double a = std::fabs(semis);
        const double grid[] = {0.0, 7.0, 12.0, 19.0, 24.0};
        double best = 0.0;
        for (double g : grid)
            if (std::fabs(a - g) < std::fabs(a - best)) best = g;
        return semis < 0.0 ? -best : best;
    }
    return semis;
}
// A grain's pitch roll in semitones: within +-PITCH SPRAY, on its grid.
inline double pitchRoll(double sprayCents, int steps, double u) { return steppedSemitones(sprayCents / 100.0 * (2.0 * u - 1.0), steps); }
// How fast a grain reads the recording: 1 at ROOT, an octave up twice as fast.
constexpr int kPitchRootAt = 120; // PITCH 60 is ROOT 60, middle C
inline double grainRatio(int key, int root, double tuneCentsValue, double slideSemis, double rollSemis) {
    return std::pow(2.0, (static_cast<double>(key - root) + slideSemis + rollSemis) / 12.0 + tuneCentsValue / 1200.0);
}

// ---- place --------------------------------------------------------------------------------
// The recording has no edge: a place past either end comes in from the other.
inline double wrapPlace(double x, double length) {
    if (!(length > 0.0)) return 0.0;
    if (x >= 0.0 && x < length) return x;
    double w = std::fmod(x, length);
    if (w < 0.0) w += length;
    return w >= length ? 0.0 : w;
}
// SCAN: how far the key's place has moved after `samples` at x`scan` (x1: through the recording
// at the speed it was made). The key adds this up grain by grain, so a MOD on SCAN moves it on.
inline double scanned(double scan, long samples) { return scan * static_cast<double>(samples); }
// Where grain starts: POSITION, plus what SCAN has moved, plus SPRAY's roll, wrapped. In samples.
inline double grainPlace(double positionShareValue, double scannedSamples, double sprayShareValue, double u, double length) {
    return wrapPlace(positionShareValue * length + scannedSamples + sprayShareValue * length * (2.0 * u - 1.0), length);
}

} // namespace cloud
} // namespace apotheca
