#include "synth_spec.hpp"

#include <cmath>
#include <cstdint>

#include <cstdio>
#include <cstring>

namespace apotheca {

namespace {

std::vector<UserPreset>& userList(TrackKind kind) {
    static std::vector<UserPreset> phazedOnes, jx3pOnes, fm6Ones, physicOnes;
    return kind == TrackKind::Jx3p ? jx3pOnes : kind == TrackKind::Fm6 ? fm6Ones : kind == TrackKind::Physic ? physicOnes : phazedOnes;
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
// The plan: docs/plans/jx3p-plan.md, section 4.

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
    default: {
        // A knob, as the programmer's dial reads: 0.0 to 10.0. Held to the dial: a value from outside
        // it (a file written by hand) read "0.-5" and "10.5".
        const int dial = value < 0 ? 0 : (value > 100 ? 100 : value);
        std::snprintf(out, n, "%d.%d", dial / 10, dial % 10);
        break;
    }
    }
}

// A macro reads -100 to +100, +0 "as the patch": under the middle it slides its setting from the
// patch's value down to 0, over it up to 10 (the engine's apply), so the number is how far, not a dial.
void macroText(int, float v, char* out, int size) { plusMinus(v, out, size); }
bool envelopePage(int) { return false; }
// The player's own presets (the USER folder) say nothing: they were set by ear, on this synth.
const char* folderNote(int index) {
    if (Presets<Jx3p, TrackKind::Jx3p>::own(index)) return nullptr;
    return jaxepFolderNote(Jx3p::presetCategory(index));
}
int itemCount(int page) { return page >= 1 && page < 6 ? kPageItems[page].count : 0; }
void item(int page, int index, int& id, const char*& name) {
    const Item& it = kPageItems[page].items[index];
    id = it.id;
    name = it.name;
}
} // namespace jx

// ---- FM-6 ------------------------------------------------------------------------------
// The plan: docs/plans/fm6-plan.md. Every one of the original's 145 voice settings, in its own
// numbering, and which operators are on.

