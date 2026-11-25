// Apotheca app shell: a 640x480 window, an audio device, and the handheld's
// controls. All sound comes from the engine library and all behaviour from
// App; this file only connects them to the screen, the speaker and the
// buttons.
//
// Everything is written against the handheld's buttons (input.hpp). The
// keyboard only stands in for them; see the README for the key map.
// SELECT + START quits. On our own firmware for the RG40XXV it turns the
// handheld off (power_off.hpp: the system says so in the environment).
//
// For checks and stills:
//   --smoke        place a hit and press play through the keyboard stand-in,
//                  check that sound came out, quit after one second
//   --demo         start with a song already made: drums and two Phazed tracks
//   --view NAME    with --demo, open on: song, drums, notes, phazed, kit,
//                  add-track, change-instrument, song-deleting, song-head, song-mixer, song-mixer-master
//                  (A held on a head: the mixer; with -12 on the end, twelve tracks), fx-deleting (B held: take
//                  the still before half a second, --still-ms 300), page1 .. page9 (Phazed's advanced pages), or
//                  jaxep, jaxep0 .. jaxep5 (JAX-EP's main panel, a macro lit; and its pages, MAIN first),
//                  jaxep-bank (MAIN on B7, in BANK B: the longest of the 32 names), jaxep-held (a knob, A held, turned)
//                  fm6, fm6-1 .. fm6-8 (FM-6's main panel, a macro lit; and its pages), fm6-long (MAIN on the
//                  widest of its 32 names), fm6-fixed (an operator at a FIXED pitch, on FINE), fm6-off (an operator OFF)
//                  physic, physic-1 .. physic-5 (PHYSIC's main panel, a macro lit; and its pages: physic-5 is BODY, on
//                  Vibes with PAIR and BUZZ turned up, every setting there read), physic-long (MAIN on the widest of its
//                  names), physic-string (RING on a string: STRETCH reads B), physic-noise (SOUND on Hand Drum, HARDNESS
//                  under the cursor: a NOISE burst's length)
//                  device-mod-held, -stepper, -new, -full, -deleting (X held on a macro: its MOD), fx-mod-held,
//                  fx-mod-stepper, fx-mod-new, fx-mod-full, fx-sends-mod-held (X held on an FX setting or a send)
//                  mod, mod-lfo, mod-steps, mod-target, mod-off, mod-drums, mod-deleting, mod-12, mod-free (the MOD tab;
//                  with --hold-a on mod-target and mod-drums, the list of targets), device-mod-free
//                  help (the card, with how hard the program works: 43, 88 and 37%), help-load (its longest line),
//                  copy-bar, copy-bar-notes, copy-bar-refused (L1 held on the drum grid / the roll after a
//                  copy: the bar's hints and BAR 1 COPIED; a drum bar pasted on a synth track: refused)
//                  volume (the SET page, on VOLUME), setup-lights, setup-light (on our own firmware: BRIGHTNESS,
//                  STICK LIGHT on STEPS), brightness (MENU + a volume key's box, on the drum grid), update-ready, update-ready-drums,
//                  update-accepted (an update accepted: the bar, and the notice), nomenu and help-nomenu
//                  (a handheld with no MENU button: its hint, and its help card), rest-presses (SEL + UP eight
//                  times from a timer while the passes rest: 108 BPM when main.cpp read them all, textviews)
//                  picker, picker-in, picker-deep, picker-deleting, picker-new, picker-typing, picker-carrying, picker-folder-deleting, sampler-picker (with --samples: the list
//                  of recordings in its columns: on SAMPLES, in a folder, two folders down, B held, the sampler's)
//                  kit-edit, kit-edit-held, kit-edit-deleting, kit-edit-column (a lane's EDIT view: with --samples
//                  on a recording, else on the built-in kick)
//                  kit-synth (the KIT page with four lanes of synth voices), kit-voice, kit-voice-held,
//                  kit-voice-deleting, kit-voice-clap (a lane's VOICE view: KICK on TUNE, A held, B held;
//                  CLAP, its empty places, on SPREAD), kit-voice-named (lane 6's CLAP on its voice
//                  FINGER SNAP, the cursor on MODEL with A held: what LEFT / RIGHT do)
//                  sampler, sampler-end (with --samples: a sampler track, START 12% and END 80%, the cursor on START / END)
//   --hold-a, --hold-y, --on-name, --on-tempo, --dice, --paint, --paint-random
//                  with --demo: a gesture held on the drum grid
//   --no-menu      with --shot: any view as on a handheld with no MENU button (the bar's
//                  last words are then HLD SEL: HELP)
//   --shot FILE    save the last frame drawn as a BMP picture
//   --frames DIR   with --shot: also save every frame drawn, DIR/0000.bmp on (to see what moves)
//   --battery DIR  read the battery from DIR (laid out like /sys/class/power_supply)
//   --draw-every-frame  draw and present every pass, as before the screen was left alone
//                  when nothing on it changes (redraw.hpp): for measuring what that saves
//   --learn        set up the controls again (as on a first start with a joystick)
//   --handheld     what the launcher passes: set up the controls on a first start
//                  even if the buttons do not show up as a joystick (some
//                  firmwares send them as keys)
//   --songs DIR    keep songs in this folder (the default is beside the program)
//   --samples DIR  look for recordings (WAV files) in this folder

