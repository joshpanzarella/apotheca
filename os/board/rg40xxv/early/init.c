/* The kernel's first program on Apotheca's system: /init of a small
 * initramfs built into the kernel (firmware plan, section 16).
 *
 * It exists to get the kernel's own messages onto the card from a handheld
 * that never reaches the real system, and to say by the lights how far the
 * start got. Then it does what the kernel did by itself before: mounts the
 * root file system and runs its /sbin/init.
 *
 *   1. /dev, /proc, /sys. The red light ON (the kernel put it out; the
 *      green one blinks the kernel's heartbeat): "the first program runs".
 *      The kernel's deadline (apotheca_deadline) moved to 150 s from now.
 *      STARTING on the screen, under the kernel's logo.
 *   2. Four times a second, until the root is found or WAIT seconds are up
 *      (40; apotheca.wait=N on the command line). A start that finds the
 *      root at once writes nothing and waits for nothing; from 2 s of
 *      waiting on, kernel-log.txt at most every 2 s:
 *        the root: root=PARTUUID=SSSSSSSS-NN is partition NN of the disk
 *          whose MBR signature is SSSSSSSS; root=/dev/NAME is that device;
 *        the boot partition: partition 1 of the root's disk, else any
 *          partition that mounts as FAT and holds extlinux/extlinux.conf;
 *        kernel-log.txt on the boot partition written again: what stage
 *          this is, the block devices seen, and the kernel's messages.
 *      10 s in with no block device at all: the green light flickers fast.
 *   3. The root mounted read-only on /newroot, kernel-log.txt written one
 *      last time, the red light out, the green one an even slow blink, the
 *      boot partition unmounted, and /sbin/init of the root run in its place.
 *   4. No root in time, or it will not mount, or it has no /sbin/init: said
 *      in kernel-log.txt, 20 s of the fast flicker, then a reset. The reset
 *      keeps the memory, so the boot loader finds the kernel's messages there
 *      (ramoops) and saves them even when Linux could not write the card.
 *
 * Static, no shell, nothing but system calls: there is nothing else in the
 * initramfs. Built by external.mk into the kernel's tree before the kernel.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

static char notes[8192]; /* what this program did, the top of kernel-log.txt */
static size_t notesLen = 0;

static double up(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void note(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    const int n = snprintf(notes + notesLen, sizeof notes - notesLen, "[%8.2f] %s\n", up(), line);
    if (n > 0 && (size_t)n < sizeof notes - notesLen) notesLen += (size_t)n;
    /* and to the kernel's log, so it is on every console and in ramoops */
    const int k = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (k >= 0) {
        char out[540];
        const int m = snprintf(out, sizeof out, "apotheca-early: %s\n", line);
        if (m > 0 && write(k, out, (size_t)m) < 0) {}
        close(k);
    }
}

static int writeText(const char* path, const char* text) {
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const ssize_t n = write(fd, text, strlen(text));
    close(fd);
    return n == (ssize_t)strlen(text);
}

static int readText(const char* path, char* out, size_t size) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const ssize_t n = read(fd, out, size - 1);
    close(fd);
    if (n < 0) return 0;
    out[n] = 0;
    out[strcspn(out, "\n")] = 0;
    return 1;
}

/* ---- the lights -------------------------------------------------------- */

/* light("power", ...) is the green one (green:power), "status" the red. */
static void light(const char* which, const char* trigger, int on, int onMs, int offMs) {
    DIR* dir = opendir("/sys/class/leds");
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (!strstr(e->d_name, which)) continue;
        char path[512], text[32];
        snprintf(path, sizeof path, "/sys/class/leds/%s/trigger", e->d_name);
        writeText(path, trigger);
        if (strcmp(trigger, "timer") == 0) {
            snprintf(path, sizeof path, "/sys/class/leds/%s/delay_on", e->d_name);
            snprintf(text, sizeof text, "%d", onMs);
            writeText(path, text);
            snprintf(path, sizeof path, "/sys/class/leds/%s/delay_off", e->d_name);
            snprintf(text, sizeof text, "%d", offMs);
            writeText(path, text);
        } else if (strcmp(trigger, "none") == 0) {
            snprintf(path, sizeof path, "/sys/class/leds/%s/brightness", e->d_name);
            writeText(path, on ? "1" : "0");
        }
    }
    closedir(dir);
}

/* ---- the command line -------------------------------------------------- */

static char cmdline[2048];

