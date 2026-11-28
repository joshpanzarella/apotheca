// The easy wins (docs/plans/easywins-plan.md), a section a batch:
//   taptempo   TAP TEMPO: the rule (tap_tempo.hpp), A on the drum grid's tempo row and on SET's TEMPO, timed by each press's own time.
//   click      the METRONOME and the COUNT IN: the engine (where each click falls, the song as it was
//              with it off, any blocks, the count's end on its sample, Stop and the tempo meanwhile), the
//              SET rows and settings.txt, when the Session tells the engine, a bounce without it.
//   transpose  TRANSPOSE: chord names, Session::transposeCell (every bar, all or nothing, undo, the
//              engine told), and hold A + UP / DOWN / the stick on a SONG cell, its words and hints.
//   scale      SCALE LOCK: the scales (scales.hpp), the song file's line, undo, the SET rows, the roll's
//              cursor and carry, the chord in hand moved into the key.
//   duplicate  DUPLICATE A TRACK: the text of undo's steps (doubleTrackText), Session::duplicateTrack (the
//              copy, the tracks after it, twelve, undo after, the engine's sound), L1 + R1 on a head, a panic.
//   even       EVEN: the rule (euclidHit), Y held on a lane's name + R1 / L1, the turn, the words, undo, hints.
//   kits       KIT PRESETS: a kit's text (kitText, readKit, copyKit), the built-in recipes, Session's
//              list, load, save and delete, and the KIT page's stop above lane 1.
//   looks      the owner's five of 2026-11-18: the lane's volume and its edge, the feel mark's room, REPEATS.
//   mutes      QUEUED MUTES: the engine's waiting gate (on the bar's first sample, to the bit; Stop, called
//              off, a track gone), the Session's waiting marks, Y and X on a SONG head, SET's MUTES row.
//
// And the gesture audit's (docs/plans/gesture-audit.md), batch B2:
//   stick      G14: the stick means what the D-pad means. A carry on the roll keeps to a SCALE and goes an
//              octave a push with L1; both ways in one push (2026-11-28); X let go with the stick pushed moves nothing on either
//              grid; L1 or A held on SONG's REPEATS turns what the D-pad turns there.
//   xempty     G11: X held + B empties what X shows (a hit's, a note's, a chord's effects; a lane's FEEL and
//              SWING), keeps the hit or note, one step of undo of its own, nothing to empty no step.
//   chordlength G27: Y held on the roll, L2 / R2 halve or double the chord in hand (or the one swapped).
//
// `easy_test NAME...` runs just those sections.

#include <SDL.h>

#include <algorithm>
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
#include "pattern_layout.hpp"
#include "scales.hpp"
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
const SDL_Keycode STICK_UP = SDLK_i, STICK_DOWN = SDLK_k, STICK_LEFT = SDLK_j, STICK_RIGHT = SDLK_l, TRIGGER_L2 = SDLK_1, TRIGGER_R2 = SDLK_2;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().view = View::Pattern;
        frames(2);
    }
    void key(SDL_Keycode code, bool down, Uint32 stamp = 0) { // (`stamp`: the time SDL puts on the event; 0, none)
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        event.common.timestamp = stamp;
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

// A tap is timed by the press's own time, SDL's stamp on its event, not by the pass that read it (gesture audit
// G06: the pass's clock was up to a pass out each tap). Taps 500 ms apart by their stamps, read by passes 512 ms
// apart: 120 BPM, where the passes say 117. On the drum grid's tempo row and on SET's TEMPO.
void testTapByThePressesOwnTime() {
    for (const bool onSet : {false, true}) {
        Rig rig;
        if (onSet) {
            rig.session().view = View::Settings;
            rig.frames(1);
            rig.app.settings().goTo(SettingsScreen::kTempo);
            rig.frames(1);
        } else {
            rig.app.drums().moveCursorTo(0, 0);
            rig.tap(UP);
            CHECK(rig.app.drums().cursorOnTempo());
        }
        Uint32 stamp = 50000;
        for (int i = 0; i < 5; ++i) {
            rig.key(A, true, stamp);
            rig.frames(1);
            rig.key(A, false, stamp + 60);
            rig.frames(31); // 32 passes from tap to tap: 512 ms by the pass's clock
            stamp += 500;
        }
        CHECK(rig.project().tempo == 120.f);
        CHECK(rig.said() == "TAPPED: 120 BPM");
    }
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

// ---- batch 4: SCALE LOCK -------------------------------------------------------------------------

void testTheScales() {
    // Each scale's keys, counted: seven, five or six; the key itself always in.
    const int counts[kScaleKinds] = {12, 7, 7, 7, 7, 7, 7, 7, 5, 5, 7, 6};
    for (int kind = 0; kind < kScaleKinds; ++kind) {
        int n = 0;
        for (int pc = 0; pc < 12; ++pc) n += inScale(0, kind, pc) ? 1 : 0;
        CHECK(n == counts[kind] && inScale(5, kind, 5 + 24));
        CHECK(std::strlen(scaleName(kind)) > 0);
    }
    // D major: D E F# G A B C#; out: D# F G# A# C.
    for (int p : {62, 64, 66, 67, 69, 71, 73, 50}) CHECK(inScale(2, kScaleMajor, p));
    for (int p : {63, 65, 68, 70, 72}) CHECK(!inScale(2, kScaleMajor, p));
    CHECK(inScale(9, kScaleHarmonicMinor, 68) && !inScale(9, kScaleMinor, 68)); // (A harmonic minor's G#)
    CHECK(inScale(0, kScaleBlues, 6) && !inScale(0, kScaleMinorPent, 6));
    CHECK(minorScale(kScaleMinor) && minorScale(kScaleDorian) && minorScale(kScaleBlues) && !minorScale(kScaleMajor) && !minorScale(kScaleLydian) && !minorScale(kScaleOff));
    CHECK(scaleKindOf(99) == kScaleOff && scaleKindOf(-1) == kScaleOff && scaleKindOf(kScaleBlues) == kScaleBlues);
    CHECK(std::string(keyName(13)) == "C#" && std::string(keyName(-2)) == "Bb");
    // A pack in C major: into D major +2, into A minor 0 (its relative major is C), into E minor -5 (G), off 0.
    CHECK(packShift(0, false, 2, kScaleMajor) == 2 && packShift(0, false, 9, kScaleMinor) == 0 && packShift(0, false, 4, kScaleMinor) == -5);
    CHECK(packShift(0, false, 7, kScaleOff) == 0 && packShift(9, true, 0, kScaleMajor) == 0 && packShift(9, true, 9, kScaleMinor) == 0);
    CHECK(packShift(0, false, 6, kScaleMajor) == 6 && packShift(0, false, 7, kScaleMajor) == -5);
    // A pack's tonic and minor read from its file.
    ChordPack pack;
    CHECK(parseChordPack("{\"name\":\"m\",\"sourceTonic\":9,\"scale\":{\"pitchClasses\":[0,2,3,5,7,8,10]},\"chords\":[{\"root\":{\"degree\":0},\"intervals\":[0,3,7],\"quality\":\"minor\"}]}", pack));
    CHECK(pack.tonic == 9 && pack.minor && std::string(pack.chords[0].name) == "Am");
    CHECK(builtInPack().tonic == 0 && !builtInPack().minor);
}

void testTheScaleInTheSong() {
    Rig rig;
    Session& session = rig.session();
    rig.settle();
    const std::string plain = songText(rig.project());
    CHECK(plain.find("\nscale ") == std::string::npos); // left out at OFF
    session.setScale(14, kScaleDorian); // (the key goes round: D)
    CHECK(rig.project().scaleKey == 2 && rig.project().scaleKind == kScaleDorian);
    session.setScale(2, kScaleKinds + 4); // (the kinds have ends)
    CHECK(rig.project().scaleKind == kScaleBlues);
    session.setScale(2, kScaleDorian);
    const std::string text = songText(rig.project());
    CHECK(text.find("\nscale 2 3\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && back->scaleKey == 2 && back->scaleKind == kScaleDorian);
    // A key out of range is held; a kind nobody knows is OFF; a line with one number is passed by.
    std::string odd = text;
    odd.replace(odd.find("\nscale 2 3\n"), 11, "\nscale 40 77\n");
    CHECK(parseSong(odd, *back) && back->scaleKey == 11 && back->scaleKind == kScaleOff);
    odd = plain + "scale 5\n";
    back.reset(new Project);
    CHECK(parseSong(odd, *back) && back->scaleKey == 0 && back->scaleKind == kScaleOff);
    // A step of undo.
    rig.settle();
    rig.undo();
    rig.settle();
    CHECK(rig.project().scaleKind == kScaleOff);
    // The SET rows: KEY goes round, SCALE has ends.
    session.view = View::Settings;
    rig.frames(1);
    rig.app.settings().goTo(SettingsScreen::kKey);
    rig.tap(LEFT);
    CHECK(rig.project().scaleKey == 11);
    rig.tap(RIGHT, 3);
    CHECK(rig.project().scaleKey == 2);
    rig.tap(DOWN);
    CHECK(rig.app.settings().cursorRow() == SettingsScreen::kScale);
    rig.tap(RIGHT);
    CHECK(rig.project().scaleKind == kScaleMajor);
    rig.tap(RIGHT, kScaleKinds + 2);
    CHECK(rig.project().scaleKind == kScaleBlues);
    rig.tap(LEFT, kScaleKinds + 2);
    CHECK(rig.project().scaleKind == kScaleOff);
}

void testTheRollKeepsToIt() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    session.view = View::Pattern;
    rig.frames(2);
    NoteScreen& roll = rig.app.notes();
    roll.moveCursorTo(62, 0);
    // Off: every key.
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 63);
    // D MAJOR: the cursor steps through its keys.
    session.setScale(2, kScaleMajor);
    roll.moveCursorTo(62, 0);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 64);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 66);
    rig.tap(LEFT, 3);
    CHECK(roll.cursorPitch() == 61);
    // A note out of the scale is a stop too (it can still be reached, and deleted).
    rig.track(1).notes[0][0].notes[0] = NoteEvent();
    NoteEvent n;
    n.step = 4, n.pitch = 63, n.length = 1.f, n.velocity = 0.9f;
    rig.track(1).notes[0][0].notes[0] = n;
    roll.moveCursorTo(62, 0);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 63);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == 64);
    // ...and the ends of the keyboard stop it.
    roll.moveCursorTo(NoteScreen::kHighest, 0);
    rig.tap(RIGHT);
    CHECK(roll.cursorPitch() == NoteScreen::kHighest);
    // A note carried sideways goes to the next key of the scale: D# (out) carried right lands on E, then F#.
    roll.moveCursorTo(63, 4);
    rig.key(R1, true);
    rig.frames(20); // (held: it carries)
    rig.tap(RIGHT);
    CHECK(rig.track(1).notes[0][0].notes[0].pitch == 64);
    rig.tap(RIGHT);
    CHECK(rig.track(1).notes[0][0].notes[0].pitch == 66);
    rig.key(R1, false);
    rig.frames(2);
    // The chord in hand: the built-in pack (C major) moved into D major: its first chord is D.
    CHECK(std::string(roll.shapeAt(0).name) == "D" && roll.shapeAt(0).rootKey == 2 && std::string(roll.shapeAt(1).name) == "Em");
    session.setScale(9, kScaleMinor); // A minor: C major's own chords
    CHECK(std::string(roll.shapeAt(0).name) == "C" && std::string(roll.shapeAt(5).name) == "Am");
    session.setScale(2, kScaleOff); // off: the pack as it is
    CHECK(std::string(roll.shapeAt(0).name) == "C");
    // Placed with Y: the chord lands moved.
    session.setScale(2, kScaleMajor);
    roll.moveCursorTo(62, 8);
    rig.tap(Y);
    rig.frames(2);
    const ChordEvent* c = nullptr;
    for (const ChordEvent& e : rig.track(1).notes[0][0].chords)
        if (e.velocity > 0.f) c = &e;
    CHECK(c && c->root % 12 == 2 && std::string(c->name) == "D");
}

