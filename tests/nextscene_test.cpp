// NEXT SCENE (SELECT + Y on the drum grid and the piano roll): the scene being
// worked on is copied into a new one put in right after it, the player is put
// there, and a song that is playing does not miss a step.
//
// "Does not miss a step" is checked on the sound itself: two engines play the
// same song, the gesture is pressed in the middle of a bar on one of them, and
// what comes out of the two is compared sample for sample.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "song_file.hpp"

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

const int kStep = 6000, kBar = 16 * kStep; // at 120 BPM, 48 kHz

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, Y = SDLK_a, SELECT = SDLK_BACKSPACE;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::vector<float> heard; // everything rendered, end to end

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    void with(SDL_Keycode held, SDL_Keycode code) { key(held, true); tap(code); key(held, false); }
    void nextScene() { with(SELECT, Y); }
    void undo() { with(SELECT, B); }
    void redo() { with(SELECT, A); }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    void settle() { frames(50); } // 0.8 s with nothing changing: a burst of editing is over
    void render(int audioFrames) {
        const std::size_t was = heard.size();
        heard.resize(was + static_cast<std::size_t>(2 * audioFrames));
        engine->render(heard.data() + was, audioFrames);
    }
    Project& project() { return app.project(); }
    Session& session() { return app.session(); }
    std::string said() { return app.undoSaid(); }
};

float loudest(const std::vector<float>& v, std::size_t from, std::size_t to) {
    float p = 0.f;
    for (std::size_t i = 2 * from; i < 2 * to && i < v.size(); ++i) p = std::fabs(v[i]) > p ? std::fabs(v[i]) : p;
    return p;
}

// How far apart two renders are at their worst sample (0: the same to the bit), or 9 if they are not the same length.
float apart(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 9.f;
    float worst = 0.f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float d = std::fabs(a[i] - b[i]);
        if (!(d <= worst)) worst = d == d ? d : 9.f;
    }
    return worst;
}

// A song with something of every kind a scene holds, and no two scenes alike:
// track 1 drums, track 2 Phazed; three scenes.
//   scene 1: the drums' cell two bars long, the synth's one; cut to 23 steps; played twice
//   scene 2: one bar, once
//   scene 3: cut to 13 steps, three times
void fill(Rig& rig) {
    Project& p = rig.project();
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    TrackData& drums = p.tracks[0];
    TrackData& synth = p.tracks[1];

    drums.bars[0] = 2;
    DrumPattern& d = drums.drums[0][0];
    for (int step = 0; step < kSteps; step += 4) d.level[0][step] = 0.8f;
    d.level[2][6] = 0.6f;
    d.nudge[2][6] = 0.25f;
    d.level[3][10] = 0.7f;
    d.chance[3][10] = 0.5f;
    d.start[3][10] = 0.25f;
    d.fromDice[3][10] = true;
    d.level[1][4] = 0.9f;
    d.fx[1][4].ratchet = 2;
    d.fx[1][4].pitch = 3;
    d.level[1][12] = 0.9f;
    d.fx[1][12].throwTo = 1;
    d.fx[1][12].cut = 2;
    d.diceSeed[5] = 4242u;
    drums.drums[0][1].level[1][4] = 0.75f;
    drums.drums[0][1].level[1][5] = 0.5f;
    drums.drums[0][1].fx[1][5].reverse = 1;
    for (int step = 0; step < kSteps; step += 2) drums.drums[1][0].level[3][step] = 0.4f;
    drums.drums[2][0].level[0][0] = 1.f;
    drums.drums[2][0].level[1][8] = 0.65f;

    NotePattern& n = synth.notes[0][0];
    n.notes[0] = NoteEvent{0, 60, 6.5f, 0.f, 0.8f, 0, 0, 0, 0}; // long: it is sounding wherever in the first steps the gesture comes
    n.notes[1] = NoteEvent{8, 67, 2.f, 0.f, 0.7f, 3, 0, 0, 0};  // a ratchet
    n.notes[2] = NoteEvent{12, 64, 3.f, -0.25f, 0.6f, 0, 1, 2, 0}; // early, gliding, thrown to SPACE
    ChordEvent& c = n.chords[0];
    c.step = 14;
    c.length = 2.f;
    c.root = 55;
    c.count = 3;
    c.intervals[0] = 0, c.intervals[1] = 4, c.intervals[2] = 7;
    c.velocity = 0.5f;
    c.ratchet = 2;
    c.throwTo = 1;
    std::snprintf(c.name, sizeof c.name, "MAJ");
    for (int step = 0; step < kSteps; ++step) synth.automation[0][0][step] = 0.1f + 0.05f * static_cast<float>(step); // a macro, moving all bar
    synth.notes[1][0].notes[0] = NoteEvent{2, 62, 1.f, 0.f, 0.8f, 0, 0, 0, 0};
    synth.automation[1][1][3] = 0.7f;
    synth.notes[2][0].notes[0] = NoteEvent{0, 72, 4.f, 0.f, 0.9f, 0, 0, 0, 0};
    drums.automation[2][20][0] = 0.3f; // the drums' ECHO send, on scene 3's first step

    p.repeats[0] = 2, p.repeats[1] = 1, p.repeats[2] = 3;
    p.steps[0] = 23;
    p.steps[2] = 13;
    p.track = 0;
    p.scene = 0;
    p.bar = 0;
    rig.session().applyProject();
    rig.session().view = View::Pattern;
    rig.frames(2);
    rig.render(512);
    rig.heard.clear();
}
const int kSongSteps = 2 * 23 + 16 + 3 * 13; // the song above, once through

