#!/bin/sh
# panel-dtb.sh IN.dtb PANEL.panel OUT.dtb [NODE]
#
# Puts a whole panel file into a device tree: the bytes of PANEL.panel become
# the property "apotheca,panel-firmware" of NODE (default /spi/panel@0, the
# RG40XXV's panel), and the panel driver uses them instead of the panel file
# built into the kernel (patch 0170). So a change to the panel's start-up
# commands or timings is one file, OUT.dtb, copied over dtb.img on the card's
# APOTHBOOT drive: no new kernel, no new card.
#
# A panel file is kikuchan's panel-firmware format
# (https://kikuchan.github.io/panel-firmware-generator/ makes one; so does
# os/tools/make-v2-stock-panel.py). Needs fdtput and fdtget (from dtc: the
# image build makes them in build-image/output/host/bin, which is tried first).
set -eu
[ $# -ge 3 ] || { echo "usage: $0 IN.dtb PANEL.panel OUT.dtb [NODE]" >&2; exit 2; }
in=$1 panel=$2 out=$3 node=${4:-/spi/panel@0}
here=$(cd "$(dirname "$0")/../.." && pwd)
PATH="$here/build-image/output/host/bin:$PATH"

[ "$(head -c 15 "$panel" | tr -d '\000')" = "PANEL-FIRMWARE" ] ||
    { echo "$panel is not a panel file (no PANEL-FIRMWARE header)" >&2; exit 1; }
fdtget "$in" "$node" compatible > /dev/null ||
    { echo "$in has no node $node" >&2; exit 1; }

cp "$in" "$out.part"
# shellcheck disable=SC2046 # one argument a byte
fdtput -t bx "$out.part" "$node" apotheca,panel-firmware $(od -An -v -tx1 "$panel")
# Read back: the property holds the file, byte for byte.
[ "$(fdtget -t bx "$out.part" "$node" apotheca,panel-firmware | tr -s ' \n' '  ' | sed 's/ *$//')" = \
  "$(od -An -v -tx1 "$panel" | tr -s ' \n' '  ' | sed 's/^ *//; s/ *$//' | sed 's/\b0\([0-9a-f]\)/\1/g')" ] ||
    { rm -f "$out.part"; echo "the property did not read back as the file" >&2; exit 1; }
mv "$out.part" "$out"
echo "$out: $node apotheca,panel-firmware = $panel ($(wc -c < "$panel") bytes)"