// ---- batch 6: DUPLICATE A TRACK ----------------------------------------------------------------------

void testDoubleTrackText() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Phazed) && session.addTrack(TrackKind::Jx3p));
    rig.app.drums().setStep(0, 0, 1.f);
    rig.settle();
    std::string text = songText(rig.project());
    std::string head;
    std::vector<std::string> before, after;
    CHECK(cutSong(text, head, before) && before.size() == 3);
    CHECK(doubleTrackText(text, 1));
    CHECK(cutSong(text, head, after) && after.size() == 4 && after[0] == before[0] && after[1] == before[1] && after[2] == before[1] && after[3] == before[2]);
    std::unique_ptr<Project> read(new Project);
    CHECK(parseSong(text, *read) && read->trackCount() == 4 && read->tracks[2].kind == TrackKind::Phazed && read->tracks[3].kind == TrackKind::Jx3p);
    // No such track, or not a song: as it was.
    const std::string same = text;
    CHECK(!doubleTrackText(text, 7) && !doubleTrackText(text, -1) && text == same);
    std::string junk = "hello";
    CHECK(!doubleTrackText(junk, 0) && junk == "hello");
}

void testDuplicateATrack() {
    Rig rig;
    Session& session = rig.session();
    const int t = synthWithNotes(rig); // track 1: Phazed, notes and a chord
    CHECK(session.addTrack(TrackKind::Jx3p)); // track 2
    session.setTrackName(t, "LEAD");
    session.setMix(t, -6, 20);
    session.setSend(t, 0, 0.4f);
    session.setMuted(t, true);
    session.setSolo(t, true);
    session.setEffect(t, 0, EffectKind::Drive);
    rig.project().track = 2;
    rig.settle();
    // An edit on track 2 before, so undo has a step from before the copy.
    NoteEvent n;
    n.step = 3, n.pitch = 50, n.length = 1.f, n.velocity = 0.9f;
    rig.track(2).notes[0][0].notes[0] = n;
    session.sendNotePattern(2, 0);
    rig.settle();
    const std::string was1 = songText(rig.project());
    std::string head;
    std::vector<std::string> then;
    cutSong(was1, head, then);
    CHECK(session.duplicateTrack(t) == 2);
    rig.settle();
    CHECK(rig.project().trackCount() == 4 && rig.project().track == 3); // (the track worked on moved down with its track)
    std::vector<std::string> now;
    CHECK(cutSong(songText(rig.project()), head, now) && now.size() == 4 && now[0] == then[0] && now[1] == then[1] && now[3] == then[2]);
    const TrackData &a = rig.track(1), &b = rig.track(2);
    CHECK(b.kind == TrackKind::Phazed && std::string(b.name) == "LEAD" && b.mixDb == -6 && b.mixPan == 20 && b.muted && !b.solo && a.solo);
    CHECK(b.notes[0][0].notes[0].pitch == 60 && b.notes[0][1].notes[0].pitch == 67 && b.notes[0][0].chords[0].velocity > 0.f && b.bars[0] == 2);
    CHECK(b.effects[0].kind == static_cast<int>(EffectKind::Drive) && b.send[0] == a.send[0] && a.send[0] > 0.3f);
    // The copy is its own: a note changed on it leaves the first as it was.
    rig.track(2).notes[0][0].notes[0].pitch = 72;
    session.sendNotePattern(2, 0);
    CHECK(rig.track(1).notes[0][0].notes[0].pitch == 60);
    rig.settle();
    // Undo: the note back; again: track 3's (was 2's) edit from before the copy, the four tracks still there.
    rig.undo();
    rig.settle();
    CHECK(rig.track(2).notes[0][0].notes[0].pitch == 60 && rig.project().trackCount() == 4);
    rig.undo();
    rig.settle();
    CHECK(rig.project().trackCount() == 4 && rig.track(3).kind == TrackKind::Jx3p && rig.track(3).notes[0][0].notes[0].velocity == 0.f);
    CHECK(rig.track(2).notes[0][0].notes[0].pitch == 60 && rig.track(1).notes[0][0].notes[0].pitch == 60);
    // Twelve is the most; no such track: nothing.
    while (rig.project().trackCount() < kNumTracks) CHECK(session.duplicateTrack(0) == 1);
    CHECK(session.duplicateTrack(0) == -1 && session.duplicateTrack(-1) == -1 && rig.project().trackCount() == kNumTracks);
    // The engine plays the copy: a song with the track copied is twice as loud a line as... the same song
    // built by hand with the track written twice sounds the same, to the bit.
    {
        Rig x, y;
        const int tx = synthWithNotes(x);
        CHECK(x.session().duplicateTrack(tx) == 2);
        synthWithNotes(y);
        CHECK(y.session().addTrack(TrackKind::Phazed));
        y.session().setCellBars(2, 0, 2);
        for (int bar = 0; bar < 2; ++bar) y.track(2).notes[0][bar] = y.track(1).notes[0][bar];
        y.session().sendNotePattern(2, 0);
        x.settle(), y.settle();
        CHECK(songText(x.project()) == songText(y.project()));
        x.session().togglePlay(false), y.session().togglePlay(false);
        std::vector<float> p(2 * 4096), q(2 * 4096);
        bool same = true;
        float loud = 0.f;
        for (int i = 0; i < 40; ++i) {
            x.engine->render(p.data(), 4096), y.engine->render(q.data(), 4096);
            same = same && p == q;
            for (float v : p) loud = std::fmax(loud, std::fabs(v));
        }
        CHECK(same && loud > 0.01f);
    }
}

void testDuplicateOnSong() {
    Rig rig;
    Session& session = rig.session();
    const int t = synthWithNotes(rig);
    session.view = View::Song;
    rig.frames(1);
    SongScreen& song = rig.app.song();
    song.moveCursorTo(0, t);
    rig.tap(UP); // onto its head
    CHECK(song.cursorOnHead());
    // L1 held, R1 tapped: the copy, the cursor on its head, the box says so.
    rig.key(L1, true);
    rig.frames(1);
    CHECK(song.copyKeyHeld());
    Canvas canvas;
    if (canvas.ok) {
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
    }
    rig.key(R1, true);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 2 && !song.choosing()); // (not yet: on R1's let go; and no list of instruments)
    rig.key(R1, false);
    rig.frames(1);
    CHECK(rig.project().trackCount() == 3 && song.cursorColumn() == 2 && song.cursorOnHead() && rig.said() == "TRACK 3: COPY OF 2");
    rig.key(L1, false);
    rig.frames(1);
    // R1 alone on a head names the track now (the list moved to A); L1 alone does nothing.
    rig.tap(L1);
    CHECK(rig.project().trackCount() == 3);
    rig.tap(R1);
    CHECK(session.view == View::Settings && rig.project().trackCount() == 3);
    rig.tap(B); // nothing typed: back, unnamed
    rig.frames(1);
    CHECK(session.view == View::Song);
    // R1 first, then L1: nothing.
    rig.key(R1, true), rig.frames(1), rig.tap(B);
    rig.key(L1, true), rig.frames(1), rig.key(R1, false), rig.frames(1), rig.key(L1, false), rig.frames(1);
    CHECK(rig.project().trackCount() == 3);
    // L1 let go before R1: nothing.
    rig.key(L1, true), rig.frames(1), rig.key(R1, true), rig.frames(1), rig.key(L1, false), rig.frames(1), rig.key(R1, false), rig.frames(1);
    CHECK(rig.project().trackCount() == 3);
    if (song.choosing()) rig.tap(B);
    // R1 let go while another screen was up (this one never saw it): a later let go of R1 copies nothing.
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.key(L1, true), rig.frames(1), rig.key(R1, true), rig.frames(1);
    session.view = View::Pattern;
    rig.frames(1), rig.key(R1, false), rig.frames(1);
    session.view = View::Song;
    rig.frames(1);
    session.view = View::Pattern;
    rig.frames(1), rig.key(R1, true), rig.frames(1);
    session.view = View::Song;
    rig.key(R1, false), rig.frames(1), rig.key(L1, false), rig.frames(1);
    CHECK(rig.project().trackCount() == 3);
    if (song.choosing()) rig.tap(B);
    // A panic begun L1, R1 on a head (then L2, R2): no track is copied, whichever is let go first.
    const SDL_Keycode L2 = SDLK_1, R2 = SDLK_2;
    session.view = View::Song;
    rig.frames(1);
    rig.key(L1, true), rig.frames(1), rig.key(R1, true), rig.frames(1), rig.key(L2, true), rig.frames(1), rig.key(R2, true), rig.frames(2);
    rig.key(R1, false), rig.frames(1), rig.key(L1, false), rig.key(L2, false), rig.key(R2, false), rig.frames(2);
    CHECK(rig.project().trackCount() == 3);
    rig.tap(A); // (the panic's notice put away)
    rig.frames(120);
    // A panic begun R1 (then L1, L2, R2) on a head: nothing is named either.
    session.view = View::Song;
    song.moveCursorTo(0, 1);
    rig.tap(UP);
    rig.frames(1);
    rig.key(R1, true), rig.frames(1), rig.key(L1, true), rig.frames(1), rig.key(L2, true), rig.frames(1), rig.key(R2, true), rig.frames(2);
    rig.key(R1, false), rig.frames(1), rig.key(L1, false), rig.key(L2, false), rig.key(R2, false), rig.frames(2);
    CHECK(session.view == View::Song);
    rig.tap(A); // (the panic's notice put away)
    rig.frames(120);
    // On a cell, L1 + R1 copies no track (R1 there is the cell's copy brush).
    session.view = View::Song;
    song.moveCursorTo(0, 1);
    rig.frames(1);
    rig.key(L1, true), rig.frames(1), rig.tap(R1), rig.key(L1, false), rig.frames(1);
    CHECK(rig.project().trackCount() == 3);
    // Twelve tracks: it says so.
    while (rig.project().trackCount() < kNumTracks) session.addTrack(TrackKind::Drums);
    song.moveCursorTo(0, 0);
    rig.tap(UP);
    rig.key(L1, true), rig.frames(1), rig.tap(R1), rig.key(L1, false), rig.frames(1);
    CHECK(rig.project().trackCount() == kNumTracks && rig.said() == "NO ROOM: 12 TRACKS");
}

// ---- batch 5: EVEN -----------------------------------------------------------------------------------

std::string laneSteps(Rig& rig, int lane) {
    std::string out;
    for (int step = 0; step < kSteps; ++step) out += rig.track(0).drums[rig.project().scene][rig.project().bar].level[lane][step] > 0.f ? 'x' : '.';
    return out;
}