/* The value of WORD=VALUE on the kernel's command line, or NULL. */
static const char* argOf(const char* word, char* out, size_t size) {
    const size_t len = strlen(word);
    const char* p = cmdline;
    while (*p) {
        while (*p == ' ') ++p;
        const char* end = p + strcspn(p, " \n");
        if ((size_t)(end - p) > len && strncmp(p, word, len) == 0 && p[len] == '=') {
            size_t n = (size_t)(end - p) - len - 1;
            if (n >= size) n = size - 1;
            memcpy(out, p + len + 1, n);
            out[n] = 0;
            return out;
        }
        p = end;
        if (*p) ++p;
    }
    return NULL;
}

/* ---- block devices ----------------------------------------------------- */

static char seen[2048]; /* the block devices as last looked at, for the log */

/* The disk's MBR signature (bytes 440..443, little endian), or 0. */
static uint32_t signatureOf(const char* disk) {
    char path[300];
    unsigned char sector[512];
    snprintf(path, sizeof path, "/dev/%s", disk);
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const ssize_t n = read(fd, sector, sizeof sector);
    close(fd);
    if (n != (ssize_t)sizeof sector || sector[510] != 0x55 || sector[511] != 0xaa) return 0;
    return (uint32_t)sector[440] | (uint32_t)sector[441] << 8 | (uint32_t)sector[442] << 16 | (uint32_t)sector[443] << 24;
}

/* Partition `number` of `disk` (mmcblk0, 2 -> mmcblk0p2), or 0. */
static int partitionOf(const char* disk, int number, char* out, size_t size) {
    char path[600], text[32];
    snprintf(path, sizeof path, "/sys/block/%s", disk);
    DIR* dir = opendir(path);
    if (!dir) return 0;
    struct dirent* e;
    int found = 0;
    while (!found && (e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, disk, strlen(disk)) != 0) continue;
        snprintf(path, sizeof path, "/sys/block/%s/%s/partition", disk, e->d_name);
        if (readText(path, text, sizeof text) && atoi(text) == number) {
            snprintf(out, size, "%s", e->d_name);
            found = 1;
        }
    }
    closedir(dir);
    return found;
}

/* A card or a disk (mmcblk0, sda, vda, nvme0n1), not the kernel's own
 * made-up ones (loop, ram, zram, nbd, mtd), which are there with no card. */
static int isDisk(const char* name) {
    return strncmp(name, "mmcblk", 6) == 0 || strncmp(name, "sd", 2) == 0 || strncmp(name, "vd", 2) == 0 ||
           strncmp(name, "nvme", 4) == 0;
}

/* Looks at every disk: fills `seen`, and finds the root named on the command
 * line. Returns 1 with the root's partition and its disk. */
static int findRoot(const char* root, char* part, size_t partSize, char* diskOut, size_t diskSize, int* disks) {
    uint32_t want = 0;
    int number = 0, byId = 0;
    if (strncmp(root, "PARTUUID=", 9) == 0 && sscanf(root + 9, "%8x-%2x", &want, &number) == 2) byId = 1;
    size_t len = 0;
    seen[0] = 0;
    *disks = 0;
    int found = 0;
    DIR* dir = opendir("/sys/block");
    if (!dir) return 0;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (!isDisk(e->d_name)) continue;
        ++*disks;
        char path[600], sectors[32] = "?";
        snprintf(path, sizeof path, "/sys/block/%s/size", e->d_name);
        readText(path, sectors, sizeof sectors);
        const uint32_t sig = signatureOf(e->d_name);
        const int n = snprintf(seen + len, sizeof seen - len, "  %s: %s sectors, MBR signature %08x\n", e->d_name, sectors, sig);
        if (n > 0 && (size_t)n < sizeof seen - len) len += (size_t)n;
        if (!found && byId && sig == want && partitionOf(e->d_name, number, part, partSize)) {
            snprintf(diskOut, diskSize, "%s", e->d_name);
            found = 1;
        }
        if (!found && !byId && strncmp(root, "/dev/", 5) == 0) {
            /* root=/dev/NAME: the disk itself, or one of its partitions */
            char sys[600];
            snprintf(sys, sizeof sys, "/sys/block/%s/%s", e->d_name, root + 5);
            if (strcmp(root + 5, e->d_name) == 0 || access(sys, F_OK) == 0) {
                snprintf(part, partSize, "%s", root + 5);
                snprintf(diskOut, diskSize, "%s", e->d_name);
                found = 1;
            }
        }
    }
    closedir(dir);
    return found;
}

/* ---- the boot partition and kernel-log.txt ----------------------------- */

static int bootMounted = 0;
static char bootPart[64] = "";

