# Changes made here to code copied in

One line each: what and why. A fresh copy from where the code came from loses
these unless they are put back.

- `easycz/dsp/cz_synth.c`, `cz_synth.h` (2026-11-12): a glide can be given a
  length. `porta_rate16` on the synth (0: the CZ's own PORTAMENTO TIME, as
  before), `glide_rate16` and `glide_frac` on a voice, `pending_rate16` on a
  slot, and `cz_synth_glide_rate16(seconds)`. Why: Apotheca's GLIDE is a
  length in steps that follows the tempo; the CZ's one fixed time (92 ms)
  could not be it. With `porta_rate16` at 0 nothing is changed.
  A note that waits for its slot keeps the rate and the key to slide from
  that it was played with (`pending_rate16`, `pending_from`): two gliding
  notes played at one moment each took the other's.
- Phazed DETUNE no longer cuts held notes at its middle (2026-11-14). **No file
  under `third_party/` changed**: the mend is in Apotheca's wrapper,
  `engine/src/phazed.cpp` (`keepTwin`, `lineBack`). Recorded here because it
  works round the copied `cz_synth_edit_tone`, which fades every voice when a
  tone's line select changes. DETUNE off the middle makes a one-line sound a
  twin (1 + 1'), and at exactly 0.5 one line again: a held note fell from RMS
  0.087 to 0.008. Now a twin made by DETUNE stays a twin at the middle (not
  detuned) while any voice sounds, and goes back to one line before the next
  note that finds the synth silent. A note that BEGINS on the kept twin (a
  legato part never falls silent) has its lines in tune and in phase, 3 dB
  over the patch: from that note on the twin is trimmed 3 dB more
  (`kDoubleInTuneTrim`, `trimInTune`), given back when DETUNE leaves the middle.
  Moving DETUNE OFF the middle with notes held still changes the layout and
  fades them, once. Held by `phazed_test`
  (`testDetuneThroughTheMiddleKeepsTheNote`). A fresh copy of `cz_synth.c`
  keeps it, since the copy is not touched.

## msfa (FM-6's core), 2026-11-15

Copied from google/music-synthesizer-for-android at f67d41d (`app/src/main/jni`),
unchanged in its first commit here; then changed as below. Each changed file
says so at its top. **Checked against the original**: with these changes, a
note with nothing bent (a fresh `NoteBend`), at the original's own envelope
rate, with no TRANSPOSE and no AM makes the original's numbers to the bit, and
the same cut into pieces of 1, 17, 3 and 40 samples: all 16 of the factory
cartridge's 32 that have neither (a scratch harness built both side by side,
2026-11-15; the plan, section 4).

- **Everything in `namespace msfa`**, and `synth.h`'s `N` and `LG_N` are
  constants, not macros (a macro named `N` broke any header after it). `min`
  and `max` are std's. Why: the tables were globals named `lut`, `sintab`...
  linked into one program with everything else.
- **No NEON, no memory barrier** (`synth.h`, `fm_op_kernel.*`). The NEON
  kernel was a float approximation of the sine, so the handheld would not
  have made the desk's numbers, and it needed a period of whole fours. The
  experimental kernels that were switched off (`#if 0`) are gone.
- **A period can be cut anywhere** (`fm_core.*`, `fm_op_kernel.*`): each
  kernel takes a starting gain, its step and a count; `FmCore::compute` takes
  where in the period to start (`from`) and how many (`count`), ramping the
  gains as over the whole period. Why: the engine renders blocks of any length
  and a key must start on its own sample; the same sound in any blocks.
- **Feedback is a gain, not a shift** (`compute_fb`: `(y0 + y) * fb_gain >>
  32`; the original's shift s is `fb_gain = 1 << (31 - s)`, the same to the
  bit). Why: the FEEDBACK macro sits between the patch's steps.
- `FmCore::is_carrier`, and `kLevelThresh` shared (the voice asks when it is
  silent).
- **The envelope's rates follow the sample rate** (`Env::init_sr`, multiplied
  in `advance`): the original counted its own 49096 samples a second, so at
  48 kHz every envelope ran 2.3% slow. `fm6_test testEnvelopeTimes` holds the
  decay to msfa's own measured figure within 1%.
- **A retrigger may keep the envelope's level** (`Env::init(..., keep_level)`):
  a stolen voice or a key played again carries on, no click.
- **Rates bent while a note sounds** (`Env::bend_rates`, clamped 0..63): the
  ATTACK, DECAY and RELEASE macros. And `level()`, `stage()`, `down()`.
- `Env`, `PitchEnv` and `Lfo` start as numbers (their members were left as
  whatever was in memory); `Lfo::start()` puts it back to its start (Play from
  the top is the same sound); `(a || b && c)` given its brackets.
- **`Dx7Note` is two halves**, `control()` once a period (the envelopes, the
  LFO, the gains: the original `compute`'s first half) and `render(buf, from,
  count)`; `silent()` says when a let-go note can be freed (the original had
  no end of a note). It also plays what the original left out: **TRANSPOSE**
  (24 is none), **OPERATOR ON/OFF** (`patch[155]`, bit 5 OP1 .. bit 0 OP6),
  **OSCILLATOR KEY SYNC** (on a retrigger), and **amplitude modulation** (AMD
  and each operator's AMS; the depths, 0, 3, 9 and 24 dB at AMD 99, are a
  GUESS). A carrier's release always ends in silence whatever its L4 (on the
  original it rang for ever). `NoteBend` carries the macros. The feedback's two
  samples start at 0 on a fresh note (the original never set them). The pitch
  is held to Freqlut's range (-11 to 20 octaves). The helpers (`osc_freq`,
  `ScaleVelocity`...) are the original's, given internal linkage.
- Built with `-fwrapv` (CMake and `scripts/build-handheld.sh`): its phases wrap
  round on purpose, which C++ calls undefined for a signed number.
