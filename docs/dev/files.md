# Files and compatibility

Song files, presets, ids, the built-in data, JAX-EP's bank, the pinned old five, effect families and TYPE.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

### Files and compatibility
- **Nothing is released; old songs are owed nothing** (owner, 2026-11-15: "don't
  ever worry about old tracks. this is all in beta"). The rules below keep old
  files loading and old sounds to the bit; they still hold as the house way, but
  where one stands in the way of a better program or a cheaper one, change it,
  and say so. Never keep slow code, an instrument or a format for an old song's
  sake. (QMOD was replaced by FM-6 outright, 2026-11-15: its word read as FM-6, its code gone.)
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
- **settings.txt and volume.txt are written once what they would say has stood still a second**
  (gesture audit G43, 2026-11-25; `Session::keepSettings`, `kKeepRestMs`): every setter wrote at once,
  two fsyncs, nine times a second with RIGHT held on a SET row, and at an end of THEME for nothing. Now
  **never when the text is what the card has** (`settingsKept_`, `volumeKept_`: what was read, or
  written, or what stands for no file), a change back before the second is up writes nothing, and
  `Session::update` writes what is owed a second after the last change. **On the way out
  `keepSettingsNow`** (main.cpp, after `saveNow`, before the power-off mark; `~Session` too). Every way
  the program ends goes through there: SEL + START, the idle rule, SDL_QUIT (SDL turns TERM and INT into
  it), and on our firmware every way off sends TERM first (`S95apotheca`). **Not covered: a power cut
  (the power key held 10 s, a battery pulled), a crash, a KILL**, which lose the song's last two seconds
  too; and how the stock firmware's own power key ends the program is not known (TERM: covered).
  `app_test` `testSettingsWaitToStandStill`; the tests that read a file at once call `keepSettingsNow`.
- **A deleted song goes to `deleted/` under a name nothing there has** (song
  or rev: "ash well", else "ash well 2"), through `binSong`, which the
  Session and the WiFi page share. Renamed over, the first one's revs paired
  with the second song.
- **Every synth has `kSynthMacros` (8) macros.** Each synth header checks it.
- **The JX-3P clone is JAX-EP wherever a player reads** (owner, 2026-11-14):
  "a virtual analog synth modeled after a popular DCO synth". **No maker's or
  model's name** (Roland, JX-3P, PG-200, Juno) in a text of `app/`, on the two
  web pages, in README.md or in docs/manual.md; `ctest -R words` fails one, with its own plants.
  **It reads the engine's texts for those names too** (preset and folder names
  are there: `kBeside` in `tests/words_test.sh` lists `engine/`'s folders, the
  two copied-in synths under `third_party/` and `tools/`; `.c` and `.h` as
  well). **A new folder of engine sources goes in `kBeside`**, which plants a
  name in each and fails on one with nothing to read. No SELECT rule there:
  the engine draws nothing.
  Code, file names, tests and `docs/plans` keep `jx3p` / `Jx3p`. Its song-file
  word is `jaxep` and its presets are `NAME.jaxep` (`jx3p` was never written
  by anything released and is not read). The list to pick from says "virtual
  analog, after a popular DCO synth" with no count: the line holds about 42
  letters.
- **A JAX-EP knob reads as its dial, 0.0 to 10.0, wherever a player reads
  it**: `settingText` on the handheld, and on the designer page too
  (`SynthSpec::pageReadsAsDevice`: the page is given every reading of every
  setting; for the other synths only a setting of 40 values or fewer). A new
  knob of this synth needs nothing more.
