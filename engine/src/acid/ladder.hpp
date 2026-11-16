#pragma once
// ACID's filter (docs/plans/acid-plan.md, section 4): a four-pole DIODE ladder, solved per sample.
//
// THE LADDER. In a diode ladder the four stages are not buffered from each other: each capacitor
// is joined to the one before and the one after through a diode pair, so a stage's current is what
// comes from the one before less what goes on to the next. As a circuit of four nodes (the input
// x at node 0):
//
//   c1 v1' = (x  - v1) - (v1 - v2)        c2 v2' = (v1 - v2) - (v2 - v3)
//   c3 v3' = (v2 - v3) - (v3 - v4)        c4 v4' = (v3 - v4)
//
// (time in units of 1 / the stages' corner). Coupled so, the poles spread: the slope just over the
// cutoff is gentler than four buffered stages', and it steepens towards 24 dB an octave further up
// (measured on this model: 13.9 dB from the singing pitch to an octave over, 18.4 the next octave,
// 21.9 the one after; four buffered stages give 16.1, 21.1, 23). The first capacitor is set apart
// (half the others: SELF, the plan's "for the 18 dB look"). The output is v4.
//
// THE FEEDBACK goes from v4 back to the input through a one-pole HIGH-PASS (a coupling capacitor
// in the loop: SELF, at kLoopHighPass of the stages' corner), so the bass under it is not fed back
// and keeps its level as RESONANCE rises; what is between it and the cutoff is thinned. Coupled
// stages lose a lot round the loop (at the pitch where the loop turns half a cycle, v4 is 28 dB
// under the input), so the feedback that makes it sing is about 24, not a buffered ladder's 4, and
// at that feedback the pass band is 26 dB down: a make-up gain (kMakeUp) gives most of it back,
// so at the edge the pass band is 9 dB under RESONANCE 0's and the singing stands over it.
//
// HOW IT IS SOLVED: each capacitor is a trapezoidal integrator (the "zero delay feedback" kind:
// v = g w + s, s' = 2 v - s), so the four nodes are a linear system each sample, tridiagonal; its
// inverse is worked out when the cutoff moves (on the voice's tick), and a sample is four sums (no
// iteration). The corner g is set so the filter SINGS exactly at the cutoff it
// reads: the pitch where the loop turns half a cycle is found once on the analog model
// (`ladderEdge`: w180 times the stages' corner, and the feedback there, kEdge), and the bilinear
// transform keeps both, at a pitch warped by tan: g = tan(pi f / rate) / w180. The loop's
// high-pass is in the same model and the same transform, so it moves neither.
//
// THE BEND (the JX-3P's lesson, CLAUDE.md): the feedback is bent by a soft clip (a ratio of
// polynomials, no flat top), taken as a gain at where the feedback is GOING this sample, read ahead
// from its last samples as a steady part plus a sine at the singing pitch. Taken from the sample
// before it is a sample late where the filter sings, and sings sharp. As a gain the solve stays
// linear: one division a sample.
//
// Rack-free and SDL-free; no allocation; every state snapped to 0 under 1e-20.

#include <cmath>
#include <complex>

#include "../jx3p/ir3109.hpp" // exp2Poly, sinPoly, cosPoly, bendGain's curve (the house's own polynomials)

