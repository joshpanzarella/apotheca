// The owner's crash (2026-11-18): on a PHAZED track, Y held on the piano roll, the chord and its
// inversion changed quickly; the sound buzzed loud and the handheld froze. This drives that gesture
// through the real App, a frame at a time with 16 ms of sound each, and fails on not-a-number, a
// sound past full scale for long, a render that takes far longer than its own time, or a hang.

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"

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

const SDL_Keycode A = SDLK_x, Y = SDLK_a, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT, L1 = SDLK_q, R1 = SDLK_w;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    double worstRatio = 0;  // the slowest render against the sound it made
    float loudest = 0;
    long longLoud = 0, longestLoud = 0; // samples in a row over full scale
    bool nan = false;
    Project& project() { return app.project(); }

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
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            for (int b = 0; b < 3; ++b) {
                std::vector<float> out(2 * 256);
                const auto t0 = std::chrono::steady_clock::now();
                engine->render(out.data(), 256);
                const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                const double ratio = took / (256.0 / 48000.0);
                if (ratio > worstRatio) worstRatio = ratio;
                for (float v : out) {
                    if (!std::isfinite(v)) nan = true;
                    const float a = std::fabs(v);
                    if (a > loudest) loudest = a;
                    longLoud = a > 1.f ? longLoud + 1 : 0;
                    if (longLoud > longestLoud) longestLoud = longLoud;
                }
            }
        }
    }
};

// Y held, the chord and inversion changed `presses` times, `gap` frames apart, then let go.
void pickQuickly(Rig& rig, int presses, int gap, unsigned seed) {
    const SDL_Keycode ways[] = {UP, DOWN, LEFT, RIGHT, LEFT, RIGHT, L1, R1};
    rig.key(Y, true);
    rig.frames(1);
    for (int i = 0; i < presses; ++i) {
        seed = seed * 1103515245u + 12345u;
        const SDL_Keycode k = ways[(seed >> 16) % (sizeof ways / sizeof ways[0])];
        rig.key(k, true);
        rig.frames(gap > 0 ? gap : 1);
        rig.key(k, false);
        if (gap > 1) rig.frames(1);
    }
    rig.key(Y, false);
    rig.frames(2);
}

void run(TrackKind kind, const char* what, int preset) {
    Rig rig;
    Session& s = rig.app.session();
    CHECK(s.setTrackKind(rig.project().track, kind));
    if (preset >= 0) s.chooseSynthPreset(rig.project().track, preset);
    rig.frames(5);
    const auto t0 = std::chrono::steady_clock::now();
    for (int round = 0; round < 40; ++round) {
        pickQuickly(rig, 30, round % 3, 7u + round);
        // Move on a step and place another (A), as a player would between chords.
        rig.key(RIGHT, true), rig.frames(1), rig.key(RIGHT, false), rig.frames(1);
        if (round % 5 == 0) rig.key(A, true), rig.frames(1), rig.key(A, false), rig.frames(1);
    }
    rig.frames(200); // the auditions let go and ring out
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  (commands lost %u)\n", rig.engine->dropped());
    std::printf("%-8s preset %3d: loudest %.2f, longest over full %ld samples, slowest render %.2fx its time, %s, %.1f s\n", what, preset,
                rig.loudest, rig.longestLoud, rig.worstRatio, rig.nan ? "NOT A NUMBER" : "numbers", secs);
    CHECK(!rig.nan);
    CHECK(rig.longestLoud < 4800); // a tenth of a second past full scale in a row: a buzz
    // (The slowest render is printed, not checked: on a shared desk one block now and then is the
    // machine taking the thread away, 0.06 to 1.75 of its time from run to run.)
}

// As on the handheld: the sound made on a thread of its own at its own pace (256 frames every 5.3 ms),
// two helpers, flush-to-zero; the program's frames every 16 ms on this thread. A watchdog fails a
// sound thread that stops for 2 s.
void runThreaded(int preset, int seconds) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->setFlushDenormals(true);
    engine->useWorkers(2, [] {});
    Input input;
    App app(*engine);
    std::atomic<bool> stop(false);
    std::atomic<long> blocks(0);
    std::atomic<bool> nan(false);
    std::atomic<int> loudRun(0);
    std::thread sound([&] {
        std::vector<float> out(2 * 256);
        auto next = std::chrono::steady_clock::now();
        long run = 0, longest = 0;
        while (!stop) {
            engine->render(out.data(), 256);
            for (float v : out) {
                if (!std::isfinite(v)) nan = true;
                run = std::fabs(v) > 1.f ? run + 1 : 0;
                if (run > longest) longest = run, loudRun = static_cast<int>(longest);
            }
            ++blocks;
            next += std::chrono::microseconds(5333);
            std::this_thread::sleep_until(next);
        }
    });
    std::thread watch([&] {
        long last = -1;
        while (!stop) {
            for (int i = 0; i < 20 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const long now = blocks;
            if (now == last && !stop) {
                std::printf("FAIL the sound thread stopped (a hang)\n");
                std::fflush(stdout);
                std::abort();
            }
            last = now;
        }
    });
    Uint32 clock = 1000;
    auto key = [&](SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    };
    auto frame = [&] {
        clock += 16;
        app.update(input, clock);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    };
    app.session().view = View::Pattern;
    frame();
    app.session().setTrackKind(app.project().track, TrackKind::Phazed);
    if (preset >= 0) app.session().chooseSynthPreset(app.project().track, preset);
    for (int i = 0; i < 5; ++i) frame();
    const SDL_Keycode ways[] = {UP, DOWN, LEFT, RIGHT, LEFT, RIGHT, L1, R1};
    unsigned seed = 99u + preset;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        key(Y, true);
        frame();
        for (int i = 0; i < 25; ++i) {
            seed = seed * 1103515245u + 12345u;
            const SDL_Keycode k = ways[(seed >> 16) % 8];
            key(k, true);
            frame();
            key(k, false);
        }
        key(Y, false);
        frame();
        key(RIGHT, true), frame(), key(RIGHT, false), frame();
    }
    for (int i = 0; i < 60; ++i) frame();
    stop = true;
    sound.join();
    watch.join();
    std::printf("threaded preset %d: %ld blocks, longest over full %d, %s, commands lost %u\n", preset, static_cast<long>(blocks), static_cast<int>(loudRun),
                nan ? "NOT A NUMBER" : "numbers", engine->dropped());
    CHECK(!nan);
    CHECK(loudRun < 4800);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--threaded") {
        const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
        for (int p = 0; p < 4; ++p) runThreaded(p, seconds);
        std::printf(failures ? "chordstress: %d failure(s)\n" : "chordstress: all passed\n", failures);
        return failures ? 1 : 0;
    }
    const int presets = argc > 1 ? std::atoi(argv[1]) : 4;
    const int first = argc > 2 ? std::atoi(argv[2]) : 0;
    for (int p = first; p < presets; ++p) run(TrackKind::Phazed, "PHAZED", p);
    if (failures) {
        std::printf("chordstress: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("chordstress: all passed\n");
    return 0;
}
