// The measuring tools every sound test shares (JX-3P plan, batch 0).
//
// Moved here from effects_test.cpp unchanged, so effects_test gives the same
// numbers it did; the new synth's tests use the same readers. Nothing here
// knows an Effect or a Synth: each tool reads a run of samples.
//
//   Sum                       a checksum of every bit (the same bits, or not)
//   Noise                     noise that is the same on every machine
//   dB, rmsOf, peakOf         the level reader
//   sineOf                    a sine to feed in
//   partAt                    one frequency's size in a run of samples
//   harmonicsIn               the spectrum reader: a sine's harmonics, its
//                             aliasing (folded-back parts) and the rest
//   pitchOf, pitchAsItGoes    the pitch reader, by upward crossings of zero
//   largestJump, sineStep     "no click": the largest step between samples
//   bandOf                    one band of a sound (half an octave about a pitch)
//   partNear                  one frequency's size, through a window: right for
//                             any frequency, not only one whose cycles fit whole
//   asideOf                   the loudest part that is not a harmonic, under a
//                             frequency: an oscillator's aliasing (JX-3P batch 2)
//
// Header only; no framework, no CHECK (each test program keeps its own).

#ifndef APOTHECA_TESTS_MEASURE_HPP
#define APOTHECA_TESTS_MEASURE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// ---- checksums ------------------------------------------------------------------------------

// A checksum of every bit of a run of samples (FNV-1a, 64 bits).
struct Sum {
    std::uint64_t h = 1469598103934665603ull;
    void add(const float* v, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t bits;
            std::memcpy(&bits, &v[i], sizeof bits);
            for (int b = 0; b < 4; ++b) {
                h ^= (bits >> (8 * b)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
    }
    void add(const std::vector<float>& v) { add(v.data(), v.size()); }
};

// ---- signals --------------------------------------------------------------------------------

// Noise that is the same on every machine: -1..1.
struct Noise {
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed) {}
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(state >> 8) - 8388608) / 8388608.f;
    }
};

static const double kPi = 3.141592653589793;

inline std::vector<float> sineOf(double hz, double amp, std::size_t frames, double rate = 48000.0) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return v;
}

// ---- the level reader -----------------------------------------------------------------------

inline double dB(double ratio) { return ratio > 1e-30 ? 20.0 * std::log10(ratio) : -600.0; }

inline double rmsOf(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > v.size()) to = v.size();
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i) e += static_cast<double>(v[i]) * v[i];
    return to > from ? std::sqrt(e / static_cast<double>(to - from)) : 0.0;
}
inline float peakOf(const std::vector<float>& v) {
    float peak = 0.f;
    for (float s : v) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    return peak;
}

// ---- the spectrum reader --------------------------------------------------------------------

