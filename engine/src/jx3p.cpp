// The JX-3P: the voices, the assigner, and the wiring of the parts.
// See engine/include/apotheca/jx3p.hpp and docs/plans/jx3p-plan.md.

#include "apotheca/jx3p.hpp"

#include <cmath>
#include <cstdint>

#include "apotheca/slide.hpp"
#include "jx3p/env.hpp"

namespace apotheca {

using namespace jx3p;

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A voice at LEVEL 7 with its envelope at the top puts out a wave of this height. SELF: about
// what Twin gives, so a track swapped to it is not much louder or quieter (the start patch's note
// measured 1.6 dB over Twin's first preset's, 2026-11-14).
constexpr float kOutputGain = 0.3f;
// The highest a DCO is let go: a little under half the sample rate (the saw's correction needs it).
constexpr double kHighestStep = 0.45;

struct Preset {
    const char* name;
    const char* category;
    int value[kPatchSettings];
};
// The start patch: one plain saw, the filter open, a short decay to a held level. The 32
// factory patches come in batch 7.
// RANGE WAVE LFO ENV | RANGE WAVE XMOD TUNE FINE LFO ENV | LFO-D ENV-D POL | MIX HPF CUT RES LFO ENV POL KEY | MODE LEVEL | WAVE DELAY RATE | A D S R | CHORUS
const Preset kPresets[] = {
    {"Start", "START", {1, 0, 0, 0, /**/ 1, 0, 0, 50, 50, 0, 0, /**/ 0, 0, 0, /**/ 0, 0, 100, 0, 0, 0, 0, 0, /**/ 0, 70, /**/ 0, 0, 50, /**/ 0, 50, 70, 30, /**/ 1}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

// The setting a macro moves, or -1. (LFO moves two: see apply.)
int macroSetting(int macro) {
    switch (macro) {
    case MacroCutoff: return Cutoff;
    case MacroReso: return Resonance;
    case MacroEnv: return VcfEnv;
    case MacroTune: return Dco2Tune;
    case MacroMix: return SourceMix;
    case MacroLfo: return VcfLfo;
    case MacroAttack: return Attack;
    case MacroRelease: return Release;
    default: return -1;
    }
}
// A macro at 0.5 leaves the patch's value; under it slides the value down to 0, over it up to
// 100, so the whole dial is in reach from any patch.
float bent(float value, float macro) {
    if (macro < 0.5f) return value * (macro / 0.5f);
    if (macro > 0.5f) return value + (100.f - value) * ((macro - 0.5f) / 0.5f);
    return value;
}

// A number for a voice's start phase: the same for the same voice and the same count of keys.
std::uint32_t mixBits(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

double rangeFactor(int range) { return range == 0 ? 0.5 : (range == 2 ? 2.0 : 1.0); }

} // namespace

namespace jx3p {

int settingMin(int) { return 0; }
int settingMax(int id) {
    switch (id) {
    case Dco1Range: case Dco1Wave: case Dco2Range: case Dco2CrossMod: case LfoWave: return 2;
    case Dco2Wave: return 3;
    case Dco1Lfo: case Dco1Env: case Dco2Lfo: case Dco2Env: case DcoEnvPolarity: case VcfEnvPolarity: case VcaMode: case Chorus: case Velocity: return 1;
    default: return 100;
    }
}

} // namespace jx3p

struct Jx3p::State {
    struct Voice {
        int note = -1;        // -1: silent
        bool held = false;    // the key is down
        float velocity = 1.f;
        Adsr env;
        Hold vca;
        int toTick = 0;       // samples to the voice's next tick (0: on the next sample)
        double phase = 0.0;   // DCO-1, 0..1
        double step = 0.0;    // DCO-1's phase step a sample
        double hz = 0.0;
        Slide slide;          // GLIDE: how far it still is from its own key, in semitones
        bool landing = false; // the glide came to its key on the last sample
        std::uint64_t startedAt = 0, silentSince = 0; // the sample clock, for the assigner
    };
    Jx3pPatch patch;
    float macro[kMacros];
    float eff[kSettings];     // each setting as the voices read it: the patch, bent by the macros
    double sampleRate = 48000.0;
    int tickLength = 480;
    float holdK = 0.f;
    EnvTimes times;
    float levelGain = 1.f, clipFrom = 1.f;
    std::uint64_t clock = 0;  // samples since allNotesOff (or since made)
    std::uint32_t keys = 0;   // note-ons since allNotesOff
    Voice voices[kVoices];
    Voice tails[kTailVoices]; // let go by allNotesOff, ringing out beside the six (kTailVoices)

    // DCO-1's frequency for a key (with a glide's offset), as the counter can make it.
    double dcoHz(const Voice& v, double semitones) const {
        double hz = 440.0 * std::exp2((v.note + semitones - 69.0) / 12.0) * rangeFactor(static_cast<int>(eff[Dco1Range]));
        if (kRoundToCounter) {
            const double divisions = std::floor(kCounterClockHz / hz + 0.5);
            hz = kCounterClockHz / (divisions < 1.0 ? 1.0 : divisions);
        }
        return hz;
    }
    void setPitch(Voice& v, double semitones) {
        v.hz = dcoHz(v, semitones);
        v.step = v.hz / sampleRate;
        if (v.step > kHighestStep) v.step = kHighestStep;
    }
    // Samples from now to where a key is taken: now, or the next 10 ms of the one loop (kKeysOnTheSample).
    int keyWait() const { return kKeysOnTheSample ? 0 : static_cast<int>((tickLength - clock % static_cast<std::uint64_t>(tickLength)) % static_cast<std::uint64_t>(tickLength)); }

    // One tick of one voice: the CPU's loop. The envelope moves, the knobs are read, the CVs are written.
    void tick(Voice& v) {
        v.env.tick(times);
        const float opened = eff[VcaMode] >= 0.5f ? (v.held ? 1.f : 0.f) : v.env.level;
        v.vca.target = levelGain * opened * (eff[Velocity] >= 0.5f ? velocityGain(v.velocity) : 1.f);
        if (v.slide.semitones == 0.f) setPitch(v, 0.0);
    }
    void renderVoice(Voice& v, float* out, int frames);
};

Jx3p::Jx3p() : state_(new State) {
    for (float& m : state_->macro) m = 0.5f;
    state_->patch = presetPatch(0);
    prepare(48000.f);
}
Jx3p::~Jx3p() = default;

int Jx3p::presetCount() { return kPresetCount; }
const char* Jx3p::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Jx3p::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
Jx3pPatch Jx3p::presetPatch(int i) {
    Jx3pPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kPatchSettings; ++id) patch.value[id] = clampi(preset.value[id], settingMin(id), settingMax(id));
    patch.value[Velocity] = kNewTrackVelocity;
    patch.value[Brilliance] = kNewTrackBrilliance;
    return patch;
}

void Jx3p::prepare(float sampleRate) {
    State& s = *state_;
    s.sampleRate = sampleRate > 0.f ? sampleRate : 48000.0;
    s.tickLength = static_cast<int>(std::lround(kTickSeconds * s.sampleRate));
    if (s.tickLength < 1) s.tickLength = 1;
    s.holdK = holdCoef(s.sampleRate);
    apply();
}

// Everything the voices read from the patch and the macros, worked out when one of them moves.
// The voices take it on their next tick, as the real one reads its panel once a loop.
void Jx3p::apply() {
    State& s = *state_;
    for (int id = 0; id < kSettings; ++id) s.eff[id] = static_cast<float>(s.patch.value[id]);
    for (int m = 0; m < kMacros; ++m) {
        const int id = macroSetting(m);
        if (id >= 0) s.eff[id] = bent(s.eff[id], s.macro[m]);
    }
    s.eff[DcoLfoDepth] = bent(s.eff[DcoLfoDepth], s.macro[MacroLfo]); // LFO is one wobble: the VCF's and the DCOs' together
    s.times.set(s.eff[Attack], s.eff[Decay], s.eff[Sustain], s.eff[Release]);
    s.levelGain = vcaGain(s.eff[VcaLevel]);
    s.clipFrom = vcaGain(kVcaClipDial);
}

void Jx3p::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    const Jx3pPatch preset = presetPatch(index);
    for (int id = 0; id < kPatchSettings; ++id) state_->patch.value[id] = preset.value[id];
    for (float& m : state_->macro) m = 0.5f;
    apply();
}

void Jx3p::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = clampi(value, settingMin(id), settingMax(id));
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    apply();
}
int Jx3p::setting(int id) const { return id >= 0 && id < kSettings ? state_->patch.value[id] : 0; }

