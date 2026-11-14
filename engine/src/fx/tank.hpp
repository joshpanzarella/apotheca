#pragma once
// What REVERB's newer types share (BLOOM, SHIMMER): one tank, eight lines
// mixed into each other every trip, with what each type adds about it.
//
//   BLOOM     the sound goes in through a long SWELL first: 64 taps from a line
//             of the last BLOOM seconds, each later tap louder than the one
//             before it (a straight rise from nothing to full), each read between two samples, so what a hit
//             puts into the tank grows for BLOOM seconds and the reverb swells
//             in after the sound instead of starting with it
//   SHIMMER   half the tank's mixed sound goes round through an octave-up
//             shifter, by SHIMMER: each trip lifts what goes round another
//             octave, so a held note's tail rises into a halo of octaves above
//
//   in (L + R) / 2 -> [BLOOM: the swell] -> x feed -> a gentle high cut -> four smearing stages -+
//                                                                                                |
//      +-------------------------------------------------------------------------------------+   |
//      v                                                                                     |   v
//    eight lines (each 44 to 89 ms, swaying a hair) -> x DECAY -> damping (TONE) -> mixed (8 x 8) -(+)
//                                                                     |               |
//                                                                     |   [SHIMMER: four of the eight
//                                                                     |    mixed through the octave shifter]
//                                                                     v
//                                                     left: four lines added, four taken away
//                                                     right: another four of each (unrelated to the left)
//
// The settings both share are named and read as HALL's and PLATE's (DECAY, TONE, MIX), so a TYPE change
// carries them to the bit:
//   DECAY   0.3 to 20 seconds, a real time (RT60): each line is turned down by exp(-6.908 x its length /
//           DECAY), so every way round falls 60 dB in DECAY
//   TONE    the damping in each line is a shelf with its corner at 1.2 kHz (dark) to 18 kHz (bright): above
//           it the tail lasts 0.4 of DECAY, at any DECAY (PLATE's)
//   MIX     the sound and the reverb swap over at equal power (they are unrelated sounds)
//
// The rules every new effect keeps (CLAUDE.md, "The engine and its threads"):
//   - THE TICK (svf.hpp): what glides is worked out every 16 samples counted from the effect's start (asleep
//     too), and what follows from it (the lines' gains and shelves, the swell's length, the shimmer's share)
//     is slid to sample by sample (`Slide`), so the sound is the same to the last bit however the caller
//     cuts it into blocks, settings moving too. The lines' sway is a turning pair worked out a sample at a
//     time from waking, put back on its circle every 4096th, and read on each tick's first sample.
//   - NOTHING UNDER kQuiet GOES ON A LINE; AN EXACT ZERO DOES (PING's rule), and a filter's state under kTiny
//     is made an exact zero on the sample. So "asleep" is "every line, the swell and the shifter zeros from
//     end to end and every filter empty", and a woken tank is a fresh one to the bit. Nothing in it makes a
//     number too small to hold (TooSmall), with flush-to-zero on or off.
//   - IT ALWAYS ENDS: every way round loses what DECAY says (under 1 a trip, at 20 s 0.995 for the longest
//     line). SHIMMER's way round is longer (it waits in the shifter, half a grain on average), so what goes
//     that way is turned down for that wait too: a shimmered tail falls as DECAY says, not slower. A ceiling
//     of kTankCeiling on what goes into a line (64, 36 dB over full scale: nothing a sound reaches) holds it
//     whatever comes in.
//   - On a bus the dry sound is dropped and MIX is the level (Unit's `bus_`).

#include <initializer_list>
#include <vector>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

