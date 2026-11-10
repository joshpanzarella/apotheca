#pragma once

namespace apotheca {

// Ask the OS to run the calling thread ahead of every normal thread
// (SCHED_FIFO on Linux) and to keep the program's memory from being paged
// out. Returns true if the realtime priority was granted. Does nothing and
// returns false on other systems, where the audio library sets its own.
bool makeThreadRealtime(int priority);

} // namespace apotheca
