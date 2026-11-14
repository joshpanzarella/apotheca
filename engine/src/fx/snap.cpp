// SNAP: a transient shaper, a TYPE of LEVEL. It does not listen for how LOUD
// the sound is, as COMP does, but for how it is MOVING: the moment a hit
// starts (its attack) and the stretch where it dies away (its sustain), and
// turns each up or down by itself. A snare made to crack, or to thud; a kick's
// boom made longer, or cut short to a click; a room on a drum loop taken out.
//
//   ATTACK   -100 to +100%: down, the start of each hit softened (a
//            brush instead of a stick); up, it cracks. At +100% the first
//            millisecond of a hit from silence is about 14 dB up, 5 ms in
//            about 7, gone by 30 ms (testSnap has the numbers)
//   SUSTAIN  -100 to +100%: down, each hit's tail cut short (dry, tight);
//            up, the tail held up (big, roomy). At +100% a drum's tail
//            100 to 250 ms in is about 6 dB up; -100%, 6 dB down
//   MIX      0 is the sound untouched, to the sample; COMP's MIX, carried
//
// How it works. Two level followers listen to the louder of left and right
// (both sides are turned together, so the picture does not lean): a QUICK one
// (1 ms) and a SLOW one (25 ms), both following the same peak reading. On a
// steady sound they agree, and SNAP does nothing to it. When a hit starts the
// quick one is ahead: that is the attack, and how far ahead (in dB) times
// ATTACK is the turn (half of it, at 100%). As it dies away the quick one falls first: that is the
// sustain, turned by SUSTAIN (all of it, at 100%). The turn is held between -24 and +12 dB.
// Nothing is late: no lookahead, so MIX can lay it under the plain sound.
//
// Every setting glides on the TICK (svf.hpp), so the sound is the same to the
// bit however it is cut into blocks. It sleeps (exact zeros, no work) once
// nothing has come in for a tick and both followers are under kQuiet; woken,
// it is a fresh one.

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kPeakRelease = 0.02f;  // seconds: the peak reading both followers listen to lets go this fast
constexpr float kQuickSeconds = 0.001f;
constexpr float kSlowSeconds = 0.025f;
constexpr float kAttackShare = 0.5f;   // of how far the quick follower is ahead, in dB, at ATTACK 100%
constexpr float kSustainShare = 1.f;   // of how far it is behind, at SUSTAIN 100%
constexpr float kMostUp = 12.f, kMostDown = 24.f; // dB
constexpr float kFloor = 1e-6f;        // under -120 dB there is no attack or sustain to speak of (a hit 40 dB down is shaped as a loud one)
constexpr float kSettle = 0.02f;
constexpr float kDbPerLog = 8.6858896f;

float signedOf(float v) { return 2.f * clamp01(v) - 1.f; }

class Snap : public Unit {
public:
    Snap() : Unit(kSnapInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        peakKeep_ = std::exp(-1.f / (kPeakRelease * sr_));
        quick_ = 1.f - std::exp(-1.f / (kQuickSeconds * sr_));
        slow_ = 1.f - std::exp(-1.f / (kSlowSeconds * sr_));
        changed();
        reset();
    }
    void reset() override {
        peak_ = fast_ = lag_ = 0.f;
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
            const float attack = attack_.next(), sustain = sustain_.next(), mix = mix_.next();
            const float level = std::fmax(std::fabs(x[0]), std::fabs(x[1]));
            peak_ = level > peak_ ? level : peak_ * peakKeep_;
            fast_ += quick_ * (peak_ - fast_);
            lag_ += slow_ * (peak_ - lag_);
            if (peak_ < kTiny) peak_ = 0.f;
            if (fast_ < kTiny) fast_ = 0.f;
            if (lag_ < kTiny) lag_ = 0.f;
            float g = 1.f;
            if (attack != 0.f || sustain != 0.f) {
                const float ahead = kDbPerLog * std::log((fast_ + kFloor) / (lag_ + kFloor)); // dB: + a hit starting, - dying away
                float db = ahead > 0.f ? attack * kAttackShare * ahead : -sustain * kSustainShare * ahead;
                db = db > kMostUp ? kMostUp : (db < -kMostDown ? -kMostDown : db);
                g = std::exp(db / kDbPerLog);
            }
            if (mix != 0.f) {
                left[i] = x[0] + mix * (x[0] * g - x[0]);
                right[i] = x[1] + mix * (x[1] * g - x[1]);
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        attack_.to = signedOf(p_[0]);
        sustain_.to = signedOf(p_[1]);
        mix_.to = p_[2];
    }

private:
    void beginTick() {
        const float k = glideK(kTick, kSettle, sr_);
        attack_.begin(kTick, k);
        sustain_.begin(kTick, k);
        mix_.begin(kTick, k);
    }
    void endTick() {
        tick_ = 0;
        attack_.done(), sustain_.done(), mix_.done();
        if (quietIn_ >= kTick && peak_ < kQuiet && fast_ < kQuiet && lag_ < kQuiet) { // nothing came in all this tick, and nothing is left
            peak_ = fast_ = lag_ = 0.f;
            asleep_ = true;
        }
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() { attack_.snap(), sustain_.snap(), mix_.snap(); }

    float peak_ = 0.f, fast_ = 0.f, lag_ = 0.f;
    float peakKeep_ = 0.f, quick_ = 0.f, slow_ = 0.f;
    Glide attack_, sustain_, mix_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index <= 1) {
        const int at = static_cast<int>(std::round(signedOf(v) * 100.f));
        if (at == 0) std::snprintf(out, static_cast<std::size_t>(size), "0%%");
        else std::snprintf(out, static_cast<std::size_t>(size), "%+d%%", at);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Snap; }

} // namespace

const EffectInfo kSnapInfo = {
    "SNAP", 3, {"ATTACK", "SUSTAIN", "MIX", ""}, {0.75f, 0.5f, 1.f, 0.f}, // +50%, 0%, all of it
    "snap", EffectFamily::Level, {0, 0, 0, 0}, reading, create, "more or less attack",
};

} // namespace fx
} // namespace apotheca
