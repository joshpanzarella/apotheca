// CHORUS: copies of the sound, slightly late and wavering.
//
//   RATE    how fast they waver: 0.1 to 8 Hz
//   DEPTH   how far: the copies' delay swings by up to the type's SWING each
//           way. Above 1 Hz the swing shrinks as the rate rises, so that the
//           pitch bends by the same amount however fast it goes.
//   TYPE    CHORUS     three copies a side, a third of a turn apart
//           ENSEMBLE   three copies, each at its own speed
//           VIBRATO    one copy and no dry sound at full MIX: the pitch itself wavers
//           FLANGE     one very short copy, fed back into itself
//   MIX     0 is the sound untouched
//
// Left and right waver a quarter turn out of step, which is the width
// (VIBRATO keeps them in step: it is one pitch). The copies are read between
// samples with a cubic curve, so a moving delay makes no zipper noise, and
// every setting glides: turning one, TYPE included, is not a click.

#include <vector>

#include "glide.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kVoices = 3;
constexpr float kRoot3rd = 0.57735027f; // three copies together as loud as one

struct Shape {
    const char* name;
    float centre[kVoices]; // each copy's delay at rest, ms
    float swing;           // how far DEPTH at full moves it each way, ms
    float weight[kVoices];
    float feedback;
    float ownSpeeds; // 1: each copy at its own speed (ENSEMBLE)
    float spread;    // 1: right a quarter turn after left; 0: in step
    bool wetOnly;    // MIX at full leaves no dry sound
};
const Shape kShapes[4] = {
    {"CHORUS", {7.f, 10.5f, 14.f}, 3.f, {kRoot3rd, kRoot3rd, kRoot3rd}, 0.f, 0.f, 1.f, false},
    {"ENSEMBLE", {7.f, 10.5f, 14.f}, 2.5f, {kRoot3rd, kRoot3rd, kRoot3rd}, 0.f, 1.f, 1.f, false},
    {"VIBRATO", {5.f, 10.5f, 14.f}, 4.f, {1.f, 0.f, 0.f}, 0.f, 0.f, 0.f, true},
    {"FLANGE", {2.2f, 10.5f, 14.f}, 1.9f, {1.f, 0.f, 0.f}, 0.6f, 0.f, 1.f, false},
};
// ENSEMBLE's three speeds, as multiples of RATE: never in step for long.
constexpr double kSpeeds[kVoices] = {1.0, 1.29, 0.77};
constexpr float kFullSwingBelow = 1.f; // Hz: above this the swing shrinks as 1 / rate
constexpr float kGlide = 0.02f;        // seconds: how long a turned setting takes to get most of the way

int shapeOf(float v) {
    const int i = static_cast<int>(clamp01(v) * 4.f);
    return i > 3 ? 3 : i;
}
float rateHz(float v) { return 0.1f * std::pow(80.f, clamp01(v)); }

class Chorus : public Unit {
public:
    Chorus() : Unit(kChorusInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        int size = 1024;
        while (size < static_cast<int>(0.03f * sampleRate) + 8) size *= 2; // 30 ms: the longest copy is 17
        mask_ = size - 1;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size), 0.f);
        reset();
        changed();
    }
    void reset() override {
        for (auto& line : line_) line.assign(line.size(), 0.f);
        at_ = 0;
        for (int j = 0; j < kVoices; ++j) {
            sin_[j] = 0.0;
            cos_[j] = 1.0;
        }
        fed_[0] = fed_[1] = 0.f;
        settled_ = false;
    }
    void process(float* left, float* right, int frames) override {
        if (line_[0].empty()) return;
        for (int done = 0; done < frames; done += kBlock) block(left + done, right + done, frames - done < kBlock ? frames - done : kBlock);
    }

protected:
    void changed() override {
        const Shape& shape = kShapes[shapeOf(p_[2])];
        const float hz = rateHz(p_[0]);
        const float perMs = sr_ / 1000.f;
        for (int j = 0; j < kVoices; ++j) {
            turn_[j] = 2.0 * std::sin(3.141592653589793 * hz * kSpeeds[j] / sr_);
            centre_[j].to = shape.centre[j] * perMs;
            weight_[j].to = shape.weight[j];
        }
        swing_.to = shape.swing * perMs * p_[1] * (hz > kFullSwingBelow ? kFullSwingBelow / hz : 1.f);
        feedback_.to = shape.feedback;
        ownSpeeds_.to = shape.ownSpeeds;
        spread_.to = shape.spread;
        const float mix = p_[3];
        dry_.to = shape.wetOnly ? 1.f - mix : std::cos(mix * 0.78539816f);
        wet_.to = shape.wetOnly ? mix : std::sin(mix * 0.78539816f);
    }

