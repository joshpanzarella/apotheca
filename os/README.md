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

**It has started on the owner's handheld** (2026-11-17): with the newer
screen's device tree, text and then Apotheca came up. The boot log there
found the sound card, the buttons and the battery. Everything else below
(sound heard, the GPU, power) has been tried only on the desk. The card writes down everything it finds (the boot log, below), so
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

## One image for both kinds of memory

**`apotheca-rg40xxv.img.gz` starts every RG40XXV.** They have been made
with two kinds of memory, LPDDR4 and LPDDR3, and nothing on the outside
says which (the owner's unit with the newer screen is LPDDR4, the other
LPDDR3: plan, sections 16 and 17). The card's first program finds out:

- It tries **LPDDR4 first, at 1.1 V**. That voltage is LPDDR4's own and is
  under LPDDR3's 1.2 V, so the first try harms neither kind.
- If that fails it raises the rail to 1.2 V and tries **LPDDR3**.
- A kind is taken only if the set-up says it passed **and** a write-and-read
  test of 64 places across the memory passes.
- It **remembers the answer** in a spare register of the handheld's clock,
  so later starts go straight to the right kind. If a try never comes back
  (the memory code resets the handheld instead), the note it left says so
  and the next run starts with the other kind.

`uboot-report.txt` (`apotheca_dram`) and `linux-report.txt` say which kind
was found. `memory-kind.txt` on APOTHBOOT says which image the card is.

| Image | For | Built with |
| --- | --- | --- |
| `apotheca-rg40xxv.img.gz` | **every unit** | `scripts/build-image.sh` |
| `apotheca-rg40xxv-lpddr4.img.gz` | LPDDR4 only (a fallback) | `scripts/build-image.sh --dram lpddr4` |
| `apotheca-rg40xxv-lpddr3.img.gz` | LPDDR3 only (a fallback; it is what first started the LPDDR3 unit) | `scripts/build-image.sh --dram lpddr3` |

(The script builds U-Boot again by itself when the kind asked for changes.)

**To make it try a kind first** at the next start: an empty file named
`dram-lpddr3.txt` or `dram-lpddr4.txt` at the top of APOTHBOOT, one start,
then delete it. **What this cannot do**: help a handheld that does not
start. The file is read by U-Boot, which runs only after the memory works.
For that case use a one-kind image. A one-kind card in the wrong handheld
does no harm: the green light blinks evenly, alone, and nothing else
happens.

## Starting it

1. Handheld off. Stock card out (into the drawer), our card into the
   **first slot** (TF1, the one the system starts from).
