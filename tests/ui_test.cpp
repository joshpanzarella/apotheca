// Tests for the Pattern screen's buttons. No window and no sound card: the
// buttons are pressed through the keyboard stand-in, and the engine is
// rendered by hand to hear what the screen told it.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "apotheca/engine.hpp"
#include "input.hpp"
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
    PatternScreen screen{engine};
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

void testYDoesNothingOffTheGrid() {
    Rig rig;
    rig.tap(SDLK_LEFT); // the lane's name
    rig.tap(Y);
    rig.tap(SDLK_UP);
    rig.tap(SDLK_UP); // the tempo
    rig.tap(Y);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) CHECK(rig.screen.level(lane, step) == 0.f);
    CHECK(rig.listen() == 0.f);
}

} // namespace

int main(int, char**) {
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
    testYDoesNothingOffTheGrid();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("screen tests passed\n");
    return 0;
}
