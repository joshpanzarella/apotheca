# The RG40XXV board files: where each came from

Everything here that we did not write was taken from **ROCKNIX**
(https://github.com/ROCKNIX/distribution), branch `next`, commit
`ad9e48d1492f272db9c742d0cfa6d81769ceee22` (2026-11-16). `VENDORED.sha256`
holds each file's sum as taken from there.

**Eight kernel patches were refreshed, not changed** (0002, 0010, 0110, 0126,
0127, 0140, 0153, 0156). ROCKNIX applies its patches allowing "fuzz" (the
lines around a change may differ a little); Buildroot does not, so those
eight did not apply. Each was applied as ROCKNIX applies it to Linux 7.2,
and written again as the exact difference it made, under its original
header. Applied in order, the refreshed set and ROCKNIX's set give **the same
source tree to the byte** (git tree `81f1e845879e`, checked 2026-11-17). The
originals, with every author's header of the two that are whole mail series
(0002, 0110), are in `rocknix-originals/`, as taken; the other files listed
in `VENDORED.sha256` still match it.

ROCKNIX's licence (`LICENSE.md` there): its own software and scripts are
GPL-2.0; changes it makes to other software are under that software's
licence; its branding is CC BY-NC-SA 4.0 (**none of the branding is here**).

| Here | From ROCKNIX | Licence |
| --- | --- | --- |
| `patches/linux/0002` to `0901` (31 patches; eight refreshed, see above) | `projects/ROCKNIX/devices/H700/patches/linux/` (all but the one ROCKNIX ships disabled, `0203-...ahub.patch.disabled`) | GPL-2.0: changes to Linux. Authors in each patch's `From:` line (Philippe Simons, Jernej Skrabec, James Hilliard, Hironori Kikuchi, Chris Morgan, Jacob Cook, Kailash, tiopex; two have no header) |
| `patches/linux/0000-mainline-0003-pwm-add-pwm_set_period.patch`, `0000-mainline-0004-input-adc-keys-redirect-keycode-316-to-rocknix-joypa.patch` | `projects/ROCKNIX/packages/linux/patches/mainline/` (0003 by ROCKNIX, 0004 by spycat88 and Paul Reioux), renamed so they go first, as ROCKNIX applies its `mainline` folder before the device's. The joypad driver needs both: `pwm_set_period` for rumble, and the `joypad_input_g` symbol 0004 exports from `adc-keys`. (0005 in that folder, a Bluetooth chip the RG40XXV does not have, is left out) | GPL-2.0 |
| `patches/linux/linux.hash` | ours: the sum kernel.org signs for `linux-7.2.tar.xz` (ROCKNIX pins the same) | - |
| `dts/allwinner/sun50i-h700-anbernic-rg40xx-v.dts`, `...-rg40xx-v-v2-panel.dts` | `projects/ROCKNIX/devices/H700/linux/dts/allwinner/` | GPL-2.0-only OR BSD-2-Clause (their own header), Philippe Simons |
| `firmware/panels/anbernic,rg40xx-panel.panel`, `...rg40xx-v2-panel.panel` | `projects/ROCKNIX/packages/linux-firmware/kernel-firmware/extra-firmware/panels/` | GPL-2.0 as ROCKNIX's own work (the files carry no header). **Data, not code**: the panel's start-up commands and timings in the format of the generic MIPI panel driver (patch 0110; https://kikuchan.github.io/panel-firmware-generator/) |
| `linux-rocknix-h700.config` | `projects/ROCKNIX/devices/H700/linux/linux.aarch64.conf` (ROCKNIX's whole H700 kernel config, for Linux 7.2.0) | GPL-2.0 (part of Linux's build) |
| `patches/uboot/0001`, `0002` | `projects/ROCKNIX/devices/H700/packages/u-boot-DDR4/patches/` (the DDR3 folder has the same two) | GPL-2.0+: changes to U-Boot. Philippe Simons; Kailash |
| `uboot.fragment`, `uboot-lpddr3.fragment` | ours: the lines where ROCKNIX's `anbernic_rg35xx_h700_lpddr4_defconfig` and `..._lpddr3_defconfig` differ from mainline U-Boot's `anbernic_rg35xx_h700_defconfig` | GPL-2.0+ (U-Boot config) |
| `patches/uboot/uboot.hash` | ours | - |

Everything else in `os/` (the scripts, the init, `genimage.cfg`, the
fragments' comments, the tools) is Apotheca's own. Two of those are made
from ROCKNIX's files and keep their licence:

| Here | Made from | Licence |
| --- | --- | --- |
| `firmware/panels/anbernic,rg40xx-v2-stock-panel.panel` | ROCKNIX's `anbernic,rg40xx-v2-panel.panel`, written by `os/tools/make-v2-stock-panel.py`: the same start-up commands, the pixel clock edge and the timings set to the values of Anbernic's own device tree for the RG40XX (`lcd0` in the boot package muOS ships, read on 2026-11-17; numbers, not code) | GPL-2.0, as ROCKNIX's file |
| `dts/allwinner/sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dts` | ROCKNIX's v2-panel tree, pointing at that file | GPL-2.0-only OR BSD-2-Clause, as ROCKNIX's tree |

**What Buildroot fetches** (each at a pinned version, each sum checked:
`BR2_DOWNLOAD_FORCE_CHECK_HASHES`): Linux 7.2 (GPL-2.0), U-Boot 2026.01
(GPL-2.0+), Trusted Firmware-A v2.15.0 (BSD-3-Clause), BusyBox, util-linux,
exfatprogs, alsa-lib, alsa-utils, evtest, Mesa 26.0.1 (MIT, with parts under
other permissive licences: Buildroot lists them), libdrm (MIT), expat, zlib,
SPIRV-Tools (Apache-2.0), SDL2 2.32.10 (Zlib), and Bootlin's toolchain (glibc,
libstdc++, libgcc: their runtime parts go on the card). LLVM, clang, libclc
and SPIRV-LLVM-Translator (Apache-2.0 with LLVM exception) are built for the
build machine only, to compile Mesa's shaders: **none of them is on the card**.

**Two changes to Buildroot itself**, in `../../patches-to-buildroot/` (each
says why; `scripts/build-image.sh` applies them once to its Buildroot tree):
panfrost without LLVM, clang and OpenCL on the target, and host Mesa
matching host LLVM's RTTI. They change only how Mesa is built. Their licences are
recorded by Buildroot itself: `scripts/build-image.sh -- legal-info` gathers
every source, patch and licence text into `build-image/output/legal-info/`.

**One gap in legal-info, to close before the image goes to anyone**: Bootlin's
toolchain is recorded with licence "unknown" (Buildroot does not list its
licence files). Its parts on the card are glibc (LGPL-2.1+) and GCC's
libstdc++ and libgcc (GPL-3.0 with the GCC Runtime Library Exception);
Bootlin publishes each toolchain's sources and its own legal-info beside it
(https://toolchains.bootlin.com, aarch64 glibc stable 2025.08-1). Apotheca's
own source is marked not to be redistributed (`APOTHECA_REDISTRIBUTE = NO`)
until the owner chooses its licence.

Nothing on the card is a binary without source except the two panel files
above (data). The WiFi and Bluetooth firmware (Realtek, from linux-firmware)
comes in batch 5, and its terms will be recorded here then.
