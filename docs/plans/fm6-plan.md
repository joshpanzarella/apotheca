# FM-6 plan

Written 2026-11-15. **The engine part is built (branch `fm6`)**: a six-operator
FM synth on msfa, its 32 factory sounds, eight macros, its tests and two
`bench` rows. **The app side is built too (section 10), and QMOD is gone.**
**Nobody has heard it, and it has not run on the handheld** (it has run built for the handheld, under an emulator: its tests
pass there, the same bits as the desk).

The owner's decision (2026-11-15): QMOD costs 57% of a handheld core for 8
notes and is replaced by FM-6, "6-operator FM, after a classic 80s synth", built
on msfa (the FM core of Google's music-synthesizer-for-android, Apache 2.0,
which Dexed grew out of; never Dexed's own GPL code), with the 32 sounds of the
first factory cartridge. No maker's or model's name where a player reads.

## 1. What was built

| Part | Where |
| --- | --- |
| The FM core, copied and changed | `third_party/msfa/` (its `LICENSE`, `README.md`; every change in `third_party/CHANGES.md`) |
| The synth: voices, assigner, LFO, macros | `engine/include/apotheca/fm6.hpp`, `engine/src/fm6.cpp` |
| The 32 factory sounds | `engine/src/fm6/rom1a.hpp`, written by `tools/fm6_rom.py` from `tools/fm6/rom1a.syx` |
| The engine's kind | `SynthKind::Fm6` (appended), made in `Engine::addSynthTrack` |
| Tests | `tests/fm6_test.cpp`, `tests/fm6_pins.inc` (ctest `fm6`); `tests/words_test.sh` reads `third_party/msfa` and `engine/src/fm6`, and fails on Yamaha or DX7 |
| Timings | `bench`: "FM-6, 8 notes", "FM-6, 16, worst preset" (`app/bench_run.cpp`, `fm6Row`) |
| Handheld build | `scripts/build-handheld.sh` builds msfa with `-fwrapv` and ships its licence as `licences/fm6-core-msfa-LICENSE.txt` |

How the voice works: msfa's note is cut into two halves (`Dx7Note::control`
once every 64 samples: envelopes, LFO, gains; `render` for any part of those
64), so each voice has its own 64-sample clock from the sample its key went
down, and the one LFO steps every 64 samples counted from `allNotesOff`. The
block is cut where the LFO steps, as JAX-EP's is. Every voice is worked out in
whole numbers (msfa's fixed point) and turned to a float once, with the
multiply and the add kept apart so ARM's fused multiply-add cannot round it
another way: **the desk and the handheld make the same bits** (the pinned
checksums pass under `qemu-aarch64` on the handheld build).

## 2. The choices made

| Choice | Taken | Why |
| --- | --- | --- |
| Voices | **16**, plus 16 tails | As the original. A voice costs only while it sounds, so 16 costs nothing until 16 keys are down; then about twice 8 (section 6). Fewer would steal in a held chord and its release |
| What `allNotesOff` does | Every sounding voice goes to a tail and rings out its release; the 16 start fresh, the LFO from its start | The house rule (JAX-EP's): Play from the top is the same performance, Stop never cuts |
| A key played again, or a steal | Carries on from its level (and its wave, unless OSCILLATOR KEY SYNC) | No click. The original's way for KEY SYNC ON is to start the wave again: kept |
| Steal | The oldest voice whose key is up, then the oldest held | JAX-EP's |
| A carrier's release | Always to silence, whatever its L4 | On the original a carrier with L4 over 0 rings for ever once let go: a voice never freed. None of the 32 does it; a player's own patch could |
| Settings | **All 145 of the original's voice parameters, in its own order and numbering** (ids 0 to 144), plus OPERATOR ON/OFF (145) | A bank from anywhere goes in setting for setting; nothing to translate; the names are the original's |
| When a setting is heard | A key played after it; the LFO's speed, delay and wave at once; the macros at once | As msfa reads its patch; the macros are what moves while notes sound |
| What is not played | Pitch bend and the mod wheel, breath and foot controls (the engine has none); the original's EG bias; Dexed's later "engine types" | Nothing in Apotheca sends them |
| Glide | Yes, per voice, a step every 64 samples (1.3 ms) | Cheap: one number added to the pitch |
| Sample rate | The engine's 48 kHz. msfa's tables are built once for it; another rate is put right in pitch, not in time | The engine never runs at another |
| NEON | Not used | msfa's ARM kernel is ARM32 assembly and a float sine; the C++ one is exact and the same on both machines. A NEON kernel is the obvious next saving (section 6) |

## 3. The macros

Each at 0.5 is the patch as it is; a preset puts all eight back there.

| # | Macro | Down | Up |
| --- | --- | --- | --- |
| 0 | BRIGHT | the modulators' levels down, by the knob's log, to none (plain sines) | up to 12 dB more |
| 1 | FEEDBACK | to none | to 7 (smoothly between the steps: feedback is a gain now) |
| 2 | ATTACK | every operator's R1 faster | slower (a rate 24 of 64 steps off at the ends: 64 times; never slower than the original's slowest) |
| 3 | DECAY | R2 and R3 faster | longer |
| 4 | RELEASE | R4 faster | longer |
| 5 | DETUNE | the patch's own DETUNE taken out (every operator in tune) | operators spread apart, up to 20 cents |
| 6 | LFO | PMD and AMD to none | to full (only where the patch's PMS or AMS let it through) |
| 7 | PITCH ENV | the pitch envelope's depth to none | to twice |

Why these: BRIGHT and FEEDBACK are how FM sounds change (how hard the
operators bend each other); the three envelope times are what a player reaches
for on any synth; DETUNE and LFO are motion; PITCH ENV is where the effect
sounds (TAKE OFF, REFS WHISL) live. Not taken: VOLUME (the mixer has it),
ALGORITHM (a choice, so no MOD could move it, and a jump in sound), OCTAVE
(TRANSPOSE is a setting). All eight are amounts, so a MOD may aim at any.

Known: FEEDBACK up does nothing on a patch already at 7 (18 of the 32); LFO up
does nothing where PMS and AMS are 0.

## 4. The presets

The first factory cartridge, ROM1A, as one 32-voice sysex bank:
`tools/fm6/rom1a.syx`, from Dave Benson's DX7 page (`sysex.tar`, "the original
factory patches are here as rom1a.syx - rom4b.syx", called public domain
there). **Its checksum is right; every parameter is in range.** msfa's own copy
of the file is edited (voice 31 is "CHIME-STRG", not "TRAIN", and its checksum
is then wrong), so it was not used. `tools/fm6_rom.py` checks the file's
sha256, the header, the checksum and every range, unpacks each voice into the
145 parameters and writes the table. Never edit the numbers by hand.

The names are the cartridge's, trailing spaces taken off: BRASS 1 to TAKE OFF
(with their inner spaces: "BRASS   1"; the app may want to close them up).
None is a maker's or a model's name. Their folder is "ROM 1A".

**msfa was checked against itself**: a scratch harness built the original msfa
beside the changed one. With nothing bent and at the original's own envelope
rate, the 16 voices that use neither TRANSPOSE nor AM (which the original does
not play) make the original's numbers to the bit for 3000 periods, held and
let go, and the same again when every period is cut into pieces of 1, 17, 3
and 40 samples. That harness is not kept (it needs a second copy of msfa); the
pinned checksums (`tests/fm6_pins.inc`) were taken from the code it passed.

## 5. Levels and how long each rings

A chord of four (keys 48, 55, 60, 64, velocity 0.8), held 1.5 s; the loudest
tenth of a second; then how long until every voice is free
(`fm6_test --levels`). The output gain was set so the middle of the 32 sits
about where JAX-EP's does (-16 dB). **SELF: nobody has listened to the balance.**

| Preset | Loudest | Rings | Preset | Loudest | Rings |
| --- | --- | --- | --- | --- | --- |
| BRASS 1 | -10.9 | 0.2 s | BASS 2 | -19.4 | 0.2 s |
| BRASS 2 | -12.4 | 0.2 s | E.ORGAN 1 | -12.0 | 0.1 s |
| BRASS 3 | -20.6 | 0.5 s | PIPES 1 | -14.6 | 2.5 s |
| STRINGS 1 | -18.2 | 2.3 s | HARPSICH 1 | -23.7 | 0.8 s |
| STRINGS 2 | -12.9 | 1.6 s | CLAV 1 | -20.6 | 0.2 s |
| STRINGS 3 | -16.0 | 2.4 s | VIBE 1 | -14.7 | 0.7 s |
| ORCHESTRA | -9.7 | 2.5 s | MARIMBA | -15.0 | 0.1 s |
| PIANO 1 | -13.5 | 0.9 s | KOTO | -22.4 | 2.2 s |
| PIANO 2 | -21.1 | 0.7 s | FLUTE 1 | -16.0 | 0.3 s |
| PIANO 3 | -20.6 | 1.2 s | ORCH-CHIME | -16.4 | 6.2 s |
| E.PIANO 1 | -11.9 | 0.7 s | TUB BELLS | -14.6 | 9.8 s |
| GUITAR 1 | -19.9 | 0.1 s | STEEL DRUM | -14.1 | 1.5 s |
| GUITAR 2 | -27.7 | 0.2 s | TIMPANI | -18.7 | 2.8 s |
| SYN-LEAD 1 | -13.8 | 0.2 s | REFS WHISL | -18.5 | 0.7 s |
| BASS 1 | -22.2 | 0.2 s | VOICE 1 | -18.5 | 5.0 s |
| | | | TRAIN | -21.3 | 4.6 s |
| | | | TAKE OFF | -16.7 (after 10 s: it swells) | 11.4 s |

The loudest peak is BRASS 1's, 1.05 (four keys); every other is under 0.8.

## 6. Cost

| Row (desk, 2026-11-15, a desk shared with other builds, load about 5) | Cost |
| --- | --- |
| FM-6, 8 notes (E.PIANO 1, held), three runs | 0.49 to 0.63% of a core |
| FM-6, 16 notes, keys again every half second (STRINGS 1) | 1.02 to 1.30% |
| ...each of the 32, 8 keys again every half second | 0.23% (TUB BELLS) to 0.67%; most 0.50 to 0.52 |
| QMOD, 8 notes, the same runs | 2.8 to 3.4% |
| The start patch (one operator heard), 8 notes | 0.15% |

**The handheld, estimated (not measured): about 6 to 10% of a core for 8
notes, 12 to 20% for 16, against QMOD's 57%.** How: callgrind counts 62 M
instructions for a second of 8 notes (E.PIANO 1), against 447 M for QMOD's
8, 96 M for JAX-EP's 6 and 114 M for Phazed's 8. Scaled by what each of those
is known or guessed to cost there (QMOD 57%; JAX-EP 0.55% x 17; Phazed 0.9% x
13), FM-6's 8 notes are 6.1 to 8.0%. The desk's 0.49 to 0.63% times 13 to 17
gives 6.4 to 10.7%. SPEED TEST on the handheld will say.

Where the time goes: six operators a voice, a sine-table read and a multiply
each a sample (about 7 cycles), whatever the patch; an operator whose envelope
is under the level the core works out is skipped (TUB BELLS, VIBE 1 and
MARIMBA cost less as they decay). If it must be cheaper: a NEON kernel doing
four samples of an operator at once (msfa's own ARM kernel shows the way, but
its float sine would make the handheld's numbers differ from the desk's).

## 7. Tests (`ctest -R fm6`, about 1.5 s; mutation-tested)

`fm6_test` runs 16: the presets and ranges; pitch (in tune to 0.1 cent at keys
36 to 96, a ratio of 2, TRANSPOSE, a FIXED operator at 100 Hz, DETUNE, OPERATOR
ON/OFF, another sample rate); every preset plays, stays a number, is in level
and ends to exact silence; velocity; the same bits in blocks of 1, 64, 256 and
333 with notes, a glide, macros, a setting and allNotesOff among them; Play
from the top while tails ring (the fresh sound plus the tails, within 1e-5;
the same to the bit with none ringing); the assigner and keys outside 0 to
127; a retrigger without a click; glide; every macro, which way it goes, and
that a preset puts them back; AM; the envelope's decay against msfa's measured
figure within 1%; a carrier's release always ending; the pinned sound of the 32
(`--print` writes it again); no allocation; a track through the engine.

**Mutants: 42 tried, 40 killed.** The two that live change nothing: putting
the clock back in `allNotesOff` (the voices' times are put back anyway) and
`silent()`'s look at the keys (the voice only asks once its key is up). Eight
that lived at first were killed by better tests (BRIGHT up, KEY SYNC, a
retrigger's level and gain, OPERATOR ON/OFF, a key below 0, ATTACK's floor).

## 8. What the app side needs next

1. **The instrument's row**: in `app/instruments.cpp`, QMOD's row becomes
   FM-6: shown "FM-6", says "6-operator FM, after a classic 80s synth" (40
   letters: fits the line), `SynthKind::Fm6`, a song-file word (`fm6`) and a
   preset ending (`.fm6`). **A song's `track qmod` reads as FM-6** (as `track
   twin` reads as JAX-EP: on its first preset, its QMOD settings, macros and
   macro automation dropped). `TrackData` gets an `Fm6Patch` (146 ints) where
   `qmod` was.
2. **`SynthSpec`** in `app/synth_spec.cpp`: presets (32, one folder "ROM 1A"),
   the macro names (BRIGHT, FEEDBACK, ATTACK, DECAY, RELEASE, DETUNE, LFO,
   PITCH ENV), `settingText` (ALGORITHM shown 1 to 32, COARSE as a ratio
   0.50, 1.00, 2.00... or a fixed pitch in Hz, DETUNE -7 to +7, curves as -LIN
   -EXP +EXP +LIN, LFO WAVE by name, TRANSPOSE as a note C1 to C5).
3. **Its DEVICE pages** (`App::screenNow()`): MAIN (preset and the eight
   macros, as every synth); then 145 settings want a way through. A proposal:
   a page an operator (OP1 to OP6, L1 / R1 to step) with its 21 in rows, a
   VOICE page (algorithm, with a small drawing of its wiring; feedback; key
   sync; transpose), a PITCH ENV page and an LFO page. The algorithm drawing
   is 32 small pictures; msfa's table says each one's wiring.
4. **Remove QMOD**: `engine/src/qmod.cpp`, `engine/include/apotheca/qmod.hpp`,
   `tests/qmod_test.cpp` (and its ctest line), `SynthKind::Qmod` (keep the
   number, or not: nothing is released), `bench`'s QMOD row and the busiest
   song's two QMOD tracks (`bench_run.cpp`, `sendBusiestSong`, held by
   `bench_test`), the help and the hints that say ROLL. **`third_party/shapetaker`
   is used by nothing else** (only `qmod.cpp` includes it): it goes too, and
   out of `kBeside` in `tests/words_test.sh` (its count then 8).
5. **The busiest song** for SPEED TEST: its QMOD tracks become FM-6 tracks
   (16 keys of E.PIANO 1 would be the worst honest case).

## 9. Open questions for the owner

**Answered (owner, 2026-11-15, "go with your picks"):** the names are closed up ("BRASS 1",
`tools/fm6_rom.py`); the other factory cartridges come later, as more folders; 16 voices and
these eight macros stay; TRANSPOSE stays in semitones. Still open: 1 (whether the factory
sounds may ship, which matters only if Apotheca is ever sold), 5, 6 and 10.

1. **May the factory sounds ship?** They are the maker's work (1983), shared
   everywhere (every FM emulator offers them; the archive used calls them public
   domain) but not under a licence anyone wrote down. msfa itself is Apache 2.0
   and its licence ships with the program.
2. **16 voices** (as the original) or fewer? Each costs only while it sounds.
3. **The macros** (section 3): these eight, or others (VOLUME, ALGORITHM)?
4. **The names as the cartridge has them** ("BRASS   1", three spaces) or closed
   up ("BRASS 1")? The folder's name, "ROM 1A"?
5. **AM depths are a guess** (0, 3, 9, 24 dB at AMD 99 for AMS 0 to 3): a
   recording of the original would set them. Three of the 32 hear AM (GUITAR
   1, FLUTE 1, VOICE 1); three more set AMD with every AMS at 0.
6. **A carrier's release always ends** (section 2): a departure from the
   original that no factory sound hears.
7. **More banks**: the other factory cartridges (ROM1B to ROM4B) are in the same
   archive, and the generator would take them as they are.
8. **The preset names' inner spaces** (question 4) are shown as the cartridge has them on MAIN:
   "BRASS   1".
9. **TRANSPOSE reads in semitones** (+0, +12, -24), not as a note (C3) as the original shows it.
10. **An operator page's ON is first**, and the pages run VOICE AND LFO, OPERATOR 1 to 6, PITCH
    ENVELOPE (section 10). No picture of the ALGORITHM's wiring yet.

## 10. The app side (built 2026-11-15)

| Part | Where |
| --- | --- |
| The row: FM-6, "6-operator FM, after a classic 80s synth", word `fm6`, presets `NAME.fm6`, in QMOD's place in the list | `app/instruments.cpp` |
| `TrackKind::Fm6` (QMOD's slot: the kinds are memory only); `TrackData::fm6` | `app/project.hpp` |
| `track qmod` read as FM-6 on its first preset (its preset, macros, settings, the macros' automation and a MOD on a macro not read) | `kOldQmodWord`, `parseSong` |
| The spec: the 32 in one folder (ROM 1A), the macros, how each setting reads | `fm` in `app/synth_spec.cpp` |
| An operator's ON as a bit of OPERATORS ON | `SynthSpec::bitOf`, `settingValue`, `settingWrite` |
| COARSE and FINE read with their operator (ratio, or Hz under FIXED) | `SynthSpec::readingIn`, `settingReading` |
| Six rows a page, 3 apart; held at LARGE at XL | `SynthScreen::settingGap`, `settingY`, `boxFontsFor` |
| A new synth track tells the engine its first preset | `Session::addTrack` |
| The busiest song: FM-6 on E.PIANO 1 where QMOD was | `app/bench_run.cpp`, `kBusiestFm6Preset`, `bench_test` |
| Stills | `fm6`, `fm6-1` to `fm6-8`, `fm6-long`, `fm6-fixed`, `fm6-off` |
| Tests | `app_test` (`testFm6OnATrack`, `testAQmodSongOpensOnFm6`, `testFm6sHintsFit`), `design_test` (`testFm6FromThePage`), `textsize`, `scope_test` |

The pages, MAIN then eight (L1 / R1 turn them):

| Page | Rows of four |
| --- | --- |
| VOICE AND LFO | ALGORITHM, FEEDBACK, TRANSPOSE, KEY SYNC / LFO WAVE, LFO SPEED, LFO DELAY, LFO SYNC / PITCH DEPTH (PMD), LEVEL DEPTH (AMD), PITCH SENS (PMS) |
| OPERATOR n | ON, OUTPUT LEVEL, COARSE, FINE / MODE, DETUNE, VELOCITY, LEVEL SENS (AMS) / RATE 1 to 4 / LEVEL 1 to 4 / RATE SCALING, BREAK POINT, LEFT DEPTH, RIGHT DEPTH / LEFT CURVE, RIGHT CURVE |
| PITCH ENVELOPE | RATE 1 to 4 / LEVEL 1 to 4 |

Why a page an operator: one operator is worked on without turning a page, and its envelope
reads as the original's panel does (rates over the levels they go to). Two pages an operator
would be fourteen pages to step through.

Removed with QMOD: `engine/src/qmod.cpp`, its header, `SynthKind::Qmod` (the kinds renumbered:
memory only), `qmod_test`, `third_party/shapetaker` (nothing else used it), ROLL (the device's Y,
the WiFi page's button and its `roll` request, `Session::rollSynth`, `SynthSpec::roll`), `bench`'s
QMOD rows. Tests that used QMOD moved to FM-6, or to Phazed where they needed an OCTAVE macro or a
choice macro. The whole song pinned in `effects_test` is remade (its second synth is FM-6; the 40
renders and the maths line held).


## 11. The first added presets (2026-11-16, branch `fm6presets`)

The owner: "a bunch more presets for the JAX-EP and FM-6 ... with the MOD and FX set up ... of all
different types." **49 more, in eight new folders, each a whole track sound** (the synth, its
slots, its sends, its MODs: `docs/plans/presets-plan.md`), written in `app/presets/fm6.inc` after
the 160 factory voices. **Nobody has heard any of them.** What each does is what its numbers say;
the figures below are measured, the sound is a guess to be tuned by ear.

| Folder | Count | What is in it |
| --- | --- | --- |
| KEYS | 7 | electric pianos (a suitcase with autopan, a glassy new one, a barking one, a reed one), a clav through a wah, a worn celeste, vibes with a motor |
| BASS | 6 | a wobble and growl, a slap, a sub that boings, a fretless, an acid line, a dub |
| BELLS & METAL | 6 | tubular bells into SHIMMER, an inharmonic bowl, struck steel, a chime ring-modulated, a steel drum dubbed, a cluster of chimes played backwards |
| PADS | 6 | glass, strings swelling through BLOOM, a choir through vowels, a saw pad, frozen pipes, a water garden |
| LEADS | 6 | a hollow square, a fuzzed wasp, a breathy flute, a feedback scream, a brass stab, a talk box |
| PLUCKS | 6 | a koto thrown side to side, a harp in a mirror, a glass pluck, a sitar, a nylon guitar, a thumb piano |
| FX & ATMOS | 6 | laser rain, a wind organ, a descent into a hall, a garden backwards, a train dubbed, a ghost on the radio |
| MOVING | 6 | a MOD is most of the sound: STEPPERS on BRIGHT, LEVEL, DECAY and PITCH ENV, drifts on DETUNE, FEEDBACK swept |

**Two kinds.** 30 take a factory voice as `base` (named `ROM 1B / E.PIANO 3`: cartridges share
names) and give it effects, sends and MODs; where it was too quiet two carriers' LEVELs are set up
(Sitar Swirl). 19 are **new voices** (Feedback Tide is Saw Drift's with two operators changed), each written an operator a line: the word **`op N`** and the
operator's 21 settings in the original's order (added to `app/presets.cpp` for this; FM-6's only;
`presets_test` `testFm6sOpLine`). Their recipes, in short:

| Recipe | Presets |
| --- | --- |
| ALGORITHM 5, three pairs, each carrier's modulator at a ratio of its own: a body (1:1), a tine (ratio 14, gone in a moment), a glass layer bent by 3.51 | Glass Tines, Reed Piano, Motor Vibes, Glass Pluck, Thumb Piano |
| inharmonic ratios (1.41, 2.76, 3.51, 5.4; ALGORITHM 31's five carriers at 1, 2.32, 3.93, 5.35, 7.14), long decays, DETUNE drifting so the partials beat | Glass Bowl, Chime Cluster, Hammered Steel |
| ratio 1:1 pairs at a high index: a saw; slow first rates for a pad | Saw Drift, Glass Pad, Feedback Tide |
| a stack with FEEDBACK 7 at its top: a rough saw or a scream | Growl, Scream, Acid FM |
| ratio 2 on 1: odd harmonics, a hollow square | Hollow Square |
| a modulator whose index falls fast: a filter's sweep made by FM | Acid FM, Glass Pluck |
| the PITCH ENV up at once and down: a boing a STEPPER on PITCH ENV makes larger or smaller | Rubber Sub, Pitch Steps |
| FEEDBACK 7 at ratio 7 bending a carrier: hiss, through a VOWEL | Wind Organ |

**Each preset's MODs are heard, not only moving.** A MOD on BRIGHT is heard only where a modulator
sounds (ALGORITHM 32's six carriers have none), FEEDBACK up does nothing at 7, LFO does nothing where
PMS and AMS are 0, and a FEEDBACK change that a low-pass then takes away is all but silent. So the
test plays the phrase once more with each MOD put OFF and compares the two, sample for sample (the
engine is fresh both times, so with nothing changed they are the same to the bit): the difference
must be at least 1% of the sound (-40 dB). Two MODs written first failed it and were changed (Reed
Piano's FEEDBACK, -61 dB at first; Worn Celeste's BRIGHT on a voice whose only modulator is op 6,
-31 dB, passed but was moved to the TAPE's AGE). Tried by breaking it: an LFO aimed at a patch with
PMS 0 (Glass Tines: it moves, 0.70, but is heard at -73 dB), a MOD at no depth (Glass Bowl: swing 0)
and two carriers turned down (Koto Pong: -51 dB) each fail it, naming the preset. **What it cannot
catch**: whether a MOD is heard as music. A drift on DETUNE or a WOW moves the waveform a lot (0 dB
apart) while it may be heard as a slight beating.

### How they were measured (`presets_test --heard-fm6`, in ctest as `presets_heard_fm6`)

Each added preset is put on an FM-6 track of a new song (TAPE on ECHO, HALL on SPACE, as a new song
has) and plays a bar at 120 BPM round twice, then Stop and the tail. **Keys** (every folder but BASS):
a low C (48) held the whole bar under four notes up (60, 67, 64, 72), a four-key chord held six
steps (55, 60, 64, 71: loose notes, one moment), a high note (79). **BASS**: a line around C1 and C2
(36, 43, 36, 40, 48), G0 (31) held six steps, 43. All at the level a note is placed at. Everything
the track and the buses give is read off the master. Held:

| Limit | Value | Why |
| --- | --- | --- |
| The loudest tenth of a second (RMS, the louder side) | -26 to -10 dB | the 160 factory voices played the same way: the middle -20.4 dB; ROM 1A from -29.1 (GUITAR 2) to -12.1 (ORCHESTRA), the five banks to -8.3 (PERC BRASS), TAKE OFF -46 (it swells after 10 s) and EXPLOSION -42 under |
| The largest sample, the tail too | under 0.9 | the factory's loudest, PERC BRASS, is 0.899 |
| The tail, Stop to the last tenth over -80 dB | ends, within 60 s | the factory's longest is EXPLOSION, 13.4 s; the longest here is Train Dub, 11.0 s (DUB) |
| Every sample | a number | |
| Each MOD on | moves (swings by more than 0.005) and is heard (the phrase without it is at least -40 dB apart) | above |

`presets_test --measure-fm6` prints the same figures for every FM-6 built-in, the 160 too (about 15 s).
**No checksum is pinned**: the owner tunes them by ear, and a tuned preset only has to stay inside the
limits.

### The presets (★: the ten to try first)

Loudest is the loudest tenth of a second with the phrase; MOD n's heard figure is how far the phrase
moved without it.

| Folder | Name | The idea | What makes it (synth; slots; sends. MODs) | Loudest | Peak | Tail |
| --- | --- | --- | --- | --- | --- | --- |
| KEYS | **Suitcase** ★ | the classic tine piano in its amp, panning side to side | E.PIANO 1; TREMOLO 1/8 SPREAD 100%, CHORUS, PLATE; SPACE 12%. LFOs: BRIGHT (2 BARS), the TREMOLO's DEPTH -40% (4 BARS) | -15.8 dB | 0.52 | 1.5 s |
| KEYS | **Glass Tines** ★ | a new tine piano: two bodies drifting in and out of tune, a tine, a glass layer bent off true | new, ALGORITHM 5; ENSEMBLE, PLATE; ECHO 10%. LFOs: DETUNE (FREE 0.08 Hz), BRIGHT -30% (4 BARS) | -19.6 dB | 0.26 | 2.6 s |
| KEYS | Bark Piano | a bright piano that barks on some notes | ROM 1B E.PIANO 3; SATURATE TUBE, PHASER, SPRING. STEPPER of 8: BRIGHT; LFO: the DRIVE (2 BARS) | -16.5 dB | 0.48 | 0.8 s |
| KEYS | Reed Piano | a reed piano: nasal, its amp's tremolo | new, ALGORITHM 5, FEEDBACK 5; DRIVE, TREMOLO 1/8; SPACE 15%. LFOs: BRIGHT (a TRIANGLE a bar), the TREMOLO's DEPTH (2 BARS) | -19.7 dB | 0.31 | 1.0 s |
| KEYS | Funk Clav Wah | a clav through a wah pedal that steps | ROM 3B FUNK CLAV; VOWEL, EQ, SPRING. STEPPER of 8: the VOWEL, 1/8; LFO: PAN (a bar) | -24.3 dB | 0.17 | 0.4 s |
| KEYS | Worn Celeste | a celeste on a worn tape | ROM 1B CELESTE; WEAR, TAPE 1/8 DOT; SPACE 20%. LFOs: WEAR's WOW (FREE DRIFT), the TAPE's AGE (4 BARS) | -13.2 dB | 0.68 | 3.0 s |
| KEYS | Motor Vibes | vibes whose motor slows and speeds | new, ALGORITHM 5 (the bar's fourth partial, a strike at 10); TREMOLO 1/8 TRIP, HALL. LFOs: the TREMOLO's DEPTH (4 BARS), DECAY (2 BARS) | -21.2 dB | 0.26 | 1.5 s |
| BASS | **Growl** ★ | a wobble bass: a rough saw through a resonant low-pass that swings | new, ALGORITHM 3, FEEDBACK 7; FILTER LOW 24, DRIVE. LFOs: the CUTOFF +50% (1/4 TRIP), FEEDBACK -40% (a RAMP a bar) | -24.6 dB | 0.17 | 0.1 s |
| BASS | Slap Snap | the slap bass, squeezed, its slaps and pops stepping | ROM 1A BASS 1; COMP, EQ, SNAP. STEPPER of 6: BRIGHT, every step | -18.8 dB | 0.54 | 0.1 s |
| BASS | Rubber Sub | a sub that boings: the pitch flicks up and falls | new, ALGORITHM 5 (two pairs), the PITCH ENV up and down; DUB 1/8, COMP. STEPPER of 8: PITCH ENV, 1/8 | -24.3 dB | 0.21 | 1.1 s |
| BASS | Fretless Chorus | a fretless in a chorus, in and out of tune | ROM 3B FRETLESS 2; CHORUS, COMP, EQ. LFOs: DETUNE (FREE DRIFT), BRIGHT (2 BARS) | -14.4 dB | 0.32 | 0.2 s |
| BASS | **Acid FM** ★ | an acid line made by FM: the index falls like a filter, accents step | new, ALGORITHM 1, FEEDBACK 6; DRIVE, PING 1/8. STEPPERS: BRIGHT (8, 1/8) against DECAY (5, 1/8) | -23.2 dB | 0.18 | 1.0 s |
| BASS | Dub Sub | a synth bass into a dark dub | ROM 2B SYN-BASS 1; FILTER LOW 12, DUB 1/8 DOT; SPACE 10%. LFOs: the DUB's FEEDBACK (4 BARS), the CUTOFF (2 BARS) | -22.4 dB | 0.17 | 3.6 s |
| BELLS & METAL | **Shimmer Bells** ★ | tubular bells into a SHIMMER that swells | ROM 1A TUB BELLS; SHIMMER; ECHO 10%, SPACE 20%. LFOs FREE: the SPACE send (DRIFT 0.07 Hz), the SHIMMER's amount (0.05 Hz) | -19.1 dB | 0.35 | 7.5 s |
| BELLS & METAL | Glass Bowl | a bowl of inharmonic partials beating | new, ALGORITHM 5 (1.41, 2.76, 3.51, 5.4); PLATE; SPACE 15%. LFOs FREE: DETUNE (DRIFT), PAN | -19.6 dB | 0.34 | 5.6 s |
| BELLS & METAL | Hammered Steel | struck plate steel, hammer and tap | new, ALGORITHM 5 (3.15, 1.98, 7.14, 4.2), FEEDBACK 6; SATURATE HARD, PING 1/4 TRIP. STEPPER of 6: BRIGHT; LFO: the PING's FEEDBACK | -22.3 dB | 0.25 | 1.5 s |
| BELLS & METAL | Clang Ring | an orchestra chime through a ring modulator | ROM 1A ORCH-CHIME; RING 360 Hz, HALL. LFOs: the RING's FREQ (FREE DRIFT), its MIX -40% (4 BARS) | -18.4 dB | 0.43 | 3.2 s |
| BELLS & METAL | Pan Dub | a steel drum into a dub, thrown about | ROM 1A STEEL DRUM; DUB 1/8 DOT; SPACE 10%. LFO: the DUB's TONE (2 BARS); STEPPER of 6: PAN, 1/8 | -15.5 dB | 0.50 | 4.1 s |
| BELLS & METAL | **Chime Cluster** ★ | five inharmonic chimes, each again backwards | new, ALGORITHM 31 (1, 2.32, 3.93, 5.35, 7.14); REVERSE 1/4, HALL. LFOs: DETUNE (FREE DRIFT), the REVERSE's FEEDBACK (4 BARS) | -23.8 dB | 0.19 | 4.2 s |
| PADS | **Glass Pad** ★ | a slow glass pad whose brightness and place drift | new, ALGORITHM 5, slow attacks, ratio 3 and 5 modulators, a slow vibrato; ENSEMBLE, HALL. LFOs FREE: BRIGHT (DRIFT 0.06 Hz), PAN | -21.9 dB | 0.20 | 4.4 s |
| PADS | Bloom Strings | strings swelling through BLOOM | ROM 1A STRINGS 2; BLOOM 1.1 s. LFOs: DETUNE (2 BARS), the BLOOM's MIX (4 BARS) | -18.0 dB | 0.30 | 6.0 s |
| PADS | Vowel Choir | the factory voice sung through vowels | ROM 1A VOICE 1; VOWEL, HALL. LFOs: the VOWEL (2 BARS), LFO +40% (4 BARS) | -21.0 dB | 0.28 | 3.7 s |
| PADS | Saw Drift | a three-saw pad under a slow low-pass | new, ALGORITHM 5, each pair 1:1; FILTER LOW 24, CHORUS ENSEMBLE; SPACE 25%. LFOs: the CUTOFF -40% (4 BARS), FEEDBACK (8 BARS) | -17.9 dB | 0.29 | 1.4 s |
| PADS | Frozen Pipes | an organ pipe in a long plate, breathing | ROM 1A PIPES 1; PLATE 13 S, EQ. LFOs: LEVEL -30% (FREE 0.25 Hz), BRIGHT (4 BARS) | -17.4 dB | 0.45 | 6.7 s |
| PADS | Water Bloom | the water garden swelling through BLOOM | ROM 2B WATER GDN; CHORUS ENSEMBLE, BLOOM 1.6 s. LFOs FREE: the BLOOM's MIX (DRIFT), PAN | -25.3 dB | 0.19 | 6.5 s |
| LEADS | Hollow Square | a hollow lead, its vibrato widening | new, ALGORITHM 5 (two pairs at 2:1), a late vibrato; DRIVE, PING 1/8 DOT. LFOs: LFO +50% (2 BARS), BRIGHT (a bar) | -19.1 dB | 0.27 | 2.0 s |
| LEADS | Wasp | a buzzing synth lead, fuzzed | ROM 2B SYN-LEAD 2; FUZZ, PING 1/8. STEPPER of 8: BRIGHT; LFO: the FUZZ's TONE | -14.9 dB | 0.46 | 1.3 s |
| LEADS | Breathy Flute | a flute whose breath and vibrato deepen | ROM 1A FLUTE 1; TAPE 1/4, PLATE; SPACE 15%. LFOs: LFO +50% (4 BARS), BRIGHT (2 BARS) | -20.1 dB | 0.30 | 3.0 s |
| LEADS | Scream | a feedback scream that settles and screams again | new, ALGORITHM 1, FEEDBACK 7; SATURATE TUBE, DELAY 1/8 DOT. LFOs: FEEDBACK -50% (1/4), BRIGHT (2 BARS) | -23.1 dB | 0.27 | 1.5 s |
| LEADS | Brass Stab | synth brass, some notes swelling in | ROM 2B SYNBRASS 1; COMP, PLATE. STEPPER of 4: ATTACK; LFO: BRIGHT -40% (2 BARS) | -13.5 dB | 0.51 | 1.9 s |
| LEADS | **Talk Box** ★ | a lead that talks: a vowel stepping every eighth | ROM 1A SYN-LEAD 1; VOWEL, COMP. STEPPER of 8: the VOWEL +90%; LFO: the VOWEL's SHIFT (4 BARS) | -19.8 dB | 0.22 | 0.1 s |
| PLUCKS | Koto Pong | a koto thrown side to side | ROM 1A KOTO; PING 1/8, EQ. STEPPER of 6: PAN, every step; LFO: BRIGHT (2 BARS) | -22.7 dB | 0.30 | 1.4 s |
| PLUCKS | Harp Mirror | a harp with each note again backwards | ROM 1B HARP 1; REVERSE 1/8 DOT, HALL. LFOs: the REVERSE's MIX -40% (4 BARS), RELEASE (2 BARS) | -13.0 dB | 0.65 | 3.7 s |
| PLUCKS | Glass Pluck | a pluck bright at the start, glass in it | new, ALGORITHM 5 (ratio 7 glass), FEEDBACK 4; PING 1/4 TRIP, PLATE. LFOs: BRIGHT (a bar), PAN (FREE DRIFT) | -23.1 dB | 0.22 | 1.8 s |
| PLUCKS | Sitar Swirl | a sitar in a drifting phaser | ROM 1B SITAR, its carriers up; PHASER, SPRING, EQ. LFOs: the PHASER's FEEDBACK (FREE DRIFT), DETUNE (4 BARS) | -20.5 dB | 0.32 | 5.9 s |
| PLUCKS | Nylon Chorus | a nylon guitar in a chorus | ROM 3B CLAS.GUIT; CHORUS, PLATE; ECHO 12%. LFOs: BRIGHT (4 BARS), PAN (FREE DRIFT) | -19.4 dB | 0.38 | 2.2 s |
| PLUCKS | Thumb Piano | two tines a little apart, hard and soft thumbs | new, ALGORITHM 5 (a knock at 5.2, a partial at 8.0); TAPE 1/8, SPRING, COMP. STEPPER of 5: BRIGHT; LFO: the TAPE's AGE (4 BARS) | -20.8 dB | 0.44 | 1.6 s |
| FX & ATMOS | Laser Rain | zaps falling side to side | ROM 2B LASER GUN; PING 1/8 TRIP, HALL. STEPPER of 7: PAN, every step; LFO: PITCH ENV -40% (4 BARS) | -17.1 dB | 0.48 | 3.4 s |
| FX & ATMOS | **Wind Organ** ★ | a pipe with wind in it, sung through a vowel, in gusts | new, ALGORITHM 5 (two pairs; FEEDBACK 7 at ratio 7: hiss), slow; VOWEL, HALL. LFOs FREE: the VOWEL (DRIFT), LEVEL -20% (DRIFT) | -22.3 dB | 0.16 | 4.5 s |
| FX & ATMOS | Descent Space | a falling sound in a big hall, wandering | ROM 2B DESCENT; HALL 13 S, EQ. LFOs: the HALL's MIX (FREE DRIFT), PAN (2 BARS) | -17.9 dB | 0.52 | 7.6 s |
| FX & ATMOS | Backwards Garden | the water garden, half backwards | ROM 2B WATER GDN; REVERSE 1/4, HALL; ECHO 15%. LFOs: the REVERSE's FEEDBACK (4 BARS), PAN (FREE DRIFT) | -18.3 dB | 0.50 | 4.1 s |
| FX & ATMOS | Train Dub | the factory train, filtered and dubbed | ROM 1A TRAIN; FILTER LOW 12, DUB 1/4, COMP. LFOs: the CUTOFF (4 BARS), the DUB's FEEDBACK (8 BARS) | -25.9 dB | 0.14 | 11.0 s |
| FX & ATMOS | Radio Ghost | a voice on a radio being tuned | new, ALGORITHM 5, slow; RING 180 Hz, WEAR, COMP; SPACE 25%. LFOs FREE: the RING's FREQ (DRIFT), LEVEL (DRIFT) | -22.7 dB | 0.32 | 1.6 s |
| MOVING | Bright Steps | a synth clav with sixteen brightnesses | ROM 2B SYN-CLAV 1; PING 1/8. STEPPERS: BRIGHT (16, every step) against FEEDBACK (5, 1/8) | -21.8 dB | 0.25 | 1.1 s |
| MOVING | Gate Strings | strings chopped by a gate | ROM 1A STRINGS 1; DELAY 1/8 DOT, CHORUS ENSEMBLE. STEPPER of 8: LEVEL +100%, every half step (-1 is silence); LFO: DETUNE (4 BARS) | -12.4 dB | 0.44 | 2.0 s |
| MOVING | Detune Drift | an electric piano in and out of tune | ROM 1B E.PIANO 2; CHORUS, TAPE 1/8 DOT. LFOs FREE: DETUNE +70% (DRIFT 0.06 Hz), LEVEL -30% (DRIFT) | -19.9 dB | 0.25 | 2.1 s |
| MOVING | Decay Walk | a marimba: a note short, a note rings | ROM 1A MARIMBA; PING 1/8 DOT, PLATE. STEPPER of 8: DECAY +70%, 1/8; LFO: BRIGHT (2 BARS) | -24.0 dB | 0.27 | 1.3 s |
| MOVING | Pitch Steps | each note falls a different way | new, ALGORITHM 5, the PITCH ENV from above; PING 1/4 TRIP, COMP; SPACE 12%. STEPPER of 6: PITCH ENV; LFO: PAN (FREE DRIFT) | -23.6 dB | 0.40 | 1.8 s |
| MOVING | **Feedback Tide** ★ | Saw Drift's three saws, one torn by FEEDBACK and smoothed again | Saw Drift with its third pair up, FEEDBACK 2; FILTER LOW 24, ENSEMBLE; SPACE 20%. LFOs: FEEDBACK +70% (a TRIANGLE over 4 BARS), the CUTOFF (FREE DRIFT) | -18.3 dB | 0.35 | 1.3 s |

Every level was set by measuring: from -25.9 (Train Dub) to -12.4 dB (Gate Strings), the middle about
-20, the factory's own middle -20.4. Quiet first drafts were brought up by more carriers, not by a
COMP: COMP's make-up is half of what a FULL-SCALE sound loses, so on a sound at -25 dB a low
threshold turned it down (Thumb Piano went from -26 to -28 before a third pair was given it).

### The ten to try first

| Preset | Why it is a favourite (on paper) |
| --- | --- |
| KEYS / Suitcase | the tine piano everyone knows, in its amp: an autopan whose depth comes and goes and a bark that swells |
| KEYS / Glass Tines | a new piano of three pairs: two bodies drifting apart beat slowly, a tine at ratio 14, a glass layer bent off true |
| BASS / Growl | a rough FEEDBACK 7 saw through a resonant low-pass that wobbles in triplets, the growl clearing each bar |
| BASS / Acid FM | the acid line made by FM: two STEPPERS, 8 and 5 long, on BRIGHT and DECAY, so the accents never repeat the same way |
| BELLS & METAL / Shimmer Bells | tubular bells into a SHIMMER whose amount and SPACE send drift on their own clocks: the owner's own example |
| BELLS & METAL / Chime Cluster | five inharmonic chimes (ALGORITHM 31) beating against each other, each again backwards |
| PADS / Glass Pad | slow glass: ratio 3 and 5 modulators on detuned bodies, its brightness and its place drifting free |
| LEADS / Talk Box | a lead that says something different every eighth note |
| FX & ATMOS / Wind Organ | FEEDBACK 7 at ratio 7 is hiss: wind through a pipe, a vowel drifting, gusts on LEVEL |
| MOVING / Feedback Tide | three saws, one torn by FEEDBACK over four bars and smoothed again |

### Cost

The checker times the engine while the phrase plays, the song's two buses included (this container,
the best of four runs on a desk shared with other builds; the factory voices read 0.17% (E.ORGAN 1) to
0.93% (HARPSI-STG) the same way, E.PIANO 1 0.36%). **The heaviest: Bloom Strings 1.12%** (BLOOM, and
STRINGS 2's 0.81% alone), Shimmer Bells 1.05% (SHIMMER, TUB BELLS ringing), Water Bloom 0.97%
(BLOOM); the middle 0.62%, the lightest Rubber Sub 0.21% (four operators, no reverb). On the handheld
(x 13 to 17) the heaviest is 15 to 19% of a core for the track. **Not run on the handheld.** None
stacks two of BLOOM, SHIMMER and FOLD (none uses FOLD); a reverb a preset only needs a little of is
a send into the song's own SPACE.
