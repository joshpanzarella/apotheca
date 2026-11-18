// WiFi on our own firmware, the app's side (app/wifi.hpp; docs/plans/firmware-plan.md, section 14),
// against a stand-in for the system's `apotheca-wifi` (tests/wifi_standin.hpp).
//   lines     what the program's lines say: every status, names with spaces, the name the page is
//             reached by there or not, a scan's order and its marks, lines that are not ours
//   calls     the Wifi itself: each call run as asked and in order, the password on stdin and never
//             among the words, no program, a program that fails, asked from the app thread only
//   row       the SET page's NETWORK row: here only with the program, its words in each state, hold A
//             + up / down and the join on letting go, B with A held, a let go unseen, hold B forgets
//   password  the keys' password mode: three sets, every sign, 63 at most, 8 at least, Y, B and START,
//             the track name's keys as they were
//   drawn     the hints at 0 over in every state, the lock mark, the page's name beside the address
//
// `wifi_test NAME...` runs just those.

#include <SDL.h>

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
#include "song_file.hpp"
#include "temp_folder.hpp"
#include "wifi.hpp"
#include "wifi_standin.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT, START = SDLK_RETURN;
typedef SettingsScreen S;

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::string folder, program;

    explicit Rig(const char* name, bool wifi = true) : folder(tempFolder(name)) {
        Session& s = app.session();
        s.useSongFolder(folder + "/songs", 3);
        s.useSampleFolder(folder + "/samples");
        if (wifi) program = wifiStandIn(folder);
        s.wifi().useProgram(program);
        s.view = View::Settings;
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) key(code, true), frames(1), key(code, false), frames(1);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    // Every call asked for has answered, and the screen has seen it.
    void settle() {
        frames(1);
        wifi().finish();
        frames(1);
    }
    // A second and more goes by: the status is asked for again, and has answered.
    void aSecond() {
        frames(70);
        settle();
    }
    Session& session() { return app.session(); }
    Wifi& wifi() { return app.session().wifi(); }
    Project& project() { return app.project(); }
    S& setup() { return app.settings(); }
    void say(const char* file, const std::string& text) { wifiFile(folder + "/" + file, text); }
    std::string log() { return wifiFileText(folder + "/log"); }
    void on() {
        setup().goTo(S::kWifi);
        tap(A);
        settle();
    }
    // What the NETWORK row draws: its value and what is said beside it.
    DrawLog drawn;
    void draw(Canvas& canvas) {
        canvas.clear();
        drawn.clear();
        gDrawLog = &drawn;
        app.draw(canvas.r, *canvas.fonts);
        gDrawLog = nullptr;
    }
    bool drew(const char* words) const {
        for (const DrawLog::Text& t : drawn.texts)
            if (t.words == words) return true;
        return false;
    }
};

// ---- the lines ---------------------------------------------------------------------------------

