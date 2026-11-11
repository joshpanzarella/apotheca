#include "apotheca/qmod.hpp"

#include <cmath>
#include <cstdint>

#include "apotheca/envelope.hpp"
#include "shapetaker/entanglement/constellation.hpp"

namespace apotheca {

using namespace qmod;
namespace ent = shapetaker::entanglement;

namespace {

constexpr int kOps = ent::OPS; // eight
constexpr float kIndexScale = 2.5f;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Preset {
    const char* name;
    const char* category;
    int value[kSettings];
};
// Sound, Variation, Mutation, Entangle, Tension, Evolve, Affinity, Symmetry, Family, Octave, Width, A, D, S, R, Level
const Preset kPresets[] = {
    {"First Light", "Keys", {101, 3, 45, 20, 42, 35, 30, 80, 0, 0, 50, 0, 62, 35, 45, 0}},
    {"Glass Keys", "Keys", {2207, 8, 40, 35, 50, 25, 20, 90, 0, 0, 55, 0, 58, 20, 48, 0}},
    {"Reed Keys", "Keys", {4410, 1, 50, 10, 38, 55, 45, 75, 0, 0, 40, 2, 60, 55, 40, 0}},
    {"Tine", "Keys", {733, 12, 35, 50, 46, 20, 15, 95, 0, 0, 45, 0, 60, 15, 50, 0}},
    {"Round Sub", "Bass", {58, 2, 30, 0, 30, 30, 20, 100, 0, -2, 10, 0, 50, 70, 25, 2}},
    {"Growl", "Bass", {1912, 5, 55, 40, 68, 60, 70, 70, 0, -2, 20, 0, 52, 60, 28, 0}},
    {"Rubber", "Bass", {3050, 9, 45, 25, 55, 28, 40, 85, 0, -1, 15, 0, 45, 40, 24, 1}},
    {"Wire Bass", "Bass", {6401, 4, 60, 60, 62, 40, 55, 60, 0, -1, 25, 0, 48, 50, 26, 0}},
    {"Small Bell", "Bells", {911, 6, 50, 30, 60, 30, 25, 20, 0, 1, 70, 0, 68, 0, 62, -2}},
    {"Temple", "Bells", {5120, 14, 55, 55, 70, 45, 35, 10, 0, 0, 80, 0, 74, 0, 70, -3}},
    {"Glock", "Bells", {277, 2, 35, 15, 48, 15, 10, 35, 0, 1, 60, 0, 55, 0, 50, -1}},
    {"Tubular", "Bells", {8088, 20, 60, 45, 66, 55, 40, 0, 0, 0, 75, 0, 72, 0, 68, -3}},
    {"Slow Bloom", "Pads", {1500, 7, 50, 50, 40, 85, 30, 85, 0, 0, 85, 62, 60, 85, 72, -1}},
    {"Choir Of Wires", "Pads", {3399, 11, 55, 65, 52, 90, 45, 65, 0, 0, 90, 58, 60, 80, 74, -2}},
    {"Cold Air", "Pads", {7204, 16, 45, 40, 36, 95, 20, 95, 0, 1, 80, 66, 60, 90, 76, -1}},
    {"Drift", "Pads", {620, 30, 70, 50, 58, 80, 60, 50, 0, 0, 95, 55, 60, 75, 70, -2}},
    {"Bright Pluck", "Plucks", {42, 5, 45, 30, 58, 12, 30, 80, 0, 0, 55, 0, 42, 0, 34, 0}},
    {"Koto", "Plucks", {2718, 9, 40, 20, 64, 18, 25, 55, 0, 0, 50, 0, 46, 0, 38, 0}},
    {"Mallet", "Plucks", {3141, 3, 50, 45, 44, 10, 15, 90, 0, 0, 45, 0, 40, 0, 32, 1}},
    {"Clav", "Plucks", {1618, 13, 55, 35, 72, 22, 50, 70, 0, 0, 35, 0, 44, 10, 28, 0}},
    {"Sharp Lead", "Leads", {999, 4, 50, 30, 66, 70, 55, 75, 0, 0, 40, 1, 55, 85, 30, -1}},
    {"Hollow Lead", "Leads", {4747, 10, 45, 55, 48, 65, 30, 90, 0, 0, 45, 3, 55, 80, 34, 0}},
    {"Scrape", "Leads", {8660, 22, 70, 60, 82, 75, 85, 35, 0, 0, 55, 1, 55, 80, 30, -3}},
    {"Furnace", "Noise", {5555, 40, 80, 50, 92, 85, 95, 15, 0, -1, 90, 5, 60, 70, 50, -4}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

// One generated wiring, flattened for playing: who feeds whom as a short list.
struct Wiring {
    struct Edge { int from, to; float amount; };
    Edge edges[kOps * kOps];
    int edgeCount = 0;
    float ratio[kOps], level[kOps], carrier[kOps], feedback[kOps], life[kOps], panL[kOps], panR[kOps];
    float inScale[kOps]; // an operator fed by many is fed less by each
    float carrierGain = 1.f;
    int family = 0;
};

std::uint32_t seedFor(int sound) { return 0x9E3779B9u * static_cast<std::uint32_t>(sound) + 0x51u; }

ent::Constellation generateA(const int* p) {
    return ent::generate(seedFor(p[Sound]), 1.f, p[Affinity] / 100.f, p[Symmetry] / 100.f, nullptr, p[Family] > 0 ? p[Family] - 1 : -1);
}

} // namespace

namespace qmod {

int settingMin(int id) { return id == Sound ? 1 : id == Octave ? -2 : id == Level ? -24 : 0; }
int settingMax(int id) {
    switch (id) {
    case Sound: return 9999;
    case Variation: return 99;
    case Family: return 32;
    case Octave: return 2;
    case Level: return 6;
    default: return 100;
    }
}
const char* familyName(int family) { return family >= 1 && family <= 32 ? ent::algorithmName(family - 1) : ""; }

} // namespace qmod

struct Qmod::State {
    struct Voice {
        envelope::Envelope env;
        float phase[2][kOps] = {}, last[2][kOps] = {}, bend[2][kOps] = {}; // bend: how much of the FM is left in this note, per wiring and operator
        float velocity = 0.f, step = 0.f;
        int note = -1;
        std::uint32_t age = 0;
    };
    QmodPatch patch;
    float macro[kMacros];
    float sampleRate = 48000.f;
    Voice voices[kVoices];
    std::uint32_t clock = 0;
    Wiring wiring[2];
    envelope::Shape shape;
    float entangle = 0.f, index = 0.f, bendFloor = 0.f, bendFall[2][kOps] = {}, gain = 0.5f, octave = 1.f;
    const ent::SineTable* sine = nullptr;
};

Qmod::Qmod() : state_(new State) {
    for (float& m : state_->macro) m = 0.5f;
    state_->patch = presetPatch(0);
    state_->sine = &ent::sineTable();
    apply();
}
Qmod::~Qmod() = default;

int Qmod::presetCount() { return kPresetCount; }
const char* Qmod::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Qmod::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
QmodPatch Qmod::presetPatch(int i) {
    QmodPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kSettings; ++id) patch.value[id] = clampi(preset.value[id], settingMin(id), settingMax(id));
    return patch;
}
int Qmod::familyOf(const QmodPatch& patch) { return generateA(patch.value).algorithm + 1; }

void Qmod::prepare(float sampleRate) {
    state_->sampleRate = sampleRate;
    apply();
}

// Work out everything the voices read from the patch and the macros. Called
// when a setting or a macro changes; the generator is a few hundred sums.
void Qmod::apply() {
    State& s = *state_;
    const int* p = s.patch.value;
    const float* m = s.macro;
    const auto bent = [&](int id, int macro, float range) { return clampf(p[id] + (m[macro] - 0.5f) * range, 0.f, 100.f); };

    // The two wirings: the SOUND's own, and a cousin of it.
    const ent::Constellation a = generateA(p);
    const int variation = clampi(p[Variation] + static_cast<int>(std::lround((m[MacroVariation] - 0.5f) * 32.f)), 0, 99);
    const ent::Constellation b = ent::generate(seedFor(p[Sound]) ^ (0x6b2f4c1du * static_cast<std::uint32_t>(variation + 1)), p[Mutation] / 100.f,
                                               p[Affinity] / 100.f, p[Symmetry] / 100.f, &a);
    const float evolve = bent(Evolve, MacroEvolve, 100.f) / 100.f;
    const float width = bent(Width, MacroWidth, 100.f) / 100.f;
    const ent::Constellation* both[2] = {&a, &b};
    for (int w = 0; w < 2; ++w) {
        const ent::Constellation& c = *both[w];
        Wiring& out = s.wiring[w];
        out.edgeCount = 0;
        out.family = c.algorithm + 1;
        float carriers = 0.f;
        int incoming[kOps] = {};
        for (int from = 0; from < kOps; ++from)
            for (int to = 0; to < kOps; ++to)
                if (from != to && c.edge[from][to] > 0.001f) {
                    out.edges[out.edgeCount++] = {from, to, c.edge[from][to]};
                    ++incoming[to];
                }
        for (int op = 0; op < kOps; ++op) {
            out.ratio[op] = c.ratio[op];
            out.level[op] = c.level[op];
            out.carrier[op] = c.carrier[op] * c.level[op];
            out.feedback[op] = c.feedback[op] + (c.edge[op][op] > 0.001f ? c.edge[op][op] * 0.3f : 0.f) + 0.08f * c.color[op];
            out.inScale[op] = incoming[op] > 1 ? 1.f / std::sqrt(static_cast<float>(incoming[op])) : 1.f;
            // Equal-power pan, as wide as WIDTH says.
            const float pan = clampf(c.pan[op] * width, -1.f, 1.f);
            out.panL[op] = std::sqrt(0.5f * (1.f - pan));
            out.panR[op] = std::sqrt(0.5f * (1.f + pan));
            carriers += out.carrier[op] * out.carrier[op];
            // How long this operator's bending lasts: its own contour, stretched by EVOLVE.
            out.life[op] = (0.04f + 2.6f * evolve * evolve) * (0.25f + 1.35f * c.contour[op]);
        }
        out.carrierGain = carriers > 0.f ? 1.f / std::sqrt(carriers) : 0.f; // several carriers are no louder than one
    }
    // Each wiring's operators lose their bending at their own rates. (They are kept apart so
    // that nothing about the second wiring is heard while ENTANGLE is all the first.)
    for (int w = 0; w < 2; ++w)
        for (int op = 0; op < kOps; ++op) s.bendFall[w][op] = std::exp(-1.f / (s.wiring[w].life[op] * s.sampleRate));
    s.bendFloor = 0.25f + 0.75f * evolve; // what is left when the first brightness has gone: all of it at full EVOLVE
    s.entangle = bent(Entangle, MacroEntangle, 100.f) / 100.f;
    // Entanglement's own map from TENSION to how far a phase is pushed (in cycles).
    const float tension = bent(Tension, MacroTension, 100.f) / 100.f;
    // QMOD's operators are plain sines (Entanglement's have overtones of their own), so the push is
    // larger here to reach the same brightness: measured, not guessed (tests/qmod_test.cpp).
    s.index = kIndexScale * std::pow(tension, 1.45f) * (0.7f + 4.3f * evolve) * (0.82f + 0.23f * p[Affinity] / 100.f) * 0.15915494f;
    s.shape.setFrom(p[Attack] + (m[MacroAttack] - 0.5f) * 100.f, static_cast<float>(p[Decay]), static_cast<float>(p[Sustain]),
                    p[Release] + (m[MacroRelease] - 0.5f) * 100.f, s.sampleRate);
    s.gain = 0.5f * std::pow(10.f, p[Level] / 20.f);
    const int shift = m[MacroOctave] < 0.25f ? -1 : (m[MacroOctave] > 0.75f ? 1 : 0);
    s.octave = std::pow(2.f, static_cast<float>(clampi(p[Octave] + shift, -3, 3)));
}

void Qmod::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    apply();
}

void Qmod::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = clampi(value, settingMin(id), settingMax(id));
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    apply();
}

