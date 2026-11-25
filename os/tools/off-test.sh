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
