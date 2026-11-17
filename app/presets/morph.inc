R"presets(
# MORPH's built-in presets past its own table (BASIC, eight), and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/morph_rule.hpp
# (0 TABLE A, 1 POSITION A, 2 TABLE B (0 OFF, 1 + the table), 3 POSITION B, 4 MIX, 5 B SEMI, 6 B FINE,
# 7 SUB, 8 OCTAVE, 9 TUNE, 10 UNISON, 11 DETUNE, 12 WIDTH, 13 FILTER (0 OFF 1 LOW 12 2 LOW 24 3 BAND
# 4 HIGH), 14 CUTOFF, 15 RESONANCE, 16 KEY TRACK, 17-20 ATTACK DECAY SUSTAIN RELEASE, 21-24 the MOD
# ENV's, 25 ENV > POSITION, 26 ENV > CUTOFF, 27 VELOCITY, 28 LEVEL).
# Tables: 0 ANALOG 1 PULSE 2 SYNC 3 VOWEL 4 ORGAN 5 HARMONIC 6 FOLD 7 BEND 8 FM 9 RATIO 10 DRIVE
# 11 STEPS 12 GRIT 13 COMB 14 BELL 15 GLASS (TABLE B is one more). Macro lanes for a MOD: 0 POSITION
# (A's and B's), 1 MIX, 2 CUTOFF, 3 RESONANCE, 4 ENV > POSITION, 5 ENV > CUTOFF, 6 DETUNE, 7 RELEASE.
# Readings: CUTOFF 40 316 HZ, 50 632 HZ, 60 1262 HZ, 70 2518 HZ, 80 5024 HZ; an ATTACK of 50 89 MS,
# 60 219 MS, 70 540 MS, 80 1.3 S; a DECAY or RELEASE of 30 32 MS, 50 200 MS, 60 502 MS, 70 1.3 S,
# 80 3.2 S; DETUNE 20 +-2.0 CENTS, 30 +-4.5, 40 +-8.0.
# A new preset starts from Analog Sweep. Every one is played by `presets_test --heard-morph` (a bar of
# keys, or a bass line for BASS, round twice, Stop, the tail): loudness, peak, a tail that ends, every
# MOD moving and heard. Nobody has heard any of them.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.

# ---- PADS: A and B, UNISON 2, a slow AMP, a slow MOD ENV walking POSITION ------------------------------
new PADS / Glass Choir
set 0 15  1 10  2 4  3 60  4 45  5 12  6 4  10 2  11 30  12 80  13 1  14 70  17 70  18 70  19 80  20 75  21 75  22 80  23 60  24 75  25 50  28 0
fx 1 shimmer 0.5 0.65 0.6 0.35   # SHIMMER, DECAY, TONE, MIX: the octaves climb on
new PADS / Warm Strings
set 0 0  1 20  2 1  3 40  4 50  6 7  10 2  11 35  12 85  13 2  14 62  15 10  17 65  18 70  19 85  20 70  21 70  22 85  23 40  24 70  25 35  28 -4
fx 1 ensemble 0.4 0.6 0.7 0.6    # RATE, DEPTH, SPREAD, MIX: a section of it
send 2 0.3                       # into SPACE (a new song's HALL)
new PADS / Slow Vowels
set 0 3  1 0  2 4  3 100  4 40  6 -6  10 2  11 25  12 70  13 1  14 75  17 60  18 70  19 85  20 70  23 0  28 0
mod 1 1 0 0.8 10 0 0 0           # MOD 1: an LFO on POSITION, +60%, 4 BARS, SINE: the voice talks slowly
fx 1 hall 0.65 0.55 0.5 0.25     # SIZE, DECAY, TONE, MIX
new PADS / Organ Haze
set 0 4  1 30  2 6  3 50  4 35  5 12  10 2  11 20  12 60  13 1  14 68  17 55  18 60  19 90  20 65  21 80  22 75  23 50  24 70  25 40  28 0
new PADS / Harmonic Bloom
set 0 5  1 0  2 6  3 0  4 50  5 7  10 2  11 30  12 80  13 2  14 66  17 70  18 70  19 85  20 75  21 85  22 85  23 70  24 80  25 70  28 4
fx 1 bloom 0.6 0.6 0.5 0.35      # BLOOM, DECAY, TONE, MIX: it swells as it walks
new PADS / Drift Bells
set 0 14  1 20  2 16  3 30  4 50  5 12  6 3  10 2  11 25  12 90  13 1  14 80  17 50  18 75  19 70  20 75  23 0  28 -3
mod 1 1 0 0.7 12 6 0 0 0.15      # an LFO on POSITION, +40%, FREE at 0.15 HZ, DRIFT: wandering about
send 2 0.35

# ---- LEADS: UNISON 2 or 3, B a fifth or an octave up, LOW 24, the MOD ENV on CUTOFF -------------------
new LEADS / Fifth Lead
set 0 0  1 40  2 1  3 50  4 40  5 7  10 2  11 25  12 50  13 2  14 50  15 25  16 70  17 8  18 50  19 80  20 40  21 0  22 55  23 20  24 45  25 15  26 40  28 -3
new LEADS / Sync Scream
set 0 2  1 20  2 3  3 60  4 40  5 12  10 3  11 30  12 60  13 2  14 52  15 35  16 70  17 8  18 50  19 85  20 40  21 0  22 45  23 30  24 45  25 50  26 35  28 -2
send 1 0.25                      # into ECHO (a new song's TAPE)
new LEADS / Fold Lead
set 0 6  1 25  2 7  3 40  4 35  5 12  6 5  10 2  11 20  12 40  13 2  14 55  15 30  16 70  17 8  18 45  19 75  20 40  21 0  22 50  23 20  24 45  25 30  26 35  28 -4
new LEADS / Vowel Lead
set 0 3  1 30  2 4  3 70  4 30  5 7  10 2  11 20  12 40  13 2  14 58  15 30  16 70  17 10  18 55  19 80  20 40  21 10  22 60  23 30  24 50  25 40  26 30  28 -1
fx 1 chorus 0.3 0.4 0.125 0.5    # a light CHORUS
new LEADS / Bend Lead
set 0 7  1 10  2 1  3 50  4 30  5 12  10 3  11 25  12 55  13 2  14 48  15 30  16 70  17 8  18 50  19 80  20 45  21 0  22 60  23 30  24 50  25 45  26 40  28 -6
send 1 0.3
new LEADS / Grit Lead
set 0 12  1 20  2 11  3 40  4 30  5 7  10 2  11 20  12 50  13 2  14 52  15 20  16 70  17 8  18 45  19 80  20 40  21 0  22 50  23 25  24 45  25 30  26 35  28 -5

