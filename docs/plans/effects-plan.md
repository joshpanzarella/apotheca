# Effects plan

Written 2026-11-11. Step 0 (groundwork), step 4 (TAPE) and step 5 (HALL) are built; see "as built" under the order of work.

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
| A note | The track's send opens for the note's length, with 5 ms ramps | Other notes sounding on that track at that moment are thrown too. The synths do not hand out single voices, and Phazed's engine is not ours to change |

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
| 1a | **Sends in the engine**: buses, `SetSend`, bus effects, lanes to 24, `kTrackSlots` 3 | large | 0 | `engine.hpp/.cpp` |
| 1b | Sends in the song file and Session: `send`, `busfx`, `fx 3` | medium | 1a | `song_file.cpp`, `session.cpp`, `project.hpp` |
| 1c | The FX screen's two pages, group jump | medium | 1b | `fx_screen.cpp/.hpp` |
| 2 | **THROW on drum hits** | medium | 1a, 1b | `drums.hpp/.cpp`, `engine.cpp`, `pattern_screen.cpp`, `song_file.cpp` |
| 3 | THROW on notes | medium | 1a, 1b | `engine.cpp`, `note_screen.cpp`, `song_file.cpp` |
| 4 | **DONE 2026-11-11.** TAPE | medium | 0 | `fx/tape.cpp` |
| 5 | **DONE 2026-11-11.** HALL | medium | 0 | `fx/hall.cpp` |
| 6 | CHORUS | small | 0 | `fx/chorus.cpp` |
| 7 | COMP | medium | 0 | `fx/comp.cpp`; the meter in `fx_screen.cpp` |
| 8 | SATURATE, with a shared 2x header | medium | 0 | `fx/saturate.cpp`, `fx/halfband.hpp` |
| 9 | DUCK | medium | 0, and a small hook in the engine | `fx/duck.cpp`, `engine.cpp` |
| 10+ | The later set, one at a time | small each (BLOOM, STUTTER medium) | 0; BLOOM needs 5, PING needs 4 | one file each |

**Step 0 as built:**

| Thing | Result |
| --- | --- |
| The old five | The same to the last bit after the move: 40 pinned renders and one whole song, on this machine and as ARM programs under emulation |
| Adding an effect | One new file, one `extern` line, one number, one row, one place in the list (`engine/src/fx/unit.hpp` lists the steps) |
| The fade on a swap | In. 10 ms, both effects running. No swap of the old five is rougher than the effect is by itself |
| Flush-to-zero | **Built, tested, and left OFF.** On, it changes 1 of the 40 pinned renders (25 samples, all under 1e-38) and the end of any long tail (under 1e-30, 600 dB down). Nothing anyone could hear, but not "the same to the last bit". **The owner decides**; it is one call, `Engine::setFlushDenormals(true)`, in `app/main.cpp` and `app/bounce.cpp`. The old five do not slow down without it (measured). HALL will need it |
| Measured with the new tools | REVERB's RT60 0.59 / 1.44 / 10.8 s at SIZE 0 / 60 / 100%. DRIVE at 81%, a 4186 Hz sine: the loudest fold-back 9 dB under the third harmonic, all of it together 16 dB under the sine. None of the old five glides a setting: FILTER TYPE, DELAY TIME and CRUSH jump when turned |
| Not done | `bench` has not been run on the handheld. Nobody has heard the fade |

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
