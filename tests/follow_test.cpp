// FOLLOW (app/follow_bar.hpp): while the song plays the scene on screen, the drum grid and the piano roll
// show each bar of the cell as the playhead comes into it. Not while a button is held, nor for
// kFollowRestMs after; not while stopped; not for another scene. The rule by itself, then both grids
// through the real App with the engine playing a two-bar cell.

#include <SDL.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/fill_rule.hpp"
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

// A cell's FILL (fill_rule.hpp): looked at by hand, the view holds still on it, however long the controls rest;
// FOLLOW shows it when it plays, and follows on from it, unless the player works on it meanwhile.
void testTheFillRule() {
    BarFollow f;
    // On the FILL by hand: no bar entered takes the view, rest or not.
    CHECK(f.step(true, 0, false, 100, kFillSlot, true) == -1);         // Play: not its bar
    CHECK(f.step(true, kFillSlot, false, 2100, kFillSlot, true) == -1); // the FILL plays: it is there already
    CHECK(f.step(true, 0, false, 4100, kFillSlot, true) == -1);         // long after any touch: held
    // Moved off by hand: followed as ever from the next bar entered.
    CHECK(f.step(true, 0, false, 4200, 0, false) == -1);                       // (the same bar: nothing)
    CHECK(f.step(true, kFillSlot, false, 6100, 0, false) == kFillSlot);        // FOLLOW shows the FILL as it plays
    CHECK(f.step(true, kFillSlot, false, 6116, kFillSlot, true) == -1);        // (no new bar)
    CHECK(f.step(true, 0, false, 8100, kFillSlot, true) == 0);                 // put there by FOLLOW: followed on
    // Shown by FOLLOW, then worked on (a button held there): held still.
    CHECK(f.step(true, kFillSlot, false, 10100, 0, false) == kFillSlot);
    CHECK(f.step(true, kFillSlot, true, 10200, kFillSlot, true) == -1);
    CHECK(f.step(true, 0, false, 14100, kFillSlot, true) == -1);
    CHECK(f.step(true, kFillSlot, false, 16100, kFillSlot, true) == -1);
    // Shown by FOLLOW, moved off and back by hand (no button counted): held, the player put it there.
    BarFollow g;
    CHECK(g.step(true, kFillSlot, false, 100, 0, false) == kFillSlot);
    CHECK(g.step(true, kFillSlot, false, 116, 0, false) == -1);         // the view on another bar (by hand)
    CHECK(g.step(true, kFillSlot, false, 132, kFillSlot, true) == -1);  // ...and back
    CHECK(g.step(true, 0, false, 4000, kFillSlot, true) == -1);
    // An eighth bar of the cell's own (no FILL) is followed off as any bar.
    BarFollow h;
    CHECK(h.step(true, 0, false, 100, kFillSlot, false) == 0);
}

// Both grids through the App: a one-bar cell with a FILL, REPEATS 2, the scene alone. The FILL plays on every
// second time round: FOLLOW shows it then, and bar 1 after it; the FILL gone to by hand holds the view.
void fillOn(Rig& rig, const char* what) {
    Session& s = rig.app.session();
    const int track = rig.project().track;
    CHECK(s.makeFill(track, 0) == Session::Fill::Made);
    rig.project().repeats[0] = 2;
    Command c;
    c.type = Command::SetSceneRepeats;
    c.a = 2;
    s.engine.send(c);
    rig.project().bar = 0;
    rig.frames(3);
    s.togglePlay(false);
    const auto until = [&](int bar) {
        for (int i = 0; i < 400; ++i) {
            rig.frames(1);
            if (rig.project().playStep >= 0 && rig.project().cellBarPlaying(track) == bar) return true;
        }
        return false;
    };
    CHECK(until(kFillSlot));
    rig.frames(1);
    CHECK(rig.project().bar == kFillSlot); // shown as it plays
    if (rig.project().bar != kFillSlot) std::printf("  (%s: the FILL played)\n", what);
    CHECK(until(0));
    rig.frames(1);
    CHECK(rig.project().bar == 0); // ...and followed on from
    // Gone to by hand: held through two whole times round (eight seconds, no button touched).
    rig.project().bar = kFillSlot;
    rig.frames(500);
    CHECK(rig.project().bar == kFillSlot);
    if (rig.project().bar != kFillSlot) std::printf("  (%s: the FILL by hand)\n", what);
    // Moved off by hand: followed again.
    rig.project().bar = 0;
    CHECK(until(kFillSlot));
    rig.frames(1);
    CHECK(rig.project().bar == kFillSlot);
    s.togglePlay(false);
    rig.frames(5);
}

