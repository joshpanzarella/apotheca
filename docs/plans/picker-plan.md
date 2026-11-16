# The sample picker: columns, two levels of folders, move and make on the device

Owner, 2026-11-14. **Batches 0 to 3 are built** (2026-11-15, branch `picker`): 3 is the review and its mend. "As built", at the end, says what and where.

> "I think we need to be able to traverse folders in columns ... similar to the way the command line
> tool yazi looks. It just needs to be easier to move around and select the sample you want. The
> movement should be fast as well (not so fast that it is hard to choose). It would be really nice if
> you can create folders and move your samples on device as well."

## Settled with the owner

| Question | Answer |
| --- | --- |
| How deep do folders go | **Two levels** (`drums/kicks/808`), up from one |
| Moving a recording | **Every saved song and rev that names it follows**, as a rename on the WiFi page does (`savedSongsFollow`, `renameRecordings`) |
| Deleting a folder | **Only when it is empty** |

## The screen: three columns, as yazi

```
 SAMPLES          | DRUMS              | KICKS            3 OF 12
------------------+--------------------+---------------------------
 UNSORTED      14 |  HATS            9 |  808 LONG       0.50 S
>DRUMS         31 | >KICKS          12 | >TIGHT          0.20 S
 VOCALS         4 |  SNARES         10 |  SUB DROP       1.10 S
 + NEW FOLDER     |  + NEW FOLDER      |  (its picture, the line where it has got to)
```

