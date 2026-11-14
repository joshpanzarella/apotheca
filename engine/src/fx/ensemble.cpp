// ENSEMBLE: a TYPE of MOD. The string machine's chorus (the Solina's): three
// copies of the sound, each a little late, swung by two waves at once (a slow
// swirl and a fast shimmer) a third of a turn apart, so that at every moment
// one copy is sharp, one flat and one between. Lusher and steadier than
// CHORUS: the three always pull against each other, and the swirl never
// stops on one pitch.
//
//   RATE    the swirl: 0.1 to 8 Hz, CHORUS's scale (carried to the bit). The
//           shimmer runs at 9.7 times it, never over 10 Hz (0.63 Hz and
//           6.1 Hz as it comes). Each swings a little less as it gets faster,
//           so the pitch bends by about the same at any RATE.
//   DEPTH   how far both swing: at 100% the swirl 1.8 ms each way, the
//           shimmer 0.14 ms (about 12 and 9 cents of bend)
//   SPREAD  how wide: the right side's three copies swing the other way from
//           the left's (half a turn on at 100%); at 0 the two sides alike
//   MIX     0 is the sound untouched (CHORUS's law: the plain sound and the
//           copies as loud together at any MIX)
//
// HOW IT DIFFERS FROM CHORUS's ENSEMBLE (three copies at 7, 10.5 and 14 ms,
// each on ONE wave at its own speed, the right a quarter turn behind): here
// the three copies share one place (9 ms) and are spread by their waves
// alone, evenly, a third of a turn apart, so their average pitch is always
// the sound's own; each rides TWO waves, the slow swirl and the fast
// shimmer, which is what makes a string machine sound like a section, not a
// pair of players; the copies are darkened (a high cut at 7 kHz, as the
// bucket-brigade chips did), so the top stays clean; and SPREAD sets the
// width.
//
// Its cost is CHORUS's (six reads between samples a sample, 0.16% of a desk
// core); worked on locals it was no quicker (0.17%), so it is left plain.
//
// Every setting and where the waves are is worked out on the TICK (svf.hpp)
// and slid to sample by sample. The line holds the sound only (no feedback):
// asleep (mod.hpp's rest rule) once nothing has come in for 4 s, the line is
// zeros and the high cuts are at rest.

#include <initializer_list>
#include <vector>

#include "bend.hpp"
#include "mod.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kCopies = 3;
constexpr float kCentreMs = 9.f;
constexpr float kSwirlMs = 1.8f, kShimmerMs = 0.14f; // each way, at DEPTH 100%
constexpr float kShimmerTimes = 9.7f, kShimmerMost = 10.f;
constexpr float kSwirlFullBelow = 1.f, kShimmerFullBelow = 6.f; // Hz: above these each swings less, as 1 / rate
constexpr float kToneHz = 7000.f;
constexpr float kWeight = 1.f / 3.f; // each copy: three alike (low notes, where the swing is a sliver of a wave) add to the sound's own size
constexpr float kSettle = 0.012f; // seconds, each of Gentle's two
// Six delays (three a side), then the plain sound's and the copies' weights.
enum { kDry = 2 * kCopies, kWet, kShapes };

