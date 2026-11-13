// NEXT SCENE (SELECT + Y on the drum grid and the piano roll): the scene being
// worked on is copied into a new one put in right after it, the player is put
// there, and a song that is playing does not miss a step.
//
// "Does not miss a step" is checked on the sound itself: two engines play the
// same song, the gesture is pressed in the middle of a bar on one of them, and
// what comes out of the two is compared sample for sample.

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "song_file.hpp"

using namespace apotheca;

// Every new and delete of this program is counted (the engine's and the app's are C++'s): how
// many bytes are held, and how many of each were made while gInRender was set. That is how a
// test can say "the audio thread freed nothing" and "everything handed over came back".
#include <cstdlib>
#include <new>
static std::atomic<long long> gHeldBytes{0};
static std::atomic<long> gNewsInRender{0}, gDeletesInRender{0};
static std::atomic<bool> gInRender{false};
static const std::size_t kCountRoom = 16; // (in front of each piece: its size; and it stays aligned to 16)
static void* countedNew(std::size_t size) {
    void* const base = std::malloc(size + kCountRoom);
    if (!base) throw std::bad_alloc();
    *static_cast<std::size_t*>(base) = size;
    gHeldBytes += static_cast<long long>(size);
    if (gInRender) ++gNewsInRender;
    return static_cast<char*>(base) + kCountRoom;
}
static void countedDelete(void* p) noexcept {
    if (!p) return;
    void* const base = static_cast<char*>(p) - kCountRoom;
    gHeldBytes -= static_cast<long long>(*static_cast<std::size_t*>(base));
    if (gInRender) ++gDeletesInRender;
    std::free(base);
}
void* operator new(std::size_t size) { return countedNew(size); }
void* operator new[](std::size_t size) { return countedNew(size); }
void operator delete(void* p) noexcept { countedDelete(p); }
void operator delete[](void* p) noexcept { countedDelete(p); }
void operator delete(void* p, std::size_t) noexcept { countedDelete(p); }
void operator delete[](void* p, std::size_t) noexcept { countedDelete(p); }

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

int gHelpers = 0; // the helper threads every engine below is given (the tracks are rendered side by side on them)

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::vector<float> heard; // everything rendered, end to end

    Rig() { engine->useWorkers(gHelpers); }

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

// A chord with its effects (chord_rule.hpp), for the song below.
ChordEvent chordOf(int step, float length, int root, int keys, int strum, int order, int time, int level, int lengths, int ratchet = 0, int throwTo = 0, float nudge = 0.f) {
    static const int kIntervals[kChordNotes] = {0, 4, 7, 11, 14, 17, 21, 24};
    ChordEvent c;
    c.step = step;
    c.length = length;
    c.root = root;
    c.count = keys;
    for (int i = 0; i < keys; ++i) c.intervals[i] = kIntervals[i];
    c.velocity = 0.6f;
    c.nudge = nudge;
    c.ratchet = static_cast<std::uint8_t>(ratchet);
    c.throwTo = static_cast<std::uint8_t>(throwTo);
    c.play = chordPlayOf(strum, order, time, level, lengths);
    std::snprintf(c.name, sizeof c.name, "C%d", step);
    return c;
}

// Track 3 of the song below: chords strummed, played as arpeggios, humanised, ratcheted and
// thrown, in every ORDER, and a glide of every length. Everything about them that the engine
// keeps while they play (an arpeggio's clock, the chord asked for a step ahead, notes waiting,
// what a glide comes from) is in flight wherever in the song a scene is moved.
void fillChords(TrackData& t) {
    t.bars[0] = 2;
    NotePattern& a = t.notes[0][0];
    a.chords[0] = chordOf(0, 2.f, 48, 3, kStrumTight, kOrderUpDown, 0, 0, 0, 2, kThrowEcho);                 // strummed on the song's first step, a ratchet, thrown
    a.notes[0] = NoteEvent{2, 72, 1.f, 0.f, 0.7f, 0, kGlideQuarter, 0, 0};
    a.chords[1] = chordOf(3, 4.f, 50, 4, kStrumQuarter, kOrderRandom, 50, 75, 50, 0, kThrowSpace);          // an arpeggio, everything humanised
    a.notes[1] = NoteEvent{7, 60, 0.75f, 0.f, 0.7f, 0, kGlideHalf, 0, 0};
    a.notes[2] = NoteEvent{8, 67, 1.f, 0.f, 0.7f, 2, kGlideOne, 0, 0};                                        // a ratchet that glides
    a.chords[2] = chordOf(9, 2.5f, 52, 3, kStrumWide, kOrderDown, 100, 100, 0, 3, 0, -0.25f);                // early: asked for a step ahead
    a.notes[3] = NoteEvent{12, 76, 1.5f, 0.25f, 0.7f, 0, kGlideTwo, 3, 0};                                    // late, thrown to both
    a.chords[3] = chordOf(14, 2.f, 45, 5, kStrumOff, kOrderRandom, 75, 0, 100);                               // HUMAN TIME alone
    NotePattern& b = t.notes[0][1]; // (the scene is cut to 23 steps: seven of this bar are played)
    b.chords[0] = chordOf(0, 4.f, 53, 3, kStrumStep, kOrderUpDown, 100, 50, 0);                               // an arpeggio asked for from the bar before
    b.chords[1] = chordOf(4, 4.f, 55, 4, kStrumHalf, kOrderDown, 0, 0, 25, 2, kThrowEcho);                   // ...and one still going when the scene ends
    b.notes[0] = NoteEvent{6, 65, 1.f, 0.f, 0.7f, 0, kGlideFull, 0, 0};
    NotePattern& c = t.notes[1][0];
    c.chords[0] = chordOf(2, 3.f, 57, 4, kStrumTight, kOrderRandom, 0, 50, 0, 4, 3);
    c.notes[0] = NoteEvent{6, 69, 2.f, 0.f, 0.7f, 0, kGlideFull, 0, 0};
    c.chords[1] = chordOf(10, 5.f, 59, 3, kStrumQuarter, kOrderUp, 25, 0, 0);
    c.notes[1] = NoteEvent{15, 62, 1.f, -0.3f, 0.7f, 0, kGlideTwo, 0, 0};
    NotePattern& d = t.notes[2][0]; // (thirteen steps, three times)
    d.chords[0] = chordOf(0, 3.f, 43, 4, kStrumWide, kOrderUpDown, 50, 0, 0, 0, 0, -0.2f);                   // asked for from the scene before, and from its own last step
    d.notes[0] = NoteEvent{5, 79, 1.f, 0.f, 0.7f, 3, kGlideQuarter, 1, 0};
    d.chords[1] = chordOf(9, 6.f, 47, 3, kStrumHalf, kOrderRandom, 100, 100, 75);                             // an arpeggio that runs on into the next time round
}

