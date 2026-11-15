#pragma once
// JX-3P: the LFO and its delay (the plan's batch 4).
//
// The real one has one LFO for the synth, worked out in software: the CPU's
// loop "Refresh[es] the Envelope and LFO data" once every "approx. 10mS"
// (Service Notes p.6, step 2), and writes it into the DCO counters and the VCF's
// CV with everything else. So the LFO is a STEP every 10 ms, as the envelope
// is: here it moves once a tick of its own (env.hpp's kTickSeconds), counted
// from allNotesOff (or from the synth's making), and every voice reads it on
// the voice's own tick. Its rate therefore tops out well under 50 Hz, as on
// the real one: at RATE 10 a cycle is five steps.
//
// Shapes (NAM): SINE, SQUARE (+1 the first half of a cycle, -1 the second),
// RANDOM (a new value at the start of each cycle, held through it). RANDOM's
// value is worked out from the count of cycles since allNotesOff, never a
// generator walked: the same bits from fresh, however the sound is cut.
//
// It runs free: a key does not start its cycle again (the 2018 plug-out added
// "key trigger"; the 1983 machine has none). A key that goes down with no other
// key held starts the DELAY again: the LFO's depth goes from 0 to full,
// straight, over DELAY's time. Which keys start it again is not written down.
//
// Rack-free and SDL-free; no allocation; nothing too small to hold (its values
// are a sine, +-1, a held number and a fade, none of which decays).
//
// THE GUESSES (one table; nothing published gives a rate, a time or a depth):
//
//   | What                     | Here                                              | Source |
//   | ------------------------ | ------------------------------------------------- | ------ |
//   | One LFO, stepped         | one for the synth, a step a 10 ms tick            | SN p.6 |
//   | Shapes                   | SINE, SQUARE, RANDOM                              | NAM    |
//   | RATE 0 to 10             | 0.1 to 20 Hz, even in octaves (kLfoSlowHz,        | SELF   |
//   |                          | kLfoFastHz)                                       |        |
//   | DELAY 0 to 10            | 0 to 5 s (kLfoDelayLongest), as the dial squared: | SELF   |
//   |                          | DELAY 5 is 1.25 s; whole ticks                    |        |
//   | The delay's fade         | from 0 to full, straight in time                  | SELF   |
//   | What starts the delay    | a key down with no other key held (kDelayOnFirstKeyOnly) | SELF |
//   | LFO DEPTH 10 (the DCOs)  | +-12 semitones (kDcoLfoSemitones), as the dial    | SELF   |
//   |                          | squared: 2.0 is 0.48, 3.0 is 1.08 (a vibrato in   |        |
//   |                          | the low dial)                                     |        |
//   | LFO MOD 10 (the VCF)     | +-4 octaves (kVcfLfoOctaves), straight in the dial| SELF   |
//   | RANDOM's numbers         | a hash of the count of cycles (kLfoSeed)          | SELF   |
//   | Runs free, no key sync   | so                                                | the plug-out lists key trigger as new |

#include <cmath>
#include <cstdint>

#include "env.hpp"

namespace apotheca {
namespace jx3p {

constexpr double kLfoSlowHz = 0.1;          // SELF: RATE 0
constexpr double kLfoFastHz = 20.0;         // SELF: RATE 10
constexpr double kLfoDelayLongest = 5.0;    // SELF: DELAY 10, in seconds
constexpr double kDcoLfoSemitones = 12.0;   // SELF: LFO DEPTH 10 swings the DCOs this far each way
constexpr double kVcfLfoOctaves = 4.0;      // SELF: LFO MOD 10 swings the cutoff this far each way
constexpr bool kDelayOnFirstKeyOnly = true; // SELF: only a key with no other held starts the delay again;
                                            // false: every key does
constexpr std::uint32_t kLfoSeed = 0xA511E9B3u; // SELF: RANDOM's numbers

enum LfoWave { LfoSine, LfoSquare, LfoRandom };

// RATE's dial (0..100, fractions fine) to Hz.
inline double lfoRateHz(double dial) {
    return kLfoSlowHz * std::exp2(dialHeld(static_cast<float>(dial)) / 100.0 * std::log2(kLfoFastHz / kLfoSlowHz));
}
// DELAY's dial to its time in seconds, and to whole ticks (0: no delay).
inline double lfoDelaySeconds(double dial) {
    const double d = dialHeld(static_cast<float>(dial)) / 100.0;
    return kLfoDelayLongest * d * d;
}
inline int lfoDelayTicks(double dial) { return static_cast<int>(std::lround(lfoDelaySeconds(dial) / kTickSeconds)); }
// LFO DEPTH's dial to semitones each way at the LFO's top; LFO MOD's to octaves.
inline double dcoLfoSemitones(double dial) {
    const double d = dialHeld(static_cast<float>(dial)) / 100.0;
    return kDcoLfoSemitones * d * d;
}
inline double vcfLfoOctaves(double dial) { return kVcfLfoOctaves * dialHeld(static_cast<float>(dial)) / 100.0; }

// RANDOM's value for cycle `n`: -1..1.
inline float lfoRandomAt(std::uint32_t n) {
    std::uint32_t x = kLfoSeed ^ (n * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(static_cast<double>(x >> 8) / 8388608.0 - 1.0);
}

// THE LFO. `tick` once every tick; `out` is what the voices read between ticks.
struct Lfo {
    double phase = 0.0;       // 0..1 of a cycle
    std::uint32_t cycles = 0; // cycles begun since reset: RANDOM's value is this one's
    int sinceKey = 0;         // ticks since the delay was started again
    float out = 0.f;          // the shape at the phase, times the delay's fade: -1..1
    int toTick = 0;           // samples to the next tick (0: on the next sample)

    void reset() { *this = Lfo(); }
    // A key down with no other key held (kDelayOnFirstKeyOnly): the depth starts again from 0,
    // from this sample (a voice whose tick comes before the LFO's next reads no LFO).
    void startDelay(int delayTicks) {
        sinceKey = 0;
        if (delayTicks > 0) out = 0.f;
    }

    // One tick: the shape at the phase now, faded by the delay; then the phase moves on by
    // `step` cycles (the rate times the tick's length).
    void tick(int wave, double step, int delayTicks) {
        float shape;
        if (wave == LfoSquare) shape = phase < 0.5 ? 1.f : -1.f;
        else if (wave == LfoRandom) shape = lfoRandomAt(cycles);
        else shape = static_cast<float>(std::sin(6.283185307179586 * phase));
        const float fade = delayTicks <= 0 || sinceKey >= delayTicks ? 1.f : static_cast<float>(sinceKey) / static_cast<float>(delayTicks);
        out = shape * fade;
        if (sinceKey < (1 << 30)) ++sinceKey;
        phase += step;
        while (phase >= 1.0) phase -= 1.0, ++cycles;
    }
};

} // namespace jx3p
} // namespace apotheca
