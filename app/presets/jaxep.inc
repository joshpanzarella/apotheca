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
# ---- The second set (2026-11-16): a starting patch for everything, with movement in it -----------
# Every one below plays its folder's phrase in `presets_test --heard-jaxep` inside its limits
# (loudness, no clip at the master, the synth's own VCA never clipped, a tail that ends, every MOD
# moving). Its figures and the idea of each: jx3p-plan.md, "The second set of presets".
# A `settings` line is all 32 of the patch, in the booklet's order (jx3p.hpp), grouped as:
#   DCO-1: RANGE (0 16' 1 8' 2 4') WAVE (0 SAW 1 PULSE 2 SQUARE) LFO ENV
#   DCO-2: RANGE WAVE (3 NOISE) CROSS MOD (0 OFF 1 SYNC 2 METAL) TUNE (50 unison, 79 a fifth up,
#          100 an octave; semitone steps) FINE (50 centre, +-50 cents) LFO ENV
#   DCO: LFO DEPTH (12 semitones x dial squared) ENV DEPTH (24 semitones) ENV POLARITY
#   VCF: SOURCE MIX (0 DCO-1, 100 DCO-2) HPF CUTOFF (50 is 1.6 kHz at key 60, 10 a doubling)
#        RESONANCE (sings from 96) LFO MOD ENV MOD (10 octaves at 100) ENV POLARITY KEY FOLLOW (98 full)
#   VCA: MODE (0 ENV 1 GATE) LEVEL (straight: the loudness each was measured to)
#   LFO: WAVE (0 SINE 1 SQUARE 2 RANDOM) DELAY RATE (50 is 1.4 Hz, 70 4.1 Hz)
#   ENV: A D S R (A 50 is 0.2 s, 80 1.2 s; D and R 50 0.32 s, 70 1.3 s, 90 5 s)   CHORUS
# Macros: 0 CUTOFF 1 RESONANCE 2 ENV 3 TUNE (DCO-2, in semitones) 4 MIX 5 LFO 6 ATTACK 7 RELEASE.
# MOD targets: those 0 to 7; slot 1's settings 8-11, slot 2's 12-15, slot 3's 16-19; 20 ECHO, 21
# SPACE; 32 LEVEL, 33 PAN. Rates: 1 STEP 1, 1/8 3, 1/4 5, 1/2 7, BAR 8, 2 BARS 9, 4 BARS 10,
# 8 BARS 11, FREE 12 (Hz last). Shapes: 0 SINE 1 TRIANGLE 2 RAMP UP 3 RAMP DOWN 4 SQUARE 5 RANDOM 6 DRIFT.

# PADS: chords held, slow and moving.

new PADS / Glass Pad
settings 1 2 0 0  2 0 0 50 54 1 0  10 0 0  45 40 55 30 8 20 0 50  0 37  0 40 30  72 70 75 78  1
fx 1 shimmer 0.35 0.7 0.6 0.35          # an octave rises out of it
send 1 0.1
mod 1 1 0 0.6 12 0 0 0 0.08             # CUTOFF breathes, +20%, free at 0.08 Hz
mod 2 1 33 0.7 12 6 0 0 0.12            # PAN drifts

new PADS / Velvet Fifths
settings 0 0 0 0  0 0 0 79 52 0 0  0 0 0  40 30 38 20 0 25 0 40  0 64  0 0 40  75 75 80 75  0
fx 1 ensemble 0.3 0.6 0.7 0.6
fx 2 hall 0.7 0.65 0.4 0.3
mod 1 1 4 0.65 10 0 0 0                 # MIX: the fifth comes and goes, +30%, 4 BARS
mod 2 1 0 0.58 11 1 0 0                 # CUTOFF, 8 BARS

new PADS / Breathing Pad
settings 1 2 0 0  1 0 0 50 56 0 0  0 0 0  50 20 45 35 22 15 0 50  0 65  0 0 25  80 60 85 80  1
fx 1 plate 0.25 0.7 0.45 0.35
mod 1 1 32 0.35 12 0 0 0 0.07           # LEVEL breathes, -30%, free at 0.07 Hz
mod 2 1 0 0.6 12 6 0 0 0.05             # CUTOFF drifts

new PADS / Polar Night
settings 0 0 0 0  1 3 0 50 50 0 0  0 0 0  25 0 30 55 10 30 0 60  0 55  2 0 35  85 90 70 92  1
fx 1 bloom 0.6 0.75 0.4 0.4             # a quarter of it is NOISE: wind under the chord
mod 1 1 1 0.6 10 1 0 0                  # RESONANCE, +20%, 4 BARS
mod 2 1 33 0.75 12 6 0 0 0.1

new PADS / Choir Of Saws
settings 1 0 1 0  1 0 0 50 62 1 0  9 0 0  50 30 58 10 0 10 0 40  0 56  0 50 55  60 70 85 70  1
fx 1 ensemble 0.25 0.7 0.9 0.5
fx 2 hall 0.75 0.6 0.45 0.3
mod 1 1 0 0.56 9 0 0 0
mod 2 1 15 0.7 10 1 0 0                 # HALL's MIX, +40%, 4 BARS

new PADS / Tidal Pad
settings 1 0 0 0  1 2 1 63 50 1 0  30 0 0  70 20 52 15 0 15 0 50  0 23  0 0 35  70 70 80 75  1
fx 1 chorus 0.25 0.55 0.125 0.5         # SYNC's square, its pitch swept by the LFO: the width moves
fx 2 hall 0.65 0.6 0.45 0.3
mod 1 1 0 0.6 12 0 0 0 0.06
mod 2 1 33 0.65 9 0 0 0

new PADS / Halo Pad
settings 2 2 0 0  1 2 0 50 47 0 0  0 0 0  50 60 62 40 0 15 0 70  0 55  0 0 0  65 80 70 85  1
fx 1 ping 0.8125 0.4 0.5 0.2            # 1/4 DOT
fx 2 plate 0.3 0.75 0.5 0.35
mod 1 1 1 0.65 11 0 0 0                 # RESONANCE, +30%, 8 BARS
mod 2 1 10 0.65 12 6 0 0 0.1            # PING's TONE drifts

new PADS / Dark Matter
settings 0 0 0 0  0 2 0 50 45 0 0  0 0 0  50 0 30 60 0 20 0 50  0 37  0 0 30  80 85 85 85  1
fx 1 saturate 0.3 0.125 0.4 0.5         # TAPE
fx 2 hall 0.6 0.7 0.35 0.3
mod 1 1 0 0.65 11 0 0 0                 # CUTOFF opens and closes, +30%, 8 BARS

