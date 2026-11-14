#pragma once
// JX-3P: the two DCOs, SYNC, METAL, NOISE and SOURCE MIX (the plan's batch 2).
//
// The real DCO (Service Notes p.6): a master clock of about 6 MHz divided by a
// counter (an 8253) the CPU sets once a loop. The counter's rectangle is the
// SQUARE. A capacitor charged at a current set by the pitch, reset at the
// counter's edge, is the SAW, so the saw is the same height on every key. The
// PULSE is that saw over-charged: "add a bias to DCO CV to excessively
// increase charging current", so it reaches the top early and stays there.
// SYNC: "pulses from DCO-1 are applied ... in DCO-2 section as RESET pulses".
// METAL: DCO-2's counter is not synced, but its saw "is clocked on every
// negative edge of both DCO-1 and 2 rectangulars". NOISE "block[s] DCO-2
// master frequency": DCO-2's counter stops.
//
// So each DCO here keeps two things: the COUNTER's phase `p` (its edges keep
// time) and the WAVE's ramp `r` (the capacitor: what the saw, the pulse and the
// square are read from). They are one and the same but in METAL, where DCO-1's
// falling edges start DCO-2's ramp again and its counter runs on.
//
// No aliasing (the house's bar, the plan: under 15 kHz, 50 dB under the
// fundamental): every jump gets a BLEP and every bend a BLAMP, minimum-phase
// tables (blep_table.hpp, made by tools/jx3p_blep.py), placed at the exact
// time of the jump within the sample. Minimum-phase: all the correction comes
// after, so a jump starts on its own sample (the wave as a whole comes through
// 2.23 samples late at the bottom, kBlampCarry, which every slope carries).
//
// Rack-free and SDL-free; no allocation; nothing a silent voice keeps moves.
//
// THE GUESSES AND THE READINGS (one table; the plan's section 2):
//
//   | What                          | Here                                        | Source |
//   | ----------------------------- | ------------------------------------------- | ------ |
//   | SAW                           | the ramp, -1 to 1, reset at the counter's   | SN p.6 |
//   |                               | falling edge                                |        |
//   | SQUARE                        | low for the first half of the counter's     | SN p.6 (the counter's own); which half |
//   |                               | cycle, high for the second (its falling     | is low is SELF |
//   |                               | edge is where the saw resets)               |        |
//   | PULSE                         | the ramp kPulseGain (10) times as steep,    | SN p.6 (over-charged); the 10% is WP; |
//   |                               | held at the top: off its top 10% of the     | the clip at the top is the plan's |
//   |                               | cycle. Its own mean taken off (kPulseMean): | reading. Taking the mean off in place |
//   |                               | the DC block                                | of a filter: SELF |
//   | NOISE                         | white, -1 to 1 (the saw's loudness), a      | SELF   |
//   |                               | xorshift seeded at the key                  |        |
//   | METAL with DCO-2 on SQUARE    | the square is read from the reset ramp too  | SELF: not written anywhere |
//   |                               | (it starts low again at each reset)         |        |
//   | ENV DEPTH 10                  | 24 semitones (kDcoEnvSemitones), straight   | SELF   |
//   |                               | in the dial                                 |        |
//   | FINE                          | 0 to 10 is -50 to +50 cents, straight       | OM     |
//   | TUNE                          | in semitones (jx3p.hpp: kTuneStepped)       | the plug-out's manual |
//   | SOURCE MIX                    | a straight crossfade of the two             | OM, NAM |

#include <cmath>
#include <cstdint>

#include "blep_table.hpp"

