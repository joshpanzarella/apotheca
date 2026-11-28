#!/usr/bin/env python3
"""Draws the start-up picture that the kernel shows in place of its penguins: the ASTROLABE, riff A "brass glow"
(owner, 2026-11-24, picked from three designs and three riffs on the one he liked). An engraved brass dial with a
warm glow behind it, the seven old planet signs round its band, the four element signs at its compass points, the
air sign at its heart; SHAPETAKER between brass rules; APOTHECA engraved (a brass shadow); a scale under it.
It writes the 224-colour PPM the kernel builds in (os/board/rg40xxv/logo/logo_apotheca_clut224.ppm;
CONFIG_LOGO_LINUX_CLUT224_FILE in linux.fragment, copied into the kernel tree by external.mk).
It also writes splash.bin (os/package/apotheca-tools/src/, installed as /usr/share/apotheca/splash.bin): the same
picture to the pixel, and the planet band in pieces for apotheca-splash.c, which turns the signs at start-up and at
turn-off. The kernel's picture is not changed by it (--splash none writes the PPM alone). Needs numpy for that.

    python3 os/tools/make-logo.py [--png PREVIEW.png]

Needs Pillow. APOTHECA is in the app's own lettering (assets/fonts/routed-gothic-narrow.ttf on main, found beside
this tree or named with --font); the planet signs are DejaVu Sans's (--symbols; its licence lets them be drawn into
a picture). SHAPETAKER is the owner's Neokarut with its alt K and R, kept as a picture
(os/board/rg40xxv/logo/shapetaker-wordmark.png, cut from the first logo) so the font is not needed. The colours are
the BROWN theme's (app/ui.hpp). Drawn at four times the size and made smaller; then held to 220 colours (the kernel
takes 224), the accents and the glow's every shade kept exactly so the glow has no edge.
"""
import argparse, math, os, sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H, S = 640, 464, 4    # the screen is 640x480; the kernel shows a logo at most 464 tall (the console keeps a row)
HERE = os.path.dirname(os.path.abspath(__file__))
LOGO = os.path.join(HERE, "..", "board", "rg40xxv", "logo")
OUT = os.path.join(LOGO, "logo_apotheca_clut224.ppm")
WORDMARK = os.path.join(LOGO, "shapetaker-wordmark.png")
WORD_AT = (205, 254)     # where the wordmark stands
GLOW = (58, 40, 16)
BG=(0,0,0); CREAM=(224,213,191); BRASS=(201,161,90); BRASS_HI=(236,204,140); BRASS_LO=(120,92,48)
TEAL=(111,191,163); PURPLE=(154,118,176); DIM=(143,131,112); LINE=(67,57,47)
def s(v): return int(round(v * S))
def hash01(i):
    v = (i * 2654435761) & 0xffffffff; v ^= v >> 15; v = (v * 0x2c1b3c6d) & 0xffffffff; v ^= v >> 12
    return (v & 0xffff) / 65535.0
def spaced(d, cx, y, text, font, fill, gap):
    widths = [d.textlength(c, font=font) for c in text]
    total = sum(widths) + gap * (len(text) - 1); x = cx - total / 2
    for c, w in zip(text, widths): d.text((x, y), c, font=font, fill=fill); x += w + gap
def ring(d, cx, cy, r, col, w): d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], outline=col, width=s(w))
def polar(cx, cy, r, deg): a = math.radians(deg); return cx + r * math.cos(a), cy + r * math.sin(a)
def triangle_mark(d, cx, cy, tr, col=CREAM, w=2.5):
    pts = [(cx + tr * math.cos(math.radians(a)), cy - tr * math.sin(math.radians(a))) for a in (90, 210, 330)]
    d.line([(s(x), s(y)) for x, y in pts + pts[:1]], fill=col, width=s(w), joint="curve")
    bar = cy + tr * 0.12
    half = (bar - pts[0][1]) / (pts[1][1] - pts[0][1]) * (pts[2][0] - pts[1][0]) / 2 + tr * 0.25
    d.line([(s(cx - half), s(bar)), (s(cx + half), s(bar))], fill=col, width=s(w))
