# The turn-off's own log, for every step of going down. Sourced, not run.
#
# off_log STEP: appends "[uptime] off: STEP" to /data/apotheca/logs/off.log.
# It does not sync: a sync for every line cost about 0.13 s a line on the
# card (owner's off.log, 2026-11-28: 23 lines, 3.4 s), most of a turn-off.
# The log is synced instead where a turn-off can stop and wait: off_limit
# syncs it before it starts its command (and S95apotheca before it waits for
# Apotheca), so a turn-off that hangs still leaves the last line it reached
# on the card (the owner's power key, held, cuts the power and everything in
# RAM with it). The same words go to the kernel's
# log (/dev/kmsg), which ramoops keeps past a warm reset, and which is the
# only place left once /data is unmounted: off_log then writes nowhere else
# and never fails. The file keeps its last OFF_KEEP lines, trimmed by the
# first line of each turn-off.
#
# off_limit SECONDS NAME COMMAND...: syncs the log (off_sync; not when the
# command is itself a sync), runs COMMAND, waits for it at most
# SECONDS, and returns its code. One still running then is sent TERM and
# KILL and left behind (a process stuck in the kernel cannot be waited
# for), the log says so with where it is stuck, and the code is 124.
#
# Every place is overridable for the desk test (os/tools/off-test.sh):
# APOTHECA_OFF_LOG, APOTHECA_OFF_KMSG, APOTHECA_OFF_RUN, and APOTHECA_POWEROFF
# (what off_poweroff runs in place of poweroff; init is then never signalled).

OFF_LOG=${APOTHECA_OFF_LOG:-/data/apotheca/logs/off.log}
OFF_KMSG=${APOTHECA_OFF_KMSG:-/dev/kmsg}
OFF_RUN=${APOTHECA_OFF_RUN:-/run/apotheca}
OFF_KEEP=200
# How long one sync of the log may take before the log stops syncing.
OFF_SYNC_S=3

