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
- **FX**: two effects on each track and two on the master.

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

**Hold SELECT for a moment and a help card comes up**: what the buttons do
on every screen (X hears what is under the cursor, the stick and D-pad
move, A, B, START, L2 / R2, what SELECT does with other buttons, the
panic). Let go and it closes. The bar at the bottom of each screen lists
only what is particular to where the cursor is. A direction that either the
stick or the D-pad gives is written as the direction alone (UP / DOWN); the
two are named only where they do different things. The bar never runs off
the screen: a hint that does not fit is left out.

**L2 and R2** walk along the screens: SONG, PATTERN, DEVICE, FX, SET.
**SELECT**, tapped by itself, goes to SONG from anywhere (and on SONG opens
the list of songs). **START** plays: the song from
the SONG screen, the scene being worked on from the others. **SELECT + START**
quits.

### DEVICE, the sampler

A fourth instrument in the + TRACK list. **One recording, played from the
keys**: the piano roll and the chord button work on it as on a synth. At its
ROOT key the recording plays as it was made; an octave up, twice as fast.

| On | Hold A + up / down | B |
| --- | --- | --- |
| SOUND | **Tap A: the list of recordings.** Or hold A + up / down: one at a time | None |
| ROOT, TUNE, START, ATTACK, RELEASE, LOOP, LEVEL | The stick turns it; the D-pad moves it by one | Its usual value |

**Y is Catch** here too: the last bar the song played becomes this track's
recording. Eight notes at a time.

### The list of recordings

**Tap A on SOUND** (a kit lane, or the sampler) and the recordings come up
as a list, with the one showing drawn as a waveform and its length.

| Button | Does |
| --- | --- |
| Up / down | The next one. It goes on **at once**: with the song playing you hear it in the beat; stopped, it plays once. The stick runs through about eight a second. |
| Left / right | Eight at a time |
| A | Keep it |
| B | Put back what was there before the list opened |
| X | Hear it again |
| START | Play / stop, with the list still open |

### FX

A fourth screen, after DEVICE. Four rows: the **two effects on the track**
being worked on, then the **two on the master** (everything, after the tracks
are mixed).

| On | Hold A + up / down | B |
| --- | --- | --- |
| The effect | NONE, FILTER, DRIVE, DELAY, REVERB, CRUSH. It goes in at once, at its usual settings. | None |
| A setting | The stick turns it. The D-pad moves it a twentieth, or to the next choice. | Its usual value |

| Effect | Settings |
| --- | --- |
| FILTER | Cutoff, resonance, type, mix. Types: LOW PASS 12 (two poles, gentle), LOW PASS 24 (four poles, steep), BAND PASS, HIGH PASS, and DJ: one knob, nothing in the middle, turned down it closes from the top, turned up it clears out the bottom. |
| DRIVE | Drive, tone, mix, output |
| DELAY | Time (1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2: it follows the tempo), feedback, tone, mix |
| REVERB | Size, damping, mix |
| CRUSH | Bits, rate, mix |

Effects are kept in the song. They can be changed while it plays. A panic
empties their tails.

### Panic

**Hold all four shoulder buttons (L1, L2, R1, R2).** The sound is shut off
within 5 milliseconds and stays off for as long as they are held. The song
stops, every drum is cut and every note is let go. It works on every screen
and changes nothing else.

Let go, and the sound opens again once whatever was left has died away, or
at the next thing you play, or after three seconds at the latest. The top
bar says SILENCED until it has.

A notice comes up saying a panic happened, in case it was an accident. Once
the buttons are let go, any button puts it away (that press does nothing
else), or it goes by itself after four seconds.

### SETUP (the SET tab)

