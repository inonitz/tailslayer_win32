#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP
#include <array>
#include <vector>
#include <cstdint>
#include <string>


struct AppConfig {
    // Very hardware specific. Hardcoded defaults that change
    static inline constexpr int CORE_MEAS_A = 2;
    static inline constexpr int CORE_MEAS_B = 4;
    static inline constexpr int CORE_MAIN = 0;
    static inline constexpr int DEFAULT_CHANNEL_OFFSET = 256; // The offset between the replicas to end up on different channels
    static inline constexpr int DEFAULT_CHANNEL_BIT = 8; // Bit in the physical memory address that says which channel the address belongs to
    static inline constexpr int DEFAULT_NUM_CHANNELS = 2;
    static inline constexpr std::array<int, 10> STRESS_CORES = {{6, 8, 10, 12, 14, 16, 18, 20, 22, 24}};
    static inline constexpr uint64_t SUPERPAGE_SIZE = (1ULL << 30); // 1GB hugepage

    // Default benchmark configurations
    static inline constexpr int DEFAULT_SAMPLES = 5000000;
    static inline constexpr int DEFAULT_STRESS = 4;
    static inline constexpr int WARMUP_ITERS = 5000;
    static inline constexpr int MAX_PAIR_GAP = 400;
    static inline constexpr int MAX_STRESS = 16;

    // What kind of run we want to perform
    bool do_all = false;
    bool do_single_quiet = false;
    bool do_hedged_quiet = false;
    bool do_single_stress = false;
    bool do_hedged_stress = false;

    // Configuration for the run
    int n_samples = DEFAULT_SAMPLES;
    int n_stress = DEFAULT_STRESS;

    // Probably change, but just set them to the most likely defaults
    int core_a = CORE_MEAS_A;
    int core_b = CORE_MEAS_B;
    int channel_bit = DEFAULT_CHANNEL_BIT;
    int channel_offset = DEFAULT_CHANNEL_OFFSET;
    int n_channels = DEFAULT_NUM_CHANNELS;
    
    uint32_t              m_numCores;
    std::vector<uint32_t> m_coreAffinity;
    std::string           m_rawPrefix = "";

    static AppConfig parse_cli(int argc, char* argv[]);
    static void usage(const char *prog);
};

#endif // APP_CONFIG_HPP