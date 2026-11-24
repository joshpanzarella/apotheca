// SIGNAL on the DEVICE pages, read off the drawn screen (owner, 2026-11-14: "it could be
// smaller, and maybe we can utilize the screen better"). The scope is a thin strip across the whole
// width just above the hints bar, and the controls above it are spread down to it: no large empty
// band is left between the last row and the strip, and nothing runs into the strip.

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "synth_spec.hpp"

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

// The most empty rows allowed between the last row of controls and the strip, on a page whose
// rows are spread down to it.
constexpr int kMostEmpty = 12;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    Project& project() { return app.project(); }
};

// What a page is, and how to get there.
struct Page {
    const char* name;
    TrackKind kind; // None: the drum track the rig starts with
    int page;       // a synth's page (0 is MAIN)
    bool fills;     // its rows are spread down to the strip
};

const Page kPages[] = {
    {"phazed MAIN", TrackKind::Phazed, 0, true}, {"phazed SOUND", TrackKind::Phazed, 1, true},
    {"phazed LINE 1", TrackKind::Phazed, 2, true}, {"phazed LINE 2", TrackKind::Phazed, 3, true},
    {"JAX-EP MAIN", TrackKind::Jx3p, 0, true}, {"JAX-EP DCO-1", TrackKind::Jx3p, 1, true},
    {"JAX-EP DCO-2", TrackKind::Jx3p, 2, true}, {"JAX-EP VCF", TrackKind::Jx3p, 3, true},
    {"JAX-EP VCA AND ENVELOPE", TrackKind::Jx3p, 4, true}, {"JAX-EP LFO AND CHORUS", TrackKind::Jx3p, 5, true},
    {"JAX-EP 6", TrackKind::Jx3p, 6, true},
    {"FM-6 MAIN", TrackKind::Fm6, 0, true}, {"FM-6 VOICE AND LFO", TrackKind::Fm6, 1, true},
    {"FM-6 OPERATOR 1", TrackKind::Fm6, 2, true}, {"FM-6 OPERATOR 6", TrackKind::Fm6, 7, true},
    {"FM-6 PITCH ENVELOPE", TrackKind::Fm6, 8, true},
    {"PHYSIC MAIN", TrackKind::Physic, 0, true}, {"PHYSIC SOUND", TrackKind::Physic, 1, true},
    {"PHYSIC RING", TrackKind::Physic, 2, true}, {"PHYSIC PLAY", TrackKind::Physic, 3, true},
    {"PHYSIC DICE", TrackKind::Physic, 4, true}, {"PHYSIC BODY", TrackKind::Physic, 5, true},
    {"ACID MAIN", TrackKind::Acid, 0, true}, {"ACID SOUND", TrackKind::Acid, 1, true}, {"ACID FILTER", TrackKind::Acid, 2, true},
    {"CLOUD MAIN", TrackKind::Cloud, 0, true}, {"CLOUD SOURCE", TrackKind::Cloud, 1, true}, {"CLOUD GRAINS", TrackKind::Cloud, 2, true}, {"CLOUD PLAY", TrackKind::Cloud, 3, true},
    {"MORPH MAIN", TrackKind::Morph, 0, true}, {"MORPH OSC", TrackKind::Morph, 1, true}, {"MORPH VOICE", TrackKind::Morph, 2, true}, {"MORPH FILTER", TrackKind::Morph, 3, true},
    {"MORPH AMP", TrackKind::Morph, 4, true}, {"MORPH ENV", TrackKind::Morph, 5, true},
    {"sampler", TrackKind::Sampler, 0, true}, {"kit", TrackKind::None, 0, true},
};

SynthScreen* synthScreen(Rig& rig, TrackKind kind) {
    return kind == TrackKind::Phazed ? &rig.app.phazed() : kind == TrackKind::Jx3p ? &rig.app.jaxep() : kind == TrackKind::Fm6 ? &rig.app.fm6() : kind == TrackKind::Physic ? &rig.app.physic() : kind == TrackKind::Acid ? &rig.app.acid() : kind == TrackKind::Cloud ? &rig.app.cloud() : kind == TrackKind::Morph ? &rig.app.morph() : nullptr;
}

