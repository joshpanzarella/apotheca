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
    void trigger(int lane, float velocity, float start = 0.f);

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
        float gain = 0.f;
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
    float chokeStep_ = 1.f / 144.f; // 3 ms at 48 kHz
};

} // namespace apotheca
