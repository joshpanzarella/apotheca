#include "dsp/cz_synth.h"

#include <math.h>
#include <string.h>

/* Single-line slots map to chip voices in this order (CZ-101 firmware). */
static const uint8_t slot_voice[UPD933_VOICES] = {0, 5, 2, 7, 4, 1, 6, 3};
/* Two-line pairs: even (primed) voice and its odd line-1 partner. */
static const uint8_t pair_even[4] = {0, 2, 4, 6};
static const uint8_t pair_odd[4] = {5, 7, 1, 3};

/* Fixed chip rates the firmware uses. */
#define RATE_STEAL 0x6b       /* reused voice fades ~9 ms before its new note */
#define RATE_TONE_CHANGE 0x65 /* ~16 ms */
#define RATE_RELEASE_MAX 0x66 /* the DCA end step is never faster */
#define PITCH_MAX 0x7100      /* firmware clamp: chip semitone 113 */

/* Firmware tick: 8.1912 ms = 327.648 chip samples. */
#define TICK_CHIP_SAMPLES_X1000 327648u

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static upd933_t *voice_chip(cz_synth_t *s, int vi) { return &s->chip[vi >> 3]; }
static int voice_vnum(int vi) { return vi & 7; }

static upd933_env_t *chip_env(cz_synth_t *s, int vi, int kind) {
    upd933_t *c = voice_chip(s, vi);
    const int v = voice_vnum(vi);
    return kind == CZ_DCA ? &c->dca[v] : kind == CZ_DCW ? &c->dcw[v] : &c->dco[v];
}

static int dual_line(const cz_tone_t *t) { return t->line_select >= CZ_LINE_1_1; }

/* --- chip register writes --- */

static void write_wave_reg(cz_synth_t *s, int chip, int n) {
    /* register 68+n carries voice n's waveform and, for even n, voice n-2's
     * modulation bits */
    uint16_t v = s->wave_bits[chip][n];
    if (!(n & 1)) v |= s->mod_bits[chip][(n + 6) & 7];
    upd933_write(&s->chip[chip], (uint8_t)(UPD933_REG_WAVE + n), v);
}

static void set_wave(cz_synth_t *s, int vi, uint16_t bits) {
    s->wave_bits[vi >> 3][vi & 7] = bits & 0xffc0;
    write_wave_reg(s, vi >> 3, vi & 7);
}

static void set_mod(cz_synth_t *s, int vi, uint8_t bits) {
    if (vi & 1) return; /* only even voices can be modulated */
    s->mod_bits[vi >> 3][vi & 7] = bits & 0x3c;
    write_wave_reg(s, vi >> 3, ((vi & 7) + 2) & 7);
}

static const uint8_t reg_base[3] = {UPD933_REG_DCA, UPD933_REG_DCW, UPD933_REG_DCO}; /* by CZ_DCA.. */

/* Ramp an envelope to a level code, direction picked from where it is now. */
static void env_write(cz_synth_t *s, int vi, int kind, int rate, int hold, int level) {
    const int down = upd933_env_target(kind, level) < chip_env(s, vi, kind)->current;
    upd933_write(voice_chip(s, vi), (uint8_t)(reg_base[kind] + voice_vnum(vi)), upd933_env_step(down, rate, hold, level));
}

/* Jump an envelope straight to 0: the chip snaps when the direction bit
 * disagrees with the actual direction. */
static void env_snap_zero(cz_synth_t *s, int vi, int kind) {
    upd933_write(voice_chip(s, vi), (uint8_t)(reg_base[kind] + voice_vnum(vi)), upd933_env_step(0, 0, 1, 0));
}

/* The 8.8 pitch word of a note before detune: note, octave, master tune. */
static int note_word(const cz_synth_t *s, int note) {
    return (note - 7) * 256 + s->tone.octave * 12 * 256 + (s->master_tune - 64) * 4;
}

static int glide_offset(const cz_voice_t *v) {
    return v->glide_acc >= 2048 ? 0 : v->glide_dist - v->glide_dist * v->glide_acc / 2048;
}

/* Expression vibrato depth for a voice, in 1/256 semitone at the LFO's peak:
 * up to 1 semitone from full pressure, 0.75 from the mod wheel. */
