#pragma once
// Recording from an input (a USB microphone or interface), with the tidying
// done for you:
//
//   - it waits for sound, so there is no silence on the front (and keeps the
//     20 ms before the sound began, so the very start is not clipped off)
//   - it stops by itself at ten seconds
//   - silence on the end is trimmed, and the end faded so it does not click
//   - a quiet recording is brought up to a healthy level
//
// No SDL here: whatever reads the input feeds it samples.

#include <vector>

namespace apotheca {

class Recorder {
public:
    enum State { Idle, Waiting, Recording };

    void arm(float sampleRate = 48000.f);            // start waiting for sound
    void feed(const float* mono, int frames);        // samples from the input
    std::vector<float> finish();                     // stop. What was recorded, tidied; empty if no sound came.
    State state() const { return state_; }
    bool full() const { return state_ == Recording && static_cast<float>(taken_.size()) >= kMaxSeconds * rate_; }
    float seconds() const { return static_cast<float>(taken_.size()) / rate_; }
    float level() const { return level_; }           // how loud the input is just now, 0..1

    static constexpr float kMaxSeconds = 10.f;
    static constexpr float kStartLevel = 0.02f;      // louder than this is "sound"

private:
    State state_ = Idle;
    float rate_ = 48000.f, level_ = 0.f;
    std::vector<float> before_; // the last 20 ms while waiting, as a ring
    std::size_t beforeAt_ = 0;
    std::vector<float> taken_;
};

} // namespace apotheca