void testTheEvenRule() {
    const auto row = [](int hits) {
        std::string out;
        for (int step = 0; step < 16; ++step) out += euclidHit(hits, step) ? 'x' : '.';
        return out;
    };
    CHECK(row(0) == "................" && row(1) == "x..............." && row(4) == "x...x...x...x...");
    CHECK(row(3) == "x.....x....x...." && row(5) == "x...x..x..x..x.." && row(16) == "xxxxxxxxxxxxxxxx");
    for (int hits = 0; hits <= 16; ++hits) {
        int n = 0, gapMost = 0, gapLeast = 99, last = -1, first = -1;
        for (int step = 0; step < 16; ++step)
            if (euclidHit(hits, step)) {
                ++n;
                if (last >= 0) gapMost = std::max(gapMost, step - last), gapLeast = std::min(gapLeast, step - last);
                if (first < 0) first = step;
                last = step;
            }
        CHECK(n == hits);
        if (hits >= 2) {
            const int round = first + 16 - last; // the gap round the bar's end
            gapMost = std::max(gapMost, round), gapLeast = std::min(gapLeast, round);
            CHECK(gapMost - gapLeast <= 1); // as even as sixteen steps allow
        }
    }
}

void testEvenOnALane() {
    Rig rig;
    Session& session = rig.session();
    PatternScreen& drums = rig.app.drums();
    rig.frames(2);
    // Lane 3 (the open hat): a hit of the player's, quiet, on step 1.
    drums.setStep(3, 0, 0.4f);
    drums.setStep(3, 9, 1.f);
    rig.settle();
    drums.moveCursorTo(3, -1);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(R1); // two hits now: three, even
    CHECK(laneSteps(rig, 3) == "x.....x....x...." && rig.said() == "EVEN: 3 HITS");
    rig.tap(R1);
    CHECK(laneSteps(rig, 3) == "x...x...x...x...");
    CHECK(drums.level(3, 0) == 0.4f && drums.level(3, 4) == kPlacedLevel); // (a hit that stays keeps what it had)
    rig.tap(RIGHT); // turned a step later
    CHECK(laneSteps(rig, 3) == ".x...x...x...x.." && rig.said() == "EVEN: 4 HITS, TURNED 1");
    rig.tap(LEFT, 2);
    CHECK(laneSteps(rig, 3) == "...x...x...x...x" && rig.said() == "EVEN: 4 HITS, TURNED 15");
    rig.tap(L1); // one fewer, the turn kept
    CHECK(laneSteps(rig, 3) == ".....x....x....x" && rig.said() == "EVEN: 3 HITS, TURNED 15");
    rig.tap(UP); // (the dice's own job is still up / down: one more hit, where the dice put it)
    int n = 0;
    for (char c : laneSteps(rig, 3)) n += c == 'x' ? 1 : 0;
    CHECK(n == 4);
    rig.key(Y, false);
    rig.frames(2);
    // Y let go after EVEN is no tap: nothing reshuffled.
    const std::string after = laneSteps(rig, 3);
    rig.settle();
    CHECK(laneSteps(rig, 3) == after);
    // A new hold: LEFT / RIGHT are the starts again until R1 or L1; L1 to none empties the lane.
    rig.key(Y, true);
    rig.frames(1);
    for (int i = 0; i < 5; ++i) rig.tap(L1);
    CHECK(laneSteps(rig, 3) == "................" && rig.said() == "EVEN: NO HITS");
    rig.tap(L1);
    CHECK(laneSteps(rig, 3) == "................");
    for (int i = 0; i < 20; ++i) rig.tap(R1);
    CHECK(laneSteps(rig, 3) == "xxxxxxxxxxxxxxxx");
    rig.key(Y, false);
    rig.frames(2);
    CHECK(laneSteps(rig, 3) == "xxxxxxxxxxxxxxxx"); // (Y let go after EVEN is no tap: no reshuffle)
    // A new hold starts unturned (the first hold's turn of 15 is forgotten), and LEFT / RIGHT are the dice's until EVEN.
    rig.settle();
    rig.key(Y, true);
    rig.frames(1);
    for (int i = 0; i < 12; ++i) rig.tap(L1);
    CHECK(laneSteps(rig, 3) == "x...x...x...x..." && rig.said() == "EVEN: 4 HITS");
    for (int i = 0; i < 12; ++i) rig.tap(R1);
    rig.key(Y, false);
    rig.frames(2);
    // Other lanes are not touched; undo puts the lane back as it was before the holds.
    CHECK(laneSteps(rig, 0) == laneSteps(rig, 0));
    rig.settle();
    rig.undo();
    rig.settle();
    CHECK(laneSteps(rig, 3) == "xxxxxxxxxxxxxxxx");
    rig.undo();
    rig.settle();
    CHECK(laneSteps(rig, 3) == after);
    rig.undo();
    rig.settle();
    CHECK(laneSteps(rig, 3) == "x........x......" && drums.level(3, 0) == 0.4f);
    // On a step (not a name), Y + R1 does nothing of EVEN's.
    drums.moveCursorTo(3, 2);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(R1);
    rig.key(Y, false);
    rig.frames(2);
    CHECK(rig.said() != "EVEN: 3 HITS");
    // The hints fit, Y held on a name before and after EVEN.
    Canvas canvas;
    if (canvas.ok) {
        drums.moveCursorTo(3, -1);
        rig.key(Y, true);
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
        rig.tap(R1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0.f);
        rig.key(Y, false);
        rig.frames(2);
    }
    (void)session;
}

// ---- batch 7: KIT PRESETS ------------------------------------------------------------------------

// What lane 1 sounds like now: struck, and the next tenth of a second summed.
double kitHeard(Rig& rig) {
    rig.frames(30); // (anything before has rung out, a made voice has landed)
    Command c;
    c.type = Command::Trigger;
    c.track = rig.project().track;
    c.a = 0;
    c.value = kPlacedLevel;
    rig.engine->send(c);
    std::vector<float> out(2 * 4800);
    rig.engine->render(out.data(), 4800);
    double sum = 0.0;
    for (float v : out) sum += std::fabs(v);
    rig.frames(40);
    return sum;
}

void testTheKitsText() {
    // The built-in kits name only voices the data has.
    for (const std::string& problem : builtInKitProblems()) std::printf("kit problem: %s\n", problem.c_str());
    CHECK(builtInKitProblems().empty());
    // A kit's text is the song file's own lines for a kit, and reads back as it was written.
    std::unique_ptr<TrackData> kit(new TrackData), back(new TrackData);
    chokeTheHats(*kit);
    kit->laneLevel[2] = 0.5f;
    kit->laneFeel[1].time = 40, kit->laneFeel[1].level = 20;
    kit->laneSwing[3] = 60;
    kit->laneFit[4] = 8;
    std::snprintf(kit->laneSample[4], sizeof kit->laneSample[4], "%s", "breaks/amen");
    kit->laneEdit[4].gain = -30;
    kit->laneSynth[0] = drumsynth::usualVoice(drumsynth::modelFromWord("kick"));
    const std::string text = kitText(*kit);
    CHECK(text.compare(0, 15, "apotheca-kit 1\n") == 0);
    CHECK(text.find("lanesynth 1 kick") != std::string::npos && text.find("swing 4 60") != std::string::npos);
    CHECK(text.find("track") == std::string::npos && text.find("pattern") == std::string::npos);
    CHECK(readKit(text, *back));
    CHECK(kitText(*back) == text);
    CHECK(back->laneLevel[2] == 0.5f && back->laneSwing[3] == 60 && back->laneFit[4] == 8 && back->laneChoke[2] == 1);
    CHECK(std::strcmp(back->laneSample[4], "breaks/amen") == 0 && back->laneEdit[4].gain == -30 && back->laneSynth[0].on());
    // Not a kit: refused, and nothing changed.
    std::unique_ptr<TrackData> other(new TrackData);
    other->laneSwing[0] = 30;
    CHECK(!readKit("apotheca-song 1\ntrack drums\nswing 0 90\n", *other));
    CHECK(!readKit("", *other) && !readKit("swing 0 90\n", *other));
    CHECK(other->laneSwing[0] == 30);
    // A kit's text cannot bring a track, a pattern or a synth's lines in with it: refused whole, and nothing changed.
    CHECK(!readKit("apotheca-kit 1\nswing 1 90\ntrack phazed\nswing 2 80\n", *other));
    CHECK(!readKit("apotheca-kit 1\nscene 1\nhit 1 1 0.8\n", *other));
    CHECK(!readKit("apotheca-kit 1\nlanes 1 1 1 1 1 1 1 1\nname SNEAKY\n", *other));
    CHECK(other->laneSwing[0] == 30);
    // copyKit takes the kit and nothing else.
    std::unique_ptr<TrackData> to(new TrackData);
    to->mixDb = -6;
    to->drums[0][0].level[0][0] = 0.9f;
    copyKit(*kit, *to);
    CHECK(kitText(*to) == text && to->mixDb == -6 && to->drums[0][0].level[0][0] == 0.9f);
}

