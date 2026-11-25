# Drums, recordings and the list of recordings

The dice, hearing, placed level, the sampler's END, recordings, slices, copy a bar, step effects, FEEL, SWING, CHOKE, EDIT, DRUM SYNTH, the picker, THROW, kit presets.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **Hold X + Y is the dice** for what a hit or a note does (`fxRoll` on both
  grids). **Hold X + A is THROW** (`fxThrow` on both): OFF, ECHO, SPACE, BOTH.
  The check for X held comes before A's own jobs, so A alone still places.
  **The dice do not throw**, and do not take a THROW away (owner's decision).
  **Hold X + L1 / R1 on a note is the track's GLIDE FROM** (the glide rules, below).
- **X HELD hears a recording until it is let go, to its end** (owner,
  2026-11-14): the SAMPLER and KIT pages, their list, the drum grid's lane
  name and empty step, the FX screen and the roll's empty place on a sampler
  track. A tap still sounds `Session::kAuditionMs` (350); **the built-in
  kit tapped is heard whole**, as before (its cymbal is 1.8 s): `letGoHeard`
  stops a tapped lane only when it holds a recording. Screens call
  `Session::auditionHeld` (a synth key, held at `kHeldOn` in `sounding_`) or
  `hearLaneHeld` (a `Trigger` with `e = 1`); **`App::update` lets go**
  (`keepHearing`) when X is up, another screen is up or another track
  picked; Play, a new song and panic let go too. A drum lane's is stopped by
  `Command::StopAudition`, which fades (3 ms) only the voices and waiting
  ratchet pieces a held audition started (`HitPlay::audition`; a hit that
  carries the sound on takes it over; a lane plays one sound, so any
  audition chokes the hit before it anyway, joined or not). On a hit, X held stays the step
  effects' hold. The line over the picture stops with it (`heardEnds`).
  `tests/hear_test.cpp`. The SONG screen's X solos the track (it heard it until 2026-11-15).
- **X sounds for as long as it is held wherever it plays something, and over the song while it plays**
  (gesture audit G33, 2026-11-25; the owner: "any of my decisions can be reversed if there is a more logical
  method", 2026-11-24). Before, on a synth: DEVICE off a macro (FOLDER, PRESET, every ADVANCED page) played a
  fixed `kAuditionMs` note and **nothing while playing** (`SynthScreen::hear`, "if (!project_.playing)"); a
  macro, FX's settings and the MOD tab held the note **only while stopped**; FX's list gave a synth the short
  note; the roll's note and key a fixed moment, **its chord nothing while playing** ("the song will play it
  soon enough", `NoteScreen::hearChord`). Now every X there is `Session::auditionHeld`, playing or not: one
  rule ("X hears; held, as long as held") instead of four, and a preset, note or chord is heard against the
  song in one press instead of stop, X, start (the loop restarts). A note over the song can clash: that is
  what the player asked to hear. **A tap is still `kAuditionMs`, and a drum hit still plays whole** (as the
  pattern plays it; a tapped built-in drum whole: `letGoHeard`). **What stays: a MOD held is run only while
  stopped** (call E, 2026-11-15, kept: `Session::hearMods` sends nothing while playing, where the song's
  MODs run). Stepping a preset or moving a note or chord with A held stays silent while playing (not X).
  **X's key and the song's are one key of the synth** (review of G33, 2026-11-25): `Command::NoteOn` / `NoteOff`
  play by pitch, and the song's own note ending let every voice on that key go, X's too, so X held on a note
  the song plays went quiet when the song's copy ended; letting X go cut the song's note. An audition's commands
  now carry `e = 1` (as a drum audition's Trigger does) and the engine keeps the keys it holds
  (`Track::auditioned`): `fireNoteOffs` passes a key X holds by, and an audition's NoteOff leaves a key the song
  still plays (`offIn` below `kNoNote`) for the song's own note-off. Play, Stop, panic and a track taken away
  clear them (so a NoteOff lost to a full queue cannot hold a key for ever). Nothing changes in a song with no
  audition (the same to the bit). **What stays: ACID plays one note at a time**, so the song's next note on its
  track takes the voice from X (the manual says so). Choosing a chord (Y held), X now holds the chord in hand
  as well (it was `hearChord`'s fixed moment a key). `hear_test` `testXHeldOverTheSongsOwnKey`.
  A chord's keys are held as one: `Session::auditionHeld(track, pitches, count, ...)`; every key X holds is
  at `kHeldOn` (only what X holds now ever is), so `letGoHeard` and `stopHeard` let every key go, and a held
  key struck again (a setting turned) stays held. **A on a drum lane's name is silent** (it sounded the drum
  on every press, so every hold of A for the lane's volume was heard); X hears it there, held.
  `hear_test testXHoldsWhatItPlays`. **Not heard on the handheld.**
- **A hit, a note or a chord placed starts at `kPlacedLevel` (0.9, `project.hpp`; owner,
  2026-11-15: it was 0.8), the brushes too, and every screen's X or A hears at it** (the KIT,
  SAMPLER, synth, FX and MOD pages, the designer page's keys): what is heard is what is placed. A
  new way to hear a sound takes it, never a number of its own (`app_test` `testHeardAsPlaced`). A
  keyboard's notes keep their own velocity; the demo song and the dice keep theirs.
- **The sampler's END is setting 7, after LEVEL (ids are appended), and the page shows it beside
  START** (owner, 2026-11-15): the page's order is not the ids' (`SamplerScreen::settingAt`: PITCH,
  TUNE, START, END / ATTACK, RELEASE, LOOP, LEVEL; `goTo` takes a place). A note plays START to END
  and stops, or with LOOP goes round between the two; at 100% it is the engine as before, to the
  bit. **They never cross**: the screen holds each a percent (`sampler::kLeastSpan`) short of the
  other, and `sampler::playedEnd` holds what a song file says (the reader stores it held too). The
  song file writes END only when it is not 100, after LEVEL. The picture marks both (`endMarkX`)
  and lights only what a note plays; the line over it ends there (`Playhead::to`). Not a MOD
  target: no sampler setting is. `engine_test` (`testTheSampler`, `testTheSamplersEndInAnyBlocks`),
  `sample_test` (`testTheSamplersEnd`).
- **A recording is 20 s at most** (owner, 2026-11-18: it was 10): `kMaxSampleSeconds` for a file read
  (a longer one is cut there and faded) and `Recorder::kMaxSeconds` for RECORD; `sample_test` holds both
  (15 s kept whole). **Catch is not tied to it**: the engine remembers `kCatchFrames`, ten seconds.
- **While a take waits or runs, R1 stops it from any stop of a screen that records** (gesture audit
  G34, 2026-11-25; `Session::recordingNow`, checked first in each screen's `onButton`): the kit's lanes,
  every column and the KIT stop (where R1 did nothing), the sampler's every stop (on PRESET R1 opened
  the name keys and the take ran on) and CLOUD's every page (`instruments.md`; not while X holds a
  macro's MOD there: R1 is the MOD's), A held or not. Not the
  kit's EDIT and VOICE views, where the recording line is not shown (VOICE's R1 is the next synth
  lane). **The line is one function, `Session::recordingLine`**, and says the limit from
  `Recorder::kMaxSeconds`: all three copies said OF 10 after the limit went to 20. The R1 hint says
  `stop` meanwhile where it named R1's other job. `sample_test` `testR1StopsATakeFromAnyStop`.
- **A panic calls off a recording that R1 began on the way** (gesture audit G25, 2026-11-25;
  `App::recBeforeShoulders_`, noted at the first shoulder, as the copies are): R1 first on a kit lane, the
  sampler or CLOUD's RECORDING starts one, which would have replaced the sound when it ended, outside undo.
  `Session::dropRecording` (public now) stops the input and keeps nothing. **Not covered: a recording
  already running when R1 went down first**: that R1 ends it, and its take is written and put on at once
  (`finishRecording`), which a panic cannot take back. `sample_test`
  `testThePanicCallsOffARecordingBegunOnTheWay`.
- **A multisample is found from the names, every time it is read** (`Session::multisampleOf`,
  `session_samples.cpp`): the song names only the file picked. Files beside it that spell a note
  (`sampleNote`) are its notes; **VELOCITY LAYERS** (owner, 2026-11-24: picked over round robin, an
  on-device auto-sampler and a zone view, which are not built) are read from a word (`sampleLayer`:
  PPP to FFF, never the first word; SOFT, MED, HARD, LOUD; V, VL, VEL, VELOCITY or LAYER and a
  number, glued straight after a note too, "A0v16"). A file with no layer word sorts as MF. A file
  with no note but a layer word, whose name is otherwise the picked one's (`sampleLayerStem`), is a
  layer of the picked one's note: "Snare", "Snare soft", "Snare_HARD". A note's layers split the
  velocities evenly, softest first (`SampleZone::top`; the engine takes the key's softest zone whose
  top reaches the velocity, inclusive, so with no layers it is the one zone as before, to the bit).
  **Caps**: 24 notes (`kMaxZoneNotes`, the nearest to the picked one's, a lower one first on a tie),
  then `kMaxZones` (48) / notes layers each, at most `kMaxLayers` (4), spread from softest to
  loudest, the picked one put in place of the nearest kept. **An un-noted picked file's root is 60**
  (ROOT then moves it as it moves the set; it was ROOT itself, which ROOT moved again). Worst case
  48 files of 20 s is about 180 MB and is read on the app thread: **load time and memory not
  measured on the handheld**. `sample_test` (`testLayerNames`, `testVelocityLayers`,
  `testVelocityLayersCap`); twelve mutants killed.
- **Sample names from `wavNames` have no `.wav` ending.** The file may end
  `.wav` or `.WAV`: whatever opens or names one tries both (`readRecording`,
  `recordingThere`, `deleteSample`).
- **The slice view is a mode of the drum grid** (`PatternScreen::slicing()`,
  drawn by `pattern_slices.cpp`), not a screen of its own, so every grid
  gesture works in it unchanged. A hit's slice is its `start`; there is no
  separate slice data. A lane has 16 slices, or its FIT in steps if longer.
- **On the slice view's lane name, UP sets ONE SLICE and DOWN sets RUNS ON** (gesture audit G09,
  2026-11-25; `PatternScreen::sliceUpDown`). Either key flipped it (Claude's slice view, commit 966202f,
  no owner tag), and a held key or the stick runs on (up to 33 and 18 flips a second), so where it
  stopped was luck. Set, a run sets the same again; one that is so already sends the engine nothing
  (`Session::setLaneOneSlice` still sends on every call; the check is in the screen, the one place a
  run calls it, so the Session's other callers, a kit loaded and undo, are as they were). The hint already read `UP / DOWN one slice / runs on`, in that order. `sample_test`
  (`caughtUp` for nothing sent; held for 0 to 4 repeats each way).
- **On the drum grid's BARS stop a held LEFT / RIGHT runs on with A or Y held too** (gesture audit G21,
  2026-11-25; `PatternScreen::direction`, one function for a press of the D-pad and its held repeats).
  The press turned the bar, and the repeat went to `nudgeExact`, which on BARS takes only up / down, so
  it stopped dead. The repeat used to test `input.held(Y)` and `input.held(A)` without `ySpent_` and
  `aSpent_`, so after a feel or a THROW it could do what the press would not; now it does what the press
  does. `copybar_test testDrumBarsLength`.
- **COPY A BAR: hold L1, R1 copies the bar shown, A pastes over it** (owner, 2026-11-17: "copying
  steps 1-16 to paste on 17-32"; `Session::copyBar` / `pasteBar` / `BarClip`, `copyBarHere` /
  `pasteBarHere` for the screens; `tests/copybar_test.cpp`). The drum grid, its slice view and the roll.
  - **The clipboard is memory only**, never in the song file; a new song keeps it. It holds a drum
    bar (`DrumPattern`, every field) or a note bar (`NotePattern`, chords and their effects), the bar's
    16 steps of every automation lane, and where it came from.
  - **The copy stays after a paste** (gesture audit G23, 2026-11-25: a paste spent it; reasons in
    `song-screen.md`), so one bar goes on bar after bar with L1 + A each; L1 + R1 copies afresh.
  - **A paste REPLACES the bar** and is **one step of undo of its own** (as NEXT SCENE: the burst
    before it ends, and `settled_` is taken after it, so an edit straight after is a step too). Onto a
    bar that is already the same (compared field by field; automation too on the same track): nothing,
    no step. The engine gets the track's image (`sendTrackImage`, as `setCellBars`).
  - **Drums only onto a drum track, notes onto any synth or sampler track**; a track with nothing, or
    a bar past the cell's end: refused. **A note over the bar line keeps its length, held to the end of
    the cell it lands in** (as the roll's `room`). **Automation only onto the SAME track** (another
    track's lanes are other knobs; its own are left).
  - **The gesture**: L1 must go down FIRST, with nothing else held (`l1First_`; not with X held: X's
    L1 / R1 are the step effects, GLIDE FROM, the feel). R1 then copies (on the drums at once, no
    brush; on the roll R1 on a note still carries, and only its let go where it went down copies), A pastes
    (no hit or note placed; on the grid `aSpent_`, so A's hold nudges nothing). **L1's own tap acts on
    its let go**: the drum grid's slices (`l1Used_`: any other button pressed while L1 was held cancels
    it) and the roll's zoom (`zoomUsed_`, set by a copy or a paste). **A stick move with L1 held
    cancels it too** (gesture audit G31, 2026-11-24: it moved the cursor and the let go still opened
    the slices or zoomed; `copybar_test testAStickMoveIsAUseOfL1`). The App tells both screens when
    another shoulder goes down with L1 held (`shouldersPressed`), so a panic's let go of L1 opens no
    slices; **a panic begun L1, R1 puts the copy back** (`clipBeforeShoulders_`, `barCopies`).
  - What was done is said in the App's box above the bar (`Session::say`, taken by
    `App::takeSessionSaid`): BAR 1 COPIED, PASTED ON BAR 2, BAR 2 IS THE SAME ALREADY, NOTHING
    COPIED YET, DRUMS GO ON A DRUM TRACK, NOTES GO ON A NOTE TRACK. Each fits the box at LARGE
    (`testTheWordsFit`).
  - **The hints are in the bar only while L1 is held** (UP / DOWN change bar, LEFT / RIGHT bar count,
    R1 copy bar, A paste bar, the same on both grids): the plain bars of the drum grid and the roll have
    no room. Not on the help card (full). Stills `copy-bar`, `copy-bar-notes`, `copy-bar-refused`.
  - **Hold L1 + UP / DOWN is the bar before or after, round the ends; hold L1 + LEFT / RIGHT the cell's
    count** (1, 2, 4, 8), on both grids (commit 5e2ae67, 2026-11-22, under the owner's own git name:
    "Hold L1 with up / down turns to the bar before or after on the piano roll and the drums, from
    anywhere; hold L1 with left / right sets the cell's count"). This line said the roll's L1 bar read
    UP / DOWN zoom, and `pattern_screen.hpp`'s header gave the two the other way round (gesture audit
    G21, 2026-11-25: both put right, the keys unchanged). **Which way they should go is the owner's
    question** (G21's options: the count always with LEFT / RIGHT, BARS too; or the drum grid's L1
    turning bars sideways as its time runs), not changed here.
  - Mutants: every one tried in `copybar_test` dies but the App's two `shouldersPressed` calls (the
    panic's, and "another shoulder with L1 held"): each is the other's belt, and the two taken out
    together die (`testTheSlicesAndL1`, the panic begun L1, L2, R2, R1).
- **A drum hit that starts where the lane's sound has just got to carries it
  on** (`DrumMachine::trigger`, within 1.5 ms, never from the very top). That
  is what makes slices in order seamless. Do not "simplify" it away.
- **Step effects are four small values on each drum hit** (`StepFx`, drums.hpp):
  pitch, ratchet, reverse, cut. Hold X on a hit sets them. In a song file they
  are `stepfx` lines (`fx` is an effect in a slot: the first try clashed).
  A ratchet waits in one `Pending` slot that re-arms itself.
  - **X held + B empties them** (gesture audit G11, 2026-11-24; `PatternScreen::fxEmpty`): pitch,
    ratchet, reverse, cut and THROW. The hit stays, its level, nudge, chance and start as they were,
    and is heard plain at once (stopped). It deleted the hit and all of it before (B was blocked only
    by A or Y); a hit is no longer lost to a B pressed while setting its effects. **A step of undo of
    its own**; nothing to empty, nothing and no step. Plain B deletes once X is let go.
- **FEEL is two amounts on each drum LANE, and it is live** (owner, 2026-11-14; built on branch
  `feel`, 2026-11-15; `engine/include/apotheca/feel_rule.hpp`, `LaneFeel`): HUMAN TIME and
  HUMAN LEVEL, 0..100, a chord's six clicks (`kAmountAt`) and a chord's two "mosts"
  (`kHumanTimeMs` 12, `kHumanLevelMost` 0.15). Nothing is written into the hits. A hit on a
  main beat (steps 1, 5, 9, 13: `feelOnBeat`) moves half as much (`kFeelBeatShare`), in time and
  in level. **Nobody has listened to it**: the tests measure where each hit lands and how loud.
  - **Every roll is worked out from where and which time** (`feelSeed(track, lane, bar of the
    cell, step, steps since Play)`): never a generator walked, never the scene's number
    (`nextscene_test`'s `fill` has three felt lanes, and fails if the seed takes the song's bar).
  - **A lane with both at 0 takes the path it always did** (`schedule`, `playHit`): old songs
    are the same to the bit (`effects_test --print` the same before and after, and a busy drum
    song with nudges, ratchets, chance, throws, a DUCK, tempo changes and a Play-once gave one
    checksum on `main`'s engine and this one).
  - **A lane with HUMAN TIME has every hit asked a step ahead** (`askAheadHits`: the roll may
    bring it early), and the track keeps that it was (`Track::feelAsked`, the step's number, as
    `chordAsked`): the hit is not begun again on its own step, whatever the lane says by then.
    Without it a feel turned while the song played lost hits or played them twice
    (`testTurnedWhilePlaying`). Play clears it; a track taken away puts the lane at rest.
  - **Everything a felt hit times is a whole number of samples** (the wait, `std::ceil`; a
    ratchet's gap, `std::floor`), so it is the same at any block length. A plain lane's nudge and
    ratchet are still a part of a sample long, as they always were: `feel_test`'s block test
    gives the lanes with HUMAN LEVEL alone no nudge and no ratchet for that reason.
  - The song's first step has no step before it: an early roll lands on the top. Played once
    (a bounce, AT END STOP) the last step asks nothing of the first, as for chords.
  - **Asked again** (a scene move, AT END switched on the last step: `askAgain`): a felt hit
    counts from the sample its asking step began on (`std::floor(elapsed)`), so it comes on the
    very sample it would have. One that has not sounded gives its mark back and may be asked
    again; **asked again after its early moment has gone by, it comes on its step** (a downbeat
    a few ms late, not lost: `schedule` returns false, the mark is not set). One that has
    sounded (`Pending::begun`) keeps its mark; its ratchet pieces still waiting go, as an early
    hit's always did. **In a scene move the old scene's hit and the new one's on the same lane
    and step are one roll** (the seed has no scene in it), so on the grid the hit is on the same
    sample sounded or not, and giving the mark back shows only when AT END is switched: that
    line's mutant is killed by `testAskedAgain`, not by any scene move (`nextscene_test` does not
    see it; `feel_test` `testASceneMoved` holds the rest of the path beside a song built with the
    scene in it: the sample counted from, and a felt hit WRITTEN early whose moment has gone by,
    which is not played late, as a plain one is not).
  - **CHANCE is still the walked dice** (`dice_`), so a felt hit with a CHANCE that is asked
    again rolls its chance again (as a nudged-early one always did), and from then on the song is
    not "the song played straight through" in its chances. `fill` keeps the feel off its CHANCE
    lane for that reason. Not mended: making chance a roll by place is a change to every song.
  - **A run of slices moves as one, through the bar lines inside its cell** (`feelRunBack`): a
    hit that starts further into its sound, with a hit on the step before it, takes the TIME of
    the hit its run begins with, so slices in order still carry the sound on (`testSlices`: the
    same samples, sooner or later, to the bit). Its LEVEL is its own (tested step by step). Moved
    each by itself, every slice met the sound up to 24 ms off and cut it. **The run goes back
    over a bar line of the same cell** (review, 2026-11-15: a two-bar break in order was cut at
    its inner bar line in 4 rounds of 6; `testSlicesOverABarLine`), never from the cell's first
    bar to its last: where the cell goes round a new run begins. `schedule` reaches the bars
    before with `&scene - bar` (a cell's bars stand in a row in `SceneStore::drums`): **whoever
    calls `schedule` hands it the bar of the CELL and that bar's own `DrumScene`.** The walk is
    a step a slice: at most 127 for the last slice of an eight-bar break.
  - **A tempo change keeps a felt hit's wait whole** (`Pending::whole`, `SetTempo`: the wait
    `ceil`, a ratchet's gap `floor`): scaled to a part of a sample, the one hit waiting on each
    felt lane came a sample sooner or later by the block length (review; `feel_test`'s block
    test turns the tempo five times, at blocks of 1 to 333 and on three helpers).
  - HUMAN LEVEL is held to 0.02..1: **a hit written at full only gets quieter** (as a chord's).
  - Three waiting slots a lane are still enough (a ratchet running on, this step's hit, the next
    step's): HUMAN TIME is never more than a quarter of a step (`kFeelMostOfStep`; at the
    program's fastest, 240 BPM, 12 ms is under a fifth).
  - **The app**: `TrackData::laneFeel`, `Session::setLaneFeel`, `Command::SetLaneFeel` (a = lane,
    b = time, c = level). `sendSong` tells every drum lane's, at rest too. In a song file
    `feel LANE TIME LEVEL`, a line of the kit's own for each lane, **left out when both are 0**;
    under a synth track it is not taken; the numbers are held as doubles first. **In undo by
    `soundMark` / `restoreSounds`** (as GLIDE FROM: not a pattern), the turns of one hold one step.
  - **The gesture: X held on a lane's NAME** (`PatternScreen::feelHeld_`, as `fxHeld_` on a
    hit): LEFT / RIGHT HUMAN TIME, UP / DOWN HUMAN LEVEL, a click each, stopping at the ends
    (`app/lane_feel.hpp`); Y the dice (OFF, 10 or 25% of each, never none, never what it was);
    A this lane's feel onto every lane (from OFF: all plain). **X there still hears the drum,
    exactly as before** (`hearLaneHeld` on the press, `keepHearing` lets go): the one hold is
    both, and a turn of the feel sounds nothing (the feel is the playing pattern's). L1 and R1 are
    SWING while it is held (next rule). **Y let go after X is not a tap** (`yDidSomething_`
    is set by the dice): it rolled the lane's hits, and on a hit it stepped the chance. **A or Y
    pressed while X was held does nothing of its own if X is let go first** (`aSpent_`,
    `ySpent_`, until the button comes up; a hit's THROW and dice too): with A still down the
    D-pad turned the lane's volume, with Y it rolled the lane's hits (review, 2026-11-15;
    `testXLetGoFirst`). The directions move the cursor meanwhile.
  - **X + B puts this lane's FEEL and SWING back to OFF** (gesture audit G11, 2026-11-24;
    `PatternScreen::fxEmpty`): HUMAN TIME, HUMAN LEVEL and SWING, that lane only (X + A from a lane
    at OFF still does every lane). **A step of undo of its own** (`beginOwnUndoStep`, the turns of
    the hold before it a step by themselves); a lane already plain: nothing, no step. No chip (the
    strip's chips stand under their fields; an "all off" has none): the help card and the manual.
    `easy_test xempty`.
  - **X let go with the stick pushed: the cursor waits for it to come back** (gesture audit G14, a
    hit or a lane's name; see `notes-and-mods.md`).
  - **The name's one-line summary gives way to what is said on its row** (`said()`: why L1 opened
    no slices on a built-in drum): the two were drawn one over the other.
  - The strip's fields and their chips share `kFeelTimeX`, `kFeelLevelX`, `kFeelAllX`
    (`pattern_layout.hpp`); EVERY LANE reads COPY, or THE SAME once they are. With X up the name's
    strip says the feel in one purple line. **The mark** (`drawFeelMark`, both views): at the left
    of the name, the upper piece HUMAN TIME, the lower HUMAN LEVEL, purple.
  - Tests: `tests/feel_test.cpp` (the engine: the rule by hand, each hit's place and level,
    blocks, helpers, Play twice, once, turned while playing, slices, DUCK and THROW, asked
    again) and `tests/feelapp_test.cpp` (the gesture, the strip off the drawn screen, the song
    file, undo, a song sent again, a bounce, X still hearing, the stick's pace, X let go first).
    `feel_test testTime testSlices` runs just those. `textsize` holds the name's line and the
    held strip as tight rows.
- **SWING is a third amount on each drum LANE, live, and no roll** (owner, 2026-11-14; built on
  branch `swing`, 2026-11-17; `feel_rule.hpp`: `swingOffStep`, `swingSamples`; plan: the SWING
  section of `docs/plans/drum-feel-plan.md`). The hits on steps 2, 4 ... 16 come SWING percent of a
  third of a step late: **at 100% exactly a third (`amount * samplesPerStep / 300`), each eighth
  played 2 to 1, a triplet**. FEEL's six clicks. **Nobody has heard it.**
  - **It goes through FEEL's path in `schedule`**: a swung lane's every hit is whole samples (`ceil`
    of nudge + roll + swing, `Pending::whole`), and **a run of slices takes its first hit's swing**
    (`feelRunBack`'s head), so a break in order is never swung inside. **A lane at 0 is the old path
    to the bit**: `effects_test --print` the same, and a busy drum song's sum the same on `main`'s
    engine and this one (`swing_test --print`).
  - **Never early, so never asked ahead for it**; a hit nudged early is asked as before (the
    wait counts from `floor(elapsed)` on a swung lane, as a felt one). **A swung ratchet is squeezed
    into its step less the swing** (`playHit`'s `late`): unsqueezed, a x4's last piece cut the next
    step's hit. The step before is not stretched.
  - **The app**: `TrackData::laneSwing`, `Session::setLaneSwing`, `Command::SetLaneSwing` (a = lane,
    b = amount; appended), every drum lane's sent by `sendSong`; in a song file `swing LANE AMOUNT`,
    **left out at 0**; undo by `soundMark` / `restoreSounds`; the designer page's `kKitSwing`.
  - **The gesture: X held on a lane's name, L1 less, R1 more** (`swingTurn`); X + A copies it with
    the feel (THE SAME needs all three alike); **the dice leave it alone**. The strip's SWING field
    is first, `kFeelSwingX`, its L1 / R1 chip under it. **The mark** (`drawSwingMark`, both views): a
    long and a short purple dash at the top right of the name. The name's line reads SWING, HUMAN
    TIME, HUMAN LEVEL. Stills `feel-swing`, `feel-swing-held`; `feel` and `feel-same-held` swing too.
  - Tests: `tests/swing_test.cpp` (the engine: places at every amount and at 133 BPM, nudge and
    HUMAN TIME added, ratchets, blocks 1 to 333, helpers, tempo turned, Play twice, a bounce, turned
    while playing, slices, DUCK and THROW, a track gone, a scene put in), `feelapp_test` `testSwing`
    and `testSwingKept`; `nextscene_test`'s `fill` has two swung lanes. Cost: nothing that shows
    (`bench`'s SWING rows: every lane of twelve kits at 100%, -0.01%).
- **CHOKE is a group on each drum LANE, two of them** (owner, 2026-11-15: "choke the open hi hat with a
  closed one"; `DrumMachine::setLaneChoke`, `kChokeNone` / `kChokeGroups` and `chokeGroupOf` in
  drums.hpp). A hit on a lane in a group gives every OTHER lane of that group still sounding the
  fade a lane already cuts its own last hit with (`chokeStep_`, 3 ms), in `trigger` (`chokeOthers`).
  **Every trigger counts**: a ratchet's pieces, a hit that carries its lane's sound on, a hit by
  hand; the thrown copy goes with its voice. **Hits that start on the same sample do not cut each
  other** (`Voice::born` against `renders_`, counted by `render`): both were written, and lane order
  must not decide. A lane cuts itself as it always did, so a group of one is no group; nothing
  grouped is the sound as before to the bit (`effects_test --print` the same, `speed_test` passes:
  the runs see a choked voice's fade as any other). A group set cuts nothing by itself; a track
  taken away is in none. **A new kit has its two hats in CHOKE 1** (`chokeTheHats` / `newKitChoke`
  in project.hpp, `Session::chokeNewKit`): a drum track added, a new song's track 1, a track given
  the kit, the first song on a card, the demo. A blank `TrackData` (a song read from a file) has none.
  A new song is still not worth a file until something is put in it (`saveNow` takes its text with
  the hats grouped, and a project as made, as empty).
  - **The app**: `TrackData::laneChoke`, `Session::setLaneChoke`, `Command::SetLaneChoke` (a = lane,
    b = group; appended). `sendSong` tells every drum lane's, NONE too. In a song file
    `choke LANE GROUP`, **left out at NONE**; a group nobody knows is NONE; under a synth track it is
    not taken. **In undo by `soundMark` / `restoreSounds`**, as FEEL.
  - **Set on the KIT page's third column** (`KitScreen::kChoke`, between FIT TO and the recording,
    `kChokeX`/`kChokeW` in kit_screen.hpp): hold A + UP / DOWN NONE, 1, 2 (the ends stop), B none;
    left / right go round the columns (four since EDIT). The recording's column is narrower for it, so its
    missing line is two short ones (FILE NOT FOUND. / THE DRUM PLAYS.), and what is said for a
    moment (a catch, a recording) is drawn over the three heads from FIT TO on.
  - **The mark** (`drawChokeMark`, both views, `kChokeMark...` in pattern_layout.hpp): brass, at
    the bottom right of the lane's name, a dash for CHOKE 1, two for CHOKE 2.
  - Tests: `tests/choke_test.cpp` (the engine: the cut and its fade, groups apart, the same sample,
    as it was, ratchets, slices, THROW and a hit by hand, blocks and helpers, a group changed, a
    track gone, numbers out of range) and `tests/chokeapp_test.cpp` (the new kit, the KIT page,
    the mark off the drawn screen in BROWN and PAPER, the song file, undo, a song sent again).
    Stills `choke`, `kit-choke`, `kit-choke-held`. **Nobody has heard it.**
- **EDIT is what a drum lane does to its sound, and the file is never written** (owner, 2026-11-16:
  "only affected the sample in the project, not the raw file"; `app/lane_edit.hpp`, `LaneEdit`,
  `TrackData::laneEdit`, `Session::setLaneEdit`). START, END (parts of the file in `kEditUnits`),
  FADE IN, FADE OUT (ms), GAIN (tenths of a dB), PITCH, FINE, REVERSE, all whole numbers (undo
  hashes the struct whole). **The engine knows nothing of it**: the app makes the lane's sound and
  hands it over as any recording (`setLaneSample`, the old one through the trash), so FIT, slices,
  the carry-on, CHOKE, THROW, Catch and a bounce all take the edited sound unchanged.
  - **The order is fixed** (`makeShape`, then FIT, then `makeLevel`): the cut (each CUT edge fades
    `kEdgeMs`, the file's own edges are left), REVERSE, PITCH (a windowed sinc, cut lowered going
    up), then FIT, then the fades as heard, GAIN and the guard (`guarded`: from `kKnee` 0.9 up,
    rounded into full scale; every edited lane, even at GAIN 0). **What FIT is worked from is the
    shape** (`LaneSound::shaped`), and the fitted sound is KEPT (`fitted`), so a GAIN or a fade turned
    on a fitted lane is one pass, never a fit again (`setLaneEdit` reshapes only when START, END,
    REVERSE, PITCH or FINE moved).
  - **A lane with every edit at rest plays the file as it always did, to the bit**, and a built-in
    drum at rest is the kit's own (`installLane`: nothing installed, no copy; `source` aliases
    `DrumKit::builtIn`). An edited built-in drum is a lane sample, and is drawn on the KIT page.
  - **A shape too costly for a frame goes to the helper thread** (`shapeCost` over `kShapeInAFrame`:
    PITCH or FINE on a long sound; a 10 s file at -24 is 0.1 s on the desk). It is the FIT job's
    thread: `Job::shape`, `LaneSound::shaping`, made and fitted in one go, the version checked as a
    fit's is. Until it lands the lane plays what it had (freshly read, the file), its level made at
    once; the view says MAKING IT..., the kit MAKING. Anything that waits for fits
    (`finishStretching`, a bounce) waits for it too.
  - **Another recording puts START and END back to the whole of it, the rest stay**, in ONE step of
    undo with the name (gesture audit G04, 2026-11-25; songs-and-undo.md): undo puts the old
    recording back with its own cut. (It was "no step of undo (`setLaneSample` takes the mark again:
    undo would put the old cut on the new file)"; with the name in the step that cannot happen.)
    `loadLane` (a song opened, a file sent over WiFi, a rename) keeps every edit.
    NORMALIZE (`normalizeLane`: the GAIN that puts what is played, fitted if it is, at -1 dB or
    just under) and TRIM SILENCE (`trimLaneSilence`: 40 dB under the file's loudest, START 1 ms
    ahead) only set the boxes.
  - In a song file `laneedit LANE START END GAIN FADEIN FADEOUT REVERSE PITCH FINE` (START and END
    0..1 of the file, GAIN in dB), **left out at rest**; a number that will not read ends the line
    there, the rest at rest; held by `laneEditHeld` (END after START by a unit: the file's length is
    not known there); under a synth track not taken. In undo by `soundMark` / `restoreSounds`.
  - **The KIT page's fourth column is the sound's picture, EDIT** (`KitScreen::kEdit`); a tap of A
    opens the view (`editing()`, `openEdit`), laid out as the sampler's page (`kEdit...` in
    kit_screen.hpp), the file's picture drawn by `ui::wavePicture`, which the sampler shares. Hold A
    + up / down turns a box, the stick by the square of its push (`laneEditRange` a second); B its
    usual value, B on a box at its usual value back to the kit (on letting go); hold B half a second (G29),
    every edit to none (any other button or a move calls it off); Y on START / END trims, elsewhere
    normalizes; X hears. Stills `kit-edit`, `kit-edit-held`, `kit-edit-deleting`,
    `kit-edit-column`; `textsize` holds the view at its widest; `idle_test` has a part.
  - **A B straight after Y goes back to the kit and keeps what Y set, in EDIT and VOICE** (gesture
    audit G16, 2026-11-25, under the owner's "any of my decisions can be reversed if there is a more
    logical method", 2026-11-24). The rule above ("B its usual value, B on a box at its usual value
    back to the kit"; no owner mark) left Y's box under the cursor: TRIM SILENCE leaves it on START,
    NORMALIZE on GAIN, ROLL on a knob, so the B meant to leave put START back to 0 or GAIN to 0 dB,
    half undoing Y, and leaving took a move to an untouched box and B (3 presses). The exit the audit
    proposed first, LEFT from a row's first box, is the one the owner turned down on FX ("LEFT doing
    two jobs was awkward", 2026-11-14), so B carries it: `yJust_` / `yBox_` (`bLeavesAfterY`), set by
    Y on its box and cleared by ANY press (a move, X, L1 / R1) or by opening the view; the stick
    moves no button, so the box is checked too (on another box B is that box's own). That B sets
    `bBack_` and puts nothing back. Held, it still deletes every
    edit at the bar's end. The bar says `B back` while it is so. `laneedit_test testBAfterYKeepsIt`,
    `drumsynthapp_test testBAfterARollKeepsIt`.
  - Tests: `tests/laneedit_test.cpp` (each step against a sound made by hand, the engine beside a
    second engine handed the hand-made sound, blocks, FIT, slices in order as one hit, the built-in
    drums, the helper thread, the file's bytes, the song file, undo, the gestures and their hints).
    **Nobody has heard it.**
- **DRUM SYNTH: a drum lane's sound may be a SYNTH VOICE** (owner, 2026-11-17: "Let's do a drum synth";
  `docs/plans/drumsynth-plan.md`; `engine/include/apotheca/drum_synth.hpp`, `engine/src/drum_synth.cpp`,
  `TrackData::laneSynth`, `Session::setLaneSynth`). Twelve MODELS (KICK, SNARE, TOM, CLAP, CL HAT, OP HAT,
  CYMBAL, RIM, COWBELL, CLAVE, ZAP, NOISE), each a `Voice`: the model and eight dials 0..100 in ONE set of
  places (TUNE, DECAY, TONE, SNAP, NOISE, SWEEP, DRIVE, the model's own: BEND, BODY, SPREAD), a model
  showing 4 to 8, each read in its unit. **Nobody has heard any of it; not run on the handheld.**
  - **It is rendered, never played live**: `drumsynth::make` on the app thread when a knob turns (the
    helper thread past `kWorkInAFrame`, `LaneSound::making`, `Job::make`), and the sound becomes the
    lane's RECORDING (`madeLane`: `recording` and `source`), so EDIT, FIT (which a built-in drum has not),
    slices, CHOKE, FEEL, THROW, Catch and a bounce take it unchanged. The engine knows nothing of it.
    Until a long one lands the lane plays what it had.
  - **The same voice is the same bits, desk and handheld** (held under qemu with the handheld's build):
    noise is `noiseAt(seed, n)`, a hash; the loop's sine is a polynomial; `tan` is the voice's own (the
    handheld's library differs in its last bit); `pow` / `exp` only for the recipe's numbers; fp
    contract off. **Every voice peaks at -1 dB exactly and ends in exact zeros**; every envelope snaps to
    0 under `kTiny` and the filters are fed steady noise (the envelope after them), so flush-to-zero
    changes no bit (`drumsynth_test` makes each both ways). A new recipe keeps all of it.
  - **A lane is a recording, the built-in drum or a voice, one at a time**: `setLaneSample` takes a
    voice off (a step of undo with the name since G04: B on SOUND no longer loses a tuned voice),
    `setLaneSynth` empties the name; another model
    puts START and END back to the whole (EDIT's rule), a knob turned keeps them. A voice is a step of
    undo (`soundMark` / `restoreSounds`, which puts the sound back first, as `putKit`, then the step's EDIT).
  - In a song file `lanesynth LANE WORD v1..v8` (the model by its word, the dials in place order),
    **left out when the lane has none**; a word nobody knows skips the line, a number that will not read
    ends it (the rest the model's usual), each held as a double first; of a `sample` and a `lanesynth`
    line for one lane the LATER wins. Model numbers and places are appended, never reordered.
  - **The KIT page**: hold A on SOUND, from the built-in drum UP the recordings (as before) and DOWN
    the twelve models (each at its usual); a synth lane reads its model and SYNTH. **A tap of A on a synth lane opens
    its VOICE view** (`KitScreen::voicing()`, the EDIT view's layout: MODEL on top, the eight places two
    rows of four, an unused one an empty box the cursor passes by): hold A turns (the stick by the
    square of its push), B default / back, hold B every knob to the usual, Y ROLL (`kRollLow` 25 to
    `kRollHigh` 75, by a hash of the voice and a count), L1 / R1 the synth lane before / after, X hears.
    **A turned box (VOICE and EDIT views) is heard ONCE, when the turn is done** (owner, 2026-11-17: heard at
    every step it "sounds odd and you can't really hear what has changed"; `KitScreen::hearWhenDone`): A let go,
    the D-pad and stick at rest, `kHearAfterMs` (300) since the last change, and the sound made (not MAKING).
    Stills `kit-synth`, `kit-voice`, `-held`, `-deleting`, `-clap`; `textsize`, `idle_test` have it.
  - Tests: `tests/drumsynth_test.cpp` (pitch, sweep, decay, every knob, peak, the end, the bits, the
    cost with `--measure`; `--print` remakes the sums only from code known right) and
    `tests/drumsynthapp_test.cpp`.
  - **Named voices** (batch 3; `app/drum_voices.hpp`, data `app/presets/drumvoices.inc`): CLASSIC (the
    usual) then 5 to 8 a model, picked on MODEL with hold A + LEFT / RIGHT, named in brass while the dials
    are its (`voicePlaceOf`). **A lane keeps its dials, never a voice's name or place, so the list is not
    append-only.** `testTheNamedVoices` fails a line the reader cannot take, a voice that is the usual or
    another's, and two voices of a model alike in length, brightness and crest: a new voice must sound
    unlike the others by those numbers.
  - **`readyLane` passes a lane whose voice is being made** (`LaneSound::making`): it has no source yet,
    and read again every frame (the designer page, the VOICE view) its job was never let land.
- **The list of recordings is three columns** (owner, 2026-11-14; `app/sample_picker.hpp`,
  plan `docs/plans/picker-plan.md`, batches 0 to 2 built): the folder this one is in, this
  folder with the cursor, and what is under the cursor. **The columns stand still** (owner,
  2026-11-15: the preview "already opened"): on SAMPLES (folders there) the cursor is in the FIRST
  column, the folder under it in the second and the preview's box, empty, in the third
  (`cursorFirst`, `cursorX`, `drawPreview`), so going in moves only the cursor; inside a folder
  the cursor's column is the middle one; with no folders (`flat()`) the one list is the middle
  column. `sample_test` `testTheColumnsStandStill` reads it off the drawn screen.
  **What the list plays is a `SamplePicker::Hear`** (owner, 2026-11-15): a recording arrived on with
  the song stopped is a PREVIEW, half of what it plays (the sampler: START to END at middle C; the
  kit: the lane's sound, fitted or not), `Session::kAuditionMs` at least, stopped the moment the
  cursor moves on to anything that puts nothing on (a folder, out, B: `quiet()`; a recording put
  on takes the lane or the key over itself). **X while it plays is REST: on to its end, not begun
  again** (`Session::hearFor` moves its stop); X with nothing playing is held, as everywhere.
  "Playing" is `Session::hearingOn(screen)` and the screen's line still running (a sound that
  ended by itself before its stop is not playing). The built-in drum is heard whole, as a tap of
  it always is. `Session::stopHeard` stops only what that screen began. `hear_test`
  (`testThePages`, `testTheSampler`, `testThePreviewStopsWhenTheCursorMovesOn`).
  **A visit to the list is one step of undo, or none** (G04, 2026-11-25: `Session::browsingRecordings`;
  songs-and-undo.md): what it puts on as the cursor goes is not twenty steps.
  `rowsAt(depth, folder)` is the one place a column's rows are made (0 SAMPLES: the first row,
  UNSORTED if anything is loose, the folders; with no folders at all, the recordings
  themselves: `flat()`). **A D-pad press puts the recording on at once; rows going by faster
  than one every `kAtOnceMs` (the stick, a D-pad held past `kFasterAfterMs`) do not: the one
  rested on does** (each is a file read from the card). A, X and B go by what is SHOWING or
  what was on (`on_`), never by the cursor alone. A folder puts nothing on. Deleting a
  folder's last recording leaves the folder (an empty folder is deleted by itself: batch 2).
  Its places are constants in the header; `sample_test` (`testFoldersInTheSamplePicker`,
  `testThePickersPace`) and `textsize_test` (every place it can be, the hints at 0 over).
  **The still `picker-deleting` holds B over a real file**: main.cpp takes its picture at
  600 ms, before the second is up (`picker-folder-deleting` the same, over a real FOLDER).
  - **Where holding B deletes, its let-go is a tap only when it is quick and alone** (gesture audit
    G42, 2026-11-25: `SamplePicker::kTapMs` 250, timed by the presses' own times, `whenPressed`;
    `bHolds_`). Before, letting go of a delete before the bar was full counted as a tap: the list
    closed and the old recording went back on, against "let go early and nothing happens" (hold B's
    rule on every screen). The bar starts the moment B goes down on a recording, so "a B that started a
    bar is no tap" would have taken away the tap that leaves the list from the row the cursor is nearly
    always on: hence a time. On a recording or a folder (a bar fills, or a folder not empty says why
    not), a let-go after kTapMs does nothing, and so does one after another button went down meanwhile
    (it called the bar off: the house's "a tap acts on its let-go, if nothing else was pressed", as
    L1's on the grids). kHoldSaysMs is longer than kTapMs, so a folder's refusal is covered. **Where
    holding B does nothing (the first row, + NEW FOLDER, an empty folder's no row) B is still the tap
    however it is let go**, as `sample_test` held it before: no delete can be called off there, and B
    there only ever goes back.
  - **With nothing at all on the card, Y catches and R1 records** (G42): the empty list's words said
    "catch a bar with Y, or record with R1", but Y did nothing there and R1 paged. `Jobs::catchBar` and
    `Jobs::record` are the opening screen's own Y and R1 (`catchBar()` / `record()` on the kit, the
    sampler and CLOUD); the list closes as B's tap does first (`putBack`), so what the screen says next
    is seen. "Nothing" is no recording and no folder, exactly when the words are drawn; with anything
    on the card Y and R1 are as they were. The bar says Y catch and R1 record there in place of the page.
    `picker_test` `testBIsATapAndTheEmptyList`, `testTheEmptyListCatchesAndRecords` (the kit's, the sampler's
    and CLOUD's lists: the bar and what each screen says, review of B4).
  - **It changes the card too** (batch 2, 2026-11-15): `Session::moveSample`,
    `makeSampleFolder`, `deleteSampleFolder`, each a `Session::FileJob` (Done, Same, TooLong,
    Taken, NotEmpty, Failed), reached through the list's `Jobs`. The list never touches a file.
  - **Hold R1 on a recording carries it** (`kCarryHoldMs` 300, or at once with a direction or
    the stick). So **R1 on a recording pages when it is LET GO**; on any other row at once.
    While carried nothing goes on the lane (`arrive` returns), A, X and Y do nothing, and the
    top line says CARRYING TO place: name (`carryTo()`: the folder under the cursor, else the
    folder showing; SAMPLES itself is "out of every folder", which is how a recording leaves
    when UNSORTED is not listed). B with R1 held calls it off. A let-go the list never saw
    (`update`: R1 not held) calls it off too: nothing moves without a let-go that was seen.
  - **A move is `moveSample`**: the file renamed with the ending it had (written again and the
    old one taken away where the card will not rename, as the page does:
    `Transfer::renameBroken` is the tests' such card), then `recordingChanged` (the name only),
    `saveNow`, `followLater` (next rule). Nothing is written over, capitals not counted; the folder
    must be there, as itself (no link); the whole name must fit `kSampleNameRoom`. After it the
    list's `original_` and `on_` follow, so B's tap is still "as it was".
  - **Where the card will not rename, the copy runs on a thread of its own** (`Session::CarryJob`,
    review 2026-11-23: a large recording held the screen for the whole copy). `moveSample` answers
    `Copying` and changes nothing yet; `Session::update` (`finishCarry`) takes the copy's end and does
    what a rename does, `carryResult()` says how it went, and the list says MOVING TO until then and
    follows it after (`SamplePicker::moved`). One copy at a time: another carry meanwhile is `Busy`.
    The Session's end lets a copy finish and follows it. `picker_test` `testCarryingTheLongWay`.
  - **Typing a name: A types, Y deletes the last letter, B keeps what is typed and leaves, START
    keeps it too** (owner, 2026-11-15: "I find myself pressing B to back out of the track naming"; it
    was B delete, Y or START keep). With nothing typed B leaves and changes nothing (the track's name
    stays, no folder is made), and START gives a track its instrument's own name. A folder
    refused (taken, too long) keeps the keys up, whichever of B or START asked. The still `typing`
    showed the SET page, not the keys, until then: the demo's START kept the name.
  - **Y held deletes on, at a list's pace, wherever a name or a password is typed** (gesture audit G48,
    2026-11-25, under the owner's "any of my decisions can be reversed if there is a more logical
    method", 2026-11-24). It was "Y does not repeat (B never did)", written in the rule above; that
    recorded how the old key behaved, the owner's ask was about B. An old name opens typed (SET, a
    SONG head, R1 on an own preset), so clearing it cost a Y a letter: ten for a track, up to 24 for
    a preset, 63 for a password. Held, the first letter goes at once and the rest one each
    `kListEveryMs` after `kListDelayMs` (200 + 9 x 110 = about 1.2 s for ten), slow enough to let
    go on the letter wanted; a tap is still one. Y is NOT a "hold deletes for good": nothing typed
    is kept until B or START. `NameKeys` (`repeating_` is the held direction or Y) and the PASSWORD
    keys (`SettingsScreen::passwordButton`, its own keys) both. `app_test` `testAHeldYDeletesOn`
    (the track's name and both preset keys), `picker_test` `testMakingAFolder`, `wifi_test`
    `password`.
  - **One set of name keys, `NameKeys` (`app/name_keys.hpp`), in all four places** (gesture audit G01,
    2026-11-24): a track's name (`SettingsScreen`, from SET or a SONG head), a new folder
    (`SamplePicker`), a synth's preset and the sampler's (R1 on PRESET). The preset keys came in on
    2026-11-21 (e7b847d) as two more copies, after the rule above said "a new one does too", and broke
    it: **B left and threw the name away** ("PAD 2" is 29 presses), they ran at a cursor's pace (170 /
    30 ms), and the tabs, SELECT's tap, undo and tempo acted under them. The keys own what is typed,
    the cursor, the stick and **a held D-pad at a list's pace (`ui::kList...`)**, and draw the box, the
    grid and the bar; `press()` answers `Keep` (B) or `Start`, and the screen does the keeping and
    closes them. A preset typed: B or START saves it; under a name of the player's own already it is
    **REPLACED** (said after, and the bar says B replace before; SAVED AS for a new one: START replaced
    without a word before). **R1 on one of the player's own opens with its name typed in**, so a sound
    turned since is kept in two presses (R1, B). The sampler's PRESET is worked out from the sound
    (`samplerPresetOf`), so a sampler sound turned since shows "-" and R1 opens empty there: only a
    synth (whose `preset` is kept) gets the two presses after a turn. START is not said on the
    preset keys (it does what B does) and still does nothing with nothing typed. **While the name typed is
    taken the title says B REPLACES THE PRESET: CANNOT BE UNDONE** (review of A3, 2026-11-25:
    `kPresetReplacingWords`; songs-and-undo.md).
    `app_test` `testSavePresetOnDevice`, `testSamplerPresetsOnDevice`,
    `testTheNameKeysRunAtAListsPace`, `testDeletingAPresetSaysItCannotBeUndone` (the bar's words);
    `textsize` holds each at its longest (24 W's), SAVED AS and REPLACED too (the sampler's said line
    is held at LARGE where a long name runs past the box at XL).
  - **While a name is typed every button is the keys'** (`App::typingAName`, it was `typingAFolder`):
    the kit's, the sampler's and CLOUD's list of recordings, a synth's and the sampler's preset keys.
    The tabs, SELECT's tap, SELECT + anything, L3 and the help card (`busy`) do nothing there; SELECT +
    B is the keys' B. Only the four shoulders' panic still acts (it is before the keys in
    `App::onButton`), and the keys are still up after it. SET's keys are SET's own case there (the
    button test with them). `app_test` `testThePresetKeysHaveEveryButton`. **Keys that R1 opened on the
    way to a panic are closed by it** (review of A3, 2026-11-25; `App::typingBeforeShoulders_`, noted at
    the first shoulder as the copies and a recording are, then `closePresetKeys` on every synth screen
    and the sampler, since R2 first may have turned to DEVICE and the panic turned back): R1 first on
    PRESET opened them, on one of the player's own with its name typed in, and after the panic the bar
    said B replace, so the second B wrote over the file (the first put the notice away). Nothing is
    kept; keys up before the panic stay up. `app_test` `testAPanicClosesTheKeysItOpened`. The bar
    offers no help card there (`screens.md`).
  - **+ NEW FOLDER is a row** (`Kind::NewFolder`: the last of SAMPLES and of a first-level
    folder; not in UNSORTED, not two levels down; only when `Jobs::makeFolder` is given). A
    opens the name keys (`NameKeys`, the one set: above), twelve letters, made in SMALL letters,
    a space at the end dropped. **While typing every button is the list's, START too**
    (`picker_.typing()` in the kit's and the sampler's `onButton`); the still `picker-typing`
    is therefore not given the demo's START, which would make the folder.
  - **Hold B on a folder deletes it only if it is empty** (`emptyFolder`: no recording under
    it, no folder in it), by `rmdir`, so a folder holding a file that is no recording stays and
    the top line says NOT DELETED: OTHER FILES ARE IN IT.
  - What was done, or why not, is on the top line for `kSaidMs` (`said()`).
  - On a recording the bar says HOLD R1 carry where it said L1 / R1 page: it holds five.
  - `tests/picker_test.cpp` (real files; the saved song and rev byte for byte; the list by
    itself with jobs that count), `textsize_test` (each state drawn, the bar's words).
  - **The review's mend (batch 3, 2026-11-15):**
    - **Big moves.** The stick is `kStickRowsPerSecond` 80 times its setting, by the SQUARE of
      the push (a little push reads each name). A held D-pad has a third pace
      (`kFastestEveryMs` 20 after `kFastestAfterMs` 3.5 s), counted from when each row was due
      (several a frame; a stall makes no burst). (Gesture audit G32, 2026-11-25: its numbers now live
      in `ui.hpp` as the one named exception to the paces, `kRecordings...`, and it counts through
      `ui::Repeat` as every screen does; the picker's names are kept for the tests.) **L1 held pages on, and R1 where it does not
      carry.** 500 rows: the stick 8.3 s, DOWN held 12.6, L1 held 5.5 (each was 33 or more).
    - **Hold R1 carries only where there is a folder to carry to** (`carries()`: not `flat()`).
      Let go where it was: LEFT WHERE IT WAS.
    - **Hold B on a folder that is not empty says so** after `kHoldSaysMs` (NOT DELETED: IT IS
      NOT EMPTY), and letting go is then no tap. A tap still closes.
    - **`moveSample` answers `NotSaved`** when the file moved but the open song could not be
      written (`saveNow` is a bool); a saved song that could not be is found as its turn comes
      (next rule). Either way the list shows it moved and says MOVED, BUT A SONG COULD NOT BE
      SAVED WITH ITS NEW PLACE.
    - **A column's rows are made when the place or the card changes, never per frame**
      (`index()` counts each folder once; `side()` keeps the two columns beside the cursor's;
      `rowsMade()` is the tests' count). Whatever changes `all_` or `dirs_` calls `index()`.
    - **While a folder's name is typed L2 / R2 and MENU's help are the list's too**
      (`App::typingAName`: every name typed, since gesture audit G01).
    - **`wavNames` lists only what `sampleNameOk` takes** (a file of 64 letters or more is not
      offered: resting on it put a cut name on the lane).
    - **On the WiFi page a file in a folder may be as long as a loose one** (60 with its ending;
      the whole still 63): held to 36, a long recording carried into a folder on the handheld
      was gone from the page, which then refused to delete it. The page's script changed with it
      (tried in a browser, 2026-11-15).
    - **The saved songs follow a move a few files a frame** (2026-11-15, our call left by the owner;
      `Session::followLater`, `followSome` in `update()`): up to `kFollowReadsAFrame` (8) read, and
      none after one is written. Measured on the desk, 40 songs with 10 revs each (one of them the
      bench's 32 KB song): every one naming the recording, all at once was 450 to 740 ms (about 2
      to 4 s on the handheld); a few a frame, the let-go is 1 ms and the longest frame 12 to 16 ms
      (one card write), 440 frames. (Since G04 the let-go also has every step of undo follow: 0.6 to 0.9
      ms more with twenty steps in the bench's song, 17 to 20 in a 2.2 MB one, after `renameRecordings`
      learnt to pass a line over at a glance: songs-and-undo.md, measured.md. The 1 ms was not timed again.) None naming it: 2 to 3 ms all at once. `moveSample` answers
      `Moving` and the list says MOVING TO X... until `following()` is false, then MOVED TO X,
      or MOVED, BUT A SONG COULD NOT BE SAVED... (`followFailures` went up). **Safe against a
      switch-off**: before any file is touched what is owed is written (`kOwedFile`,
      renames-owed.txt in the song folder, `writeTextFile`; "job" before each batch, FROM tab TO),
      rewritten as each batch ends and deleted at the last; `useSongFolder` finishes it, in order,
      before any song is opened (`finishOwed`; a file not in its words is let be). Gone through
      twice is no harm: it no longer names the old place. **Whatever reads or renames a saved song
      first finishes what is owed** (`followNow`: openSong, openRev, renameSong, deleteSong); a new
      one of those calls it too. If the owed list cannot be written, it is all done at once.
      The WiFi page's renames take the same way. `picker_test` `testSpreadOverFrames`.
- **A recording's name may carry two folders** (owner, 2026-11-14; it was one):
  `kicks/808`, `drums/kicks/808`. `sampleFolderOf` is ALL of the folder (`drums/kicks`),
  `sampleLabel` the last part; a song file's one-folder name is still a name. The whole name
  fits `kSampleNameRoom` (63 letters), on the WiFi page too (`safeSamplePath` ends in
  `sampleNameOk`; each folder 24, the file 60 with its ending, in a folder or out). `wavNames` lists two levels, `wavFolders`
  the folders (empty ones too). The page's folder import still makes one level. Everything that
  takes a name from outside (a song file, the WiFi page, delete) checks it
  with `sampleNameOk` or the page's `plainName`. Show `sampleLabel(name)`.
- **THROW is a value on a hit, a note or a chord** (`throwTo`: 1 ECHO, 2
  SPACE, 3 both; `kThrowEcho`, `kThrowSpace` in drums.hpp). In a song file it
  is one more number at the end of a line, **left off at 0**, so an unthrown
  song is written as it always was: the seventh of `stepfx`, the sixth of
  `note` (the nudge is then written even at 0), the third of `chordfx` (the
  ratchet is then written even at 1). A chord's THROW is every key's.
- **KIT PRESETS: a stop above lane 1 on the KIT page** (easywins batch 7; `app/kits.cpp`,
  `KitScreen::onKit_`, `Session::kits` / `kitOf` / `putKit` / `loadKit` / `saveKit` / `deleteKit`).
  Hold A + UP / DOWN the kits (B with A held: as it was), Y SAVE as KIT n, hold B deletes one's own.
  **A kit's text is the song file's own kit lines** (`putKit` in song_file.cpp, which `putTrack` calls;
  `kitText`, `readKit` through `parseSong`): a new thing a lane keeps goes in `putKit` and `copyKit`,
  and a kit carries it. **Which kit a track has is worked out, never kept** (`kitOf`: its text against
  the list's), so a song file has no word for it and a changed kit reads YOUR OWN. The built-in kits
  are recipes of DRUM SYNTH's named voices BY NAME (`kRecipes`): a voice renamed or taken out fails
  `easy_test kits` (`builtInKitProblems`). A kit put on goes through the lanes' own setters, so it is a
  step of undo as they are (its recordings too, since G04). A player's own are `kit N.kit` in the preset
  folder. Stills `kit-preset`, `-held`, `-own`. **Nobody has heard the kits.**
- **B with A held puts the sound back wherever hold A steps through sounds** (gesture audit G12,
  2026-11-25; `Session::keepSound` / `putSoundBack`, one function with the KIT stop's, whose own
  `kitBefore_` copy is gone). The KIT stop, a lane's SOUND, the sampler's SOUND and PRESET, a synth's
  FOLDER and PRESET and CLOUD's RECORDING (`instruments.md`) keep the track's whole sound as A goes down;
  B with A still held puts it back, **heard** with the song stopped (the KIT stop too, new), and **A is
  spent** (`holding_` false, as FX's and MOD's call-off: the D-pad and stick are the cursor's again, and
  letting go is no tap, so no list or VOICE view opens; on the kit's other columns B with A held does
  nothing but make the let go no tap, so FIT's tap below does not follow it). Before, B with A held did nothing there; a burst of undo
  ends after 0.7 s, so each sound stopped on was its own step (ten heard: ten undos, past 20 the start
  gone), and a recording or a lane's sound picked is no step of undo at all. **What is kept**
  (`Session::KeptSound`): the track's fields copied whole (`fieldsFrom`, no patterns: one `TrackData` on
  the heap, made once) and `PresetSoundMemory` as it was (files.md). **What is put back**, each only
  where it differs: the kit's lanes (`putKit`, only when `kitText` differs: sound, EDIT, FIT, one slice,
  level, FEEL, SWING, CHOKE), the recording (`setTrackSample`), the sampler's settings and CHOP, the
  preset, settings (`settingCount`) and macros (`sendSynthPatch`, as undo puts a synth back), the track
  sound (`putTrackSound`: effects, sends, GLIDE FROM, MODs), and the memory last, so a plain preset
  picked next puts back what it would have without the trip. **The effects' automation and remembered
  TYPEs too** (review of A4, 2026-11-25; `KeptSound::lanes` and `types`, `putEffectLanesBack`): they are
  in the patterns and the slot memory, not the fields, and a whole preset passed over that put another
  effect in a slot cleared its lanes in every scene and forgot its TYPEs (`setEffect`), so the slot came
  back bare though the manual said "as they were". Each slot holding its kept effect again gets its lanes
  as kept (the steps no more, no less) and its TYPEs; when nothing differs (most often) nothing is done,
  else written into the song and the track sent once, whole (`sendTrackImage`, as FX's line move: a
  command a step could fill the engine's queue). **A stick still pushed then waits for the middle** on
  all three screens (`stickWait_`, as a MOD let go): it was stepping the sounds, and the next frame moved
  the cursor off the stop (the synth's at once, the kit's and the sampler's a moment later), where a held
  D-pad was already stopped. Not the mixer, the name, mute or solo. One kept at a time
  (one A), in memory only; forgotten by `removeTrack`, `duplicateTrack` and `applyProject` (a number
  then names another track) and never put on another instrument. The put-back is a change like any
  other: a step of undo (a recording, as ever, is not). **The mixer's B with A held is not this** (owner,
  2026-11-14, song-screen.md): it is 0 dB and the middle, a reset, and stays so. `sample_test`
  `testBWithAHeldPutsTheSoundBack` (also: B with A held on FIT with no step first is no tap, the lanes and
  TYPEs, the stick's wait), `testAKeptSoundIsForgotten`; `easy_test` `testTheKitStop`; `app_test`
  `testAPutBackSoundIsTheEngines` (the engine plays the sound put back, to the sample).
- **A tap of A on FIT sets the bars the recording fills** (gesture audit G45, 2026-11-25;
  `laneFitFor`, `KitScreen::fitToItsBars`): FIT started at OFF, so the usual 1 BAR was the fifth click.
  The length is what the lane plays unfitted (`LaneSound::seconds`: the cut, turned round and pitched
  shape, which is what FIT stretches; the review's note said `sourceSeconds`, the whole file, which a
  trimmed loop would get wrong), the steps it fills at the tempo (a step is 15 / tempo seconds),
  rounded to FIT's stops **in the scene's meter**: 1, 2, 4 or 8 steps (those under a bar), or 1, 2 or
  4 bars of `meterOf` (12 a bar in 3/4, as CATCH sets; FIT's most is 4 bars, so not 8 as first proposed). The
  nearest by ratio, since a stretch is one: 24 steps in 4/4 are 2 bars, not 1. **Nothing on a
  built-in drum, a DRUM SYNTH voice or a file not found** (a hit, not a loop; `fitsByTap`), where the
  bar keeps HOLD A fit to steps; on a recording it says A fit to its bars. Hold A + up / down stays
  exact, **from a count between FIT's stops too** (review of A4, 2026-11-25: `stepFit` took 24 as the stop
  below it, 16, so DOWN went to 8 and 24 and 48 could not be stepped back to; now DOWN lands on the stop
  below, UP on the one above; CATCH's 12 in 3/4 had the same fault). One step of undo, as FIT is (`laneFit` is in `soundMark`). **Left as it was**: FIT's box reads
  16-step bars (`laneFitText`), so a tap's 48 steps in 3/4 (four of its bars) reads 3 BARS and 12 reads
  12 STEPS, as CATCH's 12 always has; a question for the owner, not changed here. `sample_test`
  `testATapOnFitFitsItsBars`.
- **Hold B deletes one of the player's own presets the same way everywhere** (gesture audit G10, G30,
  2026-11-24): a kit (`kKitDeletingWords`), a sampler preset (`SamplerScreen::kPresetDeletingWords`:
  it said nothing, a 2-pixel line only, though the file is gone for good) and a synth's
  (`SynthScreen::kPresetDeletingWords`, new: no way but the SD card before; the WiFi page's folders hold
  no presets). A second, a bar that fills, and the bar at the bottom says **CANNOT BE UNDONE** while
  it does; any other button, A held or the cursor off PRESET calls it off (on a synth, another track
  of it up too: two tracks of one synth share the screen). The track keeps its sound;
  with an own preset on PRESET the bar's START yields to HOLD B delete this preset (PHYSIC's dice made
  it 32 pixels over). A synth's: `Session::deleteSynthPreset` (the file by `UserPreset::file`, as
  read), then every track of that synth past it a place earlier, and a track on it named by the
  preset before (its sound its own now: `presetSounds_` reset, as `savePreset` does); not a step of
  undo. Undo's older steps still hold the old numbers (as after a `savePreset` that sorts in before
  others): a step taken back after a delete can name the next preset. `app_test`
  `testDeletingOwnSynthPresets`. **A tab turned with B still held calls it off** (review of A3,
  2026-11-25; each screen's `callOffDelete`, from `App::update` for every screen not shown, beside SONG's):
  L2 / R2 and SELECT's tap never reach the screen, and it is not updated while away, so back with B still
  held the bar's clock had run and the file went at once, the bar never seen. The same for the kit's
  EDIT and VOICE holds and a list of recordings' (`SamplePicker::callOffDelete`). `app_test`
  `testATabCallsOffAHoldOfB`, `sample_test` `testATabCallsOffARecordingsDelete`.
