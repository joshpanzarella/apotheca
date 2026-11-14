// SPRING: a spring tank. Two springs of different lengths, and the "boing".
//
//   DRIP    how strong the chirp is: none (plain echoes) to full. In a spring
//           the high notes travel slower than the low ones, so a click comes
//           out as a short falling-in sweep, low first, and each time it has
//           been along the spring again the sweep is longer
//   DECAY   how long the tail lasts: 0.4 to 4 seconds, a real time (RT60,
//           the time to fall 60 dB), set by formula
//   TONE    low: dark, the highs die soon (at 0% a 3.5 kHz tail lasts two
//           thirds as long as a 1 kHz one). High: bright
//   MIX     from the sound alone to the springs alone
//
// How it is built:
//
//   in (left at one spring, right at the other) -> down to a fifth of the
//   rate -> low cut ----------------------------------------------+
//                                                                 |
//        +--(+)--> 80 chirp stages --+--> the spring's sound      |
//        |   ^                       |                            |
//        |   +--- in <---------------|----------------------------+
//        +-- damped, turned down for DECAY, a little of the other
//            spring mixed in <------ the spring's length (a delay)
//
//   the two springs' sounds -> back up to the full rate -> left and right,
//   each side mostly one spring and some of the other
//
// The numbers, all pinned by tests/effects_test.cpp:
//   - THE SPRINGS RUN AT A FIFTH OF THE SAMPLE RATE (9600 Hz at 48 kHz). A
//     spring passes nothing much over 4 to 5 kHz, and a chirp stage's delay is
//     longest at the top of ITS rate: so the top of the spring's rate is put
//     where the spring's sound ends, and the sweep is in what is heard. It is
//     also a fifth of the work. Down and up are each a sixth-order low-pass
//     (Chebyshev, half a dB of ripple) at 0.42 of the springs' rate: 4.03 kHz.
//   - A chirp stage is the simplest all-pass there is (one multiply's worth of
//     memory). All 80 a spring has share one number, 0 at DRIP none (each is
//     then one sample of delay) to 0.6 at DRIP full. They are in the loop: the
//     sound goes through them again on every trip.
//   - The springs are 33.1 and 41.3 ms long, to the nearest sample of their
//     rate, and each gets 0.6 of the other's sound back in beside 0.8 of its
//     own (a turn: energy in is energy out), so the echoes soon stop being
//     counted.
//   - Each trip is turned down by exp(-6.908 x its time / DECAY), its time
//     taken at 1 kHz: that is what makes DECAY a real time.
//   - TONE: above a corner of 1.5 kHz the tail lasts 0.2 times as long at
//     TONE none, 0.85 times at full, and in between by a fixed ratio a step.
//     The corner stays put and TONE sets how far the highs are cut, so every
//     step darker is darker. (The corner was once swept from 400 Hz to
//     4.8 kHz instead: at TONE none the whole spring band was above it, all of
//     it died sooner alike, and 0% came out brighter than 50%.) At full the
//     highs are still cut a little, since they take longer a trip than the
//     1 kHz DECAY is reckoned at: cut not at all, they outlast DECAY by a fifth.
//   - Low cut: 100 Hz, 12 dB an octave, once, on the way in.
//   - The sound going in is turned down for a long DECAY (by the fourth root
//     of what a trip loses), so a held sound makes about as loud a tail at
//     any DECAY.
//   - The left goes in at one spring and the right at the other. A sound the
//     same on both sides goes into both alike (as into a tank with one way
//     in), and what comes out differs a little side to side. (Taken in as one,
//     left and right added, a sound opposite on the two sides put nothing in.)
//
// NUMBERS TOO SMALL TO HOLD: see kFloor. IT SLEEPS when the springs have held
// nothing over kQuiet for a quarter of a second with nothing coming in, on
// that SAMPLE (not at a block's edge), and wakes on the sample sound arrives
// as one fresh from the box: every memory cleared. So the sound is the same
// to the bit however it is cut into blocks.
//
// ON A BUS (the plan's sends): the dry sound is dropped and MIX is the level.
// That is Unit's `bus_`, set by the "you are on a bus" call (setOnBus, which
// also calls changed()).

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kInnerHz = 9600.f;             // the springs' own rate, as near as a whole division of the sample rate gets
constexpr int kStages = 40;                    // chirp stages in a row, and there are four rows: each spring's 80 in two halves
constexpr float kMostChirp = 0.6f;             // a stage's number at DRIP full
constexpr float kLineMs[2] = {33.1f, 41.3f};   // the two springs
constexpr float kOwn = 0.8f, kOthers = 0.6f;   // what a spring gets back of itself and of the other (0.64 + 0.36 = 1)
constexpr float kShortest = 0.4f, kLongest = 4.f; // DECAY, seconds
constexpr float kTripHz = 1000.f;              // where a trip's time is taken for DECAY
constexpr float kHighsLast = 0.2f, kHighsLastBright = 0.85f; // above TONE's corner the tail lasts this much as long: at TONE none, and at TONE full
constexpr float kToneHz = 1500.f;              // TONE's corner
constexpr float kLowCutHz = 100.f;
constexpr float kPass = 0.42f;                 // the way down and the way up pass this much of the springs' rate
constexpr double kRipple = 0.5;                // dB
constexpr int kCuts = 3;                       // second-order pieces in each of them
constexpr float kSettle = 0.03f;               // seconds for a turned setting to arrive
constexpr float kDriveIn = 0.5f;               // how hard the springs are fed
constexpr float kNear = 1.3f, kFar = 0.4f;     // each side: this much of its own spring and of the other
// NUMBERS TOO SMALL TO HOLD are slow, and anything quick that is left alone
// fades straight into them. So the way down and the low cut are each fed this
// much with the sound (360 dB down): they come to rest on it, not on nothing,
// and sound coming in under kNothing is none. The chirp stages start on it
// (see clear()). The springs themselves need none: they fade no faster than
// DECAY says, and are asleep a quarter of a second after they pass kQuiet,
// long before they could get there. Then no sum in here comes out too small
// to hold, with or without the processor's flush-to-zero. See glide.hpp,
// GOING TO SLEEP.
constexpr float kFloor = 1e-18f;

