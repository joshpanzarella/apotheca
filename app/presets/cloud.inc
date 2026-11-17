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
set 0 30  1 15  2 90  3 60  5 80  6 80  7 1  9 70  11 72  13 50  14 75
new CHOIR / Bloom Choir
set 0 60  1 30  2 95  3 65  5 90  6 75  7 2  9 90  13 80  14 85  15 70
send 2 0.45
new CHOIR / Choir Sweep
set 0 45  1 15  2 85  3 60  5 80  6 60  7 2  9 70  13 40  14 70
mod 1 1 5 0.75 9 0 0 0           # an LFO on PITCH SPRAY, +50%, 2 BARS, SINE: the choir opens and closes
# ---- More presets (2026-11-17, presets-plan.md, "More presets") --------------------------------------

# ---- PADS: long smooth grains, keys held, an effect to sit in -----------------------------------------
new PADS / Warm Grain Pad
set 0 30  2 80  3 65  5 60  9 70  13 50  14 70
fx 1 chorus 0.25 0.5 0.125 0.45        # RATE, DEPTH, TYPE, MIX
send 2 0.25
new PADS / Velvet Cloud
set 0 45  1 15  2 70  3 60  5 55  9 80  13 45  14 68
fx 1 ensemble 0.3 0.6 0.9 0.6          # RATE, DEPTH, SPREAD, MIX
new PADS / Slow Swell
set 0 35  1 10  2 90  3 55  5 100  9 60  13 70  14 75
fx 1 hall 0.7 0.55 0.45 0.3            # SIZE, DECAY, TONE, MIX
new PADS / Tape Pad
set 0 55  1 12  2 75  3 60  5 60  9 60  13 45  14 65
fx 1 wear 0.45 0.3 0.15 0.45           # WOW, DIRT, DROPS, TONE
fx 2 tape 0.6875 0.3 0.6 0.2           # TIME 1/4, FEEDBACK, AGE, MIX
new PADS / Breathing Pad
set 0 40  1 8  2 65  3 70  5 70  9 70  13 45  14 70
mod 1 1 2 0.75 9 0 0 0                 # MOD 1: an LFO on SIZE, +50%, 2 BARS, SINE: it breathes
new PADS / Hall Pad
set 0 20  1 20  2 85  3 60  5 65  6 15  9 90  13 50  14 75
send 2 0.5

# ---- DRONES: frozen in one place, long, low -----------------------------------------------------------
new DRONES / Deep Drone
set 0 30  1 5  2 95  3 60  5 60  9 60  11 84  13 40  14 80   # ROOT 84: every key an octave lower
new DRONES / Hum Drone
set 0 50  1 3  2 90  3 75  5 50  6 20  9 40  11 72  13 40  14 80
fx 1 eq 0.6 0.45 0.4 0.4               # LOW, MID, MID FREQ, HIGH: warm, the top taken off
new DRONES / Fifth Drone
set 0 40  1 10  2 90  3 65  5 70  6 50  7 2  9 70  13 50  14 80
new DRONES / Dark Drone
set 0 60  1 10  2 90  3 60  5 60  9 70  11 72  13 45  14 80
fx 1 filter 0.4 0.4 0.1 1              # CUTOFF, RESONANCE, LOW 12, MIX
mod 1 1 8 0.75 10 6 0 0                # the filter's CUTOFF, +50%, 4 BARS, DRIFT
new DRONES / Reverse Drone
set 0 70  1 15  2 85  3 60  5 60  8 70  9 80  10 45  13 50  14 80
new DRONES / Bloom Drone
set 0 25  1 8  2 80  3 55  5 75  9 80  13 55  14 78
fx 1 bloom 0.55 0.6 0.5 0.35           # BLOOM, DECAY, TONE, MIX

# ---- PLUCKS: short grains, the window's peak early, no attack -----------------------------------------
new PLUCKS / Grain Pluck
set 0 30  1 6  2 35  3 70  5 0  9 50  13 0  14 30
new PLUCKS / Glass Pluck
set 0 40  1 10  2 25  3 80  5 10  6 30  7 1  9 70  13 0  14 35
fx 1 ping 0.3125 0.35 0.55 0.25        # TIME 1/8, FEEDBACK, TONE, MIX
new PLUCKS / Kalimba Grains
set 0 20  1 4  2 40  3 65  5 10  9 40  13 0  14 40
fx 1 plate 0.15 0.45 0.55 0.25         # PRE, DECAY, TONE, MIX
new PLUCKS / Bell Grains
set 0 45  1 8  2 45  3 60  5 5  6 60  7 1  9 80  13 0  14 50
fx 1 shimmer 0.35 0.5 0.6 0.2          # SHIMMER, DECAY, TONE, MIX
new PLUCKS / Short Echo
set 0 35  1 12  2 30  3 75  5 15  9 60  13 0  14 30
fx 1 dub 0.5625 0.45 0.45 0.3          # TIME 1/8 DOT, FEEDBACK, TONE, MIX
new PLUCKS / Comb Pluck
set 0 25  1 6  2 35  3 70  5 5  9 50  13 0  14 35
fx 1 comb 0.5 0.8 0.55 0.45            # PITCH, FEEDBACK, TONE, MIX

# ---- RHYTHM: grains in time with the song, a STEPPER or an effect doing the playing --------------------
new RHYTHM / Pulse Grains
set 0 30  1 8  2 40  3 60  4 0  5 20  9 60  13 0  14 40
mod 1 2 3 1 1 0 0 0                    # a STEPPER on DENSITY, +100%, a step each
modsteps 1 8 0 0 1 -1 0.5 -1 1 -0.5 0 -1 0 0 0 0 0 0 0 0
new RHYTHM / Gate Steps
set 0 40  1 10  2 60  3 65  5 50  9 70  13 0  14 40
mod 1 2 32 0.75 1 4 0 0                # a STEPPER on LEVEL, +50%, a step each: a gate
modsteps 1 16 0 0 1 -1 1 -1 -1 1 0.5 -1 1 -1 0.3 1 -1 1 -1 -0.5
new RHYTHM / Position Steps
set 0 30  1 5  2 45  3 65  4 0  5 30  9 60  13 0  14 45
mod 1 2 0 0.9 3 0 0 0                  # a STEPPER on POSITION, +80%, an eighth each: a new place each
modsteps 1 8 0 0 -1 0.5 -0.5 1 0 -0.8 0.8 -0.2 0 0 0 0 0 0 0 0
new RHYTHM / Stutter Cloud
set 0 50  1 10  2 55  3 60  5 40  9 70  13 10  14 45
fx 1 stutter 0.5625 0.5 0.4 0.5        # LENGTH 1/8, REPEATS, CHANCE, MIX
new RHYTHM / Tremolo Cloud
set 0 35  1 12  2 75  3 65  5 60  9 60  13 30  14 55
fx 1 tremolo 0.45 0.8 0.375 0.4        # RATE 1/8, DEPTH, SQUARE, SPREAD
new RHYTHM / Chance Steps
set 0 25  1 15  2 50  3 60  4 30  5 20  9 80  13 0  14 40
mod 1 2 2 0.85 1 0 0 0                 # a STEPPER on SIZE, +70%, a step each, a third rolled afresh
modsteps 1 16 0 0.3 -1 0.6 -0.4 1 -0.8 0 0.7 -0.2 -1 0.9 -0.6 0.3 1 -0.5 0.2 -0.9
)presets"
