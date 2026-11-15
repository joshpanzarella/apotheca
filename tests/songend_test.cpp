// AT END: what a song played as a song does at its end. LOOP goes round to the first scene, as it
// always did; STOP stops on the step after the last step of the last scene, exactly as Stop does.
// The engine's side (the sample it stops on, at any block length and helper count; LOOP as it
// was; a scene alone, a tempo change and a scene moved near the end; a song of one scene; the
// last scene played four times), and the app's (the song file, undo, the gesture on the SONG
// screen, the screen as drawn, the app told the playing stopped, a bounce unchanged).

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

// ---- the engine ----------------------------------------------------------------------------------

constexpr float kTempo = 240.f; // (the fastest there is) a step is 3000 samples at 48 kHz
constexpr long kStep = 3000;

Command make(Command::Type type, int track = 0, int scene = 0) {
    Command c;
    c.type = type;
    c.track = track;
    c.scene = scene;
    return c;
}

struct Song {
    int scenes = 3;
    int lastRepeats = 1; // the last scene's
    int lastBars = 1;    // ...how many bars its cells have
    int lastSteps = 0;   // ...and the steps it is cut short to (0: all of them)
};

// Drums on track 0 and a Phazed on track 1, every scene one bar. Scene 1 plays twice. An early hit
// and an early note on scene 1's first step (asked for a step ahead: by the song's last step, when
// it goes round), a note on the last scene's last steps that runs past the end, hits to ring out.
void build(Engine& engine, const Song& song) {
    engine.addSynthTrack(1, SynthKind::Phazed);
    Command c = make(Command::SetTempo);
    c.value = kTempo;
    engine.send(c);
    for (int scene = 0; scene < song.scenes; ++scene) {
        for (int step : {0, 4, 8, 12, 15}) {
            c = make(Command::SetStep, 0, scene);
            c.a = step % 3;
            c.b = step;
            c.value = 0.9f;
            engine.send(c);
        }
        c = make(Command::SetNote, 1, scene);
        c.a = 0;
        c.b = 2;
        c.c = 48 + 3 * scene;
        c.d = 3;
        c.value = 0.8f;
        engine.send(c);
    }
    c = make(Command::SetStepNudge, 0, 0);
    c.a = 0;
    c.b = 0;
    c.value = -0.3f;
    engine.send(c);
    c = make(Command::SetStepFx, 0, 0); // ...ratcheted: its later pieces wait on, after the first has sounded
    c.a = 0;
    c.b = 0;
    c.d = 3;
    engine.send(c);
    c = make(Command::SetNote, 1, 0);
    c.a = 1;
    c.b = 0;
    c.c = 60;
    c.d = 2;
    c.value = 0.8f;
    c.shift = -0.25f;
    engine.send(c);
    c = make(Command::SetNote, 1, song.scenes - 1);
    c.a = 2;
    c.b = 14;
    c.c = 55;
    c.d = 6;
    c.value = 0.8f;
    engine.send(c);
    // An early hit on the last scene's first step too: asked ahead when that scene goes round alone.
    c = make(Command::SetStep, 0, song.scenes - 1);
    c.a = 1;
    c.b = 0;
    c.value = 0.9f;
    engine.send(c);
    c = make(Command::SetStepNudge, 0, song.scenes - 1);
    c.a = 1;
    c.b = 0;
    c.value = -0.3f;
    engine.send(c);
    if (song.lastBars > 1) {
        for (int track = 0; track < 2; ++track) {
            c = make(Command::SetCellBars, track, song.scenes - 1);
            c.a = song.lastBars;
            engine.send(c);
        }
    }
    if (song.lastSteps) {
        c = make(Command::SetSceneSteps, 0, song.scenes - 1);
        c.a = song.lastSteps;
        engine.send(c);
    }
    c = make(Command::SetSceneCount);
    c.a = song.scenes;
    engine.send(c);
    c = make(Command::SetSceneRepeats, 0, 0);
    c.a = 2;
    engine.send(c);
    if (song.scenes > 1 || song.lastRepeats != 1) {
        c = make(Command::SetSceneRepeats, 0, song.scenes - 1);
        c.a = song.scenes > 1 ? song.lastRepeats : 2 * song.lastRepeats;
        engine.send(c);
    }
}

long songSteps(const Song& song) {
    const long last = song.lastSteps ? song.lastSteps : 16L * song.lastBars;
    return song.scenes > 1 ? 16L * (2 + (song.scenes - 2)) + last * song.lastRepeats : last * 2 * song.lastRepeats;
}

enum Mode { Loop, Stop, Once, LoopUntold };

