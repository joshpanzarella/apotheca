#pragma once
// The audio engine. No screen, no device, no SDL: it turns commands into
// stereo samples, so it can be tested on its own.
//
// Signal path:
//   track instrument -> track slot 0 -> track slot 1 -> track level -+
//   (up to 12)                                                       +-> master slot 0 -> master slot 1 -> out
//
// A new engine has one track with an instrument: track 0, the drums, playing
// the built-in kit. The other tracks are empty and cost nothing.
//
// Threads:
//   - Any ONE other thread calls send(). It never blocks.
//   - The audio thread calls render(). It allocates nothing and takes no lock.
//   - setTrackEffect()/setMasterEffect()/prepare() are setup calls. Make them
//     before the audio device starts.

#include <atomic>
#include <memory>

#include "apotheca/drums.hpp"
#include "apotheca/effect.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

constexpr int kNumTracks = 12;
constexpr int kEffectSlots = 2;   // per track, and on the master
constexpr int kMasterTrack = -1;  // "track" index that addresses the master
constexpr int kMaxBlock = 64;     // internal chunk size, in frames
constexpr float kMinTempo = 40.f;
constexpr float kMaxTempo = 240.f;

struct Command {
    enum Type {
        Trigger,        // play a drum lane now:      track, a = lane, value = velocity
        SetStep,        // write one pattern step:    track, a = lane, b = step, value = velocity (0 clears)
        Play,           // start the pattern from step 1
        Stop,           // stop the pattern; sounds already playing ring out
        SetTempo,       // value = beats per minute
        SetTrackLevel,  // track, value = linear gain
        SetEffectParam  // track (or kMasterTrack), a = slot, b = param, value
    };
    Type type = Trigger;
    int track = 0;
    int a = 0;
    int b = 0;
    float value = 0.f;
};

class Engine {
public:
    Engine();

    // Setup calls (not realtime safe).
    void prepare(float sampleRate);
    void setTrackEffect(int track, int slot, std::unique_ptr<Effect> effect);
    void setMasterEffect(int slot, std::unique_ptr<Effect> effect);

    // From the screen thread. Returns false if the queue is full.
    bool send(const Command& command) { return commands_.push(command); }

    // From the audio thread. Writes `frames` interleaved stereo frames (L R L R ...).
    void render(float* interleaved, int frames);

    // For the screen. Safe from any thread.
    float trackPeak(int track) const; // loudest sample in the last render() call
    int playStep() const { return playStep_.load(std::memory_order_relaxed); } // 0..15, or -1 when stopped

    float sampleRate() const { return sampleRate_; }

private:
    struct Track {
        bool hasDrums = false;
        DrumMachine drums;
        float pattern[kDrumLanes][kSteps] = {}; // velocity per step, 0 = no hit
        float level = 0.5f; // headroom: several drums landing together must not clip
        std::unique_ptr<Effect> effects[kEffectSlots];
        std::atomic<float> peak{0.f};
    };

    void apply(const Command& command);
    void fireStep();
    void renderChunk(float* interleaved, int frames, float* peaks);

    float sampleRate_ = 48000.f;
    DrumKit builtInKit_;
    Track tracks_[kNumTracks];
    std::unique_ptr<Effect> masterEffects_[kEffectSlots];

    // The step clock. Steps are sixteenth notes.
    bool playing_ = false;
    float tempo_ = 120.f;
    double samplesPerStep_ = 6000.0;
    double untilStep_ = 0.0; // samples left before the next step fires
    int nextStep_ = 0;
    std::atomic<int> playStep_{-1};

    SpscQueue<Command, 256> commands_;
};

} // namespace apotheca
