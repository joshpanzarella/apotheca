#pragma once
// What DELAY's newer types share (DUB, REVERSE, STUTTER): TAPE's and PING's
// eight note lengths for TIME (so TYPE carries TIME among them to the bit),
// a crossfade's curve, and the 4 s rest rule's length.
//
// They keep the TICK (svf.hpp): what glides is worked out every 16 samples
// counted from the effect's start (asleep too) and slid to sample by sample,
// and everything that counts time (a window, a slice, how long it has been
// left alone) counts SAMPLES. So the sound is the same to the last bit
// however it is cut into blocks, even while a setting moves.

#include <cmath>
#include <cstdio>

#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

// TIME in thirds of a step (a step is a sixteenth), so that triplets are whole: TAPE's and PING's eight.
constexpr int kEchoTimes = 8;
constexpr int kEchoThirds[kEchoTimes] = {3, 4, 6, 8, 9, 12, 18, 24};
inline const char* echoTimeName(int choice) {
    static const char* const names[kEchoTimes] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/4 DOT", "1/2"};
    return names[choice < 0 ? 0 : (choice > kEchoTimes - 1 ? kEchoTimes - 1 : choice)];
}
inline int echoTimeChoice(float v) {
    const int index = static_cast<int>(clamp01(v) * kEchoTimes);
    return index > kEchoTimes - 1 ? kEchoTimes - 1 : index;
}
// The choice as samples, at a tempo: the step length to the nearest sample.
inline long echoTimeFrames(float v, float tempo, float rate) { return std::lround(kEchoThirds[echoTimeChoice(v)] * 5.0 / tempo * rate); }
// TIME in steps, to match another delay's TIME by (changing TYPE).
inline float echoTimeSteps(float v) { return static_cast<float>(kEchoThirds[echoTimeChoice(v)]) / 3.f; }
// The most samples any TIME is, at the slowest tempo the engine has (40 BPM): half a bar.
inline long echoLongest(float rate) { return static_cast<long>(8.0 * 15.0 / 40.0 * rate) + 1; }

// A crossfade's curve: 0 at 0, 1 at 1, flat at both ends (3x^2 - 2x^3), so a splice turns in without a corner.
inline float echoCurve(float x) {
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    return x * x * (3.f - 2.f * x);
}

constexpr float kEchoRestSeconds = 4.f; // as RING's and MOD's: what keeps time sleeps after this long of nothing

} // namespace fx
} // namespace apotheca
