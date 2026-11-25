# The screens

Tracks below zero, the FX screen, SIGNAL, the redraw, the help card, buttons and joysticks, the instrument list, hints, chips, SEL, text sizes, themes, the SET page, the metronome, lights, NETWORK, FOLLOW, the drawn-screen checks.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

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
  **Y has had two jobs here since** (added beside the owner's note, not in it): hold Y + UP / DOWN on one
  of the track's own three lines moves its effect there (`moveEffectSlot`, commit ff7080c, 2026-11-21, no
  owner record; the bar says `HOLD Y move`), and hold Y on BAND's FREQ, GAIN or Q moves the band two ways
  (gesture audit G18, below). Y does nothing anywhere else on FX. The move is a standard since G37
  (2026-11-25): CLAUDE.md's "hold Y + a direction moves a line where R1 is taken" (below, "The buttons'
  standards, as the gesture batches left them").
  The lines (`FxScreen::goTo(row, column)`): 0 to 2 the track's slots, 3 its
  SENDS (`kSendsRow`: not a slot, so `data(row)` is not for it), 4 and 5 the
  buses (`kBusRow`), 6 and 7 the master (`kMasterRow`). **Column 0 is the line
  in the list; 1 and up are its settings** (SENDS: 1 ECHO, 2 SPACE).
  - Up / down in the list goes round; right goes to the setting the line
    last used (`last_`, kept through a change of track); left comes back.
    `update()` only pulls the cursor in when the line has fewer settings now.
  - **In a slot's settings, A HELD + LEFT / RIGHT switch its TYPE** (owner, 2026-11-14;
    `FxScreen::stepType`): the type before / after, in the grip that turns the value with
    UP / DOWN, as LEFT / RIGHT is the TYPE with A held in the list. The ends stop, the
    cursor keeps its row (pulled in where the new type has fewer), every setting row alike,
    D-pad only (the stick with A held turns the value), repeating at the list's pace
    (`kListEveryMs`). **Without A, LEFT is the list from any setting and RIGHT does
    nothing.** (A first try that day had plain LEFT / RIGHT switch, with LEFT the list only
    from the first setting: LEFT doing two jobs was awkward, and a long RIGHT in from the
    list ran through the types. It is gone, `cameIn_` with it.) **With A held the
    heading's TYPE row and the bar show the key that switches** (`typeKey`, `typeKeyFor`:
    RIGHT, LEFT or LEFT / RIGHT; no RIGHT on the family's last type, no LEFT on its first,
    none for a family of one or on SENDS); with A up the bar says LEFT list and HOLD A
    "turn, type" ("turn" where there are no types). B's hint there is "default".
  - **A slot remembers each type's own settings** (`Session::rememberType` /
    `rememberedType`, `switchEffectType` in fx_screen.cpp): a switch goes through
    `changeEffectType` (lanes moved as always: **a remembered type's automation is not
    kept**, only its settings), then sets the remembered settings, WRITE off. COMP ->
    DUCK -> COMP is the COMP to the bit. **Every setting, so a MIX or a bus's LEVEL too**:
    a bus turned down to 0 on PING is at TAPE's own LEVEL again on the way back (plain
    `changeEffectType` carried it; the owner has not ruled on this). **The lanes are not
    to the bit**: a shared lane is carried there and back, so where the two scales differ
    it comes back rounded (CLEAN -> TAPE -> CLEAN: FEEDBACK 0.3333 as 0.33, a TIME lane to
    the nearest choice) and an unshared lane is cleared; undo puts them back. The list's LEFT / RIGHT with A held tries and
    commits the same (`wouldGoIn` takes the remembered settings). In memory only, never in
    the song file; forgotten by `Session::setEffect` when another FAMILY or EMPTY goes in
    (a same-family `setEffect` keeps it: `changeEffectType` goes through it), by
    `applyProject`, `setTrackKind`, `addTrack`; `removeTrack` moves the tracks after it
    up with theirs. Undo puts back the type before (as any change of TYPE); it does not
    touch the memory. With the song stopped a switch sounds the track (`hear`); playing,
    or stopped by an end, it does not. `app_test` `testFxSwitchesType`, `testFxSwitchMends`; the engine's slot is read with
    `Engine::effectKindIn` (`Effect::madeAs`, kept by the audio thread beside the meters).
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
    in brass on the cursor's line. **How many rows is LARGE's at every size**
    (as the TYPE row's view is, 2026-11-24): one row where LARGE fits it in
    `kPickTypesRoom` (at the size chosen if that fits too, else LARGE); two where
    LARGE needs two, split where the longer row is shortest, drawn at the size
    chosen where each row fits, else LARGE (`pickRows`). Since PITCH, MOD's six
    are two rows ("chorus, flanger, ensemble" over "phaser, tremolo, pitch") at
    every size. `textsize_test` (`testTheListOfEffectsHoldsTheLaterTypes`)
    holds the planned types (WAH and PITCH among them) to it at every size.
  - **The heading's TYPE row shows as many types as fit** about the one the
    slot is, "..." (`kTypeMore`) for the rest (`typesInView`, in the room the
    row keeps for the LEFT / RIGHT key held or not). **Which are in view is
    LARGE's at every size** (`typeRowView`): MOD's five do not all fit at
    LARGE, and SMALL showing more was other words (`textsize` fails that);
    drawn at the size chosen where they fit it, else held at LARGE, its row
    with it (XL). A family of one shows its type all the
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
    **A held on a setting is kept through L1 / R1 where the new track's line holds the same effect**
    (gesture audit G44, 2026-11-25: turning one dial across the tracks took a fresh A per track, and
    meanwhile the stick moved the cursor). "The same" is the same `EffectKind`, not the same family or a
    setting of the same name: CLEAN's FEEDBACK in TAPE's place is another job, and with WRITE on it would
    be written. Another effect, or an EMPTY slot, lets A go: an empty slot pulls the column back to the
    list, where a held A is the list of effects and the stick's push would TRY effects on the new track.
    SENDS keep it (the same two on every track), and a bus's or the master's line is the same whatever
    the track. At the first or the last track nothing changes, so A stays held (it was let go before). A
    try in the list is called off and A let go, as the owner's rule above says. The MOD tab does the same
    (`ModScreen::stepTrack`): the same `modKind` on the line, and on a MOD's fifth setting the same job
    (`ModKnob::moreName`: a FREE LFO's SPEED is not PHASE); a STEPS row keeps it for any two MODs of one
    kind. `app_test` `testFxHoldKeptAcrossTracks`, `modtab_test` `testAHeldThroughTheTracks`.
  - **Hold B deletes an effect** (a slot's line or a bus's: a bar fills for
    `kDeleteHoldMs`, half a second since G29 below (it was a second), on the line and the heading, which says
    `kDeletingWords`; a tap does nothing; any other button or a move calls it
    off; pit stop 2026-11-14; `app_test --hands` runs these tests). A tap of B on SENDS still empties both sends,
    and on a setting gives its usual value; a bus's LEVEL goes back to full
    (`effectDefault(kind, i, true)`).
  - **A D-pad push on an amount is a twentieth, unless the effect's row says otherwise** (`EffectInfo::pushes`,
    `effectPushes`; `pushed` in fx_screen.cpp). The twentieth keeps the arithmetic it always had (`kNotch`), so a
    value the D-pad reaches is the same to the bit. Only BAND's FREQ has its own: thirty (gesture audit G19,
    2026-11-24: engine.md's BAND rule). The sends stay twentieths.
  - **Hold Y on BAND's FREQ, GAIN or Q moves the band two ways at once** (gesture audit G18, 2026-11-25; the
    owner allowed "any of my decisions can be reversed if there is a more logical method", 2026-11-24, and took the
    audit's hold-Y option): LEFT / RIGHT is FREQ, UP / DOWN GAIN, on the Q dial Q (`FxScreen::bandGrip`,
    `kBandFreq` / `kBandGain` / `kBandQ`). **The stick and the D-pad do the same job on each axis**: the D-pad a
    push (`pushed`, the row's own pushes: FREQ thirds of an octave, GAIN and Q twentieths), the stick both axes
    together, squared, at A's turning pace (`kTurnPerSecond`), with no lock to one axis, as the SONG mixer (owner,
    2026-11-14). The cursor stays. Not on SHAPE (a choice: Y held there leaves the D-pad the cursor's), not on
    another effect (Y on a dial did nothing before and still does), not with A held (A's jobs stand: A + LEFT /
    RIGHT is still the TYPE, owner 2026-11-14, untouched), not while trying or with X's MOD. Through
    `setEffectParam`, so WRITE records both lanes. **The option not taken: hold A + the stick across = FREQ**:
    on a FILTER dial A + D-pad LEFT / RIGHT is the TYPE, so the stick and the D-pad would have done two jobs on
    one axis, which no other two-way grip (the mixer, a drum hit, a roll note) does. Y on the list (column 0)
    still moves the line; a dial is column 1 and up. **They did meet** (review of G18, 2026-11-25; this said
    "the two never meet"): with Y held on BAND's line, RIGHT goes into the settings (usually FREQ), and the
    same RIGHT held then repeated into FREQ, a third of an octave a repeat; a sideways slip of the stick while
    moving the line swept FREQ with no A held; UP held from SHAPE landed on Q and turned it. **A direction that
    brings the cursor onto BAND's dials with Y held is a move, not a turn** (`FxScreen::moved`, the roll's
    rule for X: a direction already held is no turn): the held D-pad stops repeating and the stick waits until
    it is back in the middle; a fresh push turns. The bar with Y up is full (it already
    leaves HOLD X out), so "HOLD Y" is in the manual only; with Y held the bar says LEFT / RIGHT freq, UP / DOWN
    gain (or q). `app_test --fx` `testHoldYMovesABandTwoWays`. **Not heard on the handheld.**
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
    pixels over, and the line says SATURATE: the owner is content with that, 2026-11-14). A pair that would end inside the 3-pixel clearance before the mark draws its family small too (REVERB BLOOM, 1.4 in; before batch c3 only one past the mark did). REVERB SHIMMER is drawn as SHIMMER alone, as SATURATE. The tests ask the same
    function. A new type's name is checked there at every size.
  - **The page goes down to `kPageBottom`** (449: 7 above the hints bar, as
    its top is 7 under the top bar; owner, 2026-11-13: it stopped 40 short).
    The same layout, taller: a line of the list is `kLineH` 41 (was 36), the
    picture `kPicH` 140 and the dials `kDialH` 168, the heading `kHeadH` 93 (was 92), a
    send's row `kSendH` 193 (was 128; its value, bar and purple line at
    `kSendValueY`, `kSendBarY`, `kSendLineY`, the last two from its foot), a
    line of the list of effects `kPickLineH` 38 (was 31). `fx_screen.hpp`
    works each out from the foot and `static_assert`s that all four columns
    end on it.
  - **A slot's settings are DIALS under a PICTURE** (owner, 2026-11-24, design B of two mock-ups: "A was
    feeling a bit too clinical"). Under the heading, `drawEffectPicture` (`app/fx_picture.cpp`) draws what the
    effect does from its settings as shown (a filter's curve, a drive's bent sine, a delay's echoes, a reverb's
    tail, a MOD's sweep, COMP and LIMITER's in/out curve with the meter as brass cells): theme colours only, no
    blending, no words, rectangles gathered by colour. Then four dials (`dialX`, `kDialCY`, `kDialR`; an
    effect with three settings leaves the fourth place empty): an amount a ring lit as far round, a choice a dot
    each, the MOD's brass tick across the ring, COMP's strip at the dial's foot, automation a purple name and
    line (`kAutoY`). A value has `kValueRoom` (88); `testFxTextsFit` holds every reading to it. Tests read a
    dial through `FxScreen::dialAt(p, part)`; `testTheFxPictureFollowsTheSettings` holds every effect's
    picture (drawn, inside its box, theme colours, at least two settings move it, the meter's brass grows).
    **Nobody has seen it on the handheld.**
  - **Playing, the picture moves with the song** (owner, 2026-11-24: "light visual feedback"; `FxLive`): a
    purple line where the song is (DELAY two bars across, its echoes standing where TIME puts them in steps and
    lit as the line passes; MOD four seconds at the rate it READS, with a brass dot on the sweep; DUCK a bar),
    the track's level (`Engine::trackPeak`, held and falling a fifth a pass) in a DRIVE or CRUSH wave, a reverb's
    hit and a brass dot on COMP / LIMITER's curve. **Stopped, nothing moves**: the song playing is the stir, so
    the redraw needs nothing new. The step's part is counted from when the screen saw the step (SDL ticks).
  - **Each theme has a style** (`FxStyle`, `fxStyle()` by the theme's name; "extra sauce based on the theme"):
    VINTAGE (BROWN, GRUVBOX: a knurled cap, an engraved scale, a dotted grid), NEON (DRACULA, TOKYO, MONOKAI: a
    glow ring outside the lit arc, a lit pointer, curves with a 1-pixel `kLine` afterglow), CLEAN (the rest: a
    thin ring, a dot pointer, no grid). Shapes only: colours stay the theme's jobs. A new theme falls to CLEAN.
  - `--frames DIR` with `--shot` keeps every frame drawn (DIR/0000.bmp on): how to see what moves without a
    handheld.
  - What is automated in the scene worked on has a purple name and line in its
    dial, and a purple line under its line of the list. WRITE itself is said by
    the top bar.
  - The places things are drawn are constants in `fx_screen.hpp`, so the
    tests can read the drawn screen (`testTheFxScreenAsDrawn`, with `inked`:
    is this text drawn here in this colour). `app_test --fx` runs just the FX
    screen's tests (about 9 s).
- **SIGNAL is a thin strip across the DEVICE page, just above the hints
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
  placed by `ui::scopeLineY` (full scale 4 rows inside the strip's edges), **its height bent, not to
  scale** (`scopeBent`, the power `kScopeBend` 0.4; owner, 2026-11-14: to scale a quiet recording was
  half a pixel): 30 dB down stands 3 pixels, half scale 11 of the 14; louder is never smaller. A box taller than `kBoxUsualH` (70) moves its value down by half
  the extra (`boxValueDrop`); at 70 and under nothing moves. The envelope
  pages never had the scope and do not. `scope_test` reads it all off the
  drawn screen in BROWN and PAPER.
- **The screen is drawn only when it can have changed** (owner, 2026-11-15: save the battery,
  "I don't want the user to notice"; `app/redraw.hpp`). main.cpp still goes round every 16 ms
  (a press cuts the rest short: below), reads the buttons and runs `App::update`; only `App::draw` and the present are left out on a
  pass with nothing to show. **The picture must be exactly what drawing every frame shows, and a
  button is drawn on the pass it is pressed.** A pass is drawn on a STIR (`Stir` in redraw.hpp):
  any SDL event but a stick's motion, a button held, the stick pushed, the song playing; the
  engine's readouts moved since the last picture (where the song is, the SIGNAL samples,
  every effect meter, MIDI notes and keyboards: `App::readouts`); `App::stirring` (the volume,
  the App's message, the panic notice, the update's answer, plus `Session::stirring`: a fit, a
  recording, a bounce, WiFi on, and the screen's own `Screen::stirring`: a line said for a moment,
  a line over a recording, the list's resting row, the button test). **And the pass after any
  stir** (`kStirAfter`: what ends a timer is drawn), and `kKeepDrawingMs` (2 s) after, as a margin.
  The controls setup, a text size or theme changed, and every still (`--shot`, `--smoke`) draw
  every pass. `--draw-every-frame` draws as before, for measuring.
  - **A new thing that moves by itself says so** (a timer, an animation, anything read from
    another thread or the clock): a bit, set by the App, the Session, its screen's `stirring()` or
    a readout, **and a part in `tests/idle_test.cpp`'s session** that sets it going and then
    leaves the buttons alone. The test draws EVERY pass into a canvas with the margin at 0 and
    fails a pass left out that is not the last picture shown, pixel for pixel, naming the pass,
    the part and the screen. Then it takes each stir away (`Redraw::ignore`) and must fail;
    PLAYING, RECORDING and PICKER are known to be covered by others where it can be shown (`belts`)
    and are not run. `idle_test --quick` is the session alone (about 20 s); `IDLE_PRINT=PART`
    prints each pass's reasons in a part; `--measure` times a pass's drawing on five screens.
  - The test makes the sound between passes, as the audio thread does while main.cpp waits, and
    holds it up once (`stalled`): a Stop heard three passes late takes the playhead away with no
    button, which only the readouts see.
  - **The 16 ms rest is four naps of 4 ms, and a press ends it** (gesture audit G06, 2026-11-24;
    `app/pass_wait.hpp`). Before each nap the events that came are taken off SDL's queue and read on
    a copy of the program's `Input`: a button pressed or let go (a trigger or a D-pad sent as an
    axis too, once past its threshold) or a volume key ends the rest, and the next pass reads it at
    once. Before, every pass ended in one `SDL_Delay(16)`, so a press that came during the drawing
    or the rest waited all of it before it was read. **The idle cadence is the owner's rule above
    and is kept**: nothing but a press ends the rest. The stick's motion does not (a resting stick
    that jitters sends a stream of it: `stirred` already left it out), nor a key's repeat, nor the
    window, so with no press a pass is the same 16 ms rest. A press costs at most one pass (one
    picture) more than before; a held button none. The events the rest took are read by the next
    pass first, in order (`PassWait::next`), so nothing is lost or read twice.
    - Only SDL calls every SDL2 has (`SDL_PumpEvents`, `SDL_PeepEvents`, `SDL_Delay`): the
      handheld runs its firmware's libSDL2, of an age not known here. `SDL_WaitEventTimeout` was
      not used: it wakes on every event, the stick's included.
    - Pumping every 4 ms also times a press: SDL2 stamps an event with `SDL_GetTicks` as it queues
      it, and `ButtonEvent::ms` carries that stamp. **TAP TEMPO times a tap by it**
      (`whenPressed`), not by the pass's clock (`lastMs_`, up to a pass out each tap). A tap during
      the rest is timed to within a nap (4 ms); one during the pass's work is stamped when the
      work ends. A ButtonEvent made up (a gamepad gone, a test's) has no time, and the tap then
      takes the pass's clock.
    - `controls_test` holds it (`testTheRestEndsOnAPressAndOnNothingElse`, `testTheRestWithSdl`
      with SDL's make-believe gamepad, `testAPressHasItsOwnTime`), and `easy_test taptempo`
      the taps (`testTapByThePressesOwnTime`). `idle_test`'s pass is still 16 ms: a press's
      shorter pass is a drawn pass, and nothing in the redraw counts the time between passes.
    - **main.cpp must read its events from the PassWait** (`while (wait.next(event))`), never
      from SDL's queue: the rest takes most presses off that queue, and they would sit in it unread
      (the program would ignore most buttons). The tests above model the loop themselves, and every
      other still pushes its keys before the first pass, so none of them could see that line go
      wrong (review of B1, 2026-11-25). **`textviews` runs the program on the `rest-presses` still**
      (`testPressesInTheRest`): SEL held and eight taps of UP pushed from an SDL timer 7 ms apart
      once the passes are going, so most land in a rest; the top bar must read 108 BPM from 100.
      With the poll put back it reads 100 (even SEL's press is lost). What it cannot see: main.cpp
      resting with `SDL_Delay(16)` again instead of `wait.rest` (every press is still read, only
      later); that is the rest's latency, held in `controls_test` on the PassWait itself.
    - Measured (desk, 2026-11-24, `controls_test --measure`, a model of the loop with 1.7 ms of
      work a pass): a pass with nothing pressed 17.8 ms before, 18.2 ms now (the four naps run
      about 0.1 ms over each); a press read after 9.5 ms on average and 17.7 at worst before,
      2.0 and 4.1 now. With 25 ms of work (the handheld's 13 to 20 times, an estimate): 18.5 /
      40.8 ms before, 8.8 / 24.4 now. The whole program left alone on the drum grid (`--demo`,
      offscreen, dummy sound, under strace): a pass 20.05 ms before, 19.92 ms now; its CPU over
      20 s the same within the desk's noise (8.4 to 8.8 s before, 7.9 to 8.3 s now, nearly all
      the sound's threads). **Not measured on the handheld**: what four pumps a pass cost there,
      and how late its presses really were.
  - Measured (desk, 2026-11-15): the drum grid left alone, the whole program for 20 s (software
    renderer, dummy sound): 1.1 s of CPU, against 5.8 s drawing every frame. A pass's drawing in
    the test's software renderer: 1.8 to 2.4 ms by screen; deciding whether to draw: 0.001 ms.
    **Not measured on the handheld**, where the renderer and the present are its own.
- **The help card says how hard the program works, above the battery** (owner, 2026-11-15):
  `SOUND 43%  CPU 37%` (**WORST, the worst call in the second, is still measured but not shown**: owner,
  2026-11-17, "not that helpful"). **Every `Engine::render()` is timed** (`LoadMeter`,
  `apotheca/load_meter.hpp`: a steady clock at the call's start and end against its frames at the
  engine's rate; over a second of sound the share and the worst call go into atomics:
  `soundShare`, `soundWorst`, `loadSeconds`). It costs about 50 ns a call (`load_test --measure`:
  0.001% of a 256-frame block). CPU is the whole program from `/proc/self/stat` (utime + stime over
  the wall time over the cores: `app/cpu_use.hpp`), read once a second on the app thread only while
  the card is open, a second after it opens. The App keeps the numbers it draws (`loadShown`), and
  the redraw compares them (`Readouts::load`, `kStirLoad`; a belt in `idle_test`: the card is up
  only while MENU or SELECT is held). The line's room ends 10 pixels right of `quit Apotheca` at
  the size drawn, so it is held at LARGE at XL. `load_test`, `app_test`
  `testTheHelpCardSaysTheLoad`, stills `help`, `help-load`. **Not run on the handheld.**
- **A new `Session` tells the engine what the buses hold** (its constructor).
  The engine starts as a new song in every other way, but with empty buses;
  without this a handheld with no saved song had sends that did nothing.
- **Every screen's `update()` re-reads held buttons from `input` each frame**
  and drops its own "held" flags when they are not held. Trusting the release
  event left A "stuck down" after a screen change.
- **The joysticks are kept open as they come and go** (`app/joysticks.hpp`; a gamepad that dropped
  out was never read again). A gamepad gone or come back lets go of every button (`Input::letGoOfAll`,
  each let-go handed on). **After every event main.cpp asks `Input::pending`** until it says no: a hat
  sends one event for two changes (a diagonal let go, one way straight to another), and RIGHT stayed held.
  A sound card gone (`SDL_AUDIODEVICEREMOVED`) is opened again a second apart. `controls_test`
  (`testAGamepadComesBack`, SDL's make-believe joystick). **The sound card's return is not tested.**
- **The controls setup asks for L3 and R3 after the stick** (gesture audit G05, 2026-11-25; `ControlLearner`,
  `kSteps` 19): L3 is MASTER from any screen (owner, 2026-11-17), and on the stock port it never fired:
  with a learned map a joystick button not in it is dropped (`Input::handleMapped`), and SDL's own
  LEFTSTICK is never read then (the gamepad is not opened). MENU, the stick, L3 and R3 may each be let go
  by after `kSkipAfterMs` (8 s); skipping the stick skips L3 and R3 too. **An axis is never L3 or R3**
  (THAT IS THE STICK MOVING: a stick way learned as L3 would press it whenever the stick was pushed). A
  control already in the map is refused at any step, a kept map's too. **controls.txt's first line is
  `apotheca-controls 2` from this setup on** (`ControlMap::version`); a 1 (or no number) with no L3
  (`neverAskedL3`) is asked for L3 alone, once, at the next start (`askOnlyFor`, main.cpp), and written
  again as a 2 whatever the answer. Our firmware's `controls-rg40xxv.txt` is a 1 with L3 in it: not asked.
  **Whether the stock firmware passes the stick's click is not known**: the setup's "THE HANDHELD LAST
  SENT" line will say. R3 has no job in the program yet (asked because the brief asked for both).
  The setup's last line was a promise nothing kept ("hold any button for two seconds just after Apotheca
  starts"); it names the way that is there: SET, CONTROLS, hold A. `controls_test`
  (`testAnOldMapIsAskedForL3Once`), `textsize_test` (every step's words fit their box).
- **The help card with no MENU is SELECT held by ITSELF** (gesture audit G38, 2026-11-25): any other button
  pressed while SELECT is down (`App::selectUsed_`, every screen, set at the top of `onButton`) puts it
  away until SELECT is let go, as MENU + a volume key does (`menuUsed_`). It covered the box above the bar,
  so SEL + B, B, B undid blind. `app_test` `testTheHelpCard`.
- **The bar offers the help card only where it comes up** (review of gesture audit A3, 2026-11-25):
  `ui::gHelpHint` is empty while `busy` (any name keys, `App::typingAName`, and SET's keys and button
  test), so the bar does not end in HLD SEL: HELP or MENU: HELP there. A3 had made every button the
  preset keys', and their bar still offered a card that no longer came up (SET's keys and the folder
  keys had the same wrong hint before). `Font::draw` of an empty text draws nothing, so `hintHelp` and
  `gHintsOver` need no change (the 6 pixels it keeps for the gap are kept). **It is emptied at the END of
  `App::update`, after the screen's own**: the button test closes by itself in `SettingsScreen::update`, and
  set before that, the hint lagged a pass, so the pass the redraw drew lacked it and the next (left out)
  had it (`idle_test`: 485 pixels). `app_test` `testDeletingAPresetSaysItCannotBeUndone`,
  `testTheHelpCard` (the button test).
- **SET acts on A's LET GO on BOUNCE and TRACK NAME, only if nothing else went down while A was held**
  (gesture audit G15, 2026-11-25; `SettingsScreen::aTap_`, `aRow_`): a direction, the stick moving
  something, or any other button calls the tap off, and so does the cursor on another row. Hold A + a
  direction turns a value on every other screen, and here that habit started a bounce on A's press
  (RIGHT then only changed what to bounce). TEMPO keeps the press (tap tempo is A in time); WIFI PAGE,
  HOTSPOT and BUTTONS keep it too (no LEFT / RIGHT there, so no habit to meet). **Hold B a second on a
  bounce under way stops it** (`stopping_`, `kStopHoldMs`, a red bar as NETWORK's forget, the note
  `kStoppingWords`, the hint HOLD B stop it in place of A (working); any other press calls it off). The box
  above the bar then says `kBounceStoppedWords`. `app_test` `testSetActsOnTheLetGo`.
- **One hold length a kind: half a second where undo brings it back, a second where nothing can**
  (gesture audit G29, 2026-11-25, under the owner's "any of my decisions can be reversed if there is a
  more logical method", 2026-11-24; `ui::kHoldUndoneMs`, `ui::kHoldForGoodMs`). Each screen had its own
  1000 ms; the effect's was recorded as "a bar fills for `kDeleteHoldMs`, a second" (pit stop
  2026-11-14, above), and the manual said 1 s for all but the track. An undoable hold waited as long
  as a permanent one, though SEL + B brings it back, and it is what is done most (an effect, a MOD):
  **0.5 s** for an effect (`FxScreen`), a MOD (`ModScreen`, `ModKnob`), EDIT's and VOICE's every box
  back (`KitScreen::kDeleteHoldMs`) and a scene emptied (`SongScreen::kSceneHoldMs`); **1 s** for a
  recording or folder (`SamplePicker`), a song, an own preset (`SynthScreen`, `SamplerScreen`) or kit
  (`kKitDeleteMs`), a network forgotten and a bounce stopped (`SettingsScreen`). **A track keeps its
  two seconds, red only after a quiet half second** (commit 0852245, 2026-11-11: the B tap that empties
  a cell never flashes red), and the update's hold over WiFi its own second (`App::kUpdateHoldMs`, A's
  too). **The bars keep their looks** ("the look is settled": the song row's brass, the KIT stop's 1 px
  line between the name and lane 1, the sampler PRESET's 2 px line). A shorter hold fires more easily;
  undo is what makes that safe, which is why only the undoable ones are shorter. The stills of the
  undoable bars are taken at 300 ms (`stills.cpp`). `app_test testTheHoldLengths`, `modtab_test
  testHoldBTakesASecond`, `copyscene_test testClearGesture`.
- **B on a SET row under PLAYING, LOOK or HANDHELD gives it its usual value** (gesture audit G17,
  2026-11-25; `SettingsScreen::hasUsual`, `usual`): B did nothing on SET's value rows, where B on an
  effect's dial, a sampler setting or a MOD is its usual value, so VOLUME or STICK SPEED had no
  one-press way back. The usual values are the program's own starting ones, named where they start
  (`Session::kVolumeAsBuilt`, `kOctavesAsBuilt`, `kTextSizeAsBuilt`, `kThemeAsBuilt`,
  `ui::kStickSpeedAsBuilt`, `kBrightUsual`, `kRingOn`, `kLightTheme`, `kClickOff`, `kMutesAtOnce`).
  **Not the SONG rows** (TEMPO, CHORD PACK, KEY, SCALE, TRACK NAME): they are the song, not settings,
  and BOUNCE's B is its stop; whether B there should give 120 BPM and the rest is the owner's call.
  The bar says `B default` (fits at XL). **A synth page keeps "B goes back to MAIN"** (the standard's
  "B goes back"; G17's proposal to make it a usual value was not taken). `app_test
  testBOnASettingIsItsUsual`.
- **The tabs go round** (gesture audit G35, 2026-11-25; `App::onButton`, `showTab`): L2 on SONG is SET, R2 on
  SET is SONG. They stopped at both ends, so SONG to SET was R2 five times and L2 on SONG did nothing; no owner
  decision was found for the ends. A quick run of L2 can now carry on past SONG into SET (the audit's trade-off).
  `app_test` `testTabs`.
- **A long list goes round on a PRESS, and a run stops at its end** (G35; `ui::listStep(at, by, count, pressed)`):
  SET's rows (`SettingsScreen::step(by, pressed)`) and the list of songs and its revs (`SongScreen::moveCursor`'s
  `pressed`). A press of the D-pad at an end goes to the other end; a held D-pad running on, and the stick, stop
  there, so a run never carries past the end it was going to (as SONG's scenes already stopped short of the heads:
  "only a press of the D-pad goes onto the head"). The page follows (`goTo`). **Left as they were, on purpose:
  tracks stop** (FX and MOD's L1 / R1, SONG's columns: "nothing in the program wraps tracks", owner, 2026-11-14:
  the hints show which way is left) **and FX's TYPE stops at its family's ends** (owner, 2026-11-14: the bar shows
  no RIGHT on the last, no LEFT on the first). Lists that went round on every move still do (the FX list, KIT's
  rows and columns, synth MAIN, presets with UP / DOWN, the instrument list); SET's KEY, CHORD PACK, TRACK NAME
  and BOUNCE values still go round and SCALE, THEME, TEXT SIZE still stop (values, not the list).
  `app_test` `testTheEndsOfSetsRows`, `testTheSetupScreen`; `save_test` (the list of songs and its revs, a press
  and a run, `Rig::run`); `lights_test`, `theme_test` (the page with it).
- **The drum grid's column is a ring both ways** (G35; `PatternScreen::moveCursor`): down from lane 8 is BARS, then
  the tempo, then lane 1, as up from lane 1 was always the tempo, then BARS, then lane 8. Down from lane 8 went
  straight to lane 1, skipping both. The brushes and B's drag skip the two stops (`onStep`). `ui_test`
  `testCursorMovesAndWraps`. (The KIT page's lanes have the same one-way ring: down from lane 8 is lane 1, up from
  lane 1 the KIT stop. Not in the brief; left.)
- **On SET, L1 / R1 go to the first row of the heading before / after the cursor's, round the ends** (gesture
  audit G28, 2026-11-25; `SettingsScreen::jumpGroup`). TEMPO to THEME was 11 presses; it is 3 (R1, R1,
  DOWN). (TEMPO to CONTROLS, 17 presses on the stock port before the audit, is one since G35: UP from the first
  row goes round. This note said "3 (L1, DOWN, DOWN)" until the A5 review, 2026-11-25: the slower path.) From
  inside a group L1 is the group BEFORE (THEME, L1: CLICK), never its own first row: one meaning each way. A
  press, never a run; a hold of A on CONTROLS lets go as a move does (`holding_`); while a name or a password
  is typed every button is the keys'; NETWORK's pick (A held) swallows them. **L1 / R1 kept their FX and MOD
  meaning** (the track, owner, 2026-11-14): SET's TRACK NAME and BOUNCE THIS TRACK act on the
  track worked on, and on SET L1 / R1 are the headings, as they are a page in the list of recordings. The bar
  says `L1 / R1 page` (the recordings list's word: "heading" ran 15 pixels over on NETWORK with HOLD A and HOLD B,
  "group" a fraction); it is left off while NETWORK picks or forgets, whose bars are their own. **The panic puts
  the row back** (`App::setRowBeforeShoulders_`, as FX's track). `app_test` `testSetJumpsHeadings` (every row's
  bar fits and says it).
- **The stick on a CURSOR speeds up when it is pushed all the way and held** (gesture audit G07, 2026-11-25;
  `ui::StickClimb`, `kStickFullPush` 0.9 after the dead zone, `kStickClimbTo` 3, `kStickClimbMs` 1000): a part
  push moves at the screen's own rate exactly as before; a full push held climbs from that rate to three times it
  over a second and stays there. A climb not asked for `kStickClimbGapMs` (100 ms: another screen, the stick on
  other work) starts again from the bottom. Every rate is still times STICK SPEED, the climb with it.
  **The owner's earlier call, kept in part and moved:** "The owner asked for a quarter off (2026-11)" (`ui.hpp`,
  commit 37bcdbc, now STICK SPEED's default 75%). The quarter off stays on every rate and on every part push; only
  a FULL push HELD goes past it. Why that is more logical: at 75% the stick ran slower than a held D-pad on most
  screens (SET 5.25 rows a second against 9, FX 10.5 against 33), against "stick for big moves, D-pad for exact
  ones"; a full push held is the one gesture that only ever means a big move, and SONG's scene 1 to 40 by the stick
  halves (about 2.2 s to 1 s), SET's TEMPO to STICK SPEED (14 rows on the stock port) about 2.5 s to 1.2 s. (TEMPO
  to CONTROLS is not the example: UP goes round, one press, G35.) The owner allowed reversing a decision where a
  method is more logical (2026-11-24). **Cursors only**: SET's rows, SONG's cells, the drum grid, the roll, FX, MOD, the
  sampler's and a synth's stops, the kit's lanes and its EDIT and VOICE boxes. **Not**: a value turned (hold A,
  hold X, SET's LEFT / RIGHT: "the owner's rule: faster is better" would argue for it, but one turn time slows
  hit LEVEL from 0.44 s to 1 s), a list (the list of songs, the instruments, NETWORK's networks, the recordings
  list, which squares its push already), the name and password keys, and **the kit's sounds with A held** ("slow
  enough to hear each sound", kit_screen.cpp). A wrapping cursor (the drum grid's row, the FX list) goes round
  sooner when held full: the audit's named risk. `app_test` `testTheStickClimbsOnACursor` (the rule; SET's rows
  part, full and at 50%; TEMPO turned; the kit's sounds; the instrument list; and, since the A5 review, every
  other screen's own call at STICK SPEED 25%, a full push against one just short of full from the same stop: FX,
  MOD, the sampler, a synth's MAIN across and down, the kit's lanes, EDIT's and VOICE's boxes, so dropping the
  climb at any one of them fails), `testTheStickSpeedAndTheVolume`
  (SONG); `ui_test` (the drum grid, painting); `wifi_test` `testTheStickOverTheNetworks` (NETWORK's list, A held). **0.9 is a guess at "all the way"**: what the handheld's stick
  reads at its rim is not measured.
- **A held key's repeats are one helper, `ui::Repeat`, on every screen** (gesture audit G08, 2026-11-25).
  Each repeat is counted from when the one before was DUE, not from the frame that saw it, as the list of
  recordings always counted: the cursor's 30 ms was rounded up to two frames' time (about 28 to 31 a second, not the
  33 the manual says), and now is 33; a pace faster than the frames gives several in a frame; a frame held
  up more than `kRepeatStallMs` (100) makes one, not a burst. Each screen names its pace (`ui::Pace`:
  Cursor, List, Click) with `due(now, held, pace)`; `due(now, held, waits, runs)` waits as one and runs as
  another. Twelve hand-rolled copies (`repeatAt_ = nowMs + every`) went. Tests that counted repeats were
  moved to the exact count (`chord_test`, `feelapp_test`, `app_test`). `app_test`
  `testHeldKeysRunAtTheirPace` (the rule, the roll's keys a second, the instrument list, LOOP, the roll's
  clicks), `drumsynthapp_test testHeldKeysOnTheKitPage`.
  - **Brought to the cursor's pace** (the standard calls them cursors): the KIT page's lanes and columns
    (they ran at the list's: a held DOWN is 3.7 times as fast) and a drum synth's knob with A held (0 to
    100 took about 11 s, now about 3). With A held the kit's sounds, FIT, CHOKE and the kits, and VOICE's
    MODEL (models and named voices), stay lists.
  - **The list of instruments runs when held** (it did not), at the list's pace, still going round (G35
    left it going round on every move; a held run now does too). A or B closing it ends the run, so a
    direction still held does not run on over the cells (`testHeldKeysRunAtTheirPace` closes it with each: the
    B half had no test until the C1 review).
  - **The sampler's LOOP clicks once a push of the stick** past half (`loopPush_`): up ON, down OFF, as
    the D-pad and a synth's choices; back in the middle, or pushed the other way (a flick across can pass
    the middle between two frames), the next push clicks. It was turned as a range of one: about 1.3 s of
    full push to flip it.
  - **Not made slower, though the standard would** (the owner's rule in `pattern_screen.hpp`'s `Feel`:
    "faster is better; nobody has ever asked for slower"): the list of songs and its revs, a synth's and
    the sampler's PRESET, FOLDER and choices with A held, CLOUD's RECORDING, FX's choice dials (TIME,
    filter TYPE, BAND's SHAPE with A + UP / DOWN) and BARS on both grids keep the cursor's pace. The
    lists that always waited the cursor's 170 before running at the list's 110 keep that wait (FX's list
    and TYPE, MOD's targets and clicks, the sampler's SOUND with A held, the chord picker). Both are the
    owner's question (the audit's G08 risk: the song list would scroll at 9 a second).
- **A third pace, for clicks** (gesture audit G32, 2026-11-25; `ui::kClickDelayMs` 170, `kClickEveryMs`
  125, `kStickClicksPerSecond` 8): what X held sets on a hit or a note. It was three rates written in the
  screens, "clicks, not a slide": the drum grid's pitch 8 a second, its ratchet and a lane's FEEL 4, the
  roll's ratchet and glide one each 220 ms after 200; the stick the same 8 and 4 on the drums, 4 on the
  roll. **One pace, the fastest of them**, so none clicks slower than it did: the ratchet, FEEL and the
  roll's clicks go twice as fast (FEEL's six clicks end to end in about 0.8 s, held or with the stick
  at 75%). FX's list of effects ran at a bare 200 ms; it runs at the list's 110 (it reaches its end in
  less than a second held). **The list of recordings' faster paces are named in `ui.hpp` as the one
  exception** (`kRecordings...`). CLAUDE.md's "two speeds" line is the owner's to change (changed in
  C4-docs, next).
- **The buttons' standards, as the gesture batches left them** (CLAUDE.md, gesture audit C4-docs,
  2026-11-25, under the owner's "any of my decisions can be reversed if there is a more logical method",
  2026-11-24). The standards are what a new screen copies, so they follow what the batches built; each
  line's reasoning is in the batch's own note, named here.
  - **"A held D-pad has two speeds, in `ui.hpp`: `kCursor...` for cursors, `kList...` for lists and
    choices"** became three paces through `ui::Repeat` (Cursor, List, Click), the recordings list named
    as the one exception, and the stick's climb on a cursor (G08, G32, G07 above). A new screen written
    to "two speeds" would have hand-rolled its own repeat and clicked at a cursor's or a list's pace.
  - **"Hold B deletes for good"** became "Hold B deletes", with the two lengths: since G29 the half-second
    holds (an effect, a MOD, a scene emptied, EDIT's and VOICE's every box) are undone by SEL + B, so
    "for good" was wrong for them, and the bar's length is what says which kind it is.
  - **"Hold Y + a direction moves a line where R1 is taken"** is new (gesture audit G37). Carrying is hold
    R1's job, but on FX R1 is the track key (owner, 2026-11-14, above), and the move there had been hold
    Y + UP / DOWN since ff7080c with no standard: a new screen with R1 taken now has one gesture to reuse.
    It says "where R1 is taken" because on the drum grid hold Y + a direction is already the exact chance,
    a hit's slice and the lane dice (`PatternScreen::nudgeExact`), and the roll's hold Y is the chord.
  - Brought in line, each from its own batch: L2 / R2 go round and a long list goes round on a press (G35);
    SELECT's combos on every tab (G03); the copy stays after a paste, on SONG and the grids (G23); a stick
    move is a use of L1 (G31); B during a held pick puts back what it found (G12, G26; the mixer's reset
    stays, owner 2026-11-14); B on a setting is its usual value, a synth page's still goes back (G17);
    one set of name keys with every button theirs (G01), Y held deleting on (G48); X heard as long as held,
    over the song (G33).
- **The list of instruments stands higher when it must** (`SongScreen::listTop`): six rows and what
  goes said under them (another instrument for a track) reached past the hints bar from `kListY`
  (70), so the box rises to end `kListBottom` (8 above the bar); adding a track it stands at 70 as
  before.
- **Hints must fit.** The hints bar drops what does not fit; do not rely on
  that, shorten the words. The bar draws `HOLD` as `HLD`, `SELECT` as `SEL`,
  directions as arrows, and the one word `dice` as a die (`ui.cpp`): write
  hints in full. `ui::gHintsOver` says how many pixels the last bar was
  short of holding every hint and the help: `app_test`
  (`testTheHintsFitAndSayWhatTheyDo`) holds the SONG screen and the drum grid
  to 0 with either help (pit stop, 2026-11-14: SONG's cell leaves mute and
  solo to the head, the drum grid lists a hit's own jobs on a hit and the
  brushes on an empty step). JAX-EP's MAIN and five pages are held to 0 at
  every stop, A held and not (`testJaxepsHintsFit`). **The piano roll on a note is still 48 to 69
  pixels over** (its help is not drawn): not mended.
- **The help card's B line reads "delete, empty, or go back. X held: empty the effects"** (gesture
  audit G11, 2026-11-24): X + B empties what X shows on a hit, a lane's name, a note and a chord,
  and no held strip has room for its chip (next rule). It fits the card at every size (held at LARGE
  at XL, as the card's other lines); `testTheHelpCardDrawsSel` reads it.
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
  controls.txt, which stays SELECT). The web pages, README and the manual keep
  the full word. `ctest -R words` (`tests/words_test.sh`) reads the sources and fails
  on a string that would show it spelled out; it plants its own faults at the
  end and must catch each (add a plant with a new rule). **It fails erase, remove and rub out
  too** (any form; gesture audit G10, 2026-11-24): the word is delete, a cell is emptied.
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
    anything written over a little picture (a
    recording's seconds in the list), the heads of narrow SONG columns, the
    controls setup. The tests that read a strip or a bar off the screen
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
  DRACULA, NORD, GRUVBOX, TOKYO, MONOKAI dark, SOLARIZED, GITHUB light: the
  owner, 2026-11-23, "more like common terminal or IDE themes"; they replaced
  BLUE, GREEN, BLACK, PAPER and GREY and took their numbers; `kThemeTable` in
  `ui.hpp`, `kDarkThemes` the first six; kept as `theme N` in settings.txt, no
  line reads BROWN, which is the look as built to the number). Each new one is
  its namesake's colours moved in lightness, hues kept, until it met
  `theme_test`'s rules and stood apart from the others (a script, not by hand:
  the rules are below). `ui::kBg` ... `ui::kStop` are
  references into `ui::gTheme`, which `App::draw` (and main.cpp, before the
  controls setup) sets from `Session::theme()` each frame. A theme changes
  colours only, never a size or a place.
  - **A new colour goes through the ten names (or `faded()` of one): never
    numbers of its own.** Each name is a JOB (`ui.hpp` says which), kept in
    every theme; pick by job, not by hue.
  - **A new mark is checked on a light theme** (`--theme solarized`; the tests' light theme is `kThemeSolarized`, PAPER's before). A light
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
    new alpha given to `faded()` goes in its list there. **A drum hit's body (its CHANCE: any alpha)
    and the play line's glow over anything are in it** (2026-11-17: the stills play the demo, so the
    glow lands where the clock puts it, and over a ghost hit it failed now and then); a new
    translucent thing that moves with the clock is added the same way. A still in a theme:
    `--theme solarized`.
- **The SET page is five groups under headings, and shows as many rows as fit** (owner, 2026-11-17:
  "enough rows now"; `SettingsScreen::Row`, `groupOf`, `groupName`): SONG (TEMPO, CHORD PACK, KEY,
  SCALE, TRACK NAME, BOUNCE), PLAYING (CLICK, COUNT IN, MUTES, USB KEYBOARD), LOOK (TEXT SIZE, THEME, PIANO
  ROLL), HANDHELD (VOLUME, BRIGHTNESS, STICK LIGHT, LIGHT COLOUR, STICK SPEED), SETUP (WIFI PAGE,
  NETWORK, BUTTONS, CONTROLS). The heading of the first row in view stands above the rows (`kHeadingY`); every
  other heading in view takes `kHeadingGap` before its group's first row. So **twelve rows are in
  view with one heading among them, eleven with two, ten with three** (`placeY`, `fits`, `kPageFoot`;
  `kRowsShown` 12 is the most). It moves one row at a time (L1 / R1 a heading, and a press at either end goes
  round: G28, G35 above), by as little as shows the cursor, and
  leaves no room empty under the last row. **The page counts PLACES, not rows** (`here`, `placeOf`,
  `rowAt`; `firstShown` is a place): a row that is not on this handheld is passed by. **A group's
  first row must always be here** (a heading stands on it): only BRIGHTNESS, STICK LIGHT, LIGHT
  COLOUR and NETWORK can be missing. A new row costs its place in `Row` (the numbers are kept nowhere: a setting
  is kept by its word in settings.txt) and its group in `groupOf`. An UP arrow after the top heading
  and a DOWN arrow after the last heading in view say rows are out of sight.
- **The METRONOME is added after Catch and before the volume, and never reaches a bounce**
  (easywins batch 2, `docs/plans/easywins-plan.md`; `Command::SetClick`, `Engine::addClick`). Off
  with nothing sounding it adds nothing: the output is as it was, to the bit. Only the live engine
  is told (`Session::syncClick`); a bounce's engine is its own. **The COUNT IN is `Play`'s `d`**:
  the Play waits aside (`countPlay_`) and `startPlaying` runs where the count's last step ends, on
  its sample. The Play is counted as taken when it ARRIVES (`playsTaken_`), so a count called off
  by Stop still agrees with the app's count of Plays (AT END STOP). Anything new that starts the
  song goes through `startPlaying`; anything that stops it calls the count off (`stopPlaying`, Panic).
- **BRIGHTNESS and STICK LIGHT are the program's only where the system says so**
  (2026-11-17, `app/lights.hpp`, `docs/plans/lights-plan.md`, `tests/lights_test.cpp`):
  `APOTHECA_OWNS_SCREEN=1` and `APOTHECA_STICK_LIGHT=/dev/ttyS2`, set by our
  firmware's `run`, never by the port. Elsewhere nothing is written and the two
  rows are not shown. **`bl_power` is apotheca-power's, never written here** (the
  kernel keeps a screen at bl_power 4 dark whatever `brightness` says). The ring is
  a GPIO switch (`rgb:kbd_backlight`, on/off) plus a controller on UART5 that takes
  ROCKNIX's 51-byte frame; every file is written only when what it should say
  changes, on the app thread (`Session::update`). MENU + a volume key is the
  brightness (`App::volumeKey`). **The owner has seen ON and BEAT work** (2026-11-17).
  **STEPS goes round once a SCENE** (owner: once a bar it "completes all the steps before the scene
  is up"): `RingPlace` carries the scene's step and its length (`Project::sceneSteps`), the lit LED is
  the share played, brass on a bar's first step; a one-bar scene is as it was. **LIGHT COLOUR**
  (`LightColour`, `ringColours`; a row shown only where STICK LIGHT is; `lightcolour N` in
  settings.txt, left out at THEME): premade colours, TRACK (twelve hues, the track up) and RAINBOW (a
  hue a LED, turning a LED a beat). **The owner has seen the colours and likes them** (2026-11-17).
  **THEME's colour is picked for the ring, theme by theme** (`ringThemeColour`: BROWN's is BRASS, the
  owner's word; the other five are unseen picks; a new theme must be given one, a `static_assert`
  counts). A bar's first step is brass, or white where the ring is too near brass (`ringFirstFor`).
- **NETWORK: our firmware's WiFi is joined on the SET page** (2026-11-18, `docs/plans/firmware-plan.md`
  section 14 on branch `firmware`; `app/wifi.hpp`, `tests/wifi_test.cpp`). The row is under WIFI PAGE,
  **only where `APOTHECA_WIFI` names the system's `apotheca-wifi`** (`Session::wifiHere`; the port: no row,
  nothing runs). **Every call is on the `Wifi`'s own thread, one at a time in the order asked** (no frame
  waits; a status or a scan already waiting is not asked twice); it touches only its own answers, under
  its lock, and the app thread reads a copy (`now()`). `Session::update` asks the status every second
  while the page is on, on every screen, and a scan at once and every 10 s while the row is up
  (`wifiLooking`). **The password goes on the program's stdin, never its words, never a log**, and is
  written over after. Hold A + UP / DOWN picks from the list as it was when A went down (a scan landing
  meanwhile moves nothing), let go joins: saved or open at once, else the keys in their PASSWORD mode
  (`passwordButton`: three sets of forty, X the next set, 8 to 63; the track name's keys are as they
  were). Hold B a second forgets (`kForgettingWords`). **`status`'s `joined NAME ADDRESS [HOSTNAME]` is
  read from the right** (a name may hold spaces); the hostname (`apotheca.local`, `apotheca-2.local`)
  is said beside the address, never instead (`Session::transferName`: the WIFI PAGE row in the medium or
  small of LARGE, the list of songs on a line of its own with one row fewer). A still pretends it all
  (`Session::pretendWifi`): `setup-network`, `-held`, `-keys`, `-forgetting`, `-long`, `-songs`. **The
  system side is built** (2026-11-18, branch `firmware`: `/usr/sbin/apotheca-wifi` on wpa_supplicant,
  udhcpc and avahi; `qemu-test.py` joins made-up radios). **Not run on a handheld.**
- **FOLLOW: the drum grid, its slices and the roll show each bar as the playhead comes into it**
  (owner, 2026-11-18; `app/follow_bar.hpp`, one rule for both grids, `tests/follow_test.cpp`): only
  while the song plays the scene on screen; never while a button is held or the stick pushed, nor
  `kFollowRestMs` (1.5 s) after, so a bar looked at by hand stays until the playhead next enters a bar.
  The cursor keeps its step.
  **The roll's keys follow the notes too** (owner, 2026-11-24; `keysToShow` in follow_bar.hpp, `NoteScreen::followTheNotes`):
  when FOLLOW shows a bar, the keys go to the stretch holding the most of its notes (then the most of their length,
  then centred on them, then nearest where they were); a bar with none leaves them; the cursor, if left out of view,
  goes to the nearest key shown. Read only (the const `pat()`: the writable one makes a scene's piece).
  `follow_test` `testTheKeysRule`, `testTheRollsKeys`.
- **SCALE's OFF reads CHROMATIC** (owner, 2026-11-18: "no key" was not seen as there; `scaleName`):
  the number in songs is unchanged.
- **The one colour on the screen that is not the theme's: a track's ring colour on its SONG head**
  (owner, 2026-11-17: "a little color identifier to the tracks"; a deliberate exception to "a colour
  goes through the ten names"). Only while LIGHT COLOUR is TRACK on a ring that is here and lit
  (`Session::trackColoursShown`): a bar `kHueMarkH` high along the head's foot in
  `ringTrackColour(track)`, a dim line over it for an edge on a light theme, drawn AFTER a muted
  head's dark wash (so it is flat and as bright), clear of the M / S and mixer marks at the top.
  `themeviews`' `Explain` knows the twelve hues as flat colours; `lights_test` `testTrackColours`
  reads the heads in BROWN and PAPER at 2 and 12 tracks. Not on the top bar's track name (no room
  that is not a tight row). Anything else that wants a track's colour asks `ringTrackColour`.
  A row's bar starts after its value's words and ends short of what is said
  at the right, at every text size (`SettingsScreen::barPlace`: BOUNCE's
  WORKING 37% / TAIL 12 S; VOLUME's and STICK's stay at 250, 110 wide).
- **A check of what is DRAWN uses `tests/canvas.hpp`**: a 640x480 screen in
  memory with the program's lettering. `ui_test` looks at the hints bar's
  pixels and `app_test` at the help card's boxes; a check of the text alone
  passed with the bar drawing the key as written.
