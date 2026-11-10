#pragma once
// The audio engine. No screen, no device, no SDL: it turns commands into
// stereo samples, so it can be tested on its own.
//
// Signal path:
//   track instrument -> track slot 0 -> track slot 1 -> track level -+
//   (up to 12)                                                       +-> master slot 0 -> master slot 1 -> ceiling -> out
//
// The ceiling is a soft limit at the very end: below 0.8 of full scale the
// sound passes untouched, above it the peaks are rounded off, and nothing
// ever leaves louder than full scale.
//
// A song is up to sixteen scenes. A scene is one bar of sixteen steps for
// every track, and has a repeat count. Playing a song plays scene 1 its
// number of times, then scene 2, and so on, then starts again. A scene can
// also be looped on its own.
//
// A new engine has one track with an instrument: track 0, the drums, playing
// the built-in kit. The other tracks are empty and cost nothing, until
// addDrumTrack() or addSynthTrack() gives one an instrument.
//
// Threads:
//   - ONE control thread (the screen) calls send(), addDrumTrack(),
//     addSynthTrack() and the read-outs. None of them block.
//   - The audio thread calls render(). It allocates nothing and takes no lock.
//   - setTrackEffect()/setMasterEffect()/prepare() are setup calls. Make them
//     before the audio device starts.

#include <atomic>
#include <cstdint>
#include <memory>

#include "apotheca/drums.hpp"
#include "apotheca/effect.hpp"
#include "apotheca/instrument.hpp"
#include "apotheca/phazed.hpp"
#include "apotheca/twin.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

constexpr int kNumTracks = 12;
constexpr int kEffectSlots = 2;   // per track, and on the master
constexpr int kMasterTrack = -1;  // "track" index that addresses the master
constexpr int kMaxBlock = 64;     // internal chunk size, in frames
constexpr float kMinTempo = 40.f;
constexpr float kMaxTempo = 240.f;
constexpr int kMaxScenes = 16;
constexpr int kMaxRepeats = 16;
constexpr int kMaxNotes = 64;     // notes in one scene of one synth track
constexpr int kScopeSize = 1024;  // samples the scope keeps
constexpr double kNoNote = 1e30;
constexpr float kShortestNote = 0.05f; // of a step

struct Command {
    enum Type {
        Trigger,        // play a drum lane now:      track, a = lane, value = velocity,
                        //   b = where in the sound to start, in ten-thousandths of its length
        SetStep,        // write one pattern step:    track, scene, a = lane, b = step, value = velocity (0 clears)
        Play,           // start from step 1:         a = scene to start on, b = 1 to loop that scene alone
        Stop,           // stop; drums ring out, synth notes are let go
        SetTempo,       // value = beats per minute
        SetTrackLevel,  // track, value = linear gain
        SetEffectParam, // track (or kMasterTrack), a = slot, b = param, value
        SetLaneLevel,   // one drum's volume:         track, a = lane, value = 0..1
        SetStepNudge,   // move a hit off the grid:   track, scene, a = lane, b = step, value = -0.5..0.5 of a step
        SetStepChance,  // how likely a hit plays:    track, scene, a = lane, b = step, value = 0..1
        SetStepStart,   // where in the sound a hit starts: track, scene, a = lane, b = step, value = 0..1 of its length
        NoteOn,         // a synth track plays a note now: track, a = MIDI note, value = velocity 0..1
        NoteOff,        //                                 track, a = MIDI note
        SelectPreset,   // a synth track's sound:     track, a = preset number
        SetNote,        // write one note of a synth pattern: track, scene, a = slot (0..63),
                        //   b = step, c = MIDI note, d = length in whole steps, extra = and this much of a
                        //   step more (so d = 2, extra = 0.4 is 2.4 steps), value = velocity (0 empties the slot)
        SetSceneCount,  // a = how many scenes the song has (1..16)
        SetSceneRepeats,// scene, a = times it plays (1..16)
        SetMacro,       // a synth track's macro:     track, a = macro, value = 0..1
        SetSetting,     // a synth track's patch:     track, a = setting, b = value
        InstallSynth,   // internal: sent by addSynthTrack()
        InstallDrums,   // internal: sent by addDrumTrack()
        RemoveInstrument, // internal: sent by removeTrack()
        SetTrackImage,  // internal: sent by setTrackImage()
        SetLaneSample   // internal: sent by setLaneSample()
    };
    Type type = Trigger;
    int track = 0;
    int scene = 0;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    float value = 0.f;
    float extra = 0.f;       // SetNote only
    void* pointer = nullptr; // InstallSynth only
};

// Everything a track plays, in one piece: for loading a whole song in a
// handful of commands instead of thousands.
struct TrackImage {
    float level[kMaxScenes][kDrumLanes][kSteps] = {};
    float nudge[kMaxScenes][kDrumLanes][kSteps] = {};
    float chance[kMaxScenes][kDrumLanes][kSteps];
    float start[kMaxScenes][kDrumLanes][kSteps] = {};
    struct Note { std::uint8_t step = 0, pitch = 0; float length = 0.f; float velocity = 0.f; }; // length in steps, not only whole ones
    Note notes[kMaxScenes][kMaxNotes];
    float laneLevel[kDrumLanes];
    TrackImage() {
        for (auto& scene : chance)
            for (auto& lane : scene)
                for (float& c : lane) c = 1.f;
        for (float& l : laneLevel) l = 1.f;
    }
};

class Engine {
public:
    Engine();
    ~Engine();

    // Setup calls (not realtime safe).
    void prepare(float sampleRate);
    void setTrackEffect(int track, int slot, std::unique_ptr<Effect> effect);
    void setMasterEffect(int slot, std::unique_ptr<Effect> effect);

