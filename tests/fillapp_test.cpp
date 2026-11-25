// The FILL (fill_rule.hpp; docs/plans/fills-ideas.md, design 1): the app's side. The Session's calls
// the screens will make (make, turn off, empty, go to it, and the words for each), each one step of
// undo; the copy that a FILL is made from; a cell made eight bars long; every path that carries a
// cell (copy a bar onto it, copy a scene, SONG's copy and empty of a cell, emptying a scene, NEXT
// SCENE and its undo); the song file (written, read back, an old song as it was); MIDI export and an
// audio bounce play it on the last time round; where the playing is (a scene alone counts its rounds),
// and WRITE records into it while it plays. The engine is heard: a song with a FILL beside the same
// song built with the FILL's bar as a scene of its own.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/fill_rule.hpp"
#include "app.hpp"
#include "canvas.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, R1 = SDLK_w, SELECT = SDLK_BACKSPACE;
const int kStep = 6000; // 120 BPM at 48 kHz
const int kBar = 16 * kStep;
const int kRim = 6; // the built-in kit's rim shot: short, so one hit never reaches the next

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().view = View::Song;
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code) {
        key(code, true);
        frames(1);
        key(code, false);
        frames(1);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    void settle() { frames(50); } // the burst of edits is over
    void undo() {
        key(SELECT, true);
        tap(B);
        key(SELECT, false);
        frames(1);
    }
    void redo() {
        key(SELECT, true);
        tap(A);
        key(SELECT, false);
        frames(1);
    }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    TrackData& track(int t) { return project().tracks[t]; }
    // Played from the top of scene `scene` (as a song, or the scene alone), `bars` of it, both sides.
    std::vector<float> play(int bars, bool song = true, int scene = 0) {
        project().scene = scene;
        frames(30);
        session().togglePlay(song);
        std::vector<float> all, out(2 * 250);
        while (all.size() < static_cast<std::size_t>(2 * bars * kBar)) {
            engine->render(out.data(), 250);
            all.insert(all.end(), out.begin(), out.end());
        }
        session().togglePlay(song);
        frames(4);
        return all;
    }
};

