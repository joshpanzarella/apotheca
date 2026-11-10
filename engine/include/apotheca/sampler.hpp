#pragma once
// The sampler: one recording played up and down the keys, eight notes at a
// time. At its ROOT key the recording plays as it was made; an octave up it
// plays twice as fast and half as long.
//
// A key plays the recording from START to its end and stops (or, with LOOP
// on, goes round from START for as long as the key is held). Letting go
// fades it over RELEASE.

#include "apotheca/instrument.hpp"

namespace apotheca {

namespace sampler {
enum Setting {
    Root,    // the key that plays the recording at its own pitch: 0..127
    Tune,    // cents, -100..100
    Start,   // where a note begins in the recording: 0..100 percent
    Attack,  // fade in: 0..100 (a millisecond to two seconds)
    Release, // fade out when the key is let go: 0..100 (five milliseconds to four seconds)
    Loop,    // 0 off, 1 on
    Level,   // 0..100
    kSettings
};
int settingMin(int id);
int settingMax(int id);
int settingDefault(int id);
constexpr int kVoices = 8;
constexpr float kVoiceGain = 0.6f; // a full-level note at full velocity; chords have room
}

class Sampler : public Instrument {
public:
    Sampler();
    void prepare(float sampleRate) override;
    void selectPreset(int) override {}      // it has none: its sound is its recording
    void setMacro(int, float) override {}
    void setSetting(int id, int value) override;
    void noteOn(int note, float velocity) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void setSample(const float* data, int length) override;
    void render(float* left, float* right, int frames) override;

private:
    struct Voice {
        int note = -1;       // -1: free
        double position = 0.0, step = 1.0;
        float gain = 0.f;    // velocity
        float envelope = 0.f;
        bool held = false;
        unsigned age = 0;    // for choosing which to take when all eight are busy
    };
    const float* data_ = nullptr;
    int length_ = 0;
    int value_[sampler::kSettings];
    Voice voices_[sampler::kVoices];
    unsigned clock_ = 0;
    float sampleRate_ = 48000.f;
    float attackStep_ = 1.f, releaseStep_ = 1.f, level_ = 0.8f;
};

} // namespace apotheca
