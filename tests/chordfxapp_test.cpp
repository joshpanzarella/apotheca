// CHORD EFFECTS in the app: the gesture on the piano roll (hold X on a chord: up / down pick
// one of six, left / right set it), the strip, the dice, the song file, undo, swap, break,
// drag, NEXT SCENE, a scene copied, and that the engine plays what the roll holds. The engine's
// side is tests/chordfx_test.cpp.

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
#include "chord_effects.hpp"
#include "chords.hpp"
#include "note_marks.hpp"
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
const int kTight = 432;                   // kStrumMs, in frames

// A synth that makes no sound and writes down the notes it is told to begin.
struct Began {
    long frame;
    int pitch;
    float velocity;
};
struct Spy : Instrument {
    std::vector<Began>* log;
    long frame = 0;
    explicit Spy(std::vector<Began>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float velocity) override { log->push_back({frame, note, velocity}); }
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        frame += frames;
    }
};

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a;
const SDL_Keycode R1 = SDLK_w, SELECT = SDLK_BACKSPACE;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const SDL_Keycode STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l, STICK_UP = SDLK_i, STICK_DOWN = SDLK_k;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::vector<Began> log;
    Spy* spy = nullptr;

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    void render(long audioFrames) {
        std::vector<float> out(2 * 1000);
        for (; audioFrames > 0; audioFrames -= 1000) engine->render(out.data(), audioFrames < 1000 ? static_cast<int>(audioFrames) : 1000);
    }
    Project& project() { return app.project(); }
    NotePattern& pattern(int scene = 0, int bar = 0) { return project().tracks[1].notes[scene][bar]; }
    // A synth track (track 2) with the piano roll showing.
    void roll() {
        CHECK(app.session().addTrack(TrackKind::Qmod));
        project().track = 1;
        app.session().view = View::Pattern;
        frames(2);
        render(2000);
    }
    // The synth on track 2 taken out of the engine, and one that only listens put there.
    void listen() {
        log.reserve(100000);
        frames(2);
        render(2000);
        Command c;
        c.type = Command::RemoveInstrument;
        c.track = 1;
        CHECK(engine->send(c));
        render(1000);
        spy = new Spy(&log);
        c.type = Command::InstallSynth;
        c.pointer = spy;
        CHECK(engine->send(c));
        render(1000);
        engine->collectGarbage();
        app.session().sendNotePattern(1, 0); // (taking the synth out emptied the engine's patterns)
        frames(1);
        render(1000);
    }
    // One bar of the song from the top: the notes begun, their frames counted from Play.
    std::vector<Began> playBar(long audioFrames = kBar - 2000, bool song = false) {
        frames(2);
        render(2000);
        log.clear();
        const long began = spy->frame;
        app.session().togglePlay(song);
        frames(1);
        render(audioFrames);
        app.session().togglePlay(song);
        frames(1);
        render(2000);
        std::vector<Began> heard = log;
        for (Began& b : heard) b.frame -= began;
        return heard;
    }
    // A chord of the built-in pack in the first bar, and the cursor on its root.
    ChordEvent& chord(int slot, int root, int step, float length = 4.f) {
        pattern().chords[slot] = makeChord(builtInPack().chords[0], root, step, length, 0.7f, 0);
        app.session().sendNoteBar(1, 0, 0);
        app.notes().moveCursorTo(root, step);
        return pattern().chords[slot];
    }
};

