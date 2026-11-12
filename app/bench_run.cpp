// Timings of the things Apotheca does over and over, so that "is it getting
// slower?" has a number for an answer. Run it after a change that might cost:
//
//   cmake --build build --target bench && ./build/bench
//
// On the handheld: `apotheca --bench`, which the WiFi page's SPEED TEST runs
// (it is in the program itself, so that an update over WiFi brings it).
//
// The engine figures are how much of real time one core needs for each
// instrument at full polyphony (lower is better; the handheld is roughly a
// fifth as fast as a desk machine). The effect figures are the same for one
// effect in one slot. The app figures are per call.

#include <SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "bench_run.hpp"
#include "song_file.hpp"

using namespace apotheca;

static double ms(const std::function<void()>& work, int times) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < times; ++i) work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / times;
}

static void instrument(const char* name, SynthKind kind, int notes, int setting = -1, int value = 0) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->addSynthTrack(1, kind);
    Command c;
    c.track = 1;
    if (setting >= 0) {
        c.type = Command::SetSetting;
        c.a = setting;
        c.b = value;
        engine->send(c);
    }
    std::vector<float> out(512);
    engine->render(out.data(), 256);
    c.type = Command::NoteOn;
    c.value = 0.8f;
    for (int n = 0; n < notes; ++n) {
        c.a = 40 + 3 * n;
        engine->send(c);
    }
    const double block = ms([&] { engine->render(out.data(), 256); }, 600);
    std::printf("  %-28s %5.1f%% of one core\n", name, 100.0 * block / (256.0 / 48.0));
}

// One effect, stereo at 48 kHz, in the engine's chunks of 64 frames, with
// noise going in so that nothing in it can rest. `setting` >= 0: that setting
// at `value` first (the filter's TYPE). The best of five runs, each ten
// seconds of sound: a busy machine only ever makes a run slower.
static void effect(const char* name, EffectKind kind, int setting = -1, float value = 0.f) {
    std::unique_ptr<Effect> fx = makeEffect(kind, 48000.f, 120.f);
    if (setting >= 0) fx->setParam(setting, value);
    const int blocks = 7500;
    std::vector<float> noise(64 * 64);
    std::uint32_t seed = 12345u;
    for (float& v : noise) {
        seed = seed * 1664525u + 1013904223u;
        v = 0.5f * (static_cast<float>(static_cast<std::int32_t>(seed >> 8) - 8388608) / 8388608.f);
    }
    float left[64], right[64];
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            const float* in = noise.data() + 64 * (b % 63);
            for (int i = 0; i < 64; ++i) {
                left[i] = in[i];
                right[i] = in[i + 64];
            }
            fx->process(left, right, 64);
        }
        const double block = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / blocks;
        best = block < best ? block : best;
    }
    std::printf("  %-28s %6.2f%% of one core\n", name, 100.0 * best / (64.0 / 48.0));
}

// The send buses: twelve drum tracks, every lane busy, and what the sends add
// to that. `sends`: every track sends to both buses. `echo` and `space`: what
// the buses hold (None: they are fed and give nothing back, which is the cost
// of the feeding alone). `throws`: every hit of every track is thrown to both
// buses (THROW), which is the most throwing a song can do. The best of five
// runs of ten seconds.
static double busSong(bool sends, EffectKind echo, EffectKind space, bool throws = false) {
    std::unique_ptr<Engine> engine(new Engine);
    for (int t = 1; t < kNumTracks; ++t) engine->addDrumTrack(t);
    Command c;
    for (int t = 0; t < kNumTracks; ++t) {
        c = Command();
        c.type = Command::SetStep;
        c.track = t;
        c.value = 1.f;
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = (t + lane) % 2; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        if (throws) {
            StepFx fx;
            fx.throwTo = kThrowEcho | kThrowSpace;
            c = Command();
            c.type = Command::SetStepFx;
            c.track = t;
            c.extra = fx.cutAndThrow();
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = (t + lane) % 2; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        }
        c = Command();
        c.type = Command::SetSend;
        c.track = t;
        c.value = sends ? 0.5f : 0.f;
        for (int bus = 0; bus < kBuses; ++bus) c.a = bus, engine->send(c);
    }
    if (echo != EffectKind::None) engine->setEffect(kEchoBus, 0, echo);
    if (space != EffectKind::None) engine->setEffect(kSpaceBus, 0, space);
    c = Command();
    c.type = Command::Play;
    engine->send(c);
    std::vector<float> out(512);
    for (int i = 0; i < 400; ++i) engine->render(out.data(), 256);
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const double block = ms([&] { engine->render(out.data(), 256); }, 1875);
        best = block < best ? block : best;
    }
    return 100.0 * best / (256.0 / 48.0);
}

