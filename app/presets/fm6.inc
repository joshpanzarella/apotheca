R"presets(
# FM-6's built-in presets past the engine's own, and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/fm6.hpp (an operator's setting: fm6::opSetting).
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# ---- The first added set (2026-11-16): 49 whole track sounds in eight folders ---------------------
# Every one plays the phrase of `presets_test --heard-fm6` inside its limits (loudness, no clip, a
# tail that ends, each MOD moving AND heard). Its figures and the idea of each: fm6-plan.md, section 11.
#
# Two kinds: a factory voice as `base` given effects, sends and MODs; and new voices, written an
# operator a line:
#   op N  R1 R2 R3 R4  L1 L2 L3 L4  BP LD RD LC RC  RS AMS VEL  LEVEL MODE COARSE FINE DETUNE
# (rates and levels 0..99; BP break point, LD / RD left and right depth, LC / RC curves; RS rate
# scaling 0..7; AMS 0..3; VEL 0..7; MODE 0 RATIO; COARSE 0 is 0.5, else the ratio; FINE adds
# COARSE x FINE %; DETUNE 7 in tune.) The voice's own by id: 126-129 PITCH ENV rates, 130-133 its
# levels (50 none), 134 ALGORITHM (0..31: shown 1..32), 135 FEEDBACK 0..7, 136 KEY SYNC, 137 LFO
# SPEED, 138 LFO DELAY, 139 PMD, 140 AMD, 141 LFO SYNC, 142 LFO WAVE (4 SINE), 143 PMS 0..7,
# 144 TRANSPOSE (24 none), 145 OPERATORS ON (63 all).
# Algorithms used: 1 (2>1, 6>5>4>3), 3 (3>2>1, 6>5>4), 5 (2>1, 4>3, 6>5: three pairs), 16 (one
# carrier, three stacks), 31 (5 carriers, 6>5); feedback is on op 6 in each.
# MOD targets: macros 0 BRIGHT 1 FEEDBACK 2 ATTACK 3 DECAY 4 RELEASE 5 DETUNE 6 LFO 7 PITCH ENV;
# slot 1's settings 8-11, slot 2's 12-15, slot 3's 16-19; 20 ECHO, 21 SPACE; 32 LEVEL, 33 PAN.
# BRIGHT moves only modulators: a MOD on it is heard only where a modulator sounds. FEEDBACK up does
# nothing at 7. LFO does nothing where PMS and AMS are 0. The test hears each MOD, so a dead one fails.

# KEYS: electric pianos, a clav, a celeste, vibes.

new KEYS / Suitcase
base ROM 1A / E.PIANO 1
fx 1 tremolo 0.45 0.35 0.125 1     # 1/8, 35%, SINE, SPREAD 100%: the two sides swap, an autopan
fx 2 chorus 0.3 0.4 0.125 0.5      # a light CHORUS
fx 3 plate 0.1 0.45 0.5 0.2
send 2 0.12
mod 1 1 0 0.62 9 0 0 0             # BRIGHT +24%, 2 BARS: the bark comes and goes
mod 2 1 9 0.3 10 0 0 0             # the TREMOLO's DEPTH -40%, 4 BARS

new KEYS / Glass Tines
base ROM 1A / E.PIANO 1
set 134 4  135 3  136 1  143 0     # ALGORITHM 5 (three pairs), FEEDBACK 3, KEY SYNC, no vibrato
op 1  96 30 22 55  99 85  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7   # the body, ratio 1
op 2  96 55 30 60  99 60  0  0  39 0 0 0 0  3 0 5  75 0  1  0  7   # its warmth, gone in a moment
op 3  96 35 25 55  99 80  0  0  39 0 0 0 0  3 0 2  90 0  1  0 10   # a second body, a little sharp
op 4  99 75 40 60  99  0  0  0  39 0 0 0 0  3 0 6  62 0 14  0  7   # the tine: ratio 14, a click
op 5  97 40 25 50  99 70  0  0  39 0 0 0 0  3 0 3  74 0  4  0  4   # a glass layer two octaves up
op 6  97 45 30 55  99 60  0  0  39 0 0 0 0  3 0 4  70 0  3 17  7   # bent by 3.51: not in tune with it
fx 1 ensemble 0.35 0.45 0.8 0.6
fx 2 plate 0.15 0.6 0.55 0.28
send 1 0.1
mod 1 1 5 0.7 12 6 0 0 0.08        # DETUNE drifts, +40%, free at 0.08 Hz: the two bodies beat
mod 2 1 0 0.35 10 0 0 0            # BRIGHT -30%, 4 BARS

new KEYS / Bark Piano
base ROM 1B / E.PIANO 3
fx 1 saturate 0.35 0.375 0.6 0.7   # TUBE
fx 2 phaser 0.25 0.6 0.65 0.5
fx 3 spring 0.5 0.4 0.5 0.18
mod 1 2 0 0.8 3 0 0 0              # a STEPPER on BRIGHT, +60%, 1/8: some notes bark
modsteps 1 8 0.1 0 1 -0.4 0.3 -0.8 0.6 -0.2 0.9 -0.6 0 0 0 0 0 0 0 0
mod 2 1 8 0.7 9 0 0 0              # the SATURATE's DRIVE, +40%, 2 BARS

