# DRUM SYNTH plan (a drum lane's sound made from oscillators and noise)

Written 2026-11-17 from the code at `main` 85a0565 (0.669), branch `drumsynth`.
Batches 0, 1 and 2 (the generator, its twelve models, the app) are built on that
branch; "As built" at the end says what they came to. **The owner cannot hear**: every
claim about the sound is a test or a still. Every figure for a recipe is SELF
(this plan's own; no drum machine's service notes were opened).

The owner asked (2026-11-17): "Let's do a drum synth": kicks, snares, hats,
toms, claps and more, made from oscillators and noise with knobs (TUNE, DECAY
and so on). Today a drum lane plays a recording or the built-in drum, which is
already made in code (`DrumKit::buildBuiltIn`) but has no knobs.

**What a drum synth is, in one line**: each lane's sound is worked out from a
few simple parts (a sine that falls in pitch, filtered noise, six square waves
for metal) by a recipe, and the knobs change the recipe.

## The recommendation

**A drum lane's SOUND may be a SYNTH VOICE**: one of twelve MODELS with up to
eight knobs. **The voice is rendered into a sound on the app thread when a knob
turns, and handed to the engine exactly as a recording is** (the path EDIT
already uses: `LaneSound`, `setLaneSample`, the helper thread for a render too
costly for a frame). Nothing new runs while the song plays.

| Question | Answer |
| --- | --- |
| What it is | **A lane's sound from a recipe**: KICK, SNARE, TOM, CLAP, CL HAT, OP HAT, CYMBAL, RIM, COWBELL, CLAVE, ZAP, NOISE |
| Where it is chosen | **The KIT page's SOUND column**: hold A + up / down goes from the built-in drum through the twelve models to the recordings |
| Its knobs | **One fixed set of eight places**: TUNE, DECAY, TONE, SNAP, NOISE, SWEEP, DRIVE and the model's own (BEND, BODY, SPREAD); a model shows the ones it uses (4 to 8), each read in its unit (HZ, MS / S, %, ST) |
| Where the knobs are | **The VOICE view**: tap A on a synth lane's SOUND. Laid out as the EDIT view: the sound's picture on top, two rows of four boxes |
| Cost while playing | **None.** The engine plays a sample, as for any recording |
| Cost of a knob turned | **0.1 to 6 ms on the desk** (measured, "As built"), **about 2 to 90 ms on the handheld** (x 13 to 17). A render over `kShapeInAFrame`'s budget goes to the helper thread, as a costly EDIT shape does |
| What else works | **Everything a recording has**: EDIT, FIT, slices, CHOKE, FEEL, THROW, the step effects (pitch, ratchet, reverse, cut), Catch, a bounce, the lane's VOLUME |
| In a song file | `lanesynth LANE WORD v1 .. v8`, **left out when the lane is not a synth**; numbers held as doubles first |
| Undo | **The voice is a step** (model and knobs: `soundMark` / `restoreSounds`, as CHOKE and FEEL) |
| First thing to build | **Batch 0**: `engine/include/apotheca/drum_synth.hpp`, the rule (models, knobs, readings, words) and its test. **Built** |

### The pick and the alternative

| | **Pick: rendered to a sound** | Alternative: a voice in the engine |
| --- | --- | --- |
| Play cost | **0**: a sample, as today | a voice per hit, every sample: 12 tracks of 8 lanes of busy hits is the busiest song again, on a handheld already near its limit |
| Per-hit change | the step effects (pitch, reverse, cut, ratchet) and FEEL's level, as any lane | a MOD or an accent could move TUNE or DECAY hit by hit |
| Touches | the app's lane path only (session_samples.cpp), the generator (new, no SDL) | `DrumMachine::render`'s hot loop, `plainRun`, the thrown copy, CHOKE, slices, the carry-on: each would need a synth path beside the sample path |
| Everything else | works unchanged (it is a recording to all of it) | slices, FIT, EDIT, reverse and the carry-on mean nothing on a voice that is not a sample |
| Risk | a render long on the handheld (the helper thread) | the whole drum machine's bit-exact rules (THROW's two loops, runs, choke) |

**The alternative could come later** as its own thing (a "LIVE" switch on a
voice, rendered per hit in the engine), if the owner wants a MOD on a drum's
TUNE. Nothing in the pick stands in its way: the generator is the same
function either way.

