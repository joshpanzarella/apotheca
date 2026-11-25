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
//   phaseAt, centsOff         a part's phase, and its pitch read by how its phase
//                             moves over 60 periods (PHYSIC: 0.5 cent and finer)
//   sizeAt, t60At             a part's size at a time, and its T60 (PHYSIC)
//   ringOnOf                  how much a reverb's tail rings on the same notes: the
//                             fine structure of its spectrum, second after second
//
// Header only; no framework, no CHECK (each test program keeps its own).

#ifndef APOTHECA_TESTS_MEASURE_HPP
#define APOTHECA_TESTS_MEASURE_HPP

#include <algorithm>
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

// HOW MUCH A TAIL RINGS ON THE SAME NOTES (HALL, 2026-11-25). A reverb that stands still is a set of notes (its
// modes), each falling at its own rate: the same few stand out of its spectrum second after second, and are heard
// ringing on. Bent lines move the notes, and which stand out changes. `count` windows of a second each, from
// `first` seconds on (Hann, 65536 bins: 0.73 Hz each), each bin against the mean of its neighbours within 40 Hz
// (so only which notes stand out is left, not the tail's tone), between `lo` and `hi`. Every two windows:
//   alike      how alike the two are, in dB (a correlation: 1 the same notes stand out, 0 unrelated ones)
//   standing   the 20 notes that stood out most in the earlier, how far over their neighbours they still stand in
//              the later (dB; unrelated windows give about 1.5)
// Each the mean over every two windows. (The loudest note against its neighbours in one window says nothing: it
// is 10 to 13 dB in any tail, standing still or not: the effects plan found that.)
struct RingOn {
    double alike = 0.0, standing = 0.0;
};
inline RingOn ringOnOf(const std::vector<float>& v, double first, int count, double lo, double hi, double rate = 48000.0) {
    const std::size_t n = 65536, width = static_cast<std::size_t>(rate);
    const double bin = rate / static_cast<double>(n);
    const int from = static_cast<int>(lo / bin), to = static_cast<int>(hi / bin), near = static_cast<int>(40.0 / bin);
    std::vector<std::vector<double>> fine; // each window's bins against their neighbours, from `from` to `to`
    for (int w = 0; w < count; ++w) {
        const std::size_t at = static_cast<std::size_t>((first + w) * rate);
        std::vector<double> re(n, 0.0), im(n, 0.0);
        for (std::size_t i = 0; i < width && at + i < v.size(); ++i)
            re[i] = v[at + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(width - 1)));
        for (std::size_t i = 1, j = 0; i < n; ++i) { // the bit-reversed order, then the butterflies (as spectrumOf)
            std::size_t b = n >> 1;
            for (; j & b; b >>= 1) j ^= b;
            j ^= b;
            if (i < j) std::swap(re[i], re[j]), std::swap(im[i], im[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const double a = -2.0 * kPi / static_cast<double>(len);
            for (std::size_t i = 0; i < n; i += len)
                for (std::size_t k = 0; k < len / 2; ++k) {
                    const double c = std::cos(a * static_cast<double>(k)), s = std::sin(a * static_cast<double>(k));
                    const std::size_t u = i + k, t = i + k + len / 2;
                    const double tr = re[t] * c - im[t] * s, ti = re[t] * s + im[t] * c;
                    re[t] = re[u] - tr, im[t] = im[u] - ti;
                    re[u] += tr, im[u] += ti;
                }
        }
        std::vector<double> power(n / 2), out;
        for (std::size_t i = 0; i < n / 2; ++i) power[i] = re[i] * re[i] + im[i] * im[i] + 1e-300;
        for (int i = from; i < to; ++i) {
            double around = 0.0;
            for (int j = i - near; j <= i + near; ++j) around += power[static_cast<std::size_t>(j)];
            out.push_back(power[static_cast<std::size_t>(i)] / (around / (2 * near + 1)));
        }
        fine.push_back(out);
    }
    RingOn r;
    int pairs = 0;
    for (int a = 0; a < count; ++a)
        for (int b = a + 1; b < count; ++b) {
            const std::vector<double>&x = fine[static_cast<std::size_t>(a)], &y = fine[static_cast<std::size_t>(b)];
            const double m = static_cast<double>(x.size());
            double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i) {
                const double p = 10.0 * std::log10(x[i]), q = 10.0 * std::log10(y[i]);
                sx += p, sy += q, sxx += p * p, syy += q * q, sxy += p * q;
            }
            r.alike += (sxy / m - sx / m * sy / m) / std::sqrt((sxx / m - sx / m * sx / m) * (syy / m - sy / m * sy / m));
            std::vector<std::pair<double, std::size_t>> peaks; // the notes of the earlier: bins over both neighbours
            for (std::size_t i = 1; i + 1 < x.size(); ++i)
                if (x[i] > x[i - 1] && x[i] >= x[i + 1]) peaks.push_back(std::make_pair(x[i], i));
            std::sort(peaks.begin(), peaks.end(), [](const std::pair<double, std::size_t>& p, const std::pair<double, std::size_t>& q) { return p.first > q.first; });
            double stand = 0.0;
            for (std::size_t k = 0; k < 20 && k < peaks.size(); ++k) {
                const std::size_t c = peaks[k].second;
                double most = 0.0; // (a note may slide a bin either way)
                for (std::size_t j = c - 1; j <= c + 1; ++j) most = y[j] > most ? y[j] : most;
                stand += 10.0 * std::log10(most) / 20.0;
            }
            r.standing += stand;
            ++pairs;
        }
    r.alike /= pairs, r.standing /= pairs;
    return r;
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

// ---- a part's phase, pitch, size and decay (PHYSIC) ------------------------------------------

// The phase of the part at `hz` in a window of `width` samples centred on `centre` (Hann), the
// sine's phase taken from sample 0: for a part at hz + e, it moves 2 pi e a second.
inline double phaseAt(const std::vector<float>& v, double centre, double hz, double width, double rate = 48000.0) {
    const long from = static_cast<long>(centre - width / 2), n = static_cast<long>(width);
    const double w = 2.0 * kPi * hz / rate;
    double re = 0.0, im = 0.0;
    double cr = std::cos(w * from), ci = -std::sin(w * from); // e^{-j w n}, stepped
    const double sr = std::cos(w), si = -std::sin(w);
    for (long i = 0; i < n; ++i) {
        const long k = from + i;
        if (k < 0 || k >= static_cast<long>(v.size())) break;
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * (i + 0.5) / n);
        re += win * v[static_cast<std::size_t>(k)] * cr;
        im += win * v[static_cast<std::size_t>(k)] * ci;
        const double nr = cr * sr - ci * si, ni = cr * si + ci * sr;
        cr = nr, ci = ni;
        if ((i & 1023) == 1023) { // (keep the stepped phasor on the circle)
            const double m = std::sqrt(cr * cr + ci * ci);
            cr /= m, ci /= m;
        }
    }
    return std::atan2(im, re);
}
// THE FREQUENCY of the part near `hz`, by how its phase moves over `periods` of its cycles (the
// plan's way: 60), each read through a window of `wide` cycles. In cents from `hz`.
inline double centsOff(const std::vector<float>& v, double hz, double startSeconds, double periods = 60.0, double wide = 32.0, double rate = 48000.0) {
    const double width = wide * rate / hz, apart = periods * rate / hz;
    const double c1 = startSeconds * rate + width / 2;
    double d = phaseAt(v, c1 + apart, hz, width, rate) - phaseAt(v, c1, hz, width, rate);
    while (d > kPi) d -= 2.0 * kPi;
    while (d < -kPi) d += 2.0 * kPi;
    const double e = d / (2.0 * kPi * apart / rate); // hertz off
    return 1200.0 * std::log2((hz + e) / hz);
}
// A part's size at `hz`, read through a window of `wide` cycles from `startSeconds`.
inline double sizeAt(const std::vector<float>& v, double hz, double startSeconds, double wide = 32.0, double rate = 48000.0) {
    const std::size_t from = static_cast<std::size_t>(startSeconds * rate);
    std::size_t n = static_cast<std::size_t>(wide * rate / hz);
    if (from + n > v.size()) n = v.size() - from;
    return partNear(v.data() + from, n, hz, rate);
}
// A part's T60 at `hz`: its size read at two times `apart` seconds apart.
inline double t60At(const std::vector<float>& v, double hz, double t1, double apart, double wide = 32.0, double rate = 48000.0) {
    const double a = sizeAt(v, hz, t1, wide, rate), b = sizeAt(v, hz, t1 + apart, wide, rate);
    return apart * 60.0 / (dB(a) - dB(b));
}

#endif
