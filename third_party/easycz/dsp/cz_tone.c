#include "dsp/cz_tone.h"

#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* --- waveforms --- */

/* CZ waveform number 1-8 to chip waveform code and window. */
static const struct {
    uint8_t code, window;
} wave_map[9] = {
    {0, 0}, /* unused */
    {0, 0}, /* 1 sawtooth */
    {1, 0}, /* 2 square */
    {2, 0}, /* 3 pulse */
    {4, 0}, /* 4 double sine */
    {5, 0}, /* 5 saw-pulse */
    {6, 1}, /* 6 resonance I: saw window */
    {6, 2}, /* 7 resonance II: triangle window */
    {6, 3}, /* 8 resonance III: trapezoid window */
};

uint16_t cz_wave_bits(int wave1, int wave2) {
    wave1 = clampi(wave1, 1, 8);
    uint16_t bits = (uint16_t)(wave_map[wave1].code << 13 | wave_map[wave1].window << 6);
    if (wave2 >= 1 && wave2 <= 8 && !(wave1 >= 6 && wave2 >= 6)) {
        bits |= (uint16_t)(wave_map[wave2].code << 10 | 1 << 9);
        /* one window per voice; it comes from whichever wave is a resonance */
        if (wave2 >= 6) bits |= (uint16_t)(wave_map[wave2].window << 6);
    }
    return bits;
}

static int wave_number(int code, int window) {
    switch (code) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    case 4: return 4;
    case 5: return 5;
    case 6: return window >= 1 && window <= 3 ? 5 + window : 6;
    default: return 0; /* undocumented chip waves */
    }
}

void cz_wave_numbers(uint16_t wave, int *wave1, int *wave2) {
    const int window = (wave >> 6) & 7;
    *wave1 = wave_number((wave >> 13) & 7, window);
    *wave2 = (wave >> 9) & 1 ? wave_number((wave >> 10) & 7, window) : 0;
}

/* --- panel conversions (truncating integer math, as Casio's) --- */

int cz_rate_code(int kind, int panel) {
    panel = clampi(panel, 0, 99);
    switch (kind) {
    case CZ_DCA: return 119 * panel / 99;
    case CZ_DCW: return 119 * panel / 99 + 8;
    default: return 127 * panel / 99;
    }
}

int cz_level_code(int kind, int panel) {
    panel = clampi(panel, 0, 99);
    switch (kind) {
    case CZ_DCA: return panel ? panel + 28 : 0;
    case CZ_DCW: return 127 * panel / 99;
    default: return panel <= 63 ? panel : panel + 4; /* 0x44+ = 2-semitone units */
    }
}

/* Panel fine n (0-60) is stored in 1/64 semitones as n + (n-1)/15, so steps
 * 15->16, 30->31 and 45->46 are double and fine 60 is 63/64 semitone. */
int cz_detune_word(int sign, int oct, int note, int fine) {
    fine = clampi(fine, 0, 60);
    const int u = fine ? fine + (fine - 1) / 15 : 0;
    const int word = (clampi(oct, 0, 3) * 12 + clampi(note, 0, 11)) << 8 | u << 2;
    return sign < 0 ? -word : word;
}

int cz_detune_fine(int detune) {
    const int u = ((detune < 0 ? -detune : detune) & 0xff) >> 2;
    return u - (u >> 4);
}

static int nearest_panel(int (*to_code)(int, int), int kind, int code) {
    int best = 0, best_dist = 1 << 30;
    for (int p = 0; p <= 99; p++) {
        const int d = to_code(kind, p) - code;
        const int dist = d < 0 ? -d : d;
        if (dist < best_dist) {
            best = p;
            best_dist = dist;
            if (!dist) break;
        }
    }
    return best;
}

int cz_rate_panel(int kind, int code) { return nearest_panel(cz_rate_code, kind, code); }
int cz_level_panel(int kind, int code) { return nearest_panel(cz_level_code, kind, code); }

uint8_t cz_mod_bits(int mod) {
    return mod == CZ_MOD_RING ? 0x20 : mod == CZ_MOD_NOISE ? 0x18 : 0;
}

int cz_tone_mod(const cz_tone_t *t) {
    const int m = (t->mod_bits >> 3) & 7;
    return m == 4 ? CZ_MOD_RING : m == 3 ? CZ_MOD_NOISE : CZ_MOD_NONE;
}

