#pragma once
// What the effects share. Each effect is one file in this folder, and holds
// everything about itself: its class, how its settings read, and its row for
// the table (an EffectInfo). effects.cpp gives the row its number and its
// place in the list.
//
// Adding an effect:
//   1. an EffectKind at the END of the list in effects.hpp (never in the middle);
//   2. a file here with its class and its `const EffectInfo kNameInfo`. The row
//      names its `family` (EffectFamily: FILTER, DRIVE, CRUSH, MOD, DELAY,
//      REVERB, LEVEL) and, where its name is not what the type is called,
//      its `type` ("CLEAN" for DELAY). Every effect is a type of a family;
//   3. its `extern` line at the bottom of this file;
//   4. its row in the table in effects.cpp, and its place in kList there:
//      beside its family's other types (the first of them is what the family
//      goes in as, and its `says` is the family's words);
//   5. its checks in tests/effects_test.cpp (it is in `bench` by itself).
// A new TYPE (COMB to FILTER, FUZZ to DRIVE, BLOOM to REVERB) is just that.
// The list a player picks from names it on its family's line by itself, and
// changing TYPE carries a setting over by its NAME, so:
//   - name its settings as the other types name theirs (TIME, FEEDBACK, MIX);
//     a type that shares no name with another starts at its usual values;
//   - give it `amount`, if a setting it shares is a choice whose choices
//     differ (TIME: how many steps), so a choice the other type does not have
//     goes to the nearest.
// effects_test checks every pair of every family by itself.
// A DELAY or a REVERB (EffectFamily::Delay, ::Reverb) can sit on a send bus:
// its process() must look at bus_ and, when it is set, put out only what it
// adds (wet * MIX) and none of the sound that came in. effects_test fails one
// that does not.
// The build picks the file up by itself (CMakeLists.txt and
// scripts/build-handheld.sh both take every .cpp in this folder).

#include <cmath>
#include <cstdint>
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
extern const EffectInfo kPingInfo;   // ping.cpp
extern const EffectInfo kSpringInfo; // spring.cpp
extern const EffectInfo kPlateInfo;  // plate.cpp
extern const EffectInfo kCombInfo;   // comb.cpp
extern const EffectInfo kVowelInfo;  // vowel.cpp
extern const EffectInfo kEqInfo;     // eq.cpp
extern const EffectInfo kFoldInfo;   // fold.cpp
extern const EffectInfo kFuzzInfo;   // fuzz.cpp
extern const EffectInfo kRingInfo;   // ring.cpp
extern const EffectInfo kWearInfo;   // wear.cpp
extern const EffectInfo kLimiterInfo; // limiter.cpp
extern const EffectInfo kSnapInfo;   // snap.cpp
extern const EffectInfo kFlangerInfo; // flanger.cpp
extern const EffectInfo kEnsembleInfo; // ensemble.cpp
extern const EffectInfo kPhaserInfo; // phaser.cpp
extern const EffectInfo kTremoloInfo; // tremolo.cpp
extern const EffectInfo kDubInfo;    // dub.cpp
extern const EffectInfo kReverseInfo; // reverse.cpp
extern const EffectInfo kStutterInfo; // stutter.cpp

// For the tests (tests/effects_test.cpp); nothing in the program uses these.
// SATURATE with its 2x stage left out, not prepared: to measure what the stage buys.
Effect* makeSaturatePlain();
// SATURATE's curves by themselves: what comes out for `x` going in (after DRIVE, before the level is evened out). 0 TAPE, 1 TUBE, 2 FOLD, 3 HARD.
float saturateCurve(int type, float x);
// What TAPE's FEEDBACK is multiplied by once the loop has been left alone, for a FEEDBACK setting (0 to 1): exactly 1 when it reads 90% or under.
float tapeTire(float setting);
// One sample of TAPE's multiplier on its way to `to` (down gently, up at once): it lands exactly.
float tapeTireStep(float tire, float to, float sampleRate);
// The note COMB rings at for a PITCH setting, in hertz.
double combHz(float pitch);
// FOLD's folder by itself (the rounded triangle), for `x` after DRIVE and SYMMETRY.
float foldCurve(float x);
// FUZZ's clip by itself, past a gate's step `gate`, for `x` after FUZZ.
float fuzzCurve(float x, float gate);
// RING's carrier: 0 SINE, 1 TRIANGLE, 2 SQUARE, at `at` of its turn (0 to 1), `dt` a sample's share of a turn.
float ringCarrier(int wave, float at, float dt);
// FLANGER's delay in ms for DEPTH and RATE settings, at `at` of the sweep's turn (0: its start, the longest).
float flangerDelayMs(float depth, float rate, double at);
// TREMOLO's wave, 0 to 1, before its jumps are smoothed: 0 SINE, 1 SQUARE, 2 RAMP, 3 RANDOM, at `at` of turn `turn` (since it woke).
float tremoloWave(int shape, double at, long turn);
// DUB's band centre in hertz for a TONE setting, and what its FEEDBACK is multiplied by once left alone (1, or what brings it to 85%).
float dubToneHz(float tone);
float dubTireOf(float setting);
// STUTTER's roll for a place (0 to 1): it stutters there when this is under CHANCE.
float stutterRoll(std::uint32_t place);

} // namespace fx
} // namespace apotheca
