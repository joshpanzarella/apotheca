// Tests for the Pattern screen's buttons. No window and no sound card: the
// buttons are pressed through the keyboard stand-in, and the engine is
// rendered by hand to hear what the screen told it.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "canvas.hpp"
#include "input.hpp"
#include "note_marks.hpp"
#include "ui.hpp"
#include "pattern_screen.hpp"

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

// One handheld on a desk: engine, buttons and screen.
struct Rig {
    Engine engine;
    Input input;
    std::unique_ptr<Project> project{new Project};
    Session session{engine, *project};
    PatternScreen screen{session};
    Uint32 clock = 1000;

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) screen.onButton(press, input);
    }
    void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    void frames(int count) { // screen frames, 16 ms each
        for (int i = 0; i < count; ++i) { clock += 16; screen.update(input, clock); }
    }
    float listen(int audioFrames = 4000) { // loudest sample the engine makes next
        std::vector<float> out(2 * audioFrames);
        engine.render(out.data(), audioFrames);
        float peak = 0.f;
        for (float s : out) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
        return peak;
    }
};

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w, R2 = SDLK_2;
// The one stick.
const SDL_Keycode STICK_UP = SDLK_i, STICK_DOWN = SDLK_k, STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l;

// One screen frame of the stick pushed fully over moves a value this far.
const float kTurn = 3.f * 0.016f;

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testCursorMovesAndWraps() {
    Rig rig;
    CHECK(rig.screen.cursorLane() == 0 && rig.screen.cursorStep() == 0);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_DOWN);
    CHECK(rig.screen.cursorLane() == 1 && rig.screen.cursorStep() == 1);
    // Left of step 1 is the lane's name; left of that comes round to step 16.
    rig.tap(SDLK_LEFT);
    rig.tap(SDLK_LEFT);
    CHECK(rig.screen.cursorStep() == -1 && rig.screen.cursorLane() == 1);
    rig.tap(SDLK_LEFT);
    CHECK(rig.screen.cursorStep() == kSteps - 1);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    CHECK(rig.screen.cursorStep() == 0);
    // Up from the top lane is the tempo; up again comes round to the bottom.
    rig.tap(SDLK_UP);
    CHECK(rig.screen.cursorLane() == 0 && !rig.screen.cursorOnTempo());
    rig.tap(SDLK_UP);
    CHECK(rig.screen.cursorOnTempo());
    rig.tap(SDLK_LEFT); // nothing beside the tempo
    CHECK(rig.screen.cursorOnTempo());
    rig.tap(SDLK_UP);
    CHECK(!rig.screen.cursorOnTempo() && rig.screen.cursorLane() == kDrumLanes - 1 && rig.screen.cursorStep() == 0);
    rig.tap(SDLK_DOWN);
    CHECK(rig.screen.cursorLane() == 0);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_DOWN); // down from the tempo is the top lane
    CHECK(!rig.screen.cursorOnTempo() && rig.screen.cursorLane() == 0);
}

void testHeldDirectionRunsFast() {
    Rig rig;
    rig.key(SDLK_RIGHT, true);
    CHECK(rig.screen.cursorStep() == 1); // the first move is at once
    rig.frames(9); // under the 170 ms wait
    CHECK(rig.screen.cursorStep() == 1);
    // Then it runs: across all sixteen steps in well under a second.
    rig.frames(15); // a quarter of a second more
    CHECK(rig.screen.cursorStep() >= 7);
    rig.key(SDLK_RIGHT, false);
    const int stopped = rig.screen.cursorStep();
    rig.frames(30);
    CHECK(rig.screen.cursorStep() == stopped);
}

void testStickRunsTheCursor() {
    Rig rig;
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(rig.screen.cursorStep() == 1); // no wait at all
    rig.frames(25); // 0.4 s at 36 a second
    CHECK(rig.screen.cursorStep() >= 12);
    rig.key(STICK_RIGHT, false);
    const int stopped = rig.screen.cursorStep();
    rig.frames(20);
    CHECK(rig.screen.cursorStep() == stopped);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    CHECK(rig.screen.cursorStep() == stopped - 1);
    rig.key(STICK_LEFT, false);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    CHECK(rig.screen.cursorLane() == 1);
    rig.key(STICK_DOWN, false);

    // While A is held the stick shapes the hit; it does not move the cursor.
    const int lane = rig.screen.cursorLane(), step = rig.screen.cursorStep();
    rig.key(A, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(20);
    CHECK(rig.screen.cursorLane() == lane && rig.screen.cursorStep() == step);
}

void testAStalledFrameDoesNotFlingTheCursor() {
    Rig rig;
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.clock += 5000; // the program hung for five seconds
    rig.frames(1);
    CHECK(rig.screen.cursorStep() <= 3);
}

void testAPlacesAHitAndPlaysIt() {
    Rig rig;
    CHECK(rig.listen() == 0.f);
    rig.tap(A);
    CHECK(near(rig.screen.level(0, 0), 0.8f));
    CHECK(rig.listen() > 0.2f); // heard at once, since the song is stopped
    // A on a hit that is already there leaves its level alone.
    rig.screen.setStep(0, 0, 0.3f);
    rig.tap(A);
    CHECK(near(rig.screen.level(0, 0), 0.3f));
}

void testHoldAAndStickFillsTheHit() {
    Rig rig;
    rig.screen.setStep(0, 0, 0.5f);

    // Hold A and push the stick up: it fills.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(4);
    CHECK(near(rig.screen.level(0, 0), 0.5f + 4 * kTurn));
    rig.frames(8); // empty to full takes a third of a second
    CHECK(rig.screen.level(0, 0) == 1.f); // and stops at full
    rig.key(STICK_UP, false);

    // Pull it down: it drains, but never to nothing. Deleting is B's job.
    rig.key(STICK_DOWN, true);
    rig.frames(4);
    CHECK(near(rig.screen.level(0, 0), 1.f - 4 * kTurn));
    rig.frames(200);
    CHECK(near(rig.screen.level(0, 0), 0.05f));
    rig.key(STICK_DOWN, false);

    // Let go of A and the same stick is the cursor again; the hit is left alone.
    rig.key(A, false);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    CHECK(rig.screen.cursorLane() == 1);
    CHECK(near(rig.screen.level(0, 0), 0.05f));
}

void testHoldAAndDpadStepsTheLevel() {
    Rig rig;
    rig.screen.setStep(0, 0, 0.5f);
    rig.key(A, true);
    rig.tap(SDLK_UP);
    CHECK(near(rig.screen.level(0, 0), 0.6f));
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN);
    CHECK(near(rig.screen.level(0, 0), 0.4f));
    // Sideways is the nudge, a tenth of a step at a time. The cursor stays.
    rig.tap(SDLK_LEFT);
    CHECK(near(rig.screen.nudge(0, 0), -0.1f));
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    CHECK(near(rig.screen.nudge(0, 0), 0.1f));
    CHECK(rig.screen.cursorLane() == 0 && rig.screen.cursorStep() == 0);
    CHECK(near(rig.screen.level(0, 0), 0.4f));
    // B cannot delete while A is held.
    rig.tap(B);
    CHECK(near(rig.screen.level(0, 0), 0.4f));
    rig.key(A, false);
    rig.tap(SDLK_RIGHT);
    CHECK(rig.screen.cursorStep() == 1);
}

