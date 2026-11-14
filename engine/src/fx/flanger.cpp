// FLANGER: a TYPE of MOD. The sound laid over a copy of itself a moment
// late, the moment swept up and down: the notches where the two cancel sweep
// through the sound, and FEEDBACK makes the peaks between them ring: the
// jet plane.
//
//   RATE      how fast it sweeps: 0.1 to 8 Hz, CHORUS's scale (carried to the bit)
//   DEPTH     how far: at 100% the copy runs from 0.35 to 7.3 ms late, an
//             even sweep in pitch (the delay moves by octaves, not by
//             milliseconds). Above 2 Hz the sweep narrows as the rate rises,
//             so the pitch never bends more than it does at 2 Hz.
//   FEEDBACK  -100 to +100%: how much of the copy goes round again (0.95 of
//             it at the ends). Up, the peaks ring metallic, at the notes of
//             the delay; down, hollow, an octave lower and only the odd ones.
//   MIX       0 is the sound untouched; 100% is the plain sound and the copy
//             half and half, where the notches go all the way down.
//
// HOW IT DIFFERS FROM CHORUS's FLANGE (one short copy, 2.2 ms swung 1.9 ms
// each way, a fixed feedback of 0.6 and a sine): the sweep here runs over
// 4.4 octaves (0.35 to 7.3 ms) where FLANGE's runs 0.3 to 4.1 ms by a sine
// in milliseconds, which hurries through the short end and lingers at the
// long; this one moves at an even speed in pitch (a triangle in octaves,
// rounded only at its turns) so the whoosh rises and falls evenly; the feedback is yours, either sign, to
// 95%; and both sides sweep together, so the jet sits in the middle (FLANGE
// swings the two sides a quarter turn apart).
//
// The copy is read between samples (glide.hpp's cubic, safe in a loop);
// where the sweep is, and every setting, is worked out on the TICK (svf.hpp)
// and slid to sample by sample. Nothing under kQuiet goes on the line; an
// exact zero does. Asleep (mod.hpp's rest rule): nothing has come in for 4 s
// and the line is zeros from end to end. The copy kept at the same loudness
// as FEEDBACK rises: noise comes out about as loud at every FEEDBACK
// (the wet is scaled by sqrt(1 - feedback^2)).

#include <initializer_list>
#include <vector>

#include "mod.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kCentreMs = 1.6f;       // the middle of the sweep
constexpr float kOctaves = 2.2f;        // each way, at DEPTH 100%
constexpr float kMostFeedback = 0.95f;  // under 1: the tail always ends
constexpr float kFullSweepBelow = 2.f;  // Hz
constexpr float kSettle = 0.012f;       // seconds, each of Gentle's two
enum { kDelay, kFeedback, kDry, kWet, kShapes };

float flangerMs(float depth, float rate, double at) {
    const float hz = modRateHz(rate);
    const float tri = 4.f * static_cast<float>(std::fabs(wrapTurn(at) - 0.5)) - 1.f; // 1 at its start: the longest
    const float lfo = tri * (1.5f - 0.5f * tri * tri);                              // rounded at the turns
    const float d = clamp01(depth) * (hz > kFullSweepBelow ? kFullSweepBelow / hz : 1.f);
    return kCentreMs * std::exp2(kOctaves * d * lfo);
}

class Flanger : public Unit {
public:
    Flanger() : Unit(kFlangerInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        size_ = lineSize(kCentreMs * std::exp2(kOctaves) * 0.001f * sampleRate + 4.f);
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        restAfter_ = static_cast<int>(kModRestSeconds * sr_);
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
            const float delay = shape_.now[kDelay], feedback = shape_.now[kFeedback];
            const float yl = lineRead(lineL, mask, at_, delay), yr = lineRead(lineR, mask, at_, delay);
            float wl = xl + feedback * yl, wr = xr + feedback * yr;
            wl = std::fabs(wl) < kQuiet ? 0.f : wl; // nothing too faint goes on the line: an exact zero does
            wr = std::fabs(wr) < kQuiet ? 0.f : wr;
            lineL[at_] = wl, lineR[at_] = wr;
            at_ = (at_ + 1) & mask;
            quietLine_ = wl == 0.f && wr == 0.f ? (quietLine_ < size_ ? quietLine_ + 1 : size_) : 0;
            const float dry = shape_.now[kDry], wet = shape_.now[kWet];
            if (dry != 1.f || wet != 0.f) {
                left[i] += (dry - 1.f) * xl + wet * yl;
                right[i] += (dry - 1.f) * xr + wet * yr;
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        rate_.first.to = p_[0];
        depth_.first.to = p_[1];
        feedback_.first.to = p_[2];
        mix_.first.to = p_[3];
    }

private:
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&rate_, &depth_, &feedback_, &mix_})
            if (s->moving()) s->tick(g);
        phase_ = wrapTurn(phase_ + kTick * static_cast<double>(modRateHz(rate_.now())) / sr_);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        if (quietIn_ >= restAfter_ && quietLine_ >= size_) asleep_ = true; // nothing came in for 4 s, and the line is empty
    }
    // Where everything is at the tick's end (or now, on arriving).
    void shape() {
        float delay = flangerMs(depth_.now(), rate_.now(), phase_) * 0.001f * sr_;
        delay = delay < 2.f ? 2.f : (delay > static_cast<float>(size_ - 4) ? static_cast<float>(size_ - 4) : delay);
        const float feedback = kMostFeedback * signedOf(feedback_.now()), mix = mix_.now();
        shape_.to[kDelay] = delay;
        shape_.to[kFeedback] = feedback;
        shape_.to[kDry] = 1.f - 0.5f * mix;
        shape_.to[kWet] = 0.5f * mix * std::sqrt(1.f - feedback * feedback);
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&rate_, &depth_, &feedback_, &mix_}) s->snap();
        phase_ = 0.0;
        shape();
        shape_.snap();
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, tick_ = 0, quietIn_ = 0, quietLine_ = 0, restAfter_ = 1;
    bool asleep_ = true;
    double phase_ = 0.0; // where the sweep is in its turn, 0 to 1
    Gentle rate_, depth_, feedback_, mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) modRateText(v, out, size);
    else if (index == 2) signedText(v, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Flanger; }

} // namespace

const EffectInfo kFlangerInfo = {
    "FLANGER", 4, {"RATE", "DEPTH", "FEEDBACK", "MIX"}, {0.2f, 0.7f, 0.8f, 1.f}, // 0.24 Hz, 70%, +60%, all of it
    "flanger", EffectFamily::Mod, {0, 0, 0, 0}, reading, create, "a jet sweeping",
};

float flangerDelayMs(float depth, float rate, double at) { return flangerMs(depth, rate, at); }

} // namespace fx
} // namespace apotheca
