// Apotheca app shell: a 640x480 window, an audio device, and the handheld's
// controls. All sound comes from the engine library; this file only connects
// it to the screen, the speaker and the buttons.
//
// Everything is written against the handheld's buttons (input.hpp). The
// keyboard only stands in for them; see the README for the key map.
// What the buttons do is in pattern_screen.hpp. SELECT + START quits.
//
// For checks and stills:
//   --smoke        place a hit and press play through the keyboard stand-in,
//                  check that sound came out, quit after one second
//   --demo         start with a pattern already on the grid
//   --hold-a       with --demo: hold A on a hit, as if adjusting it
//   --shot FILE    save the last frame drawn as a BMP picture

#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "pattern_screen.hpp"
#include "realtime.hpp"
#include "text.hpp"

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

void pushKey(SDL_Keycode key, bool down) {
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = key;
    SDL_PushEvent(&event);
}

void fillDemoPattern(PatternScreen& screen) {
    const struct { int lane, step; float level; } hits[] = {
        {0, 0, 1.f}, {0, 6, .7f}, {0, 8, 1.f}, {0, 11, .6f}, {1, 4, 1.f}, {1, 12, 1.f}, {1, 15, .4f},
        {2, 0, .7f}, {2, 2, .5f}, {2, 4, .7f}, {2, 6, .5f}, {2, 8, .7f}, {2, 10, .5f}, {2, 12, .7f},
        {3, 14, .8f}, {4, 12, .6f}, {6, 3, .5f}, {6, 9, .5f}};
    for (const auto& hit : hits) screen.setStep(hit.lane, hit.step, hit.level);
    screen.moveCursorTo(1, 4);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke = false, demo = false, holdA = false;
    const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        else if (std::strcmp(argv[i], "--hold-a") == 0) holdA = true;
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
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
    // Draw in 640x480 whatever size the display reports.
    SDL_RenderSetLogicalSize(renderer, kScreenW, kScreenH);

    Fonts fonts;
    {
        const std::vector<unsigned char> ttf = loadFontFile();
        if (!fonts.small.load(renderer, ttf, 12.f) || !fonts.medium.load(renderer, ttf, 14.f) ||
            !fonts.large.load(renderer, ttf, 16.f)) {
            std::fprintf(stderr, "could not load the lettering (routed-gothic-narrow.ttf)\n");
            SDL_Quit();
            return 1;
        }
    }

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i) {
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    }
    std::printf("controls: %s\n", pad ? SDL_GameControllerName(pad) : "no gamepad found, keyboard stand-in only");

    // List what the device can record from, so a log from the handheld shows
    // whether a plugged-in USB microphone or interface was recognised.
    const int inputs = SDL_GetNumAudioDevices(1);
    std::printf("audio inputs: %d\n", inputs < 0 ? 0 : inputs);
    for (int i = 0; i < inputs; ++i) std::printf("  input %d: %s\n", i + 1, SDL_GetAudioDeviceName(i, 1));

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
    engine.prepare(static_cast<float>(have.freq)); // builds the drum kit for this sample rate
    SDL_PauseAudioDevice(device, 0);
    std::printf("audio: %d Hz, %d frames per buffer\n", have.freq, have.samples);

    Input input;
    PatternScreen screen(engine);
    if (demo) fillDemoPattern(screen);

    if (smoke) {
        // What a tester would do: A to place a kick, START to play.
        pushKey(SDLK_x, true);
        pushKey(SDLK_x, false);
        pushKey(SDLK_RETURN, true);
        pushKey(SDLK_RETURN, false);
    } else if (demo) {
        pushKey(SDLK_RETURN, true);
        pushKey(SDLK_RETURN, false);
    }
    if (holdA) {
        screen.setStep(screen.cursorLane(), screen.cursorStep(), 0.55f); // part full, so the fill shows
        pushKey(SDLK_x, true);
    }

    bool running = true;
    bool reported = false;
    float loudest = 0.f;
    const Uint32 start = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            // Desk testing only: the handheld has no Escape key.
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;

            ButtonEvent press;
            if (!input.handle(event, press)) continue;
            if (input.held(Button::Select) && input.held(Button::Start)) running = false;
            else screen.onButton(press, input);
        }
        screen.update(input, SDL_GetTicks());

        if (!reported && audio.realtime.load() >= 0) {
            reported = true;
            std::printf("realtime priority: %s\n", audio.realtime.load() == 1 ? "granted" : "not granted");
        }
        const float peak = engine.trackPeak(0);
        if (peak > loudest) loudest = peak;

        screen.draw(renderer, fonts);
        if ((smoke || shot) && SDL_GetTicks() - start > 1000) running = false;
        if (!running && shot) {
            SDL_Surface* picture = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
            if (picture && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, picture->pixels,
                                                picture->pitch) == 0) {
                SDL_SaveBMP(picture, shot);
            }
            if (picture) SDL_FreeSurface(picture);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_CloseAudioDevice(device);
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (smoke) {
        if (screen.level(0, 0) <= 0.f) { std::fprintf(stderr, "smoke: pressing A placed no hit\n"); return 1; }
        if (!screen.playing()) { std::fprintf(stderr, "smoke: pressing START did not play\n"); return 1; }
        if (loudest <= 0.f) { std::fprintf(stderr, "smoke: the pattern made no sound\n"); return 1; }
    }
    return 0;
}