2. Press the power key.
3. **What you should see**, in order. The lights say how far it got, even
   when the screen shows nothing:

   | Step | You see | It means |
   | --- | --- | --- |
   | 1 | **green, then green and red together** within a second or two of the power key (together they may look like one orange light). On an LPDDR3 unit the green may go out and come back once before the red joins it | the card's first small program (the SPL) ran and set the memory up |
   | 2 | **red alone**, and **the screen lit but empty** | the boot loader (U-Boot) is running |
   | 3 | the red goes out, **the green blinks in pairs** (blink-blink, pause) | Linux has started |
   | 4 | the red on again for a moment (often too short to see) | Linux's first program is looking for the card |
   | 5 | the red out, **the green blinks evenly**, about once a second; the start-up picture's **outer ring of signs starts turning**, then **Apotheca** | the system is starting |
   | 6 | you press any button: **the green light stays on** | this screen now counts as seen working |

   **If it stops on the way:**

   | You see | It means | What to do, or what it does by itself |
   | --- | --- | --- |
   | **green alone, blinking evenly, from the start, never red, for more than ten seconds** | the first program cannot set this handheld's memory up as either kind and tries again for ever | hold the power key to turn it off; try the LPDDR3-only image, then the LPDDR4-only one, and tell us |
   | green and red blinking together | the memory is set up, the boot loader does not start | tell us |
   | red alone, staying | the boot loader runs, Linux does not start | send `uboot-started.txt` and `uboot-report.txt` |
   | green in pairs, no red, for a minute | Linux started and stopped before its first program | resets after 60 s, so U-Boot can save what Linux said (`kernel-from-memory.txt`); after three resets it stops: off, or the red light blinking evenly until the power key is held |
   | green flickering fast, red on | Linux sees no card | resets after about a minute |
   | green blinking evenly after the steps above, the screen dark or garbage | the system runs, the screen's kind is wrong | off after 5 minutes with no button. Make `screen-first.txt` or `screen-v2.txt` (below) |

   - **Do not switch off during the very first start**: it makes the
     APOTHECA part of the card then, once.
   - **The handheld never stays on behind a dark screen**: see the table.
     (`screen-check 0` in `apotheca/power.txt` turns the 5-minute rule off.)
   - **The buttons are set up already** (no "press A" setup). If they do
     the wrong things, delete `apotheca/controls.txt` on the APOTHECA drive:
     the next start asks for each button, as on the stock card.
   - **Seen on a handheld so far** (2026-11-18): the LPDDR4 unit works
     with the LPDDR4-only card and the LPDDR3 unit with the LPDDR3-only
     card (plan, section 16). **This one image for both has run on
     neither yet.**
   - **No text on the screen**, from the power key until Apotheca draws, and
     at turn-off: only the picture. (Until 2026-11-28 one line of characters
     showed below it: the prompt of a shell on the screen, and a blinking
     cursor. The shell is gone and the cursor is off,
     `vt.global_cursor_default=0` in `extlinux/extlinux.conf`.) The system's
     messages still go to the serial console (below) and into the boot log,
     `apotheca/logs/boot-latest.txt`. To see them on the screen for once, add
     `console=tty1` to the `APPEND` line of `extlinux/extlinux.conf` on
     APOTHBOOT.
   - **The outer ring turns** (since 2026-11-28): as soon as the system's
     first program runs, the ring of seven planet signs round the dial turns
     slowly clockwise (one turn in 10 s, 30 pictures a second), so you can
     see it is doing something. Everything else stays exactly the kernel's
     picture. It stops by itself the moment Apotheca's own picture is on the
     screen. The same turning picture comes back at turn-off, from the
     moment Apotheca has saved and stopped until the power goes. See "The
     turning picture" below.
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

To turn it on again: press the power key. **A charger plugged in while it
is off charges it and it stays off**, as on ROCKNIX (the rule was taken out
on 2026-11-17 while one unit would not start; it was not the cause, and it
is back: plan, section 17).

**If it does not turn off** (the screen dark or not, it stays on until the
power key is held 10 seconds), send `apotheca/logs/off.log`. Every step of
turning off writes a line there, and the lines are on the card before
anything that could hang starts, so the last line is the step it stuck at.
(Until 2026-11-28 every line was synced on its own: about 0.13 s a line,
3 s of a turn-off. Now the card is synced once a step: the logging should
cost about 1 s of a whole turn-off, plus what the steps themselves take.)

```
----- a turn-off (boot 12; boot-latest.txt is this start)
[65.40] off: apotheca-keep asks for the handheld off: apotheca asked, after 65s, code 0 (quit)
[65.41] off: poweroff: asking init to turn the handheld off
[65.52] off: rcK: going down (asked by apotheca-keep: ...)
[65.60] off: S95apotheca stop: begins (at most 40 s)
...
[66.10] off: S05data: unmounting /data (at most 10 s; ...): the LAST line on the card
```

Once Apotheca has stopped, the screen shows the start-up picture with its
outer ring turning until the power goes (`splash:` in the log). When the
power key is held the screen is dark by then, so it is not seen.

WiFi, if any of it is on, is turned off right after Apotheca stops (`WiFi:
turning it off`, at most 5 s, then `WiFi: off in ... s`), so a slow radio
shows in the log instead of holding the very end of the turn-off.

Who asked (Apotheca's quit or idle, the power key, the battery; nothing
said means `poweroff` from a shell), then each stop script with how long
it took. No step can wait for ever any more: each has a time limit, and
one cut short says `CUT SHORT`, with what was still running and where it
waited. The lines after the unmount (and every line, if the card stopped
answering) go to the kernel's own log only. The file keeps its last 200
lines.

## The turning picture (start-up and turn-off)

`apotheca-splash` (`package/apotheca-tools/src/apotheca-splash.c`) draws the
start-up picture with its ring of planet signs turning. **Not yet seen on a
handheld**: only on the desk (its frames) and in qemu.

