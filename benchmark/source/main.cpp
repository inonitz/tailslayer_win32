/*
 * main.cpp — Channel-hedged DRAM read benchmark (C++ Version)
 *
 * Build: g++ -O2 -std=c++17 -o hedged_read_cpp main.cpp -pthread
 * Run:   sudo chrt -f 99 ./hedged_read_cpp --all --channel-bit 8
*/

#include "tailslayer/hedged_reader.hpp"
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
    using AllocReqData   = tailslayer::utilities::AllocationRequest;
    using AllocReqRegion = tailslayer::utilities::PhysicalMemRegion;

    AllocReqData       m_memory{};
    AllocReqRegion     m_physContigMem;
    void*              m_replicaPage = nullptr;
    void*              m_stressPage  = nullptr;
    volatileAddrVector m_replicas;
};


static double setup_environment() {
    bool status[2] = { true, true };
    tailslayer::utilities::InitializeVMMReader();


#if defined(UTIL2_OS_WINDOWS)
    DWORD priority = REALTIME_PRIORITY_CLASS;
    status[0] = tailslayer::utilities::SetProcessPriority(&priority);
    status[1] = tailslayer::utilities::SetLockMemoryPrivilege(true);
#elif defined(UTIL2_OS_LINUX)
    int32_t priority = -20;
    status[0] = tailslayer::utilities::SetProcessPriority(&priority);
#endif

    if(status[0] == false || status[1] == false) {
        return -1;
    }


    if (tailslayer::utilities::pin_to_core(AppConfig::CORE_MAIN) != 0) {
        perror("pin main to coordinator core");
        return -1.0;
    }
    fprintf(stderr, "Main thread pinned to core %d\n", AppConfig::CORE_MAIN);

    double tsc_ghz = tailslayer::utilities::CalibrateTimestampCounterGhz();
    fprintf(stderr, "TSC frequency: %.3f GHz\n", tsc_ghz);

    return tsc_ghz;
}

static void destroy_environment(MemorySetup& toDestroy) {
    if(toDestroy.m_memory.virtaddr) {
        tailslayer::utilities::freeLargePage(toDestroy.m_memory);
    }

#if defined(UTIL2_OS_WINDOWS)
    DWORD priority = NORMAL_PRIORITY_CLASS;
    tailslayer::utilities::SetLockMemoryPrivilege(false);
    tailslayer::utilities::SetProcessPriority(&priority);
#elif defined(UTIL2_OS_LINUX)
    int32_t priority = 19; /* Lowest Priority (-20 [Highest] -> 19 [Lowest] ) */
    status[0] = tailslayer::utilities::SetProcessPriority(&priority);
#endif

    tailslayer::utilities::DestroyVMMReader();
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
        phys_addrs[i] = tailslayer::utilities::VirtToPhys(
            reinterpret_cast<uint64_t>(mem.m_replicas[i])
        );
        /* Address being 0 is 99.999% an error, UINT64_MAX is what is returned normally on error */
        if (phys_addrs[i] == UINT64_MAX || phys_addrs[i] == 0) {
            fprintf(stderr, "Cannot read physical address for replica %d (need root)\n", i);
            mem.m_replicaPage = nullptr;
            return false;
        }

        channels[i] = tailslayer::utilities::compute_channel(phys_addrs[i], config.channel_bit);
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
    tailslayer::utilities::PhysicalMemRegion largestContiguousRegion{};
    tailslayer::utilities::AllocationRequest hedgedReadBlocks{ 
        (1 + ( config.do_single_stress || config.do_hedged_stress ))
        * 
        AppConfig::SUPERPAGE_SIZE
    };
    bool status = true;
    

    /* 2. Platform Specific Section. The code converges by the end */
#if defined(UTIL2_OS_WINDOWS)
    tailslayer::utilities::allocateLargePageMin(hedgedReadBlocks, largestContiguousRegion);
    if(hedgedReadBlocks.virtaddr == nullptr) {
        perror("allocateLargePageMin (alloc)");
        return false;
    }


#elif defined(UTIL2_OS_LINUX)
    /* Step 1. Preallocate all the memory required for the benchmark */
    tailslayer::utilities::allocateLargePage(hedgedReadBlocks);
    if(hedgedReadBlocks.virtaddr == nullptr) {
        perror("mmap 1GB hugepage (alloc)");
        return false;
    }

    /* Optionally Lock the Memory Region. Not required with MEM_LARGE_PAGES on windows */
    status = tailslayer::utilities::LockMemoryRegion(hedgedReadBlocks.virtaddr, hedgedReadBlocks.sizeInBytes);
    if(status == false) {
        perror("mlock hugepage (alloc)");
        tailslayer::utilities::freeLargePage(hedgedReadBlocks);
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
    outMem.m_stressPage    = ( config.do_single_stress || config.do_hedged_stress ) ? 
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
        tailslayer::utilities::freeLargePage(outMem.m_memory);
    }
    return status;
}


/*
Running the actual benchmarks with the current configuration
*/
static void execute_benchmarks(const AppConfig& config, double tsc_ghz, const MemorySetup& mem) {
    printf("arm,n_samples,n_paired,tsc_ghz,min_cyc,p50_cyc,p90_cyc,p95_cyc,"
           "p99_cyc,p999_cyc,p9999_cyc,max_cyc,mean_cyc\n");

    Benchmark benchmark(config, tsc_ghz);
    volatile char *stress = static_cast<volatile char *>(mem.m_stressPage);

    // TODO: Fix cores for more than 2 channels
    // Temporary: Build the list of cores to pin to.
    // If n_channels > 2, extrapolate extra cores based off of core_b.
    std::vector<int> cores;
    cores.push_back(config.core_a);
    if (config.n_channels > 1) {
        cores.push_back(config.core_b);
    }
    for (int i = 2; i < config.n_channels; ++i) {
        cores.push_back(config.core_b + i - 1); 
    }

    if (config.do_single_quiet) {
        benchmark.run_arm("single_quiet", {mem.m_replicas[0]}, {cores[0]}, false, nullptr);
        benchmark.reset();
    }
    if (config.do_hedged_quiet) {
        benchmark.run_arm("hedged_quiet", mem.m_replicas, cores, false, nullptr);
        benchmark.reset();
    }
    if (config.do_single_stress) {
        benchmark.run_arm("single_stress", {mem.m_replicas[0]}, {cores[0]}, true, stress);
        benchmark.reset();
    }
    if (config.do_hedged_stress) {
        benchmark.run_arm("hedged_stress", mem.m_replicas, cores, true, stress);
        benchmark.reset();
    }
}


int main(int argc, char* argv[]) {
    MemorySetup mem{};
    double      tsc_ghz = 0;
    int         status  = 0;
    const AppConfig config = AppConfig::parse_cli(argc, argv);

    tsc_ghz = setup_environment();
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