new PADS / Sunrise Swell
settings 1 0 0 0  2 0 0 50 53 0 0  0 0 0  40 30 25 25 0 45 0 50  0 64  0 0 30  90 95 70 85  1
fx 1 hall 0.8 0.7 0.5 0.35              # a 2 s attack opening the filter as it rises
send 2 0.15
mod 1 1 21 0.7 10 0 0 0                 # SPACE send, +40%
mod 2 1 4 0.6 11 1 0 0                  # MIX

# STRINGS: sections, bowed, plucked and trembling.

new STRINGS / Ensemble Strings
settings 1 0 1 0  1 0 0 50 58 1 0  7 0 0  50 45 62 5 0 8 0 60  0 48  0 45 60  55 60 90 65  1
fx 1 ensemble 0.3 0.65 0.8 0.6
fx 2 hall 0.7 0.55 0.45 0.25
mod 1 1 0 0.56 10 0 0 0

new STRINGS / Slow Violas
settings 0 0 1 0  1 0 0 50 53 1 0  11 0 0  45 30 45 15 0 12 0 55  0 60  0 60 58  70 70 85 72  1
fx 1 plate 0.2 0.7 0.4 0.3
mod 1 1 5 0.54 9 0 0 0                  # LFO: the vibrato deepens and eases, 2 BARS
mod 2 1 33 0.65 11 0 0 0

new STRINGS / Synth Pizz
settings 1 0 0 0  1 0 0 50 55 0 0  0 0 0  50 30 35 20 0 45 0 70  0 95  0 0 50  0 45 0 42  1
fx 1 ping 0.3125 0.35 0.5 0.25          # 1/8
fx 2 plate 0.15 0.5 0.5 0.2
mod 1 2 0 0.62 3 0 0 0                  # a STEPPER on CUTOFF, 1/8
modsteps 1 6 0.1 0 0.6 -0.3 0.2 1 -0.6 0 0 0 0 0 0 0 0 0 0 0

new STRINGS / Tremolo Strings
settings 1 0 0 0  1 0 0 50 57 0 0  0 0 0  50 40 58 10 0 10 0 60  0 81  0 0 0  45 60 90 60  1
fx 1 tremolo 0.25 0.6 0.125 0.3         # 1/16, SINE
fx 2 hall 0.65 0.55 0.45 0.25
mod 1 1 9 0.65 10 0 0 0                 # TREMOLO's DEPTH, +30%, 4 BARS

new STRINGS / Cinema Strings
settings 0 0 1 0  1 0 0 50 56 1 0  8 0 0  55 30 55 10 0 12 0 55  0 45  0 70 55  65 70 90 80  1
fx 1 eq 0.55 0.5 0.5 0.62
fx 2 hall 0.85 0.7 0.45 0.3
send 2 0.1
mod 1 1 0 0.58 11 0 0 0

new STRINGS / Bowed Sync
settings 1 0 0 0  1 0 1 58 50 0 1  0 25 0  60 30 60 10 0 10 0 60  0 45  0 0 0  50 70 85 65  1
fx 1 chorus 0.2 0.5 0.125 0.45          # the ENV lifts DCO-2 under SYNC: a bow's bite
fx 2 plate 0.25 0.65 0.45 0.3
mod 1 1 4 0.6 9 1 0 0                   # MIX, 2 BARS

new STRINGS / Octave Strings
settings 1 0 0 0  2 0 0 50 52 0 0  0 0 0  45 45 60 5 0 8 0 60  0 61  0 0 0  55 60 90 65  1
fx 1 phaser 0.15 0.5 0.6 0.45
fx 2 hall 0.7 0.6 0.45 0.25
mod 1 1 9 0.7 12 6 0 0 0.1              # PHASER's DEPTH drifts

new STRINGS / Distant Section
settings 1 0 1 0  1 0 0 50 59 1 0  9 0 0  50 50 40 15 0 10 0 50  0 43  0 50 55  75 75 85 85  1
fx 1 hall 0.9 0.8 0.35 0.45
mod 1 1 33 0.75 10 0 0 0
mod 2 1 0 0.58 11 0 0 0

# BRASS: an envelope that opens the filter as it speaks.

new BRASS / Bold Brass
settings 1 0 0 0  1 0 0 50 55 0 0  0 0 0  50 20 30 10 0 50 0 60  0 71  0 50 60  20 55 65 30  1
fx 1 saturate 0.25 0.375 0.5 0.4        # TUBE
fx 2 plate 0.2 0.55 0.5 0.25
mod 1 1 2 0.65 8 5 0 0                  # ENV: each bar speaks its own way (RANDOM), +30%

new BRASS / Soft Horns
settings 0 2 0 0  1 0 0 50 53 0 0  0 0 0  50 25 35 5 0 30 0 55  0 36  0 0 0  40 60 75 45  1
fx 1 chorus 0.2 0.4 0.125 0.35
fx 2 hall 0.6 0.55 0.4 0.3
mod 1 1 0 0.56 9 0 0 0

new BRASS / Fanfare Fifths
settings 1 0 0 1  1 0 0 79 55 0 1  0 4 0  40 25 32 10 0 55 0 60  0 95  0 0 0  15 50 70 30  1
fx 1 comp 0.45 0.4167 0.5 1             # 3:1
fx 2 hall 0.65 0.55 0.45 0.25
mod 1 1 33 0.65 9 4 0 0                 # PAN: left bar, right bar

new BRASS / Brass Stab
settings 1 0 0 0  1 0 0 50 57 0 0  0 0 0  50 20 25 25 0 60 0 60  0 95  0 0 0  5 45 30 30  1
fx 1 tape 0.5625 0.3 0.3 0.2            # 1/8 DOT
mod 1 2 0 0.65 5 0 0 0                  # a STEPPER on CUTOFF, 1/4
modsteps 1 4 0 0 0.3 -0.4 0.8 0 0 0 0 0 0 0 0 0 0 0 0 0

new BRASS / Sync Horn
settings 1 0 0 0  1 0 1 50 50 0 1  0 30 0  75 20 40 10 0 40 0 60  0 53  0 0 0  20 55 65 30  1
fx 1 saturate 0.35 0.125 0.5 0.4
fx 2 plate 0.2 0.6 0.5 0.25
mod 1 1 4 0.65 9 1 0 0                  # MIX: from plain to synced, 2 BARS

