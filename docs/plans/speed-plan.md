# Speed plan

Written 2026-11-15, after the owner ran SPEED TEST on the handheld. **Nothing
in the program is changed by this plan yet**: it is measurements and the
order of work. Branch `speed`, made from `main` at b390e7a.

**The busiest song stutters on the handheld, even with two helpers.** The
handheld is 13 to 20 times slower than the desk, not 5. This plan finds where
the time goes, and what can come back.

## The answer in short

| Question | Answer |
| --- | --- |
| Where does the busiest song's time go? | QMOD 27%, the five effects on every track 38%, Phazed 14%, drums 9%, JAX-EP 7%, buses 4%, the engine's own bookkeeping 2% (instructions, below) |
| Why is the handheld so much slower than 5x? | **Cache misses, most likely.** Its cores wait on memory and cannot work round it. With an L2 of 256 KB the busiest song misses it 5.8 million times a second; at about 100 ns each that is **up to half a core**. A quarter of those misses come from **bookkeeping that scans empty lists**; another seventh from **Phazed's two coefficient tables** (162 KB per track). **Not proven on the device**: the clock speed (its governor) is not known either (batch 0) |
| Why do twelve silent Phazed tracks cost 31%? | **Phazed never rests.** Its 44.1-to-48 kHz converter runs a 24-tap filter on every sample, silence or not, and calls the CZ engine once a sample. 80% of the silent cost is that |
| Why do eleven silent tracks of anything cost 6.8%? | **The step clock scans every synth track's 256 waiting-note slots and 128 keys three times a chunk**, 750 to 1000 chunks a second, whatever is in them |
| Why is undo 200 ms? | **It makes and copies whole 12 MB songs.** One undo plus redo builds four Projects, copies about 100 MB and touches 48 MB of fresh memory (12,251 page faults). The text it reads is 32 KB |
| Does a third helper help? | **Probably, about 20% off the block** for the busiest song (a model that matches today's two-helper figure, below). It must be timed on the device, with the screen's cost while playing, which nobody has measured |
| Flags? | **-O3 does nothing measurable** (clang, desk, within 2%). **g++ beat clang by 11 to 20%** on the desk for the synths and the busiest song: worth an A/B on the device. No `-ffast-math`: the program finds bad numbers with NaN tests it would remove |
| Biggest single wins, per effort | 1 the bookkeeping counts, 2 Phazed at rest and its tables shared, 3 the drum voice loop, 4 undo reads one track, 5 the heaviest track first to the helpers |

## 0. How it was measured

- **Desk**: 4 cores, shared with other jobs (load 4 to 5 all the while: timings move by 10%).
- **Instruction counts**: `valgrind --tool=callgrind` on a build made the
  handheld's way (**clang -O2**, plus `-g`), collecting only the rendering
  (`--toggle-collect`), one second of sound per run. Callgrind counts x86
  instructions, a fair stand-in for an in-order core like the A53 that cannot
  hide a slow instruction behind others.
- **Cache**: callgrind's cache model with the A53's L1 (32 KB) and an L2 of
  **256 KB** (the H616 family's; the H700's is not published) and of 512 KB.
- **A driver** outside the repo renders the bench's own setups
  (`sendBusiestSong`, and copies of `busSong`, `chordSong`, the twelve silent
  synths) with no clock, so callgrind can be pointed at them. Batch 0 puts this
  in `bench` as `--profile NAME`.

### The calibration: desk instructions to handheld time

| Row | x86 instructions a second of sound | Handheld (owner's SPEED TEST) | Instructions per handheld second |
| --- | --- | --- | --- |
| The busiest song, audio thread alone | 2,082 M | 221% of a core | 0.94 G |
| Twelve silent Phazed tracks | 353 M | 30.9% | 1.14 G |
| Twelve drum tracks | 215 M | 18.0% | 1.19 G |
| ...every hit thrown to both buses | 437 M | 45.6% | 0.96 G |
| Eleven silent synth tracks, plain chords | 68 M | 6.8% | 1.00 G |
| FOLD / PLATE / BLOOM alone | 52 / 34 / 99 M | 5.4 / 2.3 / 5.2% | 0.96 / 1.5 / 1.9 G |

**Rule of thumb: 1 G desk instructions is about one handheld core for one
second.** An effect alone runs nearer 1.5 G (its memory fits the cache); a whole
song nearer 0.95 G (it does not). Every "handheld %" below is worked out this
way and is an estimate until batch 0's rows say otherwise.

## 1. Where the time goes

### The busiest song (one second, the audio thread alone)

| Part | Instructions | Share | L2 read misses (256 KB) | Notes |
| --- | --- | --- | --- | --- |
| QMOD x2 | 560 M | **26.9%** | 0.31 M | the wire's edge loop and the constellation table, per operator per sample. **Being replaced by FM-6** |
| PLATE x7 (six tracks and SPACE) | 228 M | **10.9%** | 0.90 M (+0.40 M written) | the eight output taps per side, the smear allpasses |
| Phazed x2 | 286 M | **13.7%** | 0.80 M | the CZ chips 9%, the CZ's own resampler, the second converter 1.6%, the chorus |
| Drums x6 | 185 M | **8.9%** | 0.17 M | one branchy loop a voice a sample; the thrown copy doubles it |
| JAX-EP x2 | 152 M | 7.3% | 0.08 M | the DCOs in double, the filter |
| HALL x6 | 145 M | 7.0% | 0.50 M (+0.24 M) | |
| CHORUS x6 | 141 M | 6.8% | 0.16 M | four-point reads per voice per side |
| SATURATE x7 | 138 M | 6.6% | 0.02 M | the halfband stages are half of it |
| COMP x13 | 126 M | 6.1% | 0.02 M | `expf` and `logf` every sample: a quarter of it |
| Feeding the buses, running them (TAPE on ECHO) | 88 M | 4.2% | 0.03 M | per-sample glide tests even when nothing glides |
| The engine's bookkeeping (`render`, `firePendingNotes`, `firePending`) | 38 M | 1.8% | **1.34 M (23%)** | scans of empty slots, below |
| **Total** | **2,082 M** | 100% | **5.8 M** (3.0 M at 512 KB) | |

The new effects' shared guard, `soundOnly` (a test of every sample for
not-a-number), and the `Glide` steps are about **3%** of the song between them.

**At 512 KB of L2** the misses halve, and Phazed's tables (30% of what is
left) and the two reverbs (41%) are most of them.

