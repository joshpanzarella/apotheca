// The JX-3P: the voices, the assigner, and the wiring of the parts.
// See engine/include/apotheca/jx3p.hpp and docs/plans/jx3p-plan.md.

#include "apotheca/jx3p.hpp"

#include <cmath>
#include <cstdint>

#include "apotheca/slide.hpp"
#include "jx3p/chorus.hpp"
#include "jx3p/dco.hpp"
#include "jx3p/env.hpp"
#include "jx3p/ir3109.hpp"
#include "jx3p/lfo.hpp"

namespace apotheca {

using namespace jx3p;

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A voice at LEVEL 7 with its envelope at the top puts out a wave of this height. SELF: about
// what Twin gives, so a track swapped to it is not much louder or quieter (the start patch's note
// measured 1.6 dB over Twin's first preset's, 2026-11-14).
constexpr float kOutputGain = 0.3f;
// The highest a DCO is let go: a little under half the sample rate (its corrections need room).
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
        DcoPair dco;          // both DCOs, the cross mod and the noise (jx3p/dco.hpp)
        double count1 = 0.0, count2 = 0.0; // DCO-1's and DCO-2's counts of the clock (kRoundToCounter; else their Hz)
        double modSemis1 = 0.0, modSemis2 = 0.0; // what ENV DEPTH and LFO DEPTH add to each DCO's pitch, set on the tick
        double over1 = 1.0, over2 = 1.0; // the key's frequency over each DCO's: RANGE, TUNE, FINE, ENV, LFO (set on the tick)
        float mix = 0.f;      // SOURCE MIX, 0..1, read on the tick
        // The VCF (jx3p/ir3109.hpp). Its CV, in octaves over kVcfLowHz, is the tick's part (CUTOFF, ENV,
        // BRILLIANCE) through the S/H, plus the key's part, which moves per sample under GLIDE.
        Ir3109 vcf;
        Hold vcfCv, vcfRes;   // the tick's part of the cutoff (octaves), and the feedback
        float vcfKey = 0.f;   // KEY FOLLOW's part (octaves), set with the pitch
        float keyFollow = 0.f;// octaves of cutoff an octave of key, read on the tick
        float vcfAt = NAN;    // the CV the coefficient was last worked out for (not a number: none yet)
        VcfCoef vcfG;         // a stage's coefficient at that CV, and its powers
        bool vcfFresh = true; // the voice's first tick sets its CVs where they are going, with no slide
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
    double tune2 = 0.0;       // DCO-2's TUNE and FINE together, in semitones
    double clockSteps = kCounterClockHz / 48000.0; // the counter's clock in steps a sample
    float vcfOctaves = 0.f;   // CUTOFF and BRILLIANCE together, in octaves: what every voice's tick reads
    float vcfEnvOctaves = 0.f;// ENV MOD with its polarity: octaves at the envelope's top
    float resK = 0.f;         // RESONANCE as the feedback
    // THE LFO (jx3p/lfo.hpp): one for the synth, on a tick of its own counted from allNotesOff. The
    // voices read `lfo.out` on their ticks. What it reads of the panel, worked out in apply().
    Lfo lfo;
    double lfoStep = 0.0;     // cycles a tick (RATE)
    int lfoDelayTicks = 0;    // DELAY in ticks
    double dcoLfoSemis = 0.0; // LFO DEPTH: semitones at the LFO's top
    float vcfLfoOctaves = 0.f;// LFO MOD: octaves at the LFO's top
    // The HPF, after the voices are summed (one for the synth). Its corner through a hold of its own,
    // set when the knob moves. (Batch 4 gave the synth a tick, the LFO's; HPF was left as batch 3 built
    // it, read at once, so its tests stand: a knob a tick late is not worth a change to them.)
    HighPass hpf;
    Hold hpfCv;               // its corner in octaves over kHpfLowHz
    bool hpfOn = false;       // HPF over 0 (at 0, and once its corner has come down, it is not run)
    float hpfAt = NAN, hpfG = 0.f;
    // THE CHORUS (jx3p/chorus.hpp), after the HPF: its sweep runs from `clock`, as the LFO does.
    BbdChorus chorus;
    bool sounded = false;     // a sample made since the synth was made, set up or allNotesOff'd: until
                              // then a switch of the chorus lands at once (it starts where the settings are)
    std::uint64_t clock = 0;  // samples since allNotesOff (or since made)
    std::uint32_t keys = 0;   // note-ons since allNotesOff
    Voice voices[kVoices];
    Voice tails[kTailVoices]; // let go by allNotesOff, ringing out beside the six (kTailVoices)

