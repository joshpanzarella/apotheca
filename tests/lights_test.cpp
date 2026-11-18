// The screen's BRIGHTNESS and the STICK LIGHT (app/lights.hpp, docs/plans/lights-plan.md): the rules by
// hand, the files written in a made-up sysfs folder (a plain file stands for the ring's serial line), the
// SET page's two rows (shown only where the system says the files are the program's), settings.txt, and
// MENU + a volume key.
//
// What only the handheld can show is not here: that the ring lights at all, which LED is which, the
// colour order, and how bright 10% looks.

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "song_screen.hpp"
#include "lights.hpp"
#include "settings_screen.hpp"
#include "temp_folder.hpp"
#include "ui.hpp"

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

const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT, MENU = SDLK_TAB, A = SDLK_x;

void put(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string got(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
int number(const std::string& path) { return std::atoi(got(path).c_str()); }

// A made-up /sys: the backlight as our kernel makes it, the ring's power switch, and a plain file for
// its serial line.
struct Sys {
    std::string root, bl, power, line;
    explicit Sys(bool withPower = true) {
        root = tempFolder("lights");
        bl = root + "/class/backlight/backlight";
        for (const std::string& d : {root + "/class", root + "/class/backlight", bl, root + "/class/leds", root + "/class/leds/rgb:kbd_backlight"})
            mkdir(d.c_str(), 0755);
        put(bl + "/brightness", "1250\n");
        put(bl + "/max_brightness", "2499\n");
        put(bl + "/bl_power", "0\n");
        power = root + "/class/leds/rgb:kbd_backlight/brightness";
        if (withPower) put(power, "0\n");
        line = root + "/ttyS2";
        put(line, "");
    }
    Lights lights(const char* screen, const char* ring) const {
        std::map<std::string, std::string> env;
        if (screen) env["APOTHECA_OWNS_SCREEN"] = screen;
        if (ring) env["APOTHECA_STICK_LIGHT"] = ring;
        return Lights::from([&](const char* name) -> const char* {
            const auto it = env.find(name);
            return it == env.end() ? nullptr : it->second.c_str();
        }, root);
    }
    Lights both() const { return lights("1", line.c_str()); }
    std::size_t sent() const { return got(line).size(); }
    // The last frame on the line, as bytes.
    std::vector<unsigned char> last() const {
        const std::string all = got(line);
        if (all.size() < static_cast<std::size_t>(kRingFrameBytes)) return {};
        return std::vector<unsigned char>(all.end() - kRingFrameBytes, all.end());
    }
};

Rgb rgb(int r, int g, int b) {
    Rgb c;
    c.r = static_cast<std::uint8_t>(r), c.g = static_cast<std::uint8_t>(g), c.b = static_cast<std::uint8_t>(b);
    return c;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Rig() {
        app.session().view = View::Settings;
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
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    Session& session() { return app.session(); }
    SettingsScreen& setup() { return app.settings(); }
};

// ---- the rules ------------------------------------------------------------------------------
void testTheRules() {
    // The screen: the square of the percent, never black.
    CHECK(backlightFor(100, 2499) == 2499 && backlightFor(70, 2499) == 1225 && backlightFor(50, 2499) == 625);
    CHECK(backlightFor(kBrightLeast, 2499) == 25 && backlightFor(0, 2499) == 1 && backlightFor(150, 2499) == 2499);
    CHECK(backlightFor(70, 0) == 0);
    for (int p = kBrightLeast; p < kBrightMost; p += kBrightStep) CHECK(backlightFor(p + kBrightStep, 2499) > backlightFor(p, 2499));
    CHECK(ringLevelFor(100) == 255 && ringLevelFor(70) == 125 && ringLevelFor(kBrightLeast) == 3);
    // A theme's colour as a light: its hue, as bright as it goes.
    CHECK(fullest(0, 106, 92) == rgb(0, 255, 221) && fullest(111, 191, 163) == rgb(148, 255, 218) && fullest(0, 0, 0) == rgb(0, 0, 0));
    CHECK(fullest(255, 0, 10) == rgb(255, 0, 10));
    CHECK(dimmed(rgb(255, 128, 0), 40) == rgb(40, 20, 0) && dimmed(rgb(255, 255, 255), 256) == rgb(255, 255, 255));
    // The frame: ROCKNIX's for this handheld (mode 1, the level, sixteen R G B, the sum & 0xFF).
    RingFrame f;
    f.on = true, f.level = 200;
    for (int i = 0; i < kRingLeds; ++i) f.leds[i] = rgb(i, 100 + i, 255 - i);
    std::uint8_t bytes[kRingFrameBytes];
    ringBytes(f, bytes);
    CHECK(kRingFrameBytes == 51 && bytes[0] == 1 && bytes[1] == 200);
    unsigned sum = 1 + 200;
    for (int i = 0; i < kRingLeds; ++i) {
        CHECK(bytes[2 + 3 * i] == i && bytes[3 + 3 * i] == 100 + i && bytes[4 + 3 * i] == 255 - i);
        sum += static_cast<unsigned>(i + 100 + i + 255 - i);
    }
    CHECK(bytes[50] == (sum & 0xFF));
    // The pictures.
    const Rgb teal = rgb(0, 255, 200), brass = rgb(255, 180, 0), dim = dimmed(teal, kRingDim);
    CHECK(!ringPicture(kRingOff, 70, 3, teal, brass).on && ringPicture(kRingOff, 70, 3, teal, brass) == RingFrame());
    CHECK(!ringPicture(-1, 70, 3, teal, brass).on && !ringPicture(kRingModes, 70, 3, teal, brass).on);
    for (int step : {-1, 0, 5}) {
        const RingFrame on = ringPicture(kRingOn, 70, step, teal, brass);
        CHECK(on.on && on.level == 125);
        for (const Rgb& led : on.leds) CHECK(led == teal);
    }
    for (int mode : {kRingBeat, kRingSteps}) { // stopped: all dim
        const RingFrame stopped = ringPicture(mode, 100, -1, teal, brass);
        CHECK(stopped.on && stopped.level == 255);
        for (const Rgb& led : stopped.leds) CHECK(led == dim);
    }
    // BEAT: all lit on a beat's first step (the bar's first in brass), dim between.
    for (int step = 0; step < 16; ++step) {
        const RingFrame beat = ringPicture(kRingBeat, 70, step, teal, brass);
        const Rgb want = step == 0 ? brass : step % 4 == 0 ? teal : dim;
        for (const Rgb& led : beat.leds) CHECK(led == want);
    }
    // STEPS: the step's LED (the bar's first in brass), the rest dim.
    for (int step = 0; step < 16; ++step) {
        const RingFrame steps = ringPicture(kRingSteps, 70, step, teal, brass);
        CHECK(ringLedOf(step) == step); // (kRingStart 0, kRingTurn 1: not known on the handheld)
        for (int i = 0; i < kRingLeds; ++i) CHECK(steps.leds[i] == (i == ringLedOf(step) ? (step == 0 ? brass : teal) : dim));
    }
}

// ---- from the environment ---------------------------------------------------------------------
void testFromTheEnvironment() {
    Sys sys;
    {
        Lights none = sys.lights(nullptr, nullptr);
        CHECK(!none.screenHere() && !none.ringHere());
        none.update(5000, 40, ringPicture(kRingOn, 40, -1, rgb(0, 255, 0), rgb(255, 0, 0)));
        // Nothing touched: the stock firmware's port.
        CHECK(got(sys.bl + "/brightness") == "1250\n" && got(sys.power) == "0\n" && sys.sent() == 0);
        CHECK(none.brightnessWritten() == -1 && none.ringSends() == 0);
    }
    CHECK(!sys.lights("0", nullptr).screenHere() && !sys.lights("yes", nullptr).screenHere());
    CHECK(sys.lights("1", nullptr).screenHere() && !sys.lights("1", nullptr).ringHere());
    CHECK(sys.lights("1", nullptr).screenMax() == 2499);
    CHECK(!sys.lights(nullptr, "").ringHere());
    CHECK(!sys.lights(nullptr, (sys.root + "/no-such-line").c_str()).ringHere());
    CHECK(sys.lights(nullptr, sys.line.c_str()).ringHere() && !sys.lights(nullptr, sys.line.c_str()).screenHere());
    CHECK(sys.both().screenHere() && sys.both().ringHere());
    // The ring's power switch missing: no ring (the line alone cannot be lit).
    Sys noPower(false);
    CHECK(!noPower.both().ringHere() && noPower.both().screenHere());
    // No backlight with a max_brightness: no screen.
    put(noPower.bl + "/max_brightness", "0\n");
    CHECK(!noPower.lights("1", nullptr).screenHere());
    // Moved, it keeps its line (and the one moved from has none).
    Lights first = sys.both();
    Lights second(std::move(first));
    CHECK(second.ringHere() && !first.ringHere() && !first.screenHere());
    Lights third;
    third = std::move(second);
    CHECK(third.ringHere() && third.screenHere() && !second.ringHere());
    CHECK(Lights::pretend().screenHere() && Lights::pretend().ringHere());
}

// ---- the files ----------------------------------------------------------------------------------
void testTheFiles() {
    Sys sys;
    Lights l = sys.both();
    const Rgb teal = rgb(0, 255, 200), brass = rgb(255, 180, 0);
    std::uint32_t now = 10000;
    l.update(now, 70, ringPicture(kRingOn, 70, -1, teal, brass));
    CHECK(number(sys.bl + "/brightness") == 1225 && l.brightnessWritten() == 1225);
    CHECK(got(sys.power) == "1\n" && l.ringPowered());
    CHECK(sys.sent() == 51 && l.ringSends() == 1);
    std::uint8_t want[kRingFrameBytes];
    ringBytes(ringPicture(kRingOn, 70, -1, teal, brass), want);
    CHECK(sys.last() == std::vector<unsigned char>(want, want + kRingFrameBytes));
    // The same again: nothing written. (The brightness file put back by hand is not written again
    // either: only a change of the number is.)
    put(sys.bl + "/brightness", "77\n");
    for (int i = 0; i < 5; ++i) l.update(now += 16, 70, ringPicture(kRingOn, 70, -1, teal, brass));
    CHECK(sys.sent() == 51 && got(sys.bl + "/brightness") == "77\n");
    // ...but the frame goes again 150 ms and a second after the power came on, then no more.
    for (int i = 0; i < 10; ++i) l.update(now += 16, 70, ringPicture(kRingOn, 70, -1, teal, brass));
    CHECK(sys.sent() == 102);
    for (int i = 0; i < 70; ++i) l.update(now += 16, 70, ringPicture(kRingOn, 70, -1, teal, brass));
    CHECK(sys.sent() == 153);
    for (int i = 0; i < 300; ++i) l.update(now += 16, 70, ringPicture(kRingOn, 70, -1, teal, brass));
    CHECK(sys.sent() == 153 && l.ringSends() == 3);
    // Another brightness: the screen, and the ring's level.
    l.update(now += 16, 40, ringPicture(kRingOn, 40, -1, teal, brass));
    CHECK(number(sys.bl + "/brightness") == backlightFor(40, 2499) && sys.sent() == 204 && sys.last()[1] == ringLevelFor(40));
    // Each new frame once.
    for (int step = 0; step < 16; ++step)
        for (int i = 0; i < 3; ++i) l.update(now += 16, 40, ringPicture(kRingSteps, 40, step, teal, brass));
    CHECK(sys.sent() == 204 + 16 * 51);
    // The power key taps the screen off: within half a second the ring goes dark too, and the
    // brightness may still be written (the kernel keeps the screen off); bl_power is never written.
    put(sys.bl + "/bl_power", "4\n");
    const std::size_t before = sys.sent();
    for (int i = 0; i < 40; ++i) l.update(now += 16, 50, ringPicture(kRingSteps, 50, i % 16, teal, brass));
    CHECK(l.screenDark() && got(sys.power) == "0\n" && !l.ringPowered());
    CHECK(sys.sent() <= before + 51 * 32); // (what went before the half second was up)
    const std::size_t dark = sys.sent();
    for (int i = 0; i < 40; ++i) l.update(now += 16, 60, ringPicture(kRingSteps, 60, i % 16, teal, brass));
    CHECK(sys.sent() == dark && got(sys.bl + "/bl_power") == "4\n" && number(sys.bl + "/brightness") == backlightFor(60, 2499));
    // Back on: the ring with it, sent at once.
    put(sys.bl + "/bl_power", "0\n");
    for (int i = 0; i < 40; ++i) l.update(now += 16, 60, ringPicture(kRingOn, 60, -1, teal, brass));
    CHECK(!l.screenDark() && got(sys.power) == "1\n" && sys.sent() > dark && got(sys.bl + "/bl_power") == "0\n");
    // OFF: unpowered, nothing sent.
    for (int i = 0; i < 100; ++i) l.update(now += 16, 60, ringPicture(kRingOff, 60, -1, teal, brass));
    const std::size_t off = sys.sent();
    for (int i = 0; i < 100; ++i) l.update(now += 16, 60, ringPicture(kRingOff, 60, 3, teal, brass));
    CHECK(got(sys.power) == "0\n" && sys.sent() == off);
    // Whatever the system left, the first pass says the power: OFF from the start writes 0.
    Sys left;
    put(left.power, "1\n");
    Lights fresh = left.both();
    fresh.update(1, 70, RingFrame());
    CHECK(got(left.power) == "0\n" && left.sent() == 0);
    // A screen only: the ring's line is never opened, bl_power never read (no ring to darken).
    Sys screenOnly;
    Lights s = screenOnly.lights("1", nullptr);
    put(screenOnly.bl + "/bl_power", "4\n");
    s.update(1, 100, ringPicture(kRingOn, 100, -1, teal, brass));
    CHECK(number(screenOnly.bl + "/brightness") == 2499 && !s.screenDark() && screenOnly.sent() == 0 && got(screenOnly.power) == "0\n");
}

// ---- the SET page --------------------------------------------------------------------------------
void testTheSetPage() {
    typedef SettingsScreen S;
    {
        // Not ours (the stock firmware): the page as it was, the two rows passed by.
        Rig rig;
        S& setup = rig.setup();
        CHECK(!setup.here(S::kBrightness) && !setup.here(S::kLight) && setup.places() == 18);
        setup.goTo(S::kVolume);
        rig.tap(DOWN);
        CHECK(setup.cursorRow() == S::kStick);
        rig.tap(UP);
        CHECK(setup.cursorRow() == S::kVolume);
        setup.goTo(S::kBrightness); // (asked for by number: the next row that is here)
        CHECK(setup.cursorRow() == S::kStick);
        setup.goTo(S::kControls);
        CHECK(setup.firstShown() > 0 && setup.shown(S::kControls) && !setup.shown(S::kTempo) && setup.shown(S::kVolume) && setup.shown(S::kStick));
    }
    Sys sys;
    Rig rig;
    rig.session().useLights(sys.both());
    S& setup = rig.setup();
    CHECK(setup.here(S::kBrightness) && setup.here(S::kLight) && setup.here(S::kLightColour) && setup.places() == 21);
    // After VOLUME, among the handheld's rows.
    setup.goTo(S::kVolume);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kBrightness && rig.session().brightness() == kBrightUsual);
    rig.tap(RIGHT);
    CHECK(rig.session().brightness() == 75);
    rig.tap(LEFT, 30);
    CHECK(rig.session().brightness() == kBrightLeast);
    CHECK(number(sys.bl + "/brightness") == backlightFor(kBrightLeast, 2499));
    rig.tap(RIGHT, 30);
    CHECK(rig.session().brightness() == 100 && number(sys.bl + "/brightness") == 2499);
    rig.tap(LEFT, 6);
    CHECK(rig.session().brightness() == 70);
    // STICK LIGHT: a choice with ends, OFF ON BEAT STEPS.
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kLight && rig.session().stickLight() == kRingOn && got(sys.power) == "1\n");
    rig.tap(RIGHT);
    CHECK(rig.session().stickLight() == kRingBeat);
    rig.tap(RIGHT, 3);
    CHECK(rig.session().stickLight() == kRingSteps);
    rig.tap(LEFT, 9);
    CHECK(rig.session().stickLight() == kRingOff && got(sys.power) == "0\n");
    rig.tap(A); // A does nothing to it
    CHECK(rig.session().stickLight() == kRingOff && setup.cursorRow() == S::kLight && !setup.testing() && !setup.typing());
    // LIGHT COLOUR: a choice with ends, THEME to RAINBOW.
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kLightColour && rig.session().lightColour() == kLightTheme);
    rig.tap(RIGHT);
    CHECK(rig.session().lightColour() == kLightTeal);
    rig.tap(RIGHT, kLightColours + 2);
    CHECK(rig.session().lightColour() == kLightRainbow);
    rig.tap(LEFT, kLightColours + 2);
    CHECK(rig.session().lightColour() == kLightTheme);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kStick);
    // Eighteen rows (CLICK and COUNT IN since easywins batch 2, LIGHT COLOUR): the page shows twelve, one place at a time.
    setup.goTo(S::kTempo);
    for (int place = 1; place < setup.places(); ++place) {
        rig.tap(DOWN);
        CHECK(setup.placeOf(setup.cursorRow()) == place && setup.shown(setup.cursorRow()));
        CHECK(setup.firstShown() == 0 || !setup.fits(place, setup.firstShown() - 1)); // moved by as little as shows the cursor
    }
    // The last row in view with HANDHELD's five and SETUP's three, and no room left empty under it.
    CHECK(setup.cursorRow() == S::kControls && setup.shown(S::kVolume) && setup.shown(S::kLightColour) && !setup.fits(setup.places() - 1, setup.firstShown() - 1));
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kControls);
    for (int place = setup.firstShown(); place < setup.places(); ++place) {
        const int row = setup.rowAt(place);
        CHECK(setup.fits(place, setup.firstShown()));
        CHECK(setup.rowNowY(row) >= ui::kTopBarH + 8 && setup.rowNowY(row) + S::kRowH - 6 <= ui::kScreenH - ui::kHintsH - 4);
        if (place > setup.firstShown()) CHECK(setup.rowNowY(row) >= setup.rowNowY(setup.rowAt(place - 1)) + S::kRowH - 2);
    }
    // The places and the rows agree both ways.
    for (int row = 0; row < S::kRows; ++row) CHECK(setup.rowAt(setup.placeOf(row)) == row);
    // The lights taken away (they never are on a handheld, but the page must not stay past its end).
    const int withLights = setup.firstShown();
    rig.session().useLights(Lights());
    setup.goTo(S::kControls);
    CHECK(setup.firstShown() < withLights && setup.cursorRow() == S::kControls && !setup.fits(setup.places() - 1, setup.firstShown() - 1));
}

