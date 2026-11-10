#!/bin/sh
# Launcher for the handheld's Ports menu. Sits beside the "apotheca" folder.
# Anything the program prints goes to apotheca/apotheca.log, so a failed
# start leaves a reason behind.

dir="$(cd "$(dirname "$0")" && pwd)/apotheca"
cd "$dir" || exit 1
./apotheca > apotheca.log 2>&1
