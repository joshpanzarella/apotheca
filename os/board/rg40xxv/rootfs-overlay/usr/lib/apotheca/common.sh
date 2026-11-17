# Shared by Apotheca's start-up scripts. Sourced, not run.

APOTHECA_RUN=/run/apotheca
APOTHECA_BOOTLOG=$APOTHECA_RUN/boot.log

# say WORDS: to the boot log (in RAM, copied to the card by S90bootlog) and
# to the console, with the seconds since the kernel started.
say() {
    mkdir -p "$APOTHECA_RUN"
    up=$(cut -d' ' -f1 /proc/uptime)
    echo "[$up] $*" >> "$APOTHECA_BOOTLOG"
    echo "apotheca: $*" > /dev/console 2>/dev/null || true
}

# The block device the root file system is on (mmcblk0p2, or vda2 under qemu).
root_partition() {
    want=$(mountpoint -d /)
    for d in /sys/class/block/*; do
        [ "$(cat "$d/dev" 2>/dev/null)" = "$want" ] && { basename "$d"; return 0; }
    done
    return 1
}

# partition_of DISK N: the partition N of DISK (mmcblk0 4 -> mmcblk0p4).
partition_of() {
    for d in /sys/block/"$1"/"$1"*; do
        [ "$(cat "$d/partition" 2>/dev/null)" = "$2" ] && { basename "$d"; return 0; }
    done
    return 1
}

# disk_of PARTITION: the whole disk (mmcblk0p2 -> mmcblk0).
disk_of() {
    basename "$(dirname "$(readlink -f "/sys/class/block/$1")")"
}
