# Presets: a track's whole sound

Built 2026-11-16 (branch `presets`). The owner: "you can make them with the MOD and
FX set up ... I am thinking that's actually a great idea for recall in the event that
you lose the track somehow." This page is for **whoever writes the built-in presets**
(JAX-EP, FM-6, PHYSIC, PHAZED): what a preset can hold, where to write one, the words,
and how to check it. The code: `app/presets.hpp` / `.cpp`; the tests: `tests/presets_test.cpp`.

## What a preset holds

| Part | In a preset? | Where it comes from |
| --- | --- | --- |
| The synth's settings and its eight macros | always | `base`, `set`, `settings`, `macros` |
| The three effect slots, each kind and settings | if it holds a track sound | `fx` lines |
| The ECHO and SPACE sends | if it holds a track sound | `send` lines |
| GLIDE FROM | if it holds a track sound | a `glidefrom` line |
| MOD 1 and MOD 2 | if it holds a track sound | `mod` / `modsteps` lines |
| The mixer's LEVEL and PAN | **never** | the song's balance, not the sound's |
| What the ECHO and SPACE buses hold | **never** | the song's (a preset only sends into them) |

A preset with **any** of the track-sound lines (or the word `whole`) holds the **whole**
track sound: picked, it puts every part in, and what it leaves out is empty (a slot
not named is emptied, a send not named is 0, a MOD not named is OFF, GLIDE FROM is TOP).
A preset with none of them is **plain**: picked, it changes the synth only, and leaves
the player's effects, sends and MODs as they are. The booklet's 32 (JAX-EP), the five
cartridges (FM-6) and PHYSIC's and PHAZED's own tables are plain unless given a sound here.

What picking does, in short (`Session::chooseSynthPreset`, `putPresetSound`):

- A whole preset: the synth, then each slot, send, GLIDE FROM and MOD **that differs**
  (as the song file writes it). An effect of another kind goes in through the usual
  10 ms fade, and that slot's automation goes, as for any effect put in. Nothing is
  written into automation, WRITE on or not. One step of undo takes it all back.
- A plain preset: the synth only. **But** if the track still holds exactly what a whole
  preset put in a moment ago (nothing turned since), what the track had before that whole
  preset came is put back: browsing past a whole preset onto a plain one does not leave
  its effects behind. Turn anything first and the track's sound is the player's, kept.
- A new track starts plain on the synth's first preset, whatever that preset holds.

## Where to write one

One text per synth, compiled into the program (no file is read at play time, so
browsing costs nothing on the handheld's card):

| Synth | File | Its settings, by id |
| --- | --- | --- |
| JAX-EP | `app/presets/jaxep.inc` | `engine/include/apotheca/jx3p.hpp`, `jx3p::Setting` (ids 0 to 31; 32 VELOCITY and 33 BRILLIANCE are the track's own, never a preset's) |
| FM-6 | `app/presets/fm6.inc` | `engine/include/apotheca/fm6.hpp`: operator n's setting s is `(6 - n) * 21 + s` (OP6 first), the voice's from 126 (`PitchR1`) to 145 (`OpsOn`, 63 all on) |
| PHYSIC | `app/presets/physic.inc` | `engine/include/apotheca/physic_rule.hpp`, `physic::Setting` |
| PHAZED | `app/presets/phazed.inc` | `engine/include/apotheca/phazed.hpp` |

Each file is one C++ raw string: keep the first line `R"presets(` and the last
`)presets"`, and write between them.

**APPEND ONLY.** A preset's place in the synth's list is kept in songs (`preset N`).
A new preset goes after the last one in the file; never put one in before another,
never move or take one out once it has been in a build the owner has. (Nothing is
released yet, so the two examples below may be replaced before the first build that
carries them.) The player's own presets (USER) always come after every built-in.

## The words

One fact a line. `#` starts a note to the end of the line. Blank lines are passed by.

### Starting a preset

| Line | Means |
| --- | --- |
| `new FOLDER / NAME` | a new built-in, after every one before it, in FOLDER. FOLDER up to 14 letters, NAME up to 20; plain letters, spaces, digits and `- . ' &` are fine. A folder is a run: its presets stand together, and a folder name used once may not come back after another folder. Not one of the engine's own folders (BANK A, ROM 1A, MALLETS...) and not USER |
| `preset N` | the engine's own built-in N (from 1, as the song's `preset N`): give it a track sound. Only the track-sound lines may follow |

### The synth's sound (a `new` preset only)

| Line | Means |
| --- | --- |
| `base N` | start from built-in N's settings (from 1; an engine one or an added one before this; an added one's macros too). Without it: built-in 1's, macros at the middle |
| `set ID VALUE ID VALUE ...` | settings by id, each a whole number in its range |
| `settings V0 V1 ...` | every setting from id 0, in order (fewer is fine: the rest stay) |
| `macros M1 ... M8` | the eight macros, 0 to 1, 0.5 the middle ("+0" on the device) |

