// FOLLOW (app/follow_bar.hpp): while the song plays the scene on screen, the drum grid and the piano roll
// show each bar of the cell as the playhead comes into it. Not while a button is held, nor for
// kFollowRestMs after; not while stopped; not for another scene. The rule by itself, then both grids
// through the real App with the engine playing a two-bar cell.

#include <SDL.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "follow_bar.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

const SDL_Keycode B = SDLK_z;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().view = View::Pattern;
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    // A frame of the program and 16 ms of sound, as on the handheld.
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 768);
            engine->render(out.data(), 768);
        }
    }
    // Frames until the engine plays bar `bar` of the cell (at most `most` frames); true if it did.
    bool untilBar(int bar, int most = 400) {
        for (int i = 0; i < most; ++i) {
            frames(1);
            if (engine->playStep() >= 0 && engine->playBar() % 2 == bar) return true;
        }
        return false;
    }
    Project& project() { return app.project(); }
};

void testTheRule() {
    BarFollow f;
    CHECK(f.step(false, -1, false, 0) == -1);   // stopped: nothing
    CHECK(f.step(true, 0, false, 100) == 0);    // playing starts: its bar
    CHECK(f.step(true, 0, false, 116) == -1);   // the same bar: nothing more
    CHECK(f.step(true, 1, false, 132) == 1);    // the next bar comes: shown
    CHECK(f.step(true, 1, true, 2000) == -1);   // a button held
    CHECK(f.step(true, 0, false, 2100) == -1);  // a bar entered within the rest after: not followed
    CHECK(f.step(true, 0, false, 3000) == -1);  // the rest over, but the bar was entered already
    CHECK(f.step(true, 1, false, 3600) == 1);   // the next bar entered after the rest: followed
    CHECK(f.step(true, 0, true, 5000) == -1);   // entered while held: not followed
    CHECK(f.step(false, -1, false, 9000) == -1);
    CHECK(f.step(true, 0, false, 9016) == 0);   // played again: its bar at once
}

// The grid shown for the current track (drum grid or roll) follows a two-bar cell.
void followsOn(Rig& rig, const char* what) {
    Session& s = rig.app.session();
    const int track = rig.project().track;
    s.setCellBars(track, 0, 2);
    rig.frames(5);
    rig.project().bar = 1;
    s.togglePlay(false);
    rig.frames(3);
    CHECK(rig.project().bar == 0); // Play: the bar playing, the first
    if (rig.project().bar != 0) std::printf("  (%s: on Play)\n", what);
    CHECK(rig.untilBar(1));
    rig.frames(1);
    CHECK(rig.project().bar == 1);
    if (rig.project().bar != 1) std::printf("  (%s: into bar 2)\n", what);
    CHECK(rig.untilBar(0));
    rig.frames(1);
    CHECK(rig.project().bar == 0);

    // A button held (B on an empty step deletes nothing): the view stays where it is.
    rig.key(B, true);
    CHECK(rig.untilBar(1));
    rig.frames(2);
    CHECK(rig.project().bar == 0);
    if (rig.project().bar != 0) std::printf("  (%s: B held)\n", what);
    rig.key(B, false);
    rig.frames(2);
    CHECK(rig.project().bar == 0); // let go inside the bar: it waits for the next one
    // The next bar comes after the rest (a bar is 2 s at 120 BPM): followed again.
    CHECK(rig.untilBar(0));
    rig.frames(1);
    CHECK(rig.project().bar == 0);
    CHECK(rig.untilBar(1));
    rig.frames(1);
    CHECK(rig.project().bar == 1);

    // Stopped: the view stays.
    s.togglePlay(false);
    rig.frames(5);
    rig.project().bar = 0;
    rig.frames(200);
    CHECK(rig.project().bar == 0);

    // Another scene playing: the view of this one stays.
    rig.project().scene = 1;
    s.setCellBars(track, 1, 2);
    rig.project().scene = 0;
    rig.frames(3);
    rig.project().scene = 1;
    s.togglePlay(false);
    rig.frames(3);
    rig.project().scene = 0;
    rig.project().bar = 1;
    rig.frames(200);
    CHECK(rig.project().bar == 1);
    s.togglePlay(false);
    rig.frames(5);
}

void testTheDrumGrid() {
    Rig rig;
    CHECK(rig.project().tracks[rig.project().track].kind == TrackKind::Drums);
    followsOn(rig, "drum grid");
}

void testTheRoll() {
    Rig rig;
    CHECK(rig.app.session().setTrackKind(rig.project().track, TrackKind::Phazed));
    rig.frames(5);
    followsOn(rig, "roll");
}

} // namespace

int main() {
    testTheRule();
    testTheDrumGrid();
    testTheRoll();
    if (failures) {
        std::printf("follow: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("follow: all passed\n");
    return 0;
}
