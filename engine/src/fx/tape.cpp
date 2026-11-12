// TAPE: a dub delay. Each repeat is duller, thinner and a little bent.
//
//   TIME      eight choices, in time with the song (kThirds below)
//   FEEDBACK  0 to 120%: how much of each echo goes round again. Past 100% it
//             howls, and holds there: the loop has a ceiling. Left alone, a
//             FEEDBACK over 90% falls back to 90% (IT ALWAYS ENDS, below)
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
// IT ALWAYS ENDS (owner, 2026-11-12: "there should be a cap"). FEEDBACK at
// 100% never died away and past it the howl held for good: a send bus with
// it never came to rest, and a bounce would never have ended. So:
//   - While sound is coming in (anything over kFedLevel, 80 dB under full),
//     and for kHoldSeconds (30) after the last of it, FEEDBACK is what it
//     reads. It builds, howls and sings exactly as it did.
//   - Left alone longer than that, the loop tires: over a couple of seconds
//     (counted and eased a sample at a time: the same however it is cut
//     into blocks) its FEEDBACK eases down to kTiredFeedback (90%, the most the old DELAY
//     has), if it was set higher. From there each echo is under nine tenths
//     of the last and the tail dies away. Sound coming in again wakes it:
//     FEEDBACK is back where it reads within kSettle.
//   - A FEEDBACK that reads 90% or less is never touched, to the bit (tapeTire).
//   - That is one TAPE. Two in a row each count from the last sound coming IN,
//     so a howling one keeps the next fed; a bounce's cap is what ends that.
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

// TIME in thirds of a step (a step is a sixteenth), so that triplets are whole.
constexpr int kTimes = 8;
constexpr int kThirds[kTimes] = {3, 4, 6, 8, 9, 12, 18, 24};
const char* const kTimeNames[kTimes] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/4 DOT", "1/2"};
int timeChoice(float v) {
    const int index = static_cast<int>(clamp01(v) * kTimes);
    return index > kTimes - 1 ? kTimes - 1 : index;
}

constexpr float kMaxFeedback = 1.2f;
constexpr float kFedLevel = 1e-4f;      // sound coming in under this is not feeding the loop
constexpr float kHoldSeconds = 30.f;    // how long after the last sound came in FEEDBACK stays what it reads
constexpr float kTiredFeedback = 0.9f;  // ...and what it is held to after that (as FEEDBACK reads: 90%)
constexpr float kTiredSetting = 0.75f;  // where FEEDBACK reads that 90%: at this or under, the loop is never touched
constexpr float kTiredRoom = 1e-4f;     // (a setting got to by sums can be a float off: it still reads 90%)
constexpr float kTireSeconds = 2.f;     // how gently it gets there
constexpr float kTireLeast = 2e-7f;     // ...and never slower than this a sample, so it arrives (as a Glide does)
constexpr float kWowHz = 0.8f;
constexpr float kWowSeconds = 0.0015f; // either way, at AGE full
constexpr int kWowEvery = 64;          // samples between two points of the wow's sine (straight lines between)
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

