// PING: a clean echo that bounces from side to side. TAPE's loop without the wear.
//
//   TIME      eight choices, in time with the song (TAPE's eight: kThirds below)
//   FEEDBACK  0 to 90%: how much of each echo is in the next. It reads what it
//             does. Under 1 at its most, so it cannot ring for good
//   TONE      how bright the repeats stay: a high cut each echo passes on its
//             way to the other side, 600 Hz to 18 kHz (the old DELAY's range)
//   MIX       how loud the echoes are beside the sound (which stays as it is)
//
// Two lines, one a side, each TIME long. What comes in goes on the LEFT line
// (both sides of it, halved and added: a sound in the middle, or on one side
// only, bounces all the same). What comes off the left line is the left echo,
// and goes on to the right line; what comes off that is the right echo, and
// goes back to the left:
//
//   in (L + R) / 2 --(+)--> left line --+--> the LEFT echo
//                     ^                 |
//                     |            high cut, x FEEDBACK
//                     |                 v
//       x FEEDBACK, high cut        right line --+--> the RIGHT echo
//                     ^                          |
//                     +--------------------------+
//
// So the echoes land TIME apart, left, right, left..., and each is FEEDBACK
// of the one before it (and the high cut once more). The first is the sound
// as it came, not cut at all.
//
// What that costs, measured (testPing), and the owner's to change: the tail
// LEANS LEFT, since each right echo is FEEDBACK of the left one before it
// (over a whole tail the left is 7.8 dB louder at 45%, 1.6 dB at 90%; at 0%
// the one echo is on the left only). And the sound's own width is not kept:
// a sound hard right echoes first on the left at half its size, and a sound
// opposite on the two sides echoes not at all. Ways to change it (a setting
// for the side it starts on, or some of the input on the right line too)
// would each cost the plain left, right, left of a mono sound.
//
// The numbers, all pinned by tests/effects_test.cpp (testPing):
//   - TIME is the step length to the nearest sample. Turned, or with a new
//     tempo, it GLIDES as TAPE's does: the lines run between half and double
//     speed until it is there (a swoop in pitch, an octave at most), read
//     between samples meanwhile. At rest a line is read on the sample: an
//     echo is the sound itself, to the bit.
//   - FEEDBACK and TONE and MIX slide for 20 ms when turned, and land.
//   - Nothing under kQuiet (240 dB down) goes on a line: an exact zero does.
//     Once both lines hold nothing but zeros it is asleep (no work, and
//     "silence in, silence out" to the zero), and what wakes it finds it as a
//     fresh one. The high cuts rest on kFloor, so nothing in here fades
//     through the numbers too small to hold.
//   - Awake or asleep is decided on each SAMPLE, never at a block's edge: the
//     sound is the same to the bit however it is cut into blocks.
//
// ON A BUS: the dry sound is dropped and MIX is the level (Unit's `bus_`).

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

// TIME in thirds of a step (a step is a sixteenth), so that triplets are whole. As TAPE has them.
constexpr int kTimes = 8;
constexpr int kThirds[kTimes] = {3, 4, 6, 8, 9, 12, 18, 24};
const char* const kTimeNames[kTimes] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/4 DOT", "1/2"};
int timeChoice(float v) {
    const int index = static_cast<int>(clamp01(v) * kTimes);
    return index > kTimes - 1 ? kTimes - 1 : index;
}

constexpr float kMaxFeedback = 0.9f;    // the most one echo is of the last: under 1, so a tail always ends
constexpr float kSettle = 0.02f;        // seconds for a turned setting to arrive
constexpr float kInertia = 0.15f;       // seconds: how fast the lines' speed follows TIME
constexpr double kSlowest = 0.5, kFastest = 2.0; // the lines' speed while TIME glides
constexpr float kFloor = 1e-18f;        // where the high cuts rest (glide.hpp: numbers too small to hold)

float onePole(float hz, float sr) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * sr ? 0.45f * sr : hz) / sr); }