static int tryBoot(const char* part) {
    char dev[300];
    snprintf(dev, sizeof dev, "/dev/%s", part);
    if (mount(dev, "/boot", "vfat", MS_NOATIME, "") != 0) return 0;
    if (access("/boot/extlinux/extlinux.conf", F_OK) != 0) {
        umount("/boot");
        return 0;
    }
    snprintf(bootPart, sizeof bootPart, "%s", part);
    bootMounted = 1;
    note("the boot partition is %s: kernel-log.txt is written there", part);
    return 1;
}

/* The boot partition: the first of the root's disk when that is known, else
 * any partition of any disk that is our boot partition. */
static void findBoot(const char* rootDisk) {
    char part[64];
    if (bootMounted) return;
    if (rootDisk && rootDisk[0] && partitionOf(rootDisk, 1, part, sizeof part) && tryBoot(part)) return;
    DIR* dir = opendir("/sys/block");
    if (!dir) return;
    struct dirent* e;
    while (!bootMounted && (e = readdir(dir)) != NULL) {
        if (!isDisk(e->d_name)) continue;
        for (int n = 1; n <= 4 && !bootMounted; ++n)
            if (partitionOf(e->d_name, n, part, sizeof part)) tryBoot(part);
    }
    closedir(dir);
}

/* kernel-log.txt: this program's notes, the disks, the kernel's messages. */
static void writeLog(const char* stage) {
    if (!bootMounted) return;
    static char kernel[1 << 20];
    int got = klogctl(3 /* SYSLOG_ACTION_READ_ALL */, kernel, (int)sizeof kernel - 1);
    if (got < 0) got = 0;
    const int fd = open("/boot/kernel-log.part", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    char head[512];
    const int n = snprintf(head, sizeof head,
                           "Apotheca: the kernel's first program (firmware plan, section 16)\n"
                           "written %.1f s after the kernel started; stage: %s\n\n"
                           "===== what this program did\n",
                           up(), stage);
    int ok = write(fd, head, (size_t)n) == n;
    ok = ok && write(fd, notes, notesLen) == (ssize_t)notesLen;
    const char* disksHead = "\n===== the disks Linux sees\n";
    ok = ok && write(fd, disksHead, strlen(disksHead)) == (ssize_t)strlen(disksHead);
    ok = ok && write(fd, seen, strlen(seen)) == (ssize_t)strlen(seen);
    const char* kernelHead = "\n===== the kernel's messages\n";
    ok = ok && write(fd, kernelHead, strlen(kernelHead)) == (ssize_t)strlen(kernelHead);
    ok = ok && write(fd, kernel, (size_t)got) == got;
    fsync(fd);
    close(fd);
    /* whole or not at all: the name changes only once the text is on the card */
    if (ok) rename("/boot/kernel-log.part", "/boot/kernel-log.txt");
    sync();
}

static void leaveBoot(void) {
    if (!bootMounted) return;
    sync();
    umount("/boot");
    bootMounted = 0;
}

static void pause_(double seconds) {
    struct timespec t;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)((seconds - (double)t.tv_sec) * 1e9);
    nanosleep(&t, NULL);
}

/* The end of a start that cannot go on: said, shown, then a reset. */
static void giveUp(const char* why) {
    note("CANNOT GO ON: %s", why);
    note("the green light flickers for 20 s, then the handheld resets itself (the boot loader then saves the kernel's messages from memory)");
    light("power", "timer", 1, 80, 80);
    light("status", "none", 1, 0, 0);
    writeLog("stopped: could not start the system");
    pause_(20);
    writeLog("stopped: could not start the system; resetting now");
    leaveBoot();
    sync();
    reboot(RB_AUTOBOOT);
    for (;;) pause_(60);
}