class Ensemble : public Unit {
public:
    Ensemble() : Unit(kEnsembleInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        size_ = lineSize((kCentreMs + kSwirlMs + kShimmerMs) * 0.001f * sampleRate + 4.f);
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        restAfter_ = static_cast<int>(kModRestSeconds * sr_);
        tone_ = onePole(kToneHz, sr_);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        at_ = 0;
        quietIn_ = 0;
        quietLine_ = size_;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        soundOnly(left, right, frames);
        float* const lineL = line_[0].data();
        float* const lineR = line_[1].data();
        const int mask = size_ - 1;
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
            float wl = 0.f, wr = 0.f;
            for (int j = 0; j < kCopies; ++j) {
                wl += lineRead(lineL, mask, at_, shape_.now[j]);
                wr += lineRead(lineR, mask, at_, shape_.now[kCopies + j]);
            }
            lineL[at_] = xl, lineR[at_] = xr;
            at_ = (at_ + 1) & mask;
            quietLine_ = none ? (quietLine_ < size_ ? quietLine_ + 1 : size_) : 0;
            cut_[0] += tone_ * (kWeight * wl - cut_[0]);
            cut_[1] += tone_ * (kWeight * wr - cut_[1]);
            const float dry = shape_.now[kDry], wet = shape_.now[kWet];
            if (dry != 1.f || wet != 0.f) {
                left[i] += (dry - 1.f) * xl + wet * cut_[0];
                right[i] += (dry - 1.f) * xr + wet * cut_[1];
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        rate_.first.to = p_[0];
        depth_.first.to = p_[1];
        spread_.first.to = p_[2];
        mix_.first.to = p_[3];
    }

private:
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&rate_, &depth_, &spread_, &mix_})
            if (s->moving()) s->tick(g);
        const float hz = modRateHz(rate_.now());
        swirl_ = wrapTurn(swirl_ + kTick * static_cast<double>(hz) / sr_);
        shimmer_ = wrapTurn(shimmer_ + kTick * static_cast<double>(shimmerHz(hz)) / sr_);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        for (float& c : cut_)
            if (std::fabs(c) < kTiny) c = 0.f;
        if (quietIn_ >= restAfter_ && quietLine_ >= size_ && cut_[0] == 0.f && cut_[1] == 0.f) asleep_ = true;
    }
    static float shimmerHz(float hz) { return hz * kShimmerTimes < kShimmerMost ? hz * kShimmerTimes : kShimmerMost; }
    void shape() {
        const float hz = modRateHz(rate_.now()), fast = shimmerHz(hz), perMs = 0.001f * sr_, depth = clamp01(depth_.now());
        const float swirl = depth * kSwirlMs * perMs * (hz > kSwirlFullBelow ? kSwirlFullBelow / hz : 1.f);
        const float shimmer = depth * kShimmerMs * perMs * (fast > kShimmerFullBelow ? kShimmerFullBelow / fast : 1.f);
        const double behind = 0.5 * clamp01(spread_.now()); // the right's waves, half a turn on at most
        for (int j = 0; j < kCopies; ++j) {
            const double third = j / 3.0;
            shape_.to[j] = kCentreMs * perMs + swirl * turnSine(swirl_ + third) + shimmer * turnSine(shimmer_ + third);
            shape_.to[kCopies + j] = kCentreMs * perMs + swirl * turnSine(swirl_ + third + behind) + shimmer * turnSine(shimmer_ + third + behind);
        }
        const float mix = mix_.now();
        shape_.to[kDry] = mix == 0.f ? 1.f : std::cos(mix * 0.78539816f);
        shape_.to[kWet] = std::sin(mix * 0.78539816f);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&rate_, &depth_, &spread_, &mix_}) s->snap();
        swirl_ = shimmer_ = 0.0;
        cut_[0] = cut_[1] = 0.f;
        shape();
        shape_.snap();
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, tick_ = 0, quietIn_ = 0, quietLine_ = 0, restAfter_ = 1;
    bool asleep_ = true;
    double swirl_ = 0.0, shimmer_ = 0.0; // where the two waves are in their turns
    float tone_ = 1.f, cut_[2] = {};
    Gentle rate_, depth_, spread_, mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) modRateText(v, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Ensemble; }

} // namespace

const EffectInfo kEnsembleInfo = {
    "ENSEMBLE", 4, {"RATE", "DEPTH", "SPREAD", "MIX"}, {0.4f, 0.6f, 0.7f, 1.f}, // 0.58 Hz (5.6 Hz shimmer), 60%, 70%, all of it
    "ensemble", EffectFamily::Mod, {0, 0, 0, 0}, reading, create, "strings, a section",
};

} // namespace fx
} // namespace apotheca
