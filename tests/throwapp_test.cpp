// THROW in the app: the gesture (hold X, press A) on the drum grid and the
// piano roll, the song file, undo, copying, and a bounce. The engine's side
// of it is tests/throw_test.cpp.
//
// Whether the engine was told is read off the buses: a new song has TAPE on
// ECHO and HALL on SPACE and every send at 0, so a bus is awake only if
// something was thrown to it.

#include <SDL.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "chords.hpp"
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
    std::vector<float> render(int audioFrames) {
        std::vector<float> out(static_cast<std::size_t>(2 * audioFrames));
        engine->render(out.data(), audioFrames);
        return out;
    }
    Project& project() { return app.project(); }
    // Which buses something is thrown to when the song plays a bar: bit 0 ECHO, bit 1 SPACE.
    // The song is stopped again and the buses put to rest, so it can be asked again.
    int thrownTo() {
        frames(2);
        render(2048);
        engine->prepare(48000.f); // the buses at rest, whatever rang before
        app.session().togglePlay(false);
        frames(1);
        render(kBar);
        const int mask = (engine->busAwake(0) ? 1 : 0) + (engine->busAwake(1) ? 2 : 0);
        app.session().togglePlay(false);
        frames(1);
        render(2048);
        return mask;
    }
};

// Keyboard stand-ins (see app/input.cpp).
const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a;
const SDL_Keycode R1 = SDLK_w;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;

int hitsOn(Rig& rig) {
    int n = 0;
    for (int l = 0; l < kDrumLanes; ++l)
        for (int s = 0; s < kSteps; ++s) n += rig.app.drums().level(l, s) > 0.f ? 1 : 0;
    return n;
}

// ---- the drum grid -----------------------------------------------------------------------------

