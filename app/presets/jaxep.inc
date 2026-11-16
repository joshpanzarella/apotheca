R"presets(
# JAX-EP's built-in presets past the booklet's 32, and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/jx3p.hpp
# (ids 0 to 31; 32 and 33, VELOCITY and BRILLIANCE, are the track's own and no preset sets them).
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.

new PADS / Warm Pad
base String I                   # (BANK A 1), by its name. Then:
set 27 70  28 60  29 80  30 75  # ATTACK 7.0, DECAY 6.0, SUSTAIN 8.0, RELEASE 7.5
set 31 0                        # the synth's own CHORUS off: the track's CHORUS does it
fx 1 chorus 0.3 0.6 0 0.7       # RATE, DEPTH, TYPE, MIX
fx 2 hall 0.7 0.6 0.45 0.35     # SIZE, DECAY, TONE, MIX
send 1 0.15                     # a little into ECHO
mod 1 1 0 0.6 9 0 0 0           # MOD 1: an LFO on macro 1 (CUTOFF), +20%, 2 BARS, SINE
)presets"
