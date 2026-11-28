#!/bin/sh
# Build Apotheca alone for our own firmware, as one file to copy onto a card.
#
#   scripts/build-firmware-app.sh
#
# Output: dist/firmware/apotheca (and dist/firmware/apotheca.md5). It is the
# program the card image holds (/usr/lib/apotheca/apotheca), made with the
# image's own toolchain (Buildroot's glibc and SDL2), not the port's build
# (dist/apotheca, made against Ubuntu 20.04 for the stock firmware).
#
# On the handheld it goes in the APOTHECA drive's apotheca folder, over the
# file "apotheca" there: the card finds it at the next start
# (/usr/sbin/apotheca-start: a program copied by hand), runs it, and if it
# does not get going twice puts the system's own back. os/README.md, "A new
# Apotheca without a new card", has the steps.
#
# Needs a card image built once before (scripts/build-image.sh), whose
# toolchain and libraries this reuses: a few minutes, not the image's hour.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/build-image/output"
strip="$out/host/bin/aarch64-linux-strip"
[ -x "$strip" ] || { echo "build the card image once first: scripts/build-image.sh" >&2; exit 1; }

# reconfigure, not rebuild: the version shown (BUILD N) is a cmake -D, set at
# configure time from the commit count (os/package/apotheca/apotheca.mk)
"$here/scripts/build-image.sh" -- apotheca-reconfigure

program="$out/target/usr/lib/apotheca/apotheca"
[ -f "$program" ] || { echo "no program at $program" >&2; exit 1; }
# An arm64 program (what apotheca-start checks before it runs one).
case "$(od -An -tx1 -N20 "$program" | tr -d ' \n')" in
    7f454c460201????????????????????????b700) ;;
    *) echo "$program is not an arm64 program" >&2; exit 1 ;;
esac

mkdir -p "$here/dist/firmware"
# Stripped, as the image's own copy is (Buildroot strips the target as it
# finishes an image; a package rebuilt alone is not).
"$strip" -o "$here/dist/firmware/apotheca.part" "$program"
mv "$here/dist/firmware/apotheca.part" "$here/dist/firmware/apotheca"
(cd "$here/dist/firmware" && md5sum apotheca > apotheca.md5)
echo "dist/firmware/apotheca: $(wc -c < "$here/dist/firmware/apotheca") bytes, version $(cat "$out/target/usr/lib/apotheca/VERSION"), md5 $(cut -d' ' -f1 "$here/dist/firmware/apotheca.md5")"