// How strong one frequency is in a run of samples: the size of a sine at `hz`
// (so a sine of 0.5 reads 0.5). Exact when a whole number of its cycles fits.
inline double partAt(const float* v, std::size_t count, double hz, double rate = 48000.0) {
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPi * hz / rate;
    for (std::size_t i = 0; i < count; ++i) {
        re += v[i] * std::cos(w * static_cast<double>(i));
        im += v[i] * std::sin(w * static_cast<double>(i));
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(count);
}

// HARMONICS AND ALIASING of a sound whose pitch is `hz` (a whole number of
// hertz, so one second holds whole cycles and every part is measured
// exactly), read off one second (`n` = 48000 samples at 48 kHz) of it: the
// fundamental, its harmonics (2, 3, 4... times `hz`, up to half the sample
// rate), and aliasing: harmonics too high for the sample rate, folded back
// down to where no harmonic is. All in dB against `amp` (for an effect, the
// size of the sine that went in; for an oscillator, its fundamental).
struct Harmonics {
    double part[8] = {};   // [1] the sine itself, [2] the second harmonic...; -600: none, or above the top
    double strongest = -600.0; // the loudest harmonic above the first
    double alias = -600.0;     // the loudest fold-back
    double aliasHz = 0.0;
    double rest = -600.0;      // everything that is neither the sine nor a harmonic, added up
    double aliasLow = -600.0;  // the loudest fold-back that lands under 19 kHz (above it a 2x stage is only part way shut, and nobody hears)
    double aliasLowHz = 0.0;
    double level = -600.0;     // all of it together, harmonics and the rest, against the sine that went in
};
inline Harmonics harmonicsIn(const float* out, std::size_t n, int hz, double amp) {
    Harmonics h;
    for (double& p : h.part) p = -600.0;
    double mean = 0.0, total = 0.0, harmonic = 0.0;
    for (std::size_t i = 0; i < n; ++i) mean += out[i];
    mean /= static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) total += (out[i] - mean) * (out[i] - mean);
    total /= static_cast<double>(n);
    for (int k = 1; k * hz < 24000; ++k) {
        const double a = partAt(out, n, static_cast<double>(k) * hz);
        harmonic += 0.5 * a * a;
        if (k < 8) h.part[k] = dB(a / amp);
        if (k > 1 && dB(a / amp) > h.strongest) h.strongest = dB(a / amp);
    }
    for (int k = 24000 / hz + 1; k <= 80; ++k) {
        int folded = (k * hz) % 48000;
        if (folded > 24000) folded = 48000 - folded;
        if (folded % hz == 0 || folded == 0 || folded == 24000) continue; // lands on a harmonic: cannot be told apart
        const double a = dB(partAt(out, n, folded) / amp);
        if (a > h.alias) {
            h.alias = a;
            h.aliasHz = folded;
        }
        if (folded < 19000 && a > h.aliasLow) {
            h.aliasLow = a;
            h.aliasLowHz = folded;
        }
    }
    h.level = 10.0 * std::log10(total / (0.5 * amp * amp) + 1e-60);
    const double rest = total - harmonic;
    h.rest = rest > 0.0 ? 10.0 * std::log10(rest / (0.5 * amp * amp)) : -600.0;
    return h;
}

// One frequency's size (a sine of 0.5 reads 0.5) through a Hann window, so it need not fit whole
// cycles; anything 50 or more of its bins (rate / count) away leaks in at under -100 dB.
inline double partNear(const float* v, std::size_t count, double hz, double rate = 48000.0) {
    double re = 0.0, im = 0.0, sum = 0.0;
    const double w = 2.0 * kPi * hz / rate;
    for (std::size_t i = 0; i < count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(count));
        re += window * v[i] * std::cos(w * static_cast<double>(i));
        im += window * v[i] * std::sin(w * static_cast<double>(i));
        sum += window;
    }
    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

// The size of each bin of a run of `count` samples (a power of two) through a four-term
// Blackman-Harris window (its side lobes are 92 dB down), in bins of rate / count.
inline std::vector<double> spectrumOf(const float* v, std::size_t count) {
    std::vector<double> re(count), im(count, 0.0);
    double mean = 0.0;
    for (std::size_t i = 0; i < count; ++i) mean += v[i];
    mean /= static_cast<double>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double x = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(count);
        re[i] = (v[i] - mean) * (0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) - 0.01168 * std::cos(3.0 * x));
    }
    for (std::size_t i = 1, j = 0; i < count; ++i) { // the bit-reversed order
        std::size_t bit = count >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(re[i], re[j]), std::swap(im[i], im[j]);
    }
    for (std::size_t len = 2; len <= count; len <<= 1) {
        const double a = -2.0 * kPi / static_cast<double>(len);
        for (std::size_t i = 0; i < count; i += len)
            for (std::size_t k = 0; k < len / 2; ++k) {
                const double c = std::cos(a * static_cast<double>(k)), s = std::sin(a * static_cast<double>(k));
                const std::size_t u = i + k, t = i + k + len / 2;
                const double tr = re[t] * c - im[t] * s, ti = re[t] * s + im[t] * c;
                re[t] = re[u] - tr, im[t] = im[u] - ti;
                re[u] += tr, im[u] += ti;
            }
    }
    std::vector<double> size(count / 2);
    for (std::size_t i = 0; i < count / 2; ++i) size[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);
    return size;
}