namespace apotheca {
namespace acid {

constexpr double kStageOneCap = 0.5;  // SELF: c1 over the other three (the first capacitor set apart)
constexpr double kLoopHighPass = 0.1; // SELF: the loop's high-pass corner over the stages' corner
constexpr double kMakeUp = 0.33;      // SELF: the output times 1 + kMakeUp x feedback
constexpr float kFeedbackKnee = 2.0f; // SELF: where the feedback's bend comes in (sets how loud it sings)
constexpr float kSnap = 1e-20f;       // a state this small is 0 (no number too small to hold)

// The analog ladder's response v4 / u at jw (w in units of the stages' corner), times the loop's
// high-pass.
inline std::complex<double> ladderLoop(double w) {
    const std::complex<double> s(0.0, w);
    const double c[4] = {kStageOneCap, 1.0, 1.0, 1.0};
    // (diag(s c) - A) v = e1, A the ladder's: elimination by hand on the tridiagonal.
    std::complex<double> d[4], r[4];
    const double off = 1.0; // every off-diagonal is -1
    d[0] = s * c[0] + 2.0, r[0] = 1.0;
    for (int i = 1; i < 4; ++i) {
        const std::complex<double> m = off / d[i - 1];
        d[i] = s * c[i] + (i == 3 ? 1.0 : 2.0) - m * off;
        r[i] = m * r[i - 1];
    }
    const std::complex<double> v4 = r[3] / d[3];
    return v4 * (s / (s + kLoopHighPass));
}
// Where the loop turns half a cycle (w180, in units of the stages' corner) and the feedback that
// makes it sing there (the edge): found by halving, once. The phase falls all the way from +90
// (the high-pass) to -360 degrees, through -180 once.
struct Edge { double w180 = 1.0, k = 4.0; };
inline Edge ladderEdge() {
    const auto turned = [](double w) {
        // The phase unwrapped: the loop's own (it falls), less a whole turn where it has wrapped past -pi.
        double sum = 0.0, last = std::arg(ladderLoop(1e-4));
        const int steps = 400;
        for (int i = 1; i <= steps; ++i) {
            const double here = std::arg(ladderLoop(1e-4 * std::pow(w / 1e-4, static_cast<double>(i) / steps)));
            double d = here - last;
            while (d > 3.141592653589793) d -= 2.0 * 3.141592653589793;
            while (d < -3.141592653589793) d += 2.0 * 3.141592653589793;
            sum += d, last = here;
        }
        return std::arg(ladderLoop(1e-4)) + sum;
    };
    double lo = 0.2, hi = 10.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = std::sqrt(lo * hi);
        (turned(mid) > -3.141592653589793 ? lo : hi) = mid;
    }
    Edge e;
    e.w180 = std::sqrt(lo * hi);
    e.k = 1.0 / std::abs(ladderLoop(e.w180));
    return e;
}

// What the solve needs, worked out when the singing pitch moves (from the polynomials: no tan):
// the system's inverse, so each sample is four sums side by side, not an elimination one step after
// another (a sample's work waits on the last one's: the shorter that chain, the quicker).
struct LadderCoef {
    float w[4][4] = {};                      // the inverse of (I - g D A): v = w s + col u
    float col[4] = {};                       // what the input u adds to each node: w[i][0] times gamma1
    float hpG = 0.f, hpH = 1.f;              // the loop's high-pass: G and 1 - G of its trapezoidal one-pole
    float turn = 3.f;                        // 1 + 2 cos of the singing pitch's turn a sample (the read-ahead)
    // `hz` is held under the top already; `w180` from ladderEdge.
    void set(double hz, double rate, double w180) {
        const double wt = 3.14159265358979323846 * hz / rate, sn = jx3p::sinPoly(wt), cs = jx3p::cosPoly(wt);
        const double gg = sn / (cs * w180), gamma1 = gg / kStageOneCap;
        // (I - g D A) is tridiagonal: its rows (1 + 2 gamma1, -gamma1), (-g, 1 + 2g, -g) twice, (-g, 1 + g).
        // Eliminated once (the pivots d), then each column of the identity solved by it.
        double d[4], m[4] = {0.0, 0.0, 0.0, 0.0};
        d[0] = 1.0 + 2.0 * gamma1;
        m[1] = gg / d[0], d[1] = 1.0 + 2.0 * gg - m[1] * gamma1;
        m[2] = gg / d[1], d[2] = 1.0 + 2.0 * gg - m[2] * gg;
        m[3] = gg / d[2], d[3] = 1.0 + gg - m[3] * gg;
        for (int j = 0; j < 4; ++j) {
            double r[4];
            for (int i = 0; i < 4; ++i) r[i] = i == j ? 1.0 : 0.0;
            for (int i = 1; i < 4; ++i) r[i] += m[i] * r[i - 1];
            double x[4];
            x[3] = r[3] / d[3];
            x[2] = (r[2] + gg * x[3]) / d[2];
            x[1] = (r[1] + gg * x[2]) / d[1];
            x[0] = (r[0] + gamma1 * x[1]) / d[0];
            for (int i = 0; i < 4; ++i) w[i][j] = static_cast<float>(x[i]);
        }
        for (int i = 0; i < 4; ++i) col[i] = static_cast<float>(static_cast<double>(w[i][0]) * gamma1);
        const double gh = kLoopHighPass * gg;
        hpG = static_cast<float>(gh / (1.0 + gh)), hpH = 1.f - hpG;
        turn = static_cast<float>(3.0 - 4.0 * sn * sn);
    }
};

struct Ladder {
    float s[4] = {}; // each capacitor's integrator state
    float h = 0.f;   // the loop's high-pass
    float z[4] = {}; // the feedback over kFeedbackKnee on the last four samples, the latest first

    void reset() { *this = Ladder(); }
    // One sample in, v4 out, at feedback `k`.
    float run(float x, const LadderCoef& c, float k) {
        // What each node would be with no input (the states alone), four sums side by side.
        float r[4];
        for (int i = 0; i < 4; ++i) r[i] = (c.w[i][0] * s[0] + c.w[i][1] * s[1]) + (c.w[i][2] * s[2] + c.w[i][3] * s[3]);
        float u = x;
        if (k != 0.f) {
            // The bend's gain at where the feedback is going (the JX-3P's read-ahead), read TWO samples
            // ahead from the three before the last (p[n] = turn (p[n-1] - p[n-2]) + p[n-3], twice:
            // exact for a steady part plus a sine at the singing pitch, as the JX-3P's), so the
            // division waits on nothing of the sample before: it is off the chain one sample's work
            // waits on (read from the latest it was; 0.21% of a core became 0.15%).
            const float one = c.turn * (z[1] - z[2]) + z[3], ahead = c.turn * (one - z[1]) + z[2], a2 = ahead * ahead;
            // hp(u) = H (v4 - h), v4 = r4 + col4 u; u = x - k (27 + a2) / (27 + 9 a2) hp(u).
            const float under = 27.f + 9.f * a2, kover = k * (27.f + a2) * c.hpH;
            const float q = 1.f / (under + kover * c.col[3]);
            u = (x * under - kover * (r[3] - h)) * q;
        }
        const float v4 = r[3] + c.col[3] * u;
        s[0] = snap(2.f * (r[0] + c.col[0] * u) - s[0]);
        s[1] = snap(2.f * (r[1] + c.col[1] * u) - s[1]);
        s[2] = snap(2.f * (r[2] + c.col[2] * u) - s[2]);
        s[3] = snap(2.f * v4 - s[3]);
        const float low = c.hpG * v4 + c.hpH * h, hp = v4 - low;
        h = snap(2.f * low - h);
        z[3] = z[2], z[2] = z[1], z[1] = z[0], z[0] = snap(k * (1.f / kFeedbackKnee) * hp);
        return v4;
    }
    static float snap(float v) { return std::fabs(v) < kSnap ? 0.f : v; }
};

} // namespace acid
} // namespace apotheca