namespace fm {
using namespace fm6;

// One page for the voice as a whole and its LFO, one a operator, and the pitch envelope last (the
// least used). An operator's page is all of its 21 settings and its ON, so one operator is worked
// on without turning a page: its tuning and level on the first two rows, its envelope on the next
// two (the four rates over the four levels they reach), how it follows the keys on the last two.
const char* const kPages[9] = {"MAIN", "VOICE AND LFO", "OPERATOR 1", "OPERATOR 2", "OPERATOR 3",
                               "OPERATOR 4", "OPERATOR 5", "OPERATOR 6", "PITCH ENVELOPE"};
constexpr int kFirstOpPage = 2, kPitchPage = 8;
struct Item { int id; const char* name; };
const Item kVoice[] = {{Algorithm, "ALGORITHM"}, {Feedback, "FEEDBACK"}, {Transpose, "TRANSPOSE"}, {KeySync, "KEY SYNC"},
                       {LfoWave, "LFO WAVE"}, {LfoSpeed, "LFO SPEED"}, {LfoDelay, "LFO DELAY"}, {LfoSync, "LFO SYNC"},
                       {LfoPmd, "PITCH DEPTH"}, {LfoAmd, "LEVEL DEPTH"}, {Pms, "PITCH SENS"}};
constexpr int kVoiceItems = static_cast<int>(sizeof kVoice / sizeof kVoice[0]);
const Item kPitch[] = {{PitchR1, "RATE 1"}, {PitchR2, "RATE 2"}, {PitchR3, "RATE 3"}, {PitchR4, "RATE 4"},
                       {PitchL1, "LEVEL 1"}, {PitchL2, "LEVEL 2"}, {PitchL3, "LEVEL 3"}, {PitchL4, "LEVEL 4"}};
constexpr int kPitchItems = 8;
// An operator's page, by its own setting (kOpOn: its ON, a bit of OPERATORS ON: opOnId below).
constexpr int kOpOn = -1;
const Item kOp[] = {{kOpOn, "ON"}, {OpLevel, "OUTPUT LEVEL"}, {OpCoarse, "COARSE"}, {OpFine, "FINE"},
                    {OpMode, "MODE"}, {OpDetune, "DETUNE"}, {OpVelocity, "VELOCITY"}, {OpAms, "LEVEL SENS"},
                    {OpR1, "RATE 1"}, {OpR2, "RATE 2"}, {OpR3, "RATE 3"}, {OpR4, "RATE 4"},
                    {OpL1, "LEVEL 1"}, {OpL2, "LEVEL 2"}, {OpL3, "LEVEL 3"}, {OpL4, "LEVEL 4"},
                    {OpRateScaling, "RATE SCALING"}, {OpBreakPoint, "BREAK POINT"}, {OpLeftDepth, "LEFT DEPTH"}, {OpRightDepth, "RIGHT DEPTH"},
                    {OpLeftCurve, "LEFT CURVE"}, {OpRightCurve, "RIGHT CURVE"}};
constexpr int kOpItems = static_cast<int>(sizeof kOp / sizeof kOp[0]);
static_assert(kOpItems == kOpSettings + 1, "every setting of an operator, and its ON");
static_assert(kVoiceItems + kPitchItems + 6 * kOpSettings + 1 == kSettings, "every setting is on one page (OPERATORS ON as six ONs)");

// OPERATOR n ON: shown on its page as a setting of its own, past the synth's (kSettings + n - 1).
// The engine has one, OPERATORS ON, a bit an operator (bit 6 - n).
constexpr int opOnId(int op) { return kSettings + op - 1; }
constexpr int kShownSettings = kSettings + 6;
bool bitOf(int id, int& whole, int& bit) {
    if (id < kSettings || id >= kShownSettings) return false;
    whole = OpsOn;
    bit = 6 - (id - kSettings + 1);
    return true;
}
int shownMin(int id) { return id >= kSettings ? 0 : settingMin(id); }
int shownMax(int id) { return id >= kSettings ? (id < kShownSettings ? 1 : 0) : settingMax(id); }

void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Fm6, TrackKind::Fm6>::own(index)) {
        for (int id = 0; id < kPatchSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.fm6.value[id] = v < settingMin(id) ? settingMin(id) : (v > settingMax(id) ? settingMax(id) : v);
        }
    } else {
        t.fm6 = Fm6::presetPatch(index);
    }
}
int* values(TrackData& t) { return t.fm6.value; }

// Which operator's setting `id` is, and which of its 21; false for the voice's own.
bool opOf(int id, int& op, int& s) {
    if (id < 0 || id >= 6 * kOpSettings) return false;
    op = 6 - id / kOpSettings;
    s = id % kOpSettings;
    return true;
}

// A ratio: COARSE 0 is a half, then 1 to 31. With FINE: up to nearly twice that.
void ratioText(int coarse, int fine, char* out, std::size_t n) {
    const double ratio = (coarse == 0 ? 0.5 : static_cast<double>(coarse)) * (1.0 + fine / 100.0);
    std::snprintf(out, n, "%.2f", ratio);
}
// A fixed pitch: 1, 10, 100 or 1000 Hz by COARSE (its last two bits), times up to nearly ten by FINE.
void fixedText(int coarse, int fine, char* out, std::size_t n) {
    const double hz = std::pow(10.0, ((coarse & 3) * 100 + fine) / 100.0);
    if (hz < 9.995) std::snprintf(out, n, "%.2f HZ", hz);
    else if (hz < 99.95) std::snprintf(out, n, "%.1f HZ", hz);
    else std::snprintf(out, n, "%.0f HZ", hz);
}