void testTheDrumGrid() {
    Rig rig;
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    drums.moveCursorTo(1, 4);
    // A alone places a hit, as ever.
    rig.tap(A);
    CHECK(drums.level(1, 4) > 0.f && hitsOn(rig) == 1 && drums.fx(1, 4).throwTo == 0);
    const float level = drums.level(1, 4);
    CHECK(rig.thrownTo() == 0);
    // X held on it, A: THROW goes round. OFF, ECHO, SPACE, BOTH, and OFF again.
    rig.key(X, true);
    CHECK(drums.effecting());
    for (int expected : {1, 2, 3, 0, 1, 2}) {
        rig.tap(A);
        CHECK(drums.fx(1, 4).throwTo == expected);
    }
    // It placed nothing, shaped nothing, and the cursor did not move.
    CHECK(hitsOn(rig) == 1 && drums.level(1, 4) == level && drums.cursorLane() == 1 && drums.cursorStep() == 4 && drums.effecting());
    // A held (with X) and a direction: still the hit's pitch, not its level.
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(drums.fx(1, 4).pitch == 1 && drums.level(1, 4) == level);
    CHECK(drums.fx(1, 4).throwTo == 3); // (that press of A was a step of THROW itself)
    rig.tap(DOWN);
    rig.tap(A, 3); // round to SPACE
    CHECK(drums.fx(1, 4).throwTo == 2);
    // The other step effects leave it alone, and it them.
    rig.tap(R1);
    rig.tap(RIGHT);
    CHECK(drums.fx(1, 4).throwTo == 2 && drums.fx(1, 4).cut == 1 && drums.fx(1, 4).hits() == 2);
    rig.tap(A);
    CHECK(drums.fx(1, 4).throwTo == 3 && drums.fx(1, 4).cut == 1 && drums.fx(1, 4).hits() == 2);
    rig.tap(A, 3);
    rig.key(X, false);
    CHECK(!drums.effecting() && drums.fx(1, 4).throwTo == 2);
    // The engine has it: SPACE wakes when the bar plays, and ECHO does not.
    CHECK(rig.thrownTo() == 2);
    // With X let go, A is itself again: on an empty step it places.
    drums.moveCursorTo(1, 8);
    rig.tap(A);
    CHECK(hitsOn(rig) == 2 && drums.fx(1, 8).throwTo == 0);
    // X on an empty step is only "hear": A then places, and nothing is thrown.
    drums.moveCursorTo(3, 2);
    rig.key(X, true);
    CHECK(!drums.effecting());
    rig.tap(A);
    rig.key(X, false);
    CHECK(drums.level(3, 2) > 0.f && drums.fx(3, 2).throwTo == 0 && hitsOn(rig) == 3);

    // The dice (hold X + Y) do not throw, and do not take a THROW away.
    drums.moveCursorTo(1, 8);
    rig.key(X, true);
    for (int roll = 0; roll < 60; ++roll) {
        rig.tap(Y);
        CHECK(drums.fx(1, 8).throwTo == 0);
        CHECK(drums.fx(1, 8).pitch != 0 || drums.fx(1, 8).hits() > 1 || drums.fx(1, 8).reverse || drums.fx(1, 8).cut); // (each roll is still something)
    }
    rig.key(X, false);
    drums.moveCursorTo(1, 4);
    rig.key(X, true);
    for (int roll = 0; roll < 60; ++roll) {
        rig.tap(Y);
        CHECK(drums.fx(1, 4).throwTo == 2);
    }
    rig.key(X, false);
    CHECK(rig.thrownTo() == 2);

    // A hit deleted leaves no THROW behind for the next one placed there.
    rig.tap(B);
    CHECK(drums.level(1, 4) == 0.f && drums.fx(1, 4).throwTo == 0);
    rig.tap(A);
    CHECK(drums.level(1, 4) > 0.f && drums.fx(1, 4).throwTo == 0);
    CHECK(rig.thrownTo() == 0);

    // setFx holds a THROW to what there is, and tells the engine when only the THROW changed.
    StepFx fx;
    fx.throwTo = 1;
    drums.setFx(1, 4, fx);
    CHECK(rig.thrownTo() == 1);
    fx.throwTo = 4; // past the last: off
    drums.setFx(1, 4, fx);
    CHECK(drums.fx(1, 4).throwTo == 0 && rig.thrownTo() == 0);
    fx.throwTo = 200;
    drums.setFx(1, 4, fx);
    CHECK(drums.fx(1, 4).throwTo == 0);

    // A muted track throws nothing.
    fx.throwTo = 3;
    drums.setFx(1, 4, fx);
    CHECK(rig.thrownTo() == 3);
    rig.app.session().setMuted(0, true);
    CHECK(rig.thrownTo() == 0);
    rig.app.session().setMuted(0, false);
    CHECK(rig.thrownTo() == 3);

    // Heard by hand while X is held (the song stopped): the hit is thrown as the pattern would throw it.
    rig.frames(2);
    rig.render(2048);
    rig.engine->prepare(48000.f);
    rig.key(X, true);
    rig.frames(1);
    rig.render(4096);
    rig.key(X, false);
    CHECK(rig.engine->busAwake(0) && rig.engine->busAwake(1));
}

// ---- the piano roll ----------------------------------------------------------------------------

