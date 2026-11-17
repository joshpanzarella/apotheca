R"presets(
# PHYSIC's built-in presets past its own table, and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/physic_rule.hpp.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.

new SHIMMER / Glass Steps
base Church Bell                # (BELLS), by its name: a place moves, a name does not. Then:
set 3 35  4 70                  # POSITION 35, DECAY 70
fx 1 shimmer 0.6 0.7 0.6 0.4    # SHIMMER, DECAY, TONE, MIX
send 1 0.2                      # into ECHO
mod 1 2 1 0.7 5 0 0 0           # MOD 1: a STEPPER on macro 2 (POSITION), +40%, 1/4
modsteps 1 8 0.2 0 0.5 -0.3 0.8 0 -0.6 0.2 0.9 -0.1 0 0 0 0 0 0 0 0
# ---- The second set (2026-11-16): character and movement, not textbook instruments --------------
# Every one below plays the phrase of `presets_test --heard` inside its limits (loudness, no clip,
# a tail that ends, its MODs moving). Its figures and the idea of each: physic-plan.md, section 17.
# Ids: 0 MODEL (0 STRING 1 MARIMBA 2 XYLO 3 VIBES 4 GLOCK 5 KALIMBA 6 BELL 7 CHIME 8 GLASS 9 DRUM
# 10 WOOD), 1 STRIKE (0 PLUCK 1 MALLET 2 NOISE), 2 HARDNESS, 3 POSITION, 4 DECAY, 5 MATERIAL,
# 6 STRETCH, 7 RELEASE (100 RING), 8 VELOCITY, 9 OCTAVE, 10 FINE, 11 WIDTH, 12 LEVEL, 13 DICE,
# 14-17 DICE POSITION, HARDNESS, TUNE, DECAY, 18 PAIR, 19 MOTOR, 20 BODY (1 GUITAR 2 BOX 3 TUBES),
# 21 BODY MIX, 22 BUZZ. DECAY 60 is 2.3 s, 70 4.4 s, 80 8.4 s, 90 16 s. MOD targets: macros 0 TONE
# 1 POSITION 2 DECAY 3 MATERIAL 4 STRETCH 5 RELEASE 6 DICE; slot 1's settings 8-11, slot 2's 12-15,
# slot 3's 16-19; 20 ECHO, 21 SPACE; 32 LEVEL, 33 PAN.

# ATMOS: long rings that swell, beat and drift.

new ATMOS / Bowed Glass
base Wine Glass
set 1 2  2 12  4 82  7 88  18 4  12 2   # a 0.9 s breath of noise draws the glass, two beating
fx 1 shimmer 0.45 0.75 0.55 0.45       # SHIMMER 45%, DECAY 7 S
send 2 0.2                              # into SPACE
mod 1 1 8 0.75 12 0 0 0 0.06            # SHIMMER's amount swells, +50%, free at 0.06 Hz
mod 2 1 33 0.8 12 6 0 0 0.15            # PAN drifts, +60%

new ATMOS / Singing Bowl
base Wine Glass
set 1 1  2 18  3 40  4 92  6 58  7 100  18 6  19 8  12 2  # a padded stick; a slow swell (MOTOR 0.3 Hz)
fx 1 plate 0.2 0.72 0.4 0.3
send 2 0.25
mod 1 1 32 0.3 12 0 0 0 0.05            # LEVEL breathes, -40%, free at 0.05 Hz
mod 2 1 4 0.58 12 6 0 0 0.11            # STRETCH wanders: the partials bend while they ring

new ATMOS / Tine Pad
base Vibes Still
set 2 30  4 88  5 96  7 92  18 3  19 28  20 3  21 40  12 1
fx 1 ensemble 0.22 0.6 0.8 0.6
fx 2 hall 0.8 0.7 0.42 0.35
send 1 0.15
mod 1 1 15 0.7 10 0 0 0                 # HALL's MIX, +40%, 4 BARS
mod 2 1 0 0.3 11 1 0 0                  # TONE, -40%, 8 BARS: soft to hard over the phrase