### The track's sound (any preset)

These are the song file's own lines, read by the same code (`readSoundLine` in
`app/song_file.cpp`), so they mean exactly what they mean in a song.

| Line | Means |
| --- | --- |
| `fx SLOT WORD V1 V2 V3 V4` | slot 1 to 3 holds the effect whose song-file word is WORD, its settings 0 to 1 in the order below. Settings left off are its usual |
| `send BUS AMOUNT` | 1 ECHO, 2 SPACE; 0 to 1 |
| `glidefrom N` | 0 TOP, 1 PAIRED, 2 NEAREST, 3 RANDOM |
| `mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE [SPEED]` | MOD N (1 or 2). Numbers below |
| `modsteps N LENGTH SLEW CHANCE V1 ... V16` | MOD N's row (a STEPPER's): LENGTH 1 to 16, SLEW and CHANCE 0 to 1, each value -1 to 1 |
| `whole` | holds the track sound even with nothing in it: picked, it empties the slots, sends and MODs |

**A setting that is a choice** (TIME, TYPE, WAVE...) takes choice i of N (from 0) at
`(i + 0.5) / N`: TAPE's TIME has eight choices, so 1/8 DOT, the fifth, is
`(4 + 0.5) / 8 = 0.5625`. An amount reads as the device reads it; `presets_test --show`
prints each one (below).

The effects, by their song-file word (settings in order; a number in brackets: a
choice of that many):

| Word | Settings |
| --- | --- |
| `filter` | CUTOFF, RESONANCE, TYPE (5: low 12, low 24, band, DJ, high), MIX |
| `comb` | PITCH, FEEDBACK, TONE, MIX |
| `vowel` | VOWEL, SHIFT, RESONANCE, MIX |
| `eq` | LOW, MID, MID FREQ, HIGH |
| `drive` | DRIVE, TONE, MIX, OUTPUT |
| `saturate` | DRIVE, TYPE (4), TONE, MIX |
| `fold` | DRIVE, SYMMETRY, TONE, MIX |
| `fuzz` | FUZZ, TONE, GATE, MIX |
| `crush` | BITS, RATE, MIX |
| `ring` | FREQ, WAVE (choices), SPREAD, MIX |
| `wear` | WOW, DIRT, DROPS, TONE |
| `chorus` | RATE, DEPTH, TYPE (4), MIX |
| `flanger` | RATE, DEPTH, FEEDBACK, MIX |
| `ensemble` | RATE, DEPTH, SPREAD, MIX |
| `phaser` | RATE, DEPTH, FEEDBACK, MIX |
| `tremolo` | RATE (choices), DEPTH, SHAPE (choices), SPREAD |
| `delay` | TIME (6), FEEDBACK, TONE, MIX |
| `tape` | TIME (8), FEEDBACK, AGE, MIX |
| `ping` | TIME (8), FEEDBACK, TONE, MIX |
| `dub` | TIME (choices), FEEDBACK, TONE, MIX |
| `reverse` | TIME (choices), FEEDBACK, MIX |
| `stutter` | LENGTH (choices), REPEATS (choices), CHANCE, MIX |
| `reverb` | SIZE, DAMPING, MIX |
| `hall` | SIZE, DECAY, TONE, MIX |
| `spring` | DRIP, DECAY, TONE, MIX |
| `plate` | PRE, DECAY, TONE, MIX |
| `bloom` | BLOOM, DECAY, TONE, MIX |
| `shimmer` | SHIMMER, DECAY, TONE, MIX |
| `comp` | THRESHOLD, RATIO (6), SPEED, MIX |
| `duck` | AMOUNT, RELEASE, SOURCE (choices) |
| `limiter` | GAIN, CEILING, RELEASE |
| `snap` | ATTACK, SUSTAIN, MIX |

