#pragma once
// GLIDE on a note: how long it is, and which note it slides from. One rule, kept in one
// place, so that the engine (which plays it) and the piano roll (which draws it) cannot
// come to differ: both ask here.
//
// A note's glide is a number, kept in songs: appended, never given another meaning.
// 1 was "on" before a glide had a length, and it was half a step: so it still is.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "apotheca/chord_rule.hpp"

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

// ---- GLIDE FROM: which key of the moment before a note slides from ------------------------
//
// A track's setting. The numbers are kept in songs: appended, never given another meaning.
constexpr int kFromTop = 0;     // every note from the highest key of the moment before
constexpr int kFromPaired = 1;  // by pitch order, spread across: the lowest new note from the lowest old key,
                                // the highest from the highest, those between spaced evenly
constexpr int kFromNearest = 2; // each from the key nearest in pitch (a tie: the higher)
constexpr int kFromRandom = 3;  // each from a key rolled for where and when the note is (never a generator walked)
constexpr int kGlideFroms = 4;
// A number from outside (a song file, a command): one that is not known is TOP.
inline int glideFromKind(int number) { return number > 0 && number < kGlideFroms ? number : kFromTop; }
inline const char* glideFromName(int kind) {
    static const char* const names[kGlideFroms] = {"TOP", "PAIRED", "NEAREST", "RANDOM"};
    return names[glideFromKind(kind)];
}

// The keys of one moment: one bit a key, so every key there is fits and nothing is ever left
// out. The same key twice in a moment is one key.
struct GlideKeys {
    std::uint64_t bits[2] = {0, 0};

    void add(int pitch) {
        if (pitch >= 0 && pitch < 128) bits[pitch >> 6] |= std::uint64_t(1) << (pitch & 63);
    }
    bool has(int pitch) const { return pitch >= 0 && pitch < 128 && ((bits[pitch >> 6] >> (pitch & 63)) & 1) != 0; }
    bool empty() const { return (bits[0] | bits[1]) == 0; }
    int count() const {
        int n = 0;
        for (int pitch = 0; pitch < 128; ++pitch) n += has(pitch) ? 1 : 0;
        return n;
    }
    int top() const { // the highest (-1: there is none)
        for (int pitch = 127; pitch >= 0; --pitch)
            if (has(pitch)) return pitch;
        return -1;
    }
    int at(int k) const { // the k-th from the bottom, 0 the lowest (-1: there are not so many)
        for (int pitch = 0; pitch < 128; ++pitch)
            if (has(pitch) && k-- == 0) return pitch;
        return -1;
    }
    int below(int pitch) const { // how many are lower than this
        int n = 0;
        for (int p = 0; p < pitch && p < 128; ++p) n += has(p) ? 1 : 0;
        return n;
    }
};

// RANDOM's roll is for one note of one moment: the track, and the whole step the moment is
// nearest to, counted from Play (so no two times round are alike, yet Play from the top gives
// the same again; not the scene's number, which NEXT SCENE changes under a playing song).
inline std::uint32_t glideSeed(int track, double at) { return chordSeed(track, 0, 0, static_cast<long>(std::floor(at + 0.5))); }

// The key of `old` (the moment before) that a note slides from. `rank` and `count`: the note is
// the rank-th lowest (0 the lowest) of the `count` keys of its own moment (PAIRED asks; a note
// alone is 0 of 1). -1: there is none.
inline int glideFromKey(int type, const GlideKeys& old, int pitch, int rank, int count, std::uint32_t seed) {
    const int m = old.count();
    if (m < 1) return -1;
    if (type == kFromPaired) {
        if (count <= 1) return old.top(); // one note alone: from the top
        if (rank < 0) rank = 0;
        if (rank > count - 1) rank = count - 1;
        return old.at((2 * rank * (m - 1) + (count - 1)) / (2 * (count - 1))); // rank * (m - 1) / (count - 1), a half rounded up
    }
    if (type == kFromNearest) {
        int best = -1, far = 1000;
        for (int k = 0; k < 128; ++k) {
            if (!old.has(k)) continue;
            const int d = k > pitch ? k - pitch : pitch - k;
            if (d <= far) { // (going up: a tie is taken by the higher)
                far = d;
                best = k;
            }
        }
        return best;
    }
    if (type == kFromRandom) {
        int k = static_cast<int>(chordUnit(seed, kRollGlide, pitch) * m);
        return old.at(k < m ? k : m - 1);
    }
    return old.top();
}

// The most notes that can be told as coming: what one step can ask, every note of this
// bar's step and every early note of the next bar's. One more than that is not kept (a
// pattern cannot ask it), and is then not counted by PAIRED.
constexpr int kGlideComing = 2 * 64;

// Which notes a glide may come from: the keys of the moment the track last began before this
// note. Notes that begin together count as one moment: none of them slides from another of
// them, and what comes after slides from one of them (GLIDE FROM says which). (The later
// pieces of a ratchet are not notes beginning: they are not told to this.)
//
// PAIRED needs to know a note's whole moment when its first note begins, so the notes a step
// is about to begin are told first (`window`, then `tellComing` for each): the engine does
// that at each step for a PAIRED track, and glideLines below in the same steps.
struct GlideTrail {
    struct From {
        int pitch = -1;  // -1: there is none
        double at = 0.0; // when its moment began, in steps
    };
    struct Moment {
        GlideKeys keys;
        double at = 0.0; // when its first note began
        bool any = false;
    };
    struct Coming {
        double at = 0.0;
        std::uint8_t pitch = 0;
        bool ahead = false; // told from the step after the one playing (a note asked a step ahead)
    };
    Moment last, before;
    Coming coming[kGlideComing];
    int comingCount = 0;

