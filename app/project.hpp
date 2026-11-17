#pragma once
// The song as the screens see it. The screens own this; the engine is told
// about every change through commands, and never the other way round.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#include "apotheca/engine.hpp"
#include "apotheca/physic.hpp"
#include "apotheca/acid.hpp"
#include "apotheca/cloud.hpp"
#include "apotheca/morph.hpp"
#include "apotheca/drum_synth.hpp"
#include "lane_edit.hpp"

namespace apotheca {

// One scene of a drum track: eight lanes by sixteen steps.
struct DrumPattern {
    float level[kDrumLanes][kSteps] = {};  // 0 = no hit
    float nudge[kDrumLanes][kSteps] = {};  // -0.5..0.5 of a step
    float chance[kDrumLanes][kSteps];      // 0.05..1
    float start[kDrumLanes][kSteps] = {};  // where in the lane's sound a hit begins, 0..1 of its length
    bool fromDice[kDrumLanes][kSteps] = {}; // hits the dice placed and nobody has touched since
    StepFx fx[kDrumLanes][kSteps];          // each hit's step effects: pitch, ratchet, backwards, cut (drums.hpp)
    std::uint32_t diceSeed[kDrumLanes];

    DrumPattern() {
        for (auto& lane : chance)
            for (float& c : lane) c = 1.f;
        for (int lane = 0; lane < kDrumLanes; ++lane) diceSeed[lane] = 0x51ED270Bu * static_cast<std::uint32_t>(lane + 1);
    }
    bool empty() const {
        for (const auto& lane : level)
            for (float v : lane)
                if (v > 0.f) return false;
        return true;
    }
};

// One scene of a synth track: up to 64 notes in one bar.
struct NoteEvent {
    int step = 0, pitch = 0;
    float length = 0.f;   // in steps. Not only whole ones: a note may end part-way through a step.
    float nudge = 0.f;    // early or late: -0.5..0.5 of a step
    float velocity = 0.f; // 0 = empty slot
    // What the note does besides play (hold X on it):
    std::uint8_t ratchet = 0; // 2..4: that many notes, evenly, inside its first step. 0: the one
    std::uint8_t glide = 0;   // it slides to its pitch from the note before it on the track, for this long (kGlideHalf
                              // and the rest, apotheca/glide_rule.hpp; 1, which was "on", is half a step). 0: it does not
    std::uint8_t throwTo = 0; // THROW: 1 ECHO, 2 SPACE, 3 both. The track's send to that bus is full while the note lasts
    std::uint8_t spare = 0;   // (so the note has no bytes that are not set: it is compared and hashed whole)
    ChordPlay play;           // only on a chord's note as NotePattern::render gives it: which key of the chord it is,
                              // and the chord's effects (apotheca/chord_rule.hpp). A loose note's is all nothing.
};

// The level a hit, a note or a chord starts at when it is placed (owner, 2026-11-15: 90%, it was
// 80%), and what X and A hear a sound at on every screen, so what is heard is what is placed.
constexpr float kPlacedLevel = 0.9f;
// EVEN (easywins batch 5): is `step` (0..15) one of `hits` spread as evenly as sixteen steps allow, the
// first on step 0? (A Euclidean rhythm, as Bresenham draws a line: 3 is steps 1, 7, 12; 5 is 1, 5, 8, 11, 14.)
inline bool euclidHit(int hits, int step) { return hits > 0 && (step * hits) % 16 < hits; }
// The keys the piano roll reaches, C0 to C8 (NoteScreen::kLowest, kHighest). TRANSPOSE keeps a cell inside them.
constexpr int kRollLowest = 12, kRollHighest = 108;

// A chord placed in a bar. It stays one thing: one level, one length, one
// name, and it can be swapped for another chord later. Its notes are worked
// out from it each time, never stored.
constexpr int kMaxChords = 16, kChordNotes = 8, kMaxInversion = 3;
struct ChordEvent {
    int step = 0;
    float length = 0.f; // in steps, as a note's
    float nudge = 0.f;  // early or late, the whole chord: -0.5..0.5 of a step
    int root = 60;      // the key the chord is built on
    int inversion = 0;  // +n: the n lowest notes go up an octave. -n: the n highest come down.
    int count = 0;      // how many of `intervals` are used
    int intervals[kChordNotes] = {}; // semitones above the root
    float velocity = 0.f; // 0 = empty slot
    char name[16] = {};
    std::uint8_t ratchet = 0;   // 2..4: the whole chord that many times, evenly, inside its first step
    std::uint8_t throwTo = 0;   // THROW, as a note's: every key of the chord is thrown
    std::uint8_t spare[2] = {};
    // CHORD EFFECTS (hold X on it, apotheca/chord_rule.hpp): STRUM, ORDER, HUMAN TIME, HUMAN LEVEL,
    // LENGTHS. Its `index` and `count` are not used here: render() fills them in for each key.
    ChordPlay play;

