#!/bin/sh
# Build Apotheca's own system image for the Anbernic RG40XXV.
#
#   scripts/build-image.sh [--dtb rg40xx-v|rg40xx-v-v2-panel|rg40xx-v-v2-stock-panel] [--dram lpddr4|lpddr3]
#                          [--tidy|--tidy-all] [-- MAKE-ARGS...]
#
# Output: build-image/apotheca-rg40xxv.img.gz (and its .sha256), ready for
# balenaEtcher or Raspberry Pi Imager. os/README.md says how to use it.
#
#   --dtb    which device tree the card starts with (dtb.img on its boot
#            partition; both are on it, and the other can be copied over).
#            Default rg40xx-v-v2-panel: the owner's unit has the newer screen
#            (it showed a picture with it, 2026-11-17; the first tree left
#            only coloured stripes). rg40xx-v is for units with the first screen;
#            rg40xx-v-v2-stock-panel is the newer screen with Anbernic's own
#            pixel clock edge and timings (a choice to compare, not the default).
#   --dram   the memory type U-Boot sets up. Default lpddr4 (ROCKNIX lists
#            the RG40XX V under its DDR4 image).
#   --tidy   after the build, delete the work files of the biggest folders
#            that are built once (Mesa, and the host LLVM, clang and SPIR-V
#            tools that build Mesa's shaders); they keep their "done" marks and
#            are not made again. **The kernel's and U-Boot's build folders are
#            kept**, so a change to either (a patch, a device tree, a panel
#            file, the config) rebuilds in minutes, not the hour a kernel from
#            nothing takes (about 2 GB more on the disk).
#   --tidy-all  as --tidy, and the kernel's and U-Boot's build folders too
#            (and the joypad module built against the kernel): the next build
#            makes all three from nothing.
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
    --tidy-all) tidy=all; shift ;;
    --) shift; break ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
case "$dtb" in rg40xx-v|rg40xx-v-v2-panel|rg40xx-v-v2-stock-panel) ;; *) echo "--dtb: rg40xx-v, rg40xx-v-v2-panel or rg40xx-v-v2-stock-panel" >&2; exit 2 ;; esac
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

# A kernel module built outside the kernel must be made again whenever the
# kernel is: the new kernel refuses an old one (its symbol versions), and
# Buildroot does not rebuild a package because one it needs was rebuilt.
# The kernel's "built" mark newer than the joypad module's: make the module
# again, and the image with it.
kstamp=$(ls "$out"/build/linux-[0-9]*/.stamp_built 2>/dev/null | head -n 1)
jstamp=$(ls "$out"/build/rocknix-joypad-*/.stamp_built 2>/dev/null | head -n 1)
if [ -n "$kstamp" ] && [ -n "$jstamp" ] && [ "$kstamp" -nt "$jstamp" ]; then
    echo "the kernel was built again: the joypad module too"
    make -C "$br" O="$out" rocknix-joypad-rebuild
    make -C "$br" O="$out" -j"$(nproc)"
fi

cp "$out/images/apotheca-rg40xxv.img.gz" "$work/apotheca-rg40xxv.img.gz"
(cd "$work" && sha256sum apotheca-rg40xxv.img.gz > apotheca-rg40xxv.img.gz.sha256)

if [ "$tidy" = all ]; then
    rm -rf "$out"/build/linux-[0-9]* "$out"/build/uboot-[0-9]* "$out"/build/rocknix-joypad-*
fi
if [ "$tidy" != no ]; then
    # These are kept as done (their stamps stay), so the next build does not
    # make LLVM and clang again; only their work files go.
    for d in "$out"/build/host-llvm-[0-9]* "$out"/build/host-clang-[0-9]* \
             "$out"/build/host-libclc-[0-9]* "$out"/build/host-spirv-llvm-translator-[0-9]* \
             "$out"/build/host-spirv-tools-[0-9]* \
             "$out"/build/host-mesa3d-[0-9]* "$out"/build/mesa3d-[0-9]*; do
        [ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 1 ! -name '.stamp_*' -exec rm -rf {} +
    done
fi

echo
echo "image: $work/apotheca-rg40xxv.img.gz ($dtb, $dram)"
cat "$work/apotheca-rg40xxv.img.gz.sha256"
