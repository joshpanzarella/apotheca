#pragma once
// The audio engine. No screen, no device, no SDL: it turns commands into
// stereo samples, so it can be tested on its own.
//
// Signal path:
//   track instrument -> slot 0 -> slot 1 -> slot 2 -> level, pan, mute -+-> mix
//   (up to 12)                                                          |
//                                                   x its ECHO amount  -+-> ECHO bus  -> its effect -+
//                                                   x its SPACE amount -+-> SPACE bus -> its effect -+-> mix
//
//   mix -> master slot 0 -> master slot 1 -> master level, pan -> ceiling -> out
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
// A song is up to kMaxScenes (64) scenes. In a scene each track has a cell of 1 to 8
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
#include "apotheca/feel_rule.hpp"
#include "apotheca/mod_rule.hpp"
#include "apotheca/glide_rule.hpp"
#include "apotheca/fill_rule.hpp"
#include "apotheca/load_meter.hpp"
#include "apotheca/instrument.hpp"
#include "apotheca/phazed.hpp"
#include "apotheca/sampler.hpp"
#include "apotheca/fm6.hpp"
#include "apotheca/jx3p.hpp"
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
constexpr int kGrainsShown = 24; // CLOUD's grains the engine keeps for its picture (cloud::kPool)
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
// A track's level as it always was (0.65: one full hit stays clean). The mixer's 0 dB.
constexpr float kUsualTrackLevel = 0.65f;
// PAN: the gains of the two sides for a place from -1 (hard left) to 1 (hard right). Equal power
// (the two gains squared add to 2 everywhere), with the middle at exactly 1 and 1: a track in the
// middle is not touched to the last bit. Hard to one side is that side at +3 dB, the other at nothing.
void panGains(float pan, float& left, float& right);
constexpr float kSendGlideSeconds = 0.005f; // a send amount moves to a new setting over this long, so it does not click
constexpr int kMaxBlock = 64;     // internal chunk size, in frames
constexpr float kSwapFadeSeconds = 0.010f; // an effect put in a slot, changed or taken out fades over this long
constexpr float kMinTempo = 40.f;
// The longest silence a delay leaves between two sounds of one tail: REVERSE at TIME 1/2 and 40 BPM, where a hit
// just after a window begins comes back at the end of the next window, two windows (6 s) later. A delay with no
// window waits one TIME at most (3 s). What waits for a tail to end must wait longer than this: the rest of a bus
// (kBusRestSeconds) and a bounce's quiet (kBounceQuietSeconds, app/session.hpp). effects_test measures every delay.
constexpr float kLongestEchoGapSeconds = 2.f * 8.f * 15.f / kMinTempo;
static_assert(kBusRestSeconds > kLongestEchoGapSeconds + 1.f, "a bus at rest must have waited longer than any gap in a tail");
constexpr float kMaxTempo = 240.f;
constexpr int kMaxScenes = 64;    // a song's scenes (scenes-plan.md: it was 16; one with nothing in it costs next to nothing)
constexpr int kOldSceneCount = 16; // what it was: a song file's `repeats` and `length` still have sixteen numbers at least
static_assert(kMaxScenes <= 64, "the engine keeps which scenes it has handed a store a bit a scene, in 64 bits (Engine::given_)");
constexpr int kMaxRepeats = 16;
constexpr int kMaxBars = 8;       // the longest a cell (one track in one scene) can be
constexpr int kPatternSlots = kMaxScenes * kMaxBars; // a bar of a scene is slot scene * kMaxBars + bar
constexpr int kMaxNotes = 64;     // notes in one scene of one synth track
static_assert(kGlideNotes == kMaxNotes && kGlideBars == kMaxBars, "glide_rule.hpp: a cell is as big there as here");
static_assert(kFillBars == kMaxBars, "fill_rule.hpp: a cell is as big there as here (its FILL is kept in its last bar's place)");
constexpr int kScopeSize = 1024;  // samples the scope keeps
constexpr double kNoNote = 1e30;

// Which places of a list may hold something: a bit each (speed plan, batch 1). A place that holds
// something always has its bit; a bit may stand on a place since emptied, and the next walk that
// finds it so takes it off. So a walk need look only where the bits are, in the places' order,
// which is the order a walk of every place found them in. (The step clock walked every synth
// track's 256 waiting-note places and 128 keys three times a chunk, nearly always empty: a quarter
// of the busiest song's cache misses.)
inline bool gWalkEveryPlace = false; // for the tests: every place is walked, bits or not (the old way, to compare with)
template <int N>
struct PlaceBits {
    static constexpr int kWords = (N + 63) / 64;
    std::uint64_t word[kWords] = {};
    void set(int i) { word[i >> 6] |= std::uint64_t(1) << (i & 63); }
    void clear(int i) { word[i >> 6] &= ~(std::uint64_t(1) << (i & 63)); }
    bool has(int i) const { return (word[i >> 6] >> (i & 63)) & 1u; }
    void clearAll() {
        for (std::uint64_t& w : word) w = 0;
    }
    // f(place) for each bit, lowest first. f may clear its own bit. (A bit set in a word already
    // read is not seen: no walk sets one.)
    template <typename F>
    void each(F&& f) const {
        if (gWalkEveryPlace) {
            for (int i = 0; i < N; ++i) f(i);
            return;
        }
        for (int k = 0; k < kWords; ++k)
            for (std::uint64_t w = word[k]; w; w &= w - 1) f(k * 64 + __builtin_ctzll(w));
    }
};
// CATCH (batch S3, 2026-11-25; it was ten seconds and one bar): what is caught is at most twenty seconds, the
// longest recording (owner, 2026-11-18), so 4 bars of 16 steps are caught down to 48 BPM, where they are 20 s
// exactly (at 40 BPM, 3 bars). The engine remembers 25 s of its own output, and of the track CATCH follows: the
// last whole bars end at the last bar line, and up to a bar has gone by since (5 s at 48 BPM). Each ring is
// 4.8 MB, made with the engine, on the heap (an Engine itself stays as it was on a stack).
constexpr int kCatchLongest = 960000; // the longest catch: twenty seconds at 48 kHz
constexpr int kCatchFrames = 1200000; // the rings: 25 seconds
constexpr int kCatchMostBars = 4;   // CATCH takes 1, 2 or 4 bars
constexpr int kBarLinesKept = 8;    // the bar lines remembered (more than kCatchMostBars + 1)
constexpr float kShortestNote = 0.05f; // of a step