new KEYS / Reed Piano
base ROM 1A / E.PIANO 1
set 134 4  135 5  136 1  143 0
op 1  97 40 30 60  99 85  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7
op 2  97 60 35 60  99 70  0  0  39 0 0 0 0  3 0 6  82 0  1  0  7   # ratio 1, hard with velocity: the reed
op 3  97 45 28 60  99 80  0  0  39 0 0 0 0  3 0 2  85 0  1  0  9
op 4  99 70 40 60  99 30  0  0  39 0 0 0 0  3 0 6  68 0  3  0  7   # ratio 3: the nasal edge
op 5  98 50 35 60  99 60  0  0  39 0 0 0 0  3 0 2  80 0  2  0  7   # an octave up, bent by op 6
op 6  98 45 30 60  99 75  0  0  39 0 0 0 0  3 0 4  82 0  1  0  7   # FEEDBACK 5 on it
fx 1 drive 0.3 0.55 1 0.5
fx 2 tremolo 0.45 0.45 0.125 0     # 1/8, 45%: the amp's tremolo
send 2 0.15
mod 1 1 0 0.65 8 1 0 0             # BRIGHT +30%, a TRIANGLE a bar: the reed bites and softens
mod 2 1 9 0.3 9 0 0 0              # the TREMOLO's DEPTH -40%, 2 BARS

new KEYS / Funk Clav Wah
base ROM 3B / FUNK CLAV
fx 1 vowel 0.1 0.5 0.7 0.85        # A-E, RESONANCE 70%
fx 2 eq 0.5 0.6 0.6 0.6            # a little up in the middle and the top
fx 3 spring 0.6 0.4 0.5 0.15
mod 1 2 8 0.9 3 0 0 0              # a STEPPER on the VOWEL, +80%, 1/8: a wah pedal
modsteps 1 8 0.3 0 0 0.7 0.2 1 0 0.5 0.9 0.3 0 0 0 0 0 0 0 0
mod 2 1 33 0.7 8 0 0 0             # PAN +40%, a bar

new KEYS / Worn Celeste
base ROM 1B / CELESTE
fx 1 wear 0.4 0.3 0.15 0.45        # WOW 40%, DIRT 30%
fx 2 tape 0.5625 0.4 0.5 0.3       # 1/8 DOT
send 2 0.2
mod 1 1 8 0.7 12 6 0 0 0.15        # WEAR's WOW drifts, +40%, free
mod 2 1 14 0.75 10 1 0 0           # the TAPE's AGE +50%, a TRIANGLE over 4 BARS

new KEYS / Motor Vibes
base ROM 1A / E.PIANO 1
set 134 4  135 0  136 1  143 0
op 1  99 35 25 45  99 75  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7   # the bar
op 2  99 70 30 50  99 40  0  0  39 0 0 0 0  3 0 5  65 0  1  0  7   # a soft mallet
op 3  99 50 30 45  99 50  0  0  39 0 0 0 0  3 0 4  78 0  4  0  7   # the bar's fourth partial
op 4  99 80 30 50  99  0  0  0  39 0 0 0 0  3 0 6  55 0  1  0  7
op 5  99 80 40 50  99  0  0  0  39 0 0 0 0  3 0 6  60 0 10  0  7   # the strike, ratio 10
op 6  99 80 40 50  99  0  0  0  39 0 0 0 0  3 0 4  40 0  1  0  7
fx 1 tremolo 0.35 0.4 0.125 0.5    # 1/8 TRIP: the motor, a little wide
fx 2 hall 0.65 0.55 0.5 0.25
mod 1 1 9 0.3 10 0 0 0             # the motor's DEPTH -40%, 4 BARS: it slows and speeds
mod 2 1 3 0.65 9 0 0 0             # DECAY +30%, 2 BARS

# BASS: played low, as a line.

new BASS / Growl
base ROM 1A / E.PIANO 1
set 134 2  135 7  136 1  143 0     # ALGORITHM 3: a sub stack and a growl stack
op 1  99 50 40 70  99 90 85  0  39 0 0 0 0  3 0 1  95 0  1  0  7   # the round low part, ratio 1
op 2  99 60 40 70  99 70 60  0  39 0 0 0 0  3 0 2  65 0  1  0  7
op 3  99 60 40 70  99 70 60  0  39 0 0 0 0  3 0 2   0 0  1  0  7   # (silent)
op 4  99 45 40 70  99 90 85  0  39 0 0 0 0  3 0 1  95 0  2  0  7   # the growl's carrier, an octave up
op 5  99 40 35 70  99 80 60  0  39 0 0 0 0  3 0 4  82 0  2  0  7
op 6  99 45 35 70  99 80 60  0  39 0 0 0 0  3 0 3  80 0  2  0  7   # FEEDBACK 7: a rough saw
fx 1 filter 0.5 0.5 0.3 1          # LOW 24 at 810 HZ, RESONANCE 50%
fx 2 drive 0.35 0.5 0.6 0.5
mod 1 1 8 0.75 4 0 0 0             # the CUTOFF +50%, 1/4 TRIP: a wobble
mod 2 1 1 0.3 8 2 0 0              # FEEDBACK -40%, a RAMP a bar: the growl clears and comes back

new BASS / Slap Snap
base ROM 1A / BASS 1
fx 1 comp 0.5 0.5833 0.15 1        # -24 DB, 4:1, fast
fx 2 eq 0.6 0.4 0.4 0.7            # LOW +2.4, MID -2.4 DB at 620 HZ, HIGH +4.8
fx 3 snap 0.7 0.5 1                # ATTACK +40%
mod 1 2 0 0.75 1 0 0 0             # a STEPPER on BRIGHT, +50%, every step: the slaps and the pops
modsteps 1 6 0 0 1 -0.5 0.2 0.8 -0.6 0 0 0 0 0 0 0 0 0 0 0

