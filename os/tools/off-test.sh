#!/bin/sh
# off-test.sh [SHELL]
#
# The desk test of the turn-off's log and time limits
# (rootfs-overlay/usr/lib/apotheca/off.sh, etc/init.d/rcK,
# usr/libexec/apotheca/off, usr/sbin/apotheca-keep), run by SHELL (default
# sh; give BusyBox's for the handheld's own: "qemu-aarch64 -L TARGET
# TARGET/bin/busybox sh"). A temporary folder stands in for /data, /run and
# /etc/init.d, a file for /dev/kmsg; poweroff is a stand-in, so nothing is
# turned off and init is never signalled.
#
#   rcK          every stop script logged, begun and done, with its time;
#                one that hangs is cut short at its limit, the log says so
#                and the next one still runs; a stop that unmounts /data (its
#                logs' folder gone) leaves the rest in the kernel's log only
#   trim         off.log keeps its last lines (about 200) at a turn-off
#   off          who asked and why; a poweroff that hangs is cut short at 10 s
#   keep         apotheca-keep, on the program's off mark, runs off
#   syncs        a line is not synced by itself; the log is synced before
#                each step that may hang (a stand-in sync on PATH records
#                what the card would have), never before a sync
#   wifi         WiFi on (an interface up) is turned off after S95apotheca,
#                timed; one that hangs is cut short at 5 s; WiFi off is not
#                touched
#   time         what the logging costs a line (on the desk's disk, not
#                the SD card: info, and a loose bound for the desk's sh)
#
# What it cannot show: the real sync, umount and poweroff, BusyBox init, the
# kernel's log and ramoops (qemu-test.py boots the image for those).

set -u
shell=${1:-sh}
here=$(cd "$(dirname "$0")/.." && pwd)
overlay=$here/board/rg40xxv/rootfs-overlay
t=$(mktemp -d "${TMPDIR:-/tmp}/apotheca-off-XXXXXX")
trap 'rm -rf "$t"' EXIT
fails=0
check() {
    if [ "$1" = 0 ]; then echo "ok    $2"; else echo "FAIL  $2"; fails=$((fails + 1)); fi
}
has() { grep -q -- "$2" "$1"; echo $?; }

setup() {
    rm -rf "$t/data" "$t/run" "$t/initd" "$t/kmsg"
    mkdir -p "$t/data/apotheca/logs" "$t/run" "$t/initd"
    : > "$t/kmsg"
    echo 41 > "$t/data/apotheca/logs/count"
}
export APOTHECA_LIB="$overlay/usr/lib/apotheca"
export APOTHECA_OFF_LOG="$t/data/apotheca/logs/off.log"
export APOTHECA_OFF_KMSG="$t/kmsg"
export APOTHECA_OFF_RUN="$t/run"
export APOTHECA_INITD="$t/initd"
export APOTHECA_POWEROFF=true
export APOTHECA_NET="$t/net"
export APOTHECA_WIFI_CMD="$t/wifi"
mkdir -p "$t/net" "$t/bin"
printf '#!/bin/sh\necho "$*" >> "%s/wifi-said"\n' "$t" > "$t/wifi"
chmod 755 "$t/wifi"
log=$APOTHECA_OFF_LOG

stand_in() {  # NAME STOP-COMMANDS
    printf '#!/bin/sh\ncase "$1" in stop) %s ;; esac\n' "$2" > "$t/initd/$1"
    chmod 755 "$t/initd/$1"
}

# ---- rcK, with a stop that hangs and one that "unmounts /data"
setup
i=1
while [ $i -le 500 ]; do echo "old line $i"; i=$((i + 1)); done > "$log"
stand_in S95apotheca "sleep 0.3"
stand_in S50hang "sleep 1000"
stand_in S40quick "exit 3"
stand_in S05data "cp '$log' '$t/off-rcK.log'; rm -rf '$t/data/apotheca'"
stand_in S02after "true"
began=$(date +%s)
APOTHECA_STOP_S=2 APOTHECA_S95_STOP_S=4 $shell "$overlay/etc/init.d/rcK"
code=$?
took=$(( $(date +%s) - began ))
check $code "rcK ends with code 0"
check $([ "$took" -le 8 ]; echo $?) "rcK did not wait for the hanging stop (took ${took}s)"
echo "--- what rcK wrote to the kernel's log (stand-in)"; sed 's/^/    /' "$t/kmsg"
k=$t/kmsg
check $(has "$k" "rcK: going down (asked by nobody of ours") "rcK's first line: who asked (nobody: a shell's poweroff)"
check $(has "$k" "WiFi programs running") "rcK says what WiFi is running and what is stuck"
check $(has "$k" "S95apotheca stop: begins (at most 4 s)") "S95apotheca gets its own, longer limit"
check $(has "$k" "S95apotheca stop: done in 0\.[0-9]* s (code 0)") "a stop's time and code are logged"
check $(has "$k" "S50hang stop: CUT SHORT after 2 s, still running (pid [0-9]* '.*': state [A-Z], in ") "a stop that hangs is cut short at its limit, with where it was stuck"
check $(has "$k" "S40quick stop: done in .* s (code 3)") "the next stop still runs after one cut short, its code logged"
check $( [ "$(grep -c 'S02after stop: done' "$k")" = 1 ]; echo $?) "a stop after /data is gone is still logged, to the kernel's log"
check $(has "$k" "rcK: every stop script done") "rcK's last line (kernel's log only)"
check $(has "$t/off-rcK.log" "S50hang stop: CUT SHORT") "the card's off.log has the cut-short line"
pgrep -f "sleep 1000" > /dev/null; check $([ $? -ne 0 ]; echo $?) "the hanging stop was killed"