void testThePianoRoll() {
    Rig rig;
    Session& session = rig.app.session();
    session.addTrack(TrackKind::Phazed);
    rig.project().track = 1;
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    roll.moveCursorTo(60, 4);
    rig.tap(A); // A alone places a note
    const int slot = p.at(60, 4);
    CHECK(slot >= 0 && p.used() == 1 && p.notes[slot].throwTo == 0);
    if (slot < 0) return;
    const float level = p.notes[slot].velocity, length = p.notes[slot].length;
    CHECK(rig.thrownTo() == 0);
    // X held on it, A: THROW goes round.
    rig.key(X, true);
    CHECK(roll.effecting());
    for (int expected : {1, 2, 3, 0, 1}) {
        rig.tap(A);
        CHECK(p.notes[slot].throwTo == expected);
    }
    CHECK(p.used() == 1 && p.notes[slot].velocity == level && p.notes[slot].length == length && roll.cursorPitch() == 60 && roll.cursorStep() == 4);
    // A held with X, and a direction: the ratchet's click, not the note's level or length.
    rig.key(A, true);
    rig.tap(RIGHT);
    rig.key(A, false);
    CHECK(p.notes[slot].ratchet == 2 && p.notes[slot].velocity == level && p.notes[slot].length == length);
    CHECK(p.notes[slot].throwTo == 2); // (that press of A was a step of THROW itself)
    rig.tap(UP);
    CHECK(p.notes[slot].glide == 1 && p.notes[slot].throwTo == 2);
    rig.key(X, false);
    CHECK(rig.thrownTo() == 2);
    // The dice leave it be, both ways.
    rig.key(X, true);
    for (int roll_ = 0; roll_ < 40; ++roll_) {
        rig.tap(Y);
        CHECK(p.notes[slot].throwTo == 2);
    }
    rig.tap(A, 2);
    for (int roll_ = 0; roll_ < 40; ++roll_) {
        rig.tap(Y);
        CHECK(p.notes[slot].throwTo == 0);
    }
    rig.tap(A, 2);
    rig.key(X, false);
    CHECK(p.notes[slot].throwTo == 2 && rig.thrownTo() == 2);
    // With X let go A places again; X on an empty step holds nothing.
    roll.moveCursorTo(64, 9);
    rig.key(X, true);
    CHECK(!roll.effecting());
    rig.tap(A);
    rig.key(X, false);
    CHECK(p.used() == 2 && p.notes[p.at(64, 9)].throwTo == 0);

    // Carried (hold R1) to another step and key, it keeps its THROW.
    roll.moveCursorTo(60, 4);
    rig.key(R1, true);
    rig.tap(DOWN, 2);
    rig.tap(RIGHT);
    rig.key(R1, false);
    const int moved = p.at(61, 6);
    CHECK(moved >= 0 && p.at(60, 4) < 0);
    if (moved >= 0) CHECK(p.notes[moved].throwTo == 2);
    CHECK(rig.thrownTo() == 2);
    // A muted track's thrown note throws nothing.
    session.setMuted(1, true);
    CHECK(rig.thrownTo() == 0);
    session.setMuted(1, false);

    // A chord takes THROW as a note does: every key of it.
    p = NotePattern();
    p.chords[0] = makeChord(builtInPack().chords[0], 60, 8, 2.f, 0.7f, 0);
    session.sendNotePattern(1, 0);
    CHECK(rig.thrownTo() == 0);
    roll.moveCursorTo(60, 8);
    rig.key(X, true);
    CHECK(roll.effecting());
    for (int expected : {1, 2, 3, 0, 1, 2, 3}) {
        rig.tap(A);
        CHECK(p.chords[0].throwTo == expected);
    }
    rig.key(X, false);
    CHECK(p.used() == p.chords[0].count && p.chords[0].velocity == 0.7f); // nothing placed, nothing shaped
    CHECK(rig.thrownTo() == 3);
    NoteEvent played[kMaxNotes];
    const int sounding = p.render(played);
    CHECK(sounding == p.chords[0].count);
    for (const NoteEvent& n : played)
        if (n.velocity > 0.f) CHECK(n.throwTo == 3);
    // Swapped for another chord (hold Y, right), it is still thrown...
    rig.key(Y, true);
    rig.tap(RIGHT);
    rig.key(Y, false);
    CHECK(p.chords[0].velocity > 0.f && p.chords[0].throwTo == 3);
    // ...and broken into notes (hold Y, B), each note is.
    rig.key(Y, true);
    rig.tap(B);
    rig.key(Y, false);
    CHECK(p.chords[0].velocity == 0.f && p.used() >= 3);
    for (const NoteEvent& n : p.notes)
        if (n.velocity > 0.f) CHECK(n.throwTo == 3);
    CHECK(rig.thrownTo() == 3);
}

