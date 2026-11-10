#!/bin/sh
# Build Apotheca for the handheld (64-bit ARM Linux) and pack it as a port.
#
#   scripts/build-handheld.sh
#
# Output: build-handheld/Apotheca-port.zip, holding
#   Apotheca.sh         the launcher the firmware's Ports menu shows
#   apotheca/apotheca   the program
#
# Runs on Linux. Needs clang, lld, curl, dpkg-deb and zip.
#
# Why the old Ubuntu packages: a program built against new system libraries
# will not start on a device with older ones, but the reverse works. So this
# builds against Ubuntu 20.04's ARM libraries, which are older than anything
# the handheld firmwares ship. The C++ library is linked into the program;
# the only things it asks the device for are the C library and SDL2.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/build-handheld"
sysroot="$out/sysroot"
mirror="http://ports.ubuntu.com/ubuntu-ports"

packages="
pool/main/g/glibc/libc6_2.31-0ubuntu9_arm64.deb
pool/main/g/glibc/libc6-dev_2.31-0ubuntu9_arm64.deb
pool/main/libx/libxcrypt/libcrypt1_4.4.10-10ubuntu4_arm64.deb
pool/main/libx/libxcrypt/libcrypt-dev_4.4.10-10ubuntu4_arm64.deb
pool/main/l/linux/linux-libc-dev_5.4.0-26.30_arm64.deb
pool/main/g/gcc-10/libgcc-s1_10-20200411-0ubuntu1_arm64.deb
pool/main/g/gcc-10/libstdc++6_10-20200411-0ubuntu1_arm64.deb
pool/main/g/gcc-9/libgcc-9-dev_9.3.0-10ubuntu2_arm64.deb
pool/main/g/gcc-9/libstdc++-9-dev_9.3.0-10ubuntu2_arm64.deb
pool/universe/libs/libsdl2/libsdl2-2.0-0_2.0.10+dfsg1-3_arm64.deb
pool/universe/libs/libsdl2/libsdl2-dev_2.0.10+dfsg1-3_arm64.deb
"

if [ ! -f "$sysroot/.ready" ]; then
    echo "fetching the ARM libraries to build against..."
    mkdir -p "$out/debs" "$sysroot"
    for p in $packages; do
        deb="$out/debs/$(basename "$p")"
        [ -f "$deb" ] || curl -fsSL -o "$deb" "$mirror/$p"
        dpkg-deb -x "$deb" "$sysroot"
    done
    # The packages hold links that point at /lib/... on a real system.
    # Re-aim them inside the sysroot, or the linker cannot follow them.
    find "$sysroot" -type l | while read -r link; do
        target=$(readlink "$link")
        case "$target" in
        /*) ln -sfn "$sysroot$target" "$link" ;;
        esac
    done
    touch "$sysroot/.ready"
fi

pack="$out/port"
rm -rf "$pack"
mkdir -p "$pack/apotheca"

echo "building..."
clang++ --target=aarch64-linux-gnu --sysroot="$sysroot" -fuse-ld=lld \
    -std=c++17 -O2 -mcpu=cortex-a53 -Wall -Wextra \
    -I"$here/engine/include" -I"$sysroot/usr/include/SDL2" -D_REENTRANT \
    "$here/engine/src/engine.cpp" \
    "$here/app/main.cpp" "$here/app/input.cpp" "$here/app/realtime.cpp" \
    -o "$pack/apotheca/apotheca" \
    -lSDL2 -lpthread -static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined

cp "$here/scripts/Apotheca.sh" "$pack/Apotheca.sh"
chmod +x "$pack/Apotheca.sh" "$pack/apotheca/apotheca"

rm -f "$out/Apotheca-port.zip"
(cd "$pack" && zip -qr "$out/Apotheca-port.zip" Apotheca.sh apotheca)
echo "wrote $out/Apotheca-port.zip"
