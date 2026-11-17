// MORPH's tables (tables.hpp says what a table is). Made on the app's thread, never on the audio
// thread; every sum the table's own, so the handheld makes the desk's tables to the bit.

// No multiply and add joined, in this file and in everything it includes (the house polynomials in
// jx3p/ir3109.hpp too): the handheld's bits are the desk's. Above every #include, or what they hold is
// compiled without it (it was below them, and nine tables came out other on the handheld).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "tables.hpp"

#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>

#include "../jx3p/ir3109.hpp" // exp2Poly, sinPoly, cosPoly: the house's own polynomials


namespace apotheca {
namespace morph {

using jx3p::exp2Poly;
using jx3p::sinPoly;
using jx3p::cosPoly;

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963267948966;

// A complex number in two doubles (the FFT's).
struct Point {
    double re = 0.0, im = 0.0;
};

// The FFT, radix 2, in place; `back` is the inverse (no 1/N). Its turns are read from a table of
// kRecipePoints / 2 sines and cosines made by the polynomials.
struct Fft {
    std::vector<Point> turn; // e^(-2 pi i k / kRecipePoints), k < kRecipePoints / 2
    Fft() : turn(kRecipePoints / 2) {
        for (int k = 0; k < kRecipePoints / 2; ++k) {
            const double t = static_cast<double>(k) / kRecipePoints;
            turn[k].re = turnCos(t);
            turn[k].im = -turnSin(t);
        }
    }
    void run(std::vector<Point>& a, int n, bool back) const {
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            const int stride = kRecipePoints / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; ++k) {
                    const Point w = turn[k * stride];
                    const double wi = back ? -w.im : w.im;
                    Point& u = a[i + k];
                    Point& v = a[i + k + len / 2];
                    const double tr = v.re * w.re - v.im * wi, ti = v.re * wi + v.im * w.re;
                    v.re = u.re - tr, v.im = u.im - ti;
                    u.re += tr, u.im += ti;
                }
        }
    }
};
const Fft& fft() {
    static const Fft f;
    return f;
}

