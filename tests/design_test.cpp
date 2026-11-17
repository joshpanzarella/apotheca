// Tests for the sound designer: the page's requests, the presets it saves,
// and the handheld doing what the page asks. The "browser" here is a socket
// talking to the real server.
//
// Set APOTHECA_DESIGN_HOLD=seconds to leave the server up after the tests,
// with a synth track ready, for looking at the page in a real browser
// (APOTHECA_DESIGN_SAMPLER=1, _PHYSIC, _JAXEP or _KIT for another kind of track;
// a KIT's lanes are printed as the handheld has them whenever one changes).

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
#include "presets.hpp"
#include "app.hpp"
#include "designer.hpp"
#include "samples.hpp"
#include "apotheca/sampler.hpp"
#include "song_file.hpp"
#include "synth_spec.hpp"
#include "temp_folder.hpp"

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
    std::string root = tempFolder("design");
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
    // A sampler's recording: the whole name after the one space, folders and spaces and all; none; and
    // nothing that could leave the samples folder.
    CHECK(desk.ask("sample drums/kicks/big 808") && desk.ask("sample") && desk.ask("sample  two spaces"));
    const std::vector<DesignRequest> samples = desk.take();
    CHECK(samples.size() == 3 && samples[0].kind == DesignRequest::Sample && samples[0].name == "drums/kicks/big 808" && samples[1].name.empty() && samples[2].name == " two spaces");
    for (const char* bad : {"sample ../songs/a", "sample a/../../b", "sample /abs", "sample a//b", "sample .hidden", "sample a\\b", "sample a/b/c/d", "samples x"})
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
    // A drum track is being worked on: its KIT's page (testKitFromThePage). A synth's asks do nothing to it.
    CHECK(rig.get("/design/spec").status == 200 && has(rig.get("/design/spec").body, "\"page\":\"kit\""));
    CHECK(has(rig.get("/design/state").body, "\"track\":1") && has(rig.get("/design/state").body, "\"name\":\"KIT\""));
    CHECK(rig.tell("note 60 1") && rig.tell("macro 0 0.9") && rig.tell("preset 3") && rig.tell("save x") && rig.tell("roll 2")); // taken, and nothing happens
    CHECK(rig.peak(4000) == 0.f && !exists(rig.root + "/presets/X.phazed") && !exists(rig.root + "/presets/X"));
    // A track with nothing on it: there is nothing to design, and the page is told so.
    rig.app.project().track = rig.app.project().trackCount();
    rig.frames(1);
    CHECK(rig.get("/design/spec").status == 200 && has(rig.get("/design/spec").body, "\"name\":\"\""));
    CHECK(has(rig.get("/design/state").body, "\"name\":\"\""));
    CHECK(rig.tell("note 60 1") && rig.tell("set 0 1") && rig.tell("hear 0 1") && rig.tell("preset 3") && rig.tell("save x")); // taken, and nothing happens
    CHECK(rig.peak(4000) == 0.f && !exists(rig.root + "/presets/X.phazed"));
    rig.app.project().track = 0;
    rig.frames(1);
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
    CHECK(count(spec, "\"folder\":") == builtInPresets(TrackKind::Phazed) && !has(spec, "\"folder\":\"USER\""));
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
    const int builtIn = builtInPresets(TrackKind::Phazed);
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
    CHECK(exists(rig.root + "/presets/MADE ONE.fm6") && fm6Spec().presetCount() == builtInPresets(TrackKind::Fm6) + 1 && phazedSpec().presetCount() == builtInPresets(TrackKind::Phazed));
    rig.tell("preset 2");
    CHECK(rig.track().fm6.value[fm6::Algorithm] != made.value[fm6::Algorithm] && rig.track().fm6.value[fm6::OpsOn] == 63);
    rig.tell("preset " + std::to_string(builtInPresets(TrackKind::Fm6)));
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
    CHECK(count(spec, "\"id\":") == physic::kSettings && count(spec, "\"env\":false") == 5 && !has(spec, "\"env\":true"));
    CHECK(has(spec, "\"name\":\"SOUND\"") && has(spec, "\"name\":\"RING\"") && has(spec, "\"name\":\"PLAY\"") && has(spec, "\"name\":\"DICE\"") && has(spec, "\"name\":\"BODY\",\"env\""));
    CHECK(has(spec, "{\"name\":\"STRIKE\",\"id\":1,\"min\":0,\"max\":2,\"texts\":[\"PLUCK\",\"MALLET\",\"NOISE\"]"));
    CHECK(has(spec, "{\"name\":\"PAIR\",\"id\":18,\"min\":0,\"max\":20,\"texts\":[\"OFF\",\"1 CENTS\"") && has(spec, "{\"name\":\"BODY\",\"id\":20,\"min\":0,\"max\":3,\"texts\":[\"NONE\",\"GUITAR\",\"BOX\",\"TUBES\"]"));
    CHECK(has(spec, "\"name\":\"MOTOR\",\"id\":19,\"min\":0,\"max\":100,\"texts\":[\"OFF\",\"0.20 HZ\"") && has(spec, "\"8.0 HZ\"]") && has(spec, "\"name\":\"BUZZ\",\"id\":22"));
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
                   physic::DicePosition, physic::DiceHardness, physic::DiceTune, physic::DiceDecay, physic::Body})
        CHECK(after.value[id] == before.value[id]);
    // Saved, it is a ".physic" preset, in PHYSIC's list only, and comes back as it was.
    const PhysicPatch made = rig.track().physic;
    CHECK(rig.tell("save made one"));
    CHECK(exists(rig.root + "/presets/MADE ONE.physic") && physicSpec().presetCount() == builtInPresets(TrackKind::Physic) + 1 && fm6Spec().presetCount() == builtInPresets(TrackKind::Fm6));
    rig.tell("preset 3");
    rig.tell("preset " + std::to_string(builtInPresets(TrackKind::Physic)));
    CHECK(std::equal(made.value, made.value + physic::kSettings, rig.track().physic.value));
    // FM-6's page has no ROLL.
    rig.app.session().addTrack(TrackKind::Fm6);
    rig.app.project().track = rig.app.project().trackCount() - 1;
    rig.frames(1);
    CHECK(!has(rig.get("/design/spec").body, "\"roll\""));
}