void testTheLines() {
    WifiStatus s = wifiStatusFrom("off\n");
    CHECK(s.state == WifiState::Off && s.name.empty());
    s = wifiStatusFrom("joining Home\n");
    CHECK(s.state == WifiState::Joining && s.name == "Home");
    s = wifiStatusFrom("joined Home 192.168.1.7 apotheca.local\n");
    CHECK(s.state == WifiState::Joined && s.name == "Home" && s.address == "192.168.1.7" && s.hostname == "apotheca.local");
    // The name the page is reached by may be another (two handhelds), or not said at all (an older system).
    s = wifiStatusFrom("joined Home 192.168.1.7 apotheca-2.local");
    CHECK(s.hostname == "apotheca-2.local" && s.address == "192.168.1.7");
    s = wifiStatusFrom("joined Home 192.168.1.7\n");
    CHECK(s.state == WifiState::Joined && s.name == "Home" && s.address == "192.168.1.7" && s.hostname.empty());
    // A network's name may hold spaces, and may itself end in .local or look like an address.
    s = wifiStatusFrom("joined The Old Mill 10.0.0.12 apotheca.local\r\n");
    CHECK(s.name == "The Old Mill" && s.address == "10.0.0.12" && s.hostname == "apotheca.local");
    s = wifiStatusFrom("joined cafe.local 10.0.0.12\n");
    CHECK(s.name == "cafe.local" && s.address == "10.0.0.12" && s.hostname.empty());
    s = wifiStatusFrom("joined My Net cafe.local\n"); // (a name that ENDS like the page's, and no address: all of it is the name)
    CHECK(s.name == "My Net cafe.local" && s.address.empty() && s.hostname.empty());
    s = wifiStatusFrom("joined 10.0.0.1\n"); // (a network NAMED like an address, and no address said)
    CHECK(s.state == WifiState::Joined && s.name == "10.0.0.1" && s.address.empty());
    s = wifiStatusFrom("joined Home fe80::1 apotheca.local\n");
    CHECK(s.name == "Home" && s.address == "fe80::1");
    s = wifiStatusFrom("wrong-password The Old Mill\n");
    CHECK(s.state == WifiState::WrongPassword && s.name == "The Old Mill");
    s = wifiStatusFrom("not-found Home\nmore\n");
    CHECK(s.state == WifiState::NotFound && s.name == "Home");
    // Not ours: off.
    for (const char* line : {"", "\n", "joined", "joining", "sideways Home", "JOINED Home 1.2.3.4"}) CHECK(wifiStatusFrom(line).state == WifiState::Off && wifiStatusFrom(line).name.empty());
    // A name is held to a network name's most.
    s = wifiStatusFrom("joining " + std::string(80, 'x'));
    CHECK(static_cast<int>(s.name.size()) == kWifiNameMost);

    // A scan: strongest first, whatever order it came in; the marks; the name with its spaces.
    std::vector<WifiNetwork> n = wifiNetworksFrom("-71 wpa2 The Old Mill\n-40 open Cafe\r\n-55 wpa2+saved Home\n-90 sae Far Away \n");
    CHECK(n.size() == 4);
    if (n.size() == 4) {
        CHECK(n[0].name == "Cafe" && n[0].open && !n[0].saved && !n[0].needsPassword() && n[0].signal == -40);
        CHECK(n[1].name == "Home" && !n[1].open && n[1].saved && !n[1].needsPassword());
        CHECK(n[2].name == "The Old Mill" && n[2].needsPassword());
        CHECK(n[3].name == "Far Away" && n[3].needsPassword());
    }
    // Percentages do as well as dBm; a name heard twice is one row, at its strongest.
    n = wifiNetworksFrom("40 WPA2 Home\n80 OPEN Cafe\n65 wpa2 Home\n");
    CHECK(n.size() == 2 && n[0].name == "Cafe" && n[0].open && n[1].name == "Home" && n[1].signal == 65);
    // Lines that are not ours, and a network with no name, are passed by.
    n = wifiNetworksFrom("\nscanning...\n-50 wpa2\nstrong open Cafe\n-50 wpa2 Home\n");
    CHECK(n.size() == 1 && n[0].name == "Home");
    CHECK(wifiNetworksFrom("").empty());
    // A hundred networks: the list is held to sixty-four.
    std::string many;
    for (int i = 0; i < 100; ++i) many += "-" + std::to_string(30 + i) + " wpa2 net" + std::to_string(i) + "\n";
    CHECK(wifiNetworksFrom(many).size() == 64);
}

// ---- the calls ---------------------------------------------------------------------------------