new BRASS / Muted Trumpets
settings 1 1 1 0  1 0 0 50 52 1 0  9 0 0  30 60 50 15 0 45 0 60  0 80  0 55 60  10 50 55 30  0
fx 1 vowel 0.7 0.5 0.4 0.35             # O: a mute in the bell
fx 2 plate 0.2 0.5 0.5 0.25
mod 1 1 8 0.7 8 1 0 0                   # the VOWEL, a wah each bar

new BRASS / Low Brass
settings 0 0 0 0  0 2 0 50 53 0 0  0 0 0  40 20 22 15 0 50 0 60  0 38  0 0 0  25 60 70 35  1
fx 1 eq 0.6 0.55 0.4 0.45
fx 2 hall 0.6 0.6 0.4 0.25
mod 1 1 0 0.6 10 0 0 0

new BRASS / Swell Brass
settings 1 0 0 0  1 0 0 50 57 0 0  0 0 0  50 25 25 10 0 55 0 60  0 50  0 60 55  65 80 75 55  1
fx 1 chorus 0.25 0.5 0.125 0.4
fx 2 hall 0.7 0.6 0.45 0.3
mod 1 1 21 0.7 10 0 0 0                 # the SPACE send
send 2 0.1

# BASS: heard an octave and more down. The resonance keeps its low end now (kBassKept).

new BASS / Round Sub
settings 0 2 0 0  1 0 0 50 50 0 0  0 0 0  0 0 30 0 0 20 0 50  0 24  0 0 0  0 60 70 20  0
fx 1 comp 0.5 0.4167 0.5 1
mod 1 1 0 0.56 8 0 0 0                  # CUTOFF, a little, a bar

new BASS / Acid Line
settings 0 0 0 0  1 0 0 50 50 0 0  0 0 0  0 0 26 85 0 55 0 40  0 95  0 0 0  0 45 0 25  0
fx 1 saturate 0.45 0.875 0.5 0.45        # HARD
fx 2 ping 0.5625 0.3 0.4 0.15           # 1/8 DOT
mod 1 2 2 0.75 1 0 0 0                  # a STEPPER on ENV, every step: the accents
modsteps 1 16 0 0 1 -0.5 0 0.6 -0.8 0 1 -0.3 0.2 -0.6 0.8 0 -1 0.5 0 0.3
mod 2 1 0 0.62 9 0 0 0                  # CUTOFF, +24%, 2 BARS

new BASS / Rubber Bass
settings 0 2 0 1  1 0 0 50 52 0 1  0 8 0  30 0 25 40 0 40 0 50  0 72  0 0 0  0 40 20 25  0
fx 1 comp 0.45 0.5833 0.4 1             # 4:1
fx 2 chorus 0.3 0.35 0.125 0.3
mod 1 2 0 0.6 3 0 0 0                   # a STEPPER on CUTOFF, 1/8
modsteps 1 8 0.2 0 0.4 -0.4 0 0.8 -0.2 0.3 -0.6 0 0 0 0 0 0 0 0 0

new BASS / Fifth Bass
settings 0 0 0 0  0 0 0 79 50 0 0  0 0 0  35 0 32 20 0 35 0 50  0 95  0 0 0  0 55 50 25  0
fx 1 drive 0.3 0.5 0.4 0.5
mod 1 1 4 0.6 8 1 0 0                   # MIX: the fifth up and down, a bar

new BASS / Sync Growl
settings 0 0 0 0  0 0 1 62 50 0 1  0 45 0  80 0 35 20 0 30 0 50  0 95  0 0 0  0 50 40 25  0
fx 1 saturate 0.4 0.375 0.45 0.45       # TUBE
mod 1 2 3 0.7 3 0 0 0                   # a STEPPER on TUNE under SYNC: each step its own growl
modsteps 1 8 0 0 0 0.3 -0.2 0.6 0 -0.4 0.3 0.8 0 0 0 0 0 0 0 0

new BASS / Deep Pluck
settings 0 0 0 0  1 2 0 50 50 0 0  0 0 0  30 0 30 30 0 45 0 50  0 95  0 0 0  0 50 0 30  0
fx 1 eq 0.62 0.5 0.4 0.45               # LOW +2.9 DB
mod 1 1 0 0.6 9 0 0 0

new BASS / Metal Bass
settings 0 0 0 0  0 0 2 55 50 0 0  0 0 0  55 0 40 15 0 30 0 50  0 85  0 0 0  0 55 30 25  0
fx 1 comp 0.5 0.4167 0.5 1
mod 1 1 4 0.65 9 1 0 0                  # MIX: plain saw to METAL and back, 2 BARS

new BASS / Wobble
settings 0 0 0 0  1 0 0 50 51 0 0  0 0 0  30 0 30 50 0 15 0 50  0 95  0 0 0  0 60 80 25  0
fx 1 saturate 0.35 0.375 0.5 0.4
mod 1 1 0 0.7 3 0 0 0                   # CUTOFF swung each 1/8, +40%
mod 2 1 1 0.6 10 1 0 0                  # RESONANCE, 4 BARS

new BASS / Dub Sub
settings 0 2 0 0  0 2 0 50 50 0 0  0 0 0  0 0 18 20 0 10 0 50  0 70  0 0 0  0 60 80 30  0
fx 1 dub 0.8125 0.3 0.35 0.3            # 1/4 DOT, into a dark echo
mod 1 1 10 0.7 9 0 0 0                  # DUB's TONE, 2 BARS

new BASS / Chorus Bass
settings 1 0 0 0  1 2 0 50 54 0 0  0 0 0  40 0 35 10 0 25 0 50  0 60  0 0 0  5 60 50 30  1
fx 1 chorus 0.3 0.4 0.125 0.35
mod 1 1 0 0.58 9 0 0 0

new BASS / Funk Pluck
settings 0 2 0 0  1 0 0 50 50 0 0  0 0 0  0 0 25 70 0 50 0 60  0 72  0 0 0  0 35 10 25  0
fx 1 comp 0.5 0.5833 0.3 1
mod 1 2 0 0.62 3 0 0 0                  # a STEPPER on CUTOFF, 1/8
modsteps 1 4 0 0 0 0.6 -0.3 0.4 0 0 0 0 0 0 0 0 0 0 0 0

new BASS / Saw Stack
settings 0 0 0 0  1 0 0 50 57 0 0  0 0 0  45 0 40 15 0 30 0 50  0 95  0 0 0  0 60 60 30  0
fx 1 saturate 0.3 0.125 0.5 0.4
fx 2 chorus 0.25 0.3 0.125 0.25
mod 1 1 0 0.62 12 6 0 0 0.2             # CUTOFF drifts, free

