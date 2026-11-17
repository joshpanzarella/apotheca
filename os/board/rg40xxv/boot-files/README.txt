APOTHECA SYSTEM - BOOT PARTITION

dtb.img is the device tree in use: the description of the handheld's
parts (screen, buttons, sound) the system starts with. It is a copy of
one of the files in device_trees/:

  sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb   the newer screen (later
                                               units; reported from late 2025).
                                               The card starts with this.
  sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb
                                               the newer screen, with the
                                               pixel clock edge and timings
                                               Anbernic's own system uses.
                                               Try it if a dark picture shows
                                               faint vertical stripes.
  sun50i-h700-anbernic-rg40xx-v.dtb            the first RG40XXV screen

dtb-in-use.txt says which one this card was made with.

A BLACK SCREEN, OR A MOMENT OF COLOURED STRIPES, WHEN IT STARTS IS NORMAL.
The screen shows stripes when it has power but no start-up commands; the
boot loader turns it off within about a second, and Linux sets it up a few
seconds later. Then text scrolls, then Apotheca comes up. Give it 30 seconds before deciding it
did not start, and do not switch it off during the first start (it
makes the APOTHECA drive then).

If the stripes stay for a minute, or the picture is garbage, the unit
may have the first screen: copy device_trees/sun50i-h700-anbernic-rg40xx-v.dtb
to the top of this drive, rename the copy to dtb.img (replace the one
there), put the card back and power on. This is the same choice ROCKNIX
makes you take. Any of the three is tried the same way.

FAINT VERTICAL LINES ON THE NEWER SCREEN: four things to try, one at a
time, each already named dtb.img in its own folder under device_trees/try/.
Copy that dtb.img to the top of this drive (replace), power on, look at a
dark picture, and note which, if any, takes the lines away:

  1-pins-40ma         the screen's 28 data lines driven harder (as stock)
  2-backlight-50khz   the backlight's flicker at 50 kHz (as stock; 25 now)
  3-dither            18-bit colour with dithering (as stock)
  4-all-as-stock      all three, and the stock clock edge and timings

To go back: copy device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb
over dtb.img (renaming the copy dtb.img).

A screen change sent to you as a file (a new dtb.img) is put in the same
way: copy it to the top of this drive over dtb.img. The screen's start-up
settings can be inside it, so no new card is needed.

Nothing else here needs changing. The songs and the boot logs are on
the other partition, APOTHECA.

To turn the handheld off: SEL + START in Apotheca, or hold the power
key for 2 seconds. Either way the song is saved first. A short press
of the power key turns the screen off (the song plays on, the buttons
do nothing) and on again. At 5% battery it saves and turns itself off.