void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    const auto one = [&](const char* const* names, int count) { std::snprintf(out, n, "%s", names[value < 0 ? 0 : (value >= count ? count - 1 : value)]); };
    static const char* const onOff[2] = {"OFF", "ON"};
    static const char* const modes[2] = {"RATIO", "FIXED"};
    static const char* const curves[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
    static const char* const waves[6] = {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "RANDOM"};
    static const char* const notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (id >= kSettings) return one(onOff, 2); // an operator's ON
    int op = 0, s = 0;
    if (opOf(id, op, s)) {
        switch (s) {
        case OpMode: return one(modes, 2);
        case OpLeftCurve: case OpRightCurve: return one(curves, 4);
        case OpCoarse: return ratioText(value, 0, out, n); // (alone it reads as a ratio: the device reads it with MODE)
        case OpDetune: std::snprintf(out, n, "%+d", value - 7); return;
        case OpBreakPoint: {
            // 0 is A-1 and 99 C8: 39 is C3, as the original names its keys.
            const int key = (value < 0 ? 0 : value > 99 ? 99 : value) + 9;
            std::snprintf(out, n, "%s%d", notes[key % 12], key / 12 - 1);
            return;
        }
        default: std::snprintf(out, n, "%d", value); return;
        }
    }
    switch (id) {
    case Algorithm: std::snprintf(out, n, "%d", value + 1); return;
    case KeySync: case LfoSync: return one(onOff, 2);
    case LfoWave: return one(waves, 6);
    case Transpose: std::snprintf(out, n, "%+d", value - 24); return; // in semitones, 0 as the key is played
    default: std::snprintf(out, n, "%d", value); return;
    }
}

// COARSE and FINE read with the rest of their operator: a ratio of the key ("1.41"), or under MODE
// FIXED a pitch ("141 HZ"). COARSE says its own part, FINE where the two land together.
void readingIn(const int* values, int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    int op = 0, s = 0;
    if (!opOf(id, op, s) || (s != OpCoarse && s != OpFine)) return settingText(id, value, out, size);
    const int coarse = s == OpCoarse ? value : values[opSetting(op, OpCoarse)];
    const int fine = s == OpFine ? value : 0;
    if (values[opSetting(op, OpMode)] != 0) fixedText(coarse, fine, out, n);
    else ratioText(coarse, fine, out, n);
}

void macroText(int, float v, char* out, int size) { plusMinus(v, out, size); }
bool envelopePage(int) { return false; }
int itemCount(int page) {
    if (page == 1) return kVoiceItems;
    if (page >= kFirstOpPage && page < kFirstOpPage + 6) return kOpItems;
    if (page == kPitchPage) return kPitchItems;
    return 0;
}
void item(int page, int index, int& id, const char*& name) {
    if (page == 1) { id = kVoice[index].id; name = kVoice[index].name; return; }
    if (page == kPitchPage) { id = kPitch[index].id; name = kPitch[index].name; return; }
    const int op = page - kFirstOpPage + 1;
    const Item& it = kOp[index];
    id = it.id == kOpOn ? opOnId(op) : opSetting(op, it.id);
    name = it.name;
}
} // namespace fm

// ---- PHYSIC ----------------------------------------------------------------------------
// The plan: docs/plans/physic-plan.md, sections 3 and 7. Twenty-three settings on five pages (BODY,
// batch 10's), every one read in its own unit where it has one (seconds, cents, dB, hertz, where along
// it is struck).

