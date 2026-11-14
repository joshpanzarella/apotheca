// The mixer: a track's LEVEL and PAN, and the master's, on the SONG screen's heads.
// The engine's side (the pan law, the glide, the middle untouched to the bit, the
// helpers), the gesture (hold A on a head), what is drawn (off the drawn screen), the
// song file, and undo.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
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

Command make(Command::Type type, int track, float value) {
    Command c;
    c.type = type;
    c.track = track;
    c.value = value;
    return c;
}

std::vector<float> render(Engine& engine, int frames) {
    std::vector<float> out(2 * static_cast<std::size_t>(frames));
    engine.render(out.data(), frames);
    return out;
}

// The loudest sample of one side (0 left, 1 right), from frame `from` on.
float side(const std::vector<float>& out, int which, int from = 0) {
    float p = 0.f;
    for (std::size_t i = 2 * static_cast<std::size_t>(from) + static_cast<std::size_t>(which); i < out.size(); i += 2) p = std::fabs(out[i]) > p ? std::fabs(out[i]) : p;
    return p;
}

// ---- the engine ----------------------------------------------------------------------------------

void testThePanLaw() {
    float l = 0.f, r = 0.f;
    panGains(0.f, l, r);
    CHECK(l == 1.f && r == 1.f); // the middle: untouched
    panGains(-1.f, l, r);
    CHECK(std::fabs(l - std::sqrt(2.f)) < 1e-6f && r == 0.f);
    panGains(1.f, l, r);
    CHECK(l == 0.f && std::fabs(r - std::sqrt(2.f)) < 1e-6f);
    panGains(-5.f, l, r); // held to hard left
    CHECK(std::fabs(l - std::sqrt(2.f)) < 1e-6f && r == 0.f);
    panGains(std::nanf(""), l, r);
    CHECK(l == 1.f && r == 1.f);
    // Equal power everywhere: the two squared add to 2. Further left, the left louder and the right quieter.
    float lastL = 2.f, lastR = -1.f;
    for (int i = -50; i <= 50; ++i) {
        panGains(static_cast<float>(i) / 50.f, l, r);
        CHECK(std::fabs(l * l + r * r - 2.f) < 1e-5f);
        CHECK(l <= lastL && r >= lastR);
        lastL = l, lastR = r;
    }
    panGains(-0.6f, l, r);
    CHECK(l > 1.f && r < 1.f && r > 0.f);
}

// A held note on track 1 (Phazed), the drums silent.
void holdANote(Engine& engine) {
    engine.addSynthTrack(1, SynthKind::Phazed);
    Command note;
    note.type = Command::NoteOn;
    note.track = 1;
    note.a = 48;
    note.value = 1.f;
    engine.send(note);
    engine.send(make(Command::SetTrackLevel, 1, 0.2f)); // under the ceiling, so what is measured is the gain
    render(engine, 4800);
}

void testAPannedTrack() {
    // Hard left: nothing on the right, and the left at +3 dB (equal power) of the middle's.
    Engine middle, left;
    holdANote(middle);
    holdANote(left);
    left.send(make(Command::SetTrackPan, 1, -1.f));
    const std::vector<float> m = render(middle, 2400), l = render(left, 2400);
    CHECK(side(l, 1, 480) == 0.f && side(l, 0) > 0.f);
    const float ratio = left.trackPeak(1) / middle.trackPeak(1);
    CHECK(std::fabs(ratio - std::sqrt(2.f)) < 0.05f);
    // It glides there: the first samples after the command are near where they were, not cut.
    CHECK(std::fabs(l[1]) > 0.5f * std::fabs(m[1]) && std::fabs(m[1]) > 0.f);
    CHECK(side(l, 1, 0) > 0.f);
    // ...and has arrived in 5 ms (240 samples): exact zeros from there.
    CHECK(side(l, 1, 245) == 0.f);
    // Hard right.
    left.send(make(Command::SetTrackPan, 1, 1.f));
    render(left, 960);
    const std::vector<float> r = render(left, 2400);
    CHECK(side(r, 0) == 0.f && side(r, 1) > 0.f);
}

void testALevelGlides() {
    Engine engine;
    holdANote(engine);
    engine.send(make(Command::SetTrackLevel, 1, 0.f));
    const std::vector<float> out = render(engine, 1200);
    CHECK(std::fabs(out[0]) + std::fabs(out[2]) > 0.f); // not cut at once: no click
    CHECK(side(out, 0, 245) == 0.f && side(out, 1, 245) == 0.f);
    // Up again: the same glide.
    engine.send(make(Command::SetTrackLevel, 1, 0.2f));
    const std::vector<float> back = render(engine, 1200);
    float early = 0.f;
    for (int i = 0; i < 20; ++i) early = std::max(early, std::fabs(back[2 * i]));
    CHECK(early < 0.5f * side(back, 0, 600) && side(back, 0, 600) > 0.f);
}