### What the silent cases spend

| Case | Instructions a second | Where |
| --- | --- | --- |
| Eleven silent Phazed tracks (+ the drums) | 353 M | **the 44.1-to-48 kHz converter 53%**, the CZ called once a sample to say it is idle 18% and its chorus test 7%; the engine's scans 14% |
| Eleven silent samplers (the step clock row) | 68 M | **the engine's scans 72%** (`render`'s three loops over each track's 256 note slots and 128 keys, and `firePendingNotes`); multiplying silence by the level and finding its peak 21% |
| Eleven silent JAX-EP tracks | 69 M | the same as the samplers: JAX-EP itself rests (3%) |
| An empty song | (0.05% of a desk core) | |

### The bookkeeping, in detail

Every chunk (750 a second at the least, about 1,030 in the busiest song), for
every synth track, `Engine::render` walks `offIn[128]` twice and
`pendingNotes[256]` twice, `fireNoteOffs` walks `offIn` again and
`firePendingNotes` walks `pendingNotes` again. A `PendingNote` is about 48
bytes, so that is **13 KB a track a chunk read three times, nearly always
empty**. In the busiest song the instructions are small (1.8%) but the reads
push the reverbs' delay lines out of the cache: **`firePendingNotes` alone is
19% of the L2 misses** and `render` a third of the L1 misses. All of it is on
the audio thread before the tracks are shared out, so it is on the critical
path whatever the helpers do.

### The app: undo and the song's text