static int expression_vib(const cz_synth_t *s, const cz_voice_t *v) {
    int depth = 0;
    if (s->press_dest == CZ_PRESS_VIB) depth += s->slot[v->slot].pressure * s->press_amt * 256 / (127 * 99);
    if (s->wheel_vib) depth += s->wheel * 192 / 127;
    return depth;
}

/* Pressure to the chip's DCW and volume offsets: up to half the DCW range
 * brighter, or up to 40 DCA codes (20 dB) quieter until pressed. */
static void apply_expression(cz_synth_t *s, int vi, int immediate) {
    const int p = s->slot[s->voice[vi].slot].pressure;
    const int dcw = s->press_dest == CZ_PRESS_DCW ? p * s->press_amt * 512 / (127 * 99) : 0;
    const int vol = s->press_dest == CZ_PRESS_AMP ? -(127 - p) * s->press_amt * 160 / (127 * 99) : 0;
    upd933_set_expression(voice_chip(s, vi), voice_vnum(vi), dcw, vol, immediate);
}

static int pitch_word(const cz_synth_t *s, const cz_voice_t *v) {
    int word = v->base_word + glide_offset(v) + s->bend_range * 256 * s->bend / 8192;
    if (s->tone.vib_depth) word += s->lfo_out;
    word += s->elfo_out * expression_vib(s, v) / 256;
    return clampi(word, 0, PITCH_MAX);
}

static void write_pitch(cz_synth_t *s, int vi) {
    cz_voice_t *v = &s->voice[vi];
    const uint16_t reg = (uint16_t)(2 * pitch_word(s, v)); /* 8.8 word to the chip's 7.9 */
    if (reg == v->pitch_reg) return;
    v->pitch_reg = reg;
    upd933_write(voice_chip(s, vi), (uint8_t)(UPD933_REG_PITCH + voice_vnum(vi)), reg);
}

/* --- envelope step machine --- */

static void env_send(cz_synth_t *s, int vi, int kind) {
    cz_voice_t *v = &s->voice[vi];
    const cz_env_t *e = &s->tone.line[v->line].env[kind];
    cz_env_state_t *st = &v->env[kind];
    if (st->step > e->end) st->step = e->end;

    int rate = e->rate[st->step] & 0x7f;
    int level = e->level[st->step] & 0x7f;
    int hold;

    if (kind == CZ_DCA) {
        rate = rate + v->dca_add < 0x7f ? rate + v->dca_add : 0x7f;
        level = level > v->dca_vsub ? level - v->dca_vsub : 0;
    }
    if (kind == CZ_DCW) {
        const int sub = v->dcw_sub + v->dcw_vsub;
        level = level > sub ? level - sub : 0;
        if (v->reso && level < 7) level = 7;
    }
    if (kind == CZ_DCO) {
        const int cap = cz_dco_cap(v->semi);
        if (level > cap) level = cap;
    }

    st->sus_pending = 0;
    if (st->step == e->end) {
        level = kind == CZ_DCW && v->reso ? 7 : 0;
        if (kind == CZ_DCA) {
            if (rate > RATE_RELEASE_MAX) rate = RATE_RELEASE_MAX;
            hold = 0; /* its arrival ends the voice */
        } else {
            hold = 1;
        }
    } else if (st->step == e->sustain) {
        /* sent without the hold bit first; holds once it arrives */
        hold = st->sustaining;
        st->sus_pending = !st->sustaining;
    } else {
        hold = 0;
    }
    env_write(s, vi, kind, rate, hold, level);
}

static void slot_check_idle(cz_synth_t *s, int si) {
    cz_slot_t *sl = &s->slot[si];
    if (sl->state != SLOT_PLAYING || sl->key_held || sl->sustained) return;
    for (int i = 0; i < sl->nvoices; i++)
        if (s->voice[sl->voice[i]].active || s->voice[sl->voice[i]].fading) return;
    sl->state = SLOT_IDLE;
}

static void finish_voice(cz_synth_t *s, int vi) {
    cz_voice_t *v = &s->voice[vi];
    v->active = 0;
    v->fading = 0;
    env_snap_zero(s, vi, CZ_DCW);
    env_snap_zero(s, vi, CZ_DCO);
}

