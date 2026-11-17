// CLOUD: clouds of grains from a recording (docs/plans/granular-plan.md; cloud.hpp says what it
// does, cloud_rule.hpp holds the figures).
//
// A render is worked in pieces of kPiece samples. Each piece: every key's envelope first (it does
// not hang on the grains), which says where a key ends; then the grains, from one onset to the
// next: every grain sounding is added in from where it is to the next onset, in the pool's order,
// and the grains due there are started. So each sample is the same sum in the same order however
// the sound is cut into blocks, and a pool slot is free from the very sample its grain ends.

#include "apotheca/cloud.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "apotheca/envelope.hpp"

namespace apotheca {

using namespace cloud;

namespace {

struct Preset {
    const char* name;
    const char* category;
    int value[kSettings];
};
// The engine's own: the STILL folder (SCAN at FREEZE: a pad from one place). The plan's other three
// folders, DRIFT, DUST and CHOIR, are data in the app (app/presets/cloud.inc), each a folder of its own
// after these. Preset 0 is a new track's. Nobody has heard any of them.
//                                  POS SPR SIZE DENS SCAT SHAPE PSPR STEPS REV STER SCAN ROOT TUNE ATK REL LEVEL VEL
const Preset kPresets[] = {
    {"Soft Cloud", "STILL", {25, 10, 60, 52, 50, 50, 0, kStepsFree, 0, 40, 50, 60, 0, 8, 54, 80, 50}},
    // Long grains packed tight from one place, a few cents apart: a held glass note.
    {"Glass Hold", "STILL", {40, 3, 85, 75, 60, 50, 10, kStepsFree, 0, 70, 50, 60, 0, 40, 70, 75, 40}},
    // Each grain swells in and some run backwards, wide: the recording breathed rather than played.
    {"Breath Pad", "STILL", {60, 20, 80, 60, 90, 85, 5, kStepsFree, 15, 90, 50, 60, 0, 65, 75, 75, 30}},
    // An octave above and below sprinkled through it (PITCH SPRAY on the octaves' grid).
    {"Frozen Shimmer", "STILL", {50, 5, 75, 70, 60, 60, 85, kStepsOctaves, 0, 80, 50, 60, 0, 50, 75, 66, 40}},
    // Few grains, each nearly a second, at random times: the recording surfacing and sinking.
    {"Still Water", "STILL", {30, 8, 95, 35, 100, 50, 0, kStepsFree, 0, 50, 50, 60, 0, 30, 70, 80, 40}},
    // Half the grains backwards, every side, a slow release: a ring of the one place.
    {"Halo", "STILL", {70, 15, 80, 65, 70, 40, 10, kStepsFree, 50, 100, 50, 60, 0, 40, 75, 75, 40}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);
constexpr int kLogged = 64; // the starts kept for the tests
constexpr std::int64_t kWhole = std::int64_t(1) << 32; // a sample, in a grain's place
// The plan's third cut (section 3): a grain shorter than this is read straight between two samples,
// not by Hermite (too short to hear the difference: SELF, the owner's ear decides later).
constexpr double kLinearUnder = 0.020;
constexpr double kMostRatio = 4096.0; // a grain reads at most this fast (and this slow), a guard

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A place in the recording, one sample at a time, either way: the recording has no edges.
inline int wrapIndex(int i, int length) {
    if (i >= 0 && i < length) return i;
    const int w = i % length;
    return w < 0 ? w + length : w;
}

} // namespace

struct Cloud::State {
    float rate = 48000.f;
    const float* data = nullptr; // the recording, or none
    int length = 0;
    const std::vector<float>* toneData = nullptr;

    CloudPatch patch;
    float macro[kMacros];
    bool dirty = true;
    float eff[kSettings]; // the patch with the macros on it
    envelope::Shape shape;

    struct Key {
        bool on = false, fading = false, released = false;
        int note = -1;
        envelope::Envelope env;
        long fadeLeft = 0;
        std::uint32_t seed = 0, n = 0; // the strike's roll; grains counted since the strike
        long clock = 0;                // samples since the strike, at the piece's start
        long nextOnset = 0, lastOnset = 0; // since the strike
        double scanned = 0.0;          // what SCAN has moved the place on, in samples
        float velocity = 1.f;
        double slideFrom = 0.0;        // semitones off at the strike (GLIDE)
        long slideLength = 0;          // samples it takes; 0: none
        unsigned age = 0;
        int grains = 0;                // its grains sounding
    };
    Key keys[kKeySlots];

