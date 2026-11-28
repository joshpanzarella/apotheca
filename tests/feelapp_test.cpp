// FEEL on the drum grid, in the app: the gesture (X held on a lane's name), the strip and the
// chips under it read off the drawn screen, the mark by a name, the dice, the copy to every
// lane, the song file, undo, a song sent again, a bounce, and X still hearing the drum.
// The engine's side (the rule, the rolls, the timing) is tests/feel_test.cpp.
//
// What the engine was told is read from what it plays: lane 3 holds a recording that is one
// sample and then silence, so each hit is one sample in the output, and where it is says
// whether the lane's feel reached the engine.

#include <SDL.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/feel_rule.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "controls.hpp"
#include "lane_feel.hpp"
#include "pattern_layout.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "temp_folder.hpp"

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

const SDL_Keycode X = SDLK_s, A = SDLK_x, Y = SDLK_a;
const int kBarTop = ui::kScreenH - ui::kHintsH, kChipTop = kBarTop + 5;
const int kClickLane = 2;
const int kStep = 6000, kBar = 16 * kStep; // 120 BPM at 48 kHz
using Chips = std::vector<std::pair<int, int>>; // (left edge, width)

struct TempFolder {
    std::string path;
    bool made = false;
    TempFolder() {
        path = tempFolder("feel");
        made = true;
        std::vector<float> click(480, 0.f);
        click[0] = 0.9f;
        CHECK(writeWav(path + "/click.wav", click, 48000));
        CHECK(writeWav(path + "/break.wav", std::vector<float>(48000, 0.25f), 48000));
    }
    ~TempFolder() {
        if (!made) return;
        std::remove((path + "/click.wav").c_str());
        std::remove((path + "/break.wav").c_str());
        rmdir(path.c_str());
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Canvas canvas, scratch;

    explicit Rig(const std::string& sampleFolder = "") {
        if (!sampleFolder.empty()) app.session().useSampleFolder(sampleFolder);
        app.session().view = View::Pattern;
        frames(2);
        CHECK(app.project().tempo == 120.f);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    // The stick: x right, y down (-1..1, before the dead zone; all the way is 1), or let go (0, 0).
    void stick(float x, float y) {
        for (int axis = 0; axis < 2; ++axis) {
            SDL_Event event = {};
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.axis = static_cast<Uint8>(axis == 0 ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_LEFTY);
            event.caxis.value = static_cast<Sint16>((axis == 0 ? x : y) * 32767.f);
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    void settle() { frames(50); } // the burst of edits is over: a step of undo
    Project& project() { return app.project(); }
    TrackData& kit() { return project().tracks[0]; }
    PatternScreen& drums() { return app.drums(); }
    // The click on its lane, a hit on every step.
    void clicks() {
        app.session().setLaneSample(0, kClickLane, "click");
        for (int s = 0; s < kSteps; ++s) drums().setStep(kClickLane, s, 0.8f);
        frames(4);
    }
    // Play from the top and say where the hits of the first `bars` bars came.
    std::vector<long> played(int bars = 2) {
        frames(30); // (whatever was heard by hand has rung out)
        app.session().togglePlay(false);
        std::vector<long> at;
        std::vector<float> out(2 * 256);
        const long total = static_cast<long>(bars) * kBar + kStep / 2;
        for (long done = 0; done < total; done += 256) {
            engine->render(out.data(), 256);
            for (int i = 0; i < 256; ++i)
                if (out[static_cast<std::size_t>(2 * i)] != 0.f && done + i < total - kStep) at.push_back(done + i);
        }
        app.session().togglePlay(false);
        frames(4);
        return at;
    }
    // Where the rule puts them, for the feel the project holds.
    std::vector<long> ruled(int bars = 2) {
        std::vector<long> at;
        const LaneFeel feel = kit().laneFeel[kClickLane];
        for (long n = 0; n < bars * kSteps; ++n) {
            const int step = static_cast<int>(n % kSteps);
            const double moved = std::ceil(feelSamples(feel.time, step, feelSeed(0, kClickLane, 0, step, n), 48.0, kStep));
            const long place = n * kStep + static_cast<long>(moved);
            at.push_back(n == 0 && place < 0 ? 0 : place);
        }
        return at;
    }
    // Where SWING puts them, for the swing the project holds (no feel).
    std::vector<long> swung(int bars = 2) {
        std::vector<long> at;
        for (long n = 0; n < bars * kSteps; ++n) at.push_back(n * kStep + static_cast<long>(std::ceil(swingSamples(kit().laneSwing[kClickLane], static_cast<int>(n % kSteps), kStep))));
        return at;
    }
    // The swing mark by a lane's name: its long piece and its short one.
    bool markSwing(int lane) const {
        using namespace pattern_layout;
        const int y = kGridTop + lane * kLaneH + kSwingMarkInset + 1;
        return canvas.is(kSwingMarkRight - 1, y, ui::kPurple) && canvas.is(kSwingMarkRight - kSwingMarkShort - kSwingMarkGap - kSwingMarkLong + 1, y, ui::kPurple) &&
               !canvas.is(kSwingMarkRight - kSwingMarkShort - 1, y, ui::kPurple); // (the gap between them)
    }
    bool anySwingMark(int lane) const {
        using namespace pattern_layout;
        const int y = kGridTop + lane * kLaneH + kSwingMarkInset + 1;
        for (int x = kSwingMarkRight - kSwingMarkShort - kSwingMarkGap - kSwingMarkLong; x < kSwingMarkRight; ++x)
            if (canvas.is(x, y, ui::kPurple)) return true;
        return false;
    }
    std::vector<long> grid(int bars = 2) {
        std::vector<long> at;
        for (long n = 0; n < bars * kSteps; ++n) at.push_back(n * kStep);
        return at;
    }
    void draw() {
        frames(1);
        canvas.clear();
        app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
    }
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
    bool chipIs(int x, const char* key) {
        const ui::Hint one[] = {{key, ""}};
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, one, 1);
        return canvas.part(x, kBarTop, chipW(key) + 1, ui::kHintsH) == scratch.part(8, kBarTop, chipW(key) + 1, ui::kHintsH);
    }
    bool diceAlone(int to) {
        const ui::Hint dice[] = {{"Y", "dice"}};
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, dice, 1);
        return canvas.part(0, kBarTop, to, ui::kHintsH) == scratch.part(0, kBarTop, to, ui::kHintsH) && canvas.is(8 + chipW("Y") + 6, kBarTop + 6, ui::kDim);
    }
    bool plainBar(const ui::Hint* list, int count) {
        scratch.clear();
        ui::hints(scratch.r, *scratch.fonts, list, count);
        return canvas.part(0, kBarTop, ui::kScreenW, ui::kHintsH) == scratch.part(0, kBarTop, ui::kScreenW, ui::kHintsH);
    }
    // Is `text` drawn at (x, y) in this lettering and colour? (As under_test's: drawn on the second
    // screen, and wherever that has the full colour the screen looked at must have it too.)
    bool inked(const Font& font, float x, float y, const char* text, SDL_Color colour) {
        const int x0 = static_cast<int>(x) - 2, x1 = static_cast<int>(x + font.width(text)) + 3, y0 = static_cast<int>(y) - 4, y1 = static_cast<int>(y) + 24;
        ui::fill(scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255});
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
    bool label(float x, const char* text) { return inked(scratch.fonts->small, x, static_cast<float>(pattern_layout::kStripY), text, ui::kDim); }
    bool value(float x, const char* text, SDL_Color colour) { return inked(scratch.fonts->large, x, pattern_layout::kStripY + 16.f, text, colour); }
    // The mark by a lane's name: its two pieces.
    bool markTime(int lane) const { return canvas.is(pattern_layout::kFeelMarkX + 1, pattern_layout::kGridTop + lane * pattern_layout::kLaneH + pattern_layout::kFeelMarkInset + 2, ui::kPurple); }
    bool markLevel(int lane) const {
        return canvas.is(pattern_layout::kFeelMarkX + 1, pattern_layout::kGridTop + lane * pattern_layout::kLaneH + pattern_layout::kCellH - pattern_layout::kFeelMarkInset - 3, ui::kPurple);
    }
};

// ---- the gesture ------------------------------------------------------------------------------

void testTheGesture() {
    const TempFolder temp;
    Rig rig(temp.path);
    PatternScreen& drums = rig.drums();
    rig.clicks();
    drums.moveCursorTo(kClickLane, -1);
    CHECK(rig.played() == rig.grid()); // a new kit plays plain

    // X held on the lane's name: right is HUMAN TIME up a click, through the six amounts, and it stops.
    rig.key(X, true);
    rig.frames(1);
    CHECK(drums.feeling() && !drums.effecting());
    const int amounts[6] = {0, 10, 25, 50, 75, 100};
    for (int i = 1; i <= 7; ++i) {
        rig.tap(SDLK_RIGHT);
        CHECK(rig.kit().laneFeel[kClickLane].time == amounts[i > 5 ? 5 : i]);
        CHECK(rig.kit().laneFeel[kClickLane].level == 0);
    }
    // Up is HUMAN LEVEL; down takes it back; each stops at its end.
    for (int i = 1; i <= 7; ++i) {
        rig.tap(SDLK_UP);
        CHECK(rig.kit().laneFeel[kClickLane].level == amounts[i > 5 ? 5 : i] && rig.kit().laneFeel[kClickLane].time == 100);
    }
    rig.tap(SDLK_DOWN, 2);
    CHECK(rig.kit().laneFeel[kClickLane].level == 50);
    rig.tap(SDLK_DOWN, 9);
    CHECK(rig.kit().laneFeel[kClickLane].level == 0);
    rig.tap(SDLK_LEFT);
    CHECK(rig.kit().laneFeel[kClickLane].time == 75);
    // The cursor has not moved, no hit was touched, no other lane was.
    CHECK(drums.cursorLane() == kClickLane && drums.cursorStep() == -1);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        if (lane != kClickLane) CHECK(rig.kit().laneFeel[lane].atRest());
    for (int s = 0; s < kSteps; ++s) CHECK(drums.level(kClickLane, s) == 0.8f && drums.nudge(kClickLane, s) == 0.f);
    rig.key(X, false);
    rig.frames(1);
    CHECK(!drums.feeling());
    // The engine was told: the hits come where the rule puts them for 75%.
    const std::vector<long> felt = rig.played();
    CHECK(felt == rig.ruled() && felt != rig.grid());
    // Let go, the directions are the cursor's again.
    rig.tap(SDLK_DOWN);
    CHECK(drums.cursorLane() == kClickLane + 1 && rig.kit().laneFeel[kClickLane].time == 75);
    rig.tap(SDLK_UP);
    // Back to OFF: plain again.
    rig.key(X, true);
    rig.tap(SDLK_LEFT, 8);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.kit().laneFeel[kClickLane].atRest());
    CHECK(rig.played() == rig.grid());

    // X on a hit is still the step effects' hold, not the feel's; on the tempo, neither.
    drums.moveCursorTo(kClickLane, 3);
    rig.key(X, true);
    rig.frames(1);
    CHECK(drums.effecting() && !drums.feeling());
    rig.tap(SDLK_RIGHT);
    CHECK(drums.fx(kClickLane, 3).ratchet == 2 && rig.kit().laneFeel[kClickLane].atRest());
    rig.key(X, false);
    rig.frames(1);
    // The cursor carried off the name with X held (it cannot be: the directions are the feel's), and
    // X held with A already down: no hold.
    drums.moveCursorTo(kClickLane, -1);
    rig.key(A, true);
    rig.key(X, true);
    rig.frames(1);
    CHECK(!drums.feeling());
    rig.key(X, false);
    rig.key(A, false);
    rig.frames(1);

    // The cursor taken off the name by something else while X is held (another screen's doing): the hold is over.
    rig.key(X, true);
    rig.frames(1);
    CHECK(drums.feeling());
    drums.moveCursorTo(kClickLane, 5);
    rig.frames(1);
    CHECK(!drums.feeling());
    rig.key(X, false);
    rig.frames(1);
    drums.moveCursorTo(kClickLane, -1);

    // The D-pad held runs through the clicks; so does the stick... (the D-pad here)
    rig.key(X, true);
    rig.key(SDLK_RIGHT, true);
    rig.frames(120); // two seconds: well past six clicks
    rig.key(SDLK_RIGHT, false);
    CHECK(rig.kit().laneFeel[kClickLane].time == 100);
    rig.key(X, false);
    rig.frames(1);

    // The stick: a click at once, then twice the held D-pad's pace for clicks while it is held over, sideways and
    // up / down alike, however far it is pushed (owner, 2026-11-25: ui::stickPerSecond(Pace::Click), sixteen a
    // second times STICK SPEED's 75%: twelve. It was the D-pad's eight, six at 75%; before G32 the ratchet's four).
    CHECK(ui::stickPerSecond(ui::Pace::Click) == 16.f && ui::gStickSpeed == 0.75f);
    rig.app.session().setLaneFeel(0, kClickLane, 0, 0);
    rig.key(X, true);
    rig.frames(1);
    rig.stick(0, -1); // up
    rig.frames(14);   // 0.22 s: the first click and two more (at the old six a second: one)
    CHECK(rig.kit().laneFeel[kClickLane].level == 50 && rig.kit().laneFeel[kClickLane].time == 0);
    rig.stick(0, 0);
    rig.frames(2);
    rig.stick(0.3f, 0); // right, just past the dead zone: as fast as all the way
    rig.frames(14);
    CHECK(rig.kit().laneFeel[kClickLane].time == 50 && rig.kit().laneFeel[kClickLane].level == 50);
    rig.frames(90);  // 1.92 s in all: well past six clicks, and it stops at the end
    CHECK(rig.kit().laneFeel[kClickLane].time == 100);
    rig.stick(0, 0);
    rig.frames(2);
    rig.stick(0, 1); // down: one click back
    rig.frames(3);
    CHECK(rig.kit().laneFeel[kClickLane].level == 25);
    rig.stick(0, 0);
    rig.key(X, false);
    rig.frames(2);
    CHECK(drums.cursorLane() == kClickLane && drums.cursorStep() == -1); // the stick did not move the cursor
}

// A or Y pressed for the feel (the copy, the dice) does nothing of its own when X is let go first
// and it stays down: no lane volume turned, no hits rolled. Pressed again, it is itself.
void testXLetGoFirst() {
    const TempFolder temp;
    Rig rig(temp.path);
    PatternScreen& drums = rig.drums();
    rig.clicks();
    drums.moveCursorTo(kClickLane, -1);
    rig.key(X, true);
    rig.frames(1);
    rig.tap(SDLK_RIGHT, 2);
    rig.key(A, true); // the copy
    rig.frames(1);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneFeel[lane] == laneFeelOf(25, 0));
    rig.key(X, false);
    rig.frames(2);
    CHECK(!drums.feeling());
    const float volume = rig.kit().laneLevel[kClickLane];
    const std::string before = songText(rig.project());
    rig.tap(SDLK_LEFT, 2);
    rig.tap(SDLK_DOWN, 2);
    CHECK(rig.kit().laneLevel[kClickLane] == volume);
    CHECK(songText(rig.project()) == before); // nothing in the song was turned
    rig.key(A, false);
    rig.frames(2);
    // A pressed again is A: held on the name, DOWN turns the lane down.
    drums.moveCursorTo(kClickLane, -1);
    rig.frames(30);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(SDLK_DOWN, 2);
    rig.key(A, false);
    rig.frames(2);
    CHECK(rig.kit().laneLevel[kClickLane] < volume);

