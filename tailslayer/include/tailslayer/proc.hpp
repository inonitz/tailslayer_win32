#ifndef __UTILITY_HEADER_PROCESSOR_CONFIG_DETECTION_HEADER_DEFINITION__
#define __UTILITY_HEADER_PROCESSOR_CONFIG_DETECTION_HEADER_DEFINITION__
#   include <vector>
#   include <unordered_map>
#   include <cstdint>
#   include <cstdio>
#   include <cstring>
#   if defined(__linux__)
#       ifndef _GNU_SOURCE
#           define _GNU_SOURCE
#       endif
#       include <sched.h>
#   elif defined(_WIN32)
#       define WIN32_LEAN_AND_MEAN
#       include <Windows.h>
#       undef WIN32_LEAN_AND_MEAN
#endif


#ifndef __force_inline
#   ifdef _MSC_VER
#       define __force_inline __forceinline
#   elif defined(__clang__) || defined(__GNUC__)
#       define __force_inline __attribute__((always_inline))
#   else
#       define __force_inline inline
#   endif
#endif


#if defined(_WIN32)


typedef GROUP_AFFINITY NativeAffinityMask;


struct GroupPair {
    WORD      m_group;
    KAFFINITY m_groupMask;

    bool operator==(NativeAffinityMask const& mask) const noexcept {
        return (m_group == mask.Group) && (m_groupMask & mask.Mask);
    }
    bool operator==(GroupPair const& other) const noexcept {
        return (m_group == other.m_group) && (m_groupMask == other.m_groupMask);
    }
};


/* Thank you for the great answer: https://stackoverflow.com/a/17017281 */
namespace std {

template<> struct hash<GroupPair> {
    size_t operator()(const GroupPair& k) const
    {
        // Compute individual hash values for first,
        // second and third and combine them using XOR
        // and bit shifting:
        // http://stackoverflow.com/a/1646913/126995
        size_t res = 17;
        res = res * 31 + hash<WORD>()( k.m_group );
        res = res * 31 + hash<KAFFINITY>()( k.m_groupMask );
        return res;
    }
};

} /* namespace std */


#elif defined(__linux__)

typedef cpu_set_t NativeAffinityMask;

#endif /* Operating System Specific */




/*
    @m_globalID:
        Unique ID for the logical processor, after parsing OS-specific structures. 

    @m_coreID:
        Unique ID for the physical core

    @m_pkgID:
        Unique ID for the physical socket

    @m_usable: 
        CGroups On Linux & CPU-Sets/Affinity-Masks On Windows may
        initially not give access to specific cores

    @m_affinityMask:
        the mask to use when calling
        * sched_setaffinity(...) on Linux 
        * SetThreadAffinityMask(...) On Windows
*/
struct LogicalProcessor {
    uint32_t           m_globalID;
    uint32_t           m_coreID;
    uint16_t           m_pkgID;
    uint8_t            m_usable;
    uint8_t            m_reserved[1];
    NativeAffinityMask m_affinityMask;


    __force_inline uint64_t uniqueCoreID() const {
        uint64_t out = m_coreID;
        uint64_t out2 = m_pkgID;

        out2 <<= 32;
        return out | out2;
    }
};


class ProcessorConfigurationVector 
{
public:
    using value_type = std::vector<LogicalProcessor>;

public:
    __force_inline bool initialize() {
        m_proc.clear();
        return ProcessCurrentConfiguration();
    }

    __force_inline void destroy() {
        m_proc.clear();
        return;
    }

    __force_inline void copy(ProcessorConfigurationVector const& other) {
        if(this == &other) {
            return;
        }
        m_proc              = other.m_proc;
        m_threadCount       = other.m_threadCount;
        m_physicalCoreCount = other.m_physicalCoreCount;
        m_corePackageCount  = other.m_corePackageCount;
        return;
    }

    __force_inline auto& data() const noexcept {
        return m_proc;
    }

    __force_inline uint32_t physicalCoreCount() const noexcept {
        return m_proc.empty() ? UINT32_MAX : m_physicalCoreCount;
    }
    __force_inline uint32_t logicalCoreCount() const noexcept {
        return m_proc.empty() ? UINT32_MAX : m_threadCount;
    }
    __force_inline uint32_t packageCount() const noexcept {
        return m_proc.empty() ? UINT32_MAX : m_corePackageCount;
    }