// ---- kept in settings.txt --------------------------------------------------------------------------
void testKept() {
    const std::string folder = tempFolder("lightset");
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().brightness() == kBrightUsual && rig.session().stickLight() == kRingOn); // (nothing kept: as built)
        rig.session().setBrightness(43); // (in fives)
        CHECK(rig.session().brightness() == 40);
        rig.session().setStickLight(kRingBeat);
        rig.session().setStickLight(kRingModes + 3);
        CHECK(rig.session().stickLight() == kRingSteps);
        rig.session().setStickLight(kRingBeat);
        rig.session().setBrightness(-5);
        CHECK(rig.session().brightness() == kBrightLeast);
        rig.session().setBrightness(40);
        const std::string text = got(folder + "/settings.txt");
        CHECK(text.find("\nbrightness 40\n") != std::string::npos && text.find("\nlight 2\n") != std::string::npos);
        CHECK(text.find("theme ") < text.find("brightness ") && text.find("brightness ") < text.find("light ")); // (appended)
    }
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().brightness() == 40 && rig.session().stickLight() == kRingBeat);
    }
    // Numbers out of range are not taken.
    put(folder + "/settings.txt", "brightness 5\nlight 4\n");
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().brightness() == kBrightUsual && rig.session().stickLight() == kRingOn);
    }
    put(folder + "/settings.txt", "brightness 105\nlight -1\n");
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().brightness() == kBrightUsual && rig.session().stickLight() == kRingOn);
    }
    put(folder + "/settings.txt", "brightness 10\nlight 0\n");
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().brightness() == 10 && rig.session().stickLight() == kRingOff);
    }
}

