#pragma once
// The FILL: one more bar a cell (one track in one scene) can have, played INSTEAD of the cell's own
// bar in the scene's LAST bar, on the scene's LAST time round (docs/plans/fills-ideas.md, design 1;
// owner, 2026-11-25). One place for the rule, so the engine (which plays it), the app (MIDI
// export, the playhead, WRITE) and the tests ask the same thing.
//
// It is kept in the cell's eighth bar (kFillSlot), which a cell of one, two or four bars never
// plays: no new pattern storage, so every copy of a cell or a scene carries it, automation too. A
// cell of eight bars has no FILL: that place is its own last bar.
//
// "The last time round": REPEATS' last, in a song. A scene played alone (looping) counts its time
// rounds the same way, round and round, and plays its FILL on the last of each REPEATS of them.
// The scene's last bar: the bar its last step is in, a short last bar too (a scene cut short).

namespace apotheca {

constexpr int kFillBars = 8;             // a cell at its longest (kMaxBars, engine.hpp)
constexpr int kFillSlot = kFillBars - 1; // the bar of a cell a FILL is kept in: its eighth

// Is bar `bar` of a scene (bars of `meter` steps, `steps` long in all) its LAST bar, played on its
// LAST time round (`round` of `repeats`, from 0)? There a cell's FILL plays.
inline bool fillTime(int bar, int meter, int steps, int round, int repeats) {
    return (bar + 1) * meter >= steps && round + 1 >= repeats;
}

// Which bar of a cell of `bars` bars plays at bar `bar` of its scene: its own, round and round
// (bar % bars), or its FILL where it has one on and it is `atFillTime`.
inline int cellBarOf(int bars, bool fillOn, int bar, bool atFillTime) {
    return fillOn && bars < kFillBars && atFillTime ? kFillSlot : bar % bars;
}

} // namespace apotheca