struct Run {
    std::vector<float> out;
    long ended = -1;    // block length 1: the frame playing stopped on (playStep -1 after it)
    long wentRound = -1; // block length 1: the frame the song began again at its top
    bool playingAtEnd = false;
    unsigned songEnded = 0;
};

// The song played from its top. `stopAt`: a Stop sent before that frame (-1: none). `event`, before
// frame `eventAt`. Rendered in blocks of `block`, each cut where a Stop or an event goes.
Run run(const Song& song, Mode mode, int block, int helpers, long total, long stopAt = -1, long eventAt = -1,
        const std::function<void(Engine&)>& event = nullptr, int playAlone = 0, int startScene = 0, std::vector<float>* lastBar = nullptr) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    build(*engine, song);
    if (mode != LoopUntold) {
        Command end = make(Command::SetSongEnd);
        end.a = mode == Stop ? 1 : 0;
        engine->send(end);
    }
    Command play = make(Command::Play);
    play.a = startScene;
    play.b = playAlone;
    play.c = mode == Once ? 1 : 0;
    engine->send(play);
    Run r;
    r.out.resize(2 * static_cast<std::size_t>(total));
    int tops = 0;
    bool atTop = false;
    for (long at = 0; at < total;) {
        if (at == stopAt) engine->send(make(Command::Stop));
        if (at == eventAt && event) event(*engine);
        long n = total - at < block ? total - at : block;
        for (long cut : {stopAt, eventAt})
            if (cut > at && cut < at + n) n = cut - at;
        engine->render(r.out.data() + 2 * at, static_cast<int>(n));
        if (block == 1) {
            if (r.ended < 0 && engine->playStep() < 0) r.ended = at;
            const bool top = engine->playScene() == 0 && engine->playRepeat() == 0 && engine->playBar() == 0 && engine->playStep() == 0;
            if (top && !atTop && ++tops == 2 && r.wentRound < 0) r.wentRound = at;
            atTop = top;
        }
        at += n;
    }
    r.playingAtEnd = engine->playStep() >= 0;
    r.songEnded = engine->songEnded();
    if (lastBar) *lastBar = engine->lastBar();
    engine->useWorkers(0);
    return r;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float loudest(const std::vector<float>& out, long from, long to) {
    float p = 0.f;
    for (long i = 2 * from; i < 2 * to && i < static_cast<long>(out.size()); ++i) p = std::fabs(out[i]) > p ? std::fabs(out[i]) : p;
    return p;
}

// STOP against the song played once (a bounce's way: nothing early asked at the end) and stopped
// by a Stop at the frame LOOP goes round on: the same to the bit, at every block length and helpers.
void stopsWhereItGoesRound(const Song& song, long eventAt = -1, const std::function<void(Engine&)>& event = nullptr, const char* what = "", bool rings = true) {
    const long total = (songSteps(song) + 24) * kStep;
    const Run loop = run(song, Loop, 1, 0, total, -1, eventAt, event);
    const Run stop1 = run(song, Stop, 1, 0, total, -1, eventAt, event);
    const long end = loop.wentRound;
    CHECK(end > kStep);
    if (end <= kStep) {
        std::printf("  %s: never went round (stopped at %ld)\n", what, stop1.ended);
        return;
    }
    if (eventAt < 0) CHECK(end == songSteps(song) * kStep); // (no tempo change: the steps are whole)
    CHECK(stop1.ended == end); // the first sample of the step after the last
    CHECK(loop.playingAtEnd && !stop1.playingAtEnd && stop1.songEnded == 1 && loop.songEnded == 0);
    if (stop1.ended != end) std::printf("  %s: stopped at %ld, goes round at %ld\n", what, stop1.ended, end);
    // Every block length on no helpers, and on three helpers the handheld's kind of block and an odd one.
    const int ways[][2] = {{1, 0}, {64, 0}, {333, 0}, {64, 3}, {333, 3}};
    for (const auto& way : ways) {
        const int block = way[0], helpers = way[1];
        {
            const Run stop = block == 1 && helpers == 0 ? stop1 : run(song, Stop, block, helpers, total, -1, eventAt, event);
            const Run once = run(song, Once, block, helpers, total, end, eventAt, event);
            CHECK(same(stop.out, once.out));
            CHECK(!stop.playingAtEnd && stop.songEnded == 1);
            if (!same(stop.out, once.out)) std::printf("  %s: block %d, helpers %d differ\n", what, block, helpers);
        }
    }
    // Up to the end it is the song as LOOP plays it, but for what LOOP asks early for its top:
    // the last step. Tails ring on after it (the hits and the long note let go).
    CHECK(same(std::vector<float>(stop1.out.begin(), stop1.out.begin() + 2 * (end - kStep)), std::vector<float>(loop.out.begin(), loop.out.begin() + 2 * (end - kStep))));
    CHECK(!same(std::vector<float>(stop1.out.begin(), stop1.out.begin() + 2 * end), std::vector<float>(loop.out.begin(), loop.out.begin() + 2 * end))); // (LOOP's early hit and note of the top)
    if (rings) CHECK(loudest(stop1.out, end, end + kStep) > 0.01f);
    CHECK(loudest(stop1.out, total - kStep, total) < loudest(loop.out, total - kStep, total));
}