    // The keys it plays, lowest first, with no key twice. Returns how many.
    int pitches(int out[kChordNotes]) const {
        int p[kChordNotes], n = 0;
        for (int i = 0; i < count && i < kChordNotes; ++i) p[n++] = root + intervals[i];
        const auto sort = [&] {
            for (int i = 1; i < n; ++i)
                for (int j = i; j > 0 && p[j] < p[j - 1]; --j) { const int t = p[j]; p[j] = p[j - 1]; p[j - 1] = t; }
        };
        sort();
        for (int k = 0; k < inversion && n > 0; ++k) { p[0] += 12; sort(); }
        for (int k = 0; k > inversion && n > 0; --k) { p[n - 1] -= 12; sort(); }
        int made = 0;
        for (int i = 0; i < n; ++i) {
            if (p[i] < 0 || p[i] > 127 || (made > 0 && out[made - 1] == p[i])) continue;
            out[made++] = p[i];
        }
        return made;
    }
};

struct NotePattern {
    NoteEvent notes[kMaxNotes]; // loose notes
    ChordEvent chords[kMaxChords];

    bool empty() const {
        for (const NoteEvent& n : notes)
            if (n.velocity > 0.f) return false;
        for (const ChordEvent& c : chords)
            if (c.velocity > 0.f) return false;
        return true;
    }
    // The chord sounding on this step, or -1. Chords never overlap in time.
    int chordAt(int step) const {
        for (int i = 0; i < kMaxChords; ++i) {
            const ChordEvent& c = chords[i];
            if (c.velocity > 0.f && step >= c.step && step < c.step + c.length) return i;
        }
        return -1;
    }
    int freeChord() const {
        for (int i = 0; i < kMaxChords; ++i)
            if (chords[i].velocity <= 0.f) return i;
        return -1;
    }
    // Everything the engine should play: the loose notes where they are, and
    // each chord's notes in the slots left over. Returns how many notes
    // sound. A note with no slot left is dropped; used() is there so that
    // never happens.
    int render(NoteEvent out[kMaxNotes]) const {
        int count = 0, next = 0;
        for (int i = 0; i < kMaxNotes; ++i) {
            out[i] = notes[i].velocity > 0.f ? notes[i] : NoteEvent();
            count += notes[i].velocity > 0.f ? 1 : 0;
        }
        for (const ChordEvent& c : chords) {
            if (c.velocity <= 0.f) continue;
            int keys[kChordNotes];
            const int n = c.pitches(keys);
            for (int k = 0; k < n; ++k) {
                while (next < kMaxNotes && out[next].velocity > 0.f) ++next;
                if (next >= kMaxNotes) return count;
                out[next].step = c.step;
                out[next].pitch = keys[k];
                out[next].length = c.length;
                out[next].nudge = c.nudge;
                out[next].velocity = c.velocity;
                out[next].ratchet = c.ratchet;
                out[next].glide = 0;
                out[next].throwTo = c.throwTo;
                out[next].play = c.play;
                out[next].play.index = static_cast<std::uint8_t>(k);
                out[next].play.count = static_cast<std::uint8_t>(n);
                ++count;
            }
        }
        return count;
    }
    // How many of the sixty-four notes are taken, chords' notes included.
    int used() const {
        int count = 0;
        for (const NoteEvent& n : notes) count += n.velocity > 0.f ? 1 : 0;
        for (const ChordEvent& c : chords) {
            int keys[kChordNotes];
            if (c.velocity > 0.f) count += c.pitches(keys);
        }
        return count;
    }
    // The note sounding at this pitch on this step, or -1.
    int at(int pitch, int step) const {
        for (int i = 0; i < kMaxNotes; ++i) {
            const NoteEvent& n = notes[i];
            if (n.velocity > 0.f && n.pitch == pitch && step >= n.step && step < n.step + n.length) return i;
        }
        return -1;
    }
    int freeSlot() const {
        for (int i = 0; i < kMaxNotes; ++i)
            if (notes[i].velocity <= 0.f) return i;
        return -1;
    }
};

// An effect in a slot: which one (an EffectKind as a number; 0 is none)
// and its settings, each 0..1.
struct EffectData {
    int kind = 0;
    float param[kEffectParams] = {};
};

// The mixer (the SONG screen's heads): a track's level in whole dB from its usual, and where it
// stands left to right, -50 (L 50) to 50 (R 50). The same for the master. 0 and 0 are the usual,
// and a song file leaves them out (`mix` on a track, `master` before the tracks).
constexpr int kMixLeastDb = -40, kMixMostDb = 6, kPanMost = 50;
// What the engine is told: a track's gain (its usual is kUsualTrackLevel, exactly, at 0 dB), the
// master's (1 at 0 dB), and a pan of -1..1.
inline float mixGain(int db, bool master) {
    const float usual = master ? 1.f : kUsualTrackLevel;
    return db == 0 ? usual : static_cast<float>(usual * std::pow(10.0, db / 20.0));
}
inline float panAmount(int pan) { return static_cast<float>(pan) / static_cast<float>(kPanMost); }

// (Memory only: a song file names an instrument by its word, instruments.hpp. Jx3p is JAX-EP on the device
// and stands where Twin Analog stood, whose tracks it takes: docs/plans/jx3p-plan.md, section 5. Fm6 is
// FM-6 and stands where QMOD stood, whose tracks it takes too: docs/plans/fm6-plan.md. Physic is PHYSIC,
// appended: docs/plans/physic-plan.md. Acid is ACID, appended after it: docs/plans/acid-plan.md.)
enum class TrackKind { None, Drums, Phazed, Jx3p, Sampler, Fm6, Physic, Acid, Cloud, Morph }; // (Cloud: CLOUD, appended: docs/plans/granular-plan.md; Morph: MORPH, after it: docs/plans/wavetable-plan.md)
// Played from the piano roll: everything but the drums. (What else there is
// to know about each instrument is in instruments.hpp.)
inline bool isSynth(TrackKind kind) { return kind != TrackKind::None && kind != TrackKind::Drums; }
// A track that plays one recording, the track's `sample`: the sampler, and CLOUD (its grains are read
// from it). Whatever means "this track holds a recording" (its uses, a delete, a rename, a file found
// again, CATCH, RECORD, the song file's `sound` line) asks this; what means the sampler's own settings
// asks for the sampler.
inline bool holdsRecording(TrackKind kind) { return kind == TrackKind::Sampler || kind == TrackKind::Cloud; }

// The patterns of every scene of a track, one piece a scene, MADE THE FIRST TIME SOMETHING IS
// WRITTEN THERE (scenes-plan.md: a song of kMaxScenes scenes uses a few, and fixed arrays at 64 were
// 4 MB a track, copied by every copy of it). Read, a scene with no piece is the one blank piece.
// So `t.drums[scene][bar]` reads and writes as an array did; a non-const track makes the piece even
// to read it, so whatever only looks takes the track `const` (or asks `has` first). A piece made and
// emptied again stays made: it is only memory, and reads as what it holds.
template <class Piece>
class SceneCells {
    static_assert(std::is_trivially_copyable<Piece>::value, "a piece is copied and compared as bytes");

public:
    using Value = decltype(Piece::v);
    SceneCells() = default;
    SceneCells(const SceneCells& other) { *this = other; }
    SceneCells& operator=(const SceneCells& other) {
        // Byte for byte, as the track's other fields. A piece here stays where it is (the blank one
        // copied in where `other` has none), so what holds a bar of it still holds that bar.
        for (int s = 0; s < kMaxScenes && this != &other; ++s)
            if (other.at_[s] || at_[s]) std::memcpy(static_cast<void*>(&made(s)), other.at_[s] ? other.at_[s].get() : &blank(), sizeof(Piece));
        return *this;
    }
    SceneCells(SceneCells&&) = default;
    SceneCells& operator=(SceneCells&&) = default;
    Value& operator[](int s) { return made(s).v; }
    const Value& operator[](int s) const { return at_[s] ? at_[s]->v : blank().v; }
    bool has(int s) const { return at_[s] != nullptr; }
    int held() const { // how many pieces are made
        int n = 0;
        for (const auto& p : at_) n += p ? 1 : 0;
        return n;
    }
    void clear() {
        for (auto& p : at_) p.reset();
    }
    void drop(int s) { at_[s].reset(); }                // back to none
    void take(int to, int from) { at_[to] = std::move(at_[from]); } // `to` has `from`'s piece, `from` none
    // Is scene `s` here as scene `t` of `other` is, byte for byte (no piece is the blank piece)?
    bool same(int s, const SceneCells& other, int t) const {
        if (!at_[s] && !other.at_[t]) return true;
        return std::memcmp(&(*this)[s], &other[t], sizeof(Piece)) == 0;
    }
    bool sameAs(const SceneCells& other) const { // every scene
        for (int s = 0; s < kMaxScenes; ++s)
            if (!same(s, other, s)) return false;
        return true;
    }
    // Every scene in turn, read (a scene with no piece is the blank one: going through makes none).
    struct Reading {
        const SceneCells* cells;
        int s;
        const Value& operator*() const { return (*cells)[s]; }
        Reading& operator++() { return ++s, *this; }
        bool operator!=(const Reading& other) const { return s != other.s; }
    };
    Reading begin() const { return {this, 0}; }
    Reading end() const { return {this, kMaxScenes}; }
    static const Piece& blank() {
        static const Piece none;
        return none;
    }

private:
    Piece& made(int s) {
        if (!at_[s]) at_[s].reset(new Piece(blank()));
        return *at_[s];
    }
    std::unique_ptr<Piece> at_[kMaxScenes];
};
struct DrumBars { DrumPattern v[kMaxBars]; };
struct NoteBars { NotePattern v[kMaxBars]; };
struct AutoLanes { // automation: [lane][bar * 16 + step], kAutoNone where nothing is written
    float v[kAutoLanes][kAutoSteps];
    AutoLanes() {
        for (auto& lane : v)
            for (float& x : lane) x = kAutoNone;
    }
};

// Everything a track holds but its patterns: one block of bytes, copied and compared so (the
// song's text cache, makeBlank), padding and all.
struct TrackFields {
    TrackKind kind = TrackKind::None;
    // A cell (this track in one scene) is 1 to 8 bars long. Bars past its
    // length are kept but not played.
    int bars[kMaxScenes];
    EffectData effects[kTrackSlots]; // on this track, one after the other
    float send[kBuses] = {};         // how much of it goes to the ECHO and the SPACE bus, 0..1
    int mixDb = 0;                   // its level on the mixer, in dB from its usual (kMixLeastDb..kMixMostDb)
    int mixPan = 0;                  // ...and its pan, -kPanMost (left) .. kPanMost (right)
    std::uint8_t glideFrom = 0;      // GLIDE FROM, a synth track's: which key of the moment before its glides come from
                                     // (kFromTop, kFromPaired, kFromNearest, kFromRandom: apotheca/glide_rule.hpp)
    ModSettings mods[kModsPerTrack]; // MOD 1 and MOD 2: the track's, not a scene's (apotheca/mod_rule.hpp). One that is OFF is
                                     // blank, and one whose target does not fit the track is OFF (mods.hpp); set by Session::setMod
    float laneLevel[kDrumLanes]; // each drum's volume
    char laneSample[kDrumLanes][64] = {}; // a recording on the lane in place of the built-in drum: its file's name ("kick", or "kicks/808" in a folder), or empty
    int laneFit[kDrumLanes] = {};         // stretch that recording to last this many steps at the song's tempo; 0: as recorded
    bool laneOneSlice[kDrumLanes] = {};   // each hit on the lane plays one slice of its sound and stops (false: it runs on to the next hit)
    drumsynth::Voice laneSynth[kDrumLanes]; // DRUM SYNTH: a lane's sound made from a model and its knobs (apotheca/drum_synth.hpp), in
                                          // place of a recording or the built-in drum; model 0 is none. A lane with one has no laneSample
    LaneEdit laneEdit[kDrumLanes];        // EDIT: what each lane does to its sound (START, END, GAIN...: lane_edit.hpp). The file is never written
    LaneFeel laneFeel[kDrumLanes];        // FEEL: each lane's HUMAN TIME and HUMAN LEVEL, 0..100 (apotheca/feel_rule.hpp). The track's,
                                          // not a scene's; live, nothing is written into the hits
    std::uint8_t laneSwing[kDrumLanes] = {}; // SWING: each lane's, 0..100 (apotheca/feel_rule.hpp): every second sixteenth up to a
                                          // third of a step late. The track's, live, as FEEL
    std::uint8_t laneChoke[kDrumLanes] = {}; // CHOKE: each lane's group, kChokeNone, 1 or 2 (drums.hpp). The track's; a new kit
                                          // has its hats in CHOKE 1 (Session::chokeNewKit), a blank TrackData none
    int preset = 0;              // a synth's preset, patch and macros
    PhazedPatch patch;           // when the track is Phazed
    Jx3pPatch jx3p = Jx3p::presetPatch(0); // when it is JAX-EP. Ids 32 and 33, VELOCITY and
                                 // BRILLIANCE, are the track's own: no preset writes them, so a new track's start here
    Fm6Patch fm6;                // when it is FM-6 (a new track's is its first preset: Session::addTrack)
    PhysicPatch physic;          // when it is PHYSIC (the same: its first preset, MARIMBA). Room for 26, 23 used
    AcidPatch acid;              // when it is ACID (the same: its first preset, Classic Saw). Room for 20, 14 used
    CloudPatch cloud;            // when it is CLOUD (the same: its first preset, Soft Cloud). Room for 24, 17 used
    MorphPatch morph;            // when it is MORPH (the same: its first preset, Analog Sweep). Room for 40, 29 used
    float macro[kSynthMacros];
    char name[11] = {};  // what the player calls the track ("BASS"); empty: its instrument's name
    bool muted = false;  // silent until unmuted; kept with the song
    bool solo = false;   // only soloed tracks are heard while any is; not kept with the song
    char sample[64] = {};           // when it is a sampler or a CLOUD (holdsRecording): its recording's file name (as a lane's), or empty
    int sampler[sampler::kSettings]; // ...and its settings

