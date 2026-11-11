#include "synth_spec.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

namespace {

std::vector<UserPreset>& userList(TrackKind kind) {
    static std::vector<UserPreset> phazedOnes, twinOnes;
    return kind == TrackKind::Twin ? twinOnes : phazedOnes;
}

// The preset functions every spec uses: the built-in presets first, then the
// player's own.
template <typename Synth, TrackKind kind>
struct Presets {
    static int count() { return Synth::presetCount() + static_cast<int>(userList(kind).size()); }
    static const char* name(int i) { return i < Synth::presetCount() ? Synth::presetName(i) : userList(kind)[i - Synth::presetCount()].name.c_str(); }
    static const char* category(int i) { return i < Synth::presetCount() ? Synth::presetCategory(i) : "USER"; }
    // The player's values for preset `i`, or nullptr if it is a built-in one.
    static const std::vector<int>* own(int i) {
        const int at = i - Synth::presetCount();
        return at >= 0 && at < static_cast<int>(userList(kind).size()) ? &userList(kind)[at].values : nullptr;
    }
};

void plusMinus(float v, char* out, int size) { // a macro as -100..+100
    std::snprintf(out, static_cast<std::size_t>(size), "%+d", static_cast<int>(std::round((v - 0.5f) * 200.f)));
}

// ---- Phazed ------------------------------------------------------------------------

namespace pz {
using namespace phazed;

const char* const kPages[10] = {"MAIN", "SOUND", "LINE 1", "LINE 2", "LINE 1 VOLUME", "LINE 1 WAVE",
                                "LINE 1 PITCH", "LINE 2 VOLUME", "LINE 2 WAVE", "LINE 2 PITCH"};
const char* const kWaveNames[9] = {"NONE", "SAW", "SQUARE", "PULSE", "DOUBLE SINE", "SAW PULSE", "RESO 1", "RESO 2", "RESO 3"};
const char* const kStepNames[kEnvSteps] = {"1", "2", "3", "4", "5", "6", "7", "8"};

void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Phazed, TrackKind::Phazed>::own(index)) {
        for (int id = 0; id < kSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.patch.value[id] = v < settingMin(id) ? settingMin(id) : (v > settingMax(id) ? settingMax(id) : v);
        }
    } else {
        t.patch = Phazed::presetPatch(index);
    }
}
int* values(TrackData& t) { return t.patch.value; }

void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (id == LineSelect) {
        static const char* lines[4] = {"LINE 1", "LINE 2", "1 + 1'", "1 + 2'"};
        std::snprintf(out, n, "%s", lines[value & 3]);
    } else if (id == Octave) {
        std::snprintf(out, n, "%+d", value);
    } else if (id == DetuneSign) {
        std::snprintf(out, n, "%s", value ? "UP" : "DOWN");
    } else if (id == Mod) {
        static const char* mods[3] = {"OFF", "RING", "NOISE"};
        std::snprintf(out, n, "%s", mods[value % 3]);
    } else if (id == VibratoWave) {
        static const char* waves[4] = {"TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE"};
        std::snprintf(out, n, "%s", waves[value & 3]);
    } else if (id == ChorusOn) {
        std::snprintf(out, n, "%s", value ? "ON" : "OFF");
    } else if (id == Level) {
        std::snprintf(out, n, "%+d dB", value);
    } else if (id >= kGlobals && (id - kGlobals) % kLineSettings <= Wave2) {
        const bool second = (id - kGlobals) % kLineSettings == Wave2;
        const bool lineTwo = (id - kGlobals) / kLineSettings == 1;
        if (value == 0 && !second && lineTwo) std::snprintf(out, n, "AS LINE 1");
        else std::snprintf(out, n, "%s", kWaveNames[value < 0 || value > 8 ? 0 : value]);
    } else if (id >= kGlobals && (id - kGlobals) % kLineSettings >= kLineHead &&
               ((id - kGlobals) % kLineSettings - kLineHead) % kEnvSettings >= 2 * kEnvSteps) {
        if (value == 0) std::snprintf(out, n, "NONE");
        else std::snprintf(out, n, "STEP %d", value);
    } else {
        std::snprintf(out, n, "%d", value);
    }
}

