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
