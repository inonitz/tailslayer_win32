#ifndef BENCHMARK_HPP
#define BENCHMARK_HPP
#include "benchmark/app_config.hpp"
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>


class sample;


struct measurement_context {
    volatile char* addr;
    int            core_id;
    int            n_samples;
    sample*        samples;
};


struct stress_context {
    volatile char *region;
    uint64_t region_size;
    int core_id;
    std::atomic<bool>& go;
    std::atomic<bool>& stop;
};


class Benchmark {
public:
    Benchmark(const AppConfig& config, double tsc_ghz);

    void reset();

    void run_arm(
        const char*                        name, 
        const std::vector<volatile char*>& addrs, 
        const std::vector<int>&            cores, 
        bool                               with_stress, 
        volatile char*                     stress_region
    );

private:
    const AppConfig& m_config;
    double m_tsc_ghz;
    std::atomic<bool> m_measure_signal{false}; // Signal for measurement threads

    struct StressGroup {
        std::vector<std::thread> threads;
        std::vector<stress_context> contexts;
        std::atomic<bool> go{false};
        std::atomic<bool> stop{false};
    };

    // Helpers
    void start_stress_threads(bool with_stress, volatile char* stress_region, StressGroup& group);
    void stop_stress_threads(bool with_stress, StressGroup& group);

    void process_and_write(const char* name, const std::vector<sample*>& channel_samples) const;
    int pair_samples_n(const std::vector<sample*>& all_samples, int num_samples, std::vector<uint64_t>& out_effective) const;
    int pair_samples_n2(
        const std::vector<sample*>& all_samples, 
        int                         num_samples, 
        std::vector<uint64_t>&      out_effective
    ) const;

    // Thread entrypoints
    void measurement_thread(measurement_context* context);
    void stress_thread(stress_context* context);
};

#endif // BENCHMARK_HPP