new ATMOS / Bloom Cluster
base Handbell
set 2 50  3 38  4 84  7 100  9 1  13 60  14 40  16 70  18 5  12 1
fx 1 bloom 0.75 0.78 0.5 0.45           # a 1.3 s swell, 9 S
mod 1 1 10 0.75 12 6 0 0 0.1            # BLOOM's TONE drifts, +50%
mod 2 1 6 0.75 9 1 0 0                  # DICE comes and goes, 2 BARS

new ATMOS / Breath Choir
base Tubular Bell
set 1 2  2 8  3 30  4 70  7 85  18 5  12 2
fx 1 vowel 0.25 0.5 0.6 0.75            # E
fx 2 hall 0.75 0.6 0.4 0.3
mod 1 1 8 0.85 9 1 0 0                  # the VOWEL sweeps A to O and back, 2 BARS
mod 2 1 33 0.7 12 6 0 0 0.2             # PAN drifts

new ATMOS / Under Ice
base Crystal
set 2 95  4 74  6 82  13 50  16 40  12 -2
fx 1 reverse 0.6875 0.35 0.45           # each sound again backwards, 1/4
fx 2 plate 0.15 0.8 0.35 0.35
mod 1 1 4 0.7 10 0 0 0                  # STRETCH, +40%, 4 BARS: the ice bends
mod 2 1 14 0.7 12 6 0 0 0.08            # PLATE's TONE drifts

new ATMOS / Ghost Harp
base Harp
set 2 35  3 30  4 76  5 70  18 4  12 -3
fx 1 ping 0.5625 0.45 0.45 0.3          # 1/8 DOT
fx 2 hall 0.9 0.75 0.4 0.4
send 2 0.15
mod 1 1 1 0.75 9 1 0 0                  # POSITION, +50%, 2 BARS
mod 2 1 15 0.65 12 0 0 0 0.07           # HALL's MIX swells

new ATMOS / Glass Harmonica
base Wine Glass
set 1 2  2 22  4 72  7 75  18 2  19 14  12 2   # a long breath, a slow turn (0.33 Hz)
fx 1 chorus 0.2 0.45 0.125 0.5
fx 2 plate 0.25 0.65 0.45 0.3
mod 1 1 3 0.3 11 0 0 0                  # MATERIAL, -40%, 8 BARS: the high glass dulls and clears

# METAL: struck, bent and dirtied steel and bronze.

new METAL / Gong Wash
base Church Bell
set 2 30  4 92  6 75  9 -1  18 12  12 2
fx 1 saturate 0.3 0.125 0.6 0.5         # TAPE
fx 2 hall 0.85 0.75 0.45 0.35
mod 1 1 4 0.62 12 0 0 0 0.07            # STRETCH, +24%, free: the wash shimmers
mod 2 1 21 0.75 10 0 0 0                # the SPACE send, +50%, 4 BARS

new METAL / Comb Clang
base Glockenspiel
set 2 92  4 45  13 60  14 60  15 40  12 -2
fx 1 comb 0.5 0.88 0.6 0.55             # C3, +76%
fx 2 ping 0.3125 0.4 0.55 0.25          # 1/8
mod 1 2 9 0.85 3 0 0 0                  # a STEPPER on COMB's FEEDBACK, 1/8
modsteps 1 6 0.1 0 0.4 -0.5 0.9 -0.2 0.1 -0.8 0 0 0 0 0 0 0 0 0 0

new METAL / Anvil
base Church Bell
set 2 100  3 10  4 68  6 92  7 70  12 1
fx 1 saturate 0.45 0.875 0.6 0.4        # HARD
fx 2 plate 0.05 0.35 0.6 0.25
mod 1 1 0 0.3 8 4 0 0                   # TONE, -40%, a SQUARE a bar: a hammer and a file

