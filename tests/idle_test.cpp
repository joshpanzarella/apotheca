// The screen is drawn only when it can have changed (app/redraw.hpp). This holds that rule to the
// picture: a long session through every screen and the gestures that start something timed (a
// message, a hold's bar, a line over a recording, the list's resting row, a bounce, a keyboard's
// key...) is run a 16 ms pass at a time, as main.cpp runs it, with the sound made beside it. EVERY
// pass is drawn here, into a screen in memory, and every pass the program would have left out
// must be the last picture it showed, pixel for pixel. One pixel off fails, with the pass and the
// part of the session it was in.
//
// The margin (Redraw::kKeepDrawingMs) is 0 here: the reasons alone must be enough. And each
// reason is needed: the session is run again with each one taken away (Redraw::ignore), and must
// then fail (but the three `belts`, known to be covered by others, which are not run). A run with
// a reason taken away draws only what it shows and what it decides unlike the program as it is.
//
//   idle_test            the session, and every reason taken away
//   idle_test --part N   the session, and the Nth third of the reasons (ctest: idle1 to idle3)
//   idle_test --quick    the session alone
//   idle_test --measure  a pass's update, its asking and its drawing, on five screens left alone
//   IDLE_PRINT=WORDS     each pass's reasons, in the parts whose name has WORDS in it

#include <SDL.h>

#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "redraw.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;
const SDL_Keycode STICK_DOWN = SDLK_k, STICK_RIGHT = SDLK_l;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const int kPassMs = 16, kPassSound = 768; // one pass of main.cpp's loop, and the sound it lasts

std::string tempFolder(const char* what) {
    std::string pattern = std::string("/tmp/apotheca-idle-") + what + "-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back(0);
    const char* made = mkdtemp(buffer.data());
    return made ? made : "/tmp";
}

