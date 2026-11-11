#include "apotheca/twin.hpp"

#include <cmath>
#include <vector>

namespace apotheca {

using namespace twin;

namespace {

// ---- the oscillator maths, as in Shapetaker's src/dsp/oscillators.hpp ----------

constexpr float kMiddleCHz = 261.626f;
constexpr float kFormantRatios[8] = {0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 5.f, 7.f};
constexpr float kPhaseNoiseScale = 0.00005f;
constexpr float kStereoSwapWidthGain = 0.35f;
constexpr float kVoicePitchOct = 0.0025f, kVoiceShape = 0.025f, kVoiceLevel = 0.035f, kVoicePan = 0.025f;
constexpr float kBusDrive = 0.25f, kBusCrosstalk = 0.025f;
constexpr float kBaseDriftSpeed = 0.00002f, kDriftLimit = 0.001f;
constexpr float kNoiseShapeExp = 0.65f, kNoisePeak = 0.09f;
constexpr float kVoiceGain = 0.3f; // one voice at full level, going into the bus colour
constexpr int kMaxOversample = 8;
constexpr int kBlock = 64;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

float softenShapeEdges(float shape) {
    constexpr float knee = 0.02f;
    if (shape < knee) {
        const float t = shape / knee;
        return t <= 0.f ? 0.f : (-t * t * t + 2.f * t * t) * knee;
    }
    if (shape > 1.f - knee) {
        const float t = (1.f - shape) / knee;
        return t <= 0.f ? 1.f : 1.f - (-t * t * t + 2.f * t * t) * knee;
    }
    return shape;
}

inline float fastTanh(float x) {
    const float x2 = x * x;
    if (x2 > 25.f) return x > 0.f ? 1.f : -1.f;
    const float a = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
    const float b = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    const float y = a / b;
    return y < -1.f ? -1.f : (y > 1.f ? 1.f : y);
}

// Note the sign: this is -sin(2 pi phase), as in the module. Every use below
// is a modulation shape, where that only sets which way it starts.
inline float fastSin2Pi(float phase) {
    const std::int32_t whole = static_cast<std::int32_t>(phase);
    float frac = phase - static_cast<float>(whole);
    if (frac < 0.f) frac += 1.f;
    const float x = frac * 2.f - 1.f;
    const float y = 4.f * x * (1.f - (x < 0.f ? -x : x));
    return y * (0.225f * ((y < 0.f ? -y : y) - 1.f) + 1.f);
}

// A saw that morphs into a sigmoid (an S-curve) with a movable centre.
inline float voicedSigmoidSaw(float phase, float blend, float range, float center, float lowShape, float airGain, float season) {
    const float linearSaw = 2.f * phase - 1.f;
    const float baseSaw = linearSaw * (1.f - 0.245f * linearSaw * linearSaw);
    float sigmoidInput = (phase - center) * range * 2.f;
    sigmoidInput += fastSin2Pi(phase * 3.f) * 0.03f * season;
    float result = linearSaw * (1.f - blend) + fastTanh(sigmoidInput) * blend;
    if (airGain > 0.f) result += fastSin2Pi(phase * 7.f) * 0.008f * season * airGain;
    const float shaped = fastTanh(result * 1.05f) * 0.95f;
    return baseSaw + (shaped - baseSaw) * lowShape;
}

// ---- the same maths without branches ---------------------------------------------
// Each oscillator step works out four waves: V's two copies and Z's two. Written
// as a loop of four with no if-statements inside, the compiler can do all four
// in one go on processors that have four-wide arithmetic (the handheld's does).
// The results match the functions above.

inline float tanhNoBranch(float x) {
    x = x < -5.f ? -5.f : (x > 5.f ? 5.f : x);
    const float x2 = x * x;
    const float a = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
    const float b = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    const float y = a / b;
    return y < -1.f ? -1.f : (y > 1.f ? 1.f : y);
}

// For phases of zero or more only.
inline float sinNoBranch(float phase) {
    const float frac = phase - static_cast<float>(static_cast<std::int32_t>(phase));
    const float x = frac * 2.f - 1.f;
    const float y = 4.f * x * (1.f - std::fabs(x));
    return y * (0.225f * (std::fabs(y) - 1.f) + 1.f);
}

inline float sigmoidSawNoBranch(float phase, float blend, float range, float center, float lowShape, float airGain, float season) {
    const float linearSaw = 2.f * phase - 1.f;
    const float baseSaw = linearSaw * (1.f - 0.245f * linearSaw * linearSaw);
    const float sigmoidInput = (phase - center) * range * 2.f + sinNoBranch(phase * 3.f) * 0.03f * season;
    const float result = linearSaw * (1.f - blend) + tanhNoBranch(sigmoidInput) * blend + sinNoBranch(phase * 7.f) * 0.008f * season * airGain;
    const float shaped = tanhNoBranch(result * 1.05f) * 0.95f;
    return baseSaw + (shaped - baseSaw) * lowShape;
}

inline float wrapBlepNoBranch(float phase, float dt) {
    const float t1 = phase / dt, t2 = (phase - 1.f) / dt;
    const float after = phase < dt ? t1 + t1 - t1 * t1 - 1.f : 0.f;
    const float before = phase > 1.f - dt ? t2 + t2 + t2 * t2 + 1.f : 0.f;
    return phase < dt ? after : before;
}

inline float polyBLEP(float t) {
    if (t >= 0.f && t < 1.f) return t + t - t * t - 1.f;
    if (t > -1.f && t < 0.f) return t + t + t * t + 1.f;
    return 0.f;
}

inline float pwmWithPolyBLEP(float phase, float width, float dt) {
    float out = phase < width ? 1.f : -1.f;
    if (phase < dt) out += polyBLEP(phase / dt);
    if (phase > 1.f - dt) out += polyBLEP((phase - 1.f) / dt);
    if (phase > width && phase < width + dt) out -= polyBLEP((phase - width) / dt);
    if (phase > width - dt && phase < width) out -= polyBLEP((phase - width) / dt);
    return out;
}

inline float phaseWrapBlep(float phase, float dt) {
    if (dt <= 0.f) return 0.f;
    if (phase < dt) return polyBLEP(phase / dt);
    if (phase > 1.f - dt) return polyBLEP((phase - 1.f) / dt);
    return 0.f;
}

inline float skewedSin2Pi(float phase, float k) {
    const float p = phase - std::floor(phase);
    const float w = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
    return fastSin2Pi(w);
}

inline void formantMod(float rangeBase, float wob, float asymWob, float baseAsym, float depth, float stereoScale,
                       float& range, float& center) {
    float scale = 1.f + 3.f * depth * wob;
    scale = (scale > 0.12f ? scale : 0.12f) * stereoScale;
    range = rangeBase * scale;
    center = 0.5f + 0.22f * clampf(baseAsym + 0.7f * depth * asymWob, -0.9f, 0.9f);
}

float formantDepthCurve(float depth) {
    const float d = clampf(depth, 0.f, 1.f);
    return d * std::sqrt(d);
}
float formantAsymCurve(float asym) { return 0.9f * std::sqrt(clampf(asym, 0.f, 1.f)); }

// Everything the WIDTH control drives.
struct WidthCoeffs {
    float width = 0.5f, phase = 0.25f, scaleA = 1.14f, scaleB = 0.86f, sideGain = 1.f;
};
WidthCoeffs makeWidthCoeffs(float widthParam) {
    WidthCoeffs w;
    w.width = clampf(widthParam, 0.f, 1.f);
    w.phase = 0.5f * w.width;
    w.scaleA = 1.f + 0.28f * w.width;
    w.scaleB = 1.f - 0.28f * w.width;
    const float t = 2.f * w.width;
    w.sideGain = t <= 1.f ? t * t : t;
    return w;
}

// One oscillator (V or Z) for one block: its two copies' speeds and its shape.
struct PairFrame {
    float deltaA = 0.f, deltaB = 0.f;
    float blend = 0.f, rangeBase = 2.2f, season = 0.f, lowShape = 0.f, pwmShape = 0.5f;
    float airGainA = 0.f, airGainB = 0.f, wrapJump = 0.f;
};
PairFrame makePair(float pitch, float fineOctaves, float driftA, float driftB, float shape, float slopeAsym, float rate) {
    PairFrame f;
    const float half = fineOctaves * 0.5f; // A goes flat by half, B sharp by half
    const float freqA = kMiddleCHz * std::exp2(pitch - half + driftA);
    const float freqB = kMiddleCHz * std::exp2(pitch + half + driftB);
    f.deltaA = freqA / rate;
    f.deltaB = freqB / rate;
    const float soft = softenShapeEdges(shape);
    f.season = soft;
    const float bt = clampf(soft * 1.8f, 0.f, 1.f);
    f.blend = bt * bt * (3.f - 2.f * bt);
    f.rangeBase = 2.2f + soft * soft * 28.f;
    const float ls = clampf(soft * 500.f, 0.f, 1.f);
    f.lowShape = ls * ls * (3.f - 2.f * ls);
    f.pwmShape = clampf(shape, 0.05f, 0.95f);
    const float threshold = rate * 0.175f, fade = rate * 0.02f > 1.f ? rate * 0.02f : 1.f;
    f.airGainA = clampf((threshold - freqA) / fade, 0.f, 1.f);
    f.airGainB = clampf((threshold - freqB) / fade, 0.f, 1.f);
    const float center0 = 0.5f + 0.22f * clampf(slopeAsym, -0.9f, 0.9f);
    const float top = voicedSigmoidSaw(0.9999f, f.blend, f.rangeBase, center0, f.lowShape, 0.f, f.season);
    const float bot = voicedSigmoidSaw(0.0001f, f.blend, f.rangeBase, center0, f.lowShape, 0.f, f.season);
    f.wrapJump = (top - bot) * 0.5f;
    return f;
}

inline void wrapPhase(float& phase) {
    if (phase >= 1.f) phase -= 1.f;
    else if (phase < 0.f) phase += 1.f;
}

inline std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline float uniformSigned(std::uint32_t& s) { return xorshift32(s) * (2.f / 4294967296.f) - 1.f; }

void randomWalk(std::uint32_t& rng, float amount, float seconds, float* drift4) {
    const float speed = kBaseDriftSpeed * amount, limit = kDriftLimit * amount;
    for (int i = 0; i < 4; ++i) drift4[i] = clampf(drift4[i] + uniformSigned(rng) * speed * seconds, -limit, limit);
}

float stableVoiceOffset(int voice, int salt) {
    std::uint32_t state = 0x9e3779b9u ^ static_cast<std::uint32_t>((voice + 1) * 0x85ebca6bu) ^ static_cast<std::uint32_t>((salt + 1) * 0xc2b2ae35u);
    if (state == 0u) state = 0x6d2b79f5u;
    return uniformSigned(state);
}

// ---- oversampling ---------------------------------------------------------------------
// The oscillators run at 1, 2, 4 or 8 times the engine's rate, and this brings
// the sum back down: a low-pass just under the engine's limit, then every
// Nth sample. Sixteen taps per step of oversampling, Blackman-Harris windowed.
class Decimator {
public:
    void prepare(int oversample) {
        oversample_ = oversample;
        taps_ = oversample * 16;
        kernel_.assign(static_cast<std::size_t>(taps_), 0.f);
        history_.assign(static_cast<std::size_t>(2 * taps_), 0.f);
        head_ = 0;
        const double cutoff = 0.9 * 0.5 / oversample; // cycles per oversampled sample
        const double pi = 3.14159265358979323846;
        double sum = 0.0;
        for (int n = 0; n < taps_; ++n) {
            const double x = n - (taps_ - 1) / 2.0;
            const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(2 * pi * cutoff * x) / (2 * pi * cutoff * x);
            const double w = static_cast<double>(n) / (taps_ - 1);
            const double window = 0.35875 - 0.48829 * std::cos(2 * pi * w) + 0.14128 * std::cos(4 * pi * w) - 0.01168 * std::cos(6 * pi * w);
            kernel_[static_cast<std::size_t>(n)] = static_cast<float>(sinc * window);
            sum += sinc * window;
        }
        for (float& k : kernel_) k = static_cast<float>(k / sum);
    }
    // `in` holds `oversample` samples; returns one.
    float process(const float* in) {
        if (oversample_ == 1) return in[0];
        for (int i = 0; i < oversample_; ++i) {
            history_[static_cast<std::size_t>(head_)] = history_[static_cast<std::size_t>(head_ + taps_)] = in[i];
            head_ = (head_ + 1) % taps_;
        }
        const float* h = &history_[static_cast<std::size_t>(head_)];
        float out = 0.f;
        for (int n = 0; n < taps_; ++n) out += h[n] * kernel_[static_cast<std::size_t>(n)];
        return out;
    }

private:
    int oversample_ = 1, taps_ = 16, head_ = 0;
    std::vector<float> kernel_, history_;
};

// ---- presets --------------------------------------------------------------------------

struct Pair { int id, value; };
struct Preset {
    const char* name;
    const char* category;
    Pair changes[16]; // from the starting patch; ends at id -1
};

TwinPatch startingPatch() {
    TwinPatch p;
    p.value[VShape] = 50;
    p.value[ZShape] = 50;
    p.value[Crossfade] = 50;
    p.value[FormantDepth] = 50;
    p.value[FormantRatio] = 3;
    p.value[Asymmetry] = 35;
    p.value[Width] = 50;
    p.value[Vintage] = 50;
    p.value[Oversample] = 1;
    p.value[Attack] = 5;
    p.value[Decay] = 45;
    p.value[Sustain] = 80;
    p.value[Release] = 35;
    p.value[Level] = -6;
    return p;
}

#define END {-1, 0}
const Preset kPresets[] = {
    // ---- bass ----
    {"Twin Bass", "Bass", {{VOctave, -1}, {ZSemitone, -12}, {VShape, 30}, {ZShape, 60}, {VFine, 4}, {ZFine, 6}, {FormantDepth, 20}, {Attack, 0}, {Decay, 40}, {Sustain, 60}, {Release, 20}, END}},
    {"Round Bass", "Bass", {{VOctave, -1}, {ZSemitone, -12}, {VShape, 8}, {ZShape, 15}, {Crossfade, 30}, {FormantDepth, 10}, {Width, 30}, {Attack, 0}, {Decay, 50}, {Sustain, 70}, {Release, 22}, END}},
    {"Sync Bass", "Bass", {{VOctave, -1}, {ZSemitone, -5}, {CrossSync, 1}, {Crossfade, 75}, {VShape, 40}, {ZShape, 70}, {FormantDepth, 15}, {Attack, 0}, {Decay, 38}, {Sustain, 50}, {Release, 18}, END}},
    {"Rubber Bass", "Bass", {{VOctave, -1}, {ZSemitone, -12}, {Waveform, 1}, {VShape, 35}, {ZShape, 60}, {FormantDepth, 40}, {FormantRatio, 1}, {Attack, 0}, {Decay, 42}, {Sustain, 55}, {Release, 20}, END}},
    // ---- leads ----
    {"Sync Lead", "Leads", {{ZSemitone, 7}, {CrossSync, 1}, {Crossfade, 80}, {VShape, 60}, {ZShape, 70}, {VFine, 3}, {ZFine, 5}, {Attack, 0}, {Sustain, 90}, {Release, 25}, END}},
    {"Reverse Lead", "Leads", {{ZSemitone, 12}, {ReverseSync, 1}, {Crossfade, 65}, {VShape, 45}, {ZShape, 55}, {FormantDepth, 30}, {Attack, 2}, {Sustain, 85}, {Release, 28}, END}},
    {"Square Lead", "Leads", {{Waveform, 1}, {VShape, 50}, {ZShape, 30}, {ZSemitone, 12}, {Crossfade, 35}, {FormantDepth, 25}, {VFine, 5}, {Attack, 0}, {Sustain, 90}, {Release, 22}, END}},
    {"Formant Lead", "Leads", {{FormantDepth, 90}, {FormantRatio, 5}, {Asymmetry, 60}, {VShape, 55}, {ZShape, 65}, {ZSemitone, 0}, {VFine, 6}, {ZFine, 8}, {Attack, 3}, {Sustain, 85}, {Release, 30}, END}},
    // ---- pads ----
    {"Wide Pad", "Pads", {{VFine, 12}, {ZFine, 14}, {Width, 80}, {FormantDepth, 40}, {FormantRatio, 1}, {VShape, 35}, {ZShape, 45}, {Attack, 60}, {Decay, 60}, {Sustain, 85}, {Release, 65}, END}},
    {"Glass Pad", "Pads", {{ZSemitone, 12}, {VShape, 20}, {ZShape, 30}, {VFine, 8}, {ZFine, 10}, {Width, 70}, {FormantDepth, 35}, {FormantRatio, 4}, {Attack, 50}, {Sustain, 80}, {Release, 70}, END}},
    {"Swap Pad", "Pads", {{CrossfadeMode, 1}, {ZSemitone, 7}, {VFine, 10}, {ZFine, 12}, {Width, 65}, {VShape, 40}, {ZShape, 50}, {Attack, 55}, {Sustain, 85}, {Release, 68}, END}},
    {"Dark Pad", "Pads", {{VShape, 5}, {ZShape, 10}, {ZSemitone, -12}, {VFine, 9}, {ZFine, 11}, {FormantDepth, 20}, {FormantRatio, 0}, {Width, 60}, {Attack, 65}, {Sustain, 90}, {Release, 72}, END}},
    // ---- keys ----
    {"Twin Keys", "Keys", {{VShape, 35}, {ZShape, 45}, {VFine, 5}, {ZFine, 7}, {FormantDepth, 30}, {Attack, 0}, {Decay, 55}, {Sustain, 30}, {Release, 35}, END}},
    {"Hollow Keys", "Keys", {{Waveform, 1}, {VShape, 50}, {ZShape, 25}, {ZSemitone, 12}, {Crossfade, 40}, {FormantDepth, 20}, {Attack, 0}, {Decay, 52}, {Sustain, 25}, {Release, 34}, END}},
    // ---- plucks ----
    {"Twin Pluck", "Plucks", {{VShape, 55}, {ZShape, 65}, {VFine, 4}, {ZFine, 6}, {FormantDepth, 45}, {Attack, 0}, {Decay, 30}, {Sustain, 0}, {Release, 25}, END}},
    {"Fifth Pluck", "Plucks", {{ZSemitone, 7}, {VShape, 45}, {ZShape, 55}, {Crossfade, 45}, {FormantDepth, 35}, {FormantRatio, 4}, {Attack, 0}, {Decay, 34}, {Sustain, 0}, {Release, 28}, END}},
};
#undef END
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

// An envelope time for a 0..100 setting: 1 ms to 8 s.
float secondsFor(float setting) { return 0.001f * std::pow(8000.f, clampf(setting, 0.f, 100.f) / 100.f); }

} // namespace

namespace twin {

int settingMin(int id) {
    switch (id) {
    case VOctave: return -2;
    case ZSemitone: return -24;
    case VFine: case ZFine: return -20;
    case Level: return -24;
    default: return 0;
    }
}

int settingMax(int id) {
    switch (id) {
    case VOctave: return 2;
    case ZSemitone: return 24;
    case VFine: case ZFine: return 20;
    case CrossSync: case Waveform: case CrossfadeMode: return 1;
    case ReverseSync: return 2;
    case FormantRatio: return 7;
    case Oversample: return 3;
    case Level: return 6;
    default: return id >= 0 && id < kSettings ? 100 : 0;
    }
}

} // namespace twin

// ---- the instrument ---------------------------------------------------------------------

struct Twin::State {
    struct Copy { float phase = 0.f, dir = 1.f, drift = 0.f, noise = 0.f; };
    struct OscPair { Copy a, b; float revTravel = 1.f; };
    enum Stage { Idle, Attacking, Decaying, Releasing };
    struct Voice {
        OscPair v, z;
        Stage stage = Idle;
        float level = 0.f;    // the envelope, 0..1
        float velocity = 0.f;
        int note = -1;
        std::uint32_t age = 0; // when it was last started; the oldest is taken first
        std::uint32_t rng = 1;
        float pitchOffset[2], shapeOffset[2], panLeft, panRight; // voice character, fixed per voice
    };

