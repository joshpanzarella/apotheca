/* apotheca-power: the power key and the battery, on Apotheca's own system
 * for the RG40XXV (firmware plan, batch 4). /etc/inittab starts it, and
 * starts it again if it ever stops.
 *
 * It turns the handheld off only through `poweroff`, so there is one way
 * down: BusyBox init runs /etc/init.d/rcK, whose S95apotheca stops Apotheca
 * first (TERM: it saves its song, as quitting does), then the card is
 * synced and unmounted, then the AXP717 cuts the power.
 *
 *   The power key, a short press (let go before LONG_MS)
 *       The screen off: the backlight off, and every joypad held
 *       (EVIOCGRAB), so nothing pressed in a pocket reaches Apotheca. The
 *       song plays on and the volume keys still work. Another short press:
 *       the screen back, the joypad let go.
 *       With "power-key sleep" in the config: suspend instead (the kernel's
 *       "mem", which on the H700 is s2idle). UNTRIED ON THE HANDHELD:
 *       suspend on the H700 is young in mainline; it is off unless asked for.
 *   The power key, held LONG_MS (2 s)
 *       The backlight off at once (the sign that it was heard), then
 *       poweroff. The AXP717 cuts the power by itself if the key is held
 *       much longer (about 10 s on stock), whatever this does.
 *   The battery, read every POLL seconds (20)
 *       Not on a charger, and at or under the low percent (5) or under
 *       3.25 V, on two reads in a row: poweroff. (The AXP717's own cut-off,
 *       later, saves nothing.)
 *   The screen (firmware plan, section 15): never running behind a dark one
 *       Nothing here can see the picture, so the owner's hands say it: a
 *       device tree (a screen's kind) counts as SEEN WORKING once a joypad
 *       button is pressed while it runs, and its name is kept in
 *       /data/apotheca/screen-seen. Until then, screen-check seconds (300)
 *       after the start with no button: poweroff. And 60 s after the start
 *       with no backlight or no display at all in /sys: poweroff.
 *       Only where the device tree names itself (rocknix-dt-id: the
 *       handheld, not qemu).
 *   The headphone socket (firmware plan, section 18): one or the other
 *       The codec has one output for the speaker's amplifier and the
 *       socket both. The kernel turns the speaker off while headphones are
 *       in (patch 0172: the jack's "Speaker" pin); this is the belt. The
 *       jack is an input device with the switch SW_HEADPHONE_INSERT: its
 *       state is read when it is found (EVIOCGSW), at every event and at
 *       every battery read, and the speaker's switch set to match, by
 *       /usr/libexec/apotheca/speaker on|off ("Speaker Switch" alone: no
 *       volume is touched).
 *   The green light: Linux blinks it from the start (the device tree's
 *       heartbeat); it is made steady here once the screen counts as seen.
 *
 * The config, /data/apotheca/power.txt, lines "WORD VALUE", read at the
 * start and at every battery read (a missing file is the defaults):
 *   low-battery PERCENT     0 to 50; 0: never off for the battery
 *   power-key screen|sleep  what a short press does (screen)
 *   screen-check SECONDS    0 to 3600; 0: never off for a screen not yet
 *                           seen working (300)
 *   headphones auto|ignore|inverted
 *                           auto: headphones in, the speaker off (the usual)
 *                           ignore: the speaker always on, as before
 *                           inverted: for a socket that reads the wrong way
 *                           round (the speaker goes off when they come OUT)
 * (Its idle-off MINUTES line is Apotheca's own: /usr/libexec/apotheca/run
 * hands it to the program.)
 *
 * Everything it does goes to /data/apotheca/logs/power.log (opened and
 * closed for each line, so it never keeps the card busy), the important
 * lines to the console too.
 *
 * Options, for the desk test (os/tools/power-test.py):
 *   --sys DIR      where /sys is: class/input, class/power_supply,
 *                  class/backlight, power/state (default /sys)
 *   --dev DIR      where the input devices' nodes are (default /dev/input)
 *   --config FILE  (default /data/apotheca/power.txt)
 *   --log FILE     (default /data/apotheca/logs/power.log)
 *   --off CMD      what turns it off, run by /bin/sh (default poweroff)
 *   --poll S       seconds between battery reads (default 20)
 *   --long-ms MS   a long press (default 2000)
 *   --console FILE where the important lines also go (default /dev/console)
 *   --tree FILE    the device tree's own name (default
 *                  /proc/device-tree/rocknix-dt-id)
 *   --seen FILE    the tree last seen working (default /data/apotheca/screen-seen)
 *   --find-screen-s S  when to look for a backlight and a display (default 60)
 *   --speaker CMD  what switches the speaker, run by /bin/sh with " on" or
 *                  " off" after it (default /usr/libexec/apotheca/speaker)
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { kMostDevices = 32 };

static const char* sysDir = "/sys";
static const char* devDir = "/dev/input";
static const char* configFile = "/data/apotheca/power.txt";
static const char* logFile = "/data/apotheca/logs/power.log";
static const char* offCommand = "poweroff";
static const char* consoleFile = "/dev/console";
static int pollSeconds = 20;
static long longMs = 2000;

/* The config */
static int lowPercent = 5;
static int keySleeps = 0;
static int screenCheckSeconds = 300;
static const char* treeFile = "/proc/device-tree/rocknix-dt-id";
static const char* seenFile = "/data/apotheca/screen-seen";
static int findScreenSeconds = 60;
static const char* speakerCommand = "/usr/libexec/apotheca/speaker";
enum { kJackAuto, kJackIgnore, kJackInverted };
static int jackRule = kJackAuto;
static const long kLowMicrovolts = 3250000; /* a backstop under the gauge (guess: not measured) */

