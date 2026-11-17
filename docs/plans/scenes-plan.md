# More scenes: past sixteen

Owner, 2026-11-17: "would it be possible to push the max scenes past 16? I think that's a good
starting point, but it would be nice if it was possible to move beyond that if necessary."

**The plan: 64 scenes, and a scene that holds nothing costs next to nothing.** Branch `scenes`.

## What the cap costs today, and what a bigger one would cost as it is built

Every track keeps every scene's patterns in fixed arrays, used or not: 62.7 KB a scene a track in the
app (`TrackData`: drums 22.8, notes 27.6, automation 12.3) and 52 KB in the engine (`SceneStore`).
So the cap's cost is paid by every copy of a track: the `Project`, the song's text cache (a copy of
each track), `StepRead` (undo's tracks), the engine's stores, a `TrackImage`.

Measured on the desk (`bench --app`, the bench's song: twelve tracks, eight scenes used, 2026-11-17,
two or three runs each). Only the constant changed; nothing else.

| | 16 (today) | 32 | 64 | 99 |
| --- | --- | --- | --- | --- |
| `TrackData` / `Project` | 1.0 / 12 MB | 2.0 / 24 MB | 4.0 / 48 MB | 6.2 / 75 MB |
| The engine's scene stores (12 tracks) | 10 MB | 20 MB | 40 MB | 62 MB |
| The whole program (`bench --app`, VmRSS) | **76 MB** | 134 MB | **246 MB** | 369 MB |
| One frame, nothing changing | 0.08 ms | 0.12 to 0.14 | 0.20 | 0.37 |
| A frame that ends an edit | 0.38 to 0.40 ms | 0.64 to 0.73 | 1.25 to 1.27 | 2.1 to 2.2 |
| Undo, then redo | 0.69 to 0.70 ms | 1.2 to 1.4 | 2.3 | 3.7 to 4.0 |
| ...a knob and a hit, undone | 1.29 ms | 2.2 to 2.5 | 4.0 to 4.1 | 6.2 to 7.6 |
| A track sent whole | 0.72 to 0.92 ms | 1.4 to 1.5 | 2.6 to 2.7 | 4.1 |
| NEXT SCENE, then undo | 5.7 to 6.7 ms | 10.0 to 10.8 | 18.2 to 19.0 | 32.4 to 32.6 |

The handheld is 13 to 20 times slower than the desk. At 64 as built, a frame that ends an edit would
be 16 to 25 ms there (a dropped frame every edit), NEXT SCENE and its undo a quarter of a second, and
every idle frame 3 to 4 ms; and 170 MB more memory for scenes nobody has written. A track sent whole
is also copied by the AUDIO thread today (16 stores, 0.8 MB, in one block); at 64 that is 3.3 MB.

**So fixed arrays are too heavy past 16. A scene's patterns are made when something is first put in
it.** Then the cap costs a few pointers and small numbers a scene, and 64 costs about what 16 does.

## The design

### The app: a track's scenes are pieces made when written

- `TrackData::drums`, `notes` and `automation` become `SceneCells`: a pointer for each scene, the
  piece (that scene's eight bars of drums, of notes, or its 24 lanes) made the first time it is
  written. **Read, a scene with no piece is the blank one** (one shared, never written). The syntax
  stays: `t.drums[scene][bar]`, `t.automation[scene][lane][step]`.
- A drum track never makes a notes piece and a synth track never a drums piece; automation only where
  a lane is written. A piece made and emptied again stays made (it is only memory).
- **Writing through a non-const track makes the piece**; anything that only reads takes the track
  `const` (the loops over every scene: `sceneEmpty`, `sceneCount`, the song's text, the screens).
- `TrackData` is no longer one block of bytes. The plain part (everything but the three) is kept as
  one block (`TrackFields`, the base), so the song-text cache and `makeBlank` still copy and compare it
  byte for byte, padding and all; the pieces are compared and copied a scene at a time, a scene with
  no piece on either side passed over at once.
- `bars`, `repeats` and `steps` stay arrays of `kMaxScenes` numbers (they are small).
- The song-text cache, `StepRead`, `parseSong`, undo: unchanged in what they do; each holds only the
  pieces the song uses.

### The engine: a scene's store is handed over when it gets something

- `Track::scenes[kMaxScenes]` points at ONE shared blank store wherever nothing was given. **The audio
  thread never writes it**: every command that writes a scene finds the scene's own store or does
  nothing (a write into a scene with nothing in it that leaves nothing, a clear, is nothing).
- **`Engine::send` (the app thread) hands a store over first** (`GiveScene`) when a command would
  write into a scene that has none. It keeps which scenes it has handed over (a bit a scene, 64 in a
  word), moved with a scene move and cleared with a track taken away.
- **A track sent whole (`SetTrackImage`) carries stores made on the app thread** for the scenes that
  hold something; the audio thread swaps pointers and hands the old ones back in the same holder. It
  copied 0.8 MB in one block before; now it copies nothing (the scene move's rule, for every track).
- **A track taken away hands its stores back in one holder** that `RemoveInstrument` brings (made by
  `send`), one item in the trash, not 64.
- `TrackImage` is made of `SceneImage`s, one for each scene that holds something (nullptr: none), with
  the slot-indexed names the tests use.
- `ScenesMoved` is as it is: one command, pointers moved, the audio thread copies nothing. A scene with
  nothing in it is handed over as nothing (the blank store goes in its place).

### The cap: 64

- **64, not 32 or 99.** With the pieces, the cap costs a few kilobytes a track, so 32 saves nothing
  that matters; 64 is the most a 64-bit word holds (the engine's "handed over" bits), and 64 scenes of
  eight bars at 120 BPM is over 17 minutes. 99 would only fill the two digits.

## What the owner sees

- **The SONG screen scrolls through 64 rows** as it scrolls through 16 now (eight shown, the `^` when
  rows are above). The row's number is two digits, as 10 to 16 are now. A held D-pad crosses all 64
  in about 2 s (170 ms, then a row every 30 ms), the stick at full in 2.6 s.
- **NEXT SCENE (SELECT + Y) at the cap: the same rule at 64.** A scene put in moves the ones after it
  down; the 64th, which must be empty then, falls off the end. When the 64th holds something (or the
  cursor is on it) there is no room: the message says NO ROOM: 64 SCENES.
- Nothing else changes: the scene numbers read up to 64 everywhere they are shown (SCENE 48, the top
  bar while playing, WRITE, the MOD tab's DEPTH lanes, COPY A BAR: each is a scene's).

## The song file

- **Old songs load unchanged, and a song of 16 scenes or fewer is written byte for byte as now**:
  `repeats` and `length` keep their sixteen numbers, and grow only to the last scene past the sixteenth
  that is not at its usual; `scene N` and `auto N` go to 64.
- **A song past 16 scenes on an older program** (0.737 and any build without this change): it loads, with its first sixteen
  scenes; the lines of scene 17 and on are skipped (a scene number past 16 is not taken), and the
  `repeats` past sixteen are not read. Saved there, those scenes are gone from that file. Nothing is
  released, so old programs are owed nothing; README says it.

## Owner's calls (taken at the recommended answer)

| Call | Recommended (taken) |
| --- | --- |
| 1. The cap | **64** (above) |
| 2. NEXT SCENE at the cap | **The same rule**: the 64th falls off when empty, NO ROOM when it holds something |
| 3. A faster way down the SONG screen (a page jump) | **None for now**: a held D-pad crosses 64 rows in 2 s. Ask again if it is slow on the handheld |
| 4. A song past 16 on an older program | **Owed nothing**: it loses the scenes past 16 there, said in README |

## Batches

0. This plan, and `bench --app`'s new rows (a track sent whole, NEXT SCENE and undo, the memory).
1. **The engine**: the blank store, `GiveScene`, `SetTrackImage` with stores made on the app thread,
   the holder for a track taken away, the "handed over" bits; `TrackImage` of `SceneImage`s. Held by
   `engine_test`, `nextscene_test` (and its thread sanitizer run: what a scene move hands over
   changes), blocks and helpers to the bit.
2. **The app**: `SceneCells`, `TrackFields`, the cache, `makeBlank`, undo, the file's `repeats` and
   `length`, the cap at 64; every loop over the scenes looked at for a piece made by reading.
3. **The screens and stills**: the SONG screen past 16, stills of a song of 40-odd scenes at every
   size and theme (`kViews`), the texts at their longest (SCENE 48).
4. **README and CLAUDE.md**: every rule that says sixteen.
5. **Review**: mutation testing on the new code, the figures again.

## As built

(Filled in as each batch is done.)
