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

// The right end of the piano roll's strip, in pixels: NUDGE, then what the note
// does besides play (RATCHET / EFFECT), then THROW. The effect moves left when
// the note is thrown, to leave THROW its room.
constexpr float kStripNudgeX = 390.f, kStripThrowX = 575.f, kStripRight = 632.f, kStripSpace = 10.f;
inline float stripEffectX(bool thrown) { return thrown ? 480.f : 510.f; }
// How wide NUDGE's value may be: up to a gap before whatever comes after it.
inline float stripNudgeRoom(bool effect, bool thrown) {
    return (effect ? stripEffectX(thrown) : thrown ? kStripThrowX : kStripRight) - kStripSpace - kStripNudgeX;
}
// The largest size a value fits its room in: 0 large, 1 medium, 2 small. `widths`
// is the text's width in each. ("45% EARLY" beside an effect and a THROW is small.)
inline int stripFont(const float widths[3], float room) { return widths[0] <= room ? 0 : widths[1] <= room ? 1 : 2; }

} // namespace apotheca
