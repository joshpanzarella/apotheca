// COMB: a tuned comb filter, a TYPE of FILTER. The sound is fed back on
// itself one cycle of a note late, so every harmonic of that note rings: a
// drum loop through it hums in the song's key, like a string or a metal tube
// struck by it.
//
//   PITCH     the note it rings at: 25 choices, C2 to C4 (65 to 262 Hz), a
//             semitone a push. A choice, not an amount: the D-pad lands on a
//             note, never between two.
//   FEEDBACK  -100% to +100% (it does 0.95 of what it reads, so it always
//             dies away). Above 0 every harmonic of PITCH rings: bright,
//             like a string. Below 0 only the odd ones, an octave lower:
//             hollow, like a clarinet or a tube. At 0 nothing rings.
//   TONE      how bright the ringing stays: a high cut inside the loop, 500 Hz
//             to 18 kHz. Lower, the high harmonics die first (a plucked
//             string); higher, it rings metallic.
//   MIX       0 is the sound untouched, to the sample.
//
// The sound in is turned down as FEEDBACK goes up (by sqrt(1 - f^2)), so a
// full sound such as drums or noise comes out about as loud at any
// FEEDBACK; a sound sitting right on one of PITCH's harmonics comes out up to
// 16 dB louder at the most (tests/effects_test.cpp, testComb).
//
// IN TUNE. The high cut inside the loop makes each trip round a little later
// (a third of a millisecond at 500 Hz), which would flatten the note by up to
// a semitone: the loop is shortened by exactly that, worked out at the note
// it rings at, and read between samples (glide.hpp's cubic). testComb holds
// the ringing to a few cents of the note.
//
// The rest is PING's pattern: every setting glides (a new PITCH swoops
// there in 60 ms; PITCH, FEEDBACK and TONE set off gently: svf.hpp's
// Gentle), what goes on the line under kQuiet is an exact zero, and once the
// line is zeros from end to end with nothing coming in it is asleep (no work;
// what comes out is what came in). The glides move on the TICK (svf.hpp) and
// the loop's numbers are slid to sample by sample, so the sound is the same
// to the bit however it is cut into blocks, even while a setting moves.

#include <vector>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kNotes = 25;      // C2 to C4
constexpr int kLowestKey = 36;  // C2, as a keyboard numbers it
constexpr float kMost = 0.95f;  // the most one trip round is of the last
constexpr float kSettle = 0.015f;   // FEEDBACK, TONE: each of Gentle's two glides (about 30 ms in all)
constexpr float kMixSettle = 0.02f;
constexpr float kPitchSettle = 0.03f; // PITCH: slower, so a new note is a swoop (about 60 ms) and not a lurch
enum { kFeedback, kCut, kDelay };
constexpr float kFloor = 1e-18f; // where the high cut rests (glide.hpp: numbers too small to hold)
const char* const kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

int noteOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kNotes);
    return i > kNotes - 1 ? kNotes - 1 : i;
}
double hzOf(int note) { return 440.0 * std::pow(2.0, (kLowestKey + note - 69) / 12.0); }
float feedbackOf(float v) { return 2.f * clamp01(v) - 1.f; } // as it reads, -1 to 1
float cutOf(float v, float sr) {
    const float hz = 500.f * std::pow(36.f, clamp01(v)), most = 0.45f * sr;
    return 1.f - std::exp(-6.2831853f * (hz > most ? most : hz) / sr);
}
// How many samples late the high cut makes a trip round, at `w` (radians a sample).
double cutLate(double cut, double w) {
    const double keep = 1.0 - cut;
    return std::atan2(keep * std::sin(w), 1.0 - keep * std::cos(w)) / w;
}