bool samePlay(const ChordPlay& a, const ChordPlay& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// ---- the list, a click, the words (chord_effects.hpp) ----------------------------------------------

void testTheList() {
    const char* names[] = {"RATCHET", "STRUM", "ORDER", "HUMAN TIME", "HUMAN LEVEL", "LENGTHS"};
    CHECK(kChordEffects == 6 && kFxRatchet == 0 && kFxStrum == 1 && kFxOrder == 2 && kFxTime == 3 && kFxLevel == 4 && kFxLengths == 5);
    for (int e = 0; e < kChordEffects; ++e) CHECK(std::string(chordEffectName(e)) == names[e]);
    CHECK(std::string(chordEffectName(-1)) == "RATCHET" && std::string(chordEffectName(6)) == "RATCHET");

    ChordEvent c = makeChord(builtInPack().chords[0], 60, 0, 4.f, 0.7f, 0);
    char text[32];
    const auto value = [&](int effect) {
        chordEffectValue(c, effect, text, sizeof text);
        return std::string(text);
    };
    for (int e = 0; e < kChordEffects; ++e) CHECK(!chordEffectSet(c, e) && value(e) == (e == kFxOrder ? "UP" : "OFF"));
    CHECK(!chordPlays(c) && value(99) == "-");
    CHECK(chordEffectWaits(c, kFxOrder)); // ORDER is the way STRUM runs: nothing to do with no STRUM
    for (int e : {kFxRatchet, kFxStrum, kFxTime, kFxLevel, kFxLengths}) CHECK(!chordEffectWaits(c, e));

    // RATCHET: x1 (kept as 0) to x4, a click at a time, and it stops at each end.
    CHECK(!chordEffectStep(c, kFxRatchet, -1) && c.ratchet == 0);
    for (int hits = 2; hits <= 4; ++hits) {
        CHECK(chordEffectStep(c, kFxRatchet, 1) && c.ratchet == hits && chordEffectSet(c, kFxRatchet));
        CHECK(value(kFxRatchet) == std::string("x") + static_cast<char>('0' + hits));
    }
    CHECK(!chordEffectStep(c, kFxRatchet, 1) && c.ratchet == 4);
    CHECK(!chordPlays(c)); // a ratchet is not one of the five that are a chord's alone
    for (int hits = 3; hits >= 2; --hits) CHECK(chordEffectStep(c, kFxRatchet, -1) && c.ratchet == hits);
    CHECK(chordEffectStep(c, kFxRatchet, -1) && c.ratchet == 0 && value(kFxRatchet) == "OFF" && !chordEffectSet(c, kFxRatchet));

    // STRUM: OFF, STRUM, WIDE STRUM, then the three that make an arpeggio.
    const char* strums[] = {"OFF", "STRUM", "WIDE STRUM", "1/4 STEP", "1/2 STEP", "1 STEP"};
    CHECK(!chordEffectStep(c, kFxStrum, -1));
    for (int s = 1; s < 6; ++s) {
        CHECK(chordEffectStep(c, kFxStrum, 1) && c.play.strum == s && value(kFxStrum) == strums[s] && chordEffectSet(c, kFxStrum) && chordPlays(c));
        CHECK(!chordEffectWaits(c, kFxOrder));
        CHECK(chordEffectWaits(c, kFxRatchet) == (s >= 3)); // a ratchet is not played in an arpeggio
    }
    CHECK(!chordEffectStep(c, kFxStrum, 1) && c.play.strum == 5);
    CHECK(chordEffectStep(c, kFxStrum, -1) && c.play.strum == 4);

    // ORDER.
    const char* orders[] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
    CHECK(!chordEffectStep(c, kFxOrder, -1) && !chordEffectSet(c, kFxOrder));
    for (int o = 1; o < 4; ++o) CHECK(chordEffectStep(c, kFxOrder, 1) && c.play.order == o && value(kFxOrder) == orders[o] && chordEffectSet(c, kFxOrder));
    CHECK(!chordEffectStep(c, kFxOrder, 1) && c.play.order == 3);
    CHECK(chordEffectStep(c, kFxOrder, -1) && c.play.order == 2);

    // The three amounts: 0, 10, 25, 50, 75, 100, and no further.
    const int clicks[] = {0, 10, 25, 50, 75, 100};
    std::uint8_t* amounts[] = {&c.play.time, &c.play.level, &c.play.lengths};
    for (int which = 0; which < 3; ++which) {
        const int effect = kFxTime + which;
        CHECK(!chordEffectStep(c, effect, -1) && *amounts[which] == 0);
        for (int i = 1; i < 6; ++i) {
            CHECK(chordEffectStep(c, effect, 1) && *amounts[which] == clicks[i] && chordEffectSet(c, effect));
            CHECK(value(effect) == std::to_string(clicks[i]) + "%");
        }
        CHECK(!chordEffectStep(c, effect, 1) && *amounts[which] == 100);
        CHECK(chordEffectStep(c, effect, -1) && *amounts[which] == 75);
        *amounts[which] = 100;
    }
    // An amount from a file that is between two clicks steps to the next one either way.
    c.play.time = 30;
    CHECK(chordEffectStep(c, kFxTime, 1) && c.play.time == 50);
    c.play.time = 30;
    CHECK(chordEffectStep(c, kFxTime, -1) && c.play.time == 10);
    CHECK(!chordEffectStep(c, 99, 1));

    // HUMAN TIME alone is a chord effect; so is each of the others.
    for (int which = 0; which < 5; ++which) {
        ChordEvent one = makeChord(builtInPack().chords[0], 60, 0, 4.f, 0.7f, 0);
        CHECK(!chordPlays(one));
        CHECK(chordEffectStep(one, kFxStrum + which, 1));
        CHECK(chordPlays(one) == (which != 1)); // (ORDER alone does nothing)
    }
}

// The strip's one line, with X let go.
void testTheLine() {
    ChordEvent c = makeChord(builtInPack().chords[0], 60, 0, 4.f, 0.7f, 0);
    char text[32];
    const auto line = [&] {
        chordEffectLine(c, text, sizeof text);
        return std::string(text);
    };
    CHECK(line().empty());
    c.play.strum = kStrumWide;
    CHECK(line() == "WIDE STRUM");
    c.play.order = kOrderDown;
    CHECK(line() == "WIDE STRUM +1");
    c.play.lengths = 25;
    CHECK(line() == "WIDE STRUM +2");
    c.ratchet = 3;
    CHECK(line() == "x3 +3");
    c.play.strum = kStrumHalf; // an arpeggio: the ratchet waits, and is not counted
    CHECK(line() == "1/2 STEP +2");
    c = makeChord(builtInPack().chords[0], 60, 0, 4.f, 0.7f, 0);
    c.play.order = kOrderRandom; // ORDER with no STRUM: nothing is on
    CHECK(line().empty());
    c.play.time = 10;
    CHECK(line() == "TIME 10%");
    c.play.level = 25;
    CHECK(line() == "TIME 10% +1");
    c.play.time = 0;
    CHECK(line() == "LEVEL 25%");
    c.play.level = 0;
    c.play.lengths = 100;
    CHECK(line() == "LENGTHS 100%");
}

// ---- the dice ----------------------------------------------------------------------------------------

void testTheDice() {
    std::uint32_t seed = 12345u;
    const auto roll = [&](std::uint32_t outOf) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 12) % outOf;
    };
    ChordEvent c = makeChord(builtInPack().chords[0], 60, 0, 4.f, 0.7f, 0);
    c.throwTo = 2;
    const ChordEvent plain = c;
    const int rolls = 6000;
    int ratchets = 0, strums = 0, arps = 0, off = 0, orders[4] = {}, times = 0, levels = 0, lengths = 0, big = 0;
    for (int i = 0; i < rolls; ++i) {
        const ChordEvent was = c;
        chordEffectRoll(c, roll);
        // Always something, never what it was.
        CHECK(c.ratchet >= 2 || chordPlays(c));
        CHECK(c.ratchet != was.ratchet || !samePlay(c.play, was.play));
        // Nothing but the six is touched: not its THROW, its notes, its level, its place.
        ChordEvent rest = c;
        rest.ratchet = plain.ratchet;
        rest.play = plain.play;
        CHECK(std::memcmp(&rest, &plain, sizeof rest) == 0);
        CHECK(c.ratchet == 0 || (c.ratchet >= 2 && c.ratchet <= 4));
        CHECK(c.play.strum < kStrumKinds && c.play.order < kOrderKinds && c.play.index == 0 && c.play.count == 0);
        CHECK(!(c.play.arpeggio() && c.ratchet)); // an arpeggio takes no ratchet
        if (c.play.strum == kStrumOff) CHECK(c.play.order == kOrderUp);
        for (std::uint8_t amount : {c.play.time, c.play.level, c.play.lengths}) {
            CHECK(amount == 0 || amount == 10 || amount == 25); // the human amounts are small
            big += amount == 25;
        }
        ratchets += c.ratchet >= 2;
        strums += c.play.strum == kStrumTight || c.play.strum == kStrumWide;
        arps += c.play.arpeggio();
        off += c.play.strum == kStrumOff;
        if (c.play.strum != kStrumOff) ++orders[c.play.order];
        times += c.play.time != 0;
        levels += c.play.level != 0;
        lengths += c.play.lengths != 0;
    }
    // The odds (chord_effects.hpp), within what six thousand rolls can show.
    const auto near = [&](int count, double share) { return std::fabs(static_cast<double>(count) / rolls - share) < 0.03; };
    CHECK(near(strums, 0.50) && near(arps, 0.15) && near(off, 0.35));
    CHECK(strums > 3 * arps); // a strum far more often than an arpeggio
    CHECK(near(ratchets, 0.30 * 0.85)); // (30 in 100, less those an arpeggio took)
    CHECK(near(times, 0.50) && near(levels, 0.50) && near(lengths, 0.30));
    CHECK(near(big, 1.30 / 3.0));
    const int ordered = orders[0] + orders[1] + orders[2] + orders[3];
    CHECK(std::fabs(orders[0] / static_cast<double>(ordered) - 0.50) < 0.04 && std::fabs(orders[1] / static_cast<double>(ordered) - 0.20) < 0.04);
    CHECK(std::fabs(orders[2] / static_cast<double>(ordered) - 0.15) < 0.04 && std::fabs(orders[3] / static_cast<double>(ordered) - 0.15) < 0.04);
}