    TrackFields() {
        for (float& v : laneLevel) v = 1.f;
        for (float& m : macro) m = 0.5f;
        for (int& b : bars) b = 1;
        for (int i = 0; i < sampler::kSettings; ++i) sampler[i] = sampler::settingDefault(i);
    }
};
static_assert(std::is_trivially_copyable<TrackFields>::value, "a track's fields are kept and compared as bytes");

struct TrackData : TrackFields {
    // Its patterns, a piece a scene (SceneCells): the hits of a drum track, the notes of a synth
    // track (each only where written), and the automation, [scene][lane][bar * 16 + step], or
    // kAutoNone (see the engine's kAutoLanes).
    SceneCells<DrumBars> drums;
    SceneCells<NoteBars> notes;
    SceneCells<AutoLanes> automation;

    TrackData() = default;
    // A copy is the fields byte for byte and the patterns piece for piece.
    TrackData(const TrackData& other) : TrackFields(), drums(other.drums), notes(other.notes), automation(other.automation) { fieldsFrom(other); }
    TrackData& operator=(const TrackData& other) {
        if (this == &other) return *this;
        fieldsFrom(other);
        drums = other.drums;
        notes = other.notes;
        automation = other.automation;
        return *this;
    }
    TrackFields& fields() { return *this; }
    const TrackFields& fields() const { return *this; }
    void fieldsFrom(const TrackFields& other) { std::memcpy(static_cast<void*>(&fields()), &other, sizeof(TrackFields)); }
    bool sameFields(const TrackFields& other) const { return std::memcmp(&fields(), &other, sizeof(TrackFields)) == 0; }
    // Does scene `s` have a piece of any kind (else it has nothing, whatever its length says)?
    bool holdsPieces(int s) const { return drums.has(s) || notes.has(s) || automation.has(s); }
    int piecesHeld() const { return drums.held() + notes.held() + automation.held(); }
    // Are every scene's patterns here those of `other`, byte for byte (no piece is the blank piece)?
    bool samePatternsAs(const TrackData& other) const {
        for (int s = 0; s < kMaxScenes; ++s)
            if (!drums.same(s, other.drums, s) || !notes.same(s, other.notes, s) || !automation.same(s, other.automation, s)) return false;
        return true;
    }

