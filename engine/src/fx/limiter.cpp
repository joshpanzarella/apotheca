// LIMITER: a hard top, a TYPE of LEVEL. Nothing comes out louder than the
// CEILING: it sees each peak 3 ms before it plays and has already turned the
// sound down to fit by the time it gets there, so there is no overshoot and no
// clipped corner. Meant for the master (it sits before the engine's soft
// ceiling, which starts to round off at 0.8: at CEILING -2 dB that never acts),
// and as good on a drum track that must not poke out.
//
//   GAIN     how hard it is pushed: 0 to +20 dB before it (each D-pad push
//            1 dB). Louder, and the peaks are held down: the whole sounds
//            louder for the same top
//   CEILING  the top: -10 to 0 dB (each push half a dB). Nothing comes out
//            over it, to the sample
//   RELEASE  how fast it lets go after a peak: 20 ms to 1 second (DUCK's
//            RELEASE, the same scale, so TYPE keeps it). Short is loud and
//            pumps; long is smooth
//
// No MIX: a limiter under the plain sound lets every peak through, and the
// plain sound is 3 ms early. The sound is always EXACTLY 3 ms late
// (kLookSeconds, 144 samples at 48 kHz), whatever the settings: with GAIN at 0
// and nothing over the CEILING it comes out as it went in, to the bit, 3 ms
// later.
//
// How it works. The turn-down each sample needs, in dB (how far the louder of
// left and right is over the CEILING), is held at its most for the 3 ms ahead
// plus one sample; then it lets go at the RELEASE speed (in dB, with a least
// speed so it ARRIVES at 0 and does not creep for ever); then it is averaged
// over the 3 ms, so the turn-down sets off 3 ms before a peak and slides,
// never jumps. A peak is in every average that reaches it, at its full need,
// so the average is never less than it needs. Last, anything still over the
// CEILING by a rounding is put on it (the ceiling each sample was given with).
// Left and right are turned down together, so the picture does not lean.
//
// meter() is how many dB it is turning down, for the screen
// (Engine::effectMeter, on the GAIN box), as COMP's.
//
// Every setting glides on the TICK (svf.hpp), counted from the effect's start,
// so the sound is the same to the bit however it is cut into blocks. Asleep
// (nothing done, what comes out is what came in) once nothing has come in for
// the 3 ms and it has let go entirely: its state is then a fresh one's, so
// waking is exact.

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kLookSeconds = 0.003f;
constexpr int kRing = 1024;         // room for 3 ms at up to 340 kHz; a power of two
constexpr float kDbPerLog = 8.6858896f; // 20 / ln(10)
constexpr float kSettle = 0.02f;    // seconds for a turned setting to get most of the way
constexpr float kLeastDb = 0.01f;   // dB: the last stretch of letting go takes at most one RELEASE

float gainDb(float v) { return 20.f * clamp01(v); }
float ceilingDb(float v) { return -10.f + 10.f * clamp01(v); }
float releaseSeconds(float v) { return 0.02f * std::pow(50.f, clamp01(v)); } // as DUCK's

