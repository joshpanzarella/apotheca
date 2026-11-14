// Tests for the sound designer: the page's requests, the presets it saves,
// and the handheld doing what the page asks. The "browser" here is a socket
// talking to the real server.
//
// Set APOTHECA_DESIGN_HOLD=seconds to leave the server up after the tests,
// with a synth track ready, for looking at the page in a real browser.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "designer.hpp"
#include "song_file.hpp"
#include "synth_spec.hpp"

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

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-design-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}
bool exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
int count(const std::string& text, const std::string& part) {
    int n = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++n;
    return n;
}

struct Answer {
    int status = 0;
    std::string body;
};
Answer raw(int port, const std::string& request) {
    Answer answer;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in where = {};
    where.sin_family = AF_INET;
    where.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &where.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0) {
        close(fd);
        return answer;
    }
    send(fd, request.data(), request.size(), MSG_NOSIGNAL);
    shutdown(fd, SHUT_WR);
    std::string all;
    char block[4096];
    for (ssize_t n = recv(fd, block, sizeof block, 0); n > 0; n = recv(fd, block, sizeof block, 0)) all.append(block, static_cast<std::size_t>(n));
    close(fd);
    if (all.size() > 12) answer.status = std::atoi(all.c_str() + 9);
    const std::size_t blank = all.find("\r\n\r\n");
    if (blank != std::string::npos) answer.body = all.substr(blank + 4);
    return answer;
}