# LEAD: one line, heard high.

new LEAD / Singing Lead
settings 1 0 1 0  1 0 0 50 58 1 0  13 0 0  45 20 55 35 0 25 0 50  0 95  0 50 62  5 60 80 40  0
fx 1 tape 0.5625 0.35 0.3 0.25          # 1/8 DOT
send 2 0.15
mod 1 1 9 0.65 10 0 0 0                 # TAPE's FEEDBACK, +30%, 4 BARS

new LEAD / Sync Scream
settings 1 0 0 0  1 0 1 60 50 1 1  9 40 0  90 20 60 20 0 20 0 50  0 95  0 40 62  0 60 70 35  0
fx 1 fuzz 0.35 0.5 0.2 0.35
fx 2 ping 0.5625 0.35 0.5 0.2
mod 1 1 3 0.65 9 0 0 0                  # TUNE under SYNC: the scream climbs and falls, 2 BARS

new LEAD / Square Lead
settings 1 2 1 0  2 2 0 50 53 1 0  9 0 0  25 20 50 20 0 20 0 50  0 49  0 50 62  5 50 80 35  1
fx 1 delay 0.4167 0.35 0.5 0.25         # 1/8 DOT
mod 1 1 33 0.6 8 0 0 0                  # PAN, a bar

new LEAD / Fat Mono
settings 0 0 0 0  1 0 0 50 57 0 0  0 0 0  50 0 52 40 0 35 0 50  0 95  0 0 0  0 55 60 30  0
fx 1 saturate 0.3 0.375 0.5 0.45
fx 2 dub 0.5625 0.3 0.5 0.2             # 1/8 DOT
mod 1 1 0 0.6 8 0 0 0

new LEAD / Pulse Lead
settings 1 1 1 0  1 0 0 50 55 1 0  12 0 0  50 30 65 15 0 15 0 50  0 95  0 50 64  0 60 80 35  0
fx 1 phaser 0.25 0.6 0.65 0.4
fx 2 tape 0.5625 0.3 0.3 0.2
mod 1 1 11 0.65 10 1 0 0                # PHASER's MIX, 4 BARS

new LEAD / Brass Lead
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 20 30 15 0 55 0 50  0 94  0 0 0  15 55 65 35  1
fx 1 saturate 0.3 0.375 0.5 0.4
fx 2 plate 0.2 0.55 0.5 0.25
mod 1 1 2 0.65 5 5 0 0                  # ENV, a new swell each 1/4 (RANDOM)

new LEAD / Metal Lead
settings 1 0 0 0  1 0 2 67 50 0 0  0 0 0  50 30 60 15 0 15 0 50  0 83  0 0 0  0 60 70 40  1
fx 1 chorus 0.3 0.45 0.125 0.35
fx 2 ping 0.6875 0.35 0.5 0.2           # 1/4
mod 1 1 4 0.65 8 0 0 0                  # MIX: the METAL comes through, a bar

new LEAD / Breath Flute
settings 2 2 1 0  1 3 0 50 50 0 0  14 0 0  12 40 42 10 0 15 0 70  0 42  0 60 60  30 60 85 35  0
fx 1 plate 0.25 0.6 0.5 0.3             # a breath of NOISE under a 4' square
mod 1 1 0 0.56 12 0 0 0 0.3

new LEAD / Fuzz Lead
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 20 50 20 0 20 0 50  0 95  0 0 0  0 60 80 35  0
fx 1 fuzz 0.55 0.45 0.25 0.6
fx 2 eq 0.45 0.55 0.5 0.45
mod 1 1 9 0.7 9 1 0 0                   # FUZZ's TONE, 2 BARS

new LEAD / Formant Lead
settings 1 0 1 0  1 0 0 50 57 1 0  10 0 0  50 20 65 10 0 10 0 50  0 47  0 50 60  5 60 85 35  0
fx 1 vowel 0.3 0.5 0.6 0.8              # through the vowels
fx 2 plate 0.2 0.55 0.5 0.2
mod 1 1 8 0.8 9 1 0 0                   # the VOWEL, +60%, 2 BARS

# KEYS: struck and released.

new KEYS / Suitcase Keys
settings 1 0 0 0  1 2 1 30 40 0 0  0 0 0  90 40 40 15 0 25 0 40  0 37  0 0 0  0 70 30 50  1
fx 1 chorus 0.25 0.3 0.125 0.3
fx 2 plate 0.25 0.6 0.5 0.25
mod 1 1 33 0.7 7 0 0 0                  # PAN, the old amp's sway, each 1/2

new KEYS / Rotor Organ
settings 0 2 0 0  2 2 0 50 50 0 0  0 0 0  40 30 70 0 0 0 0 50  1 35  0 0 0  0 0 100 8  0
fx 1 chorus 0.4 0.6 0.625 0.6           # VIBRATO
mod 1 1 8 0.7 10 0 0 0                  # its RATE speeds and slows, 4 BARS

new KEYS / Clav Wah
settings 0 1 0 0  1 2 0 50 50 0 0  0 0 0  30 40 35 60 0 40 0 60  0 95  0 0 0  0 40 30 20  0
fx 1 saturate 0.35 0.375 0.55 0.5       # TUBE
fx 2 chorus 0.3 0.35 0.125 0.3
mod 1 1 0 0.7 5 0 0 0                   # CUTOFF, a wah each 1/4, +40%

new KEYS / Bell Keys
settings 1 0 0 0  1 0 2 71 50 0 0  0 0 0  60 30 65 10 0 20 0 60  0 50  0 0 0  0 80 0 70  1
fx 1 hall 0.6 0.6 0.5 0.3
mod 1 1 4 0.65 8 5 0 0                  # MIX, RANDOM each bar

new KEYS / Hard Piano
settings 1 0 0 0  1 2 0 50 54 0 0  0 0 0  45 30 50 15 0 40 0 80  0 55  0 0 0  0 75 10 50  0
fx 1 comp 0.5 0.4167 0.4 1
fx 2 eq 0.55 0.45 0.6 0.58
mod 1 1 2 0.6 5 5 0 0                   # ENV: no two quarters struck alike

