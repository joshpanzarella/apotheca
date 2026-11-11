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
// A song is up to sixteen scenes. In a scene each track has a cell of 1 to 8
// bars of sixteen steps. The scene lasts as long as its longest cell; a
// shorter cell goes round under it (a one-bar drum loop plays four times
// under a four-bar part). A scene has a repeat count: whole passes of it. Playing a song plays scene 1 its
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

#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "apotheca/drums.hpp"
#include "apotheca/effect.hpp"
#include "apotheca/effects.hpp"
#include "apotheca/instrument.hpp"
#include "apotheca/phazed.hpp"
#include "apotheca/sampler.hpp"
#include "apotheca/qmod.hpp"
#include "apotheca/twin.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

constexpr int kNumTracks = 12;
constexpr int kEffectSlots = 2;   // per track, and on the master
constexpr int kMasterTrack = -1;  // "track" index that addresses the master
constexpr int kMaxBlock = 64;     // internal chunk size, in frames
constexpr float kSwapFadeSeconds = 0.010f; // an effect put in a slot, changed or taken out fades over this long
constexpr float kMinTempo = 40.f;
constexpr float kMaxTempo = 240.f;
constexpr int kMaxScenes = 16;
constexpr int kMaxRepeats = 16;
constexpr int kMaxBars = 8;       // the longest a cell (one track in one scene) can be
constexpr int kPatternSlots = kMaxScenes * kMaxBars; // a bar of a scene is slot scene * kMaxBars + bar
constexpr int kMaxNotes = 64;     // notes in one scene of one synth track
constexpr int kScopeSize = 1024;  // samples the scope keeps
constexpr double kNoNote = 1e30;
constexpr int kCatchFrames = 480000; // what the engine remembers of its own output: ten seconds
constexpr float kShortestNote = 0.05f; // of a step

struct Command {
    enum Type {
        Trigger,        // play a drum lane now:      track, a = lane, value = velocity,
                        //   b = where in the sound to start, in ten-thousandths of its length
                        //   c = how much of the sound to play, in ten-thousandths (0: as the lane is set)
                        //   and the step effects, to hear a hit as the pattern would play it: d = pitch in
                        //   semitones, bar = ratchet (2..4), scene = 1 for backwards, extra = cut (1, 2, 3)
        SetStep,        // write one pattern step:    track, scene, a = lane, b = step, value = velocity (0 clears)
        Play,           // start from step 1:         a = scene to start on, b = 1 to loop that scene alone
        Stop,           // stop; drums ring out, synth notes are let go
        Panic,          // silence, now. a = 1: the output fades to nothing in 5 ms and stays shut;
                        //   playing stops, every drum is cut and every note let go. a = 0: it may
                        //   open again, once what is left has died away (or the next thing is played).
        SetTempo,       // value = beats per minute
        SetVolume,      // the last thing before the output: value = linear gain, 0..1. Catch hears the sound before it.
        SetTrackLevel,  // track, value = linear gain
        SetSceneSteps,  // how many steps the scene lasts: scene, a = that many (23 is one bar and seven steps
                        //   of the next); 0, or as many as its longest cell has: all of them
        SetAutomation,  // one step of a lane: track, scene, a = lane, b = bar * 16 + step, value = 0..1 (below 0: nothing there)
        ClearAutomation,// a whole lane of a cell: track, scene, a = lane (-1: every lane)
        SetTrackHeard,  // mute and solo: track, a = 1 heard, 0 silent. It fades over 5 ms either way, so it does not click.
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
        SetCellBars,    // how long a track's cell is in a scene: track, scene, a = bars (1..8)
        ClearCell,      // empty every bar of a track's cell: track, scene. Its length is left as it is.
        SetMacro,       // a synth track's macro:     track, a = macro, value = 0..1
        SetSetting,     // a synth track's patch:     track, a = setting, b = value
        InstallSynth,   // internal: sent by addSynthTrack()
        InstallDrums,   // internal: sent by addDrumTrack()
        RemoveInstrument, // internal: sent by removeTrack()
        SetTrackImage,  // internal: sent by setTrackImage()
        SetLaneSample,  // internal: sent by setLaneSample()
        InstallEffect,  // internal: sent by setEffect()
        SetTrackSample, // internal: sent by setTrackSample()
        SetLanePart,    // how much of its sound each hit on a drum lane plays: track, a = lane,
                        //   value = 0..1 of its length (0: all of it). A slice of a longer recording.
        SetStepFx       // a hit's step effects (StepFx, drums.hpp): track, scene, bar, a = lane, b = step,
                        //   c = pitch in semitones, d = ratchet, value = 1 for backwards, extra = cut (0..3)
    };
    Type type = Trigger;
    int track = 0;
    int scene = 0;
    int bar = 0;             // which bar of the cell a pattern command writes: 0..7
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    float value = 0.f;
    float extra = 0.f;       // SetNote only
    float shift = 0.f;       // SetNote only: the note early or late, -0.5..0.5 of a step
    int e = 0;               // SetNote only: what the note does besides play. Its low byte: a ratchet (2..4:
                             //   that many notes, evenly, inside its first step). 256 added: it glides to its
                             //   pitch from the note before it.
    void* pointer = nullptr; // InstallSynth only
};