class Comb : public Unit {
public:
    Comb() : Unit(kCombInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        size_ = static_cast<int>(sampleRate / 60.f) + 8; // the lowest note, C2, at 65 Hz, and room to read round it
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size_), 0.f);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        at_ = 0;
        quiet_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (size_ == 0 || frames <= 0) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        float* const line[2] = {line_[0].data(), line_[1].data()};
        for (int i = 0; i < frames; ++i) {
            const float inL = left[i], inR = right[i];
            const float x[2] = {heardIn(inL), heardIn(inR)};
            if (asleep_) {
                if (x[0] == 0.f && x[1] == 0.f) {
                    tick_ = (tick_ + 1) % kTick; // what comes out is what came in
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            const float mix = mix_.next();
            if (loop_.moving) {
                loop_.next();
                weigh(loop_.now[kDelay]);
            }
            const float fb = loop_.now[kFeedback], cut = loop_.now[kCut];
            const float in = std::sqrt(1.f - fb * fb);
            bool zeros = x[0] == 0.f && x[1] == 0.f;
            for (int c = 0; c < 2; ++c) {
                const float echo = tapRead(line[c], size_, at_, whole_, w_);
                cutState_[c] += cut * (echo + kFloor - cutState_[c]);
                float y = in * x[c] + fb * cutState_[c];
                y = std::fabs(y) < kQuiet ? 0.f : y; // nothing too faint goes on the line: an exact zero does
                line[c][at_] = y;
                zeros = zeros && y == 0.f;
                if (mix != 0.f) io[c][i] = (c ? inR : inL) + mix * (y - x[c]);
            }
            quiet_ = zeros ? quiet_ + 1 : 0;
            if (++at_ >= size_) at_ = 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        const double hz = hzOf(noteOf(p_[0]));
        const float f = feedbackOf(p_[1]);
        feedback_.first.to = kMost * f;
        const float cut = cutOf(p_[2], sr_);
        cut_.first.to = cut;
        mix_.to = p_[3];
        // Below 0 the loop rings where a trip round is half a turn: the note's half (an octave down), odd harmonics only.
        const double w = (f < 0.f ? 3.14159265358979 : 6.28318530717959) * hz / sr_;
        delay_.first.to = static_cast<float>(sr_ / hz - cutLate(cut, w));
    }

private:
    void beginTick() {
        mix_.begin(kTick, glideK(kTick, kMixSettle, sr_));
        if (!(feedback_.moving() || cut_.moving() || delay_.moving())) return;
        // Where the loop's three are at the tick's end, and there sample by sample.
        const float k = glideK(kTick, kSettle, sr_);
        feedback_.tick(k), cut_.tick(k), delay_.tick(glideK(kTick, kPitchSettle, sr_));
        aimAt();
        loop_.aim();
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        if (loop_.moving) {
            loop_.land();
            weigh(loop_.now[kDelay]);
        }
        if (quiet_ >= size_) asleep_ = true; // the line is zeros from end to end
    }
    void aimAt() { loop_.to[kFeedback] = feedback_.now(), loop_.to[kCut] = cut_.now(), loop_.to[kDelay] = delay_.now(); }
    void weigh(float delay) {
        whole_ = static_cast<int>(delay);
        cubicWeights(delay - static_cast<float>(whole_), w_);
    }
    void wake() {
        asleep_ = false;
        quiet_ = 0;
        arrive(); // nothing is ringing: whatever was turned meanwhile is simply there
    }
    void arrive() {
        feedback_.snap(), cut_.snap(), mix_.snap(), delay_.snap();
        aimAt();
        loop_.snap();
        cutState_[0] = cutState_[1] = kFloor;
        weigh(loop_.now[kDelay]);
    }

    std::vector<float> line_[2];
    int size_ = 0, at_ = 0, quiet_ = 0, tick_ = 0, whole_ = 2;
    bool asleep_ = true;
    float w_[4] = {0.f, 1.f, 0.f, 0.f};
    float cutState_[2] = {kFloor, kFloor};
    Gentle feedback_, cut_, delay_;
    Glide mix_;
    Slide<3> loop_; // what the loop runs on: kFeedback, kCut, kDelay
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const int key = kLowestKey + noteOf(v);
        std::snprintf(out, n, "%s%d", kKeyNames[key % 12], key / 12 - 1);
    } else if (index == 1) {
        const int percent = static_cast<int>(std::round(feedbackOf(v) * 100.f));
        if (percent == 0) std::snprintf(out, n, "0%%");
        else std::snprintf(out, n, "%+d%%", percent);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Comb; }

} // namespace

const EffectInfo kCombInfo = {
    "COMB", 4, {"PITCH", "FEEDBACK", "TONE", "MIX"}, {12.5f / 25.f, 0.85f, 0.7f, 0.5f}, // C3, +70%, 6.1 kHz, half
    "comb", EffectFamily::Filter, {kNotes, 0, 0, 0}, reading, create,
    "rings at a note",
};

double combHz(float pitch) { return hzOf(noteOf(pitch)); }

} // namespace fx
} // namespace apotheca
