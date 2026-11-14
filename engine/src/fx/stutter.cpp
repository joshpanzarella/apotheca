// STUTTER: a TYPE of DELAY. Now and then, by chance, it catches a slice of
// the sound and plays it again and again, in time with the song: the
// retrigger of a drum-and-bass break, a vocal's "n-n-n-no".
//
//   LENGTH   how long a slice is: eight note lengths, 1/32 to 1/2, in time
//   REPEATS  how many times the slice is heard, the first included: 2, 3, 4,
//            6 or 8. A stutter fills that many slices, then the sound goes on
//   CHANCE   how likely each place is to stutter: 0 never, 100% every time
//   MIX      0 is the sound untouched; 100% the slice in its place
//
// HOW: time is cut into PLACES, each REPEATS slices long, counted from when
// sound arrives after a rest (4 s of nothing). A slice is rarely a whole
// number of samples (133 BPM's sixteenth is 5413.53), so each edge is the
// whole sample at or before where it falls EXACTLY, counted from waking
// (`beginSlice`), never a sum of rounded slices: those ran half a sample a
// slice late and in 10 s the offbeat hit was the one caught. At the start of each place the
// dice are rolled (a roll for that place's number, never a generator walked:
// the same sound after a rest gets the same stutters, in a bounce as live).
// On a hit the first slice plays as it is and is caught, and the rest of the
// place plays it again. Each splice is a 4 ms crossfade (kFadeSeconds) that
// ENDS on a slice's edge: into the slice (through the 4 ms that came just
// before it), round its loop, and back to the sound. So the slice's start, a
// hit's attack, is always heard whole, every time.
// LENGTH and REPEATS are taken at the start of a place; CHANCE when it rolls;
// MIX sets off gently on the TICK (svf.hpp) and slides.
//
// IN TIME, NOT ON THE BEAT: an effect is told the tempo, not where the bar is,
// so the places start where the first sound after a rest is. A drum loop that
// starts on its first beat has its places on the beat.
//
// ON THE ECHO BUS it gives back only the repeats (wet only, as every delay
// there): what was sent is heard once from its track, and when the dice say
// so its slice comes back REPEATS - 1 more times right after it, as loud as
// it went in, and then stops. A THROW of a hit there is a hit repeated
// exactly, a fixed number of times: a "throw" that does not die away. The
// dice are the bus's, for everything sent to it together.
//
// It holds nothing longer than a place (REPEATS slices: 4 s at 1/2 x 8 at
// 120 BPM, 24 s at 40 BPM), so its tail always ends: it has no feedback.

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "echo.hpp"
#include "glide.hpp"
#include "mod.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kLengths = 8;
constexpr int kSixths[kLengths] = {3, 4, 6, 8, 12, 16, 24, 48}; // a slice in sixths of a step
const char* const kLengthNames[kLengths] = {"1/32", "1/16 TRIP", "1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/4", "1/2"};
constexpr int kRepeatChoices = 5;
constexpr int kRepeats[kRepeatChoices] = {2, 3, 4, 6, 8};
constexpr float kFadeSeconds = 0.004f;
constexpr float kSettle = 0.012f;
constexpr std::uint32_t kDice = 0x5747u; // what the roll is for

int lengthOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kLengths);
    return i > kLengths - 1 ? kLengths - 1 : i;
}
int repeatsOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kRepeatChoices);
    return kRepeats[i > kRepeatChoices - 1 ? kRepeatChoices - 1 : i];
}
double sliceFrames(float length, float tempo, float rate) { return kSixths[lengthOf(length)] * 2.5 / tempo * rate; } // (not whole: 133 BPM's sixteenth is 5413.53)