class Limiter : public Unit {
public:
    Limiter() : Unit(kLimiterInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        look_ = static_cast<int>(std::round(kLookSeconds * sr_));
        if (look_ < 1) look_ = 1;
        if (look_ > kRing - 2) look_ = kRing - 2;
        invLook_ = 1.f / static_cast<float>(look_);
        changed();
        reset();
    }
    void reset() override {
        quietIn_ = 0;
        asleep_ = true;
        clear();
        arrive();
        meter_ = 0.f;
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        float most = 0.f;
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
            const float gain = gain_.next(), ceiling = ceiling_.next();
            const float s0 = x[0] * gain, s1 = x[1] * gain;
            const int at = at_, out = (at_ - look_) & (kRing - 1);
            line_[0][at] = s0, line_[1][at] = s1, top_[at] = ceiling;
            // What this sample needs, held at its most over the look ahead and one more.
            const float peak = std::fmax(std::fabs(s0), std::fabs(s1));
            const float need = peak > ceiling ? kDbPerLog * std::log(peak / ceiling) : 0.f;
            const float held = holdMost(need);
            // Let go at the RELEASE speed, arriving at 0.
            const float fallen = down_ * release_ - least_;
            down_ = held > fallen ? held : (fallen > 0.f ? fallen : 0.f);
            // Averaged over the look ahead.
            const float leaving = avg_[out]; // what was put in look_ samples ago leaves the average
            avg_[at] = down_;
            sum_ += static_cast<double>(down_) - static_cast<double>(leaving);
            busy_ += (down_ > 0.f ? 1 : 0) - (leaving > 0.f ? 1 : 0);
            if (busy_ == 0) sum_ = 0.0; // exactly nothing to turn down: no rounding left over from before
            const float turn = static_cast<float>(sum_) * invLook_;
            most = turn > most ? turn : most;
            const float g = turn > 0.f ? std::exp(-turn / kDbPerLog) : 1.f;
            const float top = top_[out];
            float y0 = line_[0][out] * g, y1 = line_[1][out] * g;
            if (std::fabs(y0) > top) y0 = std::copysign(top, y0);
            if (std::fabs(y1) > top) y1 = std::copysign(top, y1);
            left[i] = y0, right[i] = y1;
            at_ = (at_ + 1) & (kRing - 1);
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
        meter_ = most;
    }
    float meter() const override { return meter_; }

protected:
    void changed() override {
        gainSet_.to = std::exp(gainDb(p_[0]) / kDbPerLog);
        ceilingSet_.to = std::exp(ceilingDb(p_[1]) / kDbPerLog);
        releaseSet_.to = p_[2];
    }

private:
    // The most of `need` over the last look_ + 1 samples (this one included): a queue of the samples that
    // could still be the most, each smaller than the one before it.
    float holdMost(float need) {
        while (count_ > 0 && queue_[(head_ + count_ - 1) & (kRing - 1)] <= need) --count_;
        const int back = (head_ + count_) & (kRing - 1);
        queue_[back] = need, when_[back] = n_;
        ++count_;
        while (n_ - when_[head_] > static_cast<unsigned>(look_)) head_ = (head_ + 1) & (kRing - 1), --count_;
        ++n_;
        return queue_[head_];
    }
    void beginTick() {
        const float k = glideK(kTick, kSettle, sr_);
        gain_.to = gainSet_.to, ceiling_.to = ceilingSet_.to, releaseNow_.to = releaseSet_.to;
        gain_.begin(kTick, k);
        ceiling_.begin(kTick, k);
        releaseNow_.begin(kTick, k), releaseNow_.done();
        speed();
    }
    void endTick() {
        tick_ = 0;
        gain_.done();
        ceiling_.done();
        // Asleep once nothing has come in for the look ahead (what is in the line is silence), and it has let
        // go entirely: everything it holds is then as a fresh one's.
        if (quietIn_ > look_ && down_ == 0.f && busy_ == 0) {
            clear();
            asleep_ = true;
        }
    }
    void speed() {
        const float seconds = releaseSeconds(releaseNow_.now) * sr_;
        release_ = std::exp(-1.f / seconds);
        least_ = kLeastDb / seconds;
    }
    void clear() {
        for (int c = 0; c < 2; ++c)
            for (float& v : line_[c]) v = 0.f;
        for (float& v : avg_) v = 0.f;
        for (float& v : top_) v = 1.f;
        sum_ = 0.0, busy_ = 0, down_ = 0.f;
        head_ = count_ = 0, n_ = 0u, at_ = 0;
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        gain_.to = gainSet_.to, ceiling_.to = ceilingSet_.to, releaseNow_.to = releaseSet_.to;
        gain_.snap(), ceiling_.snap(), releaseNow_.snap();
        speed();
    }

    float line_[2][kRing] = {}, avg_[kRing] = {}, top_[kRing] = {}, queue_[kRing] = {};
    unsigned when_[kRing] = {};
    double sum_ = 0.0;
    int busy_ = 0, head_ = 0, count_ = 0, at_ = 0, look_ = 144;
    unsigned n_ = 0u;
    float down_ = 0.f, release_ = 0.f, least_ = 0.f, meter_ = 0.f;
    float invLook_ = 1.f / 144.f;
    Glide gain_, ceiling_, releaseNow_;
    struct To {
        float to = 0.f;
    } gainSet_, ceilingSet_, releaseSet_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const int db = static_cast<int>(std::round(gainDb(v)));
        if (db == 0) std::snprintf(out, n, "0 DB");
        else std::snprintf(out, n, "+%d DB", db);
    } else if (index == 1) {
        const float db = std::round(ceilingDb(v) * 10.f) / 10.f;
        std::snprintf(out, n, "%.1f DB", static_cast<double>(db == 0.f ? 0.f : db));
    } else {
        const float seconds = releaseSeconds(v);
        if (seconds < 0.9995f) std::snprintf(out, n, "%d MS", static_cast<int>(std::round(seconds * 1000.f)));
        else std::snprintf(out, n, "1.0 S");
    }
}

Effect* create() { return new Limiter; }

} // namespace

const EffectInfo kLimiterInfo = {
    "LIMITER", 3, {"GAIN", "CEILING", "RELEASE", ""}, {0.15f, 0.8f, 0.4f, 0.f}, // +3 dB, -2.0 dB, 96 ms
    "limiter", EffectFamily::Level, {0, 0, 0, 0}, reading, create, "a hard top",
};

} // namespace fx
} // namespace apotheca