// The handheld, with its WiFi page on, and a browser's way of talking to it.
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::string root = tempFolder();
    int port = 0;
    std::string door;

    Rig() {
        Session& s = app.session();
        s.useSongFolder(root + "/songs", 3);
        s.useSampleFolder(root + "/samples");
        s.usePresetFolder(root + "/presets");
        CHECK(s.toggleTransfer(11));
        const std::string address = s.transferAddress();
        port = std::atoi(address.c_str() + address.find(':') + 1);
        door = address.substr(address.find('/'));
        frames(1);
    }
    ~Rig() {
        setUserPresets(TrackKind::Phazed, {});
        setUserPresets(TrackKind::Twin, {});
        setUserPresets(TrackKind::Qmod, {});
    }
    void frames(int n) { for (int i = 0; i < n; ++i) app.update(input, clock += 16); }
    Answer get(const std::string& path) { return raw(port, "GET " + door + path + " HTTP/1.1\r\nHost: x\r\n\r\n"); }
    Answer post(const std::string& line, const char* method = "POST") {
        return raw(port, std::string(method) + " " + door + "/design/do HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(line.size()) + "\r\n\r\n" + line);
    }
    // Ask, and let the handheld get round to it.
    bool tell(const std::string& line) {
        const bool ok = post(line).status == 200;
        frames(1);
        return ok;
    }
    void key(SDL_Keycode code) {
        for (bool down : {true, false}) {
            SDL_Event event = {};
            event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.keysym.sym = code;
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
    }
    // A synth on track 2, and the handheld looking at it.
    void addSynth(bool twin = false) {
        app.session().view = View::Song;
        app.song().moveCursorTo(0, app.project().trackCount());
        key(SDLK_x);
        if (twin) key(SDLK_DOWN);
        key(SDLK_x);
        app.project().track = app.project().trackCount() - 1;
        frames(1);
    }
    TrackData& track() { return app.project().current(); }
    // Two things about the sound that do not depend on the moment it was
    // played (a synth's wobbles never line up twice): how loud, and how high.
    float loud() {
        float p = 0.f;
        for (float s : played()) p = std::fabs(s) > p ? std::fabs(s) : p;
        return p;
    }
    int crossings() { // times the wave goes up through zero: twice as many an octave up
        const std::vector<float> out = played();
        int n = 0;
        for (std::size_t i = 2; i < out.size(); i += 2) n += out[i - 2] < 0.f && out[i] >= 0.f ? 1 : 0;
        return n;
    }
    // How much of the sound's loudness is there in its first 30 ms: little, for a slow start.
    float early() {
        const std::vector<float> out = played();
        float first = 0.f, all = 0.f;
        for (std::size_t i = 0; i < out.size(); ++i) {
            all = std::fmax(all, std::fabs(out[i]));
            if (i < 2 * 1440) first = std::fmax(first, std::fabs(out[i]));
        }
        return all > 0.f ? first / all : 1.f;
    }
    bool sameLoudness(float a, float b) { return a > 0.f && b > 0.f && a / b > 0.8f && a / b < 1.25f; }
    float peak(int frames_) {
        std::vector<float> out(2 * frames_);
        engine->render(out.data(), frames_);
        float p = 0.f;
        for (float s : out) p = std::fabs(s) > p ? std::fabs(s) : p;
        return p;
    }
    std::vector<float> played(int key_ = 60) {
        engine->render(std::vector<float>(2 * 96000).data(), 96000); // whatever was sounding has gone
        Command c;
        c.type = Command::NoteOn;
        c.track = app.project().track;
        c.a = key_;
        c.value = 0.8f;
        engine->send(c);
        std::vector<float> out(2 * 12000);
        engine->render(out.data(), 12000);
        c.type = Command::NoteOff;
        engine->send(c);
        return out;
    }
};

// ---- names and requests ---------------------------------------------------------------------

void testNamesAndRequests() {
    CHECK(presetNameFrom("my bass") == "MY BASS");
    CHECK(presetNameFrom("  Soft   Pad 2 ") == "SOFT PAD 2");
    CHECK(presetNameFrom("../../etc/passwd") == "ETCPASSWD"); // nothing that could leave the folder
    CHECK(presetNameFrom("a/b\\c.d:e*f?\"g") == "ABCDEFG");
    CHECK(presetNameFrom("lead_one-two") == "LEAD_ONE-TWO");
    CHECK(presetNameFrom(std::string(40, 'x')) == std::string(24, 'X'));
    CHECK(presetNameFrom("").empty() && presetNameFrom("   ").empty() && presetNameFrom("!!!").empty() && presetNameFrom("..").empty());

    Designer desk;
    CHECK(desk.ask("set 12 40") && desk.ask("set 3 -2") && desk.ask("macro 0 0.25") && desk.ask("preset 7"));
    CHECK(desk.ask("note 60 1") && desk.ask("note 60 0") && desk.ask("save  my bass "));
    const std::vector<DesignRequest> got = desk.take();
    CHECK(got.size() == 7 && desk.take().empty());
    CHECK(got[0].kind == DesignRequest::Set && got[0].a == 12 && got[0].b == 40 && got[1].b == -2);
    CHECK(got[2].kind == DesignRequest::Macro && got[2].a == 0 && std::fabs(got[2].value - 0.25f) < 1e-6f);
    CHECK(got[3].kind == DesignRequest::Preset && got[3].a == 7);
    CHECK(got[4].kind == DesignRequest::Note && got[4].a == 60 && got[4].b == 1 && got[5].b == 0);
    CHECK(got[6].kind == DesignRequest::Save && got[6].name == "MY BASS");
    // A macro is kept between 0 and 1.
    CHECK(desk.ask("macro 3 7") && desk.ask("macro 3 -7"));
    const std::vector<DesignRequest> clamped = desk.take();
    CHECK(clamped[0].value == 1.f && clamped[1].value == 0.f);
    // What makes no sense is refused, and nothing is left waiting.
    for (const char* bad : {"", "hello", "set", "set 1", "set x 2", "set 1 2 3", "macro 8 0.5", "macro -1 0.5", "macro 1 nan", "macro 1", "preset", "preset -1",
                            "preset 2 2", "note 128 1", "note -1 1", "note 60 2", "note 60", "save", "save !!!", "delete everything"})
        CHECK(!desk.ask(bad));
    CHECK(desk.take().empty());
    // If the program has stopped taking them, they do not pile up for ever.
    int taken = 0;
    for (int i = 0; i < 600; ++i) taken += desk.ask("note 60 1") ? 1 : 0;
    CHECK(taken == 512 && desk.take().size() == 512 && desk.ask("note 60 1"));
    // How often the page has looked.
    Designer other;
    CHECK(other.looks() == 0);
    other.state();
    other.spec();
    other.state();
    CHECK(other.looks() == 2);
    other.ask("note 60 1"); // asking counts as being there
    other.ask("rubbish");
    CHECK(other.looks() == 3);
}

// ---- the server ---------------------------------------------------------------------------------

void testThePageIsServed() {
    Rig rig;
    const Answer page = rig.get("/design");
    CHECK(page.status == 200 && has(page.body, "SOUND DESIGNER") && has(page.body, "/design/do"));
    CHECK(has(rig.get("/").body, "Open the sound designer")); // the files page leads to it
    // Not without the number from the handheld's screen.
    CHECK(raw(rig.port, "GET /design HTTP/1.1\r\nHost: x\r\n\r\n").status == 403);
    CHECK(raw(rig.port, "POST /0000/design/do HTTP/1.1\r\nHost: x\r\nContent-Length: 9\r\n\r\nnote 60 1").status == 403);
    // A drum track is being worked on: there is nothing to design, and the page is told so.
    CHECK(rig.get("/design/spec").status == 200 && has(rig.get("/design/spec").body, "\"name\":\"\""));
    CHECK(has(rig.get("/design/state").body, "\"track\":1") && has(rig.get("/design/state").body, "\"name\":\"\""));
    CHECK(rig.tell("note 60 1") && rig.tell("set 0 1") && rig.tell("preset 3") && rig.tell("save x")); // taken, and nothing happens
    CHECK(rig.peak(4000) == 0.f && !exists(rig.root + "/presets/X.phazed"));
    // What is not one short line of plain text is refused.
    CHECK(rig.post("").status == 400 && rig.post(std::string(201, 'a')).status == 400 && rig.post("set 1\n2").status == 400 && rig.post("s\xC3\xA9t 1 2").status == 400);
    CHECK(rig.post("nonsense").status == 400 && rig.post("set 1 2", "PUT").status == 200 && rig.post("set 1 2", "DELETE").status == 404);
    CHECK(rig.get("/design/do").status == 404 && rig.get("/design/other").status == 404 && rig.get("/designer").status == 400);
    // WiFi off: nobody is listening.
    rig.app.session().toggleTransfer(1);
    CHECK(rig.get("/design").status == 0);
}

// ---- the handheld does what the page asks ---------------------------------------------------------

void testPhazedFromThePage() {
    Rig rig;
    rig.addSynth();
    CHECK(rig.track().kind == TrackKind::Phazed);
    const std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"PHAZED\"") && has(spec, "\"macros\":[\"TONE\",\"MOTION\""));
    // Every page but MAIN, with its settings; envelope pages are marked.
    CHECK(count(spec, "\"env\":") == 9 && count(spec, "\"env\":true") == 6);
    CHECK(has(spec, "{\"name\":\"SOUND\",\"env\":false,\"items\":[{\"name\":\"LINES\",\"id\":0,\"min\":0,\"max\":3,\"texts\":[\"LINE 1\",\"LINE 2\",\"1 + 1'\",\"1 + 2'\"]}"));
    CHECK(count(spec, "\"id\":") == phazed::kSettings); // every setting is on some page, once
    CHECK(count(spec, "\"folder\":") == Phazed::presetCount() && !has(spec, "\"folder\":\"USER\""));
    // A setting with many values is a slider: no names for each.
    CHECK(has(spec, "{\"name\":\"DETUNE FINE\",\"id\":5,\"min\":0,\"max\":60}"));

    std::string state = rig.get("/design/state").body;
    CHECK(has(state, "\"track\":2,\"name\":\"PHAZED\"") && has(state, "\"preset\":0,") && has(state, "\"macros\":[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5]"));
    CHECK(count(state.substr(state.find("\"values\":["), state.find("]", state.find("\"values\":[")) - state.find("\"values\":[")), ",") == phazed::kSettings - 1);

    // A setting: the track has it, the state says so, and the sound is different.
    rig.tell("set " + std::to_string(phazed::Level) + " 0");
    const float before = rig.loud();
    CHECK(rig.tell("set " + std::to_string(phazed::Level) + " -20"));
    CHECK(rig.track().patch.value[phazed::Level] == -20);
    const float quiet = rig.loud();
    CHECK(quiet > 0.f && quiet < 0.2f * before); // 20 dB down is a tenth
    // Out of range is brought to the nearest end; a setting that does not exist is nothing.
    rig.tell("set " + std::to_string(phazed::DetuneFine) + " 999");
    CHECK(rig.track().patch.value[phazed::DetuneFine] == 60);
    rig.tell("set " + std::to_string(phazed::DetuneFine) + " -5");
    CHECK(rig.track().patch.value[phazed::DetuneFine] == 0);
    const PhazedPatch kept = rig.track().patch;
    rig.tell("set 9999 1");
    rig.tell("set -1 1");
    CHECK(std::equal(kept.value, kept.value + phazed::kSettings, rig.track().patch.value));
    // A macro.
    const float sharp = rig.early();
    CHECK(rig.tell("macro 2 1")); // ATTACK, all the way up: a slow start
    CHECK(rig.track().macro[2] == 1.f && rig.early() < 0.5f * sharp);
    rig.tell("macro 0 1");
    CHECK(has(rig.get("/design/state").body, "\"macros\":[1,0.5,1,0.5,0.5,0.5,0.5,0.5]"));
    // A preset: its settings, and the macros back in the middle.
    CHECK(rig.tell("preset 5"));
    const PhazedPatch five = Phazed::presetPatch(5);
    CHECK(rig.track().preset == 5 && rig.track().macro[0] == 0.5f && std::equal(five.value, five.value + phazed::kSettings, rig.track().patch.value));
    CHECK(has(rig.get("/design/state").body, "\"preset\":5,"));
    rig.tell("preset 99999"); // there is no such preset
    CHECK(rig.track().preset == 5);
    // The handheld's own screen and the page are the same sound: what is turned there shows here.
    rig.app.session().setSynthSetting(1, phazed::VibratoDepth, 77);
    rig.frames(1);
    state = rig.get("/design/state").body;
    CHECK(rig.track().patch.value[phazed::VibratoDepth] == 77 && has(state, ",77,"));
}

