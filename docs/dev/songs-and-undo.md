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
