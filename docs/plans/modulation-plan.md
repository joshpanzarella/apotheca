# LFO and random stepper plan

Written 2026-11-14 from three independent designs (one from the player's
hands, one from the engine and song file, one from music), judged and joined
here, then revised the same day after a review (section 11). **Batches 0 and
1 are built (sections 12 and 13), their review mended (section 14), batch 2 is built (section 15), joined with `main` (section 16) and the join reviewed and mended (section 17). The owner answered section 17's calls on 2026-11-15 ("go with all your picks"), `main` was joined again, and batches 3 and 4 (X held on a knob, DEVICE and FX) are built (section 18), and batch 5, the MOD tab (section 20). Nothing is heard.** The code at 0.442 was read. The review timed `setMacro`
and found a Phazed note cut in a copy; those figures are the only ones run,
and none on the handheld.

## The recommendation

**Two modulators a track, MOD 1 and MOD 2. Each is OFF, an LFO or a STEPPER,
aimed at one setting with a signed depth, in time with the song. The fast
way in is the knob itself: hold X on a macro, an effect setting or a send and
push UP.** A MOD tab after FX shows both and is where a stepper's steps are
written. The engine works every value out from where the song is, never from
the value before, so it is the same on any helper count, block length,
tempo change and scene move. **A song with no MOD takes today's code path,
to the bit.**