    // The patch and macros, resolved into what the audio loop reads.
    struct Resolved {
        float pitchV = 0.f, pitchZ = 0.f, fineV = 0.f, fineZ = 0.f; // octaves
        float shapeV = 0.5f, shapeZ = 0.5f, xfade = 0.5f, xfadeCos = 0.7071f, xfadeSin = 0.7071f, widthBlend = 1.f;
        bool sync = false, reverse = false, mutual = false, pwm = false, stereoSwap = false;
        float formantDepth = 0.f, formantRatio = 2.f, baseAsym = 0.f;
        WidthCoeffs width;
        float vintage = 1.f, shapedNoise = 0.f;
        int oversample = 2;
        float attackStep = 1.f, decayCoef = 0.f, releaseCoef = 0.f, sustain = 0.8f, gain = 0.5f;
    } r;

    TwinPatch patch = startingPatch();
    float macro[kMacros];
    int preset = 0;
    float sampleRate = 48000.f;
    Voice voices[kVoices];
    std::uint32_t clock = 0;
    float sharedDrift[4] = {};
    std::uint32_t sharedRng = 0x1234567u;
    Decimator downL, downR;
    int preparedOversample = 0;
    int qualityLimit = 3; // the most oversampling allowed just now (Engine: the machine was falling behind)
    float dcInL = 0.f, dcOutL = 0.f, dcInR = 0.f, dcOutR = 0.f, dcCoeff = 0.995f;
    std::uint32_t noiseRng = 0x2468ACE1u;
    float mixL[kBlock * kMaxOversample], mixR[kBlock * kMaxOversample];

