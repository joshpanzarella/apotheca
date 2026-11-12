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
//   CHORUS   RATE, DEPTH, TYPE (chorus / ensemble / vibrato / flange), MIX
//
// MIX at 0 is the sound untouched.
//
// ONE TABLE. Everything the rest of the program needs to know about an
// effect is in its EffectInfo: the screen, the song file and the engine read
// it from there and spell no effect out themselves. Each effect is one file
// in engine/src/fx/, which holds its row; effects.cpp gives the row its
// number and its place in the list. How to add one: engine/src/fx/unit.hpp.

#include <memory>

#include "apotheca/effect.hpp"

namespace apotheca {

// An effect's number: what a song in memory and a command call it. NEVER
// reordered, never reused: new ones go at the end, before Count.
enum class EffectKind {
    None,
    Filter,
    Drive,
    Delay,
    Reverb,
    Crush,
    // Tape = 6,  (kept for TAPE: built on another branch)
    // Hall = 7,  (kept for HALL: likewise)
    Chorus = 8,
    // Comp = 9,
    // Saturate = 10,
    // Duck = 11,
    Count
};
constexpr int kEffectKinds = static_cast<int>(EffectKind::Count);
constexpr int kEffectParams = 4;

// What sort of thing it is, for the list a player picks from (the list is in
// groups, and a whole group can be stepped over at once).
enum class EffectGroup { None, Filter, Drive, Delay, Reverb, Moving, Level, Dirt, Count };
const char* effectGroupName(EffectGroup group); // "FILTER", "MOVING"...; "" for None

struct EffectInfo {
    const char* name;                       // on the screen: "FILTER"
    int params;                             // how many settings it has, up to four
    const char* paramNames[kEffectParams];
    float defaults[kEffectParams];
    const char* word;                       // in a song file: "filter". One word, and it never changes once it ships
    EffectGroup group;
    int choices[kEffectParams];             // a setting that is a choice, not an amount: how many choices (0: an amount)
    // How a setting reads: "810 HZ", "1/8 DOTTED", "8 BITS". `all` is the
    // effect's settings together, for one whose reading depends on another
    // (the DJ filter's cutoff), or nullptr. nullptr here: every setting is a
    // percentage. Use effectParamText(), which knows that.
    void (*reading)(int index, float value, const float* all, char* out, int size);
    Effect* (*create)();                    // a new one. Allocates: not for the audio thread. Use makeEffect()
};
// The row for a number. Anything that is not an effect gets NONE's row.
const EffectInfo& effectInfo(EffectKind kind);
// Is this number an effect this version has? (NONE is not; nor is a number
// kept for an effect that is not built yet.)
inline bool effectExists(EffectKind kind) { return effectInfo(kind).create != nullptr; }

// The list a player picks from, in its own order: the number is not the
// order. Place 0 is NONE.
int effectListSize();
EffectKind effectAt(int place);     // None for a place off either end
int effectPlace(EffectKind kind);   // 0 (NONE's) for anything not in the list
// "delay" -> EffectKind::Delay. A word this version does not know is None.
EffectKind effectFromWord(const char* word);
// How many choices a setting has; 0 if it is an amount (or is not there).
int effectChoices(EffectKind kind, int index);
// How a setting reads: "810 HZ", "1/8 DOTTED", "8 BITS", "35%".
// `all` is the effect's settings together, for one whose reading depends on
// another; without it, each is read by itself.
void effectParamText(EffectKind kind, int index, float value, char* out, int size, const float* all = nullptr);

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
