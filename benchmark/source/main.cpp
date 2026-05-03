/*
 * main.cpp — Channel-hedged DRAM read benchmark (C++ Version)
 *
 * Build: g++ -O2 -std=c++17 -o hedged_read_cpp main.cpp -pthread
 * Run:   sudo chrt -f 99 ./hedged_read_cpp --all --channel-bit 8
*/

#include "tailslayer/proc.hpp"
#include <tailslayer/hedged_reader.hpp>
#include <benchmark/benchmark.hpp>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#if defined(UTIL2_OS_LINUX)
#   include <sys/mman.h>
#elif defined(UTIL2_OS_WINDOWS)
#endif /* */






struct MemorySetup {
    using volatileAddrVector = std::vector<volatile char *>;
    using AllocReqData   = tailslayer::util::AllocationRequest;
    using AllocReqRegion = tailslayer::util::PhysicalMemRegion;

    AllocReqData       m_memory{};
    AllocReqRegion     m_physContigMem;
    void*              m_replicaPage = nullptr;
    void*              m_stressPage  = nullptr;
    volatileAddrVector m_replicas;
};


static double setup_environment(const AppConfig& cfg) {
    bool status[5] = { true, true, true, true, true };
    
    // SetAffinityToUniqueCores();
    // std::exit(-1);
    
#if defined(UTIL2_OS_WINDOWS)
    status[0] = tailslayer::util::InitializeVMMReader();
    status[1] = tailslayer::util::SetLockMemoryPrivilege(true);
#endif
    status[2] = tailslayer::util::SetProcessPriority();
    status[3] = (tailslayer::util::SetCurrentThreadProcessorID(cfg.m_mainThreadCoreID.m_coreID) != 0);
    status[4] = tailslayer::util::SetCurrentThreadPriority();
    if(
        false 
        || status[0] == false 
        || status[1] == false 
        || status[2] == false 
        || status[3] == false
        || status[4] == false
    ) {
        return -1;
    }


    double tsc_ghz = tailslayer::util::CalibrateTimestampCounterGhz();
    fprintf(stderr, "Main thread pinned to core %d\n", cfg.m_mainThreadCoreID.m_coreID);
    fprintf(stderr, "TSC frequency: %.3f GHz\n", tsc_ghz);


    return tsc_ghz;
}


static void destroy_environment(MemorySetup& toDestroy) {
    if(toDestroy.m_memory.virtaddr) {
        tailslayer::util::freeLargePage(toDestroy.m_memory);
    }

#if defined(UTIL2_OS_WINDOWS)
    tailslayer::util::SetLockMemoryPrivilege(false);
    tailslayer::util::SetProcessPriority(NORMAL_PRIORITY_CLASS);
#elif defined(UTIL2_OS_LINUX)
    status[0] = tailslayer::util::SetProcessPriority(19);
#endif

    tailslayer::util::DestroyVMMReader();
    return;
}


/*
Make n copies of the data and put them on n different channels
*/
static bool setup_replica_page(const AppConfig& config, MemorySetup& mem) {
    std::vector<uint64_t> phys_addrs(config.n_channels);
    std::vector<int> channels(config.n_channels);


    std::memset(mem.m_replicaPage, 0x42, AppConfig::SUPERPAGE_SIZE);


    // Make copies of the data and put them on different channels
    // The channel is like the communication bus from the memory controller to the RAM modules
    // The channels probably won't be doign a RAM refresh at the same time
    mem.m_replicas.resize(config.n_channels, nullptr);
    mem.m_replicas[0] = static_cast<volatile char *>(mem.m_replicaPage);
    for (int i = 1; i < config.n_channels; ++i) {
        mem.m_replicas[i] = mem.m_replicas[0] + (i * config.channel_offset);
        std::memcpy(
            static_cast<char*>(mem.m_replicaPage) + (i * config.channel_offset), 
            mem.m_replicaPage, 
            64
        );
    }


    // Resolve hardware channels
    for (int i = 0; i < config.n_channels; ++i) {
        phys_addrs[i] = tailslayer::util::VirtToPhys(
            reinterpret_cast<uint64_t>(mem.m_replicas[i])
        );
        /* Address being 0 is 99.999% an error, UINT64_MAX is what is returned normally on error */
        if (phys_addrs[i] == UINT64_MAX || phys_addrs[i] == 0) {
            fprintf(stderr, "Cannot read physical address for replica %d (need root)\n", i);
            mem.m_replicaPage = nullptr;
            return false;
        }

        channels[i] = tailslayer::util::compute_channel(phys_addrs[i], config.channel_bit);
        fprintf(stderr, "replica_%d: virt=%p phys=0x%" PRIx64 " channel=%d\n", 
                i, 
                (void *)mem.m_replicas[i], 
                phys_addrs[i], 
                channels[i]
        );
    }


    /* [NOTE] */
    // Sanity check to make sure the replicas did end up on different channels
    // for (int i = 0; i < config.n_channels; ++i) {
    //     for (int j = i + 1; j < config.n_channels; ++j) {
    //         if (channels[i] == channels[j]) {
    //             fprintf(stderr, "ERROR: Replicas %d and %d on same channel (%d)!\n", 
    //                 i, j, 
    //                 channels[i]
    //             );
    //             mem.m_replicaPage = nullptr;
    //             return false;
    //         }
    //     }
    // }


    return true;
}