// A song with something of every kind a scene holds, and no two scenes alike:
// track 1 drums, track 2 Phazed, track 3 Phazed with the chords above; three scenes.
//   scene 1: the drums' cell two bars long, the synth's one; cut to 23 steps; played twice
//   scene 2: one bar, once
//   scene 3: cut to 13 steps, three times
void fill(Rig& rig) {
    Project& p = rig.project();
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    fillChords(p.tracks[2]);
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

// ---- one track's patterns a frame ---------------------------------------------------------------

// Twelve tracks, the nine added ones each with a note of its own in each of the three scenes.
void fillTwelve(Rig& rig, bool withCopy = false) {
    fill(rig);
    for (int i = 0; i < 9; ++i) {
        CHECK(rig.session().addTrack(TrackKind::Phazed));
        for (int scene = 0; scene < 3; ++scene)
            rig.project().tracks[3 + i].notes[scene][0].notes[0] = NoteEvent{(i * 3 + scene * 5) % 13, 48 + i + 2 * scene, 1.5f, 0.f, 0.5f, 0, 0, 0, 0};
    }
    if (withCopy) rig.project() = *withCopyAfter(rig.project(), 0); // the song as NEXT SCENE on scene 1 leaves it, handed to the engine whole
    rig.session().applyProject();
    rig.frames(2);
    rig.render(512);
    rig.heard.clear();
}

// Twelve tracks, one scene going round, a block of sound made every frame: the gesture, undo and
// redo, one on top of another. The engine is told each in one command and has moved by the next
// block; no track's patterns are sent whole. The sound is the song left alone, to the bit.
void testTwelveTracksInOneStep() {
    Rig plain, rig, handed; // (handed: the song with the copy in it from the start, and the same scene going round all the while)
    fillTwelve(plain);
    fillTwelve(rig);
    fillTwelve(handed, true);
    Project& p = rig.project();
    Session& session = rig.session();
    const auto both = [&](int frames) {
        for (int i = 0; i < frames; ++i)
            for (Rig* r : {&plain, &rig, &handed}) {
                r->frames(1);
                r->render(256);
            }
    };
    for (Rig* r : {&plain, &rig, &handed}) r->session().togglePlay(false);
    both(200);
    CHECK(rig.engine->playScene() == 0);
    const unsigned images = session.imagesSent();

    rig.nextScene();
    CHECK(p.scene == 1 && p.playScene == 1 && songText(p) == songText(*withCopyAfter(plain.project(), 0)));
    CHECK(rig.engine->playScene() == 0 && session.sceneMovesUnheard() == 1);
    both(1);
    CHECK(rig.engine->playScene() == 1 && p.playScene == 1); // one block: all of it
    both(1);
    CHECK(session.sceneMovesUnheard() == 0);
    both(300);
    CHECK(rig.engine->playScene() == 1);

    rig.undo();
    CHECK(p.scene == 0 && p.playScene == 0 && songText(p) == songText(plain.project()));
    both(1);
    CHECK(rig.engine->playScene() == 0);
    both(300);

    rig.redo();
    CHECK(p.scene == 1 && p.playScene == 1);
    both(1);
    CHECK(rig.engine->playScene() == 1);
    both(300);

    // One on top of another, with a few blocks or none in between.
    rig.undo();
    both(4);
    rig.redo();
    CHECK(session.sceneMovesUnheard() == 1);
    both(3);
    rig.undo();
    CHECK(p.playScene == 0 && p.scene == 0);
    both(2);
    CHECK(rig.engine->playScene() == 0);
    rig.nextScene();
    CHECK(p.scene == 1);
    both(300);
    CHECK(rig.engine->playScene() == 1);
    // A copy of the copy, and two taken back and put back in a hurry.
    rig.undo();
    both(300);
    rig.nextScene();
    both(3);
    rig.nextScene();
    CHECK(p.scene == 2 && p.playScene == 2);
    both(1);
    CHECK(rig.engine->playScene() == 2);
    both(300);
    rig.undo();
    both(5);
    rig.undo();
    CHECK(p.playScene == 0);
    both(300);
    CHECK(rig.engine->playScene() == 0 && songText(p) == songText(plain.project()));
    rig.redo();
    rig.redo(); // (no block in between: the two go in order)
    CHECK(p.scene == 2 && p.playScene == 2 && session.sceneMovesUnheard() == 2);
    for (int i = 0; i < 12; ++i) // (longer blocks: steps begin in them)
        for (Rig* r : {&plain, &rig, &handed}) {
            r->frames(1);
            r->render(1024);
        }
    both(300);
    CHECK(rig.engine->playScene() == 2);
    rig.undo();
    both(300);
    CHECK(rig.engine->playScene() == 1);
    CHECK(session.imagesSent() == images && rig.engine->dropped() == 0); // not one track was sent whole, by the gesture or by taking it back

    const float off = apart(plain.heard, rig.heard);
    std::printf("  twelve tracks, a scene looping, a block every frame, NEXT SCENE / undo / redo: at most %g from the song left alone, over %.1f s\n",
                static_cast<double>(off), static_cast<double>(rig.heard.size()) / 2.0 / 48000.0);
    CHECK(off == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);

    // And the engine has the whole new song: stopped, then played from the top beside one handed it whole.
    CHECK(apart(plain.heard, handed.heard) == 0.f);
    for (Rig* r : {&rig, &handed}) {
        r->session().togglePlay(false); // stop
        r->frames(2);
        r->render(48000);
        r->project().scene = 0;
        r->frames(1);
        r->heard.clear();
        r->session().togglePlay(true);
        r->frames(1);
        r->render((kSongSteps + 23 * 2) * kStep);
    }
    CHECK(apart(rig.heard, handed.heard) == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);
}

// A later scene going round, the song playing through, the song started in the very frame of the
// press: the same one step. The engine has moved by its next block, with no frame in between.
void testInOneStepWhateverPlays() {
    for (int kind = 0; kind < 3; ++kind) { // 0: a later scene loops; 1: the song plays; 2: started this very frame
        Rig plain, rig;
        fillTwelve(plain);
        fillTwelve(rig);
        for (Rig* r : {&plain, &rig}) {
            r->project().scene = kind == 0 ? 2 : 0;
            r->session().togglePlay(kind == 1);
            r->project().scene = 0;
            if (kind != 2) r->frames(1), r->render(3 * kStep + 50), r->frames(1);
        }
        const unsigned images = rig.session().imagesSent();
        rig.nextScene();
        for (Rig* r : {&plain, &rig}) r->render(256); // no frame
        CHECK(rig.engine->playScene() == (kind == 0 ? 3 : 1));
        for (Rig* r : {&plain, &rig}) r->render(kBar), r->frames(1);
        rig.undo();
        for (Rig* r : {&plain, &rig}) r->render(256);
        CHECK(rig.engine->playScene() == (kind == 0 ? 2 : 0));
        for (Rig* r : {&plain, &rig}) r->render(kBar), r->frames(1);
        rig.redo();
        for (Rig* r : {&plain, &rig}) r->render(kBar), r->frames(1);
        CHECK(rig.session().imagesSent() == images);
        CHECK(apart(plain.heard, rig.heard) == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.05f);
    }
}

// Stopped, then START at once, before the engine has made a block: what plays is the new song from its first sample.
void testStartStraightAfter() {
    Rig rig, handed;
    fillTwelve(rig);
    fillTwelve(handed, true);
    handed.project().scene = 1;
    rig.nextScene();
    CHECK(songText(rig.project()) == songText(handed.project()));
    for (Rig* r : {&rig, &handed}) {
        r->session().togglePlay(true); // the song, from the new scene (the old scene 2 comes next)
        r->render(50 * kStep);
    }
    CHECK(rig.engine->playScene() == 2);
    CHECK(apart(rig.heard, handed.heard) == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);
}

// Things done straight after, before the engine has heard. A hit and a note placed on the new
// scene are in what the engine ends with; a track deleted leaves the engine and the app agreed.
void testEditsStraightAfter() {
    // Two the same but for when the keys are pressed: on one before the engine has made a
    // block, on the other long after. What the engine then plays is the same.
    Rig rig, after;
    for (Rig* r : {&rig, &after}) {
        fillTwelve(*r);
        r->session().togglePlay(false);
        r->frames(1);
        r->render(256);
        r->nextScene();
        if (r == &after) r->frames(1), r->render(256), r->frames(1), r->render(256);
        r->project().track = 7;
        r->app.notes().moveCursorTo(70, 9);
        r->tap(A);
        r->project().track = 0;
        r->app.drums().moveCursorTo(5, 3);
        r->tap(A);
        if (r == &rig) r->frames(1), r->render(256), r->frames(1), r->render(256);
        for (int i = 0; i < 400; ++i) r->frames(1), r->render(256);
        CHECK(r->engine->playScene() == 1);
        CHECK(r->project().tracks[7].notes[1][0].notes[1].velocity > 0.f && r->project().tracks[0].drums[1][0].level[5][3] > 0.f);
    }
    CHECK(apart(rig.heard, after.heard) == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);

    Rig cut;
    fillTwelve(cut);
    cut.session().togglePlay(false);
    cut.frames(1);
    cut.render(256);
    cut.nextScene();
    CHECK(cut.session().removeTrack(11)); // (it stops the song and hands the engine everything afresh)
    cut.frames(1);
    cut.render(256);
    cut.frames(1);
    CHECK(cut.session().sceneMovesUnheard() == 0 && cut.engine->playScene() == -1 && cut.engine->dropped() == 0);
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
    both(8);
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
    both(8);
    CHECK(rig.engine->playScene() == 1);
    both(2 * kBar);

    const float off = apart(plain.heard, rig.heard);
    std::printf("  a scene looping, NEXT SCENE then undo then redo while it plays: at most %g from the song left alone, over %.1f s\n",
                static_cast<double>(off), static_cast<double>(rig.heard.size()) / 2.0 / 48000.0);
    CHECK(off == 0.f);
    CHECK(loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);
    // ...and the same sound on any number of helper threads.
    static std::vector<float> alone;
    if (gHelpers == 0) alone = plain.heard;
    else CHECK(apart(alone, plain.heard) == 0.f);
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
    static std::vector<float> alone, copied; // ...and each is the same sound on any number of helper threads
    if (gHelpers == 0) alone = plain.heard, copied = rig.heard;
    else CHECK(apart(alone, plain.heard) == 0.f && apart(copied, rig.heard) == 0.f);
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

// The frames between the press and the audio thread's next block: the engine has not heard yet
// and still says the scene it was on. The app does not take that for where the playing is: the
// playhead stays on the grid, and a knob turned with WRITE on in that very frame is written into
// the new scene, not the one that was to stay as it was. The same when it is taken back.
void testTheFramesBeforeTheEngineHears() {
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
    rig.frames(3); // frames drawn, and no block of sound made in between
    CHECK(rig.engine->playScene() == 0 && rig.session().sceneMovesUnheard() == 1); // (the engine has not heard)
    CHECK(p.scene == 1 && p.playScene == 1);
    rig.session().setSynthMacro(1, 3, 0.9f);
    CHECK(p.tracks[1].automation[1][3][5] == 0.9f && p.tracks[1].automation[0][3][5] < 0.f);
    CHECK(rig.session().automated(1, 3, 0.25f) == 0.9f); // and read back from where it was written
    rig.render(64);
    CHECK(rig.engine->playScene() == 1 && rig.session().automated(1, 3, 0.25f) == 0.9f); // heard, and no frame yet: not moved twice
    rig.frames(1);
    CHECK(rig.engine->playScene() == 1 && p.playScene == 1 && rig.session().sceneMovesUnheard() == 0); // heard: not moved a second time
    rig.frames(1);
    CHECK(p.playScene == 1);
    rig.session().setWriting(false);
    rig.settle();
    rig.undo(); // (the knob's move, a step of its own)
    rig.render(64);
    rig.frames(1);
    CHECK(p.scene == 1 && p.tracks[1].automation[1][3][5] < 0.f);
    rig.session().setWriting(true);

    // Taken back: the same the other way.
    rig.undo();
    rig.frames(3);
    CHECK(rig.engine->playScene() == 1 && p.scene == 0 && p.playScene == 0);
    rig.session().setSynthMacro(1, 3, 0.8f);
    CHECK(p.tracks[1].automation[0][3][5] == 0.8f && p.tracks[1].automation[1][3][5] < 0.f);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 0 && p.playScene == 0);

    // Two before the engine hears either (the press, and it taken back at once): where it was.
    rig.session().setWriting(false);
    rig.settle();
    rig.nextScene();
    rig.frames(2);
    CHECK(rig.session().sceneMovesUnheard() == 1);
    rig.undo();
    rig.frames(1);
    CHECK(rig.engine->playScene() == 0 && p.playScene == 0 && rig.session().sceneMovesUnheard() == 2);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 0 && p.playScene == 0 && rig.session().sceneMovesUnheard() == 0);
    rig.frames(2);
    rig.redo();
    rig.frames(1);
    CHECK(rig.session().sceneMovesUnheard() == 1 && p.playScene == 1);
    rig.undo();
    CHECK(rig.session().sceneMovesUnheard() == 2 && p.playScene == 0);
    rig.frames(3);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 0 && p.playScene == 0 && rig.session().sceneMovesUnheard() == 0);
    rig.redo();
    rig.frames(1);
    rig.undo();
    rig.redo();
    rig.frames(2);
    CHECK(rig.engine->playScene() == 0 && p.playScene == 1);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 1 && p.playScene == 1);

    // A move that moves nothing is heard all the same (stopped; and a scene before the copy going
    // round, asked before its next step): it is not kept waiting for, to be applied to what plays later.
    rig.session().togglePlay(false);
    rig.frames(1);
    rig.render(64);
    rig.frames(1);
    rig.nextScene();
    rig.frames(2);
    CHECK(rig.session().sceneMovesUnheard() == 1 && p.playScene == -1);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.session().sceneMovesUnheard() == 0 && p.playScene == -1 && rig.engine->playScene() == -1);
    p.scene = 0;
    rig.session().togglePlay(false);
    p.scene = 4;
    rig.frames(1);
    rig.render(kStep + 100);
    rig.frames(1);
    rig.nextScene();
    rig.frames(2);
    CHECK(rig.session().sceneMovesUnheard() == 1 && p.playScene == 0);
    rig.render(64);
    rig.frames(1);
    CHECK(rig.session().sceneMovesUnheard() == 0 && p.playScene == 0 && rig.engine->playScene() == 0 && rig.engine->playStep() == 1);
}