// Is everything scene `sa` of `a` holds what scene `sb` of `b` holds?
bool sameScene(const Project& a, int sa, const Project& b, int sb) {
    bool same = a.repeats[sa] == b.repeats[sb] && a.steps[sa] == b.steps[sb];
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackData& x = a.tracks[t];
        const TrackData& y = b.tracks[t];
        same = same && x.bars[sa] == y.bars[sb];
        same = same && std::memcmp(x.automation[sa], y.automation[sb], sizeof x.automation[sa]) == 0;
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const DrumPattern& d = x.drums[sa][bar];
            const DrumPattern& e = y.drums[sb][bar];
            same = same && std::memcmp(d.level, e.level, sizeof d.level) == 0 && std::memcmp(d.nudge, e.nudge, sizeof d.nudge) == 0;
            same = same && std::memcmp(d.chance, e.chance, sizeof d.chance) == 0 && std::memcmp(d.start, e.start, sizeof d.start) == 0;
            same = same && std::memcmp(d.fromDice, e.fromDice, sizeof d.fromDice) == 0 && std::memcmp(d.diceSeed, e.diceSeed, sizeof d.diceSeed) == 0;
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = 0; step < kSteps; ++step) {
                    const StepFx& f = d.fx[lane][step];
                    const StepFx& g = e.fx[lane][step];
                    same = same && f.pitch == g.pitch && f.ratchet == g.ratchet && f.reverse == g.reverse && f.cut == g.cut && f.throwTo == g.throwTo;
                }
            const NotePattern& n = x.notes[sa][bar];
            const NotePattern& m = y.notes[sb][bar];
            same = same && std::memcmp(n.notes, m.notes, sizeof n.notes) == 0 && std::memcmp(n.chords, m.chords, sizeof n.chords) == 0;
        }
    }
    return same;
}

// The song as it should be with a copy of scene `at` put in after it: worked out here, forwards, from the song as it was.
std::unique_ptr<Project> withCopyAfter(const Project& was, int at) {
    std::unique_ptr<Project> p(new Project);
    *p = was;
    for (int scene = at + 1; scene < kMaxScenes; ++scene) {
        p->repeats[scene] = was.repeats[scene - 1];
        p->steps[scene] = was.steps[scene - 1];
        for (int t = 0; t < kNumTracks; ++t) {
            p->tracks[t].bars[scene] = was.tracks[t].bars[scene - 1];
            for (int bar = 0; bar < kMaxBars; ++bar) {
                p->tracks[t].drums[scene][bar] = was.tracks[t].drums[scene - 1][bar];
                p->tracks[t].notes[scene][bar] = was.tracks[t].notes[scene - 1][bar];
            }
            std::memcpy(p->tracks[t].automation[scene], was.tracks[t].automation[scene - 1], sizeof was.tracks[t].automation[scene]);
        }
    }
    p->scene = at + 1;
    return p;
}

std::unique_ptr<Project> copyOf(const Project& p) {
    std::unique_ptr<Project> c(new Project);
    *c = p;
    return c;
}

// ---- what is copied, and where the player is put -------------------------------------------------

