// DUB: a TYPE of DELAY. A dark echo whose loop is a band, not a full range:
// each repeat is narrower and darker than the last, and turned past 100% it
// sings: the loop rings up into a held howl at the note TONE sets, held by a
// ceiling. Turning TONE then sweeps the howl (the dub siren); turning TIME
// swoops it.
//
//   TIME      eight choices, in time with the song (TAPE's and PING's eight)
//   FEEDBACK  0 to 150%: how much of each echo goes round again, at the top of
//             the band. Past 100% it howls, and holds under a ceiling. Left
//             alone a while, it comes down by itself (IT ALWAYS ENDS, below)
//   TONE      where the band is: 250 Hz to 4 kHz (it reads as a percentage,
//             as PING's and CLEAN's TONE do, so TYPE carries it). Low is dark
//             and boomy, high is thin and bright. It is also the note the
//             howl rings at
//   MIX       how loud the echoes are beside the sound (which stays as it is)
//
// One trip round the loop:
//
//   in (L + R) / 2 --(+)--> the line --> read (TIME, gliding) --> band (TONE) --+--> the echo, both sides
//                    ^                                                          |
//                    +-------------- ceiling <------- x FEEDBACK <--------------+
//
// HOW IT DIFFERS FROM TAPE: TAPE's loop passes 40 Hz to 12 kHz (narrowed by
// AGE), sways in pitch (wow) and goes to 120%; its howl is wherever the sound
// was loudest. DUB's loop is a band two octaves wide (a filter with Q 0.7),
// one place you choose: every repeat loses more of the top and the bottom, so
// a few repeats in it is a muffled thump, and the howl is always at TONE. It
// goes to 150%, so it rises into the howl fast; there is no wow (the howl is
// a steady note); TIME glides twice as slowly as TAPE's (0.3 s: a long swoop
// when it is turned); and it is ONE echo for both sides, in the middle (a dub
// mixer's send to a mono tape echo). TAPE keeps the sides apart.
//
// IT ALWAYS ENDS (TAPE's rule, CLAUDE.md "A tail always ends"): while sound
// is coming in (over kFedLevel, 80 dB under full) and for kHoldSeconds (30)
// after the last of it, FEEDBACK is what it reads, howl and all. Left alone
// longer, a FEEDBACK over 85% eases down to 85% over a couple of seconds, and
// from there each echo is under 0.85 of the last at the top of the band: the
// howl dies away. Sound coming in again puts it back at once (20 ms). A
// FEEDBACK that reads 85% or less is never touched, to the bit. Counted and
// eased a SAMPLE at a time.
//
// Everything else: settings set off gently on the TICK (svf.hpp); the band's
// numbers are slid to sample by sample; TIME glides as TAPE's does, a sample
// at a time (between half and double speed). Nothing under kQuiet goes on the
// line; the band's state is made an exact zero under kTiny on each tick.
// Asleep once the line is zeros from end to end and the band is empty; a
// fresh one when it wakes.
//
// ON A BUS: the dry sound is dropped and MIX is the level (Unit's `bus_`).

#include <initializer_list>
#include <vector>

#include "echo.hpp"
#include "glide.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kMaxFeedback = 1.5f;
constexpr float kTiredFeedback = 0.85f;                    // what a loop left alone eases down to
constexpr float kTiredSetting = kTiredFeedback / kMaxFeedback; // where FEEDBACK reads that 85%: at this or under, never touched
constexpr float kTiredRoom = 1e-4f;
constexpr float kFedLevel = 1e-4f;
constexpr float kHoldSeconds = 30.f;
constexpr float kTireSeconds = 2.f;
constexpr float kTireLeast = 2e-7f;
constexpr float kSettle = 0.012f;  // each of Gentle's two
constexpr float kInertia = 0.3f;   // seconds: how fast the line's speed follows TIME
constexpr double kSlowest = 0.5, kFastest = 2.0;
constexpr float kLowHz = 250.f, kSpan = 16.f; // TONE: 250 Hz to 4 kHz
constexpr float kBandK = 1.f / 0.7f;          // the band's 1 / Q
enum { kA1, kA2, kA3, kFeedback, kMix, kShapes };

float toneHz(float v) { return kLowHz * std::pow(kSpan, clamp01(v)); }

inline float tireStep(float tire, float to, float down, float up) {
    const float away = to - tire;
    float move = away * (away < 0.f ? down : up);
    if (std::fabs(move) < kTireLeast) move = away < 0.f ? -kTireLeast : kTireLeast;
    return std::fabs(move) >= std::fabs(away) ? to : tire + move;
}
float dubTire(float setting) {
    if (!(setting > kTiredSetting + kTiredRoom)) return 1.f;
    return kTiredFeedback / (kMaxFeedback * setting);
}

// The ceiling: straight for small sounds, flat at 1 from 1.5 up (TAPE's).
// (Under 1e-5 it is straight to a float's grain, and x cubed would be a number too small to hold.)
inline float soft(float x) {
    if (std::fabs(x) < 1e-5f) return x;
    x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
    return x - (4.f / 27.f) * x * x * x;
}

