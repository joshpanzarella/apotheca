// FOLD: a wavefolder, a TYPE of DRIVE. Past the top the wave is folded back
// down, and pushed harder it folds again and again: a plain sound grows
// bright, hollow, buzzing overtones that move as it gets louder or softer
// (the "west coast" sound of a Buchla or Serge).
//
//   DRIVE     how hard the sound is pushed into the folder: 1.5 to 12 times (a
//             sine at half scale folds once at about 30%, five times at full).
//             Named as SATURATE's DRIVE, which is what pushes SATURATE's FOLD:
//             changing TYPE keeps it.
//   SYMMETRY  -100% to +100%: the folds moved off centre, so the wave folds
//             sooner one way than the other. At 0 only odd overtones (hollow);
//             off 0 the even ones come in too (fuller, more vocal), and a
//             quiet sound comes out thinner. Turning it while a note holds is
//             a sweep, like a filter's.
//   TONE      a low-pass after it: 500 Hz to 20 kHz (SATURATE's, so TYPE keeps it to the bit)
//   MIX       0 is the sound untouched, to the sample.
//
// HOW IT DIFFERS FROM SATURATE'S FOLD. SATURATE's FOLD is a sine of the input
// (sin(x pi / 2)): every turn rounded all the way, so it stays smooth and
// sounds like a soft bell. This is a triangle with only its corners rounded
// (the last 30% of each fold is a curve): straight, steep sides between the
// folds, so at the same push it has far more high overtones (effects_test,
// testFold: harmonics 10 to 40 of a 220 Hz sine, 40 dB and more over
// SATURATE's FOLD at every push). It also goes twice as far (12 times against
// six), and it has SYMMETRY, which SATURATE has not.
//
// Worked out at twice the sample rate (bend.hpp, halfband.hpp), with the
// plain sound made as late to match. The loudness is held level as DRIVE and
// SYMMETRY move, as SATURATE's is (a sine at half scale comes out as loud as
// it went in), and the bias SYMMETRY adds is taken off again, so silence in is
// silence out. Every setting glides on the TICK (svf.hpp: worked out every 16
// samples counted from its start, slid to sample by sample; DRIVE, SYMMETRY
// and TONE set off gently), so the sound does not depend on how it is cut
// into blocks, even while a setting moves. With nothing coming in and nothing
// left in it over kQuiet it is asleep: no work, what comes out is what came in.

#include <initializer_list>

#include "bend.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kCorner = 0.3f;    // how much of the top of each fold is rounded
constexpr float kMostBias = 0.75f; // SYMMETRY at either end: three quarters of the way to the first fold
constexpr float kSettle = 0.012f, kMixSettle = 0.02f;
enum { kPush, kBias, kOffset, kLevel, kTone, kShapes };

// A triangle: x up to 1, back down to -1 at 3, up again... (period 4). Written without a branch to guess:
// a folder pushed hard turns every few samples, and a guessed branch cost it twice SATURATE's time.
inline float triangle(float x) {
    if (!(std::fabs(x) < 4e6f)) return 0.f; // too far round to count the turns (and a cast of it to int would be anything at all)
    const float m = 0.25f * (x + 1.f);                                 // turns, from the trough before 0
    float whole = static_cast<float>(static_cast<int>(m));
    whole -= whole > m ? 1.f : 0.f;                                    // down, for a turn below 0
    return 1.f - std::fabs(4.f * (m - whole) - 2.f);
}
// The triangle with its corners rounded: straight to 1 - kCorner, then a curve that is flat where it turns.
inline float fold(float x) {
    const float u = triangle(x), a = std::fabs(u), over = std::fmax(a - (1.f - kCorner), 0.f);
    return std::copysign(a - over * over * (0.5f / kCorner), u);
}
float pushOf(float v) { return 1.5f * std::pow(8.f, clamp01(v)); }
float biasOf(float v) { return (2.f * clamp01(v) - 1.f) * kMostBias; }

