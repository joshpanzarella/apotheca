#!/bin/sh
# SELECT is drawn as SEL on the device (the owner's, 2026-11-12).
# And no maker's or model's name is shown (the owner's, 2026-11-14: the synth
# is JAX-EP): none in a text of the app, on the two web pages or in README.md.
#
# This reads the app's sources for every piece of text that says SELECT (in
# any case) and fails on one the screen would show spelled out.
# usage: words_test.sh APP_FOLDER
#
# Allowed, because they do not reach the screen as written:
#   - a hint's key (any screen) and a help-card line's key (app.cpp): ui.cpp
#     draws them as SEL. ui_test and app_test look at the pixels of the bar
#     and of the card, so that is seen, not taken on trust;
#   - buttonName's own word, which is the word in controls.txt (the screen
#     uses buttonLabel, checked in controls_test);
#   - ui.cpp's own comparison;
#   - the two web pages (design_page.cpp, and the page in transfer.cpp, a raw
#     string marked "page"), read on a big screen: they keep the full word.
# Anything else: write SEL, or pass the text through a hint.
#
# It reads C++ the way a compiler does, a letter at a time: comments (both
# kinds) are not text, \" does not end a text, // inside a text is not a
# comment, and "SEL" "ECT" is one text. What it cannot see: a word put
# together while the program runs. At its end it plants each of these in a
# folder of its own and checks that it is caught.
app=$1
[ -d "$app" ] || { echo "usage: words_test.sh APP_FOLDER"; exit 2; }

