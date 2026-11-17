#pragma once
// TAP TEMPO (easywins batch 1, owner 2026-11-17): A tapped in time on a TEMPO row (the drum grid's,
// up from the top lane, and the SET page's) sets the tempo from the gaps between the taps.
//
// The rule, all here so the tests can hold it without a screen:
// - the tempo is 60000 over the mean gap of the last kGapsKept gaps (the last five taps), rounded to
//   a whole beat a minute and held to the program's range (kMinTempo..kMaxTempo);
// - a gap longer than kForgetMs is not a gap: that tap starts afresh (at 40 BPM, the slowest, a beat
//   is 1.5 s);
// - the first tap of a run sets nothing (there is no gap yet): tap() says 0.

#include <SDL.h>

#include <cmath>

#include "apotheca/engine.hpp"

namespace apotheca {

class TapTempo {
public:
    static constexpr Uint32 kForgetMs = 2000;
    static constexpr int kGapsKept = 4;

    // A tap at `ms` (a frame's clock): the tempo the taps say, or 0 while this is the first of a run.
    float tap(Uint32 ms) {
        if (taps_ > 0 && (ms < times_[(taps_ - 1) % kKept] || ms - times_[(taps_ - 1) % kKept] > kForgetMs)) taps_ = 0;
        times_[taps_ % kKept] = ms;
        ++taps_;
        if (taps_ < 2) return 0.f;
        const int gaps = taps_ - 1 < kGapsKept ? taps_ - 1 : kGapsKept;
        const Uint32 first = times_[(taps_ - 1 - gaps) % kKept], last = times_[(taps_ - 1) % kKept];
        const double mean = static_cast<double>(last - first) / gaps;
        if (mean <= 0.0) return kMaxTempo; // (two taps on one frame: as fast as there is)
        const float bpm = static_cast<float>(std::round(60000.0 / mean));
        return bpm < kMinTempo ? kMinTempo : (bpm > kMaxTempo ? kMaxTempo : bpm);
    }
    int taps() const { return taps_ < kKept ? taps_ : kKept; } // in the run now, up to five
    void forget() { taps_ = 0; }

private:
    static constexpr int kKept = kGapsKept + 1;
    Uint32 times_[kKept] = {};
    int taps_ = 0;
};

} // namespace apotheca
