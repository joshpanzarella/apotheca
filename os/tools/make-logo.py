#!/usr/bin/env python3
"""Draws the start-up picture that the kernel shows in place of its penguins: the ASTROLABE, riff A "brass glow"
(owner, 2026-11-24, picked from three designs and three riffs on the one he liked). An engraved brass dial with a
warm glow behind it, the seven old planet signs round its band, the four element signs at its compass points, the
air sign at its heart; SHAPETAKER between brass rules; APOTHECA engraved (a brass shadow); a scale under it.
It writes the 224-colour PPM the kernel builds in (os/board/rg40xxv/logo/logo_apotheca_clut224.ppm;
CONFIG_LOGO_LINUX_CLUT224_FILE in linux.fragment, copied into the kernel tree by external.mk).

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
def finish(img, word, wy=254, ramp=None):
    small = img.resize((W, H), Image.LANCZOS)
    region = small.crop((WORD_AT[0], wy, WORD_AT[0] + word.size[0], wy + word.size[1]))
    small.paste(ImageChops.lighter(region, word), (WORD_AT[0], wy))
    # 220 colours at most (the kernel takes 224): the accents are kept exactly, the rest shared out.
    keep = [BG, CREAM, BRASS, BRASS_HI, BRASS_LO, TEAL, PURPLE, DIM, LINE]
    if ramp:  # the glow's fade, every shade from black to its colour, so it has no edge
        keep += [tuple(int(c * k / 40) for c in ramp) for k in range(1, 41)]
    rest = small.quantize(colors=220 - len(keep), method=Image.FASTOCTREE, dither=Image.NONE).convert("RGB")
    pal = Image.new("P", (1, 1)); flat = [c for col in keep for c in col] + [c for col in (rest.getcolors(1 << 20) or []) for c in col[1]]
    pal.putpalette(flat[:220 * 3] + [0] * (768 - len(flat[:220 * 3])))
    return small.quantize(palette=pal, dither=Image.NONE).convert("RGB")


def draw(font, symbols, word):
    cx, cy = 320, 148
    # ---- A. BRASS GLOW ----
    img = Image.new("RGB", (W * S, H * S), BG)
    glow(img, cx, cy, 96, GLOW, 1.0)
    d = ImageDraw.Draw(img)
    ring(d, cx, cy, 98, LINE, 1)
    # the planet band: between 84 and 96, the seven signs
    sym = ImageFont.truetype(symbols, s(12))
    ring(d, cx, cy, 96, BRASS_LO, 1.5); ring(d, cx, cy, 84, BRASS_LO, 1.5)
    for k, ch in enumerate("☉☽☿♀♂♃♄"):
        deg = -90 + 360 * (k + 0.5) / 7
        x, y = polar(cx, cy, 90, deg)
        d.text((s(x), s(y)), ch, font=sym, fill=BRASS, anchor="mm")
    # a bevelled rim: the dark under, the bright over the top
    ring(d, cx, cy, 77, BRASS_LO, 5); ring(d, cx, cy, 77, BRASS, 3)
    d.arc([s(cx - 77), s(cy - 77), s(cx + 77), s(cy + 77)], 200, 330, fill=BRASS_HI, width=s(1.6))
    scale(d, cx, cy, 63, 73)
    ring(d, cx, cy, 60, LINE, 1.5); ring(d, cx, cy, 44, BRASS, 1.5)
    triangle_mark(d, cx, cy + 2, 36)
    d.ellipse([s(cx - 6), s(cy - 4), s(cx + 6), s(cy + 8)], outline=BRASS_HI, width=s(2))
    elements(d, cx, cy, 77)
    lockup(img, d, font, shadow=True)
    return finish(img, word, ramp=GLOW)


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
    o = a.parse_args()
    font = find(o.font, [os.path.join(HERE, "..", "..", "assets", "fonts", "routed-gothic-narrow.ttf"),
                         "/home/user/apotheca/assets/fonts/routed-gothic-narrow.ttf"], "the app's font (--font)")
    symbols = find(o.symbols, ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/Library/Fonts/DejaVuSans.ttf"], "DejaVu Sans (--symbols)")
    img = draw(font, symbols, Image.open(WORDMARK).convert("RGB"))
    n = write_ppm(img, o.out)
    if o.png:
        img.save(o.png)
    print("make-logo: %s, %dx%d, %d colours" % (os.path.relpath(o.out), img.size[0], img.size[1], n))
    return 0 if n <= 224 else 1


if __name__ == "__main__":
    sys.exit(main())
