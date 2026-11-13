// FILTER: state-variable filters.
// Five types (FilterType): a low-pass that falls 12 dB an octave (two poles),
// one that falls 24 (four poles: two of them in a row), band-pass, high-pass,
// and the DJ filter.
//
// Old songs use it. Its sound is pinned to the bit by tests/effects_test.cpp:
// do not change the arithmetic.

#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

class Filter : public Unit {
public:
    Filter() : Unit(kFilterInfo) {}
    void reset() override { for (auto& s : state_) s[0] = s[1] = s[2] = s[3] = 0.f; }
    void process(float* left, float* right, int frames) override {
        float* io[2] = {left, right};
        const float mix = p_[3] * amount_;
        if (mix <= 0.f) {
            // Nothing of it is heard (the DJ filter in its middle): the sound
            // goes by untouched, and the filter forgets what it held.
            reset();
            return;
        }
        for (int c = 0; c < 2; ++c) {
            float ic1 = state_[c][0], ic2 = state_[c][1], jc1 = state_[c][2], jc2 = state_[c][3];
            for (int i = 0; i < frames; ++i) {
                const float in = io[c][i];
                const float v3 = in - ic2;
                const float v1 = a1_ * ic1 + a2_ * v3;
                const float v2 = ic2 + a2_ * ic1 + a3_ * v3;
                ic1 = 2.f * v1 - ic1;
                ic2 = 2.f * v2 - ic2;
                float out = shape_ == 0 ? v2 : (shape_ == 1 ? v1 * k_ : in - k_ * v1 - v2);
                if (fourPole_) {
                    // The second pair of poles: the same corner, no peak of its own.
                    const float w3 = out - jc2;
                    const float w1 = b1_ * jc1 + b2_ * w3;
                    const float w2 = jc2 + b2_ * jc1 + b3_ * w3;
                    jc1 = 2.f * w1 - jc1;
                    jc2 = 2.f * w2 - jc2;
                    out = w2;
                }
                io[c][i] = in + mix * (out - in);
            }
            // Keep a silent filter from grinding on vanishing numbers.
            state_[c][0] = std::fabs(ic1) < 1e-20f ? 0.f : ic1;
            state_[c][1] = std::fabs(ic2) < 1e-20f ? 0.f : ic2;
            state_[c][2] = std::fabs(jc1) < 1e-20f ? 0.f : jc1;
            state_[c][3] = std::fabs(jc2) < 1e-20f ? 0.f : jc2;
        }
    }

protected:
    void changed() override {
        const FilterType type = filterType(p_[2]);
        float hz = 30.f * std::pow(2.f, p_[0] * 9.5f); // 30 Hz to about 21 kHz
        amount_ = 1.f;
        fourPole_ = type == FilterType::LowPass4;
        shape_ = type == FilterType::BandPass ? 1 : type == FilterType::HighPass ? 2 : 0;
        if (type == FilterType::Dj) {
            const int side = djFilter(p_[0], hz);
            shape_ = side > 0 ? 2 : 0;
            // It comes in over the first little way out of the middle, so
            // leaving the middle is not a click.
            const float out = std::fabs(p_[0] - 0.5f) - kDjDead;
            amount_ = side == 0 ? 0.f : (out > kDjFade ? 1.f : out / kDjFade);
        }
        if (hz > 0.45f * sr_) hz = 0.45f * sr_;
        const float g = std::tan(3.14159265f * hz / sr_);
        k_ = 1.f / (0.5f + 11.5f * p_[1] * p_[1]); // Q from 0.5 to 12
        a1_ = 1.f / (1.f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
        const float flat = 1.41421356f; // the second pair: as flat as can be
        b1_ = 1.f / (1.f + g * (g + flat));
        b2_ = g * b1_;
        b3_ = g * b2_;
    }

public:
    static constexpr float kDjDead = 0.02f, kDjFade = 0.03f; // of the knob's travel, each side of the middle

private:
    float state_[2][4] = {};
    float k_ = 2.f, a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, b1_ = 0.f, b2_ = 0.f, b3_ = 0.f, amount_ = 1.f;
    int shape_ = 0; // 0 low, 1 band, 2 high
    bool fourPole_ = false;
};

// "810 HZ", "LOW 24 DB", and for the DJ filter "LOW 2.1K" / "OPEN" / "HIGH 300" (with HZ it ran out of its box).
void reading(int index, float v, const float* all, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0 && all && filterType(all[2]) == FilterType::Dj) {
        float hz = 0.f;
        const int side = djFilter(v, hz);
        if (side == 0) std::snprintf(out, n, "OPEN");
        else if (hz >= 1000.f) std::snprintf(out, n, "%s %.1fK", side < 0 ? "LOW" : "HIGH", hz / 1000.f);
        else std::snprintf(out, n, "%s %d", side < 0 ? "LOW" : "HIGH", static_cast<int>(std::round(hz)));
    } else if (index == 0) {
        const float hz = 30.f * std::pow(2.f, v * 9.5f);
        if (hz >= 1000.f) std::snprintf(out, n, "%.1f KHZ", hz / 1000.f);
        else std::snprintf(out, n, "%d HZ", static_cast<int>(std::round(hz)));
    } else if (index == 2) {
        static const char* names[5] = {"LOW 12 DB", "LOW 24 DB", "BAND PASS", "DJ", "HIGH PASS"};
        std::snprintf(out, n, "%s", names[static_cast<int>(filterType(v))]);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Filter; }

} // namespace

const EffectInfo kFilterInfo = {
    "FILTER", 4, {"CUTOFF", "RESONANCE", "TYPE", "MIX"}, {0.6f, 0.2f, 0.f, 1.f},
    "filter", EffectGroup::Filter, {0, 0, 5, 0}, reading, create, "low, band, high, DJ",
};

} // namespace fx

int djFilter(float v, float& hz) {
    const float dead = 0.02f; // Filter::kDjDead
    if (v < 0.5f - dead) {
        // Low-pass: wide open (20 kHz) next to the middle, down to 60 Hz at the bottom.
        hz = 60.f * std::pow(20000.f / 60.f, v / (0.5f - dead));
        return -1;
    }
    if (v > 0.5f + dead) {
        // High-pass: from 20 Hz next to the middle up to 8 kHz at the top.
        hz = 20.f * std::pow(8000.f / 20.f, (v - 0.5f - dead) / (0.5f - dead));
        return 1;
    }
    hz = 0.f;
    return 0;
}

} // namespace apotheca
