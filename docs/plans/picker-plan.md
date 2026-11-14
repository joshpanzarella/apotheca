# The sample picker: columns, two levels of folders, move and make on the device

Owner, 2026-11-14. Nothing here is built.

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
