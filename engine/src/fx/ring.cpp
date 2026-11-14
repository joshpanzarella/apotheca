// RING: a ring modulator, a TYPE of CRUSH. The sound is multiplied by a
// steady wave (the carrier): every note comes out as two, one the carrier's
// pitch above it and one below, and the note itself is gone. Not in tune with
// anything, so it sounds metallic, bell-like or robotic; slow, it is a
// tremolo that flips the wave over.
//
//   FREQ    the carrier's pitch: 1 Hz to 4 kHz (each D-pad push about a
//           fifth). Under 20 Hz it is heard as a beat (a choppy tremolo);
//           50 to 500 Hz, a robot or a bell; above, a harsh shimmer. It does
//           not follow the notes played: an effect is not told them.
//   WAVE    SINE (the clean ring: the two new notes only), TRIANGLE (a
//           little more: faint ones further out), SQUARE (much more: a buzz,
//           every odd step further out as well; at slow FREQ, the sound
//           flipping over at once).
//   SPREAD  how far the right side's carrier is behind the left's, up to a
//           quarter of a turn: at slow FREQ the sound swings from side to
//           side; faster, it is wider.
//   MIX     0 is the sound untouched, to the sample. Named as CRUSH's MIX.
//
// The carrier starts at its top (a quarter turn in), so the first moment of a
// drum hit comes through whole rather than being swallowed, and it starts
// there again whenever it wakes (a sleeping effect that wakes starts as a
// fresh one): every hit after four seconds of silence (kRestSeconds) sounds
// the same. Hits closer together than that meet the carrier wherever it has
// got to, as they would with any sound between them. The square's two jumps
// are smoothed over a sample (polyBLEP), so a fast square does not fold back
// as notes that were never there; the triangle is left plain (its overtones
// fall fast; effects_test, testRing, has how far down what folds back is).
// A change of WAVE fades from the one to the other over 10 ms, one fade at a
// time, the last asked for winning (as SATURATE's TYPE). FREQ and SPREAD set
// off gently (svf.hpp's Gentle) and slide sample by sample on the TICK, so the
// sound does not depend on how it is cut into blocks. It remembers nothing
// but where the carrier is: with nothing coming in for kRestSeconds it is
// asleep (what comes out is what came in; awake, silence in is silence out
// all the same).

#include <initializer_list>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kWaves = 3;
const char* const kWaveNames[kWaves] = {"SINE", "TRIANGLE", "SQUARE"};
constexpr float kStart = 0.25f;     // where the carrier starts: its top
constexpr float kMostSpread = 0.25f; // of a turn
constexpr float kWaveFade = 0.01f;  // seconds
constexpr float kSettle = 0.012f, kMixSettle = 0.02f;
// How long nothing must come in before it sleeps (and so before the carrier starts again at its top): 4 s,
// as long as a bounce waits for quiet. It slept after one tick of exact zeros at first (a third of a
// millisecond), so a drum hit after the digital silence between two hits always met the carrier at its top:
// a slow FREQ never moved on from hit to hit, and SPREAD never swung, while the same hits with sound far too
// faint to hear between them did both.
constexpr float kRestSeconds = 4.f;
enum { kStepOf, kBehind, kShapes };

float hzOf(float v) { return std::pow(4000.f, clamp01(v)); }
int waveOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kWaves);
    return i > kWaves - 1 ? kWaves - 1 : i;
}
// The triangle for a place in the turn (0 to 1): 0 at 0, 1 at a quarter, -1 at three quarters.
inline float triangleAt(float at) { return at < 0.25f ? 4.f * at : (at < 0.75f ? 2.f - 4.f * at : 4.f * at - 4.f); }
// sin(u pi / 2) for u in -1..1 (SATURATE's polynomial).
inline float sineOfTriangle(float u) {
    const float u2 = u * u;
    return u * (1.5707963f + u2 * (-0.64596410f + u2 * (0.079692626f + u2 * (-0.0046817541f + u2 * 0.00016044118f))));
}
// A jump smoothed over the samples either side of it: t is how far past it, dt a sample's share of the turn.
inline float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}
inline float carrier(int wave, float at, float dt) {
    if (wave == 0) return sineOfTriangle(triangleAt(at));
    if (wave == 1) return triangleAt(at);
    // How far past the jump down at a half. Not wrapped by taking 1 off: 0.49999997 + 0.5 is 1 in a float,
    // and taken round to 0 it read as just past the jump, not just before it (a sample of 2). At 1 blep()
    // reads it as just before, as it should.
    const float half = at < 0.5f ? at + 0.5f : at - 0.5f;
    return (at < 0.5f ? 1.f : -1.f) + blep(at, dt) - blep(half, dt);
}

