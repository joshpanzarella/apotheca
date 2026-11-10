/*
 * Stereo chorus, after the CZ-1's (CZ-1 service manual; the CZ-3000/5000
 * have one too, the CZ-101 does not): three bucket-brigade delay lines
 * (MN3209) whose clock sweeps 55.6 kHz to 33.3 kHz, a delay of about 2.3 to
 * 3.8 ms, driven by triangle LFOs at 0.56 Hz and 7.14 Hz with three outputs
 * 120 degrees apart. MAME's CZ-1 driver applies it to the centre (mono) mix.
 *
 * Here the three taps read one delay line with cubic interpolation, the wet
 * path is rounded off like a BBD's output filter, and taps 1 and 3 go left
 * and right with tap 2 in the middle. Switching on and off fades over ~20 ms.
 * The CZ-1's compander and BBD hiss are not modelled.
 */
#ifndef EASYCZ_CZ_CHORUS_H
#define EASYCZ_CZ_CHORUS_H

#define CHORUS_BUF 512 /* > the longest delay (3.8 ms = 170 samples) + interpolation */

typedef struct {
    float buf[CHORUS_BUF];
    unsigned pos;
    float slow, fast; /* LFO phases, 0..1 */
    float wet_lp[3];  /* BBD output filter state, per tap */
    float mix;        /* 0 = dry, 1 = chorus; follows target smoothly */
    float target;
} cz_chorus_t;

void cz_chorus_init(cz_chorus_t *c);
void cz_chorus_enable(cz_chorus_t *c, int on);

/* One mono sample in, a stereo pair out (44.1 kHz). */
void cz_chorus_process(cz_chorus_t *c, float in, float *left, float *right);

#endif