namespace ps {
using namespace physic;

const char* const kPages[6] = {"MAIN", "SOUND", "RING", "PLAY", "DICE", "BODY"};
struct Item { int id; const char* name; };
const Item kSound[] = {{Model, "MODEL"}, {Strike, "STRIKE"}, {Hardness, "HARDNESS"}, {Position, "POSITION"}};
const Item kRing[] = {{Decay, "DECAY"}, {Material, "MATERIAL"}, {Stretch, "STRETCH"}, {Release, "RELEASE"}};
const Item kPlay[] = {{Velocity, "VELOCITY"}, {Octave, "OCTAVE"}, {Fine, "FINE"}, {Width, "WIDTH"}, {Level, "LEVEL"}};
const Item kDice[] = {{Dice, "DICE"}, {DicePosition, "DICE POSITION"}, {DiceHardness, "DICE HARDNESS"}, {DiceTune, "DICE TUNE"}, {DiceDecay, "DICE DECAY"}};
const Item kBody[] = {{Pair, "PAIR"}, {Motor, "MOTOR"}, {Body, "BODY"}, {BodyMix, "BODY MIX"}, {Buzz, "BUZZ"}};
struct Page { const Item* items; int count; };
const Page kPageItems[6] = {{nullptr, 0}, {kSound, 4}, {kRing, 4}, {kPlay, 5}, {kDice, 5}, {kBody, 5}};
static_assert(4 + 4 + 5 + 5 + 5 == kSettings, "every setting is on one page");

// A player's own preset is written over the FIRST preset's sound, not over what the track had: a
// preset saved before a setting was appended (the plan's batch 10: PAIR, MOTOR, BODY) then plays
// the new setting as the factory has it, not as the last preset left it.
void loadPreset(TrackData& t, int index) {
    if (const std::vector<int>* own = Presets<Physic, TrackKind::Physic>::own(index)) {
        t.physic = Physic::presetPatch(0);
        for (int id = 0; id < kSettings && id < static_cast<int>(own->size()); ++id) {
            const int v = (*own)[id];
            t.physic.value[id] = v < settingMin(id) ? settingMin(id) : (v > settingMax(id) ? settingMax(id) : v);
        }
    } else {
        t.physic = Physic::presetPatch(index);
    }
}
int* values(TrackData& t) { return t.physic.value; }

// A time: "50 MS", "1.2 S", "30 S".
void secondsText(double s, char* out, std::size_t n) {
    if (s < 0.9995) std::snprintf(out, n, "%.0f MS", s * 1000.0);
    else if (s < 9.95) std::snprintf(out, n, "%.1f S", s);
    else std::snprintf(out, n, "%.0f S", s);
}
// Where along it is struck: "1/7" where that is the nearest setting to a seventh, else a share ("23.5%").
void positionText(int value, char* out, std::size_t n) {
    const double along = positionAlong(value), step = 0.49 / 100.0;
    for (int part = 2; part <= 9; ++part)
        if (std::fabs(along - 1.0 / part) <= step * 0.5) return static_cast<void>(std::snprintf(out, n, "1/%d", part));
    std::snprintf(out, n, "%.1f%%", along * 100.0);
}
// STRING's stiffness, B (F64): "B 0.0004"; under a ten-thousandth, as a power of ten.
void stiffnessText(int value, char* out, std::size_t n) {
    const double b = stiffnessOf(value);
    if (b <= 0.0) std::snprintf(out, n, "B 0");
    else if (b < 1e-4) std::snprintf(out, n, "B %.1E", b);
    else std::snprintf(out, n, "B %.4f", b);
}

void settingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    switch (id) {
    case Model: std::snprintf(out, n, "%s", modelName(value)); return;
    case Strike: std::snprintf(out, n, "%s", strikeName(value)); return;
    case Body: std::snprintf(out, n, "%s", bodyName(value)); return;
    case Pair: // the two of a pair this far apart
        if (value <= 0) std::snprintf(out, n, "OFF");
        else std::snprintf(out, n, "%d CENTS", value);
        return;
    case Motor: { // the tremolo's speed
        const double hz = motorHz(value);
        if (hz <= 0.0) std::snprintf(out, n, "OFF");
        else std::snprintf(out, n, hz < 0.995 ? "%.2f HZ" : "%.1f HZ", hz);
        return;
    }
    case Buzz: // how far under its loudest the note still rattles
        if (value <= 0) std::snprintf(out, n, "OFF");
        else std::snprintf(out, n, "%.0f DB", buzzDb(value));
        return;
    case Position: return positionText(value, out, n);
    case Decay: return secondsText(decaySeconds(value), out, n);
    case Material:
        if (value <= 0) std::snprintf(out, n, "WOOD");
        else if (value >= 100) std::snprintf(out, n, "METAL");
        else std::snprintf(out, n, "%d", value);
        return;
    case Stretch: // (alone, as on a modal model; on STRING the device reads B: readingIn)
        if (value == 50) std::snprintf(out, n, "AS MADE");
        else std::snprintf(out, n, "%+d", value - 50);
        return;
    case Release:
        if (releaseRings(value)) std::snprintf(out, n, "RING");
        else secondsText(releaseSeconds(value), out, n);
        return;
    case Octave: std::snprintf(out, n, "%+d", value); return;
    case Fine: std::snprintf(out, n, "%+d CENTS", value); return;
    case Level: std::snprintf(out, n, "%+d DB", value); return;
    case DiceTune: // its most: kDiceTuneCents either way, at DICE 100
        if (value <= 0) std::snprintf(out, n, "OFF");
        else std::snprintf(out, n, "%.1f CENTS", kDiceTuneCents * value / 100.0);
        return;
    case Velocity: case Dice: case DicePosition: case DiceHardness: case DiceDecay: case BodyMix:
        if (value <= 0) std::snprintf(out, n, "OFF");
        else std::snprintf(out, n, "%d%%", value);
        return;
    default: std::snprintf(out, n, "%d%%", value); return; // HARDNESS, WIDTH
    }
}

