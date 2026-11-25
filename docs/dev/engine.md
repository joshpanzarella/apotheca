# The engine and its threads

The audio thread's rules, the queue, helpers, buses, THROW, tails, MODs on the engine side, the JAX-EP's insides, effects' house rules.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

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
  held.** Setting the stop flag without it hung the quit. The helpers take
  the tracks from `taken_`, in track order, or heaviest first by last chunk's
  times (`setHeaviestFirst`, off: only the SPEED TEST's rows ask for it, speed
  plan batch 0); the mix always walks `active_`. `speed_test` holds both to
  the bit.
- **A helper takes a track by ONE atomic that holds the chunk's count and the next place** (`claims_`:
  count << 16 | place, 0 shut). Read apart, a helper that took a place past one chunk's end and then read
  the next chunk's larger count rendered a track another thread was rendering (review 2026-11-23).
- **The built-in drum kit is built once per sample rate** and shared
  (`DrumKit::builtIn`). It never changes after that.
- **A track taken away while it sounds fades for 5 ms first** (`RemoveInstrument`,
  `Track::going`, `kGoneFadeSeconds`; owner, 2026-11-14): its instrument, its
  effects' tails and its thrown hits go down together, then `takeAway` does
  what the command always did. Silent in its last block (`kGoneQuiet`, -100 dB):
  at once, as before. **What was sent after waits for the fade** (`held_`,
  `takeCommands`), and comes at the chunk the fade ends on, in the order sent;
  other takings away fade beside it (a new song takes all twelve at once).
  `caughtUp()` counts the held command. A test that takes a sounding track away
  and wants the next thing at once renders 5 ms first (`engine_test`
  `testATrackTakenAwayFades` holds it beside a second engine, sample for sample).
- **Every cut of a chunk is made before anything is moved on by its length** (`render()`: the steps,
  waiting notes and hits, the MOD tick, the fade of a track taken away). The fade's cut came after the
  clocks had moved, and every step after it came up to 63 samples early (review 2026-11-23;
  `engine_test` `testATrackTakenAwayKeepsTheClock`).
- **A lane with nothing written in a scene goes back to the knob** (`Track::autoBase`, read in `fireStep`
  and by `settleAutomation` at Stop), not to the last value written. So a FILTER's CUTOFF automated in
  the last scene only opens up at the wrap when the knob is more open, which sounds like the filter
  left (owner's report, 2026-11-14). That is the design; whether an unwritten scene should HOLD the last
  value instead is the owner's call, not made. **Whatever changes a knob in the engine sets its rest
  too**: `SetMacro`, `SetEffectParam`, `SetSend`, and `SelectPreset`, which puts all eight macros' rest
  at the middle as the synth does (it left the old sound's knobs as the rest, and the song played the
  old sound's macros after every wrap into a scene with none written: `engine_test`
  `testAPresetPutsTheMacrosRestBack`). The app sends the macros after a built-in preset too
  (`chooseSynthPreset`), as `sendSong` does: the engine is told the project's knobs, never left to agree
  by itself (`app_test` `testABuiltInPresetRestsTheMacrosWhereTheyShow`).
- **A key an audition holds is the player's until its own NoteOff** (review of gesture audit G33,
  2026-11-25; drums-and-samples.md): `NoteOn` / `NoteOff` with `e = 1` set and clear `Track::auditioned`;
  `fireNoteOffs` passes such a key by, and an audition's NoteOff on a key the song still plays leaves it to
  the song's note-off. `letGoOfNotes` and a track taken away clear it. With no audition nothing differs.
