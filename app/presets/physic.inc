R"presets(
# PHYSIC's built-in presets past its own table, and the track sound any built-in carries.
# How to write one: docs/plans/presets-plan.md. Settings by id: engine/include/apotheca/physic_rule.hpp.
# APPEND ONLY: a preset's place is in songs. Never put one in before another, never move one.
# `ctest -R presets` fails on any line here it cannot read.

new SHIMMER / Glass Steps
base 19                         # Church Bell (BELLS), then:
set 3 35  4 70                  # POSITION 35, DECAY 70
fx 1 shimmer 0.6 0.7 0.6 0.4    # SHIMMER, DECAY, TONE, MIX
send 1 0.2                      # into ECHO
mod 1 2 1 0.7 5 0 0 0           # MOD 1: a STEPPER on macro 2 (POSITION), +40%, 1/4
modsteps 1 8 0.2 0 0.5 -0.3 0.8 0 -0.6 0.2 0.9 -0.1 0 0 0 0 0 0 0 0
)presets"
