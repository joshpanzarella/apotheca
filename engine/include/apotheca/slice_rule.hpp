#pragma once
// SLICES AT THE HITS (sampling-gaps.md #2, batch S2): where a recording's hits begin, and a drum
// lane's own cuts. One place for both, so the drum grid (which finds, shows and moves them), the
// song file and the tests ask the same thing. The engine keeps only the numbers
// (DrumMachine::setLaneCuts). No SDL, no files, nothing of the engine: the tests run it on loops
// built by hand.
//
// A lane's slices are EVEN (16, or one a step of a FIT longer than a bar: Session::laneSlices)
// until it is cut at its hits, or a cut is moved, added or deleted by hand. Then they are its own
// CUTS: where each slice begins, as a part of what the lane plays (0..1, the unit a hit's start
// is in), rising, the first always 0 (the top of the sound). A slice runs to the next cut, the
// last to the end of the sound.
//
// THE HIT FINDER. A hit is a sudden rise in loudness. The sound is looked at every kHop samples (1.3 ms
// at 48 kHz) in three bands: all of it and the lows (under 200 Hz: a kick), each over the last
// kLongWindow (21 ms), and the highs (the change of the change from one sample to the next, which
// leaves only what is over a few kHz: a hat, a snare's crack), over the last kWindow (5.3 ms). A band's
// RISE is how many dB louder this window is than the loudest of the windows just before it (a low
// wave is longer than a short window, whose level swings with where in the wave it falls: the loudest
// of a few does not). A rise in the lows counts half: a narrow band swings most with any noise in it.
// A hit is where the biggest of the three peaks. A soft hit after a loud one still rises above the
// loud one's tail, most of all in the band the tail is not in: a hat over a kick's tail rises in the
// highs, a kick under a ringing hat in the lows. SENSITIVITY (1..10) is how big a rise counts as a
// hit (senseRise: 24 dB at 1, only hits out of near silence; 3 dB at 10). Two hits closer than
// kGapSeconds are one, the stronger. Where the hit begins is the first few samples standing 3 dB clear of
// the window before (so a hit over a loud tail is found as it starts), and the cut goes kLeadSeconds
// before it: the engine fades a slice in over a millisecond, and the fade then lies before the hit, not
// on its attack.
//
// It allocates: the app thread or a helper, never the audio thread. The candidates (findHits) are
// worked out once for a sound; a SENSITIVITY is then a quick pick from them (cutsAt).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace apotheca {

constexpr int kMaxLaneCuts = 64; // a lane's own slices at most (the slice view draws and numbers 64)
constexpr int kSenseMost = 10;   // SENSITIVITY 1..10 (0: EVEN)

// A drum lane's own cuts. Kept in TrackFields (a block of bytes, compared and hashed whole): every
// place past `count` is 0, so two alike are alike byte for byte (cutsHeld).
struct LaneCuts {
    std::uint8_t count = 0; // 0: EVEN; else this many slices, the first from at[0] = 0
    std::uint8_t sense = 0; // the SENSITIVITY they were found at, 1..10; 0: not found (EVEN, or made by hand)
    std::uint8_t spare[2] = {}; // (no padding: the bytes are compared and hashed whole)
    float at[kMaxLaneCuts] = {};
    bool even() const { return count == 0; }
    bool operator==(const LaneCuts& o) const {
        if (count != o.count || sense != o.sense) return false;
        for (int k = 0; k < count; ++k)
            if (at[k] != o.at[k]) return false;
        return true;
    }
    bool operator!=(const LaneCuts& o) const { return !(*this == o); }
};
static_assert(sizeof(LaneCuts) == 4 + 4 * kMaxLaneCuts, "a lane's cuts have no padding");