    // Y: the dice for the feel, X let go, Y still down: the directions do not roll the lane's hits.
    drums.moveCursorTo(kClickLane, -1);
    rig.frames(30);
    rig.key(X, true);
    rig.frames(1);
    rig.key(Y, true);
    rig.frames(1);
    CHECK(rig.kit().laneFeel[kClickLane] != laneFeelOf(25, 0));
    rig.key(X, false);
    rig.frames(2);
    const std::string rolled = songText(rig.project());
    rig.tap(SDLK_LEFT, 2);
    rig.tap(SDLK_RIGHT, 1);
    CHECK(songText(rig.project()) == rolled);
    rig.key(Y, false);
    rig.frames(2);
    CHECK(songText(rig.project()) == rolled); // and Y coming up is not a tap
    // Y pressed again is Y: held on the name, RIGHT rolls the lane's starts.
    drums.moveCursorTo(kClickLane, -1);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(SDLK_RIGHT, 1); // (rolls where the lane's hits start in the recording)
    CHECK(drums.cursorStep() == -1 && songText(rig.project()) != rolled); // Y's own job, not the cursor's
    rig.key(Y, false);
    rig.frames(2);

    // The same on a hit: A pressed for THROW with X held, X let go first: A does not nudge the hit.
    drums.moveCursorTo(kClickLane, 3);
    rig.frames(30);
    rig.key(X, true);
    rig.frames(1);
    rig.key(A, true);
    rig.frames(1);
    CHECK(drums.fx(kClickLane, 3).throwTo == 1);
    rig.key(X, false);
    rig.frames(2);
    const float level = drums.level(kClickLane, 3);
    rig.tap(SDLK_RIGHT, 2);
    rig.tap(SDLK_UP, 2);
    CHECK(drums.nudge(kClickLane, 3) == 0.f && drums.level(kClickLane, 3) == level);
    rig.key(A, false);
    rig.frames(2);
}