    void renderBlock(float* left, float* right, int frames);
};

Twin::Twin() : state_(new State) {
    State& s = *state_;
    for (float& m : s.macro) m = 0.5f;
    for (int i = 0; i < kVoices; ++i) {
        State::Voice& v = s.voices[i];
        v.rng = 0x9E3779B9u * static_cast<std::uint32_t>(i + 1) | 1u;
        v.pitchOffset[0] = stableVoiceOffset(i, 0);
        v.pitchOffset[1] = stableVoiceOffset(i, 1);
        v.shapeOffset[0] = stableVoiceOffset(i, 2);
        v.shapeOffset[1] = stableVoiceOffset(i, 3);
        v.panLeft = stableVoiceOffset(i, 4);  // level, until apply() turns the pair into gains
        v.panRight = stableVoiceOffset(i, 5); // pan
    }
    selectPreset(0);
}

Twin::~Twin() = default;

void Twin::prepare(float sampleRate) {
    State& s = *state_;
    s.sampleRate = sampleRate;
    s.dcCoeff = 1.f - (2.f * 3.14159265f * 10.f / sampleRate); // a 10 Hz high-pass
    s.preparedOversample = 0;
    // Build both filters for every factor's worst case now, so a change of
    // the oversampling setting on the audio thread never allocates.
    s.downL.prepare(kMaxOversample);
    s.downR.prepare(kMaxOversample);
    apply();
}

int Twin::presetCount() { return kPresetCount; }
const char* Twin::presetName(int i) { return i >= 0 && i < kPresetCount ? kPresets[i].name : ""; }
const char* Twin::presetCategory(int i) { return i >= 0 && i < kPresetCount ? kPresets[i].category : ""; }

TwinPatch Twin::presetPatch(int index) {
    TwinPatch p = startingPatch();
    const Preset& preset = kPresets[clampi(index, 0, kPresetCount - 1)];
    for (const Pair& change : preset.changes) {
        if (change.id < 0) break;
        p.value[change.id] = clampi(change.value, settingMin(change.id), settingMax(change.id));
    }
    return p;
}

// Turn the patch, bent by the macros, into what the audio loop reads.
void Twin::apply() {
    State& s = *state_;
    State::Resolved& r = s.r;
    const int* p = s.patch.value;
    const float* m = s.macro;
    const auto unit = [&](int id, int macro, float reach) { return clampf(p[id] / 100.f + (macro < 0 ? 0.f : (m[macro] - 0.5f) * reach), 0.f, 1.f); };

    r.pitchV = static_cast<float>(p[VOctave]);
    r.pitchZ = p[ZSemitone] / 12.f;
    // DETUNE spreads both oscillators' copies, up to 20 cents further each.
    const float extraCents = (m[Detune] - 0.5f) * 40.f;
    r.fineV = clampf(p[VFine] + extraCents, -20.f, 20.f) / 1200.f;
    r.fineZ = clampf(p[ZFine] + extraCents, -20.f, 20.f) / 1200.f;
    r.shapeV = unit(VShape, Shape, 1.f);
    r.shapeZ = unit(ZShape, Shape, 1.f);
    r.xfade = unit(Crossfade, Blend, 2.f);
    r.xfadeCos = std::cos(r.xfade * 1.5707963f);
    r.xfadeSin = std::sin(r.xfade * 1.5707963f);
    r.widthBlend = std::sin(r.xfade * 3.14159265f);
    r.sync = p[CrossSync] != 0;
    r.reverse = p[ReverseSync] >= 1;
    r.mutual = p[ReverseSync] >= 2;
    r.pwm = p[Waveform] != 0;
    r.stereoSwap = p[CrossfadeMode] != 0;
    r.formantDepth = formantDepthCurve(unit(FormantDepth, Formant, 2.f));
    r.formantRatio = kFormantRatios[clampi(p[FormantRatio], 0, 7)];
    r.baseAsym = formantAsymCurve(p[Asymmetry] / 100.f);
    r.width = makeWidthCoeffs(unit(Width, MacroWidth, 2.f));
    // The vintage macro as in the module: the bottom half sweeps up to the
    // calibrated amount, the top half runs on to four times it.
    const float vintage = unit(Vintage, MacroVintage, 2.f);
    r.vintage = vintage <= 0.5f ? vintage * 2.f : 1.f + (vintage - 0.5f) * 6.f;
    r.shapedNoise = std::pow(p[Noise] / 100.f, kNoiseShapeExp);

    const int oversample = 1 << clampi(p[Oversample] < s.qualityLimit ? p[Oversample] : s.qualityLimit, 0, 3);
    if (oversample != s.preparedOversample) {
        // prepare() sized the filters for the largest factor, so this only
        // rewrites numbers already allocated.
        s.downL.prepare(oversample);
        s.downR.prepare(oversample);
        s.preparedOversample = oversample;
    }
    r.oversample = oversample;

    const float attack = secondsFor(clampf(p[Attack] + (m[MacroAttack] - 0.5f) * 100.f, 0.f, 100.f));
    const float release = secondsFor(clampf(p[Release] + (m[MacroRelease] - 0.5f) * 100.f, 0.f, 100.f));
    r.attackStep = 1.f / (attack * s.sampleRate);
    // Decay and release fall by a factor of a thousand (60 dB) in their time.
    r.decayCoef = std::exp(-6.9078f / (secondsFor(static_cast<float>(p[Decay])) * s.sampleRate));
    r.releaseCoef = std::exp(-6.9078f / (release * s.sampleRate));
    r.sustain = p[Sustain] / 100.f;
    r.gain = std::pow(10.f, p[Level] / 20.f);

    // Voice character: small fixed differences between the eight voices.
    for (int i = 0; i < kVoices; ++i) {
        State::Voice& v = s.voices[i];
        const float level = 1.f + stableVoiceOffset(i, 4) * kVoiceLevel * r.vintage;
        const float pan = stableVoiceOffset(i, 5) * kVoicePan * r.vintage;
        v.panLeft = level * (1.f - pan);
        v.panRight = level * (1.f + pan);
    }
}

void Twin::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    State& s = *state_;
    s.patch = presetPatch(index);
    for (float& m : s.macro) m = 0.5f;
    s.preset = index;
    apply();
}

