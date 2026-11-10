/*
 * uPD933 — the NEC/Casio "phase distortion" sound chip in the Casio CZ series.
 *
 * C port of MAME's uPD933 device (src/devices/sound/upd933.cpp and .h,
 * copyright Devin Acker, BSD-3-Clause; see THIRD_PARTY_LICENSES.md).
 *
 * One chip = 8 voices, each with a DCO (pitch), DCW (waveform) and DCA
 * (volume) ramp. The chip only ramps each one toward a target at a rate and
 * flags the arrival; stepping through the CZ's 8-step envelopes is the
 * firmware's job (the caller's), done by writing the next step's register.
 *
 * Changes from MAME:
 * - plain C with no MAME device plumbing; the caller polls upd933_irq_data()
 *   after each sample instead of getting an IRQ line
 * - the noise source is a seeded xorshift instead of machine().rand()
 * - two hardware-measured laws (a real CZ-101, Yoshinaga's measurements;
 *   see the research report) replace MAME's: the DCO envelope's fine level
 *   unit is 1/8 semitone, not 1/16, and the DCA moves 0.5 dB per level code,
 *   not 0.613. Build with -DUPD933_MAME_COMPAT for MAME's laws, to compare
 *   output against MAME sample for sample.
 *
 * - an EasyCZ extension, not on the chip: per-voice DCW and volume offsets
 *   for expression (pad pressure), smoothed per sample. At their default of
 *   0 the output is unchanged.
 *
 * Everything here is realtime-safe except upd933_init_tables().
 */
#ifndef EASYCZ_UPD933_H
#define EASYCZ_UPD933_H

#include <stdint.h>

#define UPD933_VOICES 8
#define UPD933_CLOCK 4480000 /* 8.96 MHz crystal / 2 */
#define UPD933_CLOCKS_PER_SAMPLE 112
#define UPD933_RATE (UPD933_CLOCK / UPD933_CLOCKS_PER_SAMPLE) /* 40 kHz */

/* Register banks; the low 3 bits of a register number pick the voice. */
#define UPD933_REG_DCA 0x00   /* DCA step: dir, rate, sustain, level */
#define UPD933_REG_DCO 0x10   /* DCO step */
#define UPD933_REG_DCW 0x20   /* DCW step */
#define UPD933_REG_PITCH 0x60 /* pitch in semitones, 7.9 fixed point */
#define UPD933_REG_WAVE 0x68  /* waveforms, window, modulation */
#define UPD933_REG_PHASE 0x98 /* phase counter (0 restarts the oscillator) */
#define UPD933_REG_PM 0xb8    /* pitch modulator level (b8-bb) */

/* Envelope kinds, for upd933_env_target(). */
#define UPD933_ENV_DCA 0
#define UPD933_ENV_DCW 1
#define UPD933_ENV_DCO 2

/* upd933_irq_data() codes, as the chip reports them to the CPU. The lowest
 * set bit gives the envelope (DCA 1|v<<1, DCW 2|v<<2, DCO 4|v<<3). */
#define UPD933_IRQ_IS_DCA(d) (((d) & 1) != 0)
#define UPD933_IRQ_IS_DCW(d) (((d) & 3) == 2)
#define UPD933_IRQ_IS_DCO(d) (((d) & 7) == 4)
#define UPD933_IRQ_VOICE(d) \
    (UPD933_IRQ_IS_DCA(d) ? ((d) >> 1) & 7 : UPD933_IRQ_IS_DCW(d) ? ((d) >> 2) & 7 : ((d) >> 3) & 7)

typedef struct {
    uint16_t cosine[0x800];    /* 12-bit raised cosine, (1 - cos) / 2 */
    uint32_t pitch[0x80];      /* phase step per semitone */
    uint16_t pitch_fine[0x200];
    uint16_t volume[0x200];    /* exponential, 0..8192 */
} upd933_tables_t;

typedef struct {
    uint8_t direction; /* 0 = up, 1 = down */
    uint8_t sustain;   /* set: no arrival flag (also set after one fires) */
    uint8_t irq;       /* arrival flag, cleared by upd933_irq_data() */
    uint32_t rate, target, current;
} upd933_env_t;

typedef struct {
    uint8_t wave[2]; /* waveform for even / odd cycles */
    uint8_t window, ring_mod, pitch_mod, mute_other;
    uint16_t pitch;
    uint32_t position, pitch_step;
    uint16_t dcw_limit;
    int16_t pm_level;
    /* EasyCZ expression extension: added to the DCW (0-1016 scale) and to
     * the volume table index (4 per DCA level code, 0.5 dB per code) */
    int16_t dcw_offset, dcw_offset_target;
    int16_t vol_offset, vol_offset_target;
} upd933_voice_t;

typedef struct {
    const upd933_tables_t *t;
    upd933_voice_t voice[UPD933_VOICES];
    upd933_env_t dca[UPD933_VOICES], dcw[UPD933_VOICES], dco[UPD933_VOICES];
    uint32_t sample_count;
    int16_t last_sample;
    uint32_t noise;  /* xorshift32 state */
    uint8_t irq_any; /* some arrival is pending; saves scanning every flag */
} upd933_t;

/* Fill the lookup tables. Uses libm; call from create only. */
void upd933_init_tables(upd933_tables_t *t);

/* Power-on state. noise_seed must be nonzero. */
void upd933_reset(upd933_t *c, const upd933_tables_t *t, uint32_t noise_seed);

/* Write a 16-bit register, as the CZ firmware does over the chip's bus. */
void upd933_write(upd933_t *c, uint8_t reg, uint16_t value);

/* Advance one 40 kHz sample and return the 8-voice mix (16-bit, clamped). */
int16_t upd933_update(upd933_t *c);

/* Next pending envelope arrival (see UPD933_IRQ_*), or 0 if none. Reading
 * one clears it, so call repeatedly until it returns 0. Cheap to call when
 * nothing is pending. */
uint8_t upd933_irq_data(upd933_t *c);

/* Where a step's 7-bit level code puts the envelope accumulator
 * (upd933_env_t.current). A step's direction bit must say which way the
 * target lies: given the wrong one, the chip jumps straight to the target. */
uint32_t upd933_env_target(int kind, int level);

/* Set a voice's expression offsets (see upd933_voice_t). They glide to the
 * new values one unit per sample unless immediate. */
void upd933_set_expression(upd933_t *c, int vnum, int dcw_offset, int vol_offset, int immediate);

/* Encode an envelope step register value. level is 7 bits (DCA, DCW) or a
 * 7-bit DCO code whose bit 6 selects 2-semitone units. */
static inline uint16_t upd933_env_step(int down, int rate, int sustain, int level) {
    return (uint16_t)(((down & 1) << 15) | ((rate & 0x7f) << 8) | ((sustain & 1) << 7) | (level & 0x7f));
}

#endif
