// THEME: the colours are a setting (SET page; owner, 2026-11-13). Six themes, four dark and two
// light; the first is the look as it was built. A theme changes the colours only.
//
//   theme_test                    the six palettes (each colour's job kept, contrast measured),
//                                 the setting (kept, read back, an old file reads BROWN), the
//                                 row on the SET page and its page moving, in use from the next
//                                 frame, and every screen the rig can reach drawn in each theme
//   theme_test views APOTHECA     every still the program knows (tests/stills.hpp), drawn by the
//                                 program in all six themes (--themes): no colour on the screen
//                                 that is not the theme's own, or one of them laid over another
//   ... views APOTHECA NAME       just the stills whose name has NAME in it, each fault said
//
// What "the theme's own" means, by pixels (`Explain`): a part of the screen filled flat (a pixel
// whose eight neighbours are all its colour) is one of the ten, or one of them laid at an alpha
// over another (a wash, a fade), or that laid over a third; and any other pixel (the edge of a
// letter, a line) lies between two of those. The program has no picture with colours of its
// own, so nothing is excused.

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stills.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

// ---- measuring a colour ----------------------------------------------------------------------
double linear(int c) {
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
double luminance(SDL_Color c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }
// The WCAG contrast ratio: 1 (the same) to 21 (black on white).
double contrast(SDL_Color a, SDL_Color b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
// How far apart two colours look (CIE76 delta E: about 2.3 is "just seen").
void lab(SDL_Color c, double out[3]) {
    const double r = linear(c.r), g = linear(c.g), b = linear(c.b);
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, y = 0.2126 * r + 0.7152 * g + 0.0722 * b,
                 z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    out[0] = 116.0 * f(y) - 16.0, out[1] = 500.0 * (f(x) - f(y)), out[2] = 200.0 * (f(y) - f(z));
}
double apart(SDL_Color a, SDL_Color b) {
    double p[3], q[3];
    lab(a, p), lab(b, q);
    return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}
// A colour as a reader without red or without green sees it (Vienot, Brettel and Mollon 1999, in
// linear light): `protan` without red, else without green. The usual two of colour blindness.
SDL_Color blind(SDL_Color c, bool protan) {
    const double r = linear(c.r), g = linear(c.g), b = linear(c.b);
    const double rg = protan ? 0.11238 * r + 0.88762 * g : 0.29275 * r + 0.70725 * g;
    const double bb = protan ? 0.00401 * r - 0.00401 * g + b : -0.02234 * r + 0.02234 * g + b;
    const auto back = [](double v) {
        v = v < 0 ? 0 : v;
        const double e = 255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055);
        return static_cast<Uint8>(e > 255 ? 255 : (e < 0 ? 0 : e + 0.5));
    };
    return SDL_Color{back(rg), back(rg), back(bb), 255};
}
bool same(SDL_Color a, SDL_Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// The ten of a theme, in the order of the struct.
std::vector<SDL_Color> tenOf(const Theme& t) { return {t.bg, t.panel, t.panel2, t.line, t.text, t.dim, t.teal, t.purple, t.brass, t.stop}; }

// ---- the palettes ------------------------------------------------------------------------------
// The lowest of each kind of contrast in a theme: lettering on the three surfaces it is written
// on, an accent on them, the background's colour written on an accent (a chip: the tab, L2 / R2,
// WRITE), the faintest lettering (line: EMPTY, BUILT IN) on panel2, and how far apart the six
// lettering and accent colours are.
// And two that are not about a surface: how far the cursor's colour is in lightness from
// automation's and from the dim lettering (`cursorLight`: on a light theme the three are all dark,
// and the same lightness makes the cursor's outline and the purple line beside it one thick
// border), and how far apart the four accents stay for a colour-blind reader (`blind`).
struct Measured { double text, dim, accent, onAccent, line, lineDim, apart, cursorLight, blind; };
Measured measure(const Theme& t) {
    Measured m{99, 99, 99, 99, 99, 99, 999, 99, 999};
    const SDL_Color surfaces[3] = {t.bg, t.panel, t.panel2};
    const SDL_Color accents[4] = {t.teal, t.purple, t.brass, t.stop};
    for (const SDL_Color& s : surfaces) {
        m.text = std::min(m.text, contrast(t.text, s));
        m.dim = std::min(m.dim, contrast(t.dim, s));
        for (const SDL_Color& a : accents) m.accent = std::min(m.accent, contrast(a, s));
        m.line = std::min(m.line, contrast(t.line, s));
    }
    for (const SDL_Color& a : accents) m.onAccent = std::min(m.onAccent, contrast(t.bg, a));
    m.lineDim = contrast(t.dim, t.line); // a bar that is dim on a line's track
    const SDL_Color marks[6] = {t.teal, t.purple, t.brass, t.stop, t.text, t.dim};
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j) m.apart = std::min(m.apart, apart(marks[i], marks[j]));
    m.cursorLight = std::min(contrast(t.teal, t.purple), contrast(t.teal, t.dim));
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            for (bool protan : {true, false}) m.blind = std::min(m.blind, apart(blind(accents[i], protan), blind(accents[j], protan)));
    return m;
}

void testThePalettes(bool print) {
    CHECK(kThemes == 6 && Session::kThemeCount == kThemes && Session::kThemeAsBuilt == kThemeBrown && kThemeBrown == 0);
    // The numbers are kept in settings.txt: appended, never reordered.
    CHECK(kThemeBlue == 1 && kThemeGreen == 2 && kThemeBlack == 3 && kThemePaper == 4 && kThemeGrey == 5);
    // BROWN is the look as it was built, to the number (every pixel test of the program is held to it).
    const Theme& brown = kThemeTable[kThemeBrown];
    const SDL_Color built[10] = {{20, 17, 14, 255}, {31, 26, 22, 255}, {42, 35, 29, 255}, {67, 57, 47, 255}, {224, 213, 191, 255},
                                 {143, 131, 112, 255}, {111, 191, 163, 255}, {154, 118, 176, 255}, {201, 161, 90, 255}, {196, 84, 70, 255}};
    for (int i = 0; i < 10; ++i) CHECK(same(tenOf(brown)[static_cast<std::size_t>(i)], built[i]));
    // A fresh program draws in it.
    CHECK(same(ui::kBg, built[0]) && same(ui::kTeal, built[6]) && same(ui::kStop, built[9]));

    for (int id = 0; id < kThemes; ++id) {
        const Theme& t = kThemeTable[id];
        // A name: one plain word in upper case, and no two the same.
        bool plain = std::strlen(t.name) >= 3 && std::strlen(t.name) <= 6;
        for (const char* c = t.name; *c; ++c) plain = plain && *c >= 'A' && *c <= 'Z';
        CHECK(plain);
        for (int other = 0; other < id; ++other) CHECK(std::strcmp(t.name, kThemeTable[other].name) != 0);
        // Solid colours: nothing in a theme is see-through.
        for (const SDL_Color& c : tenOf(t)) CHECK(c.a == 255);
        // The dark ones first, then the light.
        const bool dark = id < 4;
        CHECK(dark ? luminance(t.bg) < 0.02 : luminance(t.bg) > 0.5);
        // The surfaces keep their order on either kind: bg the deepest, then panel, then panel2 (a box
        // that stands out). The line is lighter than the surfaces on a dark theme and darker on a
        // light one, so an edge shows on both; the lettering is the furthest from them.
        CHECK(luminance(t.bg) < luminance(t.panel) && luminance(t.panel) < luminance(t.panel2));
        CHECK(dark ? luminance(t.line) > luminance(t.panel2) : luminance(t.line) < luminance(t.bg));
        CHECK(dark ? luminance(t.text) > luminance(t.dim) : luminance(t.text) < luminance(t.dim));
        const Measured m = measure(t);
        if (print)
            std::printf("  %-6s text %5.2f  dim %5.2f  accent %5.2f  bg on accent %5.2f  line %4.2f  dim on line %4.2f  apart %5.1f  cursor %4.2f  blind %5.1f\n", t.name, m.text,
                        m.dim, m.accent, m.onAccent, m.line, m.lineDim, m.apart, m.cursorLight, m.blind);
        // Every theme, BROWN as built included (1.71 and 18.4): the cursor, automation and the dim
        // lettering differ in lightness, not in hue alone (BROWN's 1.71 shows as a lilac line inside
        // a mint outline; PAPER and GREY were 1.12 and 1.01 at first, one dark border); and the four
        // accents stay apart for a reader without red or without green (they were 2 and 10 there).
        CHECK(m.cursorLight >= 1.4);
        CHECK(m.blind >= 15.0);
        if (id == kThemeBrown) {
            // As built, and settled (the owner's look): measured, not held to the rules below. Its
            // dim is 4.16 on panel2 where the rule is 4.5; it stays, to the pixel.
            CHECK(m.text > 10.6 && m.dim > 4.15 && m.accent > 3.4 && m.onAccent > 4.2 && m.line > 1.3 && m.apart > 30);
            continue;
        }
        // Every new theme: the handheld is small and seen at arm's length.
        CHECK(m.text >= 7.0);     // lettering on every surface
        CHECK(m.dim >= 4.5);      // the quieter lettering
        CHECK(m.accent >= 4.5);   // the cursor, automation, a changed value, delete: on every surface...
        CHECK(m.onAccent >= 4.5); // ...and the background's colour written on each (a chip)
        CHECK(m.line >= 1.7);     // the faintest: an edge, and "nothing here"
        CHECK(m.lineDim >= 2.0);  // a quiet bar on its track
        CHECK(m.apart >= 20.0);   // the four accents and the two letterings, each told from the others
    }
    // The themes differ: no two share a background, a cursor's colour or an automation's.
    for (int a = 0; a < kThemes; ++a)
        for (int b = a + 1; b < kThemes; ++b) {
            CHECK(apart(kThemeTable[a].bg, kThemeTable[b].bg) > 2.0);
            CHECK(apart(kThemeTable[a].teal, kThemeTable[b].teal) > 8.0);
            CHECK(apart(kThemeTable[a].purple, kThemeTable[b].purple) > 8.0);
        }
    // useTheme: a number nobody knows is BROWN.
    ui::useTheme(kThemePaper);
    CHECK(same(ui::kBg, kThemeTable[kThemePaper].bg) && same(ui::kText, kThemeTable[kThemePaper].text));
    ui::useTheme(17);
    CHECK(same(ui::kBg, brown.bg));
    ui::useTheme(kThemeGrey);
    ui::useTheme(-1);
    CHECK(same(ui::kPurple, brown.purple));
}

// The measuring itself, on colours worked out by hand.
void testTheMeasures() {
    CHECK(std::fabs(contrast({0, 0, 0, 255}, {255, 255, 255, 255}) - 21.0) < 0.01);
    CHECK(std::fabs(contrast({119, 119, 119, 255}, {255, 255, 255, 255}) - 4.48) < 0.01); // the classic grey that just fails 4.5
    CHECK(contrast({50, 50, 50, 255}, {50, 50, 50, 255}) == 1.0);
    // Red and green: far apart, and close to a reader without either; a grey is a grey to both.
    CHECK(apart({200, 40, 40, 255}, {60, 140, 40, 255}) > 100.0 && apart(blind({200, 40, 40, 255}, false), blind({60, 140, 40, 255}, false)) < 10.0);
    CHECK(apart({200, 80, 40, 255}, {120, 120, 40, 255}) > 50.0 && apart(blind({200, 80, 40, 255}, true), blind({120, 120, 40, 255}, true)) < 14.0);
    CHECK(same(blind({90, 90, 90, 255}, true), SDL_Color{90, 90, 90, 255}) && same(blind({90, 90, 90, 255}, false), SDL_Color{90, 90, 90, 255}));
    CHECK(apart({10, 20, 30, 255}, {10, 20, 30, 255}) == 0.0 && apart({0, 0, 0, 255}, {255, 255, 255, 255}) > 99.0);
    // A theme that breaks a rule is found: dim too faint, two accents the same.
    Theme t = kThemeTable[kThemeBlue];
    t.dim = t.panel2;
    CHECK(measure(t).dim < 4.5);
    t = kThemeTable[kThemeBlue];
    t.brass = t.teal;
    CHECK(measure(t).apart < 1.0);
}

// ---- the setting ------------------------------------------------------------------------------
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const SDL_Keycode STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l;

Canvas& canvas() {
    static Canvas* c = new Canvas; // (kept to the end: SDL is never shut down here)
    return *c;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) key(code, true), frames(1), key(code, false), frames(1);
    }
    Session& session() { return app.session(); }
    // The screen as it is now, as App draws it, with what it drew written down.
    DrawLog log;
    std::vector<Uint32> draw() {
        Canvas& c = canvas();
        log.clear();
        gDrawLog = &log;
        app.draw(c.r, *c.fonts);
        gDrawLog = nullptr;
        SDL_RenderPresent(c.r);
        return c.all();
    }
    bool drewText(const char* words) const {
        for (const DrawLog::Text& t : log.texts)
            if (t.words == words) return true;
        return false;
    }
};