    struct Grain {
        int key = -1; // -1: free
        long k = 0, left = 0, length = 0, rise = 0;
        float invRise = 1.f, invFall = 1.f;
        bool plain = false; // whole samples at the recording's own pitch
        bool linear = false; // read straight between two samples (a grain under kLinearUnder)
        // Where it reads and how far a sample, in samples with 32 bits after the point: whole
        // samples are exact, and the sum a sample is one integer add (no double waited on).
        std::int64_t pos = 0, step = kWhole;
        float gl = 0.f, gr = 0.f;
    };
    Grain grains[kPool];
    int grainCount = 0;

    std::uint32_t nextSeed = 0, liveCount = 0;
    bool seedGiven = false;
    unsigned ageClock = 0;
    long fadeSamples = 240;
    float fadeStep = 1.f / 240.f;

    long total = 0; // samples rendered since made
    long started = 0, skipped = 0;
    Started log[kLogged];

    float env[kKeySlots][kPiece];
    float sumL[kKeySlots][kPiece], sumR[kKeySlots][kPiece]; // each key's grains, before its envelope
    int dies[kKeySlots]; // where in this piece each key ends (kPiece or more: not in it)

    State() {
        for (float& m : macro) m = 0.5f;
        for (float& e : eff) e = 0.f;
    }

    const float* source(int* n) const {
        if (data) {
            *n = length;
            return data;
        }
        *n = toneData ? static_cast<int>(toneData->size()) : 0;
        return toneData && !toneData->empty() ? toneData->data() : nullptr;
    }

    void refresh() {
        for (int id = 0; id < kSettings; ++id) eff[id] = static_cast<float>(patch.value[id]);
        for (int m = 0; m < kMacros; ++m) {
            const int id = macroSetting(m);
            eff[id] = macroBent(static_cast<float>(patch.value[id]), macro[m], static_cast<float>(settingMin(id)), static_cast<float>(settingMax(id)));
        }
        shape.setFrom(eff[Attack], 0.f, 100.f, eff[Release], rate);
        dirty = false;
    }

    void freeGrain(Grain& g) {
        --keys[g.key].grains;
        g.key = -1;
        --grainCount;
    }
    void endKey(int k) {
        for (Grain& g : grains)
            if (g.key == k) freeGrain(g);
        keys[k] = Key();
    }
    void fadeOut(Key& key) {
        key.fading = true;
        key.fadeLeft = fadeSamples;
    }

