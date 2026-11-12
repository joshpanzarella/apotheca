#pragma once
// What the effects share. Each effect is one file in this folder, and holds
// everything about itself: its class, how its settings read, and its row for
// the table (an EffectInfo). effects.cpp gives the row its number and its
// place in the list.
//
// Adding an effect:
//   1. an EffectKind at the END of the list in effects.hpp (never in the middle);
//   2. a file here with its class and its `const EffectInfo kNameInfo`;
//   3. its `extern` line at the bottom of this file;
//   4. its row in the table in effects.cpp, and its place in kList there;
//   5. its checks in tests/effects_test.cpp (it is in `bench` by itself).
// The build picks the file up by itself (CMakeLists.txt and
// scripts/build-handheld.sh both take every .cpp in this folder).

#include <cmath>
#include <cstdio>

#include "apotheca/effects.hpp"

namespace apotheca {
namespace fx {

inline float clamp01(float v) { return v != v ? 0.f : (v < 0.f ? 0.f : (v > 1.f ? 1.f : v)); } // NaN goes to 0

// Four settings, each 0..1, starting at the defaults in the effect's row.
// changed() is where an effect turns them into hertz, seconds or bits.
class Unit : public Effect {
public:
    explicit Unit(const EffectInfo& info) {
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

// A value on its way to where a setting was turned to, so that a turn is a
// slide and not a click. It goes most of the way in `seconds` and then
// arrives exactly, so a setting at rest costs nothing and MIX at 0 is 0.
//     begin(frames, part)   once a block; part = glidePart(frames, seconds, rate)
//     next()                once a sample
//     finish()              at the end of the block
struct Glide {
    float now = 0.f, to = 0.f, step = 0.f, end = 0.f;
    void set(float target) { to = target; }
    void snap() { now = end = to; step = 0.f; } // there at once: before the first sound, and on a reset
    void begin(int frames, float part) {
        end = now + (to - now) * part;
        if (std::fabs(to - end) < 1e-5f * (1.f + std::fabs(to))) end = to; // near enough: arrive
        step = (end - now) / static_cast<float>(frames);
    }
    float next() { return now += step; }
    void finish() { now = end; }
    bool still() const { return now == to; }
};
inline float glidePart(int frames, float seconds, float rate) { return 1.f - std::exp(-static_cast<float>(frames) / (seconds * rate)); }

// How a plain amount reads: "35%".
inline void readPercent(float value, char* out, int size) {
    std::snprintf(out, static_cast<std::size_t>(size), "%d%%", static_cast<int>(std::round(value * 100.f)));
}

// The rows, one from each file.
extern const EffectInfo kFilterInfo; // filter.cpp
extern const EffectInfo kDriveInfo;  // drive.cpp
extern const EffectInfo kDelayInfo;  // delay.cpp
extern const EffectInfo kReverbInfo; // reverb.cpp
extern const EffectInfo kCrushInfo;  // crush.cpp
extern const EffectInfo kChorusInfo; // chorus.cpp
extern const EffectInfo kCompInfo;   // comp.cpp

} // namespace fx
} // namespace apotheca