// ---- MENU + a volume key -----------------------------------------------------------------------------
void testTheVolumeKeys() {
    {
        // Not ours: a volume key is the volume, MENU held or not.
        Rig rig;
        rig.session().setVolume(0.5f);
        rig.key(MENU, true);
        rig.frames(30);
        CHECK(rig.app.helpShowing());
        rig.app.volumeKey(true, rig.input);
        rig.frames(1);
        CHECK(std::abs(rig.session().volume() - 0.6f) < 1e-4f && rig.session().brightness() == kBrightUsual && !rig.app.brightnessShowing());
        CHECK(rig.app.volumeShowing() && rig.app.helpShowing());
        rig.key(MENU, false);
        rig.frames(1);
    }
    Sys sys;
    Rig rig;
    rig.session().useLights(sys.both());
    rig.session().setVolume(0.5f);
    rig.app.volumeKey(false, rig.input); // no MENU: the volume
    rig.frames(1);
    CHECK(std::abs(rig.session().volume() - 0.4f) < 1e-4f && rig.app.volumeShowing() && !rig.app.brightnessShowing());
    rig.frames(120);
    rig.key(MENU, true);
    rig.frames(30);
    CHECK(rig.app.helpShowing());
    rig.app.volumeKey(true, rig.input);
    rig.frames(1);
    CHECK(rig.session().brightness() == kBrightUsual + kBrightKeyStep && rig.app.brightnessShowing() && std::abs(rig.session().volume() - 0.4f) < 1e-4f);
    CHECK(!rig.app.helpShowing()); // the screen itself is judged: the card is gone until MENU is let go
    CHECK(number(sys.bl + "/brightness") == backlightFor(kBrightUsual + kBrightKeyStep, 2499));
    rig.frames(30);
    CHECK(!rig.app.helpShowing());
    for (int i = 0; i < 6; ++i) rig.app.volumeKey(false, rig.input);
    CHECK(rig.session().brightness() == kBrightUsual - 5 * kBrightKeyStep);
    for (int i = 0; i < 10; ++i) rig.app.volumeKey(false, rig.input);
    CHECK(rig.session().brightness() == kBrightLeast);
    rig.frames(120);
    CHECK(!rig.app.brightnessShowing() && !rig.app.volumeShowing());
    rig.key(MENU, false);
    rig.frames(1);
    rig.key(MENU, true);
    rig.frames(30);
    CHECK(rig.app.helpShowing()); // MENU again: the card, as always
    rig.key(MENU, false);
    rig.frames(1);
}

