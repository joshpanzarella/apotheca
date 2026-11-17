// The easy wins (docs/plans/easywins-plan.md), a section a batch:
//   taptempo   TAP TEMPO: the rule (tap_tempo.hpp), A on the drum grid's tempo row and on SET's TEMPO.
//   click      the METRONOME and the COUNT IN: the engine (where each click falls, the song as it was
//              with it off, any blocks, the count's end on its sample, Stop and the tempo meanwhile), the
//              SET rows and settings.txt, when the Session tells the engine, a bounce without it.
//   transpose  TRANSPOSE: chord names, Session::transposeCell (every bar, all or nothing, undo, the
//              engine told), and hold A + UP / DOWN / the stick on a SONG cell, its words and hints.
//
// `easy_test NAME...` runs just those sections.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "chords.hpp"
#include "tap_tempo.hpp"
#include "song_file.hpp"
#include "temp_folder.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, L1 = SDLK_q, R1 = SDLK_w, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT,
                  RIGHT = SDLK_RIGHT, SELECT = SDLK_BACKSPACE, START = SDLK_RETURN;

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
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    void settle() { frames(50); }
    void undo() {
        key(SELECT, true);
        tap(B);
        key(SELECT, false);
        frames(1);
    }
    std::string said() { return app.undoSaid(); }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    TrackData& track(int t) { return project().tracks[t]; }
};

// ---- batch 1: TAP TEMPO ------------------------------------------------------------------------

void testTheTapRule() {
    TapTempo t;
    CHECK(t.tap(1000) == 0.f); // the first of a run: nothing yet
    CHECK(t.tap(1500) == 120.f);
    CHECK(t.tap(2000) == 120.f);
    CHECK(t.tap(2500) == 120.f);
    CHECK(t.tap(3000) == 120.f);
    // The last four gaps: 500, 500, 500, 600 -> 525 ms -> 114.3
    CHECK(t.tap(3600) == 114.f);
    CHECK(t.taps() == 5);
    // Only the last four gaps count: four more of 600 and it is 100.
    t.tap(4200), t.tap(4800), t.tap(5400);
    CHECK(t.tap(6000) == 100.f);
    // A gap over two seconds starts again.
    CHECK(t.tap(6000 + TapTempo::kForgetMs + 1) == 0.f);
    CHECK(t.taps() == 1);
    CHECK(t.tap(6000 + TapTempo::kForgetMs + 1 + 400) == 150.f);
    // Two seconds exactly is still a gap: 30 BPM, held to the slowest.
    TapTempo slow;
    slow.tap(0);
    CHECK(slow.tap(TapTempo::kForgetMs) == kMinTempo);
    // Held to the fastest.
    TapTempo fast;
    fast.tap(0);
    CHECK(fast.tap(100) == kMaxTempo);
    CHECK(fast.tap(100) == kMaxTempo); // (two on one frame)
    // A clock that went back starts again.
    TapTempo back;
    back.tap(5000);
    CHECK(back.tap(4000) == 0.f);
    back.forget();
    CHECK(back.taps() == 0);
}

void testTapOnTheDrumGrid() {
    Rig rig;
    PatternScreen& drums = rig.app.drums();
    drums.moveCursorTo(0, 0);
    rig.tap(UP);
    CHECK(drums.cursorOnTempo());
    const float was = rig.project().tempo;
    rig.tap(A); // the first: nothing changes
    CHECK(rig.project().tempo == was);
    CHECK(rig.said().empty()); // (it says nothing: hold A to turn the tempo begins with a tap)
    rig.frames(30); // 32 frames from tap to tap: 512 ms, 117.2 BPM
    rig.tap(A);
    CHECK(rig.project().tempo == 117.f);
    CHECK(rig.said() == "TAPPED: 117 BPM");
    rig.frames(30);
    rig.tap(A);
    rig.frames(30);
    rig.tap(A);
    CHECK(rig.project().tempo == 117.f);
    // The engine was told: a step at 117 BPM.
    rig.frames(2);
    // A long pause, then hold A + UP: a first tap again (no change), then the turn.
    rig.frames(200);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(UP);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.project().tempo == 118.f);
    // On a lane's name A plays the drum, and taps nothing.
    rig.frames(200);
    drums.moveCursorTo(0, -1);
    rig.tap(A);
    rig.frames(30);
    rig.tap(A);
    CHECK(rig.project().tempo == 118.f);
}

