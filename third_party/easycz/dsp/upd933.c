/*
 * uPD933 — C port of MAME's uPD933 device.
 *
 * license:BSD-3-Clause
 * copyright-holders:Devin Acker (MAME original), Josh Panzarella (C port)
 *
 * See upd933.h for what changed from MAME.
 */
#include "dsp/upd933.h"

#include <math.h>
#include <string.h>

#define NOTE_SHIFT 9
#define PITCH_SHIFT 20
#define PITCH_FINE_SHIFT 12
#define VOLUME_SHIFT 12

#define ENV_DCA_SHIFT 16
#define ENV_DCW_SHIFT 16
#define ENV_DCO_SHIFT 11

/* DCO level units. MAME treats a fine code as 1/16 semitone (shift 5).
 * Measurements of a real CZ-101 give 1/8 semitone, and only 1/8 makes Casio's
 * front-panel DCO levels continuous: level 63 is fine code 63 = 7.875
 * semitones and level 64 is coarse code 4 = 8 semitones. Coarse codes are 2
 * semitones either way. */
#ifdef UPD933_MAME_COMPAT
#define DCO_FINE_SHIFT 5
#else
#define DCO_FINE_SHIFT 6
#endif
#define DCO_COARSE_SHIFT 10 /* 2 semitones = 1024 pitch units */

#define DCA_LEVEL_SHIFT (ENV_DCA_SHIFT + 2)
#define DCW_LEVEL_SHIFT (ENV_DCW_SHIFT + 3)

#define BIT(x, n) (((x) >> (n)) & 1u)
#define BITS(x, n, w) (((x) >> (n)) & ((1u << (w)) - 1u))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void upd933_init_tables(upd933_tables_t *t) {
    for (int i = 0; i < 0x800; i++)
        t->cosine[i] = (uint16_t)(0xfff * (1 - cos(2.0 * M_PI * i / 0x7ff)) / 2);

    for (int i = 0; i < 0x80; i++) {
        /* A4 is chip note 62 (MIDI 69), 442 Hz */
        const double freq = 442.0 * pow(2, (i - 62) / 12.0);
        t->pitch[i] = (uint32_t)((1 << PITCH_SHIFT) * (freq * 0x800 / 40000));
    }

    for (int i = 0; i < 0x200; i++)
        t->pitch_fine[i] = (uint16_t)((1 << PITCH_FINE_SHIFT) * (pow(2, (double)i / (12.0 * 0x200)) - 1));

    /* Exponential volume curve; it also scales the 12-bit waveform up to a
     * 13-bit range. The DCA index is 4 x the level code, peaking at 508. */
#ifdef UPD933_MAME_COMPAT
    /* MAME: 78 dB over 511 steps, 0.613 dB per level code */
    for (int i = 1; i < 0x200; i++)
        t->volume[i] = (uint16_t)pow(2 << VOLUME_SHIFT, (double)i / 0x1ff);
#else
    /* Measured on a CZ-101: 0.5 dB (1/12 octave) per level code, integer
     * amplitude, so the bottom codes go silent. */
    for (int i = 1; i < 0x200; i++) {
        const int v = 8 * (int)floor(1024.0 * pow(2.0, (i - 508) / 48.0));
        t->volume[i] = (uint16_t)(v < 8192 ? v : 8192);
    }
#endif
    t->volume[0] = 0;
}

uint32_t upd933_env_target(int kind, int level) {
    level &= 0x7f;
    switch (kind) {
    case UPD933_ENV_DCA:
        return (uint32_t)level << DCA_LEVEL_SHIFT;
    case UPD933_ENV_DCW:
        return (uint32_t)level << DCW_LEVEL_SHIFT;
    default: /* DCO: bit 6 selects 2-semitone units */
        return (uint32_t)(level & 0x3f) << (ENV_DCO_SHIFT + ((level & 0x40) ? DCO_COARSE_SHIFT : DCO_FINE_SHIFT));
    }
}

void upd933_reset(upd933_t *c, const upd933_tables_t *t, uint32_t noise_seed) {
    memset(c, 0, sizeof(*c));
    c->t = t;
    c->noise = noise_seed ? noise_seed : 1;
    for (int i = 0; i < UPD933_VOICES; i++) {
        c->dca[i].sustain = 1;
        c->dcw[i].sustain = 1;
        c->dco[i].sustain = 1;
    }
}

