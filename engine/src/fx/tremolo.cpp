// TREMOLO: a TYPE of MOD. The sound's level moved up and down in time with
// the song; with SPREAD, the two sides out of step, so the sound moves from
// side to side (auto-pan).
//
//   RATE    one turn of the wave is a note length, from 1/32 to a whole bar
//           (ten choices, triplets and a dotted eighth among them); it
//           follows the tempo
//   DEPTH   how far down it goes: 0 is the sound untouched, to the bit; 100%
//           goes to silence at the wave's bottom
//   SHAPE   SINE (a smooth swell), SQUARE (chopped: on for half a turn, off
//           for half), RAMP (a pump: down at the start of each turn and
//           swelling back, fast and then easing: the sidechain sound with no
//           kick), RANDOM (a new level each turn, by chance). Every jump in
//           SQUARE, RAMP and RANDOM is smoothed (two stages of 0.6 ms: 99% of
//           the way in 4 ms), so none clicks; SINE is left exactly a sine.
//   SPREAD  how far the right side's wave is behind the left's, up to half a
//           turn: at 100% one side is up while the other is down (auto-pan)
//
// No MIX: DEPTH is how much of it there is (as EQ and WEAR have none).
//
// IN TIME, NOT ON THE BEAT. An effect is told the tempo, not where the bar
// is, so the wave's speed is the song's but where it starts is not: it starts
// at its top (RAMP: at its bottom) when sound arrives after 4 s of nothing
// (mod.hpp's rest rule), and runs on from there. A drum hit on the first
// beat after silence meets it at its start.
//
// The wave is worked out every sample from where it is in its turn; how fast
// it goes glides (a change of RATE or of tempo is a change of speed, never a
// jump in the wave), and DEPTH and SPREAD are worked out on the TICK and slid
// to sample by sample. A change of SHAPE fades from the old wave to the new
// over 10 ms, one fade at a time, the last asked for winning (RING's WAVE).
// RANDOM's levels are rolls by the count of turns since it woke, never a
// generator walked.

#include <initializer_list>

#include "mod.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kRates = 10;
// A turn in steps (a step is a sixteenth).
constexpr double kStepsOf[kRates] = {0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0, 8.0 / 3.0, 3.0, 4.0, 8.0, 16.0};
const char* const kRateNames[kRates] = {"1/32", "1/16 TRIP", "1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/2", "1 BAR"};
constexpr int kShapeCount = 4;
const char* const kShapeNames[kShapeCount] = {"SINE", "SQUARE", "RAMP", "RANDOM"};
constexpr float kEdgeSeconds = 0.0006f; // the smoothing of a jump: two stages of this (99% of the way in 4 ms)
constexpr float kShapeFade = 0.01f;     // seconds
constexpr float kSettle = 0.012f, kSpeedSettle = 0.03f;
enum { kDepth, kBehind, kShapes };

int rateOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kRates);
    return i > kRates - 1 ? kRates - 1 : i;
}
int shapeOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kShapeCount);
    return i > kShapeCount - 1 ? kShapeCount - 1 : i;
}
// RANDOM's level for a turn: the first turn after waking is at the top.
inline float levelOf(long turn) { return turn <= 0 ? 1.f : modRoll(static_cast<std::uint32_t>(turn), 0x7e30u); }

// The wave, 0 (the bottom) to 1 (the top), at `at` of turn number `turn`, before its jumps are smoothed. SINE needs
// no smoothing and is not given any (so it is exactly a sine, in step with the song).
float waveAt(int shape, double at, long turn) {
    const float a = static_cast<float>(at);
    switch (shape) {
    case 0: {
        const float w = 0.5f + 0.5f * turnSine(at + 0.25); // top at the start
        return w < 0.f ? 0.f : (w > 1.f ? 1.f : w);           // (the polynomial is 3e-6 over at its top)
    }
    case 1: return a < 0.5f ? 1.f : 0.f;
    case 2: {
        const float u = 1.f - a;
        return 1.f - u * u; // up from the bottom, fast and then easing: a pump
    }
    default: return levelOf(turn);
    }
}
// A jump smoothed: two one-pole stages, each `k` of the way a sample. Lands exactly (a hair from where it is
// going, it is there), so it never fades through numbers too small to hold.
struct Smooth {
    float a = 0.f, b = 0.f;
    float step(float to, float k) {
        a += k * (to - a);
        if (std::fabs(to - a) < 1e-7f) a = to;
        b += k * (a - b);
        if (std::fabs(a - b) < 1e-7f) b = a;
        return b;
    }
    void at(float v) { a = b = v; }
};