def glow(img, cx, cy, r, col, strength):
    g = Image.new("L", img.size, 0); gd = ImageDraw.Draw(g)
    gd.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=int(255 * strength))
    g = g.filter(ImageFilter.GaussianBlur(s(r * 0.45)))
    img.paste(Image.composite(Image.new("RGB", img.size, col), img, g), (0, 0))
def elem(d, x, y, up, bar, col):
    t = 7; sg = -1 if up else 1
    pts = [(x, y + sg * t), (x - t * 0.9, y - sg * t * 0.6), (x + t * 0.9, y - sg * t * 0.6)]
    d.polygon([(s(a), s(b)) for a, b in pts], outline=col, width=s(1.6))
    if bar: d.line([(s(x - 7), s(y)), (s(x + 7), s(y))], fill=col, width=s(1.4))
def scale(d, cx, cy, r0, r1, every=5, long_every=6, col=BRASS, dim=DIM):
    for k in range(360 // every):
        lg = k % long_every == 0
        a, b = polar(cx, cy, r0 if lg else r0 + (r1 - r0) * 0.45, k * every), polar(cx, cy, r1, k * every)
        d.line([(s(a[0]), s(a[1])), (s(b[0]), s(b[1]))], fill=col if lg else dim, width=s(1.2 if lg else 0.8))
def elements(d, cx, cy, r):
    for (deg, up, bar, col) in ((-90, True, False, TEAL), (0, True, True, BRASS), (90, False, False, PURPLE), (180, False, True, BRASS)):
        x, y = polar(cx, cy, r, deg)
        d.ellipse([s(x - 11), s(y - 11), s(x + 11), s(y + 11)], fill=BG, outline=col, width=s(1.5))
        elem(d, x, y, up, bar, col)
def lockup(img, d, font, wy=254, rule_end="diamond", shadow=False, under="ticks"):
    for x0, x1 in ((150, 193), (447, 490)): d.line([(s(x0), s(wy + 13)), (s(x1), s(wy + 13))], fill=BRASS, width=s(1.5))
    for x in (146, 494):
        if rule_end == "diamond": d.regular_polygon((s(x), s(wy + 13), s(3.5)), 4, rotation=45, fill=BRASS)
        else: star(d, x, wy + 13, 6, BRASS)
    big = ImageFont.truetype(font, s(76))
    if shadow:  # engraved: a brass shadow under the cream
        spaced(d, s(320) + s(1.5), s(284) + s(2.5), "APOTHECA", big, BRASS_LO, s(9))
    spaced(d, s(320), s(284), "APOTHECA", big, CREAM, s(9))
    y = s(384)
    d.line([(s(190), y), (s(450), y)], fill=DIM, width=s(1.5))
    if under == "ticks":
        for k in range(17):
            x = 190 + k * 260 / 16; h = 5 if k % 8 == 0 else (3.5 if k % 4 == 0 else 2)
            d.line([(s(x), y - s(h)), (s(x), y + s(h))], fill=DIM, width=s(1))
    d.ellipse([s(184), y - s(3), s(190), y + s(3)], fill=TEAL); d.ellipse([s(450), y - s(3), s(456), y + s(3)], fill=PURPLE)
def star(d, x, y, r, col):
    pts = []
    for k in range(8):
        rr = r if k % 2 == 0 else r * 0.28
        pts.append(polar(x, y, rr, k * 45 - 90))
    d.polygon([(s(a), s(b)) for a, b in pts], fill=col)
def finish(img, word, wy=254, ramp=None, palette=None):
    """The picture made small, the wordmark in, held to the kernel's colours. Returns it and its palette; given a
    palette (the kernel picture's), uses that one instead (the splash's picture without the planet signs)."""
    small = img.resize((W, H), Image.LANCZOS)
    region = small.crop((WORD_AT[0], wy, WORD_AT[0] + word.size[0], wy + word.size[1]))
    small.paste(ImageChops.lighter(region, word), (WORD_AT[0], wy))
    if palette is not None:
        return small.quantize(palette=palette, dither=Image.NONE).convert("RGB"), palette
    # 220 colours at most (the kernel takes 224): the accents are kept exactly, the rest shared out.
    keep = [BG, CREAM, BRASS, BRASS_HI, BRASS_LO, TEAL, PURPLE, DIM, LINE]
    if ramp:  # the glow's fade, every shade from black to its colour, so it has no edge
        keep += [tuple(int(c * k / 40) for c in ramp) for k in range(1, 41)]
    rest = small.quantize(colors=220 - len(keep), method=Image.FASTOCTREE, dither=Image.NONE).convert("RGB")
    pal = Image.new("P", (1, 1)); flat = [c for col in keep for c in col] + [c for col in (rest.getcolors(1 << 20) or []) for c in col[1]]
    pal.putpalette(flat[:220 * 3] + [0] * (768 - len(flat[:220 * 3])))
    return small.quantize(palette=pal, dither=Image.NONE).convert("RGB"), pal


CX, CY = 320, 148        # the dial's centre
PLANETS = "☉☽☿♀♂♃♄"


def planet_signs(d, symbols, fill):
    """The seven planet signs round the band (between 84 and 96): the part the splash turns."""
    sym = ImageFont.truetype(symbols, s(12))
    for k, ch in enumerate(PLANETS):
        deg = -90 + 360 * (k + 0.5) / 7
        x, y = polar(CX, CY, 90, deg)
        d.text((s(x), s(y)), ch, font=sym, fill=fill, anchor="mm")


def draw(font, symbols, word, signs=True, palette=None):
    cx, cy = CX, CY
    # ---- A. BRASS GLOW ----
    img = Image.new("RGB", (W * S, H * S), BG)
    glow(img, cx, cy, 96, GLOW, 1.0)
    d = ImageDraw.Draw(img)
    ring(d, cx, cy, 98, LINE, 1)
    # the planet band: between 84 and 96, the seven signs
    ring(d, cx, cy, 96, BRASS_LO, 1.5); ring(d, cx, cy, 84, BRASS_LO, 1.5)
    if signs:
        planet_signs(d, symbols, BRASS)
    # a bevelled rim: the dark under, the bright over the top
    ring(d, cx, cy, 77, BRASS_LO, 5); ring(d, cx, cy, 77, BRASS, 3)
    d.arc([s(cx - 77), s(cy - 77), s(cx + 77), s(cy + 77)], 200, 330, fill=BRASS_HI, width=s(1.6))
    scale(d, cx, cy, 63, 73)
    ring(d, cx, cy, 60, LINE, 1.5); ring(d, cx, cy, 44, BRASS, 1.5)
    triangle_mark(d, cx, cy + 2, 36)
    d.ellipse([s(cx - 6), s(cy - 4), s(cx + 6), s(cy + 8)], outline=BRASS_HI, width=s(2))
    elements(d, cx, cy, 77)
    lockup(img, d, font, shadow=True)
    return finish(img, word, ramp=GLOW, palette=palette)


# ---- the splash: the same picture with its planet band turning (apotheca-splash.c) ----
SPLASH = os.path.join(HERE, "..", "package", "apotheca-tools", "src", "splash.bin")
SIGMA = 0.36             # the signs' smoothing, in pixels (a round Gaussian: one that turns with them; 0.36 is the
                         # closest round one to the kernel's Lanczos, which is square and would not turn)


def splash_layers(symbols):
    """The parts the splash needs at four times the size: the signs alone (coverage), what is under them (the glow
    and the band's rings, drawn before them) and what covers them (the rim and the four element discs, drawn after:
    the signs pass under the discs, which reach into the band)."""
    signs = Image.new("L", (W * S, H * S), 0)
    planet_signs(ImageDraw.Draw(signs), symbols, 255)
    under = Image.new("RGB", (W * S, H * S), BG)
    glow(under, CX, CY, 96, GLOW, 1.0)
    d = ImageDraw.Draw(under)
    ring(d, CX, CY, 98, LINE, 1); ring(d, CX, CY, 96, BRASS_LO, 1.5); ring(d, CX, CY, 84, BRASS_LO, 1.5)
    front = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(front)
    d.ellipse([s(CX - 77), s(CY - 77), s(CX + 77), s(CY + 77)], fill=255)
    for deg in (-90, 0, 90, 180):
        x, y = polar(CX, CY, 77, deg)
        d.ellipse([s(x - 11), s(y - 11), s(x + 11), s(y + 11)], fill=255)
    return signs, under, front


def smooth_signs(signs, x0, y0, n):
    """The signs' coverage in the n x n pixels from (x0, y0), smoothed by a round Gaussian and kept at four samples
    a pixel: sample j is at x0 + j/4 (the kernel's own pixel centre x + 0.5 is sample 4(x - x0) + 2). Turned, it
    is the same picture turned, which the kernel's square filter would not be."""
    import numpy as np
    a = np.asarray(signs, dtype=np.float64)[y0 * S:(y0 + n) * S, x0 * S:(x0 + n) * S] / 255.0
    N = 8
    m = (np.arange(-N, N + 2) - 0.5) / S
    rr = np.hypot(m[None, :], m[:, None])
    h = np.exp(-rr * rr / (2 * SIGMA * SIGMA)); h /= h.sum()
    k = a.shape[0] + h.shape[0]
    f = np.fft.irfft2(np.fft.rfft2(a, (k, k)) * np.fft.rfft2(h, (k, k)), (k, k))
    return np.clip(f[N:N + n * S, N:N + n * S], 0.0, 1.0)


def write_splash(path, kernel, pal, base, symbols):
    """splash.bin, read by apotheca-splash.c (little-endian; the layout is in that file's header)."""
    import struct
    import numpy as np
    signs, under, front = splash_layers(symbols)
    x0, y0, n = CX - 100, CY - 100, 200
    f = smooth_signs(signs, x0, y0, n)
    under1 = np.asarray(under.resize((W, H), Image.LANCZOS), dtype=np.float64)
    cover = np.asarray(front.resize((W, H), Image.LANCZOS), dtype=np.float64) / 255.0
    kern = np.asarray(kernel, dtype=np.int32)
    base = np.asarray(base, dtype=np.int32)
    # Where the turning signs can reach: the band between the smallest and largest radius they cover, and a pixel
    ys, xs = np.nonzero(f >= 0.5 / 255)
    rad = np.hypot(x0 + xs / S - CX, y0 + ys / S - CY)
    r0, r1 = rad.min() - 1.0, rad.max() + 1.0
    yy, xx = np.mgrid[0:H, 0:W]
    r = np.hypot(xx + 0.5 - CX, yy + 0.5 - CY)
    # (and the few pixels under the discs where the kernel's Lanczos lets a sign show through: two, today)
    band = (r >= r0) & (r <= r1) & ((cover < 1.0) | np.any(base != kern, axis=2))
    # Outside the band the picture without the signs is the kernel's, pixel for pixel; inside, the splash adds
    # the signs: base + coverage * (1 - front) * (BRASS - under).
    outside = ~band & np.any(base != kern, axis=2)
    if outside.any():
        sys.exit("make-logo: the splash's picture differs from the kernel's at %d pixels outside the band" % outside.sum())
    py, px = np.nonzero(band)
    delta = (1.0 - cover[py, px])[:, None] * (np.array(BRASS, dtype=np.float64)[None, :] - under1[py, px])
    f[f < 0.5 / 255] = 0.0
    nz = np.nonzero(f.max(axis=0) > 0)[0]; gx0, gx1 = nz.min(), nz.max() + 1
    nz = np.nonzero(f.max(axis=1) > 0)[0]; gy0, gy1 = nz.min(), nz.max() + 1
    grid = np.rint(f[gy0:gy1, gx0:gx1] * 255).astype(np.uint8)
    # The still as the kernel's own colours (at most 224) and one byte a pixel. (Not Pillow's quantize to a
    # palette: that one is near, not exact, by up to 6 levels.)
    used = sorted(c for _, c in kernel.getcolors(256))
    lut = {c: i for i, c in enumerate(used)}
    code = (kern[:, :, 0] << 16) | (kern[:, :, 1] << 8) | kern[:, :, 2]
    idx = np.zeros((H, W), dtype=np.uint8)
    for c, i in lut.items():
        idx[code == ((c[0] << 16) | (c[1] << 8) | c[2])] = i
    colours = [v for c in used for v in c] + [0] * (3 * (256 - len(used)))
    with open(path, "wb") as o:
        o.write(b"APSPLSH1" + struct.pack("<HHH", W, H, 256) + bytes(colours) + idx.tobytes())
        o.write(struct.pack("<ii", CX * 256, CY * 256))
        o.write(struct.pack("<I", len(px)) + px.astype("<u2").tobytes() + py.astype("<u2").tobytes())
        o.write(base[py, px].astype(np.uint8).tobytes())
        o.write(np.rint(delta * 64).astype("<i2").tobytes())
        o.write(struct.pack("<iiHH", x0 * 256 + gx0 * 64, y0 * 256 + gy0 * 64, gx1 - gx0, gy1 - gy0) + grid.tobytes())
    return len(px), r0, r1, (gx1 - gx0, gy1 - gy0)


def find(given, places, what):
    for p in [given] + places:
        if p and os.path.exists(p):
            return p
    sys.exit("make-logo: %s was not found; name it" % what)


def write_ppm(img, path):
    w, h = img.size
    px = img.load()
    with open(path, "w") as f:
        f.write("P3\n# Apotheca's start-up picture: os/tools/make-logo.py\n%d %d\n255\n" % (w, h))
        for y in range(h):
            row = []
            for x in range(w):
                row.extend(str(c) for c in px[x, y])
            for i in range(0, len(row), 15):
                f.write(" ".join(row[i:i + 15]) + "\n")
    return len(img.getcolors(1 << 20))


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--font")
    a.add_argument("--symbols")
    a.add_argument("--png")
    a.add_argument("--out", default=OUT)
    a.add_argument("--splash", default=SPLASH, help="the splash's file (apotheca-splash); 'none' writes none")
    o = a.parse_args()
    font = find(o.font, [os.path.join(HERE, "..", "..", "assets", "fonts", "routed-gothic-narrow.ttf"),
                         "/home/user/apotheca/assets/fonts/routed-gothic-narrow.ttf"], "the app's font (--font)")
    symbols = find(o.symbols, ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/Library/Fonts/DejaVuSans.ttf"], "DejaVu Sans (--symbols)")
    word = Image.open(WORDMARK).convert("RGB")
    img, pal = draw(font, symbols, word)
    n = write_ppm(img, o.out)
    if o.png:
        img.save(o.png)
    print("make-logo: %s, %dx%d, %d colours" % (os.path.relpath(o.out), img.size[0], img.size[1], n))
    if o.splash != "none":
        base, _ = draw(font, symbols, word, signs=False, palette=pal)
        k, r0, r1, g = write_splash(o.splash, img, pal, base, symbols)
        print("make-logo: %s, %d pixels turning (radius %.1f to %.1f), the signs' grid %dx%d, %d bytes"
              % (os.path.relpath(o.splash), k, r0, r1, g[0], g[1], os.path.getsize(o.splash)))
    return 0 if n <= 224 else 1


if __name__ == "__main__":
    sys.exit(main())
