// Apotheca app shell: a 640x480 window, an audio device, and the handheld's
// controls. All sound comes from the engine library and all behaviour from
// App; this file only connects them to the screen, the speaker and the
// buttons.
//
// Everything is written against the handheld's buttons (input.hpp). The
// keyboard only stands in for them; see the README for the key map.
// SELECT + START quits.
//
// For checks and stills:
//   --smoke        place a hit and press play through the keyboard stand-in,
//                  check that sound came out, quit after one second
//   --demo         start with a song already made: drums and two Phazed tracks
//   --view NAME    with --demo, open on: song, drums, notes, phazed, kit,
//                  add-track, page1 .. page9 (Phazed's advanced pages), or
//                  twin, twin1 .. twin3 (Twin Analog's main panel and pages)
//   --hold-a, --hold-y, --on-name, --on-tempo, --dice, --paint, --paint-random
//                  with --demo: a gesture held on the drum grid
//   --shot FILE    save the last frame drawn as a BMP picture
//   --battery DIR  read the battery from DIR (laid out like /sys/class/power_supply)
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
#include <sys/stat.h>
#include <ctime>
#include <string>

#include "version.hpp"
#include "apotheca/engine.hpp"
#include "app.hpp"
#include "controls.hpp"
#include "input.hpp"
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

void pushKey(SDL_Keycode key, bool down) {
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = key;
    SDL_PushEvent(&event);
}
void tapKey(SDL_Keycode key, int times = 1) {
    for (int i = 0; i < times; ++i) {
        pushKey(key, true);
        pushKey(key, false);
    }
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
    const float levels[3] = {0.5f, 0.35f, 0.2f}; // room under the ceiling for three tracks
    for (int track = 0; track < 3; ++track) {
        Command c;
        c.type = Command::SetTrackLevel;
        c.track = track;
        c.value = levels[track];
        session.engine.send(c);
    }
}

} // namespace

