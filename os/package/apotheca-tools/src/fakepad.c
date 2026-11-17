/* fakepad: a stand-in for the RG40XXV's joypad, for the desk test under qemu.
 *
 * Makes a uinput device that declares exactly what ROCKNIX's
 * rocknix-singleadc-joypad driver declares for the RG40XXV's device tree
 * ("H700 Gamepad", vendor 0x484b, product 0x14df, its seventeen keys, ABS_X,
 * ABS_Y, ABS_RX and ABS_RY from -3600 to 3600), waits, then presses each key
 * in turn and pushes the stick right and down, printing what it did:
 *
 *   press 0x131 A
 *   axis ABS_X 3600
 *
 * so a test can line what SDL saw (padprobe) up with what was sent.
 * fakepad [SECONDS-BEFORE] (default 3). Needs /dev/uinput.
 *
 * fakepad --serve FIFO: makes the joypad and a stand-in for the AXP717's
 * power key ("axp20x-pek", KEY_POWER, as mainline's axp20x-pek declares it),
 * then reads lines from FIFO (opened again whenever a writer closes it) and
 * does them, until "end":
 *   down NAME / up NAME   a key down or up (NAME as above, or POWER)
 *   tap NAME              down, 150 ms, up
 *   wait MS
 * printing "did LINE" after each. */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Key { int code; const char* name; };

/* The device tree's buttons (sun50i-h700-anbernic-rg40xx-v), by the button
 * they are on the owner's RG40XXV. The tree labels BTN_NORTH (0x133) "X" and
 * BTN_WEST (0x134) "Y", but on the handheld the X button sends BTN_WEST and Y
 * BTN_NORTH (the owner, 2026-11-17). */
static const struct Key kKeys[] = {
    {0x220, "UP"},     {0x221, "DOWN"},   {0x222, "LEFT"},  {0x223, "RIGHT"},
    {0x131, "A"},      {0x130, "B"},      {0x134, "X"},     {0x133, "Y"},
    {0x136, "L1"},     {0x137, "R1"},     {0x138, "L2"},    {0x139, "R2"},
    {0x13b, "START"},  {0x13a, "SELECT"}, {0x13c, "MENU"},  {0x13d, "L3"},
    {0x13e, "R3"},
};
enum { kKeyCount = sizeof kKeys / sizeof kKeys[0] };

static void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != sizeof ev) perror("fakepad: write");
}

static void absAxis(int fd, int code) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code;
    a.absinfo.minimum = -3600;
    a.absinfo.maximum = 3600;
    a.absinfo.fuzz = 32;
    a.absinfo.flat = 32;
    if (ioctl(fd, UI_ABS_SETUP, &a) < 0) perror("fakepad: UI_ABS_SETUP");
}

static int codeOf(const char* name) {
    if (strcmp(name, "POWER") == 0) return KEY_POWER;
    for (int i = 0; i < kKeyCount; ++i)
        if (strcmp(kKeys[i].name, name) == 0) return kKeys[i].code;
    return -1;
}

static int makePowerKey(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("fakepad: /dev/uinput"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_POWER);
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_HOST;
    strcpy(setup.name, "axp20x-pek");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("fakepad: create the power key");
        return -1;
    }
    return fd;
}

/* --serve: do what the FIFO says, on the joypad (pad) and the power key (power). */
static int serve(int pad, const char* fifo) {
    const int power = makePowerKey();
    if (power < 0) return 1;
    printf("made axp20x-pek\nready\n");
    fflush(stdout);
    for (;;) {
        FILE* in = fopen(fifo, "r");
        if (!in) { perror("fakepad: the fifo"); return 1; }
        char line[128];
        while (fgets(line, sizeof line, in)) {
            char verb[16], name[16];
            line[strcspn(line, "\r\n")] = 0;
            const int n = sscanf(line, "%15s %15s", verb, name);
            if (n >= 1 && strcmp(verb, "end") == 0) {
                fclose(in);
                ioctl(power, UI_DEV_DESTROY);
                return 0;
            }
            if (n == 2 && strcmp(verb, "wait") == 0) {
                usleep((useconds_t)atoi(name) * 1000);
            } else if (n == 2) {
                const int code = codeOf(name);
                const int fd = code == KEY_POWER ? power : pad;
                const int down = strcmp(verb, "down") == 0, up = strcmp(verb, "up") == 0, tap = strcmp(verb, "tap") == 0;
                if (code < 0 || !(down || up || tap)) {
                    printf("not understood: %s\n", line);
                    fflush(stdout);
                    continue;
                }
                if (down || tap) emit(fd, EV_KEY, code, 1), emit(fd, EV_SYN, SYN_REPORT, 0);
                if (tap) usleep(150000);
                if (up || tap) emit(fd, EV_KEY, code, 0), emit(fd, EV_SYN, SYN_REPORT, 0);
            } else {
                continue;
            }
            printf("did %s\n", line);
            fflush(stdout);
        }
        fclose(in);
    }
}

int main(int argc, char** argv) {
    const char* fifo = argc > 2 && strcmp(argv[1], "--serve") == 0 ? argv[2] : NULL;
    int before = !fifo && argc > 1 ? atoi(argv[1]) : 3;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("fakepad: /dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; i < kKeyCount; ++i) ioctl(fd, UI_SET_KEYBIT, kKeys[i].code);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);
    ioctl(fd, UI_SET_ABSBIT, ABS_RY);
    absAxis(fd, ABS_X);
    absAxis(fd, ABS_Y);
    absAxis(fd, ABS_RX);
    absAxis(fd, ABS_RY);
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = 0; /* the driver leaves joypad-bustype unset: 0 */
    setup.id.vendor = 0x484b;
    setup.id.product = 0x14df;
    setup.id.version = 0x100;
    strcpy(setup.name, "H700 Gamepad");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("fakepad: create");
        return 1;
    }
    printf("made H700 Gamepad\n");
    fflush(stdout);
    if (fifo) return serve(fd, fifo);
    sleep(before);
    for (int i = 0; i < kKeyCount; ++i) {
        printf("press 0x%x %s\n", kKeys[i].code, kKeys[i].name);
        fflush(stdout);
        emit(fd, EV_KEY, kKeys[i].code, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(150000);
        emit(fd, EV_KEY, kKeys[i].code, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(150000);
    }
    printf("axis ABS_X 3600\n");
    fflush(stdout);
    emit(fd, EV_ABS, ABS_X, 3600);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(200000);
    emit(fd, EV_ABS, ABS_X, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(200000);
    printf("axis ABS_Y 3600\n");
    fflush(stdout);
    emit(fd, EV_ABS, ABS_Y, 3600);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(200000);
    emit(fd, EV_ABS, ABS_Y, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(500000);
    printf("done\n");
    fflush(stdout);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