new METAL / Steel Pan
base Vibes Still
set 2 62  3 70  4 62  5 85  6 32  18 3  20 3  21 35  12 0
fx 1 chorus 0.3 0.3 0.125 0.35
fx 2 spring 0.4 0.5 0.55 0.25
mod 1 1 1 0.7 8 1 0 0                   # POSITION, +40%, 1 BAR

new METAL / Robot Tines
base Kalimba
set 22 0  4 66  12 0
fx 1 ring 0.75 0.1667 0.3 0.5           # 503 HZ, SINE
fx 2 ping 0.4375 0.35 0.5 0.25          # 1/4 TRIP
mod 1 1 8 0.65 8 1 0 0                  # RING's FREQ, +30%, 1 BAR

new METAL / Rusty Spring
base Steel
set 1 1  2 80  4 70  5 90  6 85  22 30  12 -6
fx 1 spring 0.9 0.7 0.5 0.45
mod 1 1 4 0.65 9 0 0 0                  # STRETCH (how stiff), +30%, 2 BARS

new METAL / Cymbal Swell
base Church Bell
set 1 2  2 0  4 62  6 96  9 1  18 20  7 90  12 3   # a 2 s burst: it swells in
fx 1 flanger 0.15 0.6 0.7 0.5
mod 1 1 10 0.3 10 1 0 0                 # FLANGER's FEEDBACK, -40%, 4 BARS

new METAL / Fold Bell
base Handbell
set 2 70  4 76  18 4  12 -4
fx 1 saturate 0.45 0.625 0.5 0.45      # SATURATE's FOLD (FOLD itself costs three times as much)
send 2 0.3                              # the song's HALL, not one of its own
mod 1 1 8 0.7 9 0 0 0                   # the DRIVE, +40%, 2 BARS: the folds come and go

# GAMELAN: bronze that beats (ombak: two of each, a few cents apart).

new GAMELAN / Gender
base Vibes Still
set 2 50  4 78  5 95  6 64  7 75  18 8  20 3  21 50  12 0
fx 1 plate 0.1 0.55 0.45 0.25
send 2 0.2
mod 1 1 1 0.7 9 1 0 0                   # POSITION, +40%, 2 BARS

new GAMELAN / Bonang
base Handbell
set 2 65  3 25  4 68  6 40  18 6  12 0
fx 1 hall 0.45 0.45 0.5 0.2
mod 1 1 0 0.7 8 5 0 0                   # TONE, +40%, RANDOM a bar: each pot struck its own way

new GAMELAN / Saron
base Glockenspiel
set 2 85  4 72  5 85  9 0  18 7  12 0
fx 1 eq 0.6 0.4 0.55 0.4                # a little low, a little less 1 kHz and top
fx 2 plate 0.1 0.5 0.45 0.2
mod 1 2 1 0.75 3 0 0 0                  # a STEPPER on POSITION, 1/8
modsteps 1 5 0 0 0.6 -0.4 0.2 -0.8 0.9 0 0 0 0 0 0 0 0 0 0 0

new GAMELAN / Gong Ageng
base Church Bell
set 2 15  3 45  4 98  6 30  9 -2  18 3  12 3
fx 1 hall 0.9 0.8 0.35 0.35
send 2 0.25
mod 1 1 2 0.6 11 0 0 0                  # DECAY, +20%, 8 BARS

new GAMELAN / Kempul
base Church Bell
set 2 30  3 40  4 88  6 35  9 -1  18 5  12 2
fx 1 plate 0.2 0.65 0.4 0.25
mod 1 1 33 0.7 12 0 0 0 0.1             # PAN swings slowly, free

new GAMELAN / Ombak
base Vibes Still
set 2 45  4 84  5 96  6 60  7 85  18 14  19 10  12 -1   # PAIR 14: a fast beat
fx 1 ensemble 0.3 0.5 0.9 0.5
fx 2 hall 0.7 0.6 0.45 0.3
mod 1 1 2 0.7 10 1 0 0                  # DECAY, +40%, 4 BARS

