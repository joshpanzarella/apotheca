// X HELD hears a recording for as long as it is held (owner, 2026-11-14: "when you hold X the
// sample is played for the duration you are holding it until the end of the sample").
//
// The engine: Command::StopAudition fades what a held audition (a Trigger with e = 1) started on a
// drum lane, and nothing else there. The app: the SAMPLER page, the KIT page, the recordings
// list on both, the drum grid's lane name and empty step, the FX screen and the piano roll's
// empty place on a sampler track. A tap is heard Session::kAuditionMs; held, until let go or the
// recording ends; another screen or track, Play, a new song or panic let it go.
//
// The app's clock and the sound are run side by side: each 16 ms frame is followed by 16 ms of
// sound (768 frames at 48 kHz), so a length heard in the sound is a length on the clock.

#include <SDL.h>

#include <stdlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "samples.hpp"

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

const SDL_Keycode A = SDLK_x, X = SDLK_s, UP = SDLK_UP;
const int kFrameMs = 16, kFrameSound = 768; // one screen frame, and the sound it lasts
const float kQuiet = 1e-4f;

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-hear-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}

std::vector<float> tone(float seconds, float level) {
    std::vector<float> out(static_cast<std::size_t>(seconds * 48000.f));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = level * std::sin(0.5f + 6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
    return out;
}

std::unique_ptr<SampleData> toneData(float seconds, float level) {
    std::unique_ptr<SampleData> s(new SampleData);
    s->data = tone(seconds, level);
    return s;
}

// The last sample of the left side above kQuiet, in ms from the start of `out` (-1: none).
int lastHeardMs(const std::vector<float>& out) {
    for (std::size_t i = out.size(); i >= 2; i -= 2)
        if (std::fabs(out[i - 2]) > kQuiet) return static_cast<int>((i - 2) / 2 / 48);
    return -1;
}

// The biggest step from one sample to the next, left side, from frame `from` to `to`.
float biggestStep(const std::vector<float>& out, int from, int to) {
    float most = 0.f;
    for (int i = from + 1; i < to && 2 * static_cast<std::size_t>(i) < out.size(); ++i) {
        const float step = std::fabs(out[2 * static_cast<std::size_t>(i)] - out[2 * static_cast<std::size_t>(i - 1)]);
        most = step > most ? step : most;
    }
    return most;
}

// The loudest sample, left side, between two times in ms.
float peakIn(const std::vector<float>& out, int fromMs, int toMs) {
    float most = 0.f;
    for (std::size_t i = static_cast<std::size_t>(fromMs) * 48; i < static_cast<std::size_t>(toMs) * 48 && 2 * i < out.size(); ++i)
        most = std::fabs(out[2 * i]) > most ? std::fabs(out[2 * i]) : most;
    return most;
}

std::vector<float> renderOf(Engine& engine, int frames) {
    std::vector<float> out(2 * static_cast<std::size_t>(frames));
    for (int at = 0; at < frames; at += 256) engine.render(out.data() + 2 * at, frames - at < 256 ? frames - at : 256);
    return out;
}

Command trigger(int lane, bool audition, int ratchet = 0) {
    Command c;
    c.type = Command::Trigger;
    c.track = 0;
    c.a = lane;
    c.value = 1.f;
    c.bar = ratchet;
    c.e = audition ? 1 : 0;
    return c;
}

Command stopAudition(int lane) {
    Command c;
    c.type = Command::StopAudition;
    c.track = 0;
    c.a = lane;
    return c;
}

// ---- the engine --------------------------------------------------------------------------

void testTheEngineStopsAHeldAudition() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 2, toneData(2.f, 0.5f)));
    renderOf(*engine, 256);
    // A held audition: still sounding at half a second (a tap's 0.35 s is the app's, not the engine's).
    engine->send(trigger(2, true));
    std::vector<float> out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
    // Let go: gone within a few ms, by a fade, not a cut.
    engine->send(stopAudition(2));
    out = renderOf(*engine, 4800);
    const int gone = lastHeardMs(out);
    CHECK(gone >= 1 && gone <= 5);
    CHECK(biggestStep(out, 0, 400) < 0.02f); // a 220 Hz tone at this level moves 0.0075 a sample; a cut would be 0.3
    CHECK(biggestStep(out, 0, 400) > 0.f);

    // A hit that is not an audition (a tap from the list, a hit by hand) is not stopped.
    engine->send(trigger(2, false));
    renderOf(*engine, 4800);
    engine->send(stopAudition(2));
    out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
    // An audition, then a hit on the same lane: the hit is not the audition's, and plays on.
    engine->send(trigger(2, true));
    renderOf(*engine, 4800);
    engine->send(trigger(2, false));
    renderOf(*engine, 480);
    engine->send(stopAudition(2));
    out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
    // A hit that carries the audition's sound on (it starts where the sound has got to) takes it
    // over: the sound is the hit's now, and is not stopped.
    Command from = trigger(2, true);
    from.b = 2500; // a quarter in
    engine->send(from);
    Command on = trigger(2, false);
    on.b = 2500;
    engine->send(on);
    renderOf(*engine, 256);
    engine->send(stopAudition(2));
    out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
    // Another lane's stop leaves it alone; another track's too.
    engine->send(trigger(2, true));
    renderOf(*engine, 480);
    engine->send(stopAudition(3));
    Command other = stopAudition(2);
    other.track = 5;
    engine->send(other);
    out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
    engine->send(stopAudition(2));
    out = renderOf(*engine, 4800);
    CHECK(lastHeardMs(out) <= 5);
    // Nonsense is refused, not a crash.
    engine->send(stopAudition(-1));
    engine->send(stopAudition(kDrumLanes));
    Command away = stopAudition(0);
    away.track = kNumTracks;
    engine->send(away);
    renderOf(*engine, 256);
}