struct Command {
    enum Type {
        Trigger,        // play a drum lane now:      track, a = lane, value = velocity,
                        //   b = where in the sound to start, in ten-thousandths of its length
                        //   c = how much of the sound to play, in ten-thousandths (0: as the lane is set)
                        //   and the step effects, to hear a hit as the pattern would play it: d = pitch in
                        //   semitones, bar = ratchet (2..4), scene = 1 for backwards, extra = cut (1, 2, 3)
                        //   and, as in SetStepFx, 4 times its THROW added to `extra`.
                        //   e = 1: an audition heard while X is held; StopAudition fades it
        SetStep,        // write one pattern step:    track, scene, a = lane, b = step, value = velocity (0 clears)
        Play,           // start from step 1:         a = scene to start on, b = 1 to loop that scene alone,
                        //   c = 1: the song is played once through and then stopped (a bounce), so its last
                        //   step begins nothing early that belongs to its first
                        //   d = a COUNT IN of that many steps first (easywins batch 2; 0: none): a click on each
                        //   beat of it, nothing of the song, and the song's step 1 on the sample after its last.
                        //   A Stop or a Panic meanwhile calls it off (and the Play is counted as taken)
        Stop,           // stop; b = 1: by hand (START): sounding samples fade out fast (drums, sampler).
                        //   Otherwise drums ring out, synth notes are let go (a bounce, the song's end).
        Panic,          // silence, now. a = 1: the output fades to nothing in 5 ms and stays shut;
                        //   playing stops, every drum is cut and every note let go. a = 0: it may
                        //   open again, once what is left has died away (or the next thing is played).
        SetTempo,       // value = beats per minute
        SetSceneTempo,  // scene, value = its own tempo (0: the song's)
        SetSceneMeter,  // scene, a = steps in its bars (1..16)
        SetVolume,      // the last thing before the output: value = linear gain, 0..1. Catch hears the sound before it.
        SetTrackLevel,  // track (or kMasterTrack), value = linear gain. It glides over kSendGlideSeconds, so it does not
                        //   click. A track's usual is kUsualTrackLevel; the master's is 1, after its slots, before the ceiling.
        SetSceneSteps,  // how many steps the scene lasts: scene, a = that many (23 is one bar and seven steps
                        //   of the next); 0, or as many as its longest cell has: all of them
        SetAutomation,  // one step of a lane: track, scene, a = lane, b = bar * 16 + step, value = 0..1 (below 0: nothing there)
        ClearAutomation,// a whole lane of a cell: track, scene, a = lane (-1: every lane)
        SetTrackHeard,  // mute and solo: track, a = 1 heard, 0 silent. It fades over 5 ms either way, so it does not click.
                        //   b = 1: it waits for the next bar line (playing only; stopped, or told what it is
                        //   already: at once, and anything waiting for that track is called off). c: the
                        //   asking's number (0: none), which heardDone() gives back once nothing waits.
        SetEffectParam, // track (or kMasterTrack, kEchoBus, kSpaceBus), a = slot, b = param, value
        SetLaneLevel,   // one drum's volume:         track, a = lane, value = 0..1
        SetStepNudge,   // move a hit off the grid:   track, scene, a = lane, b = step, value = -0.5..0.5 of a step
        SetStepChance,  // how likely a hit plays:    track, scene, a = lane, b = step, value = 0..1
        SetStepStart,   // where in the sound a hit starts: track, scene, a = lane, b = step, value = 0..1 of its length
        NoteOn,         // a synth track plays a note now: track, a = MIDI note, value = velocity 0..1; e = 1: an audition (held till its NoteOff)
        NoteOff,        //                                 track, a = MIDI note; e = 1: an audition's (the song's note on the key plays on)
        SelectPreset,   // a synth track's sound:     track, a = preset number
        SetNote,        // write one note of a synth pattern: track, scene, a = slot (0..63),
                        //   b = step, c = MIDI note, d = length in whole steps, extra = and this much of a
                        //   step more (so d = 2, extra = 0.4 is 2.4 steps), value = velocity (0 empties the slot)
        SetSceneCount,  // a = how many scenes the song has (1..kMaxScenes)
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
        SetTrackZones,  // internal: sent by setTrackZones()
        SetTrackChop,   // a = a sampler chopped (1) or whole (0)
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
        SetGlideFrom,   // GLIDE FROM, which key of the moment before a synth track's glides come from:
                        //   track, a = kFromTop, kFromPaired, kFromNearest, kFromRandom (glide_rule.hpp;
                        //   a number not known is TOP). Notes that begin after it is taken go by it.
        StopAudition,   // X let go: what a held audition (a Trigger with e = 1) started on a drum lane fades
                        //   out over the choke's 3 ms: track, a = lane. A pattern's hits on the lane, and a
                        //   ratchet's pieces still waiting that are not the audition's, are left alone.
        SetTrackPan,    // where a track (or kMasterTrack) stands left to right: value = -1 (hard left) .. 1 (hard
                        //   right), 0 the middle. Equal power, the middle untouched (panGains). Taken with the
                        //   level, before the sends; it glides as the level does.
        HoldEffectAutomation, // the FX screen's list of effects is open on a track's slot, and the slot sounds what
                        //   the cursor is on: track, a = slot, b = 1 held (the slot's automation is not applied,
                        //   so it does not move the effect being tried), 0 let go (it is, from the next step).
        SetSongEnd,     // what the song does at its end, played as a song (not a scene alone, not a bounce):
                        //   a = 0 LOOP, round to its first scene (as it always did); 1 STOP: playing stops on
                        //   the step after the last step of the last scene, as Stop does (Engine::songEnded)
        SetLaneFeel,    // FEEL, a drum lane's HUMAN TIME and HUMAN LEVEL (feel_rule.hpp): track, a = lane,
                        //   b = HUMAN TIME, c = HUMAN LEVEL, each 0..100. Live: hits asked for after it is
                        //   taken go by it; nothing is written into them. Both 0: the lane plays as it always did.
        SetMod,         // one field of a track's MOD (mod_rule.hpp): track, a = MOD (0, 1), b = the field (ModField),
                        //   value = its value, held to what it can be (modSet). Taken at the MOD's next tick.
        SetModStep,     // one value of a STEPPER's row: track, a = MOD, b = place (0..15), value = -1..1
        ModAudition,    // stopped, X held on a modulated knob: track, a = which MODs run (bit 0 MOD 1, bit 1 MOD 2),
                        //   counted from now as if Play had just been pressed; a = 0 lets go (the knob is given
                        //   its centre). Not taken while playing (the song's MODs run then); Stop and Panic let go.
        SetLaneChoke,   // CHOKE, a drum lane's group (drums.hpp): track, a = lane, b = kChokeNone, 1 or 2 (a number
                        //   not known is NONE). A hit on a lane in a group cuts the group's other lanes; it cuts
                        //   nothing by itself. Every lane at NONE: the kit plays as it always did.
        SetLaneSwing,   // SWING, a drum lane's (feel_rule.hpp): track, a = lane, b = 0..100 (held there). Every
                        //   second sixteenth comes that much of a third of a step late. Live, as FEEL.
                        //   0: the lane plays as it always did.
        GiveScene,      // internal: sent by send() before the first command that writes into a scene with
                        //   nothing in it (scenes-plan.md): track, scene, pointer = its store, made here
        SetClick,       // the METRONOME (easywins batch 2): a = 1 on, 0 off. On, every beat the song plays (steps 1,
                        //   5, 9, 13 of a bar; step 1 higher) begins a short click on its first sample, added after
                        //   Catch and before the volume. Off and no click sounding: the output is as it always was,
                        //   to the bit. A bounce's engine is never told it. (Play's `d` is the COUNT IN.)
        SetCellFill,    // a cell's FILL (fill_rule.hpp; appended after SetClick): track, scene, a = 1 on, 0 off. On,
                        //   the cell's eighth bar (kFillSlot) plays in place of its own in the scene's last bar on
                        //   its last time round. A cell of kMaxBars bars plays none. Taken on the step it lands in.
        SetLaneCuts     // SLICES AT THE HITS (slice_rule.hpp, batch S2; appended after SetCellFill): a drum lane's own cuts, three at most a
                        //   command: track, a = lane, c = how many the lane has (0: none, its ONE SLICE the even
                        //   share as it always was), b = the first place this command writes, d = how many (0..3),
                        //   value, extra, shift = the places b, b + 1, b + 2 (0..1 of the sound, rising). With
                        //   some, a hit that plays ONE SLICE plays from its start to the next cut.
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
    float extra = 0.f;       // SetNote only (and SetLaneCuts: its second place)
    float shift = 0.f;       // SetNote only: the note early or late, -0.5..0.5 of a step (SetLaneCuts: its third place)
    int e = 0;               // SetNote (and Trigger: 1, a held audition): what the note does besides play. Its low byte: a ratchet (2..4:
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
inline int autoSlotOfLane(int lane) { return lane >= 8 && lane < 8 + 4 * kTrackSlots ? (lane - 8) / 4 : -1; } // -1: not an effect's lane
inline int autoLaneOfSend(int bus) { return kAutoSendLane + bus; }

// A note as the engine is handed it (TrackImage::Note).
struct ImageNote { std::uint8_t step = 0, pitch = 0; float length = 0.f; float velocity = 0.f; float nudge = 0.f; std::uint8_t ratchet = 0, glide = 0, throwTo = 0; ChordPlay play; }; // length in steps, not only whole ones

// One scene of one track: for Engine::moveScenes, and a TrackImage's scenes.
struct SceneImage {
    float level[kMaxBars][kDrumLanes][kSteps] = {};
    float nudge[kMaxBars][kDrumLanes][kSteps] = {};
    float chance[kMaxBars][kDrumLanes][kSteps];
    float start[kMaxBars][kDrumLanes][kSteps] = {};
    StepFx fx[kMaxBars][kDrumLanes][kSteps];
    int bars = 1; // how long the track's cell is in it
    bool fill = false; // ...and whether its FILL is on (fill_rule.hpp: its eighth bar, in the scene's last bar, last time round)
    ImageNote notes[kMaxBars][kMaxNotes];
    float automation[kAutoLanes * kAutoSteps]; // [lane][bar * 16 + step]
    SceneImage() {
        for (auto& bar : chance)
            for (auto& lane : bar)
                for (float& c : lane) c = 1.f;
        for (float& v : automation) v = kAutoNone;
    }
};

// Every pattern of a track, for Engine::setTrackImage: a SceneImage for each scene that holds
// something (scenes-plan.md: a song uses few of its kMaxScenes; nullptr is a scene with nothing in
// it), the cells' lengths, and the lanes' levels. Read and written by pattern slot (scene * kMaxBars
// + bar: slot 0 is scene 1, bar 1) as it always was; a slot written makes its scene.
struct TrackImage {
    using Note = ImageNote;
    std::unique_ptr<SceneImage> scenes[kMaxScenes];
    SceneImage& scene(int s) {
        if (!scenes[s]) scenes[s].reset(new SceneImage);
        return *scenes[s];
    }
    template <class Row, Row (SceneImage::*Field)[kMaxBars]>
    struct BySlot {
        TrackImage* image;
        Row& operator[](int slot) const { return (image->scene(slot / kMaxBars).*Field)[slot % kMaxBars]; }
        Row* at(int slot) const { return &(*this)[slot]; } // a scene's first slot: its bars, as an array
    };
    BySlot<float[kDrumLanes][kSteps], &SceneImage::level> level{this};
    BySlot<float[kDrumLanes][kSteps], &SceneImage::nudge> nudge{this};
    BySlot<float[kDrumLanes][kSteps], &SceneImage::chance> chance{this};
    BySlot<float[kDrumLanes][kSteps], &SceneImage::start> start{this};
    BySlot<StepFx[kDrumLanes][kSteps], &SceneImage::fx> fx{this}; // each hit's step effects
    BySlot<Note[kMaxNotes], &SceneImage::notes> notes{this};
    // Automation, [scene][lane][bar * 16 + step] as one run.
    struct Automation {
        TrackImage* image;
        float& operator[](int i) const { return image->scene(i / (kAutoLanes * kAutoSteps)).automation[i % (kAutoLanes * kAutoSteps)]; }
    } automation{this};
    int bars[kMaxScenes]; // how long the track's cell is in each scene
    bool fill[kMaxScenes] = {}; // ...and whether its FILL is on there (SetCellFill)
    float laneLevel[kDrumLanes];
    float lanePart[kDrumLanes] = {}; // how much of its sound each hit on a lane plays (0: all); see Command::SetLanePart
    TrackImage() {
        for (float& l : laneLevel) l = 1.f;
        for (int& b : bars) b = 1;
    }
    TrackImage(const TrackImage&) = delete; // (its slots point at itself)
    TrackImage& operator=(const TrackImage&) = delete;
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
    // Replace every pattern on a track at once. The engine's stores for the scenes that hold
    // something are made here, from the image (the audio thread only swaps them in), and the image
    // is freed (send(): a SetTrackImage command's pointer is a TrackImage, taken on success).
    bool setTrackImage(int track, std::unique_ptr<TrackImage> image);
    // A scene put into the song, or one taken out, in ONE step for the audio thread, which
    // copies nothing: it changes which scene is which, and takes in the one scene that is new,
    // made ready here. by = 1: a scene goes in at `from`, those from there on move down one and
    // the last falls off the end. by = -1 (from 1 up): the scene before `from` goes, those from
    // `from` on move up one, and the new one is the last. `scenes` is that new scene for each of
    // the kNumTracks tracks (nullptr for a track with nothing in it there);
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
    bool setTrackZones(int track, std::unique_ptr<sampler::SampleZoneSet> zones);
    // Free what the audio thread has handed back (removed synths, used
    // images). Call now and then from the control thread.
    void collectGarbage();

