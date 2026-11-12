// HALL: a reverb with a dense, moving tail.
//
//   SIZE    the room: how long its eight delay lines are (a quarter of full
//           size to full), and the gap before it answers (2 to 30 ms)
//   DECAY   how long the tail lasts: 0.3 to 20 seconds, a real time (RT60,
//           the time to fall 60 dB), set by formula
//   TONE    low: dark, the highs die soon. High: bright, and the lows are thinned
//   MIX     from the sound alone to the room alone
//
// How it is built:
//
//   in -> gap (pre-delay) -> four smearing stages a side -+-> early sound
//                                                         |
//        +----------------- eight delay lines <-----------+
//        |   each read, damped, turned down to suit DECAY,
//        +-> mixed into all eight (a Hadamard matrix) and written back
//
// The numbers, all pinned by tests/effects_test.cpp:
//   - Lines of 37.1 to 101.9 ms at SIZE full (kLineMs), times 0.25 + 0.75 SIZE,
//     to the nearest sample. Turned, they glide (the tail bends in pitch for a
//     moment, as a room changing size would), and so does every gain that
//     follows from SIZE, DECAY and TONE: sample by sample, never in steps.
//   - Each line is turned down by exp(-6.908 x its length / DECAY) a trip:
//     that is what makes DECAY a real time, whatever SIZE is.
//   - TONE: above a corner of 600 Hz (dark) to 12 kHz (bright) the tail lasts
//     0.3 times as long. Above TONE 50%, the tail below 200 Hz is shortened
//     too, to 0.4 times at full.
//   - Two of the eight lines are bent, slowly: 0.4 ms either way at 0.37 and
//     0.53 Hz (about 1.6 and 2.3 cents), so the tail does not ring on one note.
//   - The sound going in is turned down for a small room and a long DECAY
//     (by the square root of SIZE's factor, and the fourth root of DECAY), so
//     that a held sound makes about as loud a room at any SIZE.
//
// ON A BUS (the plan's sends): the dry sound is dropped and MIX is the level.
// That is Unit's `bus_`, set by the "you are on a bus" call (setOnBus, which
// also calls changed()).

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kLines = 8;
constexpr float kLineMs[kLines] = {37.1f, 43.3f, 51.7f, 59.9f, 68.3f, 79.1f, 89.7f, 101.9f};
constexpr float kSmearMs[2][4] = {{4.77f, 3.59f, 12.73f, 9.31f}, {5.03f, 3.83f, 13.37f, 9.71f}}; // left, right
constexpr float kSmearGain[4] = {0.7f, 0.7f, 0.6f, 0.6f};
constexpr int kBent[2] = {2, 5};               // which lines are bent: one that sounds on the left, one on the right
constexpr float kBendHz[2] = {0.37f, 0.53f};
constexpr float kBendMs = 0.4f;                // either way
constexpr float kGapLeastMs = 2.f, kGapMostMs = 30.f;
constexpr float kShortest = 0.3f, kLongest = 20.f; // DECAY, seconds
constexpr float kHighsLast = 0.3f;             // above TONE's corner the tail lasts this much as long
constexpr float kLowCornerHz = 200.f;
constexpr float kSettle = 0.03f, kSizeSettle = 0.1f; // seconds for a turned setting to arrive
constexpr float kEarly = 0.5f, kTail = 0.42f, kDriveIn = 0.5f; // how loud the early sound and the tail are, and how hard the lines are fed
// NUMBERS TOO SMALL TO HOLD are slow, and a tail left alone fades straight
// into them. So the room is fed this much with the sound (360 dB down: the
// smearing, the lines and the damping all come to rest on it, not on
// nothing), and sound coming in under kNothing is none. Then no sum in here comes out too small
// to hold, with or without the processor's flush-to-zero; and once the room
// has held nothing louder than kQuiet for a quarter of a second it is
// asleep, and what comes out is exactly nothing. See glide.hpp, GOING TO SLEEP.
constexpr float kFloor = 1e-18f;