class Fold : public Unit {
public:
    Fold() : Unit(kFoldInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        keep_ = dcKeep(sr_);
        changed();
        reset();
    }
    void reset() override {
        for (BendSide& s : side_) s.reset();
        tone_[0] = tone_[1] = 0.f;
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
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
            const float mix = mix_.next(), late = late_.next();
            shape_.next();
            const float* const n = shape_.now;
            const float push = n[kPush], bias = n[kBias], offset = n[kOffset], level = n[kLevel], coeff = n[kTone];
            const auto bend = [push, bias, offset, level](float v) { return level * (fold(v * push + bias) - offset); };
            for (int c = 0; c < 2; ++c) {
                const float wet = side_[c].twice(x[c], bend, keep_);
                tone_[c] += coeff * (wet - tone_[c]);
                const float plain = side_[c].plain(x[c]);
                const float dry = x[c] + late * (plain - x[c]); // the plain sound, as late as the bent one once MIX has left 0
                if (mix != 0.f || late != 0.f) io[c][i] += (dry - x[c]) + mix * (tone_[c] - dry);
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        drive_.first.to = p_[0];
        symmetry_.first.to = p_[1];
        toneSet_.first.to = p_[2];
        mix_.to = p_[3];
        late_.to = p_[3] > 0.f ? 1.f : 0.f; // at MIX 0 the plain sound is as it came in
    }

private:
    void beginTick() {
        const float k = glideK(kTick, kMixSettle, sr_);
        mix_.begin(kTick, k);
        late_.begin(kTick, k);
        if (!(drive_.moving() || symmetry_.moving() || toneSet_.moving())) return;
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&drive_, &symmetry_, &toneSet_}) s->tick(g);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        late_.done();
        shape_.land();
        float loudest = 0.f;
        for (int c = 0; c < 2; ++c) {
            side_[c].settle();
            if (std::fabs(tone_[c]) < kTiny) tone_[c] = 0.f;
            loudest = std::fmax(loudest, std::fmax(side_[c].loudest(), std::fabs(tone_[c])));
        }
        if (quietIn_ >= kTick && loudest < kQuiet) { // nothing came in all this tick, and nothing is left in it
            for (BendSide& s : side_) s.reset();
            tone_[0] = tone_[1] = 0.f;
            asleep_ = true;
        }
    }
    // Where the folder is: how hard, how far off centre, what silence comes out as, how loud; and the tone.
    void shape() {
        const float push = pushOf(drive_.now()), bias = biasOf(symmetry_.now()), offset = fold(bias);
        float* const to = shape_.to;
        to[kPush] = push, to[kBias] = bias, to[kOffset] = offset;
        to[kLevel] = bendLevel([push, bias, offset](float v) { return fold(v * push + bias) - offset; });
        to[kTone] = onePole(500.f * std::pow(40.f, clamp01(toneSet_.now())), sr_);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        drive_.snap(), symmetry_.snap(), toneSet_.snap(), mix_.snap(), late_.snap();
        shape();
        shape_.snap();
    }

    BendSide side_[2];
    float tone_[2] = {}, keep_ = 1.f;
    Slide<kShapes> shape_;
    Gentle drive_, symmetry_, toneSet_;
    Glide mix_, late_; // late_: 0 the plain sound as it came in (MIX 0), 1 made late to match the bent one
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 1) {
        const int at = static_cast<int>(std::round((2.f * clamp01(v) - 1.f) * 100.f));
        if (at == 0) std::snprintf(out, static_cast<std::size_t>(size), "0%%");
        else std::snprintf(out, static_cast<std::size_t>(size), "%+d%%", at);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Fold; }

} // namespace

const EffectInfo kFoldInfo = {
    "FOLD", 4, {"DRIVE", "SYMMETRY", "TONE", "MIX"}, {0.35f, 0.5f, 0.8f, 1.f},
    "fold", EffectFamily::Drive, {0, 0, 0, 0}, reading, create, "folds the wave over",
};

float foldCurve(float x) { return fold(x); }

} // namespace fx
} // namespace apotheca