| Question | Answer |
| --- | --- |
| What they are | LFO: a hand that turns a knob back and forth by itself, in time. STEPPER: a row of 1 to 16 values, one held per note length, with dice (section 1) |
| How many | **2 a track**, each one kind (owner's call: one per knob, or 3) |
| Aimed at | One target each, named by the **automation lane number** that exists: macros 0-7, effect settings 8-19, sends 20-21. Later: LEVEL 32, PAN 33. Pitch: later, the owner's call |
| Depth | **Signed**, -100% to +100%, around where the knob or automation puts it |
| Rate | **Note lengths only**, in time with the song. No free Hz (the JX-3P's own LFO is the free one) |
| Starts from | **Play** (song position). CELL and NOTE later, the owner's call |
| With automation and the knob | Knob or automation sets the **centre**; the MOD adds on top, then a clamp. **WRITE records the centre, never the wobble** |
| Per track or per scene | **Per track**, like GLIDE FROM. Per-scene depth through spare lanes 22 and 23 (owner's call, a later batch) |
| Song file | New words `mod` and `modsteps`, **left out when OFF**. Old songs load and sound as they did |
| Presets | Not in presets (owner's call) |
| Engine | On the audio thread, before the helpers, on a **64-sample tick counted from each step's start**; chunks cut on the tick **only while some MOD is on**. **Synth macros every 8th tick (512 samples)** |
| Cost target | All 24 MODs on effects and sends: **≤ 0.05% of a desk core** (≤ 0.25% on the handheld). 24 MODs on QMOD macros at 512 samples: about 0.3% desk (measured per call in review; about 1.4% handheld, guessed). **QMOD is the costly one, not Phazed** |
| While stopped | **X held on a modulated knob runs its MOD** from the moment X went down, so the gesture is heard (batches 3-4) |
| First thing to build | **Batch 0**: `mod_rule.hpp` and its test; **one "centre + offset" function** that all four ways a lane's value goes out use (offset 0); a send's given value clamped while its centre is kept; **Phazed DETUNE stops cutting held notes at 0.5**; QMOD's `setMacro` returns early on an equal value; the cost row. **Nothing on screen; no sound change except the DETUNE note cut, which is a bug** |

**Which design won.** The engine-first design is the spine: two general
modulators, lane numbers as targets, a pure rule header, the safest batches.
From the hands design: **the knob gesture** (one press from the cursor to a
wobble), WRITE records the centre, the moving brass mark, the tick from the
step's start. From the music design: LEVEL and PAN as ids outside the lanes
(so 22 and 23 stay spare), RESTART tested on the event and not the scene's
number, and what other boxes do. Section 10 says what each lost and why.

**The owner cannot hear.** Every "it wobbles" below is a test or a still
until he says otherwise.

## 1. What they are, and what a player does with them

| Thing | Plain words | In a song |
| --- | --- | --- |
| **LFO** (low-frequency oscillator) | A slow wave that turns a setting up and down by itself, in time with the song | A bass's FILTER opens and shuts every half bar. ECHO send swells over 4 bars. CHORUS depth breathes. Phazed TONE opens over a scene |
| **STEPPER** (random stepper, "sample and hold") | A row of 1 to 16 values. One is held for each note length, then the next. Dice can roll some or all of them each time round | A hi-hat's filter different on every 16th, the same 8 values going round. A lead's TONE new on each beat. Y rolls a new row; turn up CHANCE and it changes by itself |

Both ride on what is there now: the knob (or its automation) is the centre,
the MOD moves around it.

What other boxes do (from memory, no web search):

| Box | What it has | Taken |
| --- | --- | --- |
| Elektron Digitakt / Digitone | An LFO a track, synced rates, retrigger modes, signed depth, one target | Signed depth, synced rates |
| Novation Circuit | Two LFOs a synth | **Two a track** |
| Dirtywave M8 | Several general mods an instrument, each one target | General mods with a kind |
| Make Noise Turing Machine | A random loop with a LOCK | Y writes a kept row; CHANCE rolls steps afresh (section 3) |

## 2. Targets, how many, depth

**Recommend: target id = the automation lane number where one exists.** One
numbering for the song file, a TYPE change and the engine.

**Four paths hand a lane's value out today, not one** (review, finding 1):
`applyAuto` (`engine.cpp:1760-1769`), `SetMacro` (`1155-1162`: calls
`setMacro`, then sets `autoBase = autoNow`), `SetEffectParam` (`1233-1240`:
`setParam`, then the same) and `SetSend` (`1117-1124`: writes `track.send[]`
directly). Batch 0 routes all four through **one function, `giveLane(track,
lane)`, that sends `clamp(centre + offset)`**. Otherwise a knob turned while a
MOD runs would give the bare knob until the next tick. An effect just put in a
slot has its four lanes at `kAutoNone` until its `SetEffectParam` arrives
(`engine.cpp:864-869`), so **`runMods` skips a lane whose centre is below
zero**.

| Id | What | Today | Batch |
| --- | --- | --- | --- |
| 0-7 | The synth's 8 macros | Lanes 0-7 (`engine/include/apotheca/engine.hpp:229`) | 1 |
| 8-19 | The track's three slots' settings, `8 + slot*4 + setting` | `engine.hpp:229-230` | 1, **amounts only** |
| 20, 21 | ECHO and SPACE sends | `engine.hpp:230`, `233` | 1 |
| 22, 23 | Spare lanes (`engine.cpp:1767`) | — | **Not targets.** Kept for MOD 1 / MOD 2 DEPTH automation (batch 7, owner's call) |
| 32, 33 | LEVEL, PAN | `SetTrackPan` (`engine.hpp:187`), not automated | 8 |
| 34+ | NOTE PITCH, HIT PITCH, HIT START | No bend call in `Instrument` (`instrument.hpp:11-38`) | Later, owner's call |

- **2 MODs a track, one target each.** Two on one target add, then one
  clamp. One struct serves both kinds, so two LFOs or two steppers cost the
  same. (The hands design had one LFO per knob, no cap: faster to think
  about, but cost and screen unbounded. **Owner's call.**)
- **Depth signed**: a negative depth turns the other way, so no INVERT.
- **Not targets at first: choices** (FILTER TYPE is `int(v*5)`; Phazed's
  CHORUS and OCTAVE macros are thresholds; COMB's PITCH, 25 notes; RING's
  WAVE, 3). A wobbled choice clicks. The table's `choices[]` says which, so
  the rule is read, not listed. A STEPPER on OCTAVE (random octaves) is
  musical: **owner's call**, later.
- **Phazed DETUNE is an amount but cuts notes at exactly 0.5** (review,
  finding 2, measured in a copy). On a one-line sound, DETUNE at 0.5 sets
  the line back to one; off the middle it is the twin line
  (`phazed.cpp:130-144`). `cz_synth_edit_tone` then calls `select_tone`,
  which runs `fade_all` (`cz_synth.c:363-374`): every held note is cut. One
  pass through 0.500 left a held note at RMS 0.008 against 0.087 (preset 7;
  9 and 12 alike). A MOD lands on 0.5 often (a sine's start, a step of 0,
  depth 0, Stop). **Mended in Phazed in batch 0**: a change of line select
  from DETUNE no longer fades the voices (a `third_party/CHANGES.md` line).
  This bug is there today too: a knob or automation passing 0.5 cuts notes.
  Held by a test (a note held through DETUNE 0.52, 0.500, 0.52 keeps its
  level). If the mend changes something else in the tone, the fallback is a
  guard: a MOD's given value on Phazed DETUNE never lands on exactly 0.5.
- **Not targets: the buses and the master.** Nothing on them is automated
  (CLAUDE.md, "The app").
- **By instrument**:

| Instrument | What a MOD reaches |
| --- | --- |
| Phazed, QMOD, Twin / JX-3P | Macros (`setMacro`: `phazed.cpp:292`, `qmod.cpp:215`, `twin.cpp:525`), effects, sends |
| Sampler | Effects and sends: its `setMacro` does nothing (`sampler.hpp:39`) |
| Drums | Effects and sends |

## 3. Shapes, rate, restart, steps

### Both kinds

| Setting | Choices | Recommend |
| --- | --- | --- |
| RATE | 1/2 step, 1 step, 1/8T, 1/8, 1/4T, 1/4, 1/4 dotted, 1/2, 1 BAR, 2, 4, 8 BARS | **In time only.** One click per push. Ids appended; list order apart from ids (as `kGlideOrder`) |
| DEPTH | -100..+100% | The stick for big moves, the D-pad by 1% |
| RESTART | **PLAY** (song position since Play). Later: CELL (the cell's own bar and step), NOTE (the last note the track began) | PLAY first. **Owner's call** for the default once CELL exists |

**Why PLAY first:** it is a pure function of `stepsFired_` and the place in
the step (`engine.hpp:724-726`), it needs no event, and a tempo change keeps
it on the beat for free (`SetTempo`, `engine.cpp:1071`). A 1-bar LFO in a
1-bar cell sounds the same every time round either way. CELL matters only
when the LFO is longer than the cell; the hands design notes CELL then
restarts it before it ends. None of the three holds a scene's number, so
NEXT SCENE moves nothing.

**What PLAY costs (review, finding 10): a bounce and a live loop can wobble
out of phase.** Play starts on the scene being worked on
(`Session::togglePlay`); a bounce starts from the first scene
(`app/bounce.cpp:11`). A 4-bar LFO or a stepper in scene 3 is in another
phase in the bounce than when scene 3 is looped live, unless every scene
before it is a whole number of the MOD's cycles (a scene cut to odd steps
makes that unlikely). It is the same caveat GLIDE FROM RANDOM carries. A
test holds both ways, as `testRandomOnALaterScene` does, and the manual says
it. CELL restart (later) is the cure if the owner minds.

### LFO

| Setting | Choices |
| --- | --- |
| SHAPE | SINE, TRIANGLE, RAMP UP, RAMP DOWN, SQUARE, RANDOM (a seeded value each cycle, held), DRIFT (glides between seeded values) |
| PHASE | 0, 1/4, 1/2, 3/4 (two MODs a quarter apart) |

### STEPPER

| Setting | Choices | What it does |
| --- | --- | --- |
| LENGTH | 1-16 | Steps in its row; may run against the cell's length |
| STEPS | 16 values, -100..+100% | Written by hand, or rolled by Y (undoable: the "kept" row) |
| SLEW | 0-100% of a step | Glides into the next value instead of jumping |
| CHANCE | 0-100% | Each step, each time round, the chance it plays a fresh roll instead of its written value. 0: the row as written, every time (locked). 100: every step new |

**Every roll is worked out from where and which time, never walked.**
`seed = chordSeed(track, 0, positionInRow, timesRound)` and
`chordUnit(seed, kRollMod, mod * 32 + what)` (`chord_rule.hpp:121`, `130`),
with `kRollMod = 6` appended to `ChordRoll` (`chord_rule.hpp:128`). SLEW's
"value before" is the step before, worked out the same way, not remembered.

**Not taken: the Turing Machine's CHANGE** (a rolled value is kept until
rolled again). It needs memory of last time round, updated on the audio
thread; the music design pointed at `Track::strokes` as precedent. CHANCE
plus Y covers locked, free and in between with no memory. **Owner's call**
if he wants "drifts slowly away from the row".

**Pitch** (a stepper that transposes a note as it begins, kept to a scale)
is a melody maker, and it would have to agree with the `GlideTrail` and the
roll's glide lines. **Later, owner's call.**

## 4. With automation, a knob being turned, and WRITE

**What is given = clamp(centre + Σ depth × shape, 0..1).**

| Layer | Role |
| --- | --- |
| Knob (`autoBase`, `engine.hpp:533`) | The resting centre |
| Automation (the glide to the written value, `engine.cpp:1548-1557`, `1774-1789`) | Moves the centre per scene, as today |
| MOD | **Adds** on top; one clamp |

| Case | What happens |
| --- | --- |
| Knob turned while a MOD runs | Moves the centre; the wobble goes on around it |
| WRITE on | Records the knob, **never the wobble**. `recordAutomation` is fed the knob (`app/session.cpp:594-598`, `app/session_synth.cpp:49`); nothing changes there |
| B on a macro (back to the middle, its automation cleared: `app/synth_screen.cpp:232-241`) | Leaves the MOD alone |
| The FX screen trying effects (`HoldEffectAutomation`, `engine.hpp:190`; `autoWaits`, `engine.hpp:538`) | That slot's MOD waits too |
| Stop (`settleAutomation`, `engine.cpp:1791-1797`) | Every MOD offset goes to 0; the knob is heard. **X held on a modulated knob while stopped runs its MOD anyway** (below) |
| TYPE changed in a slot | The MOD's target follows the setting **by name**, as lanes do in `changeEffectType` (`app/fx_screen.cpp:22`). A setting not kept, **a kept setting that is a choice on the new type** (TIME), another family or an emptied slot: the MOD goes OFF. **The same on load**: a file's MOD aimed at a choice reads OFF |

**Engine change:** one `giveLane` for all four paths (section 2). `autoNow`
stays the centre; the synth, effect or send is given `clamp(centre +
offset)`. **A lane with no MOD has an offset of exactly 0.0f, and the same
value goes out in the same calls.** That is the bit-exact path.

**A send's given value is clamped, its centre is not touched** (review,
finding 13). `settleAutomation` compares `autoNow` to `autoBase`
(`engine.cpp:1791-1797`) and `fireStep` compares the written value with
`autoNow` (`1549-1557`): both must keep seeing the centre, never the wobble.
On Stop the given value goes back to the centre. Batch 0 tests this with a
fake offset.

**While stopped, X held on a modulated knob runs the MOD** (review, finding
6). Without it, "hold X + UP" on a stopped song sets a wobble nobody hears:
X's own note is plain. The position is counted from the moment X went down
(`Command::ModAudition` on, off), on the same tick, and the offsets go to 0
when X is let go. Nothing plays then, so a song cannot change by it. Built
with the gesture (batches 3-4), the engine side in batch 1.

## 5. Per track; scene move, undo, song file, presets

| Thing | Recommend |
| --- | --- |
| Settings and steps | **Per track** (`TrackData`, beside `glideFrom`, `app/project.hpp:209`). Automation is per scene (`project.hpp:221`); a MOD is not. **Owner's call** |
| Per-scene feel | Later: lanes 22 / 23 automate MOD 1 / MOD 2 DEPTH, so "wobble only in the drop" needs no new scene storage. They are already in `Project::automation` and in the scene move's `SceneStore`. **Mapping fixed now**: a lane holds 0..1 (`SetAutomation` stores below 0 as `kAutoNone` and clamps at 1, `engine.cpp:1164-1166`), so **0.5 is no depth, 0 is -100%, 1 is +100%**, the same scale a DEPTH setting has in the song file |
| Scene move | Carries nothing: no scene number held, position counts from Play |
| Undo | Seen by `Session::soundMark()` and put back by `restoreSounds` (`app/session.hpp:545-547`), as GLIDE FROM and sends are; compared **as the file writes them** (`%.6g`) |
| Song file | On a track: `mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE` and `modsteps N LENGTH SLEW CHANCE v1..v16`. **Both left out when KIND is OFF.** Every number through `wholeIn` / `fractionIn` / `seedIn` (`app/song_file.cpp:28-30`). An unknown KIND or TARGET reads as OFF |
| Old songs | No `mod` line: OFF, the same to the bit. An old program skips the words. `tests/everykind_main.inc` is not remade |
| Presets | **Not saved** (a MOD aims at effects and sends too). Owner's call |
| Track deleted, other instrument | `RemoveInstrument` (`engine.cpp:1187`) sets both OFF in the engine, as it puts GLIDE FROM back. Another instrument drops macro targets |
| Twin songs moved to the JX-3P | Macro targets dropped with Twin's macros and their automation (`docs/plans/jx3p-plan.md:23`) |

## 6. The engine

**Where:** a new `Engine::runMods`, in `renderChunk` beside `glideAutomation`
(`engine.cpp:2089`), **before the tracks are shared out**. Helpers only read
synth and effect state, so any helper count gives the same bits.

**When: a tick every `kModTick` = 64 samples (1.33 ms), counted from each
step's first sample.** Steps start on their exact sample (the chunk is cut
there, `engine.cpp:1700-1730`). `render()` cuts at the next tick too, **but
only while `anyMod_`**. A chunk is at most 64 frames (`kMaxBlock`,
`engine.hpp:100`) and restarts at each step, but **every other cut moves the
grid** (review, finding 8): a note's end, a waiting note, an arpeggio, a drum
ratchet (`engine.cpp:1705-1725`), and the end of each `render()` call (256 is
not a whole share of a 6000-sample step). Each costs one more cut to get back
on the tick: **about one extra chunk per such event, not per `render()`**.
With helpers every chunk wakes them, so batch 1 **counts chunks a second on
the busiest song with one MOD on**, not only its time.

| Rule | Why |
| --- | --- |
| The value at a tick = `shape(position)`, position = `stepsFired_ - 1 + (samplesPerStep_ - untilStep_) / samplesPerStep_` **read before `render()` takes the chunk off `untilStep_`** (`engine.cpp:1726`, before `renderChunk` at `1750`), or the chunk's length added back | Pure: block length, helpers, tempo and scene moves cannot change it. Read after, it is the chunk's end, one chunk late (review, finding 7) |
| `untilStep_` is fractional: a step fires on the first sample at or after its time | `mod_rule`'s test fixtures work positions out that way, not on whole samples |
| Cut chunks only while some MOD is on | Cutting every song would move today's automation glide and Twin's render splits: old songs would change |
| A value is handed over only when it changed | Phazed and Twin return early on an equal value (`phazed.cpp:295`, `twin.cpp:528`); **QMOD does not** (`qmod.cpp:215-219`): batch 0 adds the early return. A held STEPPER step costs one compare |
| **Synth macros every 8th tick (512 samples, 10.7 ms)** | Each change works the whole tone out again. Timed in review (Release, 8 notes held, warm cache, so a floor): **QMOD 0.93-1.27 µs a call, Phazed 0.34-0.59, Twin 0.05**. At 64 samples (750 calls/s) 24 QMOD MODs are about 2% of a desk core, about 10% on the handheld; at 512, about 0.3% and 1.4%. The JX-3P reads macros every 480 anyway. Effects and sends stay on every tick |
| State in a fixed array in `Engine::Track` (`engine.hpp:504`) | The audio thread never allocates |
| `std::atomic<float> modShown[2]` per track | The screen reads what is heard, as `effectMeter` (`engine.hpp:364`, `621`). The app thread never touches a synth or an `Effect` |

**Commands**, appended after `HoldEffectAutomation` (`engine.hpp:190`):
`SetMod` (track, a = mod, b = field, value) and `SetModStep` (track, a = mod,
b = step, value). Through `send()`, counted when lost.

**How a value reaches each target:**

| Target | Path | Note |
| --- | --- | --- |
| Phazed macro | `setMacro` → `apply(false)` (`phazed.cpp:292-297`) | 0.34-0.59 µs a call. CZ values are whole numbers, so a sweep moves in stairs. DETUNE's note cut mended in batch 0 (section 2) |
| QMOD macro | `setMacro` → `apply()` (`qmod.cpp:215`) | **The cost risk**: 0.93-1.27 µs a call, no early return yet. Hence the 512-sample macro tick |
| Twin macro | `setMacro` → `apply()` (`twin.cpp:525`) | 0.05 µs a call |
| JX-3P macro | Read on its own 480-sample tick (`jx3p-plan.md:169-173`) | A MOD faster than 10 ms is wasted there; fine |
| Old five effects | `setParam` → `changed()`; FILTER works `pow` and `tan` again (`engine/src/fx/filter.cpp:59`), no glide (pinned) | 1.33 ms stairs. **May zip on FILTER CUTOFF; a test measures the step size; nobody has listened** |
| Newer effects | `setParam` into their own 20-30 ms `Glide` | **A fast MOD is softened**: a 1/8 at 120 BPM through a 25 ms glide loses depth. A test states how much per rate |
| The nine types in the tree at 0.442 (commit `0ea6967`, before `b2f8e59` 0.442) | COMB, VOWEL, EQ slide sample by sample on `fx/svf.hpp`'s 16-sample TICK through `Gentle` (two glides, about 30 ms); FOLD, FUZZ, RING, WEAR, LIMITER, SNAP through `Glide` | **The smooth CUTOFF-like targets exist today** (VOWEL SHIFT, EQ MID FREQ, COMB FEEDBACK), so the old FILTER's stairs matter less: if it zips, these are the way round it. Softened like the rest. **Amounts**: all their settings but COMB PITCH and RING WAVE (choices). LIMITER has no MIX |
| Sends | `track.send`, already gliding 5 ms (`engine.cpp:1766`; `kSendGlideSeconds`, `engine.hpp:99`) | A send MOD wakes a bus at rest, as it should |
| LEVEL, PAN (batch 8) | The level and pan glides (`panGains`, `engine.hpp:98`) | The middle-at-rest fast path stays only while the offset is 0 |

**Cost targets** (desk; the handheld is about a fifth as fast):

| Case | Desk | Handheld |
| --- | --- | --- |
| Working out all 24 MODs (12 tracks × 2) at 64-sample ticks | ≤ 0.03% | ≤ 0.15% |
| One MOD on an effect or send | ≤ 0.02% | ≤ 0.1% |
| One MOD on a QMOD macro at 512 samples | about 0.012% (measured per call) | about 0.06% (guessed) |
| 24 MODs on QMOD macros at 512 samples | about 0.3% | about 1.4% (guessed) |
| Extra chunks a second, busiest song, one MOD on | counted in batch 1 | — |
| The extra chunk cuts with helpers on | measured on the busiest song (`app/bench_run.cpp:220`) | SPEED TEST row |

**The JX-3P's own LFO stays apart** (`jx3p-plan.md:338-347`):

| | JX-3P LFO | Track MOD |
| --- | --- | --- |
| Lives in | The patch (settings 24-26), saved in presets | The track, saved in the song |
| Rate | Free Hz, its 10 ms tick, a DELAY from the key | Note lengths, from song position |
| Moves | Pitch and the filter, inside each voice | The centre of a macro, effect setting or send |
| Random | Seeded, put back on `allNotesOff` | Hashed from position |

- **They share no code and they add.** A MOD on JX-3P macro 5 "LFO"
  (`jx3p-plan.md:465`) swells the synth's own vibrato in time with the song.
- **Do not sync the JX's LFO to the song**: it would stop being a clone.
- **TREMOLO** (planned, `docs/plans/effects-plan.md:60`) overlaps a MOD on
  LEVEL or PAN. **Owner's call** whether it is still wanted. If kept, it takes
  its shape and rate words from `mod_rule.hpp`.
- The effects plan's "a filter that wobbles in time" (`effects-plan.md:76`)
  is a MOD on FILTER CUTOFF, reachable from batch 1.

## 7. The screens

### The fast way: on the knob (batches 3 and 4)

**Hold X on a macro (DEVICE MAIN), an effect setting or a SENDS row (FX) is
"what it does" to that knob.** X still hears, as now
(`app/synth_screen.cpp:246`, `app/fx_screen.cpp:408`), and the MOD's fields
(KIND, DEPTH, RATE, SHAPE, PHASE) show while X is held, each chip in the
hints bar under its field (`ui::hintsUnder`, `app/ui.hpp:208`).

**Where the fields go is the owner's call: it is a change to a settled look**
(review, finding 5). `hintsUnder` only draws chips under fields that already
exist, and these screens have no room for them: DEVICE MAIN's macros fill to
`kAboveScope` (406), LIVE OUTPUT is 416-452, then the hints bar; an FX
setting's row is `kSetH` 74. Recommended, nothing moved or resized:

| Screen | While X is held on a knob |
| --- | --- |
| DEVICE MAIN | **The fields take the LIVE OUTPUT strip's place** (416-452), and the strip comes back, pixel for pixel, when X is let go |
| FX | **The fields take the heading's place** (`kHeadH` 93) |

Stills of both go to the owner before batch 3 is built.

**The order of the checks is the trap** (review, finding 12). As on the
grids, the X-held check comes before each button's own job:

| Button | Alone, today | With X held | Test |
| --- | --- | --- | --- |
| Y on DEVICE (QMOD) | ROLL (`synth_screen.cpp:214-217`) | The dice | Y pressed before X still rolls the sound |
| L1 / R1 on FX | Track before / after (`stepTrack`) | PHASE / LENGTH | L1 before X still steps the track |
| The stick on DEVICE | Moves the cursor when A is not held (`synth_screen.cpp:262-285`) | **Does nothing** (the cursor must not leave the knob) | X held, stick pushed: same knob |

| With X held on a knob | Does |
| --- | --- |
| UP / DOWN | DEPTH. **The first push on a knob with no MOD takes the first free MOD: LFO, SINE, 1 BAR, ±25%** |
| LEFT / RIGHT | RATE, a click a push |
| A | Steps the KIND and SHAPE (as X + A is THROW on a hit) |
| L1 / R1 | PHASE, or a stepper's LENGTH (as X + L1 / R1 is GLIDE FROM on a note) |
| Y | **The dice** (Hold X + Y is the house dice): a stepper's row rolled, or an LFO's shape and rate (depth kept). Owner's call what it rolls |
| Hold B | **Deletes the MOD**, with the filling bar (the house rule for deleting) |

Both MODs taken and X held on a third knob: the strip says **MODS FULL** and
does nothing. (If the owner prefers one per knob, this goes away.)

| From "I want this filter to wobble" | Presses |
| --- | --- |
| DEVICE MAIN, cursor on TONE | **1**: hold X + UP |
| FX, FILTER CUTOFF | R2 to FX, the slot's line, RIGHT to CUTOFF, hold X + UP: **4** |
| A MOD tab only (the other two designs) | about 7 |

### The MOD tab (batch 5)

**A new tab after FX: SONG, PATTERN, DEVICE, FX, MOD, SET.** The tabs are
`View` (`app/session.hpp:68`), stepped by L2 / R2 (`app/app.cpp:209-212`),
drawn in `app/ui.cpp:145`. `View` is not saved anywhere by number, so adding
one is safe. Only SET moves one press further. **Owner's call** (and where).
The top bar is drawn with the fixed fonts; **a sixth tab must fit it**:
checked first in batch 5.

Laid out as the FX screen: a list at the left (MOD 1, MOD 1 STEPS, MOD 2,
MOD 2 STEPS), the picked line drawn large at the right. No restyle.

| Button | On the MOD tab |
| --- | --- |
| D-pad / stick | Move; right goes into the settings, as on FX |
| Hold A + UP / DOWN | Turns a setting; KIND, TARGET, SHAPE, RATE click once a push |
| Hold A on TARGET | The list of targets, as the list of effects: MACROS, SLOT 1-3, SENDS |
| B | A setting back to its usual value |
| Hold B | Deletes the MOD (the filling bar) |
| X | Hears (held: until let go) |
| Y on STEPS | The dice: the row rolled and written (undoable) |
| L1 / R1 | Track before / after, as FX (`FxScreen::stepTrack`) |

**STEPS:** 16 bars, the cursor on one, hold A + UP / DOWN sets it, the
playing step lit from `modShown`.

### What moves

- On the MOD tab: a small picture of the shape with a dot at "now", or the
  row with the playing step lit.
- **On the target's own bar** (DEVICE macro, FX setting): a thin brass mark
  where the value is now, beside the purple "moves with the pattern" line
  (`app/fx_screen.cpp:700-703`). **Owner's call: a new mark on a settled
  look.** Recommend yes; colour by job only.

**Hints must fit.** DEVICE MAIN's bar is full; "HOLD X: MOD" goes in only if
`gHintsOver` stays 0, else on the help card. The X-held strip has its own
test in `tests/under_test.cpp`.

**Stills** (each in `kViews`, `tests/stills.hpp`; each at XL and in PAPER):
`device-mod-held`, `device-mod-full`, `fx-mod-held`, `fx-sends-mod-held`,
`mod`, `mod-lfo`, `mod-steps`, `mod-target --hold-a`, `mod-deleting`,
`mod-12`, `device-modded`, `fx-modded`.

## 8. Tests and batches

**House ways:** CHECK macros, one program per area, `TIMEOUT 120` each;
**mutation testing after every batch** (break the combine, the seed, the
chunk cut, the hold, the "only while on": each must fail a test);
`effects_test --print` the same before and after; block lengths 1, 7, 48,
64, 333, 512 and helpers 0 and 3, the same to the bit **on songs without HALL
or Twin Analog** (neither is block-exact; section 9); drawn checks through
`tests/canvas.hpp`.

| Batch | One agent does | Ends with |
| --- | --- | --- |
| **0 Groundwork** (**built 2026-11-14, branch `mods`: section 12**) | (a) `engine/include/apotheca/mod_rule.hpp`, no SDL: target ids, rate table, shapes, `lfoValue(settings, position)`, `stepValue(settings, steps, position, seed)` with SLEW and CHANCE, `kRollMod = 6`, the combine and clamp, positions on a fractional step. `tests/mod_test.cpp` against hand-worked values. (b) **`giveLane`: `applyAuto`, `SetMacro`, `SetEffectParam` and `SetSend` all give `clamp(centre + offset)` through it**, offset fixed at 0; centre below 0 skipped; a send's given value clamped while `autoNow` keeps the centre (tested with a fake offset in the test build). (c) **Phazed DETUNE**: no fade on a line-select change from DETUNE, a `third_party/CHANGES.md` line, the held-note test. (d) QMOD `setMacro` returns early on an equal value. (e) The review's `setMacro` timing program as a `bench` row | ctest green; **`effects_test --print` identical**; the DETUNE test fails with the mend taken out. **Nothing on screen; no sound change but the DETUNE cut** |
| 1 Engine (**built 2026-11-14, branch `mods`: section 13**) | `Track::Mod mods[2]`, `SetMod`, `SetModStep`, `ModAudition`, the tick and its cuts under `anyMod_`, macros every 8th tick, `runMods`, waiting on held slots, OFF on `RemoveInstrument`, offsets to 0 on Stop, `modShown` | Same bits across blocks and helpers **on tracks without HALL or Twin** (section 9); Play twice gives the same; tempo change keeps phase; scene move with a MOD running equals the song built; bounce against live phase; chunks a second counted; old FILTER's steps and the new effects' softening measured |
| 2 Song (**built 2026-11-14, branch `mods`: section 15**) | `ModData mods[2]` on `TrackData`, `Session::setMod`, `applyProject` sends them, `mod` / `modsteps` words, undo through `soundMark`, the TYPE carry, another family or a choice clears (on load too). `tests/modapp_test.cpp` | Old songs and everykind as they were; round trip; NaN and 1e300 held |
| 3 DEVICE knob (**built 2026-11-15: section 18**) | The X-held fields in LIVE OUTPUT's place (once the owner has seen the still), MODS FULL, hold B delete, the dice, the MOD heard while stopped, X checked before Y and the stick | `under_test`, the three order tests (section 7), stills, hints at 0, `textviews`, `themeviews`, `ctest -R words` |
| 4 FX knob (**built 2026-11-15: section 18**) | The same on slot settings and SENDS, in the heading's place; X checked before L1 / R1 | The same |
| 5 MOD tab (**built 2026-11-15: section 20**) | The tab, both MODs, the list of targets, STEPS, L1 / R1 | The same, plus the top bar at every size |
| 6 Marks | The brass mark on DEVICE and FX (if the owner wants it), the shape picture | Pixels read in BROWN and PAPER |
| 7 Depth lanes | Lanes 22 / 23 as MOD DEPTH automation (owner's call) | Scene move and undo tests |
| 8 LEVEL, PAN | Targets 32 / 33 | `mixer_test`; a song with no MOD untouched (`runMaster`) |
| 9 Cost | The busiest song with 24 MODs in `bench_run.cpp`; SPEED TEST shows it | Figures in CLAUDE.md "Measured"; **the handheld's only when the owner runs it** |
| Later | CELL / NOTE restart, pitch, choice targets, presets | Only if the owner says so |

**"Groundwork" as the owner asked = batch 0, then 1 and 2.** None of the
three shows anything on the device.

## 9. Risks and the owner's calls

| Risk | What could go wrong | Answer |
| --- | --- | --- |
| QMOD `apply()` per change | Too costly on the handheld (about 10% at 64 samples for 24 MODs) | Macros every 512 samples (about 1.4%, guessed); QMOD's early return; the handheld's figure only from SPEED TEST |
| Phazed DETUNE at exactly 0.5 | Every held note cut (measured in a copy) | Mended in batch 0, with a test. **It also changes old songs whose DETUNE automation passed 0.5: their notes stop being cut there** |
| Old FILTER stepped | A zip on CUTOFF; its arithmetic is pinned, no glide may be added | A test bounds the step; **nobody has listened**. A newer filter TYPE would glide |
| Newer effects' glide | A fast MOD loses depth | Measured and stated per rate |
| Extra chunk cuts | Helper wake-ups cost: about one extra chunk per note end, waiting note, arpeggio, ratchet and `render()` end | Only while a MOD is on; chunks a second counted in batch 1 |
| Today's automation is not block-exact | `glideAutomation` moves lanes at chunk starts (`engine.cpp:1774-1789`), so a MOD on an automated lane inherits that through the centre | Left as is (mending it changes old songs). Owner's call. Tests hold MODs block-exact on lanes without automation |
| **Turning one MOD on changes every track a little** | The extra cuts re-cut every track's synth and effects, and some are not block-exact: HALL ("does not pass it yet"), Twin's render splits, the old five's automation. So a song with one MOD on differs to the bit, on every track, from the same song with it off | Accepted as the price; said here plainly. **A song with no MOD on is still the same to the bit.** The block-exact tests avoid HALL and Twin |
| Bounce against live loop | A MOD in a later scene is in another phase in the bounce than looped live | Said to the owner; tested both ways (section 3) |
| A sixth tab | Does not fit the top bar | Checked before batch 5 |
| JX-3P in progress | Twin's macro targets dropped on load | The same rule as Twin's macros |
| The owner cannot hear | — | Everything is tests and stills, and said so |

| # | The owner decides | Recommended |
| --- | --- | --- |
| 1 | Two general MODs a track, one per knob (no cap), or three | **Two** |
| 2 | One kind with shapes, or LFO and STEPPER as kinds | **Two kinds** (a STEPPER has a row to write; RANDOM and DRIFT stay LFO shapes) |
| 3 | Names on the device | **MOD 1 / MOD 2; LFO, STEPPER** |
| 4 | Set up on the knob (hold X), a MOD tab, or both | **Both**: the knob first |
| 5 | Where the MOD tab goes | **After FX** |
| 6 | Free Hz rates | **No** |
| 7 | Default RESTART once CELL exists | **PLAY** |
| 8 | Run while stopped (X held on the knob hears it) | **Yes, with the gesture** (batches 3-4): without it the 1-press gesture is silent when stopped |
| 9 | Per track, or per scene | **Per track**; depth per scene via lanes 22 / 23 |
| 10 | Spend lanes 22 / 23 on MOD DEPTH | **Yes, batch 7** |
| 11 | Around the knob, or up only | **Around** |
| 12 | Choice targets (random OCTAVE) | **Later, STEPPER only** |
| 13 | Pitch, quantised to a scale | **Later, its own feature** |
| 14 | LEVEL / PAN as targets, or leave them to TREMOLO | **Targets (batch 8); drop TREMOLO then** |
| 15 | What X + Y rolls | **A stepper's row; an LFO's shape and rate** |
| 16 | Turing CHANGE (rolls kept until rolled again) | **No**: CHANCE plus Y |
| 17 | MODs in presets | **No** |
| 18 | A moving brass mark on the target's bar | **Yes, thin** |
| 19 | Put old automation on the tick grid | **No** (changes old songs) |
| 20 | Where the X-held fields go (a change to a settled look) | **DEVICE: in LIVE OUTPUT's place; FX: in the heading's place**, only while X is held. Stills first |
| 21 | Mend Phazed DETUNE's note cut (old songs whose DETUNE passed 0.5 change: notes no longer cut) | **Yes** |
| 22 | A bounce and a live loop may wobble out of phase in later scenes (Play counts from the scene worked on) | **Accept it for now**; CELL restart later if it matters |

## 10. The three designs, judged

| Wish | Hands (one LFO per knob, X held) | Engine (2 MODs, MOD tab) | Music (2 MODs, MOD tab, more targets) |
| --- | --- | --- | --- |
| Speed on the handheld | **Best**: 1 press | A tab and a list; Y on FX as a shortcut | A tab; X + Y shortcut later |
| Chance and dice | Shapes only, no row to write | **Row, CHANCE, Y** | Row, Turing CHANGE (needs memory) |
| Button standards | X held = what it does; B tap deletes (breaks "hold B") | Kept | Kept |
| Songs keep loading | Yes | Yes | Yes |
| Cost on the handheld | Unbounded LFO count | **Bounded** | Bounded |
| Small safe batches | Good | **Best**: batch 0 changes nothing | Batch 0 is the whole engine |
| Weak spot | The stepper the owner asked for is thin | A tab press for everything | LEVEL / PAN ids done best; most to build |

**Spine: the engine design.** Grafted: the hands design's knob gesture (as
the first way in), its WRITE rule and moving mark; the music design's ids
32 / 33, its box survey, and testing RESTART on the event. Dropped: the
hands design's B-tap delete (the house says hold B), unlimited LFOs and its
128-sample tick; the engine design's Y-on-FX shortcut (X held on the knob is
faster); the music design's stepper memory.

## 11. Decisions on the review (2026-11-14)

| # | Point | Decision |
| --- | --- | --- |
| 1 | Four paths hand a lane's value out, not only `applyAuto`; a new effect's centre is unknown | **Accepted.** One `giveLane` for `applyAuto`, `SetMacro`, `SetEffectParam`, `SetSend` in batch 0; `runMods` skips a centre below 0 (sections 2, 4) |
| 2 | Phazed DETUNE at exactly 0.5 cuts every held note (measured) | **Accepted.** Mended in Phazed in batch 0, with a test and a `CHANGES.md` line; a guard off 0.5 is the fallback. Old songs whose DETUNE passed 0.5 change: the owner's call 21 |
| 3 | QMOD, not Phazed, is the cost risk | **Accepted.** Synth macros every 512 samples; QMOD's early return; the cost table has QMOD rows; the review's program becomes the `bench` row. N is no longer left open |
| 4 | COMB, VOWEL, EQ, `svf.hpp` and the other six are on main at 0.442 | **Accepted.** The "branch newfx" line is gone; the nine are in the table, with COMB PITCH and RING WAVE named as choices; the old FILTER's zip risk now has a way round |
| 5 | DEVICE MAIN and FX have no place for the X-held fields | **Accepted.** Named as the owner's call 20: LIVE OUTPUT's place on DEVICE, the heading's on FX, only while X is held; stills before batch 3 |
| 6 | The gesture is silent while stopped | **Accepted.** X held on a modulated knob runs its MOD from X's press (`ModAudition`, batch 1 engine, batches 3-4 gesture). Call 8 changed to yes |
| 7 | The position formula reads `untilStep_` after the chunk is taken off | **Accepted.** Read before, or the chunk added back; fractional `untilStep_` stated for the test fixtures |
| 8 | "One extra cut per `render()`" undercounts | **Accepted.** About one per event; batch 1 counts chunks a second on the busiest song |
| 9 | One MOD on changes every track slightly | **Accepted.** Said plainly in section 9; block-exact tests avoid HALL and Twin. A song with no MOD on stays the same to the bit |
| 10 | Bounce and live loop can be out of phase | **Accepted.** Said in section 3 and as call 22; tested both ways |
| 11 | No rule for a choice target after TYPE change or load; DEPTH lanes need a mapping | **Accepted.** A kept setting that is a choice sends the MOD OFF, on load too; lanes 22 / 23: 0.5 no depth, 0 is -100%, 1 is +100% |
| 12 | The order of the checks on Y, L1 / R1 and the stick | **Accepted.** X-held first; three tests named in section 7; the stick does nothing while X is held |
| 13 | Batch 0: the cost row is answered; the send clamp and the DETUNE mend are missing | **Accepted.** Batch 0 now holds `giveLane`, the send clamp with the centre kept, the DETUNE mend and QMOD's early return; the cost row records the review's figures |

Nothing was rejected. Nothing above was built, run on the handheld or heard;
the review's two measurements were made in a copy on the desk.

## 12. Batch 0, as built (2026-11-14, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld.** No screen
changed. The owner's calls were taken at the recommended answers (two MODs,
two kinds, PLAY restart, DETUNE mended).

| Part | What is there |
| --- | --- |
| (a) The rule | `engine/include/apotheca/mod_rule.hpp`: kinds (OFF 0, LFO 1, STEPPER 2), targets = lanes 0-21 (`modTargetKnown`; 22 / 23 never; LEVEL 32, PAN 33 named, not taken), twelve rates by number with their steps as a fraction (`modRateInfo`; the order a player sees is `modRateAt`, the same today), seven shapes, PHASE in quarters, RESTART (PLAY only), `kModTick` 64 and `kModMacroTicks` 8, `ModSettings`, `modPosition`, `lfoValue`, `stepValue` (SLEW, CHANCE), `modOffsetAt`, `modGiven`. `kRollMod = 6` in `ChordRoll`. Unknown numbers read as OFF, SINE, 1 BAR |
| Its choices, made here | SINE and TRIANGLE start at 0 going up (so a MOD at Play gives the centre); the RAMPs and SQUARE start at an edge. RANDOM rolls `chordSeed(track, 1, 0, cycle)`; a STEPPER `chordSeed(track, 0, place, timesRound)`; `which` is `mod * 32 + what` (0 the chance roll, 1 the value). DRIFT is a cosine from one cycle's roll to the next. SLEW is a straight line over that share of the held time, from the value before worked out again; **the first value after Play does not glide in**. `ModSettings::on()` is false at depth 0 |
| (b) `giveLane` | `Engine::giveLane(track, lane)` is the one way out: `applyAuto` (automation, Stop) calls it, and so do `SetMacro`, `SetEffectParam` and `SetSend` for a lane they own (a macro over 7 and a master's or bus's setting go straight, as before). It gives `modGiven(autoNow, modOffset)`: **offset exactly 0 gives the centre as it is** (not clamped: a macro command of 1.6 still reaches the synth as 1.6, as before), else centre + offset held to 0..1. A centre below 0 with an offset gives nothing. `autoNow` is the centre and is never the wobble; a send's centre is held to 0..1 as before, its given value too. `Track::modOffset[24]`, all 0: only `Engine::setModOffsetForTests` sets it (batch 1's `runMods` will, and should take that hook's place) |
| (c) DETUNE | Mended in Apotheca's `engine/src/phazed.cpp`, no file under `third_party/` changed (a `CHANGES.md` line says why it is listed). A one-line sound DETUNE made a twin **stays a twin at 0.5 while any voice sounds** (its second line not detuned, so as loud as just off the middle), and goes back to one line before the next note that finds the synth silent (`lineBack` in `noteOn` / `noteOnFrom`). **Still a cut, once**: DETUNE moved OFF the middle while a one-line note sounds (the layout changes from one line to two). With nothing sounding, 0.5 is one line exactly as before |
| (d) QMOD | `setMacro` returns early on the value it has (after the clamp). `Qmod::applied()` counts the wirings worked out, for the test |
| (e) The cost row | `bench`: "a macro changed", the synth alone, 8 notes held, best of five |
| Found on the way | `engine_test` crashed with no word (5 runs in 6): `testNudgeMovesAHitOffTheGrid` kept six Engines on the stack, 8.37 MB, over the main thread's 8 MB once the Engine grew by 1.1 KB. Its three and `testLaneLevel`'s three are on the heap now; the largest test frame anywhere is 4.2 MB. **Batch 1 grows the Engine again**: keep it that way |

**Measured** (desk, one `bench` run, a second build running beside it):

| Thing | Figure |
| --- | --- |
| Phazed, a macro changed | 0.358 us a call (0.002 the same value). 24 MODs: 0.08% of a core at 512 samples, 0.64% at 64 |
| QMOD | 0.949 us a call (0.002 the same value, was a whole apply). 24 MODs: 0.21% at 512, 1.71% at 64 |
| Twin Analog | 0.053 us (0.002). 24 MODs: 0.01% at 512, 0.10% at 64 |
| In the same run | Phazed 8 notes 0.7%, QMOD 8 notes 2.8%, DRIVE 0.12%; the busiest song 23.3% of a block alone, 15.1% with 2 helpers (a busy desk) |
| `effects_test --print` | identical before and after, every line, the whole song's checksum too |
| ctest | 34 of 34 (the new `mod`, `TIMEOUT 120`) |

**Mutants** (each made, seen to fail, put back): `mod_test` killed 22 of 22
(giveLane ignoring the offset; an unknown centre given; a send's centre taking
the wobble; each of the four paths going round `giveLane`; no clamp; a clamp
with nothing added; the position a step late; PHASE backwards; RANDOM from
the next cycle; DRIFT straight (survived at first: the test now checks a
quarter in and that it leaves a value gently); a STEPPER rolled by its index,
not its time round; CHANCE ignored; the first value gliding in; no SLEW; the
time round before Play; a written value not held; depth not held; TRIANGLE
from the bottom; a rate's length). `phazed_test` killed 3 of 3 (no twin kept;
`lineBack` doing nothing; "sounding" not counting playing voices). `qmod_test`
killed the early return taken out.

**What only the tests saw**: everything. The DETUNE mend's sound (a twin at
the middle, 3 dB over one line alone, the same as just off it) is measured by
RMS against a twin engine, not heard. The owner's call 21 is taken: old songs
whose DETUNE passed 0.5 with notes held no longer cut them there.

