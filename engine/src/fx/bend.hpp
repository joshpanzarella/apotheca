#pragma once
// What the newer DRIVE types share (FOLD, FUZZ): one side of an effect that
// bends the wave at twice the sample rate (halfband.hpp), brings it back on
// centre, and keeps the plain sound as late as the bent one, so the two can be
// mixed without cancelling up high (CLAUDE.md: a wet sound that comes out late
// is never mixed with the plain sound as it came in).
//
// It is worked a SAMPLE at a time, so an effect built on it can keep the
// TICK (svf.hpp): what glides is worked out every 16 samples counted from its
// start, and the sound is the same to the bit however it is cut into blocks,
// even while a setting moves (SATURATE, which works in blocks, is only the same
// once its settings are at rest).
//
// Its memory is looked after the newer way: settle() on each tick makes what
// is under kTiny an exact zero, and loudest() says when it is all quiet enough
// to go to sleep.

#include <cmath>

#include "glide.hpp"
#include "halfband.hpp"
#include "svf.hpp"

namespace apotheca {
namespace fx {

constexpr float kBendDcHz = 5.f; // a lopsided bend leaves the wave off centre: this brings it back

struct BendSide {
    Halfband up, down, late;
    float dcIn = 0.f, dcOut = 0.f;
    void reset() {
        up.reset(), down.reset(), late.reset();
        dcIn = dcOut = 0.f;
    }
    void settle() {
        up.settle(), down.settle(), late.settle();
        if (std::fabs(dcOut) < kTiny) dcOut = 0.f;
        if (std::fabs(dcIn) < kTiny) dcIn = 0.f;
    }
    // (In FOLD and FUZZ the tone filters after it follow dcOut down, so they would keep it awake without
    // dcOut here: a mutant that leaves it out survives. Kept, so the side is right by itself.)
    float loudest() const {
        return std::fmax(std::fmax(up.loudest(), down.loudest()), std::fmax(late.loudest(), std::fmax(std::fabs(dcIn), std::fabs(dcOut))));
    }
    // One sample in (as heard), `bend` on each of the two worked out at twice the rate, one back on centre.
    // `keep` is dcKeep(rate).
    template <class Bend>
    float twice(float x, const Bend& bend, float keep) {
        float two[2], y;
        up.up(&x, two, 1);
        two[0] = bend(two[0]);
        two[1] = bend(two[1]);
        down.down(two, &y, 1);
        dcOut = y - dcIn + keep * dcOut;
        dcIn = y;
        return dcOut;
    }
    // The plain sound, as late as twice() makes the bent one.
    float plain(float x) {
        float y;
        late.late(&x, &y, 1);
        return y;
    }
};
// The same at FOUR times the rate (FOLD): twice the rate, and twice that again inside it. A folder pushed
// twelve times makes overtones far past what twice the rate has room for: at full DRIVE a 5 kHz note came out
// mostly as notes that were never played (12 dB under the rest, against 24 for SATURATE's FOLD at 2x). The
// plain sound goes the same way with nothing bent (up, the inner stage's late() at twice the rate, down), so
// it is exactly as late at every pitch.
struct BendSide4 {
    Halfband up, down, up2, down2, lateUp, lateDown, late2; // the plain sound has its own up and down
    float dcIn = 0.f, dcOut = 0.f;
    void reset() {
        up.reset(), down.reset(), up2.reset(), down2.reset(), lateUp.reset(), lateDown.reset(), late2.reset();
        dcIn = dcOut = 0.f;
    }
    // The stages are settled every SAMPLE (twice(), plain()), not on the tick as BendSide's are. A stage whose
    // number is small (0.08) shrinks what it holds 0.08 times a step: 16 steps from 1e-20 is 2.8e-38, a hair
    // over the smallest number a float holds, and the inner stages take 32 steps a tick. On the tick, the
    // plain sound's last stage was handed a number too small to hold (TooSmall saw it). Here only the
    // centring is settled.
    void settle() {
        if (std::fabs(dcOut) < kTiny) dcOut = 0.f;
        if (std::fabs(dcIn) < kTiny) dcIn = 0.f;
    }
    float loudest() const {
        const float a = std::fmax(std::fmax(up.loudest(), down.loudest()), std::fmax(up2.loudest(), down2.loudest()));
        const float b = std::fmax(std::fmax(std::fmax(lateUp.loudest(), lateDown.loudest()), late2.loudest()), std::fmax(std::fabs(dcIn), std::fabs(dcOut)));
        return std::fmax(a, b);
    }
    template <class Bend>
    float twice(float x, const Bend& bend, float keep) {
        float two[2], four[4], y;
        up.up(&x, two, 1);
        up2.up(two, four, 2);
        for (float& v : four) v = bend(v);
        down2.down(four, two, 2);
        down.down(two, &y, 1);
        up.settle(), up2.settle(), down2.settle(), down.settle(); // every sample: see settle() above
        dcOut = y - dcIn + keep * dcOut;
        dcIn = y;
        return dcOut;
    }
    float plain(float x) {
        float two[2], y;
        lateUp.up(&x, two, 1);
        late2.late(two, two, 2);
        lateDown.down(two, &y, 1);
        lateUp.settle(), late2.settle(), lateDown.settle();
        return y;
    }
};
inline float dcKeep(float rate) { return 1.f - 6.2831853f * kBendDcHz / rate; }

// A sine at half scale, 32 points: what a curve's level is evened out by.
struct BendSines {
    float v[32];
    BendSines() {
        for (int k = 0; k < 32; ++k) v[k] = std::sin(6.2831853f * (static_cast<float>(k) + 0.5f) / 32.f);
    }
};
inline const BendSines kBendSines;
constexpr float kBendReference = 0.5f; // the sine that comes out as loud as it went in

// What to scale `bend`'s output by so that the reference sine comes out as loud as it went in (once it is
// back on centre). Never more than kBendMostLevel (a curve that lets almost nothing of it through, FUZZ's
// GATE at its top, is not made up from nothing), and 1 if it comes out as nothing at all.
constexpr float kBendMostLevel = 4.f;
// `reference`: the sine's size (FUZZ evens out a smaller one: a fuzz brings everything to about one level, so
// the level it brings it to is where it is matched).
template <class Bend>
float bendLevel(const Bend& bend, float reference = kBendReference) {
    float sum = 0.f, squares = 0.f;
    for (float s : kBendSines.v) {
        const float y = bend(reference * s);
        sum += y;
        squares += y * y;
    }
    const float mean = sum / 32.f, power = squares / 32.f - mean * mean;
    if (!(power > 1e-8f)) return 1.f;
    const float level = reference * 0.70710678f / std::sqrt(power);
    return level < kBendMostLevel ? level : kBendMostLevel;
}

// A one-pole low-pass's number for a corner in hertz (held under 0.45 of the rate).
inline float onePole(float hz, float rate) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * rate ? 0.45f * rate : hz) / rate); }

} // namespace fx
} // namespace apotheca