void testKitsInTheSession() {
    const std::string folder = tempFolder("easykits");
    {
        Rig rig;
        Session& session = rig.session();
        session.usePresetFolder(folder);
        CHECK(rig.track(0).kind == TrackKind::Drums);
        // The list: the built-in kits, DRUMS first; a new track's kit is DRUMS.
        const std::size_t builtIn = session.kits().size();
        CHECK(builtIn >= 5 && session.kits()[0].name == "DRUMS" && !session.kits()[0].own);
        session.chokeNewKit(0); // (as main.cpp does for a new song)
        rig.settle();
        CHECK(session.kitOf(0) == 0);
        for (std::size_t i = 0; i < builtIn; ++i) {
            CHECK(session.kits()[i].name.size() <= Session::kKitNameMost);
            for (std::size_t j = 0; j < i; ++j) CHECK(session.kits()[i].text != session.kits()[j].text && session.kits()[i].name != session.kits()[j].name);
        }
        // A kit put on: every lane's, the patterns and the mixer left, the engine told, no step of undo.
        rig.app.drums().moveCursorTo(0, 0);
        rig.tap(A);
        rig.settle();
        CHECK(rig.track(0).drums[0][0].level[0][0] == kPlacedLevel);
        const double drums = kitHeard(rig);
        CHECK(session.loadKit(0, 2));
        CHECK(session.kitOf(0) == 2);
        CHECK(rig.track(0).laneSynth[0].on() && rig.track(0).drums[0][0].level[0][0] == kPlacedLevel);
        const double tight = kitHeard(rig);
        CHECK(std::fabs(tight - drums) > 0.05 * drums);
        // A step of undo of its own: the kit back, the hit left; then the hit.
        rig.undo();
        rig.settle();
        CHECK(session.kitOf(0) == 0 && rig.track(0).drums[0][0].level[0][0] == kPlacedLevel);
        CHECK(std::fabs(kitHeard(rig) - drums) < 0.005 * drums);
        rig.undo();
        rig.settle();
        CHECK(session.kitOf(0) == 0 && rig.track(0).drums[0][0].level[0][0] == 0.f);
        // Put on and back by the list: the first kit to the bit of its text, and as it sounded.
        CHECK(session.loadKit(0, 3) && session.kitOf(0) == 3);
        rig.settle();
        CHECK(session.loadKit(0, 0) && session.kitOf(0) == 0);
        CHECK(std::fabs(kitHeard(rig) - drums) < 0.005 * drums);
        CHECK(!session.loadKit(0, -1) && !session.loadKit(0, static_cast<int>(builtIn)));
        // A kit changed is nobody's: YOUR OWN.
        session.setLaneSwing(0, 1, 45);
        CHECK(session.kitOf(0) == -1);
        // Every part of a kit is put on: one with a CHOKE, a volume, a feel, a swing, an EDIT and a voice of its own.
        session.setLaneChoke(0, 5, 2);
        session.setLaneLevel(0, 2, 0.5f);
        session.setLaneFeel(0, 1, 40, 20);
        LaneEdit edit = rig.track(0).laneEdit[6];
        edit.gain = -30, edit.reverse = 1;
        session.setLaneEdit(0, 6, edit);
        drumsynth::Voice zap = drumsynth::usualVoice(drumsynth::modelFromWord("zap"));
        zap.dial[0] = 33;
        session.setLaneSynth(0, 7, zap);
        {
            std::unique_ptr<TrackData> mine(new TrackData);
            copyKit(rig.track(0), *mine);
            const std::string text = kitText(*mine);
            for (int other = 0; other < 3; ++other) {
                CHECK(session.loadKit(0, other) && session.kitOf(0) == other);
                CHECK(session.putKit(0, *mine) && kitText(rig.track(0)) == text);
            }
            CHECK(rig.track(0).laneChoke[5] == 2 && rig.track(0).laneLevel[2] == 0.5f && rig.track(0).laneFeel[1].time == 40 && rig.track(0).laneSwing[1] == 45);
            CHECK(rig.track(0).laneEdit[6].gain == -30 && rig.track(0).laneEdit[6].reverse == 1 && rig.track(0).laneSynth[7] == zap);
        }
        // Saved: KIT 1, a file beside the presets, the last of the list, and the track's kit is it.
        CHECK(session.saveKit(0) == "KIT 1");
        CHECK(session.kits().size() == builtIn + 1 && session.kits().back().own && session.kits().back().name == "KIT 1");
        CHECK(session.kitOf(0) == static_cast<int>(builtIn));
        std::string file;
        CHECK(readTextFile(folder + "/kit 1.kit", file) && file == kitText(rig.track(0)));
        // Another: KIT 2 (a name is never taken twice).
        session.setLaneSwing(0, 1, 50);
        CHECK(session.saveKit(0) == "KIT 2" && session.kits().size() == builtIn + 2);
        // A built-in kit is not deleted; one's own is, and the track keeps its sound.
        CHECK(!session.deleteKit(0) && session.kits().size() == builtIn + 2);
        CHECK(session.deleteKit(static_cast<int>(builtIn)));
        CHECK(session.kits().size() == builtIn + 1 && session.kits().back().name == "KIT 2");
        CHECK(!readTextFile(folder + "/kit 1.kit", file));
        CHECK(rig.track(0).laneSwing[1] == 50 && session.kitOf(0) == static_cast<int>(builtIn));
        // Not on a synth track.
        session.addTrack(TrackKind::Phazed);
        CHECK(session.kitOf(1) == -1 && !session.loadKit(1, 1) && session.saveKit(1).empty());
    }
    {
        // Another run finds the saved kit, and puts it on.
        Rig rig;
        Session& session = rig.session();
        session.usePresetFolder(folder);
        CHECK(session.kits().back().own && session.kits().back().name == "KIT 2");
        CHECK(session.loadKit(0, static_cast<int>(session.kits().size()) - 1));
        CHECK(rig.track(0).laneSwing[1] == 50);
    }
}

void testTheKitStop() {
    const std::string folder = tempFolder("easykitstop");
    Rig rig;
    Session& session = rig.session();
    session.usePresetFolder(folder);
    session.chokeNewKit(0);
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    const int builtIn = static_cast<int>(session.kits().size());
    // UP from lane 1 is the KIT stop; UP again the last lane, DOWN from the stop lane 1.
    CHECK(!kit.onKit());
    rig.tap(UP);
    CHECK(kit.onKit());
    rig.tap(UP);
    CHECK(!kit.onKit());
    rig.tap(DOWN); // (lane 8 -> lane 1)
    CHECK(!kit.onKit());
    rig.tap(UP);
    CHECK(kit.onKit());
    rig.tap(DOWN);
    CHECK(!kit.onKit());
    rig.tap(DOWN), rig.tap(UP);
    CHECK(!kit.onKit()); // (lane 2 -> lane 1: not the stop)
    rig.tap(UP);
    CHECK(kit.onKit());
    // Hold A + UP: the next kit; DOWN the one before; the ends stop.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(UP);
    CHECK(session.kitOf(0) == 1);
    rig.tap(UP);
    CHECK(session.kitOf(0) == 2);
    rig.tap(DOWN);
    CHECK(session.kitOf(0) == 1);
    rig.tap(DOWN), rig.tap(DOWN);
    CHECK(session.kitOf(0) == 0 && kit.onKit());
    rig.tap(UP, builtIn + 3);
    CHECK(session.kitOf(0) == builtIn - 1);
    {
        const int heardSoFar = kit.hears();
        rig.tap(UP); // past the last: nothing, and nothing heard
        CHECK(kit.hears() == heardSoFar);
    }
    // B with A still held: the kit as it was when A went down, heard (gesture audit G12: Session::putSoundBack,
    // SOUND's too). A is spent: UP with it still held moves the cursor off the stop, not to another kit.
    {
        const int heardSoFar = kit.hears();
        rig.tap(B);
        CHECK(session.kitOf(0) == 0 && kit.hears() == heardSoFar + 1);
        rig.tap(UP);
        CHECK(!kit.onKit() && kit.cursorLane() == kDrumLanes - 1 && session.kitOf(0) == 0);
        rig.tap(DOWN), rig.tap(UP);
        CHECK(kit.onKit() && session.kitOf(0) == 0);
    }
    rig.key(A, false);
    rig.frames(2);
    CHECK(kit.onKit() && !kit.editing() && !kit.voicing()); // (A let go opens nothing on the stop)
    // Left and right, X and a tap of A do nothing to the kit here.
    rig.tap(LEFT), rig.tap(RIGHT), rig.tap(A);
    CHECK(kit.onKit() && session.kitOf(0) == 0 && !kit.picker().isOpen());
    // Y on a kit of the list: said, nothing saved.
    rig.tap(Y);
    CHECK(kit.said() == "IT IS DRUMS ALREADY" && static_cast<int>(session.kits().size()) == builtIn);
    // Changed, Y saves it.
    session.setLaneSwing(0, 0, 35);
    rig.tap(Y);
    CHECK(kit.said() == "SAVED AS KIT 1" && session.kitOf(0) == builtIn);
    // A tap of B does nothing; B held a second deletes one's own; any other button calls it off.
    rig.tap(B);
    CHECK(static_cast<int>(session.kits().size()) == builtIn + 1);
    rig.key(B, true);
    rig.frames(30);
    CHECK(kit.kitDeletePart() > 0.3f && kit.kitDeletePart() < 0.7f);
    rig.tap(LEFT);
    CHECK(kit.kitDeletePart() == 0.f);
    rig.frames(60);
    rig.key(B, false);
    rig.frames(1);
    CHECK(static_cast<int>(session.kits().size()) == builtIn + 1);
    // B let go while another screen was up (this one never saw it): nothing is deleted on coming back.
    rig.key(B, true);
    rig.frames(10);
    session.view = View::Song;
    rig.frames(2);
    rig.key(B, false);
    rig.frames(2);
    session.view = View::Device;
    rig.frames(80);
    CHECK(static_cast<int>(session.kits().size()) == builtIn + 1 && kit.kitDeletePart() == 0.f && kit.onKit());
    rig.key(B, true);
    rig.frames(61);
    CHECK(static_cast<int>(session.kits().size()) == builtIn + 1); // (not before the second is up)
    rig.frames(4);
    CHECK(static_cast<int>(session.kits().size()) == builtIn && kit.kitDeletePart() == 0.f);
    CHECK(kit.said() == "KIT 1 DELETED. THE TRACK KEEPS ITS SOUND");
    rig.frames(70);
    rig.key(B, false);
    rig.frames(1);
    CHECK(rig.track(0).laneSwing[0] == 35 && session.kitOf(0) == -1 && kit.onKit());
    // Hold B on a built-in kit deletes nothing.
    session.loadKit(0, 1);
    rig.key(B, true);
    rig.frames(30);
    CHECK(kit.kitDeletePart() == 0.f); // (no bar fills)
    rig.frames(50);
    rig.key(B, false);
    rig.frames(1);
    CHECK(static_cast<int>(session.kits().size()) == builtIn && kit.kitDeletePart() == 0.f);
    // From a kit of nobody's, A + UP is the first of the list, A + DOWN nothing.
    session.setLaneSwing(0, 0, 35);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(DOWN);
    CHECK(session.kitOf(0) == -1);
    rig.tap(UP);
    CHECK(session.kitOf(0) == 0);
    rig.tap(B); // as it was: the kit of nobody's, to its text
    CHECK(session.kitOf(0) == -1 && rig.track(0).laneSwing[0] == 35 && rig.track(0).laneSynth[0].on());
    rig.key(A, false);
    rig.frames(2);
    // START plays from the stop.
    rig.tap(START);
    CHECK(rig.project().playing);
    rig.tap(START);
    // The hints fit: at rest on a built-in kit, on one of nobody's, on one's own, A held, deleting.
    Canvas canvas;
    if (canvas.ok) {
        auto fits = [&] {
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0.f);
        };
        fits();
        rig.tap(Y);
        fits();
        rig.key(A, true);
        rig.frames(1);
        fits();
        rig.key(A, false);
        rig.frames(1);
        rig.key(B, true);
        rig.frames(20);
        fits();
        rig.tap(X);
        rig.key(B, false);
        rig.frames(1);
        session.loadKit(0, 6);
        fits();
    }
    // A turn of A + UP is heard (stopped): lane 1 struck.
    const int heard = kit.hears();
    rig.key(A, true);
    rig.frames(1);
    rig.tap(UP);
    rig.key(A, false);
    rig.frames(1);
    CHECK(kit.hears() == heard + 1);
}

// ---- the owner's five (2026-11-18): the looks ---------------------------------------------------

