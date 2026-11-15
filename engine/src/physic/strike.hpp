#pragma once
// PHYSIC's strike (docs/plans/physic-plan.md, section 2): what hits or plucks the object.
//
//   MALLET  a push of contact time T (physic_rule.hpp: contactSeconds, Hertz's law). Its
//           spectrum falls away above about 1/T, so a short (hard, or hard-hit) contact is
//           brighter. Into the modes it is a smooth bump of area 1, n q^n (no zero in its
//           spectrum: physic_rule.hpp, kBumpOfContact), so each mode rings at its gain times the
//           bump's spectrum; into a string it is a half-sine, the shape put on it.
//   PLUCK   STRING: one period of noise, smoothed by HARDNESS (a finger to a plectrum), through
//           the pick comb x(t) - x(t - P), P = POSITION of the period (JS): a string plucked at
//           1/5 has no 5th or 10th harmonic. A modal model: a 0.1 ms click, its high modes
//           turned down by a soft HARDNESS (a finger, SELF).
//
// Every sample of a strike is worked out from the sample's place since the strike began, never
// stepped along (the noise is a hash of the note's seed and that place), so a strike does not
// hang on how the sound is cut into blocks.

#include <cmath>
#include <cstdint>

#include "apotheca/physic_rule.hpp"

namespace apotheca {
namespace physic {

// sin(pi u) for u in [0, 1], from a polynomial (the same bits on every machine: no maths library).
inline float sinHalfTurn(float u) {
    u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    const float x = (u > 0.5f ? 1.f - u : u) * 3.14159265f; // 0..pi/2
    const float x2 = x * x;
    return x * (1.f + x2 * (-1.6666667e-1f + x2 * (8.3333333e-3f + x2 * (-1.9841270e-4f + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
}
// sin and cos of w for w in [0, pi], from polynomials, set on the circle: a glide's tick (an
// exact pitch is worked out in double when the glide lands).
inline void sinCos(float w, float& s, float& c) {
    const float h = 1.57079633f;
    const bool past = w > h;
    const float x = past ? 3.14159265f - w : w; // 0..pi/2
    const float x2 = x * x;
    s = x * (1.f + x2 * (-1.6666667e-1f + x2 * (8.3333333e-3f + x2 * (-1.9841270e-4f + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
    c = 1.f + x2 * (-0.5f + x2 * (4.1666667e-2f + x2 * (-1.3888889e-3f + x2 * (2.4801587e-5f + x2 * (-2.7557319e-7f + x2 * 2.0876757e-9f)))));
    if (past) c = -c;
    // Put on the circle: a radius off 1 by the polynomial's error would grow a long ring.
    const float r = 1.f / std::sqrt(s * s + c * c);
    s *= r, c *= r;
}

// A strike as the voice keeps it: what each of its samples is, from its place.
struct Push {
    enum Kind { None, Pulse, Noise, Bump } kind = None;
    std::uint32_t from = 0, to = 0; // in the voice's samples since its strike: when it pushes
    float length = 1.f;             // the pulse's contact, or the noise's period, in samples
    float size = 1.f;               // a pulse's height, or the noise's
    float comb = 0.f;               // the pick comb's delay in samples (0: none)
    float grain = 1.f;              // the noise's grain: one random value every `grain` samples
    std::uint32_t seed = 0;
    float q = 0.f;                  // a bump's: e^(-1/tau)
    float qn = 1.f;                 // ...q^n at `next`, stepped a sample at a time from its start
    std::uint32_t next = 0;

    bool on(std::uint32_t at) const { return kind != None && at < to; }

    float noiseAt(float t) const { // the smoothed noise, at a place t (0 up to `length`), else 0
        if (t < 0.f || t >= length) return 0.f;
        const float g = t / grain;
        const std::uint32_t i = static_cast<std::uint32_t>(g);
        const float frac = g - static_cast<float>(i);
        return pluckNoise(seed, i) * (1.f - frac) + pluckNoise(seed, i + 1u) * frac;
    }
    float pulseAt(float t) const { // the half-sine, at a place t
        if (t < 0.f || t >= length) return 0.f;
        return sinHalfTurn((t + 0.5f) / length);
    }
    float shapeAt(float t) const { return kind == Pulse ? pulseAt(t) : noiseAt(t); }

    // Samples `at` .. `at + n` of it into `out` (asked for in order: a bump is stepped along from its
    // start, the same steps however the sound is cut).
    void fill(std::uint32_t at, float* out, int n) {
        if (kind == Bump) {
            for (int i = 0; i < n; ++i) {
                const std::uint32_t k = at + static_cast<std::uint32_t>(i);
                if (k < from || k >= to) {
                    out[i] = 0.f;
                    continue;
                }
                if (k == from) next = from, qn = 1.f;
                while (next < k) qn *= q, ++next;
                out[i] = size * static_cast<float>(k - from) * qn;
            }
            return;
        }
        for (int i = 0; i < n; ++i) {
            const std::uint32_t k = at + static_cast<std::uint32_t>(i);
            if (k < from || k >= to) {
                out[i] = 0.f;
                continue;
            }
            const float t = static_cast<float>(k - from);
            float v = shapeAt(t);
            if (comb > 0.f) v -= shapeAt(t - comb);
            out[i] = v * size;
        }
    }
};

} // namespace physic
} // namespace apotheca