- **MAIN says what is known of a folder's sounds** (`SynthSpec::folderNote`,
  JAX-EP's only): on PRESET's top row, after the word. SPEEDS GUESSED for a
  built-in folder, 1983 BOOKLET, SPEEDS GUESSED for one named "BANK ..."
  (BANK A and BANK B: every built-in sound today, so the short one has no
  folder; `SynthScreen::folderNoteAs` shows it for its test), nothing
  for the player's own. **The count beside it is the preset's place in its
  folder** (owner, 2026-11-15; `SynthScreen::placeInFolder`: B7 reads
  "7 / 16", the player's own count among themselves, every synth alike; a
  folder is the run of presets side by side under one name, as `stepFolder`
  steps them). The row keeps room for the longest count
  (`kCountLongest`), and where the three texts do not fit the size chosen the
  whole row is drawn at LARGE (the bank's line at XL). No maker's name in it.
- **JAX-EP's 32 presets are the booklet's, and the table is not edited by
  hand** (`kPresets`, `engine/src/jx3p.cpp`; batch 7): A1 to B16 in two
  folders, BANK A and BANK B, so a preset's number is the factory's and a
  folder jump is a bank jump. `tests/jx3p_nam.inc` holds the booklet as read
  (twice, and by the pointers' angles), in its own terms; `jx3p_presets_test`
  holds the table to it value for value. **Change the reading and the table
  together, with the scan in hand**, and keep each guess marked (PULSE or
  SQUARE on twelve, B14's METAL, the blanks). The start patch is not a preset
  (`Jx3p::startPatch()`: what a synth is made with, what the engine's tests
  play); a new track and an old `track twin` are on A1.
  - **The cutoff's range was set from the bank, and the VCA is straight (owner,
    2026-11-15).** `kVcfLowHz` 49.84 and
    `kVcfHighHz` 49840 (`jx3p/ir3109.hpp`; they were 15 and 15000: the same
    span times 3.32), held at `kVcfTopHz` 20 kHz as before, so **at key 60
    CUTOFF 8.7 to 10 is one sound**. **The VCA's gain is the dial, straight**
    (`vcaGain`, `jx3p/env.hpp`: LEVEL 5 is half of LEVEL 10), `kVcaUnityDial`
    100 and **`kVcaClipDial` the same: the clip starts at a full wave at LEVEL
    10, so no preset as written reaches it** (owner: "I don't think they should
    distort by default"). The synth's `kOutputGain` is 0.6 (it was 0.3), so the
    bank stayed as loud. It was 4 dB a dial step, 0 dB at LEVEL 7 and then for a
    day at 4.5 with the clip there: A8 and A16 (LEVEL 9.9, 10) were 21 and 18 dB
    into it. **Moving the 0 dB alone cannot do it**: with 4 dB a step, LEVEL 10
    is 22 dB over LEVEL 4.5, so clean and as loud they pass full scale nine
    times over (measured: at 70 with the gain made up, A8 six keys 6.9, still
    10 dB into the clip). The start patch is LEVEL 5.0 (a wave of 0.3 out,
    exactly the gain LEVEL 4.5 gave), the tests' plain patches 2.0, 3.2 and 5.0
    (were 2.5, 3.5, 4.5: the same gain within 0.15 dB). **A plain saw's corners
    overshoot to 1.31 of full**, so a player's own saw clips softly from LEVEL
    7.6; `testVca` holds that. `Jx3p::samplesClipped()` counts the samples the
    clip bent; `testEveryPatchPlays` holds it at 0 for the 32 as written.
    - **A3 Organ I sings the key played**: 0.4 cents from the grid at worst,
      keys 48 to 84, read over five seconds (`testNamsThree`). Its sine sits on
      DCO-1's second harmonic (16') and is held by it: the cutoff 40 cents off
      still sings the key, so the test also holds the cutoff's CV to the key
      within half a cent. Its level sways (DCO-2 is 27 cents flat in the patch
      and beats with the sine, twice a second at key 48): the patch, not a fault.
    - **No patch is all but silent** (`testEveryPatchPlays`, the chord's
      loudest tenth of a second; measured again 2026-11-16 after the
      resonance was mended): B15 Planet -34.0 dB, the quietest; B16 Jet
      -30.3; B4 Song Whistle -28.3; A11 Harpsichord -21.2; the middle of the
      32 -15.9; A1 String I -13.5 (PHAZED's first presets: -15.5 to -12.6);
      A13 Chime -4.6, the loudest (B7 Distortion Guitar -5.7). The low end
      given back moved the patches with RESONANCE up (A11 +7.4, B9 Filter
      Flow +5.3, B11 Sync Wah +4.8), the quieter singing those at 8 to 10
      down (B14 Pulsar -1.7, A16 Voice -1.6, A3 Organ I -1.5); the fifteen at
      RESONANCE 0 are as they were. (Before: Planet -36.1, A11 -28.6, the
      middle -16.6, B7 the loudest.)
    - **Six keys as written: nothing clips, A8 E. Piano I peaks 1.56 out of
      the synth** (1.01 after the track's usual level; it was 1.46), every
      other patch 1.31 at most (A13 Chime; it was 1.17). **Macros full:
      nothing reaches the clip either** (since 2026-11-16; twenty patches did
      while RESONANCE 10 sang loud from 8.7 up, B8 at 1.79); A8 1.54 is the
      highest. Nothing is not-a-number and everything ends.
      `kVcaClipCeiling` (1.5) is not changed. **`kBassKept` 0.7 put A8 into
      the clip** (13 samples): the share given back is held by that.
    - **A song from builds 0.504 to 0.514 loads as saved; a JAX-EP track at
      LEVEL 4 or under is within 2 dB of how it sounded, higher LEVELs
      quieter and clean (LEVEL 6 about 4 dB, LEVEL 8 about 10 dB before its
      old clip). From 0.502 or earlier: brighter, and about 12 dB louder**
      (said in the manual).
  - **Builds 0.485 to 0.493 shipped JAX-EP with one preset, "Start", as
    preset 1.** A song from them keeps its sound (all 34 settings are read
    after the preset) under String I's name (`save_test`
    `testASongFromTheOnePresetBuilds`). **A preset's place is in songs from
    now on: append, never put one in before another.**
  - **A test that listens to a new JAX-EP track makes it plain first** (MIX
    0, ATTACK 0, CHORUS off): A1's two saws beat, and a note's peak hangs on
    where in the beat it began (`design_test`).
- **`track twin` is the one word whose meaning changed** (owner, 2026-11-14;
  nothing was released): it reads as a JAX-EP track without its sound
  (`trackKindFromWord`'s `old`, `parseSong`'s `oldSound`: no `preset`,
  `macros`, `settings`, no automation on lanes 0 to 7; everything else kept).
  It is never written. **`track qmod` reads the same way, as FM-6** (owner,
  2026-11-15: QMOD replaced; `kOldQmodWord`): on FM-6's first preset, and a
  `mod` line on a macro is dropped too (`testAQmodSongOpensOnFm6`). An old
  `NAME.qmod` preset is left on the card, unlisted.
  `tests/twinsong_main.inc` is a song `main` wrote with
  one: **not to be remade**. An old `NAME.twin` preset is left on the card,
  unlisted. **Twin's code is out (batch 8b, 2026-11-15), and that reader is
  the one place the word lives**: `kOldTwinWord` and its comments, the tests
  of the old song and the old preset file, the manual's note on old files. There
  is no `SynthKind::Twin` (the kinds are memory only: Phazed, Sampler, Jx3p,
  Fm6). `ctest -R words` fails a text of `app/` that says TWIN ANALOG or
  OVERSAMPLING, with its own plants, **or BEST QUALITY: a bounce is the song
  as it plays** (the BOUNCE row said "at best quality" from Twin's x8 until
  the mend of 2026-11-15; no synth has a quality to turn up).
- **The timings' song is held by `ctest -R bench`** (`tests/bench_test.cpp`,
  through `sendBusiestSong`): what is on each track, bus and the master, that
  the two JAX-EP tracks are on A1 and the two FM-6 on E.PIANO 1
  (`kBusiestFm6Preset`; choosing either again changes nothing, to the bit;
  QMOD's two tracks until 2026-11-15), and that the real-time rows count the blocks and helpers they say.
  **Its 24 MODs too** (`sendBusiestMods`, batch 9: two a track, every kind of
  target, LEVEL on tracks 4 and 10, PAN on 5 and 11, every MOD 1's DEPTH lane
  written; each runs, and the 24 cut more chunks than the song alone).
  **It times nothing**; the figures are still run and read. `ctest -R demo`
  runs `render_demo` and reads its three lines. `bench`'s one-track rows have
  no test.
- **A preset holds `presetSettingCount(kind)` settings, a song
  `settingCount(kind)`.** They differ for JAX-EP: ids 32 and 33 (VELOCITY,
  BRILLIANCE) are the track's own, so `loadPreset` (built-in and the player's
  own) and `savePreset` stop at 32, and a new `TrackData` starts with them
  (`Jx3p::presetPatch(0)`). **Undo counts a song's** (`soundMark`,
  `restoreSounds`, `sendSynthPatch`: `app_test`
  `testUndoPutsBackTheTracksOwnTwo`, in the engine too). A number in a preset
  file is held as a double first, then to its setting's range (`readPresetFile`,
  `jx::loadPreset`), and a knob reads inside its dial whatever it is given. A new track value of any synth goes after its
  patch's ids and is kept out the same way.