void testTheSceneIsCopied() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    PatternScreen& drums = rig.app.drums();
    p.bar = 1; // the second bar of the drums' cell
    drums.moveCursorTo(1, 5);
    rig.settle();
    const std::unique_ptr<Project> was = copyOf(p);
    const int steps = rig.session().undoSteps();
    CHECK(p.sceneCount() == 3);

    rig.key(SELECT, true);
    rig.key(Y, true);
    // The player: on the new scene, on the same screen, track, bar and cursor.
    CHECK(p.scene == 1 && p.track == 0 && p.bar == 1 && rig.session().view == View::Pattern);
    CHECK(drums.cursorLane() == 1 && drums.cursorStep() == 5 && drums.level(1, 5) == 0.5f);
    CHECK(rig.said() == "SCENE 2: COPY OF 1");
    rig.key(Y, false);
    rig.key(SELECT, false);
    CHECK(rig.session().view == View::Pattern); // letting go of SELECT is not its tap: not off to the SONG screen
    CHECK(drums.level(1, 5) == 0.5f && drums.fx(1, 5).reverse == 1);

    // The song: scene 2 is scene 1 over again, scene 1 is as it was, and the two after are one further down. Nothing lost.
    CHECK(p.sceneCount() == 4);
    CHECK(sameScene(p, 0, *was, 0) && sameScene(p, 1, *was, 0) && sameScene(p, 2, *was, 1) && sameScene(p, 3, *was, 2));
    for (int scene = 4; scene < kMaxScenes; ++scene) CHECK(sameScene(p, scene, *was, scene - 1));
    CHECK(!sameScene(p, 1, *was, 1) && !sameScene(p, 2, *was, 2)); // (the scenes do differ: the checks above can tell)
    // Each kind of thing, by name.
    CHECK(p.repeats[1] == 2 && p.steps[1] == 23 && p.repeats[2] == 1 && p.steps[2] == 0 && p.repeats[3] == 3 && p.steps[3] == 13);
    CHECK(p.tracks[0].bars[1] == 2 && p.tracks[0].bars[2] == 1 && p.tracks[1].bars[1] == 1);
    CHECK(p.tracks[0].drums[1][0].fx[1][12].throwTo == 1 && p.tracks[0].drums[1][0].nudge[2][6] == 0.25f && p.tracks[0].drums[1][0].chance[3][10] == 0.5f);
    CHECK(p.tracks[1].notes[1][0].notes[2].glide == 1 && p.tracks[1].notes[1][0].notes[2].throwTo == 2 && p.tracks[1].notes[1][0].chords[0].ratchet == 2);
    CHECK(p.tracks[1].automation[1][0][7] == 0.1f + 0.05f * 7.f && p.tracks[1].automation[2][1][3] == 0.7f && p.tracks[1].automation[1][1][3] < 0.f);
    CHECK(p.tracks[0].automation[3][20][0] == 0.3f && p.tracks[0].automation[2][20][0] < 0.f);
    // And as the song file would keep it.
    CHECK(songText(p) == songText(*withCopyAfter(*was, 0)));

    // One step of undo, and going to the new scene is not taken for an edit a moment later.
    CHECK(rig.session().undoSteps() == steps + 1);
    rig.settle();
    CHECK(rig.session().undoSteps() == steps + 1 && rig.session().redoSteps() == 0);
    rig.frames(60);
    CHECK(rig.said().empty()); // the line goes after a moment

    // From the piano roll too, on a scene in the middle: the cursor and the bar stay.
    p.track = 1;
    p.scene = 2;
    p.bar = 0;
    rig.frames(1);
    NoteScreen& notes = rig.app.notes();
    notes.moveCursorTo(62, 2);
    rig.settle();
    const std::unique_ptr<Project> middle = copyOf(p);
    rig.nextScene();
    CHECK(p.scene == 3 && p.track == 1 && notes.cursorPitch() == 62 && notes.cursorStep() == 2 && notes.noteHere() != nullptr);
    CHECK(rig.said() == "SCENE 4: COPY OF 3" && rig.session().view == View::Pattern && p.sceneCount() == 5);
    CHECK(songText(p) == songText(*withCopyAfter(*middle, 2)));
    CHECK(sameScene(p, 1, *middle, 1) && sameScene(p, 2, *middle, 2) && sameScene(p, 3, *middle, 2) && sameScene(p, 4, *middle, 3));
}

// The gesture belongs to the grids. On the other screens SELECT + Y makes no scene.
void testOnlyOnTheGrids() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    for (View view : {View::Song, View::Device, View::Effects, View::Settings}) {
        rig.session().view = view;
        rig.frames(1);
        rig.nextScene();
        rig.frames(1);
        CHECK(p.sceneCount() == 3 && p.scene == 0 && rig.said().empty());
    }
    rig.session().view = View::Pattern;
    rig.settle();

    // Y's own jobs on the drum grid are not done as well: no "maybe" hit placed on an empty step
    // (the press), no chance stepped down on a hit (the let-go), in whichever order the two buttons are let go.
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(0, 4); // a kick, sure to play
    rig.key(SELECT, true);
    rig.key(Y, true);
    rig.key(SELECT, false);
    rig.key(Y, false); // SELECT first, then Y
    CHECK(p.scene == 1 && rig.session().view == View::Pattern);
    CHECK(p.tracks[0].drums[0][0].chance[0][4] == 1.f && p.tracks[0].drums[1][0].chance[0][4] == 1.f);
    drums.moveCursorTo(6, 9); // nothing there
    rig.nextScene();
    CHECK(p.scene == 2 && p.tracks[0].drums[2][0].level[6][9] == 0.f && p.tracks[0].drums[1][0].level[6][9] == 0.f);
    // Y first and SELECT after is not the gesture: not when SELECT goes down, not when Y comes up.
    rig.key(Y, true);
    rig.key(SELECT, true);
    rig.key(Y, false);
    rig.key(SELECT, false);
    CHECK(p.scene == 2 && p.sceneCount() == 5);
    rig.session().view = View::Pattern; // (SELECT, let go with nothing pressed since, is its tap: the SONG screen)
    // Y by itself afterwards is Y again.
    drums.moveCursorTo(0, 4);
    rig.tap(Y);
    CHECK(p.tracks[0].drums[2][0].chance[0][4] == 0.75f);

    // All four shoulder buttons down is a panic: nothing changes then, not a scene and not a hit.
    rig.settle();
    const std::string before = songText(p);
    const int steps = rig.session().undoSteps();
    for (SDL_Keycode shoulder : {L1, R1, L2, R2}) rig.key(shoulder, true);
    drums.moveCursorTo(6, 9);
    rig.key(SELECT, true);
    rig.key(Y, true);
    rig.key(SELECT, false);
    rig.key(Y, false);
    CHECK(p.scene == 2 && p.sceneCount() == 5 && songText(p) == before && rig.session().undoSteps() == steps);
    for (SDL_Keycode shoulder : {L1, R1, L2, R2}) rig.key(shoulder, false);
}