bool sameDrums(const DrumPattern& a, const DrumPattern& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
float loudest(const std::vector<float>& both, long from, long to) { // frames, both sides
    float most = 0.f;
    for (long i = 2 * from; i < 2 * to && i < static_cast<long>(both.size()); ++i) most = std::fmax(most, std::fabs(both[static_cast<std::size_t>(i)]));
    return most;
}

// A drum track's one-bar cell in scene 0: the rim on step 1. REPEATS `repeats`.
void ownBar(Rig& rig, int repeats) {
    rig.track(0).drums[0][0].level[kRim][0] = 0.9f;
    rig.project().repeats[0] = repeats;
    rig.session().sendDrumPattern(0, 0);
    Command c;
    c.type = Command::SetSceneRepeats;
    c.a = repeats;
    rig.session().engine.send(c);
    rig.settle();
}
// ...and its FILL, made by the Session, then changed: no rim on step 1, the rim on step 5.
void fillBar(Rig& rig) {
    CHECK(rig.session().makeFill(0, 0) == Session::Fill::Made);
    DrumPattern& fill = rig.track(0).drums[0][kFillSlot];
    fill.level[kRim][0] = 0.f;
    fill.level[kRim][4] = 0.9f;
    rig.session().sendDrumPattern(0, 0);
    rig.settle();
}

// ---- the Session's calls -------------------------------------------------------------------------

void testMakeOffEmpty() {
    using F = Session::Fill;
    Rig rig;
    Session& session = rig.session();
    TrackData& t = rig.track(0);
    t.drums[0][0].level[kRim][0] = 0.9f;
    t.drums[0][0].chance[kRim][0] = 0.5f;
    session.setAutomation(0, 0, kAutoSendLane, 3, 0.5f);
    session.sendDrumPattern(0, 0);
    rig.settle();
    // Made: a copy of the bar it plays instead of, with that bar's automation.
    CHECK(session.makeFill(0, 0) == F::Made);
    CHECK(t.fill[0] == kFillOn && t.fillOn(0) && t.fillMade(0));
    CHECK(sameDrums(t.drums[0][kFillSlot], t.drums[0][0]));
    CHECK(t.automation[0][kAutoSendLane][kFillSlot * kSteps + 3] == 0.5f && t.automation[0][kAutoSendLane][kFillSlot * kSteps + 4] < 0.f);
    CHECK(session.makeFill(0, 0) == F::AlreadyOn);
    // Changed, then off: kept.
    t.drums[0][kFillSlot].level[kRim][4] = 0.7f;
    session.sendDrumPattern(0, 0);
    rig.settle();
    CHECK(session.fillOff(0, 0) == F::Off && t.fill[0] == kFillOff && !t.fillOn(0) && t.fillMade(0));
    CHECK(t.drums[0][kFillSlot].level[kRim][4] == 0.7f);
    CHECK(session.fillOff(0, 0) == F::AlreadyOff);
    // On again: as it was kept, not made again from the bar.
    CHECK(session.makeFill(0, 0) == F::On && t.fill[0] == kFillOn && t.drums[0][kFillSlot].level[kRim][4] == 0.7f);
    // Emptied (a drop): nothing in it, its automation neither; still on. Again: nothing to do.
    CHECK(session.emptyFill(0, 0) == F::Emptied && t.fill[0] == kFillOn);
    CHECK(t.drums[0][kFillSlot].empty() && t.automation[0][kAutoSendLane][kFillSlot * kSteps + 3] < 0.f);
    CHECK(t.automation[0][kAutoSendLane][3] == 0.5f && t.drums[0][0].level[kRim][0] == 0.9f); // (the cell's own bar untouched)
    CHECK(session.emptyFill(0, 0) == F::AlreadyEmpty);
    // Off and on: a drop stays a drop (it is made: not copied again).
    CHECK(session.fillOff(0, 0) == F::Off && session.makeFill(0, 0) == F::On && t.drums[0][kFillSlot].empty());
    // Refusals.
    CHECK(session.fillOff(0, 1) == F::NoFill && session.emptyFill(0, 1) == F::NoFill);
    session.setCellBars(0, 2, 8);
    CHECK(session.makeFill(0, 2) == F::EightBars && t.fill[2] == kFillNone);
    CHECK(session.makeFill(5, 0) == F::NoTrack && session.makeFill(-1, 0) == F::NoTrack && session.makeFill(0, kMaxScenes) == F::NoTrack);
    CHECK(session.fillOff(5, 0) == F::NoTrack && session.emptyFill(5, 0) == F::NoTrack);
    // goToFill: onto it where it is made.
    CHECK(!session.goToFill(0, 1));
    CHECK(session.goToFill(0, 0) && rig.project().track == 0 && rig.project().scene == 0 && rig.project().bar == kFillSlot);
    // The ...Here calls say what they did.
    rig.project().scene = 3;
    CHECK(session.makeFillHere() == F::Made && std::string(session.saidText()) == "FILL MADE");
    CHECK(session.fillOffHere() == F::Off && std::string(session.saidText()) == "FILL OFF");
    CHECK(session.emptyFillHere() == F::AlreadyEmpty && std::string(session.saidText()) == "THE FILL IS EMPTY");
    CHECK(session.makeFillHere() == F::On && std::string(session.saidText()) == "FILL ON");
}

// The bar a FILL is made from: the cell's own bar in the scene's last bar.
void testTheBarCopied() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Drums);
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    TrackData& t = rig.track(0);
    const auto mark = [&](int track, int scene, int bars) { // each bar its rim on its own step
        session.setCellBars(track, scene, bars);
        for (int bar = 0; bar < bars; ++bar) rig.track(track).drums[scene][bar].level[kRim][bar] = 0.9f;
        session.sendDrumPattern(track, scene);
    };
    const auto copiedFrom = [&](int track, int scene) {
        for (int bar = 0; bar < kMaxBars; ++bar)
            if (rig.track(track).drums[scene][kFillSlot].level[kRim][bar] > 0.f) return bar;
        return -1;
    };
    mark(0, 0, 4); // four bars in a scene of four: the fourth
    CHECK(session.makeFill(0, 0) == Session::Fill::Made && copiedFrom(0, 0) == 3);
    mark(0, 1, 1); // one bar under a scene of four (track 2's): its own one bar
    session.setCellBars(1, 1, 4);
    CHECK(session.makeFill(0, 1) == Session::Fill::Made && copiedFrom(0, 1) == 0);
    mark(0, 2, 2); // two under four: its second
    session.setCellBars(1, 2, 4);
    CHECK(session.makeFill(0, 2) == Session::Fill::Made && copiedFrom(0, 2) == 1);
    mark(0, 3, 2); // two bars in a scene cut short to ten steps: its first bar is the scene's last
    session.setSceneSteps(3, 10);
    CHECK(session.makeFill(0, 3) == Session::Fill::Made && copiedFrom(0, 3) == 0);
    // A synth's notes: a note running over the bar line is held to the FILL's own end.
    session.setCellBars(2, 3, 2);
    NoteEvent n;
    n.step = 6;
    n.pitch = 60;
    n.length = 14.f;
    n.velocity = 0.8f;
    rig.track(2).notes[3][0].notes[0] = n;
    session.sendNotePattern(2, 3);
    CHECK(session.makeFill(2, 3) == Session::Fill::Made);
    CHECK(rig.track(2).notes[3][kFillSlot].notes[0].pitch == 60 && rig.track(2).notes[3][kFillSlot].notes[0].length == 10.f);
    CHECK(rig.track(2).notes[3][0].notes[0].length == 14.f);
    CHECK(t.fill[3] == kFillOn && rig.track(1).fill[3] == kFillNone); // (only the cell asked)
}

