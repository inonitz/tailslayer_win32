#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP
#include <tailslayer/types.hpp>
#include <tailslayer/proc.hpp>
#include <cstdint>
#include <string>


struct AppConfig {
    typedef tailslayer::util::LogicalProcessor             ThreadID;
    typedef tailslayer::util::ProcessorConfigurationVector ProcessorCfg;
    typedef tailslayer::util::DynamicThreadManager         ThreadManager;

    static inline constexpr int DEFAULT_CHANNEL_OFFSET = 256; // The offset between the replicas to end up on different channels
    static inline constexpr int DEFAULT_CHANNEL_BIT    = 8;   // Bit in the physical memory address that says which channel the address belongs to
    static inline constexpr int DEFAULT_NUM_CHANNELS   = 2;
    static inline constexpr uint64_t SUPERPAGE_SIZE = (1ULL << 30); // 1GB hugepage

    // Default benchmark configurations
    static inline constexpr int DEFAULT_SAMPLES = 5000000;
    static inline constexpr int DEFAULT_STRESS = 4;
    static inline constexpr int WARMUP_ITERS = 5000;
    static inline constexpr int MAX_PAIR_GAP = 400;

    // What kind of run we want to perform
    bool m_allBenchmarks          = false;
    bool m_singleThread_noload    = false;
    bool m_HedgedThreads_noload   = false;
    bool m_singleThread_withload  = false;
    bool m_HedgedThreads_withload = false;

    // Configuration for the run
    int n_samples = DEFAULT_SAMPLES;
    int n_stress = DEFAULT_STRESS;

    /* Core count is found dynamically through native api's. channel offset needs to be found manually */
    int channel_bit    = DEFAULT_CHANNEL_BIT;
    int channel_offset = DEFAULT_CHANNEL_OFFSET;
    int n_channels     = DEFAULT_NUM_CHANNELS;
    
    ThreadID      m_mainThreadCoreID;
    ProcessorCfg  m_coreCfg;
    ThreadManager m_coreAlloc;
    std::string   m_rawPrefix = "";


    static AppConfig parse_cli(int argc, char* argv[]);
};


#endif // APP_CONFIG_HPP
