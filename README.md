# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

Skeleton. Twelve tracks, each with two effect slots, into a master with two
effect slots. The sound source is a stand-in sine tone until the sampler
exists. The screen shows one level meter per track.

## Build

Needs CMake, a C++17 compiler and SDL2 (`brew install cmake sdl2` on macOS,
`apt install cmake libsdl2-dev` on Debian/Ubuntu).

```
cmake -B build
cmake --build build
ctest --test-dir build      # engine tests
./build/apotheca            # the app
```

## Controls

**The handheld comes first.** Every action is defined on the Anbernic
RG40XXV's own buttons, named as printed on it. The keyboard is only a
stand-in for testing at a desk: each key presses one handheld button, so the
keyboard can do nothing the handheld cannot.

What the skeleton app does today:

| Handheld | Does |
| --- | --- |
| D-pad up / down | Pick a track |
| A B X Y | Play a note on it |
| Right stick up / down | Set the track's level (the brass tick) |
| SELECT + START | Quit |

Keyboard stand-ins:

| Handheld | Key | | Handheld | Key |
| --- | --- | --- | --- | --- |
| D-pad | Arrow keys | | L1 / R1 | Q / W |
| A | X | | L2 / R2 | 1 / 2 |
| B | Z | | START | Return |
| X | S | | SELECT | Backspace |
| Y | A | | MENU | Tab |
| Right stick | I J K L | | (quit, desk only) | Esc |

The button code lives in `app/input.hpp`. Nothing else in the app may read a
key or a gamepad button directly.

## Build for the handheld

`cmake/toolchain-aarch64.cmake` cross-compiles for the handheld's chip. The
engine and its tests build with a stock ARM compiler. The app also needs the
firmware's own SDL2; see the comments in that file.

## Layout

```
engine/   the audio engine. No SDL, no device code, so it tests on its own.
app/      window, audio device, buttons. Talks to the engine through commands.
tests/    engine tests.
cmake/    cross-compile settings.
```

## Rules

- **Nothing in the audio thread may wait.** No allocation, locks, files or
  printing inside `Engine::render()` or any `Effect::process()`.
- **The screen thread talks to the engine only through `Engine::send()`.**
- **The engine never includes SDL.**
- **Handheld first.** A feature is designed on the handheld's buttons. A
  keyboard key only ever stands in for one of them.