void testBDeletes() {
    Rig rig;
    rig.tap(A);
    rig.tap(B);
    CHECK(rig.screen.level(0, 0) == 0.f);
    rig.listen(40000); // let the audition ring out
    rig.tap(START);
    CHECK(rig.listen(96000) == 0.f); // and the engine was told too
}

void testTheEngineHearsTheLevel() {
    Rig loud, soft;
    for (Rig* rig : {&loud, &soft}) {
        rig->tap(SDLK_DOWN); // snare lane
        rig->screen.setStep(1, 0, 1.f);
    }
    soft.key(A, true);
    soft.key(STICK_DOWN, true);
    soft.frames(200); // all the way down
    soft.key(STICK_DOWN, false);
    soft.tap(SDLK_UP); // A + D-pad up: to the next tenth, 0.1
    soft.tap(SDLK_UP);
    soft.tap(SDLK_UP);
    soft.tap(SDLK_UP);
    soft.key(A, false);
    CHECK(near(soft.screen.level(1, 0), 0.4f));
    loud.tap(START);
    soft.tap(START);
    const float a = loud.listen(), b = soft.listen();
    CHECK(a > 0.2f);
    CHECK(std::fabs(b - 0.4f * a) < 1e-3f);
}

void testStartPlaysAndStops() {
    Rig rig;
    rig.screen.setStep(0, 0, 1.f);
    CHECK(!rig.screen.playing());
    rig.tap(START);
    CHECK(rig.screen.playing());
    CHECK(rig.listen() > 0.2f);
    CHECK(rig.engine.playStep() == 0);
    rig.tap(START);
    CHECK(!rig.screen.playing());
    rig.listen();
    CHECK(rig.engine.playStep() == -1);
    // SELECT + START is "quit", which main.cpp handles; it must not toggle play.
    rig.key(SELECT, true);
    rig.tap(START);
    rig.key(SELECT, false);
    CHECK(!rig.screen.playing());
}

void testPlacingWhilePlayingIsNotDoubled() {
    // While the song plays the loop itself sounds the new hit; the screen
    // must not also fire it by hand.
    Rig rig;
    rig.tap(START);
    rig.listen(1000);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT); // step 5, which is 24000 frames in
    rig.tap(A);
    CHECK(rig.listen(20000) == 0.f);
    CHECK(rig.listen(8000) > 0.2f);
}

void testXHearsTheLane() {
    Rig rig;
    rig.tap(X);
    CHECK(rig.listen() > 0.2f);
    CHECK(rig.screen.level(0, 0) == 0.f); // hearing is not placing
}

void testTempoIsUnderTheCursor() {
    Rig rig;
    CHECK(rig.screen.tempo() == 120.f);
    rig.tap(SDLK_UP); // from the top lane to the tempo
    CHECK(rig.screen.cursorOnTempo());

    // A does not place anything up here.
    rig.key(A, true);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) CHECK(rig.screen.level(lane, step) == 0.f);
    // A + D-pad: exact steps, 1 up and down, 10 left and right.
    rig.tap(SDLK_UP);
    CHECK(rig.screen.tempo() == 121.f);
    rig.tap(SDLK_RIGHT);
    CHECK(rig.screen.tempo() == 131.f);
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_LEFT);
    CHECK(rig.screen.tempo() == 120.f);
    CHECK(rig.screen.cursorOnTempo());
    // Hold A + stick: fast. 90 BPM a second, so 20 frames is about 29.
    rig.key(STICK_UP, true);
    rig.frames(20);
    rig.key(STICK_UP, false);
    CHECK(rig.screen.tempo() >= 147.f && rig.screen.tempo() <= 150.f);
    rig.key(STICK_UP, true);
    rig.frames(200);
    rig.key(STICK_UP, false);
    CHECK(rig.screen.tempo() == kMaxTempo);
    rig.key(STICK_DOWN, true);
    rig.frames(400);
    rig.key(STICK_DOWN, false);
    CHECK(rig.screen.tempo() == kMinTempo);
    rig.key(A, false);

    // B and X do nothing up here.
    rig.tap(B);
    rig.tap(X);
    CHECK(rig.listen() == 0.f);

    // The engine follows: at 40 BPM a step is 18000 frames.
    rig.screen.setStep(0, 1, 1.f);
    rig.tap(START);
    CHECK(rig.listen(17000) == 0.f);
    CHECK(rig.listen(2000) > 0.2f);
}

void testSelectNoLongerChangesTempo() {
    Rig rig;
    rig.key(SELECT, true);
    rig.tap(SDLK_RIGHT);
    rig.key(SELECT, false);
    CHECK(rig.screen.tempo() == 120.f);
}

void testLaneNameIsTheDrumsVolume() {
    Rig rig;
    rig.tap(SDLK_DOWN); // snare
    rig.tap(SDLK_LEFT); // onto its name
    CHECK(rig.screen.cursorStep() == -1);
    CHECK(rig.screen.laneLevel(1) == 1.f);

    // A plays the drum and places nothing.
    rig.key(A, true);
    const float full = rig.listen();
    CHECK(full > 0.2f);
    for (int step = 0; step < kSteps; ++step) CHECK(rig.screen.level(1, step) == 0.f);

    // Hold A + stick down: the drum's volume drains, all the way to silent.
    rig.key(STICK_DOWN, true);
    rig.frames(4);
    CHECK(near(rig.screen.laneLevel(1), 1.f - 4 * kTurn));
    rig.frames(200);
    CHECK(rig.screen.laneLevel(1) == 0.f);
    rig.key(STICK_DOWN, false);
    // A + D-pad: tenths.
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    CHECK(near(rig.screen.laneLevel(1), 0.5f));
    rig.tap(SDLK_LEFT); // sideways does nothing while A is held
    CHECK(rig.screen.cursorStep() == -1);
    rig.key(A, false);
    CHECK(rig.screen.laneLevel(0) == 1.f); // the other drums were not touched

    // The engine heard it: the snare is now half as loud.
    rig.listen(40000);
    rig.tap(X);
    CHECK(std::fabs(rig.listen() - 0.5f * full) < 1e-3f);
    // B deletes nothing here.
    rig.screen.setStep(1, 0, 1.f);
    rig.tap(B);
    CHECK(rig.screen.level(1, 0) == 1.f);
}

