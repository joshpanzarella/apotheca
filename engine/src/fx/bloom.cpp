// BLOOM: a TYPE of REVERB. A reverb that swells in slowly after the sound,
// instead of starting with it: a hit is heard dry, then its reverb rises out
// of nothing over BLOOM and falls away by DECAY. A held chord gathers a cloud
// under it. (Built in fx/tank.hpp, which SHIMMER shares.)
//
//   BLOOM   how long the swell takes: 0.05 to 4 seconds. Short, it is a soft
//           reverb whose start is blurred; long, the reverb comes in like a
//           slow fade-in under the music. It is what makes BLOOM itself
//   DECAY   how long the tail lasts once it has swelled: 0.3 to 20 seconds, a
//           real time (RT60), read and set as HALL's and PLATE's are
//   TONE    low: dark, the highs die soon. High: bright (PLATE's)
//   MIX     from the sound alone to the reverb alone
//
// HOW: the sound goes in through a line of the last BLOOM seconds read at 64
// places spread unevenly over it (so no rhythm), each later one louder than
// the one before (a straight rise, from 1/64 to full; their squares add to
// 1, so a held sound is as loud as through the tank alone), then into the
// tank. Each tap is smeared into the tank's wash, so the 64 make one rising
// cloud. BLOOM turned while sounding moves the taps, never faster than half a
// sample a sample, and the sound in them bends in pitch on the way (at most an
// octave down), as PLATE's PRE does: from 0.05 s to 4 s takes about 8 seconds.
//
// ASLEEP once nothing but zeros has been written for as long as its longest
// ring (the swell's: 5.5 s at 48 kHz), so it sleeps 5.5 s after the tail is
// gone: a fresh one when it wakes, to the bit.

#include "tank.hpp"

namespace apotheca {
namespace fx {

namespace {

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) {
        const float seconds = bloomSeconds(v);
        std::snprintf(out, static_cast<std::size_t>(size), seconds < 0.995f ? "%.2f S" : "%.1f S", static_cast<double>(seconds));
        return;
    }
    if (index == 1) return readDecay(v, out, size);
    readPercent(v, out, size);
}

Effect* create() { return new Tank(kBloomInfo, Tank::kBloom); }

} // namespace

const EffectInfo kBloomInfo = {
    "BLOOM", 4, {"BLOOM", "DECAY", "TONE", "MIX"}, {0.6f, 0.55f, 0.6f, 0.3f}, // a 0.69 s swell, 3.0 s of tail
    "bloom", EffectFamily::Reverb, {0, 0, 0, 0}, reading, create,
    "", "BLOOM",
};

void bloomTapAt(int k, float* at, float* gain) {
    static BloomTap taps[kBloomTaps];
    static bool made = false;
    if (!made) bloomTaps(taps), made = true;
    *at = taps[k].at, *gain = taps[k].gain;
}

} // namespace fx
} // namespace apotheca
