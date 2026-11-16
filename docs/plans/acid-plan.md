# ACID plan

Written 2026-11-16 from the code at `main` 03eb219 (branch `acidplan`).
**Groundwork only: nothing is built.** The code was only read. No figure here
was measured; every cost is a guess from JAX-EP's and PHYSIC's rows.

The owner asked (queued 2026-11-14, started 2026-11-16): "an acid mono bass
(303-style: accent, slide)". **The owner answered the open calls the same day:
"go with your picks"** (relayed by the coordinator), so section 7 is settled and
batch 0 can start from it.

## The recommendation

**One new mono synth, ACID: one oscillator, one resonant low-pass, two
decays, ACCENT from the note's velocity and SLIDE from the note's existing
GLIDE.** No new data on a note, no new song-file word on a note, no new
gesture. The roll, its glide lines, `glide_rule.hpp` and GLIDE FROM serve it
as they are.

| Question | Answer |
| --- | --- |
| What it is | **A one-voice bass in the style of the 1981 bass line box**: SAW or SQUARE, a 4-pole diode-flavoured low-pass, a filter decay and an amp decay, ENV MOD, ACCENT, SLIDE |
| Name on the device | **ACID** (short name `ACID`, song word `acid`, presets `NAME.acid`). Alternatives: SQUELCH, LINE BASS |
| The `says` line | **"mono bass, after a classic bass line box"** (39 letters; no maker or model) |
| ACCENT | **A note at velocity 0.95 or over** (`kAccentAt`). A note placed is 0.9 (`kPlacedLevel`), so it is plain; **one click of hold A + RIGHT makes it 100%, an accent** |
| SLIDE | **The note's GLIDE.** A gliding note slides in pitch from the key GLIDE FROM names and does **not** start the envelopes again. Its time is the glide's length (half a step by default: 62.5 ms at 120 BPM, about the original's) |
| Voices | **One**, newest key wins, plus one tail slot that fades a let-go note out over 3 ms |
| Settings | **14** (room for 20), 8 macros, MAIN + 2 pages |
| Cost target | **≤ 0.15% of a desk core** for the voice playing a busy line (handheld about 2 to 2.5%, at 13 to 17 times) |
| Engine changes | **None outside the new synth.** `noteOnFrom` already hands over the key and the seconds |
| First thing to build | **Batch 0**: `acid_rule.hpp` (the rules as numbers, no sound) and its test |

**The owner cannot hear.** Every claim about the sound is a test, marked
**FAITHFUL** (checked against a named source) or **SELF** (this plan's own
guess). Almost everything about the original's exact numbers is SELF until
recordings are measured (batch 6, the owner's source, as JAX-EP's batch 9).

## 1. What it is

### The original, known and guessed

Sources, **none opened for this plan** (from memory; batch 0 opens each or
marks the claim SELF):

| Short | Source |
| --- | --- |
| **SN** | The bass line box's service notes (1982): the schematic |
| **RW** | Robin Whittle, the "Devil Fish" modification notes (firstpr.com.au): the envelopes, accent, the accent sweep, slide, what each part does |
| **TS** | Tim Stinchcombe, analyses of diode ladder filters (timstinchcombe.co.uk) |
| **VZ** | Vadim Zavalishin, *The Art of VA Filter Design* (rev. 2.1, 2018): zero-delay-feedback ladders, the diode ladder's coupled stages |
| **VH** | Välimäki & Huovilainen, "Oscillator and filter algorithms for virtual analog synthesis", *CMJ* 30(2), 2006: polyBLEP |

| Part | What is known | What is guessed |
| --- | --- | --- |
| Front panel | TUNING, CUTOFF FREQ, RESONANCE, ENV MOD, DECAY, ACCENT, WAVEFORM (saw / square) (**SN**) | |
| Oscillator | One VCO, a saw; the square is made from the saw by a one-transistor shaper (**SN**) | The square's shape: **a pulse near 50% with a sagging top** (SELF) |
| Filter | A 4-pole **diode ladder**, not a transistor ladder: the stages load each other, so the poles are not equal (**TS**, **VZ**) | **The slope reads near 18 dB/oct** above the cutoff (often said; SELF). How far it self-oscillates at full RESONANCE: **weakly, unit to unit** (SELF). The bass it loses as RESONANCE rises (a coupling capacitor in the loop; SELF) |
| Filter envelope | Decay only. DECAY sets about **200 ms to 2 s**. **On an accent DECAY is ignored and the shortest is used** (**RW**) | The exact ends (SELF) |
| Amp envelope | Fixed decay, cut by the gate (**RW**) | Its length: **about 3 s** (SELF) |
| Accent | **Louder, brighter, faster**: more VCA, the filter envelope's shortest decay, and the envelope added to the cutoff through **the accent sweep**: a capacitor that charges on each accent and leaks between, so **accents close together climb higher**. The RESONANCE pot is ganged into that circuit: more resonance, a slower, bigger sweep (**RW**) | The sweep's time and how far it climbs (SELF, 30 to 100 ms) |
| Slide | The step's gate is held into the next step and the pitch CV goes through an RC: **constant time, not constant rate; no envelope starts again** (**RW**) | The time: **about 60 ms** (folklore; SELF until measured) |

### ACID, the settings

Ids are appended, never reordered. The patch is sized for 20.

| Id | Setting | Range, reads as | Usual |
| --- | --- | --- | --- |
| 0 | WAVE | SAW, SQUARE (a choice) | SAW |
| 1 | OCTAVE | -2, -1, 0, +1 (a choice) | 0 |
| 2 | TUNE | ±100 CENTS | 0 |
| 3 | CUTOFF | 0..100, reads HZ | 30 |
| 4 | RESONANCE | 0..100 | 60 |
| 5 | ENV MOD | 0..100 | 50 |
| 6 | DECAY | 0..100, reads MS (200 MS to 2.0 S) | 40 |
| 7 | ACCENT | 0..100 | 50 |
| 8 | SWEEP | 0..100: how far accents close together climb (50 = the original, guessed) | 50 |
| 9 | DRIVE | 0..100: into the filter's soft clip (0 clean) | 0 |
| 10 | AMP DECAY | 0..100, reads S (0.2 S to 6 S; usual the original's guess, 3 S) | 70 |
| 11 | SLIDE | NOTE (the glide's length), FIXED (60 MS) (a choice) | NOTE |
| 12 | VELOCITY | OFF, ON: a plain note's level follows its velocity (a choice) | OFF |
| 13 | LEVEL | DB | 0 DB |

**ACCENT 0 is no accent at all**: an accented note is then a plain one, to the bit.

## 2. How it fits Apotheca's data

### ACCENT is velocity, not a new flag

| | Velocity ≥ 0.95 (**taken**) | A new flag on a note |
| --- | --- | --- |
| Song file | nothing new | a new word or number, `notefx` again |
| Roll gesture | **hold A + RIGHT, one click** (0.9 to 1.0) | a new one, and X's buttons are all taken (L/R ratchet, U/D glide, A THROW, Y dice, L1/R1 GLIDE FROM) |
| Undo, scene move, `sceneIn` | nothing new | every one of them |
| A keyboard | a hard hit (121 of 127) accents | nothing |
| The brush (R1 paint) | painting at 100% paints accents | nothing |

Velocity under 0.95: **the same level for every plain note** (the original has
no velocity), unless VELOCITY is ON. A chord's HUMAN LEVEL only lowers, so it
can take an accent away; that is as it reads.

The roll on an ACID track marks a note at 100% (batch 3, a small brass mark),
so an accent can be seen. **No other gesture.**

### SLIDE is the note's GLIDE

The engine already does the work (`engine.cpp`, `startNote`): for a gliding
note it asks `GlideTrail::from` for the key (by the track's GLIDE FROM) and
calls `noteOnFrom(note, velocity, from, seconds)`, `seconds` from
`glideSteps`. ACID reads it so:

| Case | ACID does |
| --- | --- |
| A note with no GLIDE | **Starts again**: both envelopes from the top, a new accent pulse if accented. Overlap does not matter (notes placed end to end may meet on one sample, in either order) |
| A note with GLIDE, the gate open or closed **under 5 ms ago** (`kTieMs`: notes end to end) | **A slide**: the pitch moves from `from` to the note, **no envelope starts again**, the gate stays open. An accent on it changes the VCA's level (gliding over 3 ms), no new sweep pulse (SELF) |
| A note with GLIDE after a rest | The pitch slides, **the envelopes start again** (there is nothing to tie to) |
| Slide time | **SLIDE NOTE**: the glide's `seconds`, so the roll's line is the truth. **FIXED**: 60 ms whatever the note says |
| Slide shape | **An RC**, a one-pole on the pitch in octaves, τ = time / 3, **landing exactly** on the note when the time is up (CLAUDE.md: a smoother lands where a float still moves) |
| A ratchet | Each piece starts again (only the first glides, as `glideSteps` says) |
| A chord | Its keys come in order; **the last to start sounds** (UP: the top) |
| `noteOff` of a key not sounding | Ignored |
| `noteOff` of the key sounding | The gate closes (3 ms); the filter envelope runs on |

**Newest key wins; letting it go does not go back to a key still held.** In a
pattern a long note under a short one would otherwise jump back.

`allNotesOff` hands the sounding note to the tail slot (3 ms fade) and the voice
starts fresh, **the accent sweep at zero**, so Play from the top is the same
line (JAX-EP's rule). `stopNotes` stays the default.

## 3. Macros, pages, MODs

| Lane | Macro | MOD target |
| --- | --- | --- |
| 0 | CUTOFF | yes |
| 1 | RESONANCE | yes |
| 2 | ENV MOD | yes |
| 3 | DECAY | yes |
| 4 | ACCENT | yes |
| 5 | DRIVE | yes |
| 6 | SWEEP | yes |
| 7 | TUNE | yes (a STEPPER on it is a pitch line) |

**No macro is a choice**, so `macroIsChoice` has no ACID entry and every macro
takes a MOD. 0.5 = as the patch says (the house rule).

| Page | Holds |
| --- | --- |
| MAIN | The eight macros, PRESET |
| SOUND | WAVE, OCTAVE, TUNE, LEVEL, VELOCITY, SLIDE |
| FILTER | CUTOFF, RESONANCE, ENV MOD, DECAY, ACCENT, SWEEP, DRIVE, AMP DECAY |

SOUND is three rows of two, FILTER four rows of two (FM-6 holds six rows, so
both fit; `textsize` checks them at XL). The WiFi designer page comes with the `SynthSpec`, nothing of its own. No ROLL
(Y on MAIN does nothing, as on JAX-EP).

## 4. DSP and cost

| Part | Design | Why |
| --- | --- | --- |
| Oscillator | **polyBLEP** saw (**VH**); SQUARE a polyBLEP pulse at 50% with a one-pole sag on its top (SELF) | Bass pitches alias little; two samples of correction a jump. **If `asideOf` finds aliasing over -60 dB under 10 kHz at key 72**, use JAX-EP's minimum-phase table (`jx3p/blep_table.hpp`, read only) and carry its lateness on the saw's slope as `kBlampCarry` does |
| Filter | **Four trapezoidal one-poles solved as one sum** (zero delay feedback, **VZ**), the stages coupled as a diode ladder (each feeds back into the one before), the first stage's capacitor set apart (SELF, for the 18 dB look) | JAX-EP's `ir3109.hpp` is the pattern |
| Its bend | A soft clip (no flat top) **in the feedback only**, taken as a gain **read ahead from the last three samples** (JAX-EP's `VcfCoef::turn`) | **The JAX-EP lesson**: taken from the sample before, the gain was a sample late and the filter sang 30 cents sharp, 3 dB loud, and buzzed |
| Low end | A one-pole high-pass in the loop (SELF corner), so RESONANCE thins the bass as the original does; how much is the owner's ear later | |
| DRIVE | Gain into the input, the same soft clip | Heavy dirt is the FX's: DRIVE, SATURATE, FUZZ |
| Coefficients | Worked out **only on a sample the cutoff moved**, from polynomials (`exp2Poly`, no `tan`, no division by a constant) | JAX-EP took 2.1% to 1.6% that way |
| Envelopes | One-pole decays, sample by sample, counted from the strike | |
| Accent sweep | A one-pole "capacitor": each accent's filter envelope charges it; it leaks with τ set by SWEEP and RESONANCE; added to the cutoff in octaves × ACCENT | **RW**'s circuit as a sum, values SELF |
| Oversampling | **None at first.** If DRIVE 100 + RESONANCE 100 aliases over the bound, 2x through `fx/halfband.hpp` | Costs double; only if the test says so |

**House rules it keeps:**

- **Tiny numbers**: every state snaps under 1e-20; the voice sleeps (exact
  zeros, no work) after the gate is shut and 64 samples under -120 dB **and the
  accent capacitor is under its quiet**, so a note after a sleep is a fresh
  voice's to the bit.
- **Blocks**: everything is per sample or on a count from the strike; the same
  sound in blocks of 1, 7, 64, 256. `setMacro` stores and returns.
- **Rolls**: ACID rolls nothing. If a drift is ever added it is a hash of
  `noteSeed`, never a generator walked.
- **NaN**: a velocity or a macro that is not a number is held, never a stuck note.
- **Clip**: no preset as written clips (`samplesClipped()` counted, as JAX-EP's).

**Cost.** Guess: polyBLEP ~5 ns a sample, the filter with its read-ahead ~25 ns,
the rest ~5 ns: 25 to 35 ns × 48 000 = **0.12 to 0.17% of a desk core** while
a note sounds, near nothing asleep. That is at the target, not under it: batch 1
measures before anything else is added. **Target ≤ 0.15%; ceiling 0.25%.** The handheld
is 13 to 17 times the desk: **about 2 to 2.5% there**, against JAX-EP's 6
notes at about 9.5%. A `bench` row: one ACID track, a 16-step line with
accents and slides at 130 BPM.

## 5. Presets

**32 in five folders**, as data in `app/presets/acid.inc`
(`docs/plans/presets-plan.md`). Preset 0 is the one a new track gets.
Names are made up; no artist's, record's or maker's name.

| Folder | Count | What | Whole sound? |
| --- | --- | --- | --- |
| CLASSIC | 8 | The box's own range: saw and square, mid resonance, accents doing the work. **Preset 0: Classic Saw** | plain, but 2 with TAPE on ECHO |
| SQUELCH | 8 | RESONANCE high, ENV MOD high, short DECAY, SWEEP up | 4 with a STEPPER on CUTOFF (MOD 1) |
| DEEP | 6 | Low CUTOFF, low ENV MOD, OCTAVE -1, long AMP DECAY: sub lines | plain |
| DISTORTED | 6 | DRIVE up, and DRIVE / SATURATE / FUZZ in slot 1 | all 6 whole: an effect, some a DELAY send |
| MOVING | 4 | An LFO on CUTOFF or RESONANCE, a STEPPER on TUNE | all 4 whole |

Each played through `presets_test --heard` (loudness, peak, a tail that ends,
every MOD moving **and heard**, as FM-6's).

**Writing a line on the handheld**: the roll is enough. A note is A; an accent
is hold A + RIGHT once; a slide is hold X + UP twice (OFF, 1/4, **1/2**). Batch
3 counts the presses for a 16-step line and says it. No pattern presets in
version 1 (Apotheca has none for any track).

## 6. Batches

One job each. Each ends with `ctest` green under `set -o pipefail` and its
mutants killed.

| Batch | What | Tests | Risk |
| --- | --- | --- | --- |
| **0** | **Groundwork, no engine change.** Open the sources or mark SELF. `engine/include/apotheca/acid_rule.hpp`: `kAccentAt`, `kTieMs`, the tie rule (`slides(glide, gateShutFor)`), the slide's τ and landing, the dial readings (HZ, MS, S, CENTS, DB), the settings table | `acid_rule_test`: the rule case by case (the table in section 2), the readings at both ends, the landing | Little. The sources may not be found: then everything is SELF and says so |
| **1** | **The voice.** `acid.hpp/.cpp`, `SynthKind::Acid` (appended), the factory; the oscillator, the filter with its read-ahead bend, both envelopes, the tail slot, sleep; the `bench` row | `measure.hpp`: **pitch within 0.5 cent** keys 24 to 72 (through `bandOf`); the filter's slope at three cutoffs (near 18 dB/oct, SELF); **its singing at RESONANCE 100: the pitch within 10 cents of the cutoff, the level** (SELF); `asideOf` under the bound; blocks of 1 to 256 the same to the bit; too small (a 60 s tail, no state under 1e-30 unsnapped); NaN; silence in, silence out; no allocation | **The filter's singing**, as JAX-EP's was: read ahead from the start. Cost over target: profile before cutting |
| **2** | **Accent and slide.** `noteOnFrom` by the rule header; the accent's level, shortest decay and sweep; SLIDE NOTE and FIXED | **Slide time measured** (pitch at τ, 2τ, the end, to the cent); no envelope restart on a slide (the filter envelope's level continues), restart after a rest; **accent: level up by its dB, decay shortest, the sweep climbing over 4 accents 125 ms apart and not over 4 a second apart**; ACCENT 0 the same as plain to the bit; through the engine with GLIDE FROM TOP and PAIRED; Play twice the same | The tie on one sample: notes end to end in either order must slide. Test both orders |
| **3** | **The app.** `TrackKind::Acid` appended, its row last in `kInstruments`, `AcidPatch`, the spec `ac` (pages, readings, `loadPreset` defaults first as PHYSIC's), `.acid`, `settingCount`, `userList`, `builtInPresets`, `specFor`, DEVICE in `App::screenNow()`, stills and `kViews`, the tests' lists (`textsize`, `scope_test`, `save_test`, `modapp_test`); the roll's accent mark | `app_test` (a track saves and loads, undo), `textsize` at XL, `textviews`, `themeviews` (the mark on PAPER), `words` (no maker or model), the hints at 0 over; **the presses for a 16-step line counted** | The hints bar on two pages of 6 and 8; the mark crowding the glide line |
| **4** | **The 32 presets** in `acid.inc` | `presets_test` (no problems, `--heard`: level, peak, tail, MODs heard), no clip as written and with macros full | Presets judged by numbers only |
| **5** | **Review**: a fresh agent reads the branch, mutation testing on the rule and the voice, `bench` again; docs: README's ACID page, CLAUDE.md (Adding an instrument list, Measured, the instrument's rules) | `ctest`, mutants | |
| 6 | **Later, on the owner's source**: calibration from recordings (slide time, sweep, the square's shape, the filter's slope and singing) | Each SELF figure becomes FAITHFUL or stays SELF | Waits on recordings |

Batches 2 and 3 can run side by side after 1.

## 7. The owner's calls

**Answered 2026-11-16: "go with your picks"** (relayed by the coordinator).
Each answer below is the plan's pick, now taken. A later "no" changes one
constant or one row.

| # | Call | Taken |
| --- | --- | --- |
| 1 | Name | **ACID** (not SQUELCH, not LINE BASS) |
| 2 | ACCENT from velocity or a new flag | **Velocity ≥ 0.95**; placed notes (0.9) plain, one click makes an accent |
| 3 | Plain notes and velocity | **All the same level** (VELOCITY OFF by default; ON on the SOUND page) |
| 4 | SLIDE's time | **The note's glide length** (SLIDE NOTE; the roll's line is the truth). FIXED 60 ms on the SOUND page |
| 5 | A GLIDE note after a rest | **Slides in pitch, starts the envelopes again** |
| 6 | Notes that overlap without GLIDE | **Start again** (no legato without GLIDE) |
| 7 | Letting go of the newest key while an older one is held | **The note ends**; no going back to the older key |
| 8 | Voices | **One** (plus a 3 ms tail slot) |
| 9 | WAVE | **SAW or SQUARE, a choice**; no blend in version 1 |
| 10 | Oversampling | **None** unless the aliasing test fails |
| 11 | A one-press ACCENT gesture | **None**: hold A + RIGHT is one click; the roll marks a 100% note on an ACID track |
| 12 | Y on MAIN | **Nothing** (no ROLL) |
| 13 | Presets | **32 in five folders** (CLASSIC, SQUELCH, DEEP, DISTORTED, MOVING); preset 0 Classic Saw |