// The SAMPLER: its settings with the screen's readings, its recording as a picture made on the app
// thread, START and END as marks, the recording picked from the page, and its key.
namespace {
// A second of a 440 Hz sine, quiet for its first half and loud for its second: where a note starts
// in it is heard in how loud its first quarter second is, and its pitch in how often it crosses zero.
std::vector<float> steppedSine() {
    std::vector<float> out(48000);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = (i < 24000 ? 0.2f : 0.8f) * std::sin(6.2831853f * 440.f * static_cast<float>(i) / 48000.f);
    return out;
}
std::string valuesOf(const std::string& state) {
    const std::size_t from = state.find("\"values\":[");
    return from == std::string::npos ? "" : state.substr(from, state.find(']', from) + 1 - from);
}
int specNumber(const std::string& state) {
    const std::size_t at = state.find("\"spec\":");
    return at == std::string::npos ? -1 : std::atoi(state.c_str() + at + 7);
}
// The picture's points, as the page is given them.
std::vector<int> peaksOf(const std::string& spec) {
    std::vector<int> out;
    const std::size_t from = spec.find("\"peaks\":[");
    if (from == std::string::npos) return out;
    std::istringstream in(spec.substr(from + 9, spec.find(']', from) - from - 9));
    std::string one;
    while (std::getline(in, one, ',')) out.push_back(std::atoi(one.c_str()));
    return out;
}
// A WAV of 32-bit floats, one channel at 48 kHz (writeWav makes 16-bit ones, which cannot pass full).
std::string floatWav(const std::vector<float>& samples) {
    std::string out;
    const auto put = [&](std::uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) out += static_cast<char>((v >> (8 * i)) & 255); };
    const std::uint32_t data = static_cast<std::uint32_t>(samples.size() * 4);
    out += "RIFF"; put(36 + data, 4); out += "WAVEfmt "; put(16, 4); put(3, 2); put(1, 2); put(48000, 4); put(48000 * 4, 4); put(4, 2); put(32, 2);
    out += "data"; put(data, 4);
    for (float f : samples) { std::uint32_t v; std::memcpy(&v, &f, 4); put(v, 4); }
    return out;
}
} // namespace