// Draws the page into the canvas, in a theme.
// `cursor` is where the cursor stands on a synth's page; `automate` gives that macro automation
// in the scene worked on (MAIN only).
bool drawPage(Canvas& canvas, const Page& page, int theme, int cursor = 0, int automate = -1) {
    Rig rig;
    rig.app.session().setTheme(theme);
    int track = 0;
    if (page.kind != TrackKind::None) {
        if (!rig.app.session().addTrack(page.kind)) return false;
        track = rig.project().trackCount() - 1;
    }
    rig.project().track = track;
    rig.app.session().view = View::Device;
    if (automate >= 0) rig.app.session().setAutomation(track, rig.project().scene, automate, 3, 0.5f);
    if (SynthScreen* screen = synthScreen(rig, page.kind)) screen->goTo(page.page, cursor);
    rig.frames(2);
    rig.app.draw(canvas.r, *canvas.fonts);
    return true;
}

bool rowIs(const Canvas& canvas, int y, int x0, int x1, SDL_Color c) {
    for (int x = x0; x < x1; ++x)
        if (!canvas.is(x, y, c)) return false;
    return true;
}

// The strip: a box across the width (8 to 631) from kScopeY, ending above the hints bar, with its
// label at the left and the line in the rest; clear of everything above it and of the hints bar.
void checkTheStrip(const Canvas& canvas, const char* name) {
    using namespace ui;
    const int top = kScopeY, bottom = kScopeY + kScopeH - 1, right = kScopeX + kScopeW - 1;
    bool ok = true;
    const auto check = [&](bool what, const char* says) {
        if (!what) std::printf("  %s: %s\n", name, says);
        ok = ok && what;
    };
    check(bottom < kScreenH - kHintsH, "ends above the hints bar");
    check(kScopeX == 8 && kScopeW == kScreenW - 16, "as wide as the page's boxes");
    check(rowIs(canvas, top, kScopeX, right + 1, kLine) && rowIs(canvas, bottom, kScopeX, right + 1, kLine), "its top and bottom edges run the width");
    for (int y = top; y <= bottom; ++y) {
        check(canvas.is(kScopeX, y, kLine) && canvas.is(right, y, kLine), "its two ends");
        check(canvas.is(kScopeX - 1, y, kBg) && canvas.is(right + 1, y, kBg), "nothing beside it");
    }
    // Clear of the hints bar, and of the controls above it (they end at kAboveScope).
    for (int y = bottom + 1; y < kScreenH - kHintsH; ++y) check(rowIs(canvas, y, 0, kScreenW, kBg), "a clear row under it");
    // (The row at kAboveScope may hold the cursor's outline, one row outside a box on the last row.)
    for (int y = kAboveScope + 1; y < top; ++y) check(rowIs(canvas, y, 0, kScreenW, kBg), "a clear row over it");
    // The label's room ends at a line, and the label is in it, dim.
    const int divider = kScopeX + kScopeLabelW - 1;
    for (int y = top + 1; y < bottom; ++y) check(canvas.is(divider, y, kLine), "the label's room ends");
    int dim = 0;
    for (int y = top + 1; y < bottom; ++y)
        for (int x = kScopeX + 1; x < divider; ++x) dim += canvas.is(x, y, kDim);
    check(dim > 40, "SIGNAL is written in its room");
    // The line: nothing playing, so it lies along the middle, teal, from the divider to the end.
    const int middle = top + kScopeH / 2;
    check(rowIs(canvas, middle, divider + 2, right - 1, kTeal), "the line runs the rest of the width");
    for (int y = top + 1; y < bottom; ++y)
        for (int x = kScopeX + 1; x < divider; ++x) check(!canvas.is(x, y, kTeal), "no line under the label");
    CHECK(ok);
}

// A synth's MAIN (design A, owner 2026-11-24): SIGNAL is a large scope right of ADVANCED, down to the strip's foot.
void checkTheMainScope(const Canvas& canvas, const char* name, int top) {
    using namespace ui;
    bool ok = true;
    const auto check = [&](bool good, const char* what) {
        if (!good && ok) std::printf("  %s: %s\n", name, what);
        ok = ok && good;
    };
    const int x = SynthScreen::kMainScopeX, w = SynthScreen::kMainScopeW, bottom = kScopeY + kScopeH - 1;
    check(canvas.is(x + w / 2, top, kLine) && canvas.is(x + w / 2, bottom, kLine), "its edges, top and foot");
    check(canvas.is(x, (top + bottom) / 2, kLine) && canvas.is(x + w - 1, (top + bottom) / 2, kLine), "its edges, left and right");
    for (int y = bottom + 1; y < kScreenH - kHintsH; ++y) check(rowIs(canvas, y, 0, kScreenW, kBg), "a clear row under it");
    int dim = 0;
    for (int y = top + 4; y < top + 22; ++y)
        for (int px = x + 4; px < x + 100; ++px) dim += canvas.is(px, y, kDim);
    check(dim > 30, "SIGNAL is written at its top left");
    // Nothing playing: the line lies along the middle, teal, across.
    const int middle = scopeBoxLineY(0.f, top, bottom + 1 - top);
    int teal = 0;
    for (int px = x + 3; px < x + w - 3; ++px) teal += canvas.is(px, middle, kTeal) ? 1 : 0;
    check(teal > w - 12, "the line runs across the middle");
    CHECK(ok);
}

