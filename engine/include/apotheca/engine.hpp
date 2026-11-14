#pragma once
// The audio engine. No screen, no device, no SDL: it turns commands into
// stereo samples, so it can be tested on its own.
//
// Signal path:
//   track instrument -> slot 0 -> slot 1 -> slot 2 -> level, mute -+-> mix
//   (up to 12)                                                     |
//                                              x its ECHO amount  -+-> ECHO bus  -> its effect -+
//                                              x its SPACE amount -+-> SPACE bus -> its effect -+-> mix
//
//   mix -> master slot 0 -> master slot 1 -> ceiling -> out
//
// The two buses are shared: every track has an amount it sends to each (0 to
// start), and each bus holds one effect which gives back its echoes or its
// room and none of the sound that went in. A bus nothing has been sent to
// costs nothing.
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
#include "apotheca/chord_rule.hpp"
#include "apotheca/glide_rule.hpp"
#include "apotheca/instrument.hpp"
#include "apotheca/phazed.hpp"
#include "apotheca/sampler.hpp"
#include "apotheca/qmod.hpp"
#include "apotheca/twin.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

constexpr int kNumTracks = 12;
constexpr int kTrackSlots = 3;    // effect slots on a track
constexpr int kMasterSlots = 2;   // ...and on the master
constexpr int kMasterTrack = -1;  // "track" index that addresses the master
// The send buses. Each has ONE slot (slot 0), addressed like the master's:
// SetEffectParam, InstallEffect and setEffect() take these as the "track".
constexpr int kBuses = 2;
constexpr int kEchoBus = -2;      // bus 0: a delay
constexpr int kSpaceBus = -3;     // bus 1: a reverb
inline int busTrack(int bus) { return kEchoBus - bus; }                       // 0 -> kEchoBus, 1 -> kSpaceBus
inline int busOfTrack(int track) { return track == kEchoBus ? 0 : (track == kSpaceBus ? 1 : -1); } // -1: not a bus
// How many effect slots the thing a "track" number names has; 0 if it names nothing.
inline int slotsOf(int track) { return track == kMasterTrack ? kMasterSlots : (busOfTrack(track) >= 0 ? 1 : (track >= 0 && track < kNumTracks ? kTrackSlots : 0)); }
// A bus that has been sent nothing, and has put out nothing above
// kBusQuiet, for this long is put to rest: its effect is cleared and it is
// skipped until something is sent to it again. Longer than the longest gap
// between two echoes of anything a bus can hold (effects_test checks that).
constexpr float kBusRestSeconds = 10.f;
constexpr float kBusQuiet = 1e-6f; // 120 dB under full scale
constexpr float kBusLoudest = 1e8f; // sent to a bus, anything beyond this (or not a number) is taken as silence
// A TAIL ALWAYS ENDS. From the last sound a bus is sent to the bus at rest is
// never longer than this, whatever effect it holds and however it is set, at
// any tempo (effects_test measures every one: testATailAlwaysEnds). A bounce's
// cap is longer than this, so no ONE effect runs into it. Effects in a row can
// (a howling TAPE in a slot keeps a TAPE on a bus fed: about 710 s at 40 BPM),
// and so can anything broken: the cap is what ends those.
// Measured 2026-11-12, at 40 BPM, a full-scale burst sent: TAPE 418 s (a half
// note, FEEDBACK full: 30 s of holding, then DELAY's own fall), DELAY 388 s,
// HALL 44 s, REVERB 36 s. At 120 BPM the two delays take a third as long.
// (SPRING, 2026-11-13: 18.5 s.)
constexpr float kLongestTailSeconds = 450.f;
constexpr float kSendGlideSeconds = 0.005f; // a send amount moves to a new setting over this long, so it does not click
constexpr int kMaxBlock = 64;     // internal chunk size, in frames
constexpr float kSwapFadeSeconds = 0.010f; // an effect put in a slot, changed or taken out fades over this long
constexpr float kMinTempo = 40.f;
constexpr float kMaxTempo = 240.f;
constexpr int kMaxScenes = 16;
constexpr int kMaxRepeats = 16;
constexpr int kMaxBars = 8;       // the longest a cell (one track in one scene) can be
constexpr int kPatternSlots = kMaxScenes * kMaxBars; // a bar of a scene is slot scene * kMaxBars + bar
constexpr int kMaxNotes = 64;     // notes in one scene of one synth track
static_assert(kGlideNotes == kMaxNotes && kGlideBars == kMaxBars, "glide_rule.hpp: a cell is as big there as here");
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
                        //   and, as in SetStepFx, 4 times its THROW added to `extra`
        SetStep,        // write one pattern step:    track, scene, a = lane, b = step, value = velocity (0 clears)
        Play,           // start from step 1:         a = scene to start on, b = 1 to loop that scene alone,
                        //   c = 1: the song is played once through and then stopped (a bounce), so its last
                        //   step begins nothing early that belongs to its first
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
        SetEffectParam, // track (or kMasterTrack, kEchoBus, kSpaceBus), a = slot, b = param, value
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
        SetStepFx,      // a hit's step effects (StepFx, drums.hpp): track, scene, bar, a = lane, b = step,
                        //   c = pitch in semitones, d = ratchet, value = 1 for backwards, extra = cut (0..3)
                        //   plus 4 times its THROW (0..3: off, ECHO, SPACE, both; drums.hpp). THROW rides with
                        //   the cut because the command has no field to spare.
        SetSend,        // how much of a track goes to a bus: track, a = bus (0 ECHO, 1 SPACE), value = 0..1.
                        //   Taken after the track's level and mute. It glides, so it does not click.
        ScenesMoved,    // internal: sent by moveScenes(). The scenes from `a` on are one place further down
                        //   the song (b = 1) or one back up it (b = -1): a scene has been put in, or taken
                        //   out. If one of them is playing, the playing goes with it and nothing else
                        //   changes: the same step, the same bar, the same time round, no note cut or
                        //   started. The patterns go with their scenes in the same command (the engine
                        //   keeps each scene of each track apart and only changes which is which), with
                        //   the scene that is new: c = the times it plays, d = its steps, scene = how many
                        //   scenes the song now has. (With no pointer: the playing moves and nothing else.)
        SetGlideFrom    // GLIDE FROM, which key of the moment before a synth track's glides come from:
                        //   track, a = kFromTop, kFromPaired, kFromNearest, kFromRandom (glide_rule.hpp;
                        //   a number not known is TOP). Notes that begin after it is taken go by it.
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
                             //   pitch from the note before it. 512 added: it is thrown to ECHO; 1024: to
                             //   SPACE (THROW: the track's send to that bus is full for as long as the note lasts).
                             //   2048 times the glide's length (kGlideQuarter and on, glide_rule.hpp) added,
                             //   with the 256: none of it there, the 256 alone is half a step, as it always was.
    void* pointer = nullptr; // InstallSynth only
    ChordPlay play;          // SetNote only: the note is one of a chord's, and what the chord does besides sound
                             //   all at once (chord_rule.hpp). Nothing set: a note like any other.
};

