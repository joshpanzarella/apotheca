#include "synth_spec.hpp"

#include <cmath>

#include "apotheca/envelope.hpp"
#include <cstdio>

namespace apotheca {

namespace {

std::vector<UserPreset>& userList(TrackKind kind) {
    static std::vector<UserPreset> phazedOnes, jx3pOnes, qmodOnes;
    return kind == TrackKind::Jx3p ? jx3pOnes : kind == TrackKind::Qmod ? qmodOnes : phazedOnes;
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
        static const char* names[kLineHead] = {"WAVE", "SECOND WAVE", "LEVEL KEY FOLLOW", "WAVE KEY FOLLOW"};
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

// ---- JAX-EP (Jx3p in the code) ----------------------------------------------------------
// The plan: docs/plans/jx3p-plan.md, section 4. Batch 6a wrote this much so that a track plays, saves
// and loads; how each value reads at every size, and the folder's line, are batch 6b's.

namespace jx {
using namespace jx3p;

// Grouped like the PG-200's panel, one page a section. The two depths the DCOs share stand on
// DCO-1's page, as they stand at the left of the PG-200's DCO block.
const char* const kPages[6] = {"MAIN", "DCO-1", "DCO-2", "VCF", "VCA AND ENVELOPE", "LFO AND CHORUS"};
struct Item { int id; const char* name; };
const Item kDco1[] = {{Dco1Range, "RANGE"}, {Dco1Wave, "WAVE"}, {Dco1Lfo, "LFO"}, {Dco1Env, "ENV"},
                      {DcoLfoDepth, "LFO DEPTH"}, {DcoEnvDepth, "ENV DEPTH"}, {DcoEnvPolarity, "ENV POLARITY"}};
const Item kDco2[] = {{Dco2Range, "RANGE"}, {Dco2Wave, "WAVE"}, {Dco2CrossMod, "CROSS MOD"}, {Dco2Tune, "TUNE"},
                      {Dco2Fine, "FINE"}, {Dco2Lfo, "LFO"}, {Dco2Env, "ENV"}};
const Item kVcf[] = {{SourceMix, "MIX"}, {Hpf, "HPF"}, {Cutoff, "CUTOFF"}, {Resonance, "RESONANCE"},
                     {VcfLfo, "LFO MOD"}, {VcfEnv, "ENV MOD"}, {VcfEnvPolarity, "ENV POLARITY"}, {KeyFollow, "KEY FOLLOW"}};
const Item kVca[] = {{VcaMode, "MODE"}, {VcaLevel, "LEVEL"}, {Attack, "ATTACK"}, {Decay, "DECAY"}, {Sustain, "SUSTAIN"}, {Release, "RELEASE"}};
const Item kLfo[] = {{LfoWave, "WAVE"}, {LfoDelay, "DELAY"}, {LfoRate, "RATE"}, {Chorus, "CHORUS"}, {Brilliance, "BRILLIANCE"}, {Velocity, "VELOCITY"}};
struct Page { const Item* items; int count; };
const Page kPageItems[6] = {{nullptr, 0}, {kDco1, 7}, {kDco2, 7}, {kVcf, 8}, {kVca, 6}, {kLfo, 6}};
static_assert(7 + 7 + 8 + 6 + 6 == kSettings, "every setting is on one page");

// A PRESET WRITES IDS 0 TO 31 ONLY (jx3p::kPatchSettings). VELOCITY and BRILLIANCE, ids 32 and 33,
// are the track's own, as BRILLIANCE is a knob on the real one's panel and not in its patch: they
// stay as the track has them, whichever preset is picked, built-in or the player's own.
void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Jx3p, TrackKind::Jx3p>::own(index)) {
        for (int id = 0; id < kPatchSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.jx3p.value[id] = v < settingMin(id) ? settingMin(id) : (v > settingMax(id) ? settingMax(id) : v);
        }
    } else {
        const Jx3pPatch preset = Jx3p::presetPatch(index);
        for (int id = 0; id < kPatchSettings; ++id) t.jx3p.value[id] = preset.value[id];
    }
}
int* values(TrackData& t) { return t.jx3p.value; }

void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    const auto one = [&](const char* const* names, int count) { std::snprintf(out, n, "%s", names[value < 0 ? 0 : (value >= count ? count - 1 : value)]); };
    static const char* const ranges[3] = {"16'", "8'", "4'"};
    static const char* const waves[4] = {"SAW", "PULSE", "SQUARE", "NOISE"};
    static const char* const cross[3] = {"OFF", "SYNC", "METAL"};
    static const char* const onOff[2] = {"OFF", "ON"};
    static const char* const polarity[2] = {"NORMAL", "INVERTED"};
    static const char* const modes[2] = {"ENV", "GATE"};
    static const char* const lfoWaves[3] = {"SINE", "SQUARE", "RANDOM"};
    switch (id) {
    case Dco1Range: case Dco2Range: one(ranges, 3); break;
    case Dco1Wave: one(waves, 3); break;
    case Dco2Wave: one(waves, 4); break;
    case Dco2CrossMod: one(cross, 3); break;
    case Dco1Lfo: case Dco1Env: case Dco2Lfo: case Dco2Env: case Chorus: case Velocity: one(onOff, 2); break;
    case DcoEnvPolarity: case VcfEnvPolarity: one(polarity, 2); break;
    case VcaMode: one(modes, 2); break;
    case LfoWave: one(lfoWaves, 3); break;
    default: std::snprintf(out, n, "%d.%d", value / 10, value % 10); break; // a knob, as the PG-200's dial reads: 0.0 to 10.0
    }
}

void macroText(int, float v, char* out, int size) { plusMinus(v, out, size); }
bool envelopePage(int) { return false; }
int itemCount(int page) { return page >= 1 && page < 6 ? kPageItems[page].count : 0; }
void item(int page, int index, int& id, const char*& name) {
    const Item& it = kPageItems[page].items[index];
    id = it.id;
    name = it.name;
}
} // namespace jx