// Everything a track plays, in one piece: for loading a whole song in a
// handful of commands instead of thousands.
// Automation: a setting that moves by itself as the pattern plays. Each
// track has sixteen "lanes" of it: its synth's eight macros, and the four
// settings of each of its two effects. A lane holds one value for every step
// of every bar of a cell, or "nothing here" (below zero), and where there is
// a value the setting glides to it over the first half of that step. Where
// there is none the setting is wherever its knob was left.
constexpr int kAutoLanes = 16;                     // 0..7 macros; 8 + slot * 4 + setting: effects
constexpr int kAutoSteps = kMaxBars * kSteps;      // a cell at its longest
constexpr float kAutoNone = -1.f;
inline int autoLaneOfEffect(int slot, int param) { return 8 + slot * 4 + param; }

struct TrackImage {
    // Indexed by pattern slot: scene * kMaxBars + bar. Slot 0 is scene 1, bar 1.
    float level[kPatternSlots][kDrumLanes][kSteps] = {};
    float nudge[kPatternSlots][kDrumLanes][kSteps] = {};
    float chance[kPatternSlots][kDrumLanes][kSteps];
    float start[kPatternSlots][kDrumLanes][kSteps] = {};
    int bars[kMaxScenes]; // how long the track's cell is in each scene
    struct Note { std::uint8_t step = 0, pitch = 0; float length = 0.f; float velocity = 0.f; float nudge = 0.f; std::uint8_t ratchet = 0, glide = 0; }; // length in steps, not only whole ones
    Note notes[kPatternSlots][kMaxNotes];
    // Automation, [scene][lane][bar * 16 + step], or null for none anywhere.
    std::unique_ptr<float[]> automation;
    float laneLevel[kDrumLanes];
    float lanePart[kDrumLanes] = {}; // how much of its sound each hit on a lane plays (0: all); see Command::SetLanePart
    StepFx fx[kPatternSlots][kDrumLanes][kSteps]; // each hit's step effects
    TrackImage() {
        for (auto& scene : chance)
            for (auto& lane : scene)
                for (float& c : lane) c = 1.f;
        for (float& l : laneLevel) l = 1.f;
        for (int& b : bars) b = 1;
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
    // From the control thread, at any time: put an effect in a slot (None
    // empties it), at its default settings. `track` may be kMasterTrack.
    // It does not click: the sound slides from what was in the slot to what
    // is there now over kSwapFadeSeconds, and the old one's tail goes with it.
    // Its settings are then SetEffectParam, each 0..1 (see effects.hpp).
    bool setEffect(int track, int slot, EffectKind kind);
    // Put a recording on one lane of a drum track in place of the built-in
    // drum, or nullptr to bring the drum back. False if the track has no drums.
    bool setLaneSample(int track, int lane, std::unique_ptr<SampleData> sample);
    // Give a synth track's instrument a recording to play (the sampler), or
    // nullptr to take it away. False if the track has no synth.
    bool setTrackSample(int track, std::unique_ptr<SampleData> sample);
    // Free what the audio thread has handed back (removed synths, used
    // images). Call now and then from the control thread.
    void collectGarbage();

    // From the control thread. Returns false if the queue is full.
    bool send(const Command& command) {
        if (commands_.push(command)) return true;
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // How many commands found the queue full and were lost, ever. The queue is
    // emptied at the start of every render(), so this only moves when the audio
    // thread has stood still for seconds. When it does move, the engine no longer
    // matches what was sent: the caller sends everything again once caughtUp().
    unsigned dropped() const { return dropped_.load(std::memory_order_relaxed); }
    bool caughtUp() const { return commands_.empty(); }
    // A second way in, for ONE other thread (the one reading a USB keyboard):
    // notes and drum hits to play now, and nothing else. They are taken up
    // at the start of the next render(), after everything sent with send().
    bool sendLive(const Command& command) {
        if (command.type != Command::NoteOn && command.type != Command::NoteOff && command.type != Command::Trigger) return false;
        return live_.push(command);
    }

    // From the audio thread. Writes `frames` interleaved stereo frames (L R L R ...).
    void render(float* interleaved, int frames);

    // Read-outs for the screen. Safe from any thread.
    float trackPeak(int track) const; // loudest sample in the last render() call
    int playStep() const { return playStep_.load(std::memory_order_relaxed); }   // 0..15, or -1 when stopped
    int playScene() const { return playScene_.load(std::memory_order_relaxed); } // 0..15, or -1 when stopped
    int playRepeat() const { return playRepeat_.load(std::memory_order_relaxed); } // 0-based pass through the scene
    int playBar() const { return playBar_.load(std::memory_order_relaxed); }       // 0-based bar of the scene

    // The scope: the last samples one track put out, after its effects.
    void setScopeTrack(int track) { scopeTrack_.store(track, std::memory_order_relaxed); } // -1: none
    void readScope(float* out, int count) const; // the newest `count` samples (up to kScopeSize), oldest first

    float sampleRate() const { return sampleRate_; }
    // Catch: the engine remembers the last ten seconds it put out (both
    // sides mixed to one). This copies out the last whole bar the song
    // played, exactly, bar line to bar line, also just after it has been
    // stopped. If there is no such bar (the song is in its first bar, or
    // something has been played by hand since it stopped, or there was a
    // panic) it is the last bar's worth of sound up to now. From the control thread; allocates.
    std::vector<float> lastBar() const;

    // True from a Panic until the output has opened again.
    bool silenced() const { return silenced_.load(std::memory_order_relaxed); }

    // ---- more than one processor core ---------------------------------------------------
    // The tracks do not depend on each other until they are mixed, so they
    // can be worked out side by side. With helpers, each chunk of sound is
    // shared out: the helpers and the audio thread each take the next track
    // not yet done, until none is left, and only then are they mixed. The
    // sound is the same to the last bit as without (a test holds it to that).
    // `count` is how many helpers besides the audio thread; 0 turns it off.
    // `atStart` runs once on each helper as it begins (to ask for realtime
    // priority, which only the program knows how to do). Not for the audio thread.
    void useWorkers(int count, void (*atStart)() = nullptr);

    // Flush-to-zero (denormals.hpp) while rendering, on the audio thread and
    // every helper: a fading tail cannot slow the processor down. OFF unless
    // asked for, because it moves the last vanishing samples of a tail to
    // zero (all of it below 1e-30; see denormals.hpp), and a song must come out the
    // same to the last bit until the owner says otherwise. From any thread.
    void setFlushDenormals(bool on) { flush_.store(on, std::memory_order_relaxed); }
    bool flushesDenormals() const { return flush_.load(std::memory_order_relaxed); }
    int workers() const { return static_cast<int>(workers_.size()); }

    // ---- keeping up ---------------------------------------------------------------
    // A sound that takes longer to work out than it lasts cannot be played:
    // the output stutters. With the watch on, render() times itself, and
    // when it is falling behind it tells the synths to do plainer work
    // (Twin Analog's oversampling, a notch at a time) until it keeps up.
    // The player's settings are not changed. It is off unless turned on, so
    // that tests and renders that do not run in real time are never touched.
    void watchLoad(bool on) { watchLoad_ = on; }
    // 3: nothing held back. 0: plainest. Lower than 3 means "it could not keep up".
    int qualityLimit() const { return qualityLimit_.load(std::memory_order_relaxed); }
    // How long the last render() took, as a part of the time it had (1 = all of it).
    float load() const { return load_.load(std::memory_order_relaxed); }
    // What render() does with each timing. Public for the tests: `part` is
    // time taken over time available, for a block of `frames`.
    void noteLoad(float part, int frames);

private:
    struct DrumScene {
        float level[kDrumLanes][kSteps] = {}; // 0 = no hit
        float nudge[kDrumLanes][kSteps] = {}; // -0.5..0.5 of a step
        float chance[kDrumLanes][kSteps];     // 0..1, set to 1 in the constructor
        float start[kDrumLanes][kSteps] = {}; // 0..1 of the sound's length
        StepFx fx[kDrumLanes][kSteps];        // the step effects: pitch, ratchet, backwards, cut
    };
    struct Note {
        std::uint8_t step = 0, pitch = 0;
        float length = 0.f;   // in steps; a note may end part-way through one
        float nudge = 0.f;    // early or late, -0.5..0.5 of a step
        float velocity = 0.f; // 0 = empty slot
        std::uint8_t ratchet = 0; // 2..4: that many notes, evenly, inside its first step
        std::uint8_t glide = 0;   // 1: it slides to its pitch from the note before it on the track
    };
    // An effect going into a slot, or coming out of one, is faded: for
    // kSwapFadeSeconds the one that was there (or the plain sound, if the slot
    // was empty) and the one that is there now both run, and the sound slides
    // from the first to the second. Outside a fade a slot costs what it did.
    struct SlotFade {
        std::unique_ptr<Effect> leaving; // what was in the slot; nullptr: it was empty
        int left = 0;                    // frames of the fade still to go; 0: no fade
        int total = 1;
    };
    void swapEffect(std::unique_ptr<Effect>& place, SlotFade& fade, Effect* incoming);
    static void runSlot(std::unique_ptr<Effect>& place, SlotFade& fade, float* left, float* right, int frames);
    void binLeaving(SlotFade& fade); // the audio thread only, with no helper at work

    struct Track {
        bool hasDrums = false;
        DrumMachine drums;
        SampleData* laneSamples[kDrumLanes] = {}; // owned; freed by the control thread
        SampleData* trackSample = nullptr;        // likewise: a sampler's recording
        std::unique_ptr<Instrument> synth; // set on a synth track
        // The patterns are large (every bar of every scene), so they live
        // on the heap: an Engine can then sit on the stack.
        std::unique_ptr<DrumScene[]> drumScenes{new DrumScene[kPatternSlots]};
        std::unique_ptr<Note[][kMaxNotes]> notes{new Note[kPatternSlots][kMaxNotes]};
        std::uint8_t bars[kMaxScenes]; // the cell's length in each scene
        // Automation (see kAutoLanes). `automation` is [scene][lane][step of the cell].
        std::unique_ptr<float[]> automation{new float[kMaxScenes * kAutoLanes * kAutoSteps]};
        float autoBase[kAutoLanes]; // where each knob was left; below zero: not known (an effect not yet told)
        float autoNow[kAutoLanes];  // what the synth or effect has been given
        float autoFrom[kAutoLanes], autoTo[kAutoLanes];
        bool autoGliding[kAutoLanes] = {};
        float* autoAt(int scene, int lane) { return automation.get() + (scene * kAutoLanes + lane) * kAutoSteps; }
        // Synth notes sounding from the pattern: samples until each is let
        // go, to the sample. kNoNote: that key is not sounding.
        // Notes waiting for their moment: late ones from the step that just
        // began, early ones that belong to the next.
        struct PendingNote { double samplesLeft = 0.0; std::uint8_t pitch = 0, glide = 0; float velocity = 0.f, length = 0.f; };
        int lastPitch = -1; // the key of the note the track last began, for a glide to start from (-1: none since Play)
        PendingNote pendingNotes[24];
        double offIn[128];
        Track() {
            for (double& off : offIn) off = kNoNote;
            for (auto& b : bars) b = 1;
        }
        // Hits waiting for their moment: a late hit from the step that just
        // began, and an early hit that belongs to the next one.
        // A ratchet waits here too: `more` hits after the next, `gap` samples apart.
        struct Pending {
            double samplesLeft = 0.0, gap = 0.0;
            float velocity = 0.f, start = 0.f, part = -1.f;
            HitPlay how;
            int more = 0;
        };
        Pending pending[kDrumLanes][3];
        float level = 0.65f; // one full hit stays clean; hits that land together push the master
        bool heard = true;   // not muted (SetTrackHeard)
        float gate = 1.f;    // ...as it is this sample: moving toward 1 or 0
        std::unique_ptr<Effect> effects[kEffectSlots];
        SlotFade fades[kEffectSlots];
        std::atomic<float> peak{0.f};
        bool hasInstrument() const { return hasDrums || synth; }
    };
    struct Position { int scene, repeat, bar; };
    int sceneBars(int scene) const;                 // the longest cell in it
    int sceneSteps(int scene) const;                // how many steps it lasts
    int slotOf(const Track& track, int scene, int bar) const { return scene * kMaxBars + bar % track.bars[scene]; }
    void fireNoteOffs();
    void firePendingNotes();
    void startNote(Track& track, const Note& note, double delaySamples);

    void apply(const Command& command);
    Position afterThisBar() const;
    void fireStep();
    void applyAuto(Track& track, int lane, float value);
    void glideAutomation(int frames);
    void settleAutomation();
    double autoPhase_ = 0.0; // how far through the present step, 0..1
    void playHit(Track& track, int lane, float velocity, float start, float part, const StepFx& fx, double delaySamples);
    void schedule(Track& track, int lane, const DrumScene& scene, int step, double delaySamples);
    void firePending();
    void clearPending();
    void letGoOfNotes();
    void renderChunk(float* interleaved, int frames, float* peaks);
    void renderTrack(int t);   // one track's chunk into trackL_ / trackR_, with its effects, level and mute
    void shareOutTracks();     // take tracks from the list until none is left
    void workerLoop(void (*atStart)());
    // A chunk being worked on: what the helpers need to know.
    float trackL_[kNumTracks][kMaxBlock], trackR_[kNumTracks][kMaxBlock];
    int chunkFrames_ = 0, chunkScope_ = -1;
    float* chunkPeaks_ = nullptr;
    int active_[kNumTracks] = {};
    std::atomic<int> activeCount_{0}, nextActive_{kNumTracks}, finished_{0};
    std::vector<std::thread> workers_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    int wakeups_ = 0;          // chunks the helpers have been called to and not yet come for
    std::atomic<bool> quitWorkers_{false};
    std::atomic<bool> flush_{false};

    float sampleRate_ = 48000.f;
    Track tracks_[kNumTracks];
    bool claimed_[kNumTracks] = {};
    bool claimedDrums_[kNumTracks] = {}; // ...and which of those are drums // control thread's view of which tracks have an instrument
    std::unique_ptr<Effect> masterEffects_[kEffectSlots];
    SlotFade masterFades_[kEffectSlots];

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
    int bar_ = 0;    // the bar of the scene now playing
    int sceneCount_ = 1;
    int repeats_[kMaxScenes];
    int steps_[kMaxScenes]; // how many steps each scene lasts, if it has been cut short (0: every step of its longest cell)
    std::atomic<int> playStep_{-1};
    std::atomic<int> playScene_{-1};
    std::atomic<int> playRepeat_{0};
    std::atomic<int> playBar_{0};
    // The gate at the very end of the chain, for Panic.
    enum class Gate { Open, Closing, Shut, Opening };
    Gate gate_ = Gate::Open;
    float gateGain_ = 1.f;
    bool panicHeld_ = false;
    bool cutEverything_ = false; // the gate has just shut: cut the drums and clear the effects
    int shutFrames_ = 0, quietFrames_ = 0;
    std::atomic<bool> silenced_{false};
    bool watchLoad_ = false;
    std::atomic<int> qualityLimit_{3};
    std::atomic<float> load_{0.f};
    int busyBlocks_ = 0;      // blocks in a row that took nearly all their time
    long long calmFrames_ = 0; // frames in a row with time to spare

    float volume_ = 1.f, volumeNow_ = 1.f; // where it is going, and where it has got to (it glides, so a change does not click)
    std::unique_ptr<std::atomic<float>[]> caught_; // a ring of kCatchFrames
    std::atomic<std::uint64_t> caughtCount_{0};    // frames ever written to it
    std::atomic<std::uint64_t> barEndedAt_{0};     // caughtCount_ when the last whole bar ended
    std::atomic<int> barFrames_{0};                // how long that bar was; 0: none yet
    std::uint64_t barBeganAt_ = 0;
    std::atomic<int> barFramesNow_{96000};         // a bar at the present tempo

    std::uint32_t dice_ = 0x2545F491u; // for chance; runs on, so no two bars need match

    std::atomic<int> scopeTrack_{-1};
    std::atomic<float> scope_[kScopeSize];
    std::atomic<unsigned> scopeWritten_{0};

    SpscQueue<Command, 16384> commands_; // room for whole cells sent at once
    SpscQueue<Command, 1024> live_;      // from the keyboard's thread (sendLive)
    std::atomic<unsigned> dropped_{0};   // commands lost to a full queue

    // Things the audio thread no longer needs and must not free itself.
    struct Trash { bool isImage = false; void* pointer = nullptr; bool isSample = false; bool isEffect = false; };
    SpscQueue<Trash, 64> trash_;
    static void destroy(const Trash& trash);
};

} // namespace apotheca
