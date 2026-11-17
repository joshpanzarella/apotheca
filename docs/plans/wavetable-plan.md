# MORPH plan (the wavetable synth)

Written 2026-11-17 from the code at `main` 56b674f (after CLOUD), branch `wavetable`.
**The plan, and batch 0 (the rule header and its test).** No sound is made yet. The
costs marked **MEASURED** come from a throwaway benchmark on the desk (section 4);
everything marked **GUESS** is a guess. **The owner cannot hear**: every claim about
the sound is a test or a still.

The owner asked (2026-11-17): "let's do the wavetable after that" (after the drum synth).

**What a wavetable synth is, in one line**: an oscillator that plays one short wave
(a FRAME, one cycle long) over and over at the key's pitch, and a TABLE is a row of
such frames, each a little different. **POSITION picks where in the row the wave is
read**, so turning it (by hand, by a MOD, or by an envelope on each note) makes the
sound change shape as it plays: a wave that turns into another.

## The recommendation

**One new synth, MORPH: 6 keys, two table oscillators (A and B) and a sine SUB a key,
the effects' state-variable filter a key, two envelopes. 16 built-in tables made in
code, 32 frames each.** POSITION is macro 0, so a MOD on it sweeps the table. The read
is **linear between samples** on tables kept at several sizes (one per half octave of
pitch, so no key reads harmonics past what it can hold), which measured about **2 ns
an oscillator-sample on the desk**, under half of a cubic read.