double held(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double frac(double x) { return x - std::floor(x); }

// The waves the recipes are made of, x in turns (0..1). A jump is worked out at its middle.
double sine(double x) { return turnSin(x); }
double triangle(double x) { // in step with the sine: its top a quarter in
    x = frac(x);
    return x < 0.25 ? 4.0 * x : (x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0);
}
double saw(double x) { // falling, so its fundamental is the sine's
    x = frac(x);
    return x == 0.0 ? 0.0 : 1.0 - 2.0 * x;
}
double pulse(double x, double width) {
    x = frac(x);
    if (x == 0.0 || x == width) return 0.0;
    return x < width ? 1.0 : -1.0;
}
double ownTanh(double z) {
    if (z > 20.0) return 1.0;
    if (z < -20.0) return -1.0;
    const double e = exp2Poly(2.0 * z * 1.4426950408889634); // e^(2z)
    return (e - 1.0) / (e + 1.0);
}
double ownExp(double z) { return exp2Poly(z * 1.4426950408889634); }
// A hash's 0..1, for GRIT's rolls (never a generator walked).
double unit(std::uint32_t a, std::uint32_t b) {
    return static_cast<double>(chordHash(a * 0x9E3779B1u ^ chordHash(b + 0x7F4A7C15u)) >> 8) * (1.0 / 16777216.0);
}

// ---- the recipes: a wave of the frame's share t (0..1) and the place in its cycle x ----------------

double analog(double t, double x) {
    const double s = held(t, 0.0, 1.0) * 4.0;
    const int seg = s >= 4.0 ? 3 : static_cast<int>(s);
    const double f = s - seg;
    switch (seg) {
    case 0: return (1.0 - f) * sine(x) + f * triangle(x);
    case 1: return (1.0 - f) * triangle(x) + f * saw(x);
    case 2: return (1.0 - f) * saw(x) + f * pulse(x, 0.5);
    default: return pulse(x, 0.5 - 0.4 * f);
    }
}
double pulseWave(double t, double x) { return pulse(x, 0.5 - 0.47 * t); }
double sync(double t, double x) { return saw((1.0 + 7.0 * t) * frac(x)); }
double fold(double t, double x) {
    const double v = (1.0 + 7.0 * t) * sine(x);
    return 4.0 * std::fabs(frac((v - 1.0) / 4.0) - 0.5) - 1.0;
}
double bend(double t, double x) {
    const double d = 0.5 - 0.47 * t;
    x = frac(x);
    return sine(x < d ? 0.5 * x / d : 0.5 + 0.5 * (x - d) / (1.0 - d));
}
double fm(double t, double x) { return sine(x + 8.0 * t * sine(x) / kTwoPi); }
double ratio(double t, double x) {
    const double r = 1.0 + 6.0 * t;
    int k = static_cast<int>(r);
    if (k > 6) k = 6;
    const double f = r - k;
    return (1.0 - f) * sine(x + 2.0 * sine(k * x) / kTwoPi) + f * sine(x + 2.0 * sine((k + 1) * x) / kTwoPi);
}
double drive(double t, double x) {
    const double g = 1.0 + 39.0 * t * t;
    return ownTanh(g * sine(x)) / ownTanh(g);
}
double steps(double t, double x) {
    const double n = std::floor(64.0 * exp2Poly(-5.0 * t) + 0.5);
    return sine((std::floor(frac(x) * n) + 0.5) / n);
}

// ---- the recipes given as harmonics: c (cosine) and s (sine) for n = 1..kTopHarmonics -------------

using Harmonics = void (*)(double t, double* c, double* s);

void vowel(double t, double*, double* s) {
    // Two formants walking A, E, I, O, U, in harmonics of a voice near 110 Hz (SELF).
    static const double f1[5] = {6.6, 4.8, 2.5, 5.2, 2.7}, f2[5] = {9.9, 16.7, 20.8, 7.6, 7.9};
    const double p = held(t, 0.0, 1.0) * 4.0;
    int k = static_cast<int>(p);
    if (k > 3) k = 3;
    const double f = p - k;
    const double a = f1[k] + f * (f1[k + 1] - f1[k]), b = f2[k] + f * (f2[k + 1] - f2[k]);
    for (int n = 1; n <= kTopHarmonics; ++n) {
        const double da = (n - a) / (1.0 + 0.15 * a), db = (n - b) / (1.0 + 0.15 * b);
        s[n] = (1.0 / std::sqrt(static_cast<double>(n))) * (0.02 + 1.0 / (1.0 + da * da) + 0.7 / (1.0 + db * db));
    }
}
void organ(double t, double*, double* s) {
    static const int harmonic[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16}; // 16', 8', 5 1/3, 4', 2 2/3, 2', 1 3/5, 1 1/3, 1'
    for (int k = 0; k < 9; ++k) s[harmonic[k]] = k == 0 ? 1.0 : held(t * 9.0 - k + 1.0, 0.0, 1.0);
}
void harmonicWalk(double t, double*, double* s) {
    const double h = 1.0 + 31.0 * held(t, 0.0, 1.0);
    const int k = h >= 32.0 ? 31 : static_cast<int>(h);
    const double f = h - k;
    s[k] = 1.0 - f;
    s[k + 1] = f;
}
void grit(double t, double* c, double* s) {
    // Four rolls of every harmonic's size, at 0, a third, two thirds and the end, slid between; each
    // harmonic's phase one roll for the whole table, so two frames never cancel where they meet.
    const double p = held(t, 0.0, 1.0) * 3.0;
    int k = static_cast<int>(p);
    if (k > 2) k = 2;
    const double f = p - k;
    for (int n = 1; n <= kTopHarmonics; ++n) {
        const double u0 = unit(1000u + k, n), u1 = unit(1001u + k, n);
        const double size = ((1.0 - f) * u0 * u0 + f * u1 * u1) / n;
        const double phase = unit(77u, n);
        c[n] = size * turnCos(phase);
        s[n] = size * turnSin(phase);
    }
}
void comb(double t, double*, double* s) {
    const double d = 0.04 + 0.21 * t; // the notches' spacing, in harmonics' turns
    for (int n = 1; n <= kTopHarmonics; ++n) s[n] = turnCos(0.5 * n * d) / n;
}
void bell(double t, double*, double* s) {
    static const int harmonic[9] = {1, 3, 7, 11, 16, 23, 31, 42, 55};
    static const double size[9] = {1.0, 0.7, 0.9, 0.5, 0.6, 0.35, 0.4, 0.25, 0.2};
    for (int k = 0; k < 9; ++k) s[harmonic[k]] = size[k] * ownExp(-t * harmonic[k] / 8.0);
}
void glass(double t, double*, double* s) {
    s[1] = 1.0;
    for (int n = 3; n <= 31; n += 2) s[n] = held(t * 16.0 - ((n - 1) / 2 - 1), 0.0, 1.0) / std::sqrt(static_cast<double>(n));
}

struct Recipe {
    double (*wave)(double, double);
    Harmonics harmonics;
};
const Recipe kRecipes[kTables] = {
    {analog, nullptr}, {pulseWave, nullptr}, {sync, nullptr},  {nullptr, vowel}, {nullptr, organ}, {nullptr, harmonicWalk},
    {fold, nullptr},   {bend, nullptr},      {fm, nullptr},    {ratio, nullptr}, {drive, nullptr}, {steps, nullptr},
    {nullptr, grit},   {nullptr, comb},      {nullptr, bell},  {nullptr, glass},
};

Table makeTable(const Recipe& r, int frames) {
    std::vector<double> c(static_cast<std::size_t>(frames) * (kTopHarmonics + 1), 0.0), s(c.size(), 0.0);
    if (r.harmonics) {
        for (int f = 0; f < frames; ++f) {
            const double t = frames > 1 ? static_cast<double>(f) / (frames - 1) : 0.0;
            r.harmonics(t, &c[static_cast<std::size_t>(f) * (kTopHarmonics + 1)], &s[static_cast<std::size_t>(f) * (kTopHarmonics + 1)]);
        }
        return tableFromHarmonics(frames, c, s);
    }
    return tableFromWaves(frames, r.wave);
}

std::mutex gMade;
std::unique_ptr<Table>* gTables = nullptr; // never freed: an engine may read one as the program ends
double gSeconds = 0.0;

} // namespace