void testTheSettingIsKept() {
    char pattern[] = "/tmp/apotheca-theme-XXXXXX";
    const std::string folder = mkdtemp(pattern), file = folder + "/settings.txt";
    {
        Rig rig;
        Session& session = rig.session();
        CHECK(session.theme() == kThemeBrown); // as built, with nothing kept
        session.useProgramFolder(folder);
        session.loadSettings(); // (no file: as it was)
        CHECK(session.theme() == kThemeBrown);
        for (int t = 0; t < kThemes; ++t) {
            session.setTheme(t);
            CHECK(session.theme() == t);
            std::string kept;
            CHECK(readTextFile(file, kept) && kept.find("\ntheme " + std::to_string(t) + "\n") != std::string::npos);
        }
        session.setTheme(-3); // it has ends
        CHECK(session.theme() == 0);
        session.setTheme(99);
        CHECK(session.theme() == kThemes - 1);
    }
    for (int t = 0; t < kThemes; ++t) {
        // Read back at the next start.
        writeTextFile(file, "stick 75\noctaves 2\nmidirec 0\ntext 2\ntheme " + std::to_string(t) + "\n");
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().theme() == t);
        CHECK(rig.session().textSize() == 2); // (and the line before it as it was)
    }
    {
        // A file from before the setting: no such line. The look as it was built, and the file left alone.
        writeTextFile(file, "stick 80\noctaves 1\nmidirec 1\ntext 3\n");
        Rig rig;
        rig.session().setTheme(kThemeGreen);
        rig.session().useProgramFolder(folder);
        Rig fresh;
        fresh.session().useProgramFolder(folder);
        fresh.session().loadSettings();
        CHECK(fresh.session().theme() == kThemeBrown && fresh.session().textSize() == 3);
        std::string kept;
        CHECK(readTextFile(file, kept) && kept == "stick 80\noctaves 1\nmidirec 1\ntext 3\n");
    }
    {
        // A number nobody knows (a later program's theme), or rubbish: left as it was.
        writeTextFile(file, "theme 6\ntheme -1\ntheme x\n");
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().theme() == kThemeBrown);
    }
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