| Question | Answer |
| --- | --- |
| What it is | **Two wavetable oscillators that morph**: TABLE and POSITION each, MIX between them, B tuned against A, a SUB, UNISON with DETUNE and WIDTH, a filter, an AMP and a MOD ENV (to POSITION and CUTOFF) |
| Name on the device | **MORPH** (short name `MRPH`, song word `morph`, presets `NAME.morph`). Alternative: **TABLE** |
| The `says` line | **"wavetables, swept from wave to wave"** (35 letters) |
| Keys | **6**, newest wins, the oldest let go stolen first (a 5 ms fade) |
| Oscillators | **A and B** (B can be OFF) and a **SUB** sine an octave under A. **UNISON 1 to 4** streams an oscillator |
| What it costs | **A pool of 24 streams** (a stream is one oscillator's read): 6 keys at UNISON 1 or 2 with A and B, fewer keys past that (section 3). The cost never passes the pool's |
| Tables | **16 built in**, made in code (recipes: section 2), **32 frames each**, shared and never changed (as `DrumKit::builtIn`). A player's own table from a recording: **later** (batch 6, the owner's call) |
| Band-limiting | Each frame kept at **18 sizes, one per half octave**: the size a key reads holds only harmonics that fold back above 20 kHz. Lengths 2048 down to 128, **8 times more points than harmonics** (measured: the clean one) |
| Settings | **29** (room for 40), 8 macros, MAIN + 5 pages |
| Cost target | **6 keys, A + B + SUB, one filter: ≤ 0.3% of a desk core** (measured prototype 0.26 to 0.29%; about 4% of a handheld core). **The pool full: ≤ 0.5%** (prototype 0.48 to 0.54%; about 8% of a handheld core, as JAX-EP's 6 notes). Ceiling 0.7% |
| Memory | **1.6 MB a table** (50 KB a frame, measured layout); the 16 built-ins **26 MB**, made when the first MORPH track is |
| Engine changes | **None outside the new synth** for version 1: tables are made in `prepare()` (app thread); the picture's readout is CLOUD's `grainPlaces` path |
| First thing to build | **Batch 0**: `engine/include/apotheca/morph_rule.hpp` (the rule as numbers, no sound) and its test. **Built** (section 9) |

## 1. What it is

### The voice, in a picture

```
key ──> pitch (OCTAVE, TUNE, GLIDE)
          │
          ├─> OSC A: TABLE A, POSITION A ──┐   UNISON streams, DETUNE spread, WIDTH apart
          ├─> OSC B: TABLE B, POSITION B ──┼─> MIX (A to B, straight) ──┐
          │      (+ B SEMI, B FINE)        │                            ├─> FILTER ──> x AMP ENV ──> out
          └─> SUB: a sine an octave under A ─────────────────────────────┘    (CUTOFF + KEY TRACK + MOD ENV)
                                         MOD ENV ──> POSITION (both), CUTOFF
```

### The settings

Ids are appended, never reordered (songs and presets store them by number). Every
amount is a whole number on its dial; the reading is in its unit.

| Id | Setting | Range, reading | Usual | What it does |
| --- | --- | --- | --- | --- |
| 0 | TABLE A | a choice: the 16 tables by name | ANALOG | A's row of frames |
| 1 | POSITION A | 0..100, read as the FRAME (1 to 32, with its tenth) | 0 | where in A's table |
| 2 | TABLE B | a choice: OFF, then the 16 | OFF | B's row of frames; OFF costs nothing |
| 3 | POSITION B | 0..100, read as the FRAME | 0 | |
| 4 | MIX | 0..100: A only at 0, B only at 100, read A 70 / B 30 | 50 | A and B crossfaded **straight** (they are one pitch: the house rule for coherent sources) |
| 5 | B SEMI | -24..+24 ST | 0 | B against A |
| 6 | B FINE | -50..+50 CENTS | 0 | B against A: a slow beat |
| 7 | SUB | 0..100, read in DB (0 OFF) | 0 | a sine an octave under A |
| 8 | OCTAVE | -2..+2 | 0 | |
| 9 | TUNE | -100..+100 CENTS | 0 | |
| 10 | UNISON | a choice: 1, 2, 3, 4 | 1 | streams an oscillator, each its own pitch and side |
| 11 | DETUNE | 0..100, read +- CENTS (squared: 0 to 50) | 20 | how far the streams are spread in pitch |
| 12 | WIDTH | 0..100% | 50 | how far the streams are spread apart in the sides |
| 13 | FILTER | a choice: OFF, LOW 12, LOW 24, BAND, HIGH | LOW 12 | |
| 14 | CUTOFF | 0..100, read HZ (20 HZ to 20 KHZ, log) | 100 | |
| 15 | RESONANCE | 0..100% (Q 0.5 to 20) | 0 | |
| 16 | KEY TRACK | 0..100% | 50 | CUTOFF follows the key, 100% an octave a key-octave |
| 17..20 | ATTACK, DECAY, SUSTAIN, RELEASE | the shared envelope's scale (`envelope.hpp`) | 2 MS, -, 100%, 200 MS | the AMP envelope |
| 21..24 | ENV ATTACK, ENV DECAY, ENV SUSTAIN, ENV RELEASE | the same scale | 2 MS, 500 MS, 0%, 200 MS | the MOD ENV |
| 25 | ENV > POSITION | -100..+100, read as FRAMES (+- the whole table) | 0 | each note sweeps its table |
| 26 | ENV > CUTOFF | -100..+100, read +- OCT (6 at most) | 0 | |
| 27 | VELOCITY | 0..100, 0 OFF | 50 | how much a key's velocity sets its level |
| 28 | LEVEL | -24..+6 DB | 0 | |

**Not in version 1**, each an owner's call in section 7:

- **WARP** (bending the read: sync, bend, mirror) makes harmonics the frame's size
  cannot hold, so it aliases unless the voice oversamples (double the cost). PHAZED
  already bends a wave's phase.
- **Frames in STEPS** (no crossfade between frames, the old hardware's sound). A frame
  jump clicks unless it fades; later, as a choice appended.
- **NOISE** in the voice: a table's frames are one cycle, so noise in one is a buzz.
  The CRUSH and DRIVE families and CLOUD cover grit.
- **A player's own table** from a recording: batch 6 (section 2).

## 2. Tables

### How a table is kept

A table is **32 frames** (built-ins; up to 64 for a player's own). Each frame is kept as
its **harmonics** while it is made, then written out at **18 sizes**, one per half
octave of pitch. Size `m` holds harmonics 1 to `H(m) = floor(512 / 2^(m/2))` (512, 362,
256, 181, 128, 90, ... 1) in `L(m)` points, the power of two at or over `8 x H(m)`, held
to 128 .. 2048. Each is stored with one guard point before and two after, so a read
never wraps by hand.

**Which size a key reads**: the first whose top harmonic, at the highest pitch the key's
streams reach in the tick, is at or under `rate - 20 kHz` (28 kHz at 48 kHz). What is
over half the rate folds back, and lands **over 20 kHz**, where nobody hears it. With a
size every half octave, the next size down drops only harmonics over 19.8 kHz, so **a
size changing under a glide or a vibrato is not heard, with no crossfade between sizes**
(batch 1 holds that with a sweep from key 24 to 108).

Why 8 times the points (measured, section 4): the error of a straight-line read falls
as the points per harmonic rise. At 2048 points for every size the read is cleaner
still, but 2.5 times the memory and 8 KB a frame read at every pitch, which crowds the
handheld's small cache (its L2 is 256 KB and the busiest song already misses it).

### POSITION and the morph

POSITION 0..100 is a place along the frames, `0` frame 1 exactly, `100` the last frame
exactly. Between two frames the voice reads both at the same phase and **crossfades
them straight**, sample by sample. **What moves POSITION** (all added, held to the
table):

| Source | How |
| --- | --- |
| The setting (POSITION A / B) | its own |
| **Macro 0, POSITION** | bends both A and B together (the house's `macroBent`) |
| **A MOD on macro 0** (LFO, STEPPER, FREE) | through the macro. The engine gives a synth's macros every 512 samples (`kModMacroTicks` x `kModTick`); **MORPH slides each new macro value in over exactly those 512 samples**, counted from when it was set, so a MOD on POSITION is a line, never a staircase (10.7 ms late, which nobody hears) |
| **The MOD ENV** | ENV > POSITION x the envelope, a key's own: each note sweeps its table from its start |

Worked out **on a tick of 16 samples counted from the key's strike** (PHYSIC's and
ACID's tick), slid sample by sample between ticks, so the sound does not hang on how it
is cut into blocks.

### Phases, so frames crossfade cleanly

Two frames whose harmonics are out of phase cancel where they are crossfaded (a hollow
dip half way). **Built-in tables are made in phase by their recipes** (each a waveform
whose shape grows smoothly), and batch 1 holds it: half way between any two frames the
level never dips more than 1 dB under the two ends' mean. A player's own table (batch 6)
takes every harmonic's phase from frame 1 for all frames (its shapes change, its sound
does not).

### The 16 built-in tables

Each is a recipe: a function of the frame (0..1) and the phase, worked out at 4096
points, its first 512 harmonics taken by an FFT. Names are plain and short (they fit a
box at XL). **Recipes and names are SELF**, chosen to differ, not heard.

| # | Name | Frames go from ... to ... |
| --- | --- | --- |
| 0 | ANALOG | sine, triangle, saw, square, a narrow pulse |
| 1 | PULSE | a square to a 3% pulse |
| 2 | SYNC | a saw restarted by a master: 1 to 8 times its own cycle |
| 3 | VOWEL | a buzz through two formants walking A, E, I, O, U |
| 4 | ORGAN | drawbars pulled out one by one, 16' to 1' |
| 5 | HARMONIC | one sine walking up the harmonics, 1 to 32 |
| 6 | FOLD | a sine folded harder and harder |
| 7 | BEND | a sine's phase bent, none to sharp |
| 8 | FM | two-operator FM, ratio 1:1, index 0 to 8 |
| 9 | RATIO | FM at index 2, the ratio walking 1 to 7 |
| 10 | DRIVE | a sine driven into a soft clip, gain 1 to 40 |
| 11 | STEPS | a sine held in 64 steps down to 2 |
| 12 | GRIT | rolled harmonics (a hash of the frame), a falling slope, each frame a new roll morphing to the next |
| 13 | COMB | a saw through a comb whose notches walk |
| 14 | BELL | high harmonics weighted unevenly, thinning out |
| 15 | GLASS | odd harmonics only, the high ones rising in |

**Made once, shared, never changed** (as `DrumKit::builtIn` and CLOUD's TONE), by
`Morph::prepare()` on the app thread (`Engine::addSynthTrack` already calls it there), so
the audio thread only ever switches a pointer. **Time to make them** (section 4): a
32-frame table with a plain complex FFT in double took **13 ms on the desk, MEASURED**;
a real FFT in float should take a quarter (GUESS). All 16: **50 to 200 ms on the desk,
0.7 to 3 s on the handheld (GUESS)**, once per run of the program, when the first MORPH
track is made. **If batch 1 finds the handheld's figure over 0.1 s** (the likely case),
the app makes them on the helper thread at start (the FIT job's), the first MORPH track
waiting only if it comes first.

### A player's own table (the recommendation: later, batch 6)

**Yes, but after version 1.** CLOUD showed recordings are wanted. The simple, robust way:

- **The track's recording, through `holdsRecording`** (as CLOUD), its START..END cut into
  **64 equal frames of one window each**, each window taken as one cycle: its seam
  faded, its first 512 harmonics by FFT, every frame's phases set to frame 1's. **No
  pitch is guessed**: a voice becomes a buzzing formant, a drum loop a gritty table.
  A FIND CYCLE choice (each frame one found period of a pitched recording, cleaner)
  is a later step, because a found period is wrong often enough to need ears.
- **No new kind of file**: the table is made again from the recording when the song
  opens (on the helper thread), the recording's name in the song as CLOUD's.
- It needs **one engine change** (`Instrument::setTable(osc, table)`, the old one through
  the trash, as `setSample`), which is why it waits.

## 3. The voice

| Part | Design | Why |
| --- | --- | --- |
| Keys | **6 sounding, 12 slots** (6 more only fading out over 5 ms: a stolen key, every key at `allNotesOff`), CLOUD's pattern | no click on a steal or on Play |
| The stream pool | **24 streams** for the track. A key takes `UNISON x (B on ? 2 : 1)` at its strike; one that finds too few steals the oldest let-go key, then the oldest. So at UNISON 1 or 2 with A and B, **6 keys**; UNISON 3, 4 keys; UNISON 4, 3 keys (6 with B OFF) | **the cost has a ceiling whatever the settings**; the page says KEYS 3 beside UNISON |
| UNISON, TABLE B on or off | **read at the strike**; a key sounding keeps its streams. The app strikes a held X audition again when they change | a key's streams never appear mid-note |
| TABLE A / B (which table) | read at the strike, too (a switch mid-note would step the wave) | |
| Phase | **32-bit fixed point** (`uint32`): one integer add a sample, the index its top bits, the fraction its bottom ones. A and B start at 0 at a strike at UNISON 1 (the same attack every time); **at UNISON 2 to 4 every stream's start is a roll** (a hash of the strike's `noteSeed`, the oscillator and the stream; never a generator walked) | exact in any blocks; streams starting together flange at the attack |
| The read | **Linear between points**, the two frames each read at the phase and crossfaded straight (5 sums, 4 loads) | measured 2 ns against Hermite's 5 (section 4); 8 points per harmonic keep it clean |
| Pitch | `440 x 2^((key - 69 + 12 OCTAVE + TUNE/100 + slide + stream's detune) / 12)`; the increment worked out on the tick, held for its 16 samples | the glide's steps are 0.33 ms apart |
| DETUNE | stream `i` of `U`: `d x (2i / (U - 1) - 1)` cents, `d` = 50 x (dial/100)^2 | even spread; fine near 0 |
| WIDTH | stream `i`'s side `w x (2i / (U - 1) - 1)`, equal power (detuned streams are unlike each other) | |
| SUB | a sine an octave under A's base pitch, from `sinPoly` (no table, no `sin`) | a few sums |
| Filter | **`fx/svf.hpp`'s `Svf`** (the trapezoidal state-variable filter the newer effects share), per key: LOW 12, LOW 24 (two in series, the resonance on the second), BAND, HIGH, OFF. **Mono, or one a side when UNISON > 1 and WIDTH > 0**. Its numbers on the tick only when the cutoff moved, from polynomials (no `tan`: the JX-3P's lesson) | already in the engine, cheap (a few sums), keeps its shape when the cutoff moves. ACID's ladder and the JX-3P's VCF are mono, one voice's worth and 10 times dearer |
| CUTOFF | `20 Hz x 1000^(dial/100)` + KEY TRACK x (key - 60) / 12 octaves + ENV > CUTOFF x 6 octaves x MOD ENV, held to 20 Hz .. 0.45 of the rate | |
| Envelopes | `envelope.hpp`'s `Envelope` and `Shape`, twice: AMP (sample by sample, after the filter) and MOD ENV (sample by sample; read at the tick's end) | the house envelope: a setting of 30 means the same on every synth |
| Macros | **`setMacro` stores and returns**; each slid in over 512 samples counted from its call (section 2), read on the key's tick | a MOD on POSITION is a line |
| Settings | stored; worked out at the top of the next render, or at a strike | ACID's rule |
| GLIDE | `noteOnFrom`: the key's pitch slides from `from` (the house `Slide`, taken a tick at a time) | GLIDE FROM serves it unchanged |
| Steal | the oldest let-go key, then the oldest; its streams fade out over 5 ms beside the new key's (the pool's 24 may be passed for those 5 ms) | |
| Sleep | a key is free when its AMP envelope is done (`kSilent`, 80 dB down); **with no key the synth writes exact zeros and does no work**; a strike starts a fresh key (filter state 0, phases as the rule says) | a woken synth is a fresh one to the bit |
| Tiny numbers | the filter's states snapped on the tick (`Svf::settle`, under 1e-20); nothing else is fed back | `TooSmall` |
| Rolls | only the unison phases (`streamPhase`), by the strike's seed; a key by hand takes `liveSeed` of a count `allNotesOff` puts back (CLOUD's) | Play from the top is the same sound |
| Blocks | everything per sample or on the tick counted from the strike; macros slid by samples counted from the call; `#pragma clang fp contract(off)` (the handheld's bits are the desk's) | |
| NaN | a macro, a velocity or a setting that is not a number is held | |