- **A preset may hold the track's WHOLE sound** (2026-11-16, owner: "a great idea
  for recall"; `app/presets.hpp`, `docs/plans/presets-plan.md`, `tests/presets_test.cpp`):
  its three effects, two sends, GLIDE FROM and two MODs (`TrackSound`), **never the
  mixer's LEVEL and PAN** (the song's balance). Written and read in the song file's own
  lines by **the one writer and reader of them** (`soundText`, `readSoundLine` in
  song_file.cpp; `parseSong` and `putTrack` go through them too): never a second parser.
  A player's own file always says `whole` (SAVE: `presetFileText`); one from before has
  none and is plain. **Picked, a whole preset puts every part in, what it leaves out
  emptied; a plain one changes the synth only** (`Session::chooseSynthPreset`,
  `putPresetSound`, `putTrackSound`: only what differs as the file writes it, another
  kind through `setEffect`, nothing written into automation). **One exception: a plain
  preset picked while the track still holds just what a whole one put in** (nothing
  turned since: `PresetSoundMemory`, compared as text) **puts back what the track had
  before it**, so browsing past a whole preset leaves no effects behind; forgotten by
  any other change, SAVE, `applyProject`, `addTrack`, `setTrackKind`, moved by
  `removeTrack`. One step of undo takes a pick back (`soundMark` sees it all). **B with A held
  puts back the whole sound A went down on, this memory with it** (gesture audit G12,
  `Session::keepSound` / `putSoundBack`: drums-and-samples.md), so a plain preset picked after the
  trip does what it would have done without it. A new
  track starts plain on preset 1, whatever that one holds.
