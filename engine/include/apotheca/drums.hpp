#pragma once
// The drum instrument: eight lanes, one sample each, played from a 16-step
// pattern. No SDL, no files: the built-in kit is made in code.

#include <cstdint>
#include <vector>

namespace apotheca {

constexpr int kDrumLanes = 8;
constexpr int kSteps = 16;

// Eight mono samples.
struct DrumKit {
    std::vector<float> samples[kDrumLanes];

    // Fills every lane with a synthesised sound: kick, snare, closed hat,
    // open hat, clap, tom, rim, crash. Allocates, so call it off the audio
    // thread. The same sample rate always gives the same samples.
    void buildBuiltIn(float sampleRate);
    // The built-in kit at a sample rate: made the first time it is asked for
    // and kept, unchanged, for as long as the program runs. Every engine at
    // that rate plays from the same one (building it is a twentieth of a
    // second of work, and was most of the time the tests took). Any thread
    // but the audio thread.
    static const DrumKit& builtIn(float sampleRate);

    static const char* laneName(int lane);
};

// The step effects: what one hit does besides play. Each hit of a pattern
// has its own, and they combine (a hit can be an octave down, backwards and
// played three times).
struct StepFx {
    std::int8_t pitch = 0;    // semitones up or down, -12..12: the sound played faster or slower
    std::uint8_t ratchet = 0; // 2, 3 or 4: that many hits, evenly, inside the step. 0 or 1: the one
    std::uint8_t reverse = 0; // 1: backwards, from the end of what it would have played to its start
    std::uint8_t cut = 0;     // 1, 2, 3: stopped after a step, half a step, a quarter. 0: it rings on
    std::uint8_t throwTo = 0; // THROW: this hit alone is sent in full to a bus. kThrowEcho, kThrowSpace, or both (3)
    bool any() const { return pitch != 0 || ratchet > 1 || reverse != 0 || cut != 0 || throwTo != 0; }
    int hits() const { return ratchet < 2 ? 1 : (ratchet > 4 ? 4 : ratchet); }
    // How long a cut hit sounds, in steps (0: not cut).
    float cutSteps() const { return cut == 1 ? 1.f : cut == 2 ? 0.5f : cut == 3 ? 0.25f : 0.f; }
    // The cut and the THROW as the one number a command carries them in (Command::SetStepFx).
    float cutAndThrow() const { return static_cast<float>((cut & 3) + 4 * (throwTo & 3)); }
};
constexpr int kMaxStepPitch = 12, kMaxRatchet = 4, kCutChoices = 4;
// THROW, on a drum hit or a note: 0 OFF, 1 ECHO, 2 SPACE, 3 BOTH. Bit `bus` is "to that bus".
constexpr int kThrowEcho = 1, kThrowSpace = 2, kThrowChoices = 4;
// The name of a THROW value, as the screens show it.
inline const char* throwName(int value) {
    return value == 1 ? "ECHO" : value == 2 ? "SPACE" : value == 3 ? "BOTH" : "OFF";
}

// How one hit is played (the drum machine's own terms; the engine works them
// out from a StepFx and the tempo).
struct HitPlay {
    float rate = 1.f;     // speed: 2 is an octave up, 0.5 an octave down
    bool reverse = false;
    int life = 0;         // frames it may sound before it is faded out. 0: as long as it lasts
    int throwTo = 0;      // THROW: which buses this hit is also written out for (kThrowEcho, kThrowSpace)
};

// A recording to play on a lane in place of the kit's drum: mono, at the
// engine's sample rate.
struct SampleData {
    std::vector<float> data;
};

// Plays a kit. render() is realtime safe; the kit must outlive the machine
// and must not change while audio runs.
class DrumMachine {
public:
    void setKit(const DrumKit* kit) { kit_ = kit; }
    void prepare(float sampleRate);
    void reset();

    // Start a lane's sample at `velocity` (0..1, linear gain). A sound still
    // ringing on that lane is faded out over a few milliseconds, not cut.
    // `start` is where in the sound to begin, 0..1 of its length (0: the
    // beginning). Starting part-way in fades up over a millisecond, so a cut
    // into the middle of a sound does not click.
    //
    // `part` is how much of the sound the hit plays, 0..1 of its length, before
    // it is faded out: a slice of a longer recording. 0: to the end. Below
    // zero: whatever the lane is set to (setLanePart).
    //
    // A hit that starts where the lane's sound has just got to (within a
    // millisecond and a half) does not start it again: the sound carries
    // straight on, at the new hit's level. So a recording cut into slices
    // and played back in order has no seams, and only a slice that is out
    // of order is heard as a cut.
    //
    // `how` is the step effects' doing: faster or slower, backwards, cut short.
    // A hit played backwards starts at the end of what it would have played
    // (its slice, or the whole sound from `start` on) and stops at its start.
    void trigger(int lane, float velocity, float start = 0.f, float part = -1.f, const HitPlay& how = HitPlay());

    // How much of the sound every hit on a lane plays (see trigger). 0, the
    // default: all of it. Survives reset().
    void setLanePart(int lane, float part);

    // Play this recording on a lane in place of the kit's drum; nullptr goes
    // back to the kit's. What was ringing on the lane stops. The samples
    // must stay alive until replaced. Survives reset().
    void setLaneSample(int lane, const float* data, int length);

    // A lane's own volume, 0..1. Takes effect at once, on sounds already
    // ringing too. Survives reset().
    void setLaneLevel(int lane, float level);

    // Writes `frames` samples (replacing what was there).
    //
    // THROW: `echo` and `space`, if given, are a second way out, one for each
    // bus. A hit that is thrown (HitPlay::throwTo) is written there as well as
    // to `left` and `right`: that hit alone, whatever else sounds on the lane
    // or beside it. Returns which of the two were written (bit 0 `echo`, bit 1
    // `space`); one that was not is left as it was found, not zeroed.
    //
    // A hit that carries the lane's sound on (see trigger) hands on its own
    // THROW: the sound slides into the bus, or out of it, over a millisecond.
    int render(float* left, float* right, int frames, float* echo = nullptr, float* space = nullptr);

private:
    struct Voice {
        double position = -1.0; // below zero: silent. Not always a whole number: a pitched hit moves in parts of a sample
        double rate = 1.0;      // how far it moves each frame; below zero: backwards
        int end = 0;            // where its part of the sound stops (then it fades); past the sound's end: it plays out
        int stop = 0;           // backwards: where it stops (it has faded by then)
        int life = 0;           // frames left before it is cut (0: not cut)
        float gain = 0.f;     // the hit's level...
        float gainTo = 0.f;   // ...and where it is heading (a hit that carries the sound on may change it)
        float fade = 1.f;     // multiplies the gain; falls while choking
        float fadeStep = 0.f; // 0: not choking
        float rise = 1.f;     // multiplies the gain; climbs after a start part-way in
        float riseStep = 0.f;
        float thrown[2] = {};   // THROW: how much of it goes to each bus, as it is this sample...
        float thrownTo[2] = {}; // ...and where that is heading: 1 thrown, 0 not
    };

    const DrumKit* kit_ = nullptr;
    Voice voices_[kDrumLanes][2];
    int next_[kDrumLanes] = {};
    const float* own_[kDrumLanes] = {}; // a lane's recording, or nullptr for the kit's drum
    int ownLength_[kDrumLanes] = {};
    float laneLevel_[kDrumLanes] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    float lanePart_[kDrumLanes] = {};
    float chokeStep_ = 1.f / 144.f; // 3 ms at 48 kHz
    int joinFrames_ = 72;           // 1.5 ms: a hit this close to where the sound is carries it on
};

} // namespace apotheca