new BASS / Rubber Sub
base ROM 1A / E.PIANO 1
set 126 99  127 70  128 60  129 60  130 62  131 50  132 50  133 50   # PITCH ENV: up at once, then down: a boing
set 134 4  135 0  136 1  143 0  145 60 # three pairs, the third off (ops 5 and 6)
op 1  99 50 40 70  99 92 88  0  39 0 0 0 0  3 0 1  99 0  1  0  7   # the round low part
op 2  99 65 40 70  99 40  0  0  39 0 0 0 0  3 0 5  80 0  1  0  7   # its pluck
op 3  99 70 40 70  99 40  0  0  39 0 0 0 0  3 0 3  82 0  2  0  7   # a body an octave up
op 4  99 85  0 70  99  0  0  0  39 0 0 0 0  3 0 6  60 0  3  0  7   # a click
op 5  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
op 6  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
fx 1 dub 0.3125 0.4 0.35 0.2       # 1/8
fx 2 comp 0.55 0.4167 0.3 1        # -22 DB, 3:1
mod 1 2 7 0.8 3 0 0 0              # a STEPPER on PITCH ENV, +60%, 1/8: some notes boing further
modsteps 1 8 0 0 1 0 -0.6 0.5 0 1 -1 0.3 0 0 0 0 0 0 0 0

new BASS / Fretless Chorus
base ROM 3B / FRETLESS 2
fx 1 chorus 0.3 0.5 0.125 0.6
fx 2 comp 0.65 0.4167 0.3 1        # -15 DB, 3:1: an even line
fx 3 eq 0.6 0.5 0.5 0.55           # LOW +2.4 DB
mod 1 1 5 0.7 12 6 0 0 0.12        # DETUNE drifts, +40%, free
mod 2 1 0 0.65 9 0 0 0             # BRIGHT +30%, 2 BARS

new BASS / Acid FM
base ROM 1A / E.PIANO 1
set 134 0  135 6  136 1  143 0     # ALGORITHM 1
op 1  99 40 40 65  99 90 85  0  39 0 0 0 0  3 0 1  95 0  1  0  7
op 2  99 55 45 65  99 55 35  0  39 0 0 0 0  3 0 5  86 0  1  0  7   # its index falls fast: a filter's sweep
op 3  99 40 40 65  99 90 85  0  39 0 0 0 0  3 0 1  82 0  1  0  7   # a second carrier, under the sweep
op 4  99 60 40 65  99 50 30  0  39 0 0 0 0  3 0 4  65 0  1  0  7
op 5  99 60 40 65  99 40 20  0  39 0 0 0 0  3 0 4  55 0  2  0  7
op 6  99 60 40 65  99 40 20  0  39 0 0 0 0  3 0 4  60 0  1  0  7
fx 1 drive 0.45 0.5 0.7 0.5
fx 2 ping 0.3125 0.35 0.4 0.2      # 1/8
mod 1 2 0 0.85 3 0 0 0             # a STEPPER on BRIGHT, +70%, 1/8: the accents
modsteps 1 8 0 0 1 -0.3 0 0.7 -0.6 1 0.2 -0.8 0 0 0 0 0 0 0 0
mod 2 2 3 0.3 3 0 0 0              # a STEPPER of 5 on DECAY, -40%: it runs against the accents
modsteps 2 5 0 0 1 -1 0.3 -0.5 0 0 0 0 0 0 0 0 0 0 0 0

new BASS / Dub Sub
base ROM 2B / SYN-BASS 1
fx 1 filter 0.4 0.3 0.1 1          # LOW 12 at 410 HZ
fx 2 dub 0.5625 0.45 0.3 0.3       # 1/8 DOT, a dark dub
send 2 0.1
mod 1 1 13 0.8 10 0 0 0            # the DUB's FEEDBACK +60%, 4 BARS
mod 2 1 8 0.65 9 1 0 0             # the CUTOFF +30%, a TRIANGLE over 2 BARS
# BELLS & METAL: tubes, bowls, chimes and struck steel, rung out through their effects.

new BELLS & METAL / Shimmer Bells
base ROM 1A / TUB BELLS
fx 1 shimmer 0.55 0.7 0.55 0.4     # SHIMMER 55%, DECAY 7 S
send 1 0.1
send 2 0.2
mod 1 1 21 0.75 12 6 0 0 0.07      # the SPACE send drifts, +50%, free at 0.07 Hz
mod 2 1 8 0.7 12 0 0 0 0.05        # the SHIMMER's amount swells, +40%, free at 0.05 Hz

new BELLS & METAL / Glass Bowl
base ROM 1A / E.PIANO 1
set 134 4  135 2  136 1  143 0
op 1  99 30 22 30  99 80  0  0  39 0 0 0 0  2 0 2  99 0  1  0  7   # the bowl, ratio 1
op 2  99 35 25 30  99 60  0  0  39 0 0 0 0  2 0 4  72 0  1 41  7   # bent by 1.41: inharmonic
op 3  99 35 25 30  99 70  0  0  39 0 0 0 0  2 0 2  82 0  2 38  9   # a partial at 2.76
op 4  99 45 30 30  99 40  0  0  39 0 0 0 0  2 0 5  62 0  3 17  7   # bent by 3.51
op 5  99 45 30 30  99 50  0  0  39 0 0 0 0  2 0 3  70 0  5  8  6   # a partial at 5.4
op 6  99 60 30 30  99 30  0  0  39 0 0 0 0  2 0 5  50 0  1  0  7
fx 1 plate 0.2 0.7 0.5 0.35
send 2 0.15
mod 1 1 5 0.75 12 6 0 0 0.1        # DETUNE drifts, +50%, free: the partials beat against each other
mod 2 1 33 0.75 12 0 0 0 0.11      # PAN, +50%, free