#include <thread>
#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <ctime>
#include <string>

#include "version.hpp"
#include "apotheca/engine.hpp"
#include "app.hpp"
#include "stills.hpp"
#include "scales.hpp"
#include "drum_voices.hpp"
#include "power_off.hpp"
#include "redraw.hpp"
#include "pass_wait.hpp"
#include "bench_run.hpp"
#include "controls.hpp"
#include "input.hpp"
#include "joysticks.hpp"
#include "realtime.hpp"
#include "song_file.hpp"
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


// Recording, through SDL: the default capture device (on the handheld, a USB
// microphone or interface), as one channel at 48 kHz. It is only open while
// a recording is being made.
class SdlInput : public AudioInput {
public:
    bool start() override {
        stop();
        SDL_AudioSpec want = {}, have = {};
        want.freq = 48000;
        want.format = AUDIO_F32SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = [](void* self, Uint8* stream, int bytes) {
            SdlInput* input = static_cast<SdlInput*>(self);
            const float* samples = reinterpret_cast<const float*>(stream);
            for (int i = 0; i < bytes / 4; ++i) input->queue_.push(samples[i]); // full: the sample is dropped
        };
        want.userdata = this;
        device_ = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0); // SDL converts whatever the device gives
        if (!device_) {
            std::printf("recording: no input (%s)\n", SDL_GetError());
            return false;
        }
        float old;
        while (queue_.pop(old)) {}
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }
    void stop() override {
        if (device_) SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    int read(float* out, int most) override {
        int got = 0;
        while (got < most && queue_.pop(out[got])) ++got;
        return got;
    }
    ~SdlInput() override { stop(); }

private:
    SDL_AudioDeviceID device_ = 0;
    SpscQueue<float, 65536> queue_; // over a second: the screen empties it sixty times in that
};

// A song to look at: three scenes of drums, a Phazed bass and Phazed chords.
// Everything goes through the same calls the buttons make.
void fillDemoSong(App& app) {
    Project& project = app.project();
    Session& session = app.session();
    PatternScreen& drums = app.drums();
    const struct { int lane, step; float level; } hits[] = {
        {0, 0, 1.f}, {0, 6, .7f}, {0, 8, 1.f}, {0, 11, .6f}, {1, 4, 1.f}, {1, 12, 1.f}, {1, 15, .4f},
        {2, 0, .7f}, {2, 2, .5f}, {2, 4, .7f}, {2, 6, .5f}, {2, 8, .7f}, {2, 10, .5f}, {2, 12, .7f},
        {3, 14, .8f}, {4, 12, .6f}, {6, 3, .5f}, {6, 9, .5f}};
    for (int scene = 0; scene < 3; ++scene) {
        project.scene = scene;
        for (const auto& hit : hits) {
            if (scene == 2 && hit.lane == 0 && hit.step > 0) continue; // the last scene thins the kick
            drums.setStep(hit.lane, hit.step, hit.level);
        }
        // Some hats late, for swing; some hits that only sometimes play.
        for (int step : {2, 6, 10}) drums.setNudge(2, step, 0.3f);
        drums.setNudge(1, 15, -0.25f);
        drums.setChance(1, 15, 0.25f);
        drums.setChance(6, 3, 0.5f);
        drums.setChance(6, 9, 0.75f);
        if (scene != 2) drums.setChance(0, 11, 0.5f);
    }
    project.scene = 0;
    drums.moveCursorTo(1, 4);
    session.chokeNewKit(0); // a new song's kit: its hats in CHOKE 1

    // Track 2: a bass. Track 3: chords.
    session.addTrack(TrackKind::Phazed);
    session.addTrack(TrackKind::Phazed);
    const auto choose = [&](int track, const char* folder) {
        for (int i = 0; i < Phazed::presetCount(); ++i) {
            if (std::strcmp(Phazed::presetCategory(i), folder) != 0) continue;
            project.tracks[track].preset = i;
            project.tracks[track].patch = Phazed::presetPatch(i);
            Command c;
            c.type = Command::SelectPreset;
            c.track = track;
            c.a = i;
            session.engine.send(c);
            return;
        }
    };
    choose(1, "Bass");
    choose(2, "Strings & Pads");
    const auto put = [&](int track, int scene, int step, int pitch, int length, float level) {
        NotePattern& p = project.tracks[track].notes[scene][0];
        const int slot = p.freeSlot();
        if (slot < 0) return;
        p.notes[slot].step = step;
        p.notes[slot].pitch = pitch;
        p.notes[slot].length = length;
        p.notes[slot].velocity = level;
    };
    const int roots[3] = {45, 41, 43};
    for (int scene = 0; scene < 3; ++scene) {
        const int root = roots[scene];
        put(1, scene, 0, root, 2, 1.f);
        put(1, scene, 3, root, 1, .7f);
        put(1, scene, 6, root + 12, 2, .8f);
        put(1, scene, 8, root, 2, 1.f);
        put(1, scene, 11, root + 3, 1, .7f);
        put(1, scene, 14, root + 7, 2, .8f);
        session.sendNotePattern(1, scene);
        // Chords from the built-in pack: Am7, Fmaj7, G7, two to the bar.
        const ChordPack& pack = builtInPack();
        const int which[3] = {12, 10, 11}, then[3] = {9, 7, 8};
        NotePattern& pads = project.tracks[2].notes[scene][0];
        pads.chords[0] = makeChord(pack.chords[static_cast<std::size_t>(which[scene])], root + 12, 0, 8, .7f, 0);
        pads.chords[1] = makeChord(pack.chords[static_cast<std::size_t>(then[scene])], root + 12, 8, 6, .6f, 1);
        session.sendNotePattern(2, scene);
    }
    const int repeats[3] = {2, 2, 4};
    for (int scene = 0; scene < 3; ++scene) {
        project.repeats[scene] = repeats[scene];
        Command c;
        c.type = Command::SetSceneRepeats;
        c.scene = scene;
        c.a = repeats[scene];
        session.engine.send(c);
    }
    // Room under the ceiling for three tracks, on the mixer (0.5, 0.35 and 0.2 as they were, to the nearest dB).
    const int levels[3] = {-2, -5, -10};
    for (int track = 0; track < 3; ++track) session.setMix(track, levels[track], 0);
}

} // namespace

