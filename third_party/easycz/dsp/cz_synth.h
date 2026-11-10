/*
 * The CZ "firmware": turns MIDI notes into uPD933 register writes the way
 * the CZ-101's CPU does, and runs the audio path to 44.1 kHz. The algorithms
 * are restated from the research report (reports/Casio CZ synthesis
 * engine.md), which cites the firmware disassembly and hardware measurements
 * they come from.
 *
 * Two chips, like the CZ-1 and CZ-5000: 16 chip voices, so 16 notes in the
 * single-line modes (1, 2) and 8 in the two-line modes (1+1', 1+2').
 *
 * Deliberate differences from the CZ-101: twice the polyphony; MIDI notes are
 * not folded into C2-C7, so the Move's full range plays; pitch bend uses all
 * 14 bits; a sustain pedal; and optional velocity sensitivity (off by
 * default, as on the CZ-101, which ignores velocity).
 *
 * Portamento is the CZ's: every glide takes the same time whatever the
 * interval, linear in semitones, stepped on the 122 Hz firmware tick. In Solo
 * mode (one note, last-note priority) a glide happens only when playing
 * legato, and a legato note does not restart the envelopes. In poly mode each
 * new note glides from the last note played.
 */
#ifndef EASYCZ_CZ_SYNTH_H
#define EASYCZ_CZ_SYNTH_H

#include <stdint.h>

#include "dsp/cz_chorus.h"
#include "dsp/cz_output.h"
#include "dsp/cz_tone.h"
#include "dsp/upd933.h"

#define CZ_CHIPS 2
#define CZ_VOICES (CZ_CHIPS * UPD933_VOICES)
#define CZ_SOLO_STACK 8

/* Where pad pressure goes (not on the CZ-101; the CZ-1 had aftertouch). */
enum { CZ_PRESS_OFF, CZ_PRESS_VIB, CZ_PRESS_DCW, CZ_PRESS_AMP };

/* Master tune: 0-128 in 1/64 semitones around the chip's A = 442 Hz. */
#define CZ_TUNE_442 64
#define CZ_TUNE_440 59 /* -7.81 cents: A = 440.01 Hz */

typedef struct {
    uint8_t step;        /* 0-based step being sent or held */
    uint8_t sustaining;  /* holding at the sustain step */
    uint8_t sus_pending; /* sustain step sent, not yet arrived */
} cz_env_state_t;

/* One chip voice. */
typedef struct {
    int slot;          /* note slot that owns this voice */
    uint8_t line;      /* tone line whose parameters it plays */
    uint8_t primed;    /* the detuned, modulated voice of a two-line note */
    uint8_t active;    /* DCA running its envelope */
    uint8_t fading;    /* DCA ramping down for a steal or a tone change */
    uint8_t reso;      /* plays a resonance wave: DCW floor 7 */
    int base_word;     /* 8.8 semitones: note, octave, tune, detune */
    int semi;          /* integer chip semitone, for key follow and the DCO cap */
    uint8_t dca_add;   /* key follow: added to DCA rates */
    uint8_t dcw_sub;   /* key follow: subtracted from DCW levels */
    uint16_t pitch_reg;
    uint8_t dca_vsub;  /* velocity: subtracted from DCA levels */
    uint8_t dcw_vsub;  /* velocity: subtracted from DCW levels */
    int glide_dist;    /* portamento: starting offset, 1/256 semitone */
    int glide_acc;     /* progress in 1/2048ths; 2048 = arrived */
    int glide_rate;    /* added per firmware tick */
    cz_env_state_t env[3];
} cz_voice_t;

typedef enum { SLOT_IDLE, SLOT_PLAYING, SLOT_STEALING } cz_slot_state_t;

/* One note: one chip voice in single-line modes, a pair in two-line modes. */
typedef struct {
    cz_slot_state_t state;
    int note;
    int key_held;
    int sustained;    /* key up, held by the sustain pedal */
    int velocity;
    int pressure;     /* 0-127, from poly or channel pressure */
    int pending_note;
    int pending_velocity;
    int pending_glide;
    uint8_t voice[2]; /* line 1 (odd chip voice), primed line (even) */
    uint8_t nvoices;
    uint8_t fades;    /* steal fades still to arrive */
} cz_slot_t;