new BELLS & METAL / Hammered Steel
base ROM 1A / E.PIANO 1
set 134 4  135 6  136 1  143 0
op 1  99 40 25 40  99 70  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7
op 2  99 60 30 40  99 40  0  0  39 0 0 0 0  3 0 6  84 0  3  5  7   # 3.15: a struck plate
op 3  99 45 25 40  99 60  0  0  39 0 0 0 0  3 0 2  85 0  1 98  3   # 1.98, a hair flat of the octave
op 4  99 70  0 40  99  0  0  0  39 0 0 0 0  3 0 6  70 0  7  2  7   # 7.14: the clang
op 5  99 55 30 40  99 40  0  0  39 0 0 0 0  3 0 3  72 0  4  5  7   # 4.2
op 6  99 70  0 40  99  0  0  0  39 0 0 0 0  3 0 6  60 0  0  0  7   # 0.5, FEEDBACK 6: grit
fx 1 saturate 0.3 0.875 0.6 0.4    # HARD
fx 2 ping 0.4375 0.4 0.5 0.25      # 1/4 TRIP
mod 1 2 0 0.8 3 0 0 0              # a STEPPER on BRIGHT, +60%, 1/8: hammer and tap
modsteps 1 6 0 0 1 -0.5 0.3 -1 0.7 -0.2 0 0 0 0 0 0 0 0 0 0
mod 2 1 13 0.7 9 0 0 0             # the PING's FEEDBACK +40%, 2 BARS

new BELLS & METAL / Clang Ring
base ROM 1A / ORCH-CHIME
fx 1 ring 0.7 0.1667 0.4 0.45      # 360 HZ, SINE, SPREAD 40%
fx 2 hall 0.7 0.6 0.45 0.3
mod 1 1 8 0.65 12 6 0 0 0.09       # the RING's FREQ drifts, +30%, free
mod 2 1 11 0.3 10 0 0 0            # the RING's MIX -40%, 4 BARS: in and out of the ring

new BELLS & METAL / Pan Dub
base ROM 1A / STEEL DRUM
fx 1 dub 0.5625 0.45 0.5 0.3       # 1/8 DOT
send 2 0.1
mod 1 1 10 0.75 9 0 0 0            # the DUB's TONE +50%, 2 BARS: the echoes open and close
mod 2 2 33 0.9 3 0 0 0             # a STEPPER on PAN, +80%, 1/8
modsteps 2 6 0.2 0 -1 1 -0.5 0.5 0 0.8 0 0 0 0 0 0 0 0 0 0

new BELLS & METAL / Chime Cluster
base ROM 1A / E.PIANO 1
set 134 30  135 3  136 1  143 0    # ALGORITHM 31: five sines heard, op 6 bends op 5
op 1  99 30 26 30  99 75  0  0  39 0 0 0 0  2 0 2  92 0  1  0  7   # 1.0
op 2  99 34 28 30  99 70  0  0  39 0 0 0 0  2 0 3  85 0  2 16  9   # 2.32
op 3  99 40 30 32  99 60  0  0  39 0 0 0 0  2 0 3  80 0  3 31  5   # 3.93
op 4  99 45 32 34  99 50  0  0  39 0 0 0 0  2 0 4  76 0  5  7  7   # 5.35
op 5  99 50 35 36  99 40  0  0  39 0 0 0 0  2 0 4  72 0  7  2  7   # 7.14
op 6  99 60  0 36  99  0  0  0  39 0 0 0 0  2 0 5  60 0  1 41  7   # bends op 5 at the strike
fx 1 reverse 0.6875 0.3 0.3        # 1/4: each chime again backwards
fx 2 hall 0.75 0.65 0.5 0.3
mod 1 1 5 0.75 12 6 0 0 0.13       # DETUNE drifts, +50%, free
mod 2 1 9 0.7 10 0 0 0             # the REVERSE's FEEDBACK +40%, 4 BARS

# PADS: slow, wide and moving.

new PADS / Glass Pad
base ROM 1A / E.PIANO 1
set 134 4  135 2  136 0  137 30  138 0  139 4  142 4  143 2   # a slow vibrato, a little
op 1  45 30 30 40  99 92 90  0  39 0 0 0 0  2 0 0  92 0  1  0 10
op 2  40 30 25 40  99 85 80  0  39 0 0 0 0  2 0 0  62 0  3  0  7   # ratio 3: glass
op 3  48 30 30 40  99 92 90  0  39 0 0 0 0  2 0 0  92 0  1  0  4
op 4  35 25 25 40  90 80 75  0  39 0 0 0 0  2 0 0  70 0  1  0  7
op 5  42 30 30 40  99 90 88  0  39 0 0 0 0  2 0 0  80 0  2  0  7
op 6  30 25 25 40  99 80 70  0  39 0 0 0 0  2 0 0  55 0  5  0  7
fx 1 ensemble 0.3 0.6 0.8 0.8
fx 2 hall 0.8 0.7 0.5 0.35
mod 1 1 0 0.7 12 6 0 0 0.06        # BRIGHT drifts, +40%, free at 0.06 Hz
mod 2 1 33 0.7 12 0 0 0 0.09       # PAN, +40%, free

