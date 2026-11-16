#pragma once
// PHYSIC: plucked and struck sounds that ring by themselves (docs/plans/physic-plan.md).
//
// A note is a strike: something hits or plucks a string, bar, bell or skin, and it rings and
// dies away. There is no ADSR. DECAY is the object's own ringing time (the longest any part of
// it rings), and letting go damps it (RELEASE; at RING letting go does nothing, as a marimba
// has no damper). The rules and figures are physic_rule.hpp's; the parts are in
// engine/src/physic/ (modes, string, strike).
//
//   MODEL     STRING, MARIMBA, XYLO, VIBES, GLOCK, KALIMBA, BELL, CHIME, GLASS, DRUM, WOOD
//   STRIKE    PLUCK or MALLET; HARDNESS the mallet (yarn to brass) or the pluck (finger to
//             plectrum); POSITION where along it is struck, which decides which modes wake
//   DECAY     how long it rings at most; MATERIAL wood (high parts die fast) to metal
//   STRETCH   50 as made; under 50 towards whole-number partials, over 50 further apart.
//             STRING: how stiff the string is (a piano's high partials are sharp)
//   DICE      each note struck a little off where, how hard, how in tune and how long the last
//             was: rolled from where and which time it is struck, so Play twice is the same
//
// Voices: 8 sounding, and 2 slots that only fade out a stolen voice (3 ms, no click). A ringing
// key struck again is the same voice (a bar is linear: the new hit adds to the ringing; a string
// is damped 2 ms first, as a plectrum meets it). A note's GLIDE from a voice that still rings
// loud takes that voice over (a slide guitar), when it is the only note of its moment to name it.
//
// Every sum a voice does is on its own tick (16 samples counted from its strike) or a sample at a
// time from its strike, so the sound does not hang on how it is cut into blocks. Asleep, a voice
// is exact zeros and does no work.

#include <cstdint>
#include <memory>

#include "instrument.hpp"
#include "physic_rule.hpp"

namespace apotheca {

namespace physic {
constexpr int kVoices = 8;      // sounding (the plan's call 5: one constant)
constexpr int kFadeSlots = 2;   // a stolen voice fades here
constexpr int kSlots = kVoices + kFadeSlots;
constexpr int kTick = 16;       // samples: a voice works out its numbers this often
} // namespace physic

struct PhysicPatch {
    int value[physic::kSettingRoom] = {};
};

class Physic : public Instrument {
public:
    Physic();
    ~Physic() override;

    void prepare(float sampleRate) override;

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index); // the folder
    static PhysicPatch presetPatch(int index);    // 0: MARIMBA, the new track's sound

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override;
    void noteSeed(std::uint32_t seed) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override; // Play, panic: every voice damped in 30 ms
    void stopNotes() override;   // Stop: every voice let go (RELEASE damps; RING rings on)
    void render(float* left, float* right, int frames) override;

    // For the tests and the timings.
    int awake() const;                 // voices not asleep (fading ones too)
    long ticksWorked() const;          // ticks any voice has worked out since made
    long strikes() const;              // strikes begun (a take-over is none)
    long tunings() const;              // times a voice's coefficients were worked out (a macro: on the next tick)
    // The smallest size (not zero) any state of any voice holds now: a mode's two numbers, a
    // string's filters and its line. 0: none but zeros.
    float smallestState(bool lines = true) const; // lines: the strings' lines too (the slow part)
    // Voice `slot`'s modes as they ring now: how many, and each one's frequency in Hz (with its
    // glide), for the tests of the tables. -1 when the slot is asleep or a string.
    int modesOf(int slot, double* hz, float* heard = nullptr) const; // heard: each one's share heard (the fade near Nyquist)
    std::uint32_t lastSeed() const;    // the roll the last strike took
    int keysSounding(int* keys) const; // the keys of the voices that sound (not asleep, not fading out), how many
    // What the last strike rolled (the sums the dice moved), for the tests.
    struct Rolled { double position = 0, hardness = 0, tuneCents = 0, decayScale = 1; };
    Rolled lastRolled() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace apotheca
