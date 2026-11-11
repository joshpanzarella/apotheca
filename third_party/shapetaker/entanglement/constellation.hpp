#pragma once
// Entanglement -- the constellation: what an FM topology IS, and how one is
// generated, inherited and repaired.
//
// This header deliberately does not include rack.hpp. Everything here is
// arithmetic over a Constellation, so it can be exercised with no SDK and no
// engine: `make test-entanglement`. Keep it that way -- the 32 algorithm
// grammars claim an invariant ("every operator is connected to an audible
// carrier, so a generated topology cannot collapse to a sine") that nothing
// checked before those tests existed.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace shapetaker {
namespace entanglement {

static const int OPS = 8;

namespace detail {
// The module's own clamp. Rack's lives in namespace rack, which this header
// must not reach for; naming it here rather than in the enclosing namespace
// keeps `using namespace shapetaker::entanglement;` from making an unqualified
// clamp() ambiguous in entanglement.cpp, which is full of them.
//
// These match rack::math::clamp EXACTLY, including on NaN, and that is not
// incidental: fmin/fmax return their non-NaN operand, so clamp(NaN, a, b) is
// `b` -- corrupted patch JSON is sanitised to the upper bound rather than
// propagating NaN through enforceGraphInvariants(). A ternary form reads the
// same and does the opposite. See the NaN finding in the audit.
inline float clamp(float x, float a, float b) { return std::fmax(std::fmin(x, b), a); }
inline int   clamp(int x, int a, int b)       { return std::max(std::min(x, b), a); }
}

// Entanglement can evaluate several hundred operator sines per host sample at
// 8x. A shared 4096-segment table stays resident in L1 and linear interpolation
// has a worst-case analytic error of roughly 2.94e-7 before float rounding --
// more than an order of magnitude below accurateSin2Pi's measured 3.6e-6 peak
// error. The table is built once when the plugin loads, never on the audio
// thread, and is immutable thereafter.
struct SineTable {
    static constexpr int SIZE = 4096;
    // Two guard entries keep the lookup branch-free even when the largest
    // float below 1 rounds to SIZE after scaling.
    std::array<float, SIZE + 2> values = {};

    SineTable() {
        for (int index = 0; index < SIZE; ++index) {
            double phase = static_cast<double>(index) / SIZE;
            values[index] = static_cast<float>(-std::sin(phase * 2.0 * M_PI));
        }
        values[SIZE] = values[0];
        values[SIZE + 1] = values[1];
    }

    inline float process(float phaseValue) const {
        int32_t phaseInt = static_cast<int32_t>(phaseValue);
        float fraction = phaseValue - static_cast<float>(phaseInt);
        if (fraction < 0.f)
            fraction += 1.f;
        float tablePosition = fraction * SIZE;
        int index = static_cast<int>(tablePosition);
        float mix = tablePosition - index;
        return values[index] + (values[index + 1] - values[index]) * mix;
    }
};

namespace detail {
// C++11 has no inline variables, and the table must be ONE object: an inline
// function that referenced a per-translation-unit `static` copy would be an
// ODR violation. A static data member of a class template may be defined in a
// header and is shared across every TU. It is also why this is not a
// function-local static -- that carries a thread-safe-init guard to test on
// every one of the several hundred lookups this module makes per host sample.
template <typename Tag> struct SineTableHolder { static const SineTable table; };
template <typename Tag> const SineTable SineTableHolder<Tag>::table;
}

inline const SineTable& sineTable() { return detail::SineTableHolder<void>::table; }

/** The 32 grammars' names, in algorithm order — what the screen prints and
    what the context menu lists. A function rather than a table so there is one
    definition across every translation unit; nothing here is on a hot path. */
inline const char* algorithmName(int algorithm) {
    static const char* const NAMES[32] = {
        "TOWER", "BRANCH", "CHOIR", "PAIRS", "CROWN", "WEB", "SPIRES", "VORTEX",
        "PRISM", "CROSS", "GRAVITY", "LABYRINTH", "CASCADE", "FORK", "TRIDENT", "QUADS",
        "PENDULUM", "HALO", "ANVIL", "CHIME", "HIVE", "MIRROR", "STAIRS", "DELTA",
        "ORBIT", "FURNACE", "LOOM", "HYDRA", "CATHEDRAL", "RELAY", "FRACTURE", "NEXUS"
    };
    return NAMES[(algorithm < 0 || algorithm > 31) ? 0 : algorithm];
}

/** One FM topology: who modulates whom, and with what voice. */
struct Constellation {
    float ratio[OPS] = {};
    float level[OPS] = {};
    float carrier[OPS] = {};
    float edge[OPS][OPS] = {}; // edge[source][destination]
    float feedback[OPS] = {};
    float color[OPS] = {};     // harmonic content of each operator
    float contour[OPS] = {};   // relative spectral-envelope duration
    float pan[OPS] = {};       // carrier position at full width
    int family = 0;
    int variant = 0;
    int algorithm = 0;
};

inline float rand01(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0x00ffffffu) / 16777215.f;
}

