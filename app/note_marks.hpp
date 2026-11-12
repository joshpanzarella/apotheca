#pragma once
// The marks a note effect leaves on a block of the piano roll, worked out
// apart from the drawing so they can be tested.

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
// begins, `at` (the note's step and nudge), and starts `steps` before that. A line that starts
// before the bar's top belongs, for that part, to the bar played before: so the roll also
// draws the lines of the bar played NEXT, moved down by this bar's length (`shift`), and what
// falls outside the bar is cut off. This says where a line's two ends are, and whether any
// of it is inside the bar's `rows` steps.
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
// The bar of the cell that plays after bar `bar` of it, in a scene of `sceneSteps` steps:
// the next one, or the first when the scene has come to its end and goes round.
inline int barPlayedNext(int bar, int cellBars, int sceneSteps) {
    return (bar + 1) * 16 < sceneSteps ? (bar + 1) % (cellBars < 1 ? 1 : cellBars) : 0;
}

// The right end of the piano roll's strip, in pixels: NUDGE, then what the note
// does besides play (RATCHET / EFFECT), then THROW. The effect moves left when
// the note is thrown, to leave THROW its room.
constexpr float kStripNudgeX = 390.f, kStripThrowX = 575.f, kStripRight = 632.f, kStripSpace = 10.f;
inline float stripEffectX(bool thrown) { return thrown ? 480.f : 510.f; }
// With X held the strip is RATCHET, GLIDE (at 250), THROW and NUDGE: the last two stand here,
// clear of the longest a glide's length reads ("1/4 STEP").
constexpr float kStripHeldGlideX = 250.f, kStripHeldThrowX = 365.f, kStripHeldNudgeX = 440.f;
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

} // namespace apotheca