void testUndo() {
    Rig rig;
    Session& session = rig.session();
    ownBar(rig, 2);
    TrackData& t = rig.track(0);
    const int steps = session.undoSteps();
    CHECK(session.makeFill(0, 0) == Session::Fill::Made);
    CHECK(session.undoSteps() == steps + 1);
    CHECK(session.makeFill(0, 0) == Session::Fill::AlreadyOn && session.undoSteps() == steps + 1); // nothing done: no step
    rig.settle();
    session.goToFill(0, 0);
    rig.undo();
    CHECK(t.fill[0] == kFillNone && t.drums[0][kFillSlot].empty());
    CHECK(rig.project().bar == 0); // (no FILL to stand on now)
    rig.redo();
    CHECK(t.fill[0] == kFillOn && t.drums[0][kFillSlot].level[kRim][0] == 0.9f);
    session.goToFill(0, 0);
    rig.settle();
    CHECK(session.fillOff(0, 0) == Session::Fill::Off);
    rig.settle();
    rig.undo();
    CHECK(t.fill[0] == kFillOn && rig.project().bar == kFillSlot); // (standing on it still)
    CHECK(session.emptyFill(0, 0) == Session::Fill::Emptied);
    rig.settle();
    rig.undo();
    CHECK(t.fill[0] == kFillOn && t.drums[0][kFillSlot].level[kRim][0] == 0.9f);
    CHECK(session.undoSteps() == steps + 1);
    // The engine is told by undo too: taken back to before the making, the song plays the cell's own bars.
    rig.undo();
    rig.settle();
    Rig plain;
    ownBar(plain, 2);
    CHECK(t.fill[0] == kFillNone);
    CHECK(rig.play(2) == plain.play(2));
}

// ---- the engine hears it -------------------------------------------------------------------------

// The FILL's song (one scene, REPEATS 2, the FILL on) beside the same built as two scenes: the cell's own
// bar, then the FILL's bar as a scene's own.
void testTheEngineHearsIt() {
    Rig fill, two;
    ownBar(fill, 2);
    fillBar(fill);
    ownBar(two, 1);
    two.track(0).drums[1][0] = fill.track(0).drums[0][kFillSlot];
    two.session().sendDrumPattern(0, 1);
    two.settle();
    const std::vector<float> a = fill.play(4), b = two.play(4);
    CHECK(a == b);
    CHECK(loudest(a, 0, 2000) > 0.1f && loudest(a, kBar, kBar + 2000) == 0.f && loudest(a, kBar + 4 * kStep, kBar + 4 * kStep + 2000) > 0.1f);
    // Off: the cell's own bar every time round.
    CHECK(fill.session().fillOff(0, 0) == Session::Fill::Off);
    fill.settle();
    Rig plain;
    ownBar(plain, 2);
    CHECK(fill.play(4) == plain.play(4));
    // On again: heard again, as it was kept.
    CHECK(fill.session().makeFill(0, 0) == Session::Fill::On);
    fill.settle();
    CHECK(fill.play(4) == a);
}

// A scene played alone counts its time rounds (playPass), round and round, and plays the FILL on the
// last of each REPEATS: where the playing is in the cell (cellBarPlaying) says so; REPEATS' own count
// (playRepeat) is as it always was. WRITE records into the FILL while it plays.
void testPlaying() {
    Rig rig;
    Session& session = rig.session();
    ownBar(rig, 3);
    fillBar(rig);
    rig.project().writing = true;
    session.togglePlay(false); // the scene alone
    std::vector<int> passes, repeats, bars;
    std::vector<float> out(2 * 250);
    for (int bar = 0; bar < 6; ++bar) {
        for (int i = 0; i < kBar / 250 / 2; ++i) rig.engine->render(out.data(), 250);
        rig.clock += 16;
        rig.app.update(rig.input, rig.clock);
        passes.push_back(rig.project().playPass);
        repeats.push_back(rig.project().playRepeat);
        bars.push_back(rig.project().cellBarPlaying(0));
        if (bar == 2) session.setSend(0, 0, 0.75f); // WRITE, in the FILL
        if (bar == 3) session.setSend(0, 0, 0.25f); // ...and in the cell's own bar
        for (int i = 0; i < kBar / 250 / 2; ++i) rig.engine->render(out.data(), 250);
    }
    session.togglePlay(false);
    rig.project().writing = false;
    CHECK((passes == std::vector<int>{0, 1, 2, 0, 1, 2}));
    CHECK((repeats == std::vector<int>{0, 0, 0, 0, 0, 0}));
    CHECK((bars == std::vector<int>{0, 0, kFillSlot, 0, 0, kFillSlot}));
    int fillWritten = 0, ownWritten = 0, crossed = 0;
    for (int s = 0; s < kSteps; ++s) {
        const float fill = rig.track(0).automation[0][kAutoSendLane][kFillSlot * kSteps + s], own = rig.track(0).automation[0][kAutoSendLane][s];
        fillWritten += fill == 0.75f;
        ownWritten += own == 0.25f;
        crossed += fill == 0.25f || own == 0.75f;
    }
    CHECK(fillWritten == 1 && ownWritten == 1 && crossed == 0);
    // automated(): what the lane is just now, the FILL's where it plays.
    rig.project().playing = true;
    rig.project().playScene = 0, rig.project().playBar = 0, rig.project().playRepeat = 0, rig.project().playLoop = true;
    for (int s = 0; s < kSteps; ++s)
        if (rig.track(0).automation[0][kAutoSendLane][kFillSlot * kSteps + s] == 0.75f) {
            rig.project().playStep = s;
            rig.project().playPass = 2;
            CHECK(session.automated(0, kAutoSendLane, 0.f) == 0.75f);
            rig.project().playPass = 1;
            CHECK(session.automated(0, kAutoSendLane, 0.f) != 0.75f);
        }
    rig.project().playing = false;
    rig.project().playScene = -1;
    CHECK(rig.project().cellBarPlaying(0) == -1);
}

