#include <tailslayer/proc.hpp>
#if defined(_WIN32)
#   include <unordered_map>
#elif defined(__linux__)
#   include <fcntl.h>
#   include <unistd.h>
#   include <cstdlib>
#endif


void ProcessorConfigurationVector::getPhysicalCoreThreadAffinity(
    uint32_t coreID,
    std::vector<NativeAffinityMask>& threadMasks
) const noexcept 
{
    if(m_proc.empty() || (coreID > m_proc.back().m_coreID)) {
        return;
    }


    for(auto& lp : m_proc) {
        if(coreID != lp.m_coreID) {
            continue;
        }

        threadMasks.push_back(lp.m_affinityMask);
    }
    return;
}




#if defined(_WIN32) /* OS Specific Function Definitions */


bool ProcessorConfigurationVector::GetProcessorNativeConfiguration(
    std::vector<BYTE>& buffer
) {
    DWORD bufSizeBytes = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX bufp = nullptr;

    if (!GetLogicalProcessorInformationEx(RelationAll, nullptr, &bufSizeBytes) && 
        GetLastError() != ERROR_INSUFFICIENT_BUFFER
    ) {
        fprintf(stderr, "ProcessorConfigurationVector::GetLogicalProcessorInformationEx() -> Failed to get buffer size\n");
        return false;
    }

    buffer.resize(bufSizeBytes);
    bufp = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( 
        buffer.data()
    );
    if (!GetLogicalProcessorInformationEx(RelationAll, bufp, &bufSizeBytes)
    ) {
        fprintf(stderr, "ProcessorConfigurationVector::GetLogicalProcessorInformationEx() -> Error retrieving processor information.\n");
        return false;
    }


    return true;
}


// void ProcessorConfigurationVector::PrettyPrintProcessorInfo(
//     const std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>& buffer
// ) {
//     printf("--- Logical Processor Information ---\n");
//     printf("%-34s | %-18s | %-18s | %s\n", "Relationship", "Relationship (Hexadecimal)", "Processor Mask", "Details");
//     printf("---------------------------------------------------------------------------\n");

//     for (const auto& info : buffer) 
//     {
//         char binaryStrBuf[65];
//         _ui64toa_s(info.ProcessorMask, binaryStrBuf, 65, 2);
//         printf("0b%-34s | ", binaryStrBuf);
//         printf("0x%-18p | ", (void*)info.ProcessorMask);

//         switch (info.Relationship) {
//         case RelationProcessorCore:
//             printf("%-18s | ", "Core");
//             // info.ProcessorCore.Flags: 1 means functional units are shared (SMT/Hyperthreading)
//             printf("SMT: %s", (info.ProcessorCore.Flags == 1) ? "Enabled" : "Disabled");
//             break;

//         case RelationNumaNode:
//             printf("%-18s | ", "NUMA Node");
//             printf("Node Number: %lu", info.NumaNode.NodeNumber);
//             break;

//         case RelationCache:
//             printf("%-18s | ", "Cache");
//             {
//                 CACHE_DESCRIPTOR cache = info.Cache;
//                 const char* type = "????";
//                 type = 
//                     (cache.Type == CacheUnified)
//                     ? "Unified" :
                    
//                     (cache.Type == CacheInstruction)
//                     ? "Instruction" :

//                     (cache.Type == CacheData)
//                     ? "Data" :

//                     (cache.Type == CacheTrace)
//                     ? "Trace" :

//                     "Unknown";

//                 printf("L%u %s, Size: %lu KB, Line: %u bytes",
//                     cache.Level, 
//                     type, 
//                     cache.Size / 1024, 
//                     cache.LineSize
//                 );
//             }
//             break;

//         case RelationProcessorPackage:
//             printf("%-18s | ", "Package (Socket)");
//             printf("Physical CPU Socket");
//             break;

//         default:
//             printf("%-18s | ", "Other");
//             printf("Unknown Relationship");
//             break;
//         }
//         printf("\n");
//     }
//     printf("-------------------------------------------------------------------------------\n");
//     return;
// }