// ---- the gesture ---------------------------------------------------------------------------------------

void testTheGesture() {
    Rig rig;
    rig.roll();
    NoteScreen& roll = rig.app.notes();
    ChordEvent& c = rig.chord(0, 60, 2);
    const ChordEvent plain = c;
    CHECK(roll.chordHere() != nullptr && roll.effectPicked() == kFxRatchet);

    rig.key(X, true);
    CHECK(roll.effecting());
    // Up / down pick the effect, a click a push; it stops at each end. Nothing of the chord changes.
    rig.tap(UP);
    CHECK(roll.effectPicked() == kFxRatchet);
    for (int e = 1; e < kChordEffects; ++e) {
        rig.tap(DOWN);
        CHECK(roll.effectPicked() == e);
    }
    rig.tap(DOWN);
    CHECK(roll.effectPicked() == kFxLengths);
    for (int e = kChordEffects - 2; e >= 0; --e) {
        rig.tap(UP);
        CHECK(roll.effectPicked() == e);
    }
    CHECK(std::memcmp(&c, &plain, sizeof c) == 0 && roll.cursorPitch() == 60 && roll.cursorStep() == 2);

    // Left / right set the one picked.
    rig.tap(RIGHT);
    CHECK(c.ratchet == 2);
    rig.tap(LEFT, 3);
    CHECK(c.ratchet == 0);
    rig.tap(DOWN);
    rig.tap(RIGHT, 2);
    CHECK(c.play.strum == kStrumWide && c.ratchet == 0);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(c.play.order == kOrderDown && c.play.strum == kStrumWide);
    rig.tap(DOWN);
    rig.tap(RIGHT, 3);
    CHECK(c.play.time == 50);
    rig.tap(LEFT);
    CHECK(c.play.time == 25);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    CHECK(c.play.level == 10 && c.play.time == 25);
    rig.tap(DOWN);
    rig.tap(RIGHT, 9);
    CHECK(c.play.lengths == 100 && c.play.level == 10);
    // The chord is still where and what it was: nothing but its effects moved.
    ChordEvent rest = c;
    rest.play = plain.play;
    CHECK(std::memcmp(&rest, &plain, sizeof rest) == 0 && rig.pattern().used() == plain.count && roll.cursorPitch() == 60 && roll.cursorStep() == 2);

    // X + A is still THROW, a step round; X + Y still the dice.
    for (int expected : {1, 2, 3, 0, 1}) {
        rig.tap(A);
        CHECK(c.throwTo == expected);
    }
    CHECK(c.play.lengths == 100 && rig.pattern().used() == plain.count && rig.pattern().freeSlot() == 0); // (A placed no note)
    for (int i = 0; i < 30; ++i) {
        const ChordEvent before = c;
        rig.tap(Y);
        CHECK(c.ratchet >= 2 || chordPlays(c));
        CHECK(c.ratchet != before.ratchet || !samePlay(c.play, before.play));
        CHECK(c.throwTo == 1); // the dice do not throw
    }
    CHECK(rig.pattern().chords[1].velocity <= 0.f); // (Y placed no chord)
    rig.key(X, false);
    CHECK(!roll.effecting());
    rig.frames(2);

    // The one picked is kept from one hold to the next: the same thing on chord after chord is quick.
    CHECK(roll.effectPicked() == kFxLengths);
    c.play = ChordPlay();
    c.ratchet = 0;
    rig.key(X, true);
    rig.tap(RIGHT);
    CHECK(c.play.lengths == 10 && roll.effectPicked() == kFxLengths);
    rig.key(X, false);

    // A held D-pad: a click at once, then clicks, not a run; and they stop when it is let go.
    c.play = ChordPlay();
    rig.key(X, true);
    rig.tap(UP, 2); // HUMAN TIME
    CHECK(roll.effectPicked() == kFxTime);
    rig.key(RIGHT, true);
    CHECK(c.play.time == 10);
    rig.frames(4);
    CHECK(c.play.time == 10);
    rig.frames(120);
    CHECK(c.play.time > 10);
    rig.key(RIGHT, false);
    const int at = c.play.time;
    rig.frames(60);
    CHECK(c.play.time == at);
    rig.key(X, false);
    rig.frames(2);

    // The stick does as the D-pad does: one click a push, at once.
    c.play = ChordPlay();
    roll.pickEffect(kFxRatchet);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    CHECK(roll.effectPicked() == kFxStrum);
    rig.frames(3);
    CHECK(roll.effectPicked() == kFxStrum); // a click, not a run
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(c.play.strum == kStrumTight);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(roll.effectPicked() == kFxRatchet);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    CHECK(c.ratchet == 0 && c.play.strum == kStrumTight); // (a ratchet is already at its least)
    rig.key(STICK_LEFT, false);
    rig.key(X, false);
    rig.frames(2);
    // A stick already pushed over when X goes down does nothing until it has been let go.
    c.play = ChordPlay();
    roll.pickEffect(kFxStrum);
    rig.key(STICK_RIGHT, true);
    roll.moveCursorTo(60, 2);
    rig.key(X, true);
    rig.frames(20);
    CHECK(c.play.strum == kStrumOff && roll.cursorPitch() == 60);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(c.play.strum == kStrumTight);
    rig.key(STICK_RIGHT, false);
    rig.key(X, false);
    rig.frames(2);

    // A plain note keeps its own directions, whatever is picked for chords: left / right its
    // ratchet, up / down its glide's length.
    roll.pickEffect(kFxLengths);
    roll.moveCursorTo(80, 9);
    rig.tap(A);
    const int slot = rig.pattern().at(80, 9);
    CHECK(slot >= 0);
    if (slot < 0) return;
    rig.key(X, true);
    rig.tap(RIGHT);
    CHECK(rig.pattern().notes[slot].ratchet == 2);
    rig.tap(UP);
    CHECK(rig.pattern().notes[slot].glide == kGlideQuarter);
    rig.tap(DOWN);
    CHECK(rig.pattern().notes[slot].glide == kGlideOff);
    rig.tap(LEFT);
    CHECK(rig.pattern().notes[slot].ratchet == 0);
    CHECK(roll.effectPicked() == kFxLengths && samePlay(rig.pattern().notes[slot].play, ChordPlay()));
    rig.key(X, false);
}

