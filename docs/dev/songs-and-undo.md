# The song in memory, undo, and scenes

Project and scene pieces, undo, the song's text, scene moves and NEXT SCENE.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **`Project` is 58 KB and a `TrackData` 4.8 KB, plus the scenes' pieces on
  the heap** (2026-11-17, scenes-plan.md: they were 12 MB and 1 MB with
  sixteen scenes in fixed arrays). The house way still holds:
  `std::unique_ptr<Project>`, and a blank track copied from one kept on the
  heap (`parseSong` runs on the WiFi server's thread too). `save_test` reads a
  song on a thread with a 256 KB stack.
- **A track's scenes are pieces made when something is written there**
  (`SceneCells` in project.hpp; `kMaxScenes` 64, scenes-plan.md): `drums`,
  `notes` and `automation` each a pointer a scene, read as `t.drums[s][bar]`
  as an array was. **A scene with no piece reads as the one blank piece; a
  NON-CONST track makes the piece even to read it**, so whatever goes through
  every scene takes the track `const` or asks `has(s)` first (`sceneEmpty`,
  `sceneCount`, the song's text and its cache, `clearAutomation`,
  `changeEffectType`'s lanes: `testAMoveMakesNothingNew`, `testScenesPastSixteen`).
  A drum track makes no notes pieces, a synth track no drums pieces. A piece
  made and emptied stays (memory only, and what holds a bar of it still holds
  it: an assignment copies into a piece that is there, never frees it).
  **`TrackData` is `TrackFields` (one block of bytes, copied and compared so:
  `fieldsFrom`, `sameFields`) and the three `SceneCells`**, whose pieces are
  compared and copied a scene at a time (`samePatternsAs`, `SceneCells::same`,
  `sameAs`). A new field goes in `TrackFields`; never `memcpy` a whole
  `TrackData`. A scene move moves pieces (`take`, `drop`), copying nothing.
- **Undo is a text snapshot of the song** (`songText`), one step per burst
  of edits, 20 deep. Anything that changes a pattern outside the grids calls
  `noteEdit()`. **It puts back the patterns and, since 2026-11-14 (owner),
  each track's effects, sends, sound (preset, settings, macros, a sampler's
  settings; since 2026-11-25 a recording too, next rule), GLIDE FROM and its two MODs, the master's effects and the buses'
  (LEVEL with them)**. Those are seen changing by `Session::soundMark()` each frame, as the
  cell is by `cellMark()`: no screen calls anything (the FX screen is
  untouched; a change is a step from the frame after it). `restoreSounds` puts them back only where they differ
  **as the song file writes them** (`sameAsText`, `effectSameAsText`: `%.6g`;
  a third reads back as 0.333332986, and compared as numbers every undo
  rounded such a setting, send or macro and told the engine, a synth's whole
  patch, the master and the buses on every undo; `testUndoLeavesWhatTheTextRounds`),
  through the engine's usual commands (`restoreEffect`: another kind is
  `Engine::setEffect` then its settings; the same kind, only the settings
  that differ; `SetSend`, `setGlideFrom`, `sendSynthPatch`), never a track
  image and never an `Effect` from this thread. Automation is a pattern, so a
  TYPE change taken back has its lanes back with it. Tempo, mutes, names and
  track deletes are not undone. After a restore the marks are taken again
  (putting back is not an edit).
  **Only another song clears undo** (`applyProject`). A resync after a full
  queue, a track deleted and a track given another instrument send the song
  with `sendSong()` and keep the steps: a delete takes its track out of every
  step's text (`dropTrackText`), and another instrument is passed by (its kind
  differs). The delete itself is still not undone.