bool ProcessorConfigurationVector::ProcessCurrentConfiguration() {

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX curptr = nullptr;
    std::vector<BYTE>                       cfgBuffer;
    std::vector<USHORT>                     currProcGroupMasks;
    std::vector<uint32_t>                   coreInfoOffset;
    std::vector<uint32_t>                   pkgInfoOffset;
    std::unordered_map<GroupPair, uint32_t> coreMap;
    std::unordered_map<GroupPair, uint32_t> packageMap;
    USHORT    procGroupCount   = 0;
    WORD      corePrimaryGroup = 0;
    KAFFINITY currProcAffinityMask = 0;
    KAFFINITY systemAffinityMask   = 0;
    GroupPair currKey       = {0, 0};
    uint32_t  nextCoreId    = 0;
    uint32_t  nextPackageId = 0;
    uint32_t  currCoreID    = 0;
    uint32_t  currPkgID     = 0;
    DWORD     byteOffset    = 0;


    if(!GetProcessorNativeConfiguration(cfgBuffer)) {
        fprintf(stderr, "ProcessorConfigurationVector::ProcessCurrentConfiguration() -> GetProcessorNativeConfiguration() Failed\n");
        return false;
    }


    /* 
        On Systems where NumCores > 64 (Before Windows 10), 
        Windows split the processor into 'Processor Groups',
        each of which has atleast 64 Cores.
        Moreover, the "Primary Group" assigned to a process cannot be changed;
        BUT! Assigning threads to groups other than the primary one is possible.

        Essentially we check the following:
            * current process' core affinity, i.e: Which CPU Cores/Threads can be accessed?
            * The Currently Executing Processors' CPU Group. 
            * The Current Process' Group Affinity, i.e: Which Groups can this process access? 
        With this information, it is possible to detect which processor is "enabled"
        for the current process & if it can be used, like CPU_ISSET(...) on linux.
    */
    /* Get the CPU Affinity masks for the current process */
    if (!GetProcessAffinityMask(GetCurrentProcess(), &currProcAffinityMask, &systemAffinityMask)) {
        currProcAffinityMask = 0;
        return false;
    }

    /* Get the group of the current thread/Physical Core */
    PROCESSOR_NUMBER procNum;
    GetCurrentProcessorNumberEx(&procNum);
    corePrimaryGroup = procNum.Group;

    /* Get the list of all processor groups the process is allowed to run in */
    GetProcessGroupAffinity(GetCurrentProcess(), &procGroupCount, nullptr);
    currProcGroupMasks.resize(procGroupCount);
    GetProcessGroupAffinity(GetCurrentProcess(), &procGroupCount, currProcGroupMasks.data());



    /* Find all relevant info structures and record their positions */
    curptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( 
        cfgBuffer.data()
    );
    while(byteOffset < cfgBuffer.size()) 
    {
        if (curptr->Relationship == RelationProcessorCore) {
            coreInfoOffset.push_back(byteOffset);

            currKey = {
                curptr->Processor.GroupMask[0].Group, 
                curptr->Processor.GroupMask[0].Mask 
            };

            if (coreMap.find(currKey) == coreMap.end()) {
                coreMap[currKey] = nextCoreId;
                ++nextCoreId;
            }
        }
        else if(curptr->Relationship == RelationProcessorPackage) {
            pkgInfoOffset.push_back(byteOffset);
            
            currKey = GroupPair{
                curptr->Processor.GroupMask[0].Group, 
                curptr->Processor.GroupMask[0].Mask
            };
            if (packageMap.find(currKey) == packageMap.end()) {
                packageMap[currKey] = nextPackageId;
                ++nextPackageId;
            }
        }

        byteOffset += curptr->Size;
        curptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( 
            reinterpret_cast<PBYTE>(curptr) + curptr->Size
        );
    }


    /* iterate over all found structures */
    for(auto& currOffset : coreInfoOffset) 
    {
        curptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( 
            cfgBuffer.data() + currOffset
        );
        currKey = { /* Unique Group ID */
            curptr->Processor.GroupMask[0].Group, 
            curptr->Processor.GroupMask[0].Mask 
        };
        currCoreID = coreMap[currKey];

        // For each logical processor (thread) in this core
        for (uint16_t g = 0; g < curptr->Processor.GroupCount; ++g) {
            currKey = { 
                curptr->Processor.GroupMask[g].Group,
                curptr->Processor.GroupMask[g].Mask
            };

            bool skip           = false;
            bool isGroupAllowed = false;
            KAFFINITY          currBitmask = 0;
            NativeAffinityMask currLpMask{0};
            for (uint8_t bit = 0; bit < 64; ++bit) {
                currBitmask = ((KAFFINITY)1 << bit);
                skip = (currKey.m_groupMask & currBitmask ) == 0;
                if(skip) {
                    continue;
                }


                std::memset(&currLpMask, 0x00, sizeof(NativeAffinityMask));
                currLpMask.Mask  = currBitmask;
                currLpMask.Group = currKey.m_group;
                /* 
                    If the current Logical Processor is part of the primary group, it may be inaccessible
                    If it isn't, it should be accessible by default.
                */
                isGroupAllowed = std::find(currProcGroupMasks.begin(), currProcGroupMasks.end(), 
                        currKey.m_group
                    ) != currProcGroupMasks.end();
                isGroupAllowed = (currKey.m_group == corePrimaryGroup) 
                        ? isGroupAllowed && (currBitmask & currProcAffinityMask)
                        : isGroupAllowed;

                m_proc.push_back(LogicalProcessor{
                    UINT32_MAX,
                    currCoreID,
                    UINT16_MAX,
                    isGroupAllowed,
                    0,
                    currLpMask
                });
            }
        }
    }


    /* 
        Second pass: Assign Package IDs
        RelationProcessorPackage gives us which masks belong to which socket
    */
    for(auto& currOffset : pkgInfoOffset) 
    {
        curptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            cfgBuffer.data() + currOffset
        );

        currKey = GroupPair{
            curptr->Processor.GroupMask[0].Group, 
            curptr->Processor.GroupMask[0].Mask
        };
        currPkgID = packageMap[currKey];
        // printf("Current Package ID is %u\n", currPkgID);

        for(auto& lp : m_proc) {
            for (uint16_t g = 0; g < curptr->Processor.GroupCount; ++g) {
                currKey = GroupPair{
                    curptr->Processor.GroupMask[g].Group, 
                    curptr->Processor.GroupMask[g].Mask
                };
                lp.m_pkgID = (currKey == lp.m_affinityMask) ? currPkgID : lp.m_pkgID;
                // printf("(lp, Group) -> (%3u, %3u) | %3u ?== %3u | 0x%-8llx & 0x%-8llx > 0 | %3u \n", 
                //     lp.m_coreID, g,
                //     currKey.m_group, lp.m_affinityMask.Group, 
                //     currKey.m_groupMask, lp.m_affinityMask.Mask,
                //     lp.m_pkgID
                // );
            }
            // putchar('\n');
        }
    }


    /* Assign GlobalID's lastly */
    for (uint32_t i = 0; i < m_proc.size(); i++) {
        m_proc[i].m_globalID = i;
    }
    m_threadCount       = m_proc.size();
    m_physicalCoreCount = nextCoreId;
    m_corePackageCount  = nextPackageId;
    return true;
}


