// SHIMMER: a TYPE of REVERB. A reverb whose tail rises: part of it goes round
// through an octave-up shifter every trip, so a note's reverb grows a halo an
// octave above, then two, then three, thinning as it climbs (the "shimmer"
// of ambient guitar records). (Built in fx/tank.hpp, which BLOOM shares.)
//
//   SHIMMER  how much of the tail goes round an octave up: at none it is a
//            plain dark-to-bright hall; at full the tail is mostly octaves
//   DECAY    how long the tail lasts: 0.3 to 20 seconds, a real time (RT60),
//            read and set as HALL's and PLATE's are. The octaves fall as fast
//            as the rest (their wait in the shifter is paid for)
//   TONE     low: dark, the highs die soon, and the octaves with them. High:
//            bright (PLATE's)
//   MIX      from the sound alone to the reverb alone
//
// THE SHIFTER, kept clean: four of the tank's eight mixed sounds are each cut
// above 7 kHz (four poles) and written to a short ring. Read twice as fast,
// anything over 12 kHz folds back down; the four poles alone are only about
// 19 dB down at 12 kHz, so most of the guard is the tank's own: its in-cut and
// TONE's damping take the highs first. Together a steady 14 or 15 kHz comes
// out folded 42 dB under itself at TONE full (testShimmer holds it under 30);
// do not loosen the damping thinking the shifter is safe alone; two grains read it
// at twice the speed it is written, half a window (60 ms) apart, each faded
// in and out by sin^2 so the two always add to exactly 1 and neither is heard
// where it jumps back. They read whole samples (twice as fast as the ring is
// written is two samples a sample): no reading between samples, nothing
// smeared. The octave is a little rough on a single held sine (60 ms grains
// beat with it); in a reverb's wash it is the usual shimmer.
//
// IT ALWAYS ENDS: what goes round the shifter's way is the tank's own sound
// (its power kept), turned down by DECAY for its trip and for its wait in
// the shifter: no way round is 1 or over. See tank.hpp.

#include "tank.hpp"

namespace apotheca {
namespace fx {

namespace {

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 1) return readDecay(v, out, size);
    readPercent(v, out, size);
}

Effect* create() { return new Tank(kShimmerInfo, Tank::kShimmer); }

} // namespace

const EffectInfo kShimmerInfo = {
    "SHIMMER", 4, {"SHIMMER", "DECAY", "TONE", "MIX"}, {0.5f, 0.6f, 0.6f, 0.3f}, // half the way round an octave up, 3.7 s of tail
    "shimmer", EffectFamily::Reverb, {0, 0, 0, 0}, reading, create,
    "", "SHIMMER",
};

} // namespace fx
} // namespace apotheca
