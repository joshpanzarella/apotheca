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
// a ratio of polynomials, not tanh: the transistor pair the SN names, mTR103
// and 104, bends as a tanh does); the bend is taken as a gain (how much the clip
// takes off the feedback) at where the feedback is GOING this sample, read
// ahead from the last three (`ahead`: exact for a steady part plus a sine at
// the cutoff, which is what the loop holds when it sings), so the solve stays
// linear and needs one division and no iterations. That bend is what holds the
// singing to one level, and it moves no pitch (it scales the feedback, not a
// stage's cutoff).
//
// THE OWNER'S REPORT (2026-11-15: "the resonance makes the low end basically
// disappear and when it is turned up all the way it just squeals"), measured and
// mended 2026-11-16 (the plan, "The filter's resonance: the owner's report"):
//  - The bend was taken from the sample BEFORE. Where the filter sings that is
//    a sample late: it sang sharp of its cutoff (10 cents at 1 kHz, 30 at 4
//    kHz) and 3 dB louder at 10 kHz than at 1 kHz, and under a 12.5 kHz cutoff
//    it stood 13 dB over the note it was played with, beaten by that note into
//    sidebands 6 dB under itself (a buzz). Read ahead, it sings on its cutoff
//    within 0.5 cents at every cutoff, at one level (7.2 dB under a saw's peak,
//    80 Hz to 10 kHz), and under 12.5 kHz it is 6.7 dB UNDER the note.
//  - RESONANCE 10 was feedback 4.6 and the edge (4) was at 8.7: the top 13% of
//    the knob all sang, 6 dB under a saw's peak, over a note the bass loss had
//    taken 13 to 15 dB down: a sine at the cutoff 10 dB over the note. Now it
//    is straight to the edge at 9.5, then climbs to 5 at 10 (kResTop): from 9.6
//    it sings. (Straight to 4.21 at 10, so near the edge, A3 Organ I's two
//    DCOs, 27 cents apart, took turns to hold the singing and it played 27
//    cents flat: the singing must be strong enough to hold DCO-1's harmonic.)
//  - The low end is mostly given back: a share of the input, low-passed (two
//    plain one-poles at a quarter of the cutoff), is added to what comes out,
//    outside the loop, so it moves no pitch and no edge, and the octave under
//    the cutoff keeps its loss (Organ I's sine stays 13 dB over its 16' DCO).
//    At RESONANCE 8 to 10 a bass note is 4 to 5 dB under RESONANCE 0 (it was 13
//    to 14). Half of what the feedback takes is given back (kBassKept 0.5, at
//    0.7 A8 E. Piano I, at LEVEL 9.9, reached the VCA's clip).
//  - The bend had a flat top past three knees (a hard clip); it is gone: the
//    curve rises on past them at a ninth of its first slope, with no corner.
//  - At RESONANCE 0 (no feedback) the bend and its division are not worked
//    out at all, and the low end's poles are not run.
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
//   | CUTOFF 0 to 10               | 49.84 Hz to 49.84 kHz, even in octaves        | The span SELF; where it sits, |
//   |                              | (kVcfLowHz, kVcfHighHz), at KEY FOLLOW's      | NAM's Organ I: its filter sings |
//   |                              | reference note: held at the highest cutoff,   | the key played (the owner's call, |
//   |                              | so at key 60 CUTOFF 8.7 to 10 are one sound   | 2026-11-15; it was 15 Hz to 15 kHz) |
//   | The highest cutoff           | 20 kHz, and never past 0.45 of the rate       | SELF   |
//   | KEY FOLLOW's reference       | note 60 (kKeyFollowNote): KEY adds nothing    | SELF   |
//   |                              | there                                         |        |
//   | KEY FOLLOW's 100%            | 9.8 (jx3p.hpp: kKeyFollowFull)                | NAM (Organ I); the owner's call |
//   | ENV MOD 10                   | 10 octaves (kVcfEnvOctaves), straight in the  | SELF   |
//   |                              | dial; polarity turns it over                  | OM p.25 (polarity) |
//   | BRILLIANCE 0 / 5 / 10        | -2 / 0 / +2 octaves (kBrillianceOctaves)      | SELF (the knob is on the panel) |
//   | RESONANCE 0 to 10            | feedback straight from 0 to 4, the edge, at   | SELF; that 10 sings: NAM Organ I; |
//   |                              | 9.5 (kResEdgeDial), then to 5.0 at 10         | that 8.8 (B8 Juicy Funk) does not: |
//   |                              | (kResTop): it sings from 9.6 (was 0 to 4.6    | a reading of the booklet (2026-11-16) |
//   |                              | straight, singing from 8.7)                   |        |
//   | The feedback's bend          | x (27 + x^2) / (27 + 9 x^2) of x /            | SELF (its shape); that it is |
//   |                              | kFeedbackKnee, a ratio of polynomials, no     | the feedback's: SN p.6, mTR103, 104 |
//   |                              | flat top (was flat past 3 knees)              |        |
//   | Where the bend is read       | at where the feedback is going, from the last | SELF (was the sample before: |
//   |                              | three (exact for a steady part and a sine at  | until |
//   |                              | the cutoff)                                   | 2026-11-16) |
//   | kFeedbackKnee                | 2.0: the sine it sings at RESONANCE 10 is 7.2 | SELF: SN p.7 trims it to 600 mV, |
//   |                              | dB under the saw's peak (was 2.75: 6 dB, at   | but prints no saw figure |
//   |                              | 4.6), its harmonics 51 dB under it            |        |
//   | Starting to sing             | 1e-4 put into stage 1 at each key (kVcfKick)  | SELF   |
//   | Bass loss                    | half given back: the input, low-passed (two   | SELF: the IR3109 loses it (the |
//   |                              | one-poles at a quarter of the cutoff,         | Juno-60's does; the JX-3P's is |
//   |                              | kBassCorner), times kBassKept k / (1 + k),    | not written down). The owner's |
//   |                              | added to the output: (1 + k / 2) / (1 + k) of | report, 2026-11-15: "basically |
//   |                              | the low end comes out, 4 to 5 dB down at      | disappear" |
//   |                              | RESONANCE 8 to 10 (was 1 / (1 + k), 13 to 15) |        |
//   |                              | jx3p.hpp: kBassKept                           |        |
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

