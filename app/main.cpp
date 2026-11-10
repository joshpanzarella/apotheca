// Apotheca app shell: a 640x480 window, an audio device, and the buttons.
// All sound comes from the engine library; this file only connects it to
// the screen, the speaker and the controls.
//
// Keyboard:  Up/Down pick a track.  Z X C V B N M , play notes on it.
// Gamepad:   D-pad up/down pick a track.  A B X Y play notes on it.
// --smoke    run for one second and quit (used by the build check).

#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apotheca/engine.hpp"
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

int pitchForKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_z: return 60;
    case SDLK_x: return 62;
    case SDLK_c: return 64;
    case SDLK_v: return 65;
    case SDLK_b: return 67;
    case SDLK_n: return 69;
    case SDLK_m: return 71;
    case SDLK_COMMA: return 72;
    default: return -1;
    }
}

int pitchForButton(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return 60;
    case SDL_CONTROLLER_BUTTON_B: return 64;
    case SDL_CONTROLLER_BUTTON_X: return 67;
    case SDL_CONTROLLER_BUTTON_Y: return 72;
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

void draw(SDL_Renderer* renderer, const Engine& engine, int selected, float* meters) {
    SDL_SetRenderDrawColor(renderer, 14, 12, 16, 255);
    SDL_RenderClear(renderer);

    const int rowH = kScreenH / kNumTracks;
    for (int t = 0; t < kNumTracks; ++t) {
        // Meters jump up with the sound and fall back slowly.
        const float peak = engine.trackPeak(t);
        meters[t] = peak > meters[t] ? peak : meters[t] * 0.9f;

        SDL_Rect row = {8, t * rowH + 4, kScreenW - 16, rowH - 8};
        if (t == selected) SDL_SetRenderDrawColor(renderer, 60, 50, 70, 255);
        else SDL_SetRenderDrawColor(renderer, 30, 26, 34, 255);
        SDL_RenderFillRect(renderer, &row);

        float level = meters[t] > 1.f ? 1.f : meters[t];
        SDL_Rect bar = {row.x + 4, row.y + 4, static_cast<int>((row.w - 8) * level), row.h - 8};
        SDL_SetRenderDrawColor(renderer, 0, 255, 180, 255);
        SDL_RenderFillRect(renderer, &bar);
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

    int selected = 0;
    float meters[kNumTracks] = {};
    bool running = true;
    bool reported = false;
    const Uint32 start = SDL_GetTicks();
    if (smoke) note(engine, 0, 69, true);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const bool down = event.type == SDL_KEYDOWN;
                const SDL_Keycode key = event.key.keysym.sym;
                if (event.key.repeat) break;
                if (down && key == SDLK_ESCAPE) running = false;
                if (down && key == SDLK_UP) selected = (selected + kNumTracks - 1) % kNumTracks;
                if (down && key == SDLK_DOWN) selected = (selected + 1) % kNumTracks;
                const int pitch = pitchForKey(key);
                if (pitch >= 0) note(engine, selected, pitch, down);
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
                const Uint8 button = event.cbutton.button;
                if (down && button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    selected = (selected + kNumTracks - 1) % kNumTracks;
                if (down && button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) selected = (selected + 1) % kNumTracks;
                const int pitch = pitchForButton(button);
                if (pitch >= 0) note(engine, selected, pitch, down);
                break;
            }
            default:
                break;
            }
        }

        if (!reported && audio.realtime.load() >= 0) {
            reported = true;
            std::printf("realtime priority: %s\n", audio.realtime.load() == 1 ? "granted" : "not granted");
        }

        draw(renderer, engine, selected, meters);
        SDL_Delay(16);
        if (smoke && SDL_GetTicks() - start > 1000) running = false;
    }

    SDL_CloseAudioDevice(device);
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
