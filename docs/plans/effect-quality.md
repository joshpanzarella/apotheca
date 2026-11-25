# Effect quality and the CPU budget

**There is no cost cap, and the handheld has little room to spare. The busiest song stuttered on 0.573 (221% of a core), and nothing built since has been timed on the device.**
**More CPU would not make BAND sound better. Elsewhere the upgrades you could hear are mostly cheap, and the costly ones (oversampling, more lines, more taps) buy nothing you would hear.**

**Nothing below has been heard or run on the handheld.** The sound figures come from scratch models outside the repo. Many costs are estimates and are marked as such.

## Is there a cap?

No.

- **No code limits an effect's cost.** Nothing watches the load and no effect has a quality setting. A song that is too heavy just stutters. SPEED TEST shows how close a song is (docs/dev/measured.md:92-98).
- **0.15% is a review line, not a limit.** An effect over it gets its loop moved onto local copies before anyone calls it costly (docs/dev/engine.md:479-484). Nine effects shipped over it. The biggest are FOLD at 0.58% and BLOOM at 0.55%.
- **The owner's call: as many effects as fit, with the costly ones pared down later** (owner, 2026-11-13; docs/plans/effects-plan.md:807-810).
- Synths have cost targets (ACID 0.15%, JAX-EP 1.2%). Effects have none. The only target that binds is for a whole song: the busiest song should average under about 60% of a block on the device (docs/plans/speed-plan.md:160).
- **The sound rules are what really block a quality change, not cost.** The old five effects are pinned to the bit. A song with nothing new in it must not change. Any change to a shipped effect changes old songs, so the owner has to listen to it (speed-plan.md:239).

## The budget

The handheld has 4 Cortex-A53 cores at 1512 MHz. The audio thread and 3 helpers share them (app/realtime.hpp:17). One block is 256 frames at 48 kHz, so **every block has 5.33 ms** (app/bench_run.cpp:656-660). **1% of a handheld core is 53 µs of that block.**

| | Desk | Handheld |
| --- | --- | --- |
| Speed | 1x | **13-20x slower** on synths, 9-16x on effects alone (measured.md:3; speed-plan.md:48) |
| One effect at the 0.15% line | 0.15% of a core | 1.5-3% of a core (80-160 µs a block) |
| Costliest effect, FOLD | 0.58% | 5.4% (measured on 0.573) |
| **Typical song** (8 tracks, 1 effect each, COMP + LIMITER on master) | 417 M instructions a second; effects are 26% of it (measured.md:90) | about 0.4 of a core, of which effects about 0.1. **Estimate, never timed** |
| **Busiest song** (12 tracks, 3 effects each, buses, master) | 1,424 M a second, down from 2,070 M (speed-plan.md:473); effects are about half | **Stuttered on 0.573**: 221% of a core, 92.7% of a block, 360 of 1875 blocks late. The plan's guess for today: ~150% of a core, ~50% of a block with 3 helpers (speed-plan.md:248-252). **Not timed since** |

What it means:

- **A typical song has room.** In a typical song the synths cost more than the effects.
- **The busiest song has no room to spare until it is timed again.** Any cost per effect is multiplied there: 13 COMPs, 8 PLATEs, 8 SATURATEs, 6 HALLs, 6 CHORUSes.
- **Some notes are out of date.** bench_run.cpp:9-11 and measured.md:6 still say the handheld is "a fifth as fast". The effects plan's budget (effects-plan.md:161-215) assumed the desk speed divided by 5. The measured factor is 13 to 20.

## BAND first

**No: more CPU buys BAND nothing you would hear.** Its one real fault is in its maths, and fixing it costs nothing while the settings are still.

**The fault is "cramping"**: a digital bell set near the top of the range gets squeezed. The filter maps 24 kHz to the analogue filter's infinity (`g = tan(pi*f/rate)`, engine/src/fx/svf.hpp:37-41). A BELL also fixes the gain at 24 kHz at exactly 1 (engine/src/fx/band.cpp:65). So a bell at 16 kHz is pulled back to 0 dB by 24 kHz, however wide Q asks it to be.