inline float musicalRatio(uint32_t& s, float symmetry) {
    static constexpr float consonant[] = {0.5f, 1.f, 1.f, 2.f, 2.f, 3.f, 3.f, 4.f, 5.f, 6.f};
    static constexpr float colorful[] = {0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 5.f, 7.f};
    int i = std::min(9, (int)(rand01(s) * 10.f));
    return rand01(s) < symmetry ? consonant[i] : colorful[i];
}

inline float operatorWave(float phaseValue, float color, float phaseIncrement,
                          float fullBandNormalization) {
    float absoluteIncrement = std::fabs(phaseIncrement);
    // Below this threshold all four partials are exactly inside the flat
    // part of harmonicFade(). This is the overwhelmingly common case at 4×
    // oversampling, so its normalization can be calculated once per control
    // sample instead of once per operator, voice, bank and substep.
    // Stay slightly inside the mathematical 0.08 boundary so floating-point
    // rounding can never select the cached path while fade5 is below 1.
    bool fullBand = absoluteIncrement <= 0.079f;
    auto harmonicFade = [absoluteIncrement](float harmonic) {
        // Fade a partial through the top 20% of the internal Nyquist band
        // instead of allowing it to fold abruptly back into the spectrum.
        return detail::clamp((0.5f - absoluteIncrement * harmonic) / 0.1f, 0.f, 1.f);
    };
    // The fundamental needs the same guard: at 1x oversampling a high note
    // on a x7 constellation ratio can cross the internal Nyquist and fold.
    float fade1 = fullBand ? 1.f : harmonicFade(1.f);
    if (fade1 <= 0.f) return 0.f;
    float fade2 = fullBand ? 1.f : harmonicFade(2.f);
    float fade3 = fullBand ? 1.f : harmonicFade(3.f);
    float fade5 = fullBand ? 1.f : harmonicFade(5.f);
    float sinPhase = sineTable().process(phaseValue);
    float fundamental = sinPhase * fade1;
    // FM sidebands already provide most of Entanglement's harmonic detail.
    // Operator color is deliberately restrained so a colored modulator does
    // not spawn several equally strong, glassy sideband families at once.
    constexpr float harmonic2 = 0.28f;
    constexpr float harmonic3 = 0.11f;
    constexpr float harmonic5 = 0.025f;
    float harmonic = 0.f;
    // The colour partials are the single most expensive thing this module
    // does — measured at 68% of operatorWave, which is itself 73% of the
    // whole per-sample loop. They do not need their own sine evaluations:
    // the odd harmonics are exact polynomials in sin of the fundamental, so
    // only the even one needs a second lookup for the cosine.
    //
    //   The lookup follows accurateSin2Pi's -sin(t) convention. With s as
    //   that value and c = lookup(phase + 0.25) = -cos(t):
    //     -sin(2t) = -2 sin t cos t = -2 s c
    //     -sin(3t) = 3s - 4s^3
    //     -sin(5t) = 5s - 20s^3 + 16s^5
    //
    // Four waveform lookups become two. The identities are exact, so the
    // residual is float rounding at or below the table's -129 dB peak-error
    // floor rather than distortion from a cheaper harmonic oscillator.
    //
    // Deriving the cosine as sqrt(1 - s*s) instead would remove the second
    // lookup, but it loses precision catastrophically near the peaks
    // where the derivative of sqrt blows up: measured -57 dB, which would
    // throw away everything the high-accuracy table bought. Do not do that.
    //
    // Looking up one wrapped phase is also more accurate than looking up
    // phaseValue * 5, which loses mantissa when modulation drives the phase
    // far from zero.
    if (color > 0.0001f) {
        float s2 = sinPhase * sinPhase;
        if (fade2 > 0.f) {
            float cosPhase = sineTable().process(phaseValue + 0.25f);
            harmonic += harmonic2 * fade2 * (-2.f * sinPhase * cosPhase);
        }
        if (fade3 > 0.f) harmonic += harmonic3 * fade3 * (sinPhase * (3.f - 4.f * s2));
        if (fade5 > 0.f) harmonic += harmonic5 * fade5 * (sinPhase * (5.f + s2 * (-20.f + 16.f * s2)));
    }
    // Energy normalization follows the partials that are actually audible.
    // This avoids dulling high notes after their upper partials have faded out.
    float normalization = fullBand ? fullBandNormalization : 0.f;
    if (!fullBand) {
        float harmonicEnergy = harmonic2 * harmonic2 * fade2 * fade2
            + harmonic3 * harmonic3 * fade3 * fade3
            + harmonic5 * harmonic5 * fade5 * fade5;
        normalization = 1.f / std::sqrt(1.f + color * color * harmonicEnergy);
    }
    return (fundamental + color * harmonic) * normalization;
}

