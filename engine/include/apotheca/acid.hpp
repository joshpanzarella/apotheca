#pragma once
// ACID: a one-voice bass after a classic bass line box (docs/plans/acid-plan.md).
//
// One oscillator (SAW or SQUARE) into a four-pole diode ladder with resonance, a filter decay that
// opens the cutoff (ENV MOD) and an amp decay. ACCENT is a note at velocity 0.95 or over: louder,
// the filter's shortest decay, and the accent SWEEP that climbs when accents come close together.
// SLIDE is a note's GLIDE: tied to the note before (end to end), the pitch slides and no envelope
// starts again. The rules and figures are acid_rule.hpp's; the filter is engine/src/acid/ladder.hpp.
//
// One voice, the newest key wins; letting it go does not go back to a key still held. One tail
// slot: allNotesOff hands the sounding note to it (a 3 ms fade) and the voice starts fresh, so Play
// from the top is the same line. A voice whose gate has been shut over 5 ms (the tie's window) and
// whose accent sweep has died away sleeps: exact zeros, no work, and it wakes as a fresh one.
//
// Everything is worked out a sample at a time, or when a setting is turned (at the top of the next
// render), so the sound does not hang on how it is cut into blocks.

#include <memory>

#include "acid_rule.hpp"
#include "instrument.hpp"

namespace apotheca {

struct AcidPatch {
    int value[acid::kSettingRoom] = {};
};

class Acid : public Instrument {
public:
    Acid();
    ~Acid() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index); // the folder
    static AcidPatch presetPatch(int index);      // 0: Classic Saw, the new track's sound

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override; // stores; worked out at the next render
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override; // Play, Stop, panic: the note to the tail (3 ms), the voice fresh
    void render(float* left, float* right, int frames) override;

    // For the tests and the timings.
    bool awake() const;            // the voice (not the tail)
    bool tailAwake() const;
    long tunings() const;          // times the filter's coefficients were worked out
    long samplesClipped() const;   // samples the output's clip bent (over 1.0)
    int key() const;               // the key sounding, -1 asleep
    bool gateOpen() const;
    long strikes() const;          // notes that started the envelopes
    long ties() const;             // notes that slid without starting them
    // What the voice holds now: the filter envelope, the amp, the accent sweep's capacitor, the
    // pitch in octaves over A4, the cutoff's singing pitch in Hz (with its envelope and sweep).
    struct Now { double filterEnv = 0, amp = 0, sweep = 0, pitchOctaves = 0, cutoffHz = 0, level = 0; };
    Now now() const;
    // The smallest size (not zero) any state of the voice and the tail holds: 0 if none.
    float smallestState() const;
    void setTuneTickForTests(int samples); // the cutoff worked out every this many samples (1: every one)

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace apotheca
