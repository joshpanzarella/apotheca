// PLATE: a plate reverb. A dense wash from the first moment, bright and
// smooth, and an even fall: no early echoes to pick out.
//
//   PRE     a gap before the plate answers: 0 to 100 ms
//   DECAY   how long the tail lasts: 0.3 to 20 seconds, a real time (RT60, the
//           time to fall 60 dB), read and set as HALL's is
//   TONE    low: dark, the highs die soon. High: bright. At any TONE the
//           lowest notes die a little sooner than the rest (a plate is thin at the bottom)
//   MIX     from the sound alone to the plate alone
//
// How it is built (the well-known figure-of-eight tank):
//
//   in (left and right together) -> PRE -> a gentle high cut -> eight smearing stages -+
//                                                                                      |
//      +--------------------------------------------------------------------------+    |
//      |                                                                          v    v
//      +-- x DECAY <- delay <- smear <- x DECAY <- damping <- delay <- bent smear <-(+)  LEFT LOOP
//      |                                                                               |
//      +-> (+) -> bent smear -> delay -> damping -> x DECAY -> smear -> delay -> x DECAY --+  RIGHT LOOP
//           ^                                                                              |
//           +-- the sound coming in                       (each loop feeds the other) <----+
//
//   Each side's sound is seven taps, taken from both loops (some added, some
//   taken away), so left and right are nearly unrelated.
//
// The numbers, all pinned by tests/effects_test.cpp (testPlate):
//   - Every length is a TIME (kSmearMs, kLoopMs, kTaps: the classic plate's
//     lengths, made times), turned into samples at the sample rate.
//   - Four short smearing stages (0.4 to 1.6 ms) go ahead of the classic four,
//     so that a click is a wash from the plate's first sound: without them it
//     was a click, a silent gap and another click in the first 12 ms.
//   - Each half of a loop is turned down by exp(-6.908 x its length / DECAY):
//     that is what makes DECAY a real time. Each TAP is turned down the same way
//     for how far into its half it reads, so the fall starts at once: read
//     before the half's gain, a tap 120 ms in was as loud as one 5 ms in, and a
//     0.3 s plate stood level for 170 ms. The fourteen taps are then turned up
//     together to kOut on average (a short DECAY turns them down most), and the
//     sound going in is turned down for a long DECAY (DECAY to the power 0.33),
//     so that a held sound makes about as loud a plate at any DECAY.
//   - A smearing stage rings by itself
//     too, so none is let ring longer than kRing (0.45) of DECAY; and the second
//     smear in each loop is turned the other way from the classic plate's
//     (-0.4, for +0.5), which held the lows 15% longer than DECAY said.
//   - TONE: the damping in the loops is a shelf with its corner at 1.2 kHz
//     (dark) to 18 kHz (bright), 15 times higher: above it the tail lasts 0.4 of
//     DECAY, at any DECAY (a plain lowpass left 8 kHz lasting 0.27 of 20 s). A
//     second shelf makes what is under 200 Hz last 0.75 of it. Both are set from
//     the trip round the loop, as HALL's are. The cut on the way in is at three
//     times the corner.
//   - The two bent smears move 0.03 ms either way at 0.9 Hz, a quarter of a
//     turn apart, so the tail does not ring on one note. (At 0.4 ms, the classic
//     plate's, a held sine's level in it wandered 25 dB; at 0.03 ms 6 dB; HALL 5.)
//     The sine is worked out every 64 samples the plate has run (not at a
//     block's end) and joined by straight lines.
//   - Left and right go in together, as the classic plate takes them: a sound
//     on one side fills both alike.
//
// GOING TO SLEEP and waking are each on their own SAMPLE, not at a block's
// edge (as TAPE's are), so the sound is the same to the bit however it is cut
// into blocks, through its sleep and its waking. It falls asleep when nothing
// has come in and what it gives out (the taps, which read every piece of both
// loops) has stayed under kQuiet for kRestSeconds.
//
// ON A BUS (the plan's sends): the dry sound is dropped and MIX is the level.
// That is Unit's `bus_`, read in changed() and nowhere else.

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