// ---- no room -------------------------------------------------------------------------------------

void testNoRoom() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    // Something in the fifteenth scene: there is room for exactly one more.
    p.tracks[0].drums[14][0].level[0][0] = 0.8f;
    rig.session().applyProject();
    rig.session().view = View::Pattern;
    rig.settle();
    CHECK(p.sceneCount() == 15);
    p.scene = 14; // on the fifteenth: its copy is the sixteenth, the last there is
    rig.settle();
    rig.nextScene();
    CHECK(p.scene == 15 && p.sceneCount() == 16 && rig.said() == "SCENE 16: COPY OF 15" && p.tracks[0].drums[15][0].level[0][0] == 0.8f);
    rig.settle();

    // Full. From the first scene, from the last: it says so and nothing at all changes.
    for (int scene : {0, 15}) {
        p.scene = scene;
        rig.settle();
        const std::string was = songText(p);
        const int steps = rig.session().undoSteps(), redos = rig.session().redoSteps();
        rig.nextScene();
        CHECK(rig.said() == "NO ROOM: 16 SCENES");
        CHECK(p.scene == scene && songText(p) == was && rig.session().undoSteps() == steps && rig.session().redoSteps() == redos);
        CHECK(rig.session().view == View::Pattern);
        rig.settle();
        CHECK(songText(p) == was && rig.session().undoSteps() == steps);
    }

    // On the sixteenth scene of a short song (nothing in it, nothing after scene 3): no room either, there is no "after".
    Rig other;
    fill(other);
    other.project().scene = 15;
    other.settle();
    const std::string was = songText(other.project());
    other.nextScene();
    CHECK(other.said() == "NO ROOM: 16 SCENES" && other.project().scene == 15 && songText(other.project()) == was);
    // ...but on the fifteenth of that song there is: an empty scene copied is an empty scene, and the player moves.
    other.project().scene = 14;
    other.settle();
    other.nextScene();
    CHECK(other.said() == "SCENE 16: COPY OF 15" && other.project().scene == 15 && other.project().sceneCount() == 3);
}

// ---- undo ------------------------------------------------------------------------------------------

void testUndo() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    PatternScreen& drums = rig.app.drums();
    rig.settle();
    const std::string was = songText(p);
    rig.nextScene();
    const std::string made = songText(p);
    CHECK(rig.session().undoSteps() == 1 && made != was);

    // SELECT + B: the song is as it was, and the player is back on the scene the copy was made from.
    rig.undo();
    CHECK(songText(p) == was && p.scene == 0 && p.sceneCount() == 3 && rig.said() == "UNDONE  (0 MORE)");
    CHECK(rig.session().undoSteps() == 0 && rig.session().redoSteps() == 1);
    rig.settle();
    CHECK(rig.session().undoSteps() == 0 && rig.session().redoSteps() == 1); // being put back there is not an edit
    // SELECT + A: the copy again, and the player on it.
    rig.redo();
    CHECK(songText(p) == made && p.scene == 1 && p.sceneCount() == 4 && rig.said() == "PUT BACK  (0 MORE)");
    rig.settle();
    CHECK(rig.session().undoSteps() == 1 && rig.session().redoSteps() == 0);

    // Something added on the new scene, then both taken back one at a time.
    drums.moveCursorTo(7, 3);
    rig.tap(A);
    rig.settle();
    const std::string added = songText(p);
    CHECK(rig.session().undoSteps() == 2 && p.tracks[0].drums[1][0].level[7][3] > 0.f && p.tracks[0].drums[0][0].level[7][3] == 0.f);
    rig.undo();
    CHECK(songText(p) == made && p.scene == 1); // the hit goes; the scene stays, and so does the player
    rig.undo();
    CHECK(songText(p) == was && p.scene == 0);
    rig.redo();
    CHECK(songText(p) == made && p.scene == 1);
    rig.redo();
    CHECK(songText(p) == added && p.scene == 1);

    // Taken back from somewhere else in the song, on a bar the scene gone back to does not have:
    // the player is put on a scene and a bar that are there.
    rig.undo();
    CHECK(songText(p) == made);
    rig.session().setCellBars(0, 5, 4); // scene 6: four bars
    rig.settle();
    p.scene = 5;
    p.bar = 3;
    rig.settle();
    for (int i = 0; i < Session::kUndoLevels && songText(p) != was; ++i) rig.undo();
    CHECK(songText(p) == was && p.scene == 0 && p.bar == 0 && rig.session().undoSteps() == 0);
    // Put forward from somewhere else: on the copy.
    p.scene = 7;
    rig.redo();
    CHECK(songText(p) == made && p.scene == 1 && p.bar == 0);
    // ...and on a bar the copy has, when the cell the player was on is longer than it.
    rig.undo();
    p.scene = 0;
    p.bar = 1; // the drums' cell in scene 1 is two bars; then the player looks at the synth, whose cell is one
    p.track = 1;
    rig.redo();
    CHECK(songText(p) == made && p.scene == 1 && p.bar == 0);
    p.bar = 1;
    rig.undo();
    CHECK(songText(p) == was && p.scene == 0 && p.bar == 0);
}