class Stutter : public Unit {
public:
    Stutter() : Unit(kStutterInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        fade_ = static_cast<int>(kFadeSeconds * sampleRate);
        if (fade_ < 1) fade_ = 1;
        size_ = static_cast<int>(std::lround(kSixths[kLengths - 1] * 2.5 / 40.0 * sampleRate)) + fade_ + 8;
        for (auto& c : caught_) c.assign(static_cast<std::size_t>(size_), 0.f);
        for (auto& p : pre_) p.assign(static_cast<std::size_t>(fade_), 0.f);
        restAfter_ = static_cast<long>(kEchoRestSeconds * sampleRate);
        changed();
        reset();
    }
    void reset() override {
        for (auto& c : caught_) c.assign(c.size(), 0.f);
        asleep_ = true;
        stutter_ = false;
        quietIn_ = 0;
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        soundOnly(left, right, frames);
        float* const cL = caught_[0].data();
        float* const cR = caught_[1].data();
        for (int i = 0; i < frames; ++i) {
            const float inL = heardIn(left[i]), inR = heardIn(right[i]);
            const bool none = inL == 0.f && inR == 0.f;
            if (asleep_) {
                if (none) {
                    if (bus_) left[i] = right[i] = 0.f;
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            shape_.next();
            if (n_ == 0 && slice_ == 0) beginPlace();
            if (n_ == 0) beginSlice();
            // What the stutter plays (`repL`, `repR`), and how much of the sound it takes the place of (`a`).
            // Every splice ENDS on a slice's edge, so a slice's first sample (a hit's attack) is always heard whole.
            float repL = 0.f, repR = 0.f, a = 0.f;
            bool playing = false;
            const int last = length_ - fade_; // where a slice's last fade begins
            if (stutter_) {
                float w = 0.f;
                const int m = n_ - last;
                if (m >= 0) w = echoCurve((static_cast<float>(m) + 0.5f) / static_cast<float>(fade_));
                if (slice_ == 1 && n_ == 0) cL[fade_ + first_] = inL, cR[fade_ + first_] = inR; // a sample past the slice: a repeat a sample longer than it plays it
                if (slice_ == 0) {
                    cL[fade_ + n_] = inL, cR[fade_ + n_] = inR; // the slice, caught as it plays
                    if (m >= 0 && repeats_ > 1) {
                        repL = w * cL[m], repR = w * cR[m]; // into what came just before the slice, so its start comes in whole
                        a = w;
                        playing = true;
                    }
                } else {
                    playing = true;
                    repL = cL[fade_ + n_], repR = cR[fade_ + n_], a = 1.f;
                    if (m >= 0) {
                        repL *= 1.f - w, repR *= 1.f - w;
                        if (slice_ + 1 < repeats_) {
                            repL += w * cL[m], repR += w * cR[m]; // round the loop
                        } else {
                            a = 1.f - w; // the last: back to the sound
                        }
                    }
                }
            }
            pre_[0][preAt_] = inL, pre_[1][preAt_] = inR;
            if (++preAt_ >= fade_) preAt_ = 0;
            const float mix = shape_.now[kMixAt];
            if (bus_) {
                left[i] = playing ? mix * repL : 0.f;
                right[i] = playing ? mix * repR : 0.f;
            } else if (playing && mix != 0.f) {
                const float keep = 1.f - mix * a; // (at MIX 100% in the slice's place: 0, and the slice exactly)
                left[i] = inL * keep + mix * repL;
                right[i] = inR * keep + mix * repR;
            }
            if (++n_ >= length_) {
                n_ = 0;
                if (++slice_ >= repeats_) {
                    slice_ = 0;
                    stutter_ = false;
                    ++place_;
                    placeAt_ += repeats_ * exact_;
                }
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override { mix_.first.to = p_[3]; }

private:
    enum { kMixAt, kShapes };
    void beginPlace() {
        const double frames = sliceFrames(p_[0], tempo_, sr_);
        const double least = 3.0 * fade_, most = size_ - fade_ - 6; // (a slice holds at least three fades)
        exact_ = frames < least ? least : (frames > most ? most : frames);
        repeats_ = repeatsOf(p_[1]);
        stutter_ = modRoll(place_, kDice) < p_[2];
        if (stutter_) // what came just before the slice, oldest first: the splices into its start go through it
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < fade_; ++k) caught_[c][static_cast<std::size_t>(k)] = pre_[c][static_cast<std::size_t>((preAt_ + k) % fade_)];
    }
    // A slice's edges are counted from where it woke, each to the whole sample at or before where it falls exactly,
    // so the places never drift off the song's steps (the engine starts a step on the first sample at or after its
    // time: a hit is on its slice's first sample, or a sample or two after it, never before). Slices of one place
    // differ by a sample at most.
    void beginSlice() {
        const long from = static_cast<long>(std::floor(placeAt_ + slice_ * exact_)), to = static_cast<long>(std::floor(placeAt_ + (slice_ + 1) * exact_));
        length_ = static_cast<int>(to - from);
        if (slice_ == 0) first_ = length_;
    }
    void beginTick() {
        if (mix_.moving()) mix_.tick(glideK(kTick, kSettle, sr_));
        shape_.to[kMixAt] = mix_.now();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        shape_.land();
        if (quietIn_ >= restAfter_ && !stutter_) asleep_ = true; // nothing came in for 4 s, and nothing is held
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        mix_.snap();
        place_ = 0, slice_ = 0, n_ = 0;
        placeAt_ = 0.0;
        stutter_ = false;
        preAt_ = 0;
        for (auto& p : pre_) p.assign(p.size(), 0.f);
        shape_.to[kMixAt] = mix_.now();
        shape_.snap();
    }

    std::vector<float> caught_[2]; // a fade of what came before the slice, then the slice
    std::vector<float> pre_[2];    // the last fade's worth of sound, a ring
    int preAt_ = 0;
    int size_ = 0, fade_ = 192, tick_ = 0;
    int length_ = 6000, first_ = 6000, repeats_ = 4, slice_ = 0, n_ = 0;
    double exact_ = 6000.0, placeAt_ = 0.0; // a slice, exactly; where this place began, exactly, in samples since it woke
    std::uint32_t place_ = 0; // places since it woke
    long quietIn_ = 0, restAfter_ = 1;
    bool asleep_ = true, stutter_ = false;
    float tempo_ = 120.f;
    Gentle mix_;
    Slide<kShapes> shape_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", kLengthNames[lengthOf(v)]);
    else if (index == 1) std::snprintf(out, static_cast<std::size_t>(size), "%d TIMES", repeatsOf(v));
    else readPercent(v, out, size);
}

Effect* create() { return new Stutter; }

} // namespace

const EffectInfo kStutterInfo = {
    "STUTTER", 4, {"LENGTH", "REPEATS", "CHANCE", "MIX"}, {0.3125f, 0.5f, 0.5f, 1.f}, // a sixteenth, four times, half the places, all of it
    "stutter", EffectFamily::Delay, {kLengths, kRepeatChoices, 0, 0}, reading, create,
    "", "STUTTER",
};

float stutterRoll(std::uint32_t place) { return modRoll(place, kDice); }

} // namespace fx
} // namespace apotheca
