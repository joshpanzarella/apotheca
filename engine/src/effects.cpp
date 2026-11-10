#include "apotheca/effects.hpp"

#include <cmath>
#include <vector>

namespace apotheca {

namespace {

float clamp01(float v) { return v != v ? 0.f : (v < 0.f ? 0.f : (v > 1.f ? 1.f : v)); } // NaN goes to 0

// What every one of them shares: four settings, each 0..1.
class Unit : public Effect {
public:
    explicit Unit(EffectKind kind) {
        const EffectInfo& info = effectInfo(kind);
        for (int i = 0; i < kEffectParams; ++i) p_[i] = info.defaults[i];
    }
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        changed();
    }
    void setParam(int index, float value) override {
        if (index < 0 || index >= kEffectParams) return;
        p_[index] = clamp01(value);
        changed();
    }

protected:
    virtual void changed() {}
    float p_[kEffectParams] = {};
    float sr_ = 48000.f;
};

// ---- FILTER: a state-variable filter, 12 dB per octave ------------------------------
class Filter : public Unit {
public:
    Filter() : Unit(EffectKind::Filter) {}
    void reset() override { for (auto& s : state_) s[0] = s[1] = 0.f; }
    void process(float* left, float* right, int frames) override {
        float* io[2] = {left, right};
        const int mode = p_[2] < 1.f / 3.f ? 0 : (p_[2] < 2.f / 3.f ? 1 : 2);
        const float mix = p_[3];
        for (int c = 0; c < 2; ++c) {
            float ic1 = state_[c][0], ic2 = state_[c][1];
            for (int i = 0; i < frames; ++i) {
                const float in = io[c][i];
                const float v3 = in - ic2;
                const float v1 = a1_ * ic1 + a2_ * v3;
                const float v2 = ic2 + a2_ * ic1 + a3_ * v3;
                ic1 = 2.f * v1 - ic1;
                ic2 = 2.f * v2 - ic2;
                const float out = mode == 0 ? v2 : (mode == 1 ? v1 * k_ : in - k_ * v1 - v2);
                io[c][i] = in + mix * (out - in);
            }
            // Keep a silent filter from grinding on vanishing numbers.
            state_[c][0] = std::fabs(ic1) < 1e-20f ? 0.f : ic1;
            state_[c][1] = std::fabs(ic2) < 1e-20f ? 0.f : ic2;
        }
    }

protected:
    void changed() override {
        float hz = 30.f * std::pow(2.f, p_[0] * 9.5f); // 30 Hz to about 21 kHz
        if (hz > 0.45f * sr_) hz = 0.45f * sr_;
        const float g = std::tan(3.14159265f * hz / sr_);
        k_ = 1.f / (0.5f + 11.5f * p_[1] * p_[1]); // Q from 0.5 to 12
        a1_ = 1.f / (1.f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

private:
    float state_[2][2] = {};
    float k_ = 2.f, a1_ = 0.f, a2_ = 0.f, a3_ = 0.f;
};

// ---- DRIVE: a soft clipper with a tone control after it -------------------------------
class Drive : public Unit {
public:
    Drive() : Unit(EffectKind::Drive) {}
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

// ---- DELAY: echoes in time with the song -------------------------------------------------
constexpr int kDelaySteps[6] = {1, 2, 3, 4, 6, 8};

class Delay : public Unit {
public:
    Delay() : Unit(EffectKind::Delay) {}
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
                io[c][i] += mix * echo;
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

// ---- REVERB: four feedback delays and two smearing stages a side ---------------------------
class Reverb : public Unit {
public:
    Reverb() : Unit(EffectKind::Reverb) {}
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
                io[c][i] = in + mix * (wet - in * 0.5f * mix); // the dry sound eases back as the room comes up
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

// ---- CRUSH: fewer bits, fewer samples ---------------------------------------------------------
class Crush : public Unit {
public:
    Crush() : Unit(EffectKind::Crush) {}
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

} // namespace

int delaySteps(float time) {
    int index = static_cast<int>(clamp01(time) * 6.f);
    return kDelaySteps[index > 5 ? 5 : index];
}

const EffectInfo& effectInfo(EffectKind kind) {
    static const EffectInfo info[kEffectKinds] = {
        {"NONE", 0, {"", "", "", ""}, {0.f, 0.f, 0.f, 0.f}},
        {"FILTER", 4, {"CUTOFF", "RESONANCE", "TYPE", "MIX"}, {0.6f, 0.2f, 0.f, 1.f}},
        {"DRIVE", 4, {"DRIVE", "TONE", "MIX", "OUTPUT"}, {0.35f, 0.7f, 1.f, 0.5f}},
        {"DELAY", 4, {"TIME", "FEEDBACK", "TONE", "MIX"}, {0.5f, 0.45f, 0.6f, 0.35f}},
        {"REVERB", 3, {"SIZE", "DAMPING", "MIX", ""}, {0.6f, 0.5f, 0.3f, 0.f}},
        {"CRUSH", 3, {"BITS", "RATE", "MIX", ""}, {0.5f, 0.3f, 1.f, 0.f}},
    };
    const int index = static_cast<int>(kind);
    return info[index < 0 || index >= kEffectKinds ? 0 : index];
}

std::unique_ptr<Effect> makeEffect(EffectKind kind, float sampleRate, float tempo) {
    std::unique_ptr<Effect> effect;
    switch (kind) {
    case EffectKind::Filter: effect.reset(new Filter); break;
    case EffectKind::Drive: effect.reset(new Drive); break;
    case EffectKind::Delay: effect.reset(new Delay); break;
    case EffectKind::Reverb: effect.reset(new Reverb); break;
    case EffectKind::Crush: effect.reset(new Crush); break;
    default: return nullptr;
    }
    effect->prepare(sampleRate);
    effect->setTempo(tempo);
    return effect;
}

} // namespace apotheca