    bool sceneEmpty(int scene) const {
        if (!drums.has(scene) && !notes.has(scene)) return true;
        for (int bar = 0; bar < bars[scene]; ++bar) {
            if (kind == TrackKind::Drums && !drums[scene][bar].empty()) return false;
            if (isSynth(kind) && !notes[scene][bar].empty()) return false;
        }
        return true;
    }
};
// (fieldsFrom copies the fields over the base: the patterns must start past its last byte, which
// they do when they are not aligned more loosely than it.)
static_assert(alignof(TrackFields) <= alignof(SceneCells<DrumBars>), "the patterns may not stand in the fields' last padding");

// A new kit's CHOKE groups: the built-in kit's two hats in CHOKE 1, so the closed hat cuts the open one
// (the owner's case, 2026-11-15), every other lane in none. The project only: Session::chokeNewKit tells
// the engine too.
inline int newKitChoke(int lane) { return lane == kClosedHatLane || lane == kOpenHatLane ? 1 : kChokeNone; }
inline void chokeTheHats(TrackData& track) {
    for (int lane = 0; lane < kDrumLanes; ++lane) track.laneChoke[lane] = static_cast<std::uint8_t>(newKitChoke(lane));
}

// What the two send buses of a new song hold, ECHO then SPACE: TAPE and HALL
// (the plan). A song file with no `busfx` lines (an old song) gets these too:
// its sends are all 0, so it sounds as it did, and the first send its owner
// turns up gives what it gives in a new song. THE ONE LINE to change when a
// new song should start with other effects.
constexpr EffectKind kNewSongBus[kBuses] = {EffectKind::Tape, EffectKind::Hall};

// What a song played as a song does at its end (Project::songEnd). The numbers are in song files: appended.
constexpr int kSongLoop = 0;
constexpr int kSongStop = 1;
// A number nobody knows (a later program's) is the usual, LOOP, as an unknown GLIDE FROM is TOP.
inline int songEndKind(int number) { return number == kSongStop ? kSongStop : kSongLoop; }

// Everything a Project holds but its tracks: the song's own lines and where the screens are. Kept
// apart so a song can be read back in place (parseSong) and a step of undo read without a whole
// Project (Session::readStep): each is a few hundred bytes, where the tracks are 12 MB.
struct ProjectHead {
    int repeats[kMaxScenes];
    int steps[kMaxScenes]; // how many steps each scene lasts, if it is cut short: 23 is a bar and seven steps. 0: every step of its longest cell
    EffectData masterEffects[kMasterSlots]; // on everything, after the tracks are mixed
    EffectData busEffects[kBuses];          // what the ECHO and the SPACE bus hold: one effect each, its MIX read as LEVEL
    float tempo = 120.f;
    int masterDb = 0, masterPan = 0; // the master's level and pan on the mixer, as a track's are
    int songEnd = 0; // AT END, played as a song: kSongLoop round to the first scene (as it always did), kSongStop stop there
    int scaleKey = 0;  // SCALE LOCK (scales.hpp): the song's key, 0 C .. 11 B
    int scaleKind = 0; // ...and its scale, kScaleOff (the roll is free) to kScaleBlues
    char chordPack[32] = {}; // the chord pack this song picks from; empty is the built-in one
    int track = 0; // the track and scene the Pattern and Device screens show
    int scene = 0;
    int bar = 0;   // ...and which bar of that cell
    bool playing = false;
    // Where the engine is, copied in once a frame for the screens to draw.
    // Not part of the song.
    int playScene = -1, playStep = -1, playRepeat = 0, playBar = 0;
    bool playLoop = false; // one scene going round, not the song
    bool writing = false;  // automation WRITE is on: a macro or effect setting moved while playing is recorded
    bool silenced = false; // a panic has shut the output and it has not opened again yet

