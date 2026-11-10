#include "apotheca/drums.hpp"

#include <cmath>
#include <cstdint>

namespace apotheca {

namespace {

constexpr float kTwoPi = 6.2831853f;

// Small repeatable noise source, so the kit is the same on every start.
struct Noise {
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed) {}
    float next() { // -1..1
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (2.f / 16777216.f) - 1.f;
    }
};

// One-pole highpass and lowpass, enough to shape noise.
struct OnePole {
    float coeff = 1.f, low = 0.f;
    OnePole(float hz, float sr) { coeff = 1.f - std::exp(-kTwoPi * hz / sr); }
    float lowpass(float x) { low += coeff * (x - low); return low; }
    float highpass(float x) { return x - lowpass(x); }
};

float decay(float t, float seconds) { return std::exp(-6.9077553f * t / seconds); } // -60 dB at `seconds`

// Six detuned square waves: the metallic part of a drum machine cymbal.
float metal(float t) {
    static const float hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
    float sum = 0.f;
    for (float f : hz) sum += std::fmod(t * f * 8.f, 1.f) < 0.5f ? 1.f : -1.f;
    return sum / 6.f;
}

// Scale to a fixed peak and fade the last 5 ms, so no sample ends on a click.
void finish(std::vector<float>& s, float sr, float peak) {
    float loudest = 0.f;
    for (float v : s) loudest = std::fabs(v) > loudest ? std::fabs(v) : loudest;
    const float scale = loudest > 0.f ? peak / loudest : 0.f;
    const int n = static_cast<int>(s.size());
    const int tail = static_cast<int>(0.005f * sr);
    for (int i = 0; i < n; ++i) {
        float g = scale;
        if (i >= n - tail) g *= static_cast<float>(n - 1 - i) / static_cast<float>(tail);
        s[i] *= g;
    }
}

template <typename F>
std::vector<float> make(float sr, float seconds, float peak, F sampleAt) {
    std::vector<float> s(static_cast<std::size_t>(seconds * sr));
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = sampleAt(static_cast<float>(i) / sr);
    finish(s, sr, peak);
    return s;
}

// A sine whose pitch falls from `from` to `to`; returns the phase at time t.
float sweepPhase(float t, float from, float to, float seconds) {
    // integral of to + (from - to) * exp(-t / tau)
    const float tau = seconds / 5.f;
    return to * t + (from - to) * tau * (1.f - std::exp(-t / tau));
}

} // namespace

const char* DrumKit::laneName(int lane) {
    static const char* names[kDrumLanes] = {"KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "CRASH"};
    return lane >= 0 && lane < kDrumLanes ? names[lane] : "";
}

void DrumKit::buildBuiltIn(float sr) {
    // Kick: a sine falling from 160 Hz to 46 Hz, with a short click on top.
    {
        Noise noise(1);
        OnePole click(3000.f, sr);
        samples[0] = make(sr, 0.45f, 0.95f, [&](float t) {
            const float body = std::sin(kTwoPi * sweepPhase(t, 160.f, 46.f, 0.12f)) * decay(t, 0.42f);
            return body + 0.25f * click.lowpass(noise.next()) * decay(t, 0.012f);
        });
    }
    // Snare: a short tone with bright noise over it.
    {
        Noise noise(2);
        OnePole hp(1800.f, sr);
        samples[1] = make(sr, 0.28f, 0.9f, [&](float t) {
            const float tone = std::sin(kTwoPi * sweepPhase(t, 240.f, 180.f, 0.05f)) * decay(t, 0.11f);
            return 0.6f * tone + 0.8f * hp.highpass(noise.next()) * decay(t, 0.2f);
        });
    }
    // Hats: metal and noise through a highpass. Closed is short, open rings.
    {
        Noise noise(3);
        OnePole hp(7000.f, sr);
        samples[2] = make(sr, 0.07f, 0.7f, [&](float t) {
            return hp.highpass(0.6f * metal(t) + 0.5f * noise.next()) * decay(t, 0.05f);
        });
    }
    {
        Noise noise(4);
        OnePole hp(7000.f, sr);
        samples[3] = make(sr, 0.5f, 0.7f, [&](float t) {
            return hp.highpass(0.6f * metal(t) + 0.5f * noise.next()) * decay(t, 0.45f);
        });
    }
    // Clap: three quick bursts of mid noise, then a tail.
    {
        Noise noise(5);
        OnePole hp(900.f, sr), lp(3500.f, sr);
        samples[4] = make(sr, 0.3f, 0.85f, [&](float t) {
            float env = decay(t > 0.03f ? t - 0.03f : 0.f, 0.22f);
            if (t < 0.03f) env = decay(std::fmod(t, 0.01f), 0.012f);
            return lp.lowpass(hp.highpass(noise.next())) * env;
        });
    }
    // Tom: like the kick, higher and shorter.
    samples[5] = make(sr, 0.35f, 0.9f, [&](float t) {
        return std::sin(kTwoPi * sweepPhase(t, 210.f, 120.f, 0.1f)) * decay(t, 0.3f);
    });
    // Rim: two high tones, very short.
    samples[6] = make(sr, 0.05f, 0.8f, [&](float t) {
        return (std::sin(kTwoPi * 1720.f * t) + 0.6f * std::sin(kTwoPi * 487.f * t)) * decay(t, 0.03f);
    });
    // Crash: the hat recipe, darker and long.
    {
        Noise noise(8);
        OnePole hp(4000.f, sr);
        samples[7] = make(sr, 1.4f, 0.7f, [&](float t) {
            return hp.highpass(0.5f * metal(t * 1.31f) + 0.6f * noise.next()) * decay(t, 1.3f);
        });
    }
}

void DrumMachine::prepare(float sampleRate) {
    chokeStep_ = 1.f / (0.003f * sampleRate);
    reset();
}

void DrumMachine::reset() {
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        voices_[lane][0] = Voice();
        voices_[lane][1] = Voice();
        next_[lane] = 0;
    }
}

void DrumMachine::trigger(int lane, float velocity) {
    if (!kit_ || lane < 0 || lane >= kDrumLanes) return;
    if (!(velocity > 0.f)) return; // also drops NaN
    if (velocity > 1.f) velocity = 1.f;
    if (kit_->samples[lane].empty()) return;

    Voice& fresh = voices_[lane][next_[lane]];
    Voice& old = voices_[lane][1 - next_[lane]];
    if (old.position >= 0) old.fadeStep = chokeStep_;
    fresh.position = 0;
    fresh.gain = velocity;
    fresh.fade = 1.f;
    fresh.fadeStep = 0.f;
    next_[lane] = 1 - next_[lane];
}

void DrumMachine::render(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    if (kit_) {
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            const std::vector<float>& sample = kit_->samples[lane];
            const int length = static_cast<int>(sample.size());
            for (Voice& v : voices_[lane]) {
                if (v.position < 0) continue;
                for (int i = 0; i < frames; ++i) {
                    if (v.position >= length) { v.position = -1; break; }
                    left[i] += sample[v.position++] * v.gain * v.fade;
                    if (v.fadeStep > 0.f) {
                        v.fade -= v.fadeStep;
                        if (v.fade <= 0.f) { v.position = -1; break; }
                    }
                }
            }
        }
    }
    for (int i = 0; i < frames; ++i) right[i] = left[i];
}

} // namespace apotheca