new GAMELAN / Kenong
base Church Bell
set 2 50  3 50  4 80  6 45  18 4  12 2
fx 1 dub 0.6875 0.45 0.35 0.25          # 1/4, dark
mod 1 1 10 0.75 9 0 0 0                 # DUB's TONE, +50%, 2 BARS

new GAMELAN / Slenthem Dub
base Vibes Still
set 2 30  4 80  5 92  6 62  9 -1  18 9  12 2
fx 1 dub 0.5625 0.5 0.4 0.35            # 1/8 DOT
fx 2 hall 0.6 0.5 0.4 0.2
mod 1 1 9 0.65 10 0 0 0                 # DUB's FEEDBACK, +30%, 4 BARS

# PREPARED: strings with things on and in them.

new PREPARED / Bolt Piano
base Dulcimer
set 2 70  3 12  4 68  5 60  6 30  18 2  20 2  21 40  22 40  12 -6
fx 1 plate 0.1 0.5 0.45 0.2
mod 1 1 1 0.7 8 5 0 0                   # POSITION, +40%, RANDOM a bar

new PREPARED / Felt Thuds
base Dulcimer
set 2 10  3 50  4 35  5 20  6 20  18 0  20 2  21 70  12 -3
fx 1 comp 0.45 0.4167 0.5 1             # 3:1
fx 2 eq 0.75 0.45 0.3 0.3               # +6 DB low, the top down
mod 1 1 2 0.7 9 1 0 0                   # DECAY, +40%, 2 BARS: thud to ring

new PREPARED / Paper Strings
base Steel
set 2 50  4 60  5 55  22 70  12 -3
fx 1 eq 0.55 0.6 0.75 0.4
mod 1 1 0 0.7 8 1 0 0                   # TONE, +40%, 1 BAR

new PREPARED / Screw Bells
base Handbell
set 2 80  4 55  6 85  22 25  12 0
fx 1 spring 0.5 0.5 0.5 0.3
mod 1 2 4 0.75 3 0 0 0                  # a STEPPER on STRETCH, 1/8: each screw sits differently
modsteps 1 7 0 0 0 0.5 -0.6 0.9 -0.2 0.3 -0.9 0 0 0 0 0 0 0 0 0

new PREPARED / Rubber Mute
base Nylon
set 2 30  3 45  4 30  5 10  21 85  12 -1
fx 1 snap 0.75 0.4 1                    # ATTACK +50%, SUSTAIN -20%
mod 1 1 3 0.75 8 0 0 0                  # MATERIAL, +50%, 1 BAR

new PREPARED / Tack Piano
base Dulcimer
set 2 95  3 15  4 66  6 25  18 9  22 10  20 2  21 35  12 -2
fx 1 saturate 0.3 0.375 0.6 0.5         # TUBE
fx 2 tape 0.4375 0.3 0.45 0.2           # 1/4 TRIP
mod 1 1 14 0.7 9 0 0 0                  # TAPE's AGE, +40%, 2 BARS

new PREPARED / Clip Harp
base Harp
set 2 60  4 66  18 3  22 50  12 -3
fx 1 delay 0.0833 0.2 0.5 0.3           # a 1/16 slap
fx 2 plate 0.05 0.45 0.5 0.2
mod 1 1 1 0.3 9 0 0 0                   # POSITION, -40%, 2 BARS

# PERCUSSION: skins, wood and clay.

new PERCUSSION / Brush Snare
base Tom
set 1 2  2 40  3 30  4 30  5 40  9 0  22 70  13 40  12 6
fx 1 comp 0.55 0.5833 0.4 1             # 4:1
fx 2 plate 0.05 0.3 0.6 0.25
mod 1 1 0 0.7 8 5 0 0                   # TONE (the brush's length), +40%, RANDOM a bar

