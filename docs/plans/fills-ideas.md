# Fills: ideas

**Build design 1, the FILL bar: hold L1 + Y on the grid gives a cell one extra bar that plays only in the scene's last bar, on its last time round.** Add design 2's engine fixes. The owner's case drops from **23 + F presses (63 + F with 12 tracks) to 4 + F**, in one scene.

**Nothing here is built, run or heard.** All of it comes from reading the code and docs. No stills were made. (F = the presses to draw the fill itself. A cell = one track in one scene. REPEATS = how many times a scene plays.)

---

## Today

**The fastest way today is to copy the whole scene on SONG: 8 + F, whatever the track count.**

| Route | 4 tracks | 12 tracks | Scenes after |
| --- | --- | --- | --- |
| The owner's route (copy each cell) | 23 + F | 63 + F | 2 |
| **Copy the scene** | **8 + F** | **8 + F** | 2 |
| NEXT SCENE (SEL + Y) | 9 + F | 9 + F | 2 |

How the scene copy goes:
1. Hold A + Down: REPEATS 4 → 3 (2 presses).
2. R1 on the REPEATS box, Down, R1: the copy pastes on the let go (3).
3. B tap: the copy's REPEATS back to 1. It carries the 3 with it (session.cpp:690) (1).
4. Right, A: opens the drum cell (2). Draw the fill.

**The cost: two scenes.** Every later change to the groove has to be made twice.

---

## The four designs

**Scores** (average of two judges, out of 10; for Effort, 10 = least work):

| Design | Speed | Fit | Power | Effort | Total | Owner's case |
| --- | --- | --- | --- | --- | --- | --- |
| 1. FILL on L1 + Y | 8.5 | **8** | 7 | 6 | **29.5** | 4 + F |
| 2. FILL with LAST / EVERY | 8.5 | 7 | 8 | 6 | **29.5** | 4 + F |
| 3. LAST hits on their own stop | 7 | 5 | 6.5 | 4 | 22.5 | 4 + F |
| 4. Fill bar + hold START | 7.5 | 4 | **9** | 2 | 22.5 | 5 + F |

### 1. FILL on L1 + Y (last time round only)

**Idea.** Each cell can have one FILL bar. It plays instead of the cell's normal bar in the scene's last bar, on the scene's last time round.

