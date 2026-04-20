#include <util2/C/platform.h>
#include <util2/C/macro.h>
#include <util2/C/thread_sleep.h>
#include "cpuid.hpp"
#include <cstdint>
#include <cstdio>
#include <ctime>


#if defined(UTIL2_OS_LINUX)
#   define _GNU_SOURCE
#   include <sys/mman.h>
#   include <sched.h>
#   include <unistd.h>
#   include <errno.h>

#elif defined(UTIL2_OS_WINDOWS)
// #   define WIN32_LEAN_AND_MEAN
#   include <vmmdll.h>
#   include <array>
#   include <vector>
// #   undef WIN32_LEAN_AND_MEAN
#endif




namespace tailslayer::utilities {
#if defined(UTIL2_OS_WINDOWS)
    inline bool       g_enabledLockMemoryPrivileges = false;
    inline VMM_HANDLE g_initVMM = nullptr;
#endif /* */

    struct PhysicalMemRegion {
        ULONG64 vaddr;
        ULONG64 paddr;
        ULONG64 size;
    };

    struct AllocationRequest {
        void*  virtaddr;
        size_t sizeInBytes;
        size_t pageSize;
        bool   verifyContiguous = false;

        AllocationRequest() :
            virtaddr{nullptr},
            sizeInBytes{0},
            pageSize{4096},
            verifyContiguous{false}
        {}
        AllocationRequest(size_t allocationRequestSizeInBytes) : 
            virtaddr{nullptr},
            sizeInBytes{allocationRequestSizeInBytes},
            pageSize{4096},
            verifyContiguous{false}
        {}
    };


#if defined(UTIL2_OS_WINDOWS)
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