void Twin::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = clampi(value, settingMin(id), settingMax(id));
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    apply();
}

void Twin::setQualityLimit(int limit) {
    limit = clampi(limit, 0, 3);
    if (limit == state_->qualityLimit) return;
    state_->qualityLimit = limit;
    apply();
}

void Twin::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !std::isfinite(value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return;
    state_->macro[macro] = value;
    apply();
}

void Twin::noteOn(int note, float velocity) {
    if (note < 0 || note > 127 || !(velocity > 0.f)) return; // also drops NaN
    State& s = *state_;
    // The same key again restarts its voice. Otherwise a free voice, or the
    // one that has been going longest.
    State::Voice* chosen = nullptr;
    for (State::Voice& v : s.voices)
        if (v.note == note && v.stage != State::Idle) { chosen = &v; break; }
    if (!chosen)
        for (State::Voice& v : s.voices)
            if (v.stage == State::Idle) { chosen = &v; break; }
    if (!chosen) {
        chosen = &s.voices[0];
        for (State::Voice& v : s.voices)
            if (s.clock - v.age > s.clock - chosen->age) chosen = &v;
    }
    chosen->note = note;
    chosen->velocity = velocity > 1.f ? 1.f : velocity;
    chosen->stage = State::Attacking; // from wherever the envelope is, so a stolen voice does not click
    chosen->age = s.clock++;
}

