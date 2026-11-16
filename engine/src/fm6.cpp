// FM-6: the voices, the assigner, the LFO and the macros, around msfa's note.
// See engine/include/apotheca/fm6.hpp and docs/plans/fm6-plan.md.

#include "apotheca/fm6.hpp"

#include <cmath>
#include <cstdint>

#include "apotheca/slide.hpp"
#include "fm6/roms.hpp"
#include "msfa/dx7note.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/lfo.h"
#include "msfa/pitchenv.h"
#include "msfa/sin.h"

namespace apotheca {

using namespace fm6;

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// msfa's tables are made for one sample rate, once, for every FM-6 (they are read-only after).
// The engine runs at 48 kHz; another rate is put right in pitch (State::rateFix) but not in time.
constexpr double kTableRate = 48000.0;
void makeTables() {
    static const bool made = [] {
        msfa::Freqlut::init(kTableRate);
        msfa::Exp2::init();
        msfa::Sin::init();
        msfa::Lfo::init(kTableRate);
        msfa::PitchEnv::init(kTableRate);
        msfa::Env::init_sr(kTableRate);
        return true;
    }();
    (void)made;
}

// The core's numbers to the engine's (a carrier at full is about 2^24 to 2^25 there). SELF: set so
// the bank sits with the other synths: four keys of each of the 32, the middle about -16 dB, as
// JAX-EP's (the plan, section 5).
constexpr float kOutputGain = 0.044f / 16777216.f;

// The keys there are: a note outside them is refused.
constexpr int kLowestKey = 0, kHighestKey = 127;

// A macro at 0.5 leaves the value; under it slides it down to 0, over it up to `top`.
double bent(double value, double macro, double top) {
    if (macro < 0.5) return value * (macro / 0.5);
    if (macro > 0.5) return value + (top - value) * ((macro - 0.5) / 0.5);
    return value;
}

int settingTop(int id) {
    if (id < PitchR1) {
        switch (id % kOpSettings) {
        case OpLeftCurve: case OpRightCurve: case OpAms: return 3;
        case OpRateScaling: case OpVelocity: return 7;
        case OpMode: return 1;
        case OpCoarse: return 31;
        case OpDetune: return 14;
        default: return 99;
        }
    }
    switch (id) {
    case Algorithm: return 31;
    case Feedback: case Pms: return 7;
    case KeySync: case LfoSync: return 1;
    case LfoWave: return 5;
    case Transpose: return 48;
    case OpsOn: return 63;
    default: return 99;
    }
}

} // namespace

namespace fm6 {
int settingMin(int) { return 0; }
int settingMax(int id) { return id >= 0 && id < kSettings ? settingTop(id) : 0; }
} // namespace fm6

struct Fm6::State {
    struct Voice {
        msfa::Dx7Note note;
        int key = -1;            // -1: free
        bool held = false;
        int toTick = 0;          // samples to the voice's next control step (0: on its next sample)
        Slide slide;             // GLIDE: how far it still is from its own key, in semitones
        std::uint64_t startedAt = 0, silentSince = 0; // the sample clock, for the assigner
    };
    Fm6Patch patch;
    char packed[156] = {};       // the patch as msfa's note reads it ([155]: the operators on)
    float macro[kMacros];
    msfa::NoteBend bend;         // the macros, as every voice reads them at its control step
    msfa::Lfo lfo;               // one for the synth, as the original
    int lfoToTick = 0;           // samples to its next step (every msfa::N, counted from allNotesOff)
    std::int32_t lfoVal = 1 << 23, lfoDelay = 0;
    float rate = 48000.f;        // the sample rate (GLIDE's speed)
    std::int32_t rateFix = 0;    // pitch put right for a sample rate the tables were not made for (Q24 octaves)
    std::uint64_t clock = 0;     // samples since allNotesOff (or since made)
    std::int32_t scratch[msfa::N];
    Voice voices[kVoices];
    Voice tails[kTailVoices];    // let go by allNotesOff, ringing out beside the 16

