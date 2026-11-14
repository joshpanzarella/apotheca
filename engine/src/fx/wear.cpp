// WEAR: an old tape or a worn record, a TYPE of CRUSH. The pitch sways, hiss
// and crackle ride on the sound, now and then it drops away for a moment, and
// the top and the bottom are worn off.
//
//   WOW    the pitch swaying: a slow sway (0.55 Hz), a faster flutter
//          (6.3 Hz) and a slow drift that never repeats. At 100% about
//          30 cents either way at worst; at 30% a faint seasick lean.
//          Turned, it moves at most all of its way in a second
//          (kWowPerSecond), so turning it bends the pitch no more than
//          its sway does
//   DIRT   hiss and crackle. Both ride on the sound: they come with it and
//          fade out with it (about 0.15 s after), so silence stays silence.
//          At 100% the hiss is about 20 dB under the sound and it crackles
//          about 25 times a second; at 25%, 16 times quieter and a few a second
//   DROPS  the chance of a dropout: for a moment (40 ms and up) the sound
//          sinks 6 to 24 dB and comes back, as where the tape was creased.
//          At 100%, nearly one in three moments; at 20%, one every few seconds
//   TONE   how worn: the top rolled off (20 kHz down to 2.5 kHz) and the
//          bottom thinned (up to 150 Hz). 0 is not worn at all
//
// No MIX: the swaying sound laid under the plain sound is a flanger, not
// wear. Every setting at 0 is the sound untouched, to the sample.
//
// THE DICE. Every hiss sample, every crackle and every dropout is a roll
// worked out from WHERE: the count of samples since it woke (or was made), and
// what the roll is for. Never a generator walked, so a bounce is the same as
// the live play, however it is cut into blocks. It sleeps only after four
// seconds of nothing (kRestSeconds, as RING), since the sway and the dice run
// on a clock: hits closer than that meet them where they have got to; hits
// further apart all start the same (each wakes as a fresh one).
//
// Every setting glides on the TICK (svf.hpp); the tone's numbers slide sample
// by sample (Slide). Anything that fades is made an exact zero under kFaint,
// on the tick's sample.

#include <cstdint>
#include <initializer_list>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kLine = 2048; // the sway's delay line: room for 10 ms at 192 kHz; a power of two
constexpr float kWowHz = 0.55f, kFlutterHz = 6.3f;
constexpr float kWowSeconds = 0.006f, kFlutterSeconds = 0.00012f, kDriftSeconds = 0.004f; // at WOW 100%
constexpr float kDriftEvery = 0.7f; // seconds between two of the drift's rolls
constexpr float kHiss = 0.1f;       // the hiss at DIRT 100%, against the sound's level
constexpr float kCrackles = 25.f;   // a second, at DIRT 100%
constexpr float kCrackle = 0.6f;    // a crackle at its loudest, against the sound's level
constexpr float kDropEvery = 0.04f; // seconds: a roll for a dropout each
constexpr float kDropMost = 0.3f;   // the chance at DROPS 100%
constexpr float kRestSeconds = 4.f;
constexpr float kSettle = 0.02f;
// WOW moves at most this much a second (all of it in a second). It sets how late the sound is read, and a delay
// that moves is a pitch that bends: on kSettle alone 0 to 100% swooped 500 cents down in 0.1 s (a tape-speed
// dive), against the 30 cents it sways at rest. At this speed the move bends at most 1% (17 cents) more.
constexpr float kWowPerSecond = 1.f;
constexpr float kFaint = 1e-15f; // what fades is nothing under this (300 dB down), made so once a tick
enum { kLow, kHigh, kWorn, kShapes };

// A roll: 0 to 1 for a place and what it is for.
inline std::uint32_t mash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline std::uint32_t roll(std::uint32_t at, std::uint32_t what) { return mash(at * 0x9e3779b9u ^ mash(what + 0x51ed270bu)); }
inline float unit(std::uint32_t r) { return static_cast<float>(r >> 8) * (1.f / 16777216.f); }
enum Roll : std::uint32_t { kHissRoll = 1, kCrackleAt, kCrackleSize, kCrackleSide, kDropAt, kDropDepth, kDrift };

inline float onePole(float hz, float rate) { return 1.f - std::exp(-6.2831853f * hz / rate); }