// ---- a cell made eight bars long -----------------------------------------------------------------

void testEightBars() {
    {
        // Four bars with a FILL: made eight, the FILL is its eighth bar, as it was, and it has no FILL.
        Rig rig;
        Session& session = rig.session();
        session.setCellBars(0, 0, 4);
        for (int bar = 0; bar < 4; ++bar) rig.track(0).drums[0][bar].level[kRim][bar] = 0.9f;
        session.sendDrumPattern(0, 0);
        CHECK(session.makeFill(0, 0) == Session::Fill::Made);
        rig.track(0).drums[0][kFillSlot].level[kRim][9] = 0.9f;
        session.sendDrumPattern(0, 0);
        const DrumPattern fill = rig.track(0).drums[0][kFillSlot];
        session.goToFill(0, 0);
        session.setCellBars(0, 0, 8);
        CHECK(rig.track(0).bars[0] == 8 && rig.track(0).fill[0] == kFillNone);
        CHECK(sameDrums(rig.track(0).drums[0][7], fill));
        CHECK(sameDrums(rig.track(0).drums[0][4], rig.track(0).drums[0][0]) && sameDrums(rig.track(0).drums[0][6], rig.track(0).drums[0][2])); // (the rest filled in as ever)
        CHECK(rig.project().bar == kFillSlot); // (on it still: the eighth bar now)
        // Four again: no FILL, and the engine knows it (it was told the FILL went).
        session.setCellBars(0, 0, 4);
        CHECK(rig.track(0).fill[0] == kFillNone && rig.project().bar == 3);
        rig.settle();
        Rig plain;
        plain.session().setCellBars(0, 0, 4);
        for (int bar = 0; bar < 4; ++bar) plain.track(0).drums[0][bar].level[kRim][bar] = 0.9f;
        plain.session().sendDrumPattern(0, 0);
        plain.settle();
        CHECK(rig.play(4) == plain.play(4));
    }
    {
        // An emptied FILL (a drop) made the eighth bar stays empty: it is not filled from the fourth.
        Rig rig;
        Session& session = rig.session();
        session.setCellBars(0, 0, 4);
        rig.track(0).drums[0][3].level[kRim][3] = 0.9f;
        session.sendDrumPattern(0, 0);
        CHECK(session.makeFill(0, 0) == Session::Fill::Made && session.emptyFill(0, 0) == Session::Fill::Emptied);
        session.setCellBars(0, 0, 8);
        CHECK(rig.track(0).drums[0][7].empty() && rig.track(0).fill[0] == kFillNone);
        // One turned off: the same.
        session.setCellBars(0, 1, 4);
        rig.track(0).drums[1][3].level[kRim][3] = 0.9f;
        session.sendDrumPattern(0, 1);
        CHECK(session.makeFill(0, 1) == Session::Fill::Made && session.emptyFill(0, 1) == Session::Fill::Emptied && session.fillOff(0, 1) == Session::Fill::Off);
        session.setCellBars(0, 1, 8);
        CHECK(rig.track(0).drums[1][7].empty() && rig.track(0).fill[1] == kFillNone);
        // ...and with none, the eighth bar is filled in as ever.
        session.setCellBars(0, 2, 4);
        rig.track(0).drums[2][3].level[kRim][3] = 0.9f;
        session.sendDrumPattern(0, 2);
        session.setCellBars(0, 2, 8);
        CHECK(rig.track(0).drums[2][7].level[kRim][3] == 0.9f);
    }
    {
        // Made eight bars where bars 5 to 7 hold something already (hidden since it was made four): nothing is
        // filled in, so the track is not sent whole; the engine is told the FILL went all the same, and made
        // four again it plays its own fourth bar.
        Rig rig;
        Session& session = rig.session();
        rig.track(0).drums[0][0].level[kRim][0] = 0.9f;
        session.sendDrumPattern(0, 0);
        session.setCellBars(0, 0, 8); // (every bar the first's)
        session.setCellBars(0, 0, 4);
        CHECK(session.makeFill(0, 0) == Session::Fill::Made);
        rig.track(0).drums[0][kFillSlot].level[kRim][9] = 0.9f;
        session.sendDrumPattern(0, 0);
        const unsigned sent = session.imagesSent();
        session.setCellBars(0, 0, 8);
        CHECK(session.imagesSent() == sent && rig.track(0).fill[0] == kFillNone);
        session.setCellBars(0, 0, 4);
        rig.settle();
        Rig plain;
        plain.track(0).drums[0][0].level[kRim][0] = 0.9f;
        plain.session().sendDrumPattern(0, 0);
        plain.session().setCellBars(0, 0, 4);
        plain.settle();
        CHECK(rig.play(4) == plain.play(4));
    }
    {
        // One bar to two: the FILL stays, and the view on it.
        Rig rig;
        Session& session = rig.session();
        ownBar(rig, 1);
        fillBar(rig);
        session.goToFill(0, 0);
        session.setCellBars(0, 0, 2);
        CHECK(rig.track(0).fill[0] == kFillOn && rig.track(0).drums[0][kFillSlot].level[kRim][4] == 0.9f && rig.project().bar == kFillSlot);
        CHECK(rig.track(0).drums[0][1].level[kRim][0] == 0.9f); // (its second bar filled from its first)
    }
}

