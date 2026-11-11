#pragma once
// Single-producer single-consumer queue. One thread pushes, one thread pops,
// and neither ever waits on the other. This is how the screen thread hands
// work to the audio thread without a lock.
//
// Capacity must be a power of two. One slot is kept empty to tell "full" from
// "empty", so a queue of N holds N-1 items.

#include <atomic>
#include <cstddef>

namespace apotheca {

template <typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0 && N >= 2, "capacity must be a power of two");

public:
    // Producer thread only. Returns false when full; the item is dropped.
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        items_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Any thread: nothing waiting, as far as can be seen from here. (A moment
    // out of date by the time it is read, which is all a caller should need.)
    bool empty() const { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }

    // Consumer thread only. Returns false when empty.
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = items_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }

private:
    T items_[N] = {};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace apotheca
