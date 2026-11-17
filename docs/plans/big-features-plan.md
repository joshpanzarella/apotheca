# Four big features: step conditions, per-step settings, a length per track, live play

Owner, 2026-11-17: these four come after the easier batch being built now (the metronome and count-in are in that batch, and are assumed here).

**This is a plan. Nothing is built, nothing has been heard, nothing has run on the handheld.** Each part says what exists to build on, the design, the owner's calls (each with a recommended answer), the batches, the risks and a rough size.

| Part | What | Size |
| --- | --- | --- |
| A | Step conditions (1:2 ... 4:4, FIRST, FILL, PREV) on hits, notes and chords, with a FILL button | **Large** |
| B | Per-step settings (locks), built on the automation lanes | **Medium** (+ medium for DECAY) |
| C | A length per track (LOOP, 1 to 128 steps) under the scene | **Large** |
| D | LIVE: play hits and notes on the handheld's own buttons, recorded, with quantize | **Large** |

## Shared groundwork (read first)

Three things serve more than one part. Build each once:

1. **`engine/include/apotheca/cell_rule.hpp`** (no SDL; the engine, the screens and the tests ask it): where a track is in its cell (bar, step), and **how many times round** the cell has gone in this visit of the scene. A adds it. C widens it for LOOP cells. D uses it to place a recorded key.
2. **CHANCE becomes a roll by place** (A batch 0). Today `Engine::schedule` walks `dice_` (an LCG, engine.hpp). That is why a felt hit asked again rolls its chance again (CLAUDE.md's FEEL section: "Not mended"). Conditions (PREV) need chance to be a rule of where and when.
3. **The X-held strip gets pages, turned by L2 / R2 while X is held** (A adds page 2, WHEN; B adds page 3, LOCK). Today `App::onButton` sends L2 / R2 to the PATTERN screen only while A is held, and otherwise turns the tabs. It would also send them while the screen says X holds a hit, a note or a chord (`PatternScreen::xHolds()`, `NoteScreen::xHolds()`). The panic's four-shoulder check stays first.

**Suggested order: A (with the groundwork), then C, then B, then D.** C changes what "times round" means for a LOOP cell, so it is cheaper after A's `cell_rule.hpp` exists. B does not depend on C. D wants A's FILL button and C's per-track place. The owner's order (A, B, C, D) also works.

---

## A. Step conditions

### What exists

- **A drum hit**: `DrumPattern` (project.hpp: level, nudge, chance, start, fromDice, fx), the engine's `DrumScene`, and `StepFx` (drums.hpp: pitch, ratchet, reverse, cut, throwTo). `Command::SetStepFx` does not use its `e` field.
- **A note**: `NoteEvent` has a free `spare` byte and `ChordEvent` has `spare[2]`. They pass through `TrackImage::Note` and the engine's `Note`. `SetNote`'s `e` uses bits 0 to 13: the ratchet in the low byte, 256 for glide, 512 / 1024 for THROW, and the glide's length shifted left by 11 (3 bits). **Notes have no CHANCE.**
- **Where a hit or note is decided**:
  - `Engine::fireStep`: drums through `schedule`; notes in its loop, run twice on a PAIRED track (`telling_`).
  - `askAheadHits` / `askAheadNotes`: the next step's early sounds.
  - `askAgain`: drops what was asked and asks again after a scene move, or after AT END is switched on the last step.
- **Where the song is**: `scene_`, `repeat_`, `bar_`, `nextStep_`, `stepsFired_` and `afterThisBar()`. **`repeat_` does not count the loops of a scene played alone** (`afterThisBar` returns `{scene_, repeat_, 0}` when `loopScene_` is set).
- **Seeds**: `feelSeed` / `chordSeed(track, bar of the cell, step, steps since Play)` (feel_rule.hpp, chord_rule.hpp). The roll kinds are `ChordRoll` (`kRollTime` ... `kRollStrike` = 7).
- **The X-held strips**:
  - Drum grid: `PatternScreen::fxHeld_`. The fields sit at `kFxPitchX` ... `kFxThrowX` (pattern_layout.hpp). The STEP field is at 120 and LANE at 8.
  - Roll: `NoteScreen::fxHeld_`, the fields at `kStripHeld...X`; a chord's list of six is in `chord_effects.hpp`.
  - Both draw each button's chip under its field with `ui::hintsUnder`.

### Design

1. **The list** (`engine/include/apotheca/cond_rule.hpp`). The numbers are appended and kept in songs; the order shown is a separate table, as `kGlideOrder` is. A number nobody knows reads as OFF (the hit plays).
   - The list: OFF, 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4, FIRST, NOT FIRST, FILL, NOT FILL, PREV, NOT PREV. That is 16 places.
   - One function, `condHolds(cond, round, fill, prev)`.
2. **"Times round" is counted in this VISIT of the scene.** A visit runs from Play, or from entering the scene, until another scene plays. It is worked out from where the song is, never kept as a roll:
   - The engine keeps `pass_`. It is 0 at Play and when another scene begins. It goes up by 1 where the scene goes round into itself: a repeat, a loop of a scene alone, or a one-scene song going round. A scene move does not touch it (the playing goes with its scene).
   - `cellRound = pass_ × roundsPerPass + bar_ / cellBars`, where `roundsPerPass = ceil(barsThePassPlays / cellBars)`. A 3-bar cell under a 4-bar scene goes round twice in each pass.
   - **A:B** plays when `round % B == A - 1`. **FIRST** plays when the round is 0, so a crash marked FIRST plays each time the song reaches its scene.
3. **FILL** is live and for the whole song: `Engine::fill_`, set by `Command::SetFill` (appended). It is read when a hit is asked. A bounce has FILL off.
4. **PREV is worked out by place, not remembered.**
   - It looks back for the nearest earlier "condition step": on the same drum lane, or on the same synth track (a chord counts once, and notes on the same step are not "before").
   - A condition step is a hit with any condition except PREV / NOT PREV, or with CHANCE under 100%.
   - The search goes back over the cell's bar lines, and into the round before.
   - The condition and chance of the step it finds are worked out again for that step's own place and time. Both are rolls by place, so the answer is what was heard.
   - If nothing comes before it in this visit, PREV does not play.
   - FILL's past comes from a ring of the last 8 steps on which FILL flipped (`fillFlips_`, in steps since Play).
   - **Why this way**: `askAgain` has nothing to put back (the way it puts back `strokesBefore` today), and the answer is the same at any block length and helper count.
   - The search is at most 128 steps per lane, on the audio thread.
5. **CHANCE becomes a roll by place** (batch 0). `chanceSeed = feelSeed(...)` with a new `kRollChance` (ChordRoll, appended). **Every song with a CHANCE under 100% then plays other dice.** No song with chance stays the same to the bit. In return:
   - askAgain no longer rolls a hit's chance again;
   - Play from the top gives the same performance;
   - a bounce is what was heard;
   - `nextscene_test`'s `fill` song may put FEEL back on its CHANCE lane.

   **The condition is asked first, then CHANCE.** A hit plays only when both say so.
6. **FEEL and SWING** act only on a hit that plays. The seeds are by place, so the rolls of other hits do not move. A hit asked a step ahead is judged for that step's own round: `schedule` is given the round and the pass, worked out from `afterThisBar()`. **`feelAsked` is set whether or not the condition lets the hit play.** Otherwise a FILL flipped between the two askings could begin the hit twice. A run of slices keeps its head's TIME even when the head is dropped.
7. **Ratchets**: the condition belongs to the whole hit, so all its pieces play or none do. The same holds for a note's pieces, every key of a chord, and a whole arpeggio.
8. **Glide**: a note that does not play is never told to the `GlideTrail`. The check stands before `startNote` / `startChordNote` in both PAIRED passes, so both get the same answer. The roll's glide lines (`Project::glideLines`; `GlideNote` gains `cond`) draw round 0 with FILL off, as RANDOM draws round 0.
9. **A scene move and askAgain**: everything is decided by place, so asking again gives the same answers.
10. **Played once** (a bounce, or AT END STOP): from the top, with FILL off. FILL hits never play and NOT FILL hits always play.
11. **Heard by hand** (X, or `Trigger`): the condition is not applied. X hears the hit.
12. **Memory and commands**:
    - `StepFx` gains `cond`; `any()` is left as it is, so the `stepfx` line is unchanged.
    - `NoteEvent::spare` becomes `cond`, and `ChordEvent::spare[0]` becomes `cond` (`NotePattern::render` copies it to each key).
    - `TrackImage::Note` and the engine's `Note` gain `cond`. **`sceneIn` and `sceneInto` copy it, once each.**
    - `SetStepFx`'s `e` carries the condition. `SetNote` adds `cond << 14` to `e`.
13. **The song file**: three new words, each **left out at OFF**, each number read through `wholeIn`:
    - `cond LANE STEP N`, after the hit's `stepfx`;
    - `notecond STEP KEY N`, after `notefx`;
    - `chordcond STEP N`, after `chordplay`.

    These are new words, not more numbers on old lines, as `chordplay` was done.
14. **What comes free**: undo (the patterns are text, and `cellMark` hashes the patterns whole), COPY A BAR, carry (R1), the SONG copy and NEXT SCENE, because each copies whole structs. **The dice (X + Y) leave a condition alone**, as they leave THROW alone.
15. **The gesture: page 2 of the X strip, WHEN.**
    - Hold X on a hit, a note or a chord; R2 goes to WHEN and L2 back to EFFECTS.
    - LEFT / RIGHT step the condition one click, and the ends stop.
    - On a drum hit, UP / DOWN set its CHANCE in tenths (the same as Y + UP / DOWN), so one page answers "when does it play".
    - The page is remembered from one hold to the next, as `fxPick_` is.
    - The page's name stands in STEP's place, with the "L2 / R2" chip under it.
    - With X up, the strip's purple summary line adds the condition.
16. **Marks**: a hit with a condition shows a small tag in its cell, at the top left, in the fixed lettering ("1:4", "1ST", "F", "!F", "P"). On the roll the tag goes on a note's block where it fits, else a corner mark. Checked in PAPER.
17. **The FILL button** (call A8): **hold START while playing.**
    - FILL is on from the press.
    - A tap shorter than `kFillTapMs` (250) still stops the song, on its let go.
    - Stopped, START plays at once on the press, as now.
    - The App's box says FILL in brass while it is held. It works on every screen and in LIVE (D).

### The owner's calls (A)

| # | Question | Recommended | Other |
| --- | --- | --- | --- |
| A1 | Which ratios | **1:2 to 4:4 (B = 2, 3, 4: 9 choices)** | Up to 8 (35 ratios, 41 places to click through) |
| A2 | FIRST is | **The first time round of each visit of the scene** | Only once per Play (a song's later visits never) |
| A3 | Times round count from | **The visit (entering the scene, or Play)** | Play, across the song; or each pass |
| A4 | PREV looks at | **The same drum lane** (a synth: the track) | The whole drum track |
| A5 | What PREV counts as a condition step | **Any condition but PREV, or CHANCE under 100%** | Conditions only |
| A6 | PREV with nothing before it | **Does not play** | Plays |
| A7 | NOT PREV too | **Yes** (A or B, never both: hit 1 at 50%, hit 2 NOT PREV) | No |
| A8 | The FILL button | **Hold START while playing** (a tap still stops, on its let go) | SELECT held alone (clashes with HLD SEL: HELP where there is no MENU); the stick pressed in (L3 is MASTER since 2026-11-17) |
| A9 | Where on the strip | **Page 2 (WHEN), L2 / R2 with X held** | A sixth field on page 1, stepped by L2 / R2 (then B needs another home) |
| A10 | CHANCE becomes a roll by place | **Yes** (every song with chance plays other dice) | Keep `dice_` (then PREV cannot be by place) |
| A11 | CHANCE on notes | **Not now** (later, on page 2) | Now |
| A12 | The dice and conditions | **The dice leave them alone** | They roll one too |

### Batches (A)

| Batch | What |
| --- | --- |
| 0 | **Engine groundwork**: `cell_rule.hpp` (`cellRound`), `pass_`, CHANCE by place (`kRollChance`). Tests: the chance rate over many rounds, askAgain does not roll again, blocks 1 to 333, 0 to 3 helpers, Play twice. `effects_test --print` must be the same unless the pinned song has a CHANCE hit; if it has one, its sum is remade only by the pinned song's rule. `swing_test --print` and the FEEL busy-song sums are remade the same way. |
| 1 | **Engine conditions**: `cond_rule.hpp`, `StepFx::cond`, `Note::cond`, the two commands, the check in `schedule` and the note paths (both PAIRED passes), PREV, FILL and its ring, askAgain, played once. `tests/cond_test.cpp`: each condition by hand over 8 rounds, a scene alone, repeats, a song, PREV chains with CHANCE, FILL flips, ratchets, FEEL / SWING / slices, the PAIRED trail, blocks and helpers, a scene move on every step including the last. `nextscene_test`'s `fill` song gets a lane of conditions; run it again under TSan. A `bench` row: a condition on every hit of twelve tracks. |
| 2 | **App**: `sceneInto`, the song file's three words, the strip pages (`App` routing), the START hold, the marks, the summary line. `tests/condapp_test.cpp`: the gesture, the strip read off the drawn screen, the file, undo, COPY A BAR, NEXT SCENE, a bounce, FILL by START. `under_test` (page chips), `textsize`, `idle_test` (FILL held), stills `cond-held`, `cond-notes-held`, `cond-chord-held`, `cond-marks`, `fill-held` (all in `kViews`). |
| 3 | Review and the mutation pass; README, CLAUDE.md. The help card is full, so its X line takes the change. |

### Risks (A)

- **CHANCE by place changes every song that uses CHANCE** (not the same to the bit).
- **START hold moves Stop to the let go of a tap** (up to 250 ms later). The owner's hand decides.
- L2 / R2 with X held changes the App's tab routing. The tests must hold that L2 with X held never turns a tab, and that the panic still comes first.
- A FILL pressed in the last moment before a step is up to a pass (16 ms) plus a block late.
- The pages' chips must fit (`gHintsOver` 0, `under_test`).
- The name `fill` (the `nextscene_test` song) clashes with FILL; rename the song.

---

## B. Per-step settings (locks)

### What exists

- **Automation**:
  - 24 lanes a track (`kAutoLanes`: 0 to 7 macros, `8 + slot × 4 + setting`, 20 / 21 the sends, 22 / 23 MOD DEPTH), 128 steps a cell (`kAutoSteps`: bar × 16 + step).
  - Stored in `TrackData::automation[scene][lane][step]` and the engine's `SceneStore::automation`; sent with `SetAutomation` / `ClearAutomation`.
  - `fireStep` sets `autoFrom` / `autoTo`; **`glideAutomation` glides over the first half of the step in 8 moves, so a hit on that step still hears the OLD value.**
  - An unwritten step goes back to the knob (`autoBase`); `settleAutomation` runs at Stop.
  - **`giveLane` is the one way a value goes out, with the MOD added on top.** `HoldEffectAutomation` holds a slot's lanes.
- **The app's side of automation**: `Session::setAutomation`, `recordAutomation` (WRITE while playing), `automated()`. COPY A BAR carries the lanes (same track only); `changeEffectType` moves lanes by name; songs keep `auto` lines; undo and scene moves carry them; FX and DEVICE draw the purple line. **The Open list in CLAUDE.md already asks for "an automation step-editor".**
- **A drum hit's own sound is already set per hit**: level, start, and the step effects (pitch, ratchet, reverse, cut, THROW). A lane's own EDIT and DRUM SYNTH are made off the audio thread, so **a per-hit DECAY cannot remake a drum-synth voice.**

### Extend automation, or a separate lock?

| | Automation steps, flagged as locks (**recommended**) | A separate lock on each hit or note |
| --- | --- | --- |
| Storage, scene move, undo, copy bar, song file | **Already there** | New store (8 × 16 × 8 × 16 per track), through `sceneIn`, `sceneInto`, `SceneStore`, the pending hits and notes, askAgain |
| MOD on top, back to the knob, Stop | **Already there** (`giveLane`, `autoBase`, `settleAutomation`) | Built again |
| Two hits on one step locking one knob differently | Impossible (one value a step) | A conflict: the drum lanes share the track's effects |
| A lock follows its hit when the hit is moved | Must be done by the gesture (R1 moves it) | By itself |
| An early hit (nudged early, HUMAN TIME, a strum) | Hears the step before's value (known limit; batch 3 can mend it) | Right by itself |
| Engine work | Small | Large |

**Recommended: extend automation.** Almost everything a lock needs already exists, is tested, and the owner already wants the step editor.

### Design

1. **The lock flag**: a written value v (0 to 1) is stored as v + 2.
   - `engine/include/apotheca/auto_rule.hpp` holds the helpers: `autoLock`, `autoValue`, `autoIsLock`.
   - `kAutoNone` stays -1, and "written" stays `>= 0`.
   - **Everything that reads a VALUE goes through `autoValue`**: `Session::automated`, `modDepthShown`, the MOD tab's DEPTH, `changeEffectType`'s carry (which keeps the flag), and the engine.
2. **The engine**, in `fireStep` and **before the step's hits and notes begin**:
   - a step written as a lock is given at once (`applyAuto`, no glide);
   - a lane leaving a lock (a new `Track::autoLocked` bit) goes back to the knob at once on an unwritten step, or glides from the lock as today on a WRITE value.
   - Steps written by WRITE still glide, so **songs without locks stay the same to the bit.**
   - Synths that store a macro and use it at the next render top (PHYSIC, ACID, MORPH) pick the lock up on the strike's own render.
3. **What is unchanged**: the MODs add on top (`giveLane`), Stop puts every lane back, and `HoldEffectAutomation` holds locks too.
4. **How far a lock reaches**:
   - from a drum hit: its own step;
   - from a note or a chord: every step it sounds in, held to the cell's end.
   - **R1 carrying a hit or note moves its locks with it, and deleting it deletes them** (`Session::setLock` / `moveLocks` / `deleteLocks`). COPY A BAR carries them already.
5. **Known limit**: a sound that comes before its step (nudged early, felt early, strummed) sounds before the lock lands. Batch 3 (optional) mends it: a lock waits with its step's earliest sound and is dropped and asked again by askAgain.
6. **A drum lane's own sound** gets one more step effect, **DECAY**: a fade over the hit, from 100% (the sound as it is) down to 5% of its length. Where CUT has three choices, DECAY is continuous. **`plainRun` must say 0 while it fades.** There is no per-hit PAN: a lane is mono into the track.
7. **The gesture: page 3 of the X strip, LOCK** (after WHEN):
   - **UP / DOWN pick the knob.** On a drum hit the list starts with the hit's own DECAY, then the track's knobs. The track's knobs are its macros (by the synth's names), its slots' settings (by the effect's names), ECHO, SPACE, MOD 1 DEPTH and MOD 2 DEPTH. The pick is kept per track.
   - **LEFT / RIGHT turn the locked value a percent a click.** The first click makes the lock at the knob's own value and moves it one click. The stick makes big moves.
   - **B with X held on this page deletes the lock, not the hit.**
   - The strip reads the knob, the lock and the knob's own value dim ("CUTOFF 72%  KNOB 40%").
   - A hit or note with a lock wears a purple corner, and the knob's own screen shows the purple line already.
