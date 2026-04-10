#include "benchmark/stats.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cinttypes>


void Stats::print_percentiles(const char *label, int n, const percentiles& p) const {
    fprintf(stderr, "\n=== ARM: %s (n=%d) ===\n", label, n);
    fprintf(stderr, "  min=%" PRIu64 " (%.1fns)  p50=%" PRIu64 " (%.1fns)  "
            "p90=%" PRIu64 " (%.1fns)  p95=%" PRIu64 " (%.1fns)\n",
            p.min, p.min / m_tsc_ghz, p.p50, p.p50 / m_tsc_ghz,
            p.p90, p.p90 / m_tsc_ghz, p.p95, p.p95 / m_tsc_ghz);
    fprintf(stderr, "  p99=%" PRIu64 " (%.1fns)  p99.9=%" PRIu64 " (%.1fns)  "
            "p99.99=%" PRIu64 " (%.1fns)  max=%" PRIu64 " (%.1fns)\n",
            p.p99, p.p99 / m_tsc_ghz, p.p999, p.p999 / m_tsc_ghz,
            p.p9999, p.p9999 / m_tsc_ghz, p.max, p.max / m_tsc_ghz);
    fprintf(stderr, "  mean=%.1f (%.1fns)\n", p.mean, p.mean / m_tsc_ghz);
}


void Stats::emit_csv_row(const char *arm, int n_samples, int n_paired, const percentiles& p) const {
    printf("%s,%d,%d,%.3f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
           "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f\n",
           arm, n_samples, n_paired, m_tsc_ghz,
           p.min, p.p50, p.p90, p.p95,
           p.p99, p.p999, p.p9999, p.max, p.mean);
}


void Stats::dump_raw_latencies(const char *arm_name, const std::vector<uint64_t>& latencies) const {
    if (m_raw_prefix.empty()) return;
    char path[512];
    snprintf(path, sizeof(path), "%s_%s.csv", m_raw_prefix.data(), arm_name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "latency_cyc\n");
    for (uint64_t lat : latencies) fprintf(f, "%" PRIu64 "\n", lat);
    fclose(f);
    fprintf(stderr, "  Dumped %zu raw latencies to %s\n", latencies.size(), path);
}


void Stats::report_stride(const char *label, sample *s, int n) const {
    if (n < 2) return;
    int count = std::min(n - 1, 10000);
    std::vector<uint64_t> strides(count);
    for (int i = 0; i < count; i++) {
        strides[i] = s[i + 1].timestamp - s[i].timestamp;
    }
    std::sort(strides.begin(), strides.end());
    fprintf(stderr, "  %s stride: min=%" PRIu64 " p50=%" PRIu64 " p99=%" PRIu64
            " max=%" PRIu64 " (first %d samples)\n",
            label, strides[0], strides[count / 2],
            strides[static_cast<int>(count * 0.99)], strides[count - 1], count);
}


percentiles Stats::compute_percentiles(std::vector<uint64_t>& data) const {
    percentiles out;
    int n = data.size();
    if (n == 0) {
        std::memset(&out, 0, sizeof(out));
        return out;
    }

    std::sort(data.begin(), data.end());

    out.min   = data[0];
    out.p50   = data[static_cast<int>(n * 0.50)];
    out.p90   = data[static_cast<int>(n * 0.90)];
    out.p95   = data[static_cast<int>(n * 0.95)];
    out.p99   = data[static_cast<int>(n * 0.99)];
    out.p999  = data[std::min(n - 1, static_cast<int>(n * 0.999))];
    out.p9999 = data[std::min(n - 1, static_cast<int>(n * 0.9999))];
    out.max   = data[n - 1];

    double sum = 0;
    for (uint64_t val : data) sum += val;
    out.mean = sum / n;
    return out;
}