int apotheca::runBench() {
    std::printf("engine, one track, every voice sounding:\n");
    instrument("Phazed, 8 notes", SynthKind::Phazed, 8);
    instrument("Twin Analog x1, 8 notes", SynthKind::Twin, 8, twin::Oversample, 0);
    instrument("Twin Analog x2, 8 notes", SynthKind::Twin, 8, twin::Oversample, 1);
    instrument("Twin Analog x8, 8 notes", SynthKind::Twin, 8, twin::Oversample, 3);
    instrument("QMOD, 8 notes", SynthKind::Qmod, 8);
    instrument("Sampler (no recording)", SynthKind::Sampler, 8);
    {
        std::unique_ptr<Engine> engine(new Engine);
        Command c;
        c.type = Command::SetStep;
        c.value = 1.f;
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = 0; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        c = Command();
        c.type = Command::Play;
        engine->send(c);
        std::vector<float> out(512);
        const double block = ms([&] { engine->render(out.data(), 256); }, 600);
        std::printf("  %-28s %5.1f%% of one core\n", "Drums, 8 lanes busy", 100.0 * block / (256.0 / 48.0));
    }
    {
        std::unique_ptr<Engine> engine(new Engine);
        std::vector<float> out(512);
        const double block = ms([&] { engine->render(out.data(), 256); }, 2000);
        std::printf("  %-28s %5.1f%% of one core\n", "an empty song", 100.0 * block / (256.0 / 48.0));
    }

    // Every effect in the table, in the order of the list: a new one is here without being added.
    std::printf("effects, one each, stereo, at their usual settings:\n");
    for (int place = 1; place < effectListSize(); ++place) {
        const EffectKind kind = effectAt(place);
        effect(effectInfo(kind).name, kind);
        if (kind == EffectKind::Filter) effect("FILTER, LOW 24 DB", kind, 2, 0.3f);
        if (kind == EffectKind::Hall) effect("HALL, TONE ABOVE 50%", kind, 2, 1.f);
        if (kind == EffectKind::Chorus) effect("CHORUS, VIBRATO or FLANGE", kind, 2, 0.9f);
    }

    // The send buses, as what they add to a song of twelve busy drum tracks.
    {
        const EffectKind none = EffectKind::None;
        const double dry = busSong(false, none, none), fed = busSong(true, none, none);
        const double old = busSong(true, EffectKind::Delay, EffectKind::Reverb), now = busSong(true, EffectKind::Tape, EffectKind::Hall);
        std::printf("the send buses (twelve drum tracks, %.2f%% of one core by themselves):\n", dry);
        std::printf("  %-28s %6.2f%% of one core\n", "feeding both from 12 tracks", fed - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "...and TAPE and HALL on them", now - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "...or DELAY and REVERB", old - dry);
        const double thrown = busSong(false, none, none, true);
        std::printf("  %-28s %6.2f%% of one core\n", "every hit thrown to both", thrown - dry);
    }

    std::printf("app, on the screen's thread (a frame is 16.7 ms):\n");
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    // A full song: twelve tracks, eight scenes, notes and hits in every cell, some automation.
    const TrackKind kinds[4] = {TrackKind::Phazed, TrackKind::Twin, TrackKind::Qmod, TrackKind::Drums};
    for (int t = 1; t < kNumTracks; ++t) session.addTrack(kinds[t % 4]);
    Project& p = app.project();
    for (int t = 0; t < kNumTracks; ++t)
        for (int scene = 0; scene < 8; ++scene) {
            for (int step = 0; step < kSteps; step += 2) p.tracks[t].drums[scene][0].level[step % kDrumLanes][step] = 0.8f;
            for (int n = 0; n < 12; ++n) p.tracks[t].notes[scene][0].notes[n] = NoteEvent{n, 48 + n, 1.f, 0.f, 0.8f};
            for (int i = 0; i < kSteps; ++i) p.tracks[t].automation[scene][t % 8][i] = 0.5f;
        }
    std::vector<float> out(512);
    const auto drain = [&] { engine->render(out.data(), 64); };
    std::string text;
    const double writing = ms([&] { text = songText(p); }, 20);
    std::printf("  %-28s %7.3f ms  (%zu KB)\n", "the song as text", writing, text.size() / 1024);
    std::unique_ptr<Project> again(new Project);
    std::printf("  %-28s %7.3f ms\n", "text back into a song", ms([&] { parseSong(text, *again); }, 20));
    Uint32 clock = 1000;
    session.view = View::Pattern;
    std::printf("  %-28s %7.3f ms\n", "one frame, nothing changing", ms([&] { app.update(input, clock += 16); drain(); }, 300));
    std::printf("  %-28s %7.3f ms\n", "a frame that ends an edit", ms([&] { session.noteEdit(); app.update(input, clock += 800); drain(); }, 20));
    {
        // Only the two calls are timed: the frames and the audio between them are not undo's cost.
        double spent = 0.0;
        const int rounds = 10;
        for (int i = 0; i < rounds; ++i) {
            session.noteEdit();
            app.update(input, clock += 800);
            spent += ms([&] { session.undo(); session.redo(); }, 1);
            for (int k = 0; k < 300; ++k) drain();
        }
        std::printf("  %-28s %7.3f ms\n", "undo, then redo", spent / rounds);
    }
    std::printf("  %-28s %7.3f ms\n", "a bar of notes to the engine", ms([&] { session.sendNoteBar(1, 0, 0); drain(); }, 200));
    std::printf("sizes: Project %zu KB, TrackData %zu KB, Engine %zu KB, a Command %zu bytes\n", sizeof(Project) / 1024, sizeof(TrackData) / 1024, sizeof(Engine) / 1024, sizeof(Command));
    return 0;
}