std::vector<float> tone(float seconds, float level) {
    std::vector<float> out(static_cast<std::size_t>(seconds * 48000.f));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = level * std::sin(0.5f + 6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
    return out;
}

// A made-up MIDI keyboard: a pipe the reader opens (as midi_test's).
struct Keyboard {
    std::string path;
    int fd = -1;
    Keyboard(const std::string& folder, const char* name) : path(folder + "/" + name) { mkfifo(path.c_str(), 0600); }
    void plugIn() { fd = open(path.c_str(), O_WRONLY); }
    void send(std::vector<unsigned char> bytes) {
        if (fd >= 0 && write(fd, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) std::printf("    (the keyboard's bytes did not all go)\n");
    }
    ~Keyboard() {
        unlink(path.c_str());
        if (fd >= 0) close(fd);
    }
};

// A file sent to the WiFi page, as a browser would (as transfer_test's): the status it answered.
int put(int port, const std::string& path, const std::string& body) {
    const std::string request = "PUT " + path + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in where = {};
    where.sin_family = AF_INET;
    where.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &where.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0) {
        close(fd);
        return 0;
    }
    for (std::size_t sent = 0; sent < request.size();) {
        const ssize_t n = send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    shutdown(fd, SHUT_WR);
    std::string all;
    char block[4096];
    for (ssize_t n = recv(fd, block, sizeof block, 0); n > 0; n = recv(fd, block, sizeof block, 0)) all.append(block, static_cast<std::size_t>(n));
    close(fd);
    std::size_t at = 0;
    while (all.compare(at, 12, "HTTP/1.1 100") == 0) at = all.find("\r\n\r\n", at) + 4;
    return all.size() > at + 12 ? std::atoi(all.c_str() + at + 9) : 0;
}

// A program for this machine, as the WiFi page takes one (transfer_test's aProgram).
std::string aProgram() {
#if defined(__aarch64__)
    const unsigned machine = 183;
#else
    const unsigned machine = 62;
#endif
    std::string b(200 * 1024, '\0');
    b[0] = 0x7F, b[1] = 'E', b[2] = 'L', b[3] = 'F', b[4] = 2, b[5] = 1, b[16] = 3;
    b[18] = static_cast<char>(machine & 255);
    b[19] = static_cast<char>(machine >> 8);
    for (std::size_t i = 64; i < b.size(); ++i) b[i] = static_cast<char>(i * 31);
    return b;
}

// A made-up microphone: silence for a moment, then a tone, as much as is asked for.
struct Microphone : AudioInput {
    int reads = 0;
    bool start() override { reads = 0; return true; }
    void stop() override {}
    int read(float* out, int most) override {
        if (++reads % 2 == 0) return 0; // (one block a frame: the Session reads until nothing comes)
        const int n = most < 768 ? most : 768;
        for (int i = 0; i < n; ++i) out[i] = reads > 20 ? 0.3f * std::sin(0.05f * static_cast<float>(i)) : 0.f;
        return n;
    }
};

const char* viewName(View v) {
    switch (v) {
    case View::Song: return "SONG";
    case View::Pattern: return "PATTERN";
    case View::Device: return "DEVICE";
    case View::Effects: return "FX";
    case View::Mods: return "MOD";
    case View::Settings: return "SETUP";
    }
    return "?";
}

struct Found {};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    std::unique_ptr<App> app{new App(*engine)};
    Canvas canvas;
    Redraw redraw{0}; // no margin: the reasons alone
    Redraw asItIs{0}; // with a reason taken away: what the program would do with every reason in
    std::vector<unsigned char> ttf = loadFontFile();
    int fontsAt = kTextLarge;
    Uint32 clock = 1000;
    bool stirred = false;
    bool mutant = false; // a reason taken away: stop drawing at the first wrong picture
    std::vector<unsigned char> shown, now;
    const char* part = "start";
    int passes = 0, drawn = 0, wrong = 0;
    double drawSeconds = 0.0;
    unsigned alone = 0; // every reason that was, on some pass, the only one
    std::string firstWrong;

    Rig(const std::string& samples, unsigned ignored) {
        mutant = ignored != 0;
        redraw.ignore(ignored);
        app->session().useSampleFolder(samples);
        app->useBatteryFolder(samples); // no battery: nothing read from this machine
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        stirred = true; // as main.cpp: any key of the window
        ButtonEvent press;
        if (input.handle(event, press)) app->onButton(press, input);
    }
    void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    // A change made by hand in the test, not by a button: drawn as if a button made it.
    void byHand(const std::function<void()>& change) {
        change();
        stirred = true;
    }
    // One pass of main.cpp's loop: the app moves on, the sound is made, and the picture is drawn
    // (here always) and held to the last one shown where the program would leave it out.
    void pass() {
        sound(); // the audio thread, between two passes (in the program: while main.cpp waits its 16 ms)
        clock += kPassMs;
        app->update(input, clock);
        const bool wanted = redraw.wanted(*app, input, stirred, clock);
        // With a reason taken away, the program as it is runs beside it: a pass neither would draw
        // is one the first run (every reason in) has already held to the picture, so it is not
        // drawn again here. Only what the two decide differently, and what the mutant shows, is.
        const bool whole = mutant ? asItIs.wanted(*app, input, stirred, clock) : true; // (every reason in: every pass drawn)
        const unsigned stirs = redraw.stirs(), why = redraw.why();
        if (stirs && (stirs & (stirs - 1)) == 0) alone |= stirs; // the one stir of this pass: without it, this pass or the next can go undrawn
        if (why == kStirAfter) alone |= kStirAfter;
        if (std::getenv("IDLE_PRINT") && !mutant && std::strstr(part, std::getenv("IDLE_PRINT"))) {
            std::printf("    pass %d:", passes + 1);
            for (unsigned bit = 1; bit; bit <<= 1)
                if (why & bit) std::printf(" %s", stirName(bit));
            std::printf("\n");
        }
        stirred = false;
        ++passes;
        drawn += wanted ? 1 : 0;
        if (app->session().textSize() != fontsAt) {
            fontsAt = app->session().textSize();
            canvas.fonts->load(canvas.r, ttf, fontsAt);
        }
        if (!wanted && !whole) return;
        const auto t0 = std::chrono::steady_clock::now();
        canvas.clear();
        app->draw(canvas.r, *canvas.fonts);
        drawSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const unsigned char* pixels = static_cast<const unsigned char*>(canvas.surface->pixels);
        now.assign(pixels, pixels + canvas.surface->pitch * canvas.surface->h);
        if (wanted) {
            shown.swap(now);
        } else if (now != shown) {
            if (wrong++ == 0) {
                int differ = 0;
                for (std::size_t i = 0; i < now.size(); i += 4) differ += std::memcmp(&now[i], &shown[i], 4) != 0;
                char text[200];
                std::snprintf(text, sizeof text, "pass %d (%s, on %s): %d pixels differ from the last picture shown", passes, part, viewName(app->session().view), differ);
                firstWrong = text;
            }
            if (mutant) throw Found(); // a reason taken away has shown itself needed: no need to go on
        }
    }
    int stalled = 0; // passes left with no sound made: the audio thread held up
    void sound() {
        if (stalled > 0) {
            --stalled;
            return;
        }
        std::vector<float> out(2 * kPassSound);
        engine->render(out.data(), kPassSound);
    }
    void run(int ms) { for (int at = 0; at < ms; at += kPassMs) pass(); }
    // A button held for `ms`, then let go.
    void hold(SDL_Keycode code, int ms) {
        key(code, true);
        run(ms);
        key(code, false);
        pass();
    }
    void press(SDL_Keycode code) { tap(code); pass(); }
    Session& session() { return app->session(); }
    Project& project() { return app->project(); }
};

// ---- the session ----------------------------------------------------------------------------------

// Everything a player might leave the handheld on, the gestures that start something timed, and
// a rest after each so the picture can settle and be left alone.
void theSession(Rig& rig, const std::string& samples) {
    Session& s = rig.session();
    Microphone mic;
    rig.part = "the drum grid at rest";
    rig.run(1000);

    rig.part = "the stick held on the drum grid";
    rig.hold(STICK_RIGHT, 400);
    rig.run(600);

    rig.part = "the volume and a message";
    rig.key(SELECT, true);
    rig.press(R1);
    rig.key(SELECT, false);
    rig.run(2000);
    rig.key(SELECT, true);
    rig.press(B); // NOTHING TO UNDO
    rig.key(SELECT, false);
    rig.run(2000);

    rig.part = "a panic, let go";
    rig.key(L1, true), rig.key(L2, true), rig.key(R1, true), rig.key(R2, true);
    rig.run(300);
    rig.key(L1, false), rig.key(L2, false), rig.key(R1, false), rig.key(R2, false);
    rig.run(4500);

    rig.part = "a line said on the drum grid";
    rig.app->drums().moveCursorTo(0, -1); // the first lane's name
    rig.press(L1);                        // a built-in drum has no slices: it says why
    rig.run(3500);

    rig.part = "a song played and stopped, a COMP on the track";
    rig.byHand([&] {
        s.setEffect(0, 0, EffectKind::Comp);
        for (int step = 0; step < 16; step += 2) rig.app->drums().moveCursorTo(0, step), rig.app->drums().onButton({Button::A, true}, rig.input), rig.app->drums().onButton({Button::A, false}, rig.input);
    });
    rig.press(R2), rig.press(R2); // FX
    rig.press(START);
    rig.run(2500);
    rig.press(START);
    rig.run(3000); // the meter lets go after the sound

    rig.part = "a hold of B on an effect, called off";
    rig.hold(B, 600);
    rig.run(800);
    rig.part = "a hold of B on an effect, to the end: the COMP deleted";
    rig.hold(B, 1200);
    rig.run(1500); // its meter's last reading goes with it

    rig.part = "a song that stops at its end by itself";
    rig.byHand([&] {
        rig.project().songEnd = kSongStop;
        s.addTrack(TrackKind::Drums); // the track worked on is silent: nothing on LIVE OUTPUT when it stops
        rig.project().track = 1;
    });
    rig.press(L2), rig.press(L2), rig.press(L2); // SONG: START there plays the song, not the scene alone
    rig.run(300);
    rig.part = "a song stopped while the audio thread is held up (SONG)";
    rig.press(START);
    rig.run(1000);
    rig.tap(START);
    rig.stalled = 3; // the Stop is heard three passes later: the playhead goes then, with no button
    rig.run(1000);

    rig.part = "a song that stops at its end by itself (SONG)";
    rig.press(START);
    rig.run(3000); // one bar of one scene: it stops by itself
    CHECK(!rig.project().playing);
    rig.byHand([&] {
        rig.project().songEnd = kSongLoop;
        rig.project().track = 0;
    });
    rig.run(500);
    rig.press(R2), rig.press(R2), rig.press(R2); // FX again

    rig.part = "the kit: a drum heard, a catch said";
    rig.press(L2); // DEVICE: the kit
    rig.run(400);
    rig.press(X);
    rig.run(2500);
    rig.press(Y); // catch: NOTHING TO CATCH, or the bar caught
    rig.run(3000);

    rig.part = "the kit: a long recording heard";
    rig.byHand([&] { s.setLaneSample(0, 0, "long"); });
    rig.run(500);
    rig.press(X);
    rig.run(4000);

    rig.part = "the kit: fitted to the tempo";
    rig.byHand([&] { s.setLaneFit(0, 0, 16); });
    rig.run(1500);
    rig.key(SELECT, true);
    rig.press(RIGHT); // ten beats a minute faster: fitted again once it settles
    rig.key(SELECT, false);
    rig.run(3000);

    rig.part = "the kit: a recording made";
    rig.byHand([&] { s.useAudioInput(&mic); });
    rig.press(R1); // waiting for sound, then recording
    rig.run(1500);
    rig.press(R1); // stopped: RECORDED AS ...
    rig.run(3500);

    rig.part = "the list of recordings, passed over";
    rig.press(DOWN); // the second lane: not fitted
    rig.press(A);    // SOUND: the list
    rig.run(300);
    while (rig.app->kit().picker().cursor() > 0) rig.press(UP);
    while (rig.app->kit().picker().under() && !(rig.app->kit().picker().under()->kind == SamplePicker::Kind::Folder && rig.app->kit().picker().under()->name.empty())) rig.press(DOWN);
    rig.press(RIGHT); // into UNSORTED (the catch made a folder, so the loose ones are there)
    rig.run(300);
    CHECK(rig.app->kit().picker().count() > 30);
    rig.hold(STICK_DOWN, 300); // a row at a time, each on as it comes
    rig.run(1000);
    rig.hold(DOWN, 2000); // held, faster than a row in kAtOnceMs: the one rested on goes on after it
    rig.run(1000);
    rig.part = "the list of recordings: hold B on a folder that is not empty";
    rig.press(LEFT); // back to SAMPLES
    while (rig.app->kit().picker().cursor() > 0) rig.press(UP);
    while (rig.app->kit().picker().under() && !(rig.app->kit().picker().under()->kind == SamplePicker::Kind::Folder && !rig.app->kit().picker().under()->name.empty())) rig.press(DOWN);
    rig.hold(B, 700); // NOT DELETED: IT IS NOT EMPTY, said for a while after
    rig.run(3000);
    rig.press(B); // closed
    rig.run(500);

    rig.part = "the sampler: a recording heard";
    rig.byHand([&] {
        s.addTrack(TrackKind::Sampler);
        rig.project().track = rig.project().trackCount() - 1;
    });
    rig.run(300);
    rig.press(A); // the list
    rig.run(200);
    {
        const SamplePicker& list = rig.app->sampler().picker();
        while (list.under() && !(list.under()->kind == SamplePicker::Kind::Folder && list.under()->name.empty())) rig.press(DOWN);
        rig.press(A); // into UNSORTED
        while (list.under() && list.under()->name != "long") rig.press(DOWN);
        rig.press(A); // LONG: on, and the list closed
        CHECK(!list.isOpen());
    }
    rig.run(500);
    rig.press(X);
    rig.run(1500);
    rig.part = "the sampler: a catch said";
    rig.press(Y);
    rig.run(3000);

    rig.part = "a synth's key heard on its page";
    rig.byHand([&] {
        s.addTrack(TrackKind::Phazed);
        rig.project().track = rig.project().trackCount() - 1;
    });
    rig.run(300);
    rig.press(X);
    rig.run(3000);

    // MOD (mod_knob.hpp): X held on a knob shows its fields in LIVE OUTPUT's place (DEVICE) or the
    // heading's (FX), and while stopped the MOD runs, so NOW moves by itself; let go, they go.
    rig.part = "X held on a macro: its MOD made and heard while stopped, then let go";
    rig.byHand([&] { rig.app->phazed().goTo(0, SynthScreen::kFirstMacro); });
    rig.run(200);
    rig.key(X, true);
    rig.run(300);
    rig.press(UP); // the first UP makes an LFO
    rig.run(1500);
    rig.key(X, false);
    rig.run(3000);
    rig.part = "X held on an FX setting: its MOD made and heard while stopped, then let go";
    rig.byHand([&] { s.setEffect(rig.project().track, 0, EffectKind::Filter); });
    rig.press(R2); // FX
    rig.byHand([&] { rig.app->fx().goTo(0, 1); }); // CUTOFF
    rig.run(200);
    rig.key(X, true);
    rig.press(UP);
    rig.run(1500);
    rig.key(X, false);
    rig.run(3000);
    // The MOD tab (mod_screen.hpp): the shape's picture has a dot at now, and a STEPPER's row its step
    // lit, which move by themselves while X runs a MOD (stopped) or the song plays; at Stop they go.
    rig.part = "the MOD tab: X held on a MOD's line runs it while stopped, then let go";
    rig.press(R2); // MOD
    rig.byHand([&] { rig.app->mods().goTo(0, 0); });
    rig.run(200);
    rig.key(X, true);
    rig.run(1500);
    rig.key(X, false);
    rig.run(3000);
    rig.part = "the MOD tab: a STEPPER's row while the song plays, and stopped";
    rig.byHand([&] {
        ModSettings steps;
        steps.kind = kModStepper, steps.target = kModFirstSend, steps.depth = 0.5f, steps.rate = kModRateStep, steps.length = 4;
        for (int i = 0; i < kModSteps; ++i) steps.steps[i] = i % 2 ? 0.5f : -0.5f;
        s.setMod(rig.project().track, 1, steps);
        rig.app->mods().goTo(3, 0);
    });
    rig.run(300);
    rig.press(START);
    rig.run(1500);
    rig.press(START);
    rig.run(3000);
    rig.press(L2); // FX
    rig.press(L2); // DEVICE again
    rig.run(300);

    rig.part = "a keyboard's key on the roll";
    {
        const std::string midiFolder = tempFolder("midi");
        Keyboard keys(midiFolder, "midiC1D0");
        rig.byHand([&] { s.startMidi(midiFolder); });
        keys.plugIn();
        for (int i = 0; i < 100 && s.midiKeyboards() == 0; ++i) usleep(5000);
        rig.run(200);
        rig.press(L2); // PATTERN: the roll of a sampler track with no recording: the key moves the cursor, and sounds nothing
        rig.byHand([&] {
            s.addTrack(TrackKind::Sampler);
            rig.project().track = rig.project().trackCount() - 1;
        });
        rig.run(500);
        keys.send({0x90, 79, 100});
        usleep(50000);
        rig.run(500);
        keys.send({0x80, 79, 0});
        usleep(50000);
        rig.run(500);
    }

    rig.part = "SETUP: text size, theme, the button test";
    rig.byHand([&] { rig.project().track = 0; });
    rig.press(R2), rig.press(R2), rig.press(R2), rig.press(R2); // SETUP (FX, MOD, SETUP)
    rig.run(300);
    rig.byHand([&] { rig.app->settings().goTo(SettingsScreen::kTextSize); });
    rig.press(RIGHT);
    rig.run(500);
    rig.press(LEFT);
    rig.run(500);
    rig.byHand([&] { rig.app->settings().goTo(SettingsScreen::kTheme); });
    rig.press(RIGHT);
    rig.run(500);
    rig.press(LEFT);
    rig.run(500);
    rig.byHand([&] { rig.app->settings().goTo(SettingsScreen::kTest); });
    rig.press(A);
    rig.run(300);
    rig.press(DOWN);
    rig.run(5000); // it closes by itself

    rig.part = "SETUP: a bounce";
    rig.byHand([&] { s.useBounceFolder(tempFolder("bounces")); });
    rig.byHand([&] { rig.app->settings().goTo(SettingsScreen::kBounce); });
    rig.press(A);
    for (int i = 0; i < 2000 && s.bouncing(); ++i) rig.pass();
    rig.run(1000);

    rig.part = "SETUP: WiFi on, an update sent and accepted";
    const std::string programs = tempFolder("program");
    writeTextFile(programs + "/apotheca", "the one running now");
    rig.byHand([&] {
        s.useSongFolder(tempFolder("songs"), 3);
        s.useProgramFolder(programs);
        rig.app->settings().goTo(SettingsScreen::kWifi);
    });
    rig.press(A);
    rig.run(1500);
    {
        const std::string address = s.transferAddress();
        const std::size_t colon = address.rfind(':');
        const int port = std::atoi(address.c_str() + colon + 1);
        const std::size_t slash = address.find('/', colon);
        CHECK(put(port, address.substr(slash) + "/update/apotheca", aProgram()) == 200);
    }
    rig.run(1500); // the notice comes up by itself
    CHECK(s.updateWaiting());
    rig.hold(A, 1200); // accepted
    rig.press(A);      // WiFi off; UPDATE ACCEPTED stays for five seconds
    rig.run(6000);
    (void)samples;
}

bool runSession(const std::string& samples, unsigned ignored, unsigned* alone = nullptr) {
    std::unique_ptr<Rig> rig(new Rig(samples, ignored));
    try {
        theSession(*rig, samples);
    } catch (const Found&) {
    }
    if (alone) *alone = rig->alone;
    if (!ignored) {
        std::printf("    %d passes, %d drawn (%.0f%%); drawing took %.1f s\n", rig->passes, rig->drawn, 100.0 * rig->drawn / rig->passes, rig->drawSeconds);
        if (rig->wrong) std::printf("    %d passes left out showed the wrong picture; the first: %s\n", rig->wrong, rig->firstWrong.c_str());
    }
    return rig->wrong == 0;
}

} // namespace

