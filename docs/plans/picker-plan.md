# The sample picker: columns, two levels of folders, move and make on the device

Owner, 2026-11-14. **Batches 0 and 1 are built** (2026-11-15, branch `picker`); 2 and 3 are not. "As built", at the end, says what and where.

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
| 2 | Not started | + NEW FOLDER, carry (hold R1), delete an empty folder (hold B), saved songs follow |
| 3 | Not started | |

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
| The cursor's column is ALWAYS the middle one; on SAMPLES the left column is empty | The cursor never jumps sideways. (The plan's words allow the cursor in the first column on SAMPLES.) |
| With no folders at all, SAMPLES lists the recordings themselves (no UNSORTED to go into) | One press fewer for a card with no folders: the list as it always was |
| Rows going by faster than one every 90 ms (`kAtOnceMs`) are not each put on; the one rested on is | Each is a file read from the card. A press, and the held D-pad's first pace, are still at once |
| Going into a folder puts its first recording on (as arriving on any row does); a folder under the cursor puts nothing on | "What is under the cursor is on" |
| A row shows a folder's count (its inner folders' too), not a recording's length | A length for each row is a file opened for each row. The length is by the picture |
| "Where it is used": ALSO IN THIS SONG: N PLACES, under the picture, for the open song only | Saved songs would mean reading every song file as the cursor moves |
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