class Wear : public Unit {
public:
    Wear() : Unit(kWearInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        restAfter_ = static_cast<int>(kRestSeconds * sr_);
        dropEvery_ = static_cast<std::uint32_t>(kDropEvery * sr_);
        driftEvery_ = static_cast<std::uint32_t>(kDriftEvery * sr_);
        follow_[0] = onePole(1.f / (6.2831853f * 0.005f), sr_); // the sound's level: 5 ms up,
        follow_[1] = onePole(1.f / (6.2831853f * 0.15f), sr_);  // 150 ms down
        dropGlide_ = onePole(1.f / (6.2831853f * 0.005f), sr_);
        crackleKeep_ = std::exp(-1.f / (0.0003f * sr_));
        hissCut_ = onePole(1000.f, sr_);
        changed();
        reset();
    }
    void reset() override {
        quietIn_ = 0;
        asleep_ = true;
        clear();
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        for (int i = 0; i < frames;) {
            if (asleep_) {
                if (heardIn(left[i]) == 0.f && heardIn(right[i]) == 0.f) {
                    tick_ = (tick_ + 1) % kTick;
                    ++i;
                    continue;
                }
                wake();
            }
            // A stretch up to the tick's end, worked on with its numbers held in locals.
            if (tick_ == 0) beginTick();
            const int count = frames - i < kTick - tick_ ? frames - i : kTick - tick_;
            stretch(left + i, right + i, count);
            i += count;
            tick_ += count;
            if (tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        wow_.to = p_[0];
        dirt_.to = p_[1];
        drops_.to = p_[2];
        tone_Set_.first.to = p_[3];
    }

private:
    // `count` samples, none of them a tick's first or last but at its edges: what the samples change is held in
    // locals (the compiler cannot keep the effect's own numbers in registers across the writes to the block).
    void stretch(float* left, float* right, int count) {
        float* const io[2] = {left, right};
        float wow = wow_.now, dirt = dirt_.now, drops = drops_.now;
        const float wowStep = wow_.step, dirtStep = dirt_.step, dropsStep = drops_.step;
        float sh[kShapes], shStep[kShapes];
        for (int k = 0; k < kShapes; ++k) sh[k] = shape_.now[k], shStep[k] = shape_.moving ? shape_.step[k] : 0.f;
        float tone[2][3] = {{tone_[0][0], tone_[0][1], tone_[0][2]}, {tone_[1][0], tone_[1][1], tone_[1][2]}};
        float hissLow[2] = {hissLow_[0], hissLow_[1]}, crackle[2] = {crackle_[0], crackle_[1]};
        float level = level_, drop = drop_, dropTo = dropTo_, wowAt = wowAt_, flutterAt = flutterAt_, driftA = driftA_, driftB = driftB_;
        std::uint32_t n = n_, driftIn = driftIn_, dropIn = dropIn_, driftK = driftK_;
        int at = at_, quietIn = quietIn_;
        const float wowTurn = kWowHz / sr_, flutterTurn = kFlutterHz / sr_, crackleChance = kCrackles / sr_;
        for (int i = 0; i < count; ++i) {
            const float x[2] = {heardIn(left[i]), heardIn(right[i])};
            const bool none = x[0] == 0.f && x[1] == 0.f;
            wow += wowStep, dirt += dirtStep, drops += dropsStep;
            for (int k = 0; k < kShapes; ++k) sh[k] += shStep[k];
            // The sway: how late the sound is read, in samples.
            line_[0][at] = x[0], line_[1][at] = x[1];
            float y[2] = {x[0], x[1]};
            if (wow != 0.f) {
                const float t = static_cast<float>(driftIn) / static_cast<float>(driftEvery_), ease = t * t * (3.f - 2.f * t);
                const float drift = driftA + ease * (driftB - driftA);
                const float late = wow * sr_ * (kWowSeconds * 0.5f * (1.f - cosTurn(wowAt)) + kFlutterSeconds * 0.5f * (1.f - cosTurn(flutterAt)) + kDriftSeconds * drift);
                read(late, at, y);
            }
            if (++driftIn == driftEvery_) driftIn = 0, ++driftK, driftA = driftB, driftB = unit(roll(driftK + 1u, kDrift));
            wowAt += wowTurn, flutterAt += flutterTurn;
            if (wowAt >= 1.f) wowAt -= 1.f;
            if (flutterAt >= 1.f) flutterAt -= 1.f;
            at = (at + 1) & (kLine - 1);
            // Hiss and crackle, on the sound's level.
            const float loud = std::fabs(x[0]) > std::fabs(x[1]) ? std::fabs(x[0]) : std::fabs(x[1]);
            level += (loud > level ? follow_[0] : follow_[1]) * (loud - level);
            if (dirt != 0.f) {
                const float d2 = dirt * dirt, amount = d2 * level;
                const std::uint32_t both = roll(n, kHissRoll); // left from its top half, right from its bottom
                for (int c = 0; c < 2; ++c) {
                    const float white = static_cast<float>(c ? (both & 0xffffu) : (both >> 16)) * (2.f / 65535.f) - 1.f;
                    hissLow[c] += hissCut_ * (white - hissLow[c]);
                    y[c] += kHiss * amount * (white - hissLow[c]);
                }
                if (unit(roll(n, kCrackleAt)) < d2 * crackleChance) {
                    const std::uint32_t size = roll(n, kCrackleSize);
                    const float pop = kCrackle * amount * (0.3f + 0.7f * unit(size)) * ((size & 1u) ? 1.f : -1.f);
                    const std::uint32_t side = roll(n, kCrackleSide) % 3u; // left, right or both
                    if (side != 1u) crackle[0] += pop;
                    if (side != 0u) crackle[1] += pop;
                }
            }
            y[0] += crackle[0], y[1] += crackle[1];
            crackle[0] *= crackleKeep_, crackle[1] *= crackleKeep_;
            // A dropout: rolled once a stretch of kDropEvery.
            if (dropIn == 0u) {
                const std::uint32_t piece = n / dropEvery_;
                dropTo = unit(roll(piece, kDropAt)) < kDropMost * drops * drops ? std::exp(-(6.f + 18.f * unit(roll(piece, kDropDepth))) / 8.6858896f) : 1.f;
            }
            if (++dropIn == dropEvery_) dropIn = 0u;
            if (drop != dropTo) {
                drop += dropGlide_ * (dropTo - drop);
                if (std::fabs(drop - dropTo) < 1e-6f) drop = dropTo;
            }
            if (drop != 1.f) y[0] *= drop, y[1] *= drop;
            // How worn: a low-pass twice and a low cut, laid over the sound by kWorn (0 at TONE 0: untouched).
            for (int c = 0; c < 2; ++c) {
                tone[c][0] += sh[kHigh] * (y[c] - tone[c][0]);
                tone[c][1] += sh[kHigh] * (tone[c][0] - tone[c][1]);
                tone[c][2] += sh[kLow] * (tone[c][1] - tone[c][2]);
                if (sh[kWorn] != 0.f) y[c] += sh[kWorn] * (tone[c][1] - tone[c][2] - y[c]);
                if (y[c] != x[c]) io[c][i] = y[c]; // (untouched, a -0 stays a -0)
            }
            ++n;
            quietIn = none ? quietIn + 1 : 0;
        }
        wow_.now = wow, dirt_.now = dirt, drops_.now = drops;
        for (int k = 0; k < kShapes; ++k) shape_.now[k] = sh[k];
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < 3; ++k) tone_[c][k] = tone[c][k];
            hissLow_[c] = hissLow[c], crackle_[c] = crackle[c];
        }
        level_ = level, drop_ = drop, dropTo_ = dropTo, wowAt_ = wowAt, flutterAt_ = flutterAt, driftA_ = driftA, driftB_ = driftB;
        n_ = n, driftIn_ = driftIn, dropIn_ = dropIn, driftK_ = driftK, at_ = at, quietIn_ = quietIn;
    }
    // cos(2 pi t) for t in 0..1, to about 1e-5: a sine's polynomial over a triangle (RING's), a quarter turn on.
    static float cosTurn(float t) {
        t += 0.25f;
        if (t >= 1.f) t -= 1.f;
        const float u = t < 0.25f ? 4.f * t : (t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f), u2 = u * u;
        return u * (1.5707963f + u2 * (-0.64596410f + u2 * (0.079692626f + u2 * (-0.0046817541f + u2 * 0.00016044118f))));
    }
    // The sound `late` samples ago, between samples (a cubic through four); at under one sample the four are
    // this one and the three before it, so 0 is this sample to the bit.
    void read(float late, int at, float* y) const {
        int whole = static_cast<int>(late);
        float f = late - static_cast<float>(whole);
        float w[4];
        if (whole == 0) {
            const float t = f;
            w[0] = (t - 1.f) * (t - 2.f) * (t - 3.f) * (-1.f / 6.f);
            w[1] = t * (t - 2.f) * (t - 3.f) * 0.5f;
            w[2] = t * (t - 1.f) * (t - 3.f) * -0.5f;
            w[3] = t * (t - 1.f) * (t - 2.f) * (1.f / 6.f);
            whole = 1; // the newest of the four is this sample
        } else {
            cubicWeights(f, w);
        }
        for (int c = 0; c < 2; ++c) {
            const float* l = line_[c];
            const int a = (at - (whole - 1)) & (kLine - 1);
            y[c] = w[0] * l[a] + w[1] * l[(a - 1) & (kLine - 1)] + w[2] * l[(a - 2) & (kLine - 1)] + w[3] * l[(a - 3) & (kLine - 1)];
        }
    }
    void beginTick() {
        const float k = glideK(kTick, kSettle, sr_);
        wow_.begin(kTick, k, kWowPerSecond * static_cast<float>(kTick) / sr_);
        dirt_.begin(kTick, k), drops_.begin(kTick, k);
        if (!tone_Set_.moving()) return;
        tone_Set_.tick(k);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        wow_.done(), dirt_.done(), drops_.done();
        shape_.land();
        // What fades is made an exact zero here, on the tick's sample: under kFaint, 16 samples of the fastest
        // fade (a 20 kHz one-pole at 44.1 kHz keeps 0.058 a sample) is still a number a float holds.
        if (level_ < kFaint) level_ = 0.f;
        for (int c = 0; c < 2; ++c) {
            if (std::fabs(crackle_[c]) < kFaint) crackle_[c] = 0.f;
            if (std::fabs(hissLow_[c]) < kFaint) hissLow_[c] = 0.f;
            for (float& t : tone_[c])
                if (std::fabs(t) < kFaint) t = 0.f;
        }
        if (quietIn_ >= restAfter_) { // nothing has come in for kRestSeconds: what is left is far under kQuiet
            clear();
            asleep_ = true;
        }
    }
    void shape() {
        const float t = clamp01(tone_Set_.now());
        shape_.to[kHigh] = onePole(20000.f * std::pow(2500.f / 20000.f, t), sr_);
        shape_.to[kLow] = onePole(150.f * t, sr_);
        shape_.to[kWorn] = t * 10.f < 1.f ? t * 10.f : 1.f;
    }
    void clear() {
        for (auto& side : line_)
            for (float& v : side) v = 0.f;
        for (auto& side : tone_)
            for (float& v : side) v = 0.f;
        hissLow_[0] = hissLow_[1] = crackle_[0] = crackle_[1] = 0.f;
        level_ = 0.f, drop_ = dropTo_ = 1.f;
        wowAt_ = flutterAt_ = 0.f;
        n_ = 0u, at_ = 0;
        driftIn_ = dropIn_ = driftK_ = 0u;
        driftA_ = unit(roll(0u, kDrift)), driftB_ = unit(roll(1u, kDrift));
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        wow_.snap(), dirt_.snap(), drops_.snap(), tone_Set_.snap();
        shape();
        shape_.snap();
    }

