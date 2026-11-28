/*
 * apotheca-splash: the start-up picture with its planet band turning, at
 * start-up and at turn-off (owner, 2026-11-28: "something to see that it is
 * doing something ... the outer part of the circle with the alchemical symbols
 * rotate").
 *
 *   apotheca-splash --boot   inittab's first line: until Apotheca's picture is
 *                            on the screen
 *   apotheca-splash --off    off and S95apotheca, once Apotheca has stopped:
 *                            until the power goes (init's TERM)
 *
 * Both go into the background at once (the parent prints the child's pid and
 * returns), at nice 10, and never hold anything up: no /dev/fb0, a short read
 * or any other failure, and it ends quietly. It opens nothing on /data (its
 * files are on the read-only root), so it holds no unmount back.
 *
 * The picture is the kernel's own (os/tools/make-logo.py writes both, and
 * splash.bin's still is the kernel's picture to the pixel), drawn whole to
 * /dev/fb0 (the DRM driver's fbdev, where the kernel drew its logo), and then
 * only the band is drawn again, about 30 times a second, its seven signs
 * turned clockwise, one turn in TURN_S seconds. Everything else stays as it
 * is. The signs pass UNDER the four element discs (they reach into the band),
 * as in the picture. A frame is made in a copy of the screen and copied in
 * just after a vertical blank (FBIO_WAITFORVSYNC), so it does not tear; with
 * no vertical blank (qemu) it is timed by the clock.
 *
 * When it stops (--boot):
 *   - Apotheca's picture is on the screen: the CRTC that showed the fbdev's
 *     framebuffer at the start shows another one (read through the display's
 *     /dev/dri/card node, opened before Apotheca starts and its master role
 *     given up at once, so Apotheca still gets it);
 *   - or the screen has been another program's (a DRM master: the fbdev
 *     answers EBUSY) for BUSY_S seconds without that (qemu: SDL's mode set
 *     is refused there);
 *   - or after BOOT_MOST_S seconds whatever happens.
 * (--off): TERM, a DRM master for 1 s, or OFF_MOST_S.
 * It says when it started and why it stopped, with what a frame cost, in the
 * kernel's log (/dev/kmsg: the boot log has it; at turn-off ramoops).
 *
 * splash.bin (little-endian), from os/tools/make-logo.py:
 *   "APSPLSH1", u16 W, u16 H, u16 256, 256 x RGB, W x H u8: the still (the
 *     kernel's picture), as indices into those colours
 *   i32 cx, cy: the dial's centre, in 1/256 px (pixel x's centre is x + 0.5)
 *   u32 n; n x u16 x; n x u16 y: the pixels of the band
 *   n x RGB u8: each one's colour without the signs
 *   n x 3 x i16: what full sign coverage adds to it, in 1/64 (the sign's
 *     brass less what is under it, times what the discs leave uncovered)
 *   i32 gx, gy (1/256 px), u16 gw, gh, gw x gh u8: the signs' coverage,
 *     smoothed by a round Gaussian, four samples a pixel: sample (i, j) is
 *     at (gx + i/4, gy + j/4)
 *
 * On the desk (no framebuffer): --frames DIR N writes N frames as PPM files,
 * --bench N times N frames' drawing; --data FILE reads another splash.bin.
 */
#define _GNU_SOURCE
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DATA "/usr/share/apotheca/splash.bin"
#define TURN_S 10.0        /* one turn of the band */
#define FPS 30.0
#define BUSY_S 10.0        /* --boot: the screen another program's this long, and not ours to show */
#define BOOT_MOST_S 90.0
#define OFF_MOST_S 120.0
#define FB_WAIT_S 3.0      /* --boot: how long /dev/fb0 may take to appear */
#define VBLANK_LEAD_S 0.006

static volatile sig_atomic_t stop_asked;
static void on_term(int s) { (void)s; stop_asked = 1; }