// How many empty rows there are between the lowest row that has anything in it and the strip.
int emptyAboveTheStrip(const Canvas& canvas) {
    int y = ui::kScopeY - 1;
    while (y > ui::kTopBarH && rowIs(canvas, y, 0, ui::kScreenW, ui::kBg)) --y;
    return ui::kScopeY - 1 - y;
}

void testTheStripOnEveryPage() {
    for (int theme : {static_cast<int>(kThemeBrown), static_cast<int>(kThemeSolarized)}) {
        for (const Page& page : kPages) {
            Canvas canvas;
            CHECK(canvas.ok);
            if (!canvas.ok) return;
            CHECK(drawPage(canvas, page, theme));
            if (page.page == 0 && page.kind != TrackKind::Sampler && page.kind != TrackKind::None) {
                const SynthSpec* spec = specFor(page.kind);
                checkTheMainScope(canvas, page.name, spec && (spec->recording || spec->table) ? SynthScreen::kPicAdvancedY : SynthScreen::kAdvancedY);
                continue;
            }
            checkTheStrip(canvas, page.name);
            const int empty = emptyAboveTheStrip(canvas);
            if (page.fills) {
                if (empty > kMostEmpty) std::printf("  %s: %d empty rows over the strip\n", page.name, empty);
                CHECK(empty <= kMostEmpty);
                // The rows end at kAboveScope (a row or two sooner where its height does not divide evenly).
                const int gap = ui::kScopeY - ui::kAboveScope;
                if (empty < gap - 1 || empty > gap + 2) std::printf("  %s: its rows end %d rows over the strip\n", page.name, empty);
                CHECK(empty >= gap - 1 && empty <= gap + 2);
            }
        }
    }
}