namespace slices {

constexpr int kHop = 64, kWindow = 256;   // the finder's look, and what each look takes in (samples)
constexpr int kFine = 16, kFineStep = 4;  // where a hit begins is looked for in this many samples at a time, this far apart
constexpr int kLongWindow = 1024;         // ...all of it and the lows' (21 ms: short windows swing with a low wave, and with noise in a narrow band)
constexpr double kLowHz = 200.0;          // the lows: under this (one pole)
constexpr float kLowsShare = 0.5f;        // a rise in the lows counts half (they swing most in noise)
constexpr double kGapSeconds = 0.03;      // two hits closer than this are one
constexpr double kLeadSeconds = 0.001;    // a cut this long before its hit's first loud sample
constexpr double kHandGapSeconds = 0.005; // a cut moved or added by hand stays this far from its neighbours
constexpr double kFloorDb = -60.0;        // under the sound's loudest look: silence
constexpr float kLeastRise = 1.5f;        // a candidate rises at least this much (dB): the finder's own floor
constexpr float kHair = 2e-6f;            // a start this close to a cut is on it (two samples of a 20 s sound)

// How big a rise counts as a hit at each SENSITIVITY (dB): about a fifth less a step, 24 down to 3.
inline float senseRise(int sense) {
    static const float rise[kSenseMost] = {24.f, 19.f, 15.f, 12.f, 9.5f, 7.5f, 6.f, 4.75f, 3.75f, 3.f};
    return rise[sense < 1 ? 0 : (sense > kSenseMost ? kSenseMost - 1 : sense - 1)];
}

// A candidate: where its cut goes (a sample) and how much it rose (dB, in the band it rose most in).
struct Found {
    int at = 0;
    float rise = 0.f;
};

// Every candidate in a sound, in order. The work: about fifteen sums a sample.
inline std::vector<Found> findHits(const float* x, int n, float sampleRate) {
    std::vector<Found> found;
    constexpr int kBands = 3; // 0 all of it, 1 the highs, 2 the lows
    constexpr int across = kWindow / kHop, longAcross = kLongWindow / kHop;
    const auto spanOf = [](int b) { return b == 1 ? across : longAcross; }; // each band's window, in looks
    const int hops = n > 0 ? n / kHop : 0;
    if (!x || hops < 2 * longAcross || !(sampleRate > 0.f)) return found;
    // The lows: one pole at kLowHz. The highs: the change of the change from one sample to the next (12 dB
    // an octave, so a kick's tail is under the floor there and does not swing). Both kept whole, to find
    // where a hit in them begins.
    std::vector<float> lows(static_cast<std::size_t>(n)), highs(static_cast<std::size_t>(n));
    const float keep = static_cast<float>(std::exp(-2.0 * 3.14159265358979 * kLowHz / sampleRate));
    float low = 0.f, one = 0.f, two = 0.f; // (the samples before the first are silence)
    for (int i = 0; i < n; ++i) {
        const float v = x[i];
        lows[static_cast<std::size_t>(i)] = low = v + keep * (low - v);
        highs[static_cast<std::size_t>(i)] = v - 2.f * one + two;
        two = one;
        one = v;
    }
    const auto signal = [&](int b, int i) -> double {
        if (i < 0 || i >= n) return 0.0;
        return b == 0 ? static_cast<double>(x[i]) : static_cast<double>(b == 1 ? highs[static_cast<std::size_t>(i)] : lows[static_cast<std::size_t>(i)]);
    };
    // Each band's energy in each look (a look's sum in floats, the looks summed in doubles), then each
    // window's (the band's last few looks), as loud as a short window's whatever its length.
    std::vector<double> energy[kBands];
    for (auto& e : energy) e.assign(static_cast<std::size_t>(hops), 0.0);
    for (int h = 0; h < hops; ++h) {
        const float* all = x + h * kHop;
        const float* hi = highs.data() + h * kHop;
        const float* lo = lows.data() + h * kHop;
        float a = 0.f, b = 0.f, c = 0.f;
        for (int i = 0; i < kHop; ++i) a += all[i] * all[i], b += hi[i] * hi[i], c += lo[i] * lo[i];
        energy[0][static_cast<std::size_t>(h)] = a;
        energy[1][static_cast<std::size_t>(h)] = b;
        energy[2][static_cast<std::size_t>(h)] = c;
    }
    std::vector<double> window[kBands];
    double loudest = 0.0;
    for (int b = 0; b < kBands; ++b) {
        window[b].assign(static_cast<std::size_t>(hops), 0.0);
        for (int h = 0; h < hops; ++h) {
            double sum = 0.0; // (added afresh, not run: a running sum's rounding would leave a floor of its own)
            for (int k = h - spanOf(b) + 1; k <= h; ++k)
                if (k >= 0) sum += energy[b][static_cast<std::size_t>(k)];
            window[b][static_cast<std::size_t>(h)] = sum * across / spanOf(b);
            loudest = std::max(loudest, window[b][static_cast<std::size_t>(h)]);
        }
    }
    if (!(loudest > 0.0)) return found; // silence
    // In dB, over one floor for all three: the loudest window of any band, kFloorDb down. A band with
    // nothing in it sits on the floor and never rises.
    const double floor = loudest * std::pow(10.0, kFloorDb / 10.0);
    std::vector<float> level[kBands];
    for (int b = 0; b < kBands; ++b) {
        level[b].assign(static_cast<std::size_t>(hops), 0.f);
        for (int h = 0; h < hops; ++h) level[b][static_cast<std::size_t>(h)] = static_cast<float>(10.0 * std::log10(window[b][static_cast<std::size_t>(h)] + floor));
    }
    // The rise at each look, in each band: this window over the loudest of the windows that end in the
    // short window's length before it begins (a low wave is longer than a window, and a window's level
    // swings with where in the wave it falls; the loudest of several does not). The lows' counts
    // kLowsShare of it. The biggest of the three, and its band.
    std::vector<float> rise(static_cast<std::size_t>(hops), 0.f);
    std::vector<std::uint8_t> band(static_cast<std::size_t>(hops), 0);
    for (int h = longAcross + across; h < hops; ++h)
        for (int b = 0; b < kBands; ++b) {
            float was = level[b][static_cast<std::size_t>(h - spanOf(b))];
            for (int k = 1; k <= across; ++k) was = std::max(was, level[b][static_cast<std::size_t>(h - spanOf(b) - k)]);
            const float r = (level[b][static_cast<std::size_t>(h)] - was) * (b == 2 ? kLowsShare : 1.f);
            if (r > rise[static_cast<std::size_t>(h)]) rise[static_cast<std::size_t>(h)] = r, band[static_cast<std::size_t>(h)] = static_cast<std::uint8_t>(b);
        }
    const int lead = static_cast<int>(std::lround(kLeadSeconds * sampleRate));
    const int lag = static_cast<int>(std::lround(sampleRate / (2.0 * 3.14159265358979 * kLowHz))); // the lows' pole, behind the sound
    for (int h = longAcross + across; h < hops; ++h) {
        const float r = rise[static_cast<std::size_t>(h)];
        if (r < kLeastRise) continue;
        // A peak: no bigger within a short window either side (the first of a flat top).
        bool peak = true;
        for (int k = 1; k <= across && peak; ++k) {
            if (rise[static_cast<std::size_t>(h - k)] >= r) peak = false;
            if (h + k < hops && rise[static_cast<std::size_t>(h + k)] > r) peak = false;
        }
        if (!peak) continue;
        // Where the hit begins: the first few samples of its window (kFine, looked at every kFineStep) that stand
        // clear of the window before it (twice its loudness, 3 dB: a hit over a loud tail is found as it starts,
        // not once it is four times the tail), and of a 36th of the hit's own loudest (a sixth of its peak: not
        // what is in the silence before), read in the band it rose most in: the lows less their pole's lag. It
        // began within those few samples: the middle of them is taken.
        const int b = band[static_cast<std::size_t>(h)];
        const int span = spanOf(b) * kHop, end = (h + 1) * kHop;
        const auto power = [&](int from, int count) {
            double sum = 0.0;
            for (int i = from; i < from + count; ++i) sum += signal(b, i) * signal(b, i);
            return sum / count;
        };
        const double before = power(end - 2 * span, span);
        double top = 0.0;
        for (int i = end - span; i < end + kWindow && i + kFine <= n; i += kFineStep) top = std::max(top, power(i, kFine));
        const double clear = std::max(2.0 * before, top / 36.0);
        int first = end - span;
        for (int i = std::max(0, end - span - kHop); i + kFine <= end + kFine && i + kFine <= n; i += kFineStep)
            if (power(i, kFine) > clear) {
                first = i + kFine / 2;
                break;
            }
        if (b == 2) first -= lag;
        Found f;
        f.at = std::max(0, first - lead);
        f.rise = r;
        found.push_back(f);
    }
    return found;
}

// The cuts at a SENSITIVITY (1..10) from a sound's candidates (`n`: its length in samples). The first
// is the top; a candidate within kGapSeconds of the top is the top's. Of candidates closer than that
// the stronger is kept (the earlier on a tie). At most kMaxLaneCuts, the strongest.
inline LaneCuts cutsAt(const std::vector<Found>& found, int n, int sense, float sampleRate) {
    LaneCuts cuts;
    cuts.count = 1;
    cuts.sense = static_cast<std::uint8_t>(sense < 1 ? 1 : (sense > kSenseMost ? kSenseMost : sense));
    if (n <= 0) return cuts;
    const float least = senseRise(cuts.sense);
    const int gap = static_cast<int>(std::lround(kGapSeconds * sampleRate));
    std::vector<Found> strong;
    for (const Found& f : found)
        if (f.rise >= least && f.at >= gap && f.at < n) strong.push_back(f);
    std::stable_sort(strong.begin(), strong.end(), [](const Found& a, const Found& b) { return a.rise > b.rise; });
    std::vector<int> kept;
    for (const Found& f : strong) {
        if (static_cast<int>(kept.size()) >= kMaxLaneCuts - 1) break;
        bool clear = true;
        for (int k : kept) clear = clear && std::abs(k - f.at) >= gap;
        if (clear) kept.push_back(f.at);
    }
    std::sort(kept.begin(), kept.end());
    for (int k : kept) cuts.at[cuts.count++] = static_cast<float>(static_cast<double>(k) / static_cast<double>(n));
    return cuts;
}

// The strongest candidate (any SENSITIVITY) between two places (parts of a sound `n` long), each more
// than kHandGapSeconds inside: where a cut added by hand goes. -1: none there.
inline float strongestBetween(const std::vector<Found>& found, int n, float from, float to, float sampleRate) {
    if (n <= 0) return -1.f;
    const double room = kHandGapSeconds * sampleRate / n;
    float best = -1.f, rise = 0.f;
    for (const Found& f : found) {
        const double at = static_cast<double>(f.at) / n;
        if (at > from + room && at < to - room && f.rise > rise) best = static_cast<float>(at), rise = f.rise;
    }
    return best;
}

// ---- a lane's slices, EVEN or its own -----------------------------------------------------------
// `even`: how many EVEN slices the lane has (Session::laneSlices without cuts).

inline int count(const LaneCuts& c, int even) { return c.count ? c.count : (even < 1 ? 1 : even); }

// Where slice k begins, round again past the last (k from 0).
inline float cutStart(const LaneCuts& c, int even, int k) {
    const int n = count(c, even);
    k = (k % n + n) % n;
    return c.count ? c.at[k] : static_cast<float>(k) / static_cast<float>(n);
}

// The slice a start falls in, from 0: the last cut at or before it (a hair's room given).
inline int sliceAt(const LaneCuts& c, int even, float start) {
    const int n = count(c, even);
    if (!c.count) {
        const int slice = static_cast<int>(start * static_cast<float>(n)); // exact: 16, 32 or 64 of them
        return slice < 0 ? 0 : (slice >= n ? n - 1 : slice);
    }
    int k = 0;
    while (k + 1 < n && c.at[k + 1] <= start + kHair) ++k;
    return k;
}

// Is a start on a cut (not slid off it)?
inline bool onCut(const LaneCuts& c, int even, float start) { return std::fabs(start - cutStart(c, even, sliceAt(c, even, start))) <= kHair; }

// How much of the sound ONE SLICE plays from `start`: an EVEN slice's share (from wherever it starts, as
// the engine has always played it), or to the next cut (the last to the end).
inline float partAt(const LaneCuts& c, int even, float start) {
    const int n = count(c, even);
    if (!c.count) return 1.f / static_cast<float>(n);
    const int k = sliceAt(c, even, start);
    const float end = k + 1 < n ? c.at[k + 1] : 1.f;
    return end - start > 0.f ? end - start : 0.f;
}

// The next cut up (by > 0) or down from a start, round the ends. From between two cuts, up is the
// next and down the one just passed.
inline float stepCut(const LaneCuts& c, int even, float start, int by) {
    const int n = count(c, even);
    if (!c.count) {
        const float now = start * static_cast<float>(n);
        int to = static_cast<int>(by > 0 ? std::floor(now + 1.001f) : std::ceil(now - 1.001f));
        to = (to % n + n) % n;
        return static_cast<float>(to) / static_cast<float>(n);
    }
    if (by > 0) {
        for (int k = 0; k < n; ++k)
            if (c.at[k] > start + kHair) return c.at[k];
        return c.at[0];
    }
    for (int k = n - 1; k >= 0; --k)
        if (c.at[k] < start - kHair) return c.at[k];
    return c.at[n - 1];
}

// The EVEN slices as cuts of the lane's own (to be moved, added to or deleted from by hand).
inline LaneCuts ownCuts(const LaneCuts& c, int even) {
    if (c.count) return c;
    LaneCuts own;
    const int n = std::min(count(c, even), kMaxLaneCuts);
    own.count = static_cast<std::uint8_t>(n);
    for (int k = 0; k < n; ++k) own.at[k] = static_cast<float>(k) / static_cast<float>(n);
    return own;
}

// Cuts from outside (a song file, the engine's command): rising, inside 0..1, the first 0, a number past
// what fits or out of order ends them there; every place past `count` 0. A SENSITIVITY nobody knows: none.
inline LaneCuts cutsHeld(const LaneCuts& in) {
    LaneCuts out;
    out.sense = in.sense <= kSenseMost ? in.sense : 0;
    if (in.count == 0) {
        out.sense = 0;
        return out;
    }
    out.count = 1;
    const int n = in.count < kMaxLaneCuts ? in.count : kMaxLaneCuts;
    for (int k = 1; k < n; ++k) {
        const float v = in.at[k];
        if (!(v > out.at[out.count - 1]) || !(v < 1.f)) break; // (NaN too)
        out.at[out.count++] = v;
    }
    return out;
}

} // namespace slices
} // namespace apotheca