float onePole(float hz, float sr) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr ? 0.45f * sr : hz) / sr); }
float decaySeconds(float setting) { return kShortest * std::pow(kLongest / kShortest, clamp01(setting)); }
float sizeFactor(float setting) { return 0.25f + 0.75f * setting; }

class Hall : public Unit {
public:
    Hall() : Unit(kHallInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        const int most = static_cast<int>((kLineMs[kLines - 1] + kBendMs) * 1e-3f * sampleRate) + 8;
        int size = 64;
        while (size < most) size *= 2;
        mask_ = size - 1;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size), 0.f);
        int gap = 64;
        while (gap < static_cast<int>(kGapMostMs * 1e-3f * sampleRate) + 8) gap *= 2;
        gapMask_ = gap - 1;
        for (auto& line : gap_) line.assign(static_cast<std::size_t>(gap), 0.f);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) smear_[c][k].assign(static_cast<std::size_t>(kSmearMs[c][k] * 1e-3f * sampleRate) + 1, 0.f);
        restAfter_ = static_cast<int>(0.25f * sampleRate); // longer than the gap, the smearing and the longest line together
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        for (auto& line : gap_) line.assign(line.size(), 0.f);
        for (auto& side : smear_)
            for (auto& line : side) line.assign(line.size(), 0.f);
        for (auto& side : smearAt_)
            for (int& at : side) at = 0;
        for (int i = 0; i < kLines; ++i) high_[i] = low_[i] = 0.f;
        at_ = 0;
        phase_[0] = phase_[1] = 0.0;
        bend_[0] = bend_[1] = 0.f;
        quiet_ = 0;
        asleep_ = true; // nothing in the room
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (line_[0].empty() || frames <= 0) return;
        soundOnly(left, right, frames);
        float coming = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float l = std::fabs(left[i]), r = std::fabs(right[i]);
            coming = l > coming ? l : coming;
            coming = r > coming ? r : coming;
        }
        const bool silent = !(coming >= kNothing);
        if (asleep_) {
            if (silent) {
                // The room is empty, and nothing is coming in: the sound as it is at MIX 0, else none.
                if (dry_.to != 1.f)
                    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
                return;
            }
            asleep_ = false;
            quiet_ = 0;
            phase_[0] = phase_[1] = 0.0, bend_[0] = bend_[1] = 0.f; // an empty room starts as one fresh from the box does
            arrive();                                               // nothing is sounding: whatever was turned meanwhile is simply there
        }
        // Settings that were turned slide to where they are going; what follows from them is worked out again while they do.
        bool moved = false;
        if (size_.moving()) size_.begin(frames, glideK(frames, kSizeSettle, sr_)), size_.done(), moved = true;
        if (decay_.moving()) decay_.begin(frames, glideK(frames, kSettle, sr_)), decay_.done(), moved = true;
        if (tone_.moving()) tone_.begin(frames, glideK(frames, kSettle, sr_)), tone_.done(), moved = true;
        if (moved) follow();
        const float k = glideK(frames, kSettle, sr_), per = 1.f / static_cast<float>(frames);
        const float dDry = dry_.begin(frames, k), dWet = wet_.begin(frames, k);
        float dry = dry_.now, wet = wet_.now;
        const float dFeed = (feedTo_ - feed_) * per, dGap = (gapTo_ - gapLen_) * per;
        float feed = feed_, gapLen = gapLen_;
        float len[kLines], dLen[kLines];
        bool gliding = moved || gapTo_ != gapLen_; // on the way: every length is read between samples, and every gain slides
        for (int i = 0; i < kLines; ++i) {
            len[i] = len_[i];
            dLen[i] = (lenTo_[i] - len_[i]) * per;
            gliding = gliding || lenTo_[i] != len_[i];
        }
        // The two bent lines: a slow sine each, worked out at the block's ends and joined by a straight line.
        float bend[2], dBend[2], bendEnd[2];
        for (int j = 0; j < 2; ++j) {
            phase_[j] += 6.283185307179586 * kBendHz[j] * frames / sr_;
            if (phase_[j] > 6.283185307179586) phase_[j] -= 6.283185307179586;
            bendEnd[j] = kBendMs * 1e-3f * sr_ * static_cast<float>(std::sin(phase_[j]));
            bend[j] = bend_[j];
            dBend[j] = (bendEnd[j] - bend_[j]) * per;
        }
        // Everything the loop touches, in its own hands (so the compiler can keep it in registers).
        float* line[kLines];
        int whole[kLines];
        float high[kLines], low[kLines], gain[kLines], highs[kLines], lows[kLines], dGain[kLines], dHighs[kLines], dLows[kLines];
        for (int i = 0; i < kLines; ++i) {
            line[i] = line_[i].data();
            whole[i] = static_cast<int>(len_[i]); // at rest a length is a whole number of samples
            high[i] = high_[i], low[i] = low_[i], gain[i] = gain_[i], highs[i] = highs_[i], lows[i] = lows_[i];
            dGain[i] = (gainTo_[i] - gain_[i]) * per, dHighs[i] = (highsTo_[i] - highs_[i]) * per, dLows[i] = (lowsTo_[i] - lows_[i]) * per;
        }
        float* smear[2][4];
        int smearAt[2][4], smearSize[2][4];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) smear[c][k] = smear_[c][k].data(), smearAt[c][k] = smearAt_[c][k], smearSize[c][k] = static_cast<int>(smear_[c][k].size());
        float* const gapL = gap_[0].data();
        float* const gapR = gap_[1].data();
        const int mask = mask_, gapMask = gapMask_, gapWhole = static_cast<int>(gapLen_);
        const float highCut = highCut_, lowCut = lowCut_;
        const bool thinned = thinned_ || wasThinned_; // (either end of this block)
        wasThinned_ = thinned_;
        int at = at_;
        float loudest = 0.f;
        for (int n = 0; n < frames; ++n) {
            dry += dDry, wet += dWet, feed += dFeed;
            // (Sound too faint to be sound is none: see kFloor.)
            const float inL = std::fabs(left[n]) < kNothing ? 0.f : left[n], inR = std::fabs(right[n]) < kNothing ? 0.f : right[n];
            // The gap before the room answers.
            gapL[at & gapMask] = inL;
            gapR[at & gapMask] = inR;
            float sL, sR;
            if (gliding) {
                gapLen += dGap;
                const int ago = static_cast<int>(gapLen);
                float w[4];
                cubicWeights(gapLen - static_cast<float>(ago), w);
                sL = ringRead(gapL, gapMask, at, ago, w);
                sR = ringRead(gapR, gapMask, at, ago, w);
            } else {
                sL = gapL[(at - gapWhole) & gapMask];
                sR = gapR[(at - gapWhole) & gapMask];
            }
            sL += kFloor, sR += kFloor; // (see kFloor)
            // Smeared: four stages a side, each a short echo fed back on itself so that a click becomes a burst.
            for (int k = 0; k < 4; ++k) {
                const float wasA = smear[0][k][smearAt[0][k]], wasB = smear[1][k][smearAt[1][k]];
                const float fedA = sL + kSmearGain[k] * wasA, fedB = sR + kSmearGain[k] * wasB;
                smear[0][k][smearAt[0][k]] = fedA;
                smear[1][k][smearAt[1][k]] = fedB;
                sL = wasA - kSmearGain[k] * fedA;
                sR = wasB - kSmearGain[k] * fedB;
                if (++smearAt[0][k] >= smearSize[0][k]) smearAt[0][k] = 0;
                if (++smearAt[1][k] >= smearSize[1][k]) smearAt[1][k] = 0;
            }
            // What the eight lines hold from one trip ago.
            float v[kLines];
            bend[0] += dBend[0], bend[1] += dBend[1];
            if (gliding) {
                for (int i = 0; i < kLines; ++i) {
                    len[i] += dLen[i], gain[i] += dGain[i], highs[i] += dHighs[i], lows[i] += dLows[i];
                    v[i] = between(line[i], mask, at, len[i] + (i == kBent[0] ? bend[0] : (i == kBent[1] ? bend[1] : 0.f)));
                }
            } else {
                for (int i = 0; i < kLines; ++i) v[i] = line[i][(at - whole[i]) & mask];
                v[kBent[0]] = between(line[kBent[0]], mask, at, len[kBent[0]] + bend[0]);
                v[kBent[1]] = between(line[kBent[1]], mask, at, len[kBent[1]] + bend[1]);
            }
            // The room's sound: the even lines to the left, the odd to the right.
            const float tailL = v[0] - v[2] + v[4] - v[6], tailR = v[1] - v[3] + v[5] - v[7];
            // Each line damped and turned down for its trip...
            float u[kLines];
            for (int i = 0; i < kLines; ++i) {
                high[i] += highCut * (v[i] - high[i]);
                u[i] = high[i] + highs[i] * (v[i] - high[i]); // the highs times highs_
            }
            if (thinned) { // TONE above the middle: the lows less lows_
                for (int i = 0; i < kLines; ++i) {
                    low[i] += lowCut * (u[i] - low[i]);
                    u[i] -= lows[i] * low[i];
                }
            }
            for (int i = 0; i < kLines; ++i) u[i] *= gain[i]; // DECAY's share (with the matrix's 1/sqrt(8))
            // ...and mixed, each into all eight (Hadamard: energy in is energy out).
            for (int i = 0; i < kLines; i += 2) {
                const float a = u[i], b = u[i + 1];
                u[i] = a + b, u[i + 1] = a - b;
            }
            for (int i = 0; i < kLines; i += 4) {
                const float a = u[i], b = u[i + 1], c = u[i + 2], d = u[i + 3];
                u[i] = a + c, u[i + 1] = b + d, u[i + 2] = a - c, u[i + 3] = b - d;
            }
            // Back in, with what is arriving: the left to the even lines, the right to the odd.
            const float fedL = sL * feed, fedR = sR * feed;
            const float back[kLines] = {u[0] + u[4] + fedL, u[1] + u[5] + fedR, u[2] + u[6] - fedL, u[3] + u[7] - fedR,
                                        u[0] - u[4] - fedL, u[1] - u[5] - fedR, u[2] - u[6] + fedL, u[3] - u[7] + fedR};
            for (int i = 0; i < kLines; ++i) line[i][at & mask] = back[i];
            if (silent) // (only then can the room be on its way to empty)
                for (int i = 0; i < kLines; ++i) loudest = std::fabs(back[i]) > loudest ? std::fabs(back[i]) : loudest;
            left[n] = inL * dry + wet * (kEarly * sL + kTail * tailL);
            right[n] = inR * dry + wet * (kEarly * sR + kTail * tailR);
            ++at;
        }
        for (int i = 0; i < kLines; ++i) high_[i] = high[i], low_[i] = low[i];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) smearAt_[c][k] = smearAt[c][k];
        at_ = at & mask; // (the gap's ring is a whole fraction of the lines': it goes round in step)
        dry_.done(), wet_.done();
        feed_ = feedTo_, gapLen_ = gapTo_;
        for (int i = 0; i < kLines; ++i) len_[i] = lenTo_[i], gain_[i] = gainTo_[i], highs_[i] = highsTo_[i], lows_[i] = lowsTo_[i];
        bend_[0] = bendEnd[0], bend_[1] = bendEnd[1];
        // Asleep once nothing worth the name has gone round for longer than the room remembers.
        quiet_ = silent && loudest < kQuiet ? quiet_ + frames : 0;
        if (quiet_ >= restAfter_) asleep_ = true;
    }

