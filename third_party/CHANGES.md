# Changes made here to code copied in

One line each: what and why. A fresh copy from where the code came from loses
these unless they are put back.

- `easycz/dsp/cz_synth.c`, `cz_synth.h` (2026-11-12): a glide can be given a
  length. `porta_rate16` on the synth (0: the CZ's own PORTAMENTO TIME, as
  before), `glide_rate16` and `glide_frac` on a voice, `pending_rate16` on a
  slot, and `cz_synth_glide_rate16(seconds)`. Why: Apotheca's GLIDE is a
  length in steps that follows the tempo; the CZ's one fixed time (92 ms)
  could not be it. With `porta_rate16` at 0 nothing is changed.
