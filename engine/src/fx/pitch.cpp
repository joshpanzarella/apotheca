// PITCH: a pitch shifter, a TYPE of MOD (owner, 2026-11-24: "Let's do ... pitch shifter"). The sound again,
// higher or lower, beside itself or instead of itself: an octave down under a bass, a fifth over a lead.
//
//   PITCH  -12 to +12 semitones, a click each (a choice: the D-pad steps it).
//   FINE   -50 to +50 cents on top: a little either way thickens it (a doubler).
//   GRAIN  how long each piece read is, 20 to 120 ms: short is quick and buzzes on low notes; long is smooth
//          and blurs fast playing.
//   MIX    0 is the sound untouched, to the sample; full is the shifted sound alone.
//
// How: the sound goes into a short line, and two heads read it back at the new speed, half a GRAIN apart. A
// head that reaches the end of its GRAIN jumps back to its start while the other is at full: each fades in and
// out in a straight line (a triangle), and the two add to one at every sample. Read between samples (the
// cubic, glide.hpp). At PITCH 0 and FINE 0 the heads stand still, so the wet sound is the dry one a GRAIN late:
// mixed with the dry it is a comb, as any shifter's is there.
//
// What glides is worked out on the TICK (16 samples counted from the start): the speed (in semitones, so a step
// of PITCH is a slide of a few ms, not a jump) and GRAIN; MIX glides every sample. Nothing under kQuiet goes on
// the line. The heads run on whether there is sound or not, so it sleeps only after kRestSeconds of nothing
// (and its line all zeros), and wakes as a fresh one: the same render however long the silence was.

#include <cmath>
#include <vector>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kSemis = 25;               // PITCH's choices: -12 to +12
constexpr float kShortest = 0.02f, kLongest = 0.12f; // GRAIN, seconds
constexpr float kRestSeconds = 4.f;
constexpr float kSettle = 0.02f;
constexpr int kLeast = 3; // the heads read at least this far back (the cubic reads two either side)

int semisOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kSemis);
    return (i >= kSemis ? kSemis - 1 : i) - 12;
}
float centsOf(float v) { return 100.f * clamp01(v) - 50.f; }
float grainOf(float v) { return kShortest + (kLongest - kShortest) * clamp01(v); }

class Pitch : public Unit {
public:
    Pitch() : Unit(kPitchInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        int size = 1;
        while (size < static_cast<int>(kLongest * sampleRate) + 2 * kLeast + 8) size *= 2;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size), 0.f);
        mask_ = size - 1;
        rest_ = static_cast<int>(kRestSeconds * sampleRate);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) std::fill(line.begin(), line.end(), 0.f);
        at_ = 0;
        phase_ = 0.f;
        quietIn_ = 0;
        asleep_ = true;
        semis_.snap(), grain_.snap(), mix_.snap();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0 || line_[0].empty()) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        float* const lines[2] = {line_[0].data(), line_[1].data()};
        const int size = mask_ + 1;
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {std::fabs(left[i]) < kQuiet ? 0.f : left[i], std::fabs(right[i]) < kQuiet ? 0.f : right[i]};
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
            // The heads: they fall behind (lower) or catch up (higher) by 1 - speed a sample, across the GRAIN.
            const float grain = grain_.next() * sr_;
            phase_ += turn_ / grain;
            phase_ -= std::floor(phase_);
            for (int c = 0; c < 2; ++c) lines[c][at_] = x[c];
            float wet[2] = {0.f, 0.f};
            for (int head = 0; head < 2; ++head) {
                float ph = phase_ + 0.5f * static_cast<float>(head);
                if (ph >= 1.f) ph -= 1.f;
                const float gain = 1.f - std::fabs(2.f * ph - 1.f); // a triangle: the two add to one
                if (gain == 0.f) continue;
                const float back = static_cast<float>(kLeast) + ph * grain;
                const int whole = static_cast<int>(back);
                float w[4];
                cubicWeights(back - static_cast<float>(whole), w);
                for (int c = 0; c < 2; ++c) wet[c] += gain * tapRead(lines[c], size, (at_ + 1) & mask_, whole, w);
            }
            at_ = (at_ + 1) & mask_;
            for (int c = 0; c < 2; ++c)
                if (mix != 0.f) io[c][i] = x[c] + mix * (wet[c] - x[c]);
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick(size);
        }
    }

protected:
    void changed() override {
        semis_.to = static_cast<float>(semisOf(p_[0])) + centsOf(p_[1]) / 100.f;
        grain_.to = grainOf(p_[2]);
        mix_.to = p_[3];
    }

private:
    void beginTick() {
        const float k = glideK(kTick, kSettle, sr_);
        semis_.begin(kTick, k), grain_.begin(kTick, k), mix_.begin(kTick, k);
        // The speed for this tick, from where PITCH and FINE are going by its end (sliding, the steps are a few
        // hundredths of a semitone: not heard).
        turn_ = 1.f - std::exp2(semis_.end * (1.f / 12.f));
    }
    void endTick(int size) {
        tick_ = 0;
        semis_.done(), grain_.done(), mix_.done();
        // Asleep: nothing for the rest time, and nothing in the line (every place written as an exact zero since).
        if (quietIn_ >= rest_ && quietIn_ >= size) {
            for (auto& line : line_) std::fill(line.begin(), line.end(), 0.f); // (zeros already: kept exact)
            asleep_ = true;
        }
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        at_ = 0;
        phase_ = 0.f;
        semis_.snap(), grain_.snap(), mix_.snap();
    }

    std::vector<float> line_[2];
    int mask_ = 0, at_ = 0, rest_ = 0;
    float phase_ = 0.f, turn_ = 0.f;
    Glide semis_, grain_, mix_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const int semis = semisOf(v);
        if (semis == 0) std::snprintf(out, n, "0 ST");
        else std::snprintf(out, n, "%+d ST", semis);
    } else if (index == 1) {
        const int cents = static_cast<int>(std::round(centsOf(v)));
        if (cents == 0) std::snprintf(out, n, "0 CENTS");
        else std::snprintf(out, n, "%+d CENTS", cents);
    } else if (index == 2) {
        std::snprintf(out, n, "%.0f MS", static_cast<double>(1000.f * grainOf(v)));
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Pitch; }

} // namespace

const EffectInfo kPitchInfo = {
    "PITCH", 4, {"PITCH", "FINE", "GRAIN", "MIX"}, {(24.f + 0.5f) / kSemis, 0.5f, 0.4f, 0.5f}, // an octave up, beside the sound
    "pitch", EffectFamily::Mod, {kSemis, 0, 0, 0}, reading, create,
    "higher or lower: an octave, a fifth",
};

} // namespace fx
} // namespace apotheca