private:
    static constexpr int kBlock = 64;

    // One copy: the line as it was `delay` samples ago, read between samples.
    float read(const float* line, int at, float delay) const {
        const int whole = static_cast<int>(delay);
        const float f = delay - static_cast<float>(whole);
        const float xm = line[(at - whole + 1) & mask_], x0 = line[(at - whole) & mask_], x1 = line[(at - whole - 1) & mask_], x2 = line[(at - whole - 2) & mask_];
        const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void block(float* left, float* right, int frames) {
        Glide* const all[] = {&centre_[0], &centre_[1], &centre_[2], &weight_[0], &weight_[1], &weight_[2], &swing_, &feedback_, &ownSpeeds_, &spread_, &dry_, &wet_};
        if (!settled_) {
            for (Glide* g : all) g->snap(); // the first sound: every setting is where it was put
            settled_ = true;
        }
        const float part = glideK(frames, kGlide, sr_);
        for (Glide* g : all) g->begin(frames, part);
        // Copies two and three are left out while they are silent (VIBRATO, FLANGE).
        const int voices = weight_[1].now == 0.f && weight_[1].to == 0.f && weight_[2].now == 0.f && weight_[2].to == 0.f ? 1 : kVoices;
        // A third of a turn on, and two thirds: sin(a + 120) = -sin(a)/2 + cos(a) * 0.866.
        static const float kCosOf[kVoices] = {1.f, -0.5f, -0.5f}, kSinOf[kVoices] = {0.f, 0.8660254f, -0.8660254f};
        float* const lineL = line_[0].data();
        float* const lineR = line_[1].data();
        // First where every copy is to be read, for the whole block (the three
        // wavers go round once a sample)...
        float delayL[kVoices][kBlock], delayR[kVoices][kBlock];
        for (int i = 0; i < frames; ++i) {
            float s[kVoices], c[kVoices];
            for (int j = 0; j < kVoices; ++j) {
                sin_[j] += turn_[j] * cos_[j];
                cos_[j] -= turn_[j] * sin_[j];
                s[j] = static_cast<float>(sin_[j]);
                c[j] = static_cast<float>(cos_[j]);
            }
            const float swing = swing_.next(), own = ownSpeeds_.next(), spread = spread_.next();
            for (int j = 0; j < voices; ++j) {
                // Which waver this copy rides: the first one's, a third of a turn on each, or its own.
                const float sj = s[0] + own * (s[j] - s[0]), cj = c[0] + own * (c[j] - c[0]);
                const float waverL = sj * kCosOf[j] + cj * kSinOf[j];
                const float quarterBehind = sj * kSinOf[j] - cj * kCosOf[j]; // sin(a - 90) = -cos(a)
                const float waverR = waverL + spread * (quarterBehind - waverL);
                const float centre = centre_[j].next();
                delayL[j][i] = centre + swing * waverL;
                delayR[j][i] = centre + swing * waverR;
            }
        }
        // ...then the sound.
        int at = at_;
        float fedL = fed_[0], fedR = fed_[1];
        for (int i = 0; i < frames; ++i) {
            const float feedback = feedback_.next();
            const float inL = left[i], inR = right[i];
            float writeL = inL + feedback * fedL, writeR = inR + feedback * fedR;
            if (std::fabs(writeL) < 1e-20f) writeL = 0.f; // a dying flange does not grind on vanishing numbers
            if (std::fabs(writeR) < 1e-20f) writeR = 0.f;
            lineL[at] = writeL;
            lineR[at] = writeR;
            fedL = read(lineL, at, delayL[0][i]);
            fedR = read(lineR, at, delayR[0][i]);
            const float first = weight_[0].next();
            float wetL = first * fedL, wetR = first * fedR;
            for (int j = 1; j < voices; ++j) {
                const float weight = weight_[j].next();
                wetL += weight * read(lineL, at, delayL[j][i]);
                wetR += weight * read(lineR, at, delayR[j][i]);
            }
            const float dry = dry_.next(), wet = wet_.next();
            left[i] = dry * inL + wet * wetL;
            right[i] = dry * inR + wet * wetR;
            at = (at + 1) & mask_;
        }
        at_ = at;
        fed_[0] = fedL;
        fed_[1] = fedR;
        for (Glide* g : all) g->done();
    }

    std::vector<float> line_[2];
    int mask_ = 0, at_ = 0;
    double sin_[kVoices] = {0.0, 0.0, 0.0}, cos_[kVoices] = {1.0, 1.0, 1.0}, turn_[kVoices] = {};
    Glide centre_[kVoices], weight_[kVoices], swing_, feedback_, ownSpeeds_, spread_, dry_, wet_;
    float fed_[2] = {};
    bool settled_ = false;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const float hz = rateHz(v);
        if (hz < 0.995f) std::snprintf(out, n, "%.2f HZ", static_cast<double>(hz));
        else std::snprintf(out, n, "%.1f HZ", static_cast<double>(hz));
    } else if (index == 2) {
        std::snprintf(out, n, "%s", kShapes[shapeOf(v)].name);
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Chorus; }

} // namespace

const EffectInfo kChorusInfo = {
    "CHORUS", 4, {"RATE", "DEPTH", "TYPE", "MIX"}, {0.4f, 0.5f, 0.f, 0.8f},
    "chorus", EffectGroup::Moving, {0, 0, 4, 0}, reading, create,
};

} // namespace fx
} // namespace apotheca