void testTapOnTheSetPage() {
    Rig rig;
    rig.session().view = View::Settings;
    rig.frames(1);
    rig.app.settings().goTo(SettingsScreen::kTempo);
    rig.frames(1);
    for (int i = 0; i < 5; ++i) {
        rig.tap(A);
        rig.frames(41); // 43 frames: 688 ms, 87.2 BPM
    }
    CHECK(rig.project().tempo == 87.f);
    CHECK(rig.said() == "TAPPED: 87 BPM");
    // Another row: A there is its own (none on CHORD PACK), and taps nothing.
    rig.frames(200);
    rig.app.settings().goTo(SettingsScreen::kPack);
    rig.tap(A);
    rig.frames(30);
    rig.tap(A);
    CHECK(rig.project().tempo == 87.f);
}


// ---- batch 2: the METRONOME and the COUNT IN -----------------------------------------------------

const long kStep120 = 6000; // a step at 120 BPM, 48 kHz

Command make(Command::Type type, int track = 0, int scene = 0) {
    Command c;
    c.type = type;
    c.track = track;
    c.scene = scene;
    return c;
}

// A drum song on track 0 (hits on steps 1, 7 and 11), at 120 BPM.
void drumSong(Engine& engine) {
    for (int step : {0, 6, 10}) {
        Command c = make(Command::SetStep);
        c.a = step % 3;
        c.b = step;
        c.value = 0.9f;
        engine.send(c);
    }
}

std::vector<float> render(Engine& engine, long frames, int block) {
    std::vector<float> all(static_cast<std::size_t>(2 * frames));
    for (long at = 0; at < frames; at += block) {
        const int n = static_cast<int>(frames - at < block ? frames - at : block);
        engine.render(all.data() + 2 * at, n);
    }
    return all;
}

float loudest(const std::vector<float>& v, long from, long to) {
    float p = 0.f;
    for (long i = 2 * from; i < 2 * to && i < static_cast<long>(v.size()); ++i) p = std::fmax(p, std::fabs(v[static_cast<std::size_t>(i)]));
    return p;
}

void play(Engine& engine, int countIn = 0) {
    Command c = make(Command::Play);
    c.d = countIn;
    engine.send(c);
}

void clickTo(Engine& engine, bool on) {
    Command c = make(Command::SetClick);
    c.a = on ? 1 : 0;
    engine.send(c);
}

void testTheClick() {
    const long bar = 16 * kStep120, click = static_cast<long>(0.03 * 48000);
    // Off: the song as it always was, to the bit (an engine never told, and one told on and off again).
    {
        std::unique_ptr<Engine> plain(new Engine), told(new Engine);
        drumSong(*plain), drumSong(*told);
        clickTo(*told, true);
        render(*told, 100, 100);
        clickTo(*told, false);
        play(*plain), play(*told);
        CHECK(render(*plain, 2 * bar, 256) == render(*told, 2 * bar, 256));
        CHECK(!told->clickOn());
    }
    // On, an empty song: a click on every beat's first sample, 30 ms long, nothing between; the bar's first louder.
    {
        std::unique_ptr<Engine> engine(new Engine);
        clickTo(*engine, true);
        render(*engine, 1000, 250); // (stopped: no click)
        CHECK(loudest(render(*engine, 2000, 250), 0, 2000) == 0.f);
        play(*engine);
        const std::vector<float> out = render(*engine, 2 * bar, 256);
        for (int beat = 0; beat < 8; ++beat) {
            const long at = beat * 4 * kStep120;
            CHECK(out[static_cast<std::size_t>(2 * at)] == 0.f); // a sine from its start...
            CHECK(loudest(out, at, at + click) > 0.1f);           // ...heard
            CHECK(loudest(out, at + click, at + 4 * kStep120) == 0.f);
            CHECK(out[static_cast<std::size_t>(2 * at + 2)] == out[static_cast<std::size_t>(2 * at + 3)]); // both sides
        }
        CHECK(loudest(out, 0, click) > 1.3f * loudest(out, 4 * kStep120, 4 * kStep120 + click));
        CHECK(loudest(out, bar, bar + click) > 1.3f * loudest(out, bar + 4 * kStep120, bar + 4 * kStep120 + click));
        // In any blocks, the same to the bit; a song under it too.
        for (int block : {1, 64, 333, 1024}) {
            std::unique_ptr<Engine> other(new Engine), song1(new Engine), song2(new Engine);
            clickTo(*other, true);
            play(*other);
            CHECK(render(*other, 2 * bar, block) == out);
            for (Engine* e : {song1.get(), song2.get()}) drumSong(*e), clickTo(*e, true), play(*e);
            CHECK(render(*song1, bar, 256) == render(*song2, bar, block));
        }
        // Catch does not hear it: the last bar caught is silence.
        Command stop = make(Command::Stop);
        engine->send(stop);
        render(*engine, 1000, 250);
        const std::vector<float> caught = engine->lastBar();
        float caughtPeak = 0.f;
        for (float v : caught) caughtPeak = std::fmax(caughtPeak, std::fabs(v));
        CHECK(caught.size() >= static_cast<std::size_t>(bar) && caughtPeak == 0.f);
        // Stopped: none, even with it on.
        CHECK(loudest(render(*engine, bar, 256), click, bar) == 0.f);
    }
    // Turned on while playing: from the next beat.
    {
        std::unique_ptr<Engine> engine(new Engine);
        play(*engine);
        render(*engine, kStep120 * 2, 256);
        clickTo(*engine, true);
        const std::vector<float> out = render(*engine, bar, 256);
        CHECK(loudest(out, 0, 2 * kStep120) == 0.f && loudest(out, 2 * kStep120, 2 * kStep120 + click) > 0.1f);
    }
}