new PADS / Bloom Strings
base ROM 1A / STRINGS 2
fx 1 bloom 0.7 0.7 0.5 0.4         # a 1.1 s swell, 7 S
mod 1 1 5 0.7 9 1 0 0              # DETUNE +40%, a TRIANGLE over 2 BARS
mod 2 1 11 0.65 10 0 0 0           # the BLOOM's MIX +30%, 4 BARS

new PADS / Vowel Choir
base ROM 1A / VOICE 1
fx 1 vowel 0.3 0.5 0.55 0.6        # E-I
fx 2 hall 0.75 0.65 0.45 0.3
mod 1 1 8 0.85 9 1 0 0             # the VOWEL sweeps, +70%, 2 BARS
mod 2 1 6 0.7 10 0 0 0             # LFO +40%, 4 BARS: the choir's vibrato deepens

new PADS / Saw Drift
base ROM 1A / E.PIANO 1
set 134 4  135 4  136 0  143 0
op 1  60 40 30 45  99 95 95  0  39 0 0 0 0  2 0 0  95 0  1  0  4   # three pairs, each ratio 1:1: a saw
op 2  60 35 30 45  99 80 72  0  39 0 0 0 0  2 0 0  80 0  1  0  7
op 3  58 40 30 45  99 95 95  0  39 0 0 0 0  2 0 0  95 0  1  0 11
op 4  58 35 30 45  99 80 72  0  39 0 0 0 0  2 0 0  80 0  1  0  7
op 5  62 40 30 45  99 95 95  0  39 0 0 0 0  2 0 0  85 0  2  0  7   # the third an octave up
op 6  62 35 30 45  99 80 72  0  39 0 0 0 0  2 0 0  78 0  2  0  7
fx 1 filter 0.62 0.3 0.3 1         # LOW 24 at 1.9 KHZ
fx 2 chorus 0.25 0.6 0.375 0.8     # ENSEMBLE
send 2 0.25
mod 1 1 8 0.3 10 1 0 0             # the CUTOFF -40%, a TRIANGLE over 4 BARS
mod 2 1 1 0.75 11 0 0 0            # FEEDBACK +50%, 8 BARS: the third pair gets rough

new PADS / Frozen Pipes
base ROM 1A / PIPES 1
fx 1 plate 0.3 0.8 0.4 0.35        # 13 S
fx 2 eq 0.45 0.5 0.5 0.6
mod 1 1 32 0.35 12 0 0 0 0.25      # LEVEL -30%, free at 0.25 Hz: it breathes
mod 2 1 0 0.65 10 1 0 0            # BRIGHT +30%, 4 BARS

new PADS / Water Bloom
base ROM 2B / WATER GDN
fx 1 chorus 0.2 0.5 0.375 0.6      # ENSEMBLE
fx 2 bloom 0.8 0.75 0.55 0.45      # a 1.6 s swell, 9 S
mod 1 1 15 0.7 12 6 0 0 0.05       # the BLOOM's MIX drifts, +40%, free
mod 2 1 33 0.75 12 0 0 0 0.08      # PAN, +50%, free

# LEADS: one line on top.

new LEADS / Hollow Square
base ROM 1A / E.PIANO 1
set 134 4  135 0  136 1  137 35  138 40  139 8  142 4  143 3  145 60   # a vibrato that comes in late
op 1  95 50 40 60  99 92 88  0  39 0 0 0 0  2 0 1  99 0  1  0  7
op 2  95 50 40 60  99 85 80  0  39 0 0 0 0  2 0 3  80 0  2  0  7   # ratio 2 on 1: odd harmonics, hollow
op 3  95 50 40 60  99 92 88  0  39 0 0 0 0  2 0 1  80 0  1  0 10
op 4  95 50 40 60  99 85 80  0  39 0 0 0 0  2 0 3  76 0  2  0  7
op 5  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
op 6  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
fx 1 drive 0.4 0.55 0.6 0.5
fx 2 ping 0.5625 0.4 0.5 0.25      # 1/8 DOT
mod 1 1 6 0.75 9 1 0 0             # LFO +50%, 2 BARS: the vibrato widens
mod 2 1 0 0.65 8 0 0 0             # BRIGHT +30%, a bar

new LEADS / Wasp
base ROM 2B / SYN-LEAD 2
fx 1 fuzz 0.5 0.6 0.1 0.6
fx 2 ping 0.3125 0.35 0.5 0.2      # 1/8
mod 1 2 0 0.8 3 0 0 0              # a STEPPER on BRIGHT, +60%, 1/8
modsteps 1 8 0.2 0 0.8 -0.6 0.2 -1 1 0 -0.4 0.5 0 0 0 0 0 0 0 0
mod 2 1 9 0.65 9 0 0 0             # the FUZZ's TONE +30%, 2 BARS

new LEADS / Breathy Flute
base ROM 1A / FLUTE 1
fx 1 tape 0.6875 0.35 0.5 0.25     # 1/4
fx 2 plate 0.15 0.5 0.55 0.2
send 2 0.15
mod 1 1 6 0.75 10 0 0 0            # LFO +50%, 4 BARS: breath and vibrato deepen
mod 2 1 0 0.65 9 1 0 0             # BRIGHT +30%, 2 BARS