# Prints a line for each fault in the folder; nothing if it is clean.
scan() {
    for f in "$1"/*.cpp "$1"/*.hpp; do
        [ -f "$f" ] || continue
        case $f in */design_page.cpp) continue ;; esac
        awk -v file="$f" '
            function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
            # A whole text has ended (every piece of it): does the screen show SELECT?
            function ended(   low, ok) {
                if (!have) return
                have = 0
                low = tolower(str)
                # No name of a maker or of a model where a player reads (owner, 2026-11-14: the synth is JAX-EP).
                # (The name of a file in an #include is not on the screen.)
                if (low ~ /roland|jx-?3p|pg-?200|juno/ && ctx !~ /^#[ \t]*include/) printf "%s:%d: a maker\047s or a model\047s name: \"%s\"\n", file, strline, str
                if (low !~ /(^|[^a-z])select([^a-z]|$)/) return
                ok = 0
                if (ctx ~ /hint\($/) ok = 1                                                    # hint("SELECT...", ...)
                if (file ~ /\/app\.cpp$/ && ctx == "{") ok = 1                                 # {"SELECT, tapped", ...}: a help-card key
                if (file ~ /\/input\.cpp$/ && ctx == "case Button::Select: return") ok = 1     # the word in controls.txt
                if (file ~ /\/ui\.cpp$/ && ctx ~ /word\.compare\(0, 6,$/) ok = 1               # the rule itself
                if (!ok) printf "%s:%d: \"%s\"\n", file, strline, str
            }
            function begin() {
                if (!have) { have = 1; str = ""; ctx = trim(code); strline = NR }
                code = code "\"\""
            }
            # buttonName is the word in controls.txt; only these lines may use it.
            function names(   c) {
                if (code !~ /buttonName/) return
                c = trim(code)
                if (file ~ /\/input\.hpp$/ && c == "const char* buttonName(Button button);") return
                if (file ~ /\/input\.cpp$/ && c == "const char* buttonName(Button button) {") return
                if (file ~ /\/input\.cpp$/ && c == "const char* buttonLabel(Button button) { return button == Button::Select ? \"\" : buttonName(button); }") return
                if (file ~ /\/controls\.cpp$/ && c == "for (int i = 0; i < static_cast<int>(Button::Count); ++i) put(out, buttonName(static_cast<Button>(i)), button[i]);") return
                if (file ~ /\/controls\.cpp$/ && c == "if (name == buttonName(static_cast<Button>(i))) made.button[i] = c;") return
                printf "%s:%d: buttonName on the way to the screen (use buttonLabel): %s\n", file, NR, c
            }
            {
                n = length($0)
                i = 1
                code = ""
                if (state == "line") state = ""
                while (i <= n) {
                    c = substr($0, i, 1)
                    two = substr($0, i, 2)
                    if (state == "block") {
                        if (two == "*/") { state = ""; i += 2 } else i++
                    } else if (state == "text") {
                        if (c == "\\") { str = str substr($0, i, 2); i += 2 }
                        else if (c == "\"") { state = ""; i++ }
                        else { str = str c; i++ }
                    } else if (state == "raw") {
                        if (substr($0, i, length(rawEnd)) == rawEnd) { state = ""; i += length(rawEnd) }
                        else { if (!page) str = str c; i++ }
                    } else if (two == "//") {
                        state = "line"; i = n + 1
                    } else if (two == "/*") {
                        state = "block"; i += 2
                    } else if (c == "\"") {
                        if (substr(code, length(code), 1) == "R" && substr(code, length(code) - 1, 1) !~ /[A-Za-z0-9_]/) {
                            # R"mark( ... )mark": the page served over WiFi is marked "page" and is left out.
                            code = substr(code, 1, length(code) - 1)
                            rest = substr($0, i + 1)
                            mark = substr(rest, 1, index(rest, "(") - 1)
                            rawEnd = ")" mark "\""
                            page = (mark == "page")
                            if (!page) begin()
                            state = "raw"; i += length(mark) + 2
                        } else {
                            begin()
                            state = "text"; i++
                        }
                    } else if (c == "\047" && substr(code, length(code), 1) !~ /[A-Za-z0-9_]/) {
                        # a letter in single quotes (\047 is the quote): not a text, and a " in it starts none
                        i++
                        while (i <= n && substr($0, i, 1) != "\047") i += (substr($0, i, 1) == "\\") ? 2 : 1
                        i++
                        ended()
                        code = code "\047\047"
                    } else {
                        if (c != " " && c != "\t") ended()   # code after a text: the text is whole
                        code = code c; i++
                    }
                }
                if (state == "raw" && !page) str = str "\n"
                names()
            }
            END { ended() }
        ' "$f"
    done
}

# The two web pages and the manual are read by a player too. They keep SELECT in full, so scan()
# leaves them out; a maker's or a model's name is looked for in them here, comments and all.
makers() {
    for f in "$1"/design_page.cpp "$1"/transfer.cpp "$1"/../README.md; do
        [ -f "$f" ] || continue
        grep -n -i -E 'roland|jx-?3p|pg-?200|juno' "$f" | sed "s|^|$f:|"
    done
}

bad=$(scan "$app"; makers "$app")
if [ -n "$bad" ]; then
    echo "The device would show SELECT spelled out (it is drawn as SEL), or a maker's name (the synth is JAX-EP):"
    echo "$bad"
    exit 1
fi
# The check must be able to see: the hints it allows are there to be found.
grep -q 'hint("SELECT"' "$app"/song_screen.cpp || { echo "words_test: found no SELECT hint at all; is the folder right?"; exit 1; }
[ "$2" = "--only" ] && { echo "words test passed"; exit 0; }

# The check checked: each of these, planted in a folder of its own, must be caught, and the last must not.
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
fails=0
plant() { # plant NAME FILE WANT(caught|clean) : the text comes on stdin
    mkdir -p "$tmp/$1"
    cat > "$tmp/$1/$2"
    got=$(scan "$tmp/$1"; makers "$tmp/$1")
    if [ "$3" = caught ] && [ -z "$got" ]; then echo "words_test: not caught: $1"; fails=$((fails + 1)); fi
    if [ "$3" = clean ] && [ -n "$got" ]; then echo "words_test: caught, and should not be: $1: $got"; fails=$((fails + 1)); fi
}
plant plain x.cpp caught <<'PLANT'
    fonts.small.draw(r, x, y, "also SELECT + L1 / R1", kDim);
PLANT
plant quotes-inside x.cpp caught <<'PLANT'
    note = "also \"SELECT\" + L1 / R1";
PLANT
plant strokes-inside x.cpp caught <<'PLANT'
    note = "a//b"[0] ? "also SELECT + L1 / R1" : "";
PLANT
plant after-a-block-comment x.cpp caught <<'PLANT'
    note = /* " */ "SELECT + Y";
PLANT
plant after-a-quote-letter x.cpp caught <<'PLANT'
    note = c == '"' ? "SELECT + Y" : "";
PLANT
plant small-letters x.cpp caught <<'PLANT'
    note = "also Select + L1 / R1";
PLANT
plant in-two-pieces x.cpp caught <<'PLANT'
    const char* key = "SEL" "ECT";
PLANT
plant in-two-lines x.cpp caught <<'PLANT'
    const char* key = "HOLD SEL"
                      "ECT + Y";
PLANT
plant a-raw-text x.cpp caught <<'PLANT'
    const char* key = R"x(SELECT + Y)x";
PLANT
plant the-last-thing-in-a-file x.hpp caught <<'PLANT'
#define kKey "SELECT + Y"
PLANT
plant a-table-row-elsewhere x.cpp caught <<'PLANT'
    const Line lines[] = {
        {"SELECT, tapped", "the SONG screen"},
    };
PLANT
plant a-second-argument x.cpp caught <<'PLANT'
    hint("Y", "what SELECT does");
PLANT
plant the-file-word-in-a-header x.hpp caught <<'PLANT'
inline const char* shown(Button b) { return buttonName(b); }
PLANT
plant the-file-word-in-brackets settings_screen.cpp caught <<'PLANT'
    fonts.medium.drawCentered(r, x, y, (buttonName)(order[i]), kDim);
PLANT
plant the-file-word-after-strokes x.cpp caught <<'PLANT'
    std::snprintf(text, sizeof text, "a//b %s", buttonName(last));
PLANT
plant a-makers-name x.cpp caught <<'PLANT'
    {TrackKind::Jx3p, "JAX-EP", "JAX", "JAX-EP", "a copy of the Roland", false, "jaxep", SynthKind::Jx3p, ".jaxep"},
PLANT
plant a-models-name x.cpp caught <<'PLANT'
    jx3pRow("JX-3P, 6 notes", 0);
PLANT
plant a-models-name-small x.hpp caught <<'PLANT'
constexpr const char* kEnding = ".jx3p";
PLANT
plant the-programmers-name x.cpp caught <<'PLANT'
    fonts.small.draw(r, x, y, "AS THE PG-200 READS", kDim);
PLANT
plant a-makers-name-on-the-page design_page.cpp caught <<'PLANT'
<div id="nothing" hidden>Move to a PHAZED or JX-3P track.</div>
PLANT
plant a-makers-name-on-the-wifi-page transfer.cpp caught <<'PLANT'
const char* const kPage = R"page(<p>Presets for the Juno are kept here.</p>
)page";
PLANT
plant what-is-allowed app.cpp clean <<'PLANT'
#include "apotheca/jx3p.hpp"
// "the JX-3P" in a comment is not on the screen
    const Jx3pPatch first = Jx3p::presetPatch(0); // "PG-200"
// "SELECT + Y" in a comment is not on the screen
/* nor "SELECT" here,
   nor "SELECT" on its second line */
const char* const kPage = R"page(<p>Hold SELECT, then "X".</p>
<select id="into"></select>
)page";
    hint("SELECT", "songs");
    hint("HOLD SELECT", "help");
        {"SELECT, tapped", "the SONG screen"},
    note = "SELECTED" "SELECTS" "RESELECT";
    note = "also SEL + L1 / R1";
    char q = '"'; // SELECT
PLANT
if [ "$fails" -ne 0 ]; then exit 1; fi
echo "words test passed"