// The classic plate's lengths were counted in samples at 29761 Hz; here they are times.
// Four short stages first (not the classic plate's: they fill its first few milliseconds), then its four.
constexpr int kSmears = 8;
constexpr float kSmearMs[kSmears] = {0.41f, 0.67f, 1.03f, 1.61f, 4.771f, 3.595f, 12.735f, 9.307f};
constexpr float kSmearGain[kSmears] = {0.6f, 0.6f, 0.6f, 0.6f, 0.75f, 0.75f, 0.625f, 0.625f};
// Each loop: the bent smear, the first delay, the second smear, the second delay.
constexpr float kLoopMs[2][4] = {{22.580f, 149.625f, 60.482f, 124.995f}, {30.510f, 141.695f, 89.244f, 106.280f}};
constexpr float kBentGain = -0.7f, kSecondGain = -0.4f;
// The taps for each side: from which loop, which piece (0 the bent smear's line... 3 the second delay), how far back, and which way.
struct Tap {
    int loop, piece;
    float ms, sign;
};
constexpr Tap kTaps[2][7] = {
    {{1, 1, 8.938f, 1.f}, {1, 1, 99.931f, 1.f}, {1, 2, 64.279f, -1.f}, {1, 3, 67.068f, 1.f}, {0, 1, 66.865f, -1.f}, {0, 2, 6.283f, -1.f}, {0, 3, 35.819f, -1.f}},
    {{0, 1, 11.861f, 1.f}, {0, 1, 121.871f, 1.f}, {0, 2, 41.262f, -1.f}, {0, 3, 89.815f, 1.f}, {1, 1, 70.932f, -1.f}, {1, 2, 11.257f, -1.f}, {1, 3, 4.066f, -1.f}},
};
constexpr float kBendHz = 0.9f, kBendMs = 0.03f; // either way
constexpr int kBendEvery = 64;                   // samples between two points of the bend's sine
constexpr float kPreMostMs = 100.f;
constexpr float kShortest = 0.3f, kLongest = 20.f; // DECAY, seconds (as HALL's)
constexpr float kDampLeastHz = 1200.f, kDampRange = 15.f; // TONE's corner: 1.2 kHz to 18 kHz
constexpr float kInCut = 3.f;                    // the gentle high cut on the way in: this many times TONE's corner
constexpr float kHighsLast = 0.4f;  // above TONE's corner the tail lasts this much as long as DECAY, at any DECAY
constexpr float kLowCornerHz = 200.f, kLowsLast = 0.75f; // and below this, this much as long: a plate is thin at the bottom
constexpr float kRing = 0.45f;                   // no smearing stage rings longer than this much of DECAY
constexpr float kPreSettle = 0.1f; // PRE: the gap changes length slowly, as HALL's SIZE does (the sound in it bends in pitch on the way)
constexpr float kSettle = 0.03f;                // seconds for a turned setting to arrive
constexpr float kDecaySettle = 0.1f;            // DECAY slower: from 0.3 s up, the far taps are turned up thirty times
constexpr float kDriveIn = 0.55f, kOut = 0.8f;  // how hard the loops are fed, and how loud the taps are
constexpr float kFeedLong = 0.33f;              // the feed falls as DECAY to this power: a held sound about as loud at any DECAY
constexpr float kRestSeconds = 1.f; // nothing in it louder than kQuiet for this long, with nothing coming in: asleep. Longer than PRE, the smearing and both loops together
// NUMBERS TOO SMALL TO HOLD (as HALL): the plate is fed this much with the
// sound, so every line and filter comes to rest on it, not on nothing.
constexpr float kFloor = 1e-18f;

float onePole(float hz, float sr) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr ? 0.45f * sr : hz) / sr); }
float decaySeconds(float setting) { return kShortest * std::pow(kLongest / kShortest, clamp01(setting)); }
int ringFor(float samples) {
    int size = 64;
    while (size < static_cast<int>(samples) + 8) size *= 2;
    return size;
}

