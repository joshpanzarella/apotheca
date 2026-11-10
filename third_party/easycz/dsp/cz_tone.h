/*
 * A CZ tone (patch), in the CZ-101's own terms.
 *
 * cz_tone_t holds what the CZ-101 keeps in its 128-byte tone memory, decoded:
 * envelope rates and levels are the 7-bit machine codes the firmware sends to
 * the chip, so any tone loaded from SysEx plays exactly as stored, including
 * values beyond what the front panel can set. cz_panel_tone_t is the same tone
 * in front-panel units (0-99 and so on), for presets and editing.
 *
 * Conversions and tables are from the research report (reports/Casio CZ
 * synthesis engine.md): Casio's panel-to-code formulas as checked by
 * Kasploosh and Bajoras's 1980s editor, and the SysEx layout with the
 * corrections listed there.
 */
#ifndef EASYCZ_CZ_TONE_H
#define EASYCZ_CZ_TONE_H

#include <stdint.h>

#define CZ_STEPS 8
#define CZ_TONE_BYTES 128

enum { CZ_LINE_1, CZ_LINE_2, CZ_LINE_1_1, CZ_LINE_1_2 }; /* line select: 1, 2, 1+1', 1+2' */
enum { CZ_MOD_NONE, CZ_MOD_RING, CZ_MOD_NOISE };
enum { CZ_VIB_TRI, CZ_VIB_SAW_UP, CZ_VIB_SAW_DOWN, CZ_VIB_SQUARE };
enum { CZ_DCA, CZ_DCW, CZ_DCO }; /* envelope kinds; same order as UPD933_ENV_* */

typedef struct {
    uint8_t rate[CZ_STEPS];  /* 7-bit chip rate codes */
    uint8_t level[CZ_STEPS]; /* 7-bit chip level codes */
    uint8_t sustain;         /* 0-based sustain step, or 0xff for none */
    uint8_t end;             /* 0-based end step */
} cz_env_t;

typedef struct {
    uint16_t wave;   /* chip waveform register bits 15-6: waves, enable, window */
    uint8_t dca_kf;  /* key follow 0-9 */
    uint8_t dcw_kf;
    cz_env_t env[3]; /* CZ_DCA, CZ_DCW, CZ_DCO */
} cz_line_t;

typedef struct {
    uint8_t line_select; /* CZ_LINE_* */
    int8_t octave;       /* -1, 0, +1 */
    int16_t detune;      /* primed line only, 1/256 semitone, signed */
    uint8_t mod_bits;    /* line-1 waveform word bits 5-2: ring 0x20, noise
                          * 0x18, mute line 1 0x04, and the hidden SysEx-only
                          * combinations. Two-line modes only. */
    uint8_t vib_wave;    /* CZ_VIB_* */
    uint8_t vib_delay, vib_rate, vib_depth; /* panel 0-99 */
    cz_line_t line[2];
} cz_tone_t;

/* --- front-panel form --- */

typedef struct {
    uint8_t rate[CZ_STEPS], level[CZ_STEPS]; /* 0-99 */
    uint8_t sustain;                         /* panel step 1-8, 0 = none */
    uint8_t end;                             /* panel step 1-8 */
} cz_panel_env_t;

typedef struct {
    uint8_t wave1, wave2; /* CZ waveform 1-8; wave2 0 = none */
    uint8_t dca_kf, dcw_kf;
    cz_panel_env_t dca, dcw, dco;
} cz_panel_line_t;

typedef struct {
    uint8_t line_select;
    int8_t octave;
    int8_t detune_sign;  /* +1 or -1 */
    uint8_t detune_oct;  /* 0-3 */
    uint8_t detune_note; /* 0-11 */
    uint8_t detune_fine; /* 0-60 */
    uint8_t mod;
    uint8_t vib_wave, vib_delay, vib_rate, vib_depth;
    cz_panel_line_t line[2];
} cz_panel_tone_t;

void cz_tone_from_panel(const cz_panel_tone_t *p, cz_tone_t *t);

/* CZ_MOD_* for the panel; hidden SysEx combinations read as CZ_MOD_NONE. */
int cz_tone_mod(const cz_tone_t *t);
uint8_t cz_mod_bits(int mod);

/* Decode 128 tone bytes (as rebuilt from SysEx nibbles). */
void cz_tone_decode(const uint8_t raw[CZ_TONE_BYTES], cz_tone_t *t);

/* Encode a tone as the CZ-101 stores it, with the firmware's vibrato words
 * and key follow multipliers, so real hardware plays it the same. Decoding
 * the result gives the same tone back. */
void cz_tone_encode(const cz_tone_t *t, uint8_t raw[CZ_TONE_BYTES]);

#define CZ_SYX_BYTES 264

/* A tone as the 264-byte "send to CZ" SysEx message (to the edit buffer,
 * MIDI channel 1), the form CZ editors and the CZ itself accept. */
void cz_syx_encode(const uint8_t raw[CZ_TONE_BYTES], uint8_t out[CZ_SYX_BYTES]);

/* Find the next tone in a .syx buffer (264-, 263- or 296-byte forms; banks
 * are concatenations). Returns the bytes consumed from data (all of it when
 * no tone is left); *found is set when raw[] was filled. name gets the CZ-1 style name
 * (296-byte form) or "". */
int cz_syx_next(const uint8_t *data, int len, uint8_t raw[CZ_TONE_BYTES], char name[17], int *found);

/* Waveform register bits for a CZ waveform pair (wave2 0 = none). Two
 * resonance waves cannot be combined; the second is then dropped. */
uint16_t cz_wave_bits(int wave1, int wave2);

/* The panel waveforms (1-8, 0 if none) of a waveform register value. */
void cz_wave_numbers(uint16_t wave, int *wave1, int *wave2);

/* Panel values to machine codes. */
int cz_rate_code(int kind, int panel);
int cz_level_code(int kind, int panel);
int cz_detune_word(int sign, int oct, int note, int fine);

/* Machine codes back to panel values: the panel value that encodes to the
 * code, or the nearest one for codes the panel cannot set (from SysEx). */
int cz_rate_panel(int kind, int code);
int cz_level_panel(int kind, int code);
int cz_detune_fine(int detune); /* panel fine 0-60 of a detune word */

/* --- firmware tables --- */

extern const uint8_t cz_kf_curve[61]; /* key follow curve, C2..C7 */
extern const uint8_t cz_dca_kf_mult[10];
extern const uint8_t cz_dcw_kf_mult[10];
uint8_t cz_dco_cap(int semi); /* DCO level ceiling for a chip semitone */

/* The firmware's mantissa/exponent expansion for vibrato delay/rate/depth. */
uint16_t cz_expand(uint8_t x);

/* A tone's vibrato as the firmware runs it: the delay in ticks, the rate
 * and depth words. */
void cz_vibrato_words(const cz_tone_t *t, uint16_t *delay, uint16_t *rate, uint16_t *depth);

#endif
