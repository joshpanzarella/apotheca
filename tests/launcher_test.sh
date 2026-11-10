#!/bin/sh
# Tests for the handheld's launcher (scripts/Apotheca.sh): an accepted update
# is put in place, and if it does not get going the one before comes back.
# Stand-in programs are small shell scripts that say which one ran.
#
#   tests/launcher_test.sh path/to/Apotheca.sh

launcher=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
failures=0
check() { # check "what" command...
    what=$1; shift
    if ! "$@"; then echo "FAIL  $what"; failures=$((failures + 1)); fi
}
is() { [ "$(cat "$1" 2>/dev/null)" = "$2" ]; }
ran() { [ "$(cat ran.txt 2>/dev/null | tr '\n' ' ')" = "$1" ]; }

setup() {
    root=$(mktemp -d /tmp/apotheca-launcher-XXXXXX)
    mkdir "$root/apotheca"
    cp "$launcher" "$root/Apotheca.sh"
    cd "$root/apotheca" || exit 1
    # The one that is installed: it runs and says so.
    printf '#!/bin/sh\necho old >> ran.txt\n' > apotheca
    chmod +x apotheca
}
good_update() { printf '#!/bin/sh\necho new >> ran.txt\nrm -f update-trying\n' > "$1"; }   # gets going: takes the mark away
bad_update() { printf '#!/bin/sh\necho broken >> ran.txt\nexit 1\n' > "$1"; }              # never gets going

# No update waiting: the program runs, and nothing is moved about.
setup
sh ../Apotheca.sh
check "plain start runs the program" ran "old "
check "plain start leaves no update files" [ ! -e apotheca.old ] 
check "plain start leaves no mark" [ ! -e update-trying ]
check "the device log is written" [ -s device-info.log ]

# An accepted update that works: it is the program now, the old one is kept.
setup
good_update apotheca.new
sh ../Apotheca.sh
check "a good update runs" ran "new "
check "a good update is in place" grep -q "echo new" apotheca
check "the one before is kept" grep -q "echo old" apotheca.old
check "the waiting file is gone" [ ! -e apotheca.new ]
check "the mark is gone" [ ! -e update-trying ]
# And it is still the program at the start after that.
sh ../Apotheca.sh
check "the update stays" ran "new new "

# An accepted update that does not get going: back to the one before, which then runs.
setup
bad_update apotheca.new
sh ../Apotheca.sh
check "a bad update is tried, then the old one runs" ran "broken old "
check "the old one is the program again" grep -q "echo old" apotheca
check "the bad one is set aside" grep -q "echo broken" apotheca.failed
check "nothing is left waiting" [ ! -e apotheca.new ]
check "no old copy is left over" [ ! -e apotheca.old ]
check "the mark is gone" [ ! -e update-trying ]
check "the log says what happened" grep -q "back to the one before" apotheca.log
# The start after that is a plain one.
sh ../Apotheca.sh
check "after going back it starts as usual" ran "broken old old "

# A second update after a good one replaces the kept copy, not the program's history.
setup
good_update apotheca.new
sh ../Apotheca.sh
printf '#!/bin/sh\necho newer >> ran.txt\nrm -f update-trying\n' > apotheca.new
sh ../Apotheca.sh
check "a second update runs" ran "new newer "
check "the kept copy is the first update" grep -q "echo new " apotheca.old

# A file that is not a program at all, accepted somehow: it cannot run, and the old one comes back.
setup
echo "not a program" > apotheca.new
sh ../Apotheca.sh 2>/dev/null
check "junk is tried, then the old one runs" ran "old "
check "the old one is the program again after junk" grep -q "echo old" apotheca

if [ "$failures" -ne 0 ]; then echo "$failures check(s) failed"; exit 1; fi
echo "launcher tests passed"