// ---- its row on the SET page ----------------------------------------------------------------
void testTheRow() {
    typedef SettingsScreen S;
    Rig rig;
    Session& session = rig.session();
    session.view = View::Settings;
    S& setup = rig.app.settings();
    // After TEXT SIZE, among the handheld's rows.
    CHECK(S::kRows == 13 && S::kTheme == S::kTextSize + 1 && S::kOctaves == S::kTheme + 1 && S::kRowsShown == 12);
    setup.goTo(S::kTextSize);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kTheme && session.theme() == kThemeBrown);
    // A choice: one click a push, and it has ends.
    for (int t = 1; t < kThemes; ++t) {
        rig.tap(RIGHT);
        CHECK(session.theme() == t);
    }
    rig.tap(RIGHT, 2);
    CHECK(session.theme() == kThemes - 1);
    rig.tap(LEFT);
    CHECK(session.theme() == kThemes - 2);
    rig.tap(LEFT, 9);
    CHECK(session.theme() == 0);
    // The stick sideways, once a push.
    rig.key(STICK_RIGHT, true), rig.frames(1), rig.key(STICK_RIGHT, false), rig.frames(1);
    CHECK(session.theme() == 1);
    rig.key(STICK_LEFT, true), rig.frames(1), rig.key(STICK_LEFT, false), rig.frames(1);
    CHECK(session.theme() == 0);
    // A, B, X and Y do nothing to it, and nothing else changes with it.
    for (SDL_Keycode other : {A, B, X, Y}) rig.tap(other);
    CHECK(session.theme() == 0 && setup.cursorRow() == S::kTheme && !setup.typing() && !setup.testing());
    const int size = session.textSize(), octaves = session.octavesShown();
    rig.tap(RIGHT, 3);
    CHECK(session.textSize() == size && session.octavesShown() == octaves);

    // In use from the next frame: the screen is drawn in it.
    for (int t = 0; t < kThemes; ++t) {
        session.setTheme(t);
        const std::vector<Uint32> screen = rig.draw();
        const Theme& theme = kThemeTable[t];
        CHECK(screen[static_cast<std::size_t>(ui::kScreenW * 30 + 4)] == Canvas::colour(theme.bg)); // under the top bar, left of the rows
        CHECK(screen[static_cast<std::size_t>(ui::kScreenW * 10 + 600)] == Canvas::colour(theme.panel)); // the top bar
        CHECK(rig.drewText(theme.name)); // and the row says which
        // The row's value is lit: the cursor's colour at the value's place.
        int lit = 0;
        for (int y = setup.rowNowY(S::kTheme); y < setup.rowNowY(S::kTheme) + S::kRowH - 6; ++y)
            for (int x = static_cast<int>(S::kValueX); x < static_cast<int>(S::kValueX) + 70; ++x) lit += canvas().is(x, y, theme.teal) ? 1 : 0;
        CHECK(lit > 20);
    }
    session.setTheme(kThemeBrown);

    // The page shows twelve rows and moves one at a time past either end.
    setup.goTo(S::kTempo);
    CHECK(setup.firstShown() == 0 && setup.shown(S::kTempo) && !setup.shown(S::kControls));
    rig.draw();
    CHECK(rig.drewText("TEMPO") && !rig.drewText("CONTROLS") && rig.drewText("THEME"));
    for (int row = 1; row < S::kRows; ++row) {
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == row && setup.shown(row));
        CHECK(setup.firstShown() == (row < S::kRowsShown ? 0 : row - S::kRowsShown + 1));
    }
    rig.draw();
    CHECK(rig.drewText("CONTROLS") && rig.drewText("SET UP AGAIN") && !rig.drewText("TEMPO") && rig.drewText("PROJECT") && rig.drewText("HANDHELD"));
    // Every row shown inside the page, none over another.
    for (int row = setup.firstShown(); row < setup.firstShown() + S::kRowsShown; ++row) {
        CHECK(setup.rowNowY(row) >= ui::kTopBarH + 8 && setup.rowNowY(row) + S::kRowH - 6 <= ui::kScreenH - ui::kHintsH - 4);
        if (row > setup.firstShown()) CHECK(setup.rowNowY(row) >= setup.rowNowY(row - 1) + S::kRowH - 2);
    }
    // Back up: it stays put until the cursor passes the top.
    rig.tap(UP);
    CHECK(setup.firstShown() == 1);
    setup.goTo(S::kPack);
    CHECK(setup.firstShown() == 1);
    rig.tap(UP);
    CHECK(setup.cursorRow() == S::kTempo && setup.firstShown() == 0);
    rig.tap(UP);
    CHECK(setup.cursorRow() == S::kTempo && setup.firstShown() == 0); // the list has ends
    // The arrow that says a row is out of sight: down after HANDHELD at the top, up after PROJECT at the bottom.
    const auto arrowAfter = [&](const char* section, int y) {
        int lit = 0;
        const float x0 = 16.f + canvas().fonts->small.width(section) + 6.f;
        for (int yy = y; yy < y + 12; ++yy)
            for (int x = static_cast<int>(x0); x < static_cast<int>(x0) + 12; ++x) lit += canvas().is(x, yy, ui::kDim) ? 1 : 0;
        return lit;
    };
    rig.draw();
    CHECK(arrowAfter("HANDHELD", S::rowY(S::kVolume) - 18) >= 9 && arrowAfter("PROJECT", 34) == 0);
    setup.goTo(S::kControls);
    rig.draw();
    CHECK(arrowAfter("PROJECT", 34) >= 9 && arrowAfter("HANDHELD", S::rowY(S::kVolume, 1) - 18) == 0);
}

