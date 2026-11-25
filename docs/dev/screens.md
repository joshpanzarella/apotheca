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
  `kRowsShown` 12 is the most). It moves one row at a time, by as little as shows the cursor, and
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