## 1. The models

**Twelve, in this order on the SOUND column** (the order is the player's; the
song file names a model by its WORD, never by its place). Every dial is 0..100
(a whole number); the reading is in its unit. **Log** dials go from the low end
to the high end by the same ratio each step.

### The parts every recipe is made of

| Part | What it is | Cheap because |
| --- | --- | --- |
| BODY | a sine whose pitch falls from `TUNE x 2^(SWEEP / 12)` to TUNE, the fall `exp(-t / tau)` with **BEND the time it is 99% over** (tau = BEND / 4.6); its own decay | one phase add, a polynomial sine, two multiplies for the envelopes |
| TONE 2 | a second sine at a fixed ratio of BODY (the snare's upper shell mode, the rim's low tone), falling with it | the same |
| SQUARES | up to six square waves at fixed ratios (the hats' metal: 1, 1.483, 1.800, 2.546, 2.630, 3.897 of TUNE; the cowbell's two: 1, 1.48), summed, through a state-variable filter | an integer phase each, its top bit |
| NOISE | white noise from **a hash of the voice and the sample's number** (never a generator walked), through a state-variable filter (low, band or high pass) | one hash, one filter |
| CLICK | a short burst of noise through a one-pole low-pass (the beater): SNAP its level, its brightness TONE on KICK and TOM | one multiply-add |
| ENVELOPES | each part decays exponentially, **DECAY the time to -60 dB**; CLAP's noise has bursts, NOISE's a rise | one multiply a sample |
| DRIVE | a soft curve (`x (27 + x^2) / (27 + 9 x^2)`, held at 1 from 3 on) after the parts are summed and set to a peak of 1 | a few multiplies |

**After the recipe, every voice is set to the same peak: -1 dB (0.891)**, so a
knob turned never clips and a model change never jumps the level; the lane's
VOLUME, EDIT's GAIN and the mixer set how loud it is. **The sound ends where
every part is 80 dB down, with its last 5 ms faded to exact zeros**, and is
never longer than 6 s.

### The twelve, and their knobs

`-` = the model does not use that place (the box is empty). Usual values in
brackets.

| Model (word) | TUNE | DECAY | TONE | SNAP | NOISE | SWEEP | DRIVE | Its own |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **KICK** (`kick`) | body, 30 to 150 HZ log (50) | 50 MS to 2 S log (500) | click brightness 1 to 12 kHz (40%) | click level (40%) | noise in the body (0%) | 0 to 48 ST (+24) | 30% | **BEND** 5 to 200 MS log (40) |
| **SNARE** (`snare`) | shell, 100 to 400 HZ log (180) | the wires, 50 MS to 1 S (250) | the wires' high pass, 500 HZ to 8 kHz (35%) | the crack (50%) | wires against shell (60%) | 0 to 24 ST (+7) | 30% | **BODY**, the shell's decay, 20 to 500 MS (130) |
| **TOM** (`tom`) | 60 to 500 HZ log (110) | 50 MS to 2 S (450) | click brightness (30%) | click (30%) | 0% | 0 to 24 ST (+12) | 20% | **BEND** (60 MS) |
| **CLAP** (`clap`) | the band's centre, 400 HZ to 5 kHz (1.2 kHz) | the tail, 50 MS to 1.5 S (360) | the band's width (50%) | bursts against tail (70%) | - | - | 30% | **SPREAD**, between bursts, 3 to 30 MS (9); four bursts |
| **CL HAT** (`clhat`) | the metal, 100 to 600 HZ (205) | 10 to 400 MS (50) | high pass 3 to 14 kHz (55%) | the front (30%) | noise against metal (50%) | - | 20% | - |
| **OP HAT** (`ophat`) | as CL HAT | 100 MS to 3 S (550) | as CL HAT | as CL HAT | as CL HAT | - | 20% | - |
| **CYMBAL** (`cymbal`) | the metal, 100 to 600 HZ (270) | 300 MS to 6 S (1.7 S) | high pass 1 to 10 kHz (55%) | the crash's front (40%) | 60% | - | 20% | - |
| **RIM** (`rim`) | 800 HZ to 3 kHz (1720) | 10 to 200 MS (40) | the low tone's share (40%) | click (70%) | - | - | 60% | - |
| **COWBELL** (`cowbell`) | the lower square, 300 HZ to 1.2 kHz (540; the upper x 1.48) | 30 MS to 1.5 S (350) | the band pass's height (50%) | the front (40%) | - | - | 20% | - |
| **CLAVE** (`clave`) | 600 HZ to 5 kHz (2500) | 5 to 300 MS (35) | - | click (30%) | - | - | 0% | - |
| **ZAP** (`zap`) | where it lands, 30 HZ to 1 kHz (80) | 20 MS to 1.5 S (250) | - | - | noise (0%) | 0 to 60 ST (+48) | 20% | **BEND** 2 to 300 MS (60) |
| **NOISE** (`noise`) | the filter's centre, 200 HZ to 14 kHz (5 kHz) | 10 MS to 2 S (150) | the filter's width: narrow 0% to wide 100% (60%) | the attack: 40 MS rise at 0%, at once at 100% (80%) | - | - | 0% | - |