// ---- what a pixel may be -------------------------------------------------------------------
// The colours a theme can put on the screen. `flat`: a theme colour, one laid over another at an
// alpha the program uses (a fade, a wash), or the background's wash laid over that, within
// kSlack of a channel. Any pixel: on the line between two of the ten or two flat colours on the
// screen (the edge of a letter, a line), within kSlack.
struct Explain {
    static constexpr int kSlack = 3; // SDL's blending rounds; a wash over a wash rounds twice
    std::vector<SDL_Color> ten;
    std::shared_ptr<std::vector<std::uint8_t>> flats; // one bit a colour: may a flat part be it?
    explicit Explain(const Theme& t) : ten(tenOf(t)) {
        // Made once for each theme (the views run asks for it from four threads at once).
        static std::mutex lock;
        static std::shared_ptr<std::vector<std::uint8_t>> made[kThemes];
        const int id = static_cast<int>(&t - kThemeTable);
        std::lock_guard<std::mutex> hold(lock);
        if (id >= 0 && id < kThemes && made[id]) {
            flats = made[id];
            return;
        }
        flats = std::make_shared<std::vector<std::uint8_t>>(std::size_t(1) << 21, 0);
        // Every alpha `faded()` is given in app/.
        static const int kAlphas[] = {60, 76, 90, 110, 120, 150, 170, 185, 190, 200, 215};
        std::vector<SDL_Color> once = ten;
        for (const SDL_Color& over : ten)
            for (const SDL_Color& under : ten)
                for (int a : kAlphas) once.push_back(mix(over, under, a));
        const auto mark = [&](SDL_Color c) {
            for (int r = c.r - kSlack; r <= c.r + kSlack; ++r)
                for (int g = c.g - kSlack; g <= c.g + kSlack; ++g)
                    for (int b = c.b - kSlack; b <= c.b + kSlack; ++b) {
                        if (r < 0 || g < 0 || b < 0 || r > 255 || g > 255 || b > 255) continue;
                        const Uint32 at = (Uint32(r) << 16) | (Uint32(g) << 8) | Uint32(b);
                        (*flats)[at >> 3] |= static_cast<std::uint8_t>(1u << (at & 7));
                    }
        };
        for (const SDL_Color& c : once) mark(c);
        // A wash over that is always the background's (a list or a card over a screen, the steps a
        // scene does not reach): faded(kBg, ...).
        static const int kWashes[] = {150, 170, 185, 190, 200, 215};
        for (std::size_t u = ten.size(); u < once.size(); ++u)
            for (int a : kWashes) mark(mix(t.bg, once[u], a));
        if (id >= 0 && id < kThemes) made[id] = flats;
    }
    static SDL_Color mix(SDL_Color over, SDL_Color under, int a) {
        const auto one = [&](int s, int d) { return static_cast<Uint8>((s * a + d * (255 - a) + 127) / 255); };
        return SDL_Color{one(over.r, under.r), one(over.g, under.g), one(over.b, under.b), 255};
    }
    // How far a colour is from the line between two others (in steps of a channel).
    static double fromLine(const double p[3], SDL_Color a, SDL_Color b) {
        const double d[3] = {double(b.r) - a.r, double(b.g) - a.g, double(b.b) - a.b};
        const double w[3] = {p[0] - a.r, p[1] - a.g, p[2] - a.b};
        const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        double t = dd > 0 ? (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd : 0.0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        double far = 0;
        for (int i = 0; i < 3; ++i) far = std::max(far, std::fabs(w[i] - t * d[i]));
        return far;
    }
    bool flat(Uint32 pixel) const {
        pixel &= 0xffffffu;
        return ((*flats)[pixel >> 3] >> (pixel & 7)) & 1u;
    }
    // Any pixel: between two of the ten, or between two flat colours that are on the screen; or that,
    // with one of the ten laid over it at an alpha the program uses (letters under a wash that
    // marks where the song is playing).
    bool edge(Uint32 pixel, const std::vector<SDL_Color>& onScreen) const {
        const double p[3] = {double((pixel >> 16) & 255), double((pixel >> 8) & 255), double(pixel & 255)};
        if (between(p, onScreen, kSlack)) return true;
        static const int kAlphas[] = {60, 76, 90, 110, 120, 150, 170, 185, 190, 200, 215};
        for (const SDL_Color& over : ten)
            for (int a : kAlphas) {
                // What was under it, worked back: under = (p - over * a) / (1 - a).
                const double keep = (255.0 - a) / 255.0, put = a / 255.0;
                const double under[3] = {(p[0] - over.r * put) / keep, (p[1] - over.g * put) / keep, (p[2] - over.b * put) / keep};
                if (under[0] < -kSlack || under[1] < -kSlack || under[2] < -kSlack || under[0] > 255 + kSlack || under[1] > 255 + kSlack || under[2] > 255 + kSlack) continue;
                if (between(under, onScreen, (kSlack + 1) / keep)) return true;
            }
        return false;
    }
    bool between(const double p[3], const std::vector<SDL_Color>& onScreen, double slack) const {
        for (std::size_t i = 0; i < ten.size(); ++i)
            for (std::size_t j = i; j < ten.size(); ++j)
                if (fromLine(p, ten[i], ten[j]) <= slack) return true;
        for (std::size_t i = 0; i < onScreen.size(); ++i)
            for (std::size_t j = i; j < onScreen.size(); ++j)
                if (fromLine(p, onScreen[i], onScreen[j]) <= slack) return true;
        return false;
    }
};

// The faults of one screen in one theme: each colour that is not the theme's, with where it is.
std::vector<std::string> colourFaults(const Theme& theme, const Uint32* pixels, int pitch) {
    const Explain explain(theme);
    const auto at = [&](int x, int y) { return 0xff000000u | pixels[y * pitch + x]; };
    std::unordered_map<Uint32, int> flatSeen, edgeSeen; // colour: where first seen (y * 640 + x)
    for (int y = 0; y < ui::kScreenH; ++y)
        for (int x = 0; x < ui::kScreenW; ++x) {
            const Uint32 c = at(x, y);
            bool isFlat = x > 0 && y > 0 && x + 1 < ui::kScreenW && y + 1 < ui::kScreenH;
            for (int dy = -1; isFlat && dy <= 1; ++dy)
                for (int dx = -1; isFlat && dx <= 1; ++dx) isFlat = at(x + dx, y + dy) == c;
            std::unordered_map<Uint32, int>& seen = isFlat ? flatSeen : edgeSeen;
            if (!seen.count(c)) seen[c] = y * ui::kScreenW + x;
        }
    std::vector<std::string> out;
    std::vector<SDL_Color> onScreen;
    const auto say = [&](const char* what, Uint32 c, int where) {
        char line[120];
        std::snprintf(line, sizeof line, "%s colour %d,%d,%d at %d,%d", what, int((c >> 16) & 255), int((c >> 8) & 255), int(c & 255), where % ui::kScreenW, where / ui::kScreenW);
        out.push_back(line);
    };
    for (const auto& f : flatSeen) {
        if (explain.flat(f.first)) onScreen.push_back(SDL_Color{Uint8((f.first >> 16) & 255), Uint8((f.first >> 8) & 255), Uint8(f.first & 255), 255});
        else say("a flat part in a", f.first, f.second);
    }
    for (const auto& e : edgeSeen)
        if (!flatSeen.count(e.first) && !explain.edge(e.first, onScreen)) say("a", e.first, e.second);
    return out;
}

// The check itself, on screens made for it: one of the theme's colours, a wash, a letter's edge
// pass; a colour of another theme, or of none, is found, flat or as a line.
void testTheColourCheck() {
    Canvas& c = canvas();
    for (int id = 0; id < kThemes; ++id) {
        ui::useTheme(id);
        c.clear();
        ui::fill(c.r, 20, 40, 200, 100, ui::kPanel);
        ui::fill(c.r, 30, 50, 60, 30, ui::faded(ui::kPurple, 76));                  // a wash...
        ui::fill(c.r, 40, 60, 60, 30, ui::faded(ui::kBg, 185));                     // ...and a wash over it
        c.fonts->large.draw(c.r, 120.f, 60.f, "THEME 123", ui::kText);              // letters' edges
        c.fonts->small.draw(c.r, 120.f, 100.f, "the quiet ones", ui::faded(ui::kDim, 170));
        SDL_RenderPresent(c.r);
        const std::vector<Uint32> good = c.all();
        const std::vector<std::string> none = colourFaults(kThemeTable[id], good.data(), ui::kScreenW);
        for (const std::string& f : none) std::printf("    %s: %s\n", kThemeTable[id].name, f.c_str());
        CHECK(none.empty());
        // A box in an accent of another theme: found, for nearly all of them. (Some come out as one of
        // this theme's laid over another: BROWN's red is BLACK's at an alpha. Another theme's surfaces
        // and lettering are near greys, and mostly do: the check is strict where the jobs show.)
        int caught = 0, tried = 0;
        for (int o = 0; o < kThemes; ++o)
            for (const SDL_Color& colour : {kThemeTable[o].teal, kThemeTable[o].purple, kThemeTable[o].brass, kThemeTable[o].stop}) {
                bool own = false;
                for (const SDL_Color& mine : tenOf(kThemeTable[id])) own = own || same(mine, colour);
                if (own) continue;
                c.clear();
                ui::fill(c.r, 300, 300, 12, 12, colour);
                SDL_RenderPresent(c.r);
                const std::vector<Uint32> bad = c.all();
                ++tried;
                const bool found = !colourFaults(kThemeTable[id], bad.data(), ui::kScreenW).empty();
                caught += found ? 1 : 0;
                if (!found && std::getenv("THEME_PRINT")) std::printf("    %s: %d,%d,%d of %s passes\n", kThemeTable[id].name, colour.r, colour.g, colour.b, kThemeTable[o].name);
            }
        if (caught < tried * 9 / 10) std::printf("    %s: %d of %d other colours caught\n", kThemeTable[id].name, caught, tried);
        CHECK(caught >= tried * 9 / 10);
        std::vector<Uint32> bad;
        // A line in a colour no theme has: found too. (A thin line in a colour that lies between two of
        // the theme's is not: the edge of a letter looks the same. Flat parts are where the check is strict.)
        c.clear();
        SDL_SetRenderDrawColor(c.r, 255, 0, 255, 255);
        SDL_RenderDrawLine(c.r, 10, 200, 300, 260);
        SDL_RenderPresent(c.r);
        bad = c.all();
        CHECK(!colourFaults(kThemeTable[id], bad.data(), ui::kScreenW).empty());
        // BROWN's accents, where a light theme's should be (the colour a screen forgot to route): every one found.
        if (id >= kThemePaper)
            for (const SDL_Color& colour : {kThemeTable[kThemeBrown].teal, kThemeTable[kThemeBrown].purple, kThemeTable[kThemeBrown].brass, kThemeTable[kThemeBrown].stop}) {
                c.clear();
                ui::fill(c.r, 300, 300, 12, 12, colour);
                SDL_RenderPresent(c.r);
                bad = c.all();
                CHECK(!colourFaults(kThemeTable[id], bad.data(), ui::kScreenW).empty());
            }
    }
    // BROWN's own colours, laid on PAPER where PAPER's should be: the stop square, the tab.
    ui::useTheme(kThemePaper);
    c.clear();
    ui::fill(c.r, 0, 0, 640, 26, ui::kPanel);
    ui::fill(c.r, 620, 8, 10, 10, kThemeTable[kThemeBrown].stop);
    SDL_RenderPresent(c.r);
    std::vector<Uint32> bad = c.all();
    CHECK(!colourFaults(kThemeTable[kThemePaper], bad.data(), ui::kScreenW).empty());
    ui::useTheme(kThemeBrown);
}

// Every screen the rig reaches without a still, in every theme (the views run has the rest).
void testTheScreensInEveryTheme() {
    Rig rig;
    Session& session = rig.session();
    const auto checkAll = [&](const char* what) {
        for (int t = 0; t < kThemes; ++t) {
            session.setTheme(t);
            const std::vector<Uint32> screen = rig.draw();
            const std::vector<std::string> faults = colourFaults(kThemeTable[t], screen.data(), ui::kScreenW);
            for (const std::string& f : faults) std::printf("    %s in %s: %s\n", what, kThemeTable[t].name, f.c_str());
            CHECK(faults.empty());
        }
        session.setTheme(kThemeBrown);
    };
    for (View v : {View::Song, View::Pattern, View::Device, View::Effects, View::Settings}) {
        session.view = v;
        rig.frames(2);
        checkAll("a tab");
    }
    // SONG with the track muted (its head washed over).
    session.view = View::Song;
    session.setMuted(0, true);
    rig.frames(2);
    checkAll("SONG, a track muted");
    session.setMuted(0, false);
    session.view = View::Settings;
    rig.app.settings().goTo(SettingsScreen::kTest);
    rig.tap(A); // the button test
    rig.key(SDLK_UP, true), rig.frames(1);
    checkAll("the button test");
    rig.key(SDLK_UP, false);
    rig.frames(300); // (it closes by itself)

    // The piano roll: its keyboard is drawn as a keyboard in every theme, the white keys lighter
    // than the black ones. (Drawn by job, the dim white keys and the background's black keys came
    // out as a negative on a light theme: light black keys on a dark strip.)
    CHECK(session.addTrack(TrackKind::Qmod));
    rig.app.project().track = 1;
    session.view = View::Pattern;
    rig.frames(2);
    CHECK(session.octavesShown() == 2); // (25 pixels a key from x 28, the keys 22 high from y 45, the black 13)
    checkAll("the piano roll");
    const auto colourAt = [](const std::vector<Uint32>& screen, int x, int y) {
        const Uint32 p = screen[static_cast<std::size_t>(y * ui::kScreenW + x)];
        return SDL_Color{Uint8((p >> 16) & 255), Uint8((p >> 8) & 255), Uint8(p & 255), 255};
    };
    for (int t = 0; t < kThemes; ++t) {
        session.setTheme(t);
        const std::vector<Uint32> screen = rig.draw();
        const SDL_Color black = colourAt(screen, 28 + 1 * 25 + 12, 45 + 5); // C#3, near the top
        const SDL_Color white = colourAt(screen, 28 + 2 * 25 + 12, 45 + 18); // D3, under the black keys
        const SDL_Color between = colourAt(screen, 28 + 5 * 25 - 1, 45 + 18); // the line between E3 and F3
        if (!(luminance(white) > luminance(black) && contrast(white, black) >= 3.0 && same(between, black)))
            std::printf("    the roll's keys in %s: white %d,%d,%d black %d,%d,%d\n", kThemeTable[t].name, white.r, white.g, white.b, black.r, black.g, black.b);
        CHECK(luminance(white) > luminance(black) && contrast(white, black) >= 3.0);
        CHECK(same(between, black)); // two white keys side by side: a line in the black keys' colour
        // BROWN as it was built, to the pixel.
        if (t == kThemeBrown) CHECK(same(white, kThemeTable[t].dim) && same(black, kThemeTable[t].bg));
    }
    session.setTheme(kThemeBrown);
}

// ---- every still, drawn by the program ---------------------------------------------------------
// One still: the program draws its last frame in every theme (--themes). Gives its faults.
std::vector<std::string> viewFaults(const std::string& program, const std::string& folder, const std::string& samples, int index, bool* ran) {
    using namespace stills;
    const std::string view = kViews[index];
    const std::size_t space = view.find(' ');
    const std::string name = view.substr(0, space), flags = space == std::string::npos ? "" : view.substr(space);
    const std::string prefix = folder + "/still" + std::to_string(index) + "-", songs = folder + "/songs" + std::to_string(index);
    mkdir(songs.c_str(), 0755);
    const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo" + (name.empty() ? "" : " --view " + name) + flags + " --samples '" + samples +
                                "' --songs '" + songs + "' --still-ms 250 --themes '" + prefix + "' > /dev/null 2>&1";
    std::vector<std::string> out;
    *ran = std::system(command.c_str()) == 0;
    if (!*ran) return out;
    std::vector<std::vector<Uint32>> drawn;
    for (int t = 0; t < kThemes; ++t) {
        const std::string path = prefix + std::to_string(t) + ".bmp";
        SDL_Surface* loaded = SDL_LoadBMP(path.c_str());
        SDL_Surface* picture = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0) : nullptr;
        if (loaded) SDL_FreeSurface(loaded);
        unlink(path.c_str());
        if (!picture || picture->w != ui::kScreenW || picture->h != ui::kScreenH) {
            *ran = false;
            if (picture) SDL_FreeSurface(picture);
            return out;
        }
        const Uint32* pixels = static_cast<const Uint32*>(picture->pixels);
        for (const std::string& f : colourFaults(kThemeTable[t], pixels, picture->pitch / 4)) out.push_back(std::string(kThemeTable[t].name) + ": " + f);
        std::vector<Uint32> all;
        for (int y = 0; y < ui::kScreenH; ++y)
            for (int x = 0; x < ui::kScreenW; ++x) all.push_back(0xff000000u | pixels[y * (picture->pitch / 4) + x]);
        drawn.push_back(all);
        // The background is the theme's (a still is drawn in it, not in the one before).
        if (all[static_cast<std::size_t>(ui::kScreenW * (ui::kScreenH - 1))] != Canvas::colour(kThemeTable[t].panel) && all[0] != Canvas::colour(kThemeTable[t].panel))
            out.push_back(std::string(kThemeTable[t].name) + ": the bars are not in its panel colour");
        SDL_FreeSurface(picture);
    }
    // The six are six: each theme changed the picture (no screen ignores it).
    for (int a = 0; a < kThemes; ++a)
        for (int b = a + 1; b < kThemes; ++b) {
            int differ = 0;
            for (std::size_t i = 0; i < drawn[static_cast<std::size_t>(a)].size(); ++i) differ += drawn[static_cast<std::size_t>(a)][i] != drawn[static_cast<std::size_t>(b)][i] ? 1 : 0;
            if (differ < 640 * 480 / 2) out.push_back(std::string(kThemeTable[a].name) + " and " + kThemeTable[b].name + ": less than half the screen differs");
        }
    return out;
}

