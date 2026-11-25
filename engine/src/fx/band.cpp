// BAND: one band of an EQ, set anywhere, a TYPE of FILTER (owner, 2026-11-24: "set a band, move it freely and
// change the Q ... maybe you only want to notch out a certain frequency"). EQ's three bands stay as they were (a
// song's EQ sounds the same to the bit); this is the band a mixing desk's "parametric" EQ has, one to a slot.
//
//   FREQ   where it is, 20 Hz to 20 kHz. A D-pad push is a thirtieth of the range (pushes 30 in the row), which is
//          10^0.1, a third of an octave to 0.1%: the points are 20 * 10^(k/10) Hz, so 50.2, 63.2, 100 and 1002 Hz.
//          It starts on the 17th, reading 1.0 KHZ, so the D-pad comes back to it (gesture audit G19: a push was a
//          twentieth, half an octave, and the start 0.5663 was on none; 50 Hz could not be reached)
//   GAIN   -12 to +12 dB (as EQ's: 1.2 dB a push). NOTCH has none: it reads "-"
//   Q      how narrow, 0.3 (wide: some three octaves) to 30 (a single note): Q 1.5 to start. A shelf goes no
//          higher than Q 2 (a little bump at its corner; past it a shelf only rings: at Q 30 full-scale noise came out
//          19 times as loud), and reads so
//   SHAPE  BELL (up or down around FREQ), NOTCH (FREQ taken right out, as narrow as Q), LOW SHELF (everything
//          under FREQ up or down alike), HIGH SHELF (everything over it)
//
// No MIX, as EQ: a BELL or a shelf at 0 dB is the sound untouched, to the bit (its numbers are exactly 1 and 0).
// NOTCH always takes its frequency out.
//
// The filter is EQ's state-variable filter with the same sums for each shape (Andrew Simper's), but for the BELL's
// numbers: those are matched to the analogue bell (Vicanek's way, bellOf below), so a bell set high keeps its shape
// up to 20 kHz instead of being squeezed back to 0 dB at 24 kHz (2026-11-24). FREQ, GAIN and Q
// glide for about 20 ms, setting off gently (Gentle), worked out on the TICK (svf.hpp), the numbers slid to sample by
// sample. A change of SHAPE fades from the old shape's numbers to the new one's over 10 ms (corner, damping and the
// three amounts the out is made of, each in a straight line: every step between is itself a filter that holds), one
// fade at a time, the last asked for winning (TREMOLO's SHAPE). A filter state under 1e-20 is made zero on a tick;
// with nothing coming in and nothing left in it over kQuiet it is asleep (what comes out is what came in) and wakes
// as a fresh one.

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

enum Shape { kBell, kNotch, kLowShelf, kHighShelf, kShapes };
constexpr float kMostDb = 12.f;
constexpr float kLeastQ = 0.3f, kQRange = 100.f; // Q 0.3 to 30
constexpr float kSettle = 0.01f;                 // each of Gentle's two glides: there in about 20 ms
constexpr int kFadeTicks = 30;                   // a change of SHAPE: 30 ticks of 16, 10 ms at 48 kHz

float hzOf(float v) { return 20.f * std::pow(1000.f, clamp01(v)); }
float dbOf(float v) { return (2.f * clamp01(v) - 1.f) * kMostDb; }
constexpr float kShelfMostQ = 2.f;
float qOf(float v) { return kLeastQ * std::pow(kQRange, clamp01(v)); }
int shapeOf(float v) {
    const int s = static_cast<int>(clamp01(v) * kShapes);
    return s >= kShapes ? kShapes - 1 : s;
}

// What the filter's numbers are made from: its corner and damping (g, k) and what its out is made of (in, band,
// low). Faded between two shapes as numbers, then made into the filter's three.
struct Numbers {
    float g, k, m0, m1, m2;
};
// The Q a shape uses: a shelf's held at kShelfMostQ.
float usedQ(int shape, float q) { return (shape == 2 || shape == 3) && q > kShelfMostQ ? kShelfMostQ : q; }