    void clear() {
        last = before = Moment();
        comingCount = 0;
    }
    // A step begins: what was told as coming before `from` has begun, or never will.
    void window(double from) {
        int kept = 0;
        for (int i = 0; i < comingCount; ++i)
            if (coming[i].at >= from) coming[kept++] = coming[i];
        comingCount = kept;
    }
    // A note is going to begin at `at`.
    void tellComing(int pitch, double at, bool ahead = false) {
        if (comingCount >= kGlideComing) return;
        coming[comingCount].at = at;
        coming[comingCount].pitch = static_cast<std::uint8_t>(pitch);
        coming[comingCount].ahead = ahead;
        ++comingCount;
    }
    // What was told from the step after is forgotten (that step is another scene's now), or is
    // only what is coming (a new step has begun: it is this step's).
    void forgetAhead() {
        int kept = 0;
        for (int i = 0; i < comingCount; ++i)
            if (!coming[i].ahead) coming[kept++] = coming[i];
        comingCount = kept;
    }
    void aheadIsNow() {
        for (int i = 0; i < comingCount; ++i) coming[i].ahead = false;
    }
    bool joins(double at) const { return last.any && at <= last.at + kGlideTogether; } // the moment last begun is its own
    // What a note beginning at `at` slides from. `seed`: glideSeed(track, at), for RANDOM.
    From from(int type, int pitch, double at, std::uint32_t seed) const {
        const Moment& old = joins(at) ? before : last;
        From result;
        if (!old.any) return result;
        int rank = 0, count = 1;
        if (type == kFromPaired) {
            // Its own moment: the keys that have begun in it, and those told as coming in it.
            GlideKeys own;
            double first = at; // when the moment begins
            if (joins(at)) {
                own = last.keys;
                first = last.at;
            } else {
                bool found = false;
                for (int i = 0; i < comingCount; ++i) {
                    if (last.any && coming[i].at <= last.at + kGlideTogether) continue; // (of the moment before)
                    if (!found || coming[i].at < first) first = coming[i].at;
                    found = true;
                }
                if (!found || at < first || at > first + kGlideTogether) first = at; // (it was not told: alone, or with what follows it closely)
            }
            for (int i = 0; i < comingCount; ++i)
                if (coming[i].at >= first && coming[i].at <= first + kGlideTogether) own.add(coming[i].pitch);
            own.add(pitch);
            rank = own.below(pitch);
            count = own.count();
        }
        result.pitch = glideFromKey(type, old.keys, pitch, rank, count, seed);
        result.at = old.at;
        return result;
    }
    // A note has begun.
    void began(int pitch, double at) {
        if (!joins(at)) {
            before = last;
            last = Moment();
            last.any = true;
            last.at = at;
        }
        last.keys.add(pitch);
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
static_assert(kGlideComing == 2 * kGlideNotes, "a step can ask a bar's notes and the next bar's early ones");

// A cell of `cellBars` bars of kGlideNotes notes each (notes[bar * kGlideNotes + slot], in the
// order the engine is given them), in a scene that lasts `sceneSteps` and goes round. For each
// note, what it slides from once the scene has gone round once: so a glide into the first
// step comes from the last note of the loop. A cell shorter than its scene plays more than
// once in it; each bar is answered for the first time it plays. `type`: the track's GLIDE
// FROM; `track`: its number, for RANDOM, which rolls anew each time round: `timesRound` says
// for which (0: the first time the scene plays after Play, which is what the roll draws).
inline void glideLines(const GlideNote* notes, int cellBars, int sceneSteps, GlideLine* lines, int type = kFromTop, int track = 0, int timesRound = 0) {
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
    type = glideFromKind(type);
    GlideTrail trail;
    double told = -1e30; // the whole step whose notes have been told as coming
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < count; ++i) {
            const Begin& b = begins[i];
            const double at = b.at + round * static_cast<double>(sceneSteps);
            if (type == kFromPaired && std::floor(at) != told) {
                // As the engine's step does: the notes this whole step begins are told first.
                told = std::floor(at);
                trail.window(told);
                for (int j = i; j < count; ++j) {
                    const double then = begins[j].at + round * static_cast<double>(sceneSteps);
                    if (std::floor(then) != told) break;
                    trail.tellComing(begins[j].pitch, then);
                }
                // The scene's last step also asks, a step ahead, for the early notes of the first
                // step of the next time round: they begin in this step, and are told with it.
                // (So `told` is kept from one round to the next: they are not told twice.)
                for (int j = 0; j < count; ++j) {
                    const double then = begins[j].at + (round + 1) * static_cast<double>(sceneSteps);
                    if (std::floor(then) != told) break;
                    trail.tellComing(begins[j].pitch, then);
                }
            }
            if (round == 1 && b.index >= 0) {
                const GlideNote& n = notes[b.index];
                const GlideTrail::From from = trail.from(type, n.pitch, at, glideSeed(track, b.at + timesRound * static_cast<double>(sceneSteps)));
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
