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
// With X held on a lane's name the strip is the lane's FEEL: HUMAN TIME, HUMAN LEVEL, and the
// copy onto every lane. The field and the button's chip under it share these, as above.
constexpr float kFeelTimeX = 190.f, kFeelLevelX = 320.f, kFeelAllX = 450.f;
// A lane with a feel wears a mark at the left of its name: the upper piece for HUMAN TIME, the lower for HUMAN LEVEL.
constexpr int kFeelMarkX = kNameX + 3, kFeelMarkW = 3, kFeelMarkH = 12, kFeelMarkInset = 5;
// A lane in a CHOKE group wears a brass mark at the bottom right of its name: one piece for CHOKE 1, two
// for CHOKE 2, the right one ending at kChokeMarkRight, kChokeMarkInset up from the box's foot.
constexpr int kChokeMarkRight = kNameX + kNameW - 4, kChokeMarkW = 8, kChokeMarkH = 3, kChokeMarkGap = 3, kChokeMarkInset = 4;

} // namespace pattern_layout
} // namespace apotheca