// ---- the engine plays what the roll holds ------------------------------------------------------------

void testThroughTheSession() {
    Rig rig;
    rig.roll();
    rig.listen();
    NoteScreen& roll = rig.app.notes();
    ChordEvent& c = rig.chord(0, 60, 2);
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    CHECK(n >= 3);
    // Plain: every key on the step.
    std::vector<Began> heard = rig.playBar();
    CHECK(static_cast<int>(heard.size()) == n);
    for (const Began& b : heard) CHECK(b.frame == 2 * kStep && b.velocity == c.velocity);
    // STRUM, set with the gesture: one after another, lowest first.
    rig.key(X, true);
    roll.pickEffect(kFxStrum);
    rig.tap(RIGHT);
    rig.key(X, false);
    CHECK(c.play.strum == kStrumTight);
    heard = rig.playBar();
    CHECK(static_cast<int>(heard.size()) == n);
    for (std::size_t k = 0; k < heard.size(); ++k) CHECK(heard[k].pitch == keys[k] && heard[k].frame == 2 * kStep + static_cast<long>(k) * kTight);
    // ORDER, the same way: DOWN.
    rig.key(X, true);
    rig.tap(DOWN);
    rig.tap(RIGHT);
    rig.key(X, false);
    CHECK(c.play.order == kOrderDown);
    heard = rig.playBar();
    for (std::size_t k = 0; k < heard.size(); ++k) CHECK(heard[k].pitch == keys[static_cast<std::size_t>(n) - 1 - k] && heard[k].frame == 2 * kStep + static_cast<long>(k) * kTight);
    // HUMAN LEVEL: the levels move, the times do not.
    c.play = chordPlayOf(0, 0, 0, 100, 0);
    rig.app.session().sendNoteBar(1, 0, 0);
    heard = rig.playBar();
    int moved = 0;
    for (const Began& b : heard) {
        CHECK(b.frame == 2 * kStep && std::fabs(b.velocity - c.velocity) <= 0.15f * c.velocity + 1e-6f);
        moved += b.velocity != c.velocity;
    }
    CHECK(moved >= n - 1);
    // An arpeggio.
    c.play = chordPlayOf(kStrumStep, 0, 0, 0, 0);
    rig.app.session().sendNoteBar(1, 0, 0);
    heard = rig.playBar();
    CHECK(heard.size() == 4);
    for (std::size_t m = 0; m < heard.size(); ++m) CHECK(heard[m].pitch == keys[m % static_cast<std::size_t>(n)] && heard[m].frame == static_cast<long>(2 + m) * kStep);

    // The track handed over whole (a cell made longer: its second bar is filled from the first,
    // and every pattern goes in one piece) carries them too, and the copy has them.
    c.play = chordPlayOf(kStrumTight, 0, 0, 0, 0);
    rig.app.session().setCellBars(1, 0, 2);
    CHECK(rig.project().tracks[1].bars[0] == 2 && samePlay(rig.pattern(0, 1).chords[0].play, c.play));
    heard = rig.playBar(2L * kBar - 2000);
    CHECK(static_cast<int>(heard.size()) == 2 * n && rig.app.session().engine.dropped() == 0);
    for (std::size_t k = 0; k < heard.size(); ++k) {
        const std::size_t key = k % static_cast<std::size_t>(n), bar = k / static_cast<std::size_t>(n);
        CHECK(heard[k].pitch == keys[key] && heard[k].frame == static_cast<long>(bar) * kBar + 2 * kStep + static_cast<long>(key) * kTight);
    }
}