    // One voice's `frames` added into `out` (the voice's control steps where they fall). Set free,
    // and stops, on the control step that finds it let go and silent.
    void renderVoice(Voice& v, float* out, int frames) {
        for (int done = 0; done < frames;) {
            if (v.toTick <= 0) {
                const float semis = v.slide.take(msfa::N);
                const std::int32_t pitch = rateFix + static_cast<std::int32_t>(std::lround(semis / 12.0 * 16777216.0));
                v.note.control(lfoVal, lfoDelay, pitch, bend);
                v.toTick += msfa::N;
                if (!v.held && v.note.silent()) {
                    v.key = -1;
                    v.slide.stop();
                    v.silentSince = clock + static_cast<std::uint64_t>(done);
                    return;
                }
            }
            const int n = frames - done < v.toTick ? frames - done : v.toTick;
            for (int i = 0; i < n; ++i) scratch[i] = 0;
            v.note.render(scratch, msfa::N - v.toTick, n);
            for (int i = 0; i < n; ++i) {
                // Two statements, so no compiler joins the multiply and the add into one (ARM's fused
                // multiply-add rounds once, not twice): the same bits on the desk and on the handheld.
                const float y = static_cast<float>(scratch[i]) * kOutputGain;
                out[done + i] += y;
            }
            v.toTick -= n;
            done += n;
        }
    }
};

Fm6::Fm6() : state_(new State) {
    makeTables();
    for (float& m : state_->macro) m = 0.5f;
    state_->patch = startPatch();
    state_->lfo.start();
    prepare(48000.f);
}
Fm6::~Fm6() = default;

int Fm6::presetCount() { return kRomVoices; }
const char* Fm6::presetName(int i) { return kRom[clampi(i, 0, kRomVoices - 1)].name; }
const char* Fm6::presetCategory(int i) { return kRom[clampi(i, 0, kRomVoices - 1)].folder; } // the bank: "ROM 1A"
Fm6Patch Fm6::presetPatch(int i) {
    const RomVoice& rom = kRom[clampi(i, 0, kRomVoices - 1)];
    Fm6Patch patch;
    for (int id = 0; id < Transpose + 1; ++id) patch.value[id] = clampi(rom.value[id], 0, settingTop(id));
    patch.value[OpsOn] = 63;
    return patch;
}
// The original's own start ("INIT VOICE"): algorithm 1, OP1 alone at full, every operator on a
// ratio of 1, every envelope straight up and held, nothing else moving: a plain sine at the key.
Fm6Patch Fm6::startPatch() {
    Fm6Patch patch;
    for (int op = 1; op <= 6; ++op) {
        for (int r = OpR1; r <= OpR4; ++r) patch.value[opSetting(op, r)] = 99;
        for (int l = OpL1; l <= OpL3; ++l) patch.value[opSetting(op, l)] = 99;
        patch.value[opSetting(op, OpL4)] = 0;
        patch.value[opSetting(op, OpBreakPoint)] = 39;
        patch.value[opSetting(op, OpLevel)] = op == 1 ? 99 : 0;
        patch.value[opSetting(op, OpCoarse)] = 1;
        patch.value[opSetting(op, OpDetune)] = 7;
    }
    for (int i = 0; i < 4; ++i) patch.value[PitchR1 + i] = 99, patch.value[PitchL1 + i] = 50;
    patch.value[KeySync] = 1;
    patch.value[LfoSpeed] = 35;
    patch.value[LfoSync] = 1;
    patch.value[Pms] = 3;
    patch.value[Transpose] = 24;
    patch.value[OpsOn] = 63;
    return patch;
}

void Fm6::prepare(float sampleRate) {
    State& s = *state_;
    const double rate = sampleRate > 0.f ? sampleRate : kTableRate;
    s.rate = static_cast<float>(rate);
    s.rateFix = static_cast<std::int32_t>(std::lround(std::log2(kTableRate / rate) * 16777216.0));
    apply();
}