**Why twelve and these**: the classic machines' voices (the built-in kit's
eight roles, plus a cowbell, a clave, a zap and a noise burst for shakers,
risers and effects). OP HAT is CL HAT with a longer DECAY range, kept apart so
a lane's name says what it is and CHOKE's "closed cuts open" reads plainly.

**A model change puts the new model's usual knobs** (KICK's TUNE 50 HZ means
nothing to a hat). Undo brings the old voice back.

## 2. The rule as code (no SDL)

`engine/include/apotheca/drum_synth.hpp` and `engine/src/drum_synth.cpp`, in
`engine/` beside the built-in kit (which is also made in code there). The
engine never calls it: the app does, and hands the result over as a recording.

| Rule | How |
| --- | --- |
| **The same voice, the same bits** | noise is `noiseAt(seed, n)`, a hash of the voice (model and eight dials) and the sample's number; nothing walked. `#pragma clang fp contract(off)` as PHYSIC, so the handheld's ARM joins no multiply and add. The sine is a polynomial; `exp`, `pow` and `tan` are used only for the recipe's numbers before the loop (as PHYSIC) |
| **No number too small** | every envelope stops at -80 dB (the sound's length is worked out from them), so no state comes near 1e-30. The test makes each model with flush-to-zero on and off: **the same bits** |
| **Ends in exact zeros** | the last 5 ms faded to 0; the last sample is exactly 0 |
| **Never past full scale** | the peak is set to 0.891 after DRIVE; nothing after it |
| **A dial is a whole number** | so a `Voice` has no bytes that are not set (undo hashes it whole) and two compare as the song file writes them |

## 3. Cost

**At play: nothing.** A synth lane is a sample to the engine.

**A knob turned**: one render. Measured in batch 1 on the desk (ns a sample,
times the sound's length): see "As built". The app's frame budget is EDIT's
`kShapeInAFrame` (about 1.5 ms of the desk, 20 to 30 of the handheld);
`drumsynth::work(voice)` estimates a render in the same units, and a render
over it goes to the helper thread (`Session::Job`, the version checked as a
fit's is). **Until it lands the lane plays what it had.** The stick turning a
knob asks for a render each frame the dial moves; a job already running is
let finish and the latest dial is rendered next (the version check drops a
stale one).

## 4. The app (batch 2)

### The data

- `TrackData::laneSynth[kDrumLanes]`: a `drumsynth::Voice` each (model 0:
  not a synth). **A lane with a synth has no `laneSample`**: picking one empties
  the other (`Session::setLaneSynth`, `setLaneSample`).
- `LaneSound`: a synth lane's `source` and `recording` are the made sound, so
  FIT, EDIT, the picture and `installLane` treat it as a recording (FIT works,
  which the built-in drum does not do). `recordingChanged`, `findMissing`, the
  WiFi page's renames do not see it (it has no name).
- `Session::setLaneSynth(track, lane, voice)` makes the sound (or starts the
  job), keeps EDIT (START and END back to the whole, as for another
  recording), and is what undo calls.

### The KIT page

- **SOUND, hold A + up / down**: from the built-in drum UP the recordings by
  name, as now, and **DOWN KICK ... NOISE (SYNTH)**, so each is one press from
  the drum and the recordings are where they were. A synth lane's row reads the
  model's name and SYNTH where a built-in reads BUILT IN. The ends stop.
- **Tap A on a synth lane's SOUND opens the VOICE view** (on any other lane it
  opens the list of recordings, as now; from a synth lane the list is reached
  with hold A + DOWN past the models, or B: the built-in drum back, then tap A).
- **The VOICE view** is the EDIT view's layout (`kEdit...` in kit_screen.hpp):
  the top box is the MODEL with the made sound's picture; under it two rows of
  four boxes, the eight knob places (an empty place is drawn empty and the
  cursor passes it by).
  - **Hold A + up / down turns a box** (the stick through its range in about a
    second, by the square of its push, as EDIT; the D-pad one dial step). On
    MODEL: the model before / after (its usual knobs).
  - **B**: the box to its usual; on a box already there, back to the kit.
    **Hold B**: the whole voice to the model's usual (a bar fills, a second;
    undo brings it back).
  - **Y: ROLL**: the shown knobs rolled by a hash of the voice and a count
    (never a generator walked), each inside the middle of its range (25 to 75),
    the MODEL kept. One step of undo.
  - **X hears** the lane (held: to its end); a box turned is heard with the
    song stopped, as EDIT's (`kHearEveryMs`).
  - **L1 / R1**: the lane before / after, staying in the view (the eight
    lanes' voices are tuned against each other).
- **EDIT works on a synth lane** as on any sound (its picture is the made one).

### Stills and checks

`kit-synth` (the column with three synth lanes), `kit-voice` (the view on
KICK), `kit-voice-held` (A held on TUNE), `kit-voice-deleting`; each in
`kViews`. `textsize` holds the view at XL with the longest reading
("14.0 KHZ", "1.50 S", "+60 ST"); `themeviews` on PAPER; `idle_test` a part
(a knob turned: the render landing from the helper thread is a stir);
`words` (nothing new: no maker's name in the model names: "CL HAT", not a
machine's).

### The song file

`lanesynth LANE WORD v1 v2 v3 v4 v5 v6 v7 v8`: the model by its word, then the
eight dials in place order (an unused place written as its usual).
**Left out when the lane is not a synth.** A word nobody knows: the line is
skipped (the lane plays what else it has). Each number is held as a double
first (`wholeIn(v, 0, 100)`); a number missing or not a number ends the line
there, the rest the model's usual. **A lane with both a `sample` and a
`lanesynth` line is what the LATER line says** (each empties the other; the
program never writes both).

### Undo

**The voice is a step of undo** (`soundMark` mixes `laneSynth`;
`restoreSounds` calls `setLaneSynth` where it differs). A recording is not in
undo (as today), so undo after a recording replaced a synth puts the synth
back and the lane forgets the recording's name; the plan takes that as it
is (a recording picked again is one press).

## 5. Presets (batch 3)

**As built (batch 3): 73 named voices, 5 to 8 a model, CLASSIC (the usual) first**,
in `app/presets/drumvoices.inc` read by `app/drum_voices.cpp` (`voice WORD NAME`, then `set PLACE
DIAL ...`). **A lane keeps its dials, not the voice's name or place**, so the list is NOT append-only.
`drumsynthapp_test` (`testTheNamedVoices`) fails a line it cannot read, a voice that is the usual or
another's, and two voices of a model that are alike (length within 1.3 times, brightness within 1.2
and crest within 1.3: it caught seven pairs in the first draft). The VOICE view writes the voice's name
in brass after the model while the dials are its. Planned below: **Minimal: each model's usual is its sound, and a short list of NAMED VOICES
a model** as built-in data (`app/presets/drumvoices.inc`: `voice MODEL NAME
v1..v8`, read by the presets reader's rules, append-only, `presets_test` reads
it): KICK BOOM, PUNCH, DEEP, CLICK...; four to six a model, about 50 in
all. **Picked on the MODEL box with hold A + LEFT / RIGHT** (UP / DOWN stays
the model). **No "synth kit" command in version 1**: eight lanes set one by
one is 8 to 16 presses; a whole drum track's preset is a later thing for every
kind of lane, not just synth ones.

## 6. The designer page (batch 4)

**As built**: the SOUND choice lists the models in a SYNTH group (`lanesynth L WORD`); a synth lane's
card has VOICE (its named voices, `lanevoice L N`, TURNED BY HAND when the dials are no voice's) and
its knobs as sliders with the handheld's readings. **The stride was NOT changed**: a knob is the id
`kKitKnobs + lane * 8 + place` (128 on), so every id before stayed where it was (`design_test` had them
as numbers). **A synth lane's picture, length and least gap come in the state, not the spec**, so a knob
turned does not rebuild the page under the slider. Tried in Chromium with Playwright: no script errors.
Planned:

The KIT page (`Session::serveKit`, `kitSpec`) gives each lane card a SOUND
choice that lists the models after the built-in drum; a synth lane's card
shows its knobs as sliders with the handheld's readings. New `set` fields per
lane, appended after `kKitHumanLevel` (`kKitSynthModel`, then eight knob fields:
16 fields a lane fit `kKitStride` 16 exactly, so the stride goes to 32,
the page told by its spec). `lanesynth L WORD` picks a model. The script tried
in a browser (Playwright), as the house rule says.

## 7. Tests

| Batch | Test | What |
| --- | --- | --- |
| 0 | `drumsynth_test` (the rule part) | every model's words, names, shown places (4 to 8), usual dials in range; each reading at both ends of its dial; log dials end on their ends; the hash the same twice and different by voice; a word round trip; an unknown word is no model |
| 1 | `drumsynth_test` (the models) | each model at its usual and at ten rolled voices: **peak 0.891**, **the last 5 ms ending in exact zeros**, no NaN, no number under 1e-30 but zero, **the same bits with flush-to-zero on and off**, the same bits twice, its length as `lengthSeconds` says; **pitch**: KICK, TOM, ZAP land on TUNE (cents, read after the sweep), CLAVE and RIM ring at TUNE, COWBELL's two squares at TUNE and x 1.48, SNARE's shell at TUNE; **SWEEP**: the first cycles are SWEEP semitones over TUNE; **DECAY**: the level falls 60 dB in DECAY (within 20%, DRIVE 0); **TONE** brightens (the spectrum's centre rises with it), **SNAP** puts more in the first 5 ms, **NOISE** more that is not the pitch, **DRIVE** less peak over level; checksums of the twelve at their usual (pinned on this machine's maths, falling back as effects_test does); cost printed with `--measure` |
| 2 | `drumsynthapp_test` | the SOUND column's order, the VOICE view's gestures, the song file, undo, the exclusivity, EDIT and FIT on a synth lane, a long render on the helper thread, a bounce, the stills' words at XL |
| 3 | `presets_test` | every named voice reads, no problems; each made at its peak |
| 4 | `design_test` | `testKitSynthFromThePage` |

## 8. Batches

| Batch | What | Risk |
| --- | --- | --- |
| **0** | **The rule**: `drum_synth.hpp` (models, knob places, ranges, readings, words, usual voices, the hash, `lengthSeconds`, `work`) and its test | little |
| **1** | **The models**: `drum_synth.cpp`, the twelve recipes, their tests, `bench`'s row (the time to make each model's usual voice) | a recipe that the tests like and the ear does not: the owner cannot hear, so each is held only by numbers |
| **2** | **The app**: `TrackData::laneSynth`, `setLaneSynth`, the lane path (`loadLane`, the job), the SOUND column, the VOICE view, ROLL, the song file, undo, stills, `textsize`, `idle_test`, `drumsynthapp_test` | the VOICE view at XL; the helper thread for long renders |
| **3** | **Named voices** as data, picked on MODEL | numbers, not ears |
| **4** | **The designer page** | the page's script has no `ctest` |
| **5** | **Review**: a fresh agent, mutation testing, README's DRUM SYNTH section, CLAUDE.md | |

Batches 3 and 4 can run side by side after 2.

## 9. The owner's calls

Each with the pick. A later "no" changes one constant or one row.

| # | Call | Pick |
| --- | --- | --- |
| 1 | Rendered to a sound, or a live voice | **Rendered**: costs nothing at play, everything a recording has works. Live later if a MOD per hit is wanted |
| 2 | The models | **Twelve**: KICK, SNARE, TOM, CLAP, CL HAT, OP HAT, CYMBAL, RIM, COWBELL, CLAVE, ZAP, NOISE |
| 3 | The knobs | **One set of eight places** (TUNE, DECAY, TONE, SNAP, NOISE, SWEEP, DRIVE, the model's own), 4 to 8 shown a model, in units |
| 4 | The level | **Every voice made to -1 dB peak**; level is VOLUME, GAIN and the mixer |
| 5 | A model changed | **The new model's usual knobs** (not carried) |
| 6 | Where | **SOUND column for the model; the VOICE view for the knobs**, EDIT unchanged |
| 7 | Y in the VOICE view | **ROLL** the shown knobs (25 to 75), the model kept |
| 8 | FIT on a synth lane | **Works, as on a recording** (the built-in drum's does not) |
| 9 | Presets | **Named voices a model as data, on MODEL with hold A + LEFT / RIGHT; no whole synth kit in version 1** |
| 10 | Undo | **The voice is a step**; a recording replacing it is not (as today) |
| 11 | A new kit | **Stays the built-in drums**; synth voices are chosen lane by lane |
| 12 | The designer page | **A model choice and the knobs on each lane's card** |

## As built: batches 0 to 2 (2026-11-17, branch `drumsynth`)

**The generator, the twelve models and the app are built and tested. Nobody has heard any of
it; it has not run on the handheld.** Every recipe figure is SELF. The owner's calls (section 9)
were not answered: built at the picks.

### What is where

| File | Holds |
| --- | --- |
| `engine/include/apotheca/drum_synth.hpp` | the rule: `Model`, `Place`, the table of knobs (name, unit, range, log, usual), the readings, words, `usualVoice`, `voiceHeld`, `seedOf`, `noiseAt` |
| `engine/src/drum_synth.cpp` | the recipes (`recipeOf`), `lengthOf`, `work`, `make`, `sineTurn`, `soft` |
| `app/project.hpp` | `TrackData::laneSynth` |
| `app/session_samples.cpp` | `Session::setLaneSynth`, `madeLane`; `loadLane` makes a voice (or `LaneSound::making`, the helper thread's `Job::make`); `setLaneSample` takes a voice off |
| `app/song_file.cpp`, `app/session.cpp` | `lanesynth`; `soundMark` / `restoreSounds` |
| `app/kit_screen.cpp/.hpp` | the SOUND column's models, the VOICE view |
| `tests/drumsynth_test.cpp` | `ctest -R drumsynth`: the rule and the sound (0.4 s) |
| `tests/drumsynthapp_test.cpp` | `ctest -R drumsynthapp`: the app (1 s) |
| `app/bench_run.cpp` | "the drum synth, a voice made": KICK, OP HAT, CYMBAL at 6 s |

### Where it differs from the plan

| Plan | Built | Why |
| --- | --- | --- |
| a filter's tan from the library | **tan from the voice's own sine** (`sineTurn(u) / sineTurn(u + 1/4)`) | the handheld's glibc gives another last bit for `tan` (found under qemu): with it the bits are the desk's |
| the usual values read round (50 HZ, 500 MS) | **the dial nearest them** (KICK: TUNE 50 HZ at dial 32, DECAY 492 MS at dial 62) | a dial is a whole number 0..100 on a log scale |
| SNAP's click as remembered | **every filtered noise set back to white noise's level** (`whiteAgain`), clicks 1.5 to 2.5 times the snap | at the first levels KICK's NOISE and SNAP changed nothing measurable |
| both lines in a file: a synth | **the later line wins** | simpler; the program writes only one |
| the models between the drum and the recordings | **the models DOWN from the drum, the recordings UP as before** | twelve presses more to the first recording was slow, and `sample_test`'s steps stayed as they were |

### Measured (`drumsynth_test --measure`, the desk)

| What | Figure |
| --- | --- |
| Pitch | KICK, TOM, ZAP, CLAVE, RIM at TUNE 0, 30, 70, 100: within 0.01 cent of TUNE (zero crossings after the sweep) |
| SWEEP | the first cycle 30 to 120 cents under `TUNE x 2^(SWEEP/12)` (the fall inside the cycle), 0 at SWEEP 0 |
| DECAY | the level falls 60 dB in DECAY within 5% (the sines) and 15% (the noisy ones), DECAY 20, 60, 90 |
| The sine | 6.6e-10 from the library's at worst |
| Peak / end | every model at its usual, at every dial 0, every dial 100 and 12 rolls: peak 0.891 (-1 dB), the last sample 0, the last 5 ms under -40 dB, no number under 1e-38 but 0, the same bits with flush-to-zero on |
| The bits | the twelve usual voices and 72 rolled ones: **the same checksums under qemu-aarch64 with the handheld's build** (clang, -O2 -mcpu=cortex-a53, its sysroot) as on the desk |
| Cost, a voice made | 8 to 20 ns a sample: KICK as it starts 0.38 ms, OP HAT 0.57 ms, CYMBAL at 6 s 4.7 ms (`bench`). Handheld at x13 to 17: about 5 to 10 ms, and 60 to 80 ms for the longest (on the helper thread) |
| `kWorkInAFrame` | 7e5 units, about 1.5 ms on the desk (2.1 ns a unit at most): KICK at 2 s, OP HAT past about 1 s and CYMBAL past about 1.3 s go to the helper thread |
| At play | nothing: a sample, as any recording |

### Tests and mutants

- **The generator: 27 mutants, 24 killed**, 3 equivalent (a phase wrap's `>=` for `>`; a line that set
  the dial's low end, taken out; the reading's ms-or-seconds edge no dial reaches). Five of the kills
  were by the checksums alone at first; a high pass test was added for one.
- **The app: 16 mutants, 15 killed**; the one left (the helper's voice checked beside its version) is
  a belt: any voice change moves the version. A ROLL test of the spread was added for one.
- `idle_test` has a part (a CYMBAL at 6 s made on the helper thread, rolled, put back); `textsize`
  holds the VOICE view at its widest (14.0 KHZ, 2.00 S, +60 ST, 300 MS) and its hints at 0 over.

### Left

- **Batch 3**: the named voices (data, on MODEL with hold A + LEFT / RIGHT).
- **Batch 4**: the designer page (a model choice and the knobs on a lane's card).
- **Batch 5**: a fresh review, README's DRUM SYNTH section.
- Not done: a still of a voice MAKING IT... (it lands within a frame or two on the desk).

## As built: batches 3 to 5 (2026-11-17, branch `drumsynth`, after main's join)

- **Batch 3, named voices**: 73 in `app/presets/drumvoices.inc` (5 to 8 a model past CLASSIC), read by
  `app/drum_voices.cpp`; picked on MODEL with hold A + LEFT / RIGHT; the name in brass while the dials
  are its; still `kit-voice-named`. **The test holds them apart** (`testTheNamedVoices`: no two of a
  model alike in length, brightness and crest, and each model's list reaching 3 times in length or 2 in
  brightness); the first draft had seven pairs too alike, re-made. Nobody has heard any of them.
- **Batch 4, the designer page**: section 6's "As built". Tried in Chromium with Playwright.
- **Batch 5, the review** (by the same agent, read afresh; no second agent was there to ask):
  - **Found and mended: a long voice with the designer page open never landed.** The page (and the VOICE
    view) asks `readyLane` each frame; a lane whose voice was on the helper thread has no source yet, so
    it was read again, its version moved, and the job's sound was thrown away every time.
    `readyLane` now passes a lane that is `making` by; `testALongVoiceWaits` asks it every frame and
    failed without the mend.
  - **Mended: LEFT / RIGHT on MODEL went on from another lane's pick** (`voicePicked_` was the
    screen's): `openVoice` takes it from the lane's own voice.
  - Mutants since batch 2: the reader 8 (7 killed after a test was strengthened, 1 equivalent:
    `shown()` is `name != nullptr`), the VOICE view's picking 3 (2 killed, 1 equivalent line taken out),
    the page 6 (5 killed after a test was added, 1 equivalent line taken out).
  - `bench`: "all 85 named voices" made one after another: 68 ms on the desk (0.8 ms each; the slowest
    RIDE 4.4 ms; 11 go to the helper thread). **A song of eight synth lanes opened makes eight on the
    app thread**: about 1 to 6 ms on the desk, so about 15 to 90 ms on the handheld, once, at open.
  - README: the KIT section's DRUM SYNTH part, and the sound designer's.
