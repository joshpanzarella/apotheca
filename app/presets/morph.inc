R"presets(
# MORPH's built-in presets past its own table (BASIC, eight), and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/morph_rule.hpp
# (0 TABLE A, 1 POSITION A, 2 TABLE B (0 OFF, 1 + the table), 3 POSITION B, 4 MIX, 5 B SEMI, 6 B FINE,
# 7 SUB, 8 OCTAVE, 9 TUNE, 10 UNISON, 11 DETUNE, 12 WIDTH, 13 FILTER, 14 CUTOFF, 15 RESONANCE,
# 16 KEY TRACK, 17-20 ATTACK DECAY SUSTAIN RELEASE, 21-24 the MOD ENV's, 25 ENV > POSITION,
# 26 ENV > CUTOFF, 27 VELOCITY, 28 LEVEL).
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# (The 24 of the plan's section 6, PADS, LEADS, BASS and MOVING, come in its batch 3.)
)presets"