// The feel turned by hand while the song plays: every step still has its one hit, near its place.
void testTurnedWhileItPlays() {
    const TempFolder temp;
    Rig rig(temp.path);
    PatternScreen& drums = rig.drums();
    rig.clicks();
    drums.moveCursorTo(kClickLane, -1);
    rig.frames(30);
    rig.app.session().togglePlay(false);
    std::vector<long> at;
    std::vector<float> out(2 * 256);
    const long total = 6L * kBar;
    const SDL_Keycode turns[6] = {SDLK_RIGHT, SDLK_RIGHT, SDLK_UP, SDLK_LEFT, SDLK_RIGHT, SDLK_LEFT};
    int turn = 0;
    rig.key(X, true); // (held all the while: the drum it hears is the click, one sample, at once)
    for (long done = 0, block = 0; done < total; done += 256, ++block) {
        if (block % 13 == 5) rig.key(turns[turn % 6], true);
        if (block % 13 == 6) rig.key(turns[turn++ % 6], false);
        if (block == 700) rig.key(SDLK_RIGHT, true); // ...and run up to 100% and back
        if (block == 900) rig.key(SDLK_RIGHT, false), rig.key(SDLK_LEFT, true);
        if (block == 1100) rig.key(SDLK_LEFT, false);
        rig.clock += 16;
        rig.app.update(rig.input, rig.clock);
        rig.engine->render(out.data(), 256);
        for (int i = 0; i < 256; ++i)
            if (out[static_cast<std::size_t>(2 * i)] != 0.f && done + i >= kStep / 2 && done + i < total - kStep) at.push_back(done + i);
    }
    rig.key(X, false);
    rig.app.session().togglePlay(false);
    rig.frames(4);
    CHECK(turn > 20);
    const int steps = static_cast<int>(total / kStep) - 1; // steps 1 .. steps - 1 (the first has the audition's click on it)
    std::vector<int> seen(static_cast<std::size_t>(steps + 1), 0);
    for (long a : at) {
        const long n = (a + kStep / 2) / kStep;
        CHECK(std::labs(a - n * kStep) <= 577);
        if (n >= 1 && n <= steps) ++seen[static_cast<std::size_t>(n)];
    }
    for (int n = 1; n < steps; ++n) CHECK(seen[static_cast<std::size_t>(n)] == 1);
}

// ---- the dice and the copy ---------------------------------------------------------------------

