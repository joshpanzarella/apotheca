#!/bin/sh
# Runs after every image part is built. Puts the boot partition together,
# makes the empty data partition and writes the SD card image.
#
# Environment (from scripts/build-image.sh):
#   APOTHECA_DTB   "auto" (default): no dtb.img, so U-Boot picks the tree
#                  by the screen this handheld has (pin PE6: uboot.fragment);
#                  or the RG40XXV device tree that becomes dtb.img and is
#                  used whatever the screen: "rg40xx-v-v2-panel" (the newer
#                  screen), "rg40xx-v" (the first screen) or
#                  "rg40xx-v-v2-stock-panel" (the newer screen, Anbernic's
#                  clock edge and timings)
set -eu

board="$(cd "$(dirname "$0")" && pwd)"
images="$BINARIES_DIR"
dtb="${APOTHECA_DTB:-auto}"

# The boot partition's files
rm -rf "$images/boot"
mkdir -p "$images/boot/device_trees"
cp "$images/Image" "$images/boot/Image"
cp "$images"/allwinner/sun50i-h700-anbernic-rg40xx-v*.dtb "$images/boot/device_trees/"
# Every tree: the green power light blinks (the kernel's heartbeat) from the
# moment Linux has the LEDs, about a second into the kernel, so a handheld
# with a dark screen still says Linux runs. apotheca-power makes it steady
# once a button has been pressed with this screen (firmware plan, section 15).
for t in "$images"/boot/device_trees/*.dtb; do
    "$HOST_DIR/bin/fdtput" -t s "$t" /leds/led-0 linux,default-trigger heartbeat
done
# Every tree: two pieces of memory kept out of Linux's hands (firmware plan,
# section 16; U-Boot's PREBOOT in uboot.fragment uses the same addresses).
#   ramoops, 1 MiB at 0x4d300000: the kernel keeps what it says there
#     (its last 512 KiB is the console's text, from 0x4d380000). A reset does
#     not clear memory, so U-Boot finds it after one and writes it to the card.
#   apotheca-crumbs, 4 KiB at 0x4d200000: U-Boot counts there how many resets
#     in a row came with no start of the system between (S00early clears it).
for t in "$images"/boot/device_trees/*.dtb; do
    "$HOST_DIR/bin/fdtput" -c "$t" /reserved-memory/ramoops@4d300000
    "$HOST_DIR/bin/fdtput" -t s "$t" /reserved-memory/ramoops@4d300000 compatible ramoops
    "$HOST_DIR/bin/fdtput" -t x "$t" /reserved-memory/ramoops@4d300000 reg 0x0 0x4d300000 0x0 0x100000
    "$HOST_DIR/bin/fdtput" -t x "$t" /reserved-memory/ramoops@4d300000 record-size 0x20000
    "$HOST_DIR/bin/fdtput" -t x "$t" /reserved-memory/ramoops@4d300000 console-size 0x80000
    "$HOST_DIR/bin/fdtput" -c "$t" /reserved-memory/apotheca-crumbs@4d200000
    "$HOST_DIR/bin/fdtput" -t x "$t" /reserved-memory/apotheca-crumbs@4d200000 reg 0x0 0x4d200000 0x0 0x1000
    "$HOST_DIR/bin/fdtput" -t s "$t" /reserved-memory/apotheca-crumbs@4d200000 no-map ""
done
# A card for a handheld with LPDDR3 memory (build-image.sh --dram lpddr3): the
# memory's rail is 1.2 V there, and the trees say 1.1 V (LPDDR4), which Linux
# would set as it starts. U-Boot's side is uboot-lpddr3.fragment.
if [ "${APOTHECA_DRAM:-lpddr4}" = lpddr3 ]; then
    for t in "$images"/boot/device_trees/*.dtb; do
        node=/soc/i2c@7081400/pmic@34/regulators/dcdc3
        [ "$("$HOST_DIR/bin/fdtget" -t u "$t" "$node" regulator-min-microvolt)" = 1100000 ]
        "$HOST_DIR/bin/fdtput" -t u "$t" "$node" regulator-min-microvolt 1200000
        "$HOST_DIR/bin/fdtput" -t u "$t" "$node" regulator-max-microvolt 1200000
    done
    echo "lpddr3: this card is for a handheld with LPDDR3 memory (1.2 V); it will not start one with LPDDR4" > "$images/boot/memory-kind.txt"
else
    echo "lpddr4: this card is for a handheld with LPDDR4 memory (1.1 V); it will not start one with LPDDR3" > "$images/boot/memory-kind.txt"
fi
cp -r "$board/boot-files/extlinux" "$images/boot/"
cp "$board/boot-files/README.txt" "$images/boot/README.txt"
genimage_cfg="$BUILD_DIR/genimage-apotheca.cfg"
if [ "$dtb" = auto ]; then
    echo "auto: U-Boot picks the tree by the screen (pin PE6), unless a dtb.img is put here" \
        > "$images/boot/dtb-in-use.txt"
    # No dtb.img on the card: genimage is not asked for one.
    grep -v '"boot/dtb.img",' "$board/genimage.cfg" > "$genimage_cfg"
else
    cp "$images/boot/device_trees/sun50i-h700-anbernic-$dtb.dtb" "$images/boot/dtb.img"
    echo "sun50i-h700-anbernic-$dtb.dtb" > "$images/boot/dtb-in-use.txt"
    cp "$board/genimage.cfg" "$genimage_cfg"
fi

# Changes to try for the faint vertical lines on the newer screen (the
# owner, 2026-11-17), each a whole dtb.img in a folder of its own, so trying
# one is a copy with no renaming. All start from the v2-panel tree, and each
# changes one thing toward what Anbernic's own system sets (its lcd0 node,
# read from the boot package muOS ships); the last changes all of them.
#   1  the 28 RGB pins driven at 40 mA (Anbernic: drive 3; ours: the reset
#      value, 20 mA)
#   2  the backlight's PWM at 50 kHz (Anbernic: lcd_pwm_freq 50000; ours 25 kHz)
#   3  an 18-bit bus with dithering (Anbernic: lcd_frm 1), through a panel
#      file in the tree (patch 0170)
#   4  all three, and the stock clock edge and timings
fdtput="$HOST_DIR/bin/fdtput"
v2="$images/boot/device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb"
tries="$board/firmware/panels-to-try"
pins=/soc/pinctrl@300b000/lcd0-rgb888-pins
try() { mkdir -p "$images/boot/device_trees/try/$1"; cp "$v2" "$images/boot/device_trees/try/$1/dtb.img"; echo "$images/boot/device_trees/try/$1/dtb.img"; }
drive40() { "$fdtput" -t u "$1" "$pins" drive-strength 40; }
pwm50k() { set -- "$1" $("$HOST_DIR/bin/fdtget" "$1" /backlight pwms); "$fdtput" -t u "$1" /backlight pwms "$2" "$3" 20000 "$5"; }
panel() { "$BR2_EXTERNAL_APOTHECA_PATH/tools/panel-dtb.sh" "$1" "$tries/$2" "$1.new" > /dev/null && mv "$1.new" "$1"; }
f=$(try 1-pins-40ma); drive40 "$f"
f=$(try 2-backlight-50khz); pwm50k "$f"
f=$(try 3-dither); panel "$f" v2-dither.panel
f=$(try 4-all-as-stock); drive40 "$f"; pwm50k "$f"; panel "$f" v2-stock-dither.panel
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
    --config "$genimage_cfg"
rm -rf "$tmp"

gzip -n -f -k -6 "$images/apotheca-rg40xxv.img"
