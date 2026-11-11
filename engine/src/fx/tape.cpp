// TAPE: a dub delay. Each repeat is duller, thinner and a little bent.
//
//   TIME      eight choices, in time with the song (kThirds below)
//   FEEDBACK  0 to 120%: how much of each echo goes round again. Past 100% it
//             howls, and holds there: the loop has a ceiling
//   AGE       how worn the tape is: wow, the dulling and the ceiling together
//   MIX       how loud the echoes are beside the sound (which stays as it is)
//
// One trip round the loop:
//
//   in --(+)--> the tape --> read between samples --> high cut --> low cut --+--> the echo
//        ^                   (TIME, gliding, + wow)                          |
//        +------ soft ceiling <------ x FEEDBACK <---------------------------+
//
// The numbers, all pinned by tests/effects_test.cpp:
//   - TIME is the step length to the nearest sample. Turned, or with a new
//     tempo, it GLIDES: the tape runs between half and double speed until it
//     is there, so what is on it swoops in pitch (an octave at most).
//   - Wow: a 0.8 Hz sine on the time, 1.5 ms either way at AGE full (which is
//     13 cents of pitch either way), in proportion to AGE. None at AGE 0.
//   - High cut: 12 kHz at AGE 0 down to 1.5 kHz at AGE full. Low cut: 40 Hz
//     up to 400 Hz. Both 6 dB an octave, and every echo passes them once more.
//   - The ceiling: 1.0 at AGE 0 down to 0.33 at AGE full. Well under it, and
//     midway between the cuts, an echo comes back at FEEDBACK.
//
// ON A BUS (the plan's sends): the dry sound is dropped and MIX is the level.
// That is `wetOnly_`, read in one place (gains()). When Effect gains its "you
// are on a bus" call, it sets that flag; nothing else changes.

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

// TIME in thirds of a step (a step is a sixteenth), so that triplets are whole.
constexpr int kTimes = 8;
constexpr int kThirds[kTimes] = {3, 4, 6, 8, 9, 12, 18, 24};
const char* const kTimeNames[kTimes] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOTTED", "1/4", "1/4 DOTTED", "1/2"};
int timeChoice(float v) {
    const int index = static_cast<int>(clamp01(v) * kTimes);
    return index > kTimes - 1 ? kTimes - 1 : index;
}

constexpr float kMaxFeedback = 1.2f;
constexpr float kWowHz = 0.8f;
constexpr float kWowSeconds = 0.0015f; // either way, at AGE full
constexpr float kSettle = 0.02f;       // seconds for a turned setting to arrive
constexpr float kTapeInertia = 0.15f;  // seconds: how fast the tape's speed follows TIME
constexpr double kSlowest = 0.5, kFastest = 2.0; // the tape's speed while TIME glides

// NUMBERS TOO SMALL TO HOLD are slow, and a cut left alone fades straight into
// them. So the cuts are fed this much on top of the tape (the low cut takes
// it out again), an echo under kQuiet (240 dB down) does not go round again,
// and sound coming in under kNothing does not go on. Between them no sum in
// the loop comes out smaller than about 1e-33, with or without the
// processor's flush-to-zero: there is nothing for it to do.
constexpr float kFloor = 1e-18f;

float onePole(float hz, float sr) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr ? 0.45f * sr : hz) / sr); }