// The SONG screen's cursor is on a scene by its number, so it goes with the scenes too: after the
// gesture a tap of SELECT shows SONG with its cursor on the new scene, not on the one just left.
void testTheSongScreensCursorFollows() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    SongScreen& song = rig.app.song();
    const auto goTo = [&](int scene) { // the player, to a scene, with things at rest again
        rig.session().view = View::Pattern;
        p.scene = scene;
        rig.frames(1);
        rig.settle();
    };
    song.moveCursorTo(0, 1);
    rig.nextScene();
    CHECK(song.cursorScene() == 1 && song.cursorColumn() == 1 && !song.cursorOnHead());
    rig.frames(1);
    rig.tap(SELECT);
    CHECK(rig.session().view == View::Song);
    rig.undo(); // (from any screen)
    CHECK(song.cursorScene() == 0 && p.scene == 0);
    rig.redo();
    CHECK(song.cursorScene() == 1);
    rig.undo();

    // On a later scene: it goes down with that scene. On an earlier one: it stays.
    goTo(0);
    song.moveCursorTo(2, 0);
    rig.nextScene(); // (of scene 1)
    CHECK(song.cursorScene() == 3);
    rig.undo();
    CHECK(song.cursorScene() == 2);
    goTo(1);
    song.moveCursorTo(0, 0);
    rig.nextScene(); // (of scene 2)
    CHECK(song.cursorScene() == 0 && p.scene == 2);
    rig.undo();
    CHECK(song.cursorScene() == 0);
    song.moveCursorTo(1, 0);
    rig.redo();
    CHECK(song.cursorScene() == 2 && p.scene == 2); // on the scene copied: it goes to the copy, with the player
    rig.undo();
    CHECK(song.cursorScene() == 1);
    song.moveCursorTo(2, 0);
    rig.redo();
    CHECK(song.cursorScene() == 3);
    rig.undo();
    CHECK(song.cursorScene() == 2 && p.sceneCount() == 3);

    // No room: nothing moved, the cursor neither. An ordinary undo: neither.
    goTo(15);
    song.moveCursorTo(15, 0);
    rig.nextScene();
    CHECK(song.cursorScene() == 15 && rig.said() == "NO ROOM: 16 SCENES");
    goTo(0);
    song.moveCursorTo(15, 0);
    rig.nextScene(); // the cursor on the sixteenth scene, which is empty and falls off the end: there is no seventeenth
    CHECK(song.cursorScene() == 15 && p.scene == 1);
    rig.undo();
    CHECK(song.cursorScene() == 14); // (it was on the last row; the row it is on now was that one's neighbour)
    p.tracks[0].drums[15][0].level[0][0] = 1.f; // the sixteenth in use
    goTo(0);
    song.moveCursorTo(4, 0);
    rig.nextScene();
    CHECK(song.cursorScene() == 4 && p.scene == 0 && rig.said() == "NO ROOM: 16 SCENES");
    p.tracks[0].drums[15][0].level[0][0] = 0.f;
    goTo(0);
    song.moveCursorTo(4, 0);
    rig.app.drums().moveCursorTo(6, 9);
    rig.tap(A);
    rig.frames(1);
    rig.settle();
    rig.undo();
    CHECK(song.cursorScene() == 4 && p.tracks[0].drums[0][0].level[6][9] == 0.f);
    rig.redo();
    CHECK(song.cursorScene() == 4 && p.tracks[0].drums[0][0].level[6][9] > 0.f);

    // The list of scenes follows the cursor off its last row and off its first.
    goTo(0);
    song.moveCursorTo(0, 0);
    song.moveCursorTo(7, 0);
    CHECK(song.topScene() == 0);
    rig.nextScene();
    CHECK(song.cursorScene() == 8 && song.topScene() == 1);
    song.moveCursorTo(1, 0);
    CHECK(song.topScene() == 1);
    rig.undo();
    CHECK(song.cursorScene() == 0 && song.topScene() == 0);
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

// The song playing THROUGH its scenes, beside a song built the finished way. An engine that got
// the copy through the gesture and one handed the song with the copy in it play the same sound
// to the bit, all the way: through the scenes that moved down, round to the top, and through the
// copy in its turn. (The two are started a scene apart, so that they are at the same place when
// the key is pressed: the scene copied and its copy sound the same.) Taken back, it is the song
// as it was, played by an engine nobody touched; put back, the finished one again.
//   later = false: the key is pressed in scene 1, in the middle of a step of its first time round.
//   later = true:  scene 3 is playing, and scene 1 is the one copied: the playing moves down a scene.
void testTheSongBesideTheFinishedSong(bool later) {
    Rig plain, handed, made, undone, redone;
    for (Rig* r : {&plain, &handed, &made, &undone, &redone}) fill(*r);
    handed.project() = *withCopyAfter(plain.project(), 0);
    handed.session().applyProject();
    handed.frames(2);
    handed.render(512);
    handed.heard.clear();
    const int start = later ? 2 : 0;
    for (Rig* r : {&plain, &handed, &made, &undone, &redone}) {
        r->project().scene = r == &handed ? start + 1 : start;
        r->session().togglePlay(true);
        r->project().scene = r == &handed ? 1 : 0;
    }
    const auto all = [&](int audioFrames) {
        for (Rig* r : {&plain, &handed, &made, &undone, &redone}) {
            r->frames(1);
            r->render(audioFrames);
        }
    };
    all(5 * kStep + 777);
    CHECK(made.engine->playScene() == start && made.engine->playRepeat() == 0 && made.engine->playStep() == 5);
    const unsigned images = undone.session().imagesSent() + redone.session().imagesSent();
    for (Rig* r : {&made, &undone, &redone}) r->nextScene();
    all(256); // one block
    CHECK(made.engine->playScene() == start + 1 && made.engine->playRepeat() == 0 && made.engine->playStep() == 5);
    all(4 * kStep + 1234); // (in the middle of a step again)
    undone.undo();
    redone.undo();
    all(256);
    CHECK(undone.engine->playScene() == start && undone.engine->playStep() == 9 && made.engine->playScene() == start + 1);
    all(3 * kStep + 99);
    redone.redo();
    all(256);
    CHECK(redone.engine->playScene() == start + 1 && redone.engine->playStep() == 12);
    all((gHelpers ? 1 : 2) * (kSongSteps + 23 * 2) * kStep); // twice through the longer song (once, in the helper threads' round)
    CHECK(undone.session().imagesSent() + redone.session().imagesSent() == images); // no track sent whole
    const float madeOff = apart(made.heard, handed.heard), redoneOff = apart(redone.heard, handed.heard), undoneOff = apart(undone.heard, plain.heard);
    std::printf("  the song playing through, NEXT SCENE pressed %s: at most %g from the song built with the copy in it; taken back, %g from the song as it was; put back, %g (%.1f s)\n",
                later ? "on scene 1 while scene 3 plays" : "in scene 1", static_cast<double>(madeOff), static_cast<double>(undoneOff), static_cast<double>(redoneOff),
                static_cast<double>(made.heard.size()) / 2.0 / 48000.0);
    CHECK(madeOff == 0.f && redoneOff == 0.f && undoneOff == 0.f);
    CHECK(loudest(made.heard, 0, made.heard.size() / 2) > 0.1f && apart(plain.heard, handed.heard) > 0.01f); // (and the two songs are not alike)
}

// Where taking it back cannot leave "the song as it was, played straight through": the copy is
// playing and the scene it was made from has already had its turn this time round. The playing
// goes to that scene (there is no other place for it: the copy is gone), at the same step, bar
// and time round, so that scene is heard for longer than its plays. Nothing is cut or started
// and no cell of another scene is played; it is not any song played from the top.
void testTakenBackWhileTheCopyPlaysInItsTurn() {
    Rig rig, plain;
    fill(rig);
    fill(plain);
    rig.nextScene();
    for (Rig* r : {&rig, &plain}) {
        const int on = r->project().scene;
        r->project().scene = 0;
        r->session().togglePlay(true);
        r->project().scene = on; // (back before a frame is drawn: going to another scene is itself a step of undo)
        r->frames(1);
        r->render((2 * 23 + 30) * kStep + 500); // scene 1 twice, and the copy: in its second time round, step 8
        r->frames(1);
    }
    CHECK(rig.engine->playScene() == 1 && rig.engine->playRepeat() == 1 && rig.engine->playStep() == 7);
    CHECK(plain.engine->playScene() == 2 && apart(rig.heard, plain.heard) > 0.01f); // (the song as it was is on its scene 3 by now)
    rig.undo();
    rig.render(256);
    rig.frames(1);
    CHECK(rig.engine->playScene() == 0 && rig.engine->playRepeat() == 1 && rig.engine->playStep() == 7 && rig.engine->playBar() == 0);
    rig.render(16 * kStep); // it plays that time round out (a third, for a scene that plays twice), and goes on to scene 2
    CHECK(rig.engine->playScene() == 1 && rig.engine->playRepeat() == 0 && rig.engine->playStep() == 0);
}

// A real audio thread, making blocks back to back, and the key pressed and taken back two
// hundred times with frames, blocks or nothing in between. Whenever a command lands, the sound
// is the song left alone, to the last sample: a move is one command, whole in one block.
//   kind 0: a LATER scene is going round (its number changes every time);
//   kind 1: the scene copied is going round; kind 2: the song is playing through, and is in a later scene.
void testARealAudioThread() {
    for (int kind = 0; kind < 3; ++kind) {
        Rig plain, rig;
        fillTwelve(plain);
        fillTwelve(rig);
        for (Rig* r : {&plain, &rig}) {
            r->project().repeats[2] = 16; // (kind 2: the song stays in its last scene for as long as the keys are pressed, 208 steps)
            r->session().applyProject();
            r->frames(2);
            r->render(512);
            r->heard.clear();
            r->project().scene = kind == 1 ? 0 : 2;
            r->session().togglePlay(kind == 2);
            r->project().scene = 0;
            r->frames(1);
        }
        const unsigned images = rig.session().imagesSent();
        std::atomic<bool> stop{false};
        std::atomic<long> blocks{0};
        const int limit = kind == 2 ? 190 * kStep / 256 : 1 << 30; // blocks: (kind 2) the song does not reach its end with a copy still in it
        // Back to back, but no more than kAhead blocks past the last thing the program did: on a
        // busy machine a key takes long, and the sound to make and to compare grew without end.
        const long kAhead = 12;
        std::atomic<long> until{kAhead};
        const auto go = [&] { until.store(blocks.load() + kAhead); };
        std::thread audio([&] {
            while (!stop.load()) {
                if (blocks.load() >= limit || blocks.load() >= until.load()) {
                    std::this_thread::yield();
                    continue;
                }
                rig.render(256);
                blocks.fetch_add(1);
            }
        });
        const auto wait = [&](int some) { // nothing, or a frame and that many blocks
            if (some == 0) return;
            const long from = blocks.load();
            go();
            rig.frames(1);
            while (blocks.load() < from + some && blocks.load() < limit) std::this_thread::yield();
            rig.frames(1);
        };
        int moved = 0;
        const int pairs = kind == 0 ? 200 : (kind == 1 ? 100 : 40); // (200: the case that was once up to 1.0 out)
        for (int i = 0; i < pairs; ++i) {
            go();
            rig.nextScene();
            wait(i % 4);
            moved += rig.project().scene == 1;
            go();
            rig.undo();
            wait((i / 4) % 3);
            moved += rig.project().scene == 0;
        }
        while (rig.session().sceneMovesUnheard() > 0 && blocks.load() < limit) wait(1);
        const bool all = rig.session().sceneMovesUnheard() == 0;
        wait(3);
        stop.store(true);
        audio.join();
        CHECK(moved == 2 * pairs && all && rig.engine->dropped() == 0 && rig.session().imagesSent() == images);
        CHECK(songText(rig.project()) == songText(plain.project()));
        plain.frames(1);
        for (long i = 0; i < blocks.load(); ++i) plain.render(256);
        const float off = apart(plain.heard, rig.heard);
        std::printf("  a real audio thread, %d NEXT SCENE and undo pairs while %s: at most %g from the song left alone, over %.1f s of sound\n", pairs,
                    kind == 0 ? "a later scene goes round" : (kind == 1 ? "the scene copied goes round" : "the song plays through"), static_cast<double>(off),
                    static_cast<double>(rig.heard.size()) / 2.0 / 48000.0);
        CHECK(off == 0.f && loudest(rig.heard, 0, rig.heard.size() / 2) > 0.05f);
    }
}

