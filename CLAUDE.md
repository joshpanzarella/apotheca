# Apotheca: notes for Claude

A pocket music workstation for the Anbernic RG40XXV (640x480, buttons and one
stick, Linux aarch64, stock firmware). Part of the Shapetaker family. The
manual is `docs/manual.md`; this file is the rules that bite when changing the code.

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

## The buttons' standards (from the UI audit)

New screens and gestures keep these; do not invent a variant.

- **A** places, opens, picks. **Hold A + a direction** shapes or turns what is under the cursor.
- **B** deletes, empties, goes back. **Hold B deletes for good** (with a bar that fills): never another button. Typing a name, B keeps it and leaves; Y deletes a letter.
- **X** hears (on SONG it solos the track: owner, 2026-11-15). **Hold X** is "what it does": step effects on a drum hit, ratchet and glide on a note.
- **Hold R1** drags: paint from an empty step, carry from a note, copy on SONG. R1 tapped on a scene's REPEATS box copies the whole scene; tapped on another row, it pastes.
- **Hold L1 on the grids** (drum grid, slices, roll) is the bar key: **+ R1 copies the bar, + A pastes it**.
  So L1's own tap (the slices, the roll's zoom) acts on its LET GO, and only if nothing else was pressed meanwhile.
- **Y** is the screen's special (chance, chord, mute, catch, roll).
- **SELECT + something** is for whole-song things. **L2 / R2** are the tabs.
- **A held D-pad has two speeds**, in `ui.hpp`: `kCursor...` for cursors, `kList...` for lists and choices.
- **One word: "delete".** Not erase, remove or rub out. A cell is "emptied".
- **What cannot be undone says so** where it is confirmed.

## Build, test, ship

| Job | Command |
| --- | --- |
| Build | `cmake -S . -B build && cmake --build build -j8` |
| All tests (83) | `cd build && ctest -j8` (about 10 min at -j3 on a busy desk; use the FULL path to `build`: a bare `cd build` once landed in another checkout; `nextscene` is the longest; `effects_test` runs as `effects1` and `effects2`, `idle_test` as `idle1` to `idle5`; `textsize`, `textviews` and `themeviews` each run by themselves) |
| Timings | `./build/bench`, or `apotheca --bench`. On the handheld: SPEED TEST on the WiFi page (the timings are in the program, `app/bench_run.cpp`, so an update over WiFi brings them). `./build/bench --profile NAME [SECONDS]` renders one setup with no clock for callgrind (`--toggle-collect='*profiledRender*'`; NAME empty lists them; `docs/plans/speed-plan.md`) |
| A still of a screen | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --shot out.bmp` (every NAME: `docs/dev/stills.md`; `--text-size xl`, `--theme solarized`, `--hold-a`, `--frames DIR` to keep every frame) |
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


## Developer notes: `docs/dev/`

**This file is an index. The rules, the measurements and the owner's calls live in `docs/dev/`, word for word as they
were here (moved 2026-11-24, when this file had grown to 3,139 lines); read the file before you change what it covers.**

| File | Covers |
| --- | --- |
| `docs/dev/layout.md` | where things are in `app/` and `engine/`; **the steps to add an instrument or an effect** |
| `docs/dev/files.md` | song files, presets, ids, built-in data, JAX-EP's bank, the pinned old five, effect families and TYPE |
| `docs/dev/engine.md` | the audio thread, the queue, helpers, buses, THROW, tails, MODs in the engine, JAX-EP's insides, an effect's house rules |
| `docs/dev/screens.md` | the FX screen, SIGNAL, the redraw, hints, SEL, text sizes, themes, the SET page, lights, WiFi joining, FOLLOW |
| `docs/dev/songs-and-undo.md` | the project in memory, undo, the song's text, scenes and NEXT SCENE |
| `docs/dev/instruments.md` | FM-6's pages, ROLL, PHYSIC, ACID, CLOUD, MORPH, Phazed |
| `docs/dev/drums-and-samples.md` | hearing, the sampler, recordings, slices, copy a bar, FEEL, SWING, CHOKE, EDIT, DRUM SYNTH, the picker, THROW, kits |
| `docs/dev/notes-and-mods.md` | note and chord effects, glides, GLIDE FROM, MODs on knobs and the MOD tab |
| `docs/dev/song-screen.md` | mutes, solo, duplicating a track, AT END, the mixer |
| `docs/dev/wifi-and-bounce.md` | the WiFi page, the designer page, renames, bounces |
| `docs/dev/tests.md` | how the tests work and their traps |
| `docs/dev/measured.md` | every timing taken |
| `docs/dev/next.md` | the road map, in the owner's order |
| `docs/dev/stills.md` | every `--view` |

Plans and reviews: `docs/plans/`. The manual: `docs/manual.md`. **A new decision goes in its `docs/dev/` file; this
file gets only a new hard rule.**

## Rules that bite even a small change (each in full in its file)

- **The audio thread never allocates, locks for long, or frees**; what it is done with goes to the trash queue. **Every
  command goes through `Engine::send`**, which can fail. (`engine.md`)
- **Ids are appended, never reordered or reused** (commands, settings, macros, effect kinds and settings, lanes,
  presets' places). An effect has a number, a word and a place, three things. (`files.md`)
- **The five old effects are pinned to the bit** (FILTER, DRIVE, DELAY, REVERB, CRUSH): never touch their arithmetic.
  A new effect glides its settings, calls `soundOnly()` first, looks after its own small numbers, sleeps by the rest
  rule and sounds the same however it is cut into blocks. (`files.md`, `engine.md`)
- **A song with nothing new in it is the same to the bit** (`effects_test --print`): a new feature at rest takes the
  old path. (`engine.md`)
- **Nothing on the app thread holds an `Effect` in a slot**; the WiFi server's thread never touches the `Project`.
- **Anything below zero is not a track** (-1 master, -2 and -3 the buses): test `track >= 0`. (`screens.md`)
- **A colour goes through the theme's ten names**; a new mark is checked on a light theme; **text is checked at XL**;
  tight rows use `fonts.fixed()`. (`screens.md`)
- **The device writes SEL, never SELECT; no maker's or model's name where a player reads; never erase, remove or rub out** (`ctest -R words`).
- **A thing that moves by itself says so to the redraw** and gets a part in `idle_test`. (`screens.md`)
- **Hints must fit** (`ui::gHintsOver`); shorten the words. (`screens.md`)
- **Every setting has an icon** (`app/icons.cpp`, by its words; `testEverySettingHasAnIcon`).
- **Check a command's real exit code** and read the WHOLE ctest failure list; **mutation testing is the standard**; a
  test's temporary folder comes from `tempFolder()`; an `Engine` is 1.4 MB, so at most three on a test's stack.
  (`tests.md`)
- **Say what was not heard or not run on the handheld.** The handheld is 13 to 20 times slower than the desk
  (`measured.md`).

## Rules that bite

### Nothing is closed to change (owner, 2026-11-12)
- **Any instrument may be improved, ported ones too** (Phazed's engine in
  `third_party/easycz`, FM-6's msfa). There was a "copied unchanged; do
  not edit" rule; the owner did not ask for it and it is gone. Never give
  "that code is not ours" as a reason a feature cannot be done.
- What still holds is the usual care: a change to a synth is a change to the
  sound (old songs and presets, the owner's ears), and a file changed under
  `third_party/` gets a line in `third_party/CHANGES.md` (what and why), so a
  fresh copy from where it came from does not lose it without anyone knowing.