class Ring : public Unit {
public:
    Ring() : Unit(kRingInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        restAfter_ = static_cast<int>(kRestSeconds * sr_);
        changed();
        reset();
    }
    void reset() override {
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {heardIn(left[i]), heardIn(right[i])};
            const bool none = x[0] == 0.f && x[1] == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            const float mix = mix_.next();
            shape_.next();
            const float dt = shape_.now[kStepOf];
            float behind = at_ - shape_.now[kBehind]; // where the right carrier is
            if (behind < 0.f) behind += 1.f;
            float c[2] = {carrier(wave_, at_, dt), carrier(wave_, behind, dt)};
            if (fade_ > 0) {
                const float old = static_cast<float>(--fade_) / static_cast<float>(fadeLength_);
                c[0] += old * (carrier(oldWave_, at_, dt) - c[0]);
                c[1] += old * (carrier(oldWave_, behind, dt) - c[1]);
            }
            if (mix != 0.f) {
                left[i] += mix * (x[0] * c[0] - x[0]);
                right[i] += mix * (x[1] * c[1] - x[1]);
            }
            at_ += dt;
            if (at_ >= 1.f) at_ -= 1.f;
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        freq_.first.to = p_[0];
        wanted_ = waveOf(p_[1]);
        spread_.first.to = p_[2];
        mix_.to = p_[3];
    }

private:
    void beginTick() {
        mix_.begin(kTick, glideK(kTick, kMixSettle, sr_));
        if (wanted_ != wave_ && fade_ == 0) {
            // Another wave: the old one fades out under the new. Only once the last fade is over (a change
            // asked for meanwhile waits here, and only the last is gone to).
            oldWave_ = wave_;
            wave_ = wanted_;
            fadeLength_ = fade_ = static_cast<int>(kWaveFade * sr_) > 0 ? static_cast<int>(kWaveFade * sr_) : 1;
        }
        if (!(freq_.moving() || spread_.moving())) return;
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&freq_, &spread_}) s->tick(g);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        shape_.land();
        if (quietIn_ >= restAfter_) asleep_ = true; // nothing has come in for kRestSeconds: it holds nothing else
    }
    void shape() {
        shape_.to[kStepOf] = hzOf(freq_.now()) / sr_;
        shape_.to[kBehind] = kMostSpread * clamp01(spread_.now());
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        freq_.snap(), spread_.snap(), mix_.snap();
        wave_ = wanted_;
        fade_ = 0;
        at_ = kStart;
        shape();
        shape_.snap();
    }

    Slide<kShapes> shape_;
    Gentle freq_, spread_;
    Glide mix_;
    float at_ = kStart; // where the left carrier is in its turn, 0 to 1
    int wave_ = 0, wanted_ = 0, oldWave_ = 0, fade_ = 0, fadeLength_ = 1;
    int tick_ = 0, quietIn_ = 0, restAfter_ = 1;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const float hz = hzOf(v);
        if (hz < 9.95f) std::snprintf(out, n, "%.1f HZ", static_cast<double>(hz));
        else if (hz < 999.5f) std::snprintf(out, n, "%d HZ", static_cast<int>(std::round(hz)));
        else std::snprintf(out, n, "%.1f KHZ", static_cast<double>(hz) / 1000.0);
    } else if (index == 1) {
        std::snprintf(out, n, "%s", kWaveNames[waveOf(v)]);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Ring; }

} // namespace

const EffectInfo kRingInfo = {
    "RING", 4, {"FREQ", "WAVE", "SPREAD", "MIX"}, {0.7f, 0.f, 0.f, 1.f}, // 333 Hz, SINE, both sides alike, all of it
    "ring", EffectFamily::Crush, {0, kWaves, 0, 0}, reading, create, "metal, bells, robots",
};

float ringCarrier(int wave, float at, float dt) { return carrier(wave, at, dt); }

} // namespace fx
} // namespace apotheca