// A pattern's hit on the lane, the song playing, is not stopped by an audition let go.
void testAPatternHitIsNotStopped() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 2, toneData(1.5f, 0.5f)));
    Command step;
    step.type = Command::SetStep;
    step.track = 0;
    step.a = 2;
    step.b = 0;
    step.value = 1.f;
    engine->send(step);
    renderOf(*engine, 256);
    Command play;
    play.type = Command::Play;
    play.b = 1;
    engine->send(play);
    renderOf(*engine, 4800);
    engine->send(stopAudition(2));
    const std::vector<float> out = renderOf(*engine, 24000);
    CHECK(lastHeardMs(out) >= 499);
}

// A held audition with a ratchet: its pieces still waiting are not played once it is let go.
void testItsRatchetIsNotPlayedOn() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 2, toneData(0.02f, 0.5f))); // each piece is over in 20 ms
    renderOf(*engine, 256);
    engine->send(trigger(2, true, 4)); // four pieces in a step (6000 frames at 120): one every 1500
    renderOf(*engine, 256);
    engine->send(stopAudition(2));
    std::vector<float> out = renderOf(*engine, 12000);
    CHECK(lastHeardMs(out) <= 20);
    // ...and a plain hit's ratchet is played on: its last piece 93 ms on.
    engine->send(trigger(2, false, 4));
    renderOf(*engine, 256);
    engine->send(stopAudition(2));
    out = renderOf(*engine, 12000);
    CHECK(lastHeardMs(out) > 90);
}

// ---- the app -----------------------------------------------------------------------------

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::vector<float> heard; // the sound since the last listen()

    explicit Rig(const std::string& folder) { app.session().useSampleFolder(folder); }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    // `ms` of the program, a frame and its sound at a time.
    void run(int ms) {
        for (int at = 0; at < ms; at += kFrameMs) {
            clock += kFrameMs;
            app.update(input, clock);
            std::vector<float> out(2 * kFrameSound);
            engine->render(out.data(), kFrameSound);
            heard.insert(heard.end(), out.begin(), out.end());
        }
    }
    // Quiet first: what rang before has died away, and the sound kept from here.
    void listen() {
        run(4000);
        heard.clear();
    }
    int heardMs() const { return lastHeardMs(heard); }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
};

