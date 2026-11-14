// EQ: three bands, a TYPE of FILTER. Turns the bottom, a middle you choose and
// the top up or down, the way a mixing desk's EQ does.
//
//   LOW       -12 to +12 dB below about 200 Hz (a shelf: everything under it
//             goes up or down alike): the kick and the bass
//   MID       -12 to +12 dB around MID FREQ (a bell, about an octave and a
//             half wide): boxiness, body, a voice's middle
//   MID FREQ  where the bell is, 125 Hz to 8 kHz (1.0 kHz in the middle;
//             each D-pad push is a little over a minor third)
//   HIGH      -12 to +12 dB above about 5 kHz (a shelf): air and hiss
//
// Four settings and no MIX: an EQ half mixed with the plain sound is a
// weaker EQ (and a comb where the two are out of phase), and every band at
// 0 dB is already the sound untouched. So it is: at 0 dB a band's numbers
// are exactly 1 and 0, and the sound comes out as it went in, to the bit
// (testEq). Each push of the D-pad is 1.2 dB.
//
// The bands are the old FILTER's state-variable filter with the usual shelf
// and bell sums (Andrew Simper's), one after another. Settings glide for
// about 20 ms, setting off gently (Gentle), and are worked out on the TICK
// (svf.hpp), the bands' numbers slid to
// from one tick to the next sample by sample; a filter state under 1e-20
// is made zero on a tick; with nothing coming in and nothing left in it over
// kQuiet it is asleep (what comes out is what came in).

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kMostDb = 12.f;
constexpr float kLowHz = 200.f, kHighHz = 5000.f;
constexpr float kShelfK = 1.41421356f; // a shelf's Q of 0.707: no bump at its corner
constexpr float kBellQ = 0.9f;
constexpr float kSettle = 0.01f; // each of Gentle's two glides: there in about 20 ms, setting off gently
constexpr int kBands = 3;

float dbOf(float v) { return (2.f * clamp01(v) - 1.f) * kMostDb; }
float midHzOf(float v) { return 125.f * std::pow(64.f, clamp01(v)); } // 125 Hz to 8 kHz, 1 kHz in the middle

// Each band's six numbers in the slide: its filter's three, and what its out is made of (in, band, low).
enum { kA1, kA2, kA3, kM0, kM1, kM2, kPerBand };

class Eq : public Unit {
public:
    Eq() : Unit(kEqInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        changed();
        reset();
    }
    void reset() override {
        for (auto& side : state_)
            for (Svf& s : side) s.reset();
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
            band_.next();
            const float* n = band_.now;
            for (int c = 0; c < 2; ++c) {
                float s = x[c], v1, v2;
                for (int b = 0; b < kBands; ++b, n += kPerBand) {
                    state_[c][b].step(n[kA1], n[kA2], n[kA3], s, v1, v2);
                    s = n[kM0] * s + n[kM1] * v1 + n[kM2] * v2; // at 0 dB: s, exactly
                }
                n = band_.now;
                const float change = s - x[c];
                if (change != 0.f) io[c][i] += change; // (so a -0 stays -0 at 0 dB)
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        for (int i = 0; i < 4; ++i) set_[i].first.to = p_[i];
    }

private:
    void beginTick() {
        if (!(set_[0].moving() || set_[1].moving() || set_[2].moving() || set_[3].moving())) return;
        const float k = glideK(kTick, kSettle, sr_);
        for (Gentle& g : set_) g.tick(k); // where they are going at the tick's end
        shape();
        band_.aim(); // and there, sample by sample
    }
    void endTick() {
        tick_ = 0;
        band_.land();
        float loudest = 0.f;
        for (auto& side : state_)
            for (Svf& s : side) s.settle(), loudest = std::fmax(loudest, s.loudest());
        if (quietIn_ >= kTick && loudest < kQuiet) {
            for (auto& side : state_)
                for (Svf& s : side) s.reset();
            asleep_ = true;
        }
    }
    // Where the bands' numbers are going, from where the settings are now.
    void shape() {
        const auto put = [this](int band, float g, float k, float m0, float m1, float m2) {
            SvfShape f;
            f.set(g, k);
            float* to = band_.to + band * kPerBand;
            to[kA1] = f.a1, to[kA2] = f.a2, to[kA3] = f.a3, to[kM0] = m0, to[kM1] = m1, to[kM2] = m2;
        };
        // LOW: a low shelf. A is the gain's square root; at 0 dB exactly 1, so m1 and m2 are exactly 0.
        const float low = std::pow(10.f, dbOf(set_[0].now()) / 40.f);
        put(0, svfG(kLowHz, sr_) / std::sqrt(low), kShelfK, 1.f, kShelfK * (low - 1.f), low * low - 1.f);
        // MID: a bell.
        const float mid = std::pow(10.f, dbOf(set_[1].now()) / 40.f), k = 1.f / (kBellQ * mid);
        put(1, svfG(midHzOf(set_[2].now()), sr_), k, 1.f, k * (mid * mid - 1.f), 0.f);
        // HIGH: a high shelf.
        const float high = std::pow(10.f, dbOf(set_[3].now()) / 40.f);
        put(2, svfG(kHighHz, sr_) * std::sqrt(high), kShelfK, high * high, kShelfK * (1.f - high) * high, 1.f - high * high);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle& g : set_) g.snap();
        shape();
        band_.snap();
    }

    Svf state_[2][kBands];
    Slide<kBands * kPerBand> band_;
    Gentle set_[4];
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 2) {
        const float hz = midHzOf(v);
        if (hz >= 1000.f) std::snprintf(out, n, "%.1f KHZ", hz / 1000.f);
        else std::snprintf(out, n, "%d HZ", static_cast<int>(std::round(hz)));
        return;
    }
    const float db = std::round(dbOf(v) * 10.f) / 10.f;
    if (db == 0.f) std::snprintf(out, n, "0 DB");
    else if (db == std::round(db)) std::snprintf(out, n, "%+d DB", static_cast<int>(db));
    else std::snprintf(out, n, "%+.1f DB", static_cast<double>(db));
}

Effect* create() { return new Eq; }

} // namespace

const EffectInfo kEqInfo = {
    "EQ", 4, {"LOW", "MID", "MID FREQ", "HIGH"}, {0.5f, 0.5f, 0.5f, 0.5f}, // flat, the bell at 1 kHz
    "eq", EffectFamily::Filter, {0, 0, 0, 0}, reading, create,
    "low, mid, high",
};

} // namespace fx
} // namespace apotheca
