// THE BAR UNDER A STRIP: while X is held on a drum hit, a note or a chord, the hints bar draws
// each button's chip directly under the field it changes, with no word, and the dice alone at
// the left. Read off the drawn screen (tests/canvas.hpp): each chip's left edge is its field's,
// on both grids, the slice view and the chord strip; and with X not held the bar is the plain
// one, pixel for pixel. The bar's own rules (ui::hintsUnder) are in tests/ui_test.cpp.

#include <SDL.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "chord_effects.hpp"
#include "chords.hpp"
#include "controls.hpp"
#include "note_marks.hpp"
#include "pattern_layout.hpp"
#include "samples.hpp"

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

const SDL_Keycode X = SDLK_s;
const int kBarTop = ui::kScreenH - ui::kHintsH, kChipTop = kBarTop + 5;
const int kRollStrip = 45 + 22 + 2 + 16 * 21 + 4; // the piano roll's strip with two octaves shown: under the keys (45, 22 tall, 2) and sixteen rows of 21, 4 down
using Chips = std::vector<std::pair<int, int>>; // (left edge, width)

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Canvas canvas, scratch;

    explicit Rig(const std::string& sampleFolder = "") {
        if (!sampleFolder.empty()) app.session().useSampleFolder(sampleFolder);
    }
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
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    Project& project() { return app.project(); }
    NotePattern& pattern() { return project().tracks[1].notes[0][0]; }
    // A synth track (track 2) with the piano roll showing.
    void roll() {
        CHECK(app.session().addTrack(TrackKind::Qmod));
        project().track = 1;
        app.session().view = View::Pattern;
        frames(2);
        CHECK(app.session().octavesShown() == 2);
    }
    void drums() {
        app.session().view = View::Pattern;
        frames(2);
    }
    // The screen as it is now.
    void draw() {
        frames(1);
        canvas.clear();
        app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
    }
    // The cream boxes in the hints bar.
    Chips chips() const {
        Chips found;
        for (int x = 0; x < ui::kScreenW;) {
            const int w = canvas.run(x, kChipTop, ui::kText);
            if (w > 0) found.push_back({x, w});
            x += w > 0 ? w : 1;
        }
        return found;
    }
    int chipW(const char* key) const { return static_cast<int>(ui::keyWidth(*canvas.fonts, key) + 8.f); }
    // Is this chip the bar's own, for this key? The same pixels as the plain bar draws for it.
    bool chipIs(int x, const char* key) {
        const ui::Hint one[] = {{key, ""}};
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, one, 1);
        return canvas.part(x, kBarTop, chipW(key) + 1, ui::kHintsH) == scratch.part(8, kBarTop, chipW(key) + 1, ui::kHintsH);
    }
    // The dice at the left: the bar's own Y and die, and nothing after them up to `to`.
    bool diceAlone(int to) {
        const ui::Hint dice[] = {{"Y", "dice"}};
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, dice, 1);
        return canvas.part(0, kBarTop, to, ui::kHintsH) == scratch.part(0, kBarTop, to, ui::kHintsH) && canvas.is(8 + chipW("Y") + 6, kBarTop + 6, ui::kDim);
    }
    // Nothing but the bar itself from x0 to x1: no word, no chip.
    bool bare(int x0, int x1) const {
        for (int y = kBarTop + 1; y < ui::kScreenH; ++y)
            for (int x = x0; x < x1; ++x)
                if (!canvas.is(x, y, ui::kPanel)) return false;
        return true;
    }
    // No words: outside the chips (and the die) the bar is bare, from the die to `to`.
    bool noWords(const Chips& list, int to) const {
        int x = 8 + chipW("Y") + 5 + 13;
        for (std::size_t i = 1; i < list.size(); ++i) {
            if (!bare(x, list[i].first)) return false;
            x = list[i].first + list[i].second;
        }
        return bare(x, to);
    }
    // How the help is opened, at the bar's end, exactly as the plain bar says it.
    int helpLeft() const { return static_cast<int>(ui::kScreenW - 6.f - canvas.fonts->small.width(ui::gHelpHint)) - 2; }
    bool helpShown() {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, nullptr, 0);
        const int x = helpLeft();
        return !bare(x, ui::kScreenW) && canvas.part(x, kBarTop, ui::kScreenW - x, ui::kHintsH) == scratch.part(x, kBarTop, ui::kScreenW - x, ui::kHintsH);
    }
    // The whole bar is the plain one for this list.
    bool plainBar(const ui::Hint* list, int count) {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, list, count);
        return canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH) == scratch.part(0, kBarTop, ui::kScreenW, ui::kHintsH);
    }
    // Is `text` drawn at (x, y) in the small lettering, in `colour`? It is drawn there on the
    // second screen, and wherever that has the full colour the screen looked at must have it too.
    bool label(float x, float y, const char* text, SDL_Color colour = ui::kDim) {
        const Font& font = scratch.fonts->small;
        const int x0 = static_cast<int>(x) - 2, x1 = static_cast<int>(x + font.width(text)) + 3, y0 = static_cast<int>(y) - 4, y1 = static_cast<int>(y) + 20;
        ui::fill(scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255}); // a ground no lettering is drawn in
        font.draw(scratch.r, x, y, text, colour);
        int body = 0;
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                if (!scratch.is(px, py, colour)) continue;
                ++body;
                if (!canvas.is(px, py, colour)) return false;
            }
        return body >= 6;
    }
};

