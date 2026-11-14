# Apotheca: notes for Claude

A pocket music workstation for the Anbernic RG40XXV (640x480, buttons and one
stick, Linux aarch64, stock firmware). Part of the Shapetaker family. The
manual is `README.md`; this file is the rules that bite when changing the code.

## How to write to me

Applies to every reply. Not optional.

- **Answer first.** First sentence = the answer. Context after, if needed.
- **Short.** Short sentences, short paragraphs, headers or bullets to skim.
- **Plain words.** "used" not "leveraged". No "delve", "robust", "elegant",
  "powerful", "crucial".
- **No fluff.** No preamble, no restating my question, no congratulations.
- **Explain like I'm new to it.** Define a term inline the first time. A small
  table or a before/after beats a paragraph.
- **Bold the part that matters.**
- **Bad news plainly**, in the first line. Say what was not done or not tested.
- **Don't pad the ending.**

Words on the device follow the same taste: plain, short, upper case. "delete",
never "rub out".

## What the owner wants from every change

- **Speed of the workflow comes first.** Every action is judged by how fast
  and easy it is on the handheld. If it is slow or awkward, it gets fixed.
- **Stick for big moves, D-pad for exact ones.** Choices (a filter type) click
  once per push; they are not sliders.
- **The look is settled.** Clean, the same layout. Do not restyle.
- **Scenes, not a timeline. Up to 12 tracks. Chance and dice are welcome.**
- **I cannot hear.** Nothing is "verified" on the handheld unless the owner
  says so. Say which things only the tests and stills have seen.

## The buttons' standards (from the UI audit)

New screens and gestures keep these; do not invent a variant.

- **A** places, opens, picks. **Hold A + a direction** shapes or turns what is under the cursor.
- **B** deletes, empties, goes back. **Hold B deletes for good** (with a bar that fills): never another button.
- **X** hears. **Hold X** is "what it does": step effects on a drum hit, ratchet and glide on a note.
- **Hold R1** drags: paint from an empty step, carry from a note, copy on SONG.
- **Y** is the screen's special (chance, chord, mute, catch, roll).
- **SELECT + something** is for whole-song things. **L2 / R2** are the tabs.
- **A held D-pad has two speeds**, in `ui.hpp`: `kCursor...` for cursors, `kList...` for lists and choices.
- **One word: "delete".** Not erase, remove or rub out. A cell is "emptied".
- **What cannot be undone says so** where it is confirmed.

## Build, test, ship