8. **The song file**: lock steps go in a new word, `lock SCENE LANE INDEX VALUE ...`, the same shape as `auto`. **`auto` keeps only glide steps and never changes meaning.** Values are read through `fractionIn`.
9. A full step editor (every step of one lane) is a later batch.

### The owner's calls (B)

| # | Question | Recommended | Other |
| --- | --- | --- | --- |
| B1 | Store | **Automation lanes, flagged** | Separate locks on hits and notes |
| B2 | How a lock lands and leaves | **At once on the step's first sample, and back at once after** | A short glide back (if the old FILTER clicks: measure) |
| B3 | A note's lock lasts | **Its whole length** | One step |
| B4 | R1 carry and delete | **Move and delete its locks** | Leave them on their steps |
| B5 | Where | **Page 3 of the X strip** | Only a step-editor view |
| B6 | DECAY step effect; per-hit PAN | **DECAY yes, in its own batch; PAN no** | |
| B7 | B with X held on LOCK | **Deletes the lock** (said on the strip) | |
| B8 | Early sounds | **Accept the limit first; mend in batch 3** | Mend first |
| B9 | Locks on a choice (FILTER TYPE) | **Allowed** (one value, not a wobble) | Amounts only, as MOD's `modTargetFits` |

### Batches (B)

| Batch | What |
| --- | --- |
| 0 | **Engine**: the flag, the at-once give and return in `fireStep`, `autoLocked`. `tests/lock_test.cpp`: a listening synth or effect reads the lock on the strike's own sample, back after, MOD on top, Stop, a scene move, blocks and helpers, every synth's macro taken at its strike. `effects_test --print` and `mod_test` must be the same (no lock: to the bit). |
| 1 | **App**: `autoValue` at every reader (grep every `automation[`), `setLock` / `moveLocks` / `deleteLocks`, the `lock` word, page 3 on both grids, the marks. `tests/lockapp_test.cpp`: the gesture, the file, undo, carry, delete, COPY A BAR, NEXT SCENE, a TYPE change. `under_test`, `textsize`, stills `lock-held`, `lock-notes-held`, `lock-marks`. |
| 2 | **DECAY**: drums.cpp and `plainRun`, its command field, its song-file number (appended at the end of `stepfx`, left off at 100), the tests the THROW way, the `bench` row. |
| 3 | Optional: a lock that waits for its step's earliest sound. Review, README, CLAUDE.md. |