// ---- QMOD -----------------------------------------------------------------------------

namespace qm {
using namespace qmod;
const char* const kPages[4] = {"MAIN", "SOUND", "CHARACTER", "ENVELOPE"};
const int kSoundItems[] = {Sound, Variation, Mutation, Entangle, Family, Octave};
const char* const kSoundNames[] = {"SOUND NUMBER", "VARIATION", "MUTATION", "ENTANGLE", "FAMILY", "OCTAVE"};
const int kCharacterItems[] = {Tension, Evolve, Affinity, Symmetry, Width};
const char* const kCharacterNames[] = {"TENSION", "EVOLVE", "AFFINITY", "SYMMETRY", "WIDTH"};
const int kEnvelopeItems[] = {Attack, Decay, Sustain, Release, Level};
const char* const kEnvelopeNames[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE", "LEVEL"};

void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Qmod, TrackKind::Qmod>::own(index)) {
        for (int id = 0; id < kSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.qmod.value[id] = v < settingMin(id) ? settingMin(id) : (v > settingMax(id) ? settingMax(id) : v);
        }
    } else {
        t.qmod = Qmod::presetPatch(index);
    }
}
int* values(TrackData& t) { return t.qmod.value; }
void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    switch (id) {
    case Sound: case Variation: std::snprintf(out, n, "%d", value); break;
    case Family: std::snprintf(out, n, "%s", value == 0 ? "AUTO" : familyName(value)); break;
    case Octave: std::snprintf(out, n, "%+d", value); break;
    case Attack: case Decay: case Release: {
        const float seconds = id == Attack ? envelope::attackSeconds(static_cast<float>(value)) : envelope::fallSeconds(static_cast<float>(value));
        if (seconds < 1.f) std::snprintf(out, n, "%d MS", static_cast<int>(std::round(seconds * 1000.f)));
        else std::snprintf(out, n, "%.1f S", seconds);
        break;
    }
    case Level: std::snprintf(out, n, "%+d dB", value); break;
    default: std::snprintf(out, n, "%d%%", value); break;
    }
}
void macroText(int macro, float v, char* out, int size) {
    if (macro == MacroOctave) std::snprintf(out, static_cast<std::size_t>(size), "%s", v < 0.25f ? "DOWN" : v > 0.75f ? "UP" : "AS PRESET");
    else if (macro == MacroVariation) std::snprintf(out, static_cast<std::size_t>(size), "%+d", static_cast<int>(std::lround((v - 0.5f) * 32.f)));
    else plusMinus(v, out, size);
}
bool envelopePage(int) { return false; }
int itemCount(int page) { return page == 1 ? 6 : 5; }
void item(int page, int index, int& id, const char*& name) {
    if (page == 1) { id = kSoundItems[index]; name = kSoundNames[index]; }
    else if (page == 2) { id = kCharacterItems[index]; name = kCharacterNames[index]; }
    else { id = kEnvelopeItems[index]; name = kEnvelopeNames[index]; }
}
// ROLL: a new sound number and a new cousin. Everything else (how bright, the envelope) is left as it is set.
void roll(TrackData& t, unsigned seed) {
    seed = seed * 1664525u + 1013904223u;
    t.qmod.value[Sound] = 1 + static_cast<int>((seed >> 8) % 9999u);
    seed = seed * 1664525u + 1013904223u;
    t.qmod.value[Variation] = static_cast<int>((seed >> 8) % 100u);
    t.qmod.value[Family] = 0;
}
} // namespace qm

} // namespace