// The fifteenth scene has something in it, so the copy pushes it into the sixteenth; taken back,
// it is the fifteenth again and the sixteenth is as empty as it was, in the song and in the
// engine, and no track was sent whole to make it so.
void testTheSixteenthSceneIsGivenBack() {
    Rig rig, plain;
    for (Rig* r : {&rig, &plain}) {
        fill(*r);
        Project& p = r->project();
        p.tracks[0].bars[14] = 2;
        p.tracks[0].drums[14][0].level[2][3] = 0.8f;
        p.tracks[0].drums[14][1].level[1][1] = 0.5f;
        p.tracks[1].notes[14][0].notes[0] = NoteEvent{4, 65, 2.f, 0.f, 0.7f, 0, 0, 0, 0};
        p.tracks[1].automation[14][1][2] = 0.6f;
        p.repeats[14] = 4;
        p.steps[14] = 9;
        // The sixteenth has no hit in it (there is room for the copy), but it is not as a new
        // scene is: a cell two bars long, played three times, cut to eleven steps.
        p.tracks[0].bars[15] = 2;
        p.repeats[15] = 3;
        p.steps[15] = 11;
        r->session().applyProject();
        r->frames(2);
        r->render(512);
        r->heard.clear();
    }
    Project& p = rig.project();
    CHECK(p.sceneCount() == 15);
    const std::string was = songText(p);
    const std::unique_ptr<Project> before = copyOf(p);
    const unsigned images = rig.session().imagesSent();
    const auto round = [&](Rig& r, int scene) { // that scene alone, going round, from silence
        if (r.project().playing) r.session().togglePlay(false);
        r.frames(1);
        r.render(6 * 48000);
        r.heard.clear();
        const int on = r.project().scene;
        r.project().scene = scene;
        r.session().togglePlay(false);
        r.project().scene = on;
        r.frames(1);
        r.render(2 * kBar);
        return r.heard;
    };
    // (Each time the song left alone plays the same scene the same number of times before: a
    // synth's second note is not its first to the bit.)
    rig.nextScene();
    CHECK(p.sceneCount() == 16 && sameScene(p, 15, *before, 14) && sameScene(p, 14, *before, 13));
    CHECK(apart(round(rig, 15), round(plain, 14)) == 0.f && loudest(rig.heard, 0, 2 * kBar) > 0.05f);
    CHECK(loudest(round(rig, 14), 0, 2 * kBar) == 0.f && loudest(round(plain, 15), 0, 2 * kBar) == 0.f);
    rig.undo();
    CHECK(songText(p) == was && sameScene(p, 15, *before, 15) && sameScene(p, 14, *before, 14) && p.sceneCount() == 15);
    CHECK(loudest(round(rig, 15), 0, 2 * kBar) == 0.f && loudest(round(plain, 15), 0, 2 * kBar) == 0.f);
    CHECK(apart(round(rig, 14), round(plain, 14)) == 0.f && loudest(rig.heard, 0, 2 * kBar) > 0.05f);
    rig.redo();
    CHECK(sameScene(p, 15, *before, 14) && p.sceneCount() == 16);
    CHECK(apart(round(rig, 15), round(plain, 14)) == 0.f && loudest(rig.heard, 0, 2 * kBar) > 0.05f);
    rig.undo();
    CHECK(songText(p) == was && rig.session().imagesSent() == images && rig.engine->dropped() == 0);
    CHECK(p.tracks[0].bars[15] == 2 && p.repeats[15] == 3 && p.steps[15] == 11);
}

// The queue is full (the audio thread has stood still for seconds) and scene moves are lost. When
// it runs again the program sends the whole song, and with it how long the song is: the engine
// plays all five scenes, as an engine handed the same song does.
void testMovesLostToAFullQueue() {
    Rig rig;
    fill(rig);
    Command c;
    c.type = Command::SetTempo;
    c.value = rig.project().tempo;
    while (rig.engine->send(c)) {}
    const unsigned lost = rig.engine->dropped();
    const int resyncs = rig.session().resyncs();
    rig.nextScene();
    rig.nextScene();
    rig.undo();
    rig.redo();
    CHECK(rig.project().sceneCount() == 5 && rig.engine->dropped() > lost);
    rig.render(512); // the audio thread is back: the queue empties
    rig.frames(2);   // ...and the program sees what was lost
    rig.render(512);
    CHECK(rig.session().resyncs() == resyncs + 1 && rig.engine->caughtUp());
    Rig handed;
    CHECK(handed.session().addTrack(TrackKind::Phazed));
    CHECK(handed.session().addTrack(TrackKind::Phazed));
    handed.project() = rig.project();
    handed.session().applyProject();
    handed.session().view = View::Pattern;
    handed.frames(2);
    handed.render(512);
    for (Rig* r : {&rig, &handed}) {
        r->project().scene = 0;
        r->frames(2);
        r->render(512);
        r->heard.clear();
        r->session().togglePlay(true);
        r->frames(1);
        r->render((kSongSteps + 23 * 4) * kStep + kBar); // the five scenes once through, and a bar of the next time round
    }
    CHECK(loudest(rig.heard, 0, rig.heard.size() / 2) > 0.1f);
    CHECK(apart(rig.heard, handed.heard) == 0.f);
}

// Things done since the copy that are not a step of undo. A knob turned (an effect's setting, the
// tempo) is not a pattern: the copy is taken back as cheaply as ever, though the song's text is no
// longer the text that was kept. A pattern that is not what the step kept (nothing in the program
// does that without a step of its own; here the test writes one) is put back as an ordinary undo
// puts it back: that once, the track is sent whole.
void testTakenBackAfterSomethingUndoDoesNotKnow() {
    Rig rig;
    fill(rig);
    Project& p = rig.project();
    rig.session().setEffect(1, 0, EffectKind::Filter);
    rig.settle();
    rig.nextScene();
    rig.frames(1);
    rig.render(256);
    unsigned images = rig.session().imagesSent();
    const std::string text = songText(p);
    rig.session().setEffectParam(1, 0, 0, 0.3f);
    rig.session().setEffectParam(kMasterTrack, 0, 0, 0.2f);
    p.tempo = 97.f;
    CHECK(songText(p) != text);
    rig.undo();
    CHECK(p.scene == 0 && p.sceneCount() == 3 && rig.session().imagesSent() == images);
    rig.redo();
    CHECK(p.scene == 1 && p.sceneCount() == 4 && rig.session().imagesSent() == images);

    rig.undo();
    rig.settle();
    const std::string was = songText(p);
    for (int what = 0; what < 6; ++what) { // (none of them on the cell being worked on: no frame takes it for an edit)
        rig.nextScene();
        rig.frames(1);
        rig.render(256);
        images = rig.session().imagesSent();
        if (what == 0) p.tracks[1].notes[2][0].notes[3] = NoteEvent{5, 70, 1.f, 0.f, 0.6f, 0, 0, 0, 0};
        if (what == 1) p.tracks[0].drums[3][0].level[5][5] = 0.5f;
        if (what == 2) p.tracks[1].bars[2] = 2;
        if (what == 3) p.tracks[1].automation[3][2][2] = 0.5f;
        if (what == 4) p.repeats[2] = 5;
        if (what == 5) p.steps[2] = 7;
        rig.undo();
        CHECK(p.scene == 0 && songText(p) == was);
        CHECK(rig.session().imagesSent() == images + (what < 4 ? 1 : 0)); // the one track, and no other
        rig.frames(1);
        rig.render(256);
        rig.settle();
    }
}

// ---- the engine's own call -------------------------------------------------------------------

// A drum track's patterns for the engine: scene `scene` has hits on `lane`, every `every` steps.
void hits(TrackImage& image, int scene, int lane, int every, float level = 0.8f) {
    for (int step = 0; step < kSteps; step += every) image.level[scene * kMaxBars][lane][step] = level;
}
std::unique_ptr<SceneImage> hitsScene(int lane, int every, float level = 0.8f) {
    std::unique_ptr<SceneImage> scene(new SceneImage);
    for (int step = 0; step < kSteps; step += every) scene->level[0][lane][step] = level;
    return scene;
}
struct Bare { // an engine by itself: two drum tracks
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<float> heard;
    Bare() {
        engine->useWorkers(gHelpers);
        engine->prepare(48000.f);
        engine->addDrumTrack(1);
    }
    void say(Command::Type type, int scene, int a, int b = 0) {
        Command c;
        c.type = type;
        c.scene = scene;
        c.a = a;
        c.b = b;
        engine->send(c);
    }
    void render(int frames) {
        const std::size_t was = heard.size();
        heard.resize(was + static_cast<std::size_t>(2 * frames));
        engine->render(heard.data() + was, frames);
    }
    std::vector<float> play(int from, int steps, bool loop = false) { // the song from a scene (or that scene alone, round and round), for so many steps
        say(Command::Stop, 0, 0);
        render(6 * 48000); // (the longest drum has rung out: what was played before is not heard in what comes)
        heard.clear();
        say(Command::Play, 0, from, loop ? 1 : 0);
        render(steps * kStep);
        return heard;
    }
};

