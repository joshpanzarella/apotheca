#pragma once
// FEEL: a drum lane played by a hand. One place for the numbers and the arithmetic, so the
// engine (which plays it), the drum grid (which sets and shows it) and the tests ask the same
// thing. It is a chord's HUMAN TIME and HUMAN LEVEL (chord_rule.hpp), for every hit of a lane:
//
//   HUMAN TIME   every time a hit plays it comes a little early or late.
//   HUMAN LEVEL  ...and a little louder or quieter.
//
// It is LIVE: nothing is written into the hits. A hit on a main beat (steps 1, 5, 9, 13) moves
// half as much as one between them: the beat stays where it is and what is between leans.
//
// Nothing is rolled once and kept, and no generator is walked: every roll is worked out from
// WHERE the hit is (track, lane, bar of its cell, step) and WHICH TIME it is being played (the
// steps since Play). So no two times round are alike, yet Play from the top gives the same
// performance again, on any number of helper threads and at any block length, and a bounce is
// what was heard.
//
// A RUN OF SLICES MOVES AS ONE. A hit that starts further into its sound (a slice of a
// recording) straight after a hit on the step before it carries that sound on without a seam
// (DrumMachine::trigger). Moved each by itself, every slice would meet the sound a few
// milliseconds off and cut it. So such a hit takes the TIME of the hit its run begins with
// (feelRunBack: back over the bar lines inside its cell too); its LEVEL is its own.
//
// The amounts are percentages, the six a chord's are (kAmountAt). They are kept in songs.

#include "apotheca/chord_rule.hpp"
#include "apotheca/drums.hpp"

namespace apotheca {

// A hit on a main beat moves this much of what the others do, in time and in level.
constexpr double kFeelBeatShare = 0.5;
// HUMAN TIME never moves a hit by more than this much of a step (at a tempo so fast that
// kHumanTimeMs would be more: over 312 beats a minute).
constexpr double kFeelMostOfStep = 0.25;
// The quietest HUMAN LEVEL leaves a hit, and the loudest (a hit written at full only gets quieter).
constexpr double kFeelLeastLevel = 0.02, kFeelMostLevel = 1.0;

// A lane's feel. Both at 0: the lane plays as it always did.
struct LaneFeel {
    std::uint8_t time = 0, level = 0; // 0..100
    bool atRest() const { return time == 0 && level == 0; }
    bool operator==(const LaneFeel& o) const { return time == o.time && level == o.level; }
    bool operator!=(const LaneFeel& o) const { return !(*this == o); }
};
// Numbers from outside (a song file, a command): what is too much is the most.
inline LaneFeel laneFeelOf(int time, int level) {
    const auto amount = [](int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v)); };
    LaneFeel feel;
    feel.time = amount(time);
    feel.level = amount(level);
    return feel;
}

inline bool feelOnBeat(int step) { return (step & 3) == 0; } // steps 1, 5, 9, 13
inline double feelShare(int step) { return feelOnBeat(step) ? kFeelBeatShare : 1.0; }

// One playing of one hit: the track, the lane, the bar of its cell, its step, and how many steps
// the song had played when that step began. (Not the scene's number: see chordSeed.)
inline std::uint32_t feelSeed(int track, int lane, int bar, int step, long stepsSincePlay) {
    return chordHash(chordSeed(track, bar, step, stepsSincePlay) ^ (static_cast<std::uint32_t>(lane) * 0x9E3779B1u + 0x7F4A7C15u));
}
// -1 up to 1: how far of the most this hit goes, early (below zero) or late; quieter or louder.
inline double feelTimeRoll(std::uint32_t seed) { return chordSigned(seed, kRollTime, 0); }
inline double feelLevelRoll(std::uint32_t seed) { return chordSigned(seed, kRollLevel, 0); }

// HUMAN TIME: how far a hit on `step` is moved, in samples (below zero: early). Not yet whole.
inline double feelSamples(int time, int step, std::uint32_t seed, double samplesPerMs, double samplesPerStep) {
    if (time <= 0) return 0.0;
    double most = time * 0.01 * kHumanTimeMs * samplesPerMs * feelShare(step);
    const double room = kFeelMostOfStep * samplesPerStep;
    if (most > room) most = room;
    return most * feelTimeRoll(seed);
}
// HUMAN LEVEL: the hit's level this time.
inline float feelLevel(float level, int amount, int step, std::uint32_t seed) {
    if (amount <= 0) return level;
    const double to = level * (1.0 + amount * 0.01 * kHumanLevelMost * feelShare(step) * feelLevelRoll(seed));
    return static_cast<float>(to > kFeelMostLevel ? kFeelMostLevel : (to < kFeelLeastLevel ? kFeelLeastLevel : to));
}

// The run a hit belongs to. A hit that starts further into its sound and has a hit on the step
// before it takes the TIME of the hit its run begins with; any other hit begins its own.
//
// How many steps before the hit on `step` of bar `bar` of its cell its run begins (0: with
// itself). `cell` is the cell's bars, each with level[lane][step] and start[lane][step]; `bar`
// counts from the cell's first.
//
// A run goes back over a bar line INSIDE the cell: a recording fitted to two bars and played in
// order is one sound from its first step to its thirty-second, and bar 2 rolled by itself met
// bar 1's sound some milliseconds off and cut it. It does not go back from the cell's first bar
// to its last: what played before that is another time round (or another scene), and a slice
// there never carried on from it anyway unless it was the very next piece of the sound.
template <class Bar>
inline int feelRunBack(const Bar* cell, int bar, int lane, int step) {
    int back = 0;
    for (;;) {
        if (cell[bar].start[lane][step] <= 0.f) break; // from the top: its own
        int barBefore = bar, stepBefore = step - 1;
        if (stepBefore < 0) {
            if (bar == 0) break;
            barBefore = bar - 1;
            stepBefore = kSteps - 1;
        }
        if (cell[barBefore].level[lane][stepBefore] <= 0.f) break; // nothing on the step before it
        bar = barBefore;
        step = stepBefore;
        ++back;
    }
    return back;
}

} // namespace apotheca
