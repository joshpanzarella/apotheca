#include "apotheca/phazed.hpp"

#include "apotheca/rate_converter.hpp"

#include <cmath>
#include <string>
#include <vector>

extern "C" {
#include "dsp/cz_presets.h"
#include "dsp/cz_synth.h"
}

namespace apotheca {

namespace {

constexpr float kSynthRate = 44100.f;

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- a patch as the engine's own "panel tone", and back ------------------------

cz_panel_env_t* envOf(cz_panel_line_t& line, int env) {
    return env == phazed::AmpEnv ? &line.dca : env == phazed::WaveEnv ? &line.dcw : &line.dco;
}

void toPanel(const PhazedPatch& p, cz_panel_tone_t& t) {
    using namespace phazed;
    t = cz_panel_tone_t();
    t.line_select = static_cast<uint8_t>(p.value[LineSelect]);
    t.octave = static_cast<int8_t>(p.value[Octave]);
    t.detune_sign = p.value[DetuneSign] ? 1 : -1;
    t.detune_oct = static_cast<uint8_t>(p.value[DetuneOctave]);
    t.detune_note = static_cast<uint8_t>(p.value[DetuneNote]);
    t.detune_fine = static_cast<uint8_t>(p.value[DetuneFine]);
    t.mod = static_cast<uint8_t>(p.value[Mod]);
    t.vib_wave = static_cast<uint8_t>(p.value[VibratoWave]);
    t.vib_delay = static_cast<uint8_t>(p.value[VibratoDelay]);
    t.vib_rate = static_cast<uint8_t>(p.value[VibratoRate]);
    t.vib_depth = static_cast<uint8_t>(p.value[VibratoDepth]);
    for (int line = 0; line < 2; ++line) {
        cz_panel_line_t& l = t.line[line];
        l.wave1 = static_cast<uint8_t>(p.value[lineSetting(line, Wave1)]);
        l.wave2 = static_cast<uint8_t>(p.value[lineSetting(line, Wave2)]);
        l.dca_kf = static_cast<uint8_t>(p.value[lineSetting(line, AmpKeyFollow)]);
        l.dcw_kf = static_cast<uint8_t>(p.value[lineSetting(line, WaveKeyFollow)]);
        for (int env = 0; env < kEnvelopes; ++env) {
            cz_panel_env_t* e = envOf(l, env);
            for (int step = 0; step < kEnvSteps; ++step) {
                e->rate[step] = static_cast<uint8_t>(p.value[envRate(line, env, step)]);
                e->level[step] = static_cast<uint8_t>(p.value[envLevel(line, env, step)]);
            }
            e->sustain = static_cast<uint8_t>(p.value[envSustain(line, env)]);
            e->end = static_cast<uint8_t>(p.value[envEnd(line, env)]);
        }
    }
}

PhazedPatch fromPreset(const cz_preset_t& preset) {
    using namespace phazed;
    PhazedPatch p;
    cz_panel_tone_t t = preset.panel;
    p.value[LineSelect] = t.line_select;
    p.value[Octave] = t.octave;
    p.value[DetuneSign] = t.detune_sign < 0 ? 0 : 1;
    p.value[DetuneOctave] = t.detune_oct;
    p.value[DetuneNote] = t.detune_note;
    p.value[DetuneFine] = t.detune_fine;
    p.value[Mod] = t.mod;
    p.value[VibratoWave] = t.vib_wave;
    p.value[VibratoDelay] = t.vib_delay;
    p.value[VibratoRate] = t.vib_rate;
    p.value[VibratoDepth] = t.vib_depth;
    p.value[ChorusOn] = preset.chorus ? 1 : 0;
    p.value[Level] = preset.level;
    for (int line = 0; line < 2; ++line) {
        cz_panel_line_t& l = t.line[line];
        p.value[lineSetting(line, Wave1)] = l.wave1;
        p.value[lineSetting(line, Wave2)] = l.wave2;
        p.value[lineSetting(line, AmpKeyFollow)] = l.dca_kf;
        p.value[lineSetting(line, WaveKeyFollow)] = l.dcw_kf;
        for (int env = 0; env < kEnvelopes; ++env) {
            const cz_panel_env_t* e = envOf(l, env);
            for (int step = 0; step < kEnvSteps; ++step) {
                p.value[envRate(line, env, step)] = e->rate[step];
                p.value[envLevel(line, env, step)] = e->level[step];
            }
            p.value[envSustain(line, env)] = e->sustain;
            // The presets write only the steps they use; an end of 0 means 1.
            p.value[envEnd(line, env)] = e->end < 1 ? 1 : e->end;
        }
    }
    for (int id = 0; id < kSettings; ++id) p.value[id] = clampInt(p.value[id], settingMin(id), settingMax(id));
    return p;
}

// ---- the macros: bend a copy of the patch ---------------------------------------

int bent(int value, float by) { return clampInt(static_cast<int>(std::lround(value + by)), 0, 99); }

void applyMacros(const float* macro, cz_panel_tone_t& t, int& chorus) {
    using namespace phazed;
    const float tone = macro[Tone] - 0.5f, motion = macro[Motion] - 0.5f, attack = macro[Attack] - 0.5f,
                release = macro[Release] - 0.5f, detune = macro[Detune] - 0.5f, vibrato = macro[Vibrato] - 0.5f;
    for (cz_panel_line_t& line : t.line) {
        // TONE: how far the wave is bent, from half as far to twice as far.
        // MOTION: how fast that bend moves.
        const float scale = std::pow(2.f, 2.f * tone);
        for (int step = 0; step < kEnvSteps; ++step) {
            line.dcw.level[step] = static_cast<uint8_t>(clampInt(static_cast<int>(std::lround(line.dcw.level[step] * scale)), 0, 99));
            line.dcw.rate[step] = static_cast<uint8_t>(bent(line.dcw.rate[step], 60.f * motion));
        }
        // ATTACK: the first stage of the volume envelope. Up is slower.
        line.dca.rate[0] = static_cast<uint8_t>(bent(line.dca.rate[0], -120.f * attack));
        // RELEASE: the stages after the held one (or after the first, in a
        // sound that never holds). Up is longer.
        const int from = line.dca.sustain ? line.dca.sustain : 1;
        const int end = line.dca.end < 1 ? 1 : (line.dca.end > kEnvSteps ? kEnvSteps : line.dca.end);
        for (int step = from; step < end; ++step) {
            line.dca.rate[step] = static_cast<uint8_t>(bent(line.dca.rate[step], -100.f * release));
        }
    }
    // DETUNE: the second line's fine tuning. Only heard when two lines play.
    t.detune_fine = static_cast<uint8_t>(clampInt(static_cast<int>(std::lround(t.detune_fine + 120.f * detune)), 0, 60));
    // VIBRATO: depth. Turning it up on a sound with no vibrato gives it a speed too.
    if (vibrato != 0.f) {
        t.vib_depth = static_cast<uint8_t>(bent(t.vib_depth, 160.f * vibrato));
        if (vibrato > 0.f && t.vib_rate < 20) t.vib_rate = 55;
    }
    // CHORUS: off at the bottom, on at the top, the patch's own in the middle.
    if (macro[Chorus] < 0.4f) chorus = 0;
    if (macro[Chorus] > 0.6f) chorus = 1;
    // OCTAVE: one down at the bottom, one up at the top.
    const int shift = macro[MacroOctave] < 0.25f ? -1 : (macro[MacroOctave] > 0.75f ? 1 : 0);
    t.octave = static_cast<int8_t>(clampInt(t.octave + shift, -1, 1));
}

} // namespace

namespace phazed {

int settingMin(int id) {
    if (id == Octave) return -1;
    if (id == Level) return -24;
    if (id < kGlobals) return 0;
    const int in = (id - kGlobals) % kLineSettings, line = (id - kGlobals) / kLineSettings;
    if (in == Wave1) return line == 0 ? 1 : 0; // line 2 may be left empty: it then plays line 1's settings
    if (in < kLineHead) return 0;
    const int e = (in - kLineHead) % kEnvSettings;
    return e == 2 * kEnvSteps + 1 ? 1 : 0; // the end step is 1..8
}

int settingMax(int id) {
    switch (id) {
    case LineSelect: return 3;
    case Octave: return 1;
    case DetuneSign: return 1;
    case DetuneOctave: return 3;
    case DetuneNote: return 11;
    case DetuneFine: return 60;
    case Mod: return 2;
    case VibratoWave: return 3;
    case VibratoDelay: case VibratoRate: case VibratoDepth: return 99;
    case ChorusOn: return 1;
    case Level: return 6;
    default: break;
    }
    if (id < 0 || id >= kSettings) return 0;
    const int in = (id - kGlobals) % kLineSettings;
    if (in == Wave1 || in == Wave2) return 8;
    if (in < kLineHead) return 9;
    const int e = (in - kLineHead) % kEnvSettings;
    return e < 2 * kEnvSteps ? 99 : 8;
}

} // namespace phazed

struct Phazed::State {
    cz_synth_t synth;

