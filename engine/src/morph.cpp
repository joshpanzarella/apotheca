// MORPH: a wavetable synth (docs/plans/wavetable-plan.md; morph.hpp says what it does, morph_rule.hpp
// holds the figures, morph/tables.hpp the tables).
//
// A render: every setting's slide is where it is; each key in its slot's order adds itself into the
// block, a tick at a time (kTick samples counted from its strike), its samples worked on locals; then
// the slides move on by the block's length. So each sample is the same sum in the same order however
// the sound is cut into blocks.

#include "apotheca/morph.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "apotheca/envelope.hpp"
#include "jx3p/ir3109.hpp" // exp2Poly, sinPoly, cosPoly: the house's own polynomials
#include "morph/tables.hpp"

// (No multiply and add joined: the handheld's bits are the desk's.)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace apotheca {

using namespace morph;
using jx3p::exp2Poly;
using jx3p::sinPoly;
using jx3p::cosPoly;

namespace {

struct Preset {
    const char* name;
    const char* category;
    int value[kSettings];
};
// The engine's own: the BASIC folder, one table each played plainly. The plan's other folders (PADS,
// LEADS, BASS, MOVING) are data in the app (batch 3). Preset 0 is a new track's. Nobody has heard any.
//   TA  POSA TB POSB MIX SEMI FINE SUB OCT TUNE UNI DET WID FILT CUT RES KEY  ATK DEC SUS REL  EATK EDEC ESUS EREL EPOS ECUT VEL LEVEL
const Preset kPresets[] = {
    {"Analog Sweep", "BASIC", {Analog, 30, 0, 0, 50, 0, 0, 0, 0, 0, 1, 20, 50, kLow12, 85, 10, 50, 8, 50, 100, 50, 8, 60, 0, 50, 20, 0, 50, 0}},
    {"Pulse Width", "BASIC", {Pulse, 15, 0, 0, 50, 0, 0, 0, 0, 0, 1, 20, 50, kLow24, 75, 20, 50, 8, 50, 100, 45, 8, 70, 0, 50, 45, 0, 50, 0}},
    {"Sync Rise", "BASIC", {Sync, 0, 0, 0, 50, 0, 0, 20, 0, 0, 1, 20, 50, kLow12, 90, 0, 50, 8, 50, 100, 40, 50, 80, 40, 50, 60, 0, 50, -2}},
    {"Vowel Talk", "BASIC", {Vowel, 40, 1 + Vowel, 80, 30, 0, 7, 0, 0, 0, 2, 25, 60, kLow12, 95, 0, 50, 20, 50, 100, 55, 40, 75, 30, 55, 30, 0, 50, -3}},
    {"Organ Stops", "BASIC", {Organ, 60, 0, 0, 50, 0, 0, 30, 0, 0, 1, 20, 50, kFilterOff, 100, 0, 50, 4, 50, 100, 30, 8, 60, 0, 50, 0, 0, 30, -4}},
    {"Harmonic Walk", "BASIC", {Harmonic, 5, 0, 0, 50, 0, 0, 0, 0, 0, 1, 20, 50, kLow12, 85, 0, 50, 8, 50, 100, 50, 8, 75, 0, 50, 50, 0, 50, 0}},
    {"Fold Grow", "BASIC", {Fold, 15, 0, 0, 50, 0, 0, 0, 0, 0, 1, 20, 50, kLow24, 70, 30, 50, 8, 50, 100, 45, 30, 70, 20, 50, 50, 15, 50, -2}},
    {"FM Bell", "BASIC", {Fm, 45, 1 + Bell, 20, 40, 12, 0, 0, 0, 0, 1, 20, 50, kLow12, 100, 0, 50, 4, 70, 0, 65, 4, 65, 0, 60, -40, 0, 60, 0}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return !(v > lo) ? lo : (v > hi ? hi : v); }

// The settings whose value slides in (every one read while a key sounds; the choices and the
// envelopes' times do not).
bool slides(int id) {
    switch (id) {
    case TableA: case TableB: case Unison: case Filter: case Octave:
    case Attack: case Decay: case Sustain: case Release:
    case EnvAttack: case EnvDecay: case EnvSustain: case EnvRelease: case Velocity:
        return false;
    default:
        return true;
    }
}

// A sine of a turn given as a 32-bit phase (the SUB): folded into a quarter turn either side, then
// a float polynomial (sin to its x^11 term: under 1e-7 there).
inline float phaseSine(std::uint32_t phase) {
    float x = static_cast<float>(static_cast<std::int32_t>(phase)) * (1.f / 4294967296.f); // -0.5 .. 0.5
    if (x > 0.25f) x = 0.5f - x;
    else if (x < -0.25f) x = -0.5f - x;
    const float y = x * 6.28318531f, y2 = y * y;
    return y * (1.f + y2 * (-0.166666667f + y2 * (0.00833333333f + y2 * (-0.000198412698f + y2 * (2.75573192e-6f + y2 * -2.50521084e-8f)))));
}

constexpr double kLog2Of1000 = 9.965784284662087; // CUTOFF: 20 Hz times 1000 at the top
constexpr double kLog2Of40 = 5.321928094887363;   // RESONANCE: Q 0.5 times 40 at the top
constexpr float kSettleTiny = 1e-20f;             // a filter state under this is nothing (fx/svf.hpp's kTiny)
constexpr float kEnvLands = 1e-9f;                // an envelope this near its sustain is on it (-180 dB)

// The sizes' bits and places in a frame, worked out once (morph_rule.hpp's sums walk loops).
struct Sizes {
    int bits[kSizes];
    long offset[kSizes];
    long stride;
    Sizes() {
        for (int m = 0; m < kSizes; ++m) bits[m] = sizeBits(m), offset[m] = sizeOffset(m);
        stride = frameFloats();
    }
};
const Sizes& sizes() {
    static const Sizes s;
    return s;
}

} // namespace

struct Morph::State {
    float rate = 48000.f;
    const Table* tables[kTables] = {};
    MorphPatch patch;
    float macro[kMacros];
    bool dirty = true;
    struct Slid {
        float from = 0.f, to = 0.f;
        long since = kMacroSlideSamples;
        float at(long ahead) const { return macroSlid(from, to, since + ahead); }
    };
    Slid slid[kSettings];      // the patch with the macros on it, slid in
    bool sliding = false;      // any of them still on its way
    int choice[kSettings] = {}; // the same, for the settings that do not slide
    envelope::Shape amp, mod; // the MOD ENV's shape is a tick's: it steps once a tick
    long fadeSamples = 240;
    float fadeStep = 1.f / 240.f;