class Tape : public Unit {
public:
    Tape() : Unit(kTapeInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        wowMost_ = kWowSeconds * sampleRate;
        // Room for the longest echo at the slowest tempo (half a bar at 40 BPM), and the wow.
        size_ = static_cast<int>(8.0 * 15.0 / 40.0 * sampleRate + wowMost_) + 16;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        low_[0] = low_[1] = high_[0] = high_[1] = kFloor; // where the cuts rest (see kFloor): the echo, their difference, is nothing
        at_ = 0;
        phase_ = 0.0;
        wow_ = 0.f;
        quiet_ = 0;
        asleep_ = true; // nothing on the tape
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        float coming = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float l = std::fabs(left[i]), r = std::fabs(right[i]);
            coming = l > coming ? l : coming;
            coming = r > coming ? r : coming;
        }
        const bool silent = !(coming >= kNothing);
        if (asleep_) {
            if (silent) {
                if (wetOnly_)
                    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
                return;
            }
            asleep_ = false;
            quiet_ = 0;
            phase_ = 0.0, wow_ = 0.f; // an empty tape starts as one fresh from the box does, wow and all
            arrive();                 // nothing is sounding: whatever was turned meanwhile is simply there
        }
        const float k = glideK(frames, kSettle, sr_);
        const float dFeedback = feedback_.begin(frames, k), dPush = push_.begin(frames, k), dCeiling = ceiling_.begin(frames, k);
        const float dHigh = highCut_.begin(frames, k), dLow = lowCut_.begin(frames, k), dDepth = depth_.begin(frames, k), dMix = mix_.begin(frames, k);
        float feedback = feedback_.now, push = push_.now, ceiling = ceiling_.now, highCut = highCut_.now, lowCut = lowCut_.now, depth = depth_.now, mix = mix_.now;
        // The wow: a slow sine, worked out at each end of the block and joined by a straight line.
        phase_ += 6.283185307179586 * kWowHz * frames / sr_;
        if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
        const float wowEnd = static_cast<float>(std::sin(phase_));
        const float dWow = (wowEnd - wow_) / static_cast<float>(frames);
        float wow = wow_;
        const float dry = wetOnly_ ? 0.f : 1.f;
        const double pull = 1.0 / (kTapeInertia * sr_);
        float* const tapeL = line_[0].data();
        float* const tapeR = line_[1].data();
        float lowL = low_[0], lowR = low_[1], highL = high_[0], highR = high_[1];
        double delay = delay_;
        int at = at_;
        float loudest = 0.f;
        for (int i = 0; i < frames; ++i) {
            feedback += dFeedback, push += dPush, ceiling += dCeiling, highCut += dHigh, lowCut += dLow, depth += dDepth, mix += dMix, wow += dWow;
            // TIME glides: the tape speeds up or slows down, never past double or half.
            if (delay != delayTo_) {
                const double left_ = delayTo_ - delay;
                double move = left_ * pull;
                move = move > 1.0 - kSlowest ? 1.0 - kSlowest : (move < 1.0 - kFastest ? 1.0 - kFastest : move);
                delay = std::fabs(left_) < 1e-3 ? delayTo_ : delay + move;
            }
            const double ago = delay + static_cast<double>(depth * wow);
            const int whole = static_cast<int>(ago);
            float w[4];
            cubicWeights(static_cast<float>(ago - whole), w);
            const float inL = left[i], inR = right[i];
            // What comes off the tape, through the two cuts: the echo.
            highL += highCut * (tapRead(tapeL, size_, at, whole, w) + kFloor - highL);
            highR += highCut * (tapRead(tapeR, size_, at, whole, w) + kFloor - highR);
            lowL += lowCut * (highL - lowL);
            lowR += lowCut * (highR - lowR);
            const float echoL = highL - lowL, echoR = highR - lowR;
            // Back onto the tape under the ceiling, with what is coming in.
            // (Nothing too faint goes round or on: see kFloor.)
            float backL = echoL * feedback * push, backR = echoR * feedback * push;
            backL = std::fabs(backL) < kQuiet ? 0.f : backL;
            backR = std::fabs(backR) < kQuiet ? 0.f : backR;
            const float nextL = (std::fabs(inL) < kNothing ? 0.f : inL) + ceiling * soft(backL);
            const float nextR = (std::fabs(inR) < kNothing ? 0.f : inR) + ceiling * soft(backR);
            tapeL[at] = nextL;
            tapeR[at] = nextR;
            const float most = std::fabs(nextL) > std::fabs(nextR) ? std::fabs(nextL) : std::fabs(nextR);
            loudest = most > loudest ? most : loudest;
            left[i] = inL * dry + mix * echoL;
            right[i] = inR * dry + mix * echoR;
            if (++at >= size_) at = 0;
        }
        at_ = at;
        delay_ = delay;
        wow_ = wowEnd;
        low_[0] = lowL, low_[1] = lowR, high_[0] = highL, high_[1] = highR;
        feedback_.done(), push_.done(), ceiling_.done(), highCut_.done(), lowCut_.done(), depth_.done(), mix_.done();
        // Asleep once the whole tape holds nothing worth the name.
        quiet_ = silent && loudest < kQuiet ? quiet_ + frames : 0;
        if (quiet_ >= size_) asleep_ = true;
    }

protected:
    void changed() override {
        const float age = p_[2];
        long frames = std::lround(kThirds[timeChoice(p_[0])] * 5.0 / tempo_ * sr_);
        const long least = static_cast<long>(wowMost_) + 8, most = size_ - static_cast<long>(wowMost_) - 8;
        delayTo_ = static_cast<double>(frames < least ? least : (frames > most ? most : frames));
        const float highHz = 12000.f * std::pow(0.125f, age), lowHz = 40.f * std::pow(10.f, age);
        highCut_.to = onePole(highHz, sr_);
        lowCut_.to = onePole(lowHz, sr_);
        // The two cuts together take a little off even what they pass best (2 dB
        // at AGE full); FEEDBACK makes that up, so that 100% is "just holds"
        // at every AGE, between the cuts.
        feedback_.to = kMaxFeedback * p_[1] * (1.f + lowHz / highHz);
        push_.to = 1.f + 2.f * age; // into the ceiling this much harder...
        ceiling_.to = 1.f / push_.to; // ...and the ceiling that much lower: small sounds come back as they were
        depth_.to = wowMost_ * age;
        mix_.to = p_[3];
    }

private:
    // Everything that glides is where it was turned to, at once.
    void arrive() {
        delay_ = delayTo_;
        feedback_.snap(), push_.snap(), ceiling_.snap(), highCut_.snap(), lowCut_.snap(), depth_.snap(), mix_.snap();
    }
    // The ceiling: straight for small sounds, flat at 1 from 1.5 up.
    static float soft(float x) {
        x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
        return x - (4.f / 27.f) * x * x * x;
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, quiet_ = 0;
    bool asleep_ = true;
    bool wetOnly_ = false; // ON A BUS: true drops the dry sound (see the top of this file)
    float tempo_ = 120.f, wowMost_ = 72.f, wow_ = 0.f;
    double delay_ = 24000.0, delayTo_ = 24000.0, phase_ = 0.0;
    float low_[2] = {}, high_[2] = {};
    Glide feedback_, push_, ceiling_, highCut_, lowCut_, depth_, mix_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", kTimeNames[timeChoice(v)]);
    else if (index == 1) readPercent(v * kMaxFeedback, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Tape; }

} // namespace

const EffectInfo kTapeInfo = {
    "TAPE", 4, {"TIME", "FEEDBACK", "AGE", "MIX"}, {0.5625f, 0.4f, 0.35f, 0.35f}, // a dotted eighth, 48%
    "tape", EffectGroup::Delay, {kTimes, 0, 0, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