double turnSin(double turns) {
    double t = turns - std::floor(turns);
    const int q = static_cast<int>(t * 4.0);
    const double a = (t * 4.0 - q) * kHalfPi; // 0 .. a quarter turn
    switch (q & 3) {
    case 0: return sinPoly(a);
    case 1: return cosPoly(a);
    case 2: return -sinPoly(a);
    default: return -cosPoly(a);
    }
}
double turnCos(double turns) { return turnSin(turns + 0.25); }

long sizeOffset(int m) {
    long at = kGuardBefore;
    for (int k = 0; k < m; ++k) at += sizeLength(k) + kGuardBefore + kGuardAfter;
    return at;
}

const float* Table::at(int frame, int m) const { return data.data() + static_cast<std::size_t>(frame) * frameFloats() + sizeOffset(m); }

Table tableFromHarmonics(int frames, const std::vector<double>& cosine, const std::vector<double>& sine) {
    Table table;
    if (frames < kFramesLeast) frames = kFramesLeast;
    table.frames = frames;
    table.data.assign(static_cast<std::size_t>(frames) * frameFloats(), 0.f);
    table.amp.assign(static_cast<std::size_t>(frames) * (kTopHarmonics + 1), 0.f);
    std::vector<Point> a(kLengthMost);
    const std::size_t given = cosine.size() / (kTopHarmonics + 1);
    for (int f = 0; f < frames; ++f) {
        const std::size_t from = static_cast<std::size_t>(f < static_cast<int>(given) ? f : static_cast<int>(given) - 1) * (kTopHarmonics + 1);
        // Every size by an inverse FFT of the harmonics it holds: value(x) = sum c cos + s sin, so the
        // bin n is (c - i s) / 2 and its mirror the other half.
        double scale = 1.0;
        for (int pass = 0; pass < 2; ++pass) {
            for (int m = pass == 0 ? 0 : 0; m < (pass == 0 ? 1 : kSizes); ++m) {
                const int length = sizeLength(m), h = sizeHarmonics(m);
                for (int i = 0; i < length; ++i) a[i] = Point();
                for (int n = 1; n <= h; ++n) {
                    a[n].re = 0.5 * cosine[from + n] * scale;
                    a[n].im = -0.5 * sine[from + n] * scale;
                    a[length - n].re = a[n].re;
                    a[length - n].im = -a[n].im;
                }
                fft().run(a, length, true);
                if (pass == 0) { // the top size's peak: every size of the frame is scaled so it is 1
                    double peak = 0.0;
                    for (int i = 0; i < length; ++i) peak = std::fabs(a[i].re) > peak ? std::fabs(a[i].re) : peak;
                    scale = peak > 1e-12 ? 1.0 / peak : 1.0;
                    continue;
                }
                float* d = table.data.data() + static_cast<std::size_t>(f) * frameFloats() + sizeOffset(m);
                for (int i = 0; i < length; ++i) d[i] = static_cast<float>(a[i].re);
                d[-1] = d[length - 1];
                d[length] = d[0];
                d[length + 1] = d[1];
            }
        }
        for (int n = 1; n <= kTopHarmonics; ++n)
            table.amp[static_cast<std::size_t>(f) * (kTopHarmonics + 1) + n] =
                static_cast<float>(scale * std::sqrt(cosine[from + n] * cosine[from + n] + sine[from + n] * sine[from + n]));
    }
    return table;
}