// ---- the paths that carry a cell ------------------------------------------------------------------

void testCopyABar() {
    Rig rig;
    Session& session = rig.session();
    ownBar(rig, 1);
    rig.track(0).drums[0][0].level[2][6] = 0.5f;
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done);
    CHECK(session.pasteBar(0, 0, kFillSlot) == Session::BarCopy::NoTrack); // no FILL: no such bar
    CHECK(session.makeFill(0, 0) == Session::Fill::Made && session.emptyFill(0, 0) == Session::Fill::Emptied);
    rig.settle();
    CHECK(session.pasteBar(0, 0, kFillSlot) == Session::BarCopy::Done && sameDrums(rig.track(0).drums[0][kFillSlot], rig.track(0).drums[0][0]));
    rig.settle();
    rig.undo();
    CHECK(rig.track(0).drums[0][kFillSlot].empty());
    CHECK(session.fillOff(0, 0) == Session::Fill::Off);
    CHECK(session.copyBar(0, 0, 0) == Session::BarCopy::Done); // (a paste spends the copy: copied again)
    CHECK(session.pasteBar(0, 0, kFillSlot) == Session::BarCopy::Done); // made and off: still its bar
    // From the FILL onto a bar.
    rig.track(0).drums[0][kFillSlot].level[3][3] = 0.6f;
    CHECK(session.copyBar(0, 0, kFillSlot) == Session::BarCopy::Done && session.pasteBar(0, 0, 0) == Session::BarCopy::Done);
    CHECK(rig.track(0).drums[0][0].level[3][3] == 0.6f);
    // Notes onto a FILL: held to its own end.
    session.addTrack(TrackKind::Phazed);
    rig.settle();
    session.setCellBars(1, 0, 2);
    NoteEvent n;
    n.step = 12;
    n.pitch = 64;
    n.length = 8.f;
    n.velocity = 0.8f;
    rig.track(1).notes[0][0].notes[0] = n;
    session.sendNotePattern(1, 0);
    CHECK(session.copyBar(1, 0, 0) == Session::BarCopy::Done && session.makeFill(1, 0) == Session::Fill::Made);
    CHECK(session.pasteBar(1, 0, kFillSlot) == Session::BarCopy::Done && rig.track(1).notes[0][kFillSlot].notes[0].length == 4.f);
}

void testCopyAndEmptyAScene() {
    Rig rig;
    Session& session = rig.session();
    ownBar(rig, 2);
    fillBar(rig);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done && session.pasteScene(1) == Session::SceneCopy::Done);
    CHECK(rig.track(0).fill[1] == kFillOn && sameDrums(rig.track(0).drums[1][kFillSlot], rig.track(0).drums[0][kFillSlot]));
    // A scene the same but for its FILL being off is not the same.
    // (A paste spends the copy: scene 1 is copied again before each.)
    CHECK(session.fillOff(0, 1) == Session::Fill::Off);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    CHECK(session.pasteScene(1) == Session::SceneCopy::Done && rig.track(0).fill[1] == kFillOn);
    CHECK(session.copyScene(0) == Session::SceneCopy::Done);
    CHECK(session.pasteScene(1) == Session::SceneCopy::Same);
    // The engine hears the pasted scene: scene 2 played alone, as scene 1.
    rig.settle();
    CHECK(rig.play(4, false, 1) == rig.play(4, false, 0));
    // Emptied: its FILL too, and the engine told its FILL went: a hit written there by the grid (which sends
    // the hit alone) plays every time round (REPEATS 1 again: with the FILL on there, each round's bar would be
    // its emptied FILL).
    CHECK(session.clearScene(1) == Session::SceneClear::Done && rig.track(0).fill[1] == kFillNone);
    rig.project().scene = 1;
    rig.project().bar = 0;
    rig.frames(1);
    rig.app.drums().setStep(kRim, 0, 0.9f);
    rig.settle();
    const std::vector<float> two = rig.play(2, false, 1);
    CHECK(loudest(two, 0, 2000) > 0.1f && loudest(two, kBar, kBar + 2000) > 0.1f);
    // A scene whose one cell has nothing but a made FILL, emptied, off: not empty (its FILL is something to empty).
    CHECK(session.makeFill(0, 2) == Session::Fill::Made && session.fillOff(0, 2) == Session::Fill::Off);
    rig.track(0).drums[2][kFillSlot] = DrumPattern(); // (a fresh one: as if nothing was written)
    CHECK(session.clearScene(2) == Session::SceneClear::Done && rig.track(0).fill[2] == kFillNone);
    // ...and one with no pieces at all, only the flag (as a song file's `fill 0` alone leaves it).
    rig.track(0).fill[4] = kFillOff;
    CHECK(!rig.track(0).holdsPieces(4));
    CHECK(session.clearScene(4) == Session::SceneClear::Done && rig.track(0).fill[4] == kFillNone);
}