void testTheCalls() {
    const std::string folder = tempFolder("wificalls");
    {
        Wifi wifi;
        wifi.useProgram(""); // (whatever this desk's environment says)
        CHECK(!wifi.here());
        // No program: nothing runs, nothing waits, nothing is said.
        wifi.up(), wifi.scan(), wifi.join("Home", "secret"), wifi.forget("Home"), wifi.down();
        wifi.finish();
        CHECK(wifi.calls() == 0 && wifi.now().status.state == WifiState::Off && wifi.now().serial == 0);
    }
    {
        Wifi wifi;
        wifi.useProgram(wifiStandIn(folder));
        CHECK(wifi.here());
        wifiFile(folder + "/scan", "-50 wpa2 The Old Mill\n-40 open Cafe\n");
        wifiFile(folder + "/right", "correct horse 9!\n");
        wifi.up();
        wifi.scan();
        wifi.finish();
        Wifi::Now now = wifi.now();
        CHECK(now.scanned && now.networks.size() == 2 && now.networks[0].name == "Cafe" && now.status.state == WifiState::Off);
        CHECK(wifiFileText(folder + "/log") == "up\nstatus\nscan\n");
        // A join: JOINING at once (no frame waits), then what the system says. The password went in on
        // stdin, whole, and is not among the words the program was given.
        const int before = now.serial;
        wifi.join("The Old Mill", "correct horse 9!");
        CHECK(wifi.now().status.state == WifiState::Joining && wifi.now().status.name == "The Old Mill" && wifi.now().serial > before);
        wifi.finish();
        now = wifi.now();
        CHECK(now.status.state == WifiState::Joined && now.status.name == "The Old Mill" && now.status.address == "192.168.1.7" && now.status.hostname == "apotheca.local");
        CHECK(wifiFileText(folder + "/password") == "correct horse 9!\n");
        const std::string log = wifiFileText(folder + "/log");
        CHECK(log == "up\nstatus\nscan\njoin The Old Mill\nstatus\n" && log.find("horse") == std::string::npos);
        // The wrong one.
        wifi.join("The Old Mill", "wrong wrong");
        wifi.finish();
        CHECK(wifi.now().status.state == WifiState::WrongPassword && wifi.now().status.name == "The Old Mill");
        // None (an open or a saved network): nothing on stdin, and the program does not wait for any.
        wifiFile(folder + "/right", "");
        wifi.join("Cafe", "");
        wifi.finish();
        CHECK(wifi.now().status.state == WifiState::Joined && wifi.now().status.name == "Cafe" && wifiFileText(folder + "/password").empty());
        // Forgotten: off, and looked for again.
        wifi.forget("Cafe");
        wifi.finish();
        CHECK(wifi.now().status.state == WifiState::Off);
        CHECK(wifiFileText(folder + "/log").find("forget Cafe\nstatus\nscan\n") != std::string::npos);
        // Asked twice before it has run, a scan or a status runs once.
        const int calls = wifi.calls();
        wifiFile(folder + "/status", "joining Cafe\n");
        for (int i = 0; i < 50; ++i) wifi.status(), wifi.scan();
        wifi.finish();
        CHECK(wifi.calls() - calls <= 6 && wifi.now().status.state == WifiState::Joining);
        // The same answer again changes nothing (the redraw has nothing to draw).
        const int serial = wifi.serial();
        wifi.status();
        wifi.finish();
        CHECK(wifi.serial() == serial);
        // Down: the list goes, and the status.
        wifi.down();
        wifi.finish();
        CHECK(wifi.now().networks.empty() && !wifi.now().scanned && wifi.now().status.state == WifiState::Off);
        // update(): the status once a second while on, a scan at once when the row is looked at and
        // every ten seconds; nothing while off.
        wifiFile(folder + "/log", "");
        wifi.update(1000, false, true);
        wifi.finish();
        CHECK(wifiFileText(folder + "/log").empty());
        for (std::uint32_t ms = 2000; ms < 2000 + 12500; ms += 16) wifi.update(ms, true, ms >= 3000), wifi.finish();
        const std::string ran = wifiFileText(folder + "/log");
        int statuses = 0, scans = 0;
        for (std::size_t at = 0; (at = ran.find("status\n", at)) != std::string::npos; ++at) ++statuses;
        for (std::size_t at = 0; (at = ran.find("scan\n", at)) != std::string::npos; ++at) ++scans;
        CHECK(statuses == 12 && scans == 2);
        // Looked away and back: a scan at once, not at the ten seconds' end.
        wifi.update(14600, true, false), wifi.finish();
        wifi.update(14616, true, true), wifi.finish();
        const std::string again = wifiFileText(folder + "/log");
        scans = 0;
        for (std::size_t at = 0; (at = again.find("scan\n", at)) != std::string::npos; ++at) ++scans;
        CHECK(scans == 3);
    }
    {
        // A program that is not there, and one that fails: off, and no harm.
        Wifi wifi;
        wifi.useProgram(folder + "/no-such-program");
        wifi.up(), wifi.scan(), wifi.join("Home", "a password");
        wifi.finish();
        CHECK(wifi.now().status.state == WifiState::Off && wifi.now().networks.empty() && !wifi.now().scanned);
        const std::string failing = folder + "/failing";
        wifiFile(failing, "#!/bin/sh\necho joined Home 1.2.3.4\nexit 1\n");
        chmod(failing.c_str(), 0755);
        wifi.useProgram(failing);
        wifi.status(), wifi.scan();
        wifi.finish();
        CHECK(wifi.now().status.state == WifiState::Off && !wifi.now().scanned);
        // A program that never reads its stdin does not end ours (SIGPIPE), and one still asked for
        // when the Wifi goes is not waited on for ever.
        const std::string deaf = folder + "/deaf";
        wifiFile(deaf, "#!/bin/sh\nexit 0\n");
        chmod(deaf.c_str(), 0755);
        wifi.useProgram(deaf);
        for (int i = 0; i < 20; ++i) wifi.join("Home", std::string(63, 'p'));
        wifi.finish();
        CHECK(wifi.calls() > 0);
    }
}

