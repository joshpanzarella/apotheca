# The screen's brightness and the light round the stick

Owner, 2026-11-17: "we have to take control of things like the screen brightness and the RGB
LED around the stick, otherwise, there is no interface to them." On the stock firmware the
system owned both; on our own (os/, firmware-plan.md sections 8 to 10) nothing did.

**Built on branch `lights` (2026-11-17). Desk only: nobody has seen the ring light, and no
brightness has been looked at on the handheld.**

## 1. What the hardware is (read, not guessed)

| Part | Where | What it takes |
| --- | --- | --- |
| The screen | `/sys/class/backlight/backlight` (the owner's boot log: 1250 of 2499 at boot) | `brightness` 0..`max_brightness`; `bl_power` 0 on, 4 off. A PWM backlight (patch 0010) |
| The ring's power | `/sys/class/leds/rgb:kbd_backlight` | **A plain GPIO (PI7) under `gpio-leds`**: `brightness` 0 or 1, `max_brightness` 1. The name is the kernel's own (`color:function`, no `label` in the tree). Not multicolour, no `multi_intensity` |
| The ring's colours | UART5, `5001400.serial`, **`/dev/ttyS2`** on our kernel (the boot log: ttyS0 0x5000000, ttyS1 0x5000400 = Bluetooth, ttyS2 0x5001400) | 115200 baud, a frame of 51 bytes: `1` (the mode), the level 0..255, 16 x R G B, the sum of the 50 bytes & 0xFF |

The GPIO alone is on/off only: **the colours go through a small controller on a serial line**.
That is why patch 0153 ("enable rgb leds") enables UART5. The frame is ROCKNIX's
(`projects/ROCKNIX/packages/hardware/quirks/devices/Anbernic RG40XX V/bin/analog_sticks_ledcontrol`
at the pinned commit `ad9e48d1`): it turns the GPIO on, sets the line raw at 115200, and sends
mode 1, a brightness, then one colour sixteen times. Nothing else of the controller is known: its
other modes, whether it wants R G B in that order, which LED is which, and whether it needs time
after its power comes on.

Two more LEDs exist and are left alone: `green:power` (PI12, on at boot) and `red:status` (PI11;
patch 0901 made it red).

## 2. What was built

- **`app/lights.hpp` / `lights.cpp`** (no SDL): the rules (`backlightFor`, `ringLevelFor`,
  `ringPicture`, `ringBytes`) and `Lights`, which writes the files. It writes the backlight only
  when the number changes, the ring's power only when on/off changes, and a frame only when it
  differs (and again 150 ms and 1 s after the power comes on, in case the controller was not
  listening). Never `bl_power`. The line is opened non-blocking: a frame that does not fit goes
  on a later pass.
- **The system says what is the program's** (`os/.../usr/libexec/apotheca/run`):
  `APOTHECA_OWNS_SCREEN=1` when there is a backlight; `APOTHECA_STICK_LIGHT=/dev/ttyS2` when the
  ring's power switch is there and a `ttyS*` sits on `5001400.serial` (found by address, not by
  number). The port's launcher sets neither: on the stock firmware nothing is written and the
  rows are not shown.
- **SET: BRIGHTNESS and STICK LIGHT**, after VOLUME, shown only where they are here. The page
  now counts PLACES (a row's place among those here: `SettingsScreen::here`, `placeOf`, `rowAt`),
  so the cursor and the twelve-row window pass the missing rows by. 15 rows on our firmware.
- **`settings.txt`**: `brightness N` and `light N`, appended after `theme`; out-of-range lines
  are not taken.
- **MENU + a volume key**: the brightness, ten at a time, shown in the volume's box. The help
  card goes while MENU is used so, and comes back on the next MENU.
- **The power key's tap darkens the ring too**: `bl_power` is read every 500 ms (only where the
  ring is ours).
- Stills `setup-lights`, `setup-light`, `brightness`; `tests/lights_test.cpp`; an `idle_test`
  part for the box; `textsize` holds both rows' words at every size; the boot log lists
  `/sys/class/leds` and each `ttyS*`'s device; `check-image.sh` checks the two exports.

Apotheca runs as root on our firmware (inittab, no user), so no file needed another owner or mode.

## 3. The owner's calls (each at its pick; one constant or one place each)

1. **Quick brightness: MENU + a volume key.** Pick: yes. It takes none of the buttons'
   standards (the volume keys are not among them; SELECT is whole-song things; MENU alone is
   still the help card). Other: SET only.
2. **The range: 10% to 100% in fives; usual 70%** (`kBrightLeast`, `kBrightUsual`). The number
   is the square of the percent (eyes see a light that way): 70% is 1225 of 2499, about where the
   system starts. Pick: as built. **10% (25 of 2499) is not known to be visible**: if it is
   black, raise `kBrightLeast`.
3. **STICK LIGHT's choices: OFF, ON, BEAT, STEPS; usual ON.** STEPS uses the sixteen LEDs as the
   bar's sixteen steps. Pick: as built. Other: OFF as the usual (battery), a colour of its own
   instead of the theme's.
4. **The ring dims with BRIGHTNESS** (`ringLevelFor`: the same square, 255 at 100%). Pick: yes.
   Other: a level of its own (a third row). **Reversed 2026-11-19: RING LEVEL, its own row, 70 until
   turned (owner: a control for the ring's brightness).**
5. **The colour is the theme's cursor colour (teal's job), the bar's first step in its brass**,
   each made as bright as it goes (`fullest`: PAPER's dark teal is a full teal on the ring). The
   LEDs not lit in BEAT and STEPS are `kRingDim` 40/256 of it. Pick: as built.
6. **The power key's tap turns the ring off with the screen.** Pick: yes (a pocket).
7. **STEPS starts at LED 0 and goes up** (`kRingStart` 0, `kRingTurn` 1). Unknown on the
   handheld: the owner says where the first step lights and which way it goes.
8. **The two rows are hidden on the stock firmware**, not shown as NOT HERE. Pick: hidden.

## 4. Not done, not known

- Nothing here has run on a handheld. The firmware image was not built (another job rebuilds it).
- The controller's colour order and LED order, and whether it misses the first frame after power
  (the two resends are a guess at the cure).
- No light on the stock firmware's port (the OS has it there).

## Since (easywins batch 2b, 2026-11-17)

The owner saw ON and BEAT work. STEPS now goes round once a SCENE, and a LIGHT COLOUR row was added:
see `docs/plans/easywins-plan.md`, "Batch 2b".