void testStopStopsOnTheRightSample() {
    stopsWhereItGoesRound(Song(), -1, nullptr, "three scenes");
}

void testASongOfOneScene() {
    Song one;
    one.scenes = 1;
    stopsWhereItGoesRound(one, -1, nullptr, "one scene");
}

void testTheLastScenePlayedFourTimes() {
    Song four;
    four.lastRepeats = 4;
    stopsWhereItGoesRound(four, -1, nullptr, "x4");
    // Not after its first time round: the song is 16 steps x (2 + 1 + 4) long.
    const Run stop = run(four, Stop, 1, 0, (songSteps(four) + 4) * kStep);
    CHECK(stop.ended == 7 * 16 * kStep);
}

// A last scene of more than one bar, or cut short, is played to ITS end: not stopped at its first bar line.
void testALongLastScene() {
    Song bars;
    bars.lastBars = 3;
    bars.lastRepeats = 2;
    stopsWhereItGoesRound(bars, -1, nullptr, "three bars x2", false); // (its hits are in its first bar: nothing rings at the end)
    CHECK(run(bars, Stop, 1, 0, (songSteps(bars) + 4) * kStep).ended == (16 * 3 + 2 * 48) * kStep);
    Song cut;
    cut.lastBars = 2;
    cut.lastSteps = 23;
    stopsWhereItGoesRound(cut, -1, nullptr, "cut to 23 steps", false);
    CHECK(run(cut, Stop, 1, 0, (songSteps(cut) + 4) * kStep).ended == (16 * 3 + 23) * kStep);
    Song few;
    few.lastSteps = 5;
    few.lastRepeats = 2;
    stopsWhereItGoesRound(few, -1, nullptr, "five steps x2");
    Song one; // one scene of two bars
    one.scenes = 1;
    one.lastBars = 2;
    stopsWhereItGoesRound(one, -1, nullptr, "one scene of two bars", false);
}

// Catch (Engine::lastBar) gives the last bar the song played. A bar's end is marked where the next
// bar begins; a song that stops at its end begins none, and its last bar is marked where it stops.
void testCatchAfterTheEnd() {
    const long bar = 16 * kStep;
    const auto is = [](const std::vector<float>& got, const std::vector<float>& out, long from) {
        for (std::size_t i = 0; i < got.size(); ++i)
            if (got[i] != 0.5f * (out[2 * (from + static_cast<long>(i))] + out[2 * (from + static_cast<long>(i)) + 1])) return false;
        return true;
    };
    for (int scenes : {3, 1}) {
        Song song;
        song.scenes = scenes;
        const long end = songSteps(song) * kStep;
        std::vector<float> got;
        // STOP, a fifth of a second after the end: the song's last bar, not the one before, nor the tail.
        const Run stop = run(song, Stop, 64, 0, end + 9600, -1, -1, nullptr, 0, 0, &got);
        CHECK(!stop.playingAtEnd && got.size() == static_cast<std::size_t>(bar));
        CHECK(got.size() == static_cast<std::size_t>(bar) && is(got, stop.out, end - bar));
        CHECK(loudest(stop.out, end - bar, end) > 0.01f);
        // LOOP, just after it has gone round: the same bar (as it always was).
        const Run loop = run(song, Loop, 64, 0, end + 640, -1, -1, nullptr, 0, 0, &got);
        CHECK(got.size() == static_cast<std::size_t>(bar) && is(got, loop.out, end - bar));
    }
    // A last scene of two bars: its second bar.
    Song two;
    two.lastBars = 2;
    const long end = songSteps(two) * kStep;
    std::vector<float> got;
    const Run stop = run(two, Stop, 333, 0, end + 9600, -1, -1, nullptr, 0, 0, &got);
    CHECK(got.size() == static_cast<std::size_t>(bar) && is(got, stop.out, end - bar));
}

void testATempoChangeNearTheEnd() {
    const Song song;
    const long lastBar = (songSteps(song) - 16) * kStep;
    const auto faster = [](Engine& engine) { Command c = make(Command::SetTempo); c.value = 187.f; engine.send(c); };
    stopsWhereItGoesRound(song, lastBar + 9 * kStep + 777, faster, "tempo in the last bar");
    stopsWhereItGoesRound(song, lastBar + 15 * kStep + 100, faster, "tempo in the last step");
}

