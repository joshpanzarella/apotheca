# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

One drum track that plays, and Phazed, a phase distortion synth, in the
engine (no screen for it yet: `build/render_demo out.wav` renders drums, a
Phazed bass and Phazed chords to a file to listen to). The drums: Eight lanes with a built-in kit (made in code, no
recordings, and built to hit hard), a 16-step pattern, tempo, and the Pattern screen from the
mockups in `docs/screens/`. The engine holds up to twelve tracks, each with
two effect slots, into a master with two; only track 1 has an instrument so
far, and no screen reaches the effects yet.

## Build

Needs CMake, a C++17 compiler and SDL2 (`brew install cmake sdl2` on macOS,
`apt install cmake libsdl2-dev` on Debian/Ubuntu).

```
cmake -B build
cmake --build build
ctest --test-dir build      # engine, synth and screen tests
./build/apotheca            # the app
```

## Controls

**The handheld comes first.** Every action is defined on the Anbernic
RG40XXV's own buttons, named as printed on it. The keyboard is only a
stand-in for testing at a desk: each key presses one handheld button, so the
keyboard can do nothing the handheld cannot.

**The handheld has one stick.** It does two jobs, and which one depends only
on whether a button is held:

| Held | The stick |
| --- | --- |
| Nothing | Moves the cursor (so does the D-pad). Hold it and it runs |
| A | Shapes what is under the cursor |
| Y | Sets how likely it is to play |

On a step, that is three pictures of one block:

| Gesture | Sets | The block |
| --- | --- | --- |
| Hold A, stick up / down | Level | Fills or drains |
| Hold A, stick left / right | Nudge, up to half a step | Slides early or late |
| Hold Y, stick up / down | Chance | Fades toward a ghost |

The stick does one of those per push: whichever way it leaves the centre is
the job until it comes back.

Painting, for a row of hits in one sweep. Hold a shoulder button and drag
the cursor; every step it crosses gets a hit:

| Held | Paints |
| --- | --- |
| R1 | Every hit at one level: the level of the hit the sweep started on, or 80% from an empty step |
| L1 | Every hit at a random level, 25% to 100% |

Dragging over hits that are already there re-levels them and keeps their
nudge and chance.

Lane dice, for a whole pattern in a second. With the cursor on a lane's
name:

| Gesture | Does |
| --- | --- |
| Hold Y, stick up | Hits arrive on that lane one after another, in a shuffled order that leans toward the beat |
| Hold Y, stick down | They leave again, last in first out |
| Tap Y | Reshuffle: the same number of hits, somewhere new. On an empty lane, four hits |

The dice only remove hits they placed. A hit you placed, or one of theirs
that you have since shaped, painted or given a chance, is yours and stays.

| Tap | Does |
| --- | --- |
| A | Place a hit |
| Y | Place a "maybe" hit (50%). On a hit: chance 100 > 75 > 50 > 25 |
| B | Delete the hit |
| X | Hear the lane |
| START | Play / stop |
| SELECT + START | Quit |

The cursor can also sit on a lane's name, left of step 1 (A plays the drum,
hold A + stick is that drum's volume), and on the tempo, up from the top
lane (hold A + stick). With A or Y held, the D-pad sets the same values in
exact steps.

How fast everything moves is set in one place, `Feel` in
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
app/      window, audio device, buttons. Talks to the engine through commands.
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
- **The synth is called Phazed.** That is the only name a player sees. Its
  engine lives in `third_party/easycz` under its own names and licences;
  anything from it that reaches the screen goes through
  `engine/include/apotheca/phazed.hpp`.
- **Handheld first.** A feature is designed on the handheld's buttons. A
  keyboard key only ever stands in for one of them.