void Twin::noteOff(int note) {
    for (State::Voice& v : state_->voices)
        if (v.note == note && v.stage != State::Idle) v.stage = State::Releasing;
}

void Twin::allNotesOff() {
    for (State::Voice& v : state_->voices)
        if (v.stage != State::Idle) v.stage = State::Releasing;
}

void Twin::render(float* left, float* right, int frames) {
    while (frames > 0) {
        const int n = frames < kBlock ? frames : kBlock;
        state_->renderBlock(left, right, n);
        left += n;
        right += n;
        frames -= n;
    }
}

void Twin::State::renderBlock(float* left, float* right, int frames) {
    const int os = r.oversample;
    const float rate = sampleRate * os;
    const int total = frames * os;
    for (int i = 0; i < total; ++i) mixL[i] = mixR[i] = 0.f;

    const float blockSeconds = frames / sampleRate;
    if (r.vintage > 0.f) randomWalk(sharedRng, r.vintage, blockSeconds, sharedDrift);
    else sharedDrift[0] = sharedDrift[1] = sharedDrift[2] = sharedDrift[3] = 0.f;

    const float noiseScale = kPhaseNoiseScale * r.shapedNoise;
    const float widthGain = kStereoSwapWidthGain * r.widthBlend;
    const bool formantActive = r.formantDepth > 0.001f || r.baseAsym > 0.001f || r.width.width > 0.001f;
    bool anySound = false;

    for (Voice& voice : voices) {
        if (voice.stage == Idle) continue;
        anySound = true;

        // Drift: each copy wanders on its own, with a little shared breathing.
        float drift[4] = {voice.v.a.drift, voice.v.b.drift, voice.z.a.drift, voice.z.b.drift};
        if (r.vintage > 0.f) randomWalk(voice.rng, r.vintage, blockSeconds, drift);
        else drift[0] = drift[1] = drift[2] = drift[3] = 0.f;
        voice.v.a.drift = drift[0];
        voice.v.b.drift = drift[1];
        voice.z.a.drift = drift[2];
        voice.z.b.drift = drift[3];
        for (int i = 0; i < 4; ++i) drift[i] = drift[i] * 0.6f + sharedDrift[i] * 0.4f;

        // Middle C is note 60. Z follows V's key, offset by its own setting.
        const float key = (voice.note - 60) / 12.f;
        const float pitchV = r.pitchV + key + voice.pitchOffset[0] * kVoicePitchOct * r.vintage;
        const float pitchZ = r.pitchZ + key + voice.pitchOffset[1] * kVoicePitchOct * r.vintage;
        const float shapeV = clampf(r.shapeV + voice.shapeOffset[0] * kVoiceShape * r.vintage, 0.f, 1.f);
        const float shapeZ = clampf(r.shapeZ + voice.shapeOffset[1] * kVoiceShape * r.vintage, 0.f, 1.f);
        const PairFrame vOsc = makePair(pitchV, r.fineV, drift[0], drift[1], shapeV, r.baseAsym, rate);
        const PairFrame zOsc = makePair(pitchZ, r.fineZ, drift[2], drift[3], shapeZ, r.baseAsym, rate);

        if (!r.sync && !r.reverse) {
            voice.z.a.dir = voice.z.b.dir = 1.f;
            voice.v.revTravel = voice.z.revTravel = 1.f;
        }
        if (!r.reverse || !r.mutual) voice.v.a.dir = voice.v.b.dir = 1.f; // V only runs backward in mutual mode

        // With the formant depth at nothing its modulators have no effect, so
        // the slope and centre are the same all block: work them out once.
        const bool formantMoves = r.formantDepth > 0.001f;
        float sVA = vOsc.rangeBase, sVB = vOsc.rangeBase, sZA = zOsc.rangeBase, sZB = zOsc.rangeBase, sCentre = 0.5f;
        if (formantActive && !formantMoves) {
            formantMod(vOsc.rangeBase, 0.f, 0.f, r.baseAsym, 0.f, r.width.scaleA, sVA, sCentre);
            formantMod(vOsc.rangeBase, 0.f, 0.f, r.baseAsym, 0.f, r.width.scaleB, sVB, sCentre);
            formantMod(zOsc.rangeBase, 0.f, 0.f, r.baseAsym, 0.f, r.width.scaleA, sZA, sCentre);
            formantMod(zOsc.rangeBase, 0.f, 0.f, r.baseAsym, 0.f, r.width.scaleB, sZB, sCentre);
        }

        // Per copy, for the loop of four: V left, V right, Z left, Z right.
        const float blend4[4] = {vOsc.blend, vOsc.blend, zOsc.blend, zOsc.blend};
        const float lowShape4[4] = {vOsc.lowShape, vOsc.lowShape, zOsc.lowShape, zOsc.lowShape};
        const float season4[4] = {vOsc.season, vOsc.season, zOsc.season, zOsc.season};
        const float air4[4] = {vOsc.airGainA, vOsc.airGainB, zOsc.airGainA, zOsc.airGainB};
        const float delta4[4] = {vOsc.deltaA, vOsc.deltaB, zOsc.deltaA, zOsc.deltaB};
        const float rangeBase4[4] = {vOsc.rangeBase, vOsc.rangeBase, zOsc.rangeBase, zOsc.rangeBase};
        const float widthScale4[4] = {r.width.scaleA, r.width.scaleB, r.width.scaleA, r.width.scaleB};

        OscPair& V = voice.v;
        OscPair& Z = voice.z;
        int at = 0;
        for (int frame = 0; frame < frames; ++frame) {
            // The volume envelope, once per engine sample.
            if (voice.stage == Attacking) {
                voice.level += r.attackStep;
                if (voice.level >= 1.f) { voice.level = 1.f; voice.stage = Decaying; }
            } else if (voice.stage == Decaying) {
                voice.level = r.sustain + (voice.level - r.sustain) * r.decayCoef;
            } else {
                voice.level *= r.releaseCoef;
                if (voice.level < 0.0001f) { voice.level = 0.f; voice.stage = Idle; }
            }
            const float amp = voice.level * voice.velocity;
            const float ampL = amp * voice.panLeft, ampR = amp * voice.panRight;

            if (noiseScale > 0.f) {
                V.a.noise = uniformSigned(voice.rng);
                V.b.noise = uniformSigned(voice.rng);
                Z.a.noise = uniformSigned(voice.rng);
                Z.b.noise = uniformSigned(voice.rng);
            }
            const float stepVA = vOsc.deltaA + V.a.noise * noiseScale, stepVB = vOsc.deltaB + V.b.noise * noiseScale;
            const float stepZA = zOsc.deltaA + Z.a.noise * noiseScale, stepZB = zOsc.deltaB + Z.b.noise * noiseScale;

            for (int k = 0; k < os; ++k, ++at) {
                const float preVA = V.a.phase, preZA = Z.a.phase;
                V.a.phase += stepVA * V.a.dir;
                V.b.phase += stepVB * V.b.dir;
                Z.a.phase += stepZA * Z.a.dir;
                Z.b.phase += stepZB * Z.b.dir;
                wrapPhase(V.a.phase);
                wrapPhase(V.b.phase);
                wrapPhase(Z.a.phase);
                wrapPhase(Z.b.phase);

                const bool vWrapped = std::fabs(V.a.phase - preVA) > 0.5f;
                const bool vHalf = !vWrapped && ((preVA < 0.5f) != (V.a.phase < 0.5f));
                V.revTravel = V.revTravel + stepVA < 1.f ? V.revTravel + stepVA : 1.f;
                Z.revTravel = Z.revTravel + stepZA < 1.f ? Z.revTravel + stepZA : 1.f;

                if (r.sync) {
                    // Cross sync: Z restarts each time V comes round.
                    if (vWrapped) {
                        const float offset = Z.b.phase - Z.a.phase;
                        Z.a.phase = V.a.phase;
                        Z.b.phase = V.a.phase + offset;
                        wrapPhase(Z.b.phase);
                        Z.a.dir = Z.b.dir = 1.f;
                    }
                } else if (r.reverse) {
                    // Reverse sync: V turns Z around, always (the module's CHANCE is left out).
                    // The quarter-cycle guard stops the two locking into a flip on every sample.
                    if (vWrapped && V.revTravel >= 0.25f) { Z.a.dir = -Z.a.dir; V.revTravel = 0.f; }
                    if (vHalf && V.revTravel >= 0.25f) { Z.b.dir = -Z.b.dir; V.revTravel = 0.f; }
                    if (r.mutual) {
                        const bool zWrapped = std::fabs(Z.a.phase - preZA) > 0.5f;
                        if (zWrapped && Z.revTravel >= 0.25f) {
                            V.a.dir = -V.a.dir;
                            V.b.dir = -V.b.dir;
                            Z.revTravel = 0.f;
                        }
                    }
                }

                float vA, vB, zA, zB;
                if (r.pwm) {
                    float wVA = vOsc.pwmShape, wVB = vOsc.pwmShape, wZA = zOsc.pwmShape, wZB = zOsc.pwmShape;
                    if (r.formantDepth > 0.001f) {
                        const float depth = 0.45f * r.formantDepth, skew = 0.5f + 0.44f * r.baseAsym;
                        wVA = clampf(vOsc.pwmShape + depth * skewedSin2Pi(V.a.phase * r.formantRatio, skew), 0.05f, 0.95f);
                        wVB = clampf(vOsc.pwmShape + depth * skewedSin2Pi(V.a.phase * r.formantRatio + r.width.phase, skew), 0.05f, 0.95f);
                        wZA = clampf(zOsc.pwmShape + depth * skewedSin2Pi(Z.a.phase * r.formantRatio, skew), 0.05f, 0.95f);
                        wZB = clampf(zOsc.pwmShape + depth * skewedSin2Pi(Z.a.phase * r.formantRatio + r.width.phase, skew), 0.05f, 0.95f);
                    }
                    vA = pwmWithPolyBLEP(V.a.phase, wVA, vOsc.deltaA);
                    vB = pwmWithPolyBLEP(V.b.phase, wVB, vOsc.deltaB);
                    zA = pwmWithPolyBLEP(Z.a.phase, wZA, zOsc.deltaA);
                    zB = pwmWithPolyBLEP(Z.b.phase, wZB, zOsc.deltaB);
                } else {
                    const float phase[4] = {V.a.phase, V.b.phase, Z.a.phase, Z.b.phase};
                    float range[4], centre[4];
                    if (formantActive && !formantMoves) {
                        range[0] = sVA; range[1] = sVB; range[2] = sZA; range[3] = sZB;
                        centre[0] = centre[1] = centre[2] = centre[3] = sCentre;
                    } else if (formantActive) {
                        // The formant section: a modulator locked to each
                        // oscillator's own pitch bends its slope and centre.
                        // The right-hand copies' modulators run ahead by the
                        // width's phase. Four at once, as below.
                        const float baseV = V.a.phase * r.formantRatio, baseZ = Z.a.phase * r.formantRatio;
                        const float base[4] = {baseV, baseV + r.width.phase, baseZ, baseZ + r.width.phase};
                        for (int i = 0; i < 4; ++i) {
                            const float wob = sinNoBranch(base[i]), asymWob = sinNoBranch(base[i] + 0.25f);
                            float scale = 1.f + 3.f * r.formantDepth * wob;
                            scale = (scale > 0.12f ? scale : 0.12f) * widthScale4[i];
                            range[i] = rangeBase4[i] * scale;
                            float a = r.baseAsym + 0.7f * r.formantDepth * asymWob;
                            a = a < -0.9f ? -0.9f : (a > 0.9f ? 0.9f : a);
                            centre[i] = 0.5f + 0.22f * a;
                        }
                    } else {
                        range[0] = range[1] = vOsc.rangeBase;
                        range[2] = range[3] = zOsc.rangeBase;
                        centre[0] = centre[1] = centre[2] = centre[3] = 0.5f;
                    }
                    // The four waves, as one loop the compiler can do in one go.
                    // Smooth the jump where a forward-running phase comes round.
                    const float jump[4] = {V.a.dir > 0.f ? vOsc.wrapJump : 0.f, V.b.dir > 0.f ? vOsc.wrapJump : 0.f,
                                           Z.a.dir > 0.f ? zOsc.wrapJump : 0.f, Z.b.dir > 0.f ? zOsc.wrapJump : 0.f};
                    float wave[4];
                    for (int i = 0; i < 4; ++i) {
                        wave[i] = sigmoidSawNoBranch(phase[i], blend4[i], range[i], centre[i], lowShape4[i], air4[i], season4[i])
                                  - jump[i] * wrapBlepNoBranch(phase[i], delta4[i]);
                    }
                    vA = wave[0];
                    vB = wave[1];
                    zA = wave[2];
                    zB = wave[3];
                }

                float outL, outR;
                if (!r.stereoSwap) {
                    // Equal-power crossfade: the A copies left, the B copies right.
                    outL = vA * r.xfadeCos + zA * r.xfadeSin;
                    outR = vB * r.xfadeCos + zB * r.xfadeSin;
                } else {
                    // Stereo swap: Z's copies change sides, with a little opposite-polarity crossfeed.
                    outL = vA * r.xfadeCos + zB * r.xfadeSin - widthGain * (vB * (1.f - r.xfade) + zA * r.xfade);
                    outR = vB * r.xfadeCos + zA * r.xfadeSin - widthGain * (vA * (1.f - r.xfade) + zB * r.xfade);
                }
                if (r.width.sideGain != 1.f) {
                    const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR);
                    outL = mid + side * r.width.sideGain;
                    outR = mid - side * r.width.sideGain;
                }
                mixL[at] += fastTanh(outL) * ampL;
                mixR[at] += fastTanh(outR) * ampR;
            }
        }
    }

    if (!anySound && dcOutL == 0.f && dcOutR == 0.f) {
        // Nothing playing and nothing ringing in the filters: silence costs nothing.
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        return;
    }

    // Back down to the engine's rate, then the output stage.
    const float drive = 1.f + kBusDrive * r.vintage;
    const float colourGain = 1.f / std::tanh(drive);
    const float crosstalk = kBusCrosstalk * r.vintage < 0.15f ? kBusCrosstalk * r.vintage : 0.15f;
    const float colourMix = r.vintage < 1.f ? r.vintage : 1.f;
    for (int i = 0; i < frames; ++i) {
        // Each voice arrives at up to +-1. Scaled so that one voice sits well
        // inside the bus colour's gentle part and a full chord leans on it.
        float l = downL.process(&mixL[i * os]) * kVoiceGain;
        float rr = downR.process(&mixR[i * os]) * kVoiceGain;
        // Remove any steady offset (a 10 Hz high-pass).
        const float hl = l - dcInL + dcCoeff * dcOutL;
        dcInL = l;
        dcOutL = hl;
        const float hr = rr - dcInR + dcCoeff * dcOutR;
        dcInR = rr;
        dcOutR = hr;
        l = hl;
        rr = hr;
        if (r.shapedNoise > 0.f) {
            l += uniformSigned(noiseRng) * kNoisePeak * r.shapedNoise;
            rr += uniformSigned(noiseRng) * kNoisePeak * r.shapedNoise;
        }
        if (r.vintage > 0.f) {
            // Bus colour: a little of each side in the other, then a soft clip.
            const float ml = l * (1.f - crosstalk) + rr * crosstalk, mr = rr * (1.f - crosstalk) + l * crosstalk;
            l = ml * (1.f - colourMix) + colourGain * fastTanh(ml * drive) * colourMix;
            rr = mr * (1.f - colourMix) + colourGain * fastTanh(mr * drive) * colourMix;
        }
        left[i] = std::isfinite(l) ? l * r.gain : 0.f;
        right[i] = std::isfinite(rr) ? rr * r.gain : 0.f;
    }
    // Let the offset filter settle to exactly nothing once the sound has gone.
    if (!anySound) {
        if (std::fabs(dcOutL) < 1e-6f) dcOutL = dcInL = 0.f;
        if (std::fabs(dcOutR) < 1e-6f) dcOutR = dcInR = 0.f;
    }
}

} // namespace apotheca
