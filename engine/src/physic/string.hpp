#pragma once
// PHYSIC's STRING (docs/plans/physic-plan.md, section 2): an extended Karplus-Strong loop (KS, JS).
// A delay line holds the string's wave; each sample round the loop goes through
//
//   the line (N whole samples) -> the tuning allpass (the part d of a sample left, JS)
//     -> the stiffness allpasses (kDispersion alike: F64's sharp high partials, VDS)
//     -> the loop's low-pass (how fast the high partials die: the damping law, VHKJ's fit)
//     -> a DC blocker (physic_rule.hpp, kDcCorner: the loop's own DC must not ring on)
//     -> back into the line, with what the strike pushes in.
//
// The whole loop's delay at the note's pitch is solved in double (physic_rule.hpp, tuningOf), so
// the string is in tune to a small part of a cent on every key; an allpass (not a Lagrange read)
// passes every frequency whole, so DECAY is the same on every key.
//
// A glide changes N on a tick: the allpass's state is then set from the new tap (the plan's
// measure: a naive switch spikes 9.5 times the sound's size, this 3.9).
//
// SMALL NUMBERS: the filters' states snap under kStringSnap on each tick; a voice that goes to
// sleep sets its whole line to zeros, so a sleeping string is zeros.

#include <cstring>

#include "apotheca/physic_rule.hpp"

namespace apotheca {
namespace physic {

constexpr float kStringSnap = 1e-20f;

struct StringLoop {
    float* line = nullptr; // a slot's own (Physic's shared block: kSlots of them)
    int mask = 0;          // the line's length less one (a power of two)
    int w = 0;             // where the next sample is written
    int whole = 1;         // N
    float eta = 0.f;       // the tuning allpass
    float apIn = 0.f, apOut = 0.f;
    float disp = 0.f;      // the stiffness allpasses' coefficient (0: none, and no delay)
    float dIn[kDispersion] = {}, dOut[kDispersion] = {};
    float pole = 0.f, feed = 0.f; // the loop's low-pass: lp = feed v + pole lp (feed = gain (1 - pole))
    float lp = 0.f;
    float dcR = 0.f, dcG = 0.f;   // the DC blocker: hp = dcG (lp - dcIn) + dcR hp (dcG = (1 + R) / 2)
    float dcIn = 0.f, dcOut = 0.f;
    // A re-strike's damping: the whole string is turned down a sample at a time (a finger on it,
    // not a loss once a pass), which the loop alone cannot do for a string longer than the damping.
    // The line holds the sound over `scale`: read times it, written over it, and `scale` falls by
    // `damp` a sample. When the damping ends, settle() puts the line back as the sound itself.
    float damp = 1.f;      // a sample (1: none)
    float scale = 1.f;

    void clear() {
        if (line) std::memset(line, 0, sizeof(float) * static_cast<std::size_t>(mask + 1));
        w = 0;
        apIn = apOut = lp = dcIn = dcOut = 0.f;
        for (int k = 0; k < kDispersion; ++k) dIn[k] = dOut[k] = 0.f;
        damp = 1.f;
        scale = 1.f;
    }
    void settle() {
        if (scale != 1.f)
            for (int i = 0; i <= mask; ++i) line[i] *= scale;
        scale = 1.f;
        damp = 1.f;
    }

    // A new length (a glide's tick, or the strike). The allpass's state is set from the new tap.
    void retune(const Tuning& t) {
        if (t.whole != whole) {
            // The new tap's sample before this one is what the allpass last took in; what it last
            // gave out stays (the whole loop's delay is the same either side of the switch, so the
            // sound it gave out is what the new length would have given).
            apIn = line[(w - 1 - t.whole) & mask] * scale;
        }
        whole = t.whole;
        eta = static_cast<float>(t.eta);
    }
    void setFilter(const LoopFilter& f, double dcPole) {
        pole = static_cast<float>(f.pole);
        feed = static_cast<float>(f.gain * (1.0 - f.pole));
        dcR = static_cast<float>(dcPole);
        dcG = static_cast<float>(0.5 * (1.0 + dcPole));
    }

    // `n` samples of the string into `dst` (written). `push`: what the strike puts into the loop
    // each sample, or null. On locals, as the modes.
    void run(const float* push, float* dst, int n) {
        float* const L = line;
        const int m = mask, N = whole;
        int at = w;
        float e = eta, ai = apIn, ao = apOut, p = pole, f = feed, low = lp;
        float hi = dcIn, ho = dcOut;
        const float hr = dcR, hg = dcG;
        const float c = disp;
        if (damp != 1.f) { // damped: no push meanwhile (a re-strike's pluck waits for it)
            float g = scale;
            const float d = damp;
            for (int i = 0; i < n; ++i) {
                g *= d;
                const float r = L[(at - N) & m] * g;
                float v = e * (r - ao) + ai;
                ai = r, ao = v;
                for (int k = 0; k < kDispersion && c != 0.f; ++k) {
                    const float o = c * (v - dOut[k]) + dIn[k];
                    dIn[k] = v, dOut[k] = o;
                    v = o;
                }
                low = f * v + p * low;
                const float h = hg * (low - hi) + hr * ho;
                hi = low, ho = h;
                L[at] = h / g;
                at = (at + 1) & m;
                dst[i] = h;
            }
            scale = g;
        } else if (c == 0.f) {
            for (int i = 0; i < n; ++i) {
                const float r = L[(at - N) & m];
                const float o = e * (r - ao) + ai;
                ai = r, ao = o;
                low = f * o + p * low;
                const float h = hg * (low - hi) + hr * ho;
                hi = low, ho = h;
                L[at] = push ? h + push[i] : h;
                at = (at + 1) & m;
                dst[i] = h;
            }
        } else {
            float di[kDispersion], dout[kDispersion];
            for (int k = 0; k < kDispersion; ++k) di[k] = dIn[k], dout[k] = dOut[k];
            for (int i = 0; i < n; ++i) {
                const float r = L[(at - N) & m];
                float v = e * (r - ao) + ai;
                ai = r, ao = v;
                for (int k = 0; k < kDispersion; ++k) {
                    const float o = c * (v - dout[k]) + di[k];
                    di[k] = v, dout[k] = o;
                    v = o;
                }
                low = f * v + p * low;
                const float h = hg * (low - hi) + hr * ho;
                hi = low, ho = h;
                L[at] = push ? h + push[i] : h;
                at = (at + 1) & m;
                dst[i] = h;
            }
            for (int k = 0; k < kDispersion; ++k) dIn[k] = di[k], dOut[k] = dout[k];
        }
        w = at, apIn = ai, apOut = ao, lp = low, dcIn = hi, dcOut = ho;
    }

    // On the tick: a filter's state under kStringSnap is set to exact zero.
    void snap() {
        const auto s = [](float& v) {
            if (v > -kStringSnap && v < kStringSnap) v = 0.f;
        };
        s(apIn), s(apOut), s(lp), s(dcIn), s(dcOut);
        for (int k = 0; k < kDispersion; ++k) s(dIn[k]), s(dOut[k]);
    }
    // The loudest sample in the line (when the voice is all but asleep: the line's whole length).
    float loudest() const {
        float most = 0.f;
        for (int i = 0; i <= mask; ++i) {
            const float v = line[i] < 0.f ? -line[i] : line[i];
            most = v > most ? v : most;
        }
        return most;
    }
};

} // namespace physic
} // namespace apotheca