constexpr double kVcfLowHz = 49.84;       // CUTOFF 0. The owner's call (2026-11-15): set so NAM's Organ I (CUTOFF 2.4, KEY FOLLOW 9.8) sings the key played
constexpr double kVcfHighHz = 49840.0;    // CUTOFF 10, before the top holds it: the same span as the first guess (15 Hz to 15 kHz), ten octaves less a hair
constexpr double kVcfTopHz = 20000.0;     // SELF: the cutoff is held under this...
constexpr double kVcfTopOfRate = 0.45;    // ...and under this share of the sample rate
constexpr int kKeyFollowNote = 60;        // SELF: KEY FOLLOW turns about this key
constexpr double kVcfEnvOctaves = 10.0;   // SELF: ENV MOD 10
constexpr double kBrillianceOctaves = 2.0;// SELF: BRILLIANCE 0 and 10, from 5
constexpr double kResEdgeDial = 95.0;     // SELF: RESONANCE 9.5 is the edge, feedback 4 (it was 8.7): it sings from 9.6
constexpr double kResTop = 5.0;           // SELF: the feedback at RESONANCE 10 (it was 4.6): the last half step climbs to it
constexpr float kFeedbackKnee = 2.0f;     // SELF: where the feedback's bend comes in: RESONANCE 10 sings 7.2 dB under the saw's peak
constexpr double kBassCorner = 0.25;      // SELF: the low end given back is under this share of the cutoff (two one-poles there)
constexpr float kVcfKick = 1e-4f;         // SELF: put into stage 1 at a key, so RESONANCE 10 can start from silence
constexpr double kHpfLowHz = 10.0;        // SELF: HPF just over 0
constexpr double kHpfHighHz = 1000.0;     // SELF: HPF 10
constexpr float kVcfCvLands = 1e-3f;     // SELF: the cutoff's held CV this near (in octaves: 1.2 cents) is on its target
constexpr float kSnap = 1e-20f;           // a state this small is 0 (no number too small to hold)