new PERCUSSION / Hand Pan
base Tom
set 2 35  3 30  4 72  5 100  6 0  9 0  20 3  21 60  13 15  12 0   # STRETCH 0: the skin's modes go to whole numbers, tuned
fx 1 plate 0.15 0.55 0.45 0.25
send 2 0.2
mod 1 1 1 0.75 9 1 0 0                  # POSITION, +50%, 2 BARS

new PERCUSSION / Udu
base Tom
set 2 30  3 35  4 42  5 30  6 30  9 -1  20 3  21 85  12 3
fx 1 eq 0.7 0.55 0.2 0.4
mod 1 1 1 0.3 8 2 0 0                   # POSITION, -40%, a RAMP UP a bar

new PERCUSSION / Rattle
base Woodblock
set 1 2  2 75  4 28  13 80  14 80  15 60  22 60  12 5
fx 1 eq 0.4 0.6 0.8 0.6
mod 1 1 33 0.8 3 5 0 0                  # PAN, +60%, RANDOM each 1/8

new PERCUSSION / Bent Skin
base Tom
set 2 50  4 55  5 70  6 50  12 2
fx 1 spring 0.6 0.4 0.5 0.2
mod 1 2 4 0.85 3 0 0 0                  # a STEPPER on STRETCH, 1/8: the skin's tension talks
modsteps 1 8 0.3 0 0.8 -0.6 0 0.5 -0.9 0.2 -0.3 0.9 0 0 0 0 0 0 0 0

new PERCUSSION / Log Steps
base Log Drum
set 2 55  13 40  14 70  12 0
fx 1 ping 0.3125 0.3 0.5 0.25           # 1/8
mod 1 2 1 0.85 1 0 0 0                  # a STEPPER on POSITION, every step
modsteps 1 16 0 0 0.9 -0.5 0.2 -0.9 0.6 0 -0.3 0.7 -0.7 0.4 -0.1 0.8 -0.6 0.3 -0.4 0.1
mod 2 2 20 1 1 0 0 0                    # a STEPPER on the ECHO send, every step: a throw now and then
modsteps 2 7 0 0 0 0 0.7 0 0 0 1 0 0 0 0 0 0 0 0 0

new PERCUSSION / Frame Drum
base Hand Drum
set 3 25  4 45  9 -1  20 2  21 60  12 6
fx 1 spring 0.5 0.45 0.45 0.2
mod 1 1 1 0.7 8 1 0 0                   # POSITION, +40%, 1 BAR: rim to middle

new PERCUSSION / Tabla
base Tom
set 2 70  3 20  4 58  5 70  6 10  9 0  13 30  14 50  12 2   # STRETCH 10: near whole, as a loaded skin
fx 1 eq 0.55 0.6 0.55 0.5
mod 1 2 1 0.85 3 0 0 0                  # a STEPPER on POSITION, 1/8: centre and edge strokes
modsteps 1 6 0 0 0.8 -0.8 0.8 0.3 -0.8 0.5 0 0 0 0 0 0 0 0 0 0

# PLUCKS: strings and tines with an effect that makes them.

new PLUCKS / Twelve Ensemble
base Twelve
set 18 9  12 2
fx 1 ensemble 0.35 0.55 0.85 0.55
fx 2 plate 0.15 0.5 0.5 0.25
mod 1 1 0 0.7 10 0 0 0                  # TONE, +40%, 4 BARS

new PLUCKS / Dub Kalimba
base Kalimba
set 22 35  4 66  12 0
fx 1 dub 0.5625 0.55 0.4 0.35           # 1/8 DOT
mod 1 1 10 0.7 9 0 0 0                  # DUB's TONE, +40%, 2 BARS
send 2 0.15

new PLUCKS / Jawari
base Steel
set 2 80  3 8  4 78  5 75  6 10  7 90  18 4  22 55  12 -3   # the bridge's buzz on a long pluck
fx 1 phaser 0.1 0.4 0.55 0.3
fx 2 plate 0.1 0.6 0.4 0.25
mod 1 1 1 0.7 10 1 0 0                  # POSITION, +40%, 4 BARS