void testSamplerFromThePage() {
    Rig rig;
    Session& session = rig.app.session();
    mkdir((rig.root + "/samples").c_str(), 0755);
    mkdir((rig.root + "/samples/drums").c_str(), 0755);
    CHECK(writeWav(rig.root + "/samples/stepped.wav", steppedSine(), 48000));
    CHECK(writeWav(rig.root + "/samples/drums/kick.wav", std::vector<float>(4800, 0.5f), 48000));
    CHECK(session.addTrack(TrackKind::Sampler));
    const int track = rig.app.project().trackCount() - 1;
    rig.app.project().track = track;
    rig.frames(1);
    CHECK(rig.track().kind == TrackKind::Sampler);

    // Until the page looks, the card is not read for it: no recordings offered.
    std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"SAMPLER\",\"macros\":[],") && has(spec, "\"page\":\"sampler\"") && has(spec, "\"recordings\":[]"));
    rig.get("/design/state");
    rig.frames(1);
    spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"recordings\":[\"drums/kick\",\"stepped\"]") || has(spec, "\"recordings\":[\"stepped\",\"drums/kick\"]"));
    // Its eight settings, in the order its screen shows them, each with every reading the screen gives.
    CHECK(count(spec, "\"id\":") == sampler::kSettings && count(spec, "\"texts\":") == sampler::kSettings);
    const char* order[] = {"PITCH", "TUNE", "START", "END", "ATTACK", "RELEASE", "LOOP", "LEVEL"};
    std::size_t at = 0;
    for (const char* name : order) {
        const std::size_t next = spec.find(std::string("{\"name\":\"") + name + "\"", at);
        CHECK(next != std::string::npos);
        at = next == std::string::npos ? at : next;
    }
    CHECK(has(spec, "{\"name\":\"PITCH\",\"id\":0,\"min\":0,\"max\":127,\"flip\":true,\"texts\":[\"+60\",\"+59\"") && count(spec, "\"flip\"") == 1);
    CHECK(has(spec, "{\"name\":\"END\",\"id\":7,\"min\":1,\"max\":100,\"texts\":[\"1%\",") && has(spec, "{\"name\":\"LOOP\",\"id\":5,\"min\":0,\"max\":1,\"texts\":[\"OFF\",\"ON\"]}"));
    CHECK(has(spec, "\"+5 CENTS\"") && has(spec, "\"8.0 S\"]") && has(spec, "\"20.0 S\"]"));
    CHECK(has(spec, "\"hear\":60,") && has(spec, "\"marks\":[2,7],\"least\":1,"));
    CHECK(has(spec, "\"recording\":\"\"") && peaksOf(spec).empty()); // no recording yet: no picture

    // A recording picked on the handheld: its picture comes with the spec, made on the program's own
    // thread when the recording was read. Not before a frame has gone by: the server only hands over
    // what the program made.
    const int before = specNumber(rig.get("/design/state").body);
    session.setTrackSample(track, "stepped");
    CHECK(rig.get("/design/spec").body == spec);
    rig.frames(1);
    std::string state = rig.get("/design/state").body;
    CHECK(specNumber(state) == before + 1);
    spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"recording\":\"stepped\"") && has(spec, "\"seconds\":1.000,\"missing\":false"));
    const std::vector<int> peaks = peaksOf(spec);
    CHECK(peaks.size() == 512 && *std::max_element(peaks.begin(), peaks.end()) == 800);
    if (peaks.size() == 512) {
        CHECK(peaks[10] >= 195 && peaks[10] <= 200 && peaks[245] >= 195 && peaks[245] <= 200); // the quiet half
        CHECK(peaks[266] >= 795 && peaks[266] <= 800 && peaks[511] >= 795 && peaks[511] <= 800); // the loud half
    }
    rig.frames(3);
    CHECK(specNumber(rig.get("/design/state").body) == before + 1); // nothing changed: the page is not sent it again
    CHECK(has(state, "\"name\":\"SAMPLER\",") && has(state, "\"preset\":-1") && valuesOf(state) == "\"values\":[60,0,0,0,20,0,80,100]" );

    // START from the page: the track has it, the engine plays from there.
    rig.loud();
    const float fromTop = rig.loud();
    CHECK(rig.tell("set 2 50") && rig.track().sampler[sampler::Start] == 50);
    const float fromMiddle = rig.loud();
    CHECK(fromMiddle > 3.f * fromTop);
    CHECK(valuesOf(rig.get("/design/state").body) == "\"values\":[60,0,50,0,20,0,80,100]");
    // START and END never cross, from the page as on the handheld.
    CHECK(rig.tell("set 7 30") && rig.track().sampler[sampler::End] == 51);
    CHECK(rig.tell("set 2 99") && rig.track().sampler[sampler::Start] == 50);
    CHECK(rig.tell("set 7 80") && rig.tell("set 2 79") && rig.track().sampler[sampler::Start] == 79);
    CHECK(rig.tell("set 2 95") && rig.track().sampler[sampler::Start] == 79);
    CHECK(rig.tell("set 7 -40") && rig.track().sampler[sampler::End] == 80);
    CHECK(rig.tell("set 2 -40") && rig.track().sampler[sampler::Start] == 0);
    CHECK(rig.tell("set 7 400") && rig.track().sampler[sampler::End] == 100);
    // PITCH, kept as the root key: 72 plays middle C an octave down.
    const int high = rig.crossings();
    CHECK(rig.tell("set 0 72") && rig.track().sampler[sampler::Root] == 72);
    const int low = rig.crossings();
    CHECK(high > 100 && low * 2 >= high - 2 && low * 2 <= high + 2);
    rig.tell("set 0 60");
    // A setting that is not there changes nothing; nor do a synth's requests.
    int kept[sampler::kSettings];
    std::copy(rig.track().sampler, rig.track().sampler + sampler::kSettings, kept);
    CHECK(rig.tell("set 8 5") && rig.tell("set -1 5") && rig.tell("macro 0 0.9") && rig.tell("preset 1") && rig.tell("roll 4") && rig.tell("save mine"));
    CHECK(std::equal(kept, kept + sampler::kSettings, rig.track().sampler) && !exists(rig.root + "/presets/MINE.sampler") && !exists(rig.root + "/presets/MINE"));

    // Its key: down, it sounds and goes on; up, it stops.
    rig.peak(96000);
    CHECK(rig.peak(4000) == 0.f);
    CHECK(rig.tell("note 60 1") && rig.peak(4000) > 0.05f);
    CHECK(rig.tell("note 60 0"));
    rig.peak(96000);
    CHECK(rig.peak(4000) < 0.002f);

    // Another recording from the page: only one it was offered. A name that could leave the folder is
    // refused at the door; one that is not on the card is taken and changes nothing.
    CHECK(rig.tell("sample drums/kick") && std::string(rig.track().sample) == "drums/kick");
    rig.frames(1);
    spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"recording\":\"drums/kick\"") && has(spec, "\"seconds\":0.100,") && peaksOf(spec).size() == 512 && peaksOf(spec)[0] == 500);
    CHECK(rig.post("sample ../songs/x").status == 400 && rig.post("sample /etc/passwd").status == 400 && rig.post("sample a/b/c/d").status == 400);
    CHECK(rig.tell("sample notthere") && std::string(rig.track().sample) == "drums/kick");
    CHECK(rig.tell("sample") && std::string(rig.track().sample).empty());
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"recording\":\"\"") && peaksOf(rig.get("/design/spec").body).empty());
    rig.tell("sample stepped");
    CHECK(std::string(rig.track().sample) == "stepped");

    // A recording put on the card some other way is offered within a few seconds while the page looks.
    CHECK(writeWav(rig.root + "/samples/later.wav", std::vector<float>(480, 0.1f), 48000));
    int waited = 0;
    for (; waited < 8 && !has(rig.get("/design/spec").body, "\"later\""); ++waited) {
        rig.get("/design/state"); // the page looks about once a second
        rig.frames(60);
    }
    CHECK(waited >= 1 && waited <= 6); // not at once (the card is not read every frame), and within five seconds
    CHECK(has(rig.get("/design/spec").body, "\"later\"") && rig.tell("sample later") && std::string(rig.track().sample) == "later");
    rig.tell("sample stepped");

    // A song may name a recording that is not on the card: the page is told so.
    session.setTrackSample(track, "gone");
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"missing\":true,\"recording\":\"gone\"") && peaksOf(rig.get("/design/spec").body).empty());
    session.setTrackSample(track, "stepped");
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"missing\":false,\"recording\":\"stepped\""));

    // One sent from the WiFi page is offered on the next frame.
    CHECK(writeWav(rig.root + "/sent.wav", std::vector<float>(480, 0.2f), 48000));
    std::string bytes;
    CHECK(readTextFile(rig.root + "/sent.wav", bytes) && !bytes.empty());
    CHECK(raw(rig.port, "PUT " + rig.door + "/samples/sent.wav HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(bytes.size()) + "\r\n\r\n" + bytes).status == 200);
    rig.get("/design/state");
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"sent\""));

    // What is turned on the handheld shows on the page; what is turned on the page is a step of undo.
    session.setSamplerSetting(track, sampler::Level, 33);
    rig.frames(1);
    CHECK(has(valuesOf(rig.get("/design/state").body), ",33,100]"));
    for (int i = 0; i < 4; ++i) rig.frames(30);
    CHECK(rig.tell("set 6 70") && rig.track().sampler[sampler::Level] == 70);
    for (int i = 0; i < 4; ++i) rig.frames(30);
    CHECK(session.undo() && rig.track().sampler[sampler::Level] == 33);

    // A file of floats may be louder than full: its picture stops at the top. And a second sampler,
    // its recording read as often as the first's (the same version), is its own picture.
    CHECK(writeTextFile(rig.root + "/samples/hot.wav", floatWav(std::vector<float>(4800, 2.f))));
    CHECK(session.addTrack(TrackKind::Sampler));
    const int second = rig.app.project().trackCount() - 1;
    while (session.trackSound(second).version < session.trackSound(track).version) session.setTrackSample(second, "hot");
    CHECK(session.trackSound(second).version == session.trackSound(track).version && std::string(rig.app.project().tracks[second].sample) == "hot");
    rig.app.project().track = second;
    rig.frames(1);
    spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"recording\":\"hot\"") && peaksOf(spec).size() == 512 && peaksOf(spec)[3] == 1000);
    rig.app.project().track = track;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"recording\":\"stepped\""));

    // The page follows the handheld to a synth and back.
    const int onSampler = specNumber(rig.get("/design/state").body);
    rig.addSynth();
    CHECK(has(rig.get("/design/spec").body, "\"page\":\"synth\"") && specNumber(rig.get("/design/state").body) > onSampler);
    rig.app.project().track = track;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"page\":\"sampler\"") && has(rig.get("/design/spec").body, "\"recording\":\"stepped\""));
}