    // What the tick sets of each DCO's pitch: RANGE, TUNE and FINE, and ENV, as the key's frequency
    // over the DCO's (so a glide, which moves the key every sample, works out one power of two, not two).
    void setTimes(Voice& v) {
        v.over1 = 1.0 / (rangeFactor(static_cast<int>(eff[Dco1Range])) * std::exp2(v.modSemis1 / 12.0));
        v.over2 = 1.0 / (rangeFactor(static_cast<int>(eff[Dco2Range])) * std::exp2((tune2 + v.modSemis2) / 12.0));
    }
    // A DCO's step a sample for a period of `period` seconds wanted: as its counter can make it, the
    // clock over a whole number of counts (kRoundToCounter), kept in `count`. One division, not three:
    // a glide works this out every sample.
    double stepOf(double period, double& count) const {
        double step;
        if (kRoundToCounter) {
            count = std::floor(kCounterClockHz * period + 0.5);
            if (count < 1.0) count = 1.0;
            step = clockSteps / count;
        } else {
            count = 1.0 / period;
            step = count / sampleRate;
        }
        return step > kHighestStep ? kHighestStep : step;
    }
    static double hzOf(double count) { return kRoundToCounter ? (count > 0.0 ? kCounterClockHz / count : 0.0) : count; }
    // Both DCOs' pitch: the key, a glide's offset (per sample), and what the tick set.
    void setPitch(Voice& v, double semitones) {
        const double period = std::exp2((69.0 - v.note - semitones) / 12.0) / 440.0; // the key's
        v.dco.one.dt = stepOf(period * v.over1, v.count1);
        v.dco.two.dt = stepOf(period * v.over2, v.count2);
        v.vcfKey = v.keyFollow * static_cast<float>((v.note + semitones - kKeyFollowNote) / 12.0); // the KCV: the key as it sounds, a glide too
    }
    // The waves, the cross mod and the mix as the panel has them: read on the tick.
    void readDcos(Voice& v, bool fresh) {
        const int wave1 = static_cast<int>(eff[Dco1Wave]), wave2 = static_cast<int>(eff[Dco2Wave]);
        if (fresh) v.dco.one.wave = wave1, v.dco.two.wave = wave2;
        else v.dco.one.setWave(wave1), v.dco.two.setWave(wave2);
        v.dco.cross = static_cast<int>(eff[Dco2CrossMod]);
        v.mix = eff[SourceMix] / 100.f;
    }
    // Samples from now to where a key is taken: now, or the next 10 ms of the one loop (kKeysOnTheSample).
    int keyWait() const { return kKeysOnTheSample ? 0 : static_cast<int>((tickLength - clock % static_cast<std::uint64_t>(tickLength)) % static_cast<std::uint64_t>(tickLength)); }