/* ---- saying what happens ---------------------------------------------- */

static double upSeconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static long nowMs(void) { return (long)(upSeconds() * 1000.0); }

/* A line in the log; with loud set, on the console too. */
static void say(int loud, const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    FILE* f = fopen(logFile, "a");
    if (f) {
        fprintf(f, "[%.2f] %s\n", upSeconds(), line);
        fclose(f);
    }
    if (loud) {
        FILE* c = fopen(consoleFile, "w");
        if (c) {
            fprintf(c, "apotheca-power: %s\n", line);
            fclose(c);
        }
    }
}

/* ---- small file helpers ------------------------------------------------ */

static int readLine(const char* path, char* out, size_t size) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    const int ok = fgets(out, (int)size, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    out[strcspn(out, "\r\n")] = 0;
    return 1;
}

static int readNumber(const char* path, long* out) {
    char text[64];
    if (!readLine(path, text, sizeof text)) return 0;
    char* end = NULL;
    errno = 0;
    const long v = strtol(text, &end, 10);
    if (end == text || errno) return 0;
    *out = v;
    return 1;
}

static int writeText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    const int ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

static void readConfig(void) {
    lowPercent = 5;
    keySleeps = 0;
    screenCheckSeconds = 300;
    jackRule = kJackAuto;
    FILE* f = fopen(configFile, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char word[32], value[32];
        if (sscanf(line, "%31s %31s", word, value) != 2 || word[0] == '#') continue;
        if (strcmp(word, "low-battery") == 0) {
            char* end = NULL;
            const long v = strtol(value, &end, 10);
            if (end != value && *end == 0 && v >= 0 && v <= 50) lowPercent = (int)v;
        } else if (strcmp(word, "power-key") == 0) {
            keySleeps = strcmp(value, "sleep") == 0;
        } else if (strcmp(word, "screen-check") == 0) {
            char* end = NULL;
            const long v = strtol(value, &end, 10);
            if (end != value && *end == 0 && v >= 0 && v <= 3600) screenCheckSeconds = (int)v;
        } else if (strcmp(word, "headphones") == 0) {
            jackRule = strcmp(value, "ignore") == 0 ? kJackIgnore : strcmp(value, "inverted") == 0 ? kJackInverted : kJackAuto;
        }
    }
    fclose(f);
}

/* ---- the input devices ------------------------------------------------- */

struct Device {
    char name[32]; /* event3 */
    int fd;
    int power;     /* reports KEY_POWER */
    int pad;       /* a joypad: held while the screen is off */
    int jack;      /* the headphone socket: reports SW_HEADPHONE_INSERT */
    int grabbed;
};
static struct Device devices[kMostDevices];
static int deviceCount = 0;