constexpr int kTankLines = 8;
constexpr float kTankLineMs[kTankLines] = {43.7f, 49.1f, 55.3f, 61.9f, 67.7f, 74.3f, 81.1f, 88.9f};
constexpr int kTankSmears = 4;
constexpr float kTankSmearMs[kTankSmears] = {4.3f, 6.7f, 9.7f, 13.1f};
constexpr float kTankSmearGain = 0.6f;
constexpr float kTankShortest = 0.3f, kTankLongest = 20.f;                 // DECAY, seconds (HALL's and PLATE's)
constexpr float kTankDampLeastHz = 1200.f, kTankDampRange = 15.f;          // TONE's corner: 1.2 kHz to 18 kHz (PLATE's)
constexpr float kTankInCut = 3.f;                                          // the gentle high cut on the way in: this many times TONE's corner
constexpr float kTankHighsLast = 0.4f;                                     // above TONE's corner the tail lasts this much as long as DECAY
constexpr float kTankSwayHz = 0.6f, kTankSwayMs = 0.12f;                   // each line's length sways this much either way, so no note rings on
constexpr int kTankSwayRound = 4096;                                       // the turning pair is put back on its circle this often
constexpr float kTankFeed = 2.8f, kTankFeedLong = 0.6f, kTankFeedShort = 0.4f; // how hard it is fed: less for a long DECAY, a little more for the shortest (a held sound about as loud at any DECAY)
constexpr float kTankOut = 0.9f;                                           // how loud the two sides are taken out
constexpr float kTankCeiling = 64.f;                                       // nothing louder than this goes into a line (36 dB over full scale: a guard, not a sound)
constexpr float kTankSettle = 0.012f;                                      // each of a Gentle's two Glides, seconds
// BLOOM
constexpr int kBloomTaps = 64;
constexpr float kBloomLeast = 0.05f, kBloomMost = 4.f;                     // BLOOM: how long the swell takes, seconds
constexpr std::uint32_t kBloomSeed = 4u;                                   // which unevenly spread taps (see bloomTaps)
constexpr float kBloomMostMove = 0.5f;                                     // the swell's length moves never more than half a sample a sample: its farthest tap bends no further than an octave down
// SHIMMER
constexpr int kShimmerWays = 4;                                            // how many of the eight mixed sounds go through the shifter
constexpr float kGrainSeconds = 0.06f;                                     // the shifter's window: two grains, half a window apart
constexpr float kShimmerCutHz = 7000.f;                                    // what goes into the shifter is cut above this, so its octave is under 14 kHz
constexpr float kShimmerLowHz = 40.f;                                      // ...and below this: an octave of nothing (a steady offset) would be itself, and could build up
constexpr float kShimmerMost = 0.3f;                                       // SHIMMER at full: this share of the four goes the shifter's way each trip
constexpr float kGrainPower = 1.1547005f;                                  // two grains that add to 1 lose a quarter of the power of unrelated sound: given back

inline float tankDecaySeconds(float v) { return kTankShortest * std::pow(kTankLongest / kTankShortest, clamp01(v)); }
inline float bloomSeconds(float v) { return kBloomLeast * std::pow(kBloomMost / kBloomLeast, clamp01(v)); }

// How DECAY reads, as HALL's and PLATE's do: "2.0 S", "20 S".
inline void readDecay(float v, char* out, int size) {
    const float seconds = tankDecaySeconds(v);
    std::snprintf(out, static_cast<std::size_t>(size), seconds < 9.95f ? "%.1f S" : "%.0f S", static_cast<double>(seconds));
}

