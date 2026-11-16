#pragma once
// PHYSIC's BODY (docs/plans/physic-plan.md, section 2; batch 10): the box a string or a tine sits
// on. One a track, after the voices: a few resonances (physic_rule.hpp, bodyResonances) run on the
// middle of the voices' sum, (L + R) / 2, and added back to both sides, so it never moves a voice
// off where WIDTH put it. TUBES is not here: it is done in each voice.
//
// Each resonance is a coupled-form resonator, as a mode (modes.hpp): z' = r e^(iw) z + g in, heard
// as Re z, which at its frequency is in step with what comes in and g / (2 (1 - r)) of it, so
// g = 2 (1 - r) kBodyPeak: at BODY MIX 100 the sound at a resonance is twice what it was (+6 dB).
//
// A sample at a time, nothing worked out on a block's edge, so it is the same however the sound is
// cut. SMALL NUMBERS: each state under kBodySnap is set to exact zero on every sample, and a block
// that comes in silent to a body all zeros is not run (it would give zeros).

#include <cmath>

#include "apotheca/physic_rule.hpp"

namespace apotheca {
namespace physic {

constexpr float kBodySnap = 1e-15f;

struct BodyBank {
    int count = 0;
    float c[kBodyModesMost] = {}, s[kBodyModesMost] = {}, g[kBodyModesMost] = {};
    float x[kBodyModesMost] = {}, y[kBodyModesMost] = {};

    void clear() {
        for (int k = 0; k < kBodyModesMost; ++k) x[k] = y[k] = 0.f;
    }
    // The resonances of `body` at `sampleRate`. The state is kept (a body changed while it rings goes on
    // from where it was); one with fewer resonances lets the rest go.
    void set(int body, double sampleRate) {
        BodyResonance r[kBodyModesMost];
        const int n = bodyResonances(body, r);
        for (int k = 0; k < kBodyModesMost; ++k) {
            if (k >= n) {
                c[k] = s[k] = g[k] = x[k] = y[k] = 0.f;
                continue;
            }
            const double radius = radiusOf(rateOfT60(r[k].t60), sampleRate);
            const double w = 6.283185307179586 * r[k].hz / sampleRate;
            c[k] = static_cast<float>(radius * std::cos(w));
            s[k] = static_cast<float>(radius * std::sin(w));
            g[k] = static_cast<float>(2.0 * (1.0 - radius) * kBodyPeak);
        }
        count = n;
    }
    static float slid(float m, float to, float step) { return m < to ? (m + step >= to ? to : m + step) : (m - step <= to ? to : m - step); }
    bool silent() const {
        for (int k = 0; k < count; ++k)
            if (x[k] != 0.f || y[k] != 0.f) return false;
        return true;
    }
    // `n` samples: each side gets `mix` (a sample at a time, from `mixNow` towards `mixTo` by `mixStep`)
    // of the body's sound added.
    void run(float* left, float* right, int n, float& mixNow, float mixTo, float mixStep) {
        if (count == 0) return;
        if (mixNow == 0.f && mixTo == 0.f) { // heard at nothing: let it go (its ring went to nothing as the mix fell)
            clear();
            return;
        }
        if (silent()) {
            bool quiet = true;
            for (int i = 0; i < n && quiet; ++i) quiet = left[i] == 0.f && right[i] == 0.f;
            if (quiet) { // nothing to add: only the mix's slide goes on, a sample at a time as below
                for (int i = 0; i < n && mixNow != mixTo; ++i) mixNow = slid(mixNow, mixTo, mixStep);
                return;
            }
        }
        float lx[kBodyModesMost], ly[kBodyModesMost];
        for (int k = 0; k < kBodyModesMost; ++k) lx[k] = x[k], ly[k] = y[k];
        float m = mixNow;
        for (int i = 0; i < n; ++i) {
            if (m != mixTo) m = slid(m, mixTo, mixStep);
            const float in = (left[i] + right[i]) * 0.5f;
            float out = 0.f;
            for (int k = 0; k < count; ++k) {
                const float nx = (c[k] * lx[k] - s[k] * ly[k]) + g[k] * in;
                const float ny = s[k] * lx[k] + c[k] * ly[k];
                lx[k] = nx > -kBodySnap && nx < kBodySnap ? 0.f : nx;
                ly[k] = ny > -kBodySnap && ny < kBodySnap ? 0.f : ny;
                out += nx;
            }
            left[i] += m * out;
            right[i] += m * out;
        }
        mixNow = m;
        for (int k = 0; k < kBodyModesMost; ++k) x[k] = lx[k], y[k] = ly[k];
    }
};

} // namespace physic
} // namespace apotheca
