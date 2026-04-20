#include "benchmark/app_config.hpp"
#include <cstdlib>
#include <cstdio>
#include <cxxopts.hpp>
#include <iostream>
#include <string>


AppConfig AppConfig::parse_cli(int argc, char* argv[]) {
    AppConfig config;
    
    cxxopts::Options options(argv[0], "Memory Test Application");
    std::string arm_type;

    options.add_options()
        // Short/Long Flags     Description                                         Value Binding
        ("a,all",               "Run all four arms")
        ("A,arm",               "Specific arm (single/hedged_quiet, single/dual_stress)", cxxopts::value<std::string>(arm_type))
        ("n,samples",           "Samples per arm",                                         cxxopts::value<int>(config.n_samples))
        ("s,stress-threads",    "Number of stress threads",                                cxxopts::value<int>(config.n_stress))
        ("1,core-a",            "Core A index",                                            cxxopts::value<int>(config.core_a))
        ("2,core-b",            "Core B index",                                            cxxopts::value<int>(config.core_b))
        ("B,channel-bit",       "Physical address bit for channel",                        cxxopts::value<int>(config.channel_bit))
        ("O,channel-offset",    "Channel offset",                                          cxxopts::value<int>(config.channel_offset))
        ("C,channels",          "Number of memory channels",                               cxxopts::value<int>(config.n_channels))
        ("R,raw-prefix",        "Raw prefix for output files",                             cxxopts::value<std::string>(config.raw_prefix))
        ("h,help",              "Show this help menu");

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::exit(0);
        }


        config.do_all = !!(result.count("all"));
        if (config.do_all) {
            config.do_single_quiet = 
                config.do_hedged_quiet = 
                config.do_single_stress = 
                config.do_hedged_stress = true;
        }
        if (result.count("arm") && !config.do_all) {
            config.do_single_quiet = !!(arm_type == "single_quiet");
            config.do_hedged_quiet = !!(arm_type == "dual_quiet");
            config.do_single_stress = !!(arm_type == "single_stress");
            config.do_hedged_stress = !!(arm_type == "dual_stress");


            if(!config.do_single_quiet && !config.do_hedged_quiet &&
                !config.do_single_stress && !config.do_hedged_stress
            ) {
                std::cerr << "Error: Invalid arm specified.\n\n" << options.help() << std::endl;
                std::exit(1);
            }
        }

        // If channel-bit is provided but channel-offset is NOT explicitly provided, calculate it.
        if (result.count("channel-bit") && !result.count("channel-offset")) {
            config.channel_offset = 1 << config.channel_bit;
        }

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << "\n\n";
        std::cerr << options.help() << std::endl;
        std::exit(1);
    }


    if (!config.do_single_quiet && !config.do_hedged_quiet && !config.do_single_stress && !config.do_hedged_stress) {
        std::cerr << "Error: No test arm specified.\n\n" << options.help() << std::endl;
        std::exit(1);
    }

    if (config.n_stress > MAX_STRESS) {
        config.n_stress = MAX_STRESS;
    }

    return config;
}

void AppConfig::usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --all                 Run all four arms\n");
    fprintf(stderr, "  --arm ARM             Run specific arm:\n");
    fprintf(stderr, "                          single_quiet, dual_quiet,\n");
    fprintf(stderr, "                          single_stress, dual_stress\n");
    fprintf(stderr, "  --samples N           Samples per arm (default: %d)\n", DEFAULT_SAMPLES);
    fprintf(stderr, "  --stress-threads N    Number of stress threads (default: %d)\n", DEFAULT_STRESS);
    fprintf(stderr, "  --channel-bit N       Physical address bit for channel (default: %d)\n", DEFAULT_CHANNEL_BIT);
    fprintf(stderr, "  --channels N          Number of memory channels (default: %d)\n", DEFAULT_NUM_CHANNELS);
    fprintf(stderr, "  --help                Show this help\n");
}