    struct Stream {
        std::uint32_t phase = 0, inc = 0;
        float ratio = 1.f; // its DETUNE against the key's pitch
        float gl = 1.f, gr = 1.f; // its side (stereo keys)
    };
    struct FilterState {
        float ic1 = 0.f, ic2 = 0.f, jc1 = 0.f, jc2 = 0.f; // two stages (LOW 24 uses the second)
    };
    struct Key {
        bool on = false, fading = false, released = false, stereo = false, bOn = false;
        int note = -1, unison = 1, filter = kLow12;
        const Table* ta = nullptr;
        const Table* tb = nullptr;
        float velocity = 1.f;
        unsigned age = 0;
        std::uint32_t seed = 0;
        long clock = 0, fadeLeft = 0;
        envelope::Envelope amp, mod;
        int tickLeft = 0;
        bool first = true;
        // Where the tick is going, and how far a sample.
        float placeA = 0.f, placeB = 0.f, dA = 0.f, dB = 0.f;
        float gA = 0.f, gB = 0.f, gSub = 0.f, gOut = 0.f;
        float dgA = 0.f, dgB = 0.f, dgSub = 0.f, dgOut = 0.f;
        int sizeA = 0, sizeB = 0;
        // What the pitch was last worked out from (worked out again only when it moves).
        double semis = 1e9, bCents = 1e9, detune = -1.0, width = -1.0;
        double hz = 0.0, bRatioNow = 1.0;
        Stream a[kUnisonMost], b[kUnisonMost];
        std::uint32_t subPhase = 0, subInc = 0;
        // The filter: its numbers for the tick, and its memory, a side each.
        float cutoffWas = -1.f, resWas = -1.f, levelDb = 1e9f, levelGain = 1.f;
        double octavesWas = -1e9;
        float a1 = 1.f, a2 = 0.f, a3 = 0.f, k = 2.f, b1 = 1.f, b2 = 0.f, b3 = 0.f, k1 = 1.41421356f;
        FilterState fl, fr;
        double slideFrom = 0.0;
        long slideLength = 0;
    };
    Key keys[kKeySlots];
    std::uint32_t nextSeed = 0, liveCount = 0;
    bool seedGiven = false;
    unsigned ageClock = 0;

