#pragma once
// CHORD EFFECTS: what a chord does besides sound all at once. One place for the numbers,
// the names and the arithmetic, so the engine (which plays it), the piano roll (which sets
// and shows it) and the tests ask the same thing.
//
//   STRUM        the chord's notes start one after another. STRUM and WIDE STRUM are a few
//                milliseconds a note and each note is held to the chord's end; the three
//                lengths in steps make an ARPEGGIO: one note at a time, round and round
//                through the chord until it ends.
//   ORDER        which way that runs.
//   HUMAN TIME   every time the chord plays, each note starts a little early or late.
//   HUMAN LEVEL  ...and is a little louder or quieter.
//   LENGTHS      ...and is let go a little sooner (never later).
//
// Nothing here is rolled once and kept: every roll is worked out from WHERE the chord is
// (track, bar of its cell, step) and WHICH TIME it is being played (the steps since Play),
// so no two times round are alike, yet Play from the top gives the same performance again,
// on any number of helper threads and at any block length. No generator is walked.
//
// The numbers are kept in songs: appended, never given another meaning.

#include <cstdint>

namespace apotheca {

constexpr int kStrumOff = 0;
constexpr int kStrumTight = 1;   // STRUM
constexpr int kStrumWide = 2;    // WIDE STRUM
constexpr int kStrumQuarter = 3; // an arpeggio, a quarter of a step a note
constexpr int kStrumHalf = 4;    // ...half a step
constexpr int kStrumStep = 5;    // ...a step
constexpr int kStrumKinds = 6;

constexpr int kOrderUp = 0, kOrderDown = 1, kOrderUpDown = 2, kOrderRandom = 3, kOrderKinds = 4;

// How far apart a strum's notes are, in milliseconds. (A strum never takes more than half
// the chord's length, or half a ratchet's piece: a shorter chord is strummed faster.)
constexpr double kStrumMs = 9.0;
constexpr double kWideStrumMs = 26.0;
constexpr double kStrumMostOfChord = 0.5;
// HUMAN TIME at 100%: each note this much early or late at the most, in milliseconds.
constexpr double kHumanTimeMs = 12.0;
// HUMAN LEVEL at 100%: each note this much of its level up or down at the most.
constexpr double kHumanLevelMost = 0.15;
// LENGTHS at 100%: each note this much shorter at the most (never longer).
constexpr double kLengthsMost = 0.5;
// In an arpeggio a note is never moved by more than this much of the time to the next.
constexpr double kHumanMostOfGap = 0.25;

// The three amounts are percentages; a player steps through these.
constexpr int kAmountClicks = 6;
constexpr int kAmountAt[kAmountClicks] = {0, 10, 25, 50, 75, 100};
inline int amountPlace(int percent) { // the click at or just under a percentage
    int place = 0;
    for (int i = 0; i < kAmountClicks; ++i)
        if (percent >= kAmountAt[i]) place = i;
    return place;
}

// What a chord's notes carry. The first five are the chord's own settings; `index` and
// `count` say which of the chord's keys a note is, lowest first (count 0: not a chord's note).
struct ChordPlay {
    std::uint8_t strum = 0, order = 0;
    std::uint8_t time = 0, level = 0, lengths = 0; // 0..100
    std::uint8_t index = 0, count = 0;
    std::uint8_t spare = 0;

    // Nothing set: the chord plays as a chord always did. (ORDER alone does nothing.)
    bool atRest() const { return strum == 0 && time == 0 && level == 0 && lengths == 0; }
    bool arpeggio() const { return strum >= kStrumQuarter; }
    bool timed() const { return strum != 0 || time != 0; } // its notes do not all begin on the chord's own moment
};
static_assert(sizeof(ChordPlay) == 8, "a chord's play values are eight bytes, every one set");

// Numbers from outside (a song file, a command): what is not known is off, what is too much is the most.
inline ChordPlay chordPlayOf(int strum, int order, int time, int level, int lengths) {
    const auto amount = [](int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v)); };
    ChordPlay p;
    p.strum = static_cast<std::uint8_t>(strum > 0 && strum < kStrumKinds ? strum : kStrumOff);
    p.order = static_cast<std::uint8_t>(order > 0 && order < kOrderKinds ? order : kOrderUp);
    p.time = amount(time);
    p.level = amount(level);
    p.lengths = amount(lengths);
    return p;
}
inline ChordPlay chordPlayChecked(const ChordPlay& from, int maxKeys) {
    ChordPlay p = chordPlayOf(from.strum, from.order, from.time, from.level, from.lengths);
    if (from.count >= 1 && from.count <= maxKeys && from.index < from.count) {
        p.index = from.index;
        p.count = from.count;
    }
    return p;
}

