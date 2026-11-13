# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

A small song machine that runs, at 48 kHz. Up to twelve tracks, each a drum
kit, a sampler, or one of two synths: Phazed (phase distortion, 65 presets) or Twin
Analog (two morphing oscillators, 16 presets). Up to sixteen scenes of
one to eight bars, each with its own repeat count. Four screens:

- **SONG**: tracks across, scenes down. Add tracks, set how many times each
  scene plays, copy bars by dragging.
- **PATTERN**: a drum grid (levels, nudge, chance, paint, lane dice) or, for
  a synth, a piano roll on its side (keys across, time down) with chords
  from chord packs.
- **DEVICE**: for a synth, a main panel of folder, preset and eight macros,
  with advanced pages behind it (nine for Phazed, three for Twin Analog).
  For drums, the kit: each lane plays its built-in drum or a recording.
- **FX**: three effects on each track, two on the master, and two shared ones (ECHO and SPACE) that every track can send to.

Songs are kept as you go: there is no save button. A **rev** keeps the song
as it is at one moment, to go back to.

While it plays, the top bar of every screen shows which scene is playing and
how far through it is: one block for each time the scene plays, filling as
the bar goes by.

Not built yet: the sound designer web page, and the handheld making its own
WiFi hotspot.

## Build

Needs CMake, a C++17 compiler and SDL2 (`brew install cmake sdl2` on macOS,
`apt install cmake libsdl2-dev` on Debian/Ubuntu).

```
cmake -B build
cmake --build build
ctest --test-dir build      # engine, song, both synths, screens, app, saving, chord, sample and transfer tests
./build/apotheca            # the app
```

## Controls

**The first start on the handheld sets up the controls.** Apotheca asks for
each button in turn ("PRESS A", "PRESS B"...) and learns what this handheld
sends for it, whatever the firmware calls it. It takes about twenty seconds,
once. MENU and the stick may be left out by waiting eight seconds, for a
device that has none or keeps MENU for itself. The answers are kept in
`controls.txt` in the `apotheca` folder.

**To set them up again** (another handheld, or a button learned wrongly),
any of three ways:

- **SETUP screen, CONTROLS row:** hold A for a second.
- **Just after Apotheca starts,** hold any button for two seconds. A brass
  line at the bottom of the screen says so for the first five seconds. This
  reads the buttons raw, so it works even when none of them is understood.
- Delete `controls.txt`.

The setup learns triggers that rest at one end of their travel (many L2 / R2
do) as well as ones that rest in the middle. SETUP also has a **button
test**: it lights each button while Apotheca hears it held. One that stays
lit, or never lights, was learned wrongly.
The bottom of the setup screen shows what the handheld last sent: if it
stays at "nothing yet" while you press buttons, the firmware is not passing
them on, and `apotheca.log` and `device-info.log` will say what it has.

**The handheld comes first.** Every action is defined on the Anbernic
RG40XXV's own buttons, named as printed on it. The keyboard is only a
stand-in for testing at a desk: each key presses one handheld button, so the
keyboard can do nothing the handheld cannot.

**The handheld has one stick, and one rule for it.** With nothing held it
moves the cursor (so does the D-pad; hold either and it runs). Hold a button
and the stick shapes the thing under the cursor:

| Held | The stick |
| --- | --- |
| Nothing | Moves the cursor |
| A | Turns the value: it fills or drains |
| Y | Chance, on the drum grid |

**Hold MENU (the small button in the middle) and a help card comes up**: what the buttons do
on every screen (X hears what is under the cursor, the stick and D-pad
move, A, B, START, L2 / R2, what SELECT does with other buttons, how to
stop all sound), and the **battery**. Let go and it closes. On a handheld whose firmware keeps MENU for
itself, holding SELECT for a moment does it instead. The bar at the bottom of each screen lists
only what is particular to where the cursor is. A direction that either the
stick or the D-pad gives is written as the direction alone (UP / DOWN); the
two are named only where they do different things. The bar never runs off
the screen: a hint that does not fit is left out.

**While X is held on a hit, a note or a chord, each button stands right under
the thing it sets**, with no word beside it: the name above it says what it
is. The arrows under RATCHET set the ratchet, A under THROW throws. Y and the
die at the left roll all of it. A field with nothing under it (NUDGE on a
note) is not set from there. Where THROW stands at the right-hand end (a drum
hit, a chord) its button takes the place of the words about the help card.

**SELECT + B is undo; SELECT + A puts it back.** Twenty steps deep. One
step is one burst of editing: a paint stroke, a roll of the dice, a cell
emptied, a chord placed, a pass of automation. It covers what is in the
patterns (hits, notes, chords, automation, how long each cell is, how often
each scene plays); sounds, effects
and settings are not part of it. The song keeps playing while you undo.

**L2 and R2** walk along the screens: SONG, PATTERN, DEVICE, FX, SET.
**SELECT**, tapped by itself, goes to SONG from anywhere (and on SONG opens
the list of songs). **START** plays: the song from
the SONG screen, the scene being worked on from the others. **SELECT + START**
quits.

### DEVICE, the sampler

A fourth instrument in the + TRACK list. **One recording, played from the
keys**: the piano roll and the chord button work on it as on a synth. Middle C
plays the recording as it was made; an octave up, twice as fast.

| On | Hold A + up / down | B |
| --- | --- | --- |
| SOUND | **Tap A: the list of recordings.** Or hold A + up / down: one at a time | None |
| PITCH, TUNE, START, ATTACK, RELEASE, LOOP, LEVEL | The stick turns it; the D-pad moves it by one | Its usual value |

**PITCH is semitones up or down, and up is higher.** (It used to be ROOT, a
key, which ran the other way. Old songs open as they were.) **X hears it at
middle C**: its own pitch at PITCH 0, and the change when PITCH or TUNE is
moved. A setting is also heard as it is changed, when the song is stopped.

**Y is Catch** here too: the last bar the song played becomes this track's
recording. Eight notes at a time.

### The list of recordings

**Tap A on SOUND** (a kit lane, or the sampler) and the recordings come up
as a list, with the one showing drawn as a waveform and its length.

| Button | Does |
| --- | --- |
| Up / down | The next one. It goes on **at once**: with the song playing you hear it in the beat; stopped, it plays once. The stick runs through about eight a second. |
| Left / right | Eight at a time |
| **L1 / R1** | The folder before / after (when there are folders) |
| A | Keep it |
| B | A tap: put back what was there before the list opened |
| X | Hear it again |
| **Hold B** | For a second: **delete** the recording under the cursor from the handheld (a bad catch, a take not wanted). Lanes that were using it go back to their drum. |
| START | Play / stop, with the list still open |

**Folders.** Make folders inside `samples` (kicks, breaks, vocals...) and the
list shows one folder at a time, its name on a bar at the top. L1 and R1 turn
them; arriving in a folder puts its first recording on. Recordings not in a
folder are in one called UNSORTED. One level of folders: a folder inside a
folder is not looked into. Catches go in `catches`, takes from a microphone
in `recorded`, by themselves. A song keeps a recording as `folder/name`.

**A recording that has moved is found again.** When a song is opened and a
recording is not where the song says, Apotheca looks through `samples` and
its folders for one of the same name (the part after the folder). If there is
exactly one, the lane or sampler track takes it and the song is saved with the
new place. If there are two of that name, or none, it does not guess: the lane
says NOT IN THE SAMPLES FOLDER and its built-in drum plays; a sampler track is
silent. So files can be sorted into folders on a computer, with the card out,
and the songs still find them. Renaming a file that way does break the link
(rename on the WiFi page instead: see Files over WiFi).

**A line runs across the picture while a recording is heard**, here and on
the kit's rows and the sampler's own picture. It is timed, not measured:
right for a recording played as it is, near enough for one tuned up or down.

### FX

A fourth screen, after DEVICE. **Two columns, and everything is in view.**

| Column | Holds |
| --- | --- |
| Left: the list | Eight lines, always there. **THIS TRACK**: its three effects, one after the other, then its **SENDS** (how much of it goes to ECHO, how much to SPACE). **BUSES**: what **ECHO** and **SPACE** hold, one effect each, shared by every track. **MASTER**: the two effects on everything, after the tracks are mixed. |
| Right: the line picked | Large. A heading (where it is, the effect, its TYPE, what it is doing just now), then each setting as a wide row: its name, its value, a bar. For SENDS: the two sends. |