### The picture's readout

**CLOUD's path, unchanged**: `Instrument::grainPlaces(places, most)` (its comment widened),
`Track::grainAt` / `grainCount`, `Engine::grainPlaces`. For MORPH the places are **each
sounding key's POSITION A as a share of its table, newest first, then B's** (-1 where B is
OFF). The app reads them as a readout for the redraw.

## 4. What it costs, and what was measured

### Measured on the desk (2026-11-17)

A throwaway benchmark (not in the tree; clang -O2, as the handheld is built, x86-64;
a busy desk, load 6 to 10; each figure the best of 15 runs of 2 s). The frames morph saw
to square to a comb: rich up to the top harmonic, the worst case for a read.
**Instructions** are callgrind's count for one second of sound; the speed plan's rule
(section 0 there: **1 G desk instructions is about one handheld core**) turns them into
a handheld share. **The handheld figures are that rule, not a run.**

| What | Desk time | Instructions a second | Handheld (by the rule) |
| --- | --- | --- | --- |
| One oscillator, two frames crossfaded, **linear** | 1.9 to 2.3 ns a sample | 34 a sample | 0.16% |
| ...the same, **4-point Hermite** | 4.5 to 5.4 ns a sample | 72 a sample | 0.35% |
| **6 keys: A + B (linear), SUB, one 12 dB SVF, an envelope** | **0.26 to 0.29%** of a core | 36 M | **about 3.6%** |
| ...the same with Hermite reads | 0.39 to 0.49% | 61 M | about 6% |
| 6 keys x 2 streams, an SVF a side, two envelopes | 0.37% | 50 M | about 5% |
| **6 keys x 4 streams (the pool full, 24), the same** | **0.48 to 0.54%** | 81 M | **about 8%** |
| Making one 32-frame table (18 sizes, plain complex FFT in double) | 13 ms | | (170 to 260 ms at x13 to x20) |