// Everything a track plays, in one piece: for loading a whole song in a
// handful of commands instead of thousands.
// Automation: a setting that moves by itself as the pattern plays. Each
// track has twenty-four "lanes" of it: its synth's eight macros, the four
// settings of each of its three effects, and its two sends (two more are
// spare). A lane holds one value for every step
// of every bar of a cell, or "nothing here" (below zero), and where there is
// a value the setting glides to it over the first half of that step. Where
// there is none the setting is wherever its knob was left.
constexpr int kAutoLanes = 24;                     // 0..7 macros; 8 + slot * 4 + setting: effects (8..19); 20, 21 sends; 22, 23 spare
constexpr int kAutoSendLane = 20;                  // the ECHO send's lane; SPACE's is the next
constexpr int kAutoSteps = kMaxBars * kSteps;      // a cell at its longest
constexpr float kAutoNone = -1.f;
inline int autoLaneOfEffect(int slot, int param) { return 8 + slot * 4 + param; }
inline int autoLaneOfSend(int bus) { return kAutoSendLane + bus; }

struct TrackImage {
    // Indexed by pattern slot: scene * kMaxBars + bar. Slot 0 is scene 1, bar 1.
    float level[kPatternSlots][kDrumLanes][kSteps] = {};
    float nudge[kPatternSlots][kDrumLanes][kSteps] = {};
    float chance[kPatternSlots][kDrumLanes][kSteps];
    float start[kPatternSlots][kDrumLanes][kSteps] = {};
    int bars[kMaxScenes]; // how long the track's cell is in each scene
    struct Note { std::uint8_t step = 0, pitch = 0; float length = 0.f; float velocity = 0.f; float nudge = 0.f; std::uint8_t ratchet = 0, glide = 0, throwTo = 0; ChordPlay play; }; // length in steps, not only whole ones
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

// One scene of one track, as TrackImage holds sixteen: for Engine::moveScenes.
struct SceneImage {
    float level[kMaxBars][kDrumLanes][kSteps] = {};
    float nudge[kMaxBars][kDrumLanes][kSteps] = {};
    float chance[kMaxBars][kDrumLanes][kSteps];
    float start[kMaxBars][kDrumLanes][kSteps] = {};
    StepFx fx[kMaxBars][kDrumLanes][kSteps];
    int bars = 1; // how long the track's cell is in it
    TrackImage::Note notes[kMaxBars][kMaxNotes];
    float automation[kAutoLanes * kAutoSteps]; // [lane][bar * 16 + step]
    SceneImage() {
        for (auto& bar : chance)
            for (auto& lane : bar)
                for (float& c : lane) c = 1.f;
        for (float& v : automation) v = kAutoNone;
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
    void setBusEffect(int bus, std::unique_ptr<Effect> effect); // told it is on a bus: it gives back no dry sound

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
    // A scene put into the song, or one taken out, in ONE step for the audio thread, which
    // copies nothing: it changes which scene is which, and takes in the one scene that is new,
    // made ready here. by = 1: a scene goes in at `from`, those from there on move down one and
    // the last falls off the end. by = -1 (from 1 up): the scene before `from` goes, those from
    // `from` on move up one, and the new one is the last. `scenes` is that new scene for each of
    // the kNumTracks tracks (nullptr for a track with nothing: it gets whichever scene went);
    // `repeats` and `steps` are its own, and `sceneCount` the song's from then on. The playing
    // goes with a scene that moves (see Command::ScenesMoved). Allocates; false if the queue is full.
    bool moveScenes(int from, int by, const std::unique_ptr<SceneImage>* scenes, int sceneCount, int repeats, int steps);
    // From the control thread, at any time: put an effect in a slot (None
    // empties it), at its default settings. `track` may be kMasterTrack, or
    // kEchoBus / kSpaceBus (slot 0 only): an effect on a bus is told so, and
    // gives back only what it adds, its MIX being how loud that is.
    // It does not click: the sound slides from what was in the slot to what
    // is there now over kSwapFadeSeconds, and the old one's tail goes with it.
    // Its settings are then SetEffectParam, each 0..1 (see effects.hpp).
    // A full queue is counted in dropped(), for the starting settings too (so
    // true can still mean those were lost; dropped() says, and the app resends).
    bool setEffect(int track, int slot, EffectKind kind);
    // Put a recording on one lane of a drum track in place of the built-in
    // drum, or nullptr to bring the drum back. False if the track has no drums,
    // or the queue is full (counted in dropped(), as every lost command is).
    bool setLaneSample(int track, int lane, std::unique_ptr<SampleData> sample);
    // Give a synth track's instrument a recording to play (the sampler), or
    // nullptr to take it away. False if the track has no synth, or the queue is full (counted).
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
    // Things the audio thread handed back that found the trash full: waiting to
    // be handed on (trashHeld), or lost to a leak because there was no room even
    // to wait (trashLost; never seen, it would take thousands in one frame).
    int trashHeld() const { return heldNow_.load(std::memory_order_relaxed); }
    unsigned trashLost() const { return trashLost_.load(std::memory_order_relaxed); }
    bool caughtUp() const { return commands_.empty() && !holding_.load(std::memory_order_acquire); }
    // A track taken away while it sounds fades over this long first (no click). Commands sent
    // after the taking away wait for the fade: they are applied in the order sent.
    static constexpr float kGoneFadeSeconds = 0.005f;
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
    bool busAwake(int bus) const { return bus >= 0 && bus < kBuses && busAwake_[bus].load(std::memory_order_relaxed); } // false: at rest, costing nothing
    float trackPeak(int track) const; // loudest sample in the last render() call
    // What the effect in a slot has to show: for COMP, how many dB it is
    // turning the sound down just now (0: not at all; 6: to half). 0 for an
    // empty slot and for an effect with nothing to show. `track` may be
    // kMasterTrack. As of the last chunk rendered; no lock, any thread.
    float effectMeter(int track, int slot) const;
    int playStep() const { return playStep_.load(std::memory_order_relaxed); }   // 0..15, or -1 when stopped
    // 0..15, or -1 when stopped. With it, in one read, how many Command::ScenesMoved the audio
    // thread has taken so far (counted round at kMovesHeardRound): a caller that has sent more
    // knows this scene number is from before the ones still on their way.
    static constexpr unsigned kMovesHeardRound = 1u << 20;
    int playScene(unsigned* movesHeard = nullptr) const {
        const unsigned both = playScene_.load(std::memory_order_relaxed);
        if (movesHeard) *movesHeard = both >> 8;
        return static_cast<int>(both & 255u) - 1;
    }
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
        std::uint8_t glide = 0;   // it slides to its pitch from the note before it on the track: for how long
                                  // (kGlideHalf and the rest, glide_rule.hpp). 0: it does not.
        std::uint8_t piece = 0;   // inside the engine: 2..4, the first of a ratchet of that many (its slide is
                                  // over by the second); kLaterPiece, one of the rest of a ratchet.
        std::uint8_t throwTo = 0; // THROW: the buses the track's send is opened to while it sounds (kThrowEcho, kThrowSpace)
        float hold = 0.f;         // inside the engine: how long that send stays open, in steps (the whole note, also
                                  // when it is the first piece of a ratchet). 0: for its own length.
        ChordPlay play;           // a chord's note: which of its keys, and the chord's effects (chord_rule.hpp)
        std::uint8_t how = 0;     // inside the engine: kUntold, kTellOnly, kWholeFrames (below)
    };
    // Everything one track plays in one scene.
    struct SceneStore {
        DrumScene drums[kMaxBars];
        Note notes[kMaxBars][kMaxNotes];
        float automation[kAutoLanes * kAutoSteps]; // [lane][step of the cell]
        std::uint8_t bars = 1;                     // the cell's length
        SceneStore() {
            for (DrumScene& bar : drums)
                for (auto& lane : bar.chance)
                    for (float& c : lane) c = 1.f;
            for (float& v : automation) v = kAutoNone;
        }
    };
    // What Command::ScenesMoved carries: the scene that is new, for each track (or nullptr). The
    // audio thread puts in their place the ones the song has no more use for, and hands it back.
    struct SceneMove {
        SceneStore* scenes[kNumTracks] = {};
        ~SceneMove() {
            for (SceneStore* scene : scenes) delete scene;
        }
    };
    // One scene's patterns read in, as the engine keeps them (from a TrackImage, or a SceneImage).
    static void sceneIn(SceneStore& to, const float (*level)[kDrumLanes][kSteps], const float (*nudge)[kDrumLanes][kSteps], const float (*chance)[kDrumLanes][kSteps],
                        const float (*start)[kDrumLanes][kSteps], const StepFx (*fx)[kDrumLanes][kSteps], const TrackImage::Note (*notes)[kMaxNotes], int bars, const float* automation);
    // A chord whose notes do not all begin on its own moment (strummed, an arpeggio, HUMAN TIME)
    // is still ONE moment to a glide: what follows slides from its keys as written (which of
    // them, the track's GLIDE FROM says), as the roll draws.
    // So those notes say nothing to the track's GlideTrail when they sound (kUntold), and the
    // trail is told of each key at the chord's own moment by a note that only tells (kTellOnly).
    // kWholeFrames: everything about the note is timed in whole samples (its ratchet's pieces and
    // its end too), so it is the same performance however the sound is cut into blocks.
    static constexpr std::uint8_t kUntold = 1, kTellOnly = 2, kWholeFrames = 4;
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
    // `bus`: the slot is a bus's, where "no effect" is silence and not the sound passing through.
    // `hits`: the lanes of the first drum track hit on this chunk's first sample (chunkHits_), for a DUCK.
    static void runSlot(std::unique_ptr<Effect>& place, SlotFade& fade, float* left, float* right, int frames, bool bus = false, unsigned hits = 0);
    void binLeaving(SlotFade& fade); // the audio thread only, with no helper at work

