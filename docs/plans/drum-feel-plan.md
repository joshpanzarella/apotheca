# FEEL on the drum grid: HUMAN TIME and HUMAN LEVEL for each lane

Owner, 2026-11-14: "there is no easy way to humanize like we have on the piano roll. That should be a
first class citizen in the drum sequencer. I want to make it as easy as possible to get a good beat and
humanize it quickly."

**Built, all three batches, on branch `feel` (2026-11-15). Not joined to `main`, not shipped, and
nobody has listened to it**: every figure below is from the tests, the bench and the stills. What was
built, what was chosen and what was learned is under "As built" at the end.

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
Planned below ("SWING"), 2026-11-17.

## As built (2026-11-15)

| Part | Where |
| --- | --- |
| The rule | `engine/include/apotheca/feel_rule.hpp` (`LaneFeel`, `feelSeed`, `feelSamples`, `feelLevel`, `feelRunBack`) |
| The engine | `Track::feel`, `Track::feelAsked`, `Command::SetLaneFeel`; `schedule`, `playHit`, `askAheadHits`, `askAgain` in `engine.cpp` |
| The app | `TrackData::laneFeel`, `Session::setLaneFeel`, the song file's `feel LANE TIME LEVEL`, undo by `soundMark` / `restoreSounds` |
| The gesture | `PatternScreen::feelHeld_`, `feelTurn`, `feelRoll`, `feelToAll`; `app/lane_feel.hpp`; the strip's places in `pattern_layout.hpp` |
| Tests | `tests/feel_test.cpp` (engine, 14 tests), `tests/feelapp_test.cpp` (app, 8 tests); three felt lanes in `nextscene_test`'s song |
| Stills | `feel`, `feel-held`, `feel-off-held`, `feel-same-held`, `slices-name-feel` |

### The choices the plan left open

| Question | Chosen | Why |
| --- | --- | --- |
| How X on a name both hears and holds | **One hold does both.** The drum is heard on the press exactly as before (a recording for as long as X is held); the directions set the feel meanwhile | Nothing about hearing changed, so nothing to relearn. A turn of the feel sounds nothing: the feel is only in the playing pattern |
| HUMAN LEVEL at its ends | Held to 2%..100% of full | As a chord's. **A hit written at 100% only gets quieter**; A places hits at 80%, which go both ways |
| A run of slices | **Moves as one**, by its first hit's roll, through the bar lines inside its cell; each slice keeps its own level | Moved one by one, every slice met the sound up to 24 ms off and cut it. Now a break in order is the same samples, sooner or later. Where the cell goes round, a new run and a new roll |
| The dice | OFF, 10 or 25% of each, never both off, never what it was | "Small amounts only" |
| X + A from a lane at OFF | Every lane goes to OFF | "This lane's feel onto every lane" taken at its word; it is also the quick way to clear a kit |
| L1 / R1 while X is held on a name | Do nothing | Kept for SWING, and the slice view does not open by mistake |
| The mark | Purple, left of the name: upper piece TIME, lower piece LEVEL | The name's box is full to its right edge with a long recording's name |
| The strip with X up | One purple line: HUMAN TIME 25%   HUMAN LEVEL 10% | As a hit's step effects are said |
| The third field | EVERY LANE: COPY, or THE SAME | A's chip needs a field over it to say what it does |
| Undo | With the sounds (`soundMark`), not the pattern text | It is the track's, as GLIDE FROM is. One hold of X is one step |
| The most HUMAN TIME | Never over a quarter of a step | A guard: 12 ms is under a fifth of a step at 240 BPM, so it never bites today |
| CHANCE on a felt lane | Left as it is (the walked dice) | Changing it would change every song's chances |

### Learned

- **A feel turned while the song plays lost hits or played them twice** until the track kept
  which step's hit it had asked ahead (`feelAsked`), as chords needed `chordAsked`.
- **A hit asked again must count from the sample its step began on**, not from the step's exact
  time: at a tempo where a step is a part of a sample long it came a sample off.
