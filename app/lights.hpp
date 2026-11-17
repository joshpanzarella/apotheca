#pragma once
// The screen's brightness and the light ring round the stick, when Apotheca is the whole system (our own
// firmware for the RG40XXV: os/, docs/plans/lights-plan.md). No SDL here, so a test needs none.
//
// The hardware, as the firmware's device tree and kernel make it (read 2026-11-17):
//
//   the screen  a PWM backlight, /sys/class/backlight/backlight: `brightness` 0 .. `max_brightness`
//               (2499; 1250 at boot), and `bl_power`, 0 on, 4 off. apotheca-power sets bl_power on a
//               tap of the power key; this file NEVER writes it. The kernel keeps a screen at bl_power
//               4 dark whatever `brightness` says (backlight_get_brightness), so a brightness written
//               while the power key has the screen off does not light it.
//   the ring    sixteen RGB LEDs behind a small controller on the SoC's UART5 (/dev/ttyS2 on our
//               kernel), 115200 baud. Its power is a plain GPIO, PI7, which gpio-leds gives as
//               /sys/class/leds/rgb:kbd_backlight: `brightness` 0 or 1 (on or off: max_brightness 1).
//               The colours go as one frame of 51 bytes: 1 (the mode: ROCKNIX sends 1, steady; the
//               controller's other modes are not known), the level 0..255, then R G B for each of the
//               sixteen, then the sum of every byte before it, & 0xFF. (ROCKNIX's
//               analog_sticks_ledcontrol for this handheld, which gives all sixteen one colour.
//               NOT TRIED HERE: which LED is which, and whether the order is R G B, are unknown.)
//
// The system says what is the program's in two environment variables (os/.../usr/libexec/apotheca/run):
//
//   APOTHECA_OWNS_SCREEN=1           the backlight: the first folder under /sys/class/backlight
//   APOTHECA_STICK_LIGHT=/dev/ttyS2  the ring's line; its power is /sys/class/leds/rgb:kbd_backlight
//
// The port's launcher (scripts/Apotheca.sh) sets neither: on the stock firmware nothing here touches a
// file, and the SET page does not show BRIGHTNESS or STICK LIGHT.

#include <cstdint>
#include <cstdlib>
#include <string>

namespace apotheca {

// ---- the brightness ---------------------------------------------------------------------------
// A percent on the SET page, kBrightLeast to 100 in fives; MENU + a volume key moves it ten.
constexpr int kBrightLeast = 10, kBrightMost = 100, kBrightStep = 5, kBrightKeyStep = 10, kBrightUsual = 70;
// The backlight's number for a percent: the square (as eyes see a light), at least 1 so no setting is
// black. 70% is 1225 of 2499, about where the system starts (1250); 10% is 25.
inline int backlightFor(int percent, int max) {
    if (max <= 0) return 0;
    const double part = static_cast<double>(percent) / 100.0;
    const int value = static_cast<int>(part * part * max + 0.5);
    return value < 1 ? 1 : (value > max ? max : value);
}
// The ring's level byte for the same percent: the ring dims with the screen (lights-plan.md, call 4).
inline int ringLevelFor(int percent) { return backlightFor(percent, 255); }

// ---- the ring ---------------------------------------------------------------------------------
constexpr int kRingLeds = 16, kRingFrameBytes = 2 + 3 * kRingLeds + 1;
// STICK LIGHT's choices. The numbers are kept in settings.txt (`light N`): appended, never reordered.
enum RingMode { kRingOff, kRingOn, kRingBeat, kRingSteps, kRingModes };
// Which LED a step lights in STEPS: (kRingStart + kRingTurn * step) round the sixteen. Not known on the
// handheld: if the step goes round the wrong way, or starts elsewhere than the top, these two change.
constexpr int kRingStart = 0, kRingTurn = 1;
// The LEDs that are not lit in BEAT and STEPS (and all of them while the song is stopped): this much of
// the colour, out of 256.
constexpr int kRingDim = 40;

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};
// A theme's colour as a light: as bright as it goes, the same hue (PAPER's dark teal is a full teal on
// the ring). Black stays black.
inline Rgb fullest(int r, int g, int b) {
    const int most = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (most <= 0) return Rgb();
    Rgb c;
    c.r = static_cast<std::uint8_t>((r * 255 + most / 2) / most);
    c.g = static_cast<std::uint8_t>((g * 255 + most / 2) / most);
    c.b = static_cast<std::uint8_t>((b * 255 + most / 2) / most);
    return c;
}
inline Rgb dimmed(Rgb c, int part) {
    c.r = static_cast<std::uint8_t>((c.r * part + 128) >> 8);
    c.g = static_cast<std::uint8_t>((c.g * part + 128) >> 8);
    c.b = static_cast<std::uint8_t>((c.b * part + 128) >> 8);
    return c;
}

// What the ring shows: powered or not, its level and each LED.
struct RingFrame {
    bool on = false;
    int level = 0; // 0..255
    Rgb leds[kRingLeds];
    bool operator==(const RingFrame& o) const {
        if (on != o.on || level != o.level) return false;
        for (int i = 0; i < kRingLeds; ++i)
            if (leds[i] != o.leds[i]) return false;
        return true;
    }
    bool operator!=(const RingFrame& o) const { return !(*this == o); }
};

