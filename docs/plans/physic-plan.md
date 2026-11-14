# PHYSIC plan

Written 2026-11-14 from the code at 0.455 (branch `physicplan`). Three
designers each wrote a plan ("sound", "engine", "hands"); this one is joined
from them, then revised after a review the same day (section 13 says what
was taken and what was not). **Nothing here is built and nobody has heard anything.** The code
was only read. The designers ran four throwaway programs for cost and tuning
in a scratchpad, not in the repo, on a 2.3 GHz Xeon container: those figures
are marked **scratch**. They are not the owner's desk and not `bench`.

After PHYSIC, on the list in the owner's order (2026-11-14): **an acid mono
bass, then a granular synth.** Neither is planned here.

## The recommendation

**One new 8-voice synth, PHYSIC, with a MODEL choice and two engines
underneath:**

- **A string**: an extended Karplus-Strong loop. A delay line one period long
  holds the string's wave; a filter in the loop sets how it dies away.
- **A bank of modes**: up to 12 tuned resonators a voice. A mode is one way an
  object rings, at its own pitch and its own decay. A marimba bar is about
  three strong modes; a bell about nine.

One strike (pluck or mallet), one set of dice and one voice handler serve
both. **BODY, PAIR, MOTOR and the NOISE strike come in a second step**
(batch 10), after the owner has seen version 1's figures (section 13, #16). This is the split Mutable Instruments Rings and Ableton
Collision use (sources **MI**, **AC**).