void testTheMaster() {
    Engine plain, master;
    holdANote(plain);
    holdANote(master);
    master.send(make(Command::SetTrackLevel, kMasterTrack, 0.5f));
    render(plain, 480);
    render(master, 480);
    const std::vector<float> p = render(plain, 2400), q = render(master, 2400);
    bool half = side(p, 0) > 0.f;
    for (std::size_t i = 0; i < p.size(); ++i) half = half && std::fabs(q[i] - 0.5f * p[i]) < 1e-6f;
    CHECK(half);
    // Back to the usual (1): it glides there (a whole step from 0 to 1 takes 5 ms, so this half takes 2.5),
    // from where it was, and then is the plain sound to the bit.
    master.send(make(Command::SetTrackLevel, kMasterTrack, 1.f));
    const std::vector<float> back = render(master, 2400), plainBack = render(plain, 2400);
    float sum = 0.f, plainSum = 0.f; // its first 60 samples: from 0.5 to 0.75 of the plain sound, 0.625 on the whole
    for (int i = 0; i < 2 * 60; ++i) sum += std::fabs(back[i]), plainSum += std::fabs(plainBack[i]);
    bool gliding = plainSum > 0.f && sum > 0.55f * plainSum && sum < 0.72f * plainSum, there = true;
    for (std::size_t i = 2 * 300; i < back.size(); ++i) there = there && back[i] == plainBack[i];
    CHECK(gliding && there);
    master.send(make(Command::SetTrackLevel, kMasterTrack, 0.5f));
    master.send(make(Command::SetTrackPan, kMasterTrack, 1.f));
    render(master, 480);
    render(plain, 480);
    const std::vector<float> right = render(master, 2400), still = render(plain, 2400);
    CHECK(side(right, 0) == 0.f && side(right, 1) > 0.f);
    CHECK(std::fabs(side(right, 1) / side(still, 1) - 0.5f * std::sqrt(2.f)) < 1e-3f); // half, and the right side 3 dB up
    // A bus or a number past the tracks is not a track: nothing happens.
    master.send(make(Command::SetTrackPan, kEchoBus, -1.f));
    master.send(make(Command::SetTrackLevel, 99, 0.f));
    render(master, 480);
    CHECK(side(render(master, 2400), 1) > 0.f);
}

// A drum hit thrown to a bus goes at the track's level as it glides: turned to nothing just before
// the hit, the bus still gets the start of it; turned down a while before, nothing.
void testAThrownHitTakesTheGlide() {
    for (int wait = 0; wait < 2; ++wait) {
        Engine engine;
        engine.setEffect(kEchoBus, 0, EffectKind::Tape);
        render(engine, 960);
        engine.send(make(Command::SetTrackLevel, 0, 0.f));
        if (wait) render(engine, 960);
        Command hit = make(Command::Trigger, 0, 1.f);
        hit.a = 0;
        hit.extra = static_cast<float>(4 * kThrowEcho);
        engine.send(hit);
        render(engine, 2400);
        CHECK(engine.busAwake(0) == (wait == 0));
    }
}

// A track taken away and given an instrument again starts at the usual level and in the middle.
void testATrackThatComesBackIsAtTheUsual() {
    Engine engine;
    holdANote(engine);
    engine.send(make(Command::SetTrackLevel, 1, 0.f));
    engine.send(make(Command::SetTrackPan, 1, 1.f));
    render(engine, 960);
    engine.removeTrack(1);
    render(engine, 960);
    engine.addSynthTrack(1, SynthKind::Phazed); // (no level sent this time)
    Command note = make(Command::NoteOn, 1, 1.f);
    note.a = 48;
    engine.send(note);
    render(engine, 960);
    const std::vector<float> out = render(engine, 2400);
    CHECK(side(out, 0) > 0.f && side(out, 1) > 0.f);
    // ...at once, not gliding up from the old track's nothing: its first notes as loud as a track never touched.
    float first[2] = {0.f, 0.f};
    for (int touched = 0; touched < 2; ++touched) {
        Engine again;
        holdANote(again);
        again.send(make(Command::SetTrackLevel, 1, touched ? 0.f : kUsualTrackLevel));
        render(again, 960);
        again.removeTrack(1);
        render(again, 960);
        again.addSynthTrack(1, SynthKind::Phazed);
        again.send(note);
        first[touched] = side(render(again, 120), 0);
    }
    CHECK(first[0] > 0.f && std::fabs(first[1] / first[0] - 1.f) < 1e-3f);
}

