# The clips: short videos of the features (desk only)

The owner asked for them on 2026-11-28: one short clip per feature, the real app on the desk, the screen sharp at
640x480, the real sound, and the keys shown as they are pressed. **Nothing here runs on the handheld**; the code is
in the handheld's program too (main.cpp's `--script`), but only the desk ever uses it.

| Job | Command |
| --- | --- |
| Every clip | `scripts/make-clips.sh` (into `videos/` at the checkout's top; `VIDEOS=folder` for elsewhere, `BUILD=` for another build) |
| Some clips | `scripts/make-clips.sh 01 fx` (a number or any part of the name) |
| One recording, by hand | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/apotheca --demo --view NAME --script clips/NN-name.clip --record OUTDIR` |
| The tests | `clip_test run build/apotheca` (ctest `clip`) |

`videos/` is in `.gitignore`: the clips, their `.md` and `.srt` are never committed. The scripts are, in `clips/`.

## What a clip is

`clips/NN-name.clip` becomes `videos/NN-name.mp4`: H.264, yuv420p, 30 frames a second, AAC sound. The picture is
1280x1120: **the screen doubled with nearest-neighbour** (1280x960, every pixel a crisp 2x2) and **a band of
160 pixels under it** that shows the keys. The screen itself is exactly what the handheld draws; nothing is drawn over
it. Beside each MP4: `NN-name.md` (title, a caption for a post, the narration with its times) and `NN-name.srt` (the
same narration as subtitles). Both come from the script's `say` lines, so they are in step with the picture by
construction.

## The script

One thing a line; `#` starts a comment. Times are seconds from the start of the run.

```
title THE DRUM GRID                  # the .md's title
about A places, hold A shapes...     # the .md's caption line
caption DRUMS                        # the band's word at the left (a "T caption WORDS" line changes it at T)
run --demo --view drums              # the program's options (make-clips.sh reads it; $SAMPLES, $STEREO: below)
from 1.0                             # recorded from here: what comes before runs, unseen and unheard
length 17.0                          # and up to here

1.2 say The drum grid, with the song playing.   # narration: until the next say (six seconds at most)
2.0 tap RIGHT 2 0.3                  # RIGHT, two taps 0.3 s apart (each down for 0.1 s)
2.8 tap A
5.4 hold A 2.8                       # A held for 2.8 s, shown HLD-A from the press
5.8 tap DOWN 4 0.3
9.1 press UP 0.5                     # held 0.5 s without the HLD- mark (a run of the cursor)
11.4 stick UP 0.3 [0.5]              # the stick pushed up for 0.3 s (all the way, or as far as the last number)
3.5 down SEL / 3.6 up SEL            # a button by itself (down X hold: marked a hold)
```

Buttons are named as `controls.txt` names them (`buttonName`): UP DOWN LEFT RIGHT A B X Y L1 R1 L2 R2 START SELECT
(or SEL) MENU L3. Each press goes to the app through the keyboard stand-ins of `input.cpp`, so the app takes it the
way it takes the desk's keyboard; the stick goes in as a gamepad axis. `OUTDIR/presses.txt` lists each press as the
app got it, with its time in milliseconds.

The clip starts on its screen with the stills' setup: `--view NAME` (`app/stills.cpp`, `docs/dev/stills.md`). Where
no view is quite right, the script walks there before `from`, unrecorded (03 goes from SONG to the pad's DEVICE page
that way). `$SAMPLES` in `run` is a folder of recordings made by `scripts/clip_samples.py` (a mono drum break and a
stereo ping-pong), `$STEREO` one holding only the ping-pong.

## How the picture and the sound stay in step

**In a recording the sound card is never started.** The app thread does all of it, on a virtual clock:

1. The run is cut into passes of 1/60 s, two to a frame (main.cpp's passes are about 60 a second too, so held keys
   repeat at their own pace). Pass p is at exactly p/60 s.
2. The pass's presses go in, stamped with that time, and `App::update` is given it.
3. **main.cpp's own audio callback is called** for exactly rate/60 frames (800 at 48 kHz, in two blocks of 400, about
   the card's 384). What it rendered comes back through an `AudioTap`, a lock-free ring the callback writes into
   (it never waits or allocates; a full ring drops and counts, and a recording with a drop fails), and goes into the
   WAV.
4. On every other pass the screen is drawn and kept, then the band.

So frame k is at k/30 s and its sound begins at sample k x 1600: **a press at T is heard from sample T x 48000**.
Nothing depends on how fast the desk is (a 17-second clip records in about 6 seconds). `clip_test` holds it: X tapped
at 0.5 s is silent to the sample before 24,000 and loud within 10 ms of it, A at 1.0 s the same at 48,000, and the
WAV is exactly the frames' length.

What does not follow the virtual clock: a bounce (clip 10) runs on its own thread at the desk's speed, so its
WORKING and TAIL go by at whatever pace the desk gives (the clip shows a real bounce, not a timed one), and the FX
screen's picture glides between steps by `SDL_GetTicks` (it moves by steps in a clip).

## The band

Drawn per frame from the script's own timeline (`KeyBand` in `app/clip_record.cpp`), in the theme's colours: the
background, the clip's caption at the left in the device's lettering, then the combo as the hint bar's cream chips
(`ui::drawKey`, so HOLD is HLD- and directions are arrows). Its lettering is a size up from XL (20 and 22 points) so
the chips read on a phone.

- A key lights the moment it goes down and stays lit while held; let go, its chip goes dim while the rest of the
  combo is still held.
- **HLD- is shown from the press for a key the script says is held** (`hold`, or `down KEY hold`), not after the
  app's own hold time: the script knows. A `tap` or `press` never shows it.
- Keys pressed while another is still down join the combo: `HLD-A + ▼ + ▲`. When all are let go the combo stays,
  fading over 0.7 s, and the next press starts a new one.
- The stick shows as `STICK` and an arrow while pushed.

## The clips

| Clip | Shows |
| --- | --- |
| 01-drum-grid | A placing hits, hold A + down / up (level) and right (nudge), Y on a hit (chance), B, Y on an empty step |
| 02-piano-roll | A note placed and made longer, a chord picked with Y held, up to TEMPO, A tapped in time, hold A + up |
| 03-synth-main | Phazed Strings' MAIN page: TONE, MOTION and DETUNE turned with the stick, OCTAVE clicked, B |
| 04-fx | The FILTER's cutoff turned, the dials walked, effects tried in the list with A held, TYPE, A + B |
| 05-mod | Hold X on the bass's TONE: UP makes an LFO, DEPTH, RATE, SHAPE; the MOD tab, DEPTH there |
| 06-song | Scenes playing, R1 copies a cell and R1 pastes it on scene 4, the mixer (hold A on a head) |
| 07-kit | SOURCE: the snare to SYNTH, back to BUILT IN, to SAMPLE (the list); its EDIT: END, PITCH, REVERSE, X, B |
| 08-catch-slices | Hold Y (1 bar), a tap of Y catches, L1 opens the slices, hold A + right on the name (AT HITS), R1 drag |
| 09-fill | L1 + Y makes the FILL, a roll painted on it with R1, it plays on the last time round, FILL OFF / ON |
| 10-bounce | SET's BOUNCE row: what, A, WORKING (TAIL too, if the desk is slow enough to show it), done; hold A + down for an earlier TAKE |
| 11-stereo | A stereo ping-pong on lane 6 (the drums soloed), EDIT's STEREO box, MONO and back |

The web pages (the files page, the sound designer) are a browser's, not the handheld's: no clip.

Every clip is checked by eye before it is kept: a few frames read back from the MP4 (each shows what its name says),
and `ffmpeg -af volumedetect` (none is silent). 11's sound shows MONO on the file: its two sides are the same, to the
sample, from MONO's press to STEREO's.