    struct Track {
        bool hasDrums = false;
        DrumMachine drums;
        SampleData* laneSamples[kDrumLanes] = {}; // owned; freed by the control thread
        SampleData* trackSample = nullptr;        // likewise: a sampler's recording
        std::unique_ptr<Instrument> synth; // set on a synth track
        // The patterns are large (every bar of every scene), so they live
        // on the heap: an Engine can then sit on the stack.
        // Each scene is a piece of its own, so that a scene put into the song or taken out
        // (Command::ScenesMoved) is sixteen pointers moved and nothing copied. What follows
        // reads them as the one run they used to be: by pattern slot, by scene, by place.
        SceneStore* scenes[kMaxScenes];
        struct DrumsOf {
            SceneStore* const* scenes;
            DrumScene& operator[](int slot) const { return scenes[slot / kMaxBars]->drums[slot % kMaxBars]; }
        } drumScenes{scenes};
        struct NotesOf {
            SceneStore* const* scenes;
            auto operator[](int slot) const -> Note (&)[kMaxNotes] { return scenes[slot / kMaxBars]->notes[slot % kMaxBars]; }
        } notes{scenes};
        struct BarsOf { // the cell's length in each scene
            SceneStore* const* scenes;
            std::uint8_t& operator[](int scene) const { return scenes[scene]->bars; }
        } bars{scenes};
        // Automation (see kAutoLanes). `automation` is [scene][lane][step of the cell].
        struct AutoOf {
            SceneStore* const* scenes;
            float& operator[](int i) const { return scenes[i / (kAutoLanes * kAutoSteps)]->automation[i % (kAutoLanes * kAutoSteps)]; }
        } automation{scenes};
        float autoBase[kAutoLanes]; // where each knob was left; below zero: not known (an effect not yet told)
        float autoNow[kAutoLanes];  // what the synth or effect has been given
        float autoFrom[kAutoLanes], autoTo[kAutoLanes];
        bool autoGliding[kAutoLanes] = {};
        float* autoAt(int scene, int lane) { return scenes[scene]->automation + lane * kAutoSteps; }
        // Synth notes sounding from the pattern: samples until each is let
        // go, to the sample. kNoNote: that key is not sounding.
        // Notes waiting for their moment: late ones from the step that just
        // began, early ones that belong to the next.
        struct PendingNote { double samplesLeft = 0.0; std::uint8_t pitch = 0, glide = 0, throwTo = 0; float velocity = 0.f, length = 0.f, hold = 0.f; std::uint8_t piece = 0; double at = 0.0; std::uint8_t how = 0; };
        GlideTrail trail; // the notes the track last began, for a glide to start from (none since Play)
        std::uint8_t glideFrom = 0; // GLIDE FROM: which of them (kFromTop and the rest, glide_rule.hpp)
        // Room for the most a step can ask: every note of the scene, ratcheted to the full and pushed late.
        // (Twenty-four ran out: an eight-key chord at x4, nudged, lost its last two keys.)
        PendingNote pendingNotes[kMaxNotes * kMaxRatchet];
        // An arpeggio (a chord whose STRUM is a length in steps): one note at a time, round the
        // chord's keys until the chord ends. One clock for the whole chord. Three, because at the most
        // one is still playing (nudged late), the next chord's waits for its own nudge and the
        // one after that has been asked a step ahead (it may be early); the one that begins ends
        // the one playing. (With two, the third took the clock of the one still playing.)
        struct Arp {
            bool on = false, begun = false;
            std::uint32_t seed = 0;       // which playing of which chord (chord_rule.hpp)
            ChordPlay play;
            std::uint8_t pitch[8] = {};   // by the key's place in the chord; `have`: which are there
            unsigned have = 0;
            int keys = 0;                 // once begun: how many, packed to the front of `pitch`
            std::uint8_t throwTo = 0;
            float velocity = 0.f;
            double length = 0.0;          // the chord's, in steps
            double wait = 0.0;            // samples until its next note
            double moved = 0.0;           // how far HUMAN TIME moved the note before, in samples
            long next = 0;                // that note's number: 0 the first
            int slot = -1, step = -1;     // where its chord is written: which bar of the song, which step
            bool look = false;            // that bar has been written to: see that the chord is still there
        };
        Arp arps[3];
        // The step (counted from Play) whose chord has been asked for a step ahead: that chord
        // is not begun again when its step comes.
        long chordAsked = -1;
        std::uint32_t lastChord = 0; // the chord the track last began (its seed), and how many it has
        long strokes = -1;           // begun since Play, less one: UP-DOWN strums up on the even ones
        double offIn[128];
        Track() {
            for (double& off : offIn) off = kNoNote;
            for (SceneStore*& scene : scenes) scene = new SceneStore;
        }
        ~Track() {
            for (SceneStore* scene : scenes) delete scene;
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
        // The lanes hit since the last chunk was worked out: bit 0 is lane 1. A hit
        // always lands on a chunk's first sample (render() ends a chunk where the
        // next one is due), so "which lanes" is all there is to say.
        unsigned hitsNow = 0;
        float level = 0.65f; // one full hit stays clean; hits that land together push the master
        bool heard = true;   // not muted (SetTrackHeard)
        float gate = 1.f;    // ...as it is this sample: moving toward 1 or 0
        float send[kBuses] = {};    // how much goes to each bus (SetSend, or its automation lane)
        float sendNow[kBuses] = {}; // ...as it is this sample: moving toward `send`
        // THROW on a note: the send to a bus is full for the note's length, whatever `send` says.
        double throwLeft[kBuses] = {}; // samples the send is still held open for (0: not)
        float throwNow[kBuses] = {};   // how far open it is this sample, 0..1: it moves over kSendGlideSeconds
        std::unique_ptr<Effect> effects[kTrackSlots];
        SlotFade fades[kTrackSlots];
        std::atomic<float> meters[kTrackSlots]; // what each slot's effect shows (effectMeter)
        std::atomic<float> peak{0.f};
        // Taken away while it still sounded: it fades to nothing over kGoneFadeSeconds (its
        // effects' tails too), and only then is its instrument taken away (Engine::takeAway).
        bool going = false;
        float goingGain = 1.f;
        bool hasInstrument() const { return hasDrums || synth; }
    };
    struct Position { int scene, repeat, bar; };
    int sceneBars(int scene) const;                 // the longest cell in it
    int sceneSteps(int scene) const;                // how many steps it lasts
    int slotOf(const Track& track, int scene, int bar) const { return scene * kMaxBars + bar % track.bars[scene]; }
    void fireNoteOffs();
    void firePendingNotes();
    void startNote(Track& track, const Note& note, double delaySamples, double at); // at: when it begins, in steps since Play
    // A chord's note with a chord effect set (chord_rule.hpp). `ahead`: asked a step before the
    // note's own step, as an early note is; `stepAt`: when that step begins, in steps since Play;
    // `wait`: samples from now until it does.
    void startChordNote(Track& track, int slot, const Note& note, bool top, double stepAt, double wait);
    void fireArps();