void testHoldAAndStickSidewaysNudges() {
    Rig rig;
    rig.screen.setStep(0, 4, 0.5f);
    rig.screen.moveCursorTo(0, 4);
    rig.key(A, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(4);
    CHECK(near(rig.screen.nudge(0, 4), 4 * kTurn * 0.5f)); // late
    CHECK(near(rig.screen.level(0, 4), 0.5f));             // the level is not touched
    rig.frames(100);
    CHECK(rig.screen.nudge(0, 4) == 0.5f); // half a step is the limit
    rig.key(STICK_RIGHT, false);
    rig.key(STICK_LEFT, true);
    rig.frames(200);
    CHECK(rig.screen.nudge(0, 4) == -0.5f); // early
    rig.key(STICK_LEFT, false);
    rig.key(A, false);
    CHECK(rig.screen.cursorStep() == 4);

    // The engine plays it early: half a step before step 5.
    rig.tap(START);
    CHECK(rig.listen(3 * 6000 + 2990) == 0.f);
    CHECK(rig.listen(20) > 0.f);
}

void testTheStickDoesOneJobPerPush() {
    // Pushed up to set the level, a drift sideways must not also nudge. And
    // the other way about.
    Rig rig;
    rig.screen.setStep(0, 0, 0.5f);
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(2);
    rig.key(STICK_RIGHT, true); // now it is up and right at once
    rig.frames(4);
    CHECK(rig.screen.nudge(0, 0) == 0.f);
    CHECK(rig.screen.level(0, 0) > 0.7f);
    rig.key(STICK_UP, false);
    rig.frames(4); // still held right, but that push began as "up"
    CHECK(rig.screen.nudge(0, 0) == 0.f);
    rig.key(STICK_RIGHT, false);
    rig.frames(1); // back to the centre: free to choose again

    const float level = rig.screen.level(0, 0);
    rig.key(STICK_RIGHT, true);
    rig.frames(2);
    rig.key(STICK_UP, true);
    rig.frames(4);
    CHECK(rig.screen.nudge(0, 0) > 0.1f);
    CHECK(rig.screen.level(0, 0) == level);
}

void testYSetsTheChance() {
    Rig rig;
    // Y on an empty step places a hit that plays half the time, and you hear it.
    rig.tap(Y);
    CHECK(near(rig.screen.level(0, 0), 0.8f));
    CHECK(near(rig.screen.chance(0, 0), 0.5f));
    CHECK(rig.listen() > 0.2f);
    // Tapping Y on a hit steps its chance down by quarters, and round again.
    rig.tap(Y);
    CHECK(near(rig.screen.chance(0, 0), 0.25f));
    rig.tap(Y);
    CHECK(rig.screen.chance(0, 0) == 1.f);
    rig.tap(Y);
    CHECK(near(rig.screen.chance(0, 0), 0.75f));
    rig.tap(Y);
    CHECK(near(rig.screen.chance(0, 0), 0.5f));

    // Hold Y + stick: smooth, and letting go afterwards is not a tap.
    rig.key(Y, true);
    rig.key(STICK_UP, true);
    rig.frames(4);
    CHECK(near(rig.screen.chance(0, 0), 0.5f + 4 * kTurn));
    rig.key(STICK_UP, false);
    rig.key(Y, false);
    CHECK(near(rig.screen.chance(0, 0), 0.5f + 4 * kTurn));
    rig.key(Y, true);
    rig.key(STICK_DOWN, true);
    rig.frames(200);
    rig.key(STICK_DOWN, false);
    CHECK(near(rig.screen.chance(0, 0), 0.05f)); // never to "never": B deletes
    // Hold Y + D-pad: tenths.
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    CHECK(near(rig.screen.chance(0, 0), 0.2f));
    rig.key(Y, false);
    CHECK(near(rig.screen.chance(0, 0), 0.2f));
    // None of that touched the level or the timing, or moved the cursor.
    CHECK(near(rig.screen.level(0, 0), 0.8f) && rig.screen.nudge(0, 0) == 0.f);
    CHECK(rig.screen.cursorLane() == 0 && rig.screen.cursorStep() == 0);

    // Y held with the stick sideways does nothing.
    rig.key(Y, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(10);
    rig.key(STICK_UP, true); // drifting up during a sideways push changes nothing either
    rig.frames(10);
    rig.key(STICK_UP, false);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(rig.screen.nudge(0, 0) == 0.f && near(rig.screen.chance(0, 0), 0.2f));
    rig.key(STICK_UP, true); // a tiny turn, so letting go is not a tap
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.key(Y, false);
}

void testTheEngineRollsTheChance() {
    Rig rig;
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN);
    rig.tap(SDLK_DOWN); // the rim: short, so each bar ends in silence
    rig.tap(Y);         // a maybe
    rig.listen(40000);
    rig.tap(START);
    int played = 0;
    for (int bar = 0; bar < 100; ++bar) played += rig.listen(96000) > 0.f ? 1 : 0;
    CHECK(played > 30 && played < 70);
}

void testDeletingAHitLeavesNothingBehind() {
    Rig rig;
    rig.tap(Y); // a maybe
    rig.key(A, true);
    rig.tap(SDLK_RIGHT); // nudged late
    rig.tap(SDLK_DOWN);  // and quieter
    rig.key(A, false);
    CHECK(rig.screen.chance(0, 0) < 1.f && rig.screen.nudge(0, 0) > 0.f && rig.screen.level(0, 0) < 0.8f);
    rig.tap(B);
    rig.tap(A); // a fresh hit on the same step is a plain one
    CHECK(near(rig.screen.level(0, 0), 0.8f));
    CHECK(rig.screen.nudge(0, 0) == 0.f);
    CHECK(rig.screen.chance(0, 0) == 1.f);
    // And the engine agrees: on the grid, every bar.
    rig.listen(40000);
    rig.tap(START);
    CHECK(rig.listen(10) > 0.f);
    for (int bar = 0; bar < 20; ++bar) {
        rig.listen(96000 - 10);
        CHECK(rig.listen(10) > 0.f);
    }
    // B does nothing while Y is held.
    rig.key(Y, true);
    rig.tap(B);
    CHECK(rig.screen.level(0, 0) > 0.f);
}

void testYDoesNothingOnTheTempo() {
    Rig rig;
    rig.tap(SDLK_UP); // the tempo
    CHECK(rig.screen.cursorOnTempo());
    rig.tap(Y);
    rig.key(Y, true);
    rig.key(STICK_UP, true);
    rig.frames(20);
    rig.key(STICK_UP, false);
    rig.tap(SDLK_UP);
    rig.key(Y, false);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) CHECK(rig.screen.level(lane, step) == 0.f);
    CHECK(rig.listen() == 0.f);
    CHECK(rig.screen.tempo() == 120.f);
}