Each line of the list says what it holds (DELAY TAPE, or EMPTY). The small
upright mark at its end is how much of the effect there is (its MIX, or a
bus's LEVEL). COMP and DUCK have a second, brass mark: what they are doing
to the sound just now.

| Where | Button | Does |
| --- | --- | --- |
| The list | Up / down | The next line. All eight, round the ends. |
| | Right | Into the line's settings, **at the one last used there** |
| | **Hold A** | **The list of effects opens on the right.** Up / down steps through it, and the effect goes in at once at its usual settings. |
| | Hold A + left / right | The **TYPE**, for an effect that has types |
| | B | Empties the line. On SENDS: nothing sent. |
| The settings | Up / down | The next setting |
| | Left | Back to the list, on the same line |
| | Hold A + up / down | The stick turns it. The D-pad moves it a twentieth, or to the next choice. |
| | B | Its usual value. A send: nothing sent. |

X hears the track and START plays, as everywhere. Y does nothing here.

**The list of effects**, in its groups:

| Group | Effects |
| --- | --- |
| FILTER | FILTER |
| DELAY | **DELAY**, with a TYPE: **CLEAN**, **TAPE** or **PING** |
| REVERB | **REVERB**, with a TYPE: **ROOM**, **HALL**, **SPRING** or **PLATE** |
| MOVING | CHORUS |
| LEVEL | COMP, DUCK |
| DIRT | SATURATE, DRIVE, CRUSH |

**Changing TYPE keeps what the two types share.** TIME, FEEDBACK and MIX go
from CLEAN to TAPE and back (and to PING, with TONE too between CLEAN and
PING); SIZE and MIX from ROOM to HALL; DECAY, TONE and MIX from HALL to
SPRING (from ROOM to SPRING, MIX alone), and from HALL or SPRING to PLATE
(from ROOM to PLATE, MIX alone). They are kept
**as they read**: TAPE's FEEDBACK goes to 120% and CLEAN's to 100%, and 48%
stays 48%. What the other type cannot read goes to the nearest it can (120%
becomes 100%; a triplet TIME becomes the nearest plain one). A setting the
new type does not have is dropped, and its own extra settings start at
their usual values. Automation follows the settings that are kept.

A setting that is a choice (a TIME, a filter's TYPE, COMP's RATIO) has no
bar to be somewhere along. It has **one piece for each choice, and the one
it is on is lit**.

**A setting that moves with the pattern** (see Automation) has a purple
line under its row and says so; its line in the list has a purple line too.

#### ECHO and SPACE: one delay and one reverb for the whole song

A **send** is a copy of a track fed to a shared effect. ECHO holds a delay,
SPACE holds a reverb. Each track says how much of itself goes to each, from
0 to 100%, on its SENDS line. Both start at 0.

| | An effect on the track | A send |
| --- | --- | --- |
| How many run | One for each track that has it | One for the whole song |
| How much | Its MIX | The track's ECHO or SPACE amount |
| The sound | Each track in its own room | Every track in the same room |

- **What ECHO and SPACE hold is in the list, under BUSES.** Their settings
  are the delay's and the reverb's, but for the last: **LEVEL**, how loud the
  echoes or the room come back. It starts at 100%.
- ECHO takes only a DELAY (CLEAN, TAPE or PING) and SPACE only a REVERB (ROOM,
  HALL, SPRING or PLATE): hold A there and that is all the list offers. B empties one.
- On ECHO or SPACE an effect gives back only what it adds: the echoes or the
  room, never the sound itself a second time.
- **LEVEL turned right down keeps the echoes going round.** Turn it up again
  and they are still there. A bus goes quiet for good only when its echoes
  have really died away.
- The send is taken after the track's level and its mute: a muted track sends
  nothing, and the echoes it already made ring on.
- The master's effects come after, so they act on the echoes and the room too.
- A send can be **automated** like any setting (see Automation).
- **THROW** sends one drum hit or one note in full, whatever the SENDS line
  says: hold X on it and press A. See "THROW" under PATTERN.
- **A new song has DELAY (TAPE) on ECHO and REVERB (HALL) on SPACE.** So does a song made
  before there were sends: nothing in it is sent, so it sounds as it did, and
  the first send turned up in it gives what it gives in a new song. CLEAN and
  ROOM, the older two, are one push away (hold A + left on the line).
- **A song saved here and then opened by an older version of the program
  loses its sends, its throws, its third effects and what ECHO and SPACE hold.** The rest
  of it opens.

In the table below, **TAPE**, **PING** and **DELAY** are the DELAY's three types
(TAPE, PING and CLEAN), and **HALL**, **SPRING**, **PLATE** and **REVERB** the REVERB's (HALL,
SPRING, PLATE, and ROOM). A
song file calls each by the name it has always had.