bool near(int ms, int want, int within = 25) {
    const bool is = ms >= want - within && ms <= want + within;
    if (!is || std::getenv("HEAR_PRINT")) std::printf("    heard %d ms, wanted %d (within %d)\n", ms, want, within);
    return is;
}

// The gestures, the same on every page: a tap 350 ms, held 2 s two seconds, a short recording
// held to its end, held again from the top, a tap and then a hold, a screen change while held.
// `hold` presses X on the page (and lets go). `away` puts another screen up.
template <class Hold, class Away, class Back>
void theGestures(Rig& rig, const char* page, Hold hold, Away away, Back back, const std::function<void(const char*)>& useRecording) {
    std::printf("  %s\n", page);
    useRecording("long");
    rig.listen();
    // A tap: a moment, as before.
    hold(true);
    hold(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), static_cast<int>(Session::kAuditionMs)));
    // Held two seconds: two seconds.
    rig.listen();
    hold(true);
    rig.run(2000);
    CHECK(rig.session().hearingHeld());
    hold(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 2000, 10)); // stopped in the frame X is seen let go, not the next
    CHECK(!rig.session().hearingHeld());
    // Held for longer than a 0.4 s recording: it ends by itself.
    useRecording("short");
    rig.listen();
    hold(true);
    rig.run(2000);
    hold(false);
    rig.run(500);
    CHECK(near(rig.heardMs(), 400));
    useRecording("long");
    // Held, let go, held again at once: from the top, and heard for the second hold.
    rig.listen();
    hold(true);
    rig.run(600);
    hold(false);
    rig.run(16);
    hold(true);
    rig.run(800);
    hold(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 600 + 16 + 800, 40));
    // A tap, then a hold before the tap's moment is over: the tap's end does not cut the hold.
    rig.listen();
    hold(true);
    hold(false);
    rig.run(96);
    hold(true);
    rig.run(1000);
    hold(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 96 + 1000, 40));
    // Another screen up while X is held: it stops there.
    rig.listen();
    hold(true);
    rig.run(800);
    away();
    rig.run(1500);
    CHECK(near(rig.heardMs(), 800, 40));
    CHECK(!rig.session().hearingHeld());
    hold(false);
    back();
    rig.run(32);
}