// The cutoff's CV in octaves over kVcfLowHz: CUTOFF's dial (0..100) alone.
inline double cutoffOctaves(double dial) { return dial / 100.0 * std::log2(kVcfHighHz / kVcfLowHz); }
// The HPF's corner in octaves over kHpfLowHz (its dial over 0; 0 is off).
inline double hpfOctaves(double dial) { return (dial <= 0.0 ? 0.0 : dial / 100.0) * std::log2(kHpfHighHz / kHpfLowHz); }
// RESONANCE's dial (0..100) to the feedback round the four stages: straight up to the edge, 4 at
// kResEdgeDial, then straight again, steeper, to kResTop at 10. (Straight all the way to 5 the edge
// would be at 8.0; at 4.21 the singing at 10 is so near the edge that A3 Organ I's two DCOs, 27 cents
// apart, pull it about between them: the plan, "The filter's resonance".)
inline float resFeedback(double dial) {
    dial = dial < 0.0 ? 0.0 : (dial > 100.0 ? 100.0 : dial);
    return static_cast<float>(dial <= kResEdgeDial ? dial * (4.0 / kResEdgeDial) : 4.0 + (dial - kResEdgeDial) * ((kResTop - 4.0) / (100.0 - kResEdgeDial)));
}
// How much of the low-passed input is added to the output at feedback `k`, for a share `kept` of
// the low end the feedback takes away: the low end comes out (1 + kept k) / (1 + k) of itself.
// (Worked out when the feedback moves, not every sample: the division is a real one.)
inline float bassGiven(float k, float kept) { return kept * k / (1.f + k); }

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
// bending): the clip is kFeedbackKnee T(x / kFeedbackKnee), T(z) = z (27 + z^2) / (27 + 9 z^2). T rises
// all the way (its slope is 9 (z^2 - 9)^2 / (27 + 9 z^2)^2: 1 at 0, 0 at z = 3 only, a ninth far out),
// so past three knees it goes on rising slowly, with no corner and no flat top (it was held flat
// there, a clip, until 2026-11-15).
// It is given as a fraction, `over` / `under`, so the filter can fold its division into its own.
inline void bendGain(float x, float& over, float& under) {
    const float z = x * (1.f / kFeedbackKnee), z2 = z * z;
    over = 27.f + z2, under = 27.f + 9.f * z2;
}

// A stage's coefficient G (as onePoleG) and the powers of it the filter's sum takes, the low end's
// coefficient, and `turn`, 1 + 2 cos of the cutoff's turn a sample (what reads the singing ahead),
// all worked out when the cutoff moves, not on every sample: one division, as onePoleG's.
struct VcfCoef {
    float g = 0.f, h = 1.f;                  // G and 1 - G
    float g2 = 0.f, g3 = 0.f, g4 = 0.f;      // G^2, G^3, G^4
    float r1 = 0.f, r2 = 0.f, r3 = 0.f;      // G^3 (1 - G), G^2 (1 - G), G (1 - G)
    float low = 0.f;                         // the low end's two one-poles: each moves this share of the way a sample
    float turn = 3.f;                        // 1 + 2 cos(2 pi hz / rate)
    // `hz` held under the top already. G = sin / (sin + cos) of w = pi hz / rate (onePoleG); cos(2w) is
    // 1 - 2 sin^2. The low end's one-poles are the plain kind (a share of the way a sample), whose
    // corner is that share times rate / 2 pi: G / 2 puts it at a quarter of the cutoff (G is about w
    // there), a little under it as the cutoff nears the top (kBassCorner; a corner that needs no
    // second division).
    void set(double hz, double rate) {
        const double w = 3.14159265358979323846 * hz / rate, s = sinPoly(w);
        const float G = static_cast<float>(s / (s + cosPoly(w)));
        g = G, h = 1.f - G, g2 = G * G, g3 = g2 * G, g4 = g2 * g2;
        r1 = g3 * h, r2 = g2 * h, r3 = G * h;
        low = G * static_cast<float>(kBassCorner * 2.0);
        turn = static_cast<float>(3.0 - 4.0 * s * s);
    }
};