// The steps of a lane that hold a hit, as sixteen bits.
unsigned hitsOn(const Rig& rig, int lane) {
    unsigned bits = 0;
    for (int step = 0; step < kSteps; ++step) bits |= rig.screen.level(lane, step) > 0.f ? 1u << step : 0u;
    return bits;
}
int countOf(unsigned bits) { int n = 0; for (; bits; bits &= bits - 1) ++n; return n; }

void testTapYOnALaneNameRollsTheDice() {
    Rig rig;
    rig.screen.moveCursorTo(2, -1); // the closed hat's name
    rig.tap(Y);
    const unsigned first = hitsOn(rig, 2);
    CHECK(countOf(first) == 4);
    CHECK(rig.screen.diceHits(2) == 4);
    for (int step = 0; step < kSteps; ++step) {
        if (!(first >> step & 1)) continue;
        CHECK(rig.screen.fromDice(2, step));
        CHECK(rig.screen.level(2, step) >= 0.5f && rig.screen.level(2, step) <= 1.f);
        CHECK(rig.screen.nudge(2, step) == 0.f && rig.screen.chance(2, step) == 1.f);
    }
    const float heard = rig.listen();
    CHECK(heard > 0.05f); // you hear it land
    CHECK(heard < 0.6f);  // as one hit, not all four fired at once
    // Only that lane.
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(lane == 2 || hitsOn(rig, lane) == 0u);

    // Tap again: four hits still, somewhere new.
    rig.tap(Y);
    CHECK(countOf(hitsOn(rig, 2)) == 4 && rig.screen.diceHits(2) == 4);
    CHECK(hitsOn(rig, 2) != first);
    // And the engine has the new ones, not the old.
    rig.listen(40000);
    rig.tap(START);
    const unsigned now = hitsOn(rig, 2);
    for (int step = 0; step < kSteps; ++step) CHECK((rig.listen(6000) > 0.f) == ((now >> step & 1) != 0));
}

void testHoldYOnALaneNameFillsAndDrains() {
    Rig rig;
    rig.screen.moveCursorTo(2, -1);
    rig.key(Y, true);
    // Exact steps with the D-pad: one hit more each time.
    unsigned trail[6];
    for (int i = 0; i < 6; ++i) {
        rig.tap(SDLK_UP);
        trail[i] = hitsOn(rig, 2);
        CHECK(countOf(trail[i]) == i + 1);
        if (i > 0) CHECK((trail[i] & trail[i - 1]) == trail[i - 1]); // the earlier hits stay put
    }
    // Back down retraces the same steps, last in first out.
    for (int i = 4; i >= 0; --i) {
        rig.tap(SDLK_DOWN);
        CHECK(hitsOn(rig, 2) == trail[i]);
    }
    // And up again brings the same hits back, each at the level it had.
    for (int i = 1; i < 6; ++i) rig.tap(SDLK_UP);
    CHECK(hitsOn(rig, 2) == trail[5]);
    float levels[kSteps];
    for (int step = 0; step < kSteps; ++step) levels[step] = rig.screen.level(2, step);
    for (int i = 0; i < 5; ++i) rig.tap(SDLK_DOWN);
    for (int i = 0; i < 5; ++i) rig.tap(SDLK_UP);
    for (int step = 0; step < kSteps; ++step) CHECK(rig.screen.level(2, step) == levels[step]);
    for (int i = 0; i < 5; ++i) rig.tap(SDLK_DOWN);
    CHECK(hitsOn(rig, 2) == trail[0]);

    // The stick is fast: a full lane in under a second, and it stops there.
    rig.key(STICK_UP, true);
    rig.frames(50);
    CHECK(countOf(hitsOn(rig, 2)) == kSteps && rig.screen.diceHits(2) == kSteps);
    rig.frames(50);
    rig.key(STICK_UP, false);
    rig.key(STICK_DOWN, true);
    rig.frames(100);
    rig.key(STICK_DOWN, false);
    CHECK(hitsOn(rig, 2) == 0u && rig.screen.diceHits(2) == 0);
    // Letting go of Y after all that is not a tap: no roll.
    rig.key(Y, false);
    CHECK(hitsOn(rig, 2) == 0u);
    CHECK(rig.screen.cursorLane() == 2 && rig.screen.cursorStep() == -1);
    CHECK(rig.screen.laneLevel(2) == 1.f); // Y never touches the drum's volume
}

void testSteppingTheDiceIsNotATap() {
    // Hold Y, add two hits with the D-pad, let go. Letting go must not roll.
    Rig rig;
    rig.screen.moveCursorTo(3, -1);
    rig.key(Y, true);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP);
    const unsigned placed = hitsOn(rig, 3);
    rig.key(Y, false);
    CHECK(countOf(placed) == 2);
    CHECK(hitsOn(rig, 3) == placed);
}