// The player is somewhere else in the song, on the third bar of a long cell, when the next scene is
// taken back or put forward: the cell they are put on has two bars, so they are on its first.
void testUndoPutsThePlayerOnABarThatIsThere() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    for (int scene : {8, 9, 10}) rig.session().setCellBars(0, scene, 4); // four bars each, before and after one moves down
    rig.settle();
    rig.nextScene();
    CHECK(p.scene == 1 && p.tracks[0].bars[0] == 2 && p.tracks[0].bars[1] == 2 && p.tracks[0].bars[9] == 4 && p.tracks[0].bars[10] == 4);
    p.scene = 9;
    p.bar = 2;
    rig.undo();
    CHECK(p.scene == 0 && p.bar == 0);
    p.scene = 9;
    p.bar = 2;
    rig.redo();
    CHECK(p.scene == 1 && p.bar == 0);
    // A bar the cell does have is kept.
    p.scene = 9;
    p.bar = 1;
    rig.undo();
    CHECK(p.scene == 0 && p.bar == 1);
}

// The step is its own: what was being edited just before is a step already, and stays one.
void testUndoIsOneStep() {
    {
        // A hit placed and, in the same frame, the gesture.
        Rig rig;
        fill(rig);
        Project& p = rig.project();
        rig.settle();
        const std::string was = songText(p);
        rig.app.drums().moveCursorTo(7, 3);
        rig.tap(A);
        const std::string hit = songText(p);
        rig.nextScene();
        CHECK(rig.session().undoSteps() == 2 && p.tracks[0].drums[1][0].level[7][3] > 0.f);
        rig.undo();
        CHECK(songText(p) == hit && p.scene == 0); // the scene goes; the hit stays
        rig.undo();
        CHECK(songText(p) == was);
    }
    {
        // A hit placed, a few frames go by (the burst is still open), the gesture.
        Rig rig;
        fill(rig);
        Project& p = rig.project();
        rig.settle();
        const std::string was = songText(p);
        rig.app.drums().moveCursorTo(7, 3);
        rig.tap(A);
        rig.frames(5);
        const std::string hit = songText(p);
        rig.nextScene();
        const std::string made = songText(p);
        // The burst ended with the gesture: a hit a moment later begins a step of its own.
        rig.frames(5);
        rig.app.drums().moveCursorTo(7, 9);
        rig.tap(A);
        rig.settle();
        CHECK(rig.session().undoSteps() == 3);
        rig.undo();
        CHECK(songText(p) == made && p.scene == 1);
        rig.undo();
        CHECK(songText(p) == hit && p.scene == 0);
        rig.undo();
        CHECK(songText(p) == was);
    }
    {
        // Before the first frame has been drawn: the gesture is still a step.
        Rig rig;
        fill(rig);
        Project& p = rig.project();
        rig.settle();
        p.tracks[0].drums[0][0].level[7][7] = 0.6f; // (so what undo last saw is not the song as it is now)
        const std::string was = songText(p);
        rig.session().applyProject(); // a song just opened: undo starts again
        rig.session().view = View::Pattern;
        rig.nextScene();
        CHECK(rig.session().undoSteps() == 1);
        rig.settle();
        CHECK(rig.session().undoSteps() == 1);
        rig.undo();
        CHECK(songText(p) == was && p.scene == 0);
    }
    {
        // Twenty steps deep and no deeper; and a new step leaves nothing to put forward.
        Rig rig;
        fill(rig);
        Project& p = rig.project();
        for (int i = 0; i < Session::kUndoLevels + 1; ++i) {
            rig.app.drums().moveCursorTo(4 + i / kSteps, i % kSteps);
            rig.tap(A);
            rig.settle();
        }
        CHECK(rig.session().undoSteps() == Session::kUndoLevels);
        rig.undo();
        rig.undo();
        rig.undo();
        CHECK(rig.session().undoSteps() == Session::kUndoLevels - 3 && rig.session().redoSteps() == 3);
        rig.nextScene();
        CHECK(rig.session().undoSteps() == Session::kUndoLevels - 2 && rig.session().redoSteps() == 0);
        rig.nextScene();
        rig.nextScene();
        rig.nextScene();
        CHECK(rig.session().undoSteps() == Session::kUndoLevels && p.scene == 4 && p.sceneCount() == 7);
        rig.undo();
        CHECK(p.scene == 3 && p.sceneCount() == 6);
    }
}

