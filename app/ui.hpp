#pragma once
// The look every screen shares: the palette, the boxes, the top bar with its
// tabs, the hint bar and the scope. Colours and sizes are the ones in
// docs/screens/screens.html.

#include <SDL.h>
#include <string>

#include "apotheca/engine.hpp"
#include "session.hpp"
#include "text.hpp"

namespace apotheca {

// The size of the lettering is a setting (SET page, TEXT SIZE; owner, 2026-11-12): from where
// it started to a little bigger than the screens were laid out for. The numbers are kept in
// "settings.txt": appended, never reordered.
enum TextSize { kTextSmall, kTextMedium, kTextLarge, kTextXl, kTextSizes };
constexpr const char* const kTextSizeNames[kTextSizes] = {"SMALL", "MEDIUM", "LARGE", "XL"};
// The three fonts at each size: small, medium, large. LARGE is what every screen was laid out at.
// MEDIUM is halfway between SMALL and LARGE (owner, 2026-11-14: it was {13.5, 16, 18.5}, and SMALL to MEDIUM
// looked a much bigger step than the two after it); each step up is now about the same.
constexpr float kTextPoints[kTextSizes][3] = {{12.f, 14.f, 16.f}, {13.25f, 15.5f, 18.f}, {14.5f, 17.f, 20.f}, {15.5f, 18.5f, 21.5f}};

// The program holds TWO sets of these: the chosen one (the setting), and the fixed one, which
// is always LARGE. The rows that were laid out to the pixel are drawn with the fixed one
// whatever the setting (`fixed()`): the top bar, the hints bar and the chips under a strip,
// and the bottom strips of the two grids. Everything else is drawn with the set it is handed.
struct Fonts {
    Font small;  // labels, hints, tabs
    Font medium; // names
    Font large;  // values
    const Fonts* fixedSet = nullptr; // LARGE's; none: this set is it (the tests' canvas)
    const Fonts& fixed() const { return fixedSet ? *fixedSet : *this; }
    // The largest of them that a text fits a width in: for a name that may be long.
    const Font& fit(const char* text, float room) const { return large.width(text) <= room ? large : medium.width(text) <= room ? medium : small; }
    // For a text whose room was measured at LARGE: one of the three (`&Fonts::small`...) at the
    // chosen size if the text fits `room` in it, and otherwise at LARGE (where XL does not fit,
    // that text is held at LARGE; it is never cut short). Never bigger than it has to be: at
    // LARGE and under this is always the chosen one.
    const Font& within(Font Fonts::*which, const char* text, float room) const {
        const Font& chosen = this->*which;
        const Font& held = fixed().*which;
        return chosen.width(text) <= room || chosen.width(text) <= held.width(text) ? chosen : held;
    }
    void unload() { small.unload(), medium.unload(), large.unload(); } // before their renderer goes
    // All three at a size. False if the font data cannot be read.
    bool load(SDL_Renderer* renderer, const std::vector<unsigned char>& ttf, int size) {
        if (size < 0 || size >= kTextSizes) size = kTextLarge;
        return small.load(renderer, ttf, kTextPoints[size][0]) && medium.load(renderer, ttf, kTextPoints[size][1]) &&
               large.load(renderer, ttf, kTextPoints[size][2]);
    }
};

// ---- the colours: a THEME (SET page; owner, 2026-11-13) ---------------------------------------
// Each colour has a JOB, and every theme keeps the jobs; only the colours change, never a size,
// a place or a shape. The surfaces, deepest first: bg (the screen, the black keys, the lettering
// on a chip that is filled with an accent or with text), panel, panel2 (a box that has been turned
// or stands out), line (edges, the empty part of a bar, dividers). The two levels of lettering:
// text and dim. The four accents: teal (the cursor, and "on"), purple (automation, glide, chords,
// where the song is playing), brass (a value changed from its usual, a held cursor, meters), stop
// (stop, WRITE, delete). A light theme keeps that order: its bg is the deepest of its surfaces and
// its line darker than them, so "fade to the background" pushes a thing back on either kind.
struct Theme {
    const char* name; // as the SET page shows it: one plain word
    SDL_Color bg, panel, panel2, line, text, dim, teal, purple, brass, stop;
};
// The numbers are kept in "settings.txt" (`theme N`): appended, never reordered. The dark ones
// first, and the first of all is the look as it was built (a file with no such line gets it).
enum ThemeId { kThemeBrown, kThemeBlue, kThemeGreen, kThemeBlack, kThemePaper, kThemeGrey, kThemes };
constexpr Theme kThemeTable[kThemes] = {
    {"BROWN", {20, 17, 14, 255}, {31, 26, 22, 255}, {42, 35, 29, 255}, {67, 57, 47, 255}, {224, 213, 191, 255}, {143, 131, 112, 255},
     {111, 191, 163, 255}, {154, 118, 176, 255}, {201, 161, 90, 255}, {196, 84, 70, 255}},
    {"BLUE", {13, 17, 25, 255}, {20, 26, 37, 255}, {29, 37, 52, 255}, {64, 79, 105, 255}, {222, 230, 242, 255}, {146, 160, 182, 255},
     {96, 204, 226, 255}, {186, 138, 236, 255}, {232, 184, 92, 255}, {238, 104, 96, 255}},
    {"GREEN", {11, 19, 15, 255}, {17, 29, 23, 255}, {25, 40, 32, 255}, {57, 84, 69, 255}, {224, 234, 216, 255}, {144, 166, 148, 255},
     {188, 226, 112, 255}, {214, 150, 226, 255}, {240, 156, 60, 255}, {246, 98, 118, 255}},
    {"BLACK", {6, 6, 6, 255}, {18, 18, 18, 255}, {30, 30, 30, 255}, {76, 76, 76, 255}, {242, 242, 242, 255}, {168, 168, 168, 255},
     {70, 226, 184, 255}, {236, 120, 255, 255}, {255, 196, 64, 255}, {255, 96, 86, 255}},
    {"PAPER", {224, 215, 196, 255}, {237, 231, 217, 255}, {248, 244, 235, 255}, {170, 156, 132, 255}, {22, 17, 12, 255}, {70, 60, 46, 255},
     {0, 106, 92, 255}, {92, 36, 124, 255}, {128, 84, 0, 255}, {134, 18, 44, 255}},
    {"GREY", {211, 217, 224, 255}, {228, 232, 238, 255}, {243, 245, 248, 255}, {152, 161, 173, 255}, {10, 13, 18, 255}, {52, 59, 70, 255},
     {0, 92, 156, 255}, {98, 16, 86, 255}, {134, 82, 0, 255}, {144, 20, 36, 255}},
};

namespace ui {

// The theme every screen draws in. The ten names below are this theme's colours, so a screen
// writes `kTeal` and gets the cursor's colour of whichever theme is chosen. It is set between two
// frames (App::draw, and main.cpp before the controls setup draws), from Session::theme().
// ANY NEW COLOUR goes through these ten (or faded() of one): never a number of its own.
inline Theme gTheme = kThemeTable[kThemeBrown];
inline void useTheme(int id) { gTheme = kThemeTable[id >= 0 && id < kThemes ? id : kThemeBrown]; }
// A light theme: its lettering is darker than its background. For the few things drawn as an object
// is (the roll's keyboard: white keys light, black keys dark), not as a surface with a job.
inline bool lightTheme() { return gTheme.bg.r + gTheme.bg.g + gTheme.bg.b > gTheme.text.r + gTheme.text.g + gTheme.text.b; }

inline const SDL_Color& kBg = gTheme.bg;
inline const SDL_Color& kPanel = gTheme.panel;
inline const SDL_Color& kPanel2 = gTheme.panel2;
inline const SDL_Color& kLine = gTheme.line;
inline const SDL_Color& kText = gTheme.text;
inline const SDL_Color& kDim = gTheme.dim;
inline const SDL_Color& kTeal = gTheme.teal;
inline const SDL_Color& kPurple = gTheme.purple;
inline const SDL_Color& kBrass = gTheme.brass;
inline const SDL_Color& kStop = gTheme.stop; // the stop square in the top bar: red, so it reads as "stopped"

constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kTopBarH = 26, kHintsH = 24;

// How fast the stick moves and turns things, on every screen: 1 is as first
// built; lower is slower. The owner asked for a quarter off (2026-11).
// It is a variable so it can become a setting, and so the tests can measure
// the screens at their own speeds.
inline float gStickSpeed = 0.75f;

// What the end of the hint bar says about the help card: "MENU: HELP", or
// "HLD SEL: HELP" on a handheld whose MENU button the firmware keeps.
inline const char* gHelpHint = "MENU: HELP";
// An update has been accepted and waits for the next start (owner,
// 2026-11-14): the end of the bar says kUpdateReadyHint in brass instead of
// the help, and it is always drawn, the hints giving way to it, until the
// program is started again. MENU still opens the help card.
inline bool gUpdateReady = false;
inline constexpr const char* kUpdateReadyHint = "RESTART TO UPDATE";

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

// How a held D-pad runs, the same on every screen. Two speeds: a cursor (grids, and a
// cursor among boxes) runs fast; a list, or a choice being stepped through, runs slowly
// enough to see or hear each one go by.
constexpr Uint32 kCursorDelayMs = 170, kCursorEveryMs = 30;
constexpr Uint32 kListDelayMs = 200, kListEveryMs = 110;

// A button's name as it is written in a cream box. The four directions are
// drawn as arrows, not spelled out ("UP / DOWN" is two small arrows): the same
// thing in a third of the room. Everything else is its letters.
// Two words are drawn shorter than they are written: HOLD as HLD, SELECT as
// SEL. Hints are written in full.
float keyWidth(const Fonts& fonts, const char* key);
// The words drawKey draws, with a space between (a direction by its name).
std::string keyShown(const char* key);
void drawKey(SDL_Renderer* r, const Fonts& fonts, float x, float textY, const char* key, SDL_Color color);

// The bar across the bottom: button names in cream boxes, each with what it does.
struct Hint { const char* key; const char* does; };
// What is true everywhere (X hears what is under the cursor; the stick or
// D-pad moves) is not listed each time: it is on the help card, which comes
// up while SELECT is held (App). A direction that either the stick or the
// D-pad gives is written as the direction alone. A hint that does not fit
// is left out; the end of the bar says how to see the help, if there is room.
void hints(SDL_Renderer* r, const Fonts& fonts, const Hint* list, int count);
// For the tests: how many pixels the last bar hints() drew was short of holding all of
// its hints and the help at the end. 0: nothing was left out.
inline float gHintsOver = 0.f;
// Where the bar's first chip starts.
constexpr float kHintsLeft = 8.f;
// The bar while a strip of fields is being set (X held on a hit, a note or a chord): each
// button's chip stands DIRECTLY UNDER THE FIELD IT CHANGES, its left edge at the field's
// (`x`: the same constant the strip draws the field at), with no word: the field's label
// above it says what it is. The chips are the bar's own. `dice` (a key, or null) is the
// button that rolls all of it: it stands by itself at the left end with a die. Written left
// to right; a chip that would come within kHintUnderGap of the one before, or pass the
// right-hand end, is left out. How to see the help is said at the end only if there is room
// after the last chip: where a field is that far right, it gives way.
struct HintUnder { float x; const char* key; };
constexpr float kHintUnderGap = 6.f;
// `diceAtEnd`: the dice stand at the right-hand end instead (the chord strip, where the left end
// is the field LEFT / RIGHT sets); the help gives way to them.
float hintDiceEndX(const Fonts& fonts, const char* dice); // where the dice's chip starts, at the end
void hintsUnder(SDL_Renderer* r, const Fonts& fonts, const char* dice, const HintUnder* list, int count, bool diceAtEnd = false);
// For a few seconds at the start, in the hints bar's place: the way back into the controls
// setup, with a line that fills (`part`, 0..1) as a button is held. A tight row: always at LARGE.

// A box taller than kBoxUsualH (what the boxes were laid out at) keeps its name at the top and its
// bar at the bottom, and its value goes down by half of the extra height, so it is not left at the
// top of an empty box. At kBoxUsualH or under, nothing moves.
constexpr int kBoxUsualH = 70;
constexpr float boxValueDrop(int h) { return h > kBoxUsualH ? static_cast<float>((h - kBoxUsualH) / 2) : 0.f; }

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

// LIVE OUTPUT: the track's latest output as a line, in a thin strip across the whole width of a
// DEVICE page, just above the hints bar (owner, 2026-11-14: the half-width box in the bottom right
// was too big and left room unused). Its label is inside it at the left; the line runs in the rest.
// Every DEVICE page that shows it (the synths' MAIN and settings pages, the sampler, the kit) ends
// its controls at kAboveScope and spreads them down to it. The places are here so the tests can read
// the drawn screen.
constexpr int kScopeH = 36;
constexpr int kScopeX = 8, kScopeW = kScreenW - 2 * kScopeX;
constexpr int kScopeY = kScreenH - kHintsH - 4 - kScopeH; // 416: 4 pixels clear of the hints bar
constexpr int kScopeLabelW = 120;                         // the label's room, inside the strip at its left
constexpr int kAboveScope = kScopeY - 10;                 // 406: where the controls above it end
void scopeStrip(SDL_Renderer* r, const Fonts& fonts, const Engine& engine);
// Where the line is drawn for a sample (already turned up and held to -1..1): the strip's middle,
// and full scale 4 pixels inside its top and bottom edges, never on them.
constexpr int kScopeMiddle = kScopeY + kScopeH / 2;
constexpr float kScopeHalf = (kScopeH - 8) / 2.f;
constexpr float kScopeBend = 0.4f; // the power the line's height is bent by, so a quiet sound shows
float scopeBent(float sample);     // -1..1 in, -1..1 out: the same sign, 0 and the ends where they were
int scopeLineY(float sample);

// The name a track goes by on screen: "2 PHAZED".
void trackName(const Project& project, int track, char* out, int size);

// A MIDI note number as a name: 60 is "C4".
void noteName(int pitch, char* out, int size);

} // namespace ui
} // namespace apotheca
