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
inline int helpersFor(unsigned cores) { return cores >= 4 ? 2 : (cores >= 2 ? 1 : 0); }

} // namespace apotheca