// ---- the song file --------------------------------------------------------------------------------------

int count(const std::string& text, const std::string& word) {
    int n = 0;
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) ++n;
    return n;
}

void testTheSongFile() {
    std::unique_ptr<Project> song(new Project);
    song->tracks[1].kind = TrackKind::Qmod;
    NotePattern& p = song->tracks[1].notes[0][0];
    p.chords[0] = makeChord(builtInPack().chords[0], 60, 2, 3.f, 0.7f, 0);
    p.chords[1] = makeChord(builtInPack().chords[1], 62, 8, 3.f, 0.7f, 0);
    // No effect set: not a word about them, and the file is what it was before there were any.
    const std::string plain = songText(*song);
    CHECK(count(plain, "chordplay") == 0 && count(plain, "chord ") == 2);
    p.chords[0].ratchet = 3;
    p.chords[0].throwTo = 2;
    CHECK(count(songText(*song), "chordplay") == 0 && count(songText(*song), "chordfx 3 3 2\n") == 1);
    p.chords[0].ratchet = 0;
    p.chords[0].throwTo = 0;

    // Set: a line of its own, after the chord's.
    p.chords[1].play = chordPlayOf(kStrumWide, kOrderRandom, 25, 10, 100);
    std::string text = songText(*song);
    CHECK(count(text, "chordplay") == 1 && count(text, "chordplay 9 2 3 25 10 100\n") == 1);
    CHECK(text.find("chordplay") > text.find("chord 9 "));
    CHECK(count(text, "chordfx") == 0); // the old line is not written for it
    // Each of the five alone is enough to write the line, ORDER too (it is the player's setting).
    for (int which = 0; which < 5; ++which) {
        p.chords[0].play = chordPlayOf(which == 0 ? 1 : 0, which == 1 ? 1 : 0, which == 2 ? 10 : 0, which == 3 ? 10 : 0, which == 4 ? 10 : 0);
        CHECK(count(songText(*song), "chordplay 3 ") == 1);
    }
    p.chords[0].play = chordPlayOf(kStrumHalf, kOrderUpDown, 0, 0, 50);
    p.chords[0].ratchet = 2;
    p.chords[0].throwTo = 1;
    text = songText(*song);
    CHECK(count(text, "chordfx 3 2 1\nchordplay 3 4 2 0 0 50\n") == 1);

    // And back.
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    const NotePattern& q = back->tracks[1].notes[0][0];
    for (int i = 0; i < 2; ++i) {
        CHECK(q.chords[i].velocity > 0.f && samePlay(q.chords[i].play, p.chords[i].play));
        CHECK(q.chords[i].ratchet == p.chords[i].ratchet && q.chords[i].throwTo == p.chords[i].throwTo && q.chords[i].step == p.chords[i].step);
    }
    // (and what is written from what was read reads back the same again)
    const std::string again = songText(*back);
    std::unique_ptr<Project> twice(new Project);
    CHECK(parseSong(again, *twice) && songText(*twice) == again && count(again, "chordfx 3 2 1\nchordplay 3 4 2 0 0 50\n") == 1 && count(again, "chordplay 9 2 3 25 10 100\n") == 1);

    // What a file may say. Each line is for the chord that starts on its step, in its bar.
    const auto read = [&](const std::string& line) {
        std::string with = plain;
        const std::size_t at = with.find("chord 9 ");
        const std::size_t end = with.find('\n', at);
        with.insert(end + 1, line + "\n");
        std::unique_ptr<Project> loaded(new Project);
        CHECK(parseSong(with, *loaded));
        CHECK(loaded->tracks[1].notes[0][0].chords[0].velocity > 0.f && loaded->tracks[1].notes[0][0].chords[1].velocity > 0.f);
        CHECK(samePlay(loaded->tracks[1].notes[0][0].chords[0].play, ChordPlay())); // the other chord is not touched
        return loaded->tracks[1].notes[0][0].chords[1].play;
    };
    CHECK(samePlay(read("chordplay 9 1 2 10 25 50"), chordPlayOf(1, 2, 10, 25, 50)));
    CHECK(samePlay(read("chordplay 9 2"), chordPlayOf(2, 0, 0, 0, 0)));           // numbers left off are 0
    CHECK(samePlay(read("chordplay 9 0 0 0 0 30"), chordPlayOf(0, 0, 0, 0, 30))); // an amount between the clicks is kept
    CHECK(samePlay(read("chordplay 9 6 4 101 5000 1e12"), chordPlayOf(0, 0, 100, 100, 100))); // not known: off. Too much: the most
    CHECK(samePlay(read("chordplay 9 -1 -1 -5 -5 -5"), ChordPlay()));
    CHECK(samePlay(read("chordplay 9 5 3 100 100 100 7 7 7"), chordPlayOf(5, 3, 100, 100, 100))); // more numbers than there are: left
    CHECK(samePlay(read("chordplay 9 x 3 10"), ChordPlay()));       // not a number: that and the rest are 0
    CHECK(samePlay(read("chordplay 9 1 x 10"), chordPlayOf(1, 0, 0, 0, 0)));
    CHECK(samePlay(read("chordplay 9 nan 1 1 1 1"), ChordPlay()));
    CHECK(samePlay(read("chordplay 9"), ChordPlay()));
    CHECK(samePlay(read("chordplay"), ChordPlay()));
    CHECK(samePlay(read("chordplay 0 1 1 10 10 10"), ChordPlay()));  // no such step
    CHECK(samePlay(read("chordplay 17 1 1 10 10 10"), ChordPlay()));
    CHECK(samePlay(read("chordplay 5 1 1 10 10 10"), ChordPlay()));  // no chord starts there
    CHECK(samePlay(read("chordplay 10 1 1 10 10 10"), ChordPlay())); // (the chord sounds on step 10; it starts on 9)
    CHECK(samePlay(read("chordplay 9 1.9 1.9 10.9 0.9 99.9"), chordPlayOf(1, 1, 10, 0, 99)));
    // A program from before chord effects skips the line whole (it does not know the word), and
    // so does this one for a word IT does not know: the chord beside it is as it was.
    CHECK(samePlay(read("chordsomething 9 1 1 10 10 10"), ChordPlay()));
    // Before its chord, the line finds none.
    {
        std::string with = plain;
        with.insert(with.find("chord 9 "), "chordplay 9 1 1 10 10 10\n");
        std::unique_ptr<Project> loaded(new Project);
        CHECK(parseSong(with, *loaded) && samePlay(loaded->tracks[1].notes[0][0].chords[1].play, ChordPlay()) && loaded->tracks[1].notes[0][0].chords[1].velocity > 0.f);
    }
    // A loose note takes none (the line is a chord's).
    {
        std::unique_ptr<Project> notes(new Project);
        notes->tracks[1].kind = TrackKind::Qmod;
        notes->tracks[1].notes[0][0].notes[0] = NoteEvent{8, 62, 1.f, 0.f, 0.8f};
        std::string with = songText(*notes);
        with += "chordplay 9 1 1 10 10 10\n";
        std::unique_ptr<Project> loaded(new Project);
        CHECK(parseSong(with, *loaded) && loaded->tracks[1].notes[0][0].notes[0].velocity > 0.f && samePlay(loaded->tracks[1].notes[0][0].notes[0].play, ChordPlay()));
    }
}