// A folder of its own for a test's recording, taken away with what the test put in it when the test ends.
struct TempFolder {
    std::string path;
    bool made = false;
    TempFolder() {
        char pattern[] = "/tmp/apotheca-under-XXXXXX";
        const char* got = mkdtemp(pattern);
        made = got != nullptr;
        path = made ? got : "/tmp";
    }
    ~TempFolder() {
        if (!made) return;
        std::remove((path + "/break.wav").c_str());
        rmdir(path.c_str());
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

// ---- the drum grid ----------------------------------------------------------------------------

// What the bar says on a hit with nothing held (pattern_screen.cpp's last list).
const ui::Hint kOnAHit[] = {{"A", "level / nudge"}, {"Y", "chance / start"}, {"HOLD X", "effects"}, {"HOLD B", "delete"}, {"HOLD R1", "paint"}, {"R1 + R2", "paint random"}};

void heldOnAHit(Rig& rig) {
    using namespace pattern_layout;
    rig.draw();
    const Chips chips = rig.chips();
    // Each chip's left edge is its field's: PITCH, RATCHET, REVERSE, CUT, THROW; the dice at the left.
    CHECK((chips == Chips{{8, rig.chipW("Y")}, {190, rig.chipW("UP / DOWN")}, {260, rig.chipW("LEFT / RIGHT")}, {355, rig.chipW("L1")}, {450, rig.chipW("R1")}, {555, rig.chipW("A")}}));
    CHECK(rig.label(kFxPitchX, kStripY, "PITCH") && rig.label(kFxRatchetX, kStripY, "RATCHET") && rig.label(kFxReverseX, kStripY, "REVERSE"));
    CHECK(rig.label(kFxCutX, kStripY, "CUT") && rig.label(kFxThrowX, kStripY, "THROW"));
    CHECK(!rig.label(kFxPitchX + 1.f, kStripY, "PITCH") && !rig.label(kFxPitchX, kStripY, "RATCHET")); // (the check can tell)
    CHECK(rig.chipIs(static_cast<int>(kFxPitchX), "UP / DOWN") && rig.chipIs(static_cast<int>(kFxRatchetX), "LEFT / RIGHT"));
    CHECK(rig.chipIs(static_cast<int>(kFxReverseX), "L1") && rig.chipIs(static_cast<int>(kFxCutX), "R1") && rig.chipIs(static_cast<int>(kFxThrowX), "A"));
    CHECK(rig.diceAlone(static_cast<int>(kFxPitchX)));
    // No words, and THROW's chip stands where the help would be said: it gives way, MENU or none.
    CHECK(rig.noWords(chips, ui::kScreenW));
    CHECK(static_cast<int>(kFxThrowX) + rig.chipW("A") > rig.helpLeft());
}

void testTheDrumGrid() {
    Rig rig;
    CHECK(rig.canvas.ok && rig.scratch.ok);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    rig.drums();
    PatternScreen& drums = rig.app.drums();
    drums.setStep(1, 4, 0.8f);
    drums.moveCursorTo(1, 4);
    // Nothing held: the plain bar, pixel for pixel.
    rig.draw();
    CHECK(rig.plainBar(kOnAHit, 6));
    CHECK(rig.label(190.f, pattern_layout::kStripY, "LEVEL") && !rig.label(pattern_layout::kFxPitchX, pattern_layout::kStripY, "PITCH"));
    // X held: a chip under each field.
    rig.key(X, true);
    CHECK(drums.effecting());
    heldOnAHit(rig);
    // The same at the fields' widest: every effect on, thrown to both.
    StepFx fx;
    fx.pitch = -12, fx.ratchet = 4, fx.reverse = true, fx.cut = 2, fx.throwTo = 3;
    drums.setFx(1, 4, fx);
    heldOnAHit(rig);
    // Let go: the plain bar again.
    rig.key(X, false);
    rig.draw();
    CHECK(!drums.effecting() && rig.plainBar(kOnAHit, 6));
    // X held where there is no hit: nothing to set, and the bar is the plain one for an empty step.
    drums.moveCursorTo(1, 5);
    rig.key(X, true);
    rig.draw();
    const ui::Hint empty[] = {{"A", "place"}, {"Y", "maybe"}, {"HOLD B", "delete"}, {"HOLD R1", "paint"}, {"R1 + R2", "paint random"}};
    CHECK(!drums.effecting() && rig.plainBar(empty, 5));
    rig.key(X, false);

    // A handheld with no MENU button: the same chips.
    static ControlMap none; // nothing learned: the keyboard stand-ins, and no MENU
    rig.input.useMap(&none);
    drums.moveCursorTo(1, 4);
    rig.key(X, true);
    rig.draw();
    CHECK(std::strcmp(ui::gHelpHint, "HLD SEL: HELP") == 0);
    heldOnAHit(rig);
    rig.key(X, false);
    rig.draw();
    CHECK(rig.plainBar(kOnAHit, 6));
    rig.input.useMap(nullptr);
    rig.frames(1);
    CHECK(std::strcmp(ui::gHelpHint, "MENU: HELP") == 0);
}

// The slice view is the drum grid in another mode: the same strip, the same chips.
void testTheSliceView() {
    const TempFolder temp; // (made before the rig, so it goes after it)
    const std::string& folder = temp.path;
    CHECK(writeWav(folder + "/break.wav", std::vector<float>(48000, 0.25f), 48000));
    Rig rig(folder);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    rig.drums();
    PatternScreen& drums = rig.app.drums();
    rig.app.session().setLaneSample(0, 2, "break");
    drums.setStep(2, 4, 0.8f);
    drums.moveCursorTo(2, 4);
    drums.setSlicing(true);
    CHECK(drums.slicing());
    rig.draw();
    const ui::Hint onASlice[] = {{"UP / DOWN", "slice"}, {"A", "level / nudge"}, {"Y", "chance"}, {"HOLD X", "effects"}, {"HOLD R1", "in order"}, {"L1", "grid"}};
    CHECK(rig.plainBar(onASlice, 6));
    rig.key(X, true);
    CHECK(drums.effecting() && drums.slicing());
    heldOnAHit(rig);
    StepFx fx;
    fx.throwTo = kThrowSpace, fx.ratchet = 2;
    drums.setFx(2, 4, fx);
    heldOnAHit(rig);
    rig.key(X, false);
    rig.draw();
    CHECK(rig.plainBar(onASlice, 6));
}

// ---- the piano roll: a note --------------------------------------------------------------------

void heldOnANote(Rig& rig) {
    rig.draw();
    const Chips chips = rig.chips();
    const float labelY = kRollStrip + 8.f;
    // RATCHET, GLIDE, THROW and GLIDE FROM (the track's): a chip under each. NUDGE, which nothing
    // sets here, is not on the strip while X is held.
    CHECK((chips == Chips{{8, rig.chipW("Y")}, {160, rig.chipW("LEFT / RIGHT")}, {250, rig.chipW("UP / DOWN")}, {385, rig.chipW("A")}, {500, rig.chipW("L1 / R1")}}));
    CHECK(rig.label(kStripHeldRatchetX, labelY, "RATCHET") && rig.label(kStripHeldGlideX, labelY, "GLIDE") && rig.label(kStripHeldThrowX, labelY, "THROW") && rig.label(kStripHeldFromX, labelY, "GLIDE FROM"));
    CHECK(!rig.label(kStripHeldRatchetX - 1.f, labelY, "RATCHET") && !rig.label(kStripHeldFromX - 1.f, labelY, "GLIDE FROM"));
    CHECK(!rig.label(kStripHeldFromX, labelY, "NUDGE") && !rig.label(kStripNudgeX, labelY, "NUDGE"));
    CHECK(rig.chipIs(static_cast<int>(kStripHeldRatchetX), "LEFT / RIGHT") && rig.chipIs(static_cast<int>(kStripHeldGlideX), "UP / DOWN") && rig.chipIs(static_cast<int>(kStripHeldThrowX), "A"));
    CHECK(rig.chipIs(static_cast<int>(kStripHeldFromX), "L1 / R1"));
    CHECK(rig.diceAlone(static_cast<int>(kStripHeldRatchetX)));
    // No words, and GLIDE FROM's chip stands where the help would be said: it gives way, MENU or none.
    CHECK(rig.noWords(chips, ui::kScreenW));
    CHECK(static_cast<int>(kStripHeldFromX) + rig.chipW("L1 / R1") + 11 > rig.helpLeft() + 2);
}

// GLIDE FROM's value on the strip, X held: how much of it is drawn in a colour (the large
// lettering, under the label, from the field's left edge to the strip's right).
int fromInked(const Rig& rig, SDL_Color colour) {
    int count = 0;
    for (int y = kRollStrip + 22; y < kRollStrip + 50; ++y)
        for (int x = static_cast<int>(kStripHeldFromX) - 1; x < static_cast<int>(kStripRight) + 1; ++x) count += rig.canvas.is(x, y, colour) ? 1 : 0;
    return count;
}
// ...and is it this word, in this colour, in the large lettering?
bool fromReads(Rig& rig, const char* word, SDL_Color colour) {
    const Font& font = rig.scratch.fonts->large;
    const int x0 = static_cast<int>(kStripHeldFromX) - 1, x1 = static_cast<int>(kStripRight) + 1, y0 = kRollStrip + 22, y1 = kRollStrip + 50;
    ui::fill(rig.scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255});
    font.draw(rig.scratch.r, kStripHeldFromX, kRollStrip + 26.f, word, colour);
    int body = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            if (rig.scratch.is(x, y, colour) != rig.canvas.is(x, y, colour)) return false; // the same lettering, and no more of it
            body += rig.scratch.is(x, y, colour) ? 1 : 0;
        }
    return body > 30;
}