void testTheDiceAndTheCopy() {
    Rig rig;
    PatternScreen& drums = rig.drums();
    drums.setStep(3, 2, 0.8f);
    drums.setStep(3, 9, 0.8f);
    drums.moveCursorTo(3, -1);
    rig.key(X, true);
    rig.frames(1);
    LaneFeel before = rig.kit().laneFeel[3];
    bool sawTimeOff = false, sawLevelOff = false, saw25 = false;
    for (int i = 0; i < 60; ++i) {
        rig.tap(Y);
        const LaneFeel now = rig.kit().laneFeel[3];
        CHECK(now != before && !now.atRest());                                 // always something, and something else
        CHECK(now.time <= 25 && now.level <= 25);                              // small amounts only
        CHECK((now.time == 0 || now.time == 10 || now.time == 25) && (now.level == 0 || now.level == 10 || now.level == 25));
        sawTimeOff = sawTimeOff || now.time == 0;
        sawLevelOff = sawLevelOff || now.level == 0;
        saw25 = saw25 || (now.time == 25 && now.level == 25);
        before = now;
    }
    CHECK(sawTimeOff && sawLevelOff && saw25);
    // The lane's own dice (hits) were not rolled, and no hit moved.
    CHECK(drums.level(3, 2) == 0.8f && drums.level(3, 9) == 0.8f && drums.diceHits(3) == 0);
    int hits = 0;
    for (int s = 0; s < kSteps; ++s) hits += drums.level(3, s) > 0.f;
    CHECK(hits == 2);
    // Y down with X held, X let go first, then Y: that is not a tap of Y (no reshuffle of the lane's hits).
    rig.key(Y, true);
    rig.frames(1);
    rig.key(X, false);
    rig.frames(1);
    rig.key(Y, false);
    rig.frames(1);
    hits = 0;
    for (int s = 0; s < kSteps; ++s) hits += drums.level(3, s) > 0.f;
    CHECK(hits == 2 && drums.level(3, 2) == 0.8f && drums.level(3, 9) == 0.8f);
    // ...and the same on a hit: its chance is not stepped.
    drums.moveCursorTo(3, 2);
    rig.tap(Y); // a plain tap first, so the last plain Y "did nothing" by itself: 100 > 75
    CHECK(drums.chance(3, 2) == 0.75f);
    rig.key(X, true);
    rig.key(Y, true);
    rig.frames(1);
    rig.key(X, false);
    rig.frames(1);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(drums.chance(3, 2) == 0.75f);

    // X + A: this lane's feel onto every lane.
    drums.moveCursorTo(3, -1);
    rig.app.session().setLaneFeel(0, 3, 50, 25);
    rig.app.session().setLaneFeel(0, 6, 100, 0);
    rig.key(X, true);
    rig.frames(1);
    rig.tap(A);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneFeel[lane].time == 50 && rig.kit().laneFeel[lane].level == 25);
    CHECK(hits == 2 && drums.level(3, 2) == 0.8f); // (A placed nothing, and did not shape the lane's volume)
    CHECK(drums.laneLevel(3) == 1.f);
    // From a lane at OFF: every lane plays plain again.
    rig.tap(SDLK_LEFT, 6);
    rig.tap(SDLK_DOWN, 6);
    CHECK(rig.kit().laneFeel[3].atRest() && !rig.kit().laneFeel[4].atRest());
    rig.tap(A);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneFeel[lane].atRest());
    rig.key(X, false);
    rig.frames(1);

    // The dice's own rule, apart from the screen.
    std::uint32_t dice = 12345u;
    LaneFeel was = laneFeelOf(100, 100);
    for (int i = 0; i < 500; ++i) {
        const LaneFeel now = feelDice(was, dice);
        CHECK(!now.atRest() && now != was && now.time <= 25 && now.level <= 25);
        was = now;
    }
    CHECK(feelAmountStep(0, -1) == 0 && feelAmountStep(0, 1) == 10 && feelAmountStep(75, 1) == 100 && feelAmountStep(100, 1) == 100 && feelAmountStep(100, -1) == 75);
    CHECK(feelAmountStep(30, 1) == 50 && feelAmountStep(30, -1) == 10); // (a number from a file by hand: from the click at or under it)
}

// ---- the strip, the chips, the mark, off the drawn screen -----------------------------------------

const ui::Hint kOnAName[] = {{"HOLD A", "volume"}, {"HOLD Y", "dice"}, {"TAP Y", "roll"}, {"HOLD X", "feel"}};

void heldOnAName(Rig& rig) {
    using namespace pattern_layout;
    rig.draw();
    const Chips chips = rig.chips();
    // Each chip's left edge is its field's; the dice alone at the left.
    CHECK((chips == Chips{{8, rig.chipW("Y")}, {static_cast<int>(kFeelSwingX), rig.chipW("L1 / R1")}, {static_cast<int>(kFeelTimeX), rig.chipW("LEFT / RIGHT")}, {static_cast<int>(kFeelLevelX), rig.chipW("UP / DOWN")}, {static_cast<int>(kFeelAllX), rig.chipW("A")}}));
    CHECK(rig.label(kFeelSwingX, "SWING") && rig.label(kFeelTimeX, "HUMAN TIME") && rig.label(kFeelLevelX, "HUMAN LEVEL") && rig.label(kFeelAllX, "EVERY LANE"));
    CHECK(!rig.label(kFeelTimeX + 1.f, "HUMAN TIME") && !rig.label(kFeelTimeX, "HUMAN LEVEL")); // (the check can tell)
    CHECK(!rig.label(134.f, "DRUM VOLUME") && !rig.label(430.f, "DICE HITS"));                     // the strip is the feel and nothing else
    CHECK(rig.chipIs(static_cast<int>(kFeelSwingX), "L1 / R1") && rig.chipIs(static_cast<int>(kFeelTimeX), "LEFT / RIGHT") && rig.chipIs(static_cast<int>(kFeelLevelX), "UP / DOWN") && rig.chipIs(static_cast<int>(kFeelAllX), "A"));
    CHECK(rig.diceAlone(static_cast<int>(kFeelSwingX)));
    // The help is said at the bar's end: the last chip leaves it room.
    CHECK(ui::gHintsOver == 0);
}