void testSongCellCopyAndEmpty() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    ownBar(rig, 2);
    fillBar(rig);
    song.moveCursorTo(0, 0);
    rig.tap(R1);
    CHECK(std::string(session.saidText()) == "CELL COPIED");
    song.moveCursorTo(1, 0);
    rig.tap(R1);
    CHECK(std::string(session.saidText()) == "CELL PASTED");
    CHECK(rig.track(0).fill[1] == kFillOn && sameDrums(rig.track(0).drums[1][kFillSlot], rig.track(0).drums[0][kFillSlot]));
    rig.settle();
    rig.project().repeats[1] = 2;
    Command c;
    c.type = Command::SetSceneRepeats;
    c.scene = 1;
    c.a = 2;
    session.engine.send(c);
    CHECK(rig.play(4, false, 1) == rig.play(4, false, 0));
    // B on the cell: emptied, its FILL with it.
    song.moveCursorTo(1, 0);
    rig.tap(B);
    CHECK(rig.track(0).fill[1] == kFillNone && rig.track(0).drums[1][kFillSlot].empty());
    // A cell with nothing but a made FILL is emptied too.
    CHECK(session.makeFill(0, 3) == Session::Fill::Made && session.fillOff(0, 3) == Session::Fill::Off);
    rig.settle();
    song.moveCursorTo(3, 0);
    rig.tap(B);
    CHECK(rig.track(0).fill[3] == kFillNone);
}

void testNextScene() {
    Rig rig;
    Session& session = rig.session();
    ownBar(rig, 2);
    fillBar(rig);
    // Scene 2: its own bar (the rim on step 3), and a FILL made, changed (the rim on step 7 too), turned off.
    rig.track(0).drums[1][0].level[kRim][2] = 0.9f;
    session.sendDrumPattern(0, 1);
    CHECK(session.makeFill(0, 1) == Session::Fill::Made);
    rig.track(0).drums[1][kFillSlot].level[kRim][6] = 0.9f;
    session.sendDrumPattern(0, 1);
    CHECK(session.fillOff(0, 1) == Session::Fill::Off);
    rig.settle();
    session.goToFill(0, 0);
    CHECK(session.nextScene(0) == 1);
    CHECK(rig.project().scene == 1 && rig.project().bar == kFillSlot); // the player on the copy's FILL
    CHECK(rig.track(0).fill[0] == kFillOn && rig.track(0).fill[1] == kFillOn && rig.track(0).fill[2] == kFillOff);
    CHECK(sameDrums(rig.track(0).drums[1][kFillSlot], rig.track(0).drums[0][kFillSlot]));
    rig.settle();
    rig.undo();
    CHECK(rig.track(0).fill[0] == kFillOn && rig.track(0).fill[1] == kFillOff && rig.track(0).fill[2] == kFillNone);
    CHECK(rig.track(0).drums[1][kFillSlot].level[kRim][6] == 0.9f);
    rig.redo();
    CHECK(rig.track(0).fill[1] == kFillOn && rig.track(0).fill[2] == kFillOff);
    rig.settle();
    // The engine has it: the copy plays as the scene it came from, FILL and all (each alone, REPEATS 2)...
    CHECK(rig.play(4, false, 1) == rig.play(4, false, 0));
    // ...and the scene moved down plays its own bar every time round (its FILL off), as one never given one.
    Rig plain;
    ownBar(plain, 2);
    plain.track(0).drums[2][0].level[kRim][2] = 0.9f;
    plain.session().sendDrumPattern(0, 2);
    plain.settle();
    const std::vector<float> moved = rig.play(3, false, 2);
    CHECK(moved == plain.play(3, false, 2) && loudest(moved, 2 * kStep, 2 * kStep + 2000) > 0.1f);
}

// ---- the song file --------------------------------------------------------------------------------

