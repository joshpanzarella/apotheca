#pragma once
// Twin Analog: two morphing oscillators, V and Z, crossfaded in stereo.
//
// This is the Clairaudient oscillator from the Shapetaker VCV Rack plugin
// (src/clairaudient.cpp there), ported to stand alone. The sound-making
// code is the module's, restated without Rack: the sigmoid saw and PWM
// waves, the two detuned copies of each oscillator (one per side), cross
// sync and reverse sync, the formant section, the width stage, drift, voice
// character and bus colour.
//
// What changed in the move from a module to an instrument:
//   - Rack patches bring the notes and the volume envelope. Here there are
//     eight voices and an attack / decay / sustain / release envelope.
//   - Reverse sync always flips. The module's CHANCE control is left out.
//   - There are no CV inputs. The module's defaults for an unpatched jack
//     apply (Z follows V's pitch).
//   - The voices are mixed before the oversampling filter and the bus
//     colour, not after: one filter per side in place of one per voice.
//     Filtering is the same for a sum as for its parts, so only the colour
//     stage (a soft clip) hears the difference, and it now acts on the
//     chord, as a mixing bus would.
//
// A sound is a TwinPatch: every setting as a whole number. Eight macros
// (0..1, 0.5 = as the patch says) bend what is played without changing it.

#include <cstdint>
#include <memory>

#include "apotheca/instrument.hpp"

namespace apotheca {

namespace twin {

enum Setting {
    VOctave,      // -2..2
    VFine,        // -20..20 cents: spreads V's two copies apart
    VShape,       // 0..100: saw, through a soft S-curve, to a hard edge
    ZSemitone,    // -24..24, against V
    ZFine,        // -20..20 cents
    ZShape,       // 0..100
    Crossfade,    // 0..100: all V to all Z
    CrossSync,    // 0..1: Z restarts with V
    ReverseSync,  // 0 off, 1 on (V turns Z around), 2 mutual (and Z turns V)
    Waveform,     // 0 sigmoid saw, 1 pulse
    CrossfadeMode,// 0 equal power, 1 stereo swap
    FormantDepth, // 0..100
    FormantRatio, // 0..7: x0.5 1 1.5 2 3 4 5 7
    Asymmetry,    // 0..100
    Width,        // 0..100: mono, natural at 50, double at 100
    Vintage,      // 0..100: drift, voice differences and bus colour
    Noise,        // 0..100
    Oversample,   // 0..3: x1 x2 x4 x8
    Attack,       // 0..100: 1 ms to 8 s
    Decay,        // 0..100
    Sustain,      // 0..100 %
    Release,      // 0..100
    Level,        // -24..6 dB
    kSettings
};
int settingMin(int id);
int settingMax(int id);

enum Macro { Shape, Blend, Formant, MacroWidth, Detune, MacroVintage, MacroAttack, MacroRelease, kMacros };
static_assert(kMacros == kSynthMacros, "every synth has the same number of macros");

constexpr int kVoices = 8;

} // namespace twin

struct TwinPatch {
    int value[twin::kSettings] = {};
};

class Twin : public Instrument {
public:
    Twin();
    ~Twin() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index);
    static TwinPatch presetPatch(int index);

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override;
    void noteOn(int note, float velocity) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void setQualityLimit(int limit) override; // caps OVERSAMPLE: 0 is x1, 3 is x8
    void render(float* left, float* right, int frames) override;

private:
    struct State;
    void apply();
    std::unique_ptr<State> state_;
};

} // namespace apotheca
