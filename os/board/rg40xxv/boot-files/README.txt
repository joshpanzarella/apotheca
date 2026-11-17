APOTHECA SYSTEM - BOOT PARTITION

The RG40XXV has had two screens. THE CARD FINDS WHICH ONE THIS HANDHELD
HAS BY ITSELF (the way Anbernic's own system does) and starts with the
matching device tree, the description of the handheld's parts:

  device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb   the newer screen
  device_trees/sun50i-h700-anbernic-rg40xx-v.dtb            the first screen

A file named dtb.img at the top of this drive overrides that: when there
is one, it is used whatever the screen. There is none on a new card
(dtb-in-use.txt says "auto"). To take an override away, delete dtb.img.

A BLACK SCREEN, OR A MOMENT OF COLOURED STRIPES, WHEN IT STARTS IS NORMAL.
The screen shows stripes when it has power but no start-up commands; the
boot loader turns it off within about a second, and Linux sets it up a few
seconds later. Then text scrolls, then Apotheca comes up. Give it 30 seconds before deciding it
did not start, and do not switch it off during the first start (it
makes the APOTHECA drive then).

If the screen stays black or striped for a minute, or the picture is
garbage, the card may have guessed the screen wrong: copy the other tree
from device_trees/ to the top of this drive and rename the copy to
dtb.img, put the card back and power on. (The third,
sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb, is the newer screen with
the pixel clock edge and timings Anbernic's own system uses.)

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
