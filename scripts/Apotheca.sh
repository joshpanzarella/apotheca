#!/bin/sh
# Launcher for the handheld's Ports menu. Sits beside the "apotheca" folder.
# Anything the program prints goes to apotheca/apotheca.log, so a failed
# start leaves a reason behind.

dir="$(cd "$(dirname "$0")" && pwd)/apotheca"
cd "$dir" || exit 1

# Written on every start: what this device and firmware can do. It only
# reads; it changes nothing. Used to decide what Apotheca can rely on
# (its own WiFi hotspot, USB audio, the realtime scheduler).
{
    echo "== system =="
    uname -srm
    grep -h PRETTY_NAME /etc/os-release 2>/dev/null
    echo "user: $(id -un 2>/dev/null) ($(id -u))"

    echo "== wifi =="
    for n in /sys/class/net/*; do
        [ -d "$n/wireless" ] || continue
        echo "interface: $(basename "$n")  driver: $(basename "$(readlink "$n/device/driver" 2>/dev/null)")"
    done
    if command -v iw >/dev/null 2>&1; then
        echo "modes the chip supports (AP = can be its own hotspot):"
        iw list 2>/dev/null | sed -n '/Supported interface modes/,/Band 1/p' | grep '\*'
    else
        echo "iw: not installed, cannot list modes"
    fi
    for tool in hostapd dnsmasq udhcpd wpa_supplicant nmcli connmanctl; do
        if command -v "$tool" >/dev/null 2>&1; then echo "$tool: yes"; else echo "$tool: no"; fi
    done

    echo "== sound cards =="
    cat /proc/asound/cards 2>/dev/null
} > device-info.log 2>&1

./apotheca > apotheca.log 2>&1