// The engine's every part, played, as a song is: drums and three synths, sends up, a slot each.
void playABusyBar(Engine& engine) {
    for (int t = 1; t < 4; ++t) engine.addSynthTrack(t, t == 2 ? SynthKind::Twin : SynthKind::Phazed);
    for (int t = 0; t < 4; ++t) {
        Command send = make(Command::SetSend, t, 0.3f);
        send.a = t % 2;
        engine.send(send);
    }
    engine.setEffect(1, 0, EffectKind::Drive);
    engine.setEffect(kEchoBus, 0, EffectKind::Tape);
    engine.setEffect(kSpaceBus, 0, EffectKind::Hall);
    for (int step = 0; step < 16; step += 2) {
        Command hit;
        hit.type = Command::SetStep;
        hit.a = step % 3;
        hit.b = step;
        hit.value = 0.9f;
        engine.send(hit);
    }
    for (int t = 1; t < 4; ++t) {
        Command note;
        note.type = Command::SetNote;
        note.track = t;
        note.a = 0;
        note.b = t * 2;
        note.c = 40 + 7 * t;
        note.d = 4;
        note.value = 0.8f;
        engine.send(note);
    }
    Command play;
    play.type = Command::Play;
    engine.send(play);
}

// A song with every pan in the middle and every level as it always was is the same to the bit, sent or not.
void testTheMiddleIsUntouched() {
    Engine plain, told;
    playABusyBar(plain);
    playABusyBar(told);
    for (int t = 0; t < kNumTracks; ++t) {
        told.send(make(Command::SetTrackLevel, t, kUsualTrackLevel));
        told.send(make(Command::SetTrackPan, t, 0.f));
    }
    told.send(make(Command::SetTrackLevel, kMasterTrack, 1.f));
    told.send(make(Command::SetTrackPan, kMasterTrack, 0.f));
    bool same = true;
    for (int block = 0; block < 200 && same; ++block) {
        const std::vector<float> a = render(plain, 256), b = render(told, 256);
        same = std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }
    CHECK(same);
    // ...and the mixer's own numbers say so: 0 DB is the usual level exactly, and the middle is 0.
    CHECK(mixGain(0, false) == kUsualTrackLevel && mixGain(0, true) == 1.f && panAmount(0) == 0.f);
    CHECK(std::fabs(mixGain(-6, false) / kUsualTrackLevel - 0.501187f) < 1e-5f && std::fabs(mixGain(6, true) - 1.995262f) < 1e-5f);
    CHECK(panAmount(-kPanMost) == -1.f && panAmount(25) == 0.5f);
}

// Levels and pans moving, on any number of helpers: the same to the bit.
void testHelpersChangeNothing() {
    std::vector<float> first;
    for (int helpers = 0; helpers <= 3; helpers += 3) {
        Engine engine;
        engine.useWorkers(helpers);
        playABusyBar(engine);
        engine.send(make(Command::SetTrackPan, 1, -0.7f));
        engine.send(make(Command::SetTrackPan, 2, 0.4f));
        engine.send(make(Command::SetTrackLevel, 3, 0.3f));
        engine.send(make(Command::SetTrackLevel, kMasterTrack, 0.8f));
        engine.send(make(Command::SetTrackPan, kMasterTrack, 0.2f));
        std::vector<float> all;
        for (int block = 0; block < 120; ++block) {
            if (block == 60) engine.send(make(Command::SetTrackPan, 0, 1.f)); // and one moving while it plays
            const std::vector<float> out = render(engine, 256);
            all.insert(all.end(), out.begin(), out.end());
        }
        engine.useWorkers(0);
        if (helpers == 0) first = all;
        else CHECK(all.size() == first.size() && std::memcmp(all.data(), first.data(), all.size() * sizeof(float)) == 0);
    }
}

// ---- the app -------------------------------------------------------------------------------------

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    SongScreen& song() { return app.song(); }
};

const SDL_Keycode A = SDLK_x, B = SDLK_z, R1 = SDLK_w, SELECT = SDLK_BACKSPACE;
const SDL_Keycode STICK_UP = SDLK_i, STICK_LEFT = SDLK_j;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;

// What the engine makes of track 1 now: a note on it (the drums silent), the right side's loudest and the left's.
struct Heard { float left, right; };
Heard hearTrackOne(Rig& rig) {
    rig.engine->render(std::vector<float>(2 * 960).data(), 960); // what was sent, glided there
    rig.session().audition(1, 60, 0.8f);
    rig.frames(1);
    const std::vector<float> out = render(*rig.engine, 4800);
    return {side(out, 0), side(out, 1)};
}