    State() {
        for (float& m : macro) m = 0.5f;
    }

    bool anyOn() const {
        for (const Key& key : keys)
            if (key.on) return true;
        return false;
    }

    // The patch with the macros on it; a value that changed slides in from where it is (at once
    // when no key sounds).
    void refresh() {
        float eff[kSettings];
        for (int id = 0; id < kSettings; ++id) eff[id] = static_cast<float>(patch.value[id]);
        for (int m = 0; m < kMacros; ++m)
            for (int id : {macroSetting(m), macroAlsoBends(m)}) {
                if (id < 0) continue;
                eff[id] = macroBent(static_cast<float>(patch.value[id]), macro[m], static_cast<float>(settingMin(id)), static_cast<float>(settingMax(id)));
            }
        const bool sounding = anyOn();
        for (int id = 0; id < kSettings; ++id) {
            choice[id] = patch.value[id];
            if (!slides(id)) continue;
            Slid& s = slid[id];
            if (!sounding) {
                s.from = s.to = eff[id];
                s.since = kMacroSlideSamples;
            } else if (eff[id] != s.to) {
                s.from = s.at(0);
                s.to = eff[id];
                s.since = 0;
                sliding = true;
            }
        }
        // RELEASE is a macro: its slid value is read here, where the shape is worked out.
        amp.setFrom(static_cast<float>(patch.value[Attack]), static_cast<float>(patch.value[Decay]), static_cast<float>(patch.value[Sustain]), slid[Release].to, rate);
        mod.setFrom(static_cast<float>(patch.value[EnvAttack]), static_cast<float>(patch.value[EnvDecay]), static_cast<float>(patch.value[EnvSustain]),
                    static_cast<float>(patch.value[EnvRelease]), rate / kTick);
        dirty = false;
    }

    int streamsHeld() const {
        int n = 0;
        for (const Key& key : keys)
            if (key.on && !key.fading) n += streamsPerKey(key.unison, key.bOn);
        return n;
    }
    void fadeOut(Key& key) {
        key.fading = true;
        key.fadeLeft = fadeSamples;
    }

