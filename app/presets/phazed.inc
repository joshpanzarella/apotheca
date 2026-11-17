R"presets(
# PHAZED's built-in presets past the engine's own, and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/phazed.hpp.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# ---- More presets (2026-11-17, presets-plan.md, "More presets"): every one a whole sound ------------
# Each starts from one of the engine's 65 (`base NAME`) and bends it with the macros and a few
# settings, then gives it its effects, sends and MODs. Ids: 1 OCTAVE (-1..1), 8 VIBRATO DELAY,
# 9 VIBRATO RATE, 10 VIBRATO DEPTH, 11 CHORUS (0 off 1 on), 12 LEVEL (-24..6 dB).
# Macros: 0 TONE 1 MOTION 2 ATTACK 3 RELEASE 4 DETUNE 5 VIBRATO 6 CHORUS (a choice: under 0.4 off,
# over 0.6 on) 7 OCTAVE (a choice). A MOD may take 0 to 5 (never DETUNE through its middle on a
# one-line sound: the CZ cuts a held note there), an effect's amount, a send, LEVEL or PAN.
# Every one is played by `presets_test --heard-phazed` (a bar of keys, or a bass line for DEEP
# BASS): loudness, peak, a tail that ends, every MOD moving and heard. Nobody has heard any of them.

# STAGE KEYS: the engine's keys, dressed for a room.
new STAGE KEYS / Tine Room
base Electric Piano
macros 0.55 0.5 0.5 0.6 0.5 0.5 0.5 0.5
fx 1 tremolo 0.45 0.35 0.125 0.6      # RATE 1/8, DEPTH, SINE, SPREAD: the old amp's pan
fx 2 plate 0.2 0.5 0.5 0.25           # PRE, DECAY, TONE, MIX
new STAGE KEYS / Dusty Keys
base Digital Piano
macros 0.4 0.5 0.5 0.55 0.5 0.5 0.5 0.5
fx 1 wear 0.35 0.45 0.2 0.35          # WOW, DIRT, DROPS, TONE: a worn tape of it
send 2 0.25
new STAGE KEYS / Phase Keys
base Wurly
macros 0.6 0.5 0.5 0.5 0.5 0.5 0.5 0.5
fx 1 phaser 0.3 0.6 0.4 0.5           # RATE, DEPTH, FEEDBACK, MIX
fx 2 reverb 0.4 0.5 0.2               # SIZE, DAMPING, MIX
new STAGE KEYS / Clav Wah
base Pulse Clav
fx 1 vowel 0.5 0.5 0.6 0.8            # VOWEL, SHIFT, RESONANCE, MIX
mod 1 1 8 0.85 5 1 0 0                # MOD 1: an LFO on the VOWEL, +70%, 1/4, TRIANGLE: the pedal
new STAGE KEYS / Bell Piano
base Rhodes Bell
macros 0.6 0.5 0.5 0.7 0.5 0.5 0.5 0.5
fx 1 chorus 0.25 0.45 0.125 0.4
send 1 0.2
send 2 0.2
new STAGE KEYS / Toy Room
base Toy Piano
macros 0.45 0.5 0.5 0.55 0.5 0.5 0.5 0.5
fx 1 crush 0.6 0.7 0.35               # BITS, RATE, MIX: a little grit
fx 2 ping 0.3125 0.3 0.5 0.2          # TIME 1/8, FEEDBACK, TONE, MIX
mod 1 1 0 0.65 8 5 0 0                # TONE, RANDOM each bar, +30%

# DEEP BASS: the engine's basses, low and heavy, each with its own colour.
new DEEP BASS / Round Sub
base Sub Bass
set 12 2
macros 0.45 0.5 0.5 0.5 0.5 0.5 0.5 0.5
fx 1 comp 0.45 0.4167 0.4 1           # THRESHOLD, RATIO, SPEED, MIX: held level
new DEEP BASS / Grit Bass
base Saw Bass
macros 0.55 0.5 0.5 0.5 0.5 0.5 0.5 0.5
fx 1 saturate 0.5 0.375 0.55 0.8      # DRIVE, TUBE, TONE, MIX
new DEEP BASS / Rubber Bass
base Reso Bass
set 12 6
mod 1 1 0 0.75 5 0 0 0                # TONE, +50%, 1/4, SINE: a wobble
new DEEP BASS / Fold Bass
base Hollow Bass
fx 1 fold 0.45 0.5 0.45 0.6           # DRIVE, SYMMETRY, TONE, MIX
mod 1 1 8 0.7 8 1 0 0                 # FOLD's DRIVE, +40%, a bar, TRIANGLE
new DEEP BASS / Dub Bass
base Finger Bass
macros 0.35 0.5 0.5 0.6 0.5 0.5 0.5 0.5
fx 1 filter 0.45 0.3 0.1 1            # CUTOFF, RESONANCE, LOW 12, MIX
send 1 0.2
new DEEP BASS / Step Bass
base Acid Reso
set 12 6
mod 1 2 0 0.85 1 0 0 0                # a STEPPER on TONE, +70%, a step each
modsteps 1 16 0.2 0 -0.8 0.4 -0.2 1 -0.6 0.1 0.7 -1 -0.4 0.9 0 -0.7 0.5 -0.3 0.8 0.2