// ---- the song file, undo, copying -------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.app.session();
    PatternScreen& drums = rig.app.drums();
    session.addTrack(TrackKind::Phazed);
    rig.frames(2);
    // Hits: one plain, one thrown to each, one thrown with other effects.
    drums.setStep(0, 0, 0.9f);
    for (int to = 1; to <= 3; ++to) {
        drums.setStep(1, 4 * to, 0.8f);
        StepFx fx;
        fx.throwTo = static_cast<std::uint8_t>(to);
        if (to == 3) fx.cut = 2, fx.ratchet = 3;
        drums.setFx(1, 4 * to, fx);
    }
    StepFx cut;
    cut.cut = 1;
    drums.setStep(2, 2, 0.7f);
    drums.setFx(2, 2, cut);
    // Notes: plain, nudged, thrown, thrown and nudged. A chord thrown, one ratcheted, one both.
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    p.notes[0] = NoteEvent{0, 60, 1.f, 0.f, 0.8f};
    p.notes[1] = NoteEvent{2, 62, 1.f, 0.25f, 0.8f};
    p.notes[2] = NoteEvent{4, 64, 2.f, 0.f, 0.8f};
    p.notes[2].throwTo = 1;
    p.notes[3] = NoteEvent{7, 65, 1.f, -0.125f, 0.8f};
    p.notes[3].throwTo = 3;
    p.notes[3].ratchet = 2;
    p.chords[0] = makeChord(builtInPack().chords[0], 48, 9, 1.f, 0.7f, 0);
    p.chords[0].throwTo = 2;
    p.chords[1] = makeChord(builtInPack().chords[0], 48, 11, 1.f, 0.7f, 0);
    p.chords[1].ratchet = 3;
    p.chords[2] = makeChord(builtInPack().chords[0], 48, 13, 1.f, 0.7f, 0);
    p.chords[2].ratchet = 2;
    p.chords[2].throwTo = 1;
    session.sendNotePattern(1, 0);

    std::string text = songText(rig.project());
    const auto has = [&](const char* line) { return text.find(std::string("\n") + line + "\n") != std::string::npos; };
    // THROW is the seventh number of a stepfx line, left off at 0: an unthrown hit's line is as it always was.
    CHECK(has("stepfx 2 5 0 1 0 0 1") && has("stepfx 2 9 0 1 0 0 2") && has("stepfx 2 13 0 3 0 2 3"));
    CHECK(has("stepfx 3 3 0 1 0 1") && text.find("stepfx 1 1") == std::string::npos);
    // The sixth number of a note line, with the nudge written before it even at 0. Unthrown notes as they always were.
    CHECK(has("note 1 60 1 0.8") && has("note 3 62 1 0.8 0.25") && has("note 5 64 2 0.8 0 1") && has("note 8 65 1 0.8 -0.125 3"));
    CHECK(has("notefx 8 65 2 0")); // (the ratchet and the glide keep their own line)
    // A chord's: after its ratchet on its chordfx line. A chord with only a ratchet has the line it always had.
    CHECK(has("chordfx 10 1 2") && has("chordfx 12 3") && has("chordfx 14 2 1"));

    // Read back, it is the same song; written again, the same text.
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    const DrumPattern& d = back->tracks[0].drums[0][0];
    CHECK(d.fx[1][4].throwTo == 1 && d.fx[1][8].throwTo == 2 && d.fx[1][12].throwTo == 3 && d.fx[1][12].cut == 2 && d.fx[1][12].ratchet == 3);
    CHECK(d.fx[2][2].throwTo == 0 && d.fx[2][2].cut == 1 && d.fx[0][0].throwTo == 0);
    const NotePattern& n = back->tracks[1].notes[0][0];
    const auto noteAt = [&](int pitch, int step) { const int i = n.at(pitch, step); return i >= 0 ? n.notes[i] : NoteEvent(); };
    CHECK(noteAt(60, 0).throwTo == 0 && noteAt(62, 2).throwTo == 0 && noteAt(62, 2).nudge == 0.25f);
    CHECK(noteAt(64, 4).throwTo == 1 && noteAt(64, 4).nudge == 0.f && noteAt(65, 7).throwTo == 3 && noteAt(65, 7).nudge == -0.125f && noteAt(65, 7).ratchet == 2);
    const auto chordOn = [&](int step) { const int i = n.chordAt(step); return i >= 0 ? n.chords[i] : ChordEvent(); };
    CHECK(chordOn(9).throwTo == 2 && chordOn(9).ratchet == 0 && chordOn(11).throwTo == 0 && chordOn(11).ratchet == 3 && chordOn(13).throwTo == 1 && chordOn(13).ratchet == 2);

    // An old song, with none of the new numbers: nothing is thrown, and the rest is as it was.
    {
        std::string old = text;
        const auto swap = [&](const char* from, const char* to) {
            const std::size_t at = old.find(from);
            CHECK(at != std::string::npos);
            if (at != std::string::npos) old.replace(at, std::strlen(from), to);
        };
        swap("stepfx 2 5 0 1 0 0 1\n", "");
        swap("stepfx 2 9 0 1 0 0 2\n", "");
        swap("stepfx 2 13 0 3 0 2 3", "stepfx 2 13 0 3 0 2");
        swap("note 5 64 2 0.8 0 1", "note 5 64 2 0.8");
        swap("note 8 65 1 0.8 -0.125 3", "note 8 65 1 0.8 -0.125");
        swap("chordfx 10 1 2\n", "");
        swap("chordfx 14 2 1", "chordfx 14 2");
        std::unique_ptr<Project> was(new Project);
        CHECK(parseSong(old, *was));
        const DrumPattern& wd = was->tracks[0].drums[0][0];
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = 0; step < kSteps; ++step) CHECK(wd.fx[lane][step].throwTo == 0);
        CHECK(wd.fx[1][12].cut == 2 && wd.fx[1][12].ratchet == 3 && wd.level[1][4] > 0.f);
        const NotePattern& wn = was->tracks[1].notes[0][0];
        for (const NoteEvent& note : wn.notes) CHECK(note.throwTo == 0);
        for (const ChordEvent& chord : wn.chords) CHECK(chord.throwTo == 0);
        CHECK(wn.used() == n.used() && wn.chords[2].ratchet == 2 && wn.at(65, 7) >= 0 && wn.notes[wn.at(65, 7)].nudge == -0.125f);
        CHECK(songText(*was) == old); // and saved again it gains nothing
    }
    // Nonsense for a THROW is no THROW; the rest of the line still counts.
    {
        std::string odd = text;
        const auto swap = [&](const char* from, const char* to) {
            const std::size_t at = odd.find(from);
            CHECK(at != std::string::npos);
            if (at != std::string::npos) odd.replace(at, std::strlen(from), to);
        };
        swap("stepfx 2 5 0 1 0 0 1", "stepfx 2 5 0 1 0 0 4");
        swap("stepfx 2 9 0 1 0 0 2", "stepfx 2 9 0 1 0 0 -1");
        swap("stepfx 2 13 0 3 0 2 3", "stepfx 2 13 0 3 0 2 x");
        swap("note 5 64 2 0.8 0 1", "note 5 64 2 0.8 0 9");
        swap("note 8 65 1 0.8 -0.125 3", "note 8 65 1 0.8 -0.125 0.5");
        swap("chordfx 10 1 2", "chordfx 10 1 77");
        swap("chordfx 14 2 1", "chordfx 14 2 -3");
        std::unique_ptr<Project> held(new Project);
        CHECK(parseSong(odd, *held));
        const DrumPattern& hd = held->tracks[0].drums[0][0];
        CHECK(hd.fx[1][4].throwTo == 0 && hd.fx[1][8].throwTo == 0 && hd.fx[1][12].throwTo == 0 && hd.fx[1][12].cut == 2 && hd.fx[1][12].ratchet == 3);
        const NotePattern& hn = held->tracks[1].notes[0][0];
        for (const NoteEvent& note : hn.notes) CHECK(note.throwTo == 0);
        CHECK(hn.used() == n.used() && hn.chords[0].throwTo == 0 && hn.chords[2].throwTo == 0 && hn.chords[2].ratchet == 2);
        CHECK(hn.at(65, 7) >= 0 && hn.notes[hn.at(65, 7)].nudge == -0.125f);
    }
}