    void startTick(Key& key, long ahead);
    void runKey(Key& key, float* left, float* right, int frames);
};

// ---- a tick ------------------------------------------------------------------------------------------

void Morph::State::startTick(Key& key, long ahead) {
    // Every value at the tick's end (the samples it is slid to), `ahead` samples into this render.
    const long end = ahead + kTick;
    // (Nothing sliding: every value is where it is going, as at() would say.)
    const bool moving = sliding;
    auto val = [&](int id) { return moving ? slid[id].at(end) : slid[id].to; };
    // The MOD ENV steps a tick; both envelopes land on their sustain here (a decay to 0 would
    // otherwise go on into numbers too small to hold).
    key.mod.next(mod);
    if (key.mod.stage == envelope::Envelope::Decay && std::fabs(key.mod.level - mod.sustain) < kEnvLands) key.mod.level = mod.sustain;
    if (key.amp.stage == envelope::Envelope::Decay && std::fabs(key.amp.level - amp.sustain) < kEnvLands) key.amp.level = amp.sustain;
    const float env = key.mod.level;
    // POSITION: the setting, the macro's bend, the MOD ENV.
    const int fa = key.ta->frames;
    float placeA = static_cast<float>(positionPlace(val(PositionA), fa) + envPositionFrames(val(EnvPosition), fa) * env);
    placeA = clampf(placeA, 0.f, static_cast<float>(fa - 1));
    float placeB = 0.f;
    if (key.bOn) {
        const int fb = key.tb->frames;
        placeB = clampf(static_cast<float>(positionPlace(val(PositionB), fb) + envPositionFrames(val(EnvPosition), fb) * env), 0.f,
                        static_cast<float>(fb - 1));
    }
    // The gains: A and B straight across MIX, each over the square root of its streams.
    const float spread = 1.f / std::sqrt(static_cast<float>(key.unison));
    const float mix = static_cast<float>(mixShare(val(Mix)));
    const float gA = (key.bOn ? 1.f - mix : 1.f) * spread, gB = key.bOn ? mix * spread : 0.f;
    const float gSub = static_cast<float>(subGain(val(Sub)));
    const float levelDb = val(Level);
    if (levelDb != key.levelDb) {
        key.levelDb = levelDb;
        key.levelGain = static_cast<float>(exp2Poly(clampf(levelDb, -24.f, 6.f) * (1.0 / 6.020599913279624)));
    }
    const float gOut = kOutputGain * key.levelGain * key.velocity;
    if (key.first) {
        key.placeA = placeA, key.placeB = placeB;
        key.gA = gA, key.gB = gB, key.gSub = gSub, key.gOut = gOut;
        key.dA = key.dB = key.dgA = key.dgB = key.dgSub = key.dgOut = 0.f;
    } else {
        const float per = 1.f / kTick;
        key.dA = (placeA - key.placeA) * per, key.dB = (placeB - key.placeB) * per;
        key.dgA = (gA - key.gA) * per, key.dgB = (gB - key.gB) * per, key.dgSub = (gSub - key.gSub) * per, key.dgOut = (gOut - key.gOut) * per;
    }
    // The pitch, worked out again only where it moved.
    const long at = key.clock + kTick;
    const double slide = key.slideLength > 0 && at < key.slideLength ? key.slideFrom * (1.0 - static_cast<double>(at) / static_cast<double>(key.slideLength)) : 0.0;
    const double semis = key.note - 69.0 + 12.0 * clampi(choice[Octave], -2, 2) + val(Tune) / 100.0 + slide;
    bool moved = false;
    if (semis != key.semis) {
        key.semis = semis;
        key.hz = 440.0 * exp2Poly(semis / 12.0);
        moved = true;
    }
    const double bc = bCents(0, 0) + 100.0 * val(BSemi) + val(BFine);
    if (bc != key.bCents) {
        key.bCents = bc;
        key.bRatioNow = exp2Poly(bc / 1200.0);
        moved = true;
    }
    const double detune = detuneCents(val(Detune));
    if (detune != key.detune) {
        key.detune = detune;
        for (int i = 0; i < key.unison; ++i) key.a[i].ratio = key.b[i].ratio = static_cast<float>(exp2Poly(streamCents(i, key.unison, detune) / 1200.0));
        moved = true;
    }
    if (key.stereo) {
        const double width = widthShare(val(Width));
        if (width != key.width) {
            key.width = width;
            for (int i = 0; i < key.unison; ++i) {
                const double side = streamSide(i, key.unison, width), angle = (side + 1.0) * (3.14159265358979323846 / 4.0);
                key.a[i].gl = key.b[i].gl = static_cast<float>(1.4142135623730951 * cosPoly(angle));
                key.a[i].gr = key.b[i].gr = static_cast<float>(1.4142135623730951 * sinPoly(angle));
            }
        }
    }
    if (moved) {
        double top = 0.0;
        for (int i = 0; i < key.unison; ++i) {
            const double hz = key.hz * key.a[i].ratio;
            key.a[i].inc = phaseStep(hz, rate);
            top = hz > top ? hz : top;
        }
        key.sizeA = sizeFor(top, rate);
        if (key.bOn) {
            top = 0.0;
            for (int i = 0; i < key.unison; ++i) {
                const double hz = key.hz * key.bRatioNow * key.b[i].ratio;
                key.b[i].inc = phaseStep(hz, rate);
                top = hz > top ? hz : top;
            }
            key.sizeB = sizeFor(top, rate);
        }
        key.subInc = phaseStep(0.5 * key.hz, rate);
    }
    // The filter's numbers, when its cutoff or its resonance moved.
    if (key.filter != kFilterOff) {
        const double octaves = kLog2Of1000 * dialHeld(val(Cutoff)) / 100.0 + keyTrackOctaves(val(KeyTrack), key.note) + envCutoffOctaves(val(EnvCutoff)) * env;
        const float res = val(Resonance);
        if (octaves != key.octavesWas || res != key.resWas) {
            key.octavesWas = octaves;
            double hz = kCutoffLeastHz * exp2Poly(octaves);
            const double most = 0.45 * rate;
            hz = hz < kCutoffLeastHz ? kCutoffLeastHz : (hz > most ? most : hz);
            key.cutoffWas = static_cast<float>(hz);
            key.resWas = res;
            const double w = 3.14159265358979323846 * hz / rate;
            const float g = static_cast<float>(sinPoly(w) / cosPoly(w));
            const float k = static_cast<float>(1.0 / (kQLeast * exp2Poly(kLog2Of40 * dialHeld(res) / 100.0)));
            key.k = k;
            key.a1 = 1.f / (1.f + g * (g + k));
            key.a2 = g * key.a1;
            key.a3 = g * key.a2;
            key.b1 = 1.f / (1.f + g * (g + key.k1));
            key.b2 = g * key.b1;
            key.b3 = g * key.b2;
        }
        for (FilterState* f : {&key.fl, &key.fr}) {
            if (std::fabs(f->ic1) < kSettleTiny) f->ic1 = 0.f;
            if (std::fabs(f->ic2) < kSettleTiny) f->ic2 = 0.f;
            if (std::fabs(f->jc1) < kSettleTiny) f->jc1 = 0.f;
            if (std::fabs(f->jc2) < kSettleTiny) f->jc2 = 0.f;
        }
    }
    key.first = false;
    key.tickLeft = kTick;
}

namespace {

// One stream's read: the two frames either side of the place, each straight between two points.
inline float readPair(const float* __restrict a, const float* __restrict b, std::uint32_t phase, int bits, float toward) {
    const std::uint32_t i = readIndex(phase, bits);
    const float f = readFraction(phase, bits);
    const float x = a[i] + f * (a[i + 1] - a[i]);
    const float y = b[i] + f * (b[i + 1] - b[i]);
    return x + toward * (y - x);
}

// A trapezoidal state-variable filter's step (fx/svf.hpp's arithmetic): the band (v1) and the low (v2).
inline void svfStep(float a1, float a2, float a3, float in, float& ic1, float& ic2, float& v1, float& v2) {
    const float v3 = in - ic2;
    v1 = a1 * ic1 + a2 * v3;
    v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
}

} // namespace

// One key into the block: a tick at a time. Each run (up to a tick's end) is worked as small arrays
// on locals, one job at a time (WEAR's lesson: the compiler keeps nothing of the key's own in
// registers across the writes to the block): the envelopes, then the places and gains, then each
// stream's reads, then the filter, then into the block. Every value moves sample by sample in the
// same order however the run is cut, so the sound is the same in any blocks.
void Morph::State::runKey(Key& key, float* left, float* right, int frames) {
    const Sizes& sz = sizes();
    const long stride = sz.stride;
    for (int i = 0; i < frames && key.on;) {
        if (key.tickLeft == 0) startTick(key, i);
        int m = key.tickLeft < frames - i ? key.tickLeft : frames - i;
        // The envelopes, on copies; where the key ends, if it does in this run.
        float e[kTick];
        {
            envelope::Envelope a = key.amp;
            long fadeLeft = key.fadeLeft;
            const bool fading = key.fading;
            int j = 0;
            for (; j < m; ++j) {
                if (fading) {
                    e[j] = a.next(amp) * (static_cast<float>(fadeLeft) * fadeStep);
                } else {
                    e[j] = a.next(amp);
                }
                if (fading ? --fadeLeft <= 0 : a.idle()) {
                    key.on = false;
                    ++j;
                    break;
                }
            }
            m = j;
            key.amp = a, key.fadeLeft = fadeLeft;
        }
        const int unison = key.unison, filter = key.filter;
        const bool bOn = key.bOn, stereo = key.stereo;
        // The places and the gains, sample by sample.
        float gA[kTick], gB[kTick], gS[kTick], gO[kTick];
        const float* fa0[kTick];
        const float* fb0[kTick];
        float ta[kTick], tb[kTick];
        {
            float p = key.placeA, q = key.placeB, x = key.gA, y = key.gB, z = key.gSub, w = key.gOut;
            const float dA = key.dA, dB = key.dB, dgA = key.dgA, dgB = key.dgB, dgS = key.dgSub, dgO = key.dgOut;
            const float* baseA = key.ta->data.data() + sz.offset[key.sizeA];
            const float* baseB = bOn ? key.tb->data.data() + sz.offset[key.sizeB] : nullptr;
            const int lastA = key.ta->frames - 2, lastB = bOn ? key.tb->frames - 2 : 0;
            for (int j = 0; j < m; ++j) {
                p += dA, x += dgA, y += dgB, z += dgS, w += dgO;
                gA[j] = x, gB[j] = y, gS[j] = z, gO[j] = w;
                int ja = static_cast<int>(p);
                if (ja > lastA) ja = lastA;
                ta[j] = p - static_cast<float>(ja);
                fa0[j] = baseA + ja * stride;
                if (bOn) {
                    q += dB;
                    int jb = static_cast<int>(q);
                    if (jb > lastB) jb = lastB;
                    tb[j] = q - static_cast<float>(jb);
                    fb0[j] = baseB + jb * stride;
                }
            }
            key.placeA = p, key.placeB = q, key.gA = x, key.gB = y, key.gSub = z, key.gOut = w;
        }
        // The streams: A's into accA (a side each when wide), B's into accB.
        float aL[kTick] = {}, aR[kTick] = {}, bL[kTick] = {}, bR[kTick] = {};
        const int bitsA = sz.bits[key.sizeA], bitsB = sz.bits[key.sizeB];
        for (int osc = 0; osc < (bOn ? 2 : 1); ++osc) {
            Stream* streams = osc ? key.b : key.a;
            const float* const* f0 = osc ? fb0 : fa0;
            const float* t = osc ? tb : ta;
            float* accL = osc ? bL : aL;
            float* accR = osc ? bR : aR;
            const int bits = osc ? bitsB : bitsA;
            // The pair of frames is mostly the same for the whole run: then one pointer.
            const bool onePair = m > 0 && f0[0] == f0[m - 1];
            const float* p0 = m > 0 ? f0[0] : nullptr;
            const float* p1 = p0 + stride;
            for (int s = 0; s < unison; ++s) {
                std::uint32_t phase = streams[s].phase;
                const std::uint32_t inc = streams[s].inc;
                if (onePair && !stereo) {
                    for (int j = 0; j < m; ++j) {
                        accL[j] += readPair(p0, p1, phase, bits, t[j]);
                        phase += inc;
                    }
                } else if (onePair) {
                    const float l = streams[s].gl, r = streams[s].gr;
                    for (int j = 0; j < m; ++j) {
                        const float v = readPair(p0, p1, phase, bits, t[j]);
                        phase += inc;
                        accL[j] += v * l;
                        accR[j] += v * r;
                    }
                } else if (stereo) {
                    const float l = streams[s].gl, r = streams[s].gr;
                    for (int j = 0; j < m; ++j) {
                        const float v = readPair(f0[j], f0[j] + stride, phase, bits, t[j]);
                        phase += inc;
                        accL[j] += v * l;
                        accR[j] += v * r;
                    }
                } else {
                    for (int j = 0; j < m; ++j) {
                        accL[j] += readPair(f0[j], f0[j] + stride, phase, bits, t[j]);
                        phase += inc;
                    }
                }
                streams[s].phase = phase;
            }
        }
        // The mix and the SUB.
        float xl[kTick], xr[kTick];
        {
            std::uint32_t sub = key.subPhase;
            const std::uint32_t subInc = key.subInc;
            for (int j = 0; j < m; ++j) {
                const float sv = gS[j] == 0.f ? 0.f : phaseSine(sub) * gS[j];
                sub += subInc;
                xl[j] = aL[j] * gA[j] + bL[j] * gB[j] + sv;
                if (stereo) xr[j] = aR[j] * gA[j] + bR[j] * gB[j] + sv;
            }
            key.subPhase = sub;
        }
        // The filter, a side each when the key is wide.
        if (filter != kFilterOff)
            for (int side = 0; side < (stereo ? 2 : 1); ++side) {
                FilterState f = side ? key.fr : key.fl;
                float* x = side ? xr : xl;
                const float a1 = key.a1, a2 = key.a2, a3 = key.a3, kq = key.k, b1 = key.b1, b2 = key.b2, b3 = key.b3;
                float v1, v2;
                switch (filter) {
                case kLow12:
                    for (int j = 0; j < m; ++j) svfStep(a1, a2, a3, x[j], f.ic1, f.ic2, v1, v2), x[j] = v2;
                    break;
                case kLow24:
                    for (int j = 0; j < m; ++j) {
                        svfStep(b1, b2, b3, x[j], f.jc1, f.jc2, v1, v2);
                        svfStep(a1, a2, a3, v2, f.ic1, f.ic2, v1, v2);
                        x[j] = v2;
                    }
                    break;
                case kBand:
                    for (int j = 0; j < m; ++j) svfStep(a1, a2, a3, x[j], f.ic1, f.ic2, v1, v2), x[j] = v1;
                    break;
                default:
                    for (int j = 0; j < m; ++j) svfStep(a1, a2, a3, x[j], f.ic1, f.ic2, v1, v2), x[j] = x[j] - kq * v1 - v2;
                    break;
                }
                (side ? key.fr : key.fl) = f;
            }
        // Into the block.
        float* __restrict l = left + i;
        float* __restrict r = right + i;
        const float* xs = stereo ? xr : xl;
        for (int j = 0; j < m; ++j) {
            const float g = e[j] * gO[j];
            l[j] += xl[j] * g;
            r[j] += xs[j] * g;
        }
        key.tickLeft -= m;
        key.clock += m;
        i += m;
    }
    if (!key.on) key = Key();
}

// ---- the synth -----------------------------------------------------------------------------------

Morph::Morph() : state_(new State) {
    state_->patch = presetPatch(0);
    prepare(48000.f);
}
Morph::~Morph() = default;

int Morph::presetCount() { return kPresetCount; }
const char* Morph::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Morph::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
MorphPatch Morph::presetPatch(int i) {
    MorphPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kSettings; ++id) patch.value[id] = settingHeld(id, preset.value[id]);
    return patch;
}
double Morph::tablesSeconds() { return builtInTablesSeconds(); }

