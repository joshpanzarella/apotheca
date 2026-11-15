#pragma once
// Changes the sample rate of a stereo stream, for an instrument that makes
// sound at one rate inside an engine running at another.
//
// Each output sample is a weighted sum of the 24 input samples around it.
// The weights are a windowed sinc: a low-pass that cuts everything above
// what the slower of the two rates can carry, so nothing folds back as a
// false tone. They are worked out ahead for 512 positions between two
// input samples, and the two nearest sets are blended.
//
// prepare() allocates and is not for the audio thread. render() is.
// The weights are made once for each pair of rates and shared (tableFor).

#include <cmath>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace apotheca {

class RateConverter {
public:
    static constexpr int kTaps = 24;
    static constexpr int kPhases = 512;

    void prepare(double inRate, double outRate) {
        step_ = inRate / outRate;
        shared_ = tableFor(inRate, outRate);
        table_ = shared_->data();
        reset();
    }
    // The weights in use (shared: every converter between the same two rates reads the same).
    const float* table() const { return table_; }

    // The weights for one pair of rates: made the first time they are asked for and kept, the same
    // for every converter between those rates (speed plan, batch 1: each Phazed track had its own
    // 49 KB, read every sample). Not for the audio thread; any other, at once.
    static std::shared_ptr<const std::vector<float>> tableFor(double inRate, double outRate) {
        static std::mutex mutex;
        static std::vector<std::pair<std::pair<double, double>, std::shared_ptr<const std::vector<float>>>> made;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& one : made)
            if (one.first.first == inRate && one.first.second == outRate) return one.second;
        std::shared_ptr<std::vector<float>> table(new std::vector<float>(static_cast<std::size_t>(kPhases + 1) * kTaps, 0.f));
        // Pass up to 90% of the lower rate's limit.
        const double cutoff = 0.9 * (outRate < inRate ? outRate / inRate : 1.0);
        for (int phase = 0; phase <= kPhases; ++phase) {
            const double fraction = static_cast<double>(phase) / kPhases;
            double sum = 0.0;
            double weights[kTaps];
            for (int tap = 0; tap < kTaps; ++tap) {
                // Distance, in input samples, from this tap to the output's position.
                const double x = (tap - (kTaps / 2 - 1)) - fraction;
                const double t = 3.14159265358979323846 * x * cutoff;
                const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(t) / t;
                // A Blackman-Harris window over the 24 taps.
                const double w = (x + kTaps / 2) / kTaps;
                const double window = w <= 0.0 || w >= 1.0 ? 0.0
                    : 0.35875 - 0.48829 * std::cos(2 * 3.14159265358979323846 * w)
                      + 0.14128 * std::cos(4 * 3.14159265358979323846 * w) - 0.01168 * std::cos(6 * 3.14159265358979323846 * w);
                weights[tap] = cutoff * sinc * window;
                sum += weights[tap];
            }
            // Each set adds up to exactly one, so a steady level stays steady.
            for (int tap = 0; tap < kTaps; ++tap) (*table)[static_cast<std::size_t>(phase) * kTaps + tap] = static_cast<float>(weights[tap] / sum);
        }
        made.emplace_back(std::make_pair(inRate, outRate), table);
        return table;
    }

    void reset() {
        for (float& v : left_) v = 0.f;
        for (float& v : right_) v = 0.f;
        position_ = 1.0;
        head_ = 0;
    }

    // Every sample of the history is exactly zero (either sign): what render() puts out is then
    // exactly +0 for as long as the samples pulled are zeros (speed plan, batch 1).
    bool silent() const {
        for (int i = 0; i < kTaps; ++i)
            if (left_[i] != 0.f || right_[i] != 0.f) return false;
        return true;
    }
    // What render() does to the converter for `frames` while every sample pulled is +0, without
    // the sums: the place moves on and the zeros go into the history. Returns how many samples it
    // would have pulled. The caller writes the zeros out.
    int skip(int frames) {
        int pulls = 0;
        for (int i = 0; i < frames; ++i) {
            while (position_ >= 1.0) {
                left_[head_] = left_[head_ + kTaps] = 0.f;
                right_[head_] = right_[head_ + kTaps] = 0.f;
                head_ = (head_ + 1) % kTaps;
                position_ -= 1.0;
                ++pulls;
            }
            position_ += step_;
        }
        return pulls;
    }

    // The most samples render() or skip() can pull for `frames` out.
    int pullsAtMost(int frames) const { return static_cast<int>(static_cast<double>(frames) * step_) + 2; }

    // `pull(l, r)` must give the next input sample each time it is called.
    template <typename Pull>
    void render(Pull&& pull, float* left, float* right, int frames) {
        for (int i = 0; i < frames; ++i) {
            while (position_ >= 1.0) {
                float l = 0.f, r = 0.f;
                pull(l, r);
                // The history is written twice, a block apart, so any 24 in a row are contiguous.
                left_[head_] = left_[head_ + kTaps] = l;
                right_[head_] = right_[head_ + kTaps] = r;
                head_ = (head_ + 1) % kTaps;
                position_ -= 1.0;
            }
            const double scaled = position_ * kPhases;
            const int phase = static_cast<int>(scaled);
            const float blend = static_cast<float>(scaled - phase);
            const float* a = table_ + static_cast<std::size_t>(phase) * kTaps;
            const float* b = a + kTaps;
            const float* l = &left_[head_];
            const float* r = &right_[head_];
            float outL = 0.f, outR = 0.f;
            for (int tap = 0; tap < kTaps; ++tap) {
                const float weight = a[tap] + blend * (b[tap] - a[tap]);
                outL += l[tap] * weight;
                outR += r[tap] * weight;
            }
            left[i] = outL;
            right[i] = outR;
            position_ += step_;
        }
    }

private:
    std::shared_ptr<const std::vector<float>> shared_;
    const float* table_ = nullptr;
    float left_[2 * kTaps] = {};
    float right_[2 * kTaps] = {};
    int head_ = 0;          // the oldest sample in the history
    double step_ = 1.0;     // input samples per output sample
    double position_ = 1.0; // how far the next output is past the newest-but-(kTaps/2) input
};

} // namespace apotheca