    float line_[2][kLine] = {};
    float tone_[2][3] = {}, hissLow_[2] = {}, crackle_[2] = {};
    float level_ = 0.f, drop_ = 1.f, dropTo_ = 1.f, wowAt_ = 0.f, flutterAt_ = 0.f;
    float follow_[2] = {}, dropGlide_ = 0.f, crackleKeep_ = 0.f, hissCut_ = 0.f;
    std::uint32_t n_ = 0u, dropEvery_ = 1920u, driftEvery_ = 33600u;
    std::uint32_t driftIn_ = 0u, dropIn_ = 0u, driftK_ = 0u; // where in its stretch the drift and the dropouts are, and which drift
    float driftA_ = 0.f, driftB_ = 0.f;
    int at_ = 0, tick_ = 0, quietIn_ = 0, restAfter_ = 1;
    Slide<kShapes> shape_;
    Glide wow_, dirt_, drops_;
    Gentle tone_Set_;
    bool asleep_ = true;
};

void reading(int, float v, const float*, char* out, int size) { readPercent(v, out, size); }

Effect* create() { return new Wear; }

} // namespace

const EffectInfo kWearInfo = {
    "WEAR", 4, {"WOW", "DIRT", "DROPS", "TONE"}, {0.3f, 0.25f, 0.2f, 0.4f},
    "wear", EffectFamily::Crush, {0, 0, 0, 0}, reading, create, "old tape, worn records",
};

} // namespace fx
} // namespace apotheca
