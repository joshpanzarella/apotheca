#pragma once
// JX-3P: the chorus (the plan's batch 5).
//
// The real one (Service Notes p.7, parts p.3): two MN3009 bucket brigades
// (BBDs), one a side, clocked by one VCO that "sweeps its frequency between
// 40k-200kHz"; the other side's sweep is 180 degrees out of phase; a 10 kHz
// low-pass before the BBDs and another after ("both have cutoff frequency at
// 10kHz"). It comes after the six voices are summed and after the HPF (the
// block diagram, p.4). One mode, on or off.
//
// HERE: one delay line read twice (the two BBDs hear the same sound, so one
// line and one filter before it do for both). An MN3009 has 256 stages, so
// its delay is 256 / (2 x clock): 3.2 ms at 40 kHz, 0.64 ms at 200 kHz. One
// triangle moves the clock of the left side from 40 to 200 kHz and back; the
// right side's clock is its mirror (40 + 200 less the left's), so the left is
// longest when the right is shortest. The triangle sweeps the CLOCK (what the
// VCO's control voltage moves), so the delay is 128 / clock: it lingers at the
// short end and hurries at the long one, as a swept BBD does
// (kSweepOnClock). A read between samples by a cubic (Hermite), as
// third_party/easycz/dsp/cz_chorus.c reads its line.
//
// Each side is the dry sound plus its own BBD. Off, the sound is the voices'
// own to the bit, mono (the same left and right). Switched, the wet fades in
// or out over 20 ms, counted in whole samples, so it lands exactly; once
// faded out, the chorus is emptied (a fresh one for its next switching on).
//
// THE SWEEP RUNS FROM THE SYNTH'S CLOCK, as the LFO does: its place is the
// samples since allNotesOff (or the synth's making), counted whole, so it is
// the same however the sound is cut into blocks, Play from the top is the same
// sweep, and a chorus that slept is in the same place as one that never did.
//
// IT GOES TO SLEEP: nothing under kChorusQuiet goes on the line (an exact
// zero does), and the filters snap to 0 under kChorusSnap. When the line has
// been fed zeros for as long as its longest read and every filter is at 0, it
// puts out exact zeros: the synth skips it, and it wakes as a fresh one.
//
// Rack-free and SDL-free; a fixed line, no allocation; nothing too small to
// hold. NOT COPIED: the BBDs' hiss (jx3p.hpp: kBbdHiss, the owner's call), and
// their clock's own leak and the loss of top through the bucket chain.
//
// THE GUESSES AND THE READINGS (one table; the plan's section 2, "Chorus"):
//
//   | What                         | Here                                          | Source |
//   | ---------------------------- | --------------------------------------------- | ------ |
//   | Two BBDs, one a side         | one line, read twice                          | SN p.3 (MN3009 x2, MN3101), p.7 |
//   | The BBD's stages             | 256 (kBbdStages)                              | the MN3009's own (Polynominal confirms the part) |
//   | The clock                    | 40 to 200 kHz (kBbdClockLowHz, kBbdClockHighHz)| SN p.7 |
//   | So the delay                 | 0.64 to 3.2 ms                                | from the two above |
//   | The other side               | its clock the mirror of this one's: 180°      | SN p.7 |
//   | The sweep's shape            | a triangle, of the clock (kSweepOnClock)      | SELF (the plan; the VCO's CV is what moves) |
//   | The sweep's rate             | 0.5 Hz (kChorusHz), over the whole range      | SELF: nothing published |
//   | Where the sweep starts       | the left's clock at 40 kHz (longest) at the   | SELF |
//   |                              | synth's sample 0                              |        |
//   | The filters                  | 10 kHz (kChorusFilterHz), each 3 dB down there| SN p.7 |
//   | ...their poles               | 2 each (a Butterworth): one before the line,  | SELF |
//   |                              | one after each side. So the wet is 6 dB down  |        |
//   |                              | at 10 kHz, 12 dB an octave above it, then 24  |        |
//   | Dry and wet                  | each side dry plus its own BBD, each at       | SELF (the real outputs are L, R and mono) |
//   |                              | kChorusDry, kChorusWet (0.71: a broad sound   |        |
//   |                              | about as loud on as off)                      |        |
//   | Switching                    | a straight fade of 20 ms (kChorusFadeSeconds) | SELF |
//   | The read between samples     | a cubic (Hermite)                             | SELF (the plan) |
//   | Quiet                        | kChorusQuiet 1e-12 (fx/glide.hpp's kQuiet)    | SELF |