class Dub : public Unit {
public:
    Dub() : Unit(kDubInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        size_ = static_cast<int>(echoLongest(sampleRate)) + 16;
        line_.assign(static_cast<std::size_t>(size_), 0.f);
        hold_ = static_cast<long>(kHoldSeconds * sampleRate);
        changed();
        reset();
    }
    void reset() override {
        line_.assign(line_.size(), 0.f);
        band_.reset();
        at_ = 0;
        quiet_ = size_;
        asleep_ = true;
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        soundOnly(left, right, frames);
        float* const line = line_.data();
        const int size = size_;
        const float dry = bus_ ? 0.f : 1.f;
        const double pull = 1.0 / (kInertia * sr_);
        const float tireDown = 1.f / (kTireSeconds * sr_), tireUp = 1.f / (kSettle * sr_);
        const float tiredTo = dubTire(p_[1]);
        for (int i = 0; i < frames; ++i) {
            const float in = heardIn(0.5f * (heardIn(left[i]) + heardIn(right[i])));
            if (asleep_) {
                if (in == 0.f) {
                    if (bus_) left[i] = right[i] = 0.f;
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            shape_.next();
            // TIME glides: the line speeds up or slows down, never past double or half.
            float echo;
            if (delay_ == delayTo_) {
                int from = at_ - static_cast<int>(delay_);
                if (from < 0) from += size;
                echo = line[from];
            } else {
                const double away = delayTo_ - delay_;
                double move = away * pull;
                move = move > 1.0 - kSlowest ? 1.0 - kSlowest : (move < 1.0 - kFastest ? 1.0 - kFastest : move);
                delay_ = std::fabs(away) < 1e-3 ? delayTo_ : delay_ + move;
                const int whole = static_cast<int>(delay_);
                float w[4];
                cubicWeights(static_cast<float>(delay_ - whole), w);
                echo = tapRead(line, size, at_, whole, w);
            }
            // The band: at its top it passes the echo as it is (the band output times 1 / Q).
            float v1, v2;
            band_.step(shape_.now[kA1], shape_.now[kA2], shape_.now[kA3], echo, v1, v2);
            echo = kBandK * v1;
            // Left alone longer than the hold, a FEEDBACK over 85% eases down to it.
            starved_ = std::fabs(in) >= kFedLevel ? 0 : (starved_ < hold_ ? starved_ + 1 : hold_);
            const float tireTo = starved_ >= hold_ ? tiredTo : 1.f;
            if (tire_ != tireTo) tire_ = tireStep(tire_, tireTo, tireDown, tireUp);
            float back = in + soft(shape_.now[kFeedback] * tire_ * echo);
            back = std::fabs(back) < kQuiet ? 0.f : back;
            line[at_] = back;
            if (++at_ >= size) at_ = 0;
            quiet_ = back == 0.f ? (quiet_ < size ? quiet_ + 1 : size) : 0;
            const float mix = shape_.now[kMix];
            if (bus_) {
                left[i] = right[i] = mix * echo;
            } else if (mix != 0.f) {
                left[i] = left[i] * dry + mix * echo;
                right[i] = right[i] * dry + mix * echo;
            }
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        const long frames = echoTimeFrames(p_[0], tempo_, sr_);
        const long least = 8, most = size_ - 8;
        delayTo_ = static_cast<double>(frames < least ? least : (frames > most ? most : frames));
        feedback_.first.to = p_[1];
        tone_.first.to = p_[2];
        mix_.first.to = p_[3];
    }

private:
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&feedback_, &tone_, &mix_})
            if (s->moving()) s->tick(g);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        band_.settle();
        if (quiet_ >= size_ && band_.loudest() == 0.f) asleep_ = true; // the line is zeros from end to end, the band empty
    }
    void shape() {
        SvfShape s;
        s.set(svfG(toneHz(tone_.now()), sr_), kBandK);
        shape_.to[kA1] = s.a1, shape_.to[kA2] = s.a2, shape_.to[kA3] = s.a3;
        shape_.to[kFeedback] = kMaxFeedback * feedback_.now();
        shape_.to[kMix] = mix_.now();
    }
    void wake() {
        asleep_ = false;
        starved_ = 0;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&feedback_, &tone_, &mix_}) s->snap();
        delay_ = delayTo_;
        tire_ = 1.f;
        starved_ = 0;
        band_.reset();
        shape();
        shape_.snap();
    }

    std::vector<float> line_;
    int size_ = 0, at_ = 0, quiet_ = 0, tick_ = 0;
    long starved_ = 0, hold_ = 1;
    bool asleep_ = true;
    float tempo_ = 120.f, tire_ = 1.f;
    double delay_ = 18000.0, delayTo_ = 18000.0;
    Svf band_;
    Gentle feedback_, tone_, mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", echoTimeName(echoTimeChoice(v)));
    else if (index == 1) readPercent(v * kMaxFeedback, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Dub; }

float amount(int index, float v) { return index == 0 ? echoTimeSteps(v) : std::nanf(""); }

} // namespace

const EffectInfo kDubInfo = {
    "DUB", 4, {"TIME", "FEEDBACK", "TONE", "MIX"}, {0.5625f, 0.48f, 0.45f, 0.35f}, // a dotted eighth, FEEDBACK 72%, the band at 870 Hz
    "dub", EffectFamily::Delay, {kEchoTimes, 0, 0, 0}, reading, create,
    "", "DUB", amount,
};

float dubToneHz(float tone) { return toneHz(tone); }
float dubTireOf(float setting) { return dubTire(setting); }

} // namespace fx
} // namespace apotheca