void Morph::prepare(float sampleRate) {
    State& s = *state_;
    sizes(); // (made here, never first on the audio thread)
    makeBuiltInTables();
    for (int t = 0; t < kTables; ++t) s.tables[t] = &builtInTable(t);
    s.rate = sampleRate > 0.f ? sampleRate : 48000.f;
    s.fadeSamples = std::lround(kFadeSeconds * s.rate);
    if (s.fadeSamples < 1) s.fadeSamples = 1;
    s.fadeStep = 1.f / static_cast<float>(s.fadeSamples);
    for (State::Key& key : s.keys) key = State::Key();
    s.seedGiven = false;
    s.liveCount = 0;
    s.refresh();
}

void Morph::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    state_->dirty = true;
}

void Morph::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = settingHeld(id, value);
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    state_->dirty = true;
}

void Morph::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return;
    state_->macro[macro] = value;
    state_->dirty = true;
}

void Morph::noteSeed(std::uint32_t seed) {
    state_->nextSeed = seed;
    state_->seedGiven = true;
}

void Morph::noteOn(int note, float velocity) {
    State& s = *state_;
    if (note < 0 || note > 127 || !(velocity > 0.f)) {
        s.seedGiven = false;
        return;
    }
    if (s.dirty) s.refresh();
    const std::uint32_t seed = s.seedGiven ? s.nextSeed : liveSeed(s.liveCount++);
    s.seedGiven = false;
    const int unison = clampi(s.choice[Unison], 1, kUnisonMost);
    const int tableB = tableOfB(s.choice[TableB]);
    const bool bOn = tableB >= 0;
    const int need = streamsPerKey(unison, bOn);
    // The same key sounding fades out; then room is made: the oldest let-go key, else the oldest,
    // until the streams fit and fewer than six keys sound.
    for (State::Key& key : s.keys)
        if (key.on && !key.fading && key.note == note) s.fadeOut(key);
    for (;;) {
        int sounding = 0;
        for (const State::Key& key : s.keys) sounding += key.on && !key.fading ? 1 : 0;
        if (sounding < kKeys && s.streamsHeld() + need <= kStreams) break;
        int victim = -1;
        for (int pass = 0; pass < 2 && victim < 0; ++pass)
            for (int k = 0; k < kKeySlots; ++k) {
                const State::Key& key = s.keys[k];
                if (!key.on || key.fading || (pass == 0 && !key.released)) continue;
                if (victim < 0 || key.age < s.keys[victim].age) victim = k;
            }
        if (victim < 0) break;
        s.fadeOut(s.keys[victim]);
    }
    int slot = -1;
    for (int k = 0; k < kKeySlots && slot < 0; ++k)
        if (!s.keys[k].on) slot = k;
    if (slot < 0) { // every slot fading: the one nearest its end goes now
        for (int k = 0; k < kKeySlots; ++k)
            if (s.keys[k].fading && (slot < 0 || s.keys[k].fadeLeft < s.keys[slot].fadeLeft)) slot = k;
        if (slot < 0) slot = 0; // (cannot be: six sound at most, so six slots fade)
    }
    State::Key& key = s.keys[slot];
    key = State::Key();
    key.on = true;
    key.note = note;
    key.unison = unison;
    key.bOn = bOn;
    key.ta = s.tables[clampi(s.choice[TableA], 0, kTables - 1)];
    key.tb = bOn ? s.tables[tableB] : nullptr;
    key.filter = clampi(s.choice[Filter], 0, kFilters - 1);
    key.stereo = unison > 1 && s.slid[Width].to > 0.f;
    key.velocity = static_cast<float>(velocityGain(s.choice[Velocity], velocity > 1.f ? 1.0 : static_cast<double>(velocity)));
    key.seed = seed;
    key.age = ++s.ageClock;
    key.amp.on();
    key.mod.on();
    for (int i = 0; i < unison; ++i) {
        key.a[i].phase = streamPhase(seed, 0, i, unison);
        key.b[i].phase = streamPhase(seed, 1, i, unison);
    }
}

