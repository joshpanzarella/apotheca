R"voices(
# DRUM SYNTH's named voices (app/drum_voices.hpp; docs/plans/drumsynth-plan.md, batch 3). Each model
# has its usual voice, CLASSIC, first; these follow it in the VOICE view (hold A + LEFT / RIGHT on
# MODEL). A lane keeps its dials, never a voice's name, so this list may change freely.
#   voice WORD NAME      WORD is the model's song-file word; NAME capitals, 12 at most
#   set PLACE DIAL ...   dials 0..100 by the model's place names; a place not set is the usual
# The note after each set reads the dials as the handheld does. `ctest -R drumsynthapp` fails on a
# line it cannot read, a voice that is the usual or another's, and a model whose voices are too
# alike (testTheNamedVoices). Some are the classic machines' kind; some are odd on purpose.
# NOBODY HAS HEARD ANY OF THEM: they are numbers chosen by their readings.

# ---- KICK --------------------------------------------------------------------------------------
voice kick BOOM
set TUNE 25  DECAY 90  SWEEP 50  BEND 67  DRIVE 20  SNAP 20   # TUNE 45 HZ, DECAY 1.38 S, SWEEP +24 ST, BEND 59 MS, DRIVE 20%, SNAP 20%
voice kick PUNCH
set TUNE 39  DECAY 53  SWEEP 62  BEND 44  SNAP 70  TONE 60  DRIVE 50   # TUNE 56 HZ, DECAY 353 MS, SWEEP +30 ST, BEND 25 MS, SNAP 70%, TONE 60%, DRIVE 50%
voice kick TIGHT
set TUNE 45  DECAY 30  SWEEP 75  BEND 30  SNAP 60  DRIVE 40   # TUNE 62 HZ, DECAY 151 MS, SWEEP +36 ST, BEND 15 MS, SNAP 60%, DRIVE 40%
voice kick SUB DROP
set TUNE 2  DECAY 100  SWEEP 100  BEND 100  SNAP 0  DRIVE 10   # TUNE 31 HZ, DECAY 2.00 S, SWEEP +48 ST, BEND 200 MS, SNAP 0%, DRIVE 10%
voice kick HARD
set TUNE 32  DECAY 56  DRIVE 100  SNAP 90  TONE 90  SWEEP 62   # TUNE 50 HZ, DECAY 395 MS, DRIVE 100%, SNAP 90%, TONE 90%, SWEEP +30 ST
voice kick BREATH
set TUNE 18  DECAY 67  NOISE 90  SNAP 0  SWEEP 25  DRIVE 30   # TUNE 40 HZ, DECAY 592 MS, NOISE 90%, SNAP 0%, SWEEP +12 ST, DRIVE 30%
voice kick CLICKER
set TUNE 100  DECAY 0  SNAP 100  TONE 100  SWEEP 0  DRIVE 0   # TUNE 150 HZ, DECAY 50 MS, SNAP 100%, TONE 100%, SWEEP +0 ST, DRIVE 0%
voice kick LASER KICK
set TUNE 10  DECAY 49  SWEEP 100  BEND 0  DRIVE 70  SNAP 30   # TUNE 35 HZ, DECAY 305 MS, SWEEP +48 ST, BEND 5.0 MS, DRIVE 70%, SNAP 30%

# ---- SNARE -------------------------------------------------------------------------------------
voice snare CRACK
set TUNE 63  DECAY 16  TONE 90  SNAP 100  NOISE 60  SWEEP 29  BODY 34   # TUNE 239 HZ, DECAY 81 MS, TONE 90%, SNAP 100%, NOISE 60%, SWEEP +7 ST, BODY 60 MS
voice snare FAT
set TUNE 29  DECAY 77  TONE 20  NOISE 50  BODY 84  DRIVE 50  SNAP 40   # TUNE 149 HZ, DECAY 502 MS, TONE 20%, NOISE 50%, BODY 299 MS, DRIVE 50%, SNAP 40%
voice snare PICCOLO
set TUNE 100  DECAY 29  TONE 50  SNAP 50  BODY 72  NOISE 30   # TUNE 400 HZ, DECAY 119 MS, TONE 50%, SNAP 50%, BODY 203 MS, NOISE 30%
voice snare BRUSH
set TUNE 42  DECAY 88  NOISE 100  SNAP 0  TONE 40  BODY 0  DRIVE 0   # TUNE 179 HZ, DECAY 698 MS, NOISE 100%, SNAP 0%, TONE 40%, BODY 20 MS, DRIVE 0%
voice snare RING
set TUNE 90  NOISE 10  BODY 100  SWEEP 0  DECAY 37  SNAP 30   # TUNE 348 HZ, NOISE 10%, BODY 500 MS, SWEEP +0 ST, DECAY 151 MS, SNAP 30%
voice snare GRIT
set TUNE 57  DRIVE 100  NOISE 80  TONE 70  DECAY 60  SNAP 80   # TUNE 220 HZ, DRIVE 100%, NOISE 80%, TONE 70%, DECAY 302 MS, SNAP 80%
voice snare LOW SLAP
set TUNE 7  DECAY 29  BODY 72  SWEEP 75  NOISE 35  TONE 10  SNAP 60   # TUNE 110 HZ, DECAY 119 MS, BODY 203 MS, SWEEP +18 ST, NOISE 35%, TONE 10%, SNAP 60%