void testThePages() {
    const std::string folder = tempFolder();
    writeWav(folder + "/long.wav", tone(3.f, 0.25f), 48000);
    writeWav(folder + "/short.wav", tone(0.4f, 0.25f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    const auto x = [&](bool down) { rig.key(X, down); };
    const auto pattern = [&] { session.view = View::Pattern; };
    const auto device = [&] { session.view = View::Device; };

    // The KIT page, lane 4.
    session.view = View::Device;
    rig.project().track = 0;
    rig.app.kit().goTo(3, KitScreen::kSound);
    theGestures(rig, "kit", x, pattern, device, [&](const char* name) { session.setLaneSample(0, 3, name); });

    // The kit's list of recordings: X there is the same.
    session.setLaneSample(0, 3, "long");
    rig.listen();
    rig.tap(A);
    CHECK(rig.app.kit().picker().isOpen());
    rig.listen();
    x(true);
    rig.run(1200);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 1200));
    rig.listen();
    x(true);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 350));
    // Arriving on a row (not X) plays it whole, as before: up to the built-in drum, down to "long" again.
    rig.tap(UP);
    rig.listen();
    rig.tap(SDLK_DOWN);
    rig.run(4000);
    CHECK(rig.heardMs() > 2900);
    rig.tap(A);
    session.setLaneSample(0, 3, "long");

    // The kit's line across the picture stops where the sound does.
    rig.listen();
    x(true);
    rig.run(1504);
    CHECK(std::fabs(rig.app.kit().playhead().at(rig.clock) - 0.5f) < 0.02f);
    x(false);
    rig.run(32);
    CHECK(rig.app.kit().playhead().at(rig.clock) < 0.f);
    // A tap: 350 ms of it.
    rig.listen();
    x(true);
    x(false);
    rig.run(320);
    CHECK(rig.app.kit().playhead().at(rig.clock) > 0.f);
    rig.run(48);
    CHECK(rig.app.kit().playhead().at(rig.clock) < 0.f);

    // Two lanes tapped in a moment: each is stopped at its own 350 ms.
    session.setLaneSample(0, 4, "long");
    rig.listen();
    x(true);
    x(false);
    rig.run(96);
    rig.app.kit().goTo(4, KitScreen::kSound);
    x(true);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 96 + 350));
    // ...the first not cut short by the second: both are heard together (two tones a little apart
    // in phase: near twice one's peak), then the second alone.
    CHECK(peakIn(rig.heard, 200, 300) > 1.5f * peakIn(rig.heard, 360, 420));
    // Five lanes tapped in a moment (four wait to stop at once): each stopped all the same.
    for (int lane = 0; lane < 5; ++lane) session.setLaneSample(0, lane, "long");
    rig.listen();
    for (int lane = 0; lane < 5; ++lane) {
        rig.app.kit().goTo(lane, KitScreen::kSound);
        x(true);
        x(false);
        rig.run(16);
    }
    rig.run(3500);
    CHECK(rig.heardMs() > 0 && rig.heardMs() < 64 + 350 + 25);
    for (int lane = 0; lane < 5; ++lane)
        if (lane != 3) session.setLaneSample(0, lane, "");
    rig.app.kit().goTo(3, KitScreen::kSound);

    // Another track picked while X is held (another drum track: the same page): it stops.
    CHECK(session.addTrack(TrackKind::Drums));
    session.setLaneSample(1, 3, "long");
    rig.project().track = 0;
    rig.listen();
    x(true);
    rig.run(800);
    rig.project().track = 1;
    rig.run(1500);
    CHECK(near(rig.heardMs(), 800, 40));
    x(false);
    rig.run(32);
    rig.project().track = 0;

    // Play while it is held: it stops (the song does not carry it on).
    rig.listen();
    x(true);
    rig.run(800);
    session.togglePlay(false);
    rig.run(1500);
    session.togglePlay(false);
    x(false);
    CHECK(near(rig.heardMs(), 800, 40));
    // Panic: let go, and nothing waits to stop.
    rig.listen();
    x(true);
    rig.run(800);
    session.panic(true);
    CHECK(!session.hearingHeld() && session.heardEnds() == rig.clock);
    session.panic(false);
    x(false);
    rig.run(32);

    // The drum grid: X on a lane's name, and on an empty step.
    session.view = View::Pattern;
    rig.app.drums().moveCursorTo(3, -1);
    theGestures(rig, "drum grid, lane name", x, device, pattern, [&](const char* name) { session.setLaneSample(0, 3, name); });
    session.setLaneSample(0, 3, "long");
    rig.app.drums().moveCursorTo(3, 6);
    CHECK(rig.app.drums().level(3, 6) == 0.f);
    theGestures(rig, "drum grid, empty step", x, device, pattern, [&](const char* name) { session.setLaneSample(0, 3, name); });
    // In the slice view, an empty step: its slice (a sixteenth of 9.6 s, 600 ms), for as long as X is held.
    writeWav(folder + "/break.wav", tone(9.6f, 0.25f), 48000);
    session.setLaneSample(0, 3, "break");
    rig.run(32);
    rig.app.drums().moveCursorTo(3, 6);
    rig.tap(SDLK_q); // L1: the slice view
    CHECK(rig.app.drums().slicing());
    rig.app.drums().moveCursorTo(3, 6);
    rig.listen();
    x(true);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 350));
    rig.listen();
    x(true);
    rig.run(448);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 448));
    rig.listen();
    x(true);
    rig.run(1500);
    x(false);
    rig.run(500);
    CHECK(near(rig.heardMs(), 600));
    rig.tap(SDLK_q);
    CHECK(!rig.app.drums().slicing());
    // On a hit X held is the step effects' hold, and the hit is heard as the pattern plays it: whole.
    session.setLaneSample(0, 3, "long");
    rig.app.drums().setStep(3, 6, 1.f);
    rig.app.drums().moveCursorTo(3, 6);
    rig.listen();
    x(true);
    rig.run(500);
    x(false);
    rig.run(3500);
    CHECK(rig.heardMs() > 2900);
    rig.app.drums().setStep(3, 6, 0.f);
    // Song change while held: it stops, nothing waits.
    rig.app.drums().moveCursorTo(3, -1);
    rig.listen();
    x(true);
    rig.run(800);
    session.newSong();
    CHECK(!session.hearingHeld());
    rig.run(1500);
    CHECK(near(rig.heardMs(), 800, 40));
    x(false);
    rig.run(32);
}