    void startGrain(int k, int t, int pieceLength);
    void renderGrains(int from, int to);
    void piece(float* left, float* right, int n);
};

// ---- a grain ------------------------------------------------------------------------------

void Cloud::State::startGrain(int k, int t, int pieceLength) {
    Key& key = keys[k];
    const long at = key.clock + t;
    const std::uint32_t n = key.n++;
    const std::uint32_t seed = key.seed;
    // The next onset, whatever becomes of this grain.
    const double hz = densityHz(eff[Density]);
    key.nextOnset = at + onsetWait(rate, hz, scatterShare(eff[Scatter]), grainUnit(seed, n, kGrainTime));
    int length = 0;
    const float* d = source(&length);
    key.scanned = wrapPlace(key.scanned + scanned(scanTimes(eff[Scan]), at - key.lastOnset), length);
    key.lastOnset = at;
    if (!d || grainCount >= kPool || key.grains >= kGrainsPerKey) {
        ++skipped;
        return;
    }
    const double size = sizeSeconds(eff[Size]);
    Grain g;
    g.key = k;
    g.length = grainSamples(size, rate);
    g.rise = riseSamples(g.length, riseShare(eff[Shape]), rate);
    g.left = g.length;
    if (dies[k] < pieceLength && g.left > dies[k] - t) g.left = dies[k] - t; // its key ends in this piece
    g.invRise = 1.f / static_cast<float>(g.rise);
    g.invFall = 1.f / static_cast<float>(g.length - g.rise);
    const double slide = key.slideLength > 0 && at < key.slideLength ? key.slideFrom * (1.0 - static_cast<double>(at) / static_cast<double>(key.slideLength)) : 0.0;
    const double roll = pitchRoll(pitchSprayCents(eff[PitchSpray]), static_cast<int>(eff[PitchSteps]), grainUnit(seed, n, kGrainPitch));
    const double ratio = grainRatio(key.note, static_cast<int>(eff[Root]), tuneCents(static_cast<int>(eff[Tune])), slide, roll);
    const bool back = grainUnit(seed, n, kGrainBack) < reverseChance(eff[Reverse]);
    double place = grainPlace(positionShare(eff[Position]), key.scanned, sprayShare(eff[Spray]), grainUnit(seed, n, kGrainPlace), length);
    if (back) place = wrapPlace(place + static_cast<double>(g.length - 1) * ratio, length); // the same stretch, from its far end
    g.plain = ratio == 1.0;
    g.linear = size < kLinearUnder;
    const double held = ratio > kMostRatio ? kMostRatio : (ratio < 1.0 / kMostRatio ? 1.0 / kMostRatio : ratio);
    g.step = g.plain ? kWhole : static_cast<std::int64_t>(std::llround(held * static_cast<double>(kWhole)));
    if (back) g.step = -g.step;
    if (g.plain) place = wrapIndex(static_cast<int>(std::lround(place)), length);
    g.pos = static_cast<std::int64_t>(std::llround(place * static_cast<double>(kWhole)));
    if (g.pos >= static_cast<std::int64_t>(length) * kWhole || g.pos < 0) g.pos = 0; // (a place a hair under the end, rounded up to it)
    double l = 0.0, r = 0.0;
    sideGains(stereoWidth(eff[Stereo]), grainUnit(seed, n, kGrainSide), &l, &r);
    const double gain = static_cast<double>(kCloudGain) * overlapGain(hz, size) * levelGain(eff[Level]) * key.velocity;
    g.gl = static_cast<float>(gain * l);
    g.gr = static_cast<float>(gain * r);
    int slot = 0;
    while (grains[slot].key >= 0) ++slot; // (grainCount < kPool: one is free)
    grains[slot] = g;
    ++grainCount;
    ++key.grains;
    Started& s = log[started % kLogged];
    s.at = total + t;
    s.place = place;
    s.step = back ? -ratio : ratio;
    s.key = k;
    s.length = g.length;
    ++started;
}

namespace {

// 4-point, 3rd-order Hermite between x0 and x1, f of the way (the same curve as the usual
// c3 f^3 + c2 f^2 + c1 f + x0, in fewer sums).
inline float hermite(float xm1, float x0, float x1, float x2, float f) {
    const float c = 0.5f * (x1 - xm1);
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + 0.5f * (x2 - x0);
    const float b = w + a;
    return ((a * f - b) * f + c) * f + x0;
}

// The window over a run, into `w`: windowPoly from x0 in steps of dx. A loop the compiler makes
// four samples at a time (nothing waits on the sample before).
// Each x is worked out from the grain's own sample number k, never added up along the run, so it
// is the same however the run is cut.
void windowRun(float* __restrict w, int n, float x0, float dx, int k0) {
    for (int j = 0; j < n; ++j) {
        w[j] = windowPoly(x0 + static_cast<float>(k0 + j) * dx);
    }
}

// Where a grain is going: one sample on, round the recording's ends.
inline std::int64_t stepOn(std::int64_t pos, std::int64_t step, std::int64_t end) {
    pos += step;
    if (pos >= end) {
        pos -= end;
        if (pos >= end) pos %= end;
    } else if (pos < 0) {
        pos += end;
        if (pos < 0) pos = end - 1 - ((-pos - 1) % end);
    }
    return pos;
}

// How many samples from `pos` on, at `step`, read the recording between `lo` and `hi` (whole
// samples, hi not included) with no end in the way: the plain stretch.
inline int clearRun(std::int64_t pos, std::int64_t step, std::int64_t lo, std::int64_t hi, int most) {
    const std::int64_t at = pos >> 32;
    if (at < lo || at >= hi) return 0;
    std::int64_t n;
    if (step > 0) n = (((hi << 32) - pos) + step - 1) / step; // samples before it reaches hi
    else n = (pos - (lo << 32)) / (-step) + 1;               // samples while it is at lo or over
    return n < most ? static_cast<int>(n) : most;
}

// One grain's run of `m` samples into its key's sums, on locals (WEAR's lesson), copied back. The
// window is worked out first over the run; then each sample read into it, in stretches with no end
// of the recording in them (no test a sample), and a sample at a time where one is; then the run
// into the sums. The window, the plain reads and the sums are loops the compiler takes four at a
// time; only the Hermite read goes one by one.
template <bool kPlain>
void runGrain(const float* __restrict d, int length, float* __restrict left, float* __restrict right, int m, long& kRef, long grainLength, long rise,
              float invRise, float invFall, std::int64_t& posRef, std::int64_t step, bool linear, float gl, float gr) {
    long k = kRef;
    std::int64_t pos = posRef;
    const std::int64_t end = static_cast<std::int64_t>(length) * kWhole;
    float w[kPiece];
    int i = 0;
    while (i < m) {
        const bool rising = k < rise;
        const int run = rising ? static_cast<int>(rise - k < m - i ? rise - k : m - i) : m - i;
        // x on the half cosine: k / rise up, (length - k) / fall down.
        windowRun(w + i, run, rising ? 0.f : static_cast<float>(grainLength) * invFall, rising ? invRise : -invFall, static_cast<int>(k));
        i += run;
        k += run;
    }
    for (i = 0; i < m;) {
        // Hermite reads a sample either side and two after: the stretch keeps all four inside.
        const int clear = kPlain ? clearRun(pos, step, 0, length, m - i) : clearRun(pos, step, 1, length - 2, m - i);
        if (clear > 0) {
            if (kPlain) {
                const float* __restrict from = d + (pos >> 32);
                const int dir = step > 0 ? 1 : -1;
                if (dir > 0)
                    for (int j = 0; j < clear; ++j) w[i + j] *= from[j];
                else
                    for (int j = 0; j < clear; ++j) w[i + j] *= from[-j];
                pos += static_cast<std::int64_t>(clear) * step;
            } else {
                // The four points and the fraction gathered one by one; the curve four at a time.
                float xm1[kPiece], x0[kPiece], x1[kPiece], x2[kPiece];
                std::int32_t part[kPiece]; // the fraction, in 24 bits
                for (int j = 0; j < clear; ++j, pos += step) {
                    const float* p = d + (pos >> 32);
                    xm1[j] = p[-1], x0[j] = p[0], x1[j] = p[1], x2[j] = p[2];
                    part[j] = static_cast<std::int32_t>((pos & 0xFFFFFFFF) >> 8);
                }
                float* __restrict wr = w + i;
                if (linear)
                    for (int j = 0; j < clear; ++j) wr[j] *= x0[j] + static_cast<float>(part[j]) * (1.f / 16777216.f) * (x1[j] - x0[j]);
                else
                    for (int j = 0; j < clear; ++j) wr[j] *= hermite(xm1[j], x0[j], x1[j], x2[j], static_cast<float>(part[j]) * (1.f / 16777216.f));
            }
            i += clear;
            if (pos >= end || pos < 0) pos = stepOn(pos - step, step, end); // (the last step of the stretch landed on an end)
            continue;
        }
        // A sample by an end: read round it.
        const int i0 = static_cast<int>(pos >> 32);
        float v;
        if (kPlain) {
            v = d[i0];
        } else {
            const float f = static_cast<float>(static_cast<std::int32_t>((pos & 0xFFFFFFFF) >> 8)) * (1.f / 16777216.f);
            const float x1 = d[wrapIndex(i0 + 1, length)];
            v = linear ? d[i0] + f * (x1 - d[i0]) : hermite(d[wrapIndex(i0 - 1, length)], d[i0], x1, d[wrapIndex(i0 + 2, length)], f);
        }
        w[i] *= v;
        pos = stepOn(pos, step, end);
        ++i;
    }
    // Into the key's sums, on its side: four at a time.
    for (i = 0; i < m; ++i) {
        left[i] += w[i] * gl;
        right[i] += w[i] * gr;
    }
    kRef = k;
    posRef = pos;
}

} // namespace

// Every grain sounding, from `from` to `to` in the piece, in the pool's order.
void Cloud::State::renderGrains(int from, int to) {
    if (to <= from || grainCount == 0) return;
    int length = 0;
    const float* d = source(&length);
    for (Grain& g : grains) {
        if (g.key < 0) continue;
        const int m = static_cast<int>(g.left < to - from ? g.left : to - from);
        if (g.plain)
            runGrain<true>(d, length, sumL[g.key] + from, sumR[g.key] + from, m, g.k, g.length, g.rise, g.invRise, g.invFall, g.pos, g.step, g.linear, g.gl, g.gr);
        else
            runGrain<false>(d, length, sumL[g.key] + from, sumR[g.key] + from, m, g.k, g.length, g.rise, g.invRise, g.invFall, g.pos, g.step, g.linear, g.gl, g.gr);
        g.left -= m;
        if (g.left <= 0) freeGrain(g);
    }
}

void Cloud::State::piece(float* left, float* right, int n) {
    bool any = false;
    for (int k = 0; k < kKeySlots; ++k) {
        Key& key = keys[k];
        dies[k] = kPiece + 1;
        if (!key.on) continue;
        any = true;
        float* e = env[k];
        int i = 0;
        for (; i < n; ++i) {
            if (key.fading) {
                e[i] = key.env.next(shape) * (static_cast<float>(key.fadeLeft) * fadeStep);
                if (--key.fadeLeft <= 0) { ++i; break; }
            } else {
                const float v = key.env.next(shape);
                if (key.env.idle()) break;
                e[i] = v;
            }
        }
        if (i < n || (key.fading && key.fadeLeft <= 0)) {
            dies[k] = i;
            for (; i < n; ++i) e[i] = 0.f;
        }
    }
    if (!any) return;
    for (int k = 0; k < kKeySlots; ++k)
        if (keys[k].on)
            for (int i = 0; i < n; ++i) sumL[k][i] = sumR[k][i] = 0.f;
    // A key that ends in this piece takes its grains with it.
    for (Grain& g : grains)
        if (g.key >= 0 && dies[g.key] < n && g.left > dies[g.key]) g.left = dies[g.key];
    int cursor = 0;
    for (;;) {
        int t = n;
        for (int k = 0; k < kKeySlots; ++k) {
            const Key& key = keys[k];
            if (!key.on || key.fading) continue;
            const long at = key.nextOnset - key.clock;
            if (at < t && at < dies[k]) t = static_cast<int>(at);
        }
        renderGrains(cursor, t);
        if (t >= n) break;
        for (int k = 0; k < kKeySlots; ++k) {
            const Key& key = keys[k];
            if (key.on && !key.fading && key.nextOnset - key.clock == t && t < dies[k]) startGrain(k, t, n);
        }
        cursor = t;
    }
    // Each key's grains through its envelope, in the keys' order.
    for (int k = 0; k < kKeySlots; ++k) {
        if (!keys[k].on) continue;
        for (int i = 0; i < n; ++i) {
            left[i] += sumL[k][i] * env[k][i];
            right[i] += sumR[k][i] * env[k][i];
        }
    }
    for (int k = 0; k < kKeySlots; ++k) {
        if (!keys[k].on) continue;
        keys[k].clock += n;
        if (dies[k] <= n) endKey(k);
    }
}

// ---- the synth -----------------------------------------------------------------------------

Cloud::Cloud() : state_(new State) {
    state_->patch = presetPatch(0);
    prepare(48000.f);
}
Cloud::~Cloud() = default;

int Cloud::presetCount() { return kPresetCount; }
const char* Cloud::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Cloud::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
CloudPatch Cloud::presetPatch(int i) {
    CloudPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kSettings; ++id) patch.value[id] = settingHeld(id, preset.value[id]);
    return patch;
}

const std::vector<float>& Cloud::tone(float sampleRate) {
    struct Kept {
        float rate;
        std::vector<float> samples;
    };
    static std::mutex mutex;
    // Never taken apart: an engine may still be playing from one as the program ends.
    static std::vector<std::unique_ptr<Kept>>& kept = *new std::vector<std::unique_ptr<Kept>>;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& k : kept)
        if (k->rate == sampleRate) return k->samples;
    kept.emplace_back(new Kept{sampleRate, {}});
    std::vector<float>& v = kept.back()->samples;
    // Three soft partials about middle C, two of them beating slowly, and a little air. Every
    // partial is a whole number of cycles in the 2 s, so the TONE goes round without a seam.
    const long n = std::lround(kToneSeconds * sampleRate);
    v.resize(static_cast<std::size_t>(n > 1 ? n : 1));
    const double twoPi = 6.283185307179586;
    const double seconds = static_cast<double>(v.size()) / sampleRate;
    const double cycles[4] = {std::round(261.5 * seconds), std::round(262.0 * seconds), std::round(523.5 * seconds), std::round(785.0 * seconds)};
    const double size[4] = {1.0, 0.6, 0.35, 0.15};
    double air = 0.0, most = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(v.size());
        double s = 0.0;
        for (int p = 0; p < 4; ++p) s += size[p] * std::sin(twoPi * cycles[p] * t);
        const double white = static_cast<double>(chordHash(static_cast<std::uint32_t>(i) * 0x9E3779B1u + 0x1234567u) >> 8) / 8388608.0 - 1.0;
        air += 0.2 * (white - air);
        s += 0.05 * air;
        v[i] = static_cast<float>(s);
        most = std::fabs(s) > most ? std::fabs(s) : most;
    }
    for (float& x : v) x = static_cast<float>(x * (0.5 / most));
    return v;
}