#elif defined(__linux__)


static inline int64_t readSysfsInt(const char* path) {
    char* endptr;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    char buf[32]; // Increased size slightly for safety
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);


    if (n <= 0) {
        return -1;
    }

    buf[n] = '\0';    
    int64_t result = static_cast<int64_t>(strtoll(buf, &endptr, 10));

    if (endptr == buf) {
        return -1;
    }

    return result;
}


bool ProcessorConfigurationVector::ProcessCurrentConfiguration() {
    bool      failure[2]    = { false, false };
    int       numProcessors = sysconf(_SC_NPROCESSORS_CONF);
    int64_t   currCoreID    = 0;
    int64_t   currPkgID     = 0;
    cpu_set_t allowedMask;
    cpu_set_t foundCore;
    char      pathBuffer[128];
    const char* k_coreIDfmt = "/sys/devices/system/cpu/cpu%d/topology/core_id"; 
    const char* k_pkgIDfmt  = "/sys/devices/system/cpu/cpu%d/topology/physical_package_id";
    
    CPU_ZERO(&allowedMask);
    CPU_ZERO(&foundCore);
    
    // Not all cores may have been allocated to this program
    failure[0] = (numProcessors <= 0);
    failure[1] = (sched_getaffinity(0, sizeof(cpu_set_t), &allowedMask) == -1);
    if (failure[0] || failure[1]) {
        return false;
    }


    failure[0] = false;
    m_proc.reserve(numProcessors);
    for (uint32_t i = 0; i < static_cast<uint32_t>(numProcessors); ++i) 
    {
        snprintf(pathBuffer, sizeof(pathBuffer), k_coreIDfmt, i);
        currCoreID = readSysfsInt(pathBuffer);
        snprintf(pathBuffer, sizeof(pathBuffer), k_pkgIDfmt, i);
        currPkgID = readSysfsInt(pathBuffer);

        failure[0] = (currCoreID == -1 || currPkgID == -1);
        if(failure[0]) {
            std::fprintf(stderr, "ProcessorConfigurationVector::ProcessCurrentConfiguration() ->\n  Can't read CPU Topology Files (cpu%u/topology/...)\n", i);
            perror("  Couldn't Read CPU Topology ");
            return false;
        }

        CPU_ZERO(&foundCore);
        CPU_SET(i, &foundCore);
        m_proc.push_back({
            i,
            static_cast<uint32_t>(currCoreID),
            static_cast<uint16_t>(currPkgID),
            static_cast<uint8_t>(CPU_ISSET(i, &allowedMask)),
            0,
            foundCore
        });
    }


    return true;
}