void testTheDiceNeverTakeYourHits() {
    Rig rig;
    // Two hits of the player's own.
    rig.screen.setStep(2, 5, 0.33f);
    rig.screen.setStep(2, 0, 0.44f);
    rig.screen.setNudge(2, 5, 0.2f);
    rig.screen.moveCursorTo(2, -1);
    rig.key(Y, true);
    rig.key(STICK_UP, true);
    rig.frames(100); // fill the lane around them
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(countOf(hitsOn(rig, 2)) == kSteps);
    CHECK(rig.screen.diceHits(2) == kSteps - 2);
    CHECK(near(rig.screen.level(2, 5), 0.33f) && near(rig.screen.level(2, 0), 0.44f)); // not overwritten
    rig.key(Y, false);

    // Shape one of the dice's hits: now it is the player's too.
    int adopted = -1;
    for (int step = 0; step < kSteps && adopted < 0; ++step) if (rig.screen.fromDice(2, step)) adopted = step;
    rig.screen.moveCursorTo(2, adopted);
    rig.key(A, true);
    rig.tap(SDLK_DOWN);
    rig.key(A, false);
    CHECK(!rig.screen.fromDice(2, adopted));
    const float shaped = rig.screen.level(2, adopted);
    // So is one given a chance with Y, and one painted over.
    int diced = -1;
    for (int step = 0; step < kSteps && diced < 0; ++step) if (rig.screen.fromDice(2, step)) diced = step;
    rig.screen.moveCursorTo(2, diced);
    rig.tap(Y);
    CHECK(!rig.screen.fromDice(2, diced));
    int painted = -1;
    for (int step = 0; step < kSteps && painted < 0; ++step) if (rig.screen.fromDice(2, step)) painted = step;
    rig.screen.moveCursorTo(2, painted);
    rig.key(R1, true);
    rig.key(R2, true); // the random brush
    rig.key(R2, false);
    rig.key(R1, false);
    CHECK(!rig.screen.fromDice(2, painted));

    // Drain the dice all the way, then roll: the five that are the player's stay.
    rig.screen.moveCursorTo(2, -1);
    rig.key(Y, true);
    rig.key(STICK_DOWN, true);
    rig.frames(100);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    rig.key(Y, false);
    const unsigned mine = 1u << 5 | 1u << 0 | 1u << adopted | 1u << diced | 1u << painted;
    CHECK(hitsOn(rig, 2) == mine);
    CHECK(near(rig.screen.level(2, 5), 0.33f) && near(rig.screen.nudge(2, 5), 0.2f));
    CHECK(near(rig.screen.level(2, adopted), shaped));
    rig.tap(Y);
    CHECK((hitsOn(rig, 2) & mine) == mine);
    CHECK(rig.screen.diceHits(2) == 4);
    // A dice hit deleted with B is simply gone; the count follows.
    int doomed = -1;
    for (int step = 0; step < kSteps && doomed < 0; ++step) if (rig.screen.fromDice(2, step)) doomed = step;
    rig.screen.moveCursorTo(2, doomed);
    rig.tap(B);
    CHECK(rig.screen.diceHits(2) == 3 && rig.screen.level(2, doomed) == 0.f);
}

void testTheDiceLeanTowardTheBeat() {
    // Four hits a roll. If every step were as likely, a quarter of them would
    // land on the four quarter notes. The shuffle is weighted, so far more do.
    Rig rig;
    rig.tap(START); // playing, so the rolls are silent
    rig.screen.moveCursorTo(0, -1);
    int onBeat = 0, total = 0;
    unsigned seen = 0;
    for (int roll = 0; roll < 100; ++roll) {
        rig.tap(Y);
        const unsigned bits = hitsOn(rig, 0);
        CHECK(countOf(bits) == 4);
        seen |= bits;
        total += 4;
        onBeat += countOf(bits & 0x1111u);
    }
    CHECK(onBeat * 100 / total > 45);
    CHECK(onBeat * 100 / total < 90); // but not only the beat
    CHECK(seen == 0xFFFFu);           // every step turns up sooner or later
}

void testEachLaneHasItsOwnDice() {
    Rig rig;
    rig.screen.moveCursorTo(0, -1);
    rig.tap(Y);
    rig.screen.moveCursorTo(1, -1);
    rig.tap(Y);
    rig.screen.moveCursorTo(2, -1);
    rig.tap(Y);
    CHECK(!(hitsOn(rig, 0) == hitsOn(rig, 1) && hitsOn(rig, 1) == hitsOn(rig, 2)));
    // Rolling one lane leaves the others as they were.
    const unsigned kick = hitsOn(rig, 0), snare = hitsOn(rig, 1);
    rig.tap(Y);
    CHECK(hitsOn(rig, 0) == kick && hitsOn(rig, 1) == snare);
}

void testR1PaintsARowAtOneLevel() {
    Rig rig;
    // Starting on an empty step, the brush is 80%. The first hit lands at once.
    rig.key(R1, true);
    CHECK(rig.screen.painting());
    CHECK(near(rig.screen.level(0, 0), 0.8f));
    CHECK(rig.listen() > 0.2f); // and is heard, since the song is stopped
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    for (int step = 0; step <= 3; ++step) CHECK(near(rig.screen.level(0, step), 0.8f));
    CHECK(rig.screen.level(0, 4) == 0.f);
    // It paints down a column too.
    rig.tap(SDLK_DOWN);
    CHECK(near(rig.screen.level(1, 3), 0.8f));
    // Let go and the cursor is just a cursor again.
    rig.key(R1, false);
    CHECK(!rig.screen.painting());
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    CHECK(rig.screen.level(1, 4) == 0.f && rig.screen.level(1, 5) == 0.f);

    // The engine has them: four kicks in a row from the top.
    rig.listen(40000);
    rig.tap(START);
    for (int step = 0; step < 4; ++step) CHECK(rig.listen(6000) > 0.2f);
}

void testR1CopiesTheLevelItStartsOn() {
    Rig rig;
    rig.screen.setStep(2, 0, 0.3f);
    // A hit already in the way, with its own timing and chance.
    rig.screen.setStep(2, 2, 1.f);
    rig.screen.setNudge(2, 2, 0.25f);
    rig.screen.setChance(2, 2, 0.5f);
    rig.screen.moveCursorTo(2, 0);
    rig.key(R1, true);
    CHECK(near(rig.screen.level(2, 0), 0.3f)); // the hit it started on is left as it was
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_RIGHT);
    rig.key(R1, false);
    for (int step = 0; step <= 3; ++step) CHECK(near(rig.screen.level(2, step), 0.3f));
    // The hit in the way took the level and kept the rest.
    CHECK(near(rig.screen.nudge(2, 2), 0.25f) && near(rig.screen.chance(2, 2), 0.5f));
    // A new hit is a plain one.
    CHECK(rig.screen.nudge(2, 1) == 0.f && rig.screen.chance(2, 1) == 1.f);

    // The next sweep from an empty step is back at 80%, not the last brush.
    rig.screen.moveCursorTo(3, 0);
    rig.key(R1, true);
    rig.tap(SDLK_RIGHT);
    rig.key(R1, false);
    CHECK(near(rig.screen.level(3, 0), 0.8f) && near(rig.screen.level(3, 1), 0.8f));
}

