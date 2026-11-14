#pragma once
// What the newer filters share (COMB, VOWEL, EQ): a state-variable filter, a
// setting that sets off gently (Gentle), numbers slid to sample by sample
// (Slide), and the TICK: settings that glide are worked out again every kTick samples, on
// samples counted from the effect's start, so the sound is the same to the
// last bit however the caller cuts it into blocks (CLAUDE.md, "a new effect's
// sound does not depend on how it is cut into blocks").
//
// The filter is the one the old FILTER uses (a "trapezoidal" state-variable
// filter: it keeps its shape when its corner moves while it sounds), but its
// state is looked after the newer way: at each tick a state under kTiny is
// made an exact zero, so nothing in it fades through the numbers too small to
// hold (TooSmall in tests/effects_test.cpp), and that is done on the tick's
// sample, never at the end of a call.

#include <cmath>

#include "glide.hpp"

namespace apotheca {
namespace fx {

constexpr int kTick = 16;       // samples between two workings-out of what glides
constexpr float kTiny = 1e-20f; // a filter state under this is nothing (it is 360 dB down)

// A corner and a damping turned into the filter's three numbers. `g` is
// tan(pi * hz / rate), `k` is 1 / Q.
struct SvfShape {
    float a1 = 1.f, a2 = 0.f, a3 = 0.f;
    void set(float g, float k) {
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};
// tan(pi * hz / rate), the corner held under 0.45 of the rate.
inline float svfG(float hz, float rate) {
    const float most = 0.45f * rate;
    return std::tan(3.14159265f * (hz > most ? most : hz) / rate);
}

// One filter's memory. step() takes a sample in and gives the band (v1) and
// the low (v2); the high is in - k * v1 - v2.
struct Svf {
    float ic1 = 0.f, ic2 = 0.f;
    void step(const SvfShape& s, float in, float& v1, float& v2) { step(s.a1, s.a2, s.a3, in, v1, v2); }
    void step(float a1, float a2, float a3, float in, float& v1, float& v2) {
        const float v3 = in - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
    }
    void settle() {
        if (std::fabs(ic1) < kTiny) ic1 = 0.f;
        if (std::fabs(ic2) < kTiny) ic2 = 0.f;
    }
    float loudest() const { return std::fmax(std::fabs(ic1), std::fabs(ic2)); }
    void reset() { ic1 = ic2 = 0.f; }
};

// NUMBERS WORKED OUT ONCE A TICK, SLID TO SAMPLE BY SAMPLE. What a tick works
// out (a filter's numbers, a gain) is where they will be at the tick's END:
// `to`, then aim(); each sample next() moves `now` a kTick-th of the way, and
// land() at the tick's end puts it exactly there. Stepped a tick at a time
// instead, a band turned from +12 to -12 dB clicked (0.28 on a sine of 2).
// At rest, `now` is `to` to the bit: an EQ at 0 dB stays the sound untouched.
template <int N>
struct Slide {
    float now[N] = {}, to[N] = {}, step[N] = {};
    bool moving = false;
    void aim() {
        for (int i = 0; i < N; ++i) step[i] = (to[i] - now[i]) * (1.f / kTick);
        moving = true;
    }
    void next() {
        if (moving)
            for (int i = 0; i < N; ++i) now[i] += step[i];
    }
    void land() {
        if (!moving) return;
        for (int i = 0; i < N; ++i) now[i] = to[i], step[i] = 0.f;
        moving = false;
    }
    void snap() {
        moving = true;
        land();
    }
};

// A SETTING THAT SETS OFF GENTLY: two Glides one after the other, the second
// following the first, moved once a tick. A plain Glide moves fastest the
// moment it is turned, and a gain turned from +12 to -12 dB that way made a
// corner 34 times a 100 Hz sine's (EQ, testEq); this way, 2.4. It lands
// exactly, as each Glide does. Turn it by writing `first.to`; read `now()`.
struct Gentle {
    Glide first, used;
    bool moving() const { return first.moving() || used.moving(); }
    // One tick's move, `k` from glideK(kTick, ...): `now()` is then where it is at the tick's end.
    void tick(float k) {
        first.begin(kTick, k), first.done();
        used.to = first.now;
        used.begin(kTick, k), used.done();
    }
    void snap() {
        first.snap();
        used.to = first.now;
        used.snap();
    }
    float now() const { return used.now; }
};

// A sample as the effect's own workings take it: sound under kNothing is none.
// (What is heard of the plain sound is the sample as it came: MIX 0 is the
// sound untouched, to the bit.)
inline float heardIn(float v) { return std::fabs(v) >= kNothing ? v : 0.f; }

} // namespace fx
} // namespace apotheca
