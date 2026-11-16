R"presets(
# ACID's built-in presets past its own table (Classic Saw, Classic Square), and the track sound any
# built-in carries. How to write one: docs/plans/presets-plan.md. Settings by id:
# engine/include/apotheca/acid_rule.hpp (0 WAVE, 1 OCTAVE, 2 TUNE, 3 CUTOFF, 4 RESONANCE, 5 ENV MOD,
# 6 DECAY, 7 ACCENT, 8 SWEEP, 9 DRIVE, 10 AMP DECAY, 11 SLIDE, 12 VELOCITY, 13 LEVEL).
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# (The 24 of the plan's section 5 past CLASSIC, batch 4: names made up, no artist's, record's or maker's.)
# Ids: 0 WAVE (0 SAW 1 SQUARE), 1 OCTAVE, 2 TUNE, 3 CUTOFF, 4 RESONANCE, 5 ENV MOD, 6 DECAY, 7 ACCENT,
# 8 SWEEP, 9 DRIVE, 10 AMP DECAY, 11 SLIDE (0 NOTE 1 FIXED), 12 VELOCITY, 13 LEVEL. A new preset
# starts from Classic Saw. Every one plays the acid line of `presets_test --heard-acid` inside its
# limits (loudness, peak, no sample into the synth's clip at 90% or accented, a tail that ends, its
# MODs moving and heard). Figures: acid-plan.md, batch 4. Nobody has heard any of them.

# ---- CLASSIC: the engine's own eight (engine/src/acid.cpp); two of them send into ECHO ---------------
preset Echo Line
send 1 0.25                     # into ECHO (a new song's TAPE)
preset Echo Square
send 1 0.3

# ---- SQUELCH: resonance and envelope up, short decays, the sweep climbing --------------------------
new SQUELCH / Squelch
set 3 15  4 90  5 85  6 20  7 70  8 80  13 -6
new SQUELCH / Rubber Duck
base Classic Square
set 3 12  4 92  5 90  6 15  7 70  8 70  13 -3
new SQUELCH / Wet Snap
set 3 20  4 88  5 80  6 10  7 80  8 90  13 -10
new SQUELCH / Screamer
set 3 25  4 96  5 95  6 25  7 90  8 70  13 -16
new SQUELCH / Step Squelch
set 3 15  4 88  5 80  6 20  7 70  8 80  13 -13
mod 1 2 0 0.75 1 0 0 0          # MOD 1: a STEPPER on CUTOFF, +50%, a step each
modsteps 1 16 0 0 0 0.6 -0.4 1 0 0.3 -0.6 0.8 0 -0.2 0.5 1 -0.5 0 0.7 0.2
new SQUELCH / Gurgle Steps
base Classic Square
set 3 14  4 90  5 85  6 18  7 70  8 75  13 -11
mod 1 2 0 0.8 3 0 0 0           # a STEPPER on CUTOFF, +60%, an eighth each, sliding into each step
modsteps 1 8 0.5 0 -0.5 0.5 1 -1 0.2 0.8 -0.3 0 0 0 0 0 0 0 0
new SQUELCH / Chance Squelch
set 3 18  4 89  5 80  6 22  7 75  8 85  13 -15
mod 1 2 0 0.75 1 0 0 0          # a STEPPER on CUTOFF, +50%, a third of its steps rolled afresh
modsteps 1 16 0 0.3 0.5 0 -0.5 0 1 0 -0.3 0 0.5 0 -0.8 0 0.2 0 0.9 0
new SQUELCH / Long Squelch
set 3 15  4 90  5 75  6 60  7 70  8 90  13 -14
mod 1 2 0 0.7 1 0 0 0           # a STEPPER of seven against the bar of sixteen: the line shifts
modsteps 1 7 0.2 0 1 0 -0.5 0.5 -1 0 0.3 0 0 0 0 0 0 0 0 0

# ---- DEEP: low cutoff, little envelope, an octave down, a long amp ---------------------------------
new DEEP / Sub Line
set 1 -1  3 15  4 20  5 15  10 95
new DEEP / Deep Square
base Classic Square
set 1 -1  3 18  4 35  5 20  10 90  13 3
new DEEP / Warm Floor
set 1 -1  3 22  4 45  5 25  6 60  10 90
new DEEP / Dark Round
base Classic Square
set 1 -1  3 12  4 50  5 30  7 30  10 90  13 4
new DEEP / Low Murmur
set 1 -1  3 25  4 30  5 10  10 95  11 1
new DEEP / Velvet Bass
set 1 -1  3 20  4 40  5 20  10 90  12 1

# ---- DISTORTED: DRIVE up, and DRIVE, SATURATE or FUZZ in slot 1 ------------------------------------
new DISTORTED / Driven
set 3 25  4 70  5 60  9 50  13 -3
fx 1 drive 0.4 0.6 1 0.5         # DRIVE, TONE, MIX, OUTPUT
new DISTORTED / Saturated
set 3 15  4 85  5 80  6 20  9 30  13 -3
fx 1 saturate 0.5 0 0.7 1        # DRIVE, TYPE (TAPE), TONE, MIX
new DISTORTED / Fuzz Line
base Classic Square
set 3 30  4 60  5 50
fx 1 fuzz 0.6 0.5 0.1 1          # FUZZ, TONE, GATE, MIX
send 1 0.15
new DISTORTED / Tube Acid
set 3 22  4 75  5 65  9 30
fx 1 saturate 0.4 0.25 0.6 1     # TYPE TUBE
new DISTORTED / Burnt Rubber
set 3 18  4 92  5 90  6 20  9 70  8 80  13 -8
fx 1 drive 0.5 0.5 1 0.45
send 1 0.2
new DISTORTED / Dub Smash
base Classic Square
set 3 25  4 70  5 60  9 40
fx 1 fuzz 0.5 0.4 0.1 1
fx 2 dub 0.5625 0.4 0.45 0.25    # TIME (a dotted eighth), FEEDBACK, TONE, MIX

# ---- MOVING: a MOD does the playing ----------------------------------------------------------------
new MOVING / Breathing
set 3 22  4 75  5 50  13 -9
mod 1 1 0 0.7 9 0 0 0            # an LFO on CUTOFF, +40%, 2 BARS, SINE
new MOVING / Wobble Reso
base Classic Square
set 3 25  4 70  5 55
mod 1 1 1 0.7 5 1 0 0            # an LFO on RESONANCE, +40%, a quarter, TRIANGLE
new MOVING / Semitone Steps
set 3 25  4 75  5 60  13 -7
mod 1 2 7 1 1 0 0 0              # a STEPPER on TUNE, +100%: a semitone up or down, a step each
modsteps 1 16 0 0 0 0 1 0 0 -1 0 1 0 0 0 -1 0 0 1 0
mod 2 1 0 0.65 8 0 0 0           # and CUTOFF, +30%, a bar
new MOVING / Drift
set 3 20  4 82  5 70  6 25  13 -12
mod 1 1 0 0.75 10 6 0 0          # an LFO on CUTOFF, +50%, 4 BARS, DRIFT
mod 2 1 2 0.7 7 5 0 0            # ENV MOD, +40%, a half, RANDOM
)presets"
