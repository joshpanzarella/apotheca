#!/usr/bin/env python3
"""Makes app/chord_packs.inc: the chord packs that ship, compiled into the program.

Reads every assets/chords/*.json (the packs as written, by hand: edit those),
in file-name order, and writes each one, its spaces taken out, as a raw string
app/chords.cpp holds in a table. Compiled in, a pack comes with an update over
WiFi; a file in the `chords` folder on the card still adds a pack, or takes the
place of a shipped one of the same name.

Run it after any change to assets/chords:

    tools/chord_packs.py

`ctest -R chord` fails when the .inc is not the files (testTheShippedPacksAreTheFiles).
"""
import glob
import json
import os
import sys

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
files = sorted(glob.glob(os.path.join(here, "assets", "chords", "*.json")))
out = os.path.join(here, "app", "chord_packs.inc")

lines = [
    "// The chord packs that ship: assets/chords/*.json, one raw string each, in file-name order.",
    "// Written by tools/chord_packs.py: do not edit; edit the .json files and run it again.",
]
for path in files:
    with open(path, encoding="utf-8") as f:
        text = json.dumps(json.load(f), separators=(",", ":"), ensure_ascii=True)
    if ")pack\"" in text:
        sys.exit(f"{path}: holds the raw string's end")
    lines.append(f"// {os.path.basename(path)}")
    lines.append(f'R"pack({text})pack",')
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print(f"wrote {out}: {len(files)} packs")