void Cloud::prepare(float sampleRate) {
    State& s = *state_;
    s.rate = sampleRate > 0.f ? sampleRate : 48000.f;
    s.toneData = &tone(s.rate);
    s.fadeSamples = std::lround(kFadeSeconds * s.rate);
    if (s.fadeSamples < 1) s.fadeSamples = 1;
    s.fadeStep = 1.f / static_cast<float>(s.fadeSamples);
    for (int k = 0; k < kKeySlots; ++k) s.endKey(k);
    s.seedGiven = false;
    s.liveCount = 0;
    s.dirty = true;
    s.refresh();
}

void Cloud::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    state_->dirty = true;
}

void Cloud::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = settingHeld(id, value);
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    state_->dirty = true;
}

void Cloud::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return;
    state_->macro[macro] = value;
    state_->dirty = true;
}

void Cloud::noteSeed(std::uint32_t seed) {
    state_->nextSeed = seed;
    state_->seedGiven = true;
}

void Cloud::noteOn(int note, float velocity) {
    State& s = *state_;
    if (note < 0 || note > 127 || !(velocity > 0.f)) {
        s.seedGiven = false;
        return;
    }
    if (s.dirty) s.refresh();
    const std::uint32_t seed = s.seedGiven ? s.nextSeed : liveSeed(s.liveCount++);
    s.seedGiven = false;
    // The same key sounding: struck again in its own slot (its grains go on). Else a free slot,
    // after taking the oldest let-go key (else the oldest) when six sound.
    int slot = -1, sounding = 0;
    for (int k = 0; k < kKeySlots; ++k) {
        const State::Key& key = s.keys[k];
        if (!key.on || key.fading) continue;
        ++sounding;
        if (key.note == note) slot = k;
    }
    if (slot < 0 && sounding >= kKeys) {
        int victim = -1;
        for (int pass = 0; pass < 2 && victim < 0; ++pass)
            for (int k = 0; k < kKeySlots; ++k) {
                const State::Key& key = s.keys[k];
                if (!key.on || key.fading || (pass == 0 && !key.released)) continue;
                if (victim < 0 || key.age < s.keys[victim].age) victim = k;
            }
        s.fadeOut(s.keys[victim]);
    }
    if (slot < 0) {
        for (int k = 0; k < kKeySlots && slot < 0; ++k)
            if (!s.keys[k].on) slot = k;
        if (slot < 0) { // every slot fading: the one nearest its end goes now
            for (int k = 0; k < kKeySlots; ++k)
                if (slot < 0 || s.keys[k].fadeLeft < s.keys[slot].fadeLeft) slot = k;
            s.endKey(slot);
        }
        s.keys[slot] = State::Key();
    }
    State::Key& key = s.keys[slot];
    key.on = true;
    key.released = false;
    key.note = note;
    key.env.on(); // from where it is: a key struck again does not click
    key.seed = seed;
    key.n = 0;
    key.clock = 0;
    key.nextOnset = 0;
    key.lastOnset = 0;
    key.scanned = 0.0;
    key.velocity = static_cast<float>(velocityGain(s.patch.value[Velocity], velocity > 1.f ? 1.0 : static_cast<double>(velocity)));
    key.slideFrom = 0.0;
    key.slideLength = 0;
    key.age = ++s.ageClock;
}

