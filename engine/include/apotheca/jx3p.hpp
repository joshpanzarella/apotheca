#pragma once
// The JX-3P: a clone of Roland's 1983 six-voice synth, its PG-200 patch
// setting for setting. It will take Twin Analog's place (owner, 2026-11-13);
// until the plan's batch 6a / 8b it is built beside Twin, which is untouched.
// The plan, with every source and every guess: docs/plans/jx3p-plan.md.
//
// One voice: DCO-1 and DCO-2 -> SOURCE MIX -> a 4-pole VCF -> the VCA (ENV or
// GATE). Six voices summed -> HPF -> chorus. The envelope and the LFO are
// software on the real one and move once a 10 ms tick (engine/src/jx3p/env.hpp).
//
// BUILT SO FAR (batch 1, the skeleton): the 34 settings and 8 macros with
// their ranges, a start patch, six voices with the assigner and the steal,
// what allNotesOff puts back, seeded start phases, the tick, the ADSR, the
// sample-and-hold, the VCA with VELOCITY. Batch 2: both DCOs, every wave, TUNE,
// FINE, SYNC, METAL, NOISE, SOURCE MIX, ENV on the DCOs (engine/src/jx3p/dco.hpp).
// Every other setting is kept and read back, and does nothing yet.
//
// Every knob reads 0 to 10 on the PG-200; it is kept as 0..100, tenths of the
// dial. A switch is 0, 1 (, 2, 3) in the order written beside it.

#include <cstdint>
#include <memory>

#include "apotheca/instrument.hpp"

namespace apotheca {

namespace jx3p {

enum Setting {
    // The patch: ids 0..31, what a preset holds.
    Dco1Range,       // 0 16', 1 8', 2 4'
    Dco1Wave,        // 0 SAW, 1 PULSE, 2 SQUARE
    Dco1Lfo,         // 0 OFF, 1 ON
    Dco1Env,         // 0 OFF, 1 ON
    Dco2Range,       // 0 16', 1 8', 2 4'
    Dco2Wave,        // 0 SAW, 1 PULSE, 2 SQUARE, 3 NOISE
    Dco2CrossMod,    // 0 OFF, 1 SYNC, 2 METAL
    Dco2Tune,        // 0..100: 50 unison
    Dco2Fine,        // 0..100: 50 centre
    Dco2Lfo,         // 0 OFF, 1 ON
    Dco2Env,         // 0 OFF, 1 ON
    DcoLfoDepth,     // 0..100
    DcoEnvDepth,     // 0..100
    DcoEnvPolarity,  // 0 NORMAL, 1 INVERTED
    SourceMix,       // 0..100: all DCO-1 to all DCO-2
    Hpf,             // 0..100
    Cutoff,          // 0..100
    Resonance,       // 0..100
    VcfLfo,          // 0..100
    VcfEnv,          // 0..100
    VcfEnvPolarity,  // 0 NORMAL, 1 INVERTED
    KeyFollow,       // 0..100
    VcaMode,         // 0 ENV, 1 GATE
    VcaLevel,        // 0..100
    LfoWave,         // 0 SINE, 1 SQUARE, 2 RANDOM
    LfoDelay,        // 0..100
    LfoRate,         // 0..100
    Attack,          // 0..100
    Decay,           // 0..100
    Sustain,         // 0..100
    Release,         // 0..100
    Chorus,          // 0 OFF, 1 ON
    // The track's own, kept through a preset change (the plan, section 1).
    Velocity,        // 0 OFF (as the real one), 1 ON
    Brilliance,      // 0..100: 50 centre
    kSettings
};
constexpr int kPatchSettings = Velocity; // 32: what a preset writes and keeps; ids from here are the track's
static_assert(kPatchSettings == 32 && kSettings == 34, "ids are appended, never reordered");
int settingMin(int id);
int settingMax(int id);

enum Macro { MacroCutoff, MacroReso, MacroEnv, MacroTune, MacroMix, MacroLfo, MacroAttack, MacroRelease, kMacros };
static_assert(kMacros == kSynthMacros, "every synth has the same number of macros");

// ---- THE OWNER'S CALLS (plan, section 8), each taken at the plan's recommended answer -----------
// One place each. Those of later batches are named here so the list is whole.
constexpr int kVoices = 6;                  // 6, as the real one (8 costs about a third more)
constexpr int kNewTrackVelocity = 1;        // VELOCITY is a track value, ON for a new track
constexpr int kNewTrackBrilliance = 50;     // BRILLIANCE is a track value (per track, not per patch)
constexpr bool kTuneStepped = true;         // DCO-2 TUNE in semitone steps (batch 2 reads it)
constexpr double kTuneSemitonesPerDial = 2.4; // ...a step of the dial (10 tenths) is 2.4 semitones, rounded
constexpr bool kRoundToCounter = true;      // DCO pitch is the 8253's clock over a whole number
constexpr double kCounterClockHz = 5999840.0; // 440 x round(6 MHz / 440): A4 is 13636 divisions (SN p.6: about 6 MHz)
// A stolen voice retriggers from its level with no fade: that is Jx3p::noteOnVoice() in jx3p.cpp,
// the one place. (The other answer, a 2 ms fade of the old note first, is not built.)
constexpr bool kStealLetGoFirst = true;     // a steal takes the oldest voice whose key is up (ringing in
                                            // its release) before the oldest key still held; false: the
                                            // oldest sounding, held or not (the plan's first words)
constexpr int kKeyFollowFull = 98;          // KEY FOLLOW 9.8 is 100% (batch 3)
constexpr bool kBassLoss = true;            // the low end falls as resonance rises (batch 3)
constexpr bool kKeysOnTheSample = true;     // a key starts (and lets go) on its own sample; false: on the
                                            // 10 ms grid of one CPU loop, as the real one (up to 10 ms late)
constexpr bool kBbdHiss = false;            // the chorus's hiss is not copied (batch 5)
// Not an owner's call, a house rule's cost: Play, Stop and panic hand every sounding voice to one
// of these, where it rings out its release beside the six, which start again as from fresh (so
// Play from the top is the same performance however long the last notes ring). Twice six, so a
// Stop and a Play inside one release both fit; past that a voice stays where it is.
constexpr int kTailVoices = 2 * kVoices;
// Kept outside the engine: the name on the device (TWIN ANALOG) and `track twin` read as A1 (batch 6a).

} // namespace jx3p

struct Jx3pPatch {
    int value[jx3p::kSettings] = {};
};

class Jx3p : public Instrument {
public:
    Jx3p();
    ~Jx3p() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index);
    static Jx3pPatch presetPatch(int index); // ids 32 and 33 at a new track's values

    // A preset writes ids 0..31 only: VELOCITY and BRILLIANCE stay as the track has them.
    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    int setting(int id) const;
    void setMacro(int macro, float value) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void render(float* left, float* right, int frames) override;

    // For the tests: what a voice is doing now.
    int voiceNote(int voice) const;     // -1: silent
    float voiceEnv(int voice) const;    // the envelope's level, as worked out on the last tick
    float voiceVca(int voice) const;    // the VCA's CV after the hold
    double voiceHz(int voice) const;    // DCO-1's frequency now
    double voiceHz2(int voice) const;   // DCO-2's
    bool voiceHeld(int voice) const;
    int tailsRinging() const;           // voices handed over by allNotesOff still ringing out

private:
    struct State;
    void apply();
    int noteOnVoice(int note, float velocity);
    std::unique_ptr<State> state_;
};

} // namespace apotheca
