#pragma once
// What the engine needs from any synth it puts on a track.
//
// The constructor and prepare() may allocate and are not for the audio
// thread. Everything else is called on it and must not allocate or wait.

namespace apotheca {

class Instrument {
public:
    virtual ~Instrument() = default;
    virtual bool ok() const { return true; }
    virtual void prepare(float sampleRate) = 0;

    virtual void selectPreset(int index) = 0;      // macros go back to the middle
    virtual void setSetting(int id, int value) = 0; // edit the patch
    virtual void setMacro(int macro, float value) = 0; // 0..1, 0.5 = as the patch says
    virtual void noteOn(int note, float velocity) = 0; // MIDI note number; velocity 0..1
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;
    // For an instrument that plays a recording: here it is (or nullptr for
    // none). The samples stay alive until replaced. Realtime safe.
    virtual void setSample(const float* data, int length) { (void)data; (void)length; }

    // The machine is falling behind: do no better than this (0 plainest ..
    // 3 best) until told otherwise. For a synth with a costly "quality"
    // setting; the setting itself is left as the player has it.
    virtual void setQualityLimit(int limit) { (void)limit; }

    // Writes `frames` samples (replacing what was there).
    virtual void render(float* left, float* right, int frames) = 0;
};

enum class SynthKind { Phazed, Twin, Sampler, Qmod };

} // namespace apotheca
