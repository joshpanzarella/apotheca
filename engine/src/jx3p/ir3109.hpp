#pragma once
// JX-3P: the VCF (an IR3109) and the HPF (the plan's batch 3).
//
// The real VCF (Service Notes p.6): "a 4-pole LPF composed of mIC102 and
// associated components"; "Feedback amount and the amplitude of resonance is
// controlled by mTR103, 104". The chip is an IR3109 (parts list, p.3): four
// transconductance stages (OTAs) in a row, each a one-pole low-pass, the last
// fed back to the first. Its CV is "a combination of KCV, CUTOFF, LFO and
// ADSR" (p.6), written once a loop through a sample-and-hold. The HPF is one
// filter after the six voices are summed, before the chorus (the block
// diagram, p.4); nothing says how many poles.
//
// HOW IT IS SOLVED: four one-pole stages, each the "zero-delay feedback"
// (trapezoidal) kind, so the cutoff and the edge of oscillation hold at 48 kHz
// with no oversampling, and the loop through all four is solved as a linear
// sum each sample. The feedback is bent where it meets the input (a soft clip,
// a ratio of polynomials, not tanh); the bend is taken from the sample before
// as a gain (how much the clip took off the feedback then), so the solve stays
// linear and needs no iterations: what the plan's 0.68% prototype timed.
// That bend is what holds the singing to one level: it takes the loop's gain
// down to the edge and no further, and it moves no pitch (it scales the
// feedback, not a stage's cutoff).
//
// NOT BUILT: a bend in each stage (the plan's words). Tried two ways
// (2026-11-14): bending what goes into each OTA made a full saw at RESONANCE 0
// 2 dB quieter at its peaks and moved SOURCE MIX's 1:2 by 0.16 dB; bending
// what is across each OTA's inputs (the stage before less its own output)
// left a slow sound alone but, where the filter sings, lowered every stage's
// cutoff with it: 4 semitones flat at 79 Hz with the knee set for the level,
// and still 16 cents off a semitone a semitone over Organ I's three octaves
// with the knee at 4 (the plan holds it to 5). The feedback's bend alone keeps
// every sound bounded (testStaysBounded).
//
// The coefficient (a tan of the cutoff) is worked out only on a sample where
// the cutoff's CV moved (the S/H's smoothing, a GLIDE, the tick), from short
// polynomials (`exp2Poly`, `sinPoly`, `cosPoly`), not the maths library: the
// same bits on every machine, and quick on the handheld.
//
// Rack-free and SDL-free; no allocation; every state snapped to 0 under 1e-20.
//
// THE GUESSES AND THE READINGS (one table; the plan's section 2):
//
//   | What                         | Here                                          | Source |
//   | ---------------------------- | --------------------------------------------- | ------ |
//   | 4 poles, feedback            | four one-poles, the 4th fed back to the 1st   | SN p.6 |
//   | CUTOFF 0 to 10               | 15 Hz to 15 kHz, even in octaves (kVcfLowHz,  | SELF   |
//   |                              | kVcfHighHz), at KEY FOLLOW's reference note   |        |
//   | The highest cutoff           | 20 kHz, and never past 0.45 of the rate       | SELF   |
//   | KEY FOLLOW's reference       | note 60 (kKeyFollowNote): KEY adds nothing    | SELF   |
//   |                              | there                                         |        |
//   | KEY FOLLOW's 100%            | 9.8 (jx3p.hpp: kKeyFollowFull)                | NAM (Organ I); the owner's call |
//   | ENV MOD 10                   | 10 octaves (kVcfEnvOctaves), straight in the  | SELF   |
//   |                              | dial; polarity turns it over                  | OM p.25 (polarity) |
//   | BRILLIANCE 0 / 5 / 10        | -2 / 0 / +2 octaves (kBrillianceOctaves)      | SELF (the knob is on the panel) |
//   | RESONANCE 0 to 10            | feedback 0 to 4.6 (kResTop), straight: 4 is   | SELF; that 10 sings: NAM Organ I |
//   |                              | the edge, so it sings from 8.7                |        |
//   | The feedback's bend          | x (27 + x^2) / (27 + 9 x^2) of x /            | SELF   |
//   |                              | kFeedbackKnee, flat past 3 (a ratio of        |        |
//   |                              | polynomials), from the sample before          |        |
//   | kFeedbackKnee                | set so the sine it sings at RESONANCE 10 is   | SELF: SN p.7 trims it to 600 mV, |
//   |                              | about 6 dB under the saw's peak               | but prints no saw figure |
//   | Starting to sing             | 1e-4 put into stage 1 at each key (kVcfKick)  | SELF   |
//   | Bass loss                    | not made up: the low end falls by 1 / (1 + k) | SELF (the Juno-60's IR3109 has it); |
//   |                              | (jx3p.hpp: kBassLoss)                         | the owner's call |
//   | The S/H's smoothing          | env.hpp's Hold, 1 ms, on the tick's part of   | SELF   |
//   |                              | the CV (in octaves), landing 1e-3 of an octave|        |
//   |                              | (kVcfCvLands) from its target; the key moves  |        |
//   |                              | per sample                                    |        |
//   | HPF                          | one pole (6 dB an octave); 0 is off, untouched| SN p.4 (where); the pole is SELF |
//   | HPF 0.1 to 10                | 10 Hz to 1 kHz, even in octaves (kHpfLowHz,   | SELF   |
//   |                              | kHpfHighHz)                                   |        |