int main(int argc, char** argv) {
    // Every line reaches the log as it is written. On the handheld the log is
    // a file, and the program is often stopped by the firmware, not by
    // quitting: without this, nothing it said would be in the file.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    bool learn = false; // ask for each button again, whatever has been learned
    bool handheld = false; // started by the handheld's launcher: with no controls learned yet, ask, joystick or not
    bool smoke = false, demo = false, holdA = false, holdY = false, onName = false, onTempo = false, dice = false;
    int paint = 0; // 1: R1, 2: L1
    const char* view = "";
    const char* shot = nullptr;
    const char* batteryFolder = nullptr;
    const char* songs = nullptr;
    const char* samples = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--learn") == 0) learn = true;
        else if (std::strcmp(argv[i], "--handheld") == 0) handheld = true;
        else if (std::strcmp(argv[i], "--demo") == 0) demo = true;
        else if (std::strcmp(argv[i], "--hold-a") == 0) holdA = true;
        else if (std::strcmp(argv[i], "--hold-y") == 0) holdY = true;
        else if (std::strcmp(argv[i], "--dice") == 0) dice = true;
        else if (std::strcmp(argv[i], "--paint") == 0) paint = 1;
        else if (std::strcmp(argv[i], "--paint-random") == 0) paint = 2;
        else if (std::strcmp(argv[i], "--on-name") == 0) onName = true;
        else if (std::strcmp(argv[i], "--on-tempo") == 0) onTempo = true;
        else if (std::strcmp(argv[i], "--view") == 0 && i + 1 < argc) view = argv[++i];
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--battery") == 0 && i + 1 < argc) batteryFolder = argv[++i];
        else if (std::strcmp(argv[i], "--songs") == 0 && i + 1 < argc) songs = argv[++i];
        else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc) samples = argv[++i];
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
        if (!fonts.small.load(renderer, ttf, 13.5f) || !fonts.medium.load(renderer, ttf, 16.f) ||
            !fonts.large.load(renderer, ttf, 18.5f)) {
            std::fprintf(stderr, "could not load the lettering (routed-gothic-narrow.ttf)\n");
            SDL_Quit();
            return 1;
        }
    }

    // ---- the controls ----------------------------------------------------------------
    // What the device has, for the log: a start that hears no buttons leaves
    // the reason behind.
    static ControlLearner learner;
    const int joysticks = SDL_NumJoysticks();
    std::vector<SDL_Joystick*> opened;
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
    bool learning = !mapped && !smoke && (!shot || learn) && (joysticks > 0 || learn || handheld);
    SDL_GameController* pad = nullptr;
    if (!mapped && !learning) {
        for (SDL_Joystick* stick : opened) SDL_JoystickClose(stick);
        opened.clear();
        for (int i = 0; i < joysticks && !pad; ++i) {
            if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
        }
    }
    std::printf("controls: %s\n", mapped ? "as learned (controls.txt)" : learning ? "not learned yet: asking for each button"
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

    SDL_AudioSpec want = {};
    want.freq = 48000; // SDL converts if the device differs
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256; // about 5 ms
    want.callback = audioCallback;
    want.userdata = &audio;
    SDL_AudioSpec have = {};
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device) {
        std::fprintf(stderr, "could not open audio: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    engine.prepare(static_cast<float>(have.freq)); // builds the drum kit for this sample rate
    engine.watchLoad(!smoke && !shot);
    // The handheld has four processor cores: the tracks are worked out across them, one fewer
    // than there are (one is left for the screen and everything else).
    {
        const unsigned cores = std::thread::hardware_concurrency();
        const int helpers = cores >= 4 ? 2 : (cores >= 2 ? 1 : 0);
        if (!smoke && !shot) engine.useWorkers(helpers, [] { makeThreadRealtime(69); });
        std::printf("processor cores: %u; tracks shared across %d\n", cores, engine.workers() + 1);
    } // real sound: if it cannot keep up, do plainer work, not stutter
    std::printf("Apotheca %s, built %s\n", kVersion, kBuilt);
    std::printf("audio: %d Hz, %d frames per buffer\n", have.freq, have.samples);

    Input input;
    if (mapped) input.useMap(&controlMap);
    static SdlInput recordingInput;
    App app(engine);
    if (batteryFolder) app.useBatteryFolder(batteryFolder);
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
        if (!smoke && !shot) app.session().startMidi(); // USB keyboards, whenever one is plugged in
        app.session().useChordFolder(beside);
        if (app.session().packs.size() == 1) app.session().useChordFolder(beside.substr(0, beside.size() - 6) + "../assets/chords");
        std::printf("chord packs: %d\n", static_cast<int>(app.session().packs.size()));
        // Recordings for drum lanes: WAV files in a "samples" folder beside the program.
        const std::string folder = samples ? samples : beside.substr(0, beside.size() - 6) + "samples";
        if (!samples && !smoke && !shot) mkdir(folder.c_str(), 0755); // so there is somewhere to copy them to
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

    // ---- what to show, for stills ---------------------------------------------------
    if (std::strcmp(view, "song") == 0) {
        app.session().view = View::Song;
    } else if (std::strcmp(view, "deleting") == 0) {
        // B held on a track: its head a second into "DELETING TRACK".
        app.session().view = View::Song;
        pushKey(SDLK_z, true);
    } else if (std::strncmp(view, "effects", 7) == 0) {
        // Step effects on the drum grid: hits that are pitched, ratcheted, backwards and cut; with X held on one.
        PatternScreen& drums = app.drums();
        const auto fx = [&](int lane, int step, int pitch, int ratchet, bool reverse, int cut) {
            StepFx f;
            f.pitch = static_cast<std::int8_t>(pitch);
            f.ratchet = static_cast<std::uint8_t>(ratchet);
            f.reverse = reverse;
            f.cut = static_cast<std::uint8_t>(cut);
            if (drums.level(lane, step) <= 0.f) drums.setStep(lane, step, 0.8f);
            drums.setFx(lane, step, f);
        };
        fx(1, 4, 0, 3, false, 0);
        fx(1, 12, 5, 0, false, 0);
        fx(1, 15, 0, 4, false, 2);
        fx(0, 6, -7, 0, true, 0);
        fx(2, 2, 0, 2, false, 0);
        fx(2, 6, 12, 0, false, 3);
        fx(4, 10, -12, 2, true, 1);
        fx(5, 3, 0, 0, true, 0);
        drums.moveCursorTo(1, 4);
        if (std::strcmp(view, "effects-held") == 0) pushKey(SDLK_s, true); // X held
    } else if (std::strcmp(view, "under") == 0) {
        // A held chord with notes written under it, and a short note under a long one.
        Session& session = app.session();
        Project& project = app.project();
        project.track = 2;
        NotePattern& p = project.tracks[2].notes[0][0];
        p = NotePattern();
        const ChordPack& pack = builtInPack();
        p.chords[0] = makeChord(pack.chords[0], 60, 0, 12.f, .7f, 0);
        int keys[kChordNotes];
        p.chords[0].pitches(keys);
        p.notes[0] = NoteEvent{4, keys[1], 2.f, 0.f, 0.9f};
        p.notes[1] = NoteEvent{8, keys[2], 3.f, 0.f, 0.6f};
        p.notes[2] = NoteEvent{2, 70, 10.f, 0.f, 0.8f};
        p.notes[3] = NoteEvent{6, 70, 2.f, 0.f, 0.8f};
        p.notes[4] = NoteEvent{13, keys[0], 2.f, 0.f, 0.8f}; // after the chord has ended: plain
        session.sendNotePattern(2, 0);
        app.notes().moveCursorTo(72, 14);
    } else if (std::strcmp(view, "songs") == 0) {
        app.session().view = View::Song;
        tapKey(SDLK_BACKSPACE); // SELECT: the list of songs
    } else if (std::strcmp(view, "sampler") == 0) {
        // With --samples: a sampler track playing the first recording.
        app.session().addTrack(TrackKind::Sampler);
        app.project().track = app.project().trackCount() - 1;
        const std::vector<std::string> names = app.session().sampleNames();
        if (!names.empty()) app.session().setTrackSample(app.project().track, names[0]);
        app.session().setSamplerSetting(app.project().track, sampler::Start, 12);
        app.session().view = View::Device;
        app.sampler().goTo(3);
    } else if (std::strncmp(view, "bars", 4) == 0) {
        // Scene 1 with cells of different lengths: the pads four bars (a
        // chord a bar, the last held over two), the bass two, the drums one.
        Session& session = app.session();
        Project& project = app.project();
        session.setCellBars(2, 0, 4);
        const ChordPack& pack = builtInPack();
        const int order[3] = {10, 7, 11};
        for (int bar = 1; bar < 3; ++bar) {
            NotePattern& p = project.tracks[2].notes[0][bar];
            p.chords[0] = makeChord(pack.chords[static_cast<std::size_t>(order[bar])], 57, 0, bar == 2 ? 28.f : 14.f, .7f, 0);
        }
        project.tracks[2].notes[0][0].chords[0].length = 14.f;
        project.tracks[2].notes[0][0].chords[1] = ChordEvent();
        session.sendNotePattern(2, 0);
        session.setCellBars(1, 0, 2);
        project.tracks[1].notes[0][1] = project.tracks[1].notes[1][0];
        session.sendNotePattern(1, 0);
        if (std::strcmp(view, "bars-roll") == 0) {
            project.track = 2;
            project.bar = 3;
            app.notes().moveCursorTo(64, 4);
        } else {
            session.view = View::Song;
            app.song().moveCursorTo(0, 2);
        }
    } else if (std::strcmp(view, "fx") == 0) {
        app.session().setEffect(0, 0, EffectKind::Delay);
        app.session().setEffect(0, 1, EffectKind::Filter);
        app.session().setEffectParam(0, 1, 0, 0.45f);
        app.session().setEffect(kMasterTrack, 0, EffectKind::Reverb);
        app.session().view = View::Effects;
        app.fx().goTo(0, 2);
    } else if (std::strcmp(view, "panic") == 0) {
        for (SDL_Keycode key : {SDLK_1, SDLK_2, SDLK_q, SDLK_w}) pushKey(key, true); // all four shoulder buttons
        for (SDL_Keycode key : {SDLK_1, SDLK_2, SDLK_q, SDLK_w}) pushKey(key, false);
    } else if (std::strcmp(view, "wifi") == 0) {
        app.session().view = View::Song;
        tapKey(SDLK_BACKSPACE); // SELECT: the list
        tapKey(SDLK_q);         // L1: files over WiFi
    } else if (std::strcmp(view, "revs") == 0) {
        app.session().view = View::Song;
        tapKey(SDLK_BACKSPACE); // SELECT
        tapKey(SDLK_s, 2);      // X twice: two revs
        tapKey(SDLK_DOWN);
        tapKey(SDLK_x);         // A on the open song: its revs
    } else if (std::strcmp(view, "add-track") == 0) {
        app.session().view = View::Song;
        app.song().moveCursorTo(0, app.project().trackCount());
        tapKey(SDLK_x);
    } else if (std::strncmp(view, "chords", 6) == 0) {
        // The pads, with Y held: "chords" to place one, "chords-swap" on one.
        app.project().track = 2;
        const bool swap = std::strcmp(view, "chords-swap") == 0;
        app.notes().moveCursorTo(60, swap ? 2 : 14);
        if (swap) app.session().choosePack(1);
        app.session().view = View::Pattern;
        pushKey(SDLK_a, true); // Y, held
        tapKey(SDLK_RIGHT, swap ? 1 : 2);
    } else if (std::strcmp(view, "notes") == 0) {
        app.project().track = 1;
        app.notes().moveCursorTo(57, 6);
    } else if (std::strcmp(view, "phazed") == 0) {
        app.project().track = 1;
        app.session().view = View::Device;
        app.phazed().goTo(0, SynthScreen::kFirstMacro);
        pushKey(SDLK_x, true);  // hold A on TONE
        tapKey(SDLK_UP, 6);     // and turn it up
    } else if (std::strncmp(view, "page", 4) == 0) {
        app.project().track = 1;
        app.session().view = View::Device;
        app.phazed().goTo(std::atoi(view + 4), 1);
    } else if (std::strncmp(view, "twin", 4) == 0) {
        // A Twin Analog track, added the way the Song screen adds one.
        app.session().addTrack(TrackKind::Twin);
        app.project().track = app.project().trackCount() - 1;
        app.session().view = View::Device;
        app.twin().goTo(std::atoi(view + 4), view[4] ? 0 : SynthScreen::kFirstMacro + 1);
    } else if (std::strcmp(view, "kit") == 0 || std::strncmp(view, "slices", 6) == 0 || std::strcmp(view, "starts") == 0 || std::strcmp(view, "picker") == 0) {
        // With --samples: the first recording goes on lane 6, fitted to one
        // bar, with hits on the beat whose starts have been rolled.
        const std::vector<std::string> names = app.session().sampleNames();
        if (!names.empty()) {
            // The break, if there is one, for the slice view; else the first recording there is.
            std::string pick = names[0];
            if (std::strncmp(view, "slices", 6) == 0)
                for (const std::string& n : names)
                    if (n.find("break") != std::string::npos) pick = n;
            app.session().setLaneSample(0, 5, pick);
            app.session().setLaneFit(0, 5, std::strcmp(view, "slices-32") == 0 ? 32 : 16);
            app.session().finishStretching();
            for (int step = 0; step < kSteps; ++step) app.drums().setStep(5, step, 0.f);
            const float starts[8] = {0.f, 0.375f, 0.25f, 0.75f, 0.f, 0.125f, 0.5f, 0.875f};
            for (int i = 0; i < 8; ++i) {
                app.drums().setStep(5, i * 2, 0.9f);
                app.drums().setStart(5, i * 2, starts[i]);
            }
        }
        if (std::strncmp(view, "slices", 6) == 0) {
            // The slice view: the break laid out in order, four of its slices moved, one hit taken out.
            for (int step = 0; step < kSteps; ++step) {
                app.drums().setStep(5, step, step % 4 == 0 ? 1.f : 0.75f);
                app.drums().setStart(5, step, step / 16.f);
            }
            app.drums().setStart(5, 5, 12.f / 16.f);
            app.drums().setStart(5, 6, 2.f / 16.f);
            app.drums().setStart(5, 10, 3.f / 16.f);
            app.drums().setStart(5, 13, 9.f / 16.f);
            app.drums().setStep(5, 11, 0.f);
            app.session().setLaneOneSlice(0, 5, true);
            if (std::strcmp(view, "slices-32") == 0) app.drums().setStart(5, 5, 25.f / 32.f + 0.012f); // on a finer cut, and slid a little off it
            app.drums().moveCursorTo(5, std::strcmp(view, "slices-name") == 0 ? -1 : 5);
            app.drums().setSlicing(true);
        } else if (std::strcmp(view, "starts") == 0) {
            app.drums().moveCursorTo(5, 6);
            pushKey(SDLK_a, true); // Y held
            pushKey(SDLK_l, true); // the stick, to the right
        } else {
            app.session().view = View::Device;
            app.kit().goTo(5, KitScreen::kSound);
            if (std::strcmp(view, "picker") == 0) {
                tapKey(SDLK_x); // the list
                tapKey(SDLK_DOWN, 3);
            }
        }
    } else if (std::strcmp(view, "setup") == 0 || std::strcmp(view, "buttons") == 0) {
        app.session().view = View::Settings;
        app.settings().goTo(std::strcmp(view, "buttons") == 0 ? SettingsScreen::kTest : SettingsScreen::kStick);
        if (std::strcmp(view, "buttons") == 0) {
            tapKey(SDLK_x);
            pushKey(SDLK_w, true);
            pushKey(SDLK_l, true);
        }
    } else if (std::strcmp(view, "typing") == 0) {
        app.session().view = View::Settings;
        app.settings().goTo(SettingsScreen::kTrackName);
        tapKey(SDLK_x);
        for (const char* k = "\x01\x01x\x02x\x02\x02x"; *k; ++k) tapKey(*k == 1 ? SDLK_RIGHT : *k == 2 ? SDLK_DOWN : SDLK_x);
    } else if (std::strncmp(view, "qmod", 4) == 0) {
        app.session().addTrack(TrackKind::Qmod);
        app.project().track = app.project().trackCount() - 1;
        app.session().view = View::Device;
        if (view[4]) app.qmod().goTo(std::atoi(view + 4), 0);
    } else if (std::strcmp(view, "help") == 0) {
        pushKey(SDLK_TAB, true); // MENU, held
    } else if (std::strcmp(view, "kit-old") == 0) {
        app.session().view = View::Device;
    }

    if (smoke) {
        // What a tester would do: A to place a kick, START to play.
        tapKey(SDLK_x);
        tapKey(SDLK_RETURN);
    } else if (demo && !*view) {
        tapKey(SDLK_RETURN);
    } else if (demo && std::strcmp(view, "panic") == 0) {
        // nothing more: any press would put the notice away
    } else if (demo && std::strcmp(view, "add-track") != 0) {
        tapKey(SDLK_RETURN); // playing, so the stills show the play position and the scope
    }
    // A gesture held on the drum grid.
    PatternScreen& drums = app.drums();
    if (dice) {
        drums.moveCursorTo(5, -1);
        pushKey(SDLK_a, true);
        tapKey(SDLK_UP, 6);
    } else if (paint) {
        drums.moveCursorTo(5, 2);
        pushKey(paint == 1 ? SDLK_w : SDLK_2, true);
        tapKey(SDLK_RIGHT, 10);
    } else if (onName) {
        drums.moveCursorTo(1, -1);
        pushKey(SDLK_x, true);
        tapKey(SDLK_DOWN, 3);
    } else if (onTempo) {
        drums.moveCursorTo(0, 0);
        tapKey(SDLK_UP);
        pushKey(SDLK_x, true);
    } else if (holdA) {
        drums.setStep(drums.cursorLane(), drums.cursorStep(), 0.55f);
        drums.setNudge(drums.cursorLane(), drums.cursorStep(), 0.3f);
        pushKey(SDLK_x, true);
        tapKey(SDLK_RIGHT);
    } else if (holdY) {
        pushKey(SDLK_a, true);
        tapKey(SDLK_DOWN, 4);
    }

    bool running = true;
    bool startedFine = false;
    bool reported = false;
    float loudest = 0.f;
    const Uint32 start = SDL_GetTicks();

    int rawLogged = 0;
    // Start the controls setup again, whatever state the controls are in.
    RelearnHold relearnHold;
    const auto learnAgain = [&](const char* why) {
        std::printf("controls: setting up again (%s)\n", why);
        if (pad) SDL_GameControllerClose(pad), pad = nullptr;
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
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            // Desk testing only: the handheld has no Escape key.
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            // The handheld's volume buttons, if the firmware passes them on as the usual keys.
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_VOLUMEUP || event.key.keysym.sym == SDLK_VOLUMEDOWN)) {
                app.changeVolume(event.key.keysym.sym == SDLK_VOLUMEUP ? 0.1f : -0.1f);
                continue;
            }
            if (mapped && !learning) relearnHold.handle(event);
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

            ButtonEvent press;
            if (!input.handle(event, press)) continue;
            if (input.held(Button::Select) && input.held(Button::Start)) running = false;
            else app.onButton(press, input);
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
        if (!learning) app.update(input, SDL_GetTicks());
        if (!learning && app.settings().takeRelearn()) learnAgain("asked for on the SETUP screen");
        if (!learning && mapped && relearnHold.update(SDL_GetTicks())) learnAgain("a button was held at the start");
        // Three seconds in and still going: this start worked. (After an
        // update, that is what tells the launcher to keep the new program.)
        if (!startedFine && SDL_GetTicks() - start > 3000) {
            startedFine = true;
            app.session().startedFine();
        }

        static int lastLimit = 3;
        if (engine.qualityLimit() != lastLimit) {
            lastLimit = engine.qualityLimit();
            std::printf("keeping up: oversampling held to x%d at most (the last block took %.0f%% of its time)\n", 1 << lastLimit, 100.f * engine.load());
        }
        if (!reported && audio.realtime.load() >= 0) {
            reported = true;
            std::printf("realtime priority: %s\n", audio.realtime.load() == 1 ? "granted" : "not granted");
        }
        const float peak = engine.trackPeak(0);
        if (peak > loudest) loudest = peak;

        if (learning) learner.draw(renderer, fonts);
        else app.draw(renderer, fonts);
        if (!learning && mapped && !shot && relearnHold.offering()) {
            // A few seconds at the start: the way back into the setup.
            const int y = kScreenH - ui::kHintsH;
            ui::fill(renderer, 0, y, kScreenW, ui::kHintsH, ui::kPanel2);
            ui::fill(renderer, 0, y, static_cast<int>(kScreenW * relearnHold.part()), 3, ui::kBrass);
            fonts.small.drawCentered(renderer, kScreenW / 2.f, y + (ui::kHintsH - fonts.small.capHeight()) / 2.f + 1.f,
                                     "BUTTONS NOT WORKING? HOLD ANY BUTTON FOR 2 SECONDS NOW TO SET THEM UP AGAIN", ui::kBrass);
        }
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

    app.session().saveNow(); // the last couple of seconds of changes
    SDL_CloseAudioDevice(device);
    if (pad) SDL_GameControllerClose(pad);
    for (SDL_Joystick* stick : opened) SDL_JoystickClose(stick);
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
