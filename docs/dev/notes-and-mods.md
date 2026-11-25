# Notes, chords, glides and MODs

Note and chord effects, glides and GLIDE FROM, MODs in the song and on the knobs, the MOD tab.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **An `Engine` is 1.4 MB: a test does not stand many on the stack.** `engine_test`'s
  `testNudgeMovesAHitOffTheGrid` had six in one frame, 8.37 MB of the 8 MB there is, with 20 KB
  to spare by luck; an Engine a kilobyte bigger (FEEL) and it crashed one run in three, before
  `main` printed anything. Five of them are on the heap now. `testLaneLevel` has five (6.98 MB).
- **Note effects are two bytes on a note** (`NoteEvent::ratchet`, `glide`; a
  chord has `ratchet` and its own five, next rule). They reach the engine in `Command::e` (low byte
  the ratchet, +256 a glide, and `glide << 11` for a length other than half a
  step, above THROW's two bits). In a song file: `notefx STEP KEY RATCHET
  GLIDE` and `chordfx STEP RATCHET`, each after its note.
- **Chord effects are a `ChordPlay` on a chord** (owner, 2026-11-12;
  `ChordEvent::play`, `engine/include/apotheca/chord_rule.hpp`): STRUM (0 off,
  1 STRUM, 2 WIDE STRUM, 3 / 4 / 5 an arpeggio at a quarter, half and one
  step), ORDER (0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM), and HUMAN TIME, HUMAN
  LEVEL and LENGTHS as percentages. The numbers are in songs: appended. The
  top figures are named constants there (`kHumanTimeMs` 12, `kHumanLevelMost`
  0.15, `kLengthsMost` 0.5, `kStrumMs` 9, `kWideStrumMs` 26).
  - **The engine does not know chords; each note says which key of its chord
    it is.** `NotePattern::render` fills `play.index` and `play.count`; they
    go in `Command::play` (SetNote) and `TrackImage::Note::play`. Anything
    new that writes chord notes out goes through `render`.
  - **A chord with nothing set takes the path it always did**
    (`play.atRest()` in `fireStep`): that is what keeps old songs to the bit.
    ORDER alone is "nothing set".
  - **Every roll is worked out from where and which time**
    (`chordSeed(track, bar of the cell, step, steps since Play)`, then `chordUnit` with
    what it is for and which note; not the scene's number, see the scene-move rules). Never a generator walked: that is what
    makes it the same on any helper count and block length, and Play from
    the top the same performance again. The only counted thing is
    `Track::strokes` (chords begun since Play, for UP-DOWN strums), counted in
    song order on the audio thread.
  - **Everything a chord effect times is a whole number of samples**
    (`kWholeFrames`: the delay, a ratchet's pieces, the note's end). A
    waiting note fires when under one sample is left, so a part of a sample
    plays a sample sooner or later by how the sound is cut into blocks. A
    nudged plain note and a plain ratchet still do (not changed: old songs at
    the handheld's block length). An arpeggio's clock fires as a step does,
    on the first sample at or after its time. **The `ceil` in `startNote`'s ratchet
    loop is reached by no note** (a chord's and an arpeggio's keys come with
    ratchet 0; `startChordNote` counts the pieces and ceils them itself): an
    abort there passed all 46 tests (2026-11-15). Kept as a guard.
  - **A note that may be early is asked for a step ahead**, as a nudged-early
    note is. Which asking takes it is `nudge < 0 || play.time > 0`: nothing
    that depends on the tempo, or a tempo change between the two askings
    plays the note twice or not at all. The song's very first step has no
    step before it: early notes land on the top (`top`).
  - **A spread chord is still one moment to a glide.** Its notes say nothing
    to the `GlideTrail` when they sound (`kUntold`); a waiting note that only
    tells (`kTellOnly`) names each key at the chord's own moment. So what
    follows slides from its keys as written (which one: the track's GLIDE
    FROM; TOP is the top key), as `Project::glideLines` draws, however it is
    strummed. `chordfx_test`, `glide_test` and `glideapp_test` hold it.
  - **An arpeggio is one clock for the chord** (`Track::Arp`, three a track:
    one still playing, the next chord's waiting for its nudge, the one after
    asked a step ahead; the one that begins ends the one playing; with two,
    the third took the clock of the one still playing), not a waiting note
    per note: 512 would not fit. `clearPending` and a track taken away empty
    them; `SetTempo` keeps its place in the step. A ratchet is not played in
    an arpeggio. **An arpeggio ends with its chord**: a `SetNote` into its
    bar sets `Arp::look`, and before its next note it looks whether an
    arpeggio's note is still written on its step (by step, not by slot: a
    chord's keys change slots when a loose note is written).
  - **Which asking takes a chord is remembered, not read twice**
    (`Track::chordAsked`: the step, counted from Play, whose chord was asked
    a step ahead). A chord asked ahead is not begun again on its step; one
    not asked begins on its step, early keys with the step. Read from the
    values at each asking, a HUMAN TIME turned while the song played lost
    the chord once or played it twice.
  - **A ratcheted chord's hits are counted from the CHORD's length**
    (`startChordNote` has the ratchet's loop itself), not from the key's own
    (longer when early, shorter when late or under LENGTHS): keys of one
    chord played different numbers of hits. LENGTHS shortens the last hit
    only. No key is so late that its last hit has no chord left.
  - **A bounce plays the song once** (`Command::Play` with `c = 1`,
    `Engine::once_`): on the song's last step nothing early is asked for
    from its first (chords, notes and drum hits alike), or it sounds in the
    file's last moments and rings into the tail. Live, going round, it is.
  - **THROW holds for the whole chord** (`Note::hold` is set before LENGTHS
    shortens the note; an arpeggio's every note carries what is left).
  - In a song file: `chordplay STEP STRUM ORDER TIME LEVEL LENGTHS`, a line
    of its own after the chord (and its `chordfx`), **left out when all five
    are 0**. A new word and not more numbers on `chordfx`: an old program
    skips a word it does not know, and `chordfx` is written as it always was.
  - **On the roll, X held on a chord shares the directions out the other
    way**: up / down pick one of six (`NoteScreen::fxPick_`, kept from one
    hold to the next), left / right set it. A plain note's are as they were.
    The list, a click, the words and the dice's odds are
    `app/chord_effects.hpp`; the strip's places are in `note_marks.hpp`
    (`kChordFx...`). A chord broken into notes keeps none of the five.
  - **A chord's RATCHET lines and its light corner are drawn once** (owner,
    2026-11-13): on its lowest key that shows, the one under its name; on
    the next key clear of the name where the name sits on that one (step 1,
    nudged early), and the name is drawn again on top. THROW's foot is on
    every key. `chordfxapp_test` reads it off the drawn screen.
  - X hears a chord plain. The effects are the step clock's.
  - **X held + B empties what X shows** (gesture audit G11, 2026-11-24; `NoteScreen::fxEmpty`): a
    note's RATCHET, GLIDE and THROW; a chord's six (STRUM OFF, ORDER UP, the three amounts 0,
    RATCHET OFF) and its THROW. The note or chord stays, its level, length and nudge as they were;
    GLIDE FROM is the track's and stays. **A step of undo of its own** (`beginOwnUndoStep` /
    `endOwnUndoStep`, as a paste): a turn just before it is a step by itself. Nothing to empty
    (ORDER alone counts, the strip shows it): nothing happens, no step. Plain B deletes once X is let
    go, as ever. **No chip**: the held strips put each chip under the field it sets (owner,
    2026-11-12, `screens.md`) and an "empty all" has no field; the help card's B line says it
    ("X held: empty the effects") and the manual. On a knob, X + hold B still DELETES the MOD (a
    hold: it deletes, it does not empty). `easy_test xempty`.
  - **Y held, L2 / R2: the chord in hand half or twice as long** (gesture audit G27, 2026-11-24;
    `NoteScreen::doubleHand`). Along 1, 2, 4, 8, 16 steps; a length between two (the stick sets any)
    goes to the next rung that way; the ends stop. A new chord: `handLength_`, which inHand holds to
    the room where it lands as ever. A chord being swapped: its own length, held to the cell's end
    and the next chord as `setLength` holds it, the cursor kept inside it, and `handLength_` takes it
    too (as hold A on a chord sets it). The App's box says what lands (`CHORD 8 STEPS`, `CHORD 16
    STEPS, 3 FIT HERE`); nothing is said where nothing changes. **The App sends L2 / R2 to the roll
    while Y is held on a synth track** (as it does while A is held); on the drum grid Y held is the
    dice and they stay swallowed. The hint is `L2 R2 length` (fits at XL, the swap's longer B
    included). **A panic begun with L2 or R2 while Y is held changes the length and does not put it
    back**, as the A-held nudge does not. `easy_test chordlength`.
  - Its tests are their own two programs: `tests/chordfx_test.cpp` (the
    engine, with a listening synth) and `tests/chordfxapp_test.cpp` (the
    gesture, the strip off the drawn screen, the dice, the song file, undo,
    swap, break, drag, scenes). `chordfx_test testStrum testArpeggio` runs
    just those.
- **The stick carries as the D-pad does** (gesture audit G14, 2026-11-24). With R1 holding a note or
  a chord, the stick's sideways steps go through `carry()`: a SCALE's next key for a note (a chord a
  semitone), and with L1 held an octave **once a push** (at `kCarryKeysPerSecond`, ten a second, an
  octave a click would fly off the keyboard). **One job a push**, as with A held (`axis_`, set afresh
  when R1 goes down): pushed up or down first it slides in time (`carryFree`) and moves no key until
  it comes back. It was `carryBy(+-1, 0)` straight, chromatic, with no lock, though the hints said
  "LEFT / RIGHT another key, L1 octave" and `docs/plans/easywins-plan.md` asked for the scale "the
  stick too". `easy_test stick`.
- **X let go with the stick still pushed: the cursor waits on the grids too** (gesture audit G14,
  2026-11-24). It extends the owner's call for FX and DEVICE (`stickWait_`, under "X HELD on a knob" below) to the roll
  and the drum grid (a hit and a lane's name), where the cursor jumped off at once. The stick's state
  is taken **at X's let go** (the event's `Input`), not from frames: a push that came after the let
  go is the cursor's. A hold lost with X still down (an undo took the note or hit) waits the same
  (`fxLastFrame_`). While it waits the stick does nothing at all; the D-pad is not affected.
- **A stick move with L1 held is a use of L1** (gesture audit G31, 2026-11-24): its let go then
  zooms nothing on the roll (`zoomUsed_`) and opens no slices on the drum grid (`l1Used_`, with L1
  held first), as any button pressed meanwhile already did. `copybar_test testAStickMoveIsAUseOfL1`.
  The two grids' COPY timings stay as they are (the drums on R1's press, the roll on its unmoved let
  go): on the roll R1 on a note is also the carry, so only an unmoved let go can mean copy; the
  drums have no carry, and the manual says both. The roll's L1 tap still zooms from BARS (the zoom
  is a view of the whole roll; the drums skip L1 there because slices need a lane).
- **A glide is a length, and its number is not its place** (owner,
  2026-11-12; `engine/include/apotheca/glide_rule.hpp`). The numbers are in
  songs, appended: 0 off, **1 half a step (what ON was, so old songs sound as
  they did)**, 2 a quarter, 3 one step, 4 two, 5 FULL. The order a player
  steps through is `kGlideOrder` (`glideAt` / `glidePlace`). A number nobody
  knows reads as half a step (`glideKind`).
- **Which note a glide comes from, and for how long, is one rule in that
  header, and the engine and the roll both ask it** (`GlideTrail`,
  `glideSteps`, `glideLines`). Do not work either out anywhere else.
  The rule: the last note the track began (Play clears it); a ratchet's later
  pieces are not notes beginning; notes that begin within 1/256 of a step are
  one moment (none slides from another, what follows slides from one of its
  keys: GLIDE FROM, next rule, says which);
  FULL is the time since that moment began; on a ratchet never more than 85% of
  the first piece. The engine counts time in steps since Play (`stepsFired_`
  plus the nudge), not in samples, so both sides get the same numbers.
  `glide_test` holds the rule to the engine with a listening synth;
  `glideapp_test` holds `Project::glideLines` (what the roll draws) to the
  engine through the Session, chords included.
- **A track's two MODs are in the song, and one rule says what a MOD may be
  aimed at** (batch 2, 2026-11-14; `TrackData::mods`, `app/mods.hpp`,
  `Session::setMod`, `tests/modapp_test.cpp`). `modTargetFits`: a send; a
  slot's setting that is there and is an AMOUNT; a macro of a synth that is
  not a choice (`macroIsChoice`: a new synth's threshold macros go there).
  **A MOD that is OFF, or whose target does not fit, is a blank
  `ModSettings()`**: nothing is kept of it, in memory or in the file.
  Whatever changes what a lane belongs to must put its MOD OFF or carry it:
  `Session::setEffect` puts OFF a MOD aimed at the slot, `changeEffectType`
  carries it by the setting's NAME (`modTargetCarried`; a choice on the new
  type: OFF), `setTrackKind` drops one on a macro. Only `Session::setMod`
  writes `mods` outside a song being read (it tells the engine); `sendSong`
  sends them all. Undo sees them through `soundMark()` and compares them as
  the file writes them (`modText`).
  - In a song file: `mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE`, then
    `modsteps N LENGTH SLEW CHANCE v1..v16` where the row is not as a new
    MOD's; **both left out when OFF**. TARGET is the automation lane's number.
    **DEPTH is 0..1 with 0.5 none** (`depthToFile`; the row's values are
    -1..1). The numbers are `mod_rule.hpp`'s: appended.
  - **A song's MODs are made to fit after the WHOLE song is read**
    (`modsFit` at the end of `parseSong`): a `mod` line may stand before the
    `fx` line it is aimed at.
- **X HELD on a knob is its MOD** (batches 3 and 4, 2026-11-15; the owner's calls A to H;
  `app/mod_knob.hpp`, one `ModKnob` the DEVICE and FX screens share; `tests/modknob_test.cpp`). A
  macro on DEVICE MAIN, a TRACK's slot setting or a send on FX, where `modTargetFits` (a choice,
  a bus's or the master's setting: X only hears). While X is held: UP / DOWN DEPTH by 1% (**the
  first UP on a knob with none makes LFO, SINE, 1 BAR, +25% in the first free MOD**; DOWN makes
  nothing), the stick DEPTH smoothly / RATE a click, LEFT / RIGHT RATE, A the SHAPE (after DRIFT a
  STEPPER, its row rolled if it was never written; then SINE), L1 / R1 PHASE or LENGTH (the ends
  stop), Y the dice (`ModKnob::rolled`: a STEPPER's row; an LFO's SHAPE and RATE, never both as
  they were, DEPTH kept), hold B deletes it (a second; undo brings it back). Both MODs on other
  knobs: MODS FULL, nothing is made.
  - **The X-held check comes before every other button's own job** (`modKnob_.onButton` right
    after X in `onButton`): L1 / R1 not FX's track, B not the default, the
    D-pad and the stick not the cursor (`update` returns after `modKnob_.update`). A pressed while
    held is the MOD's; let go after X it does nothing. **X is not taken with A held** (turning).
    X let go with the stick pushed: the cursor waits for it to come back (`stickWait_`, both
    screens, and the roll and the drum grid since 2026-11-24, gesture audit G14; the knob's own `stickWait_` the same when X goes down with the stick pushed).
  - **Where the fields go** (call B): DEVICE in SIGNAL's strip, FX in the heading's place, on
    SENDS in the OTHER send's row (`FxScreen::modPanelY`: the held send stays in full view), only
    while X is held. **They are a tight row** (`fonts.fixed()`, the same at every TEXT SIZE:
    `testTheSameAtEverySize`), each chip under its field by ONE constant (`kDeviceModPlaces`,
    `kFxModPlaces`), the dice at the left. NOW is what the engine says the MOD adds
    (`Engine::modShown`), purple, from the middle.
  - **Heard while stopped** (call E): the screen asks `Session::hearMods` every frame X is held,
    which sends `ModAudition` only when what runs changes, nothing while playing (and runs it again
    after Stop); `keepModsHeard` (App, beside `keepHearing`) lets go when X is up, another screen
    or track; `dropHearing` (panic, a new song) forgets it. The synth's note is held as long as X
    is (`auditionHeld`), on DEVICE and on FX: a tap is still a tap. **Since gesture audit G33
    (2026-11-25) the note is held while playing too, over the song** (it was held only while stopped):
    the MOD is still run only while stopped, as call E says; playing, the song's MODs run on the note.
    Why, and the rest of X's one rule (the roll's chord included): `drums-and-samples.md`, "X sounds
    for as long as it is held".
  - **"HOLD X: mod" is in a bar only where it fits at `gHintsOver` 0** (call G): DEVICE MAIN on a
    macro that takes one (FM-6's, every one); FX on SENDS, not on a setting (22
    pixels over). The help card's X line says it everywhere.
  - `Session::setMod` sends only the fields and steps that changed while a MOD stays on (a turn is
    one command, not 26); from or to OFF, the whole MOD as before.
  - The fields move by themselves only while X is held, which the redraw already counts (a button
    held); `idle_test` has a part on each screen (made, heard while stopped, let go).
- **Both MODs may aim at one knob; the list of targets marks the other MOD's** (owner,
  2026-11-15): its word purple and underlined (`ModScreen::otherTarget`; an OFF MOD is blank, so
  it marks nothing). X held on that knob still reaches MOD 1 only. `modtab_test`
  `testTheOtherModsTargetIsMarked`.
- **The MOD tab is the sixth tab, after FX** (batch 5, 2026-11-15; owner: "keep going", the
  plan's calls at their recommended answers; `app/mod_screen.hpp`, `View::Mods`,
  `tests/modtab_test.cpp`). Laid out as FX: four lines (MOD 1, its STEPS, MOD 2, its STEPS),
  the line picked large at the right. A MOD's rows: TARGET, SHAPE (SINE ... DRIFT then STEPPER,
  `ModKnob::shapeAt`: the knob's one list), DEPTH, RATE, PHASE / LENGTH; its STEPS: the 16, then
  LENGTH, SLEW, CHANCE. **Hold A on TARGET (or the MOD's line) is the list of targets, and it
  only TRIES**: nothing is set or heard while A is held, let go aims it (an OFF MOD made as the
  knob's first, call C), B with A held, another track or another screen calls it off. Only
  `pickLineTargets` (through `modTargetFits`) says what is offered. Everything goes through
  `Session::setMod`, so undo needs nothing here.
  - **The top bar's tabs read SONG, PTRN, DVICE, FX, MOD, SET, 5 pixels either side of each word**
    (owner, 2026-11-18; `ui::kTabPad`; it was PATTERN and DEVICE at 3). The screen's words have 182
    stopped, 109 playing (a track's name, "12 PHAZED", is 77), 47 playing with WRITE on
    (`TopBar::wordsRoom`). `modtab_test` holds the name to fit stopped and playing. **The help card
    says PTRN too; README and the web pages keep the full words.**
  - **The dot at now and the step lit are shown only while the song plays or X runs that MOD**
    (`ModScreen::nowPosition`, `nowPlace`: the project's word, then the engine's
    `Engine::modNow`, the position of the MOD's last tick, and `modPlace`). Read from the
    engine alone, a Stop showed the old dot for a block with nothing to stir the redraw.
    `idle_test` has two parts (X held on a MOD's line; a STEPPER while the song plays, then Stop).
- **A modulated knob's own bar has a thin brass mark where it is given now** (batch 6,
  2026-11-15, call 18; `modMarkAt` / `drawModMark` in `app/mod_knob.hpp`): the bar's centre (what
  the bar itself is drawn at) plus `Engine::modShown` of every MOD aimed at that lane, one clamp,
  `kModMarkW` (2) wide and `kModMarkOver` (3) above and below the bar. DEVICE's macros (the bar's
  place is `ui::kMacroBar...`, which `macroBox` draws from), FX's slot settings and SENDS. **Only
  while the song plays or X runs that MOD**, as the MOD tab's dot, and only once the engine has
  ticked it (`modNow` a number). `modknob_test` `testTheBrassMark` reads it off the drawn screen in
  BROWN and PAPER; `idle_test` has two parts (a macro, a send, playing then stopped). Stills
  `device-modded`, `fx-modded`.
- **FREE is RATE's last choice, an LFO's only** (owner, 2026-11-15: "a free mode ... PHASE can turn
  into a SPEED control"; `kModRateFree` 12, appended; `modFree`, `modPositionFor` in `mod_rule.hpp`).
  **A free LFO's position is SECONDS since Play** (`Engine::playedFor_`, whole samples counted beside
  `sinceStep_`, over the sample rate; stopped, `heardFor_`), so a tempo change does not move it, and
  it is still worked out from where and when: `lfoCycles` is seconds x SPEED, no PHASE (kept, unused).
  The tick is the same tick (counted from each step's start), so a song is the same in any blocks
  exactly where it was. `Engine::modNow` and the tab's dot are in the MOD's own terms (seconds for a
  free one). **SPEED is an amount in Hz** (`ModSettings::speed`, `kModFieldSpeed` 10, appended),
  0.02 to 20, usual 1: the D-pad clicks along `modSpeedClick`'s 31 numbers (`modSpeedStep`: between
  two, the next; the ends stop), the stick on the tab turns it on its log scale (`modSpeedTurned`,
  `amount()`); B 1 Hz. **A STEPPER has no FREE**: `ModKnob::rates` stops its RATE at 8 BARS, and
  `modHeldFor` puts a STEPPER at FREE to 1 BAR (the one place; a hand-written one also plays 1 BAR's
  16 steps in the engine). The dice keep a free LFO free (SHAPE and SPEED, 0.1 to 5 Hz). In a song
  file SPEED is a ninth number on `mod`, **left off at 1 Hz** (so a song without it is written as
  before), kept while the LFO is in time. The knob's fields moved for "0.025 HZ" (DEVICE's NOW from
  528, FX's RATE 465 and last field 550: `testFreeOnTheKnob` holds the widest Hz the stick can give).
  `mod_test` (`testTheFreeRule`, `testAFreeLfoCountsSeconds`, `testAFreeLfoInAnyBlocks`,
  `testAFreeLfoHeardWhileStopped`), `modapp_test` `testAFreeLfoInTheSong`, `modtab_test` `testFree`,
  `modknob_test` `testFreeOnTheKnob`. Stills `mod-free`, `device-mod-free`. Nobody has heard it.
- **Lanes 22 and 23 are MOD 1's and MOD 2's DEPTH** (batch 7, 2026-11-15, call 10;
  `modDepthLane`, `modDepthToLane` / `modDepthFromLane` in `mod_rule.hpp`: 0.5 no depth, 0 -100%,
  1 +100%, the song file's DEPTH scale). Written like any lane (WRITE records a DEPTH turned:
  `Session::setMod` when DEPTH changes on a MOD that stays on; made or deleted is not a turn), in
  `auto` lines, in the scene's `SceneStore`, so a scene move and undo carry them as any lane. **A
  MOD's own DEPTH is its lane's knob**: `SetMod`'s DEPTH sets the lane's `autoBase` and `autoNow`,
  so a scene with nothing written goes back to it (the owner, 2026-11-15: the clearing is left as
  it is). `runMods` reads the lane's centre, and **where the lane rests on its knob it uses the
  MOD's DEPTH itself** (0..1 and back is not always the same float: a song's MODs at rest give what
  they gave before, to the bit). **So the engine runs a MOD at any DEPTH** (`ModSettings::runs()`;
  `on()` still wants one, for the app): a MOD at 0% cuts the chunks on the tick while the song
  plays, and one turned to 0% lets go at once. A MOD deleted leaves its lane's automation where it
  is (no new clearing: the owner's word); the tab shows it (the purple line) and B on DEPTH
  empties it in the scene worked on, as B on an FX setting does. While playing, the knob's
  DEPTH field, the tab's DEPTH row and line and its pictures read `Session::modDepthShown`.
  `mod_test` (`testTheDepthLanes`, `testASceneMoveCarriesTheDepthLane`...), `modapp_test`
  (`testTheDepthLanes`: WRITE, undo, the file, NEXT SCENE), `modtab_test`, `modknob_test`. Still
  `mod-depth`.
- **LEVEL and PAN are MOD targets 32 and 33** (batch 8, 2026-11-15, call 14; **TREMOLO stays**, the
  owner: an effect taken out would break the songs that use it). Not lanes, not automated, every
  track. The engine keeps the mixer's own (`Track::levelSet`, `panSet`, what `SetTrackLevel` /
  `SetTrackPan` say) apart from what is given (`level`, `panL`, `panR`: `Engine::giveMix`):
  `modLevelGiven` (gain times 1 + added, held to 0..2) and `modPanGiven` (place plus added, held to
  -1..1), each the mixer's own **exactly** with nothing added, so a song with no MOD takes the
  render's at-rest path as it always did (`effects_test --print` the same; `mixer_test`
  `testTheMiddleIsUntouched`). `runMods` keeps their offsets in `Track::modMix`; `stillLane` lets
  them go too. The mixer's card, its file line and undo are the mixer's own, never the wobble. The
  MOD tab's list of targets has a sixth line, MIXER. No knob gesture (the mixer is A held on a head)
  and no brass mark on the mixer's card. `mod_test` (`testTheMixRule`, `testLevelAndPan`,
  `testLevelAndPanHeard`: silence at -100%, hard left, the same in any blocks and helpers),
  `mixer_test` `testAModOnLevelAndPan`. Still `mod-mixer`.
- **GLIDE FROM is the track's choice of which key of the moment before**
  (owner, 2026-11-13: "I always like the option"; `glideFromKey` in
  `glide_rule.hpp`). The numbers are in songs, appended: 0 TOP (the highest,
  as it was), 1 PAIRED, 2 NEAREST (a tie: the higher), 3 RANDOM. A number
  nobody knows is TOP (`glideFromKind`). The lengths do not change; "that
  note" is the key picked, and FULL counts from its moment.
  - **PAIRED**: new `i` of `n` (by pitch) from old `round(i * (m - 1) / (n - 1))`,
    a half rounded up; one note alone, from the top. **It counts every note
    of the moment**, a chord's keys too, gliding or not: a bass note under a
    chord comes from the bottom, a melody note over it from the top.
  - **The trail keeps every key of the last two moments** (`GlideKeys`: 128
    bits, one a key, so there is no cap and nothing is left out).
  - **PAIRED must know a note's whole moment when its first note begins.** So
    a PAIRED track's step is gone through twice by the one set of conditions
    (`fireStep`: `telling_`, then for real): the first time `startNote` and
    `startChordNote` only tell the trail what is coming (`tellComing`) and
    change nothing. The list holds 128 (`kGlideComing`: all a step can ask);
    one more is not kept and not counted. `glideLines` tells in the same whole
    steps, **and at the scene's last step also the early notes of the first
    step of the next time round**, as the engine's last step asks them ahead
    (a note half a step late there and one half a step early on step 1 are one
    moment: `testPairedWhereTheSceneGoesRound`). **`telling_` is false outside that loop**; anything new that begins
    a pattern's note inside `fireStep` goes through those two functions.
  - **Known, held by `testPairedAcrossAStepLine`, not hidden**: two notes
    within 1/256 of a step with a step line between them. The first is told a
    step before the second, so it counts as alone (from the top). The roll
    draws the same. Also: PAIRED turned on mid-step, that one step's notes
    count as alone.
  - **RANDOM is a roll for where and when** (`glideSeed(track, at)`: the track
    and the whole step nearest the moment, counted from Play; then
    `chordUnit(seed, kRollGlide, pitch)`). Never a generator walked, never the
    scene's number. **The roll draws the first time the scene plays after
    Play** (`glideLines`' `timesRound` 0; later rounds roll anew, and a scene
    that is not first in the song is first played at another time than the
    roll assumes). Play always starts on the scene worked on
    (`Session::togglePlay`), so the roll is right for the scene Play was
    pressed on, song or scene alone; a song played through INTO a later scene
    rolls other keys there. `testRandomOnALaterScene` holds both.
  - **It is the track's, not the patterns'**: `TrackData::glideFrom`,
    `Session::setGlideFrom`, `Command::SetGlideFrom`. Not in a track image,
    not in a scene, so a scene move carries nothing for it. `applyProject`
    tells every track's. `RemoveInstrument` puts the engine's back to TOP.
    **In undo** since 2026-11-14 (a step of its own). In a song file:
    `glidefrom N`, a line of a synth track's own, left out at TOP; under a
    drum track it is read and not taken.
  - **The gesture: X held on a note, L1 back, R1 on**, a click, stopping at
    each end (`NoteScreen::fxFrom`). Zoom and carry wait for X to be let go,
    on a note only: on a chord L1 / R1 are as they were. The dice leave it
    alone. On the strip it has NUDGE's place while X is held
    (`kStripHeldFromX`): dim while the note has no glide, brass when not TOP.
  - Tests: `glide_test` (each type by hand-worked keys, chords strummed and
    arpeggios, ratchets, RANDOM's rolls, block lengths and helpers, the roll
    against the engine under each type), `glideapp_test` (the Session, the
    song file, undo, the lines' lean off the screen), `under_test` (the strip
    and the gesture), `nextscene_test` (`fill`'s chord track is PAIRED; one
    proof again under RANDOM).
- **The roll's glide line ends at the note's start** (the owner's picture);
  the slide is heard FROM the note's start. A line may begin bars before its
  note (FULL), or before the first bar's top, which is the scene's end: so
  the roll draws **every bar's lines wherever they cross the bar shown**
  (`glideShifts`, `glideSpan` in `note_marks.hpp`; drawing only the bar shown
  and the next left a slow line out of the bar it starts in). The line is
  rectangles gathered and drawn in one call (`glideLineRects`): a call a
  pixel cost 0.6 to 0.9 ms for 64 long lines on the desk, this 0.2 to 0.4
  (`GLIDE_PRINT=1 ./build/glideapp_test`; not timed on the handheld). `glideapp_test`
  reads the lines and the strip **off the drawn screen**
  (`testTheLinesOnTheScreen`, a software renderer and the real lettering).
