#pragma once
// THE MOST OF THE LAST FEW: a sliding maximum, for an effect that reads a
// level (LIMITER's turn-down held after a peak, SNAP's level read over the
// last 14 ms). Each push gives the most of what was pushed over the last
// `back` + 1 pushes, this one included.
//
// How: a queue of the numbers that could still be the most, each smaller than
// the one before it. A new number drops every one behind it that is not bigger
// (it outlives them and is at least as big), then goes on the end; the front
// is dropped once it is older than `back`. So the front is always the most.
// Each number goes on once and off once: the work a push is the same on
// average, whatever is pushed, and nothing is ever looked at twice.
//
// The memory is the class's own (two arrays of N), made with the effect, never
// on the audio thread. N is a power of two and must be more than `back` + 1: a
// push writes the new number before the oldest is dropped, so the queue holds
// `back` + 2 for that moment (a `back` past N - 2 is taken as N - 2).
// clear() makes it as a fresh one: an effect clears it when it goes to sleep,
// so waking is exact.

namespace apotheca {
namespace fx {

template <int N>
class SlidingMost {
    static_assert(N > 1 && (N & (N - 1)) == 0, "a power of two");

public:
    // `back`: how many pushes before this one still count (0 to N - 2).
    float push(float v, unsigned back) {
        if (back > static_cast<unsigned>(N - 2)) back = static_cast<unsigned>(N - 2);
        while (count_ > 0 && most_[(head_ + count_ - 1) & (N - 1)] <= v) --count_;
        const int end = (head_ + count_) & (N - 1);
        most_[end] = v, when_[end] = n_;
        ++count_;
        while (n_ - when_[head_] > back) head_ = (head_ + 1) & (N - 1), --count_;
        ++n_;
        return most_[head_];
    }
    void clear() { head_ = count_ = 0, n_ = 0u; }

private:
    float most_[N] = {};
    unsigned when_[N] = {};
    int head_ = 0, count_ = 0;
    unsigned n_ = 0u;
};

} // namespace fx
} // namespace apotheca
