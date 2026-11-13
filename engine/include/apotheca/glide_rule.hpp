#pragma once
// GLIDE on a note: how long it is, and which note it slides from. One rule, kept in one
// place, so that the engine (which plays it) and the piano roll (which draws it) cannot
// come to differ: both ask here.
//
// A note's glide is a number, kept in songs: appended, never given another meaning.
// 1 was "on" before a glide had a length, and it was half a step: so it still is.

#include <algorithm>

namespace apotheca {

constexpr int kGlideOff = 0;
constexpr int kGlideHalf = 1;    // half a step: what "on" was
constexpr int kGlideQuarter = 2; // a quarter of a step
constexpr int kGlideOne = 3;     // one step
constexpr int kGlideTwo = 4;     // two steps
constexpr int kGlideFull = 5;    // as long as it has been since the note it slides from began
constexpr int kGlideKinds = 6;

// The order a player steps through them, shortest first. A place is not the number.
constexpr int kGlideOrder[kGlideKinds] = {kGlideOff, kGlideQuarter, kGlideHalf, kGlideOne, kGlideTwo, kGlideFull};
inline int glideAt(int place) { return kGlideOrder[place < 0 ? 0 : (place >= kGlideKinds ? kGlideKinds - 1 : place)]; }
inline int glidePlace(int kind) {
    for (int place = 0; place < kGlideKinds; ++place)
        if (kGlideOrder[place] == kind) return place;
    return 0;
}
// A number from outside (a song file, a command): one that is not known is "on" as it was, half a step.
inline int glideKind(int number) { return number <= 0 ? kGlideOff : (number < kGlideKinds ? number : kGlideHalf); }
inline const char* glideName(int kind) {
    static const char* const names[kGlideKinds] = {"OFF", "1/2 STEP", "1/4 STEP", "1 STEP", "2 STEPS", "FULL"};
    return names[glideKind(kind)];
}

// Notes that begin within this much of a step of one another begin together.
constexpr double kGlideTogether = 1.0 / 256.0;

// How long a glide takes, in steps. `since`: how long ago the note it slides from began
// (FULL takes all of that). `hits`: the note's ratchet, 2..4, else anything: only the first
// piece of a ratchet slides, and it is there before the second begins.
inline double glideSteps(int kind, double since, int hits) {
    double steps = kind == kGlideQuarter ? 0.25 : kind == kGlideOne ? 1.0 : kind == kGlideTwo ? 2.0 : kind == kGlideFull ? since : 0.5;
    if (hits >= 2 && steps > 0.85 / hits) steps = 0.85 / hits;
    return steps;
}

// Which note a glide comes from: the last note the track began before this one. Notes that
// begin together count as one moment: none of them slides from another of them, and what
// comes after slides from the highest of them. (The later pieces of a ratchet are not notes
// beginning: they are not told to this.)
struct GlideTrail {
    struct From {
        int pitch = -1;  // -1: there is none
        double at = 0.0; // when it began, in steps
    };
    From last, before;

    void clear() { last = before = From(); }
    // What a note beginning at `at` slides from.
    From from(double at) const { return last.pitch < 0 || at > last.at + kGlideTogether ? last : before; }
    // A note has begun.
    void began(int pitch, double at) {
        if (last.pitch < 0 || at > last.at + kGlideTogether) {
            before = last;
            last.pitch = pitch;
            last.at = at;
        } else if (pitch > last.pitch) {
            last.pitch = pitch;
        }
    }
};

// ---- for the screen: what each note of a looping cell slides from -------------------------

struct GlideNote {
    int step = 0, pitch = 0;
    float nudge = 0.f;
    int ratchet = 0, glide = 0;
    bool sounds = false;
};
struct GlideLine {
    int from = -1;      // the key it slides from (-1: it does not slide)
    double steps = 0.0; // for how long, from its own start
};

constexpr int kGlideBars = 8, kGlideNotes = 64; // (kMaxBars and kMaxNotes: checked in engine.hpp)

// A cell of `cellBars` bars of kGlideNotes notes each (notes[bar * kGlideNotes + slot], in the
// order the engine is given them), in a scene that lasts `sceneSteps` and goes round. For each
// note, what it slides from once the scene has gone round once: so a glide into the first
// step comes from the last note of the loop. A cell shorter than its scene plays more than
// once in it; each bar is answered for the first time it plays.
inline void glideLines(const GlideNote* notes, int cellBars, int sceneSteps, GlideLine* lines) {
    struct Begin {
        double at;
        int index; // bar * kGlideNotes + slot, or -1: a later playing of a bar, not answered for
        int pitch;
    };
    static const int kMost = kGlideBars * kGlideNotes;
    Begin begins[kMost];
    int count = 0;
    for (int i = 0; i < cellBars * kGlideNotes && i < kMost; ++i) lines[i] = GlideLine();
    if (cellBars < 1 || cellBars > kGlideBars) return;
    for (int bar = 0; bar * 16 < sceneSteps && count < kMost; ++bar) {
        const int left = sceneSteps - bar * 16, cellBar = bar % cellBars;
        for (int slot = 0; slot < kGlideNotes && count < kMost; ++slot) {
            const GlideNote& n = notes[cellBar * kGlideNotes + slot];
            if (!n.sounds || n.step < 0 || n.step >= (left < 16 ? left : 16)) continue; // past the scene's end: never played
            begins[count].at = static_cast<double>(bar * 16 + n.step) + n.nudge;
            begins[count].index = bar < cellBars ? cellBar * kGlideNotes + slot : -1;
            begins[count].pitch = n.pitch;
            ++count;
        }
    }
    std::stable_sort(begins, begins + count, [](const Begin& a, const Begin& b) { return a.at < b.at; });
    GlideTrail trail;
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < count; ++i) {
            const Begin& b = begins[i];
            const double at = b.at + round * static_cast<double>(sceneSteps);
            const GlideTrail::From from = trail.from(at);
            if (round == 1 && b.index >= 0) {
                const GlideNote& n = notes[b.index];
                if (n.glide != kGlideOff && from.pitch >= 0 && from.pitch != n.pitch) {
                    lines[b.index].from = from.pitch;
                    lines[b.index].steps = glideSteps(n.glide, at - from.at, n.ratchet);
                }
            }
            trail.began(b.pitch, at);
        }
    }
}

} // namespace apotheca