- **Asked again after its early moment has gone by, a felt hit comes on its step** (a few ms
  late) and is not lost. A nudged-early plain hit in that place is lost, as it always was.
- **A felt hit with a CHANCE, asked again after a scene move, rolls its chance again.** From then
  on the song's chances are not the ones it would have had. Not mended.
- **`engine_test` was 20 KB from the end of the stack** (six Engines of 1.4 MB in one frame). An
  Engine a kilobyte bigger crashed it one run in three. Five are on the heap now.
- Y let go after X was taken as a tap of Y: it rolled the lane's hits, and on a hit it stepped the
  chance (that one was there before). Mended for both.
- Cost: 0.10 to 0.11% of a core for all 96 lanes of twelve kits at 100% and 100%. HUMAN LEVEL
  alone costs nothing that shows.

### Mended after the review (2026-11-15)

- **A break longer than a bar was cut at its inner bar lines** (each bar rolled by itself: a seam
  in 4 rounds of 6). A run now goes back over the bar lines inside its cell (`feelRunBack`).
- **A felt hit waiting when the tempo changed came a sample sooner or later by the block.** Its
  wait and its ratchet's gap are made whole again in `SetTempo`.
- **L1's "built-in drum" message was drawn over the name's purple line.** The line gives way.
- **A or Y held on after X was let go did its own hold** (lane volume, the lane's dice; a nudge
  on a hit after THROW). That press is spent now; press again.
- Tests the review found missing: each slice's own LEVEL, the summary's two amounts each in its
  place, the slice view's mark, the stick's pace, the summary as a tight row (`textsize`), a
  ratchet whose first piece sounds in the asking, and a scene put in on the last step
  (`testASceneMoved`). Giving the asked mark back is seen only when AT END is switched, never in
  a scene move: there the old hit and the new are one roll.

### For the owner's ears

- Where a break's cell goes round, the new round's run has its own move, so the join there is
  up to 12 ms open or overlapped (each round's move is up to 6: a cell's first step is a main beat). Should a looping break keep
  one move for good instead?

- Are 12 ms and 15% the right "most"? Is half the right share for a main beat?
- Should a lane's dice go past 25%?
- Should a hit at full level be able to go louder (it cannot: full is full)?
- With a long recording on a lane, it plays on while X is held to set the feel. Is that in the way?

## SWING (planned 2026-11-17, branch `swing`)

The owner, 2026-11-14 ("Later" above): SWING per drum lane, every second sixteenth late, up to a
triplet, set with L1 / R1 while X is held on a lane's name, on the same strip as HUMAN TIME and
HUMAN LEVEL. Built the way FEEL was, in batches: the plan (this), the engine, the app, the review.

### The rule

- **Which hits**: those on steps 2, 4, 6 ... 16 of a bar, counted from 1 (the second sixteenth of
  each eighth). A cell's every bar starts again on step 1. The main beats (1, 5, 9, 13) are never
  swung, so FEEL's "beats tighter" has nothing to say to it.
- **How late**: SWING percent of a third of a step. **At 100% the off sixteenth is exactly 1/3 of
  a step late**: the two sixteenths of each eighth are played long and short, 2 to 1, as the first
  and third notes of an eighth-note triplet (an eighth cut in three: the off sixteenth lands on the
  third). That is "up to a triplet", pinned in `feel_rule.hpp` (`kSwingMostOfStep`, `swingSamples`:
  `amount * samplesPerStep / 300`, so 120 BPM at 48 kHz gives exactly 2000 samples at 100%).
- **The six clicks are FEEL's** (OFF, 10, 25, 50, 75, 100). Said as the share of the eighth its
  first sixteenth takes (the way drum machines usually print swing), they are:

  | SWING | Off sixteenth late by | First sixteenth's share of the eighth |
  | --- | --- | --- |
  | OFF | 0 | 50% (straight) |
  | 10% | 1/30 of a step | 51.7% |
  | 25% | 1/12 | 54.2% |
  | 50% | 1/6 | 58.3% |
  | 75% | 1/4 | 62.5% |
  | 100% | 1/3 | 66.7% (a triplet) |

  The four upper clicks land on the four swings most machines are played at (54, 58, 62, 66).