void testTheGesture() {
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.song();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    session.view = View::Song;
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1 && !song.mixing());
    // Hold A: the directions turn the track's level (up, down, a dB a press) and its pan (left, right).
    rig.key(A, true);
    CHECK(song.mixing());
    rig.tap(UP, 3);
    CHECK(session.mixDb(1) == 3 && session.mixPan(1) == 0 && session.mixDb(0) == 0);
    rig.tap(UP, 5);
    CHECK(session.mixDb(1) == kMixMostDb); // held at the top
    rig.tap(DOWN, 2);
    rig.tap(LEFT, 3);
    rig.tap(RIGHT);
    CHECK(session.mixDb(1) == kMixMostDb - 2 && session.mixPan(1) == -2);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1); // nothing moved the cursor
    // The D-pad held runs on, at a cursor's speed.
    rig.key(DOWN, true);
    rig.frames(30);
    rig.key(DOWN, false);
    const int ran = session.mixDb(1);
    CHECK(ran < kMixMostDb - 2 - 8 && ran >= kMixLeastDb);
    rig.frames(10);
    CHECK(session.mixDb(1) == ran); // and stops when let go
    // B, with A held: back to the usual, 0 DB and the middle. Nothing deleted.
    rig.key(B, true);
    rig.frames(150); // (held, as long as a delete would take: it is not one)
    rig.key(B, false);
    CHECK(session.mixDb(1) == 0 && session.mixPan(1) == 0 && rig.project().trackCount() == 2);
    // Let go having turned something: not a tap, so no keyboard for its name.
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.app.session().view == View::Song && !song.mixing());
    // The stick, with A held, for big moves.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(30);
    rig.key(STICK_UP, false);
    rig.key(STICK_LEFT, true);
    rig.frames(30);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(session.mixDb(1) >= 3 && session.mixPan(1) <= -10);
    CHECK(song.cursorOnHead() && song.cursorColumn() == 1);
    rig.key(A, false);
    // Hard left, by the gesture: the engine was told.
    rig.key(A, true);
    rig.key(LEFT, true);
    rig.frames(80);
    rig.key(LEFT, false);
    rig.key(A, false);
    CHECK(session.mixPan(1) == -kPanMost);
    const Heard left = hearTrackOne(rig);
    CHECK(left.left > 0.f && left.right == 0.f);
    // A tap of A on the head still names the track; a tap of B there empties nothing and changes no level.
    session.setMix(1, -6, 10);
    rig.tap(B);
    CHECK(session.mixDb(1) == -6 && session.mixPan(1) == 10 && rig.project().trackCount() == 2);
    rig.tap(A);
    rig.frames(1);
    CHECK(rig.app.session().view == View::Settings);
    session.view = View::Song;
    // A held long enough for the card, nothing turned, let go: a look at the level, not a tap. No keyboard
    // (owner, 2026-11-14). Just short of the card it is still a tap; the card is drawn only once it is up.
    rig.key(A, true);
    rig.frames(1);
    CHECK(song.mixing() && !song.mixCardUp());
    rig.frames(SongScreen::kMixCardMs / 16 + 1);
    CHECK(song.mixCardUp());
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.app.session().view == View::Song && session.mixDb(1) == -6 && session.mixPan(1) == 10);
    rig.key(A, true);
    rig.frames(SongScreen::kMixCardMs / 16 - 2);
    CHECK(!song.mixCardUp());
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.app.session().view == View::Settings);
    session.view = View::Song;
    rig.frames(1);
    // A held on a cell is the cell's length, as it was: no level moves.
    song.moveCursorTo(0, 1);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(!song.mixing() && session.mixDb(1) == -6);
    session.view = View::Song;

    // MASTER: right of + TRACK. The same gesture, for everything.
    song.moveCursorTo(0, rig.project().trackCount());
    rig.tap(RIGHT);
    CHECK(song.cursorOnMaster() && !song.cursorOnHead());
    rig.key(A, true);
    rig.tap(DOWN, 6);
    rig.tap(RIGHT, 10);
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.mixDb(kMasterTrack) == -6 && session.mixPan(kMasterTrack) == 10 && session.mixDb(1) == -6);
    CHECK(rig.project().masterDb == -6 && rig.project().masterPan == 10);
    // B, R1, a tap of A there: nothing to empty, delete, change or name.
    rig.key(B, true);
    rig.frames(150);
    rig.key(B, false);
    rig.tap(R1);
    rig.tap(A);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 2 && !song.choosing() && session.view == View::Song && song.cursorOnMaster());
    rig.key(A, true);
    rig.tap(B);
    rig.key(A, false);
    CHECK(session.mixDb(kMasterTrack) == 0 && session.mixPan(kMasterTrack) == 0);
    // Up, down and right go nowhere; left is + TRACK again.
    rig.tap(UP);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(song.cursorOnMaster());
    rig.tap(LEFT);
    CHECK(!song.cursorOnMaster() && song.cursorColumn() == rig.project().trackCount());
    // With twelve tracks there is no + TRACK: MASTER is right of the last head, and left of it is that head.
    while (rig.project().trackCount() < kNumTracks) session.addTrack(TrackKind::Drums);
    song.moveCursorTo(0, kNumTracks - 1);
    rig.tap(UP);
    rig.tap(RIGHT);
    CHECK(song.cursorOnMaster());
    rig.tap(LEFT);
    CHECK(song.cursorOnHead() && song.cursorColumn() == kNumTracks - 1 && !song.cursorOnMaster());
    // A cell's row does not go there.
    song.moveCursorTo(0, kNumTracks - 1);
    rig.tap(RIGHT);
    CHECK(!song.cursorOnMaster() && song.cursorColumn() == kNumTracks - 1);
    // Only tracks and the master: a bus is not one.
    session.setMix(kEchoBus, 5, 5);
    session.setMix(kNumTracks, 5, 5);
    CHECK(session.mixDb(kEchoBus) == 0 && rig.project().masterDb == 0);
}