// The drum KIT (owner, 2026-11-16): eight lanes, each its sound, its picture (the whole file, made on
// the app thread when it is read) with START and END, and its settings under ids of its own.
namespace {
// The values the page is given, as numbers (a kit's: kKitStride a lane).
std::vector<int> numbersOf(const std::string& state) {
    std::vector<int> out;
    const std::string list = valuesOf(state);
    if (list.size() < 11) return out;
    std::istringstream in(list.substr(10, list.size() - 11));
    std::string one;
    while (std::getline(in, one, ',')) out.push_back(std::atoi(one.c_str()));
    return out;
}
// The lane's picture in the spec (its "peaks" after the lane's own "lane":N).
std::vector<int> lanePeaksOf(const std::string& spec, int lane) {
    const std::size_t at = spec.find("{\"lane\":" + std::to_string(lane) + ",");
    return at == std::string::npos ? std::vector<int>() : peaksOf(spec.substr(at));
}
std::string laneOf(const std::string& spec, int lane) { // its wave, as text
    const std::size_t at = spec.find("{\"lane\":" + std::to_string(lane) + ",");
    return at == std::string::npos ? "" : spec.substr(at, spec.find("]}", at) + 2 - at);
}
std::string readingsOf(const std::string& state, int lane) { // the lane's eight readings, as text
    std::size_t at = state.find("\"lanes\":[");
    for (int i = 0; i <= lane && at != std::string::npos; ++i) at = state.find("{\"readings\":[", at + 1);
    return at == std::string::npos ? "" : state.substr(at + 13, state.find(']', at) - at - 13);
}
int kitId(int lane, int field) { return lane * kKitStride + field; }
} // namespace

