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
struct NotePattern {
    NoteEvent notes[kMaxNotes];

    bool empty() const {
        for (const NoteEvent& n : notes)
            if (n.velocity > 0.f) return false;
        return true;
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
    int track = 0; // the track and scene the Pattern and Device screens show
    int scene = 0;
    bool playing = false;

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
