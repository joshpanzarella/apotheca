R"presets(
# CLOUD's built-in presets past its own table (Soft Cloud, Dust, Fifths Choir), and the track sound any
# built-in carries. How to write one: docs/plans/presets-plan.md. Settings by id:
# engine/include/apotheca/cloud_rule.hpp (0 POSITION, 1 SPRAY, 2 SIZE, 3 DENSITY, 4 SCATTER, 5 SHAPE,
# 6 PITCH SPRAY, 7 PITCH STEPS, 8 REVERSE, 9 STEREO, 10 SCAN, 11 ROOT, 12 TUNE, 13 ATTACK, 14 RELEASE,
# 15 LEVEL, 16 VELOCITY). A built-in never names a recording: it plays whatever the track holds.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.
# (The 24 of the plan's section 4 come in its batch 3.)
)presets"
