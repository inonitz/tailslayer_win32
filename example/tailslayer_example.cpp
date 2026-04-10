#include <intrin.h>
#include <tailslayer/hedged_reader.hpp>
#include <iostream>

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


bool checkHugePagesSupport() {
    int cpuInfo[4];
    __cpuid(cpuInfo, 0x80000001);
    // Check bit 26 of EDX
    bool supports1GB = (cpuInfo[3] & (1 << 26)) != 0;
    return supports1GB;
}


struct AddressWindowExtensionsContiguousRegion {
    static inline constexpr uint32_t kGIGABYTE = 1024  * 1024 * 1024;
    PVOID      m_virtualAddress = nullptr;
    ULONG_PTR* m_pfnArray       = nullptr;
    ULONG_PTR  m_pagesRequested = 0;
    ULONG_PTR  m_pagesReceived  = 0;


    ~AddressWindowExtensionsContiguousRegion() {
        if(m_virtualAddress && m_pfnArray) {
            destructManually();
        }
        return;
    }

    AddressWindowExtensionsContiguousRegion() {}


    void* allocateOnce(ULONG_PTR size = kGIGABYTE) {
        if(m_virtualAddress != nullptr || m_pfnArray != nullptr) {
            return nullptr;
        }


        // 1. Get system page size (AWE typically operates on standard 4KB pages)
        SYSTEM_INFO sysInfo;
        ULONG_PTR pageSize = 0;
        
        GetSystemInfo(&sysInfo);
        pageSize = sysInfo.dwPageSize;
        m_pagesRequested = kGIGABYTE / pageSize;


        printf("Attempting to allocate %llu physical pages (%llu bytes per page)...\n", m_pagesRequested, pageSize);
        // 2. Reserve the Virtual Address Window
        m_virtualAddress = VirtualAlloc(
            NULL, 
            kGIGABYTE, 
            MEM_RESERVE | MEM_PHYSICAL, 
            PAGE_READWRITE
        );
        if (m_virtualAddress == NULL) {
            printf("VirtualAlloc (MEM_PHYSICAL) failed. Error: %lu\n", GetLastError());
            return NULL;
        }

    
        // 3. Allocate the Physical Pages
        m_pfnArray = (ULONG_PTR*)malloc(m_pagesRequested * sizeof(ULONG_PTR));
        if (m_pfnArray == NULL) {
            printf("Failed to allocate memory for PFN array.\n");
            VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
            return NULL;
        }


        m_pagesReceived = m_pagesRequested;
        if (!AllocateUserPhysicalPages(GetCurrentProcess(), &m_pagesReceived, m_pfnArray)) {
            tailslayer::utilities::PrintLastError(
                "AllocateUserPhysicalPages failed. Error: %lu\n",
                GetLastError()
            );
            free(m_pfnArray);
            VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
            return NULL;
        }

        if (m_pagesRequested != m_pagesReceived) {
            printf("Could not allocate the full 1GiB. Only got %llu pages.\n", m_pagesReceived);
            // Clean up the partial allocation
            FreeUserPhysicalPages(GetCurrentProcess(), &m_pagesReceived, m_pfnArray);
            free(m_pfnArray);
            VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
            return NULL;
        }

        // 4. The Contiguity Check (The Moment of Truth)
        // A PFN (Page Frame Number) is the physical address divided by the page size.
        // If PFN[1] == PFN[0] + 1, they are physically adjacent.
        bool isPhysicallyContiguous = true;
        for (ULONG_PTR i = 1; i < m_pagesReceived; i++) {
            if (m_pfnArray[i] != m_pfnArray[i - 1] + 1) {
                isPhysicallyContiguous = false;
                printf("Stopped Contiguous region at %llu\n", i);
                break; 
            }
        }

        if (isPhysicallyContiguous) {
            printf("SUCCESS: The 1GiB block is 100%% physically contiguous!\n");
            printf("Starting PFN: %llu (Physical Address approx: 0x%llX)\n", m_pfnArray[0], m_pfnArray[0] * pageSize);
        } else {
            printf("NOTICE: Memory is locked in DRAM, but is physically FRAGMENTED.\n");
        }

        // 5. Map the Physical Pages into the Virtual Window
        if (!MapUserPhysicalPages(m_virtualAddress, m_pagesRequested, m_pfnArray)) {
            printf("MapUserPhysicalPages failed. Error: %lu\n", GetLastError());
            FreeUserPhysicalPages(GetCurrentProcess(), &m_pagesRequested, m_pfnArray);
            free(m_pfnArray);
            VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
            return NULL;
        }
        printf("Successfully mapped physical pages to virtual address: %p\n", m_virtualAddress);


        return m_virtualAddress;
    }


    BOOL destructManually() {
        BOOL status = false;
        status = !MapUserPhysicalPages(m_virtualAddress, m_pagesRequested, NULL);


        //     printf("Unmapping failed. Error: %lu\n", GetLastError());
        // } else {
        //     printf("Virtual-to-Physical mapping removed.\n");
        // }

        // 2. Free the Physical Pages
        // This actually unlocks the DRAM and returns it to the system's free pool.
        // NOTE: This requires the original pfnArray you got from AllocateUserPhysicalPages.
        status = status && 
            !FreeUserPhysicalPages(GetCurrentProcess(), &m_pagesReceived, m_pfnArray);

        //     printf("Freeing physical pages failed. Error: %lu\n", GetLastError());
        // } else {
        //     printf("Physical DRAM pages released.\n");
        // }

        // 3. Release the Virtual Window
        // This returns the virtual address range (the "hole") to the process.
        status = status && !VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
        //     printf("VirtualFree failed. Error: %lu\n", GetLastError());
        // }

        free(m_pfnArray);
        return status;
    }
};



int main() {
    using target_size_t = uint8_t;
    tailslayer::pin_to_core(tailslayer::kCORE_MAIN);

    std::cout 
        << "Start tailslayer demo.\n"
        << "SLAT (Second Level Address Translation): " 
        << (IsProcessorFeaturePresent(PF_SECOND_LEVEL_ADDRESS_TRANSLATION) ?
            "Present\n" : "Not Present\n")
        << "Huge Page Support: " << (checkHugePagesSupport() ? "Present\n" : "Not Present\n");

    
    // if(!tailslayer::utilities::EnableLockMemoryPrivilege()) {
    //     std::cout << "AAAAAAAAAAAAAAAAAAAAAAAAA\n";
    // }

    // AddressWindowExtensionsContiguousRegion testAlloc{};
    // auto* test = testAlloc.allocateOnce();
    // testAlloc.destructManually();
    // std::exit(-1);
    // Example with arguments
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

    tailslayer::utilities::SetLockMemoryPrivilege(false);
    std::cout << "End tailslayer demo.\n";

    return 0;
}