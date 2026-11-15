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
// (feelHead); its LEVEL is its own.
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

// The step whose TIME a hit on `step` takes: its own, or, for a hit that starts further into its
// sound and has a hit on the step before it, the one its run begins with. `level` and `start`
// are the lane's sixteen steps of the bar. A run does not go back over a bar line: step 1
// begins one.
inline int feelHead(const float* level, const float* start, int step) {
    while (step > 0 && start[step] > 0.f && level[step - 1] > 0.f) --step;
    return step;
}

} // namespace apotheca