**Gestures** (SONG, cursor on scene 1's REPEATS box, REPEATS 4, 1-bar cells, drums in column 1):
1. **Right**: onto the drum cell.
2. **A**: the grid opens on bar 1 (song_screen.cpp:502-506).
3. **Hold L1, press Y**: the FILL is made as a copy of bar 1, and the grid moves onto it. The top bar reads "DRUMS / FILL / SCENE 1".
4. **Draw the fill.**

**Presses: 4 + F, with 4 tracks or 12.** From the grid it is 2 + F. REPEATS stays 4. One scene. No other track is touched.

The rest:
- L1 + Up/Down: 1 … last bar → FILL → 1.
- L1 + Y on the FILL turns it off. The bar is kept.
- L1 + R1 / L1 + A copy and paste onto it, as with any bar.

**What it changes:**
- **Storage: the cell's 8th bar slot**, which a 1-, 2- or 4-bar cell never plays (`slotOf` plays `bar % bars`, engine.hpp:845). Plus one flag per cell. No new pattern storage.
- `slotOf` returns slot 7 when the FILL is due. A new Rack-free rule file holds the one test.
- A scene played alone counts its repeats (engine.cpp:1971), so the 4th loop plays the FILL.
- One `fill 1` line per cell in the song file. Old songs sound the same to the bit.
- Copy a bar, copy a cell, copy a scene, NEXT SCENE and undo all carry it.

**Effort: medium.**

**Main flaw: it misses four places the others caught.**
- **MIDI export leaves the fill out.** It gathers one pass and repeats it (bounce.cpp:104-115, 294-299).
- **Changing `repeat_` in a loop also changes the ring lights** (session_songs.cpp:353), not just the progress bars.
- **The roll catches Y first** (note_screen.cpp:659), before the L1 block (:692). L1 + Y there would open the chord pick.
- **A REPEATS change on the last step is not asked again** (engine.cpp:1427-1429 has no `askAgain`).

### 2. FILL with LAST / EVERY

**Idea.** Same bar-8 storage as design 1. The FILL plays on the scene's last bar either on the last time round (LAST) or every time round (EVERY). Y on the BARS stop steps OFF → LAST → EVERY; L1 + Y is the shortcut.

**Gestures:** Right, A, **hold L1 + Y** (says "FILL: LAST TIME ROUND"), draw.

**Presses: 4 + F.** Without the L1 + Y shortcut, it is 7 + F (Up, Up to BARS, Y, Down, Down).

**What it changes:** everything in design 1, plus:
- A separate `loopPass_` counter, so `repeat_` and its readers stay as they are.
- `askAgain` on SetSceneRepeats and SetCellFill.
- MIDI export plays the fill on the last time round.

**Effort: medium.**

**Main flaw: EVERY is a trap in the owner's own song.**
- With 1-bar cells, every bar is the scene's last bar. EVERY then plays the fill on every bar, and the groove never plays.
- A second L1 + Y, meant only to look at the fill, turns LAST into EVERY.
- Also: it puts the new command "after GiveScene". That moves SetClick's id (engine.hpp:265). **Ids are append-only**, so it must go after SetClick.

### 3. LAST hits on their own stop

**Idea.** Hits, notes and chords get a condition: LAST (plays only on the last time round) or NOT LAST. L1 + Up from bar 1 wraps to a new stop, "BAR 1 LAST". There, A places last-time hits and B takes a hit out of the last time only.

**Gestures:** Right, A, **hold L1 + Up** (to BAR 1 LAST), draw.

**Presses: 4 + F.**

**What it changes:**
- A `cond` byte in StepFx and in spare bytes of notes and chords (project.hpp:55, :85).
- A pass counter in the engine.
- Three new song-file words.
- It joins plan A's step-condition family (big-features-plan.md:5).

**Effort: medium on paper. The judges call it nearer large.**

**Main flaw: it cannot change a hit that is already there.**
- The grid holds one hit per lane per step. So "the backbeat snare ratchets on the last pass" cannot be written, and that is the most common drum fill.
- **B in the stop means "not on the last time", not delete.** That bends the button standards.
- New L / !L tags on every grid touch the settled look.
- No automation (no sweep into the next scene).
- An older build plays the fill on every pass.

### 4. Fill bar + hold START + written WHEN

**Idea.** Every cell can hold a 9th bar, its fill bar. FILL comes on live (hold START while playing; L1 + START for the next bar), or it is written per scene: Y on REPEATS sets END, EVERY 2/4/8, with a chance.

**Gestures** for the written case:
1. **Y** on REPEATS: OFF → END.
2. **Right**.
3. **A**.
4. **Hold L1 + Up**: the FILL stop.
5. Draw.

Live: **hold START, 1 press per fill.**

**Presses: 5 + F.**

**What it changes:**
- A 9th slot. That changes the slot stride across the audio thread: `slotOf`, `DrumsOf`/`NotesOf` (engine.hpp:696-703) and every `/ kMaxBars` and `% kMaxBars` in fireStep.
- **START stops on the let go of a tap**, not on the press.
- SELECT + START quit needs a press-order rule (main.cpp:623).
- Two new head lines per song.

**Effort: large.**

**Main flaw: two settings in two places for one job.**
- Draw the fill but miss the Y on REPEATS, and the song never plays it.
- It changes START, a playing gesture. Hold START is only plan A's recommendation (call A8, big-features-plan.md:112), **not an owner decision**.
- `fillFrom` ("takes over from the first changed beat") cannot be seen.
- The fill bar has no automation of its own.

---

## Recommendation

**Build design 1's gesture and meaning, on design 2's engine work. Leave EVERY out for now.**

Why:
- **It ties for fastest (4 + F) and has the best fit.** L1 is the bar key and Y is the special. Y on the BARS stop and L1 + Y are both free today (pattern_screen.cpp:808-822, note_screen.cpp:659).
- **One meaning per press.** L1 + Y makes the FILL, jumps to it, or hides it. Nothing cycles into a trap.
- **No new pattern storage.** The unplayed 8th slot holds it, so every copy carries it, and automation (risers) comes with it.
- **Design 3 cannot change an existing hit. Design 4 changes START and the slot stride.** Both cost more and fit worse.
- **It leaves room.** "1 IN 2", EVERY or DICE can be added later as new values on the same byte. Plan A's hold START could play every cell's FILL.

**Fixes to fold in** (from the judges):

| Problem | Fix | Where |
| --- | --- | --- |
| MIDI export has no fill | Gather the last time round with the FILL | bounce.cpp:104-115, 290-299 |
| REPEATS or FILL changed on the last step | Call `askAgain` | engine.cpp:1427-1429, new SetCellFill |
| Ring lights change in a loop | A separate `loopPass_`; show REPEATS blocks from it | engine.cpp:1971, session_songs.cpp:353, ui.cpp:161 |
| New command id | Append after SetClick | engine.hpp:265 |
| Roll takes Y first | Catch L1 + Y above the Y block | note_screen.cpp:659 |
| Y's let go rolls dice or steps chance | Swallow it after L1 + Y | pattern_screen.cpp:815-820 |
| The view is pushed off slot 7 | Allow the FILL past the `bar >= bars` guard | pattern_screen.cpp:887 |
| An emptied FILL (a drop) comes back as a copy | Keep a "made" state, so empty stays empty | new `fillHere()` |
| Emptying a scene leaves the flag | Clear it too | session.cpp:776-790 |

**What it still cannot do:**
- No FILL on an 8-bar cell. Its 8th bar is its own, and editing bar 8 does the job there.
- A fill only at the scene's end.
- To hear it in a loop, you wait the whole REPEATS.

### Questions for the owner before it is built

1. **"Bring the bar count down to 3"**: did you mean REPEATS 4 → 3, then a fill bar? A FILL at REPEATS 4 gives exactly that in one scene. If you meant a 3-bar cell, that is plan C (a length per track).
2. **A scene played alone**: should the FILL come on its last time round, as in the song (recommended), or every time round while you draw it?
3. **FOLLOW while the FILL is shown**: the view jumps back to bar 1 after 1.5 s (follow_bar.hpp, owner 2026-11-18). Should it hold still on the FILL instead (recommended)?
4. **A 4-bar cell with a FILL, made 8 bars**: the FILL becomes bar 8 and goes off (recommended), or refuse?
5. **The word FILL**: plan A also uses it, for a step condition and hold START (big-features-plan.md:95). One meaning? And later, should hold START play every cell's FILL bar?
6. **When to build**: now, before the four big features (owner, 2026-11-17: they come after the easier batch), or as the first slice of plan A?