| Row (the bench's song: 12 tracks, 8 scenes, 32 KB of text) | Desk (g++ -O3) | Handheld | Where |
| --- | --- | --- | --- |
| The song as text | 2.1 ms | 16.5 ms | **`snprintf("%g")` for every number: 65%** |
| Text back into a song | 9.9 ms | 44 ms | **copying 12 MB Projects: 42%** (`new Project`, a blank `TrackData` copied into every track, then `project = *song`); `istringstream >> double` per line: 35% |
| Undo, then redo | 30.5 ms | 200 ms | each builds a `Project` in `restorePatterns` and another in `parseSong`, copies a blank track into all twelve, copies the whole result, compares 7.5 MB twice (`textNow`). **12,251 page faults** (48 MB of fresh memory) and about 100 MB moved per pair |
| ...a knob and a hit, undone | 23.6 ms | (not run) | the same |
| A frame that ends an edit | 1.2 ms | 12.3 ms | `textNow`: 7.5 MB compared, one track written again, the 32 KB joined |

**The text is not the cost; the 12 MB structure is.** A 32 KB song takes 200
ms to undo because each step builds and copies the whole `Project` (12 MB,
most of it empty scenes) several times over, into memory never touched before.

## 2. How the work is shared over the cores

How it works today (`Engine::renderChunk`): the audio thread lists the tracks
that have an instrument, wakes up to `workers` helpers through a condition
variable, and every thread takes the next track by index (`fetch_add`). The
audio thread spins (`yield`) until all are done, then alone: mixes in track
order, feeds the buses, runs the two bus effects, the master's two slots and
its level. Helpers sleep between chunks; with 1,030 chunks a second each is
woken 1,030 times a second.

| Finding | Number |
| --- | --- |
| The part only the audio thread can do (mix, buses, master, bookkeeping) | **9% of the song** (20% of a block on the handheld) |
| Tracks are taken in index order, so the heaviest come last in the busiest song (drums 0 to 5, then JAX-EP, QMOD, Phazed) | per-track cost with its three effects: drums 4.1 to 4.6%, JAX-EP 6.2 to 6.8, Phazed 9.4 to 10.0, **QMOD 16.0 to 16.6** of the song |
| A wake-up per chunk per helper | about 2.1 ms of work a chunk on the handheld; a wake-up of tens of microseconds is a few percent (not measured on the device) |

A model from those shares (the time of the slowest thread, plus the 9%):

| Threads | Order | Speed-up over the audio thread alone | Handheld block (221% alone) |
| --- | --- | --- | --- |
| 3 (2 helpers, today) | by index | 2.28x | 97% (**measured: 92.7%**, so the model holds) |
| 3 | heaviest first | 2.48x | 89% |
| 4 (3 helpers) | by index | 2.92x | 76% |
| 4 | heaviest first | **2.98x** | **74%** |

**A third helper is likely to help, about a fifth off the block.** The
heaviest track first takes a tenth off with two helpers and almost nothing
with three. Both are bit-exact (the mix stays in track order; `effects_test`
already holds helpers 1 to 3 to the bit). The 9% the audio thread does alone
then sets the floor: with three helpers it is a quarter of the block.

**What decides it is the screen.** With three helpers and the audio thread,
four realtime threads share four cores, and the screen's thread (a normal one)
gets what they leave. While the song plays the screen draws **every pass**
(`kStirPlaying` in `app/redraw.hpp`), and nobody has timed a pass on the
device. Sound would not suffer (the helpers outrank the screen), but the
screen might stutter. So: time a drawn pass while playing on the device (batch
0), and draw while playing only when something on the screen moved (the step,
the scope, a meter: the readouts already say so) rather than every pass.
`idle_test` already says PLAYING is covered by the others wherever it can show
it.

The variance matters more than the mean: 92.7% on average gave 360 late blocks
of 1875. Chunks where chords start, ratchets fire and voices are stolen cost
more. **A mean under about 60% is the target** for the busiest song.

## 3. Flags and number types

| Choice | What it would do on the A53 | Measured | Moves a pin? |
| --- | --- | --- | --- |
| **-O3** instead of -O2 (clang) | more unrolling; the A53 has little room for it | desk, best of 3 each: busiest 16.1 vs 16.3%, drums 1.60 vs 1.56, Phazed 0.98 vs 0.99, JAX-EP 0.72 vs 0.70 | no |
| **g++ instead of clang** (`aarch64-linux-gnu-g++` 13.3 is on the desk) | other code; **g++ was 11% faster on the busiest song and 18 to 20% on Phazed, JAX-EP and QMOD** (x86, desk) | x86 only. Needs checking that a g++ 13 build asks nothing of the device's glibc 2.31 it does not have (`objdump -T`) | the desk's pins are already g++; the device has none |
| **LTO** (`-flto=thin`) | Phazed calls the C engine once a sample across files; LTO can inline it | not tried | no |
| `-ffast-math` | **No.** It assumes no NaN: `soundOnly`, `feedBuses`' guard and the tests' `v != v` would be compiled away | | |
| `-fassociative-math -fno-signed-zeros` on chosen files | lets the compiler use NEON for sums of taps (the converters, PLATE, HALL, BLOOM) | | moves those effects' bits; better done in the code by hand (four running sums, as `cz_output.c` already does), so the RG505's compiler gets it too |
| float vs double | on the A53 a scalar double costs the same as a float; only NEON is twice as wide in float | | |
| Doubles in hot loops | the drum voice's place and rate, JAX-EP's DCO time, the waiting notes' times | | keep the double for the place; count an int inside a run (batch 2) |
| `-DNDEBUG` | the handheld build has none; **there is no `assert` in the engine or app** | | no |

**Flags are not where the time is**: try g++ and LTO in one A/B on the device
(batch 6), and expect a tenth at best.

## 4. The changes, ranked by gain for the effort

Handheld figures are estimates by the rule of thumb in section 0. "Pins":
`effects_test`'s 40 renders of the old five effects and its whole song
(`kSongSum`: drums, Phazed, QMOD, JAX-EP, the old five, the engine's mixing).

| # | Change | Where | Targets (measured) | Expected on the handheld | Risk | Tests and pins | `mods` branch |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **Count what waits**: a count and the highest slot used of each track's waiting notes and held keys (and drum hits); skip the scans when none, scan only to there | `engine.cpp`: `render()`'s two loops, `fireNoteOffs`, `firePendingNotes`, `firePending`; `Track` in `engine.hpp` | 72% of the silent-track cost; 1.8% of the busiest song's instructions but **23% of its L2 misses** | eleven silent tracks 6.8% to about 2%; the busiest song **5 to 15% of a core** off the audio thread's own part | low: order of firing kept (slots are still visited in order) | bit-exact: `effects_test --print` the same; `nextscene_test`, `chordfx_test`, `feel_test` | low: mods touches `apply`, `takeAway`, `runMods`, not these; `Track` gets fields beside mods' |
| 2 | **Phazed rests**: when the CZ is idle, its chorus quiet and the converter's history zeros, write zeros and only walk the converter's phase (and the CZ's gain glide, if it is moving) | `engine/src/phazed.cpp` `render` | 80% of a silent Phazed | twelve silent 30.9% to about 3% | low | bit-exact (silence is +0 both ways); `phazed_test` | none |
| 3 | **Share Phazed's tables**: one converter table and one pair of CZ coefficient tables for all tracks, made once | `rate_converter.hpp`, `third_party/easycz/dsp/cz_output.c` (+ a line in `third_party/CHANGES.md`) | 162 KB a track of tables read every 441 samples; 14% of the busiest song's L2 misses | a few % per Phazed track; more with several | low | bit-exact | none |
| 4 | **The drum voice loop in runs**: work out how many samples until the next thing changes (the end, the cut, a fade, a glide of level or THROW) and run a plain loop to there; at rate 1 and full rise and fade it is `left[i] += (s[i] * gain) * lane`, which the compiler can put in NEON. The thrown copy from the same loop | `engine/src/drums.cpp` `renderVoice` | 84% of twelve drum tracks; 8.9% of the busiest song | twelve drums 18% to about 6%; their THROW 27.6% to about 5% | medium: many cases (backwards, slices, ratchets, FEEL) | **can be kept to the bit** (multiplying by an exact 1 changes nothing); `throw_test`, `feel_test`, `kSongSum` as the proof | none |
| 5 | **Heaviest track first**: order the list the helpers take from by each track's time last chunk (or a weight by instrument and effects); mix still in track order | `engine.cpp` `renderChunk` | the model: 97% to 89% of a block | about 8% of the block with helpers | low | bit-exact by design | low |
| 6 | **Undo reads one track**: keep one scratch `TrackData` and one scratch head, made once; read the step's head lines and only the tracks whose text differs into them; never build a `Project` in undo | `app/session.cpp` (`readStep`, `restorePatterns`, `undo`, `redo`), `app/song_file.cpp` (a `parseTrack`) | 100 MB moved and 48 MB faulted per undo and redo | **200 ms to about 15 to 25 ms** (then `textNow`'s compares lead) | medium: undo has many tests | `app_test --undo`, `testUndoIsAsItWasReadWhole`, `nextscene_test`, `glideapp_test`, `modapp_test` | low: mods adds `modDepthShown` and a line in `setMod`, not the undo section |
| 7 | **`parseSong` in place**: no second `Project`, no `project = *song`; numbers read with `strtod` over the line, not an `istringstream` a line | `app/song_file.cpp` | 42% + 35% of a parse | opening a song 44 ms to about 10 ms | medium: "false and untouched if not a song" must hold (the header is checked first) | `song_test`, `save_test`, `app_test` | low (one line of mods there) |
| 8 | **A third helper**, if batch 0 says the screen can share | `app/realtime.hpp` `helpersFor`, and the screen's redraw while playing | the model: 89% to 74% | a fifth of the block | medium: the screen while playing | bit-exact | none |
| 9 | **The new effects' common work in blocks**: `soundOnly` finds the largest size in a block first and mends only if one is bad; a `Glide` at rest skips its step | `engine/src/fx/glide.hpp` | about 3% of the busiest song | 2 to 5% | low | bit-exact | none |
| 10 | **Phazed through one resampler**: the CZ's 40 kHz straight to 48 kHz (6 phases of 64 taps: 1.5 KB) instead of 40 to 44.1 (113 KB) then 44.1 to 48 (24 taps) | `cz_output.c`, `cz_synth.c` (its chorus and DC blocker at 48 kHz), `phazed.cpp` | the second converter 13% of a sounding Phazed, both tables most of its misses | **a sounding Phazed about a fifth to a third cheaper** | medium: **the sound changes** (slightly: a cleaner path) | moves `kSongSum` and `phazed_test`'s measured figures | none |
| 11 | **COMP without `expf`/`logf` a sample**: the level in dB on the TICK, or a fast exp2 | `engine/src/fx/comp.cpp` | a quarter of COMP; COMP is on every track in the busiest song | 1 to 2% of a core there | low; **the sound moves by a hair** | COMP's own tests; no pin | none |
| 12 | **Sums of taps in four running sums** (PLATE's outputs, HALL, BLOOM's 64 taps, the converter) | those files | PLATE 10.9%, BLOOM 99 M alone | 10 to 30% of each | low; **bits move** | their tests; no pin | none |
| 13 | **A silent track costs nothing**: an instrument says its chunk was exact zeros and every slot is empty or asleep; then no slots, no level loop, no peak, no mix | `engine.cpp` `renderTrack`, `renderChunk`; `instrument.hpp`, `effect.hpp` (an `asleep()`) | 21% of a silent track after #1 | the rest of the silent-track cost | medium: **the old five do not sleep**, so a slot holding one always runs | bit-exact if done so | low (mods' LEVEL and PAN moving loop is in this function) |
| 14 | **The song's text faster**: numbers written without `snprintf` where a small writer gives the same text (checked against `snprintf` for every value written, in a test). **Not `std::to_chars`: the handheld links libstdc++ 9, which has no float `to_chars`** | `app/song_file.cpp` | 65% of writing | 16.5 ms to about 6 ms; the frame that ends an edit with it | low | `save_test`, `everykind` | low |
| 15 | **Bigger chunks** when no MOD runs (64 to 256 frames) | `engine.hpp` `kMaxBlock`, `render()` | per-call costs in every effect, the scans, the wake-ups | a few % | medium: the old five may not be cut-exact | **moves `kSongSum`** probably | **high**: the MOD tick cuts chunks in the same code |

Not on the list, on purpose:

- **QMOD.** 27% of the busiest song, 56.9% of a handheld core for 8 notes.
  FM-6 replaces it. Nothing is spent on it.
- **The reverbs' memory** (PLATE and HALL: 41% of the misses at 512 KB). Their
  lines could be kept as 16-bit numbers, or shorter; both change the sound and
  the size of the cut is unknown. After batch 0 says how big the L2 is.
- **The CZ chips** (9% of the busiest song). Skipping a silent chip voice needs
  care with its phase. Measure first.

## 5. FM-6, in brief

QMOD costs 6 times JAX-EP for 8 notes against 6. FM-6 should be held to
JAX-EP's cost: **about 10% of a handheld core for 8 voices**. Two things to know
before choosing how msfa runs:

- msfa works in **fixed blocks of 64 samples**; the engine's chunks are cut
  where notes begin, so FM-6 needs its own 64-sample buffer it hands out from
  (as Phazed's converter does), or notes start late.
- msfa's fast inner loop is **32-bit ARM assembly**; on the handheld's 64-bit
  build it falls back to plain C++. Time the C++ path on the device before
  counting on the NEON one.

## 6. Order of work

Like the JX-3P plan: each batch ends with `ctest` green, `effects_test
--print` compared before and after (the same, unless the batch says which pin
moves), the bench's rows before and after on the desk, and **the owner's SPEED
TEST on the device** for the batches marked so.

| Batch | What | Pins | Device run | Done when |
| --- | --- | --- | --- | --- |
| **0** | **Measure what the desk cannot.** SPEED TEST prints: the CPU governor and clock before and after each row (`/sys/devices/system/cpu/cpu0/cpufreq/`); the L2's size (`.../cache/index2/size`); what SDL gave the sound card (`have`, opened with any change allowed and logged, to see whether SDL converts the rate on the audio thread); **a row with three helpers**; a row with the heaviest track first; **a drawn pass of the pattern screen while playing** (the real renderer). `bench --profile NAME` renders a setup with no clock, for callgrind | none | **yes** | the owner's figures are in this file |
| **1** | **Work done for nothing**: #1 (counts), #2 (Phazed rests), #3 (shared tables), #9 (soundOnly and Glide in blocks) | none: bit-exact | yes | eleven silent tracks under 2.5%, twelve silent Phazed under 4% on the device |
| **2** | **Drums in runs**: #4 | none: bit-exact (the whole song is the proof) | yes | twelve drum tracks under 8%, their THROW under 8% |
| **3** | **Sharing**: #5 (heaviest first); #8 (a third helper) if batch 0 allows; the screen draws while playing only when the readouts moved | none: bit-exact | yes | the busiest song: no late block in 30 s with its helpers |
| **4** | **Undo and the text**: #6, #7, #14 | none (app) | yes | undo then redo under 30 ms, a song read under 15 ms on the device |
| **5** | **Speed that changes the sound a little** (the owner's ear): #10 (Phazed one resampler), #11 (COMP), #12 (the tap sums) | **`kSongSum` (Phazed)**, `phazed_test`'s figures, the measured comments of the effects touched | yes | each named, measured and said |
| **6** | **Flags**: g++ 13 against clang, and thin LTO, as one A/B on the device | none on the desk | yes | the faster kept, or both dropped |
| **7** | **Later**: #13 (a silent track costs nothing), #15 (bigger chunks, after `mods` joins), the reverbs' memory, a 512-frame sound buffer (10.7 ms instead of 5.3: **the owner's call**, it is latency) | #15 moves `kSongSum` | yes | |

Batches 1, 2 and 5 touch no file the `mods` branch touches but `engine.cpp`
and `engine.hpp`, in other functions. Batch 0 touches `bench_run.cpp`, which
`mods` changes too: put the new rows in their own functions, or wait for the
join. Batch 4 touches `session.cpp` far from `mods`' lines.

**What the busiest song might come to**, if the estimates hold: batches 1, 2
and 5 take about a quarter of its instructions and many of its misses; FM-6 at
half QMOD's cost another eighth. Alone it would be about 150% of a handheld
core instead of 221%, and with three helpers, heaviest first, **about 50% of a
block**. That is a guess built on guesses; batch 0's rows are the first check.

## 7. What was not measured

- **Anything on the device.** Every handheld figure here is the owner's SPEED
  TEST or an estimate from desk instruction counts.
- **The device's clock.** If its governor lets the cores sit below 1.5 GHz
  during SPEED TEST, part of the 13 to 20 times is that, not the code.
- **The L2's size** and how slow a miss is. The miss counts are a model's.
- **The screen's cost while playing**, and so whether a third helper starves it.
- **What SDL does after the engine**: the program asks for 48 kHz float and
  lets SDL convert to whatever the card takes, on the audio thread, outside
  every SPEED TEST row.
- **The wake-up cost of a helper** on the device (a futex and a switch, per
  chunk).
- **The helpers on the desk** gave nothing useful: the desk was shared (load
  4.6), and 0 to 3 helpers read 14.4 to 18.6% in no order.
- **The bigger song**: the bench's song is 32 KB of text. The 2.5 MB song the
  undo notes speak of was not built; its text and parse times scale with it,
  the 12 MB copies do not.

## 8. As built

Desk figures; the desk was shared (load 4 to 6) all the while, so a timed row
moves by a tenth between runs. **Instructions** are callgrind's, on a build made
the handheld's way (clang -O2 -g), one second of sound, the rendering only
(`bench --profile NAME 1`, `--toggle-collect='*profiledRender*'`): they do not
move with the load.

### Batch 0: what the device does (2026-11-15)

SPEED TEST now prints, besides its old rows:

| Row | Where | On the desk |
| --- | --- | --- |
| **The processor, before and after the rows**: each core's governor, its clock now, where the governor holds it, how far it can go (`/sys/devices/system/cpu/cpu*/cpufreq/`) | `processorRows` | nothing told: the desk has no `cpufreq` |
| **May this program set the governor?** `scaling_governor` opened for writing and shut, nothing written | `processorRows` | no (no such file) |
| **The caches** as cpu0 sees them, and which cores share each | `processorRows` | L1 data 48K, L1 code 64K, L2 2048K, L3 shared |
| **The clocks during each real-time row**: every core read ten times a second on an ordinary thread, lowest and highest | `ClockWatch` | nothing told |
| **The sound card**: the default output opened once with any change allowed, said, shut. The program does it at start, before it opens the card for its sound, prints it (`sound card: ...`) and hands it to the SPEED TEST in `APOTHECA_SOUND_CARD`, since the test cannot open a card the program holds. Run alone (`apotheca --bench`), the bench looks for itself | `probeSoundCard`, `main.cpp` | could not be opened (no card) |
| **The busiest song with 3 helpers**, and **heaviest first** with 2 and 3, in real time | `sharingRows`, `Engine::setHeaviestFirst` | below |
| **A screen pass while the song plays**: the app's update and its drawing, timed apart, on SONG and the drum grid. SDL's software renderer into memory (the test cannot draw on the program's screen), so it is the drawing's work, not the handheld's renderer or its present | `screenRows` | update 0.05 to 0.06 ms, drawing 1.6 to 1.7 ms |
| `bench --profile NAME [SECONDS]`: busiest, busiest-mods, drums, drums-thrown, chords, phazed-silent, jaxep-silent, fm6-silent (qmod-silent until FM-6 took its place), empty | `runProfile` | the instruction rows below |

**Heaviest first** is in the engine, off unless asked for (only the SPEED
TEST asks): the helpers take the tracks from `taken_`, sorted by each one's
time the chunk before (two clock reads a track a chunk, only then); the mix
still walks `active_`, in track order. `speed_test` (`testHeaviestFirst`)
holds 2 and 3 helpers, in order and heaviest first, to the audio thread alone,
to the bit; mixing in the order taken fails it.

The busiest song, one run on the shared desk (the same run):

| Threads | % of a block |
| --- | --- |
| the audio thread alone | 17.8 |
| 2 helpers | 12.0 |
| 3 helpers | 13.3 |
| 2 helpers, heaviest first | 11.4 |
| 3 helpers, heaviest first | 11.1 |

The desk tells little here (section 7: its helpers read in no order). **The
device's run is what decides batch 3.**

The SPEED TEST grew by three real-time rows of 10 s and the screen rows (a
second or two): about 35 s more on the handheld, about four minutes in all.
The page says so.

Instructions, one second of sound, before batch 1 (the plan's own figures in
brackets):

| Setup | Instructions |
| --- | --- |
| busiest | 2,070 M (2,082 M) |
| phazed-silent (twelve silent Phazed tracks) | 351 M (353 M) |
| chords (eleven silent samplers, plain chords) | 67.5 M (68 M) |
| jaxep-silent | 68.5 M (69 M) |
| drums (twelve drum tracks) | 214 M (215 M) |
| drums-thrown | 435 M (437 M) |
| empty | 4.3 M |

### Batch 1: the work done for nothing (2026-11-15)

**Bit-exact**: `effects_test --print` the same before and after, the pinned
checksums untouched, `ctest` green (52).

| Change | Where | Held by |
| --- | --- | --- |
| **#1 The step clock walks only what waits.** Each track keeps a bit for each waiting-note place (256), each key (128) and each drum hit's place (24) that may hold something (`PlaceBits`): set where a place is filled, taken off by the walk that finds it empty. `render()`'s two walks, `fireNoteOffs`, `firePendingNotes`, `firePending` and `fireStep`'s marks walk the bits, lowest first: the order a walk of every place found them in | `engine.hpp` (`PlaceBits`, `Track::notesWaiting`, `keysHeld`, `hitsWaiting`), `engine.cpp` | `speed_test` `testStepClockPlaces`: eleven listening synths and a drum track (chords plain, strummed, arpeggios, humanised, ratcheted; nudges both ways; glides of every length, PAIRED and RANDOM; drum ratchets, nudges, FEEL; tempo changes, Stop, Play of a scene alone; blocks of 1 to 300) beside an engine that walks every place (`gWalkEveryPlace`): the same notes on the same frames, the same sound. `testBusiestWalksAlike`: the busiest song both ways |
| **#2 Phazed rests.** When nothing sounds, the CZ's output has settled, its chorus has been fed silence for longer than it remembers and the converter's history is zeros, a block is written as zeros and only the CZ's counts and its volume's glide move (`cz_synth_rest`), and the converter's place (`RateConverter::skip`) | `phazed.cpp`, `rate_converter.hpp`, `cz_synth.c` | `testPhazedRests` (a resting Phazed beside one that never rests, `setRestingForTests`: notes, chords, silences, the volume and a macro turned at rest, another preset, a glide that wakes it while the volume glides; at 48 kHz and at the CZ's own 44.1, blocks of 1 to 300; it rests 21.4 s of 26), `testPhazedRestsInTheEngine` (twelve tracks), `testConverterSkips` |
| **#3 Phazed's tables shared.** The CZ's two filter tables (113 KB each) and the 44.1-to-48 kHz converter's (49 KB) are made once (`resampler_design`, `cz_synth_init_shared`; `RateConverter::tableFor`, one for each pair of rates) | `cz_output.c`, `cz_synth.c` (`third_party/CHANGES.md`), `phazed.cpp`, `rate_converter.hpp` | `testPhazedTablesShared` (two synths read the same; the table is what `resampler_init` makes for itself); `effects_test`'s pinned song (a table made the other way fails it) |
| **#9 The effects' way in looks first.** `soundOnly` sweeps a block for a bad sample in one pass the compiler can do several at a time, and mends only if it found one. The plan's other half, a `Glide` at rest skipping its step, **is not done**: it is in each effect's own loop (nineteen files), for about 1% | `fx/glide.hpp` | `testSoundOnly`: every place in blocks of 1 to 70, each kind of bad sample, beside the old way, to the bit |

**Mutation tests** (each change broken, the tests run): **every mutant that
changes what the change does was killed.** The bits not set in each of the four
places that fill one, a bit taken off a note, key or ratchet still waiting, the
last word of bits not walked, a bit set in the wrong word; the rest's volume
glide left out or stopped a step early, resting without asking the chorus or
whether it is idle, the converter's place not moved, one pull too few, at 44.1
kHz half the frames rested, `silent()` not looking at either side; `soundOnly`
never mending or missing the last sample; the shared filter on the wrong
design. **Survivors, each equivalent** (no sound can show them): the rest's
coupling capacitor zeroed (the idle render had zeroed it already, and resting
needs 512 idle frames first); the chorus's count not kept (it only has to stay
over 512 while at rest); the converter's history not turned (all zeros: turning
them changes nothing); Phazed asking `silent()` at all (a resting CZ has fed the
converter 512 zeros, so it is always silent then: kept as a guard);
`clearPending` keeping its bits (a bit on an empty place costs a look, nothing
more). Two mutants of the walks' own bodies (a chunk not cut where a key is let
go, keys not counted down) survive `speed_test`, which compares the two ways of
walking, and are killed by `chordfx_test`.

**Instructions, one second of sound** (callgrind, clang -O2 -g):

| Setup | Batch 0 | Batch 1 | |
| --- | --- | --- | --- |
| busiest | 2,070 M | 2,023 M | -2.3% |
| phazed-silent (twelve silent Phazed tracks) | 351 M | 39.0 M | **-89%** |
| chords (eleven silent samplers, plain chords) | 67.5 M | 22.1 M | **-67%** |
| jaxep-silent | 68.5 M | 22.0 M | **-68%** |
| drums (twelve drum tracks) | 214 M | 212 M | -1% |
| drums-thrown | 435 M | 432 M | -1% |
| empty | 4.3 M | 4.1 M | |

**Cache, the busiest song** (callgrind's model: L1 32 KB, L2 256 KB):

| | Batch 0 | Batch 1 |
| --- | --- | --- |
| L1 data read misses | 9.13 M | 4.64 M (**-49%**) |
| L2 read misses (code and data) | 5.61 M | 3.60 M (**-36%**) |
| L2 write misses | 0.84 M | 0.76 M |
| twelve silent Phazed: L2 read misses | 3.92 M | 0.0005 M |

By section 0's rule of thumb (a miss about 100 ns on the device), 2 M fewer
misses a second is up to **a fifth of a handheld core** off the busiest song, on
the audio thread's own part as much as the helpers'. **Not measured on the
device.**

**Timed, one run on the shared desk** (before: the batch 0 run):

| Row | Before | After |
| --- | --- | --- |
| twelve silent Phazed tracks | 2.38% | **0.32%** |
| twelve silent QMOD / JAX-EP tracks | 0.70 / 0.71% | **0.15 / 0.15%** |
| eleven silent tracks, plain chords / strummed x4 / arpeggios | 0.72 / 1.19 / 1.15% | **0.15 / 0.24 / 0.21%** |
| the busiest song alone / 2 helpers | 17.8 / 12.0% | 15.7 / 11.7% |
| twelve drum tracks | 1.48% | 1.47% |

The plan's targets for the device (eleven silent tracks under 2.5%, twelve
silent Phazed under 4%): by section 0's rule (1 G instructions a handheld
core-second), about 2.2% and 3.9%, just inside both. **The device's SPEED TEST
says.**

### Batch 2: the drum voice loop in runs (2026-11-15)

**Bit-exact**: `effects_test --print` the same, the pins untouched.

`DrumMachine::renderVoice` looks, before each sample, for a **run**: the
samples from here on where nothing about the voice moves but its place
(`plainRun`): forward at speed 1, its level not gliding, not rising, not
fading, its THROW not sliding, and up to the first sample where something
would change (the sound's end, its part's end where the fade begins, the sample
its cut falls on). A run is one plain loop, `left[i] += s[i] * gain * fade *
rise * laneLevel`, the long way's sum word for word with what does not move
read once, so the compiler can do several samples at a time and the bits are
the same; the thrown copy the same way. Every other sample (pitched, backwards,
rising, fading, a level or a THROW gliding) goes the long way, one at a time,
as before. A double place plus a whole number is exact, so a run's places are
the long way's.

Held by `speed_test`: `testDrumRuns` (two machines on the built-in kit, one the
long way (`setRunsForTests`), 30 s in blocks of 1 to 300: hits plain, pitched
both ways, backwards, part-way in, parts, cuts, a lane's own short recording,
slices of the crash carried on at another level and another THROW, lane levels
and parts set, auditions let go, the buses' ways out given or not: the same
bits on both sides and both buses, the same buses written) and
`testDrumRunsInTheSong` (the busiest song both ways).

**Mutation tests**: killed, each: the run's gain or lane level left out, the
bus not cleared before the run first writes it, the run one sample past the
cut, past the part's end, one sample too long, a run taken while the level
glides, while it rises, while it fades, while pitched, while the bus share
glides, the cut not counted down in a run, the place a sample short. **One
survivor, equivalent**: the bus share left out of the run's thrown copy (in a
run it is exactly 1: 0 is skipped, and between is a slide, which is no run).