For comparison: JAX-EP's 6 notes are 0.55% of a desk core and about 9.5% of the
handheld's; CLOUD's full pool 0.54 to 0.58% and 7 to 10%.

**How clean the read is** (MEASURED: the error under the size's own harmonics, with the
straight-line read's gentle top roll-off taken out, so what is left is what folds back;
a saw-like frame, the worst case):

| Key | 2048 points at every size (2.5 MB a 32-frame table) | **8 x the harmonics, 128 to 2048 (1.6 MB): the pick** | 4 x the harmonics, 64 to 2048 (1.0 MB) |
| --- | --- | --- | --- |
| 24 | -54 dB | **-54 dB** | -54 dB |
| 36 | -64 | **-59** | -59 |
| 48 | -73 | **-69** | -56 |
| 60 | -82 | **-65** | -53 |
| 72 | -91 | **-62** | -50 |
| 84 | -99 | **-59** | -47 |
| 96 | -108 | **-56** | -44 |

Hermite on the pick is 15 to 25 dB cleaner again (-61 to -85 dB) at 2.2 times the read's
cost. **The pick: linear on the 8x sizes.** If batch 1's aliasing test (`asideOf`) finds
it audible at the top keys, the first move is 16x for the small sizes (a few hundred KB a
table), not Hermite.

### Guessed

| Case | Desk | Handheld |
| --- | --- | --- |
| The real voice over the prototype (the frame pair taken each sample, POSITION and the filter's numbers on the tick, LOW 24's second filter, the macro slides) | +10 to 20% | |
| **6 keys, A + B + SUB, LOW 12, two envelopes** | **0.30 to 0.35%** (target 0.3) | 4 to 6% |
| **The pool full** (24 streams, stereo LOW 24) | **0.55 to 0.65%** (target 0.5, ceiling 0.7) | 8 to 11% |
| Asleep | 0.00% | 0 |
| All 16 built-in tables made, a real float FFT | 50 to 200 ms | 0.7 to 3 s, once |

**Targets: 6 keys plain ≤ 0.3%, the pool full ≤ 0.5%, ceiling 0.7%.** Batch 1 measures
the pool full first. **If over, cut in this order**: (1) the frame pair taken on the tick,
the tick split where POSITION crosses a frame (not a pair each sample); (2) LOW 24 as one
filter with a steeper slope's look (the second filter only at RESONANCE 0); (3) the pool
at 20; (4) the tables as 16-bit numbers (half the memory and the cache traffic; a convert
a read). `bench` gets three rows: "MORPH, 6 keys", "MORPH, the pool full", "MORPH asleep",
and `morph_test --measure` the table-making time.

### Memory

| What | Size |
| --- | --- |
| A frame at its 18 sizes (the pick) | 50 KB, MEASURED |
| A built-in table, 32 frames | 1.6 MB |
| **The 16 built-ins** | **26 MB**, once, shared by every MORPH track |
| A key and its streams | under 1 KB; the synth about 10 KB |
| A player's own table (batch 6, 64 frames) | 3.2 MB a track |

## 5. Macros, pages, MODs

| Lane | Macro | MOD target |
| --- | --- | --- |
| 0 | **POSITION** (A and B together) | yes: **the wavetable move** |
| 1 | MIX | yes |
| 2 | CUTOFF | yes |
| 3 | RESONANCE | yes |
| 4 | ENV > POSITION | yes |
| 5 | ENV > CUTOFF | yes |
| 6 | DETUNE | yes |
| 7 | RELEASE | yes |

**No macro is a choice**, so `macroIsChoice` has no MORPH row and every macro takes a
MOD. 0.5 is the patch as written (the house rule).

