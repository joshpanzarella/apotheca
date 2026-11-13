#pragma once
// What the effects share. Each effect is one file in this folder, and holds
// everything about itself: its class, how its settings read, and its row for
// the table (an EffectInfo). effects.cpp gives the row its number and its
// place in the list.
//
// Adding an effect:
//   1. an EffectKind at the END of the list in effects.hpp (never in the middle);
//   2. a file here with its class and its `const EffectInfo kNameInfo`. The row
//      has `says`: a few plain words for the list a player picks from ("fewer
//      bits"). Without it the effect's line in that list has no words;
//   3. its `extern` line at the bottom of this file;
//   4. its row in the table in effects.cpp, and its place in kList there;
//   5. its checks in tests/effects_test.cpp (it is in `bench` by itself).
// A new TYPE of a family (PING to DELAY; SPRING, PLATE to REVERB) is a new
// effect, with three more things in its row:
//   - `family` ("DELAY", spelled as the family's other rows spell it) and
//     `type` ("PING"). Without them it is a line of its own in the list, not a
//     TYPE of DELAY. In kList it goes beside the family's other types; the
//     first of them is what the family goes in as, and its `says` is the
//     family's;
//   - its settings named as the other types name theirs (TIME, FEEDBACK, MIX):
//     changing TYPE carries a setting over by its NAME;
//   - `amount`, if a setting it shares is a choice whose choices differ (TIME:
//     how many steps), so a choice the other type does not have goes to the
//     nearest.
// effects_test checks every pair of every family by itself.
// A delay or a reverb (EffectGroup::Delay, ::Reverb) can sit on a send bus:
// its process() must look at bus_ and, when it is set, put out only what it
// adds (wet * MIX) and none of the sound that came in. effects_test fails one
// that does not.
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
    void setOnBus(bool on) override {
        bus_ = on;
        changed();
    }

protected:
    virtual void changed() {}
    float p_[kEffectParams] = {};
    float sr_ = 48000.f;
    bool bus_ = false; // on a send bus: wet only, MIX is its level
};

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
extern const EffectInfo kTapeInfo;   // tape.cpp
extern const EffectInfo kHallInfo;   // hall.cpp
extern const EffectInfo kChorusInfo; // chorus.cpp
extern const EffectInfo kCompInfo;   // comp.cpp
extern const EffectInfo kSaturateInfo; // saturate.cpp
extern const EffectInfo kDuckInfo;   // duck.cpp

// For the tests (tests/effects_test.cpp); nothing in the program uses these.
// SATURATE with its 2x stage left out, not prepared: to measure what the stage buys.
Effect* makeSaturatePlain();
// SATURATE's curves by themselves: what comes out for `x` going in (after DRIVE, before the level is evened out). 0 TAPE, 1 TUBE, 2 FOLD, 3 HARD.
float saturateCurve(int type, float x);
// What TAPE's FEEDBACK is multiplied by once the loop has been left alone, for a FEEDBACK setting (0 to 1): exactly 1 when it reads 90% or under.
float tapeTire(float setting);
// One sample of TAPE's multiplier on its way to `to` (down gently, up at once): it lands exactly.
float tapeTireStep(float tire, float to, float sampleRate);

} // namespace fx
} // namespace apotheca