new KEYS / Toy Keys
settings 2 2 0 0  2 2 0 100 50 0 0  0 0 0  30 50 70 10 0 10 0 60  0 64  0 0 0  0 50 0 40  0
fx 1 crush 0.6 0.5 0.4
fx 2 ping 0.3125 0.3 0.5 0.2
mod 1 2 8 0.65 3 0 0 0                  # a STEPPER on its BITS, 1/8
modsteps 1 4 0 0 0 0.6 -0.4 0.9 0 0 0 0 0 0 0 0 0 0 0 0

new KEYS / Dream Tines
base E. Piano II
set 23 72                               # LEVEL
fx 1 tremolo 0.75 0.4 0.125 0.8        # 1/4, a stereo tremolo
fx 2 plate 0.25 0.65 0.5 0.3
mod 1 1 9 0.7 10 0 0 0                  # TREMOLO's DEPTH, 4 BARS

new KEYS / Harpsi Tines
settings 1 1 0 0  2 2 0 50 55 0 0  0 0 0  50 50 70 20 0 20 0 60  0 93  0 0 0  0 50 0 45  0
fx 1 spring 0.5 0.3 0.5 0.25
fx 2 chorus 0.25 0.35 0.125 0.3
mod 1 1 11 0.7 9 0 0 0                  # CHORUS's MIX, 2 BARS

new KEYS / Reed Organ
settings 0 0 0 0  1 2 0 50 54 0 0  0 0 0  50 20 45 10 0 0 0 50  1 35  0 0 0  10 0 100 15  0
fx 1 chorus 0.3 0.5 0.375 0.5           # ENSEMBLE
mod 1 1 32 0.425 3 0 0 0                # LEVEL, -15%: the bellows, each 1/8

# PLUCK: short notes, made by what comes after them.

new PLUCK / Harp Pluck
settings 1 0 0 0  1 0 0 50 54 0 0  0 0 0  40 30 35 20 0 45 0 70  0 95  0 0 0  0 60 0 55  1
fx 1 plate 0.2 0.6 0.5 0.3
fx 2 ping 0.5625 0.3 0.5 0.15
mod 1 1 2 0.62 9 1 0 0                  # ENV, 2 BARS

new PLUCK / Resonant Pluck
settings 1 0 0 0  1 0 0 50 50 0 0  0 0 0  0 30 25 80 0 55 0 60  0 95  0 0 0  0 40 0 40  0
fx 1 ping 0.5625 0.4 0.45 0.25          # 1/8 DOT
mod 1 1 1 0.6 9 0 0 0                   # RESONANCE, +20%, 2 BARS

new PLUCK / Silk Pluck
settings 1 2 0 1  1 1 0 50 53 0 1  0 4 0  50 30 45 15 0 35 0 60  0 63  0 0 0  0 55 0 45  1
fx 1 spring 0.4 0.3 0.5 0.25
fx 2 chorus 0.25 0.35 0.125 0.3
mod 1 1 33 0.75 3 5 0 0                 # PAN, RANDOM each 1/8

new PLUCK / Dub Pluck
settings 1 2 0 0  1 0 0 50 50 0 0  0 0 0  20 40 40 30 0 35 0 60  0 86  0 0 0  0 40 0 30  0
fx 1 dub 0.5625 0.4 0.4 0.35            # 1/8 DOT, FEEDBACK 60%
mod 1 1 9 0.65 10 0 0 0                 # DUB's FEEDBACK, +30%, 4 BARS

new PLUCK / Glass Pluck
settings 2 2 0 0  2 0 2 79 50 0 0  0 0 0  40 50 70 10 0 15 0 70  0 60  0 0 0  0 55 0 50  1
fx 1 shimmer 0.5 0.6 0.6 0.3
mod 1 1 8 0.7 10 0 0 0                  # SHIMMER's amount, 4 BARS

new PLUCK / Muted Guitar
settings 1 1 0 0  1 0 0 50 55 0 0  0 0 0  30 30 50 20 0 35 0 60  0 80  0 0 0  0 45 0 25  0
fx 1 comp 0.5 0.5833 0.4 1
fx 2 chorus 0.25 0.35 0.125 0.35
fx 3 plate 0.15 0.5 0.5 0.2
mod 1 2 0 0.65 3 0 0 0                  # a STEPPER on CUTOFF, 1/8
modsteps 1 8 0.1 0 0 0.5 -0.3 0.2 0.8 -0.5 0.3 0 0 0 0 0 0 0 0 0

new PLUCK / Pizz Bloom
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 30 58 15 0 40 0 60  0 95  0 0 0  0 50 0 45  1
fx 1 bloom 0.5 0.65 0.5 0.3
mod 1 1 11 0.7 10 0 0 0                 # BLOOM's MIX, 4 BARS

new PLUCK / Kalimba Sync
settings 1 2 0 0  2 0 1 50 50 0 1  0 15 0  70 40 55 10 0 20 0 60  0 95  0 0 0  0 45 0 45  0
fx 1 plate 0.15 0.55 0.5 0.25
mod 1 2 3 0.7 5 0 0 0                   # a STEPPER on TUNE under SYNC: a new tine each 1/4
modsteps 1 5 0 0 0 0.4 -0.3 0.7 0.2 0 0 0 0 0 0 0 0 0 0 0

new PLUCK / Reverse Pluck
settings 1 0 0 0  1 2 0 50 54 0 0  0 0 0  40 30 40 15 0 40 0 60  0 72  0 0 0  0 55 0 50  1
fx 1 reverse 0.6875 0.35 0.45           # each note again backwards, 1/4
fx 2 hall 0.6 0.55 0.45 0.25
mod 1 1 10 0.3 10 0 0 0                 # REVERSE's MIX, -40%, 4 BARS

# SEQUENCE: sixteenths and arpeggios; the MODs make each step its own.

new SEQUENCE / Step Acid
settings 0 0 0 0  1 0 0 50 50 0 0  0 0 0  0 0 30 80 0 60 0 50  0 95  0 0 0  0 45 0 20  0
fx 1 saturate 0.5 0.875 0.5 0.45
fx 2 ping 0.5625 0.35 0.45 0.2
mod 1 2 2 0.8 1 0 0 0                   # a STEPPER on ENV, every step: accents
modsteps 1 16 0 0 1 -0.4 0 0.7 -0.8 0.2 1 -0.5 0 0.4 -1 0.9 0 -0.3 0.6 -0.6
mod 2 1 0 0.65 10 0 0 0                 # CUTOFF, +30%, 4 BARS