void Morph::noteOnFrom(int note, float velocity, int from, float seconds) {
    noteOn(note, velocity);
    State& s = *state_;
    if (from < 0 || from > 127 || from == note || !(seconds > 0.f) || seconds > 3600.f) return;
    for (State::Key& key : s.keys)
        if (key.on && !key.fading && key.note == note && key.clock == 0) {
            key.slideFrom = static_cast<double>(from - note);
            key.slideLength = std::lround(static_cast<double>(seconds) * s.rate);
        }
}

void Morph::noteOff(int note) {
    for (State::Key& key : state_->keys)
        if (key.on && !key.fading && !key.released && key.note == note) {
            key.amp.off();
            key.mod.off();
            key.released = true;
        }
}

void Morph::stopNotes() {
    for (State::Key& key : state_->keys)
        if (key.on && !key.fading && !key.released) {
            key.amp.off();
            key.mod.off();
            key.released = true;
        }
    state_->seedGiven = false;
}

void Morph::allNotesOff() {
    State& s = *state_;
    for (State::Key& key : s.keys)
        if (key.on && !key.fading) s.fadeOut(key);
    s.liveCount = 0;
    s.seedGiven = false;
}

void Morph::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    if (s.dirty) s.refresh();
    for (State::Key& key : s.keys)
        if (key.on) s.runKey(key, left, right, frames);
    if (s.sliding) {
        s.sliding = false;
        for (State::Slid& v : s.slid)
            if (v.since < kMacroSlideSamples) {
                v.since = v.since + frames < kMacroSlideSamples ? v.since + frames : kMacroSlideSamples;
                s.sliding = s.sliding || v.since < kMacroSlideSamples;
            }
    }
}