# Seconds since the kernel started, in hundredths (no dot: for sums):
# off_now sets $off_now, off_cs prints it. Read by the shell itself, no
# program started: the waits below ask many times a second.
off_now() {
    read -r _off_u _off_rest < /proc/uptime
    _off_f=${_off_u#*.}
    off_now=$(( ${_off_u%.*} * 100 + ${_off_f#0} ))    # (no leading 0: "08" is not octal)
}
off_cs() { off_now; echo "$off_now"; }

# "12.34" from hundredths
off_s() { printf '%d.%02d' $(( $1 / 100 )) $(( $1 % 100 )); }

# A short sleep while waiting for a child: 0.01 s for its first tenth of a
# second (most steps end within it), then 0.1 s. Waiting in steps of 0.1 s
# alone put 0.1 s on every sync and every step. N: hundredths waited so far.
off_nap() { if [ "$1" -lt 10 ]; then sleep 0.01; else sleep 0.1; fi; }

# Is the background child PID still running? (a zombie is not)
off_alive() {
    kill -0 "$1" 2>/dev/null || return 1
    _off_st=
    read -r _off_st 2>/dev/null < "/proc/$1/stat"
    _off_st=${_off_st##*) }
    case "${_off_st%% *}" in
        Z|X|'') return 1 ;;
    esac
    return 0
}

# PID and every process under it (the stop script's own programs), deepest
# first: TERM or KILL to the script alone would leave a hung child behind.
# (The pids by `cd /proc`, not a glob of /proc/[0-9]*: under qemu-user, the
# desk test's BusyBox, an absolute glob is looked for in the image's folder.)
off_pids() { (cd /proc && echo [0-9]*); }
off_tree() {
    for _off_p in $(off_pids); do
        [ "$(sed 's/.*) //' "/proc/$_off_p/stat" 2>/dev/null | cut -d' ' -f2)" = "$1" ] && off_tree "$_off_p"
    done
    echo "$1"
}

# Where a process is waiting, for a line that says it was cut short.
off_where() {
    echo "pid $1 '$(tr '\0' ' ' 2>/dev/null < "/proc/$1/cmdline" | cut -c1-60 | sed 's/ *$//')': state $(sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f1), in $(cat "/proc/$1/wchan" 2>/dev/null)"
}

# A sync that may take OFF_SYNC_S at most. A sync that took longer is never
# waited for again (the mark in OFF_RUN): the card is not answering, and the
# turn-off goes on without it.
off_sync() {
    [ -e "$OFF_RUN/off-sync-stuck" ] && return 1
    off_now
    _off_sync_began=$off_now
    sync &
    _off_pid=$!
    while off_alive "$_off_pid"; do
        off_now
        _off_n=$(( off_now - _off_sync_began ))
        if [ "$_off_n" -ge $(( OFF_SYNC_S * 100 )) ]; then
            mkdir -p "$OFF_RUN" 2>/dev/null
            touch "$OFF_RUN/off-sync-stuck" 2>/dev/null
            echo "apotheca off: a sync took over ${OFF_SYNC_S} s ($(off_where "$_off_pid")): the log no longer waits for the card" >> "$OFF_KMSG" 2>/dev/null
            return 1
        fi
        off_nap "$_off_n"
    done
    wait "$_off_pid" 2>/dev/null
    return 0
}

# The first line of a turn-off: the file cut to its last OFF_KEEP - 1 lines,
# and a heading (the boot's number matches boot-NNNN.txt).
off_begin() {
    mkdir -p "$OFF_RUN" 2>/dev/null
    [ -e "$OFF_RUN/off-begun" ] && return 0
    touch "$OFF_RUN/off-begun" 2>/dev/null
    if [ -f "$OFF_LOG" ]; then
        tail -n $(( OFF_KEEP - 2 )) "$OFF_LOG" > "$OFF_LOG.part" 2>/dev/null &&
            mv "$OFF_LOG.part" "$OFF_LOG" 2>/dev/null
    fi
    _off_boot=$(cat "${OFF_LOG%/*}/count" 2>/dev/null)
    echo "----- a turn-off (boot ${_off_boot:-?}; boot-latest.txt is this start)" >> "$OFF_LOG" 2>/dev/null
}

off_log() {
    read -r _off_u _off_rest < /proc/uptime
    _off_line="[$_off_u] off: $*"
    echo "apotheca off: $*" >> "$OFF_KMSG" 2>/dev/null
    # The card's folder is there only while /data is mounted.
    [ -d "${OFF_LOG%/*}" ] || return 0
    off_begin
    echo "$_off_line" >> "$OFF_LOG" 2>/dev/null
    return 0
}

off_limit() {
    _off_limit=$1 _off_name=$2
    shift 2
    # What is logged so far onto the card before anything that may hang.
    # Only while /data is there (after its unmount a sync is the
    # command's own business); never before a sync, which is one itself.
    [ "$1" != sync ] && [ -d "${OFF_LOG%/*}" ] && off_sync
    off_now
    _off_began=$off_now
    "$@" &
    _off_child=$!
    while off_alive "$_off_child"; do
        off_now
        _off_n=$(( off_now - _off_began ))
        if [ "$_off_n" -ge $(( _off_limit * 100 )) ]; then
            _off_tree=$(off_tree "$_off_child")
            _off_where_is=
            for _off_p in $_off_tree; do
                [ -n "$_off_where_is" ] && _off_where_is="$_off_where_is; "
                _off_where_is="$_off_where_is$(off_where "$_off_p")"
            done
            kill -TERM $_off_tree 2>/dev/null
            sleep 0.5
            kill -KILL $_off_tree 2>/dev/null
            off_log "$_off_name: CUT SHORT after $_off_limit s, still running ($_off_where_is); going on without it"
            return 124
        fi
        off_nap "$_off_n"
    done
    wait "$_off_child"
    _off_code=$?
    off_now
    off_took=$(off_s $(( off_now - _off_began )))
    return $_off_code
}

# The handheld off: init asked to power off (it runs rcK, which logs the
# rest). poweroff syncs before it asks init, and a sync that hangs would keep
# it from asking: so it gets 10 s, and then init is told directly
# (BusyBox init powers off on USR2).
off_poweroff() {
    off_log "poweroff: asking init to turn the handheld off"
    off_limit 10 "poweroff" ${APOTHECA_POWEROFF:-poweroff}
    _off_code=$?
    if [ "$_off_code" -ne 0 ]; then
        off_log "poweroff ended with code $_off_code: telling init directly (USR2)"
        [ -z "$APOTHECA_POWEROFF" ] && kill -USR2 1
    fi
    return 0
}