void testAScenePutInOrTakenOutNearTheEnd() {
    const Song song;
    const long lastBar = (songSteps(song) - 16) * kStep;
    std::unique_ptr<SceneImage> none[kNumTracks];
    // NEXT SCENE on the last scene: the playing goes with it.
    const auto putIn = [&none](Engine& engine) { CHECK(engine.moveScenes(2, 1, none, 4, 1, 0)); };
    stopsWhereItGoesRound(song, lastBar + 5 * kStep + 31, putIn, "a scene put in");
    stopsWhereItGoesRound(song, lastBar + 15 * kStep + 1200, putIn, "a scene put in on the last step");
    // ...and taken back: the scene before goes, and the playing with its scene.
    const auto takeOut = [&none](Engine& engine) { CHECK(engine.moveScenes(2, -1, none, 2, 1, 0)); };
    stopsWhereItGoesRound(song, lastBar + 5 * kStep + 31, takeOut, "a scene taken out");
    // The playing goes with its scene, so the end is where it was. A scene put in AFTER the one
    // playing makes the song a bar longer; the last scene emptied while the one before plays, a bar shorter.
    const long total = (songSteps(song) + 40) * kStep;
    CHECK(run(song, Stop, 1, 0, total, -1, lastBar + 100, putIn).ended == songSteps(song) * kStep);
    CHECK(run(song, Stop, 1, 0, total, -1, lastBar + 100, takeOut).ended == songSteps(song) * kStep);
    const auto putInAfter = [&none](Engine& engine) { CHECK(engine.moveScenes(3, 1, none, 4, 1, 0)); };
    stopsWhereItGoesRound(song, lastBar + 5 * kStep + 31, putInAfter, "a scene put in after", false); // (its last scene is the empty one)
    CHECK(run(song, Stop, 1, 0, total, -1, lastBar + 100, putInAfter).ended == (songSteps(song) + 16) * kStep);
    const auto shorter = [](Engine& engine) { Command c = make(Command::SetSceneCount); c.a = 2; engine.send(c); };
    const long secondScene = 2 * 16 * kStep;
    stopsWhereItGoesRound(song, secondScene + 7 * kStep, shorter, "the last scene emptied");
    CHECK(run(song, Stop, 1, 0, total, -1, secondScene + 7 * kStep, shorter).ended == (songSteps(song) - 16) * kStep);
}

// LOOP is the song as it always was: told LOOP or told nothing, the same to the bit, and it goes round.
void testLoopIsAsItWas() {
    const Song song;
    const long total = (2 * songSteps(song) + 8) * kStep;
    for (int helpers : {0, 3}) {
        const Run told = run(song, Loop, 64, helpers, total), untold = run(song, LoopUntold, 64, helpers, total);
        CHECK(same(told.out, untold.out) && told.playingAtEnd && told.songEnded == 0);
    }
    // A bounce's Play (once) is not stopped by the engine, STOP or not: the bounce stops itself.
    std::unique_ptr<Engine> engine(new Engine);
    build(*engine, song);
    Command end = make(Command::SetSongEnd);
    end.a = 1;
    engine->send(end);
    Command play = make(Command::Play);
    play.c = 1;
    engine->send(play);
    std::vector<float> out(2 * 512);
    for (long at = 0; at < total; at += 512) engine->render(out.data(), 512);
    CHECK(engine->playStep() >= 0 && engine->songEnded() == 0);
    CHECK(engine->songStopsAtEnd());
}

// A scene alone goes round for good, STOP or not, to the bit.
void testASceneAloneIsUnaffected() {
    const Song song;
    const long total = (2 * songSteps(song) + 8) * kStep;
    for (int helpers : {0, 3}) {
        const Run loop = run(song, Loop, 333, helpers, total, -1, -1, nullptr, 1);
        const Run stop = run(song, Stop, 333, helpers, total, -1, -1, nullptr, 1);
        CHECK(same(loop.out, stop.out) && stop.playingAtEnd && stop.songEnded == 0);
    }
    // The song's LAST scene alone (it plays once in the song): it goes round all the same, and its
    // last step still asks for the early hit of its own first step, each time round.
    for (int scenes : {3, 1}) {
        Song last;
        last.scenes = scenes;
        const long round = 16 * kStep;
        const Run loop = run(last, Loop, 64, 0, 5 * round, -1, -1, nullptr, 1, scenes - 1);
        const Run stop = run(last, Stop, 64, 0, 5 * round, -1, -1, nullptr, 1, scenes - 1);
        const Run untold = run(last, LoopUntold, 64, 0, 5 * round, -1, -1, nullptr, 1, scenes - 1);
        CHECK(same(loop.out, stop.out) && same(loop.out, untold.out) && stop.playingAtEnd && stop.songEnded == 0);
        // (The early hit is there to be lost: the second time round is not the first, which had it on the top.)
        CHECK(loudest(stop.out, 2 * round - kStep, 2 * round) > 0.01f);
    }
}

