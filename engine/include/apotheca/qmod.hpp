#pragma once
// QMOD: an FM synth that is rolled, not programmed.
//
// FM is sine waves ("operators") bending each other's pitch very fast; which
// one bends which is the "wiring", and it decides the sound. Programming
// that by hand, operator by operator, is slow work on a handheld. QMOD does
// not ask for it: a number (the SOUND) GENERATES a whole wiring of eight
// operators, with their tunings and levels, from one of thirty-two families.
// A second wiring, a cousin of the first, is generated from the VARIATION
// number, and ENTANGLE morphs from one to the other.
//
// The generator is Entanglement's, from the Shapetaker plugin
// (third_party/shapetaker). The voice here is QMOD's own and lighter: every
// operator is one sine, each wiring is worked out once a sample, and both
// are only worked out while ENTANGLE is between its ends.
//
//   TENSION   how hard the operators bend each other: mellow to metallic
//   EVOLVE    how long that bending lasts in each note: a quick "pluck" of
//             brightness, or a sound that stays bright
//   AFFINITY  how much the generator likes feedback (rougher, noisier)
//   SYMMETRY  how simple the tunings are: plain harmonics, or bell-like ones
//
// The volume envelope is the shared one (envelope.hpp).

#include <memory>

#include "instrument.hpp"

namespace apotheca {

namespace qmod {

enum Setting {
    Sound,     // 1..9999: the number the first wiring is generated from
    Variation, // 0..99: which cousin of it the second wiring is
    Mutation,  // 0..100: how far a cousin strays from the first
    Entangle,  // 0..100: all the first wiring to all the second
    Tension,   // 0..100
    Evolve,    // 0..100
    Affinity,  // 0..100
    Symmetry,  // 0..100
    Family,    // 0: whichever the SOUND number gives; 1..32: this one
    Octave,    // -2..2
    Width,     // 0..100: mono to wide
    Attack,    // 0..100 (envelope.hpp)
    Decay,     // 0..100
    Sustain,   // 0..100 %
    Release,   // 0..100
    Level,     // -24..6 dB
    kSettings
};
int settingMin(int id);
int settingMax(int id);
const char* familyName(int family); // 1..32: "ORBIT", "FURNACE"...; 0: ""

enum Macro { MacroEntangle, MacroTension, MacroEvolve, MacroVariation, MacroAttack, MacroRelease, MacroWidth, MacroOctave, kMacros };
static_assert(kMacros == kSynthMacros, "every synth has the same number of macros");

constexpr int kVoices = 8;

} // namespace qmod

struct QmodPatch {
    int value[qmod::kSettings] = {};
};

class Qmod : public Instrument {
public:
    Qmod();
    ~Qmod() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index);
    static QmodPatch presetPatch(int index);
    // Which family a patch's first wiring came out as (1..32), for the screen.
    static int familyOf(const QmodPatch& patch);

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void render(float* left, float* right, int frames) override;

private:
    struct State;
    void apply();
    std::unique_ptr<State> state_;
};

} // namespace apotheca