/*
    Page for making artificial noise to simulate contention
*/
static bool setup_stress_page(const AppConfig& config, MemorySetup& mem) {
    if(mem.m_stressPage == nullptr) {
        return true; /* stressPage will be null only when we don't want stress tests */
    }

    std::memset(mem.m_stressPage, 0xAB, AppConfig::SUPERPAGE_SIZE);
    return true;
}


// static bool setup_page_memory(
//     const MemorySetup& minimumAlloc, 
//     MemorySetup&       outMem
// ) {

// }


static bool setup_memory(const AppConfig& config, MemorySetup& outMem) {
    tailslayer::util::PhysicalMemRegion largestContiguousRegion{};
    tailslayer::util::AllocationRequest hedgedReadBlocks{ 
        (1 + ( config.m_singleThread_withload || config.m_HedgedThreads_withload ))
        * 
        AppConfig::SUPERPAGE_SIZE
    };
    bool status = true;
    

    /* 2. Platform Specific Section. The code converges by the end */
#if defined(UTIL2_OS_WINDOWS)
    tailslayer::util::allocateLargePageMin(hedgedReadBlocks, largestContiguousRegion);
    if(hedgedReadBlocks.virtaddr == nullptr) {
        perror("allocateLargePageMin (alloc)");
        return false;
    }


#elif defined(UTIL2_OS_LINUX)
    /* Step 1. Preallocate all the memory required for the benchmark */
    tailslayer::util::allocateLargePage(hedgedReadBlocks);
    if(hedgedReadBlocks.virtaddr == nullptr) {
        perror("mmap 1GB hugepage (alloc)");
        return false;
    }

    /* Optionally Lock the Memory Region. Not required with MEM_LARGE_PAGES on windows */
    status = tailslayer::util::LockMemoryRegion(hedgedReadBlocks.virtaddr, hedgedReadBlocks.sizeInBytes);
    if(status == false) {
        perror("mlock hugepage (alloc)");
        tailslayer::util::freeLargePage(hedgedReadBlocks);
        return false;
    }

    largestContiguousRegion.vaddr = reinterpret_cast<uint64_t>(hedgedReadBlocks.virtaddr);
    largestContiguousRegion.paddr = VirtToPhys(largestContiguousRegion.vaddr);
    largestContiguousRegion.size  = hedgedReadBlocks.sizeInBytes;
#endif /* */
    /* 
        m_memory will be used for cleanup, 
        m_physContigMem will be used for the actual allocation pages 
    */
    outMem.m_memory        = hedgedReadBlocks;
    outMem.m_physContigMem = largestContiguousRegion;
    outMem.m_replicaPage   = reinterpret_cast<char*>(outMem.m_physContigMem.vaddr);
    outMem.m_stressPage    = ( config.m_singleThread_withload || config.m_HedgedThreads_withload ) ? 
        reinterpret_cast<char*>(outMem.m_replicaPage) + AppConfig::SUPERPAGE_SIZE
        : 
        nullptr;


    status = setup_replica_page(config, outMem);
    if(!status) {
        goto cleanup_label0;
    }

    status = setup_stress_page(config, outMem);
    if(!status) {
        goto cleanup_label0;
    }

cleanup_label0:
    if(status == false) {
        tailslayer::util::freeLargePage(outMem.m_memory);
    }
    return status;
}