| Question | Answer |
| --- | --- |
| What it is | **Plucked and struck sounds**: strings, marimba, xylophone, vibes, glockenspiel, kalimba, bells, chimes, glass, drums, woodblock. You pick what is hit (MODEL), what hits it (STRIKE, HARDNESS) and where (POSITION) |
| Version 1 | **18 settings**, 11 models, PLUCK and MALLET. BODY, PAIR, MOTOR and NOISE are appended in batch 10 (4 settings and a third STRIKE) |
| Models | **11** on one choice: STRING, MARIMBA, XYLO, VIBES, GLOCK, KALIMBA, BELL, CHIME, GLASS, DRUM, WOOD. Each modal model is one row of mode ratios with a source (section 2). WOOD alone is SELF |
| New track sound | **Preset 0 is MARIMBA**, so "add a track, place notes" gives a marimba line with nothing chosen on DEVICE |
| Voices | **8 sounding, plus 2 slots that only fade out a stolen voice** (no click). One constant |
| Tuning | **Within 0.5 cent**, for the SOUNDING pitch (key, OCTAVE and FINE together): STRING 24 to 108, every modal model 12 to 120. Outside that the pitch is held at the edge (a test says so). Scratch: a plain KS loop was **up to 88 cents out**; the fixed loop 0.16 cents |
| Dice | **Per note**: strike position, hardness, a few cents of tune and the decay are rolled, each by its own amount, **from where and which time** (the track, the strike's own moment since Play, the key). Each ratchet piece and each arpeggio note has its own moment. Y on MAIN rolls a new patch, as QMOD's |
| Cost target | **≤ 0.8% of a desk core for 8 notes**, ceiling 1.2% (Phazed 0.9%, QMOD 2.6%: CLAUDE.md, Measured). Handheld about 4% (×5, a guess). Scratch: 8 voices × 12 modes 0.48% and 0.56% (two runs), 8 strings 0.09-0.22%. **Only `bench` on the desk counts** (section 6) |
| The trap | **Tiny numbers.** Scratch: the same modes left to decay with flush-to-zero off cost **15 times as much in one run and 150 times in another** (63% of a core against 0.41%). **Every MODE snaps its own state to exact zero on every tick**, not only the whole voice, because high modes die long before the voice does |
| Engine changes | **Two virtual calls**, each a no-op for every other synth, so every pinned sound stays the same to the bit: `Instrument::noteSeed(uint32_t)` just before `noteOn` (batch 4), and `Instrument::stopNotes()` for Stop, which by default is `allNotesOff()` (batch 5) |
| Song file | `track physic`, presets `.physic`, settings appended only |
| First thing to build | **Batch 0**: the sources with pages, the shared measuring tools, a pure rule header with the mode tables. No engine change |

**The owner cannot hear.** Every claim about the sound is a test, and every
test is marked:

- **FAITHFUL**: checked against a published figure or formula, source named.
- **SELF**: checked against this plan's own choice. A green SELF test only
  says the code does what the plan guessed.

### Which design won, and what was taken from the others

| Design | Strongest | Weakest | Taken |
| --- | --- | --- | --- |
| **engine** (the spine) | Measured what the handheld cares about: tuning to 0.16 cent, **glide clicks** (naive allpass switch 9.5, state reset 3.9), cost per part. Saw that **ratchet pieces share one `at`** (`engine.cpp:532`), so a seed of `at` alone rolls every piece the same. `setMacro` that only stores a number, for the modulators | Bell and glass tables from the ideal formula, not from measured instruments; PAIR on STRING only | The two cores, allpass tuning with state reset, 12-mode cap, per-sample glide, take-over glide, cheap `setMacro`, the batch order |
| **sound** | **The best physics sources**: Hibbert's measured church and tubular bells, Chapman's measured kalimba tines, the wine glass as measured; the low-note detune of the usual resonator measured (+2.57 cents at A0); Hertz's law for velocity | Seeded by `at` alone (the ratchet fault); a Lagrange read on the string, which loses level at high pitch; 24 modes | **The mode tables** (BELL, CHIME, KALIMBA, GLASS), STRETCH with 50 = as published, the Hertz test, the sources table |
| **hands** | **The player**: preset 0 MARIMBA, every setting doing something on every MODEL, MOTOR for vibes, a DICE master. Found that **a user preset is copied over the old patch** (`app/synth_spec.cpp:46-50`): an appended setting would keep the last preset's value. The damping law `a + b·f²` from Chaigne & Doutaut | Re-strikes on a glide by default | Preset 0, MOTOR, DICE master, defaults-first `loadPreset`, the damping law, 2 fade slots, the shared synth test loops |

## 0. Sources

**Not every source was opened.** Where a claim waits on a page, batch 0 fills
it or marks the claim SELF, as the JX-3P plan's batch 0 does.

| Short | Source | Used for | Opened? |
| --- | --- | --- | --- |
| **KS** | Karplus & Strong, "Digital synthesis of plucked-string and drum timbres", *Computer Music Journal* 7(2):43-55, 1983 | The string loop | Batch 0 |
| **JS** | Jaffe & Smith, "Extensions of the Karplus-Strong plucked-string algorithm", *CMJ* 7(2):56-69, 1983 | Allpass tuning, pick-position comb, decay stretching | Batch 0 pages |
| **PASP** | J. O. Smith, *Physical Audio Signal Processing*, CCRMA online | Phase delay of the loop filter, dispersion | Online |
| **VHKJ** | Välimäki, Huopaniemi, Karjalainen, Jánosy, "Physical modeling of plucked string instruments…", *JAES* 44(5), 1996 | Loop filter from T60 | Batch 0 pages |
| **F64** | Fletcher, "Normal vibration frequencies of a stiff piano string", *JASA* 36:203, 1964 | `f_n = n·f0·√(1 + B·n²)`; B 1e-4 to 1e-3 in piano strings | Quoted |
| **VDS** | Van Duyne & Smith, ICMC 1994; Rauhala & Välimäki, *IEEE SPL* 13(5), 2006 | Dispersion allpasses for a stiff string | Batch 0 |
| **FR** | Fletcher & Rossing, *The Physics of Musical Instruments*, 2nd ed., 1998 | Bars §2.15 and ch. 19, membranes ch. 3, guitar body ch. 9, bells ch. 21 | **Not opened**: batch 0 fills a page for every FR claim |
| **R00** | Rossing, *Science of Percussion Instruments*, 2000, ch. 3 | Xylophone 1:3, vibes 1:4:10, measured glockenspiel 1:2.71:5.15 | Batch 0 |
| **YR** | Yoo & Rossing, marimba bars (acoustics.org/2704-2) | Marimba mode 2 at **4.0** (undercut bar) | Online |
| **Ch** | Chapman, *Canadian Acoustics* 2006 (jcaa 1805); ASA 155th summary | Kalimba tine mode 2 measured **5.3 to 5.9** × mode 1 (the ideal clamped bar says 6.267) | Online |
| **Hib** | Bill Hibbert, hibberts.co.uk: church bells; tubular bells; modes of a tubular bell | Church bell partials at −2400, −1200, −900, −500, 0 cents from the nominal; **the strike note about an octave below the nominal** ("The strike note of bells", fetched in the review). Tubular bell partials 2-7 at **0.65, 1.25, 1.99, 2.86, 3.81, 4.86** × strike pitch (217 tubes, average) | Online |
| **Cs** | Csound manual, "Modal frequency ratios", after Rossing | Wine glass **1, 2.32, 4.25, 6.63, 9.38** | Online; batch 0 finds Rossing's original |
| **CD** | Chaigne & Doutaut, "Numerical simulations of xylophones. I.", *JASA* 101(1):539-557, 1997 | Damping `1/τ = a + b·f²` for wood; mallet contact about a millisecond | Batch 0 pages |
| **Hz** | Hertz contact, as in Johnson, *Contact Mechanics*, 1985 | **Contact time ∝ v^(−1/5)**: a harder hit is shorter, so brighter | Batch 0 |
| **AS** | Abramowitz & Stegun, table 9.5 (Bessel zeros) | Membrane ratios, recomputed in the test | Formula |
| **GS** | Gordon & Smith, "A sine generation algorithm for VLSI applications", ICMC 1985 | Resonators that stay in tune at low pitch | Formula |
| **MI** | Mutable Instruments Rings and Elements manuals | The control set: STRUCTURE, BRIGHTNESS, DAMPING, POSITION | Docs only, no code |
| **AC** | Ableton Collision pack page | Resonators: beam, marimba, string, membrane, plate, pipe, tube | Docs only |

## 1. What PHYSIC is

**A synth that copies how real things ring, not how oscillators sound.** A
note is a strike: something hits or plucks a string, bar, bell or skin, and it
rings and dies away by itself. There is no ADSR (a hand-drawn volume shape).
DECAY is the object's own ringing time, and letting go damps it (RELEASE).

| Sounds | MODEL |
| --- | --- |
| Nylon or steel guitar, harp, koto, dulcimer (a mallet on a string), pizzicato, plucked bass | STRING |
| Marimba, xylophone, vibraphone, glockenspiel, celesta | MARIMBA, XYLO, VIBES, GLOCK |
| Kalimba, mbira, music box | KALIMBA |
| Church bell, handbell; tubular bells, wind chime | BELL; CHIME |
| Wine glass, a tapped bowl | GLASS |
| Toms, tuned hand drums | DRUM |
| Woodblock, temple blocks, log drum | WOOD |

**How a player uses it in a song**, with what the program already has:

| In the song | Using | What changes |
| --- | --- | --- |
| A marimba line | Preset 0, notes on the roll | Low notes ring longer and darker, as on a real marimba |
| A marimba **roll** | RATCHET on a note | The same bar struck again: **the hit adds to the ringing**, it does not start a fresh voice |
| Strummed guitar | Chord effect STRUM | Each key plucked a few ms apart, **each with its own roll** |
| A harp run | Chord effect arpeggio | |
| Bells far off | THROW to SPACE | |
| A hand-played kalimba | DICE up | Each note struck a little off where the last was, a cent or two out |
| Softer notes darker too | HUMAN LEVEL | Velocity lengthens the mallet's contact (Hz), so soft is darker as well as quieter |
| A slide guitar | GLIDE on STRING | The string's length slides while it rings |
| Moving tone | A modulator (`docs/plans/modulation-plan.md`: macros are lanes 0-7) on POSITION or MATERIAL | An LFO walking the mallet along the bar |

**What a turned control does to a ringing note** (the physics, and the hints
say so):

- **HARDNESS and POSITION act at the next strike**, as on a real instrument.
- **DECAY, MATERIAL, STRETCH (and BODY, from batch 10) act on notes already
  ringing**, sliding on each voice's 16-sample tick.

## 2. The models

### One engine with a MODEL choice (recommended)

| Option | For | Against |
| --- | --- | --- |
| **One synth, MODEL a setting** (recommended) | One `TrackKind`, one spec, one preset list. Strike, body, dice, glide and voices written once. A modulator can sweep MATERIAL across a marimba | Two cores inside one voice |
| A synth per family (MALLET, STRING, BELL) | Simpler voices | Three of everything in "Adding an instrument" (CLAUDE.md); the dice and strike three times |

### The voice

```
note -> STRIKE (PLUCK | MALLET; NOISE from batch 10), by velocity and the rolled HARDNESS
          -> STRING loop          (MODEL = STRING)
           | MODES, ≤ 12          (every other MODEL), gains from the rolled POSITION
                                  (TUBES, batch 10: mode 1 louder and longer, in the voice)
          -> RELEASE damping after let go
          -> WIDTH: each voice panned by its key (a gain pair a voice, set at note-on)
8 voices (+2 fading) summed into L / R -> BODY (batch 10, one a track, on the mid) -> LEVEL
```

Each part is **its own header with no SDL and no Rack, with its own test**, so
batches do not edit the same lines:

| Header | Holds | Test program |
| --- | --- | --- |
| `engine/include/apotheca/physic_rule.hpp` | The mode tables and sources, the shapes, the damping law, the tuning solve, the dice maths. Pure, no state | `physic_rule_test` (batch 0) |
| `engine/src/physic/modes.hpp` | The resonator bank, its tick, its sleep | `physic_test` |
| `engine/src/physic/string.hpp` | The KS loop, tuning, stiffness, pick comb | `physic_string_test` |
| `engine/src/physic/strike.hpp` | PLUCK, MALLET, NOISE, the Hertz rule | `physic_test` |
| `engine/src/physic/body.hpp` | BODY, WIDTH | `physic_test` |
| `engine/src/physic.cpp`, `engine/include/apotheca/physic.hpp` | Voices, the steal, the seed, wiring | `physic_test` |

### The mode tables (frequency ÷ the played note)

**The played note is the pitch the ear hears**, which is not always the lowest
mode (a bell's hum is two octaves under it; a tubular bell has no partial at
its pitch at all).

| MODEL | Ratios | Source | Kind |
| --- | --- | --- | --- |
| MARIMBA | **1, 4.0, 10.0**, then free-bar modes above | YR (4.0); FR ch. 19 (10, "three octaves and a major third"). **Some sources quote 9.2 for the third**; the test names its source | FAITHFUL 3 modes, SELF above |
| XYLO | **1, 3.0**, 6.0 | R00 ch. 3 (a twelfth); 6.0 SELF | FAITHFUL 2 |
| VIBES | **1, 4.0, 10.0** | R00 ch. 3 | FAITHFUL 3 |
| GLOCK | **1, 2.7565, 5.4039, 8.9330, 13.3443, 18.6372** | Uniform free-free bar, `cos k·cosh k = 1` (FR §2.15), recomputed in the test. R00's measured bar (1 : 2.71 : 5.15) is written beside it | FAITHFUL (formula) |
| KALIMBA | 1, **5.6**, 17.55, 34.39 | Clamped-free bar `cos k·cosh k = −1` (FR §2.15) gives 6.267 for mode 2; **real tines measure 5.3-5.9 (Ch)**, so 5.6 | FAITHFUL (Ch for mode 2, formula above) |
| BELL | Played note = **the strike note, an octave under the nominal** (where the prime sits): hum **0.5**, prime **1.0**, tierce **2·2^(−900/1200) = 1.18921**, quint **2·2^(−500/1200) = 1.49831**, nominal **2.0**, superquint 3.0, octave nominal 4.0. Written in `physic_rule.hpp` as cents from the nominal and worked out in double, never rounded by hand | Hib ("The strike note of bells", fetched by the review: the strike note is "about an octave below its nominal partial", the virtual pitch "roughly the half nominal"); FR ch. 21. The tierce and quint figures are Hibbert's −900 and −500 cents from the nominal | FAITHFUL 5 modes; the upper two SELF until batch 0 reads Hib's table |
| CHIME | **0.65, 1.25, 1.99, 2.86, 3.81, 4.86** | Hib, 217 tubular bells averaged. The heard pitch is a virtual pitch from partials 4-6, near 2:3:4 | FAITHFUL. **No mode at the played pitch, on purpose** |
| GLASS | **1, 2.32, 4.25, 6.63, 9.38** | Cs after Rossing (measured). The thin-ring formula gives 2.83, 5.42: written beside it as the ideal | FAITHFUL (measurement, batch 0 finds the original) |
| DRUM | **1, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173, 3.1555, 3.5001** | Ideal circular membrane, Bessel zeros (AS; FR ch. 3), recomputed in the test. A real tom is changed by its air (FR ch. 18): not modelled | FAITHFUL (formula) |
| WOOD | 1, 1.5, 2.4, 3.6, short decays | **No published set found** | **SELF**. The device does not call it measured |
| STRING | Harmonics stretched by `√(1 + B·n²)` | F64 | FAITHFUL (formula) |

**STRETCH** (0-100) bends a modal model's ratios: **50 plays the table above
exactly** (a FAITHFUL test), below 50 pulls them towards whole numbers, above
50 stretches them (`ratio^(1 + s)`, SELF). This is Rings' STRUCTURE (MI). On
STRING, STRETCH sets B from 0 (at 0) to 2e-3 (at 100), the piano's range and a
little more (F64), and reads "B 0.0004".

### Strike POSITION

Where a thing is struck decides which modes wake. A mode's gain is its shape's
height at that point.

| MODEL | Shape | Fact a test holds |
| --- | --- | --- |
| STRING | `sin(nπx)`, by a comb on the pluck, `x[n] − x[n − p·N]` (JS) | Plucked at 1/5, harmonics 5 and 10 at least 30 dB down. FAITHFUL |
| MARIMBA, XYLO, VIBES, GLOCK, CHIME | Free-free beam shapes, a 33-point table made in `prepare` | Struck at the centre, mode 2 (anti-symmetric) at least 30 dB down. Struck at 0.224 of the length, mode 1 (a node there) at least 30 dB under mode 2. FAITHFUL (FR §2.15) |
| KALIMBA | Clamped-free shapes | |
| DRUM | Mode (n,m): `J_n(j_nm·r)` along the radius (each mode its own Bessel order; the angle is fixed, so `cos nθ` is 1) | Struck at the centre, only the (0,m) modes sound: every J_n with n ≥ 1 is 0 at r = 0. FAITHFUL (AS) |
| BELL, GLASS, WOOD | No published shape: a smooth tilt from the low modes to the high | SELF |

### Damping: DECAY and MATERIAL

- **One law, from CD**: each mode dies at `1/τ_k = a + b·f_k²`, f_k in Hz.
  τ is the time to fall by 1/e; T60 (the time to fall 60 dB) is 6.9 τ.
- **DECAY sets `a` alone: the longest any mode rings**, `a = 6.9 / T60` with
  T60 = **0.05 × 600^(v/100) s**, 50 ms to 30 s, reading "1.2 S". `a` is
  never worked out from `b`, so it is never negative.
- **MATERIAL sets `b`**: 100 reads METAL (`b` = 0: every mode of every key
  rings for DECAY), 0 reads WOOD (high modes, and high keys, die fast: wood
  "thunks"). **So on METAL, DECAY is every note's T60; toward WOOD a high
  note rings shorter than DECAY**, as a real xylophone's top bars do. That is
  the law, not a fault, and the hint says "the longest it rings". CD's law
  is FAITHFUL; the values of `b` at each end are SELF until batch 0 reads
  CD's fits. CHIME and BELL need no "played mode" for this: every mode has
  its own τ from its own frequency.
- **A voice sleeps at −120 dB, which is about 2 × T60** after its strike: a
  30 s DECAY keeps a voice working for about 60 s, not 30.
- **STRING** takes the same two settings into its loop: DECAY sets the loop
  gain (stretched per key, JS, so a T60 means the same on every key), MATERIAL
  the loop low-pass.

### Two cores, held in tune

**The modes: a coupled-form resonator, not the usual two-pole filter.** The
usual form keeps its pitch in `cos ω`, which is so close to 1 at low pitch that
a float cannot hold it finely:

| Pitch | Usual two-pole, in float | Coupled form (GS) |
| --- | --- | --- |
| 20 Hz | **−7.8 cents** (computed) | < 0.001 |
| 27.5 Hz (A0) | **+2.57 cents** (scratch, measured) | 0.000 |
| 55 Hz | +0.42 (scratch) | 0.000 |
| 440 Hz | +0.01 (scratch) | 0.000 |

The coupled form turns a 2-D point by a fixed angle each sample and shrinks it
a little: 4 multiplies and 2 adds, **the same cost** (scratch: 0.48% against
0.47% for 8 × 12). BELL's hum is an octave under the note and CHIME's first
partial 0.65 of it, so low modes do happen. Mode frequencies are worked out
in double at note-on.

**Every mode snaps on its own.** On each 16-sample tick, a mode whose state
(both coordinates) is under 1e-15 is set to exact zeros; a mode at zero with
no strike still feeding it is skipped. The voice's own sleep (below) is not
enough: toward WOOD the high modes die many times faster than mode 1, and
they would sit in subnormals while the voice is still loud. Scratch (the
review's run, 8 voices × 8 modes, 50 ms decay, 20 s, flush-to-zero off):
**63% of a core, against 0.41% with it on**.
**A mode's gain fades between 0.40 and 0.45 of the sample rate**, so a glide
across the limit does not click; QMOD cuts at 0.45 (`engine/src/qmod.cpp:301`).

**The string: the whole loop delay is solved at the note's pitch.** A KS
string's pitch is the sample rate over its *total* delay at f0: the line, the
loop low-pass's phase delay and the allpasses'. At note-on and on every tick
of a glide:

1. Work out in double the phase delay of the low-pass and of the stiffness
   allpasses at f0 (PASP), and take it from SR/f0.
2. Split the rest into whole samples N and a fraction d in [0.5, 1.5).
3. Set the tuning allpass from the closed form
   `a = sin(ω(1−d)/2) / sin(ω(1+d)/2)`, ω = 2π·f0/SR (JS).

| Scratch, MIDI 24-108, the partial's phase over 60 periods | Worst |
| --- | --- |
| Whole samples, loop filter ignored | **88 cents** (note 106) |
| Exact allpass, loop filter subtracted | **0.16 cents** |
| ...with two stiffness allpasses | 0.18 cents |

**Allpass, not a Lagrange read.** A 4-point Lagrange read is as well tuned
(0.18 cents, scratch) but loses level at high pitch by an amount that hangs on
the fraction: at fraction 0.5 it passes 0.884 at a quarter of the sample rate
(computed), so some keys ring duller and shorter than their neighbours. The
allpass passes every frequency whole, so DECAY is exact on every key.

**What the allpass costs: its fraction cannot jump.** When N changes by one in
a glide, a naive switch spikes. Scratch, an octave glide, worst second
difference ÷ its RMS:

| Glide | Spike |
| --- | --- |
| Allpass, naive switch | **9.5** |
| Allpass, its state set from the new tap at the switch | **3.9** |
| Lagrange (no state) | 3.9 |

**Recommended: the allpass with its state reset.** As smooth as Lagrange by
this measure, and DECAY stays exact. **Nobody has listened to it.**

**The string's own small numbers**: the loop's filter states and the tuning
allpass snap under 1e-20, and a line whose last period is all under `kQuiet`
is set to exact zeros (as PING's lines), so a sleeping string is zeros.

**Range.** The string's line is **8192 floats a voice slot** (32 KB; 10 slots
320 KB, allocated in the constructor): 5.9 Hz at 48 kHz, so any glide from key
0 fits. PAIR (batch 10) gives each slot a second line: **640 KB a track**,
7.7 MB for twelve. **The sounding pitch** (key + 12·OCTAVE + FINE) **is held
at MIDI 112 on STRING** (above it the loop is under 9 samples) **and at 127
on the modal models** (above it the played mode passes 0.45 of the sample
rate and is faded out). Tests say both.

### The strike

| STRIKE | What | Physics |
| --- | --- | --- |
| **MALLET** | A half-sine push of contact time T. HARDNESS sets T from **8 ms (yarn) to 0.3 ms (brass)** | Spectrum falls off above about 1/T. **Velocity shortens T by v^(−1/5)** (Hz; used by CD): a harder hit is brighter as well as louder. FAITHFUL law, SELF range |
| **PLUCK** | STRING: one period of noise, low-passed by HARDNESS (finger to plectrum), through the pick comb. Modal: a 0.1 ms click, how a tine is plucked | KS, JS |
| **NOISE** (batch 10, STRIKE value 2, appended) | A burst of filtered noise, 2 ms to 2 s with HARDNESS: brushes, a breath into a glass, a shimmer | SELF (Collision's noise exciter is the model, AC) |

- **The noise is a hash of (the note's seed, samples since the strike)**, never
  a generator stepped along. So it does not depend on block cuts, helpers or
  how long the program has run, and the same note is the same pluck.
- A MALLET on STRING is a hammered dulcimer: the pulse goes into the loop.
- **No BOW in version 1** (the owner's call). A bow is a nonlinear friction
  model run every sample, it is neither plucked nor struck, and "decay" and
  "glide" mean other things for it. A long NOISE gives a cheap bowed glass.
  A bow later is STRIKE 3, appended.

### BODY (batch 10; one a track, after the voices)

| BODY | What | Source |
| --- | --- | --- |
| NONE | | |
| GUITAR | 3 fixed modes: the air mode near 100 Hz, top plate near 200 and 400 Hz | FR ch. 9; **figures SELF until batch 0** |
| BOX | 2 modes near 180 and 420 Hz (a kalimba or music box box) | SELF |
| TUBES | **Done in each voice, not after the sum**: mode 1 louder and longer, as a marimba's tuned resonator tube. It is on the BODY choice only because that is where a player looks for it | FR ch. 19; the boost is SELF |

GUITAR and BOX: at most 3 modes, run on the mid (L + R) after the voices are
summed and panned, added back to both sides, about an eighth of a voice. It
sleeps with the track.

## 3. Settings, macros, dice

### Settings (ids appended from here, never reordered)

Every patch here is whole numbers (`app/synth_spec.hpp`, `values` returns
`int*`). A setting of 12 steps or fewer is drawn as a choice
(`app/synth_screen.hpp:93`).

| Id | Setting | Range | Reads | Page |
| --- | --- | --- | --- | --- |
| 0 | MODEL | 0-10 | STRING, MARIMBA, XYLO, VIBES, GLOCK, KALIMBA, BELL, CHIME, GLASS, DRUM, WOOD | SOUND |
| 1 | STRIKE | 0-1 (NOISE appended as 2 in batch 10) | PLUCK, MALLET | SOUND |
| 2 | HARDNESS | 0-100 | % (contact 8 ms to 0.3 ms; pluck brightness) | SOUND |
| 3 | POSITION | 0-100 | 1% to 50% along: "1/7" where it is near one | SOUND |
| 4 | DECAY | 0-100 | 50 MS to 30 S: the longest it rings | RING |
| 5 | MATERIAL | 0-100 | WOOD 0 … METAL 100 | RING |
| 6 | STRETCH | 0-100 | 50 AS MADE; STRING: B | RING |
| 7 | RELEASE | 0-100 | 10 MS to 3 S after let go; **100 reads RING** (let go does nothing, as a marimba has no damper) | RING |
| 8 | VELOCITY | 0-100 | How much velocity moves the level and, by Hertz, HARDNESS. **0 = velocity ignored: every note full** (so HUMAN LEVEL does nothing there). Default 70 | PLAY |
| 9 | OCTAVE | −2..2 | | PLAY |
| 10 | FINE | −50..50 | CENTS | PLAY |
| 11 | WIDTH | 0-100 | Low keys left, high keys right, as a marimba stands | PLAY |
| 12 | LEVEL | −24..6 | DB | PLAY |
| 13 | DICE | 0-100 | Scales the four below | DICE |
| 14 | DICE POSITION | 0-100 | At 100: ±25 points | DICE |
| 15 | DICE HARDNESS | 0-100 | At 100: ±30 points | DICE |
| 16 | DICE TUNE | 0-100 | At 100: ±15 cents | DICE |
| 17 | DICE DECAY | 0-100 | At 100: ±30% | DICE |

**Appended in batch 10** (ids 18-21, a new page BODY):

| Id | Setting | Range | Reads |
| --- | --- | --- | --- |
| 18 | PAIR | 0-20 | cents. STRING: a second string a voice, detuned (a 12-string, a piano's course): **doubles the string's cost and memory**. Modal: the lowest two modes split into a beating pair, as a real bell's doublets (FR ch. 21, page in batch 0) |
| 19 | MOTOR | 0-100 | OFF, then a tremolo up to 8 Hz (vibes). Its phase starts at each note-on |
| 20 | BODY | 0-3 | NONE, GUITAR, BOX, TUBES |
| 21 | BODY MIX | 0-100 | % |

Each is 0 (off) in every version 1 preset and in a song from before batch
10, so an old song plays as it did (the file's `settings` line is read by
position; PHYSIC's `loadPreset` sets the defaults first, section 8).

- **Every setting does something on every MODEL** (hands), so no row is ever
  dead on the screen.
- **MODEL has room for two more before it stops being a choice**: a setting
  is a choice while `max − min <= kChoiceRange` (12, `app/synth_screen.cpp:129`),
  so 0..12, 13 models, is the last. A 14th model needs the screen told that
  MODEL is a choice whatever its range. Said here so it is not found late.

### The 8 macros (automation lanes 0-7; 0.5 = as the patch)

Below 0.5 a macro slides the setting from the patch's value to its least,
above 0.5 to its most, as the JX-3P plan's rule.

| Lane | Macro | Moves | Heard |
| --- | --- | --- | --- |
| 0 | TONE | HARDNESS | At the next strike |
| 1 | POSITION | POSITION | At the next strike: the best target for an LFO |
| 2 | DECAY | DECAY | At once, sliding over 20 ms |
| 3 | MATERIAL | MATERIAL | At once, 20 ms: wood to metal in one sweep |
| 4 | STRETCH | STRETCH | At once, on the 16-sample tick (ringing modes retune; the string's fraction is solved again) |
| 5 | RELEASE | RELEASE | At the next let go: a modulator can choke the notes or let them ring |
| 6 | DICE | DICE | At the next strike: how "played by hand" |
| 7 | OCTAVE | DOWN / AS PRESET / UP at 0.25 and 0.75, as Phazed and QMOD | At the next note |

**Written to be modulation targets** (`docs/plans/modulation-plan.md`):

- **`setMacro` stores a number and returns**, early on an equal value. No
  coefficient is worked out on the call. That plan measured QMOD's `setMacro`
  (it regenerates the sound, `engine/src/qmod.cpp:215`) at about 1 µs a call;
  PHYSIC's should cost nothing that shows.
- **Coefficients are worked out on each voice's own 16-sample tick**, counted
  in samples since its strike, only when a target moved.
- OCTAVE stays a threshold choice, which that plan says is not a target.

### The dice

**The synth is never told "where and which time" today**: `noteOn(note,
velocity)` is all it gets (`engine/include/apotheca/instrument.hpp:19`). So
batch 0 adds one realtime-safe call:

```cpp
// The next noteOn / noteOnFrom's roll. A synth with no dice ignores it.
virtual void noteSeed(std::uint32_t seed) { (void)seed; }
```

- **Added in batch 4, not batch 0**: nothing can check a seed before the dice
  use it (review, section 13 #10).
- `Engine::startNote` calls it just before `noteOnFrom` / `noteOn`
  (`engine/src/engine.cpp:551-553`), after the `telling_` return (`:509`).
- **The seed is the strike's own moment, not the note's.** `startNote` gets
  one new last argument, `strikeAt` (steps since Play), **used for the seed
  and nothing else**, so `at` and everything that reads it (the trail,
  `p.at` at `:765` / `:778`) stay as they are. Three callers would otherwise
  roll alike:

  | Caller | Today | `strikeAt` |
  | --- | --- | --- |
  | Ratchet in `startNote` (`engine.cpp:532`) | every piece the note's `at` | `at + k·apart` |
  | Chord ratchet in `startChordNote` (`:690-706`) | every piece the chord's `at` | `at + k·apart` |
  | Arpeggio, `fireArps` (`:747`) | `at` = 0 for every note, so one roll a key for good | the arpeggio note's own moment in steps since Play (the arpeggio's start plus `m · gap`; an `Arp` field if it keeps none today, holding no scene number) |
  | Everyone else | | `at` |

  Then `chordSeed(track, 0, pitch, llround(strikeAt · 256))`
  (`engine/include/apotheca/chord_rule.hpp:121`), read through `chordSigned`
  with a new `ChordRoll` value **`kRollStrike = 7`** (`:128`). **6 is the
  modulation plan's `kRollMod`** (`docs/plans/modulation-plan.md:173`),
  which comes first in the owner's order; if PHYSIC lands first it writes
  `kRollMod = 6` into the enum too, unused, so neither number moves.
- Strummed keys share a moment but not a pitch, so each gets its own roll.
- **No counter** and no generator walked: the roll holds no scene number,
  is the same on any helper count and block length, and comes out again on
  Play from the top and in a bounce.
- **Each time round the loop the rolls are new**, as HUMAN TIME and LEVEL are
  (steps since Play move on). Seeding from the cell's bar and step instead
  makes them the same every time round: **the owner's call**, one line.
- **Live notes** (MIDI and X auditions, `engine/src/engine.cpp:1146`) get a
  seed from a per-track count, which `allNotesOff` puts back. Live play is not
  repeatable and the plan says so.
- **Phazed, QMOD, Twin and the sampler do not override it**, so nothing they
  play changes; a test holds the pinned checksums.

What a note rolls: POSITION, HARDNESS, TUNE and DECAY, each
`chordSigned(seed, kRollStrike, i) × its DICE row × DICE`.

**Y on MAIN rolls a new patch** through the spec's `roll` hook
(`app/synth_spec.hpp:31`, QMOD's near `app/synth_spec.cpp:237`): HARDNESS,
POSITION, DECAY, MATERIAL, STRETCH and STRIKE (BODY MIX from batch 10),
inside the current MODEL. MODEL, levels and the dice amounts are left. Whether Y may change MODEL
too is **the owner's call** (recommended no).

## 4. Voices, re-strikes, glide, velocity

| Thing | Recommended | Why |
| --- | --- | --- |
| Voices | **8 sounding + 2 fade slots** | 8 as QMOD (`engine/include/apotheca/qmod.hpp:59`). A stolen voice moves to a fade slot and fades over 3 ms, counted in samples; three steals inside 3 ms cut the oldest fade short. Slots are swapped by index, nothing copied |
| Which voice | The same key if it rings; else a sleeping voice (lowest index); else **the quietest** (peak read on the 64-sample tick; ties by index), **never one still inside its strike** (a mallet's contact, up to 8 ms, or a pluck's first period) unless all eight are | With sounds that die away, the quietest is the one nobody misses. A voice mid-strike reads quiet only because its peak is still coming. QMOD steals the oldest |
| A ringing key struck again, modal | **The same voice, the new hit added to the ringing** | The engine sends `noteOff` first (`engine/src/engine.cpp:541`); a bar is linear, so two hits add. Ratchets become a real roll |
| A ringing key struck again, STRING | **Damp 2 ms (loop gain to 30%), then pluck** | A plectrum meets the string before it plucks. SELF. The alternative, add the pluck, is the owner's call |
| Let go | RELEASE's T60, reached over 5 ms. **RING (100) ignores let go** | Mallets and bells default to RING, strings to about 40 |
| **Stop** | **Every voice is let go, as by `noteOff`**: RELEASE damps it, and RING rings on until it sleeps | **A bounce sends Stop when the song's last sample has played** and then records the tail (`app/bounce.cpp:269-272`): damped, every bell and string on the last bar was cut in 30 ms. Needs the engine to tell Stop from Play: `Instrument::stopNotes()`, default `allNotesOff()`, called by `letGoOfNotes` for Stop only (batch 5). Every other synth keeps what it does |
| `allNotesOff` (Play, panic) | **Every voice damped in 30 ms**, then asleep | Play from the top must be the same performance; panic must be quiet. Nothing else runs free (MOTOR starts at each note), so a song played again after the old sound has slept is the same to the bit. **A Stop then Play inside a ring: the first 30 ms after Play hold the old tail**: said, and tested as such |
| Sleep | A voice whose peak over a 64-sample tick, counted from its strike, stays under **1e-6 (−120 dB)**, its strike done, has every state set to **exact zeros** and does no work. Filter states snap under 1e-20 | The house rule: no number too small to hold with flush-to-zero off, and a voice woken is a fresh one. Scratch: without it, 15 to 150 times the cost. **The voice's sleep is on top of each mode's own snap** (section 2), not instead of it |
| Chords | 8 voices take an 8-key chord; STRUM and arpeggio are the step clock's | |
| **GLIDE, STRING** | `noteOnFrom` (`instrument.hpp:21`): **a lone note gliding from a voice that still rings at `from` takes it over** and its length slides, with no new pluck (a slide guitar). Otherwise it plucks at `from` and slides | The owner's call: take over (recommended) or always pluck again |
| **GLIDE, modal** | The same take-over; **every mode slides by the same ratio**, a bar or bell that bends (not physical, but a synth must glide) | Angles from a polynomial sin/cos on the tick, linear between ticks |
| **When take-over is allowed** | All three: (1) **one note of its moment names that `from`**: the notes that arrive before one `render()` are one moment (a hit always lands on a chunk's first sample), so `noteOnFrom` only queues (8 places, no allocation) and the queue is settled at the top of `render()`; if two or more name the same `from` (a chord under GLIDE FROM TOP), **none takes over: every key strikes at `from` and slides**, so a chord is never one silent key and the rest struck. (2) **The voice at `from` is louder than −40 dB** (peak 0.01, on its tick) and past its strike; a faint tail is struck afresh. (3) It is not in a fade slot | The review: with TOP every key of a chord has the same `from`, and a voice can be "sounding" at −100 dB after a FULL glide or a RING tail. QMOD never takes over (`qmod.cpp:249-253`) |
| **Velocity on a take-over** | The voice's level slides over 5 ms from its own velocity's level to the new note's, as Velocity below | Else HUMAN LEVEL did nothing on a taken-over note |
| Glide timing | **On the voice's own 16-sample tick, counted from its note** | QMOD moves its slide once a block (`Slide::take(frames)`, `engine/src/qmod.cpp:273`, `engine/include/apotheca/slide.hpp:25`), so its glide hangs on where blocks are cut. PHYSIC calls `take(16)` on its own tick, so a bounce matches live play. `slide.hpp` is not changed |
| Velocity | Level `(1 − V) + V·v`, and contact time `× v^(−V/5)` (V = VELOCITY ÷ 100): at V = 0 velocity changes nothing, at 100 the Hertz law in full | HUMAN LEVEL then darkens soft notes too. Default V 0.7 so it is heard |

## 5. Presets: 30, in six folders

**Preset 0 is MARIMBA**, the new-track sound. Each preset's stated figures
(decay, pitch, peak under 1, asleep after its tail) are tested. **Nobody has
heard any of them.** Names and folders are the owner's call.

**Batch 8 makes the 22 that need nothing from batch 10; batch 10 adds the
8 marked †** (they need BODY, PAIR, MOTOR or NOISE). A preset's place in
the list is only what the player sees, so the † ones go in beside their
folder.

| Folder | Presets |
| --- | --- |
| MALLETS | **MARIMBA**, SOFT MARIMBA (yarn), XYLOPHONE, VIBES† (MOTOR on, RELEASE 0.4 s: the damper bar), VIBES STILL, GLOCKENSPIEL, CELESTA† (GLOCK + BOX) |
| STRINGS | NYLON† (GUITAR body), STEEL, HARP, KOTO (plucked at 0.08), PIZZ, DULCIMER† (MALLET, PAIR 5), TWELVE† (PAIR 7), PLUCKED BASS (OCTAVE −1; BOX† when it comes) |
| TINES | KALIMBA† (BOX), MUSIC BOX (OCTAVE +1, hard) |
| BELLS | CHURCH BELL (RING), HANDBELL, TUBULAR BELL, WINE GLASS, CRYSTAL (GLASS, STRETCH up) |
| WOOD AND SKIN | WOODBLOCK, TEMPLE BLOCKS, LOG DRUM, TOM, HAND DRUM† (NOISE) |
| DICE | RAIN (GLOCK, DICE 80), WIND CHIME (CHIME, RING, every DICE high), LOOSE STRINGS (DICE TUNE 60), DRUNK MARIMBA |

PLUCKED BASS is in version 1 without its BOX; batch 10 does **not** change it
(a preset a song names must keep its sound), it adds PLUCKED BASS BOX†
instead. So 22 + 8 = 30.

## 6. Cost

**Scratch timings**: throwaway programs on a 2.3 GHz Xeon container, `-O2`,
48 kHz, two runs (the designers', then the review's). **They are not scaled
to the owner's desk**: the same container's `bench` read Phazed 1.3% and 1.7%,
QMOD 2.8% and 3.3%, Twin x1 5.6% and 4.9% across the two runs, against 0.9 /
2.6 / 4.7 on the desk. That is 1.27× for one synth and 1.9× for another, so
there is no one factor. Only `bench` on the desk is a figure; these say
which part costs most.

| Part, 8 voices | Designers' run | Review's run |
| --- | --- | --- |
| Coupled-form modes, 4 / 8 / 12 / 16 a voice | 0.17 / 0.32 / 0.48 / 0.64 | 0.20 / 0.38 / 0.56 / 0.74 |
| 8 × 8 modes left to decay 20 s, flush-to-zero off | **1.9-2.6** (from 0.13-0.16) | **63** (from 0.41 with it on) |
| Strings, allpass tuning | 0.12 | 0.09 |
| ...with two stiffness allpasses | 0.22 | 0.11 |

**Where the cost goes: modes a voice.** So each MODEL uses only what it needs
(MARIMBA, VIBES, XYLO 3-6; KALIMBA 4; GLASS 5; CHIME 6; BELL 7, 9 with PAIR;
DRUM 8; GLOCK 6), **the cap is 12** (one constant), and modes above 0.45 of
the sample rate are dropped. PAIR doubles a string. The strike, the gain slide,
the pan and the body are a guessed +0.1-0.2%.

| Budget, 8 notes sounding | Desk | Handheld (desk × 5, a guess) |
| --- | --- | --- |
| **Target, any model** | **≤ 0.8%** | ≤ 4% |
| Hard ceiling (BELL with PAIR, or TWELVE, all ringing, a FULL glide on every note) | 1.2% | 6% |
| Asleep | no work | no work |

- **Sleeping is most of the saving.** A marimba note sleeps within twice its
  DECAY (−120 dB). **A 30 s DECAY on 8 voices costs the full figure for about
  60 s.**
- **The mode loop is written four modes at a time by hand**: modes in groups
  of 4 (a group padded with zero modes), four running sums kept apart in the
  loop and added in a fixed order at the end of each sample. The handheld
  builds at plain `-O2 -mcpu=cortex-a53` with no `-ffast-math`
  (`scripts/build-handheld.sh:69`), so the compiler will not reorder one float
  sum across modes into NEON by itself. Batch 1 looks at the handheld build's
  code for four-wide multiplies (`fmla v.4s`) in the loop and says whether it
  found them. The fixed order keeps the sound the same on every block cut.
- **`bench` gets its own PHYSIC rows, not the shared `instrument()`**
  (`app/bench_run.cpp:42-63`): that one sends 8 notes once and times 600
  blocks, 3.2 s, in which a marimba rings down and sleeps, so it would time
  mostly sleeping voices. PHYSIC's rows **strike all 8 notes again every
  half second** through the timing, at DECAY 100 (30 s), and **fail if any
  voice slept** during it (a count of awake voices read after each block).
  Four rows beside `app/bench_run.cpp:385`: "PHYSIC, 8 notes" (MARIMBA),
  "PHYSIC string, 8 notes" (STEEL in version 1, TWELVE from batch 10), "PHYSIC
  worst" (CHURCH BELL, DICE 100, a FULL glide on every note; with PAIR and
  BODY from batch 10), "PHYSIC asleep". `instrument()` is not changed, so the
  other rows read as they did.
- **SPEED TEST runs on the handheld after batch 2**, and the owner reads the
  rows. Until then the ×5 is a guess.

## 7. The DEVICE screen

One `SynthSpec` (`app/synth_spec.hpp:13`) served by the shared `SynthScreen`.
**No new drawing and no restyle.** `envelopePage` is false on every page:
there is no ADSR.

| Page | Settings |
| --- | --- |
| MAIN | Preset and folder, the 8 macros. **Y rolls a new patch** |
| SOUND | MODEL, STRIKE, HARDNESS, POSITION |
| RING | DECAY, MATERIAL, STRETCH, RELEASE |
| PLAY | VELOCITY, OCTAVE, FINE, WIDTH, LEVEL |
| DICE | DICE, DICE POSITION, DICE HARDNESS, DICE TUNE, DICE DECAY |
| BODY (batch 10) | PAIR, MOTOR, BODY, BODY MIX |

- Values read in real units: "2.4 S", "80 MS", "±8 CENTS", "1/7", "B 0.0004",
  "DB". Upper case, plain words.
- Every value checked at XL by `textsize`; the DICE page's long names too
  ("DICE HARDNESS" may need `fonts.within`).
- Stills `physic`, `physic0`..`physic4` in `app/main.cpp`, and in `kViews`
  (`tests/stills.hpp`), or `textviews` fails.

## 8. Song file and ids

| Thing | Value |
| --- | --- |
| `TrackKind` | `Physic`, **appended** after `Qmod` (`app/project.hpp:193`) |
| `SynthKind` | `Physic`, appended (`engine/include/apotheca/instrument.hpp:41`); `Engine::addSynthTrack` gets its line (`engine/src/engine.cpp:436`) |
| Instruments row (`app/instruments.cpp`) | `{TrackKind::Physic, "PHYSIC", "PHYS", "PHYSIC", "strings, bars and bells that ring", true, "physic", SynthKind::Physic, ".physic"}`. **The word `physic` never changes.** Its place in the list: **last**, after QMOD (the owner's call) |
| `TrackData` | `PhysicPatch physic;` beside `QmodPatch qmod` (`app/project.hpp:218`): 18 ints, 22 from batch 10 (sized for 22 from the start, the last four 0) |
| File lines | The usual `preset`, `macros`, `settings` (`app/song_file.cpp:177`, read at `:489-494`). `settings` is by position: an appended setting goes at the end, and an older file's missing ones keep the preset's values |
| **So** | **A new setting's value in every preset must play the old sound** |
| User presets | Today a user preset is copied **over the patch already on the track** (`app/synth_spec.cpp:46-50`, Phazed's; QMOD's the same at `:203`), so an appended setting would keep the last preset's value. **PHYSIC's `loadPreset` sets the factory defaults first, then copies the user's values.** Tested |
| App | `physic_` beside `qmod_` (`app/app.cpp:18,32`); `userList`, `builtInPresets`, `settingCount`, `specFor` (`app/synth_spec.cpp:13-14, 271-287`) |
| An older program | Reads `track physic` as an unknown word: the track is empty there. The usual rule, nothing new |

**Collision with the JX-3P branch:** both touch `TrackKind`, `SynthKind`,
`kInstruments`, `userList` and the synth loops in the tests. Whichever joins
second rebases those few lines by hand. The modulation branch also appends
to `ChordRoll`: it takes 6, PHYSIC 7 (section 3).

**Which shared test loops get PHYSIC (batch 5)**: the plain synth loops,
`tests/engine_test.cpp:2092`, `tests/glide_test.cpp:1103` and
`tests/chordfx_test.cpp:886`. **Not** `tests/effects_test.cpp:364`: that is
`effectsSong`, the song pinned to the bit by checksums, which are remade
only from code known to be right; a fourth synth in it changes every one.
**Not** `tests/throw_test.cpp:807`, the same three-synth song inside a throw
test. PHYSIC gets **its own song and its own checksum** in `physic_test`
(`testPhysicSong`: drums, a PHYSIC track of each core, chords, ratchets,
glides, THROW to both buses, DICE 100), and its own THROW check there. Its
checksum is taken with `--print` only after batch 5's FAITHFUL tests pass,
and remade only by the same rule as the pinned song's.

## 9. Tests

`tests/physic_rule_test.cpp`, `tests/physic_test.cpp` and
`tests/physic_string_test.cpp`, each its own ctest entry with `TIMEOUT 120`.
The measuring tools live in **`tests/measure.hpp`**, which the JX-3P plan's
batch 0 also makes: **whichever lands first makes it, the other uses it.**

| Test | Holds | Kind |
| --- | --- | --- |
| `testTuning` | The **sounding** pitch (key + 12·OCTAVE + FINE) swept over STRING 24-108 and each modal model 12-120, reached with OCTAVE −2..2 and FINE ±50 as well as by key; STRETCH 0 and 100; PAIR on from batch 10: **within 0.5 cent** of equal temperament, by phase over 60 periods. **The pitch is the mode at the played note** (BELL: the prime). **CHIME has none**: its six partials are each held at their table ratio × the note within 0.5 cent; the heard virtual pitch is not measured (there is no model of the ear here) | FAITHFUL |
| `testHeldAtTheEdge` | Sounding pitch above 112 on STRING plays 112; above 127 on a modal model plays 127; below the bottom of each range, as low as the line allows, with no claim of 0.5 cent | House rule |
| `testModeRatios` | Each table recomputed from its equation (beam, Bessel) in the test, **BELL from Hibbert's cents** (tierce 1.18921, not a rounded 1.2); each model's spectrum peaks within 0.5 cent of the table at STRETCH 50 | FAITHFUL; WOOD SELF |
| `testPosition` | Pluck at 1/5: harmonics 5, 10 ≥ 30 dB down. Bar at centre: mode 2 ≥ 30 dB down. Bar at 0.224: mode 1 ≥ 30 dB under mode 2. Drum at centre: every (n ≥ 1, m) mode ≥ 30 dB down (J_n(0) = 0) | FAITHFUL |
| `testDecay` | **At MATERIAL 100 (METAL, b = 0)**: every mode's T60 within 5% of DECAY's seconds, 5 settings × 5 keys; no key more than 15% off its neighbours (an uneven fractional delay); STRING's fundamental the same at every MATERIAL. RELEASE the same after let go; RING ignores let go. **Toward WOOD no T60 is longer than DECAY's** (the law only shortens) | SELF |
| `testMaterial` | Fitted `1/τ_k` follows `a + b·f²` within 5% | Law FAITHFUL (CD), values SELF |
| `testHertz` | Contact time against velocity on a log-log fit: slope −0.2 ± 0.01; spectral centroid rises with velocity | FAITHFUL |
| `testStretchFifty` | STRETCH 50 is the table to the bit | FAITHFUL |
| `testBlocks` | A phrase with glides, macros moved, a re-strike, a steal, sleep and waking, in blocks of 1, 7, 17, 48, 64, 256, 333: **the same to the bit** | House rule |
| `testPlayFromTheTop` | Play, Stop, wait until asleep, Play: the same bits; two fresh engines the same; on 0 and 3 helpers | House rule |
| `testSeeds` (batch 4) | **A listening instrument records every seed `noteSeed` hands it** (as `chordfx_test`'s listening synth records notes), through the engine: two pieces of a note's ratchet differ; two pieces of a chord's ratchet differ; **an arpeggio's notes on one key differ from one time round to the next**; two keys of a strum differ; the same key at the same moment, Play twice, gives the same seed; a scene move and a bounce keep them | House rule |
| `testRollsDiffer` | The same through PHYSIC's sound: the rolled position and tune differ where the seeds do | SELF |
| `testTooSmall` | Flush-to-zero **off**: no subnormal in the output or **any mode's state, scanned every tick**, over a 60 s tail of every model, **at MATERIAL 0 on a high key** (where high modes die first, the voice still loud) as well as at METAL; the run also timed, and failed if it takes more than 3 times as long as the same run with flush-to-zero on | House rule |
| `testNaN` | NaN or infinite velocity, `from`, glide seconds, setting or macro: ignored or clamped, output finite (as Phazed, `engine/src/phazed.cpp:293`) | House rule |
| `testSilence` | No notes in: exact zeros out; a sleeping voice does no work (a counter) | House rule |
| `testNoAllocation` | A counted `new` (as `nextscene_test` has): 0 after `prepare`, through note-on, steal, glide, macros, `allNotesOff` | House rule |
| `testSteal` | 12 overlapping notes: the quietest goes, **never a voice inside its strike**; no step at a steal larger than the 3 ms fade allows | SELF |
| `testStopRings` (batch 5) | Stop: RING notes ring on and sleep in their own time, RELEASE notes damp by RELEASE; Play and panic damp in 30 ms. **A bounce with a CHURCH BELL on the last step keeps its tail** (through `BounceTail`, as `throwapp_test` does). Every other synth's `stopNotes` is its `allNotesOff`: pinned song the same | House rule |
| `testTakeOver` (batch 5) | A lone glide from a loud ringing voice takes it over (no new strike); a 4-key chord under GLIDE FROM TOP: **all four strike**; from a voice under −40 dB: a new strike; a taken-over note's level follows its velocity | SELF |
| `testPhysicSong` (batch 5) | PHYSIC's own song (section 8) to its own checksum, and loudness and peak where the maths library differs (`mathsPrint`) | House rule |
| `testRestrike` | A MARIMBA ratchet builds level; a STRING ratchet damps, then plucks | SELF |
| `testGlideClicks` | An octave glide: worst second difference ÷ RMS **≤ 5** (naive was 9.5); a take-over glide starts no new pluck | SELF |
| `testMacrosCheap` | `setMacro` with an equal value does nothing; with a new one, no coefficient changes before the next tick | SELF |
| `testOldSynthsUnchanged` | With `noteSeed` (batch 4) and `stopNotes` (batch 5) in: `effects_test --print`, the pinned song, Phazed and QMOD renders the same to the bit | House rule |
| `testEveryPreset` / `testEveryRoll` | As `qmod_test`: each makes sound, peaks under 1, sleeps, holds its stated figures | SELF |
| App | `app_test`, `save_test`: a song with every setting; a user preset over another keeps the defaults; `textsize`, `textviews`, `themeviews` | House rule |

**Mutation testing is the standard.** After each batch, break each of these
and see a test fail: drop the loop filter's delay from the tuning; the naive
allpass switch; the usual two-pole resonator; `take(frames)` per block; the
seed without the piece's moment (each of the three callers by itself); no
sleep; no per-mode snap; `setMacro` doing the work; the position shape (J₀ for
every drum mode); BELL a rounded tierce; take-over on a chord; take-over from
a faint voice; Stop damping; the steal taking a striking voice; `loadPreset`
without the defaults first.

## 10. Order of work

One agent a batch. Each ends with `ctest` green under `set -o pipefail` and
its mutants killed.

| Batch | What | Tests | Done when |
| --- | --- | --- | --- |
| **0** | **Groundwork, no engine change.** A page for every FR, JS, VHKJ, CD, R00 and Hib claim, or the claim marked SELF. `tests/measure.hpp`: **if the JX-3P's batch 0 has landed, use its file** (it moves the pitch reader, spectrum and level reader out of `effects_test`); if not, make that same move here, the same way, so whichever lands second finds one file and adds to it (T60, subnormal scan, counted `new`). `physic_rule.hpp` with the tables (BELL from cents), shapes (J_n), damping law, tuning solve and dice maths | `physic_rule_test` (ratios recomputed, the tuning solve, J_n(0) = 0); `effects_test` unchanged by the move | Every figure has a page or a SELF mark; one copy of the tools |
| 1 | **Skeleton.** `physic.hpp/.cpp`, the 18 settings (struct sized for 22) and 8 macros with ranges, 8 + 2 slots, the steal (not a striking voice), sleep, the per-mode snap, `allNotesOff`, `physic/modes.hpp` written four modes at a time, MALLET, **MARIMBA only**. `SynthKind::Physic`, the factory, PHYSIC's own bench rows (notes struck again, no voice asleep) | Tuning, ratios, decay, blocks, Play twice, too small (MATERIAL 0, high key), NaN, silence, no allocation, steal (MARIMBA) | A marimba plays in tune through the engine; bench under 0.5%; whether the handheld build has four-wide multiplies is said |
| 2 | **Every modal model**, the shapes, MATERIAL, STRETCH, the 0.45 fade, mode counts, the pitch held at 127 | Ratios, position, material, STRETCH 50 for all, held at the edge | Worst modal row under the ceiling; **SPEED TEST on the handheld** |
| 3 | **STRING**, `physic/string.hpp`: the loop, allpass tuning with state reset, loop filter from T60, decay stretch, stiffness, PLUCK, pick comb, re-strike, its snaps, held at 112 | `physic_string_test` | 0.5 cent, 24-108 |
| 4 | **Strike and dice.** `Instrument::noteSeed`, `startNote`'s `strikeAt` from all three callers (both ratchet loops, the arpeggio), `kRollStrike = 7` (and `kRollMod = 6` if the modulation branch has not landed); velocity by Hertz scaled by VELOCITY, the four rolls, DICE, Y's `roll` maths | `testSeeds` (the listening instrument), Hertz, rolls differ, Play twice with DICE 100, `testOldSynthsUnchanged` | Old sound to the bit; every seed mutant killed |
| 5 | **Playing.** Glide (both cores; take-over only for a lone note from a loud voice, settled at the top of `render()`; velocity on take-over), cheap `setMacro` and the tick, RELEASE and RING, **`Instrument::stopNotes` and Stop letting go**, WIDTH per voice. PHYSIC in the plain shared loops (engine, glide, chord effects, PAIRED over a 4-key chord); **its own song and checksum** | Glide clicks, take-over, Stop rings and the bounce keeps it, macros cheap, blocks again, the shared loops, `testPhysicSong`, `testOldSynthsUnchanged` | Pinned song unchanged; PHYSIC's song pinned |
| 7a | **App: song and files.** `TrackKind`, the row, `PhysicPatch`, the spec with defaults-first `loadPreset`, `.physic`, `settingCount` | `app_test`, `save_test` | A song saves and loads |
| 7b | **App: screens.** Pages SOUND, RING, PLAY, DICE, `settingText`, `macroText`, Y, `App::screenNow()`, stills in `main.cpp` and `kViews` | `textsize` at XL, `textviews`, `themeviews`, `words` | Pages draw at every size and theme |
| 8 | **The 22 version 1 presets** | Every preset holds its figures | |
| 9 | **Docs** for version 1: README's PHYSIC page, CLAUDE.md (Adding an instrument, Measured, the instrument's rules) | `ctest -R words` | **Version 1 is whole here** |
| 10 | **Second step, when the owner says so**: settings 18-21 (PAIR, MOTOR, BODY, BODY MIX), STRIKE 2 NOISE, `physic/body.hpp`, the BODY page, the 8 † presets, the bench rows with PAIR and BODY | Body modes, PAIR beats at its cents, PAIR's cost, MOTOR from each note, NOISE by hash, an old PHYSIC song plays as it did, `textviews` | Every bench row measured again |

Batches 2 and 3 can run side by side after 1 (their own headers and tests).
7a can start after 1. (There is no batch 6: BODY and PAIR moved to 10.)

## 11. Risks

| Risk | What is done about it |
| --- | --- |
| **"Sounds like a marimba" cannot be checked by the owner** | Only measured facts are claimed: ratios, cents, T60, position nulls, the Hertz slope. Timbre is SELF, and said so |
| FR, CD, JS and VHKJ were not opened | Batch 0 finds the pages; anything without one is SELF |
| Some tables are ideal formulas, not instruments (GLOCK, DRUM, upper KALIMBA) | Marked FAITHFUL to the formula only; the measured figures are written beside them |
| WOOD, BOX, the mallet range, DECAY's scale and the ends of MATERIAL have no source | SELF, one table each, set from recordings later if the owner wants |
| Marimba's third: 9.2 or 10 across sources | One number; the test names FR's 10 |
| The ×5 handheld factor | SPEED TEST after batch 2; the 12-mode cap is one constant |
| Long bells stacking up | 8 voices, the quietest stolen: cost is bounded at 8 whatever plays |
| Tiny numbers with flush-to-zero off (15× to 150× the cost, scratch) | Every mode snaps on its own each tick, the voice sleeps into exact zeros; `testTooSmall` at MATERIAL 0 on a high key, timed |
| Glide clicks only measured, not heard | `testGlideClicks` bounds them |
| The first 30 ms after Play may hold the old tail | Said; the test waits for sleep. A hard cut instead would click |
| Stop lets RING notes ring, so a live Stop is not silence at once | That is what a bounce needs (its tail is recorded after Stop). Panic still damps in 30 ms. The owner's call #13 |
| Merge with the modulation branch | `ChordRoll` 6 is theirs, 7 PHYSIC's, whoever lands first |
| The scratch cost figures disagree with each other | Not scaled to the desk; `bench` on the desk and SPEED TEST are the only figures |
| `noteSeed` touches the engine for every synth | Default does nothing; pinned checksums held |
| Merge with the JX-3P branch | Same enum and table lines; rebase by hand; one `measure.hpp`, made by whichever batch 0 lands first the same way |
| A 14th MODEL turns the choice into a bar | Said in section 3; the screen is told before it happens |

## 12. The owner's calls

| # | Call | Recommended |
| --- | --- | --- |
| 1 | Name | **PHYSIC** (the owner's, 2026-11-14) |
| 2 | One synth with MODEL, or several | **One**, 11 models |
| 3 | Which models ship | The 11 in section 2; more appended later |
| 4 | BOW | **Not in version 1**; later as STRIKE 3 |
| 5 | Voices | **8** (+2 fade slots); one constant |
| 6 | A ringing STRING struck again | **Damp 2 ms, then pluck** (alternative: add the pluck, as the modal models do) |
| 7 | GLIDE on a ringing note | **A lone note from a voice louder than −40 dB takes it over, no new strike; a chord's keys and faint tails strike again** (alternative: always strike again) |
| 8 | GLIDE on modal models | **All modes bend together** (alternative: modal models ignore glide) |
| 9 | Dice new each time round, or the same every time round | **New each time**, as HUMAN TIME and LEVEL |
| 10 | Y rolls a new patch; may it change MODEL | **Yes it rolls; no**, MODEL stays |
| 11 | HARDNESS and POSITION move a ringing note | **No**, next strike only (the physics) |
| 12 | RELEASE default for mallets and bells | **RING** |
| 13 | What Stop does to ringing notes | **Lets them go: RELEASE damps, RING rings out** (a bounce records the tail). Play and panic damp in 30 ms |
| 14 | Steal the quietest voice, or the oldest | **Quietest** |
| 15 | Macro 7: OCTAVE (as Phazed and QMOD) or RELEASE | **OCTAVE**; RELEASE is macro 5 (BODY MIX has none) |
| 16 | Version 1 without BODY, PAIR, MOTOR and NOISE, added as batch 10 | **Yes**: 18 settings and 22 presets first; PAIR is still in, as batch 10 |
| 17 | MOTOR's phase from each note-on (repeatable) or free-running (as real vibes) | **From each note-on** |
| 18 | Place in the instrument list | **Last**, after QMOD |
| 19 | Preset 0 | **MARIMBA** |
| 20 | Preset names and folders | Section 5 |
| 21 | WIDTH by pitch on by default | **30% in MALLETS presets**, 0 elsewhere |
| 22 | The played note on BELL: the strike note (an octave under the nominal, as Hibbert) or the nominal | **The strike note**, so a bell on C sounds as a founder's C bell |
| 23 | DECAY on wood: "the longest it rings" (high notes shorter, by the law) or every note's own T60 | **The longest it rings** |
| 24 | VELOCITY's default | **70** (0 would leave HUMAN LEVEL doing nothing) |

**Not done:** no code was written or built in the repo. The FR book and the
JS, VHKJ, CD and R00 texts were not opened. Every timing and tuning figure
marked scratch came from the designers' throwaway programs on a container,
not `bench` on the owner's desk.

## 13. Decisions on the review (2026-11-14)

| # | Point | Decision |
| --- | --- | --- |
| 1 | Batch 5 would put PHYSIC into the pinned song (`effects_test.cpp:364`) and the throw test's song (`throw_test.cpp:807`) | **Accepted.** Checked: both are the three-synth pinned song. PHYSIC goes only into the plain loops; it gets its own song and checksum (`testPhysicSong`, section 8), taken after its FAITHFUL tests pass |
| 2 | Stop damps every ringing note in 30 ms, so a bounce cuts the last bar's bells | **Accepted.** Checked: `bounce.cpp:269-272` sends Stop, then records the tail. A new `Instrument::stopNotes()`, default `allNotesOff()`, for Stop only; PHYSIC's lets every voice go (RELEASE, RING rings out). Play and panic still damp. `testStopRings` bounces a bell on the last step. Call #13 changed |
| 3 | Arpeggio notes reach `startNote` with `at` = 0, and the chord ratchet loop passes one `at` to every piece | **Accepted.** `startNote` gets a `strikeAt` used for the seed only (so `at` and old sound are untouched); the note ratchet, the chord ratchet and the arpeggio each pass their own moment. `testSeeds` holds each of the three |
| 4 | `kRollStrike = 6` clashes with the modulation plan's `kRollMod = 6` | **Accepted.** PHYSIC takes 7; 6 stays the modulation plan's whichever lands first |
| 5 | BELL an octave too high; tierce and quint rounded | **Accepted.** The played note is the strike note (the prime); ratios 0.5, 1, 1.18921, 1.49831, 2, 3, 4, worked from Hibbert's cents in double. Call #22 |
| 6 | Glide take-over fails for chords, faint voices and velocity | **Accepted.** Take-over only for a lone note of its moment (settled at the top of `render()`), from a voice louder than −40 dB and past its strike; the level slides to the new velocity. `testTakeOver`. Call #7 reworded |
| 7 | DECAY and the damping law contradict each other | **Accepted.** DECAY sets `a` alone, "the longest it rings"; MATERIAL's `b` only shortens. On METAL DECAY is every note's T60; `testDecay` holds that at METAL and "never longer" elsewhere. The sleep at −120 dB is about 2 × T60, and the cost text says 60 s for a 30 s DECAY. Call #23 |
| 8 | Small numbers cost 150×, and whole-voice sleep does not protect the high modes | **Accepted.** Every mode snaps on its own each tick; both scratch factors are written; `testTooSmall` runs at MATERIAL 0 on a high key, scans every mode, and is timed |
| 9 | The bench rows would time sleeping voices; the scaling is not one factor | **Accepted.** PHYSIC's own rows strike again every half second at DECAY 30 s and fail if a voice slept; `instrument()` unchanged. The "a third" correction is dropped: scratch figures are not scaled, only `bench` on the desk counts |
| 10 | Batch 0 changes the engine and cannot check the seed | **Accepted.** `noteSeed` and `strikeAt` move to batch 4 with a listening instrument that records seeds. Batch 0 changes no engine code. `measure.hpp` is made by moving `effects_test`'s tools, as the JX-3P batch 0 does, by whichever lands first |
| 11 | `testTuning` asks for notes outside the plan's ranges; CHIME has no pitch to measure | **Accepted.** Ranges are of the sounding pitch after OCTAVE and FINE; above them the pitch is held (112 STRING, 127 modal) and `testHeldAtTheEdge` says so. CHIME's partials are held to their ratios; its virtual pitch is not measured, and the plan says so |
| 12 | The drum's shape should be J_n, not J₀ | **Accepted.** J_n(j_nm·r) per mode; the centre test now follows from J_n(0) = 0 |
| 13 | Contradictions: per-voice pan, TUBES, VELOCITY 0, MODEL's limit, PAIR's memory, stealing a striking voice | **Accepted, all five.** WIDTH is a gain pair a voice before the sum; TUBES is done in the voice; VELOCITY 0 ignores velocity (default 70, call #24); MODEL has room for 13 (`max − min <= 12`); PAIR is 640 KB a track; the steal skips a voice inside its strike |
| 14 | The NEON claim may not hold without fast-math | **Accepted.** The mode loop keeps four sums by hand in a fixed order; batch 1 looks for four-wide multiplies in the handheld build and says what it found |
| 15 | The plan is large for one instrument | **Accepted in part.** BODY, PAIR, MOTOR and NOISE move to batch 10, after version 1 (18 settings, 22 presets) is whole; their ids are appended then, not reserved now, so nothing is stored that might be taken back. Macro 5 is RELEASE instead of BODY MIX, so no lane waits on batch 10. **Not taken further**: the 11 models stay in version 1 (each is one table row in the same core, and batch 2 measures them all), and the DICE page stays (dice are what the owner asked for). Call #16 |

