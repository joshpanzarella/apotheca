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
#include <sstream>
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
        setUserPresets(TrackKind::Jx3p, {});
        setUserPresets(TrackKind::Fm6, {});
        setUserPresets(TrackKind::Physic, {});
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
    void addSynth(bool jaxep = false) {
        app.session().view = View::Song;
        app.song().moveCursorTo(0, app.project().trackCount());
        key(SDLK_x);
        if (jaxep) key(SDLK_DOWN);
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
    CHECK(userPresets(TrackKind::Jx3p).empty());
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

void testFm6FromThePage() {
    Rig rig;
    // An FM-6 track, added as the SONG screen adds one.
    rig.app.session().addTrack(TrackKind::Fm6);
    rig.app.project().track = rig.app.project().trackCount() - 1;
    rig.frames(1);
    CHECK(rig.track().kind == TrackKind::Fm6);
    const std::string spec = rig.get("/design/spec").body;
    // Its 145 settings, and its six ONs (OPERATORS ON is never offered as a number); no ROLL any more.
    CHECK(has(spec, "\"name\":\"FM-6\",\"macros\":[\"BRIGHT\",\"FEEDBACK\"") && count(spec, "\"id\":") == fm6::kSettings + 5 && !has(spec, "\"roll\""));
    CHECK(has(spec, "{\"name\":\"ALGORITHM\",\"id\":134,\"min\":0,\"max\":31,\"texts\":[\"1\",\"2\""));
    CHECK(has(spec, "{\"name\":\"ON\",\"id\":146,\"min\":0,\"max\":1,\"texts\":[\"OFF\",\"ON\"]}") && has(spec, "\"id\":151,") && !has(spec, "\"id\":145,"));
    CHECK(has(spec, "\"name\":\"OPERATOR 6\"") && has(spec, "\"name\":\"PITCH ENVELOPE\""));
    // The state has a value for every id the page shows, the ONs read from their bits.
    const std::string state = rig.get("/design/state").body;
    const std::size_t from = state.find("\"values\":[");
    CHECK(from != std::string::npos);
    const std::string values = state.substr(from, state.find(']', from) - from);
    CHECK(count(values, ",") == fm6::kSettings + 5 && has(values, ",63,1,1,1,1,1,1"));
    // An ON set from the page is its bit: OPERATOR 3 off.
    CHECK(rig.tell("set 148 0"));
    CHECK(rig.track().fm6.value[fm6::OpsOn] == 63 - 8);
    CHECK(rig.tell("set 151 0") && rig.track().fm6.value[fm6::OpsOn] == 63 - 8 - 1);
    CHECK(rig.tell("set 152 0") && rig.track().fm6.value[fm6::OpsOn] == 63 - 8 - 1); // past the six: nothing
    CHECK(has(rig.get("/design/state").body, ",54,1,1,0,1,1,0"));
    CHECK(rig.tell("set 134 4") && rig.track().fm6.value[fm6::Algorithm] == 4);
    // ROLL is PHYSIC's: on FM-6 the request is taken and changes nothing.
    const Fm6Patch beforeRoll = rig.track().fm6;
    rig.tell("roll 5");
    CHECK(std::equal(beforeRoll.value, beforeRoll.value + fm6::kSettings, rig.track().fm6.value));
    // Saved, it is an ".fm6" preset, in FM-6's list only, and comes back as it was.
    const Fm6Patch made = rig.track().fm6;
    CHECK(rig.tell("save made one"));
    CHECK(exists(rig.root + "/presets/MADE ONE.fm6") && fm6Spec().presetCount() == Fm6::presetCount() + 1 && phazedSpec().presetCount() == Phazed::presetCount());
    rig.tell("preset 2");
    CHECK(rig.track().fm6.value[fm6::Algorithm] != made.value[fm6::Algorithm] && rig.track().fm6.value[fm6::OpsOn] == 63);
    rig.tell("preset " + std::to_string(Fm6::presetCount()));
    CHECK(std::equal(made.value, made.value + fm6::kSettings, rig.track().fm6.value));
}

// PHYSIC (physic-plan.md, section 7): its four pages, every setting read as the device reads it alone,
// and its ROLL, which is the device's Y.
void testPhysicFromThePage() {
    Rig rig;
    rig.app.session().addTrack(TrackKind::Physic);
    rig.app.project().track = rig.app.project().trackCount() - 1;
    rig.frames(1);
    CHECK(rig.track().kind == TrackKind::Physic && rig.track().preset == 0);
    const std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"PHYSIC\",\"roll\":true,\"macros\":[\"TONE\",\"POSITION\",\"DECAY\",\"MATERIAL\",\"STRETCH\",\"RELEASE\",\"DICE\",\"OCTAVE\"]"));
    CHECK(count(spec, "\"id\":") == physic::kSettings && count(spec, "\"env\":false") == 4 && !has(spec, "\"env\":true"));
    CHECK(has(spec, "\"name\":\"SOUND\"") && has(spec, "\"name\":\"RING\"") && has(spec, "\"name\":\"PLAY\"") && has(spec, "\"name\":\"DICE\""));
    // Every setting is given its readings, the long ones too (pageReadsAsDevice): DECAY in seconds.
    CHECK(count(spec, "\"texts\":") == physic::kSettings);
    CHECK(has(spec, "{\"name\":\"MODEL\",\"id\":0,\"min\":0,\"max\":10,\"texts\":[\"STRING\",\"MARIMBA\""));
    CHECK(has(spec, "\"name\":\"DECAY\",\"id\":4,\"min\":0,\"max\":100,\"texts\":[\"50 MS\"") && has(spec, "\"30 S\"]"));
    CHECK(has(spec, "\"RING\"]") && has(spec, "\"name\":\"LEVEL\",\"id\":12,\"min\":-24,\"max\":6,\"texts\":[\"-24 DB\""));
    CHECK(has(spec, "\"folder\":\"MALLETS\"") && has(spec, "\"folder\":\"WOOD AND SKIN\"") && has(spec, "\"name\":\"Marimba\""));
    // The state: one value an id, MARIMBA's.
    const std::string state = rig.get("/design/state").body;
    const std::size_t from = state.find("\"values\":[");
    CHECK(from != std::string::npos);
    const std::string values = state.substr(from, state.find(']', from) - from);
    CHECK(count(values, ",") == physic::kSettings - 1 && has(values, "[1,1,"));
    // A setting from the page.
    CHECK(rig.tell("set 4 90") && rig.track().physic.value[physic::Decay] == 90);
    CHECK(rig.tell("set 0 99") && rig.track().physic.value[physic::Model] == 10); // held to its range
    rig.tell("set 0 1");
    // ROLL: a new strike and ring for the same MODEL, the levels and the dice as they were.
    const PhysicPatch before = rig.track().physic;
    CHECK(rig.tell("roll 5"));
    const PhysicPatch& after = rig.track().physic;
    bool moved = false;
    for (int id : {physic::Strike, physic::Hardness, physic::Position, physic::Decay, physic::Material, physic::Stretch}) moved = moved || after.value[id] != before.value[id];
    CHECK(moved);
    for (int id : {physic::Model, physic::Release, physic::Velocity, physic::Octave, physic::Fine, physic::Width, physic::Level, physic::Dice,
                   physic::DicePosition, physic::DiceHardness, physic::DiceTune, physic::DiceDecay})
        CHECK(after.value[id] == before.value[id]);
    // Saved, it is a ".physic" preset, in PHYSIC's list only, and comes back as it was.
    const PhysicPatch made = rig.track().physic;
    CHECK(rig.tell("save made one"));
    CHECK(exists(rig.root + "/presets/MADE ONE.physic") && physicSpec().presetCount() == Physic::presetCount() + 1 && fm6Spec().presetCount() == Fm6::presetCount());
    rig.tell("preset 3");
    rig.tell("preset " + std::to_string(Physic::presetCount()));
    CHECK(std::equal(made.value, made.value + physic::kSettings, rig.track().physic.value));
    // FM-6's page has no ROLL.
    rig.app.session().addTrack(TrackKind::Fm6);
    rig.app.project().track = rig.app.project().trackCount() - 1;
    rig.frames(1);
    CHECK(!has(rig.get("/design/spec").body, "\"roll\""));
}