### Risks (B)

- **A reader that forgets `autoValue`** shows or sends 2.something. Grep every `automation[` and every `autoNow` / `autoBase` read.
- **The old FILTER does not glide**, so a lock landing at once may click. Measure with `effects_test`'s tools. If it does, the return becomes a glide of 1/8 step.
- Each synth must take a macro given in `fireStep` at its strike. Test all nine.
- A lock is the step's, not the hit's: the early-sound limit stays until batch 3.

---

## C. A length per track (polymeter)

### What exists

- **A cell** is 1, 2, 4 or 8 bars:
  - `TrackData::bars` and `SceneStore::bars`;
  - set with `Session::setCellBars` / `SetCellBars`, and on SONG with A held + LEFT / RIGHT (`SongScreen::stepBars`).
  - A shorter cell goes round under the scene (`slotOf = scene × 8 + bar_ % bars`), **and starts again at every pass of the scene**: `bar_` goes back to 0.
- **Scene STEPS** cut every track: `ProjectHead::steps` and `Engine::steps_`, set with SELECT + L2 / R2 on PATTERN or A + LEFT / RIGHT on SONG's repeat box. It is the owner's 13-step loop.
- **One step clock** for every track: `nextStep_`, `bar_`, `following`. `fireStep` and `askAgain` use them as one; the automation index is `(slot % 8) × 16 + nextStep_`.
- **Seeds**:
  - FEEL and chord seeds take the bar of the cell (not the scene's number);
  - the `GlideTrail` and MOD positions use steps since Play;
  - an arpeggio holds its slot and step;
  - `feelRunBack` walks the cell's bars (`&scene - bar`).
- **The screens** draw the playhead from `engine.playBar() % bars` and `playStep()` (pattern_screen.cpp:1024, note_screen.cpp:1106, pattern_slices.cpp:80). The same reading is used by `Session::automated`, `recordAutomation` and `hearMidi`.
- `Project::glideLines` is handed the scene's steps.

### Design

1. **A cell's LOOP**: 1 to 128 steps (`TrackData::loop[scene]`, 0 = none). The cell's bars hold its steps, so a LOOP of 20 needs 2 bars.
2. **The rule** (`cell_rule.hpp`):
   - **A cell with no LOOP plays exactly as today**: it starts again each pass and is cut by STEPS.
   - **A cell with a LOOP runs on its own clock through the whole visit**: `place = visitStep % LOOP`, its bar `place / 16`, its step `place % 16`, and A's round is `visitStep / LOOP`.
   - **Why two rules**: STEPS means "cut every track"; a LOOP means "this track's own clock". With only "start again each pass", a 5-step LOOP in a one-bar scene would play 5, 5, 5, 1, which is not polymeter.
3. **The scene's length**: LOOP cells do not make the scene longer. `sceneBars` counts only the cells with no LOOP; a scene where every cell has a LOOP lasts its longest LOOP, rounded up to bars. `Project::sceneBars` and `Engine::sceneBars` must agree.
4. **The engine**:
   - `visitStep_` is 0 at Play and on entering another scene, and goes up by 1 each step. It is told to the app in an atomic, read together with the scene as `playScene` is.
   - `fireStep` works out each track's place now and after, instead of the shared `nextStep_` / `following`. The step after crosses into another scene at 0, or carries on in the same scene at `visitStep_ + 1`.
   - The drum loop, the notes' `note.step == ...`, the automation index, `askAheadHits` / `askAheadNotes` and `askAgain`'s "the same bar?" all go per track.
   - **`SceneStore` gains `loop`** (the scene-move rule); `sceneIn` and `sceneInto` copy it; `SceneImage` and `TrackImage` gain it; `Command::SetCellLoop` is appended.
5. **The seeds**:
   - "bar of the cell" keeps its meaning (the place's bar);
   - `feelRunBack` never goes back over the LOOP's start;
   - the GlideTrail, the arpeggio clocks and PAIRED are unaffected;
   - `Project::glideLines` is given the LOOP as both the cell length and the "scene" length, because a LOOP goes round under itself.
6. **Copying**: COPY A BAR is unchanged, and a bar past the LOOP's last bar is refused. The SONG cell copy, NEXT SCENE (`moveScenesHere`'s carry) and `sceneImage` carry the LOOP.
7. **The song file**: `loop N` after `bars`, left out at 0, read through `wholeIn`. The steps past the LOOP are hidden and kept (`hidesBars`). Undo comes free through the text.
8. **The screens**:
   - **SONG: A held on a cell + UP / DOWN = LOOP a step shorter / longer** (LEFT / RIGHT stay the bars). Going past the cell's bars grows the cell, filled as `setCellBars` fills, but not the scene.
   - The cell box reads its LOOP (for example "L5").
   - The grids shade the steps past the LOOP, as they shade steps past STEPS. The strip's STEP reads "3 OF 5".
   - Each track's playhead comes from `visitStep` through `cell_rule.hpp`.
   - **PATTERN (call C5)**: hold L1 (the bar key) + L2 / R2 for the LOOP.

### The owner's calls (C)

| # | Question | Recommended | Other |
| --- | --- | --- | --- |
| C1 | A LOOP | **Runs on through the scene's passes** | Starts again each pass (5, 5, 5, 1 in a 16-step scene) |
| C2 | It starts again | **At Play and on entering the scene** | Only at Play |
| C3 | Range | **1 to 128** (all a cell holds) | 1 to 64 |
| C4 | LOOP cells and the scene's length | **They do not lengthen it** | They do (as bars) |
| C5 | The gesture | **SONG A + UP / DOWN; PATTERN L1 + L2 / R2** | SONG only |
| C6 | Cells with no LOOP | **Unchanged** (start again each pass, cut by STEPS) | Run on too (changes a 3-under-4 bar cell) |
| C7 | The roll's glide lines | **The LOOP going round under itself** | |

### Batches (C)

| Batch | What |
| --- | --- |
| 0 | **Engine**: `cell_rule.hpp`'s place and round, `visitStep_`, the place per track through `fireStep`, the two asking functions and `askAgain`, the automation index, `SetCellLoop`, `SceneStore::loop`. `tests/loop_test.cpp`: 5 against 16 by hand; notes, chords, arpeggios, PAIRED glides; FEEL / SWING / slices; automation per track; ratchets; early hits at the LOOP's top; a scene alone, repeats, a song, AT END STOP, a bounce; blocks and helpers. `nextscene_test`'s song gets a LOOP track; run TSan again. `speed_test` and `bench`'s step-clock rows. `effects_test --print` must be the same (no LOOP: to the bit). |
| 1 | **App**: `TrackData::loop`, the file, `trackImage` / `sceneImage`, both `sceneBars`, `Session::setCellLoop`, the SONG gesture and box, the grids' shading and playheads, `glideLines`, A's round. `tests/loopapp_test.cpp`, `glideapp_test`, `copybar_test`; stills `loop-song`, `loop-grid`, `loop-roll`; `textsize` at XL. |
| 2 | Review, README, CLAUDE.md. |

### Risks (C)

- **The step clock is the engine's heart.** The scene-move proofs and the last-step mend (`askAgain`, `askedFrom` / `askedBar`) become per track, so the full `nextscene_test` and TSan must pass again.
- **Two rules for "goes round"** must be said plainly in the manual.
- STEPPER MODs still run by the song's steps, not by a track's LOOP.

---

## D. LIVE: play from the handheld's buttons, recorded, quantized

### What exists

- **The USB keyboard**:
  - The thread in `midi.cpp` sends with `Engine::sendLive` (NoteOn, NoteOff, Trigger, its own queue, taken at the next render).
  - `Session::hearMidi` writes into the cell once a frame: `midiDrumLane`, `sendDrumHit`, `recordingNote_[128]`, a length in whole steps up to the bar's end.
  - **The hit is placed on `engine.playStep()` as read that frame**: the step playing, never the nearest, up to a frame plus a block late.
  - It records with `midiRecord_` on (SET: USB KEYBOARD PLAY + RECORD, `midirec`).
- **Hearing by hand**: `Session::audition`, `auditionHeld`, `hearLaneHeld`, all at `kPlacedLevel`.
- **WRITE** (SELECT + X) records knob moves while playing.
- **A hit's and a note's nudge** is -0.5 to 0.5 of a step, which is exactly what "off" and a percentage need.
- **Undo**: a still moment of 700 ms ends a step (`noteEdit`, `editing_`).
- **main.cpp** reads the buttons once a pass and then waits 16 ms (`SDL_Delay(16)`); the sound runs in blocks of 256 frames (5.3 ms).
- **Chord packs** carry a key and a scale (`sourceTonic`, `scale.pitchClasses` in `parseChordPack`), but `ChordPack` keeps only the chords.
- **Tabs**: `View` has six; the top bar has 32 px left for the screen's words while playing with WRITE on (`TopBar::wordsRoom`).

### Design

1. **Where**: **LIVE, a seventh tab after PATTERN** (call D1). Every face button and the D-pad are keys there, so it cannot share a grid's buttons. LIVE's tab takes about 36 px, so **while playing with WRITE on, the screen's own words must give way.** The alternative is a mode of PATTERN toggled by SELECT + L3, the one free pair there.
2. **The keys** (one table, `kLiveKeys`):
   - **A drum track**: B kick (lane 1), A snare (2), Y closed hat (3), X open hat (4); D-pad DOWN clap (5), LEFT tom (6), RIGHT rim (7), UP crash (8).
   - **A synth or sampler track**: 8 degrees of the song's chord pack's key and scale (the built-in pack is C major): LEFT 1, DOWN 2, UP 3, RIGHT 4, Y 5, B 6, X 7, A 8. `ChordPack` must keep its key and scale for this.
   - **Shoulders**: on a synth, L1 / R1 move the octave down / up (kept, and shown). On drums, L1 held plays soft (50%) and R1 held plays an accent (100%); otherwise keys play at `kPlacedLevel`.
   - **The stick**: a click sideways is the track before / after.
   - **The rest**: START plays and stops (FILL when held, from A). SELECT combos work as everywhere. **SELECT + L2 / R2 here is QUANTIZE less / more.** L2 / R2 turn the tabs. **B is a key here**, not delete; the screen says so, and SELECT + B still undoes.
3. **Sound**: a press sends at once on the path hearing already uses (Trigger, or NoteOn and its NoteOff on let go).
4. **Recording is armed by WRITE** (SELECT + X), only while playing.
   - **The engine says where a key landed**: Trigger / NoteOn / NoteOff with `e & 2` set, and `shift` = the press's age in ms when sent.
   - When it applies one, the engine pushes a `Stamp` onto a new queue from the engine to the app (`SpscQueue<Stamp, 256>`, `Engine::takeStamp`). The stamp says: track, lane or key, scene, the track's place (C's), the share of the step, steps since Play, velocity, on or off.
   - The place is taken back by the press's age plus the output's buffer, because the player plays to what they hear.
   - **The USB keyboard moves onto the same path** (`sendLive` with the flag), so it gets quantize too.