int main(void) {
    mkdir("/dev", 0755);
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/boot", 0755);
    mkdir("/newroot", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");
    mount("proc", "/proc", "proc", 0, "");
    mount("sysfs", "/sys", "sysfs", 0, "");

    /* Something on the screen at once (it is kept until the display is up,
     * if that comes later): the kernel itself prints nothing unless it has an
     * error to say. The picture above it (SHAPETAKER, APOTHECA) is the
     * kernel's logo (os/tools/make-logo.py): the screen is not cleared, or
     * it would go. The console is 80 letters wide; this stands in the middle. */
    {
        const int screen = open("/dev/tty1", O_WRONLY | O_NOCTTY | O_CLOEXEC);
        if (screen >= 0) {
            const char* words = "\r\n                                    STARTING\r\n";
            if (write(screen, words, strlen(words)) < 0) {}
            close(screen);
        }
    }
    writeText("/sys/module/apotheca_deadline/parameters/seconds", "150");
    light("status", "none", 1, 0, 0);
    note("started (the red light on; the kernel's deadline moved to 150 s)");

    {
        const int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
        ssize_t n = fd >= 0 ? read(fd, cmdline, sizeof cmdline - 1) : 0;
        if (fd >= 0) close(fd);
        if (n < 0) n = 0;
        cmdline[n] = 0;
    }
    char root[128] = "", type[32] = "", waitText[16] = "";
    if (!argOf("root", root, sizeof root)) note("no root= on the command line: %s", cmdline);
    if (!argOf("rootfstype", type, sizeof type)) snprintf(type, sizeof type, "squashfs");
    int wait = 40;
    if (argOf("apotheca.wait", waitText, sizeof waitText) && atoi(waitText) > 0) wait = atoi(waitText);
    note("looking for the root %s (%s), for %d s at most", root, type, wait);

    char part[64] = "", disk[64] = "";
    int disks = 0, flickering = 0, found = 0;
    const double began = up();
    double wrote = began;
    for (;;) {
        found = findRoot(root, part, sizeof part, disk, sizeof disk, &disks);
        if (found) break;
        const double waited = up() - began;
        if (waited >= wait) break;
        /* A start that goes well writes nothing and waits for nothing: the
         * boot partition is looked for, and kernel-log.txt written, only
         * once the root has kept us waiting 2 s. */
        if (waited >= 2) findBoot(NULL);
        if (disks == 0 && waited >= 10 && !flickering) {
            flickering = 1;
            note("10 s and Linux sees no disk at all (no card): the green light flickers");
            light("power", "timer", 1, 80, 80);
        }
        if (up() - wrote >= 2) {
            writeLog("waiting for the root's disk");
            wrote = up();
        }
        pause_(0.25);
    }
    if (!found) {
        char why[300];
        findBoot(NULL);
        snprintf(why, sizeof why, "no root %s after %d s (%d disk%s seen)", root, wait, disks, disks == 1 ? "" : "s");
        giveUp(why);
    }
    note("the root is %s, on the disk %s (found %.1f s after this program started)", part, disk, up() - began);

    char dev[300];
    snprintf(dev, sizeof dev, "/dev/%s", part);
    int mounted = 0, error = 0;
    for (int tries = 0; tries < 5 && !mounted; ++tries) {
        mounted = mount(dev, "/newroot", type, MS_RDONLY, "") == 0;
        error = errno;
        if (!mounted) pause_(1);
    }
    if (!mounted) {
        char why[300];
        findBoot(disk);
        snprintf(why, sizeof why, "the root %s will not mount as %s: %s", dev, type, strerror(error));
        giveUp(why);
    }
    if (access("/newroot/sbin/init", X_OK) != 0) {
        findBoot(disk);
        giveUp("the root has no /sbin/init");
    }
    note("the root is mounted; its /sbin/init is next (the red light out, the green one an even blink)");
    light("status", "none", 0, 0, 0);
    light("power", "timer", 1, 500, 500);
    if (bootMounted) { /* only when it was a wait worth a log */
        writeLog("the root is mounted: the system's own programs start now");
        leaveBoot();
    }
    note("handing over to the root's /sbin/init");

    /* Hand over: /dev goes with us, the rest is the system's own to mount. */
    umount("/proc");
    umount("/sys");
    if (mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL) != 0) mount("devtmpfs", "/newroot/dev", "devtmpfs", 0, "mode=0755");
    if (chdir("/newroot") != 0 || mount(".", "/", NULL, MS_MOVE, NULL) != 0 || chroot(".") != 0 || chdir("/") != 0) {
        mount("proc", "/proc", "proc", 0, "");
        mount("sysfs", "/sys", "sysfs", 0, "");
        giveUp("could not make the root the root");
    }
    const int console = open("/dev/console", O_RDWR);
    if (console >= 0) {
        dup2(console, 0);
        dup2(console, 1);
        dup2(console, 2);
        if (console > 2) close(console);
    }
    char* const argv[] = {(char*)"init", NULL};
    execv("/sbin/init", argv);
    /* Not reached unless the root's /sbin/init will not run. Said where it
     * can still be said (the kernel's log: ramoops keeps it), then a reset. */
    note("CANNOT GO ON: the root's /sbin/init did not start: %s", strerror(errno));
    light("power", "timer", 1, 80, 80);
    pause_(20);
    sync();
    reboot(RB_AUTOBOOT);
    for (;;) pause_(60);
}
