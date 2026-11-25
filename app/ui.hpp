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
struct Icon; // icons.hpp

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
// The numbers are kept in "settings.txt" (`theme N`). The dark ones first, and the first of all is the
// look as it was built (a file with no such line gets it). The rest take their colours from the editor
// themes they are named after, each moved in lightness (its hues kept) until it meets theme_test's
// rules (owner, 2026-11-23: "more like common terminal or IDE themes", six dark and two light; they
// replaced BLUE, GREEN, BLACK, PAPER and GREY, whose numbers they took: nothing is released).
enum ThemeId { kThemeBrown, kThemeDracula, kThemeNord, kThemeGruvbox, kThemeTokyo, kThemeMonokai, kThemeSolarized, kThemeGithub, kThemes };
constexpr int kDarkThemes = 6; // the first six: dark; the rest light
constexpr Theme kThemeTable[kThemes] = {
    {"BROWN", {20, 17, 14, 255}, {31, 26, 22, 255}, {42, 35, 29, 255}, {67, 57, 47, 255}, {224, 213, 191, 255}, {143, 131, 112, 255},
     {111, 191, 163, 255}, {154, 118, 176, 255}, {201, 161, 90, 255}, {196, 84, 70, 255}},
    {"DRACULA", {30, 31, 41, 255}, {40, 42, 54, 255}, {52, 55, 70, 255}, {98, 114, 164, 255}, {248, 248, 242, 255}, {167, 176, 212, 255},
     {139, 233, 253, 255}, {189, 147, 249, 255}, {255, 184, 108, 255}, {253, 122, 120, 255}},
    {"NORD", {31, 35, 43, 255}, {46, 52, 64, 255}, {59, 66, 82, 255}, {102, 114, 140, 255}, {236, 239, 244, 255}, {174, 184, 201, 255},
     {204, 232, 180, 255}, {202, 163, 195, 255}, {226, 195, 131, 255}, {247, 147, 155, 255}},
    {"GRUVBOX", {29, 32, 33, 255}, {40, 40, 40, 255}, {60, 56, 54, 255}, {124, 111, 100, 255}, {235, 219, 178, 255}, {181, 166, 139, 255},
     {161, 212, 142, 255}, {217, 139, 160, 255}, {250, 189, 47, 255}, {247, 129, 103, 255}},
    {"TOKYO", {22, 22, 30, 255}, {26, 27, 38, 255}, {36, 40, 59, 255}, {86, 95, 137, 255}, {192, 202, 245, 255}, {135, 147, 187, 255},
     {127, 229, 213, 255}, {194, 164, 248, 255}, {224, 175, 104, 255}, {247, 118, 142, 255}},
    {"MONOKAI", {26, 27, 23, 255}, {39, 40, 34, 255}, {62, 61, 50, 255}, {117, 113, 94, 255}, {248, 248, 242, 255}, {176, 173, 150, 255},
     {175, 235, 56, 255}, {79, 197, 219, 255}, {230, 219, 116, 255}, {249, 132, 157, 255}},
    {"SOLARIZED", {238, 232, 213, 255}, {245, 239, 220, 255}, {253, 246, 227, 255}, {147, 161, 161, 255}, {7, 54, 66, 255}, {82, 102, 108, 255},
     {7, 75, 117, 255}, {93, 81, 171, 255}, {122, 90, 0, 255}, {126, 11, 13, 255}},
    {"GITHUB", {230, 234, 239, 255}, {243, 245, 248, 255}, {255, 255, 255, 255}, {140, 149, 159, 255}, {31, 35, 40, 255}, {91, 99, 109, 255},
     {14, 71, 154, 255}, {121, 72, 214, 255}, {109, 67, 1, 255}, {196, 49, 55, 255}},
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
// built; lower is slower. The owner asked for a quarter off (2026-11): the
// default of the SET page's STICK SPEED (Session::setStickSpeed). A variable,
// so the tests can measure the screens at their own speeds. (A full push held
// on a cursor climbs past it: StickClimb, below.)
constexpr float kStickSpeedAsBuilt = 0.75f; // STICK SPEED's usual (B on its SET row: G17)
inline float gStickSpeed = kStickSpeedAsBuilt;

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
constexpr float kTabPad = 5.f; // either side of a tab's word in the top bar
struct TopBar { float tempoNumberRight, tempoRight, wordsX = 0.f, wordsRoom = 0.f, wordsWidth = 0.f; }; // wordsX / wordsRoom: where the screen's own words start, and the room they had; wordsWidth: how wide they drew
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
    float to = 1.f;         // where the end is, 0..1 (the sampler's END); start() puts it at 1
    void start(Uint32 nowMs, float wholeSeconds, float fromPart = 0.f, float speed = 1.f, float forSeconds = 0.f) {
        since = nowMs ? nowMs : 1;
        seconds = wholeSeconds;
        from = fromPart;
        rate = speed;
        lasts = forSeconds;
        to = 1.f;
    }
    // How long it sounds from where it began to its end, in seconds, played as it is (0: not known).
    float wholeLength() const { return seconds > 0.f && rate > 0.f && to > from ? (to - from) * seconds / rate : 0.f; }
    // Where it is, 0..1 of the recording; below zero when nothing is being heard.
    float at(Uint32 nowMs) const {
        if (since == 0 || seconds <= 0.f || nowMs < since) return -1.f;
        const float gone = static_cast<float>(nowMs - since) / 1000.f;
        const float where = from + gone * rate / seconds;
        return where > (to < 1.f ? to : 1.f) || (lasts > 0.f && gone > lasts) ? -1.f : where;
    }
};
// Draws it over a picture `w` wide that starts at `x`.
void drawPlayhead(SDL_Renderer* r, const Playhead& head, Uint32 nowMs, int x, int y, int w, int h);

