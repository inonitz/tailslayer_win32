#include <tailslayer/hedged_reader.hpp>
#include <iostream>
#include <vector>


/*
    Example user functions that could be passed to tailslayer
*/

// Example with arguments
__force_inline inline std::size_t dummy_read_signal2(int arg1, int arg2) {
    std::cout << "Hi with args: " << arg1 << " " << arg2 << "\n";
    return 0; // Index to read
}

template <typename T>
__force_inline inline void dummy_final_work2(T val, int arg2) {
    std::cout << "Hi with args: " << val << " " << arg2 << "\n";
}

// Example with no arguments
__force_inline inline std::size_t dummy_read_signal() {
    // UPDATE HERE - signal
    // This is the signal that the worker will wait for
    // Once this loop completes, the read will be triggered
    uint64_t starting_time_dumb = tailslayer::utilities::rdtsc_lfence();
    tailslayer::utilities::rdtsc_lfence();
    uint64_t num_cycles{0};
    do {
        num_cycles = tailslayer::utilities::rdtsc_lfence() - starting_time_dumb;
    } while (num_cycles < 2000000000);

    // UPDATE HERE - index
    std::size_t index_to_read = 1; // Desired index to read (example reading the second value 0x44)
    return index_to_read;
}

template <typename T>
__force_inline inline void dummy_final_work(T val) {
    // UPDATE HERE - final work
    // This is the function that will be executed with the value as soon as the signal function finishes
    asm volatile("" :: "r"(val)); // Dummy using value
    std::cout << "Val: " << val << "\n";
}


int main() {
    using target_size_t = uint8_t;


    tailslayer::utilities::pin_to_core(tailslayer::kCORE_MAIN);    
    tailslayer::HedgedReader<target_size_t, dummy_read_signal2, dummy_final_work2<target_size_t>, tailslayer::ArgList<1, 2>, tailslayer::ArgList<2>> reader_args{};
    reader_args.insert(0x43);
    reader_args.insert(0x44);
    reader_args.start_workers();

    // Example with no arguments
    // tailslayer::HedgedReader<
    //     target_size_t, 
    //     dummy_read_signal, 
    //     dummy_final_work<target_size_t>
    // > reader{};
    // reader.insert(0x43);
    // reader.insert(0x44);
    // reader.start_workers();

    std::cout << "End tailslayer demo.\n";
    return 0;
}