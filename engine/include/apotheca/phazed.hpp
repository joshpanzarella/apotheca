#pragma once
// Phazed: Apotheca's phase distortion synth.
//
// "Phazed" is the only name a player ever sees. Its engine is the code in
// third_party/easycz, copied unchanged from joshpanzarella/schwung-easycz;
// that folder keeps its own names and licences, and nothing in it is shown
// on screen without passing through this file.
//
// The engine makes sound at 44100 Hz, the rate its filters were designed for.
// If the engine runs at another rate the output is converted here, with a
// plain straight-line interpolation. Run the audio device at 44100 Hz and
// that step does nothing.

#include <memory>

namespace apotheca {

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

    // Everything below is realtime safe.
    void selectPreset(int index); // sounding notes fade out
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
