#pragma once
// The sampler: one recording played up and down the keys, eight notes at a
// time. At its ROOT key the recording plays as it was made; an octave up it
// plays twice as fast and half as long.
//
// A key plays the recording from START to END and stops (or, with LOOP on,
// goes round from START to END for as long as the key is held). Letting go
// fades it over RELEASE. END is never before START: a note plays at least
// kLeastSpan percent of the recording (playedEnd).

#include "envelope.hpp"
#include "apotheca/slide.hpp"
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
    End,     // where a note ends in the recording: 1..100 percent (owner, 2026-11-15). Appended: ids are in songs
    kSettings
};
constexpr int kLeastSpan = 1; // END is at least this many percent after START (the screen holds them so)
// Where a note ends, as played: END, or kLeastSpan after START where END is not after it (a song
// file may say so; the screen never does).
inline int playedEnd(int start, int end) { return end < start + kLeastSpan ? start + kLeastSpan : end; }
// START or END turned to `value`, held kLeastSpan short of the other, so they never cross. The one
// rule for whatever turns them: the device's screen and the sound-designer page.
inline int heldApart(int id, int value, int start, int end) {
    if (id == Start && value > end - kLeastSpan) return end - kLeastSpan;
    if (id == End && value < start + kLeastSpan) return start + kLeastSpan;
    return value;
}
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
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void setSample(const float* data, int length) override;
    void render(float* left, float* right, int frames) override;

private:
    struct Voice {
        int note = -1;       // -1: free
        double position = 0.0, step = 1.0;
        float gain = 0.f;    // velocity
        envelope::Envelope env; // the shared one (envelope.hpp): attack, full while held, release
        Slide slide;         // a glide still on its way to this voice's pitch
        unsigned age = 0;    // for choosing which to take when all eight are busy
    };
    const float* data_ = nullptr;
    int length_ = 0;
    int value_[sampler::kSettings];
    Voice voices_[sampler::kVoices];
    unsigned clock_ = 0;
    float sampleRate_ = 48000.f;
    envelope::Shape shape_;
    float level_ = 0.8f;
};

} // namespace apotheca