// Buttons held together with A on a head, and B's delete with A pressed during it.
void testOtherButtonsWithA() {
    const SDL_Keycode X = SDLK_s, Y = SDLK_a, L1 = SDLK_q;
    Rig rig;
    Session& session = rig.session();
    SongScreen& song = rig.song();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    session.view = View::Song;
    // B held on track 2's head, then A while its bar fills: the delete is called off, and A let go names nothing.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(B, true);
    rig.frames(20);
    rig.key(A, true);
    rig.frames(150); // (longer than a delete takes)
    CHECK(rig.project().trackCount() == 2 && song.cursorOnHead() && song.cursorColumn() == 1);
    rig.key(B, false);
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.view == View::Song && rig.project().trackCount() == 2);
    // The same on a cell: A calls the delete off (B's tap has emptied the cell, as before), and opens nothing.
    song.moveCursorTo(0, 1);
    rig.key(B, true);
    rig.frames(20);
    rig.key(A, true);
    rig.frames(150);
    CHECK(rig.project().trackCount() == 2);
    rig.key(B, false);
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.view == View::Song && rig.project().trackCount() == 2);
    // B held alone still deletes (the check above is not just a delete that never comes).
    session.addTrack(TrackKind::Phazed);
    rig.frames(1);
    song.moveCursorTo(0, 2);
    rig.tap(UP);
    rig.key(B, true);
    rig.frames(150);
    rig.key(B, false);
    CHECK(rig.project().trackCount() == 2);
    // Y, X, L1 with A held on a head: no mute, no solo, and A let go is no tap (no keyboard for the name).
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    for (SDL_Keycode other : {Y, X, L1}) {
        rig.key(A, true);
        rig.tap(other);
        rig.key(A, false);
        rig.frames(1);
        CHECK(session.view == View::Song && !rig.project().tracks[1].muted && !rig.project().tracks[1].solo);
    }
    // ...and on MASTER too.
    song.moveCursorTo(0, rig.project().trackCount());
    rig.tap(RIGHT);
    CHECK(song.cursorOnMaster());
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    rig.frames(1);
    CHECK(session.view == View::Song && song.cursorOnMaster());
    // Away from the card they do as they did: Y on the head mutes.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.tap(Y);
    CHECK(rig.project().tracks[1].muted);
}

