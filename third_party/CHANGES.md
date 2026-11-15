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
- `easycz/dsp/cz_output.c`, `cz_output.h`, `cz_synth.c`, `cz_synth.h` (2026-11-15,
  speed plan batch 1): the resampler's two filter tables (113 KB each) are made
  once and shared by every Phazed: `resampler_design`, `resampler_init_shared`
  (a `shared` flag so `resampler_free` leaves them), `cz_synth_init_shared`.
  Each track had its own, read every chip sample: a seventh of the busiest
  song's cache misses. And a synth at rest is not worked out: `cz_synth_resting`
  (idle, its output settled, the chorus fed silence longer than it remembers)
  and `cz_synth_rest` (what one-frame renders would do then: the coupling
  capacitor to zero, the chorus's count, the volume's glide; no samples).
  `cz_synth_init` and the render are as they were; Phazed (`engine/src/phazed.cpp`)
  uses the new calls. Same sound to the bit: `speed_test` holds a resting Phazed
  beside one that never rests, and `effects_test`'s pinned song stands.
