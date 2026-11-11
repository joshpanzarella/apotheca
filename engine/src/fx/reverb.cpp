// REVERB: four feedback delays and two smearing stages a side.
//
// Old songs use it. Its sound is pinned to the bit by tests/effects_test.cpp:
// do not change the arithmetic.

#include <vector>

#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

class Reverb : public Unit {
public:
    Reverb() : Unit(kReverbInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        // Lengths with no common factors, so the echoes do not line up; the
        // right side a little longer than the left, for width.
        static const int combMs[4] = {2973, 3371, 3797, 4219};   // hundredths of a millisecond
        static const int smearMs[2] = {503, 167};
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 4; ++i) comb_[c][i].assign(static_cast<std::size_t>((combMs[i] + c * 53) * 1e-5 * sampleRate) + 1, 0.f);
            for (int i = 0; i < 2; ++i) smear_[c][i].assign(static_cast<std::size_t>((smearMs[i] + c * 13) * 1e-5 * sampleRate) + 1, 0.f);
        }
        changed();
    }
    void reset() override {
        for (auto& side : comb_)
            for (auto& line : side) line.assign(line.size(), 0.f);
        for (auto& side : smear_)
            for (auto& line : side) line.assign(line.size(), 0.f);
        for (auto& side : damp_)
            for (float& d : side) d = 0.f;
        for (auto& side : combAt_)
            for (int& a : side) a = 0;
        for (auto& side : smearAt_)
            for (int& a : side) a = 0;
    }
    void process(float* left, float* right, int frames) override {
        if (comb_[0][0].empty()) return;
        float* io[2] = {left, right};
        const float mix = p_[2];
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < frames; ++i) {
                const float in = io[c][i];
                float wet = 0.f;
                for (int k = 0; k < 4; ++k) {
                    std::vector<float>& line = comb_[c][k];
                    int& at = combAt_[c][k];
                    const float out = line[static_cast<std::size_t>(at)];
                    float& damp = damp_[c][k];
                    damp += dampCoeff_ * (out - damp);
                    if (std::fabs(damp) < 1e-20f) damp = 0.f;
                    line[static_cast<std::size_t>(at)] = in * 0.25f + damp * feedback_;
                    if (++at >= static_cast<int>(line.size())) at = 0;
                    wet += out;
                }
                for (int k = 0; k < 2; ++k) {
                    std::vector<float>& line = smear_[c][k];
                    int& at = smearAt_[c][k];
                    const float delayed = line[static_cast<std::size_t>(at)];
                    const float fed = wet + 0.5f * delayed;
                    line[static_cast<std::size_t>(at)] = fed;
                    wet = delayed - 0.5f * fed;
                    if (++at >= static_cast<int>(line.size())) at = 0;
                }
                if (bus_) io[c][i] = mix * wet; // on a bus: the room alone
                else io[c][i] = in + mix * (wet - in * 0.5f * mix); // the dry sound eases back as the room comes up
            }
        }
    }

protected:
    void changed() override {
        feedback_ = 0.7f + 0.28f * p_[0]; // a closet to a hall
        const float hz = 1200.f * std::pow(14.f, 1.f - p_[1]); // more damping, darker tail
        dampCoeff_ = 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr_ ? 0.45f * sr_ : hz) / sr_);
    }

private:
    std::vector<float> comb_[2][4], smear_[2][2];
    int combAt_[2][4] = {}, smearAt_[2][2] = {};
    float damp_[2][4] = {};
    float feedback_ = 0.8f, dampCoeff_ = 1.f;
};

Effect* create() { return new Reverb; }

} // namespace

const EffectInfo kReverbInfo = {
    "REVERB", 3, {"SIZE", "DAMPING", "MIX", ""}, {0.6f, 0.5f, 0.3f, 0.f},
    "reverb", EffectGroup::Reverb, {0, 0, 0, 0}, nullptr, create, // every setting reads as a percentage
};

} // namespace fx
} // namespace apotheca
