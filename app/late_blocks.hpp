#pragma once
// Sound blocks that came late, counted on the audio thread and written down on the screen's (owner, 2026-11-25:
// "there is a noticeable click in the audio when I am moving between the parameters of the filter", FX screen,
// FILTER; nothing on the desk sends the sound anything on that move: docs/dev/engine.md, "Late sound blocks").
//
// A block is LATE when its call took longer than the sound it made lasts, or when it began more than one and a
// half blocks' time after the call before it began (the sound card holds two blocks: a gap of two is a gap heard).
// The audio thread only stores numbers in atomics: nothing made, freed or locked. The screen's thread asks once a
// pass (take) and writes a line for each new one it finds (main.cpp: logs/sound.log on the handheld, and the
// crash trail). A line says how many came late since the line before, so a burst is one line, not hundreds.

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace apotheca {

struct LateBlock {
    unsigned count = 0;  // late blocks so far, this run
    unsigned since = 0;  // of them, since the last one taken
    std::int64_t gapUs = 0, tookUs = 0, blockUs = 0; // the latest: the gap since the call before began, its own time
};

class LateBlocks {
public:
    static bool isLate(std::int64_t gapUs, std::int64_t tookUs, std::int64_t blockUs) {
        return blockUs > 0 && (tookUs > blockUs || 2 * gapUs > 3 * blockUs);
    }

    // The audio thread, at the end of each call: when it began and how long it took (a steady clock, in
    // microseconds), and how long its sound lasts.
    void block(std::int64_t startUs, std::int64_t tookUs, std::int64_t blockUs) {
        if (fresh_.exchange(false, std::memory_order_acq_rel)) last_ = -1;
        const std::int64_t gap = last_ < 0 ? 0 : startUs - last_; // (the first call has nothing before it)
        last_ = startUs;
        if (!isLate(gap, tookUs, blockUs)) return;
        gapUs_.store(gap, std::memory_order_relaxed);
        tookUs_.store(tookUs, std::memory_order_relaxed);
        blockUs_.store(blockUs, std::memory_order_relaxed);
        count_.fetch_add(1u, std::memory_order_release);
    }
    // The sound card opened again (or the sound paused): the next call's gap is not a late block.
    void restart() { fresh_.store(true, std::memory_order_release); }

    // The screen's thread: a new late block since the last ask? Then `out` has the latest's numbers.
    bool take(LateBlock& out) {
        const unsigned count = count_.load(std::memory_order_acquire);
        if (count == seen_) return false;
        out.count = count;
        out.since = count - seen_;
        out.gapUs = gapUs_.load(std::memory_order_relaxed);
        out.tookUs = tookUs_.load(std::memory_order_relaxed);
        out.blockUs = blockUs_.load(std::memory_order_relaxed);
        seen_ = count;
        return true;
    }
    unsigned count() const { return count_.load(std::memory_order_acquire); }

private:
    std::int64_t last_ = -1; // the audio thread's own
    std::atomic<bool> fresh_{false};
    std::atomic<std::int64_t> gapUs_{0}, tookUs_{0}, blockUs_{0};
    std::atomic<unsigned> count_{0u};
    unsigned seen_ = 0; // the screen's thread's own
};

// The line for one: "late sound block: began 31.2 ms after the one before, took 2.1 ms (a block is 5.3 ms); 1 since
// the last line, 4 this run; <doing>; the pass before took 18.0 ms (drawing 12.0)". `doing` is what the screen was at.
inline int lateLine(char* out, int size, const LateBlock& late, const char* doing, double passMs, double drawMs) {
    if (!out || size <= 0) return 0;
    return std::snprintf(out, static_cast<std::size_t>(size),
                         "late sound block: began %.1f ms after the one before, took %.1f ms (a block is %.1f ms); %u since the last line, %u this run; %s; the pass before took %.1f ms (drawing %.1f)",
                         static_cast<double>(late.gapUs) / 1000.0, static_cast<double>(late.tookUs) / 1000.0, static_cast<double>(late.blockUs) / 1000.0,
                         late.since, late.count, doing && *doing ? doing : "?", passMs, drawMs);
}

} // namespace apotheca