/*
    Running the actual benchmarks with the current configuration
*/
static void execute_benchmarks(AppConfig& config, double tsc_ghz, const MemorySetup& mem) {
    printf("arm,n_samples,n_paired,tsc_ghz,min_cyc,p50_cyc,p90_cyc,p95_cyc,"
           "p99_cyc,p999_cyc,p9999_cyc,max_cyc,mean_cyc\n");

    Benchmark benchmark(config, tsc_ghz);
    volatile char *stress = static_cast<volatile char *>(mem.m_stressPage);
    std::vector<tailslayer::util::LogicalProcessor> channelCores;
    std::vector<tailslayer::util::LogicalProcessor> stressThreads;
    uint32_t channelsToBenchmark = UINT32_MAX;
    uint32_t stressThreadsToUse  = UINT32_MAX;
    bool     success             = true;


    channelsToBenchmark = 
        (config.m_singleThread_noload || config.m_singleThread_withload) ? 1
        : 
        (config.m_HedgedThreads_noload || config.m_HedgedThreads_withload) ? config.n_channels 
        :
        UINT32_MAX;
    stressThreadsToUse = 
        (config.m_singleThread_noload || config.m_HedgedThreads_noload) ? 0
        : 
        (config.m_singleThread_withload || config.m_HedgedThreads_withload) ? config.n_stress 
        :
        UINT32_MAX;
        
    assert(channelsToBenchmark != UINT32_MAX && stressThreadsToUse != UINT32_MAX
        && "Invalid Options supplied (Shouldn't happen if arg parsing is correct)\n"
    );

    const float approximateThreadUtilization = channelsToBenchmark * config.m_coreAlloc.threadToCoreRatio()
        + stressThreadsToUse;
    assert( static_cast<uint32_t>(approximateThreadUtilization+1) < config.m_coreAlloc.freeThreads()
        && "Insufficient Thread Count \n"
    );


    /* Allocate physical cores for each channel */
    AppConfig::ThreadID tmpid;
    for(uint32_t i = 0; i < channelsToBenchmark && success; ++i) {
        tmpid = config.m_coreAlloc.allocateProcessor(); /* For easier debugging */
        channelCores.push_back(tmpid);
        success = (tmpid.m_coreID != UINT32_MAX);
    }
    /* allocate cpu-thread for each software-stress-thread */
    for(uint32_t i = 0; i < stressThreadsToUse && success; ++i) {
        tmpid = config.m_coreAlloc.allocateThread();
        stressThreads.push_back(tmpid);
        success = (tmpid.m_coreID != UINT32_MAX);
    }
    assert(success && "CPU/Thread Allocation failed\n");


    if (config.m_singleThread_noload) {
        benchmark.run_arm("single_quiet", /* 1 core */
            {mem.m_replicas[0]}, 
            channelCores, 
            {},
            nullptr
        );
        benchmark.reset();
    }
    if (config.m_HedgedThreads_noload) {
        benchmark.run_arm("hedged_quiet", /* 1 + n_channels cores */
            mem.m_replicas, 
            channelCores, 
            {},
            nullptr
        );
        benchmark.reset();
    }
    if (config.m_singleThread_withload) {
        benchmark.run_arm("single_stress", /* 1 + n_stress */
            {mem.m_replicas[0]}, 
            channelCores, 
            stressThreads,
            stress
        );
        benchmark.reset();
    }
    if (config.m_HedgedThreads_withload) {
        benchmark.run_arm("hedged_stress", /* 1 + n_channels + n_stress */
            mem.m_replicas, 
            channelCores, 
            stressThreads,
            stress
        );
        benchmark.reset();
    }


    return;
}


int main(int argc, char* argv[]) {
    MemorySetup mem{};
    double      tsc_ghz = 0;
    int         status  = 0;
    AppConfig   config = AppConfig::parse_cli(argc, argv);

    tsc_ghz = setup_environment(config);
    if (tsc_ghz < 0) {
        status = 1;
        goto cleanup_label;
    }
    
    if (setup_memory(config, mem) == false) {
        status = 1;
        goto cleanup_label;
    }

    execute_benchmarks(config, tsc_ghz, mem);


cleanup_label:
    destroy_environment(mem);
    return status;
}