// ---- playing -------------------------------------------------------------------------------------

// One scene going round. The gesture in the middle of a step, then undo, then redo, each in the
// middle of the music: the same sound as a song nobody touched, to the last bit.
void testPlayingGoesOnUnbroken() {
    Rig plain, rig;
    fill(plain);
    fill(rig);
    Project& p = rig.project();
    const auto both = [&](int audioFrames) {
        plain.frames(1);
        rig.frames(1);
        plain.render(audioFrames);
        rig.render(audioFrames);
    };
    plain.session().togglePlay(false);
    rig.session().togglePlay(false);
    both(kBar + 2 * kStep + 777); // into the second bar, in the middle of its third step: the long note is sounding
    CHECK(rig.engine->playScene() == 0 && rig.engine->playStep() == 2 && rig.engine->playBar() == 1);

    rig.frames(1);
    rig.nextScene();
    CHECK(p.scene == 1 && p.playing && p.playScene == 1); // what the screens draw from follows at once
    both(16);
    // The engine is on the new scene, at once (not at the next step, not at the loop's end), where it was in the bar.
    CHECK(rig.engine->playScene() == 1 && rig.engine->playStep() == 2 && rig.engine->playBar() == 1);
    CHECK(loudest(rig.heard, rig.heard.size() / 2 - 16, rig.heard.size() / 2) > 0.01f); // and there is sound to break
    both(3 * kBar + 1234);
    CHECK(rig.engine->playScene() == 1 && p.playScene == 1);

    rig.undo();
    CHECK(p.scene == 0 && p.playScene == 0);
    both(16);
    CHECK(rig.engine->playScene() == 0);
    both(2 * kBar + 4321);

    rig.redo();
    CHECK(p.scene == 1 && p.playScene == 1);
    both(16);
    CHECK(rig.engine->playScene() == 1);
    both(2 * kBar);

    const float off = apart(plain.heard, rig.heard);
    std::printf("  a scene looping, NEXT SCENE then undo then redo while it plays: at most %g from the song left alone, over %.1f s\n",
                static_cast<double>(off), static_cast<double>(rig.heard.size()) / 2.0 / 48000.0);
    CHECK(off == 0.f);
    CHECK(loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);
}

// The song, played from the top. The gesture during scene 1's first time round: the copy takes over
// in mid-flight and the song is heard as it would have been to its very end; taken back in time, it
// is the same song for good.
void testTheSongGoesOnUnbroken() {
    Rig plain, rig, back;
    fill(plain);
    fill(rig);
    fill(back);
    for (Rig* r : {&plain, &rig, &back}) r->session().togglePlay(true);
    const auto all = [&](int audioFrames) {
        for (Rig* r : {&plain, &rig, &back}) {
            r->frames(1);
            r->render(audioFrames);
        }
    };
    all(18 * kStep + 2501); // scene 1, first time, in its second bar
    rig.nextScene();
    back.nextScene();
    all(16);
    CHECK(rig.engine->playScene() == 1 && rig.engine->playRepeat() == 0 && rig.engine->playBar() == 1 && rig.engine->playStep() == 2);
    all(10 * kStep); // into its second time round
    CHECK(rig.engine->playScene() == 1 && rig.engine->playRepeat() == 1);
    back.undo();
    all(16);
    CHECK(back.engine->playScene() == 0 && back.engine->playRepeat() == 1 && rig.engine->playScene() == 1);
    const int sofar = static_cast<int>(rig.heard.size() / 2);
    all(kSongSteps * kStep - sofar); // to the song's last sample
    all(1);
    CHECK(plain.engine->playScene() == 0 && back.engine->playScene() == 0); // round to the top
    CHECK(rig.project().sceneCount() == 4 && rig.engine->playScene() == 0); // (the song with the copy in it is as long so far: it has played the copy in place of scene 1's second time)
    {
        std::vector<float> a(plain.heard.begin(), plain.heard.end() - 2), b(rig.heard.begin(), rig.heard.end() - 2);
        const float off = apart(a, b);
        std::printf("  the song, NEXT SCENE pressed in scene 1: at most %g from the song left alone, to its end (%.1f s)\n", static_cast<double>(off),
                    static_cast<double>(a.size()) / 2.0 / 48000.0);
        CHECK(off == 0.f);
    }
    all(kSongSteps * kStep / 2);
    const float off = apart(plain.heard, back.heard);
    std::printf("  ...and taken back while it played: at most %g from the song left alone, a time and a half through\n", static_cast<double>(off));
    CHECK(off == 0.f);
    CHECK(apart(plain.heard, rig.heard) > 0.01f); // the song with the copy in it has gone its own way by now
}