Table tableFromWaves(int frames, double (*wave)(double, double)) {
    if (frames < kFramesLeast) frames = kFramesLeast;
    std::vector<double> c(static_cast<std::size_t>(frames) * (kTopHarmonics + 1), 0.0), s(c.size(), 0.0);
    std::vector<Point> a(kRecipePoints);
    for (int f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / (frames - 1);
        for (int i = 0; i < kRecipePoints; ++i) {
            a[i].re = wave(t, static_cast<double>(i) / kRecipePoints);
            a[i].im = 0.0;
        }
        fft().run(a, kRecipePoints, false);
        // X_n = sum x e^(-i w n): c_n = 2 Re X / N, s_n = -2 Im X / N.
        for (int n = 1; n <= kTopHarmonics; ++n) {
            c[static_cast<std::size_t>(f) * (kTopHarmonics + 1) + n] = 2.0 * a[n].re / kRecipePoints;
            s[static_cast<std::size_t>(f) * (kTopHarmonics + 1) + n] = -2.0 * a[n].im / kRecipePoints;
        }
    }
    return tableFromHarmonics(frames, c, s);
}

double makeBuiltInTables() {
    std::lock_guard<std::mutex> lock(gMade);
    if (gTables) return 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<Table>* made = new std::unique_ptr<Table>[kTables];
    for (int t = 0; t < kTables; ++t) made[t].reset(new Table(makeTable(kRecipes[t], kTableFrames)));
    gTables = made;
    gSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return gSeconds;
}

bool builtInTablesMade() {
    std::lock_guard<std::mutex> lock(gMade);
    return gTables != nullptr;
}

double builtInTablesSeconds() {
    std::lock_guard<std::mutex> lock(gMade);
    return gSeconds;
}

const Table& builtInTable(int t) {
    makeBuiltInTables();
    return *gTables[t < 0 || t >= kTables ? 0 : t];
}

} // namespace morph
} // namespace apotheca