    // One tick of one voice: the CPU's loop. The envelope moves, the knobs are read, the CVs are written.
    void tick(Voice& v) {
        v.env.tick(times);
        // ENV and the LFO on the DCOs: stepped, as the real one writes the counters once a loop (SN p.6,
        // step 6). Each DCO's two switches say which reach it.
        const double env = v.env.level * eff[DcoEnvDepth] / 100.0 * kDcoEnvSemitones * (eff[DcoEnvPolarity] >= 0.5f ? -1.0 : 1.0);
        const double wobble = lfo.out * dcoLfoSemis;
        v.modSemis1 = (eff[Dco1Env] >= 0.5f ? env : 0.0) + (eff[Dco1Lfo] >= 0.5f ? wobble : 0.0);
        v.modSemis2 = (eff[Dco2Env] >= 0.5f ? env : 0.0) + (eff[Dco2Lfo] >= 0.5f ? wobble : 0.0);
        setTimes(v);
        readDcos(v, false);
        // The VCF's CVs, stepped and then held (SN p.6: "KCV, CUTOFF, LFO and ADSR").
        v.keyFollow = eff[KeyFollow] / static_cast<float>(kKeyFollowFull);
        v.vcfCv.target = vcfOctaves + v.env.level * vcfEnvOctaves + lfo.out * vcfLfoOctaves;
        v.vcfRes.target = resK;
        if (v.vcfFresh) v.vcfCv.value = v.vcfCv.target, v.vcfRes.value = v.vcfRes.target, v.vcfFresh = false;
        const float opened = eff[VcaMode] >= 0.5f ? (v.held ? 1.f : 0.f) : v.env.level;
        v.vca.target = levelGain * opened * (eff[Velocity] >= 0.5f ? velocityGain(v.velocity) : 1.f);
        if (v.slide.semitones == 0.f) setPitch(v, 0.0);
    }
    void renderVoice(Voice& v, float* out, int frames);
    // The LFO's tick: the panel's LFO knobs read, the shape worked out (jx3p/lfo.hpp).
    void tickLfo() { lfo.tick(static_cast<int>(eff[LfoWave]), lfoStep, lfoDelayTicks); }
    void runHpf(float* x, int frames);
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
    s.clockSteps = kCounterClockHz / s.sampleRate;
    s.chorus.prepare(s.sampleRate);
    s.sounded = false; // made or set up: the chorus is where its switch is, no fade
    apply(); // (the LFO's step a tick is the tick's own length in seconds, so it follows the rate)
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
    s.tune2 = tuneSemitones(s.eff[Dco2Tune], kTuneStepped, kTuneSemitonesPerDial) + fineSemitones(s.eff[Dco2Fine]);
    s.vcfOctaves = static_cast<float>(cutoffOctaves(s.eff[Cutoff]) + (s.eff[Brilliance] - 50.0) / 50.0 * kBrillianceOctaves);
    s.vcfEnvOctaves = static_cast<float>(s.eff[VcfEnv] / 100.0 * kVcfEnvOctaves * (s.eff[VcfEnvPolarity] >= 0.5f ? -1.0 : 1.0));
    s.resK = resFeedback(s.eff[Resonance]);
    s.lfoStep = lfoRateHz(s.eff[LfoRate]) * s.tickLength / s.sampleRate;
    s.lfoDelayTicks = lfoDelayTicks(s.eff[LfoDelay]);
    s.dcoLfoSemis = dcoLfoSemitones(s.eff[DcoLfoDepth]);
    s.vcfLfoOctaves = static_cast<float>(vcfLfoOctaves(s.eff[VcfLfo]));
    s.hpfCv.target = static_cast<float>(hpfOctaves(s.eff[Hpf]));
    if (s.eff[Hpf] > 0.f) s.hpfOn = true; // (turned to 0, it is let go once its corner has come down: runHpf)
    s.chorus.on = s.eff[Chorus] >= 0.5f;    // switched: it fades in or out over 20 ms...
    if (!s.sounded) s.chorus.snap();         // ...but before the first sample, it is there at once
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
    // THE LFO'S DELAY starts again on a key that goes down with no other key held (kDelayOnFirstKeyOnly):
    // a legato line does not start it again.
    bool another = false;
    for (const State::Voice& o : s.voices) another = another || o.held;
    if (!kDelayOnFirstKeyOnly || !another) s.lfo.startDelay(s.lfoDelayTicks);
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
        const std::uint32_t seed = mixBits(0x85EBCA6Bu * (s.keys + 1u));
        v.dco.one.start(static_cast<double>(seed >> 8) / 16777216.0);
        v.dco.two.start(static_cast<double>(mixBits(seed ^ 0x27D4EB2Fu) >> 8) / 16777216.0); // its own counter, as free as DCO-1's
        v.dco.noise.seed(mixBits(seed ^ 0x165667B1u));
        v.modSemis1 = v.modSemis2 = 0.0;
        s.setTimes(v);
        s.readDcos(v, true);
        v.vca = Hold();
        v.env.stop();
        v.vcf.reset();
        v.vcfFresh = true;
        v.vcfAt = NAN;
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
    v.vcf.kick(); // a key puts a fixed small number into stage 1, so RESONANCE 10 sings from silence too
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
    s.lfo.reset(); // its tick, phase, delay and RANDOM's count, from the start (the tails hear it so too)
    s.sounded = false;
    s.chorus.snap(); // a fade under way lands at once, as on a fresh synth. Its sweep starts again with
                     // `clock`; what is on its line (the tails' sound) stays and rings out
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
        const float source = v.dco.next(v.mix); // both DCOs, mixed (jx3p/dco.hpp)
        // The VCF: its coefficient worked out again only when its CV moved.
        const float cv = v.vcfCv.next(holdK, kVcfCvLands) + v.vcfKey;
        if (cv != v.vcfAt) {
            v.vcfAt = cv;
            v.vcfG.set(onePoleG(cornerHz(cv, kVcfLowHz, sampleRate), sampleRate));
        }
        const float k = v.vcfRes.next(holdK);
        const float filtered = v.vcf.run(kBassLoss ? source : source * (1.f + k), v.vcfG, k);
        const float gain = v.vca.next(holdK);
        out[i] += vcaClip(filtered * gain, clipFrom);
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
    s.sounded = s.sounded || frames > 0;
    const std::uint64_t start = s.clock; // the chorus's sweep: where this block starts
    // The block in pieces that end where the LFO ticks, so its value is one number through each piece
    // and a voice's tick reads it as of its own sample, however the sound is cut into blocks. The LFO
    // runs whether anything sounds or not: its place hangs on the time since allNotesOff alone.
    for (int done = 0; done < frames;) {
        if (s.lfo.toTick <= 0) {
            s.tickLfo();
            s.lfo.toTick += s.tickLength;
        }
        const int n = frames - done < s.lfo.toTick ? frames - done : s.lfo.toTick;
        for (State::Voice& v : s.voices)
            if (v.note >= 0) s.renderVoice(v, left + done, n), any = true;
        for (State::Voice& v : s.tails) // (added after the six: the six's sum is a fresh synth's, to the bit)
            if (v.note >= 0) s.renderVoice(v, left + done, n), any = true;
        s.clock += static_cast<std::uint64_t>(n);
        s.lfo.toTick -= n;
        done += n;
    }
    // The HPF (its own tail rings out after the voices). With no voice, nothing left in it and its
    // corner at rest, running it would change nothing: it is skipped.
    if (s.hpfOn && (any || s.hpf.s != 0.f || !s.hpfCv.still())) s.runHpf(left, frames), any = true;
    // THE CHORUS: run while it is on (or fading out), with sound coming in or its own still in it, or its
    // fade under way (so the fade moves on its samples however the blocks are cut). Off, or asleep with
    // nothing coming in, it is not run: off, the sound is the voices' own, mono, to the bit.
    const bool chorus = s.chorus.active() && (any || !s.chorus.idle() || s.chorus.fading());
    if (!any && !chorus) {
        for (int i = 0; i < frames; ++i) right[i] = 0.f;
        return;
    }
    if (chorus) {
        s.chorus.run(left, right, frames, start);
        for (int i = 0; i < frames; ++i) left[i] *= kOutputGain, right[i] *= kOutputGain;
    } else {
        for (int i = 0; i < frames; ++i) right[i] = left[i] = left[i] * kOutputGain;
    }
}