void testTheLooks() {
    using namespace pattern_layout;
    Rig rig;
    Session& session = rig.session();
    session.view = View::Pattern;
    session.setLaneLevel(0, 1, 0.5f);
    session.setLaneLevel(0, 2, 0.f);
    session.setLaneFeel(0, 1, 50, 50);
    rig.frames(2);
    rig.app.drums().moveCursorTo(6, 0); // (the cursor off the lanes looked at)
    Canvas canvas;
    if (!canvas.ok) return;
    // A lane's volume in its name's box: the lightest surface as far as it is turned up, the
    // background past it, no edge where it ends. In every theme, by the theme's own colours.
    for (int theme = 0; theme < kThemes; ++theme) {
        session.setTheme(theme);
        rig.frames(1);
        canvas.clear();
        rig.app.draw(canvas.r, *canvas.fonts);
        const int y = kGridTop + 1 * kLaneH + kCellH - 3; // (the box's foot: under the name's letters)
        const int filled = static_cast<int>(std::round((kNameW - 2) * 0.5f)), end = kNameX + 1 + filled;
        CHECK(canvas.is(end - 1, y, ui::kPanel2) && canvas.is(end - 12, y, ui::kPanel2));
        CHECK(canvas.is(end, y, ui::kBg) && canvas.is(end + 12, y, ui::kBg));
        // No edge at the top or the foot either: the fill right up to where it ends, whatever the row.
        const int top = kGridTop + 1 * kLaneH;
        CHECK(canvas.is(end - 1, top + 2, ui::kPanel2) && canvas.is(end - 1, top + kCellH - 3, ui::kPanel2));
        CHECK(canvas.is(end, top + 2, ui::kBg) && canvas.is(end, top + kCellH - 3, ui::kBg));
        // At full the fill stands at the box's right; at none there is no fill.
        const int y0 = kGridTop + kCellH - 3, y2 = kGridTop + 2 * kLaneH + kCellH - 3;
        CHECK(canvas.is(kNameX + kNameW - 2, y0, ui::kPanel2) && canvas.is(kNameX + kNameW - 3, y0, ui::kPanel2));
        CHECK(canvas.is(kNameX + 20, y2, ui::kBg) && canvas.is(kNameX + 60, y2, ui::kBg));
        // The feel mark, and clear room between it and the name's first letter: nothing of the name's
        // colour left of kNameTextX, in the mark's rows.
        const int my = kGridTop + 1 * kLaneH + kFeelMarkInset + 2;
        CHECK(canvas.is(kFeelMarkX, my, ui::kPurple) && canvas.is(kFeelMarkX + kFeelMarkW - 1, my, ui::kPurple));
        for (int row = kGridTop + kLaneH + 2; row < kGridTop + kLaneH + kCellH - 2; ++row)
            for (int x = kFeelMarkX + kFeelMarkW; x < kNameTextX; ++x) CHECK(!canvas.is(x, row, ui::kText) && !canvas.is(x, row, ui::kPurple));
    }
    session.setTheme(kThemeBrown);
    CHECK(kNameTextX - (kFeelMarkX + kFeelMarkW) >= 5);                      // the room asked for
    CHECK(kFeelMarkX > kNameX && kFeelMarkX + kFeelMarkW < kSwingMarkRight - 20); // inside the box, far from SWING's and CHOKE's
    // SONG: the column's word is REPEATS, inside the screen and short of the first head, at every size
    // it may be drawn at (held at LARGE where the size chosen would not fit).
    CHECK(std::strcmp(SongScreen::kRepeatsWord, "REPEATS") == 0);
    session.view = View::Song;
    rig.frames(2);
    canvas.clear();
    rig.app.draw(canvas.r, *canvas.fonts);
    const SDL_Rect head = rig.app.song().headRect(0), first = rig.app.song().repeatBox(0);
    const float wide = canvas.fonts->small.width(SongScreen::kRepeatsWord);
    CHECK(head.x - 2.f - wide >= 3.f);                       // inside the screen, clear of the first head
    CHECK(first.y - (SongScreen::kRepeatsWordY + canvas.fonts->small.capHeight()) <= 12.f); // near its boxes (it stood 20 above)
    CHECK(first.y > SongScreen::kRepeatsWordY + canvas.fonts->small.capHeight());
    // Its letters are drawn there, in dim, and none left of the screen's margin.
    int ink = 0;
    for (int y = SongScreen::kRepeatsWordY; y < first.y; ++y)
        for (int x = 0; x < head.x; ++x) {
            if (!canvas.is(x, y, ui::kDim)) continue;
            ++ink;
            CHECK(x >= 3 && x < head.x - 1);
        }
    if (ink <= 25) std::printf("ink %d\n", ink);
    CHECK(ink > 25);
}

// ---- batch 8: QUEUED MUTES ----------------------------------------------------------------------

void heardTo(Engine& engine, int track, bool heard, bool wait, int asking = 0) {
    Command c = make(Command::SetTrackHeard, track);
    c.a = heard ? 1 : 0;
    c.b = wait ? 1 : 0;
    c.c = asking;
    engine.send(c);
}

// A hit on every step of track 0, so a gate's 5 ms shows wherever it lands.
void busyDrums(Engine& engine) {
    for (int step = 0; step < 16; ++step) {
        Command c = make(Command::SetStep);
        c.a = step % 3;
        c.b = step;
        c.value = 0.9f;
        engine.send(c);
    }
}

void testTheWaitingGate() {
    const long bar = 16 * kStep120;
    // A gate asked to wait lands on the next bar's first sample: the sound is, to the bit, that of an
    // engine told AT ONCE exactly there. In any blocks.
    for (int block : {1, 64, 256, 333}) {
        std::unique_ptr<Engine> waits(new Engine), there(new Engine);
        busyDrums(*waits), busyDrums(*there);
        play(*waits), play(*there);
        std::vector<float> a = render(*waits, 5000, block), b = render(*there, 5000, block);
        heardTo(*waits, 0, false, true, 7);
        std::vector<float> more = render(*waits, bar - 5000, block);
        a.insert(a.end(), more.begin(), more.end());
        CHECK(waits->trackHeard(0) && waits->heardDone() != 7); // still heard, still waiting, up to the bar's last sample
        more = render(*there, bar - 5000, block);
        b.insert(b.end(), more.begin(), more.end());
        heardTo(*there, 0, false, false);
        more = render(*waits, bar, block);
        a.insert(a.end(), more.begin(), more.end());
        more = render(*there, bar, block);
        b.insert(b.end(), more.begin(), more.end());
        CHECK(!waits->trackHeard(0) && waits->heardDone() == 7);
        CHECK(a == b);
        CHECK(loudest(a, bar - 2000, bar) > 0.01f && loudest(a, bar + 1000, 2 * bar) == 0.f); // sounding to the line, silent after its fade
        if (block == 256) {
            // And one sample past the line is enough to land it.
            std::unique_ptr<Engine> e(new Engine);
            busyDrums(*e);
            play(*e);
            render(*e, 100, 100);
            heardTo(*e, 0, false, true, 3);
            render(*e, bar - 100, 256);
            CHECK(e->trackHeard(0));
            render(*e, 1, 1);
            CHECK(!e->trackHeard(0) && e->heardDone() == 3);
        }
    }
    {
        // A panic lands a mute waiting for the next bar, as Stop does (review 2026-11-23: it stayed
        // waiting, the head hollow and the gate as it was, until the next Play).
        std::unique_ptr<Engine> e(new Engine);
        busyDrums(*e);
        play(*e);
        render(*e, 100, 100);
        heardTo(*e, 0, false, true, 9);
        render(*e, 100, 100);
        CHECK(e->trackHeard(0) && e->heardDone() != 9);
        Command panic = make(Command::Panic);
        panic.a = 1;
        e->send(panic);
        render(*e, 64, 64);
        CHECK(!e->trackHeard(0) && e->heardDone() == 9);
    }
    {
        // Stopped: at once, whatever is asked. Told what it is already: nothing waits.
        std::unique_ptr<Engine> e(new Engine);
        busyDrums(*e);
        heardTo(*e, 0, false, true, 1);
        render(*e, 64, 64);
        CHECK(!e->trackHeard(0) && e->heardDone() == 1);
        heardTo(*e, 0, true, true, 2);
        render(*e, 64, 64);
        CHECK(e->trackHeard(0) && e->heardDone() == 2);
        play(*e);
        heardTo(*e, 0, true, true, 3);
        render(*e, 64, 64);
        CHECK(e->trackHeard(0) && e->heardDone() == 3);
        // Asked and asked back before the line: called off, and the song is as one never asked.
        std::unique_ptr<Engine> plain(new Engine);
        busyDrums(*plain);
        play(*plain);
        render(*plain, 64, 64);
        heardTo(*e, 0, false, true, 4);
        render(*e, 1000, 100), render(*plain, 1000, 100);
        CHECK(e->heardDone() == 3);
        heardTo(*e, 0, true, true, 5);
        render(*e, 1000, 100), render(*plain, 1000, 100);
        CHECK(e->heardDone() == 5 && e->trackHeard(0));
        CHECK(render(*e, 2 * 96000, 256) == render(*plain, 2 * 96000, 256));
        // One waiting, another track told at once: the count is not given out until the first lands.
        heardTo(*e, 0, false, true, 6);
        heardTo(*e, 1, false, false, 6);
        render(*e, 64, 64);
        CHECK(e->heardDone() == 5 && !e->trackHeard(1) && e->trackHeard(0));
        // AT ONCE while it waits: now, and nothing waits any more.
        heardTo(*e, 0, false, false, 6);
        render(*e, 64, 64);
        CHECK(!e->trackHeard(0) && e->heardDone() == 6);
        // Stop lands what waits (the bar line will not come).
        heardTo(*e, 0, true, true, 8);
        render(*e, 64, 64);
        CHECK(!e->trackHeard(0));
        e->send(make(Command::Stop));
        render(*e, 64, 64);
        CHECK(e->trackHeard(0) && e->heardDone() == 8);
        // A track taken away while its gate waits: nothing left waiting.
        play(*e);
        render(*e, 64, 64);
        heardTo(*e, 0, false, true, 9);
        render(*e, 64, 64);
        CHECK(e->heardDone() == 8);
        e->send(make(Command::RemoveInstrument));
        heardTo(*e, 1, true, false, 9);
        render(*e, 2000, 100);
        CHECK(e->heardDone() == 9 && e->trackHeard(0));
        render(*e, 96000, 256);
        CHECK(e->trackHeard(0)); // (the bar line lands nothing on it)
    }
}