// A scene handed over by moveScenes is, hit for hit and note for note, what the commands write
// one at a time, and what a whole track's image holds (in another scene): everything a hit, a
// note and a step of automation can carry, in a cell two bars long.
void testASceneHandedOverIsWhatTheCommandsWrite() {
    struct Hit { int bar, lane, step; float level, nudge, chance, start; int pitch, ratchet, reverse, cut, throwTo; };
    const Hit kHits[] = {
        {0, 0, 0, 0.8f, 0.f, 1.f, 0.f, 0, 0, 0, 0, 0},  {0, 1, 4, 0.9f, 0.25f, 1.f, 0.f, 0, 0, 0, 0, 0}, {0, 2, 6, 0.7f, 0.f, 1.f, 0.25f, 0, 0, 0, 0, 0},
        {0, 3, 8, 0.9f, 0.f, 0.f, 0.f, 0, 0, 0, 0, 0},  {0, 1, 12, 0.9f, 0.f, 1.f, 0.f, 3, 2, 0, 0, 0},  {0, 0, 10, 0.8f, 0.f, 1.f, 0.f, 0, 0, 0, 2, 1},
        {1, 4, 2, 0.6f, 0.f, 1.f, 0.f, 0, 0, 1, 0, 0},  {1, 0, 9, 0.75f, -0.25f, 1.f, 0.f, -2, 0, 0, 0, 2},
    };
    struct Key { int slot, step, pitch; float length, velocity, nudge; int ratchet, glide, throwTo; ChordPlay play; };
    const auto keyOf = [](int strum, int order, int time, int level, int lengths, int index, int count) { // a chord's key, with the chord's effects
        ChordPlay play = chordPlayOf(strum, order, time, level, lengths);
        play.index = static_cast<std::uint8_t>(index);
        play.count = static_cast<std::uint8_t>(count);
        return play;
    };
    const Key kKeys[] = {
        {0, 0, 60, 2.f, 0.8f, 0.f, 0, 0, 0, ChordPlay()},  {1, 4, 64, 1.5f, 0.7f, 0.f, 3, 0, 0, ChordPlay()}, {2, 8, 67, 2.f, 0.6f, 0.25f, 0, kGlideHalf, 0, ChordPlay()},
        {5, 12, 55, 1.f, 0.5f, 0.f, 0, 0, 2, ChordPlay()},
        // a glide of each of the other lengths, one of them on a ratchet
        {6, 2, 70, 1.f, 0.6f, 0.f, 0, kGlideQuarter, 0, ChordPlay()}, {7, 3, 62, 1.f, 0.6f, 0.f, 2, kGlideOne, 0, ChordPlay()}, {8, 6, 71, 1.f, 0.6f, 0.f, 0, kGlideTwo, 0, ChordPlay()},
        {9, 7, 58, 1.f, 0.6f, 0.f, 0, kGlideFull, 0, ChordPlay()},
        // a chord played as an arpeggio, everything about it humanised
        {10, 9, 52, 3.f, 0.6f, 0.f, 0, 0, 0, keyOf(kStrumQuarter, kOrderRandom, 50, 50, 50, 0, 3)}, {11, 9, 56, 3.f, 0.6f, 0.f, 0, 0, 0, keyOf(kStrumQuarter, kOrderRandom, 50, 50, 50, 1, 3)},
        {12, 9, 59, 3.f, 0.6f, 0.f, 0, 0, 0, keyOf(kStrumQuarter, kOrderRandom, 50, 50, 50, 2, 3)},
        // ...and one strummed downwards, early or late by HUMAN TIME, a ratchet, thrown
        {13, 13, 50, 2.5f, 0.6f, 0.f, 2, 0, 1, keyOf(kStrumWide, kOrderDown, 100, 0, 0, 0, 3)}, {14, 13, 53, 2.5f, 0.6f, 0.f, 2, 0, 1, keyOf(kStrumWide, kOrderDown, 100, 0, 0, 1, 3)},
        {15, 13, 57, 2.5f, 0.6f, 0.f, 2, 0, 1, keyOf(kStrumWide, kOrderDown, 100, 0, 0, 2, 3)},
    };
    const auto macro = [](int step) { return 0.05f + 0.06f * static_cast<float>(step); }; // the synth's first macro, moving all bar
    const float kSend = 0.9f;                                                               // the drums' ECHO send, on step 3

    const auto bare = [] {
        std::unique_ptr<Bare> b(new Bare);
        b->engine->addSynthTrack(2); // (track 2 has drums and is given nothing)
        b->engine->setEffect(kEchoBus, 0, EffectKind::Delay);
        b->engine->setEffect(kSpaceBus, 0, EffectKind::Reverb);
        b->render(4800);
        return b;
    };
    // Into a scene's bars of an image: a SceneImage's own arrays, or a TrackImage's from that scene's first slot.
    const auto into = [&](float (*level)[kDrumLanes][kSteps], float (*nudge)[kDrumLanes][kSteps], float (*chance)[kDrumLanes][kSteps], float (*start)[kDrumLanes][kSteps],
                          StepFx (*fx)[kDrumLanes][kSteps], TrackImage::Note (*notes)[kMaxNotes], bool drums) {
        for (const Hit& h : kHits) {
            if (!drums) break;
            level[h.bar][h.lane][h.step] = h.level;
            nudge[h.bar][h.lane][h.step] = h.nudge;
            chance[h.bar][h.lane][h.step] = h.chance;
            start[h.bar][h.lane][h.step] = h.start;
            StepFx& f = fx[h.bar][h.lane][h.step];
            f.pitch = static_cast<std::int8_t>(h.pitch);
            f.ratchet = static_cast<std::uint8_t>(h.ratchet);
            f.reverse = h.reverse;
            f.cut = static_cast<std::uint8_t>(h.cut);
            f.throwTo = static_cast<std::uint8_t>(h.throwTo);
        }
        for (const Key& k : kKeys) {
            if (drums) break;
            TrackImage::Note& n = notes[0][k.slot];
            n.step = static_cast<std::uint8_t>(k.step);
            n.pitch = static_cast<std::uint8_t>(k.pitch);
            n.length = k.length;
            n.velocity = k.velocity;
            n.nudge = k.nudge;
            n.ratchet = static_cast<std::uint8_t>(k.ratchet);
            n.glide = static_cast<std::uint8_t>(k.glide);
            n.throwTo = static_cast<std::uint8_t>(k.throwTo);
            n.play = k.play;
        }
    };

    // 1. The commands, into scene 1.
    std::unique_ptr<Bare> said = bare();
    {
        Command c;
        c.track = 0;
        c.type = Command::SetCellBars;
        c.a = 2;
        said->engine->send(c);
        for (const Hit& h : kHits) {
            c = Command();
            c.bar = h.bar;
            c.a = h.lane;
            c.b = h.step;
            c.type = Command::SetStep;
            c.value = h.level;
            said->engine->send(c);
            c.type = Command::SetStepNudge;
            c.value = h.nudge;
            said->engine->send(c);
            c.type = Command::SetStepChance;
            c.value = h.chance;
            said->engine->send(c);
            c.type = Command::SetStepStart;
            c.value = h.start;
            said->engine->send(c);
            c.type = Command::SetStepFx;
            c.c = h.pitch;
            c.d = h.ratchet;
            c.value = h.reverse ? 1.f : 0.f;
            c.extra = static_cast<float>(h.cut + 4 * h.throwTo);
            said->engine->send(c);
        }
        for (const Key& k : kKeys) {
            c = Command();
            c.type = Command::SetNote;
            c.track = 2;
            c.a = k.slot;
            c.b = k.step;
            c.c = k.pitch;
            c.d = static_cast<int>(k.length);
            c.extra = k.length - static_cast<float>(c.d);
            c.value = k.velocity;
            c.shift = k.nudge;
            c.e = k.ratchet + (k.glide ? 256 : 0) + (k.throwTo << 9) + (k.glide >= 2 ? k.glide << 11 : 0);
            c.play = k.play;
            said->engine->send(c);
        }
        c = Command();
        c.type = Command::SetAutomation;
        c.a = autoLaneOfSend(0);
        c.b = 3;
        c.value = kSend;
        said->engine->send(c);
        c.track = 2;
        c.a = 0;
        for (int step = 0; step < kSteps; ++step) {
            c.b = step;
            c.value = macro(step);
            said->engine->send(c);
        }
    }
    // 2. A scene move: the scene comes in at the top, for tracks 1 and 3 (track 2, between them, is handed none).
    std::unique_ptr<Bare> moved = bare();
    {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0].reset(new SceneImage);
        scenes[2].reset(new SceneImage);
        scenes[0]->bars = 2;
        into(scenes[0]->level, scenes[0]->nudge, scenes[0]->chance, scenes[0]->start, scenes[0]->fx, scenes[0]->notes, true);
        into(scenes[2]->level, scenes[2]->nudge, scenes[2]->chance, scenes[2]->start, scenes[2]->fx, scenes[2]->notes, false);
        scenes[0]->automation[autoLaneOfSend(0) * kAutoSteps + 3] = kSend;
        for (int step = 0; step < kSteps; ++step) scenes[2]->automation[step] = macro(step);
        CHECK(moved->engine->moveScenes(0, 1, scenes, 1, 1, 0));
    }
    // 3. Each track's image, with all of it in scene 4.
    const auto imageOf = [&](bool drums) {
        const int at = 3, slot = at * kMaxBars;
        std::unique_ptr<TrackImage> image(new TrackImage);
        if (drums) image->bars[at] = 2;
        into(image->level + slot, image->nudge + slot, image->chance + slot, image->start + slot, image->fx + slot, image->notes + slot, drums);
        image->automation.reset(new float[kMaxScenes * kAutoLanes * kAutoSteps]);
        for (int i = 0; i < kMaxScenes * kAutoLanes * kAutoSteps; ++i) image->automation[i] = kAutoNone;
        if (drums) image->automation[(at * kAutoLanes + autoLaneOfSend(0)) * kAutoSteps + 3] = kSend;
        for (int step = 0; step < kSteps && !drums; ++step) image->automation[at * kAutoLanes * kAutoSteps + step] = macro(step);
        return image;
    };
    std::unique_ptr<Bare> imaged = bare();
    imaged->engine->setTrackImage(0, imageOf(true));
    imaged->engine->setTrackImage(2, imageOf(false));
    const int kLong = 3 * 2 * kSteps; // the two bars three times round
    const std::vector<float> a = said->play(0, kLong, true), b = moved->play(0, kLong, true), c = imaged->play(3, kLong, true);
    CHECK(apart(a, b) == 0.f && apart(a, c) == 0.f && loudest(a, 0, a.size() / 2) > 0.1f);
    CHECK(loudest(a, 16 * kStep, 32 * kStep) > 0.1f); // (the second bar is played)
    for (int beside : {2, 4}) { // and nothing either side of it (asked of an engine that has made no sound: the buses ring for long)
        std::unique_ptr<Bare> fresh = bare();
        fresh->engine->setTrackImage(0, imageOf(true));
        fresh->engine->setTrackImage(2, imageOf(false));
        CHECK(loudest(fresh->play(beside, kLong, true), 0, kLong * kStep) == 0.f);
    }
    // The chord's effects and the glides' lengths are heard in it: the same scene without them is another sound.
    for (int without = 0; without < 2; ++without) { // 0: no chord effect; 1: every glide half a step
        std::unique_ptr<Bare> other = bare();
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0].reset(new SceneImage);
        scenes[2].reset(new SceneImage);
        scenes[0]->bars = 2;
        into(scenes[0]->level, scenes[0]->nudge, scenes[0]->chance, scenes[0]->start, scenes[0]->fx, scenes[0]->notes, true);
        into(scenes[2]->level, scenes[2]->nudge, scenes[2]->chance, scenes[2]->start, scenes[2]->fx, scenes[2]->notes, false);
        scenes[0]->automation[autoLaneOfSend(0) * kAutoSteps + 3] = kSend;
        for (int step = 0; step < kSteps; ++step) scenes[2]->automation[step] = macro(step);
        for (TrackImage::Note& n : scenes[2]->notes[0]) {
            if (without == 0) n.play = ChordPlay();
            else if (n.glide) n.glide = kGlideHalf;
        }
        CHECK(other->engine->moveScenes(0, 1, scenes, 1, 1, 0));
        CHECK(apart(other->play(0, kLong, true), a) > 0.01f);
    }
    // A glide's number nobody knows is half a step, handed over as from a song file.
    std::vector<float> glides[3];
    for (int which = 0; which < 3; ++which) {
        std::unique_ptr<Bare> one = bare();
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[2].reset(new SceneImage);
        TrackImage::Note* n = scenes[2]->notes[0];
        n[0].step = 0, n[0].pitch = 48, n[0].length = 1.f, n[0].velocity = 0.8f;
        n[1].step = 4, n[1].pitch = 72, n[1].length = 3.f, n[1].velocity = 0.8f;
        n[1].glide = static_cast<std::uint8_t>(which == 0 ? 9 : (which == 1 ? kGlideHalf : kGlideTwo));
        CHECK(one->engine->moveScenes(0, 1, scenes, 1, 1, 0));
        glides[which] = one->play(0, 16, true);
    }
    CHECK(apart(glides[0], glides[1]) == 0.f && apart(glides[0], glides[2]) > 0.01f && loudest(glides[0], 4 * kStep, 6 * kStep) > 0.05f);
}