**Instructions, one second of sound** (callgrind, clang -O2 -g):

| Setup | Batch 1 | Batch 2 | |
| --- | --- | --- | --- |
| drums (twelve drum tracks) | 212 M | 53.0 M | **-75%** |
| drums-thrown | 432 M | 143 M | **-67%** |
| busiest | 2,023 M | 1,877 M | -7% |

From the plan's start (batch 0): **the busiest song 2,070 to 1,877 M (-9%)**,
its L2 read misses about a third fewer (batch 1).

**Timed, one run, load 7 to 9** (before: the batch 1 run):

| Row | Before | After |
| --- | --- | --- |
| twelve drum tracks | 1.47% | **0.43%** |
| ...every hit thrown to both buses, added | +1.18% | **+0.31%** |
| ...thrown, TAPE, HALL, 12 DUCKs, added | +1.68% | +0.81% |
| the busiest song alone / 2 helpers / 3 helpers | 15.7 / 11.7 / 11.3% | 15.5 / 10.8 / 9.9% |

The plan's targets for the device (twelve drum tracks under 8%, their THROW
under 8%): by section 0's rule, 53 M and 143 M are about 5% and 14% of a
handheld core for the drums with every hit thrown (the 8% target for THROW is
its added cost: 90 M, about 9%). **Close; the device's SPEED TEST says.**

### Joined with main 0.565 (FM-6 in QMOD's place, 2026-11-16)

The figures above are the busiest song with two QMOD tracks. On main 0.565 the
two are FM-6 (`bench --profile`, the same build way):

| Setup | main 0.565 | with batches 0 to 2 | |
| --- | --- | --- | --- |
| busiest | 1,617 M | 1,424 M | **-12%** |
| fm6-silent (twelve silent FM-6 tracks) | 69.1 M | 22.6 M | **-67%** |

So from the plan's start the busiest song went **2,070 to 1,424 M (-31%)**: FM-6
for QMOD and these three batches. `effects_test --print` on the join is the same
as on main 0.565 (its pins were remade there for FM-6), and `ctest` passed: 44
of 52 in one run at a load of 20 (the other eight timed out, `themeviews`
failed at the place it fails under load) and those eight one at a time after.

### What is left

- Batch 1's other half of #9, a `Glide` at rest skipping its step: in nineteen
  effects' own loops, about 1% of the busiest song. Not done.
- The device's figures: **none of this has run on the handheld.** The SPEED
  TEST of a build with these batches is the next thing (batch 0's rows say the
  governor, the clocks, the L2 and the sound card; then batch 3 can be chosen).
