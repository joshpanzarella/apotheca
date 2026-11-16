#pragma once
// How much of the handheld's processor the whole program uses (every thread: the sound, its
// helpers, the screen, the WiFi page), for the help card (owner, 2026-11-15). Linux keeps the
// time the program has run on any core in /proc/self/stat (utime and stime, in clock ticks);
// read twice a second or more apart, the ticks gone by over the wall time gone by, shared out
// over the cores, is the program's share of the whole processor: 25% on a four-core handheld is
// one core kept busy. Read on the app thread, once a second, only while the help card is open.

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace apotheca {

// utime + stime from the text of /proc/PID/stat, in clock ticks; -1 if it cannot be read. The
// second field (the program's name, in brackets) may hold spaces and brackets: the fields are
// counted from the LAST ')'.
inline long statTicks(const char* text) {
    const char* close = text ? std::strrchr(text, ')') : nullptr;
    if (!close) return -1;
    // After ") ": field 3 (state) ... utime is field 14, stime 15.
    unsigned long utime = 0, stime = 0;
    const int read = std::sscanf(close + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime);
    return read == 2 ? static_cast<long>(utime + stime) : -1;
}

class ProgramCpu {
public:
    explicit ProgramCpu(std::string path = "/proc/self/stat") : path_(std::move(path)) {}
    void usePath(const std::string& path) { path_ = path; last_ = -1; }
    // Called with the time now (ms); reads the file at most once every kEveryMs. Returns true when
    // a new reading was made (percent() moved, or became known).
    static constexpr unsigned kEveryMs = 1000;
    bool read(unsigned nowMs) {
        if (last_ >= 0 && nowMs - lastMs_ < kEveryMs) return false;
        const long ticks = ticksNow();
        if (ticks < 0) return false;
        const bool had = last_ >= 0;
        if (had && nowMs > lastMs_) {
            const double seconds = (nowMs - lastMs_) / 1000.0, busy = static_cast<double>(ticks - last_) / ticksPerSecond_;
            percent_ = static_cast<int>(100.0 * busy / seconds / cores_ + 0.5);
        }
        last_ = ticks;
        lastMs_ = nowMs;
        return had;
    }
    // Start afresh (the card opened): the next reading is a baseline, a second later a number.
    void restart() { last_ = -1; percent_ = -1; }
    int percent() const { return percent_; } // -1: not known yet
    // For the tests: the clock's ticks a second and the cores (the machine's own by default).
    void pretend(double ticksPerSecond, int cores) { ticksPerSecond_ = ticksPerSecond, cores_ = cores; }

private:
    long ticksNow() const {
        std::FILE* file = std::fopen(path_.c_str(), "r");
        if (!file) return -1;
        char text[1024];
        const std::size_t got = std::fread(text, 1, sizeof text - 1, file);
        std::fclose(file);
        text[got] = 0;
        return statTicks(text);
    }
    std::string path_;
    long last_ = -1;
    unsigned lastMs_ = 0;
    int percent_ = -1;
    double ticksPerSecond_ = sysconf(_SC_CLK_TCK) > 0 ? static_cast<double>(sysconf(_SC_CLK_TCK)) : 100.0;
    int cores_ = sysconf(_SC_NPROCESSORS_ONLN) > 0 ? static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)) : 1;
};

} // namespace apotheca
