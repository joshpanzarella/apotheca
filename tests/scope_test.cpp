// LIVE OUTPUT on the DEVICE pages, read off the drawn screen (owner, 2026-11-14: "it could be
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
    {"twin MAIN", TrackKind::Jx3p, 0, true}, {"twin DCO-1", TrackKind::Jx3p, 1, true},
    {"twin DCO-2", TrackKind::Jx3p, 2, true}, {"twin VCF", TrackKind::Jx3p, 3, true},
    {"twin VCA AND ENVELOPE", TrackKind::Jx3p, 4, true}, {"twin LFO AND CHORUS", TrackKind::Jx3p, 5, true},
    {"twin 6", TrackKind::Jx3p, 6, true},
    {"qmod MAIN", TrackKind::Qmod, 0, true}, {"qmod SOUND", TrackKind::Qmod, 1, true},
    {"qmod CHARACTER", TrackKind::Qmod, 2, true}, {"qmod ENVELOPE", TrackKind::Qmod, 3, true},
    {"sampler", TrackKind::Sampler, 0, true}, {"kit", TrackKind::None, 0, true},
};

SynthScreen* synthScreen(Rig& rig, TrackKind kind) {
    return kind == TrackKind::Phazed ? &rig.app.phazed() : kind == TrackKind::Jx3p ? &rig.app.twin() : kind == TrackKind::Qmod ? &rig.app.qmod() : nullptr;
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
    check(dim > 40, "LIVE OUTPUT is written in its room");
    // The line: nothing playing, so it lies along the middle, teal, from the divider to the end.
    const int middle = top + kScopeH / 2;
    check(rowIs(canvas, middle, divider + 2, right - 1, kTeal), "the line runs the rest of the width");
    for (int y = top + 1; y < bottom; ++y)
        for (int x = kScopeX + 1; x < divider; ++x) check(!canvas.is(x, y, kTeal), "no line under the label");
    CHECK(ok);
}

// How many empty rows there are between the lowest row that has anything in it and the strip.
int emptyAboveTheStrip(const Canvas& canvas) {
    int y = ui::kScopeY - 1;
    while (y > ui::kTopBarH && rowIs(canvas, y, 0, ui::kScreenW, ui::kBg)) --y;
    return ui::kScopeY - 1 - y;
}