void testPaintingRunsWithTheStick() {
    Rig rig;
    rig.key(R1, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(26); // under half a second
    rig.key(STICK_RIGHT, false);
    rig.key(R1, false);
    const int reached = rig.screen.cursorStep();
    CHECK(reached >= 12); // most of the row
    for (int step = 0; step <= reached; ++step) CHECK(near(rig.screen.level(0, step), 0.8f)); // and no step skipped
    // A held D-pad paints as it repeats.
    rig.screen.moveCursorTo(4, 0);
    rig.key(R1, true);
    rig.key(SDLK_RIGHT, true);
    rig.frames(30);
    rig.key(SDLK_RIGHT, false);
    rig.key(R1, false);
    for (int step = 0; step <= rig.screen.cursorStep(); ++step) CHECK(near(rig.screen.level(4, step), 0.8f));
    CHECK(rig.screen.cursorStep() >= 8);
}

void testTheBrushSkipsNamesAndTempo() {
    Rig rig;
    rig.tap(SDLK_LEFT); // onto the lane's name
    rig.key(R1, true);  // nothing to paint here
    rig.tap(SDLK_LEFT); // round to step 16: painted, at 80%
    CHECK(near(rig.screen.level(0, 15), 0.8f));
    rig.tap(SDLK_RIGHT); // back over the name
    rig.tap(SDLK_RIGHT); // step 1
    CHECK(near(rig.screen.level(0, 0), 0.8f));
    rig.listen(40000); // let those ring out
    rig.tap(SDLK_UP);  // the tempo: nothing is painted from up here
    CHECK(rig.screen.cursorOnTempo());
    CHECK(rig.listen() == 0.f);
    rig.tap(SDLK_UP); // round to the bottom lane
    CHECK(near(rig.screen.level(kDrumLanes - 1, 0), 0.8f));
    rig.key(R1, false);
    CHECK(rig.screen.tempo() == 120.f && rig.screen.laneLevel(0) == 1.f);
    int hits = 0;
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) hits += rig.screen.level(lane, step) > 0.f ? 1 : 0;
    CHECK(hits == 3);
}

void testR1WithR2PaintsRandomLevels() {
    Rig rig;
    rig.key(R1, true);
    rig.key(R2, true);
    for (int i = 0; i < kSteps - 1; ++i) rig.tap(SDLK_RIGHT);
    rig.key(R2, false);
    rig.key(R1, false);
    float low = 1.f, high = 0.f;
    int different = 0;
    for (int step = 0; step < kSteps; ++step) {
        const float level = rig.screen.level(0, step);
        CHECK(level >= 0.25f && level <= 1.f); // every step has a hit, never a faint one
        low = std::fmin(low, level);
        high = std::fmax(high, level);
        if (step > 0 && level != rig.screen.level(0, step - 1)) ++different;
    }
    CHECK(different >= 13);          // not one level repeated
    CHECK(low < 0.5f && high > 0.8f); // and they spread over the range

    // Dragging over them again rolls new levels and keeps timing and chance.
    float before[kSteps];
    for (int step = 0; step < kSteps; ++step) before[step] = rig.screen.level(0, step);
    rig.screen.setNudge(0, 3, -0.2f);
    rig.screen.setChance(0, 3, 0.25f);
    rig.screen.moveCursorTo(0, 0);
    rig.key(R1, true);
    rig.key(R2, true);
    for (int i = 0; i < kSteps - 1; ++i) rig.tap(SDLK_RIGHT);
    rig.key(R2, false);
    rig.key(R1, false);
    int changed = 0;
    for (int step = 0; step < kSteps; ++step) changed += rig.screen.level(0, step) != before[step] ? 1 : 0;
    CHECK(changed >= 13);
    CHECK(near(rig.screen.nudge(0, 3), -0.2f) && near(rig.screen.chance(0, 3), 0.25f));

    // The engine plays the levels it was given: a quiet step is quieter.
    int quiet = 0, loud = 0;
    for (int step = 1; step < kSteps; ++step) {
        if (rig.screen.level(0, step) < rig.screen.level(0, quiet)) quiet = step;
        if (rig.screen.level(0, step) > rig.screen.level(0, loud)) loud = step;
    }
    Rig a, b;
    a.screen.setStep(1, 0, rig.screen.level(0, quiet));
    b.screen.setStep(1, 0, rig.screen.level(0, loud));
    a.tap(START);
    b.tap(START);
    CHECK(a.listen() < 0.7f * b.listen());
}

void testPaintWaitsItsTurn() {
    // A brush cannot start while A or Y is shaping a hit.
    Rig rig;
    rig.screen.setStep(0, 0, 0.5f);
    rig.key(A, true);
    rig.key(R1, true);
    CHECK(!rig.screen.painting());
    rig.tap(SDLK_RIGHT); // with A held this is still the nudge
    CHECK(rig.screen.cursorStep() == 0 && rig.screen.level(0, 1) == 0.f);
    rig.key(R1, false);
    rig.key(A, false);
    rig.key(Y, true);
    rig.key(R1, true);
    rig.key(R2, true);
    CHECK(!rig.screen.painting() && !rig.screen.paintingRandom());
    rig.key(R2, false);
    rig.key(R1, false);
    rig.key(Y, false);
}

// The random brush is R1's brush with R2 held as well: random for as long as
// R2 is down, R1's one level again when it comes up. R2 by itself is not a
// brush at all (it is "next screen", which the app sees to).
void testRandomIsR1WithR2() {
    Rig rig;
    rig.screen.moveCursorTo(3, 0);
    // R2 alone: nothing here, however long, dragged or not.
    rig.key(R2, true);
    rig.frames(30);
    rig.tap(SDLK_RIGHT);
    rig.key(R2, false);
    CHECK(!rig.screen.painting() && rig.screen.level(3, 0) == 0.f && rig.screen.level(3, 1) == 0.f);
    // R1: one level. R2 added: the step under the cursor is rolled at once, and so is each one crossed.
    rig.screen.moveCursorTo(3, 4);
    rig.key(R1, true);
    rig.tap(SDLK_RIGHT);
    CHECK(near(rig.screen.level(3, 4), 0.8f) && near(rig.screen.level(3, 5), 0.8f) && !rig.screen.paintingRandom());
    rig.key(R2, true);
    CHECK(rig.screen.paintingRandom() && rig.screen.painting());
    const float rolled = rig.screen.level(3, 5);
    for (int i = 0; i < 8; ++i) rig.tap(SDLK_RIGHT);
    int offEighty = !near(rolled, 0.8f) ? 1 : 0;
    for (int step = 6; step <= 13; ++step) {
        CHECK(rig.screen.level(3, step) >= 0.25f);
        offEighty += !near(rig.screen.level(3, step), 0.8f) ? 1 : 0;
    }
    CHECK(offEighty >= 7); // random levels, not R1's 80%
    rig.frames(60);
    CHECK(rig.screen.level(3, 13) >= 0.25f && rig.screen.level(3, 14) == 0.f); // holding still paints nothing more
    // R2 up: R1's own brush again, at the level it began with.
    rig.key(R2, false);
    CHECK(rig.screen.painting() && !rig.screen.paintingRandom());
    rig.tap(SDLK_RIGHT);
    CHECK(near(rig.screen.level(3, 14), 0.8f));
    // And back to random, as often as wanted.
    rig.key(R2, true);
    CHECK(rig.screen.paintingRandom());
    // R1 up while R2 is still down: the brush is lifted altogether; R2 coming up after does nothing.
    rig.key(R1, false);
    CHECK(!rig.screen.painting());
    rig.tap(SDLK_RIGHT);
    CHECK(rig.screen.level(3, 15) == 0.f);
    rig.key(R2, false);
    CHECK(!rig.screen.painting());
    // L1 is no brush.
    rig.screen.moveCursorTo(6, 2);
    rig.key(L1, true);
    rig.tap(SDLK_RIGHT);
    rig.key(L1, false);
    CHECK(!rig.screen.painting() && rig.screen.level(6, 2) == 0.f && rig.screen.level(6, 3) == 0.f);
}