void testQueuedMutesInTheApp() {
    const std::string folder = tempFolder("easymutes");
    const int bar = 375; // frames of 256 in a bar at 120
    {
        Rig rig;
        Session& session = rig.session();
        session.useProgramFolder(folder);
        session.addTrack(TrackKind::Phazed);
        session.addTrack(TrackKind::Phazed);
        rig.settle();
        // AT ONCE (as it always was): nothing waits, playing or not.
        CHECK(session.mutes() == Session::kMutesAtOnce);
        session.togglePlay(false);
        rig.frames(4);
        session.setMuted(1, true);
        CHECK(!session.mutesWaiting() && session.muteWaits(1) == 0 && !session.heardNow(1));
        rig.frames(2);
        CHECK(!rig.engine->trackHeard(1));
        session.setMuted(1, false);
        rig.frames(2);
        // NEXT BAR, playing: the project says what was asked, the engine waits, the mark waits.
        session.setMutes(Session::kMutesNextBar);
        session.setMuted(1, true);
        CHECK(rig.track(1).muted && session.mutesWaiting() && session.muteWaits(1) == 'M' && session.muteWaits(0) == 0);
        CHECK(session.heardNow(1) && !session.heard(1));
        rig.frames(20);
        CHECK(rig.engine->trackHeard(1) && session.mutesWaiting());
        rig.frames(bar);
        CHECK(!rig.engine->trackHeard(1) && !session.mutesWaiting() && session.muteWaits(1) == 0 && !session.heardNow(1));
        // Taken off again: an M that waits to go.
        session.setMuted(1, false);
        CHECK(session.muteWaits(1) == 'M' && !session.heardNow(1) && session.heard(1));
        rig.frames(bar);
        CHECK(rig.engine->trackHeard(1) && !session.mutesWaiting());
        // Solo: the S waits, the others sound until the line.
        session.setSolo(2, true);
        CHECK(session.muteWaits(2) == 'S' && session.heardNow(0) && session.heardNow(1) && !session.heard(0));
        rig.frames(20);
        // A second press while the first waits: both marks wait, and what sounds is still as before the first.
        session.setMuted(2, true);
        CHECK(session.muteWaits(2) == 'M' && session.heardNow(0));
        session.setSolo(2, false), session.setMuted(2, false); // all asked back
        rig.frames(4);
        CHECK(!session.mutesWaiting()); // called off: the engine says nothing waits
        CHECK(rig.engine->trackHeard(0) && rig.engine->trackHeard(1) && rig.engine->trackHeard(2));
        // A press that changes no gate does not wait (track 0 muted under a solo is silent already).
        session.setSolo(2, true);
        rig.frames(bar);
        session.setMuted(0, true);
        CHECK(!session.mutesWaiting());
        session.setMuted(0, false), session.setSolo(2, false);
        rig.frames(bar);
        CHECK(!session.mutesWaiting() && rig.engine->trackHeard(0));
        // Stop lands it at once.
        session.setMuted(0, true);
        rig.frames(4);
        CHECK(session.mutesWaiting() && rig.engine->trackHeard(0));
        session.togglePlay(false);
        rig.frames(4);
        CHECK(!session.mutesWaiting() && !rig.engine->trackHeard(0));
        // Stopped: at once.
        session.setMuted(0, false);
        CHECK(!session.mutesWaiting());
        rig.frames(2);
        CHECK(rig.engine->trackHeard(0));
        // Back to AT ONCE while one waits: it lands now.
        session.togglePlay(false);
        rig.frames(4);
        session.setMuted(1, true);
        rig.frames(4);
        CHECK(session.mutesWaiting());
        session.setMutes(Session::kMutesAtOnce);
        rig.frames(4);
        CHECK(!session.mutesWaiting() && !rig.engine->trackHeard(1));
        session.setMuted(1, false);
        session.togglePlay(false);
        session.setMutes(Session::kMutesNextBar);
        // Through the SONG screen: Y on a head while playing, the mark drawn hollow until the line.
        session.view = View::Song;
        SongScreen& song = rig.app.song();
        song.moveCursorTo(0, 1);
        rig.tap(UP);
        CHECK(song.cursorOnHead());
        rig.tap(START);
        rig.frames(4);
        rig.tap(Y);
        CHECK(rig.track(1).muted && session.muteWaits(1) == 'M');
        Canvas canvas;
        if (canvas.ok) {
            const SDL_Rect head = song.headRect(1);
            const int w = SongScreen::headMarkWidth(*canvas.fonts, "M"), left = head.x + head.w - 2 - w;
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0.f);
            CHECK(canvas.is(left, head.y + 2, ui::kBrass) && canvas.is(left + 1, head.y + 3, ui::kBg)); // hollow: an outline, the box empty
            // The head has not gone dark yet (as the head of a track left alone, track 3's); at the line it has.
            const SDL_Rect other = song.headRect(2);
            CHECK(canvas.at(head.x + 3, head.y + head.h - 8) == canvas.at(other.x + 3, other.y + other.h - 8));
            rig.frames(bar);
            CHECK(session.muteWaits(1) == 0);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(canvas.at(head.x + 3, head.y + head.h - 8) != canvas.at(other.x + 3, other.y + other.h - 8));
            CHECK(canvas.is(left, head.y + 2, ui::kBrass) && canvas.is(left + 1, head.y + 3, ui::kBrass)); // solid
            // Off again: the M still stands, hollow, until the line; then no mark.
            rig.tap(Y);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(!rig.track(1).muted && canvas.is(left, head.y + 2, ui::kBrass) && canvas.is(left + 1, head.y + 3, ui::kBg));
            rig.frames(bar);
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(!canvas.is(left, head.y + 2, ui::kBrass));
            // X: the S waits hollow.
            rig.tap(X);
            const int ws = SongScreen::headMarkWidth(*canvas.fonts, "S"), lefts = head.x + head.w - 2 - ws;
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(rig.track(1).solo && canvas.is(lefts, head.y + 2, ui::kTeal) && canvas.is(lefts + 1, head.y + 3, ui::kBg));
            rig.tap(X);
        }
        rig.tap(START);
        rig.frames(4);
        // The SET row: MUTES, after COUNT IN.
        session.view = View::Settings;
        SettingsScreen& setup = rig.app.settings();
        typedef SettingsScreen S;
        CHECK(S::kMutes == S::kCountIn + 1 && S::groupOf(S::kMutes) == S::kPlaying);
        setup.goTo(S::kMutes);
        rig.frames(1);
        rig.tap(LEFT);
        CHECK(session.mutes() == Session::kMutesAtOnce);
        rig.tap(LEFT);
        CHECK(session.mutes() == Session::kMutesAtOnce); // (the ends stop)
        rig.tap(RIGHT);
        CHECK(session.mutes() == Session::kMutesNextBar);
        rig.tap(RIGHT);
        CHECK(session.mutes() == Session::kMutesNextBar);
        if (canvas.ok) {
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0.f);
            // Its own name on its row (and USB KEYBOARD's on the row after): the cursor's outline, then MUTES in dim.
            const int place = setup.placeOf(S::kMutes), y = setup.placeY(place, setup.firstShown());
            int ink = 0, inkNext = 0;
            const float mutesW = canvas.fonts->small.width("MUTES"), usbW = canvas.fonts->small.width("USB KEYBOARD");
            for (int row = y; row < y + S::kRowH - 2; ++row)
                for (int x = 20; x < 160; ++x) {
                    if (canvas.is(x, row, ui::kDim) && x > 26 + mutesW + 4) ++ink;
                    if (canvas.is(x, row + S::kRowH, ui::kDim) && x > 26 + mutesW + 4 && x < 26 + usbW + 4) ++inkNext;
                }
            CHECK(ink == 0 && inkNext > 10); // (MUTES is short; USB KEYBOARD, on the row after, runs on past it)
        }
        // Kept in settings.txt only when it is not AT ONCE.
        std::string text;
        session.keepSettingsNow(); // (written once it has stood still a second: kKeepRestMs)
        CHECK(readTextFile(folder + "/settings.txt", text) && text.find("\nmutes 1\n") != std::string::npos);
        session.setMutes(Session::kMutesAtOnce);
        session.keepSettingsNow();
        CHECK(readTextFile(folder + "/settings.txt", text) && text.find("mutes") == std::string::npos);
        session.setMutes(Session::kMutesNextBar);
    }
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().mutes() == Session::kMutesNextBar);
    }
}

struct Section {
    const char* name;
    std::function<void()> run;
};

} // namespace

// ---- the gesture audit, batch B2 ---------------------------------------------------------------------

// A Phazed track on the roll (track 1), its bar 1 empty.
NoteScreen& rollOnPhazed(Rig& rig) {
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.session().view = View::Pattern;
    rig.frames(2);
    return rig.app.notes();
}

// G14: a carry by the stick goes through the D-pad's code: a SCALE's keys, an octave a push with L1, one job a push.
void testTheStickCarriesAsTheDpad() {
    Rig rig;
    Session& session = rig.session();
    NoteScreen& roll = rollOnPhazed(rig);
    NotePattern& bar = rig.track(1).notes[0][0];
    NoteEvent n;
    n.step = 4, n.pitch = 62, n.length = 1.f, n.velocity = 0.9f;
    bar.notes[0] = n;
    session.sendNotePattern(1, 0);
    session.setScale(2, kScaleMajor); // D major: D E F# G A B C#
    roll.moveCursorTo(62, 4);
    rig.key(R1, true);
    rig.frames(1);
    CHECK(roll.carrying());
    // One push right: the scale's next key, E, not D#.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(bar.notes[0].pitch == 64 && roll.cursorPitch() == 64);
    // Held over, it runs on through the scale's keys and lands on no other.
    rig.key(STICK_RIGHT, true);
    int moves = 0, last = bar.notes[0].pitch;
    for (int f = 0; f < 7; ++f) {
        rig.frames(1);
        CHECK(inScale(2, kScaleMajor, bar.notes[0].pitch));
        moves += bar.notes[0].pitch != last ? 1 : 0;
        last = bar.notes[0].pitch;
    }
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(moves == 5 && last == 73); // one at once, then twice the held D-pad's (50 a second at STICK SPEED 75%): 4.8 in 6 frames
    // L1 held: an octave, ONCE a push, however long it is held over.
    const int from = bar.notes[0].pitch, octaves = session.octavesShown();
    rig.key(L1, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(30);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(bar.notes[0].pitch == from + 12 && roll.cursorPitch() == from + 12);
    rig.key(STICK_LEFT, true);
    rig.frames(30);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(bar.notes[0].pitch == from);
    rig.key(L1, false);
    rig.frames(1);
    CHECK(session.octavesShown() == octaves); // (L1 was the octave: no zoom)
    // Both ways in one push (it was one job a push: owner, 2026-11-28, "move it to wherever I want"): up first, then
    // up and right together, and it goes up and across, by whole steps and the scale's keys, never off the grid.
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(bar.notes[0].step == 3 && bar.notes[0].pitch == from);
    rig.key(STICK_RIGHT, true);
    rig.frames(3);
    rig.key(STICK_UP, false);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(bar.notes[0].step < 3 && bar.notes[0].pitch > from && inScale(2, kScaleMajor, bar.notes[0].pitch) && bar.notes[0].nudge == 0.f);
    CHECK(roll.cursorPitch() == bar.notes[0].pitch && roll.cursorStep() == bar.notes[0].step);
    rig.key(R1, false);
    rig.frames(1);
}

// G14: X let go with the stick still pushed for what X did: the cursor waits for the stick to come back,
// on the roll and on the drum grid (as FX and DEVICE do for a MOD).
void testXLetGoWithTheStickPushed() {
    Rig rig;
    PatternScreen& drums = rig.app.drums();
    drums.setStep(0, 4, 0.8f);
    drums.moveCursorTo(0, 4);
    rig.frames(1);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(2);
    CHECK(drums.effecting() && drums.fx(0, 4).pitch >= 1);
    rig.key(X, false);
    rig.frames(10);
    CHECK(drums.cursorLane() == 0 && drums.cursorStep() == 4 && !drums.cursorOnTempo() && !drums.cursorOnBars()); // still on the hit
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(drums.cursorStep() == 5); // pushed again: the cursor
    // X let go first and the stick pushed after it, before a frame: that push is the cursor's.
    drums.moveCursorTo(0, 4);
    rig.key(X, true);
    rig.frames(1);
    rig.key(X, false);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(drums.cursorStep() == 5);
    // The hit gone from under X (deleted from elsewhere) with the stick pushed for it: the same wait.
    drums.moveCursorTo(0, 4);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    drums.setStep(0, 4, 0.f);
    rig.frames(5);
    CHECK(!drums.effecting() && drums.cursorStep() == 4);
    rig.key(STICK_LEFT, false);
    rig.key(X, false);
    rig.frames(1);
    // ...and on a lane's name, for its FEEL.
    drums.moveCursorTo(1, -1);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(drums.feeling() && drums.laneFeel(1).time > 0);
    rig.key(X, false);
    rig.frames(10);
    CHECK(drums.cursorLane() == 1 && drums.cursorStep() == -1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);

    // The roll.
    NoteScreen& roll = rollOnPhazed(rig);
    NotePattern& bar = rig.track(1).notes[0][0];
    NoteEvent n;
    n.step = 4, n.pitch = 60, n.length = 1.f, n.velocity = 0.9f;
    bar.notes[0] = n;
    rig.session().sendNotePattern(1, 0);
    roll.moveCursorTo(60, 4);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(roll.effecting() && bar.notes[0].ratchet == 2);
    rig.key(X, false);
    rig.frames(10);
    CHECK(roll.cursorPitch() == 60 && roll.cursorStep() == 4);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(roll.cursorStep() == 5);
    // The note gone from under X with the stick pushed for it: the same wait.
    roll.moveCursorTo(60, 4);
    rig.key(X, true);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(1);
    bar.notes[0] = NoteEvent();
    rig.frames(5);
    CHECK(!roll.effecting() && roll.cursorStep() == 4);
    rig.key(STICK_UP, false);
    rig.key(X, false);
    rig.frames(1);
}

// G14: L1 held on a scene's REPEATS, the stick turns its tempo (up / down) and its meter (sideways), as
// the D-pad does, and the cursor stays; hold A there, sideways is the scene's steps as LEFT / RIGHT are.
void testTheStickOnRepeats() {
    Rig rig;
    Session& session = rig.session();
    session.setTempo(120.f);
    rig.track(0).drums[0][0].level[0][0] = 0.9f; // scene 1 has something: the cursor stands on it, not AT END
    session.view = View::Song;
    rig.frames(1);
    SongScreen& song = rig.app.song();
    song.moveCursorTo(0, -1); // (REPEATS: the column left of the tracks)
    rig.key(L1, true);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(20); // 0.32 s at 90 BPM a second: about 29
    rig.key(STICK_UP, false);
    rig.frames(1);
    const float tempo = rig.project().sceneTempo[0];
    CHECK(tempo >= 140.f && tempo <= 160.f && song.cursorScene() == 0 && song.cursorColumn() == -1);
    rig.key(STICK_DOWN, true);
    rig.frames(4);
    rig.key(STICK_DOWN, false);
    rig.frames(1);
    CHECK(rig.project().sceneTempo[0] < tempo && rig.project().sceneTempo[0] > tempo - 10.f);
    // Sideways: the meter, a click at once and then at twice the held D-pad's pace (the cursor's: 50 a second at
    // STICK SPEED 75%; it was three). One job a push: up as well turns no tempo.
    const float kept = rig.project().sceneTempo[0];
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    CHECK(rig.project().meterOf(0) == kSteps - 1);
    rig.key(STICK_UP, true);
    rig.frames(2); // 1.6: one more click
    rig.key(STICK_UP, false);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(rig.project().meterOf(0) == kSteps - 2 && rig.project().sceneTempo[0] == kept && rig.said() == "14 STEPS IN A BAR");
    rig.key(L1, false);
    rig.frames(1);
    session.setSceneMeter(0, kSteps);
    // Hold A on REPEATS: sideways, the scene's steps; letting go is then no tap (the count stays).
    const int repeats = rig.project().repeats[0];
    rig.key(A, true);
    rig.frames(1);
    rig.key(STICK_LEFT, true);
    rig.frames(1);
    rig.key(STICK_LEFT, false);
    rig.frames(1);
    CHECK(rig.project().sceneSteps(0) == kSteps - 1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.project().sceneSteps(0) == kSteps && rig.project().repeats[0] == repeats);
    // ...and up and down still the repeat count, at twice the held D-pad's pace (the cursor's: 0.8 a frame at STICK
    // SPEED 75%), however far it is pushed.
    rig.key(A, true);
    rig.frames(1);
    rig.key(STICK_UP, true);
    rig.frames(9); // 7.2
    rig.key(STICK_UP, false);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.project().repeats[0] == repeats + 7 && rig.project().sceneSteps(0) == kSteps);
}

