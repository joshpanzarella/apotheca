// VOWEL: a formant filter, a TYPE of FILTER. Four narrow peaks at the places a
// singer's mouth makes for a vowel, so a bright sound (a saw, noise, a hat)
// seems to say "ah", "eh", "ee", "oh", "oo". Turned, it talks.
//
//   VOWEL      A, E, I, O, U, and every shade between (A-E 40% is four
//              tenths of the way from A to E): the peaks slide from one
//              vowel's places to the next, so turning it is a "wah" that
//              speaks. Five D-pad pushes from one vowel to the next.
//   SHIFT      all four peaks up or down together, -12 to +12 semitones: a
//              smaller mouth (up, a child or a cartoon) or a bigger one (down).
//   RESONANCE  how narrow the peaks are (Q 3 to 24): low is a soft colour,
//              high whistles. Named as FILTER's, so changing TYPE keeps it.
//   MIX        0 is the sound untouched, to the sample.
//
// The places and levels are a bass voice's (the usual table: A 600, 1040,
// 2250, 2450 Hz...). A shade between two vowels slides each peak's place in
// pitch (so halfway is the middle in semitones) and its level in dB.
//
// The peaks are four of the old FILTER's band-pass, each at the height of
// its vowel's level, made up by Q to the 3/4 so a narrow one is about as loud
// as a wide one on a bright sound (a saw comes out 5 to 8 dB down; testVowel).
// A pure tone sitting on a peak comes out louder: up to 16 dB at RESONANCE full. They take the sound side by side
// (in parallel) and add.
//
// What moves is worked out on the TICK (svf.hpp): the peaks' places once
// every 16 samples counted from its start, and slid to sample by sample
// (Slide); VOWEL, SHIFT and RESONANCE set off gently (Gentle, about 30 ms in
// all); MIX glides every sample. A filter state
// under 1e-20 is made zero on a tick; with nothing coming in and nothing
// left in it over kQuiet it is asleep (what comes out is what came in).

#include <initializer_list>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kPeaks = 4, kVowels = 5;
enum { kA1, kA2, kA3, kGain, kPerPeak }; // a peak's numbers in the slide
struct Shape {
    float hz[kPeaks], db[kPeaks];
};
const Shape kShapes[kVowels] = {
    {{600.f, 1040.f, 2250.f, 2450.f}, {0.f, -7.f, -9.f, -9.f}},    // A
    {{400.f, 1620.f, 2400.f, 2800.f}, {0.f, -12.f, -9.f, -12.f}},  // E
    {{250.f, 1750.f, 2600.f, 3050.f}, {0.f, -30.f, -16.f, -22.f}}, // I
    {{400.f, 750.f, 2400.f, 2600.f}, {0.f, -11.f, -21.f, -20.f}},  // O
    {{350.f, 600.f, 2400.f, 2675.f}, {0.f, -20.f, -32.f, -28.f}},  // U
};
const char kLetters[kVowels + 1] = "AEIOU";
constexpr float kSettle = 0.015f, kMixSettle = 0.02f; // VOWEL, SHIFT, RESONANCE: each of Gentle's two glides
constexpr float kMakeUp = 0.7f; // times Q to the 3/4: a saw comes out about 6 dB down at any RESONANCE (testVowel)

float qOf(float v) { return 3.f * std::pow(8.f, clamp01(v)); }
float shiftOf(float v) { return 24.f * clamp01(v) - 12.f; } // semitones

