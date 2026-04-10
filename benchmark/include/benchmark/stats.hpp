#ifndef STATS_HPP
#define STATS_HPP

#include <cstdint>
#include <vector>
#include <string_view>


struct percentiles {
    uint64_t min, p50, p90, p95, p99, p999, p9999, max;
    double mean;
};

struct sample {
    uint64_t timestamp;
    uint64_t latency;
};

class Stats {
public:
    Stats(double ghz, std::string_view prefix) 
        : m_tsc_ghz(ghz), m_raw_prefix(prefix) {}

    void print_percentiles(const char *label, int n, const percentiles& p) const;
    void emit_csv_row(const char *arm, int n_samples, int n_paired, const percentiles& p) const;
    void dump_raw_latencies(const char *arm_name, const std::vector<uint64_t>& latencies) const;
    void report_stride(const char *label, sample *s, int n) const;
    percentiles compute_percentiles(std::vector<uint64_t>& data) const;

private:
    double m_tsc_ghz;
    std::string_view m_raw_prefix;
};

#endif // STATS_HPP