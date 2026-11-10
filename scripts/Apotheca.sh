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

    # Whether a USB keyboard or controller plugged in could be read as MIDI:
    # the kernel needs its USB MIDI driver, and a device shows up as a
    # "midi" entry here when one is plugged in.
    echo "== midi =="
    ls /dev/snd 2>/dev/null | tr '\n' ' '; echo
    cat /proc/asound/devices 2>/dev/null | grep -i midi || echo "no midi device just now"
    if [ -e /proc/config.gz ]; then
        zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_SND_USB_AUDIO|CONFIG_SND_RAWMIDI|CONFIG_SND_SEQUENCER|CONFIG_USB_GADGET|CONFIG_USB_F_MIDI' || echo "kernel config has none of the MIDI options"
    else
        echo "kernel config: not readable here"
    fi
    echo "usb devices:"
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/product" ] && echo "  $(cat "$d/product" 2>/dev/null)"
    done
} > device-info.log 2>&1

# An update accepted on the handheld is waiting as "apotheca.new". Put it in
# place, and keep the one that was there. The mark "update-trying" is taken
# away by the program itself once it has been running for three seconds.
if [ -f apotheca.new ]; then
    rm -f apotheca.old
    if mv apotheca apotheca.old && mv apotheca.new apotheca; then
        chmod +x apotheca
        : > update-trying
    else
        [ -f apotheca ] || mv apotheca.old apotheca # could not swap: leave things as they were
    fi
fi

./apotheca > apotheca.log 2>&1

# The mark is still there: the new program never got going (or was closed in
# its first three seconds). Go back to the one before and start that.
if [ -f update-trying ]; then
    rm -f update-trying
    if [ -f apotheca.old ]; then
        mv apotheca apotheca.failed
        mv apotheca.old apotheca
        echo "the update did not start; back to the one before" >> apotheca.log
        ./apotheca >> apotheca.log 2>&1
    fi
fi