#include <cmath>
#include <cstdint>
#include <cstring>

namespace apotheca {
namespace jx3p {

constexpr double kVcfLowHz = 15.0;        // SELF: CUTOFF 0
constexpr double kVcfHighHz = 15000.0;    // SELF: CUTOFF 10
constexpr double kVcfTopHz = 20000.0;     // SELF: the cutoff is held under this...
constexpr double kVcfTopOfRate = 0.45;    // ...and under this share of the sample rate
constexpr int kKeyFollowNote = 60;        // SELF: KEY FOLLOW turns about this key
constexpr double kVcfEnvOctaves = 10.0;   // SELF: ENV MOD 10
constexpr double kBrillianceOctaves = 2.0;// SELF: BRILLIANCE 0 and 10, from 5
constexpr float kResTop = 4.6f;           // SELF: the feedback at RESONANCE 10 (4 is the edge)
constexpr float kFeedbackKnee = 2.75f;     // SELF: where the feedback's bend comes in: RESONANCE 10 sings about 6 dB under the saw's peak
constexpr float kVcfKick = 1e-4f;         // SELF: put into stage 1 at a key, so RESONANCE 10 can start from silence
constexpr double kHpfLowHz = 10.0;        // SELF: HPF just over 0
constexpr double kHpfHighHz = 1000.0;     // SELF: HPF 10
constexpr float kVcfCvLands = 1e-3f;     // SELF: the cutoff's held CV this near (in octaves: 1.2 cents) is on its target
constexpr float kSnap = 1e-20f;           // a state this small is 0 (no number too small to hold)

// The cutoff's CV in octaves over kVcfLowHz: CUTOFF's dial (0..100) alone.
inline double cutoffOctaves(double dial) { return dial / 100.0 * std::log2(kVcfHighHz / kVcfLowHz); }
// The HPF's corner in octaves over kHpfLowHz (its dial over 0; 0 is off).
inline double hpfOctaves(double dial) { return (dial <= 0.0 ? 0.0 : dial / 100.0) * std::log2(kHpfHighHz / kHpfLowHz); }
// RESONANCE's dial (0..100) to the feedback round the four stages.
inline float resFeedback(double dial) { return static_cast<float>((dial < 0.0 ? 0.0 : (dial > 100.0 ? 100.0 : dial)) / 100.0) * kResTop; }

// 2 to the x: a power of two by its whole part (exact), the rest by a polynomial fitted to 2^f on
// [0, 1] (least squares on Chebyshev points; 1.9e-9 at worst, 3e-6 of a cent).
// (The whole part is made a power of two by its bits, not by floor and ldexp, which are calls to
// the library: this is worked out on every sample a cutoff moves.) Held to -1000..1000 octaves.
inline double exp2Poly(double x) {
    x = x < -1000.0 ? -1000.0 : (x > 1000.0 ? 1000.0 : x);
    int whole = static_cast<int>(x);
    if (x < whole) --whole; // (toward minus infinity)
    const double f = x - whole;
    const double p = 1.0000000018154946 + f * (0.6931469870574735 + f * (0.24022979581426138 + f * (0.05548352472390537
                     + f * (0.009678472554575622 + f * (0.0012443081507187417 + f * 0.00021690609174615223)))));
    const std::uint64_t bits = static_cast<std::uint64_t>(whole + 1023) << 52;
    double two;
    std::memcpy(&two, &bits, sizeof two);
    return p * two;
}
// Sine and cosine of 0..1.42 (0.45 of a half turn) by their series to the 13th and 12th power:
// under 2e-8 there.
// (Each step a multiply by a reciprocal the compiler works out: a division by 6.0 is not.)
inline double sinPoly(double x) {
    const double x2 = x * x;
    return x * (1.0 - x2 * (1.0 / 6.0) * (1.0 - x2 * (1.0 / 20.0) * (1.0 - x2 * (1.0 / 42.0) * (1.0 - x2 * (1.0 / 72.0) * (1.0 - x2 * (1.0 / 110.0) * (1.0 - x2 * (1.0 / 156.0)))))));
}
inline double cosPoly(double x) {
    const double x2 = x * x;
    return 1.0 - x2 * 0.5 * (1.0 - x2 * (1.0 / 12.0) * (1.0 - x2 * (1.0 / 30.0) * (1.0 - x2 * (1.0 / 56.0) * (1.0 - x2 * (1.0 / 90.0) * (1.0 - x2 * (1.0 / 132.0))))));
}
// A trapezoidal one-pole's G = g / (1 + g), g = tan(pi f / rate): sin / (sin + cos). `hz` is held
// under the top already.
inline float onePoleG(double hz, double rate) {
    const double w = 3.14159265358979323846 * hz / rate, s = sinPoly(w);
    return static_cast<float>(s / (s + cosPoly(w)));
}
// A cutoff in Hz from octaves over `low`, held to the top.
inline double cornerHz(double octaves, double low, double rate) {
    const double hz = low * exp2Poly(octaves), top = kVcfTopOfRate * rate < kVcfTopHz ? kVcfTopOfRate * rate : kVcfTopHz;
    return hz < top ? hz : top;
}

// How much the feedback's bend lets through of what came into it (its gain, 1 when it is not
// bending): the clip is kFeedbackKnee T(x / kFeedbackKnee), T(z) = z (27 + z^2) / (27 + 9 z^2) up to
// |z| = 3 (where it is 1, flat, with no corner), 1 past it.
// It is given as a fraction, `over` / `under`, so the filter can fold its division into its own.
inline void bendGain(float x, float& over, float& under) {
    const float z = x * (1.f / kFeedbackKnee), z2 = z * z;
    if (z2 >= 9.f) over = 1.f, under = std::fabs(z);
    else over = 27.f + z2, under = 27.f + 9.f * z2;
}

// A stage's coefficient G (onePoleG) and the powers of it the filter's sum takes, worked out when
// the cutoff moves, not on every sample.
struct VcfCoef {
    float g = 0.f, h = 1.f;                  // G and 1 - G
    float g2 = 0.f, g3 = 0.f, g4 = 0.f;      // G^2, G^3, G^4
    float r1 = 0.f, r2 = 0.f, r3 = 0.f;      // G^3 (1 - G), G^2 (1 - G), G (1 - G)
    void set(float G) {
        g = G, h = 1.f - G, g2 = G * G, g3 = g2 * G, g4 = g2 * g2;
        r1 = g3 * h, r2 = g2 * h, r3 = G * h;
    }
};

// THE IR3109: four stages and their feedback.
struct Ir3109 {
    float s[4] = {};    // each stage's state (its capacitor)
    float fed = 0.f;    // the feedback on the sample before: its bend is taken from it