// A recording's picture with the part played marked (the sampler's START and END, a drum lane's
// EDIT): every point of `peaks` a column, lit from `start` to `end` (0..1 of the whole) and drawn in
// the line's colour outside it, with a purple mark at each. `lit`, if given, is the height to draw a
// lit point at in place of its peak (a lane's GAIN and fades), 0..1 like the peaks.
void wavePicture(SDL_Renderer* r, const std::vector<float>& peaks, int x, int y, int w, int h, float start, float end, const std::vector<float>* lit = nullptr);
// END's mark, inside a picture `w` wide.
inline int waveEndMarkX(float end, int w) { const int x = static_cast<int>(end * w) - 2; return x < 0 ? 0 : (x > w - 2 ? w - 2 : x); }

// How long B is held to delete, one length a kind (gesture audit G29, 2026-11-25; each screen had its own
// 1000 ms). Where undo (SEL + B) brings it back: an effect, a MOD, EDIT's or VOICE's every box back, a scene
// emptied. Where nothing can: a recording, a folder, a song, a preset or a kit of the player's own, a network
// forgotten, a bounce stopped. A TRACK keeps its own (song_screen.hpp: two seconds, red only after a quiet half
// second, so the B tap that empties a cell never flashes). The bars keep their own looks.
constexpr Uint32 kHoldUndoneMs = 500;   // undo brings it back
constexpr Uint32 kHoldForGoodMs = 1000; // it cannot be undone

// How a held D-pad runs, the same on every screen. Two speeds: a cursor (grids, and a
// cursor among boxes) runs fast; a list, or a choice being stepped through, runs slowly
// enough to see or hear each one go by. (A third, for clicks, and the one exception below.)
constexpr Uint32 kCursorDelayMs = 170, kCursorEveryMs = 30;
constexpr Uint32 kListDelayMs = 200, kListEveryMs = 110;
// A third speed, for CLICKS (gesture audit G32, 2026-11-25): what X held sets on a hit or a note (a hit's
// pitch and ratchet, a lane's FEEL, a note's ratchet and glide) clicks from one to the next, slower than a
// cursor runs so each click is heard, with the D-pad held and with the stick held fully over
// (kStickClicksPerSecond, times STICK SPEED as the stick always is). It was three rates written in the screens
// (8 and 4 a second on the drum grid, 220 ms on the roll); the fastest of them is the one kept, so none
// clicks slower than it did.
constexpr Uint32 kClickDelayMs = 170, kClickEveryMs = 125;
constexpr float kStickClicksPerSecond = 1000.f / static_cast<float>(kClickEveryMs);
// The one exception, named here (batch 3, 2026-11-15): the list of recordings (sample_picker.hpp) can be
// hundreds of rows long, so a held D-pad there goes on from the list's pace to kRecordingsFasterEveryMs
// after kRecordingsFasterAfterMs, and to kRecordingsFastestEveryMs after kRecordingsFastestAfterMs.
constexpr Uint32 kRecordingsFasterAfterMs = 1500, kRecordingsFasterEveryMs = 60;
constexpr Uint32 kRecordingsFastestAfterMs = 3500, kRecordingsFastestEveryMs = 20;

