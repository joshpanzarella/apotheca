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
| A board revision our device tree does not fit | Medium: Anbernic changed panels and RAM in later units | **Met and answered** (section 11): the owner's unit has the newer screen; the card starts with ROCKNIX's `rg40xx-v-v2-panel` tree and carries the other |
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
| **1** (built on the desk, 2026-11-17: section 8) | **A minimal image that boots to a console.** Buildroot, mainline U-Boot, the kernel with ROCKNIX's H700 patches, BusyBox. A shell on the screen if the panel comes up, and on USB serial (the USB-C port acting as a serial device) or the UART pins | The image builds; the same root file system boots in qemu's generic arm64 machine | Flash, power on, report what the screen shows; if nothing, a photo, or the UART (section 6) | Medium: first contact |
| **2** (built on the desk, 2026-11-17: section 9) | **Apotheca as the only program.** Display (KMSDRM), buttons and stick, sound through ALSA, a newer SDL2 built by Buildroot, Apotheca built by Buildroot's toolchain (no more Ubuntu 20.04 sysroot). A tiny init (BusyBox init or our own script; no systemd) that starts Apotheca and starts it again if it stops. Read-only system partition plus a data partition | Init and restart logic in qemu; the program starts with the software renderer under qemu's virtual screen | Play a song, press every button, plug headphones, run SPEED TEST | Medium: sound and buttons bring-up |
| **3** | **GPU drawing.** Mesa with panfrost, SDL2's OpenGL ES 2 renderer; check every screen draws the same (our stills are the reference) | The stills drawn with Mesa's software GL (llvmpipe) match today's | Look at every tab; run SPEED TEST with the new present row | Low to medium: SDL's GL renderer differs from software in small ways (lines, blending) |
| **4** (built on the desk, 2026-11-17: section 10) | **Power.** Battery reading, charging screen with the device off, clean shutdown at low battery, auto power-off when idle, suspend and resume on the power key | The shutdown logic with a made-up battery file | Leave it asleep overnight; let it run flat on purpose with a song open (songs must survive); charge it off | Medium: suspend on the H700 is still young in mainline |
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
| 8 | Will the image ever be given to other people? | **Answered (owner, 2026-11-17): for the owner alone for now, but others may use it as Apotheca nears 1.0.** So `make legal-info` is kept working from the start: every package and vendored file has its licence recorded (`os/board/rg40xxv/README.md`), sources stay fetchable at pinned versions with checked sums, no binary goes on the card without a recorded licence (the Realtek WiFi firmware's terms are recorded when batch 5 adds it). No public release packaging yet |
| 9 | Overclock past 1.5 GHz? | **No** |
| 10 | Does this go before or after the other plans (ACID, granular, the RG505)? | **Batch 0 now** (an evening, and its data steers the speed plan too); the rest when the owner says |

## 8. As built: batch 1 (2026-11-17)

**Built and tested on the desk only. Nobody has started it on a handheld.**
Batch 0 (ROCKNIX on a spare card) has not happened, so which RG40XXV device
tree the owner's unit needs is not known: the card carries both, and either
can be the one it starts with (`--dtb`, or a copy over `dtb.img`). **Since answered: the owner's unit has the
newer screen, and the card starts with that tree (section 11).**

### What was found

| Thing | Finding | Source |
| --- | --- | --- |
| ROCKNIX's H700 sources | Branch `next`, commit `ad9e48d1492f` (2026-11-16). Device folder `projects/ROCKNIX/devices/H700`: options, kernel config, 32 kernel patches (one shipped disabled: the AHUB sound driver), 15 device trees of its own, two U-Boot builds (DDR3, DDR4) | https://github.com/ROCKNIX/distribution/tree/ad9e48d1492f272db9c742d0cfa6d81769ceee22/projects/ROCKNIX/devices/H700 |
| Kernel | **Linux 7.2, plain** (`linux-7.2.tar.xz`, sha256 `f9fef3d1...02b3`), the same tarball for H700, AMD64 and the Snapdragon devices. ROCKNIX's `packages/linux/patches/7.2` folder holds only MSM (Snapdragon) fixes; from its `mainline` folder the H700 needs one (`pwm_set_period`, used by the joypad driver's rumble) | `projects/ROCKNIX/packages/linux/package.mk` |
| 7.2 is end of life | 7.2.15 (2026-11-01) is the last of 7.2. **ROCKNIX's 0003 (`sun8i_tcon_top`) does not apply to 7.2.15**: a stable fix rewrote the same lines. So we pin plain 7.2, as ROCKNIX does, and move when ROCKNIX moves (7.3 is stable; 6.18 is the long-term line, but the patches are kept against 7.x) | kernel.org `releases.json`, 2026-11-17 |
| Patches and Buildroot | Buildroot applies patches with no "fuzz" (no slack in the lines around a change); ROCKNIX's build allows it. Eight did not apply. They were refreshed against 7.2 and give the same tree to the byte (`os/board/rg40xxv/README.md`) | our build |
| RG40XXV device trees | **Mainline has none**, not even its master on 2026-11-17 (only the RG35XX 2024, H, Plus and SP). ROCKNIX has two: `sun50i-h700-anbernic-rg40xx-v.dts` (36 lines) and `...-rg40xx-v-v2-panel.dts` (14 lines), both on top of mainline's RG35XX Plus tree as ROCKNIX's patches change it. They differ only in the panel's name, so in its start-up sequence | ROCKNIX `devices/H700/linux/dts/allwinner/` |
| Screen | Parallel RGB from the SoC's TCON (patch 0155 reports it as DSI to user space), the panel set up over a 3-wire SPI by the **generic MIPI panel driver** (Hironori Kikuchi's v2 series, not in mainline: patch 0110), which reads the panel's start-up commands from a firmware file `panels/<compatible>.panel`. We build both RG40XXV files into the kernel, because the driver is built in and probes before the root file system is there. Backlight: PWM (0008 adds the H616 PWM controller, 0010 the backlight) | patches 0008, 0010, 0110, 0111, 0155; ROCKNIX `kernel-firmware/extra-firmware/panels` |
| Buttons and stick | **ROCKNIX's own out-of-tree driver** `rocknix-singleadc-joypad` (GPIO buttons, the stick through the ADC behind a 4-way analog mux, rumble on PWM 3), commit `ac1be0fa0c4e`, GPL-2.0+. The volume keys are mainline `gpio-keys`. Mainline's own RG35XX trees use `adc-joystick` instead; patch 0140 swaps it out | patch 0140; https://github.com/ROCKNIX/rocknix-joypad |
| Sound | The SoC's codec (`sun4i-codec`, H616, mainline) with ROCKNIX's 0163 (suspend, the amplifier's supply). HDMI sound: 0204. Nothing else | patches 0163, 0204 |
| Power chip | AXP717, mainline (regulators, ADC, battery, USB power); 0124 renames the battery, 0157 drops the power key press that woke it | patches 0124, 0157 |
| WiFi / BT | rtw88 `RTW88_8821CS` (SDIO) built in, with `rtw88/rtw8821c_fw.bin` and `rtl_bt/rtl8821cs_*.bin` built into the kernel (linux-firmware 20260622). **Confirmed for ROCKNIX's H700 build**, so the RG40XXV has the 8821CS as the plan guessed. Left out of batch 1 | ROCKNIX `linux.aarch64.conf`, `config/kernel-firmware.dat` |
| USB | dr_mode `otg` on the USB-C port (0151, 0152), so it can be a device (our serial console) or a host | patches 0151, 0152 |
| Serial | `serial0` is UART0 at 0x05000000, 115200; the RG40XXV tree also turns on UART5 (PH pins) | the device tree |
| SD slots | `mmc0` is TF1 (0x04020000): **the system card is `mmcblk0`**. 0159 keeps TF2 powered | the device tree |
| U-Boot | **ROCKNIX ships mainline U-Boot v2026.01** with two patches (an LPDDR3 PHY value; AXP717 "do not start on a plugged charger") and its own defconfigs: mainline's `anbernic_rg35xx_h700_defconfig` plus 7 lines (no boot delay, no EFI, the status LED on, the charger rule). We use mainline's defconfig and those 7 lines as a fragment | ROCKNIX `devices/H700/packages/u-boot-DDR4` |
| Memory type | **LPDDR4** for the RG40XX V (ROCKNIX lists it under its DDR4 image). ROCKNIX's updater tells the two apart by the DRAM regulator: 1.1 V is DDR4, 1.2 V DDR3. The LPDDR3 settings are kept as `uboot-lpddr3.fragment` (`--dram lpddr3`) | ROCKNIX `devices/H700/bootloader/update.sh` |
| Trusted Firmware-A | v2.15.0, platform `sun50i_h616`, no SCP firmware (`SCP=/dev/null`) | ROCKNIX `packages/tools/atf` |
| Boot chain | The H700's boot ROM reads the SPL (U-Boot's first stage, with an `eGON.BT0` header and checksum) from **8 KiB** into the card; the SPL sets up DRAM and the AXP717, loads Trusted Firmware (BL31) and U-Boot from the FIT image after it; U-Boot's standard boot finds `extlinux/extlinux.conf` on the bootable FAT partition and loads `Image` and the device tree (`FDT /dtb.img`, as ROCKNIX does) to `0x40080000` and `0x4FA00000`. No boot.scr | U-Boot `include/configs/sunxi-common.h`, `boot/pxe_utils.c`; ROCKNIX `bootloader/mkimage` |

### What was built

`scripts/build-image.sh` fetches Buildroot **2026.02.3** (the long-term
line; sha256 checked) and builds `os/configs/apotheca_rg40xxv_defconfig`
with Bootlin's prebuilt glibc toolchain (gcc 14, 2025.08-1): no compiler is
built. Every download is pinned and its sum checked; ours are in
`os/board/rg40xxv/patches/*/*.hash`.

| Part | What |
| --- | --- |
| Card layout (`genimage.cfg`, MBR, disk signature `0x41504f54`) | U-Boot at 8 KiB; p1 FAT32 64 MiB APOTHBOOT (Image, `dtb.img`, both trees in `device_trees/`, `extlinux/extlinux.conf`); p2 squashfs 256 MiB, LZ4, read-only (the root, `PARTUUID=41504f54-02`); p3 256 MiB empty (slot B for batch 5); p4 exFAT APOTHECA, made at 256 MiB and grown to the end of the card on the first boot |
| Kernel | ROCKNIX's H700 config with `linux.fragment` on top: no initramfs, the two panel files built in, the USB serial gadget (ACM), virtio-mmio (so the same kernel boots in qemu) |
| Init | BusyBox init, no systemd. `/etc/inittab`: mounts, `/etc/init.d/S*`, then a shell on tty1 (the screen), and through `getty -L` on ttyGS0 (USB serial), ttyS0 (UART) and ttyAMA0 (qemu); each starts again when it ends; a console whose port is missing sleeps instead of starting again and again. No syslog, klog or seedrng |
| Start-up scripts | `S02modules` loads the modules the hardware asks for (the joypad driver; there is no udev or mdev); `S05data` grows DATA on the first boot (label APOTHNEW: `sfdisk -N 4`, `partx -u`, `mkfs.exfat -L APOTHECA`), and otherwise only mounts it, **never formats one that will not mount**; `S10usbgadget` makes the USB-C port a serial device with a shell; `S90bootlog` writes `/data/apotheca/logs/boot-NNNN.txt` (and `boot-latest.txt`): the kernel's messages, the device tree, screen, input, sound, power and regulator state, again 20 s later; the last 20 are kept |
| `apotheca-keep` | The supervisor batch 2 runs Apotheca under: starts it again at once if it ran 10 s or more, else waits 1, 2, 4 ... 30 s; a hold file stops restarts; logs every start and stop; TERM stops both |
| On the card for bring-up | `evtest` (buttons), `amixer`, `aplay`, `speaker-test` |

| Batch 2, desk side | `os/package/apotheca` builds Apotheca from this repository with Buildroot's toolchain (CMake; `-DAPOTHECA_CHANGES` gives the version, the one change to `CMakeLists.txt`) against SDL2 2.32.10 with ALSA. It is on the card in `/usr/lib/apotheca` but **not started at boot**: see "Left for batch 2". `/usr/sbin/apotheca-start` runs it from `/data/apotheca` (first copy from the system, a newer system's copy wins, `apotheca.new` and the 3 s fallback exactly as `scripts/Apotheca.sh`) |

**The image** handed to the owner was built from commit `a97e0bc`:
`apotheca-rg40xxv.img.gz`, sha256 `3073cda688955a3705678cbcf00c5d4fd41c60cb6f04af1d743c512cb67c7eef`.