void testANote() {
    Rig rig;
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    rig.roll();
    NoteScreen& roll = rig.app.notes();
    NoteEvent& note = rig.pattern().notes[0];
    note = NoteEvent{4, 62, 2.f, 0.f, 0.8f};
    rig.app.session().sendNoteBar(1, 0, 0);
    roll.moveCursorTo(62, 4);
    const ui::Hint onANote[] = {{"A", "level / length"}, {"B", "delete"}, {"HOLD Y", "chord"}, {"HOLD R1", "move"}, {"HOLD X", "effects"}, {"L1", "zoom"}};
    rig.draw();
    CHECK(rig.plainBar(onANote, 6));
    CHECK(rig.label(160.f, kRollStrip + 8.f, "LEVEL") && !rig.label(kStripHeldRatchetX, kRollStrip + 8.f, "RATCHET"));
    rig.key(X, true);
    CHECK(roll.effecting());
    heldOnANote(rig);
    // GLIDE FROM is the track's: TOP until it is turned, and dim while this note has no glide.
    CHECK(fromReads(rig, "TOP", ui::kDim) && fromInked(rig, ui::kText) == 0 && fromInked(rig, ui::kBrass) == 0);
    // At its fullest: a ratchet, the longest glide to read, thrown, nudged.
    note.ratchet = 4, note.glide = kGlideQuarter, note.throwTo = kThrowSpace, note.nudge = -0.45f;
    heldOnANote(rig);
    CHECK(fromReads(rig, "TOP", ui::kText) && fromInked(rig, ui::kBrass) == 0); // the note glides: it is said plainly
    // R1 is the next of the four, L1 the one before; each stops at its end. Brass when it is not TOP.
    const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w;
    const int zoom = rig.app.session().octavesShown();
    const char* const names[4] = {"TOP", "PAIRED", "NEAREST", "RANDOM"};
    for (int press = 1; press <= 5; ++press) {
        rig.key(R1, true);
        rig.key(R1, false);
        const int want = press < 3 ? press : 3;
        CHECK(rig.project().tracks[1].glideFrom == want && roll.effecting() && !roll.carrying());
        heldOnANote(rig);
        CHECK(fromReads(rig, names[want], ui::kBrass) && fromInked(rig, ui::kText) == 0);
    }
    note.glide = kGlideOff; // no glide on this note: the track's choice is still read, dimly
    heldOnANote(rig);
    CHECK(fromReads(rig, "RANDOM", ui::kDim) && fromInked(rig, ui::kBrass) == 0);
    note.glide = kGlideQuarter;
    for (int press = 1; press <= 5; ++press) {
        rig.key(L1, true);
        rig.key(L1, false);
        const int want = press < 3 ? 3 - press : 0;
        CHECK(rig.project().tracks[1].glideFrom == want && roll.effecting());
        heldOnANote(rig);
        CHECK(fromReads(rig, names[want], want ? ui::kBrass : ui::kText));
    }
    CHECK(rig.app.session().octavesShown() == zoom); // (L1 did not zoom, R1 did not carry: they wait for X to be let go)
    CHECK(note.step == 4 && note.pitch == 62 && note.ratchet == 4 && note.glide == kGlideQuarter && note.throwTo == kThrowSpace); // ...and nothing of the note changed
    rig.key(R1, true);
    rig.key(R1, false);
    CHECK(rig.project().tracks[1].glideFrom == kFromPaired);
    // The dice are the note's: they leave the track's GLIDE FROM alone.
    for (int i = 0; i < 12; ++i) {
        rig.key(SDLK_a, true);
        rig.key(SDLK_a, false);
    }
    CHECK(rig.project().tracks[1].glideFrom == kFromPaired && roll.effecting());
    note.ratchet = 4, note.glide = kGlideQuarter;
    rig.draw();
    // A handheld with no MENU button: the bar's last words are longer, and still have their room.
    static ControlMap none;
    rig.input.useMap(&none);
    rig.frames(1);
    CHECK(std::strcmp(ui::gHelpHint, "HLD SEL: HELP") == 0);
    heldOnANote(rig);
    rig.input.useMap(nullptr);
    rig.frames(1);
    CHECK(std::strcmp(ui::gHelpHint, "MENU: HELP") == 0);
    // Let go: the plain bar again, and NUDGE where it stands with nothing held.
    rig.key(X, false);
    rig.draw();
    CHECK(!roll.effecting() && rig.plainBar(onANote, 6));
    CHECK(rig.label(kStripNudgeX, kRollStrip + 8.f, "NUDGE") && !rig.label(kStripHeldFromX, kRollStrip + 8.f, "GLIDE FROM"));
    // With X let go, L1 zooms and R1 carries, as ever, and GLIDE FROM stays.
    rig.key(SDLK_q, true);
    rig.key(SDLK_q, false);
    CHECK(rig.app.session().octavesShown() != 2 && rig.project().tracks[1].glideFrom == kFromPaired);
    rig.key(SDLK_w, true);
    CHECK(roll.carrying() && rig.project().tracks[1].glideFrom == kFromPaired);
    rig.key(SDLK_w, false);
    // L1 already down when X takes hold of the note, and let go under it: that is not a tap of the zoom.
    const int zoomNow = rig.app.session().octavesShown();
    rig.key(SDLK_q, true);
    rig.key(X, true);
    CHECK(roll.effecting());
    rig.key(SDLK_q, false);
    rig.frames(1);
    CHECK(rig.app.session().octavesShown() == zoomNow && rig.project().tracks[1].glideFrom == kFromPaired);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.app.session().octavesShown() == zoomNow);
}