static void env_arrived(cz_synth_t *s, int vi, int kind) {
    cz_voice_t *v = &s->voice[vi];
    cz_env_state_t *st = &v->env[kind];
    if (st->step >= s->tone.line[v->line].env[kind].end) {
        if (kind == CZ_DCA) {
            finish_voice(s, vi);
            slot_check_idle(s, v->slot);
        }
        return;
    }
    if (st->sus_pending) {
        st->sus_pending = 0;
        st->sustaining = 1;
        return;
    }
    st->step++;
    env_send(s, vi, kind);
}

static void env_key_off(cz_synth_t *s, int vi, int kind) {
    cz_env_state_t *st = &s->voice[vi].env[kind];
    st->step = st->sustaining ? st->step + 1 : s->tone.line[s->voice[vi].line].env[kind].end;
    st->sustaining = 0;
    st->sus_pending = 0;
    env_send(s, vi, kind);
}

/* --- notes --- */

/* Portamento: glide from the last note played, taking 2048/rate ticks
 * whatever the interval. The rate for times below 5 is a table, above it the
 * firmware's expansion (time 99 = 16.8 s). */
static int glide_rate(int time) {
    static const int fast[5] = {0x800, 0x400, 0x380, 0x300, 0x280};
    time = clampi(time, 0, 99);
    return time < 5 ? fast[time] : cz_expand((uint8_t)(100 - time));
}

int cz_synth_glide_rate16(float seconds) {
    const double most = 2048.0 * 65536.0;
    if (!(seconds > 0.f)) return (int)most;
    const double ticks = (double)seconds * 40000.0 * 1000.0 / TICK_CHIP_SAMPLES_X1000;
    const double rate = most / ticks;
    return rate >= most ? (int)most : (rate < 1.0 ? 1 : (int)(rate + 0.5));
}

static void set_glide(cz_synth_t *s, cz_voice_t *v, int word, int glide) {
    if (glide && s->porta && s->last_word >= 0 && s->last_word != word) {
        v->glide_dist = s->last_word - word;
        v->glide_acc = 0;
        v->glide_rate = s->porta_rate16 > 0 ? 0 : glide_rate(s->porta_time);
        v->glide_rate16 = s->porta_rate16 > 0 ? s->porta_rate16 : 0;
        v->glide_frac = 0;
    } else {
        v->glide_dist = 0;
        v->glide_acc = 2048;
    }
}

static void start_voice(cz_synth_t *s, int vi, int note, int velocity, int glide, int primed) {
    const cz_tone_t *t = &s->tone;
    cz_voice_t *v = &s->voice[vi];

    v->primed = (uint8_t)primed;
    v->line = (uint8_t)((t->line_select == CZ_LINE_2 || (primed && t->line_select == CZ_LINE_1_2)) ? 1 : 0);
    const cz_line_t *line = &t->line[v->line];

    const int word = note_word(s, note);
    v->base_word = word + (primed ? t->detune : 0);
    v->semi = clampi(v->base_word, 0, PITCH_MAX) >> 8;
    set_glide(s, v, word, glide);

    /* velocity (not on the CZ-101): softer notes lower the DCA levels by up
     * to 40 codes (20 dB) and the DCW levels by up to 64 at sensitivity 9 */
    const int soft = 127 - clampi(velocity, 1, 127);
    v->dca_vsub = (uint8_t)(soft * s->vel_amp * 40 / (127 * 9));
    v->dcw_vsub = (uint8_t)(soft * s->vel_wave * 64 / (127 * 9));

    /* key follow, once per note, from the voice's own semitone */
    const int idx = clampi(v->semi - 29, 0, 60);
    v->dca_add = (uint8_t)((cz_kf_curve[idx] * cz_dca_kf_mult[line->dca_kf]) >> 8);
    v->dcw_sub = (uint8_t)((cz_kf_curve[idx] * cz_dcw_kf_mult[line->dcw_kf]) >> 8);

    const int code1 = (line->wave >> 13) & 7, code2 = (line->wave >> 10) & 7;
    v->reso = (uint8_t)(code1 == 6 || ((line->wave >> 9) & 1 && code2 == 6));

    set_wave(s, vi, line->wave);
    set_mod(s, vi, dual_line(t) && primed ? t->mod_bits : 0);
    upd933_write(voice_chip(s, vi), (uint8_t)(UPD933_REG_PHASE + voice_vnum(vi)), 0);
    v->pitch_reg = 0xffff;
    write_pitch(s, vi);
    apply_expression(s, vi, 1);

    memset(v->env, 0, sizeof(v->env));
    v->active = 1;
    v->fading = 0;
}

