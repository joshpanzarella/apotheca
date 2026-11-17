# Apotheca's own system for the RG40XXV

This folder builds an SD card that starts the handheld on **our own Linux
system** instead of the stock firmware. The plan and its reasons:
`docs/plans/firmware-plan.md`.

**Batch 1 is a console only.** It does not run Apotheca yet. It shows
whether our system starts on your unit: the boot loader, the kernel, the
screen. Text scrolls on the screen and ends at a `#` prompt. Nothing else.

**Nobody has started it on a handheld yet.** Only the desk tests below have
run.

## The device cannot be bricked by this

The RG40XXV has no built-in storage. It starts from whatever card is in the
first slot. **Take our card out, put the stock card back, and it is the same
handheld as before.** Keep the stock card in a drawer and use a spare one.

## What you need

- **A spare microSD card**, 8 GB or more. Everything on it is deleted.
- A card reader for your computer.
- **balenaEtcher** (https://etcher.balena.io) or **Raspberry Pi Imager**
  (https://www.raspberrypi.com/software/). Both are free.
- The image: `apotheca-rg40xxv.img.gz` (about 25 MB; its sha256 is in
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
   - It ends with **"Apotheca system, batch 1: a console only."** and a
     `#` prompt. The buttons do not type: there is nothing more to do on
     the screen.
4. **To turn it off: hold the power key about 10 seconds.** (There is no
   menu yet. The card is safe: the system part is read-only.)

**Do not leave it on for long, and do not let the battery run low with this
card in.** Batch 1 has no low-battery shutdown; charging should work as
usual (the charger chip does it), but that is not tested.

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
| `poweroff` | turns the handheld off cleanly |

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
| 4 | APOTHECA (exFAT, the rest) | made on the first start. Logs now; songs and recordings later. A Mac or PC reads it |

## For whoever builds it

```
scripts/build-image.sh                       # build-image/apotheca-rg40xxv.img.gz
os/tools/check-image.sh build-image/apotheca-rg40xxv.img.gz   # the image's layout
python3 -I os/tools/qemu-test.py build-image/apotheca-rg40xxv.img.gz  # boots it in qemu
```

`os/` is a Buildroot "external tree": `configs/apotheca_rg40xxv_defconfig`
(what goes in), `board/rg40xxv/` (kernel patches and config, device trees,
U-Boot settings, the card layout `genimage.cfg`, the files laid over the
system in `rootfs-overlay/`), `package/` (our own packages). Where every
file we did not write came from, and its licence: `board/rg40xxv/README.md`.
`scripts/build-image.sh -- legal-info` gathers every source and licence of
what is on the card.