inline int ringLedOf(int step) { return ((kRingStart + kRingTurn * step) % kRingLeds + kRingLeds) % kRingLeds; }

// The ring for STICK LIGHT `mode` at BRIGHTNESS `percent`, the song at `step` of its bar (0..15; -1
// stopped). `colour` is the theme's cursor colour; `first` the bar's first step (the theme's brass).
//   OFF    unpowered
//   ON     all sixteen in the colour, playing or not
//   BEAT   all sixteen flash for the first step of each beat (the bar's first in `first`); dim between,
//          and dim while stopped
//   STEPS  the step's LED lit (the bar's first in `first`), the rest dim; all dim while stopped
inline RingFrame ringPicture(int mode, int percent, int step, Rgb colour, Rgb first) {
    RingFrame f;
    if (mode <= kRingOff || mode >= kRingModes) return f;
    f.on = true;
    f.level = ringLevelFor(percent);
    const Rgb dim = dimmed(colour, kRingDim);
    for (Rgb& led : f.leds) led = mode == kRingOn ? colour : dim;
    if (step < 0 || mode == kRingOn) return f;
    const Rgb lit = step == 0 ? first : colour;
    if (mode == kRingBeat && step % 4 == 0)
        for (Rgb& led : f.leds) led = lit;
    if (mode == kRingSteps) f.leds[ringLedOf(step)] = lit;
    return f;
}

// The 51 bytes the ring's controller is sent for a frame (which must be on).
inline void ringBytes(const RingFrame& f, std::uint8_t out[kRingFrameBytes]) {
    out[0] = 1; // steady
    out[1] = static_cast<std::uint8_t>(f.level < 0 ? 0 : (f.level > 255 ? 255 : f.level));
    for (int i = 0; i < kRingLeds; ++i) {
        out[2 + 3 * i] = f.leds[i].r;
        out[3 + 3 * i] = f.leds[i].g;
        out[4 + 3 * i] = f.leds[i].b;
    }
    unsigned sum = 0;
    for (int i = 0; i < kRingFrameBytes - 1; ++i) sum += out[i];
    out[kRingFrameBytes - 1] = static_cast<std::uint8_t>(sum & 0xFF);
}

// The files, written only on the program's own thread, once a frame at most, and only when what they
// should say has changed.
class Lights {
public:
    Lights() = default; // nothing is the program's
    ~Lights();
    Lights(const Lights&) = delete;
    Lights& operator=(const Lights&) = delete;
    Lights(Lights&& o) noexcept { take(o); }
    Lights& operator=(Lights&& o) noexcept;

    // From the environment, as the program starts. `sys` is where sysfs is: "/sys" on the handheld, a
    // made-up folder in the tests. The serial line is opened here: one that will not open is not here.
    template <typename Get>
    static Lights from(Get get, const std::string& sys = "/sys") {
        Lights l;
        const char* screen = get("APOTHECA_OWNS_SCREEN");
        if (screen && std::string(screen) == "1") l.findBacklight(sys);
        const char* line = get("APOTHECA_STICK_LIGHT");
        if (line && *line) l.openRing(line, sys + "/class/leds/rgb:kbd_backlight/brightness");
        return l;
    }
    static Lights fromEnvironment() { return from([](const char* name) { return std::getenv(name); }); }
    // Both here and nothing written: the stills of the two rows.
    static Lights pretend() {
        Lights l;
        l.pretend_ = true;
        return l;
    }

    bool screenHere() const { return pretend_ || !backlight_.empty(); }
    bool ringHere() const { return pretend_ || fd_ >= 0; }
    int screenMax() const { return max_; }

    // Once a frame: the screen at `percent` (written when the number changes), bl_power read every
    // kDarkEveryMs, and the ring as `wanted` (dark while the screen is: the power key's tap is a pocket).
    static constexpr std::uint32_t kDarkEveryMs = 500;
    // After the ring is powered its controller may not be listening yet: the frame goes again this long
    // after (not known on the handheld; ROCKNIX sends at once).
    static constexpr std::uint32_t kResendMs[2] = {150, 1000};
    void update(std::uint32_t nowMs, int percent, const RingFrame& wanted);

    // What was done, for the tests.
    bool screenDark() const { return dark_; }
    int brightnessWritten() const { return written_; } // -1: none yet
    unsigned ringSends() const { return sends_; }
    bool ringPowered() const { return powered_; }

private:
    void take(Lights& o);
    void findBacklight(const std::string& sys);
    void openRing(const std::string& line, const std::string& power);

    bool pretend_ = false;
    std::string backlight_; // its folder; empty: not the program's
    int max_ = 0, written_ = -1;
    bool dark_ = false;
    std::uint32_t darkReadAt_ = 0;
    bool darkRead_ = false;
    int fd_ = -1;             // the ring's line; -1: not the program's
    std::string power_;       // the ring's power switch
    bool powered_ = false, powerKnown_ = false;
    RingFrame sent_;          // what the ring was last sent whole
    bool sentAny_ = false;
    std::uint32_t poweredAt_ = 0;
    int resends_ = 0;         // of kResendMs, still to come
    unsigned sends_ = 0;
};

} // namespace apotheca
