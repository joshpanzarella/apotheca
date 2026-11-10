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
    bool fromDice[kDrumLanes][kSteps] = {}; // hits the dice placed and nobody has touched since
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
    int step = 0, pitch = 0, length = 0;
    float velocity = 0.f; // 0 = empty slot
};

// A chord placed in a bar. It stays one thing: one level, one length, one
// name, and it can be swapped for another chord later. Its notes are worked
// out from it each time, never stored.
constexpr int kMaxChords = 16, kChordNotes = 8, kMaxInversion = 3;
struct ChordEvent {
    int step = 0, length = 0;
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

enum class TrackKind { None, Drums, Phazed, Twin };
inline bool isSynth(TrackKind kind) { return kind == TrackKind::Phazed || kind == TrackKind::Twin; }

struct TrackData {
    TrackKind kind = TrackKind::None;
    DrumPattern drums[kMaxScenes];
    NotePattern notes[kMaxScenes];
    float laneLevel[kDrumLanes]; // each drum's volume
    int preset = 0;              // a synth's preset, patch and macros
    PhazedPatch patch;           // when the track is Phazed
    TwinPatch twin;              // when it is Twin Analog
    float macro[phazed::kMacros];

    TrackData() {
        for (float& v : laneLevel) v = 1.f;
        for (float& m : macro) m = 0.5f;
    }
    bool sceneEmpty(int scene) const {
        return kind == TrackKind::Drums ? drums[scene].empty() : isSynth(kind) ? notes[scene].empty() : true;
    }
};

struct Project {
    TrackData tracks[kNumTracks];
    int repeats[kMaxScenes];
    float tempo = 120.f;
    char chordPack[32] = {}; // the chord pack this song picks from; empty is the built-in one
    int track = 0; // the track and scene the Pattern and Device screens show
    int scene = 0;
    bool playing = false;
    // Where the engine is, copied in once a frame for the screens to draw.
    // Not part of the song.
    int playScene = -1, playStep = -1, playRepeat = 0;
    bool playLoop = false; // one scene going round, not the song

    Project() {
        for (int& r : repeats) r = 1;
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
    TrackData& current() { return tracks[track]; }
    const TrackData& current() const { return tracks[track]; }
};

} // namespace apotheca