    __force_inline void PrintTopology() const {
#if defined(_WIN32)
        std::fprintf(stdout, "GlobalID | PackageID | CoreID | Group | Mask\n");
        std::fprintf(stdout, "--------------------------------------------\n");
        for (const auto& lp : m_proc) {
            printf("%8u | %9u | %6u | %5u | 0x%llx\n", 
                lp.m_globalID, 
                lp.m_pkgID, 
                lp.m_coreID, 
                lp.m_affinityMask.Group, 
                lp.m_affinityMask.Mask
            );
        }
#elif defined(__linux__)
        std::fprintf(stdout, "GlobalID | PackageID | CoreID | CPU-Set Core Masks\n");
        std::fprintf(stdout, "--------------------------------------------\n");
        for (const auto& lp : m_proc) {
            std::fprintf(stdout, "%8u | %9u | %6u |", 
                lp.m_globalID, 
                lp.m_pkgID, 
                lp.m_coreID
            );
            for (uint32_t i = 0; i < sizeof(lp.m_affinityMask) - 1; ++i) {
                if (CPU_ISSET(i, &lp.m_affinityMask)) {
                    std::fprintf(stdout, "%d, ", i);
                }
                
            }
            if (CPU_ISSET(sizeof(lp.m_affinityMask) - 1, &lp.m_affinityMask)) {
                std::fprintf( stdout, "%lu\n", static_cast<size_t>(sizeof(lp.m_affinityMask) - 1) );
            }
        }
#endif
    }

    void getPhysicalCoreThreadAffinity(
        uint32_t coreIndex,
        std::vector<NativeAffinityMask>& threadMasks
    ) const noexcept;


private:
    static bool GetProcessorNativeConfiguration(
        std::vector<uint8_t>& buffer
    );
    bool ProcessCurrentConfiguration();

private:
    std::vector<LogicalProcessor> m_proc;
    uint32_t m_threadCount;
    uint32_t m_physicalCoreCount;
    uint16_t m_corePackageCount;
};


class DynamicThreadManager
{
public:
    bool initialize();
    bool initialize(ProcessorConfigurationVector const& existingConfig);

    void destroy();

    bool allocateProcessor(std::vector<LogicalProcessor>& out);
    void freeProcessor(uint32_t coreID, uint16_t packageID);
    LogicalProcessor allocateThread();
    void             freeThread(LogicalProcessor id);

    __force_inline LogicalProcessor allocateProcessor() {
        std::vector<LogicalProcessor> result;
        NativeAffinityMask tmpZero;

        std::memset(&tmpZero, 0x00, sizeof(NativeAffinityMask));
        bool status = allocateProcessor(result);

        return status ? 
            result[0] 
            : 
            LogicalProcessor{ UINT32_MAX, UINT32_MAX, UINT16_MAX, 0, 0, tmpZero };
    }

    __force_inline uint32_t freeThreads() const {
        return m_freeThreadCount;
    }
    __force_inline uint32_t freeCores() const {
        return m_freeCoreCount;
    }
    __force_inline float threadToCoreRatio() const {
        return static_cast<float>(m_freeThreadCount) / static_cast<float>(m_freeCoreCount);
    }

private:
    using ThreadMapIdx       = uint32_t;
    using uniqueIdentifier   = uint64_t;
    using ThreadCountPerCore = uint32_t;
    using ThreadIdxList      = std::vector<ThreadMapIdx>;
    struct InternalPair {
        ThreadCountPerCore m_threadCount;
        ThreadIdxList      m_threads;
    };

    using UnorderedProcMapMutable   = std::unordered_map< uniqueIdentifier, ThreadIdxList >;
    using UnorderedProcMapImmutable = std::unordered_map< uniqueIdentifier, InternalPair >;

    ProcessorConfigurationVector m_map;
    UnorderedProcMapImmutable    m_coreToThreadMapIdx;
    UnorderedProcMapMutable      m_partiallyFreeCoreMap;
    UnorderedProcMapMutable      m_freeCoreMap;
    uint32_t                     m_freeThreadCount;
    uint32_t                     m_freeCoreCount;
};


#endif /* __UTILITY_HEADER_PROCESSOR_CONFIG_DETECTION_HEADER_DEFINITION__ */
