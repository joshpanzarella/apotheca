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
//   TAPE     TIME (eight choices, triplets among them), FEEDBACK (to 120%),
//            AGE (wow, dulling and the ceiling together), MIX
//   HALL     SIZE, DECAY (0.3 to 20 seconds), TONE, MIX
//   SATURATE DRIVE, TYPE (tape / tube / fold / hard), TONE, MIX
//   COMP     THRESHOLD, RATIO (six choices), SPEED, MIX
//   CHORUS   RATE, DEPTH, TYPE (chorus / ensemble / vibrato / flange), MIX
//   DUCK     AMOUNT, RELEASE (20 ms to 1 second), SOURCE (a lane of the
//            first drum track, 1 to 8): it dips when that lane is hit
//   PING     TIME (TAPE's eight choices), FEEDBACK (to 90%), TONE, MIX: a clean
//            echo that bounces left, right, left
//   SPRING   DRIP (how strong the chirp is), DECAY (0.4 to 4 seconds), TONE, MIX
//   PLATE    PRE (0 to 100 ms), DECAY (0.3 to 20 seconds), TONE, MIX
//
// MIX at 0 is the sound untouched.
//
// FAMILIES AND TYPES. A player sees one effect called DELAY with a TYPE
// (CLEAN, TAPE, PING) and one called REVERB with a TYPE (ROOM, HALL, SPRING, PLATE). Underneath
// each type is its own effect, with its own number and its own word in a song
// file, as it always was: a family is only how the list is shown. An effect
// with no `family` in its row is a family of one and has no TYPE. A new type
// is a new effect (fx/unit.hpp) whose row names the family and the type.
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
enum class EffectKind { None, Filter, Drive, Delay, Reverb, Crush, Tape /* 6 */, Hall /* 7 */, Chorus /* 8 */, Comp /* 9 */, Saturate /* 10 */, Duck /* 11 */, Ping /* 12 */, Spring /* 13 */, Plate /* 14 */, Count };
constexpr int kEffectKinds = static_cast<int>(EffectKind::Count);
constexpr int kEffectParams = 4;

// What sort of thing it is, for the list a player picks from (the list is in
// groups). Nothing stores a group's number.
enum class EffectGroup { None, Filter, Delay, Reverb, Moving, Level, Dirt, Count };
const char* effectGroupName(EffectGroup group); // "FILTER", "MOVING"...; "" for None

struct EffectInfo {
    const char* name;                       // on the screen: "FILTER"
    int params;                             // how many settings it has, up to four
    const char* paramNames[kEffectParams];
    float defaults[kEffectParams];
    const char* word;                       // in a song file: "filter". One word, and it never changes once it ships
    EffectGroup group;
    int choices[kEffectParams];             // a setting that is a choice, not an amount: how many choices (0: an amount)
    // How a setting reads: "810 HZ", "1/8 DOT", "8 BITS". `all` is the
    // effect's settings together, for one whose reading depends on another
    // (the DJ filter's cutoff), or nullptr. nullptr here: every setting is a
    // percentage. Use effectParamText(), which knows that.
    void (*reading)(int index, float value, const float* all, char* out, int size);
    Effect* (*create)();                    // a new one. Allocates: not for the audio thread. Use makeEffect()
    // What it is in a few plain words, for the list a player picks from: "fewer bits".
    // For the first type of a family, a word for the whole family: "echoes".
    const char* says = "";
    // The family it is a type of, and what the type is called: "DELAY", "TAPE".
    // Both nullptr: an effect with no types (a family of one, called by its name).
    const char* family = nullptr;
    const char* type = nullptr;
    // A setting as a real amount (TIME: how many steps), so that a setting two
    // types share by name can be matched when their choices differ. Not a
    // number for a setting it does not know; nullptr: none are known.
    float (*amount)(int index, float value) = nullptr;
};
// The row for a number. Anything that is not an effect gets NONE's row.
const EffectInfo& effectInfo(EffectKind kind);

