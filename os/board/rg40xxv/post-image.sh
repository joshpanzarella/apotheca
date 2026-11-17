#!/bin/sh
# Runs after every image part is built. Puts the boot partition together,
# makes the empty data partition and writes the SD card image.
#
# Environment (from scripts/build-image.sh):
#   APOTHECA_DTB   which RG40XXV device tree becomes dtb.img:
#                  "rg40xx-v-v2-panel" (default: the newer screen, the
#                  owner's unit) or "rg40xx-v" (the first screen)
set -eu

board="$(cd "$(dirname "$0")" && pwd)"
images="$BINARIES_DIR"
dtb="${APOTHECA_DTB:-rg40xx-v-v2-panel}"

# The boot partition's files
rm -rf "$images/boot"
mkdir -p "$images/boot/device_trees"
cp "$images/Image" "$images/boot/Image"
cp "$images"/allwinner/sun50i-h700-anbernic-rg40xx-v*.dtb "$images/boot/device_trees/"
cp "$images/boot/device_trees/sun50i-h700-anbernic-$dtb.dtb" "$images/boot/dtb.img"
cp -r "$board/boot-files/extlinux" "$images/boot/"
cp "$board/boot-files/README.txt" "$images/boot/README.txt"
echo "sun50i-h700-anbernic-$dtb.dtb" > "$images/boot/dtb-in-use.txt"
# FAT keeps dates: give every file the build's date, and mkfs.vfat
# --invariant (genimage.cfg) its own fixed ones, so the image is the same from
# one build to the next.
find "$images/boot" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-0}" {} +

# The data partition starts as zeros: the first boot makes it
# (/etc/init.d/S05data).
rm -f "$images/empty-data.img" "$images/data.exfat"
truncate -s 32M "$images/empty-data.img"

# The second system slot, empty until batch 5.
rm -f "$images/empty-slot.img"
truncate -s 1M "$images/empty-slot.img"

# The image
tmp="$BUILD_DIR/genimage.tmp"
rm -rf "$tmp"
"$HOST_DIR/bin/genimage" \
    --rootpath "$TARGET_DIR" \
    --tmppath "$tmp" \
    --inputpath "$images" \
    --outputpath "$images" \
    --config "$board/genimage.cfg"
rm -rf "$tmp"

gzip -n -f -k -6 "$images/apotheca-rg40xxv.img"
