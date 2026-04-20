#pragma once
#include <tailslayer/utilities.hpp>
#include <util2/C/platform.h>
#include <util2/C/thread_sleep.h>


#if defined(UTIL2_OS_LINUX)
#   include <sys/mman.h>
#   include <sched.h>
#   include <unistd.h>
#elif defined(UTIL2_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#endif


namespace tslayer = tailslayer::utilities;


struct spike {
    uint64_t tsc;
    uint64_t latency;
};


static inline uint64_t timed_probe(
    volatile char *addr
) {
    tslayer::clflush_addr(addr);
    tslayer::mfence_inst();
    tslayer::lfence_inst();
    uint64_t t0 = tslayer::rdtsc_lfence();
    *(volatile char *)addr;
    uint64_t t1 = tslayer::rdtscp_lfence();
    return t1 - t0;
}