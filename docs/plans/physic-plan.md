# PHYSIC plan

Written 2026-11-14 from the code at 0.455 (branch `physicplan`). Three
designers each wrote a plan ("sound", "engine", "hands"); this one is joined
from them. **Nothing here is built and nobody has heard anything.** The code
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

One strike (pluck, mallet or noise), one body, one set of dice and one voice
handler serve both. This is the split Mutable Instruments Rings and Ableton
Collision use (sources **MI**, **AC**).

| Question | Answer |
| --- | --- |
| What it is | **Plucked and struck sounds**: strings, marimba, xylophone, vibes, glockenspiel, kalimba, bells, chimes, glass, drums, woodblock. You pick what is hit (MODEL), what hits it (STRIKE, HARDNESS) and where (POSITION) |
| Models | **11** on one choice: STRING, MARIMBA, XYLO, VIBES, GLOCK, KALIMBA, BELL, CHIME, GLASS, DRUM, WOOD. Each modal model is one row of mode ratios with a source (section 2). WOOD alone is SELF |
| New track sound | **Preset 0 is MARIMBA**, so "add a track, place notes" gives a marimba line with nothing chosen on DEVICE |
| Voices | **8 sounding, plus 2 slots that only fade out a stolen voice** (no click). One constant |
| Tuning | **Within 0.5 cent**, STRING from MIDI 24 to 108, every modal model from 12 to 120. Scratch: a plain KS loop was **up to 88 cents out**; the fixed loop 0.16 cents |
| Dice | **Per note**: strike position, hardness, a few cents of tune and the decay are rolled, each by its own amount, **from where and which time** (the track, the strike's moment since Play, the key). Y on MAIN rolls a new patch, as QMOD's |
| Cost target | **≤ 0.8% of a desk core for 8 notes**, ceiling 1.2% (Phazed 0.9%, QMOD 2.6%: CLAUDE.md, Measured). Handheld about 4% (×5, a guess). Scratch: 8 voices × 12 modes 0.48%, 8 strings 0.22% |
| The trap | **Tiny numbers.** Scratch: the same modes left to decay with flush-to-zero off cost **about 15 times as much** (0.13-0.16% became 1.9-2.6%). Every voice sleeps into exact zeros |
| Engine change | **One virtual call, `Instrument::noteSeed(uint32_t)`**, made just before `noteOn` in `Engine::startNote`. Other synths ignore it, so every pinned sound stays the same to the bit |
| Song file | `track physic`, presets `.physic`, settings appended only |
| First thing to build | **Batch 0**: the sources with pages, the shared measuring tools, a pure rule header with the mode tables, and `noteSeed` |

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
| **Hib** | Bill Hibbert, hibberts.co.uk: church bells; tubular bells; modes of a tubular bell | Church bell partials at −2400, −1200, −900, −500, 0 cents from the nominal, and the strike note set mainly by the nominal. Tubular bell partials 2-7 at **0.65, 1.25, 1.99, 2.86, 3.81, 4.86** × strike pitch (217 tubes, average) | Online |
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
- **DECAY, MATERIAL, STRETCH, BODY act on notes already ringing**, sliding on
  each voice's 16-sample tick.

## 2. The models

### One engine with a MODEL choice (recommended)

| Option | For | Against |
| --- | --- | --- |
| **One synth, MODEL a setting** (recommended) | One `TrackKind`, one spec, one preset list. Strike, body, dice, glide and voices written once. A modulator can sweep MATERIAL across a marimba | Two cores inside one voice |
| A synth per family (MALLET, STRING, BELL) | Simpler voices | Three of everything in "Adding an instrument" (CLAUDE.md); the dice and strike three times |

### The voice

```
note -> STRIKE (PLUCK | MALLET | NOISE), by velocity and the rolled HARDNESS
          -> STRING loop          (MODEL = STRING)
           | MODES, ≤ 12          (every other MODEL), gains from the rolled POSITION
          -> RELEASE damping after let go
8 voices (+2 fading) summed -> BODY (one a track) -> WIDTH -> LEVEL -> L / R
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
| BELL | Played note = **nominal**: hum 0.25, prime 0.5, tierce 0.6, quint 0.75, nominal 1.0, superquint 1.5, octave nominal 2.0 | Hib (and FR ch. 21). Hibbert: the strike note is set mainly by the nominal | FAITHFUL 5 modes; the upper two SELF until batch 0 reads Hib's table |
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
| DRUM | `J₀(j·r)` along the radius | Struck at the centre, only the (0,m) modes sound. FAITHFUL (AS) |
| BELL, GLASS, WOOD | No published shape: a smooth tilt from the low modes to the high | SELF |

### Damping: DECAY and MATERIAL

- **One law, from CD**: each mode dies at `1/τ_k = a + b·f_k²`. τ is the time
  to fall by 1/e; T60 (the time to fall 60 dB) is 6.9 τ.
- **DECAY sets `a`**: the played note's T60, **0.05 × 600^(v/100) s**, 50 ms
  to 30 s, reading "1.2 S".
- **MATERIAL sets `b`**: 0 reads WOOD (high modes die fast: wood "thunks"),
  100 reads METAL (`b` near 0, everything rings). CD's law is FAITHFUL; the
  values of `b` at each end are SELF until batch 0 reads CD's fits.
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
0.47% for 8 × 12). CHIME and BELL put modes two octaves under the note, so low
modes do happen. Mode frequencies are worked out in double at note-on.
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

**Range.** The string's line is **8192 floats a voice slot** (32 KB; 10 slots
320 KB, allocated in the constructor): 5.9 Hz at 48 kHz, so any glide from key
0 fits. Above MIDI 112 the loop is under 9 samples and the pitch is held there
(a test says so). A modal model plays any key.

### The strike

| STRIKE | What | Physics |
| --- | --- | --- |
| **MALLET** | A half-sine push of contact time T. HARDNESS sets T from **8 ms (yarn) to 0.3 ms (brass)** | Spectrum falls off above about 1/T. **Velocity shortens T by v^(−1/5)** (Hz; used by CD): a harder hit is brighter as well as louder. FAITHFUL law, SELF range |
| **PLUCK** | STRING: one period of noise, low-passed by HARDNESS (finger to plectrum), through the pick comb. Modal: a 0.1 ms click, how a tine is plucked | KS, JS |
| **NOISE** | A burst of filtered noise, 2 ms to 2 s with HARDNESS: brushes, a breath into a glass, a shimmer | SELF (Collision's noise exciter is the model, AC) |

- **The noise is a hash of (the note's seed, samples since the strike)**, never
  a generator stepped along. So it does not depend on block cuts, helpers or
  how long the program has run, and the same note is the same pluck.
- A MALLET on STRING is a hammered dulcimer: the pulse goes into the loop.
- **No BOW in version 1** (the owner's call). A bow is a nonlinear friction
  model run every sample, it is neither plucked nor struck, and "decay" and
  "glide" mean other things for it. A long NOISE gives a cheap bowed glass.
  A bow later is STRIKE 3, appended.

### BODY (one a track, after the voices)

| BODY | What | Source |
| --- | --- | --- |
| NONE | | |
| GUITAR | 3 fixed modes: the air mode near 100 Hz, top plate near 200 and 400 Hz | FR ch. 9; **figures SELF until batch 0** |
| BOX | 2 modes near 180 and 420 Hz (a kalimba or music box box) | SELF |
| TUBES | Per voice, not per track: mode 1 louder and longer, as a marimba's tuned resonator tube | FR ch. 19; the boost is SELF |

At most 3 modes, mono into WIDTH, about an eighth of a voice. It sleeps with
the track.

## 3. Settings, macros, dice

### Settings (ids appended from here, never reordered)

Every patch here is whole numbers (`app/synth_spec.hpp`, `values` returns
`int*`). A setting of 12 steps or fewer is drawn as a choice
(`app/synth_screen.hpp:93`).

| Id | Setting | Range | Reads | Page |
| --- | --- | --- | --- | --- |
| 0 | MODEL | 0-10 | STRING, MARIMBA, XYLO, VIBES, GLOCK, KALIMBA, BELL, CHIME, GLASS, DRUM, WOOD | SOUND |
| 1 | STRIKE | 0-2 | PLUCK, MALLET, NOISE | SOUND |
| 2 | HARDNESS | 0-100 | % (contact 8 ms to 0.3 ms; pluck brightness; noise length) | SOUND |
| 3 | POSITION | 0-100 | 1% to 50% along: "1/7" where it is near one | SOUND |
| 4 | DECAY | 0-100 | 50 MS to 30 S | SOUND |
| 5 | MATERIAL | 0-100 | WOOD 0 … METAL 100 | SOUND |
| 6 | STRETCH | 0-100 | 50 AS MADE; STRING: B | RING |
| 7 | RELEASE | 0-100 | 10 MS to 3 S after let go; **100 reads RING** (let go does nothing, as a marimba has no damper) | RING |
| 8 | PAIR | 0-20 | cents. STRING: a second string a voice, detuned (a 12-string, a piano's course): **doubles the string's cost**. Modal: the lowest two modes split into a beating pair, as a real bell's doublets (FR ch. 21, page in batch 0) | RING |
| 9 | MOTOR | 0-100 | OFF, then a tremolo up to 8 Hz (vibes). Its phase starts at each note-on | RING |
| 10 | BODY | 0-3 | NONE, GUITAR, BOX, TUBES | RING |
| 11 | BODY MIX | 0-100 | % | RING |
| 12 | VELOCITY | 0-100 | How much velocity moves HARDNESS (Hz) as well as level. 0 = level only | PLAY |
| 13 | OCTAVE | −2..2 | | PLAY |
| 14 | FINE | −50..50 | CENTS | PLAY |
| 15 | WIDTH | 0-100 | Low keys left, high keys right, as a marimba stands | PLAY |
| 16 | LEVEL | −24..6 | DB | PLAY |
| 17 | DICE | 0-100 | Scales the four below | DICE |
| 18 | DICE POSITION | 0-100 | At 100: ±25 points | DICE |
| 19 | DICE HARDNESS | 0-100 | At 100: ±30 points | DICE |
| 20 | DICE TUNE | 0-100 | At 100: ±15 cents | DICE |
| 21 | DICE DECAY | 0-100 | At 100: ±30% | DICE |

- **Every setting does something on every MODEL** (hands), so no row is ever
  dead on the screen.
- **MODEL has room for one more before it stops being a choice**: 11 values
  now, 12 is the limit (`kChoiceRange`). A 13th model needs the screen told
  that MODEL is a choice whatever its range. Said here so it is not found late.

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
| 5 | BODY | BODY MIX | At once, 20 ms |
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

- `Engine::startNote` calls it just before `noteOnFrom` / `noteOn`
  (`engine/src/engine.cpp:551-553`), after the `telling_` return (`:509`).
- **The seed is the strike's own moment, not the note's.** A ratchet's pieces
  are all started with the note's `at` (`engine/src/engine.cpp:532`), so a
  seed of `at` alone would roll every piece the same (found by the engine
  design). The piece's moment is `at + k·apart` steps; the ratchet loop
  passes it down. Then `chordSeed(track, 0, pitch, llround(moment · 256))`
  (`engine/include/apotheca/chord_rule.hpp:121`), read through `chordSigned`
  with a new `ChordRoll` value **`kRollStrike = 6` appended** (`:128`).
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
(`app/synth_spec.hpp:31`, QMOD's at `app/synth_spec.cpp:242`): HARDNESS,
POSITION, DECAY, MATERIAL, STRETCH, BODY MIX and STRIKE, inside the current
MODEL. MODEL, levels and the dice amounts are left. Whether Y may change MODEL
too is **the owner's call** (recommended no).

## 4. Voices, re-strikes, glide, velocity

| Thing | Recommended | Why |
| --- | --- | --- |
| Voices | **8 sounding + 2 fade slots** | 8 as QMOD (`engine/include/apotheca/qmod.hpp:59`). A stolen voice moves to a fade slot and fades over 3 ms, counted in samples; three steals inside 3 ms cut the oldest fade short. Slots are swapped by index, nothing copied |
| Which voice | The same key if it rings; else a sleeping voice (lowest index); else **the quietest** (peak read on the 64-sample tick; ties by index) | With sounds that die away, the quietest is the one nobody misses. QMOD steals the oldest |
| A ringing key struck again, modal | **The same voice, the new hit added to the ringing** | The engine sends `noteOff` first (`engine/src/engine.cpp:541`); a bar is linear, so two hits add. Ratchets become a real roll |
| A ringing key struck again, STRING | **Damp 2 ms (loop gain to 30%), then pluck** | A plectrum meets the string before it plucks. SELF. The alternative, add the pluck, is the owner's call |
| Let go | RELEASE's T60, reached over 5 ms. **RING (100) ignores let go** | Mallets and bells default to RING, strings to about 40 |
| `allNotesOff` (Play, Stop, panic) | **Every voice damped in 30 ms**, then asleep | A click-free stop. Nothing else runs free (MOTOR starts at each note), so a song played again after the old sound has slept is the same to the bit. **The first 30 ms after Play can hold the old tail**: said, and tested as such |
| Sleep | A voice whose peak over a 64-sample tick, counted from its strike, stays under **1e-6 (−120 dB)**, its strike done, has every state set to **exact zeros** and does no work. Filter states snap under 1e-20 | The house rule: no number too small to hold with flush-to-zero off, and a voice woken is a fresh one. Scratch: without it, 15 times the cost |
| Chords | 8 voices take an 8-key chord; STRUM and arpeggio are the step clock's | |
| **GLIDE, STRING** | `noteOnFrom` (`instrument.hpp:21`): **if a voice is still sounding at `from`, it is taken over and its length slides, with no new pluck** (a slide guitar). If none is, it plucks at `from` and slides | The owner's call: take over (recommended) or always pluck again |
| **GLIDE, modal** | The same take-over; **every mode slides by the same ratio**, a bar or bell that bends (not physical, but a synth must glide) | Angles from a polynomial sin/cos on the tick, linear between ticks |
| Glide timing | **On the voice's own 16-sample tick, counted from its note** | QMOD moves its slide once a block (`Slide::take(frames)`, `engine/src/qmod.cpp:273`, `engine/include/apotheca/slide.hpp:25`), so its glide hangs on where blocks are cut. PHYSIC calls `take(16)` on its own tick, so a bounce matches live play. `slide.hpp` is not changed |
| Velocity | Level `(1 − V) + V·v`, and contact time `× v^(−1/5)` scaled by VELOCITY | HUMAN LEVEL then darkens soft notes too |

## 5. Presets: 30, in six folders

**Preset 0 is MARIMBA**, the new-track sound. Each preset's stated figures
(decay, pitch, peak under 1, asleep after its tail) are tested. **Nobody has
heard any of them.** Names and folders are the owner's call.

| Folder | Presets |
| --- | --- |
| MALLETS | **MARIMBA**, SOFT MARIMBA (yarn), XYLOPHONE, VIBES (MOTOR on, RELEASE 0.4 s: the damper bar), VIBES STILL, GLOCKENSPIEL, CELESTA (GLOCK + BOX) |
| STRINGS | NYLON (GUITAR body), STEEL, HARP, KOTO (plucked at 0.08), PIZZ, DULCIMER (MALLET, PAIR 5), TWELVE (PAIR 7), PLUCKED BASS (OCTAVE −1, BOX) |
| TINES | KALIMBA (BOX), MUSIC BOX (OCTAVE +1, hard) |
| BELLS | CHURCH BELL (RING), HANDBELL, TUBULAR BELL, WINE GLASS, CRYSTAL (GLASS, STRETCH up) |
| WOOD AND SKIN | WOODBLOCK, TEMPLE BLOCKS, LOG DRUM, TOM, HAND DRUM (NOISE) |
| DICE | RAIN (GLOCK, DICE 80), WIND CHIME (CHIME, RING, every DICE high), LOOSE STRINGS (DICE TUNE 60), DRUNK MARIMBA |

## 6. Cost

**Scratch timings** (designers' throwaway code, 2.3 GHz Xeon container,
`-O2`, 48 kHz). The engine designer ran `bench` in the same session: Phazed
1.3%, QMOD 2.8%, Twin x1 5.6% there, against 0.9 / 2.6 / 4.7 on the owner's
desk (CLAUDE.md, Measured). So these run slow by about a third.

| Part, 8 voices | % of a core |
| --- | --- |
| Coupled-form modes, 4 / 8 / 12 / 16 a voice | 0.17 / 0.32 / 0.48 / 0.64 |
| The same 8 × 8, left to decay 20 s, flush-to-zero off | **1.9-2.6** (from 0.13-0.16) |
| Strings, allpass tuning | 0.12 |
| ...with two stiffness allpasses | 0.22 |

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

- **Sleeping is most of the saving.** A marimba note sleeps within its DECAY.
  But **a 30 s DECAY on 8 voices costs the full figure for 30 s.**
- The modes are laid out mode by mode (arrays) so the compiler can vectorise
  them; NEON on the handheld should take four at once. Not measured.
- **`bench` gets four rows** (beside `app/bench_run.cpp:385`): "PHYSIC, 8
  notes" (MARIMBA), "PHYSIC string, 8 notes" (TWELVE), "PHYSIC worst" (CHURCH
  BELL with PAIR, BODY on, DICE 100, a FULL glide on every note), "PHYSIC
  asleep".
- **SPEED TEST runs on the handheld after batch 2**, and the owner reads the
  rows. Until then the ×5 is a guess.

## 7. The DEVICE screen

One `SynthSpec` (`app/synth_spec.hpp:13`) served by the shared `SynthScreen`.
**No new drawing and no restyle.** `envelopePage` is false on every page:
there is no ADSR.

| Page | Settings |
| --- | --- |
| MAIN | Preset and folder, the 8 macros. **Y rolls a new patch** |
| SOUND | MODEL, STRIKE, HARDNESS, POSITION, DECAY, MATERIAL |
| RING | STRETCH, RELEASE, PAIR, MOTOR, BODY, BODY MIX |
| PLAY | VELOCITY, OCTAVE, FINE, WIDTH, LEVEL |
| DICE | DICE, DICE POSITION, DICE HARDNESS, DICE TUNE, DICE DECAY |

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
| `TrackData` | `PhysicPatch physic;` beside `QmodPatch qmod` (`app/project.hpp:218`): 22 ints |
| File lines | The usual `preset`, `macros`, `settings` (`app/song_file.cpp:177`, read at `:489-494`). `settings` is by position: an appended setting goes at the end, and an older file's missing ones keep the preset's values |
| **So** | **A new setting's value in every preset must play the old sound** |
| User presets | Today a user preset is copied **over the patch already on the track** (`app/synth_spec.cpp:46-50`, Phazed's; QMOD's the same at `:203`), so an appended setting would keep the last preset's value. **PHYSIC's `loadPreset` sets the factory defaults first, then copies the user's values.** Tested |
| App | `physic_` beside `qmod_` (`app/app.cpp:18,32`); `userList`, `builtInPresets`, `settingCount`, `specFor` (`app/synth_spec.cpp:13-14, 271-287`) |
| An older program | Reads `track physic` as an unknown word: the track is empty there. The usual rule, nothing new |

**Collision with the JX-3P branch:** both touch `TrackKind`, `SynthKind`,
`kInstruments`, `userList` and the synth loops in the tests. Whichever joins
second rebases those few lines by hand. The shared test loops that name every
synth (`tests/engine_test.cpp:2092`, `tests/glide_test.cpp:1103`,
`tests/throw_test.cpp:807`, `tests/chordfx_test.cpp:886`,
`tests/effects_test.cpp:364`) get PHYSIC in batch 5.

## 9. Tests

`tests/physic_rule_test.cpp`, `tests/physic_test.cpp` and
`tests/physic_string_test.cpp`, each its own ctest entry with `TIMEOUT 120`.
The measuring tools live in **`tests/measure.hpp`**, which the JX-3P plan's
batch 0 also makes: **whichever lands first makes it, the other uses it.**

| Test | Holds | Kind |
| --- | --- | --- |
| `testTuning` | STRING every key 24-108, each modal model 12-120, at OCTAVE ±2 and FINE ±50, STRETCH 0 and 100, PAIR on: the note's pitch **within 0.5 cent** of equal temperament, by phase over 60 periods. CHIME: its 2:3:4 of partials 4-6 | FAITHFUL |
| `testModeRatios` | Each table recomputed from its equation (beam, Bessel) in the test; each model's spectrum peaks within 0.5 cent of the table at STRETCH 50 | FAITHFUL; WOOD SELF |
| `testPosition` | Pluck at 1/5: harmonics 5, 10 ≥ 30 dB down. Bar at centre: mode 2 ≥ 30 dB down. Bar at 0.224: mode 1 ≥ 30 dB under mode 2. Drum at centre: no (1,1) | FAITHFUL |
| `testDecay` | The note's T60 within 5% of DECAY's seconds, 5 settings × 5 keys; no key more than 15% off its neighbours (an uneven fractional delay); RELEASE the same after let go; RING ignores let go | SELF |
| `testMaterial` | Fitted `1/τ_k` follows `a + b·f²` within 5% | Law FAITHFUL (CD), values SELF |
| `testHertz` | Contact time against velocity on a log-log fit: slope −0.2 ± 0.01; spectral centroid rises with velocity | FAITHFUL |
| `testStretchFifty` | STRETCH 50 is the table to the bit | FAITHFUL |
| `testBlocks` | A phrase with glides, macros moved, a re-strike, a steal, sleep and waking, in blocks of 1, 7, 17, 48, 64, 256, 333: **the same to the bit** | House rule |
| `testPlayFromTheTop` | Play, Stop, wait until asleep, Play: the same bits; two fresh engines the same; on 0 and 3 helpers | House rule |
| `testRollsDiffer` | Two ratchet pieces roll differently; two keys of a strum differ; the same key at the same moment rolls the same; the roll survives a scene move and a bounce | SELF |
| `testTooSmall` | Flush-to-zero **off**: no subnormal in the output or any state over a 60 s tail of every model (the `TooSmall` idea, `tests/effects_test.cpp:1638`) | House rule |
| `testNaN` | NaN or infinite velocity, `from`, glide seconds, setting or macro: ignored or clamped, output finite (as Phazed, `engine/src/phazed.cpp:293`) | House rule |
| `testSilence` | No notes in: exact zeros out; a sleeping voice does no work (a counter) | House rule |
| `testNoAllocation` | A counted `new` (as `nextscene_test` has): 0 after `prepare`, through note-on, steal, glide, macros, `allNotesOff` | House rule |
| `testSteal` | 12 overlapping notes: the quietest goes; no step at a steal larger than the 3 ms fade allows | SELF |
| `testRestrike` | A MARIMBA ratchet builds level; a STRING ratchet damps, then plucks | SELF |
| `testGlideClicks` | An octave glide: worst second difference ÷ RMS **≤ 5** (naive was 9.5); a take-over glide starts no new pluck | SELF |
| `testMacrosCheap` | `setMacro` with an equal value does nothing; with a new one, no coefficient changes before the next tick | SELF |
| `testOldSynthsUnchanged` | With `noteSeed` in: `effects_test --print`, the pinned song, Phazed and QMOD renders the same to the bit | House rule |
| `testEveryPreset` / `testEveryRoll` | As `qmod_test`: each makes sound, peaks under 1, sleeps, holds its stated figures | SELF |
| App | `app_test`, `save_test`: a song with every setting; a user preset over another keeps the defaults; `textsize`, `textviews`, `themeviews` | House rule |

**Mutation testing is the standard.** After each batch, break each of these
and see a test fail: drop the loop filter's delay from the tuning; the naive
allpass switch; the usual two-pole resonator; `take(frames)` per block; the
seed without the piece's moment; no sleep; `setMacro` doing the work; the
position shape; `loadPreset` without the defaults first.

## 10. Order of work

One agent a batch. Each ends with `ctest` green under `set -o pipefail` and
its mutants killed.

| Batch | What | Tests | Done when |
| --- | --- | --- | --- |
| **0** | **Groundwork.** A page for every FR, JS, VHKJ, CD, R00 and Hib claim, or the claim marked SELF. `tests/measure.hpp` (pitch by phase, spectrum peaks, T60, subnormal scan, counted `new`), or the JX-3P's. `physic_rule.hpp` with the tables, shapes, damping law, tuning solve and dice maths. `Instrument::noteSeed`, its call in `startNote` with the piece's moment, `kRollStrike` | `physic_rule_test` (ratios recomputed, the tuning solve), `testOldSynthsUnchanged` | Every figure has a page or a SELF mark; old sound to the bit |
| 1 | **Skeleton.** `physic.hpp/.cpp`, the 22 settings and 8 macros with ranges, 8 + 2 slots, the steal, sleep, `allNotesOff`, `physic/modes.hpp`, MALLET, **MARIMBA only**. `SynthKind::Physic`, the factory, the bench rows | Tuning, ratios, decay, blocks, Play twice, too small, NaN, silence, no allocation, steal (MARIMBA) | A marimba plays in tune through the engine; bench under 0.5% |
| 2 | **Every modal model**, the shapes, MATERIAL, STRETCH, the 0.45 fade, mode counts | Ratios, position, material, STRETCH 50 for all | Worst modal row under the ceiling; **SPEED TEST on the handheld** |
| 3 | **STRING**, `physic/string.hpp`: the loop, allpass tuning with state reset, loop filter from T60, decay stretch, stiffness, PLUCK, pick comb, re-strike | `physic_string_test` | 0.5 cent, 24-108 |
| 4 | **Strike and dice**, `physic/strike.hpp`: NOISE, velocity by Hertz, the four rolls from the seed, DICE, Y's `roll` maths | Hertz, rolls differ, Play twice with DICE 100 | |
| 5 | **Playing.** Glide (both cores, take-over), cheap `setMacro` and the tick, RELEASE and RING, MOTOR, WIDTH. PHYSIC put in the shared synth loops (glide, THROW, chord effects, PAIRED over a 4-key chord) | Glide clicks, macros cheap, blocks again, the shared loops | |
| 6 | **BODY and PAIR**, `physic/body.hpp` | Body modes, PAIR beats at its cents, PAIR's cost | Every bench row measured again |
| 7a | **App: song and files.** `TrackKind`, the row, `PhysicPatch`, the spec with defaults-first `loadPreset`, `.physic`, `settingCount` | `app_test`, `save_test` | A song saves and loads |
| 7b | **App: screens.** Pages, `settingText`, `macroText`, Y, `App::screenNow()`, stills in `main.cpp` and `kViews` | `textsize` at XL, `textviews`, `themeviews`, `words` | Pages draw at every size and theme |
| 8 | **The 30 presets** | Every preset holds its figures | |
| 9 | **Docs**: README's PHYSIC page, CLAUDE.md (Adding an instrument, Measured, the instrument's rules) | `ctest -R words` | |

Batches 2 and 3 can run side by side after 1 (their own headers and tests).
7a can start after 1.

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
| Tiny numbers with flush-to-zero off (15× the cost, scratch) | Sleep into exact zeros, snaps; `testTooSmall` |
| Glide clicks only measured, not heard | `testGlideClicks` bounds them |
| The first 30 ms after Play may hold the old tail | Said; the test waits for sleep. A hard cut instead would click |
| `noteSeed` touches the engine for every synth | Default does nothing; pinned checksums held |
| Merge with the JX-3P branch | Same enum and table lines; rebase by hand; one `measure.hpp` |
| A 13th MODEL turns the choice into a bar | Said in section 3; the screen is told before it happens |

## 12. The owner's calls

| # | Call | Recommended |
| --- | --- | --- |
| 1 | Name | **PHYSIC** (the owner's, 2026-11-14) |
| 2 | One synth with MODEL, or several | **One**, 11 models |
| 3 | Which models ship | The 11 in section 2; more appended later |
| 4 | BOW | **Not in version 1**; later as STRIKE 3 |
| 5 | Voices | **8** (+2 fade slots); one constant |
| 6 | A ringing STRING struck again | **Damp 2 ms, then pluck** (alternative: add the pluck, as the modal models do) |
| 7 | GLIDE on a ringing note | **Take it over, no new strike** (alternative: always strike again) |
| 8 | GLIDE on modal models | **All modes bend together** (alternative: modal models ignore glide) |
| 9 | Dice new each time round, or the same every time round | **New each time**, as HUMAN TIME and LEVEL |
| 10 | Y rolls a new patch; may it change MODEL | **Yes it rolls; no**, MODEL stays |
| 11 | HARDNESS and POSITION move a ringing note | **No**, next strike only (the physics) |
| 12 | RELEASE default for mallets and bells | **RING** |
| 13 | Stop / Play: damp in 30 ms, or let ring | **Damp** |
| 14 | Steal the quietest voice, or the oldest | **Quietest** |
| 15 | Macro 7: OCTAVE (as Phazed and QMOD) or RELEASE | **OCTAVE** |
| 16 | PAIR (doubles a string's cost) | **In** |
| 17 | MOTOR's phase from each note-on (repeatable) or free-running (as real vibes) | **From each note-on** |
| 18 | Place in the instrument list | **Last**, after QMOD |
| 19 | Preset 0 | **MARIMBA** |
| 20 | Preset names and folders | Section 5 |
| 21 | WIDTH by pitch on by default | **30% in MALLETS presets**, 0 elsewhere |

**Not done:** no code was written or built in the repo. The FR book and the
JS, VHKJ, CD and R00 texts were not opened. Every timing and tuning figure
marked scratch came from the designers' throwaway programs on a container,
not `bench` on the owner's desk.
