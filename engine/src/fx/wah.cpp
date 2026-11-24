// WAH: an auto-wah (an envelope filter), a TYPE of FILTER (owner, 2026-11-24: "Let's do auto-wah"). A
// resonant low-pass whose corner follows how loud the sound is: a hit opens it and it closes as the sound
// dies, the "quack" of a funk bass or a clavinet. Nothing moves it but the playing.
//
//   SENSE      how far a sound opens it: low, only the loudest hits open it a little; high, a quiet one
//              sweeps it all the way (250 Hz shut to 3.5 kHz open).
//   SPEED      how fast it follows: from 2 ms in / 60 ms back (fast: a quack on every note) to 30 ms in /
//              600 ms back (slow: a swell).
//   RESONANCE  how much it sings at its corner (Q 1 to 12). Named and read as FILTER's and VOWEL's (a percentage),
//              so changing TYPE keeps the knob where it was (read as a Q, a FILTER at 50% came over as Q 12).
//   MIX        0 is the sound untouched, to the sample.
//
// The level is the louder of left and right, followed sample by sample (in fast, back slowly). What moves is
// worked out on the TICK (svf.hpp): the corner once every 16 samples counted from the start, from the level
// as it is then, and slid to sample by sample (Slide); SENSE, SPEED and RESONANCE set off gently (Gentle);
// MIX glides every sample. The low-pass is made up by Q to the 1/2 at its corner's height, so the resonance
// does not make it much louder than the sound. A filter state under 1e-20 is made zero on a tick; with nothing
// coming in and nothing left in it over kQuiet it is asleep (what comes out is what came in), the level with
// it: it wakes as a fresh one.

#include <initializer_list>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

enum { kA1, kA2, kA3, kGain, kPerFilter };
constexpr float kShutHz = 250.f, kOpenHz = 3500.f;
constexpr float kSettle = 0.015f, kMixSettle = 0.02f;
constexpr float kLevelTiny = 1e-9f; // a level under this is none (it falls to it at the end of a tail)

float qOf(float v) { return 1.f * std::pow(12.f, clamp01(v)); }
// How much a level opens it at SENSE v: up to 40 times, by the square of SENSE (the bottom of the knob is fine).
float senseGain(float v) { return 1.f + 39.f * clamp01(v) * clamp01(v); }
float attackMs(float v) { return 2.f * std::pow(15.f, clamp01(v)); }    // 2 to 30 ms
float releaseMs(float v) { return 60.f * std::pow(10.f, clamp01(v)); }  // 60 to 600 ms

class Wah : public Unit {
public:
    Wah() : Unit(kWahInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        changed();
        reset();
    }
    void reset() override {
        for (Svf& s : filter_) s.reset();
        level_ = 0.f;
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
            // The level: in fast, back slowly.
            const float in = std::fmax(std::fabs(x[0]), std::fabs(x[1]));
            level_ += (in > level_ ? attack_ : release_) * (in - level_);
            if (level_ < kLevelTiny) level_ = 0.f;
            if (tick_ == 0) beginTick();
            const float mix = mix_.next();
            filters_.next();
            const float* const n = filters_.now;
            for (int c = 0; c < 2; ++c) {
                float v1, v2;
                filter_[c].step(n[kA1], n[kA2], n[kA3], x[c], v1, v2);
                if (mix != 0.f) io[c][i] += mix * (n[kGain] * v2 - x[c]);
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        sense_.first.to = p_[0];
        speed_.first.to = p_[1];
        q_.first.to = p_[2];
        mix_.to = p_[3];
    }

private:
    void beginTick() {
        mix_.begin(kTick, glideK(kTick, kMixSettle, sr_));
        const float k = glideK(kTick, kSettle, sr_);
        for (Gentle* g : {&sense_, &speed_, &q_}) g->tick(k);
        follow();
        shape();
        filters_.aim(); // the corner moves with the level: aimed every tick
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        filters_.land();
        float loudest = 0.f;
        for (Svf& s : filter_) s.settle(), loudest = std::fmax(loudest, s.loudest());
        if (quietIn_ >= kTick && loudest < kQuiet) { // nothing came in all this tick, and nothing is left ringing
            for (Svf& s : filter_) s.reset();
            level_ = 0.f;
            asleep_ = true;
        }
    }
    // How fast the level follows, from SPEED as it is now.
    void follow() {
        attack_ = 1.f - std::exp(-1000.f / (attackMs(speed_.now()) * sr_));
        release_ = 1.f - std::exp(-1000.f / (releaseMs(speed_.now()) * sr_));
    }
    // The corner from the level and SENSE (a square root, so a quiet sound still moves it), and the filter there.
    void shape() {
        float open = level_ * senseGain(sense_.now());
        open = open > 1.f ? 1.f : std::sqrt(open);
        const float hz = kShutHz * std::pow(kOpenHz / kShutHz, open), q = qOf(q_.now());
        SvfShape f;
        f.set(svfG(hz, sr_), 1.f / q);
        float* const to = filters_.to;
        to[kA1] = f.a1, to[kA2] = f.a2, to[kA3] = f.a3, to[kGain] = 1.f / std::sqrt(q > 1.f ? q : 1.f);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        sense_.snap(), speed_.snap(), q_.snap(), mix_.snap();
        follow();
        shape();
        filters_.snap();
    }

    Svf filter_[2];
    Slide<kPerFilter> filters_; // the filter's numbers and its make-up
    Gentle sense_, speed_, q_;
    Glide mix_;
    float level_ = 0.f, attack_ = 0.f, release_ = 0.f;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 1) {
        std::snprintf(out, n, "%.0f/%.0f MS", static_cast<double>(attackMs(v)), static_cast<double>(releaseMs(v)));
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Wah; }

} // namespace

const EffectInfo kWahInfo = {
    "WAH", 4, {"SENSE", "SPEED", "RESONANCE", "MIX"}, {0.5f, 0.3f, 0.5f, 1.f}, // a middling quack, Q 3.5, all of it
    "wah", EffectFamily::Filter, {0, 0, 0, 0}, reading, create,
    "an auto-wah: opens as you play",
};

} // namespace fx
} // namespace apotheca
