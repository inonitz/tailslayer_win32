#include "benchmark/benchmark.hpp"
#include "benchmark/stats.hpp"
#include <tailslayer/utilities.hpp>
#include <util2/C/aligned_malloc.h>
#include <util2/C/sleep.h>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdint>
// #include <unistd.h>


Benchmark::Benchmark(const AppConfig& config, double tsc_ghz)
    : m_config(config), m_tsc_ghz(tsc_ghz) {}

void Benchmark::reset() {
    m_measure_signal.store(false);
}


void Benchmark::measurement_thread(measurement_context* context) {
    if (tailslayer::utilities::pin_to_core(context->core_id) != 0) {
        perror("measurement_thread: sched_setaffinity");
        return;
    }

    // Barrier because we want to make sure thread creation / setup time isn't adding noise
    while (!m_measure_signal.load(std::memory_order_acquire)) 
        {} 

    volatile char *addr = context->addr;
    sample *samples = context->samples;
    int n = context->n_samples;

    for (int i = 0; i < AppConfig::WARMUP_ITERS; i++) {
        tailslayer::utilities::clflush_addr(addr);
        tailslayer::utilities::mfence_inst();
        tailslayer::utilities::lfence_inst();
        (void)tailslayer::utilities::rdtsc_lfence();
        uint8_t val = *(volatile uint8_t *)addr; // The actual read of the data
        asm volatile("" :: "r"(val));
        (void)tailslayer::utilities::rdtscp_lfence();
    }

    for (int i = 0; i < n; i++) {
        tailslayer::utilities::clflush_addr(addr);
        tailslayer::utilities::mfence_inst();
        tailslayer::utilities::lfence_inst();
        uint64_t t0 = tailslayer::utilities::rdtsc_lfence();
        uint8_t val = *(volatile uint8_t *)addr;
        asm volatile("" :: "r"(val));
        uint64_t t1 = tailslayer::utilities::rdtscp_lfence();
        samples[i].timestamp = t0;
        samples[i].latency = t1 - t0;
    }
}


/*
Generate stress / noise to simulate contention
*/
void Benchmark::stress_thread(stress_context* context) {
    if (tailslayer::utilities::pin_to_core(context->core_id) != 0) {
        perror("stress_thread: sched_setaffinity");
        return;
    }

    while (!context->go.load(std::memory_order_acquire)) {}

    volatile char *region = context->region;
    uint64_t size = context->region_size;
    uint64_t mask = (size - 1) & ~63ULL;

    uint64_t state = 0xdeadbeef12345678ULL ^ reinterpret_cast<uint64_t>(context);

    while (!context->stop.load(std::memory_order_acquire)) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;

        uint64_t off = state & mask;
        volatile char *target = region + off;
        tailslayer::utilities::clflush_addr(target);
        tailslayer::utilities::mfence_inst();
        uint8_t val = *(volatile uint8_t *)target;
        asm volatile("" :: "r"(val));
        tailslayer::utilities::mfence_inst();
    }
}


/*
Can run either the baseline (single channel) or the hedged (all channels)
In the hedged one, we probably won't see the channels stall at the same time
*/
void Benchmark::run_arm(const char* name, 
                        const std::vector<volatile char*>& addrs, 
                        const std::vector<int>& cores, 
                        bool with_stress, volatile char* stress_region) {
    fprintf(stderr, "\n--- Starting arm: %s ---\n", name);

    int n_channels = addrs.size();
    std::vector<sample*> all_samples(n_channels);
    std::vector<measurement_context> mcontexts(n_channels);
    std::vector<std::thread> mthreads;

    for (int i = 0; i < n_channels; ++i) {
        all_samples[i] = allocate_samples();
        mcontexts[i] = { addrs[i], cores[i], m_config.n_samples, all_samples[i] };
    }

    StressGroup stress_group;
    start_stress_threads(with_stress, stress_region, stress_group);

    for (int i = 0; i < n_channels; ++i) {
        mthreads.emplace_back(&Benchmark::measurement_thread, this, &mcontexts[i]);
    }

    // Signals all the measurement threads to start at the same time
    m_measure_signal.store(true, std::memory_order_release); 

    for (auto& t : mthreads) {
        t.join();
    }

    stop_stress_threads(with_stress, stress_group);

    process_and_write(name, all_samples);

    for (int i = 0; i < n_channels; ++i) {
        util2_aligned_free(all_samples[i]);
    }
}


sample* Benchmark::allocate_samples() const {
    return static_cast<sample*>(util2_aligned_malloc(m_config.n_samples * sizeof(sample), CACHE_LINE_BYTES));
}