// AN OSCILLATOR'S ALIASING: the loudest part of `count` samples (a power of two) that is not a
// harmonic of `hz` (more than 6 bins from every whole multiple of it) and lies between 20 Hz and
// `below`, in dB against the part at `reference` (its fundamental, or another harmonic named).
struct Aside {
    double dB = -600.0;
    double hz = 0.0;
};
inline Aside asideOf(const float* v, std::size_t count, double hz, double below, double reference, double rate = 48000.0) {
    const std::vector<double> size = spectrumOf(v, count);
    const double bin = rate / static_cast<double>(count);
    double ref = 0.0;
    for (std::size_t i = 0; i < size.size(); ++i)
        if (std::fabs(static_cast<double>(i) * bin - reference) <= 3.0 * bin && size[i] > ref) ref = size[i];
    Aside a;
    for (std::size_t i = 0; i < size.size(); ++i) {
        const double f = static_cast<double>(i) * bin;
        if (f < 20.0 || f >= below) continue;
        const double k = std::floor(f / hz + 0.5);
        if (k >= 1.0 && std::fabs(f - k * hz) <= 6.0 * bin) continue;
        const double level = dB(size[i] / ref);
        if (level > a.dB) a.dB = level, a.hz = f;
    }
    return a;
}

// One band of a sound: what is within about half an octave of `hz` (a
// band-pass, twice over), so a tail can be timed at one pitch.
inline std::vector<float> bandOf(const std::vector<float>& v, double hz, double rate = 48000.0) {
    const double w = 2.0 * kPi * hz / rate, alpha = std::sin(w) / (2.0 * 2.0), a0 = 1.0 + alpha;
    const double b0 = alpha / a0, a1 = -2.0 * std::cos(w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<float> out(v.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, p1 = 0, p2 = 0, q1 = 0, q2 = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double x = v[i], y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        const double z = b0 * (y - p2) - a1 * q1 - a2 * q2;
        p2 = p1, p1 = y, q2 = q1, q1 = z;
        out[i] = static_cast<float>(z);
    }
    return out;
}

// ---- the pitch reader -----------------------------------------------------------------------

// THE PITCH of something close to a sine, between two places: by its upward crossings of zero.
inline double pitchOf(const std::vector<float>& v, std::size_t from, std::size_t to, double rate = 48000.0) {
    double first = -1.0, last = -1.0;
    int ups = 0;
    for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
        if (v[i - 1] < 0.f && v[i] >= 0.f) {
            const double at = static_cast<double>(i - 1) + static_cast<double>(-v[i - 1]) / (static_cast<double>(v[i]) - v[i - 1]);
            if (first < 0.0) first = at;
            last = at;
            ++ups;
        }
    }
    return ups >= 2 ? (ups - 1) * rate / (last - first) : 0.0;
}

// A SINE'S PITCH AS IT GOES: its frequency over each run of `cycles` cycles, from where it rises through zero.
inline std::vector<double> pitchAsItGoes(const std::vector<float>& v, std::size_t from, int cycles, std::vector<double>* when = nullptr, double rate = 48000.0) {
    std::vector<double> rises, hz;
    for (std::size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] < 0.f && v[i] >= 0.f) rises.push_back(static_cast<double>(i - 1) + static_cast<double>(-v[i - 1]) / static_cast<double>(v[i] - v[i - 1]));
    for (std::size_t i = static_cast<std::size_t>(cycles); i < rises.size(); ++i) {
        hz.push_back(cycles * rate / (rises[i] - rises[i - static_cast<std::size_t>(cycles)]));
        if (when) when->push_back(0.5 * (rises[i] + rises[i - static_cast<std::size_t>(cycles)]) / rate);
    }
    return hz;
}

// ---- no click -------------------------------------------------------------------------------

// The largest step from one sample to the next.
inline float largestJump(const float* v, std::size_t count) {
    float jump = 0.f;
    for (std::size_t i = 1; i < count; ++i) jump = std::fabs(v[i] - v[i - 1]) > jump ? std::fabs(v[i] - v[i - 1]) : jump;
    return jump;
}
inline float largestJump(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > v.size()) to = v.size();
    return to > from ? largestJump(v.data() + from, to - from) : 0.f;
}
// The largest step a plain sine takes by itself.
inline double sineStep(double hz, double amp, double rate = 48000.0) { return amp * 2.0 * std::sin(kPi * hz / rate); }

#endif