void testKitFromThePage() {
    Rig rig;
    Session& session = rig.app.session();
    mkdir((rig.root + "/samples").c_str(), 0755);
    mkdir((rig.root + "/samples/drums").c_str(), 0755);
    CHECK(writeWav(rig.root + "/samples/stepped.wav", steppedSine(), 48000));
    // A quarter second of quiet, a quarter of a tone, half a second of quiet: for TRIM SILENCE.
    std::vector<float> padded(48000, 0.f);
    for (std::size_t i = 12000; i < 24000; ++i) padded[i] = 0.5f * std::sin(6.2831853f * 440.f * static_cast<float>(i) / 48000.f);
    CHECK(writeWav(rig.root + "/samples/drums/padded.wav", padded, 48000));
    CHECK(rig.track().kind == TrackKind::Drums && rig.app.project().track == 0);
    TrackData& kit = rig.track();
    const auto hit = [&](int lane, int frames) { // the lane struck once, from silence: its loudest
        rig.peak(2 * 48000);
        Command c;
        c.type = Command::Trigger;
        c.track = 0;
        c.a = lane;
        c.value = 0.9f;
        rig.engine->send(c);
        return rig.peak(frames);
    };

    // What the page builds itself from: the KIT, its settings once (each lane's is lane * stride + field),
    // a picture a lane. Until the page looks, the card is not read for it, and the lanes not for their pictures.
    std::string spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"name\":\"KIT\",\"macros\":[],\"pages\":[],\"presets\":[],\"page\":\"kit\",\"stride\":16,") && has(spec, "\"recordings\":[]"));
    CHECK(count(spec, "\"field\":") == kKitFields && count(spec, "{\"lane\":") == kDrumLanes);
    CHECK(has(spec, "{\"name\":\"START\",\"field\":0,\"min\":0,\"max\":100000,\"time\":true}") && has(spec, "{\"name\":\"END\",\"field\":1,\"min\":0,\"max\":100000,\"time\":true}"));
    CHECK(has(spec, "{\"name\":\"FADE IN\",\"field\":2,\"stops\":[0,1,2,3,5,10,") && has(spec, ",1500,2000],\"texts\":[\"0 MS\",\"1 MS\",") && has(spec, "\"750 MS\",\"1.0 S\",\"1.5 S\",\"2.0 S\"]}"));
    CHECK(has(spec, "{\"name\":\"GAIN\",\"field\":4,\"min\":-240,\"max\":240,\"texts\":[\"-24.0 DB\",\"-23.9 DB\",") && has(spec, "\"-0.1 DB\",\"0 DB\",\"+0.1 DB\",") && has(spec, "\"+24.0 DB\"]}"));
    CHECK(has(spec, "{\"name\":\"PITCH\",\"field\":5,\"min\":-24,\"max\":24,\"texts\":[\"-24\",") && has(spec, "\"-1\",\"0\",\"+1\",") && has(spec, "{\"name\":\"FINE\",\"field\":6,\"min\":-50,\"max\":50,\"texts\":[\"-50 CENTS\","));
    CHECK(has(spec, "{\"name\":\"REVERSE\",\"field\":7,\"min\":0,\"max\":1,\"texts\":[\"OFF\",\"ON\"]}"));
    CHECK(has(spec, "{\"name\":\"FIT TO\",\"field\":8,\"stops\":[0,1,2,4,8,16,32,64],\"texts\":[\"OFF\",\"1 STEP\",\"2 STEPS\",\"4 STEPS\",\"8 STEPS\",\"1 BAR\",\"2 BARS\",\"4 BARS\"]}"));
    CHECK(has(spec, "{\"name\":\"CHOKE\",\"field\":9,\"min\":0,\"max\":2,\"texts\":[\"NONE\",\"1\",\"2\"]}") && has(spec, "{\"name\":\"VOLUME\",\"field\":10,\"min\":0,\"max\":100,\"texts\":[\"0%\",\"1%\","));
    CHECK(has(spec, "{\"name\":\"HUMAN TIME\",\"field\":11,\"stops\":[0,10,25,50,75,100],\"texts\":[\"OFF\",\"10%\",\"25%\",\"50%\",\"75%\",\"100%\"]}") && has(spec, "{\"name\":\"HUMAN LEVEL\",\"field\":12,"));
    CHECK(has(spec, "{\"lane\":0,\"drum\":\"KICK\",") && has(spec, "{\"lane\":3,\"drum\":\"OP HAT\",") && has(spec, "{\"lane\":7,\"drum\":\"CRASH\","));
    CHECK(has(laneOf(spec, 5), "\"marks\":[80,81],") && has(laneOf(spec, 0), "\"recording\":\"\",") && has(laneOf(spec, 0), "\"units\":100000,"));
    // The state: every lane's settings by its ids, and what the handheld reads for its EDIT rows.
    std::string state = rig.get("/design/state").body;
    std::vector<int> v = numbersOf(state);
    CHECK(has(state, "\"name\":\"KIT\",") && v.size() == static_cast<std::size_t>(kDrumLanes * kKitStride));
    if (v.size() == static_cast<std::size_t>(kDrumLanes * kKitStride)) {
        CHECK(v[kitId(0, kEditEnd)] == kEditUnits && v[kitId(7, kEditEnd)] == kEditUnits && v[kitId(4, kKitVolume)] == 100 && v[kitId(4, kKitFit)] == 0);
        CHECK(v[kitId(2, kKitChoke)] == 1 && v[kitId(3, kKitChoke)] == 1 && v[kitId(1, kKitChoke)] == 0); // a new kit: its hats in CHOKE 1
    }
    // ...and once it looks, each lane's picture comes, made on the program's own thread: not before a frame.
    rig.frames(1);
    spec = rig.get("/design/spec").body;
    CHECK(has(spec, "\"recordings\":[\"drums/padded\",\"stepped\"]") || has(spec, "\"recordings\":[\"stepped\",\"drums/padded\"]"));
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const std::vector<int> peaks = lanePeaksOf(spec, lane);
        CHECK(peaks.size() == 512 && *std::max_element(peaks.begin(), peaks.end()) > 100); // the built-in drums, each its own
        CHECK(session.laneSound(0, lane).sourcePeaks.size() == 512);
    }
    CHECK(lanePeaksOf(spec, 0) != lanePeaksOf(spec, 1) && has(laneOf(spec, 7), "\"seconds\":1.8"));
    // START and END are parts of the lane's own sound: the kick is 0.6 s, so they come no nearer than 5 ms of it.
    CHECK(has(laneOf(spec, 0), "\"seconds\":0.600,") && has(laneOf(spec, 0), ",\"least\":" + std::to_string(leastSpan(0.6f)) + ",") && leastSpan(0.6f) == 834);
    state = rig.get("/design/state").body;
    CHECK(has(readingsOf(state, 0), "\"0.0 MS\",\"600.0 MS\",") && has(readingsOf(state, 0), "\",\"0 MS\",\"0 MS\",\"0 DB\",\"0\",\"0 CENTS\",\"OFF\"") && has(state, "\"plays\":"));

    // A recording on a lane from the page: only one it was offered. Its picture comes with the spec, the other lanes' as they were.
    const int before = specNumber(rig.get("/design/state").body);
    const std::vector<int> kick = lanePeaksOf(spec, 0);
    CHECK(rig.post("lanesample 1 stepped").status == 200);
    CHECK(rig.get("/design/spec").body == spec); // (the server only hands over what the program made)
    rig.frames(1);
    CHECK(std::string(kit.laneSample[1]) == "stepped" && std::string(kit.laneSample[0]).empty());
    CHECK(specNumber(rig.get("/design/state").body) == before + 1);
    spec = rig.get("/design/spec").body;
    CHECK(has(laneOf(spec, 1), "\"seconds\":1.000,\"missing\":false,\"recording\":\"stepped\",\"marks\":[16,17],\"least\":500,"));
    std::vector<int> peaks = lanePeaksOf(spec, 1);
    CHECK(peaks.size() == 512 && *std::max_element(peaks.begin(), peaks.end()) == 800 && lanePeaksOf(spec, 0) == kick);
    if (peaks.size() == 512) CHECK(peaks[10] >= 195 && peaks[10] <= 200 && peaks[266] >= 795);
    rig.frames(3);
    CHECK(specNumber(rig.get("/design/state").body) == before + 1); // nothing changed: the page is not sent it again
    CHECK(rig.post("lanesample 8 stepped").status == 400 && rig.post("lanesample -1 stepped").status == 400 && rig.post("lanesample x stepped").status == 400);
    CHECK(rig.post("lanesample 1 ../songs/x").status == 400 && rig.post("lanesample 1 /etc/passwd").status == 400 && rig.post("lanesample").status == 400);
    CHECK(rig.tell("lanesample 1 notthere") && std::string(kit.laneSample[1]) == "stepped"); // not on the card: nothing
    CHECK(rig.tell("lanesample 2 drums/padded") && std::string(kit.laneSample[2]) == "drums/padded");
    CHECK(rig.tell("lanesample 2") && std::string(kit.laneSample[2]).empty() && rig.tell("lanesample 2 drums/padded"));

    // A lane's START from the page: that lane's, no other; the engine plays it from there.
    const int picked = specNumber(rig.get("/design/state").body);
    const float fromTop = hit(1, 4800);
    CHECK(rig.tell("set 16 50000") && kit.laneEdit[1].start == 50000 && kit.laneEdit[0].start == 0 && kit.laneEdit[2].start == 0);
    rig.frames(1);
    const float fromMiddle = hit(1, 4800);
    CHECK(fromMiddle > 3.f * fromTop);
    CHECK(specNumber(rig.get("/design/state").body) == picked); // an edit: the picture (the file) is as it was
    state = rig.get("/design/state").body;
    CHECK(numbersOf(state)[kitId(1, kEditStart)] == 50000 && has(readingsOf(state, 1), "\"500.0 MS\",\"1.000 S\","));
    // START and END never cross: as on the handheld, START gives way (laneEditHeld).
    CHECK(rig.tell("set 17 40000") && kit.laneEdit[1].end == 40000 && kit.laneEdit[1].start == 39500);
    CHECK(rig.tell("set 17 900000") && kit.laneEdit[1].end == kEditUnits && rig.tell("set 16 -5") && kit.laneEdit[1].start == 0);
    // Every field reaches its lane.
    CHECK(rig.tell("set 18 75") && kit.laneEdit[1].fadeIn == 75 && rig.tell("set 19 3000") && kit.laneEdit[1].fadeOut == kFadeMost);
    CHECK(rig.tell("set 20 -60") && kit.laneEdit[1].gain == -60 && rig.tell("set 21 7") && kit.laneEdit[1].pitch == 7);
    CHECK(rig.tell("set 22 -9") && kit.laneEdit[1].fine == -9 && rig.tell("set 23 1") && kit.laneEdit[1].reverse == 1);
    state = rig.get("/design/state").body;
    CHECK(has(readingsOf(state, 1), "\"75 MS\",\"2.0 S\",\"-6.0 DB\",\"+7\",\"-9 CENTS\",\"ON\""));
    CHECK(rig.tell("set 24 16") && kit.laneFit[1] == 16 && rig.tell("set 25 2") && kit.laneChoke[1] == 2 && rig.tell("set 25 7") && kit.laneChoke[1] == kChokeNone);
    CHECK(rig.tell("set 26 37") && kit.laneLevel[1] == 0.37f && rig.tell("set 27 25") && kit.laneFeel[1].time == 25 && rig.tell("set 28 50") && kit.laneFeel[1].level == 50 && kit.laneFeel[1].time == 25);
    CHECK(rig.tell("set 27 0") && kit.laneFeel[1].time == 0 && kit.laneFeel[1].level == 50);
    CHECK(rig.tell("set 26 400") && kit.laneLevel[1] == 1.f && rig.tell("set 26 -4") && kit.laneLevel[1] == 0.f);
    v = numbersOf(rig.get("/design/state").body);
    CHECK(v.size() == 128 && v[24] == 16 && v[25] == 0 && v[26] == 0 && v[27] == 0 && v[28] == 50 && v[kitId(2, kKitFit)] == 0);
    // Nothing else is touched: lane 0's and lane 2's settings as they were.
    CHECK(kit.laneEdit[0].atRest() && kit.laneEdit[2].atRest() && kit.laneLevel[0] == 1.f && kit.laneChoke[2] == 1 && kit.laneFit[0] == 0 && kit.laneFeel[0].atRest());
    // An id with no lane or no setting changes nothing; nor do a synth's or a sampler's requests.
    const TrackData kept = kit;
    CHECK(rig.tell("set 128 5") && rig.tell("set -1 5") && rig.tell("set 13 5") && rig.tell("set 15 5") && rig.tell("set 125 5"));
    CHECK(rig.tell("macro 0 0.2") && rig.tell("preset 1") && rig.tell("sample stepped") && rig.tell("roll 3") && rig.tell("note 60 1") && rig.tell("note 60 0"));
    CHECK(std::memcmp(kept.laneEdit, kit.laneEdit, sizeof kit.laneEdit) == 0 && std::memcmp(kept.laneFeel, kit.laneFeel, sizeof kit.laneFeel) == 0);
    CHECK(std::memcmp(kept.laneLevel, kit.laneLevel, sizeof kit.laneLevel) == 0 && std::memcmp(kept.laneChoke, kit.laneChoke, sizeof kit.laneChoke) == 0 && std::memcmp(kept.laneFit, kit.laneFit, sizeof kit.laneFit) == 0);
    CHECK(rig.post("set 1").status == 400 && rig.post("set 16 1 2").status == 400 && rig.post("set x 1").status == 400);
    CHECK(rig.tell("name  break kit") && std::string(kit.name) == "BREAK KIT"); // the track's name, as on any page

    // The lane's volume reaches the engine; the drum grid sets it the same way (Session::setLaneLevel).
    rig.tell("set 16 0");
    rig.tell("set 17 100000");
    for (int f : {18, 19, 20, 21, 22, 23}) rig.tell("set " + std::to_string(f) + " 0");
    rig.tell("set 24 0");
    rig.frames(1);
    rig.tell("set 26 100");
    const float full = hit(1, 48000);
    rig.tell("set 26 25");
    const float quarter = hit(1, 48000);
    CHECK(full > 0.1f && quarter > 0.2f * full && quarter < 0.3f * full);
    rig.tell("set 26 100");

    // NORMALIZE: GAIN so what is played peaks at -1 dB (it starts in the loud half here, at 0.8).
    rig.tell("set 16 50000");
    CHECK(rig.tell("normalize 1") && kit.laneEdit[1].gain == 9);
    CHECK(has(readingsOf(rig.get("/design/state").body, 1), "\"+0.9 DB\""));
    // TRIM SILENCE: START and END at the first and last sound (START a millisecond ahead).
    CHECK(rig.tell("trim 2") && kit.laneEdit[2].start >= 24800 && kit.laneEdit[2].start <= 24950 && kit.laneEdit[2].end >= 50000 && kit.laneEdit[2].end <= 50010);
    CHECK(rig.post("normalize 8").status == 400 && rig.post("trim -1").status == 400 && rig.post("trim").status == 400 && rig.post("normalize 1 2").status == 400);

    // PLAY, held: heard until it is let go, whatever X does on the handheld (it is not held there).
    rig.tell("set 16 0");
    rig.tell("set 20 0");
    rig.frames(1);
    rig.peak(2 * 48000);
    CHECK(rig.peak(2400) == 0.f);
    CHECK(rig.tell("hear 1 1") && rig.peak(2400) > 0.05f);
    rig.frames(40); // 640 ms: a tap would have been let go by now
    CHECK(rig.peak(2400) > 0.05f);
    CHECK(rig.tell("hear 1 0"));
    rig.peak(480);
    CHECK(rig.peak(2400) < 0.001f);
    // Two lanes: the second takes over, and letting go of the first then does nothing.
    CHECK(rig.tell("hear 1 1") && rig.tell("hear 2 1") && rig.tell("hear 1 0"));
    rig.frames(40);
    rig.peak(480);
    CHECK(session.hearingHeld() && session.heardLane() == 2);
    rig.tell("hear 2 0");
    CHECK(!session.hearingHeld());
    // The page gone (it stops asking how things are) lets go; so does another track picked on the handheld.
    CHECK(rig.tell("hear 1 1") && session.hearingHeld());
    rig.frames(120);
    CHECK(session.hearingHeld());
    rig.frames(100);
    CHECK(!session.hearingHeld());
    CHECK(rig.tell("hear 1 1") && session.hearingHeld());
    CHECK(session.addTrack(TrackKind::Sampler));
    rig.app.project().track = 1;
    rig.frames(1);
    CHECK(!session.hearingHeld());
    rig.app.project().track = 0;
    rig.frames(1);
    CHECK(rig.post("hear 8 1").status == 400 && rig.post("hear 1 2").status == 400 && rig.post("hear 1").status == 400 && rig.post("hear -1 0").status == 400 && rig.post("hear 1 -1").status == 400);

    // What is turned on the handheld shows on the page; what is turned on the page is a step of undo.
    session.setLaneChoke(0, 5, 2);
    rig.frames(1);
    CHECK(numbersOf(rig.get("/design/state").body)[kitId(5, kKitChoke)] == 2);
    for (int i = 0; i < 4; ++i) rig.frames(30);
    CHECK(rig.tell("set 85 7") && kit.laneEdit[5].pitch == 7); // lane 5's PITCH: a built-in drum, edited
    for (int i = 0; i < 4; ++i) rig.frames(30);
    CHECK(session.laneSound(0, 5).peaks.size() == 512); // drawn on the KIT page now: it is a lane sample
    CHECK(session.undo() && kit.laneEdit[5].pitch == 0 && kit.laneChoke[5] == 2);

    // A song may name a recording that is not on the card: the page is told so, and gets no picture.
    session.setLaneSample(0, 6, "gone");
    rig.frames(1);
    spec = rig.get("/design/spec").body;
    CHECK(has(laneOf(spec, 6), "\"missing\":true,\"recording\":\"gone\"") && lanePeaksOf(spec, 6).empty());
    // ...and a recording read again from the card (sent over WiFi) is a new picture.
    CHECK(writeWav(rig.root + "/sent.wav", std::vector<float>(4800, 0.25f), 48000));
    std::string bytes;
    CHECK(readTextFile(rig.root + "/sent.wav", bytes) && !bytes.empty());
    const int sentBefore = specNumber(rig.get("/design/state").body);
    CHECK(raw(rig.port, "PUT " + rig.door + "/samples/gone.wav HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(bytes.size()) + "\r\n\r\n" + bytes).status == 200);
    rig.get("/design/state");
    rig.frames(2);
    spec = rig.get("/design/spec").body;
    CHECK(specNumber(rig.get("/design/state").body) > sentBefore && has(laneOf(spec, 6), "\"missing\":false,\"recording\":\"gone\"") && lanePeaksOf(spec, 6).size() == 512 && lanePeaksOf(spec, 6)[0] == 250);

    // The page follows the handheld to a sampler and back.
    rig.app.project().track = 1;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"page\":\"sampler\""));
    rig.app.project().track = 0;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"page\":\"kit\""));
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
    CHECK(jx3pSpec().presetCount() == builtInPresets(TrackKind::Jx3p) + 1 && phazedSpec().presetCount() == builtInPresets(TrackKind::Phazed)); // its own list, not Phazed's
    rig.tell("set " + std::to_string(jx3p::Brilliance) + " 39");
    rig.tell("preset 0"); // the first built-in one
    CHECK(rig.track().preset == 0 && rig.track().jx3p.value[jx3p::VcaLevel] == Jx3p::presetPatch(0).value[jx3p::VcaLevel]);
    rig.tell("preset 7"); // A8 E. Piano I: LEVEL 9.9, no attack (the made sound is at LEVEL 0.5)
    CHECK(rig.track().preset == 7 && rig.track().jx3p.value[jx3p::VcaLevel] == 99 && rig.loud() > 3.f * sound);
    rig.tell("preset " + std::to_string(builtInPresets(TrackKind::Jx3p)));
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
    CHECK(has(rig.get("/design/spec").body, "\"page\":\"kit\""));
    rig.app.project().track = 1;
    rig.frames(1);
    CHECK(has(rig.get("/design/spec").body, "\"name\":\"JAX-EP\""));
}

} // namespace