- **A tempo change keeps every place in the step** (`SetTempo`): what is left
  of the step, an arpeggio's wait, notes waiting, notes' ends and drum hits
  waiting are all scaled by the same amount, so the arpeggio stays on the beat.
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
  - **A voice's plain stretches are runs** (speed plan batch 2; `plainRun`,
    drums.cpp): forward at speed 1, its level, rise, fade and THROW at rest,
    up to the sound's end, its part's end or the sample its cut falls on. A
    run is a plain loop with the same sum written the same way; every other
    sample goes the long way. **Anything new that moves in a voice from one
    sample to the next makes `plainRun` say 0 while it moves**, or the run
    skips it. `speed_test` (`testDrumRuns`) holds the two ways side by side
    (`setRunsForTests`). Twelve drum tracks 1.47% to 0.43% of a desk core.
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
    a last-step throw is kept by the bounce's own rule (seven seconds of
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
- **What reaches the master's slots is sound or silence too** (`renderChunk`, the same test as
  `feedBuses`), and `ceiling` gives silence for not-a-number: one such sample from any track stayed in an
  old DELAY or REVERB on the master for good (review 2026-11-23; `testNotANumberNeverStays`).
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
- **A one-pole that lands exactly must land further out than a float can
  step.** Heading for 1, a 1 ms smoothing at 48 kHz stops moving 1.4e-6 short
  (k times the gap is under half a float step there), so a landing at 1e-6
  never came and the JX-3P's voice was never freed. Its hold lands at 1e-5
  (`jx3p::kHoldLands`); a new smoother picks its gap the same way. **And it
  lands when a step no longer moves it** (`Hold::next`): a float's step grows
  with the value, so a fixed gap is right only near 1. The HPF's hold, over 4,
  stuck 1.1e-5 short and the HPF and chorus ran on silence for good.
- **A minimum-phase BLEP makes the jumps late (2.23 samples at the bottom), so
  every slope of the wave carries the same lateness** (`kBlampCarry` in
  `Dco::take`, the saw's too). A saw that did not had a mean of 4.47 x dt:
  37% of its peak at the top keys. **A test that takes the mean off cannot
  see an offset**: `testNoOffset` reads the mean itself.
- **A `Glide` always lands, at any block length** (never slower than `kLeast`
  a sample). A fraction of what is left falls below what a float can add in
  blocks of 8 frames and under, and `moving()` then stayed true for good.
- **A gain or a filter's numbers turned at once set off gently and slide
  sample by sample** (`fx/svf.hpp`, COMB, VOWEL, EQ): what glides is worked
  out on the TICK (every 16 samples counted from the effect's start, so the
  sound is the same to the bit however it is cut into blocks, moving or
  not), `Slide` reaches each tick's numbers sample by sample, and `Gentle`
  is two Glides one after the other. EQ's LOW turned from +12 to -12 dB at
  once made a corner 790 times a 100 Hz sine's (0.28, a click) stepped a tick
  at a time; slid, 34; with `Gentle`, 2.2. `holdsTheRules` in `effects_test`
  turns every setting of a new filter at once and holds the numbers.
- **A change of curve waits for the fade before it** (SATURATE's TYPE: one
  fade at a time, the last one asked for wins). Starting a second fade inside
  the first cut the fading curve off: a click.
- **A sleeping effect that wakes starts as a fresh one** (settings snap, the
  wow and the bend start again), so a render does not depend on how long ago
  the last sound was. **So one whose sound hangs on a clock that runs on
  (RING's carrier; WEAR's sway and its dice, rolled by the count of samples)
  sleeps only after `kRestSeconds` (4 s) of nothing.** It
  slept after a third of a millisecond of exact zeros at first: every drum hit
  met the carrier at its top, and a slow ring did nothing, unlike the same
  hits with sound too faint to hear between them. **One exception, kept (review 2026-11-14): a setting
  turned within a glide of waking.** The TICK counts from the effect's start,
  asleep too (`tickCountsAsleep` pins it), so its 16-sample grid falls where
  the silence's length put it, and such a glide moves on ticks up to 15
  samples apart from a fresh one's (WEAR 1e-4, SNAP 0.002, LIMITER 0.02 apart;
  RING, COMB and FUZZ alike). With settings at rest, exact.
- **A DRIVE type that evens out its loudness does it without its gate**
  (FUZZ: GATE only takes away), and **a fuzz is matched where it lands**, a
  sine at 0.18, not half scale: it brings everything to about one level, and
  matched at half scale a quiet note came out 17 dB over DRIVE. A tone's
  make-up is for the share squares A1 to A7 keep, averaged, not one pitch
  (made up for 220 Hz alone, a high note came out 14 dB louder).
- **DUCK is told the hits, it does not listen** (`Effect::drumHits`, bit 0 is
  lane 1). A hit always lands on a chunk's first sample (`render()` ends a
  chunk where the next is due), so the engine marks it where it triggers the
  drum (`Track::hitsNow`), and `renderChunk` copies the first drum track's
  marks to `chunkHits_` and clears them **before the tracks are shared out**;
  helpers only read it. Keep it that way: written during the chunk, a ducked
  track would depend on which helper got to the drums first. A new way to
  trigger a drum must set the mark too. Every trigger counts (ratchet pieces,
  hits by hand); a bus's slot is not told.