class Ping : public Unit {
public:
    Ping() : Unit(kPingInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        // Room for the longest echo at the slowest tempo: half a bar at 40 BPM.
        size_ = static_cast<int>(8.0 * 15.0 / 40.0 * sampleRate) + 16;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        at_ = 0;
        quiet_ = 0;
        asleep_ = true; // nothing on the lines
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        soundOnly(left, right, frames);
        while (frames > 0) {
            int n = 0;
            if (asleep_) {
                while (n < frames && !(std::fabs(left[n]) >= kNothing) && !(std::fabs(right[n]) >= kNothing)) ++n;
                if (bus_)
                    for (int i = 0; i < n; ++i) left[i] = right[i] = 0.f;
                if (n < frames) wake(); // at this sample
            } else {
                n = size_ - quiet_ < frames ? size_ - quiet_ : frames; // no further than where it could fall asleep
                run(left, right, n);
                if (quiet_ >= size_) asleep_ = true; // both lines are zeros from end to end
            }
            left += n, right += n, frames -= n;
        }
    }

protected:
    void changed() override {
        const long frames = std::lround(kThirds[timeChoice(p_[0])] * 5.0 / tempo_ * sr_);
        const long least = 8, most = size_ - 8;
        delayTo_ = static_cast<double>(frames < least ? least : (frames > most ? most : frames));
        feedback_.to = kMaxFeedback * p_[1];
        cut_.to = onePole(600.f * std::pow(30.f, p_[2]), sr_); // 600 Hz to 18 kHz
        mix_.to = p_[3];
    }

private:
    void wake() {
        asleep_ = false;
        quiet_ = 0;
        arrive(); // nothing is sounding: whatever was turned meanwhile is simply there
    }
    // A stretch of the lines running.
    void run(float* left, float* right, int frames) {
        const float k = glideK(frames, kSettle, sr_);
        const float dFeedback = feedback_.begin(frames, k), dCut = cut_.begin(frames, k), dMix = mix_.begin(frames, k);
        float feedback = feedback_.now, cut = cut_.now, mix = mix_.now;
        const float dry = bus_ ? 0.f : 1.f;
        const double pull = 1.0 / (kInertia * sr_);
        float* const lineL = line_[0].data();
        float* const lineR = line_[1].data();
        float cutL = cutState_[0], cutR = cutState_[1];
        double delay = delay_;
        int at = at_, quiet = quiet_;
        for (int i = 0; i < frames; ++i) {
            feedback += dFeedback, cut += dCut, mix += dMix;
            float echoL, echoR;
            if (delay == delayTo_) {
                // At rest: on the sample.
                int from = at - static_cast<int>(delay);
                if (from < 0) from += size_;
                echoL = lineL[from], echoR = lineR[from];
            } else {
                // TIME glides: the lines speed up or slow down, never past double or half.
                const double away = delayTo_ - delay;
                double move = away * pull;
                move = move > 1.0 - kSlowest ? 1.0 - kSlowest : (move < 1.0 - kFastest ? 1.0 - kFastest : move);
                delay = std::fabs(away) < 1e-3 ? delayTo_ : delay + move;
                const int whole = static_cast<int>(delay);
                float w[4];
                cubicWeights(static_cast<float>(delay - whole), w);
                echoL = tapRead(lineL, size_, at, whole, w), echoR = tapRead(lineR, size_, at, whole, w);
            }
            const float inL = left[i], inR = right[i];
            // Each echo on its way to the other side: the high cut, and FEEDBACK.
            cutL += cut * (echoL + kFloor - cutL);
            cutR += cut * (echoR + kFloor - cutR);
            float toL = 0.5f * (inL + inR) + feedback * cutR, toR = feedback * cutL;
            // Nothing too faint goes on a line: it is silence there, exactly.
            toL = std::fabs(toL) < kQuiet ? 0.f : toL;
            toR = std::fabs(toR) < kQuiet ? 0.f : toR;
            lineL[at] = toL;
            lineR[at] = toR;
            quiet = toL == 0.f && toR == 0.f ? quiet + 1 : 0;
            left[i] = inL * dry + mix * echoL;
            right[i] = inR * dry + mix * echoR;
            if (++at >= size_) at = 0;
        }
        at_ = at;
        delay_ = delay;
        cutState_[0] = cutL, cutState_[1] = cutR;
        feedback_.done(), cut_.done(), mix_.done();
        quiet_ = quiet;
    }
    // Everything that glides is where it was turned to, at once, and the cuts are at rest.
    void arrive() {
        delay_ = delayTo_;
        cutState_[0] = cutState_[1] = kFloor;
        feedback_.snap(), cut_.snap(), mix_.snap();
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, quiet_ = 0; // quiet_: how many samples in a row both lines were given a zero
    bool asleep_ = true;
    float tempo_ = 120.f;
    double delay_ = 18000.0, delayTo_ = 18000.0;
    float cutState_[2] = {kFloor, kFloor};
    Glide feedback_, cut_, mix_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", kTimeNames[timeChoice(v)]);
    else if (index == 1) readPercent(v * kMaxFeedback, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Ping; }

// TIME in steps, to match another delay's TIME by (changing TYPE).
float amount(int index, float v) { return index == 0 ? static_cast<float>(kThirds[timeChoice(v)]) / 3.f : std::nanf(""); }

} // namespace

const EffectInfo kPingInfo = {
    "PING", 4, {"TIME", "FEEDBACK", "TONE", "MIX"}, {0.5625f, 0.5f, 0.6f, 0.35f}, // a dotted eighth, 45%
    "ping", EffectGroup::Delay, {kTimes, 0, 0, 0}, reading, create,
    "", "DELAY", "PING", amount,
};

} // namespace fx
} // namespace apotheca
