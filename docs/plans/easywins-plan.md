# The easy wins

The owner, 2026-11-17: "get the easy wins first". Eight small features, one batch each, on branch
`easywins`. Each section says what it is, the gesture, and the owner's calls with the answer taken
(my recommendation, taken so the work could go on: the owner may turn any of them over).

Nobody has heard or held any of it on the handheld. What the tests see is said in each section.

## Batch 1: TAP TEMPO

**What**: A tapped in time on a TEMPO row sets the tempo from the gaps between the taps.

**Where it is tapped**: the two places the cursor can stand on the tempo:

- the drum grid's tempo row (up from the top lane), where hold A + a direction already turns it;
- the SET page's TEMPO row, where A did nothing.

The SONG screen has no place for the cursor on the tempo (the top bar's box is only drawn), so it
has no tap; SEL + the D-pad turns the tempo there, as on every screen.

**The rule** (`app/tap_tempo.hpp`, one class, no screen in it):

- the tempo is 60000 over the mean of the last four gaps (five taps), rounded to a whole beat a
  minute and held to 40..240;
- a gap over two seconds is not a gap: that tap starts a new run (at 40 BPM a beat is 1.5 s);
- the first tap of a run changes nothing and says nothing (hold A to turn the tempo begins with a
  press, which is a tap); each after it says TAPPED: 117 BPM in the App's box above the bar
  (`Session::tapTempo`, `Session::say`).

A tap is timed by the frame it lands on (16 ms). Over four gaps at 120 BPM that is within a beat a
minute.

**The owner's calls**

| Call | Taken |
| --- | --- |
| Which button | **A**, on the row. It is the big button, it is what acts on a row, and on both rows a tap of it did nothing. Hold A + a direction still turns the tempo on the drum grid (the press that begins the hold counts as a tap; a single tap changes nothing) |
| How many taps are averaged | **The last five** (four gaps): steady after a bar, and it follows a change in a few taps |
| A long gap | **Over 2 s starts again** |
| Is it a step of undo | **No**: the tempo is not undone (as SEL + the D-pad) |

**Tests**: `easy_test taptempo`: the rule by hand (the mean, the last four only, the gap, the
ends, a clock gone back), and through the real screens: A tapped on the drum grid's tempo row and on
SET's TEMPO, the words said, hold A + UP still turning, A on a lane's name and on another SET row
tapping nothing.

**Only the owner can tell**: whether tapping on the handheld's A lands where the hand means it (a
frame's 16 ms), and whether five taps is the right number.

## Batch 2: the METRONOME and the COUNT IN

**What**: a click on every beat while the song plays, and a bar of clicks before recording from a USB
keyboard starts.

**Where**: two rows on the SET page, among the handheld's own (after USB KEYBOARD), kept in
`settings.txt` (`click N`, `countin N`), not in the song:

| Row | Choices | |
| --- | --- | --- |
| CLICK | OFF, ON, RECORDING | RECORDING: only while the song plays with USB KEYBOARD at PLAY + RECORD |
| COUNT IN | OFF, ONE BAR | Only with PLAY + RECORD: START then clicks a bar before the song's step 1 |

While a count runs the box above the bar says COUNT IN 4, 3, 2, 1 (in brass); START again calls it
off.

**The engine** (`Command::SetClick`, appended; `Play`'s `d` is the count's steps):

- the click is a 30 ms sine, 1.5 kHz on a beat and 2.5 kHz (louder) on a bar's first, a table worked
  out once in `prepare()` and read a sample at a time, so it is the same in any blocks;
- it starts on the first sample of steps 1, 5, 9 and 13 (`fireStep`), and is added **after Catch and
  before the volume**: CATCH never takes it, the volume turns it down;
- **off, with nothing sounding, nothing is added**: the output is as it always was, to the bit;
- the COUNT IN keeps the Play aside (`countPlay_`), clicks its four beats on the step clock's own
  count, and the song's step 1 begins on the sample after its last step (`startPlaying`), as a Play
  taken there would. Stop or Panic calls it off; a tempo change keeps its place in the step. The Play
  is counted as taken when it arrives, so AT END STOP's count of Plays still agrees;
- **a bounce has neither**: its engine is its own and is never told (`Session::syncClick` tells only
  the live one).

**The owner's calls**

| Call | Taken |
| --- | --- |
| A song setting or the handheld's | **The handheld's** (settings.txt), as USB KEYBOARD: a click is how you play, not the song |
| CLICK's choices | **OFF, ON, RECORDING** |
| The sound | **A short sine, higher on the bar** (1.5 and 2.5 kHz, 30 ms) |
| Where in the sound | **After Catch, before the volume**: catching a bar never takes the click |
| Count in for what | **Only for PLAY + RECORD** (the keyboard): START with PLAY ONLY plays at once, as before |
| How long | **One bar** (four beats), whatever the scene's length |

**Tests**: `easy_test click`: off is the song to the bit (an engine never told and one told on and
off); each click on its beat's first sample, 30 ms, nothing between, both sides alike, the bar's
louder; blocks of 1, 64, 333 and 1024 the same; Catch silent; turned on while playing; the count's
four clicks then the song to the bit beside one played a bar later by hand, in any blocks; the beats
to go shown; Stop meanwhile (and AT END's count after it); the tempo turned meanwhile; the two rows
and when the engine is told (RECORDING with and without PLAY + RECORD); START counting, START again
calling it off, PLAY ONLY at once; settings.txt; a bounce with CLICK ON and COUNT IN on that has
neither. `app_test` steps through the two rows. Stills `setup-click`.

**Only the owner can tell**: whether the click is too loud, too thin or too bright beside the songs,
and whether a bar is enough to come in on.

## Batch 2b: the STICK LIGHT (owner's feedback, 2026-11-17)

The owner, on the handheld: ON and BEAT work; "STEPS is a little weird, it seems to complete all the
steps before the scene is up"; "is there a way that we can get some type of color control for it?
even if it's premade colors."

**STEPS once round a SCENE.** The sixteen LEDs are the scene's length (its bars x 16, or the steps it
is cut to); the lit LED is the share of the scene played (`RingPlace`: the bar's step, the scene's
step, the scene's steps). A one-bar scene is a LED a step, as before. **The brass mark is on the step
a bar begins** (the lit LED is brass for that step), so a four-bar scene shows its four bar lines as
the ring goes round.

**LIGHT COLOUR**, a SET row after STICK LIGHT, shown only where STICK LIGHT is: THEME (as before: the
theme's cursor colour), TEAL, PURPLE, BRASS, RED, ORANGE, YELLOW, GREEN, BLUE, PINK, WHITE, TRACK,
RAINBOW. LEFT / RIGHT step it, as every choice on the SET page (the coordinator wrote "hold A +
left / right"; no SET row takes A for a choice, so the page's own pattern was kept). Kept in
settings.txt as `lightcolour N` (appended), **left out at THEME**.

| Call | Taken |
| --- | --- |
| Where the brass goes in STEPS | **The step a bar begins** (one step of brass at each bar line) |
| TRACK | **Twelve hues round the wheel, by the track's number** (track 1 red): there was no colour of a track to take |
| RAINBOW | **A hue a LED, turned a LED a beat while the song plays**, still while stopped |
| The bar's first in BEAT and STEPS on BRASS | **White** (brass on brass could not be seen) |

**Tests**: `lights_test testScenesAndColours`: a four-bar scene step by step, a scene cut to 24
steps, a one-bar scene as the old picture to the LED (BEAT too), each colour, TRACK's and RAINBOW's
hues and RAINBOW's turn, ON and the dim LEDs in RAINBOW; through the Session a two-bar scene round
once in two bars in RED with brass at the bar lines; `lightcolour` written, left out at THEME, read
back, refused out of range. `testTheSetPage` steps the row. Still `setup-light-colour`.

**Only the owner can tell**: how the colours look on the ring (the order R G B is still not known
there: a RED that shows green says the order is wrong), and whether RAINBOW's turn is too busy.

## Batch 3: TRANSPOSE

**What**: every note and chord of a synth (or sampler) track's cell in a scene moved by semitones.

**The gesture**: on the SONG screen, **hold A on a cell + UP / DOWN**: a semitone a press. **The
stick up / down with A held: an octave a push** (stick for big moves, D-pad for exact). Left / right
with A held is still the cell's length. Up / down with A held on a cell did nothing before. The box
above the bar says how far this hold has moved it (MOVED +3 SEMITONES, BACK WHERE IT WAS). The bar
says UP / DOWN transpose on a synth cell (`gHintsOver` 0).

**The rule** (`Session::transposeCell`):

- every bar of the cell, the hidden ones too (a cell made shorter keeps them, and they come back
  as they were moved);
- a chord moves its root, and its NAME (`transposeChordName`: a name that starts with a key, and the
  bass after a "/", take the key moved to, spelt as the packs spell keys; a roman numeral is left);
- **all or nothing**: if any key, a chord's inversions included, would pass C0 or C8 (the roll's
  keys), nothing moves and the box says NO ROOM: C0 TO C8 ONLY;
- a drum track does not move (DRUMS DO NOT TRANSPOSE); an empty cell says NO NOTES TO MOVE, and no
  scene is made for it (64 scenes: a scene's patterns are made when first written);
- the engine gets the cell (`sendNotePattern`), and it is a step of undo, as any edit. A hold's moves
  fall in one burst of edits, so one undo takes the hold back.

| Call | Taken |
| --- | --- |
| Where | **SONG, hold A on a cell** (the roll has no free direction with A held; a cell is what is moved) |
| One track or the whole scene | **One cell**. The whole scene's synth tracks would need a gesture on the scene's row, and the repeat count's A + up / down is taken |
| Out of range | **Nothing moves**, rather than a note held at the edge (that would change the line) |
| A held D-pad | **Does not run on**: a semitone a press; the stick is the big move |
| Chord names | **Moved with the chord** (Am up 3 is Cm) |

**Tests**: `easy_test transpose`: chord names (sharps, flats, slash chords, numerals); a cell's two
bars and its chord moved, moved back to the byte, undone, refused at C8 and at C0, a drum track, an
empty scene (none made); the engine's sound beside a cell written moved by hand, to the bit; through
SONG: UP twice, DOWN, the stick an octave, the words, LEFT still the length, letting go not opening
the cell, BACK WHERE IT WAS, a drum cell, one undo for the hold. Mutants tried: the turned flag, each
end of the range, the chord's name, the bars, the slash: each fails a check.

**Only the owner can tell**: whether a semitone a press and an octave a push are the right steps.

## Batch 4: SCALE LOCK

**What**: the song's KEY and SCALE; with one on, the piano roll keeps to it.

**Where it is set**: two SET rows in PROJECT, after CHORD PACK: **KEY** (C to B, round) and **SCALE**
(OFF, MAJOR, MINOR, DORIAN, PHRYGIAN, LYDIAN, MIXOLYDIAN, LOCRIAN, MAJOR PENT, MINOR PENT, HARMONIC
MINOR, BLUES; a choice with ends). OFF is chromatic: every key. `app/scales.hpp` is the one table.

**On the roll** (`NoteScreen::keyStop`, `nextKey`, `shapeAt`):

- **the cursor steps left / right only through the scale's keys**, the stick too; **a key a note or
  chord of the bar shown is on is a stop as well**, so a note out of the scale can still be reached,
  heard and deleted;
- **the keys out of the scale are drawn dim** (their cells the background's, with the panel's line:
  a gap, on every theme);
- **a note carried sideways (hold R1) goes to the next key of the scale** (a chord still a semitone:
  moved by scale steps its kind would change);
- **the chord in hand is the pack's moved into the KEY** (`packShift`): the pack's tonic lined up with
  the KEY, and a major pack under a minor scale moved to its relative major (a minor pack under a major
  scale to its relative minor), so C major's chords in A MINOR stay C, Dm, Em... and in D MAJOR are D,
  Em, F#m. The names move with them (`transposeChordName`); `ChordPack::tonic` and `minor` are read
  from the pack's file. A chord swapped in place (Y on a chord) takes the moved one too.

**In the song file**: `scale KEY KIND` among the song's own lines, **left out at OFF**; a KEY out of
range is held to 0..11, a kind nobody knows is OFF. A step of undo (`setScale` calls `noteEdit`;
`restorePatterns` puts the head's back).

| Call | Taken |
| --- | --- |
| The song's or a track's | **The song's**: one key for every synth track, set once |
| Where | **SET page, two rows** (the page's own pattern; the roll has no free button) |
| Out-of-scale keys | **Dim and passed by**, not hidden: the roll keeps its layout (the look is settled) |
| A note already out of the scale | **Kept, and the cursor still stops on it** |
| "Chords fit it" | **The pack moved into the key** (relative major / minor), not each chord bent to the scale |
| Undo | **Yes**, as AT END |

**Tests**: `easy_test scale`: each scale's count of keys, D major's keys, harmonic minor, blues,
minor or not, kinds out of range, key names; `packShift` for major, minor and relative; a pack's
tonic and minor from its file; the song line written, left out at OFF, read back, a key held, a kind
nobody knows, a short line; undo; the SET rows (KEY round, SCALE's ends); the roll: off every key, D
MAJOR's steps, a note out of the scale a stop, the keyboard's end, a carried note to E then F#, the
chord in hand in D major, A minor, off, and placed with Y. `app_test` steps the rows. Stills
`scale-roll`, `setup-scale`. Mutants tried: the note stop, the carry, the undo, the shift.

**Only the owner can tell**: whether the dim keys read well on the handheld's screen, and whether
moving the pack into the key is what "fit" meant (the other reading, each chord bent to the scale,
changes chords' kinds).