    // From the control thread. Returns false if the queue is full.
    // A command that writes into a scene of a track that has nothing there yet is sent after a
    // GiveScene with that scene's store, made here (scenes-plan.md: the audio thread never
    // allocates, and a scene nobody has written has no store of its own); both or neither go.
    // RemoveInstrument is given a holder for the track's stores, and SetTrackImage's TrackImage is
    // made into stores here (and freed when the command went).
    bool send(const Command& command);
    // For the tests: how many scenes of a track have a store of their own (or will have, once the
    // audio thread has taken what was sent), and whether the one blank store is still blank.
    int scenesGiven(int track) const;
    static bool blankStillBlank();
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
    unsigned notesLost() const { return notesLost_.load(std::memory_order_relaxed); } // waiting notes dropped: the pool was full
    unsigned hitsLost() const { return hitsLost_.load(std::memory_order_relaxed); } // waiting hits dropped: the lane's slots were full
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

    // How hard render() works (load_meter.hpp; the help card shows it): over the last second of
    // sound, the share of the sound's time its calls took, and the most one call took of its own.
    // Below 0: not a second of sound yet. `loadSeconds` moves when they are new.
    float soundShare() const { return load_.share(); }
    float soundWorst() const { return load_.worst(); }
    unsigned loadSeconds() const { return load_.windows(); }