void macroText(int macro, float v, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (macro == Chorus) std::snprintf(out, n, "%s", v < 0.4f ? "OFF" : v > 0.6f ? "ON" : "AS PRESET");
    else if (macro == MacroOctave) std::snprintf(out, n, "%s", v < 0.25f ? "DOWN" : v > 0.75f ? "UP" : "AS PRESET");
    else plusMinus(v, out, size);
}

bool envelopePage(int page) { return page >= 4; }

int itemCount(int page) { return page == 1 ? kGlobals : page <= 3 ? kLineHead : kEnvSettings; }

void item(int page, int index, int& id, const char*& name) {
    if (page == 1) {
        static const char* names[kGlobals] = {"LINES", "OCTAVE", "DETUNE", "DETUNE OCTAVE", "DETUNE NOTE", "DETUNE FINE", "MODULATION",
                                              "VIBRATO WAVE", "VIBRATO DELAY", "VIBRATO RATE", "VIBRATO DEPTH", "CHORUS", "LEVEL"};
        id = index;
        name = names[index];
    } else if (page <= 3) {
        static const char* names[kLineHead] = {"WAVE", "SECOND WAVE", "VOLUME KEY FOLLOW", "WAVE KEY FOLLOW"};
        id = lineSetting(page - 2, index);
        name = names[index];
    } else {
        const int line = (page - 4) / 3, env = (page - 4) % 3;
        if (index < kEnvSteps) { id = envRate(line, env, index); name = kStepNames[index]; }
        else if (index < 2 * kEnvSteps) { id = envLevel(line, env, index - kEnvSteps); name = kStepNames[index - kEnvSteps]; }
        else if (index == 2 * kEnvSteps) { id = envSustain(line, env); name = "HOLD AT"; }
        else { id = envEnd(line, env); name = "LAST STEP"; }
    }
}
} // namespace pz

// ---- Twin Analog ---------------------------------------------------------------------

namespace tw {
using namespace twin;

const char* const kPages[4] = {"MAIN", "OSCILLATORS", "FORMANT AND CHARACTER", "ENVELOPE"};
const int kOscillators[] = {VOctave, VFine, VShape, Crossfade, ZSemitone, ZFine, ZShape, Waveform, CrossSync, ReverseSync, CrossfadeMode};
const char* const kOscillatorNames[] = {"V OCTAVE", "V FINE", "V SHAPE", "CROSSFADE", "Z SEMITONE", "Z FINE", "Z SHAPE", "WAVE",
                                        "CROSS SYNC", "REVERSE SYNC", "CROSSFADE MODE"};
const int kCharacter[] = {FormantDepth, FormantRatio, Asymmetry, Width, Vintage, Noise, Oversample};
const char* const kCharacterNames[] = {"FORMANT DEPTH", "FORMANT RATIO", "ASYMMETRY", "WIDTH", "VINTAGE", "NOISE", "OVERSAMPLING"};
const int kEnvelope[] = {Attack, Decay, Sustain, Release, Level};
const char* const kEnvelopeNames[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE", "LEVEL"};

void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Twin, TrackKind::Twin>::own(index)) {
        for (int id = 0; id < twin::kSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.twin.value[id] = v < twin::settingMin(id) ? twin::settingMin(id) : (v > twin::settingMax(id) ? twin::settingMax(id) : v);
        }
    } else {
        t.twin = Twin::presetPatch(index);
    }
}
int* values(TrackData& t) { return t.twin.value; }