new PLUCKS / Harp Pings
base Harp
set 4 70  12 -3
fx 1 ping 0.5625 0.5 0.5 0.35           # 1/8 DOT
mod 1 2 9 0.85 3 0 0 0                  # a STEPPER on PING's FEEDBACK, 1/8
modsteps 1 5 0.2 0 -0.6 0.6 -0.2 0.9 -0.9 0 0 0 0 0 0 0 0 0 0 0

new PLUCKS / Koto Rain
base Koto
set 13 40  16 50  12 0
fx 1 reverse 0.5625 0.35 0.4            # 1/8 DOT, backwards
fx 2 hall 0.6 0.55 0.5 0.25
send 1 0.2
mod 1 1 1 0.75 9 0 0 0                  # POSITION, +50%, 2 BARS

new PLUCKS / Tin Banjo
base Steel
set 2 90  3 10  4 55  6 10  20 2  21 85  12 -4
fx 1 eq 0.4 0.65 0.7 0.6
mod 1 1 0 0.3 8 4 0 0                   # TONE, -40%, a SQUARE a bar

new PLUCKS / Pizz Flange
base Pizz
set 12 1
fx 1 flanger 0.3 0.7 0.75 0.6
fx 2 plate 0.05 0.4 0.5 0.2
mod 1 1 9 0.7 10 0 0 0                  # FLANGER's DEPTH, +40%, 4 BARS

new PLUCKS / Nylon Bloom
base Nylon
set 4 62  18 3  12 0                    # PAIR 3 does the chorus's work (BLOOM is the costly one: alone)
fx 1 bloom 0.6 0.6 0.5 0.3
mod 1 1 11 0.7 10 0 0 0                 # BLOOM's MIX, +40%, 4 BARS

# TEXTURES: worn, broken and backwards.

new TEXTURES / Worn Music Box
base Music Box
set 4 66  12 0
fx 1 wear 0.55 0.45 0.3 0.35
fx 2 tape 0.5625 0.35 0.6 0.25          # 1/8 DOT
send 2 0.2
mod 1 1 8 0.75 12 6 0 0 0.12            # WEAR's WOW drifts, +50%

new TEXTURES / Crushed Glock
base Glockenspiel
set 4 66  13 30  12 -2
fx 1 crush 0.6 0.5 0.6                  # 8 BITS, 1 IN 6
fx 2 ping 0.3125 0.35 0.5 0.25          # 1/8
mod 1 2 8 0.8 3 0 0 0                   # a STEPPER on the BITS, 1/8
modsteps 1 4 0 0 -0.8 0.6 -0.3 0.9 0 0 0 0 0 0 0 0 0 0 0 0

new TEXTURES / Broken Kalimba
base Kalimba
set 22 45  12 -2
fx 1 fuzz 0.5 0.3 0.3 0.45
fx 2 spring 0.6 0.55 0.5 0.3
mod 1 1 11 0.3 9 0 0 0                  # FUZZ's MIX, -40%, 2 BARS

new TEXTURES / Backwards Chimes
base Wind Chime
set 4 80  12 0
fx 1 reverse 0.6875 0.45 0.5            # 1/4
fx 2 hall 0.75 0.6 0.4 0.3
mod 1 1 10 0.3 10 0 0 0                 # REVERSE's MIX, -40%, 4 BARS

new TEXTURES / Static Bells
base Handbell
set 4 72  12 0
fx 1 wear 0.2 0.8 0.5 0.3
fx 2 vowel 0.75 0.5 0.4 0.4             # O
mod 1 1 12 0.7 9 1 0 0                  # the VOWEL, +40%, 2 BARS

new TEXTURES / Radio Glass
base Wine Glass
set 4 70  12 0
fx 1 ring 0.6 0.1667 0.5 0.35           # 145 HZ
fx 2 eq 0.2 0.8 0.55 0.3                # the low and top down, the middle up: a small speaker
mod 1 1 8 0.6 12 6 0 0 0.25             # RING's FREQ drifts, +20%

