R"presets(
# ACID's built-in presets past its own table (Classic Saw, Classic Square), and the track sound any
# built-in carries. How to write one: docs/plans/presets-plan.md. Settings by id:
# engine/include/apotheca/acid_rule.hpp (0 WAVE, 1 OCTAVE, 2 TUNE, 3 CUTOFF, 4 RESONANCE, 5 ENV MOD,
# 6 DECAY, 7 ACCENT, 8 SWEEP, 9 DRIVE, 10 AMP DECAY, 11 SLIDE, 12 VELOCITY, 13 LEVEL).
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# (The 32 of the plan's section 5 come in its batch 4.)
)presets"