// How long a pass takes on a screen left alone: the app's own work (update, the sound left out),
// and the drawing that is now left out, in this program's software renderer.
void measure(const std::string& samples) {
    struct Place { const char* name; View view; };
    const Place places[] = {{"SONG", View::Song}, {"PATTERN (drum grid)", View::Pattern}, {"DEVICE (kit)", View::Device}, {"FX", View::Effects}, {"SETUP", View::Settings}};
    for (const Place& place : places) {
        Rig rig(samples, 0);
        rig.session().view = place.view;
        rig.run(500);
        double work = 0.0, drawing = 0.0, wanted = 0.0;
        const int passes = 300;
        for (int i = 0; i < passes; ++i) {
            rig.clock += kPassMs;
            auto t0 = std::chrono::steady_clock::now();
            rig.app->update(rig.input, rig.clock);
            auto t1 = std::chrono::steady_clock::now();
            rig.redraw.wanted(*rig.app, rig.input, false, rig.clock);
            auto t2 = std::chrono::steady_clock::now();
            rig.canvas.clear();
            rig.app->draw(rig.canvas.r, *rig.canvas.fonts);
            auto t3 = std::chrono::steady_clock::now();
            work += std::chrono::duration<double>(t1 - t0).count();
            wanted += std::chrono::duration<double>(t2 - t1).count();
            drawing += std::chrono::duration<double>(t3 - t2).count();
        }
        std::printf("  %-20s update %.3f ms, asking whether to draw %.4f ms, drawing %.3f ms a pass\n", place.name, 1000.0 * work / passes, 1000.0 * wanted / passes,
                    1000.0 * drawing / passes);
    }
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    const std::string first = argc > 1 ? argv[1] : "";
    const bool quick = first == "--quick";
    const int third = first == "--part" && argc > 2 ? std::atoi(argv[2]) : 0; // 1 to 3: that third of the reasons taken away
    const std::string samples = tempFolder("samples");
    writeWav(samples + "/long.wav", tone(3.f, 0.25f), 48000);
    writeWav(samples + "/short.wav", tone(0.4f, 0.25f), 48000);
    for (int i = 0; i < 40; ++i) { // rows enough that the stick is still among them when let go
        char name[32];
        std::snprintf(name, sizeof name, "/row%02d.wav", i);
        std::vector<float> wave = tone(0.1f, 0.25f);
        wave.resize(wave.size() + 24000, 0.f); // and half a second of nothing: the line runs on where LIVE OUTPUT is still
        writeWav(samples + name, wave, 48000);
    }

    if (first == "--measure") {
        measure(samples);
        return 0;
    }
    std::printf("the session, every reason in\n");
    unsigned alone = 0;
    CHECK(runSession(samples, 0, &alone));
    if (!quick) {
        // Each reason, and which third of them it is in (ctest runs the three side by side: idle1 to idle3).
        // Three are known to be covered by others where the session can show it (kept all the same: the
        // rule is "when unsure, draw", and a later change may leave one alone): PLAYING (nothing moves
        // within a step; the step itself is "place"), RECORDING (the kit and the sampler say how it is
        // going on their own line, "screen said") and the list's resting row ("picker": the row put on
        // is heard, and its line starts at once, "playhead"; the pass it goes on is "after"). Every
        // other one must be needed.
        const unsigned belts = kStirPlaying | kStirRecording | kStirPicker;
        struct Reason { unsigned bit; int third; };
        const Reason reasons[] = {{kStirEvent, 1}, {kStirHeld, 1}, {kStirStick, 1}, {kStirPlaying, 1}, {kStirPlace, 1}, {kStirScope, 1},
                                  {kStirMeters, 1}, {kStirMidi, 1}, {kStirVolume, 1}, {kStirSaid, 1}, {kStirPanicNote, 2},
                                  {kStirUpdateSaid, 2}, {kStirFitting, 2}, {kStirRecording, 2}, {kStirBouncing, 2}, {kStirWifi, 2},
                                  {kStirScreenSaid, 3}, {kStirPlayhead, 3}, {kStirPicker, 3}, {kStirButtonTest, 3}, {kStirAfter, 3}};
        // A reason that was never the only one on a pass changes nothing when taken away: it is not
        // run again, and is said as surviving. The others are run, and must fail.
        for (const Reason& r : reasons) {
            const unsigned reason = r.bit;
            if (third != 0 && r.third != third) continue;
            if (belts & reason) { // (not run again: its survival proves nothing, and it costs seconds)
                std::printf("  without %-12s known to be covered by others; not run\n", stirName(reason));
                continue;
            }
            if (!(alone & reason)) {
                std::printf("  without %-12s SURVIVES: never the only reason in the session%s\n", stirName(reason), belts & reason ? " (known)" : "");
                CHECK(belts & reason);
                continue;
            }
            const auto began = std::chrono::steady_clock::now();
            const bool fine = runSession(samples, reason);
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            std::printf("  without %-12s %s (%.1f s)\n", stirName(reason), fine ? (belts & reason ? "SURVIVES: the picture never needed it (known)" : "SURVIVES: the picture never needed it") : "fails, as it must", took);
            CHECK(!fine || (belts & reason));
            std::fflush(stdout);
        }
    }
    std::printf(failures ? "idle_test: %d FAILED\n" : "idle_test: all passed\n", failures);
    return failures ? 1 : 0;
}