// ---- the ring follows the song --------------------------------------------------------------------
void testTheRingFollowsTheSong() {
    Sys sys;
    Rig rig;
    rig.session().useLights(sys.both());
    rig.session().setStickLight(kRingSteps);
    rig.session().setTheme(kThemePaper);
    rig.frames(2);
    // (LIGHT COLOUR at THEME: PAPER's ring colour is its amber, and beside amber a bar's first step is white.)
    const Rgb teal = ringThemeColour(kThemePaper), brass = lightColourOf(kLightWhite), dim = dimmed(teal, kRingDim);
    CHECK(teal == rgb(255, 130, 40) && ringFirstFor(teal) == brass);
    std::vector<unsigned char> last = sys.last();
    CHECK(last.size() == 51);
    for (int i = 0; i < kRingLeds; ++i) CHECK(last[static_cast<std::size_t>(2 + 3 * i)] == dim.r && last[static_cast<std::size_t>(3 + 3 * i)] == dim.g);
    rig.session().togglePlay(false);
    int stepsSeen = 0, lastStep = -1;
    for (int i = 0; i < 400; ++i) {
        rig.frames(1);
        const int step = rig.app.project().playStep;
        if (step < 0 || step == lastStep) continue;
        lastStep = step;
        ++stepsSeen;
        last = sys.last();
        for (int led = 0; led < kRingLeds; ++led) {
            const Rgb want = led == ringLedOf(step) ? (step == 0 ? brass : teal) : dim;
            CHECK(last[static_cast<std::size_t>(2 + 3 * led)] == want.r && last[static_cast<std::size_t>(3 + 3 * led)] == want.g &&
                  last[static_cast<std::size_t>(4 + 3 * led)] == want.b);
        }
    }
    CHECK(stepsSeen >= 16);
    rig.session().togglePlay(false);
    rig.frames(4);
    last = sys.last();
    for (int i = 0; i < kRingLeds; ++i) CHECK(last[static_cast<std::size_t>(2 + 3 * i)] == dim.r && last[static_cast<std::size_t>(4 + 3 * i)] == dim.b);

    // A ring with no screen of the program's (a system that gives it the light alone) still follows.
    Sys ringOnly;
    Rig other;
    other.session().useLights(ringOnly.lights(nullptr, ringOnly.line.c_str()));
    other.session().setStickLight(kRingBeat);
    other.frames(2);
    CHECK(!other.session().screenHere() && other.session().ringHere() && got(ringOnly.power) == "1\n" && ringOnly.sent() == 51);
    CHECK(got(ringOnly.bl + "/brightness") == "1250\n"); // (the screen left alone)
}

