#!/bin/sh
# stock-probe.sh: run ON THE STOCK ANBERNIC SYSTEM of an RG40XXV, it writes
# what that system knows of the handheld's parts to stock-probe.txt beside
# itself. Nothing is changed on the handheld or the card but that one file.
# (Firmware plan, section 15: one unit starts with the stock card and not
# with ours; this says what is inside it.)
#
# How: copy this file to the stock card's Roms/APPS folder (the big drive
# the computer shows), put the card in the handheld, start it, open
# App Center, then APPS, and run "stock-probe". Wait ten seconds, turn the
# handheld off the usual way, and send back Roms/APPS/stock-probe.txt.
# NOT TRIED on a handheld: written from what is known of the stock system
# (Allwinner's 4.9 kernel). Every line that fails is skipped.

here=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
[ -n "$here" ] || here=/mnt/mmc/Roms/APPS
out="$here/stock-probe.txt"

section() { echo; echo "===== $1"; }
show() { for f in "$@"; do [ -e "$f" ] && { echo "--- $f"; cat "$f" 2>&1 | tr -d '\000'; echo; }; done; }
# One hardware register, through Allwinner's sunxi_dump
reg() {
    [ -e /sys/class/sunxi_dump/dump ] || return 0
    echo "$1" > /sys/class/sunxi_dump/dump 2>/dev/null && echo "$2 ($1): $(cat /sys/class/sunxi_dump/dump 2>/dev/null)"
}

{
    echo "stock-probe: what the stock system knows of this handheld"
    date 2>/dev/null
    section "system"; uname -a; show /proc/cmdline /proc/version /etc/os-release /mnt/vendor/oem/version.ini
    section "model"; show /proc/device-tree/model /proc/device-tree/compatible /sys/firmware/devicetree/base/model
    section "memory"; grep -E 'MemTotal|CmaTotal' /proc/meminfo
    show /proc/device-tree/dram/dram_type /proc/device-tree/dram/dram_clk
    for f in /proc/device-tree/dram/*; do
        [ -f "$f" ] && echo "${f##*/}: $(od -A n -t x1 "$f" 2>/dev/null | tr -d ' \n')"
    done
    section "the power chip's rails (the memory's: 1.1 V is LPDDR4, 1.2 V LPDDR3)"
    for r in /sys/class/regulator/regulator.*; do
        [ -d "$r" ] && echo "$(cat $r/name 2>/dev/null): $(cat $r/microvolts 2>/dev/null) uV, $(cat $r/state 2>/dev/null)"
    done
    show /sys/kernel/debug/regulator/regulator_summary
    section "the power chip"; ls -l /sys/class/axp 2>&1
    for f in /sys/class/axp/* /sys/class/power_supply/*/uevent; do show "$f"; done
    section "the screen"
    show /sys/class/disp/disp/attr/sys /proc/device-tree/soc@03000000/lcd0@01c0c000/lcd_driver_name \
         /sys/class/lcd/lcd0/device/uevent /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/modes
    for f in /proc/device-tree/soc*/lcd0*/lcd_driver_name /proc/device-tree/soc*/lcd0*/lcd_x /proc/device-tree/soc*/lcd0*/lcd_dclk_freq; do
        [ -f "$f" ] && echo "$f: $(tr -d '\000' < "$f" | od -A n -c | tr -d '\n')"
    done
    echo "pin PE6 tells the screens apart (bit 6 of port E's data; low: the newer screen):"
    reg 0x0300b090 "port E cfg0"
    reg 0x0300b0a0 "port E data"
    reg 0x0300b0b0 "port E pull0"
    section "what the boot loader told the kernel"; tr '\000' '\n' < /proc/device-tree/chosen/bootargs 2>/dev/null
    section "wifi and other chips"
    for d in /sys/bus/sdio/devices/*; do [ -d "$d" ] && echo "$d: $(cat $d/vendor 2>/dev/null) $(cat $d/device 2>/dev/null)"; done
    for d in /sys/bus/mmc/devices/*; do [ -d "$d" ] && echo "$d: $(cat $d/type 2>/dev/null) $(cat $d/name 2>/dev/null) $(cat $d/manfid 2>/dev/null) $(cat $d/oemid 2>/dev/null) $(cat $d/date 2>/dev/null)"; done
    show /proc/cpuinfo
    section "the card's boot program (boot0's header at 8 KiB: its memory settings)"
    dd if=/dev/mmcblk0 bs=1024 skip=8 count=1 2>/dev/null | od -A x -t x4
    section "partitions"; cat /proc/partitions; mount
    section "modules"; cat /proc/modules 2>/dev/null
    section "kernel messages"; dmesg 2>&1
} > "$out" 2>&1
sync