static void env_from_panel(int kind, const cz_panel_env_t *p, cz_env_t *e) {
    for (int i = 0; i < CZ_STEPS; i++) {
        e->rate[i] = (uint8_t)cz_rate_code(kind, p->rate[i]);
        e->level[i] = (uint8_t)cz_level_code(kind, p->level[i]);
    }
    e->end = (uint8_t)(clampi(p->end, 1, 8) - 1);
    e->sustain = p->sustain >= 1 && p->sustain - 1 < e->end ? (uint8_t)(p->sustain - 1) : 0xff;
}

void cz_tone_from_panel(const cz_panel_tone_t *p, cz_tone_t *t) {
    memset(t, 0, sizeof(*t));
    t->line_select = (uint8_t)clampi(p->line_select, 0, 3);
    t->octave = (int8_t)clampi(p->octave, -1, 1);
    t->detune = (int16_t)cz_detune_word(p->detune_sign, p->detune_oct, p->detune_note, p->detune_fine);
    t->mod_bits = cz_mod_bits(p->mod);
    t->vib_wave = (uint8_t)clampi(p->vib_wave, 0, 3);
    t->vib_delay = (uint8_t)clampi(p->vib_delay, 0, 99);
    t->vib_rate = (uint8_t)clampi(p->vib_rate, 0, 99);
    t->vib_depth = (uint8_t)clampi(p->vib_depth, 0, 99);
    for (int l = 0; l < 2; l++) {
        /* an empty line 2 plays line 1's settings */
        const cz_panel_line_t *pl = l == 1 && p->line[1].wave1 == 0 ? &p->line[0] : &p->line[l];
        cz_line_t *tl = &t->line[l];
        tl->wave = cz_wave_bits(pl->wave1, pl->wave2);
        tl->dca_kf = (uint8_t)clampi(pl->dca_kf, 0, 9);
        tl->dcw_kf = (uint8_t)clampi(pl->dcw_kf, 0, 9);
        env_from_panel(CZ_DCA, &pl->dca, &tl->env[CZ_DCA]);
        env_from_panel(CZ_DCW, &pl->dcw, &tl->env[CZ_DCW]);
        env_from_panel(CZ_DCO, &pl->dco, &tl->env[CZ_DCO]);
    }
}

/* --- SysEx --- */

/* Where each line's fields sit in the 128 tone bytes. */
static const struct {
    int wave, kf, dca, dcw, dco;
} off[2] = {{0x0e, 0x10, 0x14, 0x25, 0x36}, {0x47, 0x49, 0x4d, 0x5e, 0x6f}};

static void env_decode(const uint8_t *raw, int end_off, int steps_off, cz_env_t *e) {
    e->end = (uint8_t)clampi(raw[end_off] & 0x0f, 0, 7);
    e->sustain = 0xff;
    for (int i = 0; i < CZ_STEPS; i++) {
        e->rate[i] = raw[steps_off + 2 * i] & 0x7f;     /* bit 7 = direction, recomputed */
        const uint8_t level = raw[steps_off + 2 * i + 1];
        e->level[i] = level & 0x7f;
        if ((level & 0x80) && e->sustain == 0xff && i < e->end) e->sustain = (uint8_t)i; /* end step wins */
    }
}

