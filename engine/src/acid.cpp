// ACID: the voice (docs/plans/acid-plan.md, batches 1 and 2). The rules are acid_rule.hpp's, the
// filter engine/src/acid/ladder.hpp's.

// (As PHYSIC: no multiply and add joined, so the handheld's sums are the desk's.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "apotheca/acid.hpp"

#include <cmath>
#include <cstring>

#include "acid/ladder.hpp" // (beside this file)

namespace apotheca {

using namespace acid;

namespace {

constexpr double kAttackSeconds = 0.003;     // SELF: the amp's rise at a strike (from where it is)
constexpr double kGateSeconds = 0.003;       // SELF: the gate shuts (and opens again on a tie) over this
constexpr double kLevelGlideSeconds = 0.003; // SELF: a level changed on a sounding note glides this long
constexpr double kTailSeconds = 0.003;       // allNotesOff: the note fades out in the tail this long
constexpr int kTuneTick = 16;                // SELF: the cutoff is worked out every this many samples, counted from the strike
constexpr double kSquareSag = 0.25;          // SELF: the square sags through a one-pole high-pass at this share of its pitch
constexpr float kSweepQuiet = 1e-5f;         // the accent sweep under this (of its one-accent height) is gone
constexpr float kOutputGain = 0.5f;          // SELF: the synth's output at LEVEL 0 dB
constexpr float kClipKnee = 1.0f;            // the output is bent softly from here...
constexpr float kClipCeiling = 1.5f;         // ...towards this, and the samples bent are counted

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The presets the engine knows (the plan's section 5: the 32 come as data in batch 4, after these).
struct Preset {
    const char* name;
    const char* category;
    int value[kSettings];
};
// WAVE OCT TUNE CUT RES ENV DECAY ACC SWEEP DRIVE AMP SLIDE VEL LEVEL
const Preset kPresets[] = {
    {"Classic Saw", "CLASSIC", {kSaw, 0, 0, 30, 60, 50, 40, 50, 50, 0, 80, kSlideNote, 0, 0}},
    {"Classic Square", "CLASSIC", {kSquare, 0, 0, 35, 65, 55, 45, 50, 50, 0, 80, kSlideNote, 0, 0}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

// The bend shared by DRIVE: x (27 + x^2) / (27 + 9 x^2), no flat top.
inline float bend(float x) {
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
const double kSlideEnd = std::exp(-kSlideTaus);

// The settings as they act now (the patch with its macros), worked out when one is turned.
struct Eff {
    float baseOct = 0.f, envOct = 0.f, accentOct = 0.f;
    float plainFall = 1.f, accentFall = 1.f, ampFall = 1.f, capK = 1.f;
    float k = 0.f, makeup = 1.f, accentDial = 0.f, accentGain = 1.f;
    bool driveOn = false;
    float drive = 1.f, driveBack = 1.f; // the gain in, and one over the bend at a full wave (so DRIVE keeps the level)
    int wave = kSaw, slide = kSlideNote;
    bool velOn = false;
    double tuneOct = 0.0;
    float gain = kOutputGain;
    float topHz = 20000.f;
    float attackStep = 0.f, gateStep = 0.f;
    int levelGlide = 1, tailSamples = 144;
};

struct Voice {
    bool asleep = true;
    int key = -1;
    bool gate = false;
    long shut = 0;                 // samples since the gate shut
    double noteOct = 0.0;          // the key in octaves over A4
    double slideSpan = 0.0, slideR = 1.0, slideK = 1.0; // the slide: how far, the RC's place and step
    long slideN = 0;               // samples of it left
    double lastPitch = 1e9, phase = 0.0, dt = 0.0, invDt = 0.0;
    float sag = 0.f;
    Ladder f;
    LadderCoef c;
    float lastHz = -1.f;
    int tuneIn = 0;                // samples to the cutoff's next working out (0: this one)
    float env = 0.f;               // the filter's decay
    bool accent = false;           // the note struck was accented (the sweep is fed and the decay is the shortest)
    float sweep = 0.f;             // the accent sweep's capacitor
    float amp = 0.f;
    bool attack = false;
    float gateLevel = 0.f;         // the gate: 1 open, 0 shut, a 3 ms ramp between
    float level = 0.f, levelTo = 0.f, levelStep = 0.f;
    int levelLeft = 0;
    int fadeLeft = 0;              // the tail's fade: samples of it left
};

} // namespace

struct Acid::State {
    float sampleRate = 48000.f;
    AcidPatch patch;
    float macro[kMacros];
    bool dirty = true;
    Eff eff;
    Edge edge;
    Voice voice, tail;
    long tunings = 0, clipped = 0, strikes = 0, ties = 0;
    int tuneTick = kTuneTick;
    double stepSeconds = 0.0; // the song's step (noteStep): the tie's window...
    long tieWindow = 240;     // ...in samples (a voice sleeps only after it)

    float bent(int m) const {
        const int id = macroSetting(m);
        return macroBent(static_cast<float>(patch.value[id]), macro[m], static_cast<float>(settingMin(id)), static_cast<float>(settingMax(id)));
    }
    void refresh() {
        dirty = false;
        const double rate = sampleRate;
        Eff& e = eff;
        const float cutoff = bent(MacroCutoff), res = bent(MacroResonance), env = bent(MacroEnvMod), decay = bent(MacroDecay);
        const float accent = bent(MacroAccent), drive = bent(MacroDrive), sweep = bent(MacroSweep), tune = bent(MacroTune);
        e.baseOct = static_cast<float>(cutoffOctaves(cutoff));
        e.envOct = static_cast<float>(envOctaves(env));
        e.plainFall = static_cast<float>(fallStep(decaySeconds(decay), rate));
        e.accentFall = static_cast<float>(fallStep(accentDecaySeconds(), rate));
        e.ampFall = static_cast<float>(fallStep(ampDecaySeconds(patch.value[AmpDecay]), rate));
        e.k = static_cast<float>(resShare(res) * edge.k);
        e.makeup = 1.f + static_cast<float>(kMakeUp) * e.k;
        e.accentDial = accent;
        e.accentGain = static_cast<float>(accentGain(accent));
        const double capTau = sweepSeconds(sweep, res);
        e.capK = capTau > 0.0 ? static_cast<float>(1.0 - std::exp(-1.0 / (capTau * rate))) : 1.f;
        const double envTau = accentDecaySeconds() / std::log(1.0 / kDecayFallsTo);
        e.accentOct = static_cast<float>(kAccentOctaves * accent / 100.0 / sweepPeak(envTau, capTau));
        e.driveOn = drive > 0.f;
        e.drive = static_cast<float>(driveGain(drive));
        e.driveBack = 1.f / bend(e.drive);
        e.wave = patch.value[Wave];
        e.slide = patch.value[Slide];
        e.velOn = patch.value[Velocity] != 0;
        e.tuneOct = patch.value[Octave] + tune / 1200.0;
        e.gain = static_cast<float>(std::pow(10.0, levelDb(patch.value[Level]) / 20.0)) * kOutputGain;
        e.topHz = static_cast<float>(kTopOfRate * rate < kTopHz ? kTopOfRate * rate : kTopHz);
        e.attackStep = static_cast<float>(1.0 / (kAttackSeconds * rate));
        e.gateStep = static_cast<float>(1.0 / (kGateSeconds * rate));
        e.tailSamples = static_cast<int>(std::ceil(kTailSeconds * rate));
        e.levelGlide = static_cast<int>(std::ceil(kLevelGlideSeconds * rate));
        tieWindow = static_cast<long>(std::ceil(tieSeconds(stepSeconds) * rate));
    }
    void levelTo(Voice& v, float to, bool glide) {
        v.levelTo = to;
        if (!glide) {
            v.level = to, v.levelLeft = 0;
            return;
        }
        v.levelLeft = eff.levelGlide;
        v.levelStep = (to - v.level) / static_cast<float>(v.levelLeft);
    }

    // One sample of a voice.
    float tick(Voice& v) {
        const Eff& e = eff;
        // The pitch: the key, OCTAVE and TUNE, and what is left of a slide.
        double p = v.noteOct + e.tuneOct;
        if (v.slideN > 0) {
            p += v.slideSpan * ((v.slideR - kSlideEnd) / (1.0 - kSlideEnd));
            v.slideR *= v.slideK;
            --v.slideN; // at 0 it has landed: the key exactly (the span is no longer added)
        }
        if (p != v.lastPitch) {
            v.lastPitch = p;
            v.dt = 440.0 * jx3p::exp2Poly(p) / sampleRate;
            if (v.dt > 0.45) v.dt = 0.45;
            v.invDt = 1.0 / v.dt;
        }
        // The oscillator: a saw, or a square, each with its jumps made smooth (polyBLEP).
        const double t = v.phase, dt = v.dt;
        float o;
        if (e.wave == kSaw) {
            o = static_cast<float>(2.0 * t - 1.0 - blep(t, dt, v.invDt));
        } else {
            const double t2 = t < 0.5 ? t + 0.5 : t - 0.5;
            const float sq = static_cast<float>((t < 0.5 ? 1.0 : -1.0) + blep(t, dt, v.invDt) - blep(t2, dt, v.invDt));
            v.sag = snap(v.sag + static_cast<float>(kSquareSag * 2.0 * 3.14159265358979323846 * dt) * (sq - v.sag));
            o = sq - v.sag;
        }
        v.phase = t + dt;
        if (v.phase >= 1.0) v.phase -= 1.0;
        if (e.driveOn) o = bend(o * e.drive) * e.driveBack;
        // The two decays, the accent sweep, the gate and the level.
        v.env = snap(v.env * (v.accent ? e.accentFall : e.plainFall));
        v.sweep = snap(v.sweep + e.capK * ((v.accent ? v.env : 0.f) - v.sweep));
        if (v.attack) {
            v.amp += e.attackStep;
            if (v.amp >= 1.f) v.amp = 1.f, v.attack = false;
        } else {
            v.amp = snap(v.amp * e.ampFall);
        }
        if (v.gate) {
            v.gateLevel = v.gateLevel + e.gateStep < 1.f ? v.gateLevel + e.gateStep : 1.f;
        } else {
            v.gateLevel = v.gateLevel - e.gateStep > 0.f ? v.gateLevel - e.gateStep : 0.f;
            ++v.shut;
        }
        if (v.levelLeft > 0) {
            v.level += v.levelStep;
            if (--v.levelLeft == 0) v.level = v.levelTo;
        }
        // The cutoff: CUTOFF, the filter's decay times ENV MOD, the sweep times ACCENT.
        // (on the tick: a strike works it out on its first sample)
        if (--v.tuneIn <= 0) {
            v.tuneIn = tuneTick;
            float hz = static_cast<float>(kCutoffLowHz * jx3p::exp2Poly(e.baseOct + e.envOct * v.env + e.accentOct * v.sweep));
            if (hz > e.topHz) hz = e.topHz;
            if (hz != v.lastHz) {
                v.lastHz = hz;
                v.c.set(hz, sampleRate, edge.w180);
                ++tunings;
            }
        }
        const float y = v.f.run(o, v.c, e.k);
        return y * (e.makeup * v.amp * v.gateLevel * v.level * e.gain);
    }
    static double blep(double t, double dt, double invDt) {
        if (t < dt) {
            const double x = t * invDt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt) {
            const double x = (t - 1.0) * invDt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }
    static float snap(float v) { return std::fabs(v) < kSnap ? 0.f : v; }
    bool sleeps(const Voice& v) const { return !v.gate && v.gateLevel == 0.f && v.shut >= tieWindow && v.sweep < kSweepQuiet; }

    float clip(float y) {
        const float a = std::fabs(y);
        if (!(a > kClipKnee)) return y;
        ++clipped;
        const float over = 2.f * (a - kClipKnee) / (kClipCeiling - kClipKnee);
        const float bentA = kClipKnee + (kClipCeiling - kClipKnee) * 0.5f * over / (1.f + 0.5f * over);
        return y < 0.f ? -bentA : bentA;
    }
};

Acid::Acid() : state_(new State) {
    static const Edge edge = ladderEdge(); // (worked out once, off the audio thread)
    state_->edge = edge;
    for (float& m : state_->macro) m = 0.5f;
    state_->patch = presetPatch(0);
    state_->refresh();
}
Acid::~Acid() = default;

int Acid::presetCount() { return kPresetCount; }
const char* Acid::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Acid::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
AcidPatch Acid::presetPatch(int i) {
    AcidPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kSettings; ++id) patch.value[id] = settingHeld(id, preset.value[id]);
    return patch;
}

void Acid::prepare(float sampleRate) {
    State& s = *state_;
    if (!(sampleRate > 1000.f)) return;
    s.sampleRate = sampleRate;
    s.voice = Voice();
    s.tail = Voice();
    s.refresh();
}

void Acid::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    state_->dirty = true;
}

void Acid::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = settingHeld(id, value);
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    state_->dirty = true;
}

void Acid::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return;
    state_->macro[macro] = value;
    state_->dirty = true;
}

void Acid::noteOn(int note, float velocity) { noteOnFrom(note, velocity, -1, 0.f); }

void Acid::noteOnFrom(int note, float velocity, int from, float seconds) {
    State& s = *state_;
    if (note < 0 || note > 127 || !(velocity == velocity)) return;
    if (s.dirty) s.refresh();
    const Eff& e = s.eff;
    velocity = clampf(velocity, 0.f, 1.f);
    Voice& v = s.voice;
    // (from == note: a GLIDE to the same key, a tie with nothing to slide)
    const bool glide = from >= 0 && from <= 127 && seconds > 0.f && seconds < 1e6f;
    const bool tie = !v.asleep && slides(glide, v.gate, static_cast<double>(v.shut) / s.sampleRate, s.stepSeconds);
    const bool accent = accented(velocity, e.accentDial);
    v.asleep = false;
    v.key = note;
    v.gate = true;
    v.shut = 0;
    v.noteOct = (note - 69) / 12.0;
    v.slideN = 0, v.slideSpan = 0.0;
    if (glide) { // (to the same key the span is 0: no slide)
        const double time = slideSeconds(e.slide, seconds);
        const long n = slideSamples(time, s.sampleRate);
        if (n >= 1) {
            v.slideSpan = (from - note) / 12.0;
            v.slideR = 1.0;
            v.slideK = slideStep(time, s.sampleRate);
            v.slideN = n;
        }
    }
    const float level = (e.velOn ? velocity : 1.f) * (accent ? e.accentGain : 1.f);
    if (tie) {
        // A slide: no envelope starts again, and the sweep is not fed again; the level glides.
        s.levelTo(v, level, true);
        ++s.ties;
        return;
    }
    s.levelTo(v, level, v.amp != 0.f);
    v.env = 1.f;
    v.accent = accent;
    v.attack = true;
    v.tuneIn = 0;
    ++s.strikes;
}

void Acid::noteStep(double seconds) {
    State& s = *state_;
    if (!(seconds > 0.0 && seconds < 60.0) || seconds == s.stepSeconds) return;
    s.stepSeconds = seconds;
    s.tieWindow = static_cast<long>(std::ceil(tieSeconds(seconds) * s.sampleRate));
}

void Acid::noteOff(int note) {
    Voice& v = state_->voice;
    if (v.asleep || !v.gate || v.key != note) return; // (a key not sounding: nothing)
    v.gate = false;
    v.shut = 0;
}

void Acid::allNotesOff() {
    State& s = *state_;
    if (!s.voice.asleep) {
        s.tail = s.voice;
        s.tail.fadeLeft = s.eff.tailSamples;
    }
    s.voice = Voice();
}

void Acid::render(float* left, float* right, int frames) {
    State& s = *state_;
    if (s.dirty) s.refresh();
    for (int i = 0; i < frames; ++i) {
        float y = 0.f;
        if (!s.voice.asleep) {
            y = s.tick(s.voice);
            if (s.sleeps(s.voice)) s.voice = Voice();
        }
        if (!s.tail.asleep) {
            y += s.tick(s.tail) * (static_cast<float>(s.tail.fadeLeft) / static_cast<float>(s.eff.tailSamples));
            if (--s.tail.fadeLeft <= 0) s.tail = Voice();
        }
        y = s.clip(y);
        left[i] = y;
        right[i] = y;
    }
}

void Acid::setTuneTickForTests(int samples) { state_->tuneTick = samples < 1 ? 1 : samples; }
bool Acid::awake() const { return !state_->voice.asleep; }
bool Acid::tailAwake() const { return !state_->tail.asleep; }
long Acid::tunings() const { return state_->tunings; }
long Acid::samplesClipped() const { return state_->clipped; }
int Acid::key() const { return state_->voice.asleep ? -1 : state_->voice.key; }
bool Acid::gateOpen() const { return !state_->voice.asleep && state_->voice.gate; }
long Acid::strikes() const { return state_->strikes; }
long Acid::ties() const { return state_->ties; }

Acid::Now Acid::now() const {
    const State& s = *state_;
    const Voice& v = s.voice;
    Now n;
    n.filterEnv = v.env, n.amp = v.amp, n.sweep = v.sweep, n.level = v.level;
    n.pitchOctaves = v.lastPitch < 1e8 ? v.lastPitch : v.noteOct + s.eff.tuneOct;
    n.cutoffHz = v.lastHz;
    return n;
}

float Acid::smallestState() const {
    float least = 0.f;
    const auto look = [&least](float x) {
        const float a = std::fabs(x);
        if (a > 0.f && (least == 0.f || a < least)) least = a;
    };
    for (const Voice* v : {&state_->voice, &state_->tail}) {
        for (float x : v->f.s) look(x);
        for (float x : v->f.z) look(x);
        look(v->f.h), look(v->env), look(v->sweep), look(v->amp), look(v->sag);
    }
    return least;
}

} // namespace apotheca
