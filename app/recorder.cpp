#include "recorder.hpp"

#include <cmath>

namespace apotheca {

void Recorder::arm(float sampleRate) {
    rate_ = sampleRate > 1000.f ? sampleRate : 48000.f;
    state_ = Waiting;
    level_ = 0.f;
    before_.assign(static_cast<std::size_t>(0.02f * rate_), 0.f);
    beforeAt_ = 0;
    taken_.clear();
    taken_.reserve(static_cast<std::size_t>(kMaxSeconds * rate_) + 1);
}

void Recorder::feed(const float* mono, int frames) {
    if (state_ == Idle) return;
    const std::size_t most = static_cast<std::size_t>(kMaxSeconds * rate_);
    for (int i = 0; i < frames; ++i) {
        float v = mono[i];
        if (!(v >= -4.f && v <= 4.f)) v = 0.f; // nonsense from a driver, NaN included
        const float loud = std::fabs(v);
        level_ = loud > level_ ? loud : level_ * 0.9995f; // a meter that falls back slowly
        if (state_ == Waiting) {
            if (loud < kStartLevel) {
                before_[beforeAt_] = v;
                beforeAt_ = (beforeAt_ + 1) % before_.size();
                continue;
            }
            // Sound. Begin with the moment before it, oldest first.
            for (std::size_t k = 0; k < before_.size(); ++k) taken_.push_back(before_[(beforeAt_ + k) % before_.size()]);
            state_ = Recording;
        }
        if (taken_.size() < most) taken_.push_back(v);
    }
}

std::vector<float> Recorder::finish() {
    std::vector<float> out;
    out.swap(taken_);
    state_ = Idle;
    level_ = 0.f;
    if (out.empty()) return out; // no sound came

    // Trim the silence off the end, leaving 50 ms after the last sound.
    std::size_t last = 0;
    float peak = 0.f;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float loud = std::fabs(out[i]);
        if (loud >= kStartLevel * 0.5f) last = i;
        if (loud > peak) peak = loud;
    }
    const std::size_t keep = last + 1 + static_cast<std::size_t>(0.05f * rate_);
    if (keep < out.size()) out.resize(keep);
    // Fade the last 5 ms, and bring a quiet take up (never past full scale).
    const std::size_t fade = static_cast<std::size_t>(0.005f * rate_) < out.size() ? static_cast<std::size_t>(0.005f * rate_) : out.size();
    for (std::size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    if (peak > 0.f && peak < 0.9f) {
        const float gain = 0.9f / peak > 8.f ? 8.f : 0.9f / peak; // at most eight times: do not turn hiss into a sound
        for (float& v : out) v *= gain;
    }
    return out;
}

} // namespace apotheca
