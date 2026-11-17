#!/bin/sh
# Build Apotheca's own system image for the Anbernic RG40XXV.
#
#   scripts/build-image.sh [--dtb rg40xx-v|rg40xx-v-v2-panel] [--dram lpddr4|lpddr3]
#                          [--tidy] [-- MAKE-ARGS...]
#
# Output: build-image/apotheca-rg40xxv.img.gz (and its .sha256), ready for
# balenaEtcher or Raspberry Pi Imager. os/README.md says how to use it.
#
#   --dtb    which device tree the card starts with (dtb.img on its boot
#            partition; both are on it, and the other can be copied over).
#            Default rg40xx-v-v2-panel: the owner's unit has the newer screen
#            (it showed a picture with it, 2026-11-17; the first tree left
#            only coloured stripes). rg40xx-v is for units with the first screen.
#   --dram   the memory type U-Boot sets up. Default lpddr4 (ROCKNIX lists
#            the RG40XX V under its DDR4 image).
#   --tidy   after the build, delete the biggest build folders (the kernel,
#            U-Boot, Mesa and the host LLVM and clang that build Mesa's
#            shaders). The kernel and U-Boot are made again next time; the
#            others keep their "done" marks and are not.
#   -- ARGS  passed to Buildroot's make (e.g. -- linux-menuconfig).
#
# Buildroot itself is fetched (a pinned release, its sum checked) into
# build-image/, with its downloads (build-image/dl) and its work
# (build-image/output). All of build-image/ is ignored by git.
#
# Runs on Linux (Ubuntu 24.04 on the desk). Needs what Buildroot needs:
# make, gcc, g++, patch, perl, python3, rsync, cpio, unzip, bc, file, wget.

set -eu

BR_VERSION=2026.02.3
BR_SHA256=5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb

here=$(cd "$(dirname "$0")/.." && pwd)
work="$here/build-image"
br="$work/buildroot-$BR_VERSION"
out="$work/output"
external="$here/os"

dtb=rg40xx-v-v2-panel
dram=lpddr4
tidy=no
while [ $# -gt 0 ]; do
    case "$1" in
    --dtb) dtb=$2; shift 2 ;;
    --dram) dram=$2; shift 2 ;;
    --tidy) tidy=yes; shift ;;
    --) shift; break ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
case "$dtb" in rg40xx-v|rg40xx-v-v2-panel) ;; *) echo "--dtb: rg40xx-v or rg40xx-v-v2-panel" >&2; exit 2 ;; esac
case "$dram" in lpddr4|lpddr3) ;; *) echo "--dram: lpddr4 or lpddr3" >&2; exit 2 ;; esac

mkdir -p "$work/dl"

# Buildroot, pinned
if [ ! -f "$br/.ready" ]; then
    tarball="$work/buildroot-$BR_VERSION.tar.xz"
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.xz"
    echo "$BR_SHA256  $tarball" | sha256sum -c -
    rm -rf "$br"
    tar xf "$tarball" -C "$work"
    touch "$br/.ready"
fi

# Our few changes to Buildroot itself (os/patches-to-buildroot, each saying
# why), applied once each.
for p in "$external"/patches-to-buildroot/*.patch; do
    [ -f "$p" ] || continue
    mark="$br/.applied-$(basename "$p")"
    [ -f "$mark" ] && continue
    patch -d "$br" -p1 -N --no-backup-if-mismatch < "$p"
    touch "$mark"
done

export BR2_DL_DIR="$work/dl"
export APOTHECA_DTB="$dtb"
APOTHECA_COMMIT=$(git -C "$here" describe --always --dirty 2>/dev/null || echo unknown)
export APOTHECA_COMMIT

make -C "$br" O="$out" BR2_EXTERNAL="$external" apotheca_rg40xxv_defconfig

if [ "$dram" = lpddr3 ]; then
    frag="$external/board/rg40xxv/uboot.fragment $external/board/rg40xxv/uboot-lpddr3.fragment"
    sed -i "s|^BR2_TARGET_UBOOT_CONFIG_FRAGMENT_FILES=.*|BR2_TARGET_UBOOT_CONFIG_FRAGMENT_FILES=\"$frag\"|" "$out/.config"
    make -C "$br" O="$out" olddefconfig
fi

if [ $# -gt 0 ]; then
    make -C "$br" O="$out" "$@"
    exit 0
fi

make -C "$br" O="$out" -j"$(nproc)"

cp "$out/images/apotheca-rg40xxv.img.gz" "$work/apotheca-rg40xxv.img.gz"
(cd "$work" && sha256sum apotheca-rg40xxv.img.gz > apotheca-rg40xxv.img.gz.sha256)

if [ "$tidy" = yes ]; then
    rm -rf "$out"/build/linux-[0-9]* "$out"/build/uboot-[0-9]*
    # These are kept as done (their stamps stay), so the next build does not
    # make LLVM and clang again; only their work files go.
    for d in "$out"/build/host-llvm-[0-9]* "$out"/build/host-clang-[0-9]* \
             "$out"/build/host-libclc-[0-9]* "$out"/build/host-spirv-llvm-translator-[0-9]* \
             "$out"/build/host-mesa3d-[0-9]* "$out"/build/mesa3d-[0-9]*; do
        [ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 1 ! -name '.stamp_*' -exec rm -rf {} +
    done
fi

echo
echo "image: $work/apotheca-rg40xxv.img.gz ($dtb, $dram)"
cat "$work/apotheca-rg40xxv.img.gz.sha256"