void testTheSampler() {
    const std::string folder = tempFolder();
    writeWav(folder + "/long.wav", tone(3.f, 0.25f), 48000);
    writeWav(folder + "/short.wav", tone(0.4f, 0.25f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Sampler));
    rig.project().track = 1;
    session.setSamplerSetting(1, sampler::Release, 0); // let go, it stops (in 2 ms)
    session.view = View::Device;
    const auto x = [&](bool down) { rig.key(X, down); };
    const auto use = [&](const char* name) { session.setTrackSample(1, name); };
    theGestures(rig, "sampler", x, [&] { session.view = View::Pattern; }, [&] { session.view = View::Device; }, use);

    // Its list.
    use("long");
    rig.listen();
    rig.tap(A);
    CHECK(rig.app.sampler().picker().isOpen());
    rig.listen();
    x(true);
    rig.run(1200);
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 1200));
    rig.tap(A);

    // The line: on while held, past what a tap gave it; gone once let go and the release has run.
    use("long");
    rig.listen();
    x(true);
    rig.run(1504);
    CHECK(std::fabs(rig.app.sampler().playhead().at(rig.clock) - 0.5f) < 0.02f);
    x(false);
    rig.run(32);
    CHECK(rig.app.sampler().playhead().at(rig.clock) < 0.f);
    // A setting turned while X holds it is heard on the held note: it stays held.
    rig.listen();
    x(true);
    rig.run(500);
    session.setSamplerSetting(1, sampler::Tune, 5);
    rig.app.sampler().goTo(1 + sampler::Tune);
    rig.key(A, true);
    rig.key(UP, true);
    rig.key(UP, false);
    rig.key(A, false);
    rig.run(1000);
    CHECK(session.hearingHeld());
    CHECK(rig.app.sampler().playhead().at(rig.clock) > 0.f); // and so does its line
    x(false);
    rig.run(1500);
    CHECK(near(rig.heardMs(), 1500, 40));
    rig.app.sampler().goTo(0);

    // The FX screen and the piano roll's empty place, on a sampler track: the same.
    session.view = View::Effects;
    rig.run(32);
    theGestures(rig, "fx", x, [&] { session.view = View::Pattern; }, [&] { session.view = View::Effects; }, use);
    session.view = View::Pattern;
    rig.run(32);
    theGestures(rig, "piano roll, an empty place", x, [&] { session.view = View::Device; }, [&] { session.view = View::Pattern; }, use);
}

// The FX screen on a drum track: lane 1.
void testTheFxScreenOnDrums() {
    const std::string folder = tempFolder();
    writeWav(folder + "/long.wav", tone(3.f, 0.25f), 48000);
    writeWav(folder + "/short.wav", tone(0.4f, 0.25f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    rig.project().track = 0;
    session.view = View::Effects;
    theGestures(rig, "fx, drums", [&](bool down) { rig.key(X, down); }, [&] { session.view = View::Pattern; }, [&] { session.view = View::Effects; },
                [&](const char* name) { session.setLaneSample(0, 0, name); });
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    const bool only = argc > 1;
    const auto wanted = [&](const char* name) {
        if (!only) return true;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], name) == 0) return true;
        return false;
    };
#define RUN(test)                          \
    if (wanted(#test)) {                   \
        std::printf("%s\n", #test);        \
        test();                            \
    }
    RUN(testTheEngineStopsAHeldAudition);
    RUN(testAPatternHitIsNotStopped);
    RUN(testItsRatchetIsNotPlayedOn);
    RUN(testThePages);
    RUN(testTheSampler);
    RUN(testTheFxScreenOnDrums);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