// Switched while the song plays: STOP set in the middle stops it at the end; LOOP set on the last step goes round.
void testSwitchedWhilePlaying() {
    const Song song;
    const long total = (songSteps(song) + 24) * kStep;
    const auto toStop = [](Engine& engine) { Command c = make(Command::SetSongEnd); c.a = 1; engine.send(c); };
    const auto toLoop = [](Engine& engine) { Command c = make(Command::SetSongEnd); c.a = 0; engine.send(c); };
    const Run on = run(song, Loop, 1, 0, total, -1, 20 * kStep, toStop);
    CHECK(on.ended == songSteps(song) * kStep && on.songEnded == 1);
    const Run off = run(song, Stop, 1, 0, total, -1, (songSteps(song) - 1) * kStep + 50, toLoop);
    CHECK(off.ended < 0 && off.playingAtEnd && off.wentRound == songSteps(song) * kStep && off.songEnded == 0);
    // The last step had asked for nothing of the top (STOP). Switched to LOOP there, it asks again:
    // the early hit and note of the song's first step are played, and it is LOOP all along, to the bit.
    const Run loop = run(song, Loop, 1, 0, total), stop = run(song, Stop, 1, 0, total);
    CHECK(same(off.out, loop.out));
    // ...and the other way: LOOP's last step had asked for them; switched to STOP before they
    // sound, they are taken back, and it is STOP all along. Switched in the middle of the song, too.
    const Run late = run(song, Loop, 1, 0, total, -1, (songSteps(song) - 1) * kStep + 50, toStop);
    CHECK(late.ended == songSteps(song) * kStep && same(late.out, stop.out));
    CHECK(same(on.out, stop.out));
    CHECK(same(run(song, Stop, 1, 0, total, -1, 20 * kStep, toLoop).out, loop.out));
    // Switched on any other step, nothing is asked again: scene 1 plays twice, and on its last
    // step the first time, the early ratcheted hit of its top has begun (0.7 of a step in); its
    // later pieces are waiting, and stay.
    CHECK(same(run(song, Loop, 1, 0, total, -1, 15 * kStep + 2500, toStop).out, stop.out));
    CHECK(same(run(song, Stop, 1, 0, total, -1, 15 * kStep + 2500, toLoop).out, loop.out));
    // A switch that changes nothing asks nothing again: LOOP told LOOP on the last step, after the
    // top's early hit has begun (its later pieces stay), and STOP told STOP there.
    CHECK(same(run(song, Loop, 1, 0, total, -1, (songSteps(song) - 1) * kStep + 2500, toLoop).out, loop.out));
    CHECK(same(run(song, Stop, 64, 0, total, -1, (songSteps(song) - 1) * kStep + 50, toStop).out, run(song, Stop, 64, 0, total).out));
    // On three helpers and an odd block, the two late switches are what they are on none.
    CHECK(same(run(song, Stop, 333, 3, total, -1, (songSteps(song) - 1) * kStep + 50, toLoop).out, run(song, Loop, 333, 3, total).out));
    CHECK(same(run(song, Loop, 333, 3, total, -1, (songSteps(song) - 1) * kStep + 50, toStop).out, run(song, Stop, 333, 3, total).out));
    // Each Play is counted: a second playing that ends says it is the second.
    std::unique_ptr<Engine> engine(new Engine);
    build(*engine, song);
    toStop(*engine);
    std::vector<float> out(2 * 1000);
    for (int time = 1; time <= 2; ++time) {
        engine->send(make(Command::Play));
        for (long at = 0; at < total; at += 1000) engine->render(out.data(), 1000);
        CHECK(engine->songEnded() == static_cast<unsigned>(time) && engine->playStep() < 0 && engine->playScene() < 0);
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
    void play(long frames) { std::vector<float> out(2 * 1024); for (long at = 0; at < frames; at += 1024) engine->render(out.data(), 1024); }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    SongScreen& song() { return app.song(); }
};

const SDL_Keycode A = SDLK_x, B = SDLK_z, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN;

// Two scenes of hits on the drum track, sent to the engine.
void twoScenes(Rig& rig) {
    for (int scene = 0; scene < 2; ++scene)
        for (int step = 0; step < kSteps; step += 4) rig.project().tracks[0].drums[scene][0].level[0][step] = 0.9f;
    rig.project().tempo = 240.f;
    rig.session().applyProject();
    rig.frames(2);
}

void testTheSongFile() {
    std::unique_ptr<Project> song(new Project), back(new Project);
    std::string text = songText(*song);
    CHECK(text.find("songend") == std::string::npos); // LOOP: written as it always was
    song->songEnd = kSongStop;
    text = songText(*song);
    CHECK(text.find("\nsongend 1\n") != std::string::npos);
    CHECK(parseSong(text, *back) && back->songEnd == kSongStop && songText(*back) == text);
    // The song's own line: undo cuts the text at the tracks, and it is in the head.
    std::string head;
    std::vector<std::string> parts;
    CHECK(cutSong(text, head, parts) && head.find("songend 1") != std::string::npos);
    CHECK(!cutSong(text + "songend 0\n", head, parts)); // among a track's lines: not cut
    // An old song has no such line: LOOP.
    CHECK(parseSong("apotheca-song 1\ntempo 100\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    // Numbers held to their limits as doubles; a line with no number is skipped.
    // A number nobody knows (a later program's) is the usual, LOOP: not the nearest.
    CHECK(parseSong("apotheca-song 1\nsongend 1e300\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend 2\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend 255\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend nan\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend 1 1 1\ntrack drums\n", *back) && back->songEnd == kSongStop);
    CHECK(songEndKind(1) == kSongStop && songEndKind(0) == kSongLoop && songEndKind(2) == kSongLoop && songEndKind(-1) == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend -7\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend\ntrack drums\n", *back) && back->songEnd == kSongLoop);
    CHECK(parseSong("apotheca-song 1\nsongend 1\r\ntrack drums\r\n", *back) && back->songEnd == kSongStop);
    // Opened, the engine is told; another song without the line, told LOOP again.
    Rig rig;
    CHECK(parseSong(text, *back));
    rig.project() = *back;
    rig.session().applyProject();
    rig.frames(1);
    rig.play(256);
    CHECK(rig.engine->songStopsAtEnd());
    CHECK(parseSong("apotheca-song 1\ntrack drums\n", *back));
    rig.project() = *back;
    rig.session().applyProject();
    rig.frames(1);
    rig.play(256);
    CHECK(!rig.engine->songStopsAtEnd());
}

// The PLAYS box of the row just past the last scene: A switches LOOP and STOP, B puts LOOP back.
void testTheGesture() {
    Rig rig;
    twoScenes(rig);
    Session& session = rig.session();
    SongScreen& song = rig.song();
    session.view = View::Song;
    CHECK(song.songEndRow() == 2);
    song.moveCursorTo(2, -1);
    CHECK(song.onSongEnd());
    rig.tap(A);
    rig.frames(1);
    rig.play(256);
    CHECK(rig.project().songEnd == kSongStop && rig.engine->songStopsAtEnd());
    CHECK(rig.project().repeats[2] == 1); // not the count of the empty scene
    rig.tap(A);
    CHECK(rig.project().songEnd == kSongLoop);
    rig.tap(A);
    rig.tap(B);
    rig.frames(1);
    rig.play(256);
    CHECK(rig.project().songEnd == kSongLoop && !rig.engine->songStopsAtEnd());
    // Held, with the D-pad: nothing (no count to turn), and letting go is not another switch.
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(rig.project().songEnd == kSongStop && rig.project().repeats[2] == 1);
    // On a scene's own box, A is still the count (1 > 2 > 4 > 8).
    song.moveCursorTo(1, -1);
    CHECK(!song.onSongEnd());
    rig.tap(A);
    CHECK(rig.project().repeats[1] == 2 && rig.project().songEnd == kSongStop);
    // ...and below the end row, too.
    song.moveCursorTo(3, -1);
    rig.tap(A);
    CHECK(rig.project().repeats[3] == 2 && rig.project().songEnd == kSongStop);
    // On MASTER (the cursor's row and column kept under it) it is not the end box: A there is the mixer's.
    song.moveCursorTo(2, -1);
    CHECK(song.onSongEnd());
    song.moveToMaster();
    CHECK(!song.onSongEnd());
    rig.tap(A);
    rig.tap(B);
    rig.frames(1);
    CHECK(rig.project().songEnd == kSongStop);
    song.moveCursorTo(2, -1);
    CHECK(song.onSongEnd());
    // A scene put in the end row: the end goes down with it.
    rig.project().tracks[0].drums[2][0].level[1][0] = 0.9f;
    CHECK(song.songEndRow() == 3);
    song.moveCursorTo(2, -1);
    CHECK(!song.onSongEnd());
}

// The engine stops, and the app knows: the play mark, the playhead. Next Play starts where Play always does.
void testTheAppIsTold() {
    Rig rig;
    twoScenes(rig);
    Session& session = rig.session();
    session.view = View::Song;
    session.setSongEnd(kSongStop);
    rig.song().moveCursorTo(0, 0);
    // No Play sent yet, and no song ended: a play mark that did not come from a Play (the other
    // tests set it by hand, "as if playing") is not taken for a song that has ended.
    rig.project().playing = true;
    rig.frames(3);
    CHECK(rig.project().playing);
    rig.project().playing = false;
    rig.tap(START);
    rig.frames(1);
    CHECK(rig.project().playing);
    // 240 BPM: a step is 3000 samples; two scenes of one bar, once each.
    rig.play(2 * 16 * 3000L - 2048);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playScene == 1);
    rig.play(4096);
    rig.frames(1);
    CHECK(!rig.project().playing && rig.project().playScene < 0 && rig.project().playStep < 0);
    // START again: it plays, from the scene worked on.
    rig.project().scene = 1;
    rig.song().moveCursorTo(1, 0);
    rig.tap(START);
    rig.frames(1);
    rig.play(1024);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playScene == 1);
    // ...and stops at the end of the song again, a bar later.
    rig.play(16 * 3000L);
    rig.frames(1);
    CHECK(!rig.project().playing);
    // No race: a song that ended, and START pressed before the engine has taken that Play. Still playing.
    rig.tap(START);
    rig.frames(3);
    CHECK(rig.project().playing);
    rig.play(1024);
    rig.frames(1);
    CHECK(rig.project().playing);
    rig.tap(START); // stop
    rig.frames(1);
    CHECK(!rig.project().playing);
    // LOOP: it goes round, and the app plays on.
    session.setSongEnd(kSongLoop);
    rig.tap(START);
    rig.frames(1);
    rig.play(3 * 16 * 3000L);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playScene >= 0);
    rig.tap(START);
    rig.frames(1);
    rig.play(1024);
    // After Stops by hand (two by now), a song that ends by itself: the app is told all the same.
    // (Only the Plays are counted: a Stop counted would run the app's number ahead of the engine's.)
    session.setSongEnd(kSongStop);
    rig.frames(1);
    rig.tap(START);
    rig.frames(1);
    rig.play(1024);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playScene == 1);
    rig.play(16 * 3000L);
    rig.frames(1);
    CHECK(!rig.project().playing && rig.project().playStep < 0 && rig.engine->playStep() < 0);
    // A scene alone (START on the drum grid) goes round, STOP or not.
    session.setSongEnd(kSongStop);
    session.view = View::Pattern;
    rig.project().track = 0;
    rig.project().scene = 1;
    rig.frames(1);
    rig.tap(START);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playLoop);
    rig.play(3 * 16 * 3000L);
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playScene == 1);
}