class Plate : public Unit {
public:
    Plate() : Unit(kPlateInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        const float perMs = 1e-3f * sampleRate;
        preMask_ = ringFor(kPreMostMs * perMs) - 1;
        pre_.assign(static_cast<std::size_t>(preMask_ + 1), 0.f);
        for (int k = 0; k < kSmears; ++k) {
            smearLen_[k] = static_cast<int>(std::lround(kSmearMs[k] * perMs));
            smearMask_[k] = ringFor(static_cast<float>(smearLen_[k])) - 1;
            smear_[k].assign(static_cast<std::size_t>(smearMask_[k] + 1), 0.f);
        }
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 4; ++k) {
                len_[s][k] = static_cast<int>(std::lround(kLoopMs[s][k] * perMs));
                mask_[s][k] = ringFor(static_cast<float>(len_[s][k]) + (k == 0 ? kBendMs * perMs + 4.f : 0.f)) - 1;
                loop_[s][k].assign(static_cast<std::size_t>(mask_[s][k] + 1), 0.f);
            }
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 7; ++t) tapAgo_[c][t] = static_cast<int>(std::lround(kTaps[c][t].ms * perMs));
        bendMost_ = kBendMs * perMs;
        rest_ = static_cast<int>(kRestSeconds * sampleRate);
        changed();
        reset();
    }
    void reset() override {
        pre_.assign(pre_.size(), 0.f);
        for (auto& line : smear_) line.assign(line.size(), 0.f);
        for (auto& side : loop_)
            for (auto& line : side) line.assign(line.size(), 0.f);
        band_ = damp_[0] = damp_[1] = low_[0] = low_[1] = 0.f;
        at_ = 0;
        quiet_ = 0;
        asleep_ = true; // nothing on the plate
        wake();
        asleep_ = true;
    }
    // Asleep and awake by turns, each change on its own SAMPLE (as TAPE).
    void process(float* left, float* right, int frames) override {
        if (pre_.empty() || frames <= 0) return;
        soundOnly(left, right, frames);
        while (frames > 0) {
            int n = 0;
            if (asleep_) {
                while (n < frames && !(std::fabs(left[n]) >= kNothing) && !(std::fabs(right[n]) >= kNothing)) ++n;
                // The plate is empty, and nothing is coming in: the sound as it is at MIX 0, else none.
                if (dry_.to != 1.f)
                    for (int i = 0; i < n; ++i) left[i] = right[i] = 0.f;
                if (n < frames) wake(); // at this sample
            } else {
                n = rest_ - quiet_ < frames ? rest_ - quiet_ : frames; // no further than where it could fall asleep
                run(left, right, n);
                if (quiet_ >= rest_) asleep_ = true;
            }
            left += n, right += n, frames -= n;
        }
    }

protected:
    void changed() override {
        preTo_ = std::round(p_[0] * kPreMostMs * 1e-3f * sr_); // a whole number of samples
        preGlide_.to = preTo_;
        decay_.to = p_[1];
        tone_.to = p_[2];
        // MIX: the sound and the plate swap over at equal power (they are unrelated sounds).
        const float mix = p_[3];
        dry_.to = bus_ ? 0.f : std::cos(1.5707963f * mix);
        wet_.to = bus_ ? mix : std::sin(1.5707963f * mix);
        if (mix >= 1.f && !bus_) dry_.to = 0.f, wet_.to = 1.f; // the plate alone, exactly (at 0 cos is 1 and sin 0: the sound untouched, to the bit)
    }