    // Read-outs for the screen. Safe from any thread.
    bool busAwake(int bus) const { return bus >= 0 && bus < kBuses && busAwake_[bus].load(std::memory_order_relaxed); } // false: at rest, costing nothing
    float trackPeak(int track) const; // loudest sample in the last render() call
    // What the effect in a slot has to show: for COMP, how many dB it is
    // turning the sound down just now (0: not at all; 6: to half). 0 for an
    // empty slot and for an effect with nothing to show. `track` may be
    // kMasterTrack. As of the last chunk rendered; no lock, any thread.
    float effectMeter(int track, int slot) const;
    // Which EffectKind the slot holds, as the audio thread last ran it (0: none). `track` may be kMasterTrack
    // or a bus. For the tests: what the engine plays, not what the project says. No lock, any thread.
    int effectKindIn(int track, int slot) const;
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
    // AT END STOP: each Play taken is counted, and when a song stops by itself at its end this
    // says which Play it was (0: none yet). The app counts the Plays it sent: the two equal, the
    // playing it started has ended. (A Play sent after the end is a later number, so no race.)
    unsigned songEnded() const { return songEnded_.load(std::memory_order_relaxed); }
    // COUNT IN: the beats of it still to come, the one sounding counted (4, 3, 2, 1), or 0: none under way.
    int countingIn() const { return countShown_.load(std::memory_order_relaxed); }
    // QUEUED MUTES: the number of the last SetTrackHeard asking (its c) once no track's gate waits for a
    // bar line any more: landed, called off, or never made to wait. Any thread, no lock.
    int heardDone() const { return heardDone_.load(std::memory_order_relaxed); }
    bool trackHeard(int track) const { return track >= 0 && track < kNumTracks ? tracks_[track].heard : false; } // (for the tests: the audio thread's own)
    bool clickOn() const { return clickOn_; } // what SetClick last said (for the tests: the audio thread's own)
    bool songStopsAtEnd() const { return songStop_; } // what SetSongEnd last said (for the tests: the audio thread's own)
    int playRepeat() const { return playRepeat_.load(std::memory_order_relaxed); } // 0-based pass through the scene
    // A scene played alone: which time round of its REPEATS it is on (0-based; round and round), for its FILL
    // (fill_rule.hpp). playRepeat stays 0 there, as it always did. In a song: 0.
    int playPass() const { return playPass_.load(std::memory_order_relaxed); }
    int playBar() const { return playBar_.load(std::memory_order_relaxed); }       // 0-based bar of the scene

    // The scope: the last samples one track put out, after its effects.
    void setScopeTrack(int track) { scopeTrack_.store(track, std::memory_order_relaxed); } // -1: none
    void readScope(float* out, int count) const; // the newest `count` samples (up to kScopeSize), oldest first

    float sampleRate() const { return sampleRate_; }
    // Catch: the engine remembers the last twenty seconds it put out (both
    // sides mixed to one). This copies out the last whole bar the song
    // played, exactly, bar line to bar line, also just after it has been
    // stopped. If there is no such bar (the song is in its first bar, or
    // something has been played by hand since it stopped, or there was a
    // panic) it is the last bar's worth of sound up to now. From the control thread; allocates.
    // `steps`, if given: how many steps that bar had (12 in a scene of 3/4, fewer in a scene's short
    // last bar; 16 for the bar's worth up to now), so what is caught can be fitted to them.
    std::vector<float> lastBar(int* steps = nullptr) const;
    // CATCH of 1, 2 or 4 bars (batch S3, 2026-11-25), of the whole mix (`track` -1) or of one track alone
    // (what that track puts out after its effects and its level, before its mute and pan: caught without
    // soloing). The last `bars` whole bars the song played, bar line to bar line; fewer where fewer were
    // played since Play (or since the ring began following that track, setCatchTrack), or where `bars`
    // of them would not fit the twenty seconds. None at all: `bars` bars' worth up to now, as lastBar's, no
    // longer than what has been heard of the track. From the control thread; allocates.
    struct Caught {
        std::vector<float> sound;
        int steps = kSteps; // how many steps it is (to fit it to)
        int bars = 0;       // how many whole bars it is; 0: a bar's worth up to now, not whole bars
    };
    Caught lastBars(int bars, int track = -1) const;
    // The track a second ring remembers, for a CATCH of one track (-1: none, and nothing is spent on it).
    // Set from the app every frame, as setScopeTrack; the ring follows it from the next chunk.
    void setCatchTrack(int track) { catchTrack_.store(track, std::memory_order_relaxed); }

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
    // The helpers take the tracks heaviest first (by each one's time the chunk before) instead
    // of in track order. The mix is in track order either way, so the sound is the same to the
    // bit. Off unless asked for: the SPEED TEST times it (docs/plans/speed-plan.md, batch 0).
    // Before the sound starts, not while render() runs.
    void setHeaviestFirst(bool on) { heaviestFirst_ = on; }

    // Which of a track's slots have their automation held, a bit a slot (HoldEffectAutomation). For the
    // tests: read between renders, never while one is under way.
    int automationHeld(int track) const { return track >= 0 && track < kNumTracks ? tracks_[track].autoHeld : 0; }
    // For the tests, between renders, never while one is under way: a lane's centre (the knob or
    // its automation), what a track's send has been given, and what the MODs add to a lane.
    float laneCentre(int track, int lane) const { return track >= 0 && track < kNumTracks && lane >= 0 && lane < kAutoLanes ? tracks_[track].autoNow[lane] : kAutoNone; }
    float sendGiven(int track, int bus) const { return track >= 0 && track < kNumTracks && bus >= 0 && bus < kBuses ? tracks_[track].send[bus] : 0.f; }
    // A track's LEVEL (gain) and PAN (the two sides' gains) as given, with what MODs add (batch 8). For the tests.
    float levelGiven(int track) const { return track >= 0 && track < kNumTracks ? tracks_[track].level : 0.f; }
    void panGiven(int track, float& left, float& right) const {
        left = track >= 0 && track < kNumTracks ? tracks_[track].panL : 0.f;
        right = track >= 0 && track < kNumTracks ? tracks_[track].panR : 0.f;
    }
    float modOffsetForTests(int track, int lane) const { return track >= 0 && track < kNumTracks && lane >= 0 && lane < kAutoLanes ? tracks_[track].modOffset[lane] : 0.f; }
    // For the tests, between renders, never while one is under way: a track's synth (null for drums
    // or none), to read what only that synth counts (JAX-EP's samplesClipped).
    const Instrument* synthForTests(int track) const { return track >= 0 && track < kNumTracks ? tracks_[track].synth.get() : nullptr; }