new LEADS / Scream
base ROM 1A / E.PIANO 1
set 134 0  135 7  136 1  137 38  138 30  139 6  142 4  143 3
op 1  90 45 35 60  99 92 88  0  39 0 0 0 0  2 0 1  95 0  1  0  7
op 2  90 45 35 60  99 88 84  0  39 0 0 0 0  2 0 3  85 0  1  0  7
op 3  90 45 35 60  99 90 85  0  39 0 0 0 0  2 0 1  85 0  1  0  9
op 4  90 45 35 60  99 85 80  0  39 0 0 0 0  2 0 3  82 0  1  0  7
op 5  90 45 35 60  99 85 80  0  39 0 0 0 0  2 0 3  78 0  1  0  7
op 6  90 45 35 60  99 85 80  0  39 0 0 0 0  2 0 3  80 0  1  0  7   # FEEDBACK 7 at the top of the stack
fx 1 saturate 0.4 0.375 0.55 0.6   # TUBE
fx 2 delay 0.4167 0.35 0.5 0.2     # 1/8 DOT
mod 1 1 1 0.25 5 0 0 0             # FEEDBACK -50%, 1/4: it screams and settles
mod 2 1 0 0.65 9 1 0 0             # BRIGHT +30%, 2 BARS

new LEADS / Brass Stab
base ROM 2B / SYNBRASS 1
fx 1 comp 0.55 0.5833 0.3 1
fx 2 plate 0.1 0.5 0.55 0.25
mod 1 2 2 0.75 3 0 0 0             # a STEPPER on ATTACK, +50%, 1/8: some notes swell in
modsteps 1 4 0 0 -1 0.6 -1 1 0 0 0 0 0 0 0 0 0 0 0 0
mod 2 1 0 0.3 9 0 0 0              # BRIGHT -40%, 2 BARS

new LEADS / Talk Box
base ROM 1A / SYN-LEAD 1
fx 1 vowel 0 0.5 0.65 1            # A
fx 2 comp 0.5 0.4167 0.3 1
mod 1 2 8 0.95 3 0 0 0             # a STEPPER on the VOWEL, +90%, 1/8: it talks
modsteps 1 8 0.4 0 0 0.9 0.3 0.6 1 0.1 0.7 0.4 0 0 0 0 0 0 0 0
mod 2 1 9 0.7 10 1 0 0             # the VOWEL's SHIFT +40%, 4 BARS
# PLUCKS: short notes, made by their echoes and their movement.

new PLUCKS / Koto Pong
base ROM 1A / KOTO
fx 1 ping 0.3125 0.5 0.5 0.3       # 1/8
fx 2 eq 0.5 0.55 0.6 0.6
mod 1 2 33 0.9 1 0 0 0             # a STEPPER on PAN, +80%, every step: the notes thrown about
modsteps 1 6 0 0 -1 0.6 -0.3 1 -0.7 0.2 0 0 0 0 0 0 0 0 0 0
mod 2 1 0 0.65 9 0 0 0             # BRIGHT +30%, 2 BARS

new PLUCKS / Harp Mirror
base ROM 1B / HARP 1
fx 1 reverse 0.5625 0.35 0.35      # 1/8 DOT, backwards
fx 2 hall 0.7 0.55 0.5 0.25
mod 1 1 10 0.3 10 0 0 0            # the REVERSE's MIX -40%, 4 BARS
mod 2 1 4 0.7 9 0 0 0              # RELEASE +40%, 2 BARS

new PLUCKS / Glass Pluck
base ROM 1A / E.PIANO 1
set 134 4  135 4  136 1  143 0
op 1  99 45 35 55  99 70  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7
op 2  99 75 50 55  99 40  0  0  39 0 0 0 0  3 0 6  88 0  1  0  7   # bright at the pluck, gone at once
op 3  99 55 40 55  99 50  0  0  39 0 0 0 0  3 0 3  75 0  3  0  8
op 4  99 85  0 55  99  0  0  0  39 0 0 0 0  3 0 6  65 0  7  0  7   # ratio 7: glass
op 5  99 50 35 55  99 60  0  0  39 0 0 0 0  3 0 2  80 0  1  0  5
op 6  99 80  0 55  99  0  0  0  39 0 0 0 0  3 0 5  70 0  2  0  7   # FEEDBACK 4
fx 1 ping 0.4375 0.4 0.55 0.25     # 1/4 TRIP
fx 2 plate 0.1 0.55 0.6 0.2
mod 1 1 0 0.7 8 0 0 0              # BRIGHT +40%, a bar
mod 2 1 33 0.7 12 6 0 0 0.2        # PAN drifts, +40%, free

new PLUCKS / Sitar Swirl
base ROM 1B / SITAR
set 79 99  121 80                  # its two carriers up (OP3's LEVEL 91 to 99, OP1's 73 to 80): it was quiet
fx 1 phaser 0.2 0.7 0.7 0.5
fx 2 spring 0.5 0.5 0.5 0.2
fx 3 eq 0.5 0.6 0.55 0.5           # the middle up: the buzz forward
mod 1 1 10 0.75 12 6 0 0 0.1       # the PHASER's FEEDBACK drifts, +50%, free
mod 2 1 5 0.7 10 0 0 0             # DETUNE +40%, 4 BARS

new PLUCKS / Nylon Chorus
base ROM 3B / CLAS.GUIT
fx 1 chorus 0.3 0.5 0.125 0.6
fx 2 plate 0.15 0.5 0.5 0.22
send 1 0.12
mod 1 1 0 0.65 10 1 0 0            # BRIGHT +30%, a TRIANGLE over 4 BARS
mod 2 1 33 0.7 12 6 0 0 0.12       # PAN drifts, +40%, free

