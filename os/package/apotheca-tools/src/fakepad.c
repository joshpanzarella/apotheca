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
 * fakepad [SECONDS-BEFORE] (default 3). Needs /dev/uinput. */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Key { int code; const char* name; };

/* The device tree's buttons, by their labels there (sun50i-h700-anbernic-rg40xx-v). */
static const struct Key kKeys[] = {
    {0x220, "UP"},     {0x221, "DOWN"},   {0x222, "LEFT"},  {0x223, "RIGHT"},
    {0x131, "A"},      {0x130, "B"},      {0x133, "X"},     {0x134, "Y"},
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

int main(int argc, char** argv) {
    int before = argc > 1 ? atoi(argv[1]) : 3;
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