// AT END switched while the command queue is full: the switch is lost, known to be lost, and sent
// again (what the program remembers it sent is set only when the send went).
void testSwitchedWithTheQueueFull() {
    Rig rig;
    twoScenes(rig);
    rig.session().setSongEnd(kSongStop);
    rig.frames(2);
    rig.play(4096);
    CHECK(rig.engine->songStopsAtEnd());
    const unsigned before = rig.engine->dropped();
    Command c;
    c.type = Command::SetVolume;
    c.value = 1.f;
    for (long sent = 0; rig.engine->dropped() == before && sent < 10000000; ++sent) rig.engine->send(c);
    CHECK(rig.engine->dropped() != before);
    rig.session().setSongEnd(kSongLoop);
    rig.frames(1);
    for (int i = 0; i < 40; ++i) {
        rig.play(1024);
        rig.frames(1);
    }
    CHECK(rig.project().songEnd == kSongLoop && !rig.engine->songStopsAtEnd());
}

void testUndo() {
    Rig rig;
    twoScenes(rig);
    Session& session = rig.session();
    session.view = View::Song;
    const auto settle = [&] { rig.frames(50); };
    settle();
    const int steps = session.undoSteps();
    rig.song().moveCursorTo(2, -1);
    rig.tap(A);
    settle();
    CHECK(session.undoSteps() == steps + 1 && rig.project().songEnd == kSongStop);
    CHECK(session.undo());
    rig.play(256);
    CHECK(rig.project().songEnd == kSongLoop && !rig.engine->songStopsAtEnd());
    CHECK(session.redo());
    rig.play(256);
    CHECK(rig.project().songEnd == kSongStop && rig.engine->songStopsAtEnd());
    // A hit after it, its own step: undoing the hit leaves STOP.
    rig.app.drums().setStep(0, 1, 0.9f);
    session.noteEdit();
    settle();
    CHECK(session.undo() && rig.project().songEnd == kSongStop);
    // SELECT + B on the SONG screen is the same undo.
    rig.key(SELECT, true);
    rig.tap(B);
    rig.key(SELECT, false);
    rig.play(256);
    CHECK(rig.project().songEnd == kSongLoop && !rig.engine->songStopsAtEnd());
}

