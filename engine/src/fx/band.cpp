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
// The filter is EQ's state-variable filter with the same sums for each shape (Andrew Simper's). FREQ, GAIN and Q
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
Numbers numbersOf(int shape, float hz, float db, float q, float sr) {
    q = usedQ(shape, q);
    const float a = std::pow(10.f, db / 40.f), g = svfG(hz, sr), k = 1.f / q;
    switch (shape) {
    case kNotch: return {g, k, 1.f, -k, 0.f};
    case kLowShelf: return {g / std::sqrt(a), k, 1.f, k * (a - 1.f), a * a - 1.f};
    case kHighShelf: return {g * std::sqrt(a), k, a * a, k * (1.f - a) * a, 1.f - a * a};
    default: {
        const float bell = 1.f / (q * a); // narrower as it cuts, wider as it lifts: the same width heard either way
        return {g, bell, 1.f, bell * (a * a - 1.f), 0.f};
    }
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