// What a setting does with the rest of the sound: HARDNESS under MALLET is the mallet's contact
// ("2.1 MS", at full velocity: a softer note's is longer, by Hertz's law) and under NOISE the burst's
// length ("2.0 S" to "2 MS"), and STRETCH on STRING is the string's stiffness ("B 0.0004").
void readingIn(const int* values, int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (id == Hardness && values[Strike] == kNoise) return secondsText(noiseSeconds(value), out, n);
    if (id == Hardness && values[Strike] == kMallet) {
        const double ms = contactSeconds(value, 1.0, 1.0) * 1000.0;
        std::snprintf(out, n, ms < 0.995 ? "%.2f MS" : "%.1f MS", ms);
        return;
    }
    if (id == Stretch && values[Model] == kString) return stiffnessText(value, out, n);
    settingText(id, value, out, size);
}

// OCTAVE's macro is a threshold (down under a quarter, up over three), as Phazed's.
void macroText(int macro, float v, char* out, int size) {
    if (macro == MacroOctave) std::snprintf(out, static_cast<std::size_t>(size), "%s", v < 0.25f ? "DOWN" : v > 0.75f ? "UP" : "AS PRESET");
    else plusMinus(v, out, size);
}
bool envelopePage(int) { return false; }
int itemCount(int page) { return page >= 1 && page < 6 ? kPageItems[page].count : 0; }
void item(int page, int index, int& id, const char*& name) {
    const Item& it = kPageItems[page].items[index];
    id = it.id;
    name = it.name;
}