new SEQUENCE / Sync Steps
settings 1 0 0 0  1 0 1 55 50 0 0  0 0 0  85 20 60 10 0 20 0 50  0 95  0 0 0  0 45 0 30  0
fx 1 ping 0.5625 0.3 0.5 0.2
mod 1 2 3 0.75 1 0 0 0                  # a STEPPER on TUNE under SYNC, every step
modsteps 1 8 0 0 0 0.6 0.2 -0.4 0.8 0 0.4 -0.2 0 0 0 0 0 0 0 0

new SEQUENCE / Bouncing Fifths
settings 1 2 0 0  1 0 0 79 50 0 0  0 0 0  40 30 45 15 0 35 0 60  0 95  0 0 0  0 40 0 35  1
fx 1 ping 0.4375 0.4 0.5 0.25           # 1/4 TRIP
mod 1 2 33 0.8 1 0 0 0                  # a STEPPER on PAN, every step
modsteps 1 4 0 0 -0.8 0.8 -0.3 0.3 0 0 0 0 0 0 0 0 0 0 0 0

new SEQUENCE / Chord Chop
settings 1 0 0 0  1 0 0 50 57 0 0  0 0 0  50 30 50 15 0 20 0 50  0 95  0 0 0  0 60 80 50  1
fx 1 plate 0.2 0.6 0.5 0.3
mod 1 2 32 1 0 0 0 0                    # a STEPPER on LEVEL, each half step: -1 is silence
modsteps 1 8 0 0 0 -1 0 0 -1 0 -1 0 0 0 0 0 0 0 0 0

new SEQUENCE / Berlin Bass
settings 0 0 0 0  1 0 0 50 52 0 0  0 0 0  30 0 38 60 0 40 0 50  0 95  0 0 0  0 45 40 25  0
fx 1 saturate 0.4 0.125 0.5 0.5         # TAPE
fx 2 tape 0.1875 0.4 0.4 0.25           # 1/8 TRIP
mod 1 1 0 0.7 11 0 0 0                  # CUTOFF, +40%, 8 BARS: the long sweep
mod 2 1 14 0.65 10 0 0 0                # TAPE's AGE

new SEQUENCE / Arp Glass
settings 2 2 0 0  2 0 0 50 46 0 0  0 0 0  40 40 60 20 0 25 0 60  0 95  0 0 0  0 40 0 40  1
fx 1 ping 0.4375 0.4 0.55 0.25          # 1/4 TRIP
fx 2 plate 0.2 0.6 0.55 0.25
mod 1 1 2 0.7 1 5 0 0                   # ENV, RANDOM every step

new SEQUENCE / Blip Seq
settings 1 2 0 1  1 2 0 50 50 0 1  0 20 0  30 30 50 20 0 30 0 60  0 95  0 0 0  0 25 0 20  0
fx 1 crush 0.65 0.55 0.35
fx 2 ping 0.3125 0.35 0.5 0.2           # 1/8
mod 1 2 9 0.7 3 0 0 0                   # a STEPPER on CRUSH's RATE, 1/8
modsteps 1 6 0 0 0 0.7 -0.5 0.3 1 -0.2 0 0 0 0 0 0 0 0 0 0

new SEQUENCE / Stutter Seq
settings 1 0 0 0  1 2 0 50 55 0 0  0 0 0  45 30 50 25 0 30 0 60  0 95  0 0 0  0 45 0 30  0
fx 1 stutter 0.5625 0.3 0.4 1           # 1/8, now and then
fx 2 plate 0.15 0.5 0.5 0.2
mod 1 1 10 0.75 10 0 0 0                # STUTTER's CHANCE, +50%, 4 BARS

new SEQUENCE / Ratchet Filter
settings 0 0 0 0  1 0 0 50 53 0 0  0 0 0  40 0 30 60 0 30 0 50  0 95  0 0 0  0 50 40 25  0
fx 1 saturate 0.3 0.375 0.5 0.4
mod 1 2 0 0.7 0 0 0 0                   # a STEPPER on CUTOFF, each half step
modsteps 1 16 0.1 0 0.8 -0.4 0.2 -0.6 0.6 0 -0.8 0.4 1 -0.2 0 -0.6 0.5 -0.4 0.2 -1

# SYNC & METAL: DCO-2 reset by DCO-1 (SYNC), or both edges together (METAL).

new SYNC & METAL / Sync Sweep Down
settings 1 0 0 0  1 0 1 50 50 0 1  0 60 0  90 20 60 10 0 15 0 50  0 49  0 0 0  0 60 60 35  1
fx 1 tape 0.5625 0.3 0.3 0.2
mod 1 1 4 0.6 9 0 0 0                   # MIX, 2 BARS

new SYNC & METAL / Inverted Sync
settings 1 0 0 0  1 0 1 100 50 0 1  0 50 1  90 20 60 10 0 15 0 50  0 55  0 0 0  60 70 50 45  1
fx 1 plate 0.2 0.6 0.5 0.25
mod 1 1 3 0.4 10 0 0 0                  # TUNE, -20%, 4 BARS

new SYNC & METAL / Metal Bells
settings 1 0 0 0  1 0 2 92 47 0 0  0 0 0  60 40 70 10 0 15 0 60  0 58  0 0 0  0 85 0 80  1
fx 1 hall 0.65 0.6 0.5 0.3
mod 1 1 4 0.65 10 1 0 0                 # MIX, 4 BARS

new SYNC & METAL / Clangers
settings 1 2 0 0  1 2 2 71 50 0 0  0 0 0  60 40 60 20 0 30 0 60  0 71  0 0 0  0 60 0 50  0
fx 1 ring 0.6 0.1667 0.3 0.35           # 145 HZ
fx 2 plate 0.2 0.55 0.5 0.25
mod 1 1 8 0.65 9 0 0 0                  # RING's FREQ, 2 BARS

new SYNC & METAL / Sync Pad
settings 1 0 0 0  1 2 1 100 50 1 0  45 0 0  75 30 55 15 0 15 0 50  0 35  0 0 25  65 70 80 70  1
fx 1 ensemble 0.25 0.6 0.8 0.5
fx 2 hall 0.7 0.6 0.45 0.3
mod 1 1 5 0.6 11 0 0 0                  # LFO: the sweep of the width deepens, 8 BARS

new SYNC & METAL / Metal Drone
settings 0 0 0 0  0 0 2 46 50 0 0  0 0 0  60 20 45 50 0 10 0 50  0 72  0 0 0  40 80 85 80  1
fx 1 flanger 0.15 0.6 0.7 0.5
mod 1 1 10 0.7 10 1 0 0                 # FLANGER's FEEDBACK, 4 BARS