| Row | Left / right | A |
| --- | --- | --- |
| TEMPO | A beat a minute | |
| CHORD PACK | Another pack | |
| TRACK NAME | A name for the track being worked on, from a list | **Type one**: a grid of letters and numbers. A types, B deletes, Y keeps. Ten characters. |
| VOLUME | A twentieth | |
| STICK SPEED | 25% to 150%, kept for next time | |
| PIANO ROLL SHOWS | 1, 2 or 3 octaves | |
| USB KEYBOARD | PLAY ONLY, or PLAY + RECORD | |
| WIFI PAGE | | On / off |
| BUTTONS | | The button test |
| CONTROLS | | Hold a second: set the controls up again |

The first three belong to the song. The rest belong to the handheld and are
kept in `settings.txt` and `volume.txt` beside the program.

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
| FAMILY | BY ITS NUMBER, or one of the 32 by name |

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
| A track (any of its cells) | **Y** mutes it, **L1** solos it, **X** hears it. All at once, playing or not. A muted track's head shows M, a soloed one S. The song keeps mutes, not solos. | | | |
| A repeat count | Tap: 1, 2, 4, 8 | Times the scene plays, 1 to 16 | Back to 1 | |
| + TRACK | Pick DRUMS, PHAZED, TWIN ANALOG, SAMPLER or QMOD | | | |

The song runs to the last scene with anything in it, then comes round.

### Longer phrases

A **cell** (one track in one scene) can be **1, 2, 4 or 8 bars** long.

- **Hold A on a cell and push the stick left / right** to set its length. A
  tap of A still opens it.
- **The scene lasts as long as its longest cell.** A shorter cell goes round
  under it: a one-bar drum loop plays four times under a four-bar part.
- A shorter cell is drawn once, as wide as its share of the scene, with
  **LOOPS x4** in the room after it. A long cell shows its length in the corner.
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

- **Drop files on the page** to send them: recordings (`.wav`), chord packs
  (`.json`), songs (`.song`). A recording is on the kit screen at once; a
  chord pack is in the chord button's list on the next frame.
- **Click a file on the page** to fetch it back: a catch, a recording, a song.
- The four digits on the end are made up each time it is turned on. A
  request without them is refused, so someone on the network who has not
  seen the handheld's screen cannot get in. It is not encryption: use it on
  a network you trust.
- Every file is checked before it is kept: a plain name of the right kind
  (no folders, no `..`), 30 MB at most, and really a WAV, a chord pack or a
  song. Anything else is refused and nothing is written.
- It is off until turned on, and off again when Apotheca closes.

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

### DEVICE, drums: the kit

A row for each lane. Each lane plays its built-in drum or a **recording**: a
WAV file from the `samples` folder beside the program. A kit can mix the two.

| On | Hold A + stick up / down | B |
| --- | --- | --- |
| SOUND | The built-in drum, then each recording by name. Heard as it changes. | The drum back |
| FIT TO | Off, 1, 2, 4, 8 steps, 1 bar, 2 bars, 4 bars | Off |

**Y is Catch.** The engine remembers the last ten seconds it played. Y on a
lane takes the last whole bar the song played, everything as it was heard,
saves it as `catch 1.wav`, `catch 2.wav`... in the `samples` folder, and puts
it on that lane fitted to one bar. It works just after stopping too. With no
whole bar to take (the first bar, or after playing by hand), it takes the
last bar's worth of sound.

**R1 is Record.** With a USB microphone or audio interface plugged into the
USB-C port: R1 on a lane starts it waiting for sound, so there is no silence
on the front. R1 again stops it; it stops by itself at ten seconds. Silence
on the end is trimmed, a quiet take is brought up, and it is saved as
`rec 1.wav`, `rec 2.wav`... and put on the lane. The sampler has the same
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
| Hold X and move | Carry the note (or the whole chord) under the cursor to another key or step. It stops at the ends of the bar and will not land on another note. |
| L1 | Zoom. A tap goes closer: 2 octaves, 1, then round to 3. Held, up is closer and down is further. |

A note runs at most to the end of the bar, or to the next note on that key.
It can be as short as a tenth of a step. The engine lets go of each note on
its own sample, not on a step line.

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