    // ---- MOD: a track's two modulators (mod_rule.hpp, docs/plans/modulation-plan.md) ----------
    // From the control thread: every field and every step of one MOD, as SetMod and SetModStep
    // commands. False if any was lost (counted in dropped(), as every send is).
    bool setMod(int track, int mod, const ModSettings& settings);
    // What a MOD adds to its setting now (depth x shape, -1..1), as of its last tick: what is
    // heard. 0 while it does not run (stopped, OFF). Any thread, no lock.
    float modShown(int track, int mod) const;
    // A STEPPER's place in its row as of its last tick (0..15); -1 while it does not run, or an LFO.
    int modPlace(int track, int mod) const;
    // Where the song was at a MOD's last tick (steps since Play, or since X went down: mod_rule.hpp's
    // position; a FREE LFO's in seconds, modPositionFor), so the MOD tab can draw its shape with a dot at now; not a number while it does not
    // run. Any thread, no lock.
    double modNow(int track, int mod) const;
    // ---- CLOUD's grains, for its picture (docs/plans/granular-plan.md) ------------------------
    // Where each grain of the track's synth reads, as a share of its recording (0..1), as of the
    // end of the last chunk: how many (at most `most`, at most kGrainsShown). -1: the track plays
    // no grains. Any thread, no lock (a picture: the count and the places may be a chunk apart).
    int grainPlaces(int track, float* places, int most) const;
    // Chunks worked out since the engine was made (a MOD's tick cuts more of them: the bench counts
    // them). As of the end of the last render(); any thread.
    std::uint64_t chunksRendered() const { return chunksShown_.load(std::memory_order_relaxed); }

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
        std::uint8_t fill = 0;                     // its FILL is on (SetCellFill): drums[kFillSlot], notes[kFillSlot]
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
    // What SetTrackImage carries: a store for each scene that holds something (nullptr: none), made
    // on the app thread, and the lanes' levels. The audio thread puts in their place the stores the
    // track had (nullptr for the blank one) and hands it back. RemoveInstrument carries one empty,
    // to hand the track's stores back in: one thing in the trash, not one a scene.
    struct SceneSet {
        SceneStore* scenes[kMaxScenes] = {};
        float laneLevel[kDrumLanes];
        float lanePart[kDrumLanes];
        ~SceneSet() {
            for (SceneStore* scene : scenes) delete scene;
        }
    };
    // Where a scene that nobody has written reads from: ONE store for every engine, made once and
    // never written (scenes-plan.md). Every write into a scene goes through `written`.
    static SceneStore* blankScene();
    static SceneStore* written(SceneStore* scene) { return scene == blankScene() ? nullptr : scene; }
    SceneStore* writable(const Command& c); // the store a pattern command writes into: nullptr where there is none
    // Which scenes of each track have a store of their own: the app thread's own count of what it
    // has handed over (GiveScene, SetTrackImage, a scene move, a track taken away), a bit a scene.
    std::uint64_t given_[kNumTracks] = {};
    static bool leavesBlank(const Command& command); // a write that leaves a scene as the blank one has it
    void moveGiven(int from, int by, const std::unique_ptr<SceneImage>* scenes);
    // One scene's patterns read in, as the engine keeps them (from a TrackImage, or a SceneImage).
    static void sceneIn(SceneStore& to, const float (*level)[kDrumLanes][kSteps], const float (*nudge)[kDrumLanes][kSteps], const float (*chance)[kDrumLanes][kSteps],
                        const float (*start)[kDrumLanes][kSteps], const StepFx (*fx)[kDrumLanes][kSteps], const TrackImage::Note (*notes)[kMaxNotes], int bars, bool fill,
                        const float* automation);
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
        sampler::SampleZoneSet* trackZones = nullptr; // ...or its multisample's zones: one or the other
        std::unique_ptr<Instrument> synth; // set on a synth track
        // The patterns are large (every bar of every scene), so they live
        // on the heap: an Engine can then sit on the stack.
        // Each scene is a piece of its own, so that a scene put into the song or taken out
        // (Command::ScenesMoved) is a few pointers moved and nothing copied. A scene nobody has
        // written is the one blank store (blankScene), never written. What follows
        // reads them as the one run they used to be: by pattern slot, by scene, by place.
        SceneStore* scenes[kMaxScenes];
        SceneSet* goneSet = nullptr; // a RemoveInstrument's holder, kept while the track fades
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
        float autoNow[kAutoLanes];  // the CENTRE: where the knob or its automation has the lane (a MOD moves around it)
        // What the track's MODs add to each lane (mod_rule.hpp). Exactly 0 where no MOD is aimed,
        // and everywhere while none runs: then the lane is given autoNow itself, in the same calls
        // as before there were MODs. Only runMods sets it, and stillLane puts it back to 0.
        float modOffset[kAutoLanes] = {};
        ModSettings mods[kModsPerTrack];          // MOD 1 and MOD 2 (SetMod, SetModStep): OFF unless told
        std::atomic<float> modShown[kModsPerTrack]; // what each adds as of its last tick, for the screen
        std::atomic<int> modPlace[kModsPerTrack];   // a STEPPER's place as of its last tick; -1: not running
        std::atomic<int> grainCount{-1};            // CLOUD's grains sounding (grainPlaces); -1: no grains
        std::atomic<float> grainAt[kGrainsShown];   // and where each reads
        std::atomic<double> modAt[kModsPerTrack];   // the position of its last tick (a FREE LFO's in seconds); not a number: not running
        std::uint8_t slotChoices[kTrackSlots] = {}; // a bit a setting: which of each slot's effect's settings are
                                                    //   choices (InstallEffect). A MOD never moves a choice.
        float autoFrom[kAutoLanes], autoTo[kAutoLanes];
        bool autoGliding[kAutoLanes] = {};
        std::uint8_t autoHeld = 0; // a bit a slot: its automation waits (HoldEffectAutomation)
        bool autoWaits(int lane) const { const int slot = autoSlotOfLane(lane); return slot >= 0 && ((autoHeld >> slot) & 1); }
        float* autoAt(int scene, int lane) { return scenes[scene]->automation + lane * kAutoSteps; }
        // Synth notes sounding from the pattern: samples until each is let
        // go, to the sample. kNoNote: that key is not sounding.
        // Notes waiting for their moment: late ones from the step that just
        // began, early ones that belong to the next.
        struct PendingNote { double samplesLeft = 0.0; std::uint8_t pitch = 0, glide = 0, throwTo = 0; float velocity = 0.f, length = 0.f, hold = 0.f; std::uint8_t piece = 0; float strikeOff = 0.f; double at = 0.0; std::uint8_t how = 0; bool ahead = false; }; // strikeOff: the strike's own moment less `at`, in steps (a ratchet's piece: k apart; it fits where the padding was)
        GlideTrail trail; // the notes the track last began, for a glide to start from (none since Play)
        std::uint8_t glideFrom = 0; // GLIDE FROM: which of them (kFromTop and the rest, glide_rule.hpp)
        // Room for the most a step can ask: every note of the scene, ratcheted to the full and pushed late.
        // (Twenty-four ran out: an eight-key chord at x4, nudged, lost its last two keys.)
        PendingNote pendingNotes[kMaxNotes * kMaxRatchet];
        PlaceBits<kMaxNotes * kMaxRatchet> notesWaiting; // which of them may be in use
        PlaceBits<128> keysHeld;                          // which keys may be sounding (offIn below kNoNote)
        // The keys an audition holds (NoteOn with e = 1: X held): the song's own note on one ending does not let it
        // go, and its NoteOff (e = 1) does not cut the song's note on it (review of gesture audit G33).
        PlaceBits<128> auditioned;
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
            bool ahead = false;           // asked a step ahead, from the step after the one playing
            double at = 0.0;              // the chord's own moment, in steps since Play (its note m is struck at + m gap: a roll each)
        };
        Arp arps[3];
        // The step (counted from Play) whose chord has been asked for a step ahead: that chord
        // is not begun again when its step comes.
        long chordAsked = -1;
        std::uint32_t lastChord = 0; // the chord the track last began (its seed), and how many it has
        long strokes = -1;           // begun since Play, less one: UP-DOWN strums up on the even ones
        // What the step playing asked a step ahead for came from here (fireStep): the scene's store
        // and the bar of the cell. If a scene move puts another there, what was asked is dropped
        // and asked again (Engine::askAgain), and these put back as they were before the asking.
        const void* askedFrom = nullptr;
        int askedBar = -1;
        long strokesBefore = -1, chordAskedBefore = -1;
        std::uint32_t lastChordBefore = 0;
        double offIn[128];
        Track() {
            for (double& off : offIn) off = kNoNote;
            for (SceneStore*& scene : scenes) scene = blankScene();
        }
        ~Track() {
            for (SceneStore* scene : scenes)
                if (scene != blankScene()) delete scene;
            delete goneSet;
        }
        // Hits waiting for their moment: a late hit from the step that just
        // began, and an early hit that belongs to the next one.
        // A ratchet waits here too: `more` hits after the next, `gap` samples apart.
        struct Pending {
            double samplesLeft = 0.0, gap = 0.0;
            float velocity = 0.f, start = 0.f, part = -1.f;
            HitPlay how;
            int more = 0;
            bool ahead = false; // an early hit of the step after the one playing
            bool begun = false; // a ratchet whose first hit has sounded
            bool whole = false; // FEEL: its wait and its gap are whole samples, and stay so through a tempo change
        };
        Pending pending[kDrumLanes][3];
        PlaceBits<kDrumLanes * 3> hitsWaiting; // which of them may be in use: lane * 3 + place
        // FEEL: each lane's HUMAN TIME and HUMAN LEVEL, and the step (counted from Play) whose hit
        // on the lane has been asked for a step ahead because HUMAN TIME may bring it early: that
        // hit is not begun again when its step comes, whatever the lane says by then.
        LaneFeel feel[kDrumLanes];
        std::uint8_t swing[kDrumLanes] = {}; // SWING: each lane's, 0..100 (feel_rule.hpp)
        long feelAsked[kDrumLanes] = {-1, -1, -1, -1, -1, -1, -1, -1};
        // The lanes hit since the last chunk was worked out: bit 0 is lane 1. A hit
        // always lands on a chunk's first sample (render() ends a chunk where the
        // next one is due), so "which lanes" is all there is to say.
        unsigned hitsNow = 0;
        float level = kUsualTrackLevel; // one full hit stays clean; hits that land together push the master
        float levelNow = kUsualTrackLevel; // ...as it is this sample: it glides to `level` over kSendGlideSeconds
        float panL = 1.f, panR = 1.f;       // PAN: the two sides' gains it is set to (panGains; 1 and 1: the middle)
        float panLNow = 1.f, panRNow = 1.f; // ...as they are this sample, gliding as the level does
        // The mixer's own LEVEL (gain) and PAN (place), as SetTrackLevel and SetTrackPan set them: the
        // centre a MOD on LEVEL or PAN moves about (batch 8). `level`, `panL` and `panR` are what is
        // given (giveMix); with nothing added, these exactly.
        float levelSet = kUsualTrackLevel, panSet = 0.f;
        float modMix[2] = {}; // what the MODs add to LEVEL and to PAN (runMods); 0 while none runs
        bool heard = true;   // not muted (SetTrackHeard)
        int heardNext = -1;  // ...what it becomes at the next bar line (SetTrackHeard with b = 1); -1: nothing waits
        float gate = 1.f;    // ...as it is this sample: moving toward 1 or 0
        float send[kBuses] = {};    // how much goes to each bus (SetSend, or its automation lane)
        float sendNow[kBuses] = {}; // ...as it is this sample: moving toward `send`
        // THROW on a note: the send to a bus is full for the note's length, whatever `send` says.
        double throwLeft[kBuses] = {}; // samples the send is still held open for (0: not)
        float throwNow[kBuses] = {};   // how far open it is this sample, 0..1: it moves over kSendGlideSeconds
        std::unique_ptr<Effect> effects[kTrackSlots];
        SlotFade fades[kTrackSlots];
        std::atomic<float> meters[kTrackSlots]; // what each slot's effect shows (effectMeter)
        std::atomic<int> kinds[kTrackSlots];    // and what it is (effectKindIn)
        std::atomic<float> peak{0.f};
        // Taken away while it still sounded: it fades to nothing over kGoneFadeSeconds (its
        // effects' tails too), and only then is its instrument taken away (Engine::takeAway).
        bool going = false;
        float goingGain = 1.f;
        bool hasInstrument() const { return hasDrums || synth; }
    };
    // Where the song is: the scene, its time round (repeat: in a song; pass: a scene played alone, round and
    // round, as loopPass_), its bar.
    struct Position { int scene, repeat, bar, pass; };
    int sceneBars(int scene) const;                 // the longest cell in it
    int sceneSteps(int scene) const;                // how many steps it lasts
    // Is a FILL due where `at` is: the scene's last bar, its last time round (fill_rule.hpp)?
    bool fillTimeAt(const Position& at) const;
    // The pattern slot a track plays at bar `bar` of `scene`: its own bar, or its FILL at the FILL's time.
    int slotOf(const Track& track, int scene, int bar, bool atFillTime) const {
        return scene * kMaxBars + cellBarOf(track.bars[scene], track.scenes[scene]->fill != 0, bar, atFillTime);
    }
    static bool fillSlot(const Track& track, int slot) { return slot % kMaxBars >= track.bars[slot / kMaxBars]; } // a slot slotOf gave is the FILL
    void fireNoteOffs();
    void firePendingNotes();
    // at: when it begins, in steps since Play. strikeAt: when this very strike is, for its roll
    // (Instrument::noteSeed) and nothing else: a ratchet's piece and an arpeggio's note each have
    // their own (kSameStrike: the same as `at`).
    void startNote(Track& track, const Note& note, double delaySamples, double at, double strikeAt = kSameStrike);
    static constexpr double kSameStrike = -1e300;
    // A chord's note with a chord effect set (chord_rule.hpp). `ahead`: asked a step before the
    // note's own step, as an early note is; `stepAt`: when that step begins, in steps since Play;
    // `wait`: samples from now until it does.
    void startChordNote(Track& track, int slot, const Note& note, bool top, double stepAt, double wait);
    void fireArps();