// ---- undo, swap, break, drag, scenes -------------------------------------------------------------------

void testUndo() {
    Rig rig;
    rig.roll();
    Session& session = rig.app.session();
    NoteScreen& roll = rig.app.notes();
    const auto settle = [&] { rig.frames(50); };
    ChordEvent& c = rig.chord(0, 60, 2);
    session.noteEdit();
    settle();
    const int steps = session.undoSteps();
    rig.key(X, true);
    roll.pickEffect(kFxStrum);
    rig.tap(RIGHT, 2);
    rig.tap(DOWN, 2);
    rig.tap(RIGHT);
    rig.key(X, false);
    settle();
    CHECK(c.play.strum == kStrumWide && c.play.time == 10 && session.undoSteps() == steps + 1);
    rig.key(X, true);
    rig.tap(Y); // the dice: a step of their own
    rig.key(X, false);
    const ChordPlay rolled = c.play;
    const int rolledRatchet = c.ratchet;
    settle();
    CHECK(session.undoSteps() == steps + 2);
    CHECK(session.undo());
    CHECK(c.velocity > 0.f && samePlay(c.play, chordPlayOf(kStrumWide, 0, 10, 0, 0)) && c.ratchet == 0);
    CHECK(session.undo());
    CHECK(c.velocity > 0.f && samePlay(c.play, ChordPlay()));
    CHECK(session.redo());
    CHECK(samePlay(c.play, chordPlayOf(kStrumWide, 0, 10, 0, 0)));
    CHECK(session.redo());
    CHECK(samePlay(c.play, rolled) && c.ratchet == rolledRatchet);
}