- **A preset is heard as it is stepped to, with the song stopped: a synth's, a kit, and the sampler's** (the
  sampler's since gesture audit G22, 2026-11-25: `SamplerScreen::stepPreset` was silent, so a preset had to be
  stepped to and then X'd). Past an end nothing goes on and nothing is heard; playing, the song is heard
  instead. The audit's other half (one save button, Y, on every preset stop) is NOT done: Y is CATCH on every
  sampler stop and PHYSIC's dice on its MAIN page, so it needs a free button (the owner's call).
  `app_test testSamplerPresetsOnDevice`.
- **The built-ins past the engine's own are DATA, compiled in** (`app/presets/NAME.inc`,
  one raw string a synth; `readPresetData`, read once, `builtInData`): `new FOLDER /
  NAME` with `base`, `set`, `settings`, `macros`, FM-6's `op N` (an operator's 21 settings a line)
  and the track-sound lines, and `preset
  N` to give one of the engine's own a sound. **Their places come after the engine's and
  before USER, and are in songs: APPEND, never insert, move or take out** (the engine
  never hears of them: `enginePresets` is what `SelectPreset` takes; past it the app
  sends every setting, as for the player's own). `builtInPresets` counts both; ask
  `listedPreset` for the values the app holds. Every line the data cannot read is a
  `problems` line, and `presets_test` fails on one; `presets_test --show` reads each as
  the device does. **The added PHYSIC and FM-6 presets are played through their whole sound**
  (`presets_test --heard`, `--heard-fm6`): loudness, peak, a tail that ends, and every MOD moving;
  FM-6's MODs must also be HEARD (the phrase with that MOD off differs by 1% or more: a MOD on BRIGHT
  where no modulator sounds, or on LFO where PMS is 0, moves and changes nothing). `fm6-plan.md` section 11. **Tests of the engine's tables (`app_test`, `design_test`,
  `textsize`) run with the data emptied** (`useBuiltInDataForTests`), so a designer
  adding presets breaks none of them; `presets_test` holds the shipped data.