// Y on MAIN (the plan's call 10): a new strike and ring for the same MODEL. STRIKE, HARDNESS,
// POSITION, DECAY, MATERIAL and STRETCH are rolled, each from the seed and its own place (a hash,
// never a generator walked), and of batch 10's PAIR, MOTOR, BODY MIX and BUZZ; MODEL, BODY, RELEASE,
// the levels and the dice are left as they are. Each roll keeps inside what sounds as the model: a
// modal model is struck by a mallet three times in four, a string plucked, and either is breathed on
// (NOISE) one time in eight; a string is never made more than a little stiff, nor rings past 15 s.
// PAIR is off half the time (else 2 to 12 cents), MOTOR three times in four (else 10 to 90), BUZZ four
// in five (else 5 to 45): a sound with all of them on every time is no longer the model's own.
double unit(unsigned seed, int which) { // 0 up to 1
    return chordHash(chordHash(seed * 0x9E3779B1u + 0x2545F491u) ^ static_cast<std::uint32_t>(which + 1) * 0x85EBCA6Bu) / 4294967296.0;
}
int between(unsigned seed, int which, int lo, int hi) { return lo + static_cast<int>(unit(seed, which) * (hi - lo + 1)); }
void roll(TrackData& t, unsigned seed) {
    int* v = t.physic.value;
    const bool string = v[Model] == kString;
    v[Strike] = (unit(seed, 0) < 0.75) == string ? 0 : 1; // PLUCK on a string, MALLET on the rest, mostly
    v[Hardness] = between(seed, 1, 10, 95);
    v[Position] = between(seed, 2, 5, 95);
    v[Decay] = string ? between(seed, 3, 30, 85) : between(seed, 3, 20, 92);
    v[Material] = between(seed, 4, 0, 100);
    v[Stretch] = string ? between(seed, 5, 0, 50) : between(seed, 5, 30, 70);
    if (unit(seed, 6) < 0.125) v[Strike] = kNoise;
    v[Pair] = unit(seed, 7) < 0.5 ? 0 : between(seed, 8, 2, 12);
    v[Motor] = unit(seed, 9) < 0.75 ? 0 : between(seed, 10, 10, 90);
    v[Buzz] = unit(seed, 11) < 0.8 ? 0 : between(seed, 12, 5, 45);
    v[BodyMix] = between(seed, 13, 20, 90);
}
} // namespace ps

} // namespace

const char* const kJaxepBankNote = "1983 BOOKLET, SPEEDS GUESSED";
const char* const kJaxepGuessNote = "SPEEDS GUESSED";
const char* jaxepFolderNote(const char* folder) { return std::strncmp(folder, "BANK ", 5) == 0 ? kJaxepBankNote : kJaxepGuessNote; }

const SynthSpec& phazedSpec() {
    static const SynthSpec spec = {"PHAZED", Presets<Phazed, TrackKind::Phazed>::count, Presets<Phazed, TrackKind::Phazed>::name, Presets<Phazed, TrackKind::Phazed>::category, pz::loadPreset, pz::values,
                                   phazed::settingMin, phazed::settingMax, pz::settingText,
                                   {"TONE", "MOTION", "ATTACK", "RELEASE", "DETUNE", "VIBRATO", "CHORUS", "OCTAVE"}, pz::macroText,
                                   10, pz::kPages, pz::itemCount, pz::item, pz::envelopePage, nullptr, false, nullptr, nullptr, nullptr};
    return spec;
}

const SynthSpec& jx3pSpec() {
    static const SynthSpec spec = {"JAX-EP", Presets<Jx3p, TrackKind::Jx3p>::count, Presets<Jx3p, TrackKind::Jx3p>::name, Presets<Jx3p, TrackKind::Jx3p>::category, jx::loadPreset, jx::values,
                                   jx3p::settingMin, jx3p::settingMax, jx::settingText,
                                   {"CUTOFF", "RESO", "ENV", "TUNE", "MIX", "LFO", "ATTACK", "RELEASE"}, jx::macroText,
                                   6, jx::kPages, jx::itemCount, jx::item, jx::envelopePage, jx::folderNote, true, nullptr, nullptr, nullptr};
    return spec;
}

