// Apotheca app shell: a 640x480 window, an audio device, and the handheld's
// controls. All sound comes from the engine library; this file only connects
// it to the screen, the speaker and the buttons.
//
// Everything here is written against the handheld's buttons (input.hpp).
// The keyboard only stands in for them; see the README for the key map.
//
//   D-pad up/down      pick a track
//   A B X Y            play a note on it
//   Right stick        up/down sets the track's level
//   SELECT + START     quit
//
// --smoke  press A through the keyboard stand-in, check that sound came out,
//          and quit after one second (used by the build check).

#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "realtime.hpp"

using namespace apotheca;

namespace {

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

struct AudioState {
    Engine* engine = nullptr;
    bool priorityAsked = false;
    std::atomic<int> realtime{-1}; // -1 not asked yet, 0 refused, 1 granted
};

void audioCallback(void* userdata, Uint8* stream, int bytes) {
    AudioState* state = static_cast<AudioState*>(userdata);
    if (!state->priorityAsked) {
        state->priorityAsked = true;
        state->realtime.store(makeThreadRealtime(70) ? 1 : 0);
    }
    const int frames = bytes / static_cast<int>(2 * sizeof(float));
    state->engine->render(reinterpret_cast<float*>(stream), frames);
}

int pitchFor(Button button) {
    switch (button) {
    case Button::B: return 60;
    case Button::A: return 64;
    case Button::Y: return 67;
    case Button::X: return 72;
    default: return -1;
    }
}

void note(Engine& engine, int track, int pitch, bool on) {
    Command c;
    c.type = on ? Command::NoteOn : Command::NoteOff;
    c.track = track;
    c.a = pitch;
    c.value = 0.8f;
    engine.send(c);
}

void setLevel(Engine& engine, int track, float level) {
    Command c;
    c.type = Command::SetTrackLevel;
    c.track = track;
    c.value = level;
    engine.send(c);
}

void draw(SDL_Renderer* renderer, const Engine& engine, int selected, float* meters, const float* levels) {
    SDL_SetRenderDrawColor(renderer, 20, 17, 14, 255);
    SDL_RenderClear(renderer);

    const int rowH = kScreenH / kNumTracks;
    for (int t = 0; t < kNumTracks; ++t) {
        // Meters jump up with the sound and fall back slowly.
        const float peak = engine.trackPeak(t);
        meters[t] = peak > meters[t] ? peak : meters[t] * 0.9f;

        SDL_Rect row = {8, t * rowH + 3, kScreenW - 16, rowH - 6};
        if (t == selected) SDL_SetRenderDrawColor(renderer, 67, 57, 47, 255);
        else SDL_SetRenderDrawColor(renderer, 31, 26, 22, 255);
        SDL_RenderFillRect(renderer, &row);

        const float shown = meters[t] > 1.f ? 1.f : meters[t];
        SDL_Rect bar = {row.x + 4, row.y + 4, static_cast<int>((row.w - 8) * shown), row.h - 8};
        SDL_SetRenderDrawColor(renderer, 111, 191, 163, 255);
        SDL_RenderFillRect(renderer, &bar);

        // The track's level, as a brass tick.
        SDL_Rect tick = {row.x + 4 + static_cast<int>((row.w - 10) * levels[t]), row.y + 2, 2, row.h - 4};
        SDL_SetRenderDrawColor(renderer, 201, 161, 90, 255);
        SDL_RenderFillRect(renderer, &tick);
    }
    SDL_RenderPresent(renderer);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Apotheca", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          kScreenW, kScreenH, 0);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, 0) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "could not open the screen: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i) {
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    }
    std::printf("controls: %s\n", pad ? SDL_GameControllerName(pad) : "no gamepad found, keyboard stand-in only");

    Engine engine;
    AudioState audio;
    audio.engine = &engine;

    SDL_AudioSpec want = {};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256; // about 5 ms at 48 kHz
    want.callback = audioCallback;
    want.userdata = &audio;
    SDL_AudioSpec have = {};
    const SDL_AudioDeviceID device =
        SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device) {
        std::fprintf(stderr, "could not open audio: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Effects go in before the audio starts. Slot 0 of every track gets a
    // lowpass, left wide open; slot 1 and both master slots are empty.
    engine.prepare(static_cast<float>(have.freq));
    for (int t = 0; t < kNumTracks; ++t) {
        engine.setTrackEffect(t, 0, std::unique_ptr<Effect>(new LowpassEffect));
    }
    SDL_PauseAudioDevice(device, 0);
    std::printf("audio: %d Hz, %d frames per buffer\n", have.freq, have.samples);

    Input input;
    int selected = 0;
    float meters[kNumTracks] = {};
    float levels[kNumTracks];
    for (float& level : levels) level = 0.8f;
    bool running = true;
    bool reported = false;
    float loudest = 0.f;
    const Uint32 start = SDL_GetTicks();

    if (smoke) {
        // Press A the way a tester would: through the keyboard stand-in.
        SDL_Event press = {};
        press.type = SDL_KEYDOWN;
        press.key.keysym.sym = SDLK_x;
        SDL_PushEvent(&press);
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            // Desk testing only: the handheld has no Escape key.
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;

            ButtonEvent press;
            if (!input.handle(event, press)) continue;

            if (press.down && press.button == Button::Up) selected = (selected + kNumTracks - 1) % kNumTracks;
            if (press.down && press.button == Button::Down) selected = (selected + 1) % kNumTracks;
            const int pitch = pitchFor(press.button);
            if (pitch >= 0) note(engine, selected, pitch, press.down);
            if (input.held(Button::Select) && input.held(Button::Start)) running = false;
        }

        // The right stick turns the value under the cursor: here, the level.
        const float turn = -input.rightY();
        if (turn != 0.f) {
            float level = levels[selected] + 0.02f * turn;
            level = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
            if (level != levels[selected]) {
                levels[selected] = level;
                setLevel(engine, selected, level);
            }
        }

        if (!reported && audio.realtime.load() >= 0) {
            reported = true;
            std::printf("realtime priority: %s\n", audio.realtime.load() == 1 ? "granted" : "not granted");
        }

        const float peak = engine.trackPeak(0);
        if (peak > loudest) loudest = peak;

        draw(renderer, engine, selected, meters, levels);
        SDL_Delay(16);
        if (smoke && SDL_GetTicks() - start > 1000) running = false;
    }

    SDL_CloseAudioDevice(device);
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (smoke && loudest <= 0.f) {
        std::fprintf(stderr, "smoke: pressing A made no sound\n");
        return 1;
    }
    return 0;
}