// Which of the speeds a held key runs at. Each screen names it: a cursor (grids, boxes, a value turned),
// a list or a choice stepped through, or a click.
enum class Pace { Cursor, List, Click };
constexpr Uint32 paceDelayMs(Pace pace) { return pace == Pace::Cursor ? kCursorDelayMs : pace == Pace::List ? kListDelayMs : kClickDelayMs; }
constexpr Uint32 paceEveryMs(Pace pace) { return pace == Pace::Cursor ? kCursorEveryMs : pace == Pace::List ? kListEveryMs : kClickEveryMs; }

// A held key's repeats, on every screen (gesture audit G08, 2026-11-25). Each is counted from when the
// one before was DUE, not from the frame that saw it: a 30 ms pace is 33 a second whatever the frames
// (counted from the frame it was rounded up to two frames' time, about 28 to 31 a second), a pace faster than the
// frames gives several in one frame, and the run stops at the let go (it is asked only while the key is
// held). A frame held up more than kRepeatStallMs makes no burst to catch up: the count starts again
// from that frame. As the list of recordings always counted.
//   the key pressed:  repeat.restart()   (its first wait starts at the next due(), on the screen's clock)
//   each frame:       while (key != Button::None && repeat.due(nowMs, input.held(key), pace)) move(key);
constexpr Uint32 kRepeatStallMs = 100;
struct Repeat {
    Uint32 at = 0; // when the next one is due; 0: a key has just gone down
    void restart() { at = 0; }
    // True once for each repeat due by `nowMs` while `held`; the first call after restart() starts the
    // wait (`delayMs`) and is false. `everyMs` is the gap after this one (above 0).
    bool due(Uint32 nowMs, bool held, Uint32 delayMs, Uint32 everyMs) {
        if (at == 0) {
            at = nowMs + delayMs;
            if (at == 0) at = 1;
            return false;
        }
        if (!held || nowMs < at) return false;
        if (nowMs - at > kRepeatStallMs) at = nowMs;
        at += everyMs > 0 ? everyMs : 1;
        return true;
    }
    bool due(Uint32 nowMs, bool held, Pace pace) { return due(nowMs, held, paceDelayMs(pace), paceEveryMs(pace)); }
    // Waits as `waits` does, then runs as `runs`: the lists that always waited the cursor's 170 before
    // running at the list's pace keep their wait (nothing held runs slower than it did).
    bool due(Uint32 nowMs, bool held, Pace waits, Pace runs) { return due(nowMs, held, paceDelayMs(waits), paceEveryMs(runs)); }
};

// A long list's ends (gesture audit G35, 2026-11-25: SET's rows, the list of songs and its revs): a
// PRESS of the D-pad at an end goes round to the other end; a held D-pad running on, or the stick,
// stops there, so a run never carries past the end it was going to. `at` 0..count-1, `by` 1 or -1.
constexpr int listStep(int at, int by, int count, bool pressed) {
    return count <= 0 ? 0 : at + by < 0 ? (pressed ? count - 1 : 0) : at + by >= count ? (pressed ? 0 : count - 1) : at + by;
}

// The stick on a CURSOR (gesture audit G07, 2026-11-25): a push short of kStickFullPush moves at the
// screen's own rate, as always; a FULL push held climbs from that rate to kStickClimbTo times it over
// kStickClimbSeconds and stays there while it is held full. STICK SPEED scales the whole of it, as it
// scales everything the stick does. Only a cursor climbs: a value turned, a list or a choice stepped
// through does not (the kit's sounds with A held stay slow enough to hear each one).
// A climb not asked about for kStickClimbGapMs (the screen was away, or the stick was doing something
// else) starts again from the bottom.
constexpr float kStickFullPush = 0.9f, kStickClimbTo = 3.f;
constexpr Uint32 kStickClimbMs = 1000, kStickClimbGapMs = 100;
struct StickClimb {
    Uint32 fullSince = 0; // when the push became full (0: it is not)
    Uint32 lastAt = 0;    // when it was last asked
    // How many times the screen's own rate, this frame: 1 below a full push, up to kStickClimbTo.
    float times(float push, Uint32 nowMs) {
        const bool again = lastAt == 0 || nowMs < lastAt || nowMs - lastAt > kStickClimbGapMs;
        lastAt = nowMs ? nowMs : 1;
        if (push < kStickFullPush && push > -kStickFullPush) {
            fullSince = 0;
            return 1.f;
        }
        if (fullSince == 0 || again) fullSince = lastAt;
        const Uint32 held = nowMs > fullSince ? nowMs - fullSince : 0;
        return 1.f + (kStickClimbTo - 1.f) * static_cast<float>(held < kStickClimbMs ? held : kStickClimbMs) / static_cast<float>(kStickClimbMs);
    }
};

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