static void start_note(cz_synth_t *s, int si, int note, int velocity, int glide) {
    cz_slot_t *sl = &s->slot[si];
    sl->state = SLOT_PLAYING;
    sl->note = note;
    sl->velocity = velocity;
    sl->pending_note = -1;
    for (int i = 0; i < sl->nvoices; i++) start_voice(s, sl->voice[i], note, velocity, glide, i == 1);
    s->last_word = note_word(s, note);
    /* step 1 of all DCAs, then all DCWs, then all DCOs */
    for (int kind = CZ_DCA; kind <= CZ_DCO; kind++)
        for (int i = 0; i < sl->nvoices; i++) env_send(s, sl->voice[i], kind);
}

static void release_slot(cz_synth_t *s, int si) {
    cz_slot_t *sl = &s->slot[si];
    for (int i = 0; i < sl->nvoices; i++) {
        const int vi = sl->voice[i];
        if (!s->voice[vi].active || s->voice[vi].fading) continue;
        for (int kind = CZ_DCA; kind <= CZ_DCO; kind++) env_key_off(s, vi, kind);
    }
}

static void fade_voice(cz_synth_t *s, int vi, int rate) {
    cz_voice_t *v = &s->voice[vi];
    v->fading = 1;
    env_write(s, vi, CZ_DCA, rate, 0, 0);
}

/* Lay the note slots over the chip voices for the current line mode. */
static void map_slots(cz_synth_t *s) {
    const int dual = dual_line(&s->tone);
    s->nslots = s->solo ? 1 : dual ? CZ_VOICES / 2 : CZ_VOICES;
    /* every slot reset (no stale steal left behind a mode change), and every
     * voice outside the new layout parked on slot 0 without belonging to it */
    memset(s->slot, 0, sizeof(s->slot));
    for (int vi = 0; vi < CZ_VOICES; vi++) s->voice[vi].slot = 0;
    for (int si = 0; si < s->nslots; si++) {
        cz_slot_t *sl = &s->slot[si];
        const int chip = si & 1;
        memset(sl, 0, sizeof(*sl));
        sl->note = -1;
        sl->pending_note = -1;
        if (dual) {
            sl->nvoices = 2;
            sl->voice[0] = (uint8_t)(chip * 8 + pair_odd[si >> 1]);
            sl->voice[1] = (uint8_t)(chip * 8 + pair_even[si >> 1]);
        } else {
            sl->nvoices = 1;
            sl->voice[0] = (uint8_t)(chip * 8 + slot_voice[si >> 1]);
        }
        for (int i = 0; i < sl->nvoices; i++) s->voice[sl->voice[i]].slot = si;
    }
    s->last_slot = s->nslots - 1;
    s->held_keys = 0;
    s->stack_n = 0;
}

static void update_vibrato_words(cz_synth_t *s) {
    cz_vibrato_words(&s->tone, &s->vib_delay_ticks, &s->vib_rate_word, &s->vib_depth_word);
}

static void lfo_restart(cz_synth_t *s) {
    s->lfo_phase = 0;
    s->lfo_delay = s->vib_delay_ticks;
    s->lfo_out = 0;
}

int cz_synth_init(cz_synth_t *s) {
    memset(s, 0, sizeof(*s));
    upd933_init_tables(&s->tables);
    for (int i = 0; i < CZ_CHIPS; i++) upd933_reset(&s->chip[i], &s->tables, 0x2545f491u + (uint32_t)i);
    s->master_tune = CZ_TUNE_440;
    s->bend_range = 2;
    s->porta_time = 30;
    s->gain = s->gain_target = 1.0f;
    s->press_amt = 60;
    s->wheel_vib = 1;
    cz_chorus_init(&s->chorus);
    s->last_word = -1;
    map_slots(s);
    update_vibrato_words(s);
    return resampler_init(&s->rs);
}

void cz_synth_free(cz_synth_t *s) {
    resampler_free(&s->rs);
}