void testKeysFromThePage() {
    Rig rig;
    rig.addSynth();
    rig.tell("preset 0");
    rig.peak(96000);
    CHECK(rig.peak(4000) == 0.f);
    // Down: it sounds, and goes on sounding. Up: it stops.
    CHECK(rig.tell("note 60 1"));
    CHECK(rig.peak(8000) > 0.02f);
    rig.get("/design/state"); // the page is still there
    rig.frames(1);
    CHECK(rig.peak(8000) > 0.02f);
    rig.tell("note 60 1"); // said twice: still one key
    CHECK(rig.tell("note 60 0"));
    rig.peak(96000);
    CHECK(rig.peak(8000) < 0.002f);
    // The page goes away with a key down (the tab was closed): three seconds on, the key is let go.
    rig.tell("note 64 1");
    rig.get("/design/state");
    rig.frames(1);
    CHECK(rig.peak(8000) > 0.02f);
    rig.frames(170); // 2.7 s without a word from the page
    CHECK(rig.peak(8000) > 0.02f);
    rig.frames(30);
    rig.peak(96000);
    CHECK(rig.peak(8000) < 0.002f);
    // While the page keeps looking, the key stays down however long.
    rig.tell("note 67 1");
    for (int i = 0; i < 10; ++i) {
        rig.get("/design/state");
        rig.frames(60);
    }
    CHECK(rig.peak(8000) > 0.02f);
    // WiFi turned off with a key down: let go.
    rig.app.session().toggleTransfer(1);
    rig.frames(1);
    rig.peak(96000);
    CHECK(rig.peak(8000) < 0.002f);
}