#endif /* OS Platform Detection */


bool DynamicThreadManager::initialize()
{
    bool good = true;
    if(!m_map.data().empty()) {
        return false;
    }

    good = m_map.initialize();
    if(!good) {
        return false;
    }

    return initialize(m_map);
}


bool DynamicThreadManager::initialize(ProcessorConfigurationVector const& existingConfig) {
    if(!m_map.data().empty()) {
        return false;
    }


    m_map.copy(existingConfig);

    /* Assign threads to their respective unique core ID */
    for(uint32_t i = 0; i < m_map.data().size(); ++i) 
    {
        /* Push all threads that belong to a certain CoreID */
        auto& logicalProc = m_map.data()[i];
        m_coreToThreadMapIdx[logicalProc.uniqueCoreID()].m_threads.push_back(i);
    }

    /* Assign amount of threads per unique core */
    for(auto& kv : m_coreToThreadMapIdx) {
        kv.second.m_threadCount = kv.second.m_threads.size();
        m_freeCoreMap[kv.first] = kv.second.m_threads;
    }


    m_freeThreadCount = m_map.logicalCoreCount();
    m_freeCoreCount = m_map.physicalCoreCount();
    return true;
}


void DynamicThreadManager::destroy() {
    m_freeCoreCount   = 0;
    m_freeThreadCount = 0;
    m_freeCoreMap.clear();
    m_partiallyFreeCoreMap.clear();
    m_coreToThreadMapIdx.clear();
    m_map.destroy();
    return;
}


bool DynamicThreadManager::allocateProcessor(std::vector<LogicalProcessor>& out) {
    if(m_freeCoreCount == 0) {
        return false;
    }
    
    auto  key = m_freeCoreMap.begin()->first;
    auto& val = m_freeCoreMap.begin()->second;
    for(auto& mapIdx : val) {
        out.push_back(m_map.data()[mapIdx]);
        --m_freeThreadCount;
    }
    m_freeCoreMap.erase(key);
    --m_freeCoreCount;

    return true;
}