void testTheStripOnEveryPage() {
    for (int theme : {static_cast<int>(kThemeBrown), static_cast<int>(kThemePaper)}) {
        for (const Page& page : kPages) {
            Canvas canvas;
            CHECK(canvas.ok);
            if (!canvas.ok) return;
            CHECK(drawPage(canvas, page, theme));
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
    // A page of settings: however many rows it has (one to four), they fill it down to the strip.
    for (int rows = 1; rows <= 4; ++rows) {
        const int end = SynthScreen::kSettingY + rows * SynthScreen::settingH(rows) + (rows - 1) * SynthScreen::kSettingGap;
        CHECK(end <= ui::kAboveScope && end > ui::kAboveScope - 4);
    }
    // The kit's eight lanes and the sampler's two rows of settings.
    CHECK(KitScreen::kLaneTop + kDrumLanes * KitScreen::kLaneRowH - KitScreen::kLaneGap == ui::kAboveScope);
    CHECK(SamplerScreen::kSettingY + 2 * SamplerScreen::kSettingH + SamplerScreen::kSettingGap == ui::kAboveScope);
    CHECK(SamplerScreen::kWaveH > 40); // the recording's picture got the room too

    // Read off the screen: the kit's last lane, the sampler's last settings and MAIN's ADVANCED end
    // on the row above kAboveScope (their bottom edges).
    struct Edge { Page page; int x; };
    const Edge edges[] = {
        {{"phazed MAIN", TrackKind::Phazed, 0, true}, 20},  // ADVANCED
        {{"phazed SOUND", TrackKind::Phazed, 1, true}, 20}, // LEVEL
        {{"sampler", TrackKind::Sampler, 0, true}, 20},     // RELEASE
        {{"kit", TrackKind::None, 0, true}, 20},            // lane 8
    };
    for (const Edge& e : edges) {
        Canvas canvas;
        if (!canvas.ok || !drawPage(canvas, e.page, kThemeBrown)) { CHECK(false); continue; }
        CHECK(canvas.is(e.x, ui::kAboveScope - 1, ui::kLine));
    }
    // A page of two rows and one of one: their boxes end on the row above kAboveScope too.
    for (const Page& page : {Page{"qmod SOUND", TrackKind::Qmod, 1, true}, Page{"phazed LINE 1", TrackKind::Phazed, 2, true}}) {
        Canvas canvas;
        if (!canvas.ok || !drawPage(canvas, page, kThemeBrown)) { CHECK(false); continue; }
        const int x = 8 + SynthScreen::kMacroPitchX + 12; // the second box (the cursor is on the first)
        if (!canvas.is(x, ui::kAboveScope - 1, ui::kLine)) std::printf("  %s: its boxes do not reach the strip\n", page.name);
        CHECK(canvas.is(x, ui::kAboveScope - 1, ui::kLine) && canvas.is(x, ui::kAboveScope, ui::kBg));
    }
}

const Page kPhazedMain = {"phazed MAIN", TrackKind::Phazed, 0, true};

// ADVANCED's two lines of words stand in ADVANCED's row, beside its box: never over the macros.
// Read off the screen: the gaps between the macro boxes are empty all the way down to ADVANCED,
// and the words are in its row.
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
    int dim = 0, top = ui::kScreenH, bottom = 0;
    for (int y = S::kMacroY; y < ui::kAboveScope; ++y)
        for (int x = 174; x < 630; ++x)
            if (canvas.is(x, y, ui::kDim) && y >= S::kAdvancedY) {
                ++dim;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
    CHECK(dim > 200);
    CHECK(top > S::kAdvancedY && bottom < S::kAdvancedY + S::kAdvancedH);
    // Both lines, about the middle of the row.
    const int middle = S::kAdvancedY + S::kAdvancedH / 2;
    CHECK(top < middle - 6 && bottom > middle + 6);
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
    for (int y = S::kAdvancedY - 1; y <= S::kAdvancedY + S::kAdvancedH; ++y) CHECK(canvas.is(7, y, ui::kTeal) && canvas.is(8 + S::kMacroW, y, ui::kTeal));
    CHECK(rowIs(canvas, S::kAdvancedY - 1, 7, 9 + S::kMacroW, ui::kTeal));
    CHECK(rowIs(canvas, S::kAdvancedY + S::kAdvancedH, 7, 9 + S::kMacroW, ui::kTeal));
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
    CHECK(scopeLineY(0.5f) == kScopeMiddle - 7);
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
    for (bool macro : {false, true}) {
        usual.clear();
        tall.clear();
        if (macro) {
            ui::macroBox(usual.r, *usual.fonts, 10, 40, 153, 70, "TONE", "+60", 0.8f, false, false);
            ui::macroBox(tall.r, *tall.fonts, 10, 40, 153, 114, "TONE", "+60", 0.8f, false, false);
        } else {
            ui::valueBox(usual.r, *usual.fonts, 10, 40, 153, 70, "CUTOFF", "1 KHZ", -1.f, false, false);
            ui::valueBox(tall.r, *tall.fonts, 10, 40, 153, 114, "CUTOFF", "1 KHZ", -1.f, false, false);
        }
        CHECK(usual.part(11, 41, 151, 20) == tall.part(11, 41, 151, 20));      // the name, where it was
        CHECK(usual.part(11, 61, 120, 20) == tall.part(11, 61 + 22, 120, 20)); // the value, 22 lower (left of the picture)
        CHECK(usual.part(11, 61, 120, 20) != tall.part(11, 61, 120, 20));
    }
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