// One sample of the tiring, or of the waking: a fraction of what is left (`down` when tiring,
// gently; `up` when fed again, at once), never slower than kTireLeast, so that it LANDS on `to`.
inline float tireStep(float tire, float to, float down, float up) {
    const float away = to - tire;
    float move = away * (away < 0.f ? down : up);
    if (std::fabs(move) < kTireLeast) move = away < 0.f ? -kTireLeast : kTireLeast;
    return std::fabs(move) >= std::fabs(away) ? to : tire + move;
}

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
        wow_ = wowStep_ = 0.f, wowLeft_ = 0;
        quiet_ = 0;
        asleep_ = true; // nothing on the tape (and when sound wakes it, the count of how long it has been left alone starts again)
        arrive();
    }
    void setTempo(float bpm) override {
        if (bpm >= 20.f && bpm <= 400.f) tempo_ = bpm;
        changed();
    }
    // Asleep and awake by turns, each change on its own SAMPLE and not at a block's edge: the
    // sound is then the same to the bit however it is cut into blocks. (Asleep at the end of the
    // call it was in, the last breath of a tail, 1e-23 or so, was a few samples longer or shorter.)
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
                if (quiet_ >= size_) asleep_ = true;
            }
            left += n, right += n, frames -= n;
        }
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
    void wake() {
        asleep_ = false;
        quiet_ = 0;
        starved_ = 0;
        phase_ = 0.0, wow_ = wowStep_ = 0.f, wowLeft_ = 0; // an empty tape starts as one fresh from the box does, wow and all
        arrive();                 // nothing is sounding: whatever was turned meanwhile is simply there
    }
    // A stretch of the tape running.
    void run(float* left, float* right, int frames) {
        const float k = glideK(frames, kSettle, sr_);
        // Fed, or left alone? Alone for longer than the hold, a FEEDBACK over kTiredFeedback is eased
        // down to it. Counted and moved SAMPLE BY SAMPLE (in the loop below), so that the sound is
        // the same to the bit however it is cut into blocks: counted a block at a time, a bounce and
        // live play came apart 30 s into a tired tail.
        const long hold = static_cast<long>(kHoldSeconds * sr_);
        const float tiredTo = tapeTire(p_[1]);
        const float tireDown = 1.f / (kTireSeconds * sr_), tireUp = 1.f / (kSettle * sr_); // (as tapeTireStep has them)
        long starved = starved_;
        int quiet = quiet_;
        const float dFeedback = feedback_.begin(frames, k), dPush = push_.begin(frames, k), dCeiling = ceiling_.begin(frames, k);
        const float dHigh = highCut_.begin(frames, k), dLow = lowCut_.begin(frames, k), dDepth = depth_.begin(frames, k), dMix = mix_.begin(frames, k);
        float tire = tire_;
        float feedback = feedback_.now, push = push_.now, ceiling = ceiling_.now, highCut = highCut_.now, lowCut = lowCut_.now, depth = depth_.now, mix = mix_.now;
        // The wow: a slow sine, worked out every kWowEvery samples the tape has run (NOT at each
        // end of a block: its shape was then the block's) and joined by straight lines.
        float wow = wow_, dWow = wowStep_;
        int wowLeft = wowLeft_;
        const float dry = bus_ ? 0.f : 1.f;
        const double pull = 1.0 / (kTapeInertia * sr_);
        float* const tapeL = line_[0].data();
        float* const tapeR = line_[1].data();
        float lowL = low_[0], lowR = low_[1], highL = high_[0], highR = high_[1];
        double delay = delay_;
        int at = at_;
        for (int i = 0; i < frames; ++i) {
            feedback += dFeedback, push += dPush, ceiling += dCeiling, highCut += dHigh, lowCut += dLow, depth += dDepth, mix += dMix;
            if (wowLeft == 0) {
                phase_ += 6.283185307179586 * kWowHz * kWowEvery / sr_;
                if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
                dWow = (static_cast<float>(std::sin(phase_)) - wow) / static_cast<float>(kWowEvery);
                wowLeft = kWowEvery;
            }
            wow += dWow, --wowLeft;
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
            starved = std::fabs(inL) >= kFedLevel || std::fabs(inR) >= kFedLevel ? 0 : (starved < hold ? starved + 1 : hold);
            const float tireTo = starved >= hold ? tiredTo : 1.f;
            if (tire != tireTo) tire = tireStep(tire, tireTo, tireDown, tireUp);
            // What comes off the tape, through the two cuts: the echo.
            highL += highCut * (tapRead(tapeL, size_, at, whole, w) + kFloor - highL);
            highR += highCut * (tapRead(tapeR, size_, at, whole, w) + kFloor - highR);
            lowL += lowCut * (highL - lowL);
            lowR += lowCut * (highR - lowR);
            const float echoL = highL - lowL, echoR = highR - lowR;
            // Back onto the tape under the ceiling, with what is coming in.
            // (Nothing too faint goes round or on: see kFloor.)
            // (`tire` is exactly 1 unless the loop has been left alone a long while: until then this is the sound as it was, to the bit.)
            const float round = feedback * tire;
            float backL = echoL * round * push, backR = echoR * round * push;
            backL = std::fabs(backL) < kQuiet ? 0.f : backL;
            backR = std::fabs(backR) < kQuiet ? 0.f : backR;
            const float nextL = (std::fabs(inL) < kNothing ? 0.f : inL) + ceiling * soft(backL);
            const float nextR = (std::fabs(inR) < kNothing ? 0.f : inR) + ceiling * soft(backR);
            tapeL[at] = nextL;
            tapeR[at] = nextR;
            // Asleep once the whole tape holds nothing worth the name: a tape's length of this.
            quiet = std::fabs(nextL) < kQuiet && std::fabs(nextR) < kQuiet && std::fabs(inL) < kNothing && std::fabs(inR) < kNothing ? quiet + 1 : 0;
            left[i] = inL * dry + mix * echoL;
            right[i] = inR * dry + mix * echoR;
            if (++at >= size_) at = 0;
        }
        at_ = at;
        delay_ = delay;
        wow_ = wow, wowStep_ = dWow, wowLeft_ = wowLeft;
        low_[0] = lowL, low_[1] = lowR, high_[0] = highL, high_[1] = highR;
        tire_ = tire;
        starved_ = starved;
        feedback_.done(), push_.done(), ceiling_.done(), highCut_.done(), lowCut_.done(), depth_.done(), mix_.done();
        quiet_ = quiet;
    }
    // Everything that glides is where it was turned to, at once.
    void arrive() {
        delay_ = delayTo_;
        tire_ = 1.f;
        feedback_.snap(), push_.snap(), ceiling_.snap(), highCut_.snap(), lowCut_.snap(), depth_.snap(), mix_.snap();
    }
    // The ceiling: straight for small sounds, flat at 1 from 1.5 up.
    static float soft(float x) {
        x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
        return x - (4.f / 27.f) * x * x * x;
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, quiet_ = 0;
    long starved_ = 0; // samples since sound last came in, up to the hold
    bool asleep_ = true;
    float tempo_ = 120.f, wowMost_ = 72.f, wow_ = 0.f, wowStep_ = 0.f;
    int wowLeft_ = 0; // samples until the wow's next point
    double delay_ = 24000.0, delayTo_ = 24000.0, phase_ = 0.0;
    float low_[2] = {}, high_[2] = {};
    Glide feedback_, push_, ceiling_, highCut_, lowCut_, depth_, mix_;
    float tire_ = 1.f; // what FEEDBACK is multiplied by: 1, or less once the loop has been left alone (IT ALWAYS ENDS)
};

void reading(int index, float v, const float*, char* out, int size) {
    if (index == 0) std::snprintf(out, static_cast<std::size_t>(size), "%s", kTimeNames[timeChoice(v)]);
    else if (index == 1) readPercent(v * kMaxFeedback, out, size);
    else readPercent(v, out, size);
}

Effect* create() { return new Tape; }

} // namespace

float tapeTire(float setting) {
    // The SETTING is compared, with a hair of room, not what it reads: 1.2f * 0.75f comes out a
    // float above 0.9f, and a FEEDBACK of exactly 90% was then multiplied by 0.99999994.
    if (!(setting > kTiredSetting + kTiredRoom)) return 1.f;
    return kTiredFeedback / (kMaxFeedback * setting);
}

float tapeTireStep(float tire, float to, float sampleRate) {
    return tireStep(tire, to, 1.f / (kTireSeconds * sampleRate), 1.f / (kSettle * sampleRate));
}

const EffectInfo kTapeInfo = {
    "TAPE", 4, {"TIME", "FEEDBACK", "AGE", "MIX"}, {0.5625f, 0.4f, 0.35f, 0.35f}, // a dotted eighth, 48%
    "tape", EffectGroup::Delay, {kTimes, 0, 0, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