static void say(const char *fmt, ...)
{
    char line[400];
    va_list ap;
    int n = snprintf(line, sizeof line, "apotheca-splash: ");
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    /* The newline matters: a record without one is left open for more and is
     * not shown (dmesg, the boot log) until the kernel's next message. */
    strcat(line, "\n");
    int k = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (k >= 0) {
        if (write(k, line, strlen(line)) < 0) { /* nothing to do */ }
        close(k);
    }
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void sleep_until(double t)
{
    struct timespec ts;
    ts.tv_sec = (time_t)t;
    ts.tv_nsec = (long)((t - (double)ts.tv_sec) * 1e9);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (!stop_asked && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

/* ---- splash.bin */

struct splash {
    int w, h;
    uint8_t pal[768];
    const uint8_t *still;
    double cx, cy;
    uint32_t n;
    uint16_t *px, *py;
    const uint8_t *base;
    int16_t *delta;
    double gx, gy;
    int gw, gh;
    const uint8_t *grid;
    int bx0, by0, bx1, by1;    /* the band's bounding box */
};

struct reader { const uint8_t *p, *end; int bad; };
static const uint8_t *take(struct reader *r, size_t n)
{
    if (r->bad || (size_t)(r->end - r->p) < n) { r->bad = 1; return NULL; }
    const uint8_t *q = r->p;
    r->p += n;
    return q;
}
static uint32_t u16at(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t u32at(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int load(const char *path, struct splash *s)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 32 || st.st_size > (64 << 20)) { close(fd); return -1; }
    uint8_t *all = malloc(st.st_size);
    size_t got = 0;
    while (all && got < (size_t)st.st_size) {
        ssize_t k = read(fd, all + got, st.st_size - got);
        if (k <= 0) break;
        got += k;
    }
    close(fd);
    if (!all || got != (size_t)st.st_size) { free(all); return -1; }
    struct reader r = { all, all + got, 0 };
    const uint8_t *q = take(&r, 14);
    if (!q || memcmp(q, "APSPLSH1", 8) != 0 || u16at(q + 12) != 256) { free(all); return -1; }
    s->w = u16at(q + 8);
    s->h = u16at(q + 10);
    if (s->w < 1 || s->h < 1 || s->w > 4096 || s->h > 4096) { free(all); return -1; }
    q = take(&r, 768);
    if (q) memcpy(s->pal, q, 768);
    s->still = take(&r, (size_t)s->w * s->h);
    q = take(&r, 12);
    if (!q) { free(all); return -1; }
    s->cx = (int32_t)u32at(q) / 256.0;
    s->cy = (int32_t)u32at(q + 4) / 256.0;
    s->n = u32at(q + 8);
    if (s->n > (uint32_t)s->w * s->h) { free(all); return -1; }
    const uint8_t *xs = take(&r, 2 * (size_t)s->n), *ys = take(&r, 2 * (size_t)s->n);
    s->base = take(&r, 3 * (size_t)s->n);
    const uint8_t *ds = take(&r, 6 * (size_t)s->n);
    q = take(&r, 12);
    if (!q) { free(all); return -1; }
    s->gx = (int32_t)u32at(q) / 256.0;
    s->gy = (int32_t)u32at(q + 4) / 256.0;
    s->gw = u16at(q + 8);
    s->gh = u16at(q + 10);
    s->grid = take(&r, (size_t)s->gw * s->gh);
    if (r.bad || s->gw < 2 || s->gh < 2) { free(all); return -1; }
    s->px = malloc(2 * (size_t)s->n + 2);
    s->py = malloc(2 * (size_t)s->n + 2);
    s->delta = malloc(6 * (size_t)s->n + 2);
    if (!s->px || !s->py || !s->delta) return -1;
    s->bx0 = s->w; s->by0 = s->h; s->bx1 = s->by1 = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        s->px[i] = u16at(xs + 2 * i);
        s->py[i] = u16at(ys + 2 * i);
        if (s->px[i] >= s->w || s->py[i] >= s->h) return -1;
        if (s->px[i] < s->bx0) s->bx0 = s->px[i];
        if (s->px[i] >= s->bx1) s->bx1 = s->px[i] + 1;
        if (s->py[i] < s->by0) s->by0 = s->py[i];
        if (s->py[i] >= s->by1) s->by1 = s->py[i] + 1;
    }
    for (size_t i = 0; i < 3 * (size_t)s->n; i++) s->delta[i] = (int16_t)u16at(ds + 2 * i);
    if (s->n == 0) s->bx0 = s->by0 = 0;
    return 0;   /* `all` stays: still, base and grid point into it */
}

/* ---- the screen: /dev/fb0, or a buffer of our own on the desk */

struct screen {
    int fd;
    uint8_t *mem;       /* the visible picture's first byte */
    size_t maplen;
    void *map;
    int w, h, bytes, stride;
    struct fb_bitfield r, g, b, t;
    int vsync;          /* FBIO_WAITFORVSYNC works */
    int probe;          /* FBIOGET/PUTCMAP works: EBUSY says the screen is another program's */
    struct fb_cmap cmap;
    uint16_t cr, cg, cb;
};

static uint32_t pack(const struct screen *sc, int r, int g, int b)
{
    uint32_t v = (uint32_t)(r >> (8 - (int)sc->r.length)) << sc->r.offset
               | (uint32_t)(g >> (8 - (int)sc->g.length)) << sc->g.offset
               | (uint32_t)(b >> (8 - (int)sc->b.length)) << sc->b.offset;
    if (sc->t.length) v |= ((1u << sc->t.length) - 1) << sc->t.offset;
    return v;
}

static void put(uint8_t *row, int x, int bytes, uint32_t v)
{
    uint8_t *p = row + (size_t)x * bytes;
    switch (bytes) {
    case 4: memcpy(p, &v, 4); break;
    case 2: { uint16_t h = (uint16_t)v; memcpy(p, &h, 2); break; }
    default: p[0] = v; p[1] = v >> 8; p[2] = v >> 16; break;
    }
}

static int open_fb(struct screen *sc, const char *dev, double wait_s)
{
    double until = now_s() + wait_s;
    for (;;) {
        sc->fd = open(dev, O_RDWR | O_CLOEXEC);
        if (sc->fd >= 0 || now_s() >= until || stop_asked) break;
        usleep(50000);
    }
    if (sc->fd < 0) return -1;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(sc->fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(sc->fd, FBIOGET_FSCREENINFO, &fix) != 0) return -1;
    if (fix.type != FB_TYPE_PACKED_PIXELS || (fix.visual != FB_VISUAL_TRUECOLOR && fix.visual != FB_VISUAL_DIRECTCOLOR))
        return -1;
    if (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) return -1;
    if (var.red.length < 1 || var.red.length > 8 || var.green.length < 1 || var.green.length > 8 ||
        var.blue.length < 1 || var.blue.length > 8) return -1;
    sc->w = var.xres;
    sc->h = var.yres;
    sc->bytes = var.bits_per_pixel / 8;
    sc->stride = fix.line_length;
    sc->r = var.red; sc->g = var.green; sc->b = var.blue; sc->t = var.transp;
    sc->maplen = fix.smem_len;
    size_t first = (size_t)var.yoffset * sc->stride + (size_t)var.xoffset * sc->bytes;
    if (sc->maplen < first + (size_t)sc->h * sc->stride) return -1;
    sc->map = mmap(NULL, sc->maplen, PROT_READ | PROT_WRITE, MAP_SHARED, sc->fd, 0);
    if (sc->map == MAP_FAILED) return -1;
    sc->mem = (uint8_t *)sc->map + first;
    uint32_t zero = 0;
    sc->vsync = ioctl(sc->fd, FBIO_WAITFORVSYNC, &zero) == 0;
    sc->cmap.start = 0;
    sc->cmap.len = 1;
    sc->cmap.red = &sc->cr; sc->cmap.green = &sc->cg; sc->cmap.blue = &sc->cb;
    sc->cmap.transp = NULL;
    sc->probe = ioctl(sc->fd, FBIOGETCMAP, &sc->cmap) == 0;
    return 0;
}

/* The fbdev answers EBUSY while another program holds the display (a DRM
 * master): setting its first colour to what it is changes nothing. */
static int screen_busy(struct screen *sc)
{
    if (!sc->probe) return 0;
    if (ioctl(sc->fd, FBIOPUTCMAP, &sc->cmap) == 0) return 0;
    if (errno == EBUSY) return 1;
    sc->probe = 0;
    return 0;
}

/* ---- the display's CRTC, to see Apotheca's picture arrive (--boot) */

struct watch { int fd; uint32_t crtc, fb; };

static void watch_open(struct watch *wt)
{
    wt->fd = -1;
    for (int k = 0; k < 8; k++) {
        char dev[32];
        snprintf(dev, sizeof dev, "/dev/dri/card%d", k);
        int fd = open(dev, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        /* The first to open a card is its master: given up at once, so
         * Apotheca, opening it later, is (no master: the next opener is). */
        ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
        struct drm_mode_card_res res;
        memset(&res, 0, sizeof res);
        uint32_t crtcs[16];
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0 && res.count_crtcs > 0) {
            uint32_t n = res.count_crtcs > 16 ? 16 : res.count_crtcs;
            memset(&res, 0, sizeof res);
            res.count_crtcs = n;
            res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
            if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0) {
                if (res.count_crtcs < n) n = res.count_crtcs;
                for (uint32_t i = 0; i < n; i++) {
                    struct drm_mode_crtc c;
                    memset(&c, 0, sizeof c);
                    c.crtc_id = crtcs[i];
                    if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &c) == 0 && c.fb_id != 0) {
                        wt->fd = fd; wt->crtc = c.crtc_id; wt->fb = c.fb_id;
                        return;
                    }
                }
            }
        }
        close(fd);
    }
}