- **The chord packs that ship are compiled in** (2026-11-17, `docs/plans/chordpacks-plan.md`):
  `assets/chords/*.json` are the source, edited by hand (schema 2, Transmutation's form);
  `tools/chord_packs.py` writes `app/chord_packs.inc` from them, and `ctest -R chord` fails a stale
  one. A file on the card with a shipped pack's name takes its place. A song keeps its pack by NAME
  and each chord whole, so chords may be added, moved or renamed; **a pack's name is kept**.
  `chord_test` (`testEveryShippedChordIsWhatItSays`) works every chord out again from its quality and
  root (`kQualities`: a new quality goes there), holds its notes to the scale (a borrowed chord says
  `borrowedFrom` and lists its notes outside in `outside`), spells its root as the scale does (a flat
  root needs a `legacyName`: Db9, never C#9), 1 to 6 voices, and fits every name to its 88-pixel
  tile on the strip (`testTheNamesFitTheStrip`).
- **FM-6's built-ins are five factory banks** (2026-11-16): ROM 1A, 1B, 2B, 3B, 4B,
  160 voices, a folder each, written by `tools/fm6_rom.py` into
  `engine/src/fm6/roms.hpp` (`kRom`, each with its folder). A bank is appended, never
  put before another. ROM 3B's three values past their range are held by the script
  (`HELD`); any other stops it. `fm6_pins.inc` pins all 160.
- **The FILTER type order is fixed** (`int(v * 5)`: low 12, low 24, band, DJ,
  high). It was chosen so old songs' three types still land right.
- **The five old effects are pinned to the bit** (FILTER, DRIVE, DELAY,
  REVERB, CRUSH): `effects_test` holds 40 renders and one whole song to
  checksums taken before they were moved. Old songs depend on them. Do not
  touch their arithmetic, not even to fix it; make a new effect instead. The
  checksums are remade (`effects_test --print`) only from code known to be right.
  **The whole song's sum was remade once (2026-11-15)**: its third synth went from
  Twin Analog to JAX-EP, and `--print` gave the same 40 renders before and after.
  It pins JAX-EP's sound too now: a change to that synth moves it, and is then
  proved by the 40 renders standing still.
- **An effect has a number, a word and a place, and they are three things.**
  The number (`EffectKind`) is in memory and commands: appended, never
  reordered. The `word` is in song files: never changed. The place in the
  list (`kList`) is only what the player sees: change it freely. Never use
  one for another (`effectAt` / `effectPlace` / `effectFromWord`).
- **A family is only how the list is shown** (owner, 2026-11-12: "TAPE should
  just be one of the delays"; 2026-11-13: seven families). The player picks
  from FILTER (FILTER, COMB, VOWEL, EQ, BAND, WAH), DRIVE (DRIVE, SATURATE, FOLD, FUZZ), CRUSH (CRUSH, RING, WEAR), MOD (CHORUS, FLANGER, ENSEMBLE, PHASER, TREMOLO, PITCH),
  DELAY (CLEAN, TAPE, PING, DUB, REVERSE, STUTTER), REVERB (ROOM, HALL, SPRING, PLATE, BLOOM, SHIMMER), LEVEL (COMP,
  DUCK, LIMITER, SNAP), in that order (`EffectFamily`; the old groups, DIRT and MOVING, are
  gone). Underneath each type is the `EffectKind` it always was, with its
  number and its word, and `info.name` is still its own ("TAPE": bench and
  the tests use it). Fifteen more types were planned (effects-plan.md, "Seven
  families"); each is a row. COMB, VOWEL, EQ, FOLD, FUZZ, RING, WEAR, LIMITER and SNAP are built (branch `newfx`); FLANGER, ENSEMBLE, PHASER and TREMOLO, DUB, REVERSE and STUTTER, BLOOM and SHIMMER (branch `newfx2`); WAH (33) and PITCH (34) on 2026-11-24 (owner: "Let's do auto-wah and pitch shifter"); BAND (35) the same day (owner: "set a band, move it freely and change the Q"), a new type so EQ's songs stay to the bit. The screen asks `effectFamily`,
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
  word, MOD's CHORUS); since RING every family does, so `effectFamilySays`
  (the first type's `says`) is drawn for none today.
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
  lanes are cleared. **Undo after a TYPE change puts back the old type, its
  settings and its lanes together** (mended 2026-11-14: effects are in undo
  now, next rule; it was wrong from 2026-11-13). Another FAMILY
  put in a slot (`Session::setEffect`) still clears all four.