| Job | Command |
| --- | --- |
| Build | `cmake -S . -B build && cmake --build build -j8` |
| All tests (31) | `cd build && ctest -j8` (about 100 s; `nextscene` is the longest; `effects_test` runs as `effects1` and `effects2`; `textsize`, `textviews` and `themeviews` each run by themselves) |
| Timings | `./build/bench`, or `apotheca --bench`. On the handheld: SPEED TEST on the WiFi page (the timings are in the program, `app/bench_run.cpp`, so an update over WiFi brings them) |
| A still of a screen | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --shot out.bmp` (views are listed in `app/main.cpp`; the FX screen's are `fx`, `fx-type`, `fx-pick`, `fx-comp`, `fx-master`, `fx-duck`, `fx-choice`, `fx-auto`, `fx-sends`, `fx-bus`, `fx-level`, `fx-ping`, `fx-ping-bus`, `fx-spring` (REVERB SPRING on the track and on SPACE), `fx-plate`, `fx-family-1` to `-7` (the last type of each family, FILTER to LEVEL, in the track's second slot; with `--hold-a` the list of effects on its line, the type lit), `fx-deleting` (B held on an effect, its bar filling), each also with `--hold-a` (on a line of the list that opens the list of effects: `fx-pick --hold-a`, `fx-type --hold-a`, `fx-bus --hold-a`); the piano roll's strip at its fullest: `notefx-worst`, `notefx-space`; GLIDE FROM, one a type on the same notes: `notefx-from` (TOP), `notefx-from-paired`, `-nearest`, `-random`, each also with `-held` (X held: the strip says it); every length of glide and its line: `notefx-glides`, `-glides-alone`, `-glides-longest-held`, `-glides-far1` to `-far4` (a FULL glide from two bars back, each bar); the BOUNCE row at work: `bounce`, `bounce-tail`; NEXT SCENE just pressed: `next-scene`, `next-scene-notes`, `next-scene-full`; chord effects: `chordfx`, `chordfx-held`, `chordfx-ratchet` / `-strum` / `-order` / `-time` / `-level` / `-lengths` (X held, that one picked), `chordfx-off`, `chordfx-arp`, `chordfx-worst` (the strip at its fullest), `chordfx-line`; a track's other instrument offered: `change-instrument`; a song being deleted: `song-deleting`; any view with `--no-menu` is as on a handheld with no MENU button) |
| Handheld build | `scripts/build-handheld.sh` (clang cross-build against an Ubuntu 20.04 arm64 sysroot) |

- **Shipping a build**: run the handheld build, copy `Apotheca-port.zip`,
  `port/apotheca/apotheca` and `VERSION` from `build-handheld/` into `dist/`,
  commit, push to `main`. The owner updates over WiFi or from the SD card.
- **The version is `0.N`**, N = commits + 1 at build time. Only
  `app/version.cpp` is compiled with it, so a new number rebuilds one file.
- **Check a command's real exit code.** `ctest | tail` returns tail's. Use
  `set -o pipefail`. A commit went out past a failing test once this way.
- **Restoring a file with `mv` keeps its old date** and the build then skips
  it. `touch` it, or write the content back.

## Layout

```
engine/   the sound engine: a static library, no SDL. Instruments, effects, the step clock.
app/      the SDL2 app: screens, the Session, files, WiFi page, MIDI.
tests/    one program per area. CHECK macros, no framework.
tools/    bench.cpp (timings), render_demo.cpp
third_party/  easycz (Phazed's engine), shapetaker/entanglement (QMOD's generator), signalsmith (stretch). Copied in, and OPEN TO CHANGE (see the rule below).
dist/     the built handheld program, kept in git so the owner can fetch it
```

Where things are in the app:

| File | Holds |
| --- | --- |
| `instruments.hpp/.cpp` | **the one table of instruments**: names, song-file word, engine kind, preset ending, list order |
| `synth_spec.cpp` | per synth: presets, settings, macro names |
| `screen.hpp` | the `Screen` base every screen derives from |
| `app.cpp` | `App::screenNow()` picks the screen; SELECT/MENU/shoulder combos live here |
| `session.cpp` | playing, tracks, patterns, mutes, undo, automation, effects, sending the song to the engine |
| `session_songs.cpp` | songs on disk, revisions, settings files |
| `session_synth.cpp` | sounds, presets, the designer page, USB keyboard |
| `session_samples.cpp` | recordings, fitting to tempo, the WiFi page, updates |

**Adding an instrument**: a `TrackKind` (project.hpp), a row in
`instruments.cpp`, a `SynthKind` and class in the engine, a `SynthSpec` if it
has presets and macros, and its DEVICE screen in `App::screenNow()`.

Where effects are, in the engine:

| File | Holds |
| --- | --- |
| `engine/include/apotheca/effects.hpp` | `EffectKind` (the numbers), `EffectInfo` (a row), and everything that reads the table |
| `engine/src/effects.cpp` | **the one table of effects**: each row's number, the list a player picks from, families and types, how a setting is carried to another type |
| `engine/src/fx/NAME.cpp` | one effect: its class, how its settings read, its row |
| `engine/src/fx/unit.hpp` | what the effects share; the steps to add one are at its top |
| `engine/src/fx/glide.hpp` | what the newer effects share: a setting that slides (`Glide`, the one such helper), a delay read between samples, the going-to-sleep rule |
| `engine/src/fx/halfband.hpp` | twice the sample rate and back, for an effect that bends the wave (SATURATE) |
| `engine/include/apotheca/denormals.hpp` | flush-to-zero |
| `tests/effects_test.cpp` | the pinned sound of the old five, and the measuring tools |

**Adding an effect**:
1. an `EffectKind` at the **end** of the list in `effects.hpp`;
2. a file `engine/src/fx/NAME.cpp` with its class and its `const EffectInfo kNameInfo`
   (name, settings, defaults, song-file `word`, group, which settings are
   choices, how a setting reads, how to make one, and `says`: a few plain
   words for the list a player picks from);
3. its `extern` line at the bottom of `fx/unit.hpp`;
4. its row in `kTable` in `effects.cpp` and its place in `kList` there;
5. its checks in `tests/effects_test.cpp`, with the tools there.

The screen, the song file and `bench` pick it up from the table; the build
takes every `.cpp` in `engine/src/fx/`. Nothing else is edited.

**Every effect is a TYPE of one of seven families** (owner, 2026-11-13), so
adding a TYPE (COMB to FILTER, FUZZ to DRIVE, BLOOM to REVERB) is adding an
effect, nothing more. Its row names its `family` (`EffectFamily`: FILTER,
DRIVE, CRUSH, MOD, DELAY, REVERB, LEVEL) and, where its name is not what
the type is called, its `type` (DELAY's is "CLEAN"). Put it in `kList`
beside the family's other types: the families in `EffectFamily`'s order,
side by side (`effects_test` fails one out of place), and the first type is
what the family goes in as. The list of effects names it on its family's
line by itself. Name the settings it shares with the other
types as they name them (TIME, FEEDBACK, MIX): changing TYPE carries a
setting over **by its name**. If a shared setting is a choice whose choices
differ (TIME), give the row an `amount` function, so a choice the other does
not have goes to the nearest. `effects_test` checks every pair of every
family by itself; nothing is listed there. A type that shares no name with
another (COMP and DUCK) starts at its usual values.

**A delay or a reverb also goes on a send bus** (by its family: `effectBus`).
Its `process()` must look at `Unit::bus_` and, when set, put out only what it
adds (wet times MIX) and none of what came in. `effects_test` checks every
effect in those two groups and fails one that does not.

## Rules that bite

### Nothing is closed to change (owner, 2026-11-12)
- **Any instrument may be improved, ported ones too** (Phazed's engine in
  `third_party/easycz`, QMOD's generator). There was a "copied unchanged; do
  not edit" rule; the owner did not ask for it and it is gone. Never give
  "that code is not ours" as a reason a feature cannot be done.
- What still holds is the usual care: a change to a synth is a change to the
  sound (old songs and presets, the owner's ears), and a file changed under
  `third_party/` gets a line in `third_party/CHANGES.md` (what and why), so a
  fresh copy from where it came from does not lose it without anyone knowing.

### Files and compatibility
- **Song files must keep loading.** Words in a song file (`track phazed`,
  `auto`, `steps`, `name`) never change meaning. Unknown lines are skipped.
- **Ids are appended, never reordered or reused**: `Command::Type`, every
  synth's settings and macros, effect kinds, effect settings, automation lanes
  (0..7 macros, `8 + slot * 4 + setting` effects for slots 1 to 3, 20 and 21
  the ECHO and SPACE sends, 22 and 23 spare). Songs and presets store them by
  number.
- **A song file with no `busfx` line is an old song.** Its buses get what a
  new song's do (`kNewSongBus` in `app/project.hpp`: TAPE and HALL), its
  sends are 0, and it sounds as it did. Old songs get the same as new ones on
  purpose: a bus nobody has touched holds the same thing in every song. `songText` always writes both `busfx` lines, an empty bus
  as `busfx 2 none`, so "none" is never mistaken for "old". A track's send is
  a `send BUS AMOUNT` line, left out at 0; `fx 3` is its third slot.
- **A cell made shorter keeps its hidden bars, in the file too.** They are
  written after a `bar K` past the cell's end (`hidesBars` in song_file.cpp);
  a program from before reads no line for a bar the cell does not have, so it
  skips them. A note there may run to where the longest cell would end.
- **A song file is read with Windows line ends too**: `parseSong` takes a
  trailing `\r` off each line (a name ending in one was refused, and every
  recording dropped); `renameRecordings` reads lines the same way and writes
  the `\r` back.
- **A number from a file is held to its limits as a double, before it is
  made an int, a uint32 or a float** (`wholeIn`, `fractionIn`, `seedIn` in
  song_file.cpp). "repeats 1e300" cast first is undefined behaviour, and the
  desk and the handheld read it differently. Anything new read from a file
  goes through them.
- **A song, a rev, a settings file or a preset is written with `writeTextFile`**: `NAME.part`, flushed and
  fsynced, renamed over `NAME`, and the folder fsynced (a flat battery left
  an empty song). `save_test` checks the order on Linux. Its cost on the
  handheld's card is not measured.
- **A deleted song goes to `deleted/` under a name nothing there has** (song
  or rev: "ash well", else "ash well 2"), through `binSong`, which the
  Session and the WiFi page share. Renamed over, the first one's revs paired
  with the second song.
- **Every synth has `kSynthMacros` (8) macros.** Each synth header checks it.
- **The FILTER type order is fixed** (`int(v * 5)`: low 12, low 24, band, DJ,
  high). It was chosen so old songs' three types still land right.
- **The five old effects are pinned to the bit** (FILTER, DRIVE, DELAY,
  REVERB, CRUSH): `effects_test` holds 40 renders and one whole song to
  checksums taken before they were moved. Old songs depend on them. Do not
  touch their arithmetic, not even to fix it; make a new effect instead. The
  checksums are remade (`effects_test --print`) only from code known to be right.
- **An effect has a number, a word and a place, and they are three things.**
  The number (`EffectKind`) is in memory and commands: appended, never
  reordered. The `word` is in song files: never changed. The place in the
  list (`kList`) is only what the player sees: change it freely. Never use
  one for another (`effectAt` / `effectPlace` / `effectFromWord`).
- **A family is only how the list is shown** (owner, 2026-11-12: "TAPE should
  just be one of the delays"; 2026-11-13: seven families). The player picks
  from FILTER (FILTER), DRIVE (DRIVE, SATURATE), CRUSH (CRUSH), MOD (CHORUS),
  DELAY (CLEAN, TAPE, PING), REVERB (ROOM, HALL, SPRING, PLATE), LEVEL (COMP,
  DUCK), in that order (`EffectFamily`; the old groups, DIRT and MOVING, are
  gone). Underneath each type is the `EffectKind` it always was, with its
  number and its word, and `info.name` is still its own ("TAPE": bench and
  the tests use it). Fifteen more types are planned (effects-plan.md, "Seven
  families"); each is a row. The screen asks `effectFamily`,
  `effectTypeName` (what the type is called: CLEAN, FILTER), `effectTypeShown`
  (the same, but "" where it is the family's own word: a line says FILTER,
  not FILTER FILTER, and DRIVE, not DRIVE DRIVE), `effectTypes` /
  `effectTypeAt` / `effectTypeIndex`, and picks from `familyAt` /
  `familyPlace` (a line a family; `bus` gives a bus its own short list). A
  song file is written as before: `app_test` holds a song with every
  kind up to DUCK to what `main` wrote (`tests/everykind_main.inc`; do not
  remake it). An effect newer than that is not in it (`kKindsMainWrote`):
  `testSongFilesAreAsTheyWere` writes and reads each newer kind by its own word.
  In the list of effects a family's line names its types, the slot's lit
  (`effectFamilyNamesTypes`: more than one, or one that is not the family's
  word, MOD's CHORUS); FILTER and CRUSH say their words instead
  (`effectFamilySays`, the first type's `says`).
- **Changing TYPE carries settings by NAME, as they READ** (`effectCarry`,
  `effectCarryValue`): the value whose reading is the same text; if the new
  type cannot read it, the nearest (an amount by the number in its reading, a
  choice by its row's `amount`). A scale that is the same on both sides is
  not touched, to the bit. Where the scales differ an amount lands on **the
  roundest value that reads so** (a hundredth, else a thousandth, nearest the
  middle of the stretch), never on the stretch's edge: from an edge the first
  push of the D-pad moved nothing that could be seen, and 45% came back as
  0.4452 in the song file. A twentieth goes there and back to the bit.
  `changeEffectType` (app/fx_screen.cpp) does it to
  a slot and **moves the automation of each kept setting to its new lane**
  (REVERB's MIX is its third setting, HALL's its fourth), read the same way,
  in every scene (a step written as 0 is a step); the rest of the slot's
  lanes are cleared. **Undo after a TYPE change is wrong and not fixed**
  (found in review, 2026-11-13): undo puts the patterns' automation back in
  the old type's lanes and the effect stays the new type (effects are not in
  undo; `main` has the same fault when an effect is changed or emptied). The
  mend is in `session.*` and is the owner's call. Another FAMILY
  put in a slot (`Session::setEffect`) still clears all four.

### The engine and its threads
- **The audio thread never allocates, locks for long, or frees.** Things it
  is done with go to the trash queue (`bin()`); `collectGarbage()` frees them,
  once a frame. What finds it full waits with the audio thread and is handed
  on at the end of a chunk (`binHeld`). **Nothing is kept in the engine for
  want of room**: a synth kept so played the next song's notes.
- **Commands go through a queue that can fill.** `Engine::send` returns false
  and counts the loss (`dropped()`). `Session::update` notices and sends the
  whole song again (`applyProject`). Callers that hand over a pointer must
  check the return themselves. **Every push goes through `send()`**, the
  ones that carry a recording or an effect too: a loss it does not count is
  never mended.
- **A test that never renders never empties the queue.** Render alongside
  frames (`rig.frames(n); rig.render(...)`), or commands pile up and drop.
- **Helper threads** (`useWorkers`): tracks are rendered side by side and
  mixed in track order, so the result is the same to the last bit with any
  number of helpers. **Anything a helper waits on is set with `wakeMutex_`
  held.** Setting the stop flag without it hung the quit.
- **The built-in drum kit is built once per sample rate** and shared
  (`DrumKit::builtIn`). It never changes after that.
- **An effect going into or out of a slot fades for 10 ms**
  (`Engine::swapEffect`, `runSlot`): the old one and the new both run, and
  the sound slides between them. Outside a fade a slot runs exactly as it did.
  The old effect goes to the trash from `renderChunk` after the helpers are
  done, never from `renderTrack`: **the trash queue takes from one thread.**
  A test that wants an effect in place at once uses the setup call
  `setTrackEffect`, or renders 10 ms before it listens.
- **Two send buses, ECHO and SPACE** (`kEchoBus` -2, `kSpaceBus` -3: "track"
  numbers beside `kMasterTrack`, one slot each; `slotsOf(track)` says how many
  slots anything has: tracks 3, master 2, a bus 1). A track's send is taken
  after its level and mute and glides for 5 ms. The buses are fed and run on
  the audio thread **after the helpers are done, in track order**
  (`feedBuses`, `runBuses`), and join the mix before the master's slots. Keep
  it there: that is what holds the output to the bit on any helper count.
- **THROW: a thrown drum hit is written twice, a thrown note opens the
  track's send.** The drum machine writes a thrown voice a second time into
  `thrown_[track][bus]` (`DrumMachine::render`'s two extra ways out), so the
  bus gets that hit alone, before the track's slots, at the track's level and
  mute. A thrown note holds `Track::throwLeft` for its length and
  `feedBuses` slides `throwNow` open and shut over 5 ms; a ratchet's first
  piece holds it for the whole note (`Note::hold`). "In full" is a send of 1:
  the knob's share is topped up, never added to (`now + open * (1 - now)`;
  a thrown hit gets the `1 - now` the knob leaves).
  - **The voice loop is built twice** (`renderVoice<true / false>`): with
    THROW's check inside the one loop, twelve unthrown drum tracks cost half
    as much again (1.46% to 2.18%). The line that adds to `left` is as it
    was; the thrown copy is worked out again beside it, not taken from it.
  - **A track that throws nothing takes the old loop in `feedBuses`**, which
    is why a song without throws is the same to the bit (the pinned song).
  - `letGoOfNotes` shuts every send a THROW held (Play, Stop, panic), as do
    `prepare` and a track taken away. Anything new that lets notes go in
    another way calls `closeThrows`.
  - In a command THROW rides with the cut (`StepFx::cutAndThrow`: cut plus 4
    times THROW, in `extra` of `SetStepFx` and `Trigger`); a note's is 512
    (ECHO) and 1024 (SPACE) added to `SetNote`'s `e`.
  - A hit that carries a lane's sound on hands on its own THROW, and the
    sound slides into the bus or out of it over a millisecond.
  - Its tests are their own two programs, each a few seconds:
    `tests/throw_test.cpp` (the engine) and `tests/throwapp_test.cpp` (the
    gesture, the song file, undo, a bounce).
  - **A thrown drum hit is not ducked, a thrown note is.** The hit's copy
    leaves before the slots; a note's THROW is a send, taken after them. A
    DUCK on the master dips what the buses give back. `throw_test` holds all
    three; do not move the thrown copy after the slots.
  - **A bounce ends the song with Stop, and Stop shuts a thrown note's send**
    but not a thrown drum hit, which rings out into its bus. The tail after
    a last-step throw is kept by the bounce's own rule (four seconds of
    quiet); `throwapp_test` bounces a hit, a note and a howling TAPE there.
- **A bus at rest is skipped whole and adds nothing, not even zeros.** That is
  why a song with no sends is the same to the last bit as before there were
  buses (`effects_test` holds the old song's checksum with an effect waiting
  on each bus). A bus rests when it has been sent nothing and has put out
  nothing above `kBusQuiet` (1e-6) for `kBusRestSeconds` (10 s); its effect is
  then `reset()`. Ten seconds must stay longer than the longest silence
  between two echoes of anything a bus can hold (`effects_test` measures each
  delay and reverb at 40 BPM: DELAY 3.0 s). A bus with no effect is not run.
- **A bus's LEVEL waits for a swap's fade** (`held` in `runBuses`): another
  effect put on a ringing bus sets where LEVEL is going (full), but it stays
  where it was for the 10 ms the old one fades out. Set at once, a bus turned
  down had its old tail turned UP as it left.
- **A bus's LEVEL is the engine's, not the effect's** (`busLevel_`, applied in
  `runBuses` after the rest rule has listened, gliding like a send). The
  effect on a bus keeps its MIX at full. Listened to after LEVEL, a bus
  parked at LEVEL 0 with echoes in it was cleared after ten seconds.
  `setEffect` says which setting is the LEVEL (`effectMixParam`); an effect
  handed over whole (`setBusEffect`, a setup call) keeps its own MIX.
- **`Engine::setEffect` refuses an effect that is not the bus's kind**
  (returns false), as the Session does. A FILTER on ECHO would give the
  track's sound back a second time.
- **What is sent to a bus is sound or it is silence**: not-a-number, infinity
  and anything beyond `kBusLoudest` are zeroed in `feedBuses`, since the old
  DELAY and REVERB cannot look after themselves and would hold it for good.
- **A tail always ends** (owner, 2026-11-12). `kLongestTailSeconds` (450) is
  the promise: from the last sound a bus is sent to the bus at rest, at every
  setting of every effect a bus can hold, at 40 BPM. `effects_test`
  (`testATailAlwaysEnds`) measures every delay and reverb in the table at 0,
  50% and 100% of each setting and fails one that takes longer, so **a new
  delay or reverb whose loop can hold at 1 or over needs its own way down**.
  TAPE's: left alone 30 s (nothing over `kFedLevel` coming in), a FEEDBACK
  over 90% eases to 90% (`tire_`); at 90% or under it is multiplied by exactly
  1 (`tapeTire` compares the SETTING, 0.75 and a hair, not what it reads:
  `1.2f * 0.75f` is a float over `0.9f`). The count, the ease down
  (`tapeTireStep`), falling asleep and waking are each on their own SAMPLE,
  never a block's edge. **The promise is for one effect.**
  Effects in a row (two TAPEs: a slot into a bus, or two slots) each count
  from the last sound coming IN, so one keeps the next fed and the tail can
  pass 450 s; the bounce's cap is what ends that one. DELAY, PING and REVERB cannot ring for good (0.9, 0.9 and 0.98 at
  most round the loop) and are not capped from outside.
- **An effect on a bus is told so** (`Effect::setOnBus`, by `Engine::setEffect`
  and `setBusEffect`), gives back no dry sound, and starts with its MIX, which
  reads LEVEL there, at full (`effectDefault(kind, i, true)`).
- **Flush-to-zero is ON in the program and in a bounce** (owner, 2026-11-11;
  `Engine::setFlushDenormals(true)` in `main.cpp` and `bounce.cpp`), set for
  each `render()` and on the helpers. It changes the last vanishing samples of
  a tail (all below 1e-30). **The tests leave it off**, which is why the
  pinned checksums still match: do not turn it on in `effects_test`. TAPE and HALL do not
  rely on it: they look after themselves (next rule).
- **A new effect glides its settings and looks after its own small numbers**
  (`fx/glide.hpp`; TAPE and HALL are the pattern). Every setting goes through
  a `Glide` (20 to 30 ms, and it lands exactly); anything worked out from a
  setting (HALL's eight gains) slides sample by sample too, never a step a
  block. Anything that fades is fed a floor of 1e-18 or gated at `kQuiet`, and
  the effect goes to sleep (exact zeros, no work) when its memory is empty: it
  must make **no number too small to hold with flush-to-zero off**, and
  `TooSmall` in `effects_test` checks that without a clock. HALL with its
  floor taken out ran 10 times slower two seconds into a tail and 40 times
  slower for good. A delay or a reverb reads `Unit::bus_` in one place for
  the bus call (TAPE and HALL had their own `wetOnly_`; it is gone).
- **A new effect calls `soundOnly()` first thing in `process()`**
  (`fx/glide.hpp`): a sample that is not a number, infinite or beyond 1e8 is
  taken as silence. One such sample used to stay in a delay line, a filter or
  COMP's level reading for good. The old five do not (their arithmetic is not
  touched). `effects_test` tries every effect newer than CRUSH, each choice
  of each setting, and fails a new one that forgets.
- **A `Glide` always lands, at any block length** (never slower than `kLeast`
  a sample). A fraction of what is left falls below what a float can add in
  blocks of 8 frames and under, and `moving()` then stayed true for good.
- **A change of curve waits for the fade before it** (SATURATE's TYPE: one
  fade at a time, the last one asked for wins). Starting a second fade inside
  the first cut the fading curve off: a click.
- **A sleeping effect that wakes starts as a fresh one** (settings snap, the
  wow and the bend start again), so a render does not depend on how long ago
  the last sound was.
- **DUCK is told the hits, it does not listen** (`Effect::drumHits`, bit 0 is
  lane 1). A hit always lands on a chunk's first sample (`render()` ends a
  chunk where the next is due), so the engine marks it where it triggers the
  drum (`Track::hitsNow`), and `renderChunk` copies the first drum track's
  marks to `chunkHits_` and clears them **before the tracks are shared out**;
  helpers only read it. Keep it that way: written during the chunk, a ducked
  track would depend on which helper got to the drums first. A new way to
  trigger a drum must set the mark too. Every trigger counts (ratchet pieces,
  hits by hand); a bus's slot is not told.
- **The MIDI thread only uses `sendLive`** (notes and drum hits, its own queue).
- **A hit written into a cell sends that one hit** (`Session::sendDrumHit`),
  never the whole cell again: a keyboard's eight pads at once on a busy
  eight-bar cell filled the command queue (`midi_test`).
- **What an effect shows goes through `Engine::effectMeter(track, slot)`**
  (COMP: the dB it is turning down). The audio thread reads the effect's
  `meter()` after each chunk and keeps it in an atomic; the screen asks the
  engine. **Nothing on the app thread may hold or call an `Effect` that is in
  a slot**: the audio thread can swap it out at any chunk. The FX screen draws
  it three ways: on the row `effectMeterParam(kind)` names (COMP: THRESHOLD; DUCK: AMOUNT), as a
  strip under the bar from the right, full at `kMeterFullDb` (24); in words in
  the heading (`FxScreen::meterWords`: TURNING DOWN 7 DB, DIPPING 3 DB); and
  as a brass mark on the effect's line of the list.
- **A new effect's sound does not depend on how it is cut into blocks**, to
  the bit, once its settings are at rest (each has a test). So what an effect
  does "between blocks" (snapping vanishing numbers to zero) is done sample by
  sample, or on a fixed count of samples (SATURATE: every 64th it has been
  given), never "at the end of this call". The same goes for anything slow
  that moves by itself: TAPE's wow was a sine worked out at each end of a
  call, and its count of time left alone went up a block at a time; both
  made a bounce differ from live play. **TAPE's test cuts 150 s four ways**
  (`testTapeAlwaysEnds`); a new effect with a tail gets the same, through its
  sleep and its waking (PING: 70 s, three settings, both sides, in `testPing`). **HALL does not pass it yet** (apart from 0.02 s in
  blocks of 64 and of 48; as on `main`, not changed). PLATE does (`testPlate`:
  its sleep, its waking and its bend are each on their own sample, as TAPE's).
- **PING (`fx/ping.cpp`) is the plain pattern for a delay**: two lines, the
  sound in on the left one (both sides halved and added, so a mono sound
  bounces), each line's echo on to the other through a high cut and FEEDBACK.
  **Nothing under `kQuiet` goes on a line; an exact zero does**, so "asleep"
  is "both lines are zeros from end to end" and waking is a fresh effect to
  the bit (TAPE leaves what is under 240 dB down on its tape: 1e-9 apart).
  So a steady sine's samples at its zero crossings (under 1e-12) are not
  echoed. **Its tail leans left** (each right echo is FEEDBACK of the left before it:
  7.8 dB over a tail at FEEDBACK 50%, each echo 0.45 of the last; a sound hard right echoes first on
  the left; the owner's call, pinned in `testPing`). **Its FEEDBACK reads 0 to 100% and does nine
  tenths of it, exactly as CLEAN's** (owner, 2026-11-14; it read 0 to 90%, and the D-pad's stops read
  5, 9, 14, 18...). Only the reading changed: the same setting does what it did, so old songs sound
  as they did. CLEAN and PING are one scale (carried to the bit); TAPE reads to 120% (carried as it
  reads, 50% to 0.42). Its loop is still 0.9 at most: the tail always ends.
- **A filter state does not reach zero by itself**: with a coefficient over a
  half it sticks at the smallest number there is. Snap it (`< 1e-20f`), or
  "silence in, silence out" fails and a processor without flush-to-zero crawls.
- **A wet sound that comes out late is never mixed with the plain sound as it
  came in.** Two samples late is half a wave at 12 kHz, and the two cancel (a
  comb filter). SATURATE's 2x stage does this; its plain sound goes through
  `Halfband::late` first, except at MIX 0 exactly. Test a new effect's
  response at MIX 50%, not only at 0 and 100%.
- **A new effect glides every setting** (`fx::Glide` in `fx/glide.hpp`, the one such helper): a
  turned setting slides for about 20 ms and then arrives exactly, so MIX at 0
  is still the sound untouched to the sample. The first block after it is
  made or reset starts where the settings are (`snap`), with no slide. The
  old five do not glide and must not start to.

### The app
- **Anything below zero is not a track**: -1 the master, -2 and -3 the buses.
  `Session::effectData`, `setEffect` and `setEffectParam` take all three
  (`slotsOf(track)` bounds the slot). Test `track >= 0` before touching
  `project.tracks[track]` or automation, never `track != kMasterTrack`: a bus
  would then index the array at -2. A bus takes only its own kind
  (`effectBus`), and nothing on a bus or the master is automated.
- **The FX screen is two columns: a list of eight lines, always all in view,
  and the line picked drawn large** (owner, 2026-11-12; the EVERYTHING page
  was never found, so there is no second page and Y does nothing there).
  The lines (`FxScreen::goTo(row, column)`): 0 to 2 the track's slots, 3 its
  SENDS (`kSendsRow`: not a slot, so `data(row)` is not for it), 4 and 5 the
  buses (`kBusRow`), 6 and 7 the master (`kMasterRow`). **Column 0 is the line
  in the list; 1 and up are its settings** (SENDS: 1 ECHO, 2 SPACE).
  - Up / down in the list goes round; right goes to the setting the line
    last used (`last_`, kept through a change of track); left comes back.
    `update()` only pulls the cursor in when the line has fewer settings now.
  - **Hold A on a slot's line opens the list of effects** (`picking()`), drawn
    from `familyAt`: DOWN is the next line of it, since it is seen (it was UP
    when the list was stepped through blind). Left / right is the TYPE. The
    group jump is gone. On a setting, A + up / down turns it as before, and
    left / right do nothing.
  - **With A held the list only TRIES** (owner, 2026-11-14: passing over the
    list threw a tuned effect away). The PROJECT is not touched while A is
    held: `tryKind` sends the engine's slot the effect under the cursor
    (`Engine::setEffect` and `SetEffectParam`: the usual 10 ms fade), and
    `Command::HoldEffectAutomation` holds that slot's automation in the engine
    so the pattern does not move the effect tried. `data(row)` (`onLine`) is
    the tried one meanwhile. **Let go of A** (`letGo`): another family goes in
    through `Session::setEffect` (its first type, then `changeEffectType` to the
    type picked), another type of the same family through `changeEffectType`
    from the effect it started with; what was heard is what goes in (the test
    holds the two equal). **Let go on the effect it started on, B with A held,
    another screen up (`App::update` calls `callOff`) or another track**: the
    engine is sent what the project holds, so the slot is as it was to the bit
    (its automation never moved). The family it started in is tried as the
    effect it started with, not afresh, which is what lets "down and back up"
    land on it exactly. LEFT / RIGHT is tried too, carried from the starting
    effect (not from what was passed over). The effect committed is installed
    afresh once more (a second fade, a tail heard while trying is cut): the
    Session has no "put in without telling the engine". **The hold is let go
    by the slot kept in `trySlot_`, after the commit** (letting go by
    `tryRow_`, already -1 by then, left the slot's automation dead in the
    engine for good: found in review, 2026-11-14). A hold is said again with
    every effect tried, and `RemoveInstrument` clears it, so a song sent again
    (a full queue) holds nothing back. `app_test` (`testTryingAnEffectKeepsIt`
    reads `Engine::automationHeld` after every way out, and listens to a
    committed PING's MIX automation), `effects_test` (`testHeldAutomation`).
  - **The list of effects is eight lines, EMPTY and the seven families**
    (`kPickLines`), each a family's name and, from `kPickTypesX`, its types
    in small letters with ", " between ("clean, tape, ping"), the slot's type
    in brass on the cursor's line. In one row at the size chosen if they fit
    `kPickTypesRoom`, else held at LARGE (MOD's five planned at XL), else two
    rows at LARGE (`pickRows`; no planned name needs it, made-up ones as long
    as SATURATE do). `textsize_test` (`testTheListOfEffectsHoldsTheLaterTypes`)
    holds the fifteen planned types to it at every size.
  - **The heading's TYPE row shows as many types as fit** about the one the
    slot is, "..." (`kTypeMore`) for the rest (`typesInView`, in the room the
    row keeps for the LEFT / RIGHT key held or not). Today's all fit; six
    planned DELAYs at LARGE do not. A family of one shows its type all the
    same where it is not the family's word (MOD: TYPE CHORUS), with no key.
  - **L1 / R1 is the track before / after** (owner, 2026-11-14: a send on
    another track cost five presses). `FxScreen::stepTrack`: it stops at the
    first and the last track, as the SONG screen's columns do (nothing in the
    program wraps tracks); the line and `last_` stay; an effect being tried
    is called off first. DEVICE keeps its own L1 / R1 (pages, record, slices).
    The list's hints were shortened to fit it (settings, effects, sends):
    `testFxStepsTheTrack` holds every line and column to the bar. **A panic
    puts the track back** (`App::trackBeforeShoulders_`, as it puts back the
    tab and the slices): the four shoulders are seldom down at once, and L1 or
    R1 first stepped the track on the way. The help card does not say L1 / R1
    here: it lists what is true on every screen, and it is full.
  - **Hold B deletes an effect** (a slot's line or a bus's: a bar fills for
    `kDeleteHoldMs`, a second, on the line and the heading, which says
    `kDeletingWords`; a tap does nothing; any other button or a move calls it
    off; pit stop 2026-11-14; `app_test --hands` runs these tests). A tap of B on SENDS still empties both sends,
    and on a setting gives its usual value; a bus's LEVEL goes back to full
    (`effectDefault(kind, i, true)`).
  - A choice has no bar: one piece for each choice, the one it is lit
    (`wideBar`). A value is drawn in the largest lettering that fits
    `kValueRoom` (`Fonts::fit`); `app_test` (`testFxTextsFit`) draws every
    setting of every effect at 33 values and looks at the screen.
  - **In a line of the list a type stands 7 pixels after its family; for one
    too long for that the family is drawn in the type's smaller lettering, or
    the line is held at LARGE** (`FxScreen::lineFonts`: REVERB SPRING is 4
    pixels too long; at XL both are drawn at LARGE). Never close the gap up:
    at 2.9 REVERB SPRING read as one word (`app_test` counts the empty
    columns). A pair too long even with both small at LARGE is drawn as its
    type alone, in the family's lettering (`typeAlone`: DRIVE SATURATE is 4
    pixels over, and the line says SATURATE: the owner is content with that, 2026-11-14). The tests ask the same
    function. A new type's name is checked there at every size.
  - **The page goes down to `kPageBottom`** (449: 7 above the hints bar, as
    its top is 7 under the top bar; owner, 2026-11-13: it stopped 40 short).
    The same layout, taller: a line of the list is `kLineH` 41 (was 36), a
    setting's row `kSetH` 74 (was 66), the heading `kHeadH` 93 (was 92), a
    send's row `kSendH` 193 (was 128; its value, bar and purple line at
    `kSendValueY`, `kSendBarY`, `kSendLineY`, the last two from its foot), a
    line of the list of effects `kPickLineH` 38 (was 31). `fx_screen.hpp`
    works each out from the foot and `static_assert`s that all four columns
    end on it.
  - What is automated in the scene worked on has a purple line under its row
    and "moves with the pattern", and a purple line under its line of the
    list. WRITE itself is said by the top bar.
  - The places things are drawn are constants in `fx_screen.hpp`, so the
    tests can read the drawn screen (`testTheFxScreenAsDrawn`, with `inked`:
    is this text drawn here in this colour). `app_test --fx` runs just the FX
    screen's tests (about 9 s).
- **LIVE OUTPUT is a thin strip across the DEVICE page, just above the hints
  bar** (owner, 2026-11-14: the half-width box in the bottom right was too big
  and left room unused). `ui::scopeStrip`, its places in `ui.hpp` (`kScopeY`
  416, `kScopeH` 36, its label inside at the left in `kScopeLabelW`). The
  synths' MAIN and settings pages, the sampler and the kit end their controls
  at `ui::kAboveScope` (406) and spread them down to it: MAIN's macros are
  `SynthScreen::kMacroH` 114 (was 70), a settings page's rows `settingH(rows)`
  (four rows 82, three 111, two 170, one 346: however few, they reach the
  strip; no empty band), the kit's lanes `kLaneRowH` 45 (was 37), the
  sampler's settings 109 and its recording's picture `kWaveH` 80 (was 40;
  with no recording its words stand in that room's middle). The line is
  placed by `ui::scopeLineY` (full scale 4 rows inside the strip's edges). A box taller than `kBoxUsualH` (70) moves its value down by half
  the extra (`boxValueDrop`); at 70 and under nothing moves. The envelope
  pages never had the scope and do not. `scope_test` reads it all off the
  drawn screen in BROWN and PAPER.
- **A new `Session` tells the engine what the buses hold** (its constructor).
  The engine starts as a new song in every other way, but with empty buses;
  without this a handheld with no saved song had sends that did nothing.
- **`Project` is about 12 MB. Never put one on the stack**, nor a `TrackData`
  (about 1 MB; measured 2026-11-14: 12,061,000 and 1,005,060 bytes).
  `std::unique_ptr<Project>`. Not even a temporary: `x = TrackData()` is one
  on the stack (`parseSong` took 1 MB of stack that way, and it runs on the
  WiFi server's thread too); copy a blank one kept on the heap.
  `save_test` reads a song on a thread with a 256 KB stack.
- **Every screen's `update()` re-reads held buttons from `input` each frame**
  and drops its own "held" flags when they are not held. Trusting the release
  event left A "stuck down" after a screen change.
- **Undo is a text snapshot of the patterns** (`songText`), one step per burst
  of edits, 20 deep. Anything that changes a pattern outside the grids calls
  `noteEdit()`. Sounds, effects and track deletes are not undone.
  **Only another song clears undo** (`applyProject`). A resync after a full
  queue, a track deleted and a track given another instrument send the song
  with `sendSong()` and keep the steps: a delete takes its track out of every
  step's text (`dropTrackText`), and another instrument is passed by (its kind
  differs). The delete itself is still not undone.
- **The song's text is made a track at a time** (`SongTextCache`,
  `Session::textNow()`): a track whose bytes are as they were when its text
  was made is not written again. The autosave, the end of a burst of edits and
  undo all use it, so with nothing changed the autosave is a compare of memory
  (2.5 MB of text: 0.8 ms on the desk, was 68). The copies it keeps cost
  about 1 MB a track (11.8 MB for twelve). **An undo reads back only the
  tracks whose text differs from the step's** (`Session::readStep`; the rest
  get a stand-in line that gives their kind and cells' lengths). A scene move
  reads the step whole (it takes the sixteenth scene from it).
  `readUndoWhole(true)` reads every step whole, as before: `app_test`
  (`testUndoIsAsItWasReadWhole`) holds the two the same. **A track whose
  numbers differ from the step's but whose text is the same is left alone**
  (compared by `trackText`, as the file writes it): a value the text rounds
  (a keyboard's 90/127) made one undo send all twelve tracks whole.
  `app_test --undo` runs the undo tests alone. Do not give the cache
  a count of changes instead: the screens write into the project in many
  places, and one that forgot to count is work never saved.
- **Scenes are numbered by their place, so a scene put in moves the ones
  after it** (NEXT SCENE, SELECT + Y: `Session::nextScene`). Whatever holds a
  scene's number must follow: the engine's playing, its patterns and an
  arpeggio that is playing (`Command::ScenesMoved`, next rule),
  `project.playScene`, a keyboard note being recorded (`Session::scenesMoved`
  does those two and counts the move), and the player (`UndoStep::nextScene`
  is how undo and redo know to move them). Its tests: `tests/nextscene_test.cpp`.
- **A scene move is ONE command, and the audio thread copies nothing**
  (`Engine::moveScenes`, which sends `Command::ScenesMoved`). The engine keeps
  each scene of each track as a piece of its own (`Track::scenes`: sixteen
  pointers to a `SceneStore`; `drumScenes`, `notes`, `bars` and `automation`
  read them as the one run they used to be). The command moves the pointers,
  takes in the one scene that is new for each track (made on the app thread:
  `Session::sceneImage`), sets that scene's repeats and steps and the song's
  length, moves the playing, and hands the scene that fell off the end back
  through the trash. **It is all in one block of sound or none of it is**, so
  there is no order to keep and no case that plays a wrong cell. Sending a
  track's patterns one a frame, and "all at once when it must be", are gone:
  do not bring them back. **Anything new the engine keeps for a scene goes in
  `SceneStore`, or is moved by that command** (`repeats_`, `steps_`).
  Measured with 12 tracks (`nextscene_test --measure`, the desk): before, the
  block after the gesture took 2.1 to 3.8 ms with every track at once (0.5 on
  average and 1.3 at worst with one a frame) against a usual 0.21; now 0.30
  to 0.37 against a usual 0.24, **which is what any block costs straight after
  the program has been busy** (0.30 to 0.35 with nothing sent). The command
  by itself is 0.1 to 0.3 millionths of a second for twelve tracks. (Again
  later on a busy desk: 0.28 to 0.33 against a usual 0.22 to 0.24, and 0.28 to
  0.30 with nothing sent; a worst of several ms there is the machine taking
  the thread away, and shows as often in a block with no move in it.) The
  key itself is the program's thread: 1.3 ms, and 11 to 12 ms for its undo or
  redo (the song read back from text), **not timed on the handheld**.
  (2026-11-13, joined with chord effects, a slower run. The test's song has
  the chord track now, so its figures are not the ones above: a usual block
  0.30 to 0.34, the block that takes the move 0.43 to 0.47. With the song as
  it was: 0.27 to 0.31 and 0.38 to 0.45, against 0.26 to 0.30 and 0.38 to
  0.43 for the branch before the join in the same run. An ordinary block of
  twelve busy tracks, timed apart: 0.57 to 0.63 ms before the join, on `main`
  and joined alike. The step clock's three rows of `bench`, four runs each:
  0.80 to 0.86%, 1.29 to 1.41% and 1.24 to 1.31% joined; 0.77 to 0.86%, 1.39
  to 1.42% and 1.24 to 1.35% on `main`.)
- **`nextscene_test` runs a real audio thread** beside 200 press and undo
  pairs, held to twelve blocks past the last key (a busy machine made minutes
  of sound otherwise). It is clean under the thread sanitizer
  (`-fsanitize=thread`, about nine minutes; sixteen with its build, on a busy
  desk, since the chord track and the helpers' round: clean again 2026-11-13, and once more that day with GLIDE FROM, fifteen minutes; the test's counted `new` has the kind that gives back nullptr too, which a sanitizer's own does not pass on); run that again after a change to
  what a scene move hands over.
- **Taking a NEXT SCENE back, or putting it back, sends no track whole**
  (`Session::restorePatterns` with a move: `moveScenesHere`, then
  `tellScenesMoved`). It then asks whether the PATTERNS, written as text, are
  the step's (`Session::patternsAre`), not whether the numbers match:
  automation read back from text is not the same float to the last bit, and a
  track found different for that alone had all its patterns sent (a megabyte
  the audio thread copies). The patterns alone, not `patternsNow()`: that is
  the whole song's text, and a knob turned since the copy is in it. Only if
  they differ (nothing in the program does that without a step of undo of its
  own) is a track put back and sent whole, as an ordinary undo does.
  `Session::imagesSent()` is how the tests see that none went.
- **Where a scene move cannot be "the song played straight through"**: the
  copy is playing in its own turn (the scene it was made from has had its
  plays this time round) and is taken back. The playing goes to the scene it
  was copied from, same step, bar and time round, so that scene is heard for
  longer than its plays. Nothing is cut and no other scene's cell is played.
  Everywhere else the tests hold the sound to 0 difference: beside the song
  left alone while one scene goes round, and beside a song built with the
  copy in it while the song plays through (press, undo, redo; also with a
  later scene playing, and with a real audio thread making blocks back to back).
- **A known fault, not mended, on `main` too** (found in review,
  2026-11-13): the song plays through, and a copy is taken back during the
  LAST STEP of the scene it was copied from. That step has already asked a
  step ahead for the early notes and humanised chords of the bar that
  follows, which was the copy; the first step of the scene that now follows
  plays wrong (0.28 from the song left alone; taken back half a step sooner,
  0). One sixteenth note wide. `nextscene_test --edge` prints it; it is not
  in `ctest`. The mend is in what `fireStep` has asked ahead, and is the
  owner's call.
- **What the program remembers it sent is set only when the send went**
  (`Session::syncSong`: `sentSceneCount_`). Set before, a scene move lost to a
  full queue left the engine with the old song length for good: the song sent
  again found nothing to send. `nextscene_test` fills the queue to hold it.
- **`sceneIn` (engine.cpp) is the one place a pattern is read into the
  engine**, for a whole track (`SetTrackImage`) and for one scene. A new
  field of a note or a hit is copied there, once; `sceneInto` (session.cpp)
  is the one place the app writes a scene out for it. A chord's `play` and a
  glide's length go through both (`glideKind`, `chordPlayChecked`).
  `nextscene_test` hands a scene over with them beside the same notes written
  by commands, and beside the bars the piano roll sends.
- **What plays while a scene moves goes with it, and nothing of a chord is
  worked out from a scene's number** (the join of chord effects and the scene
  move, 2026-11-13). `chordSeed` takes the bar of the CELL, not the bar of the
  song: with the scene's number in it a humanised chord rolled another way
  after NEXT SCENE, and no song left alone or built the finished way matched.
  So a chord effect in scene 2 or later rolls other numbers than it did
  before the join (scene 1 is the same to the bit). `Track::Arp::slot` is a
  bar of the song, so `ScenesMoved` moves it by the rule the playing moves by;
  an arpeggio whose scene has no place to go to (the sixteenth when one goes
  in, the one taken out) stays and looks whether its chord is still there
  (`Arp::look`). Waiting notes, `chordAsked`, `strokes` and the `GlideTrail`
  hold no scene number: time is steps since Play. **Anything new that the
  engine keeps while a chord plays either holds no scene number or is moved
  there.**
- **The song `nextscene_test` plays (`fill`) has a track of chords strummed,
  as arpeggios, humanised, ratcheted, thrown, in every ORDER, and a glide of
  every length, on a track whose GLIDE FROM is PAIRED**, so every proof of the sound has them in flight when the
  scene moves. The proofs marked `sound` in its table run again with the
  tracks on three helper threads, and the song left alone is held to the same
  sound on none and on three. `nextscene_test --only NAME...` runs just
  those; `--times` says how long each takes (the whole is near its two
  minutes on a busy machine: put a long new proof in the helpers' round only
  if it earns it).
- **Ask `Session::playScene()`, never `engine.playScene()`, for the scene
  that is playing.** Until the engine has heard a scene move (its next
  block) it still says the old number: the
  playhead left the grid and WRITE went into the scene left behind. The
  engine counts the moves it has taken in the same atomic as the scene
  (`Engine::playScene(&heard)`), so the two are never from different moments.
- **The SONG screen's cursor is a scene's number too** (`SongScreen::scenesMoved`,
  called by `App` after the gesture, an undo or a redo).
- **Hints must fit.** The hints bar drops what does not fit; do not rely on
  that, shorten the words. The bar draws `HOLD` as `HLD`, `SELECT` as `SEL`,
  directions as arrows, and the one word `dice` as a die (`ui.cpp`): write
  hints in full. `ui::gHintsOver` says how many pixels the last bar was
  short of holding every hint and the help: `app_test`
  (`testTheHintsFitAndSayWhatTheyDo`) holds the SONG screen and the drum grid
  to 0 with either help (pit stop, 2026-11-14: SONG's cell leaves mute and
  solo to the head, the drum grid lists a hit's own jobs on a hit and the
  brushes on an empty step). **The piano roll on a note is still 48 to 69
  pixels over** (its help is not drawn): not mended.
- **With X held on a hit, a note or a chord, each button's chip stands under
  the field it sets** (owner, 2026-11-12; `ui::hintsUnder`): no word, the
  field's label says it; the dice alone at the left (a chord: at the right). On a note the
  fourth chip is L1 / R1 under GLIDE FROM, and NUDGE, which nothing sets there, is not drawn
  until X is let go. **The chip and the field take their x from one
  constant** (`kFx...X` in `pattern_layout.hpp`; `kStripHeld...X` and
  `kChordFx...X` in `note_marks.hpp`): never write the number twice. On a
  chord LEFT / RIGHT is under the picked effect's large value at the left
  (`kChordFxSetX`), UP / DOWN under the list's names, and the dice at the
  right-hand end (owner, 2026-11-13). The chips are the bar's own (`hintChip`): do not draw
  another kind. The help's words need eleven pixels clear after the last
  chip, or they give way: they do on a drum hit and a chord, where THROW's
  chip is at the right, and on a note, where GLIDE FROM's is. `tests/under_test.cpp` reads all of it off the drawn
  screen, and that the plain bar is back, pixel for pixel, with X let go.
- **The device writes SEL, never SELECT** (owner, 2026-11-12). A hint's key
  and a help-card key are written in full and drawn short (`keyWords`); any
  other text on the screen is written `SEL` ("also SEL + L1 / R1"); a button's
  name goes through `buttonLabel`, never `buttonName` (that is the word in
  controls.txt, which stays SELECT). The web pages and README keep the full
  word. `ctest -R words` (`tests/words_test.sh`) reads the sources and fails
  on a string that would show it spelled out; it plants its own faults at the
  end and must catch each (add a plant with a new rule).
- **The lettering's size is a setting, and the program holds TWO sets of
  fonts** (owner, 2026-11-12; SET page, TEXT SIZE: SMALL, MEDIUM, LARGE, XL;
  `kTextPoints` in `ui.hpp`; kept as `text N` in settings.txt, the numbers
  appended, no line reads LARGE). A screen is handed the chosen set;
  `fonts.fixed()` is the other, always LARGE, which every screen was laid
  out at. `main.cpp` loads the chosen set again between two frames when
  `Session::textSize()` changes.
  - **The tight rows are drawn with `fixed()`**, so they are the same pixels
    at every size: the top bar (and the tempo's box), the hints bar and
    `hintsUnder` (inside `ui.cpp`: hand them the set you were given), the
    strip under the drum grid and under the piano roll (`tight` in both),
    anything written over a little picture (a cell's bar count on SONG, a
    recording's seconds in the list), the heads of narrow SONG columns, the
    controls setup, the offer at the start (`ui::relearnOffer`). The tests that read a strip or a bar off the screen
    draw at LARGE and hold at every size for that reason.
  - **Everything else follows the setting, and a new text is checked at XL.**
    Where it would not fit its room at XL it is drawn with
    `fonts.within(&Fonts::small, text, room)`: the chosen size if it fits,
    LARGE if not. Never cut it shorter than LARGE cuts it, never move it,
    never change the words. A name cut by width is cut with the font
    `within` gave. A text that stands on a line is placed from the line
    (the roll's OCTAVE: `line - capHeight`), not at a number that was right
    at 12 pixels.
  - **A held text takes its row with it.** What shares a line with a text
    held at LARGE is held too (a song's name and OPEN NOW > REVS; a SET
    row's value and its note; all sixteen step numbers of a roll's bar of
    three figures), and it is placed with the capitals' height of the font
    it is DRAWN in, not the chosen one's. A text that is made at run time
    (a name, a number, what a toast says) is checked at its longest: no
    still shows a ten-W track name or scene 10's 127 STEPS OF 128.
  - **A font loaded again lets its old letters go** (`Font::sheets()` counts
    them; a test holds it), and the fonts are unloaded before the renderer
    is destroyed (`main.cpp`'s end).
  - **`ctest -R text` holds it.** `textviews` has the program draw every
    still it knows at all four sizes (`--texts FILE` writes down each text
    and rectangle; `tests/textfit.hpp` measures them against LARGE: the same
    words, no text out of its box, into its neighbour, or into the bar beside
    it). **A new `--view` goes in `kViews` in `tests/stills.hpp`**; the
    test reads `main.cpp` and fails on one that is missing. `textsize` draws
    the screens the stills do not reach (every FX setting, long names,
    twelve tracks) and checks the tight rows pixel for pixel. A new screen or
    state with words in it gets a `holds(rig, "what")` there.
  - A still at a size: `--text-size xl`.
- **The colours are a THEME** (owner, 2026-11-13; SET page, THEME: BROWN,
  BLUE, GREEN, BLACK, PAPER, GREY; `kThemeTable` in `ui.hpp`; kept as
  `theme N` in settings.txt, the numbers appended, no line reads BROWN, which
  is the look as built to the number). `ui::kBg` ... `ui::kStop` are
  references into `ui::gTheme`, which `App::draw` (and main.cpp, before the
  controls setup) sets from `Session::theme()` each frame. A theme changes
  colours only, never a size or a place.
  - **A new colour goes through the ten names (or `faded()` of one): never
    numbers of its own.** Each name is a JOB (`ui.hpp` says which), kept in
    every theme; pick by job, not by hue.
  - **A new mark is checked on a light theme** (`--theme paper`). A light
    theme keeps the order of the surfaces (bg the deepest, then panel,
    panel2) with its line darker than them; text on a chip filled with an
    accent or with text is written in `kBg`; a wash is `faded(kBg, ...)`.
    Anything that only works light-on-dark breaks there.
  - **A thing drawn as an object, not a surface, asks `ui::lightTheme()`.**
    The roll's keyboard is the one: by job its white keys were dim and its
    black keys the background, a negative on PAPER and GREY. On a light
    theme the white keys are panel2 and the black keys text.
  - A new theme meets the contrast in `theme_test` (text 7, dim 4.5, an
    accent 4.5 on every surface and under bg letters, accents and the two
    letterings 20 apart in delta E). BROWN is measured, not held to it (its
    dim is 4.16 on panel2), and stays as it is. **Every theme, BROWN
    included:** the cursor's colour 1.4 or more in lightness from purple and
    from dim (PAPER and GREY were 1.12 and 1.01 at first: the cursor's
    outline and the purple line beside it read as one dark border), and the
    four accents 15 apart in delta E as a reader without red or without
    green sees them (simulated, not seen by a person).
  - **`ctest -R theme` holds it.** `themeviews` has the program draw every
    still in all six (`--themes PREFIX`) and finds, by pixels, any colour
    that is not the theme's or one of its laid over another (`Explain`); a
    new alpha given to `faded()` goes in its list there. A still in a theme:
    `--theme paper`.
- **The SET page shows twelve rows and moves one row at a time**
  (`SettingsScreen::firstShown`, `rowNowY`): thirteen since THEME. A new row
  costs nothing more than its place in `Row`.
  A row's bar starts after its value's words and ends short of what is said
  at the right, at every text size (`SettingsScreen::barPlace`: BOUNCE's
  WORKING 37% / TAIL 12 S; VOLUME's and STICK's stay at 250, 110 wide).
- **A check of what is DRAWN uses `tests/canvas.hpp`**: a 640x480 screen in
  memory with the program's lettering. `ui_test` looks at the hints bar's
  pixels and `app_test` at the help card's boxes; a check of the text alone
  passed with the bar drawing the key as written.
- **Hold X + Y is the dice** for what a hit or a note does (`fxRoll` on both
  grids). **Hold X + A is THROW** (`fxThrow` on both): OFF, ECHO, SPACE, BOTH.
  The check for X held comes before A's own jobs, so A alone still places.
  **The dice do not throw**, and do not take a THROW away (owner's decision).
  **Hold X + L1 / R1 on a note is the track's GLIDE FROM** (the glide rules, below).
- **X HELD hears a recording until it is let go, to its end** (owner,
  2026-11-14): the SAMPLER and KIT pages, their list, the drum grid's lane
  name and empty step, the FX screen and the roll's empty place on a sampler
  track. A tap still sounds `Session::kAuditionMs` (350); **the built-in
  kit tapped is heard whole**, as before (its cymbal is 1.8 s): `letGoHeard`
  stops a tapped lane only when it holds a recording. Screens call
  `Session::auditionHeld` (a synth key, held at `kHeldOn` in `sounding_`) or
  `hearLaneHeld` (a `Trigger` with `e = 1`); **`App::update` lets go**
  (`keepHearing`) when X is up, another screen is up or another track
  picked; Play, a new song and panic let go too. A drum lane's is stopped by
  `Command::StopAudition`, which fades (3 ms) only the voices and waiting
  ratchet pieces a held audition started (`HitPlay::audition`; a hit that
  carries the sound on takes it over; a lane plays one sound, so any
  audition chokes the hit before it anyway, joined or not). On a hit, X held stays the step
  effects' hold. The line over the picture stops with it (`heardEnds`).
  `tests/hear_test.cpp`. The SONG screen's X (`hearTrack`) is not held.
- **Sample names from `wavNames` have no `.wav` ending.** The file may end
  `.wav` or `.WAV`: whatever opens or names one tries both (`readRecording`,
  `recordingThere`, `deleteSample`).
- **The slice view is a mode of the drum grid** (`PatternScreen::slicing()`,
  drawn by `pattern_slices.cpp`), not a screen of its own, so every grid
  gesture works in it unchanged. A hit's slice is its `start`; there is no
  separate slice data. A lane has 16 slices, or its FIT in steps if longer.
- **A drum hit that starts where the lane's sound has just got to carries it
  on** (`DrumMachine::trigger`, within 1.5 ms, never from the very top). That
  is what makes slices in order seamless. Do not "simplify" it away.
- **Step effects are four small values on each drum hit** (`StepFx`, drums.hpp):
  pitch, ratchet, reverse, cut. Hold X on a hit sets them. In a song file they
  are `stepfx` lines (`fx` is an effect in a slot: the first try clashed).
  A ratchet waits in one `Pending` slot that re-arms itself.
- **Note effects are two bytes on a note** (`NoteEvent::ratchet`, `glide`; a
  chord has `ratchet` and its own five, next rule). They reach the engine in `Command::e` (low byte
  the ratchet, +256 a glide, and `glide << 11` for a length other than half a
  step, above THROW's two bits). In a song file: `notefx STEP KEY RATCHET
  GLIDE` and `chordfx STEP RATCHET`, each after its note.
- **Chord effects are a `ChordPlay` on a chord** (owner, 2026-11-12;
  `ChordEvent::play`, `engine/include/apotheca/chord_rule.hpp`): STRUM (0 off,
  1 STRUM, 2 WIDE STRUM, 3 / 4 / 5 an arpeggio at a quarter, half and one
  step), ORDER (0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM), and HUMAN TIME, HUMAN
  LEVEL and LENGTHS as percentages. The numbers are in songs: appended. The
  top figures are named constants there (`kHumanTimeMs` 12, `kHumanLevelMost`
  0.15, `kLengthsMost` 0.5, `kStrumMs` 9, `kWideStrumMs` 26).
  - **The engine does not know chords; each note says which key of its chord
    it is.** `NotePattern::render` fills `play.index` and `play.count`; they
    go in `Command::play` (SetNote) and `TrackImage::Note::play`. Anything
    new that writes chord notes out goes through `render`.
  - **A chord with nothing set takes the path it always did**
    (`play.atRest()` in `fireStep`): that is what keeps old songs to the bit.
    ORDER alone is "nothing set".
  - **Every roll is worked out from where and which time**
    (`chordSeed(track, bar of the cell, step, steps since Play)`, then `chordUnit` with
    what it is for and which note; not the scene's number, see the scene-move rules). Never a generator walked: that is what
    makes it the same on any helper count and block length, and Play from
    the top the same performance again. The only counted thing is
    `Track::strokes` (chords begun since Play, for UP-DOWN strums), counted in
    song order on the audio thread.
  - **Everything a chord effect times is a whole number of samples**
    (`kWholeFrames`: the delay, a ratchet's pieces, the note's end). A
    waiting note fires when under one sample is left, so a part of a sample
    plays a sample sooner or later by how the sound is cut into blocks. A
    nudged plain note and a plain ratchet still do (not changed: old songs at
    the handheld's block length). An arpeggio's clock fires as a step does,
    on the first sample at or after its time.
  - **A note that may be early is asked for a step ahead**, as a nudged-early
    note is. Which asking takes it is `nudge < 0 || play.time > 0`: nothing
    that depends on the tempo, or a tempo change between the two askings
    plays the note twice or not at all. The song's very first step has no
    step before it: early notes land on the top (`top`).
  - **A spread chord is still one moment to a glide.** Its notes say nothing
    to the `GlideTrail` when they sound (`kUntold`); a waiting note that only
    tells (`kTellOnly`) names each key at the chord's own moment. So what
    follows slides from its keys as written (which one: the track's GLIDE
    FROM; TOP is the top key), as `Project::glideLines` draws, however it is
    strummed. `chordfx_test`, `glide_test` and `glideapp_test` hold it.
  - **An arpeggio is one clock for the chord** (`Track::Arp`, three a track:
    one still playing, the next chord's waiting for its nudge, the one after
    asked a step ahead; the one that begins ends the one playing; with two,
    the third took the clock of the one still playing), not a waiting note
    per note: 512 would not fit. `clearPending` and a track taken away empty
    them; `SetTempo` keeps its place in the step. A ratchet is not played in
    an arpeggio. **An arpeggio ends with its chord**: a `SetNote` into its
    bar sets `Arp::look`, and before its next note it looks whether an
    arpeggio's note is still written on its step (by step, not by slot: a
    chord's keys change slots when a loose note is written).
  - **Which asking takes a chord is remembered, not read twice**
    (`Track::chordAsked`: the step, counted from Play, whose chord was asked
    a step ahead). A chord asked ahead is not begun again on its step; one
    not asked begins on its step, early keys with the step. Read from the
    values at each asking, a HUMAN TIME turned while the song played lost
    the chord once or played it twice.
  - **A ratcheted chord's hits are counted from the CHORD's length**
    (`startChordNote` has the ratchet's loop itself), not from the key's own
    (longer when early, shorter when late or under LENGTHS): keys of one
    chord played different numbers of hits. LENGTHS shortens the last hit
    only. No key is so late that its last hit has no chord left.
  - **A bounce plays the song once** (`Command::Play` with `c = 1`,
    `Engine::once_`): on the song's last step nothing early is asked for
    from its first (chords, notes and drum hits alike), or it sounds in the
    file's last moments and rings into the tail. Live, going round, it is.
  - **THROW holds for the whole chord** (`Note::hold` is set before LENGTHS
    shortens the note; an arpeggio's every note carries what is left).
  - In a song file: `chordplay STEP STRUM ORDER TIME LEVEL LENGTHS`, a line
    of its own after the chord (and its `chordfx`), **left out when all five
    are 0**. A new word and not more numbers on `chordfx`: an old program
    skips a word it does not know, and `chordfx` is written as it always was.
  - **On the roll, X held on a chord shares the directions out the other
    way**: up / down pick one of six (`NoteScreen::fxPick_`, kept from one
    hold to the next), left / right set it. A plain note's are as they were.
    The list, a click, the words and the dice's odds are
    `app/chord_effects.hpp`; the strip's places are in `note_marks.hpp`
    (`kChordFx...`). A chord broken into notes keeps none of the five.
  - **A chord's RATCHET lines and its light corner are drawn once** (owner,
    2026-11-13): on its lowest key that shows, the one under its name; on
    the next key clear of the name where the name sits on that one (step 1,
    nudged early), and the name is drawn again on top. THROW's foot is on
    every key. `chordfxapp_test` reads it off the drawn screen.
  - X hears a chord plain. The effects are the step clock's.
  - Its tests are their own two programs: `tests/chordfx_test.cpp` (the
    engine, with a listening synth) and `tests/chordfxapp_test.cpp` (the
    gesture, the strip off the drawn screen, the dice, the song file, undo,
    swap, break, drag, scenes). `chordfx_test testStrum testArpeggio` runs
    just those.
- **A glide is a length, and its number is not its place** (owner,
  2026-11-12; `engine/include/apotheca/glide_rule.hpp`). The numbers are in
  songs, appended: 0 off, **1 half a step (what ON was, so old songs sound as
  they did)**, 2 a quarter, 3 one step, 4 two, 5 FULL. The order a player
  steps through is `kGlideOrder` (`glideAt` / `glidePlace`). A number nobody
  knows reads as half a step (`glideKind`).
- **Which note a glide comes from, and for how long, is one rule in that
  header, and the engine and the roll both ask it** (`GlideTrail`,
  `glideSteps`, `glideLines`). Do not work either out anywhere else.
  The rule: the last note the track began (Play clears it); a ratchet's later
  pieces are not notes beginning; notes that begin within 1/256 of a step are
  one moment (none slides from another, what follows slides from one of its
  keys: GLIDE FROM, next rule, says which);
  FULL is the time since that moment began; on a ratchet never more than 85% of
  the first piece. The engine counts time in steps since Play (`stepsFired_`
  plus the nudge), not in samples, so both sides get the same numbers.
  `glide_test` holds the rule to the engine with a listening synth;
  `glideapp_test` holds `Project::glideLines` (what the roll draws) to the
  engine through the Session, chords included.
- **GLIDE FROM is the track's choice of which key of the moment before**
  (owner, 2026-11-13: "I always like the option"; `glideFromKey` in
  `glide_rule.hpp`). The numbers are in songs, appended: 0 TOP (the highest,
  as it was), 1 PAIRED, 2 NEAREST (a tie: the higher), 3 RANDOM. A number
  nobody knows is TOP (`glideFromKind`). The lengths do not change; "that
  note" is the key picked, and FULL counts from its moment.
  - **PAIRED**: new `i` of `n` (by pitch) from old `round(i * (m - 1) / (n - 1))`,
    a half rounded up; one note alone, from the top. **It counts every note
    of the moment**, a chord's keys too, gliding or not: a bass note under a
    chord comes from the bottom, a melody note over it from the top.
  - **The trail keeps every key of the last two moments** (`GlideKeys`: 128
    bits, one a key, so there is no cap and nothing is left out).
  - **PAIRED must know a note's whole moment when its first note begins.** So
    a PAIRED track's step is gone through twice by the one set of conditions
    (`fireStep`: `telling_`, then for real): the first time `startNote` and
    `startChordNote` only tell the trail what is coming (`tellComing`) and
    change nothing. The list holds 128 (`kGlideComing`: all a step can ask);
    one more is not kept and not counted. `glideLines` tells in the same whole
    steps, **and at the scene's last step also the early notes of the first
    step of the next time round**, as the engine's last step asks them ahead
    (a note half a step late there and one half a step early on step 1 are one
    moment: `testPairedWhereTheSceneGoesRound`). **`telling_` is false outside that loop**; anything new that begins
    a pattern's note inside `fireStep` goes through those two functions.
  - **Known, held by `testPairedAcrossAStepLine`, not hidden**: two notes
    within 1/256 of a step with a step line between them. The first is told a
    step before the second, so it counts as alone (from the top). The roll
    draws the same. Also: PAIRED turned on mid-step, that one step's notes
    count as alone.
  - **RANDOM is a roll for where and when** (`glideSeed(track, at)`: the track
    and the whole step nearest the moment, counted from Play; then
    `chordUnit(seed, kRollGlide, pitch)`). Never a generator walked, never the
    scene's number. **The roll draws the first time the scene plays after
    Play** (`glideLines`' `timesRound` 0; later rounds roll anew, and a scene
    that is not first in the song is first played at another time than the
    roll assumes). Play always starts on the scene worked on
    (`Session::togglePlay`), so the roll is right for the scene Play was
    pressed on, song or scene alone; a song played through INTO a later scene
    rolls other keys there. `testRandomOnALaterScene` holds both.
  - **It is the track's, not the patterns'**: `TrackData::glideFrom`,
    `Session::setGlideFrom`, `Command::SetGlideFrom`. Not in a track image,
    not in a scene, so a scene move carries nothing for it. `applyProject`
    tells every track's. `RemoveInstrument` puts the engine's back to TOP.
    **Not in undo** (like a send; the owner's call). In a song file:
    `glidefrom N`, a line of a synth track's own, left out at TOP; under a
    drum track it is read and not taken.
  - **The gesture: X held on a note, L1 back, R1 on**, a click, stopping at
    each end (`NoteScreen::fxFrom`). Zoom and carry wait for X to be let go,
    on a note only: on a chord L1 / R1 are as they were. The dice leave it
    alone. On the strip it has NUDGE's place while X is held
    (`kStripHeldFromX`): dim while the note has no glide, brass when not TOP.
  - Tests: `glide_test` (each type by hand-worked keys, chords strummed and
    arpeggios, ratchets, RANDOM's rolls, block lengths and helpers, the roll
    against the engine under each type), `glideapp_test` (the Session, the
    song file, undo, the lines' lean off the screen), `under_test` (the strip
    and the gesture), `nextscene_test` (`fill`'s chord track is PAIRED; one
    proof again under RANDOM).
- **The roll's glide line ends at the note's start** (the owner's picture);
  the slide is heard FROM the note's start. A line may begin bars before its
  note (FULL), or before the first bar's top, which is the scene's end: so
  the roll draws **every bar's lines wherever they cross the bar shown**
  (`glideShifts`, `glideSpan` in `note_marks.hpp`; drawing only the bar shown
  and the next left a slow line out of the bar it starts in). The line is
  rectangles gathered and drawn in one call (`glideLineRects`): a call a
  pixel cost 0.6 to 0.9 ms for 64 long lines on the desk, this 0.2 to 0.4
  (`GLIDE_PRINT=1 ./build/glideapp_test`; not timed on the handheld). `glideapp_test`
  reads the lines and the strip **off the drawn screen**
  (`testTheLinesOnTheScreen`, a software renderer and the real lettering).
- **Phazed's glide is the CZ's portamento given a rate**
  (`porta_rate16`, `third_party/CHANGES.md`): it moves on the CZ's 122 Hz
  tick. A note waiting for its slot keeps the rate and the key it was played
  with.
- **A recording's name may carry one folder**: `kicks/808`. Everything that
  takes a name from outside (a song file, the WiFi page, delete) checks it
  with `sampleNameOk` or the page's `plainName`. Show `sampleLabel(name)`.
- **THROW is a value on a hit, a note or a chord** (`throwTo`: 1 ECHO, 2
  SPACE, 3 both; `kThrowEcho`, `kThrowSpace` in drums.hpp). In a song file it
  is one more number at the end of a line, **left off at 0**, so an unthrown
  song is written as it always was: the seventh of `stepfx`, the sixth of
  `note` (the nudge is then written even at 0), the third of `chordfx` (the
  ratchet is then written even at 1). A chord's THROW is every key's.
- **Mute is its own gate** (`SetTrackHeard`), not the track level.
- **The WiFi page is PIN-gated and checks every file.** A program update must
  be accepted on the device (hold A). **Once accepted, the hints bar's end says
  RESTART TO UPDATE in brass until the program starts again** (owner,
  2026-11-14; `Session::updateReady`, `ui::gUpdateReady`): it takes the help
  words' place and is always drawn, the hints (and a strip's chips, the dice
  at the end too) giving way to it (`hintsEnd`). MENU still opens the help.
  `ui_test` (`testTheBarSaysAnUpdateWaits`) and `transfer_test` hold it;
  stills `update-ready`, `update-ready-drums`, `update-accepted`.
- **The page's own script has no test in `ctest`.** A change to it is tried
  in a browser (Playwright is installed) against a `Transfer` started by a
  small program, as the folder import was (`planFolders` can be called from
  `page.evaluate`). Say in the reply that it was, or was not.
- **The WiFi server's thread never touches the `Project`.** What it did to a
  recording goes on a list (`Transfer::takeChanges`), put there BEFORE
  `received()` goes up; `Session::update` reads it and calls
  `recordingChanged`. A rename changes the name only: reading the file again
  would cut the sound. A recording SENT (no old name) is read again on every
  lane and sampler track that names it, so a replaced file is heard at once.
- **A missing recording is looked for by base name, and taken only if there
  is exactly one** (`Session::findMissing`, run after a song or a rev opens,
  not from `applyProject`). It lists the samples folder once, and not at all
  when nothing is missing. Never guess between two.
- **A rename on the page rewrites the saved songs and revs** on the app
  thread (`Session::savedSongsFollow`, `renameRecordings`): only the name on
  `sample` and `sound` lines, the rest byte for byte, the file's date kept.
  The open song's own file is left to the autosave. It holds
  `Transfer::songFiles()`, which the server holds while it writes or deletes
  a song: anything new that writes songs off the app thread takes it too.
- **A bounce runs until its tail ends** (owner, 2026-11-12; `BounceTail` in
  `session.hpp`, no engine and no file in it, tested with made-up tails).
  Sound is `kBounceFloor` (one step of a 16-bit file) or more; it is over
  after `kBounceQuietSeconds` (4) without any, and the file ends half a second
  after the last sound. **Four seconds must stay longer than the longest gap
  between two echoes** (3 s: a half note at 40 BPM), as the bus rest time
  must. The cap (`kBounceCapSeconds`, 480) must stay longer than
  `kLongestTailSeconds` in the engine; both are checked.
- **A bounce is written as it goes**, to `NAME.wav.part`, and renamed when it
  is whole: a tail of minutes never sits in memory, and a cancelled bounce
  leaves nothing. One that is KILLED (power, a crash) leaves its `.wav.part`:
  `removeHalfBounces` takes those away when the program opens
  (`useBounceFolder`) and when a bounce starts, never while one is under way. The tracks of an EACH TRACK bounce are padded with silence
  to the longest. Its percentage counts the song's own playing only; in a
  tail `bounceTail()` gives the seconds so far and the row says that instead.
- **Do not pipe the app's output into `head`** when making a still; it dies
  before it saves.

### Tests
- **Mutation testing is the standard.** After a test passes, break the code it
  covers and see it fail. A surviving mutant usually means the fixture cannot
  reach the branch.
- **Do not compare audio sample for sample across separate notes** unless the
  engine is fresh; compare loudness, attack, or zero crossings. Let auditions
  end before listening.
- **Every test has a two-minute limit**, so a hang is a failure. A new ctest
  entry gets its `TIMEOUT 120` in CMakeLists.txt (launcher and words had none
  until 2026-11-14). **`effects_test` runs as two halves** (`--part 1`,
  `--part 2`; part 2 from `testFamiliesAndTypes`, so a test added at the end
  is in part 2; about 34 s each on a quiet desk, 2026-11-14). Move that line
  if one half grows much longer than the other.
- **Checksums of sound only hold on the machine's own maths library**
  (`tanh`, `pow`, `exp` differ in the last bit elsewhere, and compilers join
  a multiply and an add differently on ARM). `effects_test` checks which it is
  on (`mathsPrint`) and falls back to loudness and peak, saying so. To prove a
  change is bit-exact on another machine, run `--print` before and after and
  compare.
- **`effects_test --only testDuck testTape`** runs just those (a second or
  two, for trying a change); `ctest` runs them all. `through()` there hits a
  drum five times a second, lanes 1 and 8, so the checks for every effect
  reach DUCK; no other effect hears it.
- **Numbers measured from today's effects are written in `effects_test` as
  comments and checked** (`--measure` prints them). A new effect's row in the
  plan's testing table is checked with the same tools.
- When replacing a function by text range, bound it by **the next function in
  the file**, then count the functions before and after.

## Measured (desk machine; the handheld is roughly a fifth as fast)

| Thing | Cost |
| --- | --- |
| Phazed, 8 notes | 0.9% of a core |
| QMOD, 8 notes | 2.6% |
| Twin Analog x1 / x2 / x8, 8 notes | 4.7% / 9.1% / 34% |
| Drums, 8 lanes | 0.1% |
| FILTER / DRIVE / DELAY / REVERB / CRUSH, one each | 0.03% / 0.12% / 0.02% / 0.07% / 0.01% |
| TAPE / HALL / HALL with TONE above 50% (a later, slower run: REVERB read 0.09% in it) | 0.13% / 0.18% / 0.21% |
| Feeding both send buses from 12 tracks | 0.03% |
| ...and a DELAY and a REVERB on them | 0.12% |
| ...or TAPE and HALL, what a new song has (2026-11-12, a slower run: DRIVE 0.18%, DELAY and REVERB on the buses 0.09 to 0.18%) | 0.41 to 0.50% |
| Every hit of twelve busy drum tracks thrown to both buses, nothing on them (2026-11-12; the tracks themselves 1.45%, as before THROW) | 1.2% |
| CHORUS / COMP / SATURATE, one each (2026-11-12, a run where DRIVE read 0.16%) | 0.18% / 0.11% / 0.18% (0.15% before its plain sound was made late to match) |
| DUCK, dipping eight times a second (2026-11-12, a run where DRIVE read 0.12%) | 0.02% |
| SPRING (2026-11-13, four runs on a shared desk: HALL 0.15 to 0.17%, TAPE 0.10 to 0.14%, DRIVE 0.12 to 0.21% in them). Its springs run at a fifth of the sample rate | 0.13% (0.16% in one run) |
| TAPE with its way down, the same run (0.10% just before it) | 0.11% |
| PING (2026-11-13, four runs alike; in them DELAY 0.02%, TAPE 0.09 to 0.10%, HALL 0.15%, DRIVE 0.12 to 0.14%) | 0.02% |
| PLATE (2026-11-13, six runs: HALL 0.15 to 0.16%, TAPE 0.09 to 0.10% in the same) | 0.14 to 0.15% |
| ...TAPE and PLATE on the buses of twelve busy drum tracks (TAPE and HALL 0.28 to 0.39% in the same runs) | 0.28 to 0.42% |
| Everything at once on twelve busy drum tracks: every hit thrown to both buses, sends up, TAPE and HALL on them, a DUCK on each track (2026-11-12; the tracks themselves 1.44%, DRIVE 0.12%) | 1.70% |
| The step clock alone, eleven silent synth tracks, four plain chords of four keys a bar on each (2026-11-12; 0.68% before chord effects: the cost is looking through each track's waiting notes every chunk) | 0.69 to 0.72% |
| ...every chord WIDE STRUM, RANDOM, the three human amounts at 100%, x4 | 1.22% |
| ...every chord an arpeggio at 1/4 step, RANDOM, human at 100% | 1.14% |
| ...plain chords, two gliding notes after each, the tracks' GLIDE FROM at TOP / PAIRED / NEAREST / RANDOM (2026-11-13, three runs; plain chords alone 0.71 to 0.75% in them, 0.71 to 0.76% just before GLIDE FROM) | 0.71 to 0.72% each: the choice costs nothing that shows |
| The busiest song (`bench_run.cpp`, `busiestSong`: 12 tracks, chords, throws, three effects a track, buses) in real time, 256-frame blocks: the audio thread alone / with 2 helpers (2026-11-14; SPEED TEST shows both, so the handheld can say whether the helpers carry it; **not yet run on the handheld**) | 19.9% / 13.5% of a block, worst block 2.98 / 2.75 ms of 5.33 |
| One idle frame | 0.03 ms |
| A frame that ends an edit (undo snapshot) | 1 ms |
| Undo plus redo | 20 ms |

**Twin Analog is the costly one**, and its time is spread over plain
arithmetic with no single hot spot. Do not "optimise" it blind: any change
there is a change to the sound, and needs the owner's ears.

## Next, in the owner's order

1. The effects plan (`docs/plans/effects-plan.md`): sends, THROW and DUCK are built and joined; of the later set PING is built (branch `fx-ping`, to be joined); the seven families and the FX page to the hints bar are built (branch `families`, to be joined), their fifteen new types not yet.
2. LFO and random stepper per track.

Open: which chord packs ship, more presets, own WiFi hotspot, an automation
step-editor, a picture of QMOD's wiring. **Rethink Twin Analog** (owner,
2026-11-11): it has more features than it needs and is the costly synth; cut
it down rather than tune it. **Decided (owner, 2026-11-13): Twin Analog becomes a
faithful Roland JX-3P clone**, which is what it was meant to be; it grew past that
in VCV Rack. Queued after the pit stop. The engine stays at 48 kHz: 44.1 would save about
8%, which the owner judged not worth it.