// "--theme NAME" on a still: the setting is set, and the screen is drawn in it from the first
// frame; the controls setup too (main.cpp puts the theme to use before it draws, with no App).
void testTheStillsFlag(const std::string& program, const std::string& folder) {
    const auto corner = [&](const char* flags, Uint32* pixel) {
        const std::string picture = folder + "/flag.bmp";
        unlink(picture.c_str());
        const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo " + flags + " --still-ms 100 --shot '" + picture + "' > /dev/null 2>&1";
        if (std::system(command.c_str()) != 0) return false;
        SDL_Surface* loaded = SDL_LoadBMP(picture.c_str());
        SDL_Surface* p = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0) : nullptr;
        if (loaded) SDL_FreeSurface(loaded);
        if (!p) return false;
        *pixel = 0xff000000u | static_cast<const Uint32*>(p->pixels)[30 * (p->pitch / 4) + 4]; // under the top bar, left of everything
        SDL_FreeSurface(p);
        return true;
    };
    Uint32 pixel = 0;
    CHECK(corner("--view setup", &pixel) && pixel == Canvas::colour(kThemeTable[kThemeBrown].bg)); // nothing asked: as built
    const char* names[kThemes] = {"brown", "blue", "green", "black", "paper", "grey"};
    for (int t = 0; t < kThemes; ++t) CHECK(corner((std::string("--view setup --theme ") + names[t]).c_str(), &pixel) && pixel == Canvas::colour(kThemeTable[t].bg));
    CHECK(corner("--view setup --theme PAPER", &pixel) && pixel == Canvas::colour(kThemeTable[kThemePaper].bg)); // (in either case)
    CHECK(!corner("--view setup --theme mauve", &pixel)); // a theme nobody knows: said, and no still
    CHECK(corner("--learn --theme grey", &pixel) && pixel == Canvas::colour(kThemeTable[kThemeGrey].bg));
    // --themes draws all six and then puts the theme back: a picture kept after it is in the one asked for.
    const std::string six = "--view setup --theme blue --themes '" + folder + "/six-'";
    CHECK(corner(six.c_str(), &pixel) && pixel == Canvas::colour(kThemeTable[kThemeBlue].bg));
    for (int t = 0; t < kThemes; ++t) unlink((folder + "/six-" + std::to_string(t) + ".bmp").c_str());
}