    // Rate conversion, used only when the engine is not at 44100 Hz.
    float engineRate = kSynthRate;
    RateConverter converter;
};

Phazed::Phazed() : state_(new State) {
    for (float& m : macro_) m = 0.5f;
    ok_ = cz_synth_init(&state_->synth) == 0;
    if (ok_) selectPreset(0);
}

Phazed::~Phazed() {
    if (ok_) cz_synth_free(&state_->synth);
}

void Phazed::prepare(float sampleRate) {
    state_->engineRate = sampleRate;
    if (sampleRate != kSynthRate) state_->converter.prepare(kSynthRate, sampleRate);
}

int Phazed::presetCount() { return cz_preset_count; }

// The engine's presets were named for another product. Any name that carries
// that product's initials is shown under this one's.
const char* Phazed::presetName(int index) {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (int i = 0; i < cz_preset_count; ++i) {
            std::string name = cz_presets[i].name;
            for (std::size_t at = name.find("CZ"); at != std::string::npos; at = name.find("CZ", at)) {
                name.replace(at, 2, "Phazed");
            }
            out.push_back(name);
        }
        return out;
    }();
    return index >= 0 && index < cz_preset_count ? names[static_cast<std::size_t>(index)].c_str() : "";
}

const char* Phazed::presetCategory(int index) {
    return index >= 0 && index < cz_preset_count ? cz_presets[index].category : "";
}