// The patch packed for msfa's note, and the macros worked out as every voice's bend. A key played
// after this hears the patch; the bend is read by every voice at its next control step.
void Fm6::apply() {
    State& s = *state_;
    ++applied_;
    for (int id = 0; id <= Transpose; ++id) s.packed[id] = static_cast<char>(s.patch.value[id]);
    s.packed[155] = static_cast<char>(s.patch.value[OpsOn]);
    s.lfo.reset(s.packed + LfoSpeed);
    const float* m = s.macro;
    msfa::NoteBend& b = s.bend;
    // BRIGHT: up by kBrightMostDb at the top; down in doublings by the log of the knob, to none.
    const double doublingsUp = kBrightMostDb / (20.0 * std::log10(2.0));
    double bright = m[MacroBright] >= 0.5f ? (m[MacroBright] - 0.5) / 0.5 * doublingsUp
                                           : (m[MacroBright] > 0.f ? doublingsUp * std::log2(2.0 * m[MacroBright]) : -16.0);
    if (bright < -16.0) bright = -16.0;
    b.mod_level = static_cast<std::int32_t>(std::lround(bright * 16777216.0));
    // FEEDBACK: smooth between its steps; under 1 a straight line down to none.
    const double fb = bent(s.patch.value[Feedback], m[MacroFeedback], 7.0);
    b.fb_gain = fb <= 0.0 ? 0 : static_cast<std::int32_t>(std::lround(fb < 1.0 ? fb * 16777216.0 : std::exp2(23.0 + fb)));
    // ATTACK, DECAY, RELEASE: up is slower (a rate taken down), down faster.
    auto rateBend = [](float macro) { return static_cast<int>(std::lround(-(macro - 0.5) / 0.5 * kRateBendMost)); };
    b.rate_bend[0] = rateBend(m[MacroAttack]);
    b.rate_bend[1] = b.rate_bend[2] = rateBend(m[MacroDecay]);
    b.rate_bend[3] = rateBend(m[MacroRelease]);
    // DETUNE: under the middle the patch's own detune goes, over it the operators spread.
    if (m[MacroDetune] <= 0.5f) {
        b.detune_keep = static_cast<std::int32_t>(std::lround(m[MacroDetune] / 0.5 * 256.0));
        b.detune_spread = 0;
    } else {
        b.detune_keep = 256;
        b.detune_spread = static_cast<std::int32_t>(std::lround((m[MacroDetune] - 0.5) / 0.5 * kDetuneMostCents / 1200.0 * 16777216.0));
    }
    b.peg_scale = static_cast<std::int32_t>(std::lround(bent(256.0, m[MacroPitchEnv], 512.0)));
    b.pmd = static_cast<int>(std::lround(bent(s.patch.value[LfoPmd], m[MacroLfo], 99.0)));
    b.amd = static_cast<int>(std::lround(bent(s.patch.value[LfoAmd], m[MacroLfo], 99.0)));
}

void Fm6::selectPreset(int index) {
    if (index < 0 || index >= kRomVoices) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    apply();
}

void Fm6::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = clampi(value, 0, settingTop(id));
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    apply();
}
int Fm6::setting(int id) const { return id >= 0 && id < kSettings ? state_->patch.value[id] : 0; }

void Fm6::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return; // (a MOD gives the same value often: nothing to work out)
    state_->macro[macro] = value;
    apply();
}

