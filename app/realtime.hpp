#pragma once

namespace apotheca {

// Ask the OS to run the calling thread ahead of every normal thread
// (SCHED_FIFO on Linux) and to keep the program's memory from being paged
// out. Returns true if the realtime priority was granted. Does nothing and
// returns false on other systems, where the audio library sets its own.
bool makeThreadRealtime(int priority);

// How many helper threads work out tracks beside the audio thread on a machine
// with this many cores (Engine::useWorkers): one core is left for the screen
// and everything else. The program and the SPEED TEST both ask here.
// Three helpers on four cores (owner's SPEED TEST, 0.573, 2026-11-16: the busiest song 59.5% of a
// block on two, 53.8% on three, both with 0 of 1875 blocks late; the screen's thread shares the
// fourth core with the audio thread's own work).
inline int helpersFor(unsigned cores) { return cores >= 4 ? 3 : (cores >= 2 ? 1 : 0); }

} // namespace apotheca