**For batch 1**: `runMods` replaces `setModOffsetForTests` (writes
`modOffset`, then `giveLane` where the given value changed); `modPosition` is
read before `untilStep_` is taken off. Stop puts the offsets to 0 and gives the
centre (`settleAutomation` only gives a lane whose centre moved: with an
offset that is not enough).

## 13. Batch 1, the engine, as built (2026-11-14, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld, no screen
changed.** A song with no MOD on is the same to the bit (`effects_test
--print` identical before and after, every line, the whole song's checksum
too). The owner's calls stay at the recommended answers.

| Part | What is there |
| --- | --- |
| State | `Engine::Track`: `ModSettings mods[2]` (OFF until told), `std::atomic<float> modShown[2]` (what each adds, as of its last tick; 0 while it does not run), `std::atomic<int> modPlace[2]` (a STEPPER's place, for batch 5's lit step; -1 otherwise), `slotChoices[3]` (a bit a setting: which settings of each slot's effect are choices). A fixed size: the audio thread never allocates. The Engine grew about 3 KB |
| Commands | Appended after `HoldEffectAutomation`: `SetMod` (track, a = MOD, b = field, value), `SetModStep` (a = MOD, b = place, value), `ModAudition` (a = which MODs run, a bit each; 0 lets go). The fields are `ModField` in `mod_rule.hpp` (KIND 0 ... CHANCE 9, appended) and are held to what they can be there (`modSet`, `modSetStep`: a number nobody knows is OFF / no target / 1 BAR / SINE, a depth -1..1, a float rounded to the nearest whole). `Engine::setMod(track, mod, settings)` sends the row, then the fields, KIND last, through `send()` |
| The tick | `render()`: while playing and some MOD is on (`anyMod_`), or stopped with an audition, a tick every `kModTick` (64) samples counted from each step's first sample (`sinceStep_`, set to 0 where the step fires); the chunk is cut on the next tick. The position is read **before** the chunk is taken off `untilStep_` (`modPosition`). Synth macros on every `kModMacroTicks`'th tick (512 samples), effect settings and sends on every one: 94 ticks a step at 120 BPM, 12 of them macro ticks |
| `runMods` | On the audio thread, before `renderChunk` shares the tracks out. Each MOD that runs works out `modOffsetAt` at the position; two on one lane add, then `giveLane`'s one clamp; a lane is given only if its offset changed (a held STEPPER value: one compare). Passed by: a track with no instrument, a slot the FX screen is trying effects in (`autoWaits`), a setting that is a choice (`slotChoices`, told by `Engine::setEffect` in `InstallEffect`'s `c`; a setup-call effect has none) |
| Letting go (`stillLane`) | Offset to 0 and the lane given its centre, where the centre is known. **Stop and Panic** do it to every lane (then Stop settles automation as before); a MOD aimed elsewhere or turned OFF does it to the lane it left; `HoldEffectAutomation` to the held slot's four lanes; an effect put in a slot zeroes its four (what was added to the one before is never carried to the next, a choice there included); **a track taken away** (`takeAway`) lets go, then its two MODs are OFF, as GLIDE FROM goes back to TOP |
| X held while stopped | `ModAudition`: the track's MODs asked for run from the moment X went down (`heardFor_`, as if Play had just been pressed), on the same tick; a = 0 lets go (each knob given its centre). Not taken while playing (the song's MODs run then: taking it would put the knob back for a tick). Stop and Panic let go; a second audition lets the first go |
| The bench | `bench`: the busiest song with 1 MOD and with 24, alone and with helpers, and chunks a second; working MODs out on twelve silent drum tracks (0, 1, 24) |
| Tests | `mod_test` (the `mod` entry, `TIMEOUT 120`; about 2.4 s): the batch 0 engine tests now run real MODs (`setModOffsetForTests` is gone), and fourteen more: the fields, the tick and the macros' tick against the rule, two on one setting, Play twice (every value given, in order, in other blocks), a tempo change (the bar line still the ramp's start, to 90 and to 171), the same bits in any blocks and helpers, a gliding effect's difference bounded, chunks cut only while a MOD runs, a scene put in equal to the song built, a bounce against a live loop, the FX screen's hold, choices, a track taken away, X held while stopped, Panic, the old FILTER's stairs, a newer effect's softening. `mod_test --measure` prints what is measured |

