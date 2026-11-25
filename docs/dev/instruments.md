# The instruments

FM-6's pages, a new synth track, ROLL, PHYSIC, ACID, CLOUD, MORPH, Phazed.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **FM-6's DEVICE pages** (2026-11-15; `fm` in `app/synth_spec.cpp`): VOICE AND LFO, OPERATOR 1 to
  6, PITCH ENVELOPE. **An operator's page is all of it, its ON first**: 22 boxes, six rows.
  - **An operator's ON is a bit of OPERATORS ON** (engine id 145, bit 6 - n): on a page it is a
    setting of its own past the synth's (`kSettings + n - 1`, `SynthSpec::bitOf`). **Read and set any
    setting a page shows through `settingValue` / `settingWrite`** (synth_spec.hpp), never
    `values()[id]`: the screen, `Session::setSynthSetting` and the WiFi page (`designState` gives
    `shownSettingCount` values, the ONs as 0 / 1, so the page's script needed nothing) all do.
    OPERATORS ON itself is on no page.
  - **COARSE and FINE read with their operator** (`SynthSpec::readingIn`, through
    `settingReading`): a ratio, or under FIXED a pitch in Hz; FINE says where the two land. The WiFi
    page's list of readings is `settingText`'s, which reads COARSE alone as a ratio.
  - **Six rows stand `kTightGap` (3) apart** (`SynthScreen::settingGap`): at 6 they were 52 high and
    a value stood on its bar even at LARGE. `settingY` shares the leftover pixels between the gaps,
    so the last row of any page ends on `kAboveScope`. **At XL those boxes are drawn at LARGE**,
    names and values (`boxFontsFor`: the value would come within `kValueClear` of the bar);
    `textsize` holds every FM-6 setting at its least and most to it.
- **ADVANCED keeps its place** (gesture audit G13, 2026-11-25; `SynthScreen::placePage_` / `placeStop_`,
  `keepPlace`, `turnPage`; `app_test` `testAdvancedKeepsItsPlace`). Each track keeps the page and stop it
  was last on, **in memory only, never in the song** (as FX's `last_`): written by `update()` while an
  advanced page shows (so a track left by another track's coming, not by B, keeps it too) and by B on
  its way to MAIN. **Another track of the same synth picked while a page shows opens ITS place**
  (review of A4, 2026-11-25; `shownTrack_`, `followTrack`, first thing in `update()` and `onButton`):
  one screen serves every track of a synth and stayed on its page, so `keepPlace` wrote the leaving
  track's page and stop over the arriving track's own, and on CLOUD a track that had never opened the
  pages showed RECORDING, where R1 records. Now the leaving track's place is kept as last shown (a page
  turned since the last frame too), and the arriving one opens its own, or page 1 at `firstStop`. MAIN
  is every track's alike and stays. `goTo` (the tests' and stills') sets the place for the track up. **A on ADVANCED opens there**; a track never on the pages opens page 1 at
  `firstStop` (0, or 1 on CLOUD). **L1 / R1 keep the cursor's row and column** (`index / columns`, an
  envelope page's eight to a row and its HOLD AT / LAST STEP row as Phazed's `moveCursor` counts them):
  on FM-6 the same knob of the next operator is under it, one press each. Where the next page has no
  such place, the nearest: the row's last column (an envelope's sixth column on a page of four is the
  fourth), then the page's last stop. Before, A always opened page 1 at stop 0 and L1 / R1 put the
  cursor on stop 0 (Phazed's page 5, step 3 was 16 presses each visit). Per track by number, so a
  track deleted hands its place to the one after it: a cursor, clamped (`goTo`), never a sound.
- **B with A held on MAIN's FOLDER and PRESET, and on CLOUD's RECORDING, puts the sound back** as it
  was when A went down (gesture audit G12, 2026-11-25: `Session::keepSound` / `putSoundBack`, the whole
  rule in drums-and-samples.md): the preset, its settings and macros told to the engine as undo tells
  them (`sendSynthPatch`), the effects a whole preset brought, and the recording. Heard (`hear()`), and
  A is spent; **a stick still pushed then waits for the middle** (`stickWait_`, as a MOD let go: it was
  stepping presets, and moved the cursor off PRESET on the next frame, review of A4). B with A held on a
  macro or a page's setting still does nothing.
- **A new synth track tells the engine its first preset** (`Session::addTrack` calls
  `sendSynthPatch`). The engine makes a synth on its start patch, which is no preset: JAX-EP and
  FM-6 played it (a plain saw, one sine) under a MAIN that said A1 or BRASS 1 until a preset was
  picked or the song sent again. (QMOD made itself on its first preset, which hid it.)
- **ROLL is PHYSIC's alone** (`SynthSpec::roll`, null for the others; `Session::rollSynth`): Y on
  PHYSIC's MAIN, not on its pages and not with A held, and the WiFi page's ROLL A NEW SOUND (`roll N`,
  shown only for a spec with a roll: `"roll":true` in the page's spec). It went with QMOD on
  2026-11-15 and came back for PHYSIC (the plan's call 10). It rolls STRIKE, HARDNESS, POSITION,
  DECAY, MATERIAL and STRETCH by a hash of the seed and each one's place, never a generator walked,
  inside the MODEL (a string mostly plucked, a bar mostly struck, either breathed on, NOISE, one time
  in eight), and batch 10's PAIR (half the time), MOTOR (a quarter), BUZZ (a fifth) and BODY MIX;
  MODEL, BODY, RELEASE, the levels and the dice stay. A change of the sound like any other: one step of undo (`soundMark`). On a synth without
  a roll Y does nothing on DEVICE, and the page's `roll` changes nothing. The bar says `Y new sound`
  on MAIN off the macros only (on a macro, B and HOLD X fill it: `testPhysicsHintsFit`).
- **PHYSIC in the app** (2026-11-16, `docs/plans/physic-plan.md` section 15; `ps` in
  `app/synth_spec.cpp`, `app_test` `testPhysicOnATrack` and `testPhysicsHintsFit`, `design_test`
  `testPhysicFromThePage`, `textsize`, `scope_test`, `modapp_test`). `TrackKind::Physic` appended, its
  row LAST in `kInstruments` (word `physic`, presets `NAME.physic`), `TrackData::physic` a
  `PhysicPatch` of 26 (`physic::kSettingRoom`), 23 used since batch 10 (2026-11-16, section 16):
  `settingCount` and `presetSettingCount` are 23, and a song's `settings` line holds 23. A new track is
  on Marimba, preset 0.
  - **Its `loadPreset` puts preset 0's sound under a player's own first**, then the file's values
    (`ps::loadPreset`): a preset saved before a setting was appended plays that setting as the
    factory has it, not as the last preset left it. (Phazed's, JAX-EP's and FM-6's copy over the track.)
  - **Five pages, SOUND, RING, PLAY, DICE, BODY** (BODY: PAIR, MOTOR, BODY, BODY MIX, BUZZ), two rows
    at most, so nothing on them is held at LARGE.
    Every setting reads in its unit: DECAY and RELEASE as times (RING at 100), POSITION as a share of
    the way along or "1/n" on the one setting nearest that fraction, FINE and DICE TUNE in CENTS,
    LEVEL in DB, 0 as OFF on VELOCITY and the dice; PAIR in CENTS, MOTOR in HZ, BUZZ in DB under the
    note's loudest, BODY MIX a share. `readingIn` reads HARDNESS under MALLET as the
    mallet's contact at full velocity (8.0 MS to 0.30 MS), under NOISE as the burst's length (2.0 S to
    2 MS), and STRETCH on STRING as B (`B 0.0005`,
    under a ten-thousandth `B 8.0E-05`); the WiFi page's readings are `settingText`'s, every one of them
    (`pageReadsAsDevice`), STRETCH as on a bar.
  - **OCTAVE (macro 7) is a choice** (`macroIsChoice`): no MOD on it, no HOLD X on its box. The other
    seven take one.
- **Phazed's DETUNE keeps a twin at its middle while notes sound** (MOD
  batch 0, 2026-11-14; `keepTwin`, `lineBack` in `engine/src/phazed.cpp`). Off
  the middle it makes a one-line sound a twin, and the CZ fades every voice
  for a change of line layout: a note held while DETUNE passed 0.5 was cut
  (RMS 0.087 to 0.008). A twin made by DETUNE stays one at 0.5 while any voice
  sounds and goes back to one line before the next note into silence.
  Moving OFF the middle with a one-line note held still cuts it, once.
  `phazed_test` (`testDetuneThroughTheMiddleKeepsTheNote`).
  **A note that begins on the kept twin is trimmed 3 dB more**
  (`kDoubleInTuneTrim`, `trimInTune`: its lines start in tune and in phase; a
  legato part never falls silent and stayed 3 dB up), from that note's start,
  given back when DETUNE leaves the middle. Never taken as DETUNE merely
  reaches the middle: a held note fell to 0.61 of itself. 0 turns it off (the
  owner's call: a twin's short notes just off the middle are 1.2 to 3 dB up
  too, as on `main`).
- **PHYSIC** (`engine/include/apotheca/physic.hpp`, `physic_rule.hpp`, `engine/src/physic/`;
  `docs/plans/physic-plan.md`, section 14 says what was built and measured; `tests/physic_test.cpp`).
  Nobody has heard it.
  - **Every mode snaps to exact zeros on its own tick** (under 1e-15), not only the whole voice: toward
    WOOD the high modes die long before the voice, and sat in numbers too small to hold (15 to 150
    times the cost with flush-to-zero off). A voice sleeps after 64 samples under -120 dB, a string's
    whole line too. `testTooSmall` scans every state over a 60 s tail.
  - **`#pragma clang fp contract(off)` in `physic.cpp` and its test**: with it the handheld plays the
    desk's bits (its song `0x45a36ca1ca8d0d2a` on both, under qemu); without it clang joins a multiply
    and an add on ARM.
  - **A strike waits for the top of `render()`** (a queue of 16): what arrives before one render is one
    moment. A GLIDE takes a ringing voice over only when it is the one note of its moment naming that
    `from`, the voice is over -40 dB and past its strike; a chord under TOP strikes every key.
  - **`Instrument::noteSeed` and `Instrument::stopNotes`** are PHYSIC's (the others' do nothing / are
    `allNotesOff`). The seed is `strikeSeed(track, key, the strike's own moment)`: a ratchet's piece
    `at + k apart`, an arpeggio's note `arp.at + m gap` (`startNote`'s `strikeAt`, used for the seed
    alone). Stop calls `stopNotes` (RELEASE damps, RING rings on, so a bounce keeps the tail); Play and
    panic `allNotesOff` (30 ms).
  - **`setMacro` stores and returns**; a voice works its numbers out on its own 16-sample tick, counted
    from its strike, and its glide moves there (`Slide::take(16)`), so a bounce is live play to the bit.
  - **Not in the pinned song** (`effects_test`): its own song and checksum in `physic_test`, remade only
    by the pinned song's rule. **The 24 version 1 presets are pinned too** (`testVersionOne`: a phrase of
    re-strikes, take-overs, steals, macros, Stop and sleep, one checksum each, taken before batch 10).
  - **Batch 10 (2026-11-16, plan section 16): PAIR, MOTOR, BODY, BODY MIX, BUZZ (ids 18 to 22) and STRIKE
    NOISE (2).** Each 0 is the version 1 path, to the bit: the voice's sample loop is built twice (the
    old one when MOTOR and BUZZ are off), a pair's twins are made only when PAIR is on, BODY's bank is
    not run at NONE, TUBES or MIX 0. **Keep that**: a new setting at 0 must not touch the old arithmetic.
    - **PAIR is the object's make, read at the strike** (as MODEL): a re-strike with another PAIR makes
      the voice again. A string's second line is a slot's own (`str2`, the lines block twice over); a
      glide retunes the two strings by turns, a tick each (`which`), so two cost about one there.
    - **MOTOR and BUZZ are worked out when the setting is turned** (`motorHzNow`, `buzzGapNow`), never on
      a tick: `pow` there cost a tenth of a percent. MOTOR's level is `1 - D sin^2(pi t)` from the
      polynomial `sinHalfTurn`, t from the strike, a sample at a time between its tick's two values.
    - **BODY is the track's, a sample at a time** (`physic/body.hpp`), states snapped under 1e-15 on
      every sample. **A block it skips (silent in, silent state) still slides its mix a sample at a
      time**: skipped, the slide fell a sample behind in blocks of 1 (`testBlocks` found it).
    - **NOISE and BUZZ are hashes of the seed and the sample since the strike** (`pluckNoise`), never a
      generator walked. A NOISE note's level is what it is LIKELY to ring at (a random walk's size).