void Qmod::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    state_->macro[macro] = clampf(value, 0.f, 1.f);
    apply();
}

void Qmod::noteOn(int note, float velocity) {
    State& s = *state_;
    State::Voice* chosen = nullptr;
    for (State::Voice& v : s.voices)
        if (v.note == note && !v.env.idle()) { chosen = &v; break; }
    if (!chosen)
        for (State::Voice& v : s.voices)
            if (v.env.idle()) { chosen = &v; break; }
    if (!chosen) {
        chosen = &s.voices[0];
        for (State::Voice& v : s.voices)
            if (s.clock - v.age > s.clock - chosen->age) chosen = &v;
    }
    if (chosen->env.idle()) {
        // A fresh voice starts its operators together; one taken over carries on, so it does not click.
        for (int w = 0; w < 2; ++w)
            for (int op = 0; op < kOps; ++op) chosen->phase[w][op] = chosen->last[w][op] = 0.f;
    }
    for (auto& wiring : chosen->bend)
        for (float& b : wiring) b = 1.f; // the FM starts at full and falls away
    chosen->note = note;
    chosen->velocity = velocity > 1.f ? 1.f : (velocity < 0.f ? 0.f : velocity);
    chosen->step = 440.f * std::pow(2.f, (note - 69) / 12.f) / s.sampleRate;
    chosen->env.on();
    chosen->age = s.clock++;
}