void testSwapBreakAndDrag() {
    Rig rig;
    rig.roll();
    rig.listen();
    NoteScreen& roll = rig.app.notes();
    ChordEvent& c = rig.chord(0, 60, 2);
    const ChordPlay set = chordPlayOf(kStrumTight, kOrderDown, 25, 10, 50);
    c.play = set;
    c.ratchet = 2;
    c.throwTo = 1;
    rig.app.session().sendNoteBar(1, 0, 0);
    // Swapped for another chord (hold Y on it, left / right): what it does stays.
    const std::string was = c.name;
    rig.key(Y, true);
    rig.tap(RIGHT);
    rig.key(Y, false);
    CHECK(was != c.name && c.velocity > 0.f && c.step == 2);
    CHECK(samePlay(c.play, set) && c.ratchet == 2 && c.throwTo == 1);
    // Dragged (hold R1 and move): it goes with it, to another step and to another key.
    int keys[kChordNotes];
    c.pitches(keys);
    const int root = c.root;
    roll.moveCursorTo(keys[0], 2); // (the other chord's keys are other keys)
    rig.key(R1, true);
    rig.tap(DOWN, 3);
    rig.tap(RIGHT, 2);
    rig.key(R1, false);
    CHECK(c.step == 5 && c.root == root + 2 && rig.pattern().used() == c.pitches(keys) && samePlay(c.play, set) && c.ratchet == 2 && c.throwTo == 1);
    // Broken into notes (hold Y, B): the notes are plain notes. A strum is the chord's; they keep
    // its ratchet, its THROW and its nudge, as they always did, and nothing of the five.
    const int n = c.pitches(keys);
    c.play = chordPlayOf(kStrumWide, 0, 0, 0, 0);
    c.ratchet = 0;
    rig.app.session().sendNoteBar(1, 0, 0);
    std::vector<Began> heard = rig.playBar();
    CHECK(static_cast<int>(heard.size()) == n && heard.back().frame > heard.front().frame); // strummed
    rig.key(Y, true);
    rig.tap(B);
    rig.key(Y, false);
    NotePattern& p = rig.pattern();
    CHECK(p.chords[0].velocity <= 0.f && p.used() == n);
    int found = 0;
    for (const NoteEvent& note : p.notes) {
        if (note.velocity <= 0.f) continue;
        ++found;
        CHECK(note.step == 5 && note.throwTo == 1 && samePlay(note.play, ChordPlay()));
    }
    CHECK(found == n);
    heard = rig.playBar();
    CHECK(static_cast<int>(heard.size()) == n);
    for (const Began& b : heard) CHECK(b.frame == 5 * kStep); // all at once now
}

void testScenes() {
    Rig rig;
    rig.roll();
    rig.listen();
    Session& session = rig.app.session();
    ChordEvent& c = rig.chord(0, 60, 2);
    const ChordPlay set = chordPlayOf(kStrumTight, kOrderDown, 0, 0, 0);
    c.play = set;
    session.sendNoteBar(1, 0, 0);
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    // NEXT SCENE: the copy has them, and so has the scene left behind.
    rig.frames(2);
    rig.render(2000);
    CHECK(session.nextScene() == 1);
    rig.frames(30);
    rig.render(4000);
    CHECK(rig.project().scene == 1 && samePlay(rig.pattern(1).chords[0].play, set) && samePlay(rig.pattern(0).chords[0].play, set));
    // Taken back and made again: one scene, then two, the chord as it was each time.
    CHECK(session.undo());
    rig.frames(30);
    rig.render(4000);
    CHECK(rig.project().sceneCount() == 1 && samePlay(rig.pattern(0).chords[0].play, set));
    CHECK(session.redo());
    rig.frames(30);
    rig.render(4000);
    CHECK(rig.project().sceneCount() == 2 && samePlay(rig.pattern(1).chords[0].play, set));
    // Scene 1's are taken off, so only the copy can strum; the song plays through both.
    rig.pattern(0).chords[0].play = ChordPlay();
    session.sendNotePattern(1, 0);
    rig.project().scene = 0;
    std::vector<Began> heard = rig.playBar(2L * kBar - 2000, true);
    CHECK(static_cast<int>(heard.size()) == 2 * n);
    for (std::size_t k = 0; k < heard.size() && static_cast<int>(heard.size()) == 2 * n; ++k) {
        const std::size_t un = static_cast<std::size_t>(n);
        if (k < un) CHECK(heard[k].frame == 2 * kStep);
        else CHECK(heard[k].frame == kBar + 2 * kStep + static_cast<long>(k - un) * kTight && heard[k].pitch == keys[2 * un - 1 - k]);
    }
    // A cell copied on the SONG screen (hold R1 and move) takes them too.
    rig.pattern(0).chords[0].play = chordPlayOf(kStrumHalf, kOrderRandom, 10, 25, 50);
    session.sendNotePattern(1, 0);
    session.view = View::Song;
    rig.app.song().moveCursorTo(0, 1);
    rig.key(R1, true);
    rig.tap(DOWN, 2);
    rig.key(R1, false);
    CHECK(rig.pattern(2).chords[0].velocity > 0.f && samePlay(rig.pattern(2).chords[0].play, chordPlayOf(kStrumHalf, kOrderRandom, 10, 25, 50)));
}

// ---- what is drawn -----------------------------------------------------------------------------------------

