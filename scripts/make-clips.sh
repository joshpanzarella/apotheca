#!/bin/bash
# The feature clips (desk only; docs/dev/videos.md): each script in clips/ played against the desk build, its
# frames and sound encoded to videos/NN-name.mp4 (H.264, yuv420p, 30 fps, the screen doubled with nearest-neighbour
# to 1280x960 and the key band under it, 1280x1120 in all; AAC sound), with its narration as NN-name.md and
# NN-name.srt. videos/ is not kept in git.
#
#   scripts/make-clips.sh                  every clip, into videos/ at the checkout's top
#   scripts/make-clips.sh 01 04            just those (by the number or any part of the name)
#   VIDEOS=/some/folder scripts/make-clips.sh   somewhere else
#   BUILD=/path/to/build                   another build folder (default: build/ beside this script's checkout)
#
# A clip's script names the program's options on its `run` line (--demo --view NAME ...). "$SAMPLES" in it is a
# folder of recordings made here for the clips that need them (scripts/clip_samples.py).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD:-$root/build}
videos=${VIDEOS:-$root/videos}
work=${WORK:-$(mktemp -d "${TMPDIR:-/tmp}/apotheca-clips-XXXXXX")}
mkdir -p "$videos" "$work"
[ -x "$build/apotheca" ] || { echo "no $build/apotheca: build it first"; exit 1; }
SAMPLES=$work/samples
python3 "$root/scripts/clip_samples.py" "$SAMPLES"

for script in "$root"/clips/*.clip; do
    name=$(basename "$script" .clip)
    if [ $# -gt 0 ]; then
        wanted=0
        for w in "$@"; do case $name in *"$w"*) wanted=1 ;; esac; done
        [ $wanted = 1 ] || continue
    fi
    out=$work/$name
    rm -rf "$out"
    run=$(sed -n 's/^run //p' "$script" | head -1)
    run=${run//\$SAMPLES/$SAMPLES}
    echo "== $name: apotheca $run"
    # (a still in every way: no songs, settings or recordings of the desk's own are touched)
    # shellcheck disable=SC2086
    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$build/apotheca" $run --script "$script" --record "$out" > "$out.log" 2>&1 \
        || { cat "$out.log"; echo "$name: the recording failed"; exit 1; }
    tail -1 "$out.log"
    w=$(sed -n 's/^width //p' "$out/clip.txt"); h=$(sed -n 's/^height //p' "$out/clip.txt")
    ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s "${w}x${h}" -r 30 -i "$out/frames.rgb" -i "$out/sound.wav" \
        -vf "scale=$((w * 2)):$((h * 2)):flags=neighbor" -c:v libx264 -preset slow -tune animation -crf 22 -pix_fmt yuv420p \
        -c:a aac -b:a 160k -movflags +faststart "$videos/$name.mp4"
    cp "$out/clip.md" "$videos/$name.md"
    cp "$out/clip.srt" "$videos/$name.srt"
    rm -f "$out/frames.rgb" # (half a gigabyte a clip)
    echo "   $videos/$name.mp4 ($(du -h "$videos/$name.mp4" | cut -f1))"
done
[ -n "${WORK:-}" ] || rm -rf "$work"