| Effect | Settings |
| --- | --- |
| FILTER | Cutoff, resonance, type, mix. Types: LOW 12 DB (two poles, gentle), LOW 24 DB (four poles, steep), BAND PASS, HIGH PASS, and DJ: one knob, nothing in the middle, turned down it closes from the top, turned up it clears out the bottom. |
| SATURATE | Drive, type, tone, mix. Clean drive: the newer of the two, far smoother on high notes than DRIVE. DRIVE is how hard the sound is pushed; **the loudness stays about level as it rises**, so it is the grit that changes and not the volume (a quiet sound comes up, a loud one comes down). Types: TAPE (soft and even), TUBE (lopsided: warmer, with a touch of the octave), FOLD (past the top the wave folds back down: hollow, then metallic), HARD (straight, then flat: a clipper). TONE takes the top off afterwards. |
| DRIVE | Drive, tone, mix, output. The older one: rougher up high, and louder as it is driven. Songs made with it sound as they did. |
| TAPE | A tape echo: each repeat is duller, thinner and a little bent. **Time** (1/16, 1/8 triplet, 1/8, 1/4 triplet, dotted 1/8, 1/4, dotted 1/4, 1/2: it follows the tempo; a triplet, shown as TRIP, is three in the space of two; a dotted one, shown as DOT, is half as long again), **feedback** (to 120%), **age**, **mix**. Turn TIME while it sounds and the echoes swoop in pitch, as tape does when its speed changes. FEEDBACK past 100% howls and holds there; it does not blow up. **It does not howl for ever either**: while sound is coming in, and for 30 seconds after the last of it, FEEDBACK is what it reads; left alone longer than that, a FEEDBACK over 90% eases back to 90% and the echoes die away. Play into it again and it is back at once. A FEEDBACK that reads 90% or less is never touched. AGE is how worn the tape is: more wobble in pitch, duller and thinner repeats, a lower ceiling on the howl. MIX is how loud the echoes are; the sound itself stays as it is. |
| PING | A clean echo that bounces from side to side: the first repeat on the left, the next on the right, and so on. **Time** (TAPE's eight choices), **feedback** (0 to 90%: how much of each repeat is in the next; it reads what it does, and cannot ring for ever), **tone** (how bright the repeats stay: low is duller each time across), **mix**. A sound in the middle, or on one side only, bounces all the same. The first repeat is the sound exactly as it was. Turn TIME while it sounds and the echoes swoop in pitch, as TAPE's do. |
| DELAY (the type CLEAN) | Time (1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2: it follows the tempo; dotted is shown as DOT), feedback, tone, mix |
| HALL | A reverb with a dense tail that keeps moving. **Size** (the room, and the gap before it answers: 2 to 30 ms), **decay** (how long the tail lasts, in seconds: 0.3 to 20), **tone** (low is dark: the highs die soon; above the middle it is bright and the lows are thinned), **mix** (from the sound alone to the room alone). Turning SIZE while it sounds bends the room's pitch for a moment. |
| SPRING | A spring tank: the boing. In a spring the high notes travel slower than the low ones, so a click comes back as a quick sweep, low first, and each time round it is longer. **Drip** (how strong that sweep is; at none, plain echoes), **decay** (how long the tail lasts, in seconds: 0.4 to 4), **tone** (low is dark: the highs die soon), **mix**. Like a real one it passes little under 100 Hz or over about 4.5 kHz, takes the left in at one spring and the right at the other, and gives it back a little different on each side. Changing TYPE from HALL keeps DECAY as the seconds it reads, or the nearest SPRING has. |
| PLATE | A plate reverb: a dense wash from its first moment, with no early echoes to pick out, bright and smooth, and an even fall. **Pre** (a gap before it answers: 0 to 100 ms), **decay** (how long the tail lasts, in seconds: 0.3 to 20, as HALL's), **tone** (low is dark; high is bright; at any setting the lowest notes die a little sooner than the rest), **mix**. Left and right go in together, as on a real plate, and come out wide. Turning PRE while it sounds bends its pitch for a moment. |
| REVERB (the type ROOM) | The older, plainer reverb. Size, damping, mix |
| CHORUS | Rate, depth, type, mix. Copies of the sound, a little late and wavering, which makes one sound into several and spreads it left and right. RATE is how fast they waver (0.1 to 8 times a second), DEPTH how far. Types: CHORUS (three copies a side), ENSEMBLE (three copies, each at its own speed: thicker, less regular), VIBRATO (the pitch itself wavers; at full mix there is no plain sound), FLANGE (one very short copy fed back into itself: the jet sweep). |
| COMP | Threshold, ratio, speed, mix. A compressor: it turns the loud parts down and then the whole up, so a sound is more even and sits forward. THRESHOLD is how loud "loud" is (-48 to 0 dB: lower catches more). RATIO is how hard (1.5:1, 2:1, 3:1, 4:1, 8:1, 20:1). **While sound passes, a thin line under the THRESHOLD bar shows how far it is turning the sound down**, and the heading says it in words (TURNING DOWN 7 DB): it grows from the right, and the whole width is 24 dB. No line: the sound is under the threshold and nothing is done to it. SPEED is how fast it acts and lets go, as one knob: it reads both, from 1/50 MS (it catches every hit) to 30/400 MS (hits get through, the whole is held together). MIX part way keeps the plain sound beside the squashed one. The level comes back up by itself. It does not listen below 60 Hz, so a kick's thump does not pull everything down. **On the master it glues a mix together.** |
| DUCK | Amount, release, source. **The track dips each time a drum is hit**, and comes back up: the pumping sound, or room made for the kick. SOURCE is which drum: a lane, 1 to 8, of the **first drum track** in the song (lane 1 is the kick in the built-in kit). AMOUNT is how far it dips (100%: right down to nothing). RELEASE is how long it takes to come back, 20 ms to 1 second. It goes by the hit itself, not by the drum's sound: it works with the drums muted or that lane turned right down, every hit counts whatever its level (each hit of a ratchet, and a drum played by hand), and a step that its chance leaves out does nothing. **A thin line under the AMOUNT bar shows how far it is dipping**, and the heading says DIPPING 3 DB, as COMP's does. On the master it dips the whole song. Not for ECHO or SPACE. |
| CRUSH | Bits, rate, mix |

Effects are kept in the song. They can be changed while it plays. Stopping
all sound empties their tails.

TAPE, PING, HALL, SPRING, PLATE, CHORUS, COMP, SATURATE and DUCK are new. Their settings slide to where
they are turned instead of jumping, so they can be turned while sounding
without a click. A song that uses one, opened by an older version of the
program, loads without that effect: the slot is left as it was.

### Stop all sound

**Hold all four shoulder buttons (L1, L2, R1, R2).** The sound is shut off
within 5 milliseconds and stays off for as long as they are held. The song
stops, every drum is cut and every note is let go. It works on every screen
and changes nothing else.

Let go, and the sound opens again once whatever was left has died away, or
at the next thing you play, or after three seconds at the latest. The top
bar says SILENCED until it has.

A notice comes up (ALL SOUND STOPPED), in case it was an accident. Once
the buttons are let go, any button puts it away (that press does nothing
else), or it goes by itself after four seconds.

### SETUP (the SET tab)

**Which version this is** shows at the top right of the SET tab (and on the
help card, the WiFi page, and the first line of `apotheca.log`): `0.104`
and the day it was built. The number goes up by one with every build.

| Row | Left / right | A |
| --- | --- | --- |
| TEMPO | A beat a minute | |
| CHORD PACK | Another pack | |
| TRACK NAME | A name for the track being worked on, from a list | **Type one**: a grid of letters and numbers. A types, B deletes, Y keeps. Ten characters. |
| VOLUME | A twentieth | |
| STICK SPEED | 25% to 150%, kept for next time | |
| TEXT SIZE | SMALL, MEDIUM, LARGE or XL, kept for next time | |
| PIANO ROLL SHOWS | 1, 2 or 3 octaves | |
| USB KEYBOARD | PLAY ONLY, or PLAY + RECORD | |
| WIFI PAGE | | On / off |
| BUTTONS | | The button test |
| CONTROLS | | Hold a second: set the controls up again |

The first three belong to the song. The rest belong to the handheld and are
kept in `settings.txt` and `volume.txt` beside the program.

**TEXT SIZE** is how big the lettering is. It changes at once.

| Size | The three sizes of letter | |
| --- | --- | --- |
| SMALL | 12 / 14 / 16 | where Apotheca started |
| MEDIUM | 13.5 / 16 / 18.5 | |
| LARGE | 14.5 / 17 / 20 | what a new handheld gets; every screen was laid out at it |
| XL | 15.5 / 18.5 / 21.5 | a little bigger |

Some rows stay at LARGE whatever is chosen, because they were laid out to
the pixel: the bar across the top, the hints bar (and the buttons it draws
under a strip), the strip under the drum grid and under the piano roll, and
the controls setup. At XL a few words that would not fit their box stay at
LARGE too (a long recording's name, the help card's longest line): nothing
is cut short and nothing moves.

A still at a size, for looking: `--text-size small` (or `medium`, `large`,
`xl`) with `--view NAME --shot out.bmp`.

### The sound designer

A web page for building sounds on a computer's big screen. **The handheld
makes the sound**: the page is only the controls. Turn the WiFi page on
(SETUP), open its address in a browser, and follow "Open the sound
designer".

- It works on **the track being worked on** on the handheld, if that is a
  PHAZED or TWIN track. Move to another track there and the page follows.
- Every setting of the synth is on the page, in the same groups as the
  handheld's pages, with the eight macros at the top. Phazed's envelopes
  are drawn as lines. Turn something on the handheld and the page shows it.
- **Play it** by clicking the keys or with the computer's keyboard
  (A W S E D F T G Y H U J K; Z and X change octave).
- **SAVE AS PRESET** keeps the sound, macros included, under a name. It
  appears at once in the handheld's preset list, in a folder called USER,
  and is kept as a file in `presets` beside the program (`NAME.phazed`,
  `NAME.twin`). Saving under the same name again replaces it.
- TRACK NAME on the page can be anything; on the handheld it is picked from
  a list (SETUP).

A song keeps the whole sound of each track, not only the preset's number,
so a song still sounds right if a preset is later changed or removed.

### A scene of any length

A scene lasts as many steps as its longest cell has: 16 for a bar, 32 for
two. **It can be cut to any number**: 13, or 23 (a bar and seven steps).

- **On the piano roll or the drum grid: hold SELECT and press L2 (a step
  shorter) or R2 (a step longer).** A line says "SCENE 1: 23 STEPS OF 32".
- On the SONG screen: on the scene's `x2` box, hold A and press left / right.
  Up / down there is still how many times it plays.

The number is the whole scene, for every track in it. Steps past the end are
shaded on both grids and kept: lengthen it and they are back. A scene that is
cut short shows its number in its box on the SONG screen. If a cell is later
made longer, a scene that was never cut grows with it.

(Songs kept before this said "steps to a bar"; they open with the same
length they had for one-bar scenes.)

### Next scene: carry on from a copy

You have a loop going and want the next part of the song to start from it.

- **On the piano roll or the drum grid (its slices too): hold SELECT and
  press Y.** A line says "SCENE 2: COPY OF 1".
- The scene you are on is copied whole into a **new scene right after it**:
  every track's cell, how long the scene is, how many times it plays, its
  automation. The scenes after it move down one. Nothing is written over.
- **You are put on the new scene**, on the same screen, track, bar and
  cursor. Carry on adding; the scene before stays as it was.
- **A song that is playing does not miss a step.** The two scenes are the
  same, so it goes on from where it was, now in the new one.
- One press, nothing asked. **SELECT + B takes it back** (and puts you, and
  the playing, back on the scene you came from); SELECT + A does it again.
- A song holds 16 scenes. With the sixteenth in use it says "NO ROOM: 16
  SCENES" and changes nothing.

### Bouncing: getting a song out

**SET tab, BOUNCE row.** Left / right picks what, A starts it.

| What | Files written |
| --- | --- |
| THE SONG | One WAV of the whole song: every scene as many times as it plays, then **for as long as its echoes and tails take to die away**. Muted tracks are left out. |
| EACH TRACK | One WAV for every track by itself (stems), all the same length (the longest one's), so they line up in any other program |
| THIS TRACK | The track being worked on, by itself |

A bounce is **not recorded as the song plays**. A second engine works the
song out again from the start, as fast as the handheld can. So you can
carry on (even play) while it works, and it does not have to keep up:
**Twin Analog is always bounced at x8 oversampling**, even where that is more
than the handheld can play live.

**Where a bounce ends.** After the song's last step it runs on until there has
been no sound for four seconds, and the file ends half a second after the last
sound. "Sound" is anything a 16-bit file can hold (90 dB under full). So a
dry song ends just after its last drum has rung out, and a long HALL or a
TAPE with its FEEDBACK up is all there, however long it takes. Echoes far
apart (a half note at 40 BPM is three seconds) count as one tail.

There is a cap: **a tail is never longer than eight minutes**. One that would
be is faded out over its last second. No one delay or reverb rings that
long: at its most extreme settings and the slowest tempo (40 BPM) the longest
takes seven minutes to die away (TAPE, PING or DELAY at a half note with
FEEDBACK full), and at 120 BPM a third of that. HALL takes 45 seconds at most, PLATE
52, REVERB 36, SPRING 19. **Two in a row can**: a TAPE with FEEDBACK full in a track's slot feeding
another on the ECHO bus, at 40 BPM, would ring for about twelve minutes, since
the first keeps the second fed. That bounce stops at eight minutes, faded.

If the handheld loses power in the middle of a bounce, the half-written file
is removed the next time Apotheca opens, or the next time a bounce starts.

While it works the row says `WORKING 37%`: how much of the song's own playing
is done. In a tail it says `TAIL 12 S` instead: how many seconds of tail so
far. Nobody knows how long a tail will be until it ends, so there is no
percentage to give.

Files are 48 kHz, 16-bit stereo, in a `bounces` folder beside the program.
The WiFi page lists them under BOUNCES, to play, download or delete.

### Automation

A macro, or a setting of a track's effect, can be **moved by the pattern**.

1. **SELECT + X** turns WRITE on. A red WRITE chip shows in the top bar.
2. Play the song, and move the thing: hold A on a macro (DEVICE) or on an
   effect's setting or a send (FX) and turn it.
3. What you do is written into the cell that is playing, step by step.
   Turn WRITE off (SELECT + X again) and it plays back every time round.

| | |
| --- | --- |
| How fine | One value a step (16 a bar, for every bar of the cell). The setting **glides** to each value over the first half of its step, so a sweep is smooth, not a staircase. |
| Steps with nothing written | The setting rests where its knob is |
| A mark | A purple line under a macro or setting that has automation in the cell being worked on. While it plays, the box shows the value the pattern has it at. |
| Take it away | **B** on the macro, setting or send: back to its middle or default, and its automation in this cell is cleared |
| What can be automated | A synth's eight macros; the four settings of each of a track's three effects; its two sends. Not the master's effects, not what ECHO and SPACE hold, and not the settings on a synth's ADVANCED pages. |
| Undo | SELECT + B takes back a recorded pass like any other edit |

With WRITE off, moving a knob is only moving a knob. Stopped, nothing is
ever written. There is no screen yet for drawing automation step by step:
it is recorded by performing it.

### QMOD

An FM synth that is **rolled, not programmed**. FM is sine waves
("operators") bending each other's pitch very fast; which bends which is the
wiring, and it decides the sound. QMOD never asks you to wire anything: a
number generates a whole wiring of eight operators, from one of thirty-two
families. A second wiring, a cousin of the first, is generated beside it,
and ENTANGLE morphs from one to the other.

The generator is the one in the Entanglement module of the Shapetaker VCV
Rack plugin (`third_party/shapetaker`, unchanged). QMOD's voice is its own
and much lighter: eight notes at once cost about half what Twin Analog does.

| Control | Does |
| --- | --- |
| **Y** (on its DEVICE screen) | **ROLL**: a new sound, at once, and it is heard. Only the sound's numbers change; how bright it is and its envelope stay as set. |
| ENTANGLE | All the first wiring, to all the second |
| TENSION | How hard the operators bend each other: mellow to metallic |
| EVOLVE | How long that brightness lasts in each note: a quick pluck of it, or all the way through |
| VARIATION | Which cousin the second wiring is. Turning it flips through them. |
| SOUND NUMBER | 1 to 9999: every one is a playable sound (tested) |
| MUTATION | How far a cousin strays |
| AFFINITY | How much the generator likes feedback: rougher |
| SYMMETRY | Plain harmonic tunings, or bell-like ones |
| FAMILY | AUTO (whichever the sound's number gives), or one of the 32 by name |

The same number is always the same sound, so a rolled sound saved in a song
or as a preset comes back exactly. The sound designer page shows QMOD like
any other synth, with a ROLL A NEW SOUND button.

### The envelope

Twin Analog and the sampler share one volume envelope (and any synth added
later can use it), so a setting means the same on each:

| Setting | 0 | 50 | 100 |
| --- | --- | --- | --- |
| ATTACK | 1 ms | 89 ms | 8 s |
| DECAY, RELEASE | 2 ms | 200 ms | 20 s |

The attack is curved like an analog one: most of the rise comes early, so a
short attack is snappy, and it starts from wherever the envelope already is,
so a note played again does not click. Decay and release times are how long
the sound takes to fall by 60 dB. Phazed keeps its own eight-step envelopes.

### All four cores

The tracks do not depend on each other until they are mixed, so they are
worked out side by side: the audio thread and two helpers each take the next
track not yet done, and the results are mixed in track order. A heavy song
(six Twin Analog tracks at x4, a chord on each) took 55% of the time it had
on one core of the build machine and 23% with two helpers: about 2.4 times
the room. The sound is the same to the last bit, and a test holds it to
that. One core is left for the screen. Bounces use it too.

### Keeping up

Twin Analog's OVERSAMPLE setting costs a lot: x8 with two chords held is
more than the handheld can work out in time, and the sound stutters. So
Apotheca watches how long each block of sound takes. When it is falling
behind, it turns the oversampling down a notch at a time (the setting
itself is left alone) and the top bar says BUSY: OVERSAMPLING DOWN. After
ten seconds with time to spare it goes back up a notch.

### A USB keyboard

Plug a MIDI keyboard into the handheld's USB-C socket (through an adapter
if its plug is the big kind). Nothing to set up: Apotheca looks for one a
few times a second, and SETUP's USB KEYBOARD row says when it has found one.

- **It plays the track being worked on.** A synth track: the keys. A drum
  track: C2 upward is the eight drums, lane 1 first, round again every eight
  keys. How hard a key is hit is how loud.
- **On the piano roll the cursor goes to the last key played**, so A places
  a note there.
- **PLAY + RECORD:** while the song plays, what is played is also written
  into the cell that is playing: on the step it was played in, as long as
  it was held (whole steps, to the end of the bar at most).

The keys are read on a thread of their own and go straight to the sound, so
a note does not wait for the screen. Not tested on a real keyboard yet: the
tests use made-up ones. The handheld's log (`device-info.log`) lists any
MIDI device it sees.

### SONG

The track columns share the width of the screen: with few tracks they are wide.

| On | A | Hold A + stick | B | Hold R1 and drag |
| --- | --- | --- | --- | --- |
| A cell | Opens it on PATTERN | | Empties it | Copies it into every cell crossed |
| A track's head (up from the top scene, with the D-pad) | **A** types its name. **R1** changes its instrument: its notes, name and effects stay, and it starts on the new instrument's first sound. **Hold B** two seconds deletes the track. Y, L1 and X work there as on its cells. | | | |
| A track (any of its cells) | **Y** mutes it, **L1** solos it, **X** hears it. All at once, playing or not. A muted track's head shows M, a soloed one S. The song keeps mutes, not solos. | | | |
| A track | **Hold B for two seconds: delete the whole track.** (A tap of B only empties the cell.) After half a second its head starts to fill red; let go before it is full and nothing is deleted. The tracks after it move down. This cannot be undone, and the last track cannot be deleted. | | | |
| A repeat count | Tap: 1, 2, 4, 8 | Times the scene plays, 1 to 16 | Back to 1 | |
| + TRACK | Pick DRUMS, PHAZED, TWIN ANALOG, SAMPLER or QMOD | | | |

The song runs to the last scene with anything in it, then comes round.

### Longer phrases

A **cell** (one track in one scene) can be **1, 2, 4 or 8 bars** long.

- **Hold A on a cell and push the stick left / right** to set its length. A
  tap of A still opens it.
- **A cell made longer is filled with itself.** One bar to two: the second
  bar is the first again, hits, notes, step effects and automation, ready to
  be changed. Two to four: bars 3 and 4 are bars 1 and 2. Only empty bars are
  filled: a bar you wrote and then hid by shortening the cell comes back as
  it was. It is one step of undo.
- **The scene lasts as long as its longest cell.** A shorter cell goes round
  under it: a one-bar drum loop plays four times under a four-bar part.
- A shorter cell is drawn again for each time it goes round, a little dimmer,
  with a thin line where it starts over. A long cell shows its length in the corner.
- **The repeat count is whole passes of the scene.** A four-bar scene at x2
  is eight bars.
- On the PATTERN screen the grid shows one bar. Run the cursor off the end
  of step 16 and you are in the next bar; the top bar says **BAR 2 OF 4**.
  On the drum grid the lane's name is always one left of step 1.
- On the piano roll **a note may run on over the bar lines**, to the end of
  the cell. In the bars after, it shows faint from the top; it is changed
  in the bar it starts in.
- While it plays, the top bar shows the bar of the scene (2/4) beside the
  progress blocks.

**SELECT** lists the songs: start a new one, or open another.

In the list:

- **Y on the open song** gives it a new two-word name. Press again for another.
- **Hold B on a song for a second** deletes it; the row fills as you hold. A
  tap of B only closes the list. A deleted song and its revs are moved to a
  `deleted` folder inside `songs`, not destroyed.

**SELECT + D-pad** sets the tempo from any screen: up / down by 1, left /
right by 10.

**SELECT + L1 / R1** is the volume: quieter / louder, from any screen. A bar
shows it for a moment. The handheld's own volume buttons belong to its
firmware and may not reach a running program; if they do arrive as the usual
volume keys, they work too. It starts at 40% on the handheld and is
remembered between starts. Catch takes the sound before the volume, so a
quiet handheld still catches at full level.

The stick's speed, for everything it moves and turns on every screen, is one
number: `gStickSpeed` in `app/ui.hpp` (0.75: a quarter slower than first built).

### Files over WiFi

**L1 in the song list** turns it on. The list then shows an address, like
`192.168.1.23:8080/4821`. Open it in a browser on any computer or phone on
the same network.

- **Send files**: CHOOSE FILES, or drop them on the page: recordings
  (`.wav`), chord packs (`.json`), songs (`.song`). Many at once is fine;
  they go one after another, each with a bar and a result: `saved`, or
  `refused` and why. A recording is on the kit screen at once; a chord pack
  is in the chord button's list on the next frame.
- **A whole folder**: CHOOSE A FOLDER, or drop one on the page. Every `.wav`
  in it is sent, however deep its sub-folders go. The handheld keeps
  recordings one folder deep, so each sub-folder becomes one folder:
  `Pack/Drums/Kicks/808.wav` arrives as folder `Kicks`, file `808`. Two
  folders with one name get their parent's in front (`Drums Kicks`,
  `Vintage Kicks`). Names too long for the handheld (32 letters, 24 for a
  folder) are shortened by dropping words from the middle. **Nothing is sent
  until you have seen the plan**: how many, how big against the room on the
  card, which folders, which names change. Other files in the folder are left
  out. While it runs there is one row with a count and a STOP; only what is
  refused gets a row of its own.
- **SPEED TEST** (top of the page) times every instrument and effect on the
  device and shows the numbers in a new tab. Stop the song first; it takes up
  to a minute.
- **Recordings are shown by folder**, the loose ones as UNSORTED. Click a
  folder to open or shut it. FIND narrows the list by name.
- **To send into a folder**: drop the files on it, or press its ADD FILES
  (the way on a phone), or pick it under RECORDINGS GO IN. NEW FOLDER there
  takes a name; the folder is made when the first file lands in it.
- **PLAY** plays a recording or a bounce in the browser.
- **DOWNLOAD** fetches a file back: a catch, a recording, a song, a bounce.
- **Several at once.** Tick the box on each recording (or CHOOSE ALL on a
  folder; with something in the find box, that is every match). A bar comes up:
  pick a folder (or NEW FOLDER) and MOVE, or DELETE (it asks once more). They
  go one after another, each with its own line in the list at the top.
- **RENAME and MOVE** (recordings only) open a small form: the name, and the
  folder. On a computer a recording can also be dragged onto a folder.
  Nothing is written over: a name that is taken is refused. **The song that
  is open follows**: every lane and sampler track that used the old name uses
  the new one, the sound plays on without a gap, and the song is saved with
  it. **Every other song on the card follows too**, and every rev: each file
  that names the recording is rewritten with the new name and nothing else in
  it changes. It keeps its date, so the song list stays in the same order.
  Songs in `deleted` are left alone.
- **DELETE** asks once more in the row (DELETE IT / KEEP). A recording, chord
  pack or bounce is gone for good. A song goes to `deleted` in the song
  folder with its revs, the same as one deleted on the handheld. A folder
  goes with its last recording.
- **Deleting a recording the open song uses**: the song keeps the name. The
  lane says NOT IN THE SAMPLES FOLDER at once and its built-in drum plays; a
  sampler track is silent. Send a recording of that name again (into any
  folder, if it is the only one of that name) and it is back on the next
  frame. Deleting a recording changes no saved song.
- The top line shows the version and the room left on the card.
- The four digits on the end are made up each time it is turned on. A
  request without them is refused, so someone on the network who has not
  seen the handheld's screen cannot get in. It is not encryption: use it on
  a network you trust.
- Every file is checked before it is kept: a plain name of the right kind
  (no `..`, nothing hidden; a recording may be one folder down, nothing
  else may), 30 MB at most, and really a WAV, a chord pack or a song.
  Anything else is refused and nothing is written. Deleting, renaming and
  moving hold names to the same rules, and only touch a file that is there.
- It is off until turned on, and off again when Apotheca closes.
- The page is all in the handheld: it needs no internet.

For a script, the same things by hand (`ADDRESS` is what the screen shows):

| Job | Request |
| --- | --- |
| What is there | `GET ADDRESS/list` |
| Fetch | `GET ADDRESS/samples/kicks/808.wav` (also `songs`, `chords`, `bounces`) |
| Send | `PUT` to the same kind of address, the file as the body (not `bounces`) |
| Delete | `DELETE` the same address |
| Rename or move a recording | `POST` its address with a header `X-To: breaks/808.wav` (spaces as `%20`) |

**Updating Apotheca the same way.** The page has an UPDATE APOTHECA button:
choose the new `apotheca` program file (inside the `apotheca` folder of the
zip).

1. The page sends it. It is checked (a program for this processor, 30 MB at
   most) and only set aside. The network alone installs nothing.
2. The handheld shows AN UPDATE HAS ARRIVED. **Hold A for a second** to
   accept, or B to throw it away. Nothing else responds until it is answered.
3. Accepted, it is used the next time Apotheca starts. The program that was
   running is kept.
4. If the new one does not get going (it has to run for three seconds), the
   launcher goes back to the one before by itself and starts that.

One that nobody answers is dropped when WiFi is turned off or Apotheca is
closed. The first install still goes by SD card. Only the program is
updated this way: a new launcher script, fonts or chord packs still come
from the zip.

The handheld has to be on a WiFi network already (set in the firmware's own
settings). Making its own hotspot, so no router is needed, is a job for the
Apotheca-only system image.

### Revs

A rev is a snapshot: the song as it is now, kept beside it and never written
to again.

- **SELECT, then X** saves a rev.
- **SELECT, then A on the open song** lists its revs. A on one goes back to it.
- **Going back loses nothing.** If no rev holds the song as it is now, it is
  kept as a new rev first.
- Revs are files named `<song>.<number>.rev` in the `songs` folder, in the
  same text form as a song.

### Songs

- **Nothing to save.** The song is written to its file a couple of seconds
  after any change, and again on the way out.
- **The last song open comes back** at the next start.
- **New songs name themselves** with two words ("brass moth"), so nothing has
  to be typed on a D-pad.
- **Files** live in a `songs` folder beside the program, one `.song` text
  file each. They can be read by eye, copied and backed up. The format is
  described at the top of `app/song_file.hpp`.

### PATTERN, drums

| Gesture | Sets | The block |
| --- | --- | --- |
| Hold A, stick up / down | Level | Fills or drains |
| Hold A, stick left / right | Nudge, up to half a step | Slides early or late |
| Hold A, **L2 / R2** | Nudge, a tenth of a step a press: L2 early, R2 late | The same |
| Hold Y, stick up / down | Chance | Fades toward a ghost |
| Hold Y, stick left / right | Start: where in the drum's sound the hit begins | A mark slides under it, and it is heard as it moves |

The stick does one of those per push. A places a hit. Y places a "maybe"
(50%), or steps a hit's chance 100, 75, 50, 25. B deletes. X hears the lane.

Hold **B** and drag to delete every hit the cursor crosses.

Hold **R1** and drag to paint hits at one level (the level of the hit you
started on, or 80%). **Add R2 while R1 is held** and the brush paints
random levels for as long as R2 is down. R2 by itself is "next screen", at
once.

The cursor can also sit on a lane's name, left of step 1: A plays the drum,
hold A + stick is its volume, and Y is the **lane dice** (hold Y + stick up
and hits arrive in a shuffled order that leans toward the beat; down and
they leave; tap Y to reshuffle). Up from the top lane is the tempo.

On a lane's name, hold Y and push **right** to roll a new start for every
hit on the lane, on the sixteenths of the sound. **Left** puts them all back
at the top. On a loop fitted to one bar, that rearranges the beat and keeps
it in time.

### The same on every screen

A few things mean one thing everywhere:

| | Means |
| --- | --- |
| A | Place, open, pick. Held: a direction shapes or turns what is under the cursor |
| B | Delete, empty, go back |
| **Hold B** | **Delete for good**: a track (2 s), a song (1 s), a recording in the list (1 s). The row fills as you hold; let go early and nothing happens |
| X | Hear it. Held on a drum hit: its step effects |
| Hold R1 | On an empty step: paint as you drag. On a note: carry it. On SONG: copy a bar as you drag |
| L2 / R2 | The tabs |
| SELECT + something | Things about the whole song: undo, tempo, volume, WRITE, the scene's length, the next scene |

A held D-pad runs at one of two speeds on every screen: fast for a cursor
(33 moves a second), slow for a list or a choice (9 a second), so each one
can be seen or heard as it goes by.

On PRESET with A held, **left / right go eight presets at a time**, as in the
list of recordings.

### Step effects

What one hit does besides play. **Hold X on a hit** (a tap of X still just
hears it) and the stick or D-pad sets them, a click at a time, each heard as
it changes:

| Hold X and | Sets | On the block |
| --- | --- | --- |
| Up / down | **PITCH**: a semitone a click, an octave either way | A dash beside it, higher or lower |
| Left / right | **RATCHET**: the hit played 2, 3 or 4 times inside its step | Split into that many slivers |
| L1 | **REVERSE**: backwards, on and off | An arrow on its left |
| R1 | **CUT**: stopped after a step, half a step, a quarter; off | A red edge |
| A | **THROW**: this hit alone goes to ECHO, to SPACE, to both; off (see below) | A short bar above it: over its left half for ECHO, its right for SPACE |
| Y | **Dice**: rolls the first four for this hit. Mostly one effect, sometimes two. Always something, and never what it was. The dice leave THROW as it is. | |

- They combine, and every hit has its own. The strip under the grid spells
  them out; with X held it shows all five.
- A ratchet is rolled for once: with a chance below 100% it plays all its
  hits or none.
- Backwards plays what the hit would have played (its slice, or the sound
  from its start to the end) from the end to the start.
- A cut hit lasts its time whatever its pitch. In a ratchet each hit also
  stops when the next one comes.
- Kept in the song, undone by SELECT + B, and gone with the hit when it is
  deleted.
- A synth's notes have their own: see "Note effects" under PATTERN, a
  synth.

### THROW

**One hit, or one note, sent in full to ECHO or SPACE.** The snare on beat
four gets the long echo; the rest of the bar stays dry. Hold **X** on the hit
or the note and press **A**: OFF, ECHO, SPACE, BOTH, and round again.

| | A thrown drum hit | A thrown note or chord |
| --- | --- | --- |
| What goes to the bus | That hit alone. The other drums of the track do not | The whole track, for as long as the note lasts. Other notes sounding then go with it |
| How | The drum is played a second time, straight into the bus | The track's send is opened for the note, and shut after it |
| The track's own effects | **Not on the thrown copy**: it is the plain drum | On it, as on any send |
| A ratchet | Every hit of it is thrown | The send stays open through all of them |
| A DUCK on the track | **Does not dip the thrown copy**: the bus gets the whole hit while the track dips | Dips what the bus gets, as it dips the track |
| Starts and stops | With the hit | Over 5 ms each way, so it does not click |

- **How loud is the bus's LEVEL** (FX, the bus's line). THROW has no amount
  of its own: it is a send at 100% for that hit or note.
- If the track's send is already up, THROW tops it up to 100%. It never adds
  on top.
- A muted track throws nothing. Stop and panic shut every send.
- A note's echoes ring on after the send has shut; what is cut is only the
  feed. The note's own fade (its release) after it is let go is not sent.
- In the slice view a thrown slice slides into the bus over a millisecond
  when the break is running on through it, so it does not tick.
- Kept in the song, undone by SELECT + B, copied with its bar, and in a
  bounce. Gone with the hit or the note.
- **A throw on the song's last step is whole in a bounce**: the bounce runs
  on until the echoes have died away (see BOUNCE). A thrown note there is
  stopped by the song's end like any note, so the bus gets what there was of
  it, and its echoes are kept.
- A DUCK on the master dips what comes back from ECHO and SPACE with the
  rest of the song.
- The dice do not throw.

### Slices: chopping a break

**L1 on a lane that plays a recording** zooms the grid into that one lane.
L1 again goes back. (L1 on the kit screen goes straight there too.)

The recording is drawn large and cut into **slices**: sixteen of them, or
one to a step if it is fitted to more than a bar (32 for two bars, 64 for
four). Under it are the bar's sixteen steps. A line runs from every hit up
to the slice it plays: straight down when the slice is the step's own,
crossing and purple when it has been moved.

Everything the grid does works the same here. What is added:

| Do this | To |
| --- | --- |
| Up / down on a hit | The next / previous slice, heard as it changes. It goes round the ends. |
| A on an empty step | Place a hit on the step's **own** slice (step 5 plays slice 5) |
| Hold R1 and drag | Lay the break out **in order**: a hit on every step crossed, each on its own slice |
| ...and R2 as well | A **random** slice on every step crossed: the break, shuffled |
| X | Hear one slice: the hit's, or on an empty step the one a hit would get |
| Up / down on the lane's name | Each hit plays **ONE SLICE** and stops, or **RUNS ON** through the recording until the next hit |
| Hold Y + right on the name | Roll every hit's slice. Left: back in order. |

So, to chop a break: put it on a lane, FIT it to 1 bar, L1, hold R1 and
sweep the stick across. That is the break as it was, in sixteen hits. Now
move to a step and press up or down until it plays the slice you want.

- **Slices in order play as one unbroken sound.** A hit that starts where
  the recording has just got to carries it on, with no gap or click. Only a
  slice out of order is heard as a cut.
- **ONE SLICE** is for beats with gaps: take a hit out and its step is
  silent. With **RUNS ON** the recording plays on through the gap.
- A hit's start can still be slid off the cuts (hold Y, stick left / right).
  The strip then reads `9 + 40%`: slice 9, and 40% of a slice further in.
- A slice change is undone by SELECT + B like any other edit. ONE SLICE /
  RUNS ON is kept in the song.

### DEVICE, drums: the kit

A row for each lane. Each lane plays its built-in drum or a **recording**: a
WAV file from the `samples` folder beside the program. A kit can mix the two.

| On | Hold A + stick up / down | B |
| --- | --- | --- |
| SOUND | The built-in drum, then each recording by name. Heard as it changes. | The drum back |
| FIT TO | Off, 1, 2, 4, 8 steps, 1 bar, 2 bars, 4 bars | Off |

**L1 is Slices**: the PATTERN tab, zoomed into this lane's recording cut
into slices (see "Slices: chopping a break").

**Y is Catch.** The engine remembers the last ten seconds it played. Y on a
lane takes the last whole bar the song played, everything as it was heard,
saves it as `catch 1.wav`, `catch 2.wav`... in `samples/catches`, and puts
it on that lane fitted to one bar. It works just after stopping too. With no
whole bar to take (the first bar, or after playing by hand), it takes the
last bar's worth of sound.

**R1 is Record.** With a USB microphone or audio interface plugged into the
USB-C port: R1 on a lane starts it waiting for sound, so there is no silence
on the front. R1 again stops it; it stops by itself at ten seconds. Silence
on the end is trimmed, a quiet take is brought up, and it is saved as
`rec 1.wav`, `rec 2.wav`... in `samples/recorded` and put on the lane. The sampler has the same
button. Not tried on the handheld yet: it depends on the firmware seeing the
USB device, which `apotheca.log` reports at startup ("audio inputs").

**FIT** stretches the recording, at the same pitch, to last that many steps
at the song's tempo. Change the tempo and it is fitted again, a quarter of a
second after the tempo stops moving. The work is done ahead of time on
another thread and swapped in when ready, so it never costs the sound
anything while playing. Until it is ready the lane plays what it had.

- WAV files of 8, 16, 24 or 32 bits or 32-bit float, any sample rate, any
  number of channels. They are played as one channel at 48 kHz.
- **Ten seconds** of each is used.
- The song file keeps each lane's file by name and each hit's start. If the
  file is not on the card, the lane plays its drum and says so.
- The stretching is done by **Signalsmith Stretch** (MIT licence, in
  `third_party/signalsmith`). A recording too short for it, under about a
  tenth of a second, is done by a simpler method of our own
  (`app/stretch.hpp` says how both are used).

### PATTERN, a synth

A piano roll on its side. The keys run across the top, laid out as a
keyboard is, two octaves at a time (L1 zooms: one, two or three). The bar runs down the screen, step 1
at the top. A note is as tall as it is long, and filled across as far as it
is loud; holding A shapes it the way it is drawn.

| Gesture | Does |
| --- | --- |
| Stick or D-pad left / right | Along the keys (they scroll) |
| Stick or D-pad up / down | Through the bar |
| A | Place a note, one step long |
| Hold A, left / right | Level |
| Hold A, **stick** up / down | Length, freely: the note's end follows the stick to any point in a step |
| Hold A, **D-pad** up / down | Length by whole steps: the end goes to the next step line, or the one before |
| Hold A, **L2 / R2** | Nudge: the note a tenth of a step early (L2) or late (R2), up to half a step. It sits a little above or below its line. |
| B | Delete the note under the cursor |
| X | Hear the key |
| Hold R1 and drag | Paint a note on every step crossed |
| Hold B and drag | Delete every note (or chord) the cursor crosses |
| **Hold X on a note**, left / right | **RATCHET**: the note played 2, 3 or 4 times inside its first step |
| **Hold X on a note**, up / down | **GLIDE**: up is one length longer, down one shorter, down to OFF. The note slides in from the note before it |
| **Hold X on a note**, A | **THROW**: the note goes to ECHO, to SPACE, to both; off |
| **Hold X on a note**, L1 / R1 | **GLIDE FROM**, for the whole track: which key of a chord its glides come from. R1 is the next of TOP, PAIRED, NEAREST, RANDOM; L1 the one before. It stops at each end |
| **Hold R1 on a note** and move | Carry the note (or the whole chord) under the cursor. **D-pad:** to another key, or a whole step. **Stick up / down:** it slides freely in time, off the grid. It stops at the ends of the bar and will not land on another note. |
| L1 | Zoom. A tap goes closer: 2 octaves, 1, then round to 3. Held, up is closer and down is further. |

A note runs at most to the end of the bar, or to the next note on that key.
It can be as short as a tenth of a step. The engine lets go of each note on
its own sample, not on a step line.

**A note under a held chord still shows.** A note written where a chord (or a
longer note) is already sounding on the same key is drawn on top of it,
narrower, with a dark edge: a note inside a note.

### Note effects

Hold **X** on a note. The D-pad or the stick sets what the note does besides
play, one click per push. The strip at the bottom shows the values.

| Effect | Does | On a chord |
| --- | --- | --- |
| **RATCHET** (left / right) | The note 2, 3 or 4 times, evenly, inside its first step. Each is let go just before the next. The last is held for the rest of the note. | Yes: the whole chord repeats. It is one of a chord's six effects (see "Chord effects") |
| **GLIDE** (up = longer, down = shorter) | The note starts at the pitch of the note before it and slides to its own. How long the slide takes is the setting: OFF, 1/4 STEP, 1/2 STEP, 1 STEP, 2 STEPS or FULL. | No: a chord has no one note to slide from |
| **THROW** (A) | The track is sent in full to ECHO, SPACE or both for as long as the note lasts (see "THROW" under PATTERN, drums). A foot at the note's end: on the left for ECHO, the right for SPACE. | Yes: the whole chord |
| **Dice** (Y) | Rolls a ratchet, a glide of some length, or both. Always something, and never what it was. THROW is left as it is. | Rolls among the chord's six |

- A note with an effect carries a mark, so it can be told from a plain one.
- **On a chord the directions are shared out differently**: up / down pick
  one of six effects, left / right set it. See "Chord effects".
- With both set, only the first of the ratchet glides.
- A note too short for its ratchet plays as many as fit.
- Kept in the song, undone by SELECT + B, gone with the note.

**GLIDE, in full.** You set it on the note you slide *to*.

| Thing | How it works |
| --- | --- |
| Which note it slides from | The last note the track began before this one, on any key. After a chord (or any notes that began together), **GLIDE FROM** says which of its keys: see below. |
| The lengths | In steps, so they follow the tempo. **FULL** takes as long as it has been since the note it slides from began: a slow slide between two notes far apart, a quick one between two close together. |
| The line on the roll | A slanted purple line. It starts on the key the note slides from and ends at the note's start. Its height is the slide's length. The slide itself is heard from the note's start on. |
| A slide over more than one bar | FULL from a note bars back: the line is in every bar it crosses, from the bar its source note is in to the bar of its own note. |
| Into step 1 | The note slides from the last note of the loop. Its line is at the foot of the bar before (the same bar, in a one-bar cell), running off the bottom. |
| A key off the side of the roll | The line runs off that side. |
| Nothing to slide from | The first note after pressing play, and a note on the same key as the one before: no slide, no line. The corner mark stays. |
| Notes that begin together | None slides from another of them. Each slides from the note before them all. |
| With a ratchet | The slide is over before the second piece: at most 85% of the first piece, however long it is set. |
| The strip | With X held: GLIDE and its length, and the track's GLIDE FROM where NUDGE stands otherwise. Without: the length under GLIDE, or "x3 GLIDE" beside a ratchet. |
| Old songs | A glide that was ON is 1/2 STEP, which is what it was. On Phazed it was 92 ms at any tempo; it is now half a step there too. |

**GLIDE FROM: which key of a chord a glide comes from.** A setting of the
**track**, not of a note. Hold **X** on any note; **R1** is the next, **L1**
the one before. Every line on the roll moves at once.

"Moment" below: notes that begin together (a chord, or loose notes on one
step). A single note after a single note is the same under every type.

| Type | Each gliding note comes from | 4 keys, then 2 notes | 2 keys, then 4 notes |
| --- | --- | --- | --- |
| **TOP** | The highest key of the moment before. What it always did. | Both from the top key | All four from the upper key |
| **PAIRED** | Its opposite number, by pitch order: lowest from lowest, highest from highest, those between spread evenly. One note alone: from the top. | Bottom from bottom, top from top | Lower two from the lower key, upper two from the upper |
| **NEAREST** | The key nearest in pitch. A tie: the higher. Its own key: no slide. | Each from whichever key is closest | The same |
| **RANDOM** | A key of the moment before, rolled each time it plays. Play from the top gives the same rolls again. | Each from any of the four | Each from either |

| Thing | How it works |
| --- | --- |
| PAIRED counts every note of the moment | A chord's keys too, gliding or not. So a bass note under a chord comes from the bottom of the chord before, a melody note over it from the top. |
| FULL | As long as it has been since that moment began, whichever of its keys is picked. |
| RANDOM on the roll | The lines are as they are the first time the scene plays after Play. Later times round roll anew. |
| The strip, X held | GLIDE FROM and its value: dim while this note has no glide, brass when it is not TOP. |
| The dice | Leave it alone. It is the track's. |
| Undo | Does not take it back (like a send or a sound). One click puts it back. |
| In the song file | `glidefrom 1` (PAIRED), `2` (NEAREST), `3` (RANDOM), on a line of the track's own. Nothing at TOP. |
| Not on a chord | X held on a chord shows the chord's six effects; L1 / R1 there are as they were. |
| A known oddity | PAIRED, two notes under half a millisecond apart with a step line between them: the first is taken as a note alone (from the top). |

### Chords

Y is the chord button. Chords come from the song's chord pack.

| Gesture | Does |
| --- | --- |
| Hold Y | The pack's chords appear along the bottom; the one in hand sounds |
| ...left / right | The next chord (stick or D-pad, either) |
| ...up / down | Inversion: the bottom note goes up an octave, or the top one comes down |
| ...L1 / R1 | Another pack |
| ...X | Hear it again |
| Let go of Y | The chord lands at the cursor, and the cursor moves on past it |
| ...B first | Nothing lands |

So a tap of Y places the chord in hand again; hold, flick, let go places a
different one.

**A chord stays a chord**: one block, with one level, one length and its
name. Chords never overlap in time.

- Cursor on one of its notes: hold A to set the level and length of the
  whole chord, B to remove it.
- Cursor anywhere in its steps: hold Y to swap it for another chord in
  place. B while Y is held breaks it into loose notes instead.
- A new chord is as long as the last one you set.

**Chord packs** are JSON files in the form Shapetaker's Transmutation reads,
in a `chords` folder beside the program (`assets/chords` in this repository).
Drop a file in and it shows up. What is read from a pack is listed at the
top of `app/chords.hpp`. A pack called "Plain C" is built in. A song file
stores each chord whole, so a song still plays on a device without its pack.
A bar holds 64 notes, chords' notes included, and 16 chords.

### Chord effects

**A chord that sounds played by a hand, or an arpeggio.** Hold **X** on any
note of a chord. **Up / down picks the effect, left / right sets it**, one
click per push, stick or D-pad. The strip lists all six and marks the one
picked. X + A is still THROW, X + Y still the dice.

| Effect | Values | Does |
| --- | --- | --- |
| **RATCHET** | OFF, x2, x3, x4 | The whole chord that many times inside its first step, as before |
| **STRUM** | OFF, STRUM, WIDE STRUM | The notes start one after another: 9 ms apart, or 26 ms. The first is on the chord's own moment, the rest land late. Each is held to the chord's end |
| | 1/4 STEP, 1/2 STEP, 1 STEP | **An arpeggio**: one note at a time, that far apart, round and round the chord's notes until the chord ends |
| **ORDER** | UP, DOWN, UP-DOWN, RANDOM | Which way STRUM runs. Does nothing with STRUM off (the strip shows it dim) |
| **HUMAN TIME** | OFF, 10, 25, 50, 75, 100% | Each note starts a little early or late. At 100%: up to 12 ms either way |
| **HUMAN LEVEL** | the same | Each note a little louder or quieter. At 100%: up to 15% of its level either way |
| **LENGTHS** | the same | Each note is let go a little sooner. At 100%: down to half. Never longer, so nothing runs into the next chord |

**Random means fresh every time the chord plays.** No two times round the
loop are alike. But pressing play from the top gives the same performance
again, so a bounce is what you heard from the top.

How the effects meet each other, and the edges:

| Thing | What happens |
| --- | --- |
| A strum on a short chord | The strum never takes more than half the chord's length. A shorter chord is strummed faster |
| STRUM with RATCHET | Every piece of the ratchet is strummed (within half of the piece) |
| RATCHET with any of the others | Every key plays as many hits as the plain chord does, every time. LENGTHS lets the **last** hit go sooner; the hits before it are as they always are |
| An arpeggio with RATCHET | The ratchet is not played. It is kept, and the strip shows it dim |
| An arpeggio past the chord's end | It never goes past. It goes round again through the notes until the chord ends; the last note is cut there |
| UP-DOWN on a strum | Up on one chord, down on the next, as a hand goes. Play starts with up |
| UP-DOWN on an arpeggio | Up, then back down, without playing the top or bottom twice |
| RANDOM on an arpeggio | A fresh order each time round the notes. With three notes or more, never the same note twice running |
| HUMAN TIME, early | The engine looks one step ahead, so a note really can come before the beat. **The song's very first chord cannot**: its early notes land on the first sample |
| HUMAN TIME in an arpeggio | A note never moves more than a quarter of the way to the next, so the order holds |
| HUMAN TIME and the next chord | A note that comes early can start up to 12 ms before the chord before it has ended |
| THROW | The send is open from the chord's first note to the chord's end, strummed or not, however short LENGTHS leaves the notes |
| GLIDE after the chord | A gliding note after it still slides from the chord's keys as written (which one: the track's GLIDE FROM; TOP is its top key), counted from the chord's own moment, as the roll draws it. A strum does not change that |
| X (hear) | Plays the chord plain, all at once. The effects are heard when the song plays |
| Swap (hold Y) | The new chord keeps the effects |
| Break into notes (hold Y, B) | The loose notes keep the ratchet and the THROW. **They keep nothing of the other five**: a strum belongs to a chord |
| Move (hold R1), copy, NEXT SCENE, undo | The effects go with the chord |
| The mark | A light corner at the top of **one** key: the chord's lowest, the one under its name. A ratchet's lines are on that key too, once, not on every key. Where the name sits on that key (a chord on step 1, or nudged early) both go on the next key, so nothing is written over the name. THROW's foot is on every key. A ratchet that is not played (an arpeggio) is not drawn |
| Changed while the song plays | The change is heard the next time the chord plays, and that time has each note once. An arpeggio stops at once if its chord is deleted, moved or made a plain chord |
| A bounce | The song is played once, so nothing of its first chord sounds at its end (live, going round, HUMAN TIME's early notes do come just before the bar line) |
| The dice (X + Y) | Always something, never what it was. A strum about half the time, an arpeggio about one roll in seven; human amounts only 10 or 25%; an arpeggio is never given a ratchet |

In the song file a chord's effects are one line after it, left out when none
is set: `chordplay STEP STRUM ORDER TIME LEVEL LENGTHS`. A program from
before chord effects skips the line and plays the chord plain.

### DEVICE, a synth

MAIN: folder, preset, eight macros and ADVANCED. Phazed's macros are TONE,
MOTION, ATTACK, RELEASE, DETUNE, VIBRATO, CHORUS, OCTAVE. Twin Analog's are
SHAPE, BLEND, FORMANT, WIDTH, DETUNE, VINTAGE, ATTACK, RELEASE. Hold A + stick turns the one under
the cursor; B puts a macro back in the middle. Each macro has a small picture
of what it does and a colour for its kind (teal: the sound's colour; purple:
movement and width; cream: time), and stays dim until it is turned. Macros bend the sound and
leave the preset as it was made. A new preset is played at once when the
song is stopped.

Phazed's DETUNE tunes the sound's second line (oscillator) up to a semitone
sharp or flat. A sound with only one line is given a twin of that line as
soon as DETUNE leaves the middle, so the macro does something on every
preset. Two things follow from the twin: notes that are sounding at that
moment fade out once, and such a sound then plays four notes at a time
instead of eight.

ADVANCED: every setting. Phazed has nine pages (the sound, each line, and
each line's volume, wave and pitch envelopes drawn out). Twin Analog has
three (oscillators, formant and character, envelope). L1 and R1 turn the pages; B
goes back to MAIN.

How fast the drum grid moves is set in one place, `Feel` in
`app/pattern_screen.hpp`.

Keyboard stand-ins:

| Handheld | Key | | Handheld | Key |
| --- | --- | --- | --- | --- |
| D-pad | Arrow keys | | L1 / R1 | Q / W |
| A | X | | L2 / R2 | 1 / 2 |
| B | Z | | START | Return |
| X | S | | SELECT | Backspace |
| Y | A | | MENU | Tab |
| The stick | I J K L | | (quit, desk only) | Esc |

The button code lives in `app/input.hpp`. Nothing else in the app may read a
key or a gamepad button directly.

## Build for the handheld

```
scripts/build-handheld.sh
```

Runs on Linux (needs clang, lld, curl, dpkg-deb, zip). It writes
`build-handheld/Apotheca-port.zip`:

```
Apotheca.sh         the launcher the Ports menu shows
apotheca/apotheca   the program
apotheca/*.ttf      the lettering
apotheca/licences/  licences of the code and lettering built in
```

Unzip it into the firmware's ports folder on the SD card, beside the other
ports. If it fails to start, the reason is in `apotheca/apotheca.log`.

The program asks the device for only two things: the C library (version 2.29
or newer) and SDL2. `cmake/toolchain-aarch64.cmake` is a second route, for
building the engine and its tests alone.

## Layout

```
engine/   the audio engine. No SDL, no device code, so it tests on its own.
app/      window, audio device, buttons, and the screens. Talks to the
          engine through commands. app.hpp ties the screens together.
tests/    engine tests, and screen tests that press the buttons.
third_party/  stb_truetype, which reads the font (public domain);
          easycz, the engine behind Phazed, copied unchanged from
          joshpanzarella/schwung-easycz (MIT, with a BSD-3-Clause chip
          emulation from MAME); and signalsmith, which stretches
          recordings to fit the tempo (MIT).
tools/    render_demo, which renders a short piece to a WAV file.
cmake/    cross-compile settings.
scripts/  the handheld build and the port launcher.
```

## Rules

- **Nothing in the audio thread may wait.** No allocation, locks, files or
  printing inside `Engine::render()` or any `Effect::process()`.
- **The screen thread talks to the engine only through `Engine::send()`.**
- **The engine never includes SDL.**
- **One button and the stick, not a combination.** New controls follow the
  rule on the Pattern screen: the stick moves; hold a button and it shapes.
- **Fast.** Held input runs quickly. Nobody has asked for slower.
- **Twin Analog is a port of Clairaudient**, the oscillator in the Shapetaker
  Rack plugin (`src/clairaudient.cpp` there). `engine/src/twin.cpp` restates
  its sound-making code without Rack and lists what changed in the move.
  The four oscillator copies are computed in one branch-free loop of four:
  measured with clang, that halves the synth's cost. Keep it branch-free.
- **The first synth is called Phazed.** That is the only name a player sees. Its
  engine lives in `third_party/easycz` under its own names and licences;
  anything from it that reaches the screen goes through
  `engine/include/apotheca/phazed.hpp`.
- **Handheld first.** A feature is designed on the handheld's buttons. A
  keyboard key only ever stands in for one of them.