// Drawn: the end row's PLAYS box holds a loop in dim (LOOP) or a square in brass (STOP), and the
// rows either side are drawn as they were.
void testTheScreenAsDrawn() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    Rig rig;
    twoScenes(rig);
    rig.session().view = View::Song;
    rig.song().moveCursorTo(0, 0);
    const auto count = [&](SDL_Rect box, SDL_Color c) {
        int n = 0;
        for (int y = box.y + 2; y < box.y + box.h - 2; ++y)
            for (int x = box.x + 2; x < box.x + box.w - 2; ++x) n += canvas.is(x, y, c);
        return n;
    };
    const auto draw = [&] { rig.frames(1); canvas.clear(); rig.app.draw(canvas.r, *canvas.fonts); };
    rig.project().steps[2] = 5; // (an empty row left cut short: its brass step count is not drawn under the mark)
    const SDL_Rect end = rig.song().repeatBox(2), after = rig.song().repeatBox(3), last = rig.song().repeatBox(1);
    CHECK(end.w > 0 && after.w > 0);
    draw();
    const std::vector<Uint32> afterLoop = canvas.part(after.x, after.y, after.w, after.h), lastLoop = canvas.part(last.x, last.y, last.w, last.h);
    const int loopDim = count(end, ui::kDim);
    CHECK(loopDim > 40 && count(end, ui::kBrass) == 0); // the ring and its arrowhead
    // ...not an x1: the box below says x1 in the same dim, and its pixels are another picture.
    CHECK(canvas.part(end.x, end.y, end.w, end.h) != afterLoop);
    rig.session().setSongEnd(kSongStop);
    draw();
    CHECK(count(end, ui::kBrass) == 144 && count(end, ui::kDim) == 0); // a 12 x 12 square
    CHECK(canvas.part(after.x, after.y, after.w, after.h) == afterLoop && canvas.part(last.x, last.y, last.w, last.h) == lastLoop);
    // A scene put in the end row: its box is a count again, and the mark is a row down.
    rig.project().tracks[0].drums[2][0].level[1][0] = 0.9f;
    draw();
    CHECK(count(end, ui::kBrass) > 0 && count(end, ui::kBrass) < 144); // (now a scene cut to 5 steps: its count says so, in brass)
    rig.project().steps[2] = 0;
    draw();
    CHECK(count(end, ui::kBrass) == 0 && count(rig.song().repeatBox(3), ui::kBrass) == 144);
}

