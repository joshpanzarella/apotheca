# Gesture audit: buttons, speed and the EQ band

Audit of 2026-11-24: 451 gestures mapped across seven areas, judged by four lenses (consistency, logic, speed, the written standards), every finding checked against the code and against the owner's recorded decisions. Read from the code and the tests only: nothing here was tried on the handheld.

- **The app is mostly consistent and logical.** The written button standards hold on nearly every screen. The faults are in things built after the standards: preset names, SELECT combos off PATTERN, the stick, and BAND's step.
- **Two gestures lose work for good.** SONG's hold B can delete the wrong track (G02). One tap of Y (CATCH) or B replaces a lane's or a sampler's sound outside undo (G04).
- **Biggest speed wins:**
  - read each press the moment it lands (G06)
  - the right repeat speed for held lists (G08)
  - re-save a tweaked preset in 2 presses instead of about 29 (G01)
  - one press back to the sound you started a preset hunt from (G12)
  - L3 to MASTER working on the stock handheld (G05)
- **The EQ band you asked for is already built (BAND, 0.883) and cheap.** It moves one dial at a time. Its FREQ step can be fixed now; a two-way grip is your call (section 3).
- **Nothing was changed, built or run on the handheld.** All of this comes from reading the code and tests. Handheld timings are estimates.

**Status, 2026-11-25 (after the gesture batches):**
- **Every row of section 1 is done.** A few left a part out; the Done column says which part and why.
- **Section 2: most are done, under your "any of my decisions can be reversed if there is a more logical method" (2026-11-24).** Not done, still your call: G24 (a grip to the track heads), G21's direction options, G22's one save button, G20 (a question), G41 (no room for the mark).
- The Done column names the batch. Its commit message starts `Gestures [KEY]`: B1 918e3ee, A1 0ea0d25, B2 4d858bc, A2 1cd40d7, B3 f3279d1, A3 512e128, B4 4a0f73f, A4 5781eb3, B5 d10c005, A5 d0ec52a, C1 76fdca4, C3 edc966e, and C4-docs (docs only).
- **Still not run or heard on the handheld.** Only the desk tests and stills have seen any of it.

Words used below:
- **pass**: one turn of the program's loop (read the buttons, update, draw).
- **cursor speed and list speed**: the two speeds of a held key, set in `ui.hpp`.
  - Cursor: first repeat after 170 ms, then every 30 ms (33 a second).
  - List: first repeat after 200 ms, then every 110 ms (9 a second).
- **undo record**: what is saved before a change so SEL + B can put it back.
- **the panic**: all four shoulders at once, which stops every sound.

## 1. Fix now

None of these reverses an owner decision. They are ranked by gain times how often you would meet them.

### Do first

| # | ID | What is wrong | The fix | Gain | Done |
|---|---|---|---|---|---|
| 1 | G06 | Every press waits out a fixed 16 ms sleep before it is read. Tap tempo times taps by the pass, not by the press. | Sleep in four 4 ms slices and act on a button as soon as one arrives. Stick wobble does not count as a press. Keep the 16 ms idle loop. Give tap tempo the press's own time. | **Every press on every screen comes sooner:** up to 16 ms on the desk, an estimated 20-40 ms on the handheld. Tap tempo is off by about 1 ms, not one pass. | **Done** (B1). Not done: SDL_WaitEventTimeout (it wakes on the stick too, so four 4 ms naps instead); a pass of max(work, 16 ms) waits for SPEED TEST on the handheld. |
| 2 | G08 | Lists and choices repeat at cursor speed: the song list, synth and sampler presets (each loads and sounds), FX TIME and filter dials, BARS. KIT rows and VOICE knobs repeat at list speed instead. Sampler LOOP (on or off) takes 1.3 s of stick. Repeats count from the pass, so a held key does about 28 a second, not 33. | One repeat helper in `ui.hpp` that counts from when each repeat was due, as the recordings list does. Each screen names cursor or list, as the standard says. The instrument list repeats. LOOP clicks once per push. | **A held list stops where you let go.** KIT rows move 3.7x faster when held. LOOP takes one push. Used many times a session. | **Done** (C1). Left at the cursor's pace, not made slower: the song list, presets, FX's choice dials and BARS ("faster is better"); your call. |
| 3 | G03 | SELECT + Y (NEXT SCENE) and SELECT + L2/R2 (scene length) work only on PATTERN. On other tabs SELECT + Y falls through to the screen's own Y: **CATCH replaces a sound, PHYSIC rolls a new patch, SONG mutes.** | The App takes both combos on every tab and never passes a SELECT-held Y to a screen. The song list's save-a-rev stays. On SONG, NEXT SCENE copies the scene under the cursor. | No stray CATCH or re-roll. NEXT SCENE from DEVICE, FX or MOD: 2-8 presses become 1. A scene can be inserted without leaving SONG. | **Done** (A1). |
| 4 | G01 | On the synth and sampler preset keys, **B throws the typed name away.** That breaks your rule (2026-11-15: B keeps it, "a new one does too"). Tabs, SELECT's undo and tempo still act while you type. | One set of name keys in all four places. B keeps (saves) and leaves. Every button belongs to the keys while typing. Keys repeat at list speed. R1 on your own preset opens with its name typed in. Say SAVED or REPLACED, on START too. | No lost names ("PAD 2" is 29 presses). **Re-saving a tweaked preset of your own: about 25-29 presses become 2.** | **Done** (A3). The 2-press re-save works on synths only: the sampler's PRESET reads "-" once its sound is turned. |
| 5 | G12 | When you step through sounds with A held, B with A held puts back the starting sound on KIT, FX and MOD. It does not on synth or sampler presets, CLOUD recordings or a lane's sound. Each preset you stop on is its own undo step. | Keep what was playing when A went down. B with A held puts the whole sound back, its effects too. Use one function, shared with the KIT stop. | **Back to the start after a hunt: about 10 undos become 1 press.** Today it is impossible past 20 steps, or for a recording. | **Done** (A4). The mixer's A + B stays a reset (yours, 2026-11-14). |
| 6 | G05 | **L3 (MASTER from any screen, your call of 2026-11-17) never fires on the stock handheld.** The button setup never asks for it. The setup screen also promises a "hold any button 2 s at start" that does not exist. | Add L3 and R3 to the setup as skippable steps, like MENU. If an older button map has no L3, ask for L3 alone once. Fix the setup screen's line (the real way back is SET > CONTROLS, hold A). | MASTER in 1 press instead of 2-6, every session. | **Done** (A2). Not done: a help card line (the card is full). Whether the stock firmware sends the stick's click is still not known. |
| 7 | G04 | **One tap of Y (CATCH), or B on SOUND, replaces or empties a lane's or sampler's sound outside undo, with no warning.** After a CATCH, SEL + B puts back only the old FIT: half an undo. | Add a lane's recording, with its FIT, cut and voice, to the undo record. Do the same for a sampler or CLOUD track's recording. A WiFi rename or relink must update that record. Until this is done, say CANNOT BE UNDONE. | A tuned DRUM SYNTH voice or a cut recording can't be lost to one tap. CATCH becomes safe to try: catch, listen, SEL + B. | **Done** (B3). The CANNOT BE UNDONE stopgap was not needed: full undo came in at once. |
| 8 | G02 | **On SONG, hold B on a track deletes whichever track the cursor is on when the bar fills.** The cursor can still move while you hold. A track delete can't be undone. | Tie the hold to the track where B went down, as the scene hold already is. A move or any other press calls it off, as on FX. Add a test. Optional: empty the cell on B's let go, so a called-off hold changes nothing. | Closes the one path that deletes the wrong track for good. | **Done** (A1), the cell emptied on B's let go too. Not done: the optional shorter hold. |
| 9 | G13 | ADVANCED always opens on page 1 at the first stop, and L1/R1 put the cursor back on the first stop. On CLOUD that stop is RECORDING, so **"A, R1" records instead of turning the page.** | Remember the page and stop for each track, in memory, as FX does. L1/R1 keep the cursor's place. A page turn or a first visit never lands on CLOUD's RECORDING. | Back to a deep page: 5-16 presses become 1-2. The same knob across FM-6's operators: 1 press each. No stray recordings. | **Done** (A4). |
| 10 | G14 | The stick drops what the D-pad keeps. A note carried by stick ignores SCALE and the L1 octave. Letting go of X with the stick pushed jumps the cursor (FX and DEVICE wait). On SONG's REPEATS, L1 + stick moves away instead of turning the tempo. | Send the stick through the D-pad's code: carries through `carry()` (one octave per push with L1 held), `stickWait_` on both grids, and stick handling on REPEATS. | The stick does what the D-pad does. No off-scale notes from the stick. | **Done** (B2). |