void testSongFile() {
    std::unique_ptr<Project> song(new Project);
    song->tracks[0].kind = TrackKind::Drums;
    song->tracks[1].kind = TrackKind::Phazed;
    song->tracks[0].drums[0][0].level[kRim][0] = 0.9f;
    song->tracks[0].drums[0][kFillSlot].level[kRim][4] = 0.9f;
    song->tracks[0].fill[0] = kFillOn;
    song->tracks[0].bars[2] = 2;
    song->tracks[0].fill[2] = kFillOff; // made, off, empty: written all the same
    NoteEvent n;
    n.step = 3;
    n.pitch = 62;
    n.length = 2.f;
    n.velocity = 0.8f;
    song->tracks[1].notes[1][kFillSlot].notes[0] = n;
    song->tracks[1].fill[1] = kFillOn; // a cell with nothing but its FILL
    song->tracks[0].fill[5] = kFillOff; // ...and one with nothing at all but a FILL made, emptied, off
    const std::string text = songText(*song);
    CHECK(text.find("scene 6\nfill 0\n") != std::string::npos);
    CHECK(text.find("scene 1\nfill 1\n") != std::string::npos);
    CHECK(text.find("scene 3\nbars 2\nfill 0\n") != std::string::npos);
    CHECK(text.find("scene 2\nfill 1\nbar 8\nnote 4 62") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(back->tracks[0].fill[0] == kFillOn && back->tracks[0].fill[2] == kFillOff && back->tracks[0].fill[1] == kFillNone && back->tracks[0].fill[5] == kFillOff);
    CHECK(back->tracks[1].fill[1] == kFillOn && back->tracks[1].notes[1][kFillSlot].notes[0].pitch == 62);
    CHECK(back->tracks[0].drums[0][kFillSlot].level[kRim][4] == 0.9f);
    // (The synth's settings are its preset's once read: the text is the same from the second time round.)
    const std::string again = songText(*back);
    CHECK(again.find("scene 1\nfill 1\n") != std::string::npos && again.find("scene 3\nbars 2\nfill 0\n") != std::string::npos);
    CHECK(again.find("scene 2\nfill 1\nbar 8\nnote 4 62") != std::string::npos);
    std::unique_ptr<Project> third(new Project);
    CHECK(parseSong(again, *third) && songText(*third) == again);
    // The scene holding nothing but a FILL is a scene: the song is two scenes long, not one.
    CHECK(back->sceneCount() == 2 && !back->tracks[1].sceneEmpty(1));
    back->tracks[1].fill[1] = kFillOff; // (off, it plays nothing)
    CHECK(back->tracks[1].sceneEmpty(1));
    // A cell of eight bars has none, whichever line comes first; a number not known reads as made, off.
    const std::string head = "apotheca-song 1\ntempo 120\nrepeats 1\ntrack drums\n";
    CHECK(parseSong(head + "scene 1\nbars 8\nfill 1\n", *back) && back->tracks[0].fill[0] == kFillNone && back->tracks[0].bars[0] == 8);
    CHECK(parseSong(head + "scene 1\nfill 1\nbars 8\n", *back) && back->tracks[0].fill[0] == kFillNone);
    CHECK(parseSong(head + "scene 1\nbars 4\nfill 7\n", *back) && back->tracks[0].fill[0] == kFillOff);
    CHECK(parseSong(head + "scene 1\nfill 1\n", *back) && back->tracks[0].fill[0] == kFillOn);
    CHECK(parseSong(head + "fill 1\nscene 1\n", *back) && back->tracks[0].fill[0] == kFillNone); // (before any scene: no cell)
    // An old song (no fill line) reads with none, and is written as it was, to the byte.
    std::unique_ptr<Project> old(new Project);
    old->tracks[0].kind = TrackKind::Drums;
    old->tracks[0].drums[0][0].level[0][0] = 0.9f;
    old->tracks[0].bars[1] = 4;
    old->tracks[0].drums[1][7].level[1][1] = 0.5f; // a hidden bar, as a cell made shorter keeps
    const std::string was = songText(*old);
    CHECK(was.find("fill") == std::string::npos);
    CHECK(parseSong(was, *back));
    for (int s = 0; s < kMaxScenes; ++s) CHECK(back->tracks[0].fill[s] == kFillNone);
    CHECK(songText(*back) == was);
}

// ---- MIDI and a bounce ----------------------------------------------------------------------------

void testMidi() {
    // The FILL's song beside the same as scenes of their own: the same file, byte for byte.
    std::unique_ptr<Project> fill(new Project), parts(new Project);
    for (Project* song : {fill.get(), parts.get()}) {
        song->tracks[0].kind = TrackKind::Drums;
        song->tracks[1].kind = TrackKind::Phazed;
    }
    NoteEvent n;
    n.step = 2;
    n.pitch = 60;
    n.length = 2.f;
    n.velocity = 0.8f;
    fill->tracks[0].drums[0][0].level[0][0] = 0.9f;
    fill->tracks[0].drums[0][kFillSlot].level[1][4] = 0.9f;
    fill->tracks[0].fill[0] = kFillOn;
    fill->tracks[1].notes[0][0].notes[0] = n;
    n.pitch = 67;
    fill->tracks[1].notes[0][kFillSlot].notes[0] = n;
    fill->tracks[1].fill[0] = kFillOn;
    fill->repeats[0] = 3;
    fill->tracks[0].drums[1][0].level[2][8] = 0.9f; // a second scene, no FILL (its eighth bar never heard)
    fill->tracks[0].drums[1][kFillSlot].level[3][8] = 0.9f;
    fill->repeats[1] = 2;
    // ...as four scenes: the own bar twice, the FILL's bar, the second scene twice.
    parts->tracks[0].drums[0][0] = fill->tracks[0].drums[0][0];
    parts->tracks[0].drums[1][0] = fill->tracks[0].drums[0][kFillSlot];
    parts->tracks[0].drums[2][0] = fill->tracks[0].drums[1][0];
    parts->tracks[1].notes[0][0] = fill->tracks[1].notes[0][0];
    parts->tracks[1].notes[1][0] = fill->tracks[1].notes[0][kFillSlot];
    parts->repeats[0] = 2;
    parts->repeats[1] = 1;
    parts->repeats[2] = 2;
    CHECK(midiFileBytes(*fill, "song") == midiFileBytes(*parts, "song"));
    // REPEATS 1: its one time round is the last.
    fill->repeats[0] = 1;
    parts->repeats[0] = 1;
    parts->tracks[0].drums[0][0] = parts->tracks[0].drums[1][0];
    parts->tracks[1].notes[0][0] = parts->tracks[1].notes[1][0];
    parts->tracks[0].drums[1][0] = parts->tracks[0].drums[2][0];
    parts->tracks[0].drums[2][0] = DrumPattern();
    parts->tracks[1].notes[1][0] = NotePattern();
    parts->repeats[1] = 2;
    parts->repeats[2] = 1;
    CHECK(midiFileBytes(*fill, "song") == midiFileBytes(*parts, "song"));
    // Off: never.
    fill->repeats[0] = 3;
    fill->tracks[0].fill[0] = fill->tracks[1].fill[0] = kFillOff;
    std::unique_ptr<Project> plain(new Project(*fill));
    plain->tracks[0].drums[0][kFillSlot] = DrumPattern();
    plain->tracks[1].notes[0][kFillSlot] = NotePattern();
    plain->tracks[0].fill[0] = plain->tracks[1].fill[0] = kFillNone;
    CHECK(midiFileBytes(*fill, "song") == midiFileBytes(*plain, "song"));
}

void testBounce() {
    const std::string folder = tempFolder("fillapp");
    const auto read = [&](const std::string& name) {
        std::vector<float> left;
        std::string bytes;
        if (!readTextFile(folder + "/" + name, bytes) || bytes.size() < 44) return left;
        for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) {
            const int low = static_cast<unsigned char>(bytes[at]), high = static_cast<signed char>(bytes[at + 1]);
            left.push_back(static_cast<float>(high * 256 + low) / 32767.f);
        }
        return left;
    };
    const auto loud = [](const std::vector<float>& v, long from, long to) {
        float p = 0.f;
        for (long i = from; i < to && i < static_cast<long>(v.size()); ++i) p = std::fmax(p, std::fabs(v[static_cast<std::size_t>(i)]));
        return p;
    };
    Rig rig;
    Session& session = rig.session();
    session.useBounceFolder(folder);
    ownBar(rig, 3);
    fillBar(rig);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const std::vector<float> wav = read(session.bounceLast());
    CHECK(wav.size() > static_cast<std::size_t>(3 * kBar));
    // The cell's own bar twice (the rim on step 1, nothing on step 5), then the FILL (the other way round).
    for (int bar = 0; bar < 2; ++bar) CHECK(loud(wav, bar * kBar, bar * kBar + 2000) > 0.1f && loud(wav, bar * kBar + 4 * kStep, bar * kBar + 4 * kStep + 2000) == 0.f);
    CHECK(loud(wav, 2 * kBar, 2 * kBar + 2000) == 0.f && loud(wav, 2 * kBar + 4 * kStep, 2 * kBar + 4 * kStep + 2000) > 0.1f);
}

