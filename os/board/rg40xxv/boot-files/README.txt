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

THE RG40XXV HAS ALSO HAD TWO KINDS OF MEMORY (LPDDR4 and LPDDR3), and
nothing on the outside says which. This card finds that by itself too
(memory-kind.txt on this drive says "both"). It tries LPDDR4 first, at the
lower voltage, which harms neither kind; if that does not work it tries
LPDDR3. It remembers the answer in the handheld's clock, so later starts
go straight to the right kind.

To make it try a kind FIRST at the next start, make an empty file named
dram-lpddr3.txt or dram-lpddr4.txt at the top of this drive, start the
handheld once, and delete the file. This only works on a handheld that
starts: the file is read after the memory is set up. For a handheld that
will not start with this card at all there are two more card images, one
for each kind only.

WHAT YOU SEE WHEN IT STARTS, AND WHAT IT MEANS

  1. Green, then green and red together, within a second or two of the
     power key (together they may look like one orange light). On a
     handheld with LPDDR3 memory the green may go out and come back once
     before the red joins it, the first time or every time.
        The card's first small program ran and set the memory up.
  2. RED ALONE, and the screen lit but empty.
        The boot loader (U-Boot) is running.
  3. The red goes out and the green light blinks in PAIRS (blink-blink,
     pause): Linux has started.
  4. The red comes on again for a moment (often too short to see): Linux's
     first program is looking for the card.
  5. The red goes out and the green light blinks EVENLY, about once a
     second. The screen says APOTHECA STARTING, then Apotheca comes up.
        The system is starting. The very first time it also makes the
        APOTHECA drive: do not switch it off.
  6. Press any button: the green light stops blinking and stays on.
        The handheld now counts this screen as seen working.

IF IT STOPS ON THE WAY, the lights say where:

  GREEN ALONE blinking evenly, from the start, never any red, for more
  than ten seconds
        The first program cannot set this handheld's memory up as either
        kind, and tries again for ever. Hold the power key to turn it off,
        and try the card image made for one kind only (LPDDR3, then
        LPDDR4). (On a card whose memory-kind.txt names one kind, this
        light means the handheld has the other kind.)
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
                          which memory was found (apotheca_dram), which
                          tree it chose, how many starts and resets
  kernel-from-memory.txt  what Linux said in the start BEFORE, found in
                          memory by the boot loader after a reset
  kernel-log.txt          Linux's first program, only when it had to wait
                          for the card or could not go on: what Linux
                          said, the cards it sees, how far it got
  linux-early.txt         the system's start-up, step by step
  linux-report.txt        the full boot log, once the system is up (it
                          says the memory's kind too)
  memory-kind.txt         which memory this card is for (made with the
                          card; it does not change)
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

A charger plugged in while the handheld is off charges it and it STAYS
OFF, as on ROCKNIX. Press the power key to start it.