protected:
    void changed() override {
        size_.to = p_[0];
        decay_.to = p_[1];
        tone_.to = p_[2];
        // MIX: the sound and the room swap over at equal power (they are unrelated sounds).
        const float mix = p_[3];
        dry_.to = bus_ ? 0.f : std::cos(1.5707963f * mix);
        wet_.to = bus_ ? mix : std::sin(1.5707963f * mix);
        if (mix >= 1.f && !bus_) dry_.to = 0.f, wet_.to = 1.f; // the room alone, exactly (and at 0 cos is 1 and sin is 0: the sound untouched, to the bit)
    }

private:
    // Everything that follows from SIZE, DECAY and TONE as they stand this block.
    void follow() {
        const float factor = sizeFactor(size_.now), seconds = decaySeconds(decay_.now), tone = tone_.now;
        const bool there = !size_.moving();
        const float gap = (kGapLeastMs + (kGapMostMs - kGapLeastMs) * size_.now) * 1e-3f * sr_;
        gapTo_ = there ? std::round(gap) : gap;
        highCut_ = onePole(600.f * std::pow(20.f, tone), sr_);
        lowCut_ = onePole(kLowCornerHz, sr_);
        const float lowsLast = tone > 0.5f ? 1.f - 1.2f * (tone - 0.5f) : 1.f; // the tail below 200 Hz lasts this much as long
        thinned_ = tone > 0.5f;
        for (int i = 0; i < kLines; ++i) {
            const float length = kLineMs[i] * 1e-3f * sr_ * factor;
            lenTo_[i] = there ? std::round(length) : length;
            const float trip = -6.9077553f * lenTo_[i] / (sr_ * seconds); // the natural log of what one trip leaves
            gainTo_[i] = std::exp(trip) * 0.35355339f;
            highsTo_[i] = std::exp(trip * (1.f / kHighsLast - 1.f));
            lowsTo_[i] = 1.f - std::exp(trip * (1.f / lowsLast - 1.f));
        }
        feedTo_ = kDriveIn * std::sqrt(factor) * std::pow(2.f / seconds, 0.25f);
    }
    // Everything that glides is where it was turned to, at once.
    void arrive() {
        size_.snap(), decay_.snap(), tone_.snap(), dry_.snap(), wet_.snap();
        follow();
        feed_ = feedTo_, gapLen_ = gapTo_;
        for (int i = 0; i < kLines; ++i) len_[i] = lenTo_[i], gain_[i] = gainTo_[i], highs_[i] = highsTo_[i], lows_[i] = lowsTo_[i];
    }
    // The sound `ago` samples before place `at`, read between samples.
    static float between(const float* line, int mask, int at, float ago) {
        const int whole = static_cast<int>(ago);
        float w[4];
        cubicWeights(ago - static_cast<float>(whole), w);
        return ringRead(line, mask, at, whole, w);
    }
    // The sound `whole + f` samples before place `at` in a ring whose size is mask + 1.
    static float ringRead(const float* line, int mask, int at, int whole, const float* w) {
        const int i = at - whole + 1;
        return w[0] * line[i & mask] + w[1] * line[(i - 1) & mask] + w[2] * line[(i - 2) & mask] + w[3] * line[(i - 3) & mask];
    }

    std::vector<float> line_[kLines], gap_[2], smear_[2][4];
    int smearAt_[2][4] = {};
    int mask_ = 0, gapMask_ = 0, at_ = 0, quiet_ = 0, restAfter_ = 12000;
    bool asleep_ = true, thinned_ = false, wasThinned_ = false;
    Glide size_, decay_, tone_, dry_, wet_;
    float len_[kLines] = {}, lenTo_[kLines] = {}, gain_[kLines] = {}, highs_[kLines] = {}, lows_[kLines] = {};
    float gainTo_[kLines] = {}, highsTo_[kLines] = {}, lowsTo_[kLines] = {};
    float high_[kLines] = {}, low_[kLines] = {};
    float highCut_ = 1.f, lowCut_ = 0.f, feed_ = 0.f, feedTo_ = 0.f, gapLen_ = 2.f, gapTo_ = 2.f;
    double phase_[2] = {};
    float bend_[2] = {};
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index != 1) return readPercent(v, out, size);
    const float seconds = decaySeconds(v);
    std::snprintf(out, static_cast<std::size_t>(size), seconds < 9.95f ? "%.1f S" : "%.0f S", static_cast<double>(seconds));
}

Effect* create() { return new Hall; }

} // namespace

const EffectInfo kHallInfo = {
    "HALL", 4, {"SIZE", "DECAY", "TONE", "MIX"}, {0.6f, 0.45f, 0.5f, 0.3f}, // 2.0 seconds
    "hall", EffectGroup::Reverb, {0, 0, 0, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
