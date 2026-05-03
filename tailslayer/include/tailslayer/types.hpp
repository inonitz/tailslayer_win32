#ifndef __TAILSLAYER_UTILITY_HEADER_TYPES_DEFINITION__
#define __TAILSLAYER_UTILITY_HEADER_TYPES_DEFINITION__
#   include <util2/C/platform.h>
#   if defined(UTIL2_OS_LINUX)
#       define _GNU_SOURCE
#       include <sched.h>
#   elif defined(UTIL2_OS_WINDOWS)
#       include <vmmdll.h>
#endif



namespace tailslayer::util {
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
} // namespace tailslayer::util


#endif /* __TAILSLAYER_UTILITY_HEADER_TYPES_DEFINITION__ */