int Morph::grainPlaces(float* places, int most) const {
    const State& s = *state_;
    int order[kKeySlots], n = 0;
    for (int k = 0; k < kKeySlots; ++k)
        if (s.keys[k].on && !s.keys[k].fading) order[n++] = k;
    for (int i = 1; i < n; ++i) // newest first (a few keys: by hand)
        for (int j = i; j > 0 && s.keys[order[j]].age > s.keys[order[j - 1]].age; --j) std::swap(order[j], order[j - 1]);
    if (2 * n > most) n = most / 2;
    for (int i = 0; i < n; ++i) {
        const State::Key& key = s.keys[order[i]];
        places[i] = static_cast<float>(key.placeA / (key.ta->frames - 1));
        places[n + i] = key.bOn ? static_cast<float>(key.placeB / (key.tb->frames - 1)) : -1.f;
    }
    return 2 * n;
}

int Morph::keysSounding() const {
    int n = 0;
    for (const State::Key& key : state_->keys) n += key.on ? 1 : 0;
    return n;
}
bool Morph::sounds(int note) const {
    for (const State::Key& key : state_->keys)
        if (key.on && !key.fading && key.note == note) return true;
    return false;
}
int Morph::streamsInUse() const { return state_->streamsHeld(); }

float Morph::smallestState() const {
    float least = 0.f;
    auto look = [&](float v) {
        v = std::fabs(v);
        if (v > 0.f && (least == 0.f || v < least)) least = v;
    };
    for (const State::Key& key : state_->keys) {
        if (!key.on) continue;
        look(key.amp.level);
        look(key.mod.level);
        if (key.fading) look(static_cast<float>(key.fadeLeft) * state_->fadeStep);
        for (const State::FilterState* f : {&key.fl, &key.fr}) look(f->ic1), look(f->ic2), look(f->jc1), look(f->jc2);
    }
    return least;
}

} // namespace apotheca
