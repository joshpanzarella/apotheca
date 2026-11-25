# Where things are, and how to add an instrument or an effect

The app's and the engine's files, and the steps to add an instrument or an effect.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

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
| `name_keys.hpp/.cpp` | **the one set of keys a name is typed with**: a track's, a new folder's, a preset's (`NameKeys`) |

**Adding an instrument**: a `TrackKind` (project.hpp), a row in
`instruments.cpp`, a `SynthKind` and class in the engine, a `SynthSpec` if it
has presets and macros, and its DEVICE screen in `App::screenNow()` and `App::typingAName()`. Then the
places that list the synths by name: `userList`, `builtInPresets`,
`settingCount`, `specFor` (synth_spec.cpp), `macroIsChoice` (mods.hpp, for a
threshold macro), its stills in `main.cpp` and `kViews`, and the tests'
lists (`textsize`, `scope_test`, `save_test`, `modapp_test`). **`scripts/build-handheld.sh` builds
`engine/src/*.cpp` and `engine/src/*/*.cpp`**, so a source in a folder of its own under `engine/src`
is taken, one two folders down is not (until 0.711 it took only `fx/`, and MORPH's
`engine/src/morph/tables.cpp` did not link). MORPH was the last added (2026-11-17, wavetable-plan.md: a
picture of its tables, a roll); ACID's commit is the plainest pattern.

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