const SynthSpec& phazedSpec() {
    static const SynthSpec spec = {"PHAZED", Presets<Phazed, TrackKind::Phazed>::count, Presets<Phazed, TrackKind::Phazed>::name, Presets<Phazed, TrackKind::Phazed>::category, pz::loadPreset, pz::values,
                                   phazed::settingMin, phazed::settingMax, pz::settingText,
                                   {"TONE", "MOTION", "ATTACK", "RELEASE", "DETUNE", "VIBRATO", "CHORUS", "OCTAVE"}, pz::macroText,
                                   10, pz::kPages, pz::itemCount, pz::item, pz::envelopePage, nullptr};
    return spec;
}

const SynthSpec& jx3pSpec() {
    static const SynthSpec spec = {"JAX-EP", Presets<Jx3p, TrackKind::Jx3p>::count, Presets<Jx3p, TrackKind::Jx3p>::name, Presets<Jx3p, TrackKind::Jx3p>::category, jx::loadPreset, jx::values,
                                   jx3p::settingMin, jx3p::settingMax, jx::settingText,
                                   {"CUTOFF", "RESO", "ENV", "TUNE", "MIX", "LFO", "ATTACK", "RELEASE"}, jx::macroText,
                                   6, jx::kPages, jx::itemCount, jx::item, jx::envelopePage, nullptr};
    return spec;
}

void setUserPresets(TrackKind kind, std::vector<UserPreset> presets) { userList(kind) = std::move(presets); }
const std::vector<UserPreset>& userPresets(TrackKind kind) { return userList(kind); }
int builtInPresets(TrackKind kind) {
    return kind == TrackKind::Jx3p ? Jx3p::presetCount() : kind == TrackKind::Phazed ? Phazed::presetCount() : kind == TrackKind::Qmod ? Qmod::presetCount() : 0;
}
int settingCount(TrackKind kind) {
    return kind == TrackKind::Jx3p ? static_cast<int>(jx3p::kSettings) : kind == TrackKind::Phazed ? static_cast<int>(phazed::kSettings) : kind == TrackKind::Qmod ? static_cast<int>(qmod::kSettings) : 0;
}
int presetSettingCount(TrackKind kind) {
    return kind == TrackKind::Jx3p ? static_cast<int>(jx3p::kPatchSettings) : settingCount(kind);
}

const SynthSpec& qmodSpec() {
    static const SynthSpec spec = {"QMOD", Presets<Qmod, TrackKind::Qmod>::count, Presets<Qmod, TrackKind::Qmod>::name, Presets<Qmod, TrackKind::Qmod>::category, qm::loadPreset, qm::values,
                                   qmod::settingMin, qmod::settingMax, qm::settingText,
                                   {"ENTANGLE", "TENSION", "EVOLVE", "VARIATION", "ATTACK", "RELEASE", "WIDTH", "OCTAVE"}, qm::macroText,
                                   4, qm::kPages, qm::itemCount, qm::item, qm::envelopePage, qm::roll};
    return spec;
}

const SynthSpec* specFor(TrackKind kind) {
    return kind == TrackKind::Phazed ? &phazedSpec() : kind == TrackKind::Jx3p ? &jx3pSpec() : kind == TrackKind::Qmod ? &qmodSpec() : nullptr;
}

} // namespace apotheca
