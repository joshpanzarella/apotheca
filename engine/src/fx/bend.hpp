#pragma once
// What the newer DRIVE types share (FOLD, FUZZ): one side of an effect that
// bends the wave at twice the sample rate (halfband.hpp), brings it back on
// centre, and keeps the plain sound as late as the bent one, so the two can be
// mixed without cancelling up high (CLAUDE.md: a wet sound that comes out late
// is never mixed with the plain sound as it came in). FUZZ's side (BendSide)
// bends by the curve's AVERAGE between one point and the next; FOLD's
// (BendSide4) at four times the rate.
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

// Near enough that the curve's average between two points is the curve at their middle (BendSide::average). In
// the curve's own units (after FUZZ's push, up to about 300): the areas' difference, in double, over the distance
// is good to about 1e-9 there, and the middle's error (the distance squared over 12 at the curve's sharpest bend)
// to about 1e-9.
constexpr double kBendNear = 1e-4;

// FUZZ's side. Each sample at twice the rate is bent by the curve's AVERAGE between where the sound is now and
// where it was the sample before: (F(u) - F(was)) / (u - was), F the curve's area from 0 (its antiderivative).
// That is first-order antiderivative anti-aliasing (ADAA; 2026-11-25, docs/plans/effect-quality.md). A corner the
// curve turns at once (the flat top, the gate's step) is met as a slope as wide as the sound moved, so the
// overtones it makes past the top of the rate, which fold back down as notes never played, come out far quieter:
// an 887 Hz saw at FUZZ's usual settings, -27.8 dB of fold-back under 19 kHz bent plainly at twice the rate,
// -46.6 averaged (testFuzz has the rest; four times the rate was -39 in the scratch model). It costs the area and
// a division a sample at twice the rate, and half a sample there (a quarter at the plain rate) of lateness, which
// the plain sound is given too (plain()). Up high it is a little quieter: the average of a straight line is its
// middle, a cosine, 0.7 dB down at 12 kHz and 2 dB at 20 kHz.
// The curve is handed in as a `Curve`: `push` (what the sound is scaled by first), `level` (what comes out is
// scaled by after), `shape` (a number that says which curve it is: FUZZ's gate), `at(u)` the curve and
// `area(u)` its area from 0, in double (in float the difference of two areas is lost to rounding where the
// curve is flat far out, as the area there is as big as u). The area to 0 is 0 for every shape.
struct BendSide {
    Halfband up, down, lateUp, lateDown; // the plain sound has its own up and down
    float dcIn = 0.f, dcOut = 0.f;
    float halfIn = 0.f, halfOut = 0.f; // the plain sound's all-pass (halfLate)
    float was = 0.f;                    // the curve's last input (after the push), at twice the rate
    double wasArea = 0.0;               // the curve's area to `was`, for the shape `wasShape`
    float wasShape = 0.f;
    static constexpr float kHalfLate = 1.f / 3.f;
    void reset() {
        up.reset(), down.reset(), lateUp.reset(), lateDown.reset();
        dcIn = dcOut = halfIn = halfOut = 0.f;
        was = 0.f, wasArea = 0.0, wasShape = 0.f; // (the area to 0 is 0 for every shape)
    }
    // The stages are settled every SAMPLE (twice(), plain()), as BendSide4's are, and only the centring on the
    // tick. Settled on the tick (as they were before the average), what they hold falls a long way in between (by
    // 0.08 a sample in the first stage: 1e-20 to 3e-38 in a tick), and a stage whose input stops at once leaves a
    // difference far under 1e-20 to fall from: the plain sound's way down was handed a number too small for a
    // float to hold (holdsTheRules' TooSmall saw it).
    void settle() {
        if (std::fabs(dcOut) < kTiny) dcOut = 0.f;
        if (std::fabs(dcIn) < kTiny) dcIn = 0.f;
    }
    // (In FOLD and FUZZ the tone filters after it follow dcOut down, so they would keep it awake without
    // dcOut here: a mutant that leaves it out survives. Kept, so the side is right by itself; halfOut likewise,
    // which lateDown follows.) `was` is the push times up's last output and halfIn is lateUp's, so those two are
    // seen through up and lateUp; reset() puts them back to 0 with the rest.
    float loudest() const {
        const float halfbands = std::fmax(std::fmax(up.loudest(), down.loudest()), std::fmax(lateUp.loudest(), lateDown.loudest()));
        return std::fmax(std::fmax(halfbands, std::fabs(halfOut)), std::fmax(std::fabs(dcIn), std::fabs(dcOut)));
    }
    // One sample in (as heard), the curve's average on each of the two worked out at twice the rate, one back on
    // centre. `keep` is dcKeep(rate).
    template <class Curve>
    float twice(float x, const Curve& curve, float keep) {
        float two[2], y;
        up.up(&x, two, 1);
        two[0] = curve.level * average(curve, two[0] * curve.push);
        two[1] = curve.level * average(curve, two[1] * curve.push);
        down.down(two, &y, 1);
        up.settle(), down.settle(); // every sample: see settle() above
        dcOut = y - dcIn + keep * dcOut;
        dcIn = y;
        return dcOut;
    }
    // The plain sound, as late as twice() makes the bent one: up, half a sample late at twice the rate, down. Of
    // a straight line the average is the middle of the two points, half a sample late, but quieter up high (a
    // cosine: 0.7 dB down at 12 kHz, 2 dB at 20 kHz); the plain sound is kept as loud at every pitch by an
    // all-pass half a sample late instead (Thiran's, kHalfLate), as late as the average within a degree at
    // 12 kHz and 4.5 at 20 kHz. So at MIX 50% the two add in step, and the plain half is the sound as it came.
    float plain(float x) {
        float two[2], y;
        lateUp.up(&x, two, 1);
        two[0] = halfLate(two[0]);
        two[1] = halfLate(two[1]);
        lateDown.down(two, &y, 1);
        lateUp.settle(), lateDown.settle();
        if (std::fabs(halfOut) < kTiny) halfOut = 0.f;
        return y;
    }
    // The curve's average from `was` to `u`. A curve turned since the last sample (FUZZ's GATE gliding) has the
    // area to `was` taken again on the new curve: the two areas must be of one curve, or a difference over a
    // small distance is anything. The difference is taken in double and divided in float (it is the difference
    // that needs the room; a division in float is the quicker, and good to 1e-7 of the answer).
    // What is under kNothing at the middle is nothing: FUZZ's lower side squares what it is given, and 1e-20
    // squared is too small for a float to hold. At GATE 0 the clip itself did that before it was averaged
    // (holdsTheRules turns GATE to 50%, where the step hides it; testFuzz now looks at GATE 0). Far apart the
    // areas are in double, so `u` needs no such care going in.
    template <class Curve>
    float average(const Curve& curve, float u) {
        if (curve.shape != wasShape) wasArea = curve.area(was), wasShape = curve.shape;
        const double area = curve.area(u), apart = static_cast<double>(u) - static_cast<double>(was);
        const float y = std::fabs(apart) > kBendNear ? static_cast<float>(area - wasArea) / static_cast<float>(apart) : curve.at(something(0.5f * (u + was)));
        was = u, wasArea = area;
        return y;
    }
    static float something(float v) { return std::fabs(v) >= kNothing ? v : 0.f; }
    // An all-pass at twice the rate, half a sample late (as late as the average) at low notes: Halfband's stage
    // with Thiran's number for half a sample, (1 - 1/2) / (1 + 1/2).
    float halfLate(float in) {
        const float out = kHalfLate * (in - halfOut) + halfIn;
        halfIn = in, halfOut = out;
        return out;
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
    // The stages are settled every SAMPLE (twice(), plain()), not on the tick as BendSide's were before 2026-11-25
    // (it does the same now). A stage whose
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