// Off the drawn screen: the mark on a changed head, the card while A is held, the hints.
void testWhatIsDrawn() {
    Canvas canvas, scratch;
    CHECK(canvas.ok && scratch.ok);
    if (!canvas.ok || !scratch.ok) return;
    for (int theme : {kThemeBrown, kThemePaper}) {
        ui::useTheme(theme);
        Rig rig;
        Session& session = rig.session();
        SongScreen& song = rig.song();
        session.addTrack(TrackKind::Phazed);
        session.addTrack(TrackKind::Phazed);
        rig.frames(2);
        session.view = View::Song;
        const auto draw = [&] {
            rig.frames(1);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            return ui::gHintsOver == 0.f;
        };
        const auto marked = [&](int track) {
            const SDL_Rect head = song.headRect(track);
            return canvas.is(head.x + SongScreen::kMixMarkInset + 1, head.y + SongScreen::kMixMarkInset + 1, ui::kBrass) &&
                   canvas.is(head.x + SongScreen::kMixMarkInset + SongScreen::kMixMarkSize - 1, head.y + SongScreen::kMixMarkInset + SongScreen::kMixMarkSize - 1, ui::kBrass);
        };
        // Words in the fixed lettering, drawn from the right-hand end `right` (as the card draws its values).
        const auto inkedRight = [&](float right, float y, const char* text, SDL_Color colour) {
            const Font& font = scratch.fonts->small;
            const float x = right - font.width(text);
            const int x0 = static_cast<int>(x) - 2, x1 = static_cast<int>(right) + 3, y0 = static_cast<int>(y) - 4, y1 = static_cast<int>(y) + 20;
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
        };
        CHECK(draw() && !marked(0) && !marked(1) && !marked(kMasterTrack));
        // Track 2 at -6 dB, L 30: its head is marked, the others not.
        session.setMix(1, -6, -30);
        CHECK(draw() && marked(1) && !marked(0) && !marked(2) && !marked(kMasterTrack));
        // A pan alone marks it too.
        session.setMix(1, 0, 4);
        CHECK(draw() && marked(1));
        session.setMix(1, -6, -30);
        // A held on its head: the card, under the head, says both; the values brass (not the usual).
        song.moveCursorTo(0, 1);
        rig.tap(UP);
        rig.key(A, true);
        rig.frames(SongScreen::kMixCardMs / 16 + 2); // (the card comes up once A has been held a moment)
        CHECK(draw());
        const SongScreen::Place card = song.mixCard();
        const SDL_Rect head = song.headRect(1);
        CHECK(card.y > head.y + head.h && card.x <= head.x + head.w / 2 && card.x + SongScreen::kMixCardW >= head.x + head.w / 2);
        const float right = card.x + SongScreen::kMixCardW - 8.f;
        CHECK(inkedRight(right, card.y + 6.f, "-6 DB", ui::kBrass) && inkedRight(right, card.y + 34.f, "L 30", ui::kBrass));
        CHECK(inkedRight(card.x + 8.f + scratch.fonts->small.width("LEVEL"), card.y + 6.f, "LEVEL", ui::kDim));
        CHECK(canvas.is(card.x, card.y + 20, ui::kBrass)); // its edge
        // The pan bar: brass to the left of the middle, none to the right.
        const int middle = card.x + 8 + (SongScreen::kMixCardW - 16) / 2;
        CHECK(canvas.is(middle - 4, card.y + 54, ui::kBrass) && !canvas.is(middle + 4, card.y + 54, ui::kBrass));
        // ...and the level bar: brass at the left, the line's colour past -6 dB.
        CHECK(canvas.is(card.x + 10, card.y + 26, ui::kBrass) && canvas.is(card.x + SongScreen::kMixCardW - 10, card.y + 26, ui::kLine));
        // At the usual, the values are in the text's colour, and the pan says MIDDLE.
        rig.tap(B);
        CHECK(draw() && inkedRight(right, card.y + 6.f, "0 DB", ui::kText) && inkedRight(right, card.y + 34.f, "MIDDLE", ui::kText));
        rig.tap(RIGHT, 12);
        rig.tap(UP, 2);
        CHECK(draw() && inkedRight(right, card.y + 6.f, "+2 DB", ui::kBrass) && inkedRight(right, card.y + 34.f, "R 12", ui::kBrass));
        CHECK(canvas.is(middle + 4, card.y + 54, ui::kBrass) && !canvas.is(middle - 4, card.y + 54, ui::kBrass));
        rig.key(A, false);
        // Let go: no card; the head is marked.
        CHECK(draw() && !inkedRight(right, card.y + 6.f, "+2 DB", ui::kBrass) && marked(1));
        // MASTER: a head of its own at the right-hand end, past + TRACK, the same card.
        const SDL_Rect masterHead = song.headRect(kMasterTrack);
        CHECK(masterHead.x > song.headRect(2).x + 2 * song.headRect(2).w && masterHead.x + masterHead.w <= ui::kScreenW - 4);
        song.moveCursorTo(0, rig.project().trackCount());
        rig.tap(RIGHT);
        rig.key(A, true);
        rig.tap(DOWN, 3);
        CHECK(draw());
        const SongScreen::Place masterCard = song.mixCard();
        CHECK(masterCard.x + SongScreen::kMixCardW <= ui::kScreenW - 4); // held on the screen
        CHECK(inkedRight(masterCard.x + SongScreen::kMixCardW - 8.f, masterCard.y + 6.f, "-3 DB", ui::kBrass));
        rig.key(A, false);
        CHECK(draw() && marked(kMasterTrack));
    }
    ui::useTheme(kThemeBrown);
}

