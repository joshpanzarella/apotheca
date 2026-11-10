#pragma once
// The CZ synth: EasyCZ's phase distortion engine (third_party/easycz, copied
// unchanged from joshpanzarella/schwung-easycz), wrapped for Apotheca.
//
// EasyCZ makes sound at 44100 Hz, the rate its filters were designed for.
// If the engine runs at another rate the output is converted here, with a
// plain straight-line interpolation. Run the audio device at 44100 Hz and
// that step does nothing.

#include <memory>

namespace apotheca {

class CzSynth {
public:
    // Allocates and builds tables: not for the audio thread.
    CzSynth();
    ~CzSynth();
    bool ok() const { return ok_; }

    void prepare(float sampleRate);

    // The presets that ship inside EasyCZ, in folders.
    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index);

    // Everything below is realtime safe.
    void selectPreset(int index); // sounding notes fade out, as on a CZ
    int preset() const { return preset_; }
    void noteOn(int note, float velocity); // MIDI note number; velocity 0..1
    void noteOff(int note);
    void allNotesOff();

    // Writes `frames` samples (replacing what was there).
    void render(float* left, float* right, int frames);

private:
    struct State;
    std::unique_ptr<State> state_;
    bool ok_ = false;
    int preset_ = 0;
};

} // namespace apotheca