// ---- the words ------------------------------------------------------------------------------------

void testTheWords() {
    using F = Session::Fill;
    Canvas canvas;
    if (canvas.ok) {
        // Every line said fits the App's box (260 wide, 6 either side) in the lettering it is held to at XL: LARGE.
        for (F answer : {F::Made, F::On, F::Off, F::Emptied, F::AlreadyOn, F::AlreadyOff, F::AlreadyEmpty, F::NoFill, F::EightBars, F::NoTrack}) {
            const std::string words = Session::fillWords(answer);
            CHECK(!words.empty() && canvas.fonts->medium.width(words.c_str()) <= 248.f);
        }
    }
    CHECK(Session::fillWords(F::Made) == "FILL MADE" && Session::fillWords(F::Off) == "FILL OFF" && Session::fillWords(F::NoFill) == "NO FILL HERE");
    TrackData t;
    t.kind = TrackKind::Drums;
    t.bars[0] = 4;
    CHECK(Session::cellBarWords(t, 0, 1) == "BAR 2 OF 4");
    t.fill[0] = kFillOn;
    CHECK(Session::cellBarWords(t, 0, kFillSlot) == "FILL" && Session::cellBarWords(t, 0, 3) == "BAR 4 OF 4");
    t.fill[0] = kFillOff;
    CHECK(Session::cellBarWords(t, 0, kFillSlot) == "FILL OFF");
    CHECK(t.barThere(0, kFillSlot) && t.barThere(0, 3) && !t.barThere(0, 4) && !t.barThere(0, -1));
    t.fill[0] = kFillNone;
    CHECK(!t.barThere(0, kFillSlot));
    t.bars[0] = 8;
    t.fill[0] = kFillOn; // (never so in a song; an eighth bar is its own)
    CHECK(!t.fillOn(0) && !t.fillMade(0) && t.barThere(0, kFillSlot) && Session::cellBarWords(t, 0, kFillSlot) == "BAR 8 OF 8");
}

} // namespace

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testMakeOffEmpty", testMakeOffEmpty}, {"testTheBarCopied", testTheBarCopied}, {"testUndo", testUndo},
        {"testTheEngineHearsIt", testTheEngineHearsIt}, {"testPlaying", testPlaying}, {"testEightBars", testEightBars},
        {"testCopyABar", testCopyABar}, {"testCopyAndEmptyAScene", testCopyAndEmptyAScene}, {"testSongCellCopyAndEmpty", testSongCellCopyAndEmpty},
        {"testNextScene", testNextScene}, {"testSongFile", testSongFile}, {"testMidi", testMidi}, {"testBounce", testBounce}, {"testTheWords", testTheWords},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("fillapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