// ---- chords and their effects when scenes move ----------------------------------------------------

// A synth on track 3 of an engine by itself, and a chord of three keys written by commands: an
// arpeggio, a note every half step for twelve steps (velocity 0: the chord is taken away).
void arpChord(Bare& bare, int scene, float velocity) {
    for (int k = 0; k < 3; ++k) {
        Command c;
        c.type = Command::SetNote;
        c.track = 2;
        c.scene = scene;
        c.a = k;
        c.b = 0;
        c.c = 55 + 4 * k;
        c.d = 12;
        c.value = velocity;
        c.play = chordPlayOf(kStrumHalf, kOrderUp, 0, 0, 0);
        c.play.index = static_cast<std::uint8_t>(k);
        c.play.count = 3;
        bare.engine->send(c);
    }
}

// An arpeggio that is playing knows which bar of the song its chord is written in: when that
// bar is written to, it looks whether the chord is still there. A scene move takes the bar to
// another number, and the arpeggio goes with it, as the playing does.
//   - the chord taken away in the scene that is playing (its new number): the arpeggio ends,
//     as in a song where no scene moved;
//   - something written in the scene that has its old number now: the arpeggio does not look there;
//   - a scene that does not move (it is before the one put in): the arpeggio stays;
//   - a scene with no place to go to (the sixteenth, when one is put in; the one taken out): the
//     playing stays on that number, where another scene is now, and the arpeggio ends if its
//     chord is not in it.
void testAnArpeggioGoesWithItsScene() {
    struct Case { int scene, from, by, now, gone; }; // the scene the chord is in, the move, where that scene is then (gone: it has no place)
    const Case kCases[] = {{2, 0, 1, 3, 0}, {2, 2, 1, 3, 0}, {2, 3, 1, 2, 0}, {3, 1, -1, 2, 0}, {3, 3, -1, 2, 0}, {1, 3, -1, 1, 0}, {15, 0, 1, 15, 1}, {1, 2, -1, 1, 1}};
    for (const Case& k : kCases) {
        for (int then = 0; then < 3; ++then) { // 0: nothing more; 1: the chord is taken away where it is; 2: a note is written where it was
            if (k.gone && then != 0) continue;
            if (then == 2 && k.now == k.scene) continue;
            Bare moved, plain;
            for (Bare* b : {&moved, &plain}) {
                b->engine->addSynthTrack(2);
                arpChord(*b, k.scene, 0.7f);
                b->say(Command::SetSceneCount, 0, kMaxScenes);
                b->render(256);
                b->say(Command::Play, 0, k.scene, 1); // that scene, round and round
                b->render(3 * kStep + 1234);          // its seventh note is sounding
            }
            std::unique_ptr<SceneImage> scenes[kNumTracks]; // (none handed over: the scene that comes in has nothing in it)
            CHECK(moved.engine->moveScenes(k.from, k.by, scenes, kMaxScenes, 1, 0));
            if (k.gone) arpChord(plain, k.scene, 0.f); // the scene there now has no chord: as if it had been taken away
            for (Bare* b : {&moved, &plain}) b->render(700);
            CHECK(moved.engine->playScene() == k.now);
            if (then == 1) {
                arpChord(moved, k.now, 0.f);
                arpChord(plain, k.scene, 0.f);
            }
            if (then == 2) { // (a note far along the bar of the scene that is not playing: nothing to hear of it)
                Command c;
                c.type = Command::SetNote;
                c.track = 2;
                c.scene = k.scene;
                c.a = 9;
                c.b = 15;
                c.c = 80;
                c.d = 1;
                c.value = 0.5f;
                moved.engine->send(c);
            }
            for (Bare* b : {&moved, &plain}) b->render(10 * kStep);
            const float off = apart(moved.heard, plain.heard);
            if (off != 0.f) std::printf("  an arpeggio in scene %d, the scenes from %d moved by %d, then %d: %g apart\n", k.scene + 1, k.from + 1, k.by, then, static_cast<double>(off));
            CHECK(off == 0.f);
            // The arpeggio is heard for its twelve steps, or ends where its chord went.
            const bool ends = k.gone || then == 1;
            const std::size_t at = static_cast<std::size_t>(8 * kStep), to = static_cast<std::size_t>(11 * kStep);
            CHECK(ends ? loudest(moved.heard, at, to) < 0.001f : loudest(moved.heard, at, to) > 0.02f);
        }
    }
}

// The song played ONCE through (Command::Play with c = 1, a bounce): its last step asks for
// nothing early that belongs to its first. Which step is the last is the song's length, and a
// scene move changes that in the same command. A song a scene was put into, before Play or
// while it plays once through, is to the bit the finished song played once through.
void testOnceThroughAfterAMove() {
    // Each scene's first hit is early: it is asked for on the last step of the scene before.
    const auto early = [](float (*level)[kDrumLanes][kSteps], float (*nudge)[kDrumLanes][kSteps], int lane) {
        for (int step = 0; step < kSteps; step += 4) level[0][lane][step] = 0.8f;
        nudge[0][lane][0] = -0.3f;
    };
    const auto song = [&](Bare& bare, bool whole) { // whole: three scenes, the new one at the top; else the two it is put in front of
        std::unique_ptr<TrackImage> image(new TrackImage);
        const int first = whole ? 1 : 0;
        if (whole) early(image->level, image->nudge, 5);
        early(image->level + first * kMaxBars, image->nudge + first * kMaxBars, 0);
        early(image->level + (first + 1) * kMaxBars, image->nudge + (first + 1) * kMaxBars, 2);
        bare.engine->setTrackImage(0, std::move(image));
        bare.say(Command::SetSceneCount, 0, whole ? 3 : 2);
        bare.render(256);
    };
    const auto put = [&](Bare& bare) {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0].reset(new SceneImage);
        early(scenes[0]->level, scenes[0]->nudge, 5);
        CHECK(bare.engine->moveScenes(0, 1, scenes, 3, 1, 0));
    };
    const auto play = [](Bare& bare, int from, int once) {
        Command c;
        c.type = Command::Play;
        c.a = from;
        c.c = once;
        bare.engine->send(c);
    };
    const int kThrough = 3 * 16 * kStep + 4 * kStep; // the three scenes, and on into the top again
    // Put in before Play.
    Bare told, moved, again;
    song(told, true);
    song(moved, false);
    song(again, true);
    put(moved);
    moved.render(256);
    told.render(256);
    again.render(256);
    for (Bare* b : {&told, &moved}) {
        b->heard.clear();
        play(*b, 0, 1);
        b->render(kThrough);
    }
    again.heard.clear();
    play(again, 0, 0);
    again.render(kThrough);
    CHECK(apart(moved.heard, told.heard) == 0.f && loudest(told.heard, 0, told.heard.size() / 2) > 0.1f);
    // (Once through is another sound than round and round: the top's early hit is not heard at the end.)
    CHECK(apart(again.heard, told.heard) > 0.01f);
    const std::size_t end = static_cast<std::size_t>(3 * 16 * kStep), lead = static_cast<std::size_t>(0.3 * kStep);
    CHECK(std::equal(told.heard.begin(), told.heard.begin() + static_cast<std::ptrdiff_t>(2 * (end - lead)), again.heard.begin())); // the same until the last step's early hit
    // Put in while it plays once through: the two are started a scene apart, and are at the same place when it goes in.
    Bare finished, during;
    song(finished, true);
    song(during, false);
    play(finished, 1, 1);
    play(during, 0, 1);
    for (Bare* b : {&finished, &during}) b->render(5 * kStep + 321);
    put(during);
    for (Bare* b : {&finished, &during}) b->render(kThrough);
    CHECK(during.engine->playScene() == finished.engine->playScene() && during.engine->playStep() == finished.engine->playStep());
    CHECK(apart(during.heard, finished.heard) == 0.f && loudest(during.heard, 0, during.heard.size() / 2) > 0.1f);
}

// The copy NEXT SCENE hands the engine is, note for note, what the piano roll writes there a
// bar at a time: a chord's effects, a glide's length and THROW are in it. And an arpeggio that
// is playing when the key is pressed goes with its scene: its chord taken off the roll then,
// it ends as it does in a song where no key was pressed.
void testTheCopyIsWhatTheRollWrites() {
    Rig copied, written;
    fill(copied);
    fill(written);
    copied.nextScene(); // the engine's scene 1 is the one handed over now
    for (int bar = 0; bar < 2; ++bar) written.session().sendNoteBar(2, 0, bar); // ...and here every note of it is written again, by commands
    for (Rig* r : {&copied, &written}) {
        r->project().scene = 0;
        r->frames(2);
        r->render(512);
        r->heard.clear();
        r->session().togglePlay(false);
        r->frames(1);
        r->render(3 * 23 * kStep);
    }
    CHECK(copied.engine->playScene() == 0 && apart(copied.heard, written.heard) == 0.f && loudest(copied.heard, 0, copied.heard.size() / 2) > 0.1f);

    Rig plain, rig;
    fill(plain);
    fill(rig);
    for (Rig* r : {&plain, &rig}) {
        r->project().track = 2;
        r->session().togglePlay(false);
        r->frames(1);
        r->render(4 * kStep + 3000); // the arpeggio on step 4 is on its third note
        r->frames(1);
    }
    rig.nextScene();
    for (Rig* r : {&plain, &rig}) r->render(256), r->frames(1);
    CHECK(rig.engine->playScene() == 1);
    plain.project().tracks[2].notes[0][0].chords[1].velocity = 0.f;
    plain.session().sendNoteBar(2, 0, 0);
    rig.project().tracks[2].notes[1][0].chords[1].velocity = 0.f; // (the scene that is playing: scene 2 now)
    rig.session().sendNoteBar(2, 1, 0);
    for (Rig* r : {&plain, &rig}) r->render(2 * kBar), r->frames(1);
    CHECK(apart(plain.heard, rig.heard) == 0.f && rig.engine->dropped() == 0);
    // (It did end: left on the roll, it is heard.)
    Rig left;
    fill(left);
    left.project().track = 2;
    left.session().togglePlay(false);
    left.frames(1);
    left.render(4 * kStep + 3000);
    left.frames(1);
    left.render(256);
    left.frames(1);
    left.render(2 * kBar);
    CHECK(apart(left.heard, plain.heard) > 0.01f);
}