void testSavingAPreset() {
    Rig rig;
    rig.addSynth();
    Session& session = rig.app.session();
    const int builtIn = Phazed::presetCount();
    // Make a sound: a preset, bent with two settings and a macro.
    rig.tell("preset 3");
    // Quiet (a setting) and slow to start (a macro), so that both can be heard to come back.
    rig.tell("preset 0");
    rig.tell("set " + std::to_string(phazed::Level) + " 0");
    const float full = rig.loud(), sharp = rig.early();
    rig.tell("set " + std::to_string(phazed::Level) + " -20");
    rig.tell("set " + std::to_string(phazed::VibratoRate) + " 70");
    rig.tell("macro 0 0.8");
    rig.tell("macro 2 0.7"); // ATTACK, most of the way up
    const PhazedPatch made = rig.track().patch;
    rig.loud();
    const float sound = rig.loud(), start = rig.early();
    CHECK(sound < 0.3f * full && start < 0.5f * sharp);
    const auto asMade = [&] {
        const float l = rig.loud(), e = rig.early();
        return rig.sameLoudness(l, sound) && e < 0.5f * sharp;
    };
    CHECK(rig.tell("save my bass!"));
    CHECK(exists(rig.root + "/presets/MY BASS.phazed"));
    // It is in the list, in the USER folder, after the built-in ones; the track is on it, unchanged.
    const SynthSpec& spec = phazedSpec();
    CHECK(spec.presetCount() == builtIn + 1 && std::string(spec.presetName(builtIn)) == "MY BASS" && std::string(spec.presetCategory(builtIn)) == "USER");
    CHECK(rig.track().preset == builtIn && std::equal(made.value, made.value + phazed::kSettings, rig.track().patch.value) && rig.track().macro[0] == 0.8f && std::fabs(rig.track().macro[2] - 0.7f) < 1e-5f);
    CHECK(asMade());
    const std::string page = rig.get("/design/spec").body;
    CHECK(has(page, "{\"name\":\"MY BASS\",\"folder\":\"USER\"}") && has(rig.get("/design/state").body, "\"presets\":" + std::to_string(builtIn + 1)));
    // Away to another sound and back: every setting and the macro return, and it sounds the same.
    rig.tell("preset 10");
    rig.tell("set " + std::to_string(phazed::Level) + " 0");
    CHECK(!asMade() && rig.track().macro[0] == 0.5f && rig.track().macro[2] == 0.5f);
    CHECK(rig.tell("preset " + std::to_string(builtIn)));
    CHECK(rig.track().preset == builtIn && std::equal(made.value, made.value + phazed::kSettings, rig.track().patch.value) && std::fabs(rig.track().macro[0] - 0.8f) < 1e-5f && std::fabs(rig.track().macro[2] - 0.7f) < 1e-5f);
    CHECK(asMade());
    // The handheld's own preset list reaches it too: one back from the first preset is the last, the player's.
    rig.app.session().view = View::Device;
    rig.app.phazed().choosePreset(0);
    CHECK(!asMade());
    rig.app.phazed().choosePreset(-1);
    CHECK(rig.track().preset == builtIn && asMade());
    // A second one sorts by name; saving under the same name again replaces, it does not add.
    rig.tell("preset 20");
    rig.tell("save a pad");
    CHECK(spec.presetCount() == builtIn + 2 && std::string(spec.presetName(builtIn)) == "A PAD" && std::string(spec.presetName(builtIn + 1)) == "MY BASS");
    CHECK(rig.track().preset == builtIn);
    rig.tell("set " + std::to_string(phazed::VibratoDepth) + " 5");
    rig.tell("save A PAD");
    CHECK(spec.presetCount() == builtIn + 2 && userPresets(TrackKind::Phazed)[0].values[phazed::VibratoDepth] == 5);
    // A name with nothing usable in it saves nothing.
    CHECK(!session.savePreset(1, "???") && !session.savePreset(0, "DRUMS") && !session.savePreset(99, "X"));
    CHECK(spec.presetCount() == builtIn + 2);

    // Another start of the program finds them. A file that is not a preset is left out;
    // values out of range in one are brought in when it is used.
    writeTextFile(rig.root + "/presets/BROKEN.phazed", "this is not a preset\n");
    writeTextFile(rig.root + "/presets/WILD.phazed", "apotheca-preset 1\nsynth phazed\nmacros 9 -9\nsettings 99 99 99\n");
    writeTextFile(rig.root + "/presets/notes.txt", "apotheca-preset 1\nsettings 1 2 3\n");
    writeTextFile(rig.root + "/presets/HALF.phazed", "apotheca-preset 1\nsynth phazed\nmacros 0.2 0.3\n"); // cut short: no settings, so not a preset
    setUserPresets(TrackKind::Phazed, {});
    CHECK(spec.presetCount() == builtIn);
    session.usePresetFolder(rig.root + "/presets");
    CHECK(spec.presetCount() == builtIn + 3 && std::string(spec.presetName(builtIn + 2)) == "WILD");
    CHECK(userPresets(TrackKind::Twin).empty());
    rig.tell("preset " + std::to_string(builtIn + 2));
    CHECK(rig.track().patch.value[phazed::LineSelect] == 3 && rig.track().patch.value[phazed::Octave] == 1 && rig.track().patch.value[phazed::DetuneSign] == 1);
    CHECK(rig.track().macro[0] == 1.f && rig.track().macro[1] == 0.f && rig.track().macro[2] == 0.5f);
    CHECK(rig.peak(100) >= 0.f); // and the engine is still standing

    // A song remembers the sound itself, not only the preset's number: saved and opened again, it is the same.
    rig.tell("preset " + std::to_string(builtIn + 1));
    CHECK(asMade());
    session.saveNow();
    const std::string name = session.songName();
    session.newSong();
    CHECK(session.openSong(name));
    rig.app.project().track = 1;
    rig.frames(2);
    CHECK(rig.app.project().current().kind == TrackKind::Phazed && asMade());
}

