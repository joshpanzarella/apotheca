#include "apotheca/phazed.hpp"

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
} // namespace

struct Phazed::State {
    cz_synth_t synth;

    // Rate conversion, used only when the engine is not at 44100 Hz.
    float engineRate = kSynthRate;
    double step = 1.0;     // synth samples per engine sample
    double position = 1.0; // where the next output falls between `before` and `after`
    float beforeL = 0.f, beforeR = 0.f, afterL = 0.f, afterR = 0.f;
};

Phazed::Phazed() : state_(new State) {
    ok_ = cz_synth_init(&state_->synth) == 0;
    if (ok_) selectPreset(0);
}

Phazed::~Phazed() {
    if (ok_) cz_synth_free(&state_->synth);
}

void Phazed::prepare(float sampleRate) {
    state_->engineRate = sampleRate;
    state_->step = static_cast<double>(kSynthRate) / sampleRate;
    state_->position = 1.0;
    state_->beforeL = state_->beforeR = state_->afterL = state_->afterR = 0.f;
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

void Phazed::selectPreset(int index) {
    if (!ok_ || index < 0 || index >= cz_preset_count) return;
    const cz_preset_t& preset = cz_presets[index];
    cz_tone_t tone;
    cz_tone_from_panel(&preset.panel, &tone);
    cz_synth_select_tone(&state_->synth, &tone);
    cz_chorus_enable(&state_->synth.chorus, preset.chorus);
    cz_synth_set_volume(&state_->synth, preset.level);
    preset_ = index;
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
    // Draw a straight line between neighbouring synth samples and read off it.
    for (int i = 0; i < frames; ++i) {
        while (s.position >= 1.0) {
            s.beforeL = s.afterL;
            s.beforeR = s.afterR;
            cz_synth_render_stereo(&s.synth, &s.afterL, &s.afterR, 1);
            s.position -= 1.0;
        }
        const float t = static_cast<float>(s.position);
        left[i] = s.beforeL + t * (s.afterL - s.beforeL);
        right[i] = s.beforeR + t * (s.afterR - s.beforeR);
        s.position += s.step;
    }
}

} // namespace apotheca