// The rows themselves: the numbers the screens draw from, and where the pages put them.
void testTheRowsAreSpreadDown() {
    // MAIN: FOLDER and PRESET, two rows of macros, ADVANCED, ending where the strip leaves room.
    CHECK(SynthScreen::kAdvancedY + SynthScreen::kAdvancedH == ui::kAboveScope);
    CHECK(SynthScreen::kMacroH > 70 && SynthScreen::kAdvancedH > 40 && SynthScreen::kPresetH > 50); // all taller than they were
    // A page of settings: however many rows it has (one to six), they fill it down to the strip, the
    // last ending on it exactly, every gap kSettingGap or a pixel more, and the first where it always was.
    for (int rows = 1; rows <= 6; ++rows) {
        const int h = SynthScreen::settingH(rows);
        CHECK(SynthScreen::settingY(rows, 0) == SynthScreen::kSettingY);
        CHECK(SynthScreen::settingY(rows, rows - 1) + h == ui::kAboveScope || (rows == 1 && SynthScreen::kSettingY + h == ui::kAboveScope));
        for (int row = 1; row < rows; ++row) {
            const int gap = SynthScreen::settingY(rows, row) - SynthScreen::settingY(rows, row - 1) - h;
            const int want = rows >= SynthScreen::kTightRows ? SynthScreen::kTightGap : SynthScreen::kSettingGap;
            CHECK(gap >= want && gap <= want + 1);
        }
    }
    CHECK(SynthScreen::settingH(6) == 55); // FM-6's operator pages: six rows, closer together, each tall enough for its value
    // The kit's eight lanes and the sampler's two rows of settings with PRESET below them.
    CHECK(KitScreen::kLaneTop + kDrumLanes * KitScreen::kLaneRowH - KitScreen::kLaneGap == ui::kAboveScope);
    CHECK(SamplerScreen::kPresetY + SamplerScreen::kPresetH == ui::kAboveScope);
    CHECK(SamplerScreen::kWaveH > 40); // the recording's picture got the room too

    // Read off the screen: the kit's last lane and the sampler's last settings end on the row above kAboveScope
    // (their bottom edges). (MAIN's ADVANCED goes down to the foot, beside SIGNAL: checkTheMainScope.)
    struct Edge { Page page; int x; };
    const Edge edges[] = {
        {{"phazed SOUND", TrackKind::Phazed, 1, true}, 20}, // LEVEL
        {{"sampler", TrackKind::Sampler, 0, true}, 20},     // RELEASE
        {{"kit", TrackKind::None, 0, true}, 20},            // lane 8
    };
    for (const Edge& e : edges) {
        Canvas canvas;
        if (!canvas.ok || !drawPage(canvas, e.page, kThemeBrown)) { CHECK(false); continue; }
        CHECK(canvas.is(e.x, ui::kAboveScope - 1, ui::kLine));
    }
    // A page of two rows, one of one and one of six: their boxes end on the row above kAboveScope too.
    for (const Page& page : {Page{"FM-6 PITCH ENVELOPE", TrackKind::Fm6, 8, true}, Page{"phazed LINE 1", TrackKind::Phazed, 2, true},
                             Page{"FM-6 OPERATOR 3", TrackKind::Fm6, 4, true}}) {
        Canvas canvas;
        if (!canvas.ok || !drawPage(canvas, page, kThemeBrown)) { CHECK(false); continue; }
        const int x = 8 + SynthScreen::kMacroPitchX + 12; // the second box (the cursor is on the first)
        if (!canvas.is(x, ui::kAboveScope - 1, ui::kLine)) std::printf("  %s: its boxes do not reach the strip\n", page.name);
        CHECK(canvas.is(x, ui::kAboveScope - 1, ui::kLine) && canvas.is(x, ui::kAboveScope, ui::kBg));
    }
    // PHYSIC's PLAY, DICE and BODY: five boxes, the fifth alone on its row, which reaches the strip (the cursor on the second).
    for (const Page& page : {Page{"PHYSIC PLAY", TrackKind::Physic, 3, true}, Page{"PHYSIC DICE", TrackKind::Physic, 4, true}, Page{"PHYSIC BODY", TrackKind::Physic, 5, true}}) {
        Canvas canvas;
        if (!canvas.ok || !drawPage(canvas, page, kThemeBrown, 1)) { CHECK(false); continue; }
        const int x = 8 + 12;
        CHECK(canvas.is(x, ui::kAboveScope - 1, ui::kLine) && canvas.is(x, ui::kAboveScope, ui::kBg));
    }
}

const Page kPhazedMain = {"phazed MAIN", TrackKind::Phazed, 0, true};

// ADVANCED's words stand in ADVANCED's box (design A: "ADVANCED" and how many pages, beside SIGNAL), never over the
// macros. Read off the screen: the gaps between the macro boxes are empty all the way down to ADVANCED.
void testTheAdvancedWordsAreInTheirRow() {
    Canvas canvas;
    if (!canvas.ok || !drawPage(canvas, kPhazedMain, kThemeBrown)) { CHECK(false); return; }
    using S = SynthScreen;
    for (int column = 1; column < 4; ++column) {
        const int gap = 8 + column * S::kMacroPitchX - (S::kMacroPitchX - S::kMacroW); // the first empty column before the box
        for (int y = S::kMacroY; y < S::kAdvancedY; ++y)
            for (int x = gap; x < gap + S::kMacroPitchX - S::kMacroW; ++x)
                if (!canvas.is(x, y, ui::kBg)) {
                    std::printf("  something at %d,%d between the macros\n", x, y);
                    CHECK(false);
                    return;
                }
    }
    for (int y = S::kMacroY + 2 * S::kMacroH + S::kMacroGap; y < S::kAdvancedY; ++y) CHECK(rowIs(canvas, y, 0, ui::kScreenW, ui::kBg));
    // The two lines, ADVANCED (text) over the pages (dim), in the box.
    int text = 0, dim = 0;
    for (int y = S::kAdvancedY + 2; y < ui::kScopeY + ui::kScopeH - 2; ++y)
        for (int x = 10; x < 8 + S::kMacroW - 2; ++x) {
            text += canvas.is(x, y, ui::kText) ? 1 : 0;
            dim += canvas.is(x, y, ui::kDim) ? 1 : 0;
        }
    CHECK(text > 60 && dim > 30);
}