float onePole(float hz, float rate) { return 1.f - std::exp(-6.2831853f * (hz > 0.45f * rate ? 0.45f * rate : hz) / rate); }
float decaySeconds(float setting) { return kShortest * std::pow(kLongest / kShortest, clamp01(setting)); }

// One second-order piece of a low-pass whose two zeros are at the top of the rate: b1 is 2 b0 and b2 is b0.
struct Cut {
    float b0 = 0.f, a1 = 0.f, a2 = 0.f;
};
struct CutMemory {
    float s1 = 0.f, s2 = 0.f;
};
inline float cut(const Cut& c, CutMemory& m, float x) {
    const float bx = c.b0 * x, y = bx + m.s1;
    m.s1 = bx + bx - c.a1 * y + m.s2;
    m.s2 = bx - c.a2 * y;
    return y;
}

class Spring : public Unit {
public:
    Spring() : Unit(kSpringInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        every_ = static_cast<int>(std::lround(sampleRate / kInnerHz));
        every_ = every_ < 1 ? 1 : every_;
        rate_ = sampleRate / static_cast<float>(every_);
        int most = 0;
        for (int i = 0; i < 2; ++i) {
            len_[i] = static_cast<int>(std::lround(kLineMs[i] * 1e-3f * rate_));
            len_[i] = len_[i] < 1 ? 1 : len_[i];
            most = len_[i] > most ? len_[i] : most;
        }
        int size = 64;
        while (size < most + 1) size *= 2;
        mask_ = size - 1;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size), 0.f);
        // The way down and the way up: a Chebyshev low-pass, as three second-order pieces.
        const double ripple = std::sqrt(std::pow(10.0, kRipple / 10.0) - 1.0), u = std::asinh(1.0 / ripple) / (2.0 * kCuts);
        const double k = std::tan(3.141592653589793 * static_cast<double>(kPass) / every_);
        for (int i = 0; i < kCuts; ++i) {
            const double angle = 3.141592653589793 * (2.0 * i + 1.0) / (4.0 * kCuts);
            const double re = -std::sinh(u) * std::sin(angle), im = std::cosh(u) * std::cos(angle);
            const double b = -2.0 * re, c = re * re + im * im, a0 = 1.0 + b * k + c * k * k;
            cut_[i].b0 = static_cast<float>(c * k * k / a0);
            cut_[i].a1 = static_cast<float>(2.0 * (c * k * k - 1.0) / a0);
            cut_[i].a2 = static_cast<float>((1.0 - b * k + c * k * k) / a0);
        }
        lowCut_ = onePole(kLowCutHz, rate_);
        highCut_ = onePole(kToneHz, rate_);
        restAfter_ = static_cast<int>(0.25f * sampleRate); // longer than a spring, its stages and the ways down and up together
        changed();
        reset();
    }
    void reset() override {
        clear();
        quiet_ = 0;
        asleep_ = true; // nothing in the springs
        arrive();
    }
    // Asleep and awake by turns, each change on its own SAMPLE and not at a block's edge: the
    // sound is then the same to the bit however it is cut into blocks.
    void process(float* left, float* right, int frames) override {
        if (line_[0].empty() || frames <= 0) return;
        soundOnly(left, right, frames);
        while (frames > 0) {
            int n = 0;
            if (asleep_) {
                while (n < frames && !(std::fabs(left[n]) >= kNothing) && !(std::fabs(right[n]) >= kNothing)) ++n;
                // The springs are still, and nothing is coming in: the sound as it is at MIX 0, else none.
                if (dry_.to != 1.f)
                    for (int i = 0; i < n; ++i) left[i] = right[i] = 0.f;
                if (n < frames) wake(); // at this sample
            } else {
                n = restAfter_ - quiet_ < frames ? restAfter_ - quiet_ : frames; // no further than where it could fall asleep
                run(left, right, n);
                if (quiet_ >= restAfter_) asleep_ = true;
            }
            left += n, right += n, frames -= n;
        }
    }