new SYNC & METAL / Sync Stab
settings 1 0 0 0  1 0 1 50 50 0 1  0 40 0  80 30 40 20 0 45 0 60  0 95  0 0 0  0 45 20 30  1
fx 1 saturate 0.3 0.375 0.5 0.4
fx 2 ping 0.5625 0.3 0.5 0.2
mod 1 1 2 0.65 5 5 0 0                  # ENV, RANDOM each 1/4

new SYNC & METAL / Cross Wah
settings 1 0 0 0  1 0 2 63 50 0 0  0 0 0  50 20 40 60 0 20 0 50  0 67  0 0 0  0 60 70 35  1
fx 1 chorus 0.25 0.4 0.125 0.3
mod 1 1 0 0.7 5 0 0 0                   # CUTOFF, a wah each 1/4

new SYNC & METAL / Gong Metal
settings 0 0 0 0  0 0 2 21 50 0 0  0 0 0  60 20 50 20 0 15 0 50  0 40  0 0 0  0 90 0 85  1
fx 1 fold 0.3 0.5 0.5 0.3
fx 2 hall 0.7 0.65 0.4 0.3
mod 1 1 8 0.65 9 0 0 0                  # FOLD's DRIVE, 2 BARS

# ATMOS: weather, rooms and machines; held, slow, drifting.

new ATMOS / Night Wind
settings 1 0 0 0  1 3 0 50 50 0 0  0 0 0  100 30 45 85 25 10 0 98  0 72  2 0 30  80 90 85 90  1
fx 1 hall 0.8 0.75 0.4 0.4              # all NOISE through a resonant filter: wind that whistles the chord
mod 1 1 0 0.65 12 6 0 0 0.15            # CUTOFF drifts, free
mod 2 1 33 0.75 12 6 0 0 0.1

new ATMOS / Deep Space
settings 0 0 0 0  0 0 2 50 52 0 0  0 0 0  50 20 35 30 0 20 0 50  0 39  0 0 0  85 90 80 90  1
fx 1 shimmer 0.55 0.8 0.5 0.4
mod 1 1 4 0.65 12 6 0 0 0.05            # MIX, from saw to METAL, drifting

new ATMOS / Frozen Choir
settings 1 0 1 0  1 0 0 50 60 1 0  8 0 0  50 30 60 10 0 10 0 50  0 60  0 40 55  70 70 85 80  1
fx 1 vowel 0.5 0.5 0.6 0.7
fx 2 hall 0.75 0.7 0.45 0.35
mod 1 1 8 0.75 12 6 0 0 0.08            # the VOWEL drifts

new ATMOS / Radio Ghosts
settings 1 2 0 0  1 0 2 67 50 0 0  0 0 0  40 50 50 20 0 15 0 50  0 56  0 0 0  70 80 80 80  1
fx 1 wear 0.4 0.6 0.4 0.35
fx 2 ring 0.55 0.1667 0.5 0.3
mod 1 1 12 0.7 12 6 0 0 0.1             # RING's FREQ drifts: the station wanders

new ATMOS / Haunted Organ
settings 0 2 0 0  2 0 0 79 48 0 0  0 0 0  40 30 50 10 0 10 0 50  0 29  0 0 0  40 60 90 70  1
fx 1 wear 0.6 0.3 0.2 0.4
fx 2 reverse 0.8125 0.35 0.35           # 1/4 DOT, backwards behind it
mod 1 1 8 0.75 12 6 0 0 0.12            # WEAR's WOW

new ATMOS / Underwater
settings 1 0 0 0  1 2 0 50 55 0 0  0 0 0  50 0 30 40 0 10 0 50  0 55  0 0 0  70 80 85 80  1
fx 1 phaser 0.1 0.7 0.7 0.6
fx 2 dub 0.8125 0.35 0.3 0.25
mod 1 1 0 0.62 12 0 0 0 0.1

new ATMOS / Tape Memory
settings 1 0 0 0  1 0 0 50 57 0 0  0 0 0  50 40 50 10 0 10 0 50  0 61  0 0 0  65 70 85 80  1
fx 1 wear 0.5 0.4 0.5 0.4
fx 2 tape 0.6875 0.45 0.6 0.3           # 1/4
mod 1 1 8 0.75 12 6 0 0 0.15            # WEAR's WOW drifts
mod 2 1 14 0.7 10 0 0 0                 # TAPE's AGE

new ATMOS / Bloom Field
settings 1 2 0 0  2 0 0 79 53 0 0  0 0 0  40 40 55 20 0 15 0 60  0 39  0 0 30  80 80 80 80  1
fx 1 bloom 0.7 0.75 0.5 0.45
mod 1 1 5 0.65 10 0 0 0                 # LFO: the filter's wobble comes and goes
mod 2 1 33 0.7 12 6 0 0 0.1

new ATMOS / Static Drift
settings 1 0 0 0  1 3 0 50 50 0 0  0 0 0  30 30 50 20 0 10 0 50  0 95  0 0 0  70 80 85 80  1
fx 1 crush 0.55 0.6 0.4
fx 2 hall 0.7 0.7 0.4 0.35
mod 1 1 4 0.7 12 6 0 0 0.1              # MIX: more NOISE, less note, drifting

# CLASSIC 80S: the decade's moves, made here.

new CLASSIC 80S / Neon Brass
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 20 30 10 0 50 0 60  0 66  0 0 0  20 55 70 35  1
fx 1 chorus 0.3 0.5 0.125 0.4
fx 2 plate 0.25 0.6 0.5 0.25
mod 1 1 0 0.58 9 0 0 0

new CLASSIC 80S / Arcade Keys
settings 1 2 0 0  2 2 0 50 50 0 0  0 0 0  30 30 70 10 0 10 0 50  0 38  0 0 0  0 50 30 40  1
fx 1 ping 0.3125 0.35 0.5 0.25          # 1/8
mod 1 1 4 0.65 8 4 0 0                  # MIX, a SQUARE a bar

new CLASSIC 80S / Night Drive
settings 1 0 0 0  1 0 1 58 50 0 1  0 25 0  70 20 55 15 0 20 0 50  0 62  0 0 0  10 60 70 45  1
fx 1 tape 0.6875 0.35 0.35 0.25         # 1/4
fx 2 hall 0.65 0.6 0.45 0.25
mod 1 1 33 0.65 10 0 0 0