void Cloud::noteOnFrom(int note, float velocity, int from, float seconds) {
    noteOn(note, velocity);
    State& s = *state_;
    if (from < 0 || from > 127 || from == note || !(seconds > 0.f) || seconds > 3600.f) return;
    for (State::Key& key : s.keys)
        if (key.on && !key.fading && key.note == note && key.clock == 0) {
            key.slideFrom = static_cast<double>(from - note);
            key.slideLength = std::lround(static_cast<double>(seconds) * s.rate);
        }
}

void Cloud::noteOff(int note) {
    for (State::Key& key : state_->keys)
        if (key.on && !key.fading && !key.released && key.note == note) {
            key.env.off();
            key.released = true;
        }
}

void Cloud::stopNotes() {
    for (State::Key& key : state_->keys)
        if (key.on && !key.fading && !key.released) {
            key.env.off();
            key.released = true;
        }
    state_->seedGiven = false;
}

void Cloud::allNotesOff() {
    State& s = *state_;
    for (State::Key& key : s.keys)
        if (key.on && !key.fading) s.fadeOut(key);
    s.liveCount = 0;
    s.seedGiven = false;
}

void Cloud::setSample(const float* data, int length) {
    State& s = *state_;
    s.data = length > 0 ? data : nullptr;
    s.length = s.data ? length : 0;
    for (State::Grain& g : s.grains) // they were reading the old recording, which goes to the trash
        if (g.key >= 0) s.freeGrain(g);
}

