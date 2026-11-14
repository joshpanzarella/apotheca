# JX-3P plan

Written 2026-11-13, revised the same day after a sceptic's review (section 9
lists each of their 28 points and what was done). **Batch 0 is done
(2026-11-14)**: the Service Notes were read and every SN claim below has its
page (section 0, "SN pages"), and the measuring tools are shared in
`tests/measure.hpp`. **Batch 1 is done (2026-11-14)**: the skeleton plays
DCO-1's saw through the voices, the tick, the envelope and the VCA
(`engine/include/apotheca/jx3p.hpp`, `engine/src/jx3p.cpp`,
`engine/src/jx3p/env.hpp`, `tests/jx3p_test.cpp`); what it learned is under
the batch table. **Batch 2 is done (2026-11-14)**: both DCOs, every wave,
TUNE, FINE, SYNC, METAL, NOISE, SOURCE MIX and ENV on the DCOs
(`engine/src/jx3p/dco.hpp`, `tests/jx3p_dco_test.cpp`), with minimum-phase
BLEP and BLAMP tables; what it learned is under the batch table too. **Batch
3 is done (2026-11-14)**: the VCF, the HPF and BRILLIANCE
(`engine/src/jx3p/ir3109.hpp`, `tests/jx3p_vcf_test.cpp`); the filter bends
only its feedback, not each stage (see "Batch 3: what it learned"). Its SPEED
TEST on the handheld waits on the owner. **Batch 4 is done (2026-11-14)**: the
LFO, its DELAY and its routes (`engine/src/jx3p/lfo.hpp`,
`tests/jx3p_lfo_test.cpp`); see "Batch 4: what it learned". Nobody has heard
anything: every figure is a test's.

## The recommendation