new TEXTURES / Ice Stutter
base Crystal
set 4 68  13 30  12 0
fx 1 stutter 0.5625 0.3 0.5 1           # 1/8, 3 TIMES, half the time
fx 2 plate 0.1 0.55 0.5 0.25
mod 1 1 10 0.7 10 0 0 0                 # STUTTER's CHANCE, +40%, 4 BARS

new TEXTURES / Rain On Tin
base Rain
set 22 40  4 55  12 4
fx 1 tape 0.3125 0.35 0.55 0.25         # 1/8
fx 2 eq 0.3 0.55 0.7 0.55
mod 1 1 6 0.75 12 6 0 0 0.2             # DICE drifts, +50%

# MOVING: a MOD is most of the sound.

new MOVING / Timbre Walk
base Marimba
set 4 64  12 0
fx 1 ping 0.4375 0.3 0.5 0.2            # 1/4 TRIP
send 2 0.15
mod 1 2 1 0.95 1 0 0 0                  # a STEPPER on POSITION, every step: a tune of timbres
modsteps 1 16 0 0 -0.9 0.5 -0.2 0.9 -0.6 0.1 0.7 -0.4 0.3 -0.8 0.8 -0.1 0.4 -0.5 0.6 0
mod 2 2 0 0.8 3 0 0 0                   # a STEPPER of five on TONE, 1/8, against it
modsteps 2 5 0 0 0.6 -0.7 0.2 0.9 -0.4 0 0 0 0 0 0 0 0 0 0 0

new MOVING / Phaser Marimba
base Marimba
set 4 64  12 0
fx 1 phaser 0.2 0.7 0.7 0.6
fx 2 hall 0.5 0.5 0.5 0.2
mod 1 1 10 0.7 12 6 0 0 0.09            # PHASER's FEEDBACK drifts, +40%, free

new MOVING / Wood To Metal
base Marimba
set 4 82  5 50  7 100  12 -1
fx 1 plate 0.1 0.6 0.5 0.25
send 1 0.2
mod 1 1 3 0.95 9 1 0 0                  # MATERIAL, +90%, 2 BARS: wood and metal in turn, as it rings

new MOVING / Warp Bells
base Handbell
set 4 82  7 100  18 3  12 -3
fx 1 hall 0.7 0.6 0.45 0.3
mod 1 1 4 0.7 12 0 0 0 0.3              # STRETCH, +40%, free at 0.3 Hz: the partials sway as they ring

new MOVING / Decay Steps
base Vibes Still
set 4 70  5 96  7 100  12 -1
fx 1 tape 0.5625 0.3 0.4 0.2            # 1/8 DOT
mod 1 2 2 0.95 3 0 0 0                  # a STEPPER on DECAY, 1/8: a note short, a note rings
modsteps 1 8 0 0 -0.9 0.6 -0.9 -0.4 0.9 -0.9 0.2 -0.6 0 0 0 0 0 0 0 0

new MOVING / Pan Pong Blocks
base Temple Blocks
set 11 0  12 3
fx 1 ping 0.3125 0.4 0.5 0.3            # 1/8
mod 1 2 33 0.95 1 0 0 0                 # a STEPPER on PAN, every step
modsteps 1 6 0 0 -1 1 -0.3 0.6 -0.8 0.9 0 0 0 0 0 0 0 0 0 0
mod 2 1 0 0.7 9 1 0 0                   # TONE, +40%, 2 BARS

new MOVING / Filter Tide
base Vibes
set 4 82  7 92  12 2
fx 1 filter 0.55 0.5 0.3 1              # LOW 24, 1.2 KHZ, RESONANCE 50%
fx 2 hall 0.6 0.55 0.45 0.25
mod 1 1 8 0.8 10 0 0 0                  # the CUTOFF, +60%, 4 BARS

