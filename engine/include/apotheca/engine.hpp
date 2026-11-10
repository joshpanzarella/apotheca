#pragma once
// The audio engine. No screen, no device, no SDL: it turns commands into
// stereo samples, so it can be tested on its own.
//
// Signal path:
//   track source -> track slot 0 -> track slot 1 -> track level -+
//   (x12)                                                        +-> master slot 0 -> master slot 1 -> out
//
// Threads:
//   - Any ONE other thread calls send(). It never blocks.
//   - The audio thread calls render(). It allocates nothing and takes no lock.
//   - setTrackEffect()/setMasterEffect()/prepare() are setup calls. Make them
//     before the audio device starts.

#include <atomic>
#include <memory>

#include "apotheca/effect.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

constexpr int kNumTracks = 12;
constexpr int kEffectSlots = 2;   // per track, and on the master
constexpr int kMasterTrack = -1;  // "track" index that addresses the master
constexpr int kMaxBlock = 64;     // internal chunk size, in frames

struct Command {
    enum Type { NoteOn, NoteOff, SetTrackLevel, SetEffectParam };
    Type type = NoteOn;
    int track = 0;     // 0..kNumTracks-1, or kMasterTrack for SetEffectParam
    int a = 0;         // NoteOn: MIDI pitch.  SetEffectParam: slot
    int b = 0;         // SetEffectParam: param index
    float value = 0.f; // NoteOn: velocity 0..1.  SetTrackLevel / SetEffectParam: value
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

    // Loudest sample a track produced in the last render() call. For meters.
    float trackPeak(int track) const;

    float sampleRate() const { return sampleRate_; }

private:
    // Stand-in sound source until the sampler exists: a decaying sine.
    struct Voice {
        float phase = 0.f;
        float increment = 0.f;
        float amplitude = 0.f;
        float decay = 1.f;
        bool held = false;
    };

    struct Track {
        Voice voice;
        float level = 0.8f;
        std::unique_ptr<Effect> effects[kEffectSlots];
        std::atomic<float> peak{0.f};
    };

    void apply(const Command& command);
    void renderChunk(float* interleaved, int frames, float* peaks);

    float sampleRate_ = 48000.f;
    float heldDecay_ = 1.f;
    float releaseDecay_ = 1.f;
    Track tracks_[kNumTracks];
    std::unique_ptr<Effect> masterEffects_[kEffectSlots];
    SpscQueue<Command, 256> commands_;
};

} // namespace apotheca
