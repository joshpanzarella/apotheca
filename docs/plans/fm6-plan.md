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

