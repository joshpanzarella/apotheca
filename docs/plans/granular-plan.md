# CLOUD plan (the granular synth)

Written 2026-11-16 from the code at `main` f401940 (0.659), branch `granplan`.
**Groundwork only: nothing is built.** The code was only read. No figure here
was measured; every cost is a guess from the sampler's, PHYSIC's and ACID's
rows. **The owner cannot hear**: every claim about the sound is a test.

The owner asked (queued 2026-11-14, after ACID): "a granular synth (clouds
from the recordings)".

**What "granular" means, in one line**: the synth plays many short pieces of a
recording (grains, 5 ms to 1 s each) at once, each from a slightly different
place, pitch and side. Dozens of them overlapping make one sound: a cloud.

## The recommendation

**One new synth, CLOUD: up to 6 keys, a shared pool of 32 grains, read from the
same recording a sampler track uses.** Each key is a cloud at its pitch. Every
grain's place, pitch, side and direction is a roll by where and when, never a
walked generator. **Every setting is read when a grain starts, and a grain once
started never changes**: that is what makes it block-length independent and
free of zipper noise without a single glide inside the grains.

| Question | Answer |
| --- | --- |
| What it is | **Clouds of grains from a recording on the card**: POSITION, SPRAY, SIZE, DENSITY, SCATTER, SHAPE, PITCH SPRAY, REVERSE, STEREO, SCAN |
| Name on the device | **CLOUD** (short name `CLOUD`, song word `cloud`, presets `NAME.cloud`). Alternatives: GRAIN, MIST |
| The `says` line | **"clouds of grains from your recordings"** (37 letters) |
| Its sound | **The sampler's recording**: the same `TrackData::sample`, the same `sound NAME` line, the same list of recordings and preview, CATCH and RECORD on its SOURCE page as on the sampler's page |
| No recording | **A built-in TONE**, 2 s, made in code once per sample rate (as the drum kit is), so "add a track, place notes" sounds |
| Keys | **Pitch**: key 60 (ROOT) plays the recording as it was made; a chord is a cloud a key |
| Voices | **6 keys**, each at most **12 grains** at once, from a **pool of 32** for the track. A full pool skips new grains (the cloud thins); it never cuts one |
| Settings | **17** (room for 24), 8 macros, MAIN + 3 pages |
| Cost target | **One key at preset 0 ≤ 0.08% of a desk core; the pool full ≤ 0.5%** (handheld 7 to 8.5% at 13 to 17 times; JAX-EP's 6 notes are about 9.5% there). Ceiling 0.7% |
| Memory | **Nothing new for the sound**: the recording is the shared `std::vector<float>` the sampler gets (mono, 48 kHz, 10 s at most: 1.9 MB). The pool is 32 × about 64 bytes |
| Engine changes | **None outside the new synth.** `setSample`, `noteOnFrom`, `noteSeed` and `stopNotes` are there |
| First thing to build | **Batch 0**: `cloud_rule.hpp` (the rules as numbers, no sound) and its test |

## 1. What it is

### The settings

Ids are appended, never reordered (songs and presets store them by number).
Every amount is 0..100 on the dial; the reading is in its unit.

| Id | Setting | Range, reading | Usual | What it does |
| --- | --- | --- | --- | --- |
| 0 | POSITION | 0..100%, of the recording | 25 | Where in the recording the grains come from |
| 1 | SPRAY | 0..100, read as ± seconds (squared: 0 to half the recording) | 10 | How far each grain's place is rolled from POSITION |
| 2 | SIZE | 5 MS to 1.0 S, log | 120 MS | A grain's length, as heard |
| 3 | DENSITY | 1 to 200 a second (read HZ), log | 16 HZ | How often a key starts a grain |
| 4 | SCATTER | 0..100% | 50 | How far a grain's start time is rolled: 0 even (a pitched buzz at high DENSITY), 100 random (a cloud) |
| 5 | SHAPE | 0..100: PLUCK, SMOOTH at 50, SWELL | 50 | Where the grain's peak is: a fast rise and long fall, a round one, or the other way |
| 6 | PITCH SPRAY | 0 to ±12 semitones, read in CENTS | 0 | How far each grain's pitch is rolled |
| 7 | PITCH STEPS | choice: FREE, OCTAVES, FIFTHS | FREE | The roll lands anywhere, or on octaves, or on octaves and fifths |
| 8 | REVERSE | 0..100% chance | 0 | Each grain's roll: played backwards |
| 9 | STEREO | 0..100% | 40 | How far each grain's side is rolled (equal power) |
| 10 | SCAN | ×-2.00 to ×2.00, ×0 at 50 | ×0.00 | POSITION moves by itself while a key is held: ×1 goes through the recording at the speed it was made, negative backwards. **×0 is FREEZE** |
| 11 | ROOT | key 0..127 | 60 | The key that plays the recording at its own pitch (the sampler's) |
| 12 | TUNE | ±100 CENTS | 0 | |
| 13 | ATTACK | the sampler's scale | 2 MS | The key's fade in (the shared `envelope.hpp`) |
| 14 | RELEASE | the sampler's scale | 300 MS | The key's fade out; its grains go on under it |
| 15 | LEVEL | 0..100, read in DB | 80 | |
| 16 | VELOCITY | 0..100, 0 OFF | 50 | How much a key's velocity sets its level |

**Not in version 1** (each is an owner's call, section 7):

- **FEEDBACK: no.** It needs a buffer the track writes into: a copy of the
  recording per track (1.9 MB) and a loop that can howl, with its own way down
  (TAPE's rule). The DELAY and REVERB families (BLOOM, SHIMMER) on a slot do
  the job and already keep the house rules.
- **FREEZE as a switch: no.** SCAN at ×0 holds the place; a STEPPER MOD on SCAN
  is a freeze that comes and goes on the beat. A switch would be a choice
  macro, which takes no MOD.
- **Keys as places** (each key a part of the recording, like slices): later, as
  a KEYS choice appended.
- **DENSITY in steps** (grains on the beat): later, as a SYNC choice appended.

## 2. How it fits

### The recording is the sampler's, through the sampler's path

A sampler track already holds one recording: `TrackData::sample` (the name),
`Session::setTrackSample`, `LaneSound` slot 0 (`trackSound`), and the engine's
`Instrument::setSample(data, length)` (engine.cpp, with the old one through the
trash). **CLOUD takes the same field, the same calls and the same `sound NAME`
line in the song file.** A track is one kind, so one field serves both.

The app tests `TrackKind::Sampler` in 26 places (10 files). Those that mean
"this track holds a recording" (`sampleUses`, `deleteSample`,
`recordingChanged`, `findMissing`, the rename that follows saved songs, the
song file's `sound` line, the picker) go through **one new predicate,
`holdsRecording(kind)`** (Sampler and Cloud). Those that mean "the sampler's
own settings" stay. Batch 2 lists each of the 26 and says which it is.

### Choosing it, and seeing it

- **SOURCE page, its first row is RECORDING**: hold A + up / down, the sampler's
  list of recordings with its three columns and its preview (`SamplePicker`,
  as `SamplerScreen` uses it). **Y is CATCH and R1 is RECORD**, as on the
  sampler's page: a caught bar or a mic take lands in the samples folder and on
  this track.
- **The picture is `ui::wavePicture`**, the sampler's: the recording's outline,
  **POSITION as a brass line, SPRAY as a lit band about it**, and **while a key
  sounds, each grain as a short tick at its place** (the engine keeps the
  sounding grains' places in a small array of atomics after each chunk, as
  `effectMeter` does; the redraw counts it as a readout).
- **On the roll**: notes play it at pitch; a chord is one cloud a key; a held
  note's grains go on while it is held, then RELEASE fades the key out. GLIDE
  works as on the sampler (`Slide`): each grain takes the key's pitch where the
  slide is when the grain starts. GLIDE FROM serves it unchanged.

### No recording

**A built-in TONE**: 2 s, made once per sample rate on the app's side of
`prepare` (never on the audio thread), shared as `DrumKit::builtIn` is: three
soft partials beating slowly and a little air, a hash-made noise. Its picture
is drawn dim and the name reads `TONE (BUILT IN)`. A track whose recording is
missing plays it too and says FILE NOT FOUND, as the sampler does.

### Lane EDIT

**None needed.** EDIT cuts, fades and pitches a lane's file for the drums;
CLOUD's POSITION, SPRAY and SIZE already choose what part is heard, and the
file is never written either way. A lane's edited sound as a cloud's source is
not offered.

## 3. DSP

| Part | Design | Why |
| --- | --- | --- |
| Onsets | Each key counts **whole samples from its strike**. The wait to the next grain is `round(rate / DENSITY)` times `1 + SCATTER × (2u - 1)`, at least 1 sample; u is the grain's roll | Whole samples: the same sound in any blocks. DENSITY read at each onset, so a MOD on it lands at the next grain |
| Rolls | `grainUnit(seed, n, what)`: a hash of the key's `noteSeed` (PHYSIC's `strikeSeed`: track, key, the strike's moment), the grain's number since the strike, and which roll (TIME, PLACE, PITCH, SIDE, BACK) | Never a generator walked: Play from the top is the same cloud, on any helper count |
| Place | SCAN's place (POSITION + SCAN × seconds since the strike, in samples) plus SPRAY × (2u - 1). **Wraps at the recording's ends** | A cloud has no edge to fall off |
| Pitch | `2^((key - ROOT + slide) / 12 + TUNE + PITCH SPRAY roll)`, the roll put on PITCH STEPS' grid; REVERSE reads backwards from the grain's far end | Fixed for the grain's life |
| Read | **4-point Hermite** between samples. **At the recording's own pitch with no PITCH SPRAY the start is rounded and whole samples are read** (no interpolation at all). A grain that crosses an end is read as two runs | Cubic keeps pitched grains clean; the plain path is the cheap one |
| Window | Two half-cosine pieces from one 257-point table (linear read): rise over SHAPE's share of SIZE, fall over the rest, **each at least 0.5 ms**. Starts and ends at exact 0 | No click at any SHAPE or SIZE |
| Level | Each grain's gain is `1 / sqrt(max(1, DENSITY × SIZE))` (the expected overlap), times the side's equal-power gain, fixed at its start | Uncorrelated grains add in power: the cloud stays about as loud whatever DENSITY and SIZE are |
| Key | The shared envelope (ATTACK, RELEASE) on the key's sum, sample by sample. A key is free when its envelope is done; its grains end with it | |
| Steal | The oldest let-go key first, then the oldest; a stolen key fades over 5 ms with its grains (a tail slot, as PHYSIC's) | No click |
| Pool | 32 grains for the track, at most 12 a key. **A grain that finds no room is not started**; its number is still counted, so the rolls after it are the same | The cloud thins under load and never glitches |

**House rules it keeps:**

- **The audio thread never allocates**: the pool is fixed, `setSample` takes a
  pointer the app made. **A new recording ends every grain at once** (as the
  sampler's voices): the old one goes to the trash and is freed next frame, so
  a 5 ms fade would read freed memory.
- **Tiny numbers**: nothing is fed back. The window ends at exact 0, the
  envelope lands (the house's `Hold` rule), a key with no grains and its
  envelope done does no work. The recording is finite: batch 1 checks that
  `readWav` never gives a number that is not (if it can, the app makes it so on
  reading, never the audio thread).
- **Blocks**: onsets, SCAN's place and the window are counted in whole samples
  from the strike; settings are read at onsets; `setMacro` stores and returns.
  The same sound in blocks of 1, 7, 64, 256, to the bit.
- **Locals**: each grain's run is worked on locals and copied back (WEAR's
  lesson: 0.40% to 0.15%).
- **NaN**: a macro or velocity that is not a number is held, never a stuck key.

### Cost

Guess, desk: about **4 to 5 ns a grain a sample** (Hermite 2.5, window 1.5,
gain and side 0.5), the plain path about half. Overlap is DENSITY × SIZE.

| Case | Grains sounding | Desk | Handheld (×13 to 17) |
| --- | --- | --- | --- |
| Preset 0, one key (16 Hz × 120 ms) | about 2 | **0.04 to 0.05%** | under 1% |
| A 4-key chord of preset 0 | about 8 | 0.15 to 0.2% | 2 to 3.4% |
| **The pool full** (32) | 32 | **0.6 to 0.75%** guessed, **target 0.5%** | 7 to 13% |

The full pool is over the target in the guess: **batch 1 measures it first**.
If it is over, in this order: the plain path for whole-sample grains; the
window as a table of the grain's own (filled once at its start, not read twice);
linear in place of Hermite for grains under 20 ms (too short to hear the
difference; the owner's ear decides later); then the pool at 24. `bench` gets
two rows: one CLOUD track with a 4-key chord of preset 0, and the pool full.

## 4. Macros, pages, MODs, presets

| Lane | Macro | MOD target |
| --- | --- | --- |
| 0 | POSITION | yes |
| 1 | SPRAY | yes |
| 2 | SIZE | yes |
| 3 | DENSITY | yes |
| 4 | SCAN | yes (a STEPPER on it: freeze and go) |
| 5 | PITCH SPRAY | yes |
| 6 | SHAPE | yes |
| 7 | REVERSE | yes |

**No macro is a choice**, so `macroIsChoice` has no CLOUD entry and every one
takes a MOD. 0.5 is as the patch says, as every synth's. **A MOD on POSITION
is heard grain by grain** (read at each onset): an LFO on it sweeps the cloud
through the recording with no click, which is the classic granular move.

| Page | Holds |
| --- | --- |
| MAIN | PRESET, **the picture** (`kWaveH` 80, POSITION, SPRAY, grains), the eight macros in two rows of four |
| SOURCE | RECORDING (the list), POSITION, SPRAY, SCAN, ROOT, TUNE |
| GRAINS | SIZE, DENSITY, SCATTER, SHAPE, REVERSE, STEREO |
| PLAY | PITCH SPRAY, PITCH STEPS, ATTACK, RELEASE, LEVEL, VELOCITY |

Each page three rows of two. MAIN is the tight one: the picture takes the
room of a third row of macros, so `kMacroH` is less there than JAX-EP's; batch
2 checks it at XL (`textsize`) and the hints at 0 over.

**Y on MAIN is ROLL** (`SynthSpec::roll`, as PHYSIC's): it rolls SPRAY, SIZE,
DENSITY, SCATTER, SHAPE, PITCH SPRAY, PITCH STEPS (FREE mostly), REVERSE and
STEREO by a hash of the seed and each one's place; the recording, POSITION,
SCAN, ROOT, TUNE, the envelope and the levels stay. One step of undo. Y on
SOURCE is CATCH (the sampler's).

### The WiFi designer page

**A sampler-like page** (`"page": "cloud"`, or the sampler's page with the
synth's settings): the spec's `"waves"` gives the recording's 512 peaks, as the
sampler's does, with **POSITION as a mark and SPRAY as a band**, which is new
in the page's script (a mark with a width). Every setting as a synth's, the
recording chosen from `"recordings"` (`sample NAME`), ROLL as `roll N`. The
script change is tried in a browser (Playwright), as the house rule says.

### Presets

**24 in four folders, as data** in `app/presets/cloud.inc`
(`docs/plans/presets-plan.md`). Preset 0 is the one a new track gets.

| Folder | Count | What |
| --- | --- | --- |
| STILL | 6 | SCAN ×0, long grains, high DENSITY: a held pad from one place. **Preset 0: Soft Cloud** (POSITION 25, SPRAY 10, 120 MS, 16 HZ, SCATTER 50, STEREO 40) |
| DRIFT | 6 | SCAN slow forwards or back, some with an LFO on POSITION |
| DUST | 6 | Short grains, low DENSITY, SCATTER high, REVERSE: crackle and specks |
| CHOIR | 6 | PITCH SPRAY on OCTAVES or FIFTHS, long SHAPE: SWELL; two with REVERB BLOOM on SPACE (whole sound) |

**Built-ins never name a recording**: they are ways of playing whatever the
track holds (the TONE if nothing). **A player's own whole preset remembers its
recording** (a `sound NAME` line in the preset file, through the one reader of
song lines): picked, it puts that recording in if the card has it (by name,
else by base name if exactly one, as `findMissing`), and otherwise keeps the
track's own and says RECORDING NOT FOUND. A plain preset changes the synth only.

`presets_test --heard` plays each through the TONE and through a test
recording made by the test (a sine sweep and a noise burst): loudness, peak, a
tail that ends, every MOD moving and heard.

## 5. Batches

One job each. Each ends with `ctest` green under `set -o pipefail` and its
mutants killed.

| Batch | What | Tests (`tests/measure.hpp` tools) | Risk |
| --- | --- | --- | --- |
| **0** | **The rule, no sound.** `engine/include/apotheca/cloud_rule.hpp`: the readings (MS, S, HZ, CENTS, DB, ×), the onset wait (DENSITY, SCATTER, whole samples), `grainUnit`, the window's two pieces and their 0.5 ms floor, the overlap gain, PITCH STEPS' grid, the wrap, SCAN's place | `cloud_rule_test`: case by case, both ends of each reading, the window at SHAPE 0 / 50 / 100 starts and ends at 0, mutants | Little |
| **1** | **The engine.** `cloud.hpp/.cpp`, `SynthKind::Cloud` appended, the factory; keys, onsets, the pool, the read, the window, steal, the TONE; the grains' places for the picture; `bench`'s two rows | **Grain timing to the sample** (a recording of one click: each onset where the rule says); **density measured** (onsets over 10 s within 1%, the mean with SCATTER); **pitch to the cent** (a sine recording, keys 36 to 84, `partNear`, within 1 cent, PITCH STEPS on the grid); **no click at grain edges** (the house no-click tool, SIZE 5 MS, every SHAPE); level within ±3 dB over a DENSITY × SIZE grid; **blocks 1 to 256 to the bit**; **3 helpers to the bit** through the engine; **no allocation** (counted `new`, as `nextscene_test`); **too small** (a 60 s tail, `TooSmall`); NaN; silence in, silence out; a full pool skips and never clicks; Play twice the same; a new recording mid-key reads nothing freed (ASan) | **The full pool's cost** (section 3's order of cuts). The wrap's two runs (an off-by-one reads past the end: ASan) |
| **2** | **The app.** `TrackKind::Cloud` appended, its row last in `kInstruments`, `CloudPatch` (room 24), `holdsRecording` through the 26 places, the spec `cl` (pages, readings, `loadPreset` defaults first as PHYSIC's), `.cloud`, the lists (`userList`, `builtInPresets`, `settingCount`, `specFor`), DEVICE in `App::screenNow()` with the SOURCE page's picker, CATCH and RECORD, the picture and its grains, ROLL; stills and `kViews`; the tests' lists (`textsize`, `scope_test`, `save_test`, `modapp_test`) | `app_test` (saves and loads, undo, `sampleUses`, delete, rename and `findMissing` on a CLOUD track), the picker on CLOUD, `textsize` at XL, `textviews`, `themeviews` (the band and ticks on PAPER), `words`, hints at 0 over, **an `idle_test` part** (the grains move: the redraw must see them, and stop when the key ends) | MAIN's room: the picture and eight macros at XL |
| **3** | **The 24 presets** in `cloud.inc`; a player's preset with its recording | `presets_test` (no problems; `--heard` through the TONE and the test recording), no clip as written and with macros full, a whole preset's recording put back and not found | Presets judged by numbers on a test recording, not the owner's |
| **4** | **The designer page**: the spec, the band mark, `sample NAME`, `roll N` | `design_test` `testCloudFromThePage`; the script tried in a browser | The page's script has no `ctest` |
| **5** | **Review**: a fresh agent reads the branch, mutation testing on the rule and the engine, `bench` again; docs: README's CLOUD page, CLAUDE.md (Adding an instrument list, Measured, CLOUD's rules) | `ctest`, mutants | |

Batches 2 and 4 can run side by side after 1; 3 needs 2.

## 6. Risks, in short

- **Cost on the handheld** is the big one: 32 grains is 7 to 13% of its core
  by the guess. The cuts are listed; the pool's size is one constant.
- **Pitch up aliases**: a grain read at 4 times its speed has no filter. Not
  mended in version 1; `asideOf` measures it at +24 and the figure goes in the
  plan.
- **A swap of recording cuts the cloud** (no fade, section 3). A fade would
  need the old recording held past the next frame.
- **Nobody hears it**: SCATTER, SHAPE and the overlap gain are SELF guesses,
  held only by numbers.

## 7. The owner's calls

Each with the pick. A later "no" changes one constant or one row.

| # | Call | Pick |
| --- | --- | --- |
| 1 | Name | **CLOUD** (not GRAIN, not MIST) |
| 2 | Keys at once | **6** |
| 3 | Grains | **A pool of 32 for the track, 12 a key**; a full pool thins the cloud, never cuts a grain |
| 4 | FEEDBACK | **No** (a DELAY or REVERB on a slot does it) |
| 5 | FREEZE | **No switch: SCAN at ×0 is frozen**, and a STEPPER on SCAN freezes and goes |
| 6 | No recording | **A built-in TONE**, 2 s, made in code |
| 7 | Keys | **Pitch** (ROOT 60 as recorded). Keys as places later |
| 8 | DENSITY | **Free, in grains a second**. On the beat later |
| 9 | Between samples | **Hermite**; whole samples at the recording's own pitch |
| 10 | The picture | **On MAIN, with the sounding grains as ticks** |
| 11 | Y on MAIN | **ROLL a new cloud** (POSITION, the recording and the levels stay) |
| 12 | CATCH and RECORD | **On SOURCE, as on the sampler's page** |
| 13 | Presets and recordings | **Built-ins name none; a player's whole preset puts its recording back** if the card has it |
| 14 | Presets | **24 in four folders** (STILL, DRIFT, DUST, CHOIR); preset 0 Soft Cloud |
| 15 | A new recording while keys sound | **Cuts the cloud at once** (as the sampler) |
| 16 | The recording's ends | **Wrap** (a cloud has no edge) |

## As built: batches 0 and 1, the engine (2026-11-16, branch `cloud`)

**The engine is built and tested; the app is not.** Nobody has heard it; it has not run on the
handheld. Every figure is SELF. The owner's calls were not answered: built at the picks, **but the
pool is 24, not 32** (the fourth cut, below).

### What is where

| File | Holds |
| --- | --- |
| `engine/include/apotheca/cloud_rule.hpp` | the settings (17, room 24), the macros, the readings, `grainUnit`, `onsetWait`, the window (`windowAt` exact, `windowPoly` the synth's), `overlapGain`, `sideGains`, PITCH STEPS' grid, `grainRatio`, `wrapPlace`, SCAN's place |
| `engine/include/apotheca/cloud.hpp`, `engine/src/cloud.cpp` | the synth: 6 keys (12 slots: 6 to fade), the pool, the reads, the TONE, 3 engine presets |
| `engine/include/apotheca/instrument.hpp` | `SynthKind::Cloud` appended; `Instrument::grainPlaces` (-1 for every other synth) |
| `engine/src/engine.cpp`, `engine.hpp` | `addSynthTrack` makes it; `renderTrack` keeps its grains in `Track::grainAt` / `grainCount` after each chunk; `Engine::grainPlaces(track, places, most)` for the picture |
| `app/bench_run.cpp` | the rows "CLOUD, one key" and "CLOUD, full pool" |
| `tests/cloud_test.cpp` | `ctest -R cloud` (TIMEOUT 120): the rule and the synth, about 2 s |

**The recording reaches it exactly as the sampler's does**: `Engine::setTrackSample(track,
SampleData)` sends it, the audio thread hands it to `Instrument::setSample` and puts the old one in
the trash. Nothing new in the engine for it. The app only has to call that for a CLOUD track.

### Where it differs from section 3

| Plan | Built | Why |
| --- | --- | --- |
| Window from a 257-point table, read between points | **`windowPoly`**: sin(pi y) to its y^9 term, held to 0..1 (exactly 0 and 1 at the ends, 2e-6 from the half cosine), worked out over a run before the samples | the table and a cosine stepped along both cost more: every sample waited on the one before. Laid out first, the compiler takes it four at a time |
| A grain's place a double | **32.32 fixed point** | one integer add, whole samples exact |
| Settings read at onsets; SCAN "POSITION + SCAN x seconds" | SCAN **added up grain by grain** (each gap at the SCAN read at its end) | so a MOD on SCAN moves the place on from where it is, never jumps it |
| Gain 1 / sqrt(max(1, DENSITY x SIZE)) | the overlap **also held to 12** (`kGrainsPerKey`) | a key sounds 12 at most; without it the level fell 5 dB at the dense end |
| Pool 32 | **24** | the fourth cut |
| Steal: one tail slot | **12 key slots**: any key may fade (5 ms) while six new ones sound | allNotesOff fades all six; a slot free for each new key |
| ROOT, TUNE | as planned; LEVEL straight (LEVEL 50 is -6 dB) | |

### The cost, and the cuts

Measured first, the pool full (six keys at 200 grains a second, SIZE 80 = 0.35 s, PITCH SPRAY 30, so
every grain is a Hermite read; `cloud_test --measure cost`): **1.07 to 1.19%** of a desk core, against
the target 0.5% and the ceiling 0.7%. In order:

| Step | The pool full | One key of preset 0 |
| --- | --- | --- |
| first build: table window, double place, sample by sample, each grain times its key's envelope | 1.07 to 1.19% | 0.085 to 0.092% |
| cut 1, the plain path (whole samples at ROOT) | (built from the start: the pool test has PITCH SPRAY on; one key at ROOT with 11 grains costs 0.10%) | |
| cut 2, the window: `windowPoly` laid out over the run; each key's grains summed before its envelope; the Hermite in fewer sums, its four points gathered and the curve worked four at a time | 0.66 to 0.69% | 0.053 to 0.069% |
| cut 3, a straight read under 20 ms | (the pool test's grains are 0.35 s: no change there) | |
| **cut 4, the pool at 24** | **0.53 to 0.55%** | 0.05 to 0.07% |

The desk was busy (load 5 to 7). In one `bench` run: **CLOUD, one key 0.06%, CLOUD, full pool 0.65%**
with PHYSIC 8 notes at 0.31% (0.21% recorded), ACID 0.21% (0.14%) and JAX-EP 6 notes 0.64% (0.55%)
beside it: about a third over their usual, so the full pool is near 0.45 to 0.5% on a quiet desk.
**Still a hair over the target on this desk, under the ceiling.** Left if it must go lower: the
Hermite read is now the most of it (about 40 of 55 instructions a grain-sample); 20 grains, or a
straight read for every grain the owner's ear allows.

### Measured (`cloud_test --measure`)

| What | Figure |
| --- | --- |
| Grain timing | a click read at each grain's peak: 48 grains in 3 s, every one on its sample |
| DENSITY | SCATTER 0: the count over 10 s within one onset of the rate; SCATTER 50 and 100: within 1% over 60 s where there are thousands (each gap is rolled: a few dozen are within three sigma) |
| Pitch | keys 36 to 84 on a sine: 2e-5 cents at worst; PITCH STEPS: every grain on its grid |
| The read | a long grain: what is not the sine 104 dB down; sample by sample against the true sine, Hermite 1.6e-6, the straight read 2.1e-4 (its bound A d^2 / 8), across both ends and backwards too |
| Level | over 19 DENSITY x SIZE settings that are not sparse: -0.74 to +0.11 dB about the middle; 9.5 dB under the recording's RMS (the window's 3/8, the middle's two sides, LEVEL 80, VELOCITY) |
| Clicks | grains of 5 ms, every SHAPE, a flat recording: no step past the windows' own slope; a full pool skipping 329 of 617: largest step 0.0023 against 0.0035 allowed; a steal: 0.0011 |
| Tiny numbers | a minute held and 30 s let go: the least level held 1.7e-4 (the envelope's end is 1e-4), then exact zeros and nothing held |

### Tests and mutants

`tests/cloud_test.cpp`, in `ctest` as `cloud`: the rule (settings, readings, rolls, onsets, the window,
the level, pitch, the place), and the synth (timing, density, pitch, clicks, level, blocks 1 to 333
through the synth and through the engine with 0 to 3 helpers, Play again, the grains for the picture,
no allocation, tiny numbers, NaN, silence, Play from the top by step and by hand, a recording
swapped, the steal, the TONE, presets and macros, glide, SCAN and SPRAY, REVERSE, STEREO and
VELOCITY, the read).

- **The rule: 10 mutants, 10 killed** (an eleventh, the table's ends set by hand, was redundant and the
  line went; the table itself went with cut 2).
- **The synth: 26 mutants, 24 killed.** One more only by **AddressSanitizer**: the plain stretch that
  lands on the recording's end not stepped round (one read past the end). One survives, kept as a
  guard: the cut of a grain started in the piece its key ends in (it matters only to which slot a
  later grain in that same piece takes with the pool full).
- **AddressSanitizer** (a build of the engine and this test with `-fsanitize=address`, in a scratch
  folder: about four minutes): the whole test clean, `testSwap` included (the old recording freed);
  with `setSample`'s grain ending taken out it reports the read past the new, shorter recording.

### What batch 2 (the app) needs from here

- `TrackKind::Cloud` appended, its row last in `kInstruments` (word `cloud`, presets `.cloud`),
  `TrackData::cloud` a `CloudPatch` (room `cloud::kSettingRoom` 24, 17 used), `SynthKind::Cloud`.
- The recording: `holdsRecording(kind)` through the 26 places, and **`Engine::setTrackSample`** for a
  CLOUD track as for a sampler. None: send nothing (the engine plays the TONE).
- The spec `cl`: the settings' readings are `cloud_rule.hpp`'s (`sizeSeconds`, `densityHz`,
  `pitchSprayCents`, `scanTimes`, `levelDb`, `spraySeconds(dial, the recording's seconds)`,
  `envelope::attackSeconds` / `fallSeconds`, `stepsName`); the macros `cloud::macroSetting`; **no macro
  is a choice**. Engine presets: `Cloud::presetCount()` (3), `presetName`, `presetCategory`, `presetPatch`.
- The picture: **`Engine::grainPlaces(track, places, kGrainsShown)`** (shares 0..1 of the recording,
  as of the last chunk; -1 for no grains): a readout for the redraw, and an `idle_test` part.
- The pool is 24 (`cloud::kPool`): the plan's "32 grains" in its words goes with it.

## As built: batch 2, the app (2026-11-17, branch `cloud`)

**Built and tested on the desk; nobody has heard it, not run on the handheld.** CLAUDE.md's "CLOUD in
the app" block lists what is where. Where it differs from section 2 and 4:

| Plan | Built | Why |
| --- | --- | --- |
| SOURCE, GRAINS, PLAY: three rows of two | the generic screen's rows of four: SOURCE two rows (RECORDING and five), GRAINS and PLAY six | one screen for every synth |
| SPRAY read as +- seconds | **+-% of the recording** (`sprayShare`) | a setting's reading is given no recording; the picture's lit band shows the seconds |
| SCAN read as x-2.00 to x2.00 | "+1.00X", **FREEZE at the middle** | the device's lettering has no x sign |
| R1 RECORD on SOURCE | **R1 RECORD on the RECORDING stop only**; elsewhere R1 turns the page | R1 is the pages on every synth's screen |
| The picture's grains | teal ticks top and bottom, a redraw readout (`kStirGrains`) | as planned |
| A player's preset remembers its recording | **not yet**: batch 3, with the presets | |
| The WiFi page | the generic synth page serves it (no picture, no `sample NAME`) | batch 4 |

Tests: `app_test --cloud` (the track, the TONE, a recording kept, read, counted, renamed, deleted, the
song file's line, the screen's stops, the list, CATCH, RECORD with no input, every reading, the macros,
ROLL's 200 seeds in range, undo, a player's preset, a sampler and back; every stop's hints at 0 over;
the picture's brass line and ticks off the drawn screen), `idle_test`'s part, `textsize`, `scope_test`,
`save_test`, `modapp_test`, `presets_test` and `design_test` with CLOUD in their lists. **Left for
batches 3 to 5**: the 24 presets and a whole preset's recording (3), the designer page's picture,
POSITION mark and SPRAY band, `sample NAME`, ROLL (4), the review, mutants of the app side, README's
page and bench again (5).

## As built: batch 3, the presets (2026-11-17, branch `cloud`)

**24 presets in four folders of six; nobody has heard any of them.** STILL is the engine's own table
(`engine/src/cloud.cpp`: Soft Cloud, Glass Hold, Breath Pad, Frozen Shimmer, Still Water, Halo), so its
three engine presets of batch 1 (Dust, Fifths Choir) moved into the data's folders: a folder the data
adds after the engine's must have a name of its own, or the folder step finds two runs of it. DRIFT,
DUST and CHOIR are `app/presets/cloud.inc`. Made to differ, not to be one pad six ways: SCAN walking
forward, backward, stepped and swung; an LFO walking POSITION; DUST from specks of 6 ms to backwards
embers with their DENSITY flaring; CHOIR on fifths, octaves, an octave down, with SHIMMER, ENSEMBLE,
WEAR, ECHO or SPACE where a sound wants one. Six carry a MOD, eight an effect or a send.

`presets_test --heard-cloud` (ctest `presets_heard_cloud`, about 7 s): a phrase of five overlapping
keys at 120 BPM twice round, then Stop, through the TONE and through a recording the test makes (a sine
rising 200 to 800 Hz, then noise). Measured: loudest -28.2 to -15.4 dB on the TONE, -23.5 to -13.4 dB
on the recording; peak 0.55 at most; tails 0 to 4.7 s; every MOD moving and heard (the phrase without it
-2 to +4 dB apart: the rolls change with it). Held: -30 to -8 dB, peak under 0.9, a tail ending in 30 s,
the presets on one source within 14 dB, each MOD heard at -40 dB or more.

**A player's own preset keeps its recording** (`UserPreset::recording`, `sound NAME` after `whole`):
picked, it goes in by its name, else by its base name where exactly one has it, else the track keeps its
own and RECORDING NOT FOUND is said. `presets_test` `testACloudPresetBringsItsRecording`; its five
mutants killed. Not done: a pick's recording is not a step of undo (as the sampler's is not).

## As built: batch 4, the designer page (2026-11-17, branch `cloud`)

**The WiFi page's synth page knows CLOUD's recording.** A spec with `SynthSpec::recording` (CLOUD's
alone) adds to the synth's spec `"hear":60`, one `"waves"` entry and `"recordings"` (`Session::designSpec`):
the recording's 512 peaks, or the TONE's outline (`cloudToneOutline()`, moved to synth_spec.cpp so MAIN's
picture and the page share it) with `"tone":true`; `"band"` names the two settings the picture marks,
POSITION and SPRAY, whose values the page reads from the state as it does any setting. The list of
recordings is read on the app thread while the page looks, as for a sampler (`serveDesigner`).
- **The page draws the sampler's recording section** above the macros (`recordingSection`, the same
  `wave()`): with a band, the part SPRAY reaches about POSITION lit (round the ends, as the engine reads),
  POSITION a brass line, the two read under it; a click or a drag sets POSITION. The list's first choice
  reads THE BUILT-IN TONE (a sampler's NONE). `sample NAME` (only a name offered) and ROLL go through what
  the page already sent; ROLL keeps POSITION and the recording, as on the handheld.
- `design_test` `testCloudFromThePage`; its five mutants in `designSpec` and `serveDesigner`: four
  killed, one survives (`peaks.empty()` beside `missing`: a guard no state reaches, kept).
  `APOTHECA_DESIGN_CLOUD=1` with `APOTHECA_DESIGN_HOLD` holds the page open and prints the recording,
  POSITION and SPRAY whenever they change.
- **Tried in a browser** (Playwright and Chromium, 2026-11-17): the list offered THE BUILT-IN TONE and
  the test's recording, picking it put it on the track, a click at 30% of the picture set POSITION 30,
  ROLL changed SPRAY and kept POSITION, no error in the page. The page's script has no test in `ctest`.

## As built: batch 5, the review (2026-11-17, branch `cloud`)

The whole of CLOUD read again, engine and app. **The engine: nothing found to mend** (the piece and
onset order, the key and pool slots, the 32.32 reads and their stretches, the steal, the TONE, a new
recording ending every grain). Four things in the app were wrong and are mended:

| What | Before | Now | Held by |
| --- | --- | --- | --- |
| A folder's name typed in CLOUD's list of recordings | L2 / R2 turned the tab and MENU's help came up over the keys (`App::typingAFolder` knew the kit's and the sampler's lists only) | the list's, as theirs | `picker_test` (R2 alone: L2 then R2 had landed back on DEVICE and hid it) |
| Readings | SPRAY 1 to 3 read +-0.0% (as if off), PITCH SPRAY 2 read +-0 CENTS; 19 neighbouring SPRAY values, 11 SIZE, 6 DENSITY, 19 ATTACK and RELEASE and LEVEL 95 / 96 read alike | as many figures as it takes for every click to read another way (SPRAY 1 +-0.005%, SIZE 5.0 MS, DENSITY 1.00 HZ, ATTACK 2.1 MS, LEVEL -0.45 DB) | `app_test` `testCloudOnATrack` walks every setting |
| A CLOUD given another instrument | kept its recording's name, unseen, and it came back when the track went back to CLOUD (the song file had dropped it meanwhile) | an instrument with no recording lets it go (`setTrackKind`); a sampler and a CLOUD still pass theirs to each other | `app_test` |
| Nothing to mend, but untested | RECORDING's stops, the list's preview and rest, R1 and Y off RECORDING, MAIN's band round both ends and on the TONE, RECORD onto a CLOUD and what it says, a missing recording found by its base name, a rename followed in saved songs | tested | `app_test`, `sample_test` `testRecordingOntoACloud`, `transfer_test` |

**Mutants on the app side** (`synth_screen.cpp`, `session_samples.cpp`, `song_file.cpp`, `session.cpp`,
`redraw.cpp`, with batch 4's in `session_synth.cpp`): 15 of the first 15 in `synth_screen.cpp` survived
`app_test --cloud` before the tests above; after them all are killed but one, `+ 0.5f / count` in MAIN's
band, which lights the band's edge column: at SPRAY 0 that one column is always under POSITION's brass
line, so it cannot be seen there (kept). Every `holdsRecording` call site taken to the sampler alone is
killed (two only after `sample_test` and `transfer_test` gained their parts), and the grains' stir in the
redraw by `idle_test --quick`.

`bench` again (2026-11-17, two runs, load 4 to 6; Phazed 8 notes 0.9% in them): **one key 0.05%, the
pool full 0.54 to 0.58% (23.9 grains)**; `cloud_test --measure cost`: one key 0.054%, a 4-key chord
0.192%, the pool full 0.536%.

### The owner's three open questions, with what I would answer

1. **The pool is 24 grains, not the plan's 32.** Keep 24. The pool full costs 0.54 to 0.58% of a desk
   core against the plan's 0.7% ceiling; 32 would be about 0.75%, over it. The handheld is 13 to 17
   times slower, so 24 grains there are already 7 to 10% of a core. A cloud past 24 thins; it never
   clicks.
2. **R1 records only on RECORDING; elsewhere on SOURCE it turns the page.** Keep it. R1 turns the pages
   on every synth, and RECORDING is SOURCE's first stop, so RECORD is LEFT and R1 from anywhere on the
   page. (The sampler's R1 is RECORD everywhere, but the sampler has no pages.)
3. **A recording changed (picked, stepped, a preset's) is not a step of undo.** Keep it, as the
   sampler's and the kit's are not: B in the list puts back the one it opened on, and DOWN with A held
   steps back. Undo putting a recording back would read a file from the card inside undo, which on the
   handheld is the slow part.

Left: the handheld build and a run there (SPEED TEST); the owner's ears.