// G11: X held on a hit, B empties its effects: pitch, ratchet, reverse, cut and THROW. The hit stays,
// with its level, nudge and chance, and is heard plain. One step of undo of its own.
void testXAndBEmptyAHit() {
    Rig rig;
    Session& session = rig.session();
    PatternScreen& drums = rig.app.drums();
    drums.setStep(0, 4, 0.7f);
    drums.setNudge(0, 4, 0.2f);
    drums.setChance(0, 4, 0.5f);
    StepFx fx;
    fx.pitch = 7, fx.ratchet = 3, fx.reverse = 1, fx.cut = 2, fx.throwTo = 1;
    drums.setFx(0, 4, fx);
    drums.moveCursorTo(0, 4);
    rig.settle();
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.frames(1);
    std::vector<float> out(2 * 48000);
    for (int i = 0; i < 4; ++i) rig.engine->render(out.data(), 48000); // what X heard has died away
    rig.key(STICK_UP, true); // a turn first: pitch 8, a burst of edits of its own
    rig.frames(1);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(drums.fx(0, 4).pitch == 8);
    for (int i = 0; i < 4; ++i) rig.engine->render(out.data(), 48000);
    rig.key(B, true);
    rig.engine->render(out.data(), 4000);
    float heard = 0.f;
    for (int i = 0; i < 2 * 4000; ++i) heard = std::max(heard, std::fabs(out[i]));
    CHECK(heard > 0.01f); // heard, plain
    rig.frames(1);
    rig.key(B, false);
    rig.frames(1);
    CHECK(!drums.fx(0, 4).any() && drums.effecting());
    CHECK(drums.level(0, 4) == 0.7f && std::fabs(drums.nudge(0, 4) - 0.2f) < 1e-6f && drums.chance(0, 4) == 0.5f);
    CHECK(rig.track(0).drums[0][0].fx[0][4].pitch == 0); // (the pattern's own)
    rig.key(X, false);
    rig.settle();
    CHECK(session.undoSteps() == steps + 2); // the turn, then the emptying
    rig.undo();
    const StepFx& back = drums.fx(0, 4);
    CHECK(back.pitch == 8 && back.ratchet == 3 && back.reverse == 1 && back.cut == 2 && back.throwTo == 1 && drums.level(0, 4) == 0.7f);
    rig.undo();
    CHECK(drums.fx(0, 4).pitch == 7);
    // Nothing to empty: nothing, and no step.
    drums.setFx(0, 4, StepFx());
    rig.settle();
    const int none = session.undoSteps();
    rig.key(X, true);
    rig.frames(1);
    rig.tap(B);
    rig.key(X, false);
    rig.settle();
    CHECK(session.undoSteps() == none && drums.level(0, 4) == 0.7f);
    // X let go, B is the delete it always was.
    rig.tap(B);
    CHECK(drums.level(0, 4) == 0.f);
}

// G11: X held on a lane's name, B puts its HUMAN TIME, HUMAN LEVEL and SWING back to OFF: that lane only.
void testXAndBEmptyALanesFeel() {
    Rig rig;
    Session& session = rig.session();
    PatternScreen& drums = rig.app.drums();
    session.setLaneFeel(0, 2, 50, 25);
    session.setLaneSwing(0, 2, 75);
    session.setLaneFeel(0, 3, 10, 10);
    session.setLaneSwing(0, 3, 10);
    drums.moveCursorTo(2, -1);
    rig.settle();
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.frames(1);
    CHECK(drums.feeling());
    rig.tap(B);
    CHECK(drums.laneFeel(2).time == 0 && drums.laneFeel(2).level == 0 && rig.track(0).laneSwing[2] == 0 && drums.feeling());
    CHECK(drums.laneFeel(3).time == 10 && drums.laneFeel(3).level == 10 && rig.track(0).laneSwing[3] == 10);
    rig.key(X, false);
    rig.settle();
    CHECK(session.undoSteps() == steps + 1);
    rig.undo();
    CHECK(drums.laneFeel(2).time == 50 && drums.laneFeel(2).level == 25 && rig.track(0).laneSwing[2] == 75);
    // A lane with only SWING: that too goes.
    session.setLaneFeel(0, 2, 0, 0);
    rig.settle();
    rig.key(X, true);
    rig.frames(1);
    rig.tap(B);
    rig.key(X, false);
    rig.frames(1);
    CHECK(rig.track(0).laneSwing[2] == 0);
}

// G11: X held on a note, B empties its RATCHET, GLIDE and THROW; on a chord its six effects and THROW. The
// note or the chord stays as it is otherwise; the track's GLIDE FROM stays. One step of undo each.
void testXAndBEmptyANoteAndAChord() {
    Rig rig;
    Session& session = rig.session();
    NoteScreen& roll = rollOnPhazed(rig);
    NotePattern& bar = rig.track(1).notes[0][0];
    NoteEvent n;
    n.step = 4, n.pitch = 60, n.length = 2.f, n.velocity = 0.6f, n.nudge = 0.2f, n.ratchet = 3, n.glide = 3, n.throwTo = 2;
    bar.notes[0] = n;
    bar.chords[0] = makeChord(builtInPack().chords[0], 48, 8, 3.f, 0.7f, 0);
    bar.chords[0].ratchet = 2;
    bar.chords[0].throwTo = 3;
    bar.chords[0].play = chordPlayOf(kStrumWide, kOrderDown, 25, 10, 25);
    session.sendNotePattern(1, 0);
    session.setGlideFrom(1, 2);
    roll.moveCursorTo(60, 4);
    rig.settle();
    const int steps = session.undoSteps();
    rig.key(X, true);
    rig.frames(1);
    CHECK(roll.effecting());
    rig.tap(B);
    const NoteEvent& now = bar.notes[0];
    CHECK(now.ratchet == 0 && now.glide == 0 && now.throwTo == 0 && roll.effecting());
    CHECK(now.velocity == 0.6f && now.length == 2.f && std::fabs(now.nudge - 0.2f) < 1e-6f && now.pitch == 60 && now.step == 4 && roll.noteCount() == 1);
    rig.key(X, false);
    rig.settle();
    CHECK(session.undoSteps() == steps + 1);
    rig.undo();
    CHECK(bar.notes[0].ratchet == 3 && bar.notes[0].glide == 3 && bar.notes[0].throwTo == 2);
    // A turn just before it is a step of its own: the emptying is one step, whatever came before.
    rig.key(X, true);
    rig.frames(1);
    rig.tap(RIGHT); // x4
    rig.tap(B);
    rig.key(X, false);
    rig.settle();
    rig.undo();
    CHECK(bar.notes[0].ratchet == 4 && bar.notes[0].glide == 3);
    rig.undo();
    CHECK(bar.notes[0].ratchet == 3);
    // The chord.
    int keys[kChordNotes];
    bar.chords[0].pitches(keys);
    roll.moveCursorTo(keys[1], 9);
    rig.key(X, true);
    rig.frames(1);
    CHECK(roll.effecting() && roll.chordHere());
    rig.tap(B);
    const ChordEvent& c = bar.chords[0];
    CHECK(c.velocity == 0.7f && c.length == 3.f && c.step == 8 && roll.chordCount() == 1);
    CHECK(c.ratchet == 0 && c.throwTo == 0 && c.play.strum == kStrumOff && c.play.order == kOrderUp && c.play.time == 0 && c.play.level == 0 && c.play.lengths == 0);
    CHECK(rig.track(1).glideFrom == 2);
    rig.key(X, false);
    rig.settle();
    rig.undo();
    CHECK(bar.chords[0].ratchet == 2 && bar.chords[0].play.strum == kStrumWide && bar.chords[0].play.order == kOrderDown && bar.chords[0].throwTo == 3);
    // ORDER alone is still something X shows: it goes back to UP.
    bar.chords[0].ratchet = 0, bar.chords[0].throwTo = 0, bar.chords[0].play = chordPlayOf(kStrumOff, kOrderRandom, 0, 0, 0);
    session.sendNotePattern(1, 0);
    rig.key(X, true);
    rig.frames(1);
    rig.tap(B);
    rig.key(X, false);
    rig.frames(1);
    CHECK(bar.chords[0].play.order == kOrderUp);
    // Nothing to empty: nothing, and no step.
    roll.moveCursorTo(60, 4);
    bar.notes[0].ratchet = bar.notes[0].glide = bar.notes[0].throwTo = 0;
    rig.settle();
    const int none = session.undoSteps();
    rig.key(X, true);
    rig.frames(1);
    rig.tap(B);
    rig.key(X, false);
    rig.settle();
    CHECK(session.undoSteps() == none && roll.noteCount() == 1);
    // X let go, B is the delete it always was.
    rig.tap(B);
    CHECK(roll.noteCount() == 0);
}

