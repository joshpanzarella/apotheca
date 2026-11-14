// REVERSE: a TYPE of DELAY. Every echo is the sound played backwards: a hit
// comes back as a swell that rises into it, landing one TIME after the hit.
//
//   TIME      eight choices, in time with the song (TAPE's and PING's eight):
//             how long each backwards piece is, and how late its peak lands
//   FEEDBACK  0 to 100%, as CLEAN reads it, and does nine tenths of that. Each
//             trip turns the sound round again, so the echoes go backwards,
//             forwards, backwards...
//   MIX       how loud the echoes are beside the sound (which stays as it is)
//
// HOW: time is cut into WINDOWS one TIME long. Through each window the sound
// of the window before is played from its end to its start (sample n of a
// window is the sound 2n + 1 samples ago), so each window holds the last one
// backwards. Each window fades in and out over 4 ms (kFadeSeconds) so its
// edges never click. What comes out goes round again, times FEEDBACK.
//
// IN TIME: an effect is told the tempo, not where the bar is. When sound
// arrives after a rest its first window is taken to have begun half a TIME
// before: the sound's start then sits in the middle of a window, so its
// backwards echo has its peak (the hit's attack) exactly one TIME after the
// hit, with the hit's tail swelling up before it, and the next forwards echo
// two TIMEs after. A TIME turned (or a new tempo) is taken at the next
// window's start; the windows' fades keep that from clicking, so it needs no
// glide. FEEDBACK and MIX set off gently on the TICK (svf.hpp) and slide.
//
// It cannot ring for good: 0.9 at most round the loop, and the fades only
// take away. Nothing under kQuiet goes on a line; an exact zero does. Asleep
// once both lines are zeros from end to end: what wakes it finds a fresh one.
//
// ON A BUS: the dry sound is dropped and MIX is the level (Unit's `bus_`).

#include <initializer_list>
#include <vector>

#include "echo.hpp"
#include "glide.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kMostFeedback = 0.9f; // as CLEAN's and PING's: under 1, so a tail always ends
constexpr float kFadeSeconds = 0.004f;
constexpr float kSettle = 0.012f;
enum { kFeedback, kMix, kShapes };

class Reverse : public Unit {
public:
    Reverse() : Unit(kReverseInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        size_ = 2 * static_cast<int>(echoLongest(sampleRate)) + 16;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        fade_ = static_cast<int>(kFadeSeconds * sampleRate);
        if (fade_ < 1) fade_ = 1;
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
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
        float* const lineL = line_[0].data();
        float* const lineR = line_[1].data();
        const int size = size_;
        for (int i = 0; i < frames; ++i) {
            const float inL = heardIn(left[i]), inR = heardIn(right[i]);
            if (asleep_) {
                if (inL == 0.f && inR == 0.f) {
                    if (bus_) left[i] = right[i] = 0.f;
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            shape_.next();
            if (n_ >= window_) n_ = 0, window_ = windowTo_; // a new window: TIME as it is now
            // The window before, from its end to its start, faded at both edges.
            int from = at_ - (2 * n_ + 1);
            if (from < 0) from += size;
            const int edge = n_ < window_ - 1 - n_ ? n_ : window_ - 1 - n_;
            const float gain = edge >= fade_ ? 1.f : echoCurve((static_cast<float>(edge) + 0.5f) / static_cast<float>(fade_));
            const float echoL = gain * lineL[from], echoR = gain * lineR[from];
            const float feedback = shape_.now[kFeedback];
            float toL = inL + feedback * echoL, toR = inR + feedback * echoR;
            toL = std::fabs(toL) < kQuiet ? 0.f : toL;
            toR = std::fabs(toR) < kQuiet ? 0.f : toR;
            lineL[at_] = toL, lineR[at_] = toR;
            if (++at_ >= size) at_ = 0;
            ++n_;
            quiet_ = toL == 0.f && toR == 0.f ? (quiet_ < size ? quiet_ + 1 : size) : 0;
            const float mix = shape_.now[kMix];
            if (bus_) {
                left[i] = mix * echoL, right[i] = mix * echoR;
            } else if (mix != 0.f) {
                left[i] += mix * echoL, right[i] += mix * echoR;
            }
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        const long frames = echoTimeFrames(p_[0], tempo_, sr_);
        const long least = 4L * fade_, most = (size_ - 8) / 2;
        windowTo_ = static_cast<int>(frames < least ? least : (frames > most ? most : frames));
        feedback_.first.to = p_[1];
        mix_.first.to = p_[2];
    }

private:
    void beginTick() {
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&feedback_, &mix_})
            if (s->moving()) s->tick(g);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        if (quiet_ >= size_) asleep_ = true; // both lines are zeros from end to end
    }
    void shape() {
        shape_.to[kFeedback] = kMostFeedback * feedback_.now();
        shape_.to[kMix] = mix_.now();
    }
    void wake() {
        asleep_ = false;
        arrive();
    }
    void arrive() {
        for (Gentle* s : {&feedback_, &mix_}) s->snap();
        window_ = windowTo_;
        n_ = window_ / 2; // the sound that wakes it is in the middle of a window: its echo peaks one TIME later
        shape();
        shape_.snap();
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, quiet_ = 0, tick_ = 0, fade_ = 192;
    int window_ = 9000, windowTo_ = 9000, n_ = 0; // the window's length, the next one's, and where in it
    bool asleep_ = true;
    float tempo_ = 120.f;
    Gentle feedback_, mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", echoTimeName(echoTimeChoice(v)));
    else readPercent(v, out, size);
}

Effect* create() { return new Reverse; }

float amount(int index, float v) { return index == 0 ? echoTimeSteps(v) : std::nanf(""); }

} // namespace

const EffectInfo kReverseInfo = {
    "REVERSE", 3, {"TIME", "FEEDBACK", "MIX", ""}, {0.5625f, 0.3f, 0.4f, 0.f}, // a dotted eighth, FEEDBACK 30% (each echo 0.27 of the last)
    "reverse", EffectFamily::Delay, {kEchoTimes, 0, 0, 0}, reading, create,
    "", "REVERSE", amount,
};

} // namespace fx
} // namespace apotheca