    // From the control thread, at any time. Give an empty track an
    // instrument; it is there from the next render(). False if the track
    // already has one (or, for the synth, if it could not be built).
    bool addDrumTrack(int track);
    bool addSynthTrack(int track, SynthKind kind = SynthKind::Phazed); // Allocates here, never on the audio thread.

    // Take a track's instrument away and empty its patterns, so the track
    // can be given another. False if it had none.
    bool removeTrack(int track);
    // Replace every pattern on a track at once.
    bool setTrackImage(int track, std::unique_ptr<TrackImage> image);
    // Put a recording on one lane of a drum track in place of the built-in
    // drum, or nullptr to bring the drum back. False if the track has no drums.
    bool setLaneSample(int track, int lane, std::unique_ptr<SampleData> sample);
    // Free what the audio thread has handed back (removed synths, used
    // images). Call now and then from the control thread.
    void collectGarbage();

    // From the control thread. Returns false if the queue is full.
    bool send(const Command& command) { return commands_.push(command); }

    // From the audio thread. Writes `frames` interleaved stereo frames (L R L R ...).
    void render(float* interleaved, int frames);

    // Read-outs for the screen. Safe from any thread.
    float trackPeak(int track) const; // loudest sample in the last render() call
    int playStep() const { return playStep_.load(std::memory_order_relaxed); }   // 0..15, or -1 when stopped
    int playScene() const { return playScene_.load(std::memory_order_relaxed); } // 0..15, or -1 when stopped
    int playRepeat() const { return playRepeat_.load(std::memory_order_relaxed); } // 0-based pass through the scene

    // The scope: the last samples one track put out, after its effects.
    void setScopeTrack(int track) { scopeTrack_.store(track, std::memory_order_relaxed); } // -1: none
    void readScope(float* out, int count) const; // the newest `count` samples (up to kScopeSize), oldest first

    float sampleRate() const { return sampleRate_; }

private:
    struct DrumScene {
        float level[kDrumLanes][kSteps] = {}; // 0 = no hit
        float nudge[kDrumLanes][kSteps] = {}; // -0.5..0.5 of a step
        float chance[kDrumLanes][kSteps];     // 0..1, set to 1 in the constructor
        float start[kDrumLanes][kSteps] = {}; // 0..1 of the sound's length
    };
    struct Note {
        std::uint8_t step = 0, pitch = 0;
        float length = 0.f;   // in steps; a note may end part-way through one
        float velocity = 0.f; // 0 = empty slot
    };
    struct Track {
        bool hasDrums = false;
        DrumMachine drums;
        SampleData* laneSamples[kDrumLanes] = {}; // owned; freed by the control thread
        std::unique_ptr<Instrument> synth; // set on a synth track
        DrumScene drumScenes[kMaxScenes];
        Note notes[kMaxScenes][kMaxNotes];
        // Synth notes sounding from the pattern: samples until each is let
        // go, to the sample. kNoNote: that key is not sounding.
        double offIn[128];
        Track() { for (double& off : offIn) off = kNoNote; }
        // Hits waiting for their moment: a late hit from the step that just
        // began, and an early hit that belongs to the next one.
        struct Pending { double samplesLeft = 0.0; float velocity = 0.f, start = 0.f; };
        Pending pending[kDrumLanes][2];
        float level = 0.65f; // one full hit stays clean; hits that land together push the master
        std::unique_ptr<Effect> effects[kEffectSlots];
        std::atomic<float> peak{0.f};
        bool hasInstrument() const { return hasDrums || synth; }
    };
    struct Position { int scene, repeat; };
    void fireNoteOffs();

    void apply(const Command& command);
    Position afterThisBar() const;
    void fireStep();
    void schedule(Track& track, int lane, const DrumScene& scene, int step, double delaySamples);
    void firePending();
    void clearPending();
    void letGoOfNotes();
    void renderChunk(float* interleaved, int frames, float* peaks);

    float sampleRate_ = 48000.f;
    DrumKit builtInKit_;
    Track tracks_[kNumTracks];
    bool claimed_[kNumTracks] = {};
    bool claimedDrums_[kNumTracks] = {}; // ...and which of those are drums // control thread's view of which tracks have an instrument
    std::unique_ptr<Effect> masterEffects_[kEffectSlots];

    // The step clock. Steps are sixteenth notes.
    bool playing_ = false;
    bool loopScene_ = false;
    bool firstStep_ = true; // the step about to fire is the first since Play
    float tempo_ = 120.f;
    double samplesPerStep_ = 6000.0;
    double untilStep_ = 0.0; // samples left before the next step fires
    int nextStep_ = 0;
    int scene_ = 0;
    int repeat_ = 0;
    int sceneCount_ = 1;
    int repeats_[kMaxScenes];
    std::atomic<int> playStep_{-1};
    std::atomic<int> playScene_{-1};
    std::atomic<int> playRepeat_{0};
    std::uint32_t dice_ = 0x2545F491u; // for chance; runs on, so no two bars need match

    std::atomic<int> scopeTrack_{-1};
    std::atomic<float> scope_[kScopeSize];
    std::atomic<unsigned> scopeWritten_{0};

    SpscQueue<Command, 4096> commands_; // room for whole patterns sent at once

    // Things the audio thread no longer needs and must not free itself.
    struct Trash { bool isImage = false; void* pointer = nullptr; bool isSample = false; };
    SpscQueue<Trash, 64> trash_;
    static void destroy(const Trash& trash);
};

} // namespace apotheca
