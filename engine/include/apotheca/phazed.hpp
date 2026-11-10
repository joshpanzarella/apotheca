#pragma once
// Phazed: Apotheca's phase distortion synth.
//
// "Phazed" is the only name a player ever sees. Its engine is the code in
// third_party/easycz, copied unchanged from joshpanzarella/schwung-easycz;
// that folder keeps its own names and licences, and nothing in it is shown
// on screen without passing through this file.
//
// The engine makes sound at 44100 Hz, the rate its filters were designed for.
// Apotheca runs at 48000 Hz, so the output is converted on the way out
// (rate_converter.hpp). At 44100 Hz that step is skipped.
//
// A sound is a PhazedPatch: every setting as a plain number. On top of the
// patch sit eight macros, each 0..1 with 0.5 meaning "as the patch says".
// The macros never change the patch; they bend what is played.

#include <memory>

namespace apotheca {

namespace phazed {

// ---- the settings in a patch, by number --------------------------------------
enum Global {
    LineSelect, // 0..3: line 1, line 2, 1+1', 1+2'
    Octave,     // -1..1
    DetuneSign, // 0 down, 1 up
    DetuneOctave, // 0..3
    DetuneNote,   // 0..11
    DetuneFine,   // 0..60
    Mod,        // 0 off, 1 ring, 2 noise
    VibratoWave,  // 0 triangle, 1 saw up, 2 saw down, 3 square
    VibratoDelay, // 0..99
    VibratoRate,  // 0..99
    VibratoDepth, // 0..99
    ChorusOn,   // 0..1
    Level,      // -24..6 dB
    kGlobals
};
enum LineSetting { Wave1, Wave2, AmpKeyFollow, WaveKeyFollow, kLineHead }; // waves 1..8 (0 = none); key follow 0..9
enum Envelope { AmpEnv, WaveEnv, PitchEnv, kEnvelopes };
constexpr int kEnvSteps = 8;
constexpr int kEnvSettings = 2 * kEnvSteps + 2; // eight rates, eight levels, sustain step, end step
constexpr int kLineSettings = kLineHead + kEnvelopes * kEnvSettings;
constexpr int kSettings = kGlobals + 2 * kLineSettings;

constexpr int lineSetting(int line, int setting) { return kGlobals + line * kLineSettings + setting; }
constexpr int envBase(int line, int env) { return lineSetting(line, kLineHead) + env * kEnvSettings; }
constexpr int envRate(int line, int env, int step) { return envBase(line, env) + step; }           // 0..99
constexpr int envLevel(int line, int env, int step) { return envBase(line, env) + kEnvSteps + step; } // 0..99
constexpr int envSustain(int line, int env) { return envBase(line, env) + 2 * kEnvSteps; }            // 0 none, 1..8
constexpr int envEnd(int line, int env) { return envBase(line, env) + 2 * kEnvSteps + 1; }            // 1..8

int settingMin(int id);
int settingMax(int id);

// ---- the macros on the main panel -----------------------------------------------
enum Macro { Tone, Motion, Attack, Release, Detune, Vibrato, Chorus, MacroOctave, kMacros };

} // namespace phazed

struct PhazedPatch {
    int value[phazed::kSettings] = {};
};

class Phazed {
public:
    // Allocates and builds tables: not for the audio thread.
    Phazed();
    ~Phazed();
    bool ok() const { return ok_; }

    void prepare(float sampleRate);

    // The built-in presets, in folders. Names are as shown to the player.
    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index);
    static PhazedPatch presetPatch(int index);

    // Everything below is realtime safe.
    void selectPreset(int index); // sounding notes fade out; macros go back to the middle
    int preset() const { return preset_; }
    void setSetting(int id, int value);     // edit the patch
    void setMacro(int macro, float value);  // 0..1, 0.5 = as the patch says
    void noteOn(int note, float velocity);  // MIDI note number; velocity 0..1
    void noteOff(int note);
    void allNotesOff();

    // Writes `frames` samples (replacing what was there).
    void render(float* left, float* right, int frames);

private:
    struct State;
    void apply(bool newSound);

    std::unique_ptr<State> state_;
    bool ok_ = false;
    int preset_ = 0;
    PhazedPatch patch_;
    float macro_[phazed::kMacros];
};

} // namespace apotheca
