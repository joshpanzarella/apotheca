#pragma once
// ACID's rules (docs/plans/acid-plan.md): the numbers and the sums, with no state, no sound and no
// SDL. The synth (acid.hpp, engine/src/acid.cpp) reads them; tests/acid_test.cpp holds them.
//
// ACID is a one-voice bass after a classic bass line box: one oscillator (SAW or SQUARE), a
// resonant low-pass, a filter decay and an amp decay, ACCENT from a note's velocity and SLIDE from
// a note's GLIDE. Nothing new is kept on a note.
//
// Every figure says where it is from (the plan's section 1 names each source):
//   FAITHFUL  a published figure, the source named
//   SELF      the plan's own choice: a test of it only says the code does what was chosen
// None of the sources was opened for this batch (2026-11-16: the job was kept to the code), so
// EVERY number here is SELF, including the ones the plan remembers from them (RW's
// "shortest decay on an accent", the sweep that climbs, a slide of constant time). They are kept
// as the plan remembers them and marked so; batch 6 (recordings) is where any becomes FAITHFUL.

#include <cmath>

namespace apotheca {
namespace acid {

// ---- settings ------------------------------------------------------------------------------
// Ids are appended, never reordered: songs and presets store them by number.
enum Setting {
    Wave,      // 0..1: SAW, SQUARE
    Octave,    // -2..1
    Tune,      // -100..100 cents
    Cutoff,    // 0..100: reads HZ (cutoffHz)
    Resonance, // 0..100
    EnvMod,    // 0..100: how far the filter's decay opens the cutoff (envOctaves)
    Decay,     // 0..100: reads MS, 200 MS to 2.0 S (decaySeconds)
    Accent,    // 0..100: how much an accent does (0: an accented note is a plain one, to the bit)
    Sweep,     // 0..100: how far accents close together climb (sweepSeconds)
    Drive,     // 0..100: into the filter's soft clip (0 clean, untouched)
    AmpDecay,  // 0..100: reads S, 0.2 S to 6 S (ampDecaySeconds)
    Slide,     // 0..1: NOTE (the glide's own length), FIXED (kSlideFixedSeconds)
    Velocity,  // 0..1: OFF (every plain note one level), ON (a plain note's level is its velocity)
    Level,     // -24..6 dB
    kSettings
};
constexpr int kSettingRoom = 20; // a patch has room for six more, at 0
static_assert(kSettings == 14, "ids are appended, never reordered");
enum WaveKind { kSaw, kSquare, kWaves };
enum SlideKind { kSlideNote, kSlideFixed, kSlides };

inline int settingMin(int id) { return id == Octave ? -2 : id == Tune ? -100 : id == Level ? -24 : 0; }
inline int settingMax(int id) {
    switch (id) {
    case Wave: return kWaves - 1;
    case Octave: return 1;
    case Slide: return kSlides - 1;
    case Velocity: return 1;
    case Level: return 6;
    default: return 100;
    }
}
// The usual value of each (the plan's section 1). AMP DECAY's is 80, not the plan's 70: on the
// scale below 80 reads 3.0 S, the plan's own guess for the original (70 reads 2.2 S).
inline int settingUsual(int id) {
    static const int usual[kSettings] = {kSaw, 0, 0, 30, 60, 50, 40, 50, 50, 0, 80, kSlideNote, 0, 0};
    return id < 0 || id >= kSettings ? 0 : usual[id];
}
inline int settingHeld(int id, int value) {
    const int lo = settingMin(id), hi = settingMax(id);
    return value < lo ? lo : (value > hi ? hi : value);
}
inline const char* waveName(int w) { return w == kSquare ? "SQUARE" : "SAW"; }
inline const char* slideName(int s) { return s == kSlideFixed ? "FIXED" : "NOTE"; }

// ---- macros --------------------------------------------------------------------------------
// Lane by lane (the plan's section 3). No macro is a choice: each takes a MOD.
enum Macro { MacroCutoff, MacroResonance, MacroEnvMod, MacroDecay, MacroAccent, MacroDrive, MacroSweep, MacroTune, kMacros };
static_assert(kMacros == 8, "every synth has eight macros (kSynthMacros)");
inline int macroSetting(int macro) {
    static const int setting[kMacros] = {Cutoff, Resonance, EnvMod, Decay, Accent, Drive, Sweep, Tune};
    return setting[macro < 0 || macro >= kMacros ? 0 : macro];
}
// The house rule (PHYSIC's and the JX-3P's): a macro below its middle slides its setting from the
// patch's value down to the setting's least, above it up to its most. 0.5 is the patch exactly.
inline float macroBent(float patch, float macro, float least, float most) {
    if (macro == 0.5f) return patch;
    return macro < 0.5f ? least + (patch - least) * (macro * 2.f) : patch + (most - patch) * ((macro - 0.5f) * 2.f);
}

// ---- accent and slide ----------------------------------------------------------------------
// ACCENT is a note at this velocity or over (the plan's call 2). A note placed is 0.9
// (kPlacedLevel), so it is plain; one click of hold A + RIGHT makes it 1.0, an accent.
constexpr float kAccentAt = 0.95f;
// An accent, if ACCENT (with its macro) is over 0 at all. At ACCENT 0 an accented note IS a plain
// one: the same path, to the bit.
inline bool accented(float velocity, float accentDial) { return velocity >= kAccentAt && accentDial > 0.f; }

// THE TIE: a note with GLIDE slides (no envelope starts again) when the gate is open or shut
// under this long ago: notes placed end to end, in either order on one sample (the plan's
// section 2). After a longer rest it slides in pitch but starts the envelopes again.
constexpr double kTieMs = 5.0;
inline bool slides(bool glide, bool gateOpen, double shutSeconds) { return glide && (gateOpen || shutSeconds < kTieMs / 1000.0); }

// SLIDE's time: the note's own glide length (NOTE: the roll's line is the truth) or FIXED.
constexpr double kSlideFixedSeconds = 0.060; // SELF (the folklore's 60 ms)
inline double slideSeconds(int slideSetting, double noteSeconds) { return slideSetting == kSlideFixed ? kSlideFixedSeconds : noteSeconds; }
// THE SLIDE'S SHAPE: an RC on the pitch in octaves, tau = time / kSlideTaus, bent to land exactly
// when the time is up: the share of the way still to go at `t` seconds of `time`.
// (A plain RC would still be e^-3, 5% of the way, short when it is cut off: a jump.)
constexpr double kSlideTaus = 3.0; // SELF: an RC's three time constants in the slide
inline double slideLeft(double t, double time) {
    if (!(time > 0.0) || t >= time) return 0.0;
    if (t <= 0.0) return 1.0;
    const double end = std::exp(-kSlideTaus);
    return (std::exp(-kSlideTaus * t / time) - end) / (1.0 - end);
}
// The same, a sample at a time: the plain RC's factor each sample (the synth multiplies by it)...
inline double slideStep(double time, double rate) { return std::exp(-kSlideTaus / (time * rate)); }
// ...and how many samples until it lands (rounded up: it lands on the first sample at or past the time).
inline long slideSamples(double time, double rate) { return static_cast<long>(std::ceil(time * rate)); }

// ---- the dials -----------------------------------------------------------------------------
// CUTOFF reads the frequency the filter SINGS at (where its resonance stands), at no envelope.
constexpr double kCutoffLowHz = 50.0;   // SELF: CUTOFF 0
constexpr double kCutoffOctaves = 7.0;  // SELF: CUTOFF 100 is 6.4 kHz
inline double cutoffOctaves(double dial) { return dial / 100.0 * kCutoffOctaves; }
inline double cutoffHz(double dial) { return kCutoffLowHz * std::pow(2.0, cutoffOctaves(dial)); }
constexpr double kTopHz = 20000.0;      // SELF: the cutoff is held under this...
constexpr double kTopOfRate = 0.45;     // ...and under this share of the sample rate
constexpr double kEnvOctaves = 5.0;     // SELF: ENV MOD 100 opens the cutoff this far at the envelope's top
inline double envOctaves(double dial) { return dial / 100.0 * kEnvOctaves; }

// The two decays are each "the time to fall to a tenth" (-20 dB): SELF.
constexpr double kDecayFallsTo = 0.1;
inline double decaySeconds(double dial) { return 0.2 * std::pow(10.0, dial / 100.0); }    // 200 MS .. 2.0 S
inline double ampDecaySeconds(double dial) { return 0.2 * std::pow(30.0, dial / 100.0); } // 0.2 S .. 6 S
// On an accent DECAY is passed by and the shortest is used (the plan remembers it from RW: SELF here).
inline double accentDecaySeconds() { return decaySeconds(0.0); }
// A one-pole's factor a sample for a fall to kDecayFallsTo in `seconds`.
inline double fallStep(double seconds, double rate) { return std::exp(std::log(kDecayFallsTo) / (seconds * rate)); }

// ACCENT 100: this much louder, and the cutoff this much further up at the sweep's height.
constexpr double kAccentMostDb = 8.0;      // SELF
constexpr double kAccentOctaves = 2.5;     // SELF
inline double accentDb(double dial) { return dial / 100.0 * kAccentMostDb; }
inline double accentGain(double dial) { return std::pow(10.0, accentDb(dial) / 20.0); }

// THE ACCENT SWEEP: a "capacitor" (a one-pole) fed the accented note's filter envelope; it leaks
// between accents, so accents close together climb (the plan remembers RW's circuit: SELF). Its
// time grows with SWEEP and with RESONANCE (the two pots are ganged in the original, so more
// resonance is a slower, bigger sweep). SWEEP 0: the capacitor is the envelope itself (no climb).
constexpr double kSweepMostSeconds = 0.10; // SELF: SWEEP 100 at RESONANCE 0
inline double sweepSeconds(double sweepDial, double resDial) { return kSweepMostSeconds * (sweepDial / 100.0) * (1.0 + resDial / 100.0); }
// The height the capacitor reaches for one accent alone (an envelope falling with time constant
// `envTau` into a one-pole of `capTau`), so that one accent alone always opens the cutoff
// kAccentOctaves x ACCENT, whatever SWEEP: accents close together climb above it.
inline double sweepPeak(double envTau, double capTau) {
    if (!(capTau > 0.0)) return 1.0;
    if (std::fabs(envTau - capTau) < 1e-9 * envTau) return std::exp(-1.0); // the two the same: t e^-t at t = 1
    const double t = std::log(envTau / capTau) * envTau * capTau / (envTau - capTau);
    return envTau / (envTau - capTau) * (std::exp(-t / envTau) - std::exp(-t / capTau));
}

// RESONANCE: a share of the feedback at which the filter starts to sing (the edge), straight to
// the edge at kResEdgeDial, then on to kResTop of it at 100, so the last few points sing (the
// JX-3P's rule: the singing must be strong enough to hold its own pitch).
constexpr double kResEdgeDial = 95.0; // SELF
constexpr double kResTop = 1.15;      // SELF
inline double resShare(double dial) {
    dial = dial < 0.0 ? 0.0 : (dial > 100.0 ? 100.0 : dial);
    return dial <= kResEdgeDial ? dial / kResEdgeDial : 1.0 + (dial - kResEdgeDial) * ((kResTop - 1.0) / (100.0 - kResEdgeDial));
}

// DRIVE: the gain into the input's soft clip, 1 to 8 (18 dB). 0 is not run.
constexpr double kDriveMostOctaves = 3.0; // SELF
inline double driveGain(double dial) { return std::pow(2.0, dial / 100.0 * kDriveMostOctaves); }

// OCTAVE, TUNE and LEVEL read as themselves (an octave, CENTS, DB).
inline double tuneCents(int value) { return static_cast<double>(value); }
inline double levelDb(int value) { return static_cast<double>(value); }

} // namespace acid
} // namespace apotheca