void testTheCountIn() {
    const long bar = 16 * kStep120, click = static_cast<long>(0.03 * 48000);
    std::unique_ptr<Engine> counted(new Engine), later(new Engine);
    drumSong(*counted), drumSong(*later);
    play(*counted, 16);
    const std::vector<float> a = render(*counted, 3 * bar, 256);
    // The other is played a bar later, by hand, with no count.
    std::vector<float> b = render(*later, bar, 256);
    play(*later);
    const std::vector<float> rest = render(*later, 2 * bar, 256);
    b.insert(b.end(), rest.begin(), rest.end());
    // The count: four clicks and nothing of the song; then the song, to the bit, from its sample.
    for (int beat = 0; beat < 4; ++beat) {
        const long at = beat * 4 * kStep120;
        CHECK(loudest(a, at, at + click) > 0.1f && loudest(a, at + click, at + 4 * kStep120) == 0.f);
    }
    CHECK(loudest(a, 0, click) > 1.3f * loudest(a, 4 * kStep120, 4 * kStep120 + click)); // the first beat higher
    CHECK(std::vector<float>(a.begin() + 2 * bar, a.end()) == std::vector<float>(b.begin() + 2 * bar, b.end()));
    CHECK(counted->playStep() >= 0 && counted->countingIn() == 0);
    // In any blocks, the same.
    for (int block : {1, 77, 1024}) {
        std::unique_ptr<Engine> other(new Engine);
        drumSong(*other);
        play(*other, 16);
        CHECK(render(*other, 3 * bar, block) == a);
    }
    // What it shows: the beats to go (4, 3, 2, 1), and the song not playing meanwhile.
    {
        std::unique_ptr<Engine> e(new Engine);
        play(*e, 16);
        render(*e, 10, 10);
        CHECK(e->countingIn() == 4 && e->playStep() == -1);
        render(*e, 4 * kStep120, 256);
        CHECK(e->countingIn() == 3);
        render(*e, 8 * kStep120, 256);
        CHECK(e->countingIn() == 1);
        render(*e, 4 * kStep120, 256);
        CHECK(e->countingIn() == 0 && e->playStep() == 0);
    }
    // Stop meanwhile: called off, nothing plays after; the Play still counted (the app's count of Plays agrees).
    {
        std::unique_ptr<Engine> e(new Engine);
        drumSong(*e);
        play(*e, 16);
        render(*e, 5 * kStep120, 256);
        e->send(make(Command::Stop));
        const std::vector<float> after = render(*e, 2 * bar, 256);
        CHECK(loudest(after, click, 2 * bar) == 0.f && e->playStep() == -1 && e->countingIn() == 0);
        // A song that stops at its end, played after: the engine says it was the second Play.
        Command end = make(Command::SetSongEnd);
        end.a = 1;
        e->send(end);
        play(*e);
        render(*e, 3 * bar, 256);
        CHECK(e->songEnded() == 2);
    }
    // A panic meanwhile: called off too.
    {
        std::unique_ptr<Engine> e(new Engine);
        play(*e, 16);
        render(*e, 5 * kStep120, 256);
        Command panic = make(Command::Panic);
        panic.a = 1;
        e->send(panic);
        render(*e, 1000, 250);
        panic.a = 0;
        e->send(panic);
        render(*e, 2 * bar, 256);
        CHECK(e->countingIn() == 0 && e->playStep() == -1);
    }
    // The tempo turned meanwhile: the count keeps its place in the step, and the song starts on the new beat.
    {
        std::unique_ptr<Engine> e(new Engine);
        play(*e, 16);
        render(*e, 9000, 250); // a step and a half of 6000: half of step 2 is left
        Command t = make(Command::SetTempo);
        t.value = 240.f; // a step of 3000: what is left of it is 1500
        e->send(t);
        const std::vector<float> out = render(*e, 1500 + 14 * 3000, 1);
        // Step 3 of the count 1500 on, so the second beat (step 5) 1500 + 2 * 3000 on; the song's step 1 1500 + 14 * 3000 on.
        CHECK(loudest(out, 0, 7500) == 0.f && loudest(out, 7500, 7520) > 0.f);
        CHECK(e->playStep() == -1);
        render(*e, 1, 1);
        CHECK(e->playStep() == 0);
    }
}