// The swell's 64 taps: where each is, as a share of BLOOM (spread unevenly, so they make no rhythm), which way
// round, and how loud (a straight rise, the 64 together as loud as the sound: their squares add to 1).
struct BloomTap {
    float at, gain;
};
inline std::uint32_t bloomHash(std::uint32_t h) { // (a well-mixed hash: neighbouring taps get unrelated numbers)
    h ^= h >> 16, h *= 0x85ebca6bu, h ^= h >> 13, h *= 0xc2b2ae35u, h ^= h >> 16;
    return h;
}
inline void bloomTaps(BloomTap* taps) {
    double squares = 0.0;
    for (int k = 0; k < kBloomTaps; ++k) {
        const std::uint32_t h = bloomHash(static_cast<std::uint32_t>(k) * 0x9e3779b9u + kBloomSeed);
        const float jitter = static_cast<float>(h & 0xffffu) / 65536.f - 0.5f; // -0.5 to 0.5
        const float sign = ((h >> 31) & 1u) ? -1.f : 1.f;
        taps[k].at = (static_cast<float>(k) + 0.5f + 0.8f * jitter) / static_cast<float>(kBloomTaps);
        taps[k].gain = sign * static_cast<float>(k + 1) / static_cast<float>(kBloomTaps);
        squares += static_cast<double>(taps[k].gain) * taps[k].gain;
    }
    const float norm = static_cast<float>(1.0 / std::sqrt(squares));
    for (int k = 0; k < kBloomTaps; ++k) taps[k].gain *= norm;
}

