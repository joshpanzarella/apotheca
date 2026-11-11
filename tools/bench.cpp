// Timings of the things Apotheca does over and over, so that "is it getting
// slower?" has a number for an answer. Run it after a change that might cost:
//
//   cmake --build build --target bench && ./build/bench
//
// The engine figures are how much of real time one core needs for each
// instrument at full polyphony (lower is better; the handheld is roughly a
// fifth as fast as a desk machine). The app figures are per call.

#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
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

int main() {
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
    std::printf("  %-28s %7.3f ms\n", "undo, then redo", ms([&] { session.noteEdit(); app.update(input, clock += 800); session.undo(); session.redo(); for (int i = 0; i < 300; ++i) drain(); }, 5));
    std::printf("  %-28s %7.3f ms\n", "a bar of notes to the engine", ms([&] { session.sendNoteBar(1, 0, 0); drain(); }, 200));
    std::printf("sizes: Project %zu KB, TrackData %zu KB, Engine %zu KB, a Command %zu bytes\n", sizeof(Project) / 1024, sizeof(TrackData) / 1024, sizeof(Engine) / 1024, sizeof(Command));
    return 0;
}
