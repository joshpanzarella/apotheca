#!/usr/bin/env bash
# Remakes the manual's pictures (docs/manual/*.png, 640x480) from the desk build's stills.
#
#   scripts/manual-shots.sh [BUILD_DIR]      (default: build)
#
# Each picture is a --view of `apotheca --demo` (docs/dev/stills.md), in the default theme
# (Brown) at the normal text size, saved as BMP and turned into PNG. The kit and the sampler
# want recordings: a few short WAVs are made here (Python's own `wave` module) in a folder of
# their own and given with --samples, so a still is the same on every desk. Views where the
# demo plays (song, mod, bounce) differ a little run to run; they show the same screen.
# Needs: python3, and ImageMagick (`magick` or `convert`) or Pillow for the PNGs.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
app="$build/apotheca"
out="$root/docs/manual"
[ -x "$app" ] || { echo "no $app: build the desk first" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/samples"

# The recordings: a break (kicks, snares and hats, one bar at 120 BPM: 2 s), a hit and a pad.
python3 - "$work/samples" <<'EOF'
import math, random, struct, sys, wave
random.seed(7)
rate = 44100
def write(name, frames):
    with wave.open(sys.argv[1] + "/" + name, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 30000)) for s in frames))
def hit(kind, n):
    out = []
    for i in range(n):
        t = i / rate
        if kind == "kick": s = math.sin(2 * math.pi * (50 + 120 * math.exp(-t * 30)) * t) * math.exp(-t * 8)
        elif kind == "snare": s = (random.uniform(-1, 1) * 0.7 + 0.3 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 18)
        else: s = random.uniform(-1, 1) * 0.4 * math.exp(-t * 60)
        out.append(s)
    return out
step = rate // 8  # a 16th at 120 BPM
pattern = ["kick", "hat", "snare", "hat", "kick", "kick", "snare", "hat"] * 2
brk = []
for k in pattern: brk += hit(k, step)
write("break.wav", brk)
write("snap.wav", hit("snare", rate // 3))
write("pad.wav", [0.5 * math.sin(2 * math.pi * 220 * i / rate) * min(1, i / 4000) for i in range(rate * 2)])
EOF

png() { # BMP -> PNG
    if command -v magick >/dev/null; then magick "$1" "$2"
    elif command -v convert >/dev/null; then convert "$1" "$2"
    else python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])' "$1" "$2"; fi
}

shot() { # NAME VIEW [options...]
    local name="$1" view="$2"
    shift 2
    (cd "$work" && SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$app" --demo --view "$view" --shot "$work/$name.bmp" "$@" >/dev/null 2>&1)
    png "$work/$name.bmp" "$out/$name.png"
    echo "$name.png  <- --view $view $*"
}

shot bars-roll      bars-roll
shot bars-stop-grid bars-stop-grid
shot bars-stop-roll bars-stop-roll
shot bounce         bounce
shot chords         chords
shot drums          drums
shot fill           fill
shot fx             fx
shot kit            kit-source --samples "$work/samples"
shot mod            mod
shot notes          notes
shot phazed         phazed
shot sampler        sampler --samples "$work/samples"
shot setup-network  setup-network
shot song           song
shot volume         volume
shot wifi           wifi