void testUndo() {
    Rig rig;
    Session& session = rig.app.session();
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    const auto settle = [&] { rig.frames(50); }; // the burst of edits is over
    drums.moveCursorTo(1, 4);
    rig.tap(A);
    settle();
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.tap(A); // ECHO
    rig.key(X, false);
    settle();
    CHECK(drums.fx(1, 4).throwTo == 1 && session.undoSteps() == steps + 1);
    CHECK(rig.thrownTo() == 1);
    CHECK(session.undo());
    CHECK(drums.fx(1, 4).throwTo == 0 && drums.level(1, 4) > 0.f && rig.thrownTo() == 0);
    CHECK(session.redo());
    CHECK(drums.fx(1, 4).throwTo == 1 && rig.thrownTo() == 1);

    // A note's.
    session.addTrack(TrackKind::Phazed);
    rig.project().track = 1;
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    roll.moveCursorTo(60, 4);
    rig.tap(A);
    settle();
    rig.key(X, true);
    rig.tap(A, 2); // SPACE
    rig.key(X, false);
    settle();
    CHECK(p.at(60, 4) >= 0 && p.notes[p.at(60, 4)].throwTo == 2 && rig.thrownTo() == 3);
    CHECK(session.undo());
    CHECK(p.at(60, 4) >= 0 && p.notes[p.at(60, 4)].throwTo == 0 && rig.thrownTo() == 1);
    CHECK(session.redo());
    CHECK(p.at(60, 4) >= 0 && p.notes[p.at(60, 4)].throwTo == 2 && rig.thrownTo() == 3);
}

