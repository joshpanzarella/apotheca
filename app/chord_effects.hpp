#pragma once
// CHORD EFFECTS as the piano roll handles them: the list a player picks from with X held on a
// chord (up / down), what one click left or right does to each, how each reads on the strip,
// and the dice. Apart from the screen, so it can be tested. What the effects DO is the
// engine's: apotheca/chord_rule.hpp.

#include <cstddef>
#include <cstdio>

#include "project.hpp"

namespace apotheca {

// The list, in the order it is shown. (A place in the list, not a number kept anywhere.)
constexpr int kFxRatchet = 0, kFxStrum = 1, kFxOrder = 2, kFxTime = 3, kFxLevel = 4, kFxLengths = 5;
constexpr int kChordEffects = 6;

inline const char* chordEffectName(int effect) {
    static const char* const names[kChordEffects] = {"RATCHET", "STRUM", "ORDER", "HUMAN TIME", "HUMAN LEVEL", "LENGTHS"};
    return names[effect < 0 || effect >= kChordEffects ? 0 : effect];
}

// Is it doing something: set to other than nothing.
inline bool chordEffectSet(const ChordEvent& c, int effect) {
    switch (effect) {
    case kFxRatchet: return c.ratchet >= 2;
    case kFxStrum: return c.play.strum != kStrumOff;
    case kFxOrder: return c.play.order != kOrderUp;
    case kFxTime: return c.play.time != 0;
    case kFxLevel: return c.play.level != 0;
    case kFxLengths: return c.play.lengths != 0;
    }
    return false;
}
// Does it wait on another: ORDER is the way STRUM runs, so it does nothing with STRUM off; and
// a RATCHET is not played while the chord is an arpeggio. The strip draws these dim.
inline bool chordEffectWaits(const ChordEvent& c, int effect) {
    if (effect == kFxOrder) return c.play.strum == kStrumOff;
    if (effect == kFxRatchet) return c.play.arpeggio();
    return false;
}
// Any of the five that are a chord's alone (a ratchet has its own mark on the roll).
inline bool chordPlays(const ChordEvent& c) { return !c.play.atRest(); }

inline void chordEffectValue(const ChordEvent& c, int effect, char* text, std::size_t size) {
    const auto amount = [&](int percent) {
        if (percent > 0) std::snprintf(text, size, "%d%%", percent);
        else std::snprintf(text, size, "OFF");
    };
    switch (effect) {
    case kFxRatchet:
        if (c.ratchet >= 2) std::snprintf(text, size, "x%d", c.ratchet);
        else std::snprintf(text, size, "OFF");
        break;
    case kFxStrum: std::snprintf(text, size, "%s", strumName(c.play.strum)); break;
    case kFxOrder: std::snprintf(text, size, "%s", orderName(c.play.order)); break;
    case kFxTime: amount(c.play.time); break;
    case kFxLevel: amount(c.play.level); break;
    case kFxLengths: amount(c.play.lengths); break;
    default: std::snprintf(text, size, "-");
    }
}

// One click: `by` is +1 (right) or -1 (left). It stops at each end. True if anything changed.
inline bool chordEffectStep(ChordEvent& c, int effect, int by) {
    const auto choice = [&](std::uint8_t& value, int kinds) {
        const int to = static_cast<int>(value) + by;
        if (to < 0 || to >= kinds) return false;
        value = static_cast<std::uint8_t>(to);
        return true;
    };
    const auto amount = [&](std::uint8_t& value) {
        const int to = amountPlace(value) + by;
        if (to < 0 || to >= kAmountClicks) return false;
        value = static_cast<std::uint8_t>(kAmountAt[to]);
        return true;
    };
    switch (effect) {
    case kFxRatchet: {
        const int hits = c.ratchet < 2 ? 1 : c.ratchet, to = hits + by;
        if (to < 1 || to > kMaxRatchet) return false;
        c.ratchet = static_cast<std::uint8_t>(to < 2 ? 0 : to); // "one" is kept one way: 0
        return true;
    }
    case kFxStrum: return choice(c.play.strum, kStrumKinds);
    case kFxOrder: return choice(c.play.order, kOrderKinds);
    case kFxTime: return amount(c.play.time);
    case kFxLevel: return amount(c.play.level);
    case kFxLengths: return amount(c.play.lengths);
    }
    return false;
}

// The dice. `roll(n)` gives 0..n-1. Always something, and never what the chord had. Kept
// musical: a strum far more often than an arpeggio, the human amounts small (10 or 25%).
//   RATCHET      30 in 100: x2 (half of those), x3, x4
//   STRUM        35 OFF, 30 STRUM, 20 WIDE STRUM, 5 each 1/4, 1/2 and 1 STEP
//   ORDER        with a strum: 50 UP, 20 DOWN, 15 UP-DOWN, 15 RANDOM
//   HUMAN TIME   50 in 100: 10% (two in three) or 25%
//   HUMAN LEVEL  the same
//   LENGTHS      30 in 100: 10% (two in three) or 25%
// An arpeggio takes no ratchet (it would not be played). THROW is not the dice's to set.
template <typename Roll>
inline void chordEffectRoll(ChordEvent& c, Roll roll) {
    static const std::uint8_t ratchets[] = {2, 2, 3, 4}, small[] = {10, 10, 25};
    const ChordEvent was = c;
    for (;;) { // (there is always another roll to be had, so this ends)
        c.ratchet = roll(100) < 30 ? ratchets[roll(4)] : 0;
        const int strum = static_cast<int>(roll(100));
        c.play.strum = static_cast<std::uint8_t>(strum < 35 ? kStrumOff : strum < 65 ? kStrumTight : strum < 85 ? kStrumWide : strum < 90 ? kStrumQuarter : strum < 95 ? kStrumHalf : kStrumStep);
        const int order = static_cast<int>(roll(100));
        c.play.order = static_cast<std::uint8_t>(c.play.strum == kStrumOff || order < 50 ? kOrderUp : order < 70 ? kOrderDown : order < 85 ? kOrderUpDown : kOrderRandom);
        c.play.time = roll(100) < 50 ? small[roll(3)] : 0;
        c.play.level = roll(100) < 50 ? small[roll(3)] : 0;
        c.play.lengths = roll(100) < 30 ? small[roll(3)] : 0;
        if (c.play.arpeggio()) c.ratchet = 0;
        const bool something = c.ratchet >= 2 || chordPlays(c);
        const bool same = c.ratchet == was.ratchet && c.play.strum == was.play.strum && c.play.order == was.play.order && c.play.time == was.play.time && c.play.level == was.play.level && c.play.lengths == was.play.lengths;
        if (something && !same) return;
    }
}

// The strip with X let go: what the chord does, in one short line. One effect is named with
// its value ("WIDE STRUM", "x3", "TIME 25%"); more than one: the first, and how many besides.
// (ORDER is never the first: it only counts beside a STRUM, which comes before it.)
inline void chordEffectLine(const ChordEvent& c, char* text, std::size_t size) {
    static const char* const brief[kChordEffects] = {"", "", "", "TIME ", "LEVEL ", "LENGTHS "};
    int first = -1, others = 0;
    for (int effect = 0; effect < kChordEffects; ++effect) {
        if (!chordEffectSet(c, effect) || chordEffectWaits(c, effect)) continue;
        if (first < 0) first = effect;
        else ++others;
    }
    text[0] = 0;
    if (first < 0) return;
    char value[16];
    chordEffectValue(c, first, value, sizeof value);
    if (others) std::snprintf(text, size, "%s%s +%d", brief[first], value, others);
    else std::snprintf(text, size, "%s%s", brief[first], value);
}

} // namespace apotheca