// THE HPF over the summed voices. Off (and not run, the sound untouched to the bit) once HPF is at 0
// and its corner has come down to the bottom: from that sample on, and what it still held (the
// sound's own lowest part, under 10 Hz) is let go. On silence its state runs down to 0 by itself (it
// is snapped there, kSnap), and then render() skips it.
void Jx3p::State::runHpf(float* x, int frames) {
    for (int i = 0; i < frames; ++i) {
        const float cv = hpfCv.next(holdK);
        if (eff[Hpf] <= 0.f && hpfCv.still()) {
            hpfOn = false;
            hpf.s = 0.f;
            return;
        }
        if (cv != hpfAt) {
            hpfAt = cv;
            hpfG = onePoleG(cornerHz(cv, kHpfLowHz, sampleRate), sampleRate);
        }
        x[i] = hpf.run(x[i], hpfG);
    }
}

int Jx3p::voiceNote(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].note : -1; }
float Jx3p::voiceEnv(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].env.level : 0.f; }
float Jx3p::voiceVca(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].vca.value : 0.f; }
double Jx3p::voiceHz(int voice) const { return voice >= 0 && voice < kVoices ? State::hzOf(state_->voices[voice].count1) : 0.0; }
double Jx3p::voiceHz2(int voice) const { return voice >= 0 && voice < kVoices ? State::hzOf(state_->voices[voice].count2) : 0.0; }
double Jx3p::voiceCutoffHz(int voice) const {
    const State::Voice* v = voice >= 0 && voice < kVoices ? &state_->voices[voice] : nullptr;
    return v && v->vcfAt == v->vcfAt ? cornerHz(v->vcfAt, kVcfLowHz, state_->sampleRate) : 0.0;
}
bool Jx3p::voiceHeld(int voice) const { return voice >= 0 && voice < kVoices && state_->voices[voice].held; }
float Jx3p::lfoNow() const { return state_->lfo.out; }
int Jx3p::tailsRinging() const {
    int n = 0;
    for (const State::Voice& v : state_->tails) n += v.note >= 0 ? 1 : 0;
    return n;
}

} // namespace apotheca
