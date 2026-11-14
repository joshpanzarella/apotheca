#pragma once
// MOD: a track's two modulators (docs/plans/modulation-plan.md). Each is OFF, an LFO or a
// STEPPER, aimed at one setting with a signed depth, in time with the song.
//
//   LFO      a slow wave that turns a setting up and down by itself, in time.
//   STEPPER  a row of 1 to 16 values, one held for each note length, then the next; dice
//            (CHANCE) can play a fresh roll in place of a written value.
//
// This header is the whole rule, with no engine and no SDL in it, so the engine and the
// screens ask the same thing and the tests can work values out by hand. Every value is worked
// out from WHERE the song is (its position, in steps since Play), never from the value before:
// the same on any helper count, block length, tempo change and scene move. A roll is a hash of
// where and which time round (chordSeed / chordUnit), never a generator walked.
//
// What a setting is given: modGiven(centre, the MODs' offsets added), held to 0..1. The centre
// is the knob or its automation; a MOD adds depth x shape on top. With nothing added the centre
// goes out as it is, which is what keeps a song with no MOD the same to the bit.
//
// The numbers here are in songs (a `mod` line): appended, never reordered or reused.

#include <cmath>
#include <cstdint>

#include "apotheca/chord_rule.hpp"

namespace apotheca {

constexpr int kModsPerTrack = 2; // MOD 1 and MOD 2 (owner's call 1: two)
constexpr int kModSteps = 16;    // a STEPPER's row at its longest

// ---- kinds ----------------------------------------------------------------------------------
enum ModKind { kModOff = 0, kModLfo = 1, kModStepper = 2, kModKinds = 3 };
inline int modKind(int n) { return n >= 0 && n < kModKinds ? n : kModOff; } // a number nobody knows is OFF
inline const char* modKindName(int kind) {
    static const char* const names[kModKinds] = {"OFF", "LFO", "STEPPER"};
    return names[modKind(kind)];
}

// ---- targets: the automation lane numbers, so one numbering serves the song file, a TYPE
// change and the engine (engine.hpp, kAutoLanes) ------------------------------------------------
constexpr int kModTargetNone = -1;
constexpr int kModFirstMacro = 0;  // 0..7 the synth's eight macros
constexpr int kModFirstEffect = 8; // 8 + slot * 4 + setting: the track's three slots
constexpr int kModFirstSend = 20;  // 20 ECHO, 21 SPACE
constexpr int kModTargetLanes = 22; // lanes 22 and 23 are spare: kept for MOD 1 / MOD 2 DEPTH (batch 7), never targets
constexpr int kModTargetLevel = 32; // the track's LEVEL and PAN: not lanes (batch 8; not taken yet)
constexpr int kModTargetPan = 33;
// A target the engine takes today. LEVEL and PAN (and anything else) read as no target.
inline bool modTargetKnown(int target) { return target >= 0 && target < kModTargetLanes; }
inline bool modTargetIsMacro(int target) { return target >= kModFirstMacro && target < kModFirstEffect; }
inline bool modTargetIsEffect(int target) { return target >= kModFirstEffect && target < kModFirstSend; }
inline bool modTargetIsSend(int target) { return target >= kModFirstSend && target < kModTargetLanes; }
inline int modTargetSlot(int target) { return modTargetIsEffect(target) ? (target - kModFirstEffect) / 4 : -1; }
inline int modTargetSetting(int target) { return modTargetIsEffect(target) ? (target - kModFirstEffect) % 4 : -1; }

// ---- rates: note lengths only, in time with the song. A step is a sixteenth. --------------------
// Each is a cycle (an LFO) or one value held (a STEPPER) of num / den steps, kept as a fraction
// so a triplet is exact.
enum ModRate {
    kModRateHalfStep = 0, kModRateStep, kModRateEighthTriplet, kModRateEighth, kModRateQuarterTriplet, kModRateQuarter,
    kModRateDottedQuarter, kModRateHalf, kModRateBar, kModRateTwoBars, kModRateFourBars, kModRateEightBars, kModRates
};
struct ModRateInfo {
    int num, den; // steps a cycle: num / den
    const char* name;
};
inline const ModRateInfo& modRateInfo(int rate) {
    static const ModRateInfo table[kModRates] = {
        {1, 2, "1/2 STEP"}, {1, 1, "1 STEP"}, {4, 3, "1/8T"},   {2, 1, "1/8"},     {8, 3, "1/4T"},    {4, 1, "1/4"},
        {6, 1, "1/4."},     {8, 1, "1/2"},    {16, 1, "1 BAR"}, {32, 1, "2 BARS"}, {64, 1, "4 BARS"}, {128, 1, "8 BARS"},
    };
    return table[rate >= 0 && rate < kModRates ? rate : kModRateBar];
}
constexpr int kModRateUsual = kModRateBar;
inline int modRate(int n) { return n >= 0 && n < kModRates ? n : kModRateUsual; } // a number nobody knows is 1 BAR
inline double modRateSteps(int rate) { const ModRateInfo& r = modRateInfo(rate); return static_cast<double>(r.num) / r.den; }
// The order a player steps through them, apart from the numbers (as kGlideOrder): the same today.
inline int modRateAt(int place) { return place < 0 ? 0 : (place >= kModRates ? kModRates - 1 : place); }
inline int modRatePlace(int rate) { return modRate(rate); }

// ---- LFO shapes ----------------------------------------------------------------------------------
enum ModShape { kModSine = 0, kModTriangle, kModRampUp, kModRampDown, kModSquare, kModRandom, kModDrift, kModShapes };
inline int modShape(int n) { return n >= 0 && n < kModShapes ? n : kModSine; }
inline const char* modShapeName(int shape) {
    static const char* const names[kModShapes] = {"SINE", "TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "RANDOM", "DRIFT"};
    return names[modShape(shape)];
}
// PHASE: where in its cycle the LFO is at the song's start, in quarters (two MODs a quarter apart).
constexpr int kModPhases = 4;
inline int modPhase(int n) { return n >= 0 && n < kModPhases ? n : 0; }

// RESTART: what the position counts from. Only PLAY today (song position since Play); CELL and
// NOTE later (the owner's call).
enum ModRestart { kModRestartPlay = 0, kModRestarts = 1 };
inline int modRestart(int n) { return n >= 0 && n < kModRestarts ? n : kModRestartPlay; }

// ---- the engine's tick ---------------------------------------------------------------------------
// A MOD's value is worked out on a tick every kModTick samples, counted from each step's first
// sample; a synth's macros only every kModMacroTicks'th (each change works a whole tone out
// again: QMOD about a microsecond). The engine's runMods uses them; here so the tests hold one number.
constexpr int kModTick = 64;
constexpr int kModMacroTicks = 8;

// ---- the settings of one MOD ---------------------------------------------------------------------
struct ModSettings {
    int kind = kModOff;
    int target = kModTargetNone;
    float depth = 0.f; // -1..1: -100%..+100%; negative turns the other way
    int rate = kModRateUsual;
    int shape = kModSine;   // an LFO's
    int restart = kModRestartPlay;
    int phase = 0;          // an LFO's, in quarters
    int length = kModSteps; // a STEPPER's: 1..16 steps in its row
    float slew = 0.f;       // a STEPPER's: 0..1 of a held value's time spent gliding into it
    float chance = 0.f;     // a STEPPER's: 0..1, the chance a step plays a fresh roll
    float steps[kModSteps] = {}; // a STEPPER's row, -1..1
    bool on() const { return modKind(kind) != kModOff && modTargetKnown(target) && depth != 0.f; }
};
inline int modLength(int n) { return n < 1 ? 1 : (n > kModSteps ? kModSteps : n); }

// ---- one field at a time: what the engine's SetMod carries (b = the field, value = its value) ----
// The numbers are in commands, and batch 2's song file follows them: appended, never reordered.
enum ModField {
    kModFieldKind = 0, kModFieldTarget, kModFieldDepth, kModFieldRate, kModFieldShape, kModFieldRestart, kModFieldPhase,
    kModFieldLength, kModFieldSlew, kModFieldChance, kModFields
};
// A float as a whole number, or -1 where it is none (not a number, or too far out to be one).
inline int modWhole(float v) { return v >= -1e6f && v <= 1e6f ? static_cast<int>(std::floor(v + 0.5f)) : -1; }
inline float modHeld(float v, float lo, float hi) { return v >= lo ? (v <= hi ? v : hi) : lo; } // not a number: lo
// Set one field, held to what it can be (a number nobody knows reads as OFF, no target, 1 BAR,
// SINE, PLAY, 0; a depth -1..1; a share 0..1; not a number as the least).
inline void modSet(ModSettings& m, int field, float value) {
    switch (field) {
    case kModFieldKind: m.kind = modKind(modWhole(value)); break;
    case kModFieldTarget: m.target = modTargetKnown(modWhole(value)) ? modWhole(value) : kModTargetNone; break;
    case kModFieldDepth: m.depth = value == value ? modHeld(value, -1.f, 1.f) : 0.f; break;
    case kModFieldRate: m.rate = modRate(modWhole(value)); break;
    case kModFieldShape: m.shape = modShape(modWhole(value)); break;
    case kModFieldRestart: m.restart = modRestart(modWhole(value)); break;
    case kModFieldPhase: m.phase = modPhase(modWhole(value)); break;
    case kModFieldLength: m.length = modLength(modWhole(value)); break;
    case kModFieldSlew: m.slew = modHeld(value, 0.f, 1.f); break;
    case kModFieldChance: m.chance = modHeld(value, 0.f, 1.f); break;
    default: break;
    }
}
inline float modGet(const ModSettings& m, int field) {
    switch (field) {
    case kModFieldKind: return static_cast<float>(m.kind);
    case kModFieldTarget: return static_cast<float>(m.target);
    case kModFieldDepth: return m.depth;
    case kModFieldRate: return static_cast<float>(m.rate);
    case kModFieldShape: return static_cast<float>(m.shape);
    case kModFieldRestart: return static_cast<float>(m.restart);
    case kModFieldPhase: return static_cast<float>(m.phase);
    case kModFieldLength: return static_cast<float>(m.length);
    case kModFieldSlew: return m.slew;
    case kModFieldChance: return m.chance;
    default: return 0.f;
    }
}
inline void modSetStep(ModSettings& m, int step, float value) {
    if (step >= 0 && step < kModSteps) m.steps[step] = value == value ? modHeld(value, -1.f, 1.f) : 0.f;
}

// ---- where the song is -------------------------------------------------------------------------
// The position in steps since Play, from the engine's step clock: `stepsFired` steps begun (the
// one playing included), `samplesPerStep`, and `untilStep` the samples left before the next one
// (fractional: a step fires on the first sample at or after its time). Read it BEFORE a chunk is
// taken off untilStep, or it is the chunk's end. Equal to samples since Play / samplesPerStep.
inline double modPosition(long stepsFired, double samplesPerStep, double untilStep) {
    return static_cast<double>(stepsFired - 1) + (samplesPerStep - untilStep) / samplesPerStep;
}

// ---- the rolls ---------------------------------------------------------------------------------
// One place of a STEPPER's row (or one cycle of an LFO) one time round: the track, the place, and
// which time round. Bar 0 is a stepper's row, bar 1 an LFO's cycles, so the two never roll alike.
// `what` within the MOD: kModRollChance, kModRollValue.
enum ModRollWhat { kModRollChance = 0, kModRollValue = 1 };
inline std::uint32_t modSeed(int track, int bar, int place, long timesRound) { return chordSeed(track, bar, place, timesRound); }
inline double modRoll(std::uint32_t seed, int mod, int what) { return chordSigned(seed, kRollMod, mod * 32 + what); } // -1 up to 1
inline double modRollUnit(std::uint32_t seed, int mod, int what) { return chordUnit(seed, kRollMod, mod * 32 + what); } // 0 up to 1

// floor(x) as a whole number, for cycle counts (which go below 0 before the first step).
inline long modFloor(double x) { return static_cast<long>(std::floor(x)); }

// ---- an LFO's value: -1..1 at a position in steps -------------------------------------------------
// The cycle is the rate's length, started at Play, moved on by PHASE quarters. SINE starts at 0
// going up, as do TRIANGLE; the RAMPs and SQUARE are at an edge. RANDOM holds one seeded value a
// cycle; DRIFT glides (a cosine) from each cycle's value to the next's.
inline double lfoValue(const ModSettings& m, double position, int track, int mod) {
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    const double cycles = position * r.den / r.num + 0.25 * modPhase(m.phase);
    const long whole = modFloor(cycles);
    const double p = cycles - static_cast<double>(whole); // 0 up to 1 in the cycle
    switch (modShape(m.shape)) {
    case kModTriangle: return p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    case kModRampUp: return 2.0 * p - 1.0;
    case kModRampDown: return 1.0 - 2.0 * p;
    case kModSquare: return p < 0.5 ? 1.0 : -1.0;
    case kModRandom: return modRoll(modSeed(track, 1, 0, whole), mod, kModRollValue);
    case kModDrift: {
        const double from = modRoll(modSeed(track, 1, 0, whole), mod, kModRollValue);
        const double to = modRoll(modSeed(track, 1, 0, whole + 1), mod, kModRollValue);
        return from + (to - from) * 0.5 * (1.0 - std::cos(3.14159265358979323846 * p));
    }
    default: return std::sin(2.0 * 3.14159265358979323846 * p);
    }
}

// ---- a STEPPER's value: -1..1 at a position in steps ---------------------------------------------
// One value of the row for each rate's length since Play, round and round LENGTH long. Each time
// round each place plays its written value, or a fresh roll with CHANCE. SLEW glides into a value
// over that part of its time, from the value before (worked out the same way, not remembered);
// the first value after Play does not glide in.
inline double stepperHeld(const ModSettings& m, long index, int track, int mod) {
    const int length = modLength(m.length);
    const long round = index >= 0 ? index / length : -((-index + length - 1) / length);
    const int place = static_cast<int>(index - round * length);
    const std::uint32_t seed = modSeed(track, 0, place, round);
    const float chance = m.chance > 0.f ? (m.chance < 1.f ? m.chance : 1.f) : 0.f;
    if (chance > 0.f && modRollUnit(seed, mod, kModRollChance) < chance) return modRoll(seed, mod, kModRollValue);
    const float v = m.steps[place];
    return v > 1.f ? 1.0 : (v < -1.f ? -1.0 : (v == v ? v : 0.0));
}
inline double stepValue(const ModSettings& m, double position, int track, int mod) {
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    const double at = position * r.den / r.num;
    const long index = modFloor(at);
    const double now = stepperHeld(m, index, track, mod);
    const float slew = m.slew > 0.f ? (m.slew < 1.f ? m.slew : 1.f) : 0.f;
    const double into = at - static_cast<double>(index);
    if (slew <= 0.f || index <= 0 || into >= slew) return now;
    const double before = stepperHeld(m, index - 1, track, mod);
    return before + (now - before) * (into / slew);
}

// The place of a STEPPER's row playing at a position (0..LENGTH-1), for the screen; -1 for an LFO.
inline int stepperPlace(const ModSettings& m, double position) {
    if (modKind(m.kind) != kModStepper) return -1;
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    const long index = modFloor(position * r.den / r.num);
    const int length = modLength(m.length);
    const long place = index % length;
    return static_cast<int>(place < 0 ? place + length : place);
}

// What one MOD adds at a position: depth x its shape (0 when OFF or aimed at nothing).
inline float modOffsetAt(const ModSettings& m, double position, int track, int mod) {
    if (!m.on()) return 0.f;
    const double shape = modKind(m.kind) == kModLfo ? lfoValue(m, position, track, mod) : stepValue(m, position, track, mod);
    const float depth = m.depth > 1.f ? 1.f : (m.depth < -1.f ? -1.f : m.depth);
    return static_cast<float>(depth * shape);
}

// ---- the combine ---------------------------------------------------------------------------------
// What a setting is given: its centre plus what the MODs aimed at it add (two on one target add,
// then one clamp), held to 0..1. Nothing added (0, or not a number): the centre exactly as it is,
// in the same call as before there were MODs. That is the bit-exact path.
inline float modGiven(float centre, float offset) {
    if (offset == 0.f || !(offset == offset)) return centre;
    const float v = centre + offset;
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
}

} // namespace apotheca