void testTheClickInTheApp() {
    const std::string folder = tempFolder("easyclick");
    {
        Rig rig;
        Session& session = rig.session();
        session.useProgramFolder(folder);
        // Off at first: the engine is not told on.
        CHECK(session.click() == Session::kClickOff && !session.countIn());
        rig.frames(2);
        CHECK(!rig.engine->clickOn());
        session.setClick(Session::kClickOn);
        rig.frames(2);
        CHECK(rig.engine->clickOn());
        // RECORDING: on only while the song plays with PLAY + RECORD.
        session.setClick(Session::kClickRecording);
        rig.frames(2);
        CHECK(!rig.engine->clickOn());
        session.togglePlay(false);
        rig.frames(2);
        CHECK(!rig.engine->clickOn()); // (PLAY ONLY)
        session.togglePlay(false);
        session.setMidiRecording(true);
        session.togglePlay(false);
        rig.frames(2);
        CHECK(rig.engine->clickOn());
        session.togglePlay(false);
        rig.frames(2);
        CHECK(!rig.engine->clickOn());
        // COUNT IN: only with PLAY + RECORD. START counts a bar first; the box says so.
        session.setCountIn(true);
        rig.frames(1);
        rig.tap(START);
        rig.frames(2);
        CHECK(rig.project().playing && rig.engine->countingIn() == 4 && rig.engine->playStep() == -1);
        rig.tap(START); // called off
        rig.frames(2);
        CHECK(!rig.project().playing && rig.engine->countingIn() == 0);
        session.setMidiRecording(false);
        rig.tap(START);
        rig.frames(2);
        CHECK(rig.engine->countingIn() == 0 && rig.engine->playStep() >= 0); // PLAY ONLY: at once
        rig.tap(START);
        session.setMidiRecording(true);
        session.setClick(Session::kClickOn);
    }
    // Kept in settings.txt, and read back.
    {
        std::string text;
        CHECK(readTextFile(folder + "/settings.txt", text) && text.find("click 1") != std::string::npos && text.find("countin 1") != std::string::npos);
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().click() == Session::kClickOn && rig.session().countIn() && rig.session().midiRecording());
    }
    // A bounce has none: one rim shot, CLICK ON, and nothing at the second beat.
    {
        Rig rig;
        Session& session = rig.session();
        session.useBounceFolder(folder + "/bounces");
        session.setClick(Session::kClickOn);
        session.setCountIn(true);
        session.setMidiRecording(true);
        rig.frames(2);
        rig.app.drums().setStep(6, 0, 1.f);
        rig.frames(2);
        CHECK(session.startBounce(Session::Bounce::Song));
        session.finishBounce();
        std::string bytes;
        CHECK(readTextFile(folder + "/bounces/" + session.bounceLast(), bytes) && bytes.size() > 44 + 4 * 16 * kStep120);
        float hit = 0.f, after = 0.f;
        for (std::size_t at = 44; at + 3 < bytes.size(); at += 4) {
            const int low = static_cast<unsigned char>(bytes[at]), high = static_cast<signed char>(bytes[at + 1]);
            const float v = std::fabs(static_cast<float>(high * 256 + low) / 32767.f);
            const long frame = static_cast<long>((at - 44) / 4);
            if (frame < 2000) hit = std::fmax(hit, v); // the rim shot at once: no count in either
            else if (frame > 3 * kStep120) after = std::fmax(after, v);
        }
        CHECK(hit > 0.2f && after == 0.f);
    }
}