void testTheStrip() {
    using namespace pattern_layout;
    const TempFolder temp;
    Rig rig(temp.path);
    CHECK(rig.canvas.ok && rig.scratch.ok);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    PatternScreen& drums = rig.drums();
    drums.moveCursorTo(1, -1);
    // Nothing held, no feel: the plain bar with HOLD X in it, and it fits, with either help.
    rig.draw();
    CHECK(rig.plainBar(kOnAName, 4) && ui::gHintsOver == 0);
    CHECK(rig.label(134.f, "DRUM VOLUME") && !rig.label(kFeelTimeX, "HUMAN TIME"));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(!rig.markTime(lane) && !rig.markLevel(lane));
    // X held: the strip is the lane's feel, a chip under each field.
    rig.key(X, true);
    CHECK(drums.feeling());
    heldOnAName(rig);
    CHECK(rig.value(kFeelTimeX, "OFF", ui::kText) && rig.value(kFeelLevelX, "OFF", ui::kText));
    CHECK(rig.value(kFeelAllX, "THE SAME", ui::kDim)); // every lane is at OFF
    rig.tap(SDLK_RIGHT, 2);
    rig.tap(SDLK_UP, 5);
    heldOnAName(rig);
    CHECK(rig.value(kFeelTimeX, "25%", ui::kBrass) && rig.value(kFeelLevelX, "100%", ui::kBrass));
    CHECK(!rig.value(kFeelTimeX, "50%", ui::kBrass) && !rig.value(kFeelLevelX, "25%", ui::kBrass));
    CHECK(rig.value(kFeelAllX, "COPY", ui::kText));
    CHECK(rig.markTime(1) && rig.markLevel(1) && !rig.markTime(0) && !rig.markLevel(2));
    rig.tap(A);
    heldOnAName(rig);
    CHECK(rig.value(kFeelAllX, "THE SAME", ui::kDim));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.markTime(lane) && rig.markLevel(lane));
    // The widest: 100% and 100%.
    rig.tap(SDLK_RIGHT, 5);
    heldOnAName(rig);
    CHECK(rig.value(kFeelTimeX, "100%", ui::kBrass));
    // Let go: the plain bar again, pixel for pixel, and the strip says the feel in one line.
    rig.key(X, false);
    rig.draw();
    CHECK(!drums.feeling() && rig.plainBar(kOnAName, 4));
    CHECK(rig.label(134.f, "DRUM VOLUME"));
    {
        const char* line = "SWING OFF   HUMAN TIME 100%   HUMAN LEVEL 100%";
        const Font& small = rig.scratch.fonts->small;
        CHECK(rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
    }
    // ...each amount in its own place (TIME first).
    rig.app.session().setLaneFeel(0, 1, 25, 50);
    rig.draw();
    {
        const char* line = "SWING OFF   HUMAN TIME 25%   HUMAN LEVEL 50%";
        const char* swapped = "SWING OFF   HUMAN TIME 50%   HUMAN LEVEL 25%";
        const Font& small = rig.scratch.fonts->small;
        CHECK(rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
        CHECK(!rig.inked(small, 632.f - small.width(swapped), kStripY + 44.f, swapped, ui::kPurple));
        // L1 on a built-in drum says why it has no slices, on that row: the line gives way to it
        // (the two were drawn one over the other), and is back when it has been said.
        const char* why = "LANE 2 IS A BUILT-IN DRUM. PUT A RECORDING ON IT TO SLICE IT.";
        CHECK(!rig.inked(small, 8.f, kStripY + 44.f, why, ui::kBrass));
        rig.tap(SDLK_q);
        rig.draw();
        CHECK(!drums.slicing());
        CHECK(rig.inked(small, 8.f, kStripY + 44.f, why, ui::kBrass));
        CHECK(!rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
        bool purple = false; // nothing of the line is under the words
        for (int y = kStripY + 40; y < kStripY + 62; ++y)
            for (int x = 0; x < ui::kScreenW; ++x) purple = purple || rig.canvas.is(x, y, ui::kPurple);
        CHECK(!purple);
        rig.frames(200); // 3.2 s: it has been said
        rig.draw();
        CHECK(!rig.inked(small, 8.f, kStripY + 44.f, why, ui::kBrass));
        CHECK(rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
    }
    // One piece each: a lane with HUMAN TIME alone, one with HUMAN LEVEL alone.
    rig.app.session().setLaneFeel(0, 4, 10, 0);
    rig.app.session().setLaneFeel(0, 5, 0, 10);
    rig.app.session().setLaneFeel(0, 6, 0, 0);
    rig.draw();
    CHECK(rig.markTime(4) && !rig.markLevel(4) && !rig.markTime(5) && rig.markLevel(5) && !rig.markTime(6) && !rig.markLevel(6));
    // On a hit, and on a lane with no feel, no line.
    drums.moveCursorTo(6, -1);
    rig.draw();
    {
        const char* line = "SWING OFF   HUMAN TIME OFF   HUMAN LEVEL OFF";
        const Font& small = rig.scratch.fonts->small;
        CHECK(!rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
    }

    // A handheld with no MENU button: the same chips, and the plain list still fits.
    static ControlMap none;
    rig.input.useMap(&none);
    drums.moveCursorTo(1, -1);
    rig.draw();
    CHECK(std::strcmp(ui::gHelpHint, "HLD SEL: HELP") == 0);
    CHECK(rig.plainBar(kOnAName, 4) && ui::gHintsOver == 0);
    rig.key(X, true);
    heldOnAName(rig);
    rig.key(X, false);
    rig.frames(1);

    // A lane that plays a recording: L1 is said too, and it still fits; and in the slice view.
    rig.app.session().setLaneSample(0, 2, "break");
    rig.frames(2);
    drums.moveCursorTo(2, -1);
    rig.draw();
    const ui::Hint recording[] = {{"HOLD A", "volume"}, {"HOLD Y", "dice"}, {"TAP Y", "roll"}, {"HOLD X", "feel"}, {"L1", "slices"}};
    CHECK(rig.plainBar(recording, 5) && ui::gHintsOver == 0);
    drums.setSlicing(true);
    CHECK(drums.slicing());
    rig.draw();
    const ui::Hint sliced[] = {{"UP / DOWN", "one slice / runs on"}, {"HOLD A", "volume"}, {"HOLD Y", "dice"}, {"HOLD X", "feel"}, {"L1", "grid"}};
    CHECK(rig.plainBar(sliced, 5) && ui::gHintsOver == 0);
    // The slice view's one name wears the lane's mark (it stands where the grid's first name does).
    CHECK(!rig.kit().laneFeel[2].atRest() && rig.markTime(0) && rig.markLevel(0));
    {
        const LaneFeel had = rig.kit().laneFeel[2];
        rig.app.session().setLaneFeel(0, 2, 0, had.level);
        rig.draw();
        CHECK(!rig.markTime(0) && rig.markLevel(0));
        rig.app.session().setLaneFeel(0, 2, 0, 0);
        rig.draw();
        CHECK(!rig.markTime(0) && !rig.markLevel(0));
        rig.app.session().setLaneFeel(0, 2, had.time, had.level);
        rig.draw();
    }
    rig.key(X, true);
    CHECK(drums.feeling() && drums.slicing());
    heldOnAName(rig);
    // L1 and R1 with X held are SWING: the slice view stays, and no brush goes down.
    CHECK(rig.kit().laneSwing[2] == 0);
    rig.tap(SDLK_q);
    CHECK(drums.slicing() && drums.feeling() && rig.kit().laneSwing[2] == 0); // (at OFF: L1 stops there)
    rig.key(SDLK_w, true);
    rig.frames(1);
    CHECK(!drums.painting() && rig.kit().laneSwing[2] == 10);
    rig.key(SDLK_w, false);
    rig.frames(1);
    rig.tap(SDLK_q);
    CHECK(drums.slicing() && rig.kit().laneSwing[2] == 0);
    const LaneFeel was = rig.kit().laneFeel[2];
    rig.tap(SDLK_UP); // HUMAN LEVEL, not "one slice / runs on"
    CHECK(rig.kit().laneFeel[2].level != was.level || was.level == 100);
    CHECK(!rig.kit().laneOneSlice[2]);
    rig.key(X, false);
    rig.draw();
    CHECK(rig.plainBar(sliced, 5));
    rig.input.useMap(nullptr);
    rig.frames(1);
}

// ---- the song file ------------------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.app.session();
    rig.drums().setStep(1, 4, 0.8f);
    rig.frames(2);
    const std::string plain = songText(rig.project());
    CHECK(plain.find("feel") == std::string::npos); // a lane at rest writes nothing: the file is as it always was
    session.setLaneFeel(0, 2, 25, 10);
    session.setLaneFeel(0, 7, 0, 100);
    session.setLaneFeel(0, 0, 75, 0);
    const std::string text = songText(rig.project());
    CHECK(text.find("\nfeel 3 25 10\n") != std::string::npos && text.find("\nfeel 8 0 100\n") != std::string::npos && text.find("\nfeel 1 75 0\n") != std::string::npos);
    CHECK(text.find("feel 2 ") == std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].laneFeel[2] == laneFeelOf(25, 10) && back->tracks[0].laneFeel[7] == laneFeelOf(0, 100) && back->tracks[0].laneFeel[0] == laneFeelOf(75, 0));
    CHECK(back->tracks[0].laneFeel[1].atRest());
    // Put back at rest: the very text it was before there was a feel.
    session.setLaneFeel(0, 2, 0, 0);
    session.setLaneFeel(0, 7, 0, 0);
    session.setLaneFeel(0, 0, 0, 0);
    CHECK(songText(rig.project()) == plain);
    // By hand: numbers out of range are held (as doubles first), a lane that is not there is skipped,
    // a level left off is OFF, a line under a synth track is not taken.
    const std::size_t lanes = plain.find("\nlanes ");
    CHECK(lanes != std::string::npos);
    if (lanes == std::string::npos) return;
    const std::size_t after = plain.find('\n', lanes + 1) + 1;
    const auto with = [&](const std::string& lines) { return plain.substr(0, after) + lines + plain.substr(after); };
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(with("feel 2 1e300 -1e300\nfeel 4 -7 250\nfeel 5 50\nfeel 9 50 50\nfeel 0 50 50\nfeel x\nfeel 6 nan 10\nfeel 7 12.9 99.9\n"), *odd));
    CHECK(odd->tracks[0].laneFeel[1] == laneFeelOf(100, 0));
    CHECK(odd->tracks[0].laneFeel[3] == laneFeelOf(0, 100));
    CHECK(odd->tracks[0].laneFeel[4] == laneFeelOf(50, 0));
    CHECK(odd->tracks[0].laneFeel[6] == laneFeelOf(12, 99));
    for (int lane : {0, 2, 5, 7}) CHECK(odd->tracks[0].laneFeel[lane].atRest());
    // (a lane that is not there wrote nowhere: the song is the one with the four good lines, and nothing else of it moved)
    CHECK(songText(*odd) == with("feel 2 100 0\nfeel 4 0 100\nfeel 5 50 0\nfeel 7 12 99\n"));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(!odd->tracks[0].laneOneSlice[lane] && odd->tracks[0].laneFit[lane] == 0);
    CHECK(odd->tracks[0].preset == rig.project().tracks[0].preset);
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.frames(2);
    std::string synth = songText(rig.project());
    synth += "feel 3 50 50\n"; // after the last track's lines: the synth's
    std::unique_ptr<Project> none(new Project);
    CHECK(parseSong(synth, *none));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(none->tracks[1].laneFeel[lane].atRest() && none->tracks[0].laneFeel[lane].atRest());
}

// ---- undo, and a song sent again ------------------------------------------------------------------

void testUndoAndASongSentAgain() {
    const TempFolder temp;
    Rig rig(temp.path);
    Session& session = rig.app.session();
    PatternScreen& drums = rig.drums();
    rig.clicks();
    rig.settle();
    drums.moveCursorTo(kClickLane, -1);
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.tap(SDLK_RIGHT, 5);
    rig.tap(SDLK_UP, 2);
    rig.key(X, false);
    rig.settle();
    CHECK(rig.kit().laneFeel[kClickLane] == laneFeelOf(100, 25));
    CHECK(session.undoSteps() == steps + 1); // the turns of one hold: one step
    CHECK(rig.played() == rig.ruled() && rig.played() != rig.grid());
    // Undo: the lane is plain, in the project and in the engine. Redo: felt again.
    CHECK(session.undo());
    CHECK(rig.kit().laneFeel[kClickLane].atRest());
    for (int s = 0; s < kSteps; ++s) CHECK(drums.level(kClickLane, s) == 0.8f);
    CHECK(rig.played() == rig.grid());
    CHECK(session.redo());
    CHECK(rig.kit().laneFeel[kClickLane] == laneFeelOf(100, 25));
    CHECK(rig.played() == rig.ruled() && rig.played() != rig.grid());
    // A hit edited after the feel: undoing the hit leaves the feel.
    rig.settle();
    drums.setStep(5, 3, 0.8f);
    rig.settle();
    CHECK(session.undo());
    CHECK(drums.level(5, 3) == 0.f && rig.kit().laneFeel[kClickLane] == laneFeelOf(100, 25));
    // The copy to every lane is one step, and comes back as it was, lane by lane.
    session.setLaneFeel(0, 6, 10, 10);
    rig.settle();
    rig.key(X, true);
    rig.tap(A);
    rig.key(X, false);
    rig.settle();
    CHECK(rig.kit().laneFeel[6] == laneFeelOf(100, 25) && rig.kit().laneFeel[0] == laneFeelOf(100, 25));
    CHECK(session.undo());
    CHECK(rig.kit().laneFeel[6] == laneFeelOf(10, 10) && rig.kit().laneFeel[0].atRest() && rig.kit().laneFeel[kClickLane] == laneFeelOf(100, 25));

    // The whole song sent again (another song opened, a full queue): the engine has the feel.
    session.applyProject();
    rig.frames(4);
    CHECK(rig.played() == rig.ruled() && rig.played() != rig.grid());
    // ...and a song with none: plain, though the engine had one.
    session.setLaneFeel(0, kClickLane, 100, 0);
    rig.frames(2);
    rig.kit().laneFeel[kClickLane] = LaneFeel(); // (as a song read from a file: the project alone changes)
    session.applyProject();
    rig.frames(4);
    CHECK(rig.played() == rig.grid());
}

// ---- a bounce: the song, a TAKE of its own ---------------------------------------------------------

void testABounce() {
    const TempFolder temp;
    const std::string folder = tempFolder("feelb");
    Rig rig(temp.path);
    Session& session = rig.app.session();
    session.useBounceFolder(folder);
    rig.clicks();
    session.setLaneFeel(0, kClickLane, 100, 0);
    rig.frames(4);
    // Heard: the song played (not the scene alone), its one bar.
    rig.frames(30);
    session.togglePlay(true);
    std::vector<long> heard;
    {
        std::vector<float> out(2 * 256);
        for (long done = 0; done < kBar - 600 - 256; done += 256) { // (to just short of the bar's end, where the next round's first hit may come early)
            rig.engine->render(out.data(), 256);
            for (int i = 0; i < 256; ++i)
                if (out[static_cast<std::size_t>(2 * i)] != 0.f) heard.push_back(done + i);
        }
    }
    session.togglePlay(true);
    rig.frames(30);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    std::string bytes;
    CHECK(readTextFile(folder + "/" + session.bounceLast(), bytes) && bytes.size() > 44);
    std::vector<long> bounced;
    long last = -1;
    for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) {
        const int low = static_cast<unsigned char>(bytes[at]), high = static_cast<signed char>(bytes[at + 1]);
        const long frame = static_cast<long>((at - 44) / 4);
        if (high * 256 + low != 0) {
            if (frame < (kBar - 600 - 256 + 255) / 256 * 256) bounced.push_back(frame);
            last = frame;
        }
    }
    // A bounce is a TAKE of its own (wifi-and-bounce.md): its HUMAN TIME rolled afresh, so not where the live hits
    // were, but every hit there, each near its step, the song's one bar (and not the scene alone).
    CHECK(heard.size() == 16 && bounced.size() == 16 && bounced != heard);
    for (std::size_t i = 0; i < bounced.size() && i < heard.size(); ++i) CHECK(std::labs(bounced[i] - heard[i]) < kStep / 2);
    // The file is the bar and nothing of a second: the last step asked nothing early of the first.
    CHECK(last < kBar);
    CHECK(last > kBar - kStep - 600);
    std::remove((folder + "/" + session.bounceLast()).c_str());
    rmdir(folder.c_str());
}