new CLASSIC 80S / Gated Stab
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 30 45 15 0 30 0 50  0 95  0 0 0  0 50 60 30  1
fx 1 plate 0.15 0.45 0.55 0.45
mod 1 2 32 1 0 0 0 0                    # a STEPPER on LEVEL, each half step: a gate
modsteps 1 4 0 0 0 0 -1 -1 0 0 0 0 0 0 0 0 0 0 0 0

new CLASSIC 80S / Chorus Pluck
settings 1 0 0 0  1 2 0 50 54 0 0  0 0 0  40 30 45 15 0 40 0 60  0 72  0 0 0  0 55 0 45  1
fx 1 ensemble 0.3 0.6 0.8 0.5
mod 1 1 2 0.6 9 1 0 0

new CLASSIC 80S / Video Strings
settings 1 0 1 0  1 0 0 50 58 1 0  6 0 0  50 40 60 5 0 8 0 60  0 66  0 40 60  50 60 90 60  1
fx 1 phaser 0.2 0.6 0.65 0.5
fx 2 hall 0.65 0.55 0.45 0.25
mod 1 1 11 0.65 10 0 0 0                # PHASER's MIX

new CLASSIC 80S / Power Ballad
settings 1 0 0 0  1 2 2 50 45 0 0  0 0 0  70 40 50 10 0 20 0 50  0 27  0 0 0  0 75 30 60  1
fx 1 chorus 0.25 0.45 0.125 0.4
fx 2 hall 0.75 0.65 0.45 0.35
mod 1 1 4 0.6 10 0 0 0

new CLASSIC 80S / Pulse Pop
settings 1 1 0 0  1 2 0 50 54 0 0  0 0 0  35 30 55 15 0 25 0 60  0 72  0 0 0  0 55 40 35  1
fx 1 delay 0.4167 0.3 0.5 0.2           # 1/8 DOT
mod 1 1 0 0.6 8 1 0 0

# MOVING: a MOD is most of the sound.

new MOVING / Filter Steps
settings 1 0 0 0  1 2 0 50 54 0 0  0 0 0  50 20 35 50 0 20 0 50  0 59  0 0 0  0 60 70 40  1
fx 1 ping 0.5625 0.3 0.5 0.15
mod 1 2 0 0.75 1 0 0 0                  # a STEPPER on CUTOFF, every step, sliding
modsteps 1 16 0.3 0 0.8 -0.6 0 0.5 -0.9 0.2 1 -0.3 0.6 -0.8 0.1 0.9 -0.5 0 0.4 -0.2

new MOVING / Pan Dance
settings 1 2 0 0  1 0 0 50 53 0 0  0 0 0  40 30 50 15 0 25 0 60  0 43  0 0 0  0 55 30 40  1
fx 1 plate 0.2 0.55 0.5 0.2
mod 1 2 33 0.85 3 0 0 0                 # a STEPPER on PAN, 1/8
modsteps 1 6 0 0 -1 0.6 -0.2 1 -0.7 0.3 0 0 0 0 0 0 0 0 0 0
mod 2 1 20 0.75 9 4 0 0                 # the ECHO send, a SQUARE every 2 BARS

new MOVING / Breathing Mix
settings 1 0 0 0  1 2 1 63 50 0 0  0 0 0  50 20 45 30 0 20 0 50  0 25  0 0 0  10 60 75 50  1
fx 1 chorus 0.25 0.45 0.125 0.35
mod 1 1 4 0.8 9 0 0 0                   # MIX, +60%, 2 BARS: plain to synced and back
mod 2 1 1 0.65 12 6 0 0 0.2             # RESONANCE drifts

new MOVING / Random Tone
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 30 40 35 0 25 0 60  0 95  0 0 0  0 60 50 40  1
fx 1 chorus 0.25 0.45 0.125 0.35
mod 1 1 0 0.75 3 5 0 0                  # CUTOFF, RANDOM each 1/8
mod 2 1 9 0.7 12 6 0 0 0.2              # CHORUS's DEPTH drifts

new MOVING / Swell And Fall
settings 1 0 0 0  1 0 0 50 56 0 0  0 0 0  50 30 50 15 0 20 0 50  0 55  0 0 0  20 70 80 55  1
fx 1 hall 0.6 0.6 0.45 0.3
mod 1 1 32 0.3 8 1 0 0                  # LEVEL, -40%, a TRIANGLE a bar
mod 2 1 0 0.65 8 1 0 0                  # CUTOFF with it

new MOVING / Interval Walk
settings 1 0 0 0  1 0 0 50 50 0 0  0 0 0  50 30 55 15 0 20 0 50  0 67  0 0 0  0 55 60 40  1
fx 1 ping 0.4375 0.3 0.5 0.2
mod 1 2 3 0.8 5 0 0 0                   # a STEPPER on TUNE: DCO-2 walks to other intervals, 1/4
modsteps 1 8 0 0 0 0.33 0.12 0.6 -0.2 0.45 0.25 1 0 0 0 0 0 0 0 0

new MOVING / Echo Throws
settings 1 2 0 0  1 0 0 50 55 0 0  0 0 0  40 30 50 15 0 30 0 60  0 57  0 0 0  0 50 20 35  1
mod 1 2 20 1 3 0 0 0                    # a STEPPER on the ECHO send, 1/8: a throw now and then
modsteps 1 7 0 0 -1 -1 1 -1 -1 -1 1 0 0 0 0 0 0 0 0 0
mod 2 1 21 0.65 10 0 0 0                # the SPACE send, 4 BARS

new MOVING / Slow Morph
settings 1 0 0 0  1 2 2 58 50 0 0  0 0 0  50 30 45 30 0 30 0 50  0 38  0 0 0  30 70 75 65  1
fx 1 hall 0.6 0.6 0.45 0.3
mod 1 1 4 0.75 12 6 0 0 0.07            # MIX drifts, saw to METAL
mod 2 1 2 0.7 12 6 0 0 0.11             # ENV drifts

new MOVING / Trance Gate
settings 1 0 0 0  1 0 0 50 58 0 0  0 0 0  50 30 55 15 0 15 0 50  0 69  0 0 0  10 60 90 35  1
fx 1 ping 0.3125 0.35 0.5 0.2           # 1/8
mod 1 2 32 1 0 0 0 0                    # a STEPPER on LEVEL, each half step
modsteps 1 16 0 0 0 -1 0 -1 0 0 -1 0 -1 0 0 -1 0 -1 0 0
mod 2 1 0 0.7 10 0 0 0                  # CUTOFF opens over 4 BARS
)presets"
