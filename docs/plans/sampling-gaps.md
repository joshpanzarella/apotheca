# Sampling: what we have and what is missing

**The basics are all there**: load, record, catch, chop, fit to the tempo, edit, multisample, and a sampler instrument.
**What is missing is mostly getting a sound out again, and chopping well**: save an edit or a slice as a new file, resample more than one bar, cut slices where the hits land, and EDIT on the sampler.

This comes from reading the code only. **Nothing was built, run or heard for this report.**

## Bugs found on the way

| Bug | Where | Fix |
| --- | --- | --- |
| **RECORD's line says "OF 10", but a take stops at 20 s** (owner, 2026-11-18). Two header comments still say "Ten seconds at most". No test checks the text. | `app/kit_screen.cpp:324`, `app/sampler_screen.cpp:347`, `app/synth_screen.cpp:494`; comments `app/kit_screen.hpp:31`, `app/sampler_screen.hpp:30` | Print the cap from `Recorder::kMaxSeconds` (`app/recorder.hpp:29`) |
| **A 3/4 catch can't get back to 12 steps.** CATCH sets FIT to 12, but once FIT is moved by hand it can't return: 12 goes up to 16 or down to 4. | `app/session_samples.cpp:746`, `app/kit_screen.cpp:89-96`, `app/kit_screen.hpp:80` | Add the missing stops (see "FIT to any length" below) |
| The manual says "any sample rate". The code takes 4 kHz to 192 kHz. | `docs/manual.md:2150` vs `app/samples.cpp:73` | Fix the manual |

## What exists