inline void calculateReachability(const Constellation& c, bool reachable[OPS]) {
    for (int op = 0; op < OPS; ++op) reachable[op] = c.carrier[op] > 0.001f;
    for (int pass = 0; pass < OPS; ++pass) {
        bool changed = false;
        for (int src = 1; src < OPS; ++src) {
            if (reachable[src]) continue;
            for (int dst = 0; dst < src; ++dst) {
                if (c.edge[src][dst] > 0.001f && reachable[dst]) {
                    reachable[src] = true;
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) break;
    }
}

inline bool hasValidSignalGraph(const Constellation& c) {
    bool reachable[OPS] = {};
    calculateReachability(c, reachable);
    for (bool audible : reachable)
        if (!audible) return false;
    return true;
}

// Static on purpose: neither this nor generate() reads or writes a single
// member, and both run on BOTH threads -- the constructor and dataFromJson
// on the GUI thread, completeMutation() on the audio thread. Saying so in
// the signature is what makes that safe to see at a glance.
inline void enforceGraphInvariants(Constellation& c, uint32_t& s, float affinity) {
    affinity = detail::clamp(affinity, 0.f, 1.f);

    // The DSP engine is deliberately feed-forward between operators. Remove
    // malformed forward/self edges from old or hybridized patch data.
    for (int src = 0; src < OPS; ++src)
        for (int dst = 0; dst < OPS; ++dst) {
            if (dst >= src) c.edge[src][dst] = 0.f;
            else c.edge[src][dst] = detail::clamp(c.edge[src][dst], 0.f, 1.f);
        }

    // Operator 1 is the invariant root carrier. Every curated template uses
    // it, and restoring it makes even malformed legacy graphs repairable.
    //
    // Negated rather than written as `<= 0.001f` so that a NaN carrier -- which
    // corrupted patch JSON does produce -- restores it too. Every comparison
    // against NaN is false, so the plain form left operator 0 unreachable, and
    // the repair loop below then took its `destination = 0` fallback for
    // src = 0 and wrote a self-edge on edge[0][0]: the one thing this function
    // clears on its first pass. The audio path only ever reads src > dst, so
    // that edge was inert, but it survived into the saved patch. For every
    // finite carrier the two forms are identical.
    if (!(c.carrier[0] > 0.001f)) {
        c.carrier[0] = 1.f;
        c.pan[0] = 0.f;
        c.ratio[0] = 1.f;
    }

    // Repair hereditary edge combinations branch by branch. High Affinity
    // prefers attaching to the nearest audible ancestor, creating longer
    // paths; low Affinity tends toward a direct carrier connection.
    bool reachable[OPS] = {};
    calculateReachability(c, reachable);
    for (int src = 0; src < OPS; ++src) {
        if (reachable[src]) continue;
        int directCarrier = -1;
        int deepestAncestor = -1;
        for (int dst = 0; dst < src; ++dst) {
            if (!reachable[dst]) continue;
            deepestAncestor = dst;
            if (directCarrier < 0 && c.carrier[dst] > 0.001f) directCarrier = dst;
        }
        int destination = (rand01(s) < affinity && deepestAncestor >= 0)
            ? deepestAncestor : directCarrier;
        if (destination < 0) destination = 0;
        c.edge[src][destination] = 0.32f + 0.34f * affinity + 0.12f * rand01(s);
        calculateReachability(c, reachable);
    }

    // Affinity now has a literal structural role: it adds a small number of
    // deterministic cross-links without turning the graph into a full matrix.
    int extraConnections = (int)std::floor(affinity * 3.99f);
    for (int added = 0, attempts = 0; added < extraConnections && attempts < 64; ++attempts) {
        int src = 1 + std::min(OPS - 2, (int)(rand01(s) * (OPS - 1)));
        int dst = std::min(src - 1, (int)(rand01(s) * src));
        if (c.edge[src][dst] > 0.001f) continue;
        c.edge[src][dst] = 0.16f + 0.24f * affinity + 0.10f * rand01(s);
        ++added;
    }

    // Keep feedback expressive but bounded to a few operators.
    int maxFeedbackOperators = 2 + (int)std::round(2.f * affinity);
    for (;;) {
        int count = 0;
        int quietest = -1;
        float quietestAmount = 2.f;
        for (int op = 0; op < OPS; ++op) {
            c.feedback[op] = detail::clamp(c.feedback[op], 0.f, 0.32f);
            if (c.feedback[op] > 0.001f) {
                ++count;
                if (c.feedback[op] < quietestAmount) {
                    quietestAmount = c.feedback[op];
                    quietest = op;
                }
            }
        }
        if (count <= maxFeedbackOperators || quietest < 0) break;
        c.feedback[quietest] = 0.f;
    }

    // Normalize carrier power after independently inherited carrier roles.
    float carrierEnergy = 0.f;
    for (int op = 0; op < OPS; ++op) {
        c.carrier[op] = detail::clamp(c.carrier[op], 0.f, 1.f);
        c.pan[op] = detail::clamp(c.pan[op], -1.f, 1.f);
        carrierEnergy += c.carrier[op] * c.carrier[op];
    }
    float carrierScale = 1.f / std::sqrt(std::max(0.0001f, carrierEnergy));
    for (int op = 0; op < OPS; ++op) c.carrier[op] *= carrierScale;

    // A final deterministic safety net makes the invariant unconditional,
    // including for corrupted third-party patch JSON.
    if (!hasValidSignalGraph(c)) {
        for (int op = 1; op < OPS; ++op) c.edge[op][0] = std::max(c.edge[op][0], 0.42f);
    }
}

/** The 32 musical grammars, applied to `c` on their own.

    Split out of generate() so a test can assert what the grammars claim --
    that each one already produces a valid signal graph -- rather than only
    what enforceGraphInvariants() repairs afterwards. It is pure in `c` and
    `algorithm`: no randomness, no parent, no affinity. */
inline void applyAlgorithmGrammar(Constellation& c, int algorithm) {
    auto carrier = [&](int op, float weight, float pan) {
        c.carrier[op] = weight; c.pan[op] = pan; c.ratio[op] = (op == 0 ? 1.f : c.ratio[op]);
    };
    auto connect = [&](int src, int dst, float amount) { c.edge[src][dst] = amount; };
    // Thirty-two explicit musical grammars. Every operator is connected to an
    // audible carrier, so a generated topology cannot collapse to a sine.
    switch (algorithm) {
        case 0: // Tower: deep cascading spectrum
            carrier(0, 1.f, 0.f);
            for (int op = 1; op < OPS; ++op) connect(op, op - 1, 0.72f);
            connect(4, 0, 0.34f); connect(7, 2, 0.28f);
            break;
        case 1: // Branch: vocal parallel formants
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.82f); connect(2, 0, 0.7f); connect(3, 0, 0.58f);
            connect(4, 1, 0.72f); connect(5, 2, 0.68f); connect(6, 3, 0.62f); connect(7, 0, 0.42f);
            break;
        case 2: // Choir: four independently articulated carriers
            carrier(0, 0.5f, -1.f); carrier(1, 0.5f, -0.33f); carrier(2, 0.5f, 0.33f); carrier(3, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f; c.ratio[3] = 0.5f;
            connect(4, 0, 0.72f); connect(5, 1, 0.65f); connect(6, 2, 0.58f); connect(7, 3, 0.52f);
            connect(7, 0, 0.24f); connect(6, 1, 0.2f);
            break;
        case 3: // Pairs: four classic two-operator FM voices
            carrier(0, 0.5f, -1.f); carrier(2, 0.5f, -0.33f); carrier(4, 0.5f, 0.33f); carrier(6, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[2] = 1.f; c.ratio[4] = 2.f; c.ratio[6] = 0.5f;
            connect(1, 0, 0.9f); connect(3, 2, 0.76f); connect(5, 4, 0.64f); connect(7, 6, 0.54f);
            break;
        case 4: // Crown: one high operator conducts a three-carrier body
            carrier(0, 0.58f, -0.75f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.75f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f;
            connect(7, 0, 0.72f); connect(7, 1, 0.61f); connect(7, 2, 0.52f);
            connect(6, 7, 0.7f); connect(5, 7, 0.5f); connect(4, 1, 0.56f); connect(3, 0, 0.46f);
            break;
        case 5: // Web: cross-braced dual carriers
            carrier(0, 0.72f, -0.7f); carrier(1, 0.72f, 0.7f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.74f); connect(3, 1, 0.72f); connect(4, 0, 0.5f); connect(4, 1, 0.42f);
            connect(5, 2, 0.68f); connect(6, 3, 0.64f); connect(7, 4, 0.74f); connect(7, 1, 0.28f);
            break;
        case 6: // Twin Spires: two long, opposing modulation chains
            carrier(0, 0.72f, -0.78f); carrier(1, 0.72f, 0.78f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.78f); connect(4, 2, 0.7f); connect(6, 4, 0.62f);
            connect(3, 1, 0.74f); connect(5, 3, 0.66f); connect(7, 5, 0.58f); connect(7, 0, 0.24f);
            break;
        case 7: // Vortex: nested formant branches orbit one carrier
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.86f); connect(2, 0, 0.7f); connect(3, 0, 0.54f);
            connect(4, 1, 0.66f); connect(5, 1, 0.48f); connect(6, 2, 0.64f);
            connect(7, 6, 0.7f); connect(7, 3, 0.38f);
            break;
        case 8: // Prism: shared high modulators refract through three carriers
            carrier(0, 0.58f, -0.8f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.8f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f;
            connect(7, 0, 0.7f); connect(7, 1, 0.58f); connect(7, 2, 0.48f);
            connect(6, 7, 0.74f); connect(5, 7, 0.52f); connect(4, 2, 0.6f); connect(3, 1, 0.52f);
            break;
        case 9: // Crosspairs: paired voices with diagonal modulation bridges
            carrier(0, 0.5f, -1.f); carrier(2, 0.5f, -0.33f); carrier(4, 0.5f, 0.33f); carrier(6, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[2] = 1.f; c.ratio[4] = 2.f; c.ratio[6] = 0.5f;
            connect(1, 0, 0.82f); connect(3, 2, 0.74f); connect(3, 0, 0.3f);
            connect(5, 4, 0.66f); connect(5, 2, 0.28f); connect(7, 6, 0.58f); connect(7, 4, 0.26f);
            break;
        case 10: // Gravity: a descending mass bends a dual-carrier core
            carrier(0, 0.72f, -0.65f); carrier(1, 0.72f, 0.65f);
            c.ratio[0] = 1.f; c.ratio[1] = 0.5f;
            connect(7, 6, 0.74f); connect(6, 5, 0.7f); connect(5, 4, 0.66f);
            connect(4, 0, 0.72f); connect(4, 1, 0.56f); connect(3, 0, 0.46f); connect(2, 1, 0.5f);
            break;
        case 11: // Labyrinth: two carriers reached through crossing corridors
            carrier(0, 0.72f, -0.72f); carrier(2, 0.72f, 0.72f);
            c.ratio[0] = 1.f; c.ratio[2] = 1.f;
            connect(1, 0, 0.72f); connect(3, 0, 0.58f); connect(4, 2, 0.62f);
            connect(5, 3, 0.68f); connect(5, 0, 0.24f); connect(6, 4, 0.64f); connect(6, 2, 0.22f);
            connect(7, 5, 0.7f); connect(7, 6, 0.62f);
            break;
        case 12: // Cascade: three staggered chains converge on one carrier
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.72f); connect(3, 1, 0.66f); connect(6, 3, 0.58f);
            connect(2, 0, 0.62f); connect(5, 2, 0.6f); connect(7, 5, 0.52f); connect(4, 0, 0.42f);
            break;
        case 13: // Fork: one trunk divides into three carrier branches
            carrier(0, 0.58f, -0.8f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.8f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 1.f;
            connect(4, 0, 0.62f); connect(5, 1, 0.58f); connect(6, 2, 0.54f);
            connect(7, 4, 0.7f); connect(7, 5, 0.62f); connect(7, 6, 0.56f); connect(3, 0, 0.34f);
            break;
        case 14: // Trident: three carriers share a modulator with separate teeth
            carrier(0, 0.58f, -0.75f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.75f);
            c.ratio[0] = 1.f; c.ratio[1] = 2.f; c.ratio[2] = 0.5f;
            connect(3, 0, 0.68f); connect(4, 1, 0.62f); connect(5, 2, 0.58f);
            connect(6, 3, 0.62f); connect(7, 3, 0.36f); connect(7, 4, 0.56f); connect(7, 5, 0.5f);
            break;
        case 15: // Quads: four carriers receive crossed two-level modulation
            carrier(0, 0.5f, -1.f); carrier(1, 0.5f, -0.33f); carrier(2, 0.5f, 0.33f); carrier(3, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f; c.ratio[3] = 0.5f;
            connect(4, 0, 0.62f); connect(4, 2, 0.28f); connect(5, 1, 0.58f);
            connect(5, 3, 0.26f); connect(6, 4, 0.62f); connect(7, 5, 0.58f);
            break;
        case 16: // Pendulum: alternating chains swing between two carriers
            carrier(0, 0.72f, -0.82f); carrier(1, 0.72f, 0.82f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.72f); connect(3, 2, 0.64f); connect(4, 1, 0.7f);
            connect(5, 4, 0.62f); connect(6, 3, 0.56f); connect(6, 4, 0.28f);
            connect(7, 5, 0.54f); connect(7, 2, 0.3f);
            break;
        case 17: // Halo: a ring of modulators illuminates a centered carrier
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.54f); connect(2, 0, 0.5f); connect(3, 0, 0.46f); connect(4, 0, 0.42f);
            connect(5, 1, 0.52f); connect(6, 2, 0.48f); connect(7, 3, 0.44f); connect(7, 4, 0.34f);
            break;
        case 18: // Anvil: a compact carrier is struck by a dense upper stack
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.88f); connect(2, 1, 0.76f); connect(3, 1, 0.54f);
            connect(4, 2, 0.68f); connect(5, 2, 0.48f); connect(6, 3, 0.62f); connect(7, 4, 0.66f); connect(7, 0, 0.3f);
            break;
        case 19: // Chime: four light carriers with descending modulator ratios
            carrier(0, 0.5f, -1.f); carrier(2, 0.5f, -0.33f); carrier(4, 0.5f, 0.33f); carrier(6, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[2] = 2.f; c.ratio[4] = 3.f; c.ratio[6] = 0.5f;
            connect(1, 0, 0.66f); connect(3, 2, 0.58f); connect(5, 4, 0.5f); connect(7, 6, 0.44f);
            connect(7, 0, 0.18f);
            break;
        case 20: // Hive: many shallow cells feed a dual-carrier body
            carrier(0, 0.72f, -0.66f); carrier(1, 0.72f, 0.66f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.48f); connect(3, 1, 0.46f); connect(4, 0, 0.42f); connect(4, 1, 0.24f);
            connect(5, 1, 0.4f); connect(6, 0, 0.36f); connect(6, 1, 0.28f); connect(7, 2, 0.5f); connect(7, 3, 0.48f);
            break;
        case 21: // Mirror: matched carrier branches with mirrored depths
            carrier(0, 0.72f, -0.85f); carrier(1, 0.72f, 0.85f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.76f); connect(3, 1, 0.76f); connect(4, 2, 0.62f); connect(5, 3, 0.62f);
            connect(6, 4, 0.52f); connect(7, 5, 0.52f); connect(7, 0, 0.2f); connect(6, 1, 0.2f);
            break;
        case 22: // Staircase: a stepped chain exposes multiple carrier landings
            carrier(0, 0.58f, -0.8f); carrier(2, 0.58f, 0.f); carrier(4, 0.58f, 0.8f);
            c.ratio[0] = 1.f; c.ratio[2] = 1.f; c.ratio[4] = 2.f;
            connect(1, 0, 0.72f); connect(3, 2, 0.66f); connect(3, 1, 0.24f);
            connect(5, 4, 0.6f); connect(5, 3, 0.22f); connect(6, 5, 0.54f); connect(7, 6, 0.48f);
            break;
        case 23: // Delta: triangular carrier core with converging upper apex
            carrier(0, 0.58f, -0.78f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.78f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f;
            connect(3, 0, 0.58f); connect(4, 1, 0.54f); connect(5, 2, 0.5f);
            connect(6, 3, 0.62f); connect(6, 4, 0.52f); connect(7, 5, 0.58f); connect(7, 6, 0.46f);
            break;
        case 24: // Orbit: satellite pairs circle two central carriers
            carrier(0, 0.72f, -0.58f); carrier(1, 0.72f, 0.58f);
            c.ratio[0] = 1.f; c.ratio[1] = 2.f;
            connect(2, 0, 0.56f); connect(3, 1, 0.54f); connect(4, 0, 0.48f); connect(5, 1, 0.46f);
            connect(6, 2, 0.54f); connect(6, 3, 0.38f); connect(7, 4, 0.52f); connect(7, 5, 0.36f);
            break;
        case 25: // Furnace: feedback-heavy branches melt into one carrier
            carrier(0, 1.f, 0.f);
            connect(1, 0, 0.82f); connect(2, 0, 0.68f); connect(3, 1, 0.66f); connect(4, 1, 0.52f);
            connect(5, 2, 0.62f); connect(6, 2, 0.48f); connect(7, 3, 0.6f); connect(7, 5, 0.54f);
            c.feedback[1] = std::max(c.feedback[1], 0.16f); c.feedback[2] = std::max(c.feedback[2], 0.12f);
            break;
        case 26: // Loom: alternating warp and weft chains feed four carriers
            carrier(0, 0.5f, -1.f); carrier(1, 0.5f, -0.33f); carrier(2, 0.5f, 0.33f); carrier(3, 0.5f, 1.f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f; c.ratio[2] = 2.f; c.ratio[3] = 0.5f;
            connect(4, 0, 0.58f); connect(4, 2, 0.34f); connect(5, 1, 0.56f); connect(5, 3, 0.32f);
            connect(6, 0, 0.28f); connect(6, 5, 0.52f); connect(7, 1, 0.26f); connect(7, 4, 0.5f);
            break;
        case 27: // Hydra: one carrier receives seven independently moving heads
            carrier(0, 1.f, 0.f);
            for (int op = 1; op < OPS; ++op) connect(op, 0, 0.82f - 0.07f * op);
            connect(7, 3, 0.42f); connect(6, 2, 0.38f); connect(5, 1, 0.34f);
            break;
        case 28: // Cathedral: octave carriers share tall nested modulators
            carrier(0, 0.58f, -0.75f); carrier(1, 0.58f, 0.f); carrier(2, 0.58f, 0.75f);
            c.ratio[0] = 0.5f; c.ratio[1] = 1.f; c.ratio[2] = 2.f;
            connect(3, 0, 0.46f); connect(4, 1, 0.42f); connect(5, 2, 0.38f);
            connect(6, 3, 0.58f); connect(7, 6, 0.56f); connect(7, 4, 0.46f); connect(7, 5, 0.42f);
            break;
        case 29: // Relay: modulation passes through alternating handoffs
            carrier(0, 0.72f, -0.7f); carrier(1, 0.72f, 0.7f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.68f); connect(3, 2, 0.62f); connect(4, 1, 0.66f);
            connect(5, 4, 0.58f); connect(6, 3, 0.54f); connect(6, 1, 0.3f);
            connect(7, 6, 0.5f); connect(7, 5, 0.46f);
            break;
        case 30: // Fracture: asymmetric carrier shards share sharp cross-links
            carrier(0, 0.68f, -0.9f); carrier(2, 0.58f, 0.1f); carrier(5, 0.48f, 0.9f);
            c.ratio[0] = 1.f; c.ratio[2] = 1.5f; c.ratio[5] = 2.f;
            connect(1, 0, 0.76f); connect(3, 2, 0.64f); connect(3, 0, 0.28f);
            connect(4, 2, 0.56f); connect(6, 5, 0.52f); connect(6, 0, 0.24f); connect(7, 4, 0.58f); connect(7, 5, 0.34f);
            break;
        default: // Nexus: a dense but ordered hub connecting the full bank
            carrier(0, 0.72f, -0.65f); carrier(1, 0.72f, 0.65f);
            c.ratio[0] = 1.f; c.ratio[1] = 1.f;
            connect(2, 0, 0.62f); connect(3, 1, 0.6f); connect(4, 0, 0.48f); connect(4, 2, 0.42f);
            connect(5, 1, 0.46f); connect(5, 3, 0.4f); connect(6, 2, 0.5f); connect(6, 4, 0.38f);
            connect(7, 3, 0.48f); connect(7, 5, 0.36f); connect(7, 0, 0.22f); connect(7, 1, 0.2f);
            break;
    }
}

inline Constellation generate(uint32_t sourceSeed, float mutation, float affinity,
                              float symmetry, const Constellation* parent = nullptr,
                              int forcedAlgorithm = -1) {
    uint32_t s = sourceSeed ? sourceSeed : 1u;
    Constellation c;
    int proposedAlgorithm = forcedAlgorithm >= 0 ? detail::clamp(forcedAlgorithm, 0, 31)
        : std::min(31, (int)(rand01(s) * 32.f));
    c.algorithm = (parent && forcedAlgorithm < 0 && rand01(s) > mutation)
        ? parent->algorithm : proposedAlgorithm;
    c.family = c.algorithm % 6;
    c.variant = c.algorithm >= 6 ? 1 : 0;
    for (int i = 0; i < OPS; ++i) {
        bool inherit = parent && rand01(s) > mutation;
        c.ratio[i] = inherit ? parent->ratio[i] : musicalRatio(s, symmetry);
        c.level[i] = inherit ? parent->level[i] : (0.62f + 0.38f * rand01(s));
        c.feedback[i] = inherit ? parent->feedback[i] : ((rand01(s) < 0.12f + 0.22f * affinity) ? 0.04f + 0.24f * rand01(s) : 0.f);
        c.color[i] = inherit ? parent->color[i] : rand01(s) * (0.25f + 0.75f * (1.f - symmetry));
        c.contour[i] = inherit ? parent->contour[i] : rand01(s);
    }

    applyAlgorithmGrammar(c, c.algorithm);

    // Mutation preserves individual relationships as well as the family.
    if (parent) {
        for (int src = 0; src < OPS; ++src)
            for (int dst = 0; dst < OPS; ++dst)
                if (rand01(s) > mutation) c.edge[src][dst] = parent->edge[src][dst];
        for (int i = 0; i < OPS; ++i)
            if (rand01(s) > mutation) { c.carrier[i] = parent->carrier[i]; c.pan[i] = parent->pan[i]; }
    }
    enforceGraphInvariants(c, s, affinity);
    return c;
}

// Oscillator increments are strictly positive and the stored phase is
// always in [0, 1). Most substeps therefore do not wrap at all. Calling
// floor() unconditionally for 8 operators x 2 banks x 8 substeps was a
// surprisingly large tax at 8x; retain the general floor path only for the
// uncommon wrap (and for extreme pitches that cross several cycles).
inline float wrapPositivePhase(float value) {
    if (value >= 1.f)
        value -= std::floor(value);
    return value;
}

} // namespace entanglement
} // namespace shapetaker