// JAX-EP (jx3p-plan.md, batch 6a).
void testJaxepFromThePage() {
    Rig rig;
    rig.addSynth(true);
    CHECK(rig.track().kind == TrackKind::Jx3p);
    const std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"JAX-EP\"") && has(spec, "\"macros\":[\"CUTOFF\",\"RESO\"") && count(spec, "\"env\":") == 5 && !has(spec, "\"env\":true"));
    CHECK(count(spec, "\"id\":") == jx3p::kSettings);
    CHECK(has(spec, "\"texts\":[\"OFF\",\"SYNC\",\"METAL\"]") && has(spec, "\"texts\":[\"SAW\",\"PULSE\",\"SQUARE\",\"NOISE\"]"));
    // A knob reads on the page as on the device, its dial in tenths, 0.0 to 10.0 (batch 6b): the page is given
    // all 101 readings of each of the 19 knobs, and shows the one the slider is at.
    CHECK(count(spec, "\"texts\":") == jx3p::kSettings && count(spec, "\"texts\":[\"0.0\",\"0.1\",\"0.2\",") == 19 && count(spec, ",\"9.8\",\"9.9\",\"10.0\"]") == 19);
    CHECK(has(spec, "{\"name\":\"CUTOFF\",\"id\":16,\"min\":0,\"max\":100,\"texts\":[\"0.0\",") && !has(spec, "\"100\"") && !has(spec, "\"10.1\""));
    {
        // ...and only JAX-EP's: Phazed's long settings still show their plain number there.
        Rig other;
        other.addSynth();
        const std::string phazed = other.get("/design/spec").body;
        CHECK(has(phazed, "\"name\":\"PHAZED\"") && count(phazed, "\"texts\":") > 0 && count(phazed, "\"texts\":") < count(phazed, "\"id\":"));
    }
    // A new track is on A1 String I (batch 7). Made plain to listen to: one DCO (its two saws beat, and
    // a note's peak then hangs on where in the beat it began), no slow attack, the chorus off.
    rig.tell("set " + std::to_string(jx3p::Chorus) + " 0"); // (off: its sweep moves a note's peak from one note to the next)
    rig.tell("set " + std::to_string(jx3p::SourceMix) + " 0");
    rig.tell("set " + std::to_string(jx3p::Attack) + " 0");
    rig.tell("set " + std::to_string(jx3p::VcaLevel) + " 50"); // (half the VCA's 0 dB: nowhere near the clip)
    const float before = rig.loud();
    CHECK(rig.tell("set " + std::to_string(jx3p::Resonance) + " 30") && rig.track().jx3p.value[jx3p::Resonance] == 30);
    rig.tell("set " + std::to_string(jx3p::VcaLevel) + " 5"); // a tenth of the dial: 20 dB down
    CHECK(rig.loud() < 0.2f * before);
    rig.tell("set " + std::to_string(jx3p::Resonance) + " 500");
    CHECK(rig.track().jx3p.value[jx3p::Resonance] == 100);
    rig.tell("set " + std::to_string(jx3p::Resonance) + " 30");
    // The track's own two, turned before the save: a preset does not hold them.
    rig.tell("set " + std::to_string(jx3p::Brilliance) + " 61");
    rig.tell("set " + std::to_string(jx3p::Velocity) + " 0");
    const Jx3pPatch made = rig.track().jx3p;
    const float sound = rig.loud();
    CHECK(rig.tell("save wide saw"));
    CHECK(exists(rig.root + "/presets/WIDE SAW.jaxep") && !exists(rig.root + "/presets/WIDE SAW.phazed") && !exists(rig.root + "/presets/WIDE SAW.twin"));
    std::string file;
    CHECK(readTextFile(rig.root + "/presets/WIDE SAW.jaxep", file));
    {
        std::istringstream in(file.substr(file.find("settings") + 8));
        int v = 0, n = 0;
        while (in >> v) ++n;
        CHECK(n == jx3p::kPatchSettings); // ids 0 to 31
    }
    CHECK(jx3pSpec().presetCount() == Jx3p::presetCount() + 1 && phazedSpec().presetCount() == Phazed::presetCount()); // its own list, not Phazed's
    rig.tell("set " + std::to_string(jx3p::Brilliance) + " 39");
    rig.tell("preset 0"); // the first built-in one
    CHECK(rig.track().preset == 0 && rig.track().jx3p.value[jx3p::VcaLevel] == Jx3p::presetPatch(0).value[jx3p::VcaLevel]);
    rig.tell("preset 7"); // A8 E. Piano I: LEVEL 9.9, no attack (the made sound is at LEVEL 0.5)
    CHECK(rig.track().preset == 7 && rig.track().jx3p.value[jx3p::VcaLevel] == 99 && rig.loud() > 3.f * sound);
    rig.tell("preset " + std::to_string(Jx3p::presetCount()));
    CHECK(std::equal(made.value, made.value + jx3p::kPatchSettings, rig.track().jx3p.value));
    CHECK(rig.track().jx3p.value[jx3p::Brilliance] == 39 && rig.track().jx3p.value[jx3p::Velocity] == 0); // as the track had them, not as they were at the save
    // ...and it sounds as it did, with BRILLIANCE where it was when it was listened to (it moves A1's
    // filter, and a saw's peak with it).
    rig.tell("set " + std::to_string(jx3p::Brilliance) + " 61");
    CHECK(rig.sameLoudness(rig.loud(), sound));
    rig.tell("set " + std::to_string(jx3p::Brilliance) + " 39");
    // The page follows the handheld to another track: a different synth, a different page.
    rig.app.project().track = 0;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"name\":\"\""));
    rig.app.project().track = 1;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"name\":\"JAX-EP\""));
}

} // namespace

int main() {
    testNamesAndRequests();
    testThePageIsServed();
    testPhazedFromThePage();
    testKeysFromThePage();
    testSavingAPreset();
    testFm6FromThePage();
    testPhysicFromThePage();
    testJaxepFromThePage();
    if (const char* hold = std::getenv("APOTHECA_DESIGN_HOLD")) {
        Rig rig;
        if (std::getenv("APOTHECA_DESIGN_PHYSIC")) { // (a PHYSIC track, on its first preset)
            rig.app.session().addTrack(TrackKind::Physic);
            rig.app.project().track = rig.app.project().trackCount() - 1;
            rig.frames(1);
        } else {
            rig.addSynth(std::getenv("APOTHECA_DESIGN_JAXEP") != nullptr);
            rig.tell("preset 8");
        }
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
