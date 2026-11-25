# The SONG screen

Mute and queued mutes, head marks, duplicating a track, solo, AT END, the mixer.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **Mute is its own gate** (`SetTrackHeard`), not the track level.
- **QUEUED MUTES: SET's MUTES at NEXT BAR, a mute or solo pressed while playing lands on the next bar's
  first sample** (easywins batch 8; `SetTrackHeard` with b = 1, `Track::heardNext`, `Engine::landHeard`
  from `fireStep` at step 0 and from `stopPlaying`). AT ONCE (b = 0) is the engine as it was. Stopped,
  or told what it is already: at once, and what waited for that track is called off. **The engine says
  when nothing waits, the app does not guess**: an asking's number rides on the LAST track's command
  (c) and comes back in `heardDone()`; `Session::mutesWaiting_` goes down when the two are equal. The
  project's `muted` / `solo` say what was ASKED at once (the song file, undo's text); the SONG head
  draws the mark hollow while it waits (`muteWaits`: going on or coming off) and keeps the head as it
  still sounds (`heardNow`). A song sent again, or MUTES put back to AT ONCE, lands it at once.
  `mutes N` in settings.txt, left out at AT ONCE. Anything new that sends `SetTrackHeard` for a
  press goes through `Session::pressedMute`. Stills `song-mute-waiting`, `setup-mutes`. Its marks
  change only while the song plays, which the redraw already draws: no `idle_test` part. **One mutant
  lives**: the narrow heads' number moved aside for a WAITING mark (11 and 12 tracks) has no test.
- **A head's M or S mark is a box made to its letter** (`SongScreen::headMarkWidth`: the letter at the
  heads' own lettering, LARGE, 2 pixels clear all round; owner, 2026-11-14: in a fixed 11 by 11 the letter
  stood out of it). Where the mark would stand on the track's number (11 and 12 in the narrowest heads)
  the number is centred in the room left of it, clear of the mixer's mark. Stills `song-marks`, `-12`.
- **DUPLICATE A TRACK: L1 held on a SONG head, then R1** (easywins batch 6; `Session::duplicateTrack`,
  `doubleTrackText`). The copy goes right after its track, the tracks after a place down (their type
  memories and preset memories with them), the song sent again (`sendSong`, as a delete). **Undo keeps
  its steps: each has the track twice from then** (the step's own version), so an undo from before the
  copy puts both back to what the one was; the copy itself is not undone. **It is done on R1's LET GO
  with L1 still held**, and the App tells the screen when all four shoulders are down
  (`SongScreen::shouldersPressed`): a panic begun L1, R1 on a head copies nothing. A let go the SONG
  screen never saw copies nothing either (`update`). A cell is emptied by B and copied by HOLD R1 and
  drag, as before: nothing was added there.
- **The owner's five of 2026-11-18** (`easy_test looks`). SONG's column word is REPEATS (`SongScreen::
  kRepeatsWord`, low in the heads' row, 10 above the first box; `kRepeatW` 38, it was 34, so the word
  fits at LARGE: the columns start 4 further right). **A lane's volume in its name's box is `kPanel2`,
  no edge where it ends** (tried 2026-11-18, confusing): panel on bg was 1.08 to 1 in BLACK; the fill
  is 1.22. The
  name's word starts at `kNameTextX`, 5 clear of the feel mark (it was 1), in the grid and the slices.
- **Solo is a tap of X on a track** (a cell or its head; owner, 2026-11-15: "I don't like the hold L1
  for solo since Y for mute is just a one tap"). It was a tap of L1, then for a day L1 HELD 400 ms
  (a tap of L1 soloed by mistake too easily); the hold, its timer and `kSoloHoldMs` are gone, and **L1
  does nothing on a track**. **X no longer hears the track on SONG** (`Session::hearTrack` is gone with
  it): a track is heard by X on its DEVICE page. The head's bar says `X solo` and R1 `instrument` again;
  the cell's bar has no room for `X solo` (20 pixels over): mute and solo are said on the head. The help
  card's X line reads "hear it (SONG: solo). Held: a step's effects, a knob's MOD" (joined with the MOD
  screens' line, 2026-11-15; it is held to the card's margin by `testTheHelpCardDrawsSel`). `app_test` `testMuteSoloAndNames`, still `song-head`.
- **The stick on a scene's REPEATS does what the D-pad does there** (gesture audit G14, 2026-11-24;
  `SongScreen::repeatStick`). **L1 held**: up / down the scene's own tempo, squared, a beat at a
  time at up to `kSceneTempoPerSecond` (90, the drum grid's tempo row's), left / right its meter.
  **A held**: up / down the repeat count as before, left / right the scene's steps (16 or fewer).
  Sideways is a click at once, then `kRepeatSidePerSecond` (3): they are counts. **One job a push**
  (`repeatAxis_`, set afresh when A or L1 goes down). Before, L1 + the stick moved the cursor off the
  count (and so dropped `l1OnRepeat_`), and hold A read only the stick's y. `easy_test stick`.