// ---- batch 3: TRANSPOSE -------------------------------------------------------------------------

std::string moved(const char* name, int by) {
    char n[16] = {};
    std::snprintf(n, sizeof n, "%s", name);
    transposeChordName(n, by);
    return n;
}

void testChordNames() {
    CHECK(moved("C", 2) == "D" && moved("Am7", 3) == "Cm7" && moved("F#m", 1) == "Gm" && moved("Bb7", -1) == "A7");
    CHECK(moved("C/E", 2) == "D/F#" && moved("IV", 5) == "IV" && moved("Eb", 12) == "Eb" && moved("Bdim", 1) == "Cdim");
    CHECK(moved("G7sus4", -7) == "C7sus4" && moved("Ab", 1) == "A" && moved("E", 1) == "F" && moved("Cmaj7", -1) == "Bmaj7");
}

// A Phazed track (track 1) with notes in two bars of scene 0 and a chord.
int synthWithNotes(Rig& rig) {
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Phazed));
    const int t = 1;
    session.setCellBars(t, 0, 2);
    TrackData& track = rig.track(t);
    NoteEvent n;
    n.step = 0, n.pitch = 60, n.length = 1.f, n.velocity = 0.9f;
    track.notes[0][0].notes[0] = n;
    n.step = 4, n.pitch = 67;
    track.notes[0][1].notes[0] = n;
    ChordEvent c = makeChord(builtInPack().chords[5], 57, 8, 4.f, 0.9f, 0); // Am
    track.notes[0][0].chords[0] = c;
    session.sendNotePattern(t, 0);
    rig.settle();
    return t;
}

void testTransposeACell() {
    Rig rig;
    Session& session = rig.session();
    const int t = synthWithNotes(rig);
    const std::string before = songText(rig.project());
    CHECK(session.transposeCell(t, 0, 3) == Session::kTransposed);
    const TrackData& track = rig.track(t);
    CHECK(track.notes[0][0].notes[0].pitch == 63 && track.notes[0][1].notes[0].pitch == 70);
    CHECK(track.notes[0][0].chords[0].root == 60 && std::string(track.notes[0][0].chords[0].name) == "Cm");
    rig.settle();
    // Back: the same song as before, to the byte.
    CHECK(session.transposeCell(t, 0, -3) == Session::kTransposed);
    rig.settle();
    CHECK(songText(rig.project()) == before);
    // One step of undo: a move of 5 taken back.
    session.transposeCell(t, 0, 5);
    rig.settle();
    rig.undo();
    rig.settle();
    CHECK(rig.track(t).notes[0][0].notes[0].pitch == 60 && songText(rig.project()) == before);
    // All or nothing: a key that would pass C8 moves nothing.
    rig.track(t).notes[0][1].notes[1] = rig.track(t).notes[0][1].notes[0];
    rig.track(t).notes[0][1].notes[1].pitch = kRollHighest - 2;
    CHECK(session.transposeCell(t, 0, 3) == Session::kOutOfRange && rig.track(t).notes[0][0].notes[0].pitch == 60);
    CHECK(session.transposeCell(t, 0, 2) == Session::kTransposed && rig.track(t).notes[0][1].notes[1].pitch == kRollHighest);
    CHECK(session.transposeCell(t, 0, -60) == Session::kOutOfRange); // (the chord's A3 would pass C0)
    // A drum track, an empty scene (none is made), nothing to move.
    CHECK(session.transposeCell(0, 0, 1) == Session::kNothingToMove);
    CHECK(session.transposeCell(t, 5, 1) == Session::kNothingToMove && !rig.track(t).notes.has(5));
    CHECK(session.transposeCell(t, 0, 0) == Session::kNothingToMove);
    // The engine hears it: a song played with the cell moved is the song written moved by hand.
    {
        Rig a, b;
        const int ta = synthWithNotes(a), tb = synthWithNotes(b);
        a.session().transposeCell(ta, 0, 7);
        for (int bar = 0; bar < 2; ++bar) {
            NotePattern& p = b.track(tb).notes[0][bar];
            for (NoteEvent& n : p.notes)
                if (n.velocity > 0.f) n.pitch += 7;
            for (ChordEvent& c : p.chords)
                if (c.velocity > 0.f) c.root += 7, transposeChordName(c.name, 7);
        }
        b.session().sendNotePattern(tb, 0);
        a.settle(), b.settle();
        CHECK(songText(a.project()) == songText(b.project()));
        a.session().togglePlay(false), b.session().togglePlay(false);
        std::vector<float> x(2 * 4096), y(2 * 4096);
        bool same = true;
        float loud = 0.f;
        for (int i = 0; i < 60; ++i) {
            a.engine->render(x.data(), 4096), b.engine->render(y.data(), 4096);
            same = same && x == y;
            for (float v : x) loud = std::fmax(loud, std::fabs(v));
        }
        CHECK(same && loud > 0.01f);
    }
}