void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    switch (id) {
    case VOctave: std::snprintf(out, n, "%+d", value); break;
    case ZSemitone: std::snprintf(out, n, "%+d ST", value); break;
    case VFine: case ZFine: std::snprintf(out, n, "%+d CENTS", value); break;
    case Crossfade:
        if (value == 0) std::snprintf(out, n, "ALL V");
        else if (value == 100) std::snprintf(out, n, "ALL Z");
        else std::snprintf(out, n, "V %d : Z %d", 100 - value, value);
        break;
    case CrossSync: std::snprintf(out, n, "%s", value ? "ON" : "OFF"); break;
    case ReverseSync: std::snprintf(out, n, "%s", value == 0 ? "OFF" : value == 1 ? "ON" : "MUTUAL"); break;
    case Waveform: std::snprintf(out, n, "%s", value ? "PULSE" : "SIGMOID SAW"); break;
    case CrossfadeMode: std::snprintf(out, n, "%s", value ? "STEREO SWAP" : "EQUAL POWER"); break;
    case FormantRatio: {
        static const char* ratios[8] = {"x0.5", "x1", "x1.5", "x2", "x3", "x4", "x5", "x7"};
        std::snprintf(out, n, "%s", ratios[value & 7]);
        break;
    }
    case Width: std::snprintf(out, n, "%d%%", value * 2); break; // 50 is natural: 100%
    case Oversample: std::snprintf(out, n, "x%d", 1 << value); break;
    case Attack: case Decay: case Release: {
        const float seconds = 0.001f * std::pow(8000.f, value / 100.f); // as in engine/src/twin.cpp
        if (seconds < 1.f) std::snprintf(out, n, "%d MS", static_cast<int>(std::round(seconds * 1000.f)));
        else std::snprintf(out, n, "%.1f S", seconds);
        break;
    }
    case Level: std::snprintf(out, n, "%+d dB", value); break;
    default: std::snprintf(out, n, "%d%%", value); break;
    }
}

void macroText(int, float v, char* out, int size) { plusMinus(v, out, size); }
bool envelopePage(int) { return false; }
int itemCount(int page) { return page == 1 ? 11 : page == 2 ? 7 : 5; }
void item(int page, int index, int& id, const char*& name) {
    if (page == 1) { id = kOscillators[index]; name = kOscillatorNames[index]; }
    else if (page == 2) { id = kCharacter[index]; name = kCharacterNames[index]; }
    else { id = kEnvelope[index]; name = kEnvelopeNames[index]; }
}
} // namespace tw

} // namespace

const SynthSpec& phazedSpec() {
    static const SynthSpec spec = {"PHAZED", Presets<Phazed, TrackKind::Phazed>::count, Presets<Phazed, TrackKind::Phazed>::name, Presets<Phazed, TrackKind::Phazed>::category, pz::loadPreset, pz::values,
                                   phazed::settingMin, phazed::settingMax, pz::settingText,
                                   {"TONE", "MOTION", "ATTACK", "RELEASE", "DETUNE", "VIBRATO", "CHORUS", "OCTAVE"}, pz::macroText,
                                   10, pz::kPages, pz::itemCount, pz::item, pz::envelopePage};
    return spec;
}

const SynthSpec& twinSpec() {
    static const SynthSpec spec = {"TWIN", Presets<Twin, TrackKind::Twin>::count, Presets<Twin, TrackKind::Twin>::name, Presets<Twin, TrackKind::Twin>::category, tw::loadPreset, tw::values,
                                   twin::settingMin, twin::settingMax, tw::settingText,
                                   {"SHAPE", "BLEND", "FORMANT", "WIDTH", "DETUNE", "VINTAGE", "ATTACK", "RELEASE"}, tw::macroText,
                                   4, tw::kPages, tw::itemCount, tw::item, tw::envelopePage};
    return spec;
}

void setUserPresets(TrackKind kind, std::vector<UserPreset> presets) { userList(kind) = std::move(presets); }
const std::vector<UserPreset>& userPresets(TrackKind kind) { return userList(kind); }
int builtInPresets(TrackKind kind) { return kind == TrackKind::Twin ? Twin::presetCount() : kind == TrackKind::Phazed ? Phazed::presetCount() : 0; }
int settingCount(TrackKind kind) {
    return kind == TrackKind::Twin ? static_cast<int>(twin::kSettings) : kind == TrackKind::Phazed ? static_cast<int>(phazed::kSettings) : 0;
}

const SynthSpec* specFor(TrackKind kind) {
    return kind == TrackKind::Phazed ? &phazedSpec() : kind == TrackKind::Twin ? &twinSpec() : nullptr;
}

} // namespace apotheca
