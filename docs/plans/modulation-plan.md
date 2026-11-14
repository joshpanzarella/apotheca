# LFO and random stepper plan

Written 2026-11-14 from three independent designs (one from the player's
hands, one from the engine and song file, one from music), judged and joined
here. **Nothing here is built, run or heard.** Only the code at 0.442 was
read. Every cost is a target or an estimate until batch 0 measures it.

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
| Engine | On the audio thread, before the helpers, on a **64-sample tick counted from each step's start**; chunks cut on the tick **only while some MOD is on** |
| Cost target | All 24 MODs on effects and sends: **≤ 0.05% of a desk core** (≤ 0.25% on the handheld). Synth macros measured in batch 0 (Phazed works its whole tone out again per change) |
| First thing to build | **Batch 0**: a Rack-free rule header (`mod_rule.hpp`) and its test, `applyAuto` split into centre + given (nothing added yet), and the macro cost measured. **No sound changes, nothing on screen** |

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
numbering for the song file, a TYPE change and the engine, and the engine
meets them in the one function that already hands lane values out
(`Engine::applyAuto`, `engine/src/engine.cpp:1760-1769`).

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
  CHORUS and OCTAVE macros are thresholds). A wobbled choice clicks.
  A STEPPER on OCTAVE (random octaves) is musical: **owner's call**, later.
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
| Stop (`settleAutomation`, `engine.cpp:1791-1797`) | Every MOD offset goes to 0; the knob is heard |
| TYPE changed in a slot | The MOD's target follows the setting **by name**, as lanes do in `changeEffectType` (`app/fx_screen.cpp:22`). A setting not kept, another family or an emptied slot: the MOD goes OFF |

**Engine change:** `applyAuto` keeps `autoNow` as the centre and hands on
`given[lane]`. **A lane with no MOD has an offset of exactly 0.0f, and the
same value goes out in the same calls.** That is the bit-exact path.

## 5. Per track; scene move, undo, song file, presets

| Thing | Recommend |
| --- | --- |
| Settings and steps | **Per track** (`TrackData`, beside `glideFrom`, `app/project.hpp:209`). Automation is per scene (`project.hpp:221`); a MOD is not. **Owner's call** |
| Per-scene feel | Later: lanes 22 / 23 automate MOD 1 / MOD 2 DEPTH, so "wobble only in the drop" needs no new scene storage. They are already in `Project::automation` and in the scene move's `SceneStore` |
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
only while `anyMod_`**. Since a chunk is at most 64 frames (`kMaxBlock`,
`engine.hpp:100`) and already restarts at each step, chunks mostly land on
the grid already; the extra cut is about one per `render()` call.

| Rule | Why |
| --- | --- |
| The value at a tick = `shape(position)`, position = `stepsFired_ - 1 + (samplesPerStep_ - untilStep_) / samplesPerStep_` | Pure: block length, helpers, tempo and scene moves cannot change it |
| Cut chunks only while some MOD is on | Cutting every song would move today's automation glide and Twin's render splits: old songs would change |
| A value is handed over only when it changed | The synths return early on an equal value (`phazed.cpp:295`); a held STEPPER step costs one compare |
| Macros may tick every Nth tick | Phazed works its whole tone out again per change (`Phazed::apply`, `phazed.cpp:261`), which is why automation glides in only 8 moves (`engine.cpp:1771-1773`). Batch 0 measures and sets N |
| State in a fixed array in `Engine::Track` (`engine.hpp:504`) | The audio thread never allocates |
| `std::atomic<float> modShown[2]` per track | The screen reads what is heard, as `effectMeter` (`engine.hpp:364`, `621`). The app thread never touches a synth or an `Effect` |

**Commands**, appended after `HoldEffectAutomation` (`engine.hpp:190`):
`SetMod` (track, a = mod, b = field, value) and `SetModStep` (track, a = mod,
b = step, value). Through `send()`, counted when lost.

**How a value reaches each target:**

| Target | Path | Note |
| --- | --- | --- |
| Phazed macro | `setMacro` → `apply(false)` (`phazed.cpp:292-297`) | **The cost risk.** CZ values are whole numbers, so a sweep moves in stairs |
| QMOD, Twin macro | `setMacro` → `apply()` (`qmod.cpp:215`, `twin.cpp:525`) | Measured |
| JX-3P macro | Read on its own 480-sample tick (`jx3p-plan.md:169-173`) | A MOD faster than 10 ms is wasted there; fine |
| Old five effects | `setParam` → `changed()`; FILTER works `pow` and `tan` again (`engine/src/fx/filter.cpp:59`), no glide (pinned) | 1.33 ms stairs. **May zip on FILTER CUTOFF; a test measures the step size; nobody has listened** |
| Newer effects | `setParam` into their own 20-30 ms `Glide` | **A fast MOD is softened**: a 1/8 at 120 BPM through a 25 ms glide loses depth. A test states how much per rate. The newer filters on branch `newfx` (COMB, VOWEL, EQ, `fx/svf.hpp`, not on `main` at 0.442) slide sample by sample on their own 16-sample TICK, so they are the smoothest CUTOFF target once joined |
| Sends | `track.send`, already gliding 5 ms (`engine.cpp:1766`; `kSendGlideSeconds`, `engine.hpp:99`) | A send MOD wakes a bus at rest, as it should |
| LEVEL, PAN (batch 8) | The level and pan glides (`panGains`, `engine.hpp:98`) | The middle-at-rest fast path stays only while the offset is 0 |

