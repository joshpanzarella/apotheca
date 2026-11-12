// SATURATE: clean drive, four characters.
//
//   DRIVE   how hard the sound is pushed into the curve: up to 24 dB, sixteen times (FOLD: up to six times)
//   TYPE    TAPE   soft, the same both ways: odd harmonics only
//           TUBE   the same curve off centre, so one way bends before the other: even harmonics too
//           FOLD   past the top the wave folds back down (a wavefolder)
//           HARD   straight, then flat: a clipper with a small round corner
//   TONE    a low-pass after it: 500 Hz to 20 kHz
//   MIX     0 is the sound untouched
//
// It is worked out at twice the sample rate (halfband.hpp), so the harsh
// fold-back the old DRIVE has up high is far lower: tests/effects_test.cpp
// has the numbers, side by side. The curves are cheap ones (a ratio of small
// polynomials, a polynomial sine): the plan measured `tanh` at more than
// twice the cost for the same fold-back.
//
// The loudness is held level as DRIVE rises: for each DRIVE and TYPE the
// output is scaled so that a sine at half scale comes out as loud as it went
// in. Quieter sounds come up and louder ones come down, which is what drive is.
//
// Every setting glides, and a change of TYPE is a 10 ms fade from one curve
// to the other: turning one is not a click.

#include "halfband.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kReference = 0.5f; // the sine that comes out as loud as it went in
constexpr float kBias = 0.5f;      // TUBE: how far off centre
constexpr float kGlide = 0.02f;
constexpr float kTypeFade = 0.01f; // seconds
constexpr float kDcHz = 5.f;       // TUBE leaves the wave off centre: this brings it back

