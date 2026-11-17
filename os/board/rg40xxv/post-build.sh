#!/bin/sh
# Runs after the packages are installed into the target, before the root
# file system image is made. $1 is the target folder.
set -eu
target="$1"

# Where the data partition is mounted (/etc/init.d/S05data).
mkdir -p "$target/data"

# Nothing runs that we did not ask for. Buildroot's own start-up scripts
# would start crond and the network scripts, try to write a random seed to
# the read-only root (seedrng), run sysctl with nothing to set, and load
# modules from a list we do not keep (our S02modules loads what the
# hardware asks for). Syslog and klog would write to a RAM disk nobody
# reads: our boot log takes dmesg itself.
for s in S01seedrng S20seedrng S01syslogd S02klogd S02sysctl S11modules \
         S40network S50crond; do
    rm -f "$target/etc/init.d/$s"
done

# What went into this build, for the boot log.
{
    echo "built: $(date -u -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}" '+%Y-%m-%d %H:%M UTC' 2>/dev/null || date -u)"
    echo "apotheca commit: ${APOTHECA_COMMIT:-unknown}"
    echo "buildroot: ${BR2_VERSION:-unknown}"
} > "$target/etc/apotheca-build"
