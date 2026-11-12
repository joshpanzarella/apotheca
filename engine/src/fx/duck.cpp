// DUCK: the track dips each time a drum is hit, and comes back up.
//
//   AMOUNT    how far it dips: 0 not at all, 100% right down to nothing
//   RELEASE   how long it takes to come back: 20 ms to 1 second
//   SOURCE    which lane of the first drum track it listens for: 1 to 8
//
// It is keyed by the HIT, not by the drum's sound: the engine tells it "these
// lanes were hit on the first sample of the block you are about to get"
// (Effect::drumHits). So there is no lag, nothing to set a threshold for, and
// it works with the drum muted, turned down or playing a silent recording.
// Every hit counts, whatever its level: a pattern's hit, each hit of a
// ratchet, and one played by hand.
//
// The shape, all pinned by tests/effects_test.cpp:
//   - At the hit the level falls in a straight line to 1 - AMOUNT, at a speed
//     that would take kFall (3 ms) from the top. Not at once: a step in level
//     is a click, and a muted drum would not be there to hide it.
//   - Then it comes back over RELEASE, quickly at first and easing in at the
//     end: (1 - t)^2 of the dip is left at t of the way. It ARRIVES, exactly,
//     and from then on the sound passes untouched to the sample.
//   - A hit before it is back starts the fall again from where the level is.
//
// meter() is how many dB it is dipping, for the screen (Engine::effectMeter).

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kLanes = 8;
constexpr float kFall = 0.003f;  // seconds, from the top to the full dip
constexpr float kGlide = 0.02f;  // seconds for a turned setting to arrive
constexpr float kMostDb = 60.f;  // what the meter says for "right down"
constexpr float kDbPerLog = 8.6858896f; // 20 / ln(10)

float releaseSeconds(float v) { return 0.02f * std::pow(50.f, clamp01(v)); }
int laneOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kLanes);
    return i > kLanes - 1 ? kLanes - 1 : i;
}

class Duck : public Unit {
public:
    Duck() : Unit(kDuckInfo) {}
    void reset() override {
        dip_ = 0.f, back_ = 1.0, meter_ = 0.f;
        falling_ = hit_ = false;
        depth_.snap(), rate_.snap();
    }
    void drumHits(unsigned lanes) override {
        if ((lanes >> lane_) & 1u) hit_ = true;
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        // At rest a turned setting is simply there: nothing is sounding that could jump.
        if (!falling_ && dip_ == 0.f) depth_.snap(), rate_.snap();
        if (hit_) {
            hit_ = false;
            falling_ = true;
        }
        float most = 0.f;
        for (int done = 0; done < frames; done += kBlock) {
            if (!falling_ && dip_ == 0.f) break; // back up: the rest is the sound as it came in
            const float got = block(left + done, right + done, frames - done < kBlock ? frames - done : kBlock);
            most = got > most ? got : most;
        }
        meter_ = most <= 0.f ? 0.f : (most >= 0.999f ? kMostDb : -kDbPerLog * std::log(1.f - most));
    }
    float meter() const override { return meter_; }

protected:
    void changed() override {
        depth_.to = p_[0];
        rate_.to = 1.f / (releaseSeconds(p_[1]) * sr_);
        lane_ = laneOf(p_[2]);
        fallStep_ = 1.f / (kFall * sr_);
    }

private:
    static constexpr int kBlock = 64;

    // Returns the most it took off in the block, 0 to 1 of the sound.
    float block(float* left, float* right, int frames) {
        const float part = glideK(frames, kGlide, sr_);
        depth_.begin(frames, part);
        rate_.begin(frames, part);
        float dip = dip_, most = 0.f;
        double back = back_; // (a double: a second of small steps added as floats lands 50 samples out)
        bool falling = falling_;
        for (int i = 0; i < frames; ++i) {
            const float depth = depth_.next(), rate = rate_.next();
            if (falling) {
                dip += fallStep_;
                if (dip > 0.99999f) dip = 1.f, back = 0.0, falling = false; // (the steps do not add up to 1 to the bit)
            } else if (dip > 0.f) {
                back += static_cast<double>(rate);
                const float to = static_cast<float>(1.0 - back);
                dip = back >= 1.0 ? 0.f : to * to;
            }
            const float off = depth * dip;
            most = off > most ? off : most;
            left[i] -= left[i] * off;
            right[i] -= right[i] * off;
        }
        dip_ = dip, back_ = back, falling_ = falling;
        depth_.done(), rate_.done();
        return most;
    }

    Glide depth_, rate_;
    float dip_ = 0.f;   // how much of AMOUNT is on just now, 0 to 1
    double back_ = 1.0; // how far through the way back, 0 to 1
    float fallStep_ = 1.f / 144.f, meter_ = 0.f;
    int lane_ = 0;
    bool falling_ = false, hit_ = false;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 1) {
        const float seconds = releaseSeconds(v);
        if (seconds < 0.9995f) std::snprintf(out, n, "%d MS", static_cast<int>(std::round(seconds * 1000.f)));
        else std::snprintf(out, n, "1.0 S");
    } else if (index == 2) {
        std::snprintf(out, n, "LANE %d", laneOf(v) + 1);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Duck; }

} // namespace

const EffectInfo kDuckInfo = {
    "DUCK", 3, {"AMOUNT", "RELEASE", "SOURCE", ""}, {0.6f, 0.6f, 0.f, 0.f}, // lane 1, 209 ms
    "duck", EffectGroup::Level, {0, 0, kLanes, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
