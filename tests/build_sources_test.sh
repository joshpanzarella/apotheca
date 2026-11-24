#!/bin/sh
# The handheld build (scripts/build-handheld.sh) takes every .cpp under engine/src, one folder down too, and
# app/; CMake lists its sources by name (and every .cpp of engine/src/fx). The two must build the same program:
# MORPH's engine/src/morph/tables.cpp was in one and not the other once, and the handheld did not link. This
# fails on a source one has and the other has not, and plants a file of its own to show it can.
#   tests/build_sources_test.sh SOURCE_DIR
set -u
root=${1:-.}
listed() { # what CMake builds into the program (the tests' own sources aside)
    { grep -oE '(engine/src|app)/[A-Za-z0-9_/]+\.cpp' "$root/CMakeLists.txt"
      for f in "$root"/engine/src/fx/*.cpp; do echo "engine/src/fx/$(basename "$f")"; done; } | sort -u
}
globbed() { # what the handheld script takes
    for f in "$root"/engine/src/*.cpp "$root"/engine/src/*/*.cpp "$root"/app/*.cpp; do [ -f "$f" ] && echo "${f#$root/}"; done | sort -u
}
a=$(listed); b=$(globbed)
fail=0
if [ "$a" != "$b" ]; then
    echo "CMake and the handheld build take other sources:"
    printf '%s\n' "$a" > /tmp/build_sources_a.$$; printf '%s\n' "$b" > /tmp/build_sources_b.$$
    diff /tmp/build_sources_a.$$ /tmp/build_sources_b.$$ | sed 's/^</  CMake only:/; s/^>/  handheld only:/' | grep only
    rm -f /tmp/build_sources_a.$$ /tmp/build_sources_b.$$
    fail=1
fi
# The plant: a source the handheld would take and CMake does not list must be found.
plant="$root/app/zz_build_sources_plant.cpp"
: > "$plant"
b2=$(globbed)
rm -f "$plant"
if [ "$a" = "$b2" ]; then echo "the plant was not found: the test cannot see a difference"; fail=1; fi
[ $fail -eq 0 ] && echo "build sources: the same $(printf '%s\n' "$a" | wc -l) files"
exit $fail