// ---- the row -----------------------------------------------------------------------------------

void testTheRow() {
    {
        // No program (the port): no row, and nothing runs.
        Rig rig("wifinone", false);
        CHECK(!rig.session().wifiHere() && !rig.setup().here(S::kNetwork));
        rig.setup().goTo(S::kWifi);
        rig.tap(DOWN);
        CHECK(rig.setup().cursorRow() == S::kTest);
        rig.setup().goTo(S::kNetwork);
        CHECK(rig.setup().cursorRow() != S::kNetwork);
    }
    Rig rig("wifirow");
    S& setup = rig.setup();
    Session& session = rig.session();
    CHECK(session.wifiHere() && setup.here(S::kNetwork) && S::groupOf(S::kNetwork) == S::kSetup && S::kNetwork == S::kWifi + 1);
    rig.say("scan", "-71 wpa2 The Old Mill\n-40 open Cafe\n-55 wpa2+saved Home\n");
    rig.say("right", "millstone 42\n");
    // The page off: A on the row does nothing, no call has run.
    setup.goTo(S::kNetwork);
    rig.frames(80);
    rig.key(A, true), rig.frames(1);
    CHECK(!setup.picking());
    rig.key(A, false), rig.frames(1);
    rig.key(B, true), rig.frames(30);
    CHECK(setup.forgetPart() == 0.f);
    rig.frames(40), rig.key(B, false), rig.frames(1);
    rig.wifi().finish();
    CHECK(rig.log().empty() && rig.wifi().calls() == 0);
    // WIFI PAGE on: `up`. The row reached: a scan.
    rig.on();
    CHECK(session.transferOn() && rig.log().compare(0, 3, "up\n") == 0 && rig.log().find("scan") == std::string::npos);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kNetwork);
    rig.settle();
    CHECK(rig.log().find("scan\n") != std::string::npos && rig.wifi().now().networks.size() == 3);
    // A tap of A joins nothing.
    rig.tap(A);
    rig.settle();
    CHECK(rig.log().find("join") == std::string::npos && !setup.typing());
    // Hold A + DOWN: the strongest first; the ends stop; UP from the first stays.
    rig.key(A, true), rig.frames(1);
    CHECK(setup.picking() && setup.picked() == -1);
    rig.tap(UP);
    CHECK(setup.picked() == -1);
    rig.tap(DOWN);
    CHECK(setup.picked() == 0);
    rig.tap(DOWN, 5);
    CHECK(setup.picked() == 2);
    rig.tap(UP);
    CHECK(setup.picked() == 1 && setup.cursorRow() == S::kNetwork); // (the cursor has not left the row)
    // A scan that lands meanwhile moves nothing under the hand.
    rig.say("scan", "-30 open Newcomer\n-71 wpa2 The Old Mill\n-40 open Cafe\n-55 wpa2+saved Home\n");
    rig.wifi().scan();
    rig.settle();
    CHECK(setup.picked() == 1 && rig.wifi().now().networks.size() == 4);
    // B with A held: nothing is joined.
    rig.tap(B);
    CHECK(!setup.picking());
    rig.key(A, false), rig.frames(1);
    rig.settle();
    CHECK(rig.log().find("join") == std::string::npos);
    // A let go that this screen never saw (another tab meanwhile): nothing is joined.
    rig.key(A, true), rig.frames(1);
    rig.tap(DOWN);
    session.view = View::Song;
    rig.frames(2);
    rig.key(A, false), rig.frames(1);
    session.view = View::Settings;
    rig.frames(2);
    rig.settle();
    CHECK(!setup.picking() && rig.log().find("join") == std::string::npos);
    // Let go on a saved one: joined at once, no keys. (Newcomer, Cafe, Home, The Old Mill.)
    rig.key(A, true), rig.frames(1);
    rig.tap(DOWN, 3);
    CHECK(setup.picked() == 2);
    rig.key(A, false), rig.frames(1);
    CHECK(!setup.typing());
    rig.settle();
    CHECK(rig.log().find("join Home\n") != std::string::npos && wifiFileText(rig.folder + "/password").empty());
    CHECK(rig.wifi().now().status.state == WifiState::WrongPassword); // (the stand-in's "right" is the Mill's: no matter here)
    // A tap of A on a row that is about a network joins nothing again.
    rig.say("log", "");
    rig.tap(A);
    rig.settle();
    CHECK(rig.log().find("join") == std::string::npos);
    // Held again: the list starts on the network the row is about.
    rig.key(A, true), rig.frames(1);
    CHECK(setup.picked() == 2);
    // ...and one that needs a password opens the keys; nothing is joined yet.
    rig.tap(DOWN);
    rig.key(A, false), rig.frames(1);
    CHECK(setup.typingPassword() && setup.passwordFor() == "The Old Mill");
    rig.settle();
    CHECK(rig.log().find("join The Old Mill") == std::string::npos);
    // B with nothing typed leaves, and nothing is joined.
    rig.tap(B);
    CHECK(!setup.typing());
    rig.settle();
    CHECK(rig.log().find("join The Old Mill") == std::string::npos);
    // HOLD B forgets the one the row is about: not on a tap, not before the second is up, called off by another button.
    rig.say("status", "joined Home 192.168.1.7 apotheca.local\n");
    rig.aSecond();
    CHECK(rig.wifi().now().status.state == WifiState::Joined);
    rig.tap(B);
    rig.key(B, true), rig.frames(30);
    CHECK(setup.forgetPart() > 0.3f && setup.forgetPart() < 0.7f);
    rig.tap(LEFT);
    CHECK(setup.forgetPart() == 0.f);
    rig.frames(60);
    rig.key(B, false), rig.frames(1);
    rig.settle();
    CHECK(rig.log().find("forget") == std::string::npos);
    rig.key(B, true), rig.frames(61);
    rig.wifi().finish();
    CHECK(rig.log().find("forget") == std::string::npos);
    rig.frames(4);
    rig.settle();
    CHECK(rig.log().find("forget Home\n") != std::string::npos && setup.networkSaid() == "FORGOTTEN" && setup.forgetPart() == 0.f);
    rig.frames(70);
    rig.key(B, false), rig.frames(1);
    rig.aSecond();
    CHECK(rig.wifi().now().status.state == WifiState::Off);
    // With none joined, HOLD B forgets nothing.
    rig.say("log", "");
    rig.key(B, true), rig.frames(30);
    CHECK(setup.forgetPart() == 0.f); // (no bar fills)
    rig.frames(50), rig.key(B, false), rig.frames(1);
    rig.settle();
    CHECK(rig.log().find("forget") == std::string::npos);
    // The status is asked for about once a second while the page is on; a scan every ten while the row is up.
    rig.say("log", "");
    for (int second = 0; second < 11; ++second) rig.frames(63), rig.wifi().finish(); // (the test's seconds go by faster than a call answers)
    rig.settle();
    {
        const std::string ran = rig.log();
        int statuses = 0, scans = 0;
        for (std::size_t at = 0; (at = ran.find("status\n", at)) != std::string::npos; ++at) ++statuses;
        for (std::size_t at = 0; (at = ran.find("scan\n", at)) != std::string::npos; ++at) ++scans;
        if (!(statuses >= 10 && statuses <= 12 && scans >= 1 && scans <= 2)) std::printf("statuses %d scans %d\n", statuses, scans);
        CHECK(statuses >= 10 && statuses <= 12 && scans >= 1 && scans <= 2);
    }
    // WIFI PAGE off: `down`, and the row says NONE whatever the radio was doing.
    rig.setup().goTo(S::kWifi);
    rig.tap(A);
    rig.settle();
    CHECK(!session.transferOn() && rig.log().find("down\n") != std::string::npos);
}

