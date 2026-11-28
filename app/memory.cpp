#include "memory.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace apotheca {
namespace memory {

namespace {
std::function<long long()>& pretended() {
    static std::function<long long()> figure;
    return figure;
}
} // namespace

long long systemAvailable() {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    long long kb = -1;
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, "MemAvailable:", 13) == 0) {
            char* end = nullptr;
            kb = std::strtoll(line + 13, &end, 10);
            if (end == line + 13) kb = -1; // (no number: as unreadable)
            break;
        }
    std::fclose(f);
    return kb < 0 ? -1 : kb * 1024LL;
#else
    return -1;
#endif
}

void pretend(std::function<long long()> figure) { pretended() = std::move(figure); }

long long available() {
    const std::function<long long()>& figure = pretended();
    return figure ? figure() : systemAvailable();
}

long long room() {
    const long long have = available();
    if (have < 0) return -1;
    return have > kMargin ? have - kMargin : 0;
}

void settle() {
#if defined(__GLIBC__)
    // A block of 128 KB or more (a recording of a third of a second) is its own mapping, given back when freed.
    // Set at all, the threshold no longer moves by itself (glibc's dynamic threshold is turned off).
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
#endif
}

int megabytes(long long bytes) {
    if (bytes <= 0) return 0;
    return static_cast<int>((bytes + kMegabyte - 1) / kMegabyte);
}

} // namespace memory
} // namespace apotheca