// The hints say what the gesture does, and fit.
void testTheHints() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    session.view = View::Song;
    // The bar's words, by what was written for it: log what is drawn.
    DrawLog frame;
    const auto said = [&](const char* words) {
        for (const DrawLog::Text& t : frame.texts)
            if (t.words == words) return true;
        return false;
    };
    const auto draw = [&] {
        rig.frames(1);
        frame.clear();
        gDrawLog = &frame;
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        gDrawLog = nullptr;
        if (std::getenv("MIX_PRINT")) { std::printf("over %.1f:", ui::gHintsOver); for (const DrawLog::Text& t : frame.texts) std::printf(" [%s]", t.words.c_str()); std::printf("\n"); }
        return ui::gHintsOver == 0.f;
    };
    for (const char* help : {"MENU: HELP", "HLD SEL: HELP"}) {
        ui::gHelpHint = help;
        rig.song().moveCursorTo(0, 1);
        rig.tap(UP);
        CHECK(draw() && said("mix") && said("name") && said("mute") && said("solo"));
        rig.key(A, true);
        rig.frames(SongScreen::kMixCardMs / 16 + 2);
        CHECK(draw() && said("level") && said("pan") && said("back to 0 DB, middle"));
        rig.tap(UP), rig.tap(DOWN); // (turned, so letting go is not a tap: that would name the track)
        rig.key(A, false);
        rig.song().moveCursorTo(0, rig.project().trackCount());
        rig.tap(RIGHT);
        CHECK(draw() && said("level and pan of everything") && said("MASTER"));
        rig.key(A, true);
        rig.frames(SongScreen::kMixCardMs / 16 + 2);
        CHECK(draw() && said("level") && said("pan"));
        rig.key(A, false);
    }
    ui::gHelpHint = "MENU: HELP";
}

// ---- the song file ---------------------------------------------------------------------------------

void testTheSongFile() {
    std::unique_ptr<Project> song(new Project), back(new Project);
    song->tracks[1].kind = TrackKind::Phazed;
    song->tracks[2].kind = TrackKind::Drums;
    // At the usual: nothing written, so the file is as it was before there was a mixer.
    std::string text = songText(*song);
    CHECK(text.find("\nmix ") == std::string::npos && text.find("\nmaster ") == std::string::npos);
    song->tracks[1].mixDb = -6;
    song->tracks[1].mixPan = -30;
    song->tracks[2].mixPan = 7; // a pan alone is written
    song->masterDb = 3;
    text = songText(*song);
    CHECK(text.find("\nmix -6 -30\n") != std::string::npos && text.find("\nmix 0 7\n") != std::string::npos && text.find("\nmaster 3 0\n") != std::string::npos);
    std::size_t lines = 0;
    for (std::size_t at = text.find("\nmix "); at != std::string::npos; at = text.find("\nmix ", at + 1)) ++lines;
    CHECK(lines == 2); // track 1, at the usual, has none
    CHECK(parseSong(text, *back));
    CHECK(back->tracks[1].mixDb == -6 && back->tracks[1].mixPan == -30 && back->tracks[2].mixDb == 0 && back->tracks[2].mixPan == 7);
    CHECK(back->tracks[0].mixDb == 0 && back->tracks[0].mixPan == 0 && back->masterDb == 3 && back->masterPan == 0);
    const std::string again = songText(*back); // (read, a synth track has its preset: written again, it is stable)
    CHECK(again.find("\nmix -6 -30\n") != std::string::npos && again.find("\nmaster 3 0\n") != std::string::npos);
    CHECK(parseSong(again, *back) && songText(*back) == again);
    // The text cuts into its parts (undo reads a track at a time): MASTER is the song's own line.
    std::string head;
    std::vector<std::string> parts;
    CHECK(cutSong(text, head, parts) && parts.size() == 3 && head.find("master 3 0") != std::string::npos && parts[1].find("mix -6 -30") != std::string::npos);
    CHECK(!cutSong(text + "master 1 1\n", head, parts)); // a song's own line among a track's: not cut
    // An old song (no such lines) is at the usual everywhere.
    CHECK(parseSong("apotheca-song 1\ntempo 100\ntrack drums\ntrack phazed\n", *back));
    CHECK(back->tracks[0].mixDb == 0 && back->tracks[1].mixPan == 0 && back->masterDb == 0 && back->masterPan == 0);
    // Numbers held to their limits, as doubles, before they are made whole.
    CHECK(parseSong("apotheca-song 1\nmaster 1e300 -1e300\ntrack drums\nmix -99 99\ntrack phazed\nmix 4.7\ntrack phazed\nmix nan 3\ntrack phazed\nmix\n", *back));
    CHECK(back->masterDb == kMixMostDb && back->masterPan == -kPanMost);
    CHECK(back->tracks[0].mixDb == kMixLeastDb && back->tracks[0].mixPan == kPanMost);
    CHECK(back->tracks[1].mixDb == 4 && back->tracks[1].mixPan == 0);
    CHECK(back->tracks[2].mixDb == 0 && back->tracks[2].mixPan == 0); // ("nan" is not a number the file reads: the line is skipped)
    CHECK(back->tracks[3].mixDb == 0 && back->tracks[3].mixPan == 0);
    // A `mix` before any track means nothing.
    CHECK(parseSong("apotheca-song 1\nmix -5 5\ntrack drums\n", *back) && back->tracks[0].mixDb == 0 && back->masterDb == 0);

    // Opened, the engine is told: track 2 hard left is heard on the left only.
    Rig rig;
    std::unique_ptr<Project> opened(new Project);
    CHECK(parseSong("apotheca-song 1\nmaster -2 0\ntrack drums\ntrack phazed\nmix -3 -50\n", *opened));
    rig.project() = *opened;
    rig.session().applyProject();
    rig.frames(2);
    CHECK(rig.session().mixPan(1) == -kPanMost && rig.session().mixDb(kMasterTrack) == -2);
    const Heard heard = hearTrackOne(rig);
    CHECK(heard.left > 0.f && heard.right == 0.f);
    // The master's level is the engine's too: a song opened at -6 DB is heard at half (one in the middle, to compare).
    std::unique_ptr<Project> halved(new Project), whole(new Project);
    CHECK(parseSong("apotheca-song 1\nmaster -6 0\ntrack drums\ntrack phazed\n", *halved));
    CHECK(parseSong("apotheca-song 1\ntrack drums\ntrack phazed\n", *whole));
    rig.project() = *whole;
    rig.session().applyProject();
    rig.frames(2);
    const Heard full = hearTrackOne(rig);
    rig.project() = *halved;
    rig.session().applyProject();
    rig.frames(2);
    const Heard half = hearTrackOne(rig);
    CHECK(full.left > 0.f && std::fabs(half.left / full.left - 0.501187f) < 0.02f && std::fabs(half.right / full.right - 0.501187f) < 0.02f);
    // ...and another song with none: the middle again.
    std::unique_ptr<Project> plain(new Project);
    CHECK(parseSong("apotheca-song 1\ntrack drums\ntrack phazed\n", *plain));
    rig.project() = *plain;
    rig.session().applyProject();
    rig.frames(2);
    const Heard middle = hearTrackOne(rig);
    CHECK(middle.right > 0.f && middle.left > 0.f);
}