5. **Quantize** (settings.txt `livequant`): **OFF, 50%, 75%, 100% (snap, the default)**. The quantize maths is in `app/live_rule.hpp`, one place, tested by hand.
   - The place p (in steps into the cell) goes on `round(p)`, going round inside the cell: a press just before the cell's top lands on its first step.
   - The nudge is `(p - round(p)) × (1 - Q)`, which fits the nudge's range exactly.
   - **A note's length**: whole steps at 100% (at least one); otherwise as held (at least `kShortestNote`); held to the cell's end.
6. **No double hit**: a key played early is written onto the step about to play, which would play it again a moment later.
   - **The engine keeps the moment each recording key was struck** (`Track::liveAt[lane]`, `liveKeyAt[128]`).
   - The pattern's hit or note within half a step after that moment is not played, and a waiting one not yet begun is dropped.
   - This applies only while recording.
7. **OVERDUB or REPLACE** (settings.txt `livemode`; a SET row, LIVE RECORD):
   - OVERDUB adds: a hit already there takes the new level, and a note on the same step and key is replaced.
   - **REPLACE: from a lane's first key in a take, that lane's hits are emptied where the playhead passes, for one time round of the cell.** On a synth track the same goes for its loose notes; whether chords are kept is call D7.
8. **Undo: a take is one step**, from its first key written to WRITE off, Stop or leaving LIVE (`Session::beginTake` / `endTake` hold the burst open).
9. **A scene move during a take** moves the held notes' scene, as `recordingNote_` is moved today.
10. **Count-in** (from the easy batch): START with WRITE on counts in. A key in the count's last half step lands on step 1; any key before that is heard only.
11. **Latency**:
    - From press to sound: up to a pass (16 ms), plus a block, plus the output buffer.
    - The place recorded is mended by the stamp.
    - **To cut the pass, LIVE waits for events** (`SDL_WaitEventTimeout`, up to 16 ms) so a press goes out at once. This touches the redraw's pass rule, so `idle_test` must still hold the picture.
    - A SET row, LIVE LATE BY (ms, default 0), takes up whatever is left.
    - **Not measured on the handheld.**