    void apply(const Command& command);
    void takeAway(int t);      // RemoveInstrument's work, at once (after its fade, if it sounded)
    void takeCommands();       // the queue, in order, up to a command that waits for a fade
    int going_ = 0;            // tracks fading before they are taken away (the audio thread's)
    Command held_;             // the first command after a taking away, waiting for the fade
    std::atomic<bool> holding_{false};
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
    // THROW on a drum hit: what each drum track's thrown hits put out this chunk, for each bus,
    // after the track's level and mute (and before its effect slots: it is taken inside the
    // drum machine). thrownMask_ says which were written (bit = bus); the rest hold nothing.
    float thrown_[kNumTracks][kBuses][kMaxBlock];
    int thrownMask_[kNumTracks] = {};
    void closeThrows(); // every note's THROW is over: the sends shut (in 5 ms)
    int chunkFrames_ = 0, chunkScope_ = -1;
    // DUCK: the lanes of the first drum track hit on this chunk's first sample.
    // Written by the audio thread before the tracks are shared out, only read
    // after that: a helper working on the drums changes nothing a helper
    // working on a ducked track looks at.
    unsigned chunkHits_ = 0;
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
    std::unique_ptr<Effect> masterEffects_[kMasterSlots];
    SlotFade masterFades_[kMasterSlots];
    std::atomic<float> masterMeters_[kMasterSlots];
    // The send buses: one effect each, wet only.
    std::unique_ptr<Effect> busEffects_[kBuses];
    SlotFade busFades_[kBuses];
    std::atomic<bool> busAwake_[kBuses]; // sent something, or still ringing; false: skipped
    int busQuietFrames_[kBuses] = {};    // how long it has been sent nothing and put out nothing
    // A bus's LEVEL is the engine's own (the effect's MIX stays at full): the rest
    // rule has to listen to the bus BEFORE its level, or a bus parked at LEVEL 0
    // with echoes in it would be taken for an empty one and cleared.
    int busMix_[kBuses] = {-1, -1};        // which setting of the effect it holds is its LEVEL; -1: none (a setup-call effect keeps its own)
    float busLevel_[kBuses] = {1.f, 1.f};  // where LEVEL is set
    float busLevelNow_[kBuses] = {1.f, 1.f}; // ...and as it is this sample (it moves like a send, kSendGlideSeconds)
    void feedBuses(const int* active, int count, int frames, float busL[][kMaxBlock], float busR[][kMaxBlock], bool* fed);
    void runBuses(int frames, float busL[][kMaxBlock], float busR[][kMaxBlock], const bool* fed, float* mixL, float* mixR);
    // The slot a "track" number and a slot number name, or nullptr.
    std::unique_ptr<Effect>* effectPlace(int track, int slot, SlotFade** fade = nullptr);