| Option | What you would hear | Handheld cost | Worth it |
| --- | --- | --- | --- |
| **Matched bell** (Vicanek 2016): change only the BELL case of `numbersOf` (band.cpp:63-66), worked out on the tick in double | The top comes out 1-2 dB fuller, only on bells set above about 8 kHz | **0 while still**; +0.3-0.55% while a setting moves (beginTick returns early at rest, band.cpp:129) | **yes** |
| Run the filter at 96 kHz (SATURATE's halfband stage) | Nothing the matched bell does not already give. The top 0.8 kHz of FREQ goes dull, and the sound comes out 2 samples late | +0.7-1.6%, all the time | no |
| Matched shelves and notch | Nothing: today's shelves are within 1 dB of the analogue up to 8 kHz | +0.3-0.6% while moving | no |
| More bands per slot, or a steeper shelf | The same as BANDs in separate slots | small | no: an effect has only 4 settings (effects.hpp:83), and a fifth SHAPE would change saved ones (band.cpp:44-47) |
| Filter numbers every sample, or the filter in double | Nothing | +2.6-4% while moving | no |

**The matched bell, in numbers.** "Gap" is the worst difference from the analogue bell, up to 20 kHz:

| Bell | Gap today | Gap matched |
| --- | --- | --- |
| 10 kHz, Q 0.7, +12 dB | 4.5 dB | 0.7 dB |
| 16 kHz, Q 1.5 | 6.0 dB | 0.8 dB |
| 20 kHz, Q 4 | 7.1 dB | 1.0 dB |
| 1 kHz | 0.0-0.4 dB | 0.0-0.1 dB |

- **Honestly, the gain is small.** On the speaker you cannot hear it. On headphones with bright material it is an A/B difference, the kind a player fixes by turning GAIN up a click.
- **Risk.** BAND shipped today (0.883), so any song saved since with a BELL changes. Below 2 kHz the change is under 0.1 dB. At the top of bells set above 8 kHz it is 2-7 dB.
- **Three traps:**
  - At 0 dB the bell must still return exactly `{g, 1/Q, 1, 0, 0}` (band.cpp:14).
  - Work it out in double. In float, the bell was 30 dB wrong at 40 Hz with Q 30.
  - Make a cut the lift turned upside down. That keeps testBand's check that a cut is the lift turned upside down (tests/effects_test.cpp:8322-8324).
- It was checked only in a scratch model, not run through testBand.

## Other upgrades worth doing

Costs are % of one handheld core for each copy of the effect. "While moving" means only while a setting glides or a MOD moves it.

### Worth doing

| Effect | Change | What you would hear | Extra handheld cost | Risk |
| --- | --- | --- | --- | --- |
| **PITCH** | Choose each splice where the two waves best match (correlation), and use short crossfades. Today each splice jumps a fixed half GRAIN (pitch.cpp:517-532) | **Clearly, on held notes.** Today 7 of 25 notes dip more than 6 dB, 17-33 times a second. 349 Hz and 784 Hz dip 21-24 dB, close to a dropout. With the change, all stay within 0.1 dB. It also sounds less doubled and flams less. testPitch checks only 440 Hz, which happens to be a lucky note (effects_test.cpp:8359-8392) | +0.5-3%, partly paid back by the shorter crossfades. **The search must be spread over ticks**: done in one go, it takes up to 15% of a block | Low. PITCH shipped today and no preset uses it. Up to 25 ms more delay, which still fits the line (pitch.cpp:483) |
| **PITCH** | A 4-pole low cut on what goes into the line when shifting up (pitch.cpp:520) | Bright sounds shifted up lose their grainy, out-of-tune fizz. Octave-up saw: the fold-back goes from -26..-32 dB to -53 dB | +0.3-1% | Low. It cuts only the wet sound, so MIX 0 stays exact. The filter must follow the sleep rule |
| **FUZZ** | Antiderivative anti-aliasing (ADAA: each sample uses the curve's average between this input and the last) on today's 2x stage (fuzz.cpp:69-77) | Less fizz that does not follow the pitch on high leads, glides and vibrato. 887 Hz saw at FUZZ 70%: -30 dB goes to -52. **Better than 4x** (-41 dB) | +0.8-2% (40-107 µs a block) | 8 factory presets and every FUZZ song change slightly. Needs a small change to bend.hpp:49. testFuzz has no aliasing check yet |
| **HALL** | Bend all 8 lines, not only 2 (hall.cpp:28-29, 52-54), using the tank's method (tank.hpp:278-286, 386-395) | At long DECAY (5-20 s) on pads and piano: fewer single notes ringing on, and a softer wash. At the default 2 s on drums: little. **It also fixes HALL failing the cut-into-blocks test** (engine.md:385) | +0.3-1.3% each; **+2-8% in the busiest song** (6 HALLs) | HALL is on the SPACE bus of every new song (app/project.hpp:421), so every song that sends there changes. How deep to bend is a listening call: level wander is pinned at 4.86 dB (effects_test.cpp:1883) |
| **LIMITER** | Hold the turn-down about 10 ms after a peak before releasing it (limiter.cpp:138-146; kRing at limiter.cpp:52 needs to grow) | Cleaner bass under heavy limiting. 50 Hz at RELEASE 20 ms: 4.5% distortion today (an audible buzz on a sub), gone with the hold. The price is 0.3-1.3 dB less loudness at the same GAIN | **0** (+8-24 KB of memory) | Songs where a LIMITER is turning down change. No preset uses LIMITER. It stays exactly 3 ms late. testLimiter's `shareFast > share` check (:9559) gets rewritten |
| **SNAP** | Read the level as the biggest peak of the last 15-20 ms (a sliding maximum) instead of a peak that starts falling at once (snap.cpp:84) | Low notes stop sounding rough. 50 Hz at SUSTAIN +100%: -27.5 dB (about 4%, a buzz) goes to nothing. The 808 tail's wobble goes from 2.3 dB to 0.2 dB. Hits stay within 0.3 dB | +0.2-0.6% | 3 presets change. Hits under 15 ms apart (a fast roll) are read as one hit |

### Maybe

| Effect | Change | What you would hear | Extra handheld cost | Risk |
| --- | --- | --- | --- | --- |
| BLOOM | Read the swell taps on whole samples while still, and sum them in four running sums (tank.hpp:364-369; speed-plan.md:197) | Nothing | **Saves about 1%** per BLOOM | Its bits change, inaudibly. The landing must not click |
| BLOOM | Two longer smearing stages, 25 and 40 ms (tank.hpp:373-385) | Fixes the flutter of a long BLOOM (2-4 s) over a short DECAY (under 1 s) on drums | under +0.2% | BLOOM songs and the Harmonic Bloom preset change slightly |
| COMP | Wait about 12 ms after each wave top before the turn-down falls (option A, a counter; comp.cpp:121) | Only on held low notes at fast SPEED: 40 Hz at SPEED 0% goes from -33.8 dB to nothing. Many call that thickening a "character" | 0 to +0.4%. A sliding window (option B) would be +8-15% in the busiest song: **no** | The most used effect: 23 presets and both bench songs. testComp's 51 ms release pin (:2654-2656) fails unless the release is shortened to match |
| SATURATE, HARD only | ADAA on the HARD curve (saturate.cpp:68-73) | Only at high DRIVE on high, bright notes: DRIVE 100% goes from -35 to -57 dB. Presets stop at 55%, where it is very likely inaudible today | +0.4-1.2% while HARD is chosen | 5 HARD presets change. The TYPE fade needs a second memory. testSaturate's HARD rows get rewritten |
| FLANGER | Its own 8-point sinc read inside the loop, leaving `lineRead` and `cubicWeights` alone | Only at FEEDBACK near ±100% on bright sounds. Today the jet's top peaks swing 6.6 dB at 10 kHz and 10 dB at 12 kHz; with it, 0.05 dB. At the defaults and in presets: inaudible | +0.3-0.8% | 6 presets change inaudibly. It must be shown to never make the loop louder (the sinc peaks at x1.0006) |
| COMB | A 6-point read, inside comb.cpp only (comb.cpp:118, 165-168) | Only with TONE near full and FEEDBACK near ±100%: neighbouring notes ring equally metallic | +0.07-0.2% | COMB songs change. The shared cubic stays as it is: 7 other effects use it |
| PITCH | Restart the grain on a hit. Build it after the matched splices | Drums and plucks are heard once, not twice (no flam) | +0.1-0.4% | Shifting up, a hit is still one grain late. Model it first |
| VOWEL | Give each peak its own width from the table (vowel.cpp:147-154) | Possibly: A's F3 and F4 become two peaks instead of one hump | 0 | Every VOWEL song changes, and so does what RESONANCE means. **A listening call, not a CPU one** |
| HALL | 6-8 early echoes a side, 5-60 ms apart, read from the gap line HALL already keeps (hall.cpp:185-198) | On drums and voices: the room's size before the wash. More character than quality | +0.1-0.3% | Every SPACE-bus song changes |
| SHIMMER | A separate grain timing for each of the four shifters (tank.hpp:422-426) | Maybe, on one held note at high SHIMMER. It may already be blurred | about 0 | SHIMMER songs and the Glass Choir preset change |
| SPRING | A slow random wobble in each spring's length (spring.cpp:296) | Subtle: two hits no longer give identical boings. A taste call | under +0.2% | SPRING songs change. The noise must count from waking so the result stays the same however it is cut into blocks |
| TAPE | A 6-10 Hz flutter and a slow drift on top of the 0.8 Hz wow (tape.cpp:20-21, 74-76) | Held notes sound closer to a real tape machine. A taste call | about 0 | TAPE is on the ECHO bus of every song, so they all change. testTapeAlwaysEnds (:5800) must still pass |

**If every "worth doing" row were built:**

| Song | Extra cost | Why |
| --- | --- | --- |
| Typical song | about +0.3-1.3% of a core | It has one HALL and one LIMITER |
| Busiest song | **+2-8%** | Only HALL touches it, and this song stuttered last time it ran |

FUZZ, PITCH and SNAP cost only where a song uses them. BLOOM's still-taps change would pay some of it back.

## Not worth it

These spend CPU on nothing you would hear, or are not allowed:

- **Oversampling:** BAND (a linear filter, so 96 kHz buys nothing), SATURATE/FOLD/FUZZ at 4x or 8x, the master's soft ceiling at 2x.
- **More of the same:** more half-band stages, finer reads in CHORUS, ENSEMBLE, PITCH, TAPE and WEAR, a 16-line HALL, 128 BLOOM taps, a second PLATE tank.
- **New heavy engines:** a phase vocoder for PITCH, tape hysteresis for TAPE.
- **True-peak reading** on LIMITER or COMP.
- **The pinned five** (FILTER, DRIVE, CLEAN, ROOM, CRUSH): nothing may change. A better sound there has to be a new effect type with its own appended number.

Scratch models behind the figures, not in the repo:

- /tmp/claude-0/-home-user/6e651755-2a5f-538c-83cf-2f4a940824c5/scratchpad/alias/ (FUZZ, SATURATE)
- /tmp/claude-0/-home-user/6e651755-2a5f-538c-83cf-2f4a940824c5/scratchpad/levelsim/ (LIMITER, SNAP, COMP)
- /tmp/claude-0/-home-user/6e651755-2a5f-538c-83cf-2f4a940824c5/scratchpad/pitch_sim.py and pitch_corr.py (PITCH)