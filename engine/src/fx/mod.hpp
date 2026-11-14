#pragma once
// What MOD's newer types share (FLANGER, ENSEMBLE, PHASER, TREMOLO): a wave
// worked out from a place in its turn, CHORUS's scale of RATE (so changing
// TYPE between them carries RATE to the bit), a delay line read between
// samples, and the rest rule.
//
// They keep the TICK (svf.hpp): what glides, and where a wave has got to, is
// worked out every 16 samples counted from the effect's start (asleep too),
// and reached sample by sample (Slide). So the sound is the same to the last
// bit however it is cut into blocks, even while a setting moves.
//
// THE REST RULE (as RING's): each of them remembers where its wave is, so it
// sleeps only once nothing has come in for kModRestSeconds and its memory is
// exact zeros. Hits closer together than that meet the wave wherever it has
// got to; one after a longer silence meets it at its start (a sleeping effect
// that wakes starts as a fresh one). Asleep, what comes out is what came in.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

constexpr float kModRestSeconds = 4.f;

// RATE as CHORUS has it: 0.1 to 8 Hz, each D-pad push the same step in pitch.
inline float modRateHz(float v) { return 0.1f * std::pow(80.f, clamp01(v)); }
inline void modRateText(float v, char* out, int size) {
    const float hz = modRateHz(v);
    const std::size_t n = static_cast<std::size_t>(size);
    if (hz < 0.995f) std::snprintf(out, n, "%.2f HZ", static_cast<double>(hz));
    else std::snprintf(out, n, "%.1f HZ", static_cast<double>(hz));
}
// A signed amount, -100 to +100%, for a setting 0 to 1 (0.5 is 0).
inline float signedOf(float v) { return 2.f * clamp01(v) - 1.f; }
inline void signedText(float v, char* out, int size) {
    const int percent = static_cast<int>(std::round(signedOf(v) * 100.f));
    if (percent == 0) std::snprintf(out, static_cast<std::size_t>(size), "0%%");
    else std::snprintf(out, static_cast<std::size_t>(size), "%+d%%", percent);
}

// A place in a turn, 0 to 1, taken back into 0 to 1.
inline double wrapTurn(double at) { return at - std::floor(at); }
// sin(2 pi at): a polynomial on the triangle (as RING's carrier), good to 1e-5.
inline float turnSine(double at) {
    const float a = static_cast<float>(wrapTurn(at));
    const float u = a < 0.25f ? 4.f * a : (a < 0.75f ? 2.f - 4.f * a : 4.f * a - 4.f);
    const float u2 = u * u;
    return u * (1.5707963f + u2 * (-0.64596410f + u2 * (0.079692626f + u2 * (-0.0046817541f + u2 * 0.00016044118f))));
}

// The sound `delay` samples ago in a ring of a power of two (`mask` its size less 1), `at` the place the next
// sample will be written; read between samples with glide.hpp's cubic (which never makes anything louder, so it
// is safe in a feedback loop). `delay` at least 2.
inline float lineRead(const float* line, int mask, int at, float delay) {
    const int whole = static_cast<int>(delay);
    float w[4];
    cubicWeights(delay - static_cast<float>(whole), w);
    const int i = at - whole + 1;
    return w[0] * line[i & mask] + w[1] * line[(i - 1) & mask] + w[2] * line[(i - 2) & mask] + w[3] * line[(i - 3) & mask];
}
// The smallest power of two that holds `samples` and a few more.
inline int lineSize(float samples) {
    int size = 256;
    while (size < static_cast<int>(samples) + 8) size *= 2;
    return size;
}

// A roll, 0 to 1, for a count and what it is for (WEAR's dice): never a generator walked.
inline std::uint32_t modMash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline float modRoll(std::uint32_t at, std::uint32_t what) { return static_cast<float>(modMash(at * 0x9e3779b9u ^ modMash(what + 0x51ed270bu)) >> 8) * (1.f / 16777216.f); }

} // namespace fx
} // namespace apotheca
