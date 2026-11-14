// PHASER: a TYPE of MOD. The sound through six stages that each turn its
// phase (allpass filters: they change no loudness, only how late each pitch
// comes out), laid over the plain sound: where the two are half a wave apart
// they cancel, three notches, and the notches sweep up and down. Softer and
// more liquid than a flanger: its notches are spread unevenly, not a comb of
// even teeth, and nothing in it is a copy of the sound late.
//
//   RATE      how fast it sweeps: 0.1 to 8 Hz, CHORUS's scale (carried to the bit)
//   DEPTH     how far: the stages' corner moves from 640 Hz up to 2.6 octaves
//             each way at 100% (105 Hz to 3.9 kHz)
//   FEEDBACK  -100 to +100% (0.9 of it at the ends): the last stage's sound
//             fed back into the first. Up, the peaks between the notches
//             sharpen and whistle; down, they move to other places (FLANGER's
//             name and scale, carried to the bit)
//   MIX       0 is the sound untouched; 100% is the plain sound and the turned
//             one half and half, where the notches go all the way down
//
// Six stages, as the classic pedals with four to six have (the plan's "six
// notches" would be twelve stages: twice the work, and a sound nearer a
// flanger's). The right side's sweep is a quarter turn behind the left's, so
// the notches move across the two sides: the classic stereo phaser.
//
// Where the sweep is, and every setting, is worked out on the TICK (svf.hpp)
// and slid to sample by sample. Each stage's memory under kTiny is made an
// exact zero on the tick; asleep (mod.hpp's rest rule) once nothing has come
// in for 4 s and every stage holds an exact zero. The turned sound kept at
// the same loudness as FEEDBACK rises (scaled by sqrt(1 - feedback^2)).

#include <initializer_list>

#include "mod.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kStages = 6;
constexpr float kCentreHz = 640.f;
constexpr float kOctaves = 2.6f;        // each way, at DEPTH 100%
constexpr float kMostFeedback = 0.9f;
constexpr float kSettle = 0.012f;       // seconds, each of Gentle's two
enum { kLeft, kRight, kFeedback, kDry, kWet, kShapes };

// A stage's number for a corner: H(z) = (a + 1/z) / (1 + a/z).
float allpassFor(float hz, float rate) {
    const float g = svfG(hz, rate);
    return (g - 1.f) / (g + 1.f);
}

class Phaser : public Unit {
public:
    Phaser() : Unit(kPhaserInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        restAfter_ = static_cast<int>(kModRestSeconds * sr_);
        changed();
        reset();
    }
    void reset() override {
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        for (int i = 0; i < frames; ++i) {
            const float xl = heardIn(left[i]), xr = heardIn(right[i]);
            const bool none = xl == 0.f && xr == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            shape_.next();
            const float feedback = shape_.now[kFeedback];
            const float yl = side(state_[0], shape_.now[kLeft], xl + feedback * last_[0]);
            const float yr = side(state_[1], shape_.now[kRight], xr + feedback * last_[1]);
            last_[0] = yl, last_[1] = yr;
            const float dry = shape_.now[kDry], wet = shape_.now[kWet];
            if (dry != 1.f || wet != 0.f) {
                left[i] += (dry - 1.f) * xl + wet * yl;
                right[i] += (dry - 1.f) * xr + wet * yr;
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        rate_.first.to = p_[0];
        depth_.first.to = p_[1];
        feedback_.first.to = p_[2];
        mix_.first.to = p_[3];
    }

private:
    static float side(float* s, float a, float x) {
        for (int k = 0; k < kStages; ++k) {
            const float y = a * x + s[k];
            s[k] = x - a * y;
            x = y;
        }
        return x;
    }
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&rate_, &depth_, &feedback_, &mix_})
            if (s->moving()) s->tick(g);
        phase_ = wrapTurn(phase_ + kTick * static_cast<double>(modRateHz(rate_.now())) / sr_);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        bool empty = true;
        for (int side = 0; side < 2; ++side) {
            for (float& s : state_[side]) s = std::fabs(s) < kTiny ? 0.f : s, empty = empty && s == 0.f;
            last_[side] = std::fabs(last_[side]) < kTiny ? 0.f : last_[side];
            empty = empty && last_[side] == 0.f;
        }
        if (quietIn_ >= restAfter_ && empty) asleep_ = true;
    }
    void shape() {
        const float reach = kOctaves * clamp01(depth_.now());
        shape_.to[kLeft] = allpassFor(kCentreHz * std::exp2(reach * turnSine(phase_)), sr_);
        shape_.to[kRight] = allpassFor(kCentreHz * std::exp2(reach * turnSine(phase_ - 0.25)), sr_);
        const float feedback = kMostFeedback * signedOf(feedback_.now()), mix = mix_.now();
        shape_.to[kFeedback] = feedback;
        shape_.to[kDry] = 1.f - 0.5f * mix;
        shape_.to[kWet] = 0.5f * mix * std::sqrt(1.f - feedback * feedback);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&rate_, &depth_, &feedback_, &mix_}) s->snap();
        for (auto& side : state_)
            for (float& s : side) s = 0.f;
        last_[0] = last_[1] = 0.f;
        phase_ = 0.0;
        shape();
        shape_.snap();
    }

    float state_[2][kStages] = {}, last_[2] = {};
    int tick_ = 0, quietIn_ = 0, restAfter_ = 1;
    bool asleep_ = true;
    double phase_ = 0.0;
    Gentle rate_, depth_, feedback_, mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) modRateText(v, out, size);
    else if (index == 2) signedText(v, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Phaser; }

} // namespace

const EffectInfo kPhaserInfo = {
    "PHASER", 4, {"RATE", "DEPTH", "FEEDBACK", "MIX"}, {0.3f, 0.8f, 0.75f, 1.f}, // 0.37 Hz, 80%, +50%, all of it
    "phaser", EffectFamily::Mod, {0, 0, 0, 0}, reading, create, "notches sweeping",
};

} // namespace fx
} // namespace apotheca