# LEADS: single lines that sing, with an echo or a fuzz.
new LEADS / Tape Lead
base Saw Lead
set 8 30  9 55  10 12
fx 1 tape 0.5625 0.35 0.3 0.25        # TIME 1/8 DOT, FEEDBACK, AGE, MIX
new LEADS / Fuzz Lead
base Square Lead
fx 1 fuzz 0.45 0.55 0.1 0.8           # FUZZ, TONE, GATE, MIX
fx 2 ping 0.6875 0.3 0.45 0.2         # TIME 1/4
new LEADS / Sync Sweep
base Sync Lead
mod 1 1 0 0.75 9 0 0 0                # TONE, +50%, 2 BARS, SINE
send 1 0.25
new LEADS / Night Flute
base Flute
set 8 20  9 50  10 18
fx 1 hall 0.6 0.55 0.45 0.3           # SIZE, DECAY, TONE, MIX
new LEADS / Chip Hop
base Chip Lead
fx 1 crush 0.45 0.5 0.6
mod 1 2 0 0.8 1 4 0 0                 # a STEPPER on TONE, +60%, a step each
modsteps 1 8 0 0 1 -1 0.5 -0.5 1 0 -1 0 0 0 0 0 0 0 0
new LEADS / Wide Saw
base Saw
macros 0.55 0.5 0.5 0.5 0.62 0.5 0.5 0.5  # DETUNE up: a second line, a little sharp
fx 1 ensemble 0.35 0.5 0.8 0.45       # RATE, DEPTH, SPREAD, MIX

# PADS: chords held, slow and wide.
new PADS / Velvet Pad
base Warm Pad
set 12 4
macros 0.5 0.5 0.5 0.7 0.5 0.5 0.5 0.5
fx 1 ensemble 0.3 0.6 0.9 0.6
send 2 0.3
new PADS / Glass Bloom
base Glass Pad
macros 0.55 0.5 0.5 0.65 0.5 0.5 0.5 0.5
fx 1 bloom 0.5 0.6 0.55 0.35          # BLOOM, DECAY, TONE, MIX
new PADS / Choir Hall
base Choir
macros 0.5 0.5 0.6 0.65 0.5 0.55 0.5 0.5
fx 1 hall 0.75 0.6 0.4 0.35
new PADS / Slow Sweep
base Sweep Pad
set 12 2
mod 1 1 0 0.8 10 0 0 0                # TONE, +60%, 4 BARS, SINE
send 2 0.3
new PADS / Shimmer Pad
base 80s Pad
macros 0.5 0.5 0.5 0.6 0.5 0.5 0.5 0.5
fx 1 shimmer 0.5 0.6 0.55 0.3         # SHIMMER, DECAY, TONE, MIX
new PADS / Dark Drift
base Dark Strings
macros 0.45 0.5 0.5 0.6 0.5 0.5 0.5 0.5
fx 1 chorus 0.2 0.6 0.375 0.5
mod 1 1 0 0.75 11 6 0 0               # TONE, +50%, 8 BARS, DRIFT

# PLUCKS: short notes with somewhere to go.
new PLUCKS / Ping Pluck
base Saw Pluck
fx 1 ping 0.3125 0.45 0.5 0.3
new PLUCKS / Harp Space
base Harp
macros 0.55 0.5 0.5 0.6 0.5 0.5 0.5 0.5
send 2 0.45
new PLUCKS / Pizz Tape
base Pizzicato
fx 1 tape 0.6875 0.3 0.45 0.3
new PLUCKS / Kalimba Dub
base Kalimba
fx 1 dub 0.5625 0.45 0.45 0.3         # TIME 1/8 DOT, FEEDBACK, TONE, MIX
new PLUCKS / Comb Pluck
base Harpsichord
fx 1 comb 0.55 0.8 0.5 0.45          # PITCH, FEEDBACK, TONE, MIX
mod 1 1 9 0.7 9 1 0 0                 # COMB's FEEDBACK, +40%, 2 BARS, TRIANGLE
new PLUCKS / Step Pluck
base Saw Pluck
macros 0.5 0.5 0.5 0.4 0.5 0.5 0.5 0.5
mod 1 2 0 0.85 3 0 0 0                # a STEPPER on TONE, +70%, an eighth each
modsteps 1 8 0.3 0 -1 0.5 0 1 -0.5 0.8 -0.8 0.2 0 0 0 0 0 0 0 0
send 1 0.25

