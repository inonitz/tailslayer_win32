#include <util2/C/platform.h>
#include <util2/C/macro.h>
#include <util2/C/thread_sleep.h>
#include <cstdint>
#include <cstdio>
#include <ctime>


#if defined(UTIL2_OS_LINUX)
#   include <sys/mman.h>
#   include <sched.h>
#   include <unistd.h>
#   include <errno.h>

#elif defined(UTIL2_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <memoryapi.h>
#   undef WIN32_LEAN_AND_MEAN
#endif




namespace tailslayer::utilities {
#if defined(UTIL2_OS_WINDOWS)
    inline bool enabledLockMemoryPrivileges = false;
#endif /* */


    __force_inline inline void clflush_addr(volatile void *addr) {
        __asm__ volatile("clflush (%0)" :: "r"(addr) : "memory");
    }

    __force_inline inline void mfence_inst() {
        __asm__ volatile("mfence" ::: "memory");
    }

    __force_inline inline void lfence_inst()
    {
        __asm__ volatile("lfence" ::: "memory");
    }

    __force_inline inline uint64_t rdtsc_lfence() {
        uint64_t lo, hi;
        __asm__ volatile("\n\t\
            lfence\n\t"
            "rdtsc"
            : 
            "=a"(lo), 
            "=d"(hi)
        );
        return (hi << 32) | lo;
    }

    __force_inline inline uint64_t rdtscp_lfence() {
        uint64_t lo, hi;
        uint32_t aux;
        __asm__ volatile("rdtscp" : 
            "=a"(lo), 
            "=d"(hi), 
            "=c"(aux)
        );
        __asm__ volatile("lfence" ::: "memory");
        return (hi << 32) | lo;
    }


#ifdef UTIL2_OS_WINDOWS
    /* Big Thanks to: https://stackoverflow.com/a/45565001 */
    inline BOOL GetErrorMessage(DWORD dwErrorCode, LPTSTR pBuffer, DWORD cchBufferLength)
    {
        if (cchBufferLength == 0) {
            return FALSE;
        }
        DWORD cchMsg = FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,  /* (not used with FORMAT_MESSAGE_FROM_SYSTEM) */
            dwErrorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            pBuffer,
            cchBufferLength,
            NULL
        );
        return (cchMsg > 0);
    }

    inline BOOL SetLockMemoryPrivilege(bool enable) {
        HANDLE hToken;
        LUID luid;
        TOKEN_PRIVILEGES tp;
        if(enabledLockMemoryPrivileges && enable == true) {
            return TRUE;
        }


        // 1. Open the process token for "Adjusting" privileges
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            fprintf(stderr, "OpenProcessToken failed: %lu\n", GetLastError());
            return FALSE;
        }

        // 2. Look up the LUID (Locally Unique Identifier) for the Lock Memory privilege string
        if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
            fprintf(stderr, "LookupPrivilegeValue failed: %lu\n", GetLastError());
            CloseHandle(hToken);
            return FALSE;
        }

        // 3. Set up the privilege structure
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_REMOVED;
        
        // 4. Apply the change to the token
        if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
            fprintf(stderr, "AdjustTokenPrivileges failed: %lu\n", GetLastError());
            CloseHandle(hToken);
            return FALSE;
        }

        // 5. AdjustTokenPrivileges can return TRUE even if it failed to enable the privilege. 
        // You MUST check GetLastError() to confirm it actually succeeded.
        if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            fprintf(stderr, "The privilege 'SeLockMemoryPrivilege' is not assigned to this user.\n");
            fprintf(stderr, "Please add it via secpol.msc and Relogin to apply changes.\n");
            CloseHandle(hToken);
            return FALSE;
        }

        
        enabledLockMemoryPrivileges = true;
        CloseHandle(hToken);
        return TRUE;
    }

    inline void PrintLastError(const char* format, ...) {
        if(!UTIL2_DEBUG_BUILD) {
            return;
        }
        va_list arg_list;
        va_start(arg_list, format);
        vfprintf(stderr, format, arg_list);
        va_end(arg_list);


        static TCHAR errBuf[1024] = {0};
        BOOL  status = false;
        DWORD errCode = GetLastError();
        status = GetErrorMessage(errCode, errBuf, 1024);
        fprintf(stderr, "    Optional System Message (Windows errCode=%lu): %s\n", 
            (unsigned long)errCode,
            status > 0 ? errBuf : "None"
        );
        return;
    }

    inline int clock_gettime_monotonic(struct timespec *tv)
    {
        /* See: https://stackoverflow.com/a/51974214 */
        static constexpr auto kNS_PER_SEC = 1000 * 1000 * 1000;
        static LARGE_INTEGER ticksPerSec;
        LARGE_INTEGER ticks;

        if (!ticksPerSec.QuadPart) {
            QueryPerformanceFrequency(&ticksPerSec);
            if (!ticksPerSec.QuadPart) {
                errno = ENOTSUP;
                return -1;
            }
        }

        QueryPerformanceCounter(&ticks);

        tv->tv_sec = (long)(ticks.QuadPart / ticksPerSec.QuadPart);
        tv->tv_nsec = (long)(((ticks.QuadPart % ticksPerSec.QuadPart) * kNS_PER_SEC) / ticksPerSec.QuadPart);
        return 0;
    }


    __force_inline inline int pin_to_core(int core_id) {
        DWORD_PTR affinityMask = 1 << core_id;
        return SetProcessAffinityMask(GetCurrentProcess(), affinityMask) == false ? -1 : 0;
    }


    inline void* allocateHugePages(size_t desiredSize) {
        /* 
            Closest Equivalent in Windows.
            No need to allocate a "file" and map it to a memory backing, 
            Lauries' mmap call was just interested in allocating a contiguous physical 1GiB region of memory 
            * MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT - 
                Try to allocate large (> 2MiB Pages), and Commit all the pages requested Right Now
                (no need to call virtualAlloc on the requested region to actually back the memory with physical pages later)

            * MEM_LARGE_PAGES - MAP_HUGETLB (kinda)
                [NOTE]: We also need to supply 'MEM_EXTENDED_PARAMETER_NONPAGED_HUGE'
                        See -> https://stackoverflow.com/a/71543569

            * MEM_RESERVE | MEM_COMMIT - Allocate all the physical memory now, 
                and reserve it to the allocated virtual memory region

            * PAGE_READWRITE - PROT_READ | PROT_WRITE

            * MAP_PRIVATE & MAP_ANONYMOUS - 
                MAP_ANON doesn't map the memory contents to a file (specified by the later fd & offset parameters);
                we just allocate memory, so there's no file mapping (yet lol)

                MAP_PRIVATE means that other processes won't see changes to the mapped file
                also less relevant, I'll assume I don't need to address this flag on windows (unless everything breaks lol)
        */

        size_t largepageSize = GetLargePageMinimum();
        if (desiredSize % largepageSize != 0) {
            // Round up to nearest multiple
            desiredSize = (desiredSize + largepageSize - 1) & ~(largepageSize - 1);
        }

        /* Make sure there's privilege to lock the physical memory s.t it won't be swapped to disk */
        if(!SetLockMemoryPrivilege(true)) {
            PrintLastError("Could not acquire SeLockMemoryPrivilege. Large pages will fail\n");
            return nullptr;
        }


        MEM_EXTENDED_PARAMETER extended {};
        memset(&extended, 0x00, sizeof(MEM_EXTENDED_PARAMETER));
        extended.Type = MemExtendedParameterAttributeFlags;
        extended.ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;

        // PVOID out = VirtualAlloc2(GetCurrentProcess (), NULL, 
        //     desiredSize,
        //     MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT,
        //     PAGE_READWRITE, 
        //     &extended, 
        //     1
        // );
        // PrintLastError("allocateHugePages (VirtualAlloc2) Failed\n");
        // PVOID out = VirtualAlloc2(GetCurrentProcess (), NULL, 
        //     desiredSize,
        //     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
        //     PAGE_READWRITE, 
        //     &extended, 
        //     1
        // );
        PrintLastError("");
        PVOID out = VirtualAlloc(NULL, desiredSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);

        if(out == nullptr) {
            PrintLastError("allocateHugePages (VirtualAlloc2) Failed\n");
        }
        return out;
    }

    inline void freeHugePages(void* address, size_t sizeAllocated) {
        if(!address) {
            return;
        }
        bool status = VirtualFree(address, 0, MEM_RELEASE);

        if(status == 0) {
            PrintLastError("freeHugePages (VirtualFree) Failed\n");
        }
        return;
    }

    inline bool LockMemoryRegion(void* memMappedAddress, size_t regionToLockSize) {
        bool status = VirtualLock(memMappedAddress, regionToLockSize);
        if(status == 0) {
            PrintLastError("LockMemoryRegion (VirtualLock) Failed\n");
        }
        return status;
        return true;
    }