// ---- the piano roll: a chord -------------------------------------------------------------------

void testAChord() {
    Rig rig;
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    rig.roll();
    NoteScreen& roll = rig.app.notes();
    ChordEvent& chord = rig.pattern().chords[0];
    chord = makeChord(builtInPack().chords[0], 60, 2, 4.f, 0.7f, 0);
    rig.app.session().sendNoteBar(1, 0, 0);
    roll.moveCursorTo(60, 2);
    const ui::Hint onAChord[] = {{"A", "level / length"}, {"B", "delete"}, {"HOLD Y", "swap"}, {"HOLD R1", "move"}, {"HOLD X", "effects"}, {"L1", "zoom"}};
    rig.draw();
    CHECK(rig.plainBar(onAChord, 6));
    rig.key(X, true);
    CHECK(roll.effecting());
    const auto held = [&] {
        rig.draw();
        const Chips chips = rig.chips();
        // LEFT / RIGHT sets the one picked: under its large value at the left. UP / DOWN picks
        // one of the six: under their names. A under THROW. The dice at the right-hand end.
        const int diceX = static_cast<int>(ui::hintDiceEndX(*rig.canvas.fonts, "Y"));
        CHECK((chips == Chips{{8, rig.chipW("LEFT / RIGHT")}, {160, rig.chipW("UP / DOWN")}, {522, rig.chipW("A")}, {diceX, rig.chipW("Y")}}));
        CHECK(kChordFxSetX == kChordFxPickedX);
        CHECK(rig.chipIs(static_cast<int>(kChordFxListX), "UP / DOWN") && rig.chipIs(static_cast<int>(kChordFxSetX), "LEFT / RIGHT") && rig.chipIs(static_cast<int>(kChordFxThrowX), "A"));
        CHECK(rig.chipIs(diceX, "Y") && diceX > static_cast<int>(kChordFxThrowX) + rig.chipW("A") + 6);
        CHECK(rig.canvas.is(diceX + rig.chipW("Y") + 6, kBarTop + 6, ui::kDim) && diceX + rig.chipW("Y") + 5 + 13 <= ui::kScreenW - 6); // the die, inside the bar
        CHECK(rig.label(kChordFxThrowX, kRollStrip + 8.f, "THROW"));
        CHECK(rig.label(kChordFxPickedX, kRollStrip + 8.f, chordEffectName(roll.effectPicked())) && rig.label(8.f, kRollStrip + 8.f, chordEffectName(roll.effectPicked())));
        // No words: bare between the chips, and after the die. (The help gives way to the dice.)
        for (std::size_t i = 0; i + 1 < chips.size(); ++i) CHECK(rig.bare(chips[i].first + chips[i].second, chips[i + 1].first));
        CHECK(rig.bare(0, 8) && rig.bare(diceX + rig.chipW("Y") + 5 + 13, ui::kScreenW));
        CHECK(static_cast<int>(kChordFxThrowX) + rig.chipW("A") + 11 > rig.helpLeft() + 2);
    };
    // The list's first name and its value stand where the two chips do.
    roll.pickEffect(1);
    held();
    char value[32];
    chordEffectValue(chord, 0, value, sizeof value);
    CHECK(rig.label(kChordFxListX, static_cast<float>(kRollStrip + kChordFxRowTop), chordEffectName(0)));
    CHECK(rig.label(kChordFxListX + kChordFxValueIn[0], static_cast<float>(kRollStrip + kChordFxRowTop), value));
    // Whichever is picked, and at its fullest, the chips stay where they are.
    chord.play = chordPlayOf(kStrumWide, kOrderUpDown, 100, 100, 100);
    chord.ratchet = 4, chord.throwTo = 3;
    for (int effect = 0; effect < kChordEffects; ++effect) {
        roll.pickEffect(effect);
        held();
    }
    static ControlMap none;
    rig.input.useMap(&none);
    rig.frames(1);
    held();
    rig.input.useMap(nullptr);
    rig.frames(1);
    // A chord does not glide: L1 and R1 with X held on one do not turn the track's GLIDE FROM
    // (they are as they were there: R1 takes hold of the chord).
    rig.key(SDLK_w, true);
    CHECK(rig.project().tracks[1].glideFrom == kFromTop && roll.carrying());
    rig.key(SDLK_w, false);
    rig.key(SDLK_q, true);
    rig.key(SDLK_q, false);
    CHECK(rig.project().tracks[1].glideFrom == kFromTop);
    CHECK(!rig.label(kStripHeldFromX, kRollStrip + 8.f, "GLIDE FROM"));
    rig.key(X, false);
    rig.draw();
    CHECK(!roll.effecting() && rig.plainBar(onAChord, 6));
}

} // namespace

int main(int, char**) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    testTheDrumGrid();
    testTheSliceView();
    testANote();
    testAChord();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("the bar under a strip: tests passed\n");
    return 0;
}