static uint32_t env_rate(uint8_t data) {
    return (uint32_t)(8 | (data & 7)) << (data >> 3);
}

static void update_pitch_step(upd933_t *c, int vnum) {
    upd933_voice_t *voice = &c->voice[vnum];
    const int32_t pitch = (int32_t)(voice->pitch + (c->dco[vnum].current >> ENV_DCO_SHIFT)) + voice->pm_level;
    uint32_t step = 0;

    if (pitch > 0 && pitch < (1 << 16)) {
        const uint8_t note = (uint8_t)(pitch >> NOTE_SHIFT);
        const uint16_t fine = (uint16_t)(pitch & ((1 << NOTE_SHIFT) - 1));
        step = c->t->pitch[note];
        if (fine) step += (step >> PITCH_FINE_SHIFT) * c->t->pitch_fine[fine];
    }

    voice->pitch_step = step;

    /* The effective DCW is limited at high pitch, which keeps narrow pulses
     * audible and stops aliasing at extreme pitches. */
    const uint32_t lim = step >> (PITCH_SHIFT - 2);
    voice->dcw_limit = (uint16_t)(0x400 - (lim < 0x400 ? lim : 0x400));
}

void upd933_write(upd933_t *c, uint8_t reg, uint16_t value) {
    const int vnum = reg & 7;
    upd933_voice_t *voice = &c->voice[vnum];
    /* Registers 68-6f set the waveform for voice n but the modulation for
     * voice n-2. */
    upd933_voice_t *mod_voice = &c->voice[(vnum + 6) & 7];

    switch (reg >> 3) {
    case 0x0: { /* 00-07: DCA step */
        upd933_env_t *e = &c->dca[vnum];
        e->direction = BIT(value, 15);
        e->rate = env_rate(BITS(value, 8, 7));
        e->sustain = BIT(value, 7);
        e->target = upd933_env_target(UPD933_ENV_DCA, (int)BITS(value, 0, 7));
        e->irq = 0;
        break;
    }
    case 0x2: { /* 10-17: DCO step; bit 6 of the level selects 2-semitone units */
        upd933_env_t *e = &c->dco[vnum];
        e->direction = BIT(value, 15);
        e->rate = env_rate(BITS(value, 8, 7));
        e->sustain = BIT(value, 7);
        e->target = upd933_env_target(UPD933_ENV_DCO, (int)BITS(value, 0, 7));
        e->irq = 0;
        break;
    }
    case 0x4: { /* 20-27: DCW step */
        upd933_env_t *e = &c->dcw[vnum];
        e->direction = BIT(value, 15);
        e->rate = env_rate(BITS(value, 8, 7));
        e->sustain = BIT(value, 7);
        e->target = upd933_env_target(UPD933_ENV_DCW, (int)BITS(value, 0, 7));
        e->irq = 0;
        break;
    }
    case 0xc: /* 60-67: pitch */
        voice->pitch = value;
        update_pitch_step(c, vnum);
        break;
    case 0xd: /* 68-6f: waveform
               * nnn----- -------- first waveform
               * ---nnn-- -------- second waveform
               * ------n- -------- second waveform enable
               * -------n nn------ window
               * -------- --n----- ring mod     \
               * -------- ---n---- pitch mod     | for voice n-2,
               * -------- ----n--- 0 = voice, 1 = noise | even n only
               * -------- -----n-- negate the other voice of the pair /
               */
        voice->wave[0] = (uint8_t)BITS(value, 13, 3);
        voice->wave[1] = BIT(value, 9) ? (uint8_t)BITS(value, 10, 3) : voice->wave[0];
        voice->window = (uint8_t)BITS(value, 6, 3);
        if (!(vnum & 1)) {
            mod_voice->ring_mod = (uint8_t)BIT(value, 5);
            mod_voice->pitch_mod = (uint8_t)BITS(value, 3, 2);
            mod_voice->mute_other = (uint8_t)BIT(value, 2);
        }
        break;
    case 0x13: /* 98-9f: phase counter */
        voice->position = (uint32_t)value << (PITCH_SHIFT - 4);
        break;
    case 0x17: /* b8-bb: pitch modulator level */
        if (vnum < 4) c->voice[vnum << 1].pm_level = (int16_t)value;
        break;
    default:
        break;
    }
}

