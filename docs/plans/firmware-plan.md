# Firmware plan: an Apotheca-only system for the RG40XXV

Owner, 2026-11-16: "I would like to start planning how we are going to replace
the firmware on the RG40XXV with our own. I want this machine to be strictly
dedicated to Apotheca. I want full control of the OS so we can make this as
good as possible."

**This is a plan. No program code is in it, and nothing here has run on the
handheld.** Facts marked **(guess)** are not confirmed by a source; facts
marked **(source)** are, and the list is at the end.

## The answer, short

- **Build our own small image with Buildroot** (a tool that builds a whole
  Linux system from source into one SD card image), on the **mainline kernel**
  (the public Linux kernel, not Allwinner's old fork) **with ROCKNIX's H700
  patches**, and mainline U-Boot (the boot loader).
- **Before any of that, spend one evening on ROCKNIX** (an open firmware that
  already runs the RG40XXV on a mainline kernel with the GPU driver): put
  Apotheca on it as a port and run SPEED TEST. That says what mainline and the
  GPU are worth before we build anything.
- **The device cannot be bricked by this.** It has no internal storage; it
  boots from the SD card. The stock card in the drawer is the way back.
- **Biggest expected wins**: the screen drawn by the GPU instead of the CPU
  (13 ms of CPU a frame today, **(guess)** 1 to 3 ms after), Apotheca on screen
  a few seconds after power on, a real sleep, a shutdown before the battery
  runs flat, and a sound path we own end to end.

## 1. The device as it is

### Hardware

| Part | What it is | How sure |
| --- | --- | --- |
| SoC (the main chip) | **Allwinner H700**, a sibling of the H616/H618 | source (Anbernic) |
| CPU | 4 x Cortex-A53. **1512 MHz, governor already "performance"** on the owner's unit | source (owner's SPEED TEST, 0.573) |
| L2 cache | 256 KB is assumed (the H616 family's); the H700's is not published | guess (speed plan) |
| GPU | **Mali-G31 MP2**. Open driver: **panfrost** in Mesa, OpenGL ES 3.1 | source (Anbernic, ROCKNIX) |
| RAM | **1 GB LPDDR4** | source (Anbernic, ROCKNIX) |
| Screen | 4.0" IPS, 640x480, laminated. Driven over a parallel RGB bus with a small SPI setup line | size: source. Bus: guess (a forum post; ROCKNIX patches "panel-mipi-dpi-spi" and "rgb connector") |
| Buttons | GPIO buttons (each wired to a pin of the chip) | source (mainline device trees of the sister models) |
| Stick | Analog, read by the chip's ADC through a mux (`adc-joystick`); ROCKNIX uses its own joypad driver | source (mainline RG35XX-H tree, ROCKNIX patch 0140) |
| Sound | The SoC's own codec (H616 codec, in mainline since late 2024) into a speaker amplifier and a 3.5 mm jack | codec: source. Speaker count, amp, jack detect: guess |
| WiFi / BT | **Realtek RTL8821CS** on SDIO, BT 4.2 on a UART. Mainline driver `rtw88` | source for the RG35XX Plus; **guess for the RG40XXV** (same family; Anbernic says WiFi 5 and BT 4.2) |
| Power chip (PMIC) | **X-Powers AXP717**: regulators, battery gauge, charger, power key | source (mainline trees of the sister models; mainline U-Boot `AXP717_POWER`) |
| Battery | 3200 mAh Li-polymer, 5 V / 1.5 A charging over USB-C | source (Anbernic) |
| Storage | **No eMMC. Two microSD slots**: TF1 holds the system, TF2 may hold data | source (ROCKNIX: "eMMC: None") |
| Debug serial (UART) | 4 pinholes on the board (on the RG35XXSP: bottom right under the back cover), 3.3 V, 115200 baud | source for the RG35XXSP and RG35XX Plus; **RG40XXV place unknown** |
| Board revisions | Newer units of the H700 family need other device trees ("rev6", "v2"); the only way to know is to try | source (ROCKNIX install guide) |

### The firmware on it today

| Thing | What we know |
| --- | --- |
| Stock kernel | **Allwinner BSP 4.9.170** (a vendor kernel from 2019's 4.9 line). **Anbernic has not published its source** | source (Knulli) |
| Stock boot | BROM (the chip's built-in first loader) reads boot0 from the card (at 8 KB on stock), then U-Boot, then an Android-style boot image holding the kernel and device tree | source (Knulli, macromorgan) |
| Stock user space | Not known to us. **(guess)** an Ubuntu-style Linux with Anbernic's own frontend | guess |
| How Apotheca runs | As a **port**: `Apotheca.sh` in the firmware's ports folder starts `apotheca --handheld`. The program asks the system only for glibc 2.29+ and SDL2 (built against Ubuntu 20.04's SDL **2.0.10**). It renders with whatever `SDL_CreateRenderer` gives (the owner's SPEED TEST says the software renderer is suspected), asks for 48 kHz float, 256 frames (5.3 ms), SCHED_FIFO 70 for the audio thread and 69 for 3 helpers, `mlockall`, flush-to-zero | source (our code) |
| Which firmware the owner runs | CLAUDE.md says "stock firmware", but the launcher is written for a Ports menu, which Knulli and muOS have. **`apotheca/device-info.log` on the card names it** (PRETTY_NAME, kernel) | open: question 1 |
| Updates today | The WiFi page sets a new program aside (`apotheca.pending` / `apotheca.new`); the launcher swaps it in at the next start and goes back if it does not run 3 s | source (our code) |

## 2. The routes

| | **a. Strip an open firmware** (ROCKNIX) | **b. Our own Buildroot image** on ROCKNIX's mainline kernel work | **c. Stay on stock, tune** |
| --- | --- | --- | --- |
| Kernel | Mainline 7.0.x plus ROCKNIX's 37 H700 patches | The same kernel and patches, taken into our tree | BSP 4.9.170, **no source** |
| What we control | Everything in principle, but inside a large distro (Sway, EmulationStation, emulators, its own build system) | **Every file on the card** | Only our program and its launcher |
| Effort to first boot | Small: it already boots | Medium: 2 to 4 sessions to a console, more to sound and screen | None |
| Effort overall | Medium, and it never ends: we follow their releases or freeze a fork | Large at first, then small: a few hundred lines of config and scripts we own | Small |
| Risk | Low to start; high later (a big fork one person must keep) | Medium: bring-up of screen and sound on our own, with ROCKNIX's patches as the map | Low, but stuck |
| What we gain | GPU, mainline, WiFi, quick | All of a's gains, plus seconds-to-boot, nothing else running, our own update and rescue, read-only system | Little: the frontend already sleeps while a port runs |
| Upkeep | Rebase on every ROCKNIX release, or fall behind | Bump the kernel when we choose (an LTS line); the device has no other users to break | None, and no fixes either |
| Licence | GPL-2 for their code; some parts of ROCKNIX are under a NonCommercial licence, to be checked file by file before we ship any of it | GPL-2 kernel, U-Boot, BusyBox: we offer the sources (Buildroot gathers them: `make legal-info`) | Stock's kernel source is not published |

**Knulli and muOS are out** for this job: both run the stock 4.9.170 BSP kernel
taken from Anbernic's image, whose source is not published. We could not change
the kernel (no real-time kernel, no GPU driver of our own, no fixes), and we
could not meet the GPL if we gave the image to anyone. (Knulli is a Batocera
fork; muOS's frontend is GPL-3.)

**Yocto** (another system builder) would also work, but it is built for teams
and long-lived products; it is slower to learn and to build. **Buildroot** fits
one program on one device.

### Recommendation

**Route b, with route a as the measuring stick.**

1. ROCKNIX first, untouched, as a second firmware on a spare card: it is the
   cheapest real data on what mainline and panfrost do for Apotheca.
2. Then our own Buildroot image, taking ROCKNIX's H700 kernel patches and
   device tree (GPL-2, so we may) and mainline U-Boot's
   `anbernic_rg35xx_h700_defconfig` (LPDDR4 settings already in it).
3. If bring-up of screen or sound stalls for more than a batch, fall back to
   a: boot ROCKNIX's kernel and our root file system, and keep going.

Why not just a: ROCKNIX is a distribution for emulators. Its build makes
hundreds of packages we would delete, and its boot goes through Sway and a
frontend. Stripping it means owning a large fork. With b we own a small tree
and still use the hard part of their work, the kernel patches.

## 3. What "as good as possible" buys

Each line says what could change, how much, and how SPEED TEST (or a new row of
it) would show it. "Today" is the owner's 0.573 run unless it says otherwise.

| Area | Today | With our own system | How much it could matter | How we measure |
| --- | --- | --- | --- | --- |
| **Drawing the screen** | 13 ms of CPU a frame, software renderer suspected, on a core the audio thread shares | Mali-G31 through panfrost, Mesa, SDL2 on KMSDRM (the kernel's display API, no desktop) with the OpenGL ES 2 renderer | **The biggest single win.** (guess) 1 to 3 ms a frame. That frees most of a core while the screen moves: room for the third helper to stop sharing, or a fourth | SPEED TEST already prints the renderer. **Add a row that times a real present** (draw + `SDL_RenderPresent`) on the program's screen |
| **Boot to Apotheca** | Firmware boot, frontend, then the port: (guess) 20 to 40 s | U-Boot with no wait, a trimmed kernel, a tiny init that starts Apotheca at once | **Target under 6 s** from power to the SONG screen; 3 to 4 s with work | A stopwatch; the kernel's own timestamps (`dmesg`); Apotheca logs the time since boot when its first frame is drawn |
| **Nothing else running** | A frontend sleeping behind the port, its services, its daemons | Only Apotheca, the WiFi tools when WiFi is on, and the kernel | Small for CPU (they mostly sleep), **real for the worst block**: no stray process can take a core at the wrong moment | SPEED TEST's "worst block" and "late" counts over a long run |
| **Sound path** | SDL2 2.0.10 picks a driver (ALSA, maybe PulseAudio) and may convert the rate on the audio thread (the probe says) | ALSA straight to the codec, 48 kHz, our period size; a newer SDL2, or our own ALSA backend | Latency from a button to sound: today 256 frames (5.3 ms) at best plus what the driver adds; (guess) 2 periods of 128 frames, about 5 ms in all, is reachable | The probe row (what the card gives). A new row: the card's real buffer and period (`/proc/asound/card0/pcm0p/sub0/hw_params`) |
| **Real-time kernel** | 4.9 BSP, normal preemption | **PREEMPT_RT**, in mainline since 6.12 for arm64; or the plain low-latency setting first | Fewer late blocks under load (WiFi, SD writes). (guess) worst wake-up delay from milliseconds to under 0.1 ms | `cyclictest` (a standard latency test) on the device; SPEED TEST's late count with WiFi sending a file |
| **Cores** | 4 realtime threads share 4 cores with the screen | The audio thread pinned to one core, the helpers to two, the screen and the rest on the fourth; device interrupts (IRQs) kept off the audio core | Steadier blocks; the helpers' gain (59.5% to 53.8% of a block) may grow | SPEED TEST with and without pinning |
| **Clocks** | 1512 MHz, performance | The same. ROCKNIX ships 1.4 GHz with 1.5 GHz as an overclock | **None from clocks.** Going past 1.5 GHz risks heat, crashes and the battery; not proposed | SPEED TEST prints the clock before and after each row |
| **Sleep** | Whatever the firmware does | Real suspend to RAM (ROCKNIX has patches for display, codec and USB across suspend, and a 480 MHz suspend clock); the song stopped and saved first | Days of standby instead of a flat battery; instant back to the screen it was on | Battery percent before and after a night asleep; time from the power key to a drawn screen |
| **Battery** | Read from `/sys/class/power_supply` (our `battery.hpp`) | The same files from the AXP717 driver, plus: a charging screen with the device off, **a clean shutdown at a low level** (save, then power off), an auto power-off when idle | Protects songs: today a flat battery left an empty song once | The pull-the-power tests in batch 4 |
| **SD card** | The firmware's choice of file system and mount options | A **read-only system partition** (it can never be corrupted by a flat battery) and a separate data partition for songs and recordings, mounted `noatime` | Safety first; and `writeTextFile`'s fsync cost (not measured on the handheld) set by us | A new SPEED TEST row: write, fsync and rename a 32 KB song, ten times |
| **Updates** | The program alone, over WiFi, with a 3 s fallback | Keep that, and add **the whole system over WiFi into a second slot** (A/B: two system partitions; boot the new one, fall back to the old if it does not come up) | The launcher, fonts, chord packs and the system itself become updatable without the card leaving the device | Tried on the desk under qemu first (section 5) |
| **WiFi** | The firmware's settings menu | Our own: Apotheca's WiFi page gets a network list and password entry; **its own hotspot** if the chip's driver allows it (the README already waits for this) | No router needed in a studio or on the road | Joining, the transfer rate of a 30 MB file |
| **USB** | Unknown whether host mode works on stock | USB host on the USB-C port (ROCKNIX patches OTG host mode): **USB MIDI keyboards, USB audio in** with `snd-usb-audio` in our kernel | MIDI and recording without a computer | A keyboard plugged in: "audio inputs" in the log |
| **Bluetooth MIDI** | No | Later: BlueZ with its MIDI profile | Wireless keyboards | Later |

**Memory**: 1 GB, of which Apotheca uses tens of MB (`Project` 12 MB, undo,
recordings). A frontend left resident costs (guess) 50 to 150 MB. Small today,
but it is room for long recordings later.

## 4. Risks and how the device stays safe

| Risk | How likely | What we do |
| --- | --- | --- |
| **Bricking** (a device that will not start again) | **Very low.** There is no eMMC or SPI flash for software to overwrite; the boot code is on the card. With no bootable card the chip's BROM waits for a USB computer ("FEL" mode) | Keep the **stock card untouched in a drawer**. Every experiment is on a spare card |
| Hardware harm from wrong power settings | Low but real: the boot loader sets the PMIC's voltages, and the battery's charge limits are in the device tree | Use the values mainline U-Boot and ROCKNIX already ship for the H700 family; change none of them; no overclock |
| A board revision our device tree does not fit | Medium: Anbernic changed panels and RAM in later units | Batch 0 finds which of ROCKNIX's RG40XXV trees works on the owner's unit |
| Screen or sound do not come up on our image | Medium | ROCKNIX's patches are the reference; route a's kernel is the fallback |
| WiFi hotspot not possible with `rtw88` | Unknown | The hotspot is a later wish; joining a network is enough to ship |
| Upkeep | Ongoing | Pin one kernel line (an LTS); move only when a fix or feature is wanted |
| **Licences** | Certain to apply if the image is given to anyone | The kernel, U-Boot and BusyBox are GPL-2: whoever gets the image must be able to get the exact sources (Buildroot's `legal-info` makes the bundle). Realtek's WiFi/BT firmware files come from linux-firmware under Realtek's redistributable licence. Trusted Firmware-A (a small piece the boot needs) is BSD. Apotheca's own licences already travel in `licences/` |
| Time | High | The batches below stop at points that are useful by themselves |

## 5. Batches

Each batch is one job and ends with something the owner can try. "Desk" is
what can be checked without the handheld: **qemu cannot run the H700's
screen, GPU, sound, PMIC or WiFi**, so those always need the owner's hands.
The build runs in Docker on Linux, as `build-handheld.sh` does today.

| # | Job | Tested on the desk | The owner does | Risk |
| --- | --- | --- | --- | --- |
| **0** | **Measure ROCKNIX against today.** ROCKNIX H700 (the DDR4 image) on a spare card, the RG40XXV device tree picked, Apotheca copied in as a port, SPEED TEST run. Also: `device-info.log` from both cards | Nothing to build. We write the steps and a small script that gathers the logs | Flash the card, pick the tree, start Apotheca, run SPEED TEST, send both logs | Low. **The cheapest real data in this plan** |
| **1** | **A minimal image that boots to a console.** Buildroot, mainline U-Boot, the kernel with ROCKNIX's H700 patches, BusyBox. A shell on the screen if the panel comes up, and on USB serial (the USB-C port acting as a serial device) or the UART pins | The image builds; the same root file system boots in qemu's generic arm64 machine | Flash, power on, report what the screen shows; if nothing, a photo, or the UART (section 6) | Medium: first contact |
| **2** | **Apotheca as the only program.** Display (KMSDRM), buttons and stick, sound through ALSA, a newer SDL2 built by Buildroot, Apotheca built by Buildroot's toolchain (no more Ubuntu 20.04 sysroot). A tiny init (BusyBox init or our own script; no systemd) that starts Apotheca and starts it again if it stops. Read-only system partition plus a data partition | Init and restart logic in qemu; the program starts with the software renderer under qemu's virtual screen | Play a song, press every button, plug headphones, run SPEED TEST | Medium: sound and buttons bring-up |
| **3** | **GPU drawing.** Mesa with panfrost, SDL2's OpenGL ES 2 renderer; check every screen draws the same (our stills are the reference) | The stills drawn with Mesa's software GL (llvmpipe) match today's | Look at every tab; run SPEED TEST with the new present row | Low to medium: SDL's GL renderer differs from software in small ways (lines, blending) |
| **4** | **Power.** Battery reading, charging screen with the device off, clean shutdown at low battery, auto power-off when idle, suspend and resume on the power key | The shutdown logic with a made-up battery file | Leave it asleep overnight; let it run flat on purpose with a song open (songs must survive); charge it off | Medium: suspend on the H700 is still young in mainline |
| **5** | **WiFi and updates.** `rtw88` with Realtek's firmware, `wpa_supplicant`, network choice on Apotheca's WiFi page; the program update as today; **whole-system update into slot B** with U-Boot's boot counter falling back to slot A; the hotspot tried | A/B and fallback under qemu (a bad image must fall back by itself) | Join the home network; send an update; send a deliberately broken one and see it fall back | Medium |
| **6** | **Polish.** A boot splash (a still from Apotheca), boot time cut (no U-Boot wait, a trimmed kernel, Apotheca's first frame early), PREEMPT_RT tried against the plain kernel, core pinning and IRQ affinity, USB MIDI and USB audio in | Boot-time budget from the kernel's timestamps | Time the boot; play a long session with WiFi on; plug in a keyboard | Low |

After batch 2 the device is already Apotheca-only. Batches 3 to 6 are each
worth doing alone, in any order the owner prefers; 3 (GPU) and 4 (power) matter
most.

### Where it lives

A folder `os/` in this repository: a Buildroot "external tree" (our config,
kernel patches, device tree, init script, partition layout) and one script,
`scripts/build-image.sh`, which writes `build-image/apotheca-rg40xxv.img.gz`.
The port zip stays as it is for anyone on another firmware.

## 6. What the owner must provide or do

1. **Two spare microSD cards**, 8 GB or more, a known brand (one for ROCKNIX,
   one for our image). **Keep the stock card as it is, in a drawer.**
2. **A card reader** for the computer.
3. **A flashing tool**: balenaEtcher or Raspberry Pi Imager (both free; either
   writes an `.img.gz` to a card).
4. Batch 0, step by step:
   1. Take the ROCKNIX H700 image (the newest release; the DDR4 one).
   2. Flash it to a spare card. Before the first boot, copy the RG40XXV device
      tree from `device_trees/` to the card's top folder as `dtb.img`, as
      ROCKNIX's H700 guide says. If the screen shows garbage, hold power 10 s,
      flash again, try the other RG40XXV tree.
   3. Boot it once, let it finish, shut down.
   4. Copy the Apotheca port into its ports folder; start Apotheca from Ports.
   5. Run SPEED TEST. Send the results and both `device-info.log` files (this
      card's and the old card's).
5. **Maybe later: a USB serial adapter** (3.3 V, about 10 to 15 dollars, with
   jumper wires) and a small soldering job or pogo pins on the board's 4 UART
   holes. Only needed if batch 1 shows a black screen and USB serial does not
   come up. Opening the device means removing the back screws.

## 7. Questions for the owner

| # | Question | Recommended answer |
| --- | --- | --- |
| 1 | Which firmware is on your card now (stock, Knulli, muOS)? | Send `apotheca/device-info.log`; it says |
| 2 | Route b (our own image, ROCKNIX's kernel work), with ROCKNIX as the first measurement? | **Yes** |
| 3 | Should the data partition be readable by a computer when the card is taken out? exFAT (Mac and Windows read it) or ext4 (safer on a flat battery, but a Mac cannot read it; the WiFi page does everything) | **exFAT**, because recordings are sorted on a computer today; our own clean shutdown at low battery and `writeTextFile`'s fsync and rename keep songs safe. Revisit if the batch 4 tests lose a song |
| 4 | Keep a way to run other ports or emulators? | **No**: strictly Apotheca, as asked. The stock card is there for games |
| 5 | Will you open the device for the UART if needed? | **Only if batch 1 needs it**; try USB serial first |
| 6 | PREEMPT_RT from the start, or the plain kernel first? | **Plain first** (batches 1 to 5), RT in batch 6, kept only if SPEED TEST shows fewer late blocks |
| 7 | Whole-system updates over WiFi (A/B), or the program only, with system updates by card? | **A/B over WiFi** in batch 5; the card stays the rescue |
| 8 | Will the image ever be given to other people? | Say now: if yes, we keep `legal-info` bundles from the start (little work then, much later) |
| 9 | Overclock past 1.5 GHz? | **No** |
| 10 | Does this go before or after the other plans (ACID, granular, the RG505)? | **Batch 0 now** (an evening, and its data steers the speed plan too); the rest when the owner says |

## Sources

- Anbernic, RG 40XXV product page (SoC, RAM, screen, battery, WiFi, BT): https://anbernic.com/products/rg-40xxv
- ROCKNIX, RG40XX V device page (mainline kernel, panfrost, 1 GB LPDDR4, no eMMC, 1.4/1.5 GHz, fake suspend): https://rocknix.org/devices/anbernic/rg40xx-v/
- ROCKNIX, H700 installation guide (device tree per model, `dtb.img`, board revisions): https://rocknix.org/configure/h700-installation/
- ROCKNIX, H700 kernel patches (37 files: LCD, GPU OPP, joypad, OTG host, suspend, codec): https://github.com/ROCKNIX/distribution/tree/next/projects/ROCKNIX/devices/H700/patches/linux
- ROCKNIX repository and licence (GPL-2, a JELOS fork): https://github.com/ROCKNIX/distribution
- ROCKNIX release 20260701 (kernel 7.0.11, the H700 DDR4 image lists the RG40XX V): https://newreleases.io/project/github/ROCKNIX/distribution/release/20260701
- Knulli, RG40XX V page (BSP 4.9.170, no published kernel or U-Boot source, stock boot parts reused): https://knulli.org/devices/anbernic/rg40xx-v
- Knulli, H700 firmware extraction (boot0 at 8 KB, the stock boot chain): https://knulli.org/guides/h700-firmware-extract
- MustardOS (muOS) on GitHub (frontend GPL-3, no kernel repository shown): https://github.com/MustardOS
- Mainline Linux, Allwinner device tree list (H700 trees for the RG35XX family only; no RG40XXV tree): https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/Makefile
- Mainline Linux, RG35XX 2024 device tree (AXP717, codec, GPU, USB, UART): https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/sun50i-h700-anbernic-rg35xx-2024.dts
- Mainline U-Boot, H700 config (LPDDR4, AXP717): https://github.com/u-boot/u-boot/blob/master/configs/anbernic_rg35xx_h700_defconfig
- RG35XX device tree series (RTL8821CS on SDIO, BT on uart1): https://lore.kernel.org/all/2190536.Icojqenx9y@jernej-laptop/T/
- H616/H700 audio codec enabled in mainline: https://lists.infradead.org/pipermail/linux-arm-kernel/2024-October/972366.html
- RG35XX LCD output patch (July 2025): https://lists.infradead.org/pipermail/linux-arm-kernel/2025-July/1046500.html
- Kernel oops on H700 with panfrost frequency scaling (Nov 2025, ROCKNIX 6.17 kernel): https://lkml.iu.edu/2511.0/09912.html
- macromorgan, development info for the RG35XXSP (UART pinholes, device tree from the stock boot image): https://macromorgan.blogspot.com/2024/05/anbernic35xxspinfoextract.html
- PREEMPT_RT merged for Linux 6.12, arm64 included: https://phoronix.com/news/Linux-6.12-Does-Real-Time
- Apotheca's own: `app/main.cpp`, `app/realtime.hpp`, `app/battery.hpp`, `app/transfer.cpp`, `scripts/Apotheca.sh`, `scripts/build-handheld.sh`, `docs/plans/speed-plan.md`, commit 9ff2f8e (the owner's SPEED TEST on 0.573)