- **AT END: a song played as a song LOOPs (as it always did) or STOPs**
  (owner, 2026-11-14; `Project::songEnd`, `kSongLoop` / `kSongStop`). In a
  song file `songend 1` among the song's own lines (`cutSong` knows it),
  **left out at LOOP**, so a looping song is written as before and an old
  program plays it round. In undo with the song (`restorePatterns`); told to
  the engine by `syncSong` (`Command::SetSongEnd`, `sentSongEnd_`, live: it
  may be switched while the song plays). **LOOP is the engine as it was, to
  the bit** (`effects_test --print` and a busy song against `main`, the
  same). STOP: the engine asks nothing early of the top at the last step
  (`playsOnce()`, as a bounce's `once_`), and on the first sample of the step
  after the last step of scene `sceneCount_` (its last time round) it stops
  exactly as `Command::Stop` does (`stopPlaying`, `songEndsNow`, checked in
  `render()` before anything of that sample). Not for a scene alone, not for
  a bounce (`once_`: it stops itself, so a STOP song bounces to the same
  file). **The engine says it stopped, the app does not guess**: it counts
  the Plays it takes (`playsTaken_`) and keeps the one that ended in
  `songEnded()`; the Session counts the Plays it sent (`playsSent_`) and
  takes `project.playing` down only when the two are equal, so a START
  pressed just after the end is never lost. The place: the PLAYS box of the
  first empty row (`SongScreen::songEndRow`, `onSongEnd`) shows a loop arrow
  (dim) or a square (brass); A switches, B puts LOOP back; that box had a
  repeat count of a scene with nothing in it, which it no longer shows.
  Tests: `tests/songend_test.cpp`; stills `song-end-loop`, `song-end-stop`.
  - **A `songend` number nobody knows is LOOP** (`songEndKind`), as an unknown
    GLIDE FROM is TOP: a later program's third choice plays round here.
  - **Where the song stops is a bar line for Catch** (`Engine::barLine`, called
    by `fireStep` and before `stopPlaying` in `render()`): a bar's end is
    marked where the next begins, and a stopped song begins none. Without it
    Catch after the end gave the bar before the last. (A Stop by hand exactly
    on a bar line still gives the bar before: as on `main`.)
  - **AT END switched during the song's LAST step asks that step again**
    (`askAgain(true)` from `SetSongEnd`, only when `playsOnce()` flips and only
    on that step): to LOOP, the top's early hits and notes are asked for (it
    is LOOP all along, to the bit, if none was due yet); to STOP, those not
    yet begun are taken back. On any other step, or told what it already is,
    nothing is asked again: a ratchet's waiting pieces would be dropped.
  - **Only Plays that went are counted** (`togglePlay`: not a Stop, not a
    Play lost to a full queue), and `playsSent_ != 0` keeps a play mark set
    by hand in the tests ("as if playing") from being taken down.
- **The mixer (owner, 2026-11-14): hold A on a track's head on SONG, or on
  MASTER (the head right of + TRACK), and UP / DOWN is LEVEL, LEFT / RIGHT PAN;
  B with A held puts both back.** A tap of A on a head still names the track:
  it acts on letting go, as on a cell, and **only if no other button was
  pressed while A was held** (`mixTurned_`) **and it was let go before the card came up**
  (`kMixCardMs`, 300 ms: `mixShown_`; owner, 2026-11-14: a look at the level opened the keyboard); while A is held on a head every
  other button but START does nothing (Y there muted the track under the
  card). **A pressed while B's delete is filling calls the delete off**, on a
  head and on a cell (`update()`), and is then no tap. `TrackData::mixDb` (whole dB from the
  usual, -40..+6) and `mixPan` (-50..50), `Project::masterDb` / `masterPan`;
  `Session::setMix(track, db, pan)` takes `kMasterTrack` and nothing else
  below zero, calls `noteEdit()` (a step of undo: `restorePatterns` puts them
  back, from the head for the master and from each track's text read).
  In a song file `mix DB PAN` on a track and `master DB PAN` among the song's
  own lines (`cutSong` knows it), each **left out at 0 0**. **0 DB is
  `kUsualTrackLevel` (0.65) exactly** (`mixGain`), and the engine's PAN is
  `panGains`: equal power, the middle exactly 1 and 1. Level and pan glide
  for `kSendGlideSeconds`; at rest in the middle a track runs the loop it
  always ran, and the master at 1 and the middle is not run at all
  (`runMaster`), so a song left alone is the same to the bit (`effects_test
  --print` the same before and after). The thrown copy of a drum hit takes
  the level's glide but not the pan (it is one sound for both sides of a bus).
  Not automated. MASTER takes a column, so with 11 or 12 tracks the heads are
  42 px, their numbers held at LARGE, and DRUMS reads DRUM. Tests:
  `tests/mixer_test.cpp`; stills `song-mixer`, `song-mixer-master`, each also
  `-12`.