    ProjectHead() {
        for (int& r : repeats) r = 1;
        for (int& n : steps) n = 0;
        for (int bus = 0; bus < kBuses; ++bus) setBusEffect(bus, kNewSongBus[bus]);
    }
    // Put an effect on a bus at its usual settings there (LEVEL at full).
    void setBusEffect(int bus, EffectKind kind) {
        busEffects[bus].kind = static_cast<int>(kind);
        for (int i = 0; i < kEffectParams; ++i) busEffects[bus].param[i] = effectDefault(kind, i, true);
    }
};

struct Project : ProjectHead {
    TrackData tracks[kNumTracks];

    Project() {
        tracks[0].kind = TrackKind::Drums; // a new project is one drum track
    }
    // Tracks are filled from the left, so the count is also the next free one.
    int trackCount() const {
        int n = 0;
        while (n < kNumTracks && tracks[n].kind != TrackKind::None) ++n;
        return n;
    }
    // How many scenes the song has: up to the last one with anything in it.
    int sceneCount() const {
        for (int scene = kMaxScenes - 1; scene > 0; --scene) {
            for (const TrackData& t : tracks)
                if (!t.sceneEmpty(scene)) return scene + 1;
        }
        return 1;
    }
    // A scene is as long as the longest cell in it.
    int sceneBars(int s) const {
        int most = 1;
        for (const TrackData& t : tracks)
            if (t.kind != TrackKind::None && t.bars[s] > most) most = t.bars[s];
        return most;
    }
    // How many steps a scene lasts, and how many of them fall in one of its bars (16, fewer in the bar it ends in, none after).
    int sceneSteps(int s) const {
        const int whole = sceneBars(s) * kSteps;
        return steps[s] > 0 && steps[s] < whole ? steps[s] : whole;
    }
    int barSteps(int s, int barIndex) const {
        const int left = sceneSteps(s) - barIndex * kSteps;
        return left > kSteps ? kSteps : (left < 0 ? 0 : left);
    }
    // GLIDE, for the roll to draw: what each note of a synth track's cell slides from and for how
    // long, as the scene goes round (lines[bar * kMaxNotes + slot], kMaxBars * kMaxNotes of them;
    // a chord's notes are in the slots NotePattern::render gives them, as the engine has them).
    // The rule is the engine's own (apotheca/glide_rule.hpp), by the track's GLIDE FROM. (RANDOM is
    // as it is the first time the scene plays after Play; later times round roll anew.)
    void glideLines(int trackIndex, int s, GlideLine* lines) const {
        GlideNote all[kMaxBars * kMaxNotes];
        const TrackData& t = tracks[trackIndex];
        const int bars = t.bars[s] < 1 ? 1 : (t.bars[s] > kMaxBars ? kMaxBars : t.bars[s]);
        for (int b = 0; b < bars; ++b) {
            NoteEvent out[kMaxNotes];
            t.notes[s][b].render(out);
            for (int i = 0; i < kMaxNotes; ++i) {
                GlideNote& n = all[b * kMaxNotes + i];
                n.sounds = out[i].velocity > 0.f;
                n.step = out[i].step;
                n.pitch = out[i].pitch;
                n.nudge = out[i].nudge;
                n.ratchet = out[i].ratchet;
                n.glide = glideKind(out[i].glide);
            }
        }
        apotheca::glideLines(all, bars, sceneSteps(s), lines, t.glideFrom, trackIndex);
    }
    TrackData& current() { return tracks[track]; }
    const TrackData& current() const { return tracks[track]; }
};

} // namespace apotheca