// ---- the password's keys -----------------------------------------------------------------------

// Types `text` on the password's keys: turns to the set a letter is on (X), goes to it, A.
void type(Rig& rig, const std::string& text) {
    S& setup = rig.setup();
    for (char c : text) {
        int set = -1, at = -1;
        for (int s = 0; s < 3 && set < 0; ++s) {
            const int tried = (setup.passwordSet() + s) % 3;
            const char* found = std::strchr(S::kPasswordKeys[tried], c);
            if (found && c != '\x01') set = tried, at = static_cast<int>(found - S::kPasswordKeys[tried]);
        }
        CHECK(set >= 0);
        if (set < 0) return;
        while (setup.passwordSet() != set) rig.tap(X);
        while (setup.keyAt() / S::kKeyColumns != at / S::kKeyColumns) rig.tap(DOWN);
        while (setup.keyAt() % S::kKeyColumns != at % S::kKeyColumns) rig.tap(RIGHT);
        rig.tap(A);
    }
}

void testThePassword() {
    // Every sign a WPA password may hold (printable ASCII) is on a key, each set is forty, and no
    // set says another's name.
    for (int set = 0; set < 3; ++set) CHECK(std::strlen(S::kPasswordKeys[set]) == 40);
    for (char c = ' '; c <= '~'; ++c) CHECK(std::strchr(S::kPasswordKeys[0], c) || std::strchr(S::kPasswordKeys[1], c) || std::strchr(S::kPasswordKeys[2], c));
    CHECK(kWifiPasswordMost == 63 && kWifiPasswordLeast == 8);
    // The track name's keys are as they were.
    CHECK(std::strcmp(S::kKeys, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_ ") == 0 && S::kNameLength == 10);

    Rig rig("wifipassword");
    S& setup = rig.setup();
    rig.say("scan", "-50 wpa2 The Old Mill\n");
    rig.say("right", "Mill-stone 42!{~}\n");
    rig.on();
    setup.goTo(S::kNetwork);
    rig.settle();
    const auto open = [&] {
        rig.key(A, true), rig.frames(1);
        rig.tap(DOWN);
        rig.key(A, false), rig.frames(1);
    };
    open();
    CHECK(setup.typingPassword() && setup.passwordSet() == 0 && setup.typed().empty());
    // X turns the set, round; an empty key types nothing.
    rig.tap(X);
    CHECK(setup.passwordSet() == 1);
    rig.tap(X);
    CHECK(setup.passwordSet() == 2);
    rig.tap(UP), rig.tap(LEFT); // (the last key of the signs: one with nothing on it)
    rig.tap(A);
    CHECK(setup.typed().empty());
    rig.tap(X);
    CHECK(setup.passwordSet() == 0);
    rig.tap(DOWN), rig.tap(RIGHT);
    // Small letters and capitals are two things; a space may go anywhere; Y deletes the last.
    type(rig, "aA  b");
    CHECK(setup.typed() == "aA  b");
    rig.tap(Y, 2);
    CHECK(setup.typed() == "aA ");
    rig.tap(Y, 5);
    CHECK(setup.typed().empty());
    // Under eight: not joined, said, the keys stay. B or START alike.
    type(rig, "Mill");
    rig.tap(B);
    CHECK(setup.typingPassword() && setup.networkSaid() == "8 AT LEAST" && setup.typed() == "Mill");
    rig.tap(START);
    CHECK(setup.typingPassword());
    rig.settle();
    CHECK(rig.log().find("join") == std::string::npos && !rig.project().playing); // (START did not play either)
    // The whole of it, signs and all; B keeps it and joins. The words the program got do not hold it.
    type(rig, "-stone 42!{~}");
    CHECK(setup.typed() == "Mill-stone 42!{~}");
    rig.tap(B);
    CHECK(!setup.typing() && setup.typed().empty()); // (and it is not kept in the screen)
    CHECK(rig.wifi().now().status.state == WifiState::Joining);
    rig.settle();
    CHECK(wifiFileText(rig.folder + "/password") == "Mill-stone 42!{~}\n");
    CHECK(rig.log().find("join The Old Mill\n") != std::string::npos && rig.log().find("stone") == std::string::npos);
    CHECK(rig.wifi().now().status.state == WifiState::Joined && rig.wifi().now().status.hostname == "apotheca.local");
    // The wrong one: said on the row. START joins as B does.
    rig.say("scan", "-50 wpa2 Other Mill\n");
    rig.wifi().scan();
    rig.settle();
    open();
    CHECK(setup.typingPassword() && setup.passwordFor() == "Other Mill");
    type(rig, "12345678");
    rig.tap(START);
    CHECK(!setup.typing());
    rig.settle();
    CHECK(rig.wifi().now().status.state == WifiState::WrongPassword && wifiFileText(rig.folder + "/password") == "12345678\n");
    // Sixty-three at most.
    open();
    for (int i = 0; i < 70; ++i) rig.tap(A);
    CHECK(static_cast<int>(setup.typed().size()) == 63);
    rig.tap(Y, 63);
    rig.tap(B);
    CHECK(!setup.typing());
    // The track name's keys still type a name, capitals only, ten at most, and join nothing.
    rig.say("log", "");
    setup.goTo(S::kTrackName);
    rig.tap(A);
    CHECK(setup.typing() && !setup.typingPassword());
    rig.tap(Y, 12);
    for (int i = 0; i < 12; ++i) rig.tap(A);
    CHECK(setup.typed() == "AAAAAAAAAA");
    rig.tap(X); // (no sets here)
    rig.tap(B);
    CHECK(!setup.typing() && std::string(rig.project().current().name) == "AAAAAAAAAA");
    rig.settle();
    CHECK(rig.log().find("join") == std::string::npos);
}

// ---- what is drawn -----------------------------------------------------------------------------

void testDrawn() {
    Canvas canvas;
    if (!canvas.ok) return;
    Rig rig("wifidrawn");
    S& setup = rig.setup();
    rig.say("scan", "-71 wpa2 The Old Mill\n-40 open Cafe\n");
    const auto fits = [&](const char* what) {
        rig.draw(canvas);
        if (ui::gHintsOver != 0.f) std::printf("the hints are %.0f over: %s\n", static_cast<double>(ui::gHintsOver), what);
        CHECK(ui::gHintsOver == 0.f);
    };
    // The page off.
    setup.goTo(S::kNetwork);
    fits("off");
    CHECK(rig.drew("NETWORK") && rig.drew("NONE") && rig.drew("turn WIFI PAGE on first"));
    rig.on();
    setup.goTo(S::kNetwork);
    rig.frames(1);
    rig.draw(canvas);
    CHECK(rig.drew("LOOKING...") || rig.drew("2 FOUND"));
    rig.settle();
    fits("none joined");
    CHECK(rig.drew("NONE") && rig.drew("2 FOUND"));
    // Held: the network picked, its place in the list, and a lock only where a password is needed.
    const int y = setup.rowNowY(S::kNetwork);
    const auto lockAfter = [&](const char* name) {
        const int x = static_cast<int>(S::kValueX + canvas.fonts->large.width(name)) + 8;
        return canvas.is(x + 1, y + (S::kRowH - 6 - S::kLockH) / 2 + 6, ui::kBrass);
    };
    rig.key(A, true), rig.frames(1);
    fits("held, none picked");
    CHECK(rig.drew("0 OF 2"));
    rig.tap(DOWN);
    fits("held on an open one");
    CHECK(rig.drew("Cafe") && rig.drew("1 OF 2") && !lockAfter("Cafe"));
    rig.tap(DOWN);
    fits("held on a locked one");
    CHECK(rig.drew("The Old Mill") && rig.drew("2 OF 2") && lockAfter("The Old Mill"));
    rig.key(A, false), rig.frames(1);
    // The keys: each set, with nothing typed and with something.
    for (int set = 0; set < 3; ++set) {
        fits("the keys");
        CHECK(rig.drew("PASSWORD FOR The Old Mill") && rig.drew("0 OF 63") && rig.drew("SPACE"));
        CHECK(rig.drew(set == 0 ? "q" : set == 1 ? "Q" : "{") && !rig.drew(set == 0 ? "Q" : "q"));
        rig.tap(X);
    }
    rig.tap(A, 3);
    fits("the keys, typed");
    CHECK(rig.drew("aaa") && rig.drew("3 OF 63"));
    rig.tap(B);
    rig.draw(canvas);
    CHECK(rig.drew("8 AT LEAST"));
    // A long one shows its end.
    for (int i = 0; i < 59; ++i) rig.tap(A);
    rig.tap(RIGHT), rig.tap(A);
    fits("the keys, full");
    CHECK(rig.drew("63 OF 63"));
    bool tail = false;
    for (const DrawLog::Text& t : rig.drawn.texts) tail = tail || (t.words.compare(0, 3, "...") == 0 && t.words.back() == 'b' && t.x + t.w < ui::kScreenW - 56.f - 60.f);
    CHECK(tail);
    rig.tap(Y, 63);
    rig.tap(B);
    // Each state's words on the row.
    rig.say("status", "joining The Old Mill\n");
    rig.aSecond();
    fits("joining");
    CHECK(rig.drew("The Old Mill") && rig.drew("JOINING..."));
    rig.say("status", "wrong-password The Old Mill\n");
    rig.aSecond();
    fits("wrong");
    CHECK(rig.drew("WRONG PASSWORD"));
    rig.say("status", "not-found The Old Mill\n");
    rig.aSecond();
    fits("not found");
    CHECK(rig.drew("NOT FOUND"));
    // Joined: its address on the row; the WIFI PAGE row keeps its own address and says the name beside it.
    rig.say("status", "joined The Old Mill 192.168.1.7 apotheca-2.local\n");
    rig.aSecond();
    fits("joined");
    CHECK(rig.drew("The Old Mill") && rig.drew("192.168.1.7"));
    const std::string address = rig.session().transferAddress();
    const std::string on = "ON   " + address, beside = "or apotheca-2.local" + address.substr(address.find(':'));
    CHECK(rig.drew(on.c_str()) && rig.drew(beside.c_str()));
    // HOLD B: what cannot be undone says so, and a bar fills in the row.
    rig.key(B, true), rig.frames(30);
    fits("forgetting");
    CHECK(rig.drew(S::kForgettingWords));
    CHECK(canvas.is(20, y + S::kRowH - 9, ui::kStop) && !canvas.is(S::kRowX + S::kRowW - 20, y + S::kRowH - 9, ui::kStop));
    rig.key(B, false), rig.frames(1);
    // A system that does not say the name: the address alone, as on the port.
    rig.say("status", "joined The Old Mill 192.168.1.7\n");
    rig.aSecond();
    rig.draw(canvas);
    CHECK(rig.drew(on.c_str()) && !rig.drew(beside.c_str()));
    for (const DrawLog::Text& t : rig.drawn.texts) CHECK(t.words.compare(0, 3, "or ") != 0);
    // A name at its longest is cut to its room, and what is said beside it still stands clear.
    rig.say("status", "joined " + std::string(32, 'W') + " 192.168.100.200 apotheca.local\n");
    rig.aSecond();
    fits("the longest name");
    for (const DrawLog::Text& t : rig.drawn.texts)
        if (t.words.compare(0, 3, "WWW") == 0) CHECK(t.words.size() < 32 && t.words.compare(t.words.size() - 3, 3, "...") == 0 && t.x + t.w <= S::kNoteRight - canvas.fonts->small.width("192.168.100.200") - S::kNoteGap + 1.f);
    CHECK(rig.drew("192.168.100.200"));
    // The list of songs says the address too: by name and by number, where the name is said.
    const std::string named = "OPEN  apotheca.local" + address.substr(address.find(':')), numbered = "OR  " + address;
    rig.session().view = View::Song;
    rig.frames(1);
    rig.tap(SDLK_BACKSPACE); // SELECT: the list of songs
    fits("the list of songs, the page named");
    CHECK(rig.drew(named.c_str()) && rig.drew(numbered.c_str()));
    for (const DrawLog::Text& t : rig.drawn.texts)
        if (t.words == named || t.words == numbered) CHECK(t.x + t.w <= (ui::kScreenW + 360) / 2.f - 12.f + 0.5f && t.y + t.h <= 60 + 80 + 9 * 34);
    rig.tap(SDLK_BACKSPACE);
    rig.say("status", "joined The Old Mill 192.168.1.7\n");
    rig.aSecond();
    rig.tap(SDLK_BACKSPACE);
    rig.draw(canvas);
    CHECK(rig.drew(("OPEN IN A BROWSER:  " + address).c_str()) && !rig.drew(numbered.c_str()));
    rig.tap(SDLK_BACKSPACE);
}

struct Section {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const Section sections[] = {
        {"lines", [] { testTheLines(); }}, {"calls", [] { testTheCalls(); }}, {"row", [] { testTheRow(); }},
        {"password", [] { testThePassword(); }}, {"drawn", [] { testDrawn(); }},
    };
    for (const Section& s : sections) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], s.name) == 0;
        if (wanted) s.run();
    }
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("wifi: all passed\n");
    return 0;
}