// The purple "this macro has automation" line runs along the bottom of the macro's box, as tall as
// the box is now: not across its middle.
void testTheAutomationLineIsAtTheBottom() {
    using S = SynthScreen;
    Canvas plain, automated;
    if (!plain.ok || !automated.ok || !drawPage(plain, kPhazedMain, kThemeBrown) || !drawPage(automated, kPhazedMain, kThemeBrown, 0, 1)) {
        CHECK(false);
        return;
    }
    const int x = 8 + S::kMacroPitchX, y = S::kMacroY; // the second macro
    CHECK(rowIs(automated, y + S::kMacroH - 3, x + 1, x + S::kMacroW - 1, ui::kPurple));
    CHECK(rowIs(automated, y + S::kMacroH - 2, x + 1, x + S::kMacroW - 1, ui::kPurple));
    int purple = 0;
    for (int yy = y; yy < y + S::kMacroH - 3; ++yy)
        for (int xx = x; xx < x + S::kMacroW; ++xx) purple += automated.is(xx, yy, ui::kPurple);
    CHECK(purple == 0);
    CHECK(!rowIs(plain, y + S::kMacroH - 3, x + 1, x + S::kMacroW - 1, ui::kPurple)); // and only where there is automation
}

// The cursor on ADVANCED: its outline goes round the whole box, as tall as it is.
void testTheAdvancedOutline() {
    using S = SynthScreen;
    Canvas canvas;
    if (!canvas.ok || !drawPage(canvas, kPhazedMain, kThemeBrown, S::kAdvanced)) { CHECK(false); return; }
    const int foot = ui::kScopeY + ui::kScopeH; // (down beside SIGNAL, to its foot: design A)
    for (int y = S::kAdvancedY - 1; y <= foot; ++y) CHECK(canvas.is(7, y, ui::kTeal) && canvas.is(8 + S::kMacroW, y, ui::kTeal));
    CHECK(rowIs(canvas, S::kAdvancedY - 1, 7, 9 + S::kMacroW, ui::kTeal));
    CHECK(rowIs(canvas, foot, 7, 9 + S::kMacroW, ui::kTeal));
}

// The preset's count ("1 / 65") stands at the right of PRESET's box on the line of its name.
void testThePresetCountIsOnTheNamesLine() {
    using S = SynthScreen;
    Canvas canvas;
    if (!canvas.ok || !drawPage(canvas, kPhazedMain, kThemeBrown)) { CHECK(false); return; }
    const auto dimRows = [&](int x0, int x1, int& top, int& bottom) {
        top = ui::kScreenH, bottom = 0;
        for (int y = S::kPresetY + 1; y < S::kPresetY + S::kPresetH - 1; ++y)
            for (int x = x0; x < x1; ++x)
                if (canvas.is(x, y, ui::kDim)) {
                    top = y < top ? y : top;
                    bottom = y > bottom ? y : bottom;
                }
    };
    int nameTop, nameBottom, countTop, countBottom;
    dimRows(223, 300, nameTop, nameBottom);  // PRESET
    dimRows(520, 625, countTop, countBottom); // the count
    if (std::abs(countTop - nameTop) > 1) std::printf("  the count's top %d, PRESET's %d\n", countTop, nameTop);
    CHECK(nameTop < nameBottom && std::abs(countTop - nameTop) <= 1 && std::abs(countBottom - nameBottom) <= 3);
}