// THE ASSIGNER: the same key if it is sounding, else the free voice longest silent, else the oldest
// voice whose key is up (ringing in its release), else the oldest of all (a steal).
int Fm6::noteOnVoice(int note, float velocity) {
    State& s = *state_;
    bool another = false;
    for (const State::Voice& o : s.voices) another = another || o.held;
    if (!another) s.lfo.keydown(); // the LFO's delay (and its sync) start again on a key with none held
    int chosen = -1;
    for (int i = 0; i < kVoices && chosen < 0; ++i)
        if (s.voices[i].key == note) chosen = i;
    for (int i = 0; i < kVoices && chosen < 0; ++i)
        if (s.voices[i].key < 0) {
            chosen = i;
            for (int j = i + 1; j < kVoices; ++j)
                if (s.voices[j].key < 0 && s.voices[j].silentSince < s.voices[chosen].silentSince) chosen = j;
        }
    for (int pass = 0; pass < 2 && chosen < 0; ++pass)
        for (int i = 0; i < kVoices; ++i)
            if ((pass == 1 || !s.voices[i].held) && (chosen < 0 || s.voices[i].startedAt < s.voices[chosen].startedAt)) chosen = i;
    State::Voice& v = s.voices[chosen];
    // A sounding voice carries on from where it is (its levels, and its waves unless KEY SYNC):
    // no click. A free one starts from silence.
    const bool retrigger = v.key >= 0;
    const float vel = velocity > 0.f ? (velocity < 1.f ? velocity : 1.f) : 0.f; // not a number: 0
    v.note.init(s.packed, note, static_cast<int>(std::lround(vel * 127.f)), retrigger);
    v.key = note;
    v.held = true;
    v.slide.stop();
    v.toTick = 0; // its first control step on this very sample
    v.startedAt = s.clock;
    return chosen;
}

void Fm6::noteOn(int note, float velocity) {
    if (note < kLowestKey || note > kHighestKey) return;
    noteOnVoice(note, velocity);
}

void Fm6::noteOnFrom(int note, float velocity, int from, float seconds) {
    if (note < kLowestKey || note > kHighestKey) return;
    State::Voice& v = state_->voices[noteOnVoice(note, velocity)];
    v.slide.start(from, note, seconds, state_->rate);
}

void Fm6::noteOff(int note) {
    for (State::Voice& v : state_->voices)
        if (v.key == note && v.held) {
            v.held = false;
            v.note.keyup();
        }
}

// Play, Stop, panic: every key let go, and the LFO and the clock from the start, so Play from the
// top is the same performance. A voice still sounding is not cut: it is handed to a tail, rings
// out there, and its place among the 16 starts again as from fresh.
void Fm6::allNotesOff() {
    State& s = *state_;
    for (State::Voice& v : s.voices) {
        if (v.held) {
            v.held = false;
            v.note.keyup();
        }
        if (v.key >= 0)
            for (State::Voice& t : s.tails)
                if (t.key < 0) {
                    t = v;
                    v = State::Voice();
                    break;
                }
        v.startedAt = v.silentSince = 0;
    }
    s.clock = 0;
    s.lfo.start();
    s.lfoToTick = 0;
}

void Fm6::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    bool any = false;
    // The block in pieces that end where the LFO steps, so every voice's control step reads it as of
    // its own sample, however the sound is cut. The LFO runs whether anything sounds or not.
    for (int done = 0; done < frames;) {
        if (s.lfoToTick <= 0) {
            s.lfoVal = s.lfo.getsample();
            s.lfoDelay = s.lfo.getdelay();
            s.lfoToTick += msfa::N;
        }
        const int n = frames - done < s.lfoToTick ? frames - done : s.lfoToTick;
        for (State::Voice& v : s.voices)
            if (v.key >= 0) s.renderVoice(v, left + done, n), any = true;
        for (State::Voice& v : s.tails)
            if (v.key >= 0) s.renderVoice(v, left + done, n), any = true;
        s.clock += static_cast<std::uint64_t>(n);
        s.lfoToTick -= n;
        done += n;
    }
    (void)any;
    for (int i = 0; i < frames; ++i) right[i] = left[i];
}

int Fm6::voiceNote(int voice) const { return voice >= 0 && voice < kVoices ? state_->voices[voice].key : -1; }
bool Fm6::voiceHeld(int voice) const { return voice >= 0 && voice < kVoices && state_->voices[voice].held; }
int Fm6::voicesSounding() const {
    int n = 0;
    for (const State::Voice& v : state_->voices) n += v.key >= 0 ? 1 : 0;
    return n;
}
int Fm6::tailsRinging() const {
    int n = 0;
    for (const State::Voice& v : state_->tails) n += v.key >= 0 ? 1 : 0;
    return n;
}

} // namespace apotheca
