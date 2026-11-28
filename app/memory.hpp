#pragma once
// The memory guard (2026-11-28): how much memory the system still has for recordings, so a load that would not
// fit is refused and said, never let run the handheld out of memory (where the system kills the program, with no
// word and the song's unsaved work gone). The program locks every page it uses in RAM (realtime.cpp: mlockall),
// so what it takes is taken from the 1 GB the handheld shares with its system.
//
// What the recordings themselves hold is counted by the Session (Session::recordingBytes) and the Engine
// (Engine::sampleBytes); this is the system's side: MemAvailable, less a margin kept for the rest.

#include <functional>

namespace apotheca {
namespace memory {

constexpr long long kMegabyte = 1024LL * 1024LL;
// Kept free after any load, for what is never asked first: a bounce's second engine (21 MB with its catch rings),
// a file sent over WiFi (30 MB at most, and its check), a FIT on the helper thread (two copies of a 20 s stereo
// sound and the stretch's own), SAVE A COPY's or CATCH's file in memory (up to 8 MB), undo's texts, and the
// system's other programs. drums-and-samples.md, "The memory guard".
constexpr long long kMargin = 96 * kMegabyte;

// The system's MemAvailable (/proc/meminfo), in bytes; -1 where there is none to read (not Linux, unreadable).
long long systemAvailable();
// For tests and stills: the figure to use in place of the system's (bytes; below 0: no guard). Empty: the system's.
void pretend(std::function<long long()> figure);
long long available(); // the pretended figure, or the system's
// What a load may still take: available() less the margin, never below 0. -1: nothing is known, nothing refused.
long long room();
// Before anything is loaded (main): memory a recording held is given back to the system when it is let go. With
// the allocator's own rule (its mmap threshold rises to the largest block freed), a freed 20 s recording stays
// with the program and MemAvailable never rises again: measured, drums-and-samples.md.
void settle();
// Bytes as whole megabytes, rounded up (a byte over is a megabyte said).
int megabytes(long long bytes);

} // namespace memory
} // namespace apotheca