// Every effect in the order a player meets them: the families in the list's
// order and the types of each side by side. The number is not the order.
// Place 0 is NONE.
int effectListSize();
EffectKind effectAt(int place);     // None for a place off either end
int effectPlace(EffectKind kind);   // 0 (NONE's) for anything not in the list
// "delay" -> EffectKind::Delay. A word this version does not know is None.
EffectKind effectFromWord(const char* word);
// How many choices a setting has; 0 if it is an amount (or is not there).
int effectChoices(EffectKind kind, int index);
// How a setting reads: "810 HZ", "1/8 DOT", "8 BITS", "35%".
// `all` is the effect's settings together, for one whose reading depends on
// another; without it, each is read by itself.
void effectParamText(EffectKind kind, int index, float value, char* out, int size, const float* all = nullptr);

// What an effect shows while sound passes (Engine::effectMeter): the setting
// whose box on the FX screen draws it, or -1 for an effect that shows nothing.
// COMP: how many dB it is turning the sound down, on its THRESHOLD box.
// DUCK: how many dB it is dipping, on its AMOUNT box.
int effectMeterParam(EffectKind kind);
constexpr float kMeterFullDb = 24.f; // what fills the box's width

// ---- families and types ----
// The family's name: "DELAY" for DELAY and for TAPE; an effect with no types: its own name.
const char* effectFamily(EffectKind kind);
// What the type is called: "TAPE"; "" for an effect with no types.
const char* effectTypeName(EffectKind kind);
// The types of an effect's family, in the list's order. One: there is no TYPE to change.
int effectTypes(EffectKind kind);
EffectKind effectTypeAt(EffectKind kind, int index); // None off either end
int effectTypeIndex(EffectKind kind);                // which of them this one is
// The list a player picks from: one line a family, place 0 NONE. `bus` 0 or 1
// gives a bus's own short list (NONE and the one family it may hold); anything
// else the whole list. familyAt gives the family's first type; familyPlace
// takes any of its types (0 for one that is not in the list asked for).
int familyListSize(int bus = -1);
EffectKind familyAt(int place, int bus = -1);
int familyPlace(EffectKind kind, int bus = -1);
// A family in a few plain words: "fewer bits"; for one with types, its types: "echoes: clean, tape, ping".
// Never more than kFamilySaysMost letters, what the list of effects has room for: where the family's words
// and its types together are longer, its types alone ("room, hall, spring, plate").
constexpr int kFamilySaysMost = 28;
void effectFamilySays(EffectKind kind, char* out, int size);

// Changing TYPE keeps the settings the two types share BY NAME (TIME,
// FEEDBACK, MIX...), as values that READ the same where their scales differ:
// TAPE's FEEDBACK goes to 120% and DELAY's to 100%, and 48% stays 48%.
// Which of `from`'s settings a setting of `to` is carried from; -1: none (it starts at its default).
int effectCarryFrom(EffectKind from, EffectKind to, int toIndex);
// What that setting is in `to`, for `value` in `from`: the value that reads the
// same, or the nearest there is (a choice the other does not have: the nearest
// by `amount`). For a pair that is not carried, `to`'s default.
float effectCarryValue(EffectKind from, int fromIndex, EffectKind to, int toIndex, float value);
// All of them at once. `now` gets kEffectParams values: carried, or the default (on a bus, LEVEL's is full).
void effectCarry(EffectKind from, EffectKind to, const float* was, bool onBus, float* now);

// Which send bus an effect may sit on, by its group: 0 (ECHO) for a delay, 1
// (SPACE) for a reverb, -1 for anything else. A new delay or reverb is
// offered on its bus without being added anywhere.
int effectBus(EffectKind kind);
// The setting named MIX, which on a bus reads LEVEL; -1 if it has none.
int effectMixParam(EffectKind kind);
// A setting's usual value. On a bus, LEVEL (the MIX) starts at full: the
// tracks' send amounts say how much, so the bus gives all of it back.
float effectDefault(EffectKind kind, int index, bool onBus = false);
// The list a bus's player picks from: NONE (place 0), then the effects that
// may sit on that bus, in the order of the main list.
int busListSize(int bus);
EffectKind busEffectAt(int bus, int place); // None off either end
int busEffectPlace(int bus, EffectKind kind); // 0 for anything not in it

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