namespace apotheca {
namespace jx3p {

enum DcoWave { WaveSaw, WavePulse, WaveSquare, WaveNoise };
enum DcoCross { CrossOff, CrossSync, CrossMetal };

constexpr double kPulseGain = 10.0;                 // the over-charge: the ramp is this many times as steep
constexpr double kPulseTop = 1.0 / kPulseGain;      // ...so it is at the top this far into the cycle
constexpr double kPulseMean = 1.0 - kPulseTop;      // a free pulse's mean (its ramp averages 0): taken off
constexpr double kDcoEnvSemitones = 24.0;           // SELF: ENV DEPTH 10
constexpr double kFineCents = 50.0;                 // FINE 0 and 10: -50 and +50 cents (OM)
constexpr int kBlepRing = 32;                       // a power of two over kBlepLength
static_assert(kBlepRing >= kBlepLength && (kBlepRing & (kBlepRing - 1)) == 0, "the ring holds a whole correction");

// A wave's value, and its slope (a phase of 1 a cycle), at a place `r` (0..1) on its ramp.
inline double waveValue(int wave, double r) {
    switch (wave) {
    case WaveSaw: return 2.0 * r - 1.0;
    case WavePulse: return (r < kPulseTop ? -1.0 + 2.0 * kPulseGain * r : 1.0) - kPulseMean;
    case WaveSquare: return r < 0.5 ? -1.0 : 1.0;
    default: return 0.0;
    }
}
inline double waveSlope(int wave, double r) {
    switch (wave) {
    case WaveSaw: return 2.0;
    case WavePulse: return r < kPulseTop ? 2.0 * kPulseGain : 0.0;
    default: return 0.0;
    }
}
// Where on its ramp a wave bends or jumps by itself (the pulse's top, the square's rising edge), or -1.
inline double waveCorner(int wave) { return wave == WavePulse ? kPulseTop : (wave == WaveSquare ? 0.5 : -1.0); }

// The corrections still to be heard: a ring of the next kBlepRing samples.
struct Bleps {
    float ring[kBlepRing] = {};
    int at = 0;

    void clear() {
        for (float& x : ring) x = 0.f;
        at = 0;
    }
    // A jump of `jump` and a bend of `bend` (a change of slope a sample), `ago` samples before this
    // sample's value is taken (0 <= ago <= 1).
    void add(double jump, double bend, double ago) {
        double x = ago * kBlepPhases;
        if (x > kBlepPhases - 1e-6) x = kBlepPhases - 1e-6; // (a jump at the very top of the sample: the table's last point is its own)
        if (x < 0.0) x = 0.0;
        const int i = static_cast<int>(x);
        const float part = static_cast<float>(x - i), j = static_cast<float>(jump), b = static_cast<float>(bend);
        for (int n = 0; n < kBlepLength; ++n) {
            const int k = i + n * kBlepPhases;
            const float step = kBlep[k] + part * (kBlep[k + 1] - kBlep[k]);
            const float ramp = kBlamp[k] + part * (kBlamp[k + 1] - kBlamp[k]);
            ring[(at + n) & (kBlepRing - 1)] += j * step + b * ramp;
        }
    }
    float take() {
        const float x = ring[at];
        ring[at] = 0.f;
        at = (at + 1) & (kBlepRing - 1);
        return x;
    }
};

// One DCO: its counter's phase, its wave's ramp, its step a sample, and the corrections it owes.
struct Dco {
    double p = 0.0;   // the counter, 0..1 a cycle; it wraps at its falling edge
    double r = 0.0;   // the wave's ramp (the capacitor), 0..1; the same as p but in METAL
    double dt = 0.0;  // the phase step a sample (never 0 while it sounds)
    int wave = WaveSaw;
    Bleps bleps;
    std::uint32_t resets = 0; // the wave's ramp started again (for the tests: METAL counts them)

    void start(double phase) {
        p = r = phase;
        bleps.clear();
    }
    // The ramp starts again here, `ago` samples before this sample's value: the jump and the bend
    // it makes are corrected.
    void restartRamp(double ago) {
        const double jump = waveValue(wave, 0.0) - waveValue(wave, r);
        const double bend = (waveSlope(wave, 0.0) - waveSlope(wave, r)) * dt;
        r = 0.0;
        ++resets;
        if (jump != 0.0 || bend != 0.0) bleps.add(jump, bend, ago);
    }
    // Another wave from this sample on (read on the tick): the jump between them is corrected too.
    void setWave(int to) {
        if (to == wave) return;
        if (wave != WaveNoise && to != WaveNoise) {
            const double jump = waveValue(to, r) - waveValue(wave, r), bend = (waveSlope(to, r) - waveSlope(wave, r)) * dt;
            bleps.add(jump, bend, 1.0);
        }
        wave = to;
    }