    void apply(const Command& command);
    // apply()'s areas, each true where the command was its own (engine.cpp).
    bool applyDrums(const Command& c);
    bool applySound(const Command& c);
    bool applySong(const Command& c);
    bool applyMix(const Command& c);
    void takeAway(int t);      // RemoveInstrument's work, at once (after its fade, if it sounded)
    void takeCommands();       // the queue, in order, up to a command that waits for a fade
    int going_ = 0;            // tracks fading before they are taken away (the audio thread's)
    Command held_;             // the first command after a taking away, waiting for the fade
    std::atomic<bool> holding_{false};
    Position afterThisBar() const;
    void fireStep();
    void applyAuto(Track& track, int lane, float value);
    void giveLane(Track& track, int lane);
    void giveMix(Track& track); // LEVEL and PAN given: the mixer's, with what the MODs add (batch 8)
    // MOD. runMods: one tick, at `steps` since Play (or since X went down) and `seconds` (a FREE
    // LFO's position: whole samples counted, so no tempo change moves it); `macros`: this
    // tick also gives the synth's macros (every kModMacroTicks'th). stillLane: a lane's offset back
    // to 0, and the lane given its centre. stillMods: every lane of the track so. modsChanged:
    // whether any MOD is on (anyMod_), after one was set or a track taken away.
    void runMods(double steps, double seconds, bool macros);
    void stillLane(Track& track, int lane);
    void stillMods(Track& track);
    void modsChanged();
    bool anyMod_ = false;      // some track has a MOD on: chunks are cut on the tick while playing
    long sinceStep_ = 0;       // samples since the first sample of the step playing (the tick counts from it)
    long playedFor_ = 0;       // samples since Play: a FREE LFO's clock (seconds, not steps)
    int heardAsked_ = 0;               // SetTrackHeard: the last asking's number
    std::atomic<int> heardDone_{0};    // ...given out once nothing waits
    void landHeard();                  // a bar line, Stop: every waiting gate is set
    void tellHeardDone();
    int heardTrack_ = -1;      // ModAudition: the track whose MODs run while stopped, -1 none
    unsigned heardMask_ = 0;   // ...which of them (bit 0 MOD 1), 0: none runs
    long heardFor_ = 0;        // ...samples since X went down
    std::uint64_t chunks_ = 0; // chunks worked out (the audio thread's), shown at the end of render()
    std::atomic<std::uint64_t> chunksShown_{0};
    void glideAutomation(int frames);
    void settleAutomation();
    double autoPhase_ = 0.0; // how far through the present step, 0..1
    void playHit(Track& track, int lane, float velocity, float start, float part, const StepFx& fx, double delaySamples, bool audition = false, bool whole = false, double late = 0.0); // audition: a held audition's, which StopAudition fades; late: SWING's, which squeezes a ratchet
    // `bar`: the bar of the cell the hit is in; `stepAt`: its step's number, counted from Play (both for FEEL's
    // rolls); `ahead`: asked a step ahead (a hit whose moment has gone by is then not played).
    // False: asked ahead, and not taken (its moment had gone by).
    // `fill`: the hit is in the cell's FILL (its eighth bar, played in place of its own): a run of slices begins
    // in it, as in a cell's first bar (the bar before it in its place, the seventh, is not the one played before).
    bool schedule(Track& track, int lane, const DrumScene& scene, int step, double delaySamples, int bar, long stepAt, bool ahead, bool fill = false);
    void firePending();
    void clearPending();
    void letGoOfNotes(bool stopping = false); // stopping: Stop (each synth's stopNotes), else Play and panic (allNotesOff)
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
    int taken_[kNumTracks] = {};      // active_ in the order the helpers take them (heaviestFirst_: by tookNs_)
    bool heaviestFirst_ = false;
    long tookNs_[kNumTracks] = {};    // heaviestFirst_: how long each track took last chunk (written by whoever did it)
    // The list of tracks being shared out: the count in the high half, the next place to take in the
    // low half, ONE atomic, so a place taken is always held to its own chunk's count. (Apart, a helper
    // that took a place past the end of one chunk and read the count of the next, larger one rendered
    // a track another thread was rendering too: review 2026-11-23.) 0: shut.
    std::atomic<std::uint32_t> claims_{0};
    std::atomic<int> finished_{0};
    std::vector<std::thread> workers_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    int wakeups_ = 0;          // chunks the helpers have been called to and not yet come for
    std::atomic<bool> quitWorkers_{false};
    std::atomic<bool> flush_{false};
    LoadMeter load_; // every render() timed (the audio thread writes, any thread reads its atomics)