// tanh, nearly: x (27 + x^2) / (27 + 9 x^2), flat beyond 3 (where it has just reached 1, level).
inline float soft(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
const float kSoftAtBias = soft(kBias);
inline float tape(float x) { return soft(x); }
inline float tube(float x) { return soft(x + kBias) - kSoftAtBias; }
// sin(x * pi / 2): 1 at x = 1, back to 0 at 2, -1 at 3...
inline float fold(float x) {
    float t = 0.25f * x;
    t -= static_cast<float>(static_cast<int>(t + (t < 0.f ? -0.5f : 0.5f))); // -0.5..0.5 of a turn
    float u = 4.f * t;
    if (u > 1.f) u = 2.f - u;
    else if (u < -1.f) u = -2.f - u;
    const float u2 = u * u;
    return u * (1.5707963f + u2 * (-0.64596410f + u2 * (0.079692626f + u2 * (-0.0046817541f + u2 * 0.00016044118f))));
}
// Straight to 0.75, flat from 1.25, a curve between.
inline float hard(float x) {
    const float a = std::fabs(x);
    if (a <= 0.75f) return x;
    const float y = a >= 1.25f ? 1.f : a - (a - 0.75f) * (a - 0.75f);
    return x < 0.f ? -y : y;
}
inline float curve(int type, float x) { return type == 0 ? tape(x) : type == 1 ? tube(x) : type == 2 ? fold(x) : hard(x); }

const char* const kNames[4] = {"TAPE", "TUBE", "FOLD", "HARD"};
int typeOf(float v) {
    const int i = static_cast<int>(clamp01(v) * 4.f);
    return i > 3 ? 3 : i;
}
// How many times louder the sound goes into the curve.
float pushOf(int type, float drive) { return type == 2 ? 1.f + 5.f * drive : std::pow(10.f, 1.2f * drive); }
// What to scale the curve's output by so that the reference sine comes out as loud as it went in.
float levelFor(int type, float push) {
    static float sines[32];
    static bool made = false;
    if (!made) {
        for (int k = 0; k < 32; ++k) sines[k] = std::sin(6.2831853f * (static_cast<float>(k) + 0.5f) / 32.f);
        made = true;
    }
    float sum = 0.f, squares = 0.f;
    for (int k = 0; k < 32; ++k) {
        const float y = curve(type, push * kReference * sines[k]);
        sum += y;
        squares += y * y;
    }
    const float mean = sum / 32.f;
    const float power = squares / 32.f - mean * mean; // what is left once it is back on centre
    return power > 1e-12f ? kReference * 0.70710678f / std::sqrt(power) : 1.f;
}

class Saturate : public Unit {
public:
    explicit Saturate(bool twice = true) : Unit(kSaturateInfo), twice_(twice) { levelFor(0, 1.f); } // the table of sines is made here, off the audio thread
    void reset() override {
        for (int c = 0; c < 2; ++c) {
            up_[c].reset();
            down_[c].reset();
            dcIn_[c] = dcOut_[c] = tone_[c] = 0.f;
        }
        fade_ = 0;
        sinceSettle_ = 0;
        settled_ = false;
    }
    void process(float* left, float* right, int frames) override {
        while (frames > 0) {
            // In blocks that end every 64th sample it has ever been given,
            // however the caller cuts the sound up: what is done between
            // blocks is then done at the same samples, and the result is the
            // same to the last bit.
            const int n = frames < kBlock - sinceSettle_ ? frames : kBlock - sinceSettle_;
            block(left, right, n);
            left += n;
            right += n;
            frames -= n;
            sinceSettle_ += n;
            if (sinceSettle_ == kBlock) {
                sinceSettle_ = 0;
                settle();
            }
        }
    }

protected:
    void changed() override {
        const int type = typeOf(p_[1]);
        if (type != type_ && settled_) {
            // Another curve: the old one, as it stood, fades out under the new one.
            oldType_ = type_;
            oldPush_ = push_;
            oldLevel_ = level_;
            fadeLength_ = fade_ = static_cast<int>(kTypeFade * sr_) > 0 ? static_cast<int>(kTypeFade * sr_) : 1;
            push_ = pushOf(type, drive_.now);
            level_ = levelFor(type, push_);
        }
        type_ = type;
        drive_.set(p_[0]);
        const float hz = 500.f * std::pow(40.f, p_[2]); // 500 Hz to 20 kHz
        toneCoeff_.set(1.f - std::exp(-6.2831853f * (hz > 0.45f * sr_ ? 0.45f * sr_ : hz) / sr_));
        mix_.set(p_[3]);
        dcKeep_ = 1.f - 6.2831853f * kDcHz / sr_;
    }

private:
    static constexpr int kBlock = 64;

    void block(float* left, float* right, int frames) {
        Glide* const all[] = {&drive_, &toneCoeff_, &mix_};
        if (!settled_) {
            for (Glide* g : all) g->snap();
            push_ = pushOf(type_, drive_.now);
            level_ = levelFor(type_, push_);
            settled_ = true;
        }
        const float part = glidePart(frames, kGlide, sr_);
        for (Glide* g : all) g->begin(frames, part);
        // DRIVE slides, and the level that keeps the loudness even goes with
        // it: both are worked out for where DRIVE will be at the end of the
        // block, and go there in a straight line.
        float pushStep = 0.f, levelStep = 0.f, pushEnd = push_, levelEnd = level_;
        if (!drive_.still()) {
            pushEnd = pushOf(type_, drive_.end);
            levelEnd = levelFor(type_, pushEnd);
            pushStep = (pushEnd - push_) / static_cast<float>(frames);
            levelStep = (levelEnd - level_) / static_cast<float>(frames);
        }
        // The same for both sides: how hard, how loud, and how much of the old curve, sample by sample.
        float push[kBlock], level[kBlock], old[kBlock], coeff[kBlock], mix[kBlock];
        const bool fading = fade_ > 0;
        for (int i = 0; i < frames; ++i) {
            push[i] = push_ + pushStep * static_cast<float>(i + 1);
            level[i] = level_ + levelStep * static_cast<float>(i + 1);
            coeff[i] = toneCoeff_.next();
            mix[i] = mix_.next();
            old[i] = fade_ > 0 ? static_cast<float>(--fade_) / static_cast<float>(fadeLength_) : 0.f;
        }
        const int n = twice_ ? 2 : 1; // samples worked out for each one that goes by
        float* io[2] = {left, right};
        for (int c = 0; c < 2; ++c) {
            float work[2 * kBlock];
            if (twice_) up_[c].up(io[c], work, frames);
            else
                for (int i = 0; i < frames; ++i) work[i] = io[c][i];
            if (fading) {
                for (int i = 0; i < n * frames; ++i) {
                    const int at = i / n;
                    const float now = curve(type_, work[i] * push[at]) * level[at];
                    work[i] = now + old[at] * (curve(oldType_, work[i] * oldPush_) * oldLevel_ - now);
                }
            } else {
                switch (type_) {
                case 0:
                    for (int i = 0; i < n * frames; ++i) work[i] = tape(work[i] * push[i / n]) * level[i / n];
                    break;
                case 1:
                    for (int i = 0; i < n * frames; ++i) work[i] = tube(work[i] * push[i / n]) * level[i / n];
                    break;
                case 2:
                    for (int i = 0; i < n * frames; ++i) work[i] = fold(work[i] * push[i / n]) * level[i / n];
                    break;
                default:
                    for (int i = 0; i < n * frames; ++i) work[i] = hard(work[i] * push[i / n]) * level[i / n];
                    break;
                }
            }
            float wet[kBlock];
            if (twice_) down_[c].down(work, wet, frames);
            else
                for (int i = 0; i < frames; ++i) wet[i] = work[i];
            float dcIn = dcIn_[c], dcOut = dcOut_[c], tone = tone_[c];
            for (int i = 0; i < frames; ++i) {
                dcOut = wet[i] - dcIn + dcKeep_ * dcOut; // back on centre
                dcIn = wet[i];
                tone += coeff[i] * (dcOut - tone);
                const float in = io[c][i];
                io[c][i] = in + mix[i] * (tone - in);
            }
            dcIn_[c] = dcIn;
            dcOut_[c] = dcOut;
            tone_[c] = tone;
        }
        for (Glide* g : all) g->finish();
        push_ = pushEnd;
        level_ = levelEnd;
    }

    // What has rung down to nearly nothing is nothing.
    void settle() {
        for (int c = 0; c < 2; ++c) {
            up_[c].settle();
            down_[c].settle();
            if (std::fabs(dcOut_[c]) < 1e-20f) dcOut_[c] = 0.f;
            if (std::fabs(tone_[c]) < 1e-20f) tone_[c] = 0.f;
        }
    }

    const bool twice_;
    Halfband up_[2], down_[2];
    Glide drive_, toneCoeff_, mix_;
    float push_ = 1.f, level_ = 1.f; // how hard into the curve and how loud out of it, as of the end of the last block
    float dcIn_[2] = {}, dcOut_[2] = {}, tone_[2] = {}, dcKeep_ = 1.f;
    int type_ = 0, oldType_ = 0, fade_ = 0, fadeLength_ = 1, sinceSettle_ = 0;
    float oldPush_ = 1.f, oldLevel_ = 1.f;
    bool settled_ = false;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 1) std::snprintf(out, static_cast<std::size_t>(size), "%s", kNames[typeOf(v)]);
    else readPercent(v, out, size);
}

Effect* create() { return new Saturate; }

} // namespace

const EffectInfo kSaturateInfo = {
    "SATURATE", 4, {"DRIVE", "TYPE", "TONE", "MIX"}, {0.4f, 0.f, 0.8f, 1.f},
    "saturate", EffectGroup::Drive, {0, 4, 0, 0}, reading, create,
};

Effect* makeSaturatePlain() { return new Saturate(false); }
float saturateCurve(int type, float x) { return curve(type, x); }

} // namespace fx
} // namespace apotheca
