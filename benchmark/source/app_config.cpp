#include "benchmark/app_config.hpp"
#include <tailslayer/utilities.hpp>
#include <cxxopts.hpp>
#include <cstdlib>


AppConfig AppConfig::parse_cli(int argc, char* argv[]) {
    AppConfig config;
    
    cxxopts::Options options(argv[0], "Memory Test Application");
    std::string arm_type;

    options.add_options()
        // Short/Long Flags     Description                                                                  Value Binding
        ("a,all",               "Run all four arms")
        ("A,arm",               "Specific Benchmark To run - single/multi_ch_quiet, single/multi_ch_stress", cxxopts::value<std::string>(arm_type))
        ("n,samples",           "Samples per arm",                                                           cxxopts::value<int>(config.n_samples))
        ("s,stress-threads",    "Number of stress threads",                                                  cxxopts::value<int>(config.n_stress))
        // ("1,core-a",            "Core A index",                                                              cxxopts::value<int>(config.core_a))
        // ("2,core-b",            "Core B index",                                                              cxxopts::value<int>(config.core_b))
        ("B,channel-bit",       "Physical address bit for channel",                                          cxxopts::value<int>(config.channel_bit))
        ("O,channel-offset",    "Channel offset",                                                            cxxopts::value<int>(config.channel_offset))
        ("C,channels",          "Number of memory channels",                                                 cxxopts::value<int>(config.n_channels))
        ("R,raw-prefix",        "Raw prefix for output files",                                               cxxopts::value<std::string>(config.m_rawPrefix))
        ("h,help",              "Show this help menu");

    try {
        auto result = options.parse(argc, argv);
        
        if (result.count("help")) {
            std::fputs(options.help().c_str(), stderr);
            std::exit(0);
        }


        config.m_allBenchmarks = !!(result.count("all"));
        if (config.m_allBenchmarks) {
            config.m_singleThread_noload = 
                config.m_HedgedThreads_noload = 
                config.m_singleThread_withload = 
                config.m_HedgedThreads_withload = true;
        }
        if (result.count("arm") && !config.m_allBenchmarks) {
            config.m_singleThread_noload    = !!(arm_type == "single_ch_quiet");
            config.m_HedgedThreads_noload   = !!(arm_type == "multi_ch_quiet");
            config.m_singleThread_withload  = !!(arm_type == "single_ch_stress");
            config.m_HedgedThreads_withload = !!(arm_type == "multi_ch_stress");

            if(!config.m_singleThread_noload && !config.m_HedgedThreads_noload &&
                !config.m_singleThread_withload && !config.m_HedgedThreads_withload
            ) {
                std::fprintf(stderr, "Error: Invalid Benchmarking Arm Specified\n");
                std::fputs(options.help().c_str(), stderr);
                std::exit(1);
            }
        }

        // If channel-bit is provided but channel-offset is NOT explicitly provided, calculate it.
        if (result.count("channel-bit") && !result.count("channel-offset")) {
            config.channel_offset = 1 << config.channel_bit;
        }

    } catch (const cxxopts::exceptions::exception& e) {
        std::fprintf(stderr, "Error parsing options: %s\n", e.what());
        std::fputs(options.help().c_str(), stderr);
        std::exit(1);
    }


    if (!config.m_singleThread_noload && !config.m_HedgedThreads_noload && 
        !config.m_singleThread_withload && !config.m_HedgedThreads_withload
    ) {
        std::fputs("Error: No Benchmarking Arm Specified\n", stderr);
        std::fputs(options.help().c_str(), stderr);
        std::exit(1);
    }


    if(!config.m_coreCfg.initialize()) {
        std::fputs("Error: Processor-Info Retrieval failed\n", stderr);
        std::exit(1);
    }
    /* m_coreAlloc.initialize returns false on double-init, unless an exception was raised :o */
    config.m_coreAlloc.initialize(config.m_coreCfg);
    config.n_stress    = (config.m_singleThread_withload || config.m_HedgedThreads_withload) ? config.n_stress : 0;
    config.n_stress    = config.n_stress > config.m_coreCfg.logicalCoreCount() ?
        config.m_coreCfg.logicalCoreCount() 
        : 
        config.n_stress;


    /* SetCurrentThreadProcessorID works on simple ID's */
    config.m_mainThreadCoreID = config.m_coreAlloc.allocateProcessor();
    return config;
}

// void AppConfig::usage(const char *prog) {
//     fprintf(stderr, "Usage: %s [options]\n", prog);
//     fprintf(stderr, "  --all                 Run all four arms\n");
//     fprintf(stderr, "  --arm ARM             Run specific arm:\n");
//     fprintf(stderr, "                        single_quiet, dual_quiet,\n");
//     fprintf(stderr, "                        single_stress, dual_stress\n");
//     fprintf(stderr, "  --samples N           Samples per arm (default: %d)\n",                  DEFAULT_SAMPLES);
//     fprintf(stderr, "  --stress-threads N    Number of stress threads (default: %d)\n",         DEFAULT_STRESS);
//     fprintf(stderr, "  --channel-bit N       Physical address bit for channel (default: %d)\n", DEFAULT_CHANNEL_BIT);
//     fprintf(stderr, "  --channels N          Number of memory channels (default: %d)\n",        DEFAULT_NUM_CHANNELS);
//     fprintf(stderr, "  --help                Show this help\n");
// }