// ---- undo --------------------------------------------------------------------------------------------

void testUndo() {
    Rig rig;
    Session& session = rig.session();
    session.addTrack(TrackKind::Phazed);
    session.view = View::Song;
    rig.frames(2);
    const auto settle = [&] { rig.frames(50); };
    settle();
    const int steps = session.undoSteps();
    // A burst of turning is one step.
    rig.song().moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(A, true);
    rig.tap(LEFT, 50);
    rig.tap(DOWN, 4);
    rig.key(A, false);
    settle();
    CHECK(session.undoSteps() == steps + 1 && session.mixPan(1) == -kPanMost && session.mixDb(1) == -4);
    CHECK(hearTrackOne(rig).right == 0.f);
    // The master, its own step.
    session.setMix(kMasterTrack, -5, 0);
    settle();
    CHECK(session.undoSteps() == steps + 2);
    // A hit on track 1, its own step: undoing it leaves the levels as they are.
    rig.app.drums().setStep(0, 0, 0.9f);
    session.noteEdit();
    settle();
    CHECK(session.undoSteps() == steps + 3);
    CHECK(session.undo());
    CHECK(rig.project().tracks[0].drums[0][0].level[0][0] == 0.f && session.mixPan(1) == -kPanMost && session.mixDb(kMasterTrack) == -5);
    CHECK(session.undo());
    CHECK(session.mixDb(kMasterTrack) == 0 && session.mixPan(1) == -kPanMost);
    CHECK(session.undo());
    CHECK(session.mixDb(1) == 0 && session.mixPan(1) == 0);
    // The engine is told what undo put back: the middle again.
    const Heard middle = hearTrackOne(rig);
    CHECK(middle.right > 0.f);
    // Redo: hard left again, and the master down.
    CHECK(session.redo() && session.redo());
    CHECK(session.mixPan(1) == -kPanMost && session.mixDb(1) == -4 && session.mixDb(kMasterTrack) == -5);
    CHECK(hearTrackOne(rig).right == 0.f);
    // SELECT + B on the SONG screen is the same undo.
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    CHECK(session.mixDb(kMasterTrack) == 0 && session.mixPan(1) == -kPanMost);
}

} // namespace

int main() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    SDL_Init(0);
    testThePanLaw();
    testAPannedTrack();
    testALevelGlides();
    testTheMaster();
    testAThrownHitTakesTheGlide();
    testATrackThatComesBackIsAtTheUsual();
    testTheMiddleIsUntouched();
    testHelpersChangeNothing();
    testTheGesture();
    testOtherButtonsWithA();
    testWhatIsDrawn();
    testTheHints();
    testTheSongFile();
    testUndo();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mixer tests passed\n");
    return 0;
}
