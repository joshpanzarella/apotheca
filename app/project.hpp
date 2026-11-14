#pragma once
// The song as the screens see it. The screens own this; the engine is told
// about every change through commands, and never the other way round.

#include <cmath>
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
    // What the note does besides play (hold X on it):
    std::uint8_t ratchet = 0; // 2..4: that many notes, evenly, inside its first step. 0: the one
    std::uint8_t glide = 0;   // it slides to its pitch from the note before it on the track, for this long (kGlideHalf
                              // and the rest, apotheca/glide_rule.hpp; 1, which was "on", is half a step). 0: it does not
    std::uint8_t throwTo = 0; // THROW: 1 ECHO, 2 SPACE, 3 both. The track's send to that bus is full while the note lasts
    std::uint8_t spare = 0;   // (so the note has no bytes that are not set: it is compared and hashed whole)
    ChordPlay play;           // only on a chord's note as NotePattern::render gives it: which key of the chord it is,
                              // and the chord's effects (apotheca/chord_rule.hpp). A loose note's is all nothing.
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
// and stands where Twin Analog stood, whose tracks it takes: docs/plans/jx3p-plan.md, section 5.)
enum class TrackKind { None, Drums, Phazed, Jx3p, Sampler, Qmod };
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
    EffectData effects[kTrackSlots]; // on this track, one after the other
    float send[kBuses] = {};         // how much of it goes to the ECHO and the SPACE bus, 0..1
    int mixDb = 0;                   // its level on the mixer, in dB from its usual (kMixLeastDb..kMixMostDb)
    int mixPan = 0;                  // ...and its pan, -kPanMost (left) .. kPanMost (right)
    std::uint8_t glideFrom = 0;      // GLIDE FROM, a synth track's: which key of the moment before its glides come from
                                     // (kFromTop, kFromPaired, kFromNearest, kFromRandom: apotheca/glide_rule.hpp)
    float laneLevel[kDrumLanes]; // each drum's volume
    char laneSample[kDrumLanes][64] = {}; // a recording on the lane in place of the built-in drum: its file's name ("kick", or "kicks/808" in a folder), or empty
    int laneFit[kDrumLanes] = {};         // stretch that recording to last this many steps at the song's tempo; 0: as recorded
    bool laneOneSlice[kDrumLanes] = {};   // each hit on the lane plays one slice of its sound and stops (false: it runs on to the next hit)
    int preset = 0;              // a synth's preset, patch and macros
    PhazedPatch patch;           // when the track is Phazed
    Jx3pPatch jx3p = Jx3p::presetPatch(0); // when it is JAX-EP. Ids 32 and 33, VELOCITY and
                                 // BRILLIANCE, are the track's own: no preset writes them, so a new track's start here
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

// What the two send buses of a new song hold, ECHO then SPACE: TAPE and HALL
// (the plan). A song file with no `busfx` lines (an old song) gets these too:
// its sends are all 0, so it sounds as it did, and the first send its owner
// turns up gives what it gives in a new song. THE ONE LINE to change when a
// new song should start with other effects.
constexpr EffectKind kNewSongBus[kBuses] = {EffectKind::Tape, EffectKind::Hall};

struct Project {
    TrackData tracks[kNumTracks];
    int repeats[kMaxScenes];
    int steps[kMaxScenes]; // how many steps each scene lasts, if it is cut short: 23 is a bar and seven steps. 0: every step of its longest cell
    EffectData masterEffects[kMasterSlots]; // on everything, after the tracks are mixed
    EffectData busEffects[kBuses];          // what the ECHO and the SPACE bus hold: one effect each, its MIX read as LEVEL
    float tempo = 120.f;
    int masterDb = 0, masterPan = 0; // the master's level and pan on the mixer, as a track's are
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
        for (int& n : steps) n = 0;
        tracks[0].kind = TrackKind::Drums; // a new project is one drum track
        for (int bus = 0; bus < kBuses; ++bus) setBusEffect(bus, kNewSongBus[bus]);
    }
    // Put an effect on a bus at its usual settings there (LEVEL at full).
    void setBusEffect(int bus, EffectKind kind) {
        busEffects[bus].kind = static_cast<int>(kind);
        for (int i = 0; i < kEffectParams; ++i) busEffects[bus].param[i] = effectDefault(kind, i, true);
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
