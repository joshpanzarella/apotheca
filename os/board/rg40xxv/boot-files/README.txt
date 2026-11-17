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

  1. The power light, green, within a second of the power key.
        The card's first small program ran.
  2. A second light or colour (red; with the green it may look orange),
     and the screen lit but empty.
        The boot loader (U-Boot) ran. It also wrote uboot-report.txt here.
  3. The red goes out and THE GREEN LIGHT BLINKS; the screen goes dark for
     a moment, then text scrolls, then Apotheca comes up.
        Linux is running. Give it 30 seconds the first time (it makes the
        APOTHECA drive then): do not switch it off.
  4. Press any button: the green light stops blinking and stays on.
        The handheld now counts this screen as seen working.

IF IT STOPS ON THE WAY, the last step it reached says where. Then put the
card in the computer and send every .txt file from the top of this drive:

  uboot-report.txt   written by U-Boot at every start: what it read from
                     the handheld and which tree it chose. If a start left
                     no such file, or its count (apotheca_starts) did not
                     go up, U-Boot did not run.
  linux-report.txt   written by Linux a few seconds after it starts, and
                     again 20 seconds later: everything it found.

THE HANDHELD NEVER STAYS ON BEHIND A DARK SCREEN. Until a button has been
pressed once with this screen, it turns itself off after 5 minutes with no
button (the green light blinks all that time). If Linux finds no screen at
all it turns off after a minute. If the screen is dark or garbage but the
green light blinks, the card has the screen's kind wrong: make
screen-first.txt (or screen-v2.txt) as above and start again.

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
