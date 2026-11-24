#pragma once
// MORPH's rules (docs/plans/wavetable-plan.md): the numbers and the sums, with no state, no sound
// and no SDL. The synth (batch 1: morph.hpp, engine/src/morph.cpp, engine/src/morph/) will read
// them; tests/morph_test.cpp holds them.
//
// MORPH is a wavetable synth: an oscillator plays one cycle of a wave (a FRAME) at the key's pitch,
// a TABLE is a row of frames, and POSITION picks where in the row it reads, crossfading the two
// frames either side. Two such oscillators (A and B), a SUB sine, UNISON streams, the effects'
// state-variable filter, an AMP and a MOD ENV.
//
// What is here:
//   the settings (29, room 40) and the macros            settingMin/Max/Usual/Held, macroSetting
//   the readings, each in its unit                       positionPlace, detuneCents, cutoffHz ...
//   the SIZES a frame is kept at, one per half octave    sizeHarmonics, sizeLength, sizeFor
//   POSITION to the two frames read and the crossfade    framePlace
//   the phase, a 32-bit whole number                     phaseStep, readIndex, readFraction
//   a macro slid in over 512 samples                     macroSlid
//   the stream pool and the keys it leaves               streamsPerKey, keysFor
//   UNISON's spread in pitch and in the sides            streamCents, streamSide, sideGains
//   the rolls (unison phases), by place, never walked    streamPhase, liveSeed
//
// Every figure is SELF (the plan's own choice; nobody has heard any of it): a test of it only says
// the code does what was chosen.

#include <cmath>
#include <cstdint>

#include "chord_rule.hpp"
#include "mod_rule.hpp"