void testTheFillOnBothGrids() {
    {
        Rig rig;
        fillOn(rig, "drum grid");
    }
    Rig rig;
    CHECK(rig.app.session().setTrackKind(rig.project().track, TrackKind::Phazed));
    rig.frames(5);
    fillOn(rig, "roll");
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


// THE ROLL'S KEYS FOLLOW THE NOTES (2026-11-24): the rule by hand.
void testTheKeysRule() {
    const int lo = 12, hi = 108;
    // A chord C, E, G in a window of 12: all three, and centred on them (60..67: the window 58..69).
    CHECK(keysToShow({{60, 1.f}, {64, 1.f}, {67, 1.f}}, 12, lo, hi, 48) == 58);
    // Two places: three notes low, two high: the three, centred (40..47 in 38..49).
    CHECK(keysToShow({{40, 1.f}, {43, 1.f}, {47, 1.f}, {80, 1.f}, {84, 1.f}}, 12, lo, hi, 70) == 38);
    // As many notes in each: the longer ones (80..83: 76..87).
    CHECK(keysToShow({{40, 1.f}, {44, 1.f}, {80, 4.f}, {83, 4.f}}, 12, lo, hi, 40) == 76);
    // Too wide for the window: the most that fit, centred on those.
    CHECK(keysToShow({{36, 1.f}, {50, 1.f}, {52, 1.f}, {55, 1.f}, {70, 1.f}}, 12, lo, hi, 60) == 47);
    // No notes: the keys stay.
    CHECK(keysToShow({}, 24, lo, hi, 48) == -1);
    // Held to the keyboard's ends: notes at its bottom, and at its top.
    CHECK(keysToShow({{12, 1.f}, {13, 1.f}}, 24, lo, hi, 48) == 12);
    CHECK(keysToShow({{107, 1.f}, {108, 1.f}}, 24, lo, hi, 48) == 108 - 23);
    // One note in a window of 36: in its middle (60 in 43..78: as near the middle as a whole key goes).
    const int one = keysToShow({{60, 1.f}}, 36, lo, hi, 0);
    CHECK(one + 17 <= 60 && 60 <= one + 18);
    // A tie in everything but where the keys are now: the nearest of the windows that centre the notes.
    const int a = keysToShow({{60, 1.f}, {61, 1.f}}, 12, lo, hi, 0), b = keysToShow({{60, 1.f}, {61, 1.f}}, 12, lo, hi, 100);
    CHECK(a == 55 && b == 55); // (60..61 sit exactly in the middle of 55..66, wherever the keys were)
}

// The roll, playing a two-bar cell with its notes low in bar 1 and high in bar 2: the keys go to each bar's
// notes as FOLLOW shows it, and the cursor is in view. Held at the controls, they stay.
void testTheRollsKeys() {
    Rig rig;
    Session& s = rig.app.session();
    const int track = rig.project().track;
    CHECK(s.setTrackKind(track, TrackKind::Phazed));
    s.setCellBars(track, 0, 2);
    rig.frames(5);
    NotePattern& low = rig.project().tracks[track].notes[0][0];
    NotePattern& high = rig.project().tracks[track].notes[0][1];
    const int lowKeys[3] = {36, 40, 43}, highKeys[3] = {84, 88, 91};
    for (int i = 0; i < 3; ++i) {
        low.notes[i] = NoteEvent{i * 4, lowKeys[i], 2.f, 0.f, 0.8f};
        high.notes[i] = NoteEvent{i * 4, highKeys[i], 2.f, 0.f, 0.8f};
    }
    s.sendNotePattern(track, 0);
    rig.frames(3);
    NoteScreen& roll = rig.app.notes();
    const int keys = s.octavesShown() * 12;
    const auto shows = [&](const int* at) {
        const int l = roll.lowestKeyShown();
        return at[0] >= l && at[2] < l + keys && roll.cursorPitch() >= l && roll.cursorPitch() < l + keys;
    };
    s.togglePlay(false);
    rig.frames(3);
    CHECK(rig.project().bar == 0 && shows(lowKeys));
    CHECK(rig.untilBar(1));
    rig.frames(1);
    CHECK(rig.project().bar == 1 && shows(highKeys));
    if (!shows(highKeys)) std::printf("  the keys from %d, the cursor on %d\n", roll.lowestKeyShown(), roll.cursorPitch());
    // Centred: the window's middle within a key of the notes' middle.
    const int middle = roll.lowestKeyShown() * 2 + keys - 1;
    CHECK(middle >= 84 + 91 - 2 && middle <= 84 + 91 + 2);
    CHECK(rig.untilBar(0));
    rig.frames(1);
    CHECK(shows(lowKeys));
    s.togglePlay(false);
    rig.frames(5);
}

} // namespace

int main() {
    testTheRule();
    testTheFillRule();
    testTheFillOnBothGrids();
    testTheDrumGrid();
    testTheRoll();
    testTheKeysRule();
    testTheRollsKeys();
    if (failures) {
        std::printf("follow: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("follow: all passed\n");
    return 0;
}
