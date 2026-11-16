#pragma once
// PHYSIC's rules (docs/plans/physic-plan.md): the numbers and the sums, with no state and no
// SDL. The synth (physic.hpp, engine/src/physic/) reads them; tests/physic_test.cpp holds them.
//
// PHYSIC copies how real things ring. A note is a STRIKE (a mallet or a pluck) on an object
// (the MODEL), which rings and dies away by itself:
//
//   STRING      a Karplus-Strong loop: a delay line one period long holds the string's wave,
//               a filter in the loop sets how it dies (engine/src/physic/string.hpp)
//   the rest    a bank of MODES: each one way the object rings, at its own pitch and its own
//               decay (engine/src/physic/modes.hpp). A marimba bar is about three strong
//               modes; a bell about seven.
//
// Every figure says where it is from (the plan's section 0 names each source):
//   FAITHFUL  a published figure or formula, the source named
//   SELF      the plan's own choice: a test of it only says the code does what was chosen

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "chord_rule.hpp"

namespace apotheca {
namespace physic {

// ---- settings ------------------------------------------------------------------------------
// Ids are appended, never reordered: songs and presets store them by number.
enum Setting {
    Model,        // 0..10: STRING, MARIMBA ... WOOD (kModelNames)
    Strike,       // 0..2: PLUCK, MALLET, NOISE (StrikeKind; NOISE appended in batch 10)
    Hardness,     // 0..100: a mallet's contact 8 ms (yarn) to 0.3 ms (brass); a pluck's brightness
    Position,     // 0..100: struck 1% to 50% along
    Decay,        // 0..100: 50 MS to 30 S, the longest any mode rings
    Material,     // 0..100: WOOD (high modes, and high keys, die fast) to METAL (all ring for DECAY)
    Stretch,      // 0..100: 50 plays the table exactly; STRING: B, how stiff the string is
    Release,      // 0..100: 10 MS to 3 S after let go; 100 RING (let go does nothing)
    Velocity,     // 0..100: how much a note's velocity moves its level and its mallet (0: ignored)
    Octave,       // -2..2
    Fine,         // -50..50 cents
    Width,        // 0..100: low keys left, high keys right
    Level,        // -24..6 dB
    Dice,         // 0..100: scales the four below
    DicePosition, // 0..100: at 100, POSITION +-25 points a note
    DiceHardness, // 0..100: at 100, HARDNESS +-30 points
    DiceTune,     // 0..100: at 100, +-15 cents
    DiceDecay,    // 0..100: at 100, DECAY's time +-30%
    // Batch 10 (the plan's sections 2 and 3), each 0 (off) in every version 1 preset:
    Pair,         // 0..20 cents: each note two of it, kept that far apart (they beat). STRING: a second
                  // string; a modal model: its two lowest modes each split into a pair (a bell's doublets)
    Motor,        // 0..100: OFF, then a tremolo from 0.2 Hz (a slow swell) to 8 Hz (a vibraphone's motor)
    Body,         // 0..3: NONE, GUITAR, BOX, TUBES (BodyKind)
    BodyMix,      // 0..100: how much of the BODY is heard (TUBES: how much louder and longer the note rings)
    Buzz,         // 0..100: OFF, then loose parts rattle while the note is loud (an mbira's caps, a snare's
                  // wires, a balafon's membranes): for the top 6 dB of its ring up to the top 36 dB
    kSettings
};
// A patch has room for three more after BUZZ, at 0.
constexpr int kSettingRoom = 26;
static_assert(kSettings == 23, "ids are appended, never reordered");
enum StrikeKind { kPluck, kMallet, kNoise, kStrikes };
enum BodyKind { kBodyNone, kBodyGuitar, kBodyBox, kBodyTubes, kBodies };
inline const char* bodyName(int b) {
    static const char* const names[kBodies] = {"NONE", "GUITAR", "BOX", "TUBES"};
    return names[b < 0 || b >= kBodies ? 0 : b];
}
inline const char* strikeName(int s) {
    static const char* const names[kStrikes] = {"PLUCK", "MALLET", "NOISE"};
    return names[s < 0 || s >= kStrikes ? 0 : s];
}

inline int settingMin(int id) { return id == Octave ? -2 : id == Fine ? -50 : id == Level ? -24 : 0; }
inline int settingMax(int id) {
    switch (id) {
    case Model: return 10;
    case Strike: return kStrikes - 1;
    case Pair: return 20;
    case Body: return kBodies - 1;
    case Octave: return 2;
    case Fine: return 50;
    case Level: return 6;
    default: return 100;
    }
}

enum Macro { MacroTone, MacroPosition, MacroDecay, MacroMaterial, MacroStretch, MacroRelease, MacroDice, MacroOctave, kMacros };
static_assert(kMacros == 8, "every synth has eight macros (kSynthMacros)");

// A macro below its middle slides its setting from the patch's value down to the setting's
// least, above it up to its most (as the JX-3P plan's rule). 0.5 is the patch exactly.
inline float macroBent(float patch, float macro, float least, float most) {
    if (macro == 0.5f) return patch;
    return macro < 0.5f ? least + (patch - least) * (macro * 2.f) : patch + (most - patch) * ((macro - 0.5f) * 2.f);
}
// OCTAVE's macro is a threshold, as Phazed's and QMOD's: down an octave under a quarter, up over three.
inline int macroOctave(float macro) { return macro < 0.25f ? -1 : (macro > 0.75f ? 1 : 0); }

// ---- the models ----------------------------------------------------------------------------

enum ModelKind { kString, kMarimba, kXylo, kVibes, kGlock, kKalimba, kBell, kChime, kGlass, kDrum, kWood, kModels };
inline const char* modelName(int m) {
    static const char* const names[kModels] = {"STRING", "MARIMBA", "XYLO", "VIBES", "GLOCK", "KALIMBA", "BELL", "CHIME", "GLASS", "DRUM", "WOOD"};
    return names[m < 0 || m >= kModels ? 0 : m];
}

// Where a thing is struck decides which modes wake: a mode's gain is its shape's height there.
enum Shape {
    kShapeString,  // sin(n pi x), by a comb on the strike (JS)
    kShapeFreeBar, // a bar free at both ends (FR 2.15): MARIMBA, XYLO, VIBES, GLOCK, CHIME
    kShapeTine,    // a bar held at one end (FR 2.15): KALIMBA, measured from the free tip
    kShapeSkin,    // a round membrane: J_n(j_nm r) along the radius (AS; FR ch. 3)
    kShapeTilt     // no published shape (BELL, GLASS, WOOD): a smooth tilt from the low modes to the high. SELF
};

constexpr int kModesMost = 12; // the cap of modes a voice (one constant: the cost is mostly here)

struct ModelRow {
    int modes = 0;                     // how many (at most kModesMost)
    double ratio[kModesMost] = {};     // frequency over the played note's, at STRETCH 50
    double weight[kModesMost] = {};    // each mode's own level before the strike's shape (1: none. SELF where not 1)
    int beam[kModesMost] = {};         // FREE BAR / TINE: which of the bar's modes it is (0 the first); SKIN: the Bessel order n
    double zero[kModesMost] = {};      // SKIN: j_nm, the zero of J_n the mode is
    Shape shape = kShapeTilt;
    int played = 0;                    // which mode is the played note (ratio 1); -1: none (CHIME)
};

// The free-free bar's beta_n L, roots of cos k cosh k = 1 (FR 2.15; recomputed in the test).
constexpr double kFreeBar[6] = {4.730040744862704, 7.853204624095838, 10.995607838001671, 14.137165491257464, 17.278759657399480, 20.420352245626059};
// The clamped-free bar's, roots of cos k cosh k = -1.
constexpr double kTineBar[4] = {1.875104068711961, 4.694091132974175, 7.854757438237613, 10.995540734875467};

// A church bell's partials, in cents from the NOMINAL (Hib: hum -2400, prime -1200, tierce -900,
// quint -500, nominal 0; the superquint (+702) and the octave nominal (+1200) are SELF until a
// page of Hibbert's table is read). The played note is the strike note, an octave under the
// nominal, where the prime sits (Hib, "The strike note of bells"), so each ratio is
// 2 * 2^(cents / 1200), worked out in double and never rounded by hand.
constexpr double kBellCents[7] = {-2400.0, -1200.0, -900.0, -500.0, 0.0, 701.955, 1200.0};
inline double bellRatio(double centsFromNominal) { return 2.0 * std::pow(2.0, centsFromNominal / 1200.0); }

// Each MODEL's row. Built in double each time it is asked (the synth keeps one shared copy).
inline ModelRow modelRow(int model) {
    ModelRow r;
    const auto put = [&r](std::initializer_list<double> ratios) {
        r.modes = 0;
        for (double v : ratios) {
            r.ratio[r.modes] = v;
            r.weight[r.modes] = 1.0;
            r.beam[r.modes] = r.modes;
            ++r.modes;
        }
    };
    switch (model) {
    case kString: // its harmonics are the loop's own: no table (F64 stretches them)
        r.modes = 0;
        r.shape = kShapeString;
        break;
    case kMarimba: // YR (mode 2 at 4.0, an undercut bar); FR ch. 19 (10.0, three octaves and a major third). Some sources say 9.2. The 4th: SELF
        put({1.0, 4.0, 10.0, 17.0});
        r.weight[3] = 0.5;
        r.shape = kShapeFreeBar;
        break;
    case kXylo: // R00 ch. 3 (a twelfth, 3.0); 6.0 SELF
        put({1.0, 3.0, 6.0});
        r.shape = kShapeFreeBar;
        break;
    case kVibes: // R00 ch. 3: 1, 4, 10
        put({1.0, 4.0, 10.0});
        r.shape = kShapeFreeBar;
        break;
    case kGlock: // the uniform free-free bar, (beta_n / beta_1)^2 (FR 2.15); R00's measured bar is 1 : 2.71 : 5.15
        put({1.0, 2.7565, 5.4039, 8.9330, 13.3443, 18.6379}); // (the plan wrote 18.6372: the root gives 18.63789)
        for (int k = 0; k < 6; ++k) r.ratio[k] = (kFreeBar[k] / kFreeBar[0]) * (kFreeBar[k] / kFreeBar[0]);
        r.shape = kShapeFreeBar;
        break;
    case kKalimba: // mode 2 as real tines measure (Ch: 5.3 to 5.9; the ideal bar says 6.267); 3 and 4 the clamped-free bar
        put({1.0, 5.6, 17.55, 34.39});
        for (int k = 2; k < 4; ++k) r.ratio[k] = (kTineBar[k] / kTineBar[0]) * (kTineBar[k] / kTineBar[0]);
        r.shape = kShapeTine;
        break;
    case kBell: // Hib, from the cents above
        r.modes = 7;
        for (int k = 0; k < 7; ++k) {
            r.ratio[k] = bellRatio(kBellCents[k]);
            r.beam[k] = k;
        }
        { // each partial's own level: SELF (a bell's hum and nominal are its strongest)
            const double w[7] = {0.7, 1.0, 0.8, 0.4, 1.0, 0.5, 0.3};
            for (int k = 0; k < 7; ++k) r.weight[k] = w[k];
        }
        r.played = 1;
        r.shape = kShapeTilt;
        break;
    case kChime: // Hib: partials 2 to 7 of 217 tubular bells, averaged, as a multiple of the strike pitch. No partial at the played pitch, on purpose
        put({0.65, 1.25, 1.99, 2.86, 3.81, 4.86});
        r.played = -1;
        r.shape = kShapeFreeBar;
        break;
    case kGlass: // Cs after Rossing (measured). The thin ring's formula says 2.83, 5.42 (the ideal)
        put({1.0, 2.32, 4.25, 6.63, 9.38});
        { const double w[5] = {1.0, 0.5, 0.3, 0.2, 0.15}; for (int k = 0; k < 5; ++k) r.weight[k] = w[k]; } // SELF
        r.shape = kShapeTilt;
        break;
    case kDrum: { // the ideal round membrane: j_nm / j_01 (AS table 9.5; FR ch. 3), recomputed in the test
        const double zero[8] = {2.404825557695773, 3.831705970207512, 5.135622301840683, 5.520078110286311,
                                6.380161895923984, 7.015586669815619, 7.588342434503805, 8.417244140399865};
        const int order[8] = {0, 1, 2, 0, 3, 1, 4, 2};
        r.modes = 8;
        for (int k = 0; k < 8; ++k) {
            r.ratio[k] = zero[k] / zero[0];
            r.weight[k] = 1.0;
            r.beam[k] = order[k];
            r.zero[k] = zero[k];
        }
        r.shape = kShapeSkin;
        break;
    }
    case kWood: // no published set found: SELF. The device does not call it measured
        put({1.0, 1.5, 2.4, 3.6});
        { const double w[4] = {1.0, 0.6, 0.4, 0.25}; for (int k = 0; k < 4; ++k) r.weight[k] = w[k]; }
        r.shape = kShapeTilt;
        break;
    default:
        return modelRow(kMarimba);
    }
    return r;
}

// STRETCH on a modal model (Rings' STRUCTURE, MI): 50 plays the table exactly (FAITHFUL: the
// test holds it to the bit). Under 50 each ratio is pulled towards a whole number (at 0 a plain
// harmonic series, a ratio under 3/4 to a half); over 50 stretched, ratio^(1 + 0.3 s). SELF.
// The played mode (ratio 1) never moves.
constexpr double kStretchMost = 0.3;
inline double stretched(double ratio, double stretch) {
    if (stretch == 50) return ratio;
    const double s = (stretch - 50) / 50.0;
    if (s > 0.0) return std::pow(ratio, 1.0 + kStretchMost * s);
    const double whole = ratio < 0.75 ? 0.5 : std::floor(ratio + 0.5);
    return ratio + (whole - ratio) * -s;
}
// STRETCH on STRING: the string's stiffness B, 0 at 0 to 2e-3 at 100 (piano strings 1e-4 to 1e-3,
// F64), on a square taper so the low end has room (STRETCH 20: B 8e-5, a guitar's). The taper SELF.
constexpr double kStiffnessMost = 2e-3;
inline double stiffnessOf(double stretch) {
    const double s = stretch / 100.0;
    return kStiffnessMost * s * s;
}
// A stiff string's partial n, over its fundamental times n (F64): sqrt(1 + B n^2) / sqrt(1 + B).
inline double stiffPartial(double b, int n) { return n * std::sqrt((1.0 + b * n * n) / (1.0 + b)); }

// ---- where it is struck --------------------------------------------------------------------

// POSITION 0..100 is 1% to 50% of the way along.
inline double positionAlong(double position) {
    const double p = position < 0.0 ? 0.0 : (position > 100.0 ? 100.0 : position);
    return 0.01 + 0.49 * p / 100.0;
}

// A free-free bar's mode shape (beta = its beta_n L), at x (0..1 along), over its height at an
// end (2): so 1 at an end, 0 at a node. Centre: the odd modes (n = 2, 4..) are exactly 0 there.
inline double freeBarShape(double beta, double x) {
    const double sigma = (std::cosh(beta) - std::cos(beta)) / (std::sinh(beta) - std::sin(beta));
    const double bx = beta * x;
    return 0.5 * (std::cosh(bx) + std::cos(bx) - sigma * (std::sinh(bx) + std::sin(bx)));
}
// A clamped-free bar (a tine) at `fromTip` (0 at the free tip), over its height at the tip.
inline double tineShape(double beta, double fromTip) {
    const auto at = [beta](double x) {
        const double sigma = (std::cosh(beta) + std::cos(beta)) / (std::sinh(beta) + std::sin(beta));
        const double bx = beta * x;
        return std::cosh(bx) - std::cos(bx) - sigma * (std::sinh(bx) - std::sin(bx));
    };
    return at(1.0 - fromTip) / at(1.0);
}
// J_n(z), Bessel's function of the first kind, by its series (enough terms for z up to 12).
inline double besselJ(int n, double z) {
    double term = 1.0;
    for (int k = 1; k <= n; ++k) term *= (z * 0.5) / k; // (z/2)^n / n!
    double sum = term;
    const double q = -(z * 0.5) * (z * 0.5);
    for (int k = 1; k < 60; ++k) {
        term *= q / (static_cast<double>(k) * (k + n));
        sum += term;
        if (std::fabs(term) < 1e-17 * (std::fabs(sum) + 1e-300)) break;
    }
    return sum;
}
// A round skin struck at `along` (POSITION's 1% at the rim to 50% at the centre): r = 1 - 2 along.
// Mode (n, m): J_n(j_nm r). The angle is fixed (cos n theta = 1). At the centre every n >= 1 is 0.
inline double skinShape(int n, double zero, double along) {
    const double r = 1.0 - 2.0 * along;
    return besselJ(n, zero * (r < 0.0 ? 0.0 : r));
}
// BELL, GLASS, WOOD: no published shape. Struck near the edge (small POSITION) the high modes are
// up, near the middle down: gain ratio^(-0.7 t), t from -1 (1%) to 1 (50%). SELF.
inline double tiltShape(double ratio, double along) {
    const double t = (along - 0.255) / 0.245;
    return std::pow(ratio, -0.7 * t);
}

// ---- the damping law -----------------------------------------------------------------------
// Each mode dies at 1/tau = a + b f^2, f in Hz (CD: FAITHFUL as a law; the values SELF).
// T60 (60 dB down) is ln(1000) tau. DECAY sets a alone, the longest any mode rings; MATERIAL
// sets b, which only ever shortens: on METAL (b = 0) DECAY is every mode's T60, toward WOOD a
// high mode, and a high key, rings shorter.
constexpr double kLn1000 = 6.907755278982137;
inline double decaySeconds(double decay) { return 0.05 * std::pow(600.0, decay / 100.0); } // 50 ms to 30 s
inline double rateOfT60(double seconds) { return kLn1000 / seconds; }                     // a, from a T60
constexpr double kWoodB = 3e-5; // b at MATERIAL 0 (s^-1 Hz^-2): a 2 kHz mode rings under 0.06 s. SELF
inline double materialB(double material) {
    const double m = 1.0 - (material < 0.0 ? 0.0 : (material > 100.0 ? 100.0 : material)) / 100.0;
    return kWoodB * m * m;
}
// RELEASE: 10 ms to 3 s after let go; 100 is RING, which let go does not touch.
inline bool releaseRings(double release) { return release >= 99.5; }
inline double releaseSeconds(double release) { return 0.01 * std::pow(300.0, release / 100.0); }
// A mode's radius a sample: how much of it is left after one sample at 1/tau = rate.
inline double radiusOf(double rate, double sampleRate) { return std::exp(-rate / sampleRate); }

// ---- the strike ----------------------------------------------------------------------------
// A MALLET is a half-sine push of contact time T: 8 ms (yarn) at HARDNESS 0 to 0.3 ms (brass) at
// 100, on a log scale (the range SELF). A harder hit is shorter (Hertz contact, Hz: T goes as
// v^(-1/5)), so brighter as well as louder: T times v^(-V/5), V = VELOCITY / 100.
constexpr double kSoftestContact = 0.008, kHardestContact = 0.0003;
constexpr double kPluckContact = 0.0001; // a tine plucked: a 0.1 ms click (the plan; SELF)
// Into the modes the push is a smooth bump, t e^(-t / tau) (no zero in its spectrum, which falls
// 12 dB an octave as a half-sine's does): a half-sine's spectrum has zeros, and a key whose
// fundamental fell on one sounded as nothing. tau = T / kBumpOfContact puts the bump's -6 dB where
// the half-sine of contact T has its own. SELF.
constexpr double kBumpOfContact = 3.77;
constexpr double kBumpLong = 12.0; // the push ends 12 tau after it began (what is left: under 1e-4 of its height)
// What the bump (discrete: n q^n, q = e^(-1/tau samples)) leaves a mode at w with, against a click.
inline double bumpSpectrum(double q, double w) { return (1.0 - q) * (1.0 - q) / (1.0 - 2.0 * q * std::cos(w) + q * q); }
inline double contactSeconds(double hardness, double velocity, double velocityAmount) {
    const double h = (hardness < 0.0 ? 0.0 : (hardness > 100.0 ? 100.0 : hardness)) / 100.0;
    double t = kSoftestContact * std::pow(kHardestContact / kSoftestContact, h);
    const double v = velocity < 0.05 ? 0.05 : (velocity > 1.0 ? 1.0 : velocity);
    const double amount = velocityAmount < 0.0 ? 0.0 : (velocityAmount > 1.0 ? 1.0 : velocityAmount);
    t *= std::pow(v, -0.2 * amount);
    return t;
}
// A note's level by its velocity: (1 - V) + V v. At V = 0 every note is full.
inline double velocityLevel(double velocity, double velocityAmount) {
    const double v = velocity < 0.0 ? 0.0 : (velocity > 1.0 ? 1.0 : velocity);
    return (1.0 - velocityAmount) + velocityAmount * v;
}

// ---- pitch ---------------------------------------------------------------------------------
// The sounding pitch (key + 12 OCTAVE + FINE + the dice's tune) is held inside what the core can
// play: STRING 24 to 112 (above 112 the loop is under 9 samples; under 24 its line is too short),
// a modal model at most 127 (above, the played mode passes 0.45 of the sample rate and is faded).
constexpr double kStringLowest = 24.0, kStringHighest = 112.0, kModalHighest = 127.0;
inline double heldPitch(double pitch, bool string) {
    if (string) return pitch < kStringLowest ? kStringLowest : (pitch > kStringHighest ? kStringHighest : pitch);
    return pitch > kModalHighest ? kModalHighest : (pitch < 0.0 ? 0.0 : pitch);
}
inline double hzOf(double pitch) { return 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0); }

// A mode's gain fades out between 0.40 and 0.45 of the sample rate (QMOD, gone since, cut at 0.45), so a
// glide across the limit does not click; above, the mode is silent.
constexpr double kFadeFrom = 0.40, kFadeTo = 0.45;
inline double nyquistFade(double hz, double sampleRate) {
    const double f = hz / sampleRate;
    return f <= kFadeFrom ? 1.0 : (f >= kFadeTo ? 0.0 : (kFadeTo - f) / (kFadeTo - kFadeFrom));
}

// ---- the string's loop ---------------------------------------------------------------------
// A first-order allpass (c + z^-1) / (1 + c z^-1): its phase delay at w (radians a sample).
inline double allpassDelay(double c, double w) {
    // arg((c + e^-jw) / (1 + c e^-jw))
    const double re1 = c + std::cos(w), im1 = -std::sin(w), re2 = 1.0 + c * std::cos(w), im2 = -c * std::sin(w);
    double phase = std::atan2(im1, re1) - std::atan2(im2, re2);
    while (phase > 0.0) phase -= 2.0 * 3.141592653589793; // the delay is positive: the phase lags
    return -phase / w;
}
// The loop's low-pass, k (1 - p) / (1 - p z^-1): its phase delay at w, and its size at w (for k = 1).
inline double lowpassDelay(double p, double w) { return std::atan2(p * std::sin(w), 1.0 - p * std::cos(w)) / w; }
inline double lowpassSize(double p, double w) { return (1.0 - p) / std::sqrt(1.0 - 2.0 * p * std::cos(w) + p * p); }
// THE LOOP'S DC BLOCKER: ((1 + R) / 2) (1 - z^-1) / (1 - R z^-1), its corner kDcCorner of the way to
// f0. Without it the loop's own DC (nothing in a pluck is quite zero on average, and a float's
// rounding adds some) rang on at the loop's DC gain, near 1: a WOOD string on a high key kept a
// voice awake for over a minute. Its phase (a lead: a delay under zero) and its size at f0 go into
// the tuning and the loop's gain, so the fundamental is as exact as without it.
constexpr double kDcCorner = 1.0 / 200.0;
inline double dcPole(double f0, double sampleRate) { return std::exp(-2.0 * 3.141592653589793 * kDcCorner * f0 / sampleRate); }
inline double dcDelay(double r, double w) {
    const double lead = (3.141592653589793 - w) * 0.5 - std::atan2(r * std::sin(w), 1.0 - r * std::cos(w)); // arg H
    return -lead / w;
}
inline double dcSize(double r, double w) { return 0.5 * (1.0 + r) * 2.0 * std::sin(w * 0.5) / std::sqrt(1.0 - 2.0 * r * std::cos(w) + r * r); }

// THE LOOP FILTER FROM THE LAW (VHKJ's idea: a one-pole fitted to the loss). On the string DECAY
// is the fundamental's T60 on every key and at every MATERIAL (JS's decay stretching: each pass
// round the loop, one period 1/f0, it keeps exactly exp(-a / f0)); MATERIAL's b only makes the
// partials above it die faster, by the law: the part at kLoopFitHarmonic times f0 keeps
// exp(-b (f1^2 - f0^2) / f0) of what the fundamental keeps.
constexpr double kLoopFitHarmonic = 5.0, kLoopPoleMost = 0.97, kLoopGainMost = 0.99999;
struct LoopFilter {
    double pole = 0.0; // p
    double gain = 1.0; // k: the size at DC
};
inline LoopFilter loopFilterOf(double a, double b, double f0, double sampleRate) {
    LoopFilter lf;
    const double w0 = 2.0 * 3.141592653589793 * f0 / sampleRate;
    double f1 = kLoopFitHarmonic * f0;
    if (f1 > 0.4 * sampleRate) f1 = 0.4 * sampleRate;
    const double w1 = 2.0 * 3.141592653589793 * f1 / sampleRate;
    const double keep0 = std::exp(-a / f0);
    if (b > 0.0 && f1 > f0 * 1.01) {
        const double r = std::exp(-b * (f1 * f1 - f0 * f0) / f0); // what f1 keeps over what f0 keeps
        const double r2 = r * r, c0 = std::cos(w0), c1 = std::cos(w1);
        if (r2 < 1.0) {
            const double bb = (c0 - r2 * c1) / (r2 - 1.0);
            double p = bb * bb > 1.0 && bb < 0.0 ? -bb - std::sqrt(bb * bb - 1.0) : kLoopPoleMost;
            if (p > kLoopPoleMost || !(p >= 0.0)) p = kLoopPoleMost;
            lf.pole = p;
        }
    }
    // Never more than kLoopGainMost round the loop at DC: where the fit would need more (the law's
    // bend between DC and f0 is steeper than a one-pole's, on a high key), the pole is eased until
    // the fundamental is reached at that gain. The fundamental is exact either way.
    const double dc = dcSize(dcPole(f0, sampleRate), w0);
    const double target = keep0 / dc / kLoopGainMost;
    if (lowpassSize(lf.pole, w0) < target) {
        const double t2 = target * target, c0 = std::cos(w0), q = 1.0 - t2 * c0;
        lf.pole = t2 < 1.0 ? (q - std::sqrt(q * q - (1.0 - t2) * (1.0 - t2))) / (1.0 - t2) : 0.0;
        if (!(lf.pole >= 0.0)) lf.pole = 0.0;
    }
    lf.gain = keep0 / (lowpassSize(lf.pole, w0) * dc);
    return lf;
}

// STIFFNESS: kDispersion first-order allpasses of one coefficient c (VDS; Rauhala & Valimaki
// design with several alike). c is fitted so that partial kDispersionPartial of the loop lands
// where F64 puts it; the others follow as the allpasses allow (the test says how near). Under 0:
// the low parts are later than the high, so a high partial comes round sooner, sharp.
constexpr int kDispersion = 4;
constexpr int kDispersionPartial = 6;
constexpr double kDispersionMost = -0.9;
// The phase delay all of them add at w.
inline double dispersionDelay(double c, double w) { return c == 0.0 ? 0.0 : kDispersion * allpassDelay(c, w); }
inline double dispersionOf(double b, double f0, double sampleRate) {
    if (!(b > 0.0)) return 0.0;
    int n = kDispersionPartial;
    while (n > 1 && n * f0 > 0.25 * sampleRate) --n;
    if (n < 2) return 0.0;
    const double w0 = 2.0 * 3.141592653589793 * f0 / sampleRate;
    const double wn = w0 * stiffPartial(b, n);
    const double total = sampleRate / f0;                 // the loop's delay at f0, whole
    const double want = total - 2.0 * 3.141592653589793 * n / wn; // how much less partial n must wait
    double lo = kDispersionMost, hi = -1e-6;                     // c: more dispersion towards lo
    const auto lessAtN = [&](double c) { return dispersionDelay(c, w0) - dispersionDelay(c, wn); };
    if (lessAtN(lo) <= want) return lo; // as stiff as it can be: short of F64 on a low key
    for (int i = 0; i < 48; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (lessAtN(mid) > want) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}
// THE TUNING (JS; PASP): the whole loop's delay at f0 is SR / f0. What the loop filter and the
// stiffness allpasses take at f0 is taken off; what is left is N whole samples and a part d in
// [0.5, 1.5), which the tuning allpass makes: eta = sin(w (1 - d) / 2) / sin(w (1 + d) / 2).
struct Tuning {
    int whole = 0;     // N
    double part = 1.0; // d
    double eta = 0.0;  // the tuning allpass's coefficient
};
inline Tuning tuningOf(double f0, double sampleRate, double pole, double dispersion) {
    Tuning t;
    const double w = 2.0 * 3.141592653589793 * f0 / sampleRate;
    const double rest = sampleRate / f0 - lowpassDelay(pole, w) - dispersionDelay(dispersion, w) - dcDelay(dcPole(f0, sampleRate), w);
    t.whole = static_cast<int>(std::floor(rest - 0.5));
    if (t.whole < 1) t.whole = 1;
    t.part = rest - t.whole;
    t.eta = std::sin(w * (1.0 - t.part) * 0.5) / std::sin(w * (1.0 + t.part) * 0.5);
    return t;
}

// ---- batch 10: NOISE, PAIR, MOTOR, BODY, BUZZ (the plan's sections 2 and 3) ------------------

// NOISE: a burst of noise, its loudness a half-sine over its length, HARDNESS setting the length
// from 2 s (0: a breath, a bow on glass) to 2 ms (100: a brush, a slap), on a log scale. Into the
// modes or the string as a mallet's push is (AC's noise exciter is the model). SELF.
constexpr double kNoiseLongest = 2.0, kNoiseShortest = 0.002;
inline double noiseSeconds(double hardness) {
    const double h = (hardness < 0.0 ? 0.0 : (hardness > 100.0 ? 100.0 : hardness)) / 100.0;
    return kNoiseLongest * std::pow(kNoiseShortest / kNoiseLongest, h);
}

// PAIR: the two of a pair sit half its cents either side of the note, so the pitch heard stays the
// note's, and they beat at the difference of their frequencies (FAITHFUL: two sines that far apart).
inline double pairRatio(double cents, int side) { return std::pow(2.0, (side ? cents : -cents) / 2400.0); }
inline double pairBeatHz(double hz, double cents) { return hz * (pairRatio(cents, 1) - pairRatio(cents, 0)); }
constexpr int kPairedModes = 2; // a modal model's lowest two modes are paired (the plan: BELL 7 modes, 9 with PAIR)

// MOTOR: a tremolo whose turn starts at each strike (the plan's call 17), so a note is the same each
// time it is struck. 1 reads 0.2 Hz, 100 reads 8 Hz, on a log scale. The level falls from full at
// the turn's start to 1 - kMotorDepth half a turn on (a vibraphone's discs shut the tubes). SELF.
constexpr double kMotorSlowest = 0.2, kMotorFastest = 8.0, kMotorDepth = 0.6;
inline double motorHz(double motor) {
    if (!(motor >= 0.5)) return 0.0;
    const double m = motor > 100.0 ? 100.0 : motor;
    return kMotorSlowest * std::pow(kMotorFastest / kMotorSlowest, (m - 1.0) / 99.0);
}

// BODY: GUITAR and BOX are a few resonances of a box the strings or tines sit on, one for the track,
// run on the voices' sum (its middle, L + R) and added back to both sides. At BODY MIX 100 each
// resonance adds as much again as the sound had at its frequency (+6 dB there). Their frequencies
// and rings: a guitar's air mode near 100 Hz and top plate near 200 and 400 Hz (FR ch. 9: the
// figures SELF until a page is read); a small box's two near 180 and 420 Hz (SELF).
struct BodyResonance { double hz, t60; };
constexpr int kBodyModesMost = 3;
inline int bodyResonances(int body, BodyResonance* out) {
    switch (body) {
    case kBodyGuitar: out[0] = {98.0, 0.14}, out[1] = {204.0, 0.10}, out[2] = {392.0, 0.07}; return 3;
    case kBodyBox: out[0] = {180.0, 0.10}, out[1] = {420.0, 0.06}; return 2;
    default: return 0;
    }
}
constexpr double kBodyPeak = 1.0; // a resonance's gain at its frequency, at BODY MIX 100
// TUBES is done in each voice (the plan): the played mode (a marimba's tube is tuned to it; CHIME's
// lowest, which has none) rings louder and longer, a string's fundamental longer. At BODY MIX 100:
// struck kTubeLouder + 1 times as hard against the rest, ringing kTubeLonger + 1 times as long. SELF.
constexpr double kTubeLouder = 1.0, kTubeLonger = 2.0;

// BUZZ: loose parts on the object rattle while it is loud. A rattle touches only when the sound is
// over its gap, which sits BUZZ's reading in dB under the loudest the note has been: 6 dB (BUZZ 1:
// only the top of each strike) to 36 dB (100: most of the ring). What is over the gap comes out as
// a sizzle (hashed noise: never a generator walked), so it buzzes a few times a wave and stops by
// itself as the note dies away. SELF.
constexpr double kBuzzNearestDb = 6.0, kBuzzFurthestDb = 36.0;
inline double buzzDb(double buzz) {
    if (!(buzz >= 0.5)) return 0.0;
    const double b = buzz > 100.0 ? 100.0 : buzz;
    return kBuzzNearestDb + (kBuzzFurthestDb - kBuzzNearestDb) * (b - 1.0) / 99.0;
}
inline double buzzGap(double buzz) { return buzz >= 0.5 ? std::pow(10.0, -buzzDb(buzz) / 20.0) : 0.0; } // over the loudest
constexpr float kBuzzSize = 1.f; // the sizzle: what is over the gap, times noise of this size

// ---- the dice ------------------------------------------------------------------------------
// A note's roll, from where and which time it is struck (the engine's noteSeed: the track, the
// key and the strike's own moment since Play, through chordSeed), never a generator walked.
enum DiceRoll { kRollsPosition, kRollsHardness, kRollsTune, kRollsDecay };
inline double diceRoll(std::uint32_t seed, DiceRoll which) { return chordSigned(seed, kRollStrike, static_cast<int>(which)); } // -1 up to 1
// How far each goes at its row and DICE both at 100.
constexpr double kDicePositionMost = 25.0, kDiceHardnessMost = 30.0, kDiceTuneCents = 15.0, kDiceDecayMost = 0.3;
// The seed the engine hands a strike: the track, the key, and the strike's moment in 1/256 steps.
inline std::uint32_t strikeSeed(int track, int pitch, double strikeAt) {
    return chordSeed(track, 0, pitch, static_cast<long>(std::llround(strikeAt * 256.0)));
}

// The noise a pluck is made of: a hash of the note's seed and the sample since the strike, never a
// generator stepped along (so it does not hang on how the sound is cut). -1 up to 1.
inline float pluckNoise(std::uint32_t seed, std::uint32_t at) {
    const std::uint32_t h = chordHash(seed ^ chordHash(at * 0x9E3779B1u + 0x632BE5ABu));
    return static_cast<float>(static_cast<std::int32_t>(h >> 8) - 8388608) * (1.f / 8388608.f);
}

} // namespace physic
} // namespace apotheca