void Qmod::noteOff(int note) {
    for (State::Voice& v : state_->voices)
        if (v.note == note) v.env.off();
}

void Qmod::allNotesOff() {
    for (State::Voice& v : state_->voices) v.env.off();
}

void Qmod::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    // Only the wirings that can be heard are worked out.
    const bool useA = s.entangle < 0.995f, useB = s.entangle > 0.005f;
    const float mixA = useB ? 1.f - s.entangle : 1.f, mixB = useA ? s.entangle : 1.f;
    const ent::SineTable& sine = *s.sine;
    for (State::Voice& v : s.voices) {
        if (v.env.idle()) continue;
        const float step = v.step * s.octave;
        // Louder keys are brighter too, a little.
        const float index = s.index * (0.55f + 0.45f * v.velocity);
        for (int i = 0; i < frames; ++i) {
            const float amp = v.env.next(s.shape) * v.velocity * s.gain;
            if (v.env.idle()) break;
            float outL = 0.f, outR = 0.f;
            for (int w = 0; w < 2; ++w) {
                if (w == 0 ? !useA : !useB) continue;
                const Wiring& wire = s.wiring[w];
                float* phase = v.phase[w];
                float* last = v.last[w];
                // What each operator is being bent by: the others' last sample, and its own.
                float in[kOps], bend[kOps];
                for (int op = 0; op < kOps; ++op) {
                    in[op] = last[op] * wire.feedback[op];
                    v.bend[w][op] = s.bendFloor + (v.bend[w][op] - s.bendFloor) * s.bendFall[w][op];
                    bend[op] = v.bend[w][op] * index;
                }
                for (int e = 0; e < wire.edgeCount; ++e) in[wire.edges[e].to] += last[wire.edges[e].from] * wire.edges[e].amount * wire.inScale[wire.edges[e].to];
                float l = 0.f, r = 0.f;
                for (int op = 0; op < kOps; ++op) {
                    const float inc = step * wire.ratio[op];
                    float ph = phase[op] + inc;
                    if (ph >= 1.f) ph -= static_cast<float>(static_cast<int>(ph));
                    phase[op] = ph;
                    // Nothing above half the sample rate: an operator tuned that high is left out.
                    const float out = inc < 0.45f ? -sine.process(ph + in[op] * bend[op] + 1.f) * wire.level[op] : 0.f;
                    last[op] = out;
                    if (wire.carrier[op] > 0.f) {
                        l += out * wire.carrier[op] * wire.panL[op];
                        r += out * wire.carrier[op] * wire.panR[op];
                    }
                }
                const float mix = (w == 0 ? mixA : mixB) * wire.carrierGain;
                outL += l * mix;
                outR += r * mix;
            }
            left[i] += outL * amp;
            right[i] += outR * amp;
        }
    }
}

} // namespace apotheca