// ---- X still hears the drum -------------------------------------------------------------------------

void testXStillHears() {
    const TempFolder temp;
    Rig rig(temp.path);
    PatternScreen& drums = rig.drums();
    rig.app.session().setLaneSample(0, 2, "break"); // a second long: it sounds while X is held
    rig.frames(4);
    drums.moveCursorTo(2, -1);
    const auto loud = [&](int frames) {
        std::vector<float> out(2 * 256);
        float peak = 0.f;
        for (int f = 0; f < frames; ++f) {
            rig.clock += 16;
            rig.app.update(rig.input, rig.clock);
            rig.engine->render(out.data(), 256);
            for (float v : out) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        }
        return peak;
    };
    CHECK(loud(10) == 0.f);
    rig.key(X, true);
    CHECK(drums.feeling());
    CHECK(loud(10) > 0.05f); // heard, and the hold is the feel's too
    rig.key(SDLK_RIGHT, true);
    rig.key(SDLK_RIGHT, false);
    CHECK(rig.kit().laneFeel[2].time == 10);
    CHECK(loud(10) > 0.05f); // still sounding while X is held, a turn of the feel does not stop it
    rig.key(X, false);
    loud(30);
    CHECK(loud(10) == 0.f); // let go: it stops, as ever
    // A tap hears it and sets nothing.
    const LaneFeel was = rig.kit().laneFeel[2];
    rig.key(X, true);
    const float tapped = loud(2);
    rig.key(X, false);
    CHECK(tapped > 0.05f && rig.kit().laneFeel[2] == was);
}