| When | Started by | Stops |
| --- | --- | --- |
| start-up | inittab's very first line (`--boot`), before any start-up script | when Apotheca's picture is on the screen (it watches the display); or the screen has been another program's for 10 s; or after 90 s |
| turn-off | `off` (Apotheca quit or idle) or `S95apotheca` (power key, battery, shell `poweroff`), once Apotheca has stopped | init's TERM at the very end: the last picture stays until the power goes (at most 120 s) |

- **The picture is the kernel's own, to the pixel**: `os/tools/make-logo.py`
  writes the kernel's logo and `splash.bin` together, and `check-image.sh`
  compares them. Only the band between radius 82 and 98 round the dial is
  drawn again (8,803 pixels); the signs pass under the four element discs,
  as in the picture.
- **Cost**: on the desk 0.12 to 0.13 ms a frame; the handheld is 13 to 20
  times slower, so about 1.6 to 2.7 ms a frame, **5 to 8% of one of the
  four cores** at 30 a second, at nice 10. (Under qemu, which emulates the
  CPU and is no guide to speed, 2.5 to 3.8 ms.) That is an estimate, not a
  measurement: the boot log's line `apotheca-splash: start-up: stopped
  (...) ... ms a frame to draw` gives the real number.
- Each frame is copied in just after the screen's vertical blank, so it
  should not tear (only the handheld can show that).
- **It never holds anything up**: it goes into the background at once,
  opens nothing on the APOTHECA drive, and with no `/dev/fb0` or any other
  failure it ends quietly. Its start and stop, and why it stopped, are lines
  `apotheca-splash:` in the kernel's log (the boot log has the start-up's).
- To see it on the desk: `apotheca-splash --data splash.bin --frames DIR N`
  writes N frames as pictures, `--bench N` times them.

## Headphones or speaker, one or the other

**Headphones in: the speaker is off. Headphones out: the speaker is on.**
Also while sound plays, and right at the start whichever way the socket is.
(Until 2026-11-18 both played: plan, section 18.) **Not tried on a
handheld.**

- The sound chip has one output for both. The headphone socket has a
  contact that says "something is in" (pin PI3); the speaker's amplifier has
  an on/off line (PI5). The kernel now turns the amplifier off while the
  socket says "in" (patch 0172), and `apotheca-power` does the same from
  outside the kernel, as a second belt. Only the speaker's switch is
  touched; **no volume changes**.
- **If the speaker is silent with NO headphones in**, the socket reads the
  wrong way round on that handheld. Put `headphones inverted` in
  `apotheca/power.txt` (below), start again, and tell us. `headphones
  ignore` is the old behaviour (both play).
- `linux-report.txt` has a "headphones and speaker" section: the socket's
  state, the speaker's switch, the two pins. Send it once with headphones in
  at the start and once with them out.

**Settings**, if you want other numbers: a text file `apotheca/power.txt`
on the APOTHECA drive, one setting a line (any you leave out keep their
usual value):

```
idle-off 30         minutes before it turns itself off when left alone (0: never; usual 15)
low-battery 8       the battery percent it turns off at (0: never; usual 5)
power-key sleep     a tap of the power key SLEEPS instead of turning the screen off
headphones ignore   the speaker stays on with headphones in (as it was before 2026-11-18)
headphones inverted for a handheld whose speaker goes off when the headphones come OUT
```

**`power-key sleep` has not been tried on any handheld.** Sleep (suspend) on
this chip is new in the Linux kernel we use. If it does not wake, hold the
power key 10 seconds and take the line out.

What the power key and battery did is written to `apotheca/logs/power.log`.

## The screen's brightness and the light round the stick

On this card nothing but Apotheca sets them, so Apotheca has them: the SET
tab's **BRIGHTNESS** (10% to 100%) and **STICK LIGHT** (OFF, ON, BEAT, STEPS),
kept for next time. **MENU + a volume key** changes the brightness from any
screen. A tap of the power key turns the ring off with the screen. **None of it
has been seen on a handheld yet**: does the ring light, what colour, and in
STEPS does the lit LED go round clockwise from the top? (If not, say where it
starts and which way it goes.) The boot log's "lights" section lists what the
system found.

## What to send back

Whatever you see is useful. Send:

1. **A photo of the screen**, whatever is on it (black, garbage, text,
   sideways text).
2. **Every `.txt` file from the top of the APOTHBOOT drive**, whatever is
   there:

   | File | Written by | Holds |
   | --- | --- | --- |
   | `uboot-started.txt` | U-Boot, its first act at every start | nothing much: that it is there says U-Boot ran and can write the card |
| `uboot-report.txt` | U-Boot, at every start | **the kind of memory found** (`apotheca_dram`), the pin it read, the tree it chose and why, counts of starts and of resets in a row (hexadecimal) |
   | `kernel-from-memory.txt` | U-Boot, after a reset | what Linux said in the start **before**, found still in memory (512 KiB, the text goes round) |
   | `kernel-log.txt` | Linux's first program, **only when the card kept it waiting 2 s or it could not go on** | how far it got, the cards Linux sees, what Linux said |
   | `linux-early.txt` | the system's first script, every 5 s until the boot log is written | the start-up step by step and what is running |
   | `linux-report.txt` | the system, once it is up | the full boot log, the memory's kind in it (also on APOTHECA: `apotheca/logs/boot-latest.txt`) |
   | `kernel-from-card.txt` | a start that works | the same as `kernel-from-memory.txt`, taken from a hidden part of the card U-Boot also writes |

   **If a handheld left no new file at all**: copy what is there, then put
   the card in a handheld that does start with it, start it once, wait a
   minute, turn it off, and look for `kernel-from-card.txt`.
3. **What the light did** (on, off, blinking) and whether text appeared.
4. If Apotheca came up: **does it draw right** (compare with the stock
   card), **is there sound** from the speaker and the headphones, **do the
   buttons and the stick do what they should**. **Power**: does SEL + START
   turn it off, does a tap of the power key turn the screen off and back,
   does holding it 2 s turn it off; `apotheca/logs/power.log` says what the
   battery read, `apotheca/logs/off.log` how far each turn-off got. `apotheca/apotheca.log` on
   the card says which way it draws (`screen: opengles2, accelerated` is the
   GPU) and how the sound card opened.

## If Apotheca does not come up, or draws wrongly

- **It draws with the GPU** (Mesa's panfrost driver). After two starts in a
  row that stop within seconds it starts drawing with the CPU instead, by
  itself. To make it always draw with the CPU, put an empty file named
  **`gpu-off`** in the `apotheca` folder on the APOTHECA drive. Drawing
  with the CPU is slower (the screen's moving parts may lag), but it does
  not need the GPU at all. `apotheca/apotheca.log` then says
  `screen: software`. **Images before 2026-11-27 crashed on every start
  this way** and stayed on the start-up picture; this one does not.
- **No sound**: `apotheca/logs/boot-latest.txt` lists the sound cards and
  every mixer control; send it. A card can be named in a file
  `apotheca/sound-card` (its id, one word).
- The shell is still there underneath: from the computer (next section),
  `touch /run/apotheca/apotheca.hold` and stop Apotheca with
  `kill $(pidof apotheca)`; it is not started again until the hold file goes.

## If the screen stays striped, black or shows garbage

The RG40XXV has had **two screens**, and each needs its own device tree.
**The card picks the tree by itself** (since 2026-11-17): as it starts,
U-Boot reads pin PE6, the way Anbernic's own boot loader tells the screens
apart (low: the newer screen), and loads the matching tree from
`device_trees/`. **A `dtb.img` at the top of APOTHBOOT overrides it** (used
whatever the screen); a new card has none (`dtb-in-use.txt` says `auto`).
The owner has one unit of each. The unit with the newer screen starts
right with it; **the unit with the first screen has not started with any of
our cards yet** (plan, section 15), so that PE6 reads high there is still
from the stock boot loader's code, not seen. `uboot-report.txt` on APOTHBOOT
and the boot log's "screen:" line give PE6 and the tree U-Boot chose.

| Tree | Screen | Units |
| --- | --- | --- |
| `sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb` | the newer screen (PE6 low) | later units: Knulli and muOS users reported new RG40XXVs with only stripes from November 2025, and ROCKNIX added this tree in April 2026 |
| `sun50i-h700-anbernic-rg40xx-v.dtb` | the first screen (PE6 high) | earlier units |
| `sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb` | the newer screen, with the pixel clock edge and the timings Anbernic's own system uses | never chosen by itself: to compare |

**First wait 30 seconds.** If the screen stays dark, striped or garbage
while **the green light blinks** (Linux is running), the card has the
screen's kind wrong. Tell it:

1. Card into the computer, open **APOTHBOOT**.
2. Make an empty text file at the top of it named **`screen-first.txt`**
   (the first screen) or **`screen-v2.txt`** (the newer one). Anything or
   nothing in it; only the name counts. (Check the name has no second
   ending, like `screen-first.txt.txt`.)
3. Card back in, power on, and wait 30 seconds.

Delete the file to go back to the card's own choice. `uboot-report.txt`
says what was read and used: `apotheca_pe6` (the pin), `apotheca_why` (PE6,
the file's name, or dtb.img) and `fdtfile` (the tree).

A whole tree by hand still works and wins over both: copy one from
`device_trees/` to the top of APOTHBOOT and rename the copy to
**`dtb.img`**.

To go back, delete `dtb.img`. A card can be built
with a `dtb.img` already in place: `scripts/build-image.sh --dtb rg40xx-v`
(or `rg40xx-v-v2-panel`, `rg40xx-v-v2-stock-panel`).

**Faint vertical lines in a dark picture** (seen on the owner's unit with
the v2 tree and with `v2-stock-panel`, not fading over 15 minutes). Four
things to try, each a whole `dtb.img` in its own folder, so no renaming:

| Folder in `device_trees/try/` | Changes, from the v2 tree, toward Anbernic's own settings |
| --- | --- |
| `1-pins-40ma` | the 28 RGB data pins driven at 40 mA (stock: drive 3; ours: the chip's reset value, 20 mA) |
| `2-backlight-50khz` | the backlight's PWM at 50 kHz (stock `lcd_pwm_freq` 50000; ours 25 kHz) |
| `3-dither` | an 18-bit bus, the display controller dithering (stock `lcd_frm 1`) |
| `4-all-as-stock` | all three, with the stock clock edge and timings |

Mac: open **APOTHBOOT**, then `device_trees`, `try`, the folder; drag its
`dtb.img` to the top of APOTHBOOT, **Replace**. Eject, power on, look at a
dark picture. Try them one at a time; say which, if any, takes the lines
away; delete `dtb.img` afterwards to go back to the card's own choice. Also worth a look: does **the stock card** show the same faint lines
in a dark picture? If it does, it is the panel itself.

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
| `cat /data/apotheca/logs/off.log` | each step of the last turn-offs, the last one reached at the end |
| `poweroff` | turns the handheld off cleanly (Apotheca saves first) |

If the USB serial port never appears, the handheld also has a serial port on
four small holes inside (the plan's section 6); that needs opening the
device and a 3.3 V USB serial adapter. Only if needed.

## A new Apotheca without a new card

A fix to Apotheca can come as **one file, `apotheca`** (made by
`scripts/build-firmware-app.sh` into `dist/firmware/apotheca`). No new card:

1. Handheld off. Card into the Mac.
2. Open the **APOTHECA** drive, then its **apotheca** folder.
3. Drag the new **`apotheca`** file into that folder. Finder asks whether to
   replace the one there: **Replace**.
4. Eject the card (Finder: the eject arrow beside APOTHECA), card back in,
   power on.

At the start the card sees the program changed (its sum is not the one it
put there) and runs it. **If it does not get going twice, the card's own
Apotheca comes back by itself** and the new one is kept as
`apotheca.failed`. A file that is not a program for this handheld (cut
short, the wrong file) is never run. Every step is written to
`apotheca/logs/apotheca-start.log` (its lines begin `program:`). An update
over WiFi (the WiFi page's UPDATE) lands in the same folder and follows the
same rules.

The card's own Apotheca is put back over a copied one only by a newer card
image, or by the two failed starts.

## A screen change without a new card

The screen's start-up commands and timings (its "panel file") can travel
inside the device tree, so **a screen change is one file, a new
`dtb.img`**: copy it to the top of **APOTHBOOT**, replacing the one there
(as in "If the screen stays striped" above). The kernel's own panel files
are used whenever the device tree carries none.

(Why not a loose `panels/*.panel` file on APOTHBOOT: the screen is set up
by a driver inside the kernel, about two seconds into the start, before any
part of the card is opened as a drive; and a panel file built into the
kernel always wins over one on a drive. The device tree is read before all
of that.)

To make one (on the desk): `os/tools/panel-dtb.sh IN.dtb NEW.panel OUT.dtb`
puts the panel file's bytes into the tree as `apotheca,panel-firmware`.
The boot log's line `panel-mipi spi0.0: firmware: ...` says which panel data
ran, with its size and CRC-32.

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
scripts/build-image.sh                       # build-image/apotheca-rg40xxv.img.gz (both kinds of memory)
scripts/build-image.sh --dram lpddr3         # ...-lpddr3.img.gz, one kind only (or lpddr4)
os/tools/dram-kind-test.sh --mutants         # which memory: the rule and the SPL's loop, on the desk (seconds)
python3 -I os/tools/uboot-pick-test.py build-image/apotheca-rg40xxv.img.gz  # U-Boot's start under qemu (minutes)
os/tools/check-image.sh build-image/apotheca-rg40xxv.img.gz   # the image's layout
python3 -I os/tools/qemu-test.py build-image/apotheca-rg40xxv.img.gz  # boots it in qemu (6 boots, about 20 min)
scripts/build-firmware-app.sh                # Apotheca alone: dist/firmware/apotheca (minutes)
os/tools/panel-dtb.sh IN.dtb NEW.panel OUT.dtb   # a panel file into a device tree
python3 -I os/tools/power-test.py           # apotheca-power on the desk, a made-up /sys (about 1 min)
os/tools/off-test.sh [SHELL]                 # the turn-off's log and time limits on the desk (about 30 s)
python3 os/tools/make-logo.py                # the start-up picture (the kernel's) and splash.bin (the turning ring)
```

`os/` is a Buildroot "external tree": `configs/apotheca_rg40xxv_defconfig`
(what goes in), `board/rg40xxv/` (kernel patches and config, device trees,
U-Boot settings, the card layout `genimage.cfg`, the files laid over the
system in `rootfs-overlay/`), `package/` (our own packages: Apotheca, the
joypad driver, apotheca-power, apotheca-splash, padprobe and fakepad), `patches-to-buildroot/` (two changes to
Buildroot's Mesa package, each saying why). The first build takes about three
hours on four cores (LLVM and clang, for Mesa's shaders, are most of it) and
about 13 GB; `--tidy` gives back the biggest part afterwards and **keeps the
kernel's and U-Boot's build folders**, so a change to either (a patch, a
device tree, a panel file) rebuilds in minutes; `--tidy-all` gives those
back too (about 2 GB more), and the next kernel takes the hour again. Where every
file we did not write came from, and its licence: `board/rg40xxv/README.md`.
`scripts/build-image.sh -- legal-info` gathers every source and licence of
what is on the card.

**The CPU fallback** (`gpu-off`, or two failed starts in a row:
`rootfs-overlay/usr/libexec/apotheca/run`). Mesa is built with softpipe (a
software driver that needs no LLVM) next to panfrost, so its KMS software
path, kms_swrast, is in `libgallium`: softpipe draws into a plain ("dumb")
buffer that the display shows. **Both halves of Mesa must be told**:

| Set | GBM (SDL makes it) | EGL | Result |
| --- | --- | --- | --- |
| `LIBGL_ALWAYS_SOFTWARE=1` only (before 2026-11-27) | panfrost | pure software swap | crash on the first frame, every start: a call to address 0 in `dri2_drm_swap_buffers` (libEGL), on panfrost's drawable |
| `GBM_ALWAYS_SOFTWARE=1` and `LIBGL_ALWAYS_SOFTWARE=1` | kms_swrast | kms_swrast | softpipe draws, KMS shows it |

GBM reads only `GBM_ALWAYS_SOFTWARE`; EGL reads `LIBGL_ALWAYS_SOFTWARE`
and, told it, takes the pure-software swap unless GBM's driver is
kms_swrast. **qemu cannot show the crash**: its virtio GPU has no Mesa
driver, so there GBM falls back to kms_swrast by itself, in both modes.
`qemu-test.py` reads the running program's environment instead, and
`check-image.sh` checks the pieces are in the image. SDL keeps its own
software renderer here (`SDL_RENDER_DRIVER=software`): it draws the picture
with SDL's CPU code, and KMSDRM, which has no framebuffer of its own, takes
it to the screen as one texture a frame through GLES2 on softpipe; with
SDL's opengles2 renderer every rectangle and letter would go through
softpipe instead.