| Page | Holds |
| --- | --- |
| MAIN | PRESET, **the picture** (below), the eight macros under it (CLOUD's room: `kPicMacroH`) |
| OSC | TABLE A, POSITION A, TABLE B, POSITION B, MIX, B SEMI, B FINE, SUB |
| VOICE | OCTAVE, TUNE, UNISON (with KEYS n beside it), DETUNE, WIDTH, VELOCITY, LEVEL |
| FILTER | FILTER, CUTOFF, RESONANCE, KEY TRACK |
| AMP | ATTACK, DECAY, SUSTAIN, RELEASE |
| ENV | ENV ATTACK, ENV DECAY, ENV SUSTAIN, ENV RELEASE, ENV > POSITION, ENV > CUTOFF |

Two rows of four at most, so nothing is held at LARGE (`textsize` holds them at XL).

### The picture on MAIN

**The wave POSITION reads, drawn** (the player cannot hear: this is how the morph is
seen). In CLOUD's picture room (`SynthScreen::kPictureY`, 80 high), through a widened
`SynthSpec` hook (`recording` becomes a picture kind: NONE, RECORDING, TABLE):

- **A's frame at POSITION, a teal line** across the box, read from the table's
  largest size at the box's columns (about 600 reads, on the app thread).
- **B's frame in purple** behind it when B is on, at MIX's share of the height.
- **A strip under it**: a dim mark per frame, POSITION a brass line.
- **While a key sounds**, the wave drawn is the frame at the **sounding** POSITION (the
  readout: a MOD or the MOD ENV moving it), and its place a teal tick on the strip. So a
  MOD on POSITION is seen as the wave changing shape.
- The table's name in the fixed lettering over it, as CLOUD's recording name.

`idle_test` gets a part (a key held, an LFO on POSITION at LEVEL -24 so LIVE OUTPUT
hardly moves: only the picture stirs the redraw).

### Y on MAIN: ROLL

**Yes** (PHYSIC's and CLOUD's `SynthSpec::roll`): a hash of the seed and each setting's
place, never a generator walked. It rolls TABLE A, TABLE B (OFF one time in four), both
POSITIONs, MIX, B SEMI (0, +-12, +7, +19 mostly), B FINE (small), SUB (0 half the time),
UNISON (1 mostly), DETUNE, WIDTH, FILTER (LOW 12 or LOW 24 mostly), CUTOFF, RESONANCE
(low mostly), the MOD ENV and its two amounts. **It keeps** OCTAVE, TUNE, the AMP
envelope (a pad stays a pad), KEY TRACK, VELOCITY and LEVEL. One step of undo.

## 6. Presets, the designer page

### 32 presets in five folders, as data

The engine's own 8 in BASIC (`kPresets`), the other 24 as data in
`app/presets/morph.inc` (`docs/plans/presets-plan.md`). Names are made up; no maker's,
product's or artist's name. Preset 0 is what a new track gets.

| Folder | Count | What | Whole sound? |
| --- | --- | --- | --- |
| BASIC | 8 | One table each played plainly, POSITION in the middle, the MOD ENV still. **Preset 0: Analog Sweep** (ANALOG, POSITION 30, a little ENV > POSITION) | plain |
| PADS | 6 | A and B, UNISON 2, slow AMP, a slow MOD ENV on POSITION; two with an LFO on POSITION | 3 whole: HALL, BLOOM or SHIMMER on SPACE |
| LEADS | 6 | UNISON 2 or 3, B a fifth or an octave up, LOW 24 with a MOD ENV on CUTOFF | 2 with an ECHO send |
| BASS | 6 | OCTAVE -1, SUB up, short ENV > POSITION (a growl on each note), LOW 24 | 2 with DRIVE or SATURATE |
| MOVING | 6 | A MOD on POSITION in each: an LFO in time, a FREE LFO, a STEPPER (a table played as a sequence of waves), a STEPPER with CHANCE; one with MOD 2 on CUTOFF | all whole |

`presets_test --heard-morph` plays each through its whole sound (a phrase of chords and a
line, round twice, Stop, the tail), as ACID's and CLOUD's: the loudest tenth between -30
and -8 dB, the master's peak under 0.9, a tail that ends inside 30 s, every MOD moving and
**heard** (the phrase with it off differs by 1% or more).

### The WiFi designer page

The generic synth page serves every setting. **Batch 4 adds the picture**: the spec
carries, for each of A and B, the table's frames as outlines (32 frames x 64 points,
from a small size: about 4 000 numbers, made once a table); the page draws **the frame at
POSITION** (crossfaded in the page's script, as the voice does), the table's other frames
faint behind it as a stack, and a click or a drag sets POSITION. ROLL as `roll N`. The
script tried in a browser (Playwright), as the house rule says.

## 7. The owner's calls

Each with the pick. A later "no" changes one constant or one row.

| # | Call | Pick |
| --- | --- | --- |
| 1 | Name | **MORPH** (alternative TABLE) |
| 2 | Keys | **6** |
| 3 | Unison and its cost | **A pool of 24 streams**: UNISON 1 or 2 with A and B keep 6 keys; UNISON 3 has 4, UNISON 4 has 3. The page says KEYS n. The cost never passes the pool's |
| 4 | Oscillators | **A and B (B can be OFF) and a SUB sine**. No NOISE |
| 5 | The read | **Straight lines between points** on tables 8 times the harmonics (MEASURED clean to -54 dB at key 24, -56 to -69 dB above), not the cubic read (2.2 times the cost) |
| 6 | Built-in tables | **16, 32 frames each**, made in code, all made with the first MORPH track (26 MB) |
| 7 | A player's own table from a recording | **Yes, later (batch 6)**: the recording cut into 64 frames, no pitch guessed, made again when the song opens, no new file |
| 8 | The filter | **The effects' state-variable filter a key**: LOW 12, LOW 24, BAND, HIGH, OFF. Not ACID's or JAX-EP's ladders (mono, 10 times the cost) |
| 9 | Envelopes | **AMP and one MOD ENV** (to POSITION and CUTOFF) |
| 10 | Macro 0 | **POSITION moves A and B together** |
| 11 | A MOD on POSITION | **Slid in over 512 samples** (a line, 10.7 ms late), never a step |
| 12 | Phase at a strike | **0 at UNISON 1** (the same attack every time); **rolled at UNISON 2 to 4** |
| 13 | TABLE, UNISON, B on or off while a key sounds | **Read at the strike**; the X audition is struck again |
| 14 | WARP, frames in STEPS | **Not in version 1** |
| 15 | Y on MAIN | **ROLL a new sound** (OCTAVE, TUNE, the AMP envelope and the levels stay) |
| 16 | The picture on MAIN | **The frame at POSITION, B's behind it, the sounding place while a key plays** |
| 17 | Presets | **32 in five folders** (BASIC, PADS, LEADS, BASS, MOVING); preset 0 Analog Sweep |

## 8. Batches

One job each. Each ends with `ctest` green under `set -o pipefail` and its mutants
killed. **Every figure in batches 1 to 4 is SELF until someone hears it.**

| Batch | What | Tests (`tests/measure.hpp` tools) | Risk |
| --- | --- | --- | --- |
| **0** | **The rule, no sound.** `engine/include/apotheca/morph_rule.hpp`: the settings (29, room 40), the macros, the readings (FRAME, CENTS, ST, HZ, OCT, DB, %), the sizes (`sizeHarmonics`, `sizeLength`, `sizeFor`), POSITION to a frame pair (`framePlace`), the phase step (`phaseStep`), the macro slide (`macroSlid`), the stream pool (`streamsPerKey`, `keysFor`), the unison spread (`streamCents`, `streamSide`), the rolls (`streamPhase`), the cutoff (`cutoffHz`), the filter's Q | `morph_test`: case by case, both ends of each reading, every size against the 28 kHz rule, mutants. **Built** | little |
| **1** | **The tables and the voice.** `engine/src/morph/tables.hpp/.cpp` (the FFT, the 16 recipes, the sizes), `morph.hpp/.cpp`, `SynthKind::Morph` appended, `addSynthTrack`; keys, the pool, the reads, the filter, both envelopes, steal, sleep; the readout through `grainPlaces`; `bench`'s three rows; a new engine folder (`morph/`) in `words_test.sh`'s `kBeside` | **pitch within 0.5 cent** (keys 24 to 96, `partNear`); **each frame's harmonics as made** (read back at a held POSITION, `harmonicsIn`); **the morph straight** (half way, each harmonic the mean of its two frames); **aliasing** (`asideOf`, every table at keys 36 to 108, under the bound section 4 sets); **no step at a size change** (a glide 24 to 108: nothing new under 19.8 kHz); **no dip half way** (1 dB); a MOD on POSITION a line (no step over 1/512 of the move a sample); blocks 1 to 333 to the bit, through the engine with 0 to 3 helpers; Play from the top; no allocation (counted `new`); `TooSmall` over a 60 s tail; NaN; silence; the pool's key counts and steals; no click at a steal; the table-making time | **The pool's cost** (section 4's cuts). **The tables' making time on the handheld** |
| **2** | **The app.** `TrackKind::Morph` appended, its row last in `kInstruments`, `MorphPatch` (room 40), the spec `mo` (pages, readings, `loadPreset` with preset 0's sound first, as PHYSIC's), `.morph`, `settingCount`, `userList`, `builtInPresets`, `specFor`, DEVICE in `App::screenNow()`, the picture (the `SynthSpec` hook widened), KEYS n beside UNISON, the X audition struck again on a change read at the strike, ROLL; stills and `kViews`; the tests' lists (`textsize`, `scope_test`, `save_test`, `modapp_test`, `design_test`); the words test plants the names a wavetable synth might be compared to | `app_test --morph` (a track saves and loads, undo, every reading, every stop's hints at 0 over, ROLL's 200 seeds in range, the picture's lines off the drawn screen in BROWN and PAPER), `textsize` at XL, `textviews`, `themeviews`, `words`, **an `idle_test` part** (the readout stirs the redraw, and stops) | MAIN's room (CLOUD's: it fits); the picture on a light theme |
| **3** | **The 32 presets** (8 in `kPresets`, 24 in `morph.inc`) | `presets_test` (no problems; `--heard-morph`: level, peak, tail, every MOD heard), no clip as written and with macros full | presets judged by numbers only |
| **4** | **The designer page**: the frames' outlines in the spec, the frame at POSITION, the stack, a click sets POSITION, ROLL | `design_test testMorphFromThePage`; the script tried in a browser (Playwright) | the page's script has no `ctest` |
| **5** | **Review**: a fresh read of the branch, mutation testing on the rule, the tables, the voice and the app, `bench` again; docs: README's MORPH page, CLAUDE.md (Adding an instrument's lists, Measured, MORPH's rules) | `ctest`, mutants | |
| 6 | **Later, the owner's call 7**: a player's own table from the recording (`holdsRecording`, `Instrument::setTable`, made on the helper thread when the song opens) | the frames' harmonics against the recording's windows; the seam; the phases held to frame 1's; the table swapped while keys sound (ASan) | an engine change |