class Tank : public Unit {
public:
    enum Kind { kBloom, kShimmer };
    Tank(const EffectInfo& info, Kind kind) : Unit(info), kind_(kind) {}

    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        const float perMs = 1e-3f * sampleRate;
        swayMost_ = kTankSwayMs * perMs;
        for (int i = 0; i < kTankLines; ++i) {
            len_[i] = static_cast<int>(std::lround(kTankLineMs[i] * perMs));
            mask_[i] = ringFor(static_cast<float>(len_[i]) + 2.f * swayMost_ + 8.f) - 1;
            line_[i].assign(static_cast<std::size_t>(mask_[i] + 1), 0.f);
        }
        rest_ = 0;
        for (int j = 0; j < kTankSmears; ++j) {
            smearLen_[j] = static_cast<int>(std::lround(kTankSmearMs[j] * perMs));
            smearMask_[j] = ringFor(static_cast<float>(smearLen_[j])) - 1;
            smear_[j].assign(static_cast<std::size_t>(smearMask_[j] + 1), 0.f);
        }
        for (int i = 0; i < kTankLines; ++i) rest_ = mask_[i] + 1 > rest_ ? mask_[i] + 1 : rest_;
        if (kind_ == kBloom) {
            swellMask_ = ringFor(kBloomMost * sampleRate + 8.f) - 1;
            swell_.assign(static_cast<std::size_t>(swellMask_ + 1), 0.f);
            bloomTaps(taps_);
            rest_ = swellMask_ + 1 > rest_ ? swellMask_ + 1 : rest_;
        } else {
            grain_ = 2 * static_cast<int>(std::lround(0.5f * kGrainSeconds * sampleRate)); // even, so the two windows add to 1
            grainMask_ = ringFor(static_cast<float>(grain_) + 4.f) - 1;
            for (auto& ring : grainRing_) ring.assign(static_cast<std::size_t>(grainMask_ + 1), 0.f);
            window_.resize(static_cast<std::size_t>(grain_));
            for (int p = 0; p < grain_; ++p) {
                const double s = std::sin(3.141592653589793 * p / grain_);
                window_[static_cast<std::size_t>(p)] = static_cast<float>(s * s);
            }
            for (int p = 0; p < grain_ / 2; ++p) window_[static_cast<std::size_t>(p + grain_ / 2)] = 1.f - window_[static_cast<std::size_t>(p)]; // the two add to 1 exactly
            const float g = svfG(kShimmerCutHz > 0.3f * sampleRate ? 0.3f * sampleRate : kShimmerCutHz, sampleRate);
            cutA_.set(g, 1.f / 0.5412f), cutB_.set(g, 1.f / 1.3066f); // four poles, Butterworth
            lowC_ = onePole(kShimmerLowHz, sampleRate);
            rest_ = grainMask_ + 1 > rest_ ? grainMask_ + 1 : rest_;
        }
        swayTurn_ = static_cast<float>(6.283185307179586 * kTankSwayHz / sampleRate);
        swayCos_ = std::cos(swayTurn_), swaySin_ = std::sin(swayTurn_);
        changed();
        reset();
    }
    void reset() override {
        for (auto& l : line_) l.assign(l.size(), 0.f);
        for (auto& l : smear_) l.assign(l.size(), 0.f);
        swell_.assign(swell_.size(), 0.f);
        for (auto& ring : grainRing_) ring.assign(ring.size(), 0.f);
        at_ = 0;
        quiet_ = rest_;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (line_[0].empty() || frames <= 0) return;
        soundOnly(left, right, frames);
        int n = 0;
        while (n < frames) {
            if (asleep_) {
                if (heardIn(0.5f * (heardIn(left[n]) + heardIn(right[n]))) == 0.f) {
                    if (bus_) left[n] = right[n] = 0.f;
                    tick_ = (tick_ + 1) % kTick;
                    ++n;
                    continue;
                }
                wake(); // at this sample
            }
            if (tick_ == 0) beginTick();
            const int len = frames - n < kTick - tick_ ? frames - n : kTick - tick_;
            const int done = stretch(left + n, right + n, len); // to the tick's end, or to falling asleep
            n += done, tick_ += done;
            if (tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        character_.first.to = p_[0];
        decay_.first.to = p_[1];
        tone_.first.to = p_[2];
        mix_.first.to = p_[3];
        again_ = true; // (a bus told so moves nothing that glides: worked out again all the same)
    }

private:
    // What follows from the settings, slid to sample by sample.
    enum { kGain = 0, kHighs = kGain + kTankLines, kDamp = kHighs + kTankLines, kCut, kFeedIn, kDry, kWet, kKeep, kFall, kSwell, kShapes };

    static int ringFor(float samples) {
        int size = 64;
        while (size < static_cast<int>(samples) + 8) size *= 2;
        return size;
    }
    static float onePole(float hz, float sr) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr ? 0.45f * sr : hz) / sr); }
    static float tiny(float v) { return std::fabs(v) < kTiny ? 0.f : v; }
    // Everything that follows from the settings as they stand now, as where the slide is going.
    void follow() {
        const float seconds = tankDecaySeconds(decay_.now()), tone = tone_.now();
        for (int i = 0; i < kTankLines; ++i) {
            const float trip = -6.9077553f * static_cast<float>(len_[i]) / (sr_ * seconds);
            shape_.to[kGain + i] = std::exp(trip);
            shape_.to[kHighs + i] = std::exp(trip * (1.f / kTankHighsLast - 1.f));
        }
        const float corner = kTankDampLeastHz * std::pow(kTankDampRange, tone);
        shape_.to[kDamp] = onePole(corner, sr_);
        shape_.to[kCut] = onePole(kTankInCut * corner, sr_);
        shape_.to[kFeedIn] = kTankFeed * std::pow(2.f / seconds, kTankFeedLong) * std::sqrt(1.f + kTankFeedShort / seconds);
        const float mix = mix_.now();
        float dry = bus_ ? 0.f : std::cos(1.5707963f * mix), wet = bus_ ? mix : std::sin(1.5707963f * mix);
        if (mix >= 1.f && !bus_) dry = 0.f, wet = 1.f; // the reverb alone, exactly (at 0, cos is 1 and sin 0: the sound untouched)
        shape_.to[kDry] = dry, shape_.to[kWet] = wet;
        if (kind_ == kShimmer) {
            const float share = kShimmerMost * character_.now();
            // What goes the shifter's way waits in it half a grain on average: turned down for that too.
            const float wait = std::exp(-6.9077553f * 0.5f * static_cast<float>(grain_) / (sr_ * seconds));
            shape_.to[kKeep] = std::cos(1.5707963f * share), shape_.to[kFall] = kGrainPower * std::sin(1.5707963f * share) * wait; // at equal power: what goes up is unrelated to what stays
        } else {
            shape_.to[kKeep] = 1.f, shape_.to[kFall] = 0.f;
            swellTarget_ = bloomSeconds(character_.now()) * sr_;
            const float away = swellTarget_ - shape_.now[kSwell], most = kBloomMostMove * kTick;
            shape_.to[kSwell] = away > most ? shape_.now[kSwell] + most : (away < -most ? shape_.now[kSwell] - most : swellTarget_);
        }
    }
    bool anyMoving() const { return character_.moving() || decay_.moving() || tone_.moving() || mix_.moving(); }
    void beginTick() {
        if (!anyMoving() && !again_ && !swellMoving_) return;
        again_ = false;
        const float k = glideK(kTick, kTankSettle, sr_);
        if (character_.moving()) character_.tick(k);
        if (decay_.moving()) decay_.tick(k);
        if (tone_.moving()) tone_.tick(k);
        if (mix_.moving()) mix_.tick(k);
        follow();
        shape_.aim();
        swellMoving_ = kind_ == kBloom && shape_.to[kSwell] != shape_.now[kSwell];
    }
    void endTick() {
        tick_ = 0;
        if (!shape_.moving) return;
        shape_.land();
        if (swellMoving_ && !character_.moving() && shape_.now[kSwell] == swellTarget_) restTaps(); // the swell has arrived: its taps worked out once again
    }
    // The swell at rest: each tap's place worked out once (as the moving loop works it out, to the bit).
    void restTaps() {
        for (int k = 0; k < kBloomTaps; ++k) {
            const float ago = taps_[k].at * shape_.now[kSwell];
            tapAgo_[k] = static_cast<int>(ago);
            tapFrac_[k] = ago - static_cast<float>(tapAgo_[k]);
        }
        swellMoving_ = false;
    }
    // Where the four swaying lines read, from the turning pair as it stands: whole samples and four weights each.
    void swayAt(float c, float sn) {
        const float sway[4] = {sn, c, -sn, -c};
        for (int j = 0; j < 4; ++j) {
            const float ago = swayMost_ * (1.f + sway[j]);
            const int whole = static_cast<int>(ago);
            swayWhole_[j] = static_cast<unsigned>(whole);
            cubicWeights(ago - static_cast<float>(whole), swayW_[j]);
        }
    }
    void wake() {
        asleep_ = false;
        arrive();
    }
    // A fresh tank: the settings where they were turned to, the sway and the grains from their start.
    void arrive() {
        character_.snap(), decay_.snap(), tone_.snap(), mix_.snap();
        follow();
        shape_.to[kSwell] = swellTarget_;
        shape_.snap();
        if (kind_ == kBloom) restTaps();
        swellMoving_ = false;
        quiet_ = 0;
        swayC_ = 1.f, swayS_ = 0.f, swayCount_ = 0;
        swayAt(swayC_, swayS_);
        phase_ = 0;
        for (float& d : damp_) d = 0.f;
        cutState_ = 0.f;
        for (Svf& s : grainCut_) s.reset();
        for (float& v : lowState_) v = 0.f;
    }

    // A stretch of the tank awake, up to the tick's end: worked on locals (everything the samples change copied
    // in, worked on, copied back), as WEAR's is. Stops early at the sample it falls asleep on; says how many it did.
    int stretch(float* left, float* right, int frames) {
        float sh[kShapes], st[kShapes];
        const bool moving = shape_.moving;
        for (int i = 0; i < kShapes; ++i) sh[i] = shape_.now[i], st[i] = shape_.step[i];
        float* line[kTankLines];
        unsigned mask[kTankLines], len[kTankLines];
        float damp[kTankLines];
        for (int i = 0; i < kTankLines; ++i) line[i] = line_[i].data(), mask[i] = static_cast<unsigned>(mask_[i]), len[i] = static_cast<unsigned>(len_[i]), damp[i] = damp_[i];
        float* smear[kTankSmears];
        unsigned smearMask[kTankSmears], smearLen[kTankSmears];
        for (int j = 0; j < kTankSmears; ++j) smear[j] = smear_[j].data(), smearMask[j] = static_cast<unsigned>(smearMask_[j]), smearLen[j] = static_cast<unsigned>(smearLen_[j]);
        float cut = cutState_, swayC = swayC_, swayS = swayS_;
        int swayCount = swayCount_, phase = phase_, quiet = quiet_;
        unsigned at = at_;
        const float swayMost = swayMost_, swayCos = swayCos_, swaySin = swaySin_;
        const bool bloom = kind_ == kBloom, shimmer = kind_ == kShimmer;
        float* const swell = bloom ? swell_.data() : nullptr;
        const unsigned swellMask = static_cast<unsigned>(swellMask_);
        const bool swellMoving = swellMoving_;
        float* ring[kShimmerWays] = {};
        float low[kShimmerWays] = {}, ic[2 * kShimmerWays][2] = {};
        const float* window = shimmer ? window_.data() : nullptr;
        const unsigned grainMask = static_cast<unsigned>(grainMask_);
        const int grain = grain_, half = grain_ / 2;
        const float lowC = lowC_;
        const SvfShape cutA = cutA_, cutB = cutB_;
        if (shimmer)
            for (int w = 0; w < kShimmerWays; ++w) {
                ring[w] = grainRing_[w].data(), low[w] = lowState_[w];
                for (int f = 0; f < 2; ++f) ic[2 * w + f][0] = grainCut_[2 * w + f].ic1, ic[2 * w + f][1] = grainCut_[2 * w + f].ic2;
            }
        static const float kInSign[kTankLines] = {1.f, -1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f};
        int n = 0;
        bool asleep = false;
        while (n < frames && !asleep) {
            if (moving)
                for (int i = 0; i < kShapes; ++i) sh[i] += st[i];
            const float in = heardIn(0.5f * (heardIn(left[n]) + heardIn(right[n])));
            bool stirred = in != 0.f; // anything written that is not zero, or a filter not empty, this sample
            // The way in.
            float x = in;
            if (bloom) {
                // Each tap read between two samples (a straight line): its place moves smoothly while BLOOM moves.
                // (Every tap is 10 samples back or more: read before this sample is written.)
                float sum = 0.f;
                if (swellMoving) {
                    const float length = sh[kSwell];
                    for (int k = 0; k < kBloomTaps; ++k) {
                        const float ago = taps_[k].at * length;
                        const int whole = static_cast<int>(ago);
                        const float f = ago - static_cast<float>(whole), a = swell[(at - static_cast<unsigned>(whole)) & swellMask];
                        sum += taps_[k].gain * (a + f * (swell[(at - static_cast<unsigned>(whole) - 1u) & swellMask] - a));
                    }
                } else {
                    for (int k = 0; k < kBloomTaps; ++k) {
                        const float a = swell[(at - static_cast<unsigned>(tapAgo_[k])) & swellMask];
                        sum += taps_[k].gain * (a + tapFrac_[k] * (swell[(at - static_cast<unsigned>(tapAgo_[k]) - 1u) & swellMask] - a));
                    }
                }
                swell[at & swellMask] = in;
                x = sum;
            }
            x *= sh[kFeedIn];
            cut = tiny(cut + sh[kCut] * (x - cut));
            x = cut;
            stirred = stirred || cut != 0.f;
            for (int j = 0; j < kTankSmears; ++j) {
                float* const l = smear[j];
                const float was = l[(at - smearLen[j]) & smearMask[j]];
                float fed = x + kTankSmearGain * was;
                fed = std::fabs(fed) < kQuiet ? 0.f : fed;
                l[at & smearMask[j]] = fed;
                stirred = stirred || fed != 0.f;
                x = was - kTankSmearGain * fed;
            }
            // The sway: a slow turning pair, a sample at a time, put back on its circle every kTankSwayRound.
            const float c = swayC * swayCos - swayS * swaySin, sn = swayS * swayCos + swayC * swaySin;
            swayC = c, swayS = sn;
            if (++swayCount == kTankSwayRound) {
                const float r = 1.f / std::sqrt(swayC * swayC + swayS * swayS);
                swayC *= r, swayS *= r, swayCount = 0;
            }
            // Each line: read (swaying, four ways: a quarter turn apart, where the sway was at
            // the tick's start: it moves 0.007 of a sample a tick), turned down for its length, damped.
            if (tick_ + n == 0) swayAt(swayC, swayS);
            float o[kTankLines];
            for (int i = 0; i < kTankLines; ++i) {
                float v;
                {
                    const float* const w = swayW_[i & 3];
                    const unsigned k = at - len[i] - swayWhole_[i & 3] + 1u, m = mask[i];
                    const float* const l = line[i];
                    v = w[0] * l[k & m] + w[1] * l[(k - 1u) & m] + w[2] * l[(k - 2u) & m] + w[3] * l[(k - 3u) & m];
                }
                v *= sh[kGain + i];
                const float d = tiny(damp[i] + sh[kDamp] * (v - damp[i]));
                damp[i] = d;
                stirred = stirred || d != 0.f;
                o[i] = d + sh[kHighs + i] * (v - d);
            }
            // The two sides: four lines added and four taken away each, in two ways that are unrelated.
            const float outL = kTankOut * 0.35355339f * ((o[0] - o[1]) + (o[2] - o[3]) + (o[4] - o[5]) + (o[6] - o[7]));
            const float outR = kTankOut * 0.35355339f * ((o[0] + o[1]) - (o[2] + o[3]) + (o[4] + o[5]) - (o[6] + o[7]));
            // Mixed: eight into eight (Hadamard's, which keeps the sound's power).
            const float a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3], a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
            const float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
            float mixed[kTankLines] = {b0 + b4, b1 + b5, b2 + b6, b3 + b7, b0 - b4, b1 - b5, b2 - b6, b3 - b7};
            for (float& v : mixed) v *= 0.35355339f;
            // SHIMMER: four of the eight go round an octave up, by SHIMMER. (At none the shifter still takes in what
            // it would, so turning it up finds it full.)
            if (shimmer) {
                const int p1 = phase, p2 = phase < half ? phase + half : phase - half;
                const float w1 = window[p1], w2 = window[p2];
                // Each grain reads twice as fast as the ring is written: its delay falls a sample a sample, from a
                // whole window to 2 (where its window is shut).
                const unsigned ago1 = static_cast<unsigned>(grain - p1 + 1), ago2 = static_cast<unsigned>(grain - p2 + 1);
                const bool up = sh[kFall] != 0.f;
                for (int w = 0; w < kShimmerWays; ++w) {
                    low[w] = tiny(low[w] + lowC * (mixed[w] - low[w]));
                    float in2 = mixed[w] - low[w];
                    for (int f = 0; f < 2; ++f) { // four poles: two state-variable lowpasses (svf.hpp's)
                        const SvfShape& sv = f ? cutB : cutA;
                        float* const q = ic[2 * w + f];
                        const float v3 = in2 - q[1], v1 = sv.a1 * q[0] + sv.a2 * v3, v2 = q[1] + sv.a2 * q[0] + sv.a3 * v3;
                        q[0] = 2.f * v1 - q[0], q[1] = 2.f * v2 - q[1]; // (made an exact zero under kTiny at the tick's end, svf.hpp's way)
                        in2 = v2;
                    }
                    stirred = stirred || low[w] != 0.f;
                    const float put = std::fabs(in2) < kQuiet ? 0.f : in2;
                    float* const r = ring[w];
                    r[at & grainMask] = put;
                    stirred = stirred || put != 0.f;
                    if (up) mixed[w] = sh[kKeep] * mixed[w] + sh[kFall] * (w1 * r[(at - ago1) & grainMask] + w2 * r[(at - ago2) & grainMask]);
                }
                if (++phase == grain) phase = 0;
            }
            // Back into the lines, with the sound coming in (a different way round for each).
            const float xin = 0.35355339f * x;
            for (int i = 0; i < kTankLines; ++i) {
                float v = mixed[i] + kInSign[i] * xin;
                v = v > kTankCeiling ? kTankCeiling : (v < -kTankCeiling ? -kTankCeiling : v);
                v = std::fabs(v) < kQuiet ? 0.f : v;
                line[i][at & mask[i]] = v;
                stirred = stirred || v != 0.f;
            }
            ++at;
            // The sound out. At MIX 0, at rest, the sound as it came, to the bit. (The plain sound as the tank heard
            // it otherwise: sound under kNothing is none, as PLATE's.)
            const float dry = sh[kDry], wet = sh[kWet];
            if (dry != 1.f || wet != 0.f) {
                left[n] = heardIn(left[n]) * dry + wet * outL;
                right[n] = heardIn(right[n]) * dry + wet * outR;
            }
            // Asleep once nothing has been written but zeros, and every filter has been empty, for as long as the
            // longest ring: then every ring is zeros from end to end.
            quiet = stirred ? 0 : quiet + 1;
            asleep = quiet >= rest_;
            ++n;
        }
        for (int i = 0; i < kShapes; ++i) shape_.now[i] = sh[i];
        for (int i = 0; i < kTankLines; ++i) damp_[i] = damp[i];
        cutState_ = cut, swayC_ = swayC, swayS_ = swayS, swayCount_ = swayCount, phase_ = phase, quiet_ = quiet, at_ = at;
        if (shimmer)
            for (int w = 0; w < kShimmerWays; ++w) {
                for (float* q : {ic[2 * w], ic[2 * w + 1]}) {
                    q[0] = tiny(q[0]), q[1] = tiny(q[1]);
                    if (q[0] != 0.f || q[1] != 0.f) quiet_ = 0; // (a filter not empty: not quiet)
                }
                lowState_[w] = low[w];
                for (int f = 0; f < 2; ++f) grainCut_[2 * w + f].ic1 = ic[2 * w + f][0], grainCut_[2 * w + f].ic2 = ic[2 * w + f][1];
            }
        asleep_ = asleep;
        return n;
    }

    const Kind kind_;
    std::vector<float> line_[kTankLines], smear_[kTankSmears], swell_, grainRing_[kShimmerWays], window_;
    int len_[kTankLines] = {}, mask_[kTankLines] = {}, smearLen_[kTankSmears] = {}, smearMask_[kTankSmears] = {};
    int swellMask_ = 0, grainMask_ = 0, grain_ = 2, phase_ = 0, tick_ = 0, quiet_ = 0, rest_ = 1, swayCount_ = 0;
    unsigned at_ = 0;
    bool asleep_ = true, swellMoving_ = false, again_ = false;
    Gentle character_, decay_, tone_, mix_;
    Slide<kShapes> shape_;
    float damp_[kTankLines] = {}, cutState_ = 0.f, swellTarget_ = 0.f;
    float swayW_[4][4] = {};
    unsigned swayWhole_[4] = {};
    float swayMost_ = 5.f, swayTurn_ = 0.f, swayCos_ = 1.f, swaySin_ = 0.f, swayC_ = 1.f, swayS_ = 0.f;
    BloomTap taps_[kBloomTaps] = {};
    int tapAgo_[kBloomTaps] = {};
    float tapFrac_[kBloomTaps] = {};
    SvfShape cutA_, cutB_;
    Svf grainCut_[2 * kShimmerWays];
    float lowState_[kShimmerWays] = {}, lowC_ = 0.f;
};

} // namespace fx
} // namespace apotheca