// ---- SWING -----------------------------------------------------------------------------------------

// X held on a lane's name: R1 one click of SWING more, L1 one less, through the six amounts, stopping at
// each end. The engine plays it; the strip says it; the name wears its mark; the copy takes it; the dice
// leave it alone.
void testSwing() {
    using namespace pattern_layout;
    const TempFolder temp;
    Rig rig(temp.path);
    PatternScreen& drums = rig.drums();
    rig.clicks();
    drums.moveCursorTo(kClickLane, -1);
    CHECK(rig.played() == rig.grid());
    rig.key(X, true);
    rig.frames(1);
    const int amounts[6] = {0, 10, 25, 50, 75, 100};
    for (int i = 1; i <= 7; ++i) {
        rig.tap(SDLK_w); // R1
        CHECK(rig.kit().laneSwing[kClickLane] == amounts[i > 5 ? 5 : i]);
    }
    rig.tap(SDLK_q, 2); // L1
    CHECK(rig.kit().laneSwing[kClickLane] == 50);
    CHECK(rig.kit().laneFeel[kClickLane].atRest()); // SWING is not the feel's amounts
    for (int lane = 0; lane < kDrumLanes; ++lane)
        if (lane != kClickLane) CHECK(rig.kit().laneSwing[lane] == 0);
    CHECK(drums.cursorLane() == kClickLane && drums.cursorStep() == -1 && drums.feeling() && !drums.slicing() && !drums.painting());
    rig.key(X, false);
    rig.frames(2);
    CHECK(!drums.slicing()); // (L1 let go with X held opens no slices)
    // The engine was told: every second sixteenth a sixth of a step late.
    const std::vector<long> at = rig.played();
    CHECK(at == rig.swung() && at != rig.grid());
    if (at.size() > 3) CHECK(at[1] == kStep + 1000 && at[2] == 2 * kStep && at[3] == 3 * kStep + 1000);
    // Without X, L1 and R1 are what they were: R1 is the brush on a step.
    drums.moveCursorTo(kClickLane, 0);
    rig.key(SDLK_w, true);
    rig.frames(1);
    CHECK(drums.painting() && rig.kit().laneSwing[kClickLane] == 50);
    rig.key(SDLK_w, false);
    rig.frames(1);
    drums.moveCursorTo(kClickLane, -1);
    // HUMAN TIME beside it: each hit at its roll plus its swing.
    rig.app.session().setLaneFeel(0, kClickLane, 50, 0);
    rig.frames(2);
    {
        const std::vector<long> both = rig.played();
        std::vector<long> want;
        for (long n = 0; n < 2 * kSteps; ++n) {
            const int step = static_cast<int>(n % kSteps);
            const long place = n * kStep + static_cast<long>(std::ceil(feelSamples(50, step, feelSeed(0, kClickLane, 0, step, n), 48.0, kStep) + swingSamples(50, step, kStep)));
            want.push_back(n == 0 && place < 0 ? 0 : place);
        }
        CHECK(both == want);
    }
    rig.app.session().setLaneFeel(0, kClickLane, 0, 0);

    // The mark: a lane with SWING alone wears it, not FEEL's; one with none wears neither.
    CHECK(rig.canvas.ok && rig.scratch.ok);
    if (!rig.canvas.ok || !rig.scratch.ok) return;
    rig.draw();
    CHECK(rig.markSwing(kClickLane) && !rig.markTime(kClickLane) && !rig.markLevel(kClickLane));
    for (int lane = 0; lane < kDrumLanes; ++lane)
        if (lane != kClickLane) CHECK(!rig.anySwingMark(lane));
    // ...in the slice view too (the one name stands where the grid's first does).
    rig.app.session().setLaneSample(0, kClickLane, "break");
    rig.frames(2);
    drums.setSlicing(true);
    rig.draw();
    CHECK(drums.slicing() && rig.markSwing(0));
    rig.app.session().setLaneSwing(0, kClickLane, 0);
    rig.draw();
    CHECK(!rig.anySwingMark(0));
    rig.app.session().setLaneSwing(0, kClickLane, 50);
    drums.setSlicing(false);
    rig.app.session().setLaneSample(0, kClickLane, "click");
    rig.frames(2);
    // The strip with X up: SWING first in the line, and a lane with SWING alone has the line.
    rig.draw();
    {
        const char* line = "SWING 50%   HUMAN TIME OFF   HUMAN LEVEL OFF";
        const Font& small = rig.scratch.fonts->small;
        CHECK(rig.inked(small, 632.f - small.width(line), kStripY + 44.f, line, ui::kPurple));
    }
    // X held: SWING's field, brass, its chip under it; EVERY LANE asks for SWING too.
    rig.key(X, true);
    heldOnAName(rig);
    CHECK(rig.value(kFeelSwingX, "50%", ui::kBrass) && !rig.value(kFeelSwingX, "25%", ui::kBrass));
    CHECK(rig.value(kFeelTimeX, "OFF", ui::kText));
    CHECK(rig.value(kFeelAllX, "COPY", ui::kText)); // the feels are alike (all OFF), the swings are not
    // The dice leave SWING alone.
    for (int i = 0; i < 12; ++i) {
        rig.tap(Y);
        CHECK(rig.kit().laneSwing[kClickLane] == 50 && !rig.kit().laneFeel[kClickLane].atRest());
    }
    // X + A: the feel and the SWING onto every lane.
    rig.tap(A);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneSwing[lane] == 50 && rig.kit().laneFeel[lane] == rig.kit().laneFeel[kClickLane]);
    heldOnAName(rig);
    CHECK(rig.value(kFeelAllX, "THE SAME", ui::kDim));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.markSwing(lane));
    // ...and a lane at OFF takes every lane's SWING off with its feel.
    rig.tap(SDLK_q, 6);
    rig.tap(SDLK_LEFT, 6);
    rig.tap(SDLK_DOWN, 6);
    rig.tap(A);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneSwing[lane] == 0 && rig.kit().laneFeel[lane].atRest());
    rig.key(X, false);
    rig.frames(1);
    rig.draw();
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(!rig.anySwingMark(lane));
}

