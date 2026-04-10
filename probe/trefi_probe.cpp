/*
    * trefi_probe.c — Detect periodic DRAM refresh (tREFI) jitter via clflush timing
    *
    * Probes a single address with clflush+reload, records all spike timestamps.
    *
    * Build: gcc -O2 -o trefi_probe trefi_probe.c -lm
    * Run:   sudo chrt -f 99 taskset -c 3 ./trefi_probe
*/
#include "trefi_probe.hpp"
#include <cxxopts.hpp>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>





static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}




int main(int argc, char* argv[]) {
    constexpr auto kHUGEPAGE_2M      = (1ULL << 21);
    constexpr auto kCALIB_PROBES     = 500000;
    constexpr auto kMAX_SPIKES       = 2000000;
    constexpr auto kDEFAULT_PROBES   = 20000000;
    constexpr auto kDEFAULT_TREFI_US = 7.8;

    int       status           = 0;
    int       n_probes         = kDEFAULT_PROBES;
    uint64_t  manual_threshold = 0;
    double    trefi_us         = kDEFAULT_TREFI_US;
    double    thresh_mult      = 2.0;
    double    tsc_ghz          = 0.0f;
    double    expected_trefi_cyc = 0.0f;
    void*     p                  = nullptr;
    int                   n_spikes    = 0;
    int                   n_intervals = 0;
    int                   hist_bins   = 200;
    std::vector<uint64_t> calib;
    std::vector<spike>    spikes;
    std::vector<double>   intervals;
    std::vector<int>      hist;


    double T = expected_trefi_cyc;
    int count_1T    = 0;
    int count_2T    = 0;
    int count_3T    = 0;
    int count_other = 0;
    bool tmp[3] = { false, false, false };

    double frac_1T = 0;
    double frac_2T = 0;
    double frac_3T = 0;
    double frac_harmonic = 0;

    double bin_lo = 0;
    double bin_hi = 0;
    double bin_width = 0;
    int hist_total = 0;

    int peak_bin = 0;
    int peak_count = 0;
    double peak_cyc = 0;
    double peak_us = 0;

    uint64_t lat_min = 0;
    uint64_t lat_max = 0;
    double lat_sum = 0;


    /* Argument Parsing using cxxopts */
    try {
        cxxopts::Options options(argv[0], "Tailslayer Probe\n");
        options.add_options()
            ( "n,probes",      "Number of probes",      cxxopts::value<int>(n_probes))
            ( "T,threshold",   "Manual threshold",      cxxopts::value<uint64_t>(manual_threshold))
            ( "t,trefi-us",    "tREFI in microseconds", cxxopts::value<double>(trefi_us))
            ( "m,thresh-mult", "Threshold multiplier",  cxxopts::value<double>(thresh_mult))
            ( "h,help",        "Print usage");

        auto result = options.parse(argc, argv);


        // Handle the help flag
        if (result.count("help")) {
            printf("help:\n    %s\n", options.help().c_str());
            return 0;
        }

    } catch (const cxxopts::exceptions::exception& e) 
    {
        fprintf(stderr, "Error parsing options:\n    %s\n", e.what());
        fprintf(stderr, 
            "Usage: %s [--probes N] [--threshold N] [--trefi-us F] [--thresh-mult F]\n", 
            argv[0]
        );
        return 1;
    }



    tsc_ghz = tslayer::CalibrateTimestampCounterGhz();
    expected_trefi_cyc = trefi_us * 1000.0 * tsc_ghz;
    
    printf("TSC: %.3f GHz\n", tsc_ghz);
    printf("Expected tREFI: %.1f us = %.0f cycles\n",
        trefi_us, 
        expected_trefi_cyc
    );


    // Map 2MB hugepage
    p = tslayer::allocateHugePages(kHUGEPAGE_2M);
    if (p == nullptr) 
    {
        fprintf(stderr, "Failure to allocate Huge 2MiB Pages\n");
        fprintf(stderr, "Setup (Linux): sudo bash -c 'echo 64 > "
                "/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages'\n"
        );
        return 1;
    }
    
    std::memset(p, 0x42, kHUGEPAGE_2M);
    // if (!tslayer::LockMemoryRegion(p, kHUGEPAGE_2M)) 
    // {
    //     fprintf(stderr, "Failure to Lock Memory Region\n");
    //     return 1;
    // }
    
    
    printf("\n=== CALIBRATING ===\n");
    calib.resize(kCALIB_PROBES);
    volatile char *addr = (volatile char *)p;
    for (int i = 0; i < 2000; i++) {
        timed_probe(addr);
    }
    for(auto& elem : calib) {
        elem = timed_probe(addr);
    }
    // std::sort(calib.begin(), calib.end());
    qsort(calib.data(), kCALIB_PROBES, sizeof(uint64_t), cmp_u64);


    uint64_t median = calib[kCALIB_PROBES / 2];
    uint64_t p90    = calib[static_cast<int>(kCALIB_PROBES * 0.90)];
    uint64_t p99    = calib[static_cast<int>(kCALIB_PROBES * 0.99)];
    uint64_t p999   = calib[static_cast<int>(kCALIB_PROBES * 0.999)];
    uint64_t p9999  = calib[static_cast<int>(kCALIB_PROBES * 0.9999)];
    int      n_above = 0;
    uint64_t threshold = (manual_threshold > 0) ? 
        manual_threshold 
        : 
        static_cast<uint64_t>(thresh_mult * median);


    for(const auto& elem : calib) {
        n_above += (elem > threshold);
    }
    fprintf(stderr, "\
            %d probes: median=%llu p90=%llu p99=%llu p99.9=%llu p99.99=%llu\n\
            Threshold: %llu (%.1fx median)\n\
            Calibration spikes: %d (%.3f%%)\n\
        ",
        kCALIB_PROBES, median, p90, p99, p999, p9999,
        threshold, thresh_mult,
        n_above, 100.0 * n_above / kCALIB_PROBES
    );


    // Main probe loop
    fprintf(stderr, "\n=== PROBING (%d probes) ===\n", n_probes);
    uint64_t tsc_start = tslayer::rdtsc_lfence();

    spikes.resize(kMAX_SPIKES);
    for (int i = 0; i < n_probes; i++) {
        tslayer::clflush_addr(addr);
        tslayer::mfence_inst();
        tslayer::lfence_inst();
        uint64_t t0 = tslayer::rdtsc_lfence();
        *(volatile char *)addr;
        uint64_t t1 = tslayer::rdtscp_lfence();
        uint64_t lat = t1 - t0;

        if (lat > threshold && n_spikes < kMAX_SPIKES) {
            spikes[n_spikes].tsc = t0;
            spikes[n_spikes].latency = lat;
            n_spikes++;
        }
    }

    uint64_t tsc_end = tslayer::rdtscp_lfence();
    double elapsed_s = (double)(tsc_end - tsc_start) / (tsc_ghz * 1e9);

    fprintf(stderr, "  Duration: %.2f s\n", elapsed_s);
    fprintf(stderr, "  Spikes: %d (%.4f%%)\n", n_spikes,
            100.0 * n_spikes / n_probes);

    // Output CSV to stdout
    printf("abs_tsc,latency_cyc\n");
    for (int i = 0; i < n_spikes; i++) {
        // printf("%llu, %llu\n", spikes[i].tsc, spikes[i].latency);
    }
    fprintf(stderr, "\n=== PERIODICITY ANALYSIS ===\n");
    if (n_spikes < 10) {
        fprintf(stderr, "  Too few spikes (%d) for analysis\n", n_spikes);
        fprintf(stderr, "  VERDICT: INSUFFICIENT DATA\n");
        status = 0;
        goto __cleanup;
    }


    // Compute inter-spike intervals
    n_intervals = n_spikes - 1;
    intervals.resize(n_intervals);
    for (int i = 0; i < n_intervals; i++) {
        intervals[i] = (double)(spikes[i + 1].tsc - spikes[i].tsc);
    }
    // Harmonic binning: count intervals near 1T, 2T, 3T 
    T = expected_trefi_cyc;
    count_1T    = 0;
    count_2T    = 0;
    count_3T    = 0;
    count_other = 0;
    for (auto& interval : intervals) {
        tmp[0] = (interval >= T * 0.85 && interval <= T * 1.15);
        tmp[1] = (interval >= T * 1.85 && interval <= T * 2.15);
        tmp[2] = (interval >= T * 2.85 && interval <= T * 3.15);
        count_1T += tmp[0];
        count_2T += tmp[1];
        count_3T += tmp[2];
        count_other += !(tmp[0] || tmp[1] || tmp[2]);

        // if (iv >= T * 0.85 && iv <= T * 1.15) { 
        //     count_1T++; 
        // }
        // else if (iv >= T * 1.85 && iv <= T * 2.15) {
        //     count_2T++; 
        // }
        // else if (iv >= T * 2.85 && iv <= T * 3.15) {
        //     count_3T++; 
        // }
        // else {
        //     count_other++; 
        // }
    }


    frac_1T = (double)count_1T / n_intervals;
    frac_2T = (double)count_2T / n_intervals;
    frac_3T = (double)count_3T / n_intervals;
    frac_harmonic = (double)(count_1T + count_2T + count_3T) / n_intervals;
    fprintf(stderr, "  Expected tREFI: %.0f cycles (%.1f us)\n", T, trefi_us);
    fprintf(stderr, "  Intervals: %d total\n", n_intervals);
    fprintf(stderr, "  1T (±15%%): %d (%.1f%%)\n", count_1T, frac_1T * 100);
    fprintf(stderr, "  2T (±15%%): %d (%.1f%%)\n", count_2T, frac_2T * 100);
    fprintf(stderr, "  3T (±15%%): %d (%.1f%%)\n", count_3T, frac_3T * 100);
    fprintf(stderr, "  Other:     %d (%.1f%%)\n", count_other,
            100.0 * count_other / n_intervals);
    fprintf(stderr, "  Harmonic total: %.1f%%\n", frac_harmonic * 100);

    // Fine-grained histogram near 1T to find exact peak
    bin_lo = T * 0.5;
    bin_hi = T * 1.5;
    bin_width = (bin_hi - bin_lo) / hist_bins;
    hist_total = 0;
    hist.resize(hist_bins);
    for (int i = 0; i < n_intervals; i++) {
        double iv = intervals[i];
        if (iv >= bin_lo && iv < bin_hi) {
            int bin = (int)((iv - bin_lo) / bin_width);
            if (bin >= 0 && bin < hist_bins) {
                hist[bin]++;
                hist_total++;
            }
        }
    }


    peak_bin = 0;
    peak_count = 0;
    for (int b = 0; b < hist_bins; b++) {
        if (hist[b] > peak_count) {
            peak_count = hist[b];
            peak_bin = b;
        }
    }
    peak_cyc = bin_lo + (peak_bin + 0.5) * bin_width;
    peak_us = peak_cyc / (tsc_ghz * 1000.0);

    fprintf(stderr, "\n  Histogram peak: %.0f cycles (%.2f us), count=%d\n",
            peak_cyc, peak_us, peak_count);
    fprintf(stderr, "  Expected:       %.0f cycles (%.2f us)\n", T, trefi_us);
    fprintf(stderr, "  Deviation:      %.1f%%\n",
            fabs(peak_cyc - T) / T * 100);

    // Spike latency stats
    lat_min = UINT64_MAX;
    lat_max = 0;
    lat_sum = 0;
    for (int i = 0; i < n_spikes; i++) {
        if (spikes[i].latency < lat_min) lat_min = spikes[i].latency;
        if (spikes[i].latency > lat_max) lat_max = spikes[i].latency;
        lat_sum += spikes[i].latency;
    }
    fprintf(stderr, "\n  Spike latency: min=%llu avg=%.0f max=%llu cycles\n",
            lat_min, lat_sum / n_spikes, lat_max);
    fprintf(stderr, "  Spike latency: min=%.1f avg=%.1f max=%.1f ns\n",
            lat_min / tsc_ghz, (lat_sum / n_spikes) / tsc_ghz,
            lat_max / tsc_ghz);

    fprintf(stderr, "\n");
    if (frac_harmonic > 0.30) {
        fprintf(stderr, "  VERDICT: PERIODIC — %.0f%% of intervals at tREFI harmonics\n",
                frac_harmonic * 100);
        fprintf(stderr, "  tREFI is visible via clflush timing on this DDR4 system\n");
    } else if (frac_harmonic > 0.15) {
        fprintf(stderr, "  VERDICT: WEAK SIGNAL — %.0f%% at harmonics (borderline)\n",
                frac_harmonic * 100);
    } else {
        fprintf(stderr, "  VERDICT: NO PERIODIC SIGNAL — %.0f%% at harmonics\n",
                frac_harmonic * 100);
        fprintf(stderr, "  Spikes are likely controller noise, not refresh\n");
    }


__cleanup:
    tslayer::freeHugePages(p, kHUGEPAGE_2M);
    tslayer::SetLockMemoryPrivilege(false);
    return status;
}


/* Define goto cleanup at the end */
