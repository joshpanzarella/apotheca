# Apotheca: notes for Claude

A pocket music workstation for the Anbernic RG40XXV (640x480, buttons and one
stick, Linux aarch64, stock firmware). Part of the Shapetaker family. The
manual is `README.md`; this file is the rules that bite when changing the code.

## How to write to me

Applies to every reply. Not optional.

- **Answer first.** First sentence = the answer. Context after, if needed.
- **Short.** Short sentences, short paragraphs, headers or bullets to skim.
- **Plain words.** "used" not "leveraged". No "delve", "robust", "elegant",
  "powerful", "crucial".
- **No fluff.** No preamble, no restating my question, no congratulations.
- **Explain like I'm new to it.** Define a term inline the first time. A small
  table or a before/after beats a paragraph.
- **Bold the part that matters.**
- **Bad news plainly**, in the first line. Say what was not done or not tested.
- **Don't pad the ending.**

Words on the device follow the same taste: plain, short, upper case. "delete",
never "rub out".

## What the owner wants from every change

- **Speed of the workflow comes first.** Every action is judged by how fast
  and easy it is on the handheld. If it is slow or awkward, it gets fixed.
- **Stick for big moves, D-pad for exact ones.** Choices (a filter type) click
  once per push; they are not sliders.
- **The look is settled.** Clean, the same layout. Do not restyle.
- **Scenes, not a timeline. Up to 12 tracks. Chance and dice are welcome.**
- **I cannot hear.** Nothing is "verified" on the handheld unless the owner
  says so. Say which things only the tests and stills have seen.

## Build, test, ship