typedef struct {
    upd933_tables_t tables;
    upd933_t chip[CZ_CHIPS];
    uint16_t wave_bits[CZ_CHIPS][UPD933_VOICES]; /* register bits 15-6 per voice */
    uint8_t mod_bits[CZ_CHIPS][UPD933_VOICES];   /* bits 5-2, even voices only */

    cz_tone_t tone;
    cz_voice_t voice[CZ_VOICES];
    cz_slot_t slot[CZ_VOICES];
    int nslots;
    int last_slot; /* round-robin position */
    int held_keys;

    /* global settings */
    int master_tune; /* CZ_TUNE_* or anything in 0-128 */
    int bend_range;  /* semitones, 0-12 */
    int bend;        /* MIDI pitch bend, -8192..8191 */
    int solo;        /* Solo mode: one note, last-note priority */
    int porta;       /* portamento on */
    int porta_time;  /* 0-99 */
    int vel_amp;     /* velocity sensitivity 0-9 for DCA levels; 0 = none */
    int vel_wave;    /* ... for DCW levels */
    int pedal;       /* sustain pedal down */
    int press_dest;  /* CZ_PRESS_* */
    int press_amt;   /* 0-99 */
    int wheel_vib;   /* the mod wheel adds vibrato */
    int wheel;       /* mod wheel, 0-127 */
    uint16_t elfo_phase; /* expression vibrato LFO, 5.5 Hz triangle */
    int elfo_out;        /* -256..256 */

    int last_word;   /* 8.8 pitch of the last note played (for glides), -1 if none */
    int stack[CZ_SOLO_STACK]; /* Solo: held notes, newest last */
    int stack_vel[CZ_SOLO_STACK];
    int stack_n;

    /* vibrato: one LFO per tone, stepped on the firmware tick */
    uint16_t vib_rate_word, vib_depth_word, vib_delay_ticks;
    uint16_t lfo_phase;
    int lfo_delay;
    int lfo_out; /* 1/256 semitone */
    int lfo_used;

    uint32_t tick_acc; /* firmware tick, in chip samples x 1000 */

    resampler_t rs;
    dc_block_t dc;
    cz_chorus_t chorus;
    int volume_db;         /* the patch's volume, -24..+6 dB */
    float gain, gain_target;
    uint32_t quiet_run;    /* chip samples in a row with no output */
    uint32_t chorus_quiet; /* output samples in a row of silence into the chorus */
} cz_synth_t;

/* Not realtime-safe (builds tables, allocates the resampler). 0 on success. */
int cz_synth_init(cz_synth_t *s);
void cz_synth_free(cz_synth_t *s);

/* Select a tone, as the CZ does on a tone change: sounding notes fade out
 * (about 16 ms) and held keys are forgotten. */
void cz_synth_select_tone(cz_synth_t *s, const cz_tone_t *tone);

/* Edit the current tone. Waveforms, modulation, detune, octave and vibrato
 * also change notes already sounding; envelopes and key follow apply from the
 * next note. A line select change re-voices, so it fades like a tone change. */
void cz_synth_edit_tone(cz_synth_t *s, const cz_tone_t *tone);

/* The patch's volume in dB (-24..+6); it glides there over ~10 ms. */
void cz_synth_set_volume(cz_synth_t *s, int db);

/* Switch Solo mode; sounding notes fade as on a tone change. */
void cz_synth_set_solo(cz_synth_t *s, int solo);

void cz_synth_note_on(cz_synth_t *s, int note, int velocity);
void cz_synth_note_off(cz_synth_t *s, int note);
void cz_synth_sustain(cz_synth_t *s, int down);

/* Pressure 0-127 for one note, or for every note when note is -1. */
void cz_synth_pressure(cz_synth_t *s, int note, int value);
void cz_synth_mod_wheel(cz_synth_t *s, int value);
void cz_synth_all_notes_off(cz_synth_t *s);
void cz_synth_all_sound_off(cz_synth_t *s);
void cz_synth_pitch_bend(cz_synth_t *s, int value); /* -8192..8191 */

/* Mono output at 44.1 kHz, nominally -1..1, without the chorus. */
void cz_synth_render(cz_synth_t *s, float *out, int frames);

/* Stereo output through the chorus (dry and centred when it is off). */
void cz_synth_render_stereo(cz_synth_t *s, float *left, float *right, int frames);

#endif
