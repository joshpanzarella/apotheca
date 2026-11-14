// DRIVE: a soft clipper with a tone control after it.
//
// Old songs use it. Its sound is pinned to the bit by tests/effects_test.cpp:
// do not change the arithmetic (not even to fix its aliasing; that is what
// SATURATE is for).

#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

class Drive : public Unit {
public:
    Drive() : Unit(kDriveInfo) {}
    void reset() override { tone_[0] = tone_[1] = 0.f; }
    void process(float* left, float* right, int frames) override {
        float* io[2] = {left, right};
        for (int c = 0; c < 2; ++c) {
            float tone = tone_[c];
            for (int i = 0; i < frames; ++i) {
                const float in = io[c][i];
                tone += coeff_ * (std::tanh(in * gain_) * makeUp_ - tone);
                io[c][i] = (in + p_[2] * (tone - in)) * out_;
            }
            tone_[c] = std::fabs(tone) < 1e-20f ? 0.f : tone;
        }
    }

protected:
    void changed() override {
        gain_ = std::pow(40.f, p_[0]); // 1 to 40 times
        makeUp_ = 1.f / std::sqrt(gain_); // more drive is thicker, not just louder
        const float hz = 500.f * std::pow(40.f, p_[1]); // 500 Hz to 20 kHz
        coeff_ = 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr_ ? 0.45f * sr_ : hz) / sr_);
        out_ = 2.f * p_[3]; // the middle is "as it came in"
    }

private:
    float tone_[2] = {};
    float gain_ = 1.f, makeUp_ = 1.f, coeff_ = 1.f, out_ = 1.f;
};

void reading(int index, float v, const float*, char* out, int size) {
    // OUTPUT: the middle is 100%, as it came in.
    if (index == 3) std::snprintf(out, static_cast<std::size_t>(size), "%d%%", static_cast<int>(std::round(v * 100.f)) * 2);
    else readPercent(v, out, size);
}

Effect* create() { return new Drive; }

} // namespace

const EffectInfo kDriveInfo = {
    "DRIVE", 4, {"DRIVE", "TONE", "MIX", "OUTPUT"}, {0.35f, 0.7f, 1.f, 0.5f},
    "drive", EffectFamily::Drive, {0, 0, 0, 0}, reading, create, "hard edge",
};

} // namespace fx
} // namespace apotheca