// Another scene is the one going round, not the one on the screen. A scene after the copy moves
// down and the playing goes with it; a scene before it stays where it is.
void testAnotherSceneIsPlaying() {
    for (int playing : {2, 0}) {
        Rig plain, rig;
        fill(plain);
        fill(rig);
        for (Rig* r : {&plain, &rig}) {
            r->project().scene = playing;
            r->session().togglePlay(false);
            r->project().scene = 1; // the player goes off to scene 2
            r->frames(1);
            r->render(5 * kStep + 99);
            r->frames(1);
        }
        rig.nextScene();
        const int now = playing == 2 ? 3 : 0;
        CHECK(rig.project().scene == 2 && rig.project().playScene == now);
        for (Rig* r : {&plain, &rig}) r->render(2 * kBar + 7), r->frames(1);
        CHECK(rig.engine->playScene() == now && rig.project().playScene == now);
        rig.undo();
        CHECK(rig.project().scene == 1 && rig.project().playScene == playing);
        for (Rig* r : {&plain, &rig}) r->render(2 * kBar + 7), r->frames(1);
        CHECK(rig.engine->playScene() == playing);
        rig.redo();
        for (Rig* r : {&plain, &rig}) r->render(kBar), r->frames(1);
        CHECK(rig.engine->playScene() == now && rig.project().playScene == now);
        CHECK(apart(plain.heard, rig.heard) == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.05f);
    }
}

// The scene the copy was made from is the one going round when the copy is taken back (the player
// went back to it and pressed START): it is not one of those that move, and the playing stays on it.
void testUndoWhileTheFirstOfTheTwoPlays() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    p.scene = 1;
    rig.settle();
    const std::string was = songText(p);
    const int steps = rig.session().undoSteps();
    rig.nextScene();
    CHECK(p.scene == 2);
    p.scene = 1;
    rig.session().togglePlay(false);
    p.scene = 2; // (back before a frame is drawn: going to another scene is itself a step of undo)
    rig.frames(1);
    rig.render(3 * kStep);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 1 && rig.session().undoSteps() == steps + 1);
    rig.undo();
    CHECK(songText(p) == was && p.scene == 1 && p.playScene == 1);
    rig.render(kStep);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 1 && p.playScene == 1);
}

// The sixteenth scene, with nothing in it, is going round, and a copy is made further up: there is
// no seventeenth to move to. The playing stays, and so does what the screens are told.
void testTheLastSceneIsPlaying() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    p.scene = 15;
    rig.session().togglePlay(false);
    p.scene = 0;
    rig.frames(1);
    rig.render(3 * kStep);
    rig.frames(1);
    CHECK(p.playScene == 15);
    rig.nextScene();
    CHECK(p.scene == 1 && p.playScene == 15);
    rig.render(kStep);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 15 && p.playScene == 15);
}

// The song's last scene is playing, the copy is made, and the bar ends before another frame is
// drawn: the engine already knows the song is a scene longer, plays the copy out, and goes round to the top.
void testTheSongIsLongerAtOnce() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    p.scene = 2;
    rig.frames(1);
    rig.session().togglePlay(true);
    rig.frames(1);
    rig.render(2 * 13 * kStep + 5 * kStep + 50); // its third and last time round
    rig.frames(1);
    CHECK(rig.engine->playScene() == 2 && rig.engine->playRepeat() == 2);
    rig.nextScene();
    rig.render(6 * kStep); // no frame in between
    CHECK(rig.engine->playScene() == 3 && rig.engine->playRepeat() == 2 && rig.engine->playStep() == 11);
    rig.render(3 * kStep);
    CHECK(rig.engine->playScene() == 0 && rig.engine->playRepeat() == 0 && rig.engine->playStep() == 1);
}

// Stopped: nothing starts playing, and the engine is not left thinking it is somewhere.
void testStopped() {
    Rig rig;
    fill(rig);
    rig.nextScene();
    rig.frames(1);
    rig.render(4000);
    rig.frames(1);
    CHECK(!rig.project().playing && rig.engine->playScene() == -1 && rig.project().playScene == -1);
    CHECK(loudest(rig.heard, 0, 4000) == 0.f);
    rig.undo();
    rig.render(4000);
    rig.frames(1);
    CHECK(rig.engine->playScene() == -1 && rig.project().playScene == -1 && loudest(rig.heard, 0, 8000) == 0.f);
}

