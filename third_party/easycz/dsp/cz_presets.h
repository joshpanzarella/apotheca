/*
 * Built-in presets: original EasyCZ tones in CZ front-panel units, each in a
 * category that becomes a folder on the Move.
 */
#ifndef EASYCZ_CZ_PRESETS_H
#define EASYCZ_CZ_PRESETS_H

#include "dsp/cz_tone.h"

typedef struct {
    const char *name;
    const char *category;
    int chorus; /* the stereo chorus's setting when chosen */
    int level;  /* volume in dB when chosen, for an even loudness */
    cz_panel_tone_t panel;
} cz_preset_t;

extern const cz_preset_t cz_presets[];
extern const int cz_preset_count;

#endif