protected:
    void changed() override {
        drip_.to = p_[0];
        decay_.to = p_[1];
        tone_.to = p_[2];
        // MIX: the sound and the springs swap over at equal power (they are unrelated sounds).
        const float mix = p_[3];
        dry_.to = bus_ ? 0.f : std::cos(1.5707963f * mix);
        wet_.to = bus_ ? mix : std::sin(1.5707963f * mix);
        if (mix >= 1.f && !bus_) dry_.to = 0.f, wet_.to = 1.f; // the springs alone, exactly (and at 0 cos is 1 and sin is 0: the sound untouched, to the bit)
    }

private:
    // What follows from DRIP, DECAY and TONE: a stage's number, each spring's share of a trip, the damping, how hard each is fed.
    struct Follows {
        float chirp = 0.f, feed = 0.f, gain[2] = {}, highs[2] = {};
    };
    void follow() {
        const float a = kMostChirp * drip_.now, seconds = decaySeconds(decay_.now);
        to_.chirp = a;
        // How long a stage holds a 1 kHz sound, in samples of the springs' rate (1 at DRIP none, less as it is turned up: the lows hurry).
        const float held = (1.f - a * a) / (1.f + 2.f * a * std::cos(6.2831853f * kTripHz / rate_) + a * a);
        const float last = std::pow(kHighsLast, 1.f - tone_.now) * std::pow(kHighsLastBright, tone_.now);
        for (int i = 0; i < 2; ++i) {
            const float trip = -6.9077553f * (static_cast<float>(len_[i] + 1) + 2.f * kStages * held) / (rate_ * seconds); // the natural log of what one trip leaves
            to_.gain[i] = std::exp(trip);
            to_.highs[i] = std::exp(trip * (1.f / last - 1.f));
        }
        // Both are fed alike, and less for a long DECAY (by the fourth root of what a trip of the two together loses).
        to_.feed = kDriveIn * std::sqrt(std::sqrt(1.f - to_.gain[0] * to_.gain[1]));
    }
    // Everything that glides is where it was turned to, at once.
    void arrive() {
        drip_.snap(), decay_.snap(), tone_.snap(), dry_.snap(), wet_.snap();
        follow();
        now_ = to_;
    }
    // Every memory emptied (no allocation: the audio thread calls this when it wakes).
    void clear() {
        for (auto& line : line_)
            for (float& v : line) v = 0.f;
        // (The stages start on the floor, not on nothing: a sound's first trip through 80 empty ones would
        // come out as itself times a stage's number 80 times over, which is a number too small to hold.)
        for (auto& stage : stage_)
            for (float& v : stage) v = kFloor;
        for (auto& side : down_)
            for (CutMemory& m : side) m = CutMemory();
        for (auto& side : up_)
            for (CutMemory& m : side) m = CutMemory();
        low_[0] = low_[1] = low_[2] = low_[3] = high_[0] = high_[1] = half_[0] = half_[1] = 0.f;
        at_ = 0, phase_ = 0;
        calm_ = false;
    }
    void wake() {
        clear(); // what is left in the springs is under 240 dB down: gone, so that it starts as one fresh from the box does, to the bit
        asleep_ = false;
        quiet_ = 0;
        arrive(); // nothing is sounding: whatever was turned meanwhile is simply there
    }
    // A stretch of the springs sounding.
    void run(float* left, float* right, int frames) {
        // Settings that were turned slide to where they are going; what follows from them is worked out again while they do.
        const float k = glideK(frames, kSettle, sr_), per = 1.f / static_cast<float>(frames);
        bool moved = false;
        if (drip_.moving()) drip_.begin(frames, k), drip_.done(), moved = true;
        if (decay_.moving()) decay_.begin(frames, k), decay_.done(), moved = true;
        if (tone_.moving()) tone_.begin(frames, k), tone_.done(), moved = true;
        if (moved) follow();
        const bool gliding = moved || now_.chirp != to_.chirp || now_.gain[0] != to_.gain[0] || now_.gain[1] != to_.gain[1] || now_.highs[0] != to_.highs[0] ||
                             now_.highs[1] != to_.highs[1]; // (a feed moves only with its gain)
        Follows v = now_, d;
        if (gliding) {
            d.chirp = (to_.chirp - v.chirp) * per, d.feed = (to_.feed - v.feed) * per;
            for (int i = 0; i < 2; ++i) d.gain[i] = (to_.gain[i] - v.gain[i]) * per, d.highs[i] = (to_.highs[i] - v.highs[i]) * per;
        }
        const float dDry = dry_.begin(frames, k), dWet = wet_.begin(frames, k);
        float dry = dry_.now, wet = wet_.now;
        float* const lineA = line_[0].data();
        float* const lineB = line_[1].data();
        const int mask = mask_, lenA = len_[0], lenB = len_[1], every = every_;
        const float lowCut = lowCut_, highCut = highCut_, back = static_cast<float>(every_);
        const Cut c0 = cut_[0], c1 = cut_[1], c2 = cut_[2];
        CutMemory downL[kCuts], downR[kCuts], upA[kCuts], upB[kCuts];
        for (int i = 0; i < kCuts; ++i) downL[i] = down_[0][i], downR[i] = down_[1][i], upA[i] = up_[0][i], upB[i] = up_[1][i];
        int at = at_, phase = phase_, quiet = quiet_;
        bool calm = calm_;
        for (int n = 0; n < frames; ++n) {
            dry += dDry, wet += dWet;
            if (gliding) {
                v.chirp += d.chirp, v.feed += d.feed;
                v.gain[0] += d.gain[0], v.gain[1] += d.gain[1], v.highs[0] += d.highs[0], v.highs[1] += d.highs[1];
            }
            // (Sound too faint to be sound is none: see kFloor.)
            const bool nothing = std::fabs(left[n]) < kNothing && std::fabs(right[n]) < kNothing;
            const float inL = std::fabs(left[n]) < kNothing ? 0.f : left[n], inR = std::fabs(right[n]) < kNothing ? 0.f : right[n];
            // Down to the springs' rate: everything they cannot carry is taken off first.
            const float lowL = cut(c2, downL[2], cut(c1, downL[1], cut(c0, downL[0], inL + kFloor)));
            const float lowR = cut(c2, downR[2], cut(c1, downR[1], cut(c0, downR[0], inR + kFloor)));
            float springA = 0.f, springB = 0.f;
            if (phase == 0) { // one sample of the springs
                // The low cut, twice (the first takes the floor out, so the second is fed it again).
                low_[0] += lowCut * (lowL - low_[0]);
                low_[2] += lowCut * (lowR - low_[2]);
                const float onceL = lowL - low_[0] + kFloor, onceR = lowR - low_[2] + kFloor;
                low_[1] += lowCut * (onceL - low_[1]);
                low_[3] += lowCut * (onceR - low_[3]);
                const float inA = onceL - low_[1], inB = onceR - low_[3];
                // What comes off the end of each spring, damped and turned down for its trip...
                const float endA = lineA[(at - lenA) & mask], endB = lineB[(at - lenB) & mask];
                high_[0] += highCut * (endA - high_[0]);
                high_[1] += highCut * (endB - high_[1]);
                const float backA = (high_[0] + v.highs[0] * (endA - high_[0])) * v.gain[0], backB = (high_[1] + v.highs[1] * (endB - high_[1])) * v.gain[1];
                // ...goes back in at the start with what is arriving (the left at one spring, the right at the
                // other), and a little of the other spring's.
                float x0 = inA * v.feed + kOwn * backA + kOthers * backB, x1 = inB * v.feed + kOwn * backB - kOthers * backA;
                // The chirp stages. Each spring's 80 are two rows of 40, the second a sample behind the
                // first, so that four short rows are worked at once instead of two long ones.
                float x2 = half_[0], x3 = half_[1];
                const float a = v.chirp;
                for (int s = 0; s < kStages; ++s) {
                    float* const m = stage_[s];
                    const float y0 = a * x0 + m[0], y1 = a * x1 + m[1], y2 = a * x2 + m[2], y3 = a * x3 + m[3];
                    m[0] = x0 - a * y0, m[1] = x1 - a * y1, m[2] = x2 - a * y2, m[3] = x3 - a * y3;
                    x0 = y0, x1 = y1, x2 = y2, x3 = y3;
                }
                half_[0] = x0, half_[1] = x1;
                lineA[at & mask] = x2;
                lineB[at & mask] = x3;
                at = (at + 1) & mask;
                calm = std::fabs(x2) < kQuiet && std::fabs(x3) < kQuiet;
                springA = x2 * back, springB = x3 * back; // (back up: one sample in `every`, so that much louder)
            }
            if (++phase >= every) phase = 0;
            // Back up to the full rate.
            const float a = cut(c2, upA[2], cut(c1, upA[1], cut(c0, upA[0], springA))), b = cut(c2, upB[2], cut(c1, upB[1], cut(c0, upB[0], springB)));
            left[n] = inL * dry + wet * (kNear * a + kFar * b);
            right[n] = inR * dry + wet * (kFar * a + kNear * b);
            // Asleep once the springs have held nothing worth the name, with nothing coming in, for longer than they remember.
            quiet = calm && nothing ? quiet + 1 : 0;
        }
        for (int i = 0; i < kCuts; ++i) down_[0][i] = downL[i], down_[1][i] = downR[i], up_[0][i] = upA[i], up_[1][i] = upB[i];
        at_ = at, phase_ = phase, quiet_ = quiet;
        calm_ = calm;
        dry_.done(), wet_.done();
        now_ = to_;
    }

    std::vector<float> line_[2];
    float stage_[kStages][4] = {};
    Cut cut_[kCuts];
    CutMemory down_[2][kCuts], up_[2][kCuts];
    float low_[4] = {}, high_[2] = {}, half_[2] = {};
    int len_[2] = {1, 1}, mask_ = 0, at_ = 0, every_ = 5, phase_ = 0, quiet_ = 0, restAfter_ = 12000;
    bool asleep_ = true, calm_ = false;
    float rate_ = 9600.f, lowCut_ = 0.f, highCut_ = 1.f;
    Glide drip_, decay_, tone_, dry_, wet_;
    Follows now_, to_;
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index != 1) return readPercent(v, out, size);
    std::snprintf(out, static_cast<std::size_t>(size), "%.1f S", static_cast<double>(decaySeconds(v)));
}

Effect* create() { return new Spring; }

} // namespace

const EffectInfo kSpringInfo = {
    "SPRING", 4, {"DRIP", "DECAY", "TONE", "MIX"}, {0.7f, 0.6f, 0.5f, 0.3f}, // 1.6 seconds
    "spring", EffectFamily::Reverb, {0, 0, 0, 0}, reading, create,
    "", "SPRING",
};

} // namespace fx
} // namespace apotheca