Batches 2 and 4 can run side by side after 1; 3 needs 2.

### Stills (batch 2)

`morph` (MAIN, A on ANALOG at 30, B off), `morph-b` (B on, MIX 50), `morph-sounding` (a
key held, an LFO on POSITION: the readout; taken before the engine renders, so the test
reads it off a drawn screen as CLOUD's), `morph-1` to `morph-5` (the pages), `morph-keys`
(VOICE at UNISON 4: KEYS 3), `morph-long` (the widest preset name and table name).

## 9. Batch 0, as built (2026-11-17)

`engine/include/apotheca/morph_rule.hpp`, held by `tests/morph_test.cpp` (`ctest -R morph`,
TIMEOUT 120, a hundredth of a second). No sound, no engine change, no app change. What it holds
is listed at the top of the header; every figure is SELF.

| What | Held by |
| --- | --- |
| The 29 settings (room 40), their ranges, usual values, the 16 table names (8 letters at most), TABLE B's OFF, the filter's names, which three settings wait for a strike | `testSettings` |
| The eight macros (none a choice), POSITION bending A and B, the house rule, **the 512-sample slide: a line, no step past 1/512 of the move** | `testMacros` |
| Every reading at both ends and the middle, the cutoff with KEY TRACK and the MOD ENV added in octaves and held, NaN held | `testReadings` |
| The 18 sizes: harmonics, lengths (12 800 floats a frame, 50 KB: the plan's), which size each key reads at 48 and 44.1 kHz, **at 48 kHz a size change drops nothing under 19 852 Hz** | `testSizes` |
| POSITION to the pair of frames: frame 1 and the last exactly, the place read back, never past the table | `testFramePlace` |
| The phase step (a second at 440 Hz is 440 turns), the point and the part read back at every size | `testPhase` |
| The pool: KEYS 6, 6, 4, 3 at UNISON 1 to 4 with B, 6 without; as many as fit, never more than six | `testPool` |
| UNISON's spread even and centred, the sides equal power | `testUnison` |
| The rolls: a stream alone at 0, together each its own start, spread evenly over the turn (40 000 rolls) | `testRolls` |

- **A fault found by the test, mended**: the read's fraction, made from every bit under the
  point, rounds up to 1.0 in a float just under a whole point, so a read would have taken the
  pair one point on. `readFraction` takes only the top 23 of those bits now.
- **Mutants: 18, all killed** (one survived at first, "under" for "at or under" in `sizeFor`:
  no check stood exactly on the ceiling; one does now).
- **Not run**: the whole `ctest` (nothing outside the new header and its test was touched;
  `morph`, `words` and `temp` pass). Nothing is heard: there is no sound yet.

## 10. Batch 1, as built (2026-11-17): the tables and the voice

**Built and tested on the desk; nobody has heard it, not run on the handheld, no app side yet.**
Every figure is SELF.

| File | Holds |
| --- | --- |
| `engine/src/morph/tables.hpp`, `tables.cpp` | a table's layout, the FFT, the 16 recipes, `tableFromHarmonics` / `tableFromWaves`, `builtInTable`, `turnSin` / `turnCos` (the house's polynomials: no library call, so the handheld's tables are the desk's) |
| `engine/include/apotheca/morph.hpp`, `engine/src/morph.cpp` | the synth: 12 key slots, the 24-stream pool, the tick, the reads, the filter, both envelopes, 8 engine presets (BASIC) |
| `engine/include/apotheca/instrument.hpp` | `SynthKind::Morph` appended; `grainPlaces`' comment widened (MORPH's places) |
| `engine/src/engine.cpp` | `addSynthTrack` makes it |
| `app/bench_run.cpp` | "MORPH, 6 keys", "MORPH, the pool full", "MORPH asleep", and the tables' making time |
| `tests/morph_test.cpp` | `ctest -R morph`, about 12 s: the rule, the tables, the voice, through the engine |
| `tests/words_test.sh` | `engine/src/morph` in `kBeside` (11 folders) |