What the top rows cost:
- **G06**: the handheld's SDL and battery use are not measured. Time it with SPEED TEST first. Your battery rule (2026-11-15) keeps the 16 ms idle loop.
- **G08**: the song list and presets get slower when held (9 a second). That follows the written two-speed rule, but goes against "faster is better" in `pattern_screen.hpp`. The 8-preset jump stays for big moves.
- **G01**: B no longer cancels a save. To cancel, press Y until the name is empty, then B, as on SET.
- **G03**: SELECT + Y no longer mutes on SONG; Y alone still does. A 2026-11-12 test that keeps NEXT SCENE on the grids (Claude's choice, not yours) changes, and so does the manual.
- **G05**: it is not known whether the stock firmware sends the stick click at all. Only your own firmware's button map has it.

### Then

| # | ID | What is wrong | The fix | Gain | Done |
|---|---|---|---|---|---|
| 11 | G09 | In the slice view, UP or DOWN on the lane name flips ONE SLICE / RUNS ON. A held key or the stick flips it up to 18 times a second. | UP sets ONE SLICE and DOWN sets RUNS ON. Send nothing when the setting is already there. | One press always lands. | **Done** (C1). |
| 12 | G11 | With X held on a hit or note (its effects showing), B deletes the hit. Nothing clears a step's effects in one go. | X held + B empties what X shows, as one undo step: a hit's pitch, ratchet, reverse, cut and THROW; a note's ratchet, glide and THROW; a chord's six effects. On a lane name it sets that lane's FEEL and SWING to OFF. | Clearing a step's effects while keeping its level, nudge and chance: about 13 presses become 2. There is no room for a chip, so it lives in the manual and help card only. | **Done** (B2). No chip (no room): the help card's B line and the manual say it. |
| 13 | G19 | BAND: one D-pad push on FREQ is half an octave, so 50 Hz and the 1 kHz start can't be reached. | Make FREQ's step a thirtieth of its range, which is a third of an octave. Start on a step (1.0 kHz). See section 3. | Notch 50 Hz with the D-pad. | **Done** (B4). |
| 14 | G27 | A chord's length can't be set while you pick it. A new chord takes the last length you set. | With Y held on the roll (synth tracks), L2/R2 halve or double the chord in hand. | An 8-step chord: about 10 presses become 2. | **Done** (B2). The length is said in the box above the bar, not in the chord readout (the look is settled). |
| 15 | G15 | SET acts on A's press. Hold A + RIGHT on BOUNCE starts a bounce that nothing on the handheld can stop. On TRACK NAME, RIGHT then moves the key cursor. | On BOUNCE and TRACK NAME, act on A's let go, and only if no direction went down. Hold B on a running bounce stops it (a 1 s bar, like NETWORK's). | No stray bounce files and no stray load on the handheld. | **Done** (A2). |
| 16 | G10 | The delete words are wrong both ways:<br>• FX says "DELETING: CANNOT BE UNDONE", but effects have been in undo since 2026-11-14, and a test covers it.<br>• The sampler preset delete (a file, gone for good) shows no words.<br>• REPEATS says "HOLD B delete" for an empty that can be undone.<br>• The help card says undo is "the last change to the patterns". | • FX: "DELETING THIS EFFECT", like MOD's words.<br>• Sampler preset: the kit's CANNOT BE UNDONE.<br>• REPEATS: "HOLD B empty".<br>• Help card: "UNDO the last change".<br>• Add erase, remove and rub out to the words test. | You can trust the words. An effect deleted by mistake is 2 presses away, not rebuilt by hand. | **Done** (A3). Left: the grids' HOLD B delete (labelled on purpose by the UI audit, paired with HOLD R1 paint); EDIT and VOICE had no wrong words. |
| 17 | G25 | The panic leaves a recording armed if R1 went down first. When the take ends, it replaces the sound. | Note the recording state at the first shoulder. Drop any recording started on the way (`dropRecording` already exists). | No surprise sound swap after a panic. | **Done** (A2). Not done: a take already running that R1 ends on the way is kept (the manual says so). |
| 18 | G21 | On the drum grid's BARS stop, hold A (or Y) + LEFT/RIGHT moves the bar once, then the held repeat stops working. The header comment gives the L1 directions backwards. | Make the held repeat do what the press did. Fix the comment and `drums-and-samples.md`. | A held key keeps going. | **Done** (C1): the held repeat and the backwards comment and doc. Which way the count runs is section 2's G21, not done. |
| 19 | G31 | With L1 held on the grids, the stick moves the cursor but does not count as using L1. Letting go of L1 still opens the slices or zooms. | A stick move while L1 is held counts as a use. | No stray slice view or zoom. | **Done** (B2), the stick part. The two copy timings and the roll's BARS zoom were kept: both deliberate (section 4). |
| 20 | G39 | START does nothing while SONG's song list or instrument list is open. The help card says START works on every screen. | START plays and stops there too, but not with SELECT held (SEL + START quits). | Stopping the song: 1 press instead of 2-3. | **Done** (A1). The update notice stays modal. |
| 21 | G38 | Without a MENU button, holding SELECT for 0.4 s raises the help card even mid-combo. It covers "UNDONE (n MORE)" and the volume box. | Raise the card only while SELECT is held by itself. | You see each undo and volume step. | **Done** (A2). |
| 22 | G44 | On FX and MOD, L1/R1 to the next track lets go of A, so turning one dial across tracks takes a fresh A each time. | Keep the hold through L1/R1 only when the new track has the same effect in that slot. Drop it otherwise. | One press per track in a mixing pass. | **Done** (B4). |
| 23 | G45 | FIT starts at OFF, so a loop's usual 1 BAR is the fifth click. | A tap of A on FIT sets the bars the recording fills at the tempo, rounded to FIT's stops (up to 4 bars, in the scene's meter). It does nothing on built-in or DRUM SYNTH lanes. | 5-6 presses become 1 per loop. | **Done** (A4). |
| 24 | G30 | Your own synth presets can't be deleted on the handheld, though kits and sampler presets can. The WiFi page's folders don't include presets either. | Hold B on one of your own synth presets deletes it: a 1 s bar and CANNOT BE UNDONE. The track keeps its sound. | Clean up without the SD card. | **Done** (A3). |
| 25 | G34 | The recording line says "OF 10", but the limit is 20 s (your call of 2026-11-18). It says "R1 TO STOP" where R1 saves or turns the page instead. | Print the real limit. While a take waits or runs, R1 stops it from any stop on that screen. | The right number, and no mic left running. | **Done** (A4). |
| 26 | G42 | In the recordings list, letting go of a delete early closes the list. The empty list names Y and R1, which do nothing there. | A let go after about 0.25 s is not a tap. A quick tap still puts back the old recording and closes. In an empty list, Y catches and R1 records. | Stopping a delete no longer means reopening the list. | **Done** (B4). |
| 27 | G43 | Every SET step and volume press writes a file and forces the card to save it (two "fsync" calls). That is 9 a second when held, even when nothing changed. | Skip the write when nothing changed (no risk). Then write only once the value has rested about 1 s, and on quit. | No card writes during a held run. Not measured. | **Done** (A2). The stock firmware's power key path is not known, so a cut there may lose the last second's setting. |
| 28 | G40 | On the update notice, a tap of B deletes the program you sent, while accepting takes a 1 s hold. The words say "throw it away". | Refusing becomes hold B for 1 s with a bar, worded "B delete it". | A stray B no longer means sending the update again from the desk. | **Done** (A2). |
| 29 | docs | Some docs describe gestures that have changed:<br>• SONG's "hold R1 and drag" (it is a tap now)<br>• "Y does nothing on FX" (hold Y moves an effect)<br>• the recordings list moving "fifteen" rows a second by stick (it is 60)<br>• STICK SPEED as a number in `ui.hpp` (it is a SET setting)<br>• FIT "not undone" (it is)<br>• BAND's "third of an octave" comment | Correct the words. Add to your 2026-11-12 FX note rather than rewriting it. | The manual matches the device. | **Done** (C4-docs). The batches had put FIT, STICK SPEED, SONG's R1 and BAND's comment right. C4 added to the FX note in `screens.md`, gave the stick's 60 rows a second, and fixed the FX and SONG headers' stale keys. |

## 2. Your call

Each of these changes a standard or one of your decisions, or is a matter of taste.

| ID | What is wrong | Proposal | Gain | Trade-off | Done |
|---|---|---|---|---|---|
| G07 | At your stick speed (75%), the stick is slower than a held D-pad on most screens: SET moves 5 rows a second against 9, FX 10.5 against 33. It never speeds up. | Cursors only. A partial push works as today. A full push held climbs to about 3x over a second, scaled by STICK SPEED. Value turns don't change. | SONG scene 1 to 40: about 2.2 s becomes about 1 s. SET TEMPO to CONTROLS: about 3.2 s becomes about 1.2 s. | You asked for the stick a quarter slower (2026-11). STICK SPEED is now a SET setting (25-150%). This only speeds up a long full push. Kit sounds stay slow on purpose ("slow enough to hear each sound"). | **Done** (A5). **Replaced by the owner, 2026-11-25**: "As soon as you move it, it goes as fast as you set the speed to. Stick for macro moves and d-pad for detailed ones." The climb is gone: any push past the dead zone runs at once at twice the held D-pad's pace, times STICK SPEED (a cursor 50 a second at 75%, a list 13.6, a click 12; at 50% the D-pad's own), the kit's sounds included (docs/dev/screens.md). |
| G35 | Lists, rows and tabs either go round at the end or stop, with no rule you can see. SONG to SET is R2 five times, and L2 on SONG does nothing. Down from drum lane 8 skips TEMPO and BARS, but up from lane 1 reaches them. | Tabs go round. Long lists (SET's rows, the song list) go round on a fresh press and stop on a held repeat. | SONG to SET: 5 presses become 1. SET's last row to TEMPO: 23 become 1. | Tracks are left out: "nothing in the program wraps tracks" (owner, 2026-11-14). FX TYPE is left out: "The ends stop" (owner, 2026-11-14). A quick run of L2 could carry on past SONG into SET. | **Done** (A5). Tracks and FX's TYPE still stop, as you set. |
| G28 | SET's 24 rows under five headings have no jump. TEMPO to CONTROLS is 17 presses on the stock handheld. | L1/R1 jump to the previous or next heading, going round at the ends. | TEMPO to THEME: 11 become 3. TEMPO to CONTROLS: 17 become 3. | On FX and MOD, L1/R1 change the track (owner, 2026-11-14), and TRACK NAME is on SET. This matches L1/R1 as "a page" in the recordings list. The panic must put SET's row back. | **Done** (A5). |
| G23 | A paste uses up the copy (cell, scene or bar). SONG's copy is a tap of R1, not the "hold R1 drag" the standard names. | Keep the copy after a paste; its brass mark shows it is still there. R1 on the copied cell or scene clears it. Optional: bring back the drag (hold R1 on a target and move; each cell crossed gets the copy, as one undo step). | A part across 8 scenes: about 21 presses today, about 15 with the copy kept, about 4 with the drag. | "A copy clears on paste" and the tap copy came from your own feature list (commit e7b847d, 2026-11-21). A forgotten copy stays ready to paste. | **Reversed by the owner, 2026-11-25.** Done in C3 (the copy stayed), then reversed the same day: a paste spends the copy again ("That gives you the chance to copy something else nearby, and you don't have to go all the way back to the thing you originally copied"). Kept: R1 on the copied cell or scene clears it. Not done: the optional drag. |
| G24 | The track heads (mixer, rename, instrument, DUPLICATE) are drawn at every scroll position but can only be reached by UP from scene 1. From scene 40 that is about 1.3 s of held UP each way, and your row is lost. | A grip you choose that jumps to the cursor's head. DOWN already brings you back to the same row. | Mixer or rename from a deep scene: about 3 s of held keys become 2 presses. | "L1 does nothing on a track" (owner, 2026-11-15). L1 + UP would leave L1 held on a head, which is half of DUPLICATE, right next to REPEATS' L1 tempo. Your page-jump call said "None for now ... Ask again if it is slow on the handheld". This is asking again. | **Not done.** No batch took it: it needs your grip, and your page-jump call was "none for now". |
| G26 | In a chord swap (Y held on a chord), B breaks the chord into loose notes and drops its chord effects. Everywhere else, B during a held pick means "as it was". | B puts back the chord (shape, inversion, pack) as it was when Y went down. Breaking up moves to Y held + A, which is free today. | Browsing chords on a placed chord becomes safe: 1 press back instead of SEL + B and re-checking its effects. | Habit since 2026-11-10. Where breaking up goes is taste. | **Done** (C3). Y held + A breaks the chord up now. |
| G16, G17 | B on a setting means different things: back to the usual value (FX, sampler, MOD), go back (synth pages), usual value then back (KIT EDIT, VOICE), or nothing (SET). In EDIT, the B meant to leave after a Y trim puts that box back, half undoing the trim, and nothing else leaves. | (a) In EDIT and VOICE, a B straight after Y leaves and keeps the trim. (b) B on SET's value rows gives the usual value. | Leaving EDIT after a trim: 3 presses become 1, and the trim stays whole. A one-press reset of VOLUME or STICK SPEED. | The obvious exit, LEFT from a row's first box, is what you turned down on FX: "LEFT doing two jobs was awkward" (owner, 2026-11-14). (a) is a special case. "B goes back" is in the standard, so synth pages keep it. | **Done** (C3), (a) and (b). SET's song rows and the synth pages keep their B. |
| G33 | X plays a fixed 350 ms on synth DEVICE stops, but plays as long as held elsewhere. It is silent while playing on DEVICE stops and roll chords. A on a drum lane name sounds the drum each time you grab its volume. | X sounds as long as it is held wherever it plays something. While the song plays, a note, preset or chord sounds over it, as roll notes already do. MODs still sound only when stopped. A on a lane name is silent; X plays it. | Hear a preset or chord against the song in 1 press instead of stop, X, start. Setting lane volumes is quiet. | Your call E (2026-11-15) stays: MODs are "heard while stopped ... nothing while playing". A note over the song can clash. The roll's silent chords were on purpose ("the song will play it soon enough"). | **Done** (B5). MODs still run only while stopped (call E). |
| G21 | Setting how many bars: hold A + LEFT/RIGHT on SONG, L1 + LEFT/RIGHT on the grids, hold A + UP/DOWN on BARS. On the drum grid time runs sideways, yet L1 + UP/DOWN changes the bar. | Option A: the count is always something held + LEFT/RIGHT, BARS too. Option B: on the drum grid, L1 + LEFT/RIGHT changes the bar, following its time; the roll stays as it is. | One direction for the count (A), or a bar key that runs the way the drums run (B). | Today's directions came from commit 5e2ae67 (2026-11-22) on your git account. They were chosen two days ago to match on both grids. Option B makes the grids differ. | **Not done.** Options A and B are your choice; nothing was moved. |
| G22 | Saving a sound is Y on KIT (numbered, no name) but R1 + a typed name on sampler and synth presets. On the sampler, R1 saves on PRESET but records one stop up. Sampler presets are silent while stepped; synth presets and kits are heard. | Hear sampler presets as they are stepped (this can be done now). One save button for all three needs a free button: Y is CATCH on every sampler stop and PHYSIC's dice on its MAIN page. | A quick save: about 29 presses become 2, with G01's numbered name. | Moving save to Y would take CATCH and PHYSIC's "Y rolls a new patch" off PRESET. The kit's one-press save would gain a press. | **Part done** (B5): sampler presets are heard as stepped. One save button is not done: it needs a free button. |
| G46 | R2 from SONG opens the last cell opened, not the one under SONG's cursor. START takes the cursor's scene but not its track. | When SONG opens, put its cursor on the cell being worked on. Leaving with R2 takes the cursor's cell only if you moved the cursor this visit, keeping the bar if it is the same cell. | SONG to the right track's FX in 3 presses. | A trip to mute or solo another track (Y or X) would then change the cell being edited. FX's L1/R1 track change (owner, 2026-11-14) is kept. | **Done** (A5). |
| G48 | When typing, Y deletes one letter per press and does not repeat. An old name costs one Y per letter; a WiFi password can be 63. | Y held deletes letters at list speed, in all four typing places. | A 10-letter name clears in about a 1.2 s hold. | "Y does not repeat (B never did)" is written inside your typing rule of 2026-11-15. | **Done** (C3). |
| G29 | Deletes that undo can bring back (an effect, a MOD, EDIT's "all back") take as long to hold as permanent ones (1 s). The track hold is 2 s. The bars differ in colour and thickness. | One shared length: 0.5 s where undo brings it back, 1 s where it can't. The track keeps its quiet start. The bars' look stays as it is. | 0.5 s saved per undoable delete. | "The look is settled", so no restyle of the bars. A shorter hold fires more easily, though undo covers it. The manual's "a second" changes. | **Done** (C3). |
| G20 | Since design B (owner, today), a slot's settings are dials in a row, but you move between them with UP/DOWN and RIGHT does nothing. On DEVICE, LEFT/RIGHT moves along the macros. | A question, not a fix: now that they are drawn across, do you want LEFT/RIGHT along the dials? If not, leave it. | The keys match the drawing, and RIGHT gets a job. | "Without A, LEFT is the list from any setting and RIGHT does nothing ... LEFT doing two jobs was awkward" (owner, 2026-11-14). SENDS are still stacked rows. The hint bar has no room left to show UP/DOWN. | **Not done.** A question, not a fix. Hold Y on BAND's dials (G18) now gives LEFT / RIGHT a job there. |
| G32 | The X-held effect clicks (8 and 4 a second, 220 ms) and the recordings list's fastest speed sit outside the two speeds in `ui.hpp`. FX's effect list uses a bare 200 ms. | Name a third "click" speed in `ui.hpp` and in the standard. Name the recordings list as an exception. FX's list uses list speed. | One speed the hand learns once, and the written rule matches the code. | Those rates were tuned on purpose ("clicks, not a slide"). The two-speed standard gets a third entry. | **Done** (C1); CLAUDE.md's standard names the three paces (C4-docs). |
| G37 | Moving an effect is hold Y + UP/DOWN, which is in no standard. Carrying belongs to hold R1, but R1 is FX's track key. | Add to the standard: "Hold Y + a direction moves a line where R1 is taken." | New screens reuse the same gesture. | One more standard line. It must say "where R1 is taken", because on the drum grid hold Y + a direction is the exact nudge. | **Done** (C4-docs): the standard line, scoped as the corrections asked. The stale docs were put right in B5 (manual) and C4-docs (`screens.md`). |
| G41 | USB keyboard PLAY + RECORD writes notes while the song plays but shows no mark (WRITE has one). Its switch is about 19 presses away on SET. | A small mark while it is on and a keyboard is plugged in. Tying it to SEL + X is still an open question in the LIVE plan (D5). | No surprise notes. | There is no room: with WRITE on, the top bar's words get 47 px, and a track name needs 77 while playing. The mark needs another place. | **Not done.** There is still no room for the mark; tying it to SEL + X is the LIVE plan's open D5. |

## 3. The EQ band's move

**BAND is built and cheap: 0.05-0.07% of an audio block, about half the old three-band EQ's cost.** It shipped today in 0.883 as a FILTER type:
- **FREQ**: 20 Hz to 20 kHz
- **GAIN**: -12 to +12 dB
- **Q** (how narrow the band is): 0.3 to 30
- **SHAPE**: BELL, NOTCH, LOW SHELF, HIGH SHELF

That cost was timed on the desk. It has not been heard on the handheld.

**The audit found two things: it moves one dial at a time, and the D-pad can't land on mains hum (50 or 60 Hz).**

**How it moves today.** FREQ, GAIN and Q are dials 1-3. You move between them with UP/DOWN and turn one with hold A + the stick's up/down (or the D-pad). With A held, the stick's left/right does nothing. To move a bell in both pitch and level, you let go of A, press UP or DOWN, and hold A again, every time you switch.

**Fix now (G19): one D-pad push on FREQ is half an octave, not the third the code comment says.**

| FREQ step | D-pad points near hum | Reaches the 1 kHz start |
|---|---|---|
| Today: 1/20 of the range (half an octave) | 39.9, 56.4, 79.6 Hz | No (only B does) |
| Proposed: 1/30 (a third of an octave) | 39.9, 50.2, 63.2, 79.6 Hz | Yes (1.0 kHz) |

60 Hz falls between 50 and 63. For that, use the stick, or a semitone step instead of thirds.

**Moving it two ways at once (G18, your call):**

| Option | How it works | The catch |
|---|---|---|
| Hold A + stick sideways = FREQ | Up/down turns the dial under the cursor as now; sideways always moves FREQ. | **Not recommended.** On a FILTER dial the D-pad's A + LEFT/RIGHT changes TYPE (owner, 2026-11-14). The stick and D-pad would then do different jobs on the same direction. The mixer, a drum hit and a roll note all give them the same job. |
| Hold Y on a BAND dial | Sideways = FREQ; up/down = GAIN, or Q on the Q dial. Stick and D-pad do the same. | Y does nothing on an FX dial today, so nothing clashes. The hint bar is full (it already leaves out HOLD X), so this would live in the manual unless another hint makes room. |

With either option, WRITE would record two lanes at once.

**Gain:** sweeping a notch by ear and narrowing it takes about 6 presses and 2 stick moves today; this makes it one grip. Notching out one frequency took 19 presses, 6 of them spent moving between FREQ and Q.

**My read: do G19 now. Take the hold-Y grip if moving a band two ways at once matters to you.**

**Done:**
- **G19** (B4): FREQ steps by thirds of an octave. 50 Hz and the 1.0 kHz start are each on a push.
- **G18** (B5): the hold-Y grip. The hold-A + stick option was not used.
- Neither has been heard on the handheld.

## 4. Checked and fine

These looked odd but are deliberate:
- **The roll's hold A: LEFT/RIGHT is level, UP/DOWN is length.** It follows the drawing, since a note grows down the screen. Both grids put time on their own time direction, and the manual documents it.
- **The roll copies a bar on R1's let go; the drums copy on the press.** On the roll, R1 on a note is also a carry, so the copy has to wait.
- **L1 tapped on the roll's BARS stop zooms; on the drums' BARS stop it does nothing.** The slices need a lane under the cursor; the zoom doesn't.
- **Tracks stop at the ends** (owner, 2026-11-14: "nothing in the program wraps tracks"). **FX TYPE stops at its family's ends** (owner, 2026-11-14), so the hints can show which way is left.
- **Y alone mutes on SONG** (owner, 2026-11-15: "Y for mute is just a one tap"). G03 keeps it.
- **The mixer's B with A held is a reset, not a put-back** (owner, 2026-11-14).
- **The update notice ignores every button but A and B.** The manual says "Nothing else responds until it is answered". Accepting needs a hold because accepting an unknown program is the risky answer.
- **The X-held click rates and the recordings list's fastest speed** are named and on purpose ("clicks, not a slide"; batch 3, 2026-11-15).
- **The kit's sound list was slow by stick on purpose** ("slow enough to hear each sound"). Since the owner's stick rule (2026-11-25) it runs at twice the held D-pad's list pace, 13.6 a second at 75%.
- **Keyboard PLAY + RECORD is kept across restarts,** like the metronome (your call in the easywins plan).
- **SET's A acts on the press,** which matches the house rule where A has no hold job (SONG's AT END does the same). Only BOUNCE and TRACK NAME need the let go (G15).
- **Hold Y moves an effect on FX.** It is shown in the hint bar ("HOLD Y move") and in the manual; only two docs were out of date (put right in B5 and C4-docs).
- **The track delete's quiet first half second** means the B tap that empties a cell never flashes red.

## 5. Dropped

These are findings, or parts of findings, that the code check did not confirm:
- **G47, "emptying a drum lane is a 16-step B drag":** wrong. Hold Y + DOWN on the lane name (the lane dice) empties a lane in about 0.7 s. B on the name already starts the hold-B drag delete, so emptying on the press would clash with it.
- **G36, the roll's brush taking a note's level:** the roll's brush can never start on a note, because R1 on a note carries it.
- **G36, the roll's hold-A directions:** deliberate (section 4).
- **G07, one ~1 s turn for every value:** it would slow the fastest turns (hit LEVEL from 0.44 s to 1 s).
- **G15, "drops WiFi and throws away an update":** this can't happen, because the App takes every button while an update waits.
- **G39, START under the update notice:** a deliberate modal (section 4).
- **G10, "no test tries the effect undo":** a test does.
- **G38, "tempo presses made blind":** the top bar, with the tempo, stays above the help card.
- **G03, "the SONG cursor is lost after NEXT SCENE":** the cursor moves onto the copy.
- **G45, "rounded to 8 bars":** FIT tops out at 4 bars.
- **G31, "one copy timing on both grids" and "no zoom on BARS":** both deliberate (section 4).
- **G18, stick sideways = FREQ:** collides with TYPE; it was moved to section 3 as the option not recommended.

## 6. Open questions after the fixes (2026-11-25)

The batches left these for the owner. Each is in its docs/dev file too. Nothing below was felt on the handheld.

**Speed and feel**
- **Click pace** (G32): the drum ratchet, lane FEEL and the roll's ratchet and glide now click twice as fast (8 a second after 170 ms; they were 4 a second or 220 ms). Raise `ui::kClickEveryMs` if they overshoot; above 125 ms the drum pitch gets slower than it was.
- **Lists at list pace** (G08): the song list, presets, folders, CLOUD's RECORDING, FX's TIME, the filter TYPE and BAND SHAPE dials and BARS kept the cursor's 33 a second ("faster is better"). The standard says 9 a second. Four lists wait 170 ms, not the standard's 200. **More urgent since your stick rule (2026-11-25)**: until this is answered the stick runs the lists among these (the song list, presets, folders, CLOUD's RECORDING) at twice the cursor's pace, 50 a second at 75%. The choices among them do not: FX's dials and a synth's choices run at twice the list's (13.6), and BARS clicks once a push (the [owner-fix] review, CLAUDE.md "choices click once per push").
- **Choices once a push, or a run?** (the [owner-fix] review): BARS on both grids, VOICE's MODEL, LOOP and REVERSE click once a push of the stick however long it is held; FX's choice dials and a synth's choices with A held click at once, then run at 13.6 a second. Should those run too, or click once a push like the others?
- **The stick's full push** (G07): answered by the owner, 2026-11-25. There is no full push any more (no 0.9, no climb): any push past the dead zone runs at once at twice the held D-pad's pace, times STICK SPEED, the name keys too (13.6 a second at 75%).
- **Lists that go round on every move** (G35: the FX list, KIT rows, synth MAIN, presets) still go round on a held run. Stop them at the ends on a run, as SET and the song list now do?

**Gestures**
- **"How many bars"** (G21): A, always something held + LEFT / RIGHT (BARS too); or B, on the drum grid L1 + LEFT / RIGHT changes the bar.
- **SONG** (G46): Y or X on another track's cell counts as a move, so R2 opens that track. Leave the cell being worked on alone?
- **KIT lanes** (G35): down from lane 8 goes to lane 1, up from lane 1 goes to the KIT stop. Make it go both ways, as the drum grid now does?
- **BAND's grip** (G18): on a NOTCH, up / down moves GAIN, which does nothing there. Make it Q? The bar has no room for "HOLD Y": only the manual says it.
- **One save button on every preset stop** (G22): no button is free on all three.
- **Hold Y moves a line** (G37) only on FX, where R1 is taken. Is that the standard?

**Words and marks**
- SET's hint says "L1 / R1 page" ("heading" ran 15 px over).
- The chord picker reads "A notes", "B as it was", "UP / DOWN invert" (G26).
- "Hold B deletes for good" now reads "Hold B deletes": the half-second holds can be undone.
- The words test now checks the manual and README for erase, remove and rub out, comments and all.

**Undo and values**
- A held pick put back with B (a chord swap, the KIT stop) leaves an undo step that changes nothing. Drop such steps?
- B on SET's SONG rows (TEMPO, KEY, SCALE, CHORD PACK, TRACK NAME) does nothing. Give them a usual value too (G17)?

**BAND and PITCH** (fx-quality, merged)
- A/B a high bell before calling it done? Keep the plain numbers at exactly 0 dB?
- PITCH: keep the new head-strip picture? Sums of 8 or 4 in the splice search (a bell tone -12.3 dB against -17)? The GRAIN wording? The extra 25 ms of delay?

**Not done, still yours** (section 2): G24, G20, G41, G21's direction, G22's save button.

## Appendix: file:line references

- **G01** app/synth_screen.cpp:120-123, 432-435, 474-479, 566; app/sampler_screen.cpp:224-227, 271-274, 334-339, 418; app/settings_screen.cpp:19, 114-120, 271, 366-371; app/sample_picker.cpp:339-342, 492-497; app/app.hpp:115-118; app/app.cpp:153-158, 180-234, 253-270, 348; app/session_synth.cpp:231; docs/dev/drums-and-samples.md:396-401; docs/manual.md:690-692
- **G02** app/song_screen.cpp:461-465, 596-631, 662-694, 745-757; app/song_screen.hpp:72-73, 153; app/fx_screen.cpp:540, 649; docs/dev/songs-and-undo.md:45; docs/dev/song-screen.md:93-94
- **G03** app/app.cpp:204, 208-209, 238, 253-272, 546-547; app/song_screen.cpp:88-95, 326-330, 519-520; app/kit_screen.cpp:207, 251-255; app/sampler_screen.cpp:218-222; app/synth_screen.cpp:394-399; app/mod_screen.cpp:414; app/session.cpp:1437-1457; tests/nextscene_test.cpp:406-417; docs/manual.md:734-735; docs/plans/big-features-plan.md:321
- **G04** app/kit_screen.cpp:247-255; app/sampler_screen.cpp:212-222; app/synth_screen.cpp:394-397; app/session_samples.cpp:173-215, 351-366, 705-709, 724-750; app/session.cpp:1132-1153, 1262-1273, 1293-1297; docs/dev/songs-and-undo.md:30-33, 45
- **G05** scripts/Apotheca.sh:71; scripts/build-handheld.sh:104-127; app/main.cpp:397-405; app/controls.cpp:13-19, 83, 259; app/controls.hpp:56; app/input.cpp:93-97, 161-166; app/app.cpp:131-149, 532-549; app/ui.hpp:233-234; os/board/rg40xxv/rootfs-overlay/usr/share/apotheca/controls-rg40xxv.txt; docs/manual.md:49-50, 1810
- **G06** app/main.cpp:330, 574-626, 663-667, 746; app/input.hpp:26-29; app/tap_tempo.hpp:24-34; app/settings_screen.cpp:270; app/pattern_screen.cpp:802; app/redraw.hpp:93; docs/dev/screens.md:190-195; docs/dev/measured.md:82
- **G07** app/ui.hpp:121-125, 193; app/settings_screen.cpp:20, 204; app/song_screen.cpp:22, 24, 745-757; app/pattern_screen.hpp:138-144; app/kit_screen.cpp:23; app/synth_screen.cpp:24; app/sample_picker.hpp:109; docs/manual.md:218, 471, 1590-1591
- **G08** app/ui.hpp:189-194; app/song_screen.cpp:21, 380-382, 759-777; app/synth_screen.cpp:21-22, 286-299, 312-337, 529-535; app/sampler_screen.cpp:15, 364-374, 388-392; app/fx_screen.cpp:500, 702-706; app/note_screen.cpp:607, 924-930; app/pattern_screen.cpp:652-653, 969-983; app/kit_screen.cpp:22, 352-357, 515-518, 777-781; app/sample_picker.cpp:568-581; docs/manual.md:1812-1814
- **G09** app/pattern_screen.cpp:333-340, 534-538, 954-983, 1302; app/session_samples.cpp:766-770; docs/manual.md:1973
- **G10** app/fx_screen.hpp:129; app/fx_screen.cpp:657, 883; app/sampler_screen.cpp:294-308, 485, 499; app/kit_screen.hpp:142; app/song_screen.cpp:1267-1268; app/session.cpp:836; app/mod_screen.hpp:133; app/app.cpp:542; tests/app_test.cpp:9194-9198; docs/manual.md:287, 1455
- **G11** app/pattern_screen.cpp:83-104, 713-735, 857-863; app/note_screen.cpp:780-783; app/mod_knob.cpp:239-243; docs/dev/screens.md:260-271; docs/plans/big-features-plan.md:185, 201
- **G12** app/kit_screen.cpp:181-189, 247; app/fx_screen.cpp:577-579; app/mod_screen.cpp:402-403; app/synth_screen.cpp:146, 375-381, 417; app/sampler_screen.cpp:212-217; app/session.hpp:512-515; docs/dev/files.md:195-202; docs/dev/screens.md:59-60
- **G13** app/synth_screen.cpp:36-46, 383-386, 418, 427-431, 436-439; app/synth_screen.hpp:143; docs/dev/screens.md:20-21
- **G14** app/note_screen.cpp:33, 226-242, 850-851, 899-905, 1461-1464; app/pattern_screen.cpp:928-929, 950-964; app/fx_screen.cpp:534, 677-678; app/synth_screen.cpp:344, 503-505; app/song_screen.cpp:610-617, 737-757; docs/dev/notes-and-mods.md:160-161
- **G15** app/settings_screen.cpp:183-213, 245-250, 263-285; app/session.cpp:2029; app/bounce.cpp:444, 460, 533-595; app/app.cpp:107-111
- **G16/G17** app/kit_screen.cpp:399-405, 419-454, 684-696; app/synth_screen.cpp:418-425; app/fx_screen.cpp:453-458, 591-598; app/settings_screen.cpp:263-292; docs/dev/screens.md:23-31; docs/dev/drums-and-samples.md:291
- **G18/G19** engine/src/fx/band.cpp:1-12, 40, 213; app/fx_screen.cpp:20, 438, 486, 493-509, 683-689, 1074-1079; app/fx_picture.cpp:191-206; docs/dev/measured.md:54; docs/dev/screens.md:23-27; docs/manual.md:292; commit bd2828e
- **G20** app/fx_screen.hpp:161-167; app/fx_screen.cpp:452-457, 1073-1079; docs/dev/screens.md:23-31, 143-147
- **G21** app/song_screen.cpp:598-601; app/pattern_screen.cpp:545, 563-567, 652-653, 752-762, 847, 982; app/note_screen.cpp:299-302, 607, 718-733; app/pattern_screen.hpp:117-122; docs/dev/drums-and-samples.md:94-95; commit 5e2ae67
- **G22** app/kit_screen.cpp:207-211; app/sampler_screen.cpp:151-158, 218-234; app/synth_screen.cpp:399-402, 432-435; docs/dev/instruments.md:26-27
- **G23** app/song_screen.cpp:271-273, 524-566; app/session.cpp:648, 749; docs/dev/song-screen.md:31-32; CLAUDE.md:43; docs/manual.md:1449; commit e7b847d
- **G24** app/song_screen.cpp:124, 151-155, 435-477, 610-616; docs/dev/song-screen.md:39-42; docs/plans/scenes-plan.md:109
- **G25** app/app.cpp:62-94; app/session.cpp:72-81, 101-107; app/kit_screen.cpp:256-261; app/sampler_screen.cpp:230-239; app/synth_screen.cpp:427-431; app/session_samples.cpp:505-540; docs/manual.md:433-434
- **G26** app/note_screen.cpp:440-480, 495-516, 664-683, 1441; docs/manual.md:2287-2288
- **G27** app/note_screen.cpp:376, 407-416, 664-685, 1264; app/app.cpp:255-258, 266; docs/manual.md:2289
- **G28** app/settings_screen.hpp:44-55; app/settings_screen.cpp:19-20, 134-139, 169-181, 263-292; docs/dev/screens.md:366
- **G29** app/song_screen.hpp:72-73; app/song_screen.cpp:17, 842, 968, 1161-1162; app/fx_screen.hpp:128; app/kit_screen.cpp:893-894; app/sampler_screen.cpp:485; docs/dev/screens.md:115-118
- **G30** app/synth_screen.cpp:417-426; app/kits.cpp:164; app/sampler_presets.cpp:156; app/transfer.cpp:909; app/session_synth.cpp:235-238
- **G31** app/pattern_screen.cpp:741-745, 790-792, 950-965; app/note_screen.cpp:642-645, 708-711, 740-761, 908-922; docs/manual.md:1545
- **G32** app/fx_screen.cpp:19, 703-706; app/mod_screen.cpp:17, 467-470; app/pattern_screen.cpp:27, 970-980; app/note_screen.cpp:28, 852-856, 925-928; app/sample_picker.hpp:107-108; docs/dev/drums-and-samples.md:419-424
- **G33** app/synth_screen.cpp:137-139, 368-370; app/note_screen.cpp:418-421, 784-787; app/pattern_screen.cpp:764-778, 799; app/fx_screen.cpp:409-424, 550; docs/dev/notes-and-mods.md:146, 168-171
- **G34** app/kit_screen.cpp:324; app/sampler_screen.cpp:223-229, 343-347; app/synth_screen.cpp:436-439, 491-495; app/recorder.hpp:29; app/session_samples.cpp:506-508
- **G35** app/app.cpp:266-270; app/pattern_screen.cpp:530, 541-558; app/settings_screen.cpp:169-196; app/kit_screen.cpp:156-162; app/synth_screen.cpp:141-143; docs/dev/screens.md:20, 23-25, 104-107
- **G36** app/note_screen.cpp:621-627, 735-753; app/pattern_screen.cpp:654-662, 828-838; docs/manual.md:2175-2177
- **G37** app/fx_screen.cpp:434-444, 555-558, 1060-1070; docs/dev/screens.md:13-15; docs/manual.md:288, 318
- **G38** app/app.cpp:19, 296-298, 344-364, 458-483, 524-528
- **G39** app/song_screen.cpp:305-393, 632-637; app/sampler_screen.cpp:188-190; app/app.cpp:105-111, 537; docs/manual.md:1697-1698
- **G40** app/app.cpp:105-111, 505-506; app/session_samples.cpp:493-497; docs/dev/wifi-and-bounce.md:6-7
- **G41** app/settings_screen.cpp:208, 630; app/session_songs.cpp:313; app/session_synth.cpp:941-974; app/ui.cpp:149-155; tests/modtab_test.cpp:165; docs/dev/notes-and-mods.md:194-197; docs/plans/big-features-plan.md:362
- **G42** app/sample_picker.cpp:400-421, 517-519, 687-688; docs/manual.md:223, 1806
- **G43** app/session_songs.cpp:116-121, 310-380; app/song_file.cpp:1469-1494; app/session.cpp:1020; app/main.cpp:749
- **G44** app/fx_screen.cpp:631-642, 667-671; app/mod_screen.cpp:263-269; docs/dev/screens.md:103-108
- **G45** app/kit_screen.hpp:79-80; app/kit_screen.cpp:89-96, 217-233; app/session_samples.cpp:31, 746-747; docs/manual.md:615-616
- **G46** app/app.cpp:65-79, 253-270; app/song_screen.cpp:473, 502-505, 634-635
- **G47** app/pattern_screen.cpp:25, 451-477, 628-631, 858-863; docs/manual.md:1775-1780
- **G48** app/settings_screen.cpp:42-43, 112-113, 271, 365-371; app/sample_picker.cpp:339; app/synth_screen.cpp:118-119; docs/dev/drums-and-samples.md:396-403
- **Docs row** docs/dev/song-screen.md:31-32; CLAUDE.md:43; docs/manual.md:218, 287, 318, 615-616, 1449, 1590-1591; docs/dev/screens.md:15; docs/dev/drums-and-samples.md:94-95; engine/src/fx/band.cpp:4-5