    inline BOOL SetLockMemoryPrivilege(bool enable) {
        HANDLE hToken;
        LUID luid;
        TOKEN_PRIVILEGES tp;
        if(g_enabledLockMemoryPrivileges && enable == true) {
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

        
        g_enabledLockMemoryPrivileges = true;
        CloseHandle(hToken);
        return TRUE;
    }


    inline BOOL InitializeVMMReader() {
        const char* args[] = { "-device", "pmem", "-v" };
    
        VMM_HANDLE hVMM = VMMDLL_Initialize(3, args);
        if (!hVMM) {
            perror("[-] Failed to init VMM. Is winpmem_x64.sys in the current working directory?\n");
            return false;
        }
        g_initVMM = hVMM;
        return true;
    }

    inline void DestroyVMMReader() {
        VMMDLL_Close(g_initVMM);
        g_initVMM = nullptr;
        return;
    }


    inline BOOL GetProcessorConfiguration(
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>& config
    ) {
        DWORD length = 0;
        
        if (!GetLogicalProcessorInformation(NULL, &length) && 
            GetLastError() != ERROR_INSUFFICIENT_BUFFER
        ) {
            fprintf(stderr, "Failed to get buffer size\n");
            return false;
        }

        config.resize(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (!GetLogicalProcessorInformation(config.data(), &length)) {
            fprintf(stderr, "Error retrieving processor information.\n");
            return false;
        }


        return true;
    }

    inline bool GetProcessorAffinities(std::vector<ULONG_PTR>& affinityMasks) {
        // Helper to get the first set bit in a mask (to pin to exactly one LP per core)
        static const auto skf_GetFirstLogicalProcessor = [](ULONG_PTR coreMask) -> ULONG_PTR {
            return coreMask & -static_cast<long long>(coreMask); // Returns the lowest set bit (e.g., 0x0011 -> 0x0001)
        };
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer;


        if(!GetProcessorConfiguration(buffer)) {
            fprintf(stderr, "Failed to get SYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer\n");
            return false;
        }

        for (const auto& info : buffer) {
            if (info.Relationship == RelationProcessorCore) {
                ULONG_PTR singleLPMask = skf_GetFirstLogicalProcessor(info.ProcessorMask);
                affinityMasks.push_back(singleLPMask);
            }
        }


        printf("Detected %llu Physical Cores\n", affinityMasks.size());
        return true;
    }

    inline void PrettyPrintProcessorInfo(
        const std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>& buffer
    ) {
        printf("--- Logical Processor Information ---\n");
        printf("%-34s | %-18s | %-18s | %s\n", "Relationship", "Relationship (Hexadecimal)", "Processor Mask", "Details");
        printf("---------------------------------------------------------------------------\n");

        for (const auto& info : buffer) 
        {
            char binaryStrBuf[65];
            _ui64toa_s(info.ProcessorMask, binaryStrBuf, 65, 2);
            printf("0b%-34s | ", binaryStrBuf);
            printf("0x%-18p | ", (void*)info.ProcessorMask);

            switch (info.Relationship) {
            case RelationProcessorCore:
                printf("%-18s | ", "Core");
                // info.ProcessorCore.Flags: 1 means functional units are shared (SMT/Hyperthreading)
                printf("SMT: %s", (info.ProcessorCore.Flags == 1) ? "Enabled" : "Disabled");
                break;

            case RelationNumaNode:
                printf("%-18s | ", "NUMA Node");
                printf("Node Number: %lu", info.NumaNode.NodeNumber);
                break;

            case RelationCache:
                printf("%-18s | ", "Cache");
                {
                    CACHE_DESCRIPTOR cache = info.Cache;
                    const char* type = "Unknown";
                    if (cache.Type == CacheUnified) type = "Unified";
                    else if (cache.Type == CacheInstruction) type = "Instruction";
                    else if (cache.Type == CacheData) type = "Data";
                    else if (cache.Type == CacheTrace) type = "Trace";

                    printf("L%u %s, Size: %lu KB, Line: %u bytes",
                        cache.Level, type, cache.Size / 1024, cache.LineSize);
                }
                break;

            case RelationProcessorPackage:
                printf("%-18s | ", "Package (Socket)");
                printf("Physical CPU Socket");
                break;

            default:
                printf("%-18s | ", "Other");
                printf("Unknown Relationship");
                break;
            }
            printf("\n");
        }
        printf("-------------------------------------------------------------------------------\n");
        return;
    }


    __force_inline inline BOOL SetProcessPriority(
        int32_t  _In_  newPriority = REALTIME_PRIORITY_CLASS,
        int32_t* _Out_ oldPriority = nullptr 
    ) {
        if(oldPriority) {
            *oldPriority = GetPriorityClass(GetCurrentProcess());
        }
        
        return SetPriorityClass(GetCurrentProcess(), newPriority);
    }

    __force_inline inline BOOL SetCurrentThreadPriority(
        int32_t  _In_  newPriority = THREAD_PRIORITY_TIME_CRITICAL,
        int32_t* _Out_ oldPriority = nullptr 
    ) {
        if(oldPriority) {
            *oldPriority = GetThreadPriority(GetCurrentThread());
        }
        return SetThreadPriority(GetCurrentThread(), newPriority);
    }

    __force_inline inline uint32_t GetNumberOfProcessorCores() {
        using LogicalProcInfoVector = std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>;
        DWORD                  length  = 0;
        DWORD                  numProc = 0;
        LogicalProcInfoVector  buffer;
        if (!GetLogicalProcessorInformation(NULL, &length) && 
            GetLastError() != ERROR_INSUFFICIENT_BUFFER
        ) {
            fprintf(stderr, "Failed to get buffer size.\n");
            return 0;
        }


        buffer.reserve(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (!GetLogicalProcessorInformation(buffer.data(), &length)) {
            fprintf(stderr, "Error retrieving processor information.\n");
            return 0;
        }

        for (const auto& info : buffer) {
            numProc += (info.Relationship == RelationProcessorCore);
        }
        return numProc;
    }


    __force_inline inline BOOL SetCurrentThreadProcessorID(int core_id) {
        // return SetProcessAffinityMask(GetCurrentProcess(), affinityMask) == false ? -1 : 0;
        
        /* https://stackoverflow.com/a/5919804 */
        DWORD_PTR affinityMask = 1 << core_id;
        return SetThreadAffinityMask(GetCurrentThread(), affinityMask);
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


    inline uint64_t VirtToPhys(uint64_t vaddr) {
        uint64_t physicalAddr = UINT64_MAX;
        bool status = VMMDLL_MemVirt2Phys(g_initVMM, GetCurrentProcessId(), vaddr, &physicalAddr);

        if(status == false) {
            perror("[-] Failed to Read Virtual Address\n");
            return UINT64_MAX;
        }
        fprintf(stderr, "[+] VA 0x%llx -> PA 0x%llx\n", vaddr, physicalAddr);


        // BYTE buf[0x100];
        // DWORD read = 0;
        // status = VMMDLL_MemReadEx(gs_initVMM, -1, 
        //     physicalAddr, 
        //     buf, 
        //     sizeof(buf), 
        //     &read, 
        //     VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO
        // );
        // if(!status) {
        //     perror("[-] Failed to Read Physical Address\n");
        //     return status;
        // }


        // fprintf(stderr, "[+] Read %lu Bytes from Physical memory\n", read);
        return physicalAddr;
    }

    inline bool verifyHugePageIsContiguous(uint64_t vaddrBegin, size_t sizeBytes) {
        if(!g_initVMM) {
            return false;
        }
        const size_t kMinimumBigPageSize = GetLargePageMinimum();
        SYSTEM_INFO si;
        std::array<ULONG64, 2> paddr{0, 0};
        bool                   isContiguous = true;
        bool                   opsuccess    = false;
        bool                   tmp          = true;
        
        
        GetSystemInfo(&si);
        fprintf(stderr, "GetMinimum() -> 0x%llx\nGetSystemInfo() -> 0x%llx, 0x%llx\n",
            kMinimumBigPageSize,
            (size_t)si.dwAllocationGranularity,
            (size_t)si.dwPageSize
        );
        if(sizeBytes % kMinimumBigPageSize != 0) {
            sizeBytes = (sizeBytes + kMinimumBigPageSize - 1) & ~(kMinimumBigPageSize - 1);
        }
        sizeBytes /= kMinimumBigPageSize;


        opsuccess = VMMDLL_MemVirt2Phys(g_initVMM, GetCurrentProcessId(), vaddrBegin, &paddr[0]);
        isContiguous = opsuccess; /* Incase of early exit the status will be correct */
        for(size_t lpage_offset = 1; lpage_offset < sizeBytes; ++lpage_offset) {
            opsuccess = VMMDLL_MemVirt2Phys(g_initVMM, GetCurrentProcessId(), 
                vaddrBegin + kMinimumBigPageSize * lpage_offset, 
                &paddr[lpage_offset % 2]
            );

            paddr[lpage_offset % 2] = (opsuccess == false) 
                ? 
                UINT64_MAX 
                : 
                paddr[lpage_offset % 2];
            
            tmp = (paddr[!(lpage_offset % 2)] + kMinimumBigPageSize == paddr[(lpage_offset % 2)]);
            isContiguous = isContiguous && tmp;

            
            fprintf(stderr, "v 0x%llx ->  p0x%llx | p 0x%llx + 0x%llx bytes == p 0x%llx ? -> %s\n", 
                vaddrBegin + kMinimumBigPageSize * lpage_offset, 
                paddr[lpage_offset % 2],

                paddr[!(lpage_offset % 2)], 
                kMinimumBigPageSize, 
                paddr[lpage_offset % 2],

                isContiguous ? "YES" : " NO"
            );
            if(opsuccess == false || !isContiguous) {
                break;
            }
        }


        return isContiguous;
    }

    inline PhysicalMemRegion FindLargestPhysicalRegion(
        VMM_HANDLE hVMM, 
        ULONG64    vaddr, 
        ULONG64    vregionSizeBytes, 
        ULONG64    vregionPageSizeBytes,
        ULONG64    desiredSizeBytes = 0 /* will find the biggest region by default */
    ) {
        const ULONG64 kPAGE_SIZE = vregionPageSizeBytes;
        desiredSizeBytes = (desiredSizeBytes == 0) ? UINT64_MAX : desiredSizeBytes;
        if(desiredSizeBytes < vregionPageSizeBytes) {
            return PhysicalMemRegion{};
        }


        PhysicalMemRegion maxRegion = { 0, 0, 0 };
        PhysicalMemRegion currentRegion = { 0, 0, 0 };
        ULONG64 currvaddr = 0;
        ULONG64 currpaddr = 0;
        bool    success  = true;


        for (ULONG64 offset = 0; offset < vregionSizeBytes; offset += kPAGE_SIZE) {
            currvaddr = vaddr + offset;
            currpaddr = UINT64_MAX;
            success  = VMMDLL_MemVirt2Phys(hVMM, GetCurrentProcessId(), currvaddr, &currpaddr);
            
            // 0 usually indicates the page isn't present/mapped
            if (!success || currpaddr == UINT64_MAX) {
                currentRegion = { 0, 0, 0 };
                continue;
            }

            // Check if this physical page follows the previous one
            if (currentRegion.size > 0 && currpaddr == currentRegion.paddr + currentRegion.size) {
                currentRegion.size += kPAGE_SIZE;
            } else {
                // Start of a new contiguous block
                currentRegion.vaddr = currvaddr;
                currentRegion.paddr = currpaddr;
                currentRegion.size = kPAGE_SIZE;
            }

            // Keep track of the biggest one found
            if (currentRegion.size > maxRegion.size) {
                maxRegion = currentRegion;
            }

            // Stop if we found a block large enough
            if (desiredSizeBytes > 0 && maxRegion.size >= desiredSizeBytes) {
                break;
            }
        }


        return maxRegion;
    }


    inline void allocateLargePageMin(AllocationRequest& out, PhysicalMemRegion& memBegin) 
    {
        /* https://stackoverflow.com/a/63391576 */
        /* Static const lambda functor */
        static const auto skf_getAvailablePhysicalMemory = []() -> DWORDLONG
        {
            MEMORYSTATUSEX status;
            status.dwLength = sizeof(status);
            GlobalMemoryStatusEx(&status);
            return status.ullAvailPhys;
        };


        PhysicalMemRegion physMem{};
        PVOID    outVirtAddr           = nullptr;
        size_t   allocAttemptSize      = 0;
        uint32_t memoryMultiplierBegin = 5;
        uint32_t memoryMultiplierEnd   = 80;
        bool     failure = true;

        out.pageSize = GetLargePageMinimum();
        for(uint32_t memMul = memoryMultiplierBegin; memMul < memoryMultiplierEnd; ++memMul) {
            allocAttemptSize = skf_getAvailablePhysicalMemory();
            allocAttemptSize = memMul * allocAttemptSize / 100;
            allocAttemptSize = (allocAttemptSize + out.pageSize - 1) & ~(out.pageSize - 1);
            
            outVirtAddr = VirtualAlloc(NULL, 
                allocAttemptSize,
                MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT,
                PAGE_READWRITE
            );
            if(outVirtAddr == nullptr) {
                PrintLastError("allocateLargePageMin Of Size %u Large Pages Failed.", allocAttemptSize / out.pageSize);
            }

            physMem = FindLargestPhysicalRegion(g_initVMM, 
                reinterpret_cast<ULONG64>(outVirtAddr), 
                allocAttemptSize, 
                out.pageSize
            );
            failure = (physMem.size < out.sizeInBytes);
            if(failure) { /* Alloc too small */
                if(outVirtAddr != nullptr) {
                    while(VirtualFree(outVirtAddr, 0, MEM_RELEASE) == false) {}
                }
            }
            else {
                break;
            }
        }

        out.virtaddr         = failure ? nullptr : outVirtAddr;
        out.sizeInBytes      = allocAttemptSize;
        out.verifyContiguous = true;
        memBegin = physMem;
        return;
    }


    inline void allocateLargePageMax(AllocationRequest& out) {
        MEM_ADDRESS_REQUIREMENTS addressReqs = {0};
        MEM_EXTENDED_PARAMETER extParams[2] = {};
        PVOID  outVirtAddr  = nullptr;
        size_t desiredAlloc = out.sizeInBytes;
        size_t allocAttemptSize = 0;

        /* https://stackoverflow.com/a/63391576 */
        /* Static const lambda functor */
        static const auto skf_getAvailablePhysicalMemory = []() -> DWORDLONG
        {
            MEMORYSTATUSEX status;
            status.dwLength = sizeof(status);
            GlobalMemoryStatusEx(&status);
            return status.ullAvailPhys;
        };


        out.pageSize = 1024 * 1024 * 1024; /* Typical Huge page size on windows */
        allocAttemptSize = static_cast<size_t>(skf_getAvailablePhysicalMemory() * 0.8);
        allocAttemptSize = (allocAttemptSize + out.pageSize - 1) & ~(out.pageSize - 1);
        desiredAlloc = (desiredAlloc + out.pageSize - 1) & ~(out.pageSize - 1);


        /* 
            1. Attempt to allocate with VirtualAlloc2(). Works on some machines.
                This atleast guarantees the backing physical-pages will be 1GiB in size 
        */
        /* Set up extended parameters for huge pages request */
        addressReqs.Alignment = out.pageSize;
        extParams[0].Type = MemExtendedParameterAddressRequirements;
        extParams[0].Pointer = &addressReqs;
        extParams[1].Type = MemExtendedParameterAttributeFlags;
        extParams[1].ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;
        // tailslayer::utilities::PrintLastError("VirtualAlloc2 Begin");
        outVirtAddr = VirtualAlloc2(GetCurrentProcess(), NULL, 
            allocAttemptSize,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE, 
            extParams, 
            2
        );
        // tailslayer::utilities::PrintLastError("VirtualAlloc2 End");


        if(outVirtAddr != nullptr && allocAttemptSize >= desiredAlloc) { /* We allocated enough memory */
            out.virtaddr  = outVirtAddr;
            out.sizeInBytes = allocAttemptSize;
            out.pageSize  = 1024ull * 1024 * 1024;
            out.verifyContiguous = false;
            return;
        }
        if(outVirtAddr != nullptr) { /* incase attempted request wasn't big enough */
            if(!VirtualFree(out.virtaddr, 0, MEM_RELEASE)) {
                tailslayer::utilities::PrintLastError("freeWithLargePages() Error");
            }
            outVirtAddr = nullptr;
        }


        /* 2. 
            Try with VirtualAlloc() with MEM_LARGE_PAGES Requirement. 
            Works on my machine way better.
            We'll need to verify & find the biggest contiguous physical region later.
        */
        uint32_t memoryMultiplier = 85;
        out.pageSize = GetLargePageMinimum();
        for(; outVirtAddr == nullptr && memoryMultiplier > 0; memoryMultiplier -= 5)
        {
            allocAttemptSize = skf_getAvailablePhysicalMemory();
            allocAttemptSize = memoryMultiplier * allocAttemptSize / 100;
            allocAttemptSize = (allocAttemptSize + out.pageSize - 1) & ~(out.pageSize - 1);
            // tailslayer::utilities::PrintLastError("Error Status");
            outVirtAddr = VirtualAlloc(NULL, 
                allocAttemptSize,
                MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT,
                PAGE_READWRITE
            );
            // tailslayer::utilities::PrintLastError("Error Status");
        }


        out.sizeInBytes = allocAttemptSize;
        out.virtaddr  = outVirtAddr;
        /* Non-paged Large Page Pools Are already locked in memory */
        // if(out.virtaddr) {
        //     bool status = VirtualLock(out.virtaddr, out.sizeInBytes);
        //     if(status == false) {
        //         VirtualFree(out.virtaddr, out.sizeInBytes, MEM_RELEASE);
        //         return;
        //     }
        // }
        return;
    }


    inline void allocateLargePage(AllocationRequest& out) {
        /* 
            Closest Equivalent to mmap(HUGE_TLB, ...) in Windows.
            No need to allocate a "file" and map it to a memory backing, 
            Lauries' mmap call is an allocation of a physically-contiguous 1GiB region of memory 
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
                The memory allocated using MEM_LARGE_PAGES is a private commit to the current process, nobody can see it.
        */
        MEM_ADDRESS_REQUIREMENTS addressReqs  = {0};
        MEM_EXTENDED_PARAMETER   extParams[2] = {};
        PVOID  outVirtAddr = nullptr;
        size_t lPageSize   = GetLargePageMinimum();
        

        addressReqs.Alignment = GetLargePageMinimum();
        extParams[0].Type = MemExtendedParameterAddressRequirements;
        extParams[0].Pointer = &addressReqs;
        extParams[1].Type = MemExtendedParameterAttributeFlags;
        extParams[1].ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;

        out.sizeInBytes = (out.sizeInBytes + lPageSize - 1) & ~(lPageSize - 1);
        outVirtAddr = VirtualAlloc2(GetCurrentProcess(), NULL, 
            out.sizeInBytes,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE, 
            extParams, 
            2
        );

        if(outVirtAddr != nullptr) {
            out.virtaddr = outVirtAddr;
            out.pageSize = 1024ull * 1024 * 1024;
            return;
        }


        outVirtAddr = VirtualAlloc(NULL, 
            out.sizeInBytes,
            MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE
        );
        
        out.virtaddr = outVirtAddr;
        out.pageSize = lPageSize;
        return;
    }


    inline void freeLargePage(AllocationRequest& out) {
        if(out.virtaddr == nullptr) {
            return;
        }
        if(!VirtualFree(out.virtaddr, 0, MEM_RELEASE)) {
            tailslayer::utilities::PrintLastError("freeWithLargePages() Error");
        }
        return;
    }

    inline bool LockMemoryRegion(void* memMappedAddress, size_t regionToLockSize) {
        bool status = VirtualLock(memMappedAddress, regionToLockSize);
        if(status == false) {
            PrintLastError("LockMemoryRegion (VirtualLock) Failed\n");
        }
        return status;
    }


#elif defined(UTIL2_OS_LINUX)
    __force_inline inline BOOL SetProcessPriority(
        int32_t  _In_  newPriority = -20,
        int32_t* _Out_ oldPriority = nullptr 
    ) {
        /* Lowest Priority (-20 [Highest] -> 19 [Lowest] ) */
        errno = 0;
        if(oldPriority) {
            /* oldprio=UINT32_MAX if getpriority fails */
            *oldPriority = getpriority(PRIO_PROCESS, 0);
            *oldPriority = ( (*oldPriority == -1) && (errno != 0) ) ? UINT32_MAX : *oldPriority;
            errno = 0;
        }

        /* https://linux.die.net/man/2/setpriority */
        status = setpriority(PRIO_PROCESS, 0, newPriority);
        return (status == -1) ? false : true;
    }

    __force_inline inline BOOL SetCurrentThreadPriority(
        int32_t  _In_  newPriority,
        int32_t* _Out_ oldPriority 
    ) {
        /* 
            https://man7.org/linux/man-pages/man2/sched_setscheduler.2.html
            https://man7.org/linux/man-pages/man2/sched_setparam.2.html
            https://man7.org/linux/man-pages/man2/sched_get_priority_min.2.html
        */
        struct sched_param policyPriority;
        auto policyID = sched_getscheduler();
        int32 sched_max = sched_get_priority_max(policyID);
        int32 sched_min = sched_get_priority_min(policyID);

        if(oldPriority) {
            sched_getparam(0, &policyPriority);
            *oldPriority = policyPriority.sched_priority;
        }

        if(newPriority > sched_max || newPriority < sched_min) {
            perror("SetCurrentThreadPriority() failed (Linux)\n")
            return false;
        }


        policyPriority.sched_priority = newPriority;
        return sched_setparam(0, &policyPriority) == 0 ? true : false;
    }

    __force_inline inline uint32_t GetNumberOfProcessorCores() {
        /* Very good answer: https://stackoverflow.com/a/50117787 */
        cpu_set_t mask;

        if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
            perror("sched_getaffinity");
            return 0;
        }
        return static_cast<uint32_t>(CPU_COUNT(&mask));
    }

    __force_inline inline bool SetCurrentThreadProcessorID(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return sched_setaffinity(gettid(), sizeof(cpuset), &cpuset) == 0 ? true : false;
    }

    __force_inline inline int clock_gettime_monotonic(struct timespec *tv) {
        /* See: https://linux.die.net/man/3/clock_gettime */
        return clock_gettime(CLOCK_MONOTONIC, &tv);
    }


    inline uint64_t VirtToPhys(uint64_t vaddr) {
        int fd = open("/proc/self/pagemap", O_RDONLY);
        if (fd < 0) {
            return UINT64_MAX;
        }

        uint64_t entry;
        off_t offset = (vaddr / 4096) * 8;
        
        if (pread(fd, &entry, 8, offset) != 8) { 
            close(fd); 
            return UINT64_MAX; 
        }
        
        close(fd);
        if (!(entry & (1ULL << 63))) {
            return UINT64_MAX;
        }

        uint64_t pfn = entry & ((1ULL << 55) - 1);
        return (pfn * 4096) | (vaddr & 0xFFF);
    }

    /* If HugeTLB Works there is no point in all of these shenanigans */
    inline bool verifyHugePageIsContiguous(uint64_t vaddrBegin, size_t sizeBytes) {
        return true; /* HUGE_TLB pages are contiguous in memory */
    }

    inline PhysicalMemRegion FindLargestPhysicalRegion(
        VMM_HANDLE hVMM, 
        uint64_t   vaddr, 
        uint64_t   vregionSizeBytes, 
        uint64_t   vregionPageSizeBytes,
        uint64_t   desiredSizeBytes = 0
    ) {
        /* 
            There is currently no fallback to searching the virtual memory region in linux. 
            Either the allocation succeeds or it fails & we exit.
        */
        return PhysicalMemRegion{nullptr, nullptr, 0};
    }


    inline void allocateLargePage(AllocationRequest& req) {
        /* See: https://linux.die.net/man/2/munmap */
        req.pageSize = 1024ull * 1024 * 1024;
        void* out = mmap(nullptr, 
            req.sizeInBytes, 
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (30 << MAP_HUGE_SHIFT), 
            -1, 
            0
        );


        out.virtaddr = (out == MAP_FAILED) ? nullptr : out;
    }

    inline void freeLargePage(void* address, size_t sizeAllocated) {
        int status = munmap(address, sizeAllocated);
        if(status == -1) { /* See: https://stackoverflow.com/a/504039 */
            std::fprintf(stderr, "freeLargePage (munmap) Failed, Error Message (Code=%lu):\n    %s\n", 
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

    __force_inline inline int compute_channel(uint64_t physAddr, int channel_bit) {
        return (physAddr >> channel_bit) & 1;
    }

    __force_inline inline bool CheckCPUSupportForHugePages() {
        constexpr uint32_t hugePageBit = 1 << 26;
        CPUID info{0x80000001};

        return (info.EDX() & hugePageBit) != 0;
    }



} // namespace tailslayer::utilities