/* Bit `bit` of a sysfs capability bitmap ("hex words, most significant
 * first", each word an unsigned long). */
static int hasBit(const char* bitmap, int bit) {
    unsigned long words[64];
    int n = 0;
    char copy[1024];
    snprintf(copy, sizeof copy, "%s", bitmap);
    for (char* w = strtok(copy, " \t\n"); w && n < 64; w = strtok(NULL, " \t\n")) words[n++] = strtoul(w, NULL, 16);
    const int per = (int)(sizeof(unsigned long) * 8);
    const int index = bit / per;
    if (index >= n) return 0;
    return (int)((words[n - 1 - index] >> (bit % per)) & 1ul);
}

static int isPad(const char* keys) {
    for (int b = BTN_SOUTH; b <= BTN_THUMBR; ++b)
        if (hasBit(keys, b)) return 1;
    return hasBit(keys, BTN_DPAD_UP);
}

static int screenOff = 0;

/* ---- the headphone socket ---------------------------------------------- */

static int jackIn = -1;       /* 1 in, 0 out, -1 not known yet */
static int speakerSet = -1;   /* what the speaker was last switched to */

/* The speaker for the socket's state and the rule; said and done only when
 * it changes. */
static void setSpeaker(const char* how) {
    if (jackIn < 0) return;
    const int on = jackRule == kJackIgnore ? 1 : jackRule == kJackInverted ? jackIn : !jackIn;
    if (on == speakerSet) return;
    char command[600];
    snprintf(command, sizeof command, "%s %s", speakerCommand, on ? "on" : "off");
    const int code = system(command);
    say(0, "headphones %s (%s)%s: the speaker %s%s", jackIn ? "in" : "out", how,
        jackRule == kJackIgnore ? ", headphones ignore" : jackRule == kJackInverted ? ", headphones inverted" : "",
        on ? "on" : "off", code == 0 ? "" : " COULD NOT BE SWITCHED");
    speakerSet = on;
}

/* The socket's state as the kernel holds it. A made-up device (the desk
 * test's FIFO) has none: it is known from its first event. */
static void readJack(const struct Device* d, const char* how) {
    unsigned long bits[SW_MAX / (8 * sizeof(unsigned long)) + 1];
    memset(bits, 0, sizeof bits);
    if (ioctl(d->fd, EVIOCGSW(sizeof bits), bits) < 0) return;
    jackIn = (int)((bits[SW_HEADPHONE_INSERT / (8 * sizeof(unsigned long))] >> (SW_HEADPHONE_INSERT % (8 * sizeof(unsigned long)))) & 1ul);
    setSpeaker(how);
}

static void grab(struct Device* d, int on) {
    if (d->fd < 0 || d->grabbed == on) return;
    if (ioctl(d->fd, EVIOCGRAB, on ? 1 : 0) == 0) d->grabbed = on;
    else if (on) say(0, "%s: could not hold it (%s)", d->name, strerror(errno));
}

static void forget(int i) {
    say(0, "%s: gone", devices[i].name);
    if (devices[i].fd >= 0) close(devices[i].fd);
    devices[i] = devices[--deviceCount];
}

/* Opens every power key and joypad not yet open. */
static void findDevices(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/class/input", sysDir);
    DIR* dir = opendir(path);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0 || strlen(e->d_name) >= sizeof devices[0].name) continue;
        int known = 0;
        for (int i = 0; i < deviceCount; ++i)
            if (strcmp(devices[i].name, e->d_name) == 0) known = 1;
        if (known || deviceCount >= kMostDevices) continue;
        char keys[1024], switches[256] = "", name[128] = "?";
        snprintf(path, sizeof path, "%s/class/input/%s/device/capabilities/key", sysDir, e->d_name);
        if (!readLine(path, keys, sizeof keys)) continue;
        snprintf(path, sizeof path, "%s/class/input/%s/device/capabilities/sw", sysDir, e->d_name);
        readLine(path, switches, sizeof switches);
        const int power = hasBit(keys, KEY_POWER), pad = isPad(keys), jack = hasBit(switches, SW_HEADPHONE_INSERT);
        if (!power && !pad && !jack) continue;
        snprintf(path, sizeof path, "%s/%s", devDir, e->d_name);
        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue; /* its node is not there yet: next time */
        snprintf(path, sizeof path, "%s/class/input/%s/device/name", sysDir, e->d_name);
        readLine(path, name, sizeof name);
        struct Device* d = &devices[deviceCount++];
        memset(d, 0, sizeof *d);
        memcpy(d->name, e->d_name, strlen(e->d_name) + 1);
        d->fd = fd;
        d->power = power;
        d->pad = pad;
        d->jack = jack;
        say(0, "%s (%s): %s%s%s%s%s", d->name, name, power ? "the power key" : "", power && pad ? ", " : "", pad ? "a joypad" : "",
            jack && (power || pad) ? ", " : "", jack ? "the headphone socket" : "");
        if (jack) readJack(d, "as found");
        if (pad && screenOff) grab(d, 1);
    }
    closedir(dir);
}