- **A lane's value goes out through `Engine::giveLane`, and only there**
  (MOD, `docs/plans/modulation-plan.md`; `engine/include/apotheca/mod_rule.hpp`
  is the rule, no SDL, `tests/mod_test.cpp`). Automation, Stop and
  the knob's own commands (`SetMacro`, `SetEffectParam`, `SetSend`) all set the
  lane's CENTRE (`autoNow`, `autoBase`) and call it; it gives
  `modGiven(centre, Track::modOffset[lane])`. **An offset of exactly 0 gives
  the centre as it is, in the same call as before** (not even clamped): that
  is why a song with no MOD is the same to the bit (`effects_test --print`
  identical, 2026-11-14). The centre is never the wobble: automation and Stop
  compare with it. A new way to set a macro, an effect setting or a send of a
  track goes through it, or a MOD on that lane is lost until its next tick.
  Only `runMods` sets an offset, and `stillLane` puts it back to 0.
- **A MOD runs on a tick, and only while one is on** (batch 1, 2026-11-14;
  `Engine::runMods`, `Track::mods`, `SetMod` / `SetModStep` / `ModAudition`).
  The tick is every `kModTick` (64) samples **counted from each step's first
  sample** (`sinceStep_`), the position read before the chunk is taken off
  `untilStep_`; a synth's macros every `kModMacroTicks`'th tick (512), effect
  settings and sends on each. `render()` cuts the chunk on the tick **only
  while playing with `anyMod_`, or stopped with an audition**: a song with none
  is cut as it always was, to the bit. `runMods` runs on the audio thread
  before the tracks are shared out; a lane is given only when its offset
  changed; two on one lane add. It passes by a slot held by the FX screen
  (`autoWaits`) and a setting that is a choice (`slotChoices`, told in
  `InstallEffect` by `Engine::setEffect`). **Stop, Panic, a MOD aimed
  elsewhere or OFF, the hold, an effect put in a slot and a track taken away
  let go** (`stillLane`: offset 0, the centre given); a track taken away has
  its MODs OFF. **Anything new that stops the song, or changes what a lane
  belongs to, lets go of the offset**, or the old wobble stays frozen on the
  knob. `ModAudition` (X held while stopped) runs a track's MODs from the
  moment X went down; it is not taken while playing. `mod_test` holds all of
  it; `mod_test --measure` prints the figures.