void Jx3p::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    state_->macro[macro] = clampf(value, 0.f, 1.f);
    apply();
}

// THE ASSIGNER: the same key if it is sounding, else the free voice longest silent, else the
// oldest sounding voice (a steal).
int Jx3p::noteOnVoice(int note, float velocity) {
    State& s = *state_;
    int chosen = -1;
    for (int i = 0; i < kVoices && chosen < 0; ++i)
        if (s.voices[i].note == note) chosen = i;
    for (int i = 0; i < kVoices && chosen < 0; ++i)
        if (s.voices[i].note < 0) {
            chosen = i;
            for (int j = i + 1; j < kVoices; ++j)
                if (s.voices[j].note < 0 && s.voices[j].silentSince < s.voices[chosen].silentSince) chosen = j;
        }
    // The steal: the oldest voice whose key is up (kStealLetGoFirst), else the oldest of all.
    for (int pass = kStealLetGoFirst ? 0 : 1; pass < 2 && chosen < 0; ++pass)
        for (int i = 0; i < kVoices; ++i)
            if ((pass == 1 || !s.voices[i].held) && (chosen < 0 || s.voices[i].startedAt < s.voices[chosen].startedAt)) chosen = i;
    State::Voice& v = s.voices[chosen];
    if (v.note < 0) {
        // A silent voice starts its DCO at a phase from a seed: the count of keys since allNotesOff
        // (new at every key, so the voice's number would add nothing). The real counters run free,
        // so a note's phase there is as good as random; this gives the same spread, and the same
        // bits every time.
        v.phase = static_cast<double>(mixBits(0x85EBCA6Bu * (s.keys + 1u)) >> 8) / 16777216.0;
        v.vca = Hold();
        v.env.stop();
    }
    // THE STEAL (and a key played again): a retrigger from where the voice is. Its envelope starts
    // its attack from its level now (software on the real one, so it is not set back to 0), its
    // DCO carries on from its phase, and its pitch moves to the new key on its first tick, the
    // sample the key is taken. No fade. (The owner's call; the other answer is a 2 ms fade.)
    v.note = note;
    v.held = true;
    v.velocity = velocity > 0.f ? (velocity < 1.f ? velocity : 1.f) : 0.f; // not a number: 0, never a stuck voice
    v.slide.stop();
    v.landing = false;
    v.env.on();
    v.startedAt = s.clock;
    v.toTick = s.keyWait();
    s.setPitch(v, 0.0);
    ++s.keys;
    return chosen;
}