/* ---- the screen -------------------------------------------------------- */

static void backlight(int on) {
    char path[512];
    snprintf(path, sizeof path, "%s/class/backlight", sysDir);
    DIR* dir = opendir(path);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        /* bl_power: 0 on, 4 off (FB_BLANK_POWERDOWN); the brightness is kept as it was */
        snprintf(path, sizeof path, "%s/class/backlight/%s/bl_power", sysDir, e->d_name);
        if (!writeText(path, on ? "0\n" : "4\n")) say(0, "backlight %s: could not set bl_power", e->d_name);
    }
    closedir(dir);
}

static void setScreen(int off) {
    screenOff = off;
    backlight(!off);
    for (int i = 0; i < deviceCount; ++i)
        if (devices[i].pad) grab(&devices[i], off);
    say(1, off ? "the screen off, the buttons held (a short press of the power key: back)" : "the screen on");
}

/* ---- the screen seen working, and the green light ---------------------- */

static char treeId[128] = ""; /* empty: not the handheld, nothing is checked */
static int screenSeen = 0;

/* A file's first line without the NUL a device-tree string ends with. */
static void readTree(void) {
    treeId[0] = 0;
    FILE* f = fopen(treeFile, "r");
    if (!f) return;
    const size_t got = fread(treeId, 1, sizeof treeId - 1, f);
    fclose(f);
    treeId[got] = 0;
    treeId[strcspn(treeId, "\r\n")] = 0;
}

/* The power light steady (no trigger, full): Linux has it blinking until now. */
static void steadyLight(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/class/leds", sysDir);
    DIR* dir = opendir(path);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (!strstr(e->d_name, "power")) continue;
        snprintf(path, sizeof path, "%s/class/leds/%s/trigger", sysDir, e->d_name);
        writeText(path, "none\n");
        snprintf(path, sizeof path, "%s/class/leds/%s/brightness", sysDir, e->d_name);
        writeText(path, "1\n");
    }
    closedir(dir);
}

static void screenWasSeen(const char* how) {
    screenSeen = 1;
    steadyLight();
    char text[160];
    snprintf(text, sizeof text, "%s\n", treeId);
    const int kept = writeText(seenFile, text);
    say(1, "the screen counts as seen working (%s) with %s%s", how, treeId, kept ? "" : "; that could not be kept on the card");
}

static int anyEntry(const char* folder, const char* with) {
    char path[512];
    snprintf(path, sizeof path, "%s/class/%s", sysDir, folder);
    DIR* dir = opendir(path);
    if (!dir) return 0;
    int found = 0;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL)
        if (e->d_name[0] != '.' && (!with || strstr(e->d_name, with))) found = 1;
    closedir(dir);
    return found;
}

/* ---- off and sleep ----------------------------------------------------- */

static int offAsked = 0;

static void turnOff(const char* why) {
    if (offAsked) return;
    offAsked = 1;
    say(1, "%s: turning off", why);
    backlight(0);
    sync();
    const int code = system(offCommand);
    if (code != 0) {
        say(1, "\"%s\" failed (%d): still on", offCommand, code);
        offAsked = 0;
        backlight(!screenOff);
    }
}