static void fade_all(cz_synth_t *s) {
    for (int vi = 0; vi < CZ_VOICES; vi++)
        if (s->voice[vi].active || s->voice[vi].fading) fade_voice(s, vi, RATE_TONE_CHANGE);
}

void cz_synth_select_tone(cz_synth_t *s, const cz_tone_t *tone) {
    fade_all(s);
    s->tone = *tone;
    map_slots(s);
    update_vibrato_words(s);
}

void cz_synth_edit_tone(cz_synth_t *s, const cz_tone_t *tone) {
    if (tone->line_select != s->tone.line_select) {
        cz_synth_select_tone(s, tone);
        return;
    }
    s->tone = *tone;
    update_vibrato_words(s);
    for (int vi = 0; vi < CZ_VOICES; vi++) {
        cz_voice_t *v = &s->voice[vi];
        if (!v->active || v->fading) continue;
        const cz_slot_t *sl = &s->slot[v->slot];
        set_wave(s, vi, tone->line[v->line].wave);
        set_mod(s, vi, dual_line(tone) && v->primed ? tone->mod_bits : 0);
        v->base_word = note_word(s, sl->note) + (v->primed ? tone->detune : 0);
        write_pitch(s, vi);
    }
}

/* CZ-101 allocation: round-robin from the slot after the last one used,
 * taking the first whose key is up (a voice in its release tail is reused,
 * after a short fade). If every key is held, the next slot is stolen. */
static int pick_slot(cz_synth_t *s) {
    for (int i = 1; i <= s->nslots; i++) {
        const int si = (s->last_slot + i) % s->nslots;
        if (!s->slot[si].key_held) return si;
    }
    return (s->last_slot + 1) % s->nslots;
}

void cz_synth_set_solo(cz_synth_t *s, int solo) {
    if (!!solo == s->solo) return;
    fade_all(s);
    s->solo = !!solo;
    map_slots(s);
}

/* Start a note in a slot, after a short fade if its voices still sound. */
static void play_in_slot(cz_synth_t *s, int si, int note, int velocity, int glide) {
    cz_slot_t *sl = &s->slot[si];
    sl->sustained = 0;
    int sounding = 0;
    for (int i = 0; i < sl->nvoices; i++) sounding |= s->voice[sl->voice[i]].active | s->voice[sl->voice[i]].fading;
    if (!sounding) {
        start_note(s, si, note, velocity, glide);
        return;
    }
    /* Fade the voices first; the note starts when every fade has arrived. */
    sl->state = SLOT_STEALING;
    sl->note = note;
    sl->pending_note = note;
    sl->pending_velocity = velocity;
    sl->pending_glide = glide;
    sl->pending_rate16 = s->porta_rate16;
    sl->pending_from = s->last_word;
    sl->fades = sl->nvoices;
    for (int i = 0; i < sl->nvoices; i++) fade_voice(s, sl->voice[i], RATE_STEAL);
}

/* Key up: release, unless the sustain pedal holds the note. */
static void key_up(cz_synth_t *s, int si) {
    cz_slot_t *sl = &s->slot[si];
    sl->key_held = 0;
    if (s->pedal) {
        sl->sustained = 1;
        return;
    }
    if (sl->state == SLOT_PLAYING) release_slot(s, si);
    slot_check_idle(s, si);
    /* a stealing slot releases once its note has started */
}

/* Solo: move the sounding note to a new pitch without restarting it. */
static void legato_to(cz_synth_t *s, int note) {
    cz_slot_t *sl = &s->slot[0];
    const int word = note_word(s, note);
    sl->note = note;
    for (int i = 0; i < sl->nvoices; i++) {
        cz_voice_t *v = &s->voice[sl->voice[i]];
        set_glide(s, v, word, 1);
        v->base_word = word + (v->primed ? s->tone.detune : 0);
        write_pitch(s, sl->voice[i]);
    }
    s->last_word = word;
}

/* Any voice of the slot still sounding its own note (not fading out)? */
static int slot_sounding(const cz_synth_t *s, const cz_slot_t *sl) {
    if (sl->state != SLOT_PLAYING) return 0;
    for (int i = 0; i < sl->nvoices; i++)
        if (s->voice[sl->voice[i]].active && !s->voice[sl->voice[i]].fading) return 1;
    return 0;
}