void testPaintingWhilePlayingIsNotDoubled() {
    Rig rig;
    rig.tap(START);
    rig.listen(1000);
    rig.screen.moveCursorTo(6, 8); // the rim, half a bar ahead of the play position
    rig.key(R1, true);
    rig.tap(SDLK_RIGHT);
    rig.key(R1, false);
    CHECK(rig.listen(40000) == 0.f); // nothing fired by hand
    CHECK(rig.listen(12000) > 0.2f); // the loop reaches them
}

} // namespace

// A ratchet's dark lines cut a note's first step into even pieces, as many as it plays.
void testRatchetLinesCutEvenPieces() {
    int tops[3] = {-1, -1, -1};
    // A one-step note on the piano roll is 17 pixels tall.
    CHECK(ratchetLines(17, 2, tops) == 3 && tops[0] == 7);                                  // 7 | 7
    CHECK(ratchetLines(17, 3, tops) == 3 && tops[0] == 4 && tops[1] == 10);                 // 4 | 3 | 4
    CHECK(ratchetLines(17, 4, tops) == 3 && tops[0] == 2 && tops[1] == 7 && tops[2] == 12); // four of 2
    // Half a step: thinner lines when thick ones would leave pieces under two pixels.
    CHECK(ratchetLines(7, 2, tops) == 3 && tops[0] == 2);
    CHECK(ratchetLines(7, 3, tops) == 0);
    CHECK(ratchetLines(8, 3, tops) == 1 && tops[0] == 2 && tops[1] == 5);
    CHECK(ratchetLines(10, 3, tops) == 2 && tops[0] == 2 && tops[1] == 6);
    CHECK(ratchetLines(5, 2, tops) == 1 && tops[0] == 2);
    CHECK(ratchetLines(4, 2, tops) == 0);
    // Not a ratchet: nothing is drawn and nothing is written.
    int kept[3] = {-7, -7, -7};
    CHECK(ratchetLines(17, 1, kept) == 0 && ratchetLines(17, 0, kept) == 0 && ratchetLines(17, 5, kept) == 0);
    CHECK(kept[0] == -7 && kept[1] == -7 && kept[2] == -7);
    // Whatever the height: the right number of pieces, none under two pixels, none more than a
    // pixel taller than another, the whole height used, and the same from both ends where it can be.
    for (int tall = 1; tall <= 48; ++tall) {
        for (int pieces = 2; pieces <= 4; ++pieces) {
            const int thick = ratchetLines(tall, pieces, tops);
            // The thickest lines that leave two pixels a piece.
            int want = 0;
            for (int t = 3; t >= 1 && !want; --t)
                if (tall - t * (pieces - 1) >= 2 * pieces) want = t;
            CHECK(thick == want);
            if (!thick) continue;
            int size[4], y = 0, least = 99, most = 0;
            for (int i = 0; i < pieces; ++i) {
                const int end = i < pieces - 1 ? tops[i] : tall;
                size[i] = end - y;
                y = end + thick;
                least = size[i] < least ? size[i] : least;
                most = size[i] > most ? size[i] : most;
            }
            CHECK(least >= 2);
            CHECK(most - least <= 1);
            const int light = tall - thick * (pieces - 1);
            if (pieces % 2 == 1 || (light % pieces) % 2 == 0)
                for (int i = 0; i < pieces; ++i) CHECK(size[i] == size[pieces - 1 - i]);
        }
    }
}

// The right end of the piano roll's strip: NUDGE, then what the note does (RATCHET / EFFECT),
// then THROW. A nudged, ratcheted, thrown note had its NUDGE drawn over its RATCHET.
static void testNoteStripNudgeFits() {
    // "45% EARLY" in the three sizes, about as the fonts measure it.
    const float widths[3] = {110.f, 93.5f, 80.f};
    // Nothing after it: the value is as large as ever, whatever its width.
    CHECK(stripNudgeRoom(false, false) > 200.f && stripNudgeRoom(false, true) > 150.f);
    CHECK(stripFont(widths, stripNudgeRoom(false, false)) == 0);
    // THROW alone stands at 575 and is not in NUDGE's way.
    CHECK(stripFont(widths, stripNudgeRoom(false, true)) == 0);
    // An effect after it, not thrown: the field is at 510, as it always was, and NUDGE ends before it.
    CHECK(stripEffectX(false) == 510.f && stripEffectX(true) == 480.f && kStripThrowX == 575.f && kStripNudgeX == 390.f);
    CHECK(stripNudgeRoom(true, false) == 110.f);
    CHECK(stripFont(widths, stripNudgeRoom(true, false)) == 0);
    // Thrown as well: the effect moves left to make room for THROW, and NUDGE is drawn smaller.
    CHECK(stripNudgeRoom(true, true) == 80.f);
    const int font = stripFont(widths, stripNudgeRoom(true, true));
    CHECK(font == 2);
    CHECK(kStripNudgeX + widths[font] + kStripSpace <= stripEffectX(true));
    // A short one ("5% LATE") stays large there.
    const float small[3] = {78.f, 66.f, 57.f};
    CHECK(stripFont(small, stripNudgeRoom(true, true)) == 0);
    // The middle size is taken when it fits and the large does not; exactly fitting is fitting.
    const float mid[3] = {95.f, 80.f, 69.f};
    CHECK(stripFont(mid, stripNudgeRoom(true, true)) == 1);
    CHECK(stripFont(mid, 95.f) == 0 && stripFont(mid, 94.9f) == 1 && stripFont(mid, 79.9f) == 2);
    // Nothing fits: the smallest there is.
    CHECK(stripFont(widths, 10.f) == 2);
}