# ---- TOM ---------------------------------------------------------------------------------------
voice tom LOW
set TUNE 14  DECAY 72  SWEEP 50   # TUNE 81 HZ, DECAY 712 MS, SWEEP +12 ST
voice tom FLOOR
set TUNE 2  DECAY 84  SWEEP 25  DRIVE 60   # TUNE 63 HZ, DECAY 1.11 S, SWEEP +6 ST, DRIVE 60%
voice tom HIGH
set TUNE 65  DECAY 53  SWEEP 33   # TUNE 238 HZ, DECAY 353 MS, SWEEP +8 ST
voice tom SYN TOM
set TUNE 33  DECAY 92  SWEEP 100  BEND 100  SNAP 0   # TUNE 121 HZ, DECAY 1.49 S, SWEEP +24 ST, BEND 200 MS, SNAP 0%
voice tom LOG
set TUNE 76  DECAY 13  SNAP 80  SWEEP 8  TONE 70   # TUNE 301 HZ, DECAY 81 MS, SNAP 80%, SWEEP +2 ST, TONE 70%
voice tom WARP
set TUNE 7  DECAY 100  SWEEP 100  BEND 100  NOISE 100  DRIVE 40   # TUNE 70 HZ, DECAY 2.00 S, SWEEP +24 ST, BEND 200 MS, NOISE 100%, DRIVE 40%
voice tom CONGA
set TUNE 79  DECAY 44  SWEEP 17  BEND 19  SNAP 50  TONE 50   # TUNE 320 HZ, DECAY 253 MS, SWEEP +4 ST, BEND 10 MS, SNAP 50%, TONE 50%

# ---- CLAP --------------------------------------------------------------------------------------
voice clap TIGHT
set TUNE 52  DECAY 32  SPREAD 22  SNAP 90   # TUNE 1.5 KHZ, DECAY 148 MS, SPREAD 5.0 MS, SNAP 90%
voice clap ROOM
set TUNE 36  DECAY 93  SPREAD 60  SNAP 50   # TUNE 993 HZ, DECAY 1.18 S, SPREAD 12 MS, SNAP 50%
voice clap CROWD
set TUNE 32  DECAY 73  SPREAD 92  TONE 100  SNAP 60   # TUNE 898 HZ, DECAY 599 MS, SPREAD 25 MS, TONE 100%, SNAP 60%
voice clap FINGER SNAP
set TUNE 73  DECAY 14  SPREAD 0  TONE 20  SNAP 100   # TUNE 2.5 KHZ, DECAY 80 MS, SPREAD 3.0 MS, TONE 20%, SNAP 100%
voice clap DRAG
set TUNE 22  DECAY 61  SPREAD 100  DRIVE 80  SNAP 70   # TUNE 697 HZ, DECAY 398 MS, SPREAD 30 MS, DRIVE 80%, SNAP 70%
voice clap PAPER
set TUNE 91  DECAY 47  SPREAD 37  TONE 80  SNAP 40  DRIVE 0   # TUNE 4.0 KHZ, DECAY 247 MS, SPREAD 7.0 MS, TONE 80%, SNAP 40%, DRIVE 0%

