#pragma once
// What the newer effects share (TAPE, HALL, CHORUS, COMP, SATURATE): a
// setting that slides to where it was turned instead of jumping, a delay line
// read between samples, and the rule for going to sleep.
//
// None of the five old effects use this file.

#include <cmath>

namespace apotheca {
namespace fx {

// A value that slides. Turn it by writing `to`. Once a block:
// `step = g.begin(frames, k)`; then, for each sample, either add `step` to a
// local copy or call `g.next()` (one or the other, not both); then
// `g.done()`. It moves a fraction `k` of what is left each block, and lands
// EXACTLY on `to`: a value that only ever crept closer would become a number
// too small to hold, which is slow.
// IT ALWAYS LANDS, whatever the block length. A fraction of what is left gets
// smaller for ever, and in short blocks (8 frames and under) it used to fall
// below the smallest amount a float can be moved by: the value then stood
// still a hair short of `to` and `moving()` stayed true for good (HALL and
// SATURATE do their costly work while it is). So it never moves slower than
// kLeast a sample: the last stretch is a straight line, and it arrives.
struct Glide {
    float now = 0.f, to = 0.f, end = 0.f, step = 0.f;
    static constexpr float kLeast = 1e-7f; // a sample, for a value near 1; in proportion for a larger one
    void snap() { now = end = to, step = 0.f; } // there at once: before the first sound, and on a reset
    bool moving() const { return now != to; }
    float begin(int frames, float k) {
        const float left = to - now, least = static_cast<float>(frames) * kLeast * (1.f + std::fabs(to));
        float move = k * left;
        if (std::fabs(move) < least) move = left < 0.f ? -least : least;
        end = std::fabs(move) >= std::fabs(left) ? to : now + move;
        if (end == now) end = to; // (cannot happen while kLeast is more than a float's grain; kept as the last word)
        return step = (end - now) / static_cast<float>(frames);
    }
    float next() { return now += step; }
    void done() { now = end, step = 0.f; }
};
// The fraction a Glide moves in a block of `frames`, to be most of the way in `seconds`.
inline float glideK(int frames, float seconds, float sampleRate) {
    return static_cast<float>(frames) / (seconds * sampleRate + static_cast<float>(frames));
}

// THE WAY IN. A sample that is not a number, is infinite, or is beyond
// kLoudest (160 dB over full scale: a fault upstream, not a sound) is taken as
// silence. Without this one such sample stays for good in anything that
// remembers (a delay line, a filter, a level reading): the effect is then
// silent or broken until it is taken out. Every new effect calls it first
// thing in process(); the engine does the same to what a bus is sent. The old
// five do not (their arithmetic is not touched). One compare a sample.
constexpr float kLoudest = 1e8f;
inline void soundOnly(float* samples, int frames) {
    for (int i = 0; i < frames; ++i)
        if (!(std::fabs(samples[i]) <= kLoudest)) samples[i] = 0.f; // (written so that not-a-number fails it)
}
inline void soundOnly(float* left, float* right, int frames) {
    soundOnly(left, frames);
    soundOnly(right, frames);
}

// READING BETWEEN SAMPLES. Four neighbours, weighted (a cubic through them:
// Lagrange's). `f` is how far past the whole sample, 0 to 1. At f = 0 the
// answer is the sample itself, to the bit. It never makes anything louder at
// any frequency, so it is safe inside a feedback loop.
inline void cubicWeights(float f, float* w) {
    const float fm = f - 1.f, fn = f - 2.f, fp = f + 1.f;
    w[0] = -f * fm * fn * (1.f / 6.f);
    w[1] = fp * fm * fn * 0.5f;
    w[2] = -fp * f * fn * 0.5f;
    w[3] = fp * f * fm * (1.f / 6.f);
}
// The sound `whole + f` samples ago in a ring of `size`, where `at` is the
// place the next sample will be written. `whole` at least 2, at most size - 3.
inline float tapRead(const float* line, int size, int at, int whole, const float* w) {
    int i = at - whole + 1; // the newest of the four
    if (i < 0) i += size;
    if (i >= 3) return w[0] * line[i] + w[1] * line[i - 1] + w[2] * line[i - 2] + w[3] * line[i - 3];
    const int b = i >= 1 ? i - 1 : i - 1 + size, c = i >= 2 ? i - 2 : i - 2 + size, d = i - 3 + size;
    return w[0] * line[i] + w[1] * line[b] + w[2] * line[c] + w[3] * line[d];
}

// GOING TO SLEEP. An effect with a tail keeps the loudest thing it stored in
// each block. Once that has been under kQuiet for as long as its longest
// memory, with nothing coming in, it is asleep: it does no work until sound
// arrives. That is "silence in, silence out" to the exact zero, and it means
// nothing inside ever fades through the numbers too small to hold: the tail
// stops 240 dB down instead. It does not depend on the processor's
// flush-to-zero switch, and the sound is the same with that on or off.
constexpr float kQuiet = 1e-12f;   // a tail below this is over (240 dB under full)
constexpr float kNothing = 1e-15f; // sound coming in below this is not sound

} // namespace fx
} // namespace apotheca