    float sampleRate_ = 48000.f;
    Track tracks_[kNumTracks];
    bool claimed_[kNumTracks] = {};
    bool claimedDrums_[kNumTracks] = {}; // ...and which of those are drums // control thread's view of which tracks have an instrument
    std::unique_ptr<Effect> masterEffects_[kMasterSlots];
    SlotFade masterFades_[kMasterSlots];
    std::atomic<float> masterMeters_[kMasterSlots];
    std::atomic<int> masterKinds_[kMasterSlots], busKinds_[kBuses]; // effectKindIn
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
    bool songStop_ = false; // AT END STOP (SetSongEnd): the song, played as a song, stops by itself at its end
    unsigned playsTaken_ = 0; // Command::Play taken so far (the audio thread's)
    std::atomic<unsigned> songEnded_{0}; // the playsTaken_ of the playing that stopped at its end
    // Nothing of the song's start is asked for at its end: a bounce, or a song that stops there.
    bool playsOnce() const { return once_ || (songStop_ && !loopScene_); }
    // The step after the song's last: a song that stops at its end stops here (render()).
    bool songEndsNow() const;
    void stopPlaying(bool byHand); // Command::Stop (by hand from START, else tails ring), and a song stopping at its end
    void startPlaying(const Command& c); // Command::Play, at once or at the end of its COUNT IN
    // The METRONOME and the COUNT IN (easywins batch 2). The click is a table worked out once in
    // prepare(), read a sample at a time from where it began, so it is the same in any blocks.
    static constexpr int kClickMost = 4096;   // samples: 30 ms at up to 136 kHz
    float clickWave_[2][kClickMost] = {};     // [0] a beat, [1] a bar's first beat (higher, louder)
    int clickLength_ = 0;                     // samples of it at this rate
    bool clickOn_ = false;                    // SetClick
    int clickAt_ = -1;                        // the sample of the click sounding; -1: none
    int clickWhich_ = 0;
    void startClick(bool bar) { clickAt_ = 0, clickWhich_ = bar ? 1 : 0; }
    void addClick(float* interleaved, int frames);
    Command countPlay_;          // the Play waiting for the COUNT IN to end
    int countSteps_ = 0;         // its steps; 0: no count under way
    int countAt_ = 0;            // the step of it about to begin
    double untilCount_ = 0.0;    // samples before it does
    std::atomic<int> countShown_{0};
    void barLine();     // Catch: a bar has ended here (a bar's first step, or a song stopping at its end)
    bool firstStep_ = true; // the step about to fire is the first since Play
    bool telling_ = false;  // inside fireStep, for a PAIRED track: its notes are only told to the trail as coming, not begun
    bool asking_ = false;   // inside fireStep: what is set waiting now is asked a step ahead (marked `ahead`)
    int askedFollowing_ = -1; // the step after the one playing, as fireStep worked it out (-1: none asked)
    // The early notes, hits and chords of the step after the one playing, asked from where it is.
    void askAheadHits(Track& track, const DrumScene& then, int bar, int following, double elapsed, bool fill);
    void askAheadNotes(Track& track, int slotThen, int following, double now, double elapsed);
    // After a scene move: the step after the one playing is another scene's now? Then what was
    // asked from the one it was is dropped, and asked again from the one it is.
    void askAgain(bool endChanged = false); // endChanged: AT END was switched (asked again only on the song's last step, the same bar or not)
    long stepsFired_ = 0;   // steps begun since Play: the clock a note's start is told to its track's GlideTrail by
    float tempo_ = 120.f;   // the tempo playing now: the scene's, or the song's where it has none of its own
    float songTempo_ = 120.f;
    float sceneTempo_[kMaxScenes] = {}; // ...a scene's own (0: the song's)
    float sceneTempoNow(int scene) const { return scene >= 0 && scene < kMaxScenes && sceneTempo_[scene] > 0.f ? sceneTempo_[scene] : songTempo_; }
    void useTempo(float bpm); // play at this tempo from here (the step keeps its place, as SetTempo always did)
    double samplesPerStep_ = 6000.0;
    double untilStep_ = 0.0; // samples left before the next step fires
    int nextStep_ = 0;
    int scene_ = 0;
    int repeat_ = 0;
    int loopPass_ = 0; // a scene played alone: its time round, round and round through its REPEATS (its FILL's clock; repeat_ stays 0)
    int bar_ = 0;    // the bar of the scene now playing
    int sceneCount_ = 1;
    int repeats_[kMaxScenes];
    int steps_[kMaxScenes]; // how many steps each scene lasts, if it has been cut short (0: every step of its longest cell)
    int meter_[kMaxScenes]; // ...and in each of its bars (a bar of 12 is 3/4)
    int meterOf(int scene) const {
        const int m = scene >= 0 && scene < kMaxScenes ? meter_[scene] : kSteps;
        return m < 1 ? kSteps : (m > kSteps ? kSteps : m);
    }
    std::atomic<int> playStep_{-1};
    std::atomic<unsigned> playScene_{0}; // the scene playing + 1 in its low byte, movesHeard_ above it: one read gives both
    unsigned movesHeard_ = 0;            // Command::ScenesMoved taken so far (the audio thread's)
    void tellScene(int scene) { playScene_.store(((movesHeard_ % kMovesHeardRound) << 8) | static_cast<unsigned>(scene + 1), std::memory_order_relaxed); }
    std::atomic<int> playRepeat_{0};
    std::atomic<int> playPass_{0};
    std::atomic<int> playBar_{0};
    // The gate at the very end of the chain, for Panic.
    enum class Gate { Open, Closing, Shut, Opening };
    Gate gate_ = Gate::Open;
    float gateGain_ = 1.f;
    bool panicHeld_ = false;
    bool cutEverything_ = false; // the gate has just shut: cut the drums and clear the effects
    int shutFrames_ = 0, quietFrames_ = 0;
    std::atomic<bool> silenced_{false};

