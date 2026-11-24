#pragma once
// FOLLOW (owner, 2026-11-18: "if there are 32 steps in a drum sequence I think the playhead should move
// the view to the next set of steps"). While the song plays the scene on screen, the drum grid (and its
// slice view) and the piano roll show each bar of the cell as the playhead comes into it; the cursor
// keeps its step. Never while the player is at the controls: a button held or the stick pushed, and
// kFollowRestMs after, so a bar looked at by hand stays until the playhead next comes into a bar.
// One rule for both grids (tests/follow_test.cpp).

#include <cstdint>
#include <vector>

namespace apotheca {

constexpr std::uint32_t kFollowRestMs = 1500;

class BarFollow {
public:
    // Called every frame. playingHere: the song plays the scene on screen. playingBar: the bar of the
    // cell it is in (0-based). atControls: a button held or the stick pushed. Returns the bar to show
    // now, or -1 to leave the view where it is.
    int step(bool playingHere, int playingBar, bool atControls, std::uint32_t nowMs) {
        if (atControls) touched_ = true, touchedMs_ = nowMs;
        if (!playingHere || playingBar < 0) {
            last_ = -1;
            return -1;
        }
        const bool entered = playingBar != last_;
        last_ = playingBar;
        if (!entered) return -1;
        if (touched_ && nowMs - touchedMs_ < kFollowRestMs) return -1;
        return playingBar;
    }

private:
    int last_ = -1;
    bool touched_ = false;
    std::uint32_t touchedMs_ = 0;
};


// THE ROLL'S KEYS FOLLOW THE NOTES (owner, 2026-11-24: "center, to the best of our ability, the current notes on the
// piano roll as it automatically advances ... on the most concentrated areas"). When FOLLOW shows a bar, the roll's
// keys move to the stretch of `keys` keys that holds the most of that bar's notes; of those, the one with the most of
// their length in it; of those, the one whose middle is nearest the middle of the notes it holds (so they sit in the
// centre); of those, the one nearest where the keys are. A bar with no notes: -1, the keys stay. Each note is a
// pitch and its length in steps. Returns the lowest key to show, held to lowest..highest.
struct HeldNote {
    int pitch;
    float length;
};
inline int keysToShow(const std::vector<HeldNote>& notes, int keys, int lowest, int highest, int now) {
    if (notes.empty() || keys <= 0) return -1;
    int best = -1, bestCount = -1;
    float bestLength = -1.f, bestOff = 0.f;
    int bestFromNow = 0;
    for (int low = lowest; low + keys - 1 <= highest; ++low) {
        int count = 0, lo = 1000, hi = -1;
        float length = 0.f;
        for (const HeldNote& n : notes)
            if (n.pitch >= low && n.pitch < low + keys) {
                ++count, length += n.length;
                lo = n.pitch < lo ? n.pitch : lo;
                hi = n.pitch > hi ? n.pitch : hi;
            }
        if (count == 0) continue;
        // How far the window's middle is from the middle of the notes it holds (in half keys, so it stays whole).
        const float off = static_cast<float>(2 * low + keys - 1 - (lo + hi));
        const float offSize = off < 0.f ? -off : off;
        const int fromNow = low > now ? low - now : now - low;
        const bool better = count > bestCount ||
                            (count == bestCount && (length > bestLength + 1e-4f ||
                                                    (length > bestLength - 1e-4f && (offSize < bestOff || (offSize == bestOff && fromNow < bestFromNow)))));
        if (better) best = low, bestCount = count, bestLength = length, bestOff = offSize, bestFromNow = fromNow;
    }
    return best;
}

} // namespace apotheca
