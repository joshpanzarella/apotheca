# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

Skeleton. Eight tracks, each with two effect slots, into a master with two
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

| Control | Keyboard | Gamepad |
| --- | --- | --- |
| Pick a track | Up / Down | D-pad up / down |
| Play a note on it | Z X C V B N M , | A B X Y |
| Quit | Esc | |

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