# ---- CL HAT ------------------------------------------------------------------------------------
voice clhat TICK
set TUNE 40  DECAY 25  TONE 80   # TUNE 205 HZ, DECAY 25 MS, TONE 80%
voice clhat METAL
set NOISE 0  TUNE 77  DECAY 49  TONE 40   # NOISE 0%, TUNE 397 HZ, DECAY 61 MS, TONE 40%
voice clhat SHAKER
set NOISE 100  DECAY 67  TONE 50  SNAP 0   # NOISE 100%, DECAY 118 MS, TONE 50%, SNAP 0%
voice clhat DIRTY
set DRIVE 100  DECAY 56  NOISE 60   # DRIVE 100%, DECAY 79 MS, NOISE 60%
voice clhat GLASS
set TUNE 100  NOISE 10  TONE 100  DECAY 38  SNAP 60   # TUNE 600 HZ, NOISE 10%, TONE 100%, DECAY 41 MS, SNAP 60%
voice clhat PEDAL
set TUNE 26  DECAY 78  TONE 0  NOISE 40  SNAP 0   # TUNE 159 HZ, DECAY 178 MS, TONE 0%, NOISE 40%, SNAP 0%

# ---- OP HAT ------------------------------------------------------------------------------------
voice ophat LONG
set DECAY 80   # DECAY 1.52 S
voice ophat HALF OPEN
set DECAY 20  SNAP 50   # DECAY 197 MS, SNAP 50%
voice ophat SIZZLE
set NOISE 90  TONE 90  DECAY 68   # NOISE 90%, TONE 90%, DECAY 1.01 S
voice ophat GONG
set TUNE 10  NOISE 0  TONE 0  DECAY 95  SNAP 0  DRIVE 30   # TUNE 120 HZ, NOISE 0%, TONE 0%, DECAY 2.53 S, SNAP 0%, DRIVE 30%
voice ophat TRASH
set DRIVE 100  NOISE 70  DECAY 61   # DRIVE 100%, NOISE 70%, DECAY 796 MS
voice ophat TSS
set NOISE 100  SNAP 0  TONE 0  DECAY 27   # NOISE 100%, SNAP 0%, TONE 0%, DECAY 251 MS

# ---- CYMBAL ------------------------------------------------------------------------------------
voice cymbal RIDE
set TUNE 70  DECAY 86  NOISE 30  SNAP 10  TONE 70   # TUNE 351 HZ, DECAY 3.94 S, NOISE 30%, SNAP 10%, TONE 70%
voice cymbal CHINA
set TUNE 23  DECAY 71  DRIVE 90  NOISE 40  SNAP 60   # TUNE 151 HZ, DECAY 2.52 S, DRIVE 90%, NOISE 40%, SNAP 60%
voice cymbal SPLASH
set TUNE 77  DECAY 23  SNAP 80  TONE 70   # TUNE 397 HZ, DECAY 598 MS, SNAP 80%, TONE 70%
voice cymbal WASH
set NOISE 100  DECAY 100  SNAP 0  TONE 40   # NOISE 100%, DECAY 6.00 S, SNAP 0%, TONE 40%
voice cymbal BELL
set TUNE 90  DECAY 77  NOISE 0  TONE 40  SNAP 20   # TUNE 502 HZ, DECAY 3.01 S, NOISE 0%, TONE 40%, SNAP 20%
voice cymbal DARK
set TUNE 5  DECAY 82  TONE 0  NOISE 20  SNAP 0   # TUNE 109 HZ, DECAY 3.50 S, TONE 0%, NOISE 20%, SNAP 0%

# ---- RIM ---------------------------------------------------------------------------------------
voice rim STICK
set TUNE 83  DECAY 23  TONE 0  SNAP 100   # TUNE 2.4 KHZ, DECAY 20 MS, TONE 0%, SNAP 100%
voice rim WOODY
set TUNE 9  DECAY 69  TONE 80  DRIVE 30   # TUNE 901 HZ, DECAY 79 MS, TONE 80%, DRIVE 30%
voice rim BLOCK
set TUNE 31  DECAY 90  TONE 20  DRIVE 0  SNAP 40   # TUNE 1.2 KHZ, DECAY 148 MS, TONE 20%, DRIVE 0%, SNAP 40%
voice rim PING
set TUNE 100  DECAY 100  TONE 0  SNAP 0  DRIVE 0   # TUNE 3.0 KHZ, DECAY 200 MS, TONE 0%, SNAP 0%, DRIVE 0%
voice rim CRUNCH
set TUNE 48  DECAY 54  TONE 60  DRIVE 100  SNAP 90   # TUNE 1.5 KHZ, DECAY 50 MS, TONE 60%, DRIVE 100%, SNAP 90%