// SWING in the song file (left out at 0), in undo (one hold, one step), in a song sent again, in a bounce.
void testSwingKept() {
    const TempFolder temp;
    const std::string folder = tempFolder("swingb");
    Rig rig(temp.path);
    Session& session = rig.app.session();
    PatternScreen& drums = rig.drums();
    rig.clicks();
    rig.settle();
    // The song file.
    const std::string plain = songText(rig.project());
    CHECK(plain.find("swing") == std::string::npos);
    session.setLaneSwing(0, 0, 25);
    session.setLaneSwing(0, 7, 100);
    const std::string text = songText(rig.project());
    CHECK(text.find("\nswing 1 25\n") != std::string::npos && text.find("\nswing 8 100\n") != std::string::npos && text.find("swing 2 ") == std::string::npos);
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(text, *back) && songText(*back) == text);
        CHECK(back->tracks[0].laneSwing[0] == 25 && back->tracks[0].laneSwing[7] == 100 && back->tracks[0].laneSwing[1] == 0);
    }
    session.setLaneSwing(0, 0, 0);
    session.setLaneSwing(0, 7, 0);
    CHECK(songText(rig.project()) == plain);
    {
        // By hand: held as doubles first, a lane that is not there skipped, a line with no amount skipped,
        // a line under a synth track not taken.
        const std::size_t lanes = plain.find("\nlanes ");
        CHECK(lanes != std::string::npos);
        const std::size_t after = plain.find('\n', lanes + 1) + 1;
        const auto with = [&](const std::string& lines) { return plain.substr(0, after) + lines + plain.substr(after); };
        std::unique_ptr<Project> odd(new Project);
        CHECK(parseSong(with("swing 2 1e300\nswing 4 -7\nswing 5\nswing 9 50\nswing 0 50\nswing x\nswing 6 nan\nswing 7 66.9\n"), *odd));
        CHECK(odd->tracks[0].laneSwing[1] == 100 && odd->tracks[0].laneSwing[6] == 66);
        for (int lane : {0, 2, 3, 4, 5, 7}) CHECK(odd->tracks[0].laneSwing[lane] == 0);
        std::unique_ptr<Project> want(new Project); // (and nothing else of the song moved)
        CHECK(parseSong(plain, *want));
        want->tracks[0].laneSwing[1] = 100;
        want->tracks[0].laneSwing[6] = 66;
        CHECK(songText(*odd) == songText(*want));
        // Under a synth track: not taken.
        std::unique_ptr<Project> synth(new Project);
        CHECK(parseSong(plain, *synth));
        synth->tracks[1] = synth->tracks[0];
        synth->tracks[1].kind = TrackKind::Phazed;
        std::string two = songText(*synth);
        two += "swing 3 50\n"; // after the last track's lines: the synth's
        std::unique_ptr<Project> none(new Project);
        CHECK(parseSong(two, *none));
        for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(none->tracks[1].laneSwing[lane] == 0 && none->tracks[0].laneSwing[lane] == 0);
    }
    // Only a drum track has a SWING.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.frames(2);
    session.setLaneSwing(1, 0, 50);
    session.setLaneSwing(-1, 0, 50);
    session.setLaneSwing(0, kDrumLanes, 50);
    session.setLaneSwing(0, -1, 50);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.project().tracks[1].laneSwing[lane] == 0 && rig.kit().laneSwing[lane] == 0);
    CHECK(songText(rig.project()).find("swing") == std::string::npos);
    rig.app.session().view = View::Pattern;
    rig.project().track = 0;
    rig.frames(2);
    // Undo: the clicks of one hold are one step; undo and redo reach the engine.
    rig.settle();
    drums.moveCursorTo(kClickLane, -1);
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.tap(SDLK_w, 5);
    rig.key(X, false);
    rig.settle();
    CHECK(rig.kit().laneSwing[kClickLane] == 100 && session.undoSteps() == steps + 1);
    CHECK(rig.played() == rig.swung() && rig.played() != rig.grid());
    CHECK(session.undo());
    CHECK(rig.kit().laneSwing[kClickLane] == 0 && rig.played() == rig.grid());
    CHECK(session.redo());
    CHECK(rig.kit().laneSwing[kClickLane] == 100 && rig.played() == rig.swung());
    // The whole song sent again: the engine has it; a song with none plays straight, though the engine had one.
    session.applyProject();
    rig.frames(4);
    CHECK(rig.played() == rig.swung() && rig.played() != rig.grid());
    rig.kit().laneSwing[kClickLane] = 0; // (as a song read from a file: the project alone changes)
    session.applyProject();
    rig.frames(4);
    CHECK(rig.played() == rig.grid());
    // A bounce is what was heard: the last step's hit swung, inside the bar.
    session.useBounceFolder(folder);
    session.setLaneSwing(0, kClickLane, 100);
    rig.frames(30);
    session.togglePlay(true);
    std::vector<long> heard;
    {
        std::vector<float> out(2 * 256);
        for (long done = 0; done < kBar - 256; done += 256) {
            rig.engine->render(out.data(), 256);
            for (int i = 0; i < 256; ++i)
                if (out[static_cast<std::size_t>(2 * i)] != 0.f) heard.push_back(done + i);
        }
    }
    session.togglePlay(true);
    rig.frames(30);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    std::string bytes;
    CHECK(readTextFile(folder + "/" + session.bounceLast(), bytes) && bytes.size() > 44);
    std::vector<long> bounced;
    for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) {
        const int low = static_cast<unsigned char>(bytes[at]), high = static_cast<signed char>(bytes[at + 1]);
        if (high * 256 + low != 0) bounced.push_back(static_cast<long>((at - 44) / 4));
    }
    CHECK(heard.size() == 16 && bounced == heard);
    if (heard.size() == 16) CHECK(heard[15] == 15 * kStep + 2000);
    std::remove((folder + "/" + session.bounceLast()).c_str());
    rmdir(folder.c_str());
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheGesture", testTheGesture}, {"testTurnedWhileItPlays", testTurnedWhileItPlays}, {"testTheDiceAndTheCopy", testTheDiceAndTheCopy}, {"testTheStrip", testTheStrip}, {"testTheSongFile", testTheSongFile},
        {"testUndoAndASongSentAgain", testUndoAndASongSentAgain}, {"testABounce", testABounce}, {"testXStillHears", testXStillHears}, {"testXLetGoFirst", testXLetGoFirst},
        {"testSwing", testSwing}, {"testSwingKept", testSwingKept},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("feelapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