#elif defined(UTIL2_OS_LINUX) 
    inline int pin_to_core(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return sched_setaffinity(0, sizeof(cpuset), &cpuset);
    }

    inline int clock_gettime_monotonic(struct timespec *tv) {
        /* See: https://linux.die.net/man/3/clock_gettime */
        return clock_gettime(CLOCK_MONOTONIC, &tv);
    }

    inline void* allocateHugePages(size_t desiredSize) {
        /* See: https://linux.die.net/man/2/munmap */
        void* out = mmap(nullptr, 
            desiredSize, 
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (30 << MAP_HUGE_SHIFT), 
            -1, 
            0
        );
        return out == MAP_FAILED ? nullptr : out;
    }

    inline void freeHugePages(void* address, size_t sizeAllocated) {
        int status = munmap(address, sizeAllocated);
        if(status == -1) { /* See: https://stackoverflow.com/a/504039 */
            std::fprintf(stderr, "freeHugePages (munmap) Failed, Error Message (Code=%lu):\n    %s\n", 
                (unsigned long)errno,
                strerror(errno)
            );
        }
        return;
    }

    inline bool LockMemoryRegion(void* memMappedAddress, size_t regionToLockSize) {
        /* See: https://man7.org/linux/man-pages/man2/mlock.2.html */
        int status = mlock(memMappedAddress, regionToLockSize);
        if(status == -1) {
            std::fprintf(stderr, "LockMemoryRegion (mlock) Failed, Error Message (Code=%lu):\n    %s\n", 
                (unsigned long)errno,
                strerror(errno)
            );
        }
        return status != -1;
    }

#endif /* UTIL2_OS_LINUX */


    inline double CalibrateTimestampCounterGhz()
    {
        struct timespec t0, t1;
        clock_gettime_monotonic(&t0);
        uint64_t tsc0 = rdtsc_lfence();

        util2_thread_sleep(100000000);

        uint64_t tsc1 = rdtscp_lfence();
        clock_gettime_monotonic(&t1);

        double elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1e9 +
                            (t1.tv_nsec - t0.tv_nsec);
        return (double)(tsc1 - tsc0) / elapsed_ns;
    }


} // namespace tailslayer::utilities