void DynamicThreadManager::freeProcessor(uint32_t coreID, uint16_t packageID) {
    LogicalProcessor tmp{
        UINT32_MAX, 
        coreID, 
        packageID, 
        true, 
        0, 
        NativeAffinityMask{} 
    };
    uniqueIdentifier tmpID = tmp.uniqueCoreID();

    if(
        (m_freeCoreMap.size() == m_coreToThreadMapIdx.size())
        || 
        (m_freeCoreMap.find(tmpID) != m_freeCoreMap.end())
    ) {
        return;
    }


    /* 
        A. all threads belonging to uniqueCoreID need to be moved 
            from partially-free to completely-free 
    */
    auto pUsedCoreIter = m_partiallyFreeCoreMap.find(tmpID);
    if(pUsedCoreIter != m_partiallyFreeCoreMap.end()) {
        m_partiallyFreeCoreMap.erase(pUsedCoreIter);
    }

    /* Move threads back to free map */
    m_freeCoreMap[tmpID] = m_coreToThreadMapIdx[tmpID].m_threads;

    m_freeThreadCount += m_coreToThreadMapIdx[tmpID].m_threadCount;
    ++m_freeCoreCount;
    return;
}


LogicalProcessor DynamicThreadManager::allocateThread()
{
    uniqueIdentifier key = 0;
    ThreadMapIdx     idx = 0;

    if(m_freeThreadCount == 0) {
        return LogicalProcessor{ UINT32_MAX, UINT32_MAX, UINT16_MAX, 0, 0, NativeAffinityMask{} }; 
    }


    if(m_partiallyFreeCoreMap.empty()) {
        if(m_freeCoreMap.empty()) { 
            /* Should never reach this due to m_freeThreadCount check */
            return LogicalProcessor{ UINT32_MAX, UINT32_MAX, UINT16_MAX, 0, 0, NativeAffinityMask{} };
        }
        key = m_freeCoreMap.begin()->first;

        /* Move Thread-Vector from completely-free to partially-free */
        m_partiallyFreeCoreMap[key] = m_freeCoreMap[key];
        m_freeCoreMap.erase(key);
        --m_freeCoreCount;
    }

    key = m_partiallyFreeCoreMap.begin()->first;
    idx = m_partiallyFreeCoreMap[key].back();
    m_partiallyFreeCoreMap[key].pop_back();

    /* delete the processor from the map if there are no available cores */
    if(m_partiallyFreeCoreMap[key].size() == 0) {
        m_partiallyFreeCoreMap.erase(key);
    }

    --m_freeThreadCount;
    return m_map.data()[idx];
}


void DynamicThreadManager::freeThread(LogicalProcessor id) {
    /* Attempt to free thread that is already free */
    for(auto& kv : m_partiallyFreeCoreMap) {
        for(auto& threadMapIdx : kv.second) {
            if(m_map.data()[threadMapIdx].m_globalID == id.m_globalID) {
                return;
            }
        }
    }
    for(auto& kv : m_freeCoreMap) {
        for(auto& threadMapIdx : kv.second) {
            if(m_map.data()[threadMapIdx].m_globalID == id.m_globalID) {
                return;
            }
        }
    }


    m_partiallyFreeCoreMap[id.uniqueCoreID()].push_back(id.m_globalID);
    ++m_freeThreadCount;

    if(m_partiallyFreeCoreMap[id.uniqueCoreID()].size() == 
        m_coreToThreadMapIdx[id.uniqueCoreID()].m_threadCount
    ) {
        m_partiallyFreeCoreMap.erase(id.uniqueCoreID());
        m_freeCoreMap[id.uniqueCoreID()] = m_coreToThreadMapIdx[id.uniqueCoreID()].m_threads;
        ++m_freeCoreCount;
    }
    return;
}


