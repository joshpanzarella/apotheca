R"presets(
# CLOUD's built-in presets past its own table (the STILL folder, engine/src/cloud.cpp), and the track
# sound any built-in carries. How to write one: docs/plans/presets-plan.md. Settings by id
# (engine/include/apotheca/cloud_rule.hpp): 0 POSITION, 1 SPRAY, 2 SIZE, 3 DENSITY, 4 SCATTER, 5 SHAPE,
# 6 PITCH SPRAY, 7 PITCH STEPS (0 FREE 1 OCTAVES 2 FIFTHS), 8 REVERSE, 9 STEREO, 10 SCAN (50 FREEZE,
# 75 x1, 25 x-1), 11 ROOT, 12 TUNE, 13 ATTACK, 14 RELEASE, 15 LEVEL, 16 VELOCITY. Macro lanes for a MOD:
# 0 POSITION, 1 SPRAY, 2 SIZE, 3 DENSITY, 4 SCAN, 5 PITCH SPRAY, 6 SHAPE, 7 REVERSE.
# A built-in never names a recording: it plays whatever the track holds (the TONE if nothing).
# A new preset starts from Soft Cloud. Every one is played by `presets_test --heard-cloud` through the
# TONE and through a recording the test makes (a rising sine and a noise burst): loudness, peak, a tail
# that ends, every MOD moving and heard. Nobody has heard any of them.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# Readings: SIZE 20 14 MS, 30 24 MS, 50 71 MS, 70 205 MS, 85 450 MS; DENSITY 30 5 HZ, 50 14 HZ,
# 65 31 HZ, 80 69 HZ; PITCH SPRAY 40 +-192 CENTS, 75 +-675, 90 +-972; SCAN 52 +0.08X, 45 -0.20X.

# ---- DRIFT: SCAN moves the place on while a key is held, or a MOD wanders it -------------------------
new DRIFT / Slow Tide
set 0 10  1 8  2 75  3 60  10 52  13 40  14 70
new DRIFT / Backwards River
set 0 80  1 12  2 70  3 65  8 30  9 70  10 45  14 70
send 2 0.35                      # into SPACE (a new song's HALL)
new DRIFT / Wander
set 0 40  1 6  2 80  3 60  5 60  9 70  13 40  14 70
mod 1 1 0 0.75 10 0 0 0          # MOD 1: an LFO on POSITION, +50%, 4 BARS, SINE: the cloud walks the recording
new DRIFT / Tape Scrub
set 0 20  1 3  2 50  3 70  4 20  14 60
mod 1 2 4 0.625 1 0 0 0          # a STEPPER on SCAN, +25%: steps at 0 FREEZE, steps at 1 scrub on at x1
modsteps 1 8 0 0 0 1 0 1 1 0 1 0 0 0 0 0 0 0 0
new DRIFT / Drifting Choir
set 1 15  2 85  3 55  5 75  6 30  9 80  10 55  13 60  14 75
mod 1 1 1 0.75 10 6 0 0          # an LFO on SPRAY, +50%, 4 BARS, DRIFT
fx 1 ensemble 0.4 0.6 0.7 0.6    # RATE, DEPTH, SPREAD, MIX: a section of it
new DRIFT / Pendulum
set 0 50  1 5  2 65  3 65  9 60  14 65
mod 1 1 4 0.625 9 1 0 0          # an LFO on SCAN, +25% (x-1 to x1), 2 BARS, TRIANGLE: back and forth
send 1 0.25                      # into ECHO (a new song's TAPE)

# ---- DUST: short grains, few and scattered, some backwards: crackle and specks -----------------------
new DUST / Dust
set 0 40  1 60  2 20  3 30  4 100  5 10  8 30  9 80  13 0  14 40
fx 1 wear 0.4 0.6 0.5 0.4        # WOW, DIRT, DROPS, TONE: an old record's surface
new DUST / Crackle
set 1 80  2 12  3 55  4 100  5 0  9 100  13 0  14 30
new DUST / Rain on Glass
set 1 70  2 25  3 65  4 100  5 20  6 40  9 100  13 0  14 45
send 1 0.3
new DUST / Embers
set 1 50  2 30  3 35  4 100  5 80  8 60  9 80  14 60
mod 1 1 3 0.75 5 5 0 0           # an LFO on DENSITY, +50%, a quarter, RANDOM: flaring and dying down
new DUST / Static Specks
set 1 100  2 5  3 80  4 100  5 30  6 60  9 100  13 0  14 30  15 70
new DUST / Step Dust
set 1 40  2 20  3 50  4 30  5 10  9 90  13 0  14 40
mod 1 2 3 1 1 0 0 0              # a STEPPER on DENSITY, +100%, a step each: bursts on the beat
modsteps 1 16 0 1 -1 -0.5 0.3 1 -1 0.6 -1 1 -1 -0.6 0.2 0.8 -1 0.5 -1

# ---- CHOIR: PITCH SPRAY on the grid of octaves or fifths, SWELL, long ----------------------------------
new CHOIR / Fifths Choir
set 0 50  1 20  2 85  3 60  4 60  5 80  6 75  7 2  9 60  13 40  14 70
new CHOIR / Octave Ghosts
set 0 35  1 10  2 90  3 55  5 85  6 90  7 1  9 80  13 60  14 75
fx 1 shimmer 0.6 0.7 0.6 0.35     # SHIMMER, DECAY, TONE, MIX: the octaves climb on
new CHOIR / Cathedral
set 0 55  1 25  2 95  3 70  5 70  6 80  7 2  8 20  9 100  13 60  14 85  15 70
send 2 0.3                       # into SPACE (a new song's HALL)
new CHOIR / Low Choir
set 0 30  1 15  2 90  3 60  5 80  6 80  7 1  9 70  11 48  13 50  14 75
new CHOIR / Bloom Choir
set 0 60  1 30  2 95  3 65  5 90  6 75  7 2  9 90  13 80  14 85  15 70
send 2 0.45
new CHOIR / Choir Sweep
set 0 45  1 15  2 85  3 60  5 80  6 60  7 2  9 70  13 40  14 70
mod 1 1 5 0.75 9 0 0 0           # an LFO on PITCH SPRAY, +50%, 2 BARS, SINE: the choir opens and closes
)presets"