void Benchmark::start_stress_threads(bool with_stress, volatile char* stress_region, StressGroup& group) {
    if (!with_stress) return;

    group.contexts.reserve(m_config.n_stress);
    
    for (int i = 0; i < m_config.n_stress; i++) {
        group.contexts.push_back({ 
            stress_region, 
            AppConfig::SUPERPAGE_SIZE,
            AppConfig::STRESS_CORES[i % AppConfig::STRESS_CORES.size()],
            group.go, 
            group.stop 
        });
        group.threads.emplace_back(&Benchmark::stress_thread, this, &group.contexts.back());
    }
    
    group.go.store(true, std::memory_order_release);
    microsleep(50000); // Allow stress threads time to hit steady state
    return;
}


void Benchmark::stop_stress_threads(bool with_stress, StressGroup& group) {
    if (!with_stress) return;

    group.stop.store(true, std::memory_order_release);
    for (auto& t : group.threads) {
        if (t.joinable()) t.join();
    }
}


void Benchmark::process_and_write(const char* name, const std::vector<sample*>& channel_samples) const {
    Stats stats(m_tsc_ghz, m_config.raw_prefix);
    int n_channels = channel_samples.size();

    // Process each individual channel
    for (int c = 0; c < n_channels; ++c) {
        char label[128];
        if (n_channels == 1) {
            snprintf(label, sizeof(label), "%s", name);
        } else {
            snprintf(label, sizeof(label), "%s_ch%d", name, c);
            stats.report_stride(label, channel_samples[c], m_config.n_samples);
        }

        std::vector<uint64_t> lat(m_config.n_samples);
        for (int i = 0; i < m_config.n_samples; i++) {
            lat[i] = channel_samples[c][i].latency;
        }

        percentiles p = stats.compute_percentiles(lat);
        stats.print_percentiles(label, m_config.n_samples, p);
        stats.emit_csv_row(label, m_config.n_samples, m_config.n_samples, p);
        stats.dump_raw_latencies(label, lat);
    }

    // Process the hedged minimums if we have more than 1 channel
    if (n_channels > 1) {
        std::vector<uint64_t> effective;
        effective.reserve(m_config.n_samples);
        
        int n_paired = pair_samples_n(channel_samples, m_config.n_samples, effective);

        fprintf(stderr, "  Pairing: %d/%d samples paired across %d channels (%.1f%%)\n",
                n_paired, m_config.n_samples, n_channels, 100.0 * n_paired / m_config.n_samples);

        percentiles pe = stats.compute_percentiles(effective);
        stats.print_percentiles(name, n_paired, pe);
        stats.emit_csv_row(name, m_config.n_samples, n_paired, pe);
        stats.dump_raw_latencies(name, effective);
    }
}


/*
Take the minimum latency. The data was replicated so it doesn't matter who got the data first.
Using sliding windows trying to pair threads that have a timestamp within a super small gap.
Pair those threads and take the minimum
*/
int Benchmark::pair_samples_n(const std::vector<sample*>& all_samples, int num_samples, std::vector<uint64_t>& out_effective) const {
    int n_channels = all_samples.size();
    if (n_channels == 0) return 0;
    
    std::vector<int> indices(n_channels, 0);
    
    while (true) {
        uint64_t min_ts = UINT64_MAX;
        uint64_t max_ts = 0;
        int min_idx_channel = -1;
        uint64_t min_latency = UINT64_MAX;

        bool out_of_bounds = false;
        
        // Find the boundary spread (min and max timestamps) for the current indices across all channels
        for (int c = 0; c < n_channels; ++c) {
            if (indices[c] >= num_samples) {
                out_of_bounds = true;
                break;
            }
            
            uint64_t ts = all_samples[c][indices[c]].timestamp;
            uint64_t lat = all_samples[c][indices[c]].latency;
            
            if (ts < min_ts) { 
                min_ts = ts; 
                min_idx_channel = c; 
            }
            if (ts > max_ts) { max_ts = ts; }
            if (lat < min_latency) { min_latency = lat; }
        }
        if (out_of_bounds) break;

        // All timestamps within the acceptable gap?
        if ((max_ts - min_ts) < AppConfig::MAX_PAIR_GAP) {
            out_effective.push_back(min_latency);
            for (int c = 0; c < n_channels; ++c) {
                indices[c]++; // Move the window forward for all channels
            }
        } else {
            // They are spread too far apart. Advance the channel that is lagging furthest behind in time.
            indices[min_idx_channel]++;
        }
    }
    
    return out_effective.size();
}