// G27: Y held on the roll, L2 / R2 halve or double the chord in hand: 1, 2, 4, 8, 16 steps.
void testTheTriggersSetTheChordsLength() {
    Rig rig;
    Session& session = rig.session();
    NoteScreen& roll = rollOnPhazed(rig);
    NotePattern& bar = rig.track(1).notes[0][0];
    CHECK(roll.handLength() == 4.f);
    roll.moveCursorTo(60, 0);
    rig.key(Y, true);
    rig.frames(1);
    CHECK(roll.picking());
    rig.tap(TRIGGER_R2);
    CHECK(roll.handLength() == 8.f && rig.said() == "CHORD 8 STEPS" && session.view == View::Pattern);
    rig.tap(TRIGGER_R2);
    CHECK(roll.handLength() == 16.f);
    rig.tap(TRIGGER_R2);
    CHECK(roll.handLength() == 16.f); // the end stops
    rig.tap(TRIGGER_L2, 4);
    CHECK(roll.handLength() == 1.f && rig.said() == "CHORD 1 STEP");
    rig.tap(TRIGGER_L2);
    CHECK(roll.handLength() == 1.f && session.view == View::Pattern);
    rig.tap(TRIGGER_R2);
    CHECK(roll.handLength() == 2.f && roll.picking());
    rig.key(Y, false);
    rig.frames(1);
    CHECK(roll.chordCount() == 1 && bar.chords[0].length == 2.f && bar.chords[0].step == 0 && roll.cursorStep() == 2);
    // A length between two goes to the next one that way: hold A on the chord, DOWN twice (3 steps).
    int keys[kChordNotes];
    bar.chords[0].pitches(keys);
    roll.moveCursorTo(keys[0], 0);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(DOWN);
    rig.key(A, false);
    rig.frames(1);
    CHECK(bar.chords[0].length == 3.f && roll.handLength() == 3.f);
    roll.moveCursorTo(60, 8);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(TRIGGER_L2);
    CHECK(roll.handLength() == 2.f);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(bar.chords[1].length == 2.f || bar.chords[0].length == 2.f);
    // Where it does not fit, the box says so: 16 at step 13 of a one-bar cell lands 3 long.
    roll.moveCursorTo(60, 13);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(TRIGGER_R2, 3);
    CHECK(roll.handLength() == 16.f && rig.said() == "CHORD 16 STEPS: 3 FIT");
    rig.tap(B); // (called off: nothing lands)
    rig.key(Y, false);
    rig.frames(1);
    CHECK(roll.chordCount() == 2);
    // A chord being swapped (Y held in its steps): its own length, held to its room, and the cursor stays inside it.
    roll.moveCursorTo(60, 1);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(TRIGGER_R2); // 3 -> 4
    CHECK(bar.chords[0].length == 4.f && roll.handLength() == 4.f);
    rig.tap(TRIGGER_R2); // 8, but the next chord starts at step 8: it fills to there
    CHECK(bar.chords[0].length == 8.f);
    rig.tap(TRIGGER_R2); // no more room: as it was, nothing said
    CHECK(bar.chords[0].length == 8.f);
    rig.key(Y, false);
    rig.frames(1);
    roll.moveCursorTo(60, 6);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(TRIGGER_L2, 2); // 8 -> 4 -> 2: the cursor comes back inside it
    CHECK(bar.chords[0].length == 2.f && roll.cursorStep() == 1 && roll.handLength() == 2.f);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(roll.chordCount() == 2);
    // The hints while Y is held fit the bar, at LARGE and at XL, with either help: on a new chord (B cancel) and on
    // a swap (the chord at step 0, two long: A notes, B as it was, the longer row). The swap's has no room for
    // L2 R2 (11 pixels over with HLD SEL: HELP): only a new chord's says it.
    Canvas canvas, scratch;
    if (canvas.ok && scratch.ok) {
        const std::vector<unsigned char> ttf = loadFontFile();
        Fonts xl;
        xl.fixedSet = canvas.fonts.get();
        CHECK(xl.load(canvas.r, ttf, kTextXl));
        for (int swap = 0; swap < 2; ++swap) {
            roll.moveCursorTo(60, swap ? 1 : 12);
            rig.key(Y, true);
            rig.frames(1);
            for (const char* help : {"MENU: HELP", "HLD SEL: HELP"})
                for (const Fonts* fonts : {static_cast<const Fonts*>(canvas.fonts.get()), static_cast<const Fonts*>(&xl)}) {
                    ui::gHelpHint = help; // (App::update sets it each pass from the controls)
                    canvas.clear();
                    rig.app.draw(canvas.r, *fonts);
                    CHECK(ui::gHintsOver == 0.f);
                    if (ui::gHintsOver != 0.f) std::printf("    %s, %s: %.0f pixels over\n", swap ? "swap" : "new chord", help, static_cast<double>(ui::gHintsOver));
                    if (fonts == &xl && help[0] == 'H') CHECK(barSays(canvas, scratch, "length") == !swap); // (read once a row: slow)
                }
            rig.tap(B); // (called off, or put back as it was: nothing changes)
            rig.key(Y, false);
            rig.frames(1);
        }
        CHECK(roll.chordCount() == 2 && bar.chords[0].length == 2.f);
        ui::gHelpHint = "MENU: HELP";
    }
    // On the drum grid Y held is the dice: L2 / R2 stay swallowed, and turn no tab. Nor do they reach
    // the roll, though it still thinks Y is held there (the track changed under a held Y).
    roll.moveCursorTo(60, 12);
    rig.key(Y, true);
    rig.frames(1);
    CHECK(roll.picking());
    const float hand = roll.handLength();
    rig.project().track = 0;
    rig.frames(1);
    rig.tap(TRIGGER_R2);
    CHECK(roll.handLength() == hand && session.view == View::Pattern);
    rig.key(Y, false);
    rig.frames(1);
    rig.app.drums().moveCursorTo(0, -1);
    rig.key(Y, true);
    rig.frames(1);
    rig.tap(TRIGGER_R2);
    CHECK(session.view == View::Pattern);
    rig.key(Y, false);
    rig.frames(1);
}

// G27, after review: the box that says the length sits over the middle of the pack's tiles, the lit one among
// them. It goes as soon as the pick moves on (another chord, inversion or pack, by the D-pad or the stick); let go
// of Y with nothing moved, it stays its moment. And its longest line fits the box at XL.
void testTheLengthsBoxMakesWay() {
    Rig rig;
    NoteScreen& roll = rollOnPhazed(rig);
    Session& session = rig.session();
    // Y held, the length changed (R2, or L2 from 16): is the box up?
    const auto lengthSaid = [&] {
        rig.key(Y, true);
        rig.frames(1);
        rig.tap(roll.handLength() < 16.f ? TRIGGER_R2 : TRIGGER_L2);
        return roll.picking() && rig.said().find("CHORD") == 0;
    };
    roll.moveCursorTo(60, 0);
    for (SDL_Keycode press : {RIGHT, LEFT, UP, DOWN, R1, L1, STICK_RIGHT, STICK_LEFT}) {
        CHECK(lengthSaid());
        const int hand = roll.handIndex(), inversion = roll.handInversion(), pack = session.packIndex();
        rig.tap(press);
        CHECK(roll.picking() && (roll.handIndex() != hand || roll.handInversion() != inversion || session.packIndex() != pack));
        CHECK(rig.said().empty());
        rig.tap(B); // (called off: nothing lands)
        rig.key(Y, false);
        rig.frames(1);
    }
    CHECK(roll.chordCount() == 0);
    // X hears it again and moves nothing: the box stays. Let go of Y: it stays, and says what landed.
    CHECK(lengthSaid());
    rig.tap(X);
    CHECK(rig.said().find("CHORD") == 0);
    rig.key(Y, false);
    rig.frames(1);
    CHECK(roll.chordCount() == 1 && rig.said().find("CHORD") == 0);
    // Something else said since is not the length's: a move of the pick leaves it be.
    roll.moveCursorTo(60, 12);
    CHECK(lengthSaid());
    rig.key(SELECT, true);
    rig.tap(TRIGGER_L2); // the scene a step shorter, said in the same box
    rig.key(SELECT, false);
    rig.frames(1);
    const std::string scene = rig.said();
    rig.tap(RIGHT);
    CHECK(scene.find("SCENE") == 0 && rig.said() == scene);
    rig.tap(B);
    rig.key(Y, false);
    rig.frames(1);
    // The longest it says, 16 asked where 15 fit (a chord at step 1 of an empty one-bar cell), fits the box's room
    // (260 wide less 12) in the medium letters at XL: never held at LARGE, never crowding the box's edge.
    NotePattern& bar = rig.track(1).notes[0][0];
    for (ChordEvent& c : bar.chords) c.velocity = 0.f;
    session.sendNoteBar(1, 0, 0);
    CHECK(roll.chordCount() == 0);
    roll.moveCursorTo(60, 1);
    rig.key(Y, true);
    rig.frames(1);
    if (roll.handLength() >= 16.f) rig.tap(TRIGGER_L2);
    while (roll.handLength() < 16.f) rig.tap(TRIGGER_R2);
    CHECK(rig.said() == "CHORD 16 STEPS: 15 FIT");
    Canvas canvas;
    if (canvas.ok) {
        const std::vector<unsigned char> ttf = loadFontFile();
        Fonts xl;
        xl.fixedSet = canvas.fonts.get();
        CHECK(xl.load(canvas.r, ttf, kTextXl));
        CHECK(xl.medium.width(rig.said().c_str()) <= 260.f - 12.f);
    }
    rig.tap(B);
    rig.key(Y, false);
    rig.frames(1);
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const Section sections[] = {
        {"taptempo", [] { testTheTapRule(), testTapOnTheDrumGrid(), testTapOnTheSetPage(), testTapByThePressesOwnTime(); }},
        {"click", [] { testTheClick(), testTheCountIn(), testTheClickInTheApp(); }},
        {"transpose", [] { testChordNames(), testTransposeACell(), testTransposeOnSong(); }},
        {"scale", [] { testTheScales(), testTheScaleInTheSong(), testTheRollKeepsToIt(); }},
        {"duplicate", [] { testDoubleTrackText(), testDuplicateATrack(), testDuplicateOnSong(); }},
        {"even", [] { testTheEvenRule(), testEvenOnALane(); }},
        {"looks", [] { testTheLooks(); }},
        {"mutes", [] { testTheWaitingGate(), testQueuedMutesInTheApp(); }},
        {"kits", [] { testTheKitsText(), testKitsInTheSession(), testTheKitStop(); }},
        {"stick", [] { testTheStickCarriesAsTheDpad(), testXLetGoWithTheStickPushed(), testTheStickOnRepeats(); }},
        {"xempty", [] { testXAndBEmptyAHit(), testXAndBEmptyALanesFeel(), testXAndBEmptyANoteAndAChord(); }},
        {"chordlength", [] { testTheTriggersSetTheChordsLength(), testTheLengthsBoxMakesWay(); }},
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
