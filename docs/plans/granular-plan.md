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