**Sizes**: the image is **24.0 MB** (612 MiB
unpacked; the zeros of slot B and DATA cost nothing packed). The kernel
`Image` is 29.1 MB (ROCKNIX's config builds nearly everything in), the root
file system 11.8 MB (with Apotheca and SDL2), U-Boot with its SPL 612 KiB.

**Build**: the first build on the desk (4 cores) took about an hour, most of
it host tools (Python, OpenSSL, GnuTLS for U-Boot) and the kernel (about 20
minutes); a change to a script or the overlay rebuilds the image in about
20 seconds. **Disk: 6.3 GB** for `build-image/` (2.4 GB of it the kernel's
build folder; `--tidy` deletes it and U-Boot's after a build).

### Tested on the desk

| Test | Result |
| --- | --- |
| Builds from the defconfig, every download's sum checked | yes (`scripts/build-image.sh`). The two git downloads (Trusted Firmware, the joypad driver) had no sums in Buildroot: theirs are recorded in `os/` now |
| **Reproducible** | The kernel, both device trees and U-Boot rebuilt from clean are identical to the bit to an earlier build; two image builds in a row give **the same `img.gz` to the bit**. (The first DATA partition, made by `mkfs.exfat` at build time, was not: it writes the clock in. So DATA is zeros in the image and made on the first boot; `mkfs.vfat --invariant` fixes the boot partition's label time) |
| `os/tools/check-image.sh` | **every check passes**: the MBR (signature, disk signature, four partitions, types, sizes, 1 MiB boundaries, no overlap), **the SPL at 8 KiB with a good `eGON.BT0` header and checksum**, U-Boot's FIT right after it (40 KiB in) holding BL31 and ending at 619 KiB, before p1 at 4 MiB; p1's files and `extlinux.conf`; p2 squashfs LZ4; p3 and p4 zeros. A flipped byte in the SPL and a wrong disk signature each make it fail |
| `os/tools/qemu-test.py` (the image's own kernel and root in `qemu-system-aarch64 -M virt`, 4 x Cortex-A53, the card as a virtio disk 512 MiB larger than the image) | **every check passes** (36): a shell on the serial port 8 to 9 s after power on (emulated CPU); the root squashfs read-only; DATA grown from 32 to 544 MiB, exFAT, APOTHECA, the boot log written to it; only our four start-up scripts, no daemon we did not ask for; ROCKNIX's joypad module loads into this kernel and is in `modules.alias`; the absent consoles (ttyS0, ttyGS0) asleep, not restarting; the serial shell, killed, back in 2.4 s with its port as its terminal; `apotheca-keep`: waits 1, 2, 4, 4 s for a program that keeps failing, none for one that ran long, nothing while held, stops the program and itself when told; **Apotheca 0.68x, built by Buildroot's toolchain, draws the FX screen to a BMP** with SDL's offscreen driver; `poweroff` ends cleanly; on the second boot DATA keeps a file written on the first and is not grown or made again; the log count goes on |
| The still against a desk build of the same source | 292,079 of 307,200 pixels identical, 15,019 within 2 levels of 255 (the lettering's anti-aliasing: gcc on arm64 joins a multiply and an add into one rounding), **80 to 134 further apart, a different set each run**: the demo is playing while the still is taken, and under emulation it is at another place. Not a fault of the image. The test allows 200 |
| `legal-info` | runs; every package has its licence and sources, the kernel and U-Boot patches included. **One gap**: Bootlin's toolchain is "unknown" (`os/board/rg40xxv/README.md` says what it holds) |

**qemu cannot test** the H700's boot ROM, U-Boot's SPL (DRAM, the AXP717),
the screen, the GPU, sound, the buttons, USB, WiFi or the battery: the qemu
test starts the kernel directly, with qemu's own disk and serial port.

### Only the handheld can tell

qemu has no H700. Everything below needs the owner's hands: **whether the
SPL starts at all** (DRAM settings, the boot ROM), U-Boot finding the card,
**the screen** (panel start-up, which device tree), the backlight, the
buttons and stick (the joypad driver), sound, the AXP717 (battery reading,
charging, the power key), USB serial, the second slot, WiFi, and how long
it takes to start.

### Left for batch 2

- **The display needs Mesa.** SDL2's driver for a system with no desktop
  (KMSDRM) needs Mesa's GBM and EGL and a GLES or GL renderer (Buildroot's
  `BR2_PACKAGE_SDL2_KMSDRM` depends on them; SDL2 has no plain framebuffer
  driver any more). So batch 2's screen brings in Mesa with **panfrost**,
  which is most of batch 3; with it, SDL2's GLES renderer is on hand, and
  the software renderer is a hint away (`SDL_RENDER_DRIVER=software`), so
  the two can be compared on the device. Batch 3 is then the check that
  every screen draws the same.
- **Start Apotheca from inittab** under `apotheca-keep` once the display
  works: `::respawn:/usr/sbin/apotheca-keep apotheca /usr/sbin/apotheca-start`
  with `SDL_VIDEODRIVER=kmsdrm SDL_AUDIODRIVER=alsa`, the shell on tty1
  moved to tty2. "Quit" from the help card then restarts it at once; what
  quit should mean on a dedicated device (power off?) is batch 4's.
- **Buttons**: the joypad module is on the card and loads by its device-tree
  name; what SDL sees (a joystick, its button numbers) is for the controls
  setup to learn, as on stock (`--handheld`).
- **Sound**: ALSA straight to the codec; the mixer's controls (speaker,
  headphones, volume) need setting once (`alsactl` state, or Apotheca's own),
  read from the boot log's "sound" section first.
- **The read-only root and Apotheca's files**: done (`/data/apotheca`).

### Questions only the owner can answer

| # | Question | Recommended answer |
| --- | --- | --- |
| 11 | Apotheca's own licence: legal-info marks its source "not to be given out" until one is chosen. Which? | **Answered (2026-11-17): the owner's, later.** Nothing is needed for the owner's own use |
| 12 | Batch 1 gives a root shell with no password on USB serial and the UART. Keep it in later batches for bring-up, or close it from batch 2? | **Answered (2026-11-17): keep it open until batch 5, then close it** |

## 9. As built: batch 2 (2026-11-17)

**Apotheca starts at boot, on the GPU, with the CPU as the fallback. Built
and tested on the desk only; nobody has started it on a handheld.**

### What was found

| Thing | Finding |
| --- | --- |
| **SDL2 on a system with no desktop needs Mesa** | SDL2's KMSDRM driver (the kernel's display API) needs GBM and EGL and an OpenGL ES (or GL) library: Buildroot's `BR2_PACKAGE_SDL2_KMSDRM` depends on them, and SDL2 has no plain-framebuffer driver any more. Even SDL's software renderer presents through a GLES texture there. So batch 2 brings in Mesa, and with it most of batch 3's GPU drawing |
| **Buildroot 2026.02 ties panfrost to LLVM and clang on the card** | Its `mesa3d` package makes the panfrost driver depend on target LLVM and its shader compiler select target OpenCL (target clang, libclc, SPIRV-LLVM-Translator): four LLVM-sized builds and over 100 MB on the card. Mesa itself needs none of that on the target: Buildroot already configures it with `-Dmesa-clc=system -Dprecomp-compiler=system`, so the shaders are compiled at build time by `host-mesa3d` (built with host LLVM and clang). `os/patches-to-buildroot/0001-...` drops the target side; the build script applies it to the Buildroot tree once |
| **No udev, and SDL copes** | SDL2 finds joysticks with no udev by watching `/dev/input` (inotify). Keyboards it does not look for, so the power key (`axp20x-pek`) and the volume keys (`gpio-keys`) are handed to it in `SDL_EVDEV_DEVICES` (`/usr/libexec/apotheca/run`) |
| **The joypad as SDL numbers it** | SDL numbers a joystick's buttons by their key codes from `BTN_JOYSTICK` (0x120) up. The RG40XXV's device tree gives the driver these codes, so the buttons come out B 0, A 1, X 3, Y 2 (**on the handheld the X button sends the tree's BTN_WEST and Y its BTN_NORTH**, the reverse of the tree's labels: the owner, 2026-11-17; it was X 2, Y 3 until then), L1 4, R1 5, L2 6, R2 7, SELECT 8, START 9, MENU (F) 10, L3 11, R3 12, UP 13, DOWN 14, LEFT 15, RIGHT 16, and the axes ABS_X 0, ABS_Y 1, ABS_RX 2, ABS_RY 3. That is `usr/share/apotheca/controls-rg40xxv.txt`. **Which ADC channel the RG40XXV's one stick is on is a guess** (X and Y, the left stick's codes); the device tree declares four |
| **The codec's mixer** | `sun4i-codec`'s H616 controls: DAC Playback Volume, Line Out Playback Volume and Switch, Line Out Source Playback Route, the mixers' DAC Playback Switch, and the card's Speaker pin switch. `/etc/init.d/S20sound` sets them to full (Apotheca's own VOLUME is on top), and the boot log lists every control the card has |

### What was built

| Part | What |
| --- | --- |
| Mesa 26.0.1 | panfrost (the Mali-G31), softpipe (drawing with the CPU, the fallback), GBM, EGL, GLES. No LLVM on the card |
| SDL2 2.32.10 | KMSDRM and GLES on, ALSA, and the offscreen driver (for the desk test's stills) |
| Start at boot | `/etc/inittab`: `apotheca-keep apotheca /usr/libexec/apotheca/run`. `run` sets SDL's driver, the sound card (`plughw` on the first card that is not HDMI, or `/data/apotheca/sound-card`) and the keys, then `apotheca-start` runs `/data/apotheca/apotheca --handheld` |
| The fallback | `apotheca-keep` tells the program how many of its starts in a row stopped within 10 s (`KEEP_FAILS`). From two, or with `/data/apotheca/gpu-off` on the card, `run` starts it with `LIBGL_ALWAYS_SOFTWARE=1` (Mesa's softpipe) and `SDL_RENDER_DRIVER=software`. A start that then runs long resets the count, so the GPU is tried again next time |
| The buttons | `controls-rg40xxv.txt` is copied to `/data/apotheca/controls.txt` when there is none: no button setup on the first start. Deleting it brings the setup back |
| Sound | `S20sound` sets the mixer at boot |
| Tools | `padprobe` (what SDL sees of the joysticks, in controls.txt's numbers: for the owner from the USB shell) and `fakepad` (a uinput stand-in for the joypad, for the desk test), `os/package/apotheca-tools` |
| Kernel | virtio-gpu and virtio-sound as modules, for the desk test (loaded only where qemu's devices are) |
| Boot log | adds every sound card's mixer controls, the joysticks SDL sees, and the ends of Apotheca's logs |

**The image** for the owner was built from commit `fdb3fc2`:
`apotheca-rg40xxv.img.gz`, **32.5 MB**, sha256
`2115cae3de1ccb5318c5acba44825cf99db9e30cf87986d0c13072d8c5414af8`. The
root file system is 22.1 MB (11.8 in batch 1: Mesa's `libgallium` is 19.9 MB
unpacked). **The build**: about three hours on the desk's four cores the
first time, nearly two of them host LLVM (45 min) and clang (50 min); after
that a change to a script is 20 seconds and to Apotheca under two minutes.
**Disk**: 13 GB at its peak, 7.0 GB after `--tidy` (which keeps LLVM's and
Mesa's "done" marks, so they are not made again).

### Tested on the desk

| Test | Result |
| --- | --- |
| Reproducible | **Two builds give the same `img.gz` to the bit** (Apotheca and the tools rebuilt from the commit in between) |
| `check-image.sh` | every check passes |
| `qemu-test.py` (now with qemu's virtio screen and sound card, modern virtio) | **every check passes (44)**, one gap said, below |
| Apotheca at boot | started by inittab under `apotheca-keep` 11 s after power on (emulated CPU), still running 20 s later; its log: `screen: opengles2, accelerated` (on qemu that is Mesa drawing with the CPU: there is no virgl, and Mesa fell back to `kms_swrast` by itself), `audio: 48000 Hz, 256 frames per buffer` through ALSA, `controls: as learned (controls.txt)` (no button setup) |
| The fallback | with `gpu-off` it starts again on SDL's software renderer (`screen: software`) and keeps running |
| The joypad | a uinput stand-in declaring what the RG40XXV's driver declares (name, ids, 17 keys, 4 axes) presses every button: **SDL numbers all 17 as `controls-rg40xxv.txt` says**, and the stick's right and down are axes 0 and 1 upwards |
| The still against the desk | Apotheca built for the card draws the FX screen within 2 levels of the desk's on all but 68 to 124 pixels (the demo is at another place under emulation) |
| **Gap: no picture of qemu's screen** | On qemu's virtio-gpu, with Mesa drawing through `kms_swrast`, SDL's KMSDRM mode set is refused (`Could not set videomode on CRTC`, `pageflip: -22` in apotheca.log) and the virtual screen stays blank while Apotheca runs. The handheld takes another path (sun4i-drm and panfrost through Mesa's `kmsro`), so this says nothing either way about the panel. **If it happens on the handheld too, Apotheca runs with a black screen and does not stop**, so the fallback does not start; the boot log has the line. Not chased further |

### Only the handheld can tell

**Whether panfrost draws Apotheca right and fast** (the desk has no Mali: qemu's
virtual screen has no GPU, so the desk only ever drew with the CPU),
whether the panel's mode and orientation are right under KMSDRM, **whether
sound comes out** (the codec, the speaker amplifier, the headphone jack),
**whether the buttons and the stick are where controls-rg40xxv.txt says**
(the stand-in declares what the device tree says; the real driver was not
run), the power and volume keys, and how long it takes from power to the
SONG screen.

### Left for batch 3 and later

- **Batch 3**: check every screen draws the same on panfrost as in the stills
  (GLES differs from SDL's software renderer in small ways), and time a
  present on the device (SPEED TEST's new row).
- **Batch 4**: a real power-off and low-battery shutdown; what "quit" means
  on a dedicated device (today `apotheca-keep` starts it again at once).
- The stick's ADC channel, if it is not X and Y.

## 10. As built: batch 4 (2026-11-17)

**Every way off now saves the song first, and quitting turns the handheld
off. Built and tested on the desk only; nobody has started it on a
handheld.** Suspend is in, off unless asked for, and **untried**. No
charging screen (left over, below).

### What was decided

| Thing | Decided | Why |
| --- | --- | --- |
| **What QUIT means** | **SEL + START saves the song and turns the handheld off.** The help card's START line says "turn off", the update notice "SEL + START turns off; then turn it on" | On a device that runs only Apotheca there is nothing to quit to. An update accepted over WiFi is then put in at the next power on, as before |
| **How the app tells the system** | **A file, not an exit code.** The system sets `APOTHECA_OFF_MARK` (`/run/apotheca/apotheca.off`); the app writes it AFTER `saveNow`, holding why (`quit`, `idle`). `apotheca-keep` deletes it before every start; finding it after a stop it runs `poweroff` and starts nothing. No mark (a crash, a kill, a TERM) is a restart, as before | An exit code cannot tell a quit from a TERM: SDL turns TERM into the same quit, and both end with 0. The port's launcher sets no variable, so **on stock firmware nothing changes** |
| **The power key, held 2 s** | **Off**: the backlight off at once (the sign it was heard), then `poweroff`, the song saved on the way | The safe one. It acts while the key is still down, well before the AXP717 cuts the power by itself (about 10 s on stock: **a guess** for this PMIC's setting) |
| **The power key, a tap** | **The screen off**: the backlight off (`bl_power` 4) and the joypad held (`EVIOCGRAB`), so nothing pressed in a pocket reaches Apotheca. The song plays on; the volume keys still work. Another tap: back | A real sleep, safe and testable. Suspend is young on the H700 in mainline (ROCKNIX's own page says "fake suspend"), so it is **opt-in**: `power-key sleep` in `power.txt` makes a tap write `mem` to `/sys/power/state` (s2idle on the H700). **Untried** |
| **Low battery** | **5%, or under 3.25 V, on the battery, on two reads 20 s apart: off** (the song saved). On a charger (a supply `online`, or the battery `Charging`, `Full` or `Not charging`) never. No warning on screen | Two reads, so one dip of the gauge does not do it. 3.25 V is a backstop under the gauge (**a guess**, not measured). The AXP717's own cut-off, later, saves nothing |
| **Idle** | **15 minutes with nothing to show: off** (the song saved). "Nothing to show" is the redraw's own idea (`redraw.hpp`): no button, the song stopped, nothing recording, bouncing, fitting or on WiFi, no MIDI | The app already knows, to the pass, whether anything is happening. A watcher outside would have turned off a song playing with nobody touching it |
| **Settings** | A text file, `apotheca/power.txt` on the card: `idle-off MINUTES`, `low-battery PERCENT`, `power-key screen|sleep` (0 for never). Read at each start (idle) and each battery read | Cheap. A SET page row would be an app change; left over |
| **Charging screen with the device off** | **Not built.** Plugged in while off, the AXP717 charges with the screen dark, as batch 1 set up (ROCKNIX's U-Boot patch: a start by the charger shuts straight back down) | Showing a picture means U-Boot starting Linux in a "charging" mode instead of shutting down, and a small program that draws and turns off at unplug or full. Not cheap |

### What was built

| Part | What |
| --- | --- |
| **App** (`app/power_off.hpp`, `main.cpp`, `app.hpp/.cpp`) | `PowerOff` reads the two variables (`APOTHECA_OFF_MARK`, `APOTHECA_IDLE_OFF_MINUTES`: whole minutes, 1 to 1440, else never; no mark, never). SEL + START, or the idle rule, ends the loop; after `saveNow` the mark is written. `App::setQuitTurnsOff` gives the help card and the update notice their words (no longer than the old ones, so the load line keeps its room). Not for `--smoke`, `--shot` or `--demo`. Test: `app_test` `testQuittingTurnsTheHandheldOff` (the rule, the clock going round, the mark, the words, the card as drawn changing on one line only) |
| `apotheca-power` (`os/package/apotheca-tools/src`, C, no SDL; `/usr/sbin`, inittab respawn) | Finds the power key and the joypads from `/sys/class/input/*/device/capabilities/key` (KEY_POWER; a gamepad button or the D-pad's) and opens them, and any that appear later (inotify on `/dev/input`). Times the key itself on its own clock; reads the battery every 20 s; logs to `logs/power.log` (opened for each line, so it never keeps the card busy; the important lines to the console too). Turns the backlight on when it starts, whatever an earlier run left |
| `S95apotheca` | **Every way off goes through `poweroff`**, so through rcK, and S95 is first there: holds restarts, TERMs `apotheca-keep`, which hands the TERM on through `apotheca-start` to Apotheca, which saves and ends (SDL makes TERM a quit). Waits up to 15 s. **Before this, a shell `poweroff` made the card read-only with Apotheca still running**: BusyBox init runs rcK before it TERMs everything, so the last save failed |
| `apotheca-keep` | the off mark (above), its pid file, the stopped program's code in the log |
| `apotheca-start` | runs Apotheca in the background and hands TERM on (it used to wait in the foreground, and TERM ended only the script) |
| `run` | sets `APOTHECA_OFF_MARK` and the idle minutes from `power.txt` (CRLF read too); no longer hands the power key to SDL (it was a keyboard to Apotheca, which ignored it) |
| Boot log | the backlight, `/sys/power/state` and `mem_sleep`, the end of `power.log` |
| Kernel | `CONFIG_TEST_POWER=m`: a made-up battery for the desk test, loaded by hand only, never on the H700 |
| `fakepad --serve FIFO` | the joypad and a stand-in `axp20x-pek` power key, driven by lines (`down`, `up`, `tap`, `wait`) |

**Found while reading the kernel**: mainline names the AXP717's battery
`axp20x-battery`, ROCKNIX's patch 0124 renames it `battery`; its status is
`Charging`, `Discharging` or `Not charging` (on the charger, not charging:
full). Apotheca's `battery.hpp` reads the first supply with a `capacity`,
so the help card's battery works under mainline; it counts `Not charging`
as not charging, which only matters for its red colour under 15%, where a
full battery never is. The power key is mainline's `axp20x-pek` (KEY_POWER on
press and release); ROCKNIX's 0157 drops the press that wakes it from suspend.

**The image** for the owner was built from commit `8c16ca3`:
`apotheca-rg40xxv.img.gz`, **32.5 MB** (32,529,471 bytes), sha256
`27471f07e4b4a3c9637aacd0b62f0a3fefccafc004eb702b6cb8c939ac92724c`. The root
file system is 22.1 MB, as in batch 2 (`apotheca-power` is 22 KB). The kernel
was built again (`CONFIG_TEST_POWER=m`): about 40 minutes on a desk shared
with another build. **Disk**: 7.0 GB after `--tidy`.

### Tested on the desk

| Test | Result |
| --- | --- |
| **Reproducible** | **Two builds give the same `img.gz` to the bit** (Apotheca and the tools rebuilt from the commit in between) |
| `check-image.sh` | **every check passes (62)**. New: with `unsquashfs` it reads the root: `apotheca-power`, `apotheca-keep`, `apotheca-start`, `S95apotheca`, `run` and `fakepad` there and executable; inittab starting `apotheca-power` and Apotheca, and running rcK; `run` handing over the off mark; `apotheca-keep` turning off on it; the Apotheca on the card knowing `APOTHECA_OFF_MARK`; `test_power.ko` there. Against the batch 2 image the seven new checks fail, as they should |
| `power-test.py` (new: `apotheca-power` built for the desk, a made-up `/sys`, input devices as FIFOs fed the kernel's records, "off" a command that writes a file; about a minute) | **every check passes (33)**: the power key and joypad found, the volume keys left alone, one found later, and with no `/dev/input` at the start the folder and its first device found when they appear; a tap: the backlight off and on; held 1.5 s nothing, 2.3 s off with the key down, the backlight off first, letting go after does nothing; 5% on a charger (USB online; Charging; Not charging) never, one low read never, two reads off; 30% at 3.20 V off; a voltage in millivolts not read; `low-battery 0` never, `10` at 8%, `99` refused; `power-key sleep` writes `mem`; TERM ends it. **Mutants**: two reads made one, `Not charging` not counted, the long press at 0 ms, the backlight not put back at the start, the voltage rule taken out, the watch on `/dev`'s parent taken out: each fails the test |
| `qemu-test.py` (now five boots, about 17 minutes) | **every check passes (76)**, the one gap of batch 2 said again (qemu's virtual screen stays blank). New: `apotheca-power` running from inittab; `apotheca-keep` runs its off command and starts nothing when the program leaves the mark, and a stale mark counts for nothing; **a short press of a stand-in power key holds a stand-in joypad** (`evtest --grab` is told it is grabbed and sees no press), another lets it go; **Apotheca quit with SEL + START on the stand-in joypad leaves the mark and the machine powers down** (`apotheca-keep`'s log on the next boot: code 0, quit; Apotheca's own log: "turning the handheld off (quit)"); **the power key held 2.6 s powers it down, S95apotheca stopping Apotheca first** ("Apotheca stopped (in 1.3 s), its song saved" on the console); **the kernel's made-up battery at 4%**: on a charger nothing in 50 s, unplugged one low read, a second, off, Apotheca stopped first; **`idle-off 1`**: Apotheca left alone powers the machine down after 80 s (a minute idle, and the start); a shell `poweroff` stops Apotheca first too |
| App (`ctest`, 71 tests) | **68 passed under `ctest -j2`; `effects2`, `idle1` and `idle2` timed out at 120 s** with the image build running beside them on the desk's four cores, and **each passed run by itself** (98, 82 and 118 s, still beside the build). `app_test` passes, `testQuittingTurnsTheHandheldOff` with it |

**Not shown by any test**: that the song is on the card after a turn-off.
The tests show the order (the mark is written after `saveNow`, and TERM
reaches `saveNow` through SDL's quit), not the file; a new song with nothing
in it is not saved at all, by design.

### Only the handheld can tell

Whether the power key reaches `axp20x-pek` on this unit (and nothing else
takes it), **whether `bl_power` turns the panel's backlight off** and back,
whether `poweroff` really cuts the power (the AXP717's power-off, never run
on a handheld, batch 1 and 2 included), how long the save at shutdown takes
on the real card, the AXP717's own long-press cut-off time (if it is under
about 4 s it could cut the power during the save), whether the gauge's 5% is
near empty and 3.25 V a fair backstop (a run down to flat with `power.log`
says), charging while off, and **suspend** (`power-key sleep`): whether it
sleeps, wakes on the key, and comes back with sound and screen.

### Left over

- **A charging screen** with the device off (above).
- **A warning on screen** before the low-battery turn-off (the app does not
  know the system will turn it off; it shows the battery only on the help
  card, red under 15%).
- `idle-off` and `low-battery` as rows of the SET page instead of a file.
- Suspend tried on the handheld; if it works, it may become what a tap does.

## 11. The owner's first start: the newer screen (2026-11-17)

**The owner's RG40XXV has the newer screen, and works with ROCKNIX's
`rg40xx-v-v2-panel` tree.** With the first tree (`rg40xx-v`) it showed only
coloured vertical stripes, though the boot log said everything else came up
(sound card, joypad, battery; the panel driver loaded `rg40xx-panel.panel`
and read the display ID as `000000`). With the v2 tree, after a longer wait,
text scrolled and Apotheca came up. **The card now starts with the v2 tree**
(`scripts/build-image.sh`'s and `post-image.sh`'s default); `rg40xx-v` stays
in `device_trees/` for units with the first screen, and `--dtb rg40xx-v`
builds a card that starts with it. `check-image.sh` now also checks that
`dtb.img` is the tree `dtb-in-use.txt` names.

**The stripes before Linux are normal on this unit, with either tree.** The
panel shows them from power-on until Linux's panel driver sends its
start-up commands (about 2.9 s into the kernel, a few seconds after the
power key). U-Boot has no driver for this SPI-set-up RGB panel, so it cannot
show anything earlier; holding the backlight off from U-Boot would only turn
the stripes black, and was not done. The owner's first try of the v2 tree
wrote no boot log because it was switched off during the stripes. os/README.md
and the boot partition's README.txt now say to wait 30 seconds.

**Evidence that later units have the newer screen** (no maker's statement):

- Knulli issue 455 (November 2025 on): several new RG40XXVs showing only
  coloured vertical lines with sound playing; the stock OS worked; muOS
  2508.4 worked.
- muOS commit `a55cdbe` ("Updated 40H/V device u-boot variants") replaced
  the RG40XX H and V stock-style `u-boot.fex` (one file now for both). Its new
  `u-boot.fex` holds **both** panels' start-up commands as code (the first:
  NV3052C-style page selects `FF 30`, `FF 52`, `FF 01`; the second starts
  `D7 61 74 97`, `D0 10`, `60..6F FF`: the same sequence as ROCKNIX's v2
  file), and code that reads **pin PE6** (H616 pin controller, port E data
  register `0x0300B0A0`, bit 6, after setting PE6's function) to pick
  `lcd_type`; it prints `--allen--PE_data[31:0] = 0x%x,lcd_type = %d`. The
  u-boot before that commit has neither sequence in that form nor the PE6 read. Its
  string table names one driver, `fog_fj035fhd05_v1`, for both.
- ROCKNIX added `anbernic,rg40xx-v2-panel.panel` and the two `-v2-panel`
  trees (H and V) on 2026-04-04 (commit `765c0e53f`, Philippe Simons); its
  `next` branch on 2026-11-17 has no newer RG40XX panel.

**A faint ghost of the stripes in dark pictures** (the owner, 2026-11-17,
with the v2 tree). Two causes are possible, and the owner's eyes tell them
apart:

1. **Image sticking** from the seconds the panel spent powered, lit and
   driven with no start-up commands (the stripes). An LCD held like that can
   keep a faint copy for a while. **Now cut short**: U-Boot's `PREBOOT`
   (`uboot.fragment`) turns the backlight off (PD28), holds the panel in
   reset (PI14) and turns its power off (PI15) as soon as U-Boot runs, which
   is the state Linux keeps the panel in until its driver powers it up and
   sends the start-up commands (about 3 s into the kernel). The boot ROM and
   the SPL run first, so a fraction of a second of stripes may remain; doing
   it in the SPL would need a U-Boot patch (not done). **Not tried on the
   handheld.** If it works the screen goes black within about a second of the
   power key and stays black until the text.
2. **The v2 start-up commands not quite right for this unit's panel** (its
   VCOM, gamma or inversion settings). The display ID reads `000000` (the bus
   cannot read), so nothing says this panel is exactly ROCKNIX's v2.

**What tells them apart**: a ghost that **fades over some minutes** of
Apotheca running is (1). **The owner's does not fade over 10 to 15 minutes**,
so it is (2) or the panel itself. What was then found:

- **The start-up commands are Anbernic's own, to the byte.** Decoded from the
  muOS `u-boot.fex` above (every call to its command and data writers on the
  `lcd_type == 1` path), the 98 commands are exactly ROCKNIX's v2 file, then
  `11`, 120 ms, `29`, 20 ms, which the driver sends itself. So VCOM, gamma
  and inversion are as Anbernic sets them.
- **What differs is the SoC's side, the TCON** (the display controller). The
  stock boot package's device tree has one `lcd0` node for both screens:
  `lcd_hv_clk_phase = 2` (Allwinner's TCON code makes that `clk_inv = 1`,
  the bit mainline sets for `DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE`),
  `lcd_hv_sync_polarity = 3` (both active high), 24 MHz, `lcd_ht` 768,
  `lcd_hbp` 46, `lcd_hspw` 20, `lcd_vt` 521, `lcd_vbp` 15, `lcd_vspw` 4,
  `lcd_frm 1` (dither to 18 bits). **ROCKNIX's v2 file drives the pixel
  data on the other clock edge** (its bus flags 0x46: `PIXDATA_DRIVE_POSEDGE`;
  its first-screen file has NEGEDGE, as stock) and uses 27 MHz, 750 x 600.
  Data latched on the wrong edge is the classic cause of faint column
  patterns, so this is the leading suspect, not a proven one.
- **A third tree to compare**, `rg40xx-v-v2-stock-panel` (not the default):
  ROCKNIX's v2 start-up commands unchanged, the clock edge NEGEDGE (bus flags
  `0x0a`: DE high, data driven on the falling edge) and the stock timings
  (24 MHz; horizontal 640 + 82 + 20 + 26 = 768; vertical 480 + 26 + 4 + 11 =
  521; sync both active high; 59.98 Hz). Written by
  `os/tools/make-v2-stock-panel.py` from ROCKNIX's file. The stock `lcd_frm`
  dithering is not copied: the panel takes 24 bits here (`3A 70`, the RGB888
  pins), so there is nothing to dither. **Not tried on the handheld.**
- **What the owner can tell us**: whether the stripes are gone, fainter or the
  same with the third tree; and whether **the stock card** shows them in a
  dark picture (then it is the panel, and nothing in software is wrong).

On the stock boot loader's choice: the code reads PE6 and sets
`lcd_type = !PE6`, and `lcd_type == 1` runs the v2 commands, so **PE6 low
means the newer screen** (read from the code, not tried).

**A way to tell the screens apart without asking the owner (not built):**
the stock boot loader reads PE6 (low: the newer screen, read from its
code). Our kernel or an early init script could read the same pin and pick
the panel file, so one card would fit both screens. It needs a unit of each
kind to try. The display ID cannot do it: the panel's
SPI is write-only here (`spi-gpio` with no MISO), so it reads `000000`.

### The faint vertical lines remain (owner, 2026-11-17)

**Neither the v2 tree nor `v2-stock-panel` takes them away**, so the clock
edge and the timings are not it. The owner believes they came with our
system (not checked on the stock card). Weighed against Anbernic's `lcd0`
node and pin groups (the muOS boot package):

| Suspect | Stock | Ours | Tried as |
| --- | --- | --- | --- |
| RGB pins' drive strength | `rgb24@0`: PD0 to PD27, `allwinner,drive = <3>` (40 mA) | no `drive-strength`: the reset value (20 mA). A weak edge at 24 to 27 MHz can garble low bits column by column | `try/1-pins-40ma` |
| Backlight PWM | `lcd_pwm_freq` 50000 (50 kHz), `lcd_pwm_pol 1` | `pwms = <&pwm 0 40000 0>`: 25 kHz. A beat would more likely show as horizontal bands; cheap to rule out | `try/2-backlight-50khz` |
| Dithering, bus depth | `lcd_frm = 1`: dithered to 18 bits on the 24 lines | 24 bits, no dithering (an SPI panel's bus format was never set). If the panel's lowest bits are noisy, dark greys show it most | `try/3-dither` (patch 0170 now reads an SPI panel file's pixel format: RGB666_PACKED gives MEDIA_BUS_FMT_RGB666_1X18, and sun4i's FRM is on) |
| All of them, and the stock edge and timings | | | `try/4-all-as-stock` |
| Start-up commands (VCOM, gamma, inversion) | the same as ROCKNIX's v2 file, to the byte (decoded above) | | not a suspect |
| `lcd_gamma_en`, `lcd_cmap_en` | 0, 0 | none | not a suspect |
| LCD supply and reset order | `lcd_power`, `lcd_pin_power` and five GPIOs in the stock driver's own order (not decoded) | regulator on, reset 5 ms, wait 20 ms, commands | not tried: a wrong order would rather fail outright |

Each is a whole `dtb.img` in its own folder under `device_trees/try/`, made
by `post-image.sh` from the v2 tree with `fdtput` and `panel-dtb.sh`. Tries 1
and 2 also work on the card before this one; 3 and 4 need this card's kernel.
`check-image.sh` checks all four; `qemu-test.py`'s panel boot uses try 4's
panel file (its CRC-32, the 24 MHz mode, the 18-bit bus format).

## 12. Changes without a new card (2026-11-17)

The owner: a new card image takes too long. Three things so most fixes are
one file:

| What | How | Desk test |
| --- | --- | --- |
| **A new Apotheca, one file** | `scripts/build-firmware-app.sh` builds the app alone with the image's toolchain (`build-image.sh -- apotheca-reconfigure`, so the version shown is the commit count; stripped) into `dist/firmware/apotheca`. Copied over `apotheca/apotheca` on the APOTHECA drive, `apotheca-start` finds it by its md5 differing from `program.md5` (the sum of what it last put there), checks it is an arm64 ELF (else `apotheca.failed`, the system's runs), and runs it as an update: `update-trying` set, the system's program as `apotheca.old`. **A new program, copied or from WiFi, has two tries** (the port's `Apotheca.sh` keeps one) to take `update-trying` away (the app does, 3 s in); then the one before comes back and `run` does not count those failed starts towards the CPU-drawing fallback (`/run/apotheca/apotheca.rolledback`). The WiFi update already landed where the program runs (`apotheca.new` beside it, `/data/apotheca`) and needed nothing | `qemu-test.py`, first boot: the system's program with bytes added runs and stays; BusyBox copied as `apotheca` has two tries then the system's is back, drawing with the GPU; a text file is never run. `check-image.sh`: the rule is in the image |
| **A screen change, one file** | **Patch 0170**: the panel driver uses the device tree's `apotheca,panel-firmware` (a whole panel file's bytes) when it is there, before `panels/<compatible>.panel`. A loose panel file on APOTHBOOT could not work: a built-in firmware file always wins over one on a file system (`firmware_request_builtin_buf` first, `drivers/base/firmware_loader/main.c`), and the panel probes about 2 s in, before any file system is mounted; making the driver a module loaded after a mount would delay the screen and risk it, untestable here. So the panel file travels in `dtb.img`. `os/tools/panel-dtb.sh` puts one in (fdtput, read back to the byte). The driver's log line now gives the panel data's size and CRC-32 | `qemu-test.py`, a sixth boot on qemu's own device tree with a simulated GPIO chip (`gpio-sim`, `CONFIG_GPIO_SIM=m`: loaded only where a tree asks for `gpio-simulator`), two bit-banged SPI buses and a panel on each: one takes the built-in v2 file (its CRC-32 and 27 MHz mode), the other the stock-timings file `panel-dtb.sh` put in its node (its CRC-32 and 24 MHz mode). `check-image.sh`: the kernel holds the three panel files byte for byte and the property's name |
| **Kernel and U-Boot rebuilds in minutes** | `build-image.sh --tidy` keeps their build folders (`--tidy-all` does not); it also clears host-spirv-tools' work files (1.7 GB). After every build, when the kernel's built mark is newer than the joypad module's, the module and the image are made again (a module from before a kernel rebuild is refused: the qemu test caught it once) | timed by hand: see below |

## 13. One card for both screens: the tree picked by PE6 (2026-11-17)

The owner has a second RG40XXV: with the v2 tree the backlight comes on and
the screen stays black, so it most likely has the first screen. Anbernic's
own boot loader tells the two apart by pin PE6 (section 11); now ours does.

**How** (uboot.fragment's `CONFIG_PREBOOT`, run as U-Boot starts, before it
reads extlinux.conf):

1. The backlight off (PD28), so nothing below shows.
2. `fdtfile` is `dtb.img` if the boot partition has one (`test -e mmc 0:1
   dtb.img`): **a tree copied there by hand always wins**.
3. The panel powered (PI15) and taken through the stock code's reset pulse
   (PI14 high 15 ms, low 50 ms, high 100 ms), then `gpio input PE6`: its exit
   status is the pin's level, so `if gpio input PE6` is "PE6 low", the newer
   screen. A failed read counts as high.
4. Without a `dtb.img`, `fdtfile` becomes
   `device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb` (low) or
   `device_trees/sun50i-h700-anbernic-rg40xx-v.dtb` (high).
5. The panel off (PI14, PI15), as before.

`extlinux.conf` says `FDTDIR /`, so U-Boot loads `/$fdtfile` (pxe_utils:
FDTDIR plus `$fdtfile`); one label, the kernel loaded once. The image is
made with no `dtb.img` (`--dtb auto`, the default; `dtb-in-use.txt` says
`auto`); `--dtb NAME` still puts one there. U-Boot says what it did on its
console (the UART, not the screen): "apotheca: PE6 says the v2 screen",
"apotheca: device tree ...". The boot log's "screen:" line reads PE6 again
from Linux (`devmem`, port E's data register) beside `rocknix-dt-id`.

**Tested on the desk**: `os/tools/uboot-pick-test.py` builds mainline U-Boot
v2026.01 for qemu with the card's own PREBOOT text (the disk `virtio 0:1`,
the gpio set and clear only echoed, `gpio input PE6` replaced by a file on
the boot partition standing for "low") and boots the card's own boot
partition three ways: no `dtb.img` and "low" loads the v2 tree, "high" the
first, a `dtb.img` wins. `check-image.sh` reads the PREBOOT in the image's
U-Boot and the extlinux.conf.

**Only the two handhelds can show**: that PE6 reads low on the newer screen
and high on the first (from the stock code, not seen), that the reset pulse
is what the pin needs (PE6 may be driven by the panel only once it is out of
reset), that `gpio input PE6` reads it on the H700 (U-Boot's sunxi gpio
names), and that the pulse shows nothing with the backlight off.

## 14. Plan: batch 5's WiFi (2026-11-17; the A/B system update is a later part)

**Why the WiFi page does not work on our card**: the boot log has rtw88's
8821CS probing and failing on `rtw88/rtw8821c_fw.bin`. ROCKNIX builds that
file into its kernel (`CONFIG_EXTRA_FIRMWARE`); our `linux.fragment` replaced
the list with the panel files, so it went. Nothing joins a network either: no
`wpa_supplicant`, no DHCP running.

| Part | Choice | Why |
| --- | --- | --- |
| Firmware | Buildroot's `linux-firmware` (pinned with Buildroot 2026.02.3, sum checked), `BR2_PACKAGE_LINUX_FIRMWARE_RTL_RTW88`: `/lib/firmware/rtw88/rtw8821c_fw.bin` on the root. Licence `LICENCE.rtlwifi_firmware.txt` (Realtek: redistributable, binary only), in legal-info by Buildroot and listed in `board/rg40xxv/README.md` | Recorded by Buildroot itself; no file vendored by hand |
| The driver | rtw88 (`RTW88`, `_SDIO`, `_8821C`, `_8821CS`) **as modules** in `linux.fragment`: S02modules loads them by modalias after the root is mounted, so the firmware is read from `/lib/firmware` (built in, the driver asks before any file system is there) | Also keeps the kernel smaller; the panel files stay built in |
| Joining | `wpa_supplicant` (nl80211, control socket, `wpa_cli`, WPA2 and WPA3-SAE), no D-Bus. iwd needs D-Bus: more to carry | Small, scriptable |
| Address | BusyBox `udhcpc` with Buildroot's `default.script` | Already on the card |
| The glue | `/usr/sbin/apotheca-wifi` (shell, root): `up`, `down`, `scan` (one line a network: signal, security, name), `join NAME` (password on stdin, never on a command line), `forget NAME`, `hotspot` (our own open network `apotheca`: `wpa_supplicant`'s AP mode on 2412 MHz, the handheld at 192.168.4.1, BusyBox `udhcpd` handing out .2-.20 with no way out and no names, so a phone keeps its own internet), `status` (`off`, `joining NAME`, `joined NAME ADDRESS`, `hotspot NAME ADDRESS`, `wrong-password NAME`, `not-found NAME`). Saved networks in `/data/apotheca/wifi/networks.conf` (`wpa_supplicant` format, `update_config=1`): on the APOTHECA drive, so a card moved to a new handheld keeps them, and a Mac can delete the file | One small program the app talks to; testable on its own |
| When it runs | **Only while the WIFI PAGE is on**: turning it on runs `apotheca-wifi up` (the radio, `wpa_supplicant`, `udhcpc`; it rejoins a saved network by itself), off runs `down` (the radio off). The app runs it on a thread of its own: no frame waits | Battery, and nothing running beside the sound when WiFi is not wanted |
| `run` | sets `APOTHECA_WIFI=/usr/sbin/apotheca-wifi`. On the port (stock firmware) it is not set and the app is as today | The same program for both |

**On the handheld (app side; the owner's button standards)**: the SET page
gets a **NETWORK** row under WIFI PAGE, shown only when `APOTHECA_WIFI` is
set:

- Its value is the network joined, or NONE; its note says JOINING..., WRONG
  PASSWORD, NOT FOUND, or the address (the WIFI PAGE row keeps its address).
- **Hold A + UP / DOWN** steps through the networks found (strongest first,
  a lock mark where one needs a password; scanned when the row is reached
  and again every 10 s while the page is on). Let go of A: join it. A saved
  network or an open one joins at once; one that needs a password opens
  **the keys a track's name is typed with** (Y deletes a letter, B or START
  keep it and join; B with nothing typed leaves). The keys need upper case,
  figures and the common signs, and room for 63 letters (a WPA password's
  most): the typing code gets those as a mode of its own, the track name's
  keys left as they are.
- **Hold B** (the bar that fills) forgets the network joined: "what cannot
  be undone says so".
- A **HOTSPOT** row under NETWORK (shown only where NETWORK is): **A** turns
  our own network on and off. On: the value is ON with 192.168.4.1 beside
  it, and a phone joins `apotheca` with one tap (the page still asks for
  its PIN). Off: `down` then `up`, so a saved network rejoins by itself.
  While it is on the NETWORK row names it, HOLD A finds nothing (the radio
  cannot look about), and HOLD B forgets nothing.
- The password is never written to a log, the boot log included
  (`networks.conf` holds it, as any computer's WiFi settings do).

**Tests**: the app side in `app_test`/`textsize` with a stand-in
`apotheca-wifi` (a script in the test folder that answers from a file): the
row, the list, the keys' new mode, the hints at 0 over, every text at XL. The
system side in `qemu-test.py` with `mac80211_hwsim` (two made-up radios;
`CONFIG_MAC80211_HWSIM=m`, never loaded on the H700) and `hostapd` on one as
the access point (WPA2 and an open one): `scan` lists them, `join` with the
right password gets an address, with the wrong one says so, `forget` and
`down`, `hotspot` (a made-up radio joins it as a phone would and gets an
address of 192.168.4), and the WiFi page answers over it. **Only the
handheld can show**:
the 8821CS loading its firmware and joining the owner's network, the
transfer rate, and whether the radio is heard in the sound.

## 15. BLACK: the first-screen unit that would not start (2026-11-17)

**Not solved: nobody knows yet why BLACK shows nothing with our card. The
next card is built to say where it stops, and takes out the likeliest cause.
Nothing of it has run on a handheld.**

The owner has two RG40XXVs. WHITE (the newer screen) starts with every one
of our cards. BLACK (the first screen):

| When | Card | BLACK did |
| --- | --- | --- |
| earlier in the day | an older card of ours (the v2 tree put there by hand) | **the backlight lit, no picture** |
| later | the card of section 13 (PE6 picks the tree) | nothing: no light, no backlight |
| later still | the older card again | nothing; on a charger its charging light came on |
| after that | **a stock Anbernic card** | **starts with no problem** |
| after that | our newest card again | **no light at all, not even for a moment; no backlight; no boot log written** |

### What the facts allow

**A lit backlight does not show that anything of ours ran.** The backlight is
on by the board itself from the moment the power chip brings the rails up:
on WHITE the stripes showed from power-on, seconds before Linux's driver
touched the backlight, and nothing in the SPL, Trusted Firmware or mainline
U-Boot drives PD28 (section 13's PREBOOT turned it off for that reason). So
the first row fits both "Linux ran with the wrong tree" and "the SPL hung".

**"Nothing, not even for a moment" is not a hang.** A start that hangs
anywhere (the memory set-up, U-Boot, the kernel) leaves the rails up, so the
backlight stays lit as in the first row. Dark from the first moment means
the rails never stayed up: the power was cut within a fraction of a second,
or never came on.

### The causes, ranked

| # | Cause | For | Against | Told apart by |
| --- | --- | --- | --- | --- |
| 1 | **Our SPL turns the power off at once: the "started by a charger" rule.** `CONFIG_AXP_DISABLE_BOOT_ON_POWERON` with ROCKNIX's U-Boot patch 0002: in the SPL, if the AXP717's power-on cause register (0x20) reads exactly "VBUS", VBUS is good and a battery is there, it writes the power-off bit. About 0.2 s after the key, before any light | The only code in the whole chain that cuts the power, and it gives exactly "nothing, not for a moment". BLACK went on a charger when a flat battery was suspected, and everything of ours has been dead on it since. The stock boot loader has no such rule. Both of our cards have it (it is from batch 1), which fits the older card dying too | ROCKNIX ships it, so a key press on a charger must work on its testers' units: for BLACK it needs the register to still read "VBUS" after a key press (a stale cause, or a power chip that starts again by the charger after our power-off, so every key press lands in a start "by VBUS"). **Not known whether the charger was in at each try** | **No new card needed**: charger out, hold the power key 15 s (the power chip's own cut), let go, wait, press it with our newest card. If it starts, this is it |
| 2 | **The battery was flat, and our cards were never tried on a charged BLACK without the charger** (the first row ran Linux behind a dark screen until it turned itself off or the owner did; a card that never shows a picture invites that) | Fits rows 2 and 3 | The last row was after the stock card ran, so the battery had charge, unless the charger was the only thing keeping it up, which is cause 1 again | The same test |
| 3 | **Something in section 13's PREBOOT or the first screen's tree** (what changed between the first row and the second) | The time order | U-Boot's PREBOOT only runs about a second in: the board's own backlight would have shown for that second, and U-Boot's status light (PI11) before it; the same commands run on WHITE; a failed command does not stop the list (`;`), and a tree is always chosen (desk test, PE6 "high" included). Whatever tree is loaded, Linux lights the green light a second in; the first screen's tree gave WHITE everything but the picture (section 11) | The new card's lights and `uboot-report.txt` |
| 4 | **BLACK's memory is another part** (LPDDR3, or an LPDDR4 our fixed timings do not train): the SPL stops in `sunxi_dram_init` | H700 handhelds exist with both kinds (ROCKNIX builds two U-Boots and tells them apart by the memory rail, 1.1 or 1.2 V); mainline's SPL has the kind and timings fixed at build time (`CONFIG_SUNXI_DRAM_H616_LPDDR4`, `CONFIG_DRAM_CLK=672`, the TPR values), only the size is found | It is a hang: the backlight would stay lit, which the last row does not have (the first row does). ROCKNIX, muOS and Knulli list the RG40XX V as LPDDR4 with no second kind; none of the three reports one | The new card: green light on, never the red, no `uboot-report.txt`. Then `scripts/build-image.sh --dram lpddr3`, and `stock-probe.sh` below reads the rail |
| 5 | The boot ROM does not take our card in BLACK (slot TF2, a card it reads badly, the SPL's place) | "No light" fits | The stock card starts from the same slot; the older card lit the backlight (which the stock boot ROM alone does too, so this is weak) | The new card: no green light either |

**Other chips inside?** Possible, and not to be seen from outside. What is
known to vary on this board family: the screen (two kinds, told by PE6),
and on other H700 models the memory. The WiFi chip, the power chip (AXP717)
and the SoC are the same in every report found. `os/tools/stock-probe.sh`
run on the stock system (below) writes down what BLACK has.

### What the new card does about it

| Part | Change |
| --- | --- |
| **Cause 1 taken out** | `CONFIG_AXP_DISABLE_BOOT_ON_POWERON` is off (`uboot.fragment`). ROCKNIX's patch 0002 is still applied and does nothing. **A charger plugged in while off now starts the handheld** (Apotheca turns itself off after 15 idle minutes); whether it then goes on charging off is not tried |
| **The lights say how far it got** | Green (PI12): lit by the SPL as its first act, before the PMIC and the memory (**U-Boot patch 0003**, `CONFIG_SPL_EARLY_LED_GPIO=268`, six lines in `board_init_f`). Red (PI11): U-Boot's status LED, lit when U-Boot itself runs (it was: ROCKNIX's setting, bit 267). Linux's LED driver puts the red out and **blinks the green** (`linux,default-trigger = "heartbeat"`, put into every tree by `post-image.sh`). `apotheca-power` makes it steady once a button is pressed |
| **U-Boot leaves a note on the card** | PREBOOT ends by writing `uboot-report.txt` to the boot partition (`env export -t`, `fatwrite`): `apotheca_pe6` (low / high), `apotheca_screen`, `apotheca_why` (PE6, a file's name, dtb.img), `fdtfile`, and `apotheca_starts`, a count read back from the last report (hexadecimal). A card taken out of a handheld that showed nothing says whether U-Boot ran |
| **Linux's log on the same partition** | `S90bootlog` copies the boot log to the boot partition as `linux-report.txt` (mounted for the copy only), with U-Boot's report in it, and writes it there even when DATA could not be made or mounted |
| **The backlight on from U-Boot** | After the PE6 read U-Boot turns the panel off and **the backlight on** (it turned it off until now): a lit, empty screen until Linux's driver takes it over. A start that stops after U-Boot is never dark |
| **The screen by hand, without a tree** | `screen-first.txt` or `screen-v2.txt` at the top of the boot partition (any content) overrides PE6; `dtb.img` still wins over both |
| **Never running behind a dark screen** | Nothing can see the picture (the panel's SPI is write-only), so the owner's hands say it. `apotheca-power`: a device tree counts as **seen working** once a joypad button is pressed while it runs (`/data/apotheca/screen-seen` keeps its name). Until then, **off after 300 s with no button** (`screen-check SECONDS` in `power.txt`; 0 never). **Off after 60 s when `/sys` has no backlight or no display.** Only where the tree names itself (`rocknix-dt-id`: not under qemu). The app's 15-minute idle rule stays behind it |

**What a start looks like now**, and where it stopped if it goes no further:

| Seen | Reached | If it stops here |
| --- | --- | --- |
| nothing, the screen dark | not even the SPL | causes 1 (should be gone), 2, 5: the battery, the slot, the card |
| **green light**, the screen as the board leaves it (lit) | the SPL | the PMIC or the memory set-up: cause 4. No `uboot-report.txt` |
| **green and red** (perhaps one orange light), the screen lit and empty | U-Boot; `uboot-report.txt` written, its count one up | the kernel did not start or stopped in its first second |
| **red out, green blinking**; dark a moment, text, Apotheca | Linux | a dark or garbage screen here is the wrong screen kind: `screen-first.txt` / `screen-v2.txt`. `linux-report.txt` has everything. Off by itself after 5 minutes with no button |
| green steady | a button was pressed with this screen | - |

### Tested on the desk

| Test | Result |
| --- | --- |
| `uboot-pick-test.py` (the card's PREBOOT in mainline U-Boot under qemu, the gpio commands echoed, the card's own boot partition) | **every check passes (24)**: five ways (PE6 low, PE6 high, `screen-first.txt` over a low pin, `screen-v2.txt` over a high pin, `dtb.img` over both): the right tree loaded and the kernel started each time; the green light's command first, the backlight off for the pulse and on after it; `uboot-report.txt` read back from the card after each start with the count 1 to 5, the pin, the tree and why; the FAT sound after the five writes (one wart, below) and `Image`, `extlinux.conf` and the trees byte for byte as made |
| `power-test.py` | **every check passes (50)**, 17 of them new: no check where the tree does not name itself; a tree not seen: off after `screen-check` seconds, nothing kept; the stick's readings and a button let go are not a press; a press keeps the tree's name and makes the light steady (trigger `none`, on); the next start is "seen before"; another tree is checked again; a press with the screen turned off by the power key does not count; `screen-check 0` never; no display, or no backlight, in `/sys`: off. **Mutants** (seven: the turn-off taken out, a let-go counted as a press, "or" made "and" in the no-screen rule, any old name counted as seen, the screen-off guard taken out, the stick counted, the trigger left blinking): each fails the test |
| `check-image.sh` | **every check passes (93)**. New: the PREBOOT in the image's U-Boot (the green light first, the backlight on after the read, the two override files inside the "by PE6" branch, the report written last); **the text "Power on by plug-in" is not in the image's U-Boot** (the charger rule is compiled out); every tree on the boot partition has the heartbeat; README.txt names the three files; `apotheca-power` and `S90bootlog` in the root hold the new rules |
| `qemu-test.py` | **every check passes (94)**, the one gap of batch 2 said again. New: on the first boot `linux-report.txt` is on the boot partition, the same bytes as `boot-latest.txt`. (qemu's tree does not name itself, so the screen rules do not run there: `power-test.py` has them.) **Not tested**: the log written to the boot partition alone when DATA is not there |
| The SPL's new lines | read in the built SPL's code: `gpio_direction_output(268, 1)` straight after `gpio_init()`, before `spl_init()`, the console, the I2C bus and `sunxi_board_init()` (the PMIC and the DRAM). **Not run**: qemu has no H700 |
| Reproducible | **the same `img.gz` to the bit** from two builds, U-Boot and the tools rebuilt in between |

**One wart**: U-Boot's FAT code does not keep the FSInfo sector's
"free clusters" note, so after its write `fsck.fat` says "Free cluster
summary wrong" (by one or two clusters) and nothing else; Linux, Windows and
mtools count for themselves. With it put right, `fsck.fat` finds nothing.

**The image** was built from commit `7d2cc6d`: `apotheca-rg40xxv.img.gz`,
32.7 MB (32,660,090 bytes), sha256
`ee7c3bdfe563dfeaf1708ff3ff62209f1b6d3ca3a46b57e54091a2e19aaf5735`.

### Only the handheld can tell

Everything in the table of lights: that PI12 is the green light and PI11
the red on the RG40XXV (from the RG35XX tree ROCKNIX builds on; WHITE's
boot log lists `green:power` and a red one), that the SPL's write lights
it, that U-Boot's `fatwrite` works on the H700's card reader (the desk used
qemu's disk; U-Boot's own "save the settings to FAT" uses the same code on
this board), what an unpowered panel with the backlight on looks like, and
that Linux's pwm-backlight driver takes PD28 over from a GPIO driven high.
**And the cause itself.**

### What is possible for a kernel that stops before its own programs

Not built, because both units run the same kernel and nothing points
there. If the lights ever say "U-Boot ran, Linux did not": the kernel's
`ramoops` (`CONFIG_PSTORE_RAM` is off in ROCKNIX's config; a kernel rebuild)
keeps its messages in a fixed piece of memory that survives a reset, though
not reliably a power cut, and U-Boot can write that piece to the boot
partition at the next start with the `fatwrite` already there. The other
way is the UART pads inside the case (115200), which needs the case open.

### The stock card can say what is inside BLACK

`os/tools/stock-probe.sh`: copied to the stock card's `Roms/APPS` folder
and run from the stock system's App Center > APPS, it writes
`stock-probe.txt` beside itself: the memory's rail (1.1 V LPDDR4, 1.2 V
LPDDR3) and size, the power chip's rails and readings, the screen's driver
and **pin PE6 as the stock kernel reads it**, the WiFi chip, the card's
`boot0` header, the kernel's messages. It changes nothing else. **Not
tried**: written from what is known of Allwinner's 4.9 system; whether that
menu runs a `.sh` put there is from other people's stock mods, not seen.
Run on WHITE too, the two files side by side say every difference.

### To undo later

The report writes (U-Boot's and Linux's, on the boot partition at every
start) are for this hunt: a power cut in the middle of one can leave the
FAT needing a check. Take them out, or write only on a start that failed,
once BLACK starts. Whether a charger should start the handheld is the
owner's call once cause 1 is proved or cleared.

## 16. BLACK, the second card: the first program goes round (2026-11-18)

**BLACK's fault is found, as far as lights can find one: the card's first
program (the SPL) cannot set BLACK's memory up, and tries again for ever.
The likeliest reason is that BLACK has the other kind of memory (LPDDR3).
A second card image for that kind is built. Nothing of it has run on a
handheld.**

### What the owner saw with the card of section 15 (image `ee7c3bdf`)

| Unit | Seen |
| --- | --- |
| WHITE | red at the start, then the green light blinking as Linux's text comes up, then green steady once Apotheca has loaded. Works. Its start wrote `uboot-report.txt` (`apotheca_starts=1`, PE6 low, the v2 tree) and `linux-report.txt` |
| BLACK | **the green light blinks about once a second, evenly, from the start. Never any red.** The same with the charger out. The screen fully dark. It never turns itself off. No APOTHECA drive. **No `uboot-report.txt`**: WHITE's `apotheca_starts=1` came after several starts in BLACK with the same card, so U-Boot never wrote on BLACK. `screen-first.txt` / `screen-v2.txt` change nothing |

### What that says

| Fact | So |
| --- | --- |
| On that card three things light the green light: the SPL (as its first act), U-Boot's PREBOOT, Linux. Only U-Boot itself lights the red one, before its PREBOOT | **No red, ever: U-Boot never ran on BLACK.** The green light is the SPL's |
| Linux's blink on that card is the heartbeat: two quick blinks and a pause. BLACK's is even | It is not Linux |
| The SPL lights the light once. For it to blink, the SPL must start again and again | **The board resets about once a second, in the SPL** |
| The H616 memory code in mainline U-Boot calls `panic()` when it cannot train the memory ("This DRAM setup is currently not supported", eight places in `dram_sun50i_h616.c`), and a panic in the SPL resets the board through the watchdog. A plain failure to find the size would `hang()`: a steady light | **The reset is most likely the memory set-up giving up** |
| Mainline's SPL has the memory's kind and timings fixed at build time (`CONFIG_SUNXI_DRAM_H616_LPDDR4`, `CONFIG_DRAM_CLK=672`, the ODT and TPR values); ours are mainline's `anbernic_rg35xx_h700_defconfig`, which is also ROCKNIX's LPDDR4 build. ROCKNIX builds a second U-Boot for H700 handhelds with LPDDR3 | **BLACK most likely has LPDDR3**, or an LPDDR4 part these timings do not suit. Nothing found names an RG40XX V with LPDDR3: this is from the lights, not from a report |
| The stock boot loader starts BLACK | Anbernic's own first stage carries settings for both, or finds the kind itself |

**Section 15 was wrong about its cause 1** (the charger rule). With the
earlier cards the SPL had no light, so the same going round showed as
"nothing at all". Section 15's cause 4 (the memory) was ranked low because a
hang leaves the backlight lit; this is not a hang but a reset every second,
and BLACK's screen stays dark through that (seen; why is not known). The charger rule stays out
(it is not the cause, and not needed to find it).

**What is not shown**: that it is the memory and not the power chip's
set-up just before it (`axp_init`, the three rails), which the same light
covers. A wrong reading there does not panic in the code, so the memory is
the better guess.

### The two cards

| Image | For | What differs |
| --- | --- | --- |
| `apotheca-rg40xxv.img.gz` | LPDDR4 (WHITE) | as before |
| **`apotheca-rg40xxv-lpddr3.img.gz`** (`scripts/build-image.sh --dram lpddr3`) | LPDDR3 (BLACK, if the guess is right) | U-Boot built with `uboot-lpddr3.fragment` (ROCKNIX's LPDDR3 settings for the H700 handhelds, and its one-line fix to the memory code, patch 0001, which only LPDDR3 uses); the memory's rail (`dcdc3`) set to 1.2 V by the SPL and **in every device tree** (they say 1.1 V, which Linux would set as it starts); `memory-kind.txt` on the boot partition says which card it is |

Each card goes round on the other kind of handheld (green alone, blinking):
no harm, it is what BLACK has done all along. **One card for both** needs
the SPL to try one kind and then the other; not built: it is a change to the
memory code that only the two handhelds can test, and comes after BLACK has
started once.

### The lights, from this card on

| Seen | Reached |
| --- | --- |
| **green alone, blinking evenly about once a second** | the SPL starts, cannot set the memory (or the power chip) up, resets: **the wrong card for this handheld's memory** |
| green and red blinking together | the memory is set up (the SPL lights the red one then: patch 0003, `CONFIG_SPL_DRAM_OK_LED_GPIO`); it resets after that, before U-Boot's own commands |
| **red alone** | U-Boot runs (its PREBOOT puts the green one out, then writes `uboot-started.txt` before anything else) |
| red out, **green in pairs** | Linux has started |
| red on again for a moment | Linux's first program looks for the card |
| red out, **green even, once a second**, then text and Apotheca | the system starts. (The same blink as the first row, but it comes after red, and the screen is lit) |
| green steady | a button was pressed with this screen |

### What else the new cards do (for a start that stops later)

| Part | What |
| --- | --- |
| **Linux's first program** (`os/board/rg40xxv/early/init.c`) | A 600 KB static program, the only thing in an initramfs built into the kernel (`CONFIG_INITRAMFS_SOURCE`; `external.mk` builds it into the kernel's tree before each kernel build). It runs before the root is there: red light on; looks four times a second for the root (`root=PARTUUID=41504f54-02`: partition 2 of the disk whose MBR signature that is) and for the boot partition (partition 1 of that disk, else any partition that mounts as FAT and holds `extlinux/extlinux.conf`); writes **`kernel-log.txt`** there at most every 2 s (its own notes, the disks Linux sees, the kernel's messages; written to a `.part` file and renamed, so it is whole or the last one). Then it mounts the root, puts the red light out, sets the green one to an even blink, and runs the root's `/sbin/init`. **The kernel no longer mounts the root itself** |
| When it cannot go on | No root after 40 s, a root that will not mount, or no `/sbin/init`: said in `kernel-log.txt` (when the card can be written), the green light flickers fast for 20 s, then **a reset**. With no disk at all the flicker starts 10 s in |
| **The kernel's deadline** (kernel patch `0171`, `drivers/misc/apotheca_deadline.c`) | A timer armed as the kernel starts (60 s; `apotheca_deadline.seconds=N`). If nobody has put it off it **panics**, and `panic=5` on the command line makes that a reset. The first program moves it to 150 s; `S00early` turns it off. This is what gets a kernel that waits for good in a driver to give up: without it BLACK would blink for ever again |
| **What the kernel said, kept in memory** | `CONFIG_PSTORE_RAM` and `CONFIG_PSTORE_CONSOLE`; every device tree names 1 MiB at `0x4d300000` for it (`post-image.sh`), the console's text in the last 512 KiB. A reset does not clear memory (a power cut does, within seconds) |
| **U-Boot saves it** (PREBOOT) | With the mark `DBGC` at `0x4d380000`: those 512 KiB written twice. **Straight onto the card** at sector 4096 (2 MiB in, the unused room before the boot partition: `mmc write`, no file system code in the way), and as **`kernel-from-memory.txt`** on the boot partition. `S90bootlog` of a start that works turns the bare copy into **`kernel-from-card.txt`** and clears it: so a card from BLACK, put in WHITE and started once, gives up BLACK's kernel log even if U-Boot's file write fails there again |
| **Three resets, then no more** | U-Boot counts resets with no start of the system between (a mark and a count in memory at `0x4d200000`, kept out of Linux's hands by the device tree; `S00early` clears it). At three it does not start a fourth time: it tells the power chip to turn off (register 0x27 over I2C, on either bus: **a guess at the bus, not tried**), and if still on blinks the red light evenly until the power key is held. So a handheld that cannot start goes round three times (about four minutes), not for good |
| **The system's first script** (`S00early`) | Turns the deadline off, clears U-Boot's count, mounts pstore, and writes **`linux-early.txt`** to the boot partition at once and every 5 s for 90 s or until the boot log is written: the scripts' own lines, what is running (a script that hangs is in that list), the kernel's messages |
| **Every driver named** | `initcall_debug loglevel=8 log_buf_len=1M` on the command line: the kernel says each driver it starts and when it is done, so a stop inside one names it last. **For this hunt only**: the start is a few seconds longer and the screen fills with text |
| U-Boot | `mmc dev 0; mmc rescan` before the first look at the card (a guess at why the report was missing; harmless) |
| `S90bootlog` | every mount of the boot partition with the same options (a second mount with other options is refused while `S00early` has it); the kernel's messages of the start before (pstore) in the boot log |

These were built while the blink was still taken for Linux's. They cost
nothing on a start that works and are kept: BLACK's first start with the
right memory may stop somewhere new.

### Tested on the desk

| Test | Result |
| --- | --- |
| `uboot-pick-test.py`, part one (qemu "virt", the card commands echoed) | **passes**: the five ways to pick the tree as before, now with the green light put out first and `uboot-started.txt` written before anything else (and the report saying that write went); a made-up piece of kernel text put in memory at `0x4d380000` is saved whole as `kernel-from-memory.txt` and the start goes on; a second reset in a row is counted and starts; a third does not start, tells the power chip to turn off and blinks the red light; the FAT sound after it all |
| `uboot-pick-test.py`, part two: **the card commands for real** on qemu's Orange Pi PC (Allwinner H3: mainline U-Boot v2026.01 built for it with the card's PREBOOT, the same sunxi card driver and FAT code as the handheld's U-Boot; needs `arm-linux-gnueabihf-gcc`) | **passes** (41 checks in the two parts): PREBOOT's first look at the card works with nothing having touched it before; `uboot-started.txt` and `uboot-report.txt` are on the boot partition, the second start counts on from the first; the 512 KiB from memory are in `kernel-from-memory.txt` **and on the card's bare sectors from 2 MiB, with the sectors either side untouched**; the boot partition's files and the root partition's start unchanged. So the commands that left no report on BLACK are sound: U-Boot simply never ran there |
| `qemu-test.py` | **every check passes (108)**, run on the image of the commit before the last (the same but for the commit's name in `/etc/apotheca-build`). New: the first program finds the root by the disk's signature, the deadline is off once the system runs, nothing of the initramfs is left mounted; `kernel-log.txt` and `linux-early.txt` on the boot partition; a root that is not there (the reason said, 20 s of waiting, a reset, the reason in `kernel-log.txt`); a root that will not mount; the kernel left waiting with no first program: the deadline's panic and a reset; `rdinit=/none`: the kernel mounts the root itself, as before; on qemu's tree with the two pieces of memory: ramoops in use, U-Boot's mark Linux's to clear, and **after a panic and a reset the next start finds what the kernel said, its last words included**. **One thing seen and not explained**: on an earlier build of the day that last start (the one after the reset) went silent after the first program handed over, 3 times in 12; 6 of 6 and the full run since. A cold start never did it |
| `check-image.sh` | **every check passes (102)** on both images |
| The SPL | read in the built SPL's code: the green light's pin (268) driven high after `gpio_init()`, the red one's (267) after `sunxi_board_init()` returns. The LPDDR3 SPL: its `.config` has `CONFIG_SUNXI_DRAM_H616_LPDDR3`, ROCKNIX's ODT and TPR values and `CONFIG_AXP_DCDC3_VOLT=1200`; every tree on its card has `dcdc3` at 1,200,000 uV (the LPDDR4 card's 1,100,000). **Neither run**: qemu has no H700 |
| Reproducible | the LPDDR4 image: **the same `img.gz` to the bit** from two builds, U-Boot (from clean) and the tools rebuilt between. The LPDDR3 image was built once |

**The images**: built from commit `0731b19`. `apotheca-rg40xxv.img.gz` (LPDDR4), 32,966,872 bytes, sha256 `f61f222ca8b228274b45ff702ab09c6686b9ce625a1e7563718345c106465ac7`. **`apotheca-rg40xxv-lpddr3.img.gz`** (LPDDR3), 32,966,907 bytes, sha256 `96997d23c7ac5ba4b62f2c3d1f97a04b397a502ad955596106900e788ec7f3f7`. The kernel grew 0.3 MB (the first program)

### Only the handheld can tell

- **Whether the LPDDR3 card starts BLACK at all**, and if it does, whether
  Linux is steady with the memory's rail at 1.2 V and ROCKNIX's timings.
- **Whether memory keeps the kernel's text through a reset on the H700**:
  the reset is the SoC's watchdog; the memory chips stay powered but are not
  refreshed until the SPL sets them up again, and the SPL's set-up writes
  test words into memory (at its start and at round offsets, not at
  `0x4d300000`: read from the code, not seen). Bits may flip; the text
  should still read.
- Whether U-Boot's `mmc write` and `fatwrite` work on the H700's card
  reader with a real card (the emulated board is another Allwinner chip).
- Whether `panic` really resets the H700 under mainline (through Trusted
  Firmware), and how long BLACK's kernel takes to reach the deadline.
- The I2C power-off after three resets.
- Everything about the lights' colours, as in section 15.
- **The cause.**

### If the card says nothing again

Left, in order of cost: the same card started once in WHITE
(`kernel-from-card.txt`); `stock-probe.sh` on the stock card in both units
(section 15); a serial console over the USB-C port from the kernel itself
(`CONFIG_USB_G_SERIAL` built in, `console=ttyGS0`: the owner's computer
would need a terminal program); the UART pads inside the case.

### To undo when BLACK starts

`initcall_debug loglevel=8 log_buf_len=1M` (back to `loglevel=7`); the
writes to the boot partition at every start (section 15's, `kernel-log.txt`,
`linux-early.txt`); perhaps the deadline's 60 s, once real start times are
known.

## 17. One card for both kinds of memory, and a quick start (2026-11-18)

**The LPDDR3 card of section 16 started BLACK** (the owner: "it worked",
then "boots and works perfect"). So BLACK has LPDDR3 memory and WHITE
LPDDR4, the same model, and nothing on the outside says which. The owner
asked for **one image for every RG40XXV**, and then for a start that does
not take a minute. This section is that image. **It has run on neither
handheld.**

### How the first program finds the memory

U-Boot's H616 memory code has the kind fixed when it is built, and panics
on a memory it cannot train (section 16: BLACK's even green blink).
Patch `0004` adds a third choice, `CONFIG_SUNXI_DRAM_H616_LPDDR4_LPDDR3`:
both kinds' timings and PHY tables are built in and the kind is found as it
starts. The rule is one small header with no hardware in it
(`os/board/rg40xxv/uboot/dram_kind.h`).

| Step | What | Why |
| --- | --- | --- |
| 1 | **LPDDR4 first, the rail (DCDC3) at 1.1 V** | 1.1 V is LPDDR4's own voltage and is under LPDDR3's 1.2 V. An LPDDR3 part is not harmed by too little; so the first try over-volts nothing |
| 2 | A try fails if the set-up refuses, **if a wait in it times out** (1 s; upstream panics there, this build gives up and goes on), or if the memory test fails | the wrong kind must be an answer, not a reset |
| 3 | **The memory test**: 64 places spread over the size found, each written with its own word, read back, written with the inverse, read back, then put back as they were | a set-up can say it passed on the wrong kind. Put back, so the kernel's last words (ramoops) survive |
| 4 | LPDDR4 failed: **the rail to 1.2 V, 5 ms, then LPDDR3** | an LPDDR4 part sees 1.2 V only after its own kind failed. 1.2 V is over LPDDR4's working range (1.06 to 1.17 V) and far under its limit (1.5 V) |
| 5 | **The mark**: one word in the clock's spare register 7 (`0x0700011c`), which a reset does not clear. TRYING kind before each try, GOOD kind after | a later start goes straight to the kind that worked. A try that never comes back (another panic in the memory code resets the board) leaves TRYING, and the next run starts with the other kind |
| 6 | Neither works: PREFER the other kind, panic (a reset) | it goes on turn about; the green light blinks alone |

Two ways to the second kind, on purpose: **in the same run** (steps 2 to
4), which needs nothing kept, and **through the mark** after a reset, which
needs nothing of the failed set-up to be recoverable. BLACK's blink of
about once a second fits the 1 s time-out, so the first way is the likely
one there; which it is, only BLACK can say.

**The trees**: `dcdc3` (vdd-dram) is 1.1 V fixed in ROCKNIX's trees. On
this card every tree allows 1.1 to 1.2 V, so Linux leaves the rail where
the first program set it (a one-kind LPDDR3 card has it fixed at 1.2 V).
No tree is picked by the memory: nothing else differs.

**The reports**: `uboot-report.txt` has `apotheca_dram` (lpddr4, lpddr3,
unknown) and `apotheca_dram_asked`; `linux-report.txt` has a "memory"
section (the mark, read again, and the rail's microvolts).
`memory-kind.txt` says which image the card is (both, lpddr4, lpddr3).

**The override, and what it cannot do**: `dram-lpddr3.txt` or
`dram-lpddr4.txt` on the boot partition makes U-Boot write PREFER that
kind into the mark, for the **next** start. U-Boot runs only after the
memory works, so the file cannot help a handheld that does not start. It
is for a handheld that starts on the wrong kind's settings (not expected).
The real fallback is a one-kind image: `scripts/build-image.sh --dram
lpddr4` and `--dram lpddr3` still build them (the script cleans U-Boot by
itself when the kind changes).

### The charger rule is back

`CONFIG_AXP_DISABLE_BOOT_ON_POWERON=y` again: plugged in while off, the
handheld charges and stays off, as on ROCKNIX. It was taken out in section
15 as a suspect for BLACK; it was not the cause (the memory was), and a
handheld that starts by itself in a bag on a charger is worse than one that
does not.

### The start made quick

**Where the time went** (WHITE's own report of 2026-11-17, an image
without the hunt's settings):

| Stage | Then | Why | Now (estimated, not measured) |
| --- | --- | --- | --- |
| Kernel start to the first program | 3.53 s | **about 2.7 s of it printing 30,700 characters on the serial line at 115,200 baud** (counted from the log) | about 1 s: `loglevel=5` prints warnings and errors (above quiet, so the boot logo draws) |
| The screen showing anything | 3.18 s (the text console) | the same printing | about 1 s; then APOTHECA STARTING |
| First program to Apotheca started | 3.9 s (to 7.42 s) | 1.8 s the boot report, written before Apotheca; a card write in the first script; the USB serial set-up; 0.5 s making DATA (first start only) | about 1.5 s (2 s the first start) |
| **Linux in all** | **7.4 s** | | **about 3 s** |

BLACK's "a minute", and its five seconds of green blinking before any
text, were the hunt: `initcall_debug loglevel=8` names every driver twice,
on the serial line and on the screen. **Off now.**

What changed:

- `extlinux.conf`: `loglevel=5` (the boot log still has every
  message; above quiet, so the boot logo draws). `initcall_debug`,
  `loglevel=8`, `log_buf_len=1M` out.
- **APOTHECA STARTING** is written to the screen by the kernel's first
  program, its first act. It shows as soon as the display is up.
- The first program writes **no `kernel-log.txt` and mounts no boot
  partition when the root is there at once**; from 2 s of waiting on, and
  on every failure, it writes as before.
- `S00early`'s write, the USB serial console (`S10usbgadget`) and the whole
  boot report (`S90bootlog`) run **in the background**. Apotheca starts
  without them. Modules, DATA and sound stay in order before it.
- U-Boot: `mmc dev 0; mmc rescan` out of PREBOOT (it was for the hunt).
  `bootdelay` was 0 already. The panel's reset pulse (0.165 s) stays: PE6
  is read after it.

**Not cut**: U-Boot reading the 29 MB kernel from the card (a second or
two, a guess; a compressed kernel would trade reading for unpacking, not
tried), U-Boot's two small report files, the heartbeat and the lights.
**No clock is read before Linux**, so the time from the power key to the
kernel is not in any report: a stopwatch on the handheld is the measure.

### What the owner sees

| | WHITE (LPDDR4) | BLACK (LPDDR3) |
| --- | --- | --- |
| The lights at the start | green, then green and red within a second | green alone for a second or two longer (the LPDDR4 try, then LPDDR3); it may go out and back once. Then green and red. **After the first start, as WHITE if the clock keeps the mark** |
| Then | red alone, the screen lit and empty (U-Boot); green in pairs (Linux); APOTHECA STARTING; green even; Apotheca | the same |
| `uboot-report.txt` | `apotheca_dram=lpddr4` | `apotheca_dram=lpddr3` |

**On neither**: green alone, blinking, for more than ten seconds. That
means neither kind worked; the one-kind image is next.

### Desk tests

| Test | Result |
| --- | --- |
| `os/tools/dram-kind-test.sh` (new) | the rule; the patch's copy of the header equal to ours; **the SPL's own loop, cut out of the patch and compiled against a made-up handheld** of each kind whose wrong-kind set-up refuses, never comes back, lies, aliases or times out, from nine marks, with 0 to 2 set-ups of the right kind lost: always the right kind, the right rail, LPDDR4 settings never at 1.2 V, one run at the next start, the memory left as found. **24 mutants, all die** |
| `uboot-pick-test.py` | **every check passes (54)**: the mark read into the report as lpddr4, lpddr3 or unknown (the mark stood in for by a word of ordinary memory: the clock's register is not in the emulator), `dram-lpddr3.txt` and `dram-lpddr4.txt` writing PREFER, the five tree picks, the reports, the memory dump, the resets; the card commands for real on an emulated Allwinner board. The PREBOOT broken on purpose (the two kinds swapped) fails three checks |
| `check-image.sh` | **every check passes (108)** on all three images: the SPL of the kind `memory-kind.txt` says, the rail's range in every tree, the `loglevel=5` kernel (above quiet, so the boot logo draws), the charger rule in, the README's words |
| `qemu-test.py` | **every check passes (110)**. It found two faults of the background work on the way, both mended: `/data/apotheca/logs` was made by the boot report (now by `S05data`: `apotheca-power` and the keeper log from their first moment), and the report read Apotheca's log after Apotheca had begun it anew (the start now keeps the last 40 lines first). First boot to a shell 11.5 s, second 7.9 s (emulated CPU; 15 to 22 and 15 to 17 before). APOTHECA STARTING read off the text console |
| `power-test.py` | **every check passes (50)** |
| The SPL | 38.6 KiB of 49 allowed. Read in the build: both timing functions and both PHY tables in; `mctl_gave_up` in `.data` (**the SPL's `.bss` is in the memory being set up**, at `0x4ff80000`, so nothing there may be used before it works) |
| Reproducible | **the same `img.gz` to the bit** from two builds with U-Boot built from clean three times between (the LPDDR4-only and LPDDR3-only images were built in between). All tests above ran on those bits |

**The image**: `build-image/apotheca-rg40xxv.img.gz`, built from commit `cc8636f`, 32,967,705 bytes, sha256 `20676e8576f866daf955ddea0b217be24c84e96eef0608b2aa4a60e658e54492`. The fallbacks, built from the same tree and **not run** (they differ from section 16's: the quick start and the charger rule are in them): `apotheca-rg40xxv-lpddr4.img.gz` `76466298cb00099de282cfa4f9edeff73fe775aff04dd7caea39ed136a755f6a`, `apotheca-rg40xxv-lpddr3.img.gz` `9a396a40bf63fd54576d3077fba8a7976cbff632598a24518e39ed83aa48761f`. The LPDDR3 image BLACK started with is section 16's `96997d23...`, built from commit `0731b19`.

### Only the handhelds can tell

- **Whether this image starts either unit.** Both kinds' code is ROCKNIX's
  settings, each proven alone (section 16); together in one SPL they have
  not run.
- **BLACK: whether a failed LPDDR4 try leaves the controller able to set
  up LPDDR3 in the same run.** Each try begins by resetting the controller,
  as a cold start does; nobody has seen it.
- **BLACK: whether the clock's register keeps the mark** through a reset
  (believed: Allwinner's own loader keeps flags there) and through power
  off (it should while the battery has charge). If not, BLACK takes the
  second or two longer at every start.
- **WHITE: that LPDDR4 passes first time**, so 1.2 V is never applied.
  `apotheca_dram=lpddr4` and vdd-dram 1,100,000 in `linux-report.txt` say
  so.
- Whether the memory test passes on good memory of every size (written
  for 1 GiB; the places scale with the size found).
- The charger rule: plugged in while off, does it stay off, on both units.
- **Every time in this section.** The estimates come from WHITE's log and
  a count of characters.
- That APOTHECA STARTING shows, and for how long the screen is lit and
  empty before it.

## 18. Headphones or speaker, one or the other (2026-11-18)

**The fault** (the owner, on BLACK): sound from the speaker and the
headphones at once. **It is not BLACK's alone**: nothing in our system ever
turned the speaker off, so WHITE does the same. The fix below is built and
passes the desk; **no handheld has run it.**

### How the handheld is made

| Part | What | Where it is said |
| --- | --- | --- |
| The sound chip | the H616's own codec (`allwinner,sun50i-h616-codec`, driver `sun4i-codec`). **One analog output, LINEOUT** | the kernel's driver |
| The speaker | an amplifier fed from LINEOUT, switched by **PI5** (`allwinner,pa-gpios`), powered from `cldo1` | the tree, `&codec` in `sun50i-h700-anbernic-rg35xx-2024.dts` |
| The headphone socket | fed from the same LINEOUT. A contact says "something is in": **PI3** (`hp-det-gpios`, high = in) | the same |
| The two screens' trees | the v2 tree and the stock-panel tree `#include` the first screen's, and none of them touches `&codec`: **the same sound nodes in every tree we ship**. `check-image.sh` now holds that | `os/board/rg40xxv/dts` |

The kernel makes a jack for PI3 ("Headphone Jack": a control on the card,
and an input device with the switch SW_HEADPHONE_INSERT). Upstream it
switches the "Headphone" pin only. The amplifier's pin, "Speaker", is left
to userland: alsa-ucm-conf's `sun4i-h616` profile turns `Speaker Switch`
off when the jack says in, and ROCKNIX, with PipeWire, uses that. We run no
sound server, and `S20sound` set `Speaker Switch on` at every start. So
both played.

### The fix, in two layers

| Layer | What | Why |
| --- | --- | --- |
| **The kernel** (patch `0172`) | on an H616 codec with `hp-det-gpios` and an amplifier GPIO the jack has a second pin: **"Speaker", inverted**. In: DAPM powers the Speaker widget down, which takes PI5 low (then its supply, patch 0163's order). Out: back on | it switches inside the kernel's own power sequence, the same one that turns the amplifier on for a first sound, so a click is the driver's, no new one. It is right from the moment the card is made: the jack's first reading sets it |
| **`apotheca-power`** (the belt) | it already reads the input devices. It now finds the one with SW_HEADPHONE_INSERT, **asks its state** when found (EVIOCGSW), follows its events, reads it again at every battery read, and runs `/usr/libexec/apotheca/speaker on|off` (`amixer ... "Speaker Switch"`) when the speaker should change | if the kernel's pin ever fails to follow, or a later kernel drops the patch, it still switches. And it carries the rule (next row) |
| `power.txt`: `headphones auto` / `ignore` / `inverted` | auto is the usual. `ignore`: both play, as before. `inverted`: the socket's line reads the other way | **PI3's sense (high = in) is ROCKNIX's for the RG35XX family; nobody has checked it on an RG40XXV for us.** If it is wrong, the speaker is silent with nothing plugged in: this line mends that without a new card |
| `S20sound` | no longer sets `Speaker Switch` | it undid the kernel's reading at every start with headphones in |

**The app's volume is untouched**: only the speaker's switch moves.
Nothing in `app/` changed.

**Not done**: no fade around the switch (the driver's own 700 ms wait
after the amplifier comes on is all there is); nothing for HDMI sound.

### The report

`linux-report.txt` has a section "headphones and speaker": for each card
`Headphone Jack` and `Speaker Switch` as the mixer reads them, every input
device that has switches (the socket's is the one with bit 4), the two pins
read straight from the pin controller (PI3, PI5), and `apotheca-power`'s
lines about the socket. The start-up log says what the kernel had set when
`S20sound` ran.

### Onto a card that already works, without flashing it

**There is no whole way.** The system (the watcher, the scripts) is a
read-only partition; nothing on the two drives a computer sees can replace
it, and it should not be otherwise. Two honest options:

| Way | What it gives | What it costs |
| --- | --- | --- |
| **Copy the new `Image` over the one on APOTHBOOT** (keep the old one as `Image-old`). The kernel is a file there, and the patch is in it | plugging and unplugging switch the speaker. **But started with headphones already in, the speaker plays until they are pulled and put back** (the old `S20sound` turns it on), and there is no `headphones inverted` rule | nothing is erased. If the speaker is then silent with no headphones, put `Image-old` back |
| Flash the new image | all of it | **it empties the APOTHECA drive.** Copy the `apotheca` folder to the computer first, flash, start once, copy it back |

A `dtb.img` cannot do it: the trees are right already, and a `dtb.img`
would fix the screen's kind and defeat the pick by PE6.

### Desk tests

| Test | Result |
| --- | --- |
| `power-test.py` | every check passed, the headphone part included (in, out, the same twice, another switch, the screen off, the three rules, a switch that fails, no socket). **One more since the first run: `/dev/input` taken away and made again** (next row); it fails on the watcher as it was |
| `qemu-test.py` | every check passed (a stand-in socket made by `fakepad`: found, followed in and out, asked its state at a restart). **The first runs failed, and it was the watcher's fault**: when the last input device goes, the kernel takes `/dev/input` away and the watch on it with it, and the watcher kept thinking it watched. A device made after (a stand-in joypad made again, so the socket after it) was only found at the 20 s read, after the test had looked. The watch is now set again every time the watcher wakes for a new device (`inotify_add_watch` on a watch it has is harmless). On the handheld `/dev/input` is never empty, so it would not have shown there |
| `check-image.sh` | passed |
| `uboot-pick-test.py`, `dram-kind-test.sh` | both passed |
| The kernel patch | compiled into the image's kernel. **Not run: qemu has no H616 codec.** Read against `snd_soc_jack_report()`: every report sets each pin from the jack's status (the inverted one the other way) and syncs DAPM |
| Reproducible | **not checked for this image** (it was for the images of sections 16 and 17; nothing here writes a date or a random number) |

**The image**: `apotheca-rg40xxv.img.gz`, sha256 `04b58c8fbd8e47b71588228d97d33ff62b680cac2a7a92691c1f2ab140dbed39`, one image for both memory kinds (section 17). **Not run on a handheld.**

### Only the handhelds can tell

- **That it works at all**: in, the speaker off; out, on; while sound plays.
- **PI3's sense on the RG40XXV**, on both units (the report's "pins" line
  with and without headphones says it).
- Whether a click is heard at the switch, and how it compares with stock.
- Whether the amplifier's 700 ms is noticed when headphones come out.
- Started with headphones in: silent speaker from the first sound.

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
- ROCKNIX at the commit taken for batch 1 (`ad9e48d1492f`, 2026-11-16): https://github.com/ROCKNIX/distribution/tree/ad9e48d1492f272db9c742d0cfa6d81769ceee22
- ROCKNIX's joypad driver (commit `ac1be0fa0c4e`): https://github.com/ROCKNIX/rocknix-joypad
- Mainline Linux master's Allwinner device trees, read 2026-11-17 (no RG40XXV): https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/Makefile
- kernel.org releases (7.2.15 end of life, 7.3.4 stable, 6.18 long-term): https://www.kernel.org/releases.json
- Buildroot 2026.02.3 (long-term line): https://buildroot.org/downloads/
- Bootlin toolchains: https://toolchains.bootlin.com
- Mainline U-Boot v2026.01's H700 defconfig: https://github.com/u-boot/u-boot/blob/v2026.01/configs/anbernic_rg35xx_h700_defconfig
- Apotheca's own: `app/main.cpp`, `app/realtime.hpp`, `app/battery.hpp`, `app/transfer.cpp`, `scripts/Apotheca.sh`, `scripts/build-handheld.sh`, `docs/plans/speed-plan.md`, commit 9ff2f8e (the owner's SPEED TEST on 0.573)
- Knulli issue 455, new RG40XXVs show only vertical lines: https://github.com/knulli-cfw/distribution/issues/455
- muOS commit a55cdbe, "Updated 40H/V device u-boot variants" (binaries; read with `strings` and a Thumb disassembler): https://github.com/MustardOS/internal/commit/a55cdbe8ed13472d9b2d54c3ff74e47b9a6c454c
- ROCKNIX, "h700: add support for rg40xx v2 panel": https://github.com/ROCKNIX/distribution/commit/765c0e53ffa946b3ca7ef0a0d8460dc6ac49a9f7