void testTransposeOnSong() {
    Canvas canvas;
    Rig rig;
    Session& session = rig.session();
    const int t = synthWithNotes(rig);
    session.view = View::Song;
    rig.frames(1);
    rig.app.song().moveCursorTo(0, t);
    rig.frames(1);
    rig.key(A, true);
    rig.frames(2);
    // The bar says it (and fits).
    if (canvas.ok) {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
    }
    rig.tap(UP), rig.tap(UP);
    CHECK(rig.track(t).notes[0][0].notes[0].pitch == 62 && rig.said() == "MOVED +2 SEMITONES");
    rig.tap(DOWN);
    CHECK(rig.track(t).notes[0][0].notes[0].pitch == 61 && rig.said() == "MOVED +1 SEMITONE");
    // The stick: an octave a push.
    rig.key(SDLK_i, true); // (the stick up)
    rig.frames(2);
    rig.key(SDLK_i, false);
    rig.frames(2);
    CHECK(rig.track(t).notes[0][0].notes[0].pitch == 73 && rig.said() == "MOVED +13 SEMITONES");
    rig.tap(LEFT); // (and its length, as before)
    CHECK(rig.track(t).bars[0] == 1);
    rig.key(A, false);
    rig.frames(2);
    CHECK(session.view == View::Song); // a hold that turned something does not open the cell
    // A fresh hold counts from 0; back to where it began says so.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(DOWN);
    rig.tap(UP);
    CHECK(rig.said() == "BACK WHERE IT WAS");
    rig.key(A, false);
    rig.frames(2);
    // On a drum cell: nothing moves, and it says why.
    rig.app.song().moveCursorTo(0, 0);
    rig.app.drums().setStep(0, 0, 1.f);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(UP);
    CHECK(rig.said() == "DRUMS DO NOT TRANSPOSE" && rig.track(0).drums[0][0].level[0][0] == 1.f);
    rig.key(A, false);
    rig.frames(2);
    // Undo takes the whole hold's moves back as one step (one burst of edits).
    rig.settle();
    rig.undo();
    rig.settle();
    CHECK(rig.track(t).notes[0][0].notes[0].pitch == 60);
}

struct Section {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const Section sections[] = {
        {"taptempo", [] { testTheTapRule(), testTapOnTheDrumGrid(), testTapOnTheSetPage(); }},
        {"click", [] { testTheClick(), testTheCountIn(), testTheClickInTheApp(); }},
        {"transpose", [] { testChordNames(), testTransposeACell(), testTransposeOnSong(); }},
    };
    for (const Section& s : sections) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], s.name) == 0;
        if (wanted) s.run();
    }
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("easy: all passed\n");
    return 0;
}
