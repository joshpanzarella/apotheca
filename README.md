# Apotheca

A pocket music workstation for Linux handhelds (first target: Anbernic
RG40XXV, 640x480, buttons only). Part of the Shapetaker family.

## Status

A small song machine that runs, at 48 kHz. Up to twelve tracks, each a drum
kit or one of two synths: Phazed (phase distortion, 65 presets) or Twin
Analog (two morphing oscillators, 16 presets). Up to sixteen scenes,
each one bar, each with its own repeat count. Three screens:

- **SONG**: tracks across, scenes down. Add tracks, set how many times each
  scene plays, copy bars by dragging.
- **PATTERN**: a drum grid (levels, nudge, chance, paint, lane dice) or, for
  a synth, a piano roll on its side (keys across, time down) with chords
  from chord packs.
- **DEVICE**: for a synth, a main panel of folder, preset and eight macros,
  with advanced pages behind it (nine for Phazed, three for Twin Analog).
  For drums, the kit: each lane plays its built-in drum or a recording.

Songs are kept as you go: there is no save button. A **rev** keeps the song
as it is at one moment, to go back to.

While it plays, the top bar of every screen shows which scene is playing and
how far through it is: one block for each time the scene plays, filling as
the bar goes by.

Not built yet: effects on screen, recording into the device, a sampler
played from the keys, the sound designer web page.

## Build

Needs CMake, a C++17 compiler and SDL2 (`brew install cmake sdl2` on macOS,
`apt install cmake libsdl2-dev` on Debian/Ubuntu).

```
cmake -B build
cmake --build build
ctest --test-dir build      # engine, song, both synths, screens, app, saving, chord and sample tests
./build/apotheca            # the app
```

## Controls

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

**L2 and R2** walk along the three screens. **START** plays: the song from
the SONG screen, the scene being worked on from the others. **SELECT + START**
quits.

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

### SONG

| On | A | Hold A + stick | B | Hold R1 and drag |
| --- | --- | --- | --- | --- |
| A cell | Opens it on PATTERN | | Empties it | Copies it into every cell crossed |
| A repeat count | Tap: 1, 2, 4, 8 | Times the scene plays, 1 to 16 | Back to 1 | |
| + TRACK | Pick DRUMS, PHAZED or TWIN ANALOG | | | |

The song runs to the last scene with anything in it, then comes round.

**SELECT** lists the songs: start a new one, or open another.

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
- Deleting a song is done by deleting its file; there is no button for it yet.

### PATTERN, drums

| Gesture | Sets | The block |
| --- | --- | --- |
| Hold A, stick up / down | Level | Fills or drains |
| Hold A, stick left / right | Nudge, up to half a step | Slides early or late |
| Hold Y, stick up / down | Chance | Fades toward a ghost |
| Hold Y, stick left / right | Start: where in the drum's sound the hit begins | A mark slides under it, and it is heard as it moves |

The stick does one of those per push. A places a hit. Y places a "maybe"
(50%), or steps a hit's chance 100, 75, 50, 25. B deletes. X hears the lane.

Hold **R1** and drag to paint hits at one level (the level of the hit you
started on, or 80%). Hold **L1** and drag for random levels.

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
keyboard is, three octaves at a time. The bar runs down the screen, step 1
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
| B | Delete the note under the cursor |
| X | Hear the key |
| Hold R1 and drag | Paint a note on every step crossed |

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