// A box taller than kBoxUsualH (what the boxes were laid out at) keeps its name at the top and its
// bar at the bottom, and its value goes down by half of the extra height, so it is not left at the
// top of an empty box. At kBoxUsualH or under, nothing moves.
constexpr int kBoxUsualH = 70;
constexpr float boxValueDrop(int h) { return h > kBoxUsualH ? static_cast<float>((h - kBoxUsualH) / 2) : 0.f; }

// A named value in a box. `part` 0..1 draws a bar; below 0 draws none.
// `centre` draws the bar outward from the middle (for values that go both ways).
void valueBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, bool centre = false, const apotheca::Icon* icon = nullptr);

// A macro: its name, a small picture of what it does in the top right, its
// value, and a bar that grows from the middle. Dim until it is turned away
// from the middle, so the ones in use stand out.
// A value in a display window (DEVICE MAIN's FOLDER and PRESET, design A): valueBox's words in the same places, on
// the background with faint scan lines, as a synth's patch screen.
void displayBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value, bool cursor, bool held);
void macroBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, float mark = std::nanf(""));
// A macro is a DIAL turned from the middle (owner, 2026-11-24, design A): lit from the top (the preset as it was
// made) to its value either way, a dim detent over the top, its name and picture at the top, its value under the
// dial; a MOD's brass tick across the ring (`mark`). A box shorter than kMacroDialTallH (a picture above the
// macros: CLOUD, MORPH) puts a smaller dial at its left and the words beside it. Where the dial is:
constexpr int kMacroDialTallH = 100;
struct MacroDial {
    int cx, cy, radius, ring;
};
inline MacroDial macroDialIn(int x, int y, int w, int h) {
    if (h >= kMacroDialTallH) return {x + w / 2, y + 26 + (h - 26 - 30) / 2, 24, 6};
    return {x + 30, y + h / 2 + 1, 19, 5};
}
constexpr int kMacroBarInset = 7, kMacroBarFromFoot = 11, kMacroBarH = 5;

// How far through a scene the song is: one block per time the scene plays.
// `play` is the pass it is on (from 0) and `step` the step within it.
void sceneProgress(SDL_Renderer* r, int x, int y, int w, int h, int plays, int play, int step, int steps = kSteps); // `steps` in one pass

// SIGNAL: the track's latest output as a line, in a thin strip across the whole width of a
// DEVICE page, just above the hints bar (owner, 2026-11-14: the half-width box in the bottom right
// was too big and left room unused). Its label is inside it at the left; the line runs in the rest.
// Every DEVICE page that shows it (the synths' MAIN and settings pages, the sampler, the kit) ends
// its controls at kAboveScope and spreads them down to it. The places are here so the tests can read
// the drawn screen.
constexpr int kScopeH = 36;
constexpr int kScopeX = 8, kScopeW = kScreenW - 2 * kScopeX;
constexpr int kScopeSamples = 512; // the newest samples the line is drawn from (redraw.hpp reads the same)
constexpr int kScopeY = kScreenH - kHintsH - 4 - kScopeH; // 416: 4 pixels clear of the hints bar
constexpr int kScopeLabelW = 120;                         // the label's room, inside the strip at its left
constexpr int kAboveScope = kScopeY - 10;                 // 406: where the controls above it end
void scopeStrip(SDL_Renderer* r, const Fonts& fonts, const Engine& engine);
// SIGNAL as a large scope (DEVICE MAIN, owner 2026-11-24, design A): a dark screen with a graticule, the line
// as the strip's (bent, the same samples) through its middle, full scale kScopeBoxInset inside its edges, its
// label at the top left.
constexpr int kScopeBoxInset = 10;
void scopeBox(SDL_Renderer* r, const Fonts& fonts, const Engine& engine, int x, int y, int w, int h);
int scopeBoxLineY(float sample, int y, int h);
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