/* A slot fading out before its next note: make that note another one. */
static void retarget_steal(cz_slot_t *sl, int note, int velocity) {
    sl->note = note;
    sl->pending_note = note;
    sl->pending_velocity = velocity;
}

static void solo_note_on(cz_synth_t *s, int note, int velocity) {
    /* newest last; a repeated note moves to the top */
    int n = 0;
    for (int i = 0; i < s->stack_n; i++) {
        if (s->stack[i] == note) continue;
        s->stack[n] = s->stack[i];
        s->stack_vel[n++] = s->stack_vel[i];
    }
    if (n == CZ_SOLO_STACK) {
        memmove(s->stack, s->stack + 1, sizeof(int) * (CZ_SOLO_STACK - 1));
        memmove(s->stack_vel, s->stack_vel + 1, sizeof(int) * (CZ_SOLO_STACK - 1));
        n--;
    }
    s->stack[n] = note;
    s->stack_vel[n++] = velocity;
    s->stack_n = n;
    s->held_keys = n;
    if (n == 1) lfo_restart(s);

    cz_slot_t *sl = &s->slot[0];
    const int legato = n > 1 && slot_sounding(s, sl);
    sl->key_held = 1;
    if (legato) {
        legato_to(s, note);
    } else if (n > 1 && sl->state == SLOT_STEALING) {
        retarget_steal(sl, note, velocity); /* still fading: start the newest note */
    } else {
        sl->pressure = 0;
        play_in_slot(s, 0, note, velocity, 0);
    }
}

static void solo_note_off(cz_synth_t *s, int note) {
    int n = 0, found = 0;
    for (int i = 0; i < s->stack_n; i++) {
        if (s->stack[i] == note) {
            found = 1;
            continue;
        }
        s->stack[n] = s->stack[i];
        s->stack_vel[n++] = s->stack_vel[i];
    }
    if (!found) return;
    s->stack_n = n;
    s->held_keys = n;

    cz_slot_t *sl = &s->slot[0];
    if (sl->note != note) return; /* not the sounding note */
    if (n == 0) {
        key_up(s, 0);
    } else if (sl->state == SLOT_STEALING) {
        retarget_steal(sl, s->stack[n - 1], s->stack_vel[n - 1]); /* start the held note instead */
    } else if (slot_sounding(s, sl)) {
        legato_to(s, s->stack[n - 1]); /* back to the newest held note */
    } else {
        play_in_slot(s, 0, s->stack[n - 1], s->stack_vel[n - 1], 0); /* it had ended: strike the held note */
    }
}

void cz_synth_note_on(cz_synth_t *s, int note, int velocity) {
    if (s->solo) {
        solo_note_on(s, note, velocity);
        return;
    }
    const int si = pick_slot(s);
    cz_slot_t *sl = &s->slot[si];
    s->last_slot = si;
    if (!sl->key_held) s->held_keys++;
    sl->key_held = 1;
    sl->pressure = 0; /* a new press; aftertouch follows */
    if (s->held_keys == 1) lfo_restart(s);
    play_in_slot(s, si, note, velocity, 1);
}

void cz_synth_note_off(cz_synth_t *s, int note) {
    if (s->solo) {
        solo_note_off(s, note);
        return;
    }
    for (int si = 0; si < s->nslots; si++) {
        cz_slot_t *sl = &s->slot[si];
        if (!sl->key_held || sl->note != note) continue;
        s->held_keys--;
        key_up(s, si);
    }
}

void cz_synth_sustain(cz_synth_t *s, int down) {
    s->pedal = !!down;
    if (s->pedal) return;
    for (int si = 0; si < s->nslots; si++) {
        cz_slot_t *sl = &s->slot[si];
        if (!sl->sustained) continue;
        sl->sustained = 0;
        if (!sl->key_held) key_up(s, si);
    }
}

void cz_synth_all_notes_off(cz_synth_t *s) {
    for (int si = 0; si < s->nslots; si++) {
        cz_slot_t *sl = &s->slot[si];
        if ((sl->key_held || sl->sustained) && sl->state == SLOT_PLAYING) release_slot(s, si);
        sl->key_held = 0;
        sl->sustained = 0;
    }
    s->held_keys = 0;
    s->stack_n = 0;
    s->pedal = 0;
}