new PLUCKS / Thumb Piano
base ROM 1A / E.PIANO 1
set 134 4  135 0  136 1  143 0
op 1  99 45 30 50  99 80  0  0  39 0 0 0 0  3 0 1  99 0  1  0  7   # the tine
op 2  99 80 40 50  99 30  0  0  39 0 0 0 0  3 0 6  76 0  4 30  7   # 5.2: a woody knock, gone at once
op 3  99 70 40 50  99 30  0  0  39 0 0 0 0  3 0 4  72 0  5 60  7   # a partial at 8.0, short
op 4  99 85  0 50  99  0  0  0  39 0 0 0 0  3 0 6  55 0  2  0  7
op 5  99 45 30 50  99 75  0  0  39 0 0 0 0  3 0 1  92 0  1  0 10   # a second tine, a little sharp: the metal beats
op 6  99 85  0 50  99  0  0  0  39 0 0 0 0  3 0 6  60 0  3  0  7
fx 1 tape 0.3125 0.35 0.6 0.25     # 1/8, a worn echo
fx 2 spring 0.45 0.45 0.5 0.18
fx 3 comp 0.6 0.4167 0.3 1         # -19 DB, 3:1
mod 1 2 0 0.8 3 0 0 0              # a STEPPER on BRIGHT, +60%, 1/8: hard and soft thumbs
modsteps 1 5 0 0 1 -0.4 0.2 -0.8 0.6 0 0 0 0 0 0 0 0 0 0 0
mod 2 1 10 0.75 10 0 0 0           # the TAPE's AGE +50%, 4 BARS

# FX & ATMOS: sounds for the spaces between.

new FX & ATMOS / Laser Rain
base ROM 2B / LASER GUN
fx 1 ping 0.1875 0.55 0.4 0.35     # 1/8 TRIP
fx 2 hall 0.8 0.65 0.4 0.3
mod 1 2 33 0.95 1 0 0 0            # a STEPPER on PAN, +90%, every step
modsteps 1 7 0 0 -1 0.8 -0.4 1 -0.9 0.3 0.6 0 0 0 0 0 0 0 0 0
mod 2 1 7 0.3 10 0 0 0             # PITCH ENV -40%, 4 BARS: the zaps shorten

new FX & ATMOS / Wind Organ
base ROM 1A / E.PIANO 1
set 134 4  135 7  136 0  143 0  145 51   # pairs 1 and 3 on (ops 1, 2, 5, 6)
op 1  40 30 30 35  99 95 92  0  39 0 0 0 0  1 0 0  95 0  1  0  7   # a pipe, slow in
op 2  35 30 30 35  99 90 85  0  39 0 0 0 0  1 0 0  60 0  2  0  7
op 3  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
op 4  99 99 99 99   0  0  0  0  39 0 0 0 0  0 0 0   0 0  1  0  7
op 5  30 30 30 35  99 95 92  0  39 0 0 0 0  1 0 0  88 0  1  0  7   # the wind: a carrier bent by noise
op 6  30 30 30 35  99 95 92  0  39 0 0 0 0  1 0 0  92 0  7  0  7   # FEEDBACK 7 at ratio 7: hiss
fx 1 vowel 0.6 0.5 0.75 0.7        # I-O, RESONANCE 75%
fx 2 hall 0.85 0.75 0.4 0.4
mod 1 1 8 0.8 12 6 0 0 0.12        # the VOWEL drifts, +60%, free
mod 2 1 32 0.4 12 6 0 0 0.2        # LEVEL drifts, -20%: gusts

new FX & ATMOS / Descent Space
base ROM 2B / DESCENT
fx 1 hall 0.9 0.8 0.4 0.35         # 13 S
fx 2 eq 0.45 0.5 0.5 0.55
mod 1 1 11 0.7 12 6 0 0 0.08       # the HALL's MIX drifts, +40%, free
mod 2 1 33 0.75 9 1 0 0            # PAN +50%, a TRIANGLE over 2 BARS

new FX & ATMOS / Backwards Garden
base ROM 2B / WATER GDN
fx 1 reverse 0.6875 0.4 0.5        # 1/4, half backwards
fx 2 hall 0.75 0.7 0.5 0.3
send 1 0.15
mod 1 1 9 0.75 10 0 0 0            # the REVERSE's FEEDBACK +50%, 4 BARS
mod 2 1 33 0.75 12 6 0 0 0.1       # PAN drifts, +50%, free

new FX & ATMOS / Train Dub
base ROM 1A / TRAIN
fx 1 filter 0.55 0.5 0.1 1         # LOW 12 at 1.1 KHZ, RESONANCE 50%
fx 2 dub 0.6875 0.5 0.4 0.4        # 1/4
fx 3 comp 0.3 0.4167 0.4 1         # -33 DB, 3:1: the far-off train brought near
mod 1 1 8 0.75 10 1 0 0            # the CUTOFF +50%, a TRIANGLE over 4 BARS
mod 2 1 13 0.7 11 0 0 0            # the DUB's FEEDBACK +40%, 8 BARS

