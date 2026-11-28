# Installing Apotheca

Apotheca runs on the **Anbernic RG40XXV**. There are two ways to put it on.
**Way A is the one to pick** unless you want to keep games on the same card.

Apotheca is alpha: expect rough edges. If something goes wrong, the last
section says which files to send.

## What you need

- The handheld, charged.
- A computer with a microSD card reader.
- **For way A: a spare microSD card**, 8 GB or more. **Everything on it is
  deleted.**
- **For way A: balenaEtcher** (https://etcher.balena.io) or **Raspberry Pi
  Imager** (https://www.raspberrypi.com/software/). Both are free. They write
  a card image (a copy of a whole card) onto a card.

## Choose your way

| | A) Apotheca's own card | B) The Ports menu of your firmware |
| --- | --- | --- |
| What it is | A card that starts the handheld **straight into Apotheca**. Nothing else is on it | Apotheca is one app among others on the card you have now (the firmware is the handheld's system: its menus and games) |
| You need | A spare microSD card | A firmware with a Ports menu |
| Your stock card | **Stays untouched**, in a drawer | Stays in. Apotheca adds one folder |
| What you get | Everything: the light round the stick, BRIGHTNESS, WiFi joined from Apotheca, its own hotspot, turning off from Apotheca | The music, all of it |
| What you lose | Games, until you swap the stock card back | The firmware keeps some things for itself: **no BRIGHTNESS or stick light rows, WiFi is joined in the firmware's settings, no hotspot**. The manual marks each one "only on Apotheca's own system" |

## A) Apotheca's own card

### 1. Download

- The card image:
  [apotheca-rg40xxv.img.gz](https://github.com/joshpanzarella/apotheca/raw/main/dist/firmware/apotheca-rg40xxv.img.gz)
  (about 40 MB). **Do not unpack it**: both flashing apps read the `.gz` as
  it is.
- Optional, to check the download:
  [apotheca-rg40xxv.img.gz.sha256](https://github.com/joshpanzarella/apotheca/raw/main/dist/firmware/apotheca-rg40xxv.img.gz.sha256).

### 2. Check it (optional)

A sha256 is a long number worked out from the file. If yours matches the one
in the `.sha256` file, the download is whole.

| Computer | Type in a terminal, in the download folder |
| --- | --- |
| Mac | `shasum -a 256 apotheca-rg40xxv.img.gz` |
| Windows | `certutil -hashfile apotheca-rg40xxv.img.gz SHA256` |
| Linux | `sha256sum apotheca-rg40xxv.img.gz` |

### 3. Flash the card

1. Put the **spare** card in the reader. Not your stock card.
2. **balenaEtcher**: **Flash from file**, pick `apotheca-rg40xxv.img.gz`.
   **Select target**: the spare card. **Flash**.

   **Raspberry Pi Imager**: **Choose OS**, **Use custom**, pick the `.img.gz`.
   **Choose storage**: the spare card. **Next**, and **No** to any
   "customisation" question.
3. When it is done the computer may show a drive called **APOTHBOOT**.
   **If Windows asks to format a drive, say no.** The other parts of the card
   are Linux's, and Windows cannot read them.
4. Eject the card.

### 4. First start

1. Handheld off. Take the stock card out and keep it safe.
2. Put the Apotheca card in the **first card slot (TF1)**. The handheld
   always starts from that slot.
3. Press the power key.
4. You see the start-up picture, and its **outer ring of signs starts
   turning**. Then Apotheca's **SONG** screen. The buttons are already set
   up: no "press A" questions.
5. **Do not turn it off during the very first start.** It is making the
   card's APOTHECA drive, once.

The light by the power key also says how far it got; the table in
[os/README.md](https://github.com/joshpanzarella/apotheca/blob/firmware/os/README.md)
("Starting it") has each step.

### 5. Where your files live

Put the card in a computer: the **APOTHECA** drive has an **`apotheca`**
folder. Songs, recordings, settings and logs are all in it. A Mac or PC reads
it. (APOTHECA appears only after the first start.)

### Turning it off

- **SEL + START** in Apotheca: it saves the song and turns the handheld off.
- Or **hold the power key 2 seconds**. Let go when the screen goes dark.
- A tap of the power key turns only the screen off. The song keeps playing.

## B) The Ports menu of your firmware

1. Download
   [Apotheca-port.zip](https://github.com/joshpanzarella/apotheca/raw/main/dist/Apotheca-port.zip)
   and unzip it. You get **`Apotheca.sh`** and an **`apotheca`** folder.
2. Put the handheld's card in the computer.
3. Copy **both** into the **firmware's ports folder** on the card, beside the
   other ports. Its name and place depend on your firmware. **Not checked
   here for each firmware.**
4. Card back in, start the handheld, open **Ports**, start **Apotheca**.
5. **The first start asks for each button** ("PRESS A", "PRESS B"...). About
   twenty seconds, once. A button your handheld does not have (MENU, the
   stick pressed in): wait eight seconds and it moves on.

Your files live in that **`apotheca`** folder in the ports folder: songs,
recordings, settings. If it does not start, the reason is in
`apotheca/apotheca.log`.

**SEL + START** quits back to the firmware.

## First steps

The [manual](docs/manual.md) has every screen. To start:

- **Help**: hold **MENU** for a card of what the buttons do on this screen.
  On a handheld or firmware without MENU, hold **SEL** by itself instead.
- **A beat**: on SONG, go to **+ TRACK**, press **A**, pick **DRUMS**. Press
  **A** on its first cell to open the drum grid. **A** places a hit, **B**
  deletes one. **START** plays and stops.
- **L2 / R2** go through the tabs: SONG, PATTERN, DEVICE, FX, MOD, SET.
- **Nothing to save**: songs save themselves as you go.
- **Recordings over WiFi**: on the SET tab, turn **WIFI PAGE** on.
  - Way A: join your network on the **NETWORK** row first (hold A, up / down,
    let go). Or turn **HOTSPOT** on and join the `apotheca` network from your
    phone.
  - Way B: join WiFi in the firmware's own settings first.
  - The row shows an address like `apotheca.local/4821` (way B:
    `192.168.1.23:8080/4821`). Open it in a browser. **The last four digits
    are the PIN**: a new one each time WiFi is turned on.
  - Manual: [Files over WiFi](docs/manual.md#files-over-wifi).

## Updating

Which build you have: the top right of the **SET** tab says `BUILD N`.

### Way A: the app

Either:

- **Over WiFi**: open the WiFi page, press **UPDATE APOTHECA**, choose the
  card's app file,
  [dist/firmware/apotheca](https://github.com/joshpanzarella/apotheca/raw/main/dist/firmware/apotheca).
  The handheld says AN UPDATE HAS ARRIVED: **hold A a second** to take it.
  It runs from the next start (SEL + START, then power on).
- **By card**: handheld off, card in the computer. Copy that same `apotheca`
  file into the APOTHECA drive's **`apotheca`** folder; **Replace** the one
  there. Eject, card back, power on.

**Use the card's file (`dist/firmware/apotheca`), not the port's.** If a new
app fails to get going twice, the card puts its own back by itself.

### Way A: a new card image

Only the app updates by file. **A change to the system under it** (the
start-up picture, the screen's set-up, power, WiFi parts) **needs a new card
image**, and **flashing deletes everything on the card**. First:

1. Copy the whole **`apotheca`** folder from the APOTHECA drive to your
   computer.
2. Flash the new image as in A step 3, and start it once (A step 4).
3. Copy your **`songs`**, **`samples`**, **`presets`** and **`bounces`**
   folders back into the new card's `apotheca` folder. Leave the rest: the
   new card makes its own. You join WiFi again.

### Way B

- **Over WiFi**: UPDATE APOTHECA, choose the `apotheca` file from inside the
  new zip's `apotheca` folder (or
  [dist/apotheca](https://github.com/joshpanzarella/apotheca/raw/main/dist/apotheca)).
  Hold A to take it; it runs from the next start.
- **By card**: unzip the new `Apotheca-port.zip` over the old one in the
  ports folder, replacing its files. Your songs and recordings stay.

## Going back

- **Way A**: turn off, take the Apotheca card out, put the stock card back.
  **The handheld has no storage of its own**: it is exactly as it was. Keep
  the Apotheca card for next time.
- **Way B**: copy out anything you want from the `apotheca` folder, then
  **delete `Apotheca.sh` and the `apotheca` folder** from the ports folder.

## If something goes wrong

**Send the files below, with a photo of the screen and what the light did**
([GitHub issues](https://github.com/joshpanzarella/apotheca/issues)).

### It will not start (way A)

| You see | Do this |
| --- | --- |
| A green light blinking evenly, alone, never red, for more than ten seconds | Hold the power key to turn it off. Flash the other image, [apotheca-rg40xxv-lpddr3.img.gz](https://github.com/joshpanzarella/apotheca/raw/main/dist/firmware/apotheca-rg40xxv-lpddr3.img.gz) (for the other kind of memory some units have), and try again |
| Stops on the start-up picture | It switches to drawing without the graphics chip by itself after two failed starts. Or put an empty file named **`gpu-off`** in the `apotheca` folder |
| Anything else | Send every `.txt` file at the top of the **APOTHBOOT** drive |

### The screen is black, striped or garbage (way A)

The RG40XXV came with **two kinds of screen**. The card picks by itself, but
can pick wrong.

1. Wait 30 seconds. If the green light blinks and the screen is still wrong:
2. Card in the computer, open **APOTHBOOT**.
3. Make an empty file at the top named **`screen-v2.txt`** (the newer screen)
   or **`screen-first.txt`** (the first one). Try one, then the other.
   Check the name has no second ending, like `screen-v2.txt.txt`.
4. Card back, power on, wait 30 seconds.

Delete the file to go back to the card's own choice.

### No sound

- Check **VOLUME** on the SET tab and the handheld's volume keys.
- Way A: **headphones in, the speaker is off**. If the speaker is silent with
  no headphones in, send `apotheca/logs/boot-latest.txt`.

### Which files to send

All in the **`apotheca`** folder (way A: on the APOTHECA drive; way B: in the
ports folder). Send what is there.

| File | What it holds |
| --- | --- |
| `apotheca.log` | what Apotheca said as it ran |
| `logs/crash.txt` | a crash or a freeze |
| `logs/sound.log` | when the sound stuttered |
| `logs/apotheca-start.log` | way A: how the app was started, and any update |
| `logs/off.log` | way A: each step of turning off (send it if it will not turn off) |
| `logs/boot-latest.txt` | way A: the system's start, the sound card, the buttons |
| `device-info.log` | way B: what the firmware offers |