void cz_synth_all_sound_off(cz_synth_t *s) {
    for (int i = 0; i < CZ_CHIPS; i++) upd933_reset(&s->chip[i], &s->tables, s->chip[i].noise);
    memset(s->wave_bits, 0, sizeof(s->wave_bits));
    memset(s->mod_bits, 0, sizeof(s->mod_bits));
    for (int vi = 0; vi < CZ_VOICES; vi++) {
        s->voice[vi].active = 0;
        s->voice[vi].fading = 0;
    }
    map_slots(s);
}

void cz_synth_pitch_bend(cz_synth_t *s, int value) {
    s->bend = clampi(value, -8192, 8191);
}

/* --- chip events and the firmware tick --- */

/* Is voice vi one of its slot's voices? (After a mode change, voices left
 * fading out belong to no slot.) */
static int voice_in_slot(const cz_synth_t *s, int vi) {
    const cz_slot_t *sl = &s->slot[s->voice[vi].slot];
    for (int i = 0; i < sl->nvoices; i++)
        if (sl->voice[i] == vi) return 1;
    return 0;
}

static void on_dca_arrival(cz_synth_t *s, int vi) {
    cz_voice_t *v = &s->voice[vi];
    if (v->fading) {
        finish_voice(s, vi);
        if (!voice_in_slot(s, vi)) return;
        cz_slot_t *sl = &s->slot[v->slot];
        if (sl->state == SLOT_STEALING && sl->fades && --sl->fades == 0) {
            const int si = v->slot;
            const int rate16 = s->porta_rate16; /* the waiting note glides at the rate it was played with */
            s->porta_rate16 = sl->pending_rate16;
            if (sl->pending_rate16 > 0) s->last_word = sl->pending_from; /* (the CZ's own glide: from the last note started, as it was) */
            start_note(s, si, sl->pending_note, sl->pending_velocity, sl->pending_glide);
            s->porta_rate16 = rate16;
            if (!sl->key_held && !s->pedal) release_slot(s, si);
            else if (!sl->key_held) sl->sustained = 1;
        } else {
            slot_check_idle(s, v->slot);
        }
        return;
    }
    if (v->active) env_arrived(s, vi, CZ_DCA);
}

static void service_chip(cz_synth_t *s, int chip) {
    uint8_t d;
    while ((d = upd933_irq_data(&s->chip[chip])) != 0) {
        const int vi = chip * 8 + UPD933_IRQ_VOICE(d);
        if (UPD933_IRQ_IS_DCA(d)) {
            on_dca_arrival(s, vi);
        } else if (s->voice[vi].active && !s->voice[vi].fading) {
            env_arrived(s, vi, UPD933_IRQ_IS_DCW(d) ? CZ_DCW : CZ_DCO);
        }
    }
}

static void lfo_step(cz_synth_t *s) {
    if (s->lfo_delay > 0) {
        s->lfo_delay--;
        s->lfo_out = 0;
        return;
    }
    uint32_t p = (uint32_t)s->lfo_phase + s->vib_rate_word;
    if (s->lfo_phase < 0x8000 && p >= 0x8000 && p < 0x10000)
        p = 0x8000; /* lands exactly on the midpoint */
    else if (p >= 0x10000)
        p = 0; /* and exactly on the wrap */
    s->lfo_phase = (uint16_t)p;

    const int half = p >= 0x8000;
    const int ramp = (int)((2 * p) >> 8) & 0xff; /* 0..255 within each half */
    int mag, neg;
    switch (s->tone.vib_wave) {
    case CZ_VIB_SAW_UP:
        mag = half ? 255 - ramp : ramp;
        neg = half;
        break;
    case CZ_VIB_SAW_DOWN:
        mag = half ? ramp : 255 - ramp;
        neg = half;
        break;
    case CZ_VIB_SQUARE: /* unipolar: bends upward only */
        mag = half ? 255 : 0;
        neg = 0;
        break;
    default: /* triangle */
        mag = ramp < 128 ? 2 * ramp : 2 * (255 - ramp);
        neg = half;
        break;
    }
    const int out = ((mag * s->vib_depth_word) >> 8) << 2;
    s->lfo_out = neg ? -out : out;
}

