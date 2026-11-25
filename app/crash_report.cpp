#include "crash_report.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>

namespace crash {

namespace {

char gPath[512] = {};
pthread_t gMain;
std::atomic<unsigned long long> gAliveMs{0};
std::atomic<bool> gInstalled{false};

char gTrail[kTrail][112] = {};
std::atomic<unsigned> gTrailNext{0};

unsigned long long nowMs() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<unsigned long long>(t.tv_sec) * 1000ull + static_cast<unsigned long long>(t.tv_nsec) / 1000000ull;
}

void put(int fd, const char* text) {
    size_t left = std::strlen(text);
    while (left > 0) {
        const ssize_t n = write(fd, text, left);
        if (n <= 0) return;
        text += n;
        left -= static_cast<size_t>(n);
    }
}

// A number in hex, without printf (not safe in a handler).
void putHex(int fd, unsigned long long v) {
    char out[19] = "0x";
    char digits[16];
    int n = 0;
    do {
        digits[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v && n < 16);
    for (int i = 0; i < n; ++i) out[2 + i] = digits[n - 1 - i];
    out[2 + n] = 0;
    put(fd, out);
}

void putDec(int fd, unsigned long long v) {
    char digits[24];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v && n < 23);
    char out[24];
    for (int i = 0; i < n; ++i) out[i] = digits[n - 1 - i];
    out[n] = 0;
    put(fd, out);
}

void report(int sig, siginfo_t* info, void* context, bool stall) {
    const int fd = open(gPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    put(fd, stall ? "\n===== STALL: the main loop has not passed for " : "\n===== CRASH: signal ");
    if (stall) {
        putDec(fd, nowMs() - gAliveMs.load());
        put(fd, " ms");
    } else {
        putDec(fd, static_cast<unsigned long long>(sig));
        put(fd, " (");
        put(fd, sig == SIGTRAP ? "SIGTRAP" : sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : "?");
        put(fd, ") code ");
        putDec(fd, info ? static_cast<unsigned long long>(info->si_code) : 0ull);
        put(fd, " address ");
        putHex(fd, info ? reinterpret_cast<unsigned long long>(info->si_addr) : 0ull);
    }
    put(fd, "\nuptime ms ");
    putDec(fd, nowMs());
    put(fd, "\n");
#if defined(__aarch64__)
    if (context) {
        const ucontext_t* uc = static_cast<const ucontext_t*>(context);
        put(fd, "pc ");
        putHex(fd, uc->uc_mcontext.pc);
        put(fd, " lr ");
        putHex(fd, uc->uc_mcontext.regs[30]);
        put(fd, " sp ");
        putHex(fd, uc->uc_mcontext.sp);
        put(fd, "\n");
    }
#else
    (void)context;
#endif
    put(fd, "--- call chain\n");
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
    put(fd, "--- trail (oldest first)\n");
    const unsigned next = gTrailNext.load();
    for (unsigned i = 0; i < static_cast<unsigned>(kTrail); ++i) {
        const char* line = gTrail[(next + i) % kTrail];
        if (!line[0]) continue;
        put(fd, line);
        put(fd, "\n");
    }
    put(fd, "--- memory map\n");
    const int maps = open("/proc/self/maps", O_RDONLY);
    if (maps >= 0) {
        char buffer[1024];
        ssize_t n;
        while ((n = read(maps, buffer, sizeof buffer)) > 0)
            if (write(fd, buffer, static_cast<size_t>(n)) != n) break;
        close(maps);
    }
    fsync(fd);
    close(fd);
}

void onFatal(int sig, siginfo_t* info, void* context) {
    report(sig, info, context, false);
    signal(sig, SIG_DFL); // then die as before, so the supervisor sees the same code
    raise(sig);
}

void onStall(int sig, siginfo_t* info, void* context) { report(sig, info, context, true); }

void watch() {
    unsigned long long reportedAt = 0;
    for (;;) {
        usleep(250000);
        const unsigned long long now = nowMs(), alive = gAliveMs.load();
        if (alive == 0 || now - alive < kStallMs) {
            reportedAt = 0;
            continue;
        }
        if (reportedAt == 0 || now - reportedAt >= kStallAgainMs) {
            reportedAt = now;
            pthread_kill(gMain, SIGUSR2); // the main thread writes where it is
        }
    }
}

} // namespace

void install(const std::string& folder) {
    if (gInstalled.exchange(true)) return;
    std::snprintf(gPath, sizeof gPath, "%s/crash.txt", folder.c_str());
    gMain = pthread_self();
    void* warm[2];
    backtrace(warm, 2); // loads the unwinder now, not inside a handler
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction fatal {};
    fatal.sa_sigaction = onFatal;
    fatal.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&fatal.sa_mask);
    for (int sig : {SIGTRAP, SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &fatal, nullptr);
    struct sigaction stall {};
    stall.sa_sigaction = onStall;
    stall.sa_flags = SA_SIGINFO | SA_RESTART; // (on the main thread's own stack: its frames are what is wanted)
    sigemptyset(&stall.sa_mask);
    sigaction(SIGUSR2, &stall, nullptr);
    alive();
    std::thread(watch).detach();
    note("started");
}

void alive() { gAliveMs.store(nowMs()); }

void note(const char* format, ...) {
    if (!gInstalled.load()) return;
    const unsigned at = gTrailNext.fetch_add(1) % kTrail;
    char* line = gTrail[at];
    const int head = std::snprintf(line, sizeof gTrail[0], "%llu ", nowMs());
    va_list args;
    va_start(args, format);
    if (head > 0 && head < static_cast<int>(sizeof gTrail[0])) std::vsnprintf(line + head, sizeof gTrail[0] - static_cast<size_t>(head), format, args);
    va_end(args);
}

} // namespace crash