// ---- STEPS once round a SCENE, and LIGHT COLOUR (easywins) -------------------------------------------
void testScenesAndColours() {
    const Rgb teal = rgb(0, 255, 200), brass = rgb(255, 180, 0);
    Rgb one[kRingLeds];
    for (Rgb& c : one) c = teal;
    // A scene of four bars: a LED every four steps, the lit one the share played; brass on a bar's first step.
    for (int step = 0; step < 64; ++step) {
        RingPlace at;
        at.step = step % 16, at.sceneStep = step, at.sceneSteps = 64;
        const RingFrame f = ringPicture(kRingSteps, 70, at, one, brass);
        for (int i = 0; i < kRingLeds; ++i)
            CHECK(f.leds[i] == (i == ringLedOf(step / 4) ? (step % 16 == 0 ? brass : teal) : dimmed(teal, kRingDim)));
    }
    // A scene cut to 24 steps: the sixteen LEDs share them (step 23 is the last LED; step 16, a bar's first, in brass).
    {
        RingPlace at;
        at.step = 7, at.sceneStep = 23, at.sceneSteps = 24;
        CHECK(ringPicture(kRingSteps, 70, at, one, brass).leds[ringLedOf(15)] == teal);
        at.step = 0, at.sceneStep = 16;
        CHECK(ringPicture(kRingSteps, 70, at, one, brass).leds[ringLedOf(10)] == brass);
    }
    // A one-bar scene is as it was: the old picture to the LED.
    for (int step = 0; step < 16; ++step) {
        RingPlace at;
        at.step = at.sceneStep = step;
        CHECK(ringPicture(kRingSteps, 70, at, one, brass) == ringPicture(kRingSteps, 70, step, teal, brass));
        CHECK(ringPicture(kRingBeat, 70, at, one, brass) == ringPicture(kRingBeat, 70, step, teal, brass));
    }
    // The colours: THEME is the theme's; the premade ones their own; TRACK a hue a track; RAINBOW a hue a LED, turning a LED a beat.
    Rgb c[kRingLeds];
    ringColours(kLightTheme, teal, 3, -1, c);
    for (const Rgb& x : c) CHECK(x == teal);
    for (int which = kLightTeal; which <= kLightWhite; ++which) {
        ringColours(which, teal, 3, -1, c);
        for (const Rgb& x : c) CHECK(x == lightColourOf(which) && (x.r | x.g | x.b) != 0);
        for (int other = kLightTeal; other < which; ++other) CHECK(lightColourOf(other) != lightColourOf(which));
    }
    ringColours(kLightTrack, teal, 0, -1, c);
    CHECK(c[0] == rgb(255, 0, 0) && c[15] == rgb(255, 0, 0));
    ringColours(kLightTrack, teal, 4, -1, c);
    CHECK(c[0] == rgb(0, 255, 0) && c[9] == rgb(0, 255, 0));
    ringColours(kLightRainbow, teal, 0, -1, c);
    CHECK(c[0] == rgb(255, 0, 0) && c[4] == hueRgb(384) && c[8] == rgb(0, 255, 255));
    Rgb turned[kRingLeds];
    ringColours(kLightRainbow, teal, 0, 3, turned);
    for (int i = 0; i < kRingLeds; ++i) CHECK(turned[i] == c[(i + 3) % kRingLeds]);
    CHECK(hueRgb(0) == rgb(255, 0, 0) && hueRgb(256) == rgb(255, 255, 0) && hueRgb(1024) == rgb(0, 0, 255) && hueRgb(kHueRound) == hueRgb(0) && hueRgb(-256) == hueRgb(1280));
    // THEME's own colours: BROWN's is BRASS (the owner's), each theme's its own, a number out of range BROWN's.
    CHECK(ringThemeColour(kThemeBrown) == lightColourOf(kLightBrass) && ringThemeColour(99) == ringThemeColour(kThemeBrown) && ringThemeColour(-1) == ringThemeColour(0));
    for (int a = 0; a < kThemes; ++a)
        for (int b = 0; b < a; ++b) CHECK(ringThemeColour(a) != ringThemeColour(b));
    // A bar's first step: brass, or white where the ring is too near brass to tell.
    const Rgb white = lightColourOf(kLightWhite), brassSet = lightColourOf(kLightBrass);
    for (int which : {kLightBrass, kLightOrange, kLightYellow}) CHECK(ringFirstFor(lightColourOf(which)) == white);
    for (int which : {kLightTeal, kLightPurple, kLightRed, kLightGreen, kLightBlue, kLightPink, kLightWhite}) CHECK(ringFirstFor(lightColourOf(which)) == brassSet);
    CHECK(ringFirstFor(ringThemeColour(kThemeBrown)) == white && ringFirstFor(ringThemeColour(kThemeBlue)) == brassSet && ringFirstFor(ringThemeColour(kThemeBlack)) == brassSet);
    // ON in RAINBOW: every LED its own hue; dim ones dimmed each by its own.
    ringColours(kLightRainbow, teal, 0, -1, c);
    RingPlace stopped;
    const RingFrame on = ringPicture(kRingOn, 70, stopped, c, brass), dim = ringPicture(kRingSteps, 70, stopped, c, brass);
    for (int i = 0; i < kRingLeds; ++i) CHECK(on.leds[i] == c[i] && dim.leds[i] == dimmed(c[i], kRingDim));

    // Through the Session: a two-bar scene goes round once in two bars; LIGHT COLOUR kept in settings.txt.
    const std::string folder = tempFolder("lightcolour");
    {
        Sys sys;
        Rig rig;
        rig.session().useLights(sys.both());
        rig.session().useProgramFolder(folder);
        rig.session().setStickLight(kRingSteps);
        rig.session().setLightColour(kLightRed);
        rig.session().setCellBars(0, 0, 2);
        rig.frames(2);
        rig.session().togglePlay(false);
        int seen = 0, lastPlace = -1;
        for (int i = 0; i < 900; ++i) { // (a step is 23 frames here: 256 samples a frame)
            rig.frames(1);
            const Project& p = rig.app.project();
            if (p.playStep < 0) continue;
            const int place = p.playBar * 16 + p.playStep;
            if (place == lastPlace) continue;
            lastPlace = place;
            ++seen;
            const std::vector<unsigned char> last = sys.last();
            const int lit = ringLedOf(place / 2);
            const Rgb want = place % 16 == 0 ? lightColourOf(kLightBrass) : rgb(255, 0, 0);
            CHECK(last.size() == 51 && last[static_cast<std::size_t>(2 + 3 * lit)] == want.r && last[static_cast<std::size_t>(3 + 3 * lit)] == want.g &&
                  last[static_cast<std::size_t>(4 + 3 * lit)] == want.b);
            const int other = (lit + 8) % kRingLeds;
            CHECK(last[static_cast<std::size_t>(2 + 3 * other)] == dimmed(rgb(255, 0, 0), kRingDim).r);
        }
        CHECK(seen >= 33); // round the scene and on into its next time
        rig.session().togglePlay(false);
        const std::string text = got(folder + "/settings.txt");
        CHECK(text.find("\nlightcolour 4\n") != std::string::npos);
        rig.session().setLightColour(kLightTheme);
        CHECK(got(folder + "/settings.txt").find("lightcolour") == std::string::npos); // (left out at THEME)
        rig.session().setLightColour(kLightRainbow);
    }
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().lightColour() == kLightRainbow);
    }
    put(folder + "/settings.txt", "lightcolour 13\n");
    {
        Rig rig;
        rig.session().useProgramFolder(folder);
        rig.session().loadSettings();
        CHECK(rig.session().lightColour() == kLightTheme);
    }
}