static void sleepNow(void) {
    char path[512];
    say(1, "power key: sleep (suspend: untried on the handheld)");
    backlight(0);
    sync();
    snprintf(path, sizeof path, "%s/power/state", sysDir);
    const long before = nowMs();
    if (!writeText(path, "mem\n")) {
        say(1, "could not sleep (%s): the screen off instead", strerror(errno));
        setScreen(1);
        return;
    }
    say(1, "awake after %.1f s", (nowMs() - before) / 1000.0);
    backlight(!screenOff);
}

/* ---- the battery ------------------------------------------------------- */

static int lowReads = 0;
static int lastPercent = -2, lastPower = -1;

static void readBattery(void) {
    char path[512], text[64];
    snprintf(path, sizeof path, "%s/class/power_supply", sysDir);
    DIR* dir = opendir(path);
    if (!dir) return;
    int onPower = 0, found = 0;
    long percent = -1, microvolts = -1;
    char status[32] = "";
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/class/power_supply/%s/type", sysDir, e->d_name);
        if (!readLine(path, text, sizeof text)) continue;
        if (strcmp(text, "Battery") == 0) {
            long cap;
            snprintf(path, sizeof path, "%s/class/power_supply/%s/capacity", sysDir, e->d_name);
            if (found || !readNumber(path, &cap)) continue;
            found = 1;
            percent = cap;
            snprintf(path, sizeof path, "%s/class/power_supply/%s/voltage_now", sysDir, e->d_name);
            if (!readNumber(path, &microvolts)) microvolts = -1;
            snprintf(path, sizeof path, "%s/class/power_supply/%s/status", sysDir, e->d_name);
            if (!readLine(path, status, sizeof status)) status[0] = 0;
        } else {
            long online;
            snprintf(path, sizeof path, "%s/class/power_supply/%s/online", sysDir, e->d_name);
            if (readNumber(path, &online) && online > 0) onPower = 1;
        }
    }
    closedir(dir);
    if (!found) return;
    /* Charging, full, or held at full on the charger ("Not charging") */
    if (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0 || strcmp(status, "Not charging") == 0) onPower = 1;
    /* A voltage that is not a single cell's (a test supply in millivolts) is not read */
    const int voltsKnown = microvolts >= 2500000 && microvolts <= 5000000;
    if (percent != lastPercent || onPower != lastPower) {
        char volts[32] = "";
        if (voltsKnown) snprintf(volts, sizeof volts, ", %.2f V", microvolts / 1e6);
        say(0, "battery %ld%%%s, %s, %s", percent, volts, status[0] ? status : "?", onPower ? "on a charger" : "on the battery");
        lastPercent = (int)percent;
        lastPower = onPower;
    }
    const int low = !onPower && ((lowPercent > 0 && percent >= 0 && percent <= lowPercent) || (voltsKnown && microvolts <= kLowMicrovolts));
    if (!low) {
        if (lowReads) say(0, "battery no longer low");
        lowReads = 0;
        return;
    }
    ++lowReads;
    say(lowReads == 1, "battery low (%ld%%%s), read %d of 2", percent, voltsKnown && microvolts <= kLowMicrovolts ? ", under 3.25 V" : "", lowReads);
    if (lowReads >= 2) {
        char why[64];
        snprintf(why, sizeof why, "battery low (%ld%%)", percent);
        turnOff(why);
    }
}

/* ---- the loop ---------------------------------------------------------- */