void testQmodFromThePage() {
    Rig rig;
    // A QMOD track, added as the SONG screen adds one.
    rig.app.session().addTrack(TrackKind::Qmod);
    rig.app.project().track = rig.app.project().trackCount() - 1;
    rig.frames(1);
    CHECK(rig.track().kind == TrackKind::Qmod);
    const std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"QMOD\",\"roll\":true") && has(spec, "\"macros\":[\"ENTANGLE\",\"TENSION\"") && count(spec, "\"id\":") == qmod::kSettings);
    CHECK(has(spec, "{\"name\":\"SOUND NUMBER\",\"id\":0,\"min\":1,\"max\":9999}")); // a slider through every sound there is
    // The page's ROLL button: a new sound; twice, two different ones.
    const int before = rig.track().qmod.value[qmod::Sound];
    CHECK(rig.tell("roll 12345"));
    const int once = rig.track().qmod.value[qmod::Sound];
    CHECK(rig.tell("roll 999"));
    CHECK(once != before && rig.track().qmod.value[qmod::Sound] != once);
    CHECK(rig.post("roll").status == 400 && rig.post("roll x").status == 400);
    // Saved, it is a ".qmod" preset, in QMOD's list only, and comes back as it was.
    const QmodPatch made = rig.track().qmod;
    CHECK(rig.tell("save rolled one"));
    CHECK(exists(rig.root + "/presets/ROLLED ONE.qmod") && qmodSpec().presetCount() == Qmod::presetCount() + 1 && phazedSpec().presetCount() == Phazed::presetCount());
    rig.tell("preset 2");
    CHECK(rig.track().qmod.value[qmod::Sound] != made.value[qmod::Sound]);
    rig.tell("preset " + std::to_string(Qmod::presetCount()));
    CHECK(std::equal(made.value, made.value + qmod::kSettings, rig.track().qmod.value));
    // On a synth that is not rolled the request is taken and does nothing; the page is not offered the button.
    rig.addSynth();
    CHECK(!has(rig.get("/design/spec").body, "\"roll\""));
    const PhazedPatch phazed = rig.track().patch;
    rig.tell("roll 5");
    CHECK(std::equal(phazed.value, phazed.value + phazed::kSettings, rig.track().patch.value));
}