- **Words follow undo** (gesture audit G10, 2026-11-24): CANNOT BE UNDONE only where undo cannot bring
  it back (a file: a song, a kit, a preset, a recording; a track; a network; the update). What undo
  brings back says what is happening: FX's hold B **DELETING THIS EFFECT** (it said DELETING: CANNOT BE
  UNDONE, written at 01:57 on 2026-11-14, three and a half hours before effects went into undo; the
  MOD screen's DELETING THIS MOD is the house wording), REPEATS' **HOLD B empty** (it empties the
  scene, a step of undo; it said delete), and the help card's **UNDO the last change (20 deep)** (it
  said "to the patterns"). `app_test` `testHoldBDeletesAnEffect` takes the deleted effect back with SEL
  + B. **Replacing one of the player's own presets says it where it is confirmed** (review of A3,
  2026-11-25): while the name typed is taken, the name keys' title is `kPresetReplacingWords`, **B
  REPLACES THE PRESET: CANNOT BE UNDONE**, in brass (the colour of SONG's words for what goes), on a
  synth's and the sampler's keys; it said SAVE AS PRESET and only the bar's B replace told, though R1
  on one's own opens with its name typed in, so one B writes over the file. `app_test`
  `testDeletingAPresetSaysItCannotBeUndone`, `textsize` (whole at XL). `ctest -R words` fails a text that says erase, remove or rub out (the word is delete; a cell
  is emptied), and since C4-docs (2026-11-25) the same words anywhere in the manual or README.md (the
  roll's chord said "B to remove it"; the three are now "delete"), with plants of its own.
- **A recording is a step of undo** (gesture audit G04, 2026-11-25): a drum lane's (with its FIT, cut
  and voice), a sampler's and a CLOUD's. So CATCH, B on SOUND, a pick, a take (R1) and a kit or preset
  put on are put back by SEL + B. Before, this file said "Tempo, mutes, names, **the kit's recordings**
  and track deletes are not undone", and `setLaneSample` took the mark again so that "undo would
  [not] put the old cut on the new file"; neither carried an owner mark. It left one tap of Y or B
  able to lose a cut recording or a tuned DRUM SYNTH voice, with nothing on the screen saying so, and
  only half undone: a CATCH changes FIT, which the mark saw, so SEL + B put the old FIT on the caught
  bar. **More logical: the name, FIT, cut and voice are one step**, so the old sound comes back with
  its own cut, which answers the old reason. How:
  - `soundMark` mixes each lane's `laneSample` and a sampler's or CLOUD's `sample` **to its end**
    (`strlen + 1`: what lies past a name is what a longer one left there). `setLaneSample` no longer
    takes the mark again.
  - `restoreSounds` puts a lane's sound back as `putKit` does (a voice through `setLaneSynth`, else a
    name or the drum through `setLaneSample`), then its EDIT, LEVEL, FIT and ONE SLICE; a sampler's or
    CLOUD's name through `setTrackSample`.
  - **A step keeps only the NAME, as the song file does: no sample data is copied or kept per step.**
    Putting it back reads the file again, as a pick does (the app thread; the old sound goes to the
    engine's trash as any). A file deleted since is missing, as in a song opened. Undo's text is no
    longer: the song's text named the recordings already. Keeping each step's sound read (a shared
    pointer) would spare the read but hold up to twenty recordings in memory: not done. **A restore's
    read is not timed on the handheld.**
  - **A rename, a move or a carry (`recordingChanged`) and a recording found elsewhere (`findMissing`)
    are no step, and the steps follow them** (`undoFollows`: every undo and redo step and `settled_`,
    through `renameRecordings`, as the saved songs; a plain search first). Without it a rename was a
    step, and every step before it put back a name whose file had gone. **What that costs** (review of
    G04, 2026-11-25): up to 41 whole songs' texts gone through on the frame of the rename, which nearly
    always all name it. `renameRecordings` now passes a line over at a glance unless its first word is
    `track`, `sample` or `sound` (`namesARecording`: it made a stream for every line), which the saved
    songs' following (`followFile`) gains too; the WiFi page's changes of one frame are one
    `undoFollows` (`recordingChanged(..., false)`, in order), not one each. A rename with twenty steps
    that name it: 0.6 to 0.9 ms in the bench's song (33 KB), 17 to 20 ms in the largest (64 scenes,
    every lane of automation: 2.2 MB); 3.4 to 4.0 and 85 to 99 ms before (`bench --app`, measured.md).
    **Not timed on the handheld** (13 to 20 times slower: about 8 to 18 ms, and a quarter to two fifths
    of a second in the largest song, a frame or more it stops the screen for).
  - **Deleting a recording (hold B in the list) is no step either** (`deleteSample` takes the mark
    again): it is for good, and undo would only put back a missing name. A step from before that
    names it finds it missing.
  - **A visit to the list of recordings is ONE step, or none** (`Session::browsingRecordings`, told by
    the App each frame before `update`): the list puts each recording on as the cursor comes to it,
    and resting on one for 0.7 s would otherwise end a burst, so one browse made a step per recording
    rested on and twenty of them pushed every older step out. While a list is open on the screen
    shown, the sound's mark is not compared and a burst does not end; when it closes, A's pick is one step
    from before it opened, and B (what was there put back) leaves the mark as it was: no step, and
    the redo steps stay. **B puts a lane's cut back too** (review of G04, 2026-11-25: the kit's
    `SamplePicker::Jobs::putBack`, kept from when the list opened): every recording put on gives the lane
    the whole (`setLaneSample`), the one it had as well when the cursor comes back to it, so B put back
    only the name, the cut was lost, and the mark differed: a step, the redo steps gone, against these
    words. A recording deleted in the list meanwhile takes its cut with it (the drum goes back whole). No
    DRUM SYNTH voice to put back: on a voice lane A's tap opens its knobs, never the list. The sampler's
    and CLOUD's settings are not touched by a pick, so their B stays `use(original)`. A burst the list was opened in goes on, so the pick joins it. Hold A + UP /
    DOWN on CLOUD's SOURCE (no list) is a step per rest, as a knob turned.
  - Tests: `sample_test` `testUndoPutsBackARecording` (CATCH on a lane and on a CLOUD, B on SOUND on
    a lane, a voice and a sampler, renames, the WiFi page's two in one frame, a move with a redo step
    waiting, a find, a delete, a visit to the list: A, B, opened in a burst, a carry while open, B on a
    lane with a cut and on the built-in drum cut, its recording deleted meanwhile), `transfer_test`
    `testRenamingInSongText` (the lines passed over at a glance) and
    `laneedit_test` `testUndoAndANewRecording` (the old sound and its cut, heard as by hand).
    Twenty mutants, all killed. The idle frame with the names in the mark: 0.032 to 0.034 ms
    (`bench --app` on a busy desk, not side by side with the old). **Not run on the handheld; nobody
    has heard it.**
- **The undo marks (`cellMark`, `soundMark`) are 64 bits, mixed eight bytes at a time** (`markMix`):
  only ever compared with one made the same way, in memory. A byte at a time, the idle frame was 0.048 ms
  on the desk; now 0.014 (`bench --app`, 2026-11-23).
- **last.txt is written only when the song open is another** (`Session::rememberLast`): it was written,
  two fsyncs, with every autosave. A song or rev opened, a rename and a new song's first save write it.
- **Moving an effect a slot swaps the lanes in the song and sends the track's image once**
  (`moveEffectSlot`): a command a step filled the queue on a busy track, and the song was sent again.