**Found, and said plainly:**

- **A MOD on an effect that glides a setting a CALL at a time is not the
  same to the bit cut into other blocks.** CHORUS, COMP, DUCK, HALL, PING,
  PLATE, SATURATE, SPRING and TAPE begin their `fx::Glide` in each
  `process()`, so they are block-exact only once their settings rest, and a
  MOD never lets them rest. Measured on SATURATE DRIVE and CHORUS DEPTH under
  a MOD, two bars at 133: **at most 0.0024 apart** (blocks of 7 against 64;
  0.0011 at 333) on a peak of 0.5, about 46 dB down. A bounce under such a
  MOD differs that much from live play. Held in `mod_test`
  (`testAGlidingEffectUnderAModIsCloseInOtherBlocks`: over 0, under 0.005).
  The TICK effects (COMB, VOWEL, EQ, FOLD, FUZZ, the newer MOD, DELAY and
  REVERB types, LIMITER, SNAP, RING, WEAR), the old FILTER, the sends and the
  synths' macros are the same to the bit. Not mended: it would change how
  those nine glide, which is their sound. **Owner's call.**
- **A song at a tempo whose step is not a whole number of samples is not
  always the same to the bit in other blocks, with or without a MOD, as on
  `main`** (corrected in review, section 14: this was blamed on Phazed). At
  133 BPM a step is 5413.5 samples, and a note that ends or waits then lands
  a sample apart by how the sound is cut (about 4e-4 at a note's start). At
  120 (6000 samples) Phazed is exact in blocks of 1. The block test runs
  blocks of 1 at 133 without the Phazed track, and with it at 120.
- **Helpers never change a bit**, MOD or none (0 and 3, every song tried).

**Measured** (desk; `bench` twice, a second build running beside it the
first time; `mod_test --measure`):

| Thing | Figure |
| --- | --- |
| Chunks a second, the busiest song | 1030 with no MOD, **1285 with one or 24** (+25%: the tick's cuts where steps, notes and the 256-frame blocks do not fall on it) |
| The busiest song, real time, 256-frame blocks: no MOD / 1 MOD / 24 MODs | Alone 21.7 / 24.3 / 22.5% of a block; 2 helpers 14.2 / 14.4 / 15.6% (the second run; the first, busier: 25.1 / 24.8 / 28.3% and 15.7 / 17.3 / 15.7%). **Within the desk's noise**: the difference is not resolved by this row |
| Working MODs out, twelve silent drum tracks, on the sends | No MOD 0.18%; **1 MOD +0.028%, 24 MODs +0.096%** of a core, both runs. **Over the plan's targets** (0.02% and 0.03%): one MOD's cost is the extra chunks, 24's the shapes and hashes too. For batch 9 (Cost) |
| In the same run | Phazed 8 notes 0.7%, QMOD 2.8%, DRIVE 0.12%; a macro changed 0.36 / 0.94 / 0.05 us (Phazed / QMOD / Twin), as batch 0 |
| Ticks in four steps at 120 | 376 (94 a step), 48 of them macros (12 a step) |
| A tempo change mid-bar (120 to 90, and to 171) | At the next bar line a 1-BAR RAMP gives -0.499995 at depth 50%: its start |
| Chunks in a bar at 120, blocks of 512 | 1516 with no MOD, 1692 with one on |
| The old FILTER's CUTOFF under a SINE at 50% | Its largest stair, a tick apart, as a share of the knob's travel: 1/2 STEP 0.067, 1 STEP 0.034, 1/8T 0.025, 1/8 0.017, 1/4T 0.013, 1/4 0.0084, 1/4. 0.0056, 1/2 0.0042, 1 BAR 0.0021. Each is depth x 2 pi x 64 / the cycle's samples. **Nobody has listened** |
| A newer effect's Glide (20 ms / 30 ms) under a SINE, at 120 | Kept of the swing: 1/2 STEP 43 / 31%, 1 STEP 69 / 54%, 1/8T 79 / 65%, 1/8 89 / 79%, 1/4T 93 / 87%, 1/4 97 / 93%, 1/4. 98.5 / 97%, 1/2 99.2 / 98.2%, 1 BAR 99.8 / 99.5%, longer all. **A fast MOD on CHORUS, TAPE or the like is mostly smoothed away**; on the old FILTER, a TICK effect or a send it is not |
| Play twice | The same 16 macro and 1433 setting values, in order, in blocks of 512 and of 333 (forty steps, a RANDOM LFO and a STEPPER at CHANCE 60%) |
| `effects_test --print` | identical before and after |
| ctest | 34 of 34 (twice; once `effects2` and `nextscene` passed 120 s under a load of 9 on 4 cores, and passed again alone in 75 s) |

**Mutants** (each made, seen to fail, put back; a script, the file copied
back and touched each time): `mod_test` killed **30 of 30**: the cut taken
always while playing; the position read a chunk late; macros every tick; the
ticks not counted from the step; a held lane not passed by; the hold not
letting go; a choice moved; an effect put in keeping the offset; a MOD aimed
elsewhere keeping the old lane's; Stop, Panic, a track taken away not letting
go; its MODs kept; the audition ignoring which MODs, not counted from X, or
taken while playing; the place not stored; `modsChanged` not called;
given every tick; two not added; the row not sent; no cut on the tick; the
choices not told; the shown value not stored, or kept when OFF; and in the
rule, the depth not held, the place before Play, a float not rounded
(survived at first: the test now sends 1.6), the target not checked, a step
not held. One change had no test that could see it and was taken out: Play
letting an audition go (Stop and Panic do, and Play's first tick works every
lane out again).

**What only the tests saw**: everything. No MOD has been heard.

**For batch 2**: `Engine::setMod` is the way to send a track's two MODs
(`applyProject`, undo); `RemoveInstrument` already puts them OFF in the
engine. The choice rule on load and on a TYPE change is the app's (the engine
only refuses to move a choice; it does not turn the MOD off).

## 14. The review of batches 0 and 1, mended (2026-11-14, branch `mods`)

**All five findings were real. Three are mended in code, one in the words,
one by running what was only worked out. Nothing heard, nothing on the
handheld, no screen changed.** `effects_test --print` is still identical to
the merge base (42 lines, the whole song's checksum too).

| # | Finding | Fate |
| --- | --- | --- |
| 1 | A preset chosen while a MOD runs on a macro dropped the MOD, then moved it about the old knob | **Mended.** `Command::SelectPreset` sets the eight macro lanes' centre to the middle (`kPresetMacro` 0.5, where every synth's `selectPreset` puts its macros and `Session::chooseSynthPreset` the project's) and gives each lane a MOD is adding to, at once. With nothing added no macro is given. `mod_test` `testAPresetUnderAModMovesAboutTheMiddle` (a spy synth that resets on a preset, as the real ones) |
| 2 | A legato Phazed part stayed 3 dB up after DETUNE came back to exactly 0.5 | **Mended, with a call for the owner.** A note that BEGINS on a twin kept at the middle has its two lines in tune and in phase (6 dB over one line, not the 3 a detuned pair averages), so from that note's start the twin is trimmed 3 dB more (`kDoubleInTuneTrim` in `phazed.cpp`; 0 turns it off), and the trim is given back when DETUNE leaves the middle. Not taken when DETUNE merely reaches or passes the middle: a held note's lines are wherever their beat left them, and trimmed there it fell to 0.61 to 0.80. The CZ glides its volume over 10 ms, so no click. Measured: a legato part at the middle +0.02 dB against DETUNE never moved (17 presets, plain notes and glided; it was +3.02) |
| 3 | Ten of 18 mutants survived | **Tests added; 9 of the 10 are killed now, one is equivalent.** See below |
| 4 | "Phazed is not the same in blocks of 1" was the tempo, not Phazed | **Words mended** (section 13, CLAUDE.md, the test's comment); `testTheSameInAnyBlocks` also runs the song WITH Phazed at 120 in blocks of 1, 64 and 333 with 3 helpers, MODs and none |
| 5 | The "24 MODs on macros" cost was arithmetic | **Run now**: `bench` has "24 MODs on macros, run" (twelve silent tracks of the synth, a SINE and a STEPPER on two macros of each, through the engine). The worked-out row stays beside it |

**The owner's call in finding 2.** Just off the middle (DETUNE 0.52) a
twin's short notes are already 1.2 to 3.0 dB over one line, on `main` too:
they begin with their lines together and the beat is slow. So a part at
exactly 0.5 on a kept twin could be left like its neighbours (3 dB up, no
trim) or made as loud as the patch (the trim, what `main` gave once its cut
was over). **Taken: as loud as the patch.** `kDoubleInTuneTrim = 0` is the
other answer.

**Also changed by finding 1, said plainly**: a built-in preset chosen while
the song plays now resets the automation centre of the eight macros too
(with no MOD: no macro given, the synth is already there). Before, the
engine still held the old knob as the centre, and Stop could give it back to
a synth the project had at the middle. A song loaded and played is untouched
(`applyProject` sends every macro after the preset).

**Mutants** (each made, built, seen to fail, put back; `git diff` read before
the commit), 20 this round, **19 killed, 1 equivalent**:

| Mutant | Killed by |
| --- | --- |
| `stillLane` giving an unknown centre | `mod_test` (`testAnUnknownCentreIsLeftAlone`: an audition and a Stop on an effect that has said nothing) |
| Ticks while stopped whenever a MOD is on; `takeAway` keeping the audition | `mod_test` `testStoppedIsCutAsAlways` (100 chunks for 6400 frames, after an audition let go part way into a tick) |
| `takeAway` not counting the MODs again | `mod_test` `testTheLastModTakenAwayStopsTheCuts` (chunk for chunk as an engine that never had one) |
| No instrument check in `runMods` | `mod_test` `testATrackWithNothingRunsNoMod` |
| `keepTwin` on a new sound; `noteOn` / `noteOnFrom` not going back to one line | `phazed_test` `testATwinKeptAtTheMiddleGoesBack` (`Phazed::twinned()`) |
| QMOD comparing before the clamp | `qmod_test` (3.0 twice) |
| The preset: centre not followed; the MOD not given at once; every macro given; only lane 0 | `mod_test` `testAPresetUnderAModMovesAboutTheMiddle` |
| The trim: not taken in `noteOn`, in `noteOnFrom`; kept off the middle; taken as DETUNE lands; taken on any twin; forgotten when another macro turns | `phazed_test` (`testALegatoPartAtTheMiddleIsAsLoudAsTheMiddle`, `testTheInTuneTrimIsGivenBack`, `testDetuneThroughTheMiddleKeepsTheNote`) |
| `sounding()` ignoring fading voices | **Equivalent**: in `cz_synth.c` a voice is only ever marked fading while active, and both are cleared together (every write read). No test can see it |
| A send's given value not clamped (the review's own) | Equivalent, as the review said: the centre is already held to 0..1 |

**Measured** (desk, a second build running beside it; `bench` twice):

| Thing | Figure |
| --- | --- |
| 24 MODs on QMOD macros, run: twelve silent QMOD tracks, none / 24 | 0.71 / 0.98% and 0.73 / 0.99% of a core: **+0.26 to +0.27%** (worked out from one call: 0.21%; the rest is the tick's extra chunks) |
| ...on Phazed macros | 2.41 / 2.56% and 2.32 / 2.67%: **+0.15 and +0.36%** (worked out: 0.08%). Twelve silent Phazed tracks cost 2.3 to 2.4% by themselves, so this row is noisy |
| MODs worked out on the sends, 1 / 24 | +0.028 / +0.086% in the quieter run (as batch 1) |
| A legato Phazed part at the middle against DETUNE never moved | +0.02 dB, every one of 17 presets, plain and glided (was +3.02) |
| DETUNE off the middle again, or only passing it | 0.00 dB against a twin left alone |
| A held note as DETUNE reaches the middle | 0.86 to 1.10 of the twin left alone (the beat); with the trim taken there it was 0.61 to 0.80 |
| `phazed_test` | about 17 s (the legato parts are most of it) |

**Left for the owner**: `kDoubleInTuneTrim` (above); the nine effects that
glide a call at a time under a MOD (section 13); the cost over the plan's
targets (batch 9).


## 15. Batch 2, the song, as built (2026-11-14, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld, no screen
changed.** No file under `engine/` changed, so a song with no MOD on is the
same to the bit (`effects_test --print` identical to the merge base's, 42
lines). A song with no MOD on is written as it always was (no `mod` line).

| Part | What is there |
| --- | --- |
| The data | `TrackData::mods[kModsPerTrack]`, a `ModSettings` each (the engine's own struct: no separate `ModData`). **A MOD that is OFF is a blank one**, so "OFF" is one thing to compare and nothing about it is kept. TrackData grew 208 bytes |
| What a MOD may be aimed at | `app/mods.hpp`, the one rule (`modTargetFits`): a send, any track; a slot's setting that is there and is an amount (`effectChoices` 0); a macro on a track with a `SynthSpec` (not drums, not the sampler) that is not a choice (`macroIsChoice`: Phazed CHORUS and OCTAVE, QMOD OCTAVE; Twin has none). Anything else, lanes 22 / 23, LEVEL and PAN: **the MOD is OFF**. `modHeldFor` holds every field (`modSet`) and applies it |
| `Session::setMod(track, mod, settings)` | Holds and stores the whole MOD, tells the engine (`Engine::setMod`; OFF is one command), returns whether the track holds what was asked. The same again sends nothing. `Session::modData` reads it. No `noteEdit()`: `soundMark()` sees it, as GLIDE FROM |
| A song sent | `sendSong` sends every track's two MODs (after `RemoveInstrument` put them OFF), so a song opened, a resync after a full queue, a track deleted and another instrument all carry them |
| The song file | On a track, after `glidefrom`: `mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE` and `modsteps N LENGTH SLEW CHANCE v1..v16`. **DEPTH is 0..1, 0.5 none** (section 5's scale; `depthToFile` / `depthFromFile`, one place); the row's values are -1..1; SLEW and CHANCE 0..1. `mod` is left out when OFF; `modsteps` is left out when OFF **or when the row is as a new MOD's** (16 long, no slew, no chance, every value 0), so an LFO writes one line. Reading: KIND and TARGET must be there, the rest is as a new MOD's if missing; a number nobody knows is OFF / 1 BAR / SINE / PLAY / 0; every number held as a double first. **After the whole song is read each track's MODs are made to fit** (`modsFit`): a choice, an empty slot, a macro on drums read OFF, wherever the `mod` line stood |
| Undo | `soundMark()` mixes the MODs in; `restoreSounds` puts back a MOD whose `modText` (the lines the file writes) differs, after the slots, and tells the engine. A value the text rounds (a third) is left to the bit |
| TYPE change | `changeEffectType` carries a MOD aimed at the slot **by name** (`modTargetCarried`, through `effectCarryFrom`, as values and lanes go): the same MOD, another target. A setting not kept, or kept as a choice: OFF. `Session::setEffect` (another family, an emptied slot, the same kind put in afresh) puts OFF a MOD aimed at that slot |
| Another instrument | `setTrackKind` drops a MOD on a macro (with the macros' lanes); one on an effect or a send stays |
| Tests | `tests/modapp_test.cpp` (`modapp`, `TIMEOUT 120`, about 2 s): nine tests. The Session and what fits; an effect gone; **every pair of types of every family, every amount setting** (122 pairs: carried 220 times, 44 of them to another place in the slot; OFF 183 times, 4 where the name is a choice on the new type), nothing listed; the song file (the exact lines, round trip to the bit for every whole percent of DEPTH, Windows line ends, `cutSong`'s parts, 23 lines that read OFF, missing numbers, `1e300` / `nan` / `inf` in every place of both lines, a third MOD); a song opened and the next with none; a full queue mended; another instrument and a track deleted; undo and redo of each kind of change, of a TYPE change and of an effect deleted; what the text rounds. What the engine holds is read from `Engine::modOffsetForTests` a moment after Play |

**Choices made here** (each one place):

- **A MOD with no target that fits is OFF, so "KIND on, TARGET not yet
  picked" cannot be stored.** Batch 5's MOD tab either picks the target first
  or starts a MOD on a send (always fits). Stored half-made, it would vanish
  on save and on undo (the file reads an unknown target as OFF).
- A MOD at DEPTH 0 is kept and written (it is set up, and adds nothing).
- An LFO keeps a row written for it (a KIND changed from STEPPER and back).
- `Session::setEffect` with the kind already there (an effect put in afresh,
  settings back to usual) also puts its MOD OFF.

**Mutants** (each made, built, seen to fail, put back by a script; `git diff`
read before the commit): **49 made, 44 killed, 5 equivalent.** Five survived
at first and have tests now: a third MOD read, a third MOD's row read (both
wrote past the track's two MODs: the check is a `mod 3` at the end of the drum
track's part), an effect gone keeping its MOD (the first mutant did not
build), `setMod` on a track with nothing, `setMod` sending what it already
holds. Equivalent: DEPTH and a row's value not held on reading (`modsFit`
holds them again; the first hold is what keeps `1e300` from being made a
float); an empty slot "fitting" (no effect has no settings); the choice
check in `modTargetCarried` (`effectCarryFrom` already refuses a name that is
an amount on one side and a choice on the other); a bus's effect looking at a
track's MOD (`setMod` refuses a track below zero).

**Measured** (desk, one `bench` run, another build beside it):

| Thing | Figure |
| --- | --- |
| One frame, nothing changing | 0.056 ms (0.048 to 0.049 before: the frame's `soundMark` reads 208 bytes more a track; within this desk's noise) |
| A frame that ends an edit; undo, then redo; a knob and a hit, undone | 0.94 ms; 22.8 ms; 19.4 ms (0.62 to 0.75; 16.7 to 20.1; 18.0 to 34.2 before) |
| In the same run | DRIVE 0.12%, Phazed 8 notes 0.7%, QMOD 2.8%; 24 MODs on macros, run: Phazed +0.17%, QMOD +0.27%; MODs on the sends 1 / 24: +0.029 / +0.098% |
| Sizes | Project 11780 KB, TrackData 981 KB |
| `effects_test --print` | identical to the merge base's |
| ctest | 35 of 35 (the new `modapp`) |

**What only the tests saw**: everything. No MOD has been heard, and no
screen sets one yet: `Session::setMod` is called by the tests alone.

**For batch 3**: `Session::setMod` / `modData` are the way in and out; the
first free MOD is the first whose `kind` is `kModOff`; `modTargetFits` says
whether X + UP on this knob may take one (a choice may not).

## 16. Joined with `main` (2026-11-15)

Batches 0 to 2 were built before `main` gained JAX-EP's presets, Twin
Analog's removal, FEEL, the picker, AT END, the mixer and the effect type
memory. The join:

- **Twin Analog is gone; JAX-EP stands where this plan says Twin.** The bench's
  macro-cost row and the 24-MODs-on-macros row run on JAX-EP
  (`bench_run.cpp`); `modapp_test` aims its "every macro an amount" synth at a
  JAX-EP track. JAX-EP's `setMacro` does not return early on the value it has
  (Phazed's and QMOD's do). Its cost under a MOD is printed by `bench`, not
  recorded yet.
- **Phazed's DETUNE mend keeps `main`'s name**: `kTwinTrim` is `kDoubleTrim`
  on `main`; this plan's `kTwinInTuneTrim` is now `kDoubleInTuneTrim`.
- **Command ids moved** (memory only, never saved): `SetMod` 42 to 44,
  `SetModStep` 43 to 45, `ModAudition` 44 to 46, after `main`'s `SetSongEnd`
  (42) and `SetLaneFeel` (43).
- **Stop is `Engine::stopPlaying`**, which `main`'s AT END STOP calls too: the
  MODs are let go there (`stillMods`, the audition's mark), so a song that
  stops at its end lets go of its MODs as Stop does
  (`mod_test testASongThatStopsAtItsEndLetsGoOfTheMods`).
- **A preset chosen** sets the macro lanes' centre, glide and target together
  (`main`'s) and gives a lane a MOD adds to at once (this plan's).
- **A track taken away** forgets its lanes' FEEL and its MODs both.
- `soundMark`, `restoreSounds` and `sendSong` carry the MODs and FEEL both.
- Measured: `effects_test --print` the same on `main` and joined (a song with
  no MOD is the same to the bit). FEEL, chord effects and MODs in one song
  (`mod_test testFeelChordsAndModsInAnyBlocks`, 120 BPM, JAX-EP chords) the
  same in blocks of 1, 64 and 333 on 0 and 3 helpers. Nobody has heard it.

## 17. The join's review, mended (2026-11-15)

**The review found no defect in batch 2 or the join.** It ran on a copy:
46 of 46 tests; a song with no MOD the same to the bit as `main`
(`effects_test --print`); `main` reads the join's song files (MODs, FEEL,
the mixer, AT END: the same text with the `mod` lines left out); not-a-number
held everywhere; **no allocation on the audio thread** with 24 MODs, a scene
move and a tempo change; Play twice the same; 22 mutants, 21 killed, 1
equivalent. Not done: the thread sanitizer on `nextscene`, the handheld,
listening. Its four findings:

| Finding | Fate |
| --- | --- |
| `bench`'s "MODs on the sends" row read 24 MODs -0.04% in one run, +0.10% in the next | **Mended.** `modsAlone` builds the three engines (0, 1, 24 MODs) first and times them in turns, 40 rounds of 100 blocks, each its best. Six runs since, load 1.5 to 3.4: 1 MOD +0.028 to +0.046%, 24 MODs +0.084 to +0.122%, never below zero. `macroModsRun` had the same fault (a Phazed run read -0.06%) and is timed the same way: three runs, QMOD +0.26 to +0.27%, Phazed +0.14 to +0.21%, JAX-EP +0.13 to +0.14% (+0.04 once). `bench` times nothing in ctest; the figures are run and read |
| The join adds a compiler warning at `bench_run.cpp:672` | **Not so.** It is `main`'s own warning at `bench_run.cpp:534` (the app timing's `NoteEvent{...}` with no `play`), the same line moved down by the MOD rows. `main` and the join each have one in that file. Left as `main` has it |
| Removing `wait = std::ceil(wait)` (`engine.cpp`, `startNote`'s ratchet loop) survives `main`'s `chordfx_test` | **Shown why: no note reaches it.** Only a chord's key (`startChordNote`) and an arpeggio's note carry `kWholeFrames`, and both come with ratchet 0: `startChordNote` counts a chord's pieces itself and ceils each. An `abort()` put on that line passed all 46 tests. Kept as a guard, with a comment, as `aheadIsNow` is (CLAUDE.md says both) |
| JAX-EP's TUNE macro takes a MOD, but moves DCO-2 in semitone steps | **The owner's call** (call A below). Read from the code (`kTuneStepped`, 2.4 semitones a dial step, rounded to the semitone), not measured, not heard |

### What batch 3 needs to know

- **The way in**: `Session::setMod(track, mod, settings)` (returns whether the
  track holds what was asked) and `Session::modData(track, mod)`. The first
  free MOD is the first whose `kind` is `kModOff`. `modTargetFits(track,
  lane)` says whether X + UP on that knob may take one: a macro that is a
  choice may not (`macroIsChoice`: Phazed CHORUS and OCTAVE, QMOD OCTAVE).
  **Every JAX-EP macro fits** (call A).
- A MOD with no target that fits cannot be stored (it reads OFF), so the knob
  gesture always has its target: the knob under the cursor.
- **Undo needs nothing from the screen**: `soundMark()` sees a MOD change the
  frame after it.
- **While stopped**, the engine's `ModAudition` runs a track's MODs from the
  moment X went down; the screen must send it on X down and let go on X up
  (as `keepHearing` lets go of a held audition). It is not taken while
  playing.
- DEVICE MAIN's X already hears (`synth_screen.cpp`); the X-held check must
  come before Y (QMOD's ROLL) and before the stick (section 7's order table:
  three tests).
- `ui::hintsUnder` draws chips under fields that exist; the fields need the
  room the owner gives them (call B). Every new still goes in `kViews`
  (`tests/stills.hpp`), at XL and in PAPER (`textviews`, `themeviews`).
- What only tests saw: everything. No screen sets a MOD yet; nobody has heard one.

### The owner's calls before batch 3

**Answered 2026-11-15: "go with all your picks"**, each final: every call below at its recommended
answer. Section 18 says how each is built.

| # | Call | Recommended |
| --- | --- | --- |
| A | JAX-EP's TUNE (DCO-2) under a MOD steps in semitones, as its knob does: allow it, keep it out (as a choice), or make the MOD's TUNE glide | **Allow it as it is**: a stepped pitch wobble is a sound of its own, and it is what the knob does; a gliding TUNE is a change to the synth |
| B | Where the X-held fields go (section 9, call 20: a change to a settled look) | **DEVICE: in LIVE OUTPUT's strip, only while X is held; FX: in the heading.** Stills first |
| C | What the first UP on a knob with no MOD makes | **LFO, SINE, 1 BAR, +25%** (section 7) |
| D | Two MODs a track (MODS FULL on a third knob), or one per knob | **Two** (call 1) |
| E | X held while stopped runs the MOD (call 8) | **Yes** |
| F | What X + Y rolls (call 15) | **A stepper's row; an LFO's shape and rate, DEPTH kept** |
| G | "HOLD X: MOD" in DEVICE MAIN's hints bar (it is full) | **On the help card only**, unless it fits with `gHintsOver` at 0 |
| H | Nine effects glide a setting a call at a time, so under a MOD they are not the same to the bit in other blocks (at most 0.0024 apart, about 46 dB down), and a fast MOD on them is mostly smoothed away (CLAUDE.md) | **Leave them**: mending it changes their glide on old songs |


## 18. Batches 3 and 4, X held on a knob, as built (2026-11-15, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld.** `main` was joined first (JAX-EP's
straight VCA, the picker's spread-out follow, emptied folders kept, the idle redraw: two
conflicts, CMakeLists.txt's test list and CLAUDE.md's test count, both sides kept). The owner's
calls A to H are taken as answered. One piece of code serves both screens: `app/mod_knob.hpp`
(`ModKnob`), handed the knob, a place to draw and the screen's own button order.

| Part | What is there |
| --- | --- |
| Where | DEVICE MAIN: a macro that takes a MOD (`modTargetFits`; Phazed's CHORUS and OCTAVE, QMOD's OCTAVE do not). FX: a TRACK's slot setting that is an amount, and its two sends. Not the list, a bus, the master, a choice: X only hears there, as before |
| The gesture | UP / DOWN DEPTH by 1% on whole percents; **the first UP on a knob with none makes LFO, SINE, 1 BAR, +25% (call C) in the first free MOD; DOWN makes nothing**. The stick: up / down DEPTH smoothly (1.2 a second at full push, squared), left / right RATE a click (5 a second held). LEFT / RIGHT RATE; A SHAPE (SINE ... DRIFT, STEPPER, SINE); L1 / R1 PHASE or LENGTH; Y the dice (call F); hold B deletes (a second, any other press calls it off). The ends stop. A held D-pad repeats: DEPTH at the cursor's pace, RATE at a list's |
| MODS FULL (call D) | Both MODs aimed elsewhere: the strip says MODS FULL and BOTH MODS ARE IN USE, no chips, nothing is made |
| The order (section 7) | The X-held check comes right after X in `onButton`, before Y's ROLL (QMOD), B's middle / default, L1 / R1's track (FX), A's turn, the D-pad's cursor; `update` hands the stick to the knob and returns. X is not the MOD's with A held. A stick still pushed when X is let go moves no cursor until it has come back (both screens), and one pushed when X goes down turns nothing until it has come back |
| The fields (call B) | DEVICE: in the LIVE OUTPUT strip's place (one row: the knob and MOD n; SHAPE; DEPTH; RATE; PHASE or LENGTH; NOW). FX: in the heading's place (the knob and NOW above, the four fields below); **on SENDS in the other send's row**, so the send held stays in view (a choice made here: the SENDS heading is one line). Only while X is held. A tight row (LARGE at every size), each chip under its field from one constant (`kDeviceModPlaces`, `kFxModPlaces`), the dice at the left, HOLD B under the knob's field. NOW: `Engine::modShown`, purple from the middle |
| Heard while stopped (call E) | `Session::hearMods(track, mask, screen)` each frame X is held: `ModAudition` sent only on a change, nothing while playing, sent again after Stop; `keepModsHeard` (App) lets go on X up, another screen or track; panic and a new song forget it. The synth's note is held while X is (`auditionHeld`), on DEVICE and on FX |
| Hints (call G) | "HOLD X: mod" where it fits at `gHintsOver` 0: DEVICE MAIN on a macro that takes one (Phazed, JAX-EP; not QMOD, whose ROLL leaves it 50 pixels short), FX on SENDS (a setting's bar is 22 pixels short). The help card's X line: "hear it. Held: a hit's or a note's effects, a knob's MOD" |
| Call A | JAX-EP's TUNE takes a MOD as it is (every JAX-EP macro fits): nothing to build |
| Call H | The nine effects that glide a call at a time are left as they are |
| `Session::setMod` | While a MOD stays on, only the fields and steps that changed are sent (a turn every frame was 26 commands) |
| Stills | `device-mod-held`, `-stepper`, `-new`, `-full`, `-deleting`; `fx-mod-held`, `-stepper`, `-new`, `-full`, `fx-sends-mod-held`, in `kViews` (`textviews` at four sizes, `themeviews` in six themes) |
| Tests | `tests/modknob_test.cpp` (`modknob`, `TIMEOUT 120`, under a second): the words and the dice; the gesture on a macro; MODS FULL, choices, FOLDER, A held; the order (Y before X still rolls, X held Y is the dice, the stick and D-pad keep the cursor, B is not the middle, the stick after X let go); heard while stopped and let go (X up, another screen, Play and Stop, a tap); only what changed is sent (a SQUARE's offset is DEPTH exactly, a STEPPER's first value); panic; B let go out of sight; X let go out of sight; the drawn strip and heading (every field where its constant says, each chip at its field, the widest values clear of the next, NOW purple, DELETING, MODS FULL, the plain bar and LIVE OUTPUT back pixel for pixel, the heading back pixel for pixel); the same pixels at XL as at LARGE. `idle_test` has two parts (DEVICE and FX: made, heard, let go). `app_test`'s help card line. `ctest`: 50 of 50 |

**Choices made here** (each one place): DOWN with no MOD makes nothing (call C names UP); a STEPPER
reached by A with a row never written is rolled, so it moves; the dice's LFO rates are 1/8 to 4
BARS (the faster ones are mostly smoothed away on the newer effects); NOW is drawn on both screens
(the plan's "what moves" asked for it on the MOD tab; here it shows the gesture is alive); on SENDS
the fields take the other send's row, not the one-line heading.

**Mutants** (made by a script, each built and run, put back; `git diff` read before the commit):
**55 made, 52 killed (one by not building), 3 equivalent.** Eight survived at first and have
tests now (B let go on another screen, X let go on another screen, the stick pushed when X goes
down, the first stick push up making one, the fixed lettering, FX's A-held check, FX's stick after
the knob, and DEVICE's stick after X let go: its test held the stick 20 frames, and the cursor went
round the four rows back to where it started). Equivalent: LENGTH's ends (`modLength` holds it
anyway); FX letting go when the knob under the cursor changes (it cannot while X is held: every
direction and L1 / R1 are the knob's); sending every field instead of the changed ones (the same in
the engine, only more commands). Not counted: `main.cpp`'s view name renamed (`textviews` holds the
names, `modknob` does not), and one idle part's UP taken out (the session still passes: X held
stirs the redraw whatever it shows).

**What only the tests and stills saw**: everything. No MOD has been heard and nothing has run on
the handheld.

**Left**: batch 5 (the MOD tab: both MODs, the list of targets, a STEPPER's row written by hand,
SLEW and CHANCE, which the knob does not reach), 6 (the brass mark on the target's own bar, the
shape picture), 7 (lanes 22 / 23 as DEPTH automation), 8 (LEVEL and PAN), 9 (the cost on the
busiest song, SPEED TEST).

## 19. The review of batches 3 and 4, mended (2026-11-15, branch `mods`)

| Finding | Fate |
| --- | --- |
| X held across a change of track (picked on SONG, X never let go) kept the gesture on the old track: UP changed the old track's MOD, and its audition was let go and asked again every frame (`keepModsHeard` let go, the screen's `ModKnob::update` asked again), so the wobble stood still | **Mended.** Both screens let go when `project.track != modKnob_.track()` (`FxScreen::update`, `SynthScreen::update`: two tracks of one synth share a screen). `testTrackChangedWhileHeld`, on DEVICE (two PHAZED tracks) and FX, picks the other track on SONG with X held: the knob let go, nothing runs, UP touches neither track, X pressed again is the shown track's. Both mends' mutants fail it |
| FX letting go when the knob under the cursor changes was called equivalent in section 18; it is not: SELECT + B (undo) works with X held, and an undo that takes a 4-setting effect back to a 3-setting one pulls the cursor in | **Tested** (the code was right). `testUndoTakesTheKnobAway`: FILTER after REVERB, X on MIX, UP, two undos; the knob let go, UP makes nothing. The mutant fails it |
| Three mutants passed: DOWN with no MOD making one (`by <= 0` to `by < 0`); `keepModsHeard` without its track term; `letGoOfMods` forgetting a let go lost to a full queue | **DOWN**: equivalent. `depthBy` is only ever called with `+-kDepthStep` (the D-pad) or `+kDepthStep` (the stick's first push), never 0, so the two compare alike on every call; `testTheGestureOnAMacro` already holds DOWN making nothing. **The track term**: with the screens' mend no gesture reaches it (they let go first); it stays as the second guard, and `testHeardOnlyForTheTrackShown` holds the Session's rule by itself. **The lost let go**: `testLetGoLostToAFullQueue` fills the queue, lets X go, sees the Session still asking, then the MOD stopped once there is room |

**Mutants of the mend** (each built and run, put back, `app/` compared with a saved copy): the
FX track check, the DEVICE track check, `keepModsHeard`'s track term, the lost let go, FX's
knob check: **5 made, 5 killed.**

**What only the tests saw**: all of it. Nothing heard, nothing run on the handheld.


## 20. Batch 5, the MOD tab, as built (2026-11-15, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld.** The owner (2026-11-15): "keep going
with the LFO and random stepper"; every open call of section 9 at its recommended answer (the tab
after FX; the brass mark yes, thin, which is batch 6; lanes 22 / 23 as DEPTH automation, batch 7;
LEVEL / PAN as targets, batch 8), **except TREMOLO, which stays** (call 14: an effect taken out
would break the songs that use it).

| Part | What is there |
| --- | --- |
| The tab | `View::Mods`, between FX and SET (`app/session.hpp`: `View` is never saved by number); `App::mods()`; the top bar's six words |
| **Does a sixth tab fit?** | **The tabs fit; the screen's words lost room.** At 5 pixels either side the six tabs left 143 pixels for the screen's words stopped, 70 playing (the scene's blocks take the rest) and 8 playing with WRITE: "2 PHAZED" read "2 PHAZE" while playing. **The plainest fit: 3 pixels either side of a tab's word** (`ui::kTabPad`, was 5): 167 stopped, 94 playing, 32 with WRITE (five tabs had 186, 113, 51). A track's name ("12 PHAZED", 77) fits stopped and playing (`modtab_test`); with WRITE on while playing it is cut, as a long name already was. The bar is a tight row, the same at every TEXT SIZE (`textviews` holds the stills' top rows to LARGE's) |
| The screen | `app/mod_screen.hpp/.cpp`, laid out as FX (`kListX`, `kPaneX`, `kHeadH` as FX's; the page to `kPageBottom`, `static_assert`ed). The list: MOD 1, its STEPS, MOD 2, its STEPS, under the headings MOD 1 / MOD 2 (the track's name beside MOD 1). A MOD's line: its shape (or OFF), its target, DEPTH and RATE, a small picture with the dot. A STEPS line: its length and a small row, the step playing lit; an LFO's says AN LFO: NOT PLAYED |
| A MOD's settings | TARGET, SHAPE (SINE ... DRIFT, STEPPER: the knob's one list, `ModKnob::shapeAt` / `shapePlace`; the ends stop here, where the knob's A goes round), DEPTH (1% a push, the stick smooth, from the middle), RATE (a click), PHASE or LENGTH. An OFF MOD has TARGET alone, and words on how to aim it |
| STEPS | The 16 in a box (from the middle; past LENGTH faint; the cursor's outlined; the one playing purple), then LENGTH (a click), SLEW, CHANCE (5% a push, the stick smooth). LEFT / RIGHT go along the row, LEFT from the first is the list, DOWN is LENGTH, UP from LENGTH the step it came from. **A held + LEFT / RIGHT moves along the row too, so a row is written in one grip** |
| The list of targets | Hold A on TARGET or on a MOD's line: MACROS, FX 1 to 3 (each with its effect's name), SENDS, a line each, what may be moved small and lower case, the cursor's lit. **Only `modTargetFits` decides** (`pickLineTargets`): a choice, an empty slot, a drum track's macros are never offered. UP / DOWN skip a line with nothing; the place along a line is kept where it can be. **Nothing is set or heard while A is held**; let go aims it (an OFF MOD starts as the knob's first, call C; one that is on keeps everything but its target). B with A held, another track (L1 / R1, or any other way), another screen, A let go unseen: called off |
| B, hold B, Y | B: a setting to its usual (DEPTH +25%, SHAPE SINE, RATE 1 BAR, PHASE 0, LENGTH 16, SLEW and CHANCE 0, a step 0); none on TARGET. Hold B on either of a MOD's lines: deleted at a second (the bar on the line and the heading's foot, DELETING THIS MOD); any press, the stick moving the cursor off, A held: called off. Y: the dice, the knob's on a MOD's line (call F), the row on STEPS (an LFO's row too, kept as an LFO) |
| X, L1 / R1, START | X hears the track (held: a synth's note held, a drum lane until let go); **held on a MOD's line with the song stopped, that MOD runs** (`Session::hearMods`, only its bit; App lets go as for the knob). L1 / R1 the track before / after (the ends stop). START play / stop |
| The dot at now | `drawPicture`: an LFO's cycle playing drawn at full height (upside down for a negative DEPTH), from `lfoCycles` / `lfoPositionAt` (`mod_rule.hpp`, the arithmetic `lfoValue` always did, moved into a function: `effects_test --print` identical), the dot where it is now; a STEPPER's row with the step playing lit. The position comes from the engine: **`Engine::modNow(track, mod)`**, the position of the MOD's last tick (`Track::modAt`, an atomic set beside `modShown`, not a number while it does not run). **Drawn only while the song plays or X runs that MOD**: from the engine alone a Stop left the dot for a block with nothing to stir the redraw |
| Hints | Every place on three tracks (drums, PHAZED, QMOD), OFF, LFO and STEPPER, A held and not: `gHintsOver` 0 |
| Stills | `mod`, `mod-lfo`, `mod-steps`, `mod-target` (with `--hold-a`), `mod-off`, `mod-drums` (with `--hold-a`), `mod-deleting`, `mod-12`, in `kViews` (`textviews` at four sizes, `themeviews` in six themes) |
| Tests | `tests/modtab_test.cpp` (`modtab`, `TIMEOUT 120`, about 3 s): the tabs and the top bar's room; the list and its settings (round, ends, RIGHT to the one last used, a MOD gone OFF under the cursor); each setting turned and its ends, the stick, B; the list of targets (what is offered, skipping, kept place, let go, B, another screen, another track two ways, the drum track); a row written in one grip, its ends, LENGTH / SLEW / CHANCE; the dice and undo; hold B (tap, called off by a press and by the stick, on STEPS, undo); L1 / R1 and X (only that MOD runs, none while playing, let go on another screen); the hints; what is DRAWN (no dot stopped; while playing the dot where the ramp is, the one dot, the step playing lit and only it; the STEPS pane; the dot gone on the very pass START is pressed; X held while stopped; a negative DEPTH drawn falling). `app_test` (the tabs, START on the tab), `idle_test` (two parts) |

**Choices made here** (each one place): hold A on a MOD's line in the list opens the targets too (as A
held on an FX slot's line opens the effects); hold B deletes from either of a MOD's two lines; the
dice on a MOD's line too (Y is the screen's special); on STEPS of an LFO the row can be written and
rolled (it is kept); SHAPE stops at its ends on the tab (UP / DOWN), where the knob's A goes round;
a step moves 5% a push (DEPTH 1%, as the plan says); the target list's words are lower case, as the
list of effects'; the dot is drawn at full height (the shape, not DEPTH x shape) and turned over for
a negative DEPTH; X held on a STEPS line runs that MOD too.

**Mutants** (made by a script, each built and run, put back; `git diff` read before the commit):
**34 written, 33 made (one's text was not there), 29 killed, 2 equivalent, 2 showed code to take out.**
Six survived at first and have tests now: another track up by another way while A is held, A held +
RIGHT at the row's end, the stick moving the cursor off while B is held (with both MODs on, so
deleting the other is seen), the step lit on the pass START is pressed, the engine's position against
its own value (`modOffsetAt(modNow) == modShown`), UP from LENGTH to a middle step. Equivalent: SHAPE's
end at 8 (`shapeAt` reads every place past DRIFT as STEPPER); B with A held on the list not letting go
of the list itself (the next `update` calls it off, A no longer counted held). Taken out because no
gesture reaches them: `shapeAt`'s "already a STEPPER" return, and the list's row rounded down to the
MOD's line (only a MOD's line opens it).

**What only the tests and stills saw**: everything. No MOD has been heard, nothing has run on the
handheld.

**Left**: batch 6 (the thin brass mark on the target's own bar on DEVICE and FX), 7 (lanes 22 / 23 as
MOD DEPTH automation), 8 (LEVEL and PAN as targets; TREMOLO kept), 9 (the cost on the busiest song,
SPEED TEST).

## 21. The review of batch 5, mended (2026-11-15, branch `mods`)

| Finding | Fate |
| --- | --- |
| The list's small STEPS row drawn upside down from the STEPS pane beside it for a negative DEPTH | **Mended.** `drawPicture` takes `asWritten`: the STEPS line draws the row as written, as the pane does; the MOD's own picture (on its line and in the heading) still turns over, since it is what the target does. `modtab_test` `testTheListRowAsThePane` reads both signs off the drawn screen (the pane, the list's row, the MOD's picture) |
| The hints wrong with A held on a STEPS line in the list (RIGHT steps, Y dice: neither does anything there) | **Mended.** That place now says only L1 / R1 track (START still works and is not listed, as with A held on a setting). The bar's words are `ModScreen::hintsNow` (what `draw()` hands the bar), so a test reads them: `testAHeldOnAStepsLine` |
| The tab lets both MODs aim at one target, and the knob only ever shows MOD 1 | **Not changed: the owner's call.** Two on one target add (section 2), so it is allowed; X held on that knob reaches MOD 1 only (its fields, hold B, the dice), and MOD 2 is then reached on the tab alone. The list of targets does not mark what the other MOD holds. Choices: leave it; mark the other MOD's target in the list; or refuse it in the list (one MOD a target). Recommended: mark it, so nothing is hidden and nothing is refused |
| Six mutants survived `modtab_test` | **Each has a test now, and each is killed**: hold B at 0.5 s (`testHoldBTakesASecond`: not gone at 800 ms, gone by 1040), the stick on SLEW 4x (`testTheStickAndTheRepeat`), X on a drum track hearing lane 2 (`testXOnTheDrums`, through `Session::heardLane()`, new), Y with A held rolling (`testAHeldOnAStepsLine`), a click held with A repeating at the cursor's pace (LENGTH from 1 held a second: one, then one each `kListEveryMs`), a setting's hints losing Y dice (`hintsNow`). The three the review called equivalent or redundant were left |

Mutants of the mend: the two fixes taken back and the six above put in again, eight made, eight
killed, each put back. Full `ctest -j3`: 51 of 51. No engine code changed. **Seen only by the tests
and the stills** (`mod-steps`: the list's row now stands as the pane's, the MOD's picture turned over).

## 22. Batch 6, the brass mark, as built (2026-11-15, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld.** Call 18 at its recommended answer:
a thin brass mark on the target's own bar.

| Part | What is there |
| --- | --- |
| Where | DEVICE MAIN's eight macro bars, FX's slot settings (a track's, not a bus's or the master's) and the two SENDS bars |
| What it shows | `modMarkAt(session, track, lane, shown)` (`app/mod_knob.hpp`): the bar's own value (`shown`: the knob, or where its automation has it while playing) plus `Engine::modShown` of every MOD aimed at that lane (two add), held to 0..1, as the engine gives it (`modGiven`). Not a number, and nothing drawn, unless the song plays or X runs that MOD (`Session::modsHeard`), and the engine has ticked it (`modNow` a number) |
| How it looks | `drawModMark`: `kModMarkW` 2 pixels wide, from `kModMarkOver` 3 above the bar to 3 below, brass (the job of a meter: it moves by itself), held inside the bar's ends. Over a brass bar (A held) it shows above and below. The macro bar's place is now three constants, `ui::kMacroBarInset`, `kMacroBarFromFoot`, `kMacroBarH`, which `macroBox` and the mark both use |
| Redraw | It moves only while the song plays (stirred already) or X is held (a button held): nothing new to stir. `idle_test` has two parts: a macro and a send, the song played then stopped |
| Stills | `device-modded` (TONE under an LFO, the third macro under a STEPPER), `fx-modded` (TAPE's FEEDBACK under an LFO, AGE under a STEPPER), in `kViews` |
| Tests | `modknob_test` `testTheBrassMark`: no mark stopped; playing, a SQUARE's mark exactly where centre + depth is, 2 wide, 3 over and under and no further; no mark on the seven other macros; a MOD set while playing has none until its tick; two MODs on one knob add and the mark is held inside the bar at the top; gone on the very pass START stops it (before the engine has heard the Stop); X held while stopped shows it, let go hides it; FX: FEEDBACK's mark, none on TIME, AGE, MIX; a send's about the send's own value; the same in PAPER |

**Choices made here**: the mark is drawn where the song gives the knob, not where the MOD alone
would put it about the bare knob (so on an automated knob it moves about the automation); two MODs
on one knob make one mark (their sum); none on the list's small bars (only the target's own bar).

**Mutants**: 13 made, 13 killed (the running check, the playing check, the target check, two not
added, no clamp, the edge not held in, a short mark, DEVICE not drawn or on the wrong macro, FX's
setting or send not drawn, a send's mark about nothing, not brass). Two guards the first run found
no way to reach were taken out of FX's call (a choice: no MOD is aimed at one; an effect being
tried: the settings are not drawn then).

**What only the tests and stills saw**: all of it.

## 23. Batch 7, the DEPTH lanes, as built (2026-11-15, branch `mods`)

**Built and tested; nothing heard, nothing on the handheld.** Call 10 at its recommended answer:
lanes 22 and 23 are MOD 1's and MOD 2's DEPTH. The owner's word on the clearing ("leave the
automation clearing as is") is kept: a scene with nothing written goes back to the knob, here the
MOD's own DEPTH, and nothing new clears a lane.

| Part | What is there |
| --- | --- |
| The scale | `mod_rule.hpp`: `modDepthLane(m)` (22, 23), `modDepthToLane` / `modDepthFromLane`: 0.5 no depth, 0 -100%, 1 +100%, held; the song file's DEPTH scale (section 5) |
| The engine | `SetMod`'s DEPTH sets the lane's knob (`autoBase`) and where it is (`autoNow`), and stops a glide under way, as `SetMacro` does a macro's. `runMods` gives each MOD the depth its lane has now; **where the lane rests on its knob, the MOD's DEPTH itself** (0.3 to 0.65 and back is 0.29999995: a song's MODs at rest are as before, to the bit). A new engine and a track taken away put both lanes at no depth. `effects_test --print` the same before and after |
| Runs at any DEPTH | `ModSettings::runs()` (KIND and a TARGET) is what the engine runs; `on()` (and a DEPTH) is still the app's. So a MOD set to 0% wobbles in the scene whose lane says so. It costs a MOD at 0% the tick's chunk cuts while the song plays (as one at 40%). A DEPTH turned to 0% lets go at once (the setting given its centre), not at the next tick |
| The app | `Session::setMod` writes a DEPTH turned into its lane with WRITE on while playing (`recordAutomation`), only for a MOD that stays on (made or deleted is not a turn). `Session::modDepthShown`: the lane's while playing, else the MOD's own exactly. The knob's DEPTH field, the tab's DEPTH row, its list line and both pictures read it. On the tab a lane written in the scene worked on puts the purple line under DEPTH and "moves with the pattern" (`ModScreen::kMovesWords`); B on DEPTH gives +25% and empties the lane in that scene, as B on an FX setting does |
| File, undo, scene move | Nothing new: the lanes are `auto` lines, pattern text in undo, `SceneStore` automation in a scene move |
| Still | `mod-depth` (MOD 1's lane -60% then +80%, the demo playing), in `kViews` |
| Tests | `mod_test`: the scale; the lanes (written +60% on a MOD at 0%, the other MOD at its own exactly, back to the knob in an unwritten scene, the knob turned, MOD 2's lane MOD 2's only, Stop, heard while stopped at the knob's); retargeted with only its lane's depth; the knob stopping the lane's glide; 0% lets go at once and still cuts; a scene move with the lane written equal to the song built (sound and shown, bit for bit); a track taken away. `modapp_test` `testTheDepthLanes` (shown while playing, the file, WRITE and one undo, WRITE off, a MOD made writes nothing, NEXT SCENE copies and plays it, undone). `modtab_test` (purple line, B, undo; the lane drawn as the depth, pixel for pixel). `modknob_test` (the field; a MOD at 0% with its lane written has the brass mark) |

**Choices made here**: a MOD deleted leaves its lane's automation (nothing new clears a lane: the
owner's word), and the tab shows it on the next MOD made there; the purple line is on the tab's
DEPTH row only (not in the knob's tight strip); a MOD made is not a DEPTH turned (WRITE writes
nothing then); a MOD at 0% runs (cuts chunks) so its lane can move it.

**Mutants**: 28 made, 28 killed (22 at first; the six survivors each have a test now: the glide not
stopped, the lanes not at no depth on a new engine, `anyMod_` by `on()`, a retarget let go by
`on()`, the row drawn at the knob's depth, the brass mark by `on()`).

**What only the tests and stills saw**: all of it.