/* 1 when the CRTC shows another framebuffer than the fbdev's (or none). */
static int watch_changed(struct watch *wt)
{
    if (wt->fd < 0) return 0;
    struct drm_mode_crtc c;
    memset(&c, 0, sizeof c);
    c.crtc_id = wt->crtc;
    if (ioctl(wt->fd, DRM_IOCTL_MODE_GETCRTC, &c) != 0) return 0;
    return c.fb_id != wt->fb;
}

/* ---- drawing */

/* The still (the kernel's picture) into the copy, and black below it. */
static void draw_still(const struct splash *s, const struct screen *sc, uint8_t *copy)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = pack(sc, s->pal[3 * i], s->pal[3 * i + 1], s->pal[3 * i + 2]);
    uint32_t black = pack(sc, 0, 0, 0);
    for (int y = 0; y < sc->h; y++) {
        uint8_t *row = copy + (size_t)y * sc->stride;
        for (int x = 0; x < sc->w; x++)
            put(row, x, sc->bytes, (y < s->h && x < s->w) ? lut[s->still[(size_t)y * s->w + x]] : black);
    }
}

/* The band, its signs turned by `turn` radians (clockwise on the screen). */
static void draw_band(const struct splash *s, const struct screen *sc, uint8_t *copy, double turn)
{
    const float c = (float)cos(turn), sn = (float)sin(turn);
    const float cx = (float)s->cx, cy = (float)s->cy;
    const float ox = (float)((s->cx - s->gx) * 4.0), oy = (float)((s->cy - s->gy) * 4.0);
    const int gw = s->gw, gh = s->gh;
    for (uint32_t i = 0; i < s->n; i++) {
        const float dx = s->px[i] + 0.5f - cx, dy = s->py[i] + 0.5f - cy;
        /* where this pixel was before the turn, in the grid's samples */
        const float u = (c * dx + sn * dy) * 4.0f + ox, v = (-sn * dx + c * dy) * 4.0f + oy;
        int a = 0;
        if (u >= 0.0f && v >= 0.0f) {
            const int iu = (int)u, iv = (int)v;
            if (iu < gw - 1 && iv < gh - 1) {
                const int fu = (int)((u - iu) * 256.0f), fv = (int)((v - iv) * 256.0f);
                const uint8_t *g = s->grid + (size_t)iv * gw + iu;
                const int top = g[0] * (256 - fu) + g[1] * fu, bottom = g[gw] * (256 - fu) + g[gw + 1] * fu;
                a = (top * (256 - fv) + bottom * fv) >> 8;   /* 0 .. 255 x 256 */
            }
        }
        int rgb[3];
        for (int k = 0; k < 3; k++) {
            int v8 = s->base[3 * i + k];
            if (a) {
                /* a / (255 x 256) of delta / 64 */
                const int64_t add = (int64_t)a * s->delta[3 * i + k];
                v8 += (int)((add + (add >= 0 ? 2088960 : -2088960)) / 4177920);
                v8 = v8 < 0 ? 0 : v8 > 255 ? 255 : v8;
            }
            rgb[k] = v8;
        }
        put(copy + (size_t)s->py[i] * sc->stride, s->px[i], sc->bytes, pack(sc, rgb[0], rgb[1], rgb[2]));
    }
}