static void env_update(upd933_env_t *e) {
    if (e->current != e->target) {
        if (!e->direction) {
            if (e->current > e->target || e->target - e->current <= e->rate)
                e->current = e->target;
            else
                e->current += e->rate;
        } else {
            if (e->current < e->target || e->current - e->target <= e->rate)
                e->current = e->target;
            else
                e->current -= e->rate;
        }
    }
    /* Setting sustain too makes the arrival fire only once. */
    if (!e->sustain && e->current == e->target) e->irq = e->sustain = 1;
}

static uint32_t noise_next(upd933_t *c) {
    uint32_t x = c->noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return c->noise = x;
}

/* The voice's waveform at its current position: the transfer function and
 * window applied to the cosine table, 0..4095 before volume. */
static int32_t voice_wave(const upd933_t *c, int vnum) {
    const upd933_voice_t *voice = &c->voice[vnum];
    const uint32_t pos = BITS(voice->position, PITCH_SHIFT, 11);
    const uint32_t wave = BIT(voice->position, PITCH_SHIFT + 11);

    int32_t dcw_signed = (int32_t)(c->dcw[vnum].current >> ENV_DCW_SHIFT) + voice->dcw_offset;
    if (dcw_signed < 0) dcw_signed = 0;
    uint32_t dcw = (uint32_t)dcw_signed;
    if (dcw > voice->dcw_limit) dcw = voice->dcw_limit;
    const uint32_t pivot = 0x400 - dcw;
    uint32_t phase = 0;
    uint32_t window = 0;

    /* transfer function */
    switch (voice->wave[wave] & 7) {
    case 0: /* sawtooth: rises over [0, pivot), falls over [pivot, 800) */
        if (pos < pivot)
            phase = pos * 0x400 / pivot;
        else
            phase = 0x400 + (pos - pivot) * 0x400 / (0x800 - pivot);
        break;
    case 1: /* square: rises over [0, pivot), holds to 400, then the same inverted */
        if ((pos & 0x3ff) < pivot)
            phase = (pos & 0x3ff) * 0x400 / pivot;
        else
            phase = 0x3ff;
        phase |= (pos & 0x400);
        break;
    case 2: /* pulse: rises and falls over [0, pivot*2), then stays low */
        if (pos < pivot * 2)
            phase = pos * 0x800 / (pivot * 2);
        else
            phase = 0x7ff;
        break;
    case 3: /* silent (undocumented) */
        break;
    case 4: /* double sine: rises and falls over [0, pivot), again over [pivot, 800) */
        if (pos < pivot)
            phase = pos * 0x800 / pivot;
        else
            phase = (pos - pivot) * 0x800 / (0x800 - pivot);
        break;
    case 5: /* saw pulse: rises over [0, 400), falls over [400, 400+pivot), then low */
        if (pos < 0x400)
            phase = pos;
        else if (pos < pivot + 0x400)
            phase = 0x400 + (pos & 0x3ff) * 0x400 / pivot;
        else
            phase = 0x7ff;
        break;
    case 6: /* resonance: frequency times (1 + dcw/64), hard-synced to the fundamental */
        phase = (pos + ((pos * dcw) >> 6)) & 0x7ff;
        break;
    case 7: /* double pulse (undocumented): pulse at double frequency */
        if ((pos & 0x3ff) < pivot)
            phase = (pos & 0x3ff) * 0x400 / pivot;
        else
            phase = 0x7ff;
        break;
    }

    /* window */
    switch (voice->window & 7) {
    case 0: /* none */
        break;
    case 1: /* sawtooth: falls over [0, 800) */
        window = pos;
        break;
    case 2: /* triangle: rises over [0, 400), falls over [400, 800) */
        window = (pos & 0x3ff) * 2;
        if (pos < 0x400) window ^= 0x7fe;
        break;
    case 3: /* trapezoid: falls over [400, 800) */
        if (pos >= 0x400) window = (pos & 0x3ff) * 2;
        break;
    case 4: /* pulse (undocumented): falls over [0, 400) */
        window = pos < 0x400 ? pos * 2 : 0x7ff;
        break;
    default: /* double saw (undocumented) */
        window = (0x3ff ^ (pos & 0x3ff)) * 2;
        break;
    }

    int32_t sample = c->t->cosine[phase];
    if (window) sample = sample * (int32_t)(0x800 - window) / 0x800;
    return sample;
}

