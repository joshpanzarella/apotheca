#pragma once
// The marks a note effect leaves on a block of the piano roll, worked out
// apart from the drawing so they can be tested.

#include <cmath>

namespace apotheca {

// A ratchet cuts the part of a note in its first step into as many pieces as
// it plays notes, with dark lines between them. `tall` is that part in
// pixels, `pieces` is 2, 3 or 4. Gives where each line starts (from the top
// of the block) and how thick the lines are; 0 when there is no room for them.
//
// The lines are as thick as fits (3, 2 or 1 pixels) with every piece at least
// two pixels tall. The pieces are the same height; pixels left over go to
// the outside pieces first, so the block looks the same from both ends.
inline int ratchetLines(int tall, int pieces, int lineTops[3]) {
    if (pieces < 2 || pieces > 4) return 0;
    for (int thick = 3; thick >= 1; --thick) {
        const int light = tall - thick * (pieces - 1);
        if (light < 2 * pieces) continue;
        int extra[4] = {0, 0, 0, 0}, left = light % pieces;
        if (left % 2 == 1 && pieces % 2 == 1) extra[pieces / 2] = 1, --left; // an odd one out goes in the middle
        for (int i = 0; left >= 2; ++i, left -= 2) extra[i] = extra[pieces - 1 - i] = 1;
        for (int i = 0; left > 0 && i < pieces; ++i)
            if (!extra[i]) extra[i] = 1, --left;
        int y = 0;
        for (int i = 0; i < pieces - 1; ++i) {
            y += light / pieces + extra[i];
            lineTops[i] = y;
            y += thick;
        }
        return thick;
    }
    return 0;
}

// A glide's line on the roll, in steps down the bar the roll shows: it ends where its note
// begins, `at` (the note's step and nudge), and starts `steps` before that. The note may be in
// another bar of the cell, or in this one the next time round: `shift` is how many steps after
// the top of the bar shown its own bar begins (glideShifts). What falls outside the bar is cut
// off. This says where a line's two ends are, and whether any of it is inside the bar's `rows`
// steps.
struct GlideSpan {
    double from = 0.0, to = 0.0; // in steps from the bar's top; from may be above it, to below
    bool seen = false;
};
inline GlideSpan glideSpan(double at, double steps, int shift, int rows) {
    GlideSpan span;
    span.to = at + shift;
    span.from = span.to - steps;
    span.seen = steps > 0.0 && span.to > 0.0 && span.from < rows;
    return span;
}
// A line belongs to a note, and it may start bars before it: a FULL glide from a note three
// bars back crosses every bar between. So the roll draws, in the bar it shows, the lines of
// EVERY bar of the cell. This says where bar `noteBar` of the cell begins, in steps after the
// top of bar `shownBar`: once for each time the bar shown plays in the scene (a cell shorter
// than its scene plays more than once) and that time is as long as the `rows` the roll shows,
// this time round the scene and the next (a glide into the first step comes from the scene's
// end). A note's line is answered for the first time its bar plays (glideLines), so that is
// the playing measured to. Gives how many there are, no two the same; kGlideShifts is the most.
constexpr int kGlideShifts = 16;
inline int glideShifts(int noteBar, int shownBar, int cellBars, int sceneSteps, int rows, int shifts[kGlideShifts]) {
    int count = 0;
    if (cellBars < 1) cellBars = 1;
    for (int played = shownBar; played * 16 < sceneSteps && count + 2 <= kGlideShifts; played += cellBars) {
        const int left = sceneSteps - played * 16;
        if ((left < 16 ? left : 16) != rows) continue; // cut short that time: not the bar as it is shown
        for (int round = 0; round < 2; ++round) shifts[count++] = (noteBar - played) * 16 + round * sceneSteps;
    }
    return count;
}

// The line itself, in pixels: little squares along it from (x0, y0) down to (x1, y1), `size`
// across, each one whole inside the box or left out (so a line stops clean at the roll's edge).
// Squares that sit in one column or one row are handed over as one rectangle: emit(x, y, w, h).
// (One call a pixel, sixty-four long lines cost a frame's worth of time on the handheld.)
template <typename Emit>
inline void glideLineRects(double x0, double y0, double x1, double y1, int size, int left, int top, int right, int bottom, Emit emit) {
    const double dx = x1 - x0, dy = y1 - y0;
    const double longest = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    const int count = static_cast<int>(std::ceil(longest)) + 1;
    int first = 0, last = count;
    if (dy > 0.0) { // only the part of it that is level with the box is walked
        const double a = (top - size - 1 - y0) / dy * count, b = (bottom + 1 - y0) / dy * count;
        if (b < 0.0 || a > count) return;
        if (a > first) first = static_cast<int>(a);
        if (b < last) last = static_cast<int>(b) + 1;
    }
    int rx = 0, ry = 0, rw = 0, rh = 0; // the rectangle being gathered (rw 0: none)
    for (int i = first; i <= last; ++i) {
        const double t = static_cast<double>(i) / count;
        const int x = static_cast<int>(std::floor(x0 + dx * t)) - size / 2, y = static_cast<int>(std::floor(y0 + dy * t)) - size / 2;
        if (x < left || x + size > right || y < top || y + size > bottom) continue;
        if (rw && x == rx && rw == size && y >= ry && y <= ry + rh) {
            if (y + size > ry + rh) rh = y + size - ry; // the same column: taller
            continue;
        }
        if (rw && y == ry && rh == size && x >= rx - size && x <= rx + rw) {
            if (x < rx) rw += rx - x, rx = x; // the same row: wider, to either side
            if (x + size > rx + rw) rw = x + size - rx;
            continue;
        }
        if (rw) emit(rx, ry, rw, rh);
        rx = x, ry = y, rw = rh = size;
    }
    if (rw) emit(rx, ry, rw, rh);
}

// The right end of the piano roll's strip, in pixels: NUDGE, then what the note
// does besides play (RATCHET / EFFECT), then THROW. The effect moves left when
// the note is thrown, to leave THROW its room.
constexpr float kStripNudgeX = 390.f, kStripThrowX = 575.f, kStripRight = 632.f, kStripSpace = 10.f;
inline float stripEffectX(bool thrown) { return thrown ? 480.f : 510.f; }
// With X held the strip is RATCHET, GLIDE (at 250), THROW and GLIDE FROM (the track's: which
// key of the moment before its glides come from): the last two stand here, clear of the
// longest a glide's length reads ("1/4 STEP"). NUDGE, which nothing sets with X held, is not
// drawn then; it is back when X is let go.
constexpr float kStripHeldRatchetX = 160.f, kStripHeldGlideX = 250.f, kStripHeldThrowX = 385.f, kStripHeldFromX = 500.f;
// (The bar under the strip draws each field's button at the same x: ui::hintsUnder.)
// How wide NUDGE's value may be: up to a gap before whatever comes after it.
inline float stripNudgeRoom(bool effect, bool thrown) {
    return (effect ? stripEffectX(thrown) : thrown ? kStripThrowX : kStripRight) - kStripSpace - kStripNudgeX;
}
// How wide a glide's length may be there (it has the field to itself: no ratchet), with a gap
// left before THROW when the note is thrown. "1/4 STEP" beside a THROW is drawn in the middle size.
inline float stripGlideRoom(bool thrown) { return (thrown ? kStripThrowX - kStripSpace : kStripRight) - stripEffectX(thrown); }
// The largest size a value fits its room in: 0 large, 1 medium, 2 small. `widths`
// is the text's width in each. ("45% EARLY" beside an effect and a THROW is small.)
inline int stripFont(const float widths[3], float room) { return widths[0] <= room ? 0 : widths[1] <= room ? 1 : 2; }
// Where THROW's value goes and in which size: it ends at kStripRight or before, as the other
// fields do. ("SPACE" at 575 in the large size ran to pixel 638 of 640.) It moves left as far as
// it needs, but never to before `least` (a gap after the effect before it; 0 when there is
// none), and is drawn smaller only when that is not far enough. Returns the size; sets `x`.
inline int stripThrowFont(const float widths[3], float least, float* x) {
    for (int font = 0; font < 3; ++font) {
        const float fits = kStripRight - widths[font];
        *x = fits >= kStripThrowX ? kStripThrowX : fits > least ? fits : least;
        if (*x + widths[font] <= kStripRight) return font;
    }
    return 2;
}

// With X held on a CHORD the strip is its six effects: the one picked in full at the left (its
// name, and its value in the large size), all six in two columns of three (a name, and its
// value further on: by as much as the column's longest name needs), and THROW. Measured on
// the screen at its fullest (--view chordfx-worst): "WIDE STRUM" ends at 135 at the left and
// at 322 in the list, "100%" at 488, "SPACE" at 588.
constexpr float kChordFxListX = 160.f, kChordFxColumn = 184.f, kChordFxThrowX = 522.f;
constexpr float kChordFxValueIn[2] = {76.f, 108.f};
constexpr int kChordFxRowTop = 5, kChordFxRowH = 15;
// The one picked, in full, starts here. The bar under the strip (ui::hintsUnder) draws LEFT / RIGHT
// (it sets the one picked) at kChordFxSetX, under the large value (owner, 2026-11-13: it was under
// the list's first column, wrong for the second), UP / DOWN (it picks one of the six) under the
// list at kChordFxListX, A under THROW at kChordFxThrowX, and the dice at the right-hand end.
constexpr float kChordFxPickedX = 8.f, kChordFxSetX = kChordFxPickedX;

} // namespace apotheca