void cz_synth_pressure(cz_synth_t *s, int note, int value) {
    value = clampi(value, 0, 127);
    for (int si = 0; si < s->nslots; si++)
        if (note < 0 || s->slot[si].note == note) s->slot[si].pressure = value;
}

void cz_synth_mod_wheel(cz_synth_t *s, int value) {
    s->wheel = clampi(value, 0, 127);
}

static void firmware_tick(cz_synth_t *s) {
    /* the expression LFO: a 5.5 Hz triangle, stepped on the tick like the CZ's */
    s->elfo_phase = (uint16_t)(s->elfo_phase + 2953); /* 5.5 Hz / 122.08 Hz x 65536 */
    const int ph = s->elfo_phase >> 6;                 /* 0..1023 */
    s->elfo_out = ph < 512 ? ph - 256 : 768 - ph;
    /* the LFO restarts when no sounding voice used it during the last tick */
    if (!s->lfo_used) lfo_restart(s);
    s->lfo_used = 0;
    if (s->tone.vib_depth) lfo_step(s);

    for (int vi = 0; vi < CZ_VOICES; vi++) {
        cz_voice_t *v = &s->voice[vi];
        if (!v->active && !v->fading) continue;
        if (s->tone.vib_depth) s->lfo_used = 1;
        if (v->glide_acc < 2048) {
            v->glide_acc += v->glide_rate;
            v->glide_frac += v->glide_rate16;
            v->glide_acc += v->glide_frac >> 16;
            v->glide_frac &= 0xffff;
        }
        write_pitch(s, vi);
        if (!v->fading) apply_expression(s, vi, 0);
    }
}

void cz_synth_render_stereo(cz_synth_t *s, float *left, float *right, int frames) {
    cz_synth_render(s, left, frames);
    for (int f = 0; f < frames; f++) {
        /* after a delay line's worth of silence the chorus has nothing left
         * to say: skip it */
        s->chorus_quiet = left[f] == 0.0f ? s->chorus_quiet + 1 : 0;
        if (s->chorus_quiet > CHORUS_BUF)
            right[f] = left[f];
        else
            cz_chorus_process(&s->chorus, left[f], &left[f], &right[f]);
        s->gain += 0.002f * (s->gain_target - s->gain); /* ~10 ms glide */
        left[f] *= s->gain;
        right[f] *= s->gain;
    }
}

void cz_synth_set_volume(cz_synth_t *s, int db) {
    s->volume_db = clampi(db, -24, 6);
    s->gain_target = powf(10.0f, (float)s->volume_db / 20.0f);
}

/* Nothing sounding, and the output path has settled: the chips would
 * produce zeros, so the whole block can be zeros. */
static int idle(const cz_synth_t *s) {
    if (s->quiet_run < 4 * RS_TAPS || s->dc.y1 > 1e-6f || s->dc.y1 < -1e-6f) return 0;
    for (int vi = 0; vi < CZ_VOICES; vi++)
        if (s->voice[vi].active || s->voice[vi].fading) return 0;
    return 1;
}

void cz_synth_render(cz_synth_t *s, float *out, int frames) {
    if (idle(s)) {
        memset(out, 0, sizeof(float) * (size_t)frames);
        s->dc.x1 = s->dc.y1 = 0.0f;
        return;
    }
    for (int f = 0; f < frames; f++) {
        while (resampler_needs(&s->rs)) {
            s->tick_acc += 1000;
            if (s->tick_acc >= TICK_CHIP_SAMPLES_X1000) {
                s->tick_acc -= TICK_CHIP_SAMPLES_X1000;
                firmware_tick(s);
            }
            /* Each chip has its own DAC and expander (as in the CZ-1); the
             * two are mixed after conversion. */
            int32_t mix = 0;
            for (int i = 0; i < CZ_CHIPS; i++) {
                mix += cz_dac(upd933_update(&s->chip[i]));
                service_chip(s, i);
            }
            s->quiet_run = mix ? 0 : s->quiet_run + 1;
            resampler_push(&s->rs, (float)mix / 32768.0f);
        }
        out[f] = dc_block(&s->dc, resampler_next(&s->rs));
    }
}