// The line stays inside the strip at any level: full scale is 4 rows inside its edges.
void testTheLineStaysInTheStrip() {
    using namespace ui;
    CHECK(scopeLineY(0.f) == kScopeMiddle);
    CHECK(scopeLineY(1.f) == kScopeY + 4 && scopeLineY(-1.f) == kScopeY + kScopeH - 4);
    CHECK(scopeLineY(5.f) == scopeLineY(1.f) && scopeLineY(-5.f) == scopeLineY(-1.f));
    // Bent, so a quiet sound shows (owner, 2026-11-14): half scale is 11 of the 14 pixels, a sound 30 dB
    // down (0.03) stands 3 pixels and one 20 dB down 5 or 6, where to scale they were under one and under two.
    CHECK(scopeLineY(0.5f) == kScopeMiddle - 11 && scopeLineY(-0.5f) == kScopeMiddle + 11);
    CHECK(scopeLineY(0.03f) == kScopeMiddle - 3 && scopeLineY(-0.03f) == kScopeMiddle + 3);
    CHECK(kScopeMiddle - scopeLineY(0.1f) >= 5 && kScopeMiddle - scopeLineY(0.1f) <= 6);
    // A louder sound is never drawn smaller, the sign is kept, and what is not a number is the middle.
    for (float a = 0.f; a < 1.f; a += 0.01f) CHECK(scopeBent(a + 0.01f) >= scopeBent(a) && scopeBent(-a) == -scopeBent(a));
    CHECK(scopeBent(0.f) == 0.f && scopeBent(1.f) == 1.f && scopeBent(-1.f) == -1.f && scopeBent(std::nanf("")) == 0.f);
}

// The sampler with no recording: what it says stands in the middle of the picture's room.
void testTheSamplersWordsAreInTheMiddle() {
    Canvas canvas;
    if (!canvas.ok || !drawPage(canvas, {"sampler", TrackKind::Sampler, 0, true}, kThemeBrown)) { CHECK(false); return; }
    int top = ui::kScreenH, bottom = 0;
    for (int y = SamplerScreen::kSoundY + 40; y < SamplerScreen::kSoundY + SamplerScreen::kSoundH - 1; ++y)
        for (int x = 12; x < 620; ++x)
            if (canvas.is(x, y, ui::kDim)) {
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
    const int middle = SamplerScreen::kWaveY + SamplerScreen::kWaveH / 2;
    if (std::abs((top + bottom) / 2 - middle) > 4) std::printf("  the sampler's words at %d..%d, the room's middle %d\n", top, bottom, middle);
    CHECK(top < bottom && std::abs((top + bottom) / 2 - middle) <= 4);
}

// A box taller than it was laid out for keeps its name at the top and moves its value down by
// half of the extra; at the old height and under nothing moves.
void testATallBoxsValueIsInItsMiddle() {
    CHECK(ui::boxValueDrop(ui::kBoxUsualH) == 0.f && ui::boxValueDrop(50) == 0.f && ui::boxValueDrop(114) == 22.f);
    Canvas usual, tall;
    if (!usual.ok || !tall.ok) { CHECK(false); return; }
    // (A macro is a dial now, design A: its value stands under its dial, which is checked below.)
    for (bool macro : {false}) {
        usual.clear();
        tall.clear();
        if (!macro) {
            ui::valueBox(usual.r, *usual.fonts, 10, 40, 153, 70, "CUTOFF", "1 KHZ", -1.f, false, false);
            ui::valueBox(tall.r, *tall.fonts, 10, 40, 153, 114, "CUTOFF", "1 KHZ", -1.f, false, false);
        }
        CHECK(usual.part(11, 41, 151, 20) == tall.part(11, 41, 151, 20));      // the name, where it was
        CHECK(usual.part(11, 61, 120, 20) == tall.part(11, 61 + 22, 120, 20)); // the value, 22 lower (left of the picture)
        CHECK(usual.part(11, 61, 120, 20) != tall.part(11, 61, 120, 20));
    }
    // A tall macro: the dial between the name and the value; a short one: the dial at the left, the words beside it.
    const ui::MacroDial tallDial = ui::macroDialIn(10, 40, 153, 114), shortDial = ui::macroDialIn(10, 40, 153, 69);
    CHECK(tallDial.cy - tallDial.radius > 40 + 20 && tallDial.cy + tallDial.radius < 40 + 114 - 26);
    CHECK(shortDial.cx + shortDial.radius < 10 + 60 && shortDial.cy - shortDial.radius >= 40 && shortDial.cy + shortDial.radius <= 40 + 69);
}

} // namespace

int main() {
    if (SDL_Init(0) != 0) return 1;
    testTheStripOnEveryPage();
    testTheRowsAreSpreadDown();
    testATallBoxsValueIsInItsMiddle();
    testTheAdvancedWordsAreInTheirRow();
    testTheAutomationLineIsAtTheBottom();
    testTheAdvancedOutline();
    testThePresetCountIsOnTheNamesLine();
    testTheLineStaysInTheStrip();
    testTheSamplersWordsAreInTheMiddle();
    if (failures == 0) std::printf("scope tests passed\n");
    return failures == 0 ? 0 : 1;
}