void cz_tone_decode(const uint8_t raw[CZ_TONE_BYTES], cz_tone_t *t) {

    memset(t, 0, sizeof(*t));
    t->line_select = raw[0x00] & 3;
    const int oct = (raw[0x00] >> 2) & 3;
    t->octave = (int8_t)(oct == 1 ? 1 : oct == 2 ? -1 : 0);
    const int detune = raw[0x03] << 8 | raw[0x02];
    t->detune = (int16_t)(raw[0x01] & 1 ? -detune : detune);

    switch (raw[0x04]) {
    case 0x04: t->vib_wave = CZ_VIB_SAW_UP; break;
    case 0x20: t->vib_wave = CZ_VIB_SAW_DOWN; break;
    case 0x02: t->vib_wave = CZ_VIB_SQUARE; break;
    default: t->vib_wave = CZ_VIB_TRI; break;
    }
    /* panel values; the stored words are recomputed (some editors write bad ones) */
    t->vib_delay = (uint8_t)clampi(raw[0x05], 0, 99);
    t->vib_rate = (uint8_t)clampi(raw[0x08], 0, 99);
    t->vib_depth = (uint8_t)clampi(raw[0x0b], 0, 99);

    for (int l = 0; l < 2; l++) {
        cz_line_t *tl = &t->line[l];
        const uint16_t word = (uint16_t)(raw[off[l].wave] << 8 | raw[off[l].wave + 1]);
        tl->wave = word & 0xffc0;
        if (l == 0) t->mod_bits = word & 0x3c; /* line 2's modulation bits are ignored */
        tl->dca_kf = (uint8_t)clampi(raw[off[l].kf] & 0x0f, 0, 9);
        tl->dcw_kf = (uint8_t)clampi(raw[off[l].kf + 2] & 0x0f, 0, 9);
        env_decode(raw, off[l].dca, off[l].dca + 1, &tl->env[CZ_DCA]);
        env_decode(raw, off[l].dcw, off[l].dcw + 1, &tl->env[CZ_DCW]);
        env_decode(raw, off[l].dco, off[l].dco + 1, &tl->env[CZ_DCO]);
    }
}

static void env_encode(const cz_env_t *e, uint8_t *raw, int end_off, int steps_off) {
    raw[end_off] = e->end & 7;
    int prev = 0;
    for (int i = 0; i < CZ_STEPS; i++) {
        const int level = e->level[i] & 0x7f;
        /* the stored direction bit: falling from the previous step's level */
        raw[steps_off + 2 * i] = (uint8_t)((e->rate[i] & 0x7f) | (level < prev ? 0x80 : 0));
        raw[steps_off + 2 * i + 1] = (uint8_t)(level | (i == e->sustain ? 0x80 : 0));
        prev = level;
    }
}

void cz_tone_encode(const cz_tone_t *t, uint8_t raw[CZ_TONE_BYTES]) {
    static const uint8_t vib_byte[4] = {0x08, 0x04, 0x20, 0x02};

    memset(raw, 0, CZ_TONE_BYTES);
    raw[0x00] = (uint8_t)((t->line_select & 3) | (t->octave > 0 ? 1 : t->octave < 0 ? 2 : 0) << 2);
    const int detune = t->detune < 0 ? -t->detune : t->detune;
    raw[0x01] = t->detune < 0;
    raw[0x02] = (uint8_t)(detune & 0xff);
    raw[0x03] = (uint8_t)(detune >> 8);
    raw[0x04] = vib_byte[t->vib_wave & 3];

    /* panel value, then the firmware's 16-bit word, LSB first */
    uint16_t delay, rate, depth;
    cz_vibrato_words(t, &delay, &rate, &depth);
    const uint8_t vib[9] = {t->vib_delay, delay & 0xff, delay >> 8, t->vib_rate, rate & 0xff, rate >> 8,
                            t->vib_depth, depth & 0xff, depth >> 8};
    memcpy(&raw[0x05], vib, sizeof(vib));

    for (int l = 0; l < 2; l++) {
        const cz_line_t *tl = &t->line[l];
        const uint16_t word = (uint16_t)(tl->wave & 0xffc0) | (l == 0 ? (t->mod_bits & 0x3c) : 0);
        raw[off[l].wave] = (uint8_t)(word >> 8);
        raw[off[l].wave + 1] = (uint8_t)(word & 0xff);
        raw[off[l].kf] = tl->dca_kf;
        raw[off[l].kf + 1] = cz_dca_kf_mult[tl->dca_kf % 10];
        raw[off[l].kf + 2] = tl->dcw_kf;
        raw[off[l].kf + 3] = cz_dcw_kf_mult[tl->dcw_kf % 10];
        env_encode(&tl->env[CZ_DCA], raw, off[l].dca, off[l].dca + 1);
        env_encode(&tl->env[CZ_DCW], raw, off[l].dcw, off[l].dcw + 1);
        env_encode(&tl->env[CZ_DCO], raw, off[l].dco, off[l].dco + 1);
    }
}