- **Live**, as FEEL: nothing is written into the hits; a lane's swing is the track's, in every scene.
- **It adds** to the hit's own nudge and to HUMAN TIME's roll (the quarter-step guard is on the
  roll alone). A swung lane's every hit is a whole number of samples from its step's first sample
  (`ceil` of nudge + roll + swing), as a felt lane's: the same in any blocks.
- **Never early, so never asked ahead for it**: a hit is asked a step ahead only as before (a
  nudge early, or HUMAN TIME on the lane). A hit nudged early on an off step may come late once
  swung; it is still asked ahead, and waits the longer.
- **A ratchet on a swung hit is squeezed into what is left of its step**: its pieces are spread
  over (step - swing), so the last ends on the next step line, as an unswung ratchet's does. At
  100%, unsqueezed, a x4's last piece landed 1/12 of a step after the next step's hit and cut it
  (a lane cuts its own). The step before is not stretched (pieces are whole samples apart, `floor`).
- **Slices in order still carry their sound on** (FEEL's `feelRunBack` lesson): a run of slices
  takes the SWING of the hit its run begins with, as it takes that hit's HUMAN TIME. So a break
  laid out in order on a swung lane is not swung inside (it plays as one sound); a run that begins
  on an off sixteenth is moved late as a whole. Swung slice by slice, each off slice would meet the
  sound a sixth of a step on and cut it.
- **A lane at 0 takes the path it always did**: old songs, and FEEL songs, to the bit.

### The app

| Part | Pick |
| --- | --- |
| Where it is kept | `TrackData::laneSwing[8]` (0..100), `Track::swing[8]` in the engine, `Command::SetLaneSwing` (appended: a = lane, b = amount), `Session::setLaneSwing` |
| The song file | `swing LANE AMOUNT`, a line of the kit's own, **left out at 0**; under a synth track not taken; the number held as a double first |
| Undo | `soundMark` / `restoreSounds`, as FEEL: one hold of X is one step |
| The gesture | X held on a lane's NAME: **R1 one click more, L1 one less**, stopping at the ends (and held, they repeat at the feel's pace) |
| The strip | SWING is its first field, `kFeelSwingX`; the L1 / R1 chip stands under it by that one constant. HUMAN TIME, HUMAN LEVEL and EVERY LANE move right to make room |
| The mark | Purple, as FEEL's: a long and a short piece (2 to 1) along the top right of the name |
| X up | The name's purple line says SWING too |
| X + A | Copies the lane's SWING with its HUMAN TIME and HUMAN LEVEL ("this lane's feel onto every lane"; THE SAME only when all three are) |
| The dice (X + Y) | **Leave SWING alone** |
| The sound designer page | SWING is a KIT field, appended after HUMAN LEVEL |

### The owner's calls (each taken at the recommended answer until the owner says)

1. **"Up to a triplet" is 1/3 of a step late at 100%**, the off sixteenth on an eighth-note
   triplet's third note. Recommended. (The other reading, a sixteenth-note triplet, is 2/3 of a
   step: past where the next sixteenth would be heard as the beat.)
2. **The clicks are FEEL's six.** Recommended: they fall on the swings players reach for. A finer
   click (5% steps) is a one-line change in `lane_feel.hpp` if 25 and 50 are too far apart.
3. **The dice do not roll SWING.** Recommended: the dice are "a hand"; swing is the groove, chosen.
4. **X + A copies SWING too.** Recommended: a kit usually swings as one.
5. **A sliced break laid out in order is not swung inside** (the run keeps its sound). Recommended.
   The other way (each slice late, as an MPC plays a chopped break) repeats a sixth of a step of
   the sound at every off slice; it is one test in `schedule` if wanted.
6. **A ratchet is squeezed into its swung step; the step before is not stretched.** Recommended.
7. **The swing restarts every bar** (steps by number), so a scene cut to an odd length starts
   its next bar on an on step. Recommended: it is what a step sequencer does.