- The column the cursor is in is the middle one once inside a folder; what is to its left is where it
  came from, what is to its right is what the thing under the cursor holds (a folder's contents, or a
  recording's picture, length and where it is used).
- A recording under the cursor goes onto the lane AT ONCE, as now (heard in the beat when playing,
  played once when stopped).

| Button | Does |
| --- | --- |
| UP / DOWN | The next in the column. Held: the list's pace, then faster after a moment (not so fast a name cannot be read). The stick: big moves |
| LEFT / RIGHT | Out to the column before / into the folder under the cursor |
| L1 / R1 | A page up / down |
| A | Keep the recording showing. On a folder: into it. On + NEW FOLDER: the keyboard for its name |
| X | Hear it, for as long as held |
| B | A tap: put back what was there before the list opened |
| Hold B | Delete for good: a recording, or an EMPTY folder (a bar fills; says it cannot be undone) |
| **Hold R1** | **Carry the recording under the cursor**: steer to a folder (LEFT / RIGHT / UP / DOWN), let go to put it there. B while carrying calls it off |

## What changes underneath

- `sampleNameOk` (app/samples.cpp:182) allows two slashes; `sampleFolderOf` is the whole folder path
  (everything before the last slash); the WiFi page's `plainName`, `wavNames` (lists two levels),
  `findMissing`, `recordingThere`, `deleteSample` and the page's folder import follow.
- **A name still has to fit `kSampleNameRoom` (64 bytes) whole, folders and all**: the picker refuses a
  move or a folder name that would not fit, and says so. Song files keep loading: a one-folder name is
  still a name.
- Moving is a rename of the file, then `recordingChanged` for the open song (name only: the sound is
  not read again) and `savedSongsFollow` for the saved ones, holding `Transfer::songFiles()`.
- New folder: `mkdir` under the samples folder (or under a first-level folder); the name through the
  same checks as a recording's.

## Batches

| Batch | What |
| --- | --- |
| 0 | Two levels underneath: the name rules, listing, the WiFi page, missing-recording search; tests. Nothing on the screen changes |
| 1 | The three-column picker, its pace, its stills at every text size and theme |
| 2 | New folder, carry (move), delete an empty folder; saved songs follow; tests with real files |
| 3 | Review by measurement and by trying it; mend |

## The owner's calls still open

| Question | Recommended |
| --- | --- |
| The first row (THE BUILT-IN DRUM / none) | Stays at the top of the first column |
| Rename a recording or a folder on the device | Later; the WiFi page renames today |
| Carry several recordings at once | Later |

## As built (batches 0 and 1, 2026-11-15)

Nothing here has run on the handheld; the owner has not seen or heard it. Tests and stills only.

| Batch | State | Where |
| --- | --- | --- |
| 0 | **Done** | `app/samples.cpp` (`sampleNameOk`, `sampleFolderOf`, `wavNames`, `wavFolders`), `app/transfer.cpp` (`safeSamplePath`, `realFile`, `dropIfEmpty`, `foldersMade`, `foldersIn`, `recordingsIn`, and the page's script: `split`, `wrongFolder`, `tooLong`, `folders`); `sample_test`, `transfer_test` |
| 1 | **Done** | `app/sample_picker.hpp/.cpp`; `Session::sampleFolders`, `sampleUses`; stills `picker`, `picker-in`, `picker-deep`, `picker-deleting`, `sampler-picker`; `sample_test`, `textsize_test`, `textviews`, `themeviews` |
| 2 | **Done** | `Session::moveSample`, `makeSampleFolder`, `deleteSampleFolder` (`app/session_samples.cpp`); the list's carry, + NEW FOLDER row and keys, hold B on an empty folder (`app/sample_picker.hpp/.cpp`); stills `picker-new`, `picker-typing`, `picker-carrying`, `picker-folder-deleting`; `tests/picker_test.cpp`, `textsize_test`. See "As built (batch 2)" below |
| 3 | **Done** | The review's ten findings: see "As built (batch 3)" at the end |

**The gestures as built**

| Button | Does |
| --- | --- |
| UP / DOWN | The next in the column, the ends stop. A press puts the recording on at once. Held: `ui::kListEveryMs` (110 ms), and after 1.5 s (`kFasterAfterMs`) 60 ms a row (`kFasterEveryMs`) |
| The stick | Up to 20 rows a second times `ui::gStickSpeed` (15 as set), by how far it is pushed |
| LEFT / RIGHT | Out / into the folder under the cursor. The cursor is where it was in a folder gone back into |
| L1 / R1 | A page (10 rows of the 11 shown) |
| A | A recording: keep it. A folder: into it |
| X | Hear it, for as long as held. Nothing on a folder |
| B | A tap: as it was |
| Hold B | Delete the recording (a second, the bar, CANNOT BE UNDONE). Nothing on a folder yet |

**Choices made where the plan left room** (each one place to change)

| Choice | Why |
| --- | --- |
| ~~The cursor's column is ALWAYS the middle one; on SAMPLES the left column is empty~~ **Changed (owner, 2026-11-15)**: on SAMPLES the cursor is in the first column, the folder under it in the second and the preview's box open in the third, so going in moves only the cursor | The owner: "start out with the sound preview open to the right of the 2 levels ... that way it doesn't need to expand". Before, going into a folder slid every column left and the preview appeared |
| The preview (2026-11-15): a recording arrived on, the song stopped, plays half of what it plays, stopped as soon as the cursor moves on; X while it plays: on to its end; X with nothing playing: held, as before | The owner's words. The kit's list played the whole recording before, the sampler's a 0.35 s tap |
| With no folders at all, SAMPLES lists the recordings themselves (no UNSORTED to go into) | One press fewer for a card with no folders: the list as it always was |
| Rows going by faster than one every 90 ms (`kAtOnceMs`) are not each put on; the one rested on is | Each is a file read from the card. A press, and the held D-pad's first pace, are still at once |
| Going into a folder puts its first recording on (as arriving on any row does); a folder under the cursor puts nothing on | "What is under the cursor is on" |
| A row shows a folder's count (its inner folders' too), not a recording's length | A length for each row is a file opened for each row. The length is by the picture |
| "Where it is used": ALSO IN THIS SONG: / N PLACES (two lines since 2026-11-15: one ran past the panel), under the picture, for the open song only | Saved songs would mean reading every song file as the cursor moves |
| Deleting a folder's last recording leaves the folder, empty | The plan: a folder is deleted by itself, only when empty. (The WiFi page still drops a folder with its last recording.) |
| The hints bar does not say UP / DOWN | It was 16 pixels over with it. The help card says the D-pad and the stick move |
| A lane on the KIT page says the inner folder of two; the SAMPLER says `DRUMS/KICKS  /  808` | The lane's room is 70 pixels |
| The page's folder import still makes one level | Not asked for; its plan and its words would change |
| The first row stays at the top of SAMPLES | The plan's recommended answer |

**For batch 2**

- R1 pages on the PRESS today. Carry is hold R1: the page will have to wait for the let-go, or for a hold.
- `SamplePicker::open` takes the folders (`Session::sampleFolders`), so an empty folder is already listed, can be gone into (NOTHING IN IT) and left.
- UNSORTED is listed only when something is loose: carrying a recording OUT of every folder needs somewhere to let go (SAMPLES itself, or UNSORTED always shown).
- + NEW FOLDER rows are not there: `rowsAt` is the one place rows are made.
- `Session::deleteSample` and the WiFi page's `dropIfEmpty` disagree about an emptied folder (see the table). The owner may want one rule.

## As built (batch 2, 2026-11-15)

Nothing here has run on the handheld; the owner has not seen it. Tests and stills only.
**The WiFi page was not changed in this batch** (the plan asks nothing of it here; batch 0 did its part).

**The gestures added**

| Button | Does |
| --- | --- |
| Hold R1 on a recording | Carries it, after 300 ms (`kCarryHoldMs`), or at once with a direction or the stick. Steer with the D-pad, the stick, L1; LEFT / RIGHT go out of and into folders. The top line: CARRYING TO place: name |
| Let go of R1 | It goes into the folder under the cursor; else into the folder showing; on SAMPLES, on a row that is no folder: out of every folder. The top line says MOVED TO place, or why not |
| B with R1 held | Calls it off |
| R1, a tap, on a recording | A page, when it is let go (on any other row: at once, as before) |
| A on + NEW FOLDER | The keys: A types, B deletes (nothing typed: leaves), Y or START keeps. Twelve letters |
| Hold B on an empty folder | Deletes it (a second, the bar, CANNOT BE UNDONE) |

**Choices made where the plan left room**

| Choice | Why |
| --- | --- |
| Where a carried recording goes: the folder UNDER THE CURSOR if there is one, else the folder SHOWING | Both readings of "steer to a folder" work: stop on it, or go into it |
| Out of every folder: let go on SAMPLES on a row that is no folder (the first row, + NEW FOLDER) or on UNSORTED | UNSORTED is not listed when nothing is loose; this needs no extra row |
| A direction with R1 carries at once, without the 300 ms | Speed. The wait is only there so a tap is still a page |
| On a recording, R1's page waits for the let-go | A press cannot be both. On other rows it is at once |
| The bar on a recording says HOLD R1 carry in place of L1 / R1 page | It holds five hints. L1 / R1 still page |
| + NEW FOLDER is the LAST row | The plan's picture. First would put it under the cursor on going into a folder, where the first recording goes on |
| No + NEW FOLDER in UNSORTED | UNSORTED is not a folder |
| A folder is made in small letters, 12 letters at most; one of that name, capitals not counted, is refused | The folders the program makes are small; a card may not tell KICKS from kicks |
| After a folder is made the cursor is on it; after a move into the folder showing, on the recording, which goes on the lane | "What is under the cursor is on" |
| While carried nothing goes on the lane and nothing is heard | Rows are only steered over |
| Nothing is written over: a recording of that name there (capitals not counted) refuses the move | As the WiFi page |
| A carry is never made into a folder that is not there, and never through a link | The list only offers real folders |
| An "empty" folder is one `rmdir` takes: a file that is no recording keeps it, and the top line says so | Never delete what the list does not show |
| A folder holding only an empty folder is not deleted: the inner one first | "Only when it is empty" |
| The folder a recording was carried out of stays, empty | As batch 1's delete |
| A let-go of R1 the list never saw calls the carry off | Nothing moves without a let-go that was seen |
| The keys are the list's own copy of the SET page's | `SettingsScreen` was not touched |
| What was done, or why not, shows on the top line for 2.5 s | The title's place; no new box |

**Known, not hidden**

- A recording whose name is too long to be a name at all (listed from the card, over 63 letters) says COULD NOT MOVE IT.
- The still `picker-typing` gets no START (it would make the folder); `picker-folder-deleting` is taken at 600 ms, as `picker-deleting`.
- Rename on the device, and carrying several at once, are still "later" (the owner's open calls).
- The hold's 300 ms and the page-on-let-go are not tried on the handheld.

## As built (batch 3: the review and its mend, 2026-11-15)

Nothing here has run on the handheld. Every figure is the desk's.

| Finding | Fate |
| --- | --- |
| A long recording carried into a folder vanished from the WiFi page | **Mended**: a file in a folder may be 60 with its ending, as a loose one (the whole still 63). Server and the page's script; the script tried in a browser |
| The stick was no big move; nothing paged when held | **Mended**: stick 80 rows a second times its setting, by the square of the push; a third D-pad pace (20 ms a row after 3.5 s); L1 held pages on, R1 too where it does not carry. 500 rows: 8.3 s / 12.6 s / 5.5 s |
| A carry let go freezes while every saved song is read | **Mended (2026-11-15, left to us by the owner)**: a few files a frame, MOVING TO X... on the top line until done; what is owed written down first (renames-owed.txt) and finished at the next start, so a switch-off mid-way leaves nothing naming the old place. Desk: 40 songs x 11 files all naming it, 450 to 740 ms at once; now the longest frame 12 to 16 ms |
| Two columns' rows worked out every frame | **Mended**: counted once when the card changes, kept until the place or the card changes |
| MOVED though the songs were not rewritten | **Mended**: `FileJob::NotSaved`, and the top line says so |
| Hold B on a folder that is not empty said nothing and closed the list | **Mended**: after 400 ms it says NOT DELETED: IT IS NOT EMPTY, and the let-go is no tap |
| Four guards with no test | **Mended**: each has a test and its mutant dies; `wavFolders`' filter too |
| R2 / L2 changed the tab while typing | **Mended** (MENU's help also stays down) |
| No folders: hold R1 said CARRYING and did nothing | **Mended**: no carry there; R1 pages at once and held pages on |
| A name over 63 letters was listed | **Mended**: not listed |

**Choices made** (each one place)

| Choice | Why |
| --- | --- |
| The stick by the SQUARE of the push | Half way is 15 rows a second, all the way 60: one stick both reads and travels |
| The D-pad's third pace only after 3.5 s | "Not so fast that it is hard to choose": the first 3.5 s are as they were |
| R1 held on a recording still carries, so it cannot page on there | Hold R1 drags on every screen. L1 held, the stick and DOWN held are the ways down a long folder |
| A carry let go where it was says LEFT WHERE IT WAS | An R1 held a moment too long for a page did nothing and said nothing |
| A refused hold of B says so at 400 ms, with no bar | A bar would promise a delete |
| A file too long to be a name is hidden, not shown as broken | It could never be put on a lane |

**For the owner**

- The freeze on a carry let go: mended (above). Not timed on the handheld: one card write a frame is an estimated 60 to 80 ms frame there, and a recording in 40 songs with their revs is about 440 frames of MOVING.
- An emptied folder: kept on the handheld and on the WiFi page (owner, 2026-11-15: "Keep them").
- A folder holding only a computer's hidden file (.DS_Store) cannot be deleted on the handheld: it says OTHER FILES ARE IN IT.
