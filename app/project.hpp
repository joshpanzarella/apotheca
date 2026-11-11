#pragma once
// The song as the screens see it. The screens own this; the engine is told
// about every change through commands, and never the other way round.

#include <cstdint>

#include "apotheca/engine.hpp"

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
};

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

enum class TrackKind { None, Drums, Phazed, Twin, Sampler, Qmod };
// Played from the piano roll: everything but the drums. (What else there is
// to know about each instrument is in instruments.hpp.)
inline bool isSynth(TrackKind kind) { return kind != TrackKind::None && kind != TrackKind::Drums; }

struct TrackData {
    TrackKind kind = TrackKind::None;
    // A cell (this track in one scene) is 1 to 8 bars long. Bars past its
    // length are kept but not played.
    DrumPattern drums[kMaxScenes][kMaxBars];
    NotePattern notes[kMaxScenes][kMaxBars];
    int bars[kMaxScenes];
    EffectData effects[kEffectSlots]; // on this track, one after the other
    float laneLevel[kDrumLanes]; // each drum's volume
    char laneSample[kDrumLanes][64] = {}; // a recording on the lane in place of the built-in drum: its file's name ("kick", or "kicks/808" in a folder), or empty
    int laneFit[kDrumLanes] = {};         // stretch that recording to last this many steps at the song's tempo; 0: as recorded
    bool laneOneSlice[kDrumLanes] = {};   // each hit on the lane plays one slice of its sound and stops (false: it runs on to the next hit)
    int preset = 0;              // a synth's preset, patch and macros
    PhazedPatch patch;           // when the track is Phazed
    TwinPatch twin;              // when it is Twin Analog
    QmodPatch qmod;              // when it is QMOD
    float macro[kSynthMacros];
    // Automation: [scene][lane][bar * 16 + step], or kAutoNone (see the engine's kAutoLanes).
    float automation[kMaxScenes][kAutoLanes][kAutoSteps];
    char name[11] = {};  // what the player calls the track ("BASS"); empty: its instrument's name
    bool muted = false;  // silent until unmuted; kept with the song
    bool solo = false;   // only soloed tracks are heard while any is; not kept with the song
    char sample[64] = {};           // when it is a sampler: its recording's file name (as a lane's), or empty
    int sampler[sampler::kSettings]; // ...and its settings

    TrackData() {
        for (float& v : laneLevel) v = 1.f;
        for (float& m : macro) m = 0.5f;
        for (int& b : bars) b = 1;
        for (int i = 0; i < sampler::kSettings; ++i) sampler[i] = sampler::settingDefault(i);
        for (auto& scene : automation)
            for (auto& lane : scene)
                for (float& v : lane) v = kAutoNone;
    }
    bool sceneEmpty(int scene) const {
        for (int bar = 0; bar < bars[scene]; ++bar) {
            if (kind == TrackKind::Drums && !drums[scene][bar].empty()) return false;
            if (isSynth(kind) && !notes[scene][bar].empty()) return false;
        }
        return true;
    }
};

struct Project {
    TrackData tracks[kNumTracks];
    int repeats[kMaxScenes];
    int steps[kMaxScenes]; // steps to a bar in each scene: 16, or fewer for an odd-length loop
    EffectData masterEffects[kEffectSlots]; // on everything, after the tracks are mixed
    float tempo = 120.f;
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
    bool busy = false;     // the handheld cannot keep up: synths are doing plainer work (Engine::qualityLimit)
    bool silenced = false; // a panic has shut the output and it has not opened again yet

    Project() {
        for (int& r : repeats) r = 1;
        for (int& n : steps) n = kSteps;
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
    TrackData& current() { return tracks[track]; }
    const TrackData& current() const { return tracks[track]; }
};

} // namespace apotheca
