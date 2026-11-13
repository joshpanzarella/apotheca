// CRUSH: fewer bits, fewer samples.
//
// Old songs use it. Its sound is pinned to the bit by tests/effects_test.cpp:
// do not change the arithmetic.

#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

class Crush : public Unit {
public:
    Crush() : Unit(kCrushInfo) {}
    void reset() override { held_[0] = held_[1] = 0.f; count_ = 0.f; }
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            count_ += 1.f;
            if (count_ >= every_) {
                count_ -= every_;
                held_[0] = std::round(left[i] * levels_) / levels_;
                held_[1] = std::round(right[i] * levels_) / levels_;
            }
            // Written so that MIX at full is the held value exactly, with no
            // rounding left over from the sound underneath.
            left[i] = held_[0] * p_[2] + left[i] * (1.f - p_[2]);
            right[i] = held_[1] * p_[2] + right[i] * (1.f - p_[2]);
        }
    }

protected:
    void changed() override {
        levels_ = std::pow(2.f, 15.f - 13.f * p_[0]); // 16 bits down to 3
        every_ = std::pow(40.f, p_[1]);               // every sample, down to one in forty
    }

private:
    float held_[2] = {};
    float count_ = 0.f, levels_ = 32768.f, every_ = 1.f;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        std::snprintf(out, n, "%d BITS", static_cast<int>(std::round(16.f - 13.f * v)));
    } else if (index == 1) {
        const int every = static_cast<int>(std::round(std::pow(40.f, v)));
        if (every <= 1) std::snprintf(out, n, "FULL");
        else std::snprintf(out, n, "1 IN %d", every);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Crush; }

} // namespace

const EffectInfo kCrushInfo = {
    "CRUSH", 3, {"BITS", "RATE", "MIX", ""}, {0.5f, 0.3f, 1.f, 0.f},
    "crush", EffectGroup::Dirt, {0, 0, 0, 0}, reading, create, "fewer bits",
};

} // namespace fx
} // namespace apotheca
