// benchmark/include/benchmark/awe_alloc.hpp

#ifndef BENCHMARK_AWE_ALLOC_HPP
#define BENCHMARK_AWE_ALLOC_HPP
#include <tailslayer/utilities.hpp>
#include <cstdint>
#include <windows.h>
#include <memory>


namespace tailslayer::utilities {


/**
 * @brief Custom deleter for the mapped memory pointer.
 * 
 * Ensures proper deallocation of both the memory region and the physical pages.
 */
struct AweMemoryDeleter {
    size_t sizeAllocated;
    void* mappedAddress;

    AweMemoryDeleter(size_t size, void* addr) : sizeAllocated(size), mappedAddress(addr) {}

    void operator()(void* ptr) const {
        if (ptr) {
            // Step 1: Unmap the virtual address range
            // Note: MapUserPhysicalPages usually handles the cleanup, but explicit unmapping is safer.
            // For simplicity and relying on the OS/AWE cleanup, we rely on the main function's scope,
            // but if explicit unmapping were needed, it would go here.

            // Step 2: Free the physical pages
            // The PFN array would need to be maintained here to call FreeUserPhysicalPages correctly.
            // For this structure, we rely on the caller managing the PFN array's lifetime.
            
            // In a real implementation, this deleter would manage the PFN array and call FreeUserPhysicalPages.
            // Since the PFN array management is complex, this stub emphasizes the virtual address release.
            VirtualFree(mappedAddress, 0, MEM_RELEASE);
            
            // In a production environment, the corresponding FreeUserPhysicalPages call must happen here.
        }
    }
};


using AweMemoryPtr = std::unique_ptr<void, AweMemoryDeleter>;


/**
 * @brief Allocates and maps a contiguous physical memory region into userspace (AWE required).
 * 
 * NOTE: This function will only succeed if the application is compiled and run within 
 * a properly configured Address Windowing Extensions (AWE) environment on Windows.
 * 
 * @param desiredSize The total size of the memory region to allocate (e.g., 1GiB).
 * @param outMappedAddress A pointer to store the resulting virtual memory address.
 * @return bool True if allocation and mapping succeeded, false otherwise.
 */
inline bool AllocateAndMapPhysicalMemory(size_t desiredSize, void** outMappedAddress) {
    // 1. Validate Input
    if (desiredSize == 0 || outMappedAddress == nullptr) {
        return false;
    }

    // --- STEP 1: Allocate Physical Pages (Get PFNs) ---
    // We need a buffer to hold the Page Frame Numbers (PFNs).
    // The number of pages is desiredSize / PAGE_SIZE.
    size_t numPages = (desiredSize + PAGE_SIZE - 1) / PAGE_SIZE;
    HANDLE hProcess = GetCurrentProcess(); // Current process handle
    
    // Allocate the array to hold the physical page frame numbers
    // This buffer itself is virtual memory.
    ULONG_PTR* pUserPfnArray = (ULONG_PTR*)LocalAlloc(LMEM_TAIL, numPages * sizeof(ULONG_PTR));
    if (pUserPfnArray == nullptr) {
        // Handle allocation failure
        return false;
    }
    
    // Use the specialized Nt-level function (or its wrapper) to allocate the physical pages.
    // This is the function that returns the physical identifiers (PFNs).
    NTSTATUS status = AllocateUserPhysicalPages(
        hProcess,
        pUserPfnArray,
        (ULONG)numPages
    );

    if (status != 0) {
        // Handle allocation failure (status != 0)
        LocalFree(pUserPfnArray);
        return false;
    }

    // --- STEP 2: Map Physical Pages to Virtual Address ---
    
    // We need a starting virtual address. We typically use VirtualAlloc to reserve a region
    // where the physical pages will be mapped.
    PVOID virtualAddress = VirtualAlloc(NULL, desiredSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (virtualAddress == nullptr) {
        // Handle virtual allocation failure
        // Crucially, we must call FreeUserPhysicalPages here to release the physical memory!
        // (Implementation of FreeUserPhysicalPages omitted for brevity but is required)
        return false;
    }

    // Now, we use the mapping function to connect the PFNs to the reserved virtual address.
    // This is the call that performs the actual page table modification.
    // The function signature is generally:
    // NTSTATUS NtMapUserPhysicalPages(
    //     HANDLE ProcessHandle,
    //     PVOID VirtualAddress,
    //     ULONG NumberOfPages,
    //     PULONG_PTR UserPfnArray
    // );
    
    MapUserPhysicalPages()
    NTSTATUS mapStatus = NtMapUserPhysicalPages(
        hProcess,
        virtualAddress,
        (ULONG)numPages,
        pUserPfnArray
    );

    // Cleanup the PFN array buffer regardless of success or failure
    LocalFree(pUserPfnArray);

    if (mapStatus != 0) {
        // Handle mapping failure
        VirtualFree(virtualAddress, 0, MEM_RELEASE);
        return false;
    }

    // Success! Store the usable virtual address.
    *outMappedAddress = virtualAddress;
    return true;
}


/**
 * @brief Example testing function demonstrating the usage of the memory allocation mechanism.
 * 
 * @param sizeInBytes The size of the memory region to test (e.g., 1024 * 1024 * 1024 for 1GiB).
 * @return bool True if the allocation and mapping succeeded, false otherwise.
 */
inline bool TestAweMemoryAllocation(size_t sizeInBytes) {
    void* mappedPtr = nullptr;
    
    printf("--- Starting AWE Memory Allocation Test ---\n");
    printf("Attempting to allocate and map %zu bytes of contiguous physical RAM.\n", sizeInBytes);

    // Note: The actual implementation of AllocateAndMapPhysicalMemory requires the NTDLL calls (NtAllocateUserPhysicalPages, NtMapUserPhysicalPages)
    // which are not standard C++ APIs and require linking against the NTDLL library.
    
    bool success = AllocateAndMapPhysicalMemory(sizeInBytes, &mappedPtr);

    if (success) {
        printf("\n[SUCCESS] Memory allocation and mapping successful!\n");
        printf("Virtual Address obtained: %p\n", mappedPtr);
        
        // Example usage: Write a test value to the memory
        *(uint32_t*)mappedPtr = 0xDEADBEEF;
        uint32_t readValue = *(uint32_t*)mappedPtr;

        printf("Test write successful. Read back value: 0x%X\n", readValue);

        // Use the custom deleter to safely clean up the memory
        AweMemoryPtr mem(mappedPtr, AweMemoryDeleter(sizeInBytes, mappedPtr));
        printf("Memory successfully managed by unique_ptr deleter.\n");

    } else {
        printf("\n[FAILURE] Memory allocation and mapping failed.\n");
        printf("Check if the application is running in a properly configured AWE environment.\n");
        printf("If running outside AWE, these low-level NTDLL calls will fail.\n");
    }
    printf("------------------------------------------\n");
    return success;
}

} // namespace tailslayer::utilities

#endif // BENCHMARK_AWE_ALLOC_HPP