    // The master's level and pan (the mixer): after its slots, before the ceiling. At 1, 1 and 1
    // it is not run at all, so a song left alone comes out as it did to the bit.
    float masterLevel_ = 1.f, masterLevelNow_ = 1.f;
    float masterPanL_ = 1.f, masterPanR_ = 1.f, masterPanLNow_ = 1.f, masterPanRNow_ = 1.f;
    void runMaster(float* mixL, float* mixR, int frames);
    float volume_ = 1.f, volumeNow_ = 1.f; // where it is going, and where it has got to (it glides, so a change does not click)
    std::unique_ptr<std::atomic<float>[]> caught_; // a ring of kCatchFrames
    std::atomic<std::uint64_t> caughtCount_{0};    // frames ever written to it
    long long barBeganStep_ = 0;                   // stepsFired_ when the bar playing began
    std::atomic<int> barFramesNow_{96000};         // a bar at the present tempo
    // The last bar lines (CATCH of 1, 2 or 4 bars): caughtCount_ at each, and the steps of the bar that ended
    // there, in a ring of kBarLinesKept. lineCount_ only grows; the lines from runFrom_ on are of one run of
    // the song (a Play, a panic or a hand played after a stop begins another). Read with a check, no lock.
    std::atomic<std::uint64_t> lineAt_[kBarLinesKept];
    std::atomic<int> lineSteps_[kBarLinesKept];
    std::atomic<std::uint64_t> lineCount_{0}, runFrom_{0};
    // CATCH of one track: a second ring, fed by the track catchTrack_ names, from followFrom_ on.
    std::unique_ptr<std::atomic<float>[]> trackCaught_;
    std::atomic<int> catchTrack_{-1};
    std::atomic<int> following_{-1};             // the track the ring holds since followFrom_ (-1: none)
    std::atomic<std::uint64_t> followFrom_{0};
    int chunkCatch_ = -1;        // the track followed this chunk (the audio thread's)
    bool chunkTapped_ = false;   // ...and it was worked out this chunk (written by whoever rendered it)
    std::uint64_t chunkAt_ = 0;  // caughtCount_ at this chunk's first frame

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
    struct Trash { bool isImage = false; void* pointer = nullptr; bool isSample = false; bool isZones = false; bool isEffect = false; bool isMove = false; bool isStore = false; }; // isImage: a SceneSet
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
    std::atomic<unsigned> notesLost_{0};
    std::atomic<unsigned> hitsLost_{0};
    void bin(const Trash& trash); // the audio thread only, with no helper at work
    void binHeld();               // the same
};

} // namespace apotheca
