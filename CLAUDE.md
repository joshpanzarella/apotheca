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
| All tests (16) | `cd build && ctest -j8` (about 25 s) |
| Timings | `./build/bench`, or `apotheca --bench`. On the handheld: SPEED TEST on the WiFi page (the timings are in the program, `app/bench_run.cpp`, so an update over WiFi brings them) |
| A still of a screen | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --shot out.bmp` (views are listed in `app/main.cpp`; the FX screen's are `fx`, `fx-sends`, `fx-pick`, `fx-all`, `fx-bus`, `fx-level`, `fx-comp`, `fx-comp-all`, each also with `--hold-a`) |
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
third_party/  easycz (Phazed's engine), shapetaker/entanglement (QMOD's generator), signalsmith (stretch). Copied unchanged; do not edit.
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
| `engine/src/effects.cpp` | **the one table of effects**: each row's number, and the list a player picks from |
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
   choices, how a setting reads, how to make one);
3. its `extern` line at the bottom of `fx/unit.hpp`;
4. its row in `kTable` in `effects.cpp` and its place in `kList` there;
5. its checks in `tests/effects_test.cpp`, with the tools there.

The screen, the song file and `bench` pick it up from the table; the build
takes every `.cpp` in `engine/src/fx/`. Nothing else is edited.

**A delay or a reverb also goes on a send bus** (by its group: `effectBus`).
Its `process()` must look at `Unit::bus_` and, when set, put out only what it
adds (wet times MIX) and none of what came in. `effects_test` checks every
effect in those two groups and fails one that does not.

## Rules that bite

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

### The engine and its threads
- **The audio thread never allocates, locks for long, or frees.** Things it
  is done with go to the trash queue; `collectGarbage()` frees them.
- **Commands go through a queue that can fill.** `Engine::send` returns false
  and counts the loss (`dropped()`). `Session::update` notices and sends the
  whole song again (`applyProject`). Callers that hand over a pointer must
  check the return themselves.
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
- **A bus at rest is skipped whole and adds nothing, not even zeros.** That is
  why a song with no sends is the same to the last bit as before there were
  buses (`effects_test` holds the old song's checksum with an effect waiting
  on each bus). A bus rests when it has been sent nothing and has put out
  nothing above `kBusQuiet` (1e-6) for `kBusRestSeconds` (10 s); its effect is
  then `reset()`. Ten seconds must stay longer than the longest silence
  between two echoes of anything a bus can hold (`effects_test` measures each
  delay and reverb at 40 BPM: DELAY 3.0 s). A bus with no effect is not run.
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
- **The MIDI thread only uses `sendLive`** (notes and drum hits, its own queue).
- **What an effect shows goes through `Engine::effectMeter(track, slot)`**
  (COMP: the dB it is turning down). The audio thread reads the effect's
  `meter()` after each chunk and keeps it in an atomic; the screen asks the
  engine. **Nothing on the app thread may hold or call an `Effect` that is in
  a slot**: the audio thread can swap it out at any chunk. The FX screen draws
  it on the box `effectMeterParam(kind)` names (COMP: THRESHOLD), as a thin
  strip over the bar from the right, full at `kMeterFullDb` (24).
- **A new effect's sound does not depend on how it is cut into blocks**, to
  the bit, once its settings are at rest (each has a test). So what an effect
  does "between blocks" (snapping vanishing numbers to zero) is done sample by
  sample, or on a fixed count of samples (SATURATE: every 64th it has been
  given), never "at the end of this call".
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
- **The FX screen is two pages of four rows, and its rows are numbered
  through both** (`FxScreen::goTo`): 0 to 2 the track's slots, 3 its SENDS
  (`kSendsRow`: two columns, not a slot, so `data(row)` is not for it), 4 and
  5 the buses (`kBusRow`), 6 and 7 the master (`kMasterRow`). Y adds four. A
  bus's row picks from its own list (`busEffectAt`), shows its MIX as LEVEL,
  and B there gives `effectDefault(kind, i, true)`. Hold A + left / right is
  `effectGroupJump`: it reads the groups from the list, so a new effect needs
  nothing done for it.
- **A new `Session` tells the engine what the buses hold** (its constructor).
  The engine starts as a new song in every other way, but with empty buses;
  without this a handheld with no saved song had sends that did nothing.
- **`Project` is nearly 10 MB. Never put one on the stack**, nor a `TrackData`
  (821 KB). `std::unique_ptr<Project>`.
- **Every screen's `update()` re-reads held buttons from `input` each frame**
  and drops its own "held" flags when they are not held. Trusting the release
  event left A "stuck down" after a screen change.
- **Undo is a text snapshot of the patterns** (`songText`), one step per burst
  of edits, 20 deep. Anything that changes a pattern outside the grids calls
  `noteEdit()`. Sounds, effects and track deletes are not undone.
- **Hints must fit.** The hints bar drops what does not fit; do not rely on
  that, shorten the words. The bar draws `HOLD` as `HLD`, directions as
  arrows, and the one word `dice` as a die (`ui.cpp`): write hints in full.
- **Hold X + Y is the dice** for what a hit or a note does (`fxRoll` on both
  grids). **Hold X + A is kept for THROW.**
- **Sample names from `wavNames` have no `.wav` ending.**
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
  chord has `ratchet` only). They reach the engine in `Command::e` (low byte
  the ratchet, +256 a glide). In a song file: `notefx STEP KEY RATCHET GLIDE`
  and `chordfx STEP RATCHET`, each after its note. A glide comes from
  `track.lastPitch`, which Play resets, through `Instrument::noteOnFrom`.
- **A recording's name may carry one folder**: `kicks/808`. Everything that
  takes a name from outside (a song file, the WiFi page, delete) checks it
  with `sampleNameOk` or the page's `plainName`. Show `sampleLabel(name)`.
- **Mute is its own gate** (`SetTrackHeard`), not the track level.
- **The WiFi page is PIN-gated and checks every file.** A program update must
  be accepted on the device (hold A).
- **The page's own script has no test in `ctest`.** A change to it is tried
  in a browser (Playwright is installed) against a `Transfer` started by a
  small program, as the folder import was (`planFolders` can be called from
  `page.evaluate`). Say in the reply that it was, or was not.
- **The WiFi server's thread never touches the `Project`.** What it did to a
  recording goes on a list (`Transfer::takeChanges`), put there BEFORE
  `received()` goes up; `Session::update` reads it and calls
  `recordingChanged`. A rename changes the name only: reading the file again
  would cut the sound.
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
- **Do not pipe the app's output into `head`** when making a still; it dies
  before it saves.

### Tests
- **Mutation testing is the standard.** After a test passes, break the code it
  covers and see it fail. A surviving mutant usually means the fixture cannot
  reach the branch.
- **Do not compare audio sample for sample across separate notes** unless the
  engine is fresh; compare loudness, attack, or zero crossings. Let auditions
  end before listening.
- **Every test has a two-minute limit**, so a hang is a failure.
- **Checksums of sound only hold on the machine's own maths library**
  (`tanh`, `pow`, `exp` differ in the last bit elsewhere, and compilers join
  a multiply and an add differently on ARM). `effects_test` checks which it is
  on (`mathsPrint`) and falls back to loudness and peak, saying so. To prove a
  change is bit-exact on another machine, run `--print` before and after and
  compare.
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
| CHORUS / COMP / SATURATE, one each (2026-11-12, a run where DRIVE read 0.16%) | 0.18% / 0.11% / 0.18% (0.15% before its plain sound was made late to match) |
| One idle frame | 0.03 ms |
| A frame that ends an edit (undo snapshot) | 1 ms |
| Undo plus redo | 20 ms |

**Twin Analog is the costly one**, and its time is spread over plain
arithmetic with no single hot spot. Do not "optimise" it blind: any change
there is a change to the sound, and needs the owner's ears.

## Next, in the owner's order

1. The effects plan (`docs/plans/effects-plan.md`): sends, THROW, then the new effects.
2. LFO and random stepper per track.

Open: which chord packs ship, more presets, own WiFi hotspot, an automation
step-editor, a picture of QMOD's wiring. **Rethink Twin Analog** (owner,
2026-11-11): it has more features than it needs and is the costly synth; cut
it down rather than tune it. The engine stays at 48 kHz: 44.1 would save about
8%, which the owner judged not worth it.