class Vowel : public Unit {
public:
    Vowel() : Unit(kVowelInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        changed();
        reset();
    }
    void reset() override {
        for (auto& side : peak_)
            for (Svf& s : side) s.reset();
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {heardIn(left[i]), heardIn(right[i])};
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
            peaks_.next();
            const float* const n = peaks_.now;
            for (int c = 0; c < 2; ++c) {
                float wet = 0.f, v1, v2;
                for (int p = 0; p < kPeaks; ++p) {
                    const float* const q = n + p * kPerPeak;
                    peak_[c][p].step(q[kA1], q[kA2], q[kA3], x[c], v1, v2);
                    wet += q[kGain] * v1;
                }
                if (mix != 0.f) io[c][i] += mix * (wet - x[c]);
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        vowel_.first.to = p_[0];
        shift_.first.to = p_[1];
        q_.first.to = p_[2];
        mix_.to = p_[3];
    }

private:
    void beginTick() {
        const float k = glideK(kTick, kSettle, sr_);
        const bool moving = vowel_.moving() || shift_.moving() || q_.moving();
        mix_.begin(kTick, glideK(kTick, kMixSettle, sr_));
        if (!moving) return;
        // The places for this tick: where the three are going at its end, and there sample by sample.
        for (Gentle* g : {&vowel_, &shift_, &q_}) g->tick(k);
        shape();
        peaks_.aim();
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        peaks_.land();
        float loudest = 0.f;
        for (auto& side : peak_)
            for (Svf& s : side) s.settle(), loudest = std::fmax(loudest, s.loudest());
        if (quietIn_ >= kTick && loudest < kQuiet) { // nothing came in all this tick, and nothing is left ringing
            for (auto& side : peak_)
                for (Svf& s : side) s.reset();
            asleep_ = true;
        }
    }
    // The four peaks' places, Q and heights, from where VOWEL, SHIFT and RESONANCE are now.
    void shape() {
        const float at = clamp01(vowel_.now()) * (kVowels - 1);
        const int from = at >= kVowels - 1 ? kVowels - 2 : static_cast<int>(at);
        const float t = at - static_cast<float>(from);
        const float shift = std::pow(2.f, shiftOf(shift_.now()) / 12.f), q = qOf(q_.now()), k = 1.f / q;
        const float makeUp = kMakeUp * std::pow(q, 0.75f) * k; // k: the band-pass's own peak to 1
        for (int p = 0; p < kPeaks; ++p) {
            const Shape &a = kShapes[from], &b = kShapes[from + 1];
            const float hz = a.hz[p] * std::pow(b.hz[p] / a.hz[p], t) * shift;
            const float db = a.db[p] + t * (b.db[p] - a.db[p]);
            SvfShape f;
            f.set(svfG(hz, sr_), k);
            float* const to = peaks_.to + p * kPerPeak;
            to[kA1] = f.a1, to[kA2] = f.a2, to[kA3] = f.a3, to[kGain] = std::pow(10.f, db / 20.f) * makeUp;
        }
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        vowel_.snap(), shift_.snap(), q_.snap(), mix_.snap();
        shape();
        peaks_.snap();
    }

    Svf peak_[2][kPeaks];
    Slide<kPeaks * kPerPeak> peaks_; // each peak's filter numbers and its height
    Gentle vowel_, shift_, q_;
    Glide mix_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const float at = clamp01(v) * (kVowels - 1);
        const int from = static_cast<int>(at), part = static_cast<int>(std::round((at - static_cast<float>(from)) * 100.f));
        if (from >= kVowels - 1 || part == 0) std::snprintf(out, n, "%c", kLetters[from > kVowels - 1 ? kVowels - 1 : from]);
        else if (part == 100) std::snprintf(out, n, "%c", kLetters[from + 1]);
        else std::snprintf(out, n, "%c-%c %d%%", kLetters[from], kLetters[from + 1], part);
    } else if (index == 1) {
        const int semis = static_cast<int>(std::round(shiftOf(v)));
        if (semis == 0) std::snprintf(out, n, "0 ST");
        else std::snprintf(out, n, "%+d ST", semis);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Vowel; }

} // namespace

const EffectInfo kVowelInfo = {
    "VOWEL", 4, {"VOWEL", "SHIFT", "RESONANCE", "MIX"}, {0.f, 0.5f, 0.5f, 1.f}, // A, no shift, Q 8.5, all of it
    "vowel", EffectFamily::Filter, {0, 0, 0, 0}, reading, create,
    "talks: a, e, i, o, u",
};

} // namespace fx
} // namespace apotheca
