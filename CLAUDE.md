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

## The buttons' standards (from the UI audit)

New screens and gestures keep these; do not invent a variant.

- **A** places, opens, picks. **Hold A + a direction** shapes or turns what is under the cursor.
- **B** deletes, empties, goes back. **Hold B deletes for good** (with a bar that fills): never another button. Typing a name, B keeps it and leaves; Y deletes a letter.
- **X** hears (on SONG it solos the track: owner, 2026-11-15). **Hold X** is "what it does": step effects on a drum hit, ratchet and glide on a note.
- **Hold R1** drags: paint from an empty step, carry from a note, copy on SONG.
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
| All tests (66) | `cd build && ctest -j8` (about 100 s; `nextscene` is the longest; `effects_test` runs as `effects1` and `effects2`, `idle_test` as `idle1` to `idle3`; `textsize`, `textviews` and `themeviews` each run by themselves) |
| Timings | `./build/bench`, or `apotheca --bench`. On the handheld: SPEED TEST on the WiFi page (the timings are in the program, `app/bench_run.cpp`, so an update over WiFi brings them). `./build/bench --profile NAME [SECONDS]` renders one setup with no clock for callgrind (`--toggle-collect='*profiledRender*'`; NAME empty lists them; `docs/plans/speed-plan.md`) |
| A still of a screen | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --shot out.bmp` (views are listed in `app/main.cpp`; the FX screen's are `fx`, `fx-type`, `fx-pick`, `fx-comp`, `fx-switch` (COMP, on its second setting; with `--hold-a` the heading shows the key that switches its TYPE), `fx-master`, `fx-duck`, `fx-choice`, `fx-auto`, `fx-sends`, `fx-bus`, `fx-level`, `fx-ping`, `fx-ping-bus`, `fx-spring` (REVERB SPRING on the track and on SPACE), `fx-plate`, `fx-family-1` to `-7` (the last type of each family, FILTER to LEVEL, in the track's second slot; with `--hold-a` the list of effects on its line, the type lit), `fx-kind-WORD` (the effect whose song-file word is WORD in the track's second slot: `fx-kind-comb`; `fx-kind-comb-1` on its first setting), `fx-deleting` (B held on an effect, its bar filling); the list of recordings: `picker` (SAMPLES, on a folder), `picker-in` (in a folder), `picker-deep` (two folders down), `picker-deleting`, `picker-new` (on + NEW FOLDER), `picker-typing` (its name being typed), `picker-carrying` (R1 held, steered onto a folder), `picker-folder-deleting` (B held on an empty folder), `sampler-picker`; the help card: `help` (with SOUND 43%, WORST 88%, CPU 37%: pretended, `App::pretendLoad`), `help-load` (its longest line); the sampler: `sampler` (on START), `sampler-end` (on END; both with START 12% and END 80% marked; the demo's last track becomes the sampler, all twelve being used), each also with `--hold-a` (on a line of the list that opens the list of effects: `fx-pick --hold-a`, `fx-type --hold-a`, `fx-bus --hold-a`); the piano roll's strip at its fullest: `notefx-worst`, `notefx-space`; GLIDE FROM, one a type on the same notes: `notefx-from` (TOP), `notefx-from-paired`, `-nearest`, `-random`, each also with `-held` (X held: the strip says it); every length of glide and its line: `notefx-glides`, `-glides-alone`, `-glides-longest-held`, `-glides-far1` to `-far4` (a FULL glide from two bars back, each bar); the BOUNCE row at work: `bounce`, `bounce-tail`, and at rest: `bounce-ready`; NEXT SCENE just pressed: `next-scene`, `next-scene-notes`, `next-scene-full`; chord effects: `chordfx`, `chordfx-held`, `chordfx-ratchet` / `-strum` / `-order` / `-time` / `-level` / `-lengths` (X held, that one picked), `chordfx-off`, `chordfx-arp`, `chordfx-worst` (the strip at its fullest), `chordfx-line`; a track's other instrument offered: `change-instrument`; JAX-EP's panel and pages: `jaxep`, `jaxep0` to `jaxep5`, `jaxep-bank` (MAIN on B7 Distortion Guitar, in BANK B: the longest of the 32 names), `jaxep-held` (a knob, A held); FM-6's: `fm6` (MAIN), `fm6-1` to `fm6-8` (its pages), `fm6-long` (HARPSICH 1, the widest name), `fm6-fixed` (an operator at a FIXED pitch, on FINE), `fm6-off` (an operator OFF); PHYSIC's: `physic` (MAIN), `physic-1` to `physic-5` (SOUND, RING, PLAY, DICE, BODY: on Vibes, PAIR and BUZZ up), `physic-long` (its widest preset name), `physic-string` (RING on Steel: STRETCH reads B), `physic-noise` (SOUND on Hand Drum: HARDNESS reads a NOISE burst's length); a song being deleted: `song-deleting`; the mixer (A held on a head): `song-mixer`, `song-mixer-master`, each also `-12` (twelve tracks); FEEL on the drum grid: `feel` (lanes with their marks, the cursor on a name), `feel-held` (X held: the strip is the lane's feel), `feel-off-held`, `feel-same-held` (every lane at 100%, the strip at its widest), `feel-said` (L1 on a built-in drum with a feel: the message has the row), `slices-name-feel` (the slice view's name with its mark; needs `--samples`); X held on a knob, its MOD: `device-mod-held` (an LFO on Phazed's TONE), `device-mod-stepper`, `device-mod-new` (none yet), `device-mod-full` (MODS FULL), `device-mod-deleting` (B held), `fx-mod-held` (an LFO on TAPE's FEEDBACK), `fx-mod-stepper`, `fx-mod-new`, `fx-mod-full`, `fx-sends-mod-held` (on ECHO: the panel in SPACE's row); the brass mark with the song playing: `device-modded`, `fx-modded`; the MOD tab: `mod` (an LFO on TONE, a STEPPER on ECHO; the demo plays, so the dots move), `mod-lfo` (on DEPTH), `mod-steps` (MOD 2's row, on step 5), `mod-depth` (MOD 1's DEPTH lane written, the demo playing), `mod-mixer` (LEVEL and PAN as targets), `mod-target` and `mod-drums` (each with `--hold-a`: the list of targets), `mod-off`, `mod-deleting`, `mod-12`, `mod-free` (a free LFO: SPEED in Hz), `device-mod-free`; any view with `--no-menu` is as on a handheld with no MENU button) |
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
third_party/  easycz (Phazed's engine), msfa (FM-6's core), signalsmith (stretch). Copied in, and OPEN TO CHANGE (see the rule below).
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
| `mods.hpp` | a track's two MODs in the app: what one may be aimed at, held, carried by a TYPE change |
| `session_songs.cpp` | songs on disk, revisions, settings files |
| `session_synth.cpp` | sounds, presets, the designer page, USB keyboard |
| `session_samples.cpp` | recordings, fitting to tempo, the WiFi page, updates |

**Adding an instrument**: a `TrackKind` (project.hpp), a row in
`instruments.cpp`, a `SynthKind` and class in the engine, a `SynthSpec` if it
has presets and macros, and its DEVICE screen in `App::screenNow()`. Then the
places that list the synths by name: `userList`, `builtInPresets`,
`settingCount`, `specFor` (synth_spec.cpp), `macroIsChoice` (mods.hpp, for a
threshold macro), its stills in `main.cpp` and `kViews`, and the tests'
lists (`textsize`, `scope_test`, `save_test`, `modapp_test`). CLOUD was the
last added (2026-11-17, batch 2 of granular-plan.md: a roll, and a recording as the sampler's); ACID's
commit is the plainest pattern.

Where effects are, in the engine:

| File | Holds |
| --- | --- |
| `engine/include/apotheca/effects.hpp` | `EffectKind` (the numbers), `EffectInfo` (a row), and everything that reads the table |
| `engine/src/effects.cpp` | **the one table of effects**: each row's number, the list a player picks from, families and types, how a setting is carried to another type |
| `engine/src/fx/NAME.cpp` | one effect: its class, how its settings read, its row |
| `engine/src/fx/unit.hpp` | what the effects share; the steps to add one are at its top |
| `engine/src/fx/glide.hpp` | what the newer effects share: a setting that slides (`Glide`, the one such helper), a delay read between samples, the going-to-sleep rule |
| `engine/src/fx/halfband.hpp` | twice the sample rate and back, for an effect that bends the wave (SATURATE) |
| `engine/src/fx/svf.hpp` | what the newer filters share (COMB, VOWEL, EQ): the state-variable filter, the TICK, `Slide`, `Gentle` |
| `engine/src/fx/mod.hpp` | what MOD's newer types share (FLANGER, ENSEMBLE, PHASER, TREMOLO): CHORUS's RATE scale, a signed amount, a sine from a place in a turn, a delay line read between samples, the 4 s rest rule |
| `engine/src/fx/echo.hpp` | what DELAY's newer types share (DUB, REVERSE, STUTTER): TAPE's eight TIMEs, a crossfade's curve, the 4 s rest |
| `engine/src/fx/tank.hpp` | what REVERB's newer types share (BLOOM, SHIMMER): one tank of eight lines mixed eight into eight, DECAY a real time per line, PLATE's TONE, the sway; BLOOM's 64-tap swell and SHIMMER's octave shifter |
| `engine/src/fx/bend.hpp` | what the newer DRIVE types share (FOLD, FUZZ): a side bent at twice the rate (`BendSide`; FOLD at four, `BendSide4`) a sample at a time (so the TICK holds), back on centre, the plain sound made as late; `bendLevel` |
| `engine/include/apotheca/denormals.hpp` | flush-to-zero |
| `tests/effects_test.cpp` | the pinned sound of the old five, and the tools that drive an effect |
| `tests/measure.hpp` | the measuring tools every sound test shares: level, spectrum (`harmonicsIn`), pitch, checksum, no click |

**Adding an effect**:
1. an `EffectKind` at the **end** of the list in `effects.hpp`;
2. a file `engine/src/fx/NAME.cpp` with its class and its `const EffectInfo kNameInfo`
   (name, settings, defaults, song-file `word`, group, which settings are
   choices, how a setting reads, how to make one, and `says`: a few plain
   words for the list a player picks from);
3. its `extern` line at the bottom of `fx/unit.hpp`;
4. its row in `kTable` in `effects.cpp` and its place in `kList` there;
5. its checks in `tests/effects_test.cpp`, with the tools there.

The screen, the song file and `bench` pick it up from the table; the build
takes every `.cpp` in `engine/src/fx/`. Nothing else is edited.

**Every effect is a TYPE of one of seven families** (owner, 2026-11-13), so
adding a TYPE (COMB to FILTER, FUZZ to DRIVE, BLOOM to REVERB) is adding an
effect, nothing more. Its row names its `family` (`EffectFamily`: FILTER,
DRIVE, CRUSH, MOD, DELAY, REVERB, LEVEL) and, where its name is not what
the type is called, its `type` (DELAY's is "CLEAN"). Put it in `kList`
beside the family's other types: the families in `EffectFamily`'s order,
side by side (`effects_test` fails one out of place), and the first type is
what the family goes in as. The list of effects names it on its family's
line by itself. Name the settings it shares with the other
types as they name them (TIME, FEEDBACK, MIX): changing TYPE carries a
setting over **by its name**. If a shared setting is a choice whose choices
differ (TIME), give the row an `amount` function, so a choice the other does
not have goes to the nearest. `effects_test` checks every pair of every
family by itself; nothing is listed there. A type that shares no name with
another (COMP and DUCK) starts at its usual values.

**A delay or a reverb also goes on a send bus** (by its family: `effectBus`).
Its `process()` must look at `Unit::bus_` and, when set, put out only what it
adds (wet times MIX) and none of what came in. `effects_test` checks every
effect in those two groups and fails one that does not.

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

### Files and compatibility
- **Nothing is released; old songs are owed nothing** (owner, 2026-11-15: "don't
  ever worry about old tracks. this is all in beta"). The rules below keep old
  files loading and old sounds to the bit; they still hold as the house way, but
  where one stands in the way of a better program or a cheaper one, change it,
  and say so. Never keep slow code, an instrument or a format for an old song's
  sake. (QMOD was replaced by FM-6 outright, 2026-11-15: its word read as FM-6, its code gone.)
- **Song files must keep loading.** Words in a song file (`track phazed`,
  `auto`, `steps`, `name`) never change meaning. Unknown lines are skipped.
- **Ids are appended, never reordered or reused**: `Command::Type`, every
  synth's settings and macros, effect kinds, effect settings, automation lanes
  (0..7 macros, `8 + slot * 4 + setting` effects for slots 1 to 3, 20 and 21
  the ECHO and SPACE sends, 22 and 23 spare). Songs and presets store them by
  number.
- **A song file with no `busfx` line is an old song.** Its buses get what a
  new song's do (`kNewSongBus` in `app/project.hpp`: TAPE and HALL), its
  sends are 0, and it sounds as it did. Old songs get the same as new ones on
  purpose: a bus nobody has touched holds the same thing in every song. `songText` always writes both `busfx` lines, an empty bus
  as `busfx 2 none`, so "none" is never mistaken for "old". A track's send is
  a `send BUS AMOUNT` line, left out at 0; `fx 3` is its third slot.
- **A cell made shorter keeps its hidden bars, in the file too.** They are
  written after a `bar K` past the cell's end (`hidesBars` in song_file.cpp);
  a program from before reads no line for a bar the cell does not have, so it
  skips them. A note there may run to where the longest cell would end.
- **A song file is read with Windows line ends too**: `parseSong` takes a
  trailing `\r` off each line (a name ending in one was refused, and every
  recording dropped); `renameRecordings` reads lines the same way and writes
  the `\r` back.
- **A number from a file is held to its limits as a double, before it is
  made an int, a uint32 or a float** (`wholeIn`, `fractionIn`, `seedIn` in
  song_file.cpp). "repeats 1e300" cast first is undefined behaviour, and the
  desk and the handheld read it differently. Anything new read from a file
  goes through them.
- **A song, a rev, a settings file or a preset is written with `writeTextFile`**: `NAME.part`, flushed and
  fsynced, renamed over `NAME`, and the folder fsynced (a flat battery left
  an empty song). `save_test` checks the order on Linux. Its cost on the
  handheld's card is not measured.
- **A deleted song goes to `deleted/` under a name nothing there has** (song
  or rev: "ash well", else "ash well 2"), through `binSong`, which the
  Session and the WiFi page share. Renamed over, the first one's revs paired
  with the second song.
- **Every synth has `kSynthMacros` (8) macros.** Each synth header checks it.
- **The JX-3P clone is JAX-EP wherever a player reads** (owner, 2026-11-14):
  "a virtual analog synth modeled after a popular DCO synth". **No maker's or
  model's name** (Roland, JX-3P, PG-200, Juno) in a text of `app/`, on the two
  web pages or in README.md; `ctest -R words` fails one, with its own plants.
  **It reads the engine's texts for those names too** (preset and folder names
  are there: `kBeside` in `tests/words_test.sh` lists `engine/`'s folders, the
  two copied-in synths under `third_party/` and `tools/`; `.c` and `.h` as
  well). **A new folder of engine sources goes in `kBeside`**, which plants a
  name in each and fails on one with nothing to read. No SELECT rule there:
  the engine draws nothing.
  Code, file names, tests and `docs/plans` keep `jx3p` / `Jx3p`. Its song-file
  word is `jaxep` and its presets are `NAME.jaxep` (`jx3p` was never written
  by anything released and is not read). The list to pick from says "virtual
  analog, after a popular DCO synth" with no count: the line holds about 42
  letters.
- **A JAX-EP knob reads as its dial, 0.0 to 10.0, wherever a player reads
  it**: `settingText` on the handheld, and on the designer page too
  (`SynthSpec::pageReadsAsDevice`: the page is given every reading of every
  setting; for the other synths only a setting of 40 values or fewer). A new
  knob of this synth needs nothing more.
- **MAIN says what is known of a folder's sounds** (`SynthSpec::folderNote`,
  JAX-EP's only): on PRESET's top row, after the word. SPEEDS GUESSED for a
  built-in folder, 1983 BOOKLET, SPEEDS GUESSED for one named "BANK ..."
  (BANK A and BANK B: every built-in sound today, so the short one has no
  folder; `SynthScreen::folderNoteAs` shows it for its test), nothing
  for the player's own. **The count beside it is the preset's place in its
  folder** (owner, 2026-11-15; `SynthScreen::placeInFolder`: B7 reads
  "7 / 16", the player's own count among themselves, every synth alike; a
  folder is the run of presets side by side under one name, as `stepFolder`
  steps them). The row keeps room for the longest count
  (`kCountLongest`), and where the three texts do not fit the size chosen the
  whole row is drawn at LARGE (the bank's line at XL). No maker's name in it.
- **JAX-EP's 32 presets are the booklet's, and the table is not edited by
  hand** (`kPresets`, `engine/src/jx3p.cpp`; batch 7): A1 to B16 in two
  folders, BANK A and BANK B, so a preset's number is the factory's and a
  folder jump is a bank jump. `tests/jx3p_nam.inc` holds the booklet as read
  (twice, and by the pointers' angles), in its own terms; `jx3p_presets_test`
  holds the table to it value for value. **Change the reading and the table
  together, with the scan in hand**, and keep each guess marked (PULSE or
  SQUARE on twelve, B14's METAL, the blanks). The start patch is not a preset
  (`Jx3p::startPatch()`: what a synth is made with, what the engine's tests
  play); a new track and an old `track twin` are on A1.
  - **The cutoff's range was set from the bank, and the VCA is straight (owner,
    2026-11-15).** `kVcfLowHz` 49.84 and
    `kVcfHighHz` 49840 (`jx3p/ir3109.hpp`; they were 15 and 15000: the same
    span times 3.32), held at `kVcfTopHz` 20 kHz as before, so **at key 60
    CUTOFF 8.7 to 10 is one sound**. **The VCA's gain is the dial, straight**
    (`vcaGain`, `jx3p/env.hpp`: LEVEL 5 is half of LEVEL 10), `kVcaUnityDial`
    100 and **`kVcaClipDial` the same: the clip starts at a full wave at LEVEL
    10, so no preset as written reaches it** (owner: "I don't think they should
    distort by default"). The synth's `kOutputGain` is 0.6 (it was 0.3), so the
    bank stayed as loud. It was 4 dB a dial step, 0 dB at LEVEL 7 and then for a
    day at 4.5 with the clip there: A8 and A16 (LEVEL 9.9, 10) were 21 and 18 dB
    into it. **Moving the 0 dB alone cannot do it**: with 4 dB a step, LEVEL 10
    is 22 dB over LEVEL 4.5, so clean and as loud they pass full scale nine
    times over (measured: at 70 with the gain made up, A8 six keys 6.9, still
    10 dB into the clip). The start patch is LEVEL 5.0 (a wave of 0.3 out,
    exactly the gain LEVEL 4.5 gave), the tests' plain patches 2.0, 3.2 and 5.0
    (were 2.5, 3.5, 4.5: the same gain within 0.15 dB). **A plain saw's corners
    overshoot to 1.31 of full**, so a player's own saw clips softly from LEVEL
    7.6; `testVca` holds that. `Jx3p::samplesClipped()` counts the samples the
    clip bent; `testEveryPatchPlays` holds it at 0 for the 32 as written.
    - **A3 Organ I sings the key played**: 0.4 cents from the grid at worst,
      keys 48 to 84, read over five seconds (`testNamsThree`). Its sine sits on
      DCO-1's second harmonic (16') and is held by it: the cutoff 40 cents off
      still sings the key, so the test also holds the cutoff's CV to the key
      within half a cent. Its level sways (DCO-2 is 27 cents flat in the patch
      and beats with the sine, twice a second at key 48): the patch, not a fault.
    - **No patch is all but silent** (`testEveryPatchPlays`, the chord's
      loudest tenth of a second; measured again 2026-11-16 after the
      resonance was mended): B15 Planet -34.0 dB, the quietest; B16 Jet
      -30.3; B4 Song Whistle -28.3; A11 Harpsichord -21.2; the middle of the
      32 -15.9; A1 String I -13.5 (PHAZED's first presets: -15.5 to -12.6);
      A13 Chime -4.6, the loudest (B7 Distortion Guitar -5.7). The low end
      given back moved the patches with RESONANCE up (A11 +7.4, B9 Filter
      Flow +5.3, B11 Sync Wah +4.8), the quieter singing those at 8 to 10
      down (B14 Pulsar -1.7, A16 Voice -1.6, A3 Organ I -1.5); the fifteen at
      RESONANCE 0 are as they were. (Before: Planet -36.1, A11 -28.6, the
      middle -16.6, B7 the loudest.)
    - **Six keys as written: nothing clips, A8 E. Piano I peaks 1.56 out of
      the synth** (1.01 after the track's usual level; it was 1.46), every
      other patch 1.31 at most (A13 Chime; it was 1.17). **Macros full:
      nothing reaches the clip either** (since 2026-11-16; twenty patches did
      while RESONANCE 10 sang loud from 8.7 up, B8 at 1.79); A8 1.54 is the
      highest. Nothing is not-a-number and everything ends.
      `kVcaClipCeiling` (1.5) is not changed. **`kBassKept` 0.7 put A8 into
      the clip** (13 samples): the share given back is held by that.
    - **A song from builds 0.504 to 0.514 loads as saved; a JAX-EP track at
      LEVEL 4 or under is within 2 dB of how it sounded, higher LEVELs
      quieter and clean (LEVEL 6 about 4 dB, LEVEL 8 about 10 dB before its
      old clip). From 0.502 or earlier: brighter, and about 12 dB louder**
      (said in README).
  - **Builds 0.485 to 0.493 shipped JAX-EP with one preset, "Start", as
    preset 1.** A song from them keeps its sound (all 34 settings are read
    after the preset) under String I's name (`save_test`
    `testASongFromTheOnePresetBuilds`). **A preset's place is in songs from
    now on: append, never put one in before another.**
  - **A test that listens to a new JAX-EP track makes it plain first** (MIX
    0, ATTACK 0, CHORUS off): A1's two saws beat, and a note's peak hangs on
    where in the beat it began (`design_test`).
- **`track twin` is the one word whose meaning changed** (owner, 2026-11-14;
  nothing was released): it reads as a JAX-EP track without its sound
  (`trackKindFromWord`'s `old`, `parseSong`'s `oldSound`: no `preset`,
  `macros`, `settings`, no automation on lanes 0 to 7; everything else kept).
  It is never written. **`track qmod` reads the same way, as FM-6** (owner,
  2026-11-15: QMOD replaced; `kOldQmodWord`): on FM-6's first preset, and a
  `mod` line on a macro is dropped too (`testAQmodSongOpensOnFm6`). An old
  `NAME.qmod` preset is left on the card, unlisted.
  `tests/twinsong_main.inc` is a song `main` wrote with
  one: **not to be remade**. An old `NAME.twin` preset is left on the card,
  unlisted. **Twin's code is out (batch 8b, 2026-11-15), and that reader is
  the one place the word lives**: `kOldTwinWord` and its comments, the tests
  of the old song and the old preset file, README's note on old files. There
  is no `SynthKind::Twin` (the kinds are memory only: Phazed, Sampler, Jx3p,
  Fm6). `ctest -R words` fails a text of `app/` that says TWIN ANALOG or
  OVERSAMPLING, with its own plants, **or BEST QUALITY: a bounce is the song
  as it plays** (the BOUNCE row said "at best quality" from Twin's x8 until
  the mend of 2026-11-15; no synth has a quality to turn up).
- **The timings' song is held by `ctest -R bench`** (`tests/bench_test.cpp`,
  through `sendBusiestSong`): what is on each track, bus and the master, that
  the two JAX-EP tracks are on A1 and the two FM-6 on E.PIANO 1
  (`kBusiestFm6Preset`; choosing either again changes nothing, to the bit;
  QMOD's two tracks until 2026-11-15), and that the real-time rows count the blocks and helpers they say.
  **Its 24 MODs too** (`sendBusiestMods`, batch 9: two a track, every kind of
  target, LEVEL on tracks 4 and 10, PAN on 5 and 11, every MOD 1's DEPTH lane
  written; each runs, and the 24 cut more chunks than the song alone).
  **It times nothing**; the figures are still run and read. `ctest -R demo`
  runs `render_demo` and reads its three lines. `bench`'s one-track rows have
  no test.
- **A preset holds `presetSettingCount(kind)` settings, a song
  `settingCount(kind)`.** They differ for JAX-EP: ids 32 and 33 (VELOCITY,
  BRILLIANCE) are the track's own, so `loadPreset` (built-in and the player's
  own) and `savePreset` stop at 32, and a new `TrackData` starts with them
  (`Jx3p::presetPatch(0)`). **Undo counts a song's** (`soundMark`,
  `restoreSounds`, `sendSynthPatch`: `app_test`
  `testUndoPutsBackTheTracksOwnTwo`, in the engine too). A number in a preset
  file is held as a double first, then to its setting's range (`readPresetFile`,
  `jx::loadPreset`), and a knob reads inside its dial whatever it is given. A new track value of any synth goes after its
  patch's ids and is kept out the same way.
- **A preset may hold the track's WHOLE sound** (2026-11-16, owner: "a great idea
  for recall"; `app/presets.hpp`, `docs/plans/presets-plan.md`, `tests/presets_test.cpp`):
  its three effects, two sends, GLIDE FROM and two MODs (`TrackSound`), **never the
  mixer's LEVEL and PAN** (the song's balance). Written and read in the song file's own
  lines by **the one writer and reader of them** (`soundText`, `readSoundLine` in
  song_file.cpp; `parseSong` and `putTrack` go through them too): never a second parser.
  A player's own file always says `whole` (SAVE: `presetFileText`); one from before has
  none and is plain. **Picked, a whole preset puts every part in, what it leaves out
  emptied; a plain one changes the synth only** (`Session::chooseSynthPreset`,
  `putPresetSound`, `putTrackSound`: only what differs as the file writes it, another
  kind through `setEffect`, nothing written into automation). **One exception: a plain
  preset picked while the track still holds just what a whole one put in** (nothing
  turned since: `PresetSoundMemory`, compared as text) **puts back what the track had
  before it**, so browsing past a whole preset leaves no effects behind; forgotten by
  any other change, SAVE, `applyProject`, `addTrack`, `setTrackKind`, moved by
  `removeTrack`. One step of undo takes a pick back (`soundMark` sees it all). A new
  track starts plain on preset 1, whatever that one holds.
- **The built-ins past the engine's own are DATA, compiled in** (`app/presets/NAME.inc`,
  one raw string a synth; `readPresetData`, read once, `builtInData`): `new FOLDER /
  NAME` with `base`, `set`, `settings`, `macros`, FM-6's `op N` (an operator's 21 settings a line)
  and the track-sound lines, and `preset
  N` to give one of the engine's own a sound. **Their places come after the engine's and
  before USER, and are in songs: APPEND, never insert, move or take out** (the engine
  never hears of them: `enginePresets` is what `SelectPreset` takes; past it the app
  sends every setting, as for the player's own). `builtInPresets` counts both; ask
  `listedPreset` for the values the app holds. Every line the data cannot read is a
  `problems` line, and `presets_test` fails on one; `presets_test --show` reads each as
  the device does. **The added PHYSIC and FM-6 presets are played through their whole sound**
  (`presets_test --heard`, `--heard-fm6`): loudness, peak, a tail that ends, and every MOD moving;
  FM-6's MODs must also be HEARD (the phrase with that MOD off differs by 1% or more: a MOD on BRIGHT
  where no modulator sounds, or on LFO where PMS is 0, moves and changes nothing). `fm6-plan.md` section 11. **Tests of the engine's tables (`app_test`, `design_test`,
  `textsize`) run with the data emptied** (`useBuiltInDataForTests`), so a designer
  adding presets breaks none of them; `presets_test` holds the shipped data.
- **FM-6's built-ins are five factory banks** (2026-11-16): ROM 1A, 1B, 2B, 3B, 4B,
  160 voices, a folder each, written by `tools/fm6_rom.py` into
  `engine/src/fm6/roms.hpp` (`kRom`, each with its folder). A bank is appended, never
  put before another. ROM 3B's three values past their range are held by the script
  (`HELD`); any other stops it. `fm6_pins.inc` pins all 160.
- **The FILTER type order is fixed** (`int(v * 5)`: low 12, low 24, band, DJ,
  high). It was chosen so old songs' three types still land right.
- **The five old effects are pinned to the bit** (FILTER, DRIVE, DELAY,
  REVERB, CRUSH): `effects_test` holds 40 renders and one whole song to
  checksums taken before they were moved. Old songs depend on them. Do not
  touch their arithmetic, not even to fix it; make a new effect instead. The
  checksums are remade (`effects_test --print`) only from code known to be right.
  **The whole song's sum was remade once (2026-11-15)**: its third synth went from
  Twin Analog to JAX-EP, and `--print` gave the same 40 renders before and after.
  It pins JAX-EP's sound too now: a change to that synth moves it, and is then
  proved by the 40 renders standing still.
- **An effect has a number, a word and a place, and they are three things.**
  The number (`EffectKind`) is in memory and commands: appended, never
  reordered. The `word` is in song files: never changed. The place in the
  list (`kList`) is only what the player sees: change it freely. Never use
  one for another (`effectAt` / `effectPlace` / `effectFromWord`).
- **A family is only how the list is shown** (owner, 2026-11-12: "TAPE should
  just be one of the delays"; 2026-11-13: seven families). The player picks
  from FILTER (FILTER, COMB, VOWEL, EQ), DRIVE (DRIVE, SATURATE, FOLD, FUZZ), CRUSH (CRUSH, RING, WEAR), MOD (CHORUS, FLANGER, ENSEMBLE, PHASER, TREMOLO),
  DELAY (CLEAN, TAPE, PING, DUB, REVERSE, STUTTER), REVERB (ROOM, HALL, SPRING, PLATE, BLOOM, SHIMMER), LEVEL (COMP,
  DUCK, LIMITER, SNAP), in that order (`EffectFamily`; the old groups, DIRT and MOVING, are
  gone). Underneath each type is the `EffectKind` it always was, with its
  number and its word, and `info.name` is still its own ("TAPE": bench and
  the tests use it). Fifteen more types were planned (effects-plan.md, "Seven
  families"); each is a row. COMB, VOWEL, EQ, FOLD, FUZZ, RING, WEAR, LIMITER and SNAP are built (branch `newfx`); FLANGER, ENSEMBLE, PHASER and TREMOLO, DUB, REVERSE and STUTTER, BLOOM and SHIMMER (branch `newfx2`). The screen asks `effectFamily`,
  `effectTypeName` (what the type is called: CLEAN, FILTER), `effectTypeShown`
  (the same, but "" where it is the family's own word: a line says FILTER,
  not FILTER FILTER, and DRIVE, not DRIVE DRIVE), `effectTypes` /
  `effectTypeAt` / `effectTypeIndex`, and picks from `familyAt` /
  `familyPlace` (a line a family; `bus` gives a bus its own short list). A
  song file is written as before: `app_test` holds a song with every
  kind up to DUCK to what `main` wrote (`tests/everykind_main.inc`; do not
  remake it). An effect newer than that is not in it (`kKindsMainWrote`):
  `testSongFilesAreAsTheyWere` writes and reads each newer kind by its own word.
  In the list of effects a family's line names its types, the slot's lit
  (`effectFamilyNamesTypes`: more than one, or one that is not the family's
  word, MOD's CHORUS); since RING every family does, so `effectFamilySays`
  (the first type's `says`) is drawn for none today.
- **Changing TYPE carries settings by NAME, as they READ** (`effectCarry`,
  `effectCarryValue`): the value whose reading is the same text; if the new
  type cannot read it, the nearest (an amount by the number in its reading, a
  choice by its row's `amount`). A scale that is the same on both sides is
  not touched, to the bit. Where the scales differ an amount lands on **the
  roundest value that reads so** (a hundredth, else a thousandth, nearest the
  middle of the stretch), never on the stretch's edge: from an edge the first
  push of the D-pad moved nothing that could be seen, and 45% came back as
  0.4452 in the song file. A twentieth goes there and back to the bit.
  `changeEffectType` (app/fx_screen.cpp) does it to
  a slot and **moves the automation of each kept setting to its new lane**
  (REVERB's MIX is its third setting, HALL's its fourth), read the same way,
  in every scene (a step written as 0 is a step); the rest of the slot's
  lanes are cleared. **Undo after a TYPE change puts back the old type, its
  settings and its lanes together** (mended 2026-11-14: effects are in undo
  now, next rule; it was wrong from 2026-11-13). Another FAMILY
  put in a slot (`Session::setEffect`) still clears all four.

### The engine and its threads
- **The audio thread never allocates, locks for long, or frees.** Things it
  is done with go to the trash queue (`bin()`); `collectGarbage()` frees them,
  once a frame. What finds it full waits with the audio thread and is handed
  on at the end of a chunk (`binHeld`). **Nothing is kept in the engine for
  want of room**: a synth kept so played the next song's notes.
- **Commands go through a queue that can fill.** `Engine::send` returns false
  and counts the loss (`dropped()`). `Session::update` notices and sends the
  whole song again (`applyProject`). Callers that hand over a pointer must
  check the return themselves. **Every push goes through `send()`**, the
  ones that carry a recording or an effect too: a loss it does not count is
  never mended.
- **A test that never renders never empties the queue.** Render alongside
  frames (`rig.frames(n); rig.render(...)`), or commands pile up and drop.
- **Helper threads** (`useWorkers`): tracks are rendered side by side and
  mixed in track order, so the result is the same to the last bit with any
  number of helpers. **Anything a helper waits on is set with `wakeMutex_`
  held.** Setting the stop flag without it hung the quit. The helpers take
  the tracks from `taken_`, in track order, or heaviest first by last chunk's
  times (`setHeaviestFirst`, off: only the SPEED TEST's rows ask for it, speed
  plan batch 0); the mix always walks `active_`. `speed_test` holds both to
  the bit.
- **The built-in drum kit is built once per sample rate** and shared
  (`DrumKit::builtIn`). It never changes after that.
- **A track taken away while it sounds fades for 5 ms first** (`RemoveInstrument`,
  `Track::going`, `kGoneFadeSeconds`; owner, 2026-11-14): its instrument, its
  effects' tails and its thrown hits go down together, then `takeAway` does
  what the command always did. Silent in its last block (`kGoneQuiet`, -100 dB):
  at once, as before. **What was sent after waits for the fade** (`held_`,
  `takeCommands`), and comes at the chunk the fade ends on, in the order sent;
  other takings away fade beside it (a new song takes all twelve at once).
  `caughtUp()` counts the held command. A test that takes a sounding track away
  and wants the next thing at once renders 5 ms first (`engine_test`
  `testATrackTakenAwayFades` holds it beside a second engine, sample for sample).
- **A lane with nothing written in a scene goes back to the knob** (`Track::autoBase`, read in `fireStep`
  and by `settleAutomation` at Stop), not to the last value written. So a FILTER's CUTOFF automated in
  the last scene only opens up at the wrap when the knob is more open, which sounds like the filter
  left (owner's report, 2026-11-14). That is the design; whether an unwritten scene should HOLD the last
  value instead is the owner's call, not made. **Whatever changes a knob in the engine sets its rest
  too**: `SetMacro`, `SetEffectParam`, `SetSend`, and `SelectPreset`, which puts all eight macros' rest
  at the middle as the synth does (it left the old sound's knobs as the rest, and the song played the
  old sound's macros after every wrap into a scene with none written: `engine_test`
  `testAPresetPutsTheMacrosRestBack`). The app sends the macros after a built-in preset too
  (`chooseSynthPreset`), as `sendSong` does: the engine is told the project's knobs, never left to agree
  by itself (`app_test` `testABuiltInPresetRestsTheMacrosWhereTheyShow`).
- **A tempo change keeps every place in the step** (`SetTempo`): what is left
  of the step, an arpeggio's wait, notes waiting, notes' ends and drum hits
  waiting are all scaled by the same amount, so the arpeggio stays on the beat.
- **An effect going into or out of a slot fades for 10 ms**
  (`Engine::swapEffect`, `runSlot`): the old one and the new both run, and
  the sound slides between them. Outside a fade a slot runs exactly as it did.
  The old effect goes to the trash from `renderChunk` after the helpers are
  done, never from `renderTrack`: **the trash queue takes from one thread.**
  A test that wants an effect in place at once uses the setup call
  `setTrackEffect`, or renders 10 ms before it listens.
- **Two send buses, ECHO and SPACE** (`kEchoBus` -2, `kSpaceBus` -3: "track"
  numbers beside `kMasterTrack`, one slot each; `slotsOf(track)` says how many
  slots anything has: tracks 3, master 2, a bus 1). A track's send is taken
  after its level and mute and glides for 5 ms. The buses are fed and run on
  the audio thread **after the helpers are done, in track order**
  (`feedBuses`, `runBuses`), and join the mix before the master's slots. Keep
  it there: that is what holds the output to the bit on any helper count.
- **THROW: a thrown drum hit is written twice, a thrown note opens the
  track's send.** The drum machine writes a thrown voice a second time into
  `thrown_[track][bus]` (`DrumMachine::render`'s two extra ways out), so the
  bus gets that hit alone, before the track's slots, at the track's level and
  mute. A thrown note holds `Track::throwLeft` for its length and
  `feedBuses` slides `throwNow` open and shut over 5 ms; a ratchet's first
  piece holds it for the whole note (`Note::hold`). "In full" is a send of 1:
  the knob's share is topped up, never added to (`now + open * (1 - now)`;
  a thrown hit gets the `1 - now` the knob leaves).
  - **The voice loop is built twice** (`renderVoice<true / false>`): with
    THROW's check inside the one loop, twelve unthrown drum tracks cost half
    as much again (1.46% to 2.18%). The line that adds to `left` is as it
    was; the thrown copy is worked out again beside it, not taken from it.
  - **A voice's plain stretches are runs** (speed plan batch 2; `plainRun`,
    drums.cpp): forward at speed 1, its level, rise, fade and THROW at rest,
    up to the sound's end, its part's end or the sample its cut falls on. A
    run is a plain loop with the same sum written the same way; every other
    sample goes the long way. **Anything new that moves in a voice from one
    sample to the next makes `plainRun` say 0 while it moves**, or the run
    skips it. `speed_test` (`testDrumRuns`) holds the two ways side by side
    (`setRunsForTests`). Twelve drum tracks 1.47% to 0.43% of a desk core.
  - **A track that throws nothing takes the old loop in `feedBuses`**, which
    is why a song without throws is the same to the bit (the pinned song).
  - `letGoOfNotes` shuts every send a THROW held (Play, Stop, panic), as do
    `prepare` and a track taken away. Anything new that lets notes go in
    another way calls `closeThrows`.
  - In a command THROW rides with the cut (`StepFx::cutAndThrow`: cut plus 4
    times THROW, in `extra` of `SetStepFx` and `Trigger`); a note's is 512
    (ECHO) and 1024 (SPACE) added to `SetNote`'s `e`.
  - A hit that carries a lane's sound on hands on its own THROW, and the
    sound slides into the bus or out of it over a millisecond.
  - Its tests are their own two programs, each a few seconds:
    `tests/throw_test.cpp` (the engine) and `tests/throwapp_test.cpp` (the
    gesture, the song file, undo, a bounce).
  - **A thrown drum hit is not ducked, a thrown note is.** The hit's copy
    leaves before the slots; a note's THROW is a send, taken after them. A
    DUCK on the master dips what the buses give back. `throw_test` holds all
    three; do not move the thrown copy after the slots.
  - **A bounce ends the song with Stop, and Stop shuts a thrown note's send**
    but not a thrown drum hit, which rings out into its bus. The tail after
    a last-step throw is kept by the bounce's own rule (seven seconds of
    quiet); `throwapp_test` bounces a hit, a note and a howling TAPE there.
- **A bus at rest is skipped whole and adds nothing, not even zeros.** That is
  why a song with no sends is the same to the last bit as before there were
  buses (`effects_test` holds the old song's checksum with an effect waiting
  on each bus). A bus rests when it has been sent nothing and has put out
  nothing above `kBusQuiet` (1e-6) for `kBusRestSeconds` (10 s); its effect is
  then `reset()`. Ten seconds must stay longer than the longest silence
  between two echoes of anything a bus can hold (`effects_test` measures each
  delay and reverb at 40 BPM: DELAY 3.0 s). A bus with no effect is not run.
- **A bus's LEVEL waits for a swap's fade** (`held` in `runBuses`): another
  effect put on a ringing bus sets where LEVEL is going (full), but it stays
  where it was for the 10 ms the old one fades out. Set at once, a bus turned
  down had its old tail turned UP as it left.
- **A bus's LEVEL is the engine's, not the effect's** (`busLevel_`, applied in
  `runBuses` after the rest rule has listened, gliding like a send). The
  effect on a bus keeps its MIX at full. Listened to after LEVEL, a bus
  parked at LEVEL 0 with echoes in it was cleared after ten seconds.
  `setEffect` says which setting is the LEVEL (`effectMixParam`); an effect
  handed over whole (`setBusEffect`, a setup call) keeps its own MIX.
- **`Engine::setEffect` refuses an effect that is not the bus's kind**
  (returns false), as the Session does. A FILTER on ECHO would give the
  track's sound back a second time.
- **What is sent to a bus is sound or it is silence**: not-a-number, infinity
  and anything beyond `kBusLoudest` are zeroed in `feedBuses`, since the old
  DELAY and REVERB cannot look after themselves and would hold it for good.
- **A tail always ends** (owner, 2026-11-12). `kLongestTailSeconds` (450) is
  the promise: from the last sound a bus is sent to the bus at rest, at every
  setting of every effect a bus can hold, at 40 BPM. `effects_test`
  (`testATailAlwaysEnds`) measures every delay and reverb in the table at 0,
  50% and 100% of each setting and fails one that takes longer, so **a new
  delay or reverb whose loop can hold at 1 or over needs its own way down**.
  TAPE's: left alone 30 s (nothing over `kFedLevel` coming in), a FEEDBACK
  over 90% eases to 90% (`tire_`); at 90% or under it is multiplied by exactly
  1 (`tapeTire` compares the SETTING, 0.75 and a hair, not what it reads:
  `1.2f * 0.75f` is a float over `0.9f`). The count, the ease down
  (`tapeTireStep`), falling asleep and waking are each on their own SAMPLE,
  never a block's edge. **The promise is for one effect.**
  Effects in a row (two TAPEs: a slot into a bus, or two slots) each count
  from the last sound coming IN, so one keeps the next fed and the tail can
  pass 450 s; the bounce's cap is what ends that one. DELAY, PING and REVERB cannot ring for good (0.9, 0.9 and 0.98 at
  most round the loop) and are not capped from outside.
- **An effect on a bus is told so** (`Effect::setOnBus`, by `Engine::setEffect`
  and `setBusEffect`), gives back no dry sound, and starts with its MIX, which
  reads LEVEL there, at full (`effectDefault(kind, i, true)`).
- **Flush-to-zero is ON in the program and in a bounce** (owner, 2026-11-11;
  `Engine::setFlushDenormals(true)` in `main.cpp` and `bounce.cpp`), set for
  each `render()` and on the helpers. It changes the last vanishing samples of
  a tail (all below 1e-30). **The tests leave it off**, which is why the
  pinned checksums still match: do not turn it on in `effects_test`. TAPE and HALL do not
  rely on it: they look after themselves (next rule).
- **A new effect glides its settings and looks after its own small numbers**
  (`fx/glide.hpp`; TAPE and HALL are the pattern). Every setting goes through
  a `Glide` (20 to 30 ms, and it lands exactly); anything worked out from a
  setting (HALL's eight gains) slides sample by sample too, never a step a
  block. Anything that fades is fed a floor of 1e-18 or gated at `kQuiet`, and
  the effect goes to sleep (exact zeros, no work) when its memory is empty: it
  must make **no number too small to hold with flush-to-zero off**, and
  `TooSmall` in `effects_test` checks that without a clock. HALL with its
  floor taken out ran 10 times slower two seconds into a tail and 40 times
  slower for good. A delay or a reverb reads `Unit::bus_` in one place for
  the bus call (TAPE and HALL had their own `wetOnly_`; it is gone).
- **A new effect calls `soundOnly()` first thing in `process()`**
  (`fx/glide.hpp`): a sample that is not a number, infinite or beyond 1e8 is
  taken as silence. One such sample used to stay in a delay line, a filter or
  COMP's level reading for good. The old five do not (their arithmetic is not
  touched). `effects_test` tries every effect newer than CRUSH, each choice
  of each setting, and fails a new one that forgets.
- **A one-pole that lands exactly must land further out than a float can
  step.** Heading for 1, a 1 ms smoothing at 48 kHz stops moving 1.4e-6 short
  (k times the gap is under half a float step there), so a landing at 1e-6
  never came and the JX-3P's voice was never freed. Its hold lands at 1e-5
  (`jx3p::kHoldLands`); a new smoother picks its gap the same way. **And it
  lands when a step no longer moves it** (`Hold::next`): a float's step grows
  with the value, so a fixed gap is right only near 1. The HPF's hold, over 4,
  stuck 1.1e-5 short and the HPF and chorus ran on silence for good.
- **A minimum-phase BLEP makes the jumps late (2.23 samples at the bottom), so
  every slope of the wave carries the same lateness** (`kBlampCarry` in
  `Dco::take`, the saw's too). A saw that did not had a mean of 4.47 x dt:
  37% of its peak at the top keys. **A test that takes the mean off cannot
  see an offset**: `testNoOffset` reads the mean itself.
- **A `Glide` always lands, at any block length** (never slower than `kLeast`
  a sample). A fraction of what is left falls below what a float can add in
  blocks of 8 frames and under, and `moving()` then stayed true for good.
- **A gain or a filter's numbers turned at once set off gently and slide
  sample by sample** (`fx/svf.hpp`, COMB, VOWEL, EQ): what glides is worked
  out on the TICK (every 16 samples counted from the effect's start, so the
  sound is the same to the bit however it is cut into blocks, moving or
  not), `Slide` reaches each tick's numbers sample by sample, and `Gentle`
  is two Glides one after the other. EQ's LOW turned from +12 to -12 dB at
  once made a corner 790 times a 100 Hz sine's (0.28, a click) stepped a tick
  at a time; slid, 34; with `Gentle`, 2.2. `holdsTheRules` in `effects_test`
  turns every setting of a new filter at once and holds the numbers.
- **A change of curve waits for the fade before it** (SATURATE's TYPE: one
  fade at a time, the last one asked for wins). Starting a second fade inside
  the first cut the fading curve off: a click.
- **A sleeping effect that wakes starts as a fresh one** (settings snap, the
  wow and the bend start again), so a render does not depend on how long ago
  the last sound was. **So one whose sound hangs on a clock that runs on
  (RING's carrier; WEAR's sway and its dice, rolled by the count of samples)
  sleeps only after `kRestSeconds` (4 s) of nothing.** It
  slept after a third of a millisecond of exact zeros at first: every drum hit
  met the carrier at its top, and a slow ring did nothing, unlike the same
  hits with sound too faint to hear between them. **One exception, kept (review 2026-11-14): a setting
  turned within a glide of waking.** The TICK counts from the effect's start,
  asleep too (`tickCountsAsleep` pins it), so its 16-sample grid falls where
  the silence's length put it, and such a glide moves on ticks up to 15
  samples apart from a fresh one's (WEAR 1e-4, SNAP 0.002, LIMITER 0.02 apart;
  RING, COMB and FUZZ alike). With settings at rest, exact.
- **A DRIVE type that evens out its loudness does it without its gate**
  (FUZZ: GATE only takes away), and **a fuzz is matched where it lands**, a
  sine at 0.18, not half scale: it brings everything to about one level, and
  matched at half scale a quiet note came out 17 dB over DRIVE. A tone's
  make-up is for the share squares A1 to A7 keep, averaged, not one pitch
  (made up for 220 Hz alone, a high note came out 14 dB louder).
- **DUCK is told the hits, it does not listen** (`Effect::drumHits`, bit 0 is
  lane 1). A hit always lands on a chunk's first sample (`render()` ends a
  chunk where the next is due), so the engine marks it where it triggers the
  drum (`Track::hitsNow`), and `renderChunk` copies the first drum track's
  marks to `chunkHits_` and clears them **before the tracks are shared out**;
  helpers only read it. Keep it that way: written during the chunk, a ducked
  track would depend on which helper got to the drums first. A new way to
  trigger a drum must set the mark too. Every trigger counts (ratchet pieces,
  hits by hand); a bus's slot is not told.
- **A lane's value goes out through `Engine::giveLane`, and only there**
  (MOD, `docs/plans/modulation-plan.md`; `engine/include/apotheca/mod_rule.hpp`
  is the rule, no SDL, `tests/mod_test.cpp`). Automation, Stop and
  the knob's own commands (`SetMacro`, `SetEffectParam`, `SetSend`) all set the
  lane's CENTRE (`autoNow`, `autoBase`) and call it; it gives
  `modGiven(centre, Track::modOffset[lane])`. **An offset of exactly 0 gives
  the centre as it is, in the same call as before** (not even clamped): that
  is why a song with no MOD is the same to the bit (`effects_test --print`
  identical, 2026-11-14). The centre is never the wobble: automation and Stop
  compare with it. A new way to set a macro, an effect setting or a send of a
  track goes through it, or a MOD on that lane is lost until its next tick.
  Only `runMods` sets an offset, and `stillLane` puts it back to 0.
- **A MOD runs on a tick, and only while one is on** (batch 1, 2026-11-14;
  `Engine::runMods`, `Track::mods`, `SetMod` / `SetModStep` / `ModAudition`).
  The tick is every `kModTick` (64) samples **counted from each step's first
  sample** (`sinceStep_`), the position read before the chunk is taken off
  `untilStep_`; a synth's macros every `kModMacroTicks`'th tick (512), effect
  settings and sends on each. `render()` cuts the chunk on the tick **only
  while playing with `anyMod_`, or stopped with an audition**: a song with none
  is cut as it always was, to the bit. `runMods` runs on the audio thread
  before the tracks are shared out; a lane is given only when its offset
  changed; two on one lane add. It passes by a slot held by the FX screen
  (`autoWaits`) and a setting that is a choice (`slotChoices`, told in
  `InstallEffect` by `Engine::setEffect`). **Stop, Panic, a MOD aimed
  elsewhere or OFF, the hold, an effect put in a slot and a track taken away
  let go** (`stillLane`: offset 0, the centre given); a track taken away has
  its MODs OFF. **Anything new that stops the song, or changes what a lane
  belongs to, lets go of the offset**, or the old wobble stays frozen on the
  knob. `ModAudition` (X held while stopped) runs a track's MODs from the
  moment X went down; it is not taken while playing. `mod_test` holds all of
  it; `mod_test --measure` prints the figures.
- **Under a MOD, an effect that glides a setting a CALL at a time is not the
  same to the bit in other blocks** (CHORUS, COMP, DUCK, HALL, PING, PLATE,
  SATURATE, SPRING, TAPE: `Glide::begin` in each `process()`). At most 0.0024
  apart (about 46 dB down), held under 0.005 by `mod_test`. The TICK effects,
  the old FILTER, the sends and the macros are exact, and helpers never change
  a bit. Not mended (it would change those nine's glide): the owner's call. A
  fast MOD on them is also mostly smoothed away (a 1/2 STEP SINE keeps 31 to
  43% of its swing; the plan's section 13 has every rate).
- **A song is the same to the bit in any blocks only at a tempo whose step is
  a whole number of samples** (120 BPM: 6000), MOD or none, as on `main`
  (review, 2026-11-14: it was blamed on Phazed). At 133 (5413.5) a note that
  ends or waits, a nudge, a ratchet or a glide can land a sample apart by how
  the sound is cut. A block-exact test says its tempo: `mod_test` runs blocks
  of 1 with Phazed at 120, and without it at 133.
- **A preset chosen puts the eight macro lanes' centre at the middle**
  (`Command::SelectPreset`, `kPresetMacro`): every synth's `selectPreset` puts
  its macros there, and the engine's centre must agree or a MOD moves about
  the old knob. A lane a MOD is adding to is given at once; with nothing
  added no macro is given. **A new `Instrument` keeps that promise**
  (`selectPreset`: macros to 0.5), or the engine's centre is wrong for it.
- **The JX-3P's `allNotesOff` hands every sounding voice to a tail**
  (`kTailVoices`, `engine/src/jx3p.cpp`): it rings out its release beside the
  six, which start again as from fresh, so Play from the top is the same
  performance after Stop however long the release (`jx3p_test`
  `testPlayFromTheTopWhileRinging`). Play and Stop let notes go, never cut
  them; a fresh voice is `State::Voice()`, so anything new a voice remembers
  is put back with it. Its steal takes a key let go before a key held
  (`kStealLetGoFirst`).
- **The JX-3P's VCF bends only its feedback** (`engine/src/jx3p/ir3109.hpp`):
  four trapezoidal one-poles solved as one sum, the feedback's soft clip (no
  flat top) taken as a gain **at where the feedback is going: read ahead from
  its last three samples as a steady part plus a sine at the cutoff**
  (`VcfCoef::turn`; the owner's report, 2026-11-15, "it just squeals").
  Taken from the sample before, the gain was a sample late where the filter
  sings: 30 cents sharp at 4 kHz, 3 dB louder at 10 kHz, and a bass note beat
  the sine into a buzz. **Keep the read-ahead**: a line from the last two, or
  the tangent at the last, were tried and are worse. RESONANCE is straight to
  the edge (4) at 9.5, then climbs to 5 at 10 (`kResEdgeDial`, `kResTop`):
  **straight to 4.21 left Organ I's sine so weak that DCO-2's harmonic, 27
  cents flat, held it**. Half the low end the feedback takes is given back
  outside the loop (`kBassKept` 0.5, two plain one-poles at a quarter of the
  cutoff), so the octave under the cutoff keeps its loss and Organ I's sine
  stays 13 dB over its 16' DCO. At RESONANCE 0 the bend's sum is not worked
  out (y4 is the stages' sum). A bend in each stage was tried and taken
  out: it moved the singing's pitch with its level (16 cents off over Organ
  I's three octaves; the plan holds 5). Its coefficient is worked out only on
  a sample its CV moved, from polynomials (`exp2Poly`, `sinPoly`, `cosPoly`:
  no `floor`, `ldexp`, `tan`, and **no division by a constant**, which the
  compiler does not turn into a multiply; together with the bend's division
  folded into the filter's own, taking them out brought the moving-cutoff
  bench row from 2.1% to 1.6%). The cutoff's hold lands 1e-3 of an octave
  from its step (`kVcfCvLands`): landing at the VCA's 1e-5 it slid on into
  the last part of the tick (`testTheCvSteps` fails), and the coefficient is
  worked out on every sample it slides.
- **The JX-3P's LFO is one for the synth, on a 10 ms tick counted from
  allNotesOff, and it runs whether anything sounds or not** (`engine/src/jx3p/lfo.hpp`).
  `Jx3p::render` cuts its block into pieces that end where the LFO ticks, so a
  voice's own tick reads it as of its sample however the sound is cut
  (`jx3p_lfo_test` `testBlocks`). Anything new the synth works out once for all
  voices on a tick goes in that loop, never once a block. A voice reads the LFO
  only on its tick; a key with no other key held starts the DELAY again, and the
  LFO reads 0 from that sample (`startDelay`). RANDOM is a hash of the count of
  cycles (`kLfoSeed`, pinned): never a generator walked.
- **The JX-3P's chorus sweeps from the synth's clock, and is switched at once
  before the first sample** (`engine/src/jx3p/chorus.hpp`). Its place is the
  samples since allNotesOff, counted whole, so it is the same however the
  sound is cut, Play from the top is the same sweep, and it can sleep (exact
  zeros, skipped) and wake as a fresh one. **It is not skipped while its fade
  is under way**, or a switch in silence would wait for the next sound. Until
  the synth's first sample after it is made, set up or allNotesOff'd
  (`sounded`), a switch of CHORUS lands at once. The tests that measure
  another part turn it off (`held()` and the plain patches); the house rules'
  tests keep it on. Tails hear its sweep start again, as they hear the LFO.
- **The JX-3P's DCOs correct every jump and every bend where it falls in the
  sample** (`engine/src/jx3p/dco.hpp`): minimum-phase BLEP and BLAMP tables, so
  nothing is late and a key still sounds on its own sample. The tables are
  numbers written by `tools/jx3p_blep.py` (`blep_table.hpp`): change the
  script and run it, never the numbers. A bend's correction leaves a constant
  (`kBlampCarry`), kept as a state worked out from the slope, never summed.
  **A jump or a bend worked out to a hair past the sample's end is still that
  sample's** (passed by, the wave jumped raw: `testAnEdgeOnTheSamplesEnd`).
  A wave, DCO-2's start phase and the noise seed are each a voice's own; what
  a free voice starts with comes from the count of keys, as batch 1's phase.
- **The step clock walks only the places that may hold something** (speed
  plan batch 1; `PlaceBits` in engine.hpp: `Track::notesWaiting`, `keysHeld`,
  `hitsWaiting`). **Whatever fills a waiting note's place, sets a key's
  `offIn` or fills a drum hit's place sets its bit**; an emptied place may keep
  its bit (the next walk takes it off). A walk visits the bits lowest first,
  the order a walk of every place had. `speed_test` (`testStepClockPlaces`)
  holds it beside an engine that walks every place (`gWalkEveryPlace`).
- **The MIDI thread only uses `sendLive`** (notes and drum hits, its own queue).
- **A hit written into a cell sends that one hit** (`Session::sendDrumHit`),
  never the whole cell again: a keyboard's eight pads at once on a busy
  eight-bar cell filled the command queue (`midi_test`).
- **What an effect shows goes through `Engine::effectMeter(track, slot)`**
  (COMP: the dB it is turning down). The audio thread reads the effect's
  `meter()` after each chunk and keeps it in an atomic; the screen asks the
  engine. **Nothing on the app thread may hold or call an `Effect` that is in
  a slot**: the audio thread can swap it out at any chunk. The FX screen draws
  it three ways: on the row `effectMeterParam(kind)` names (COMP: THRESHOLD; DUCK: AMOUNT; LIMITER: GAIN), as a
  strip under the bar from the right, full at `kMeterFullDb` (24); in words in
  the heading (`FxScreen::meterWords`: TURNING DOWN 7 DB, DIPPING 3 DB); and
  as a brass mark on the effect's line of the list.
- **A new effect's sound does not depend on how it is cut into blocks**, to
  the bit, once its settings are at rest (each has a test). So what an effect
  does "between blocks" (snapping vanishing numbers to zero) is done sample by
  sample, or on a fixed count of samples (SATURATE: every 64th it has been
  given), never "at the end of this call". The same goes for anything slow
  that moves by itself: TAPE's wow was a sine worked out at each end of a
  call, and its count of time left alone went up a block at a time; both
  made a bounce differ from live play. **TAPE's test cuts 150 s four ways**
  (`testTapeAlwaysEnds`); a new effect with a tail gets the same, through its
  sleep and its waking (PING: 70 s, three settings, both sides, in `testPing`). **HALL does not pass it yet** (apart from 0.02 s in
  blocks of 64 and of 48; as on `main`, not changed). PLATE does (`testPlate`:
  its sleep, its waking and its bend are each on their own sample, as TAPE's).
- **PING (`fx/ping.cpp`) is the plain pattern for a delay**: two lines, the
  sound in on the left one (both sides halved and added, so a mono sound
  bounces), each line's echo on to the other through a high cut and FEEDBACK.
  **Nothing under `kQuiet` goes on a line; an exact zero does**, so "asleep"
  is "both lines are zeros from end to end" and waking is a fresh effect to
  the bit (TAPE leaves what is under 240 dB down on its tape: 1e-9 apart).
  So a steady sine's samples at its zero crossings (under 1e-12) are not
  echoed. **Its tail leans left** (each right echo is FEEDBACK of the left before it:
  7.8 dB over a tail at FEEDBACK 50%, each echo 0.45 of the last; a sound hard right echoes first on
  the left; the owner's call, pinned in `testPing`). **Its FEEDBACK reads 0 to 100% and does nine
  tenths of it, exactly as CLEAN's** (owner, 2026-11-14; it read 0 to 90%, and the D-pad's stops read
  5, 9, 14, 18...). Only the reading changed: the same setting does what it did, so old songs sound
  as they did. CLEAN and PING are one scale (carried to the bit); TAPE reads to 120% (carried as it
  reads, 50% to 0.42). Its loop is still 0.9 at most: the tail always ends.
- **A filter state does not reach zero by itself**: with a coefficient over a
  half it sticks at the smallest number there is. Snap it (`< 1e-20f`), or
  "silence in, silence out" fails and a processor without flush-to-zero crawls.
- **A wet sound that comes out late is never mixed with the plain sound as it
  came in.** Two samples late is half a wave at 12 kHz, and the two cancel (a
  comb filter). SATURATE's 2x stage does this; its plain sound goes through
  `Halfband::late` first, except at MIX 0 exactly; FOLD and FUZZ do the same
  through `BendSide::plain` / `BendSide4::plain` (`fx/bend.hpp`). Test a new effect's
  response at MIX 50%, not only at 0 and 100%.
- **An effect that is late by design has no MIX** (LIMITER: always exactly
  3 ms late, `kLookSeconds`, 144 samples at 48 kHz; a MIX would lay the plain
  sound 3 ms early under it). Its "untouched" is the sound as it came in, 3 ms
  later, to the bit (`testLimiter`, `engine_test`); `holdsTheRules` skips its
  MIX-0 check. **On a track it puts that track 3 ms behind the others**: it is
  meant for the master, where everything is late alike. A swap into or out of
  it lays the sine over itself 3 ms apart for the fade (2.10 of a step,
  allowed 2.3 in `testSwapFade`).
- **An effect with a wave that runs by itself sleeps by the rest rule** (RING,
  WEAR, and MOD's four: `kModRestSeconds`, 4 s of nothing, AND its memory
  exact zeros). FLANGER's, ENSEMBLE's and PHASER's sweeps and TREMOLO's wave
  start again from their start when they wake. **FLANGER's reach (DEPTH,
  narrowed above 2 Hz) moves at most its whole way a second, and the sweep
  goes only as fast as that reach allows** (`reach_` x Hz <= 2): with the
  narrowing worked out at once, RATE thrown from slow to 8 Hz bent the pitch
  five semitones for 25 ms as the sweep closed up (review, 2026-11-14). **TREMOLO is in time, not on
  the beat**: an effect is told the tempo, not where the bar is, so its wave
  starts where the first sound after a rest is.
- **DELAY's newer types (DUB, REVERSE, STUTTER) keep the TICK and count
  time in samples** (`fx/echo.hpp`). **DUB goes to 150% and has its own way
  down** (TAPE's rule: left alone 30 s, a FEEDBACK over 85% eases to 85%,
  `dubTireOf`; 85% or under is never touched, to the bit): its loop is a band
  (Q 0.7) at TONE, so its howl is a roar about TONE, one echo for both sides.
  **REVERSE plays each window of TIME backwards through the next**; the sound
  that wakes it is put in the middle of a window, so THAT hit's backwards echo
  peaks exactly one TIME after it (`T - 1` for an even T); a later hit comes
  back 0 to 2 TIMEs after it, by where it fell in its window. **Its longest
  gap is two windows** (6 s at 1/2 and 40 BPM: `kLongestEchoGapSeconds` in
  engine.hpp), so a bounce's quiet is 7 s (`kBounceQuietSeconds`; it was 4 and
  cut REVERSE's last echo off) and a bus's rest 10. Its windows fade 4 ms
  at each edge; TIME is taken at a window's start. **STUTTER's splices END on
  a slice's edge** (into its start through the 4 ms before it, kept in a
  ring), so a hit's attack is heard whole every repeat; **its slice edges are
  the whole sample at or before the exact edge, counted from waking**, never
  a sum of rounded slices (those drifted off the steps: at 133 BPM the
  offbeat was caught from 9.7 s on); its dice roll a place
  (REPEATS slices) by its number since waking (`stutterRoll`); on ECHO it gives
  back only the repeats. A slot's output there is `in * (1 - mix * a) + mix *
  rep`, never `in + mix * (rep - in)`: that one is not the slice to the bit.
- **REVERB's newer types (BLOOM, SHIMMER) share `fx/tank.hpp`** and keep the
  TICK; nothing under kQuiet goes on a line, so a woken one is fresh to the
  bit. **BLOOM sleeps 5.5 s after its tail** (its swell's 4 s ring must be all
  zeros). BLOOM turned moves its taps at most half a sample a sample (0.05 s
  to 4 s takes 8 s; the swell bends down an octave at most on the way).
  **SHIMMER's way round never gains**: what goes up an octave is mixed with
  what stays at equal power, the two grains' lost quarter is given back, its
  wait in the shifter is paid for in DECAY, and the shifter takes nothing
  under 40 Hz (an octave of a steady offset is itself) or over 7 kHz (with the tank's damping, a steady 14 or 15 kHz folds back 42 dB under itself; `testShimmer` holds it under 30). The
  octaves climb out past 7 kHz, so at SHIMMER full a 20 s tail is 0.58 as long
  (tested). A ceiling of 64 on a line is a guard only.
- **A per-sample loop works on locals** (WEAR's `stretch`: up to the tick's
  end, everything the samples change copied in, worked on, copied back).
  Written straight on the effect's own numbers WEAR cost 0.40% of a core; the
  same arithmetic on locals, 0.15% (the compiler cannot keep members in
  registers across the writes to the block). A new effect over about 0.15%
  is looked at this way before it is called costly.
- **A new effect glides every setting** (`fx::Glide` in `fx/glide.hpp`, the one such helper): a
  turned setting slides for about 20 ms and then arrives exactly, so MIX at 0
  is still the sound untouched to the sample. The first block after it is
  made or reset starts where the settings are (`snap`), with no slide. The
  old five do not glide and must not start to.

### The app
- **Anything below zero is not a track**: -1 the master, -2 and -3 the buses.
  `Session::effectData`, `setEffect` and `setEffectParam` take all three
  (`slotsOf(track)` bounds the slot). Test `track >= 0` before touching
  `project.tracks[track]` or automation, never `track != kMasterTrack`: a bus
  would then index the array at -2. A bus takes only its own kind
  (`effectBus`), and nothing on a bus or the master is automated.
- **The FX screen is two columns: a list of eight lines, always all in view,
  and the line picked drawn large** (owner, 2026-11-12; the EVERYTHING page
  was never found, so there is no second page and Y does nothing there).
  The lines (`FxScreen::goTo(row, column)`): 0 to 2 the track's slots, 3 its
  SENDS (`kSendsRow`: not a slot, so `data(row)` is not for it), 4 and 5 the
  buses (`kBusRow`), 6 and 7 the master (`kMasterRow`). **Column 0 is the line
  in the list; 1 and up are its settings** (SENDS: 1 ECHO, 2 SPACE).
  - Up / down in the list goes round; right goes to the setting the line
    last used (`last_`, kept through a change of track); left comes back.
    `update()` only pulls the cursor in when the line has fewer settings now.
  - **In a slot's settings, A HELD + LEFT / RIGHT switch its TYPE** (owner, 2026-11-14;
    `FxScreen::stepType`): the type before / after, in the grip that turns the value with
    UP / DOWN, as LEFT / RIGHT is the TYPE with A held in the list. The ends stop, the
    cursor keeps its row (pulled in where the new type has fewer), every setting row alike,
    D-pad only (the stick with A held turns the value), repeating at the list's pace
    (`kListEveryMs`). **Without A, LEFT is the list from any setting and RIGHT does
    nothing.** (A first try that day had plain LEFT / RIGHT switch, with LEFT the list only
    from the first setting: LEFT doing two jobs was awkward, and a long RIGHT in from the
    list ran through the types. It is gone, `cameIn_` with it.) **With A held the
    heading's TYPE row and the bar show the key that switches** (`typeKey`, `typeKeyFor`:
    RIGHT, LEFT or LEFT / RIGHT; no RIGHT on the family's last type, no LEFT on its first,
    none for a family of one or on SENDS); with A up the bar says LEFT list and HOLD A
    "turn, type" ("turn" where there are no types). B's hint there is "default".
  - **A slot remembers each type's own settings** (`Session::rememberType` /
    `rememberedType`, `switchEffectType` in fx_screen.cpp): a switch goes through
    `changeEffectType` (lanes moved as always: **a remembered type's automation is not
    kept**, only its settings), then sets the remembered settings, WRITE off. COMP ->
    DUCK -> COMP is the COMP to the bit. **Every setting, so a MIX or a bus's LEVEL too**:
    a bus turned down to 0 on PING is at TAPE's own LEVEL again on the way back (plain
    `changeEffectType` carried it; the owner has not ruled on this). **The lanes are not
    to the bit**: a shared lane is carried there and back, so where the two scales differ
    it comes back rounded (CLEAN -> TAPE -> CLEAN: FEEDBACK 0.3333 as 0.33, a TIME lane to
    the nearest choice) and an unshared lane is cleared; undo puts them back. The list's LEFT / RIGHT with A held tries and
    commits the same (`wouldGoIn` takes the remembered settings). In memory only, never in
    the song file; forgotten by `Session::setEffect` when another FAMILY or EMPTY goes in
    (a same-family `setEffect` keeps it: `changeEffectType` goes through it), by
    `applyProject`, `setTrackKind`, `addTrack`; `removeTrack` moves the tracks after it
    up with theirs. Undo puts back the type before (as any change of TYPE); it does not
    touch the memory. With the song stopped a switch sounds the track (`hear`); playing,
    or stopped by an end, it does not. `app_test` `testFxSwitchesType`, `testFxSwitchMends`; the engine's slot is read with
    `Engine::effectKindIn` (`Effect::madeAs`, kept by the audio thread beside the meters).
  - **Hold A on a slot's line opens the list of effects** (`picking()`), drawn
    from `familyAt`: DOWN is the next line of it, since it is seen (it was UP
    when the list was stepped through blind). Left / right is the TYPE. The
    group jump is gone. On a setting, A + up / down turns it as before, and
    left / right do nothing.
  - **With A held the list only TRIES** (owner, 2026-11-14: passing over the
    list threw a tuned effect away). The PROJECT is not touched while A is
    held: `tryKind` sends the engine's slot the effect under the cursor
    (`Engine::setEffect` and `SetEffectParam`: the usual 10 ms fade), and
    `Command::HoldEffectAutomation` holds that slot's automation in the engine
    so the pattern does not move the effect tried. `data(row)` (`onLine`) is
    the tried one meanwhile. **Let go of A** (`letGo`): another family goes in
    through `Session::setEffect` (its first type, then `changeEffectType` to the
    type picked), another type of the same family through `changeEffectType`
    from the effect it started with; what was heard is what goes in (the test
    holds the two equal). **Let go on the effect it started on, B with A held,
    another screen up (`App::update` calls `callOff`) or another track**: the
    engine is sent what the project holds, so the slot is as it was to the bit
    (its automation never moved). The family it started in is tried as the
    effect it started with, not afresh, which is what lets "down and back up"
    land on it exactly. LEFT / RIGHT is tried too, carried from the starting
    effect (not from what was passed over). The effect committed is installed
    afresh once more (a second fade, a tail heard while trying is cut): the
    Session has no "put in without telling the engine". **The hold is let go
    by the slot kept in `trySlot_`, after the commit** (letting go by
    `tryRow_`, already -1 by then, left the slot's automation dead in the
    engine for good: found in review, 2026-11-14). A hold is said again with
    every effect tried, and `RemoveInstrument` clears it, so a song sent again
    (a full queue) holds nothing back. `app_test` (`testTryingAnEffectKeepsIt`
    reads `Engine::automationHeld` after every way out, and listens to a
    committed PING's MIX automation), `effects_test` (`testHeldAutomation`).
  - **The list of effects is eight lines, EMPTY and the seven families**
    (`kPickLines`), each a family's name and, from `kPickTypesX`, its types
    in small letters with ", " between ("clean, tape, ping"), the slot's type
    in brass on the cursor's line. In one row at the size chosen if they fit
    `kPickTypesRoom`, else held at LARGE (MOD's five planned at XL), else two
    rows at LARGE (`pickRows`; no planned name needs it, made-up ones as long
    as SATURATE do). `textsize_test` (`testTheListOfEffectsHoldsTheLaterTypes`)
    holds the fifteen planned types to it at every size.
  - **The heading's TYPE row shows as many types as fit** about the one the
    slot is, "..." (`kTypeMore`) for the rest (`typesInView`, in the room the
    row keeps for the LEFT / RIGHT key held or not). **Which are in view is
    LARGE's at every size** (`typeRowView`): MOD's five do not all fit at
    LARGE, and SMALL showing more was other words (`textsize` fails that);
    drawn at the size chosen where they fit it, else held at LARGE, its row
    with it (XL). A family of one shows its type all the
    same where it is not the family's word (MOD: TYPE CHORUS), with no key.
  - **L1 / R1 is the track before / after** (owner, 2026-11-14: a send on
    another track cost five presses). `FxScreen::stepTrack`: it stops at the
    first and the last track, as the SONG screen's columns do (nothing in the
    program wraps tracks); the line and `last_` stay; an effect being tried
    is called off first. DEVICE keeps its own L1 / R1 (pages, record, slices).
    The list's hints were shortened to fit it (settings, effects, sends):
    `testFxStepsTheTrack` holds every line and column to the bar. **A panic
    puts the track back** (`App::trackBeforeShoulders_`, as it puts back the
    tab and the slices): the four shoulders are seldom down at once, and L1 or
    R1 first stepped the track on the way. The help card does not say L1 / R1
    here: it lists what is true on every screen, and it is full.
  - **Hold B deletes an effect** (a slot's line or a bus's: a bar fills for
    `kDeleteHoldMs`, a second, on the line and the heading, which says
    `kDeletingWords`; a tap does nothing; any other button or a move calls it
    off; pit stop 2026-11-14; `app_test --hands` runs these tests). A tap of B on SENDS still empties both sends,
    and on a setting gives its usual value; a bus's LEVEL goes back to full
    (`effectDefault(kind, i, true)`).
  - A choice has no bar: one piece for each choice, the one it is lit
    (`wideBar`). A value is drawn in the largest lettering that fits
    `kValueRoom` (`Fonts::fit`); `app_test` (`testFxTextsFit`) draws every
    setting of every effect at 33 values and looks at the screen.
  - **In a line of the list a type stands 7 pixels after its family; for one
    too long for that the family is drawn in the type's smaller lettering, or
    the line is held at LARGE** (`FxScreen::lineFonts`: REVERB SPRING is 4
    pixels too long; at XL both are drawn at LARGE). Never close the gap up:
    at 2.9 REVERB SPRING read as one word (`app_test` counts the empty
    columns). A pair too long even with both small at LARGE is drawn as its
    type alone, in the family's lettering (`typeAlone`: DRIVE SATURATE is 4
    pixels over, and the line says SATURATE: the owner is content with that, 2026-11-14). A pair that would end inside the 3-pixel clearance before the mark draws its family small too (REVERB BLOOM, 1.4 in; before batch c3 only one past the mark did). REVERB SHIMMER is drawn as SHIMMER alone, as SATURATE. The tests ask the same
    function. A new type's name is checked there at every size.
  - **The page goes down to `kPageBottom`** (449: 7 above the hints bar, as
    its top is 7 under the top bar; owner, 2026-11-13: it stopped 40 short).
    The same layout, taller: a line of the list is `kLineH` 41 (was 36), a
    setting's row `kSetH` 74 (was 66), the heading `kHeadH` 93 (was 92), a
    send's row `kSendH` 193 (was 128; its value, bar and purple line at
    `kSendValueY`, `kSendBarY`, `kSendLineY`, the last two from its foot), a
    line of the list of effects `kPickLineH` 38 (was 31). `fx_screen.hpp`
    works each out from the foot and `static_assert`s that all four columns
    end on it.
  - What is automated in the scene worked on has a purple line under its row
    and "moves with the pattern", and a purple line under its line of the
    list. WRITE itself is said by the top bar.
  - The places things are drawn are constants in `fx_screen.hpp`, so the
    tests can read the drawn screen (`testTheFxScreenAsDrawn`, with `inked`:
    is this text drawn here in this colour). `app_test --fx` runs just the FX
    screen's tests (about 9 s).
- **LIVE OUTPUT is a thin strip across the DEVICE page, just above the hints
  bar** (owner, 2026-11-14: the half-width box in the bottom right was too big
  and left room unused). `ui::scopeStrip`, its places in `ui.hpp` (`kScopeY`
  416, `kScopeH` 36, its label inside at the left in `kScopeLabelW`). The
  synths' MAIN and settings pages, the sampler and the kit end their controls
  at `ui::kAboveScope` (406) and spread them down to it: MAIN's macros are
  `SynthScreen::kMacroH` 114 (was 70), a settings page's rows `settingH(rows)`
  (four rows 82, three 111, two 170, one 346: however few, they reach the
  strip; no empty band), the kit's lanes `kLaneRowH` 45 (was 37), the
  sampler's settings 109 and its recording's picture `kWaveH` 80 (was 40;
  with no recording its words stand in that room's middle). The line is
  placed by `ui::scopeLineY` (full scale 4 rows inside the strip's edges), **its height bent, not to
  scale** (`scopeBent`, the power `kScopeBend` 0.4; owner, 2026-11-14: to scale a quiet recording was
  half a pixel): 30 dB down stands 3 pixels, half scale 11 of the 14; louder is never smaller. A box taller than `kBoxUsualH` (70) moves its value down by half
  the extra (`boxValueDrop`); at 70 and under nothing moves. The envelope
  pages never had the scope and do not. `scope_test` reads it all off the
  drawn screen in BROWN and PAPER.
- **The screen is drawn only when it can have changed** (owner, 2026-11-15: save the battery,
  "I don't want the user to notice"; `app/redraw.hpp`). main.cpp still goes round every 16 ms,
  reads the buttons and runs `App::update`; only `App::draw` and the present are left out on a
  pass with nothing to show. **The picture must be exactly what drawing every frame shows, and a
  button is drawn on the pass it is pressed.** A pass is drawn on a STIR (`Stir` in redraw.hpp):
  any SDL event but a stick's motion, a button held, the stick pushed, the song playing; the
  engine's readouts moved since the last picture (where the song is, the LIVE OUTPUT samples,
  every effect meter, MIDI notes and keyboards: `App::readouts`); `App::stirring` (the volume,
  the App's message, the panic notice, the update's answer, plus `Session::stirring`: a fit, a
  recording, a bounce, WiFi on, and the screen's own `Screen::stirring`: a line said for a moment,
  a line over a recording, the list's resting row, the button test). **And the pass after any
  stir** (`kStirAfter`: what ends a timer is drawn), and `kKeepDrawingMs` (2 s) after, as a margin.
  The controls setup, a text size or theme changed, and every still (`--shot`, `--smoke`) draw
  every pass. `--draw-every-frame` draws as before, for measuring.
  - **A new thing that moves by itself says so** (a timer, an animation, anything read from
    another thread or the clock): a bit, set by the App, the Session, its screen's `stirring()` or
    a readout, **and a part in `tests/idle_test.cpp`'s session** that sets it going and then
    leaves the buttons alone. The test draws EVERY pass into a canvas with the margin at 0 and
    fails a pass left out that is not the last picture shown, pixel for pixel, naming the pass,
    the part and the screen. Then it takes each stir away (`Redraw::ignore`) and must fail;
    PLAYING, RECORDING and PICKER are known to be covered by others where it can be shown (`belts`)
    and are not run. `idle_test --quick` is the session alone (about 20 s); `IDLE_PRINT=PART`
    prints each pass's reasons in a part; `--measure` times a pass's drawing on five screens.
  - The test makes the sound between passes, as the audio thread does while main.cpp waits, and
    holds it up once (`stalled`): a Stop heard three passes late takes the playhead away with no
    button, which only the readouts see.
  - Measured (desk, 2026-11-15): the drum grid left alone, the whole program for 20 s (software
    renderer, dummy sound): 1.1 s of CPU, against 5.8 s drawing every frame. A pass's drawing in
    the test's software renderer: 1.8 to 2.4 ms by screen; deciding whether to draw: 0.001 ms.
    **Not measured on the handheld**, where the renderer and the present are its own.
- **The help card says how hard the program works, above the battery** (owner, 2026-11-15):
  `SOUND 43%  WORST 88%  CPU 37%`. **Every `Engine::render()` is timed** (`LoadMeter`,
  `apotheca/load_meter.hpp`: a steady clock at the call's start and end against its frames at the
  engine's rate; over a second of sound the share and the worst call go into atomics:
  `soundShare`, `soundWorst`, `loadSeconds`). It costs about 50 ns a call (`load_test --measure`:
  0.001% of a 256-frame block). CPU is the whole program from `/proc/self/stat` (utime + stime over
  the wall time over the cores: `app/cpu_use.hpp`), read once a second on the app thread only while
  the card is open, a second after it opens. The App keeps the numbers it draws (`loadShown`), and
  the redraw compares them (`Readouts::load`, `kStirLoad`; a belt in `idle_test`: the card is up
  only while MENU or SELECT is held). The line's room ends 10 pixels right of `quit Apotheca` at
  the size drawn, so it is held at LARGE at XL. `load_test`, `app_test`
  `testTheHelpCardSaysTheLoad`, stills `help`, `help-load`. **Not run on the handheld.**
- **A new `Session` tells the engine what the buses hold** (its constructor).
  The engine starts as a new song in every other way, but with empty buses;
  without this a handheld with no saved song had sends that did nothing.
- **`Project` is about 12 MB. Never put one on the stack**, nor a `TrackData`
  (about 1 MB; measured 2026-11-14: 12,061,000 and 1,005,060 bytes).
  `std::unique_ptr<Project>`. Not even a temporary: `x = TrackData()` is one
  on the stack (`parseSong` took 1 MB of stack that way, and it runs on the
  WiFi server's thread too); copy a blank one kept on the heap.
  `save_test` reads a song on a thread with a 256 KB stack.
- **Every screen's `update()` re-reads held buttons from `input` each frame**
  and drops its own "held" flags when they are not held. Trusting the release
  event left A "stuck down" after a screen change.
- **Undo is a text snapshot of the song** (`songText`), one step per burst
  of edits, 20 deep. Anything that changes a pattern outside the grids calls
  `noteEdit()`. **It puts back the patterns and, since 2026-11-14 (owner),
  each track's effects, sends, sound (preset, settings, macros, a sampler's
  settings), GLIDE FROM and its two MODs, the master's effects and the buses'
  (LEVEL with them)**. Those are seen changing by `Session::soundMark()` each frame, as the
  cell is by `cellMark()`: no screen calls anything (the FX screen is
  untouched; a change is a step from the frame after it). `restoreSounds` puts them back only where they differ
  **as the song file writes them** (`sameAsText`, `effectSameAsText`: `%.6g`;
  a third reads back as 0.333332986, and compared as numbers every undo
  rounded such a setting, send or macro and told the engine, a synth's whole
  patch, the master and the buses on every undo; `testUndoLeavesWhatTheTextRounds`),
  through the engine's usual commands (`restoreEffect`: another kind is
  `Engine::setEffect` then its settings; the same kind, only the settings
  that differ; `SetSend`, `setGlideFrom`, `sendSynthPatch`), never a track
  image and never an `Effect` from this thread. Automation is a pattern, so a
  TYPE change taken back has its lanes back with it. Tempo, mutes, names, the
  kit's recordings and track deletes are not undone. After a restore the
  marks are taken again (putting back is not an edit).
  **Only another song clears undo** (`applyProject`). A resync after a full
  queue, a track deleted and a track given another instrument send the song
  with `sendSong()` and keep the steps: a delete takes its track out of every
  step's text (`dropTrackText`), and another instrument is passed by (its kind
  differs). The delete itself is still not undone.
- **The song's text is made a track at a time** (`SongTextCache`,
  `Session::textNow()`): a track whose text would be as it was when its text
  was made is not written again. **It is compared by what the text reads**
  (`sameForText`, `TextUsed`): everything outside the patterns byte for byte;
  a bar or a lane of automation that had something in it byte for byte; one
  that had nothing only looked at, one side, to see it still has nothing
  (first by one `memcmp` against an empty one: `levelsEmpty`, `notesEmpty`,
  `laneEmpty`). Exact, because `putTrack` writes nothing for such a piece. The
  autosave, the end of a burst of edits and undo all use it, so with nothing
  changed the autosave is a compare of memory. The copies it keeps cost about
  1 MB a track (11.8 MB for twelve). **An undo reads back only the tracks
  whose text differs from the step's, and builds no `Project`** (speed plan
  batch 4: `Session::readStep` into a `StepRead`: the song's own lines, and
  the tracks read into `TrackData` made once and kept, a megabyte a track ever
  needed; `SongReader` reads a part at a time as `parseSong` reads the whole,
  `cutSongAt` gives the parts' places). The text of each track now is what
  undo or redo just made (`patternsNow`), not compared again: **only they call
  `restorePatterns`**. A scene move reads the step whole (it takes the
  sixteenth scene from it). `readUndoWhole(true)` reads every step whole:
  `app_test` (`testUndoIsAsItWasReadWhole`) holds the two the same, and
  `save_test` (`testASongReadInPartsIsAsReadWhole`) the reader. **A track whose
  numbers differ from the step's but whose text is the same is left alone**
  (`patternsSameAsText`: of one kind, only the patterns' own lines,
  `patternText`): a value the text rounds (a keyboard's 90/127) made one undo
  send all twelve tracks whole. `app_test --undo` runs the undo tests alone.
  Do not give the cache a count of changes instead: the screens write into the
  project in many places, and one that forgot to count is work never saved.
- **The song's text is written and read without snprintf or streams** (speed
  plan batch 4): `appendNumber` writes `%.0f`, `%.4g` and `%.6g` to the byte
  as snprintf does (exact 128-bit arithmetic; snprintf where it would not
  fit), and `LineIn` (`app/line_in.hpp`) reads a line as a `std::istringstream`
  of it does, failures included (num_get's gathering, then strtod or its exact
  fast path). `save_test` holds both to the library over millions of values
  and lines (every float was checked once, outside the tests). **`parseSong`
  reads in place** (`Project` is a `ProjectHead` and the tracks; a track is
  made blank piece by piece, `makeBlank`), so it is never handed a second
  12 MB Project. A new format in the song file goes through `put`'s three, or
  snprintf; a new way to read goes through `LineIn`'s operations.
- **Scenes are numbered by their place, so a scene put in moves the ones
  after it** (NEXT SCENE, SELECT + Y: `Session::nextScene`). Whatever holds a
  scene's number must follow: the engine's playing, its patterns and an
  arpeggio that is playing (`Command::ScenesMoved`, next rule),
  `project.playScene`, a keyboard note being recorded (`Session::scenesMoved`
  does those two and counts the move), and the player (`UndoStep::nextScene`
  is how undo and redo know to move them). Its tests: `tests/nextscene_test.cpp`.
- **A scene move is ONE command, and the audio thread copies nothing**
  (`Engine::moveScenes`, which sends `Command::ScenesMoved`). The engine keeps
  each scene of each track as a piece of its own (`Track::scenes`: sixteen
  pointers to a `SceneStore`; `drumScenes`, `notes`, `bars` and `automation`
  read them as the one run they used to be). The command moves the pointers,
  takes in the one scene that is new for each track (made on the app thread:
  `Session::sceneImage`), sets that scene's repeats and steps and the song's
  length, moves the playing, and hands the scene that fell off the end back
  through the trash. **It is all in one block of sound or none of it is**, so
  there is no order to keep and no case that plays a wrong cell. Sending a
  track's patterns one a frame, and "all at once when it must be", are gone:
  do not bring them back. **Anything new the engine keeps for a scene goes in
  `SceneStore`, or is moved by that command** (`repeats_`, `steps_`).
  Measured with 12 tracks (`nextscene_test --measure`, the desk): before, the
  block after the gesture took 2.1 to 3.8 ms with every track at once (0.5 on
  average and 1.3 at worst with one a frame) against a usual 0.21; now 0.30
  to 0.37 against a usual 0.24, **which is what any block costs straight after
  the program has been busy** (0.30 to 0.35 with nothing sent). The command
  by itself is 0.1 to 0.3 millionths of a second for twelve tracks. (Again
  later on a busy desk: 0.28 to 0.33 against a usual 0.22 to 0.24, and 0.28 to
  0.30 with nothing sent; a worst of several ms there is the machine taking
  the thread away, and shows as often in a block with no move in it.) The
  key itself is the program's thread: 1.3 ms, and 11 to 12 ms for its undo or
  redo (the song read back from text), **not timed on the handheld**.
  (2026-11-13, joined with chord effects, a slower run. The test's song has
  the chord track now, so its figures are not the ones above: a usual block
  0.30 to 0.34, the block that takes the move 0.43 to 0.47. With the song as
  it was: 0.27 to 0.31 and 0.38 to 0.45, against 0.26 to 0.30 and 0.38 to
  0.43 for the branch before the join in the same run. An ordinary block of
  twelve busy tracks, timed apart: 0.57 to 0.63 ms before the join, on `main`
  and joined alike. The step clock's three rows of `bench`, four runs each:
  0.80 to 0.86%, 1.29 to 1.41% and 1.24 to 1.31% joined; 0.77 to 0.86%, 1.39
  to 1.42% and 1.24 to 1.35% on `main`.)
- **`nextscene_test` runs a real audio thread** beside 200 press and undo
  pairs, held to twelve blocks past the last key (a busy machine made minutes
  of sound otherwise). It is clean under the thread sanitizer
  (`-fsanitize=thread`, about nine minutes; sixteen with its build, on a busy
  desk, since the chord track and the helpers' round: clean again 2026-11-13, and once more that day with GLIDE FROM, fifteen minutes; clean 2026-11-14 with the scene move's asking again, nineteen minutes for the run alone on a shared desk; clean 2026-11-15 with the chord track on JAX-EP, eleven minutes for the run alone; the test's counted `new` has the kind that gives back nullptr too, which a sanitizer's own does not pass on); run that again after a change to
  what a scene move hands over.
- **Taking a NEXT SCENE back, or putting it back, sends no track whole**
  (`Session::restorePatterns` with a move: `moveScenesHere`, then
  `tellScenesMoved`). It then asks whether the PATTERNS, written as text, are
  the step's (`Session::patternsAre`), not whether the numbers match:
  automation read back from text is not the same float to the last bit, and a
  track found different for that alone had all its patterns sent (a megabyte
  the audio thread copies). The patterns alone, not `patternsNow()`: that is
  the whole song's text, and a knob turned since the copy is in it. Only if
  they differ (nothing in the program does that without a step of undo of its
  own) is a track put back and sent whole, as an ordinary undo does.
  `Session::imagesSent()` is how the tests see that none went.
- **Where a scene move cannot be "the song played straight through"**: the
  copy is playing in its own turn (the scene it was made from has had its
  plays this time round) and is taken back. The playing goes to the scene it
  was copied from, same step, bar and time round, so that scene is heard for
  longer than its plays. Nothing is cut and no other scene's cell is played.
  Everywhere else the tests hold the sound to 0 difference: beside the song
  left alone while one scene goes round, and beside a song built with the
  copy in it while the song plays through (press, undo, redo; also with a
  later scene playing, and with a real audio thread making blocks back to back).
- **A copy taken back during the LAST STEP of the scene it was copied from
  is mended in the engine** (found in review 2026-11-13, mended 2026-11-14).
  That step has asked a step ahead for the early notes, hits and chords of the
  bar that follows, which was the copy. Everything asked ahead is marked
  (`ahead` on a waiting note, a drum hit waiting, an arpeggio, a PAIRED
  `Coming`; `asking_` while it is asked), and each track keeps where it was
  asked from (`askedFrom`, `askedBar`) and what the asking changed
  (`strokesBefore`, `lastChordBefore`, `chordAskedBefore`). `ScenesMoved` ends
  with `askAgain`: where another scene's bar follows now, the marked things
  are dropped, those three put back, and the step asked again from the new
  bar (`askAheadHits`, `askAheadNotes`, the same code `fireStep` asks with),
  as far into the step as the playing is. A new step clears the marks first.
  **One thing it cannot mend**: an early note of the copy already heard
  (here from 61.7 of the step on: 0.11 from the song left alone with the chord track on JAX-EP; it was 0.28 on PHAZED),
  and the next scene's notes whose moment has gone by are not played late.
  `nextscene_test` `testTakenBackOnTheLastStep` (in ctest) holds 0 up to there
  for `fill` and for a song with more asked ahead (`addEdges`);
  `nextscene_test --edge` prints the figures. **One line no test reaches**
  (its mutant survives): `GlideTrail::aheadIsNow` in `fireStep`, clearing the
  PAIRED marks at a new step. Kept: a mark left on would let a move forget a
  moment's own keys, but the keys begin before any note of their moment can
  ask, so no song found shows it.
- **What the program remembers it sent is set only when the send went**
  (`Session::syncSong`: `sentSceneCount_`). Set before, a scene move lost to a
  full queue left the engine with the old song length for good: the song sent
  again found nothing to send. `nextscene_test` fills the queue to hold it.
- **`sceneIn` (engine.cpp) is the one place a pattern is read into the
  engine**, for a whole track (`SetTrackImage`) and for one scene. A new
  field of a note or a hit is copied there, once; `sceneInto` (session.cpp)
  is the one place the app writes a scene out for it. A chord's `play` and a
  glide's length go through both (`glideKind`, `chordPlayChecked`).
  `nextscene_test` hands a scene over with them beside the same notes written
  by commands, and beside the bars the piano roll sends.
- **What plays while a scene moves goes with it, and nothing of a chord is
  worked out from a scene's number** (the join of chord effects and the scene
  move, 2026-11-13). `chordSeed` takes the bar of the CELL, not the bar of the
  song: with the scene's number in it a humanised chord rolled another way
  after NEXT SCENE, and no song left alone or built the finished way matched.
  So a chord effect in scene 2 or later rolls other numbers than it did
  before the join (scene 1 is the same to the bit). `Track::Arp::slot` is a
  bar of the song, so `ScenesMoved` moves it by the rule the playing moves by;
  an arpeggio whose scene has no place to go to (the sixteenth when one goes
  in, the one taken out) stays and looks whether its chord is still there
  (`Arp::look`). Waiting notes, `chordAsked`, `strokes` and the `GlideTrail`
  hold no scene number: time is steps since Play. **Anything new that the
  engine keeps while a chord plays either holds no scene number or is moved
  there.**
- **The song `nextscene_test` plays (`fill`) has a track of chords strummed,
  as arpeggios, humanised, ratcheted, thrown, in every ORDER, and a glide of
  every length, on a JAX-EP track (six voices: its chords and glides steal; `testTheChordTrackIsHeard`) whose GLIDE FROM is PAIRED**, so every proof of the sound has them in flight when the
  scene moves. The proofs marked `sound` in its table run again with the
  tracks on three helper threads, and the song left alone is held to the same
  sound on none and on three. `nextscene_test --only NAME...` runs just
  those; `--times` says how long each takes (the whole is near its two
  minutes on a busy machine: put a long new proof in the helpers' round only
  if it earns it).
- **Ask `Session::playScene()`, never `engine.playScene()`, for the scene
  that is playing.** Until the engine has heard a scene move (its next
  block) it still says the old number: the
  playhead left the grid and WRITE went into the scene left behind. The
  engine counts the moves it has taken in the same atomic as the scene
  (`Engine::playScene(&heard)`), so the two are never from different moments.
- **The SONG screen's cursor is a scene's number too** (`SongScreen::scenesMoved`,
  called by `App` after the gesture, an undo or a redo).
- **FM-6's DEVICE pages** (2026-11-15; `fm` in `app/synth_spec.cpp`): VOICE AND LFO, OPERATOR 1 to
  6, PITCH ENVELOPE. **An operator's page is all of it, its ON first**: 22 boxes, six rows.
  - **An operator's ON is a bit of OPERATORS ON** (engine id 145, bit 6 - n): on a page it is a
    setting of its own past the synth's (`kSettings + n - 1`, `SynthSpec::bitOf`). **Read and set any
    setting a page shows through `settingValue` / `settingWrite`** (synth_spec.hpp), never
    `values()[id]`: the screen, `Session::setSynthSetting` and the WiFi page (`designState` gives
    `shownSettingCount` values, the ONs as 0 / 1, so the page's script needed nothing) all do.
    OPERATORS ON itself is on no page.
  - **COARSE and FINE read with their operator** (`SynthSpec::readingIn`, through
    `settingReading`): a ratio, or under FIXED a pitch in Hz; FINE says where the two land. The WiFi
    page's list of readings is `settingText`'s, which reads COARSE alone as a ratio.
  - **Six rows stand `kTightGap` (3) apart** (`SynthScreen::settingGap`): at 6 they were 52 high and
    a value stood on its bar even at LARGE. `settingY` shares the leftover pixels between the gaps,
    so the last row of any page ends on `kAboveScope`. **At XL those boxes are drawn at LARGE**,
    names and values (`boxFontsFor`: the value would come within `kValueClear` of the bar);
    `textsize` holds every FM-6 setting at its least and most to it.
- **A new synth track tells the engine its first preset** (`Session::addTrack` calls
  `sendSynthPatch`). The engine makes a synth on its start patch, which is no preset: JAX-EP and
  FM-6 played it (a plain saw, one sine) under a MAIN that said A1 or BRASS 1 until a preset was
  picked or the song sent again. (QMOD made itself on its first preset, which hid it.)
- **ROLL is PHYSIC's alone** (`SynthSpec::roll`, null for the others; `Session::rollSynth`): Y on
  PHYSIC's MAIN, not on its pages and not with A held, and the WiFi page's ROLL A NEW SOUND (`roll N`,
  shown only for a spec with a roll: `"roll":true` in the page's spec). It went with QMOD on
  2026-11-15 and came back for PHYSIC (the plan's call 10). It rolls STRIKE, HARDNESS, POSITION,
  DECAY, MATERIAL and STRETCH by a hash of the seed and each one's place, never a generator walked,
  inside the MODEL (a string mostly plucked, a bar mostly struck, either breathed on, NOISE, one time
  in eight), and batch 10's PAIR (half the time), MOTOR (a quarter), BUZZ (a fifth) and BODY MIX;
  MODEL, BODY, RELEASE, the levels and the dice stay. A change of the sound like any other: one step of undo (`soundMark`). On a synth without
  a roll Y does nothing on DEVICE, and the page's `roll` changes nothing. The bar says `Y new sound`
  on MAIN off the macros only (on a macro, B and HOLD X fill it: `testPhysicsHintsFit`).
- **PHYSIC in the app** (2026-11-16, `docs/plans/physic-plan.md` section 15; `ps` in
  `app/synth_spec.cpp`, `app_test` `testPhysicOnATrack` and `testPhysicsHintsFit`, `design_test`
  `testPhysicFromThePage`, `textsize`, `scope_test`, `modapp_test`). `TrackKind::Physic` appended, its
  row LAST in `kInstruments` (word `physic`, presets `NAME.physic`), `TrackData::physic` a
  `PhysicPatch` of 26 (`physic::kSettingRoom`), 23 used since batch 10 (2026-11-16, section 16):
  `settingCount` and `presetSettingCount` are 23, and a song's `settings` line holds 23. A new track is
  on Marimba, preset 0.
  - **Its `loadPreset` puts preset 0's sound under a player's own first**, then the file's values
    (`ps::loadPreset`): a preset saved before a setting was appended plays that setting as the
    factory has it, not as the last preset left it. (Phazed's, JAX-EP's and FM-6's copy over the track.)
  - **Five pages, SOUND, RING, PLAY, DICE, BODY** (BODY: PAIR, MOTOR, BODY, BODY MIX, BUZZ), two rows
    at most, so nothing on them is held at LARGE.
    Every setting reads in its unit: DECAY and RELEASE as times (RING at 100), POSITION as a share of
    the way along or "1/n" on the one setting nearest that fraction, FINE and DICE TUNE in CENTS,
    LEVEL in DB, 0 as OFF on VELOCITY and the dice; PAIR in CENTS, MOTOR in HZ, BUZZ in DB under the
    note's loudest, BODY MIX a share. `readingIn` reads HARDNESS under MALLET as the
    mallet's contact at full velocity (8.0 MS to 0.30 MS), under NOISE as the burst's length (2.0 S to
    2 MS), and STRETCH on STRING as B (`B 0.0005`,
    under a ten-thousandth `B 8.0E-05`); the WiFi page's readings are `settingText`'s, every one of them
    (`pageReadsAsDevice`), STRETCH as on a bar.
  - **OCTAVE (macro 7) is a choice** (`macroIsChoice`): no MOD on it, no HOLD X on its box. The other
    seven take one.
- **The list of instruments stands higher when it must** (`SongScreen::listTop`): six rows and what
  goes said under them (another instrument for a track) reached past the hints bar from `kListY`
  (70), so the box rises to end `kListBottom` (8 above the bar); adding a track it stands at 70 as
  before.
- **Hints must fit.** The hints bar drops what does not fit; do not rely on
  that, shorten the words. The bar draws `HOLD` as `HLD`, `SELECT` as `SEL`,
  directions as arrows, and the one word `dice` as a die (`ui.cpp`): write
  hints in full. `ui::gHintsOver` says how many pixels the last bar was
  short of holding every hint and the help: `app_test`
  (`testTheHintsFitAndSayWhatTheyDo`) holds the SONG screen and the drum grid
  to 0 with either help (pit stop, 2026-11-14: SONG's cell leaves mute and
  solo to the head, the drum grid lists a hit's own jobs on a hit and the
  brushes on an empty step). JAX-EP's MAIN and five pages are held to 0 at
  every stop, A held and not (`testJaxepsHintsFit`). **The piano roll on a note is still 48 to 69
  pixels over** (its help is not drawn): not mended.
- **With X held on a hit, a note or a chord, each button's chip stands under
  the field it sets** (owner, 2026-11-12; `ui::hintsUnder`): no word, the
  field's label says it; the dice alone at the left (a chord: at the right). On a note the
  fourth chip is L1 / R1 under GLIDE FROM, and NUDGE, which nothing sets there, is not drawn
  until X is let go. **The chip and the field take their x from one
  constant** (`kFx...X` in `pattern_layout.hpp`; `kStripHeld...X` and
  `kChordFx...X` in `note_marks.hpp`): never write the number twice. On a
  chord LEFT / RIGHT is under the picked effect's large value at the left
  (`kChordFxSetX`), UP / DOWN under the list's names, and the dice at the
  right-hand end (owner, 2026-11-13). The chips are the bar's own (`hintChip`): do not draw
  another kind. The help's words need eleven pixels clear after the last
  chip, or they give way: they do on a drum hit and a chord, where THROW's
  chip is at the right, and on a note, where GLIDE FROM's is. `tests/under_test.cpp` reads all of it off the drawn
  screen, and that the plain bar is back, pixel for pixel, with X let go.
- **The device writes SEL, never SELECT** (owner, 2026-11-12). A hint's key
  and a help-card key are written in full and drawn short (`keyWords`); any
  other text on the screen is written `SEL` ("also SEL + L1 / R1"); a button's
  name goes through `buttonLabel`, never `buttonName` (that is the word in
  controls.txt, which stays SELECT). The web pages and README keep the full
  word. `ctest -R words` (`tests/words_test.sh`) reads the sources and fails
  on a string that would show it spelled out; it plants its own faults at the
  end and must catch each (add a plant with a new rule).
- **The lettering's size is a setting, and the program holds TWO sets of
  fonts** (owner, 2026-11-12; SET page, TEXT SIZE: SMALL, MEDIUM, LARGE, XL;
  `kTextPoints` in `ui.hpp`; kept as `text N` in settings.txt, the numbers
  appended, no line reads LARGE). A screen is handed the chosen set;
  `fonts.fixed()` is the other, always LARGE, which every screen was laid
  out at. `main.cpp` loads the chosen set again between two frames when
  `Session::textSize()` changes.
  - **The tight rows are drawn with `fixed()`**, so they are the same pixels
    at every size: the top bar (and the tempo's box), the hints bar and
    `hintsUnder` (inside `ui.cpp`: hand them the set you were given), the
    strip under the drum grid and under the piano roll (`tight` in both),
    anything written over a little picture (a
    recording's seconds in the list), the heads of narrow SONG columns, the
    controls setup. The tests that read a strip or a bar off the screen
    draw at LARGE and hold at every size for that reason.
  - **Everything else follows the setting, and a new text is checked at XL.**
    Where it would not fit its room at XL it is drawn with
    `fonts.within(&Fonts::small, text, room)`: the chosen size if it fits,
    LARGE if not. Never cut it shorter than LARGE cuts it, never move it,
    never change the words. A name cut by width is cut with the font
    `within` gave. A text that stands on a line is placed from the line
    (the roll's OCTAVE: `line - capHeight`), not at a number that was right
    at 12 pixels.
  - **A held text takes its row with it.** What shares a line with a text
    held at LARGE is held too (a song's name and OPEN NOW > REVS; a SET
    row's value and its note; all sixteen step numbers of a roll's bar of
    three figures), and it is placed with the capitals' height of the font
    it is DRAWN in, not the chosen one's. A text that is made at run time
    (a name, a number, what a toast says) is checked at its longest: no
    still shows a ten-W track name or scene 10's 127 STEPS OF 128.
  - **A font loaded again lets its old letters go** (`Font::sheets()` counts
    them; a test holds it), and the fonts are unloaded before the renderer
    is destroyed (`main.cpp`'s end).
  - **`ctest -R text` holds it.** `textviews` has the program draw every
    still it knows at all four sizes (`--texts FILE` writes down each text
    and rectangle; `tests/textfit.hpp` measures them against LARGE: the same
    words, no text out of its box, into its neighbour, or into the bar beside
    it). **A new `--view` goes in `kViews` in `tests/stills.hpp`**; the
    test reads `main.cpp` and fails on one that is missing. `textsize` draws
    the screens the stills do not reach (every FX setting, long names,
    twelve tracks) and checks the tight rows pixel for pixel. A new screen or
    state with words in it gets a `holds(rig, "what")` there.
  - A still at a size: `--text-size xl`.
- **The colours are a THEME** (owner, 2026-11-13; SET page, THEME: BROWN,
  BLUE, GREEN, BLACK, PAPER, GREY; `kThemeTable` in `ui.hpp`; kept as
  `theme N` in settings.txt, the numbers appended, no line reads BROWN, which
  is the look as built to the number). `ui::kBg` ... `ui::kStop` are
  references into `ui::gTheme`, which `App::draw` (and main.cpp, before the
  controls setup) sets from `Session::theme()` each frame. A theme changes
  colours only, never a size or a place.
  - **A new colour goes through the ten names (or `faded()` of one): never
    numbers of its own.** Each name is a JOB (`ui.hpp` says which), kept in
    every theme; pick by job, not by hue.
  - **A new mark is checked on a light theme** (`--theme paper`). A light
    theme keeps the order of the surfaces (bg the deepest, then panel,
    panel2) with its line darker than them; text on a chip filled with an
    accent or with text is written in `kBg`; a wash is `faded(kBg, ...)`.
    Anything that only works light-on-dark breaks there.
  - **A thing drawn as an object, not a surface, asks `ui::lightTheme()`.**
    The roll's keyboard is the one: by job its white keys were dim and its
    black keys the background, a negative on PAPER and GREY. On a light
    theme the white keys are panel2 and the black keys text.
  - A new theme meets the contrast in `theme_test` (text 7, dim 4.5, an
    accent 4.5 on every surface and under bg letters, accents and the two
    letterings 20 apart in delta E). BROWN is measured, not held to it (its
    dim is 4.16 on panel2), and stays as it is. **Every theme, BROWN
    included:** the cursor's colour 1.4 or more in lightness from purple and
    from dim (PAPER and GREY were 1.12 and 1.01 at first: the cursor's
    outline and the purple line beside it read as one dark border), and the
    four accents 15 apart in delta E as a reader without red or without
    green sees them (simulated, not seen by a person).
  - **`ctest -R theme` holds it.** `themeviews` has the program draw every
    still in all six (`--themes PREFIX`) and finds, by pixels, any colour
    that is not the theme's or one of its laid over another (`Explain`); a
    new alpha given to `faded()` goes in its list there. A still in a theme:
    `--theme paper`.
- **The SET page shows twelve rows and moves one row at a time**
  (`SettingsScreen::firstShown`, `rowNowY`): thirteen since THEME. A new row
  costs nothing more than its place in `Row`.
  A row's bar starts after its value's words and ends short of what is said
  at the right, at every text size (`SettingsScreen::barPlace`: BOUNCE's
  WORKING 37% / TAIL 12 S; VOLUME's and STICK's stay at 250, 110 wide).
- **A check of what is DRAWN uses `tests/canvas.hpp`**: a 640x480 screen in
  memory with the program's lettering. `ui_test` looks at the hints bar's
  pixels and `app_test` at the help card's boxes; a check of the text alone
  passed with the bar drawing the key as written.
- **Hold X + Y is the dice** for what a hit or a note does (`fxRoll` on both
  grids). **Hold X + A is THROW** (`fxThrow` on both): OFF, ECHO, SPACE, BOTH.
  The check for X held comes before A's own jobs, so A alone still places.
  **The dice do not throw**, and do not take a THROW away (owner's decision).
  **Hold X + L1 / R1 on a note is the track's GLIDE FROM** (the glide rules, below).
- **X HELD hears a recording until it is let go, to its end** (owner,
  2026-11-14): the SAMPLER and KIT pages, their list, the drum grid's lane
  name and empty step, the FX screen and the roll's empty place on a sampler
  track. A tap still sounds `Session::kAuditionMs` (350); **the built-in
  kit tapped is heard whole**, as before (its cymbal is 1.8 s): `letGoHeard`
  stops a tapped lane only when it holds a recording. Screens call
  `Session::auditionHeld` (a synth key, held at `kHeldOn` in `sounding_`) or
  `hearLaneHeld` (a `Trigger` with `e = 1`); **`App::update` lets go**
  (`keepHearing`) when X is up, another screen is up or another track
  picked; Play, a new song and panic let go too. A drum lane's is stopped by
  `Command::StopAudition`, which fades (3 ms) only the voices and waiting
  ratchet pieces a held audition started (`HitPlay::audition`; a hit that
  carries the sound on takes it over; a lane plays one sound, so any
  audition chokes the hit before it anyway, joined or not). On a hit, X held stays the step
  effects' hold. The line over the picture stops with it (`heardEnds`).
  `tests/hear_test.cpp`. The SONG screen's X solos the track (it heard it until 2026-11-15).
- **A hit, a note or a chord placed starts at `kPlacedLevel` (0.9, `project.hpp`; owner,
  2026-11-15: it was 0.8), the brushes too, and every screen's X or A hears at it** (the KIT,
  SAMPLER, synth, FX and MOD pages, the designer page's keys): what is heard is what is placed. A
  new way to hear a sound takes it, never a number of its own (`app_test` `testHeardAsPlaced`). A
  keyboard's notes keep their own velocity; the demo song and the dice keep theirs.
- **The sampler's END is setting 7, after LEVEL (ids are appended), and the page shows it beside
  START** (owner, 2026-11-15): the page's order is not the ids' (`SamplerScreen::settingAt`: PITCH,
  TUNE, START, END / ATTACK, RELEASE, LOOP, LEVEL; `goTo` takes a place). A note plays START to END
  and stops, or with LOOP goes round between the two; at 100% it is the engine as before, to the
  bit. **They never cross**: the screen holds each a percent (`sampler::kLeastSpan`) short of the
  other, and `sampler::playedEnd` holds what a song file says (the reader stores it held too). The
  song file writes END only when it is not 100, after LEVEL. The picture marks both (`endMarkX`)
  and lights only what a note plays; the line over it ends there (`Playhead::to`). Not a MOD
  target: no sampler setting is. `engine_test` (`testTheSampler`, `testTheSamplersEndInAnyBlocks`),
  `sample_test` (`testTheSamplersEnd`).
- **Sample names from `wavNames` have no `.wav` ending.** The file may end
  `.wav` or `.WAV`: whatever opens or names one tries both (`readRecording`,
  `recordingThere`, `deleteSample`).
- **The slice view is a mode of the drum grid** (`PatternScreen::slicing()`,
  drawn by `pattern_slices.cpp`), not a screen of its own, so every grid
  gesture works in it unchanged. A hit's slice is its `start`; there is no
  separate slice data. A lane has 16 slices, or its FIT in steps if longer.
- **COPY A BAR: hold L1, R1 copies the bar shown, A pastes over it** (owner, 2026-11-17: "copying
  steps 1-16 to paste on 17-32"; `Session::copyBar` / `pasteBar` / `BarClip`, `copyBarHere` /
  `pasteBarHere` for the screens; `tests/copybar_test.cpp`). The drum grid, its slice view and the roll.
  - **The clipboard is memory only**, never in the song file; a new song keeps it. It holds a drum
    bar (`DrumPattern`, every field) or a note bar (`NotePattern`, chords and their effects), the bar's
    16 steps of every automation lane, and where it came from.
  - **A paste REPLACES the bar** and is **one step of undo of its own** (as NEXT SCENE: the burst
    before it ends, and `settled_` is taken after it, so an edit straight after is a step too). Onto a
    bar that is already the same (compared field by field; automation too on the same track): nothing,
    no step. The engine gets the track's image (`sendTrackImage`, as `setCellBars`).
  - **Drums only onto a drum track, notes onto any synth or sampler track**; a track with nothing, or
    a bar past the cell's end: refused. **A note over the bar line keeps its length, held to the end of
    the cell it lands in** (as the roll's `room`). **Automation only onto the SAME track** (another
    track's lanes are other knobs; its own are left).
  - **The gesture**: L1 must go down FIRST, with nothing else held (`l1First_`; not with X held: X's
    L1 / R1 are the step effects, GLIDE FROM, the feel). R1 then copies (no brush, no carry), A pastes
    (no hit or note placed; on the grid `aSpent_`, so A's hold nudges nothing). **L1's own tap acts on
    its let go**: the drum grid's slices (`l1Used_`: any other button pressed while L1 was held cancels
    it) and the roll's zoom (`zoomUsed_`, set by a copy or a paste). The App tells both screens when
    another shoulder goes down with L1 held (`shouldersPressed`), so a panic's let go of L1 opens no
    slices; **a panic begun L1, R1 puts the copy back** (`clipBeforeShoulders_`, `barCopies`).
  - What was done is said in the App's box above the bar (`Session::say`, taken by
    `App::takeSessionSaid`): BAR 1 COPIED, PASTED ON BAR 2, BAR 2 IS THE SAME ALREADY, NOTHING
    COPIED YET, DRUMS GO ON A DRUM TRACK, NOTES GO ON A NOTE TRACK. Each fits the box at LARGE
    (`testTheWordsFit`).
  - **The hints are in the bar only while L1 is held** (R1 copy bar, A paste bar; the roll also
    UP / DOWN zoom): the plain bars of the drum grid and the roll have no room. Not on the help card
    (full). Stills `copy-bar`, `copy-bar-notes`, `copy-bar-refused`.
  - Mutants: every one tried in `copybar_test` dies but the App's two `shouldersPressed` calls (the
    panic's, and "another shoulder with L1 held"): each is the other's belt, and the two taken out
    together die (`testTheSlicesAndL1`, the panic begun L1, L2, R2, R1).
- **A drum hit that starts where the lane's sound has just got to carries it
  on** (`DrumMachine::trigger`, within 1.5 ms, never from the very top). That
  is what makes slices in order seamless. Do not "simplify" it away.
- **Step effects are four small values on each drum hit** (`StepFx`, drums.hpp):
  pitch, ratchet, reverse, cut. Hold X on a hit sets them. In a song file they
  are `stepfx` lines (`fx` is an effect in a slot: the first try clashed).
  A ratchet waits in one `Pending` slot that re-arms itself.
- **FEEL is two amounts on each drum LANE, and it is live** (owner, 2026-11-14; built on branch
  `feel`, 2026-11-15; `engine/include/apotheca/feel_rule.hpp`, `LaneFeel`): HUMAN TIME and
  HUMAN LEVEL, 0..100, a chord's six clicks (`kAmountAt`) and a chord's two "mosts"
  (`kHumanTimeMs` 12, `kHumanLevelMost` 0.15). Nothing is written into the hits. A hit on a
  main beat (steps 1, 5, 9, 13: `feelOnBeat`) moves half as much (`kFeelBeatShare`), in time and
  in level. **Nobody has listened to it**: the tests measure where each hit lands and how loud.
  - **Every roll is worked out from where and which time** (`feelSeed(track, lane, bar of the
    cell, step, steps since Play)`): never a generator walked, never the scene's number
    (`nextscene_test`'s `fill` has three felt lanes, and fails if the seed takes the song's bar).
  - **A lane with both at 0 takes the path it always did** (`schedule`, `playHit`): old songs
    are the same to the bit (`effects_test --print` the same before and after, and a busy drum
    song with nudges, ratchets, chance, throws, a DUCK, tempo changes and a Play-once gave one
    checksum on `main`'s engine and this one).
  - **A lane with HUMAN TIME has every hit asked a step ahead** (`askAheadHits`: the roll may
    bring it early), and the track keeps that it was (`Track::feelAsked`, the step's number, as
    `chordAsked`): the hit is not begun again on its own step, whatever the lane says by then.
    Without it a feel turned while the song played lost hits or played them twice
    (`testTurnedWhilePlaying`). Play clears it; a track taken away puts the lane at rest.
  - **Everything a felt hit times is a whole number of samples** (the wait, `std::ceil`; a
    ratchet's gap, `std::floor`), so it is the same at any block length. A plain lane's nudge and
    ratchet are still a part of a sample long, as they always were: `feel_test`'s block test
    gives the lanes with HUMAN LEVEL alone no nudge and no ratchet for that reason.
  - The song's first step has no step before it: an early roll lands on the top. Played once
    (a bounce, AT END STOP) the last step asks nothing of the first, as for chords.
  - **Asked again** (a scene move, AT END switched on the last step: `askAgain`): a felt hit
    counts from the sample its asking step began on (`std::floor(elapsed)`), so it comes on the
    very sample it would have. One that has not sounded gives its mark back and may be asked
    again; **asked again after its early moment has gone by, it comes on its step** (a downbeat
    a few ms late, not lost: `schedule` returns false, the mark is not set). One that has
    sounded (`Pending::begun`) keeps its mark; its ratchet pieces still waiting go, as an early
    hit's always did. **In a scene move the old scene's hit and the new one's on the same lane
    and step are one roll** (the seed has no scene in it), so on the grid the hit is on the same
    sample sounded or not, and giving the mark back shows only when AT END is switched: that
    line's mutant is killed by `testAskedAgain`, not by any scene move (`nextscene_test` does not
    see it; `feel_test` `testASceneMoved` holds the rest of the path beside a song built with the
    scene in it: the sample counted from, and a felt hit WRITTEN early whose moment has gone by,
    which is not played late, as a plain one is not).
  - **CHANCE is still the walked dice** (`dice_`), so a felt hit with a CHANCE that is asked
    again rolls its chance again (as a nudged-early one always did), and from then on the song is
    not "the song played straight through" in its chances. `fill` keeps the feel off its CHANCE
    lane for that reason. Not mended: making chance a roll by place is a change to every song.
  - **A run of slices moves as one, through the bar lines inside its cell** (`feelRunBack`): a
    hit that starts further into its sound, with a hit on the step before it, takes the TIME of
    the hit its run begins with, so slices in order still carry the sound on (`testSlices`: the
    same samples, sooner or later, to the bit). Its LEVEL is its own (tested step by step). Moved
    each by itself, every slice met the sound up to 24 ms off and cut it. **The run goes back
    over a bar line of the same cell** (review, 2026-11-15: a two-bar break in order was cut at
    its inner bar line in 4 rounds of 6; `testSlicesOverABarLine`), never from the cell's first
    bar to its last: where the cell goes round a new run begins. `schedule` reaches the bars
    before with `&scene - bar` (a cell's bars stand in a row in `SceneStore::drums`): **whoever
    calls `schedule` hands it the bar of the CELL and that bar's own `DrumScene`.** The walk is
    a step a slice: at most 127 for the last slice of an eight-bar break.
  - **A tempo change keeps a felt hit's wait whole** (`Pending::whole`, `SetTempo`: the wait
    `ceil`, a ratchet's gap `floor`): scaled to a part of a sample, the one hit waiting on each
    felt lane came a sample sooner or later by the block length (review; `feel_test`'s block
    test turns the tempo five times, at blocks of 1 to 333 and on three helpers).
  - HUMAN LEVEL is held to 0.02..1: **a hit written at full only gets quieter** (as a chord's).
  - Three waiting slots a lane are still enough (a ratchet running on, this step's hit, the next
    step's): HUMAN TIME is never more than a quarter of a step (`kFeelMostOfStep`; at the
    program's fastest, 240 BPM, 12 ms is under a fifth).
  - **The app**: `TrackData::laneFeel`, `Session::setLaneFeel`, `Command::SetLaneFeel` (a = lane,
    b = time, c = level). `sendSong` tells every drum lane's, at rest too. In a song file
    `feel LANE TIME LEVEL`, a line of the kit's own for each lane, **left out when both are 0**;
    under a synth track it is not taken; the numbers are held as doubles first. **In undo by
    `soundMark` / `restoreSounds`** (as GLIDE FROM: not a pattern), the turns of one hold one step.
  - **The gesture: X held on a lane's NAME** (`PatternScreen::feelHeld_`, as `fxHeld_` on a
    hit): LEFT / RIGHT HUMAN TIME, UP / DOWN HUMAN LEVEL, a click each, stopping at the ends
    (`app/lane_feel.hpp`); Y the dice (OFF, 10 or 25% of each, never none, never what it was);
    A this lane's feel onto every lane (from OFF: all plain). **X there still hears the drum,
    exactly as before** (`hearLaneHeld` on the press, `keepHearing` lets go): the one hold is
    both, and a turn of the feel sounds nothing (the feel is the playing pattern's). L1 and R1 do
    nothing while it is held (kept for SWING). **Y let go after X is not a tap** (`yDidSomething_`
    is set by the dice): it rolled the lane's hits, and on a hit it stepped the chance. **A or Y
    pressed while X was held does nothing of its own if X is let go first** (`aSpent_`,
    `ySpent_`, until the button comes up; a hit's THROW and dice too): with A still down the
    D-pad turned the lane's volume, with Y it rolled the lane's hits (review, 2026-11-15;
    `testXLetGoFirst`). The directions move the cursor meanwhile.
  - **The name's one-line summary gives way to what is said on its row** (`said()`: why L1 opened
    no slices on a built-in drum): the two were drawn one over the other.
  - The strip's fields and their chips share `kFeelTimeX`, `kFeelLevelX`, `kFeelAllX`
    (`pattern_layout.hpp`); EVERY LANE reads COPY, or THE SAME once they are. With X up the name's
    strip says the feel in one purple line. **The mark** (`drawFeelMark`, both views): at the left
    of the name, the upper piece HUMAN TIME, the lower HUMAN LEVEL, purple.
  - Tests: `tests/feel_test.cpp` (the engine: the rule by hand, each hit's place and level,
    blocks, helpers, Play twice, once, turned while playing, slices, DUCK and THROW, asked
    again) and `tests/feelapp_test.cpp` (the gesture, the strip off the drawn screen, the song
    file, undo, a song sent again, a bounce, X still hearing, the stick's pace, X let go first).
    `feel_test testTime testSlices` runs just those. `textsize` holds the name's line and the
    held strip as tight rows. SWING is not built.
- **CHOKE is a group on each drum LANE, two of them** (owner, 2026-11-15: "choke the open hi hat with a
  closed one"; `DrumMachine::setLaneChoke`, `kChokeNone` / `kChokeGroups` and `chokeGroupOf` in
  drums.hpp). A hit on a lane in a group gives every OTHER lane of that group still sounding the
  fade a lane already cuts its own last hit with (`chokeStep_`, 3 ms), in `trigger` (`chokeOthers`).
  **Every trigger counts**: a ratchet's pieces, a hit that carries its lane's sound on, a hit by
  hand; the thrown copy goes with its voice. **Hits that start on the same sample do not cut each
  other** (`Voice::born` against `renders_`, counted by `render`): both were written, and lane order
  must not decide. A lane cuts itself as it always did, so a group of one is no group; nothing
  grouped is the sound as before to the bit (`effects_test --print` the same, `speed_test` passes:
  the runs see a choked voice's fade as any other). A group set cuts nothing by itself; a track
  taken away is in none. **A new kit has its two hats in CHOKE 1** (`chokeTheHats` / `newKitChoke`
  in project.hpp, `Session::chokeNewKit`): a drum track added, a new song's track 1, a track given
  the kit, the first song on a card, the demo. A blank `TrackData` (a song read from a file) has none.
  A new song is still not worth a file until something is put in it (`saveNow` takes its text with
  the hats grouped, and a project as made, as empty).
  - **The app**: `TrackData::laneChoke`, `Session::setLaneChoke`, `Command::SetLaneChoke` (a = lane,
    b = group; appended). `sendSong` tells every drum lane's, NONE too. In a song file
    `choke LANE GROUP`, **left out at NONE**; a group nobody knows is NONE; under a synth track it is
    not taken. **In undo by `soundMark` / `restoreSounds`**, as FEEL.
  - **Set on the KIT page's third column** (`KitScreen::kChoke`, between FIT TO and the recording,
    `kChokeX`/`kChokeW` in kit_screen.hpp): hold A + UP / DOWN NONE, 1, 2 (the ends stop), B none;
    left / right go round the columns (four since EDIT). The recording's column is narrower for it, so its
    missing line is two short ones (FILE NOT FOUND. / THE DRUM PLAYS.), and what is said for a
    moment (a catch, a recording) is drawn over the three heads from FIT TO on.
  - **The mark** (`drawChokeMark`, both views, `kChokeMark...` in pattern_layout.hpp): brass, at
    the bottom right of the lane's name, a dash for CHOKE 1, two for CHOKE 2.
  - Tests: `tests/choke_test.cpp` (the engine: the cut and its fade, groups apart, the same sample,
    as it was, ratchets, slices, THROW and a hit by hand, blocks and helpers, a group changed, a
    track gone, numbers out of range) and `tests/chokeapp_test.cpp` (the new kit, the KIT page,
    the mark off the drawn screen in BROWN and PAPER, the song file, undo, a song sent again).
    Stills `choke`, `kit-choke`, `kit-choke-held`. **Nobody has heard it.**
- **EDIT is what a drum lane does to its sound, and the file is never written** (owner, 2026-11-16:
  "only affected the sample in the project, not the raw file"; `app/lane_edit.hpp`, `LaneEdit`,
  `TrackData::laneEdit`, `Session::setLaneEdit`). START, END (parts of the file in `kEditUnits`),
  FADE IN, FADE OUT (ms), GAIN (tenths of a dB), PITCH, FINE, REVERSE, all whole numbers (undo
  hashes the struct whole). **The engine knows nothing of it**: the app makes the lane's sound and
  hands it over as any recording (`setLaneSample`, the old one through the trash), so FIT, slices,
  the carry-on, CHOKE, THROW, Catch and a bounce all take the edited sound unchanged.
  - **The order is fixed** (`makeShape`, then FIT, then `makeLevel`): the cut (each CUT edge fades
    `kEdgeMs`, the file's own edges are left), REVERSE, PITCH (a windowed sinc, cut lowered going
    up), then FIT, then the fades as heard, GAIN and the guard (`guarded`: from `kKnee` 0.9 up,
    rounded into full scale; every edited lane, even at GAIN 0). **What FIT is worked from is the
    shape** (`LaneSound::shaped`), and the fitted sound is KEPT (`fitted`), so a GAIN or a fade turned
    on a fitted lane is one pass, never a fit again (`setLaneEdit` reshapes only when START, END,
    REVERSE, PITCH or FINE moved).
  - **A lane with every edit at rest plays the file as it always did, to the bit**, and a built-in
    drum at rest is the kit's own (`installLane`: nothing installed, no copy; `source` aliases
    `DrumKit::builtIn`). An edited built-in drum is a lane sample, and is drawn on the KIT page.
  - **A shape too costly for a frame goes to the helper thread** (`shapeCost` over `kShapeInAFrame`:
    PITCH or FINE on a long sound; a 10 s file at -24 is 0.1 s on the desk). It is the FIT job's
    thread: `Job::shape`, `LaneSound::shaping`, made and fitted in one go, the version checked as a
    fit's is. Until it lands the lane plays what it had (freshly read, the file), its level made at
    once; the view says MAKING IT..., the kit MAKING. Anything that waits for fits
    (`finishStretching`, a bounce) waits for it too.
  - **Another recording puts START and END back to the whole of it, the rest stay**, and that is no
    step of undo (`setLaneSample` takes the mark again: undo would put the old cut on the new
    file). `loadLane` (a song opened, a file sent over WiFi, a rename) keeps every edit.
    NORMALIZE (`normalizeLane`: the GAIN that puts what is played, fitted if it is, at -1 dB or
    just under) and TRIM SILENCE (`trimLaneSilence`: 40 dB under the file's loudest, START 1 ms
    ahead) only set the boxes.
  - In a song file `laneedit LANE START END GAIN FADEIN FADEOUT REVERSE PITCH FINE` (START and END
    0..1 of the file, GAIN in dB), **left out at rest**; a number that will not read ends the line
    there, the rest at rest; held by `laneEditHeld` (END after START by a unit: the file's length is
    not known there); under a synth track not taken. In undo by `soundMark` / `restoreSounds`.
  - **The KIT page's fourth column is the sound's picture, EDIT** (`KitScreen::kEdit`); a tap of A
    opens the view (`editing()`, `openEdit`), laid out as the sampler's page (`kEdit...` in
    kit_screen.hpp), the file's picture drawn by `ui::wavePicture`, which the sampler shares. Hold A
    + up / down turns a box, the stick by the square of its push (`laneEditRange` a second); B its
    usual value, B on a box at its usual value back to the kit (on letting go); hold B a second,
    every edit to none (any other button or a move calls it off); Y on START / END trims, elsewhere
    normalizes; X hears. Stills `kit-edit`, `kit-edit-held`, `kit-edit-deleting`,
    `kit-edit-column`; `textsize` holds the view at its widest; `idle_test` has a part.
  - Tests: `tests/laneedit_test.cpp` (each step against a sound made by hand, the engine beside a
    second engine handed the hand-made sound, blocks, FIT, slices in order as one hit, the built-in
    drums, the helper thread, the file's bytes, the song file, undo, the gestures and their hints).
    **Nobody has heard it.**
- **An `Engine` is 1.4 MB: a test does not stand many on the stack.** `engine_test`'s
  `testNudgeMovesAHitOffTheGrid` had six in one frame, 8.37 MB of the 8 MB there is, with 20 KB
  to spare by luck; an Engine a kilobyte bigger (FEEL) and it crashed one run in three, before
  `main` printed anything. Five of them are on the heap now. `testLaneLevel` has five (6.98 MB).
- **Note effects are two bytes on a note** (`NoteEvent::ratchet`, `glide`; a
  chord has `ratchet` and its own five, next rule). They reach the engine in `Command::e` (low byte
  the ratchet, +256 a glide, and `glide << 11` for a length other than half a
  step, above THROW's two bits). In a song file: `notefx STEP KEY RATCHET
  GLIDE` and `chordfx STEP RATCHET`, each after its note.
- **Chord effects are a `ChordPlay` on a chord** (owner, 2026-11-12;
  `ChordEvent::play`, `engine/include/apotheca/chord_rule.hpp`): STRUM (0 off,
  1 STRUM, 2 WIDE STRUM, 3 / 4 / 5 an arpeggio at a quarter, half and one
  step), ORDER (0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM), and HUMAN TIME, HUMAN
  LEVEL and LENGTHS as percentages. The numbers are in songs: appended. The
  top figures are named constants there (`kHumanTimeMs` 12, `kHumanLevelMost`
  0.15, `kLengthsMost` 0.5, `kStrumMs` 9, `kWideStrumMs` 26).
  - **The engine does not know chords; each note says which key of its chord
    it is.** `NotePattern::render` fills `play.index` and `play.count`; they
    go in `Command::play` (SetNote) and `TrackImage::Note::play`. Anything
    new that writes chord notes out goes through `render`.
  - **A chord with nothing set takes the path it always did**
    (`play.atRest()` in `fireStep`): that is what keeps old songs to the bit.
    ORDER alone is "nothing set".
  - **Every roll is worked out from where and which time**
    (`chordSeed(track, bar of the cell, step, steps since Play)`, then `chordUnit` with
    what it is for and which note; not the scene's number, see the scene-move rules). Never a generator walked: that is what
    makes it the same on any helper count and block length, and Play from
    the top the same performance again. The only counted thing is
    `Track::strokes` (chords begun since Play, for UP-DOWN strums), counted in
    song order on the audio thread.
  - **Everything a chord effect times is a whole number of samples**
    (`kWholeFrames`: the delay, a ratchet's pieces, the note's end). A
    waiting note fires when under one sample is left, so a part of a sample
    plays a sample sooner or later by how the sound is cut into blocks. A
    nudged plain note and a plain ratchet still do (not changed: old songs at
    the handheld's block length). An arpeggio's clock fires as a step does,
    on the first sample at or after its time. **The `ceil` in `startNote`'s ratchet
    loop is reached by no note** (a chord's and an arpeggio's keys come with
    ratchet 0; `startChordNote` counts the pieces and ceils them itself): an
    abort there passed all 46 tests (2026-11-15). Kept as a guard.
  - **A note that may be early is asked for a step ahead**, as a nudged-early
    note is. Which asking takes it is `nudge < 0 || play.time > 0`: nothing
    that depends on the tempo, or a tempo change between the two askings
    plays the note twice or not at all. The song's very first step has no
    step before it: early notes land on the top (`top`).
  - **A spread chord is still one moment to a glide.** Its notes say nothing
    to the `GlideTrail` when they sound (`kUntold`); a waiting note that only
    tells (`kTellOnly`) names each key at the chord's own moment. So what
    follows slides from its keys as written (which one: the track's GLIDE
    FROM; TOP is the top key), as `Project::glideLines` draws, however it is
    strummed. `chordfx_test`, `glide_test` and `glideapp_test` hold it.
  - **An arpeggio is one clock for the chord** (`Track::Arp`, three a track:
    one still playing, the next chord's waiting for its nudge, the one after
    asked a step ahead; the one that begins ends the one playing; with two,
    the third took the clock of the one still playing), not a waiting note
    per note: 512 would not fit. `clearPending` and a track taken away empty
    them; `SetTempo` keeps its place in the step. A ratchet is not played in
    an arpeggio. **An arpeggio ends with its chord**: a `SetNote` into its
    bar sets `Arp::look`, and before its next note it looks whether an
    arpeggio's note is still written on its step (by step, not by slot: a
    chord's keys change slots when a loose note is written).
  - **Which asking takes a chord is remembered, not read twice**
    (`Track::chordAsked`: the step, counted from Play, whose chord was asked
    a step ahead). A chord asked ahead is not begun again on its step; one
    not asked begins on its step, early keys with the step. Read from the
    values at each asking, a HUMAN TIME turned while the song played lost
    the chord once or played it twice.
  - **A ratcheted chord's hits are counted from the CHORD's length**
    (`startChordNote` has the ratchet's loop itself), not from the key's own
    (longer when early, shorter when late or under LENGTHS): keys of one
    chord played different numbers of hits. LENGTHS shortens the last hit
    only. No key is so late that its last hit has no chord left.
  - **A bounce plays the song once** (`Command::Play` with `c = 1`,
    `Engine::once_`): on the song's last step nothing early is asked for
    from its first (chords, notes and drum hits alike), or it sounds in the
    file's last moments and rings into the tail. Live, going round, it is.
  - **THROW holds for the whole chord** (`Note::hold` is set before LENGTHS
    shortens the note; an arpeggio's every note carries what is left).
  - In a song file: `chordplay STEP STRUM ORDER TIME LEVEL LENGTHS`, a line
    of its own after the chord (and its `chordfx`), **left out when all five
    are 0**. A new word and not more numbers on `chordfx`: an old program
    skips a word it does not know, and `chordfx` is written as it always was.
  - **On the roll, X held on a chord shares the directions out the other
    way**: up / down pick one of six (`NoteScreen::fxPick_`, kept from one
    hold to the next), left / right set it. A plain note's are as they were.
    The list, a click, the words and the dice's odds are
    `app/chord_effects.hpp`; the strip's places are in `note_marks.hpp`
    (`kChordFx...`). A chord broken into notes keeps none of the five.
  - **A chord's RATCHET lines and its light corner are drawn once** (owner,
    2026-11-13): on its lowest key that shows, the one under its name; on
    the next key clear of the name where the name sits on that one (step 1,
    nudged early), and the name is drawn again on top. THROW's foot is on
    every key. `chordfxapp_test` reads it off the drawn screen.
  - X hears a chord plain. The effects are the step clock's.
  - Its tests are their own two programs: `tests/chordfx_test.cpp` (the
    engine, with a listening synth) and `tests/chordfxapp_test.cpp` (the
    gesture, the strip off the drawn screen, the dice, the song file, undo,
    swap, break, drag, scenes). `chordfx_test testStrum testArpeggio` runs
    just those.
- **A glide is a length, and its number is not its place** (owner,
  2026-11-12; `engine/include/apotheca/glide_rule.hpp`). The numbers are in
  songs, appended: 0 off, **1 half a step (what ON was, so old songs sound as
  they did)**, 2 a quarter, 3 one step, 4 two, 5 FULL. The order a player
  steps through is `kGlideOrder` (`glideAt` / `glidePlace`). A number nobody
  knows reads as half a step (`glideKind`).
- **Which note a glide comes from, and for how long, is one rule in that
  header, and the engine and the roll both ask it** (`GlideTrail`,
  `glideSteps`, `glideLines`). Do not work either out anywhere else.
  The rule: the last note the track began (Play clears it); a ratchet's later
  pieces are not notes beginning; notes that begin within 1/256 of a step are
  one moment (none slides from another, what follows slides from one of its
  keys: GLIDE FROM, next rule, says which);
  FULL is the time since that moment began; on a ratchet never more than 85% of
  the first piece. The engine counts time in steps since Play (`stepsFired_`
  plus the nudge), not in samples, so both sides get the same numbers.
  `glide_test` holds the rule to the engine with a listening synth;
  `glideapp_test` holds `Project::glideLines` (what the roll draws) to the
  engine through the Session, chords included.
- **A track's two MODs are in the song, and one rule says what a MOD may be
  aimed at** (batch 2, 2026-11-14; `TrackData::mods`, `app/mods.hpp`,
  `Session::setMod`, `tests/modapp_test.cpp`). `modTargetFits`: a send; a
  slot's setting that is there and is an AMOUNT; a macro of a synth that is
  not a choice (`macroIsChoice`: a new synth's threshold macros go there).
  **A MOD that is OFF, or whose target does not fit, is a blank
  `ModSettings()`**: nothing is kept of it, in memory or in the file.
  Whatever changes what a lane belongs to must put its MOD OFF or carry it:
  `Session::setEffect` puts OFF a MOD aimed at the slot, `changeEffectType`
  carries it by the setting's NAME (`modTargetCarried`; a choice on the new
  type: OFF), `setTrackKind` drops one on a macro. Only `Session::setMod`
  writes `mods` outside a song being read (it tells the engine); `sendSong`
  sends them all. Undo sees them through `soundMark()` and compares them as
  the file writes them (`modText`).
  - In a song file: `mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE`, then
    `modsteps N LENGTH SLEW CHANCE v1..v16` where the row is not as a new
    MOD's; **both left out when OFF**. TARGET is the automation lane's number.
    **DEPTH is 0..1 with 0.5 none** (`depthToFile`; the row's values are
    -1..1). The numbers are `mod_rule.hpp`'s: appended.
  - **A song's MODs are made to fit after the WHOLE song is read**
    (`modsFit` at the end of `parseSong`): a `mod` line may stand before the
    `fx` line it is aimed at.
- **X HELD on a knob is its MOD** (batches 3 and 4, 2026-11-15; the owner's calls A to H;
  `app/mod_knob.hpp`, one `ModKnob` the DEVICE and FX screens share; `tests/modknob_test.cpp`). A
  macro on DEVICE MAIN, a TRACK's slot setting or a send on FX, where `modTargetFits` (a choice,
  a bus's or the master's setting: X only hears). While X is held: UP / DOWN DEPTH by 1% (**the
  first UP on a knob with none makes LFO, SINE, 1 BAR, +25% in the first free MOD**; DOWN makes
  nothing), the stick DEPTH smoothly / RATE a click, LEFT / RIGHT RATE, A the SHAPE (after DRIFT a
  STEPPER, its row rolled if it was never written; then SINE), L1 / R1 PHASE or LENGTH (the ends
  stop), Y the dice (`ModKnob::rolled`: a STEPPER's row; an LFO's SHAPE and RATE, never both as
  they were, DEPTH kept), hold B deletes it (a second; undo brings it back). Both MODs on other
  knobs: MODS FULL, nothing is made.
  - **The X-held check comes before every other button's own job** (`modKnob_.onButton` right
    after X in `onButton`): L1 / R1 not FX's track, B not the default, the
    D-pad and the stick not the cursor (`update` returns after `modKnob_.update`). A pressed while
    held is the MOD's; let go after X it does nothing. **X is not taken with A held** (turning).
    X let go with the stick pushed: the cursor waits for it to come back (`stickWait_`, both
    screens; the knob's own `stickWait_` the same when X goes down with the stick pushed).
  - **Where the fields go** (call B): DEVICE in LIVE OUTPUT's strip, FX in the heading's place, on
    SENDS in the OTHER send's row (`FxScreen::modPanelY`: the held send stays in full view), only
    while X is held. **They are a tight row** (`fonts.fixed()`, the same at every TEXT SIZE:
    `testTheSameAtEverySize`), each chip under its field by ONE constant (`kDeviceModPlaces`,
    `kFxModPlaces`), the dice at the left. NOW is what the engine says the MOD adds
    (`Engine::modShown`), purple, from the middle.
  - **Heard while stopped** (call E): the screen asks `Session::hearMods` every frame X is held,
    which sends `ModAudition` only when what runs changes, nothing while playing (and runs it again
    after Stop); `keepModsHeard` (App, beside `keepHearing`) lets go when X is up, another screen
    or track; `dropHearing` (panic, a new song) forgets it. The synth's note is held as long as X
    is (`auditionHeld`), on DEVICE and on FX: a tap is still a tap.
  - **"HOLD X: mod" is in a bar only where it fits at `gHintsOver` 0** (call G): DEVICE MAIN on a
    macro that takes one (FM-6's, every one); FX on SENDS, not on a setting (22
    pixels over). The help card's X line says it everywhere.
  - `Session::setMod` sends only the fields and steps that changed while a MOD stays on (a turn is
    one command, not 26); from or to OFF, the whole MOD as before.
  - The fields move by themselves only while X is held, which the redraw already counts (a button
    held); `idle_test` has a part on each screen (made, heard while stopped, let go).
- **Both MODs may aim at one knob; the list of targets marks the other MOD's** (owner,
  2026-11-15): its word purple and underlined (`ModScreen::otherTarget`; an OFF MOD is blank, so
  it marks nothing). X held on that knob still reaches MOD 1 only. `modtab_test`
  `testTheOtherModsTargetIsMarked`.
- **The MOD tab is the sixth tab, after FX** (batch 5, 2026-11-15; owner: "keep going", the
  plan's calls at their recommended answers; `app/mod_screen.hpp`, `View::Mods`,
  `tests/modtab_test.cpp`). Laid out as FX: four lines (MOD 1, its STEPS, MOD 2, its STEPS),
  the line picked large at the right. A MOD's rows: TARGET, SHAPE (SINE ... DRIFT then STEPPER,
  `ModKnob::shapeAt`: the knob's one list), DEPTH, RATE, PHASE / LENGTH; its STEPS: the 16, then
  LENGTH, SLEW, CHANCE. **Hold A on TARGET (or the MOD's line) is the list of targets, and it
  only TRIES**: nothing is set or heard while A is held, let go aims it (an OFF MOD made as the
  knob's first, call C), B with A held, another track or another screen calls it off. Only
  `pickLineTargets` (through `modTargetFits`) says what is offered. Everything goes through
  `Session::setMod`, so undo needs nothing here.
  - **The top bar's tabs have 3 pixels either side of their word** (`ui::kTabPad`; it was 5),
    so with six tabs the screen's words still have room: 167 stopped, 94 playing (a track's
    name, "12 PHAZED", is 77), 32 playing with WRITE on (`TopBar::wordsRoom`; with five tabs at
    5 it was 186, 113, 51). `modtab_test` holds the name to fit stopped and playing.
  - **The dot at now and the step lit are shown only while the song plays or X runs that MOD**
    (`ModScreen::nowPosition`, `nowPlace`: the project's word, then the engine's
    `Engine::modNow`, the position of the MOD's last tick, and `modPlace`). Read from the
    engine alone, a Stop showed the old dot for a block with nothing to stir the redraw.
    `idle_test` has two parts (X held on a MOD's line; a STEPPER while the song plays, then Stop).
- **A modulated knob's own bar has a thin brass mark where it is given now** (batch 6,
  2026-11-15, call 18; `modMarkAt` / `drawModMark` in `app/mod_knob.hpp`): the bar's centre (what
  the bar itself is drawn at) plus `Engine::modShown` of every MOD aimed at that lane, one clamp,
  `kModMarkW` (2) wide and `kModMarkOver` (3) above and below the bar. DEVICE's macros (the bar's
  place is `ui::kMacroBar...`, which `macroBox` draws from), FX's slot settings and SENDS. **Only
  while the song plays or X runs that MOD**, as the MOD tab's dot, and only once the engine has
  ticked it (`modNow` a number). `modknob_test` `testTheBrassMark` reads it off the drawn screen in
  BROWN and PAPER; `idle_test` has two parts (a macro, a send, playing then stopped). Stills
  `device-modded`, `fx-modded`.
- **FREE is RATE's last choice, an LFO's only** (owner, 2026-11-15: "a free mode ... PHASE can turn
  into a SPEED control"; `kModRateFree` 12, appended; `modFree`, `modPositionFor` in `mod_rule.hpp`).
  **A free LFO's position is SECONDS since Play** (`Engine::playedFor_`, whole samples counted beside
  `sinceStep_`, over the sample rate; stopped, `heardFor_`), so a tempo change does not move it, and
  it is still worked out from where and when: `lfoCycles` is seconds x SPEED, no PHASE (kept, unused).
  The tick is the same tick (counted from each step's start), so a song is the same in any blocks
  exactly where it was. `Engine::modNow` and the tab's dot are in the MOD's own terms (seconds for a
  free one). **SPEED is an amount in Hz** (`ModSettings::speed`, `kModFieldSpeed` 10, appended),
  0.02 to 20, usual 1: the D-pad clicks along `modSpeedClick`'s 31 numbers (`modSpeedStep`: between
  two, the next; the ends stop), the stick on the tab turns it on its log scale (`modSpeedTurned`,
  `amount()`); B 1 Hz. **A STEPPER has no FREE**: `ModKnob::rates` stops its RATE at 8 BARS, and
  `modHeldFor` puts a STEPPER at FREE to 1 BAR (the one place; a hand-written one also plays 1 BAR's
  16 steps in the engine). The dice keep a free LFO free (SHAPE and SPEED, 0.1 to 5 Hz). In a song
  file SPEED is a ninth number on `mod`, **left off at 1 Hz** (so a song without it is written as
  before), kept while the LFO is in time. The knob's fields moved for "0.025 HZ" (DEVICE's NOW from
  528, FX's RATE 465 and last field 550: `testFreeOnTheKnob` holds the widest Hz the stick can give).
  `mod_test` (`testTheFreeRule`, `testAFreeLfoCountsSeconds`, `testAFreeLfoInAnyBlocks`,
  `testAFreeLfoHeardWhileStopped`), `modapp_test` `testAFreeLfoInTheSong`, `modtab_test` `testFree`,
  `modknob_test` `testFreeOnTheKnob`. Stills `mod-free`, `device-mod-free`. Nobody has heard it.
- **Lanes 22 and 23 are MOD 1's and MOD 2's DEPTH** (batch 7, 2026-11-15, call 10;
  `modDepthLane`, `modDepthToLane` / `modDepthFromLane` in `mod_rule.hpp`: 0.5 no depth, 0 -100%,
  1 +100%, the song file's DEPTH scale). Written like any lane (WRITE records a DEPTH turned:
  `Session::setMod` when DEPTH changes on a MOD that stays on; made or deleted is not a turn), in
  `auto` lines, in the scene's `SceneStore`, so a scene move and undo carry them as any lane. **A
  MOD's own DEPTH is its lane's knob**: `SetMod`'s DEPTH sets the lane's `autoBase` and `autoNow`,
  so a scene with nothing written goes back to it (the owner, 2026-11-15: the clearing is left as
  it is). `runMods` reads the lane's centre, and **where the lane rests on its knob it uses the
  MOD's DEPTH itself** (0..1 and back is not always the same float: a song's MODs at rest give what
  they gave before, to the bit). **So the engine runs a MOD at any DEPTH** (`ModSettings::runs()`;
  `on()` still wants one, for the app): a MOD at 0% cuts the chunks on the tick while the song
  plays, and one turned to 0% lets go at once. A MOD deleted leaves its lane's automation where it
  is (no new clearing: the owner's word); the tab shows it (the purple line) and B on DEPTH
  empties it in the scene worked on, as B on an FX setting does. While playing, the knob's
  DEPTH field, the tab's DEPTH row and line and its pictures read `Session::modDepthShown`.
  `mod_test` (`testTheDepthLanes`, `testASceneMoveCarriesTheDepthLane`...), `modapp_test`
  (`testTheDepthLanes`: WRITE, undo, the file, NEXT SCENE), `modtab_test`, `modknob_test`. Still
  `mod-depth`.
- **LEVEL and PAN are MOD targets 32 and 33** (batch 8, 2026-11-15, call 14; **TREMOLO stays**, the
  owner: an effect taken out would break the songs that use it). Not lanes, not automated, every
  track. The engine keeps the mixer's own (`Track::levelSet`, `panSet`, what `SetTrackLevel` /
  `SetTrackPan` say) apart from what is given (`level`, `panL`, `panR`: `Engine::giveMix`):
  `modLevelGiven` (gain times 1 + added, held to 0..2) and `modPanGiven` (place plus added, held to
  -1..1), each the mixer's own **exactly** with nothing added, so a song with no MOD takes the
  render's at-rest path as it always did (`effects_test --print` the same; `mixer_test`
  `testTheMiddleIsUntouched`). `runMods` keeps their offsets in `Track::modMix`; `stillLane` lets
  them go too. The mixer's card, its file line and undo are the mixer's own, never the wobble. The
  MOD tab's list of targets has a sixth line, MIXER. No knob gesture (the mixer is A held on a head)
  and no brass mark on the mixer's card. `mod_test` (`testTheMixRule`, `testLevelAndPan`,
  `testLevelAndPanHeard`: silence at -100%, hard left, the same in any blocks and helpers),
  `mixer_test` `testAModOnLevelAndPan`. Still `mod-mixer`.
- **GLIDE FROM is the track's choice of which key of the moment before**
  (owner, 2026-11-13: "I always like the option"; `glideFromKey` in
  `glide_rule.hpp`). The numbers are in songs, appended: 0 TOP (the highest,
  as it was), 1 PAIRED, 2 NEAREST (a tie: the higher), 3 RANDOM. A number
  nobody knows is TOP (`glideFromKind`). The lengths do not change; "that
  note" is the key picked, and FULL counts from its moment.
  - **PAIRED**: new `i` of `n` (by pitch) from old `round(i * (m - 1) / (n - 1))`,
    a half rounded up; one note alone, from the top. **It counts every note
    of the moment**, a chord's keys too, gliding or not: a bass note under a
    chord comes from the bottom, a melody note over it from the top.
  - **The trail keeps every key of the last two moments** (`GlideKeys`: 128
    bits, one a key, so there is no cap and nothing is left out).
  - **PAIRED must know a note's whole moment when its first note begins.** So
    a PAIRED track's step is gone through twice by the one set of conditions
    (`fireStep`: `telling_`, then for real): the first time `startNote` and
    `startChordNote` only tell the trail what is coming (`tellComing`) and
    change nothing. The list holds 128 (`kGlideComing`: all a step can ask);
    one more is not kept and not counted. `glideLines` tells in the same whole
    steps, **and at the scene's last step also the early notes of the first
    step of the next time round**, as the engine's last step asks them ahead
    (a note half a step late there and one half a step early on step 1 are one
    moment: `testPairedWhereTheSceneGoesRound`). **`telling_` is false outside that loop**; anything new that begins
    a pattern's note inside `fireStep` goes through those two functions.
  - **Known, held by `testPairedAcrossAStepLine`, not hidden**: two notes
    within 1/256 of a step with a step line between them. The first is told a
    step before the second, so it counts as alone (from the top). The roll
    draws the same. Also: PAIRED turned on mid-step, that one step's notes
    count as alone.
  - **RANDOM is a roll for where and when** (`glideSeed(track, at)`: the track
    and the whole step nearest the moment, counted from Play; then
    `chordUnit(seed, kRollGlide, pitch)`). Never a generator walked, never the
    scene's number. **The roll draws the first time the scene plays after
    Play** (`glideLines`' `timesRound` 0; later rounds roll anew, and a scene
    that is not first in the song is first played at another time than the
    roll assumes). Play always starts on the scene worked on
    (`Session::togglePlay`), so the roll is right for the scene Play was
    pressed on, song or scene alone; a song played through INTO a later scene
    rolls other keys there. `testRandomOnALaterScene` holds both.
  - **It is the track's, not the patterns'**: `TrackData::glideFrom`,
    `Session::setGlideFrom`, `Command::SetGlideFrom`. Not in a track image,
    not in a scene, so a scene move carries nothing for it. `applyProject`
    tells every track's. `RemoveInstrument` puts the engine's back to TOP.
    **In undo** since 2026-11-14 (a step of its own). In a song file:
    `glidefrom N`, a line of a synth track's own, left out at TOP; under a
    drum track it is read and not taken.
  - **The gesture: X held on a note, L1 back, R1 on**, a click, stopping at
    each end (`NoteScreen::fxFrom`). Zoom and carry wait for X to be let go,
    on a note only: on a chord L1 / R1 are as they were. The dice leave it
    alone. On the strip it has NUDGE's place while X is held
    (`kStripHeldFromX`): dim while the note has no glide, brass when not TOP.
  - Tests: `glide_test` (each type by hand-worked keys, chords strummed and
    arpeggios, ratchets, RANDOM's rolls, block lengths and helpers, the roll
    against the engine under each type), `glideapp_test` (the Session, the
    song file, undo, the lines' lean off the screen), `under_test` (the strip
    and the gesture), `nextscene_test` (`fill`'s chord track is PAIRED; one
    proof again under RANDOM).
- **The roll's glide line ends at the note's start** (the owner's picture);
  the slide is heard FROM the note's start. A line may begin bars before its
  note (FULL), or before the first bar's top, which is the scene's end: so
  the roll draws **every bar's lines wherever they cross the bar shown**
  (`glideShifts`, `glideSpan` in `note_marks.hpp`; drawing only the bar shown
  and the next left a slow line out of the bar it starts in). The line is
  rectangles gathered and drawn in one call (`glideLineRects`): a call a
  pixel cost 0.6 to 0.9 ms for 64 long lines on the desk, this 0.2 to 0.4
  (`GLIDE_PRINT=1 ./build/glideapp_test`; not timed on the handheld). `glideapp_test`
  reads the lines and the strip **off the drawn screen**
  (`testTheLinesOnTheScreen`, a software renderer and the real lettering).
- **Phazed's DETUNE keeps a twin at its middle while notes sound** (MOD
  batch 0, 2026-11-14; `keepTwin`, `lineBack` in `engine/src/phazed.cpp`). Off
  the middle it makes a one-line sound a twin, and the CZ fades every voice
  for a change of line layout: a note held while DETUNE passed 0.5 was cut
  (RMS 0.087 to 0.008). A twin made by DETUNE stays one at 0.5 while any voice
  sounds and goes back to one line before the next note into silence.
  Moving OFF the middle with a one-line note held still cuts it, once.
  `phazed_test` (`testDetuneThroughTheMiddleKeepsTheNote`).
  **A note that begins on the kept twin is trimmed 3 dB more**
  (`kDoubleInTuneTrim`, `trimInTune`: its lines start in tune and in phase; a
  legato part never falls silent and stayed 3 dB up), from that note's start,
  given back when DETUNE leaves the middle. Never taken as DETUNE merely
  reaches the middle: a held note fell to 0.61 of itself. 0 turns it off (the
  owner's call: a twin's short notes just off the middle are 1.2 to 3 dB up
  too, as on `main`).
- **PHYSIC** (`engine/include/apotheca/physic.hpp`, `physic_rule.hpp`, `engine/src/physic/`;
  `docs/plans/physic-plan.md`, section 14 says what was built and measured; `tests/physic_test.cpp`).
  Nobody has heard it.
  - **Every mode snaps to exact zeros on its own tick** (under 1e-15), not only the whole voice: toward
    WOOD the high modes die long before the voice, and sat in numbers too small to hold (15 to 150
    times the cost with flush-to-zero off). A voice sleeps after 64 samples under -120 dB, a string's
    whole line too. `testTooSmall` scans every state over a 60 s tail.
  - **`#pragma clang fp contract(off)` in `physic.cpp` and its test**: with it the handheld plays the
    desk's bits (its song `0x45a36ca1ca8d0d2a` on both, under qemu); without it clang joins a multiply
    and an add on ARM.
  - **A strike waits for the top of `render()`** (a queue of 16): what arrives before one render is one
    moment. A GLIDE takes a ringing voice over only when it is the one note of its moment naming that
    `from`, the voice is over -40 dB and past its strike; a chord under TOP strikes every key.
  - **`Instrument::noteSeed` and `Instrument::stopNotes`** are PHYSIC's (the others' do nothing / are
    `allNotesOff`). The seed is `strikeSeed(track, key, the strike's own moment)`: a ratchet's piece
    `at + k apart`, an arpeggio's note `arp.at + m gap` (`startNote`'s `strikeAt`, used for the seed
    alone). Stop calls `stopNotes` (RELEASE damps, RING rings on, so a bounce keeps the tail); Play and
    panic `allNotesOff` (30 ms).
  - **`setMacro` stores and returns**; a voice works its numbers out on its own 16-sample tick, counted
    from its strike, and its glide moves there (`Slide::take(16)`), so a bounce is live play to the bit.
  - **Not in the pinned song** (`effects_test`): its own song and checksum in `physic_test`, remade only
    by the pinned song's rule. **The 24 version 1 presets are pinned too** (`testVersionOne`: a phrase of
    re-strikes, take-overs, steals, macros, Stop and sleep, one checksum each, taken before batch 10).
  - **Batch 10 (2026-11-16, plan section 16): PAIR, MOTOR, BODY, BODY MIX, BUZZ (ids 18 to 22) and STRIKE
    NOISE (2).** Each 0 is the version 1 path, to the bit: the voice's sample loop is built twice (the
    old one when MOTOR and BUZZ are off), a pair's twins are made only when PAIR is on, BODY's bank is
    not run at NONE, TUBES or MIX 0. **Keep that**: a new setting at 0 must not touch the old arithmetic.
    - **PAIR is the object's make, read at the strike** (as MODEL): a re-strike with another PAIR makes
      the voice again. A string's second line is a slot's own (`str2`, the lines block twice over); a
      glide retunes the two strings by turns, a tick each (`which`), so two cost about one there.
    - **MOTOR and BUZZ are worked out when the setting is turned** (`motorHzNow`, `buzzGapNow`), never on
      a tick: `pow` there cost a tenth of a percent. MOTOR's level is `1 - D sin^2(pi t)` from the
      polynomial `sinHalfTurn`, t from the strike, a sample at a time between its tick's two values.
    - **BODY is the track's, a sample at a time** (`physic/body.hpp`), states snapped under 1e-15 on
      every sample. **A block it skips (silent in, silent state) still slides its mix a sample at a
      time**: skipped, the slide fell a sample behind in blocks of 1 (`testBlocks` found it).
    - **NOISE and BUZZ are hashes of the seed and the sample since the strike** (`pluckNoise`), never a
      generator walked. A NOISE note's level is what it is LIKELY to ring at (a random walk's size).