#include <cmath>
#include <cstdint>
#include <cstring>

namespace apotheca {
namespace jx3p {

constexpr double kBbdStages = 256.0;          // the MN3009's
constexpr double kBbdClockLowHz = 40000.0;    // SN p.7
constexpr double kBbdClockHighHz = 200000.0;  // SN p.7
constexpr bool kSweepOnClock = true;          // SELF: the triangle moves the clock, the delay is 128 / clock;
                                              // false: the triangle moves the delay itself, straight
constexpr double kChorusHz = 0.5;             // SELF: the sweep's rate
constexpr double kChorusFilterHz = 10000.0;   // SN p.7: each filter's corner
constexpr double kChorusFadeSeconds = 0.02;   // SELF: switching on or off
constexpr float kChorusDry = 0.70710678f;     // SELF: the dry sound in each side when on
constexpr float kChorusWet = 0.70710678f;     // SELF: each side's BBD
constexpr float kChorusQuiet = 1e-12f;        // under this nothing goes on the line (an exact 0 does)
constexpr float kChorusSnap = 1e-20f;         // a filter's state this small is 0
constexpr int kChorusLine = 1024;             // samples: 3.2 ms and room up to about 300 kHz

// The BBD's delay in seconds at a clock in Hz.
inline double bbdDelaySeconds(double clockHz) { return kBbdStages / (2.0 * clockHz); }

// A 2-pole low-pass (a Butterworth, by the bilinear transform with its corner prewarped, so it is 3 dB
// down exactly there), transposed direct form 2. Its states snap to 0 under kChorusSnap.
struct LowPass2 {
    struct Coef {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        void set(double hz, double rate) {
            if (hz > 0.45 * rate) hz = 0.45 * rate;
            const double k = std::tan(3.14159265358979323846 * hz / rate), q = 0.70710678118654752;
            const double norm = 1.0 / (1.0 + k / q + k * k);
            b0 = static_cast<float>(k * k * norm);
            b1 = 2.f * b0;
            b2 = b0;
            a1 = static_cast<float>(2.0 * (k * k - 1.0) * norm);
            a2 = static_cast<float>((1.0 - k / q + k * k) * norm);
        }
    };
    float z1 = 0.f, z2 = 0.f;
    bool zero() const { return z1 == 0.f && z2 == 0.f; }
    float run(float x, const Coef& c) {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        if (std::fabs(z1) < kChorusSnap) z1 = 0.f;
        if (std::fabs(z2) < kChorusSnap) z2 = 0.f;
        return y;
    }
};

// THE CHORUS. `prepare` (setup), then `run` over the summed voices. `on` is the switch; `snap` puts the
// fade where the switch is at once (a fresh synth, allNotesOff).
struct BbdChorus {
    float line[kChorusLine];
    unsigned pos = 0;           // where the newest sample is
    LowPass2::Coef coef;
    LowPass2 before, afterL, afterR;
    int quietRun = 0;           // samples in a row that put an exact 0 on the line
    int reach = 160;            // the farthest a read goes back, and its cubic's taps: a line quiet this long is empty
    bool on = true;             // the switch
    int fade = 0, fadeLength = 960; // the wet's share is fade / fadeLength
    std::uint32_t period = 96000;   // the sweep's cycle in samples
    float delayK = 153.6f;      // the rate x 128: a clock in Hz to a delay in samples
    float span = 0.f;           // the clock's swing, in Hz
    float delayLow = 30.72f, delayHigh = 153.6f; // the delay's ends in samples (kSweepOnClock false)

    BbdChorus() { clear(); }

