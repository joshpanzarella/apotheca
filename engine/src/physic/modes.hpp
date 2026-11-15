#pragma once
// PHYSIC's bank of modes (docs/plans/physic-plan.md, section 2): up to kModesMost resonators a
// voice, each one way the object rings.
//
// A mode is a COUPLED-FORM resonator (GS): a point in the plane turned by a fixed angle each
// sample and shrunk a little. Its pitch is held in sin and cos of the angle, which a float holds
// finely at any pitch (the usual two-pole form keeps it in cos w, so close to 1 at low pitch that
// a float is cents out: +2.57 at A0, measured in the plan). Four multiplies and two adds, the
// same as the usual form.
//
//   x' = c x - s y + in g        c = r cos w, s = r sin w
//   y' = s x + c y               the mode's sound is y (a struck mode starts as a sine, from 0)
//
// The modes are kept in groups of four (a group's unused places have no gain and no state), the
// sum of each place kept apart through a sample and added in one fixed order at its end: the
// handheld builds without -ffast-math, so the compiler will not reorder a float sum by itself,
// and a fixed order keeps the sound the same however it is cut into blocks.
//
// SMALL NUMBERS: each mode snaps its own state to exact zeros on every tick when both of its
// numbers are under kModeSnap (high modes die long before the voice does, toward WOOD many
// times faster), and a group all zeros and not fed is not run. So no number too small to hold
// with flush-to-zero off is ever made (tests/physic_test.cpp, testTooSmall).

#include "apotheca/physic_rule.hpp"

namespace apotheca {
namespace physic {

constexpr int kGroups = kModesMost / 4;
constexpr float kModeSnap = 1e-15f;

struct ModeBank {
    float x[kModesMost] = {}, y[kModesMost] = {}; // the state
    float c[kModesMost] = {}, s[kModesMost] = {}; // r cos w, r sin w
    float in[kModesMost] = {};                    // how much of a strike reaches each mode
    float out[kModesMost] = {};                   // how much of each is heard (the fade near Nyquist)
    int groups = 0;                               // groups run: the last that can sound, and those before it

    void clear() { *this = ModeBank(); }

    // `n` samples of the bank's sound into `dst` (written, not added). `feed`: the strike's push for
    // each sample, or null when nothing strikes. Worked on locals (the compiler keeps them in
    // registers; on the members it cannot, across the writes to dst).
    void run(const float* feed, float* dst, int n) {
        const int g = groups;
        if (g == 0) {
            for (int i = 0; i < n; ++i) dst[i] = 0.f;
            return;
        }
        float lx[kModesMost], ly[kModesMost], lc[kModesMost], ls[kModesMost], lin[kModesMost], lout[kModesMost];
        const int count = 4 * g;
        for (int k = 0; k < count; ++k) {
            lx[k] = x[k], ly[k] = y[k], lc[k] = c[k], ls[k] = s[k], lin[k] = in[k], lout[k] = out[k];
        }
        if (feed) {
            for (int i = 0; i < n; ++i) {
                const float f = feed[i];
                float acc[4] = {0.f, 0.f, 0.f, 0.f};
                for (int q = 0; q < g; ++q) {
                    float* X = lx + 4 * q;
                    float* Y = ly + 4 * q;
                    const float* C = lc + 4 * q;
                    const float* S = ls + 4 * q;
                    const float* I = lin + 4 * q;
                    const float* O = lout + 4 * q;
                    for (int j = 0; j < 4; ++j) {
                        const float nx = (C[j] * X[j] - S[j] * Y[j]) + I[j] * f;
                        const float ny = S[j] * X[j] + C[j] * Y[j];
                        X[j] = nx;
                        Y[j] = ny;
                        acc[j] += O[j] * ny;
                    }
                }
                dst[i] = (acc[0] + acc[1]) + (acc[2] + acc[3]);
            }
        } else {
            for (int i = 0; i < n; ++i) {
                float acc[4] = {0.f, 0.f, 0.f, 0.f};
                for (int q = 0; q < g; ++q) {
                    float* X = lx + 4 * q;
                    float* Y = ly + 4 * q;
                    const float* C = lc + 4 * q;
                    const float* S = ls + 4 * q;
                    const float* O = lout + 4 * q;
                    for (int j = 0; j < 4; ++j) {
                        const float nx = C[j] * X[j] - S[j] * Y[j];
                        const float ny = S[j] * X[j] + C[j] * Y[j];
                        X[j] = nx;
                        Y[j] = ny;
                        acc[j] += O[j] * ny;
                    }
                }
                dst[i] = (acc[0] + acc[1]) + (acc[2] + acc[3]);
            }
        }
        for (int k = 0; k < count; ++k) x[k] = lx[k], y[k] = ly[k];
    }

    // On the tick: every mode under kModeSnap is set to exact zeros, and the groups at the top that
    // are all zeros (and are not being fed, `fed`) are no longer run. `modes`: how many there are.
    void snap(int modes, bool fed) {
        int last = -1;
        for (int k = 0; k < 4 * groups; ++k) {
            if (x[k] > -kModeSnap && x[k] < kModeSnap && y[k] > -kModeSnap && y[k] < kModeSnap) x[k] = y[k] = 0.f;
            if (x[k] != 0.f || y[k] != 0.f) last = k;
        }
        if (fed) last = modes - 1;
        groups = last < 0 ? 0 : last / 4 + 1;
    }
    bool silent() const {
        for (int k = 0; k < 4 * groups; ++k)
            if (x[k] != 0.f || y[k] != 0.f) return false;
        return true;
    }
};

} // namespace physic
} // namespace apotheca