(The table is `engine/src/fx/NAME.cpp`'s `EffectInfo` rows; where they differ, they win.)
An effect in a slot runs on the track's sound: its MIX is the dry/wet. ECHO and SPACE
are the song's buses; a preset only sets how much it sends to them.

### A MOD's numbers (`engine/include/apotheca/mod_rule.hpp`)

| Field | Values |
| --- | --- |
| KIND | 1 LFO, 2 STEPPER |
| TARGET | 0 to 7 the synth's macros (1st to 8th: JAX-EP's CUTOFF is 0); `8 + (slot - 1) * 4 + setting` an effect's setting (slot 1's first setting is 8, slot 3's fourth is 19); 20 the ECHO send, 21 the SPACE send; 32 LEVEL, 33 PAN |
| DEPTH | 0 to 1, **0.5 is none**: 0.6 is +20%, 0.25 is -50%, 1 is +100% |
| RATE | 0 1/2 STEP, 1 1 STEP, 2 1/8T, 3 1/8, 4 1/4T, 5 1/4, 6 1/4., 7 1/2, 8 1 BAR, 9 2 BARS, 10 4 BARS, 11 8 BARS, 12 FREE (an LFO's only: SPEED in Hz, the ninth number) |
| SHAPE | 0 SINE, 1 TRIANGLE, 2 RAMP UP, 3 RAMP DOWN, 4 SQUARE, 5 RANDOM, 6 DRIFT (an LFO's) |
| RESTART | 0 |
| PHASE | 0 to 3: a quarter turn each |

A MOD may be aimed only where the device would let it (`modTargetFits`): an effect
setting that is there and is an amount, not a choice; a macro that is not a choice
(PHAZED's CHORUS and OCTAVE, PHYSIC's OCTAVE are choices); a send; LEVEL; PAN. A MOD
aimed anywhere else reads OFF, and the test says so. A MOD may stand before the `fx`
line it is aimed at.

## Two worked examples

Both are in the data now, and `presets_test` plays them.

**A JAX-EP pad with CHORUS and HALL, and an LFO on its CUTOFF macro** (`jaxep.inc`):

```
new PADS / Warm Pad
base 1                          # String I (BANK A 1), then:
set 27 70  28 60  29 80  30 75  # ATTACK 7.0, DECAY 6.0, SUSTAIN 8.0, RELEASE 7.5
set 31 0                        # the synth's own CHORUS off: the track's CHORUS does it
fx 1 chorus 0.3 0.6 0 0.7       # RATE, DEPTH, TYPE (the first of 4), MIX
fx 2 hall 0.7 0.6 0.45 0.35     # SIZE, DECAY, TONE, MIX
send 1 0.15                     # a little into ECHO
mod 1 1 0 0.6 9 0 0 0           # MOD 1: an LFO on macro 1 (CUTOFF), +20%, 2 BARS, SINE
```

**A PHYSIC bell through SHIMMER, a STEPPER moving where it is struck** (`physic.inc`):

```
new SHIMMER / Glass Steps
base 12                         # Church Bell (BELLS), then:
set 3 35  4 70                  # POSITION 35, DECAY 70
fx 1 shimmer 0.6 0.7 0.6 0.4    # SHIMMER, DECAY, TONE, MIX
send 1 0.2                      # into ECHO
mod 1 2 1 0.7 5 0 0 0           # MOD 1: a STEPPER on macro 2 (POSITION), +40%, 1/4
modsteps 1 8 0.2 0 0.5 -0.3 0.8 0 -0.6 0.2 0.9 -0.1 0 0 0 0 0 0 0 0
```

Giving one of the engine's own a sound (here JAX-EP's A13 Chime, a bit of SPACE):

```
preset 13
send 2 0.3
```

## How to check what you wrote

1. `cmake --build build -j4 --target presets_test && ./build/presets_test`. **It fails on
   any line it cannot read**, naming the synth and the line ("line 12: a word a preset does
   not have"), and plays every added preset (a number every sample, a note heard).
2. `./build/presets_test --show` prints every preset with a sound as the device reads it:
   each effect's settings ("SIZE 2.0 S"), the sends, each MOD with its target's name.
3. `./build/presets_test --print` lists the added ones and their places.
4. A still of the device on one: `--view jaxep` and pick it by hand, or the WiFi page's
   list (it shows the folder and the name).

Nobody has listened to any of it on the handheld: the owner cannot hear, so what a preset
sounds like is only what its numbers say. Keep each loud enough and clean (the tests hold
"a note heard"; a designer's own render with `render_demo` or `bench` is the way to measure).

## What is in a player's own preset file

`presets/NAME.jaxep` (and `.fm6`, `.physic`, `.phazed`), written by SAVE AS PRESET and
by the WiFi page:

```
apotheca-preset 1
synth jaxep
macros 0.5 0.5 0.5 0.8 0.5 0.5 0.5 0.5
settings 0 0 0 0 ...
whole
fx 1 chorus 0.4 0.75 0 0.8
fx 3 hall 0.25 0.45 0.5 0.5
send 1 0.2
send 2 0.6
glidefrom 1
mod 1 1 19 0.625 8 4 0 0
mod 2 2 2 0.25 8 0 0 0
modsteps 2 8 0.25 0 -0.5 0 0.5 ...
```

It is read as carefully as a song (`readPresetFile`): every number held as a double
first, then to its range; a word nobody knows passed by; a MOD aimed at nothing OFF;
Windows line ends taken. A file from before 2026-11-16 has no `whole` and is plain.