void Jx3p::noteOn(int note, float velocity) { noteOnVoice(note, velocity); }

void Jx3p::noteOnFrom(int note, float velocity, int from, float seconds) {
    State& s = *state_;
    State::Voice& v = s.voices[noteOnVoice(note, velocity)];
    v.slide.start(from, note, seconds, static_cast<float>(s.sampleRate));
    s.setPitch(v, v.slide.semitones);
}

void Jx3p::noteOff(int note) {
    State& s = *state_;
    for (State::Voice& v : s.voices)
        if (v.note == note && v.held) {
            v.held = false;
            v.env.off();
            v.toTick = s.keyWait(); // let go on its sample: the release starts here, not up to 10 ms on
        }
}

// Play, Stop, panic: every key let go, and what remembers the past put back, so Play from the top
// is the same performance (the plan, section 2). A voice still sounding is not cut (Play and Stop
// let notes go): it is handed to a tail, rings out there, and its place among the six starts again
// as from fresh. So what comes after is the same bits as from a fresh synth, plus the tails.
void Jx3p::allNotesOff() {
    State& s = *state_;
    for (State::Voice& v : s.voices) {
        if (v.held) {
            v.held = false;
            v.env.off();
            v.toTick = 0;
        }
        if (v.note >= 0)
            for (State::Voice& t : s.tails)
                if (t.note < 0) {
                    t = v;
                    v = State::Voice();
                    break;
                }
        v.startedAt = v.silentSince = 0; // (a free voice's phase is set again from the seed at its next key;
                                         // one with no tail free to take it rings on here, as before tails)
    }
    s.clock = 0;
    s.keys = 0;
}

