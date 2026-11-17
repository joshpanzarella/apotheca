APOTHECA SYSTEM - BOOT PARTITION

dtb.img is the device tree in use: the description of the handheld's
parts (screen, buttons, sound) the system starts with. It is a copy of
one of the files in device_trees/:

  sun50i-h700-anbernic-rg40xx-v.dtb            the first RG40XXV screen
  sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb   later units ("v2" panel)

dtb-in-use.txt says which one this card was made with. If the screen stays black or
shows garbage, copy the other over dtb.img (same name), put the card
back and power on. This is the same choice ROCKNIX makes you take.

Nothing else here needs changing. The songs and the boot logs are on
the other partition, APOTHECA.

To turn the handheld off: SEL + START in Apotheca, or hold the power
key for 2 seconds. Either way the song is saved first. A short press
of the power key turns the screen off (the song plays on, the buttons
do nothing) and on again. At 5% battery it saves and turns itself off.