static void show(const struct screen *sc, const uint8_t *copy, int y0, int y1, int x0, int x1)
{
    if (y1 > sc->h) y1 = sc->h;
    if (x1 > sc->w) x1 = sc->w;
    for (int y = y0; y < y1; y++)
        memcpy(sc->mem + (size_t)y * sc->stride + (size_t)x0 * sc->bytes,
               copy + (size_t)y * sc->stride + (size_t)x0 * sc->bytes, (size_t)(x1 - x0) * sc->bytes);
}

/* ---- the desk: frames as PPM files, and timing */

static int desk(const struct splash *s, const char *dir, int frames, int bench)
{
    struct screen sc;
    memset(&sc, 0, sizeof sc);
    sc.w = 640; sc.h = 480; sc.bytes = 4; sc.stride = 640 * 4;
    sc.r.offset = 16; sc.g.offset = 8; sc.b.offset = 0;
    sc.r.length = sc.g.length = sc.b.length = 8;
    uint8_t *copy = calloc((size_t)sc.stride, sc.h);
    sc.mem = calloc((size_t)sc.stride, sc.h);
    if (!copy || !sc.mem) return 1;
    draw_still(s, &sc, copy);
    if (bench) {
        double t0 = now_s(), most = 0;
        for (int f = 0; f < bench; f++) {
            double a = now_s();
            draw_band(s, &sc, copy, 2 * M_PI * f / (TURN_S * FPS));
            show(&sc, copy, s->by0, s->by1, s->bx0, s->bx1);
            double took = now_s() - a;
            if (took > most) most = took;
        }
        double each = (now_s() - t0) / bench;
        printf("apotheca-splash: %d frames, %.1f us a frame (the band, %u pixels, and its copy to the screen), "
               "at most %.1f us; %.2f%% of one core at %.0f frames a second\n",
               bench, each * 1e6, s->n, most * 1e6, each * FPS * 100, FPS);
        return 0;
    }
    for (int f = 0; f < frames; f++) {
        if (f > 0) draw_band(s, &sc, copy, 2 * M_PI * f / (TURN_S * FPS));
        show(&sc, copy, 0, sc.h, 0, sc.w);
        char path[1024];
        snprintf(path, sizeof path, "%s/frame-%04d.ppm", dir, f);
        FILE *o = fopen(path, "wb");
        if (!o) return 1;
        fprintf(o, "P6\n%d %d\n255\n", sc.w, sc.h);
        for (int i = 0; i < sc.w * sc.h; i++) {
            const uint8_t *p = sc.mem + 4 * (size_t)i;
            fputc(p[2], o); fputc(p[1], o); fputc(p[0], o);
        }
        fclose(o);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = DATA, *dir = NULL, *dev = "/dev/fb0";
    int boot = 0, off = 0, frames = 0, bench = 0, fg = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--boot")) boot = 1;
        else if (!strcmp(argv[i], "--off")) off = 1;
        else if (!strcmp(argv[i], "--foreground")) fg = 1;
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--fb") && i + 1 < argc) dev = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 2 < argc) { dir = argv[++i]; frames = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: apotheca-splash --boot | --off [--foreground] [--data FILE] [--fb DEV]\n"
                            "       apotheca-splash [--data FILE] --frames DIR N | --bench N\n");
            return 2;
        }
    }
    struct splash s;
    if (dir || bench) {
        if (load(data, &s) != 0) { fprintf(stderr, "apotheca-splash: %s: not a splash file\n", data); return 1; }
        return desk(&s, dir, frames, bench);
    }
    if (boot == off) return 2;
    const char *what = boot ? "start-up" : "turn-off";

    /* Into the background at once: nothing waits for this. */
    if (!fg) {
        pid_t p = fork();
        if (p < 0) return 0;
        if (p > 0) { printf("%d\n", (int)p); return 0; }
        setsid();
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
    }
    if (chdir("/") != 0) { /* nothing to do */ }
    setpriority(PRIO_PROCESS, 0, 10);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (load(data, &s) != 0) { say("%s: %s is missing or not a splash file: not drawn", what, data); return 0; }
    struct screen sc;
    memset(&sc, 0, sizeof sc);
    if (open_fb(&sc, dev, boot ? FB_WAIT_S : 0.0) != 0) { say("%s: no usable %s: not drawn", what, dev); return 0; }
    if (sc.w < s.w || sc.h < s.h) { say("%s: the screen is %dx%d, smaller than the picture: not drawn", what, sc.w, sc.h); return 0; }
    struct watch wt = { -1, 0, 0 };
    if (boot) watch_open(&wt);
    uint8_t *copy = malloc((size_t)sc.stride * sc.h);
    if (!copy) return 0;
    draw_still(&s, &sc, copy);
    show(&sc, copy, 0, sc.h, 0, sc.w);
    say("%s: the band turning (pid %d; %dx%d, %d bits, %s, %s, %s)", what, (int)getpid(), sc.w, sc.h, sc.bytes * 8,
        sc.vsync ? "drawn after each vertical blank" : "no vertical blank: timed by the clock",
        sc.probe ? "sees another program take the screen" : "cannot see another program take the screen",
        wt.fd >= 0 ? "watches the display for Apotheca's picture" : "no display to watch");

    const double began = now_s(), most = boot ? BOOT_MOST_S : OFF_MOST_S, busy_s = boot ? BUSY_S : 1.0;
    const double period = 1.0 / FPS;
    double next = began + period, busy_since = 0, drawn = 0, worst = 0;
    long frame = 0;
    const char *why = "TERM";
    while (!stop_asked) {
        double t = now_s();
        if (t - began > most) { why = boot ? "the longest a start-up may show it" : "the longest a turn-off may show it"; break; }
        if (watch_changed(&wt)) { why = "Apotheca's picture is on the screen"; break; }
        if (screen_busy(&sc)) {
            if (busy_since == 0) busy_since = t;
            else if (t - busy_since >= busy_s) { why = "another program has had the screen"; break; }
        } else {
            busy_since = 0;
        }
        frame++;
        double a = now_s();
        draw_band(&s, &sc, copy, 2 * M_PI * (next - began) / TURN_S);
        double took = now_s() - a;
        drawn += took;
        if (took > worst) worst = took;
        if (sc.vsync) {
            sleep_until(next - VBLANK_LEAD_S);
            uint32_t zero = 0;
            if (ioctl(sc.fd, FBIO_WAITFORVSYNC, &zero) != 0 && errno != EINTR) sc.vsync = 0;
        } else {
            sleep_until(next);
        }
        if (stop_asked) break;
        /* Every second the whole picture again (a console redraw would wipe it), else the band. */
        if (frame % (long)FPS == 0) show(&sc, copy, 0, sc.h, 0, sc.w);
        else show(&sc, copy, s.by0, s.by1, s.bx0, s.bx1);
        t = now_s();
        next = (sc.vsync ? t : next) + period;
        if (next < t) next = t + period;    /* fell behind: go on from now */
    }
    say("%s: stopped (%s) after %.1f s, %ld frames, %.2f ms a frame to draw (at most %.2f)", what, why,
        now_s() - began, frame, frame ? drawn / frame * 1e3 : 0.0, worst * 1e3);
    return 0;
}