static volatile sig_atomic_t stopping = 0;
static void onStop(int sig) { (void)sig; stopping = 1; }

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v) { fprintf(stderr, "apotheca-power: %s needs a value\n", a); return 2; }
        if (strcmp(a, "--sys") == 0) sysDir = v;
        else if (strcmp(a, "--dev") == 0) devDir = v;
        else if (strcmp(a, "--config") == 0) configFile = v;
        else if (strcmp(a, "--log") == 0) logFile = v;
        else if (strcmp(a, "--off") == 0) offCommand = v;
        else if (strcmp(a, "--console") == 0) consoleFile = v;
        else if (strcmp(a, "--poll") == 0) pollSeconds = atoi(v) > 0 ? atoi(v) : 20;
        else if (strcmp(a, "--long-ms") == 0) longMs = atol(v) > 0 ? atol(v) : 2000;
        else if (strcmp(a, "--tree") == 0) treeFile = v;
        else if (strcmp(a, "--seen") == 0) seenFile = v;
        else if (strcmp(a, "--find-screen-s") == 0) findScreenSeconds = atoi(v) > 0 ? atoi(v) : 60;
        else if (strcmp(a, "--speaker") == 0) speakerCommand = v;
        else { fprintf(stderr, "apotheca-power: unknown option %s\n", a); return 2; }
        ++i;
    }
    signal(SIGTERM, onStop);
    signal(SIGINT, onStop);
    signal(SIGPIPE, SIG_IGN);

    /* A log that has grown large starts again (the last one kept as .1). */
    struct stat st;
    if (stat(logFile, &st) == 0 && st.st_size > 256 * 1024) {
        char old[512];
        snprintf(old, sizeof old, "%s.1", logFile);
        rename(logFile, old);
    }
    readConfig();
    say(0, "started: a short press %s, held %ld ms turns off; off at %d%% on the battery%s",
        keySleeps ? "sleeps (untried)" : "turns the screen off", longMs, lowPercent, lowPercent ? "" : " (never)");
    backlight(1); /* whatever an earlier run left: the screen on */

    /* Has this screen (this device tree) been seen working? */
    readTree();
    const long startedAt = nowMs();
    long checkAt = 0, findScreenAt = 0;
    if (treeId[0]) {
        char seen[128] = "";
        readLine(seenFile, seen, sizeof seen);
        screenSeen = strcmp(seen, treeId) == 0;
        if (screenSeen) {
            steadyLight();
            say(0, "the screen: %s, seen working before", treeId);
        } else {
            say(1, "the screen: %s, not yet seen working (before: %s). Any button says it is%s", treeId, seen[0] ? seen : "none",
                screenCheckSeconds ? "; with none, off" : "");
            if (screenCheckSeconds) checkAt = startedAt + screenCheckSeconds * 1000L;
        }
        findScreenAt = startedAt + findScreenSeconds * 1000L;
    }

    /* New input devices: watched in /dev/input, and /dev/input watched for
     * from its parent, since the kernel makes it only with the first device
     * (none in qemu until a stand-in is made). */
    const int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (watch >= 0) {
        char parent[512];
        snprintf(parent, sizeof parent, "%s", devDir);
        char* slash = strrchr(parent, '/');
        if (slash && slash != parent) *slash = 0;
        inotify_add_watch(watch, parent, IN_CREATE);
        inotify_add_watch(watch, devDir, IN_CREATE | IN_ATTRIB);
    }
    findDevices();
    readBattery();

    long nextRead = nowMs() + pollSeconds * 1000L;
    long pressedAt = 0, findAt = 0;
    int keyDown = 0, longDone = 0, noJackSaid = 0;
    while (!stopping) {
        struct pollfd fds[kMostDevices + 1];
        int n = 0;
        if (watch >= 0) fds[n].fd = watch, fds[n].events = POLLIN, fds[n].revents = 0, ++n;
        for (int i = 0; i < deviceCount; ++i) fds[n].fd = devices[i].fd, fds[n].events = POLLIN, fds[n].revents = 0, ++n;
        long now = nowMs();
        long wait = nextRead - now;
        if (keyDown && !longDone && pressedAt + longMs - now < wait) wait = pressedAt + longMs - now;
        if (findAt && findAt - now < wait) wait = findAt - now;
        if (checkAt && !screenSeen && checkAt - now < wait) wait = checkAt - now;
        if (findScreenAt && findScreenAt - now < wait) wait = findScreenAt - now;
        if (wait < 0) wait = 0;
        poll(fds, (nfds_t)n, (int)wait);
        now = nowMs();

        if (watch >= 0 && (fds[0].revents & POLLIN)) {
            char buffer[4096];
            while (read(watch, buffer, sizeof buffer) > 0) {}
            findAt = now + 200; /* a new node: its sysfs entry is there, its permissions a moment later */
        }
        for (int i = deviceCount - 1; i >= 0; --i) {
            struct input_event ev[32];
            for (;;) {
                const ssize_t got = read(devices[i].fd, ev, sizeof ev);
                if (got < 0 && errno == ENODEV) { forget(i); break; }
                if (got <= 0) break;
                if (devices[i].pad && treeId[0] && !screenSeen && !screenOff && !offAsked) {
                    /* A button pressed (not the stick's readings): somebody sees the screen */
                    for (int k = 0; k < (int)(got / (ssize_t)sizeof ev[0]); ++k)
                        if (ev[k].type == EV_KEY && ev[k].value == 1 && ev[k].code >= BTN_MISC) {
                            screenWasSeen("a button was pressed");
                            break;
                        }
                }
                if (devices[i].jack) {
                    for (int k = 0; k < (int)(got / (ssize_t)sizeof ev[0]); ++k) {
                        if (ev[k].type == EV_SW && ev[k].code == SW_HEADPHONE_INSERT) {
                            jackIn = ev[k].value != 0;
                            setSpeaker("just now");
                        } else if (ev[k].type == EV_SYN && ev[k].code == SYN_DROPPED) {
                            readJack(&devices[i], "read again");
                        }
                    }
                }
                if (!devices[i].power) continue; /* a joypad's other events: read only to keep its queue empty */
                for (int k = 0; k < (int)(got / (ssize_t)sizeof ev[0]); ++k) {
                    if (ev[k].type != EV_KEY || ev[k].code != KEY_POWER) continue;
                    if (ev[k].value == 1 && !keyDown) {
                        keyDown = 1, longDone = 0, pressedAt = now;
                    } else if (ev[k].value == 0 && keyDown) {
                        keyDown = 0;
                        if (!longDone && !offAsked) {
                            say(0, "power key: a short press (%ld ms)", now - pressedAt);
                            if (keySleeps && !screenOff) sleepNow();
                            else setScreen(!screenOff);
                        }
                    }
                }
            }
        }
        if (keyDown && !longDone && now - pressedAt >= longMs) {
            longDone = 1;
            turnOff("power key held");
        }
        if (findScreenAt && now >= findScreenAt) {
            findScreenAt = 0;
            const int light = anyEntry("backlight", NULL), display = anyEntry("drm", "-");
            if (!light || !display) {
                say(1, "no screen to show anything on (backlight: %s, display: %s)", light ? "yes" : "none", display ? "yes" : "none");
                turnOff("no screen found");
                if (!offAsked) findScreenAt = now + 60000L;
            }
        }
        if (checkAt && !screenSeen && now >= checkAt) {
            char why[200];
            snprintf(why, sizeof why, "no button in %d s with a screen not yet seen working (%s)", screenCheckSeconds, treeId);
            checkAt = 0;
            turnOff(why);
            if (!offAsked) checkAt = now + 60000L;
        }
        if (findAt && now >= findAt) {
            findAt = 0;
            /* Always again: the kernel takes /dev/input away with its last device,
             * and the watch on it with it (a stand-in joypad made again) */
            if (watch >= 0) inotify_add_watch(watch, devDir, IN_CREATE | IN_ATTRIB);
            findDevices();
        }
        if (now >= nextRead) {
            /* Always again: the kernel takes /dev/input away with its last device,
             * and the watch on it with it (a stand-in joypad made again) */
            if (watch >= 0) inotify_add_watch(watch, devDir, IN_CREATE | IN_ATTRIB);
            const int before = screenCheckSeconds;
            readConfig();
            if (screenCheckSeconds != before && treeId[0] && !screenSeen)
                checkAt = screenCheckSeconds ? startedAt + screenCheckSeconds * 1000L : 0;
            readBattery();
            findDevices();
            /* The socket again, in case an event was missed, and the rule,
             * in case the config changed */
            int jacks = 0;
            for (int i = 0; i < deviceCount; ++i)
                if (devices[i].jack) ++jacks, readJack(&devices[i], "read again");
            setSpeaker("the rule changed");
            if (!jacks && !noJackSaid) {
                noJackSaid = 1;
                say(0, "no headphone socket among the input devices: the speaker is left as it is");
            }
            nextRead = now + pollSeconds * 1000L;
        }
    }
    say(0, "stopped");
    for (int i = 0; i < deviceCount; ++i) close(devices[i].fd); /* a hold ends with its file */
    return 0;
}
