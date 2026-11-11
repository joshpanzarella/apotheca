#pragma once
// The look every screen shares: the palette, the boxes, the top bar with its
// tabs, the hint bar and the scope. Colours and sizes are the ones in
// docs/screens/screens.html.

#include <SDL.h>

#include "apotheca/engine.hpp"
#include "session.hpp"
#include "text.hpp"

namespace apotheca {

struct Fonts {
    Font small;  // 12: labels, hints, tabs
    Font medium; // 14: names
    Font large;  // 16: values
};

namespace ui {

constexpr SDL_Color kBg = {20, 17, 14, 255};
constexpr SDL_Color kPanel = {31, 26, 22, 255};
constexpr SDL_Color kPanel2 = {42, 35, 29, 255};
constexpr SDL_Color kLine = {67, 57, 47, 255};
constexpr SDL_Color kText = {224, 213, 191, 255};
constexpr SDL_Color kDim = {143, 131, 112, 255};
constexpr SDL_Color kTeal = {111, 191, 163, 255};
constexpr SDL_Color kPurple = {154, 118, 176, 255};
constexpr SDL_Color kBrass = {201, 161, 90, 255};
constexpr SDL_Color kStop = {196, 84, 70, 255}; // the stop square in the top bar: red, so it reads as "stopped"

constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kTopBarH = 26, kHintsH = 24;

// How fast the stick moves and turns things, on every screen: 1 is as first
// built; lower is slower. The owner asked for a quarter off (2026-11).
// It is a variable so it can become a setting, and so the tests can measure
// the screens at their own speeds.
inline float gStickSpeed = 0.75f;

// What the end of the hint bar says about the help card: "MENU: HELP", or
// "HOLD SELECT: HELP" on a handheld whose MENU button the firmware keeps.
inline const char* gHelpHint = "MENU: HELP";

void fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c);
void outline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, int thickness = 1);
void box(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color inside, SDL_Color edge);
SDL_Color faded(SDL_Color c, int alpha);

// Clears the screen and draws the bar across the top: the three screens as
// tabs (L2 and R2 move along them), what is being worked on, the tempo and
// whether it is playing. Returns the x where the tempo's number ends and the
// x of the play mark, for a screen that puts a cursor on the tempo.
struct TopBar { float tempoNumberRight, tempoRight; };
TopBar topBar(SDL_Renderer* r, const Fonts& fonts, View view, const char* context, const Project& project);

// A line that runs across a recording's picture while it is heard. It is
// timed here, from when the sound was asked for (the engine does not say
// where a sound has got to): right for a recording played as it is, near
// enough for one played faster or slower.
struct Playhead {
    Uint32 since = 0;       // when it was started; 0: never
    float seconds = 0.f;    // how long the whole recording lasts, played as it is
    float from = 0.f;       // where in it the sound began, 0..1
    float rate = 1.f;       // how fast it moves: 2 is an octave up
    float lasts = 0.f;      // it is over after this many seconds, wherever it has got to (0: only at the end)
    void start(Uint32 nowMs, float wholeSeconds, float fromPart = 0.f, float speed = 1.f, float forSeconds = 0.f) {
        since = nowMs ? nowMs : 1;
        seconds = wholeSeconds;
        from = fromPart;
        rate = speed;
        lasts = forSeconds;
    }
    // Where it is, 0..1 of the recording; below zero when nothing is being heard.
    float at(Uint32 nowMs) const {
        if (since == 0 || seconds <= 0.f || nowMs < since) return -1.f;
        const float gone = static_cast<float>(nowMs - since) / 1000.f;
        const float where = from + gone * rate / seconds;
        return where > 1.f || (lasts > 0.f && gone > lasts) ? -1.f : where;
    }
};
// Draws it over a picture `w` wide that starts at `x`.
void drawPlayhead(SDL_Renderer* r, const Playhead& head, Uint32 nowMs, int x, int y, int w, int h);

// The bar across the bottom: button names in cream boxes, each with what it does.
struct Hint { const char* key; const char* does; };
// What is true everywhere (X hears what is under the cursor; the stick or
// D-pad moves) is not listed each time: it is on the help card, which comes
// up while SELECT is held (App). A direction that either the stick or the
// D-pad gives is written as the direction alone. A hint that does not fit
// is left out; the end of the bar says how to see the help, if there is room.
void hints(SDL_Renderer* r, const Fonts& fonts, const Hint* list, int count);

// A named value in a box. `part` 0..1 draws a bar; below 0 draws none.
// `centre` draws the bar outward from the middle (for values that go both ways).
void valueBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, bool centre = false);

// A macro: its name, a small picture of what it does in the top right, its
// value, and a bar that grows from the middle. Dim until it is turned away
// from the middle, so the ones in use stand out.
void macroBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held);

// How far through a scene the song is: one block per time the scene plays.
// `play` is the pass it is on (from 0) and `step` the step within it.
void sceneProgress(SDL_Renderer* r, int x, int y, int w, int h, int plays, int play, int step, int steps = kSteps); // `steps` in one pass

// One track's latest output as a line.
void scope(SDL_Renderer* r, const Engine& engine, int x, int y, int w, int h);

// The name a track goes by on screen: "2 PHAZED".
void trackName(const Project& project, int track, char* out, int size);

// A MIDI note number as a name: 60 is "C4".
void noteName(int pitch, char* out, int size);

} // namespace ui
} // namespace apotheca
