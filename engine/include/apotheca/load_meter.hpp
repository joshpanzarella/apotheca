#pragma once
// How hard the sound works (owner, 2026-11-15: "It would be nice to see the CPU usage"; the help
// card shows it). Each call of Engine::render is timed with a steady clock, on the audio thread,
// against how long the sound it made lasts (its frames at the engine's rate). Over about a second
// of sound (kWindowSeconds) that gives two numbers:
//
//   share   the time the calls took, over the time their sound lasts: 0.43 is 43% of the sound's
//           time spent making it. At 1 or over the sound cannot keep up and stutters.
//   worst   the one call of that second that took the most of its own time.
//
// They are kept in atomics for the app thread to read, a second at a time: no lock, nothing made or
// freed. The timing is two reads of the clock a call (tests/load_test.cpp --measure times it).

#include <atomic>
#include <chrono>

namespace apotheca {

class LoadMeter {
public:
    static constexpr double kWindowSeconds = 1.0;

    // A call made `lasts` seconds of sound in `busy` seconds. (The audio thread's, or a test's.)
    void add(double busy, double lasts) {
        if (!(lasts > 0.0) || !(busy >= 0.0)) return;
        busy_ += busy;
        sound_ += lasts;
        const double share = busy / lasts;
        if (share > worst_) worst_ = share;
        if (sound_ < kWindowSeconds) return;
        share_.store(static_cast<float>(busy_ / sound_), std::memory_order_relaxed);
        worstShown_.store(static_cast<float>(worst_), std::memory_order_relaxed);
        windows_.fetch_add(1u, std::memory_order_release);
        busy_ = sound_ = worst_ = 0.0;
    }

    // The last second's numbers (below 0: not a whole second of sound yet).
    float share() const { return share_.load(std::memory_order_relaxed); }
    float worst() const { return worstShown_.load(std::memory_order_relaxed); }
    // How many seconds have been measured: it moves when the numbers are new.
    unsigned windows() const { return windows_.load(std::memory_order_acquire); }

    // Times one call: made at its start, counted when it goes.
    class Timing {
    public:
        Timing(LoadMeter& meter, double lasts) : meter_(meter), lasts_(lasts), start_(std::chrono::steady_clock::now()) {}
        ~Timing() { meter_.add(std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(), lasts_); }
        Timing(const Timing&) = delete;
        Timing& operator=(const Timing&) = delete;

    private:
        LoadMeter& meter_;
        double lasts_;
        std::chrono::steady_clock::time_point start_;
    };

private:
    double busy_ = 0.0, sound_ = 0.0, worst_ = 0.0; // this second so far: the audio thread's own
    std::atomic<float> share_{-1.f}, worstShown_{-1.f};
    std::atomic<unsigned> windows_{0u};
};

} // namespace apotheca
