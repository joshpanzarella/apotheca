#pragma once
// MOD: a track's two modulators (docs/plans/modulation-plan.md). Each is OFF, an LFO or a
// STEPPER, aimed at one setting with a signed depth, in time with the song.
//
//   LFO      a slow wave that turns a setting up and down by itself, in time (or FREE: at a
//            SPEED in Hz of its own, the tempo apart).
//   STEPPER  a row of 1 to 16 values, one held for each note length, then the next; dice
//            (CHANCE) can play a fresh roll in place of a written value.
//
// This header is the whole rule, with no engine and no SDL in it, so the engine and the
// screens ask the same thing and the tests can work values out by hand. Every value is worked
// out from WHERE the song is (its position, in steps since Play; a FREE LFO's in seconds since
// Play), never from the value before: the same on any helper count, block length, tempo change
// and scene move. A roll is a hash of
// where and which time round (chordSeed / chordUnit), never a generator walked.
//
// What a setting is given: modGiven(centre, the MODs' offsets added), held to 0..1. The centre
// is the knob or its automation; a MOD adds depth x shape on top. With nothing added the centre
// goes out as it is, which is what keeps a song with no MOD the same to the bit.
//
// The numbers here are in songs (a `mod` line): appended, never reordered or reused.

#include <cmath>
#include <cstdint>
#include <limits>

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
constexpr int kModTargetLanes = 22; // lanes 22 and 23 are MOD 1's and MOD 2's DEPTH (batch 7), never targets
// A MOD's DEPTH as an automation lane (batch 7, owner's call 10): lane 22 is MOD 1's, 23 MOD 2's,
// written with WRITE like any lane, so a wobble can be in one scene only. A lane holds 0..1, so
// 0.5 is no depth, 0 is -100% and 1 is +100%: the scale a DEPTH has in the song file. A scene with
// nothing written on it goes back to the MOD's own DEPTH, as every lane goes back to its knob.
constexpr int kModDepthLane = kModTargetLanes;
inline int modDepthLane(int mod) { return kModDepthLane + mod; }
inline float modDepthToLane(float depth) {
    const float d = depth == depth ? (depth > 1.f ? 1.f : (depth < -1.f ? -1.f : depth)) : 0.f;
    return 0.5f * (d + 1.f);
}
inline float modDepthFromLane(float v) {
    const float d = 2.f * v - 1.f;
    return d == d ? (d > 1.f ? 1.f : (d < -1.f ? -1.f : d)) : 0.f;
}
constexpr int kModTargetLevel = 32; // the track's LEVEL and PAN on the mixer: not lanes, not automated (batch 8)
constexpr int kModTargetPan = 33;
inline bool modTargetIsMix(int target) { return target == kModTargetLevel || target == kModTargetPan; }
// A target the engine takes: a lane that is not a DEPTH lane, LEVEL or PAN. Anything else reads as no target.
inline bool modTargetKnown(int target) { return (target >= 0 && target < kModTargetLanes) || modTargetIsMix(target); }
inline bool modTargetIsMacro(int target) { return target >= kModFirstMacro && target < kModFirstEffect; }
inline bool modTargetIsEffect(int target) { return target >= kModFirstEffect && target < kModFirstSend; }
inline bool modTargetIsSend(int target) { return target >= kModFirstSend && target < kModTargetLanes; }
inline int modTargetSlot(int target) { return modTargetIsEffect(target) ? (target - kModFirstEffect) / 4 : -1; }
inline int modTargetSetting(int target) { return modTargetIsEffect(target) ? (target - kModFirstEffect) % 4 : -1; }

