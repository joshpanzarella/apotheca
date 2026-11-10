/*
 * The CZ's audio path after the sound chip, plus the rate change to Move.
 *
 * Hardware (CZ-101/1000 schematic, CZ-1 service manual): the uPD933 sends
 * each 40 kHz sample to a 12-bit DAC (DO4-DO15) and drives an analog
 * "expander" (DO1-DO3) that scales the DAC output by 1, 1/2, 1/4, 1/8 or
 * 1/16. So each sample keeps its 12 most significant bits: floating point
 * with a 12-bit mantissa. Then a 40 kHz sample/hold and a 17 kHz low-pass
 * module ("LPF17K").
 *
 * Here: cz_dac() is the DAC and expander. The resampler converts 40 kHz to
 * 44.1 kHz through one of two low-pass designs:
 *   analog (default)  the sample/hold, modelled exactly: each 40 kHz sample is
 *                     held for 25 us, which the filter's impulse response
 *                     includes (sinc droop: -0.9 dB at 10 kHz, -2.75 dB at
 *                     17 kHz), and the LPF17K as a steep low-pass, -6 dB at
 *                     17.5 kHz. The module's real order is undocumented.
 *   clean             flat to about 17 kHz, -6 dB at 18.5 kHz
 * Both are at least 80 dB down from 20 kHz, where the images of the 40 kHz
 * stream start. dc_block is the output coupling capacitor (3.3 uF into a ~10k
 * input, about 5 Hz), which the resonance and pulse waves need: they carry a
 * large DC offset.
 */
#ifndef EASYCZ_CZ_OUTPUT_H
#define EASYCZ_CZ_OUTPUT_H

#include <math.h>
#include <stdint.h>

/* 12-bit DAC with the 5-step expander: drop the bits below the 12 most
 * significant ones. The DAC sees bits, so this truncates rather than rounds. */
static inline int32_t cz_dac(int16_t x) {
    const int32_t v = x;
    const int32_t mag = v < 0 ? -v - 1 : v;
    int shift = 0;
    while (shift < 4 && (mag >> shift) >= 2048) shift++;
    return (v >> shift) * (1 << shift);
}

#define RS_IN_RATE 40000
#define RS_OUT_RATE 44100
#define RS_UP 441   /* 44100 / 40000 = 441 / 400 */
#define RS_DOWN 400
#define RS_TAPS 64  /* per phase, at the 40 kHz input rate */
#define RS_BUF 256  /* input history, power of two, > RS_TAPS + one block */

enum { RS_CLEAN, RS_ANALOG };

typedef struct {
    float *coef[2];     /* [RS_UP][RS_TAPS] per design, allocated by resampler_init */
    int design;         /* RS_CLEAN or RS_ANALOG; switch any time */
    float buf[RS_BUF + RS_TAPS]; /* input history ring; its first RS_TAPS
                                  * entries are mirrored past the end, so
                                  * every read of RS_TAPS is contiguous */
    uint32_t written;   /* input samples pushed so far */
    uint32_t base;      /* input index of the current output position */
    uint32_t phase;     /* fractional position, in 1/RS_UP input samples */
} resampler_t;

/* Allocates and designs both filters. Not realtime-safe; call from create.
 * Starts on RS_ANALOG. Returns 0 on success. */
int resampler_init(resampler_t *r);
void resampler_free(resampler_t *r);

/* Input samples needed before resampler_next() can run. */
static inline int resampler_needs(const resampler_t *r) {
    /* as a difference, so the counters may wrap (2^32 samples = 29.8 h) */
    return (int32_t)(r->written - r->base) <= RS_TAPS / 2;
}

static inline void resampler_push(resampler_t *r, float x) {
    const uint32_t i = r->written++ & (RS_BUF - 1);
    r->buf[i] = x;
    if (i < RS_TAPS) r->buf[i + RS_BUF] = x;
}

/* One 44.1 kHz output sample. Call only when !resampler_needs(). */
float resampler_next(resampler_t *r);

/* One-pole high-pass at the output coupling capacitor's corner. */
typedef struct {
    float x1, y1;
} dc_block_t;

#define DC_BLOCK_POLE 0.999288f /* exp(-2 pi 5 Hz / 44100) */

static inline float dc_block(dc_block_t *d, float x) {
    const float y = x - d->x1 + DC_BLOCK_POLE * d->y1;
    d->x1 = x;
    d->y1 = y;
    return y;
}

/* Output stage to the Move. The CZ-101's headroom: one chip's 8 voices at
 * full volume reach exactly full scale, so one voice is about -18 dBFS.
 * Only the second chip's voices can push past it; above the knee the signal
 * is rounded off instead of hard clipping. */
#define CZ_OUTPUT_GAIN 1.0f
#define CZ_CLIP_KNEE 0.8f

static inline float cz_output_stage(float x) {
    x *= CZ_OUTPUT_GAIN;
    const float a = x < 0.0f ? -x : x;
    if (a <= CZ_CLIP_KNEE) return x;
    const float y = CZ_CLIP_KNEE + (1.0f - CZ_CLIP_KNEE) * tanhf((a - CZ_CLIP_KNEE) / (1.0f - CZ_CLIP_KNEE));
    return x < 0.0f ? -y : y;
}

#endif
