// COMP: a compressor. It turns the loud parts down, then the whole up.
//
//   THRESHOLD   how loud is "loud": -48 dB up to 0 dB (a sound that just
//               reaches full scale is at 0)
//   RATIO       how hard: 1.5:1, 2:1, 3:1, 4:1, 8:1, 20:1. At 4:1 a sound
//               8 dB over the threshold comes out 2 dB over
//   SPEED       how fast it acts and lets go, together: from 1 ms / 50 ms
//               (fast: it catches every hit) to 30 ms / 400 ms (slow: hits
//               get through and the whole is held together)
//   MIX         0 is the sound untouched; part way is the squashed sound
//               under the plain one ("parallel" compression)
//
// How it works. The level is read in dB from the louder of left and right
// (so the picture does not lean), with everything under 60 Hz taken out
// first: a kick's thump does not push the whole mix down. Over the threshold
// the sound is turned down by (1 - 1/ratio) of the dB it is over, easing in
// over a 6 dB knee. That wanted turn-down is held from peak to peak (it
// falls at the release speed) and then smoothed at the attack speed, both in
// dB. The make-up gain is automatic: half of what a full-scale sound is
// turned down by, and never more than 12 dB.
//
// In a master slot this is the glue compressor.
//
// meter() is how many dB it is turning down right now, for the screen
// (Engine::effectMeter).

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kRatios[6] = {1.5f, 2.f, 3.f, 4.f, 8.f, 20.f};
constexpr float kKnee = 6.f;         // dB, centred on the threshold
constexpr float kMaxMakeUp = 12.f;   // dB
constexpr float kIgnoreBelow = 60.f; // Hz
constexpr float kDbPerLog = 8.6858896f; // 20 / ln(10)
constexpr float kGlide = 0.02f;

int ratioOf(float v) {
    const int i = static_cast<int>(clamp01(v) * 6.f);
    return i > 5 ? 5 : i;
}
float thresholdDb(float v) { return -48.f + 48.f * clamp01(v); }
float attackMs(float v) { return std::pow(30.f, clamp01(v)); }
float releaseMs(float v) { return 50.f * std::pow(8.f, clamp01(v)); }

class Comp : public Unit {
public:
    Comp() : Unit(kCompInfo) {}
    void reset() override {
        for (auto& side : state_) side[0] = side[1] = 0.f;
        held_ = down_ = meter_ = 0.f;
        settled_ = false;
    }
    void process(float* left, float* right, int frames) override {
        float most = 0.f;
        for (int done = 0; done < frames; done += kBlock) {
            const float got = block(left + done, right + done, frames - done < kBlock ? frames - done : kBlock);
            most = got > most ? got : most;
        }
        meter_ = most;
    }
    float meter() const override { return meter_; }

protected:
    void changed() override {
        threshold_ = thresholdDb(p_[0]);
        slope_ = 1.f - 1.f / kRatios[ratioOf(p_[1])];
        kneeFloor_ = std::exp((threshold_ - 0.5f * kKnee) / kDbPerLog); // under this level nothing is wanted: no logarithm needed
        attack_ = std::exp(-1000.f / (attackMs(p_[2]) * sr_));
        release_ = std::exp(-1000.f / (releaseMs(p_[2]) * sr_));
        float makeUp = 0.5f * slope_ * -threshold_;
        if (makeUp > kMaxMakeUp) makeUp = kMaxMakeUp;
        makeUp_.to = std::exp(makeUp / kDbPerLog);
        mix_.to = p_[3];
        const float g = std::tan(3.14159265f * kIgnoreBelow / sr_);
        a1_ = 1.f / (1.f + g * (g + 1.41421356f));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

private:
    static constexpr int kBlock = 64;

    // The detector's high-pass: what is left of `in` with the bottom taken out.
    float above(float in, float* s) const {
        const float v3 = in - s[1];
        const float v1 = a1_ * s[0] + a2_ * v3;
        const float v2 = s[1] + a2_ * s[0] + a3_ * v3;
        s[0] = 2.f * v1 - s[0];
        s[1] = 2.f * v2 - s[1];
        return in - 1.41421356f * v1 - v2;
    }

    // Returns the most it turned down in the block, in dB.
    float block(float* left, float* right, int frames) {
        if (!settled_) {
            makeUp_.snap();
            mix_.snap();
            settled_ = true;
        }
        const float part = glideK(frames, kGlide, sr_);
        makeUp_.begin(frames, part);
        mix_.begin(frames, part);
        float held = held_, down = down_, most = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float inL = left[i], inR = right[i];
            const float l = std::fabs(above(inL, state_[0])), r = std::fabs(above(inR, state_[1]));
            const float level = l > r ? l : r;
            // How many dB down this level wants to be.
            float want = 0.f;
            if (level > kneeFloor_) {
                const float over = kDbPerLog * std::log(level) - threshold_;
                want = over >= 0.5f * kKnee ? slope_ * over : slope_ * (over + 0.5f * kKnee) * (over + 0.5f * kKnee) / (2.f * kKnee);
            }
            // Held from peak to peak, falling at the release speed; then eased in at the attack speed.
            held = want > held ? want : want + release_ * (held - want);
            down = held + attack_ * (down - held);
            if (down < 1e-5f && held < 1e-5f) held = down = 0.f; // let go entirely: nothing to work out, and no vanishing numbers
            most = down > most ? down : most;
            const float gain = (down > 0.f ? std::exp(down * (-1.f / kDbPerLog)) : 1.f) * makeUp_.next();
            const float mix = mix_.next();
            left[i] = inL + mix * (inL * gain - inL);
            right[i] = inR + mix * (inR * gain - inR);
        }
        held_ = held;
        down_ = down;
        makeUp_.done();
        mix_.done();
        for (auto& side : state_)
            for (float& s : side)
                if (std::fabs(s) < 1e-20f) s = 0.f;
        return most;
    }

    float state_[2][2] = {};
    float held_ = 0.f, down_ = 0.f, meter_ = 0.f;
    float threshold_ = -18.f, slope_ = 0.75f, kneeFloor_ = 0.f, attack_ = 0.f, release_ = 0.f;
    float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f;
    Glide makeUp_, mix_;
    bool settled_ = false;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        std::snprintf(out, n, "%d DB", static_cast<int>(std::round(thresholdDb(v))));
    } else if (index == 1) {
        static const char* const names[6] = {"1.5:1", "2:1", "3:1", "4:1", "8:1", "20:1"};
        std::snprintf(out, n, "%s", names[ratioOf(v)]);
    } else if (index == 2) {
        std::snprintf(out, n, "%d/%d MS", static_cast<int>(std::round(attackMs(v))), static_cast<int>(std::round(releaseMs(v))));
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Comp; }

} // namespace

const EffectInfo kCompInfo = {
    "COMP", 4, {"THRESHOLD", "RATIO", "SPEED", "MIX"}, {0.625f, 0.58f, 0.4f, 1.f},
    "comp", EffectGroup::Level, {0, 6, 0, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