// The strip and the hints with X held on a chord, and the chord's mark on the roll: read off
// the drawn screen (a software renderer and the program's own lettering).
void testTheScreen() {
    Rig rig;
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    rig.roll();
    NoteScreen& roll = rig.app.notes();
    ChordEvent& c = rig.chord(0, 60, 2);
    const auto shot = [&] {
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
        return canvas.all();
    };
    const auto differ = [&](const std::vector<Uint32>& a, const std::vector<Uint32>& b, int x0, int y0, int x1, int y1) {
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) n += a[static_cast<std::size_t>(y * 640 + x)] != b[static_cast<std::size_t>(y * 640 + x)];
        return n;
    };
    const int stripTop = 400, hintsTop = 456; // (the strip's rows and the hints bar, as the stills show them)
    // The mark: a chord with a chord effect set has a light corner on each key; without, none.
    const std::vector<Uint32> plain = shot();
    c.play.time = 10;
    const std::vector<Uint32> marked = shot();
    int light = 0, other = 0;
    for (int y = 0; y < stripTop; ++y)
        for (int x = 0; x < 640; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * 640 + x);
            if (marked[i] == plain[i]) continue;
            ++(marked[i] == Canvas::colour(ui::kText) ? light : other);
        }
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    CHECK(light >= 20 * n && light <= 60 * n); // a corner ten across on each of its keys
    CHECK(other > 0 && other <= 12 * n);       // ...and the dark edge beside it
    CHECK(differ(marked, plain, 0, stripTop, 640, 480) > 0); // the strip says so too ("EFFECTS")
    // A ratchet's mark is another thing: lines and a purple edge, no light corner.
    c.play = ChordPlay();
    c.ratchet = 3;
    const std::vector<Uint32> ratcheted = shot();
    int lightToo = 0;
    for (int y = 0; y < stripTop; ++y)
        for (int x = 0; x < 640; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * 640 + x);
            lightToo += ratcheted[i] != plain[i] && ratcheted[i] == Canvas::colour(ui::kText);
        }
    CHECK(lightToo == 0 && differ(ratcheted, plain, 0, 0, 640, stripTop) > 0);
    c.ratchet = 0;

    // X held: the strip is the six, and each one picked reads differently; nothing is drawn
    // past the right margin, at its fullest.
    c.play = chordPlayOf(kStrumWide, kOrderUpDown, 100, 100, 100);
    c.ratchet = 4;
    c.throwTo = 2; // SPACE: THROW's widest
    const std::vector<Uint32> letGo = shot();
    rig.key(X, true);
    std::vector<std::vector<Uint32>> strips;
    for (int effect = 0; effect < kChordEffects; ++effect) {
        roll.pickEffect(effect);
        strips.push_back(shot());
        CHECK(differ(strips.back(), letGo, 0, stripTop, 640, hintsTop) > 200);
        for (int before = 0; before < effect; ++before) CHECK(differ(strips[static_cast<std::size_t>(before)], strips.back(), 0, stripTop, 640, hintsTop) > 40);
        // The marker: a brass bar left of the picked row, in its column.
        const int x = static_cast<int>(kChordFxListX + (effect / 3) * kChordFxColumn) - 8, y = 400 + kChordFxRowTop + (effect % 3) * kChordFxRowH;
        int brass = 0;
        for (int row = y - 3; row < y + 16; ++row)
            for (int col = x; col < x + 3; ++col) brass += canvas.is(col, row, ui::kBrass);
        CHECK(brass >= 20);
        // The margin: nothing in the strip from 633 on.
        int past = 0;
        for (int row = stripTop; row < hintsTop; ++row)
            for (int col = 633; col < 640; ++col) past += !canvas.is(col, row, ui::kBg) && strips.back()[static_cast<std::size_t>(row * 640 + col)] != plain[static_cast<std::size_t>(row * 640 + col)];
        CHECK(past == 0);
    }
    // The hints with X held on a chord are not a note's, and all four are there: the bar's last
    // key (Y, the dice) is drawn, with room to spare before MENU: HELP.
    const std::vector<Uint32> held = strips.back();
    CHECK(differ(held, letGo, 0, hintsTop, 640, 480) > 100);
    int lastInk = 0;
    for (int x = 0; x < 520; ++x)
        for (int y = hintsTop + 4; y < 478; ++y)
            if (held[static_cast<std::size_t>(y * 640 + x)] == Canvas::colour(ui::kText)) lastInk = x;
    CHECK(lastInk > 240 && lastInk < 420);
    rig.key(X, false);
    // On a plain note the hints are the note's own.
    roll.moveCursorTo(80, 9);
    rig.tap(A);
    rig.key(X, true);
    const std::vector<Uint32> onNote = shot();
    CHECK(differ(onNote, held, 0, hintsTop, 640, 480) > 100);
    rig.key(X, false);
}

} // namespace

int main(int argc, char** argv) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Named { const char* name; void (*run)(); };
    const Named all[] = {
        {"testTheList", testTheList}, {"testTheLine", testTheLine}, {"testTheDice", testTheDice}, {"testTheGesture", testTheGesture},
        {"testThroughTheSession", testThroughTheSession}, {"testTheSongFile", testTheSongFile}, {"testUndo", testUndo},
        {"testSwapBreakAndDrag", testSwapBreakAndDrag}, {"testScenes", testScenes}, {"testTheScreen", testTheScreen},
    };
    for (const Named& t : all) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], t.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        t.run();
        if (failures != before) std::printf("  (in %s)\n", t.name);
    }
    if (failures == 0) std::printf("chordfxapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
