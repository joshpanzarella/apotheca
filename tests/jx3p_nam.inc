// THE BOOKLET AS READ (the plan, docs/plans/jx3p-plan.md, section 4, batch 7): the maker's
// 1983 booklet of the programmer's settings for 32 sounds (NAM-0150, a scan), a line a
// sound, in the booklet's own terms: what each panel drawing shows, the number printed on
// each knob. jx3p_presets_test reads this and holds the engine's table to it, value for
// value. READ TWICE (2026-11-14): once from the scan at 400 dots an inch, a panel at a time,
// and once more from another rendering of it (170 dots an inch, two panels a picture); the
// two readings agree on every value. A program also read each knob's pointer off the
// drawing (564 knobs): 0.07 of a dial step from the printed number on average and 0.35 at
// worst, but for TUNE and FINE (a ring is drawn round them: 0.6 at worst); and the RANGE,
// WAVE and CROSS MOD knobs' angles, which is where PUL, SQU and B14's METAL come from.
//
//   DCO-1: RANGE WAVE LFO ENV | DCO-2: RANGE WAVE CROSS-MOD TUNE FINE LFO ENV |
//   LFO-DEPTH ENV-DEPTH POLARITY | MIX HPF CUTOFF RESONANCE LFO-MOD ENV-MOD POLARITY KEY-FOLLOW |
//   VCA-MODE LEVEL | LFO: WAVE DELAY RATE | A D S R | CHORUS
//
//   _    NOT DRAWN in the booklet (a blank knob or switch). The engine keeps 8', SAW, OFF,
//        5.0, off, NORMAL there. None can be heard as the patch stands, but the VCF's
//        POLARITY of A5 and A16 (ENV MOD 0.4 and 0.5): NORMAL there is a guess.
//   PUL / SQU   A GUESS wherever SQU stands: the two are told apart by the wave knob's angle
//        only (A4, A8, A9, A10, A12, A14, B2, B3, B7, B12, B13, B15 could be swapped).
//   B14's METAL: its pointer is at a place with no label, where A13's says Metal. A guess.
//   N / I   the envelope's polarity, NORMAL or INVERTED.
// Not to be changed without the scan in hand.
    {"A1", "String I", "16 SAW off off | 16 SAW OFF 4.9 6.1 off off | 3.2 3.4 _ | 4.7 0.6 5.3 0 0 0.8 N 6.7 | ENV 3.4 | SINE 0.8 7.8 | 1.9 2.8 8.4 2.7 | on"},
    {"A2", "String II", "8 SAW off off | 8 SAW OFF 5.0 3.2 off off | 1.9 0 _ | 4.9 0 6.0 0 0 0.6 N 4.6 | ENV 2.7 | SINE 1.8 8.1 | 2.4 2.4 7.6 2.1 | on"},
    {"A3", "Organ I", "16 SAW off off | 16 SAW OFF 5.0 2.3 off off | 4.0 0 _ | 4.8 0 2.4 10 0 0.9 N 9.8 | GATE 4.4 | SINE 2.4 7.1 | 0 0 0 0 | on"},
    {"A4", "Organ II", "16 SQU off off | 4 SQU SYNC 5.1 2.8 off off | 3.6 0 _ | 4.8 0 3.2 4.8 0 1.3 N 9.8 | GATE 3.6 | SINE 0.2 5.8 | 0 0 0 0 | on"},
    {"A5", "Organ III", "16 SAW off off | 4 SAW SYNC 5.0 4.3 off off | 4.7 0 _ | 7.4 0 4.8 0 0 0.4 _ 5.1 | ENV 4.8 | SINE 1.6 7.9 | 1.5 7.5 7.8 2.1 | on"},
    {"A6", "Brass I", "16 SAW off off | _ _ _ _ _ _ _ | 4.7 0.3 _ | 0 0 0 0 0 10 N 4.1 | ENV 3.8 | SINE 1.2 5.9 | 1.6 1.9 6.1 1.4 | off"},
    {"A7", "Brass II", "16 SAW off off | _ _ _ _ _ _ _ | 5.7 0 _ | 0 1.6 0 0 0 5.6 N 4.3 | ENV 3.7 | SINE 0 5.8 | 1.8 2.6 4.9 2.3 | on"},
    {"A8", "E. Piano I", "8 SAW off off | 8 SQU SYNC 2.8 3.9 off on | 0 5.1 N | 9.8 5.8 0 4.9 2.4 4.5 N 3.4 | ENV 9.9 | SINE 0 1.1 | 0 7.5 5.2 1.2 | off"},
    {"A9", "E. Piano II", "8 SAW off off | 8 SQU SYNC 1.4 3.8 off off | 2.4 0 _ | 10 3.8 4.1 1.6 0 1.0 N 3.8 | ENV 5.1 | SINE 0.7 8.1 | 0 4.5 2.7 2.2 | on"},
    {"A10", "Clavi", "16 SQU off off | 16 SQU SYNC 0.9 1.2 off off | 1.4 0 _ | 10 4.5 2.4 0 0 5.0 N 5.0 | ENV 3.2 | SINE 0.5 5.6 | 0 7.1 1.7 1.1 | off"},
    {"A11", "Harpsichord", "8 SAW off off | 4 SAW OFF 5.0 3.0 off off | 1.2 0 _ | 6.8 9.2 10 6.1 0 1.4 N 5.0 | ENV 8.5 | SINE 0 7.8 | 0 4.7 0 0.9 | off"},
    {"A12", "Vibraphone", "8 SQU off off | 4 SQU OFF 7.9 4.5 off off | 3.6 0 _ | 5.4 0 1.6 0 2.4 3.0 N 4.0 | ENV 5.4 | SINE 0 4.9 | 0 6.6 0 6.3 | off"},
    {"A13", "Chime", "4 SAW off off | 4 SAW METAL 1.6 4.0 off off | 0 0 _ | 10 0 0 1.1 0 4.4 N 9.9 | ENV 4.5 | SINE 2.3 8.4 | 0 9.1 0 7.2 | on"},
    {"A14", "Celesta", "8 SQU off off | 4 SQU OFF 10 10 off off | 5.5 4.3 _ | 9.4 9.8 2.8 0 0 4.6 N 10 | ENV 4.5 | SINE 2.4 6.3 | 0 3.0 0 3.8 | off"},
    {"A15", "Accordion", "16 SAW off off | 16 SAW OFF 10 6.6 off off | 0 5.0 _ | 7.8 10 6.0 2.4 1.4 0.8 N 2.9 | ENV 4.1 | SINE 0 0.3 | 1.2 10 7.2 0.5 | on"},
    {"A16", "Voice", "16 PUL on on | _ _ _ _ _ _ _ | 4.5 4.9 N | 0 4.5 3.6 7.9 0 0.5 _ 5.0 | ENV 10 | SINE 4.6 8.5 | 1.6 3.4 9.8 0 | off"},
    {"B1", "Violin", "8 SAW on on | _ _ _ _ _ _ _ | 0.5 0 N | 0 0 7.1 0 0.3 0 _ 3.8 | ENV 2.4 | SINE 3.2 8.6 | 2.8 3.1 9.3 1.7 | off"},
    {"B2", "Flute", "4 SAW off off | 4 SQU SYNC 5.1 2.9 off off | 2.7 0 _ | 3.4 3.6 3.3 4.1 1.1 1.4 N 5.8 | ENV 4.3 | SINE 1.6 6.5 | 1.8 4.5 7.1 1.0 | off"},
    {"B3", "Oboe", "8 _ off off | 8 SQU SYNC 1.4 2.9 off off | 0.8 0 _ | 10 10 5.9 4.0 0 0 _ 4.4 | ENV 4.5 | SINE 1.2 7.2 | 1.5 5.6 10 1.1 | off"},
    {"B4", "Song Whistle", "_ _ _ _ | 8 SAW OFF 9.6 4.7 on on | 1.4 0.3 N | 10 5.0 2.8 6.2 0.4 0 _ 10 | ENV 5.2 | SINE 2.1 7.8 | 1.7 3.0 10 1.1 | off"},
    {"B5", "Synth Brass I", "8 SAW off off | 8 SAW OFF 3.9 3.1 off on | 3.6 1.0 N | 4.0 0 3.9 0 0 3.2 N 9.2 | GATE 2.4 | SINE 1.2 7.3 | 1.3 0 10 0 | on"},
    {"B6", "Synth Brass II", "16 SAW off off | 16 SAW OFF 5.0 3.7 off off | 4.6 0.3 _ | 4.3 0 0.5 4.8 0 5.8 N 4.1 | ENV 5.6 | SINE 1.7 6.8 | 1.6 2.5 5.2 0 | on"},
    {"B7", "Distortion Guitar", "16 PUL off off | 16 SQU SYNC 5.0 3.8 off on | 2.5 6.8 I | 10 0 5.7 0 0 6.2 N 6.7 | GATE 1.7 | SINE 1.2 6.2 | 0 6.0 4.7 0 | on"},
    {"B8", "Juicy Funk", "16 SAW off off | _ _ _ _ _ _ _ | 4.1 0 _ | 0 0 0 8.8 0 7.8 N 4.8 | GATE 5.2 | SINE 0.8 6.7 | 1.4 1.7 5.0 0 | off"},
    {"B9", "Filter Flow", "16 SAW off off | 16 SAW OFF 4.9 4.0 off off | 4.3 0 _ | 5.2 0 2.9 3.8 0 4.3 N 3.0 | GATE 3.8 | SINE 0.6 7.2 | 7.0 7.4 7.6 0 | on"},
    {"B10", "Fat Fifth", "16 SAW off off | 16 SAW OFF 7.9 4.5 off off | 3.5 0 _ | 6.2 0 0.9 0 0 5.8 N 4.8 | GATE 2.5 | SINE 1.3 6.6 | 0 3.0 9.0 0 | on"},
    {"B11", "Sync Wah", "16 SAW off off | 16 SAW SYNC 7.1 8.6 off on | 2.9 10 N | 10 0 3.8 3.5 0 4.5 N 7.1 | GATE 2.1 | SINE 1.1 7.1 | 1.1 0.7 10 0 | on"},
    {"B12", "Sync Sweep", "16 SAW off off | 8 SQU SYNC 3.6 4.7 off on | 3.8 6.3 N | 10 0 7.7 0 0 0 _ 4.8 | GATE 1.9 | SINE 1.3 6.8 | 6.8 0 10 0 | on"},
    {"B13", "Funky Clavi", "16 PUL off off | 16 SQU SYNC 9.5 0 off on | 6.1 1.3 N | 6.3 0 1.3 4.4 0 5.4 N 3.8 | ENV 5.6 | SINE 1.6 6.0 | 0 2.1 5.1 0 | on"},
    {"B14", "Pulsar", "16 SAW off off | 8 SAW METAL 5.0 3.4 off off | 10 0 _ | 4.9 0 2.5 8.0 5.0 1.4 N 9.2 | ENV 4.8 | RANDOM 0 7.5 | 0 0 9.7 8.4 | on"},
    {"B15", "Planet", "4 SQU off off | 8 SQU OFF 5.0 4.4 off off | 8.1 0 _ | 5.1 0 2.3 10 0 6.8 I 5.0 | ENV 4.8 | SINE 1.8 7.6 | 0 7.3 0 7.9 | on"},
    {"B16", "Jet", "_ _ _ _ | _ NOISE _ _ _ _ _ | 0 2.1 _ | 10 0 0 0 0 6.0 N 10 | ENV 6.6 | SINE 0 9.0 | 10 0 10 10 | on"},