- **ACID** (2026-11-16, the engine only: batches 0 to 2 of `docs/plans/acid-plan.md`;
  `engine/include/apotheca/acid.hpp`, `acid_rule.hpp`, `engine/src/acid.cpp`, `engine/src/acid/ladder.hpp`;
  `tests/acid_test.cpp`). `SynthKind::Acid` appended. Nobody has heard it, not run on the handheld.
  **Every figure is SELF** (no source was opened). The app: next block.
  - **One voice, the newest key wins**; letting it go never goes back to a key still held. `allNotesOff`
    (Play, Stop, panic: `stopNotes` is the default) hands the note to ONE tail slot (3 ms fade, counted
    in samples) and the voice is fresh: Play from the top is the same line (`testPlayFromTheTop`).
  - **ACCENT is velocity 0.95 or over** (`kAccentAt`), **SLIDE is the note's GLIDE**, through
    `noteOnFrom` as the engine already calls it; nothing new on a note. **ACCENT 0 (setting or macro)
    is the plain path to the bit** (`accented()` is false there: `testAccentZero`).
  - **The tie** (`slides()`, `tieSeconds`): a GLIDE note with the gate open, or shut under 15/16 of a
    step ago (`kTieSteps`; never under 5 ms, `kTieMs`, which is all a synth played by hand gets) starts no
    envelope and feeds no new sweep: a note placed shorter than its step (0.9, 0.5) still ties to the
    GLIDE on the next step, as the original holds the gate into a slide; an empty step between starts
    again (and slides in pitch). Both orders on one sample tie (`testTie`). **The step comes from the
    engine**: `Instrument::noteStep(seconds)` before every note the step clock starts (a no-op for the
    others). The voice stays awake through the window (a step of silence, running) before it sleeps.
  - **A GLIDE to the same key is a TIE, one long note** (coordinator for the owner, 2026-11-16):
    `Instrument::tiesSameKey()` (ACID's only) has `startNote` call `noteOnFrom(note, ..., note, ...)`
    where every other synth still gets a plain `noteOn`: `effects_test --print` identical. A new synth
    that wants it says so; nothing else changes.
  - **The slide lands exactly**: an RC of three time constants bent so its last sample is on the key
    (`slideLeft`; the voice steps `slideR` a sample and counts `slideN` down). `testSlideTime` holds
    every sample it reads to the rule within 1e-6 cents.
  - **The filter is a diode ladder solved per sample, its CUTOFF the pitch it sings at.** Four coupled
    trapezoidal nodes (stage 1's capacitor half: SELF), a high-pass in the loop (`kLoopHighPass`),
    the inverse of the 4x4 system worked out on the tick, so a sample is four sums side by side and one
    division. The pitch where the loop turns half a cycle and the feedback there (`ladderEdge`: 1.531
    of the stages' corner, 24.47) are found once on the analog model; g = tan(pi f / rate) / w180.
    **Coupled stages lose 28 dB round the loop**, so the edge is at feedback 24 (a buffered ladder's 4)
    and the pass band at the edge is 26 dB down: `kMakeUp` (output x (1 + 0.33 k)) gives most back.
  - **The bend is read TWO samples ahead** (the JX-3P's read-ahead applied twice: exact for a steady
    part plus a sine at the singing pitch), so its division waits on nothing of the sample before. Read
    from the latest sample (the JX-3P's way) the division sat on the chain each sample waits on: 0.21%
    of a core against 0.15%. It sings within 0.6 cent of CUTOFF, 80 Hz to 10 kHz, at one level.
  - **The cutoff is worked out every 16 samples counted from the strike** (`kTuneTick`, PHYSIC's
    tick; a strike works it out on its own first sample): against every sample, the worst line (ENV MOD,
    ACCENT and SWEEP full) differs 50 dB under the sound (`testTuneTick` holds -40). At 8 it cost 0.16%.
  - **It sleeps once the gate has been shut for the tie's window** (a step, or 5 ms with no step
    known), the gate's 3 ms ramp at 0 and the accent sweep under `kSweepQuiet`, and wakes as a fresh voice
    to the bit (`testSilence`). Its oscillator and filter run, silent, until then, so a slide in the
    window is seamless.
  - **`setMacro` and `setSetting` store and return**; the settings are worked out at the top of the
    next render (or a note's start). `#pragma clang fp contract(off)`, as PHYSIC.
- **ACID in the app** (batch 3, 2026-11-16; `ac` in `app/synth_spec.cpp`, `app_test` `testAcidOnATrack`,
  `testAcidsHintsFit`, `testAcidsAccentMark`, `testAnAcidLine`; `app_test --acid` runs them).
  `TrackKind::Acid` appended, its row LAST in `kInstruments` (word `acid`, presets `NAME.acid`, "mono
  bass, after a classic bass line box": the words test fails "TB-303" and "tb303" too), `TrackData::acid`
  an `AcidPatch` of 20, 14 used. A new track is on Classic Saw. MAIN, SOUND (WAVE, OCTAVE, TUNE, LEVEL,
  VELOCITY, SLIDE) and FILTER (eight). Its own preset over preset 0's sound first (as PHYSIC's). **No
  macro is a choice** (`macroIsChoice` has no ACID row), **no roll** (Y does nothing). Its built-in data
  is `app/presets/acid.inc`, empty until batch 4.
  - **The roll marks an accent** (a note at 0.95 or over, on an ACID track only): a brass corner of 6
    (4 on narrow keys) at the block's top right, left of a ratchet's purple edge (`accentMark`,
    `note_marks.hpp`). `testAcidsAccentMark` reads it off the drawn screen in BROWN and PAPER.
  - **Writing a line** (`testAnAcidLine`): an accent is one press more (RIGHT while A is held to
    place), a slide three (hold X, UP twice); a 16-step line with 4 of each was 165 presses, 133 of
    them the D-pad walking the keys.
  - **Seven instruments do not fit the list at its pitch with the words under it**: the list stands as
    high as it must (`listTop`, now for adding a track too), and only with the words its rows close
    from 52 to 47 (`SongScreen::listPitch`).
- **CLOUD** (2026-11-16, the engine only: batches 0 and 1 of `docs/plans/granular-plan.md`;
  `engine/include/apotheca/cloud.hpp`, `cloud_rule.hpp`, `engine/src/cloud.cpp`; `tests/cloud_test.cpp`).
  `SynthKind::Cloud` appended. **Every figure is SELF. Nobody has heard it, not run on the handheld; no
  app side yet** (batch 2). The plan's calls are at their picks but one: **the pool is 24, not 32**.
  - **Every onset is a whole number of samples counted from the key's strike, every setting is read at
    an onset, and a grain once started never changes** (`onsetWait`, `grainUnit(seed, n, what)`: the
    strike's `noteSeed` and the grain's number, skipped ones counted). That is all the block and helper
    rule needs: `testBlocks`, `testThroughTheEngine` (1 to 333 frames, 0 to 3 helpers, Play again).
  - **A render is worked in pieces of 64**: every key's envelope first (it does not hang on the grains,
    and says where a key ends), then the grains from one onset to the next, each added into ITS KEY's
    sums in the pool's order, then the keys through their envelopes in key order. **A pool slot is free
    from the very sample its grain ends**, and a key's grains are cut to the sample the key ends
    (`dies`): freed at a piece's end instead, which slot the next grain took would hang on the block
    length (`testBlocks`' full pool with keys ending). Anything new that frees a grain frees it on a
    sample, never "at the end of this call".
  - **A grain's place is 32.32 fixed point** (`kWhole`): one integer add a sample, whole samples
    exact, the same in any blocks. At the recording's own pitch (ratio exactly 1: no PITCH SPRAY, TUNE
    or glide) the place is rounded and whole samples are read (`plain`); under 20 ms a straight read
    (`kLinearUnder`, the plan's third cut); else 4-point Hermite (`hermite`, within 2e-6 of a true sine,
    `testTheRead`). Reads go in stretches with no end of the recording in them (`clearRun`); the sample
    by an end reads round it. **The recording has no edges** (`wrapPlace`, and REVERSE reads the same
    stretch from its far end).
  - **The window is `windowPoly`**, the half cosine as a polynomial (sin to its y^9 term, held to 0..1:
    exactly 0 and 1 at its ends, 2e-6 from the cosine), worked out over a run first, so the compiler
    takes it four at a time. A 257-point table and a cosine stepped along were both tried: dearer (each
    sample waited on the one before). Each piece at least 0.5 ms (`windowFloor`).
  - **The gain is 1 / sqrt(DENSITY x SIZE, held to 1..12)** (`overlapGain`: a key sounds 12 at most),
    so the level holds within 0.8 dB over DENSITY x SIZE (`testCloudLevel`); the sides equal power.
  - **Keys: 6 sound, 12 slots** (6 more to fade out over 5 ms: a seventh key takes the oldest let go,
    else the oldest; `allNotesOff`, Play and panic, fades them all; `stopNotes`, Stop, lets them go so
    the grains ring on under RELEASE). The same key struck again is its own slot (its grains go on,
    SCAN and the count start again). A key played by hand rolls `liveSeed` of a count `allNotesOff`
    puts back, so Play from the top is the same cloud.
  - **A new recording ends every grain at once** (`setSample`): the old one goes to the trash and is
    freed next frame. ASan holds it (`testSwap`: the old one written over with not-a-number and freed;
    that mutant reads past the new, shorter one). The engine hands it over as the sampler's
    (`Engine::setTrackSample`, unchanged). No recording, or a length of 0: **the TONE** (`Cloud::tone`,
    2 s about middle C, every partial whole cycles round; made once per rate in `prepare`, shared, as
    `DrumKit::builtIn`).
  - **The picture**: `Instrument::grainPlaces` (CLOUD's alone, -1 for the rest), read after each chunk
    on the thread that rendered the track into `Track::grainAt` / `grainCount` (atomics, as the
    meters); the app asks `Engine::grainPlaces(track, places, most)`. `kGrainsShown` is `cloud::kPool`
    (a `static_assert`).
  - `setMacro` and `setSetting` store and return; `selectPreset` puts the macros at 0.5. Three engine
    presets (Soft Cloud, Dust, Fifths Choir); the plan's 24 are app data (batch 3).
- **CLOUD in the app** (batch 2, 2026-11-17; `cl` in `app/synth_spec.cpp`, `app_test` `testCloudOnATrack`
  and `testCloudsHintsFit`, `app_test --cloud`). `TrackKind::Cloud` appended, its row LAST (word `cloud`,
  presets `NAME.cloud`, short name CLD, "clouds of grains from your recordings"), `TrackData::cloud` a
  `CloudPatch` of 24, 17 used. A new track is on Soft Cloud with no recording: the TONE. MAIN, SOURCE,
  GRAINS, PLAY; no macro a choice; **ROLL on Y** (`cl::roll`: SPRAY, SIZE, DENSITY, SCATTER, SHAPE, PITCH
  SPRAY, PITCH STEPS, REVERSE, STEREO; never the recording, POSITION, SCAN, ROOT, TUNE, the envelope or
  the levels). Its own preset over preset 0's sound first. **Nobody has heard it.**
  - **Its recording is the sampler's `TrackData::sample`, through `holdsRecording(kind)`** (project.hpp):
    `sampleUses`, `deleteSample`, `recordingChanged`, `findMissing`, CATCH, RECORD, `setTrackSample`,
    `applyProject`'s `loadTrackSample`, the song file's `sound NAME` (written before `preset` on a CLOUD
    track) and the rename that follows saved songs. What means the sampler's own settings still asks for
    the sampler. A track given the other of the two keeps the recording.
  - **`SynthSpec::recording`** (CLOUD's alone) gives the generic screen two things: **MAIN's picture**
    (`SynthScreen::drawPicture`, between PRESET and the macros, `kPictureY`/`kPictureH` 80, the macros
    `kPicMacroH` 69 under it): the recording's outline (the TONE's, dim), lit within SPRAY of POSITION
    (macros on), POSITION a brass line, each sounding grain a teal tick top and bottom from
    `Engine::grainPlaces`, the name in the fixed lettering over it; and **RECORDING as the first stop of
    SOURCE** (`kRecordingId`, not a setting: `item()` adds it): a tap of A the list of recordings (the
    sampler's `SamplePicker`, its preview the cloud at middle C), hold A + up / down the next, R1 there
    RECORD (elsewhere R1 is the page), Y anywhere on SOURCE CATCH; what is said shows in the top bar.
  - **The grains are a readout for the redraw** (`Readouts::grains`, `kStirGrains`: the track up's places,
    `App::readouts`); `idle_test` has a part (a CLOUD at LEVEL 0, so LIVE OUTPUT stands still and only the
    grains move).
  - **Eight instruments close the list up**: 46 adding a track, 41 with the words under it, each box a
    pixel short of the pitch (`SongScreen::rowH`, its two lines placed within).
  - **24 presets, four folders of six** (batch 3, 2026-11-17): STILL is the engine's table (Soft Cloud
    first), DRIFT, DUST and CHOIR are `app/presets/cloud.inc` (a folder the data adds must not share an
    engine folder's name, or the folder step finds two runs). Six carry a MOD, eight an effect or a send.
    `presets_test --heard-cloud` (ctest `presets_heard_cloud`) plays each through the TONE and through a
    recording it makes (a rising sine and a noise burst): -30 to -8 dB loudest, peak under 0.9, a tail
    that ends in 30 s, the presets on one source within 14 dB of each other, every MOD moving and heard
    on the recording. **A player's own CLOUD preset keeps its recording** (`UserPreset::recording`, a
    `sound NAME` line after `whole` in the file): picked, `Session::putPresetRecording` puts it in by its
    name, else by its base name where exactly one has it, else keeps the track's and says RECORDING NOT
    FOUND (`Session::say`). A built-in never names one. The recording a pick changes is not a step of
    undo (as the sampler's).
  - **The WiFi page's synth page draws its recording** (batch 4, 2026-11-17): a spec with
    `SynthSpec::recording` adds `waves` (the recording's peaks, or the TONE's: `cloudToneOutline()`, which
    MAIN's picture shares), `band` (POSITION and SPRAY: the lit part and the brass mark; a click or a drag
    sets POSITION) and `recordings` (read on the app thread while the page looks). `sample NAME` and ROLL
    as before; the list's first choice is THE BUILT-IN TONE. `design_test` `testCloudFromThePage`;
    `APOTHECA_DESIGN_CLOUD=1` holds the page for a browser. Tried in Chromium with Playwright, 2026-11-17.
  - Stills `cloud`, `cloud-1` to `cloud-3`, `cloud-long`, `cloud-sounding` (a key held: the still is
    taken before the engine renders, so no ticks show there; `testCloudsHintsFit` reads them off a
    drawn screen).
- **FM-6's `setMacro` returns early on the value it has**, as Phazed's
  does (JAX-EP's does not; `bench` times it: 0.035 us a change, 0.002 the same value;
  `Fm6::applied()` counts them for `fm6_test`).
- **Phazed's glide is the CZ's portamento given a rate**
  (`porta_rate16`, `third_party/CHANGES.md`): it moves on the CZ's 122 Hz
  tick. A note waiting for its slot keeps the rate and the key it was played
  with.
- **Phazed rests in silence, to the bit** (speed plan batch 1;
  `cz_synth_resting` / `cz_synth_rest`, `RateConverter::silent` / `skip`):
  idle, its output settled, its chorus fed silence longer than it remembers
  and the converter's history zeros, a block is zeros and only the CZ's counts
  and its volume's glide move. **Anything new the CZ moves in a silent frame
  goes in `cz_synth_rest` too**, or a woken Phazed is not the one that never
  slept; `speed_test` (`testPhazedRests`) holds the two side by side.
  Its filter tables and the converter's are made once and shared
  (`cz_synth_init_shared`, `RateConverter::tableFor`): never written after.
- **The list of recordings is three columns** (owner, 2026-11-14; `app/sample_picker.hpp`,
  plan `docs/plans/picker-plan.md`, batches 0 to 2 built): the folder this one is in, this
  folder with the cursor, and what is under the cursor. **The columns stand still** (owner,
  2026-11-15: the preview "already opened"): on SAMPLES (folders there) the cursor is in the FIRST
  column, the folder under it in the second and the preview's box, empty, in the third
  (`cursorFirst`, `cursorX`, `drawPreview`), so going in moves only the cursor; inside a folder
  the cursor's column is the middle one; with no folders (`flat()`) the one list is the middle
  column. `sample_test` `testTheColumnsStandStill` reads it off the drawn screen.
  **What the list plays is a `SamplePicker::Hear`** (owner, 2026-11-15): a recording arrived on with
  the song stopped is a PREVIEW, half of what it plays (the sampler: START to END at middle C; the
  kit: the lane's sound, fitted or not), `Session::kAuditionMs` at least, stopped the moment the
  cursor moves on to anything that puts nothing on (a folder, out, B: `quiet()`; a recording put
  on takes the lane or the key over itself). **X while it plays is REST: on to its end, not begun
  again** (`Session::hearFor` moves its stop); X with nothing playing is held, as everywhere.
  "Playing" is `Session::hearingOn(screen)` and the screen's line still running (a sound that
  ended by itself before its stop is not playing). The built-in drum is heard whole, as a tap of
  it always is. `Session::stopHeard` stops only what that screen began. `hear_test`
  (`testThePages`, `testTheSampler`, `testThePreviewStopsWhenTheCursorMovesOn`).
  `rowsAt(depth, folder)` is the one place a column's rows are made (0 SAMPLES: the first row,
  UNSORTED if anything is loose, the folders; with no folders at all, the recordings
  themselves: `flat()`). **A D-pad press puts the recording on at once; rows going by faster
  than one every `kAtOnceMs` (the stick, a D-pad held past `kFasterAfterMs`) do not: the one
  rested on does** (each is a file read from the card). A, X and B go by what is SHOWING or
  what was on (`on_`), never by the cursor alone. A folder puts nothing on. Deleting a
  folder's last recording leaves the folder (an empty folder is deleted by itself: batch 2).
  Its places are constants in the header; `sample_test` (`testFoldersInTheSamplePicker`,
  `testThePickersPace`) and `textsize_test` (every place it can be, the hints at 0 over).
  **The still `picker-deleting` holds B over a real file**: main.cpp takes its picture at
  600 ms, before the second is up (`picker-folder-deleting` the same, over a real FOLDER).
  - **It changes the card too** (batch 2, 2026-11-15): `Session::moveSample`,
    `makeSampleFolder`, `deleteSampleFolder`, each a `Session::FileJob` (Done, Same, TooLong,
    Taken, NotEmpty, Failed), reached through the list's `Jobs`. The list never touches a file.
  - **Hold R1 on a recording carries it** (`kCarryHoldMs` 300, or at once with a direction or
    the stick). So **R1 on a recording pages when it is LET GO**; on any other row at once.
    While carried nothing goes on the lane (`arrive` returns), A, X and Y do nothing, and the
    top line says CARRYING TO place: name (`carryTo()`: the folder under the cursor, else the
    folder showing; SAMPLES itself is "out of every folder", which is how a recording leaves
    when UNSORTED is not listed). B with R1 held calls it off. A let-go the list never saw
    (`update`: R1 not held) calls it off too: nothing moves without a let-go that was seen.
  - **A move is `moveSample`**: the file renamed with the ending it had (written again and the
    old one taken away where the card will not rename, as the page does:
    `Transfer::renameBroken` is the tests' such card), then `recordingChanged` (the name only),
    `saveNow`, `followLater` (next rule). Nothing is written over, capitals not counted; the folder
    must be there, as itself (no link); the whole name must fit `kSampleNameRoom`. After it the
    list's `original_` and `on_` follow, so B's tap is still "as it was".
  - **Typing a name: A types, Y deletes the last letter, B keeps what is typed and leaves, START
    keeps it too** (owner, 2026-11-15: "I find myself pressing B to back out of the track naming"; it
    was B delete, Y or START keep). With nothing typed B leaves and changes nothing (the track's name
    stays, no folder is made), and START gives a track its instrument's own name. The two places a
    name is typed, `SettingsScreen::typeButton` (a track's name, from SET or a SONG head) and
    `SamplePicker::typeButton` (a new folder), keep the same keys; a new one does too. A folder
    refused (taken, too long) keeps the keys up, whichever of B or START asked. Y does not repeat
    (B never did). The still `typing` showed the SET page, not the keys, until then: the demo's START
    kept the name.
  - **+ NEW FOLDER is a row** (`Kind::NewFolder`: the last of SAMPLES and of a first-level
    folder; not in UNSORTED, not two levels down; only when `Jobs::makeFolder` is given). A
    opens the keys a track's name is typed with (the list's own copy: `typeButton`,
    `drawTyping`; SettingsScreen's was not touched), twelve letters, made in SMALL letters,
    a space at the end dropped. **While typing every button is the list's, START too**
    (`picker_.typing()` in the kit's and the sampler's `onButton`); the still `picker-typing`
    is therefore not given the demo's START, which would make the folder.
  - **Hold B on a folder deletes it only if it is empty** (`emptyFolder`: no recording under
    it, no folder in it), by `rmdir`, so a folder holding a file that is no recording stays and
    the top line says NOT DELETED: OTHER FILES ARE IN IT.
  - What was done, or why not, is on the top line for `kSaidMs` (`said()`).
  - On a recording the bar says HOLD R1 carry where it said L1 / R1 page: it holds five.
  - `tests/picker_test.cpp` (real files; the saved song and rev byte for byte; the list by
    itself with jobs that count), `textsize_test` (each state drawn, the bar's words).
  - **The review's mend (batch 3, 2026-11-15):**
    - **Big moves.** The stick is `kStickRowsPerSecond` 80 times its setting, by the SQUARE of
      the push (a little push reads each name). A held D-pad has a third pace
      (`kFastestEveryMs` 20 after `kFastestAfterMs` 3.5 s), counted from when each row was due
      (several a frame; a stall makes no burst). **L1 held pages on, and R1 where it does not
      carry.** 500 rows: the stick 8.3 s, DOWN held 12.6, L1 held 5.5 (each was 33 or more).
    - **Hold R1 carries only where there is a folder to carry to** (`carries()`: not `flat()`).
      Let go where it was: LEFT WHERE IT WAS.
    - **Hold B on a folder that is not empty says so** after `kHoldSaysMs` (NOT DELETED: IT IS
      NOT EMPTY), and letting go is then no tap. A tap still closes.
    - **`moveSample` answers `NotSaved`** when the file moved but the open song could not be
      written (`saveNow` is a bool); a saved song that could not be is found as its turn comes
      (next rule). Either way the list shows it moved and says MOVED, BUT A SONG COULD NOT BE
      SAVED WITH ITS NEW PLACE.
    - **A column's rows are made when the place or the card changes, never per frame**
      (`index()` counts each folder once; `side()` keeps the two columns beside the cursor's;
      `rowsMade()` is the tests' count). Whatever changes `all_` or `dirs_` calls `index()`.
    - **While a folder's name is typed L2 / R2 and MENU's help are the list's too**
      (`App::typingAFolder`).
    - **`wavNames` lists only what `sampleNameOk` takes** (a file of 64 letters or more is not
      offered: resting on it put a cut name on the lane).
    - **On the WiFi page a file in a folder may be as long as a loose one** (60 with its ending;
      the whole still 63): held to 36, a long recording carried into a folder on the handheld
      was gone from the page, which then refused to delete it. The page's script changed with it
      (tried in a browser, 2026-11-15).
    - **The saved songs follow a move a few files a frame** (2026-11-15, our call left by the owner;
      `Session::followLater`, `followSome` in `update()`): up to `kFollowReadsAFrame` (8) read, and
      none after one is written. Measured on the desk, 40 songs with 10 revs each (one of them the
      bench's 32 KB song): every one naming the recording, all at once was 450 to 740 ms (about 2
      to 4 s on the handheld); a few a frame, the let-go is 1 ms and the longest frame 12 to 16 ms
      (one card write), 440 frames. None naming it: 2 to 3 ms all at once. `moveSample` answers
      `Moving` and the list says MOVING TO X... until `following()` is false, then MOVED TO X,
      or MOVED, BUT A SONG COULD NOT BE SAVED... (`followFailures` went up). **Safe against a
      switch-off**: before any file is touched what is owed is written (`kOwedFile`,
      renames-owed.txt in the song folder, `writeTextFile`; "job" before each batch, FROM tab TO),
      rewritten as each batch ends and deleted at the last; `useSongFolder` finishes it, in order,
      before any song is opened (`finishOwed`; a file not in its words is let be). Gone through
      twice is no harm: it no longer names the old place. **Whatever reads or renames a saved song
      first finishes what is owed** (`followNow`: openSong, openRev, renameSong, deleteSong); a new
      one of those calls it too. If the owed list cannot be written, it is all done at once.
      The WiFi page's renames take the same way. `picker_test` `testSpreadOverFrames`.
- **A recording's name may carry two folders** (owner, 2026-11-14; it was one):
  `kicks/808`, `drums/kicks/808`. `sampleFolderOf` is ALL of the folder (`drums/kicks`),
  `sampleLabel` the last part; a song file's one-folder name is still a name. The whole name
  fits `kSampleNameRoom` (63 letters), on the WiFi page too (`safeSamplePath` ends in
  `sampleNameOk`; each folder 24, the file 60 with its ending, in a folder or out). `wavNames` lists two levels, `wavFolders`
  the folders (empty ones too). The page's folder import still makes one level. Everything that
  takes a name from outside (a song file, the WiFi page, delete) checks it
  with `sampleNameOk` or the page's `plainName`. Show `sampleLabel(name)`.
- **THROW is a value on a hit, a note or a chord** (`throwTo`: 1 ECHO, 2
  SPACE, 3 both; `kThrowEcho`, `kThrowSpace` in drums.hpp). In a song file it
  is one more number at the end of a line, **left off at 0**, so an unthrown
  song is written as it always was: the seventh of `stepfx`, the sixth of
  `note` (the nudge is then written even at 0), the third of `chordfx` (the
  ratchet is then written even at 1). A chord's THROW is every key's.
- **Mute is its own gate** (`SetTrackHeard`), not the track level.
- **A head's M or S mark is a box made to its letter** (`SongScreen::headMarkWidth`: the letter at the
  heads' own lettering, LARGE, 2 pixels clear all round; owner, 2026-11-14: in a fixed 11 by 11 the letter
  stood out of it). Where the mark would stand on the track's number (11 and 12 in the narrowest heads)
  the number is centred in the room left of it, clear of the mixer's mark. Stills `song-marks`, `-12`.
- **Solo is a tap of X on a track** (a cell or its head; owner, 2026-11-15: "I don't like the hold L1
  for solo since Y for mute is just a one tap"). It was a tap of L1, then for a day L1 HELD 400 ms
  (a tap of L1 soloed by mistake too easily); the hold, its timer and `kSoloHoldMs` are gone, and **L1
  does nothing on a track**. **X no longer hears the track on SONG** (`Session::hearTrack` is gone with
  it): a track is heard by X on its DEVICE page. The head's bar says `X solo` and R1 `instrument` again;
  the cell's bar has no room for `X solo` (20 pixels over): mute and solo are said on the head. The help
  card's X line reads "hear it (SONG: solo). Held: a step's effects, a knob's MOD" (joined with the MOD
  screens' line, 2026-11-15; it is held to the card's margin by `testTheHelpCardDrawsSel`). `app_test` `testMuteSoloAndNames`, still `song-head`.
- **AT END: a song played as a song LOOPs (as it always did) or STOPs**
  (owner, 2026-11-14; `Project::songEnd`, `kSongLoop` / `kSongStop`). In a
  song file `songend 1` among the song's own lines (`cutSong` knows it),
  **left out at LOOP**, so a looping song is written as before and an old
  program plays it round. In undo with the song (`restorePatterns`); told to
  the engine by `syncSong` (`Command::SetSongEnd`, `sentSongEnd_`, live: it
  may be switched while the song plays). **LOOP is the engine as it was, to
  the bit** (`effects_test --print` and a busy song against `main`, the
  same). STOP: the engine asks nothing early of the top at the last step
  (`playsOnce()`, as a bounce's `once_`), and on the first sample of the step
  after the last step of scene `sceneCount_` (its last time round) it stops
  exactly as `Command::Stop` does (`stopPlaying`, `songEndsNow`, checked in
  `render()` before anything of that sample). Not for a scene alone, not for
  a bounce (`once_`: it stops itself, so a STOP song bounces to the same
  file). **The engine says it stopped, the app does not guess**: it counts
  the Plays it takes (`playsTaken_`) and keeps the one that ended in
  `songEnded()`; the Session counts the Plays it sent (`playsSent_`) and
  takes `project.playing` down only when the two are equal, so a START
  pressed just after the end is never lost. The place: the PLAYS box of the
  first empty row (`SongScreen::songEndRow`, `onSongEnd`) shows a loop arrow
  (dim) or a square (brass); A switches, B puts LOOP back; that box had a
  repeat count of a scene with nothing in it, which it no longer shows.
  Tests: `tests/songend_test.cpp`; stills `song-end-loop`, `song-end-stop`.
  - **A `songend` number nobody knows is LOOP** (`songEndKind`), as an unknown
    GLIDE FROM is TOP: a later program's third choice plays round here.
  - **Where the song stops is a bar line for Catch** (`Engine::barLine`, called
    by `fireStep` and before `stopPlaying` in `render()`): a bar's end is
    marked where the next begins, and a stopped song begins none. Without it
    Catch after the end gave the bar before the last. (A Stop by hand exactly
    on a bar line still gives the bar before: as on `main`.)
  - **AT END switched during the song's LAST step asks that step again**
    (`askAgain(true)` from `SetSongEnd`, only when `playsOnce()` flips and only
    on that step): to LOOP, the top's early hits and notes are asked for (it
    is LOOP all along, to the bit, if none was due yet); to STOP, those not
    yet begun are taken back. On any other step, or told what it already is,
    nothing is asked again: a ratchet's waiting pieces would be dropped.
  - **Only Plays that went are counted** (`togglePlay`: not a Stop, not a
    Play lost to a full queue), and `playsSent_ != 0` keeps a play mark set
    by hand in the tests ("as if playing") from being taken down.
- **The mixer (owner, 2026-11-14): hold A on a track's head on SONG, or on
  MASTER (the head right of + TRACK), and UP / DOWN is LEVEL, LEFT / RIGHT PAN;
  B with A held puts both back.** A tap of A on a head still names the track:
  it acts on letting go, as on a cell, and **only if no other button was
  pressed while A was held** (`mixTurned_`) **and it was let go before the card came up**
  (`kMixCardMs`, 300 ms: `mixShown_`; owner, 2026-11-14: a look at the level opened the keyboard); while A is held on a head every
  other button but START does nothing (Y there muted the track under the
  card). **A pressed while B's delete is filling calls the delete off**, on a
  head and on a cell (`update()`), and is then no tap. `TrackData::mixDb` (whole dB from the
  usual, -40..+6) and `mixPan` (-50..50), `Project::masterDb` / `masterPan`;
  `Session::setMix(track, db, pan)` takes `kMasterTrack` and nothing else
  below zero, calls `noteEdit()` (a step of undo: `restorePatterns` puts them
  back, from the head for the master and from each track's text read).
  In a song file `mix DB PAN` on a track and `master DB PAN` among the song's
  own lines (`cutSong` knows it), each **left out at 0 0**. **0 DB is
  `kUsualTrackLevel` (0.65) exactly** (`mixGain`), and the engine's PAN is
  `panGains`: equal power, the middle exactly 1 and 1. Level and pan glide
  for `kSendGlideSeconds`; at rest in the middle a track runs the loop it
  always ran, and the master at 1 and the middle is not run at all
  (`runMaster`), so a song left alone is the same to the bit (`effects_test
  --print` the same before and after). The thrown copy of a drum hit takes
  the level's glide but not the pan (it is one sound for both sides of a bus).
  Not automated. MASTER takes a column, so with 11 or 12 tracks the heads are
  42 px, their numbers held at LARGE, and DRUMS reads DRUM. Tests:
  `tests/mixer_test.cpp`; stills `song-mixer`, `song-mixer-master`, each also
  `-12`.
- **The WiFi page is PIN-gated and checks every file.** A program update must
  be accepted on the device (hold A). **Five wrong numbers lock it for a
  minute** (`Transfer::kWrongMost`, `lockoutMs`), counted for the whole server,
  every request refused with 429 meanwhile, the right number too. Only a path
  whose first part is all digits is a try (a browser asks for `/favicon.ico`
  by itself). **A right number does not begin the count again** (the owner's
  page asks with it all the time: a guesser beside it was never stopped); a
  minute with no wrong number does, and so does a fresh `start`. **An update,
  once accepted, makes the hints bar's end say RESTART TO UPDATE in brass until the program starts again** (owner,
  2026-11-14; `Session::updateReady`, `ui::gUpdateReady`): it takes the help
  words' place and is always drawn, the hints (and a strip's chips, the dice
  at the end too) giving way to it (`hintsEnd`). MENU still opens the help.
  `ui_test` (`testTheBarSaysAnUpdateWaits`) and `transfer_test` hold it;
  stills `update-ready`, `update-ready-drums`, `update-accepted`.
- **A song sent under the open song's name is refused** (409, "that song is
  open on the handheld", case not counted: a card may not tell them apart).
  The Session tells the server the name (`Transfer::setOpenSong`, from
  `update()` when it changes and when WiFi goes on). **`saveNow` holds
  `Transfer::songFiles()` while it writes**, as the server does.
- **A folder of recordings is deleted only on purpose** (owner, 2026-11-15: "Keep them"):
  one that loses its last recording, deleted or moved out, stays, on the page as on the
  handheld. `DELETE /PIN/folders/NAME` (`safeFolderPath`: one or two plain names) deletes an
  EMPTY one by `rmdir` (409 with anything in it, 404 for a link or one inside a link); the
  page offers DELETE FOLDER on a heading with no recording and no folder under it. Folders
  made for a request that is then refused are taken away again, and only those (`foldersMade`
  keeps what it made, `dropMade`): a folder that was there before is never touched.
- **The sound designer page knows three kinds of track** (2026-11-16): the spec's `"page"` is
  `"synth"`, `"sampler"` or `"kit"` (`Session::designSpec` / `samplerSpec` / `kitSpec`), and the state's `"spec"` number
  (`designSpecSerial_`) goes up whenever the spec's text changes, which is when the page asks for it
  again. A sampler's spec carries `"waves"`: a list of pictures, each a recording's 512 loudest points
  (`LaneSound::peaks`, made on the app thread when the recording is read; 0 to 1000) and the settings
  that mark it (`"marks"`, ids, kept `"least"` apart by `sampler::heldApart`, the screen's rule too),
  and `"recordings"`, read from the card on the app thread only while the page looks (at once, on
  `Transfer::received()`, then every 5 s). `sample NAME` takes only a name in that list.
  - **The KIT** (owner, 2026-11-16: "I think the kit could also be helpful"): a card a lane, built by
    `laneCard` round the same `wave()`. **A lane's setting is the id `lane * kKitStride + field`**
    (`designer.hpp`: fields 0 to 7 are `LaneEditRow`'s, then `KitField`: FIT, CHOKE, VOLUME, HUMAN
    TIME, HUMAN LEVEL); `"fields"` gives each once, every reading that does not hang on the lane with
    it (START and END are a time into the lane's own file: the state gives the handheld's eight
    readings a lane, `laneEditText`). `Session::serveKit` routes `set` by lane into the calls the
    handheld makes (`setLaneEdit`, `setLaneFit`, `setLaneChoke`, `setLaneLevel`, `setLaneFeel`);
    `lanesample L NAME` (only a name offered, or none: the drum), `hear L 1|0`, `normalize L`,
    `trim L`. A synth's or a sampler's asks do nothing to a kit, but `name`. **VOLUME is
    `Session::setLaneLevel`, which the drum grid now calls too.**
  - **The picture is the EDIT view's**: the lane's whole source (`LaneSound::sourcePeaks`, made on the
    app thread when the lane is read; `serveDesigner` readies every lane while the page looks), the
    part played lit by GAIN and the fades. The page's `guarded` and `levelAt` copy `lane_edit.cpp`'s
    `guarded` and `laneEditLevelAt` for that: **change them together** (tried against the C++ numbers
    in the browser, 2026-11-16). So an edit changes no spec; another sound does: the spec is made
    again when any lane's `LaneSound::version` moves (their sum), and its number goes up only when its
    text changes.
  - **PLAY held is `hearLaneHeld` with the Designer as its owner**, and `keepHearing` leaves that one
    to the page (X is not held for it): it is let go by `hear L 0`, by the page going away, or by
    another track on the handheld.
  - `design_test` `testSamplerFromThePage`, `testKitFromThePage`; `APOTHECA_DESIGN_SAMPLER=1` or
    `APOTHECA_DESIGN_KIT=1` with `APOTHECA_DESIGN_HOLD` holds a page open for a browser (the KIT's
    lanes are printed whenever one changes on the handheld).
- **The page's own script has no test in `ctest`.** A change to it is tried
  in a browser (Playwright is installed) against a `Transfer` started by a
  small program, as the folder import was (`planFolders` can be called from
  `page.evaluate`). Say in the reply that it was, or was not.
- **The WiFi server's thread never touches the `Project`.** What it did to a
  recording goes on a list (`Transfer::takeChanges`), put there BEFORE
  `received()` goes up; `Session::update` reads it and calls
  `recordingChanged`. A rename changes the name only: reading the file again
  would cut the sound. A recording SENT (no old name) is read again on every
  lane and sampler track that names it, so a replaced file is heard at once.
- **A missing recording is looked for by base name, and taken only if there
  is exactly one** (`Session::findMissing`, run after a song or a rev opens,
  not from `applyProject`). It lists the samples folder once, and not at all
  when nothing is missing. Never guess between two.
- **A rename on the page rewrites the saved songs and revs** on the app
  thread, a few files a frame (`Session::followLater`, as a carry; `renameRecordings`): only the name on
  `sample` and `sound` lines, the rest byte for byte, the file's date kept.
  The open song's own file is left to the autosave. It holds
  `Transfer::songFiles()`, which the server holds while it writes or deletes
  a song: anything new that writes songs off the app thread takes it too.
- **A bounce runs until its tail ends** (owner, 2026-11-12; `BounceTail` in
  `session.hpp`, no engine and no file in it, tested with made-up tails).
  Sound is `kBounceFloor` (one step of a 16-bit file) or more; it is over
  after `kBounceQuietSeconds` (4) without any, and the file ends half a second
  after the last sound. **Four seconds must stay longer than the longest gap
  between two echoes** (3 s: a half note at 40 BPM), as the bus rest time
  must. The cap (`kBounceCapSeconds`, 480) must stay longer than
  `kLongestTailSeconds` in the engine; both are checked.
- **A bounce is written as it goes**, to `NAME.wav.part`, and renamed when it
  is whole: a tail of minutes never sits in memory, and a cancelled bounce
  leaves nothing. One that is KILLED (power, a crash) leaves its `.wav.part`:
  `removeHalfBounces` takes those away when the program opens
  (`useBounceFolder`) and when a bounce starts, never while one is under way. The tracks of an EACH TRACK bounce are padded with silence
  to the longest. Its percentage counts the song's own playing only; in a
  tail `bounceTail()` gives the seconds so far and the row says that instead.
- **Do not pipe the app's output into `head`** when making a still; it dies
  before it saves.

### Tests
- **Mutation testing is the standard.** After a test passes, break the code it
  covers and see it fail. A surviving mutant usually means the fixture cannot
  reach the branch.
- **Do not compare audio sample for sample across separate notes** unless the
  engine is fresh; compare loudness, attack, or zero crossings. Let auditions
  end before listening.
- **An `Engine` is about 1.4 MB: a test keeps at most three on the stack
  at once** (`std::unique_ptr` for more). Six were 8.37 MB, over the main
  thread's 8 MB once MOD grew the Engine by a kilobyte, and `engine_test`
  crashed with no word, five runs in six (2026-11-14). `g++ -fstack-usage`
  on a test file says each function's frame.
- **Every test has a two-minute limit**, so a hang is a failure. A new ctest
  entry gets its `TIMEOUT 120` in CMakeLists.txt (launcher and words had none
  until 2026-11-14). **`effects_test` runs as two halves** (`--part 1`,
  `--part 2`; part 2 from `testFamiliesAndTypes`, so a test added at the end
  is in part 2; about 34 s each on a quiet desk, 2026-11-14). Move that line
  if one half grows much longer than the other.
- **Checksums of sound only hold on the machine's own maths library**
  (`tanh`, `pow`, `exp` differ in the last bit elsewhere, and compilers join
  a multiply and an add differently on ARM). `effects_test` checks which it is
  on (`mathsPrint`) and falls back to loudness and peak, saying so. To prove a
  change is bit-exact on another machine, run `--print` before and after and
  compare.
- **`effects_test --only testDuck testTape`** runs just those (a second or
  two, for trying a change); `ctest` runs them all. `through()` there hits a
  drum five times a second, lanes 1 and 8, so the checks for every effect
  reach DUCK; no other effect hears it.
- **The measuring tools are one copy, in `tests/measure.hpp`** (JX-3P plan,
  batch 0): a tool that reads a run of samples and knows no Effect or Synth
  goes there, not into a test program of its own. Moved out of `effects_test`
  unchanged: `--measure` and `--print` gave the same lines before and after
  (but for the block timings). The older copies in `engine_test`
  and `phazed_test` are other tools under the same names
  (interleaved buffers, int places) and were left. Batch 2 added `partNear`
  (one frequency through a window, any pitch), `spectrumOf` and `asideOf`
  (an oscillator's aliasing: the loudest part that is not a harmonic, under
  a frequency). **A band-limited wave's zero crossings are not its pitch near
  the top** (its ringing crosses too): read it through `bandOf` first.
- **A `bench` row that is the difference of two small figures times the two
  in turns** (many short rounds, each its best: `modsAlone`, `macroModsRun`).
  Timed one after the other, a busy desk moved the whole row more than the
  difference: 24 MODs read -0.04% in one run and +0.10% in the next.
- **Numbers measured from today's effects are written in `effects_test` as
  comments and checked** (`--measure` prints them). A new effect's row in the
  plan's testing table is checked with the same tools.
- When replacing a function by text range, bound it by **the next function in
  the file**, then count the functions before and after.
- **A test's temporary folder comes from `tempFolder("name")`**
  (`tests/temp_folder.hpp`): `$TMPDIR` or /tmp, `apotheca-name-XXXXXX`,
  deleted with all in it when the program ends normally (never before, so a
  second Session may open it again). A crash or a timeout keeps it (nothing
  is deleted from a signal handler); `APOTHECA_KEEP_TEMP=1` keeps them for a
  look. Links are deleted as links, never followed. **`ctest -R temp` fails a
  test source that says `mkdtemp(` or `"/tmp/apotheca`**, and a shell test
  whose `mktemp` has no `trap ... rm -rf` (2026-11-16: the tests had left
  33,000 folders, 16 GB, and filled the disk mid-build). A test that writes
  "../x" to prove a path is refused does it one folder down inside its own.

## Measured (desk machine; the handheld is roughly a fifth as fast)

| Thing | Cost |
| --- | --- |
| Phazed, 8 notes | 0.9% of a core |
| PHYSIC, 8 notes struck again every half second, none asleep (2026-11-15, `bench` in a container whose Phazed row read 0.7%): MARIMBA / STEEL (a string) / the worst (CHURCH BELL, DICE 100, every note gliding) / asleep. **Not yet run on the handheld** (x 13 to 17: physic-plan.md, section 14) | 0.21% / 0.23 to 0.37% / 0.40 to 0.59% / 0.01% |
| PHYSIC with batch 10 (2026-11-16, `physic_test --measure`, the same container): Twelve (PAIR: two strings a note) / every BODY page setting on, not gliding (BELL, DRUM, STRING, MARIMBA) / the same, every note gliding (BELL, STRING) / NOISE 2 s bursts on GLASS, STRING. **Not yet run on the handheld** | 0.44% / 0.43 to 0.57% / 0.73 to 0.77% / 0.48%, 0.64% |
| ACID, one voice playing a busy 16-step line at 130 BPM (accents, slides, RESONANCE 85, ENV MOD 70: its cutoff moving all the while; batch 1, 2026-11-16): `acid_test --measure cost` / `bench`'s row / with the cutoff still / asleep. The plan's target was 0.15%. **Not yet run on the handheld** | 0.13% / 0.14% / 0.10% / 0.00% |
| CLOUD (batch 1, 2026-11-16, a busy desk: load 5 to 7; in the same `bench` run PHYSIC 8 notes 0.31%, ACID 0.21%, JAX-EP 6 notes 0.64%, a third over their usual): one key of preset 0 (two grains) / a 4-key chord / the pool full (six keys at 200 grains a second, SIZE 0.35 s, PITCH SPRAY on: 24 grains, all Hermite). The plan's targets 0.08% / 0.5% (ceiling 0.7%). `cloud_test --measure cost` / `bench`. **Not yet run on the handheld** | 0.05 to 0.07% / 0.19 to 0.24% / 0.53 to 0.55%; `bench` 0.06% / - / 0.65% |
| ...`bench`'s rows after batch 10 (one run, its Phazed row 1.0%): Marimba / Twelve / breathed (Wine Glass, NOISE) / the worst (Church Bell, DICE 100, gliding, PAIR, GUITAR, MOTOR, BUZZ) / asleep | 0.21% / 0.44% / 0.48% / 0.66% / 0.01% |
| FM-6, 8 notes of E.PIANO 1 / 16 of STRINGS 1, keys again every half second (2026-11-15, `bench`; the plan's three runs 0.49 to 0.63% / 1.02 to 1.30%). QMOD's 8 were 2.6 to 3.4%. **Not yet run on the handheld** (estimated 6 to 10% of a core there for 8, QMOD's measured 57%: fm6-plan.md, section 6) | 0.67% / 1.22% |
| The busiest song with FM-6 where QMOD was (2026-11-15, one run): the audio thread alone / 2 helpers. **Not yet run on the handheld** | 16.9% / 13.1% of a block, worst 3.0 / 4.4 ms of 5.33, 0 of 1875 late |
| FM-6, a macro changed, a call; 24 MODs on its macros run, twelve silent tracks (the same run: Phazed +0.18%, JAX-EP +0.14%) | 0.035 us; +0.14% of a core |
| JAX-EP with its presets (batch 7): 6 notes of A1 String I / the worst of the 32, B15 Planet, keys again every half second / moving cutoff (2026-11-14, three runs: Phazed 0.7%, QMOD 2.8 to 2.9% in them). Each of the 32 that way: 0.47% (B3 Oboe) to 1.07%. **Not yet run on the handheld** | 0.55 to 0.56% / 1.03 to 1.06% / 1.78 to 1.83% |
| JAX-EP, the same three rows with Twin Analog's code out (batch 8b, 2026-11-15, three runs; the engine's sound not touched: `effects_test --print` the same before and after; Phazed 0.7 to 1.2%, QMOD 2.8 to 3.9% in them). How it got there, batch by batch (0.08% for the bare skeleton), is in `docs/plans/jx3p-plan.md` | 0.55 to 0.67% / 1.04 to 1.26% / 1.78 to 1.79% |
| JAX-EP after the cutoff's range and the VCA's 0 dB moved (2026-11-15), `bench`'s three rows, the four quieter of seven runs on a busy desk (Phazed 0.7 to 1.1% in them). The 32 one by one were not timed again. **Not yet run on the handheld** | 0.55 to 0.56% / 1.03 to 1.14% / 1.77 to 1.86% |
| JAX-EP with the VCA straight and its clip counted (2026-11-15, `bench`'s three rows, the two quieter of five runs on a desk shared with another build; Phazed 1.0 to 1.2% in them). **Not yet run on the handheld** | 0.56% / 1.05 to 1.11% / 1.79 to 1.81% |
| JAX-EP after the resonance was mended (2026-11-16; the read-ahead and the low end given back), `bench`'s three rows timed in turns against the build before, each the best of 60 half seconds, on a busy desk: before 0.51 to 0.54% / 0.98 to 1.02% / 1.72 to 1.78%. **Over on resonant patches (+15%, +13%), under at RESONANCE 0 (-7%)**. Not run on the handheld | 0.49 to 0.50% / 1.14 to 1.19% / 1.92 to 2.01% |
| Drums, 8 lanes | 0.1% |
| FILTER / DRIVE / DELAY / REVERB / CRUSH, one each | 0.03% / 0.12% / 0.02% / 0.07% / 0.01% |
| TAPE / HALL / HALL with TONE above 50% (a later, slower run: REVERB read 0.09% in it) | 0.13% / 0.18% / 0.21% |
| Feeding both send buses from 12 tracks | 0.03% |
| ...and a DELAY and a REVERB on them | 0.12% |
| ...or TAPE and HALL, what a new song has (2026-11-12, a slower run: DRIVE 0.18%, DELAY and REVERB on the buses 0.09 to 0.18%) | 0.41 to 0.50% |
| Every hit of twelve busy drum tracks thrown to both buses, nothing on them (2026-11-12; the tracks themselves 1.45%, as before THROW) | 1.2% |
| CHORUS / COMP / SATURATE, one each (2026-11-12, a run where DRIVE read 0.16%) | 0.18% / 0.11% / 0.18% (0.15% before its plain sound was made late to match) |
| DUCK, dipping eight times a second (2026-11-12, a run where DRIVE read 0.12%) | 0.02% |
| SPRING (2026-11-13, four runs on a shared desk: HALL 0.15 to 0.17%, TAPE 0.10 to 0.14%, DRIVE 0.12 to 0.21% in them). Its springs run at a fifth of the sample rate | 0.13% (0.16% in one run) |
| TAPE with its way down, the same run (0.10% just before it) | 0.11% |
| PING (2026-11-13, four runs alike; in them DELAY 0.02%, TAPE 0.09 to 0.10%, HALL 0.15%, DRIVE 0.12 to 0.14%) | 0.02% |
| PLATE (2026-11-13, six runs: HALL 0.15 to 0.16%, TAPE 0.09 to 0.10% in the same) | 0.14 to 0.15% |
| COMB / VOWEL / EQ (2026-11-14, four runs: DRIVE 0.12%, HALL 0.15%, FILTER 0.03% in all) | 0.04% / 0.08% / 0.07% (one run 0.07 / 0.11 / 0.09) |
| ...each with a setting turned all the while (PITCH, VOWEL, MID FREQ: worked out again every 16 samples) | 0.08 to 0.10% / 0.13% / 0.10% |
| FOLD / FUZZ / RING (2026-11-14, four runs: DRIVE 0.12%, SATURATE 0.14 to 0.15%, HALL 0.15% in all; FOLD was 0.30% before its triangle lost its branches; 0.58% since it runs at four times the rate, two runs, DRIVE 0.12%) | 0.58% / 0.16 to 0.18% / 0.03 to 0.04% |
| ...FOLD's DRIVE, FUZZ's TONE and RING's FREQ turned all the while (FUZZ's TONE was 0.61% before its making up was summed once); RING with a SQUARE | 0.66 to 0.67% (0.32 at twice the rate) / 0.20 to 0.22% / 0.04% (0.06% in one run); 0.04% |
| WEAR / LIMITER / SNAP (2026-11-14, ten runs on two desks: DRIVE 0.12 to 0.13%, HALL 0.15%, COMP 0.09 to 0.13% in all) | 0.15 to 0.16% (0.40% before its loop worked on locals) / 0.05% (0.09% in one run) / 0.08% |
| ...WEAR at DIRT 100%, or its WOW turned all the while (0.23% in one run); LIMITER at +20 dB (turning down all the while); SNAP's ATTACK turned all the while | 0.15% / 0.12 to 0.13% / 0.08% |
| FLANGER / ENSEMBLE / PHASER / TREMOLO (2026-11-14, four runs: DRIVE 0.12%, HALL 0.15%, CHORUS 0.15% in all) | 0.07% / 0.16 to 0.17% / 0.09 to 0.11% / 0.07% (0.11% in one run) |
| ...FLANGER's, ENSEMBLE's and PHASER's DEPTH turned all the while; TREMOLO at RANDOM | 0.07% / 0.16 to 0.17% / 0.09 to 0.11%; 0.06% |
| DUB / REVERSE / STUTTER (2026-11-14, three runs: DRIVE 0.12%, TAPE 0.10%, PING 0.02%, HALL 0.15 to 0.21% in all) | 0.05% / 0.03% / 0.03 to 0.04% |
| ...DUB howling (FEEDBACK 150%) or its TONE turned all the while; REVERSE at FEEDBACK 100%; STUTTER at CHANCE 100% | 0.05 to 0.07% / 0.03% / 0.04% |
| BLOOM / SHIMMER (2026-11-14, three runs: DRIVE 0.12%, HALL 0.15%, PLATE 0.14 to 0.15% in all; on locals, the sway's weights once a tick: they were 0.74% / 0.56%) | 0.55 to 0.56% / 0.35% |
| ...BLOOM turned all the while; SHIMMER at 0, or its DECAY turned all the while | 0.66 to 0.67%; 0.34 to 0.35% / 0.39 to 0.40% |
| ...TAPE and PLATE on the buses of twelve busy drum tracks (TAPE and HALL 0.28 to 0.39% in the same runs) | 0.28 to 0.42% |
| Everything at once on twelve busy drum tracks: every hit thrown to both buses, sends up, TAPE and HALL on them, a DUCK on each track (2026-11-12; the tracks themselves 1.44%, DRIVE 0.12%) | 1.70% |
| FEEL on every lane of twelve busy drum tracks, HUMAN TIME and HUMAN LEVEL at 100% (2026-11-15, three runs: the tracks themselves 1.47 to 1.48%, DRIVE 0.12%; the same plain song measured again read -0.03 to +0.04) | 0.10 to 0.11% more; HUMAN TIME alone 0.08 to 0.11; HUMAN LEVEL alone nothing that shows |
| The step clock alone, eleven silent synth tracks, four plain chords of four keys a bar on each (2026-11-12; 0.68% before chord effects: the cost is looking through each track's waiting notes every chunk) | 0.69 to 0.72% |
| ...every chord WIDE STRUM, RANDOM, the three human amounts at 100%, x4 | 1.22% |
| ...every chord an arpeggio at 1/4 step, RANDOM, human at 100% | 1.14% |
| ...plain chords, two gliding notes after each, the tracks' GLIDE FROM at TOP / PAIRED / NEAREST / RANDOM (2026-11-13, three runs; plain chords alone 0.71 to 0.75% in them, 0.71 to 0.76% just before GLIDE FROM) | 0.71 to 0.72% each: the choice costs nothing that shows |
| The busiest song (`bench_run.cpp`, `busiestSong`: 12 tracks, chords, throws, three effects a track, buses) in real time, 256-frame blocks: the audio thread alone / with 2 helpers (2026-11-14; SPEED TEST shows both, so the handheld can say whether the helpers carry it; **not yet run on the handheld**) | 19.9% / 13.5% of a block, worst block 2.98 / 2.75 ms of 5.33 |
| ...the same song with its two Twin Analog tracks (x2) now two JAX-EP tracks on A1 (2026-11-15, three runs after; one run before on the same desk that day read 23.0% / 13.8%). The figures above are the old song's. **Not yet run on the handheld** | 19.6 to 21.1% / 11.6 to 14.2% of a block, worst block 2.92 to 5.95 / 3.96 to 6.08 ms of 5.33 (a shared desk: 0 to 2 blocks of 1875 late) |
| A macro changed, the synth alone, 8 notes held, a call (MOD batch 0, 2026-11-14; Phazed 8 notes 0.7%, QMOD 2.8%, DRIVE 0.12% in the same run): Phazed / QMOD / Twin Analog (gone since; `bench` now has JAX-EP's row in its place, not yet recorded here). The same value: 0.002 us each | 0.36 / 0.95 / 0.05 us: 24 MODs on QMOD macros 0.21% of a core at a 512-sample tick, 1.71% at 64 |
| MODs worked out, twelve silent drum tracks, on the sends (MOD batch 1, 2026-11-14, two runs; Phazed 0.7%, QMOD 2.8%, DRIVE 0.12% in them): 1 MOD / 24 MODs, over none. **Since 2026-11-15 the three engines are timed in turns** (`modsAlone`, 40 rounds, each its best): one after another, one run read 24 MODs -0.04% and the next +0.10%. Six runs that way, load 1.5 to 3.4: +0.028 to +0.046% / +0.084 to +0.122% | +0.028% / +0.096% (over the plan's 0.02% / 0.03%: for its cost batch) |
| 24 MODs on macros RUN through the engine, twelve silent tracks of the synth, a SINE and a STEPPER each (2026-11-14, two runs, a build beside them): QMOD / Phazed, over none | +0.26 to +0.27% / +0.15 and +0.36% (twelve silent Phazed tracks are 2.3 to 2.4% by themselves: noisy) |
| ...the same, the two engines timed in turns (2026-11-15, three runs, load 2.3 to 3.4; one after another a Phazed run had read -0.06%): QMOD / Phazed / JAX-EP | +0.26 to +0.27% / +0.14 to +0.21% / +0.13 to +0.14% (+0.04% in one run) |
| JAX-EP, a macro changed, a call (2026-11-15, five runs of six; no early return on the same value) | 0.12 us (0.20 in one run): 24 MODs 0.03% of a core at 512 samples, 0.22% at 64 |
| Chunks a second, the busiest song: no MOD / a MOD on (one or 24) | 1030 / 1285. The song's own time with them is within the desk's noise (21.7 / 24.3 / 22.5% alone in the quieter run) |
| The busiest song with 24 MODs (MOD batch 9, 2026-11-15; `busiestMods`: two a track on macros, effect settings, the ECHO send, LEVEL and PAN, every MOD 1's DEPTH lane written; SPEED TEST runs it ten seconds, as the song without), real time, the audio thread alone / 2 helpers, six runs on a quiet desk (load 0.6 to 2.3). In the same runs the song with no MOD: 18.3 to 22.2% / 12.3 to 17.2%; with 1 MOD: 18.1 to 22.1% / 13.1 to 16.9%. **Not yet run on the handheld** | 19.0 to 26.1% / 14.3 to 16.6% of a block, worst block 3.2 to 9.4 / 3.9 to 10.4 ms of 5.33 (one run 60 ms: the machine; 0 to 2 blocks of 1875 late, as with no MOD). **Within the desk's noise of the song without**: the rows cannot tell the MODs' cost apart |
| ...chunks a second: no MOD / 1 MOD / 24 MODs | 1030 / 1285 / 1287 |
| MODs worked out, twelve silent drum tracks (the same six runs, the engines timed in turns): 1 MOD / 24 on the sends / **24 on LEVEL and PAN** (batch 8's moving level and pan loop), over none (0.171 to 0.174%) | +0.030 to +0.033% / +0.093 to +0.098% / +0.123 to +0.126% |
| ...a macro changed, a call: Phazed / QMOD / JAX-EP (the same runs) | 0.36 / 0.92 / 0.11 us; 24 MODs on QMOD's macros run: +0.27% of a core |
| 24 LFOs FREE against the same 24 in time, twelve silent drum tracks, on the sends (2026-11-16, three `bench` runs, the six engines timed in turns, load 5 to 12; the 24 in time +0.074 to +0.090% over none) | -0.002 to -0.005%: nothing that shows |
| One idle frame | 0.03 ms |
| A frame that ends an edit (undo snapshot) | 0.32 to 0.37 ms (speed batch 4, 2026-11-17, `bench --app`; 0.57 to 0.59 before, 12.3 on the handheld). **Handheld: about 7 ms, an estimate** |
| Undo plus redo (bench: twelve tracks, an edit to one) | **0.63 to 0.67 ms** (speed batch 4; 20.9 to 21.7 before it on the same desk, the handheld's SPEED TEST 200). **Handheld: about 6 to 10 ms, an estimate** |
| ...a knob and a hit on two tracks, undone and redone | 1.1 to 1.3 ms (10 to 11 before batch 4; 213 on the handheld). Handheld estimate 12 to 20 ms |
| The bench's song (33 KB) as text / read back into a song | 0.68 to 0.76 ms / 0.77 to 0.84 ms (1.61 to 1.75 / 3.88 to 3.95 before batch 4; 17.5 / 47 on the handheld). Handheld estimate about 8 / 10 ms |
| The busiest song shared out other ways (speed plan batch 0, 2026-11-15, one run, load 6): 2 helpers / 3 / 2 heaviest first / 3 heaviest first. **The desk cannot tell; the device's SPEED TEST decides** | 12.0 / 13.3 / 11.4 / 11.1% of a block |
| A screen pass while the song plays, SDL's software renderer (batch 0): SONG / drum grid | update 0.06 / 0.05 ms, drawing 1.7 / 1.6 ms |
| Instructions in one second of sound (callgrind, clang -O2 -g, `bench --profile`; speed plan section 8): busiest / twelve silent Phazed / eleven silent samplers, chords / twelve drum tracks / thrown, before batch 1. The busiest song then had two QMOD tracks where FM-6 is now | 2,070 / 351 / 67.5 / 214 / 435 M |
| ...after batch 1 (the step clock walks only what waits, Phazed rests, its tables shared) | 2,023 / 39.0 / 22.1 / 212 / 432 M; the busiest song's L2 read misses (a 256 KB model) 5.61 to 3.60 M |
| Twelve silent tracks after batch 1 (one run, load 6): Phazed / QMOD / JAX-EP; eleven silent samplers with plain chords | 0.32 / 0.15 / 0.15%; 0.15% (were 2.38 / 0.70 / 0.71%; 0.72%) |
| ...after batch 2 (the drum voice loop in runs): instructions, twelve drum tracks / thrown / busiest; timed (load 7 to 9), twelve drum tracks / every hit thrown, added / the busiest song alone, 2 helpers | 53 / 143 / 1,877 M (were 212 / 432 / 2,023); 0.43% / +0.31% (were 1.47% / +1.18%); 15.5 / 10.8% of a block |
| The busiest song with FM-6 (main 0.565) without / with speed batches 0 to 2; twelve silent FM-6 tracks (instructions, a second of sound) | 1,617 / 1,424 M; 69.1 / 22.6 M |

**No synth is far costlier than another now**: Twin Analog (4.7% and up) went in batch 8b,
QMOD (2.6 to 3.4%; 57% of a handheld core) on 2026-11-15, for FM-6. **No synth has a quality setting and nothing watches the load**:
`Instrument::setQualityLimit`, `Engine::watchLoad` / `qualityLimit` / `load`
/ `noteLoad`, `Project::busy` and the top bar's BUSY: OVERSAMPLING DOWN went
with Twin, their only user. A song too heavy for the handheld stutters; SPEED
TEST's busiest song is how to see how near one is. If a synth ever needs the
watch again, it is in git (the commit before batch 8b).

## Next, in the owner's order

1. The effects plan (`docs/plans/effects-plan.md`): all built and joined (0.452): the seven families and their 27 types.
2. Twin Analog became a JX-3P clone, **JAX-EP on the device** (`docs/plans/jx3p-plan.md`; branch `jx3p`: **batches 0 to 8c are done (2026-11-15)**: the engine, the app, the 32 presets, the tests off Twin, Twin's code out, the docs. Not joined to `main`, nobody has heard any of it, SPEED TEST has not run on the handheld. Left: batch 9, calibration from recordings, which waits on the owner's source; and the owner's open calls in the plan's section 8).
3. LFO and random stepper per track (`docs/plans/modulation-plan.md`): batch 0, the groundwork, is built (branch `mods`: the rule, `giveLane`, DETUNE mended, QMOD's early return, the cost row), and batch 1, the engine (the tick, `runMods`, the commands, X held while stopped); their review is mended (the plan's section 14); batch 2, the song, is built too (`TrackData::mods`, `Session::setMod`, the `mod` / `modsteps` words, undo, the TYPE carry: section 15); **batches 3 and 4 are built (2026-11-15, the owner's calls A to H taken; section 18): X held on a knob is its MOD, on DEVICE and FX**; **batch 5, the MOD tab after FX, is built (2026-11-15, section 20)**; next, batch 6 (the brass mark on the target's own bar). **Joined with `main` (2026-11-15)**, and the join's review mended (section 17: no defect, the bench's MOD rows timed in turns): FEEL, AT END, the mixer, the picker, JAX-EP and Twin's removal sit beside the MODs; nobody has heard a MOD.
4. **PHYSIC** (owner, 2026-11-14): a physical-model synth for plucked and struck sounds (strings, mallets, bells). Plan: `docs/plans/physic-plan.md`. **Built and joined: the engine (2026-11-15, 24 presets) and the app (2026-11-16). Batch 10 (BODY, PAIR, MOTOR, NOISE, BUZZ, a fifth page and 8 presets: 32) on branch `physic2`, 2026-11-16. Nobody has heard it, not run on the handheld.**
5. **An acid mono bass** (303-style: accent, slide; **ACID, built 2026-11-16, batches 0 to 5 of `docs/plans/acid-plan.md`: the engine, the app, 32 presets, README. Nobody has heard it, not run on the handheld; batch 6, calibration from recordings, waits on the owner's source**), then **a granular synth** (clouds from the recordings) (owner, 2026-11-14; **CLOUD, planned 2026-11-16 in `docs/plans/granular-plan.md`; its engine and its app built on branch `cloud` (batches 0 to 2; the pool 24, not 32), not heard, not run on the handheld; batch 3's 24 presets and batch 4's designer page too; next: 5 (review, README)**).
7. **FEEL on the drum grid** (owner, 2026-11-14; `docs/plans/drum-feel-plan.md`): HUMAN TIME and HUMAN LEVEL for each lane, live, the main beats tighter. **Built on branch `feel` (2026-11-15), not joined, not heard by anyone**; SWING later.
6. **The sample picker in columns** (owner, 2026-11-14; `docs/plans/picker-plan.md`): three columns as yazi, folders two levels deep, new folders and moving recordings on the device. The owner asked for it to go soon: after the JAX-EP's presets. **Batches 0 to 2 are built on branch `picker`** (two levels underneath, the three columns, + NEW FOLDER, carry with hold R1, hold B on an empty folder), not joined, not tried on the handheld; batch 3, the review and its mend, is done (2026-11-15).

8. **The efficiency pass, then FM-6** (owner, 2026-11-15, after SPEED TEST on the handheld: it is
   13 to 20 times slower than the desk, not 5, and the busiest song stutters even with helpers):
   a third helper timed beside two, undo that reads back only what changed (200 ms on the
   handheld), the busiest song profiled, nothing worked out on silence; then QMOD replaced by
   FM-6, a 6-operator FM synth on msfa (Apache 2.0, the core inside Dexed; never Dexed's GPL
   code), with the 32 sounds of the first factory cartridge. **The engine part is built (branch
   `fm6`, 2026-11-15; `docs/plans/fm6-plan.md`)**: `third_party/msfa` (changes in
   `third_party/CHANGES.md`, built with `-fwrapv`), `engine/src/fm6.cpp`, the 32 written by
   `tools/fm6_rom.py`, `fm6_test`. **The app side is built too (2026-11-15): FM-6 is in QMOD's
   place and QMOD is gone** (its code, its test, `third_party/shapetaker`, ROLL). Not heard,
   not run on the handheld; the owner's questions are in the plan's section 9.
9. **The RG505 on the road map** (owner, 2026-11-15: "could be the perfect handheld"; it stays on
   the RG40XXV for now). Unisoc T618 (2 x A75 + 6 x A55 at 2.0 GHz, about 3 to 4 times the
   speed on a big core), 960x544 OLED, a headphone jack, 5000 mAh, **Android 12 only**: a port
   through SDL2's Android side (sound, files, the WiFi page, which core the sound runs on).
   Keep the engine free of Linux-only calls where it costs nothing.
10. **Our own firmware for the RG40XXV, Apotheca and nothing else** (owner, 2026-11-16; `docs/plans/firmware-plan.md`): a Buildroot image on the mainline kernel with ROCKNIX's H700 patches. **Planned only; batch 0 (ROCKNIX on a spare card, SPEED TEST there) is the first step.**
11. **A drum synth** (owner, 2026-11-17): a kit lane's sound made from knobs (KICK, SNARE, HATS...) instead of a recording. Plan and engine on branch `drumsynth` (`docs/plans/drumsynth-plan.md`).
12. **A wavetable synth, after the drum synth** (owner, 2026-11-17). Not planned yet.

Open: FM-6's banks ROM2A, 3A and 4A (in the same archive; not taken: the job asked for the B sides), which chord packs ship, more presets, own WiFi hotspot, an automation
step-editor, a picture of FM-6's ALGORITHM (its wiring; msfa's table says each). **Twin Analog is gone** (owner, 2026-11-11: rethink it; 2026-11-13: it
becomes a faithful Roland JX-3P clone, which is what it was meant to be). The
clone is JAX-EP, built in batches 0 to 8c of `docs/plans/jx3p-plan.md` on
branch `jx3p`; each batch's "what it learned" is there. The owner's calls
(plan section 8) are taken at the recommended answers, each one constant in
`jx3p.hpp` ("THE OWNER'S CALLS") or one named place. The engine stays at
48 kHz: 44.1 would save about 8%, which the owner judged not worth it.