    // ONE SAMPLE: from the top of the sample (0) to where its value is taken (1). `resetAt`: a
    // time in it at which the wave's ramp starts again (DCO-1's falling edge, for METAL), and its
    // counter with it if `counterToo` (SYNC); < 0 for none. Returns when its own counter wrapped
    // (its falling edge) in this sample, or -1.
    double run(double resetAt = -1.0, bool counterToo = false) {
        double t = 0.0, wrapped = -1.0;
        const double corner = waveCorner(wave);
        for (int guard = 0; guard < 8; ++guard) {
            const double left = 1.0 - t;
            double next = 1.0;
            int what = 0; // 0 the end of the sample, 1 the counter's falling edge, 2 the wave's corner, 3 the reset
            if (p + dt * left >= 1.0) next = t + (1.0 - p) / dt, what = 1;
            if (corner > 0.0 && r < corner && r + dt * left >= corner) {
                // (Worked out, the time can come to a hair past the sample's end when the corner is
                // just on it: it is still this sample's, or the ramp would pass it uncorrected. It is
                // held to the end below.)
                const double at = t + (corner - r) / dt;
                if (at < next || what == 0) next = at, what = 2;
            }
            if (resetAt >= t && resetAt <= next) next = resetAt, what = 3;
            if (next > 1.0) next = 1.0;
            if (next < t) next = t;
            const double moved = dt * (next - t);
            p += moved;
            r += moved;
            t = next;
            if (what == 0) break;
            const double ago = 1.0 - t;
            if (what == 1) {
                p = 0.0;
                wrapped = t;
                restartRamp(ago); // its own falling edge starts the ramp again
            } else if (what == 2) {
                r = corner; // the square's rising edge (a jump of 2), or the pulse reaching its top (a bend)
                if (wave == WaveSquare) bleps.add(2.0, 0.0, ago);
                else bleps.add(0.0, -2.0 * kPulseGain * dt, ago);
            } else {
                resetAt = -1.0;
                if (counterToo) p = 0.0;
                restartRamp(ago);
            }
        }
        return wrapped;
    }
    // This sample's value: the wave where its ramp is, its corrections, and the lateness of its
    // slope (kBlampCarry: the part of a bend's correction that stays). EVERY slope carries, the
    // saw's too: a minimum-phase jump's correction comes through 2.23 samples late at the bottom, so
    // the corrected jumps are late, and the ramp between them must be as late, or the wave's mean
    // is off by the carry (a saw was 4.47 x dt over 0: 19% of its peak at key 96, and it stepped
    // with the pitch at the tick). `testNoOffset`.
    float take() {
        const double carry = kBlampCarry * dt * waveSlope(wave, r);
        return static_cast<float>(waveValue(wave, r) + carry) + bleps.take();
    }
};

// NOISE: white, -1..1. A xorshift: the same numbers from the same seed on every machine.
struct DcoNoise {
    std::uint32_t state = 0x9E3779B9u;
    void seed(std::uint32_t s) { state = s ? s : 0x9E3779B9u; }
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<std::int32_t>(state)) * (1.f / 2147483648.f);
    }
};

// BOTH DCOs OF A VOICE, the cross mod and the noise. `mix` is SOURCE MIX, 0 (DCO-1) to 1 (DCO-2).
struct DcoPair {
    Dco one, two;
    DcoNoise noise;
    int cross = CrossOff;

    float next(float mix) {
        const double edge = one.run();
        float b;
        if (two.wave == WaveNoise) {
            b = noise.next(); // DCO-2's clock is stopped: its counter does not move, and gives no edges
        } else {
            two.run(cross == CrossOff ? -1.0 : edge, cross == CrossSync);
            b = two.take();
        }
        const float a = one.take();
        return (1.f - mix) * a + mix * b;
    }
};

// DCO-2 TUNE's dial (0..100, 50 unison) in semitones: in steps of a semitone when `stepped`
// (round((dial - 5) x 2.4) on the 0..10 dial: 7.9 is a fifth, 10 an octave), else straight.
inline double tuneSemitones(double dial, bool stepped, double perDial) {
    const double s = (dial - 50.0) / 10.0 * perDial;
    return stepped ? (s < 0.0 ? -std::floor(-s + 0.5) : std::floor(s + 0.5)) : s;
}
// FINE's dial (0..100, 50 centre) in semitones.
inline double fineSemitones(double dial) { return (dial - 50.0) / 50.0 * kFineCents / 100.0; }

} // namespace jx3p
} // namespace apotheca
