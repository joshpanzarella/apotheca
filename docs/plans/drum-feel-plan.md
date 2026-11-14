# FEEL on the drum grid: HUMAN TIME and HUMAN LEVEL for each lane

Owner, 2026-11-14: "there is no easy way to humanize like we have on the piano roll. That should be a
first class citizen in the drum sequencer. I want to make it as easy as possible to get a good beat and
humanize it quickly." Nothing here is built.

## Settled with the owner

| Question | Answer |
| --- | --- |
| Live, or written into the hits | **Live**: applied as the beat plays, nothing written into the hits; every time round differs |
| SWING | Wanted, **later**: HUMAN TIME and HUMAN LEVEL first |
| The main beats | **Tighter**: a hit on beat 1, 2, 3 or 4 (steps 1, 5, 9, 13) moves half as much as one between them |

## What it is

Each LANE of a drum track has two amounts, as a chord has (`chord_rule.hpp`): **HUMAN TIME** and
**HUMAN LEVEL**, each OFF, 10, 25, 50, 75 or 100%.

| Setting | At 100% |
| --- | --- |
| HUMAN TIME | each hit up to 12 ms early or late (`kHumanTimeMs`), 6 ms on a beat |
| HUMAN LEVEL | each hit up to 15% of its level louder or quieter (`kHumanLevelMost`), 7.5% on a beat |

- Added on top of a hit's own nudge and level; a ratchet's pieces move together with their hit.
- **Every roll is worked out from where and which time** (track, lane, bar of the cell, step, steps
  since Play), never a generator walked: the same on any helper count and block length, Play from the
  top the same performance, a bounce what was heard, each time round another.
- A hit that may come early is asked for a step ahead, as a nudged-early hit is; the song's first step
  lands on the top. A bounce's last step asks nothing early of the first (as chords).
- **A lane with both at OFF takes the path it always did**: old songs the same to the bit.
- In a song file: a line of the lane's own, left out when both are OFF. In undo with the song.
- It is the lane's (the track's), not the scene's: every scene of the track plays with it.

## The gesture

- **Cursor on a lane's name, X held** ("what it does"): the strip shows HUMAN TIME and HUMAN LEVEL,
  each button's chip under its field (`ui::hintsUnder`). LEFT / RIGHT: HUMAN TIME. UP / DOWN: HUMAN LEVEL.
- **X + Y: the dice**, small amounts only (10 or 25%, as a chord's dice).
- **X + A: this lane's feel onto every lane of the kit** (A places).
- A lane with a feel shows a small mark by its name.
- X on a lane's name still hears the drum on a tap (it is held for the strip, as on a hit).

## Batches

| Batch | What |
| --- | --- |
| 1 | The engine: the rule (a Rack-free header beside `chord_rule.hpp`), the lane's two amounts, the command, the trigger path, asking ahead; tests (the rolls by hand, block lengths, helpers, Play twice, a bounce, old songs to the bit); the cost row |
| 2 | The app: `TrackData`, the song file, undo, the gesture, the strip, the mark, the dice, X + A; stills at every size and theme; README, CLAUDE.md |
| 3 | Review by measurement and by trying it; mend |

## Later

SWING (per lane, every second sixteenth late, up to a triplet), with L1 / R1 on the same strip.
