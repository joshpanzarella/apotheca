#!/bin/sh
# SELECT is drawn as SEL on the device (the owner's, 2026-11-12).
#
# This reads the app's sources for every string that says SELECT and fails on
# one the screen would show spelled out. usage: words_test.sh APP_FOLDER
#
# Allowed, because they do not reach the screen as written:
#   - a hint's key and a help-card line's key: ui.cpp draws them as SEL
#     (keyWords, checked in ui_test: testTheBarDrawsSelForSelect);
#   - buttonName's own word, which is the word in controls.txt (the screen
#     uses buttonLabel, checked in controls_test);
#   - ui.cpp's own comparison;
#   - the two web pages (design_page.cpp, and the page in transfer.cpp between
#     its page markers), read on a big screen: they keep the full word.
# Anything else: write SEL, or pass the text through a hint.
app=$1
[ -d "$app" ] || { echo "usage: words_test.sh APP_FOLDER"; exit 2; }
bad=$(
    for f in "$app"/*.cpp "$app"/*.hpp; do
        case $f in */design_page.cpp) continue ;; esac
        # The WiFi page is one raw string in transfer.cpp: R"page( ... )page". Leave it out.
        awk -v file="$f" '
            /R"page\(/ { raw = 1 }
            {
                line = $0
                if (!raw) {
                    sub(/[ \t]*\/\/.*$/, "", line)                       # a comment is not on the screen
                    text = line
                    while (match(text, /"[^"]*"/)) {
                        s = substr(text, RSTART, RLENGTH)
                        if (s ~ /SELECT/) {
                            before = substr(text, 1, RSTART - 1)
                            ok = 0
                            if (before ~ /hint\($/) ok = 1                    # hint("SELECT...", ...)
                            if (before ~ /^[ \t]*\{$/) ok = 1                 # {"SELECT, tapped", ...}: a help-card key
                            if (before ~ /case Button::Select: return $/) ok = 1
                            if (file ~ /ui\.cpp$/ && before ~ /word\.compare\(/) ok = 1
                            if (!ok) printf "%s:%d: %s\n", file, NR, s
                        }
                        text = substr(text, RSTART + RLENGTH)
                    }
                }
            }
            /\)page"/ { raw = 0 }
        ' "$f"
    done
)
if [ -n "$bad" ]; then
    echo "The device would show SELECT spelled out (it is drawn as SEL):"
    echo "$bad"
    exit 1
fi
# A button's name goes to the screen through buttonLabel ("SEL"). buttonName is
# the word in controls.txt, and only the two lines that write and read that
# file may use it.
names=$(grep -n 'buttonName(' "$app"/*.cpp | grep -v '/input\.cpp:' | grep -v 'controls\.cpp:.*put(out, buttonName(' | grep -v 'controls\.cpp:.*name == buttonName(')
if [ -n "$names" ]; then
    echo "buttonName is the word in controls.txt; the screen uses buttonLabel:"
    echo "$names"
    exit 1
fi
# The check must be able to see: the hints it allows are there to be found.
grep -q 'hint("SELECT"' "$app"/song_screen.cpp || { echo "words_test: found no SELECT hint at all; is the folder right?"; exit 1; }
echo "words test passed"