static int16_t update_voice(upd933_t *c, int vnum) {
    upd933_voice_t *voice = &c->voice[vnum];
    const upd933_tables_t *t = c->t;

    /* center around zero, apply volume and ring mod. A silent voice skips
     * the waveform: it would come out as 0 anyway. */
    int32_t vol_index = (int32_t)(c->dca[vnum].current >> ENV_DCA_SHIFT) + c->voice[vnum].vol_offset;
    vol_index = vol_index < 0 ? 0 : vol_index > 0x1ff ? 0x1ff : vol_index;
    const int32_t volume = t->volume[vol_index];
    int32_t sample = 0;
    if (volume) sample = ((voice_wave(c, vnum) * volume) >> VOLUME_SHIFT) - volume / 2;

    if (voice->ring_mod) sample = sample * c->last_sample / 0x1000;

    /* 'mute' negates the other voice of the modulating pair */
    if (voice->mute_other) sample -= c->last_sample;

    /* envelopes and pitch modulation; recalculate the pitch step if needed */
    const uint32_t old_dco = c->dco[vnum].current;
    const int16_t old_pm = voice->pm_level;

    env_update(&c->dca[vnum]);
    env_update(&c->dcw[vnum]);
    env_update(&c->dco[vnum]);
    c->irq_any |= c->dca[vnum].irq | c->dcw[vnum].irq | c->dco[vnum].irq;

    /* pitch/noise modulation latches a new value every 8 samples */
    if (!(c->sample_count & 7)) {
        switch (voice->pitch_mod & 3) {
        default:
            voice->pm_level = 0;
            break;
        case 2: /* other voice (unused by the CZs, about +/- 7.5 semitones) */
            voice->pm_level = c->last_sample;
            break;
        case 3: /* noise: 0 or 32 semitones above the base pitch */
            voice->pm_level = (int16_t)(noise_next(c) & (32u << NOTE_SHIFT));
            break;
        }
    }

    if (((old_dco ^ c->dco[vnum].current) >> ENV_DCO_SHIFT) || old_pm != voice->pm_level)
        update_pitch_step(c, vnum);

    voice->position += voice->pitch_step;

    /* expression offsets glide one unit per sample */
    if (voice->dcw_offset != voice->dcw_offset_target) voice->dcw_offset += voice->dcw_offset < voice->dcw_offset_target ? 1 : -1;
    if (voice->vol_offset != voice->vol_offset_target) voice->vol_offset += voice->vol_offset < voice->vol_offset_target ? 1 : -1;

    c->last_sample = (int16_t)sample;
    return (int16_t)sample;
}

int16_t upd933_update(upd933_t *c) {
    /* Order matters for modulation: each odd voice ("line 1") runs just
     * before its even partner ("line 2"), which reads it as last_sample. */
    static const int voice_map[UPD933_VOICES] = {5, 0, 7, 2, 1, 4, 3, 6};
    int32_t sample = 0;
    for (int j = 0; j < UPD933_VOICES; j++) sample += update_voice(c, voice_map[j]);
    c->sample_count++;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}

void upd933_set_expression(upd933_t *c, int vnum, int dcw_offset, int vol_offset, int immediate) {
    upd933_voice_t *v = &c->voice[vnum & 7];
    v->dcw_offset_target = (int16_t)(dcw_offset < -1024 ? -1024 : dcw_offset > 1024 ? 1024 : dcw_offset);
    v->vol_offset_target = (int16_t)(vol_offset < -511 ? -511 : vol_offset > 511 ? 511 : vol_offset);
    if (immediate) {
        v->dcw_offset = v->dcw_offset_target;
        v->vol_offset = v->vol_offset_target;
    }
}

uint8_t upd933_irq_data(upd933_t *c) {
    if (!c->irq_any) return 0;
    for (int i = 0; i < UPD933_VOICES; i++) {
        if (c->dco[i].irq) {
            c->dco[i].irq = 0;
            return (uint8_t)(4 | (i << 3));
        }
    }
    for (int i = 0; i < UPD933_VOICES; i++) {
        if (c->dcw[i].irq) {
            c->dcw[i].irq = 0;
            return (uint8_t)(2 | (i << 2));
        }
    }
    for (int i = 0; i < UPD933_VOICES; i++) {
        if (c->dca[i].irq) {
            c->dca[i].irq = 0;
            return (uint8_t)(1 | (i << 1));
        }
    }
    c->irq_any = 0;
    return 0;
}
