# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

One drum track that plays. Eight lanes with a built-in kit (made in code, no
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
ctest --test-dir build      # engine tests and screen tests
./build/apotheca            # the app
```

## Controls

**The handheld comes first.** Every action is defined on the Anbernic
RG40XXV's own buttons, named as printed on it. The keyboard is only a
stand-in for testing at a desk: each key presses one handheld button, so the
keyboard can do nothing the handheld cannot.

The Pattern screen has one rule, so there is nothing to memorise:

| Handheld | Does |
| --- | --- |
| D-pad or left stick | Move the cursor. Hold either and it runs |
| A | Act on what is under the cursor |
| Hold A + right stick | Turn what is under the cursor. It fills or drains |
| Hold A + D-pad up / down | The same, in exact steps |
| B | Remove what is under the cursor |
| X | Hear the lane |
| START | Play / stop |
| SELECT + START | Quit |

The cursor can sit on three kinds of thing:

| Under the cursor | A | Hold A + right stick |
| --- | --- | --- |
| A step | Places a hit | That hit's level |
| A lane's name (left of step 1) | Plays the drum | That drum's volume |
| The tempo (up from the top lane) | | The tempo |

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
| Right stick | I J K L | | (quit, desk only) | Esc |
| Left stick | T F G H | | | |

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
apotheca/*.ttf      the lettering, and its licence
```

Unzip it into the firmware's ports folder on the SD card, beside the other
ports. If it fails to start, the reason is in `apotheca/apotheca.log`.

The program asks the device for only two things: the C library (version 2.27
or newer) and SDL2. `cmake/toolchain-aarch64.cmake` is a second route, for
building the engine and its tests alone.

## Layout

```
engine/   the audio engine. No SDL, no device code, so it tests on its own.
app/      window, audio device, buttons. Talks to the engine through commands.
tests/    engine tests, and screen tests that press the buttons.
third_party/  stb_truetype, which reads the font (public domain).
cmake/    cross-compile settings.
scripts/  the handheld build and the port launcher.
```

## Rules

- **Nothing in the audio thread may wait.** No allocation, locks, files or
  printing inside `Engine::render()` or any `Effect::process()`.
- **The screen thread talks to the engine only through `Engine::send()`.**
- **The engine never includes SDL.**
- **One button and a stick, not a combination.** New controls follow the
  rule on the Pattern screen: move, A acts, hold A and turn, B removes.
- **Fast.** Held input runs quickly. Nobody has asked for slower.
- **Handheld first.** A feature is designed on the handheld's buttons. A
  keyboard key only ever stands in for one of them.
