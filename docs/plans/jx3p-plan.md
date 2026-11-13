# JX-3P plan

Written 2026-11-13. Nothing here is built. Only the code was read and one
throwaway timing program was run; nobody has heard anything.

## The recommendation

**Write a new six-voice synth that copies the Roland JX-3P's patch, then take
Twin Analog out.** Twin has no filter, no LFO and no chorus, so there is
nothing in it to trim down to a JX-3P. Most of the code around it (the
engine's synth interface, the spec table, the song file) is generic and only
needs a new row.

| Question | Answer |
| --- | --- |
| What is copied | **The whole PG-200 patch**: 2 DCOs, cross mod (SYNC, METAL), noise, source mix, HPF, a 4-pole resonant low-pass, VCA (ENV or GATE), one LFO with delay, one ADSR, chorus on/off. 32 settings, each with the real machine's range and its 0-10 dial |
| What is added | Only what the program needs to fit: 8 macros, GLIDE (the engine's), a VELOCITY switch (off in every factory patch: the owner's call), BRILLIANCE as a per-track setting |
| Voices | **6**, as the real one (the owner's call; 8 is one constant) |
| Presets | **The 32 factory patches**, A1 to B16, from Roland's 1983 booklet of PG-200 settings. About a third hold a guess (pulse or square read off a drawing; listed in section 4) |
| Name on the device | **Keep TWIN ANALOG** (recommended): it still has two oscillators, and the device carries no maker's name, as Phazed and QMOD do not. The owner's call |
| Old songs | **Load.** `track twin` reads as the new synth on its first preset; Twin's old settings, macros and macro automation are dropped. Notes, scenes, effects, sends stay |
| Cost | Target **1.2% of a desk core for 6 notes** (Twin x1 measured 4.1% today, Phazed 0.7%, QMOD 2.7%). A plain version timed for this plan: 0.68% |
| Oversampling | **None.** Band-limited oscillators instead. Twin's load watch and "BUSY: OVERSAMPLING DOWN" lose their only user and go |
| What cannot be done from published figures | **Every time and rate**: ADSR times, LFO rate and delay, chorus rate and depth, the cutoff and HPF in Hz, the envelope curve, the VCA taper. These start as stated guesses in one table each, and are set from recordings of a real JX-3P (batch 9) |
| First thing to build | Batch 1: the voice skeleton, its test program and its bench row |

**The owner cannot hear.** Every claim about the sound is a test against a
figure from Roland's own papers, or is marked as a guess waiting for a
recording. Sources: Service Notes 27 July 1983 (**SN**), the owner's manual
(**OM**), Roland's 1983 booklet *PG-200 音のサンプル32* (NAM-0150, **NAM**).

## 1. The JX-3P, parameter by parameter

### What one patch holds

Every knob on the PG-200 reads 0 to 10. The clone stores it as **0 to 100,
tenths of the dial**, and shows it as the dial does ("5.3"). NAM prints every
value to a tenth, so every factory value fits exactly.

| Id | Section | Setting | Range | Fact it must meet | Source |
| --- | --- | --- | --- | --- | --- |
| 0 | DCO-1 | RANGE | 16', 8', 4' | 8' plays the written note; 16' an octave down | NAM, OM |
| 1 | DCO-1 | WAVE | SAW, PULSE, SQUARE | Pulse is an over-driven, clipped saw; square is the counter's raw output | SN |
| 2 | DCO-1 | LFO | OFF, ON | The shared LFO DEPTH reaches this DCO | NAM |
| 3 | DCO-1 | ENV | OFF, ON | The shared ENV DEPTH reaches this DCO | NAM |
| 4 | DCO-2 | RANGE | 16', 8', 4' | | NAM |
| 5 | DCO-2 | WAVE | SAW, PULSE, SQUARE, NOISE | Noise stops DCO-2's clock | NAM, SN |
| 6 | DCO-2 | CROSS MOD | OFF, SYNC, METAL | SYNC: DCO-1 resets DCO-2. METAL: DCO-2's saw reset on both DCOs' falling edges | SN |
| 7 | DCO-2 | TUNE | 0-10 | **5.0 unison, 10 an octave up, 0 an octave down, linear in semitones** (7.9 is "a fifth") | OM, NAM |
| 8 | DCO-2 | FINE | 0-10 | ±50 cents, 5.0 centre | OM |
| 9 | DCO-2 | LFO | OFF, ON | | NAM |
| 10 | DCO-2 | ENV | OFF, ON | | NAM |
| 11 | DCO | LFO DEPTH | 0-10 | Pitch moves in ~10 ms steps (software, into the counter) | SN |
| 12 | DCO | ENV DEPTH | 0-10 | The same | SN |
| 13 | DCO | ENV POLARITY | NORMAL, INVERTED | | OM p.22 |
| 14 | VCF | SOURCE MIX | 0-10 | **Linear amplitude crossfade**: 0 DCO-1, 10 DCO-2; 6.8 is 1:2, 7.4 to 7.8 is 1:3 | OM, NAM |
| 15 | VCF | HPF | 0-10 | Non-resonant, after the voices are summed | NAM, SN diagram |
| 16 | VCF | CUTOFF | 0-10 | Hz unknown | |
| 17 | VCF | RESONANCE | 0-10 | **Self-oscillates at 10**, a sine | NAM Organ I, SN adj. 5 |
| 18 | VCF | LFO MOD | 0-10 | | |
| 19 | VCF | ENV MOD | 0-10 | | |
| 20 | VCF | ENV POLARITY | NORMAL, INVERTED | Its own, apart from the DCO one | NAM (Planet), OM p.25 |
| 21 | VCF | KEY FOLLOW | 0-10 | "100%" somewhere near 8 to 9.8 (sources disagree) | panel, NAM |
| 22 | VCA | MODE | ENV, GATE | | OM p.25 |
| 23 | VCA | LEVEL | 0-10 | "Too high may cause distortion" | OM p.26 |
| 24 | LFO | WAVE | SINE, SQUARE, RANDOM | | NAM |
| 25 | LFO | DELAY | 0-10 | Time unknown | OM p.26 |
| 26 | LFO | RATE | 0-10 | Hz unknown | OM p.26 |
| 27-30 | ENV | ATTACK, DECAY, SUSTAIN, RELEASE | 0-10 each | All at 0 clicks | OM p.27 |
| 31 | CHORUS | | OFF, ON | One mode. Two BBDs swept 180° apart | SN |

Not stored in a patch on the real one, kept per track here:

| Id | Setting | Range | Why it is here |
| --- | --- | --- | --- |
| 32 | BRILLIANCE | 0-10, 5.0 centre | A panel knob on the real one that moves every voice's cutoff. A track is the nearest thing to "the panel". Not in the factory patches (5.0) |
| 33 | VELOCITY | OFF, ON | **Not on the real one** (it ignores velocity). The program's HUMAN LEVEL and any loud/soft note would do nothing without it. OFF in every factory patch. The owner's call whether it exists |

Ids are appended from here on, never reordered.

### What the clone does not copy

| Real JX-3P | Here |
| --- | --- |
| Sequencer, MIDI, Hold, Key Transpose, patch banks, cassette | The program's own |
| Bender (±2/4/7 semitones) | None: the program has no bender. GLIDE is the program's (below) |
| Mute (lower two octaves down) | None |
| Master Tune | The program is at A440 |
| Hidden "Poly II" assign, hidden "noise random" LFO shape | Not copied: no panel position reaches them, and neither is measured. Owner's call |
| ENV-2, LFO key trigger, 64'/32'/2', 8 voices, effects | Roland's 2018 plug-out added these; the 1983 machine has none. Not copied |
| BBD hiss, DAC low-end clipping on some voices | Not copied (forum reports only; a trim artefact the SN accepts). Owner's call |

### What the program brings, not the synth

| Thing | How it reaches the synth |
| --- | --- |
| GLIDE (lengths, GLIDE FROM) | `noteOnFrom(note, vel, from, seconds)`. The JX-3P has no portamento; the slide moves the key, so the DCOs and the VCF's key follow both slide |
| Chord effects, ratchets, THROW, dice | Only as `noteOn` / `noteOff`. Nothing in the synth |
| The 8 macros, automation lanes 0 to 7 | `setMacro`; read at the control tick like a knob |
| Level, mute, sends, slots | The engine's, after the synth |

## 2. The sound, block by block

One voice:

```
DCO-1 -+
       +- SOURCE MIX -> 4-pole VCF -> VCA -+
DCO-2 -+  (linear)     (cutoff, res,  (ENV or |
(noise, sync, metal)    LFO, ENV, key) GATE)  |
     6 voices summed -> HPF -> chorus (on/off) -> L / R (mono when off)
```

### The control tick: the JX-3P's own CPU

The SN: "The CPU cycles steps 1 to 6 within approx. 10mS": read keys, work out
ENV and LFO, read the panel, write the CVs, write the counters. **ADSR and LFO
are software, and every CV is a step every 10 ms through a sample-and-hold.**
That is part of the sound and is kept.

| Thing | How |
| --- | --- |
| The tick | **480 samples** at 48 kHz, counted per sample (`kTick`), never at a block's edge |
| Where ticks start | Each voice's at its note-on (the real one has one loop; a shared grid would add up to 10 ms of lateness to every note, and the program's timing comes first). The LFO has one tick for the synth, restarted by `allNotesOff` (Play, Stop), so Play from the top is the same performance |
| DCO pitch (LFO, ENV) | **Stepped**: a new pitch each tick, as the real one writes the counter's divisor |
| VCF and VCA CVs | Stepped, then a one-pole smoothing for the S/H's hold capacitor. Its time is a guess (1 ms) until measured |
| Knobs and macros | Read on the tick, as the real one reads its panel |
| Key and GLIDE | Per sample. GLIDE is the program's, and its length is tested to the sample |

Measured by `testTheTick`: a slow LFO on DCO pitch read by a pitch tracker is
flat for 480 samples, then jumps; the same with the sound cut into blocks of
1, 48, 64, 100 and 512 gives the same bits.

### DCOs

The real DCO: a 6 MHz master clock, divided by a counter (8253) set by the CPU;
the counter's rectangle is the SQUARE; a saw is made by charging a capacitor at
a current set by the pitch, so the saw's height is the same on every key.

| Wave | Here | Band-limiting |
| --- | --- | --- |
| SAW | A phase ramp, -1 to 1 | A BLEP (a short correction around a jump that takes the alias out) at the reset |
| SQUARE | 50% rectangle | A BLEP at each edge |
| PULSE | The saw times a gain over 1, clipped at the top: a ramp that flattens. The SN: "add a bias to DCO CV to excessively increase charging current". The gain is a guess (2) until measured from a recording | A BLEP at the reset, and a **BLAMP** (the same correction for a bend, not a jump) at the clip point, whose time is known exactly |
| NOISE | White, a xorshift seeded per voice | None needed |

- **The BLEP is first the two-sample polynomial kind** (Twin's `polyBLEP`).
  If it does not pass the alias test below, a table of 16 samples a side.
- **SYNC**: when DCO-1 wraps, DCO-2's phase is reset at the same fraction of a
  sample, and a BLEP of the size of the jump is put there.
- **METAL**: DCO-2 is not synced. Its saw is reset at every falling edge of
  DCO-1's square **and** of DCO-2's own square (SN). Each reset is a jump with
  a BLEP. What METAL does with DCO-2 on SQUARE is not written anywhere: the
  clone resets the square's phase the same way, marked as a guess.
- **Pitch**: the key, TUNE, FINE, LFO and ENV become one frequency on each
  tick. DCO-2's TUNE is continuous until a recording says it steps by
  semitones (the owner's call, below).
- **A note's start phase**: each DCO free-runs, as the counters do; a voice's
  start phase is from its seed. Never `rand()`, never a clock.

| Test | Measure | Pass |
| --- | --- | --- |
| In tune | Pitch of each wave, each RANGE, notes 24 to 96 | Within 1 cent of the written note (8'), an octave down at 16', up at 4' |
| Saw height | Peak of the saw at notes 24, 60, 96 | The same within 0.2 dB (the SN's constant height) |
| TUNE is linear | DCO-2 against DCO-1 at TUNE 0, 2.5, 5, 7.9, 10 | -12, -6, 0, +6.96, +12 semitones within 1 cent |
| FINE | FINE 0 and 10 | -50 and +50 cents within 1 |
| SYNC's width | DCO-2 on SQUARE, SYNC, TUNE from 5 down to 0 | The pulse's width is DCO-1's frequency over twice DCO-2's (50% at unison, all high an octave down), within 1%. This is the PWM trick in OM p.23 and NAM |
| METAL | Count the saw's resets in one second, DCO-2 a fifth up | Falling edges of both DCOs, exactly |
| Aliasing | Every wave, SYNC and METAL, notes up to 96 at 4', a spectrum | **Every part that is not a harmonic at least 50 dB under the fundamental.** (Effects use 50 dB for SATURATE.) The VCF is open for this test |
| Source mix | DCO-1 and DCO-2 levels at MIX 0, 5, 6.8, 7.6, 10 | 1:0, 1:1, 1:2, 1:3, 0:1 within 0.3 dB (linear crossfade) |

### VCF: the IR3109

The IR3109 is four transconductance stages (OTAs) in a row, each a one-pole
low-pass, with feedback from the last to the first for resonance (SN: "4-pole
LPF... feedback amount... controlled by mTR103, 104").

| Choice | Here | Why |
| --- | --- | --- |
| Model | **Four one-pole stages solved together each sample** (a "zero-delay feedback" filter: the feedback is worked out within the sample, so the tuning and resonance stay right up high without oversampling) | Holds its pitch and resonance at 48 kHz. A plain filter with a sample of delay in its loop goes sharp and loses resonance up high |
| Saturation | One soft clip (a cheap ratio of polynomials, not `tanh`) where the feedback meets the input, and one per stage | OTAs clip softly; this is what keeps self-oscillation at a steady level |
| Resonance | Feedback 0 to 4 (4 = the edge of oscillation in a 4-pole), mapped so dial 10 is just past it | NAM's Organ I uses res 10 as a pitched source |
| Bass loss | **Not compensated**: the low end falls as resonance rises, by 1/(1 + feedback), as the circuit does | A trait of the IR3109 on the Juno-60, **not documented on the JX-3P**. A test pins whichever the owner chooses |
| Self-oscillation level | Set so it is the same on every voice and matches the SN's trim: **600 mV peak to peak** against the DCO's saw on the same scope | SN adjustment 5 |
| Cutoff CV | Sum of CUTOFF, KEY FOLLOW × key, LFO, ENV (with its polarity), BRILLIANCE. Exponential: a volt an octave | SN: "KCV, CUTOFF, LFO and ADSR" |
| Cutoff in Hz | **A guess**: dial 0 to 10 covers 10 octaves, 15 Hz to 15 kHz, until a recording | No source gives it |
| Denormals | Each state snapped to 0 under 1e-20 | House rule |

| Test | Measure | Pass |
| --- | --- | --- |
| A 24 dB slope | Noise through, res 0, a spectrum two octaves over cutoff | 24 dB an octave within 2 dB |
| It sings at 10 | Silence in (DCOs off by MIX and level), res 10 | A steady sine, never stops, the same level on all 6 voices within 0.1 dB |
| Organ I | Preset A3 (res 10, KEY 9.8, CUTOFF 2.4) | **Plays in tune** across three octaves: the sine moves a semitone for a semitone, within 5 cents (NAM: "100% to play in tune"). Its pitch against the 16' note says where CUTOFF 2.4 sits, which pins one point of the cutoff scale |
| KEY FOLLOW | Self-oscillating pitch at notes 36 and 84, KEY 0, 5, 8, 10 | Moves in a straight line with KEY. Where "100%" falls is a measurement for batch 9 |
| Polarity | ENV MOD up, polarity inverted | Cutoff goes down with the envelope |
| Bass loss | 100 Hz sine, cutoff 2 kHz, res 0 and 8 | The figure chosen, within 0.5 dB |
| Stays bounded | Every setting at its ends, loud input, 30 s | Never NaN, never over a stated peak |

### HPF

One filter for the summed voices, before the chorus (SN block diagram), not
resonant. **A one-pole high-pass** (6 dB an octave) until a recording says
otherwise; 0 is off, 10 is a guess of 1 kHz. Test: the slope and that 0
passes the sound untouched.

### Envelope (one ADSR)

| Choice | Here |
| --- | --- |
| Where | Software, worked out on the tick: a new level each 10 ms, then the S/H smoothing |
| Curve | Attack heads for a point over the top and stops at it, decay and release fall by a fixed ratio, as `envelope.hpp`. **The real curve is not written down**; the constants are one place to change |
| Times | **A table of 101 times for 0-10 a tenth at a time** (`kEnvSeconds`), filled at first with a guess: attack 1 ms to 4 s, decay and release 2 ms to 10 s. Replaced by measurement |
| A note played again | From its current level, no click (the program retriggers often) |
| Feeds | VCF (ENV MOD, polarity), VCA (MODE = ENV), DCO pitch (ENV DEPTH, polarity, each DCO's switch) |

| Test | Pass |
| --- | --- |
| Times | Each of 0, 2.5, 5, 7.5, 10 for A, D and R lands on its table's time within one tick |
| Stepped | The VCA's gain read off a steady tone moves once a tick |
| All at 0 | A click (a step) at the start, as OM says |
| A retrigger | No jump between samples larger than the wave's own |

### VCA

A transistor used as a voltage-controlled resistor (SN). Gain = LEVEL × ENV,
or LEVEL × the gate in GATE mode. **The taper is a guess** (exponential, a
dial step of 1 is 4 dB) until measured. Above LEVEL 7 a soft clip comes in
("too high may cause distortion"), also a guess in shape. GATE mode opens on
the next sample, not on a tick: the program's timing comes first.

Tests: LEVEL steps follow the taper table within 0.3 dB; GATE mode has no
slope; LEVEL 10 on a loud chord adds odd harmonics, LEVEL 5 does not.

### LFO

| Choice | Here |
| --- | --- |
| How many | One for the synth (the real one has one CPU) |
| Shapes | SINE, SQUARE, RANDOM (a new seeded value each cycle, held) |
| Worked out | On the LFO's tick: steps every 10 ms. So the rate tops out well under 50 Hz, as on the real one |
| RATE | **A table**, a guess of 0.1 to 20 Hz, until measured |
| DELAY | Fades the LFO in from 0, starting when a key goes down with no other key held. **Which keys restart the delay is not written down** (a guess after the Juno-60). Time a guess of 0 to 5 s |
| Seed | Fixed; restarted with the tick by `allNotesOff` |

Tests: rate within 1% of its table; RANDOM is the same from fresh twice and
different from cycle to cycle; DELAY reaches full depth in its time; legato
notes do not restart it.

### Chorus: a model of the JX-3P's own

Not the CHORUS effect (it is an `Effect` that allocates, three voices a side,
and not this circuit) and not the CZ-1 model as it is, but built **the way
`third_party/easycz/dsp/cz_chorus.c` is**: a fixed buffer, no allocation, one
mono sample in and a pair out.

| From the SN | Here |
| --- | --- |
| Two MN3009 BBDs, one a side, clocked by one VCO sweeping **40 to 200 kHz** | Two reads of one delay line. An MN3009 has 256 stages, so its delay is 256 / (2 × clock): **0.64 to 3.2 ms**, worked out from the SN's figures |
| One triangle sweep, the other side **180° out of phase** | One triangle; left reads at d, right at the mirror of d |
| **10 kHz** low-pass before and after | A 2-pole low-pass at 10 kHz on the wet path, before and after (their order is a guess) |
| Rate and depth | **Not published.** A guess of 0.5 Hz (between the Juno-60's 0.4 and 0.6 Hz, the same BBD) over the whole delay range |
| Dry and wet | Each side is dry plus its own BBD (a guess; the real outputs are L, R and mono) |

Switching it on or off fades over 20 ms. A wet read with cubic interpolation.

Tests: an impulse through finds the delay on each side; over a whole sweep
the delay stays within 0.64 to 3.2 ms and left is longest when right is
shortest; the wet path is 3 dB down at 10 kHz within 1 dB; OFF is the dry
sound to the bit, mono.

### What every part must keep (house rules)

| Rule | Test |
| --- | --- |
| No allocation, lock or free outside the constructor and `prepare` | The engine's counted `new`, as the other synths |
| The same sound however the blocks are cut | `testBlocks`: a 20 s phrase with every part moving, cut 1, 48, 64, 100, 512 and at random: the same bits. **Twin fails this today; the new one must not** |
| The same on any helper count and twice from fresh | Joins the synth trio in `chordfx_test`, `effects_test`, `throw_test`, `engine_test` |
| Silence in, silence out | All voices finished and the chorus empty: exact zeros, and the voice loop skipped |
| GLIDE | `glide_test`'s `testEverySynthTakesTheLength` and `engine_test`'s glide test, without a JX exception |

**Mutation testing**: after each test passes, break what it names (a BLEP's
sign, the sync's fraction, the 180°, the tick's length, the polarity) and see
it fail.

## 3. Cost

Desk, `-O2`, 48 kHz. Today on this machine (`./build/bench`, 2026-11-13):

| Synth, 8 notes | % of a desk core |
| --- | --- |
| Phazed | 0.7 |
| QMOD | 2.7 |
| Twin Analog x1 / x2 / x8 | **4.1** / 8.0 / 31.4 |

A plain version written for this plan (scratch, not in the repo): two
polyBLEP saws with sync, the four-stage filter solved each sample with a
`tan` for its tuning and a soft clip per stage, an envelope, and a two-tap
chorus:

| Voices | % of a desk core |
| --- | --- |
| 6 | 0.68 |
| 8 | 0.84 |
| 8, at 2x (for comparison) | 1.81 |

**Budget:**

| | Desk | Handheld (desk × 5, an estimate) |
| --- | --- | --- |
| **Target, 6 notes** | **1.2%** | 6% |
| Hard ceiling, 6 notes | 2.0% | 10% |
| If the owner picks 8 voices | 1.6% target | 8% |

The real thing will cost more than the plain version (pulse's BLAMP, METAL's
two resets, the LFO, the S/H smoothing). The target leaves room for that and
is still **under a third of Twin x1** and under QMOD. If the polynomial BLEP
fails the alias test and a table BLEP is needed, measure again before going
on. `bench` gets two rows: "JX-3P, 6 notes" (A1 String I, chorus on) and the
same at the worst patch (B12 Sync Sweep: sync, env on pitch, chorus).

## 4. The player's side

### The DEVICE page

Grouped like the PG-200's panel, one page a section. A setting of 12 steps or
fewer is drawn as a choice (`kChoiceRange`), so every switch is a choice and
every knob a bar that reads like the dial.

| Page | Settings |
| --- | --- |
| MAIN | Preset, folder, the 8 macros (as the other synths) |
| DCO-1 | RANGE, WAVE, LFO, ENV, LFO DEPTH, ENV DEPTH, ENV POLARITY |
| DCO-2 | RANGE, WAVE, CROSS MOD, TUNE, FINE, LFO, ENV |
| VCF | MIX, HPF, CUTOFF, RESONANCE, LFO MOD, ENV MOD, ENV POLARITY, KEY FOLLOW |
| VCA AND ENVELOPE | MODE, LEVEL, ATTACK, DECAY, SUSTAIN, RELEASE |
| LFO AND CHORUS | WAVE, DELAY, RATE, CHORUS, BRILLIANCE, VELOCITY |

The shared DCO depths sit on DCO-1's page, as they sit at the left of the
PG-200's DCO block. Every value is checked at XL by `textsize`.

### The 8 macros

Each is 0 to 1, 0.5 is "as the patch". Below 0.5 it slides the setting from
the patch's value down to 0, above it up to 10, so the whole dial is reachable
from any patch. Read on the tick like a knob.

| Lane | Macro | Moves | Why |
| --- | --- | --- | --- |
| 0 | CUTOFF | VCF CUTOFF | The sweep everyone reaches for |
| 1 | RESO | RESONANCE | |
| 2 | ENV | VCF ENV MOD | How much the note opens; the brass and funk patches live on it |
| 3 | TUNE | DCO-2 TUNE | **The JX-3P's own sweep**: with SYNC it is the pulse width and the sync tone, without it the interval |
| 4 | MIX | SOURCE MIX | DCO-1 to DCO-2 |
| 5 | LFO | VCF LFO MOD and DCO LFO DEPTH together | One wobble control |
| 6 | ATTACK | ATTACK | As Phazed and Twin, so a song's habits carry over |
| 7 | RELEASE | RELEASE | The same |

Ids appended from here: the macros are automation lanes 0 to 7.

### Presets: the 32 factory patches

From NAM, in the factory order. Folders are **BANK A** and **BANK B**, so the
preset numbers are the factory numbers and a folder jump is a bank jump (the
screen's folder jump needs a folder's presets side by side, which the factory
order is not if sorted by kind).

| | Patches |
| --- | --- |
| Taken from NAM as printed | A1 String I to A16 Voice, B1 Violin to B16 Jet, except below |
| **Guesses** | Pulse against square is read off the knob's angle in the booklet: **A4, A8, A9, A10, A12, A14, B2, B3, B7, B12, B13, B15** could be swapped. **B14 Pulsar**: cross mod's pointer is at an unlabelled place; read as METAL. **B3 Oboe** DCO-1 wave not drawn (MIX 10 hides it). **A6, A7, B8** DCO-2 not drawn (MIX 0 hides it); B4's DCO-1 the same. **B14** has LFO DEPTH 10 with both switches off |
| Not known | Whether NAM is exactly the ROM ("the knob positions are only a guide") |

**They will sound only as right as the guessed times** until batch 9. NAM's
own text gives three checks a test holds every preset to: Fat Fifth's TUNE 7.9
is a fifth, Accordion's TUNE 10 is an octave, Organ I plays in tune. A test
also holds that every printed value is stored exactly.

### The name

| Choice | For | Against |
| --- | --- | --- |
| **Keep TWIN ANALOG** (recommended) | Two oscillators still; nothing new to learn; no maker's name on the device, as the house names Phazed (the CZ) and QMOD | It no longer says what it is |
| JX-3P | Says exactly what it is | Roland's name on the device |
| A new name of the owner's | | One more thing to choose |

Inside the code it is `Jx3p` either way (`jx3p.hpp`, `SynthKind::Jx3p`), with
the file word `jx3p` and the preset ending `.jx3p`. The line under the name in
the picker: "two DCOs, a filter, a chorus".

## 5. Song files and ids

**Recommended: a new word, the old one kept.**

| Line | After |
| --- | --- |
| `track jx3p` | The new synth. What every song is written with from now on |
| `track twin` | **Read as a JX-3P track.** Its `preset`, `macros` and `settings` lines are skipped and its automation lanes 0 to 7 are dropped, so it plays A1 String I with the macros at 0.5. Its name, notes, scenes, `glidefrom`, effects, sends and lanes 8 and up load as they are. Saved again, it is written `track jx3p` |
| An old `.twin` preset file | Not listed (the folder lists `.jx3p`) |

Why not map Twin's settings: the two share only ATTACK, DECAY, SUSTAIN,
RELEASE and LEVEL, on other scales; a mapped song would sound no more like
it did than A1 does. Nothing is released, so nothing is owed.

This keeps both house rules: **"twin" keeps its meaning** (Twin's layout,
which the reader knows to skip), and an older program reading `track jx3p`
skips the track, as with any instrument it does not have.

`TrackKind::Twin` stays in the enum (memory only) or becomes `Jx3p`; the word
`twin` is the only thing that must live on, in `trackKindFromWord`.
`SynthKind` is memory only and is renamed freely.

## 6. Order of work

One batch for one agent each. Each ends with `ctest` green (with
`set -o pipefail`) and its mutants killed.

| Batch | What | Tests | Done when |
| --- | --- | --- | --- |
| 1 | **Skeleton.** `jx3p.hpp/.cpp`, the 34 settings and 8 macros with ranges, a start patch, 6 voices with the assigner (same key, else the free voice longest silent, else the oldest), the tick, the envelope and VCA with their guessed tables, DCO-1 saw only. `SynthKind::Jx3p`, the factory, two `bench` rows. `tests/jx3p_test.cpp` with a pitch reader, a spectrum and a level reader (copied from `effects_test`, not shared yet) | In tune, voices, envelope times, the tick, `testBlocks`, twice from fresh, no allocation | A note sounds in tune through the engine and the bench row is under 1% |
| 2 | **DCOs.** Both DCOs, every wave, TUNE, FINE, SYNC, METAL, NOISE, SOURCE MIX, DCO pitch from LFO and ENV | The DCO table in section 2, aliasing first | Every DCO test passes and the bench row is measured again |
| 3 | **VCF and HPF**, BRILLIANCE | The VCF table, the HPF slope | Organ I's settings play in tune at res 10 |
| 4 | **LFO** and its routes, VELOCITY | The LFO tests | |
| 5 | **Chorus** | The chorus tests | Bench row with chorus on under 1.2% at 6 notes |
| 6 | **App side.** `TrackKind`, the instruments row in place of Twin's (same place in the picker), the spec pages, `settingText` in dial tenths, `macroText`, the DEVICE screen in `App::screenNow()`, `track jx3p` and the `twin` reading, `.jx3p` presets, still views `jx3p`, `jx3p0..5` in `main.cpp` and `kViews` | `app_test`, `design_test`, `save_test`, `textsize` (every setting at XL), a song with `track twin` loads as A1 with its notes | The picker shows it, a song saves and loads, Twin is no longer offered |
| 7 | **The 32 presets** | Every preset loads, plays and stays bounded; every NAM value stored exactly; the three NAM checks | 32 presets in two folders |
| 8 | **Twin out.** Delete `twin.hpp/.cpp` and `twin_test`; move every test that named Twin to the new synth (`engine_test` tails, glide, the trio songs, `render_demo` to String I); remove `setQualityLimit`, the load watch, "BUSY: OVERSAMPLING DOWN", the bounce's x8; README, CLAUDE.md's Measured table and rules, `effects-plan.md`'s cost figures | All of `ctest`; the thread-sanitizer run of `nextscene_test` once | No word "twin" left but the file reader's |
| 9 | **Calibration from recordings** (waits on the owner, below). A small tool reads WAVs of a real JX-3P and fits the tables: envelope times and curve, LFO rate and delay, cutoff in Hz, HPF, KEY FOLLOW's 100%, TUNE stepped or not, the VCA taper, pulse's gain, chorus rate and depth | Each fitted table is written in `jx3p_test` with its measured figure, as `effects_test` does | Every "guess" in this plan is a measured figure, or the owner has chosen to keep it |

Batches 2 to 5 can go side by side once 1 is in: they touch different parts
of one file, so join them one at a time.

## 7. Risks

| Risk | What is done about it |
| --- | --- |
| **Most of the character is in figures nobody has published** (times, rates, Hz) | Each is one table with its guess named. Batch 9 measures them. Until then the presets are right in their settings and wrong in their speeds |
| NAM may not be the ROM | Say so on the presets; a ROM dump, if one turns up, replaces them |
| The polynomial BLEP may not reach 50 dB on sync and metal | A table BLEP; measure the cost again |
| The 10 ms steps may sound rough to anyone used to Twin | It is the real machine's. The tick is one constant |
| 6 voices: chords of 7 or more and fast strums with long releases steal | The real one does the same. The owner may pick 8 |
| Twin's load watch is removed | No other synth needs it; if one ever does, it is in git |
| The handheld factor of 5 | Unmeasured, as for everything else. Run SPEED TEST once batch 5 ships |

## 8. The owner's calls

| Question | Recommended |
| --- | --- |
| The name on the device | Keep TWIN ANALOG |
| 6 voices or 8 | 6, as the real one |
| VELOCITY: add the switch, or ignore velocity as the real one (HUMAN LEVEL then does nothing here) | Add it, OFF in the factory patches |
| A Twin track in an old song: start on A1, or map ADSR and LEVEL | A1 |
| Bass loss with resonance (the Juno-60 has it; the JX-3P is not documented) | Keep it until a recording says otherwise |
| DCO-2 TUNE stepped by semitones or smooth | Smooth until a recording says otherwise. Stepped makes the TUNE macro step and the sync sweep only move by ENV |
| Note starts on the CPU's 10 ms grid (faithful, up to 10 ms late) or on the sample | On the sample |
| BBD hiss, the hidden Poly II and noise LFO | Not copied |
| **Recordings for batch 9**: who makes them, of what | One unit, every knob of the table at 0, 2.5, 5, 7.5 and 10, one held note, dry. Without them batch 9 cannot start and the speeds stay guesses |
| Bass patches: the factory 32 have none | Add own patches later, in a third folder, after the 32 |