- **The song's text is made a track at a time** (`SongTextCache`,
  `Session::textNow()`): a track whose text would be as it was when its text
  was made is not written again. **It is compared by what the text reads**
  (`sameForText`, `TextUsed`): everything outside the patterns byte for byte;
  a bar or a lane of automation that had something in it byte for byte; one
  that had nothing only looked at, one side, to see it still has nothing
  (first by one `memcmp` against an empty one: `levelsEmpty`, `notesEmpty`,
  `laneEmpty`). Exact, because `putTrack` writes nothing for such a piece. The
  autosave, the end of a burst of edits and undo all use it, so with nothing
  changed the autosave is a compare of memory. The copies it keeps cost what
  the song's tracks hold (a scene with no piece is passed over at once). **An undo reads back only the tracks
  whose text differs from the step's, and builds no `Project`** (speed plan
  batch 4: `Session::readStep` into a `StepRead`: the song's own lines, and
  the tracks read into `TrackData` made once and kept (`makeBlank` lets their
  pieces go: the step's text makes those it writes); `SongReader` reads a part at a time as `parseSong` reads the whole,
  `cutSongAt` gives the parts' places). The text of each track now is what
  undo or redo just made (`patternsNow`), not compared again: **only they call
  `restorePatterns`**. A scene move reads the step whole (it takes the
  last scene from it). `readUndoWhole(true)` reads every step whole:
  `app_test` (`testUndoIsAsItWasReadWhole`) holds the two the same, and
  `save_test` (`testASongReadInPartsIsAsReadWhole`) the reader. **A track whose
  numbers differ from the step's but whose text is the same is left alone**
  (`patternsSameAsText`: of one kind, only the patterns' own lines,
  `patternText`): a value the text rounds (a keyboard's 90/127) made one undo
  send all twelve tracks whole. `app_test --undo` runs the undo tests alone.
  Do not give the cache a count of changes instead: the screens write into the
  project in many places, and one that forgot to count is work never saved.
- **The song's text is written and read without snprintf or streams** (speed
  plan batch 4): `appendNumber` writes `%.0f`, `%.4g` and `%.6g` to the byte
  as snprintf does (exact 128-bit arithmetic; snprintf where it would not
  fit), and `LineIn` (`app/line_in.hpp`) reads a line as a `std::istringstream`
  of it does, failures included (num_get's gathering, then strtod or its exact
  fast path). `save_test` holds both to the library over millions of values
  and lines (every float was checked once, outside the tests). **`parseSong`
  reads in place** (`Project` is a `ProjectHead` and the tracks; a track is
  made blank, `makeBlank`), so it is never handed a second Project. A new format in the song file goes through `put`'s three, or
  snprintf; a new way to read goes through `LineIn`'s operations.
  **One difference is on purpose** (audit #9, 2026-11-21): past the doubles' end (`1e999`) the stream
  fails, `LineIn` gives the same largest double WITHOUT failing, so the line reads on and the value is
  held to its limit (`testPastDoubleEndNumbersAreHeld`). `testALineReadsAsAStreamReadsIt` allows that
  and nothing else.
- **Scenes are numbered by their place, so a scene put in moves the ones
  after it** (NEXT SCENE, SELECT + Y: `Session::nextScene`). Whatever holds a
  scene's number must follow: the engine's playing, its patterns and an
  arpeggio that is playing (`Command::ScenesMoved`, next rule),
  `project.playScene`, a keyboard note being recorded and the scene copy's
  place (`Session::scenesMoved` does those three and counts the move), the
  SONG screen's cursor, copied cell and a paste waiting for R1's let go
  (`SongScreen::scenesMoved`, called by the App; G23 review, 2026-11-25), and
  the player (`UndoStep::nextScene` is how undo and redo know to move them).
  All of them move by one rule, `movedScene` (`session.hpp`). Its tests:
  `tests/nextscene_test.cpp`; the copies `copyscene_test`
  `testTheCopyFollowsNextScene`.
- **NEXT SCENE (SELECT + Y) and the scene's length (SELECT + L2 / R2) are the App's, on every tab**
  (gesture audit G03, 2026-11-24). They were PATTERN's alone: "The gesture belongs to the grids. On the
  other screens SELECT + Y makes no scene" (`nextscene_test`, commit 211fd8e, 2026-11-12: Claude's
  choice, not the owner's). Off PATTERN the press fell through to the screen's own Y, which does not
  look at SELECT: SONG muted, the kit and the sampler CATCH (a sound replaced outside undo), PHYSIC and
  MOD rolled, and SELECT + L2 / R2 turned the tab. More logical: "SELECT + something is for whole-song
  things" (CLAUDE.md) holds on every tab, as undo, tempo and volume already did, and NEXT SCENE from
  DEVICE, FX or MOD is one combo instead of a trip through the tabs. **No screen is handed a
  SELECT-held Y**, but one: the open song list, where SELECT + Y saves a rev (its Y, as ever). The scene
  is `App::sceneOfSelect()`: the scene being worked on, or on SONG the one under the cursor
  (`SongScreen::sceneUnderCursor`: a cell's row or a REPEATS box; a head, + TRACK and MASTER are off the
  scenes and take the scene being worked on). **`Session::nextScene(scene)`** copies any scene: the player
  goes to the copy on a bar its cell has, the cell worked on until then is checked for an edit first
  (so no step of undo is made by the move itself), and with no room nothing changes, the player
  neither. The unbuilt LIVE tab (`big-features-plan.md`) wants SELECT + L2 / R2 for its QUANTIZE: it
  would be the one exception. Tests: `nextscene_test` `testOnEveryTab`, `app_test` `testASceneOfAnyLength`,
  `save_test` (the rev).
- **A scene move is ONE command, and the audio thread copies nothing**
  (`Engine::moveScenes`, which sends `Command::ScenesMoved`). The engine keeps
  each scene of each track as a piece of its own (`Track::scenes`: kMaxScenes
  pointers to a `SceneStore`; `drumScenes`, `notes`, `bars` and `automation`
  read them as the one run they used to be). The command moves the pointers,
  takes in the one scene that is new for each track (made on the app thread:
  `Session::sceneImage`), sets that scene's repeats and steps and the song's
  length, moves the playing, and hands the scene that fell off the end back
  through the trash. **It is all in one block of sound or none of it is**, so
  there is no order to keep and no case that plays a wrong cell. Sending a
  track's patterns one a frame, and "all at once when it must be", are gone:
  do not bring them back. **Anything new the engine keeps for a scene goes in
  `SceneStore`, or is moved by that command** (`repeats_`, `steps_`).
  Measured with 12 tracks (`nextscene_test --measure`, the desk): before, the
  block after the gesture took 2.1 to 3.8 ms with every track at once (0.5 on
  average and 1.3 at worst with one a frame) against a usual 0.21; now 0.30
  to 0.37 against a usual 0.24, **which is what any block costs straight after
  the program has been busy** (0.30 to 0.35 with nothing sent). The command
  by itself is 0.1 to 0.3 millionths of a second for twelve tracks. (Again
  later on a busy desk: 0.28 to 0.33 against a usual 0.22 to 0.24, and 0.28 to
  0.30 with nothing sent; a worst of several ms there is the machine taking
  the thread away, and shows as often in a block with no move in it.) The
  key itself is the program's thread: 1.3 ms, and 11 to 12 ms for its undo or
  redo (the song read back from text), **not timed on the handheld**.
  (2026-11-13, joined with chord effects, a slower run. The test's song has
  the chord track now, so its figures are not the ones above: a usual block
  0.30 to 0.34, the block that takes the move 0.43 to 0.47. With the song as
  it was: 0.27 to 0.31 and 0.38 to 0.45, against 0.26 to 0.30 and 0.38 to
  0.43 for the branch before the join in the same run. An ordinary block of
  twelve busy tracks, timed apart: 0.57 to 0.63 ms before the join, on `main`
  and joined alike. The step clock's three rows of `bench`, four runs each:
  0.80 to 0.86%, 1.29 to 1.41% and 1.24 to 1.31% joined; 0.77 to 0.86%, 1.39
  to 1.42% and 1.24 to 1.35% on `main`.)
- **`nextscene_test` runs a real audio thread** beside 200 press and undo
  pairs, held to twelve blocks past the last key (a busy machine made minutes
  of sound otherwise). It is clean under the thread sanitizer
  (`-fsanitize=thread`, about nine minutes; sixteen with its build, on a busy
  desk, since the chord track and the helpers' round: clean again 2026-11-13, and once more that day with GLIDE FROM, fifteen minutes; clean 2026-11-14 with the scene move's asking again, nineteen minutes for the run alone on a shared desk; clean 2026-11-15 with the chord track on JAX-EP, eleven minutes for the run alone; the test's counted `new` has the kind that gives back nullptr too, which a sanitizer's own does not pass on); run that again after a change to
  what a scene move hands over.
- **Taking a NEXT SCENE back, or putting it back, sends no track whole**
  (`Session::restorePatterns` with a move: `moveScenesHere`, then
  `tellScenesMoved`). It then asks whether the PATTERNS, written as text, are
  the step's (`Session::patternsAre`), not whether the numbers match:
  automation read back from text is not the same float to the last bit, and a
  track found different for that alone had all its patterns sent (a megabyte
  the audio thread copies). The patterns alone, not `patternsNow()`: that is
  the whole song's text, and a knob turned since the copy is in it. Only if
  they differ (nothing in the program does that without a step of undo of its
  own) is a track put back and sent whole, as an ordinary undo does.
  `Session::imagesSent()` is how the tests see that none went.
- **Where a scene move cannot be "the song played straight through"**: the
  copy is playing in its own turn (the scene it was made from has had its
  plays this time round) and is taken back. The playing goes to the scene it
  was copied from, same step, bar and time round, so that scene is heard for
  longer than its plays. Nothing is cut and no other scene's cell is played.
  Everywhere else the tests hold the sound to 0 difference: beside the song
  left alone while one scene goes round, and beside a song built with the
  copy in it while the song plays through (press, undo, redo; also with a
  later scene playing, and with a real audio thread making blocks back to back).
- **A copy taken back during the LAST STEP of the scene it was copied from
  is mended in the engine** (found in review 2026-11-13, mended 2026-11-14).
  That step has asked a step ahead for the early notes, hits and chords of the
  bar that follows, which was the copy. Everything asked ahead is marked
  (`ahead` on a waiting note, a drum hit waiting, an arpeggio, a PAIRED
  `Coming`; `asking_` while it is asked), and each track keeps where it was
  asked from (`askedFrom`, `askedBar`) and what the asking changed
  (`strokesBefore`, `lastChordBefore`, `chordAskedBefore`). `ScenesMoved` ends
  with `askAgain`: where another scene's bar follows now, the marked things
  are dropped, those three put back, and the step asked again from the new
  bar (`askAheadHits`, `askAheadNotes`, the same code `fireStep` asks with),
  as far into the step as the playing is. A new step clears the marks first.
  **One thing it cannot mend**: an early note of the copy already heard
  (here from 61.7 of the step on: 0.11 from the song left alone with the chord track on JAX-EP; it was 0.28 on PHAZED),
  and the next scene's notes whose moment has gone by are not played late.
  `nextscene_test` `testTakenBackOnTheLastStep` (in ctest) holds 0 up to there
  for `fill` and for a song with more asked ahead (`addEdges`);
  `nextscene_test --edge` prints the figures. **One line no test reaches**
  (its mutant survives): `GlideTrail::aheadIsNow` in `fireStep`, clearing the
  PAIRED marks at a new step. Kept: a mark left on would let a move forget a
  moment's own keys, but the keys begin before any note of their moment can
  ask, so no song found shows it.
- **What the program remembers it sent is set only when the send went**
  (`Session::syncSong`: `sentSceneCount_`). Set before, a scene move lost to a
  full queue left the engine with the old song length for good: the song sent
  again found nothing to send. `nextscene_test` fills the queue to hold it.
- **A scene nobody has written is the engine's ONE blank store, never written**
  (`Engine::blankScene`, scenes-plan.md batch 1). **Every audio-thread write into a
  scene goes through `writable(c)`** and does nothing on the blank one; **`Engine::send`
  (the app thread) hands a scene its store first** (`Command::GiveScene`, appended) for
  any write that leaves something (`leavesBlank` says which do not), both or neither
  (`SpscQueue::hasRoom`). It keeps which scenes it has handed over in `given_` (a bit a
  scene: so `kMaxScenes` <= 64), moved by `moveScenes` as the command moves the stores
  (`moveGiven`) and cleared with a track taken away. **`SetTrackImage` carries stores made
  on the app thread** (`SceneSet`, made in `send` from the `TrackImage`, which it frees on
  success): the audio thread swaps pointers and hands the old ones back in the same holder
  (it copied 0.8 MB a track before). **`RemoveInstrument` brings a holder** (from `send`,
  when the track has stores) that `takeAway` hands the track's stores back in: one thing in
  the trash. `TrackImage` is a `SceneImage` for each scene that holds something (nullptr:
  nothing; read and written by slot as before, a slot written makes its scene), and
  `Session::sceneImage` is nullptr for a scene with nothing and a cell of one bar. A test
  that wants the engine's count reads `Engine::scenesGiven(track)`; `blankStillBlank()`
  says the blank one was never written (`engine_test` `testScenesHaveStoresWhenWritten`).
- **`sceneIn` (engine.cpp) is the one place a pattern is read into the
  engine**, for a whole track (`SetTrackImage`) and for one scene. A new
  field of a note or a hit is copied there, once; `sceneInto` (session.cpp)
  is the one place the app writes a scene out for it. A chord's `play` and a
  glide's length go through both (`glideKind`, `chordPlayChecked`).
  `nextscene_test` hands a scene over with them beside the same notes written
  by commands, and beside the bars the piano roll sends.
- **`sceneIn` holds each note as `SetNote` holds one** (a pitch over 127, a step past the bar, a velocity
  or length that is not a number: silent; length to 255, nudge to half a step, ratchet to `kMaxRatchet`).
  A pitch over 127 wrote past `offIn` and `keysHeld` (review 2026-11-23; `testAnImageHoldsItsNotes`).
- **What plays while a scene moves goes with it, and nothing of a chord is
  worked out from a scene's number** (the join of chord effects and the scene
  move, 2026-11-13). `chordSeed` takes the bar of the CELL, not the bar of the
  song: with the scene's number in it a humanised chord rolled another way
  after NEXT SCENE, and no song left alone or built the finished way matched.
  So a chord effect in scene 2 or later rolls other numbers than it did
  before the join (scene 1 is the same to the bit). `Track::Arp::slot` is a
  bar of the song, so `ScenesMoved` moves it by the rule the playing moves by;
  an arpeggio whose scene has no place to go to (the last when one goes
  in, the one taken out) stays and looks whether its chord is still there
  (`Arp::look`). Waiting notes, `chordAsked`, `strokes` and the `GlideTrail`
  hold no scene number: time is steps since Play. **Anything new that the
  engine keeps while a chord plays either holds no scene number or is moved
  there.**
- **The song `nextscene_test` plays (`fill`) has a track of chords strummed,
  as arpeggios, humanised, ratcheted, thrown, in every ORDER, and a glide of
  every length, on a JAX-EP track (six voices: its chords and glides steal; `testTheChordTrackIsHeard`) whose GLIDE FROM is PAIRED**, so every proof of the sound has them in flight when the
  scene moves. The proofs marked `sound` in its table run again with the
  tracks on three helper threads, and the song left alone is held to the same
  sound on none and on three. `nextscene_test --only NAME...` runs just
  those; `--times` says how long each takes (the whole is near its two
  minutes on a busy machine: put a long new proof in the helpers' round only
  if it earns it).
- **Ask `Session::playScene()`, never `engine.playScene()`, for the scene
  that is playing.** Until the engine has heard a scene move (its next
  block) it still says the old number: the
  playhead left the grid and WRITE went into the scene left behind. The
  engine counts the moves it has taken in the same atomic as the scene
  (`Engine::playScene(&heard)`), so the two are never from different moments.
- **The SONG screen's cursor is a scene's number too** (`SongScreen::scenesMoved`,
  called by `App` after the gesture, an undo or a redo).

## The FILL (engine and data: batch F1-fill-engine, 2026-11-25)

Design 1 of `docs/plans/fills-ideas.md` on design 2's engine work, **without EVERY**. The owner read the
research and said "I like all of it, let's build all of that in" (2026-11-25), which approved the plan and
the picks it made. **The gesture (hold L1 + Y on the grids) and every screen are the next batch**; this one
is the engine, the data and the Session calls the screens will make.

**The decisions** (the report's picks, approved with the plan, 2026-11-25):
- **A cell can have one FILL bar.** It plays **instead of** the cell's own bar in the scene's **last bar**,
  on the scene's **last time round** (REPEATS). Everywhere else the cell plays as it always did.
- **A scene played alone** (looping) counts its time rounds the same way, round and round, and plays the
  FILL on the last of each REPEATS of them (`Engine::loopPass_`, below). To hear it, wait the REPEATS.
- **One meaning of the word FILL** (plan A's step condition and hold START take other words if they come).
- **Kept in the cell's eighth bar** (`kFillSlot`, slot 7), which a cell of 1, 2 or 4 bars never plays: no
  new pattern storage, so every copy of a cell or a scene carries it, automation (risers) included.
- **A cell of eight bars cannot have one** (that bar is its own: `Fill::EightBars`).
- **A cell with a FILL made eight bars long: the FILL becomes bar 8 and the flag goes off**
  (`setCellBars`; the engine is sent `SetCellFill 0`, or made four again it would play it).

**The rule is one header, `engine/include/apotheca/fill_rule.hpp`** (Rack-free, as `feel_rule.hpp`):
`fillTime(bar, meter, steps, round, repeats)` (the scene's last bar on its last round) and
`cellBarOf(bars, fillOn, bar, atFillTime)` (which bar of a cell plays: `bar % bars`, or `kFillSlot`).
The engine (`slotOf`), MIDI export (`gatherMidiScene`) and the app (`Project::cellBarPlaying`) all ask it.
**Anything that asks which bar of a cell plays goes through it, never `bar % bars`** (CLAUDE.md).
Its test is `tests/fill_test.cpp` (`ctest -R fill`).

**In memory:** `TrackFields::fill[scene]`, one byte a cell: `kFillNone` (never made), `kFillOn`, `kFillOff`
(made and turned off: its bar is kept). **"Made" keeps an emptied FILL empty** (a drop): turned on again it
is what it was, never a fresh copy. `TrackData::fillOn`, `fillMade` (both false on an eight-bar cell) and
`barThere(scene, bar)` (a bar the player can stand on: one of the cell's, or its FILL once made). In the
fields, so the text cache, `sameFields`, a duplicated track and undo's compares carry it with no work.
`sceneEmpty` counts a FILL that is on (a cell with nothing but its FILL is a scene: `sceneCount`).

**In the engine:** `SceneStore::fill` (so a scene move carries it: the store moves), `SceneImage::fill`,
`TrackImage::fill[]`, `sceneIn` takes it. **`Command::SetCellFill`** (track, scene, a = 1 on, 0 off) is
**appended after `SetClick`**; it goes through `send`'s GiveScene path (off on a blank scene needs no
store: `leavesBlank`). `slotOf(track, scene, bar, atFillTime)` returns `kFillSlot` where the rule says.
**`loopPass_` counts a looped scene's rounds; `repeat_` stays 0 there as it always did**, so its readers
(the ring lights, `songEndsNow`, `askAgain`'s last step) are untouched. **The REPEATS blocks are not**
(the plan's fixes table: "show REPEATS blocks from it"; done in the review fixes, 2026-11-25): in a loop the
top bar and SONG's REPEATS box draw `repeats` blocks filled by `Project::roundPlaying()` (`playPass` in a
loop, `playRepeat` in the song), where they drew one, so a player looping a scene sees the FILL's round
coming. Every looped scene shows it, with a FILL or not (`fillscreens_test` `testLoopedRepeatsBlocks`).
`playPass()` and `ProjectHead::playPass` tell it out. **`askAgain` runs after `SetSceneRepeats` and
`SetCellFill`**: changed on a bar's last step, the next bar may be the FILL or not (or another scene: a
REPEATS change on the last step was not asked again before; `fill_test` `testAskedAgain` holds both ways).
Turned on part way through the FILL's bar, the rest of that bar is the FILL's at once.
**A slice's run begins in the FILL** (`schedule`'s `fill`): FEEL and SWING find the hit a run begins with
by looking back over the bars of the cell, and the bar before the FILL's place (the seventh) is not the
bar played before it. Claude's pick, 2026-11-25, open to the owner: the FILL is a cell of its own there,
as a cell's first bar never looks back to its last (`testSliceRuns`).

**The Session's calls for the screens** (all in `session.hpp`, "the FILL"):
- `makeFill(track, scene, from = -1)`: never made, **made as a copy of the bar it plays instead of** (the
  cell's own bar in the scene's last bar, with that bar's 16 steps of automation; a note or chord running over
  the bar line held to the FILL's end) and on (`Made`); made and off, on as it was kept (`On`); on:
  `AlreadyOn`. Given `from` (one of the cell's bars), a copy of that bar instead: the gesture's, the bar on
  screen (F2, "The FILL's screens" below).
- `fillOff(track, scene)`: off, its bar kept (`Off`); `AlreadyOff`; none made: `NoFill`.
- `emptyFill(track, scene)`: its hits or notes and its automation emptied, **on stays on: a drop**
  (`Emptied`); nothing in it: `AlreadyEmpty`.
- `goToFill(track, scene)`: `project.track`, `scene` and `bar = kFillSlot`; false where none is made.
- `...Here()` for the cell the grids show, saying what was done (`fillWords`: FILL MADE, FILL ON,
  FILL OFF, FILL EMPTIED, THE FILL IS ON / OFF / EMPTY, NO FILL HERE, NO FILL ON 8 BARS, NO TRACK HERE;
  each fits the App's box at XL, `fillapp_test` `testTheWords`).
- `cellBarWords(track, scene, bar)`: **what the top bar reads**: "BAR 2 OF 4", "FILL" (on), "FILL OFF".
- `Project::cellBarPlaying(track)`: the bar of the cell playing now (`kFillSlot` while the FILL plays;
  -1 stopped), for the playhead and FOLLOW.
- Each change is ONE step of undo (`beginOwnUndoStep`); one that changes nothing is none.

**What the next batch (the screens) had to do** (the report's fixes table; all done in F2, "The FILL's
screens" below): the grids' own
`bar >= bars` guards push the view off the FILL (`pattern_screen.cpp` `project_.bar >= trk().bars`,
`note_screen.cpp` `project_.bar >= cellBars()`): use `barThere`. The playhead (`project_.playBar % bars ==
project_.bar` in `pattern_screen.cpp`, `pattern_slices.cpp`, `note_screen.cpp`) and FOLLOW
(`follow_.step(..., project_.playBar % bars, ...)`) want `cellBarPlaying`. The roll catches Y first
(`note_screen.cpp`, before its L1 block); Y's let go rolls dice or steps chance on the drum grid (swallow it
after L1 + Y). The top bar: `cellBarWords`. The roll's glide lines (`Project::glideLines`) do not draw the
FILL bar's (it is not one of the cell's bars in order). The Session's own guards already use `barThere`
(`setCellBars`, `restorePatterns`, `nextScene`), so undo, NEXT SCENE and a length change keep a view on it.

**What carries it** (each with a test in `tests/fillapp_test.cpp`):
- copy a bar onto the FILL (`pasteBar` takes `kFillSlot` once made: a note held to its own end, as
  `longestFrom`) and from it; copy a scene (`SceneClip::Track::fill`; a scene the same but for the flag is
  not "the same"); SONG's copy of a cell (`CellCopy::fill`); NEXT SCENE (`moveScenesHere` carries it; its
  undo and redo); undo (the song's text has it, `restorePatterns` compares and copies `fill` with `bars`).
- **Emptying takes it**: a scene (`clearScene`: and `SetCellFill 0`), a cell on SONG (`clearCell`), each
  counting a made FILL as something to empty.
- **WRITE and a keyboard's recording go into the FILL while it plays** (`automated`, `recordAutomation`,
  the keyboard's bar: `cellBarPlaying`). Claude's pick, 2026-11-25, open to the owner: what is recorded
  goes where what is heard comes from (`fillapp_test` `testPlaying`, `midi_test` `testRecordingIntoTheFill`).
- **MIDI export plays it on the last time round** (`gatherMidiScene` takes the round; a scene is gathered
  twice where a cell has one on). `testMidi`: a song with FILLs is the same file, byte for byte, as the
  same song built with each FILL's bar as a scene of its own.
- **An audio bounce plays it**: the bounce's engine is sent the song as any (`trackImage` carries the flag).
  `testBounce` reads the WAV.
- **The song file**: `fill 1` (on) or `fill 0` (made, off) after the scene's `bars` line, its bar the
  `bar 8` lines a hidden bar is written with. Never made: no line, so **an old song is written and read as
  it was, to the byte** (`testSongFile`; `effects_test --print` the same before and after). A `fill` on a
  cell of eight bars is dropped, whichever line comes first; a number not known reads as made, off.
  A scene with nothing but a made FILL is still written (its `scene` line and the `fill` line).

**Claude's picks, 2026-11-25, open to the owner** (beyond the two above):
- A FILL is made as a copy of **the bar it plays instead of**, not always bar 1 (the report's example was
  a one-bar cell, where they are the same): it starts as what would have played.
- **A FILL made and turned off also becomes bar 8** when the cell is made eight bars (kept as it is, empty
  or not), as one that is on does.
- `emptyFill` **leaves it on**: an emptied FILL is a drop. Turning it off is what takes it out of play.
- Making a FILL **writes over what the eighth bar hid** (a cell made shorter from eight keeps its bar 8):
  undo brings it back. Making the FILL from it instead would make an unseen bar the FILL.

**Costs** (`measured.md`): a step works out the rule twice (now and the bar after) and a compare or two a
track, eight times a second at 120 BPM, nothing per sample. The step clock's song before / after / every cell
a FILL: 0.193 to 0.207% / 0.202 to 0.210% / 0.202 to 0.211% of a desk core, within the runs' spread. Not run
on the handheld. **Nothing new moves by itself on the screen**: `playPass` changes on a step, which the
redraw already watches (`App::readouts`' `playStep`).

**Tests and mutants**: `fill_test` (13 parts: the rule; the FILL in the last bar of the last round and
never before, in a song and a looped scene, longer and cut-short scenes, 3/4, cell lengths, notes,
automation, early hits, asked again, images and moves, two scenes, slice runs) and `fillapp_test` (14
parts), `midi_test` `testRecordingIntoTheFill`. **78 mutants, 76 killed** (four only after the tests were
made to reach them: the engine told when a FILL comes on again, when a cell made eight bars long fills no
bar in, when a scene is emptied; a scene with nothing but its flag emptied). **Two survive, both kept**:
`moveScenesHere`'s `t.fill[from] = t.fill[from + 1]` (the carry before it has already copied that value; it
stands beside the same line for `bars`), and `patternsSameAsText`'s copy of `fill` for a track of another
kind (only `patternsAre` after a NEXT SCENE reaches it, where the two are the same). `fillTimeAt`'s
`validScene` check is never false (it is there for GCC's array-bounds warning) and was not mutated.

## The FILL's screens (batch F2-fill-screens, 2026-11-25)

The gesture and the screens on F1's engine (above). The owner approved the plan, its picks and its
recommendations with it (2026-11-25: "I like all of it, let's build all of that in").

**HOLD L1 + Y on the drum grid (its slice view too) and the roll: `Session::fillKeyHere()`**, one call for
both grids, one meaning a state:
- **No FILL made: made as a copy of the bar on screen, and the view put on it** (FILL MADE). See the pick
  below on which bar.
- **On the FILL: off** (FILL OFF; its bar kept, the view stays on it), **or on again** as it was (FILL ON).
- **From another bar, with one made: the view goes to it** (`Fill::Shown`, appended to the enum; nothing
  changed, no step of undo, nothing said: the top bar reads FILL or FILL OFF). It does not turn an off FILL
  on: going to it is looking (Claude's pick, 2026-11-25, open to the owner).
- **A cell of eight bars says why: NO FILL ON 8 BARS** (F1's words, which fit the App's box at XL).
- **L1 must go down first** (`l1First_`), as for L1 + R1 / A, and not with X held (X's Y is the dice for a hit
  or a note). It works wherever L1 + R1 / A do, the BARS stop and the tempo included.
- **Y was pressed for the FILL, so the rest of Y is swallowed.** The drum grid sets `yDidSomething_` (its let
  go rolls no lane dice and steps no chance; its press placed no maybe hit) and `ySpent_` (its hold is not the
  dice: D-pad UP on a name adds no hit). The roll catches it ABOVE its own Y block (which picks a chord on the
  press and places it on the let go): nothing is picked, so its let go places nothing (`endPick`).
- **L1's own tap acts on its let go only if nothing else went down meanwhile** (the slices, the zoom): Y is
  something (both grids already counted any button: `l1Used_`, `zoomUsed_`; `fillscreens_test` holds it).

**The ring of bars** (`TrackData::barAfter`): **1 ... the last bar, the FILL, 1**, for hold L1 + UP / DOWN
(the brief), and, so the FILL is a bar like the others, for the BARS stop's LEFT / RIGHT (`moveBar` on both
grids) and for the cursor run off a bar's end (the drum grid's sideways walk: off the last bar's step 16 to
the FILL's step 1, off the FILL's to bar 1's name; the roll's `moveCursorTo`: down off the last bar into the
FILL, off the FILL to bar 1, up from bar 1's top past BARS round to the FILL's end). Claude's pick,
2026-11-25, open to the owner: the FILL could instead be an island the cursor never walks into.

**What the screens do on the FILL:**
- **The guards keep the view on it**: `barThere` in both grids' `update` (was `bar >= bars`) and in
  `Session::standOn`; NEXT SCENE's undo keeps the bar shown across `restorePatterns` (which checked it
  against the copy being taken away), so the view stays on the FILL there too (its redo keeps it already: the
  scene checked has the FILL).
- **The top bar: `cellBarWords`**: 1 DRUMS / FILL / SCENE 1 (FILL OFF; the slice view 1 DRUMS / SLICES /
  FILL ...). The BARS stop's BAR field reads FILL or FILL OFF. **While the song plays the top bar has no room
  for it**: `topBar` cuts whole parts first, and 1 DRUMS / FILL is 110.6 pixels from x 356 against a room
  that ends at 462.5 while playing (4 short), so it reads 1 DRUMS, as BAR 2 OF 4 is cut today. Not changed
  (the look is settled): an owner question.
- **The playhead and FOLLOW: `Project::cellBarPlaying`** (the drum grid, the slice view, the roll).
- **The step numbers, the STEP field and the shading past a scene's end go by `Project::sceneBarOf`**: the
  FILL counts as the scene's last bar (`Project::lastBar`), where it plays. A FILL in a scene of two bars
  reads 17 to 32; a scene cut short to twenty steps shades the FILL's last twelve.
- **`Project::ownBarOf`: the bar it stands for** (the one it plays instead of): a slice placed on it in the
  slice view plays that bar's own slice (`ownStart`), and the roll draws the notes still sounding into it from
  the bars before that one.
- **A note or chord on the FILL runs at most to the FILL's end** (`NoteScreen::barsToEnd`: one bar), as F1's
  `makeFill` and `pasteBar` hold them.
- **The roll draws no glide lines on the FILL** (`Project::glideLines` knows the cell's bars in order, and the
  FILL is not one of them; without the skip, the line of the next time round's first note was drawn into it
  from below); a note's own glide mark still shows. Not mended: what a FILL's note glides from is the FILL's
  own business (`fillscreens_test` `testNoGlideLinesOnTheFill` holds the skip).
- **Copy a bar says FILL** on it: FILL COPIED, PASTED ON THE FILL, THE FILL IS THE SAME
  (`barCopyWords(..., fill)`; an eighth bar of the cell's own is still BAR 8).
- **The hints with L1 held say what Y does** (`Session::fillKeyHint`): make fill, go to fill, fill off, fill
  on; nothing on eight bars. Every one fits (`gHintsOver` 0; the bar is always at LARGE, so XL the same).
- **SONG has no new mark** for a cell with a FILL (the look is settled; an owner question).
- **Not on the help card** (the App's ON EVERY SCREEN / WITH SEL HELD card): it lists only what every screen
  does, and it is full (`app.cpp`), as for COPY A BAR (`drums-and-samples.md`). The manual and the L1 hints
  carry it.

**Claude's picks, 2026-11-25, open to the owner:**
- **The gesture copies the bar on screen**, as the brief for the screens says, not the bar it plays instead
  of (F1's pick, kept as `makeFill`'s default where no bar is given). Quoted, F1: "A FILL is made as a copy
  of **the bar it plays instead of** ... it starts as what would have played." Why: what is seen is what is
  copied, as L1 + R1 copies the bar on screen, and the player chooses the starting bar by standing on it; in
  the owner's case (one-bar cells) the two are the same bar.
- The ring includes the cursor's walk and the BARS stop, above. Going to an off FILL leaves it off, above.
- FOLLOW's hold on the FILL (`screens.md`, FOLLOW): held when the player put the view there or worked on it.

**Tests**: `tests/fillscreens_test.cpp` (17 parts: the ring and the scene's bars by themselves; the gesture on
the grid; Y swallowed; the ring on the grid; copy and paste on the FILL; eight bars; the gesture and the ring
on the roll; a note on the FILL; the top bar, the hints and the words; the numbers and the shading, both
grids; the playhead, both grids; a slice on the FILL (its start, numbers, shading, playhead); NEXT SCENE's undo
and redo; the BARS field; notes running into the FILL; no glide lines on it; a looped scene's REPEATS blocks), `follow_test` `testTheFillRule`,
`testTheFillOnBothGrids`; `copybar_test`'s L1 hints now with Y. Every check reads the real App through key
presses, or what it drew on a canvas.

**Mutants: 61, 60 killed.** Three only after the tests were made to reach them: the grid's `l1First_` (a test
of A held first, then L1 + Y), the slice view's playhead (the check compared the top bar too, which shows the
scene playing either way), the roll's glide-line skip (it shows only under a scene of eight bars). **One
survived and its code went**: NEXT SCENE's redo keeping the bar shown (the scene it stands on is the copy, which
has the FILL, so `restorePatterns` keeps it already). The review fixes' looped REPEATS blocks: 3 more, 3 killed
(one block in a loop, in the top bar and on SONG; SONG's blocks filled by `playRepeat`).

**Not seen on the handheld; nothing heard.** The stills (`docs/dev/stills.md`: `fill`, `fill-roll`,
`fill-eight`, `fill-playing`, at every text size in `textsize`, every theme in `themeviews`) and the tests are
all that has seen it.