| Feature | What it does | Code |
| --- | --- | --- |
| Read WAV | 8/16/24/32-bit or 32-bit float, 1 to 8 channels, 4 to 192 kHz. Played as **mono** at 48 kHz. Cut at **20 s** with a 10 ms fade. | `app/samples.cpp:50-111` |
| Write WAV | Catches and takes are saved as 16-bit mono 48 kHz files. | `app/samples.cpp:118-142` |
| Samples folder | `samples`, with folders up to two levels deep. A name is at most 63 letters. | `app/samples.cpp:169-217` |
| The list (picker) | Three columns. The D-pad loads a sound at once and plays a preview. The stick moves up to 80 rows/s. X hears it, B puts back the old one, A keeps it. | `app/sample_picker.hpp:1-60`, `:97-112` |
| Carry, new folder, delete | Hold R1 carries a file to another folder and every song follows the new name. + NEW FOLDER. Hold B deletes. | `app/session_samples.cpp:60-76`, `:94-171` |
| A missing file is found again | It relinks only when exactly one file has the same base name. Otherwise the lane plays its built-in drum. | `app/session_samples.cpp:173-236` |
| Hearing a sound | Tap X: 350 ms. Hold X: plays until let go. Heard at 0.9. | `app/session.hpp:135-142` |
| Drum lane sound | 8 lanes. Each plays the built-in drum, a recording or a DRUM SYNTH voice. One hit at a time, with a 3 ms fade. | `engine/include/apotheca/drums.hpp:10-11`, `:181-189` |
| FIT | Stretches a recording to 1 to 64 steps at the song's tempo, at the same pitch, on a helper thread. | `app/session_samples.cpp:782-869` |
| Lane EDIT | START/END, fades, GAIN, PITCH, FINE, REVERSE, trim silence, normalize. The file is never written (owner, 2026-11-16). | `app/lane_edit.hpp:1-61` |
| CHOKE | Two groups. A new kit puts its hats in CHOKE 1. | `engine/include/apotheca/drums.hpp:13-19` |
| Per-hit START | Hold Y + stick slides where a hit starts. Y + right on a lane's name rolls new starts. | `app/pattern_screen.cpp:236-280` |
| Step effects | PITCH ±12, RATCHET 2 to 4, REVERSE, CUT, THROW, and the dice. | `engine/include/apotheca/drums.hpp:41-57` |
| Slice view | L1 cuts the sound into 16 even slices, or 32/64 when fitted longer. You can paint the slices in order, paint random ones, and runs play seamlessly. | `app/pattern_slices.cpp:47-164`, `engine/src/drums.cpp:249-263` |
| FEEL and SWING | Per lane, applied live. A run of slices moves as one. | `engine/include/apotheca/feel_rule.hpp` |
| Copy a bar | Hold L1 + R1 copies a bar, L1 + A pastes it. Slices and step effects go with it. | `docs/dev/drums-and-samples.md:69-99` |
| CATCH | Y saves the last whole bar of the master mix as a file and puts it on the lane, fitted. | `engine/src/engine.cpp:241-261`, `app/session_samples.cpp:724-749` |
| RECORD | R1 records from a USB mic. It waits for sound, stops at 20 s and evens out the level. | `app/session_samples.cpp:505-540`, `app/recorder.cpp:7-64` |
| SAMPLER | 8 voices played from the roll. PITCH, TUNE, START, END, ATTACK, RELEASE, LOOP, LEVEL. Glides work. | `engine/src/sampler.cpp:60-170` |
| Multisample | Notes come from the file names (C2, C#3). Up to 24 notes. | `app/samples.cpp:229-264`, `app/session_samples.cpp:550-637` |
| Velocity layers | A layer word in the file name (PPP to FFF, SOFT, HARD...). At most 4 a note, 48 files (owner, 2026-11-24). | `app/samples.cpp:281-352` |
| Sampler CHOP | L1 cuts one recording into 16 slices on C1 to D#2. | `engine/src/sampler.cpp:81-87` |
| Sampler presets | R1 saves under a typed name and relinks the recording when picked. | `app/sampler_presets.cpp:1-13`, `app/session_samples.cpp:684-703` |
| Kit presets | 7 built-in kits. Yours save as KIT N. | `app/kits.cpp:33-41`, `:142-159` |
| DRUM SYNTH into a lane | 12 models, 73 voices. Each is rendered into a recording, so EDIT, FIT and slices all work on it. | `app/session_samples.cpp:265-300`, `:369-381` |
| CLOUD | A granular synth (plays tiny grains of a recording). It can record and catch too. | `engine/include/apotheca/cloud.hpp:35-36`, `app/synth_screen.cpp:394-431` |
| Designer web page | Edit SAMPLER and DRUMS in a browser. The handheld makes the sound. | `app/designer.cpp`, `app/design_page.cpp` |
| Files over WiFi | Upload (30 MB each), folders, FIND, play, download, rename, move. | `app/transfer.hpp:112`, `app/transfer.cpp:1321` |
| Bounce | Song, stems, one track or MIDI. Saved as 48 kHz 16-bit stereo files in `bounces`. | `app/bounce.cpp:1-60` |
| Undo | Covers EDIT, FIT, VOLUME, CHOKE, FEEL, SWING, slices and sampler settings. **Not the swap of a recording.** | `app/session.cpp:1143`, `app/session_samples.cpp:355-365` |

**Two limits shape everything**:
- **Every sound is mono.** Stereo files are averaged to one channel (`app/samples.cpp:102`), and so is CATCH (`engine/src/engine.cpp:3061`).
- **A recording is 20 s at most** (owner, 2026-11-18).

**Not heard by anyone yet**: lane EDIT, DRUM SYNTH, CLOUD, FEEL, SWING and CHOKE (`docs/dev/drums-and-samples.md:112`, `:200`, `:253`, `:298`, `:304`). **RECORD has never been tried on the handheld** (`docs/manual.md:2141-2143`).

## What is missing

What the effort words mean:
- **small**: a few files, and the parts already exist.
- **medium**: new code in several places, or it must be timed on the handheld first.
- **large**: the engine and the song file both change.

### Expected and missing (high value)

| # | Feature | What it does for you | Effort |
| --- | --- | --- | --- |
| 1 | **SAVE AS A RECORDING** | One press saves what a lane or the sampler really plays as a new WAV: after EDIT, after FIT, a DRUM SYNTH voice, or one slice. Example: slice 5 of a break becomes a sampler sound, or a synth kick becomes a sampler bass. Today only CATCH and RECORD write files (`app/session_samples.cpp:536`, `:741`). The edited and fitted copies are already in memory (`app/session.hpp:325-330`). R1 and L1 are free in the EDIT view (`app/kit_screen.cpp:411-461`). | small |
| 2 | **Slices at the hits** | Cuts a break where its drums land, with a SENSITIVITY setting. A "transient" is the sharp start of a hit. Then the D-pad nudges, adds or deletes a slice point. Painting in order and the slice dice then give clean hits. Today there are 16 equal cuts (`app/session_samples.cpp:751-755`), fixed by hand one hit at a time (`app/pattern_screen.cpp:236-280`). The engine needs nothing new. | medium |
| 3 | **Load a folder as a kit** | Puts a folder's first 8 recordings on the 8 lanes in one press, by name where it helps: kick on lane 1, snare on 2, hats on 3 and 4. Folders could show on the KIT stop. Today that takes 8 trips into the list (`app/kit_screen.cpp:103-110`). | small |
| 4 | **Resample more** | CATCH takes 1, 2 or 4 bars, or only the track under the cursor, with no soloing first. BOUNCE THIS TRACK lands in `samples` and on a lane, fitted. Printing a busy track frees CPU, and the handheld is 13 to 20 times slower than the desk. Today CATCH is one bar of the whole mix (`engine/src/engine.cpp:241-261`). Its memory holds only 10 s, and 4 bars at 90 BPM is 10.7 s (`engine/include/apotheca/engine.hpp:153`). Bounces go to a folder the list can't see (`app/main.cpp:511`). | medium |
| 5 | **EDIT on the sampler** | The sampler gets the lane's EDIT: START/END to the millisecond, fades, GAIN, NORMALIZE, TRIM SILENCE, REVERSE. Today START and END move in whole percent, so 1% of a 20 s file is 200 ms (`engine/src/sampler.cpp:9-10`). Open question: how it applies to a multisample's notes. | medium |
| 6 | **A LOOP that works for pads** | A LOOP START apart from START, so the attack plays only once. A crossfade (a short blend) at the jump back. A short fade at END. Today the loop always goes back to START with a hard cut, and a note that reaches END stops dead (`engine/src/sampler.cpp:145`, `:153-155`). Drum lanes already fade their end (`engine/src/drums.cpp:368-369`). | small |

### Nice to have

| Feature | What it does for you | Effort |
| --- | --- | --- |
| FIT to any length, AUTO, tempo from a loop | Adds FIT stops at 3, 6, 12, 24 and 48 steps. AUTO picks the nearest whole number of bars. SET TEMPO FROM THIS LOOP sets the song's BPM from a loaded loop. This also fixes bug 2. Today the stops are 0, 1, 2, 4, 8, 16, 32, 64 (`app/kit_screen.hpp:80`). | small |
| Zoom while trimming | While START or END turns, the picture zooms to the few ms around that edge. Today it always shows the whole file: on a 20 s sound one pixel is about 33 ms, so EDIT's 1 ms step can't be seen (`app/ui.hpp:180-186`). | small |
| FIT by speed (TAPE) | A second kind of FIT that speeds the loop up or slows it down like a turntable, so the pitch moves too. The attacks stay clean, you get the sped-up break sound, and it is far cheaper than a stretch. Today FIT always keeps the pitch (`app/session_samples.cpp:836`). | small |
| Dice for the sound | Y in the list puts a random recording from this folder on the lane. Y again rolls another. Today Y in the list only deletes a letter while typing (`app/sample_picker.cpp:339`). | small |
| Copy or swap a lane's sound | Copies a lane's whole sound to another lane, or swaps two lanes. Example: a second snare a semitone down for fills. R1 on a lane is RECORD today (`app/kit_screen.cpp:256`), so the gesture needs care. | small |
| Rename on the handheld, numbers in order | Rename a sound from the list with the typing keys. "catch 2" lists before "catch 10", so the newest is last. Today renaming is WiFi only, and the list sorts as plain text (`app/samples.cpp:164`). Needs a free button that keeps the button standards. | small |
| DECAY per hit | Sets a hit's own length, 100% down to 5%, with X held like CUT. Today CUT has three stops (`engine/include/apotheca/drums.hpp:47-49`). It is already designed: `docs/plans/big-features-plan.md:181`. | small |
| More sampler play modes | PING-PONG (forward, then back), REVERSE, and ONE SHOT (plays to END after the key is let go). These are appended values, so old songs don't change. Today LOOP is 0 or 1, and letting go of a key always starts the release (`engine/src/sampler.cpp:10`, `:118-121`). | small |
| FILTER in a lane's EDIT | A low cut and a high cut, worked once into the lane's sound. Today a FILTER effect takes a slot for the whole track, all 8 lanes (`app/lane_edit.hpp:47-55`). It needs a third row in the EDIT view. | small |
| Cleaner pitch on sampler notes and pitched hits | Reads between samples with a 4-point curve (Hermite) instead of a straight line. That gives fewer false high tones ("aliasing") pitched up, and less dullness pitched down. CLOUD has it already (`engine/src/cloud.cpp:229-239`). **Your call**: pitched notes in old songs would sound slightly different, against the "same to the bit" rule. | small |
| RECORD in time, with a meter | Starts on the next bar after the count-in, runs 1, 2 or 4 bars, and lands fitted as CATCH does. A meter shows the level, and the start level can be set. Today it starts on any sound above a fixed 0.02, and nothing draws `recordLevel()` (`app/recorder.hpp:30`, `app/session.hpp:406`). **First check that a USB mic works on the handheld at all.** | medium |
| MODs on the sampler | MOD 1 and 2 can aim at START, PITCH and LEVEL. A random START on every note is a quick chop machine. Today `setMacro` is empty (`engine/include/apotheca/sampler.hpp:80`). | medium |
| Sampler DECAY/SUSTAIN and a filter per note | A full envelope and a low-pass on each note, opened by velocity. A held sample becomes a plucked bass. Today decay is 0 and sustain is 100 (`engine/src/sampler.cpp:37`). 8 voices of filter on the handheld: time it first. | medium |
| FIT on the sampler and CLOUD | A loop played from a key stays in time when the tempo moves. Today FIT is for drum lanes only (`app/project.hpp:325`). | medium |
| Root note found from the sound | A take with no note in its name is measured, so middle C plays it in key. Today it gets root 60 (`app/samples.cpp:229-264`). | medium |

### Probably not for a handheld

| Feature | What it does for you | Effort |
| --- | --- | --- |
| Typed kit names | DUSTY 808 instead of KIT 3. Typing is slow on buttons. It was put off in `docs/plans/easywins-plan.md:405`. | small |
| A "this song" row in the list | Lists only the recordings this song already uses. The preview already counts the uses (`app/sample_picker.cpp:749-754`). | small |
| Undo a recording swap | Puts back the old recording with its START/END cut. It is left out of undo on purpose today (`app/session_samples.cpp:355-357`). B in the open list already puts the old one back. | medium |
| AIFF, FLAC, and WAV loop points | Reads AIFF and FLAC, and a WAV's loop points and root note. Today it reads WAV only, and only the audio and format parts of it (`app/samples.cpp:50-73`). Loop points alone would be small; FLAC needs a decoder library. | medium |
| Sample locks | One hit plays a different recording from its lane's. Lanes, slices and hit START already cover most of this (`engine/include/apotheca/drums.hpp:41-47`). | large |

### Already there (dropped from the list)

- **Undo for FIT and lane VOLUME**: already undone (`app/session.cpp:1143`). Only swapping the recording itself is not.
- **Choosing the slice count**: partly there. You get 16 slices, or 32/64 when FIT is longer than a bar (`app/session_samples.cpp:751-755`). There is no free choice of 4 or 8, and the sampler's CHOP is always 16.

## The top 3 to build first

Fix the three bugs first; each is a few lines.

1. **SAVE AS A RECORDING**: it is small, and it makes every edit, fit, slice and synth voice usable on any track.
2. **Slices at the hits**: chopping breaks is the heart of sampling, and today's fix (sliding each hit by hand) is the slowest job on buttons.
3. **Load a folder as a kit**: eight trips into the list become one press, for small effort.