# ---- COWBELL -----------------------------------------------------------------------------------
voice cowbell SHORT
set DECAY 25   # DECAY 80 MS
voice cowbell HIGH
set TUNE 79  DECAY 54   # TUNE 897 HZ, DECAY 248 MS
voice cowbell CLANK
set TUNE 5  DECAY 77  DRIVE 60  TONE 20   # TUNE 322 HZ, DECAY 610 MS, DRIVE 60%, TONE 20%
voice cowbell AGOGO
set TUNE 94  DECAY 41  TONE 80  SNAP 60   # TUNE 1.1 KHZ, DECAY 149 MS, TONE 80%, SNAP 60%
voice cowbell ALARM
set TUNE 61  DECAY 100  DRIVE 100  SNAP 0   # TUNE 699 HZ, DECAY 1.50 S, DRIVE 100%, SNAP 0%

# ---- CLAVE -------------------------------------------------------------------------------------
voice clave WOODBLOCK
set TUNE 29  DECAY 61  SNAP 60   # TUNE 1.1 KHZ, DECAY 61 MS, SNAP 60%
voice clave TINK
set TUNE 98  DECAY 17  SNAP 20   # TUNE 4.8 KHZ, DECAY 10 MS, SNAP 20%
voice clave DROP
set TUNE 7  DECAY 100  DRIVE 40  SNAP 10   # TUNE 696 HZ, DECAY 300 MS, DRIVE 40%, SNAP 10%
voice clave BLIP
set TUNE 57  DECAY 0  SNAP 0   # TUNE 2.0 KHZ, DECAY 5.0 MS, SNAP 0%
voice clave BUZZ
set TUNE 43  DECAY 78  DRIVE 100  SNAP 50   # TUNE 1.5 KHZ, DECAY 122 MS, DRIVE 100%, SNAP 50%

# ---- ZAP ---------------------------------------------------------------------------------------
voice zap PEW
set TUNE 74  DECAY 42  SWEEP 60  BEND 60   # TUNE 402 HZ, DECAY 123 MS, SWEEP +36 ST, BEND 40 MS
voice zap DIVE
set TUNE 8  DECAY 95  SWEEP 100  BEND 100  DRIVE 40   # TUNE 40 HZ, DECAY 1.21 S, SWEEP +60 ST, BEND 300 MS, DRIVE 40%
voice zap BLOOP
set TUNE 54  DECAY 47  SWEEP 20  BEND 46  DRIVE 0   # TUNE 199 HZ, DECAY 152 MS, SWEEP +12 ST, BEND 20 MS, DRIVE 0%
voice zap ROCKET
set TUNE 20  DECAY 100  NOISE 80  SWEEP 80  BEND 100  DRIVE 50   # TUNE 60 HZ, DECAY 1.50 S, NOISE 80%, SWEEP +48 ST, BEND 300 MS, DRIVE 50%
voice zap CHIRP
set TUNE 97  DECAY 16  SWEEP 40  BEND 18   # TUNE 900 HZ, DECAY 40 MS, SWEEP +24 ST, BEND 4.9 MS
voice zap DROP KICK
set TUNE 12  DECAY 75  SWEEP 100  BEND 74  DRIVE 80   # TUNE 46 HZ, DECAY 510 MS, SWEEP +60 ST, BEND 82 MS, DRIVE 80%

# ---- NOISE -------------------------------------------------------------------------------------
voice noise HISS
set TUNE 92  DECAY 64  TONE 100   # TUNE 10.0 KHZ, DECAY 297 MS, TONE 100%
voice noise WIND
set TUNE 33  DECAY 100  TONE 0  SNAP 0   # TUNE 813 HZ, DECAY 2.00 S, TONE 0%, SNAP 0%
voice noise SWISH
set TUNE 64  DECAY 70  SNAP 0  TONE 50   # TUNE 3.0 KHZ, DECAY 408 MS, SNAP 0%, TONE 50%
voice noise STATIC
set TUNE 71  DECAY 34  DRIVE 100  SNAP 100   # TUNE 4.1 KHZ, DECAY 61 MS, DRIVE 100%, SNAP 100%
voice noise STEAM
set TUNE 96  DECAY 83  TONE 80  SNAP 60   # TUNE 11.8 KHZ, DECAY 813 MS, TONE 80%, SNAP 60%
voice noise THUD
set TUNE 5  DECAY 47  TONE 30  SNAP 100  DRIVE 60   # TUNE 247 HZ, DECAY 121 MS, TONE 30%, SNAP 100%, DRIVE 60%
)voices"