// One voice's `frames` added into `out`; it is set free (and stops) when silent with nothing to open it.
void Jx3p::State::renderVoice(Voice& v, float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        if (v.toTick <= 0) {
            tick(v);
            v.toTick += tickLength;
        }
        --v.toTick;
        if (v.slide.semitones != 0.f) {
            setPitch(v, v.slide.take(1));
            v.landing = v.slide.semitones == 0.f;
        } else if (v.landing) {
            setPitch(v, 0.0); // the glide is over: on its own key from this sample, not from the next tick
            v.landing = false;
        }
        // DCO-1's saw (batch 1: the plain two-sample BLEP, as Twin's; batch 2 builds the DCOs).
        const double t = v.phase, dt = v.step;
        float saw = static_cast<float>(2.0 * t - 1.0);
        if (t < dt) {
            const double x = t / dt;
            saw -= static_cast<float>(x + x - x * x - 1.0);
        } else if (t > 1.0 - dt) {
            const double x = (t - 1.0) / dt;
            saw -= static_cast<float>(x * x + x + x + 1.0);
        }
        v.phase += dt;
        if (v.phase >= 1.0) v.phase -= 1.0;
        const float gain = v.vca.next(holdK);
        out[i] += vcaClip(saw * gain, clipFrom);
        if (!v.held && gain == 0.f && v.vca.target == 0.f && (v.env.idle() || eff[VcaMode] >= 0.5f)) {
            // Silent, and nothing to open it again: the voice is free. Nothing of a free voice
            // moves, so how long it stays free cannot change the sound.
            v.note = -1;
            v.env.stop();
            v.slide.stop();
            v.silentSince = clock + static_cast<std::uint64_t>(i) + 1;
            return;
        }
    }
}

void Jx3p::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    bool any = false;
    for (State::Voice& v : s.voices)
        if (v.note >= 0) s.renderVoice(v, left, frames), any = true;
    for (State::Voice& v : s.tails) // (added after the six: the six's sum is a fresh synth's, to the bit)
        if (v.note >= 0) s.renderVoice(v, left, frames), any = true;
    s.clock += static_cast<std::uint64_t>(frames);
    if (!any) {
        for (int i = 0; i < frames; ++i) right[i] = 0.f;
        return;
    }
    for (int i = 0; i < frames; ++i) right[i] = left[i] = left[i] * kOutputGain; // mono until the chorus (batch 5)
}

int Jx3p::voiceNote(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].note : -1; }
float Jx3p::voiceEnv(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].env.level : 0.f; }
float Jx3p::voiceVca(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].vca.value : 0.f; }
double Jx3p::voiceHz(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].hz : 0.0; }
bool Jx3p::voiceHeld(int voice) const { return voice >= 0 && voice < kVoices && state_->voices[voice].held; }
int Jx3p::tailsRinging() const {
    int n = 0;
    for (const State::Voice& v : state_->tails) n += v.note >= 0 ? 1 : 0;
    return n;
}

} // namespace apotheca
