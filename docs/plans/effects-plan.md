# Effects plan

Written 2026-11-11. Steps 0, 1a to 1c (sends), 4 to 8 (TAPE, HALL, CHORUS, COMP, SATURATE) are built and joined (2026-11-12); see "as built" under the order of work, and "Joined" at its end for what changed when they met (the buses start with TAPE and HALL, a bus's LEVEL is the engine's, COMP's meter is on the screen). THROW (2, 3) and DUCK (9) are built and joined too (2026-11-12; "Steps 2 and 3 as built", "Step 9 as built", and "THROW and DUCK joined" for where they meet).

## The recommendation

**Build two shared send buses first (ECHO and SPACE), then six new effects.**
The buses are what THROW needs, they make one good reverb serve twelve tracks,
and they cost about 2.5% of one handheld core.

| Question | Answer |
| --- | --- |
| First set | **TAPE** (dub delay), **HALL** (reverb), **CHORUS** (with vibrato and flange), **COMP**, **SATURATE**, **DUCK** |
| Later set | PING, BLOOM, PHASER, TREMOLO, LIMITER, WEAR, EQ, SNAP, STUTTER |
| Sends | **Yes, two.** ECHO holds a delay, SPACE holds a reverb. Every track has an amount to each |
| THROW | A hit or a note opens its send for itself alone. Hold X, press **A** |
| Insert slots | Tracks go from 2 to **3**. The master stays at 2 |
| Settings per effect | **Stay at 4.** One knob that moves several things (AGE, SPEED) is faster on a handheld than a fifth knob |
| Cost | Every first-set effect is **under 1% of a handheld core** (estimated). A full song's effects cost less than one Twin Analog track |
| Old songs | Load and sound the same. Nothing is renumbered; the five old effects are not touched |
| First thing to build | Step 0 below: the effects test bench and the effect table, so every later step is measured |

**What is measured and what is not.** Desk timings below are measured, on this
machine, with plain versions written for this plan (not in the repo). Handheld
figures are those times 5: **an estimate**. Nobody has heard any of it.

## 1. The list

"Wet" is the processed sound, "dry" the untouched one. MIX at 0 stays "the
sound untouched", as today.

### First set

| Effect | What it is | Settings | What makes it good |
| --- | --- | --- | --- |
| **TAPE** | A dub delay: each repeat duller, thinner and a little bent | TIME (8 choices, in time), FEEDBACK, AGE, MIX | The echo is read between samples (cubic), so TIME **glides** and the pitch swoops as tape does. A low cut, a high cut and a soft clip sit **inside the loop**, so FEEDBACK can pass 100% and howl without blowing up. AGE turns up wow (slow pitch drift), the dulling and the clip together |
| **HALL** | A reverb with a dense, moving tail | SIZE, DECAY, TONE, MIX | Eight delay lines feeding each other (an FDN), four smearing stages a side before them, two lines slowly bent so the tail does not ring on one note. DECAY is a real time: 0.3 to 20 s, set by formula. TONE damps highs and trims lows in the tail |
| **CHORUS** | Copies of the sound, slightly late and wavering | RATE, DEPTH, TYPE (4 choices), MIX | TYPE: CHORUS (3 voices a side), ENSEMBLE (3 voices, three speeds), VIBRATO (wet only, one voice), FLANGE (short, with feedback). Cubic reads, so no zipper noise. Left and right waver out of step for width |
| **COMP** | A compressor: turns loud parts down, then the whole up | THRESHOLD, RATIO, SPEED, MIX | Level is measured in dB with a soft knee, left and right linked, attack and release in the dB domain. Makeup gain is automatic. The detector ignores below 60 Hz so a kick does not pump everything. SPEED runs from fast (1 ms / 50 ms) to slow (30 ms / 400 ms). MIX gives parallel compression. **In a master slot this is the glue compressor** |
| **SATURATE** | Clean drive, four characters | DRIVE, TYPE (4 choices), TONE, MIX | Run at **twice the sample rate**, so the harsh fold-back (aliasing) drops from 22 dB under the harmonics to 54 dB (measured). TYPE: TAPE (soft), TUBE (lopsided, adds even harmonics), FOLD (wavefolder), HARD. Loudness is held level as DRIVE rises |
| **DUCK** | The track dips each time the kick hits | AMOUNT, RELEASE, SOURCE (lane 1 to 8) | Keyed by the kick's **trigger**, not its sound: no lag, and it works with the kick muted. SOURCE is a lane of the first drum track |

The old DELAY, REVERB and DRIVE stay exactly as they are, under the same
names. Old songs depend on them.

Two facts measured about what is there now:

| Thing | Measured |
| --- | --- |
| DRIVE at 81%, a 4.2 kHz sine | Aliasing only **17 dB** under the harmonics. It is rough up high |
| REVERB, SIZE 0 / 60 / 100% | Tail of 0.6 / 1.5 / **10.8 s**. 130 distinct echoes in the first 100 ms; the 8-line version gives 566 |

### Later set

| Effect | What it is | Settings | Note |
| --- | --- | --- | --- |
| PING | Clean echo bouncing left and right | TIME, FEEDBACK, TONE, MIX | TAPE's loop without the wear. Small |
| BLOOM | HALL with an octave-up fed back into the tail (shimmer) | SIZE, DECAY, SHIMMER, MIX | Measured: 0.1% more than HALL. The pitch shifter is the simple kind; ears decide if it is good enough |
| PHASER | Six sweeping notches | RATE, DEPTH, FEEDBACK, MIX | |
| TREMOLO | Level or pan moved in time with the song | RATE (choices), DEPTH, SHAPE, PAN | SHAPE: sine, square, ramp (a pump with no kick), **random steps** |
| LIMITER | A hard top for the master, seeing 3 ms ahead | GAIN, RELEASE | Sits before the existing soft ceiling, which then never acts. Delays the output 3 ms |
| WEAR | Tape and vinyl: wow, flutter, hiss, dropouts | WOW, DIRT, DROPS (a chance), TONE | Dropouts come from a seeded dice, so a bounce repeats |
| EQ | Three bands | LOW, MID, HIGH, MID FREQ | |
| SNAP | More or less attack on drums | ATTACK, SUSTAIN, MIX | Two level followers; cheap |
| STUTTER | Catches the last slice and repeats it, by chance | LENGTH (choices), CHANCE, DECAY, MIX | Dice on an effect |

### Not worth doing on this device

| Thing | Why not |
| --- | --- |
| A fifth setting | The row holds EFFECT plus four boxes, and lanes are `8 + slot * 4 + setting`. If an effect needs more, it becomes two effects |
| Saturation at 4x | Measured: 89 dB instead of 54, but about **5% of a handheld core each** |
| A 16-line reverb | Measured: 1.3% against 0.8%, and no more echoes in my count |
| Convolution reverb, anything FFT-based | Several percent each, added delay, and files to manage |
| A compressor keyed by another track's **sound** | That track would have to be rendered first, which breaks rendering tracks side by side. DUCK by trigger does the job |
| A filter that wobbles in time | The per-track LFO is next on the list anyway. Aim it at FILTER CUTOFF |
| Multiband compression | A lot of settings for a small screen |

## 2. Structure

### Sends

**Two buses: ECHO and SPACE.** Each holds one effect, running wet only. Every
track has two amounts, ECHO and SPACE, both 0 to start.

```
track: instrument -> slot 1 -> slot 2 -> slot 3 -> level, mute -+-> mix
                                                                |
                                          x ECHO amount  -------+-> ECHO bus  -> its effect -+
                                          x SPACE amount -------+-> SPACE bus -> its effect -+-> mix

mix -> master slot 1 -> master slot 2 -> ceiling -> out
```

- Sends are taken **after** the track's level and mute. Muting a track stops
  what it feeds; the echoes already in the bus ring on.
- The buses join the mix **before** the master slots, so a master COMP glues
  them in with the rest.
- ECHO may hold DELAY, TAPE or PING. SPACE may hold REVERB, HALL or BLOOM. A
  new song starts with TAPE and HALL.
- On a bus, the effect's MIX box reads **LEVEL**: how loud the bus comes back.
  `Effect` gains one call, "you are on a bus", which drops the dry sound.

**A bus against a reverb on every track** (handheld, estimated):

| | Twelve HALLs | One SPACE bus |
| --- | --- | --- |
| CPU | 9% of a core | 0.8%, plus 0.45% to feed both buses |
| Memory | 3 MB | 0.25 MB |
| Sound | Twelve rooms | One room, which is what makes a mix sit together |
| THROW | Not possible | Falls out of it |

A reverb in a track's slot is still allowed.

### How it fits the helper threads

Nothing changes while tracks render. After the helpers finish, the audio
thread, **in track order**:

1. adds each track to the mix (as today),
2. adds each track times its send amount to the two buses,
3. runs the two bus effects,
4. adds the buses to the mix, then the master slots.

The order is fixed, so the output stays the same to the last bit with any
number of helpers. Steps 2 to 4 run on one core; they total about 2.5% of it.

A bus that has been fed nothing for longer than its tail is skipped, so an old
song pays nothing for buses it does not use.

### THROW

THROW is a value on a hit or a note: **OFF, ECHO, SPACE, BOTH**. Full send,
no amount.

| On | How | What it costs |
| --- | --- | --- |
| A drum hit | The drum machine writes thrown hits a second time into a small "throw" buffer for the track; that buffer goes to the bus. **Only that hit is thrown**, even with other lanes sounding | `DrumMachine::render` gains a second output. The thrown hit reaches the bus before the track's own slots (it is tapped inside the instrument) |
| A note | The track's send opens for the note's length, with 5 ms ramps | Other notes sounding on that track at that moment are thrown too. The synths do not hand out single voices |

### Slots, and where the compressor goes

- **Tracks get a third slot.** With delay and reverb moved to buses, three
  slots cover filter + drive + one more. It fills the fourth row of the track
  page (below).
- **COMP is an ordinary effect.** Put it on a track or the master. No special
  master-only path. The master's two slots are then COMP and LIMITER by habit,
  not by rule.
- **Swapping an effect fades** over 10 ms. Today the new one goes in at once
  and the old tail is cut, which can click.

### DUCK

Before the helpers start a chunk, the engine already knows which lanes of the
first drum track fire in it. It writes that down (lane, sample offset) where
every DUCK can read it. Each DUCK keeps its own envelope: full dip at the hit,
back up over RELEASE. No track waits on another.

## 3. Cost

Stereo, 48 kHz, 64-frame blocks, `-O2`. **Desk: measured. Handheld: desk
times 5, an estimate.** The delays and reverbs read a lot of memory and the
handheld's cache is small, so those two may come out worse than 5.

| Effect | Desk, % of a core | Handheld, estimated |
| --- | --- | --- |
| FILTER (today) | 0.03 | 0.2 |
| DRIVE (today) | 0.08 | 0.4 |
| DELAY (today) | 0.02 | 0.1 |
| REVERB (today) | 0.07 | 0.3 |
| CRUSH (today) | 0.01 | 0.1 |
| TAPE, with the clip in the loop | 0.16 | **0.8** |
| PING (TAPE with no clip) | 0.06 | 0.3 |
| HALL, 8 lines, 2 bent | 0.15 | **0.8** |
| BLOOM | 0.17 | 0.9 |
| a 16-line reverb | 0.26 | 1.3 |
| CHORUS, 3 voices a side | 0.13 | **0.6** |
| VIBRATO or FLANGE type | 0.05 | 0.3 |
| PHASER | 0.07 | 0.3 |
| TREMOLO | 0.02 | 0.1 |
| COMP, quick log maths | 0.05 | **0.25** |
| COMP, exact log maths | 0.07 | 0.35 |
| LIMITER | 0.04 | 0.2 |
| DUCK | 0.01 | **0.1** |
| SATURATE at 2x, cheap curve | 0.18 | **0.9** |
| SATURATE at 2x, real `tanh` | 0.47 | 2.4 |
| saturation at 4x, real `tanh` | 1.00 | 5.0 |
| WEAR | 0.15 | 0.7 |
| Feeding 2 buses from 12 tracks | 0.09 | 0.45 |

Two things the numbers decided:

- **`tanh` is the cost in saturation, not the oversampling.** SATURATE's four
  curves must be cheap ones (a ratio of small polynomials). Same aliasing,
  under half the time.
- **Trying to fix aliasing without oversampling is not enough.** The cheap
  trick (ADAA) measured 30 dB, against 54 dB at 2x.

### Budget

Instruments on the handheld, from CLAUDE.md times 5: drums 0.5%, Phazed 4.5%,
QMOD 13%, Twin Analog x1 23.5%, x2 45%.

| A 12-track song | % of one handheld core |
| --- | --- |
| 2 drums, 4 Phazed, 3 QMOD, 3 Twin x1, all voices sounding | 128 (about 43 on each of three cores) |
| Two of three slots used on every track, typical effects | 12 |
| Both buses, master COMP and LIMITER | 2.5 |
| **Worst case: all 36 slots hold SATURATE** | 32 |

**All the effects in a normal song cost less than one Twin Analog track.**
There is no need for a limit on how many of each. The instruments set the
ceiling, as now.

**Denormals** (numbers so small the processor crawls): eight fading filter
states ran **30 times slower** than zeros on the desk machine. Today's effects
snap small values to zero by hand, once a block. Reverbs with many states
should not rely on that: set the processor's flush-to-zero flag on the audio
thread and every helper (`useWorkers` already has a start hook). The cost on
the handheld's processor is not measured.

## 4. Compatibility

**Rule kept: ids are appended.** In a song file an effect is a word
(`fx 1 delay ...`), in memory and in commands a number.

| Thing | Today | After |
| --- | --- | --- |
| `EffectKind` | None, Filter, Drive, Delay, Reverb, Crush | The same six, then Tape, Hall, Chorus, Comp, Saturate, Duck, then the later set as built |
| Effect words in files | `filter drive delay reverb crush`, taken from the screen name | `EffectInfo` gets its own `word`, so a screen name can change and the file word cannot. One word, no spaces |
| Order in the picker | By number | By a separate list order, grouped. The number is never the order again |
| Settings per effect | 4 | 4 |
| Automation lanes | 16: 0..7 macros, 8..15 slots 1 and 2 | **24**: 16..19 slot 3 (the same `8 + slot * 4 + setting`), 20 ECHO send, 21 SPACE send, 22 and 23 spare |
| Track slots | 2 | 3 (`kTrackSlots`); master 2 (`kMasterSlots`). The one constant `kEffectSlots` splits |
| `Command::Type` | | `SetSend` appended. Bus effects use the existing `SetEffectParam` and `InstallEffect` with two new "track" numbers beside `kMasterTrack` (-2 ECHO, -3 SPACE) |

New words in a song file. An older program skips every one of them and still
loads the song, without the new parts:

| Line | Where | Means |
| --- | --- | --- |
| `fx 3 NAME ...` | in a track | the third slot. (An older program rejects slot 3 and moves on) |
| `send BUS AMOUNT` | in a track | BUS 1 ECHO, 2 SPACE. Left out when 0 |
| `busfx BUS NAME ...` | before any track, by the master's `fx` lines | the bus's effect and settings |
| `stepfx ... CUT THROW` | as now | a seventh number, 0 to 3. Left off, it is 0, so old songs read right |
| `note ... NUDGE THROW` | as now | a sixth number. NUDGE is written whenever THROW is |
| `auto SCENE LANE ...` | as now | lanes 16 to 23 now load. An older program skips them |

Things to watch:

- **A song with no `busfx` lines is an old song.** Its buses get TAPE and
  HALL, its sends are 0, so it sounds as it did.
- **An old song saved by a new program is unchanged** apart from the two
  `busfx` lines.
- **A new song opened and saved by an old program loses the new parts.** That
  is the rule already ("unknown lines are skipped"); worth a line in the manual.
- `Project` grows about 0.8 MB (8 more lanes on 12 tracks), the engine's
  automation the same. It was never to go on the stack and still is not.
- `SetStepFx` has no spare field in `Command`. THROW rides with `cut`.
- The old effects' maths is not touched, not even to fix DRIVE's aliasing. A
  test holds each of the five to a saved recording, sample for sample.

## 5. Controls and screen

Nothing here is a new gesture. A holds and turns, B empties, X hears, Y is the
screen's special.

### The FX screen: two pages, same rows

> **Replaced 2026-11-12.** The owner did not find the second page ("I just
> didn't think to scroll down to the EVERYTHING page") and picked the "two
> columns" design: a list of all eight lines always in view on the left, the
> line picked drawn large on the right, no pages and no Y. The same day DELAY
> and REVERB became **families with a TYPE** (CLEAN, TAPE; ROOM, HALL), each
> type still its own effect underneath; PING, SPRING and PLATE will be new
> types. The group jump went with the blind list (hold A now opens the list
> of effects, and left / right is the TYPE). What follows in this section is
> the two-page screen as it was built. The rules now: `CLAUDE.md`, "The FX
> screen is two columns" and "A family is only how the list is shown".

Today: four rows. After: **two pages of four rows**, drawn with the same boxes.
**Y** flips the page. Moving down off the last row also goes to the next page.

```
 BASS / EFFECTS                                                TRACK page
 BASS, EFFECT 1
 [EFFECT  ] [CUTOFF ] [RESONANCE] [TYPE     ] [MIX   ]
 [FILTER  ] [810 HZ ] [20%      ] [LOW 24 DB] [100%  ]
 BASS, EFFECT 2
 [SATURATE] [DRIVE  ] [TYPE     ] [TONE     ] [MIX   ]
 BASS, EFFECT 3
 [NONE    ]  Nothing here. Hold A and push up to put an effect in.
 BASS, SENDS
 [ECHO 35%] [SPACE 10%]
 HOLD A turn   B default value   Y buses   START play
```

```
 EVERYTHING / EFFECTS                                          ALL page
 ECHO (WHAT THE TRACKS SEND)
 [TAPE    ] [TIME   ] [FEEDBACK ] [AGE      ] [LEVEL ]
 SPACE (WHAT THE TRACKS SEND)
 [HALL    ] [SIZE   ] [DECAY    ] [TONE     ] [LEVEL ]
 MASTER (EVERYTHING), EFFECT 1
 [COMP    ] [THRESHOLD] [RATIO  ] [SPEED    ] [MIX   ]
 MASTER (EVERYTHING), EFFECT 2
 [NONE    ]
 HOLD A turn   B default value   Y track   START play
```

| Job | Buttons |
| --- | --- |
| Pick an effect | Hold A + up / down, as now |
| **Jump a whole group** (FILTER, DRIVE, DELAY, REVERB, MOVING, LEVEL, DIRT) | Hold A + **left / right**. With 11 effects, stepping one at a time is too slow |
| Turn a setting | Hold A + stick; D-pad for a twentieth. Unchanged |
| Choices (TYPE, TIME, SOURCE, RATIO) | Click, once a push. Unchanged |
| A send amount | Hold A + stick or D-pad, on the SENDS row. B puts it back to 0 |
| Automate a send | As any setting; it shows the purple line |
| The other page | Y |

COMP wants its gain reduction seen. Proposal: the THRESHOLD box's bar shows
it while sound passes, as an automated setting already shows its moving value.
No new part on the screen.

### THROW on a drum hit

Hold X on a hit, as now. Up / down, left / right, L1 and R1 are taken.
**A steps THROW round: OFF, ECHO, SPACE, BOTH.** (Owner, 2026-11-11: A, not
Y. X and A sit side by side under one thumb; X + Y felt odd.)

Hints while X is held: `UP / DOWN pitch  LEFT / RIGHT ratchet  L1 reverse
R1 cut  A throw`. Five; check it fits with a still. A alone places a hit on
this screen, so the check for X held must come first. Hold X + Y is the dice (built).

A thrown hit is marked on the grid the way the other step effects are.

### THROW on a note

Hold X on a note sets its ratchet and glide with the directions. **A is THROW
there too.**

## 6. Testing without ears

One new test program, `tests/effects_test.cpp`, with shared tools: an impulse,
a sine, a spectrum, a tail timer and a click finder. All of these were tried
for this plan and gave sane numbers.

### Every effect

| Check | How | Pass |
| --- | --- | --- |
| MIX at 0 | compare with the input | the same, to the sample |
| No clicks on a turn | sweep each setting end to end under a 100 Hz sine | no jump between samples larger than the sine's own, plus a margin |
| No clicks on a swap | change the effect under a sine | the same |
| Stays bounded | every setting at each extreme, loud noise in, 30 s | never NaN, never over a stated peak |
| Silence in, silence out | silence for longer than the tail | exact zeros, and no slower than at the start (the denormal check) |
| Same on any helper count | a song with sends and throws, 0 to 3 helpers | the same to the last bit |
| Repeats | render twice from fresh | the same to the last bit (dice are seeded) |
| Cost | a line each in `bench` | within the table above |
| Old five | against a saved render | the same to the last bit |

### Each effect

| Effect | Measure | Pass |
| --- | --- | --- |
| TAPE, PING | an impulse: when each echo lands, and how much quieter | TIME within a sample of the step length at three tempos; each repeat down by FEEDBACK within 0.5 dB at AGE 0. PING: echoes swap sides |
| TAPE | a sine in, AGE up: track the pitch | wow rate and depth as written down; FEEDBACK past 100% holds a steady level |
| HALL, BLOOM | an impulse: tail time (RT60), echoes in the first 100 ms, left against right | RT60 within 10% of DECAY at five settings; over 400 echoes; left and right nearly unrelated |
| BLOOM | a 440 Hz burst: is 880 Hz in the tail | present, and rising with SHIMMER |
| CHORUS | a sine: pitch wobble rate and depth | within 5% of the reading on screen; VIBRATO has no dry sound |
| COMP | steady sines at eleven levels; a level step up, then down | the curve within 0.5 dB of threshold, ratio and knee; attack and release times within 20% |
| DUCK | fire the source lane | dip of AMOUNT at the hit's sample, back up in RELEASE; nothing from other lanes |
| LIMITER | loud drums | never over the top; 3 ms late, exactly |
| COMB | an impulse: where it rings, how long; noise and a sine on a harmonic | within 3.5 cents of the note at every TONE (and an octave down below 0); rt60 as written; noise level within 0.5 dB to FEEDBACK 50%, 4.5 dB at 100% |
| VOWEL | sines at each vowel's peaks and between; a saw and noise | each first peak +10.9 dB and 6 dB over the place between; SHIFT moves it by the octave; half way A-E at 490 Hz; a saw 2.7 to 7.6 dB down at any RESONANCE |
| EQ | sines at 40 Hz to 15 kHz | each band's table within 0.1 dB; every band at 0 dB the sound to the bit |
| SATURATE | 4186 Hz sine, 20 dB of drive, a spectrum | everything that is not a harmonic at least **50 dB** under the harmonics (measured now: 54). Level steady within 1 dB across DRIVE. TUBE has even harmonics, TAPE has none |
| Sends | one track, send at half | the bus gets half; a muted track sends nothing; lanes 20 and 21 move it |
| THROW | one thrown hit among others | the bus holds that hit and nothing from the step before or after |

**Mutation testing, as the house rule says**: after each passes, break the
thing it names (wrong line length, feedback sign, a missing ramp) and see it
fail.

One measure did **not** work: "how much does the tail ring on single notes"
gave 11 to 14 dB for the old reverb and the new alike. Whether HALL sounds
metallic is for ears.

### For the owner's ears

1. HALL against the old REVERB: smoother, or just different? Is the slow bend in the tail heard as pitch?
2. TAPE: is the swoop on a TIME change right? Does AGE at full go too far?
3. SATURATE: do the four types differ enough to keep four?
4. COMP: does the automatic makeup keep the loudness about level? Is SPEED enough, or are attack and release missed as two knobs?
5. BLOOM: is the simple octave-up good enough, or grainy?
6. THROW on a drum hit reaches the bus without the track's own filter and drive. Does that matter?
7. Any click, anywhere, when turning or swapping.
8. **Does the handheld keep up?** Someone runs `bench` on the device once after step 0. Every handheld number here is a guess until then.

## 7. Order of work

| Step | What | Size | Needs | Mostly touches |
| --- | --- | --- | --- | --- |
| 0 | **DONE 2026-11-11, but for one switch (see below).** **Groundwork.** `EffectInfo` gets `word`, group, list order and its own "how a setting reads" function; effects move to one file each under `engine/src/fx/`; `effects_test` with the tools; every effect in `bench`; flush-to-zero; the 10 ms fade on a swap; the saved-render test for the old five | medium | | `effects.hpp/.cpp`, `fx_screen.cpp`, `bench.cpp`, tests |
| 1a | **DONE 2026-11-11 (see below).** **Sends in the engine**: buses, `SetSend`, bus effects, lanes to 24, `kTrackSlots` 3 | large | 0 | `engine.hpp/.cpp` |
| 1b | **DONE 2026-11-11 (see below).** Sends in the song file and Session: `send`, `busfx`, `fx 3` | medium | 1a | `song_file.cpp`, `session.cpp`, `project.hpp` |
| 1c | **DONE 2026-11-11 (see below).** The FX screen's two pages, group jump | medium | 1b | `fx_screen.cpp/.hpp` |
| 2 | **DONE 2026-11-12 (see below).** **THROW on drum hits** | medium | 1a, 1b | `drums.hpp/.cpp`, `engine.cpp`, `pattern_screen.cpp`, `song_file.cpp` |
| 3 | **DONE 2026-11-12 (see below).** THROW on notes | medium | 1a, 1b | `engine.cpp`, `note_screen.cpp`, `song_file.cpp` |
| 4 | **DONE 2026-11-11.** TAPE | medium | 0 | `fx/tape.cpp` |
| 5 | **DONE 2026-11-11.** HALL | medium | 0 | `fx/hall.cpp` |
| 6 | **DONE 2026-11-12.** CHORUS | small | 0 | `fx/chorus.cpp` |
| 7 | **DONE 2026-11-12** (the meter went on the screen at the join). COMP | medium | 0 | `fx/comp.cpp`; the meter in `fx_screen.cpp` |
| 8 | **DONE 2026-11-12.** SATURATE, with a shared 2x header | medium | 0 | `fx/saturate.cpp`, `fx/halfband.hpp` |
| 9 | **DONE 2026-11-12.** DUCK | medium | 0, and a small hook in the engine | `fx/duck.cpp`, `engine.cpp` |
| 10+ | The later set, one at a time | small each (BLOOM, STUTTER medium) | 0; BLOOM needs 5, PING needs 4 | one file each |

**Step 0 as built:**

| Thing | Result |
| --- | --- |
| The old five | The same to the last bit after the move: 40 pinned renders and one whole song, on this machine and as ARM programs under emulation |
| Adding an effect | One new file, one `extern` line, one number, one row, one place in the list (`engine/src/fx/unit.hpp` lists the steps) |
| The fade on a swap | In. 10 ms, both effects running. No swap of the old five is rougher than the effect is by itself |
| Flush-to-zero | **ON in the program and in a bounce since 2026-11-11 (the owner's call); the tests leave it off.** As built in step 0 it was: Built, tested, and left OFF. On, it changes 1 of the 40 pinned renders (25 samples, all under 1e-38) and the end of any long tail (under 1e-30, 600 dB down). Nothing anyone could hear, but not "the same to the last bit". **The owner decides**; it is one call, `Engine::setFlushDenormals(true)`, in `app/main.cpp` and `app/bounce.cpp`. The old five do not slow down without it (measured). HALL will need it |
| Measured with the new tools | REVERB's RT60 0.59 / 1.44 / 10.8 s at SIZE 0 / 60 / 100%. DRIVE at 81%, a 4186 Hz sine: the loudest fold-back 9 dB under the third harmonic, all of it together 16 dB under the sine. None of the old five glides a setting: FILTER TYPE, DELAY TIME and CRUSH jump when turned |
| Not done | `bench` has not been run on the handheld. Nobody has heard the fade |

**Step 1a as built** (2026-11-11; only tests and measurements have seen it, nobody has heard it):

| Thing | Result |
| --- | --- |
| The buses | `kEchoBus` (-2) and `kSpaceBus` (-3), one slot each, addressed like the master. `Command::SetSend` appended. Sends are taken after level and mute and glide over 5 ms |
| Wet only | `Effect::setOnBus`. DELAY and REVERB honour it: one changed line each, off a bus their sound is still the pinned one. A new delay or reverb must do the same (`Unit::bus_`); a test fails one that does not |
| LEVEL | An effect put on a bus starts with its MIX at 100%. A send of 35% into DELAY at LEVEL 100% is then the echo an insert DELAY gives at MIX 35% |
| The rest rule | A bus sent nothing, and quieter than 120 dB under full scale, for **10 s** is cleared and skipped. Measured longest silence between echoes: DELAY 3.0 s (a half note at 40 BPM), REVERB 0.26 s. A bus never sent to is never run |
| No sends, same bits | The step 0 song's checksum holds, also with an effect waiting on each bus, also with helpers |
| Helpers | A song with sends from four tracks, send automation, both buses, a third slot, a mute and a bus effect swapped mid-song: the same to the last bit with 0, 1, 2 and 3 helpers, and twice from fresh |
| Lanes | 24. 16 to 19 are slot 3, 20 ECHO send, 21 SPACE send, 22 and 23 do nothing |
| Slots | `kTrackSlots` 3, `kMasterSlots` 2; `kEffectSlots` is gone |
| A swap on a bus | Fades like any slot when the bus is sounding (to and from silence, not the dry sound). On a bus at rest the new effect is simply there |
| Cost (desk, `bench`) | Feeding both buses from 12 tracks **0.03%** of a core (plan: 0.09). With DELAY and REVERB on them 0.12% in all. Handheld, times 5: 0.15% and 0.6%, against the plan's 2.5% for buses with TAPE and HALL |
| Departed from the plan | "Skipped after its tail" is measured, not declared: no effect says how long its tail is. A tail that never falls under the line (DELAY at full FEEDBACK is 90%, so it does fall) would keep its bus awake |

**Step 1b as built** (2026-11-11; only tests have seen it):

| Thing | Result |
| --- | --- |
| In a song file | `busfx BUS NAME settings` twice, after the master's `fx` lines; `send BUS AMOUNT` in a track, left out at 0; `fx 3 NAME` for the third slot. Lanes 16 to 23 load and save |
| An empty bus | Written as `busfx 2 none`. Departed from the plan, which had no word for it: without one, a bus the player emptied would come back full, since "no line" has to mean "old song" |
| A new song's buses | **TAPE and HALL since the join (2026-11-12)**, LEVEL 100%; as built in this step they were DELAY and REVERB. One line, `kNewSongBus` in `app/project.hpp`. An old song (no `busfx`) gets the same |
| A `busfx` line this version cannot use | An effect it does not have, or one of the wrong kind for the bus: skipped, the bus keeps the usual one |
| The Session | `setSend` (written as automation on lanes 20 and 21 like any setting); `setEffect` and `setEffectParam` take `kEchoBus` and `kSpaceBus`; a bus refuses anything but its own kind; `applyProject` sends the buses, the third slots and every send |
| Old songs | A five-track song with effects on tracks and master, opened through the Session and played for 12 s: **the same to the last bit** in the program before this step and after it, with 0, 1 and 3 helpers (checked once by hand, both programs built side by side). In the tests: an old song is the same to the bit with the buses emptied, and saves as it was plus the two `busfx` lines |
| Found on the way | A handheld with no saved song never told the engine its buses, so sends did nothing until a song was opened. The Session's constructor now does it |
| Not undone | Sends and what the buses hold are not part of undo, like effects. Send automation is, like all automation |

**Step 1c as built** (2026-11-11; seen in stills and tests, not on the handheld):

| Thing | Result |
| --- | --- |
| The pages | As drawn in section 5. TRACK: three slots, then SENDS (two boxes, ECHO and SPACE). ALL: the ECHO bus, the SPACE bus, the master's two slots. Y flips; moving down off the last row or up off the first goes to the other page too |
| A bus's row | Picks from NONE and its own kind only. MIX reads LEVEL. B on LEVEL gives 100% |
| A send | Hold A + stick turns it, the D-pad moves it a twentieth, B puts it at 0 and clears its automation in the cell. The purple line and the moving value show as on any setting |
| The group jump | Hold A + left / right (D-pad, or one push of the stick) on an effect: the first effect of the next group in the list, or of the one before. `effectGroupJump` reads the groups from the list. **With today's five effects every group has one effect, so a jump is the same as a step**; it starts to matter when TAPE joins DELAY. The rule is tested on made-up lists |
| Hints | All fit, with room, in nine stills (`fx`, `fx-sends`, `fx-pick`, `fx-all`, `fx-bus`, `fx-level`, three with `--hold-a`). The longest: `HLD A turn  B default value  Y buses  START stop` |
| Left out | Nothing marks which page is up except the title, the rows and the Y hint. (COMP's gain reduction on the THRESHOLD box was left out here and drawn at the join: see "Joined") |

**Step 4 (TAPE) as built, 2026-11-11.** `engine/src/fx/tape.cpp`, number 6,
word `tape`, in the list before DELAY. Only tests and measurements have seen
it; nobody has heard it.

| Thing | Result |
| --- | --- |
| TIME | Eight choices: 1/16, 1/8 triplet, 1/8, 1/4 triplet, dotted 1/8, 1/4, dotted 1/4, 1/2 (a triplet reads `1/8 TRIP`: `1/8 TRIPLET` ran over the edge of its box in a still). The step length **to the sample** at 90, 120 and 174 BPM, all eight |
| TIME turned, or a new tempo | The tape runs between half and double speed until it is there: a 1 kHz sine on it reads 500.0 Hz slowing, 2000.0 Hz catching up. The largest step is 0.99 of the sines' own: no click |
| FEEDBACK | 0 to **120%**. At AGE 0 and 1 kHz each echo is FEEDBACK times the last within 0.05 dB (plan: 0.5) |
| Tone lost each repeat | A high cut (12 kHz at AGE 0, 4.2 kHz at 50%, 1.5 kHz at full) and a low cut (40 / 126 / 400 Hz), 6 dB an octave. Per repeat at 100 Hz / 1 kHz / 6 kHz: -0.6 / 0.0 / -0.8 dB at AGE 0; -3.9 / -0.1 / -4.4 at 50%; -10.5 / -0.4 / -10.4 at full |
| Wow | A 0.8 Hz sine on the time, 1.5 ms either way at AGE full: **13 cents** either way. In proportion to AGE (measured 0.750 ms and 6.5 cents at 50%), none at 0 |
| Never runs away | FEEDBACK 120% for 30 s: level 0.79 / 0.43 / 0.18 at AGE 0 / 35 / 100%, the same within 0.2 dB from 10 to 20 s and 20 to 30 s. Loudest sample out with full-scale noise in at every extreme: 2.9 |
| Settings glide | 20 ms, all four. A sweep under a 100 Hz sine is 1.15 / 0.90 / 1.01 / 1.00 of the sine's own step (old DELAY's TIME: 33) |
| Small numbers | It makes **none too small to hold with flush-to-zero off** (checked by the processor's own mark, not a clock), and is the same to the bit with it on or off. A tail ends at 240 dB down and the effect sleeps: exact zeros, 0.1 us a block |
| Cost | **0.13%** of a desk core (plan: 0.16), so about 0.65% of a handheld core by the times-5 guess |
| MIX | The echoes' level; the dry sound stays whole (as the old DELAY). On a bus: `wetOnly_` in `tape.cpp`, read in one line; the bus call sets it |
| Departed from the plan | FEEDBACK tops out at 120%, a number the plan did not give. The clip is a cubic soft ceiling, not `tanh` (cheaper, and flat above 1.5) |
| Not done | The bus call itself (step 1a). Not heard; `bench` not run on the handheld |

**Step 5 (HALL) as built, 2026-11-11.** `engine/src/fx/hall.cpp`, number 7,
word `hall`, in the list before REVERB. Only tests and measurements have seen
it; nobody has heard it.

| Thing | Result |
| --- | --- |
| Build | Eight delay lines (37.1 to 101.9 ms at SIZE full, a quarter of that at SIZE 0) mixed into each other, four smearing stages a side before them, two lines bent (0.4 ms either way at 0.37 and 0.53 Hz, about 2 cents) |
| DECAY | Reads in seconds, 0.3 to 20, and is one: RT60 around 500 Hz is 0.93 to 1.02 of the reading at 15 settings (five DECAYs at three SIZEs), but for 0.3 s in the largest room (1.14). At 80 Hz it is within 5%. Plan: within 10% |
| The same DECAY at two SIZEs | The same time within 5% |
| TONE | DECAY 2.0 s: at TONE 0% the tail lasts 1.9 s at 80 Hz, 0.85 s at 1 kHz, 0.6 s at 8 kHz. At 50%: 2.0 / 1.7 / 0.74. At 100%: 0.9 / 1.85 / 1.5 (the lows thinned). Above its corner (600 Hz to 12 kHz) the tail lasts 0.3 as long |
| Pre-delay | Follows SIZE: 2 ms to 30 ms, to the sample (96, 768, 902, 1440 samples at SIZE 0, 50, 60, 100%) |
| Density | 1250 / 900 / 650 separate echoes in the first 100 ms at SIZE 0 / 50 / 100% (old REVERB: 200; plan: over 400) |
| Stereo | Left and right are unrelated: 0.03 or less (1 is the same). A sound on one side fills both, 0.5 to 6.5 dB more on its own side |
| Level | Held noise comes out 1.6 to 4.2 dB under itself at every corner of SIZE and DECAY |
| MIX | Equal power: the sound's share is cos, the room's sin; MIX 0 is the sound to the bit, MIX full the room alone. (The old REVERB only eases the dry sound to half.) On a bus: `wetOnly_` in `hall.cpp`, read in `changed()`; the bus call sets it and calls `changed()` |
| Settings glide | 30 ms (SIZE's lines 100 ms), and every gain that follows slides sample by sample. Turned end to end at once under a held sine, DECAY, TONE and MIX are no rougher than the sound standing still. SIZE bends the room's pitch on the way: a step 2.5 times the steady one going small. A sweep: 1.4 / 1.0 / 0.8 / 1.0 of the sine's step |
| Silence in, silence out | Exact zeros 6.7 s after a click at DECAY 2.0 s (1.3 s at 0.3 s, 60 s at 20 s): the room is asleep once it has held nothing over 240 dB down for a quarter of a second |
| **Flush-to-zero** | **Not needed, and not switched on.** HALL rests on a floor 360 dB down, so it makes no number too small to hold (checked by the processor's own mark, three DECAYs and with vanishing sound coming in), and is the same to the bit with flush-to-zero on or off. By the clock, flush-to-zero OFF: a block of 64 takes **2.7 us early, 2.8 us at its slowest in 40 s, 0.13 us asleep**. With the floor and the sleep taken out: 2.7 us, then 11 us in the second second, 30 us from the third, **100 to 130 us from the 22nd, for good** (10% of a desk core for one silent reverb). The other effects are not touched |
| Cost | **0.18%** of a desk core, 0.21% with TONE above 50% (the low trim runs only then). REVERB reads 0.09% in the same run against the plan's 0.07%, so on the plan's scale about 0.14% and 0.16%, against its 0.15%. By the times-5 guess, about 0.9 to 1% of a handheld core |
| Departed from the plan | MIX is a true swap-over, not the old REVERB's. Pre-delay is tied to SIZE (the plan gives no fifth setting). The cost is a little over the plan's with TONE up |
| Not done | The bus call itself (step 1a). Not heard; `bench` not run on the handheld |

**Step 6 (CHORUS) as built**, 2026-11-12. Only tests and measurements have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| File, number, word | `engine/src/fx/chorus.cpp`; `EffectKind::Chorus` = 8; `chorus`. Numbers 6 and 7 are kept for TAPE and HALL (NONE's row until they are built) |
| RATE | 0.1 to 8 Hz (`0.1 x 80^v`), read in HZ. Measured within 2% of the reading at 0.25, 1, 2, 4, 8 Hz |
| DEPTH | A percentage of the type's swing. Above 1 Hz the swing shrinks as 1 / RATE, so the pitch bend stays the same however fast it goes. Measured: VIBRATO at full 4.0 ms at 1 Hz, 2.0 at 2, 1.0 at 4, 0.5 at 8 |
| CHORUS | Three copies a side, at rest 7, 10.5 and 14 ms late, swinging 3 ms each way at full, a third of a turn apart; right a quarter turn behind left. All measured to 0.05 ms and 0.01 of a turn |
| ENSEMBLE | The same three, 2.5 ms, at 1, 1.29 and 0.77 times RATE |
| VIBRATO | One copy, 5 ms, swing 4 ms. No dry sound at full MIX (the click's own sample is exactly 0). Left and right in step. A 1 kHz sine at 5 Hz, DEPTH 50%: 987.4 to 1012.6 Hz (22 cents each way), as worked out |
| FLANGE | One copy, 2.2 ms, swing 1.9 ms, fed back at 0.6 (each time round measured x 0.600) |
| Width | The same noise on both sides in, MIX full: left and right 0.55 alike for CHORUS and ENSEMBLE, 0.46 for FLANGE, 1.00 for VIBRATO (1 is no width, 0 unrelated). With DEPTH 0 there is no width |
| Loudness | Dry and copies add to the same power. Noise comes out 0.4 dB quieter (CHORUS), 0.9 (VIBRATO), 0.4 louder (FLANGE): a copy read between samples loses a little of the very top |
| MIX 0 | The sound untouched, to the sample, in all four types; and turned down to 0 while sounding it gets there exactly |
| Clicks | Every setting glides (about 20 ms), TYPE included: the copies slide to their new places. Swept or thrown end to end under a 100 Hz sine, nothing steeper than 1.7 of the sine's own step, which is the louder output and not a jump |
| Cost | 0.18% of a desk core (this run; DRIVE reads 0.17 beside the plan's 0.08, so the machine is slower than the plan's). VIBRATO or FLANGE 0.08%. The plan said 0.13 and 0.05 |
| Departed from the plan | DEPTH's shrink above 1 Hz (not in the plan: at 8 Hz a full 3 ms swing bends the pitch by 15%). VIBRATO keeps left and right in step, against "left and right waver out of step", because out of step it is a chorus when heard in mono |

For the owner's ears: is ENSEMBLE different enough from CHORUS; is FLANGE's
fixed feedback (0.6) right; is DEPTH at full too much; VIBRATO in step.

**Step 7 (COMP) as built**, 2026-11-12. Only tests and measurements have seen it; nobody has heard it. The meter was in the engine only as built here; **it is on the screen since the join** (see "Joined").

| Thing | Result |
| --- | --- |
| File, number, word | `engine/src/fx/comp.cpp`; `EffectKind::Comp` = 9; `comp`. Group LEVEL |
| THRESHOLD | -48 to 0 dB, read in DB. Starts at -18 |
| RATIO | A choice of six: 1.5:1, 2:1, 3:1, 4:1, 8:1, 20:1. Starts at 4:1 |
| SPEED | Attack `1 x 30^v` ms, release `50 x 8^v` ms. It reads both: `1/50 MS` to `30/400 MS`, `4/115 MS` to start |
| The curve | A steady 1 kHz sine at 11 levels, 3 thresholds, 3 ratios: the worst of the 99 is **0.02 dB** from threshold, ratio and knee. The 6 dB knee, half a dB at a time: 0.01 dB |
| Attack and release | From a step in level (-40 to -4 dB and back, 4 kHz), 63% of the way in dB: **1.0 / 51 ms** at SPEED 0, 3.9 / 119 at 40%, 30.0 / 431 at 100%. The same at 96 kHz. The release runs about one attack longer than it reads (the turn-down is held, let go at the release speed, then smoothed at the attack speed) |
| Make-up | Automatic: half of what a full-scale sound is turned down by, **never more than 12 dB**. Measured +6.75 dB at -18 DB 4:1, +9.0 at -24 DB 4:1, +12.0 at -48 DB 20:1 (it would be 22.8) |
| A steady tone | It does not pump. Turned down 13.5 dB, the gain still moves within the wave by 0.000 dB at 1 kHz, 0.04 dB at 100 Hz (3rd harmonic 65 dB down), and 0.25 dB at 100 Hz with SPEED at its fastest (48 dB down): as fast as it goes, on a low note, it follows the wave a little |
| Below 60 Hz | The level is read through a two-pole high-pass at 60 Hz. The same sine at -6 dB is turned down 13.5 dB at 1 kHz, 11.2 at 60 Hz, 7.6 at 40, 4.2 at 30 |
| Left and right | Turned down together, by the louder: measured the same gain to 0.01 dB |
| MIX | The squashed sound beside the plain one. At 0 the sound untouched, to the sample |
| The meter | `Engine::effectMeter(track, slot)`: dB being turned down, as of the last chunk; 0 for an empty slot, an effect with nothing to show, or a deleted track. `track` may be `kMasterTrack`. An atomic: any thread, no lock. `Effect` gained `virtual float meter() const` for it |
| Clicks | The turn-down moves at its own speed and the make-up glides: any setting thrown across at once is no steeper than the sine underneath |
| Cost | 0.12% of a desk core on loud noise, every sample over the threshold (the plan: 0.07 with exact log maths; this machine reads DRIVE at 0.17 against the plan's 0.08). Under the threshold it skips the logarithm |
| Departed from the plan | Exact `log` and `exp`, not the quick kind: the curve is then exact, and it is still a tenth of a percent. The 12 dB cap on the make-up (not in the plan): without it a slow attack lets the first hit through 23 dB up |

For the owner's ears: is the automatic make-up about level on real sounds; is
one SPEED knob enough; does the 60 Hz cut do what is wanted on a bass track
(a bass under 60 Hz is hardly squashed); the master glue.

**For whoever builds the meter on the screen** (plan section 5: the THRESHOLD
box's bar shows it): each frame, `engine.effectMeter(track, slot)` with the
track and slot of the row (`kMasterTrack` for the master's rows). It returns
dB turned down, 0 or more. 0 means draw nothing. A full bar at 24 dB would fit
most uses. It is not smoothed: the engine reports the last chunk.

**Step 8 (SATURATE and the 2x stage) as built**, 2026-11-12. Only tests and measurements have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| Files, number, word | `engine/src/fx/saturate.cpp`, `engine/src/fx/halfband.hpp`; `EffectKind::Saturate` = 10; `saturate`. Group DRIVE, listed before the old DRIVE |
| The 2x stage | A half-band filter of four all-pass stages (two chains of two): eight multiplies a sample for up and back. Up and back with nothing between: 0.000 dB at every frequency tried to 23 kHz. What would fold back from above 28.8 kHz: **70 dB down or more**. Between 24 and 28.8 kHz it is part way shut (10 dB down at 25 kHz, 21 at 26), so a note folding back to above 19.2 kHz is only partly removed. **It makes the sound late**: 1.8 samples low down, 2.0 at 12 kHz (half a wave). So at a part-way MIX the plain sound is made late to match (`Halfband::late`, the four stages at the plain rate) before the two are added. As first built it was not, and MIX 50% lost 12 dB at 10 kHz and 25 at 12 kHz (a comb filter; found in review, 2026-11-12). Now MIX 50% is within 0.1 dB of part way from 1 to 18 kHz. MIX at exactly 0 is still the sound untouched: the plain sound slides between the two in about 20 ms |
| DRIVE | Up to **24 dB** (16 times); FOLD up to 6 times. 20 dB of drive is 83% |
| Curves | TAPE: `x (27 + x^2) / (27 + 9 x^2)`, flat beyond 3 (tanh, nearly). TUBE: the same curve 0.5 off centre, brought back on centre afterwards (5 Hz). FOLD: a sine, by polynomial. HARD: straight to 0.75, flat from 1.25, a curve between |
| Fold-back, 4186 Hz sine at half scale, 20 dB of drive | Against the sine going in. **TAPE: loudest fold-back 59 dB down, all of it together 57** (48 under its third harmonic). FOLD 84 and 84. HARD 43 and 38. TUBE 38 at 22884 Hz (its sixth harmonic, folding into the part-way band; too high to hear) and **63 under 19 kHz** |
| The same, 2x stage left out | TAPE 24 dB down, TUBE 27, FOLD 30, HARD 22. **The stage buys 35 dB on TAPE**, 54 on FOLD, 21 on HARD, 36 on TUBE under 19 kHz |
| Against the old DRIVE | At its 81%, the same sine: loudest fold-back 20 dB under the sine going in (9 under its third harmonic), all of it together 16. TAPE without the 2x stage is where DRIVE is |
| The plan's 50 dB line | "Everything that is not a harmonic at least 50 dB under the harmonics": TAPE 57 and FOLD 84, yes. TUBE yes under 19 kHz (63), no above. **HARD no (38)**: a corner makes high harmonics, and twice the rate is not enough for them. The plan measured one soft curve |
| At full DRIVE (24 dB) | Worse, as any drive is when it squares the wave: TAPE 43 dB down, TUBE 39, HARD 39, FOLD 78 |
| Harmonics | TAPE, FOLD, HARD: odd only (even ones under -100 dB). TUBE: second harmonic 15 to 25 dB down. FOLD at 83%: the third harmonic 13 dB over the sine itself |
| Loudness | For each DRIVE and TYPE the output is scaled so a sine at half scale comes out as loud as it went in: **within 0.03 dB over the whole of DRIVE, all four types** (the old DRIVE: +1.4 to -7.1 dB). A quieter sound comes up as DRIVE rises (a sine at -20 dB: +4.4 dB at 50%, +11.8 at 100%) and a full-scale one comes down (-5.0, -5.9) |
| TONE | One-pole low-pass after the curve, 500 Hz to 20 kHz: 3.0 dB down at the corner, measured at 48 and 96 kHz |
| MIX 0 | The sound untouched, to the sample. Half way: half of each, sample for sample |
| Clicks | DRIVE slides and its level goes with it; TYPE fades from the old curve to the new over 10 ms. Swept, thrown end to end, or one TYPE to another: nothing steeper than the wave is when held still |
| Cost | **0.15% of a desk core**, under the old DRIVE's 0.16 in the same run (the plan: 0.18 at 2x with a cheap curve) |
| Departed from the plan | DRIVE stops at 24 dB, not more: past that every type is a square wave and the fold-back climbs. FOLD has its own, shorter range. A second change of TYPE inside the first one's 10 ms fade could click as built here; **fixed at the join**: the second waits for the first |

For the owner's ears: do the four types differ enough to keep four; is TUBE
lopsided enough (0.5 off centre) or too much; is FOLD's range right; is
holding a half-scale sine level the right "level" (quiet tails and noise come
up with DRIVE); TONE's range; SATURATE beside the old DRIVE on a high lead.

**Step 9 (DUCK) as built**, 2026-11-12. Only tests, measurements and one still have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| Where | `engine/src/fx/duck.cpp`, number 11, word `duck`, group LEVEL (after COMP in the list). Three settings: AMOUNT, RELEASE, SOURCE (8 choices, reads `LANE 1` to `LANE 8`). As it comes: 60%, 209 ms, lane 1 |
| The hook | `Effect::drumHits(lanes)`: "these lanes of the first drum track are hit on the first sample of the block you are about to get". Only DUCK listens. The engine marks a hit where it triggers the drum (`Track::hitsNow`, two places: `playHit` and `firePending`) |
| When it is written and read | **Written on the audio thread before the tracks are shared out; only read after.** `render()` already ends a chunk where the next hit is due, so every hit lands on a chunk's first sample and "which lanes" is all there is to say (no sample offsets). `renderChunk` copies the first drum track's marks to `chunkHits_` and clears them before any helper is woken; `runSlot` hands them to the effect. A helper working on the drums changes nothing a ducked track looks at. The same to the last bit on 0 to 3 helpers and twice from fresh (a song with DUCK on the drums themselves, three synths, the master, and one swapped out and in) |
| The first drum track | The lowest-numbered track that is drums, worked out each chunk. Remove it and the next one is the source |
| Which hits count | **Every trigger of the lane, whatever made it** (the plan was silent): a pattern's hit, each hit of a ratchet, a hit pushed early or late (on its own sample), a drum played by hand or from a keyboard, song stopped or not. A step its chance leaves out is no hit. The hit's level does not matter (a ghost dips as far as an accent) |
| The shape | A straight fall to `1 - AMOUNT` in **3 ms** (`kFall`), not at once: a step in level is a click, and a muted drum is not there to hide it. Then back over RELEASE, `(1 - t)^2` of the dip left at `t` of the way: quick at first, easing in, and **it arrives exactly** (from then on the sound passes untouched to the sample). A hit before it is back falls again from where the level is. RELEASE is 20 ms to 1 s (`20 * 50^v` ms) |
| Where it may go | Track slots and **the master (allowed: the plan was silent; it dips the whole mix)**. Not a bus: its group is LEVEL, so the picker does not offer it there and `Engine::setEffect` refuses it; a bus's slot is not told the hits |
| The rules for new effects | `soundOnly` first; AMOUNT and the speed of the way back glide (20 ms) and at rest a turned setting is simply there; the same to the bit in blocks of 1, 7, 35, 64 and 4480; no number too small to hold (`TooSmall`); the same with flush-to-zero on or off; at rest it does no work. No MIX: AMOUNT at 0 is the sound untouched |
| The meter | `meter()` is the dB it is dipping (60 for "right down"); `effectMeterParam` puts it on the AMOUNT box, the same strip as COMP's. Still `fx-duck` |
| Tests | `testDuck` in `effects_test` (the plan's row: dip of AMOUNT at the hit's sample, back up in RELEASE, nothing from other lanes; then the engine: what each slot is told and when, muted drums, the master, a DUCK fading out of its slot). `through()` there now hits a drum five times a second so the checks for every effect reach DUCK. 53 mutants, 50 killed; the 3 left change nothing that can be seen (a second clearing of state on reset, a shortcut for speed, the block the glide is worked out in) |
| Cost | 0.02% of a desk core, dipping eight times a second (a run where DRIVE read 0.12%). The plan guessed 0.01 |

For the owner's ears: the 3 ms fall (shorter is tighter but nearer a click, and it is one number); the shape of the way back; whether a ghost hit should dip less than an accent; whether a drum played by hand should dip at all; 60% and 209 ms as the place to start.

**A bounce runs until its tail ends** (owner, 2026-11-12), as built the same day. Tests and two stills have seen it; nobody has listened to a bounce.

| Thing | Result |
| --- | --- |
| Before | Three seconds after the song's last step, always. A long HALL or TAPE tail was cut |
| What is sound | Anything at or over **-90 dB** (`kBounceFloor`, 3.1e-5: one step of a 16-bit file; less is not in the file anyway), on either side |
| When it is over | **No sound for 7 seconds** (`kBounceQuietSeconds`; 4 until REVERSE, 2026-11-14). Half a second was the first thought and is wrong: a DELAY or TAPE at a half note and 40 BPM leaves 3 seconds between echoes, and REVERSE there 6 (a hit at a window's start comes back two windows later: `kLongestEchoGapSeconds`), and a gap would have been taken for the end. Seven is longer than that, as the bus's ten are |
| Where the file ends | **Half a second after the last sound** (`kBounceAfterSeconds`). A dry song is now shorter than it was: its last drum rings out, half a second, done (it had three seconds whatever was in them). Nothing a dry song plays is cut: the end is measured, not guessed |
| The cap | `kBounceCapSeconds`. A tail that reaches it is faded out in a straight line over its last second (`kBounceFadeSeconds`) |
| Each track | The files of an EACH TRACK bounce are padded with silence to the longest, so they still line up and are the same length |
| Memory | Written as it is worked out (16-bit, to `NAME.wav.part`, renamed when whole) instead of held as floats until the end: eight minutes of tail would have been 180 MB in memory on a 1 GB handheld. A cancelled bounce leaves no file, as before |
| Progress | The percentage counts the song's own playing only, since that length is known. In a tail the row reads `TAIL 12 S` (seconds of tail so far) and "until the echoes have died away". For EACH TRACK the percentage stands still during each track's tail. Stills `bounce`, `bounce-tail` |
| Code | `BounceTail` (`app/session.hpp`, `app/bounce.cpp`): fed blocks, it says "another" or "stop" and how many frames to keep. No engine and no file in it |
| Tests | `testWhereABounceEnds` (made-up tails: silence, long, sparse echoes, what counts as sound, the cap and its fade), `testABounceRunsUntilTheTailEnds` (real bounces: HALL at 20 s, the cap set short, echoes three seconds apart at 40 BPM, two tracks of different tails), `testBouncing` (a dry song ends on its last sound; cancelled part-way, nothing is left). 30 mutants, all killed. The row's words are seen in the still only |

**A cap so a tail always ends** (owner, 2026-11-12: "there should be a cap"), as built the same day. Only tests and measurements have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| What was wrong | TAPE with FEEDBACK at 100% never died away, and past it the howl held for good. A send bus holding it never came to rest and cost TAPE's time for good; a bounce that runs until the tail ends would never have ended |
| TAPE: what changed | **Only a loop left alone for more than 30 seconds with FEEDBACK reading over 90%.** While sound comes in (anything over 80 dB under full, `kFedLevel`), and for 30 s after the last of it (`kHoldSeconds`), FEEDBACK is what it reads: it builds, howls and holds exactly as it did. Then FEEDBACK eases down to 90% (`kTiredFeedback`: the most the old DELAY has) over about 2 s, and each echo is nine tenths of the last. Sound coming in again puts FEEDBACK back within 20 ms and starts the 30 s again |
| TAPE: what did not | FEEDBACK at 90% or under: nothing, ever, to the bit (the loop's gain is multiplied by exactly 1). Over 90%: nothing while it is played into or for 30 s after. The measurement from step 4 stands: 120% holds one level from 10 to 30 s within 0.2 dB |
| Measured, 120% at a sixteenth, 120 BPM | Level 0.79 until 30 s; 0.46 by 32 to 33 s; then 7.7 dB a second; 120 dB down by 48 s; the exact zero (asleep) at 68.7 s. No step larger than the howl's own where the fall starts. At AGE full it falls faster (9.2 dB a second) |
| Measured, each echo of the last at 1 kHz, a half note at 40 BPM | FEEDBACK 90%: 0.8974 before and 0.8974 at 69 s (the same to five places). 95%: 0.9472, then 0.8974. 120%: 1.1965, then 0.8974 |
| The reading | Still `120%`: it is true whenever sound is going in, which is when it is being set. The manual says what happens when it is left alone |
| In a track's slot | The same code, the same cap |
| DELAY, REVERB (pinned) | **Not touched, and not capped from outside: neither can ring for good.** DELAY's loop is at most 0.9 times a low-pass that never adds; REVERB's four loops are at most 0.98 times the same, and its smearing stages pass everything at one level. Measured below |
| HALL | Not touched: DECAY is a real time, 20 s at most |
| **How long a bus takes to come to rest** | From the last sound sent (a quarter second of full-scale noise) to the bus at rest (120 dB down, then its 10 s), the worst of every setting at 0, 50% and 100%, at 40 BPM: **TAPE 418 s** (a half note, FEEDBACK full, AGE 0), **DELAY 388 s** (a half note, FEEDBACK and TONE full), **HALL 44 s**, **REVERB 36 s**. At 120 BPM the two delays take a third as long. `kLongestTailSeconds` = 450 in `engine.hpp` is the promise; `testATailAlwaysEnds` holds every case under it and the worst within 20% of it |
| The bounce's cap | 480 s, checked to be over `kLongestTailSeconds` (a compile-time check in `bounce.cpp`, and in `app_test`). A bounce stops listening at -90 dB, not -120, so it ends well before a bus rests |
| Louder than full scale | DELAY's tail grows by its echo time for each 0.9 dB the sound sent is over full scale (it is a straight line: +12 dB is 40 s more at 40 BPM). TAPE's does not: its ceiling sets where the fall starts |
| Cost | TAPE 0.11% of a desk core with it, 0.10% just before, in runs where DRIVE read 0.12% both times: one more multiply a sample |
| Tests | `testTapeAlwaysEnds`, `testATailAlwaysEnds` in `effects_test`; a real bounce of TAPE at 120% in `app_test` (it ends by itself, 36 to 60 s after the last step). 25 mutants, 24 killed; the one left only guards a counter against running over |

**After the review of that work** (2026-11-12). Three things it found, all small, all reproduced.

| Thing | Result |
| --- | --- |
| FEEDBACK reading exactly 90% was touched | `1.2f * 0.75f` is one float over `0.9f`, so the test "reads over 90%" was true at 90% itself and the loop's gain was multiplied by 0.99999994 after 30 s (6 parts in 100 million: not to be heard, but not "to the bit"). Now the SETTING is compared, 0.75 and a hair of room (`tapeTire` in `tape.cpp`, a function the test calls). 75% reached by the D-pad's notches, or by sums that land a float off, is multiplied by exactly 1; 76% (reads 91%) is brought down |
| Two TAPEs in a row outlast the cap | Each counts its 30 s from the last sound coming IN, and a howling one upstream keeps the next fed: a slot into ECHO, both at a half note, FEEDBACK full, 40 BPM, is about 710 s by the numbers (the reviewer's bounce ran into the 480 s cap and was faded, as the cap is there to do). `kLongestTailSeconds` is a promise for one effect; the manual and the comments now say so. **Not changed**: whether effects in a row should tire together is the owner's call |
| A killed bounce left its `.wav.part` | The file is written as it goes, so a bounce killed part-way (power, a crash) left up to 90 MB that nothing listed or removed. `removeHalfBounces` takes away every `*.wav.part` in the bounces folder when the program opens and when a bounce starts, never while one is under way |

For the owner's ears: 30 seconds of hold (is that "a long while"); the two seconds of easing down, which is the moment a held howl starts to go; 90% as where it settles (lower would end sooner and sound more like a fade); whether a howl should ever be allowed to hold for good when a hand is on the knob (today it does as long as anything is played into it).

**Joined, 2026-11-12** (sends, TAPE and HALL, CHORUS, COMP and SATURATE on one branch, and what the reviews found). Only tests, measurements and stills have seen any of it; nobody has heard it.

| Thing | Result |
| --- | --- |
| A new song's buses | **TAPE on ECHO, HALL on SPACE**, LEVEL 100% (`kNewSongBus`). ECHO takes TAPE or DELAY, SPACE takes HALL or REVERB, the newer first in each list |
| An old song's buses (no `busfx` line) | **The same: TAPE and HALL.** Its sends are all 0, so it sounds as it did, to the bit, whatever the buses hold. The choice only shows when its owner first turns a send up, and then it gives what the same move gives in a new song: one rule, "a bus nobody has touched holds TAPE and HALL". The other choice (DELAY and REVERB for old songs) would have made the same gesture sound different by the song's age, for no reason a player could see; no saved song can hold a bus set to DELAY by an earlier build, since a build that knew buses always wrote the `busfx` lines |
| One Glide | `fx::Glide` in `fx/glide.hpp` for all five new effects. **It always lands**: never slower than `kLeast` (1e-7 a sample, for a value near 1). In blocks of 8 frames and under the old one stood a hair short for good, and HALL and SATURATE stayed in their costly moving mode. Tested on Glide itself at 1 to 64 frames a block |
| One wet-only flag | `Unit::bus_`. TAPE's and HALL's own `wetOnly_` went |
| A bus takes its own kind | Refused in `Engine::setEffect` too (it returns false), not only in the Session. A FILTER on ECHO gave the track's sound back a second time |
| **A bus's LEVEL is the engine's** | Departed from 1a, where LEVEL was the effect's MIX. The effect on a bus now stays at MIX 100% and the engine applies LEVEL after the rest rule has listened, gliding 5 ms like a send. As built in 1a a bus parked at LEVEL 0 with echoes in it was heard as silent and cleared after ten seconds. For DELAY and REVERB the sound at a steady LEVEL is the same multiply, moved; a turned LEVEL now glides where theirs stepped. An effect handed over whole by the setup call keeps its own MIX |
| The rest rule's numbers | 1e-6 and 10 s are held by a test, on each side alone; `prepare()` puts both buses to rest and clears them (tested) |
| A send of 1% | Saved and read back as it is (tested down to 0.1%) |
| Not a number, infinity | Taken as silence at the way in of every new effect (`fx::soundOnly`, one compare a sample, also anything beyond 1e8) and at the feed of both buses (`feedBuses`). One bad sample used to stay for good in TAPE, HALL, CHORUS, SATURATE, COMP (which also left its make-up gain on) and both buses. With the bad sample in, each now gives the same output to the bit as with a zero there. The old five are not touched. SATURATE's FOLD gives 0 for a value too far round to count the turns, instead of casting it to an int |
| SATURATE, TYPE changed twice within 10 ms | The second change waits for the first fade and only the last one asked for is gone to. Was up to 11.6 times the steady step; now no steeper than the wave held still, for every pair of changes at five spacings |
| COMP's meter on the screen | A thin brass line over the THRESHOLD box's bar, growing from the right; the whole width is 24 dB (`kMeterFullDb`), a tenth of a dB shows as one dot. `effectMeterParam(kind)` names the box, so the screen still spells no effect out. On track rows and master rows. Stills `fx-comp`, `fx-comp-all` |
| DELAY and REVERB on a bus, on ARM | fx-sends put an `if (bus_)` inside their per-sample loops. `effects_test --print`, built as the handheld's program (clang, aarch64, `-O2 -mcpu=cortex-a53`) and run under an emulator: **the 40 renders and the whole song are the same to the last bit before the sends (main) and after**. The loops were left as they are |
| A read past the end in a test | `testBusEffectsInTheEngine` read 4800 floats past a vector. Fixed; `effects_test` built with the address and undefined-behaviour sanitizers runs clean (the version before fails there) |
| Readings that did not fit | `1/8 DOTTED` ran out of TAPE's and DELAY's TIME box (now seen on every new song's ALL page). They read `1/8 DOT` and `1/4 DOT`, beside `1/8 TRIP` |
| Cost (desk, `bench`) | One each, in a run where DRIVE read 0.18%: FILTER 0.05, SATURATE 0.21, DRIVE 0.18, TAPE 0.15, DELAY 0.03, HALL 0.20 (0.23 with TONE above 50%), REVERB 0.09, CHORUS 0.20 (0.10 as VIBRATO or FLANGE), COMP 0.13, CRUSH 0.02. The way-in check costs each new effect about 0.01 (the build before the join read TAPE 0.16, HALL 0.19, CHORUS 0.19, COMP 0.12, SATURATE 0.21 back to back). **The buses, as what they add to twelve busy drum tracks (2.0% by themselves): feeding both 0.05 to 0.12; with TAPE and HALL 0.41 to 0.50; with DELAY and REVERB 0.09 to 0.18** (a small difference of two large numbers: it moves that much from run to run). Handheld, times 5: about 2.5% of a core for both buses with TAPE and HALL, which is the plan's figure |
| Not done | (THROW and DUCK were built after this join: "Steps 2 and 3 as built", "Step 9 as built".) The bus LEVEL is not automated (nothing on a bus is). The meter is not smoothed on the screen: it shows the last chunk's reading, which COMP's own release already smooths |

For the owner's ears, new at the join: TAPE and HALL as the first thing a send
gives (against DELAY and REVERB); a LEVEL turned while echoes ring (it glides
5 ms: is that slow enough not to click, fast enough to feel direct); whether
the meter's 24 dB width suits how hard COMP gets used.

**Steps 2 and 3 (THROW) as built**, 2026-11-12. Only tests, measurements and
stills have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| The value | `throwTo` on a hit (`StepFx`), a note (`NoteEvent`) and a chord: 0 OFF, 1 ECHO, 2 SPACE, 3 BOTH |
| A thrown drum hit | The drum machine writes that voice a second time, into a buffer for each bus (`DrumMachine::render`'s two extra ways out). The bus is given **the hit as it sounds alone, to the sample**, with kick and hat around it and the same lane a step before and a step after. It skips the track's slots; it takes the track's level and mute |
| Its ratchet | Every piece is thrown: they are the one hit. A cut hit is thrown cut |
| A thrown note | The track's send is held open for the note's length and slides open and shut over 5 ms (240 samples). The steepest it makes a 100 Hz sine is the sine's own step plus 1/240 of its height. Other notes sounding on the track then go with it, as the plan said |
| A ratcheted note | The first piece opens the send for the whole note, so it does not shut in the gaps between the pieces (5 ms at 120 BPM, x4: it would have dipped every piece) |
| Two thrown notes that overlap | Open from the first's start to the last one's end |
| A chord | **Takes THROW as a note does** (the plan was silent): every key of it. Kept when the chord is swapped or broken into notes |
| With the send knob up | "In full" is a send of 1. The knob's share is topped up to 1, never added to: a note gets `knob + open * (1 - knob)`, a thrown hit the `1 - knob` the knob leaves. At 100% a THROW changes nothing, to the bit |
| A resting bus | Woken on the hit's or the note's own sample |
| A muted track | Throws nothing and wakes nothing. Muted while a thrown hit rings, the bus's share fades with the track's (5 ms) |
| Stop, Play, panic | Every send a THROW held shuts (5 ms; behind a shut panic gate). So do `prepare` and a track taken away. A thrown drum hit rings out after Stop as any drum does |
| Slices (a hit that carries the lane's sound on) | It hands on its own THROW: the running sound slides into the bus over a millisecond, and out again at the next slice that is not thrown. No step, and the lane's own sound is the same to the bit as with no THROW |
| No throws, same bits | The pinned song and the 40 renders still match. A track that throws nothing takes the loop it always took in `feedBuses`, and an unthrown voice the loop it always took in the drum machine. **On ARM too**: `effects_test --print` built as the handheld's program (clang, aarch64, `-O2 -mcpu=cortex-a53`) and run under an emulator gives the same 40 renders and the same whole song, to the last bit, before THROW (main) and after |
| Helpers | A song with thrown hits (ratcheted, cut, pitched, backwards) on two drum tracks, thrown notes (ratcheted, nudged) on three synths, TAPE and HALL on the buses, two send knobs up and two mutes part way: **the same to the last bit with 0, 1, 2 and 3 helpers**, and twice from fresh |
| In a command | With the cut, as planned: `extra` is the cut plus 4 times THROW (`SetStepFx`, and `Trigger` so that X on a hit is heard thrown). A note's: 512 and 1024 added to `SetNote`'s `e` |
| In a song file | The seventh number of `stepfx`; the sixth of `note`, with the nudge written before it even at 0; the third of `chordfx`, with the ratchet written before it even at 1. Each left off at 0: **a song with no throws is written as it always was**, and an older program reads a thrown song without its throws |
| The gesture | Hold X + A, on the drum grid, in the slice view and on the piano roll: OFF, ECHO, SPACE, BOTH, round. Heard at once on a drum hit (song stopped). A alone still places; with X held it neither places nor shapes |
| The dice | Hold X + Y neither throws nor takes a THROW away |
| On the grid | A drum hit: a short purple bar above its block, over the left half for ECHO, the right for SPACE, one each for both (the order the sends have on the FX screen). A note: a foot at its end, the same way round (cream on a chord, which is purple already) |
| The strip, X held | Drums: PITCH, RATCHET, REVERSE, CUT, THROW. Five fields, so they sit closer than the four did. Notes: RATCHET, GLIDE, THROW. With X not held a thrown hit or note says so in its line of effects. On the piano roll a note that is nudged, has an effect and is thrown has no room for all three at full size: its NUDGE value is drawn in the largest size that fits before the effect (`stripFont` in `app/note_marks.hpp`; the small one for 10% and over), where it used to be drawn over the RATCHET. Still: `--view notefx-worst` |
| Hints | `UP / DOWN pitch  LEFT / RIGHT ratchet  L1 reverse  R1 cut  A throw  Y dice` fits with room (still `effects-held`); the piano roll's too |
| Undo, copies | Undone and redone by SELECT + B (it is in the patterns' text). Copied with its bar on SONG; carried with its note (hold R1) |
| A bounce | Has the thrown hit's echoes (tested: the first echo three steps on, as loud as the hit); none from a muted track. As built the bounce stopped three seconds after the song and cut a late throw's tail; **since the join it runs until the tail ends** ("THROW and DUCK joined") |
| Cost (desk, `bench`) | Twelve busy drum tracks with nothing thrown: 1.45% of a core, as before THROW (1.46). With THROW's check inside the one voice loop it was 2.18%, so the loop is built twice. **Every hit of all twelve thrown to both buses: 1.2% more**, the most a song can throw. A thrown note costs one more multiply a sample on its track while its send is open |
| Tests | `throw_test` (the engine, 2 s) and `throwapp_test` (the gesture, the song file, undo, a bounce; 5 s). Mutation: 68 breaks of the engine's part, 67 caught, and the one left changes nothing (an empty note slot's THROW bits); 37 of the app's, all caught. The drawing of the marks is seen in stills only |
| Departed from the plan | Nothing in the design. Added: the chord; `Trigger` carries THROW; the top-up rule with the knob |
| Not done | The dice do not scatter throws (the owner's "later"). A hit or a key played by hand from a USB keyboard is never thrown. The thrown copy of a drum hit is mono like the drums. Nothing draws which bus a held note's send is open to while the song plays |

For the owner's ears: a thrown hit's echo comes back **as loud as the hit**
with the bus's LEVEL at 100% (measured 0.71 against 0.62 through TAPE): is
full too much as the only amount? Does a thrown drum hit want the track's
filter and drive on it (question 6)? Is 5 ms the right ramp on a thrown note,
which softens the note's own attack in the bus? Other notes on the track
going with a thrown note: wanted or not? A thrown slice inside a running
break: does the 1 ms slide tick?

**THROW and DUCK joined**, 2026-11-12 (`fx-duck` merged into `fx-throw`). The
code joined by itself; the one conflict was this file's first paragraph. No
line of the engine or the app was changed for the join: what is below is how
the two behave together, each held by a test. Only tests, measurements and
stills have seen it; nobody has heard it.

| Where they meet | What happens | Held by |
| --- | --- | --- |
| A thrown drum hit, a DUCK on its track | **The thrown copy is not ducked.** It is written out inside the drum machine, before the slots, as the plan says. The bus is given the same to the sample with the DUCK and without, while the track itself dips | `throw_test`, `testWithDuck` |
| A thrown note, a DUCK on its track | **The thrown note is ducked.** A note's THROW is the track's send held open, and a send is taken after the slots: the bus gets the track as it sounds, dips and all. At AMOUNT 100% the bus is given nothing 3 ms after the kick | the same |
| A DUCK on the master | Dips the whole mix, which the buses have joined by then: a throw's echoes dip with the song. What the bus is GIVEN does not change | the same |
| A thrown hit is a hit | It marks its lane for DUCK like any other (the mark is made where the drum is triggered, thrown or not) | a mutant that leaves thrown hits unmarked fails `throw_test` |
| A hit thrown into a TAPE with FEEDBACK at 120% | The tape is fed by the throw, sings for thirty seconds after it, tires, and **the bus rests 58 s after the hit**. A second hit thrown at 25 s starts the thirty seconds again (83 s). Both far inside `kLongestTailSeconds` | `throw_test`, `testThrownIntoAHowl` |
| A bounce whose LAST step throws a drum hit | The hit is in the song and every echo of it is in the tail: all kept. The file is exactly as long as with the hit on step 1, fifteen steps later (7.63 s against 5.76), and ends half a second after the last sound | `throwapp_test`, `testABounce` |
| A bounce whose last step throws a note | The song's end stops the note and shuts its send (5 ms), as Stop always does. The bus has had the one step of the note there was; its echoes are kept (the file runs 4.9 s past the song against 0.7 s unthrown) | the same |
| The last step thrown into a howling TAPE, bounced | The bounce ends by itself, 64 s after the song. With the cap set to 5 s it ends there, faded | the same |
| Helpers | A song with thrown hits, thrown notes, sends, TAPE and HALL on the buses, DUCKs on both drum tracks, two synths and the master: the same to the last bit on 0, 1, 2 and 3 helpers and twice from fresh | `throw_test`, `testOnAnyHelperCount` |
| Old songs | The pinned 40 renders and the whole song match; `effects_test --print` gives the same as main's | `effects_test` |
| Cost (desk, `bench`) | In one run (DRIVE 0.12%): DUCK 0.02%, TAPE 0.11%; twelve busy drum tracks 1.44%; every hit thrown to both buses 1.19% more; **all of it at once (every hit thrown, sends up, TAPE and HALL on the buses, a DUCK on each of the twelve tracks) 1.70% more** | |
| Changed for the join | `throwapp_test`'s bounce expected the old fixed three seconds; it now expects the file to end half a second after the last sound. 18 tests | |
| Mutants | 15 at the meeting points, 14 caught. The one left (no cap on how long the bounce WORKS) writes the same file: the tape tires by itself and the file is cut at the cap either way | |
| Not done | No handheld build, nothing run on the device. A thrown note's own release, after the song's end, is not sent to the bus (as for any thrown note that is let go) |

After the second review (2026-11-12):

| Thing | Result |
| --- | --- |
| A tired TAPE depended on its blocks | The count of time left alone went up a block at a time, the ease down was a glide a block, and it fell asleep at the end of the call it was in. Blocks of 64 and of 48 came apart 30.2 s into a tired tail (by up to 0.002 in the test here), and by 1e-23 where it fell asleep: a bounce and live play were not the same. **Now each is on its own sample**: the count, the ease (`tapeTireStep`: a fraction of what is left a sample, 2 s down and 20 ms up, never slower than 2e-7, so it lands), falling asleep, and waking (at the first sample of sound, not the block's first). The numbers are as they were: 0.46 by 32 to 33 s, asleep at 68.7 s |
| TAPE's wow took its shape from the blocks (on `main` too) | The sine was worked out at each end of a call and joined by a straight line. With AGE up (the default is 35%) blocks of 64 and of 48 were apart from 0.15 s, by 0.05 once it howled. Now a point every 64 samples the tape has run (`kWowEvery`), whatever the calls. Rate and depth as measured before. **A song with TAPE and AGE up is no longer the same to the bit as before this**, which it never was from one block size to another |
| TAPE's test | `testTapeAlwaysEnds`: 150 s (howl, tire, sleep, a faint sound waking it part way through a block, tire again) cut four ways (64, 48, 256, and 1 to 61 in turn) at AGE 0, 35% and 100%: the same to the bit. TAPE had no such test before |
| **HALL depends on its blocks too. Not changed** | Found while testing TAPE: HALL (SIZE 50%, DECAY 50%, MIX full) in blocks of 64 and of 48 is apart from 0.02 s, by 0.06. It is the same on `main`; it is not this step's code and a change there is a change to the sound. For its own piece of work |
| THROW's value on the piano roll's strip | "SPACE" at 575 in the large size ran to pixel 638 of 640 (the margin is 632). `stripThrowFont` (`app/note_marks.hpp`): the value moves left to end at the margin (SPACE alone: at 569), and beside an effect too long to leave it room ("x4 GLIDE") it is drawn in the middle size where it was. ECHO and BOTH are where they were. Stills: `--view notefx-space`, `notefx-worst` |
| Cost | TAPE 0.09 to 0.13% over six runs (DRIVE 0.11 to 0.13% in them; 0.11% before) |
| Mutants | 26, 24 caught. The two left change nothing that can be seen: `reset()` no longer clearing the wow's count (waking clears it too), and falling asleep with sound under 240 dB down still coming in (it wakes on the next sample) |

For the owner's ears, new at the join: a thrown kick on a track that ducks
itself goes to the bus at full while the track dips. Is that right, or should
the echo dip too? A thrown note under a DUCK sends a dipped note: wanted?

**Each step ships by itself.** Steps 4 to 8 are useful in today's two slots
before sends exist.

**Side by side, once step 0 is in:**

| Person | Does | Shared files |
| --- | --- | --- |
| A | 1a, 1b, 1c, then 2 | the engine, the song file, the FX screen |
| B | 4, 5 (the long-memory ones) | only one row each in the effect table |
| C | 6, 7, 8 | the same one row each |

Step 0 exists to make that true: today a new effect means edits in
`effects.cpp`, `effects.hpp` and `fx_screen.cpp` at once. Steps 2, 3 and 9
all touch `engine.cpp`: one person, or in turn.

**PING as built**, 2026-11-13 (branch `fx-ping`). Only tests, measurements and stills have seen it; nobody has heard it.

| Thing | Result |
| --- | --- |
| Where | `engine/src/fx/ping.cpp`, number 12, word `ping`, group DELAY, family DELAY, type PING (third, after TAPE). TIME, FEEDBACK, TONE, MIX. As it comes: 1/8 DOT, 45%, 60%, 35% |
| The loop | Two lines, each TIME long. The sound goes on the left line (both sides halved and added). The left line's echo goes on to the right line, the right's back to the left, each time through a 6 dB an octave high cut and times FEEDBACK. So: left, right, left..., TIME apart, each FEEDBACK of the last |
| TIME | TAPE's eight choices. The step length to the sample at 90, 120 and 174 BPM, all eight; the first echo is the sound itself to the bit. Turned (or a new tempo) it glides as TAPE's: half to double speed, a 1 kHz sine reads 500.0 and 2000.0 Hz at the extremes, then lands exactly |
| FEEDBACK | **Reads what it does, 0 to 90%** (the plan asked "each repeat down by FEEDBACK within 0.5 dB"; CLEAN's reads 100% for 0.9). An echo's sum is FEEDBACK of the last to 0.002 at any TONE; at 1 kHz within 0.01 dB at TONE full and 0.19 dB at TONE 60% |
| TONE | The cut's corner, 600 Hz to 18 kHz (CLEAN's range, so TONE carries to and from CLEAN untouched). Each repeat loses, at 100 Hz / 1 kHz / 6 kHz: 0.12 / 5.77 / 19.82 dB at TONE 0; 0 / 0.19 / 4.07 dB at 60%; 0 / 0.01 / 0.28 dB at full |
| Sides | Echo n is all on one side; the other is exact zeros there. Left and right share 0.0000 of the tail. A sound on one side only bounces the same, at half the size; opposite on the two sides, it makes no echo |
| It ends | 0.9 at most: 7.43 dB a second at a sixteenth at 120 BPM, silent (the exact zero) 31 s after a burst. On a bus at 40 BPM, a half note, FEEDBACK and TONE full: at rest 388.0 s after the last sound (DELAY the same; the promise is 450) |
| The rules for new effects | `soundOnly` first; FEEDBACK, TONE and MIX glide 20 ms and land; nothing under 240 dB down goes on a line (a zero does), asleep when both lines are zeros, awake again as a fresh one **to the bit**; the same to the bit in blocks of 64, 48, 1 to 61 in turn and 256 over 70 s (ring, sleep, woken part way through a block), three settings, both sides; `TooSmall` sees nothing at three TONEs; the same with flush-to-zero on or off; on a bus no dry sound, asleep too |
| TYPE | To and from CLEAN: all four carried. To and from TAPE: TIME, FEEDBACK, MIX (TAPE's AGE and PING's TONE start at their defaults). FEEDBACK reads 0 to 100% as CLEAN's (owner, 2026-11-14; it read 0 to 90%), so with CLEAN it is carried to the bit; with TAPE as it reads (50% to 0.42), over 100% becomes 100% |
| Screen | Nothing written for it: the list says "echoes: clean, tape, ping", the TYPE row has three. Stills `fx-ping`, `fx-ping-bus`, each with `--hold-a`; they are in `textviews` at all four sizes |
| Tests | `testPing` in `effects_test`, and the checks for every effect, every delay on a bus, every pair of a family and every tail. 61 mutants of `ping.cpp`, 60 killed (6 lived at first and got tests); the one left moves the moment it falls asleep to the end of the block, which changes no sample (both lines are zeros by then) |
| Cost | 0.02% of a desk core, four runs alike (DELAY 0.02%, TAPE 0.09 to 0.10%, HALL 0.15% in the same runs). The plan guessed 0.06 |

For the owner's ears: (settled 2026-11-14: FEEDBACK now reads 0 to 100% in steps of 5%, doing nine tenths of it, as CLEAN); the sound coming in halved when it is on one side only; TONE at 60% as the place to start; whether the first echo should be on the left.

## 8. Open questions for the owner

| Question | Recommended answer |
| --- | --- |
| Two buses, named ECHO and SPACE? | Yes. Two fits one row and one THROW button. The names are plain and are not effect names |
| Third slot on tracks? | Yes. It fills the page and the cost is small |
| THROW on a drum hit: that hit alone (skips the track's slots), or open the whole track's send for a step (keeps the slots, throws the hat too)? | That hit alone. Lanes share a track here, so the other way throws too much |
| THROW with one fixed amount? | Yes, full. The bus's LEVEL sets how loud |
| Should the old REVERB and DRIVE leave the picker for new songs? | No. Keep them listed after the new ones; decide by ear once HALL and SATURATE exist |
| DUCK's source: only the first drum track? | Yes for now. A choice of track is a fifth thing to set |
| A limiter on by default in new songs? | No. The soft ceiling stays the default; LIMITER is put in by hand |
| Should the dice be able to scatter THROWs? | Later, after THROW has been used by hand |
| Is 5 a fair handheld factor for effects? | Unknown. Run `bench` on the device after step 0 before trusting any budget here |

## Seven families (owner, 2026-11-13)

The list a player picks from becomes seven families, each with a TYPE (left /
right). The owner wants as many effects and types as fit; each gets a cost
figure so the costly ones can be pared down after. Old effects keep their
number, word and sound; they only move under a family name.

| Family | Types |
| --- | --- |
| FILTER | the five today, COMB, VOWEL, EQ |
| DRIVE | DRIVE, SATURATE's curves, FOLD, FUZZ |
| CRUSH | CRUSH, RING, WEAR |
| MOD | CHORUS, FLANGER, ENSEMBLE, PHASER, TREMOLO |
| DELAY | CLEAN, TAPE, PING, DUB, REVERSE, STUTTER |
| REVERB | ROOM, HALL, SPRING, PLATE, BLOOM, SHIMMER |
| LEVEL | COMP, DUCK, LIMITER, SNAP |

- Built in batches of three, a build shipped after each batch.
- STUTTER on the ECHO bus is to be tried; if it is odd there, it stays in the
  family but out of the bus's list.
- Types that share no setting names (FILTER to EQ, COMP to SNAP) start from
  their usual values.
- Order: after the pit stop audit, the colour themes and the mixer.

**Built (2026-11-14, branch `families`):** the list is the seven families in
this order, each line naming its types (the slot's lit), FILTER and CRUSH
saying their words. Every effect today is a type: DRIVE (DRIVE, SATURATE),
MOD (CHORUS), LEVEL (COMP, DUCK) beside DELAY and REVERB; the DIRT and
MOVING groups are gone. Numbers, words and sounds unchanged (the pinned
checksums and `everykind_main.inc` untouched). A later type is a row: its
`EffectFamily`, its `type`, its place in `kList`. The list's line and the
heading's TYPE row were measured against all fifteen planned names at every
text size (`textsize_test`): MOD's five are held at LARGE at XL; the TYPE row
shows as many as fit with "..." (six DELAYs at LARGE do not all fit). The
FX page now goes down to the hints bar. Not built: the fifteen new types.

**After review (2026-11-14):** the list's two rows, the TYPE row's "..." and
the room it keeps for the LEFT / RIGHT key are now drawn in a test with the
planned names and made-up longer ones, at every size, and read off the pixels
(`textsize_test --fx-list`). Two things for the owner, not changed:
- **Two rows say TYPE on one screen** for SATURATE and CHORUS: the heading's
  TYPE row (DRIVE / SATURATE; CHORUS) above the effect's own TYPE setting
  (SATURATE's curve, CHORUS's kind: CHORUS, VIBRATO...). On MOD it reads "TYPE CHORUS"
  twice, two meanings. FILTER will do the same when COMB, VOWEL and EQ join.
  Renaming the setting (CURVE, MODE) would change only what the screen says;
  its number and the song file stay. The owner's call.
- **STUTTER out of the ECHO bus's list is more than a row.** A bus's list is
  a whole family, its type stepped with left / right; nothing in a row keeps
  one type off a bus. If STUTTER is odd there, it needs a field in its row
  that `effectTypes` / `effectTypeAt` honour on a bus (and the carry and the
  bus's TYPE row with them).

**Batch b1 built (2026-11-14, branch `newfx`): FILTER's COMB, VOWEL and EQ.**
Numbers 15, 16, 17; words `comb`, `vowel`, `eq`; in the list after FILTER.

| Type | What it is | Settings | Cost (desk; DRIVE 0.12%, HALL 0.15% in the same runs) |
| --- | --- | --- | --- |
| COMB | the sound fed back on itself one cycle of a note late: it rings at the note and its harmonics | PITCH (25 notes, C2 to C4, a choice), FEEDBACK (-100 to +100%: below 0 hollow, odd harmonics, an octave down, and PITCH then reads the note an octave down, C1 to C3), TONE (a high cut in the loop), MIX | 0.04% (0.07% in one run); 0.08 to 0.10% while PITCH moves |
| VOWEL | four narrow peaks at a bass voice's vowel places, morphing A E I O U | VOWEL (and every shade between), SHIFT (-12 to +12 semitones), RESONANCE (Q 3 to 24, named as FILTER's so TYPE keeps it), MIX | 0.08% (0.11% in one run); 0.13% while VOWEL moves |
| EQ | three bands: a low shelf at 200 Hz, a bell, a high shelf at 5 kHz | LOW, MID, MID FREQ (125 Hz to 8 kHz), HIGH (-12 to +12 dB each; no MIX: every band at 0 dB is the sound untouched, to the bit) | 0.07% (0.09% in one run); 0.10% while MID FREQ moves |

- **They share `fx/svf.hpp`**: the old FILTER's state-variable filter, the
  TICK (what glides is worked out every 16 samples counted from the effect's
  start, so the sound is the same to the bit however it is cut into blocks,
  even while a setting moves), `Slide` (the tick's numbers reached sample by
  sample) and `Gentle` (two Glides one after the other, so a turned setting
  sets off slowly). A band of EQ turned from +12 to -12 dB at once clicked
  with a plain Glide and numbers stepped once a tick: a corner of 790 times a
  sine's (0.28); slid, 34; with Gentle, 2.2.
- **Nothing overlaps an existing choice.** COMB is a tuned resonance, not a
  delay (CLEAN is in time with the song, 1/16 and up; COMB is 4 to 15 ms, a
  note). VOWEL's peaks are not FILTER's BAND PASS: four of them at once, at
  a voice's places, moving together. EQ is the only type with no MIX.
- **For the owner's eyes:** FILTER now has two rows that say TYPE on one
  screen (the heading's FILTER / COMB / VOWEL / EQ, and FILTER's own TYPE:
  LOW 12 DB...), as SATURATE and CHORUS did. COMB's FEEDBACK and EQ's bands
  are signed, but their bars fill from the left like any amount (0 is the
  middle). The list of effects' FILTER line now names its types ("filter,
  comb, vowel, eq") where it said "low, band, high, DJ".
- Stills: `fx-kind-comb`, `fx-kind-vowel`, `fx-kind-eq` (each with
  `--hold-a`, and `-N` on a setting).

**Batch b2 built (2026-11-14, branch `newfx`): DRIVE's FOLD and FUZZ, CRUSH's RING.**
Numbers 18, 19, 20; words `fold`, `fuzz`, `ring`; in the list after SATURATE
(FOLD, FUZZ) and after CRUSH (RING). CRUSH's line now names its types
("crush, ring") where it said "fewer bits".

| Type | What it is | Settings | Cost (desk; DRIVE 0.12%, SATURATE 0.14 to 0.15%, HALL 0.15% in the same runs) |
| --- | --- | --- | --- |
| FOLD | a wavefolder: past the top the wave folds back down, again and again as it is pushed | DRIVE (1.5 to 12 times; SATURATE's name, so TYPE keeps it), SYMMETRY (-100 to +100%: the folds off centre, even overtones), TONE (SATURATE's low-pass, kept to the bit), MIX | 0.58% (0.66% while DRIVE turns; four times the rate: 0.23% at twice, which folded back) |
| FUZZ | a fuzz pedal: turned up until nearly square, one side clipped harder than the other, a GATE that makes a dying note sputter and stop | FUZZ (6 to 48 dB, its own name: starts at 70%), TONE (a Big Muff's: dark, scooped, thin; what it takes from a fuzzed note made up, on average over A1 to A7), GATE (only takes away), MIX | 0.16% (0.20% while TONE turns) |
| RING | a ring modulator: every note comes out as two, the carrier's pitch above and below it, the note itself gone | FREQ (1 Hz to 4 kHz), WAVE (SINE, TRIANGLE, SQUARE), SPREAD (the right carrier up to a quarter turn behind), MIX (CRUSH's, kept) | 0.03 to 0.04% |

- **FOLD is not SATURATE's FOLD.** SATURATE's is a sine of the input: every
  turn rounded, a soft bell. FOLD is a triangle with only the top 30% of each
  fold rounded: steep straight sides, so far more high overtones (harmonics
  10 to 40 of a 220 Hz sine at the same push: -57 to -26 dB of the sound,
  against -160 to -69 for SATURATE's FOLD), twice the push (12 times against
  6), and SYMMETRY, which SATURATE has not.
- **FUZZ is not DRIVE, SATURATE's HARD or TUBE.** Those keep the shape of a
  quiet sound; FUZZ is 48 dB at most and nearly square from 35%; its GATE
  is a step the sound must get past (a note dying away 40 dB a second:
  at GATE 50% nothing over -60 dB after 0.77 s, with no gate 2.0 s), and its TONE is a scoop, not a low-pass.
- **RING has no note-tracking.** An effect is not told the notes played, so
  FREQ is in hertz. Tracking would need the engine to hand the slot the
  track's pitch: more than a row.
- They share `fx/bend.hpp` (FOLD, FUZZ): one side bent at twice the rate
  (FOLD: four times, `BendSide4`, since the review) a sample at a time, so the TICK holds and the sound is the same to the bit
  however it is cut into blocks, even while a setting moves (SATURATE is
  only once at rest), the plain sound made as late to match.
- **A fault found by the tests and mended before it shipped**: RING's square
  read 2 (twice full scale) for one sample whenever the carrier stood at
  0.49999997 of its turn (0.49999997 + 0.5 is 1 in a float).
- **Mended after review (2026-11-14)**, each with a test that fails without it:
  - **FUZZ's TONE made up for a 220 Hz square alone.** At TONE full a high note
    came out 14 dB louder than it went in, peaks past 2. Now made up for the
    share squares A1 to A7 keep, averaged: no note louder than it went in, peak
    0.42 (a 0.3 sine, FUZZ 70%). TONE is still a tone control: at TONE full A1
    comes out 20 dB under A7, at TONE 0 A7 18 dB under A1, as a pedal's does.
  - **FUZZ was matched to a half-scale sine**, so at its usual settings a 0.02
    sine came out 17 dB over DRIVE. A fuzz brings everything to about one level;
    it is now matched to a sine at 0.18: from 0.01 to 0.9, and noise, it is
    within +8.9 and -10.8 dB of DRIVE (louder on quiet sound, quieter on loud).
    **Owner's call: FUZZ on full-scale sound is now about 10 dB quieter than
    DRIVE**; a larger `kFuzzReference` makes it louder, and quiet notes louder still.
  - **FUZZ's level took in the GATE**, so a reference that only just got past
    the step was made up as much as four times over (FUZZ 25%, GATE 50%: a
    half-scale sine 12 dB over its neighbours; a FUZZ turned through there at
    once jumped twice as far as any setting held still). The level is evened
    out without the gate now: GATE only takes away.
  - **FOLD at full DRIVE aliased above about 2.5 kHz** at twice the rate. It
    now bends at four times the rate (`BendSide4`). What folds back under
    19 kHz, against all of it, at full DRIVE: 1 kHz -75 dB (was -54), 2.5 kHz
    -54 (-38), 5 kHz -38 (-19), 10 kHz -21 (-1.4); SATURATE's FOLD at its
    full DRIVE -69, -40, -32, -19. Cleaner than SATURATE's FOLD everywhere now.
    Its stages are settled every sample (at four times, 32 steps a tick took
    a stage from 1e-20 past the smallest float).
  - **RING slept after one tick of exact zeros** and woke with the carrier at
    its top, so drum hits with digital silence between them all met the
    carrier at its top (a 1 Hz ring did nothing, SPREAD never swung), unlike
    the same hits with sound 120 dB down between them. It sleeps after 4 s
    of nothing now (`kRestSeconds`); hits closer than that meet the carrier
    where it has got to.
  - Tests that did not test: RING's WAVE fade (its turns landed where SINE and
    TRIANGLE meet; a WAVE with no fade passed), MIX's glide time (FOLD, FUZZ,
    RING: `mixGlides`), FOLD's and FUZZ's filter snaps (`faintAfterANote`: a
    note, then a faint steady offset).
  - Not changed: RING passes a 1e7 sample on as 4.3e6. The rule zeroes only
    what is past 1e8; old CRUSH does the same, and FOLD and FUZZ clip by nature.
- Stills: `fx-kind-fold`, `fx-kind-fuzz`, `fx-kind-ring` (each with
  `--hold-a`, and `-N` on a setting).

| Type | Measure | Pass |
| --- | --- | --- |
| FOLD | the folder by itself; a 220 Hz sine's harmonics 10 to 40 against SATURATE's FOLD; SYMMETRY's 2nd harmonic; the reference sine's level; 4186 Hz's fold-back; 2.5 and 5 kHz at full DRIVE; 12 kHz at MIX 50%; MIX's glide; a note then a faint offset | the table to 1e-5, no corner; 40 dB more than SATURATE's at every push; under -90 dB at 0, over -30 off it; within -1 / +0.5 dB everywhere; 40 dB under the strongest harmonic; 45 and 25 dB under it (at 2x: 37 and 14); within 0.5 dB of MIX 100%; under 20% of the way in 3 ms, 90 to 99% at 60 to 80 ms; nothing too small |
| FUZZ | the clip by itself; a 220 Hz sine's harmonics at four FUZZ; the reference sine (0.18) at FUZZ 35 to 100%; GATE 50% against off at every FUZZ; a 0.3 sine A1 to A7 at three TONEs; sines 0.01 to 0.9 and noise against DRIVE; TONE on quiet sines; a dying note at three GATEs; 12 kHz at MIX 50%; MIX's glide; a note then a faint offset | 2nd over -35 dB at 35%; within -2 / +1 dB; never louder; none over +1 dB, peak under 0.6, TONE 50% within 5 dB; under +10 and over -12 dB against DRIVE; a scoop of 3 dB at 1 kHz, 15 dB from end to end; GATE ends it 0.3 s and more sooner; in step to 0.3 dB; as FOLD's; nothing too small |
| RING | the carrier at every place and pitch; 1 kHz by 300 Hz; what folds back of a 2.9 kHz carrier; SPREAD at 2 Hz; a click after 4 s of silence, and after 3.9 s; hits 0.25 s apart, zeros or noise 120 dB down between; WAVE changed twice in 2 ms, where SINE and TRIANGLE differ; MIX's glide | never past 1; 0.25 at 700 and 1300 Hz (SQUARE 4/pi of it), nothing at 1 kHz; SINE -62, TRIANGLE -35, SQUARE -28 dB; the right is the left 1/8 s later; the click whole every time, and 0.9 of a turn on at 3.9 s; the two within 1e-5, hits from under 0.1 to whole, SPREAD's right whole on some; SQUARE's own sound after 30 ms, to the bit, never rougher than the carrier; as FOLD's |

**Batch b3 built (2026-11-14, branch `newfx`): CRUSH's WEAR, LEVEL's LIMITER and SNAP.**
Numbers 21, 22, 23; words `wear`, `limiter`, `snap`; in the list after RING
(WEAR) and after DUCK (LIMITER, SNAP). In the list of effects LEVEL LIMITER
is drawn with its family small, as REVERB SPRING is (its meter's mark keeps
7 pixels more).

| Type | What it is | Settings | Cost (desk; DRIVE 0.12 to 0.13%, HALL 0.15%, COMP 0.09 to 0.10% in the same runs) |
| --- | --- | --- | --- |
| WEAR | an old tape or a worn record: the pitch sways, hiss and crackle ride on the sound, now and then it drops away, the top and the bottom worn off | WOW (a 0.55 Hz sway, a 6.3 Hz flutter and a drift that never repeats: 31 cents at most at 100%), DIRT (hiss 23 dB under the sound and about 25 crackles a second at 100%; both ride on the sound and fade 0.15 s after it), DROPS (the chance a 40 ms piece sinks 6 to 24 dB: about one in three at 100%), TONE (the top from 20 kHz to 2.5 kHz, the bottom thinned to 150 Hz). No MIX | 0.15 to 0.16% (0.40% before its loop worked on locals); 0.15% at DIRT 100% and with WOW turning (0.23% in one run of ten) |
| LIMITER | a hard top: nothing comes out over the CEILING, seen 3 ms ahead and turned down before it gets there; always exactly 3 ms late | GAIN (0 to +20 dB), CEILING (-10 to 0 dB, usual -2 dB: the engine's soft ceiling, from 0.8, then never acts), RELEASE (DUCK's, 20 ms to 1 s, carried to the bit). No MIX. Its meter on GAIN, as COMP's | 0.05% (0.09% in one run of ten); 0.12 to 0.13% at +20 dB |
| SNAP | a transient shaper: more or less of each hit's start, more or less of its tail, whatever its level | ATTACK (-100 to +100%: about +12 / -13 dB in a hit's first ms, gone by 30 ms), SUSTAIN (-100 to +100%: about 6 dB on a tail 100 to 250 ms in), MIX (COMP's, carried) | 0.08% (ATTACK turning the same) |

- **Why these settings.** WEAR has no MIX: its swaying sound laid under the
  plain one is a flanger, not wear; every setting at 0 is the sound untouched,
  to the bit. TONE is how worn (the top and the bottom together), one knob for
  "age". LIMITER has no MIX for the same reason as its lateness: a limiter
  under the plain sound lets every peak through. CEILING got the third place
  the plan left "if room". SNAP is the plan's three: two knobs and a blend is
  what a transient designer has.
- **Nothing overlaps an existing choice.** TAPE's AGE sways and dulls the
  ECHOES only; WEAR is an insert on the sound itself, with dirt and dropouts.
  COMP at 20:1 still lets each hit's first milliseconds through (1 ms at its
  fastest, no look ahead) and is over the top on every hit; LIMITER is never
  over by a sample. SNAP listens to how the sound moves, not how loud it is:
  a hit 40 dB down is shaped exactly as a loud one (testSnap), and a steady
  note is left alone (within 0.25 dB).
- **For the owner:**
  - **LIMITER on a track makes that track 3 ms late** against the rest (no
    lateness is made up anywhere in the engine). On the master all is late
    alike, which is what it is for.
  - WEAR's dice are rolled by the count of samples since it woke; it sleeps
    after 4 s of nothing (as RING). Hits further apart than that each start
    the dice afresh, so each hears the same crackles and dropouts at the same
    places. Closer hits hear new ones.
  - WEAR's hiss and crackle follow the sound's level: a quiet passage has
    quiet hiss, and silence none (tape through a noise gate, not a tape left
    running). Rule: silence in, silence out.
  - SNAP at full ATTACK or SUSTAIN bends a steady 100 Hz bass a little
    (harmonics -33 dB of it): its followers ripple at twice the note.
- Stills: `fx-kind-wear`, `fx-kind-limiter`, `fx-kind-snap` (each with
  `--hold-a`, and `-N` on a setting).
- Mutants: 53 tried, 48 killed. Survivors, each looked at: LIMITER's hold one
  sample short and its average one sample short (the last-word clip on the
  ceiling catches the one-sample overshoot, to the bit; the second is the
  same guarantee), its running sum not put back to 0 when nothing is turned
  down (the sum of these floats is exact in a double: kept as a guard);
  SNAP's followers snapped under 1e-20 (it sleeps at 1e-12 first) and its
  sleep not waiting for a tick of nothing (a sound under 1e-12 comes out as
  it went in either way).
- **Mended after review (2026-11-14)**, each with a test that fails without it:
  - **Turning WEAR's WOW dived the pitch.** WOW sets how late the sound is
    read, and on the usual 20 ms glide 0 to 100% swooped 500 cents down (0 to
    30%: 143; 100% to 0: 120 up), a tape-speed dive on every automated step.
    WOW now moves at most all its way in a second (`kWowPerSecond`, through
    `Glide::begin`'s new `most`): turned at once the pitch strays 29 cents at
    worst, no more than it sways at rest (31). 0 to 100% takes 1.1 s.
  - Tests that could not see a fault (each mutant now killed): SNAP's right
    side at MIX between 0 and 100% (left and right the same, to the bit, and
    the plain and shaped laid together); LIMITER's meter is the most of the
    block, not its last sample; WEAR's hiss is two hisses (left and right
    alike by 0.10), not one.
  - Kept: a setting turned just after waking glides on a tick grid set by the
    silence's length (CLAUDE.md, the wake rule), as in every type on the TICK.

| Type | Measure | Pass |
| --- | --- | --- |
| WEAR | a 1 kHz sine's pitch at WOW 100%, 30%, 0 and 1%; its flutter, sway and drift; DIRT on a sine: the dirt's level, crackles counted, half a second after, under 300 Hz; dropouts counted in 40 ms pieces at DROPS 100%, 20%, 0; TONE at 10 kHz, 1 kHz, 40 Hz; hits 2 s apart with zeros or faint noise between, and 4.5 s apart; a click with no DIRT; WOW turned at once 0 to 100%, 100% to 0, 0 to 30%; the left's and right's dirt on one sine | 20 to 45 cents, the mean within 1, 30% a fifth to two fifths of it, 0 none; as smooth as the sine; flutter 1.5 to 5 cents rms, sway over 6, drift over 3; -26 to -12 dB, 8 to 20 a second, 20 dB further down, under 0.08; 80 to 200, 1 to 15, 0, no step over the sine's; under -12 dB, over -2, under -3; new dice near, the same far, the two 1e-4 apart; nothing too small; strays under 1.6 times its sway at rest (0.8 for 30%), there in 0.9 to 1.3 s; alike under 0.5 |
| LIMITER | noise through at GAIN 0, CEILING 0, at 48, 44.1 and 96 kHz; noise, clicks and a sweep at +20 dB at four ceilings and two RELEASEs; a click on a quiet sine; a 100 Hz sine 12 dB in at three RELEASEs; the meter; the time back from 12 dB down; a loud side; a quiet sine after a loud second with zeros or faint noise between; the meter on one block loud at first and quiet at its end | exactly 3 ms late, to the bit; never over the ceiling, to 1e-7; the click at the ceiling, the sine turned down 1.5 ms before, untouched 3.1 ms before; harmonics 2 to 9 under -35 / -50 / -18 dB; 14 dB; under 80 / 150 to 350 / 1500 to 3500 ms; the other side down as much; the same to 1e-6; 14 dB (the block's most) |
| SNAP | a drum (noise falling 60 dB in 0.3 s) at five settings: its first ms, 4 to 6 ms, 20 to 40, 100 to 250; the same 40 dB down; MIX 50%; steady sines at 100 Hz and 1 kHz at every end; a hit on one side; ATTACK and SUSTAIN turned at once under hits; one drum on both sides at MIX 25, 50, 80% | +11 to +12.8, +6.5 to 8.5 dB, falling, the tail untouched; -12.5 to -14; tail +5 to +7 / -5 to -7; within 0.5 dB of the loud one; the two added; within 0.5 dB, harmonics under -25 / -45 dB; the other side up 1.5 times; under 0.25 of the way in 3 ms, 0.85 at 60 to 80 ms, there by 300; the sides the same to the bit, the plain and shaped laid together to 1e-5 |

**Batch c1 built (2026-11-14, branch `newfx2`): MOD's FLANGER, ENSEMBLE, PHASER and TREMOLO.**
Numbers 24, 25, 26, 27; words `flanger`, `ensemble`, `phaser`, `tremolo`; in
the list after CHORUS. They share `fx/mod.hpp` (CHORUS's RATE scale, so RATE,
DEPTH and MIX carry among CHORUS, FLANGER, ENSEMBLE and PHASER to the bit;
FEEDBACK between FLANGER and PHASER; SPREAD between ENSEMBLE and TREMOLO;
TREMOLO's RATE is note lengths, not carried), keep the TICK, and sleep by the
rest rule (4 s of nothing and memory all zeros, as RING).

| Type | What it is | Settings | Cost (desk; DRIVE 0.12%, HALL 0.15%, CHORUS 0.15% in the same four runs) |
| --- | --- | --- | --- |
| FLANGER | the sound and a copy 0.35 to 7.3 ms late, the delay swept by octaves (a triangle rounded at its turns), fed back either way: the jet | RATE (0.1 to 8 Hz), DEPTH (to 4.4 octaves of sweep; narrower above 2 Hz; the reach moves its whole way in a second at most, and the sweep is held to what that reach allows, so RATE or DEPTH thrown never bends the pitch more than 2 Hz at rest does), FEEDBACK (-100 to +100%: 0.95 at the ends; the copy scaled by sqrt(1 - f^2) so noise is as loud at any FEEDBACK), MIX (100% is half and half: the notches all the way down) | 0.07% (the same while DEPTH turns) |
| ENSEMBLE | the string machine's chorus: three copies at one place (9 ms), a third of a turn apart on two waves at once (a slow swirl at RATE and a shimmer at 9.7 times it, at most 10 Hz), darkened from 7 kHz | RATE, DEPTH (swirl 1.8 ms, shimmer 0.14 ms at 100%), SPREAD (the right's waves up to half a turn on), MIX (CHORUS's law) | 0.16 to 0.17% (as CHORUS: six reads between samples a sample; on locals no quicker) |
| PHASER | six allpass stages swept from 640 Hz, 2.6 octaves each way at 100%, laid over the plain sound: three notches; the right a quarter turn behind | RATE, DEPTH, FEEDBACK (-100 to +100%: 0.9 at the ends, scaled as FLANGER's), MIX | 0.09 to 0.11% |
| TREMOLO | the level moved by a wave a note length long, following the tempo | RATE (1/32 to 1 BAR, ten, triplets and 1/8 DOT), DEPTH, SHAPE (SINE, SQUARE, RAMP: the pump, RANDOM: a new level a turn by a roll), SPREAD (the right's wave up to half a turn behind: auto-pan). No MIX: DEPTH 0 is the sound to the bit | 0.07% (0.11% in one run); 0.06% at RANDOM. The plan said 0.02: two sines a sample and a double-precision place in the turn |

- **Each against what CHORUS already has** (for the owner's ears):
  - FLANGER is not CHORUS's FLANGE kind: FLANGE is one copy at 2.2 ms swung
    1.9 ms by a sine (0.3 to 4.1 ms, lingering at the ends), a fixed
    feedback of 0.6, the right a quarter turn behind. FLANGER sweeps 4.4
    octaves at an even speed in pitch, its feedback is yours either way to
    95%, and both sides sweep together (the jet in the middle).
  - ENSEMBLE is not CHORUS's ENSEMBLE kind: that is three copies at 7, 10.5
    and 14 ms, each on one wave at its own speed; their centre wanders (219
    samples at DEPTH 100%, measured) so the pitch drifts. Here the three share
    a place and pull against each other: their centre moves under a sample,
    the pitch stays the note's own, and the shimmer is on top.
  - **Loud at the ends of FEEDBACK** (review, 2026-11-14): noise comes out
    about as loud at any FEEDBACK, but a steady tone on a peak rings up:
    FLANGER +11 dB at +100% (+8.4 at -100%), PHASER +8.5 dB at +100% (+4.7
    at -100%), at MIX 100%. What a flanger and a phaser do; worth knowing
    before turning FEEDBACK to its ends.
  - **The same word twice**: MOD's line says ENSEMBLE as a type, and CHORUS's
    own TYPE setting has an ENSEMBLE too (and FLANGE beside FLANGER). Renaming
    CHORUS's kinds would change only what the screen says (the number and the
    song file stay): the owner's call.
- **TREMOLO is in time, not on the beat.** An effect is told the tempo, not
  where the bar is: its wave starts at its top (RAMP at its bottom) when sound
  arrives after 4 s of nothing, and runs on. Locking it to the bar needs the
  engine to tell the slot where the step is: more than a row.
- **The heading's TYPE row now shows LARGE's choice at every size**
  (`FxScreen::typeRowView`): MOD's five do not all fit at LARGE ("... ENSEMBLE
  PHASER TREMOLO" on TREMOLO), and at SMALL it showed all five: other words,
  which `textsize` fails. At XL, where LARGE's choice does not fit, the row is
  held at LARGE.
- Mutants: 53 tried, 49 killed. Survivors, each looked at: FLANGER's and
  ENSEMBLE's sleep also asking that the line (and ENSEMBLE's high cut) be
  empty, and ENSEMBLE's count of empty samples on its line (the 4 s rest is
  far longer than any of them holds anything: kept as guards); TREMOLO's
  `depth != 0` skip (the same numbers either way: a saving).
- Stills: `fx-kind-flanger`, `fx-kind-ensemble`, `fx-kind-phaser`,
  `fx-kind-tremolo` (each with `--hold-a`, and `-N` on a setting).

| Type | Measure | Pass |
| --- | --- | --- |
| FLANGER | the sweep by itself at its top, bottom, middle, at 4 Hz, and its turns; clicks 25 ms apart at 0.24 Hz; an impulse at DEPTH 0, FEEDBACK +-100%; sines at 312.5 and 625 Hz at MIX 100 and 50%; noise at three FEEDBACKs; noise on one side | 7.352 to 0.348 ms, 1.6 at DEPTH 0, half as far at 4 Hz; even speed in octaves within 15%, the first 2% from the top under a quarter of 2% in the middle; each copy on the sweep to 0.01 octave, 16.7 to 352.9 samples; the copy 0.156, the next +-0.95 of it; the notch under -40 dB, the peak 0 dB, -6 dB at 50%; -2.5 to -5 dB; the other side silent |
| ENSEMBLE | clicks 20 ms apart: the copies' centre and spread at DEPTH 0, 100% at 0.58 Hz and 8 Hz, against CHORUS's ENSEMBLE; noise at SPREAD 0, 50, 100%; what the copies add at 333 Hz and 12 kHz; MIX 50% at 333 Hz; noise at its usual settings | centre 432.67 samples (9 ms and the cut's 0.67), moving under 1.5 samples (CHORUS's 219); spread^2 (swirl^2 + shimmer^2) / 2 within 5%, swinging by 2 swirl x shimmer within 15%; the sides the same to the bit at 0, copies 0.04 and 0.03 alike; 0 and -5.1 dB; +2.32 dB (cos and sin of 22.5 degrees); -1.5 to -3 dB |
| PHASER | sines at the three notches and between at DEPTH 0; between at FEEDBACK -100, 0, +100%; 640 Hz swept at 1 Hz on both sides; noise at three FEEDBACKs; noise on one side at +100% | under -40 dB, 0 dB within 0.05; -4.2, 0, +7.8 dB; from -33 to 0 dB, the right 0.250 s behind; -1 to -2 dB; the other side silent |
| TREMOLO | a steady 0.5 through: SINE at every RATE at 120 and 90 BPM; DEPTH 50%; SQUARE at 1 BAR and 1/32; RAMP at 1/4; RANDOM for 30 s, again, at SPREAD 100%, after a sleep; SPREAD 100 and 50%; a new tempo and RATE; SINE to SQUARE and to RAMP; hits 1.5 s and 6 s apart | 0.5 + 0.5 cos to 7e-6; 0.5 to 1; falls counted, up half the time, 99% to 1% in 187 samples, no step past 0.0128; 0, 0.43, 0.75, 1, 0.02; levels 0.004 to 0.999, the same twice, the right the left's half a turn on, a fresh one's after waking; the sides adding to 1, a quarter turn to 2e-6; a turn of 24000 after, no step past the wave's own; steps under the fade's, no corner as the fade starts; 0 then the top again |

**Batch c2 built (2026-11-14, branch `newfx2`): DELAY's DUB, REVERSE and STUTTER.**
Numbers 28, 29, 30; words `dub`, `reverse`, `stutter`; in the list after PING,
and on the ECHO bus with the rest of the family. They share `fx/echo.hpp`
(TAPE's and PING's eight TIMEs, so TIME carries among PING, TAPE, DUB and
REVERSE to the bit, and from CLEAN by its steps; a crossfade's curve; the 4 s
rest), keep the TICK and count every time in samples: the same to the bit
however the sound is cut into blocks, settings moving too.

| Type | What it is | Settings | Cost (desk; DRIVE 0.12%, TAPE 0.10%, PING 0.02%, HALL 0.15 to 0.21% in the same three runs) |
| --- | --- | --- | --- |
| DUB | a dark echo whose loop is a band (Q 0.7) at TONE: each repeat darker and narrower; past 100% it rises into a held roar about TONE, under a ceiling, and comes down by itself when left alone | TIME (the eight), FEEDBACK (0 to 150%; left alone 30 s, over 85% eases to 85%), TONE (the band, 250 Hz to 4 kHz, read as a percentage so TYPE carries PING's and CLEAN's), MIX | 0.05%; howling 0.05 to 0.07%; TONE turned all the while 0.05 to 0.06% |
| REVERSE | each echo the sound backwards: a hit comes back as a swell rising into it (the first hit after a rest: its peak exactly one TIME after it; a later one 0 to 2 TIMEs after it, by where it fell in its window, and hits in one window come back in reverse order), then forwards, backwards... | TIME (the eight: how long a backwards piece is), FEEDBACK (0 to 100%, doing 90%, as CLEAN's), MIX. Three settings: nothing a fourth would do was worth a knob | 0.03% |
| STUTTER | now and then, by chance, a slice caught and played again in its place, in time: a retrigger | LENGTH (1/32 to 1/2, eight), REPEATS (2, 3, 4, 6, 8 times, the first included), CHANCE (each place: REPEATS slices from waking, rolled by its number), MIX (100% as it comes: the slice in the sound's place) | 0.03 to 0.04% |

- **How each differs from what was there:**
  - DUB against TAPE: TAPE's loop passes 40 Hz to 12 kHz (narrowed by AGE),
    sways (wow), goes to 120%, two sides apart. DUB's is a two-octave band at
    one place: three repeats in it is a muffled thump (what is near 6 kHz
    falls 0.21, 0.064, 0.020, 0.010 of each echo); it goes to 150% so it
    rises into the howl in a second; the howl sits about TONE (turn TONE
    while it howls: the siren); no wow; TIME glides twice as slowly (0.3 s:
    a long swoop); one echo for both sides, in the middle.
  - **DUB's howl is a roar, not one clean note**: with a band this wide many
    of the loop's notes are over 1 at once, so it is a band of sound about
    TONE (all of it within an octave of TONE, its middle within 4 semitones;
    497 Hz for 500, 1918 for 2000). A narrower band would ring one note but
    make every echo thin. The owner's ears decide.
  - REVERSE and STUTTER have no near neighbour (the drum grid's REVERSE step
    effect plays one hit's recording backwards; this is the echo of
    anything, in time).
- **STUTTER on the ECHO bus: kept there.** On a bus it gives back only the
  repeats: what was sent is heard once from its track, and when the dice say
  so its slice comes back REPEATS - 1 more times right after it, as loud as it
  went in, then stops. A THROW of a hit there is "a throw that repeats
  exactly and does not die away" (tested through the engine: a hit sent at
  full, CHANCE 100%, comes back three times a sixteenth apart, the same
  level, then nothing). Odd only in that the dice are the bus's, for
  everything sent to it together, and its places count from the first sound
  after 4 s of nothing on the bus.
- **In time, not on the beat** (as TREMOLO): REVERSE's windows and STUTTER's
  places start where the first sound after a rest is. A loop that starts on
  its beat has them on the beat.
- **Every splice ENDS on an edge.** STUTTER's crossfades (4 ms) run up to a
  slice's start through the 4 ms that came before it, so a hit's attack is
  heard whole every repeat; REVERSE puts the sound that wakes it in the middle
  of a window, so its attack is never in a window's 4 ms fade.
- Tails (testATailAlwaysEnds, 40 BPM, worst setting): DUB 295 s, REVERSE
  406 s, STUTTER 31 s (450 is the promise; TAPE 418, CLEAN and PING 388).
- Mutants: 54 tried, 50 killed. Survivors, each looked at: DUB's sleep also
  asking that its band be empty, and its band emptied on waking (the line is
  zeros for its whole length first: the band has long rung out; guards);
  STUTTER's dice compared with `<=` (the same but where a roll equals CHANCE
  exactly); its 4 ms ring emptied on waking (4 s of nothing filled it with
  zeros already).
- **Review, mended (2026-11-14).** STUTTER's slices were whole rounded
  samples added up; where a step is not whole (133 BPM: 5413.53) its places
  ran half a sample a slice late, and from 9.7 s on (140 BPM: 30 s) the
  downbeat's place caught the offbeat hit. Each edge is now the whole sample
  at or before the exact one, counted from waking: no downbeat lost at 93,
  120, 127, 133, 133.7 or 140 BPM over 45 s to 2 min, every repeat's top
  within two samples of its step. REVERSE leaves up to two windows of silence
  (6 s at 1/2 and 40 BPM), and a bounce's 4 s of quiet cut its last echo off:
  the quiet is 7 s now (`kBounceQuietSeconds`, against
  `kLongestEchoGapSeconds`). REVERSE's "one TIME later" is the first hit's
  alone; a later hit comes back 0 to 2 TIMEs after it (pinned). Pinned too:
  STUTTER's 4 s rest (a 3.5 s gap keeps the places' count, 4.5 s starts
  again) and DUB's fed edge (1e-4 exactly is fed). Left: DUB's ceiling
  straight under 1e-5 (moved to 1e-2 it changes a sample by 1.5e-7 at most:
  not a sound).
- **Not heard by anyone.** Every number here is from the tests; the owner's
  ears decide whether DUB's roar is the sound wanted and whether 4 ms splices
  are smooth enough on a vocal.
- Stills: `fx-kind-dub`, `fx-kind-reverse`, `fx-kind-stutter` (each with
  `--hold-a`, and `-N` on a setting). In the list DELAY REVERSE and DELAY
  STUTTER draw the family small, as REVERB SPRING does; the heading's TYPE row
  shows "... PING DUB REVERSE STUTTER" from PING on.

| Type | Measure | Pass |
| --- | --- | --- |
| DUB | a howl fed 3e-5, 3e-4, or a burst at 34 s; a faint sine at the band's top, FEEDBACK 60%; a click on the right alone, a click in a slot, sound that cancels on a bus; TIME 1/16 to 1/8 with clicks, and to 1/2 under a 1 kHz sine; a click at three tempos and three TIMEs; a steady sine at TONE's place and two octaves either side (25, 50, 75%); noise: what is near 6 kHz echo by echo; FEEDBACK 150% at TONE 25 and 75%: where the howl is, its peak at 10 and 29 s, at 36 and 45 s, when silent, fed again at 70 s; 85% left alone 31 to 40 s against one fed all along; 70 s cut four ways, on a bus too; MIX's glide | stops / howls on / howls again; echoes 1.000 and 0.600 of the last; half on both sides, the sides alike, nothing; on the new TIME from 2.9 s (2 to 4.5), down to 500 Hz and no lower; on the step to the sample, nothing after 0.1 s; 0 dB, -9 dB (-9.8 over at 2 kHz); each under 0.7 of the last; middle within 4 semitones, 85% within an octave; 0.5 to 1.6 and held to 5%; under 0.2 and 1e-3 of it; before 70 s; as loud again; 1e-3 of the echo; the same to the bit; as FOLD's |
| REVERSE | a falling hit, FEEDBACK 50%, at three tempos and three TIMEs, and 40 BPM 1/2; a steady 97 Hz sine's echo alone; a click at a window's start at 40 BPM; a click in a slot on both sides; TIME turned in a window; 30 s cut four ways, on a bus too; MIX's glide | the hit backwards sample for sample, its top at T - 1 (T even) or T, then forwards at 0.45 from exactly 2T, nothing between, both sides; no step over 1.05 of the sine's, no corner over 2.5 (1.99); back 287999 later; each side's own echo; the window keeps its length; the same to the bit; as FOLD's (early 0.009) |
| STUTTER | a falling hit on a bus, CHANCE 100%, two tempos, three LENGTHs, three REPEATS, and 1/2 eight times at 40 BPM; two places in a slot worked out by hand, each side its own sine; MIX 0 with -0 in; a sine in a slot at MIX 100%; CHANCE 0 in a slot and on a bus; 60 places at CHANCE 50% and 20%, and the same after 6 s of nothing; a hit sent through the engine to ECHO; 20 s cut four ways, on a bus too; MIX's glide | exactly the hit REPEATS - 1 more times a slice apart, nothing else; within 1e-6 (2.3e-8); to the bit; the slice again and again to the bit, then the sound; no step over 1.3 of the sine's (1.06); untouched to the bit, nothing; exactly the places whose roll is under CHANCE (29 and 14 of 60), the same again; three repeats as loud as the hit, then nothing; the same to the bit; as FOLD's |
