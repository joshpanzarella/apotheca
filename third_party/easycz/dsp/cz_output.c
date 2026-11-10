#include "dsp/cz_output.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Low-pass cutoffs (-6 dB) and Kaiser beta. With 64 taps at 40 kHz the
 * transition band is about 3.2 kHz wide, so both designs are at least 80 dB
 * down from 20 kHz, where the images of the 40 kHz stream start. */
#define RS_CUTOFF_CLEAN_HZ 18500.0
#define RS_CUTOFF_ANALOG_HZ 17500.0
#define RS_KAISER_BETA 8.0
#define RS_HOLD_POINTS 16 /* sub-sample points averaged for the sample/hold */

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

/* Windowed-sinc low-pass at distance t (in input samples) from its centre. */
static double prototype(double t, double fc, double i0_beta) {
    const double half = RS_TAPS / 2.0;
    const double u = t / half;
    if (fabs(u) >= 1.0) return 0.0;
    const double x = 2.0 * fc * t;
    const double sinc = fabs(x) < 1e-12 ? 1.0 : sin(M_PI * x) / (M_PI * x);
    return 2.0 * fc * sinc * bessel_i0(RS_KAISER_BETA * sqrt(1.0 - u * u)) / i0_beta;
}

static void design(float *coef, double cutoff_hz, int hold) {
    const double fc = cutoff_hz / RS_IN_RATE; /* cycles per input sample */
    const double half = RS_TAPS / 2.0;
    const double i0_beta = bessel_i0(RS_KAISER_BETA);

    for (int p = 0; p < RS_UP; p++) {
        float *c = &coef[p * RS_TAPS];
        double sum = 0.0;
        for (int j = 0; j < RS_TAPS; j++) {
            /* distance in input samples from output position to tap j */
            const double t = (double)p / RS_UP + (half - 1) - j;
            double h;
            if (hold) {
                /* the sample/hold keeps tap j's value for one input period,
                 * so its response is the prototype averaged over that period */
                h = 0.0;
                for (int k = 0; k < RS_HOLD_POINTS; k++) h += prototype(t - (k + 0.5) / RS_HOLD_POINTS + 0.5, fc, i0_beta);
                h /= RS_HOLD_POINTS;
            } else {
                h = prototype(t, fc, i0_beta);
            }
            c[j] = (float)h;
            sum += h;
        }
        /* unity DC gain in every phase, so no phase-dependent ripple */
        for (int j = 0; j < RS_TAPS; j++) c[j] = (float)(c[j] / sum);
    }
}

int resampler_init(resampler_t *r) {
    memset(r, 0, sizeof(*r));
    for (int d = 0; d < 2; d++) {
        r->coef[d] = calloc((size_t)RS_UP * RS_TAPS, sizeof(float));
        if (!r->coef[d]) return -1;
    }
    design(r->coef[RS_CLEAN], RS_CUTOFF_CLEAN_HZ, 0);
    design(r->coef[RS_ANALOG], RS_CUTOFF_ANALOG_HZ, 1);
    r->design = RS_ANALOG;
    return 0;
}

void resampler_free(resampler_t *r) {
    for (int d = 0; d < 2; d++) {
        free(r->coef[d]);
        r->coef[d] = NULL;
    }
}

float resampler_next(resampler_t *r) {
    const float *c = &r->coef[r->design == RS_CLEAN ? RS_CLEAN : RS_ANALOG][r->phase * RS_TAPS];
    const float *x = &r->buf[(r->base - (RS_TAPS / 2 - 1)) & (RS_BUF - 1)];
    /* four running sums: independent chains the compiler can keep in one
     * SIMD register, instead of one 64-long chain of dependent adds */
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    for (int j = 0; j < RS_TAPS; j += 4) {
        a0 += c[j] * x[j];
        a1 += c[j + 1] * x[j + 1];
        a2 += c[j + 2] * x[j + 2];
        a3 += c[j + 3] * x[j + 3];
    }
    const float acc = (a0 + a1) + (a2 + a3);

    r->phase += RS_DOWN;
    if (r->phase >= RS_UP) {
        r->phase -= RS_UP;
        r->base++;
    }
    return acc;
}