- **ACID** (2026-11-16, the engine only: batches 0 to 2 of `docs/plans/acid-plan.md`;
  `engine/include/apotheca/acid.hpp`, `acid_rule.hpp`, `engine/src/acid.cpp`, `engine/src/acid/ladder.hpp`;
  `tests/acid_test.cpp`). `SynthKind::Acid` appended. Nobody has heard it, not run on the handheld.
  **Every figure is SELF** (no source was opened). The app: next block.
  - **One voice, the newest key wins**; letting it go never goes back to a key still held. `allNotesOff`
    (Play, Stop, panic: `stopNotes` is the default) hands the note to a tail slot (3 ms fade, counted
    in samples; a second slot when the first still fades, so a voice is never cut dead: review 2026-11-23) and the voice is fresh: Play from the top is the same line (`testPlayFromTheTop`).
  - **ACCENT is velocity 0.95 or over** (`kAccentAt`), **SLIDE is the note's GLIDE**, through
    `noteOnFrom` as the engine already calls it; nothing new on a note. **ACCENT 0 (setting or macro)
    is the plain path to the bit** (`accented()` is false there: `testAccentZero`).
  - **The tie** (`slides()`, `tieSeconds`): a GLIDE note with the gate open, or shut under 15/16 of a
    step ago (`kTieSteps`; never under 5 ms, `kTieMs`, which is all a synth played by hand gets) starts no
    envelope and feeds no new sweep: a note placed shorter than its step (0.9, 0.5) still ties to the
    GLIDE on the next step, as the original holds the gate into a slide; an empty step between starts
    again (and slides in pitch). Both orders on one sample tie (`testTie`). **The step comes from the
    engine**: `Instrument::noteStep(seconds)` before every note the step clock starts (a no-op for the
    others). The voice stays awake through the window (a step of silence, running) before it sleeps.
  - **A GLIDE to the same key is a TIE, one long note** (coordinator for the owner, 2026-11-16):
    `Instrument::tiesSameKey()` (ACID's only) has `startNote` call `noteOnFrom(note, ..., note, ...)`
    where every other synth still gets a plain `noteOn`: `effects_test --print` identical. A new synth
    that wants it says so; nothing else changes.
  - **The slide lands exactly**: an RC of three time constants bent so its last sample is on the key
    (`slideLeft`; the voice steps `slideR` a sample and counts `slideN` down). `testSlideTime` holds
    every sample it reads to the rule within 1e-6 cents.
  - **The filter is a diode ladder solved per sample, its CUTOFF the pitch it sings at.** Four coupled
    trapezoidal nodes (stage 1's capacitor half: SELF), a high-pass in the loop (`kLoopHighPass`),
    the inverse of the 4x4 system worked out on the tick, so a sample is four sums side by side and one
    division. The pitch where the loop turns half a cycle and the feedback there (`ladderEdge`: 1.531
    of the stages' corner, 24.47) are found once on the analog model; g = tan(pi f / rate) / w180.
    **Coupled stages lose 28 dB round the loop**, so the edge is at feedback 24 (a buffered ladder's 4)
    and the pass band at the edge is 26 dB down: `kMakeUp` (output x (1 + 0.33 k)) gives most back.
  - **The bend is read TWO samples ahead** (the JX-3P's read-ahead applied twice: exact for a steady
    part plus a sine at the singing pitch), so its division waits on nothing of the sample before. Read
    from the latest sample (the JX-3P's way) the division sat on the chain each sample waits on: 0.21%
    of a core against 0.15%. It sings within 0.6 cent of CUTOFF, 80 Hz to 10 kHz, at one level.
  - **The cutoff is worked out every 16 samples counted from the strike** (`kTuneTick`, PHYSIC's
    tick; a strike works it out on its own first sample): against every sample, the worst line (ENV MOD,
    ACCENT and SWEEP full) differs 50 dB under the sound (`testTuneTick` holds -40). At 8 it cost 0.16%.
  - **It sleeps once the gate has been shut for the tie's window** (a step, or 5 ms with no step
    known), the gate's 3 ms ramp at 0 and the accent sweep under `kSweepQuiet`, and wakes as a fresh voice
    to the bit (`testSilence`). Its oscillator and filter run, silent, until then, so a slide in the
    window is seamless.
  - **`setMacro` and `setSetting` store and return**; the settings are worked out at the top of the
    next render (or a note's start). `#pragma clang fp contract(off)`, as PHYSIC.
- **ACID in the app** (batch 3, 2026-11-16; `ac` in `app/synth_spec.cpp`, `app_test` `testAcidOnATrack`,
  `testAcidsHintsFit`, `testAcidsAccentMark`, `testAnAcidLine`; `app_test --acid` runs them).
  `TrackKind::Acid` appended, its row LAST in `kInstruments` (word `acid`, presets `NAME.acid`, "mono
  bass, after a classic bass line box": the words test fails "TB-303" and "tb303" too), `TrackData::acid`
  an `AcidPatch` of 20, 14 used. A new track is on Classic Saw. MAIN, SOUND (WAVE, OCTAVE, TUNE, LEVEL,
  VELOCITY, SLIDE) and FILTER (eight). Its own preset over preset 0's sound first (as PHYSIC's). **No
  macro is a choice** (`macroIsChoice` has no ACID row), **no roll** (Y does nothing). Its built-in data
  is `app/presets/acid.inc`, empty until batch 4.
  - **The roll marks an accent** (a note at 0.95 or over, on an ACID track only): a brass corner of 6
    (4 on narrow keys) at the block's top right, left of a ratchet's purple edge (`accentMark`,
    `note_marks.hpp`). `testAcidsAccentMark` reads it off the drawn screen in BROWN and PAPER.
  - **Writing a line** (`testAnAcidLine`): an accent is one press more (RIGHT while A is held to
    place), a slide three (hold X, UP twice); a 16-step line with 4 of each was 165 presses, 133 of
    them the D-pad walking the keys.
  - **Seven instruments do not fit the list at its pitch with the words under it**: the list stands as
    high as it must (`listTop`, now for adding a track too), and only with the words its rows close
    from 52 to 47 (`SongScreen::listPitch`).
- **CLOUD** (2026-11-16, the engine only: batches 0 and 1 of `docs/plans/granular-plan.md`;
  `engine/include/apotheca/cloud.hpp`, `cloud_rule.hpp`, `engine/src/cloud.cpp`; `tests/cloud_test.cpp`).
  `SynthKind::Cloud` appended. **Every figure is SELF. Nobody has heard it, not run on the handheld; no
  app side yet** (batch 2). The plan's calls are at their picks but one: **the pool is 24, not 32**.
  - **Every onset is a whole number of samples counted from the key's strike, every setting is read at
    an onset, and a grain once started never changes** (`onsetWait`, `grainUnit(seed, n, what)`: the
    strike's `noteSeed` and the grain's number, skipped ones counted). That is all the block and helper
    rule needs: `testBlocks`, `testThroughTheEngine` (1 to 333 frames, 0 to 3 helpers, Play again).
  - **A render is worked in pieces of 64**: every key's envelope first (it does not hang on the grains,
    and says where a key ends), then the grains from one onset to the next, each added into ITS KEY's
    sums in the pool's order, then the keys through their envelopes in key order. **A pool slot is free
    from the very sample its grain ends**, and a key's grains are cut to the sample the key ends
    (`dies`): freed at a piece's end instead, which slot the next grain took would hang on the block
    length (`testBlocks`' full pool with keys ending). Anything new that frees a grain frees it on a
    sample, never "at the end of this call".
  - **A grain's place is 32.32 fixed point** (`kWhole`): one integer add a sample, whole samples
    exact, the same in any blocks. At the recording's own pitch (ratio exactly 1: no PITCH SPRAY, TUNE
    or glide) the place is rounded and whole samples are read (`plain`); under 20 ms a straight read
    (`kLinearUnder`, the plan's third cut); else 4-point Hermite (`hermite`, within 2e-6 of a true sine,
    `testTheRead`). Reads go in stretches with no end of the recording in them (`clearRun`); the sample
    by an end reads round it. **The recording has no edges** (`wrapPlace`, and REVERSE reads the same
    stretch from its far end).
  - **The window is `windowPoly`**, the half cosine as a polynomial (sin to its y^9 term, held to 0..1:
    exactly 0 and 1 at its ends, 2e-6 from the cosine), worked out over a run first, so the compiler
    takes it four at a time. A 257-point table and a cosine stepped along were both tried: dearer (each
    sample waited on the one before). Each piece at least 0.5 ms (`windowFloor`).
  - **The gain is 1 / sqrt(DENSITY x SIZE, held to 1..12)** (`overlapGain`: a key sounds 12 at most),
    so the level holds within 0.8 dB over DENSITY x SIZE (`testCloudLevel`); the sides equal power.
  - **Keys: 6 sound, 12 slots** (6 more to fade out over 5 ms: a seventh key takes the oldest let go,
    else the oldest; `allNotesOff`, Play and panic, fades them all; `stopNotes`, Stop, lets them go so
    the grains ring on under RELEASE). The same key struck again is its own slot (its grains go on,
    SCAN and the count start again). A key played by hand rolls `liveSeed` of a count `allNotesOff`
    puts back, so Play from the top is the same cloud.
  - **A new recording ends every grain at once** (`setSample`): the old one goes to the trash and is
    freed next frame. ASan holds it (`testSwap`: the old one written over with not-a-number and freed;
    that mutant reads past the new, shorter one). The engine hands it over as the sampler's
    (`Engine::setTrackSample`, unchanged). No recording, or a length of 0: **the TONE** (`Cloud::tone`,
    2 s about middle C, every partial whole cycles round; made once per rate in `prepare`, shared, as
    `DrumKit::builtIn`).
  - **The picture**: `Instrument::grainPlaces` (CLOUD's alone, -1 for the rest), read after each chunk
    on the thread that rendered the track into `Track::grainAt` / `grainCount` (atomics, as the
    meters); the app asks `Engine::grainPlaces(track, places, most)`. `kGrainsShown` is `cloud::kPool`
    (a `static_assert`).
  - `setMacro` and `setSetting` store and return; `selectPreset` puts the macros at 0.5. Three engine
    presets (Soft Cloud, Dust, Fifths Choir); the plan's 24 are app data (batch 3).
- **CLOUD in the app** (batch 2, 2026-11-17; `cl` in `app/synth_spec.cpp`, `app_test` `testCloudOnATrack`
  and `testCloudsHintsFit`, `app_test --cloud`). `TrackKind::Cloud` appended, its row LAST (word `cloud`,
  presets `NAME.cloud`, short name CLD, "clouds of grains from your recordings"), `TrackData::cloud` a
  `CloudPatch` of 24, 17 used. A new track is on Soft Cloud with no recording: the TONE. MAIN, SOURCE,
  GRAINS, PLAY; no macro a choice; **ROLL on Y** (`cl::roll`: SPRAY, SIZE, DENSITY, SCATTER, SHAPE, PITCH
  SPRAY, PITCH STEPS, REVERSE, STEREO; never the recording, POSITION, SCAN, ROOT, TUNE, the envelope or
  the levels). Its own preset over preset 0's sound first. **Nobody has heard it.**
  - **Its recording is the sampler's `TrackData::sample`, through `holdsRecording(kind)`** (project.hpp):
    `sampleUses`, `deleteSample`, `recordingChanged`, `findMissing`, CATCH, RECORD, `setTrackSample`,
    `applyProject`'s `loadTrackSample`, the song file's `sound NAME` (written before `preset` on a CLOUD
    track) and the rename that follows saved songs. What means the sampler's own settings still asks for
    the sampler. A track given the other of the two keeps the recording.
  - **`SynthSpec::recording`** (CLOUD's alone) gives the generic screen two things: **MAIN's picture**
    (`SynthScreen::drawPicture`, between PRESET and the macros, `kPictureY`/`kPictureH` 80, the macros
    `kPicMacroH` 69 under it): the recording's outline (the TONE's, dim), lit within SPRAY of POSITION
    (macros on), POSITION a brass line, each sounding grain a teal tick top and bottom from
    `Engine::grainPlaces`, the name in the fixed lettering over it; and **RECORDING as the first stop of
    SOURCE** (`kRecordingId`, not a setting: `item()` adds it): a tap of A the list of recordings (the
    sampler's `SamplePicker`, its preview the cloud at middle C), hold A + up / down the next, R1 there
    RECORD (elsewhere R1 is the page), Y anywhere on SOURCE CATCH; what is said shows in the top bar.
    **Neither a first visit nor a page turn lands on RECORDING** (gesture audit G13, 2026-11-25:
    `SynthScreen::firstStop`): "A, R1", page 2 on every other synth, started a recording here, and so
    did R1 once the pages came round to SOURCE. Both land on POSITION; a place remembered on RECORDING
    (the player was there last) opens on it. **While a take waits or runs, R1 on any page and stop stops
    it** (G34), before SAVE on MAIN's PRESET and before a page turn, **but after a held MOD's buttons**
    (review of A4, 2026-11-25): with X held on a macro R1 is the MOD's PHASE / LENGTH / SPEED, and the
    stop came first and took it (the line is not even refreshed while the MOD holds the screen), as the
    kit's EDIT and VOICE keep their own R1. `sample_test` `testR1StopsATakeFromAnyStop`.
  - **The grains are a readout for the redraw** (`Readouts::grains`, `kStirGrains`: the track up's places,
    `App::readouts`); `idle_test` has a part (a CLOUD at LEVEL 0, so SIGNAL stands still and only the
    grains move).
  - **Eight instruments close the list up**: 46 adding a track, 41 with the words under it, each box a
    pixel short of the pitch (`SongScreen::rowH`, its two lines placed within).
  - **24 presets, four folders of six** (batch 3, 2026-11-17): STILL is the engine's table (Soft Cloud
    first), DRIFT, DUST and CHOIR are `app/presets/cloud.inc` (a folder the data adds must not share an
    engine folder's name, or the folder step finds two runs). Six carry a MOD, eight an effect or a send.
    `presets_test --heard-cloud` (ctest `presets_heard_cloud`) plays each through the TONE and through a
    recording it makes (a rising sine and a noise burst): -30 to -8 dB loudest, peak under 0.9, a tail
    that ends in 30 s, the presets on one source within 14 dB of each other, every MOD moving and heard
    on the recording. **A player's own CLOUD preset keeps its recording** (`UserPreset::recording`, a
    `sound NAME` line after `whole` in the file): picked, `Session::putPresetRecording` puts it in by its
    name, else by its base name where exactly one has it, else keeps the track's and says RECORDING NOT
    FOUND (`Session::say`). A built-in never names one. The recording a pick changes is in the pick's
    step of undo (as the sampler's: since gesture audit G04, 2026-11-25, songs-and-undo.md).
  - **The WiFi page's synth page draws its recording** (batch 4, 2026-11-17): a spec with
    `SynthSpec::recording` adds `waves` (the recording's peaks, or the TONE's: `cloudToneOutline()`, which
    MAIN's picture shares), `band` (POSITION and SPRAY: the lit part and the brass mark; a click or a drag
    sets POSITION) and `recordings` (read on the app thread while the page looks). `sample NAME` and ROLL
    as before; the list's first choice is THE BUILT-IN TONE. `design_test` `testCloudFromThePage`;
    `APOTHECA_DESIGN_CLOUD=1` holds the page for a browser. Tried in Chromium with Playwright, 2026-11-17.
  - **Every click of a setting reads another way** (batch 5's review): the readings carry as many
    figures as it takes (SPRAY 1 +-0.005%, SIZE 5.0 MS, DENSITY 1.00 HZ); `testCloudOnATrack` walks
    every value of every setting. **A CLOUD (or a sampler) given an instrument with no recording lets its
    recording go** (`setTrackKind`); the two pass theirs to each other. **`App::typingAName` (it was
    `typingAFolder`) knows CLOUD's list too**: a new list of recordings goes in it, or L2 / R2 turn the tab under the keys.
  - Stills `cloud`, `cloud-1` to `cloud-3`, `cloud-long`, `cloud-sounding` (a key held: the still is
    taken before the engine renders, so no ticks show there; `testCloudsHintsFit` reads them off a
    drawn screen).
- **MORPH** (2026-11-17, the engine only: batches 0 and 1 of `docs/plans/wavetable-plan.md`;
  `engine/include/apotheca/morph.hpp`, `morph_rule.hpp`, `engine/src/morph.cpp`, `engine/src/morph/tables.*`;
  `tests/morph_test.cpp`). `SynthKind::Morph` appended. **Every figure is SELF. Nobody has heard it, not
  run on the handheld.** The owner's 17 calls are at the plan's picks.
  - **A table is 32 frames kept at 18 sizes, one a half octave**; a stream reads the first size whose top
    harmonic is at or under rate - 20 kHz, so a size change drops nothing under 19.85 kHz and needs no
    crossfade. **32 points a harmonic (capped at 2048)**: at 8 a straight-line read aliased -39 dB on
    FOLD at key 96; at 32, -64 dB at worst. 80 KB a frame, **41 MB for the 16 built-ins**, made once
    in `prepare` (130 to 200 ms on the desk), shared, never freed.
  - **The tables use no maths library** (`turnSin` / `turnCos` from the house polynomials, `exp2Poly`;
    floor, fabs and sqrt only, which are exact), so the handheld makes the desk's tables. **The handheld
    plays the desk's bits** (batch 5): `morph_test --sums` (every table's bits, the busy song, a phrase of
    each preset, the engine's song) is the same built with the handheld's flags under qemu-aarch64, and
    all of `morph_test` passes there. **`#pragma clang fp contract(off)` stands ABOVE every `#include`**
    in `morph.cpp` and `morph/tables.cpp`: below them, `jx3p/ir3109.hpp`'s polynomials were fused into
    multiply-adds on ARM and nine tables and every sound came out other.
  - **A key works on a 16-sample tick counted from its strike**; every setting the patch and macros give
    slides in over 512 samples counted from when it changed (a MOD on POSITION is a line), at once when
    no key sounds. The MOD ENV steps once a tick (its shape made at rate / 16). TABLE A, TABLE B and
    UNISON are read at the strike.
  - **A pool of 24 streams** (a stream is one oscillator's read; UNISON x (B on ? 2 : 1) a key): a key
    that finds too few steals the oldest let-go key, then the oldest, until it fits. The cost's ceiling.
  - **The only roll is a unison stream's start** (`streamPhase`: the strike's seed); one stream alone
    starts at 0. The readout is CLOUD's `grainPlaces` path: each key's place in A, newest first, then B's.
  - Cost (`bench`, a quiet desk): 6 keys 0.47%, the pool full 0.67%, asleep 0; 53 M / 87 M instructions
    a second (about 5% / 9% of a handheld core by the speed plan's rule, not run there). Batch 5,
    `morph_test --measure cost`, a busy desk: 0.45% / 0.62 to 0.66%.
  - **A key X holds is struck again when TABLE A, TABLE B or UNISON changes** (`Session::setSynthSetting`:
    `audition` on the held key; the plan's call 13); any other setting changes the key as it sounds.
    `Morph::strikes()` counts strikes for the test.
- **MORPH in the app** (batch 2, 2026-11-17; `mo` in `app/synth_spec.cpp`, `app_test` `testMorphOnATrack`
  and `testMorphsHintsAndPicture`, `app_test --morph`). `TrackKind::Morph` appended, its row LAST (word `morph`,
  presets `NAME.morph`, short name MRPH, "wavetables, swept from wave to wave"), `TrackData::morph` a
  `MorphPatch` of 40, 29 used. A new track is on Analog Sweep. MAIN and five pages, OSC, VOICE, FILTER, AMP,
  ENV, two rows of four at most; every click of every setting reads another way (the test walks them);
  **UNISON reads with the keys the pool leaves** (`mo::readingIn`: "4, 3 KEYS" with B on). No macro a
  choice; **ROLL on Y** (`mo::roll`: OCTAVE, TUNE, the AMP envelope, KEY TRACK, VELOCITY and LEVEL stay).
  **32 presets in five folders** (batch 3): BASIC is the engine's table, PADS, LEADS, BASS and MOVING
  are `app/presets/morph.inc`; `presets_test --heard-morph` (ctest `presets_heard_morph`) plays each
  through its whole sound (FM-6's player: keys, or the bass line for BASS), every MOD heard, the 32
  within 8 dB of each other. **Nobody has heard it.**
  - **`SynthSpec::table`** (MORPH's alone) gives MAIN the picture's room (as CLOUD's recording does) and
    `SynthScreen::drawTablePicture`: A's wave teal, B's purple behind it, a strip of frames with POSITION A
    brass, and while a key sounds the wave the newest key READS (the MOD ENV, a MOD) with a teal tick: the
    readout is CLOUD's (`Engine::grainPlaces`, `kStirGrains`), MORPH's places there. `idle_test` has a part.
  - **The WiFi page draws A's and B's tables** (batch 4): the spec carries all sixteen once
    (`morphTablesText`: every frame at 64 points, about 110 KB) and `"oscs"`, so a turned TABLE or
    POSITION is in the state only and never sends a new spec; a click or drag sets POSITION
    (`tableWave` in `design_page.cpp`, tried in Chromium). `design_test` `testMorphFromThePage`.
  - **The tables are made on a thread of their own as the program starts** (`main.cpp`: `Morph::makeTables`,
    joined at the end); a MORPH track added, or MAIN drawn, before it is done waits on the same lock.
  - **Nine instruments close the list up to 41 adding a track and 36 with the words under it** (each box a
    pixel short): **a box under 44 draws its name in the medium lettering and both lines by their own
    heights in its middle** (`SongScreen::kListLineGap`, `kListTailRoom`), held at LARGE's where the size chosen does not
    fit (at 35 the large name ran into the line under it at every size). `app_test` holds the heights.
  - Stills `morph`, `morph-b`, `morph-1` to `morph-5`, `morph-keys`, `morph-long`, `morph-sounding`.
- **FM-6's `setMacro` returns early on the value it has**, as Phazed's
  does (JAX-EP's does not; `bench` times it: 0.035 us a change, 0.002 the same value;
  `Fm6::applied()` counts them for `fm6_test`).
- **Phazed's glide is the CZ's portamento given a rate**
  (`porta_rate16`, `third_party/CHANGES.md`): it moves on the CZ's 122 Hz
  tick. A note waiting for its slot keeps the rate and the key it was played
  with.
- **Phazed rests in silence, to the bit** (speed plan batch 1;
  `cz_synth_resting` / `cz_synth_rest`, `RateConverter::silent` / `skip`):
  idle, its output settled, its chorus fed silence longer than it remembers
  and the converter's history zeros, a block is zeros and only the CZ's counts
  and its volume's glide move. **Anything new the CZ moves in a silent frame
  goes in `cz_synth_rest` too**, or a woken Phazed is not the one that never
  slept; `speed_test` (`testPhazedRests`) holds the two side by side.
  Its filter tables and the converter's are made once and shared
  (`cz_synth_init_shared`, `RateConverter::tableFor`): never written after.