/* =============================================================================== */
/* ================================ Example Usage ================================ */
/* =============================================================================== */
// #include "proc.hpp"
// #if defined(__linux__)
// #   include <unistd.h>
// #endif


// int main()
// {
//     ProcessorConfigurationVector cfg;
//     DynamicThreadManager threadMan;
    
//     if(!cfg.initialize()) {
//         std::fputs("Error: Processor-Info Retrieval failed\n", stderr);
//         return 1;
//     }

    
//     if(!threadMan.initialize(cfg)) {
//         std::fputs("Error: Thread Manager Initialization failed\n", stderr);
//         return 1;
//     }

//     auto threadCount = cfg.logicalCoreCount();
//     auto coreCount   = cfg.physicalCoreCount();
//     auto pkgCount    = cfg.packageCount();
//     std::fprintf(stdout, "Processor Has %u Cores & %u Threads on %u Separate Packages\n",
//         threadCount,
//         coreCount,
//         pkgCount
//     );


//     /* Set the main thread to a specific cpu-core/thread/group */
//     auto m_mainThreadCoreID = threadMan.allocateProcessor();
//     bool status = 0xFF;
//     if(m_mainThreadCoreID.m_usable) {
// #ifdef _WIN32
//         /* Set Thread to specific CPU */
//         status = SetThreadAffinityMask(GetCurrentThread(), 1ull << m_mainThreadCoreID.m_coreID);
//         /* Use the thread-group api to select the specific group this core is part-of */
//         status = SetThreadAffinityMask(GetCurrentThread(), m_mainThreadCoreID.m_affinityMask.Mask);
//         status = SetThreadGroupAffinity(GetCurrentThread(), &m_mainThreadCoreID.m_affinityMask, NULL);

// #elif defined(__linux__)
//         status = sched_setaffinity(
//             gettid(), 
//             sizeof(m_mainThreadCoreID.m_affinityMask), 
//             &m_mainThreadCoreID.m_affinityMask
//         ) == 0 ? true : false;
// #endif

//         if(status == false) {
//             std::fputs("Couldn't Set The thread affinity for this process\n", stdout);
// #ifdef _WIN32
//             std::fprintf(stdout, "OS Specific Error Code %lu\n", GetLastError());
// #elif defined(__linux__)
//             perror("OS Specific Information\n");
// #endif
//         }
//     }

    
//     /* Thread Allocation example */
//     auto otherThread = threadMan.allocateThread();
//     if(otherThread.m_usable) {
//         std::fprintf(stdout, "Allocated another thread ID %u\nBelongs to Core, Package -> %u, %u\nNative Affinity mask: ", 
//             otherThread.m_globalID, 
//             otherThread.m_coreID, 
//             otherThread.m_pkgID
//         );
// #ifdef _WIN32
//         std::fprintf(stdout, "  Processor Mask: 0x%llx\n  Group: 0x%x\n", 
//             otherThread.m_affinityMask.Mask, 
//             otherThread.m_affinityMask.Group
//         );
// #elif defined(__linux__)
//         std::fputs("CPU ID's In Set:\n", stdout);
//         for (uint32_t i = 0; i < sizeof(otherThread.m_affinityMask) - 1; ++i) {
//             if (CPU_ISSET(i, &otherThread.m_affinityMask)) {
//                 std::fprintf(stdout, "%d, ", i);
//             }
            
//         }
//         if (CPU_ISSET(sizeof(otherThread.m_affinityMask) - 1, &otherThread.m_affinityMask)) {
//             std::fprintf(stdout, "%lu\n", static_cast<size_t>(sizeof(otherThread.m_affinityMask) - 1));
//         }
// #endif
//     }


//     /* Cleanup */
//     threadMan.freeThread(otherThread);
//     threadMan.freeProcessor(m_mainThreadCoreID.m_coreID, m_mainThreadCoreID.m_pkgID);
//     threadMan.destroy();
//     return 0;
// }