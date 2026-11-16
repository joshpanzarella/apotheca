#pragma once
// FM-6: a six-operator FM synth, after the classic one of 1983, built on msfa
// (third_party/msfa: the FM core of Google's "music-synthesizer-for-android",
// Apache 2.0). It takes QMOD's place (owner, 2026-11-15). The plan, with what
// was built, what it costs and what is left: docs/plans/fm6-plan.md.
//
// FM is sine waves ("operators") bending each other's pitch very fast. Six
// operators, wired one of 32 ways (the ALGORITHM): an operator that is heard
// is a "carrier", one that only bends another a "modulator". Each has its own
// envelope of four rates and four levels, its own tuning (a ratio of the key,
// or a fixed pitch) and its own level. On top: a pitch envelope, one LFO for
// the synth, and feedback on one operator.
//
// A sound is an Fm6Patch: the original's 155 voice parameters in its own
// order and numbering (0 to 125 the operators, OP6 first; 126 to 144 the
// voice's), less the name, plus which operators are on. So a sound from any
// bank of the original goes straight in, setting for setting. The 160 built-in
// sounds are five factory banks, ROM 1A then 1B, 2B, 3B and 4B, each its own
// folder (engine/src/fm6/roms.hpp, made by tools/fm6_rom.py; appended). The macros bend what is played; they never change the patch.

#include <cstdint>
#include <memory>

#include "apotheca/instrument.hpp"

namespace apotheca {

namespace fm6 {

// An operator's 21 settings, in the original's order.
enum OpSetting {
    OpR1, OpR2, OpR3, OpR4,     // 0..99: the envelope's rates (R4 is the release)
    OpL1, OpL2, OpL3, OpL4,     // 0..99: its levels (L3 is held while the key is; L4 is where the release goes)
    OpBreakPoint,               // 0..99: the key level scaling starts from (39 is C3)
    OpLeftDepth, OpRightDepth,  // 0..99
    OpLeftCurve, OpRightCurve,  // 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN
    OpRateScaling,              // 0..7: higher keys, faster envelope
    OpAms,                      // 0..3: how much the LFO moves its level
    OpVelocity,                 // 0..7: how much the velocity moves its level
    OpLevel,                    // 0..99: OUTPUT LEVEL
    OpMode,                     // 0 RATIO, 1 FIXED
    OpCoarse,                   // 0..31: the ratio (0 is 0.5), or the fixed pitch's decade
    OpFine,                     // 0..99
    OpDetune,                   // 0..14: 7 in tune
    kOpSettings
};
static_assert(kOpSettings == 21, "the original's 21 a operator");
// Operator `op` (1..6, OP1 is the one usually heard) setting `s`. OP6 comes first, as in the original.
constexpr int opSetting(int op, int s) { return (6 - op) * kOpSettings + s; }

// The voice's own settings, then what is not in the original.
enum VoiceSetting {
    PitchR1 = 6 * kOpSettings, PitchR2, PitchR3, PitchR4, // 0..99: the pitch envelope
    PitchL1, PitchL2, PitchL3, PitchL4,                   // 0..99: 50 is the key's pitch
    Algorithm,   // 0..31: shown 1..32
    Feedback,    // 0..7
    KeySync,     // 0 OFF, 1 ON: a key played again starts its operators' waves from the top
    LfoSpeed,    // 0..99
    LfoDelay,    // 0..99
    LfoPmd,      // 0..99: the LFO on the pitch
    LfoAmd,      // 0..99: the LFO on the operators' levels (each by its AMS)
    LfoSync,     // 0 OFF, 1 ON: the LFO starts again at a key
    LfoWave,     // 0 TRIANGLE, 1 SAW DOWN, 2 SAW UP, 3 SQUARE, 4 SINE, 5 SAMPLE AND HOLD
    Pms,         // 0..7: how much the LFO moves the pitch
    Transpose,   // 0..48: 24 is none, a semitone a step
    OpsOn,       // 0..63: which operators are on, bit 6-n OP n (bit 5 OP1 .. bit 0 OP6; 63: all). Not in the original's bank
    kSettings
};
static_assert(Transpose == 144 && kSettings == 146, "ids are appended, never reordered");
constexpr int kPatchSettings = kSettings; // what a preset holds: all of them (no track's own yet)
int settingMin(int id);
int settingMax(int id);

// The macros (docs/plans/fm6-plan.md, section 3). Each at 0.5 is the patch as it is.
enum Macro {
    MacroBright,   // the modulators' levels: down to none (plain sines), up to 12 dB more
    MacroFeedback, // FEEDBACK, smoothly between its steps: down to none, up to 7
    MacroAttack,   // every operator's first rate (R1): up is slower, down faster
    MacroDecay,    // R2 and R3: up is longer
    MacroRelease,  // R4: up is longer
    MacroDetune,   // down takes the patch's own DETUNE out, up spreads the operators apart
    MacroLfo,      // the LFO's depths (PMD and AMD): down to none, up to full
    MacroPitchEnv, // the pitch envelope's depth: down to none, up to twice
    kMacros
};
static_assert(kMacros == kSynthMacros, "every synth has the same number of macros");

// ---- the choices made (the plan, section 2) ---------------------------------------------------
constexpr int kVoices = 16;           // as the original. What only sounds costs: see the plan's costs
constexpr int kTailVoices = kVoices;  // voices let go by allNotesOff ring out here, beside the 16
constexpr int kRateBendMost = 24;     // ATTACK, DECAY, RELEASE at an end: a rate 24 steps of 64 off (64 times)
constexpr double kBrightMostDb = 12.0;  // BRIGHT at full: the modulators this much up
constexpr double kDetuneMostCents = 20.0; // DETUNE at full: an operator moved this far at most

} // namespace fm6

struct Fm6Patch {
    int value[fm6::kSettings] = {};
};

class Fm6 : public Instrument {
public:
    Fm6();
    ~Fm6() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);     // as the cartridge has it, its gaps closed up: "BRASS 1"
                                                  // (folder: its bank, "ROM 1A" to "ROM 4B")
    static const char* presetCategory(int index);
    static Fm6Patch presetPatch(int index);
    static Fm6Patch startPatch(); // what a synth is made with: OP1 alone, a plain sine (not a preset)

    void selectPreset(int index) override; // macros back to the middle
    void setSetting(int id, int value) override; // a key played after it hears it (the macros: at once)
    int setting(int id) const;
    void setMacro(int macro, float value) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void render(float* left, float* right, int frames) override;

    // For the tests.
    int voiceNote(int voice) const; // -1: free
    bool voiceHeld(int voice) const;
    int voicesSounding() const;
    int tailsRinging() const;
    unsigned applied() const { return applied_; } // how many times the patch and macros were worked out

private:
    struct State;
    void apply();
    int noteOnVoice(int note, float velocity);
    std::unique_ptr<State> state_;
    unsigned applied_ = 0;
};

} // namespace apotheca