// Engine::moveScenes by itself, beside an engine told the finished song the long way.
void testTheEnginesMove() {
    // The song: three scenes on track 1 (each its own lane), scene 1 alone on track 2.
    const auto song = [](Bare& bare) {
        std::unique_ptr<TrackImage> one(new TrackImage), two(new TrackImage);
        hits(*one, 0, 0, 4);
        hits(*one, 1, 2, 2);
        hits(*one, 2, 1, 8);
        hits(*one, 14, 7, 4); // (past the song's end: the last scene but one, which becomes the last)
        one->bars[1] = 2;
        one->level[1 * kMaxBars + 1][3][5] = 0.9f;
        hits(*two, 0, 4, 3);
        bare.engine->setTrackImage(0, std::move(one));
        bare.engine->setTrackImage(1, std::move(two));
        bare.say(Command::SetSceneCount, 0, 3);
        bare.say(Command::SetSceneRepeats, 0, 2);
        bare.say(Command::SetSceneRepeats, 2, 3);
        bare.say(Command::SetSceneSteps, 1, 21);
        bare.render(256);
    };
    const int kWhole = (2 * 16 + 21 + 3 * 16) * 2; // more than once through, with a scene more or less

    // A scene put in at the top. Track 1 is handed the new scene; track 2 is handed none, and gets
    // the sixteenth scene, which went (it is empty), so its scene 1 moves down like the rest.
    Bare moved, told, plain;
    for (Bare* b : {&moved, &told, &plain}) song(*b);
    {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0] = hitsScene(5, 1, 0.7f);
        scenes[0]->bars = 99; // (as long as a cell can be, and no longer)
        CHECK(moved.engine->moveScenes(0, 1, scenes, 4, 3, 7));
        moved.render(256);
        unsigned heard = 0;
        moved.engine->playScene(&heard);
        CHECK(heard == 1);

        std::unique_ptr<TrackImage> one(new TrackImage), two(new TrackImage);
        hits(*one, 0, 5, 1, 0.7f);
        one->bars[0] = kMaxBars;
        hits(*one, 1, 0, 4);
        hits(*one, 2, 2, 2);
        hits(*one, 3, 1, 8);
        hits(*one, 15, 7, 4);
        one->bars[2] = 2;
        one->level[2 * kMaxBars + 1][3][5] = 0.9f;
        hits(*two, 1, 4, 3);
        told.engine->setTrackImage(0, std::move(one));
        told.engine->setTrackImage(1, std::move(two));
        told.say(Command::SetSceneCount, 0, 4);
        const int repeats[] = {3, 2, 1, 3}, steps[] = {7, 0, 21, 0};
        for (int scene = 0; scene < 4; ++scene) {
            told.say(Command::SetSceneRepeats, scene, repeats[scene]);
            told.say(Command::SetSceneSteps, scene, steps[scene]);
        }
        told.render(256);
    }
    const std::vector<float> was = plain.play(0, kWhole);
    CHECK(apart(moved.play(0, kWhole), told.play(0, kWhole)) == 0.f && loudest(moved.heard, 0, moved.heard.size() / 2) > 0.1f);
    CHECK(apart(moved.heard, was) > 0.01f);
    const std::vector<float> wasLast = plain.play(14, 20, true);
    CHECK(apart(moved.play(15, 20, true), wasLast) == 0.f && loudest(wasLast, 0, 20 * kStep) > 0.1f); // the last scene but one is the last now
    CHECK(loudest(moved.play(14, 20, true), 0, 20 * kStep) == 0.f);

    // ...and taken out again: the song as it was, but for the sixteenth scene, which is the one handed over.
    {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0] = hitsScene(6, 2);
        scenes[1] = hitsScene(7, 4);
        CHECK(moved.engine->moveScenes(1, -1, scenes, 3, 5, 9));
        moved.render(256);
        CHECK(apart(moved.play(0, kWhole), was) == 0.f);
        CHECK(apart(moved.play(14, 20, true), wasLast) == 0.f);
        moved.say(Command::SetSceneCount, 0, kMaxScenes);
        std::unique_ptr<TrackImage> one(new TrackImage), two(new TrackImage);
        hits(*one, 15, 6, 2);
        hits(*two, 15, 7, 4);
        Bare last;
        last.engine->setTrackImage(0, std::move(one));
        last.engine->setTrackImage(1, std::move(two));
        last.say(Command::SetSceneCount, 0, kMaxScenes);
        last.say(Command::SetSceneRepeats, 15, 5);
        last.say(Command::SetSceneSteps, 15, 9);
        last.render(256);
        CHECK(apart(moved.play(15, 5 * 9), last.play(15, 5 * 9)) == 0.f && loudest(last.heard, 0, last.heard.size() / 2) > 0.1f); // (five times its nine steps, and not a step of the top)
        CHECK(loudest(moved.heard, 2 * 9 * kStep, 5 * 9 * kStep) > 0.1f);
        moved.say(Command::SetSceneCount, 0, 3);
        moved.render(256);
    }

    // What is not a move changes no pattern (it is counted as heard all the same, and its scenes freed).
    const int from[] = {-1, kMaxScenes, 0, kMaxScenes, 0, 0, 3};
    const int by[] = {1, 1, -1, -1, 2, 0, -3};
    for (int i = 0; i < 7; ++i) {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0] = hitsScene(5, 1);
        scenes[1] = hitsScene(5, 1);
        CHECK(moved.engine->moveScenes(from[i], by[i], scenes, 9, 9, 9));
        moved.render(256);
    }
    unsigned heard = 0;
    moved.engine->playScene(&heard);
    CHECK(heard == 9 && apart(moved.play(0, kWhole), was) == 0.f);

    // Numbers that cannot be are held to what can: 99 scenes are 16, 99 plays 16, fewer than one, one.
    Bare wild, mild;
    for (Bare* b : {&wild, &mild}) song(*b);
    {
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0] = hitsScene(5, 1);
        CHECK(wild.engine->moveScenes(2, 1, scenes, 99, 99, -5));
        scenes[0] = hitsScene(5, 1);
        CHECK(mild.engine->moveScenes(2, 1, scenes, kMaxScenes, kMaxRepeats, 0));
        for (Bare* b : {&wild, &mild}) b->render(256);
        CHECK(apart(wild.play(2, 20 * 16), mild.play(2, 20 * 16)) == 0.f);
        CHECK(wild.engine->playScene() == 4 && wild.engine->playRepeat() == 0); // sixteen plays of the new scene, three of the next, and into the empty ones
        CHECK(apart(wild.play(15, 20), mild.play(15, 20)) == 0.f && wild.engine->playScene() == 0); // the sixteenth is in the song: one bar of it, then the top
        // ...and a cell 99 bars long is eight: the scene goes round after its eighth bar (only its first has hits).
        scenes[0] = hitsScene(5, 1);
        scenes[0]->bars = 99;
        CHECK(wild.engine->moveScenes(0, 1, scenes, -4, 0, 0));
        scenes[0] = hitsScene(5, 1);
        scenes[0]->bars = kMaxBars;
        CHECK(mild.engine->moveScenes(0, 1, scenes, 1, 1, 0));
        for (Bare* b : {&wild, &mild}) b->render(256);
        const int kTwice = 9 * 16 + 8;
        CHECK(apart(wild.play(0, kTwice), mild.play(0, kTwice)) == 0.f && wild.engine->playScene() == 0 && loudest(wild.heard, 8 * 16 * kStep, kTwice * kStep) > 0.1f);
        CHECK(wild.engine->playBar() == 1 && wild.engine->playRepeat() == 0);
    }

    // The queue full: it says so, and nothing is lost but the move (no leak: the sanitizer's to see).
    Bare full;
    Command c;
    c.type = Command::SetTempo;
    c.value = 120.f;
    while (full.engine->send(c)) {}
    std::unique_ptr<SceneImage> scenes[kNumTracks];
    scenes[0] = hitsScene(5, 1);
    CHECK(!full.engine->moveScenes(0, 1, scenes, 4, 1, 0));
}

// `nextscene_test --measure`: what the block after the gesture costs the audio thread (not a check:
// it is a time). Blocks of 256, what the program asks for: 5.33 ms of sound each. The block
// that takes the move is timed against the same block with nothing in it (the median of each:
// one late block on a busy machine is not the cost).
// The audio thread makes nothing and frees nothing when scenes move, and nothing is lost: the
// command's own piece and the scene the song has no more use for both come back through the
// trash, and the program frees them (collectGarbage). Counted on every new and delete: after any
// number of moves in and out, the program holds what it held, to the byte.
void testNothingFreedOnTheAudioThreadAndAllOfItComesBack() {
    { // (the counters can see)
        const long long held = gHeldBytes;
        gInRender = true;
        int* const one = new int(1);
        gInRender = false;
        CHECK(gHeldBytes == held + static_cast<long long>(sizeof(int)) && gNewsInRender == 1 && gDeletesInRender == 0);
        gInRender = true;
        delete one;
        gInRender = false;
        CHECK(gHeldBytes == held && gDeletesInRender == 1);
        gNewsInRender = 0;
        gDeletesInRender = 0;
    }
    const int helpersWere = gHelpers;
    for (int helpers : {0, 3}) {
        gHelpers = helpers;
        Bare bare;
        {
            std::unique_ptr<TrackImage> one(new TrackImage), two(new TrackImage);
            hits(*one, 0, 0, 4);
            hits(*one, 1, 2, 2);
            hits(*one, 2, 1, 8);
            hits(*two, 0, 4, 3);
            bare.engine->setTrackImage(0, std::move(one));
            bare.engine->setTrackImage(1, std::move(two));
        }
        bare.say(Command::SetSceneCount, 0, 3);
        std::vector<float> out(2 * 256);
        const auto render = [&] {
            gInRender = true;
            bare.engine->render(out.data(), 256);
            gInRender = false;
        };
        // One scene in after scene 1, and out again: track 1 is handed a scene each way, track 2
        // none (it is given the one that went). Then a move that is no move, its scene freed.
        const auto round = [&] {
            const long long before = gHeldBytes;
            {
                std::unique_ptr<SceneImage> scenes[kNumTracks];
                scenes[0] = hitsScene(5, 1, 0.7f);
                CHECK(bare.engine->moveScenes(1, 1, scenes, 4, 2, 0));
            }
            CHECK(gHeldBytes > before); // (what was handed over is on its way)
            render();
            bare.engine->collectGarbage();
            {
                std::unique_ptr<SceneImage> scenes[kNumTracks];
                scenes[0] = hitsScene(6, 2);
                CHECK(bare.engine->moveScenes(2, -1, scenes, 3, 1, 0));
            }
            render();
            bare.engine->collectGarbage();
            {
                std::unique_ptr<SceneImage> scenes[kNumTracks];
                scenes[0] = hitsScene(5, 1);
                scenes[1] = hitsScene(5, 1);
                CHECK(bare.engine->moveScenes(kMaxScenes, 1, scenes, 9, 9, 9));
            }
            render();
            bare.engine->collectGarbage();
        };
        for (int i = 0; i < 3; ++i) round();
        for (int playing = 0; playing < 3; ++playing) { // stopped, one scene going round, the song through
            if (playing) bare.say(Command::Play, 0, 0, playing == 1 ? 1 : 0);
            render();
            bare.engine->collectGarbage();
            const long long held = gHeldBytes;
            for (int i = 0; i < 20; ++i) {
                round();
                for (int block = 0; block < 5; ++block) render();
            }
            bare.engine->collectGarbage();
            CHECK(gHeldBytes == held); // nothing lost on the way, nothing kept
            unsigned heard = 0;
            bare.engine->playScene(&heard);
            CHECK(heard == static_cast<unsigned>(3 * (3 + 20 * (playing + 1))));
        }
        CHECK(gNewsInRender == 0 && gDeletesInRender == 0); // the audio thread, and the helpers beside it
        CHECK(bare.engine->dropped() == 0);
    }
    gHelpers = helpersWere;
}

