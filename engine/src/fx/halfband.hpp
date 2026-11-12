#pragma once
// Twice the sample rate, and back. For an effect that bends the sound's
// shape (SATURATE): bending makes new high notes, and any too high for the
// sample rate fold back down as notes that were never played (aliasing).
// Worked out at twice the rate, there is an octave more room for them, and
// they are taken out on the way back down.
//
//   Halfband up, down;                 one of each for each side
//   up.up(in, twice, frames);          `frames` samples in, 2 * frames out
//   ...bend `twice`...
//   down.down(twice, out, frames);     2 * frames in, `frames` out
//   up.settle(); down.settle();        now and then: see settle()
//
// What it is: a half-band filter made of two short chains of all-pass stages
// (four stages in all), which is the cheapest kind: eight multiplies for a
// sample up and back. It adds no delay worth the name (2.3 samples for up
// and down together, low down), so the plain sound can still be mixed in
// beside what comes out.
//
// Measured (tests/effects_test.cpp, testHalfband):
//   up to 19.2 kHz at 48 kHz        passed whole (within 0.01 dB)
//   above 28.8 kHz at the 2x rate   70 dB down, so what folds back is 70 dB down
//   between the two                 falls away: notes folding back to above 19.2 kHz
//                                   are only partly taken out (nobody hears up there)
//
// No allocation; safe on the audio thread.

#include <cmath>

namespace apotheca {
namespace fx {

// The stages' numbers: an elliptic half-band, 70 dB, a tenth of the 2x rate
// between "passed" and "stopped". The first chain has the 1st and 3rd of
// them, the second the 2nd and 4th.
constexpr float kHalfbandStage[4] = {0.0798664262f, 0.5453236511f, 0.2838293449f, 0.8344118915f};

class Halfband {
public:
    void reset() {
        for (float& v : x_) v = 0.f;
        for (float& v : y_) v = 0.f;
    }
    // `frames` in, 2 * frames out.
    void up(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) {
            out[2 * i] = stage(1, stage(0, in[i]));
            out[2 * i + 1] = stage(3, stage(2, in[i]));
        }
    }
    // A stage that has rung down to nearly nothing is nothing. Call this now
    // and then (SATURATE: every 64 samples). Left to itself a stage never
    // gets to zero: it sticks at the smallest number there is, and a
    // processor without flush-to-zero crawls on those.
    void settle() {
        for (float& v : y_)
            if (std::fabs(v) < 1e-20f) v = 0.f;
    }
    // 2 * frames in, `frames` out.
    void down(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = 0.5f * (stage(1, stage(0, in[2 * i + 1])) + stage(3, stage(2, in[2 * i])));
    }

private:
    float stage(int n, float in) {
        const float out = kHalfbandStage[n] * (in - y_[n]) + x_[n];
        x_[n] = in;
        y_[n] = out;
        return out;
    }
    float x_[4] = {}, y_[4] = {};
};

} // namespace fx
} // namespace apotheca