# BELLS: struck, ringing on.
new BELLS / Bell Hall
base Ring Bell
fx 1 hall 0.7 0.6 0.5 0.3
new BELLS / Tubular Shimmer
base Tubular Bell
fx 1 shimmer 0.45 0.55 0.6 0.25
new BELLS / Glock Echo
base Glockenspiel
fx 1 ping 0.4375 0.4 0.55 0.3         # TIME 1/4 TRIP
new BELLS / Vibe Tremolo
base Vibes
fx 1 tremolo 0.45 0.45 0.125 0.2
send 2 0.25
new BELLS / Wood Room
base Marimba
fx 1 reverb 0.3 0.6 0.3
mod 1 1 0 0.65 7 5 0 0                # TONE, +30%, 1/2, RANDOM: the mallet hardens and softens
new BELLS / Steel Pan Space
base Steel Drum
fx 1 chorus 0.3 0.4 0.125 0.35
fx 2 plate 0.3 0.6 0.55 0.3

# DRONES: a held note that moves on its own.
new DRONES / Organ Drone
base Organ
macros 0.5 0.5 0.6 0.7 0.5 0.5 0.5 0.5
fx 1 hall 0.8 0.65 0.4 0.35
mod 1 1 0 0.75 11 0 0 0               # TONE, +50%, 8 BARS, SINE
new DRONES / Reed Drone
base Reed Organ
fx 1 flanger 0.15 0.6 0.4 0.5         # RATE, DEPTH, FEEDBACK, MIX
new DRONES / Ring Drone
base Ring Sweep
fx 1 plate 0.3 0.7 0.4 0.35
new DRONES / Bowed Drone
base Bowed Glass
macros 0.5 0.5 0.5 0.65 0.5 0.5 0.5 0.5
fx 1 bloom 0.6 0.65 0.5 0.4
new DRONES / Wind Drone
base Wind
set 12 6
fx 1 hall 0.7 0.6 0.45 0.4
mod 1 1 0 0.75 10 6 0 0               # TONE, +50%, 4 BARS, DRIFT: the gusts
new DRONES / Sine Drone
base Sine Chorus
macros 0.5 0.5 0.6 0.65 0.5 0.5 0.5 0.5
fx 1 chorus 0.15 0.6 0.375 0.5
mod 1 1 5 0.8 10 1 0 0                # VIBRATO, +60%, 4 BARS, TRIANGLE

# TEXTURES: hits and noises with an effect that turns them into something else.
new TEXTURES / Laser Echo
base Laser
fx 1 ping 0.4375 0.55 0.5 0.35
new TEXTURES / Noise Wash
base Noise Hit
fx 1 reverse 0.6875 0.4 0.5           # TIME 1/4, FEEDBACK, MIX
new TEXTURES / Zap Steps
base Pitch Zap
mod 1 2 0 0.85 1 0 0 0                # a STEPPER on TONE, +70%, a step each
modsteps 1 16 0 0 1 -1 0.5 -0.5 0.8 -0.8 0 1 -1 0.3 -0.3 0.6 -0.6 1 -1
fx 1 stutter 0.4375 0.5 0.3 0.4       # LENGTH, REPEATS, CHANCE, MIX
new TEXTURES / Reso Swirl
base Reso Sweep
fx 1 phaser 0.2 0.75 0.6 0.6
new TEXTURES / Glass Grit
base Glass Pad
fx 1 crush 0.4 0.4 0.5
mod 1 1 9 0.75 9 0 0 0                # CRUSH's RATE, +50%, 2 BARS, SINE
new TEXTURES / Broken Organ
base Perc Organ
fx 1 wear 0.6 0.5 0.5 0.4
fx 2 tremolo 0.25 0.3 0.625 0.3       # RATE 1/16, DEPTH, RAMP, SPREAD
)presets"
