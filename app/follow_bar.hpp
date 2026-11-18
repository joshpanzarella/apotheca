#pragma once
// FOLLOW (owner, 2026-11-18: "if there are 32 steps in a drum sequence I think the playhead should move
// the view to the next set of steps"). While the song plays the scene on screen, the drum grid (and its
// slice view) and the piano roll show each bar of the cell as the playhead comes into it; the cursor
// keeps its step. Never while the player is at the controls: a button held or the stick pushed, and
// kFollowRestMs after, so a bar looked at by hand stays until the playhead next comes into a bar.
// One rule for both grids (tests/follow_test.cpp).

#include <cstdint>

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

} // namespace apotheca
