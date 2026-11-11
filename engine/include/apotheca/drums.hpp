#pragma once
// The drum instrument: eight lanes, one sample each, played from a 16-step
// pattern. No SDL, no files: the built-in kit is made in code.

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
    void trigger(int lane, float velocity, float start = 0.f, float part = -1.f);

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
    void render(float* left, float* right, int frames);

private:
    struct Voice {
        int position = -1; // -1: silent
        int end = 0;       // where its part of the sound stops (then it fades); past the sound's end: it plays out
        float gain = 0.f;     // the hit's level...
        float gainTo = 0.f;   // ...and where it is heading (a hit that carries the sound on may change it)
        float fade = 1.f;     // multiplies the gain; falls while choking
        float fadeStep = 0.f; // 0: not choking
        float rise = 1.f;     // multiplies the gain; climbs after a start part-way in
        float riseStep = 0.f;
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