// A bounce plays the song once and stops itself: a STOP song bounces to the same file as LOOP.
std::string readFile(const std::string& path) {
    std::string out;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[65536];
        for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

void testABounceIsUnchanged() {
    char pattern[] = "/tmp/apotheca-songend-XXXXXX";
    const std::string folder = mkdtemp(pattern);
    std::string files[2];
    for (int end = kSongLoop; end <= kSongStop; ++end) {
        Rig rig;
        rig.project().tracks[0].drums[0][0].level[0][15] = 0.9f; // a hit on the last step, ringing into the tail
        rig.project().tracks[0].drums[0][0].level[1][0] = 0.9f;
        rig.project().tracks[0].drums[0][0].level[2][0] = 0.9f;
        rig.project().tracks[0].drums[0][0].nudge[2][0] = -0.3f; // early on the first step: not asked at the end
        rig.project().tempo = 240.f;
        rig.project().songEnd = end;
        rig.session().applyProject();
        rig.session().useBounceFolder(folder);
        rig.frames(2);
        CHECK(rig.session().startBounce(Session::Bounce::Song));
        rig.session().finishBounce();
        CHECK(rig.session().bounceFiles() == 1);
        const std::string path = folder + "/" + rig.session().bounceLast();
        files[end] = readFile(path);
        std::remove(path.c_str());
    }
    CHECK(files[0].size() > 10000 && files[0] == files[1]);
    rmdir(folder.c_str());
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    SDL_Init(0);
    const bool appOnly = argc > 1 && std::strcmp(argv[1], "--app") == 0;
    if (!appOnly) {
        testStopStopsOnTheRightSample();
        testASongOfOneScene();
        testTheLastScenePlayedFourTimes();
        testALongLastScene();
        testCatchAfterTheEnd();
        testATempoChangeNearTheEnd();
        testAScenePutInOrTakenOutNearTheEnd();
        testLoopIsAsItWas();
        testASceneAloneIsUnaffected();
        testSwitchedWhilePlaying();
    }
    testTheSongFile();
    testTheGesture();
    testTheAppIsTold();
    testSwitchedWithTheQueueFull();
    testUndo();
    testTheScreenAsDrawn();
    testABounceIsUnchanged();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("song end tests passed\n");
    return 0;
}