**Cost targets** (desk; the handheld is about a fifth as fast):

| Case | Desk | Handheld |
| --- | --- | --- |
| Working out all 24 MODs (12 tracks × 2) at 64-sample ticks | ≤ 0.03% | ≤ 0.15% |
| One MOD on an effect or send | ≤ 0.02% | ≤ 0.1% |
| One MOD on a Phazed macro at N ticks | ≤ 0.3%, **or N is raised** | ≤ 1.5% |
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
(`app/synth_screen.cpp:246`, `app/fx_screen.cpp:408`), and a strip of fields
appears under the knob, each chip under its field (`ui::hintsUnder`,
`app/ui.hpp:208`).

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
64, 333, 512 and helpers 0 and 3, the same to the bit; drawn checks through
`tests/canvas.hpp`.

| Batch | One agent does | Ends with |
| --- | --- | --- |
| **0 Groundwork** | `engine/include/apotheca/mod_rule.hpp`, no SDL: target ids, rate table, shapes, `lfoValue(settings, position)`, `stepValue(settings, steps, position, seed)` with SLEW and CHANCE, `kRollMod = 6`, the combine and clamp. `tests/mod_test.cpp` against hand-worked values. `applyAuto` split into centre + `given` with the offset fixed at 0. A `bench` row: `setMacro` cost per synth at 64-sample ticks, which sets N | ctest green; **`effects_test --print` identical**; the figures written in this plan. **No sound or screen change** |
| 1 Engine | `Track::Mod mods[2]`, `SetMod`, `SetModStep`, the tick and its cuts under `anyMod_`, `runMods`, waiting on held slots, OFF on `RemoveInstrument`, offsets to 0 on Stop, `modShown` | Same bits across blocks and helpers; Play twice gives the same; tempo change keeps phase; scene move with a MOD running equals the song built; old FILTER's steps and the new effects' softening measured |
| 2 Song | `ModData mods[2]` on `TrackData`, `Session::setMod`, `applyProject` sends them, `mod` / `modsteps` words, undo through `soundMark`, the TYPE carry, another family clears. `tests/modapp_test.cpp` | Old songs and everykind as they were; round trip; NaN and 1e300 held |
| 3 DEVICE knob | The X-held strip on MAIN's macros, MODS FULL, hold B delete, the dice | `under_test`, stills, hints at 0, `textviews`, `themeviews`, `ctest -R words` |
| 4 FX knob | The same on slot settings and SENDS | The same |
| 5 MOD tab | The tab, both MODs, the list of targets, STEPS, L1 / R1 | The same, plus the top bar at every size |
| 6 Marks | The brass mark on DEVICE and FX (if the owner wants it), the shape picture | Pixels read in BROWN and PAPER |
| 7 Depth lanes | Lanes 22 / 23 as MOD DEPTH automation (owner's call) | Scene move and undo tests |
| 8 LEVEL, PAN | Targets 32 / 33 | `mixer_test`; a song with no MOD untouched (`runMaster`) |
| 9 Cost | The busiest song with 24 MODs in `bench_run.cpp`; SPEED TEST shows it | Figures in CLAUDE.md "Measured"; **the handheld's only when the owner runs it** |
| Later | CELL / NOTE restart, running while stopped (X held), pitch, choice targets, presets | Only if the owner says so |

**"Groundwork" as the owner asked = batch 0, then 1 and 2.** None of the
three shows anything on the device.

## 9. Risks and the owner's calls

| Risk | What could go wrong | Answer |
| --- | --- | --- |
| Phazed `apply()` per tick | Too costly on the handheld | Measured in batch 0; macros every Nth tick |
| Old FILTER stepped | A zip on CUTOFF; its arithmetic is pinned, no glide may be added | A test bounds the step; **nobody has listened**. A newer filter TYPE would glide |
| Newer effects' glide | A fast MOD loses depth | Measured and stated per rate |
| Extra chunk cuts | Helper wake-ups cost | Only while a MOD is on; measured |
| Today's automation is not block-exact | `glideAutomation` moves lanes at chunk starts (`engine.cpp:1774-1789`), so a MOD on an automated lane inherits that through the centre. Turning a MOD on also moves other tracks' automation steps by under a ms | Left as is (mending it changes old songs). Owner's call. Tests hold MODs block-exact on lanes without automation |
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
| 8 | Run while stopped (X held hears it) | **Later** |
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
