// LIVE OUTPUT on the DEVICE pages, read off the drawn screen (owner, 2026-11-14: "it could be
// smaller, and maybe we can utilize the screen better"). The scope is a thin strip across the whole
// width just above the hints bar, and the controls above it are spread down to it: no large empty
// band is left between the last row and the strip, and nothing runs into the strip.

#include <SDL.h>

#include <cstdio>
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
    {"phazed LINE 1", TrackKind::Phazed, 2, false}, {"phazed LINE 2", TrackKind::Phazed, 3, false},
    {"twin MAIN", TrackKind::Twin, 0, true}, {"twin OSCILLATORS", TrackKind::Twin, 1, true},
    {"twin CHARACTER", TrackKind::Twin, 2, false}, {"twin ENVELOPE", TrackKind::Twin, 3, false},
    {"qmod MAIN", TrackKind::Qmod, 0, true}, {"qmod SOUND", TrackKind::Qmod, 1, false},
    {"qmod CHARACTER", TrackKind::Qmod, 2, false}, {"qmod ENVELOPE", TrackKind::Qmod, 3, false},
    {"sampler", TrackKind::Sampler, 0, true}, {"kit", TrackKind::None, 0, true},
};

SynthScreen* synthScreen(Rig& rig, TrackKind kind) {
    return kind == TrackKind::Phazed ? &rig.app.phazed() : kind == TrackKind::Twin ? &rig.app.twin() : kind == TrackKind::Qmod ? &rig.app.qmod() : nullptr;
}

// Draws the page into the canvas, in a theme.
bool drawPage(Canvas& canvas, const Page& page, int theme) {
    Rig rig;
    rig.app.session().setTheme(theme);
    int track = 0;
    if (page.kind != TrackKind::None) {
        if (!rig.app.session().addTrack(page.kind)) return false;
        track = rig.project().trackCount() - 1;
    }
    rig.project().track = track;
    rig.app.session().view = View::Device;
    if (SynthScreen* screen = synthScreen(rig, page.kind)) screen->goTo(page.page, 0);
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
    for (int y = kAboveScope; y < top; ++y) check(rowIs(canvas, y, 0, kScreenW, kBg), "a clear row over it");
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
                if (empty < gap || empty > gap + 2) std::printf("  %s: its rows end %d rows over the strip\n", page.name, empty);
                CHECK(empty >= gap && empty <= gap + 2);
            }
        }
    }
}

// The rows themselves: the numbers the screens draw from, and where the pages put them.
void testTheRowsAreSpreadDown() {
    // MAIN: FOLDER and PRESET, two rows of macros, ADVANCED, ending where the strip leaves room.
    CHECK(SynthScreen::kAdvancedY + SynthScreen::kAdvancedH == ui::kAboveScope);
    CHECK(SynthScreen::kMacroH > 70 && SynthScreen::kAdvancedH > 40 && SynthScreen::kPresetH > 50); // all taller than they were
    // A page of settings: four rows fill it; fewer are never taller than a macro.
    CHECK(SynthScreen::kSettingY + 4 * SynthScreen::settingH(4) + 3 * SynthScreen::kSettingGap <= ui::kAboveScope);
    CHECK(SynthScreen::kSettingY + 4 * SynthScreen::settingH(4) + 3 * SynthScreen::kSettingGap > ui::kAboveScope - 4);
    CHECK(SynthScreen::kSettingY + 3 * SynthScreen::settingH(3) + 2 * SynthScreen::kSettingGap > ui::kAboveScope - 4);
    CHECK(SynthScreen::settingH(2) == SynthScreen::kMacroH && SynthScreen::settingH(1) == SynthScreen::kMacroH);
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
    // A page of two rows: its boxes are as tall as a macro, and end there.
    Canvas qmod;
    if (qmod.ok && drawPage(qmod, {"qmod SOUND", TrackKind::Qmod, 1, false}, kThemeBrown)) {
        const int end = SynthScreen::kSettingY + 2 * SynthScreen::kMacroH + SynthScreen::kSettingGap;
        CHECK(qmod.is(20, end - 1, ui::kLine) && qmod.is(20, end, ui::kBg));
    }
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
    if (failures == 0) std::printf("scope tests passed\n");
    return failures == 0 ? 0 : 1;
}