// THE BELL, MATCHED (Martin Vicanek, "Matched Second Order Digital Filters", 2016). The analogue bell is
// H(s) = (s^2 + s A/Q + 1) / (s^2 + s/(Q A) + 1), A = 10^(dB/40): narrower as it cuts, wider as it lifts, the same
// width heard either way. Made from it the plain way (Simper's sums on g = tan(pi f / rate)) a bell "cramps" near
// the top: that g puts the analogue's infinity at half the rate, where the gain is then held at exactly 1, so a
// bell at 16 kHz was pulled back toward 0 dB by 24 kHz whatever Q asked (6.0 dB off the analogue at 16 kHz, Q 1.5;
// 7.0 at 20 kHz, Q 4). Matched: the poles are the analogue's own (impulse invariance, e^(sT)), and the top is chosen
// so the gain is the analogue's at 0 Hz (1), at FREQ (GAIN) and at half the rate (|H| there, no longer 1): 0.8 and
// 1.0 dB off. On the unit circle g, k and m0..m2 are an analogue biquad in t = tan(w / 2), the filter's out
//   (m0 s^2 + beta s + g^2) / (s^2 + g k s + g^2),  beta = g (m0 k + m1),  m0 + m2 = 1 (1 at 0 Hz)
// so m0 is the gain at half the rate and beta is what puts the gain at GAIN at FREQ. Only a lift is matched; a cut
// is that lift turned upside down (its poles the lift's zeros), so +x and -x dB still add to nothing everywhere.
// At 0 dB, the plain bell's numbers to the bit: {g, 1/Q, 1, 0, 0}, the sound untouched.
// Worked out in double, and only on a tick when a setting moves. (Vicanek's own sums, his B0..B2, cancel: worked in
// float in a model they came out 17 dB wrong at 40 Hz, Q 30, 56 at 100 Hz. These do not cancel, within 0.0001 dB
// of double in the same model; double is the belt and braces.)
// beta^2 t^2 is never under 5% of its first term on the whole range (20 Hz to 20 kHz, Q 0.3 to 30, at 22.05 to
// 96 kHz), so the cut's damping, beta / (g sqrt(m0)), is always over 0: every cut holds.
Numbers bellOf(float hzIn, float db, float q, float sr) {
    if (db == 0.f) return {svfG(hzIn, sr), 1.f / q, 1.f, 0.f, 0.f};
    const double rate = sr, most = 0.45 * rate, hz = hzIn > most ? most : hzIn; // (svfG's ceiling)
    const double a = std::pow(10.0, std::fabs(static_cast<double>(db)) / 40.0), qd = static_cast<double>(q) * a; // the lift's
    const double w0 = 2.0 * 3.14159265358979323846 * hz / rate, zeta = 0.5 / qd, s = -zeta * w0;
    // The poles' 1 + a1 + a2 and 1 - a1 + a2 (their z polynomial at z = 1 and -1), written so nothing cancels at
    // 20 Hz: |1 - p|^2 and |1 + p|^2 for a pair p = e^(s +- j wd); two real poles when Q A is under 0.5.
    double plus, minus;
    if (zeta <= 1.0) {
        const double e = std::exp(s), em = std::expm1(s), h = std::sin(0.5 * w0 * std::sqrt(1.0 - zeta * zeta)), turn = 4.0 * e * h * h;
        plus = em * em + turn;
        minus = (1.0 + e) * (1.0 + e) - turn;
    } else {
        const double wd = w0 * std::sqrt(zeta * zeta - 1.0);
        plus = std::expm1(s + wd) * std::expm1(s - wd);
        minus = (1.0 + std::exp(s + wd)) * (1.0 + std::exp(s - wd));
    }
    const double g2 = plus / minus, g = std::sqrt(g2), k = -2.0 * std::expm1(2.0 * s) / (g * minus);
    const double over = 0.5 * rate / hz, x = 1.0 - over * over, top = a * a * over / qd, under = over / qd;
    const double m0 = std::sqrt((x * x + top * top) / (x * x + under * under)); // the analogue's gain at half the rate
    const double t = std::tan(0.5 * w0), t2 = t * t, peak2 = a * a * a * a, low = g2 - m0 * t2;
    const double beta = std::sqrt(peak2 * ((g2 - t2) * (g2 - t2) + g2 * k * k * t2) - low * low) / t;
    const auto f = [](double v) { return static_cast<float>(v); };
    if (db > 0.f) return {f(g), f(k), f(m0), f(beta / g - m0 * k), f(1.0 - m0)};
    const double root = std::sqrt(m0); // the cut: 1 / that, its poles g^2 / m0 and g k = beta / m0
    return {f(g / root), f(beta / (g * root)), f(1.0 / m0), f((k - beta / (m0 * g)) / root), f(1.0 - 1.0 / m0)};
}

Numbers numbersOf(int shape, float hz, float db, float q, float sr) {
    if (shape == kBell) return bellOf(hz, db, q, sr);
    q = usedQ(shape, q);
    const float a = std::pow(10.f, db / 40.f), g = svfG(hz, sr), k = 1.f / q;
    switch (shape) {
    case kNotch: return {g, k, 1.f, -k, 0.f};
    case kLowShelf: return {g / std::sqrt(a), k, 1.f, k * (a - 1.f), a * a - 1.f};
    default: return {g * std::sqrt(a), k, a * a, k * (1.f - a) * a, 1.f - a * a}; // HIGH SHELF
    }
}

enum { kA1, kA2, kA3, kM0, kM1, kM2, kPerFilter };

