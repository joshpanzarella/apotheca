// What the program was doing when it died or froze, written to the card for the owner to send (2026-11-25: the
// firmware's Apotheca stopped with a trap, code 133, after locking up on the FX screen's FILTER, and nothing on the
// desk or under qemu did it).
//
//   install(folder)   once, on the handheld: a death by a signal (a trap, a fault, an abort) writes
//                     folder/crash.txt; a main loop that has not passed for kStallMs writes a STALL there too,
//                     with where the loop is stuck, and again every kStallAgainMs while it stays stuck
//   alive()           each pass of the main loop
//   note(...)         a line for the trail: the last kTrail of them go into every report (the presses, the screen)
//
// Each report has the signal, the program counter and the return address, the call chain as glibc's backtrace()
// finds it (addresses and names), the trail, and the process's memory map, so an address in a library can be
// looked up against the build's own copy. Nothing here allocates once installed: the handler writes with write().
#pragma once

#include <string>

namespace crash {

constexpr int kTrail = 48;
constexpr unsigned kStallMs = 3000, kStallAgainMs = 10000;

void install(const std::string& folder);
void alive();
void note(const char* format, ...) __attribute__((format(printf, 1, 2)));

} // namespace crash