namespace apotheca {
namespace morph {

// ---- the tables ----------------------------------------------------------------------------
constexpr int kBuiltInTables = 16;
constexpr int kTableFrames = 32;  // a built-in table's frames
constexpr int kFramesMost = 64;   // a player's own (batch 6)
constexpr int kFramesLeast = 2;   // a table has two frames at least, so a pair is always there
enum TableKind { Analog, Pulse, Sync, Vowel, Organ, Harmonic, Fold, Bend, Fm, Ratio, Drive, Steps, Grit, Comb, Bell, Glass, kTables };
static_assert(kTables == kBuiltInTables, "a name for every built-in table");
inline const char* tableName(int t) {
    static const char* const names[kTables] = {"ANALOG", "PULSE", "SYNC", "VOWEL", "ORGAN", "HARMONIC", "FOLD", "BEND",
                                               "FM",     "RATIO", "DRIVE", "STEPS", "GRIT",  "COMB",     "BELL", "GLASS"};
    return names[t < 0 || t >= kTables ? 0 : t];
}
// TABLE B's value: 0 is OFF, 1 + t is table t.
inline int tableOfB(int value) { return value <= 0 || value > kTables ? -1 : value - 1; }
inline const char* tableBName(int value) { return tableOfB(value) < 0 ? "OFF" : tableName(tableOfB(value)); }

// ---- settings ------------------------------------------------------------------------------
// Ids are appended, never reordered: songs and presets store them by number.
enum Setting {
    TableA,       // 0..15: A's table (a choice)
    PositionA,    // 0..100: where in A's table (positionPlace), read as the FRAME
    TableB,       // 0..16: OFF, then the tables (a choice; tableOfB)
    PositionB,    // 0..100
    Mix,          // 0..100: A only at 0, B only at 100, crossfaded straight
    BSemi,        // -24..24: B against A, semitones
    BFine,        // -50..50: B against A, cents
    Sub,          // 0..100: a sine an octave under A, read in DB (0 OFF)
    Octave,       // -2..2
    Tune,         // -100..100 cents
    Unison,       // 1..4: streams an oscillator (a choice)
    Detune,       // 0..100: the streams' spread, read +- CENTS (detuneCents)
    Width,        // 0..100: the streams' spread apart in the sides
    Filter,   // 0..4: OFF, LOW 12, LOW 24, BAND, HIGH (a choice)
    Cutoff,       // 0..100: read HZ, 20 HZ to 20 KHZ (cutoffHz)
    Resonance,    // 0..100: Q 0.5 to 20 (resonanceQ)
    KeyTrack,     // 0..100: how far CUTOFF follows the key
    Attack,       // 0..100: the AMP envelope, the shared scale (envelope.hpp)
    Decay,        // 0..100
    Sustain,      // 0..100
    Release,      // 0..100
    EnvAttack,    // 0..100: the MOD ENV, the same scale
    EnvDecay,     // 0..100
    EnvSustain,   // 0..100
    EnvRelease,   // 0..100
    EnvPosition,  // -100..100: the MOD ENV on POSITION, read as FRAMES (+- the whole table)
    EnvCutoff,    // -100..100: the MOD ENV on CUTOFF, read +- OCT (envCutoffOctaves)
    Velocity,     // 0..100: how much a key's velocity sets its level, 0 OFF
    Level,        // -24..6 dB
    kSettings
};
constexpr int kSettingRoom = 40; // a patch has room for eleven more, at 0
static_assert(kSettings == 29, "ids are appended, never reordered");
enum FilterKind { kFilterOff, kLow12, kLow24, kBand, kHigh, kFilters };
inline const char* filterName(int f) {
    static const char* const names[kFilters] = {"OFF", "LOW 12", "LOW 24", "BAND", "HIGH"};
    return names[f < 0 || f >= kFilters ? 0 : f];
}
constexpr int kUnisonMost = 4;

inline int settingMin(int id) {
    switch (id) {
    case BSemi: return -24;
    case BFine: return -50;
    case Octave: return -2;
    case Tune: return -100;
    case Unison: return 1;
    case EnvPosition:
    case EnvCutoff: return -100;
    case Level: return -24;
    default: return 0;
    }
}
inline int settingMax(int id) {
    switch (id) {
    case TableA: return kTables - 1;
    case TableB: return kTables;
    case BSemi: return 24;
    case BFine: return 50;
    case Octave: return 2;
    case Unison: return kUnisonMost;
    case Filter: return kFilters - 1;
    case Level: return 6;
    default: return 100;
    }
}
// The usual value of each (the plan's section 1), on the scales below: ATTACK 8 reads 2.1 MS,
// DECAY / RELEASE / ENV RELEASE 50 read 200 MS, ENV DECAY 60 reads 502 MS.
inline int settingUsual(int id) {
    static const int usual[kSettings] = {Analog, 0,  0,   0,  50, 0, 0,  0, 0, 0, 1,  20, 50, kLow12, 100,
                                         0,      50, 8,   50, 100, 50, 8, 60, 0, 50, 0, 0,  50, 0};
    return id < 0 || id >= kSettings ? 0 : usual[id];
}
inline int settingHeld(int id, int value) {
    const int lo = settingMin(id), hi = settingMax(id);
    return value < lo ? lo : (value > hi ? hi : value);
}
// The settings read at a key's strike, never while it sounds (a switch would step the wave, or
// give a key streams it did not take from the pool).
inline bool readAtStrike(int id) { return id == TableA || id == TableB || id == Unison || id == Width; }

// ---- macros --------------------------------------------------------------------------------
// Lane by lane (the plan's section 5). No macro is a choice: each takes a MOD.
enum Macro { MacroPosition, MacroMix, MacroCutoff, MacroResonance, MacroEnvPosition, MacroEnvCutoff, MacroDetune, MacroRelease, kMacros };
static_assert(kMacros == 8, "every synth has eight macros (kSynthMacros)");
inline int macroSetting(int macro) {
    static const int setting[kMacros] = {PositionA, Mix, Cutoff, Resonance, EnvPosition, EnvCutoff, Detune, Release};
    return setting[macro < 0 || macro >= kMacros ? 0 : macro];
}
// The second setting a macro bends, -1 for none: POSITION bends A's and B's together.
inline int macroAlsoBends(int macro) { return macro == MacroPosition ? PositionB : -1; }
// The house rule (PHYSIC's, ACID's, CLOUD's): a macro below its middle slides its setting from the
// patch's value down to the setting's least, above it up to its most. 0.5 is the patch exactly.
inline float macroBent(float patch, float macro, float least, float most) {
    if (macro == 0.5f) return patch;
    return macro < 0.5f ? least + (patch - least) * (macro * 2.f) : patch + (most - patch) * ((macro - 0.5f) * 2.f);
}
// A macro's new value is slid in over exactly this many samples, counted from when it was set:
// the engine gives a synth's macros on every kModMacroTicks'th MOD tick, so a MOD on POSITION is
// a line from one to the next (10.7 ms late at 48 kHz), never a staircase.
constexpr int kMacroSlideSamples = kModTick * kModMacroTicks;
static_assert(kMacroSlideSamples == 512, "the plan's figure");
inline float macroSlid(float from, float to, long since) {
    if (since >= kMacroSlideSamples) return to;
    if (since <= 0) return from;
    return from + (to - from) * (static_cast<float>(since) / static_cast<float>(kMacroSlideSamples));
}

// ---- the readings --------------------------------------------------------------------------
// Each from the dial (a float: a macro bends it between whole values).
inline double dialHeld(double d, double lo = 0.0, double hi = 100.0) { return !(d > lo) ? lo : (d > hi ? hi : d); }
// POSITION: a place along a table of `frames`, 0 the first frame exactly, 100 the last exactly.
inline double positionPlace(double d, int frames) { return dialHeld(d) / 100.0 * static_cast<double>(frames - 1); }
// The FRAME it reads as, counted from 1 (1.0 to 32.0 on a built-in table).
inline double positionFrame(double d, int frames) { return 1.0 + positionPlace(d, frames); }
// ENV > POSITION: frames at the envelope's top, +- the whole table at +-100.
inline double envPositionFrames(double d, int frames) { return dialHeld(d, -100.0, 100.0) / 100.0 * static_cast<double>(frames - 1); }
inline double mixShare(double d) { return dialHeld(d) / 100.0; }
inline double bCents(int semi, int fine) { return 100.0 * semi + fine; }
// SUB: a share of full, read in DB; 0 is OFF.
inline double subGain(double d) { return dialHeld(d) / 100.0; }
inline double subDb(double d) { return d > 0.0 ? 20.0 * std::log10(subGain(d)) : -INFINITY; }
// DETUNE: squared, so the first part of the dial is a slow beat; the outer streams +-50 cents at 100.
constexpr double kDetuneMostCents = 50.0;
inline double detuneCents(double d) { const double x = dialHeld(d) / 100.0; return kDetuneMostCents * x * x; }
inline double widthShare(double d) { return dialHeld(d) / 100.0; }
// CUTOFF: 20 HZ to 20 KHZ, the same ratio each step.
constexpr double kCutoffLeastHz = 20.0, kCutoffMostHz = 20000.0;
inline double cutoffHz(double d) { return kCutoffLeastHz * std::pow(kCutoffMostHz / kCutoffLeastHz, dialHeld(d) / 100.0); }
// RESONANCE: Q 0.5 (none) to 20, the same ratio each step; the filter takes k = 1 / Q.
constexpr double kQLeast = 0.5, kQMost = 20.0;
inline double resonanceQ(double d) { return kQLeast * std::pow(kQMost / kQLeast, dialHeld(d) / 100.0); }
// KEY TRACK: octaves the cutoff moves for `key`, 100% an octave a key-octave, about key 60.
inline double keyTrackOctaves(double d, double key) { return dialHeld(d) / 100.0 * (key - 60.0) / 12.0; }
// ENV > CUTOFF: octaves at the envelope's top, +-6 at +-100.
constexpr double kEnvCutoffMostOctaves = 6.0;
inline double envCutoffOctaves(double d) { return dialHeld(d, -100.0, 100.0) / 100.0 * kEnvCutoffMostOctaves; }
// Where the cutoff is: the dial, KEY TRACK and the MOD ENV added in octaves, held to 20 Hz and
// 0.45 of the rate (the filter's own bound, fx/svf.hpp).
inline double cutoffAt(double dial, double keyTrack, double key, double envCutoff, double env, double rate) {
    const double hz = cutoffHz(dial) * std::exp2(keyTrackOctaves(keyTrack, key) + envCutoffOctaves(envCutoff) * env);
    const double most = 0.45 * rate;
    return !(hz > kCutoffLeastHz) ? kCutoffLeastHz : (hz > most ? most : hz);
}
inline double tuneCents(int value) { return static_cast<double>(value); }
inline double levelGain(double db) { return std::pow(10.0, dialHeld(db, -24.0, 6.0) / 20.0); }
// VELOCITY: 0 OFF (every key one level), 100 the key's level is its velocity.
inline double velocityGain(double dial, double velocity) {
    const double v = !(velocity > 0.0) ? 0.0 : (velocity > 1.0 ? 1.0 : velocity);
    return 1.0 - dialHeld(dial) / 100.0 * (1.0 - v);
}
// A key's pitch: OCTAVE, TUNE and what a glide still has to go, in semitones; B's is A's times
// bRatio. The SUB is half A's.
inline double keyHz(double key, int octave, double tune, double slideSemis) {
    return 440.0 * std::exp2((key - 69.0 + 12.0 * octave + tune / 100.0 + slideSemis) / 12.0);
}
inline double bRatio(int semi, int fine) { return std::exp2(bCents(semi, fine) / 1200.0); }

// ---- the sizes a frame is kept at ----------------------------------------------------------
// One SIZE every half octave. Size m holds harmonics 1 to sizeHarmonics(m) = floor(512 / 2^(m/2))
// in sizeLength(m) points: the power of two at or over 32 times the harmonics, held to 128..2048.
// A straight-line read between points makes images of each harmonic about 12 dB further down for
// each doubling of the points it has (sinc squared): at 8 points a frame rich in its top harmonics
// (FOLD) aliased 39 dB under its loudest at key 96 (batch 1, measured), at 32 the sizes from H 64
// down are 47 dB or more. The four largest sizes (low keys) are held at 2048 points.
constexpr int kSizes = 18;
constexpr int kTopHarmonics = 512;
constexpr int kPointsPerHarmonic = 32;
constexpr int kLengthLeast = 128, kLengthMost = 2048;
inline int sizeHarmonics(int m) {
    static const int h[kSizes] = {512, 362, 256, 181, 128, 90, 64, 45, 32, 22, 16, 11, 8, 5, 4, 2, 2, 1};
    return h[m < 0 ? 0 : (m >= kSizes ? kSizes - 1 : m)];
}
inline int sizeLength(int m) {
    int length = kLengthLeast;
    while (length < kPointsPerHarmonic * sizeHarmonics(m) && length < kLengthMost) length *= 2;
    return length;
}
inline int sizeBits(int m) {
    int bits = 0;
    while ((1 << bits) < sizeLength(m)) ++bits;
    return bits;
}
// Each size is kept with one guard point before and two after (a read never wraps by hand).
constexpr int kGuardBefore = 1, kGuardAfter = 2;
inline long frameFloats() {
    long n = 0;
    for (int m = 0; m < kSizes; ++m) n += sizeLength(m) + kGuardBefore + kGuardAfter;
    return n;
}
// What folds back over half the rate must land over 20 kHz: a size's top harmonic may reach
// rate - 20 kHz (28 kHz at 48 kHz). At 48 kHz the next size down then drops only harmonics over
// 19.8 kHz, so a size changing under a glide is not heard, and needs no crossfade.
constexpr double kHeardTopHz = 20000.0;
inline double foldCeiling(double rate) { return rate - kHeardTopHz; }
// The size a stream reads at `hz` (the highest pitch it reaches in the tick): the first whose top
// harmonic is at or under the ceiling. Past every size (a pitch over the ceiling), the last.
inline int sizeFor(double hz, double rate) {
    const double ceiling = foldCeiling(rate);
    for (int m = 0; m < kSizes; ++m)
        if (static_cast<double>(sizeHarmonics(m)) * hz <= ceiling) return m;
    return kSizes - 1;
}

// ---- POSITION to the two frames read -----------------------------------------------------------
// `place` in frames (0 .. frames - 1): the first of the two frames read and how far towards the
// second. The last frame is read as the pair before it at 1 exactly, so the pair is always there.
inline void framePlace(double place, int frames, int* first, double* toward) {
    if (frames < kFramesLeast) frames = kFramesLeast;
    const double last = static_cast<double>(frames - 1);
    const double p = !(place > 0.0) ? 0.0 : (place > last ? last : place);
    int j = static_cast<int>(p);
    if (j > frames - 2) j = frames - 2;
    *first = j;
    *toward = p - static_cast<double>(j);
}

// ---- the phase -----------------------------------------------------------------------------------
// A stream's phase is a 32-bit whole number: a whole turn is 2^32, one integer add a sample, the
// same in any blocks. The step for `hz`, held under half a turn.
inline std::uint32_t phaseStep(double hz, double rate) {
    if (!(hz > 0.0) || !(rate > 0.0)) return 0;
    double step = std::round(hz / rate * 4294967296.0);
    if (step > 2147483647.0) step = 2147483647.0;
    return static_cast<std::uint32_t>(step);
}
// The point of a size of 2^bits points the phase is at, and how far on to the next (0 up to, never
// reaching, 1). The fraction is the bits under the point, only their top 23 taken: all of them made
// a float rounds up to 1.0 just under a whole point (a read past the pair).
inline std::uint32_t readIndex(std::uint32_t phase, int bits) { return phase >> (32 - bits); }
inline float readFraction(std::uint32_t phase, int bits) {
    return static_cast<float>((phase << bits) >> 9) * (1.f / 8388608.f);
}

// ---- the stream pool -------------------------------------------------------------------------------
constexpr int kKeys = 6;             // sounding at most
constexpr int kKeySlots = 2 * kKeys; // and as many fading out (5 ms)
constexpr int kStreams = 24;         // streams for the track: the cost's ceiling
// What a key takes from the pool at its strike: UNISON streams for A, as many for B when B is on.
inline int streamsPerKey(int unison, bool bOn) {
    const int u = unison < 1 ? 1 : (unison > kUnisonMost ? kUnisonMost : unison);
    return u * (bOn ? 2 : 1);
}
// How many keys the pool lets sound together (the page's KEYS n).
inline int keysFor(int unison, bool bOn) {
    const int n = kStreams / streamsPerKey(unison, bOn);
    return n > kKeys ? kKeys : n;
}

// ---- UNISON's spread ---------------------------------------------------------------------------------
// Stream i of `unison`, evenly from -1 to +1 (0 for one alone).
inline double streamSpread(int i, int unison) {
    if (unison <= 1) return 0.0;
    return 2.0 * static_cast<double>(i) / static_cast<double>(unison - 1) - 1.0;
}
inline double streamCents(int i, int unison, double detune) { return detune * streamSpread(i, unison); }
inline double streamSide(int i, int unison, double width) { return width * streamSpread(i, unison); }
// A side -1 (left) .. +1 (right) as two gains, equal power (detuned streams are unlike each other):
// the middle 0.7071 each side.
inline void sideGains(double side, double* left, double* right) {
    const double s = !(side > -1.0) ? -1.0 : (side > 1.0 ? 1.0 : side);
    const double angle = (s + 1.0) * (3.14159265358979323846 / 4.0);
    *left = std::cos(angle);
    *right = std::sin(angle);
}

// ---- the rolls ---------------------------------------------------------------------------------------
// Where stream i of oscillator `osc` (0 A, 1 B) starts its turn, for the key struck with `seed`
// (the engine's noteSeed). One stream alone starts at 0, the same attack every time; streams
// together start where a hash of the three puts them (never a generator walked), or they would
// flange at every attack.
inline std::uint32_t streamPhase(std::uint32_t seed, int osc, int i, int unison) {
    if (unison <= 1) return 0;
    return chordHash(seed ^ chordHash(static_cast<std::uint32_t>(osc) * 0x9E3779B1u + static_cast<std::uint32_t>(i) * 0x85EBCA77u + 0x5BD1E995u));
}
// A key played by hand (no seed handed over): the count of such keys since allNotesOff.
inline std::uint32_t liveSeed(std::uint32_t n) { return chordHash(n * 0x2545F491u + 0x6A09E667u); }

} // namespace morph
} // namespace apotheca
