#pragma once
// The volume envelope every synth here can use: attack, decay, sustain,
// release. One shape and one set of times, so a setting of 30 means the same
// on any instrument.
//
// The times, from a 0..100 setting:
//   attack    1 ms to 8 s
//   decay     2 ms to 20 s   (to fall by a factor of a thousand, 60 dB)
//   release   2 ms to 20 s   (the same)
//
// The attack is not a straight ramp. It heads for a point half as high again
// as the top and is stopped when it gets there, as an analog envelope does:
// most of the rise comes early, so a short attack is snappy, and it starts
// from wherever the envelope already is, so it does not click on a note
// played again. Decay and release are falls by a constant ratio.
//
// A synth keeps one `Shape` (worked out when a setting changes) and one
// `Envelope` per voice. No allocation, nothing to wait for: fine on the
// audio thread.

#include <cmath>

namespace apotheca {
namespace envelope {

inline float held(float v) { return v < 0.f ? 0.f : (v > 100.f ? 100.f : v); }
inline float attackSeconds(float setting) { return 0.001f * std::pow(8000.f, held(setting) / 100.f); }
inline float fallSeconds(float setting) { return 0.002f * std::pow(10000.f, held(setting) / 100.f); }

constexpr float kAttackAim = 1.5f; // where the attack heads for; it is stopped at 1
constexpr float kSilent = 0.0001f; // 80 dB down: the voice is finished

struct Shape {
    float attackCoef = 1.f, decayCoef = 0.f, releaseCoef = 0.f, sustain = 1.f;
    // Times in seconds; sustain 0..1.
    void set(float attack, float decay, float sustainLevel, float release, float sampleRate) {
        // From 0, heading for kAttackAim, to arrive at 1 in `attack` seconds.
        const float toArrive = std::log(kAttackAim / (kAttackAim - 1.f));
        attackCoef = 1.f - std::exp(-toArrive / (attack * sampleRate));
        decayCoef = std::exp(-6.9078f / (decay * sampleRate));
        releaseCoef = std::exp(-6.9078f / (release * sampleRate));
        sustain = sustainLevel < 0.f ? 0.f : (sustainLevel > 1.f ? 1.f : sustainLevel);
    }
    // From the usual 0..100 settings.
    void setFrom(float attack, float decay, float sustainPercent, float release, float sampleRate) {
        set(attackSeconds(attack), fallSeconds(decay), sustainPercent / 100.f, fallSeconds(release), sampleRate);
    }
};

struct Envelope {
    enum Stage { Idle, Attack, Decay, Release };
    Stage stage = Idle;
    float level = 0.f;

    void on() { stage = Attack; }          // from wherever it is
    void off() { if (stage != Idle) stage = Release; }
    void stop() { stage = Idle; level = 0.f; }
    bool idle() const { return stage == Idle; }

    // The next sample's level.
    float next(const Shape& s) {
        if (stage == Attack) {
            level += (kAttackAim - level) * s.attackCoef;
            if (level >= 1.f) level = 1.f, stage = Decay;
        } else if (stage == Decay) {
            level = s.sustain + (level - s.sustain) * s.decayCoef;
        } else if (stage == Release) {
            level *= s.releaseCoef;
            if (level < kSilent) level = 0.f, stage = Idle;
        }
        return level;
    }
};

} // namespace envelope
} // namespace apotheca