// A bar copied to another scene on the SONG screen (hold R1 and move) takes its throws with it.
void testCopiedToAnotherScene() {
    Rig rig;
    Session& session = rig.app.session();
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    drums.setStep(1, 4, 0.8f);
    StepFx fx;
    fx.throwTo = 2;
    drums.setFx(1, 4, fx);
    session.addTrack(TrackKind::Phazed);
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    p.notes[0] = NoteEvent{2, 60, 2.f, 0.f, 0.8f};
    p.notes[0].throwTo = 1;
    session.sendNotePattern(1, 0);
    session.view = View::Song;
    for (int track = 0; track < 2; ++track) {
        rig.app.song().moveCursorTo(0, track);
        rig.key(R1, true);
        rig.tap(DOWN);
        rig.key(R1, false);
    }
    CHECK(rig.project().tracks[0].drums[1][0].fx[1][4].throwTo == 2 && rig.project().tracks[0].drums[1][0].level[1][4] > 0.f);
    CHECK(rig.project().tracks[1].notes[1][0].notes[0].throwTo == 1);
    // The engine plays scene 2 with them. (Scene 1's are taken out first, so only the copy can wake a bus.)
    session.view = View::Pattern;
    rig.project().track = 0;
    drums.setFx(1, 4, StepFx());
    p.notes[0].throwTo = 0;
    session.sendNotePattern(1, 0);
    rig.frames(2);
    rig.render(2048);
    rig.engine->prepare(48000.f);
    session.togglePlay(true); // the whole song: scene 1, then scene 2
    rig.frames(1);
    rig.render(kBar);
    CHECK(!rig.engine->busAwake(0) && !rig.engine->busAwake(1));
    rig.render(kBar);
    CHECK(rig.engine->busAwake(0) && rig.engine->busAwake(1));
}

// ---- a bounce -----------------------------------------------------------------------------------

void testABounce() {
    char pattern[] = "/tmp/apotheca-throw-XXXXXX";
    const std::string folder = mkdtemp(pattern);
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
    const auto loudest = [](const std::vector<float>& v, std::size_t from, std::size_t to) {
        float p = 0.f;
        for (std::size_t i = from; i < to && i < v.size(); ++i) p = std::fmax(p, std::fabs(v[i]));
        return p;
    };
    Rig rig;
    Session& session = rig.app.session();
    PatternScreen& drums = rig.app.drums();
    session.useBounceFolder(folder);
    rig.frames(2);
    // One rim shot on step 1 (60 ms long) and nothing else. Its sends are at 0.
    drums.setStep(6, 0, 1.f);
    rig.frames(2);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const std::vector<float> dry = read(session.bounceLast());
    // The same, thrown to ECHO: the bounce has the echoes (a new song's ECHO holds TAPE, a dotted eighth note).
    StepFx fx;
    fx.throwTo = 1;
    drums.setFx(6, 0, fx);
    rig.frames(2);
    sleep(1); // the files are named by the second
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const std::vector<float> wet = read(session.bounceLast());
    CHECK(dry.size() == static_cast<std::size_t>(kBar + 3 * 48000) && wet.size() == dry.size());
    if (wet.size() != dry.size() || dry.empty()) return;
    CHECK(loudest(dry, 0, 2000) > 0.2f && loudest(dry, 2 * kStep, kBar) == 0.f);
    CHECK(loudest(wet, 0, 2000) > 0.2f && loudest(wet, 3 * kStep, 4 * kStep) > 0.2f); // the first echo, three steps on, about as loud as the hit
    // And thrown from a muted track: nothing of it.
    session.setMuted(0, true);
    sleep(1);
    CHECK(session.startBounce(Session::Bounce::Song));
    session.finishBounce();
    const std::vector<float> muted = read(session.bounceLast());
    CHECK(muted.size() == dry.size() && loudest(muted, 0, muted.size()) == 0.f);
}

} // namespace

int main() {
    ui::gStickSpeed = 1.f;
    testTheDrumGrid();
    testThePianoRoll();
    testTheSongFile();
    testUndo();
    testCopiedToAnotherScene();
    testABounce();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("throw app tests passed\n");
    return 0;
}