### Where it differs from sections 2 to 4

| Plan | Built | Why |
| --- | --- | --- |
| 8 points a harmonic, 50 KB a frame | **32 points a harmonic** (capped at 2048), **80 KB a frame**, 2.5 MB a table, **41 MB for the 16** | measured: a straight-line read makes an image of each harmonic about 12 dB further down per doubling of its points. At 8, FOLD's last frame (its energy in its top harmonics) aliased **-39 dB** under its loudest at key 96; at 32, **-64 dB at worst** over every table at keys 36 to 96. The plan's throwaway weighted the error by a saw's energy and hid it |
| The MOD ENV sample by sample | **stepped once a tick** (its shape made at a sixteenth of the rate); both envelopes land on their sustain on the tick | the cost (below) |
| The setting FILTER TYPE | **FILTER** (`morph::Filter`) | `FilterType` is the engine's own enum's name |
| Pool full target 0.5% | 0.64 to 0.68% (under the 0.7% ceiling) | below |
| A first-pass voice | the frame pair taken once a run when it does not change, the size tables worked out once, nothing slid when nothing is sliding | below |

### Measured (desk)

| What | Figure |
| --- | --- |
| The 16 tables made (once, at the first MORPH track's `prepare`) | **130 to 200 ms** (a plain double FFT); the handheld at x13 to x20: **2 to 4 s, a guess**, so batch 2 makes them on the helper thread at the program's start |
| Pitch, a sine frame, keys 24 to 96 | 0.0002 cents off at worst |
| A frame as made, harmonics 2 to 10 read off the sound (ANALOG, VOWEL, FM, ORGAN) | within 0.1 dB of the table |
| A quarter of the way between two frames | 3.5e-8 from the straight crossfade, sample for sample |
| Aliasing under 10 kHz, every table at its middle and last frame, keys 36 to 96 | **-64 dB** at worst (FOLD, key 96) |
| The worst dip half way between two frames | under 1 dB for 13 tables; SYNC 1.84, HARMONIC 3.01 (its frames share no harmonic), ANALOG 0.54 |
| Filter, a saw at key 36, CUTOFF 1047 Hz | LOW 12 at 4 kHz -25 dB, LOW 24 -50 dB, HIGH at 131 Hz -36 dB, BAND -6 at its middle; KEY TRACK holds the cutoff a key-octave apart to 0.01 dB |
| A MOD on POSITION (20% to 100%) | a line: the most it moves a sample is 0.8 / 512, exactly |
| A steal, a key struck again, allNotesOff | no step past what the sines sounding could make (0.07 against 0.097) |
| A minute held on a decay to nothing, then let go | the least state held 1.1e-9, then exact zeros |
| The 8 engine presets, a chord of four | -23 to -9 dB, peak 0.95 at most (Pulse Width) |
| **Cost** (`bench`, a quiet run: Phazed 0.9%, JAX-EP 0.61% in it) | **6 keys (A + B + SUB, LOW 12, the MOD ENV on POSITION) 0.47%; the pool full (UNISON 2, A + B, WIDTH, LOW 24) 0.67%; asleep 0.00%**. `morph_test --measure cost`: 0.43 / 0.29 (A alone) / 0.64 / 0.42 (UNISON 4, 3 keys) |
| Instructions (callgrind, a second, clang -O2) | 6 keys A + B, LOW 12: **53 M**; the pool full: **87 M**. By the speed plan's rule (1 G is about a handheld core): **about 5% and 9% of a handheld core, not run there**. JAX-EP's 6 notes: about 9.5% |

**Over the plan's targets** (0.3% plain, 0.5% the pool): the plain case is 1.5 times its target,
the pool full is under the ceiling. Cut from 88 M to 53 M instructions in this batch (the size tables
were worked out by loops every tick; the MOD ENV ran a sample at a time; the frame pair was taken
every sample). Left, if it must go lower: the gains and places as arrays each run (about 20% of
what is left), the per-key tick (about 15%).

### Tests and mutants

`morph_test` holds the rule (batch 0) and: the tables (32 frames, peak 1, no DC, guards, each size's
harmonics as made and none past them, the tables' sine against the library's, the dips), a table from
given harmonics, pitch, frames as made and the straight morph, aliasing, the filter, SUB, LEVEL,
VELOCITY and WIDTH's sides, glide, POSITION moved by the MOD ENV and by a macro, the pool (UNISON's
key counts, mixed UNISONs, a let-go key stolen first, then the oldest, a key struck again), no click,
blocks 1 to 333 to the bit with everything moving, through the engine (0 to 3 helpers, other blocks,
Play from the top, the readout), no allocation, tiny numbers, NaN, silence, Play from the top with the
rolls, the presets.