# ---- trim, and the card's lines stop when /data goes
setup
i=1
while [ $i -le 500 ]; do echo "old line $i"; i=$((i + 1)); done > "$log"
stand_in S10quick "true"
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
old=$(grep -c "^old line" "$log")
first=$(grep -m1 "^old line" "$log")
check $([ "$old" = 198 ] && [ "$first" = "old line 303" ]; echo $?) "trim: the last 198 old lines kept ($old, from \"$first\")"
check $(has "$log" "^----- a turn-off (boot 41;") "a turn-off's heading, with the boot's number"
check $([ "$(grep -c '^-----' "$log")" = 1 ]; echo $?) "trimmed once a turn-off, not at every line"
check $([ "$(wc -l < "$log")" -le 210 ]; echo $?) "off.log is about 200 lines ($(wc -l < "$log"))"
check $(has "$log" "^\[[0-9.]*\] off: S10quick stop: done in") "each line: [uptime] off: STEP"

# ---- off: who asked, and a poweroff that hangs
setup
APOTHECA_POWEROFF="sleep 1000" $shell "$overlay/usr/libexec/apotheca/off" apotheca-power "power key held"
check $? "off ends with code 0"
check $(has "$log" "off: apotheca-power asks for the handheld off: power key held") "off: who asked and why"
check $(has "$log" "poweroff: CUT SHORT after 10 s") "off: a poweroff that hangs is cut short at 10 s"
check $(has "$log" "poweroff ended with code 124: telling init directly") "off: then init is told directly (not here: a stand-in)"
check $(has "$t/run/off-asked" "apotheca-power: power key held") "off: rcK will say who asked"
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
check $(has "$log" "rcK: going down (asked by apotheca-power: power key held)") "rcK says who asked"

# ---- apotheca-keep: the program's off mark runs off
setup
mkdir -p "$t/keep"
KEEP_RUN=$t/keep KEEP_LOG=$t/keep KEEP_OFF_SCRIPT="$overlay/usr/libexec/apotheca/off" \
    $shell "$overlay/usr/sbin/apotheca-keep" offs sh -c "echo quit > '$t/keep/offs.off'" > /dev/null 2>&1 &