    // The step clock. Steps are sixteenth notes.
    bool playing_ = false;
    bool loopScene_ = false;
    bool once_ = false;     // the song is played once and then stopped (a bounce): nothing of its start is asked for at its end
    bool firstStep_ = true; // the step about to fire is the first since Play
    bool telling_ = false;  // inside fireStep, for a PAIRED track: its notes are only told to the trail as coming, not begun
    long stepsFired_ = 0;   // steps begun since Play: the clock a note's start is told to its track's GlideTrail by
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
    std::atomic<unsigned> playScene_{0}; // the scene playing + 1 in its low byte, movesHeard_ above it: one read gives both
    unsigned movesHeard_ = 0;            // Command::ScenesMoved taken so far (the audio thread's)
    void tellScene(int scene) { playScene_.store(((movesHeard_ % kMovesHeardRound) << 8) | static_cast<unsigned>(scene + 1), std::memory_order_relaxed); }
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

    // Things the audio thread no longer needs and must not free itself. Emptied
    // once a frame (collectGarbage). One song opened throws away at most about
    // 150 (12 tracks: a synth and its recording, or eight lane recordings; 12
    // track images; 40 effects fading out), so this holds several of those.
    struct Trash { bool isImage = false; void* pointer = nullptr; bool isSample = false; bool isEffect = false; bool isMove = false; };
    SpscQueue<Trash, 1024> trash_;
    static void destroy(const Trash& trash);
    // What found the trash full waits here (the audio thread's alone) and is
    // handed on at the end of a chunk (binHeld), as an effect fading out is.
    // Only if this is full too is a thing lost (leaked, and counted): never
    // kept in the engine, so a track never keeps an instrument it was told to drop.
    static const int kHeldTrash = 256;
    Trash heldTrash_[kHeldTrash];
    int heldCount_ = 0;
    std::atomic<int> heldNow_{0};
    std::atomic<unsigned> trashLost_{0};
    void bin(const Trash& trash); // the audio thread only, with no helper at work
    void binHeld();               // the same
};

} // namespace apotheca