private:
    // A fresh plate: everything that glides is where it was turned to, and the bend starts again.
    void wake() {
        asleep_ = false;
        quiet_ = 0;
        phase_ = 0.0, bend_ = bendStep_ = 0.f, bendQ_ = 1.f, bendQStep_ = 0.f, bendLeft_ = 0;
        preGlide_.snap(), decay_.snap(), tone_.snap(), dry_.snap(), wet_.snap();
        follow();
        snapFollowers();
    }
    // What follows from the settings is where they are going, at once.
    void snapFollowers() {
        for (int s = 0; s < 2; ++s) gain_[s][0] = gainTo_[s][0], gain_[s][1] = gainTo_[s][1], highs_[s] = highsTo_[s], lows_[s] = lowsTo_[s];
        dampC_ = dampTo_, bandC_ = bandTo_, feed_ = feedTo_;
        for (int j = 0; j < kSmears; ++j) smearG_[j] = smearGTo_[j];
        for (int s = 0; s < 2; ++s) loopG_[s][0] = loopGTo_[s][0], loopG_[s][1] = loopGTo_[s][1];
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 7; ++t) tapG_[c][t] = tapGTo_[c][t];
    }
    // Everything that follows from DECAY and TONE as they stand.
    void follow() {
        const float seconds = decaySeconds(decay_.now), tone = tone_.now;
        for (int s = 0; s < 2; ++s) {
            const float first = static_cast<float>(len_[s][0] + len_[s][1]), second = static_cast<float>(len_[s][2] + len_[s][3]);
            gainTo_[s][0] = std::exp(-6.9077553f * first / (sr_ * seconds));
            gainTo_[s][1] = std::exp(-6.9077553f * second / (sr_ * seconds));
        }
        // The damping, once round each loop (all four pieces): above TONE's corner the tail lasts kHighsLast as
        // long, below kLowCornerHz kLowsLast as long, at any DECAY (as HALL's: the shelf is set from the trip).
        for (int s = 0; s < 2; ++s) {
            const float trip = -6.9077553f * static_cast<float>(len_[s][0] + len_[s][1] + len_[s][2] + len_[s][3]) / (sr_ * seconds);
            highsTo_[s] = std::exp(trip * (1.f / kHighsLast - 1.f));
            lowsTo_[s] = 1.f - std::exp(trip * (1.f / kLowsLast - 1.f));
        }
        // Each tap is turned down for how far into its half of the loop it is, so the fall begins at once and
        // goes on evenly (DECAY's gains are taken at the end of each half: a tap read before them, 120 ms in,
        // was as loud as one 5 ms in, and a short plate stood level for its first 170 ms). Then all fourteen
        // are turned up together to kOut on average (a short DECAY turns its taps down most), at the taps and
        // not on the way in: turned up on the way in, a plate fed hard at 0.3 s and then turned to 20 s read
        // the loud loops through taps turned up, and jumped three times as loud.
        float taps = 0.f;
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 7; ++t) {
                const Tap& tap = kTaps[c][t];
                const int* const l = len_[tap.loop];
                const float into = static_cast<float>(tapAgo_[c][t] + (tap.piece == 1 ? l[0] : (tap.piece == 3 ? l[2] : 0)));
                tapGTo_[c][t] = tap.sign * std::exp(-6.9077553f * into / (sr_ * seconds));
                taps += tapGTo_[c][t] * tapGTo_[c][t];
            }
        const float up = kOut / std::sqrt(taps / 14.f);
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 7; ++t) tapGTo_[c][t] *= up;
        const float corner = kDampLeastHz * std::pow(kDampRange, tone);
        dampTo_ = onePole(corner, sr_);
        lowC_ = onePole(kLowCornerHz, sr_);
        bandTo_ = onePole(kInCut * corner, sr_);
        feedTo_ = kDriveIn * std::pow(2.f / seconds, kFeedLong); // (less for a long DECAY)
        // A smearing stage rings by itself for a while (its own RT60: 6.9 x its length / -ln g). For a short DECAY that
        // would be the tail: so none rings longer than kRing of DECAY (nearer nothing it smears less).
        const auto held = [&](float g, float ms) {
            const float most = std::exp(-6.9077553f * ms * 1e-3f / (kRing * seconds));
            return g < 0.f ? (-g > most ? -most : g) : (g > most ? most : g);
        };
        for (int j = 0; j < kSmears; ++j) smearGTo_[j] = held(kSmearGain[j], kSmearMs[j]);
        for (int s = 0; s < 2; ++s) loopGTo_[s][0] = held(kBentGain, kLoopMs[s][0]), loopGTo_[s][1] = held(kSecondGain, kLoopMs[s][2]);
    }
    // A stretch of the plate running.
    void run(float* left, float* right, int frames) {
        const float k = glideK(frames, kSettle, sr_), per = 1.f / static_cast<float>(frames);
        bool moved = false;
        if (decay_.moving()) decay_.begin(frames, glideK(frames, kDecaySettle, sr_)), decay_.done(), moved = true;
        if (tone_.moving()) tone_.begin(frames, k), tone_.done(), moved = true;
        if (moved) follow();
        const float dDry = dry_.begin(frames, k), dWet = wet_.begin(frames, k);
        float dry = dry_.now, wet = wet_.now;
        const bool preMoving = preGlide_.moving();
        const float dPre = preGlide_.begin(frames, glideK(frames, kPreSettle, sr_));
        float pre = preGlide_.now;
        const int preWhole = static_cast<int>(preTo_);
        // What follows from the settings slides sample by sample (at rest these steps are 0).
        float gain[2][2], dGain[2][2];
        for (int s = 0; s < 2; ++s)
            for (int j = 0; j < 2; ++j) gain[s][j] = gain_[s][j], dGain[s][j] = (gainTo_[s][j] - gain_[s][j]) * per;
        float dampC = dampC_, bandC = bandC_, feed = feed_, highs[2] = {highs_[0], highs_[1]}, lows[2] = {lows_[0], lows_[1]};
        const float dHighs[2] = {(highsTo_[0] - highs_[0]) * per, (highsTo_[1] - highs_[1]) * per}, dLows[2] = {(lowsTo_[0] - lows_[0]) * per, (lowsTo_[1] - lows_[1]) * per};
        const float lowC = lowC_;
        float tapG[2][7], dTapG[2][7];
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 7; ++t) tapG[c][t] = tapG_[c][t], dTapG[c][t] = (tapGTo_[c][t] - tapG_[c][t]) * per;
        const float dDamp = (dampTo_ - dampC_) * per, dBand = (bandTo_ - bandC_) * per, dFeed = (feedTo_ - feed_) * per;
        // Everything the loop touches, in its own hands.
        float* const preLine = pre_.data();
        const int preMask = preMask_;
        float* smear[kSmears];
        int smearMask[kSmears], smearLen[kSmears];
        float smearG[kSmears], dSmearG[kSmears], loopG[2][2], dLoopG[2][2];
        for (int j = 0; j < kSmears; ++j) {
            smear[j] = smear_[j].data(), smearMask[j] = smearMask_[j], smearLen[j] = smearLen_[j];
            smearG[j] = smearG_[j], dSmearG[j] = (smearGTo_[j] - smearG_[j]) * per;
        }
        for (int s = 0; s < 2; ++s)
            for (int j = 0; j < 2; ++j) loopG[s][j] = loopG_[s][j], dLoopG[s][j] = (loopGTo_[s][j] - loopG_[s][j]) * per;
        float* line[2][4];
        int mask[2][4], len[2][4];
        for (int s = 0; s < 2; ++s)
            for (int j = 0; j < 4; ++j) line[s][j] = loop_[s][j].data(), mask[s][j] = mask_[s][j], len[s][j] = len_[s][j];
        float band = band_, damp[2] = {damp_[0], damp_[1]}, low[2] = {low_[0], low_[1]};
        float bend = bend_, bendStep = bendStep_, bendQ = bendQ_, bendQStep = bendQStep_;
        int bendLeft = bendLeft_;
        const float bendMost = bendMost_;
        int quiet = quiet_;
        unsigned at = at_;
        for (int n = 0; n < frames; ++n) {
            dry += dDry, wet += dWet;
            if (moved) { // (DECAY or TONE turned: else every step here is 0, and adding 0 changes no bit)
                dampC += dDamp, bandC += dBand, feed += dFeed;
                for (int s = 0; s < 2; ++s) gain[s][0] += dGain[s][0], gain[s][1] += dGain[s][1], loopG[s][0] += dLoopG[s][0], loopG[s][1] += dLoopG[s][1], highs[s] += dHighs[s], lows[s] += dLows[s];
                for (int c = 0; c < 2; ++c)
                    for (int t = 0; t < 7; ++t) tapG[c][t] += dTapG[c][t];
                for (int j = 0; j < kSmears; ++j) smearG[j] += dSmearG[j];
            }
            // The bend: a slow sine and the same a quarter turn on, a point every kBendEvery samples, straight lines between.
            if (bendLeft == 0) {
                phase_ += 6.283185307179586 * kBendHz * kBendEvery / sr_;
                if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
                bendStep = (static_cast<float>(std::sin(phase_)) - bend) / static_cast<float>(kBendEvery);
                bendQStep = (static_cast<float>(std::cos(phase_)) - bendQ) / static_cast<float>(kBendEvery);
                bendLeft = kBendEvery;
            }
            bend += bendStep, bendQ += bendQStep, --bendLeft;
            // (Sound too faint to be sound is none: see kFloor.)
            const float inL = std::fabs(left[n]) < kNothing ? 0.f : left[n], inR = std::fabs(right[n]) < kNothing ? 0.f : right[n];
            // PRE: the gap.
            preLine[at & static_cast<unsigned>(preMask)] = 0.5f * (inL + inR);
            float x;
            if (preMoving) {
                pre += dPre;
                x = between(preLine, preMask, at, pre);
            } else {
                x = preLine[(at - static_cast<unsigned>(preWhole)) & static_cast<unsigned>(preMask)];
            }
            x = x * feed + kFloor;
            // A gentle high cut, then eight smearing stages: a click becomes a burst.
            band += bandC * (x - band);
            x = band;
            for (int j = 0; j < kSmears; ++j) {
                float* const l = smear[j];
                const float was = l[(at - static_cast<unsigned>(smearLen[j])) & static_cast<unsigned>(smearMask[j])];
                const float fed = x + smearG[j] * was;
                l[at & static_cast<unsigned>(smearMask[j])] = fed;
                x = was - smearG[j] * fed;
            }
            // What each loop gives the other: the end of its second delay, turned down for its trip.
            float end[2];
            for (int s = 0; s < 2; ++s) end[s] = line[s][3][(at - static_cast<unsigned>(len[s][3])) & static_cast<unsigned>(mask[s][3])] * gain[s][1];
            for (int s = 0; s < 2; ++s) {
                float* const* const piece = line[s];
                const int* const m = mask[s];
                const int* const l = len[s];
                // The bent smear.
                const float was = between(piece[0], m[0], at, static_cast<float>(l[0]) + bendMost * (s == 0 ? bend : bendQ));
                const float g1 = loopG[s][0], g2 = loopG[s][1];
                const float fed = x + end[1 - s] + g1 * was;
                piece[0][at & static_cast<unsigned>(m[0])] = fed;
                piece[1][at & static_cast<unsigned>(m[1])] = was - g1 * fed;
                // The first delay, damped (the highs less highs, the lows less lows) and turned down.
                const float v = piece[1][(at - static_cast<unsigned>(l[1])) & static_cast<unsigned>(m[1])];
                damp[s] += dampC * (v - damp[s]);
                float u = damp[s] + highs[s] * (v - damp[s]);
                low[s] += lowC * (u - low[s]);
                u -= lows[s] * low[s];
                const float into = u * gain[s][0];
                // The second smear, into the second delay.
                const float was2 = piece[2][(at - static_cast<unsigned>(l[2])) & static_cast<unsigned>(m[2])];
                const float fed2 = into + g2 * was2;
                piece[2][at & static_cast<unsigned>(m[2])] = fed2;
                const float out2 = was2 - g2 * fed2;
                piece[3][at & static_cast<unsigned>(m[3])] = out2;
            }
            // Each side: seven taps from the two loops.
            float side[2];
            for (int c = 0; c < 2; ++c) {
                float sum = 0.f;
                for (int t = 0; t < 7; ++t) {
                    const Tap& tap = kTaps[c][t];
                    sum += tapG[c][t] * line[tap.loop][tap.piece][(at - static_cast<unsigned>(tapAgo_[c][t])) & static_cast<unsigned>(mask[tap.loop][tap.piece])];
                }
                side[c] = sum;
            }
            // At MIX 0, at rest, the sound as it came, to the bit (the faintest of it and a -0 too).
            if (dry != 1.f || wet != 0.f) {
                left[n] = inL * dry + wet * side[0];
                right[n] = inR * dry + wet * side[1];
            }
            // Asleep once nothing worth the name has come in or come out (the plate's own sound, before MIX) for
            // longer than the plate remembers: the taps read all of both loops, each trip round is under 0.4 s.
            quiet = inL == 0.f && inR == 0.f && std::fabs(side[0]) < kQuiet && std::fabs(side[1]) < kQuiet ? quiet + 1 : 0;
            ++at;
        }
        at_ = at;
        band_ = band, damp_[0] = damp[0], damp_[1] = damp[1], low_[0] = low[0], low_[1] = low[1];
        bend_ = bend, bendStep_ = bendStep, bendQ_ = bendQ, bendQStep_ = bendQStep, bendLeft_ = bendLeft;
        quiet_ = quiet;
        dry_.done(), wet_.done(), preGlide_.done();
        snapFollowers();
    }
    // The sound `ago` samples before place `at` (written at `at` already), read between samples.
    static float between(const float* line, int mask, unsigned at, float ago) {
        const int whole = static_cast<int>(ago);
        const float f = ago - static_cast<float>(whole);
        const unsigned m = static_cast<unsigned>(mask);
        if (whole < 2) { // (too near the newest sample for four neighbours: a straight line between two)
            const float a = line[(at - static_cast<unsigned>(whole)) & m], b = line[(at - static_cast<unsigned>(whole) - 1u) & m];
            return a + f * (b - a);
        }
        float w[4];
        cubicWeights(f, w);
        const unsigned i = at - static_cast<unsigned>(whole) + 1u;
        return w[0] * line[i & m] + w[1] * line[(i - 1u) & m] + w[2] * line[(i - 2u) & m] + w[3] * line[(i - 3u) & m];
    }

    std::vector<float> pre_, smear_[kSmears], loop_[2][4];
    float smearG_[kSmears] = {}, smearGTo_[kSmears] = {}, loopG_[2][2] = {}, loopGTo_[2][2] = {};
    int preMask_ = 0, smearMask_[kSmears] = {}, smearLen_[kSmears] = {}, mask_[2][4] = {}, len_[2][4] = {}, tapAgo_[2][7] = {};
    unsigned at_ = 0;
    int quiet_ = 0, rest_ = 48000;
    bool asleep_ = true;
    Glide preGlide_, decay_, tone_, dry_, wet_;
    float preTo_ = 0.f;
    float gain_[2][2] = {}, gainTo_[2][2] = {}, dampC_ = 1.f, dampTo_ = 1.f, bandC_ = 1.f, bandTo_ = 1.f, feed_ = 0.f, feedTo_ = 0.f;
    float band_ = 0.f, damp_[2] = {}, low_[2] = {};
    float highs_[2] = {}, highsTo_[2] = {}, lows_[2] = {}, lowsTo_[2] = {}, lowC_ = 0.f, tapG_[2][7] = {}, tapGTo_[2][7] = {};
    double phase_ = 0.0;
    float bend_ = 0.f, bendStep_ = 0.f, bendQ_ = 1.f, bendQStep_ = 0.f, bendMost_ = 19.2f;
    int bendLeft_ = 0;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) {
        std::snprintf(out, static_cast<std::size_t>(size), "%d MS", static_cast<int>(std::lround(clamp01(v) * kPreMostMs)));
        return;
    }
    if (index != 1) return readPercent(v, out, size);
    const float seconds = decaySeconds(v);
    std::snprintf(out, static_cast<std::size_t>(size), seconds < 9.95f ? "%.1f S" : "%.0f S", static_cast<double>(seconds));
}

Effect* create() { return new Plate; }

} // namespace

const EffectInfo kPlateInfo = {
    "PLATE", 4, {"PRE", "DECAY", "TONE", "MIX"}, {0.1f, 0.45f, 0.6f, 0.3f}, // 10 ms, 2.0 seconds
    "plate", EffectFamily::Reverb, {0, 0, 0, 0}, reading, create,
    "", "PLATE",
};

} // namespace fx
} // namespace apotheca