    void prepare(double rate) {
        coef.set(kChorusFilterHz, rate);
        fadeLength = static_cast<int>(std::lround(kChorusFadeSeconds * rate));
        if (fadeLength < 1) fadeLength = 1;
        const double cycle = std::floor(rate / kChorusHz + 0.5);
        period = cycle < 2.0 ? 2u : static_cast<std::uint32_t>(cycle);
        delayK = static_cast<float>(rate * kBbdStages / 2.0);
        span = static_cast<float>(kBbdClockHighHz - kBbdClockLowHz);
        delayLow = static_cast<float>(rate * bbdDelaySeconds(kBbdClockHighHz));
        delayHigh = static_cast<float>(rate * bbdDelaySeconds(kBbdClockLowHz));
        reach = static_cast<int>(std::ceil(delayHigh)) + 3;
        if (reach > kChorusLine - 1) reach = kChorusLine - 1;
        clear();
        snap();
    }
    // Emptied: a fresh chorus (the switch and the fade as they are).
    void clear() {
        std::memset(line, 0, sizeof line);
        pos = 0;
        before = afterL = afterR = LowPass2();
        quietRun = reach;
    }
    void snap() {
        fade = on ? fadeLength : 0;
        if (!on) clear();
    }
    // Anything to do: on, or still fading out.
    bool active() const { return on || fade > 0; }
    // Still on its way in or out.
    bool fading() const { return fade != (on ? fadeLength : 0); }
    // Nothing in it: with silence coming in it puts out exact zeros and its state stays as it is.
    bool idle() const { return quietRun >= reach && before.zero() && afterL.zero() && afterR.zero(); }

    // Where the sweep is, `at` samples into its cycle: each side's delay in samples.
    void delaysAt(std::uint32_t at, float& left, float& right) const {
        const std::uint32_t half = period / 2;
        const float tri = static_cast<float>(at < half ? at : period - at) / static_cast<float>(half); // 0..1..0
        if (kSweepOnClock) {
            const float clock = static_cast<float>(kBbdClockLowHz) + span * tri;
            left = delayK / clock;
            right = delayK / (static_cast<float>(kBbdClockLowHz + kBbdClockHighHz) - clock);
        } else {
            left = delayHigh + (delayLow - delayHigh) * tri;
            right = delayLow + delayHigh - left;
        }
    }
    // The line `d` samples behind the newest (a cubic, Hermite).
    float read(float d) const {
        const int whole = static_cast<int>(d);
        const float f = d - static_cast<float>(whole);
        const unsigned i = pos - static_cast<unsigned>(whole), mask = kChorusLine - 1;
        const float xm1 = line[(i + 1u) & mask], x0 = line[i & mask], x1 = line[(i - 1u) & mask], x2 = line[(i - 2u) & mask];
        const float a = 0.5f * (x1 - xm1);
        const float b = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        const float c = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c * f + b) * f + a) * f + x0;
    }

    // `frames` of the summed voices in `left` (the synth's samples from `clock` on): each side written
    // into `left` and `right`.
    void run(float* left, float* right, int frames, std::uint64_t clock) {
        if (!active()) { // off and faded out: the dry sound, mono, and nothing kept
            for (int i = 0; i < frames; ++i) right[i] = left[i];
            return;
        }
        std::uint32_t at = static_cast<std::uint32_t>(clock % period);
        const float toShare = 1.f / static_cast<float>(fadeLength);
        for (int i = 0; i < frames; ++i) {
            if (fading()) {
                fade += on ? 1 : -1;
                if (fade == 0) { // faded out: emptied, and the rest is the dry sound
                    clear();
                    for (int j = i; j < frames; ++j) right[j] = left[j];
                    return;
                }
            }
            const float x = left[i];
            const float y = before.run(x, coef);
            const float w = std::fabs(y) < kChorusQuiet ? 0.f : y;
            pos = (pos + 1u) & (kChorusLine - 1);
            line[pos] = w;
            quietRun = w != 0.f ? 0 : (quietRun < reach ? quietRun + 1 : reach);
            float dl, dr;
            delaysAt(at, dl, dr);
            const float wl = afterL.run(read(dl), coef), wr = afterR.run(read(dr), coef);
            const float share = static_cast<float>(fade) * toShare, wet = share * kChorusWet;
            const float dry = 1.f - share * (1.f - kChorusDry);
            left[i] = dry * x + wet * wl;
            right[i] = dry * x + wet * wr;
            if (++at == period) at = 0;
        }
    }
};

} // namespace jx3p
} // namespace apotheca