// ---- LIGHT COLOUR at TRACK: each head on SONG wears its track's colour (owner's ask B) ---------------
void testTrackColours() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const auto hueOf = [](int track) {
        const Rgb c = ringTrackColour(track);
        return SDL_Color{c.r, c.g, c.b, 255};
    };
    // The twelve are all different, track 1 red, and the ring's TRACK colour is the same one.
    for (int a = 0; a < kNumTracks; ++a)
        for (int b = 0; b < a; ++b) CHECK(ringTrackColour(a) != ringTrackColour(b));
    Rgb ring[kRingLeds];
    ringColours(kLightTrack, rgb(1, 2, 3), 7, -1, ring);
    CHECK(ringTrackColour(0) == rgb(255, 0, 0) && ring[5] == ringTrackColour(7));
    for (int tracks : {2, kNumTracks}) {
        Sys sys;
        Rig rig;
        Session& session = rig.session();
        session.useLights(sys.both());
        while (rig.app.project().trackCount() < tracks) session.addTrack(TrackKind::Phazed);
        session.view = View::Song;
        session.setMuted(tracks - 2, true);
        session.setSolo(tracks - 1, true);
        session.setMix(0, -6, 10); // (the mixer's mark on track 1)
        typedef SongScreen G;
        // How many pixels of each head's foot are the track's colour: the whole bar, or none.
        const auto marked = [&](int track) {
            rig.frames(1);
            ui::useTheme(session.theme());
            canvas.clear();
            rig.app.draw(canvas.r, *canvas.fonts);
            const SDL_Rect head = rig.app.song().headRect(track);
            int n = 0;
            for (int y = head.y; y < head.y + head.h; ++y)
                for (int x = head.x; x < head.x + head.w; ++x) n += canvas.is(x, y, hueOf(track)) ? 1 : 0;
            // Where it is: the foot of the head, and nowhere else in it.
            const int top = head.y + head.h - G::kHueMarkFoot - G::kHueMarkH;
            int inBar = 0;
            for (int y = top; y < top + G::kHueMarkH; ++y) inBar += canvas.run(head.x + G::kHueMarkInset, y, hueOf(track));
            CHECK(n == inBar);
            return n;
        };
        for (int theme : {static_cast<int>(kThemeBrown), static_cast<int>(kThemePaper)}) {
            session.setTheme(theme);
            session.setStickLight(kRingOn);
            session.setLightColour(kLightTrack);
            for (int t = 0; t < tracks; ++t) {
                const SDL_Rect head = rig.app.song().headRect(t);
                CHECK(marked(t) == (head.w - 2 * G::kHueMarkInset) * G::kHueMarkH); // muted and soloed heads too, as bright
                // A line of the dim lettering over it (its edge on a light theme).
                CHECK(canvas.run(head.x + G::kHueMarkInset, head.y + head.h - G::kHueMarkFoot - G::kHueMarkH - 1, ui::kDim) >= head.w - 2 * G::kHueMarkInset);
                // Clear of the marks at the head's top: the M / S box and the mixer's square end above it.
                const int markFoot = head.y + 2 + static_cast<int>(std::ceil(canvas.fonts->fixed().small.capHeight())) + 4;
                CHECK(markFoot < head.y + head.h - G::kHueMarkFoot - G::kHueMarkH - 1 && head.y + G::kMixMarkInset + G::kMixMarkSize < markFoot + 20);
            }
            // The M and S marks and the mixer's are still drawn (brass on the first head's corner).
            const SDL_Rect first = rig.app.song().headRect(0);
            CHECK(canvas.is(first.x + G::kMixMarkInset + 1, first.y + G::kMixMarkInset + 1, ui::kBrass));
            // Only at TRACK, and only with the ring lit.
            session.setLightColour(kLightRed);
            CHECK(marked(0) == 0 && marked(tracks - 1) == 0);
            session.setLightColour(kLightRainbow);
            CHECK(marked(0) == 0);
            session.setLightColour(kLightTrack);
            session.setStickLight(kRingOff);
            CHECK(marked(0) == 0 && !session.trackColoursShown());
            session.setStickLight(kRingSteps);
            CHECK(marked(1) > 0 && session.trackColoursShown());
        }
        // No ring on this handheld (the stock firmware): never, whatever settings.txt says.
        Rig stock;
        stock.session().view = View::Song;
        stock.session().setLightColour(kLightTrack);
        stock.frames(1);
        canvas.clear();
        stock.app.draw(canvas.r, *canvas.fonts);
        const SDL_Rect head = stock.app.song().headRect(0);
        int n = 0;
        for (int y = head.y; y < head.y + head.h; ++y)
            for (int x = head.x; x < head.x + head.w; ++x) n += canvas.is(x, y, hueOf(0)) ? 1 : 0;
        CHECK(n == 0 && !stock.session().trackColoursShown());
    }
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheRules", testTheRules}, {"testFromTheEnvironment", testFromTheEnvironment}, {"testTheFiles", testTheFiles},
        {"testTheSetPage", testTheSetPage}, {"testKept", testKept}, {"testTheVolumeKeys", testTheVolumeKeys},
        {"testTheRingFollowsTheSong", testTheRingFollowsTheSong}, {"testScenesAndColours", testScenesAndColours}, {"testTrackColours", testTrackColours},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("lights_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