class Band : public Unit {
public:
    Band() : Unit(kBandInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        changed();
        reset();
    }
    void reset() override {
        for (Svf& s : state_) s.reset();
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {heardIn(left[i]), heardIn(right[i])};
            const bool none = x[0] == 0.f && x[1] == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            filter_.next();
            const float* n = filter_.now;
            for (int c = 0; c < 2; ++c) {
                float v1, v2;
                state_[c].step(n[kA1], n[kA2], n[kA3], x[c], v1, v2);
                const float s = n[kM0] * x[c] + n[kM1] * v1 + n[kM2] * v2; // a BELL or a shelf at 0 dB: x, exactly
                const float change = s - x[c];
                if (change != 0.f) io[c][i] += change; // (so a -0 stays -0)
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        for (int i = 0; i < 3; ++i) set_[i].first.to = p_[i];
        wanted_ = shapeOf(p_[3]);
    }

private:
    void beginTick() {
        // A new SHAPE starts its fade when none is under way (the last asked for wins).
        if (wanted_ != shape_ && fade_ == 0) {
            from_ = numbersNow();
            shape_ = wanted_;
            fade_ = kFadeTicks;
        }
        if (!(fade_ > 0 || set_[0].moving() || set_[1].moving() || set_[2].moving())) return;
        const float k = glideK(kTick, kSettle, sr_);
        for (Gentle& g : set_) g.tick(k);
        if (fade_ > 0) --fade_;
        aimAt(numbersNow());
        filter_.aim();
    }
    void endTick() {
        tick_ = 0;
        filter_.land();
        float loudest = 0.f;
        for (Svf& s : state_) s.settle(), loudest = std::fmax(loudest, s.loudest());
        if (quietIn_ >= kTick && loudest < kQuiet) {
            for (Svf& s : state_) s.reset();
            asleep_ = true;
        }
    }
    // The numbers for the settings as they are now: the shape's, or partway from the old one's while it fades.
    Numbers numbersNow() const {
        Numbers to = numbersOf(shape_, hzOf(set_[0].now()), dbOf(set_[1].now()), qOf(set_[2].now()), sr_);
        if (fade_ == 0) return to;
        const float part = static_cast<float>(kFadeTicks - fade_) / static_cast<float>(kFadeTicks); // 0 at its start
        const auto mix = [part](float a, float b) { return a + part * (b - a); };
        return {mix(from_.g, to.g), mix(from_.k, to.k), mix(from_.m0, to.m0), mix(from_.m1, to.m1), mix(from_.m2, to.m2)};
    }
    void aimAt(const Numbers& n) {
        SvfShape f;
        f.set(n.g, n.k);
        float* to = filter_.to;
        to[kA1] = f.a1, to[kA2] = f.a2, to[kA3] = f.a3, to[kM0] = n.m0, to[kM1] = n.m1, to[kM2] = n.m2;
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    // Where the settings are, at once: no glide and no fade.
    void arrive() {
        for (Gentle& g : set_) g.snap();
        shape_ = wanted_;
        fade_ = 0;
        aimAt(numbersNow());
        filter_.snap();
    }

    Svf state_[2];
    Slide<kPerFilter> filter_;
    Gentle set_[3]; // FREQ, GAIN, Q
    Numbers from_ = {0.f, 0.f, 1.f, 0.f, 0.f};
    int shape_ = kBell, wanted_ = kBell, fade_ = 0;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

const char* const kShapeNames[kShapes] = {"BELL", "NOTCH", "LOW SHELF", "HIGH SHELF"};

void reading(int index, float v, const float* all, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const float hz = hzOf(v);
        if (std::round(hz) >= 1000.f) std::snprintf(out, n, "%.1f KHZ", static_cast<double>(hz / 1000.f)); // (999.98 is 1.0 KHZ)
        else std::snprintf(out, n, "%d HZ", static_cast<int>(std::round(hz)));
    } else if (index == 1) {
        if (all && shapeOf(all[3]) == kNotch) {
            std::snprintf(out, n, "-"); // (a notch has no gain: it takes FREQ out)
            return;
        }
        const float db = std::round(dbOf(v) * 10.f) / 10.f;
        if (db == 0.f) std::snprintf(out, n, "0 DB");
        else if (db == std::round(db)) std::snprintf(out, n, "%+d DB", static_cast<int>(db));
        else std::snprintf(out, n, "%+.1f DB", static_cast<double>(db));
    } else if (index == 2) {
        const float q = usedQ(all ? shapeOf(all[3]) : kBell, qOf(v));
        std::snprintf(out, n, q < 10.f ? "Q %.1f" : "Q %.0f", static_cast<double>(q));
    } else {
        std::snprintf(out, n, "%s", kShapeNames[shapeOf(v)]);
    }
}

Effect* create() { return new Band; }

} // namespace

const EffectInfo kBandInfo = {
    "BAND", 4, {"FREQ", "GAIN", "Q", "SHAPE"}, {17.f / 30.f, 0.5f, 0.3495f, 0.125f}, // 1 kHz, 0 dB, Q 1.5, a BELL: untouched
    "band", EffectFamily::Filter, {0, 0, 0, kShapes}, reading, create,
    "one band anywhere: bell, notch, shelf", nullptr, nullptr,
    {30, 0, 0, 0}, // FREQ: a third of an octave a push
};

} // namespace fx
} // namespace apotheca