void testTwinFromThePage() {
    Rig rig;
    rig.addSynth(true);
    CHECK(rig.track().kind == TrackKind::Twin);
    const std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"TWIN\"") && has(spec, "\"macros\":[\"SHAPE\",\"BLEND\"") && count(spec, "\"env\":") == 3 && !has(spec, "\"env\":true"));
    CHECK(count(spec, "\"id\":") == twin::kSettings);
    rig.tell("set " + std::to_string(twin::Level) + " 0");
    const float before = rig.loud();
    CHECK(rig.tell("set " + std::to_string(twin::VShape) + " 100") && rig.track().twin.value[twin::VShape] == 100);
    rig.tell("set " + std::to_string(twin::Level) + " -20");
    CHECK(rig.loud() < 0.2f * before);
    rig.tell("set " + std::to_string(twin::VShape) + " 500");
    CHECK(rig.track().twin.value[twin::VShape] == 100);
    const TwinPatch made = rig.track().twin;
    const float sound = rig.loud();
    CHECK(rig.tell("save wide saw"));
    CHECK(exists(rig.root + "/presets/WIDE SAW.twin") && !exists(rig.root + "/presets/WIDE SAW.phazed"));
    CHECK(twinSpec().presetCount() == Twin::presetCount() + 1 && phazedSpec().presetCount() == Phazed::presetCount()); // Twin's list, not Phazed's
    rig.tell("preset 2");
    rig.tell("set " + std::to_string(twin::Level) + " 0");
    CHECK(rig.loud() > 3.f * sound);
    rig.tell("preset " + std::to_string(Twin::presetCount()));
    CHECK(std::equal(made.value, made.value + twin::kSettings, rig.track().twin.value) && rig.sameLoudness(rig.loud(), sound));
    // The page follows the handheld to another track: a different synth, a different page.
    rig.app.project().track = 0;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"name\":\"\""));
    rig.app.project().track = 1;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"name\":\"TWIN\""));
}

} // namespace

int main() {
    testNamesAndRequests();
    testThePageIsServed();
    testPhazedFromThePage();
    testKeysFromThePage();
    testSavingAPreset();
    testQmodFromThePage();
    testTwinFromThePage();
    if (const char* hold = std::getenv("APOTHECA_DESIGN_HOLD")) {
        Rig rig;
        rig.addSynth(std::getenv("APOTHECA_DESIGN_TWIN") != nullptr);
        rig.tell("preset 8");
        std::printf("open http://127.0.0.1:%d%s/design\n", rig.port, rig.door.c_str());
        std::fflush(stdout);
        for (int i = 0; i < std::atoi(hold) * 60; ++i) {
            rig.frames(1);
            rig.peak(800);
            usleep(16000);
        }
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("design tests passed\n");
    return 0;
}