    void reset() {
        for (float& x : s) x = 0.f;
        fed = 0.f;
    }
    // A key: a fixed small number into stage 1, so a filter on the edge with nothing coming in
    // still starts to sing, and the same bits every time.
    void kick() { s[0] += kVcfKick; }

    // One sample: four trapezoidal one-poles of coefficient G, the 4th fed back to the 1st through
    // the bend (taken from the sample before as a gain), solved as a straight sum. Stage n's output
    // is G y(n-1) + (1 - G) s(n); each is written out from the input and the states, so the four
    // are worked out side by side, not one after another (a sample's work waits on the last one's:
    // the shorter that chain, the quicker).
    float run(float x, const VcfCoef& c, float k) {
        float over, under; // the feedback's bend, kb = k over / under
        bendGain(fed, over, under);
        // The 4th stage's output: y4 = G^4 (x - kb y4) + rest, so y4 = (G^4 x + rest) / (1 + kb G^4),
        // and what is fed back, kb y4, is k over (G^4 x + rest) / (under + k over G^4): one division for both.
        const float rest = (c.r1 * s[0] + c.r2 * s[1]) + (c.r3 * s[2] + c.h * s[3]), sum = c.g4 * x + rest;
        const float q = 1.f / (under + k * over * c.g4), kq = k * over * q;
        const float y4 = under * sum * q, u = x - kq * sum;
        const float y1 = c.g * u + c.h * s[0];
        const float y2 = c.g2 * u + (c.r3 * s[0] + c.h * s[1]);
        const float y3 = c.g3 * u + (c.r2 * s[0] + (c.r3 * s[1] + c.h * s[2]));
        s[0] = snap(2.f * y1 - s[0]);
        s[1] = snap(2.f * y2 - s[1]);
        s[2] = snap(2.f * y3 - s[2]);
        s[3] = snap(2.f * y4 - s[3]);
        fed = snap(k * y4);
        return y4;
    }
    static float snap(float v) { return std::fabs(v) < kSnap ? 0.f : v; }
};

// THE HPF: one trapezoidal pole, a high-pass (what is left when its low-pass is taken off).
struct HighPass {
    float s = 0.f;
    float run(float x, float g) {
        const float v = (x - s) * g, low = v + s;
        s = 2.f * low - s;
        if (std::fabs(s) < kSnap) s = 0.f;
        return x - low;
    }
};

} // namespace jx3p
} // namespace apotheca