12. **The screen**:
    - The top half is the cell's bar shown small, as its grid draws it, with the playhead.
    - The bottom half is two diamonds of keys, each with its lane's or note's name, lit when pressed.
    - It also shows the QUANT, OVERDUB / REPLACE and REC words.
    - The tight rows use the fixed lettering.

### The owner's calls (D)

| # | Question | Recommended | Other |
| --- | --- | --- | --- |
| D1 | Where | **A seventh tab, LIVE** (the screen's words give way while playing with WRITE on) | A mode of PATTERN, SELECT + L3 |
| D2 | Drum keys | **B kick, A snare, Y closed hat, X open hat; D-pad clap, tom, rim, crash** | The grid's lane order, D-pad first |
| D3 | Note keys and their scale | **8 degrees, left to right; the chord pack's key and scale** | A SET row, SCALE |
| D4 | Loudness | **L1 held soft, R1 held accent (drums); `kPlacedLevel` otherwise** | The stick's push |
| D5 | What arms recording | **WRITE** (SELECT + X) | A REC of its own |
| D6 | Quantize | **OFF, 50, 75, 100%; 100% to start** | OFF to start |
| D7 | REPLACE empties | **The lanes struck** (a synth: its loose notes) | The whole cell |
| D8 | The USB keyboard | **Onto the stamp path and quantize** | As it is |
| D9 | Latency | **LIVE waits for events; a LATE BY row at 0** | Neither |
| D10 | Chords played live | **Later** | Now |
| D11 | B as a key in LIVE | **Yes** (said on the screen) | |

### Batches (D)

| Batch | What |
| --- | --- |
| 0 | **Engine**: the stamp flag, the `Stamp` queue (a lost stamp counted), the taking back of the age and buffer, `liveAt` and the suppression. `tests/live_test.cpp`: a stamp exact at blocks 1 to 333, during a ratchet, while a scene moves; a double suppressed only within half a step; the queue full and counted. |
| 1 | **App recording**: `live_rule.hpp` (quantize by hand), takes in undo, OVERDUB / REPLACE, the USB keyboard moved over. `tests/liveapp_test.cpp`. |
| 2 | **The LIVE screen**: the tab, the keys, the drawing, the SET rows, the hints. Stills `live`, `live-notes`, `live-rec`, `live-replace`; `textsize`, `idle_test` (keys lit), `themeviews`, the words test (SEL, never SELECT). |
| 3 | **Latency**: waiting for events in LIVE, the LATE BY row, a desk timing from press to sound. Review, README, CLAUDE.md. |

### Risks (D)

- **The top bar's room with a seventh tab.**
- **The main loop's waiting changes the redraw's passes.**
- A new queue on the audio thread.
- **B as a key** goes against the B standard and muscle memory.
- It depends on the count-in from the easy batch.
- **The keys' layout has not been tried in a hand, and nothing here can be heard by us.**

---

## Critical files for implementation

- `/home/user/apotheca/engine/src/engine.cpp`: `fireStep`, `schedule`, `askAheadHits`, `askAheadNotes`, `askAgain`, `sceneIn`, `glideAutomation`, `apply`
- `/home/user/apotheca/engine/include/apotheca/engine.hpp`: `Command`, `SceneStore`, `TrackImage` / `SceneImage`, `Track`, the step-clock state, `dice_`
- `/home/user/apotheca/app/session.cpp`: `sceneInto`, `setAutomation` / `recordAutomation`, `setCellBars`, `noteEdit`
- `/home/user/apotheca/app/song_file.cpp`: `putDrumScenes`, `putNoteScenes`, `putAutomation`, `parseSong`
- `/home/user/apotheca/app/pattern_screen.cpp` and `/home/user/apotheca/app/note_screen.cpp`: the X-held strips
- `/home/user/apotheca/app/app.cpp`: L2 / R2 and START routing
- `/home/user/apotheca/app/session_synth.cpp`: `hearMidi`
- `/home/user/apotheca/app/project.hpp`: `DrumPattern`, `NoteEvent` / `ChordEvent` spare bytes, `TrackData`, `sceneBars`