# ---- BASS: OCTAVE -1, SUB up, a short ENV > POSITION (a growl on each note), LOW 24 ------------------
new BASS / Growl Bass
set 0 6  1 10  2 0  7 50  8 -1  10 1  13 2  14 45  15 30  16 50  17 2  18 40  19 70  20 30  21 0  22 35  23 0  24 30  25 50  26 40  28 0
new BASS / Sub Pulse
set 0 1  1 20  2 0  7 70  8 -1  10 1  13 2  14 48  15 20  16 50  17 2  18 45  19 75  20 30  21 0  22 30  23 0  24 30  25 40  26 30  28 0
new BASS / Talking Bass
set 0 3  1 0  2 0  7 50  8 -1  10 1  13 2  14 50  15 35  16 50  17 2  18 45  19 70  20 30  21 0  22 45  23 0  24 30  25 70  26 30  28 2
new BASS / Drive Bass
set 0 10  1 20  2 1  3 30  4 30  6 -8  7 50  8 -1  10 1  13 2  14 45  15 25  16 50  17 2  18 45  19 75  20 30  21 0  22 40  23 0  24 30  25 40  26 40  28 -4
fx 1 drive 0.4 0.6 1 0.5         # DRIVE, TONE, MIX, OUTPUT
new BASS / Steps Bass
set 0 11  1 0  2 0  7 55  8 -1  10 1  13 2  14 46  15 25  16 50  17 2  18 40  19 70  20 30  21 0  22 30  23 0  24 30  25 60  26 35  28 -1
new BASS / Wide Fold Bass
set 0 6  1 30  2 7  3 50  4 40  6 6  7 45  8 -1  10 2  11 20  12 30  13 2  14 44  15 25  16 50  17 2  18 45  19 75  20 30  21 0  22 40  23 0  24 30  25 40  26 35  28 -4
fx 1 saturate 0.5 0 0.7 1        # DRIVE, TYPE (TAPE), TONE, MIX

# ---- MOVING: a MOD on POSITION in each, every one a whole sound ---------------------------------------
new MOVING / Table Walk
set 0 5  1 0  2 0  10 1  13 1  14 75  17 8  18 60  19 85  20 50  23 0  25 0  28 -5
mod 1 1 0 0.875 8 1 0 0          # MOD 1: an LFO on POSITION, +75%, 1 BAR, TRIANGLE: up the table and down
send 1 0.2
new MOVING / Free Swirl
set 0 13  1 30  2 9  3 50  4 40  5 12  10 2  11 20  12 70  13 1  14 72  17 30  18 60  19 85  20 60  23 0  25 0  28 -4
mod 1 1 0 0.8 12 0 0 0 0.4       # an LFO on POSITION, +60%, FREE at 0.4 HZ, SINE: in no time with the song
fx 1 chorus 0.3 0.4 0.125 0.5
new MOVING / Wave Sequence
set 0 11  1 0  2 0  10 1  13 2  14 70  15 20  17 4  18 55  19 80  20 40  23 0  25 0  28 -5
mod 1 2 0 1 1 0 0 0              # a STEPPER on POSITION, +100%, a step each: the table played as a sequence of waves
modsteps 1 16 0 0 -1 -0.5 0 0.5 1 -1 -0.25 0.25 0.75 -0.75 0 1 -0.5 0.5 -1
send 1 0.2
new MOVING / Chance Waves
set 0 15  1 30  2 0  10 2  11 15  12 60  13 1  14 80  17 4  18 50  19 75  20 50  23 0  25 0  28 -4
mod 1 2 0 0.9 3 0 0 0            # a STEPPER on POSITION, +80%, an eighth each, a step skipped half the time
modsteps 1 8 0 0.5 -1 0.6 -0.3 1 0.2 -0.8 0.9 -0.1 0 0 0 0 0 0 0 0
fx 1 ping 0.3125 0.35 0.4 0.2     # 1/8
new MOVING / Two Movers
set 0 8  1 20  2 15  3 40  4 40  5 12  10 2  11 20  12 60  13 2  14 55  15 35  17 8  18 55  19 80  20 50  23 0  25 0  28 -4
mod 1 1 0 0.75 9 0 0 0           # an LFO on POSITION, +50%, 2 BARS, SINE
mod 2 1 2 0.7 7 1 0 0            # MOD 2: an LFO on CUTOFF, +40%, a half, TRIANGLE
new MOVING / Ratio Tremble
set 0 9  1 40  2 0  10 1  13 1  14 78  17 8  18 60  19 85  20 50  23 0  25 0  28 -5
mod 1 1 0 0.7 5 5 0 0            # an LFO on POSITION, +40%, a quarter, RANDOM: a new wave each beat
send 2 0.25
)presets"
