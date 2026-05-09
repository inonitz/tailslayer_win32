#include <tailslayer/proc.hpp>

using namespace tailslayer::util;


int main()
{
    ProcessorConfigurationVector cfg;
    DynamicThreadManager threadMan;
    
    if(!cfg.initialize()) {
        std::fputs("Error: Processor-Info Retrieval failed\n", stderr);
        return 1;
    }

    
    if(!threadMan.initialize(cfg)) {
        std::fputs("Error: Thread Manager Initialization failed\n", stderr);
        return 1;
    }

    auto threadCount = cfg.logicalCoreCount();
    auto coreCount   = cfg.physicalCoreCount();
    auto pkgCount    = cfg.packageCount();
    std::fprintf(stdout, "Processor Has %u Cores & %u Threads on %u Separate Packages\n",
        threadCount,
        coreCount,
        pkgCount
    );


    /* Set the main thread to a specific cpu-core/thread/group */
    auto m_mainThreadCoreID = threadMan.allocateProcessor();
    bool status = 0xFF;
    if(m_mainThreadCoreID.m_usable) {
#ifdef _WIN32
        /* Set Thread to specific CPU */
        status = SetThreadAffinityMask(GetCurrentThread(), 1ull << m_mainThreadCoreID.m_coreID);
        /* Use the thread-group api to select the specific group this core is part-of */
        status = SetThreadAffinityMask(GetCurrentThread(), m_mainThreadCoreID.m_affinityMask.Mask);
        status = SetThreadGroupAffinity(GetCurrentThread(), &m_mainThreadCoreID.m_affinityMask, NULL);

#elif defined(__linux__)
        status = sched_setaffinity(
            gettid(), 
            sizeof(m_mainThreadCoreID.m_affinityMask), 
            &m_mainThreadCoreID.m_affinityMask
        ) == 0 ? true : false;
#endif

        if(status == false) {
            std::fputs("Couldn't Set The thread affinity for this process\n", stdout);
#ifdef _WIN32
            std::fprintf(stdout, "OS Specific Error Code %lu\n", GetLastError());
#elif defined(__linux__)
            perror("OS Specific Information\n");
#endif
        }
    }

    
    /* Thread Allocation example */
    auto otherThread = threadMan.allocateThread();
    if(otherThread.m_usable) {
        std::fprintf(stdout, "Allocated another thread ID %u\nBelongs to Core, Package -> %u, %u\nNative Affinity mask: ", 
            otherThread.m_globalID, 
            otherThread.m_coreID, 
            otherThread.m_pkgID
        );
#ifdef _WIN32
        std::fprintf(stdout, "  Processor Mask: 0x%llx\n  Group: 0x%x\n", 
            otherThread.m_affinityMask.Mask, 
            otherThread.m_affinityMask.Group
        );
#elif defined(__linux__)
        std::fputs("CPU ID's In Set:\n", stdout);
        for (uint32_t i = 0; i < sizeof(otherThread.m_affinityMask) - 1; ++i) {
            if (CPU_ISSET(i, &otherThread.m_affinityMask)) {
                std::printf(stdout, "%d, ", i)
            }
            
        }
        if (CPU_ISSET(sizeof(otherThread.m_affinityMask) - 1, &otherThread.m_affinityMask)) {
            std::printf(stdout, "%d\n", i)
        }
#endif
    }


    /* Cleanup */
    threadMan.freeThread(otherThread);
    threadMan.freeProcessor(m_mainThreadCoreID.m_coreID, m_mainThreadCoreID.m_pkgID);
    threadMan.destroy();
    return 0;
}