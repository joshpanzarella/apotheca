APOTHECA SYSTEM - BOOT PARTITION

The RG40XXV has had two screens. THE CARD FINDS WHICH ONE THIS HANDHELD
HAS BY ITSELF (the way Anbernic's own system does) and starts with the
matching device tree, the description of the handheld's parts:

  device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb   the newer screen
  device_trees/sun50i-h700-anbernic-rg40xx-v.dtb            the first screen

A file named dtb.img at the top of this drive overrides that: when there
is one, it is used whatever the screen. There is none on a new card
(dtb-in-use.txt says "auto"). To take an override away, delete dtb.img.

A file named screen-first.txt or screen-v2.txt at the top of this drive
(empty, or anything in it) says which screen instead of the handheld: the
card then starts with that screen's tree. It is the easy way to try the
other screen: make an empty text file with that name. Delete it to go back.
(dtb.img, when there is one, still wins over both.)

WHAT YOU SEE WHEN IT STARTS, AND WHAT IT MEANS

  1. Green and red together, within a second of the power key (it may
     look like one orange light).
        The card's first small program ran and set the memory up.
  2. RED ALONE, and the screen lit but empty.
        The boot loader (U-Boot) is running.
  3. The red goes out and the green light blinks in PAIRS (blink-blink,
     pause): Linux has started.
  4. The red comes on again for a moment (often too short to see): Linux's
     first program is looking for the card.
  5. The red goes out and the green light blinks EVENLY, about once a
     second. The screen goes dark for a moment, then text scrolls (a lot
     of it on this card), then Apotheca comes up.
        The system is starting. Give it 30 seconds the first time (it makes
        the APOTHECA drive then): do not switch it off.
  6. Press any button: the green light stops blinking and stays on.
        The handheld now counts this screen as seen working.

IF IT STOPS ON THE WAY, the lights say where:

  GREEN ALONE blinking evenly about once a second, from the start, never
  any red
        The first program cannot set this handheld's memory up, and tries
        again for ever. THIS CARD IS FOR THE OTHER KIND OF MEMORY
        (memory-kind.txt on this drive says which kind it is for): use the
        other card image. Hold the power key to turn it off.
  green and red blinking together
        The memory is set up but the boot loader does not start.
  red alone, staying
        The boot loader runs but Linux does not start.
  green blinking in pairs, no red, for a minute; then it starts again by
  itself
        Linux started but stopped before its first program. It resets
        itself after 60 seconds so the boot loader can save what Linux
        said. After three such resets it stops trying: it turns off, or
        the red light blinks evenly until you hold the power key.
  green flickering fast, red on
        Linux sees no card. It resets itself after about a minute, too.
  green blinking evenly AFTER the steps above, the screen dark or garbage
        the system runs but the screen's kind is wrong: make
        screen-first.txt or screen-v2.txt (above). It turns itself off
        after 5 minutes with no button.

THEN PUT THE CARD IN THE COMPUTER AND SEND EVERY .txt FILE from the top of
this drive, whatever is there:

  uboot-started.txt       the boot loader's first act at every start
  uboot-report.txt        the boot loader, at every start: what it read,
                          which tree it chose, how many starts and resets
  kernel-from-memory.txt  what Linux said in the start BEFORE, found in
                          memory by the boot loader after a reset
  kernel-log.txt          Linux's first program: what Linux said, the
                          cards it sees, how far it got
  linux-early.txt         the system's start-up, step by step
  linux-report.txt        the full boot log, once the system is up
  kernel-from-card.txt    see the next paragraph

IF A HANDHELD LEFT NO NEW FILE AT ALL: copy what is here first, then put
this card in a handheld that DOES start with it, start it once, wait a
minute, turn it off, and look again. The boot loader also writes what it
finds in memory onto a hidden part of the card, and a start that works
turns that into kernel-from-card.txt.

THE HANDHELD NEVER STAYS ON BEHIND A DARK SCREEN. Until a button has been
pressed once with this screen, it turns itself off after 5 minutes with no
button. If Linux finds no screen at all it turns off after a minute. A
start that cannot go on resets itself, three times at most.

FAINT VERTICAL LINES ON THE NEWER SCREEN: four things to try, one at a
time, each already named dtb.img in its own folder under device_trees/try/.
Copy that dtb.img to the top of this drive (replace), power on, look at a
dark picture, and note which, if any, takes the lines away:

  1-pins-40ma         the screen's 28 data lines driven harder (as stock)
  2-backlight-50khz   the backlight's flicker at 50 kHz (as stock; 25 now)
  3-dither            18-bit colour with dithering (as stock)
  4-all-as-stock      all three, and the stock clock edge and timings

To go back: delete dtb.img from the top of this drive.

A screen change sent to you as a file (a new dtb.img) is put in the same
way: copy it to the top of this drive as dtb.img. The screen's start-up
settings can be inside it, so no new card is needed.

Nothing else here needs changing. The songs and the boot logs are on
the other partition, APOTHECA.

To turn the handheld off: SEL + START in Apotheca, or hold the power
key for 2 seconds. Either way the song is saved first. A short press
of the power key turns the screen off (the song plays on, the buttons
do nothing) and on again. At 5% battery it saves and turns itself off.

A charger plugged in while the handheld is off STARTS it (since
2026-11-17). Turned off again it should go on charging (not yet tried).