void Cloud::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    if (s.dirty) s.refresh();
    for (int done = 0; done < frames;) {
        const int n = frames - done < kPiece ? frames - done : kPiece;
        s.piece(left + done, right + done, n);
        s.total += n;
        done += n;
    }
}

int Cloud::grainPlaces(float* places, int most) const {
    const State& s = *state_;
    int length = 0;
    s.source(&length);
    int count = 0;
    for (const State::Grain& g : s.grains) {
        if (g.key < 0 || count >= most) continue;
        places[count++] = length > 0 ? static_cast<float>(static_cast<double>(g.pos) / static_cast<double>(kWhole) / length) : 0.f;
    }
    return count;
}

int Cloud::keysSounding() const {
    int n = 0;
    for (const State::Key& key : state_->keys) n += key.on ? 1 : 0;
    return n;
}
int Cloud::grainsSounding() const { return state_->grainCount; }
long Cloud::grainsStarted() const { return state_->started; }
long Cloud::grainsSkipped() const { return state_->skipped; }
bool Cloud::onTone() const { return !state_->data; }

int Cloud::lastStarted(Started* out, int most) const {
    const State& s = *state_;
    long count = s.started < kLogged ? s.started : kLogged;
    if (most < count) count = most;
    for (long i = 0; i < count; ++i) out[i] = s.log[(s.started - count + i) % kLogged];
    return static_cast<int>(count);
}

float Cloud::smallestState() const {
    float least = 0.f;
    for (const State::Key& key : state_->keys) {
        if (!key.on) continue;
        const float fade = key.fading ? static_cast<float>(key.fadeLeft) * state_->fadeStep : 1.f;
        for (float v : {key.env.level, fade})
            if (v > 0.f && (least == 0.f || v < least)) least = v;
    }
    return least;
}

} // namespace apotheca
