/*
 * main.cpp — Channel-hedged DRAM read benchmark (C++ Version)
 *
 * Build: g++ -O2 -std=c++17 -o hedged_read_cpp main.cpp -pthread
 * Run:   sudo chrt -f 99 ./hedged_read_cpp --all --channel-bit 8
*/

#include <benchmark/benchmark.hpp>
#include <benchmark/hw_utils.hpp>
#include <tailslayer/utilities.hpp>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#if defined(UTIL2_OS_LINUX)
#   include <sys/mman.h>
#elif defined(UTIL2_OS_WINDOWS)
#endif /* */


struct MemorySetup {
    using volatileAddrVector = std::vector<volatile char *>;
    void*              replica_page = nullptr;
    void*              stress_page  = nullptr;
    volatileAddrVector replicas;
    bool               ok  = false;
};


static double setup_environment() {
    HardwareUtils::InitializeMemoryReader();

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
    if (toDestroy.stress_page) {
        tailslayer::utilities::freeHugePages(toDestroy.stress_page, AppConfig::SUPERPAGE_SIZE);
        tailslayer::utilities::freeHugePages(toDestroy.replica_page, AppConfig::SUPERPAGE_SIZE);
    }
    HardwareUtils::DestroyMemoryReader();
    return;
}


/*
Make n copies of the data and put them on n different channels
*/
static bool setup_replica_page(const AppConfig& config, MemorySetup& mem) {
    mem.replica_page = tailslayer::utilities::allocateHugePages(AppConfig::SUPERPAGE_SIZE);
    if (mem.replica_page == nullptr) {
        perror("mmap 1GB hugepage (replicas)");
        mem.replica_page = nullptr;
        return false;
    }
    std::memset(mem.replica_page, 0x42, AppConfig::SUPERPAGE_SIZE);
    tailslayer::utilities::LockMemoryRegion(mem.replica_page, AppConfig::SUPERPAGE_SIZE);


    // Make copies of the data and put them on different channels
    // The channel is like the communication bus from the memory controller to the RAM modules
    // The channels probably won't be doign a RAM refresh at the same time
    mem.replicas.resize(config.n_channels, nullptr);
    mem.replicas[0] = static_cast<volatile char *>(mem.replica_page);
    for (int i = 1; i < config.n_channels; ++i) {
        mem.replicas[i] = mem.replicas[0] + (i * config.channel_offset);
        std::memcpy(static_cast<char*>(mem.replica_page) + (i * config.channel_offset), mem.replica_page, 64);
    }


    std::vector<uint64_t> phys_addrs(config.n_channels);
    std::vector<int> channels(config.n_channels);
    
    // Resolve hardware channels
    for (int i = 0; i < config.n_channels; ++i) {
        phys_addrs[i] = HardwareUtils::virt_to_phys(reinterpret_cast<uint64_t>(mem.replicas[i]));
        
        if (phys_addrs[i] == 0) {
            fprintf(stderr, "Cannot read physical address for replica %d (need root)\n", i);
            tailslayer::utilities::freeHugePages(mem.replica_page, AppConfig::SUPERPAGE_SIZE);
            mem.replica_page = nullptr;
            return false;
        }

        channels[i] = HardwareUtils::compute_channel(phys_addrs[i], config.channel_bit);
        fprintf(stderr, "replica_%d: virt=%p phys=0x%" PRIx64 " channel=%d\n", 
                i, (void *)mem.replicas[i], phys_addrs[i], channels[i]);
    }

    // Sanity check to make sure the replicas did end up on different channels
    for (int i = 0; i < config.n_channels; ++i) {
        for (int j = i + 1; j < config.n_channels; ++j) {
            if (channels[i] == channels[j]) {
                fprintf(stderr, "ERROR: Replicas %d and %d on same channel (%d)!\n", i, j, channels[i]);
                tailslayer::utilities::freeHugePages(mem.replica_page, AppConfig::SUPERPAGE_SIZE);
                mem.replica_page = nullptr;
                return false;
            }
        }
    }


    return true;
}


/*
Page for making artificial noise to simulate contention
*/
static bool setup_stress_page(const AppConfig& config, MemorySetup& mem) {
    if (!config.do_single_stress && !config.do_hedged_stress) {
        return true;
    }


    mem.stress_page = tailslayer::utilities::allocateHugePages(AppConfig::SUPERPAGE_SIZE);
    if (mem.stress_page == nullptr) {
        perror("mmap 1GB hugepage (stress)");
        mem.stress_page = nullptr;
        return false;
    }
    
    std::memset(mem.stress_page, 0xAB, AppConfig::SUPERPAGE_SIZE);
    tailslayer::utilities::LockMemoryRegion(mem.stress_page, AppConfig::SUPERPAGE_SIZE);


    return true;
}


static MemorySetup setup_memory(const AppConfig& config) {
    MemorySetup mem;

    if (!setup_replica_page(config, mem)) {
        return mem;
    }

    if (!setup_stress_page(config, mem)) {
        tailslayer::utilities::freeHugePages(mem.replica_page, AppConfig::SUPERPAGE_SIZE);
        mem.replica_page = nullptr;
        return mem;
    }


    mem.ok = true;
    return mem;
}


/*
Running the actual benchmarks with the current configuration
*/
static void execute_benchmarks(const AppConfig& config, double tsc_ghz, const MemorySetup& mem) {
    printf("arm,n_samples,n_paired,tsc_ghz,min_cyc,p50_cyc,p90_cyc,p95_cyc,"
           "p99_cyc,p999_cyc,p9999_cyc,max_cyc,mean_cyc\n");

    Benchmark benchmark(config, tsc_ghz);
    volatile char *stress = static_cast<volatile char *>(mem.stress_page);

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
        benchmark.run_arm("single_quiet", {mem.replicas[0]}, {cores[0]}, false, nullptr);
        benchmark.reset();
    }
    if (config.do_hedged_quiet) {
        benchmark.run_arm("hedged_quiet", mem.replicas, cores, false, nullptr);
        benchmark.reset();
    }
    if (config.do_single_stress) {
        benchmark.run_arm("single_stress", {mem.replicas[0]}, {cores[0]}, true, stress);
        benchmark.reset();
    }
    if (config.do_hedged_stress) {
        benchmark.run_arm("hedged_stress", mem.replicas, cores, true, stress);
        benchmark.reset();
    }
}


int main(int argc, char* argv[]) {


    const AppConfig config = AppConfig::parse_cli(argc, argv);

    double tsc_ghz = setup_environment();
    if (tsc_ghz < 0) return 1;

    MemorySetup mem = setup_memory(config);
    if (!mem.ok) return 1;


    execute_benchmarks(config, tsc_ghz, mem);


    destroy_environment(mem);
    return 0;
}
