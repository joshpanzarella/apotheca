// Where things are on the drum grid, in pixels on the 640x480 screen. Shared
// by the grid (pattern_screen.cpp) and its slice view (pattern_slices.cpp),
// so the steps of one sit exactly where the steps of the other do.
#pragma once

namespace apotheca {
namespace pattern_layout {

constexpr int kGridLeft = 116, kGridTop = 54, kStepW = 32, kLaneH = 41;
constexpr int kCellW = kStepW - 2, kCellH = kLaneH - 3;
constexpr int kNameX = 8, kNameW = 104;
constexpr int kBlockW = kCellW - 8, kBlockFull = kCellH - 8;
// The strip under the grid that spells out what is under the cursor: at the
// same height in both views, so it does not jump when one turns into the other.
constexpr int kStripY = kGridTop + 8 * kLaneH + 6;
// With X held on a hit the strip is its five step effects. Where each field starts: the strip
// draws its name and value there, and the bar under it draws the button that sets it there
// (ui::hintsUnder), so the two cannot drift apart.
constexpr float kFxPitchX = 190.f, kFxRatchetX = 260.f, kFxReverseX = 355.f, kFxCutX = 450.f, kFxThrowX = 555.f;

} // namespace pattern_layout
} // namespace apotheca