void cz_syx_encode(const uint8_t raw[CZ_TONE_BYTES], uint8_t out[CZ_SYX_BYTES]) {
    static const uint8_t header[7] = {0xf0, 0x44, 0x00, 0x00, 0x70, 0x20, 0x60};
    memcpy(out, header, sizeof(header));
    for (int k = 0; k < CZ_TONE_BYTES; k++) { /* low nibble first */
        out[7 + 2 * k] = raw[k] & 0x0f;
        out[8 + 2 * k] = raw[k] >> 4;
    }
    out[CZ_SYX_BYTES - 1] = 0xf7;
}

int cz_syx_next(const uint8_t *data, int len, uint8_t raw[CZ_TONE_BYTES], char name[17], int *found) {
    *found = 0;
    name[0] = '\0';
    for (int i = 0; i < len; i++) {
        if (data[i] != 0xf0) continue;
        if (i + 6 >= len) return len;
        const uint8_t *h = &data[i];
        if (h[1] != 0x44 || h[2] != 0x00 || h[3] != 0x00 || (h[4] & 0xf0) != 0x70) continue;

        int start, bytes;
        if (h[5] == 0x20) { /* send to CZ: + program byte */
            start = i + 7;
            bytes = CZ_TONE_BYTES;
        } else if (h[5] == 0x30) { /* dump from CZ */
            start = i + 6;
            bytes = CZ_TONE_BYTES;
        } else if (h[5] == 0x21) { /* CZ-1 style, 144 bytes with a name */
            start = i + 7;
            bytes = 144;
        } else {
            continue;
        }
        if (start + 2 * bytes >= len || data[start + 2 * bytes] != 0xf7) continue;

        for (int k = 0; k < CZ_TONE_BYTES; k++)
            raw[k] = (uint8_t)((data[start + 2 * k] & 0x0f) | (data[start + 2 * k + 1] & 0x0f) << 4);
        if (bytes == 144) {
            int n = 0;
            for (int k = CZ_TONE_BYTES; k < 144 && n < 16; k++) {
                const char c = (char)((data[start + 2 * k] & 0x0f) | (data[start + 2 * k + 1] & 0x0f) << 4);
                name[n++] = c >= 0x20 && c < 0x7f ? c : ' ';
            }
            while (n > 0 && name[n - 1] == ' ') n--;
            name[n] = '\0';
        }
        *found = 1;
        return start + 2 * bytes + 1;
    }
    return len;
}

/* --- firmware tables (CZ-101 v2 ROM, via the research report) --- */

const uint8_t cz_kf_curve[61] = {
    0,  0,  1,  1,  1,  1,  2,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  9,
    10, 11, 11, 12, 13, 14, 15, 17, 18, 19, 20, 22, 23, 25, 27, 29, 31, 33, 35, 37, 40,
    42, 45, 48, 51, 54, 58, 61, 65, 69, 74, 78, 83, 89, 94, 100, 106, 113, 120, 127,
};
const uint8_t cz_dca_kf_mult[10] = {0x00, 0x08, 0x11, 0x1a, 0x24, 0x2f, 0x3a, 0x45, 0x52, 0x5f};
const uint8_t cz_dcw_kf_mult[10] = {0x00, 0x1f, 0x2c, 0x39, 0x46, 0x53, 0x60, 0x6e, 0x92, 0xff};

/* The DCO level is capped so base pitch + DCO envelope stays at or below
 * about chip semitone 113: 0x66 falling one code per two semitones to 0x44,
 * then steeper; 0 past the table. */
uint8_t cz_dco_cap(int semi) {
    static const uint8_t top[7] = {0x38, 0x30, 0x28, 0x20, 0x18, 0x10, 0x08};
    if (semi <= 35) return 0x7f;
    const int i = (semi < 113 ? semi : 113) - 36;
    if (i < 70) return (uint8_t)(0x66 - (i >> 1));
    if (i < 77) return top[i - 70];
    return 0;
}

void cz_vibrato_words(const cz_tone_t *t, uint16_t *delay, uint16_t *rate, uint16_t *depth) {
    *delay = cz_expand(t->vib_delay);
    *rate = (uint16_t)(cz_expand((uint8_t)(t->vib_rate + 1)) << 5);
    *depth = t->vib_depth == 99 ? 0x300 : cz_expand((uint8_t)(t->vib_depth + 1));
}

uint16_t cz_expand(uint8_t x) {
    const int m = x & 15, e = (x >> 4) & 7;
    return (uint16_t)(e == 0 ? m : ((16 | m) << (e - 1)) | ((1 << (e - 1)) - 1));
}
