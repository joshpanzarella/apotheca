# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

A small song machine that runs, at 48 kHz. Up to twelve tracks, each a drum
kit or one of two synths: Phazed (phase distortion, 65 presets) or Twin
Analog (two morphing oscillators, 16 presets). Up to sixteen scenes,
each one bar, each with its own repeat count. Three screens:

- **SONG**: tracks across, scenes down. Add tracks, set how many times each
  scene plays, copy bars by dragging.
- **PATTERN**: a drum grid (levels, nudge, chance, paint, lane dice) or, for
  a synth, a piano roll.
- **DEVICE**: for a synth, a main panel of folder, preset and eight macros,
  with advanced pages behind it (nine for Phazed, three for Twin Analog).
  For drums, the kit's levels and scope.

Not built yet: effects on screen, saving and opening songs, loading samples,
the sound designer web page.

## Build

Needs CMake, a C++17 compiler and SDL2 (`brew install cmake sdl2` on macOS,
`apt install cmake libsdl2-dev` on Debian/Ubuntu).

```
cmake -B build
cmake --build build
ctest --test-dir build      # engine, song, both synths, screen and app tests
./build/apotheca            # the app
```

## Controls

**The handheld comes first.** Every action is defined on the Anbernic
RG40XXV's own buttons, named as printed on it. The keyboard is only a
stand-in for testing at a desk: each key presses one handheld button, so the
keyboard can do nothing the handheld cannot.

**The handheld has one stick, and one rule for it.** With nothing held it
moves the cursor (so does the D-pad; hold either and it runs). Hold a button
and the stick shapes the thing under the cursor:

| Held | The stick |
| --- | --- |
| Nothing | Moves the cursor |
| A | Turns the value: it fills or drains |
| Y | Chance, on the drum grid |

**L2 and R2** walk along the three screens. **START** plays: the song from
the SONG screen, the scene being worked on from the others. **SELECT + START**
quits.

### SONG

| On | A | Hold A + stick | B | Hold R1 and drag |
| --- | --- | --- | --- | --- |
| A cell | Opens it on PATTERN | | Empties it | Copies it into every cell crossed |
| A repeat count | Tap: 1, 2, 4, 8 | Times the scene plays, 1 to 16 | Back to 1 | |
| + TRACK | Pick DRUMS, PHAZED or TWIN ANALOG | | | |

The song runs to the last scene with anything in it, then comes round.

### PATTERN, drums

| Gesture | Sets | The block |
| --- | --- | --- |
| Hold A, stick up / down | Level | Fills or drains |
| Hold A, stick left / right | Nudge, up to half a step | Slides early or late |
| Hold Y, stick up / down | Chance | Fades toward a ghost |

The stick does one of those per push. A places a hit. Y places a "maybe"
(50%), or steps a hit's chance 100, 75, 50, 25. B deletes. X hears the lane.

Hold **R1** and drag to paint hits at one level (the level of the hit you
started on, or 80%). Hold **L1** and drag for random levels.

The cursor can also sit on a lane's name, left of step 1: A plays the drum,
hold A + stick is its volume, and Y is the **lane dice** (hold Y + stick up
and hits arrive in a shuffled order that leans toward the beat; down and
they leave; tap Y to reshuffle). Up from the top lane is the tempo.

### PATTERN, a synth

A piano roll. The drum grid's rules, except that sideways a note has a
length where a hit has a nudge.

| Gesture | Does |
| --- | --- |
| A | Place a note, one step long |
| Hold A, stick up / down | Level |
| Hold A, stick left / right | Length, up to the end of the bar or the next note on that key |
| B | Delete the note under the cursor |
| X | Hear the key |
| Hold R1 and drag | Paint a note on every step crossed |

### DEVICE, a synth

MAIN: folder, preset, eight macros and ADVANCED. Phazed's macros are TONE,
MOTION, ATTACK, RELEASE, DETUNE, VIBRATO, CHORUS, OCTAVE. Twin Analog's are
SHAPE, BLEND, FORMANT, WIDTH, DETUNE, VINTAGE, ATTACK, RELEASE. Hold A + stick turns the one under
the cursor; B puts a macro back in the middle. Macros bend the sound and
leave the preset as it was made. A new preset is played at once when the
song is stopped.

ADVANCED: every setting. Phazed has nine pages (the sound, each line, and
each line's volume, wave and pitch envelopes drawn out). Twin Analog has
three (oscillators, formant and character, envelope). L1 and R1 turn the pages; B
goes back to MAIN.

How fast the drum grid moves is set in one place, `Feel` in
`app/pattern_screen.hpp`.

Keyboard stand-ins:

| Handheld | Key | | Handheld | Key |
| --- | --- | --- | --- | --- |
| D-pad | Arrow keys | | L1 / R1 | Q / W |
| A | X | | L2 / R2 | 1 / 2 |
| B | Z | | START | Return |
| X | S | | SELECT | Backspace |
| Y | A | | MENU | Tab |
| The stick | I J K L | | (quit, desk only) | Esc |

The button code lives in `app/input.hpp`. Nothing else in the app may read a
key or a gamepad button directly.

## Build for the handheld

```
scripts/build-handheld.sh
```

Runs on Linux (needs clang, lld, curl, dpkg-deb, zip). It writes
`build-handheld/Apotheca-port.zip`:

```
Apotheca.sh         the launcher the Ports menu shows
apotheca/apotheca   the program
apotheca/*.ttf      the lettering
apotheca/licences/  licences of the code and lettering built in
```

Unzip it into the firmware's ports folder on the SD card, beside the other
ports. If it fails to start, the reason is in `apotheca/apotheca.log`.

The program asks the device for only two things: the C library (version 2.29
or newer) and SDL2. `cmake/toolchain-aarch64.cmake` is a second route, for
building the engine and its tests alone.

## Layout

```
engine/   the audio engine. No SDL, no device code, so it tests on its own.
app/      window, audio device, buttons, and the screens. Talks to the
          engine through commands. app.hpp ties the screens together.
tests/    engine tests, and screen tests that press the buttons.
third_party/  stb_truetype, which reads the font (public domain), and
          easycz, the engine behind Phazed, copied unchanged from
          joshpanzarella/schwung-easycz (MIT, with a BSD-3-Clause chip
          emulation from MAME).
tools/    render_demo, which renders a short piece to a WAV file.
cmake/    cross-compile settings.
scripts/  the handheld build and the port launcher.
```

## Rules

- **Nothing in the audio thread may wait.** No allocation, locks, files or
  printing inside `Engine::render()` or any `Effect::process()`.
- **The screen thread talks to the engine only through `Engine::send()`.**
- **The engine never includes SDL.**
- **One button and the stick, not a combination.** New controls follow the
  rule on the Pattern screen: the stick moves; hold a button and it shapes.
- **Fast.** Held input runs quickly. Nobody has asked for slower.
- **Twin Analog is a port of Clairaudient**, the oscillator in the Shapetaker
  Rack plugin (`src/clairaudient.cpp` there). `engine/src/twin.cpp` restates
  its sound-making code without Rack and lists what changed in the move.
  The four oscillator copies are computed in one branch-free loop of four:
  measured with clang, that halves the synth's cost. Keep it branch-free.
- **The first synth is called Phazed.** That is the only name a player sees. Its
  engine lives in `third_party/easycz` under its own names and licences;
  anything from it that reaches the screen goes through
  `engine/include/apotheca/phazed.hpp`.
- **Handheld first.** A feature is designed on the handheld's buttons. A
  keyboard key only ever stands in for one of them.