// ---- rates: note lengths, in time with the song. A step is a sixteenth. ------------------------
// Each is a cycle (an LFO) or one value held (a STEPPER) of num / den steps, kept as a fraction
// so a triplet is exact. The last, FREE (owner, 2026-11-15), is an LFO's only: its cycle is its
// SPEED in Hz, counted in seconds since Play, so a tempo change does not move it. A STEPPER's
// list stops at 8 BARS (kModTempoRates); one that reads FREE (a file written by hand) plays as
// 1 BAR (its row's num / den), and the app makes it 1 BAR (mods.hpp, modHeldFor).
enum ModRate {
    kModRateHalfStep = 0, kModRateStep, kModRateEighthTriplet, kModRateEighth, kModRateQuarterTriplet, kModRateQuarter,
    kModRateDottedQuarter, kModRateHalf, kModRateBar, kModRateTwoBars, kModRateFourBars, kModRateEightBars, kModRateFree, kModRates
};
constexpr int kModTempoRates = kModRateFree; // the rates in time with the song: all a STEPPER has
struct ModRateInfo {
    int num, den; // steps a cycle: num / den
    const char* name;
};
inline const ModRateInfo& modRateInfo(int rate) {
    static const ModRateInfo table[kModRates] = {
        {1, 2, "1/2 STEP"}, {1, 1, "1 STEP"}, {4, 3, "1/8T"},   {2, 1, "1/8"},     {8, 3, "1/4T"},    {4, 1, "1/4"},
        {6, 1, "1/4."},     {8, 1, "1/2"},    {16, 1, "1 BAR"}, {32, 1, "2 BARS"}, {64, 1, "4 BARS"}, {128, 1, "8 BARS"},
        {16, 1, "FREE"}, // (its length is its SPEED's: lfoCycles. The 16 steps are only for a STEPPER that reads it)
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
// A FREE LFO has no PHASE (its row is SPEED); it keeps the number for when it is in time again.
constexpr int kModPhases = 4;
inline int modPhase(int n) { return n >= 0 && n < kModPhases ? n : 0; }

// SPEED: a FREE LFO's cycles a second, 0.02 Hz (50 s a cycle) to 20 Hz. Kept as Hz, an amount: the
// stick turns it smoothly on a log scale (a free LFO is set by ear, against nothing in time), the
// D-pad clicks along kModSpeedClicks, numbers that read well, about a third of an octave apart.
// Its usual: 1 Hz.
constexpr float kModSpeedLeast = 0.02f, kModSpeedMost = 20.f, kModSpeedUsual = 1.f;
constexpr int kModSpeedClickCount = 31;
inline float modSpeedClick(int i) {
    static const float clicks[kModSpeedClickCount] = {0.02f, 0.025f, 0.03f, 0.04f, 0.05f, 0.06f, 0.08f, 0.1f, 0.12f, 0.15f, 0.2f,
                                                      0.25f, 0.3f,   0.4f,  0.5f,  0.6f,  0.8f,  1.f,   1.2f, 1.5f,  2.f,   2.5f,
                                                      3.f,   4.f,    5.f,   6.f,   8.f,   10.f,  12.f, 15.f,  20.f};
    return clicks[i < 0 ? 0 : (i >= kModSpeedClickCount ? kModSpeedClickCount - 1 : i)];
}
// Held to 0.02..20; not a number: the least (as every amount here).
inline float modSpeed(float hz) { return hz >= kModSpeedLeast ? (hz <= kModSpeedMost ? hz : kModSpeedMost) : kModSpeedLeast; }
// A click up or down from `hz`: the next number of the list past it (one between two goes to the
// next, not a click on). At an end: `hz` as it is (the ends stop).
inline float modSpeedStep(float hz, int by) {
    hz = modSpeed(hz);
    if (by > 0) {
        for (int i = 0; i < kModSpeedClickCount; ++i)
            if (modSpeedClick(i) > hz * 1.0001f) return modSpeedClick(i);
    } else if (by < 0) {
        for (int i = kModSpeedClickCount - 1; i >= 0; --i)
            if (modSpeedClick(i) < hz * 0.9999f) return modSpeedClick(i);
    }
    return hz;
}
// Where `hz` is on the log scale, 0 (0.02) to 1 (20): the bar on the MOD tab.
inline float modSpeedPart(float hz) {
    return static_cast<float>(std::log(static_cast<double>(modSpeed(hz)) / kModSpeedLeast) / std::log(static_cast<double>(kModSpeedMost) / kModSpeedLeast));
}
// Turned by `octaves` on the log scale (the stick), held to its ends.
inline float modSpeedTurned(float hz, double octaves) { return modSpeed(static_cast<float>(static_cast<double>(modSpeed(hz)) * std::exp2(octaves))); }

// RESTART: what the position counts from. Only PLAY today (song position since Play); CELL and
// NOTE later (the owner's call).
enum ModRestart { kModRestartPlay = 0, kModRestarts = 1 };
inline int modRestart(int n) { return n >= 0 && n < kModRestarts ? n : kModRestartPlay; }

// ---- the engine's tick ---------------------------------------------------------------------------
// A MOD's value is worked out on a tick every kModTick samples, counted from each step's first
// sample; a synth's macros only every kModMacroTicks'th (each change works a whole tone out
// again: Phazed about a third of a microsecond). The engine's runMods uses them; here so the tests hold one number.
constexpr int kModTick = 64;
// A position that is none: the MOD does not run (Engine::modNow).
constexpr double kModNotRunning = std::numeric_limits<double>::quiet_NaN();
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
    float speed = kModSpeedUsual; // a FREE LFO's: Hz, 0.02..20 (kept, unused, while it is in time)
    float steps[kModSteps] = {}; // a STEPPER's row, -1..1
    bool on() const { return modKind(kind) != kModOff && modTargetKnown(target) && depth != 0.f; }
    // The engine runs a MOD that has a kind and a target at ANY depth: its DEPTH lane may be
    // automated (batch 7), so a MOD set at 0% may wobble in the scene whose lane says so.
    bool runs() const { return modKind(kind) != kModOff && modTargetKnown(target); }
};
inline int modLength(int n) { return n < 1 ? 1 : (n > kModSteps ? kModSteps : n); }
// An LFO at RATE FREE: its position is seconds since Play, its cycle 1 / SPEED, no PHASE.
inline bool modFree(const ModSettings& m) { return modKind(m.kind) == kModLfo && modRate(m.rate) == kModRateFree; }
// The position a MOD runs at: `steps` since Play, or, a FREE LFO, `seconds` since Play.
inline double modPositionFor(const ModSettings& m, double steps, double seconds) { return modFree(m) ? seconds : steps; }

// ---- one field at a time: what the engine's SetMod carries (b = the field, value = its value) ----
// The numbers are in commands, and batch 2's song file follows them: appended, never reordered.
enum ModField {
    kModFieldKind = 0, kModFieldTarget, kModFieldDepth, kModFieldRate, kModFieldShape, kModFieldRestart, kModFieldPhase,
    kModFieldLength, kModFieldSlew, kModFieldChance, kModFieldSpeed, kModFields
};
// A float as a whole number, or -1 where it is none (not a number, or too far out to be one).
inline int modWhole(float v) { return v >= -1e6f && v <= 1e6f ? static_cast<int>(std::floor(v + 0.5f)) : -1; }
inline float modHeld(float v, float lo, float hi) { return v >= lo ? (v <= hi ? v : hi) : lo; } // not a number: lo
// Set one field, held to what it can be (a number nobody knows reads as OFF, no target, 1 BAR,
// SINE, PLAY, 0; a depth -1..1; a share 0..1; a SPEED 0.02..20 Hz; not a number as the least).
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
    case kModFieldSpeed: m.speed = modSpeed(value); break;
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
    case kModFieldSpeed: return m.speed;
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
inline std::uint32_t modSeed(int track, int bar, int place, long timesRound, std::uint32_t take = 0) { return takeSeed(chordSeed(track, bar, place, timesRound), take); } // (take: a bounce's, Engine::setTake)
inline double modRoll(std::uint32_t seed, int mod, int what) { return chordSigned(seed, kRollMod, mod * 32 + what); } // -1 up to 1
inline double modRollUnit(std::uint32_t seed, int mod, int what) { return chordUnit(seed, kRollMod, mod * 32 + what); } // 0 up to 1

// floor(x) as a whole number, for cycle counts (which go below 0 before the first step).
inline long modFloor(double x) { return static_cast<long>(std::floor(x)); }

// How many of its cycles an LFO has gone through at a position (PHASE included): its whole part is
// which cycle, the rest where in it. And back: the position at a count of cycles (the MOD tab draws
// the cycle playing from it, with a dot at now). A FREE LFO's position is in seconds (modPositionFor):
// SPEED cycles a second, from 0 at Play.
inline double lfoCycles(const ModSettings& m, double position) {
    if (modFree(m)) return position * static_cast<double>(modSpeed(m.speed));
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    return position * r.den / r.num + 0.25 * modPhase(m.phase);
}
inline double lfoPositionAt(const ModSettings& m, double cycles) {
    if (modFree(m)) return cycles / static_cast<double>(modSpeed(m.speed));
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    return (cycles - 0.25 * modPhase(m.phase)) * r.num / r.den;
}

// ---- an LFO's value: -1..1 at a position in steps (a FREE one's: in seconds) ----------------------
// The cycle is the rate's length, started at Play, moved on by PHASE quarters. SINE starts at 0
// going up, as do TRIANGLE; the RAMPs and SQUARE are at an edge. RANDOM holds one seeded value a
// cycle; DRIFT glides (a cosine) from each cycle's value to the next's.
inline double lfoValue(const ModSettings& m, double position, int track, int mod, std::uint32_t take = 0) {
    const double cycles = lfoCycles(m, position);
    const long whole = modFloor(cycles);
    const double p = cycles - static_cast<double>(whole); // 0 up to 1 in the cycle
    switch (modShape(m.shape)) {
    case kModTriangle: return p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    case kModRampUp: return 2.0 * p - 1.0;
    case kModRampDown: return 1.0 - 2.0 * p;
    case kModSquare: return p < 0.5 ? 1.0 : -1.0;
    case kModRandom: return modRoll(modSeed(track, 1, 0, whole, take), mod, kModRollValue);
    case kModDrift: {
        const double from = modRoll(modSeed(track, 1, 0, whole, take), mod, kModRollValue);
        const double to = modRoll(modSeed(track, 1, 0, whole + 1, take), mod, kModRollValue);
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
inline double stepperHeld(const ModSettings& m, long index, int track, int mod, std::uint32_t take = 0) {
    const int length = modLength(m.length);
    const long round = index >= 0 ? index / length : -((-index + length - 1) / length);
    const int place = static_cast<int>(index - round * length);
    const std::uint32_t seed = modSeed(track, 0, place, round, take);
    const float chance = m.chance > 0.f ? (m.chance < 1.f ? m.chance : 1.f) : 0.f;
    if (chance > 0.f && modRollUnit(seed, mod, kModRollChance) < chance) return modRoll(seed, mod, kModRollValue);
    const float v = m.steps[place];
    return v > 1.f ? 1.0 : (v < -1.f ? -1.0 : (v == v ? v : 0.0));
}
inline double stepValue(const ModSettings& m, double position, int track, int mod, std::uint32_t take = 0) {
    const ModRateInfo& r = modRateInfo(modRate(m.rate));
    const double at = position * r.den / r.num;
    const long index = modFloor(at);
    const double now = stepperHeld(m, index, track, mod, take);
    const float slew = m.slew > 0.f ? (m.slew < 1.f ? m.slew : 1.f) : 0.f;
    const double into = at - static_cast<double>(index);
    if (slew <= 0.f || index <= 0 || into >= slew) return now;
    const double before = stepperHeld(m, index - 1, track, mod, take);
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

// What one MOD adds at a position: `depth` x its shape (0 when OFF, aimed at nothing or at no
// depth). The engine hands it the depth now (the MOD's own, or its DEPTH lane's: batch 7), and the
// position in the MOD's own terms (modPositionFor: steps, or a FREE LFO's seconds).
inline float modOffsetAt(const ModSettings& m, double position, int track, int mod, float depth, std::uint32_t take = 0) {
    depth = depth > 1.f ? 1.f : (depth < -1.f ? -1.f : depth);
    if (!m.runs() || depth == 0.f || depth != depth) return 0.f; // (not a number: none)
    const double shape = modKind(m.kind) == kModLfo ? lfoValue(m, position, track, mod, take) : stepValue(m, position, track, mod, take);
    return static_cast<float>(depth * shape);
}
// ...at the MOD's own DEPTH.
inline float modOffsetAt(const ModSettings& m, double position, int track, int mod) { return modOffsetAt(m, position, track, mod, m.depth); }

// ---- the combine ---------------------------------------------------------------------------------
// LEVEL and PAN (batch 8) are not 0..1 settings, so they have their own combine, with the same
// promise: nothing added gives the mixer's value exactly as it is.
//   LEVEL: the track's gain times (1 + what is added), held to 0..2: at 100% a SINE swings from
//          silence up to twice the gain (+6 dB, the mixer's top step), about the mixer's level.
//   PAN:   the place (-1 hard left .. 1 hard right) plus what is added, held to -1..1: at 100% from
//          the middle it reaches either side.
constexpr float kModLevelMost = 2.f;
inline float modLevelGiven(float level, float offset) {
    if (offset == 0.f || !(offset == offset)) return level;
    const float times = 1.f + offset;
    return level * (times > 0.f ? (times < kModLevelMost ? times : kModLevelMost) : 0.f);
}
inline float modPanGiven(float pan, float offset) {
    if (offset == 0.f || !(offset == offset)) return pan;
    const float v = pan + offset;
    return v > -1.f ? (v < 1.f ? v : 1.f) : -1.f;
}
// What a setting is given: its centre plus what the MODs aimed at it add (two on one target add,
// then one clamp), held to 0..1. Nothing added (0, or not a number): the centre exactly as it is,
// in the same call as before there were MODs. That is the bit-exact path.
inline float modGiven(float centre, float offset) {
    if (offset == 0.f || !(offset == offset)) return centre;
    const float v = centre + offset;
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
}

} // namespace apotheca
