#!/usr/bin/env python3
"""Draws the start-up picture (SHAPETAKER, APOTHECA) that the kernel shows in
place of its penguins, and writes it as the 224-colour PPM the kernel builds
in (os/board/rg40xxv/logo/logo_apotheca_clut224.ppm; CONFIG_LOGO_LINUX_CLUT224_FILE
in linux.fragment, copied into the kernel tree by external.mk).

    python3 os/tools/make-logo.py [--png PREVIEW.png]

Needs Pillow. The lettering is the app's own (assets/fonts/routed-gothic-narrow.ttf
on main, found beside this tree or named with --font), the colours the BROWN
theme's (app/ui.hpp). Drawn at four times the size and made smaller, so the
edges are smooth; then held to 220 colours (the kernel takes 224).
"""
import argparse, math, os, sys

from PIL import Image, ImageDraw, ImageFont

W, H = 640, 360          # the screen is 640 wide; the console's text starts under it
S = 4                    # drawn this many times larger
BG = (0, 0, 0)           # the console's own black, so the picture has no edge
CREAM = (224, 213, 191)  # BROWN's text
BRASS = (201, 161, 90)
TEAL = (111, 191, 163)
PURPLE = (154, 118, 176)
DIM = (143, 131, 112)
LINE = (67, 57, 47)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "board", "rg40xxv", "logo", "logo_apotheca_clut224.ppm")


def font_path(given):
    for p in [given, os.path.join(HERE, "..", "..", "assets", "fonts", "routed-gothic-narrow.ttf"),
              "/home/user/apotheca/assets/fonts/routed-gothic-narrow.ttf"]:
        if p and os.path.exists(p):
            return p
    sys.exit("make-logo: the app's font was not found; name it with --font")


def spaced(d, cx, y, text, font, fill, gap):
    """Text centred on cx, each letter `gap` apart beyond its own width."""
    widths = [d.textlength(c, font=font) for c in text]
    total = sum(widths) + gap * (len(text) - 1)
    x = cx - total / 2
    for c, w in zip(text, widths):
        d.text((x, y), c, font=font, fill=fill)
        x += w + gap
    return cx - total / 2, cx + total / 2


def draw(font):
    img = Image.new("RGB", (W * S, H * S), BG)
    d = ImageDraw.Draw(img)
    s = lambda v: int(round(v * S))

    # The emblem: a circle, a triangle in it and a bar across (the old sign
    # for air), a small circle at its heart; the theme's four accents round it.
    cx, cy, r = 320, 98, 66
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], outline=BRASS, width=s(3))
    d.ellipse([s(cx - r + 9), s(cy - r + 9), s(cx + r - 9), s(cy + r - 9)], outline=LINE, width=s(1.5))
    tr = r - 9
    pts = [(cx + tr * math.cos(math.radians(a)), cy - tr * math.sin(math.radians(a))) for a in (90, 210, 330)]
    d.line([(s(x), s(y)) for x, y in pts + pts[:1]], fill=CREAM, width=s(2.5), joint="curve")
    bar = cy + tr * 0.12
    half = (bar - pts[0][1]) / (pts[1][1] - pts[0][1]) * (pts[2][0] - pts[1][0]) / 2 + 14
    d.line([(s(cx - half), s(bar)), (s(cx + half), s(bar))], fill=CREAM, width=s(2.5))
    d.ellipse([s(cx - 9), s(cy - 4 - 9), s(cx + 9), s(cy - 4 + 9)], outline=BRASS, width=s(2))
    for ang, col in ((90, TEAL), (270, PURPLE), (0, BRASS), (180, BRASS)):
        x, y = cx + r * math.cos(math.radians(ang)), cy - r * math.sin(math.radians(ang))
        d.ellipse([s(x - 5), s(y - 5), s(x + 5), s(y + 5)], fill=col, outline=BG, width=s(2))

    # SHAPETAKER, small and wide, between two brass rules.
    small = ImageFont.truetype(font, s(21))
    left, right = spaced(d, s(320), s(186), "SHAPETAKER", small, BRASS, s(11))
    y = s(186) + s(13)
    d.line([(s(150), y), (left - s(16), y)], fill=BRASS, width=s(1.5))
    d.line([(right + s(16), y), (s(490), y)], fill=BRASS, width=s(1.5))

    # APOTHECA, large.
    big = ImageFont.truetype(font, s(76))
    spaced(d, s(320), s(218), "APOTHECA", big, CREAM, s(9))
    # A dim rule under it, teal and purple at its ends.
    y = s(318)
    d.line([(s(210), y), (s(430), y)], fill=DIM, width=s(1.5))
    d.ellipse([s(204), y - s(3), s(210), y + s(3)], fill=TEAL)
    d.ellipse([s(430), y - s(3), s(436), y + s(3)], fill=PURPLE)

    small = img.resize((W, H), Image.LANCZOS)
    return small.quantize(colors=220, method=Image.FASTOCTREE, dither=Image.NONE).convert("RGB")


def write_ppm(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
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
    return len(set(img.get_flattened_data()))


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--font")
    a.add_argument("--png")
    a.add_argument("--out", default=OUT)
    o = a.parse_args()
    img = draw(font_path(o.font))
    n = write_ppm(img, o.out)
    if o.png:
        img.save(o.png)
    print("make-logo: %s, %dx%d, %d colours" % (os.path.relpath(o.out), img.size[0], img.size[1], n))
    return 0 if n <= 224 else 1


if __name__ == "__main__":
    sys.exit(main())
