#include "realtime.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace apotheca {

bool makeThreadRealtime(int priority) {
#if defined(__linux__)
    mlockall(MCL_CURRENT | MCL_FUTURE); // best effort; fine if refused
    sched_param param = {};
    param.sched_priority = priority;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
    (void)priority;
    return false;
#endif
}

} // namespace apotheca