new FX & ATMOS / Radio Ghost
base ROM 1A / E.PIANO 1
set 134 4  135 0  136 0  137 20  138 0  139 6  142 4  143 4  145 63   # three pairs, a little apart; a slow wobble
op 1  50 30 30 40  99 92 90  0  39 0 0 0 0  2 0 0  99 0  1  0  7
op 2  45 30 30 40  99 85 80  0  39 0 0 0 0  2 0 0  72 0  1  0  7
op 3  50 30 30 40  99 92 90  0  39 0 0 0 0  2 0 0  99 0  1  0 11
op 4  45 30 30 40  99 85 80  0  39 0 0 0 0  2 0 0  72 0  1  0 11
op 5  50 30 30 40  99 92 90  0  39 0 0 0 0  2 0 0  92 0  2  0  4   # an octave up
op 6  45 30 30 40  99 85 80  0  39 0 0 0 0  2 0 0  60 0  1  0  7
fx 1 ring 0.62 0.1667 0.6 0.5      # 180 HZ, SINE, wide, half
fx 2 wear 0.5 0.55 0.35 0.3        # WOW 50%, DIRT 55%, DROPS 35%
fx 3 comp 0.6 0.4167 0.3 1         # -19 DB, 3:1: a radio's squash
send 2 0.25
mod 1 1 8 0.7 12 6 0 0 0.15        # the RING's FREQ drifts, +40%, free: a tuning dial
mod 2 1 32 0.4 12 6 0 0 0.3        # LEVEL drifts, -20%: the signal fades

# MOVING: a MOD is most of the sound.

new MOVING / Bright Steps
base ROM 2B / SYN-CLAV 1
fx 1 ping 0.3125 0.4 0.5 0.25      # 1/8
mod 1 2 0 0.85 1 0 0 0             # a STEPPER of 16 on BRIGHT, +70%, every step
modsteps 1 16 0.1 0 1 -0.6 0.2 -1 0.7 -0.3 0 -0.8 0.9 -0.5 0.4 -1 0.6 0 -0.2 0.3
mod 2 2 1 0.25 3 0 0 0             # a STEPPER of 5 on FEEDBACK, -50%, 1/8: against it
modsteps 2 5 0 0 1 0 0.5 0 1 0 0 0 0 0 0 0 0 0 0 0

new MOVING / Gate Strings
base ROM 1A / STRINGS 1
fx 1 delay 0.4167 0.35 0.5 0.2     # 1/8 DOT
fx 2 chorus 0.3 0.5 0.375 0.5      # ENSEMBLE
mod 1 2 32 1 0 0 0 0               # a STEPPER on LEVEL, +100%, every half step: -1 is silence
modsteps 1 8 0 0 0 -1 0 0 -1 0 -1 0 0 0 0 0 0 0 0 0
mod 2 1 5 0.7 10 0 0 0             # DETUNE +40%, 4 BARS

new MOVING / Detune Drift
base ROM 1B / E.PIANO 2
fx 1 chorus 0.25 0.5 0.125 0.5
fx 2 tape 0.5625 0.35 0.45 0.2     # 1/8 DOT
mod 1 1 5 0.85 12 6 0 0 0.06       # DETUNE drifts, +70%, free: in and out of tune
mod 2 1 32 0.35 12 6 0 0 0.11      # LEVEL drifts, -30%

new MOVING / Decay Walk
base ROM 1A / MARIMBA
fx 1 ping 0.5625 0.4 0.5 0.25      # 1/8 DOT
fx 2 plate 0.1 0.45 0.55 0.2
mod 1 2 3 0.85 3 0 0 0             # a STEPPER of 8 on DECAY, +70%, 1/8: a note short, a note rings
modsteps 1 8 0 0 -1 1 -0.5 0.6 -1 0.2 1 -0.8 0 0 0 0 0 0 0 0
mod 2 1 0 0.65 9 1 0 0             # BRIGHT +30%, 2 BARS

new MOVING / Pitch Steps
base ROM 1A / E.PIANO 1
set 126 99  127 72  128 60  129 60  130 70  131 50  132 50  133 50   # PITCH ENV: from above, down at once
set 134 4  135 3  136 1  143 0
op 1  99 50 35 55  99 70  0  0  39 0 0 0 0  3 0 2  99 0  1  0  7
op 2  99 70 40 55  99 40  0  0  39 0 0 0 0  3 0 6  82 0  1  0  7
op 3  99 60 40 55  99 50  0  0  39 0 0 0 0  3 0 3  80 0  2  0  9
op 4  99 85  0 55  99  0  0  0  39 0 0 0 0  3 0 6  62 0  5  0  7
op 5  99 50 35 55  99 70  0  0  39 0 0 0 0  3 0 2  92 0  1  0  4
op 6  99 75  0 55  99  0  0  0  39 0 0 0 0  3 0 5  70 0  1  0  7   # FEEDBACK 3
fx 1 ping 0.4375 0.45 0.5 0.3      # 1/4 TRIP
fx 2 comp 0.35 0.4167 0.3 1
send 2 0.12
mod 1 2 7 0.9 3 0 0 0              # a STEPPER on PITCH ENV, +80%, 1/8: each note falls a different way
modsteps 1 6 0 0 1 -1 0.4 -0.6 0 0.8 0 0 0 0 0 0 0 0 0 0
mod 2 1 33 0.75 12 6 0 0 0.15      # PAN drifts, +50%, free

new MOVING / Feedback Tide
base PADS / Saw Drift
set 135 2
op 5  62 40 30 45  99 95 95  0  39 0 0 0 0  2 0 0  95 0  2  0  7   # the pair FEEDBACK tears, brought up
op 6  62 35 30 45  99 85 80  0  39 0 0 0 0  2 0 0  84 0  2  0  7
fx 1 filter 0.7 0.35 0.3 1         # LOW 24 at 3.2 KHZ, RESONANCE 35%
fx 2 ensemble 0.3 0.5 0.8 0.7
send 2 0.2
mod 1 1 1 0.85 10 1 0 0            # FEEDBACK +70%, a TRIANGLE over 4 BARS: smooth to torn
mod 2 1 8 0.7 12 6 0 0 0.09        # the CUTOFF drifts, +40%, free
)presets"