**Mutants: 22, 21 killed.** Six survived the first run: five were tests that could not reach them
(a key let go before it had sounded, mixed UNISONs never tried, the first sample's unison roll, the
first tick's place, a key struck again) and each got its check; **one is kept as a guard**: the filter's
states snapped on the tick, which no sounding key reaches (the oscillators always feed the filter; the
envelope is after it).

**Not done**: the desk and the handheld's bits compared under qemu (the tables use no library call
and the voice has `fp contract(off)`, as PHYSIC's; not run); the 24 data presets (batch 3).

## 11. Batch 2, as built (2026-11-17): the app

**Built and tested on the desk; nobody has heard it, not run on the handheld.** CLAUDE.md's "MORPH in
the app" says what is where.

| Plan | Built | Why |
| --- | --- | --- |
| A picture kind (NONE, RECORDING, TABLE) in `SynthSpec` | **`SynthSpec::table`**, a flag beside `recording` (every spec given it) | the smallest change: `SynthScreen::picture()` asks either |
| KEYS n beside UNISON | **UNISON reads "2, 6 KEYS"** (`mo::readingIn`: the keys `keysFor` leaves, B on or not) | the box has room; no new place on the page |
| The tables made on the helper thread at start, if the handheld's figure is over 0.1 s | **always, a thread of their own from `main()`'s first line**, joined at its end | 130 to 200 ms on the desk already; MAIN's picture and a track added wait on the same lock if it is not done |
| "MAKING THE TABLES" while they are made | **none: MAIN's picture waits for them** | a state that cannot come back in a process (the tables are never unmade) cannot be held by `idle_test`, so it would be a picture the redraw might leave stale |
| TABLE, UNISON or B changed while X is held strikes the audition again | **not built** | the key sounding keeps what it was struck with; letting X go and holding it again hears the change |

**The picture** (`SynthScreen::drawTablePicture`, in CLOUD's room on MAIN): A's wave across the box in
teal, B's in purple behind it, a strip of 32 frame marks with POSITION A in brass; while a key sounds
the wave the NEWEST key reads (the MOD ENV or a MOD moving it) and a teal tick on the strip, through
CLOUD's readout (`Engine::grainPlaces`, `kStirGrains`), so the redraw needed nothing new. The table names
over it in the fixed lettering ("ANALOG / VOWEL").

**Tests**: `app_test --morph` (`testMorphOnATrack`: the track from the SONG screen, the list's ninth row,
the engine plays it, the pages and every item once, every reading, **every click of all 29 settings reads
another way**, UNISON's keys, the macros take MODs, ROLL's 200 seeds in range and what it keeps, ROLL a
step of undo, a player's preset over preset 0, the song file; `testMorphsHintsAndPicture`: every stop of
every page at 0 over with either help, A held and not, Y and HOLD X on MAIN, the brass mark, teal and
purple in the picture, a sounding key's tick, the sine frame crossing the middle). `idle_test` has a part
(the MOD ENV moving POSITION while a key sounds). The tests' lists: `textsize`, `scope_test`, `save_test`,
`modapp_test`, `design_test`, `presets_test`. Stills `morph`, `morph-b`, `morph-1` to `morph-5`,
`morph-keys`, `morph-long`, `morph-sounding` (in `kViews`: `textviews` and `themeviews` draw them at every
size and theme).

**The list of instruments, nine rows**: they close up to 41 adding a track and 36 with the words under
them. At 36 the large name ran into the line under it at every size (the still showed it; `textviews`
did not catch it: the two texts are one row's), so a box under 44 draws its name in the medium lettering
and both lines by their own heights in its middle (`SongScreen::kListLineGap` 4, with `kListTailRoom` 5 kept for the tails and the outline), held at LARGE's at XL.

**Not done here**: the designer page's picture (batch 4: today the generic
synth page serves every setting and ROLL), README's page and the review (batch 5).

## 12. Batch 3, as built (2026-11-17): the 32 presets

The 24 of section 6 are data in `app/presets/morph.inc`, after the engine's 8 (BASIC): PADS, LEADS,
BASS, MOVING, six each. Names made up. **Nobody has heard any of them.**

| Planned | Built | Why |
| --- | --- | --- |
| PADS: HALL, BLOOM or SHIMMER "on SPACE" | **in the track's first slot** (Glass Choir SHIMMER, Slow Vowels HALL, Harmonic Bloom BLOOM), Warm Strings and Drift Bells send into SPACE | a preset holds the track's sound (three slots, two sends, MODs), never a bus's effect |
| The engine's 8 as batch 1 left them | **LEVEL moved on six** (Analog Sweep -3, Pulse Width -4, Harmonic Walk -3, Fold Grow -4, Vowel Talk 0, Organ Stops -2 dB) | Pulse Width peaked 0.76 and Vowel Talk sat at -22 dB: the 32 now lie within 6.6 dB of each other |
| `--heard-morph` its own phrase | **FM-6's player** (`hearFm6Preset` takes the kind: a bar of keys, or the bass line for a BASS folder) | the same phrase, round twice, Stop, the tail; no second copy of the player |

MOVING has every MOD the plan named: an LFO in time (Table Walk, 1 BAR TRIANGLE), a FREE LFO (Free
Swirl, 0.4 HZ; Drift Bells in PADS, 0.15 HZ DRIFT), a STEPPER playing the table as a sequence of waves
(Wave Sequence), a STEPPER with CHANCE (Chance Waves), MOD 2 on CUTOFF (Two Movers), and a RANDOM quarter
(Ratio Tremble). Two leads send into ECHO; Drive Bass has DRIVE, Wide Fold Bass SATURATE.

**Measured** (`presets_test --measure-morph`, the desk): the loudest tenth -15.9 dB (Pulse Width) to
-22.5 dB (Talking Bass); every peak under 0.6; every tail ends inside 3.2 s of Stop (a reverb's);
every MOD moves and is heard, the faintest -12 dB (Drift Bells: the phrase with it off differs by a
quarter of its RMS). Cost while playing 0.14% (a bass) to 1.0% (Harmonic Bloom, UNISON 2 with B and
BLOOM) of a desk core.

**`presets_test --heard-morph`** (ctest `presets_heard_morph`, about 4 s) holds all 32, the engine's
too: -26 to -10 dB, peak under 0.9, a tail inside 30 s, each MOD moving and heard (1% of the RMS), and
the loudest and the quietest within 8 dB.