| Job | Command |
| --- | --- |
| Build | `cmake -S . -B build && cmake --build build -j8` |
| All tests (15) | `cd build && ctest -j8` (about 10 s) |
| Timings | `./build/bench` |
| A still of a screen | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --shot out.bmp` (views are listed in `app/main.cpp`) |
| Handheld build | `scripts/build-handheld.sh` (clang cross-build against an Ubuntu 20.04 arm64 sysroot) |

- **Shipping a build**: run the handheld build, copy `Apotheca-port.zip`,
  `port/apotheca/apotheca` and `VERSION` from `build-handheld/` into `dist/`,
  commit, push to `main`. The owner updates over WiFi or from the SD card.
- **The version is `0.N`**, N = commits + 1 at build time. Only
  `app/version.cpp` is compiled with it, so a new number rebuilds one file.
- **Check a command's real exit code.** `ctest | tail` returns tail's. Use
  `set -o pipefail`. A commit went out past a failing test once this way.
- **Restoring a file with `mv` keeps its old date** and the build then skips
  it. `touch` it, or write the content back.

## Layout

```
engine/   the sound engine: a static library, no SDL. Instruments, effects, the step clock.
app/      the SDL2 app: screens, the Session, files, WiFi page, MIDI.
tests/    one program per area. CHECK macros, no framework.
tools/    bench.cpp (timings), render_demo.cpp
third_party/  easycz (Phazed's engine), shapetaker/entanglement (QMOD's generator), signalsmith (stretch). Copied unchanged; do not edit.
dist/     the built handheld program, kept in git so the owner can fetch it
```

Where things are in the app:

| File | Holds |
| --- | --- |
| `instruments.hpp/.cpp` | **the one table of instruments**: names, song-file word, engine kind, preset ending, list order |
| `synth_spec.cpp` | per synth: presets, settings, macro names |
| `screen.hpp` | the `Screen` base every screen derives from |
| `app.cpp` | `App::screenNow()` picks the screen; SELECT/MENU/shoulder combos live here |
| `session.cpp` | playing, tracks, patterns, mutes, undo, automation, effects, sending the song to the engine |
| `session_songs.cpp` | songs on disk, revisions, settings files |
| `session_synth.cpp` | sounds, presets, the designer page, USB keyboard |
| `session_samples.cpp` | recordings, fitting to tempo, the WiFi page, updates |

**Adding an instrument**: a `TrackKind` (project.hpp), a row in
`instruments.cpp`, a `SynthKind` and class in the engine, a `SynthSpec` if it
has presets and macros, and its DEVICE screen in `App::screenNow()`.

## Rules that bite

### Files and compatibility
- **Song files must keep loading.** Words in a song file (`track phazed`,
  `auto`, `steps`, `name`) never change meaning. Unknown lines are skipped.
- **Ids are appended, never reordered or reused**: `Command::Type`, every
  synth's settings and macros, effect kinds, effect settings, automation lanes
  (0..7 macros, `8 + slot * 4 + setting` effects). Songs and presets store
  them by number.
- **Every synth has `kSynthMacros` (8) macros.** Each synth header checks it.
- **The FILTER type order is fixed** (`int(v * 5)`: low 12, low 24, band, DJ,
  high). It was chosen so old songs' three types still land right.

### The engine and its threads
- **The audio thread never allocates, locks for long, or frees.** Things it
  is done with go to the trash queue; `collectGarbage()` frees them.
- **Commands go through a queue that can fill.** `Engine::send` returns false
  and counts the loss (`dropped()`). `Session::update` notices and sends the
  whole song again (`applyProject`). Callers that hand over a pointer must
  check the return themselves.
- **A test that never renders never empties the queue.** Render alongside
  frames (`rig.frames(n); rig.render(...)`), or commands pile up and drop.
- **Helper threads** (`useWorkers`): tracks are rendered side by side and
  mixed in track order, so the result is the same to the last bit with any
  number of helpers. **Anything a helper waits on is set with `wakeMutex_`
  held.** Setting the stop flag without it hung the quit.
- **The built-in drum kit is built once per sample rate** and shared
  (`DrumKit::builtIn`). It never changes after that.
- **The MIDI thread only uses `sendLive`** (notes and drum hits, its own queue).

### The app
- **`Project` is 8.6 MB. Never put one on the stack**, nor a `TrackData`
  (717 KB). `std::unique_ptr<Project>`.
- **Every screen's `update()` re-reads held buttons from `input` each frame**
  and drops its own "held" flags when they are not held. Trusting the release
  event left A "stuck down" after a screen change.
- **Undo is a text snapshot of the patterns** (`songText`), one step per burst
  of edits, 20 deep. Anything that changes a pattern outside the grids calls
  `noteEdit()`. Sounds, effects and track deletes are not undone.
- **Hints must fit.** The hints bar drops what does not fit; do not rely on
  that, shorten the words.
- **Sample names from `wavNames` have no `.wav` ending.**
- **Mute is its own gate** (`SetTrackHeard`), not the track level.
- **The WiFi page is PIN-gated and checks every file.** A program update must
  be accepted on the device (hold A).
- **Do not pipe the app's output into `head`** when making a still; it dies
  before it saves.

### Tests
- **Mutation testing is the standard.** After a test passes, break the code it
  covers and see it fail. A surviving mutant usually means the fixture cannot
  reach the branch.
- **Do not compare audio sample for sample across separate notes** unless the
  engine is fresh; compare loudness, attack, or zero crossings. Let auditions
  end before listening.
- **Every test has a two-minute limit**, so a hang is a failure.
- When replacing a function by text range, bound it by **the next function in
  the file**, then count the functions before and after.

## Measured (desk machine; the handheld is roughly a fifth as fast)

| Thing | Cost |
| --- | --- |
| Phazed, 8 notes | 0.9% of a core |
| QMOD, 8 notes | 2.6% |
| Twin Analog x1 / x2 / x8, 8 notes | 4.7% / 9.1% / 34% |
| Drums, 8 lanes | 0.1% |
| One idle frame | 0.03 ms |
| A frame that ends an edit (undo snapshot) | 1 ms |
| Undo plus redo | 20 ms |

**Twin Analog is the costly one**, and its time is spread over plain
arithmetic with no single hot spot. Do not "optimise" it blind: any change
there is a change to the sound, and needs the owner's ears.

## Next, in the owner's order

1. Slice view for chopping breaks.
2. Step effects, tracker style (ratchet, reverse, pitch, cut, glide, throw).
3. LFO and random stepper per track.

Open: which chord packs ship, more presets, own WiFi hotspot, an automation
step-editor, a picture of QMOD's wiring.