void setUserPresets(TrackKind kind, std::vector<UserPreset> presets) { userList(kind) = std::move(presets); }
const std::vector<UserPreset>& userPresets(TrackKind kind) { return userList(kind); }
int builtInPresets(TrackKind kind) {
    return kind == TrackKind::Jx3p ? Jx3p::presetCount() : kind == TrackKind::Phazed ? Phazed::presetCount() : kind == TrackKind::Fm6 ? Fm6::presetCount()
         : kind == TrackKind::Physic ? Physic::presetCount() : 0;
}
int settingCount(TrackKind kind) {
    return kind == TrackKind::Jx3p ? static_cast<int>(jx3p::kSettings) : kind == TrackKind::Phazed ? static_cast<int>(phazed::kSettings) : kind == TrackKind::Fm6 ? static_cast<int>(fm6::kSettings)
         : kind == TrackKind::Physic ? static_cast<int>(physic::kSettings) : 0;
}
int shownSettingCount(TrackKind kind) { return kind == TrackKind::Fm6 ? fm::kShownSettings : settingCount(kind); }

int settingValue(const SynthSpec& spec, const int* values, int id) {
    int whole = 0, bit = 0;
    if (spec.bitOf && spec.bitOf(id, whole, bit)) return (values[whole] >> bit) & 1;
    return values[id];
}
void settingWrite(const SynthSpec& spec, const int* values, int id, int value, int& realId, int& realValue) {
    value = value < spec.settingMin(id) ? spec.settingMin(id) : (value > spec.settingMax(id) ? spec.settingMax(id) : value);
    int whole = 0, bit = 0;
    if (spec.bitOf && spec.bitOf(id, whole, bit)) {
        realId = whole;
        realValue = (values[whole] & ~(1 << bit)) | (value << bit);
        return;
    }
    realId = id;
    realValue = value;
}
void settingReading(const SynthSpec& spec, const int* values, int id, char* out, int size) {
    const int value = settingValue(spec, values, id);
    if (spec.readingIn) spec.readingIn(values, id, value, out, size);
    else spec.settingText(id, value, out, size);
}
int presetSettingCount(TrackKind kind) {
    return kind == TrackKind::Jx3p ? static_cast<int>(jx3p::kPatchSettings) : settingCount(kind);
}

const SynthSpec& fm6Spec() {
    static const SynthSpec spec = {"FM-6", Presets<Fm6, TrackKind::Fm6>::count, Presets<Fm6, TrackKind::Fm6>::name, Presets<Fm6, TrackKind::Fm6>::category, fm::loadPreset, fm::values,
                                   fm::shownMin, fm::shownMax, fm::settingText,
                                   {"BRIGHT", "FEEDBACK", "ATTACK", "DECAY", "RELEASE", "DETUNE", "LFO", "PITCH ENV"}, fm::macroText,
                                   9, fm::kPages, fm::itemCount, fm::item, fm::envelopePage, nullptr, false, fm::bitOf, fm::readingIn, nullptr};
    return spec;
}

// PHYSIC's macros are the engine's eight (physic::Macro): TONE is HARDNESS, the rest are named for
// what they move. Its WiFi page reads every setting as the device does (pageReadsAsDevice): "1.2 S".
const SynthSpec& physicSpec() {
    static const SynthSpec spec = {"PHYSIC", Presets<Physic, TrackKind::Physic>::count, Presets<Physic, TrackKind::Physic>::name, Presets<Physic, TrackKind::Physic>::category, ps::loadPreset, ps::values,
                                   physic::settingMin, physic::settingMax, ps::settingText,
                                   {"TONE", "POSITION", "DECAY", "MATERIAL", "STRETCH", "RELEASE", "DICE", "OCTAVE"}, ps::macroText,
                                   6, ps::kPages, ps::itemCount, ps::item, ps::envelopePage, nullptr, true, nullptr, ps::readingIn, ps::roll};
    return spec;
}

const SynthSpec* specFor(TrackKind kind) {
    return kind == TrackKind::Phazed ? &phazedSpec() : kind == TrackKind::Jx3p ? &jx3pSpec() : kind == TrackKind::Fm6 ? &fm6Spec()
         : kind == TrackKind::Physic ? &physicSpec() : nullptr;
}

} // namespace apotheca