int runViews(const char* program, const char* only) {
    using namespace stills;
    char pattern[] = "/tmp/apotheca-themeviews-XXXXXX";
    const std::string folder = mkdtemp(pattern), samples = folder + "/samples";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/kicks").c_str(), 0755);
    const auto wav = [&](const char* name, float seconds, float hz) {
        std::vector<float> sound(static_cast<std::size_t>(48000.f * seconds));
        for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(6.2831853f * hz * static_cast<float>(i) / 48000.f) * (1.f - static_cast<float>(i) / static_cast<float>(sound.size()));
        CHECK(writeWav(samples + "/" + name + ".wav", sound, 48000));
    };
    wav("amen break 136", 1.76f, 180.f);
    wav("kicks/808 long boom", 0.5f, 55.f);
    wav("hat closed", 0.1f, 4000.f);
    wav("vocal take 06", 1.2f, 220.f);
    wav("long ambient texture", 2.f, 110.f);

    if (!only) testTheStillsFlag(program, folder);
    // The controls setup is a still of its own (--learn), drawn by main.cpp with no App.
    std::vector<std::string> list(kViews, kViews + kViewCount);
    list.push_back(" --learn");
    const int count = static_cast<int>(list.size());
    std::vector<std::vector<std::string>> faults(static_cast<std::size_t>(count));
    std::vector<char> ran(static_cast<std::size_t>(count), 0);
    std::atomic<int> next{0};
    std::vector<std::thread> workers;
    for (int w = 0; w < 4; ++w)
        workers.emplace_back([&] {
            for (int i = next++; i < count; i = next++) {
                if (only && list[static_cast<std::size_t>(i)].find(only) == std::string::npos) continue;
                bool did = false;
                // (viewFaults reads kViews by number: the one past them is the setup.)
                if (i < kViewCount) faults[static_cast<std::size_t>(i)] = viewFaults(program, folder, samples, i, &did);
                else {
                    const std::string prefix = folder + "/learn-";
                    did = std::system(("SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + std::string(program) + "' --demo --learn --still-ms 250 --themes '" + prefix + "' > /dev/null 2>&1").c_str()) == 0;
                    for (int t = 0; did && t < kThemes; ++t) {
                        const std::string path = prefix + std::to_string(t) + ".bmp";
                        SDL_Surface* loaded = SDL_LoadBMP(path.c_str());
                        SDL_Surface* picture = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0) : nullptr;
                        if (loaded) SDL_FreeSurface(loaded);
                        unlink(path.c_str());
                        did = picture != nullptr;
                        if (!picture) break;
                        for (const std::string& f : colourFaults(kThemeTable[t], static_cast<const Uint32*>(picture->pixels), picture->pitch / 4))
                            faults[static_cast<std::size_t>(i)].push_back(std::string(kThemeTable[t].name) + ": " + f);
                        if ((0xff000000u | static_cast<const Uint32*>(picture->pixels)[0]) != Canvas::colour(kThemeTable[t].bg))
                            faults[static_cast<std::size_t>(i)].push_back(std::string(kThemeTable[t].name) + ": not on its background");
                        SDL_FreeSurface(picture);
                    }
                }
                ran[static_cast<std::size_t>(i)] = did ? 1 : 0;
            }
        });
    for (std::thread& worker : workers) worker.join();
    int drawn = 0;
    for (int i = 0; i < count; ++i) {
        const std::string& name = list[static_cast<std::size_t>(i)];
        if (only && name.find(only) == std::string::npos) continue;
        ++drawn;
        if (!ran[static_cast<std::size_t>(i)]) std::printf("    the still \"%s\" was not drawn\n", name.c_str());
        CHECK(ran[static_cast<std::size_t>(i)]);
        int said = 0;
        for (const std::string& fault : faults[static_cast<std::size_t>(i)])
            if (++said <= 6) std::printf("    %s: %s\n", name.empty() ? "(the drum grid)" : name.c_str(), fault.c_str());
        if (said > 6) std::printf("    %s: and %d more\n", name.c_str(), said - 6);
        CHECK(faults[static_cast<std::size_t>(i)].empty());
    }
    CHECK(only || drawn == count);
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("themes, %d stills in six themes: tests passed\n", drawn);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (argc >= 3 && std::strcmp(argv[1], "views") == 0) return runViews(argv[2], argc >= 4 ? argv[3] : nullptr);
    const bool print = argc >= 2 && std::strcmp(argv[1], "--measure") == 0;
    CHECK(canvas().ok);
    testTheMeasures();
    testThePalettes(print);
    testTheColourCheck();
    testTheSettingIsKept();
    testTheRow();
    testTheScreensInEveryTheme();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("themes: tests passed\n");
    return 0;
}