int main(int argc, char** argv) {
    // MORPH's 16 tables (wavetable-plan.md, section 2) are made on a thread of their own from the start:
    // seconds on the handheld (a guess), so the first MORPH track does not wait. Joined at the end.
    struct Tables {
        std::thread making{Morph::makeTables};
        ~Tables() { making.join(); }
    } tables;
    // Every line reaches the log as it is written. On the handheld the log is
    // a file, and the program is often stopped by the firmware, not by
    // quitting: without this, nothing it said would be in the file.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    bool learn = false; // ask for each button again, whatever has been learned
    bool everyFrame = false; // --draw-every-frame: as before redraw.hpp, every pass drawn (for measuring what it saves)
    bool handheld = false; // started by the handheld's launcher: with no controls learned yet, ask, joystick or not
    bool noMenu = false;
    bool smoke = false, demo = false, holdA = false, holdY = false, onName = false, onTempo = false, dice = false;
    int paint = 0; // 1: R1, 2: L1
    const char* view = "";
    const char* shot = nullptr;
    const char* framesDir = nullptr;
    int framesKept = 0;
    const char* batteryFolder = nullptr;
    const char* songs = nullptr;
    const char* samples = nullptr;
    int textSize = -1;           // --text-size small | medium | large | xl: a still at that TEXT SIZE
    const char* texts = nullptr; // --texts FILE: what the last frame drew, at each text size in turn (for textsize_test)
    const char* themes = nullptr; // --themes PREFIX: the last frame drawn in every theme, PREFIX0.bmp to PREFIX7.bmp (for theme_test)
    int theme = -1;              // --theme NAME: a still in that THEME (brown, dracula, nord, gruvbox, tokyo, monokai, solarized, github)
    int stillMs = 1000;          // --still-ms N: how long a still (or the smoke run) goes before its picture is taken
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--bench") == 0) return runBench(); // the timings and nothing else: no window, no sound
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--learn") == 0) learn = true;
        else if (std::strcmp(argv[i], "--draw-every-frame") == 0) everyFrame = true;
        else if (std::strcmp(argv[i], "--handheld") == 0) handheld = true;
        else if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        else if (std::strcmp(argv[i], "--hold-a") == 0) holdA = true;
        else if (std::strcmp(argv[i], "--no-menu") == 0) noMenu = true;
        else if (std::strcmp(argv[i], "--hold-y") == 0) holdY = true;
        else if (std::strcmp(argv[i], "--dice") == 0) dice = true;
        else if (std::strcmp(argv[i], "--paint") == 0) paint = 1;
        else if (std::strcmp(argv[i], "--paint-random") == 0) paint = 2;
        else if (std::strcmp(argv[i], "--on-name") == 0) onName = true;
        else if (std::strcmp(argv[i], "--on-tempo") == 0) onTempo = true;
        else if (std::strcmp(argv[i], "--view") == 0 && i + 1 < argc) view = argv[++i];
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) framesDir = argv[++i];
        else if (std::strcmp(argv[i], "--battery") == 0 && i + 1 < argc) batteryFolder = argv[++i];
        else if (std::strcmp(argv[i], "--songs") == 0 && i + 1 < argc) songs = argv[++i];
        else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc) samples = argv[++i];
        else if (std::strcmp(argv[i], "--texts") == 0 && i + 1 < argc) texts = argv[++i];
        else if (std::strcmp(argv[i], "--still-ms") == 0 && i + 1 < argc) stillMs = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--themes") == 0 && i + 1 < argc) themes = argv[++i];
        else if (std::strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
            ++i;
            for (int t = 0; t < kThemes; ++t)
                if (strcasecmp(argv[i], kThemeTable[t].name) == 0) theme = t;
            if (theme < 0) {
                std::fprintf(stderr, "--theme is one of brown, dracula, nord, gruvbox, tokyo, monokai, solarized, github\n");
                return 1;
            }
        }
        else if (std::strcmp(argv[i], "--text-size") == 0 && i + 1 < argc) {
            ++i;
            for (int size = 0; size < kTextSizes; ++size)
                if (strcasecmp(argv[i], kTextSizeNames[size]) == 0) textSize = size;
            if (textSize < 0) {
                std::fprintf(stderr, "--text-size is one of small, medium, large, xl\n");
                return 1;
            }
        }
    }

    if ((texts || themes) && !shot) shot = ""; // a still in every way, with no picture kept
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
    {
        // Which renderer the screen got (SPEED TEST says it: the drawing's cost hangs on it).
        SDL_RendererInfo info = {};
        char said[128] = "unknown";
        if (SDL_GetRendererInfo(renderer, &info) == 0)
            std::snprintf(said, sizeof said, "%s%s%s", info.name ? info.name : "?", info.flags & SDL_RENDERER_ACCELERATED ? ", accelerated" : "",
                          info.flags & SDL_RENDERER_SOFTWARE ? ", software" : "");
        std::printf("screen: %s\n", said);
        setenv(kRendererVariable, said, 1);
    }
    // Draw in 640x480 whatever size the display reports.
    SDL_RenderSetLogicalSize(renderer, kScreenW, kScreenH);

    // Two sets of lettering: the fixed one is always LARGE, for the rows laid out to the pixel
    // (the bars, the strips); the other is the size chosen on the SET page (TEXT SIZE), LARGE
    // until the settings are read. It is loaded again, between two frames, when that changes.
    const std::vector<unsigned char> ttf = loadFontFile();
    Fonts fixedFonts, fonts;
    fonts.fixedSet = &fixedFonts;
    int fontsAt = kTextLarge;
    if (!fixedFonts.load(renderer, ttf, kTextLarge) || !fonts.load(renderer, ttf, fontsAt)) {
        std::fprintf(stderr, "could not load the lettering (routed-gothic-narrow.ttf)\n");
        SDL_Quit();
        return 1;
    }

    // ---- the controls ----------------------------------------------------------------
    // What the device has, for the log: a start that hears no buttons leaves
    // the reason behind.
    static ControlLearner learner;
    const int joysticks = SDL_NumJoysticks();
    Joysticks sticks; // (kept open as they come and go: joysticks.hpp)
    std::vector<SDL_Joystick*>& opened = sticks.opened;
    for (int i = 0; i < joysticks; ++i) {
        SDL_Joystick* stick = SDL_JoystickOpen(i);
        std::printf("joystick %d: \"%s\", %d buttons, %d axes, %d hats, %s\n", i + 1, SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?",
                    stick ? SDL_JoystickNumButtons(stick) : 0, stick ? SDL_JoystickNumAxes(stick) : 0, stick ? SDL_JoystickNumHats(stick) : 0,
                    SDL_IsGameController(i) ? "known to SDL as a gamepad" : "not known to SDL as a gamepad");
        if (stick) {
            opened.push_back(stick);
            // Where each axis rests, for the setup: a trigger sits at one end.
            for (int axis = 0; axis < SDL_JoystickNumAxes(stick); ++axis) {
                Sint16 rest = 0;
                if (SDL_JoystickGetAxisInitialState(stick, axis, &rest)) learner.setAxisRest(axis, rest);
            }
        }
    }
    if (joysticks == 0) std::printf("joysticks: none\n");
    // Controls learned on this device are kept beside the program. With none
    // yet, and a joystick there, the first thing shown is the setup that
    // learns them (controls.hpp). With no joystick at all (a desk), SDL's own
    // gamepad reading and the keyboard stand-ins are used.
    std::string programDir;
    {
        char* base = SDL_GetBasePath();
        programDir = base ? base : "";
        if (base) SDL_free(base);
    }
    const std::string controlsFile = programDir + "controls.txt";
    static ControlMap controlMap;
    std::string savedControls;
    const bool mapped = !smoke && !shot && !learn && readTextFile(controlsFile, savedControls) && controlMap.parse(savedControls);
    // A map from a setup that never asked for L3 (the stick pressed in: MASTER from any screen), and
    // with none in it: L3 alone, once. Answered or let go by, the map is written again and not asked again.
    const bool askL3 = mapped && controlMap.neverAskedL3() && (joysticks > 0 || handheld);
    if (askL3) learner.askOnlyFor(controlMap, Button::L3);
    bool learning = askL3 || (!mapped && !smoke && (!shot || learn) && (joysticks > 0 || learn || handheld));
    SDL_GameController*& pad = sticks.pad;
    sticks.raw = mapped || learning;
    if (!mapped && !learning) {
        for (SDL_Joystick* stick : opened) SDL_JoystickClose(stick);
        opened.clear();
        for (int i = 0; i < joysticks && !pad; ++i) {
            if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
        }
    }
    std::printf("controls: %s\n", askL3 ? "as learned (controls.txt), but L3 was never asked for: asking for it alone"
                                   : mapped ? "as learned (controls.txt)" : learning ? "not learned yet: asking for each button"
                                                                        : pad ? SDL_GameControllerName(pad) : "no gamepad found, keyboard stand-in only");

    // List what the device can record from, so a log from the handheld shows
    // whether a plugged-in USB microphone or interface was recognised.
    const int inputs = SDL_GetNumAudioDevices(1);
    std::printf("audio inputs: %d\n", inputs < 0 ? 0 : inputs);
    for (int i = 0; i < inputs; ++i) std::printf("  input %d: %s\n", i + 1, SDL_GetAudioDeviceName(i, 1));

    // The engine is large; it lives for the whole run.
    static Engine engine;
    AudioState audio;
    audio.engine = &engine;

    // What the sound card itself runs at, before the program opens it for its sound: said here and
    // handed to the SPEED TEST, which cannot open a card the program holds (speed plan, batch 0).
    if (!smoke && !shot) {
        const std::string card = probeSoundCard();
        std::printf("sound card: %s\n", card.c_str());
        setenv(kSoundCardVariable, card.c_str(), 1);
    }
    SDL_AudioSpec want = {};
    want.freq = 48000; // SDL converts if the device differs
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256; // about 5 ms
    want.callback = audioCallback;
    want.userdata = &audio;
    SDL_AudioSpec have = {};
    // Tried a few times: the probe above has just let go of the card, and a sound card may take a
    // moment to be free again (never tried on the handheld: a program that would not start is the
    // worst thing a probe could cost).
    SDL_AudioDeviceID device = 0;
    for (int tries = 0; tries < 10 && !device; ++tries) {
        if (tries) SDL_Delay(100);
        device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    }
    if (!device) {
        std::fprintf(stderr, "could not open audio: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    engine.prepare(static_cast<float>(have.freq)); // builds the drum kit for this sample rate
    engine.setFlushDenormals(true); // numbers too small to hear count as nothing, so a long tail never slows the processor (owner, 2026-11-11)
    // The handheld has four processor cores: the tracks are worked out across them, one fewer
    // than there are (one is left for the screen and everything else).
    {
        const unsigned cores = std::thread::hardware_concurrency();
        const int helpers = helpersFor(cores);
        if (!smoke && !shot) engine.useWorkers(helpers, [] { makeThreadRealtime(69); });
        std::printf("processor cores: %u; tracks shared across %d\n", cores, engine.workers() + 1);
    } // real sound: if it cannot keep up, do plainer work, not stutter
    std::printf("Apotheca %s, built %s\n", kVersion, kBuilt);
    std::printf("audio: %d Hz, %d frames per buffer\n", have.freq, have.samples);

    Input input;
    if (mapped) input.useMap(&controlMap);
    else if (noMenu && shot) input.useMap(&controlMap); // nothing learned: the keyboard stand-ins, and no MENU
    static SdlInput recordingInput;
    App app(engine);
    if (batteryFolder) app.useBatteryFolder(batteryFolder);
    // On our own firmware, quitting (and the idle rule) turns the handheld off. Not for a still or a check.
    PowerOff powerOff = smoke || shot || demo ? PowerOff() : PowerOff::fromEnvironment();
    app.setQuitTurnsOff(powerOff.turnsOff());
    if (powerOff.turnsOff()) std::printf("quitting turns the handheld off; idle: %s\n", powerOff.idleMs ? (std::to_string(powerOff.idleMs / 60000) + " minutes").c_str() : "never");
    app.session().useAudioInput(&recordingInput);
    {
        // Chord packs: a "chords" folder beside the program, or the one in
        // the source tree when running from a build folder.
        char* base = SDL_GetBasePath();
        const std::string beside = std::string(base ? base : "") + "chords";
        if (base) SDL_free(base);
        if (!smoke && !shot) app.session().useProgramFolder(beside.substr(0, beside.size() - 7)); // where the program is: updates land here
        // Not full blast on the handheld's little speaker: two fifths, until it is changed.
        app.session().loadVolume(handheld ? 0.4f : 1.f);
        app.session().loadSettings();
        // The screen's brightness and the light round the stick: only where the system says they are the
        // program's (our own firmware; lights.hpp). Not for a still or a check.
        if (!smoke && !shot) {
            app.session().useLights(Lights::fromEnvironment());
            if (app.session().screenHere() || app.session().ringHere())
                std::printf("lights: screen %s, stick light %s\n", app.session().screenHere() ? "ours" : "not ours", app.session().ringHere() ? "ours" : "not ours");
        }
        // (With --shot nothing is kept: a still has no program folder. Without it the program runs
        // at that size and it is the setting from then on, written to settings.txt as if set on the SET page.)
        if (textSize >= 0) app.session().setTextSize(textSize);
        if (theme >= 0) app.session().setTheme(theme); // (the same: kept, without --shot)
        if (!smoke && !shot) app.session().startMidi(); // USB keyboards, whenever one is plugged in
        // (The packs that ship are compiled in; a folder adds packs, or takes a shipped one's place.)
        struct stat chordsThere;
        const bool besideIsThere = stat(beside.c_str(), &chordsThere) == 0 && S_ISDIR(chordsThere.st_mode);
        app.session().useChordFolder(besideIsThere ? beside : beside.substr(0, beside.size() - 6) + "../assets/chords");
        std::printf("chord packs: %d\n", static_cast<int>(app.session().packs.size()));
        // Recordings for drum lanes: WAV files in a "samples" folder beside the program.
        const std::string folder = samples ? samples : beside.substr(0, beside.size() - 6) + "samples";
        if (!samples && !smoke && !shot) mkdir(folder.c_str(), 0755); // so there is somewhere to copy them to
        // The battery's history, for a power cut's post-mortem: nothing kept where there is no battery.
        if (!smoke && !shot) {
            const std::string logs = beside.substr(0, beside.size() - 6) + "logs";
            mkdir(logs.c_str(), 0755);
            app.useBatteryLog(logs + "/battery.log");
        }
        // Sounds made on the sound-designer page: a "presets" folder beside the program.
        if (!smoke && !shot) app.session().useBounceFolder(beside.substr(0, beside.size() - 6) + "bounces");
        if (!smoke && !shot) app.session().usePresetFolder(beside.substr(0, beside.size() - 6) + "presets");
        app.session().useSampleFolder(folder);
        std::printf("samples: %d in %s\n", static_cast<int>(app.session().sampleNames().size()), folder.c_str());
    }
    if (songs) {
        app.session().useSongFolder(songs, 1);
        if (demo) fillDemoSong(app);
    } else if (demo) {
        fillDemoSong(app);
    } else if (!smoke && !shot) {
        // Songs are kept in a folder beside the program, and the last one
        // open comes back. (Checks and stills run without touching it.)
        char* base = SDL_GetBasePath();
        const std::string folder = std::string(base ? base : "") + "songs";
        if (base) SDL_free(base);
        app.session().useSongFolder(folder, static_cast<std::uint32_t>(std::time(nullptr)));
        std::printf("songs: %s (open: %s)\n", folder.c_str(), app.session().songName().c_str());
    }
    SDL_PauseAudioDevice(device, 0); // after the demo is in, so its commands are not racing the audio thread

    // ---- what to show, for stills (stills.cpp) ----------------------------------------
    {
        StillOptions still;
        still.view = view, still.samples = samples, still.songs = songs, still.shot = shot;
        still.demo = demo, still.smoke = smoke, still.holdA = holdA, still.holdY = holdY;
        still.onName = onName, still.onTempo = onTempo, still.dice = dice, still.noMenu = noMenu, still.paint = paint;
        setUpStill(app, still, input, controlMap, stillMs);
    }

    bool running = true;
    bool startedFine = false;
    Uint32 stillClock = 0; // (stills: the App's clock, a frame a pass at most)
    bool reported = false;
    float loudest = 0.f;
    const Uint32 start = SDL_GetTicks();

    int rawLogged = 0;
    // Start the controls setup again, whatever state the controls are in.
    const auto learnAgain = [&](const char* why) {
        std::printf("controls: setting up again (%s)\n", why);
        if (pad) SDL_GameControllerClose(pad), pad = nullptr;
        sticks.raw = true;
        if (opened.empty())
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_Joystick* stick = SDL_JoystickOpen(i)) opened.push_back(stick);
        learner = ControlLearner();
        for (SDL_Joystick* stick : opened)
            for (int axis = 0; axis < SDL_JoystickNumAxes(stick); ++axis) {
                Sint16 rest = 0;
                if (SDL_JoystickGetAxisInitialState(stick, axis, &rest)) learner.setAxisRest(axis, rest);
            }
        input = Input(); // nothing is held any more
        rawLogged = 0;
        learning = true;
    };
    // The screen is drawn only when it can have changed (redraw.hpp): buttons are still read and
    // the app still moves on every pass, at the same pace.
    Redraw redraw;
    int drawnTheme = -1;
    const char* offWhy = nullptr; // set: the program ends to turn the handheld off ("quit", "idle")
    powerOff.busy(SDL_GetTicks());
    Uint32 audioTriedAt = 0; // the sound card gone: when it was last tried again
    // The rest between passes: 16 ms with nothing pressed, cut short by a press (pass_wait.hpp).
    PassWait wait;
    while (running) {
        SDL_Event event;
        bool stirred = false; // a button, or anything of the window, this pass (a stick's motion counts by where it is)
        while (wait.next(event)) { // (what came during the rest first, then SDL's own)
            if (event.type != SDL_JOYAXISMOTION && event.type != SDL_CONTROLLERAXISMOTION) stirred = true;
            if (event.type == SDL_QUIT) running = false;
            // Desk testing only: the handheld has no Escape key.
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            // The handheld's volume buttons, if the firmware passes them on as the usual keys.
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_VOLUMEUP || event.key.keysym.sym == SDLK_VOLUMEDOWN)) {
                app.volumeKey(event.key.keysym.sym == SDLK_VOLUMEUP, input); // (with MENU held, where the screen is ours: BRIGHTNESS)
                continue;
            }
            if (learning) {
                // The setup has the controls to itself until it is done.
                const std::string before = learner.saw();
                learner.handle(event);
                if (learner.saw() != before && rawLogged < 80) {
                    ++rawLogged;
                    std::printf("the handheld sent: %s\n", learner.saw().c_str());
                }
                continue;
            }

            // The sound card gone (its driver let go of it): opened again, a second apart, below.
            if (event.type == SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture && event.adevice.which == device) {
                std::fprintf(stderr, "the sound card went away: opening it again\n");
                SDL_CloseAudioDevice(device);
                device = 0;
                audioTriedAt = 0;
                continue;
            }
            // A gamepad gone or come back: anything held is let go (its let-go would never come),
            // and one that comes back is read again.
            const Joysticks::Change change = sticks.handle(event);
            if (change != Joysticks::Change::None) {
                std::printf("a gamepad %s\n", change == Joysticks::Change::Added ? "came back" : "went away");
                if (learning) continue; // (the setup reads them raw itself)
                ButtonEvent ups[static_cast<int>(Button::Count)];
                int many = 0;
                input.letGoOfAll(ups, many);
                for (int i = 0; i < many; ++i) app.onButton(ups[i], input);
                continue;
            }

            ButtonEvent press;
            if (!input.handle(event, press)) continue;
            stirred = true;
            do { // (and whatever else the one event moved: a hat's diagonal)
                if (input.held(Button::Select) && input.held(Button::Start)) running = false, offWhy = "quit";
                else app.onButton(press, input);
            } while (running && input.pending(press));
        }
        if (!device && SDL_GetTicks() - audioTriedAt >= 1000) {
            audioTriedAt = SDL_GetTicks();
            SDL_AudioSpec again = {};
            device = SDL_OpenAudioDevice(nullptr, 0, &want, &again, 0); // (as at the start: SDL converts, so the rate is the same)
            if (device) {
                SDL_PauseAudioDevice(device, 0);
                std::printf("the sound card is back\n");
            }
        }
        if (learning) {
            learner.update(SDL_GetTicks());
            if (learner.done()) {
                controlMap = learner.map();
                const bool kept = writeTextFile(controlsFile, controlMap.text());
                std::printf("controls learned%s:\n%s", kept ? "" : " (but could not be saved)", controlMap.text().c_str());
                input.useMap(&controlMap);
                learning = false;
            }
        }
        // A still's clock goes at most a frame a pass: a desk that stalls a pass for half a second must not
        // let a hold that the still shows filling (B on a MOD, a song, an effect) finish before the picture.
        Uint32 appNow = SDL_GetTicks();
        if (smoke || shot) {
            if (stillClock == 0 || appNow < stillClock) stillClock = appNow;
            else if (appNow - stillClock > 16) stillClock += 16, appNow = stillClock;
            else stillClock = appNow;
        }
        if (!learning) app.update(input, appNow);
        if (!learning && app.settings().takeRelearn()) learnAgain("asked for on the SETUP screen");
        // Three seconds in and still going: this start worked. (After an
        // update, that is what tells the launcher to keep the new program.)
        if (!startedFine && SDL_GetTicks() - start > 3000) {
            startedFine = true;
            app.session().startedFine();
        }

        if (!reported && audio.realtime.load() >= 0) {
            reported = true;
            std::printf("realtime priority: %s\n", audio.realtime.load() == 1 ? "granted" : "not granted");
        }
        const float peak = engine.trackPeak(0);
        if (peak > loudest) loudest = peak;

        // TEXT SIZE was changed (or read from the settings): the letters are made again here,
        // on this thread, before anything of the frame is drawn. If that fails they stay as they were.
        if (app.session().textSize() != fontsAt) {
            fontsAt = app.session().textSize();
            if (!fonts.load(renderer, ttf, fontsAt)) std::fprintf(stderr, "could not load the lettering at that size\n");
            redraw.forget();
        }
        if (app.session().theme() != drawnTheme) drawnTheme = app.session().theme(), redraw.forget();
        // Drawn this pass? Asked every pass, so it keeps count; a still is drawn on every pass.
        const bool wanted = redraw.wanted(app, input, stirred, SDL_GetTicks());
        const bool draw = wanted || learning || smoke || shot || everyFrame;
        // Nothing to show for the idle time (no button, the song stopped, nothing moving by itself): off.
        if (wanted || learning) powerOff.busy(SDL_GetTicks());
        else if (powerOff.idleDue(SDL_GetTicks()) && running) running = false, offWhy = "idle";
        if (learning) redraw.forget(); // the setup is drawn every pass, and the app's picture the pass after it
        ui::useTheme(app.session().theme()); // (App::draw does the same; the setup draws without it)
        if (learning) learner.draw(renderer, fonts); // (always at LARGE: it asks for the fixed set itself)
        else if (draw) app.draw(renderer, fonts);
        if ((smoke || shot) && SDL_GetTicks() - start > static_cast<Uint32>(stillMs < 0 ? 0 : stillMs)) running = false;
        if (!running && texts) {
            // The same moment drawn once at each text size, and what each drew written down:
            //   CHOSEN n cap / SIZE n / T order cap x y w h words / B order solid x y w h
            // `cap` is the capitals' height of the font a text was drawn in. The sound is held
            // still meanwhile: a meter must read the same in all four.
            if (FILE* out = std::fopen(texts, "w")) {
                DrawLog log;
                // First, the size the frames before this one were drawn at: what "--text-size" (or nothing) came to.
                std::fprintf(out, "CHOSEN %d %g\n", fontsAt, fonts.small.capHeight());
                SDL_LockAudioDevice(device);
                for (int size = 0; size < kTextSizes; ++size) {
                    if (!fonts.load(renderer, ttf, size)) break;
                    log.clear();
                    gDrawLog = &log;
                    app.draw(renderer, fonts);
                    gDrawLog = nullptr;
                    std::fprintf(out, "SIZE %d\n", size);
                    for (const DrawLog::Text& t : log.texts) std::fprintf(out, "T %d %g %g %g %g %g %s\n", t.order, t.font->capHeight(), t.x, t.y, t.w, t.h, t.words.c_str());
                    for (const DrawLog::Box& b : log.boxes) std::fprintf(out, "B %d %d %d %d %d %d\n", b.order, b.solid ? 1 : 0, b.x, b.y, b.w, b.h);
                }
                SDL_UnlockAudioDevice(device);
                std::fclose(out);
            }
            fonts.load(renderer, ttf, fontsAt);
            app.draw(renderer, fonts); // the picture, if one is asked for, is at the size asked for
        }
        // A picture of the screen as it is now, kept as a BMP file.
        const auto keepPicture = [&](const char* path) {
            SDL_Surface* picture = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
            if (picture && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, picture->pixels, picture->pitch) == 0) SDL_SaveBMP(picture, path);
            if (picture) SDL_FreeSurface(picture);
        };
        if (!running && themes) {
            // The same moment drawn once in each theme, each kept as a picture; the sound held still
            // meanwhile, so the six differ only in their colours. Then back to the theme it had.
            const int was = app.session().theme();
            SDL_LockAudioDevice(device);
            for (int t = 0; t < kThemes; ++t) {
                app.session().setTheme(t);
                ui::useTheme(t);
                if (learning) learner.draw(renderer, fonts); // (--learn: the controls setup)
                else app.draw(renderer, fonts);
                keepPicture((std::string(themes) + std::to_string(t) + ".bmp").c_str());
            }
            SDL_UnlockAudioDevice(device);
            app.session().setTheme(was);
            ui::useTheme(was);
            if (learning) learner.draw(renderer, fonts);
            else app.draw(renderer, fonts);
        }
        if (!running && shot && *shot) keepPicture(shot);
        if (framesDir && shot && draw && running) {
            char frame[512];
            std::snprintf(frame, sizeof frame, "%s/%04d.bmp", framesDir, framesKept++);
            keepPicture(frame);
        }
        if (draw) SDL_RenderPresent(renderer);
        wait.rest(input); // 16 ms in four naps, and a button pressed meanwhile ends it: the next pass reads it at once
    }

    app.session().saveNow(); // the last couple of seconds of changes
    app.session().keepSettingsNow(); // ...and a setting or the volume changed in the last second (they wait to stand still)
    // Saved: now the system may turn the handheld off (its supervisor reads the mark once we have gone).
    if (offWhy && powerOff.turnsOff()) {
        if (powerOff.writeMark(offWhy)) std::printf("turning the handheld off (%s)\n", offWhy);
        else std::fprintf(stderr, "could not write %s: the handheld will start Apotheca again\n", powerOff.mark.c_str());
    }
    if (device) SDL_CloseAudioDevice(device);
    sticks.closeAll();
    // The letters go before the renderer that made them: it takes its textures with it, and a
    // font that then let its own go would be letting it go twice (stills ended in a fault that way).
    fonts.unload();
    fixedFonts.unload();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (smoke) {
        if (app.drums().level(0, 0) <= 0.f) { std::fprintf(stderr, "smoke: pressing A placed no hit\n"); return 1; }
        if (!app.project().playing) { std::fprintf(stderr, "smoke: pressing START did not play\n"); return 1; }
        if (loudest <= 0.f) { std::fprintf(stderr, "smoke: the pattern made no sound\n"); return 1; }
    }
    return 0;
}