void measure() {
    const double ms = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    const auto middle = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    for (int song = 0; song < 3; ++song) { // one scene going round; a later one going round; the song playing through
        Rig rig;
        fillTwelve(rig);
        rig.project().scene = song == 1 ? 2 : 0;
        rig.session().togglePlay(song == 2);
        rig.project().scene = 0;
        rig.frames(1);
        std::vector<float> out(512);
        const auto block = [&] {
            const Uint64 from = SDL_GetPerformanceCounter();
            rig.engine->render(out.data(), 256);
            return ms * static_cast<double>(SDL_GetPerformanceCounter() - from);
        };
        std::vector<double> usual, cold, after[3], key[3];
        double worst[3] = {};
        for (int i = 0; i < 400; ++i) usual.push_back(block());
        for (int round = 0; round < 60; ++round) {
            for (int what = 0; what < 3; ++what) {
                const Uint64 pressed = SDL_GetPerformanceCounter();
                if (what == 0) rig.nextScene();
                else if (what == 1) rig.undo();
                else rig.redo();
                key[what].push_back(ms * static_cast<double>(SDL_GetPerformanceCounter() - pressed)); // (the program's own thread: a frame is 16 ms)
                const double t = block(); // the block that takes it
                after[what].push_back(t);
                worst[what] = t > worst[what] ? t : worst[what];
                for (int frame = 0; frame < 3; ++frame) {
                    rig.frames(1);
                    for (int i = 0; i < 3; ++i) usual.push_back(block());
                }
            }
            rig.undo(); // (back to where the round began)
            block();
            rig.frames(1);
            // A block made straight after the program has been busy with something else and sent
            // the engine nothing (here: the song written out as text, as a step of undo is): what
            // such a block costs with the processor's memory of the engine's work gone cold.
            const std::string text = songText(rig.project()) + songText(rig.project());
            cold.push_back(block() + (text.empty() ? 1.0 : 0.0));
            for (int i = 0; i < 9; ++i) usual.push_back(block());
        }
        std::printf("  12 tracks, %s: a usual block %.3f ms, one straight after the program was busy and sent nothing %.3f; the block that takes NEXT SCENE %.3f (worst %.3f), its undo %.3f (worst %.3f), its redo %.3f (worst %.3f)\n",
                    song == 0 ? "the scene going round" : (song == 1 ? "a later scene going round" : "the song playing through"), middle(usual), middle(cold), middle(after[0]), worst[0],
                    middle(after[1]), worst[1], middle(after[2]), worst[2]);
        std::printf("    ...and on the program's own thread: the key %.1f ms, its undo %.1f, its redo %.1f\n", middle(key[0]), middle(key[1]), middle(key[2]));
    }
    // The command by itself, with nothing else in the block: twelve tracks' scenes moved.
    Engine engine;
    engine.prepare(48000.f);
    std::vector<float> out(512);
    std::vector<double> with, without;
    for (int i = 0; i < 400; ++i) {
        for (int move = 0; move < 2; ++move) {
            std::unique_ptr<SceneImage> scenes[kNumTracks];
            for (auto& scene : scenes) scene.reset(new SceneImage);
            if (move) engine.moveScenes(i % 2 ? 1 : 0, i % 2 ? -1 : 1, scenes, 3, 1, 0);
            const Uint64 from = SDL_GetPerformanceCounter();
            engine.render(out.data(), 256);
            (move ? with : without).push_back(1000.0 * ms * static_cast<double>(SDL_GetPerformanceCounter() - from));
        }
        engine.collectGarbage();
    }
    std::printf("  an empty engine's block: %.1f millionths of a second; with a scene move for twelve tracks in it: %.1f\n", middle(without), middle(with));
}

} // namespace

// A fault that is known and not mended (on `main` too; found in review, 2026-11-13). Not part of
// ctest: `nextscene_test --edge` prints it. The song plays through with a copy of scene 2 after
// it, and the copy is taken back while the LAST STEP of scene 2 plays. That step has already asked
// for the early notes and humanised chords of the bar that follows, which was the copy; with the
// copy gone the bar that follows is scene 3, whose first step then plays wrong.
void edge() {
    for (const double at : {60.5, 61.05, 61.5, 61.99}) { // steps since Play; scene 2 ends at 62
        Rig rig, plain;
        fill(rig);
        fill(plain);
        rig.project().scene = 1;
        rig.nextScene();
        for (Rig* r : {&rig, &plain}) {
            r->frames(2);
            r->render(512);
            r->heard.clear();
            const int on = r->project().scene;
            r->project().scene = 0;
            r->session().togglePlay(true);
            r->project().scene = on; // (back before a frame is drawn: going to another scene is itself a step of undo)
            r->frames(1);
            r->render(static_cast<int>(at * kStep));
        }
        rig.undo();
        for (Rig* r : {&rig, &plain}) {
            r->frames(1);
            r->render(40 * kStep);
        }
        std::printf("  the copy of scene 2 taken back at step %.2f of the song: %g from the song left alone\n", at, static_cast<double>(apart(rig.heard, plain.heard)));
    }
}

int main(int argc, char** argv) {
    ui::gStickSpeed = 1.f;
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measure();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--edge") == 0) {
        edge();
        return 0;
    }
    struct Test { const char* name; void (*run)(); bool sound; }; // sound: its proof is of the sound, and is made again on three helper threads
    const Test kTests[] = {
        {"testTheSceneIsCopied", testTheSceneIsCopied, false}, {"testOnlyOnTheGrids", testOnlyOnTheGrids, false}, {"testNoRoom", testNoRoom, false}, {"testUndo", testUndo, false},
        {"testUndoPutsThePlayerOnABarThatIsThere", testUndoPutsThePlayerOnABarThatIsThere, false}, {"testUndoIsOneStep", testUndoIsOneStep, false},
        {"testPlayingGoesOnUnbroken", testPlayingGoesOnUnbroken, true}, {"testTheSongGoesOnUnbroken", testTheSongGoesOnUnbroken, true}, {"testAnotherSceneIsPlaying", testAnotherSceneIsPlaying, false},
        {"testUndoWhileTheFirstOfTheTwoPlays", testUndoWhileTheFirstOfTheTwoPlays, false}, {"testTheLastSceneIsPlaying", testTheLastSceneIsPlaying, false},
        {"testTheSongIsLongerAtOnce", testTheSongIsLongerAtOnce, false}, {"testStopped", testStopped, false}, {"testTheEngineHasTheWholeSong", testTheEngineHasTheWholeSong, false},
        {"testWriteGoesToTheNewScene", testWriteGoesToTheNewScene, false}, {"testTheFramesBeforeTheEngineHears", testTheFramesBeforeTheEngineHears, false}, {"testTheCommand", testTheCommand, false},
        {"testTheEnginesMove", testTheEnginesMove, false}, {"testNothingFreedOnTheAudioThreadAndAllOfItComesBack", testNothingFreedOnTheAudioThreadAndAllOfItComesBack, false}, {"testASceneHandedOverIsWhatTheCommandsWrite", testASceneHandedOverIsWhatTheCommandsWrite, false},
        {"testAnArpeggioGoesWithItsScene", testAnArpeggioGoesWithItsScene, true}, {"testOnceThroughAfterAMove", testOnceThroughAfterAMove, true},
        {"testTheCopyIsWhatTheRollWrites", testTheCopyIsWhatTheRollWrites, true}, {"testTheSixteenthSceneIsGivenBack", testTheSixteenthSceneIsGivenBack, false},
        {"testMovesLostToAFullQueue", testMovesLostToAFullQueue, false}, {"testTakenBackAfterSomethingUndoDoesNotKnow", testTakenBackAfterSomethingUndoDoesNotKnow, false},
        {"testTheSongBesideTheFinishedSong", [] { testTheSongBesideTheFinishedSong(false); }, false}, {"testTheSongBesideTheFinishedSongLater", [] { testTheSongBesideTheFinishedSong(true); }, true},
        {"testTakenBackWhileTheCopyPlaysInItsTurn", testTakenBackWhileTheCopyPlaysInItsTurn, false}, {"testTheSongScreensCursorFollows", testTheSongScreensCursorFollows, false},
        {"testTwelveTracksInOneStep", testTwelveTracksInOneStep, false}, {"testInOneStepWhateverPlays", testInOneStepWhateverPlays, false}, {"testStartStraightAfter", testStartStraightAfter, false},
        {"testEditsStraightAfter", testEditsStraightAfter, false}, {"testARealAudioThread", testARealAudioThread, false},
    };
    // `--only NAME...`: just those (for trying a change). `--times`: how long each takes.
    bool times = false;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--times") == 0) times = true;
        else if (std::strcmp(argv[i], "--only") != 0) only.push_back(argv[i]);
    }
    int ran = 0;
    for (int helpers : {0, 3}) {
        gHelpers = helpers;
        if (helpers) std::printf("the proofs of the sound again, the tracks rendered on %d helper threads:\n", helpers);
        for (const Test& test : kTests) {
            if (helpers && !test.sound) continue;
            if (!only.empty() && std::find(only.begin(), only.end(), test.name) == only.end()) continue;
            const Uint64 from = SDL_GetPerformanceCounter();
            test.run();
            ++ran;
            if (times) std::printf("  %s: %.1f s\n", test.name, static_cast<double>(SDL_GetPerformanceCounter() - from) / static_cast<double>(SDL_GetPerformanceFrequency()));
        }
    }
    if (ran == 0) {
        std::printf("no test of that name\n");
        return 1;
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("next scene tests passed\n");
    return 0;
}
