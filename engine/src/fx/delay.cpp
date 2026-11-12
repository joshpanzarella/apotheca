// DELAY: echoes in time with the song.
//
// Old songs use it. Its sound is pinned to the bit by tests/effects_test.cpp:
// do not change the arithmetic.

#include <vector>

#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kDelaySteps[6] = {1, 2, 3, 4, 6, 8};

class Delay : public Unit {
public:
    Delay() : Unit(kDelayInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        // Room for the longest echo at the slowest tempo: half a bar at 40 BPM.
        size_ = static_cast<int>(8.0 * 15.0 / 40.0 * sampleRate) + 8;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        changed();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        damp_[0] = damp_[1] = 0.f;
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0) return;
        float* io[2] = {left, right};
        const float feedback = 0.9f * p_[1], mix = p_[3];
        for (int c = 0; c < 2; ++c) {
            std::vector<float>& line = line_[c];
            int at = at_;
            float damp = damp_[c];
            for (int i = 0; i < frames; ++i) {
                int from = at - delay_;
                if (from < 0) from += size_;
                const float echo = line[static_cast<std::size_t>(from)];
                damp += coeff_ * (echo - damp); // each repeat a little duller
                line[static_cast<std::size_t>(at)] = io[c][i] + damp * feedback;
                if (bus_) io[c][i] = mix * echo; // on a bus: the echoes alone
                else io[c][i] += mix * echo;
                if (++at >= size_) at = 0;
            }
            damp_[c] = std::fabs(damp) < 1e-20f ? 0.f : damp;
        }
        at_ = (at_ + frames) % size_;
    }

protected:
    void changed() override {
        const int steps = delaySteps(p_[0]);
        int frames = static_cast<int>(std::lround(steps * 15.0 / tempo_ * sr_));
        delay_ = frames < 1 ? 1 : (frames > size_ - 1 ? size_ - 1 : frames);
        const float hz = 600.f * std::pow(30.f, p_[2]); // 600 Hz to 18 kHz
        coeff_ = 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr_ ? 0.45f * sr_ : hz) / sr_);
    }

private:
    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, delay_ = 1;
    float tempo_ = 120.f, coeff_ = 1.f, damp_[2] = {};
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) {
        static const char* names[9] = {"", "1/16", "1/8", "1/8 DOTTED", "1/4", "", "1/4 DOTTED", "", "1/2"};
        std::snprintf(out, static_cast<std::size_t>(size), "%s", names[delaySteps(v)]);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Delay; }

} // namespace

const EffectInfo kDelayInfo = {
    "DELAY", 4, {"TIME", "FEEDBACK", "TONE", "MIX"}, {0.5f, 0.45f, 0.6f, 0.35f},
    "delay", EffectGroup::Delay, {6, 0, 0, 0}, reading, create,
};

} // namespace fx

int delaySteps(float time) {
    int index = static_cast<int>(fx::clamp01(time) * 6.f);
    return fx::kDelaySteps[index > 5 ? 5 : index];
}

} // namespace apotheca