**Write a new six-voice synth that copies the Roland JX-3P's patch, then take
Twin Analog out.** Twin has no filter, no LFO and no chorus, so there is
nothing in it to trim down to a JX-3P. Most of the code around it (the
engine's synth interface, the spec table, the song file) is generic and only
needs a new row.

| Question | Answer |
| --- | --- |
| What is copied | **The whole PG-200 patch**: 2 DCOs, cross mod (SYNC, METAL), noise, source mix, HPF, a 4-pole resonant low-pass, VCA (ENV or GATE), one LFO with delay, one ADSR, chorus on/off. 32 settings, each with the real machine's range and its 0-10 dial |
| What is added | Only what the program needs to fit: 8 macros, GLIDE (the engine's), and two **track values** kept out of the patch: VELOCITY and BRILLIANCE (section 1) |
| Voices | **6**, as the real one (the owner's call; 8 is one constant) |
| Presets | **The 32 factory patches**, A1 to B16, from Roland's 1983 booklet of PG-200 settings. About a third hold a guess (section 4). **Right settings, guessed speeds** until batch 9, and the device says so |
| Name on the device | **Keep TWIN ANALOG** (recommended). The owner's call |
| Old songs | **Load.** `track twin` reads as the new synth on its first preset; Twin's settings, preset, macros and macro automation are dropped. Notes, scenes, effects, sends stay. The owner's call (section 5) |
| Cost | Target **1.2% of a desk core for 6 notes** (Twin x1 measured 4.1% today, Phazed 0.7%, QMOD 2.7%). A plain version timed for this plan: 0.68% |
| Oversampling | **None.** Band-limited oscillators instead. Twin's load watch and "BUSY: OVERSAMPLING DOWN" lose their only user and go |
| What cannot be done from published figures | **Every time and rate**: ADSR times, LFO rate and delay, chorus rate and depth, the cutoff and HPF in Hz, the envelope curve, the VCA taper. These start as stated guesses in one table each, and are set from recordings of a real JX-3P (batch 9, which needs a source of recordings: the owner's call) |
| First thing to build | Batch 0: the sources table with page numbers, and the measuring tools shared in one header |

**The owner cannot hear.** Every claim about the sound is a test, and **every
test is marked FAITHFUL (against a published figure, source named) or SELF
(against this plan's own guess)**. A green SELF test only says the code does
what the plan guessed. Only the FAITHFUL ones say anything about the machine.

## 0. Sources, and how sure each is

| Short | Source | Read for this plan? |
| --- | --- | --- |
| **SN** | Roland JX-3P (PG-200) Service Notes, 27 July 1983 (3rd printing Oct. 1986), 18 pages: manualslib 3497592 | **Read in batch 0** (2026-11-14), through the site's OCR text of the scan, not the images: OCR garbles some words ("lOmS", "eOOmVp-p", "OCO-1"), so each figure below is the plain reading of a garbled one. The viewer's page numbers and the printed ones agree (6 and 7). Pages: 2-3 parts list, 4 block diagram, 6 circuit description (CPU, DCO, VCF), 7 circuit description (VCA, chorus) and adjustments, 8 on main board |
| **OM** | JX-3P owner's manual (manualslib 1139203) | Pages cited where known. It confirms 32 ROM presets and 32 user patches (p.4) |
| **NAM** | Roland, *PG-200 音のサンプル32* (NAM-0150), 1983, scanned | Read for the values; scanned images, so every value is checked twice by hand in batch 7 |
| **WP** | Wikipedia, "Roland JX-3P" | Waves: "pulse at 50% and 10%" |
| **SOS** | Sound On Sound review | Its SYNC wording is the other way round (section 2) |
| **Synthmania** | synthmania.com/jx-3p.htm | Preset names match NAM's (A1 String I, A3 Organ I, B14 Pulsar, B15 Planet, B16 Jet), so NAM is probably the ROM. Not proven |
| **Polynominal** | polynominal.com, JX-3P page | Confirms the MN3009 BBDs |
| Roland plug-out manual | manualslib 1461110 | **A model, not the machine.** Used only where nothing else speaks (DCO-2 TUNE in semitones) |

### SN pages (batch 0)

Every claim this plan takes from the SN, with its page. Quotes are the OCR's
words, mended where the misreading is plain ("lOmS" is 10mS). **FAITHFUL**
tests may cite these; a claim marked SELF has no page and stays a guess.

| Claim in this plan | SN page | What the page says |
| --- | --- | --- |
| Master clock about 6 MHz | **p.6** | "Master Oscillator mTR10(11) generate a frequency of approx. 6MHz" |
| An 8253 divides it, one counter a DCO | **p.6**; p.3 (parts: M5L8253P-5) | "Programmable counter 8253 containing three 16-bit counters is capable of dividing high frequency signals"; dividing by 6000 gives "1kHz rectangular signals" |
| SQUARE is the counter's raw output | **p.6** | "Output from Programmable Counter 8253 is a rectangular" |
| The saw charges a capacitor, and its height is the same on every key | **p.6** | the DCO CV "keeps the sawtooth amplitude constant over the frequency range"; the charging current "is determined by the output from the S/H circuit of DCO CV" |
| PULSE is an over-charged saw | **p.6** | "The CPU will add a bias to DCO CV to excessively increase charging current when the program needs a pulse like sawtooth". **The 10% is not on the page** (WP only); the clip at the top is this plan's reading |
| SYNC: DCO-1 resets DCO-2 | **p.6** | "In SYNC mode, pulses from DCO-1 are applied to the gate of [the] counter in DCO-2 section as RESET pulses". **This settles SOS's wording: DCO-1 is the master** |
| METAL: DCO-2 not synced, its saw reset on the falling edges of both | **p.6** | "DCO-2 programmable counter does not sync to DCO-1"; the saw's transistor "is clocked on every negative edge of both DCO-1 and 2 rectangulars"; likened to ring modulation |
| NOISE stops DCO-2's clock | **p.6** | "NOISE ON signal is also routed to pin 1 of mIC37 to block DCO-2 master frequency" |
| The CPU loop, about 10 ms | **p.6** | "The CPU cycles steps 1 to 6 within approx. 10mS": 1 the keyboard, 2 "Refresh the Envelope and LFO data", 3 the panel switches, 4 bender and EDIT, 5 "Write the control voltage data into DCO, VCF, VCA", 6 "Write the note data into the programmable counter in DCO section" |
| ADSR and LFO are software | **p.6** | step 2 above |
| DCO pitch steps once a loop | **p.6** | steps 5 and 6: the counter's divisor and the DCO CV are written once a loop |
| Every CV comes from a D/A and is held | **p.6**; p.2 (HD14051 demultiplexers) | 0 to 4.7 V from the D/A, "sampled into correct channel in individual stages". The DCO CV's S/H is named; the hold for VCF and VCA is not, so **its 1 ms smoothing is SELF** |
| 6 voices | **p.6** | "6-voice (12 VCOs)" |
| VCF: 4-pole low-pass with feedback for resonance | **p.6** | "This is a 4-pole LPF composed of mIC102 and associated components"; "Feedback amount and the amplitude of resonance is controlled by mTR103, 104" |
| The VCF chip is an IR3109 | **p.3** (parts: "IR3109 VCF") | not named in the description |
| VCF CV is KCV, CUTOFF, LFO and ADSR | **p.6** | "a combination of KCV, CUTOFF, LFO and ADSR controls" |
| VCA CV | **p.6** | "VCA CV - ADSR, LEVEL, MUTE" |
| What the VCA is | **p.7**, **p.3** | p.7 describes a controlled resistance with "a minimum forward bias maintained by the CPU"; the parts list (p.3) names the part: **"BA662B VCA"**. A forum said BA662 and WP "discrete"; both pages here are Roland's own, so it is a BA662. The taper is still a guess |
| HPF after the voices are summed, before the chorus | **p.4** (block diagram: MIXER, HPF, STEREO CHORUS) | the OCR keeps the labels, not the arrows; the order is read from where they stand. Nothing says how many poles: **one pole is SELF** |
| Two MN3009 BBDs and an MN3101 driver | **p.3** (parts) | "MN3009 BBD", "MN3101 BBD DRIVER" |
| BBD clock swept 40 to 200 kHz | **p.7** | "The VCO sweeps its frequency between 40k-200kHz" |
| The other side 180° out of phase | **p.7** | the other channel's sweep is 180 degrees out of phase |
| 10 kHz low-pass before and after the BBDs | **p.7** | two filters that "both have cutoff frequency at 10kHz". Their order and poles are not given: **2-pole is SELF** |
| Chorus rate and depth | none | **SELF** |
| Self-oscillation trimmed to 600 mV | **p.7**, adjustment 5-3 | "Depress a key, and adjust VR-103 (RESO) so that the sine wave is 600mVp-p" |
| The saw's peak to peak at the same point | **none** | Adjustment 3-4 sets the D/A offset "for the maximum amplitude without clip"; **no saw figure is printed**. So the self-oscillation level against the saw **stays SELF** (6 dB under the saw's peak) |
| The factory tuning | **p.7**, adjustment 2-4 | L1 (the master oscillator's coil) is set so **A4 reads 442 Hz**. The program stays at A440 (its own); said here because a recording of a real one may be at 442 |
| Envelope times and curve, LFO rate and delay, cutoff in Hz, HPF corner, VCA taper | none | **SELF**, as section 2 says |

What the SN does not settle stays as planned: PULSE's 10% (WP), METAL with
DCO-2 on SQUARE (not written: a guess), DCO-2 TUNE's steps (the plug-out
manual), and how near to 6 MHz "approx." is. **For the counter rounding**
(section 8): the clock is confirmed as an 8253 fed by about 6 MHz, trimmed
so A4 is in tune. The clone takes its clock as 440 × round(6 MHz / 440) =
**5,999,840 Hz**, so A4 is exactly 13636 divisions at A440, as the real
one's trim makes it in tune at 442. One constant, in batch 2.

## 1. The JX-3P, parameter by parameter

### What one patch holds

Every knob on the PG-200 reads 0 to 10. The clone stores it as **0 to 100,
tenths of the dial**, and shows it as the dial does ("5.3"). NAM prints every
value to a tenth, so every factory value fits exactly.

| Id | Section | Setting | Range | Fact it must meet | Source |
| --- | --- | --- | --- | --- | --- |
| 0 | DCO-1 | RANGE | 16', 8', 4' | 8' plays the written note; 16' an octave down | NAM, OM |
| 1 | DCO-1 | WAVE | SAW, PULSE, SQUARE | Pulse is an over-driven, clipped saw **reading 10%**; square is the counter's raw output | SN p.6, WP (the 10%) |
| 2 | DCO-1 | LFO | OFF, ON | The shared LFO DEPTH reaches this DCO | NAM |
| 3 | DCO-1 | ENV | OFF, ON | The shared ENV DEPTH reaches this DCO | NAM |
| 4 | DCO-2 | RANGE | 16', 8', 4' | | NAM |
| 5 | DCO-2 | WAVE | SAW, PULSE, SQUARE, NOISE | Noise stops DCO-2's clock | NAM, SN p.6 |
| 6 | DCO-2 | CROSS MOD | OFF, SYNC, METAL | SYNC: DCO-1 resets DCO-2. METAL: DCO-2's saw reset on both DCOs' falling edges | SN p.6, OM p.23 |
| 7 | DCO-2 | TUNE | 0-10 | **5.0 unison, 10 an octave up, 0 an octave down, in semitone steps** (7.9 is "a fifth") | OM, NAM, plug-out manual |
| 8 | DCO-2 | FINE | 0-10 | ±50 cents, 5.0 centre, smooth | OM |
| 9 | DCO-2 | LFO | OFF, ON | | NAM |
| 10 | DCO-2 | ENV | OFF, ON | | NAM |
| 11 | DCO | LFO DEPTH | 0-10 | Pitch moves in ~10 ms steps (software, into the counter) | SN p.6 |
| 12 | DCO | ENV DEPTH | 0-10 | The same | SN p.6 |
| 13 | DCO | ENV POLARITY | NORMAL, INVERTED | | OM p.22 |
| 14 | VCF | SOURCE MIX | 0-10 | **Linear amplitude crossfade**: 0 DCO-1, 10 DCO-2; 6.8 is 1:2, 7.4 to 7.8 is 1:3 | OM, NAM |
| 15 | VCF | HPF | 0-10 | Non-resonant, after the voices are summed | NAM, SN p.4 (diagram) |
| 16 | VCF | CUTOFF | 0-10 | Hz unknown | |
| 17 | VCF | RESONANCE | 0-10 | **Self-oscillates at 10**, a sine | NAM Organ I, SN p.7 adj. 5-3 |
| 18 | VCF | LFO MOD | 0-10 | | |
| 19 | VCF | ENV MOD | 0-10 | | |
| 20 | VCF | ENV POLARITY | NORMAL, INVERTED | Its own, apart from the DCO one | NAM (Planet), OM p.25 |
| 21 | VCF | KEY FOLLOW | 0-10 | "100%" somewhere near 8 to 9.8 (sources disagree); **this plan takes 9.8 = 100%** (section 2) | panel, NAM |
| 22 | VCA | MODE | ENV, GATE | | OM p.25 |
| 23 | VCA | LEVEL | 0-10 | "Too high may cause distortion" | OM p.26 |
| 24 | LFO | WAVE | SINE, SQUARE, RANDOM | | NAM |
| 25 | LFO | DELAY | 0-10 | Time unknown | OM p.26 |
| 26 | LFO | RATE | 0-10 | Hz unknown | OM p.26 |
| 27-30 | ENV | ATTACK, DECAY, SUSTAIN, RELEASE | 0-10 each | All at 0 gives "an extremely short pulse" | OM p.27 |
| 31 | CHORUS | | OFF, ON | One mode. Two BBDs swept 180° apart | SN p.7 |

### Track values: not in the patch

The preset code today writes every id from 0 to `kSettings` when a preset is
picked (`loadPreset`, `app/synth_spec.cpp`; Twin's at line 135), and a factory
preset replaces the whole patch. So a value meant to stay with the track must
be kept apart from the patch's ids.

| Id | Value | Range | Why |
| --- | --- | --- | --- |
| 32 | VELOCITY | OFF, ON; **ON for a new track** | **Not on the real one** (it ignores velocity). With it ON, a note at full velocity plays the patch exactly as written, and softer notes close the VCA (a stated curve, SELF). So HUMAN LEVEL and soft notes do something, and the factory patches still sound as Roland wrote them at full velocity. OFF is the real machine |
| 33 | BRILLIANCE | 0-10, 5.0 centre | A panel knob on the real one that moves every voice's cutoff. A track is the nearest thing to "the panel" |

- **`kPatchSettings` is 32.** `loadPreset` (factory and own) writes ids 0 to
  31 only; saving a `.jx3p` preset writes ids 0 to 31 only. Ids 32 and 33
  stay as the track had them. A test picks a preset after turning both, and
  reads a saved preset file for them.
- Both are still settings with ids (appended, in the song file's `settings`
  line), so songs keep them.
- Ids are appended from here on, never reordered.

### What the clone does not copy

| Real JX-3P | Here |
| --- | --- |
| Sequencer, MIDI, Hold, Key Transpose, patch banks, cassette | The program's own |
| Bender (±2/4/7 semitones) | None: the program has no bender. GLIDE is the program's (below) |
| Mute (lower two octaves down) | None |
| Master Tune | The program is at A440 |
| Hidden "Poly II" assign, hidden "noise random" LFO shape | Not copied: no panel position reaches them, and neither is measured. Owner's call |
| ENV-2, LFO key trigger, 64'/32'/2', 8 voices, effects | Roland's 2018 plug-out added these; the 1983 machine has none. Not copied |
| BBD hiss, DAC low-end clipping on some voices | Not copied (forum reports only). Owner's call |

### What the program brings, not the synth

| Thing | How it reaches the synth |
| --- | --- |
| GLIDE (lengths, GLIDE FROM) | `noteOnFrom(note, vel, from, seconds)`. The JX-3P has no portamento; the slide moves the key, so the DCOs and the VCF's key follow both slide. The slide starts from `from`'s pitch whether or not the voice that played `from` is still sounding |
| Chord effects, ratchets, THROW, dice | Only as `noteOn` / `noteOff`. Nothing in the synth. HUMAN LEVEL reaches the sound only with VELOCITY ON |
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

Each part is **its own Rack-free header with its own test program**, so the
batches that build them never edit the same lines:

| Header | Holds | Test program |
| --- | --- | --- |
| `engine/src/jx3p/env.hpp` | the tick, the ADSR, the S/H smoothing, the VCA | `jx3p_test` (batch 1) |
| `engine/src/jx3p/dco.hpp` | both DCOs, the BLEPs, SYNC, METAL, NOISE, SOURCE MIX | `jx3p_dco_test` |
| `engine/src/jx3p/ir3109.hpp` | the VCF and the HPF | `jx3p_vcf_test` |
| `engine/src/jx3p/lfo.hpp` | the LFO and its delay | `jx3p_lfo_test` |
| `engine/src/jx3p/chorus.hpp` | the chorus | `jx3p_chorus_test` |
| `engine/src/jx3p.cpp` | the voices, the assigner, wiring the parts | `jx3p_test` |

### The control tick: the JX-3P's own CPU

The SN (p.6): "The CPU cycles steps 1 to 6 within approx. 10mS": read keys, work out
ENV and LFO, read the panel, write the CVs, write the counters. **ADSR and LFO
are software, and every CV is a step every 10 ms through a sample-and-hold.**
That is part of the sound and is kept.

| Thing | How |
| --- | --- |
| The tick | **480 samples** at 48 kHz, counted per sample (`kTick`), never at a block's edge |
| Where ticks start | Each voice's at its note-on (the real one has one loop; a shared grid would add up to 10 ms of lateness to every note, and the program's timing comes first). The LFO has one tick for the synth |
| DCO pitch (LFO, ENV) | **Stepped**: a new pitch each tick, as the real one writes the counter's divisor |
| VCF and VCA CVs | Stepped, then a one-pole smoothing for the S/H's hold capacitor. Its time is a guess (1 ms). **It lands exactly**: within 1e-6 of its target it is set to the target, so a held CV is a constant |
| Knobs and macros | Read on the tick, as the real one reads its panel |
| Key and GLIDE | Per sample. GLIDE is the program's, and its length is tested to the sample |

### Play from the top is the same performance

`allNotesOff` (Play, Stop, panic) puts back **everything that remembers the
past**:

| State | Put back to |
| --- | --- |
| The LFO's tick, phase, delay and RANDOM seed | Their start |
| The assigner: each voice's age ("longest silent", "oldest") and the count of note-ons | As at creation |
| Each voice's noise seed | Its fixed seed (from the voice's number) |
| Each voice's DCO phases | 0 (they are set again at its next note-on) |

**A DCO does not free-run.** A voice's start phase is set at each note-on from
a seed (the voice's number and the count of note-ons since `allNotesOff`).
The real counters free-run, so a note's phase there is as good as random; a
seeded phase gives the same spread and the same bits each time. This also
fits "a silent voice is skipped": nothing of a silent voice moves, so how long
it was silent cannot change the sound.

Test (`testPlayFromTheTop`, SELF): an X audition of a chord, then Play from
the top, gives the same bits as Play from the top on a fresh engine.

### Voice stealing

Assigner: the same key if it is sounding, else the free voice longest silent,
else the oldest voice whose key is up (ringing in its release), else the
oldest held (`kStealLetGoFirst`; added after the batch 1 review: a steal cut a
key still held while quieter tails rang on. The owner's call, section 8).

**A steal is a retrigger from where the voice is**: the envelope starts its
attack from its current level (it is software on the real one, so it is not
reset to 0), and the pitch moves to the new key on the next sample. No fade.
This is a reading of how a software envelope behaves, **not documented**. The
other choice is a 2 ms fade of the old note first. The owner's call (section
8).

Tests (SELF): six held notes and a seventh, the stolen voice's level has no
jump between samples larger than the wave's own. And with GLIDE PAIRED: a
4-key chord, then four gliding notes, on 6 voices (so two steals): each new
note starts at the pitch `glideLines` says it comes from and lands in its
length, stolen or not.

### DCOs

The real DCO (SN p.6): a 6 MHz master clock, divided by a counter (8253) set by
the CPU; the counter's rectangle is the SQUARE; a saw is made by charging a
capacitor at a current set by the pitch, so the saw's height is the same on
every key.

| Wave | Here | Band-limiting |
| --- | --- | --- |
| SAW | A phase ramp, -1 to 1 | A BLEP (a short correction around a jump that takes the alias out) at the reset |
| SQUARE | 50% rectangle | A BLEP at each edge |
| PULSE | The saw times a gain over 1, clipped at the top, then DC-blocked: a ramp that hits the top early and stays there. The SN (p.6): "add a bias to DCO CV to excessively increase charging current". **Gain 10, so the ramp reaches the top in 10% of the cycle and the wave reads as WP's 10% pulse.** Fitted again from a recording in batch 9 | A BLEP at the reset, and a **BLAMP** (the same correction for a bend, not a jump) at the clip point, whose time is known exactly |
| NOISE | White, a xorshift seeded per voice | None needed |

- **SYNC**: when DCO-1 wraps, DCO-2's phase is reset at the same fraction of a
  sample, and a BLEP of the size of the jump is put there. **Why DCO-1 is the
  master**: OM p.23 and NAM get pulse-width sweeps from SYNC by turning
  DCO-2's TUNE, which only works if DCO-2 is the one reset. SOS writes it the
  other way round ("syncs DCO 1's frequency to DCO 2"); **the SN (p.6)
  settles it: "pulses from DCO-1 are applied ... in DCO-2 section as RESET
  pulses"**.
- **METAL**: DCO-2 is not synced. Its saw is reset at every falling edge of
  DCO-1's square **and** of DCO-2's own square (SN p.6). Each reset is a jump with
  a BLEP. What METAL does with DCO-2 on SQUARE is not written anywhere: the
  clone resets the square's phase the same way, marked as a guess.
- **DCO-2 TUNE is in semitone steps**: dial d plays `round((d - 5) × 2.4)`
  semitones. Roland's plug-out manual says so ("adjusts the DCO-2 pitch in
  semitone units"); it is a model, but no other source speaks. **What is
  lost**: the TUNE macro and a SYNC sweep by TUNE move in semitones. Smooth
  sweeps come from FINE, ENV and LFO (ENV on DCO-2 with SYNC is how NAM's
  sweeps are made). The owner's call; one constant.
- **Pitch**: the key, TUNE, FINE, LFO and ENV become one frequency on each
  tick (the key also per sample under GLIDE).
- **Rounded to the counter** (owner's call, recommended; **the clock is
  confirmed, SN p.6: about 6 MHz into an 8253**): the frequency is
  `clock / round(clock / f)`, as an 8253 can only divide by a whole number.
  At 6 MHz that is under 1 cent off at the top notes. One constant (the clock
  taken as 5,999,840 Hz: see "SN pages").

| Test | Measure | Pass | Kind |
| --- | --- | --- | --- |
| In tune | Pitch of each wave, each RANGE, notes 24 to 96 | Within 1 cent of the written note (8'), an octave down at 16', up at 4'. With the counter rounding on: exactly `clock / N` | SELF (A440 is the program's) |
| Saw height | Peak of the saw at notes 24, 60, 96 | The same within 0.2 dB | FAITHFUL (SN p.6: constant height) |
| PULSE is 10% | Duty cycle after the DC block, notes 36, 60, 84 | 10% ±1 | **FAITHFUL (WP)** |
| TUNE | DCO-2 against DCO-1 at TUNE 0, 2.5, 5, 7.9, 10 | -12, -6, 0, +7, +12 semitones within 1 cent; 7.9 a fifth, 10 an octave | **FAITHFUL (OM, NAM)**; the stepping itself is the plug-out's |
| FINE | FINE 0 and 10 | -50 and +50 cents within 1 | FAITHFUL (OM) |
| SYNC's width | DCO-2 on SQUARE, SYNC, TUNE from 5 down to 0 | The pulse's width is DCO-1's frequency over twice DCO-2's, within 1% | **FAITHFUL (OM p.23, NAM)** |
| METAL | Count the saw's resets in one second, DCO-2 a fifth up | Falling edges of both DCOs, exactly | FAITHFUL (SN p.6) |
| Aliasing | Every wave, SYNC and METAL, notes up to 96 at 4', a spectrum | **Every part that is not a harmonic and falls under 15 kHz at least 50 dB under the fundamental** | SELF (the real DCO is analog and does not alias; this is the house's bar) |
| Source mix | DCO-1 and DCO-2 levels at MIX 0, 5, 6.8, 7.6, 10 | 1:0, 1:1, 1:2, 1:3, 0:1 within 0.3 dB | FAITHFUL (OM, NAM) |

**The BLEP, decided now**: the two-sample polynomial kind first (Twin's
`polyBLEP`). Batch 2 runs the alias test on it before anything else. If it
fails, a table BLEP (16 samples a side) at the resets only, with **0.3% of a
desk core budgeted for it** inside the 1.2% target; measured again in batch 2
before going on.

### VCF: the IR3109

Four transconductance stages (OTAs) in a row, each a one-pole low-pass, with
feedback from the last to the first for resonance (SN p.6: "4-pole LPF...
feedback amount... controlled by mTR103, 104").

| Choice | Here | Why |
| --- | --- | --- |
| Model | **Four one-pole stages, a "zero-delay feedback" filter solved as a linear system each sample**, with each stage's soft clip taken from the previous sample's value (no iterations). This is the method the 0.68% prototype timed | Holds its pitch and resonance at 48 kHz without oversampling. An exact nonlinear solve needs iterations each sample; not budgeted |
| Saturation | One soft clip (a ratio of polynomials, not `tanh`) where the feedback meets the input, and one per stage, as above | Keeps self-oscillation at a steady level |
| Resonance | Feedback 0 to 4 (4 = the edge of oscillation in a 4-pole), mapped so dial 10 is just past it | NAM's Organ I uses res 10 as a pitched source |
| Starting to sing | **At each note-on a fixed 1e-4 is put into stage 1.** A filter that is linear near zero stays at exact zero with nothing in, so without this res 10 on silence never starts | It is the same number every time, so the same bits |
| Bass loss | **Not compensated**: the low end falls as resonance rises, by 1/(1 + feedback) | A trait of the IR3109 on the Juno-60, **not documented on the JX-3P**. The owner's call |
| Self-oscillation level | **SELF**: a guess of 6 dB under the saw's peak. SN adjustment 5-3 (p.7) trims it to 600 mV peak to peak, but **the SN prints no figure for the saw** (adjustment 3-4 sets the D/A "for the maximum amplitude without clip"), so there is no ratio to take. Batch 9's recordings can give it | SN p.7 |
| Cutoff CV | Sum of CUTOFF, KEY FOLLOW × key, LFO, ENV (with its polarity), BRILLIANCE. Exponential: a volt an octave | SN p.6: "KCV, CUTOFF, LFO and ADSR" |
| Cutoff in Hz | **A guess**: dial 0 to 10 covers 10 octaves, 15 Hz to 15 kHz, until a recording | No source gives it |
| KEY FOLLOW's 100% | **9.8, chosen from NAM's text** (Organ I is at 9.8 and NAM says it plays in tune). The test pins this choice; it does not prove it | NAM |
| The coefficient's cost | `exp2` and `tan` as short polynomials, not the maths library. The coefficient is worked out **only on samples where the cutoff CV moved** (a smoothing, a GLIDE, an LFO step) | The handheld's maths calls are slow; a FULL glide moves the cutoff for bars |

| Test | Measure | Pass | Kind |
| --- | --- | --- | --- |
| A 24 dB slope | Noise through, res 0, two octaves over cutoff | 24 dB an octave within 2 dB | FAITHFUL (SN p.6: 4-pole) |
| It starts to sing | Silence in, res 10, a note-on | A steady sine within 50 ms that never stops | FAITHFUL that it sings (NAM); the 50 ms is SELF |
| Organ I | Preset A3 (res 10, KEY 9.8, CUTOFF 2.4), pitch read 200 ms in, at its steady level | A semitone for a semitone across three octaves, within 5 cents | SELF: it pins 9.8 = 100% |
| KEY FOLLOW | Self-oscillating pitch at notes 36 and 84, KEY 0, 5, 8, 10 | A straight line in KEY | SELF |
| Polarity | ENV MOD up, inverted | Cutoff goes down with the envelope | FAITHFUL (OM p.25) |
| Bass loss | 100 Hz sine, cutoff 2 kHz, res 0 and 8 | The figure chosen, within 0.5 dB | SELF |
| Stays bounded | Every setting at its ends, loud input, 30 s | Never NaN, never over a stated peak | SELF |

### HPF

One filter for the summed voices, before the chorus (SN p.4, block diagram), not
resonant. **A one-pole high-pass** (6 dB an octave) until a recording says
otherwise; 0 is off, 10 is a guess of 1 kHz. Tests: the slope (SELF), and 0
passes the sound untouched to the bit (SELF).

### Envelope (one ADSR)

| Choice | Here |
| --- | --- |
| Where | Software, worked out on the tick: a new level each 10 ms, then the S/H smoothing |
| Curve | Attack heads for a point over the top and stops at it, decay and release fall by a fixed ratio, as `envelope.hpp`. **The real curve is not written down**; the constants are one place to change |
| Times | **A table of 101 times for 0-10 a tenth at a time** (`kEnvSeconds`). **Entry 0 is one tick**: at ATTACK 0 the level is at the top on the first tick, rising only over the S/H smoothing; at all four at 0 the note is one tick long, which is the OM's "extremely short pulse". The rest is a guess: attack to 4 s, decay and release to 10 s. Replaced by measurement |
| A note played again, or a steal | From its current level, no click |
| Feeds | VCF (ENV MOD, polarity), VCA (MODE = ENV), DCO pitch (ENV DEPTH, polarity, each DCO's switch) |

| Test | Pass | Kind |
| --- | --- | --- |
| Times | Each of 0, 2.5, 5, 7.5, 10 for A, D and R lands on its table's time within one tick | SELF |
| Stepped | The VCA's gain read off a steady tone moves once a tick | FAITHFUL (SN p.6: 10 ms loop) |
| All at 0 | A pulse one tick long | FAITHFUL (OM p.27) for "short"; one tick is this plan's reading |
| A retrigger | No jump between samples larger than the wave's own | SELF |

### VCA

SN: a voltage-controlled resistance (p.7); the part is a BA662B (p.3; see
"SN pages"). Gain = LEVEL × ENV, or LEVEL × the gate in GATE mode, times the
velocity curve with VELOCITY ON (1 at full velocity). **The taper is a guess**
(exponential, a dial step of 1 is 4 dB). Above LEVEL 7 a soft clip comes in
("too high may cause distortion", OM p.26), also a guess in shape. GATE mode
opens on the next sample, not on a tick: the program's timing comes first.

Tests: LEVEL steps follow the taper table within 0.3 dB (SELF); GATE mode has
no slope (SELF); LEVEL 10 on a loud chord adds odd harmonics, LEVEL 5 does not
(FAITHFUL to the OM's warning, not to a level); full velocity with VELOCITY
ON is the same bits as VELOCITY OFF (SELF).

### LFO

| Choice | Here |
| --- | --- |
| How many | One for the synth (the real one has one CPU) |
| Shapes | SINE, SQUARE, RANDOM (a new seeded value each cycle, held) |
| Worked out | On the LFO's tick: steps every 10 ms. So the rate tops out well under 50 Hz, as on the real one |
| RATE | **A table**, a guess of 0.1 to 20 Hz, until measured |
| DELAY | Fades the LFO in from 0, starting when a key goes down with no other key held. **Which keys restart the delay is not written down** (a guess). Time a guess of 0 to 5 s |
| Seed | Fixed; put back by `allNotesOff` |

Tests (all SELF): rate within 1% of its table; RANDOM is the same from fresh
twice and differs from cycle to cycle; DELAY reaches full depth in its time;
legato notes do not restart it.

### Chorus: a model of the JX-3P's own

Not the CHORUS effect (it is an `Effect` that allocates, three voices a side,
and not this circuit) and not the CZ-1 model as it is, but built **the way
`third_party/easycz/dsp/cz_chorus.c` is**: a fixed buffer, no allocation, one
mono sample in and a pair out.

| From the SN (p.3 and p.7) | Here |
| --- | --- |
| Two MN3009 BBDs (confirmed: Polynominal), one a side, clocked by one VCO sweeping **40 to 200 kHz** (SN p.7; MN3009 and MN3101 in the parts, p.3) | Two reads of one delay line. An MN3009 has 256 stages, so its delay is 256 / (2 × clock): **0.64 to 3.2 ms** |
| One triangle sweep, the other side **180° out of phase** | One triangle; left reads at d, right at the mirror of d |
| **10 kHz** low-pass before and after (SN p.7) | A 2-pole low-pass at 10 kHz on the wet path, before and after (their order is a guess) |
| Rate and depth | **Not published. A guess of 0.5 Hz** over the whole delay range. No source; batch 9 measures it |
| Dry and wet | Each side is dry plus its own BBD (a guess; the real outputs are L, R and mono) |

Switching it on or off fades over 20 ms, and the fade lands exactly. A wet
read with cubic interpolation. **It goes to sleep**: when its line holds
nothing over `kQuiet` and its filters are snapped to zero, it puts out exact
zeros and does no work, and wakes as a fresh one.

Tests: an impulse through finds the delay on each side; over a whole sweep
the delay stays within 0.64 to 3.2 ms and left is longest when right is
shortest (FAITHFUL, SN p.7); the wet path is 3 dB down at 10 kHz
within 1 dB (FAITHFUL, SN p.7; its 2 poles are SELF); OFF is the dry sound to the bit, mono
(SELF); the sweep rate (SELF).

### What every part must keep (house rules)

| Rule | Test |
| --- | --- |
| No allocation, lock or free outside the constructor and `prepare` | The engine's counted `new`, as the other synths |
| The same sound however the blocks are cut | `testBlocks`: a 20 s phrase with every part moving, cut 1, 48, 64, 100, 512 and at random: the same bits. **Twin fails this today; the new one must not** |
| The same on any helper count and twice from fresh | Joins the synth trio in `chordfx_test`, `effects_test`, `throw_test`, `engine_test` |
| Play from the top is the same performance | `testPlayFromTheTop` (above), and `testPlayFromTheTopWhileRinging` (the last notes still in their release: the fresh performance plus the tails) |
| Silence in, silence out | All voices finished and the chorus asleep: exact zeros, and the voice loop skipped |
| **Every state looks after its small numbers** | Snapped to 0 under 1e-20 or gated at `kQuiet`: the four VCF stages, the HPF, the S/H smoothers, the four 10 kHz chorus filters, the chorus line, the DC block of PULSE. **Every smoother and fade lands exactly** (the `Glide` rule). `testTooSmall`: the whole synth, flush-to-zero off, every preset through its release: no number too small to hold, as `TooSmall` in `effects_test` |
| GLIDE | `glide_test`'s `testEverySynthTakesTheLength` and `engine_test`'s glide test, without a JX exception |

**Mutation testing**: after each test passes, break what it names (a BLEP's
sign, the sync's fraction, the 180°, the tick's length, the polarity, a reset
in `allNotesOff`, the 1e-4 kick, the skip of ids 32 and 33 in `loadPreset`)
and see it fail.

## 3. Cost

Desk, `-O2`, 48 kHz. Today on this machine (`./build/bench`, 2026-11-13):

| Synth, 8 notes | % of a desk core |
| --- | --- |
| Phazed | 0.7 |
| QMOD | 2.7 |
| Twin Analog x1 / x2 / x8 | **4.1** / 8.0 / 31.4 |

A plain version written for this plan (scratch, not in the repo): two
polyBLEP saws with sync, the four-stage filter solved linearly each sample
with a `tan` for its tuning and each stage's clip from the sample before, an
envelope, and a two-tap chorus:

| Voices | % of a desk core |
| --- | --- |
| 6 | 0.68 |
| 8 | 0.84 |
| 8, at 2x (for comparison) | 1.81 |

**Budget:**

| | Desk | Handheld (desk × 5, an estimate) |
| --- | --- | --- |
| **Target, 6 notes** | **1.2%** (of which up to 0.3% a table BLEP, if needed) | 6% |
| Hard ceiling, 6 notes | 2.0% | 10% |
| If the owner picks 8 voices | 1.6% target | 8% |

`bench` gets three rows: "JX-3P, 6 notes" (A1 String I, chorus on), the worst
patch (B12 Sync Sweep: sync, env on pitch, chorus), and **a moving cutoff**
(FULL glides on every note and the LFO on the cutoff, so the coefficient is
worked out every sample). **The ×5 is a guess** and may be optimistic for
filter maths: **SPEED TEST runs on the handheld after batch 3** (the first
batch with the filter), and the owner reads the three rows.

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
| 2 | ENV | VCF ENV MOD | How much the note opens |
| 3 | TUNE | DCO-2 TUNE | The JX-3P's own sweep: with SYNC the pulse width and the sync tone, without it the interval. **In semitone steps** while TUNE is stepped |
| 4 | MIX | SOURCE MIX | DCO-1 to DCO-2 |
| 5 | LFO | VCF LFO MOD and DCO LFO DEPTH together | One wobble control |
| 6 | ATTACK | ATTACK | As Phazed and Twin |
| 7 | RELEASE | RELEASE | The same |

Ids appended from here: the macros are automation lanes 0 to 7.

### Presets: the 32 factory patches

From NAM, in the factory order. Folders are **BANK A** and **BANK B**, so the
preset numbers are the factory numbers and a folder jump is a bank jump.
**On the device each folder's line says "1983 BOOKLET, SPEEDS GUESSED"**
(checked at XL), so a player knows both that they may not be the ROM and that
every time is a guess.

| | Patches |
| --- | --- |
| Taken from NAM as printed | A1 String I to A16 Voice, B1 Violin to B16 Jet, except below |
| **Guesses** | Pulse against square is read off the knob's angle in the booklet: **A4, A8, A9, A10, A12, A14, B2, B3, B7, B12, B13, B15** could be swapped. **B14 Pulsar**: cross mod's pointer is at an unlabelled place; read as METAL. **B3 Oboe** DCO-1 wave not drawn (MIX 10 hides it). **A6, A7, B8** DCO-2 not drawn (MIX 0 hides it); B4's DCO-1 the same. **B14** has LFO DEPTH 10 with both switches off |
| Not known | Whether NAM is exactly the ROM. Synthmania's names match; NAM says "the knob positions are only a guide" |

NAM's own text gives three checks a test holds every preset to (FAITHFUL):
Fat Fifth's TUNE 7.9 is a fifth, Accordion's TUNE 10 is an octave, Organ I
plays in tune (the last also pins 9.8 = 100%). A test also holds that every
printed value is stored exactly.

### The name

| Choice | For | Against |
| --- | --- | --- |
| **Keep TWIN ANALOG** (recommended) | Two oscillators still; nothing new to learn; no maker's name on the device, as Phazed and QMOD | It no longer says what it is |
| JX-3P | Says exactly what it is | Roland's name on the device |
| A new name of the owner's | | One more thing to choose |

Inside the code it is `Jx3p` either way (`jx3p.hpp`, `SynthKind::Jx3p`), with
the file word `jx3p` and the preset ending `.jx3p`. The line under the name in
the picker: "two DCOs, a filter, a chorus".

## 5. Song files and ids

**Recommended: a new word, and the old one read in a stated way.**

| Line | After |
| --- | --- |
| `track jx3p` | The new synth. What every song is written with from now on |
| `track twin` | **Read as a JX-3P track.** Its `preset`, `macros` and `settings` lines are skipped and its automation lanes 0 to 7 are dropped, so it plays A1 String I with the macros at 0.5 and the track values at their new-track defaults. Its name, notes, scenes, `glidefrom`, effects, sends and lanes 8 and up load as they are. Saved again, it is written `track jx3p` |
| An old `.twin` preset file | Left on the card, not listed (the folder lists `.jx3p`). The README and the update's notes say so |

**This changes what `twin` means**, which the house rule forbids. It is
defended only because nothing is released, so it is **the owner's call**.
The alternative that keeps the rule whole: an old `track twin` is skipped as
an unknown instrument (its notes are lost). The recommendation keeps the
notes.

Test (batch 6a): a real song file written by `main` today with a Twin track
that has a preset, its settings, its macros and lanes 0 to 7 and 8 and up
automated, kept as `tests/twinsong_main.inc` in the manner of
`everykind_main.inc` (not remade): it loads as A1, notes and lanes 8 and up
whole, lanes 0 to 7 empty, and saves as `track jx3p`.

`TrackKind::Twin` becomes `Jx3p` (memory only); the word `twin` lives on only
in `trackKindFromWord`. `SynthKind` is memory only and is renamed freely.

## 6. Order of work

One batch for one agent each. Each ends with `ctest` green (with
`set -o pipefail`) and its mutants killed.

| Batch | What | Tests | Done when |
| --- | --- | --- | --- |
| 0 | **Sources and tools.** Find a readable SN; fill an "SN page" against every SN claim in this plan, and the saw's peak to peak at adjustment 5; mark any claim with no page SELF. Move the pitch reader, spectrum and level reader out of `effects_test` into `tests/measure.hpp`, shared | `effects_test` gives the same results with the shared header | Every SN claim has a page or is marked SELF; one copy of the tools |
| | **Batch 0 DONE (2026-11-14, branch `jx3p`).** SN read (manualslib 3497592, OCR text): 27 claims paged, in "SN pages"; none is without a page but the speeds, the chorus rate and the saw's figure (not printed: the self-oscillation level stays SELF). Found on the way: SYNC's direction settled (DCO-1 resets DCO-2, p.6), the VCA is a BA662B (p.3), the factory tunes A4 to 442 Hz (p.7). `tests/measure.hpp` holds the level, spectrum (`harmonicsIn`, split out of `harmonicsOf`), pitch, checksum and no-click readers; `effects_test --measure` and `--print` give the same output before and after, line for line, but for the 15 lines that time blocks. Mutants in the header: 13 made, 12 killed; `Sum` survives by design (a changed checksum changes `mathsPrint` too, and the pinned sound falls back to loudness and peak, as it does on another machine) | | |
| 1 | **Skeleton.** `jx3p.hpp/.cpp`, the 34 settings (32 patch, 2 track) and 8 macros with ranges, a start patch, 6 voices with the assigner and the steal rule, `allNotesOff`'s resets, seeded start phases, `jx3p/env.hpp` (tick, ADSR, S/H smoothing, VCA, velocity), DCO-1 saw only. `SynthKind::Jx3p`, the factory, the three `bench` rows | In tune, voices, steal, envelope times, the tick, `testBlocks`, `testPlayFromTheTop`, twice from fresh, no allocation, `testTooSmall` | A note sounds in tune through the engine and the bench row is under 1% |
| | **Batch 1 DONE (2026-11-14, branch `jx3p`).** Built beside Twin (Twin untouched; `SynthKind::Jx3p` appended, made by `Engine::addSynthTrack`; not in the app's instruments yet, that is 6a). `jx3p_test` (in ctest, TIMEOUT 120, under a second): 22 tests, each marked FAITHFUL or SELF. It also joined `glide_test`'s `testEverySynthTakesTheLength` (plain 440 Hz; 1/4 440, 1/2 440, 1 381, 2 293, FULL 253) and `engine_test`'s helpers' song (a sixth track). **Bench** (desk, one run, synth alone, 256-frame blocks): 6 notes **0.08%**, the worst row (a moving envelope, keys again every half second) 0.10%, moving cutoff (six notes gliding all the while, so the pitch is worked out every sample) **0.40%**; in the same run Phazed 0.9, QMOD 2.8, Twin x1 4.3. The start patch's note is 1.6 dB over Twin's first preset's (RMS 0.095 against 0.079). Mutants: see "Batch 1: what it learned" | | |
| 2 | **DCOs**, `jx3p/dco.hpp`: both DCOs, every wave, TUNE, FINE, SYNC, METAL, NOISE, SOURCE MIX, DCO pitch from ENV | `jx3p_dco_test`: the DCO table, **aliasing first**, and the table BLEP if needed | Every DCO test passes and the bench row is measured again |
| | **Batch 2 DONE (2026-11-14, branch `jx3p`).** The polynomial BLEP was tried first, as planned, and fails the bar (a saw at 4186 Hz: -35.3 dB of aliasing under 15 kHz; 1760 Hz: -42.6). Built instead: **minimum-phase** BLEP and BLAMP tables, 16 samples, 64 points a sample (`engine/src/jx3p/blep_table.hpp`, made by `tools/jx3p_blep.py`), so nothing is late. Every wave, SYNC and METAL, notes 36 to 96 at 4': **the worst aliasing under 15 kHz is -86.3 dB** (METAL on the pulse); the bar is -50. `jx3p_dco_test` (in ctest, TIMEOUT 120, about 1.5 s): 20 tests. `jx3p_test`: 28 (its alias baseline moved here). **Bench** (desk, two runs, synth alone): 6 notes **0.21 / 0.22%** (0.08 in batch 1), the worst row (now DCO-2's pulse synced a fifth up, ENV on its pitch, both heard) 0.30%, moving cutoff 0.63 / 0.68% (0.40); in the same runs Phazed 0.7 / 1.1, QMOD 2.8, Twin x1 4.1 / 4.5. Mutants: 38 in the last round, **38 killed** (see "Batch 2: what it learned") | | |
| 3 | **VCF and HPF**, `jx3p/ir3109.hpp`, BRILLIANCE | `jx3p_vcf_test`: the VCF table, the HPF | Organ I's settings play in tune at res 10; **SPEED TEST on the handheld** |
| | **Batch 3 DONE (2026-11-14, branch `jx3p`)**, but for the SPEED TEST on the handheld, which **waits on the owner** (the three JX-3P rows are in `bench` and in SPEED TEST). The VCF: four trapezoidal one-poles solved as one sum each sample, the feedback's soft clip from the sample before; its coefficient from polynomials, only when its CV moved. The HPF: one pole on the summed voices, off and untouched at 0. BRILLIANCE: plus or minus 2 octaves on every voice's cutoff. `jx3p_vcf_test` (in ctest, TIMEOUT 120, about 1.5 s): 17 tests. `jx3p_test`'s phrase moves every VCF and HPF setting too (`testBlocks`, `testTooSmall`, twice from fresh, the helpers). **Organ I**: a semitone a semitone within 2.2 cents over notes 48 to 84 (the bar is 5). **Bench** (desk, four runs on a busy machine, the synth alone): 6 notes **0.43 to 0.46%** (0.21 in batch 2), the worst row (now with ENV on the VCF and RESONANCE 5) 0.80 to 0.87% (0.30), moving cutoff (KEY FOLLOW 9.8, every note gliding, so the coefficient is worked out every sample) **1.56 to 1.67%** (0.63 to 0.68); in the same runs Phazed 0.7 to 1.2, QMOD 2.8 to 2.9, Twin x1 4.1 to 5.4. The moving-cutoff row is under the 2.0% ceiling but over the 1.2% target, with the LFO and the chorus still to come: see "Batch 3: what it learned". Mutants: 35 made, 34 killed |
| 4 | **LFO**, `jx3p/lfo.hpp`, and its routes | `jx3p_lfo_test` | |
| | **Batch 4 DONE (2026-11-14, branch `jx3p`).** One LFO for the synth on a 10 ms tick of its own, counted from allNotesOff; SINE, SQUARE, RANDOM; RATE 0.1 to 20 Hz; DELAY 0 to 5 s, a straight fade from a key with no other key held; LFO DEPTH to each DCO by its own switch (12 semitones at 10, as the dial squared), LFO MOD to the VCF (4 octaves at 10, straight); the LFO macro moves both (batch 1's survivor is killed). Every figure is a guess, in one table at the top of `lfo.hpp`. `jx3p_lfo_test` (in ctest, TIMEOUT 120, under a second): 11 tests, one FAITHFUL (stepped, SN p.6). `jx3p_test`'s phrase moves every LFO setting too (`testBlocks`, `testTooSmall`, twice from fresh), its helpers' song has an LFO on each track, and its Play-from-the-top song a RANDOM LFO with a DELAY. **Bench** (desk, two runs, the synth alone): 6 notes **0.43%**, the worst row 0.86 / 0.87%, moving cutoff (now with the LFO on the cutoff and on DCO-1) **1.67 / 1.68%**: no change from batch 3 that shows; in the same runs Phazed 0.8, QMOD 2.8 / 3.2, Twin x1 4.2 / 4.0. Mutants: 31 made, 29 killed, the other 2 equivalent |
| 5 | **Chorus**, `jx3p/chorus.hpp` | `jx3p_chorus_test` | Bench row with chorus on under 1.2% at 6 notes |
| 6a | **App: the song and files.** `TrackKind`, the instruments row in Twin's place, `track jx3p` and the `twin` reading, `.jx3p` presets, `loadPreset` and saving skip ids 32 and 33 | `app_test`, `save_test`, the old Twin song, a preset picked keeps BRILLIANCE and VELOCITY | A song saves and loads; Twin is no longer offered |
| 6b | **App: the screens.** The spec pages, `settingText` in dial tenths, `macroText`, the DEVICE screen in `App::screenNow()`, the folder line, still views `jx3p`, `jx3p0..5` in `main.cpp` and `kViews` | `design_test`, `textsize` (every setting at XL), `textviews` | The pages draw at every size |
| 7 | **The 32 presets** | Every preset loads, plays and stays bounded; every NAM value stored exactly (read twice by hand); the three NAM checks | 32 presets in two folders |
| 8a | **Tests off Twin.** Move every test that names Twin to the new synth (`engine_test` tails, glide, the trio songs, `render_demo` to String I); the PAIRED steal test; `nextscene_test` with its PAIRED chord track on the new synth | All of `ctest`; the thread-sanitizer run of `nextscene_test` once (about 15 minutes) | No test names Twin |
| 8b | **Twin's code out.** Delete `twin.hpp/.cpp` and `twin_test`; remove `setQualityLimit`, the load watch, "BUSY: OVERSAMPLING DOWN", the bounce's x8 | All of `ctest` | No word "twin" left but the file reader's |
| 8c | **Docs.** README, CLAUDE.md's Measured table and rules, `effects-plan.md`'s cost figures | `ctest -R words` | |
| 9 | **Calibration from recordings** (waits on the owner's source). A small tool reads WAVs of a real JX-3P and fits the tables: envelope times and curve, LFO rate and delay, cutoff in Hz, HPF, KEY FOLLOW's 100%, the VCA taper, pulse's gain, chorus rate and depth, the self-oscillation level | Each fitted table written in the tests with its measured figure; those tests become FAITHFUL | Every "guess" here is measured, or the owner chose to keep it |

Batches 2 to 5 can go side by side once 1 is in: each has its own header and
its own test program. Only the wiring in `jx3p.cpp` is shared, and it is
joined one batch at a time.

### Batch 1: what it learned (2026-11-14)

Where the build differs from the words above, and why. Each is in the code
with the same words.

| Plan said | Built | Why |
| --- | --- | --- |
| Start phase from a seed of the voice's number and the count of keys | **The count of keys alone** (since `allNotesOff`) | The count is new at every key, so the voice's number added nothing: its mutant survived |
| The phase set "at each note-on" | Set when a FREE voice takes a key. **A sounding voice taken again (the same key, or a steal) keeps its phase** | A reset there is a jump in the wave; carrying on is just as repeatable, since the phase it has came from a seed |
| `allNotesOff` puts the DCO phases back to 0 | Not done: a free voice's phase is set from the seed at its next key, so it was dead code (its mutant survived) | |
| `allNotesOff` puts back everything | It lets the keys go and puts back the count of keys, the sample clock and every voice's age. **A voice still sounding is handed to a tail** (`kTailVoices`, twice six) and rings out its release there, beside the six, which start again as from fresh. So Play from the top is the fresh performance plus the tails, to a float's rounding, however long the release (`testPlayFromTheTopWhileRinging`: Stop, then Play 64, 4800, 48000 samples on, RELEASE 7: off by 7.5e-8 at worst; before the mend, 0.84). Up to twelve voices sound for a release's length after Stop and Play; a third handover inside one release finds no tail free and the voice rings on among the six, as before | Mended after the review (2026-11-14). Play and Stop let notes go, they do not cut them, so the tails must ring somewhere other than the six |
| A steal takes the oldest sounding voice | **The oldest let go first** (`kStealLetGoFirst`), then the oldest held | The review: a seventh key cut a key still down while three released tails rang on |
| A velocity in [0, 1] | **A NaN velocity is 0** (at `noteOnVoice`, the one way in) | The review: NaN made the VCA's target NaN, the hold never landed, the voice was never freed, and the track gave NaN for good; Twin and Phazed survive it |
| The hold lands "within 1e-6" | **Within 1e-5** (`kHoldLands`) | A one-pole of 1 ms in float, heading for 1, stops moving 1.4e-6 short (a step of k times the gap is under half a float step there), so 1e-6 never came: the voice was never freed |
| VCA: 4 dB a dial step, a soft clip above LEVEL 7 | LEVEL 7.0 is 0 dB (`kVcaUnityDial`), and a full wave there just meets the clip; it bends toward 1.5 times that at most (`kVcaClipCeiling`) | With 0 dB at 8, the start patch was already in the clip; a ceiling of exactly the clip point divided by zero |
| ADSR "times" | **A time is the time to land**: the attack from 0 to the top (the key's own tick counts as the first), a decay from the top to SUSTAIN 0, a release from the top to silence; a fall lands when 80 dB from its target (`kEnvLand`) | One meaning for "lands within one tick" |
| A key on its own sample | **And a key let go**: the voice's tick restarts at the note-off, so the release (or GATE's close) starts on its sample, not up to 10 ms on | The program's timing comes first, as for the key |
| `bench`'s three rows | Built, through the synth alone (not the engine, so the rows can glide); rows 0 and 1 use the start patch until batch 7 brings A1 and B12 | |

**For batch 2: the 8253's counters are 16 bits.** 5,999,840 / 65535 is
91.6 Hz, so no single count reaches a note below about F#2 at 8' (and an
octave more at 16'). The real DCO must divide again after the counter (an
octave stage), or switch ranges another way; the SN pages read do not say.
Today the rounding is `clock / round(clock / f)` with no limit on the count,
which is 0.90 cents from the written note at worst over the piano's keys (21
to 108, every RANGE: note 106 at 4'; under 0.4 for 24 to 96) and exactly the
counter's pitch. The lowest 8' note whose count fits 16 bits is 42 (F#2).
**The alias baseline for batch 2** (`testAliasBaseline`, a held 1760 Hz saw,
everything that is not a harmonic against the fundamental): -27.2 dB, the
loudest fold-back -31.2 dB at 23360 Hz; with the BLEP's sign flipped -13.2
(-23.0). The review's figures, by another measure: 33.5 / 28.4 / 25.9 dB at
notes 84 / 96 / 103. The plan's 50 dB is far off: batch 2's job.

**The review's mend (2026-11-14)**: 12 mutants made on what it touched and
what the review found untested (the tail handover three ways, the tails
rendered, the steal's order two ways, NaN velocity, the tick's retune of a
RANGE turned under a held key, GATE at LEVEL 0 keeping its voice, a preset
putting the macros back, the top's clamp, the BLEP's sign): **12 killed**.
`jx3p_test` is 29 tests. The bench rows did not move (0.08 / 0.10 / 0.40%).

**Mutants (batch 1): 32 in the last round, 29 killed** (two more, earlier, survived because their code was dead, and it was taken out: the voice's number in the seed, and `allNotesOff` setting free voices' phases to 0). Survived: `allNotesOff` not
putting the sample clock back (it only matters with `kKeysOnTheSample` off,
where the 10 ms grid is counted from it); the BLEP's sign (no alias test yet:
batch 2's first job, and batch 2 replaces this saw); the LFO macro moving DCO
LFO DEPTH too (nothing reads it until batch 4).

**What only the tests saw**: everything. Nothing here has been heard.

### Batch 2: what it learned (2026-11-14)

| Plan said | Built | Why |
| --- | --- | --- |
| A table BLEP, "16 samples a side" (so 16 samples late) | **Minimum-phase**, 16 samples, all after the jump (`blep_table.hpp`) | A key sounds on its own sample (`testAKeyStartsOnItsSample`, the owner's call): a linear-phase table makes every note 0.33 ms late. Measured in a numpy prototype before choosing: with 16 zero crossings a side cut to 16 samples, -56 dB (saw) / -43 (pulse) at 4186 Hz; with **8 a side, -98 / -91**: the shorter filter's minimum-phase version fits in 16 samples. Cutoff 0.45 of the rate, Kaiser beta 9: -0.1 dB at 15 kHz, -3 at 20 kHz |
| The table "computed" | **Written out as numbers** by `tools/jx3p_blep.py` (numpy) | No maths library runs on it at play time, so every machine plays the same bits. Run the script again only to change it |
| BLAMP at PULSE's clip | A BLAMP at every bend (the clip, a reset from the flat top), and **`kBlampCarry`**: a minimum-phase filter is late at the bottom, so a bend's correction does not die away; it leaves a constant (-2.23 samples times the bend). Kept as a state (the carry is that constant times the slope's distance from the wave's plain slope), never summed, so nothing drifts | Without it the pulse fails the alias bar (its mutant fails 33 of the aliasing checks) |
| PULSE "DC-blocked" | **Its own mean taken off** (`kPulseMean`, 0.9): exact, no filter, nothing to settle at a note's start, nothing too small to hold | A DC-blocking filter starting at each key would thump: the DCOs do not free-run. A SYNCed or METAL pulse keeps whatever mean its resets give it |
| PULSE is 10%: "duty cycle after the DC block" | Held two ways: **its harmonics** (2nd to 8th under 15 kHz, notes 36, 60, 84) are a 10% ramp's within 0.000 dB measured (0.05 allowed; 9% or 11% at least 1.24 dB off), and by time at notes 36 and 60 (twice the share under its middle: 9.88, 9.73) | A band-limited edge 46 samples a cycle long (note 84) reads 8.5 or 11.6 by counting samples, whichever line is drawn. The harmonics say what the wave is |
| The saw's height "peak at notes 24, 60, 96 within 0.2 dB" | **Its fundamental** (2 / pi of its height) | A band-limited saw's peak is its ringing, not its height. Measured: 0.00 dB apart |
| SOURCE MIX: "6.8 is 1:2, 7.6 is 1:3, within 0.3 dB", and "linear" | Straight, held to 0.05 dB; NAM's two readings held to 0.6 dB | A straight crossfade gives 1:2 at 6.67 and 1:3 at 7.5, so 6.8 is 0.53 dB over 1:2 and 7.6 0.47 over 1:3: the plan's 0.3 dB and its own straight line cannot both hold. NAM prints a dial to a tenth; 6.8 is a tenth and a third from 6.67 |
| Each voice's noise seed "from the voice's number" | **From the count of keys**, as the start phases (batch 1) | One rule for every seed; allNotesOff puts the count back, so it is put back with it (`testFromTheTopWithBothDcos`) |
| DCO-2's start phase | Its own, from the same count of keys hashed another way | The real counters run free and apart. With both at one phase the two DCOs at unison were always one saw (its mutant); apart, eight keys read 0.50 to 0.92 of the in-phase level |
| METAL with DCO-2 on SQUARE: "resets the square's phase the same way" | Every DCO-2 wave is read from the reset ramp, the counter keeps time: a square starts low again at each reset | SELF, as planned |
| ENV DEPTH | 10 is 24 semitones (`kDcoEnvSemitones`), straight in the dial, stepped on the tick | **SELF**: no source gives it |
| The pitch "per tick, and per sample under GLIDE" | So; the key's period is worked out with one power of two a sample, and each DCO's count with one division | A glide works it out every sample: the moving-cutoff row was 0.78% with two powers and three divisions a DCO, 0.63 to 0.68 now |

**A bug the mutants found** (fixed, tested): an edge or a bend that falls exactly on a sample's end
was worked out to a hair past it and passed by, so the wave jumped raw. Its time is now held to the
sample (`testAnEdgeOnTheSamplesEnd`).

**The pulse after its mean is taken off swings from -1.9 to +0.1**, so at LEVEL 7 (where a full saw
just meets the VCA's soft clip) its short part is bent by the clip. The clip's shape is a guess
(batch 1); batch 3's filter will change the peaks anyway. For the owner: whether LEVEL 7 should
leave a pulse unbent.

**The top** (`testTheTopIsHeld`): at 21.6 kHz the tables' own band-limit leaves 0.011 of the
fundamental (it was 0.064 with the polynomial BLEP); nothing folds back (0.000004).

**Mutants (batch 2)**: 37 in the first round, 31 killed; the six survivors each got a test (the saw's
direction, a wave turned under a held key, a fresh DCO owing nothing, DCO-2's own start phase, a
freed voice's stale ENV, the 44.1 kHz clock) and one found the bug above. Last round: **38 made, 38
killed**. One more (holding the edge's time to 1 before the compare) was equivalent to the clamp after
it, and that line was taken out.

**What only the tests saw**: everything. Nothing here has been heard.

### Batch 3: what it learned (2026-11-14)

| Plan said | Built | Why |
| --- | --- | --- |
| "One soft clip where the feedback meets the input, and one per stage" | **One, on the feedback only** (`kFeedbackKnee` 2.75), taken from the sample before as a gain | Tried both ways first. A bend on what goes INTO each stage made a full saw at RESONANCE 0 lose 2 dB at its peaks and moved SOURCE MIX's 1:2 by 0.16 dB (`testSourceMix` failed). A bend on what is ACROSS each OTA (the stage before less its own output) left slow sounds alone but, where the filter sings, lowered every stage's cutoff: 4 semitones flat at 79 Hz with the knee set for the level, still 16 cents off a semitone a semitone over Organ I with the knee at 4. The feedback's bend scales the loop's gain, not a cutoff, so it holds the level and moves no pitch, and it keeps every sound bounded (`testStaysBounded`: the filter alone fed a square of 10 never passes 14) |
| Feedback 0 to 4, dial 10 "just past it" | 0 to **4.6** (`kResTop`), straight in the dial: it sings from 8.7 | At 4.4 the kick would take about 60 ms to grow at 1 kHz (worked out from the poles, not run); at 4.6 it is within 1 dB of its level in 50 ms at 1 and 4 kHz (`testItSings`). Higher sharpens its pitch more |
| Self-oscillation 6 dB under the saw's peak (SELF) | So: 6.0 / 6.1 / 6.4 / 6.5 dB at 80 Hz / 300 Hz / 1 kHz / 4 kHz | `kFeedbackKnee` set for it |
| Its pitch | **It sings sharp of its cutoff**: 0.7 cents at 80 Hz, 3 at 300 Hz, 10 at 1 kHz, 30 at 4 kHz | The bend taken from the sample before lags a sample. Organ I's notes (cutoff 39 to 316 Hz) are within 2.2 cents of a semitone a semitone. KEY FOLLOW's CV is a straight line exactly; what it sings over four octaves at KEY 10 is 17 cents off it (`testKeyFollow` allows 20) |
| Organ I's pitch "read 200 ms in" | Read from **0.5 s** | At 40 Hz it is still growing at 0.2 s and reads 13 cents sharp there |
| KEY FOLLOW's pitch read through the synth | The CV read from the synth, the singing from the filter alone at those cutoffs | Through the synth the saw's own partials near the cutoff pull the singing onto them (KEY 5: 183 cents off) |
| The cutoff in Hz | 15 Hz to 15 kHz at **note 60** (`kKeyFollowNote`, SELF), held under 20 kHz and 0.45 of the rate | KEY FOLLOW turns about one key; the plan named none. **Organ I at C4 sings at 79 Hz**, two octaves and a third under the key: in tune with itself, not with the DCO. A cutoff table anchored so CUTOFF 2.4 at KEY 9.8 sings the key played is another reading of NAM's "plays in tune": the owner's call (section 8) |
| ENV MOD's size, BRILLIANCE's | 10 octaves at ENV MOD 10 (`kVcfEnvOctaves`), plus or minus 2 at BRILLIANCE 0 and 10 (`kBrillianceOctaves`) | SELF: no source |
| The S/H's smoothing "lands exactly" (1e-5, the VCA's) | The cutoff's CV (in octaves) lands at **1e-3 of an octave** (`kVcfCvLands`, 1.2 cents) | At 1e-5 a step of an octave slides for 11.5 ms (worked out), longer than the tick (`testTheCvSteps` fails), and the coefficient is worked out on every sample it slides |
| `tan` and `exp2` "as short polynomials" | `exp2Poly` (a fitted polynomial, the whole part by its bits: no `floor`, no `ldexp`), `sinPoly` and `cosPoly` (their series, every step a multiply), G = sin / (sin + cos): one division | `floor` and `ldexp` are calls to the library, and a division by a constant (`x2 / 6.0`) is a real division. With them, and the bend's own division, the moving-cutoff row was 2.1%; without, 1.6 |
| The coefficient only when the CV moves | So, and its powers with it (`VcfCoef`): each stage's output is written out from the input and the states, so the four are worked out side by side | A sample of the filter is a chain each sample waits on (15 ns, 13 after); the shorter it is, the quicker |
| HPF: 0 off, 10 a guess of 1 kHz | 0 off; 0.1 to 10 is **10 Hz to 1 kHz** even in octaves (`kHpfLowHz`); its corner through a hold of 1 ms, set when the knob moves (the synth has no tick of its own until the LFO's) | Turned to 0, it slides down to 10 Hz and is then let go, with what it held (the sound under 10 Hz): from that sample the sound is untouched (`testHpfOffIsUntouched`) |
| A fresh voice's filter | Emptied when a free voice takes a key, and its CVs set where they are going on its first tick (no slide from the last note's) | `testAVoiceTakenAgainIsFresh`, `testTheCvSteps` |
| Bass loss (the owner's call: kept) | Kept: RESONANCE 8 takes the low end down 13.4 dB, 1 / (1 + 3.68); through the synth 13.2 (the feedback's bend gives a little back where the saw rings the filter) | `kBassLoss` false lifts the input by 1 + feedback; not built into a test, since it is a constant |

**The kick (1e-4 into stage 1 at each key) is built, but in the synth no test
reaches it**: the DCOs always feed the filter, so a key never meets a filter
with nothing coming in. Its mutant in the synth survives; in the filter alone
(`testItSings`) it is killed. Kept because the plan asks it and it costs nothing.

**The cost, and what is left for it.** The 6-note row doubled (0.21 to about
0.45%): the filter's sample is a chain of about 13 ns a voice. The moving
cutoff, 1.6%, is the filter's sample, its coefficient (about 11 ns each time)
and the DCOs' glide. Batches 4 and 5 add the LFO and the chorus on top. If the
handheld's SPEED TEST says it is too much, the next step is to run the six
voices' filters side by side, a sample of all six at a time, so their chains
overlap (not tried: a rewrite of the voice loop).

**Mutants (batch 3)**: 35 made, **34 killed** (the bend's shape and flat top,
the feedback's sign, each stage's state, the kick in the filter, the
polynomials, the top, the edge, the HPF's every part, the KCV and the glide,
KEY FOLLOW's 100%, ENV MOD and its polarity, BRILLIANCE, the fresh voice, the
hold's landing, the coefficient's recompute, the bass loss, the HPF's tail,
its slide). Survived: the kick in the synth (above). Three mutants first hung or
crashed the test (a filter that never sings gave a pitch of 0 to the
harmonics' reader): it now fails instead.

**What only the tests saw**: everything. Nothing here has been heard.

### Batch 4: what it learned (2026-11-14)

| Plan said | Built | Why |
| --- | --- | --- |
| "One tick for the synth" | The LFO's tick counts from allNotesOff (or the synth's making) and runs **whether anything sounds or not**. `render` cuts its block into pieces that end where the LFO ticks, so through a piece the LFO is one number and a voice's own tick reads it as of its sample | The same bits however the sound is cut (`testBlocks` in both programs, cut 1 to 1000 and at random). Reading the LFO once a block would tie it to the block's length |
| The LFO "on the tick" | The LFO moves on its tick; **a voice takes it on the voice's tick** (counted from its key), the pitch stepped, the cutoff through the S/H | One CPU loop writes the CVs after working the LFO out (SN p.6); here the two grids differ by up to 10 ms, as the envelope's do. `testStepped` holds both grids |
| Shapes | SINE from 0, rising; SQUARE +1 its first half; RANDOM a value a cycle from a hash of the count of cycles (`kLfoSeed`), pinned in the test | Never a generator walked. **A first seed was 2 x 0x9E3779B9**, so cycle 2 hashed to 0 and read exactly -1: the test found it |
| RATE "a table, 0.1 to 20 Hz" | Even in octaves (`lfoRateHz`), so 5.0 is 1.41 Hz; measured within 0.01% in the LFO alone and in the synth at 48 and 44.1 kHz | SELF |
| DELAY "0 to 5 s" | As the dial squared (5.0 is 1.25 s), in whole ticks; a straight fade from 0 to full, full on the tick DELAY's ticks after the first the key meets; **0 from the key's own sample** (`startDelay`) | SELF. Without the 0 at the key, a voice ticking before the LFO's next tick read the old full depth for up to 10 ms |
| Which keys start the delay | A key with **no other key held** (`kDelayOnFirstKeyOnly`); a key played again while its voice rings counts as none held | SELF: not written down |
| The routes' sizes (not in the plan) | LFO DEPTH 10 is 12 semitones each way, as the dial squared (2.0 is 0.48, 3.0 is 1.08: a vibrato in the low dial); LFO MOD 10 is 4 octaves each way, straight. They add to ENV's | SELF: no source. One constant each (`kDcoLfoSemitones`, `kVcfLfoOctaves`) |
| Key trigger | **None: it runs free** (`testFreeRunning`: the LFO sample for sample the same with keys as without) | The 1983 machine has none; the 2018 plug-out lists key trigger as new |
| allNotesOff puts it back | So: phase, count of cycles, delay, tick (`testAllNotesOffPutsItBack`; through the engine, `testPlayFromTheTop` now has a RANDOM LFO with a DELAY) | |
| HPF's knob read on the tick | **Not changed**: read when the knob moves, as batch 3 built it | Its tests stand; a knob read up to a tick late is not worth changing them |

**Known: a tail hears the LFO put back.** allNotesOff hands the ringing voices to tails, and puts the
LFO back: a tail with the LFO on its pitch or cutoff takes the LFO from its start, mid-release.
`testPlayFromTheTopWhileRinging` holds "fresh plus tails" with the LFO off; with it on, the tails of
a Play would differ from those of a Stop alone. Left so: one LFO is the machine's.

**The cost.** The LFO's tick is a sine (or a hash) every 10 ms for the synth, and a voice's tick two
more adds; the pieces add a loop round the voices per 480 samples. Measured, nothing that shows: 6
notes 0.43%, the worst row 0.86 / 0.87%, moving cutoff 1.67 / 1.68% with the LFO now on its cutoff
and on DCO-1 (batch 3: 0.43 to 0.46 / 0.80 to 0.87 / 1.56 to 1.67). Still over the 1.2% target on
the moving cutoff, under the 2.0 ceiling, with the chorus to come.

**Mutants (batch 4)**: 31 made, **29 killed** (the square's half, RANDOM's count and its hash, the
sine's turn, the delay's count, its start and its 0 at the key, which keys start it, RATE's octaves,
DELAY's and DEPTH's squares, LFO MOD's size, each part of `reset`, allNotesOff's reset, each DCO's
switch and its sign, the VCF's route, the rate's step through the synth, the pieces at the LFO's
ticks, the macro's second setting, DELAY read, the wave read). Equivalent: the fade's `>=` against
`>` (at `sinceKey == delayTicks` the fraction is exactly 1 too) and the step as `0.01` of a cycle a
tick at the rate (the tick is a whole 480 or 441 samples at 48 and 44.1 kHz, so 10 ms exactly).

**What only the tests saw**: everything. Nothing here has been heard.

## 7. Risks

| Risk | What is done about it |
| --- | --- |
| **Most of the character is in figures nobody has published** (times, rates, Hz) | Each is one table with its guess named, its tests marked SELF. Batch 9 measures them. Until then the device says "speeds guessed" |
| The SN figures cannot be checked here | **Done in batch 0**: every SN claim has a page, read through OCR text (section 0). No saw figure is printed, so the self-oscillation level stays SELF |
| NAM may not be the ROM | Said on the device; a ROM dump, if one turns up, replaces them |
| The polynomial BLEP may not reach 50 dB under 15 kHz | **Batch 2: it did not (-35 dB).** Minimum-phase tables reach -86 dB; the whole of batch 2 costs 0.13% more than batch 1 at six notes |
| The filter's maths on the handheld | Polynomial `tan` and `exp2`, worked out only when the cutoff moves; the moving-cutoff bench row; SPEED TEST after batch 3. **Batch 3: 1.6% on the desk for the moving cutoff (target 1.2, ceiling 2.0); the SPEED TEST waits on the owner** |
| The 10 ms steps may sound rough to anyone used to Twin | It is the real machine's. The tick is one constant |
| 6 voices: chords of 7 or more, fast strums with long releases, PAIRED glides over a chord steal | The steal is a retrigger with no click (or a fade: the owner's call). A glide's pitch is not changed by a steal; the old note's sound is. The owner may pick 8 |
| Twin's load watch is removed | No other synth needs it; if one ever does, it is in git |

## 8. The owner's calls

| Question | Recommended |
| --- | --- |
| The name on the device | Keep TWIN ANALOG |
| 6 voices or 8 | 6, as the real one |
| VELOCITY: a track value, ON for a new track (full velocity = the patch as written), or ignore velocity as the real one (then HUMAN LEVEL and soft notes do nothing on this synth) | **A track value, ON**. Faithful at full velocity, and the program's chord effects still work |
| BRILLIANCE: per track (kept through a preset change) or per patch | **Per track**, as the real one's panel knob |
| `track twin` in an old song: read as A1 (changes the word's meaning; nothing is released) or skipped as unknown (loses the notes) | Read as A1 |
| Old `.twin` preset files: left on the card unlisted, or something said on the device | Left, said in README and the update's notes |
| DCO-2 TUNE in semitone steps (Roland's plug-out says so) or smooth (no source) | **Stepped**. The TUNE macro and a SYNC sweep by TUNE then move in semitones; smooth sweeps come from FINE, ENV and LFO |
| DCO pitch rounded to the counter's whole divisions (faithful; under 1 cent off at the top) or exact | **Rounded**, once batch 0 confirms the 6 MHz clock |
| A stolen voice: retrigger from its level with no fade (how a software envelope behaves; not documented) or a 2 ms fade | **Retrigger, no fade** |
| Which voice is stolen: the oldest whose key is up (ringing in its release), or the oldest sounding, held or not (not documented) | **The oldest let go first** (`kStealLetGoFirst`): a held key is cut only when every voice is held |
| After Stop and Play, the last notes' releases ring on beside six fresh voices (up to twelve for a release's length), so Play from the top is the same performance; or they take voices from the new notes (faithful to six, but every Play after a long pad differs) | **Ring beside** (`kTailVoices`) |
| KEY FOLLOW: 9.8 is 100% (from NAM's Organ I) | Yes, until a recording says otherwise |
| Where the cutoff's Hz sit (batch 3): CUTOFF 0 to 10 is 15 Hz to 15 kHz at note 60 (Organ I at C4 sings at 79 Hz, in tune with itself), or anchored so Organ I sings the key played (CUTOFF 2.4, KEY 9.8: the cutoff is the key's own pitch) | 15 Hz to 15 kHz, as planned, until a recording. One constant each (`kVcfLowHz`, `kVcfHighHz`, `kKeyFollowNote`) |
| Bass loss with resonance (the Juno-60 has it; the JX-3P is not documented) | Keep it until a recording says otherwise |
| Note starts on the CPU's 10 ms grid (faithful, up to 10 ms late) or on the sample | On the sample |
| BBD hiss, the hidden Poly II and noise LFO | Not copied |
| **Where batch 9's recordings come from**: the owner's own unit, a hired one, or dry multisample libraries of a real unit (Roland's plug-out is a model, not a source) | A hired or borrowed unit: every knob of the table at 0, 2.5, 5, 7.5 and 10, one held note, dry. A multisample library can give the patches' speeds but not each knob. Without one, batch 9 cannot start and the speeds stay guesses |
| Bass patches: the factory 32 have none | Add own patches later, in a third folder |

## 9. Decisions on the review (2026-11-13)

| # | Point | Decision |
| --- | --- | --- |
| 1 | PULSE gain of 2 gives 50%, not WP's 10% | **Accepted.** Gain 10; a FAITHFUL test of 10% ±1 after the DC block |
| 2 | DCO-2 TUNE should step by semitones | **Accepted.** Stepped recommended, citing the plug-out manual as a model; what is lost is said |
| 3 | BRILLIANCE and VELOCITY would be reset by `loadPreset` | **Accepted.** Checked: `loadPreset` writes 0 to `kSettings`, and a factory preset replaces the whole patch. Ids 32 and 33 are track values that preset loading and saving skip, with a test |
| 4 | Play from the top is not the same performance | **Accepted.** `allNotesOff` puts back the assigner, phases, seeds and LFO; start phase from a seed at note-on, no free-running; `testPlayFromTheTop` |
| 5 | Juno-60 chorus rates misquoted | **Accepted.** The comparison is dropped; 0.5 Hz is a plain guess |
| 6 | MN3009 confirmed; 40-200 kHz and 10 kHz are SN-only; give pages | **Accepted in part.** The SN pages are not in hand here; batch 0 fills them or marks the claim SELF. **Done: p.7 (40-200 kHz, 180°, 10 kHz), p.3 (MN3009, MN3101)** |
| 7 | SYNC direction ambiguous in the press | **Accepted.** Kept as DCO-1 the master, on the grounds that OM p.23 and NAM sweep pulse width with DCO-2's TUNE; SOS's wording noted; SN page in batch 0 **Settled in batch 0: SN p.6 has DCO-1 reset DCO-2** |
| 8 | The VCA type is contested | **Accepted.** Said in section 0; the design does not depend on it **Settled in batch 0: the parts list (SN p.3) says BA662B VCA** |
| 9 | NAM probably the ROM, not proven | **Accepted.** Said on the device's folder line |
| 10 | Self-oscillation cannot start from silence | **Accepted.** A fixed 1e-4 into stage 1 at note-on; tested to start within 50 ms |
| 11 | "Solved together" with clips is not what was timed | **Accepted.** Linear solve, clips from the previous sample; that is what the prototype timed |
| 12 | `tan` per sample on the handheld | **Accepted.** Polynomial `tan` and `exp2`, only when the cutoff moves; a moving-cutoff bench row; SPEED TEST after batch 3 |
| 13 | 50 dB alias target likely fails with a 2-sample polyBLEP | **Accepted.** Target weighted to under 15 kHz; table BLEP planned in batch 2 with 0.3% budgeted |
| 14 | 1 ms minimum attack is out of reach | **Accepted.** Table entry 0 is one tick |
| 15 | Smoothers, HPF, chorus filters and line need the house rules | **Accepted.** Each listed; smoothers and fades land exactly; the chorus sleeps; `testTooSmall` on the whole synth |
| 16 | Voice stealing has no stated sound | **Accepted.** Retrigger from the current level, no fade, recommended; the owner's call; tested |
| 17 | Most tests check the clone against its own guesses | **Accepted.** Every test marked FAITHFUL or SELF |
| 18 | The Organ I test is circular | **Accepted.** Said plainly: 9.8 = 100% is chosen from NAM and the test pins the choice; pitch read at the steady level |
| 19 | The 600 mV test needs the saw's figure | **Accepted.** The level is SELF until batch 0 finds the saw's peak to peak **Batch 0: the SN prints no saw figure (p.7, adjustment 3-4 is "maximum amplitude without clip"), so the level stays SELF until batch 9** |
| 20 | Batch 9 has no source of recordings | **Accepted.** The owner's call, with the options; "speeds guessed" on the device |
| 21 | `track twin` mapping changes a word's meaning | **Accepted** as the owner's call; the reader's rule stated; a real old song as a test; `.twin` files an owner's call |
| 22 | HUMAN LEVEL does nothing with VELOCITY OFF | **Accepted, another way.** VELOCITY becomes a track value, ON for a new track; full velocity plays the patch as written. The owner's call |
| 23 | 6 voices against GLIDE PAIRED | **Accepted.** A test of a 4-key chord then gliding notes on 6 voices against `glideLines` |
| 24 | Batch 6 is two jobs | **Accepted.** 6a and 6b |
| 25 | Batch 8 is too big | **Accepted.** 8a, 8b, 8c |
| 26 | Batches 2 to 5 would collide in one file | **Accepted.** One header and one test program each |
| 27 | Share the measuring tools now | **Accepted.** Batch 0 |
| 28 | Missing owner's calls | **Accepted.** All seven are in section 8 |
