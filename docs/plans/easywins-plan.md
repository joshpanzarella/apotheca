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
