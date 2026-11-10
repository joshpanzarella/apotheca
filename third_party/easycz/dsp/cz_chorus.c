#include "dsp/cz_chorus.h"

#include <string.h>

#define RATE 44100.0f
#define SLOW_HZ 0.56f
#define FAST_HZ 7.14f

/* Delay in samples: centre 3.07 ms, swinging +/-0.6 ms with the slow LFO and
 * +/-0.12 ms with the fast one, inside the BBD's 2.3-3.8 ms range. */
#define DELAY_CENTRE (3.07e-3f * RATE)
#define DELAY_SLOW (0.60e-3f * RATE)
#define DELAY_FAST (0.12e-3f * RATE)

#define WET_LP 0.55f    /* one-pole coefficient: about 10 kHz, the BBD's dull top */
#define MIX_STEP 0.0011f /* full fade in ~20 ms */
#define DRY_GAIN 0.71f
#define WET_GAIN 0.50f

void cz_chorus_init(cz_chorus_t *c) {
    memset(c, 0, sizeof(*c));
}

void cz_chorus_enable(cz_chorus_t *c, int on) {
    c->target = on ? 1.0f : 0.0f;
}

/* triangle, -1..1, from a phase in 0..1 */
static float tri(float phase) {
    phase -= (float)(int)phase;
    return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
}

/* 4-point cubic (Hermite) read, d samples behind the newest */
static float read_delay(const cz_chorus_t *c, float d) {
    const int whole = (int)d;
    const float f = d - (float)whole;
    const unsigned i = c->pos - (unsigned)whole;
    const float xm1 = c->buf[(i + 1) & (CHORUS_BUF - 1)];
    const float x0 = c->buf[i & (CHORUS_BUF - 1)];
    const float x1 = c->buf[(i - 1) & (CHORUS_BUF - 1)];
    const float x2 = c->buf[(i - 2) & (CHORUS_BUF - 1)];
    const float a = 0.5f * (x1 - xm1);
    const float b = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float d3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((d3 * f + b) * f + a) * f + x0;
}

void cz_chorus_process(cz_chorus_t *c, float in, float *left, float *right) {
    c->pos++;
    c->buf[c->pos & (CHORUS_BUF - 1)] = in;

    if (c->mix != c->target) {
        c->mix += c->mix < c->target ? MIX_STEP : -MIX_STEP;
        if ((c->target > 0.5f && c->mix > 1.0f) || (c->target < 0.5f && c->mix < 0.0f)) c->mix = c->target;
    }
    if (c->mix <= 0.0f) {
        *left = *right = in;
        return;
    }

    c->slow += SLOW_HZ / RATE;
    c->fast += FAST_HZ / RATE;
    if (c->slow >= 1.0f) c->slow -= 1.0f;
    if (c->fast >= 1.0f) c->fast -= 1.0f;

    float wet[3];
    for (int k = 0; k < 3; k++) {
        const float shift = (float)k / 3.0f; /* 120 degrees apart */
        const float d = DELAY_CENTRE + DELAY_SLOW * tri(c->slow + shift) + DELAY_FAST * tri(c->fast + shift);
        c->wet_lp[k] += WET_LP * (read_delay(c, d) - c->wet_lp[k]);
        wet[k] = c->wet_lp[k];
    }

    const float l = DRY_GAIN * in + WET_GAIN * (wet[0] + 0.5f * wet[1]);
    const float r = DRY_GAIN * in + WET_GAIN * (wet[2] + 0.5f * wet[1]);
    *left = in + c->mix * (l - in);
    *right = in + c->mix * (r - in);
}