class Tremolo : public Unit {
public:
    Tremolo() : Unit(kTremoloInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        restAfter_ = static_cast<int>(kModRestSeconds * sr_);
        changed();
        reset();
    }
    void reset() override {
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        for (int i = 0; i < frames; ++i) {
            const float xl = heardIn(left[i]), xr = heardIn(right[i]);
            const bool none = xl == 0.f && xr == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            shape_.next();
            const float depth = shape_.now[kDepth];
            double atR = at_ - shape_.now[kBehind];
            long turnR = turns_;
            if (atR < 0.0) atR += 1.0, --turnR;
            float w[2] = {wave(wave_, now_, at_, turns_, 0), wave(wave_, now_, atR, turnR, 1)};
            if (fade_ > 0) {
                const float old = static_cast<float>(--fade_) / static_cast<float>(fadeLength_);
                w[0] += old * (wave(oldWave_, was_, at_, turns_, 0) - w[0]);
                w[1] += old * (wave(oldWave_, was_, atR, turnR, 1) - w[1]);
            }
            if (depth != 0.f) {
                left[i] -= depth * (1.f - w[0]) * xl;
                right[i] -= depth * (1.f - w[1]) * xr;
            }
            at_ += speed_.now;
            if (at_ >= 1.0) at_ -= 1.0, ++turns_;
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        speedTo_ = static_cast<double>(tempo_) / (15.0 * kStepsOf[rateOf(p_[0])]) / sr_; // turns a sample
        depth_.first.to = p_[1];
        wanted_ = shapeOf(p_[2]);
        spread_.first.to = p_[3];
    }

private:
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&depth_, &spread_})
            if (s->moving()) s->tick(g);
        speed_.to = static_cast<float>(speedTo_);
        if (speed_.moving()) speed_.begin(kTick, glideK(kTick, kSpeedSettle, sr_)), speed_.done();
        if (wanted_ != wave_ && fade_ == 0) {
            // The old wave fades out under the new; the new one's smoothing starts where the sound is now.
            for (int side = 0; side < 2; ++side) {
                was_[side] = now_[side];
                now_[side].at(wave_ == 0 ? lastSine_[side] : was_[side].b);
            }
            oldWave_ = wave_;
            wave_ = wanted_;
            fadeLength_ = fade_ = static_cast<int>(kShapeFade * sr_) > 0 ? static_cast<int>(kShapeFade * sr_) : 1;
        }
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        if (quietIn_ >= restAfter_) asleep_ = true; // nothing has come in for 4 s: it holds nothing but where the wave is
    }
    // A wave as heard: SINE as it is, the rest smoothed (`s` its smoothing, one a side).
    float wave(int shape, Smooth* s, double at, long turn, int side) {
        const float hard = waveAt(shape, at, turn);
        if (shape == 0) return lastSine_[side] = hard;
        return s[side].step(hard, smooth_);
    }
    void shape() {
        shape_.to[kDepth] = clamp01(depth_.now());
        shape_.to[kBehind] = 0.5f * clamp01(spread_.now());
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&depth_, &spread_}) s->snap();
        speed_.to = static_cast<float>(speedTo_);
        speed_.snap();
        smooth_ = 1.f - std::exp(-1.f / (kEdgeSeconds * sr_));
        wave_ = wanted_;
        fade_ = 0;
        at_ = 0.0;
        turns_ = 0;
        for (int side = 0; side < 2; ++side) {
            lastSine_[side] = waveAt(0, 0.0, 0);
            now_[side].at(waveAt(wave_, 0.0, 0));
            was_[side] = now_[side];
        }
        shape();
        shape_.snap();
    }

    Gentle depth_, spread_;
    Glide speed_;          // turns a sample
    double speedTo_ = 0.0;
    Slide<kShapes> shape_;
    double at_ = 0.0;                    // where the left's wave is in its turn, 0 to 1
    Smooth now_[2], was_[2];             // the wave's smoothing, and the old wave's while it fades
    float lastSine_[2] = {1.f, 1.f}, smooth_ = 1.f;
    long turns_ = 0;                     // turns since it woke
    float tempo_ = 120.f;
    int wave_ = 0, wanted_ = 0, oldWave_ = 0, fade_ = 0, fadeLength_ = 1;
    int tick_ = 0, quietIn_ = 0, restAfter_ = 1;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) std::snprintf(out, n, "%s", kRateNames[rateOf(v)]);
    else if (index == 2) std::snprintf(out, n, "%s", kShapeNames[shapeOf(v)]);
    else readPercent(v, out, size);
}

Effect* create() { return new Tremolo; }

} // namespace

const EffectInfo kTremoloInfo = {
    "TREMOLO", 4, {"RATE", "DEPTH", "SHAPE", "SPREAD"}, {0.45f, 0.5f, 0.f, 0.f}, // 1/8, 50%, SINE, both sides alike
    "tremolo", EffectFamily::Mod, {kRates, 0, kShapeCount, 0}, reading, create, "the level pulsing",
};

float tremoloWave(int shape, double at, long turn) { return waveAt(shape, at, turn); }

} // namespace fx
} // namespace apotheca