- **Under a MOD, an effect that glides a setting a CALL at a time is not the
  same to the bit in other blocks** (CHORUS, COMP, DUCK, HALL, PING, PLATE,
  SATURATE, SPRING, TAPE: `Glide::begin` in each `process()`). At most 0.0024
  apart (about 46 dB down), held under 0.005 by `mod_test`. The TICK effects,
  the old FILTER, the sends and the macros are exact, and helpers never change
  a bit. Not mended (it would change those nine's glide): the owner's call. A
  fast MOD on them is also mostly smoothed away (a 1/2 STEP SINE keeps 31 to
  43% of its swing; the plan's section 13 has every rate).
- **A song is the same to the bit in any blocks only at a tempo whose step is
  a whole number of samples** (120 BPM: 6000), MOD or none, as on `main`
  (review, 2026-11-14: it was blamed on Phazed). At 133 (5413.5) a note that
  ends or waits, a nudge, a ratchet or a glide can land a sample apart by how
  the sound is cut. A block-exact test says its tempo: `mod_test` runs blocks
  of 1 with Phazed at 120, and without it at 133.
- **A preset chosen puts the eight macro lanes' centre at the middle**
  (`Command::SelectPreset`, `kPresetMacro`): every synth's `selectPreset` puts
  its macros there, and the engine's centre must agree or a MOD moves about
  the old knob. A lane a MOD is adding to is given at once; with nothing
  added no macro is given. **A new `Instrument` keeps that promise**
  (`selectPreset`: macros to 0.5), or the engine's centre is wrong for it.
- **The JX-3P's `allNotesOff` hands every sounding voice to a tail**
  (`kTailVoices`, `engine/src/jx3p.cpp`): it rings out its release beside the
  six, which start again as from fresh, so Play from the top is the same
  performance after Stop however long the release (`jx3p_test`
  `testPlayFromTheTopWhileRinging`). Play and Stop let notes go, never cut
  them; a fresh voice is `State::Voice()`, so anything new a voice remembers
  is put back with it. Its steal takes a key let go before a key held
  (`kStealLetGoFirst`).
- **The JX-3P's VCF bends only its feedback** (`engine/src/jx3p/ir3109.hpp`):
  four trapezoidal one-poles solved as one sum, the feedback's soft clip (no
  flat top) taken as a gain **at where the feedback is going: read ahead from
  its last three samples as a steady part plus a sine at the cutoff**
  (`VcfCoef::turn`; the owner's report, 2026-11-15, "it just squeals").
  Taken from the sample before, the gain was a sample late where the filter
  sings: 30 cents sharp at 4 kHz, 3 dB louder at 10 kHz, and a bass note beat
  the sine into a buzz. **Keep the read-ahead**: a line from the last two, or
  the tangent at the last, were tried and are worse. RESONANCE is straight to
  the edge (4) at 9.5, then climbs to 5 at 10 (`kResEdgeDial`, `kResTop`):
  **straight to 4.21 left Organ I's sine so weak that DCO-2's harmonic, 27
  cents flat, held it**. Half the low end the feedback takes is given back
  outside the loop (`kBassKept` 0.5, two plain one-poles at a quarter of the
  cutoff), so the octave under the cutoff keeps its loss and Organ I's sine
  stays 13 dB over its 16' DCO. At RESONANCE 0 the bend's sum is not worked
  out (y4 is the stages' sum). A bend in each stage was tried and taken
  out: it moved the singing's pitch with its level (16 cents off over Organ
  I's three octaves; the plan holds 5). Its coefficient is worked out only on
  a sample its CV moved, from polynomials (`exp2Poly`, `sinPoly`, `cosPoly`:
  no `floor`, `ldexp`, `tan`, and **no division by a constant**, which the
  compiler does not turn into a multiply; together with the bend's division
  folded into the filter's own, taking them out brought the moving-cutoff
  bench row from 2.1% to 1.6%). The cutoff's hold lands 1e-3 of an octave
  from its step (`kVcfCvLands`): landing at the VCA's 1e-5 it slid on into
  the last part of the tick (`testTheCvSteps` fails), and the coefficient is
  worked out on every sample it slides.
- **The JX-3P's LFO is one for the synth, on a 10 ms tick counted from
  allNotesOff, and it runs whether anything sounds or not** (`engine/src/jx3p/lfo.hpp`).
  `Jx3p::render` cuts its block into pieces that end where the LFO ticks, so a
  voice's own tick reads it as of its sample however the sound is cut
  (`jx3p_lfo_test` `testBlocks`). Anything new the synth works out once for all
  voices on a tick goes in that loop, never once a block. A voice reads the LFO
  only on its tick; a key with no other key held starts the DELAY again, and the
  LFO reads 0 from that sample (`startDelay`). RANDOM is a hash of the count of
  cycles (`kLfoSeed`, pinned): never a generator walked.
- **The JX-3P's chorus sweeps from the synth's clock, and is switched at once
  before the first sample** (`engine/src/jx3p/chorus.hpp`). Its place is the
  samples since allNotesOff, counted whole, so it is the same however the
  sound is cut, Play from the top is the same sweep, and it can sleep (exact
  zeros, skipped) and wake as a fresh one. **It is not skipped while its fade
  is under way**, or a switch in silence would wait for the next sound. Until
  the synth's first sample after it is made, set up or allNotesOff'd
  (`sounded`), a switch of CHORUS lands at once. The tests that measure
  another part turn it off (`held()` and the plain patches); the house rules'
  tests keep it on. Tails hear its sweep start again, as they hear the LFO.
- **The JX-3P's DCOs correct every jump and every bend where it falls in the
  sample** (`engine/src/jx3p/dco.hpp`): minimum-phase BLEP and BLAMP tables, so
  nothing is late and a key still sounds on its own sample. The tables are
  numbers written by `tools/jx3p_blep.py` (`blep_table.hpp`): change the
  script and run it, never the numbers. A bend's correction leaves a constant
  (`kBlampCarry`), kept as a state worked out from the slope, never summed.
  **A jump or a bend worked out to a hair past the sample's end is still that
  sample's** (passed by, the wave jumped raw: `testAnEdgeOnTheSamplesEnd`).
  A wave, DCO-2's start phase and the noise seed are each a voice's own; what
  a free voice starts with comes from the count of keys, as batch 1's phase.
- **The step clock walks only the places that may hold something** (speed
  plan batch 1; `PlaceBits` in engine.hpp: `Track::notesWaiting`, `keysHeld`,
  `hitsWaiting`). **Whatever fills a waiting note's place, sets a key's
  `offIn` or fills a drum hit's place sets its bit**; an emptied place may keep
  its bit (the next walk takes it off). A walk visits the bits lowest first,
  the order a walk of every place had. `speed_test` (`testStepClockPlaces`)
  holds it beside an engine that walks every place (`gWalkEveryPlace`).
- **The MIDI thread only uses `sendLive`** (notes and drum hits, its own queue).
- **A hit written into a cell sends that one hit** (`Session::sendDrumHit`),
  never the whole cell again: a keyboard's eight pads at once on a busy
  eight-bar cell filled the command queue (`midi_test`).
- **What an effect shows goes through `Engine::effectMeter(track, slot)`**
  (COMP: the dB it is turning down). The audio thread reads the effect's
  `meter()` after each chunk and keeps it in an atomic; the screen asks the
  engine. **Nothing on the app thread may hold or call an `Effect` that is in
  a slot**: the audio thread can swap it out at any chunk. The FX screen draws
  it three ways: on the row `effectMeterParam(kind)` names (COMP: THRESHOLD; DUCK: AMOUNT; LIMITER: GAIN), as a
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
  `Halfband::late` first, except at MIX 0 exactly; FOLD and FUZZ do the same
  through `BendSide::plain` / `BendSide4::plain` (`fx/bend.hpp`). Test a new effect's
  response at MIX 50%, not only at 0 and 100%.
- **An effect that is late by design has no MIX** (LIMITER: always exactly
  3 ms late, `kLookSeconds`, 144 samples at 48 kHz; a MIX would lay the plain
  sound 3 ms early under it). Its "untouched" is the sound as it came in, 3 ms
  later, to the bit (`testLimiter`, `engine_test`); `holdsTheRules` skips its
  MIX-0 check. **On a track it puts that track 3 ms behind the others**: it is
  meant for the master, where everything is late alike. A swap into or out of
  it lays the sine over itself 3 ms apart for the fade (2.10 of a step,
  allowed 2.3 in `testSwapFade`).
- **An effect with a wave that runs by itself sleeps by the rest rule** (RING,
  WEAR, and MOD's four: `kModRestSeconds`, 4 s of nothing, AND its memory
  exact zeros). FLANGER's, ENSEMBLE's and PHASER's sweeps and TREMOLO's wave
  start again from their start when they wake. **FLANGER's reach (DEPTH,
  narrowed above 2 Hz) moves at most its whole way a second, and the sweep
  goes only as fast as that reach allows** (`reach_` x Hz <= 2): with the
  narrowing worked out at once, RATE thrown from slow to 8 Hz bent the pitch
  five semitones for 25 ms as the sweep closed up (review, 2026-11-14). **TREMOLO is in time, not on
  the beat**: an effect is told the tempo, not where the bar is, so its wave
  starts where the first sound after a rest is.
- **DELAY's newer types (DUB, REVERSE, STUTTER) keep the TICK and count
  time in samples** (`fx/echo.hpp`). **DUB goes to 150% and has its own way
  down** (TAPE's rule: left alone 30 s, a FEEDBACK over 85% eases to 85%,
  `dubTireOf`; 85% or under is never touched, to the bit): its loop is a band
  (Q 0.7) at TONE, so its howl is a roar about TONE, one echo for both sides.
  **REVERSE plays each window of TIME backwards through the next**; the sound
  that wakes it is put in the middle of a window, so THAT hit's backwards echo
  peaks exactly one TIME after it (`T - 1` for an even T); a later hit comes
  back 0 to 2 TIMEs after it, by where it fell in its window. **Its longest
  gap is two windows** (6 s at 1/2 and 40 BPM: `kLongestEchoGapSeconds` in
  engine.hpp), so a bounce's quiet is 7 s (`kBounceQuietSeconds`; it was 4 and
  cut REVERSE's last echo off) and a bus's rest 10. Its windows fade 4 ms
  at each edge; TIME is taken at a window's start. **STUTTER's splices END on
  a slice's edge** (into its start through the 4 ms before it, kept in a
  ring), so a hit's attack is heard whole every repeat; **its slice edges are
  the whole sample at or before the exact edge, counted from waking**, never
  a sum of rounded slices (those drifted off the steps: at 133 BPM the
  offbeat was caught from 9.7 s on); its dice roll a place
  (REPEATS slices) by its number since waking (`stutterRoll`); on ECHO it gives
  back only the repeats. A slot's output there is `in * (1 - mix * a) + mix *
  rep`, never `in + mix * (rep - in)`: that one is not the slice to the bit.
- **REVERB's newer types (BLOOM, SHIMMER) share `fx/tank.hpp`** and keep the
  TICK; nothing under kQuiet goes on a line, so a woken one is fresh to the
  bit. **BLOOM sleeps 5.5 s after its tail** (its swell's 4 s ring must be all
  zeros). BLOOM turned moves its taps at most half a sample a sample (0.05 s
  to 4 s takes 8 s; the swell bends down an octave at most on the way).
  **SHIMMER's way round never gains**: what goes up an octave is mixed with
  what stays at equal power, the two grains' lost quarter is given back, its
  wait in the shifter is paid for in DECAY, and the shifter takes nothing
  under 40 Hz (an octave of a steady offset is itself) or over 7 kHz (with the tank's damping, a steady 14 or 15 kHz folds back 42 dB under itself; `testShimmer` holds it under 30). The
  octaves climb out past 7 kHz, so at SHIMMER full a 20 s tail is 0.58 as long
  (tested). A ceiling of 64 on a line is a guard only.
- **WAH (FILTER) and PITCH (MOD), 2026-11-24.** WAH's level is followed every sample (louder of left
  and right, in fast, back slowly) but its corner is aimed once a TICK from the level then and slid to
  (`Slide`), as VOWEL's; its low-pass is made up by `1/sqrt(Q)` so RESONANCE does not make it much
  louder. It sleeps after a tick with nothing in and the filters under kQuiet, the level with them, and
  wakes with the level at zero. **PITCH plays one head at a time; at a splice the next starts where its sound
  matches** (2026-11-24, after 0.877 shipped two heads half a GRAIN apart with triangle fades: out of step on
  some notes, 35 of 216 held notes dipped over 6 dB, 784 Hz an octave down 19.2; `testPitch` played only 440 Hz,
  a lucky one). The next head starts a GRAIN on and up to 25 ms more (`kReachSeconds`), at the best correlation (rough
  on sums of 8 over every 8th place, then fine within 4 at full rate, left and right added), and the two cross
  in 5 ms. **The search is spread over ticks**: it starts 32 ticks before the splice and does a 32nd a tick,
  three at most (in one go, 10.8 us on the desk); late, a head shifting down waits for it (and for its place to
  be behind the newest sample: GRAIN turned shorter put it at the line's start, -6.8 dB), one shifting up takes
  the best so far. The spreading changes no sample, so `testPitchMatched` holds it by the clock (the slowest
  tick against the middle one). **Shifting up, the line is fed through a 4-pole low-pass** (wet only) whose
  corner reaches the top of the band as the speed comes to 1, so it comes on with no step (at 0.45 of the rate
  it read a corner 5 times the steady one). The speed is worked out once a TICK from where PITCH and FINE are
  gliding to, in semitones, so a click of PITCH slides. Its line holds 0.17 s (8192 samples, as before); it
  sleeps only after 4 s of nothing AND once every place in the line has been written as zero and the low-pass
  is at rest, and wakes with the write place at 0 and the head half a GRAIN back, so a render is the same however
  long the silence was. At 0 shift the wet is the dry half a GRAIN late: a comb against the dry, as any shifter.
  `testPitch` measures the new pitch by band level against the input (a zero-crossing count is thrown by the
  splices); `testPitchMatched` the held level, the fold-back, the house rules (PITCH had not been through
  `holdsTheRules`). 10 mutants killed. **A search is kept only while its splice is near** (review, 2026-11-25):
  sent more than twice as far off as a search starts (GRAIN turned longer, the speed slowed) it is dropped, and at
  a stop (PITCH and FINE at 0) it is forgotten. Kept, a note played since crossed out of step at the next splice,
  9.4 to 9.9 dB, and a search caught half done by the stop went on reading a line written over since, 11.8
  (`testPitchStaleSearch`). The review's own test, one note held through the stop, could not see it: a place
  found on a held note stays in step with it. A held setting renders the same to the bit as before. 4 of 5
  mutants killed; dropping at once at 34 ticks (not twice that) lives: a search started again is paced to the
  speed then, which only fell, so it still finishes in time; the two only saves the work.
- **BAND (FILTER), 2026-11-24: one EQ band anywhere**, EQ's state-variable filter and Simper's sums per
  SHAPE (BELL with proportional Q, k = 1 / (Q A), so a cut is the lift upside down; NOTCH m1 = -k; the shelves
  with g over / times the root of A, so FREQ is the shelf's middle, half the gain there). **A shelf's Q is held
  at 2** (at 30 full-scale noise came out 19 times as loud) and its reading says so; NOTCH's GAIN reads "-". A
  change of SHAPE fades the numbers (g, k, m0..m2: each step between is a filter that holds) over 30 ticks, one
  fade at a time. At 0 dB a BELL or a shelf is the sound to the bit, so the generic "every setting does
  something" check exempts FREQ, Q and SHAPE (testBand moves each). 15 mutants killed.
  **The BELL is matched** (Vicanek 2016, `bellOf`; 2026-11-24, after 0.883 shipped it plain). On g = tan(pi f /
  rate) a bell's gain at 24 kHz is held at 1, so one set high was squeezed back to 0 dB there ("cramping": 16 kHz
  Q 1.5 was 6.0 dB off the analogue up to 20 kHz, 20 kHz Q 4 7.0; now 0.8 and 1.0, the worst anywhere 2.1). Its
  poles are the analogue's own and its gain the analogue's at 0 Hz, FREQ and 24 kHz, written in the same g, k,
  m0..m2 (Slide and the SHAPE fade untouched); a cut is the matched lift upside down; 0 dB is the plain numbers to
  the bit. **Double, on the tick only** (nothing at rest; Vicanek's own sums cancel in float). It keeps svfG's 0.45
  ceiling (past half the rate a cut runs away). A song's BELL moves under 0.1 dB at 1 kHz and below (Q 0.7 up), up
  to 7.7 dB at 20 kHz, toward the analogue curve the FX picture already drew. `testBandMatched`; 6 mutants killed.
  - **FREQ's D-pad push is a thirtieth of its range, and it starts on one** (gesture audit G19, 2026-11-25;
    `pushes` 30 in its row). FREQ is 20 * 1000^v, so a twentieth was 1000^0.05 = 1.41 times, HALF an octave,
    not the third the comment said: the points near hum were 39.9, 56.4 and 79.6 Hz, and the start, 0.5663,
    was on none, so the D-pad could not come back to 1 kHz. A thirtieth is 10^0.1, the decade third octave
    (0.08% off 2^(1/3)): the points are 20 * 10^(k/10), 50.2, 63.2, 79.6, 100 ... 1002 Hz. **The start is
    17/30** (1002 Hz, reading 1.0 KHZ), so B's usual value and the D-pad's point are one number. 60 Hz falls
    between 50 and 63 (the stick, or a semitone step if exact 60 Hz hum ever matters more than the series).
    BAND had shipped that day, so its start could move; a song file writes every setting, so a saved BAND
    keeps its own. 17/30 does not survive the song file's %.6g to the bit, as a twentieth does: it comes back
    a hair off, which reads the same, steps the same (the push's 0.001 slack) and is `sameAsText` to undo.
    The picture's fallback reads the table's default (it was a copy of 0.5663). No other row has `pushes`, so
    no other setting's push moved.
- **A per-sample loop works on locals** (WEAR's `stretch`: up to the tick's
  end, everything the samples change copied in, worked on, copied back).
  Written straight on the effect's own numbers WEAR cost 0.40% of a core; the
  same arithmetic on locals, 0.15% (the compiler cannot keep members in
  registers across the writes to the block). A new effect over about 0.15%
  is looked at this way before it is called costly.
- **A new effect glides every setting** (`fx::Glide` in `fx/glide.hpp`, the one such helper): a
  turned setting slides for about 20 ms and then arrives exactly, so MIX at 0
  is still the sound untouched to the sample. The first block after it is
  made or reset starts where the settings are (`snap`), with no slide. The
  old five do not glide and must not start to.
- **CATCH's memory** (batch S3, 2026-11-25; the whole of it in `drums-and-samples.md`): two 25 s rings made with the
  engine (the mix, and one track named by `setCatchTrack`, fed in `renderTrack` beside the scope), and the last 8 bar
  lines (`barLine`). Nothing is allocated on the audio thread; following no track costs nothing; following one is one
  store a frame (`measured.md`).