// THROW's value at the strip's right end stops at the margin (632), as every other field does.
// "SPACE" at 575 in the large size ran to pixel 638 of 640.
// Hints are written in full; the bar draws two words shorter: HOLD as HLD, SELECT as SEL.
static void testTheBarDrawsSelForSelect() {
    CHECK(ui::keyShown("SELECT") == "SEL");
    CHECK(ui::keyShown("SELECT + Y") == "SEL + Y");
    CHECK(ui::keyShown("HOLD SELECT") == "HLD SEL");
    CHECK(ui::keyShown("SELECT, tapped") == "SEL, tapped"); // the help card: what follows the word stays
    CHECK(ui::keyShown("SELECT+X") == "SEL+X");
    CHECK(ui::keyShown("SELECTED") == "SELECTED" && ui::keyShown("SELECTS A") == "SELECTS A"); // another word is left alone
    CHECK(ui::keyShown("RESELECT") == "RESELECT" && ui::keyShown("SELLER") == "SELLER" && ui::keyShown("SEL") == "SEL");
    CHECK(ui::keyShown("HOLD A") == "HLD A" && ui::keyShown("L2  R2") == "L2 R2" && ui::keyShown("START") == "START");
    CHECK(ui::keyShown("UP / DOWN") == "UP DOWN" && ui::keyShown("A / B") == "A / B"); // arrows stand side by side
}

// The same, seen on the screen: the bar itself draws SEL, in a box as wide as SEL.
// (keyShown above is the rule; this is the bar using it.)
static void testTheBarDrawsSelOnTheScreen() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Fonts& fonts = *canvas.fonts;
    const auto drawn = [&](const char* key) {
        const ui::Hint list[] = {{key, "songs"}, {"A", "open"}};
        canvas.clear();
        ui::hints(canvas.r, fonts, list, 2);
        return canvas.all();
    };
    const std::vector<Uint32> full = drawn("SELECT"), shortened = drawn("SEL");
    CHECK(full == shortened);
    CHECK(drawn("SELECT + Y") == drawn("SEL + Y") && drawn("HOLD SELECT") == drawn("HLD SEL"));
    CHECK(full != drawn("SELEC") && full != drawn("SE") && full != drawn("SEL + Y")); // the canvas can tell words apart
    // The box: cream, starting at 8, the word and four pixels each side.
    drawn("SELECT");
    const int top = ui::kScreenH - ui::kHintsH + 5;
    CHECK(canvas.run(8, top, ui::kText) == static_cast<int>(fonts.small.width("SEL") + 8.f));
    CHECK(canvas.run(8, top, ui::kText) < static_cast<int>(fonts.small.width("SELECT") + 8.f));
    // And the letters in it are SEL's own, drawn plainly.
    const int w = canvas.run(8, top, ui::kText), h = ui::kHintsH - 9;
    const std::vector<Uint32> box = canvas.part(8, top, w, h);
    canvas.clear();
    ui::fill(canvas.r, 8, top, w, h, ui::kText);
    fonts.small.draw(canvas.r, 12.f, ui::kScreenH - ui::kHintsH + (ui::kHintsH - fonts.small.capHeight()) / 2.f + 1.f, "SEL", ui::kBg);
    CHECK(box == canvas.part(8, top, w, h));
}

static void testNoteStripThrowFits() {
    const float space[3] = {63.f, 53.5f, 46.f}, both[3] = {51.f, 43.f, 37.f}; // about as the fonts measure them
    float x = 0.f;
    // Short enough: where it always was, as large as ever.
    CHECK(stripThrowFont(both, 0.f, &x) == 0 && x == kStripThrowX);
    CHECK(stripThrowFont(both, 577.f, &x) == 0 && x == kStripThrowX); // (an effect's end never pushes it right)
    const float exact[3] = {kStripRight - kStripThrowX, 40.f, 30.f};
    CHECK(stripThrowFont(exact, 0.f, &x) == 0 && x == kStripThrowX);  // exactly fitting is fitting
    // SPACE with nothing before it: moved left to end at the margin, still large.
    CHECK(stripThrowFont(space, 0.f, &x) == 0 && x == 569.f && x + space[0] == kStripRight);
    // A short effect before it ("x4", ending near 507): the same.
    CHECK(stripThrowFont(space, 517.f, &x) == 0 && x == 569.f);
    // A long one ("x4 GLIDE" ends at 567, and a gap): it cannot move, and is drawn in the middle size.
    CHECK(stripThrowFont(space, 577.f, &x) == 1 && x == kStripThrowX && x + space[1] <= kStripRight);
    // Room to move a little, not enough for the large size: the middle size, where it always was.
    CHECK(stripThrowFont(space, 572.f, &x) == 1 && x == kStripThrowX);
    // The middle size too wide where it stands: it moves left as far as it may before going smaller still.
    const float wide[3] = {90.f, 60.f, 50.f};
    CHECK(stripThrowFont(wide, 572.f, &x) == 1 && x == 572.f && x + wide[1] == kStripRight);
    CHECK(stripThrowFont(wide, 573.f, &x) == 2 && x == kStripThrowX);
    // Nothing fits: the smallest, as far left as it may go.
    const float huge[3] = {200.f, 150.f, 100.f};
    CHECK(stripThrowFont(huge, 560.f, &x) == 2 && x == 560.f);
}

int main(int, char**) {
    testNoteStripNudgeFits();
    testNoteStripThrowFits();
    testTheBarDrawsSelForSelect();
    testTheBarDrawsSelOnTheScreen();
    ui::gStickSpeed = 1.f; // the checks below are of each screen's own speeds; the overall scale has its own check
    testCursorMovesAndWraps();
    testHeldDirectionRunsFast();
    testStickRunsTheCursor();
    testAStalledFrameDoesNotFlingTheCursor();
    testAPlacesAHitAndPlaysIt();
    testHoldAAndStickFillsTheHit();
    testHoldAAndDpadStepsTheLevel();
    testBDeletes();
    testTheEngineHearsTheLevel();
    testStartPlaysAndStops();
    testPlacingWhilePlayingIsNotDoubled();
    testXHearsTheLane();
    testTempoIsUnderTheCursor();
    testSelectNoLongerChangesTempo();
    testLaneNameIsTheDrumsVolume();
    testHoldAAndStickSidewaysNudges();
    testTheStickDoesOneJobPerPush();
    testYSetsTheChance();
    testTheEngineRollsTheChance();
    testDeletingAHitLeavesNothingBehind();
    testYDoesNothingOnTheTempo();
    testTapYOnALaneNameRollsTheDice();
    testHoldYOnALaneNameFillsAndDrains();
    testSteppingTheDiceIsNotATap();
    testTheDiceNeverTakeYourHits();
    testTheDiceLeanTowardTheBeat();
    testEachLaneHasItsOwnDice();
    testR1PaintsARowAtOneLevel();
    testR1CopiesTheLevelItStartsOn();
    testPaintingRunsWithTheStick();
    testTheBrushSkipsNamesAndTempo();
    testR1WithR2PaintsRandomLevels();
    testRandomIsR1WithR2();
    testPaintWaitsItsTurn();
    testPaintingWhilePlayingIsNotDoubled();
    testRatchetLinesCutEvenPieces();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("screen tests passed\n");
    return 0;
}
