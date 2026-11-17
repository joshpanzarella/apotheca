# Apotheca's own system for the RG40XXV

This folder builds an SD card that starts the handheld on **our own Linux
system** instead of the stock firmware. The plan and its reasons:
`docs/plans/firmware-plan.md`.

**Batch 2 starts Apotheca by itself.** Power on, a few seconds of text,
then Apotheca's SONG screen: no firmware menu, no Ports. The buttons are
set up already. Songs, recordings and settings live on the card's APOTHECA
drive, in its `apotheca` folder.

**Batch 4 adds power**: turning it off saves the song first, the power key
turns the screen off, and a low battery turns it off before it goes flat.
See "Turning it off" below.

**Nobody has started it on a handheld yet.** Only the desk tests below have
run: the screen, the sound, the buttons and the GPU are the handheld's to
show. The card writes down everything it finds (the boot log, below), so
whatever happens can be sent back.

## The device cannot be bricked by this

The RG40XXV has no built-in storage. It starts from whatever card is in the
first slot. **Take our card out, put the stock card back, and it is the same
handheld as before.** Keep the stock card in a drawer and use a spare one.

## What you need

- **A spare microSD card**, 8 GB or more. Everything on it is deleted.
- A card reader for your computer.
- **balenaEtcher** (https://etcher.balena.io) or **Raspberry Pi Imager**
  (https://www.raspberrypi.com/software/). Both are free.
- The image: `apotheca-rg40xxv.img.gz` (about 33 MB; its sha256 is in
  `apotheca-rg40xxv.img.gz.sha256` beside it).

## Putting it on the card

1. Put the **spare** card in the reader.
2. balenaEtcher: **Flash from file**, pick `apotheca-rg40xxv.img.gz`, **Select
   target**: the spare card, **Flash**.
   Raspberry Pi Imager: **Choose OS**, **Use custom**, pick the `.img.gz`;
   **Choose storage**: the spare card; **Next**, and **No** to any
   "customisation" question.
   Both read the `.gz` as it is: no need to unpack it.
3. When it is done the computer may show a drive called **APOTHBOOT**.
   **If Windows offers to format a drive, say no**: the other parts of the
   card are Linux's, or still empty, and Windows cannot read them. The
   **APOTHECA** drive appears only after the handheld's first start.
4. Eject the card.

## Starting it

1. Handheld off. Stock card out (into the drawer), our card into the
   **first slot** (TF1, the one the system starts from).
2. Press the power key.
3. **What you should see**, in order:
   - **The light** by the screen comes on within a second or two: the boot
     loader (U-Boot) is running. The screen is still black: U-Boot cannot
     drive this screen.
   - **Text scrolls on the screen** after about 2 to 5 seconds: the Linux
     kernel starting.
   - The first start only: a line `apotheca: DATA: first boot, growing ...`.
     It makes the APOTHECA part of the card, filling the rest of the card.
     It happens once.
   - **Apotheca's screen** comes up, a few seconds after the text. The
     first time it starts a new song.
   - **The buttons are set up already** (no "press A" setup). If they do
     the wrong things, delete `apotheca/controls.txt` on the APOTHECA drive:
     the next start asks for each button, as on the stock card.
4. To turn it off: see the next section.

## Turning it off, the power key, the battery

On this card Apotheca is the whole handheld, so **quitting it turns the
handheld off**. Every way off saves the song first.

| You do | What happens |
| --- | --- |
| **SEL + START** in Apotheca (the help card's START line says "turn off") | the song is saved, the handheld turns off |
| **Hold the power key 2 seconds** | the screen goes dark at once; the song is saved; off a moment later. **Let go when the screen goes dark** |
| **Tap the power key** | **the screen off**: the song keeps playing, the buttons do nothing (safe in a pocket), the volume keys still work. Tap again: the screen is back |
| Leave it alone **15 minutes**, the song stopped | it saves and turns itself off (not while a song plays, records, bounces or WiFi is on) |
| The battery at **5%**, not on a charger | it saves and turns itself off. **There is no warning on screen yet** |
| Hold the power key about 10 seconds | the power chip cuts the power by itself, whatever the system is doing (the last resort, as on stock: songs are saved as you go, but the last couple of seconds may be lost) |

To turn it on again: press the power key. **Plugged in while off, it
charges and stays off** (no charging picture on the screen yet; the power
chip charges by itself).

**Settings**, if you want other numbers: a text file `apotheca/power.txt`
on the APOTHECA drive, one setting a line (any you leave out keep their
usual value):

```
idle-off 30         minutes before it turns itself off when left alone (0: never; usual 15)
low-battery 8       the battery percent it turns off at (0: never; usual 5)
power-key sleep     a tap of the power key SLEEPS instead of turning the screen off
```

**`power-key sleep` has not been tried on any handheld.** Sleep (suspend) on
this chip is new in the Linux kernel we use. If it does not wake, hold the
power key 10 seconds and take the line out.

What the power key and battery did is written to `apotheca/logs/power.log`.

## What to send back

Whatever you see is useful. Send:

1. **A photo of the screen**, whatever is on it (black, garbage, text,
   sideways text).
2. **The boot log**, if it got far enough to write one. Put the card in the
   computer, open the **APOTHECA** drive: `apotheca/logs/boot-latest.txt`
   (the newest; `boot-0001.txt` is the first start). It holds everything
   the system found: the screen, the buttons, the sound chip, the battery,
   and the kernel's own messages. **If there is no `apotheca` folder at
   all, the system did not get that far**: the photo is then the clue.
3. **What the light did** (on, off, blinking) and whether text appeared.
4. If Apotheca came up: **does it draw right** (compare with the stock
   card), **is there sound** from the speaker and the headphones, **do the
   buttons and the stick do what they should**. **Power**: does SEL + START
   turn it off, does a tap of the power key turn the screen off and back,
   does holding it 2 s turn it off; `apotheca/logs/power.log` says what the
   battery read. `apotheca/apotheca.log` on
   the card says which way it draws (`screen: opengles2, accelerated` is the
   GPU) and how the sound card opened.

## If Apotheca does not come up, or draws wrongly

- **It draws with the GPU** (Mesa's panfrost driver). After two starts in a
  row that stop within seconds it starts drawing with the CPU instead, by
  itself. To make it always draw with the CPU, put an empty file named
  **`gpu-off`** in the `apotheca` folder on the APOTHECA drive.
- **No sound**: `apotheca/logs/boot-latest.txt` lists the sound cards and
  every mixer control; send it. A card can be named in a file
  `apotheca/sound-card` (its id, one word).
- The shell is still there underneath: from the computer (next section),
  `touch /run/apotheca/apotheca.hold` and stop Apotheca with
  `kill $(pidof apotheca)`; it is not started again until the hold file goes.

## If the screen stays black or shows garbage

Later units of the RG40XXV have another screen ("v2 panel"), which needs
the other device tree. To try it:

1. Card into the computer, open **APOTHBOOT**.
2. Copy `device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb` to the top
   of APOTHBOOT, and rename the copy to **`dtb.img`**, replacing the one
   there.
3. Card back in, power on.

(`dtb-in-use.txt` says which one the card was made with. To go back, copy
`sun50i-h700-anbernic-rg40xx-v.dtb` over `dtb.img` the same way.) If
ROCKNIX's batch 0 shows which tree your unit needs, the card can be built
with that one: `scripts/build-image.sh --dtb rg40xx-v-v2-panel`.

## A shell from the computer (optional)

Plug the handheld's USB-C port into the computer (a data cable, not a
charge-only one) after it has started. It appears as a serial port with a
shell on it:

- **Mac**: Terminal, `screen /dev/tty.usbmodem* 115200` (leave with Ctrl-A
  then K, then Y).
- **Windows**: Device Manager shows a new COM port under "Ports"; open it
  with PuTTY (Serial, that COM port, speed 115200).
- **Linux**: `screen /dev/ttyACM0 115200`.

Press Enter for a `#` prompt. Useful there:

| Type | What it does |
| --- | --- |
| `cat /data/apotheca/logs/boot-latest.txt` | the boot log |
| `evtest` | pick the gamepad, then press buttons: each press is printed |
| `speaker-test -t sine -l 1` | a tone from each side (we cannot hear it on the desk) |
| `/usr/libexec/apotheca/padprobe` | press buttons: prints each as Apotheca's `controls.txt` numbers it |
| `cat /data/apotheca/logs/power.log` | what the power key and the battery did |
| `poweroff` | turns the handheld off cleanly (Apotheca saves first) |

If the USB serial port never appears, the handheld also has a serial port on
four small holes inside (the plan's section 6); that needs opening the
device and a 3.3 V USB serial adapter. Only if needed.

## What the card holds

| Part | Name | What |
| --- | --- | --- |
| 8 KiB in | - | the boot loader (U-Boot), where the chip looks for it |
| 1 | APOTHBOOT (FAT32, 64 MB) | the kernel (`Image`), the device trees, `extlinux/extlinux.conf` |
| 2 | (Linux, 256 MB) | the system, **read-only**: a flat battery cannot spoil it |
| 3 | (Linux, 256 MB) | empty: room for a second system, for updates over WiFi later |
| 4 | APOTHECA (exFAT, the rest) | made on the first start. Songs, recordings, settings (`power.txt` too) and logs, in `apotheca`. A Mac or PC reads it |

## For whoever builds it

```
scripts/build-image.sh                       # build-image/apotheca-rg40xxv.img.gz
os/tools/check-image.sh build-image/apotheca-rg40xxv.img.gz   # the image's layout
python3 -I os/tools/qemu-test.py build-image/apotheca-rg40xxv.img.gz  # boots it in qemu (5 boots, about 15 min)
python3 -I os/tools/power-test.py           # apotheca-power on the desk, a made-up /sys (about 1 min)
```

`os/` is a Buildroot "external tree": `configs/apotheca_rg40xxv_defconfig`
(what goes in), `board/rg40xxv/` (kernel patches and config, device trees,
U-Boot settings, the card layout `genimage.cfg`, the files laid over the
system in `rootfs-overlay/`), `package/` (our own packages: Apotheca, the
joypad driver, apotheca-power, padprobe and fakepad), `patches-to-buildroot/` (two changes to
Buildroot's Mesa package, each saying why). The first build takes about three
hours on four cores (LLVM and clang, for Mesa's shaders, are most of it) and
about 13 GB; `--tidy` gives back the biggest part afterwards. Where every
file we did not write came from, and its licence: `board/rg40xxv/README.md`.
`scripts/build-image.sh -- legal-info` gathers every source and licence of
what is on the card.
