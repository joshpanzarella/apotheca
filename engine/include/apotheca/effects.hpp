#pragma once
// The effects a slot can hold. Every setting is 0..1, so the screen, the
// song file and the engine all speak the same numbers; each effect turns
// its own into hertz, seconds or bits.
//
//   FILTER   CUTOFF, RESONANCE, TYPE (low / band / high), MIX
//   DRIVE    DRIVE, TONE, MIX, OUTPUT
//   DELAY    TIME (a 16th, an 8th, a dotted 8th, a quarter, a dotted
//            quarter, a half: it follows the tempo), FEEDBACK, TONE, MIX
//   REVERB   SIZE, DAMPING, MIX
//   CRUSH    BITS, RATE, MIX
//
// MIX at 0 is the sound untouched.

#include <memory>

#include "apotheca/effect.hpp"

namespace apotheca {

enum class EffectKind { None, Filter, Drive, Delay, Reverb, Crush, Count };
constexpr int kEffectKinds = static_cast<int>(EffectKind::Count);
constexpr int kEffectParams = 4;

struct EffectInfo {
    const char* name;
    int params;
    const char* paramNames[kEffectParams];
    float defaults[kEffectParams];
};
const EffectInfo& effectInfo(EffectKind kind);

// A new one, prepared and set to its defaults. nullptr for None. Allocates:
// not for the audio thread.
std::unique_ptr<Effect> makeEffect(EffectKind kind, float sampleRate, float tempo);

// The filter's TYPE setting as one of five. They are in this order so that
// songs kept when there were three (low, band, high, a third of the range
// each) still open as what they were.
enum class FilterType { LowPass2, LowPass4, BandPass, Dj, HighPass };
inline FilterType filterType(float v) {
    const int i = static_cast<int>((v < 0.f ? 0.f : v) * 5.f);
    return static_cast<FilterType>(i > 4 ? 4 : i);
}
// The DJ filter is one knob for two filters: below the middle it closes down
// from the top (a low-pass), above the middle it clears out the bottom (a
// high-pass), and in the middle it does nothing at all. This says which,
// and where: -1 low-pass, 0 nothing, +1 high-pass; `hz` is the corner.
int djFilter(float cutoffSetting, float& hz);

// The delay's TIME setting as a number of steps (sixteenth notes).
int delaySteps(float time);

} // namespace apotheca
