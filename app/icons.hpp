#pragma once
// A small drawing for every setting a screen shows (owner, 2026-11-24: "Every parameter needs an icon"): the
// synths' macros and their settings pages, and every effect's settings. A drawing is a few lines on a 20 by 14
// grid; which one a setting gets is decided by its NAME's words (`iconFor`), so a new setting called "LFO RATE" or
// "AMP DECAY" finds its drawing by itself. tests/app_test.cpp (`testEverySettingHasAnIcon`) walks every synth's
// macros and pages and every effect's settings, and fails a name no rule takes: give it a rule or a drawing here.
#include <SDL.h>

namespace apotheca {

constexpr int kIconW = 20, kIconH = 14;

// What the drawing is about, which decides its colour: the sound's colour (teal), movement and width (purple), time
// (the text's colour).
enum class IconFamily { Sound, Movement, Time };

struct IconStroke {
    int n;     // points
    int p[16]; // x,y pairs on the 20 by 14 grid
};
struct Icon {
    const char* id; // its own name, for the tests
    IconFamily family;
    IconStroke strokes[5];
};

// The drawing for a setting called `name`; null where no rule takes it (the test fails such a name).
const Icon* iconFor(const char* name);
// Drawn at (x, y), its top left; `lit` the full colour (a setting turned or under the cursor), else faded.
void drawIcon(SDL_Renderer* r, const Icon& icon, int x, int y, bool lit);

} // namespace apotheca
