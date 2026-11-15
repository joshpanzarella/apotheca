#pragma once
// FEEL as the drum grid handles it: what one click does to a lane's HUMAN TIME or HUMAN LEVEL,
// how each reads on the strip, and the dice. Apart from the screen, so it can be tested. What
// the feel DOES is the engine's: apotheca/feel_rule.hpp.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "apotheca/feel_rule.hpp"

namespace apotheca {

// "OFF", or the amount: "25%".
inline void feelAmountText(int percent, char* text, std::size_t size) {
    if (percent > 0) std::snprintf(text, size, "%d%%", percent);
    else std::snprintf(text, size, "OFF");
}

// One click of an amount: `by` is +1 or -1, through OFF, 10, 25, 50, 75, 100. It stops at each end.
inline int feelAmountStep(int percent, int by) {
    const int to = amountPlace(percent) + by;
    return to < 0 || to >= kAmountClicks ? percent : kAmountAt[to];
}

// The dice: small amounts only (OFF, 10 or 25% each, as a chord's dice roll its human amounts),
// never both off, and never what the lane has already. `dice` is the screen's generator, walked.
inline LaneFeel feelDice(LaneFeel before, std::uint32_t& dice) {
    static const std::uint8_t small[3] = {0, 10, 25};
    const auto roll = [&](std::uint32_t outOf) {
        dice = dice * 1664525u + 1013904223u;
        return (dice >> 12) % outOf;
    };
    for (;;) { // (eight things it can be, and at the most one is ruled out: this ends)
        LaneFeel feel;
        feel.time = small[roll(3)];
        feel.level = small[roll(3)];
        if (!feel.atRest() && feel != before) return feel;
    }
}

} // namespace apotheca