int main() {
    // The engine's own presets only: the tests here are of their tables. The presets the app's data
    // adds (and any track sound it gives a built-in) are presets_test's.
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Physic, TrackKind::Acid, TrackKind::Cloud}) useBuiltInDataForTests(kind, PresetData());
    testNamesAndRequests();
    testThePageIsServed();
    testPhazedFromThePage();
    testKeysFromThePage();
    testSavingAPreset();
    testFm6FromThePage();
    testPhysicFromThePage();
    testJaxepFromThePage();
    testSamplerFromThePage();
    testKitFromThePage();
    if (const char* hold = std::getenv("APOTHECA_DESIGN_HOLD")) {
        Rig rig;
        if (std::getenv("APOTHECA_DESIGN_SAMPLER")) { // (a SAMPLER track, on a recording of four hits; two more on the card)
            mkdir((rig.root + "/samples").c_str(), 0755);
            mkdir((rig.root + "/samples/drums").c_str(), 0755);
            std::vector<float> hits(2 * 48000);
            for (std::size_t i = 0; i < hits.size(); ++i) {
                const float t = static_cast<float>(i % 24000) / 48000.f;
                hits[i] = (0.9f - 0.15f * static_cast<float>(i / 24000)) * std::exp(-7.f * t) * std::sin(6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
            }
            writeWav(rig.root + "/samples/four hits.wav", hits, 48000);
            writeWav(rig.root + "/samples/stepped.wav", steppedSine(), 48000);
            writeWav(rig.root + "/samples/drums/kick.wav", std::vector<float>(4800, 0.5f), 48000);
            rig.app.session().addTrack(TrackKind::Sampler);
            rig.app.project().track = rig.app.project().trackCount() - 1;
            rig.app.session().setTrackSample(rig.app.project().track, "four hits");
            rig.frames(1);
        } else if (std::getenv("APOTHECA_DESIGN_KIT")) { // (the first track's KIT: four hits on lane 2, a stepped sine on lane 6)
            mkdir((rig.root + "/samples").c_str(), 0755);
            mkdir((rig.root + "/samples/drums").c_str(), 0755);
            std::vector<float> hits(2 * 48000);
            for (std::size_t i = 0; i < hits.size(); ++i) {
                const float t = static_cast<float>(i % 24000) / 48000.f;
                hits[i] = (0.9f - 0.15f * static_cast<float>(i / 24000)) * std::exp(-7.f * t) * std::sin(6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
            }
            writeWav(rig.root + "/samples/four hits.wav", hits, 48000);
            writeWav(rig.root + "/samples/stepped.wav", steppedSine(), 48000);
            writeWav(rig.root + "/samples/drums/kick.wav", std::vector<float>(4800, 0.5f), 48000);
            rig.app.project().track = 0;
            rig.app.session().setLaneSample(0, 1, "four hits");
            rig.app.session().setLaneSample(0, 5, "stepped");
            rig.frames(1);
        } else if (std::getenv("APOTHECA_DESIGN_PHYSIC")) { // (a PHYSIC track, on its first preset)
            rig.app.session().addTrack(TrackKind::Physic);
            rig.app.project().track = rig.app.project().trackCount() - 1;
            rig.frames(1);
        } else {
            rig.addSynth(std::getenv("APOTHECA_DESIGN_JAXEP") != nullptr);
            rig.tell("preset 8");
        }
        std::printf("open http://127.0.0.1:%d%s/design\n", rig.port, rig.door.c_str());
        std::fflush(stdout);
        bool sounding = false;
        const bool kit = rig.track().kind == TrackKind::Drums;
        std::vector<std::string> lanesWere(kDrumLanes);
        for (int i = 0; i < std::atoi(hold) * 60; ++i) {
            rig.frames(1);
            // A kit's lanes as the handheld has them, each time one changes (for a browser test: what the page did).
            for (int lane = 0; kit && lane < kDrumLanes; ++lane) {
                const TrackData& t = rig.track();
                const LaneEdit& e = t.laneEdit[lane];
                char line[256];
                std::snprintf(line, sizeof line, "lane %d sample '%s' start %d end %d gain %d fadein %d fadeout %d pitch %d fine %d reverse %d fit %d choke %d volume %.2f time %d level %d",
                              lane + 1, t.laneSample[lane], e.start, e.end, e.gain, e.fadeIn, e.fadeOut, e.pitch, e.fine, e.reverse, t.laneFit[lane], t.laneChoke[lane], static_cast<double>(t.laneLevel[lane]), t.laneFeel[lane].time, t.laneFeel[lane].level);
                if (lanesWere[static_cast<std::size_t>(lane)] != line) std::printf("%s\n", line), std::fflush(stdout);
                lanesWere[static_cast<std::size_t>(lane)] = line;
            }
            const bool now = rig.peak(800) > 0.001f;
            if (now != sounding) std::printf(now ? "sounding\n" : "silent\n"), std::fflush(stdout); // (for a browser test: the page's key was heard)
            sounding = now;
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