inline const char* strumName(int strum) {
    static const char* const names[kStrumKinds] = {"OFF", "STRUM", "WIDE STRUM", "1/4 STEP", "1/2 STEP", "1 STEP"};
    return names[strum < 0 || strum >= kStrumKinds ? 0 : strum];
}
inline const char* orderName(int order) {
    static const char* const names[kOrderKinds] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
    return names[order < 0 || order >= kOrderKinds ? 0 : order];
}
// An arpeggio's time from one note to the next, in steps (0: not an arpeggio).
inline double arpeggioGap(int strum) { return strum == kStrumQuarter ? 0.25 : strum == kStrumHalf ? 0.5 : strum == kStrumStep ? 1.0 : 0.0; }
inline double strumMs(int strum) { return strum == kStrumTight ? kStrumMs : strum == kStrumWide ? kWideStrumMs : 0.0; }

// ---- the rolls ----------------------------------------------------------------------------

inline std::uint32_t chordHash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
// One playing of one chord: the track, the bar of its cell it is in, its step, and how many
// steps the song had played when that step began. (Not the scene's number: NEXT SCENE moves a
// scene that is playing to another number, and the chord goes on as it was. Two scenes are
// told apart by when they play.)
inline std::uint32_t chordSeed(int track, int bar, int step, long stepsSincePlay) {
    std::uint32_t h = chordHash(static_cast<std::uint32_t>(track) + 0x9E3779B9u);
    h = chordHash(h ^ (static_cast<std::uint32_t>(bar) * 0x85EBCA6Bu + 1u));
    h = chordHash(h ^ (static_cast<std::uint32_t>(step) * 0xC2B2AE35u + 2u));
    h = chordHash(h ^ (static_cast<std::uint32_t>(stepsSincePlay) * 0x27D4EB2Fu + 3u));
    return h;
}
enum ChordRoll : std::uint32_t { kRollTime = 1, kRollLevel = 2, kRollLengths = 3, kRollOrder = 4 };
// A number from 0 up to (not reaching) 1 for one thing about one note of that playing.
inline double chordUnit(std::uint32_t seed, ChordRoll what, int which) {
    const std::uint32_t h = chordHash(seed ^ chordHash(static_cast<std::uint32_t>(what) * 0x10000u + static_cast<std::uint32_t>(which) + 0x51ED270Bu));
    return static_cast<double>(h >> 8) * (1.0 / 16777216.0);
}
inline double chordSigned(std::uint32_t seed, ChordRoll what, int which) { return 2.0 * chordUnit(seed, what, which) - 1.0; } // -1 up to 1

// The keys 0..n-1 in a random order: order[place] is the key played in that place.
inline void chordShuffle(std::uint32_t seed, int which, int n, int* order) {
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int i = n - 1; i > 0; --i) {
        const int j = static_cast<int>(chordUnit(seed, kRollOrder, which * 16 + i) * (i + 1));
        const int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
}

// STRUM and WIDE STRUM: which place key `index` of `n` (lowest first) has in the strum.
// `stroke`: how many chords the track has played since Play; UP-DOWN goes up on the even
// ones and down on the odd, as a hand does.
inline int strumPlace(int order, int index, int n, std::uint32_t seed, long stroke) {
    if (n < 1 || index < 0 || index >= n) return 0;
    if (order == kOrderDown || (order == kOrderUpDown && (stroke & 1))) return n - 1 - index;
    if (order == kOrderRandom) {
        int keys[16];
        if (n > 16) return index;
        chordShuffle(seed, 0, n, keys);
        for (int place = 0; place < n; ++place)
            if (keys[place] == index) return place;
    }
    return index;
}

// An arpeggio: which key (0..n-1, lowest first) its note number `m` (0 the first) plays.
// UP-DOWN turns at the top and the bottom without playing either twice. RANDOM is a fresh
// order each time round, and (with three keys or more) never the same key twice running
// where two rounds meet.
inline int arpeggioKey(int order, long m, int n, std::uint32_t seed) {
    if (n <= 1 || m < 0) return 0;
    if (order == kOrderDown) return n - 1 - static_cast<int>(m % n);
    if (order == kOrderUpDown) {
        const int round = 2 * n - 2, at = static_cast<int>(m % round);
        return at < n ? at : round - at;
    }
    if (order == kOrderRandom && n <= 16) {
        const int round = static_cast<int>(m / n), at = static_cast<int>(m % n);
        int keys[16], before[16];
        chordShuffle(seed, round, n, keys);
        if (round > 0 && n >= 3) { // (the first two change places, so a round's last key is never moved)
            chordShuffle(seed, round - 1, n, before);
            if (keys[0] == before[n - 1]) {
                keys[0] = keys[1];
                keys[1] = before[n - 1];
            }
        }
        return keys[at];
    }
    return static_cast<int>(m % n);
}

} // namespace apotheca