// THE IR3109: four stages and their feedback, and the low end given back beside them.
struct Ir3109 {
    float s[4] = {};    // each stage's state (its capacitor)
    float z[3] = {};    // the feedback over kFeedbackKnee on the last three samples, the latest first
    float l[2] = {};    // the low end's two poles

    void reset() {
        for (float& x : s) x = 0.f;
        for (float& x : z) x = 0.f;
        for (float& x : l) x = 0.f;
    }
    // A key: a fixed small number into stage 1, so a filter on the edge with nothing coming in
    // still starts to sing, and the same bits every time.
    void kick() { s[0] += kVcfKick; }

    // One sample: four trapezoidal one-poles of coefficient G, the 4th fed back to the 1st through
    // the bend, solved as a straight sum, and `given` (bassGiven) of the input's low end added.
    // Stage n's output is G y(n-1) + (1 - G) s(n); each is written out from the input and the states,
    // so the four are worked out side by side, not one after another (a sample's work waits on the
    // last one's: the shorter that chain, the quicker; the low end is off it).
    float run(float x, const VcfCoef& c, float k, float given) {
        const float rest = (c.r1 * s[0] + c.r2 * s[1]) + (c.r3 * s[2] + c.h * s[3]), sum = c.g4 * x + rest;
        float y4 = sum, u = x; // with no feedback (RESONANCE 0) the four stages are all there is
        if (k != 0.f) {
            // The bend's gain, kb = k over / under, at where the feedback is going: the last three
            // read ahead as a steady part plus a sine at the cutoff (p[n] = turn (p[n-1] - p[n-2]) +
            // p[n-3]; the latest is used last, so the rest waits on nothing).
            const float back = z[2] - c.turn * z[1], ahead = c.turn * z[0] + back, a2 = ahead * ahead;
            // The 4th stage's output: y4 = G^4 (x - kb y4) + rest, so y4 = (G^4 x + rest) / (1 + kb G^4)
            // = under sum / (under + k G^4 over), and what is fed back, kb y4, is k over sum / (that):
            // one division for both. The denominator is put together as 27 (1 + k G^4) + (9 + k G^4)
            // z^2, so it waits on the feedback for two steps, not four.
            const float kg = k * c.g4;
            const float q = 1.f / ((27.f + 27.f * kg) + (9.f + kg) * a2);
            y4 = (27.f + 9.f * a2) * sum * q, u = x - k * (27.f + a2) * sum * q;
        }
        const float y1 = c.g * u + c.h * s[0];
        const float y2 = c.g2 * u + (c.r3 * s[0] + c.h * s[1]);
        const float y3 = c.g3 * u + (c.r2 * s[0] + (c.r3 * s[1] + c.h * s[2]));
        s[0] = snap(2.f * y1 - s[0]);
        s[1] = snap(2.f * y2 - s[1]);
        s[2] = snap(2.f * y3 - s[2]);
        s[3] = snap(2.f * y4 - s[3]);
        z[2] = z[1], z[1] = z[0], z[0] = snap(k * (1.f / kFeedbackKnee) * y4);
        // The low end: two plain one-poles of the input at a quarter of the cutoff. At RESONANCE 0
        // (nothing given back) they are not run, and wait at 0 (a share given back starts with the
        // feedback's 1 ms hold, from nothing).
        if (given == 0.f) {
            l[0] = l[1] = 0.f;
            return y4;
        }
        l[0] = snap(l[0] + c.low * (x - l[0]));
        l[1] = snap(l[1] + c.low * (l[0] - l[1]));
        return y4 + given * l[1];
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