PhazedPatch Phazed::presetPatch(int index) {
    return fromPreset(cz_presets[clampInt(index, 0, cz_preset_count - 1)]);
}

// Send the patch, bent by the macros, to the synth.
void Phazed::apply(bool newSound) {
    if (!ok_) return;
    cz_panel_tone_t panel;
    toPanel(patch_, panel);
    int chorus = patch_.value[phazed::ChorusOn];
    applyMacros(macro_, panel, chorus);
    cz_tone_t tone;
    cz_tone_from_panel(&panel, &tone);
    if (newSound) cz_synth_select_tone(&state_->synth, &tone);
    else cz_synth_edit_tone(&state_->synth, &tone);
    cz_chorus_enable(&state_->synth.chorus, chorus);
    cz_synth_set_volume(&state_->synth, patch_.value[phazed::Level]);
}

void Phazed::selectPreset(int index) {
    if (!ok_ || index < 0 || index >= cz_preset_count) return;
    patch_ = fromPreset(cz_presets[index]);
    for (float& m : macro_) m = 0.5f;
    preset_ = index;
    apply(true);
}

void Phazed::setSetting(int id, int value) {
    if (id < 0 || id >= phazed::kSettings) return;
    value = clampInt(value, phazed::settingMin(id), phazed::settingMax(id));
    if (patch_.value[id] == value) return;
    patch_.value[id] = value;
    apply(false);
}

void Phazed::setMacro(int macro, float value) {
    if (macro < 0 || macro >= phazed::kMacros || !std::isfinite(value)) return;
    value = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    if (macro_[macro] == value) return;
    macro_[macro] = value;
    apply(false);
}

void Phazed::noteOn(int note, float velocity) {
    if (!ok_ || note < 0 || note > 127 || !(velocity > 0.f)) return; // also drops NaN
    int v = static_cast<int>(std::lround(velocity * 127.f));
    v = v < 1 ? 1 : (v > 127 ? 127 : v);
    cz_synth_note_on(&state_->synth, note, v);
}

void Phazed::noteOff(int note) {
    if (ok_ && note >= 0 && note <= 127) cz_synth_note_off(&state_->synth, note);
}

void Phazed::allNotesOff() {
    if (ok_) cz_synth_all_notes_off(&state_->synth);
}

void Phazed::render(float* left, float* right, int frames) {
    if (!ok_) {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        return;
    }
    State& s = *state_;
    if (s.engineRate == kSynthRate) {
        cz_synth_render_stereo(&s.synth, left, right, frames);
        return;
    }
    s.converter.render([&s](float& l, float& r) { cz_synth_render_stereo(&s.synth, &l, &r, 1); }, left, right, frames);
}

} // namespace apotheca
