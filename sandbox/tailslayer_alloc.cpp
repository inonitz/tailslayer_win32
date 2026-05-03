#include <vmmdll.h>
#include <tailslayer/hedged_reader.hpp>
#include <util2/C/debugbreak.h>
#include <iostream>
#include <tailslayer/proc.hpp>


typedef BOOL (* AllocateUserPhysicalPages2_FuncPtr)(
    HANDLE                  ObjectHandle,
    PULONG_PTR              NumberOfPages,
    PULONG_PTR              PageArray,
    PMEM_EXTENDED_PARAMETER ExtendedParameters,
    ULONG                   ExtendedParameterCount
);


struct PhysRegion {
    ULONG64 vaddr;
    ULONG64 paddr;
    ULONG64 size;
};


inline DWORDLONG getAvailablePhysicalMemory()
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullAvailPhys;
};


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
    uint64_t starting_time_dumb = tailslayer::util::rdtsc_lfence();
    tailslayer::util::rdtsc_lfence();
    uint64_t num_cycles{0};
    do {
        num_cycles = tailslayer::util::rdtsc_lfence() - starting_time_dumb;
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







BOOL SetProcessPriority(DWORD* priority) {
    DWORD oldPriority = GetPriorityClass(GetCurrentProcess());
    BOOL status = SetPriorityClass(GetCurrentProcess(), *priority);


    tailslayer::util::PrintLastError("SetProcessPriority Begin");
    std::cout << "Process Priority Status: Old=" << oldPriority 
        << ", New=" << (status ? *priority : oldPriority) 
        << "\n";
    
    tailslayer::util::PrintLastError("SetProcessPriority End  ");
    
    *priority = oldPriority;
    return status;
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

    
        m_pfnArray = (ULONG_PTR*)malloc(m_pagesRequested * sizeof(ULONG_PTR));
        if (m_pfnArray == NULL) {
            printf("Failed to allocate memory for PFN array.\n");
            VirtualFree(m_virtualAddress, 0, MEM_RELEASE);
            return NULL;
        }


        m_pagesReceived = m_pagesRequested;
        if (!AllocateUserPhysicalPages(GetCurrentProcess(), &m_pagesReceived, m_pfnArray)) {
            tailslayer::util::PrintLastError(
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







struct PhysicalPageAllocator {
    using allocPage    = AllocateUserPhysicalPages2_FuncPtr;
    using PageFrameBuf = std::vector<ULONG_PTR>;
    bool         m_init   = false;
    bool         m_status = true;
    bool         m_reserved[7];
    HMODULE      m_hKernelbase;
    allocPage    m_pageAllocPtr;
    size_t       m_pageSize;
    size_t       m_allocReqSizeInPages; 
    size_t       m_pageFrameBufSize;
    PageFrameBuf m_pageFrames;
    PVOID        m_virtualHugePageAddr;


    PhysicalPageAllocator() {
        m_init   = false;
        m_status = true;


        if(!tailslayer::util::SetLockMemoryPrivilege(true)) {
            tailslayer::util::PrintLastError(
                "PhysicalPageAllocator() Constructor failed. Error: %lu\n",
                GetLastError()
            );
            m_hKernelbase = nullptr;
            m_pageAllocPtr = nullptr;
            return;
        }


        m_hKernelbase = LoadLibraryA("kernelbase.dll");
        if(!m_hKernelbase) {
            tailslayer::util::PrintLastError(
                "PhysicalPageAllocator() Constructor failed. Error: %lu\n",
                GetLastError()
            );
            return;
        }

        m_pageAllocPtr = (allocPage)GetProcAddress(m_hKernelbase, "AllocateUserPhysicalPages2");
        if(!m_pageAllocPtr) {
            tailslayer::util::PrintLastError(
                "PhysicalPageAllocator() Constructor failed. Error: %lu\n",
                GetLastError()
            );
            return;
        }
        



        m_pageSize = GetLargePageMinimum();
        m_allocReqSizeInPages = static_cast<size_t>(getAvailablePhysicalMemory() * 0.7) / m_pageSize;
        m_init = true;
        return;
    }


    bool tryAllocateSizeOnce(size_t desiredSize) {
        MEM_EXTENDED_PARAMETER extended {};
        size_t pagesReceived = UINT64_MAX; 

        if(!m_init) {
            return false;
        }

        if (desiredSize % m_pageSize != 0) { // Round up to nearest multiple
            desiredSize = (desiredSize + m_pageSize - 1) & ~(m_pageSize - 1);
        }
        
        
        memset(&extended, 0x00, sizeof(MEM_EXTENDED_PARAMETER));
        extended.Type = MemExtendedParameterAttributeFlags;
        extended.ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_LARGE;
        desiredSize /= m_pageSize;
        pagesReceived = m_allocReqSizeInPages; 
        m_pageFrames.resize(m_allocReqSizeInPages);

        /* Make sure allocation succeeds & that we allocated enough memory for the request */
        tailslayer::util::PrintLastError("");
        m_status = m_pageAllocPtr(GetCurrentProcess(), &pagesReceived, m_pageFrames.data(), &extended, 1);
        if(!m_status || pagesReceived < desiredSize) {
            tailslayer::util::PrintLastError(
                "AllocateUserPhysicalPages2 failed. Error: %lu\n",
                GetLastError()
            );
            return false;
        }


        /* 
            I found out from Manual verification that windows DOES use huge virtual pages under the hood, even with the normal VirtualAlloc 
            The only issue is getting physically contiguous 2MiB Pages as requested.
        */
        tailslayer::util::PrintLastError("");
        m_virtualHugePageAddr = VirtualAlloc(GetCurrentProcess(), 
            desiredSize * m_pageSize,
            MEM_RESERVE | MEM_PHYSICAL,
            PAGE_READWRITE
        );
        m_status = (m_virtualHugePageAddr != nullptr);
        if(!m_status) {
            tailslayer::util::PrintLastError(
                "VirtualAlloc failed. Error: %lu\n",
                GetLastError()
            );
            m_status = FreeUserPhysicalPages(GetCurrentProcess(), &pagesReceived, m_pageFrames.data());
            tailslayer::util::PrintLastError("Error Status for FreeUserPhysicalPages (0=success)-> %lu\n", GetLastError());
            return false;
        }


        m_pageFrameBufSize = pagesReceived;
        m_status = MapUserPhysicalPages(m_virtualHugePageAddr, m_pageFrameBufSize, m_pageFrames.data());
        tailslayer::util::PrintLastError("");
        if(!m_status) {
            tailslayer::util::PrintLastError(
                "MapUserPhysicalPages failed. Error: %lu\n",
                GetLastError()
            );
            m_status = FreeUserPhysicalPages(GetCurrentProcess(), &pagesReceived, m_pageFrames.data());
            tailslayer::util::PrintLastError("Error Status for FreeUserPhysicalPages (0=success)-> %lu\n", GetLastError());
            m_status = VirtualFree(m_virtualHugePageAddr, desiredSize * m_pageSize, MEM_RELEASE);
            tailslayer::util::PrintLastError("Error Status for VirtualFree (0=success)-> %lu\n", GetLastError());
            return false;
        }


        return true;
    }


    bool free() {
        bool status[4]{ true, true, true, true }; 

        if(m_virtualHugePageAddr) 
        {
            status[0] = MapUserPhysicalPages(m_virtualHugePageAddr, m_pageFrameBufSize, NULL);
            tailslayer::util::PrintLastError("");
            status[1] = VirtualFree(m_virtualHugePageAddr, m_allocReqSizeInPages * m_pageSize, MEM_RELEASE);
            tailslayer::util::PrintLastError("");
        }
        status[2] = FreeUserPhysicalPages(GetCurrentProcess(), &m_allocReqSizeInPages, m_pageFrames.data());
        tailslayer::util::PrintLastError("");
    
        status[3] = tailslayer::util::SetLockMemoryPrivilege(false);
        tailslayer::util::PrintLastError("");

        return status[0] && status[1] && status[2] && status[3];
    }
};




int main() {
    std::cout 
        << "Start tailslayer demo.\n"
        << "SLAT (Second Level Address Translation): " 
        << (IsProcessorFeaturePresent(PF_SECOND_LEVEL_ADDRESS_TRANSLATION) ?
            "Present\n" : "Not Present\n")
        << "Huge Page Support: " << (tailslayer::util::CheckCPUSupportForHugePages() ? "Present\n" : "Not Present\n");

    

    
    tailslayer::util::ProcessorConfigurationVector topo;

    topo.initialize();
    topo.PrintTopology();

    // auto physical = topo.processorMap();
    // printf("Physical Processor Map\n");
    // printf("Key  | GlobalID | PackageID | CoreID | Group | Mask\n");
    // printf("--------------------------------------------\n");
    // for(auto& kv : physical) {
    //     printf("%4u | %8u | %9u | %6u | %5u | 0x%llx\n", 
    //         kv.first, 
    //         kv.second.m_globalID, 
    //         kv.second.m_pkgID, 
    //         kv.second.m_coreID, 
    //         kv.second.m_affinityMask.Group, 
    //         kv.second.m_affinityMask.Mask
    //     );
    // }

    // auto queue = topo.processorQueue();
    // printf("Physical Processor Queue\n");
    // printf("GlobalID | PackageID | CoreID | Group | Mask\n");
    // printf("--------------------------------------------\n");
    // while(!queue.empty()) {
    //     auto& item = queue.front();
    //     printf("%8u | %9u | %6u | %5u | 0x%llx\n", 
    //         item.m_globalID, 
    //         item.m_pkgID, 
    //         item.m_coreID, 
    //         item.m_affinityMask.Group, 
    //         item.m_affinityMask.Mask
    //     );
    //     queue.pop();
    // }

    topo.destroy();
    std::exit(-1);


    const char* args[] = { "-device", "pmem", "-v" };
    VMM_HANDLE hVMM = nullptr;
    DWORD prio = REALTIME_PRIORITY_CLASS;

    
    if(!SetProcessPriority(&prio) || !tailslayer::util::SetLockMemoryPrivilege(true)) {
        std::exit(-1);
    }


    hVMM = VMMDLL_Initialize(3, args);
    if (!hVMM) {
        perror("[-] Failed to init VMM. Is winpmem_x64.sys in the build folder?\n");
    }


    // MEM_EXTENDED_PARAMETER extLarge{};
    // extLarge.Type = MemExtendedParameterAttributeFlags;
    // extLarge.ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_LARGE;
    // void* test = VirtualAlloc2(GetCurrentProcess(), NULL, 2 * 1024 * 1024 * 1024, )


    // AllocationRequest bigRequest{4 * 1024 * 1024 * 1024ull};
    // allocateWithLargePages(bigRequest);
    // freeWithLargePages(bigRequest);

    // tailslayer::util::PrintLastError("AA");
    // auto* test = VirtualAlloc(NULL, 1024ull * 1024 * 1024, 
    //     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, 
    //     PAGE_READWRITE
    // );
    // if(test) {
    //     VirtualFree(test, NULL, MEM_RELEASE);
    // }
    // tailslayer::util::PrintLastError("BB");
    

    // MEM_ADDRESS_REQUIREMENTS requirement;
    // requirement.LowestStartingAddress = NULL;
    // requirement.HighestEndingAddress = NULL;
    // requirement.Alignment = 4ull * 1024 * 1024 * 1024; // align to 4GB boundary

    // MEM_EXTENDED_PARAMETER xp[2];
    // xp[0].Type = MemExtendedParameterAddressRequirements;
    // xp[0].Pointer = &requirement;

    // xp[1].Type = MemExtendedParameterAttributeFlags;
    // xp[1].ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;

    // size_t allocAttemptSize = 6 * getAvailablePhysicalMemory() / 10;
    // allocAttemptSize = (allocAttemptSize + requirement.Alignment - 1) & ~(requirement.Alignment - 1);
    // tailslayer::util::PrintLastError("A");
    // test = VirtualAlloc2 (NULL, NULL, 
    //     allocAttemptSize, 
    //     MEM_RESERVE | MEM_COMMIT, 
    //     PAGE_READWRITE, 
    //     xp, 2
    // );
    // tailslayer::util::PrintLastError("B");
    // if(test) {
    //     VirtualFree(test, NULL, MEM_RELEASE);
    // }


    // PhysicalPageAllocator test;
    
    // auto success = test.tryAllocateSizeOnce(1024 * 1024 * 1024);
    // success = test.free();


//     AllocationRequest bigRequest{4 * 1024 * 1024 * 1024ull};
//     allocateWithLargePages(bigRequest);
    

//     /* Touch every Large Page frame s.t the OS is forced to allocate memory */
//     volatile char* p = reinterpret_cast<char*>(bigRequest.virtaddr);
//     for (size_t i = 0; i < bigRequest.allocSize; i += bigRequest.pageSize) {
//         p[i] = 0; // Force a Page Fault so the OS assigns a physical frame
//     }

//     auto region = FindLargestPhysicalRegion(hVMM, (ULONG64)bigRequest.virtaddr, 
//         bigRequest.allocSize, 
//         bigRequest.pageSize,
//         4 * 1024 * 1024 * 1024ull 
//     );
//     printf("\
// Largest Region Found:\n\
//     Virtual  Address: 0x%llx\n\
//     Physical Address: 0x%llx\n\
//     Size (Bytes):     0x%llx\n\n",
//         region.vaddr,
//         region.paddr,
//         region.size
//     );

//     freeWithLargePages(bigRequest);
    VMMDLL_Close(hVMM);

    tailslayer::util::SetLockMemoryPrivilege(false);
    SetProcessPriority(&prio);
    std::cout << "End tailslayer demo.\n";
    return 0;
}




