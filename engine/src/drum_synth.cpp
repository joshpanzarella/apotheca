// DRUM SYNTH: the twelve recipes (drum_synth.hpp; docs/plans/drumsynth-plan.md). Every figure in a
// recipe is SELF: the plan's own, from the built-in kit's (drums.cpp) and the classic machines' as
// remembered, no service notes opened. NOBODY HAS HEARD ANY OF IT: tests/drumsynth_test.cpp holds
// what can be measured (pitch, decay, peak, the end, the bits).
//
// No multiply and add joined into one rounding (a compiler for the handheld's ARM does that by
// itself; the desk's does not): so the handheld makes the desk's bits.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "apotheca/drum_synth.hpp"

#include <algorithm>

namespace apotheca {
namespace drumsynth {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSquares = 6;
// The hats' six squares, as ratios of the lowest (a classic machine's 205.3, 304.4, 369.6, 522.7,
// 540 and 800 Hz: SELF, from memory).
constexpr double kMetal[kSquares] = {1.0, 1.4827, 1.8003, 2.5460, 2.6303, 3.8967};
constexpr double kCowbellUpper = 1.48; // the cowbell's upper square (540 and 800 Hz)

enum Filter { kLow, kBand, kHigh };

// The recipe: every part's numbers, worked out from the voice before the loop. A level of 0 is a
// part not run.
struct Recipe {
    // BODY: a sine whose pitch falls from `from` to `hz` (the fall exp(-t / tau)), with TONE 2, a
    // second sine at `ratio` of BODY's pitch.
    double bodyLevel = 0, hz = 100, from = 100, tau = 0.01, bodyDecay = 0.1;
    double toneLevel = 0, ratio = 1, toneDecay = 0.1;
    // SQUARES: up to six, through a filter; an envelope with a front (a faster decay on top).
    int squares = 0;
    double squareHz[kSquares] = {};
    double squareLevel = 0, squareDecay = 0.1, squareFront = 0, frontDecay = 0.005;
    Filter squareFilter = kHigh;
    double squareFc = 8000, squareQ = 0.7071;
    // NOISE: through a filter; an envelope that may rise, may come in bursts, and has a front.
    double noiseLevel = 0, noiseDecay = 0.1, noiseFront = 0;
    Filter noiseFilter = kHigh;
    double noiseFc = 8000, noiseQ = 0.7071, rise = 0;
    int bursts = 0;
    double gap = 0.01, burstDecay = 0.01, tailLevel = 1; // CLAP: bursts of 1 `gap` apart, then the tail at tailLevel
    // CLICK: noise through a one-pole low pass, a short decay.
    double clickLevel = 0, clickFc = 5000, clickDecay = 0.006;
    double drive = 0; // 0..1
};

double value(const Voice& v, int place) { return knobValue(v.model, place, v.dial[place]); }

Recipe recipeOf(const Voice& v, float sampleRate) {
    Recipe r;
    const double top = sampleRate * (1.0 / 3.0); // no sine or filter higher than this
    const double tune = value(v, kTune), decay = value(v, kDecay);
    const double tone = shown(v.model, kTone) ? value(v, kTone) : 0.0;
    const double snap = shown(v.model, kSnap) ? value(v, kSnap) : 0.0;
    const double noise = shown(v.model, kNoise) ? value(v, kNoise) : 0.0;
    const double sweep = shown(v.model, kSweep) ? value(v, kSweep) : 0.0;
    const double own = shown(v.model, kOwn) ? value(v, kOwn) : 0.0;
    r.drive = shown(v.model, kDrive) ? value(v, kDrive) : 0.0;
    r.hz = tune;
    r.from = std::min(top, tune * std::pow(2.0, sweep / 12.0));
    switch (v.model) {
    case kKick:
    case kTom:
        r.bodyLevel = 1.0;
        r.bodyDecay = decay;
        r.tau = own / 4.6; // BEND: the fall 99% over
        r.clickLevel = 2.5 * snap;
        r.clickFc = 1000.0 * std::pow(12.0, tone);
        r.clickDecay = 0.008;
        r.noiseLevel = 0.6 * noise; // a breath of low noise in the body
        r.noiseFilter = kLow;
        r.noiseFc = std::min(top, 4.0 * tune);
        r.noiseDecay = 0.5 * decay;
        break;
    case kSnare:
        r.bodyLevel = 1.0 - 0.7 * noise;
        r.bodyDecay = own; // BODY
        r.tau = 0.03 / 4.6;
        r.toneLevel = 0.5 * r.bodyLevel;
        r.ratio = 1.65;
        r.toneDecay = 0.6 * own;
        r.noiseLevel = 1.2 * noise; // the wires
        r.noiseFilter = kHigh;
        r.noiseFc = std::min(top, 500.0 * std::pow(16.0, tone));
        r.noiseDecay = decay;
        r.clickLevel = 1.5 * snap; // the crack: noise, nearly the whole band
        r.clickFc = 12000.0;
        r.clickDecay = 0.012;
        break;
    case kClap:
        r.noiseLevel = 1.0;
        r.noiseFilter = kBand;
        r.noiseFc = tune;
        r.noiseQ = 4.0 * std::pow(0.125, tone); // TONE: narrow (Q 4) to wide (Q 0.5)
        r.bursts = 4;
        r.gap = own; // SPREAD
        r.burstDecay = 1.3 * own;
        r.tailLevel = 1.0 - 0.8 * snap;
        r.noiseDecay = decay; // the tail's
        break;
    case kClHat:
    case kOpHat:
    case kCymbal:
        r.squares = kSquares;
        for (int i = 0; i < kSquares; ++i) r.squareHz[i] = tune * kMetal[i];
        r.squareLevel = 1.0 - noise;
        r.noiseLevel = noise;
        r.squareDecay = r.noiseDecay = decay;
        r.squareFront = r.noiseFront = (v.model == kCymbal ? 3.0 : 2.0) * snap;
        r.frontDecay = v.model == kCymbal ? 0.05 : 0.005;
        r.squareFilter = r.noiseFilter = kHigh;
        r.squareFc = r.noiseFc = std::min(top, v.model == kCymbal ? 1000.0 * std::pow(10.0, tone) : 3000.0 * std::pow(14000.0 / 3000.0, tone));
        break;
    case kRim:
        r.bodyLevel = 1.0;
        r.bodyDecay = decay;
        r.toneLevel = 1.4 * tone;
        r.ratio = 0.283;
        r.toneDecay = decay;
        r.clickLevel = 1.5 * snap;
        r.clickFc = 10000.0;
        r.clickDecay = 0.003;
        break;
    case kCowbell:
        r.squares = 2;
        r.squareHz[0] = tune;
        r.squareHz[1] = tune * kCowbellUpper;
        r.squareLevel = 1.0;
        r.squareDecay = decay;
        r.squareFront = 2.0 * snap;
        r.frontDecay = 0.015;
        r.squareFilter = kBand;
        r.squareFc = std::min(top, tune * std::pow(2.0, 1.0 + 2.0 * tone)); // 2 to 8 times TUNE
        r.squareQ = 1.5;
        break;
    case kClave:
        r.bodyLevel = 1.0;
        r.bodyDecay = decay;
        r.clickLevel = 2.5 * snap;
        r.clickFc = 8000.0;
        r.clickDecay = 0.004;
        break;
    case kZap:
        r.bodyLevel = 1.0;
        r.bodyDecay = decay;
        r.tau = own / 4.6;
        r.noiseLevel = 0.8 * noise;
        r.noiseFilter = kLow;
        r.noiseFc = std::min(top, 8000.0);
        r.noiseDecay = 0.5 * decay;
        break;
    case kNoiseBurst:
        r.noiseLevel = 1.0;
        r.noiseFilter = kBand;
        r.noiseFc = std::min(top, tune);
        r.noiseQ = 8.0 * std::pow(1.0 / 16.0, tone); // TONE: narrow (Q 8) to wide (Q 0.5)
        r.noiseDecay = decay;
        r.rise = 0.04 * (1.0 - snap); // SNAP: a 40 ms rise at 0, at once at 100%
        break;
    default: break;
    }
    return r;
}

// How long a part lasts to kQuietDb: its decay is to -60 dB.
double partSeconds(double decay) { return decay * (kQuietDb / -60.0); }

double recipeSeconds(const Recipe& r) {
    double s = 0;
    if (r.bodyLevel > 0) s = std::max(s, partSeconds(r.bodyDecay));
    if (r.toneLevel > 0) s = std::max(s, partSeconds(r.toneDecay));
    if (r.squareLevel > 0) s = std::max(s, partSeconds(std::max(r.squareDecay, r.squareFront > 0 ? r.frontDecay : 0.0)));
    if (r.noiseLevel > 0) {
        const double start = r.bursts > 0 ? (r.bursts - 1) * r.gap : 0.0; // the tail starts with the last burst
        s = std::max(s, r.rise + start + partSeconds(std::max(r.noiseDecay, r.noiseFront > 0 ? r.frontDecay : 0.0)));
    }
    if (r.clickLevel > 0) s = std::max(s, partSeconds(r.clickDecay));
    return std::min(kLongestSeconds, std::max(kShortestSeconds, s));
}

// What a filter passes of white noise is set back to white noise's level, so a part's level means
// the same narrow or wide: the square root of the half band over the filter's noise bandwidth (as
// an analog filter's: a low pass pi fc Q / 2, a band pass pi fc / 2Q, a high pass what the low pass
// leaves). Held to 40 (32 dB) for the narrowest.
double whiteAgain(Filter f, double fc, double q, float sampleRate) {
    const double half = sampleRate * 0.5, low = kPi * fc * q * 0.5;
    double band = f == kLow ? low : f == kBand ? kPi * fc / (2.0 * q) : half - low;
    band = std::min(half, std::max(band, half / 1600.0));
    return std::sqrt(half / band);
}

// A decay to -60 dB in `seconds`, as a factor a sample.
double fall(double seconds, float sampleRate) { return std::pow(10.0, -3.0 / (seconds * sampleRate)); }

// The state-variable filter (trapezoidal), its numbers fixed for the whole sound.
struct Svf {
    double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    Filter type = kHigh;
    void set(Filter f, double fc, double q, float sampleRate) {
        type = f;
        const double u = 0.5 * fc / sampleRate; // tan(pi fc / rate) from the voice's own sine: the library's tan
        const double g = sineTurn(u) / sineTurn(u + 0.25); // differs in its last bit on the handheld's

        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    double run(double x) {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return type == kLow ? v2 : type == kBand ? v1 : x - k * v1 - v2;
    }
};

// One envelope: a decay a sample, snapped to 0 under kTiny.
struct Fall {
    double now = 0, by = 1;
    void start(double level, double seconds, float sampleRate) {
        now = level;
        by = fall(seconds, sampleRate);
    }
    double next() {
        const double was = now;
        now *= by;
        if (now < kTiny) now = 0;
        return was;
    }
};

} // namespace

double sineTurn(double x) {
    // sin(2 pi x) = -sin(2 pi y), y = x - 1/2 in [-1/2, 1/2); folded into [-1/4, 1/4].
    double y = x - 0.5;
    if (y > 0.25) y = 0.5 - y;
    else if (y < -0.25) y = -0.5 - y;
    const double z = 2.0 * kPi * y, z2 = z * z;
    // Taylor to z^13: under 1e-10 out on [-pi/2, pi/2].
    const double s = z * (1.0 + z2 * (-1.0 / 6 + z2 * (1.0 / 120 + z2 * (-1.0 / 5040 + z2 * (1.0 / 362880 + z2 * (-1.0 / 39916800 + z2 * (1.0 / 6227020800.0)))))));
    return -s;
}

double soft(double x) {
    if (x >= 3.0) return 1.0;
    if (x <= -3.0) return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

std::size_t lengthOf(const Voice& v, float sampleRate) {
    if (!v.on() || !(sampleRate > 0.f)) return 0;
    return static_cast<std::size_t>(std::lround(recipeSeconds(recipeOf(voiceHeld(v), sampleRate)) * sampleRate));
}

double work(const Voice& v, float sampleRate) {
    if (!v.on()) return 0.0;
    const Recipe r = recipeOf(voiceHeld(v), sampleRate);
    double each = 2.0; // the envelopes, the sum, the drive
    if (r.bodyLevel > 0) each += 3.0;
    if (r.toneLevel > 0) each += 3.0;
    if (r.squareLevel > 0) each += r.squares + 2.0;
    if (r.noiseLevel > 0 || r.clickLevel > 0) each += 1.0; // the hash
    if (r.noiseLevel > 0) each += 2.0;
    if (r.clickLevel > 0) each += 1.0;
    return each * static_cast<double>(lengthOf(v, sampleRate));
}

std::vector<float> make(const Voice& in, float sampleRate) {
    std::vector<float> out;
    if (!in.on() || !(sampleRate > 0.f)) return out;
    const Voice v = voiceHeld(in);
    const Recipe r = recipeOf(v, sampleRate);
    const std::size_t n = lengthOf(v, sampleRate);
    out.resize(n);
    const std::uint32_t seed = seedOf(v);
    const double rate = sampleRate;

    // The parts' states.
    Fall body, tone2, square, squareFront, noiseFall, noiseFront, burst, click;
    body.start(r.bodyLevel, r.bodyDecay, sampleRate);
    tone2.start(r.toneLevel, r.toneDecay, sampleRate);
    square.start(r.squareLevel, r.squareDecay, sampleRate);
    squareFront.start(r.squareLevel * r.squareFront, r.frontDecay, sampleRate);
    noiseFront.start(r.noiseLevel * r.noiseFront, r.frontDecay, sampleRate);
    click.start(r.clickLevel, r.clickDecay, sampleRate);
    // CLAP: the bursts start at 0, gap, 2 gap...; the tail with the last.
    const long gapN = r.bursts > 0 ? std::max(1L, std::lround(r.gap * rate)) : 0;
    const long tailAt = r.bursts > 0 ? (r.bursts - 1) * gapN : 0;
    const long riseN = std::lround(r.rise * rate);
    noiseFall.start(r.bursts > 0 || riseN > 0 ? 0.0 : r.noiseLevel, r.noiseDecay, sampleRate); // (a rise: the decay starts at its top)
    burst.start(0.0, r.burstDecay, sampleRate);
    double sweepNow = 1.0;
    const double sweepBy = std::exp(-1.0 / (r.tau * rate));
    double phase = 0, phase2 = 0;
    std::uint32_t squarePhase[kSquares] = {}, squareStep[kSquares] = {};
    for (int i = 0; i < r.squares; ++i) squareStep[i] = static_cast<std::uint32_t>(std::lround(std::min(r.squareHz[i], rate * 0.49) / rate * 4294967296.0));
    Svf squareSvf, noiseSvf;
    squareSvf.set(r.squareFilter, r.squareFc, r.squareQ, sampleRate);
    noiseSvf.set(r.noiseFilter, r.noiseFc, r.noiseQ, sampleRate);
    const double clickC = 1.0 - std::exp(-2.0 * kPi * std::min(r.clickFc, rate / 3.0) / rate);
    double clickLow = 0;
    const double noiseGain = whiteAgain(r.noiseFilter, r.noiseFc, r.noiseQ, sampleRate);
    const double clickGain = std::sqrt(rate * 0.5 / std::min(rate * 0.5, kPi * std::min(r.clickFc, rate / 3.0) * 0.5));
    const bool bodyOn = r.bodyLevel > 0, toneOn = r.toneLevel > 0, squaresOn = r.squareLevel > 0, noiseOn = r.noiseLevel > 0, clickOn = r.clickLevel > 0;
    const bool hashOn = noiseOn || clickOn;

    // Pass 1: the parts summed, and the loudest.
    double loudest = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double x = 0;
        if (bodyOn || toneOn) {
            const double f = r.hz + (r.from - r.hz) * sweepNow;
            sweepNow *= sweepBy;
            if (sweepNow < kTiny) sweepNow = 0;
            if (bodyOn) x += sineTurn(phase) * body.next();
            phase += f / rate;
            if (phase >= 1.0) phase -= 1.0;
            if (toneOn) {
                x += sineTurn(phase2) * tone2.next();
                phase2 += f * r.ratio / rate;
                if (phase2 >= 1.0) phase2 -= 1.0;
            }
        }
        if (squaresOn) {
            int sum = 0;
            for (int s = 0; s < r.squares; ++s) {
                sum += (squarePhase[s] >> 31) ? -1 : 1;
                squarePhase[s] += squareStep[s];
            }
            const double metal = squareSvf.run(static_cast<double>(sum) / r.squares);
            x += metal * (square.next() + squareFront.next());
        }
        if (hashOn) {
            const double white = noiseAt(seed, static_cast<std::uint32_t>(i));
            if (noiseOn) {
                const long at = static_cast<long>(i);
                if (r.bursts > 0) {
                    if (at < tailAt + gapN && at % gapN == 0) burst.start(1.0, r.burstDecay, sampleRate); // (the last burst lies over the tail's start)
                    if (at == tailAt) noiseFall.start(r.noiseLevel * r.tailLevel, r.noiseDecay, sampleRate);
                }
                if (riseN > 0 && at == riseN) noiseFall.start(r.noiseLevel, r.noiseDecay, sampleRate);
                double env = noiseFall.next() + noiseFront.next() + r.noiseLevel * burst.next();
                if (at < riseN) env += r.noiseLevel * static_cast<double>(at) / static_cast<double>(riseN);
                x += noiseSvf.run(white) * noiseGain * env;
            }
            if (clickOn) {
                clickLow += clickC * (white - clickLow);
                x += clickLow * clickGain * click.next();
            }
        }
        out[i] = static_cast<float>(x);
        const double a = std::fabs(out[i]); // (the float's: so the loudest sample over it is 1 exactly)
        if (a > loudest) loudest = a;
    }

    // Pass 2: DRIVE on the sum set to a peak of 1, then the peak set to kPeak, and the end faded to
    // exact zeros. The loudest sample lands on kPeak however hard it is driven (soft(g) / soft(g)).
    if (!(loudest > 0)) {
        std::fill(out.begin(), out.end(), 0.f);
        return out;
    }
    const double g = 1.0 + 7.0 * r.drive * r.drive;
    const bool driven = r.drive > 0;
    const double most = soft(g);
    const std::size_t tail = std::min(n, static_cast<std::size_t>(std::lround(kTailSeconds * rate)));
    for (std::size_t i = 0; i < n; ++i) {
        const double x = out[i] / loudest;
        double y = driven ? soft(g * x) / most : x;
        if (i + tail >= n) y *= static_cast<double>(n - 1 - i) / static_cast<double>(tail); // the last sample is 0
        out[i] = static_cast<float>(kPeak * y);
    }
    return out;
}

} // namespace drumsynth
} // namespace apotheca
