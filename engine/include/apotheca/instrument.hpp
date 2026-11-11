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
    // The same, but the note slides to its pitch from the key `from`, taking `seconds` (GLIDE).
    // A synth that cannot slide just plays the note.
    virtual void noteOnFrom(int note, float velocity, int from, float seconds) {
        (void)from;
        (void)seconds;
        noteOn(note, velocity);
    }
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

// Every synth has this many macros (the eight knobs on its DEVICE screen).
// Songs, presets and automation lanes 0..7 all count on it; each synth's
// header checks its own list against it.
constexpr int kSynthMacros = 8;

} // namespace apotheca