// The engine has all of it: the new song played from the top by an engine that got it through the
// gesture is, to the bit, the song an engine plays that was handed the finished thing.
void testTheEngineHasTheWholeSong() {
    Rig made, undone, redone, plain;
    for (Rig* r : {&made, &undone, &redone, &plain}) fill(*r);
    const std::unique_ptr<Project> whole = withCopyAfter(plain.project(), 0);
    for (Rig* r : {&made, &undone, &redone}) r->nextScene();
    undone.undo();
    redone.undo();
    redone.redo();
    Rig handed; // an engine as new as the others: the song goes to it once, as theirs did
    CHECK(handed.session().addTrack(TrackKind::Phazed));
    handed.project() = *whole;
    handed.session().applyProject();
    handed.session().view = View::Pattern;
    handed.frames(2);
    handed.render(512); // (as fill() does: what a new song's effects do in their first moments is over)
    for (Rig* r : {&made, &undone, &redone, &plain, &handed}) {
        r->project().scene = 0;
        r->frames(2);
        r->render(512);
        r->heard.clear();
        r->session().togglePlay(true);
        r->frames(1);
        r->render((kSongSteps + 23 * 2) * kStep + kBar); // the longer song once through, and a bar of the next time round
    }
    CHECK(loudest(handed.heard, 0, handed.heard.size() / 2) > 0.1f);
    CHECK(apart(made.heard, handed.heard) == 0.f);
    CHECK(apart(redone.heard, handed.heard) == 0.f);
    CHECK(apart(undone.heard, plain.heard) == 0.f);
    CHECK(apart(plain.heard, handed.heard) > 0.01f); // (the two songs do not sound alike)
}

// A knob moved with WRITE on, straight after the gesture, is written into the scene that is playing
// now: the new one. The engine says where it is at once, not at the next step.
void testWriteGoesToTheNewScene() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    p.track = 1;
    rig.frames(1);
    rig.session().togglePlay(false);
    rig.frames(1);
    rig.render(5 * kStep + 100);
    rig.frames(1);
    rig.session().setWriting(true);
    rig.nextScene();
    rig.render(64); // the engine has heard; no step has begun since
    CHECK(rig.engine->playStep() == 5);
    rig.session().setSynthMacro(1, 3, 0.9f);
    CHECK(p.tracks[1].automation[1][3][5] == 0.9f && p.tracks[1].automation[0][3][5] < 0.f);
}

// The engine's command, by itself: it moves the playing only for a scene that has moved, only by one, and never off the song.
void testTheCommand() {
    const auto at = [](int scene, int from, int by, bool play = true) {
        Engine engine;
        engine.prepare(48000.f);
        engine.addDrumTrack(0);
        Command c;
        c.type = Command::SetSceneCount;
        c.a = kMaxScenes;
        engine.send(c);
        if (play) {
            c.type = Command::Play;
            c.a = scene;
            c.b = 1;
            engine.send(c);
        }
        std::vector<float> out(2 * 256);
        engine.render(out.data(), 256);
        c.type = Command::ScenesMoved;
        c.a = from;
        c.b = by;
        engine.send(c);
        engine.render(out.data(), 256);
        return engine.playScene();
    };
    CHECK(at(3, 3, 1) == 4 && at(3, 0, 1) == 4 && at(3, 4, 1) == 3); // itself and those after it; not one before
    CHECK(at(3, 3, -1) == 2 && at(3, 4, -1) == 3 && at(3, 1, -1) == 2);
    CHECK(at(15, 0, 1) == 15 && at(14, 0, 1) == 15); // not off the end
    CHECK(at(0, 0, -1) == 0 && at(1, 0, -1) == 0);   // nor off the top
    CHECK(at(3, 0, 2) == 3 && at(3, 0, 0) == 3 && at(3, 0, -2) == 3 && at(3, 0, 12) == 3); // one place, down or up: nothing else
    CHECK(at(3, -1, 1) == 3 && at(3, kMaxScenes, -1) == 3 && at(3, -5, -1) == 3);           // from a scene there is
    CHECK(at(3, 0, 1, false) == -1);                                                         // stopped: it is nowhere, and stays there
}

} // namespace

int main() {
    ui::gStickSpeed = 1.f;
    testTheSceneIsCopied();
    testOnlyOnTheGrids();
    testNoRoom();
    testUndo();
    testUndoPutsThePlayerOnABarThatIsThere();
    testUndoIsOneStep();
    testPlayingGoesOnUnbroken();
    testTheSongGoesOnUnbroken();
    testAnotherSceneIsPlaying();
    testUndoWhileTheFirstOfTheTwoPlays();
    testTheLastSceneIsPlaying();
    testTheSongIsLongerAtOnce();
    testStopped();
    testTheEngineHasTheWholeSong();
    testWriteGoesToTheNewScene();
    testTheCommand();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("next scene tests passed\n");
    return 0;
}