new MOVING / Gated Glass
base Crystal
set 4 72  7 100  12 0
fx 1 plate 0.1 0.6 0.5 0.3
mod 1 2 32 1 0 0 0 0                    # a STEPPER on LEVEL, +100%, every half step: a gate (-1 is silence)
modsteps 1 6 0 0 0 -1 0 0 -1 -1 0 0 0 0 0 0 0 0 0 0
# ---- More presets (2026-11-17, presets-plan.md, "More presets") --------------------------------------

# BASS: plucked, struck and buzzing, an octave or two down.

new BASS / Round Bass
base Plucked Bass
set 2 25  3 30  4 50  12 -2
fx 1 comp 0.45 0.4167 0.4 1             # THRESHOLD, RATIO, SPEED, MIX: held level
new BASS / Upright
base Plucked Bass Box
set 2 40  4 45  20 1  21 70  12 2       # the guitar's body, bigger
fx 1 eq 0.6 0.45 0.4 0.4                # LOW, MID, MID FREQ, HIGH
new BASS / Log Bass
base Log Drum
set 4 45  12 2
fx 1 saturate 0.35 0.375 0.5 0.5        # DRIVE, TUBE, TONE, MIX
new BASS / Marimba Bass
base Marimba
set 9 -1  4 50  12 2
mod 1 1 0 0.7 8 1 0 0                   # TONE, +40%, a bar, TRIANGLE
new BASS / Deep Tom
base Tom
set 4 55  2 40  12 2
fx 1 filter 0.5 0.35 0.1 1              # CUTOFF, RESONANCE, LOW 12, MIX
new BASS / Buzz Bass
base Plucked Bass
set 22 40  4 55  12 0                   # BUZZ: a fret's rattle
send 1 0.2
new BASS / Steel Bass
base Steel
set 9 -1  2 60  4 45  12 0
fx 1 phaser 0.2 0.5 0.4 0.4             # RATE, DEPTH, FEEDBACK, MIX
new BASS / Muted Bass
base Nylon
set 9 -1  3 20  4 35  12 0
fx 1 comp 0.5 0.5833 0.3 1
mod 1 1 1 0.7 5 5 0 0                   # POSITION, +40%, a quarter, RANDOM: every pluck in another place

# KEYS: struck strings, tines and bars, played as keys.

new KEYS / Tine Piano
base Kalimba
set 2 50  4 65  20 2  21 60  12 0
fx 1 tremolo 0.45 0.3 0.125 0.7         # RATE 1/8, DEPTH, SINE, SPREAD
new KEYS / Hammer Keys
base Dulcimer
set 1 1  2 55  4 62  20 2  21 55  12 0  # struck, not plucked
fx 1 plate 0.15 0.5 0.5 0.2             # PRE, DECAY, TONE, MIX
new KEYS / Celeste Keys
base Celesta
set 4 70  12 0
fx 1 chorus 0.3 0.4 0.125 0.35          # RATE, DEPTH, TYPE, MIX
new KEYS / Plectrum Keys
base Steel
set 2 85  3 12  4 52  20 2  21 60  12 -2
fx 1 eq 0.45 0.55 0.65 0.6
new KEYS / Vibe Keys
base Vibes Still
set 19 30  12 0                         # MOTOR: the fans turning
fx 1 chorus 0.25 0.35 0.125 0.3
new KEYS / Glass Keys
base Crystal
set 4 70  12 -1
fx 1 hall 0.55 0.5 0.55 0.25            # SIZE, DECAY, TONE, MIX
new KEYS / Bell Keys
base Glockenspiel
set 9 -1  4 60  2 70  12 0
mod 1 1 0 0.65 9 0 0 0                  # TONE, +30%, 2 BARS, SINE
new KEYS / Felt Piano
base Dulcimer
set 1 1  2 15  4 66  20 2  21 60  12 6  # a soft felt hammer
fx 1 wear 0.25 0.3 0.1 0.4              # WOW, DIRT, DROPS, TONE: an old tape of it
fx 2 plate 0.2 0.55 0.45 0.25
)presets"
