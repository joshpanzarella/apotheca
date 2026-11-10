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

// Drive: pushes a sound into a soft ceiling. The peaks flatten and the body
// comes up, which is what makes a drum sound loud at the same peak level.
static float drive(float x, float amount) { return std::tanh(amount * x); }

void DrumKit::buildBuiltIn(float sr) {
    // The kit is built to hit hard: fast pitch drops, a sharp click on the
    // front of each drum, and every sound driven before it is levelled.

    // Kick: a sine that drops from 250 Hz to 47 Hz in a few milliseconds,
    // driven hard, with a click in front.
    {
        Noise noise(1);
        OnePole clickTone(5000.f, sr);
        samples[0] = make(sr, 0.6f, 0.98f, [&](float t) {
            const float body = std::sin(kTwoPi * sweepPhase(t, 250.f, 47.f, 0.075f)) * decay(t, 0.7f);
            const float click = clickTone.lowpass(noise.next()) * decay(t, 0.006f)
                              + 0.6f * std::sin(kTwoPi * 2900.f * t) * decay(t, 0.004f);
            return drive(drive(body, 3.5f) + 0.5f * click, 1.4f);
        });
    }
    // Snare: two falling tones for the shell, bright noise for the wires,
    // and a crack of full-band noise on the front.
    {
        Noise noise(2);
        OnePole hp(1400.f, sr);
        samples[1] = make(sr, 0.34f, 0.98f, [&](float t) {
            const float shell = std::sin(kTwoPi * sweepPhase(t, 340.f, 188.f, 0.04f)) * decay(t, 0.13f)
                              + 0.5f * std::sin(kTwoPi * sweepPhase(t, 560.f, 290.f, 0.03f)) * decay(t, 0.08f);
            const float n = noise.next();
            const float wires = hp.highpass(n) * decay(t, 0.3f);
            const float crack = n * decay(t, 0.012f);
            return drive(0.8f * shell + 0.9f * wires + 0.9f * crack, 3.f);
        });
    }
    // Hats: metal and noise above 8 kHz, driven so they cut. Closed is a
    // tick, open rings.
    {
        Noise noise(3);
        OnePole hp(8000.f, sr);
        samples[2] = make(sr, 0.07f, 0.85f, [&](float t) {
            return drive(hp.highpass(0.6f * metal(t) + 0.6f * noise.next()) * decay(t, 0.05f), 2.5f);
        });
    }
    {
        Noise noise(4);
        OnePole hp(8000.f, sr);
        samples[3] = make(sr, 0.55f, 0.85f, [&](float t) {
            return drive(hp.highpass(0.6f * metal(t) + 0.6f * noise.next()) * decay(t, 0.55f), 2.5f);
        });
    }
    // Clap: four tight bursts of mid noise, then a wide tail.
    {
        Noise noise(5);
        OnePole hp(700.f, sr), lp(5500.f, sr);
        samples[4] = make(sr, 0.42f, 0.95f, [&](float t) {
            float env = decay(t > 0.036f ? t - 0.036f : 0.f, 0.36f);
            if (t < 0.036f) env = decay(std::fmod(t, 0.009f), 0.011f);
            return drive(lp.lowpass(hp.highpass(noise.next())) * env, 3.5f);
        });
    }
    // Tom: the kick's recipe, higher.
    {
        Noise noise(6);
        OnePole clickTone(4000.f, sr);
        samples[5] = make(sr, 0.45f, 0.95f, [&](float t) {
            const float body = std::sin(kTwoPi * sweepPhase(t, 300.f, 112.f, 0.07f)) * decay(t, 0.45f);
            return drive(drive(body, 2.5f) + 0.3f * clickTone.lowpass(noise.next()) * decay(t, 0.005f), 1.3f);
        });
    }
    // Rim: two high tones and a click, very short and very loud.
    {
        Noise noise(7);
        samples[6] = make(sr, 0.06f, 0.95f, [&](float t) {
            const float tones = std::sin(kTwoPi * 1720.f * t) + 0.7f * std::sin(kTwoPi * 487.f * t);
            return drive(tones * decay(t, 0.04f) + 0.8f * noise.next() * decay(t, 0.003f), 3.f);
        });
    }
    // Crash: the hat recipe, darker and long.
    {
        Noise noise(8);
        OnePole hp(3500.f, sr);
        samples[7] = make(sr, 1.8f, 0.85f, [&](float t) {
            const float wash = hp.highpass(0.5f * metal(t * 1.31f) + 0.7f * noise.next());
            return drive(wash * (decay(t, 1.7f) + 0.6f * decay(t, 0.05f)), 2.2f);
        });
    }
}

void DrumMachine::prepare(float sampleRate) {
    chokeStep_ = 1.f / (0.003f * sampleRate);
    reset();
}

void DrumMachine::setLaneLevel(int lane, float level) {
    if (lane < 0 || lane >= kDrumLanes || !(level >= 0.f)) return; // also drops NaN
    laneLevel_[lane] = level > 1.f ? 1.f : level;
}

void DrumMachine::reset() {
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        voices_[lane][0] = Voice();
        voices_[lane][1] = Voice();
        next_[lane] = 0;
    }
}

void DrumMachine::setLaneSample(int lane, const float* data, int length) {
    if (lane < 0 || lane >= kDrumLanes) return;
    own_[lane] = length > 0 ? data : nullptr;
    ownLength_[lane] = own_[lane] ? length : 0;
    voices_[lane][0] = Voice(); // they were reading the old sound
    voices_[lane][1] = Voice();
}

void DrumMachine::trigger(int lane, float velocity, float start) {
    if (!kit_ || lane < 0 || lane >= kDrumLanes) return;
    if (!(velocity > 0.f)) return; // also drops NaN
    if (velocity > 1.f) velocity = 1.f;
    const int length = own_[lane] ? ownLength_[lane] : static_cast<int>(kit_->samples[lane].size());
    if (length == 0) return;
    if (!(start > 0.f)) start = 0.f; // also NaN
    int from = static_cast<int>(start * static_cast<float>(length));
    if (from >= length) from = length - 1;

    Voice& fresh = voices_[lane][next_[lane]];
    Voice& old = voices_[lane][1 - next_[lane]];
    if (old.position >= 0) old.fadeStep = chokeStep_;
    fresh.position = from;
    fresh.gain = velocity;
    fresh.fade = 1.f;
    fresh.fadeStep = 0.f;
    fresh.rise = from > 0 ? 0.f : 1.f;
    fresh.riseStep = from > 0 ? chokeStep_ * 3.f : 0.f; // a millisecond
    next_[lane] = 1 - next_[lane];
}

void DrumMachine::render(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    if (kit_) {
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            const float* sample = own_[lane] ? own_[lane] : kit_->samples[lane].data();
            const int length = own_[lane] ? ownLength_[lane] : static_cast<int>(kit_->samples[lane].size());
            const float laneLevel = laneLevel_[lane];
            for (Voice& v : voices_[lane]) {
                if (v.position < 0) continue;
                for (int i = 0; i < frames; ++i) {
                    if (v.position >= length) { v.position = -1; break; }
                    left[i] += sample[v.position++] * v.gain * v.fade * v.rise * laneLevel;
                    if (v.rise < 1.f) {
                        v.rise += v.riseStep;
                        if (v.rise > 1.f) v.rise = 1.f;
                    }
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