keep=$!
n=0
while ! grep -q "poweroff: asking init" "$log" 2>/dev/null && [ $n -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
kill $keep 2>/dev/null
check $(has "$log" "off: apotheca-keep asks for the handheld off: offs asked, after [0-9]*s, code 0 (quit)") "apotheca-keep: the off mark is logged, with why"
check $(has "$log" "poweroff: asking init") "apotheca-keep: off runs poweroff"
check $(has "$t/keep/offs.log" "asking for the handheld off (quit)") "apotheca-keep's own log as before"

# ---- syncs: where the log reaches the card (a stand-in sync records the log)
setup
printf '#!/bin/sh\necho "sync at: $(tail -n 1 "%s" 2>/dev/null)" >> "%s/syncs"\ncat "%s" >> "%s/synced" 2>/dev/null\n' "$log" "$t" "$log" "$t" > "$t/bin/sync"
chmod 755 "$t/bin/sync"
rm -f "$t/syncs" "$t/synced"
stand_in S95apotheca "true"
stand_in S50hang "sleep 1000"
stand_in S20sync ". '$overlay/usr/lib/apotheca/off.sh'; off_log 'S20sync: sync'; off_limit 5 'S20sync: sync' sync"
PATH="$t/bin:$PATH" APOTHECA_STOP_S=1 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
s=$t/syncs
echo "--- the stand-in sync saw these last lines"; sed 's/^/    /' "$s"
check $(has "$s" "sync at: .*off: S50hang stop: begins") "the log is synced before a stop that hangs begins (its line on the card)"
check $(has "$t/synced" "off: S50hang stop: CUT SHORT") "the cut-short line reaches the card at the next step"
check $([ "$(grep -c 'sync at:.*S20sync: sync$' "$s")" = 1 ]; echo $?) "no extra sync before a sync: the sync itself puts its line there"
lines=$(grep -c '\] off: ' "$log")
syncs=$(wc -l < "$s")
check $([ "$syncs" -lt "$lines" ]; echo $?) "fewer syncs ($syncs) than lines ($lines): a line is not synced by itself"

# ---- wifi: off after S95apotheca when it is on
setup
stand_in S95apotheca "true"
stand_in S05data "true"
rm -f "$t/wifi-said"; rm -rf "$t/net"; mkdir -p "$t/net/lo"; echo 0x9 > "$t/net/lo/flags"
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
if pidof wpa_supplicant udhcpc udhcpd avahi-daemon > /dev/null 2>&1; then
    echo "skip  WiFi off already: this desk runs a WiFi program"
else
    check $(has "$log" "off: WiFi: off already") "WiFi off already: said, and not touched"
    check $([ ! -e "$t/wifi-said" ]; echo $?) "WiFi off already: apotheca-wifi not run"
fi
setup
stand_in S95apotheca "true"
stand_in S05data "true"
mkdir -p "$t/net/wlan0/wireless"; echo 0x1003 > "$t/net/wlan0/flags"
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
check $(has "$log" "off: WiFi: turning it off (at most 5 s; on:.*wlan0 up)") "WiFi up: the step is logged, with what was on"
check $(has "$t/wifi-said" "^down$") "WiFi up: apotheca-wifi down is run"
check $(has "$log" "off: WiFi: off in [0-9.]* s (code 0)") "WiFi up: its time is logged"
check $(grep -n "" "$log" | grep -E "S95apotheca stop: done|WiFi: turning|S05data stop: begins" | cut -d: -f1 | tr '\n' ' ' | awk '{exit !($1 < $2 && $2 < $3)}'; echo $?) "WiFi is turned off after S95apotheca, before S05data (/data still there)"
setup
stand_in S95apotheca "true"
printf '#!/bin/sh\nsleep 1000\n' > "$t/wifi"
began=$(date +%s)
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
took=$(( $(date +%s) - began ))
check $(has "$log" "off: WiFi off: CUT SHORT after 5 s") "a WiFi off that hangs is cut short at 5 s (rcK took ${took}s)"
pgrep -f "sleep 1000" > /dev/null; check $([ $? -ne 0 ]; echo $?) "the hanging WiFi off was killed"
printf '#!/bin/sh\necho "$*" >> "%s/wifi-said"\n' "$t" > "$t/wifi"
rm -rf "$t/net"; mkdir -p "$t/net"

# ---- time: what a logged turn-off costs a line (the real sync, the desk's disk)
setup
for n in 90 80 70 60 50 40 30 20 10; do stand_in "S${n}quick" "true"; done
b=$(cut -d' ' -f1 /proc/uptime)
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
e=$(cut -d' ' -f1 /proc/uptime)
lines=$(grep -c '\] off: ' "$log")
ms=$(echo "$b $e $lines" | awk '{printf "%d", ($2 - $1) * 1000 / $3}')
first=$(grep '\] off: ' "$log" | head -n 1 | sed 's/^\[\([0-9.]*\)\].*/\1/')
last=$(grep '\] off: ' "$log" | tail -n 1 | sed 's/^\[\([0-9.]*\)\].*/\1/')
echo "info  rcK with 9 quick stops: $lines lines on the card from $first to $last, about $ms ms a line (desk disk; the SD card's sync is slower)"
# (A bound only for the desk's own shell: BusyBox under qemu-aarch64 is
# emulated, and took 70 to 100 ms a line, against 220 before.)
if [ "$shell" = sh ]; then
    check $([ "$ms" -lt 90 ]; echo $?) "a line costs under 90 ms on the desk ($ms ms; it was 130: a sync and a 0.1 s wait for every line)"
fi

# ---- /data not there at all: nothing written there, nothing fails
setup
rm -rf "$t/data"
APOTHECA_STOP_S=2 $shell "$overlay/etc/init.d/rcK" > /dev/null 2>&1
check $? "rcK with no /data: code 0"
check $([ ! -e "$t/data" ]; echo $?) "with no /data, nothing is made there"
check $(has "$t/kmsg" "rcK: every stop script done") "with no /data, the kernel's log still has every line"

echo
[ "$fails" = 0 ] && echo "every check passed" || echo "$fails checks FAILED"
[ "$fails" = 0 ]
