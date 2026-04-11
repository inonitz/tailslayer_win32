#ifndef HW_UTILS_HPP
#define HW_UTILS_HPP


#include <util2/C/platform.h>
#include <cstdint>
#include <cstdio>
#include <array>
#if defined(UTIL2_OS_LINUX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sched.h>
#elif defined(UTIL2_OS_WINDOWS)
#   include <vmmdll.h>
#   include <leechcore.h>
#endif /* */



namespace HardwareUtils {
#if defined(UTIL2_OS_WINDOWS)
    inline VMM_HANDLE gs_initVMM = nullptr;
    inline VMM_HANDLE gs_initLeechCore = nullptr;
#endif /* */


    inline int compute_channel(uint64_t phys, int channel_bit) {
        return (phys >> channel_bit) & 1;
    }
    
#if defined(UTIL2_OS_LINUX)
    /* If HugeTLB Works there is no point in all of these shenanigans */
    inline bool InitializeMemoryReader() { return true; }
    inline void DestroyMemoryReader() {}
    inline bool verifyHugePageIsContiguous(uint64_t vaddrBegin, size_t sizeBytes) { return true; }


    inline uint64_t virt_to_phys(uint64_t vaddr) {
        int fd = open("/proc/self/pagemap", O_RDONLY);
        if (fd < 0) return 0;
        uint64_t entry;
        off_t offset = (vaddr / 4096) * 8;
        if (pread(fd, &entry, 8, offset) != 8) { close(fd); return 0; }
        close(fd);
        if (!(entry & (1ULL << 63))) return 0;
        uint64_t pfn = entry & ((1ULL << 55) - 1);
        return (pfn * 4096) | (vaddr & 0xFFF);
    }
#elif defined(UTIL2_OS_WINDOWS)
    inline bool InitializeMemoryReader() {
        const char* args[] = { "-device", "pmem", "-v" };
    
        VMM_HANDLE hVMM = VMMDLL_Initialize(3, args);
        if (!hVMM) {
            perror("[-] Failed to init VMM. Is winpmem_x64.sys in the build folder?\n");
            return false;
        }
        gs_initVMM = hVMM;
        return true;
    }

    inline void DestroyMemoryReader() {
        VMMDLL_Close(gs_initVMM);
        gs_initVMM = nullptr;
        return;
    }


    inline uint64_t virt_to_phys(uint64_t vaddr) {
        uint64_t physicalAddr;
        bool status = VMMDLL_MemVirt2Phys(gs_initVMM, GetCurrentProcessId(), vaddr, &physicalAddr);

        if(!status) {
            perror("[-] Failed to Read Virtual Address\n");
            return UINT64_MAX;
        }
        fprintf(stderr, "[+] VA 0x%llx -> PA 0x%llx\n", vaddr, physicalAddr);


        // BYTE buf[0x100];
        // DWORD read = 0;
        // status = VMMDLL_MemReadEx(gs_initVMM, -1, 
        //     physicalAddr, 
        //     buf, 
        //     sizeof(buf), 
        //     &read, 
        //     VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO
        // );
        // if(!status) {
        //     perror("[-] Failed to Read Physical Address\n");
        //     return status;
        // }


        // fprintf(stderr, "[+] Read %lu Bytes from Physical memory\n", read);
        return physicalAddr;
    }


    inline bool verifyHugePageIsContiguous(uint64_t vaddrBegin, size_t sizeBytes) {
        if(!gs_initVMM) {
            return false;
        }
        const size_t kMinimumBigPageSize = GetLargePageMinimum();
        SYSTEM_INFO si;
        std::array<ULONG64, 2> paddr{0, 0};
        bool                   isContiguous = true;
        bool                   opsuccess    = false;
        bool                   tmp          = true;
        
        
        GetSystemInfo(&si);
        fprintf(stderr, "GetMinimum() -> 0x%llx\nGetSystemInfo() -> 0x%llx, 0x%llx\n",
            kMinimumBigPageSize,
            (size_t)si.dwAllocationGranularity,
            (size_t)si.dwPageSize
        );
        if(sizeBytes % kMinimumBigPageSize != 0) {
            sizeBytes = (sizeBytes + kMinimumBigPageSize - 1) & ~(kMinimumBigPageSize - 1);
        }
        sizeBytes /= kMinimumBigPageSize;


        opsuccess = VMMDLL_MemVirt2Phys(gs_initVMM, GetCurrentProcessId(), vaddrBegin, &paddr[0]);
        isContiguous = opsuccess; /* Incase of early exit the status will be correct */
        for(size_t lpage_offset = 1; lpage_offset < sizeBytes; ++lpage_offset) {
            opsuccess = VMMDLL_MemVirt2Phys(gs_initVMM, GetCurrentProcessId(), 
                vaddrBegin + kMinimumBigPageSize * lpage_offset, 
                &paddr[lpage_offset % 2]
            );

            
            paddr[lpage_offset % 2] = (opsuccess == false) 
                ? 
                UINT64_MAX 
                : 
                paddr[lpage_offset % 2];
            
            tmp = (paddr[!(lpage_offset % 2)] + kMinimumBigPageSize == paddr[(lpage_offset % 2)]);
            isContiguous = isContiguous && tmp;

            
            fprintf(stderr, "v 0x%llx ->  p0x%llx | p 0x%llx + 0x%llx bytes == p 0x%llx ? -> %s\n", 
                vaddrBegin + kMinimumBigPageSize * lpage_offset, 
                paddr[lpage_offset % 2],

                paddr[!(lpage_offset % 2)], 
                kMinimumBigPageSize, 
                paddr[lpage_offset % 2],

                isContiguous ? "YES" : " NO"
            );
            if(opsuccess == false || !isContiguous) {
                break;
            }
        }


        return isContiguous;
    }


    inline bool initializePCILeech() {
        ULONG64   lcHandle = 0;
        LC_CONFIG lcConfig = { 0 };
        DWORD     cbLcConfig = sizeof(lcConfig);
        
        if(!gs_initVMM) {
            fprintf(stderr, "[-] Could not retrieve LeechCore handle - Initialize the VMM library first\n");
            return false;
        }

        if (!VMMDLL_ConfigGet(gs_initVMM, VMMDLL_OPT_CORE_LEECHCORE_HANDLE, &lcHandle)) {
            fprintf(stderr, "[-] Could not retrieve LeechCore handle.\n");
            return false;
        }

        gs_initLeechCore = reinterpret_cast<VMM_HANDLE>(lcHandle);
        return true;
    }


    // inline PVOID64 allocateHugePageUsingPCILeech(uint64_t size) {
    //     if(!gs_initLeechCore) {
    //         return nullptr;
    //     }
    //     uint64_t vaddr;

    //     fprintf(stderr, "[*] Requesting 1GiB of physically contiguous memory...\n");

        
        
    //     BOOL success = LcCommand(
    //         gs_initLeechCore, 
    //         LC_CMD_VMM
    //         LC_CMD_MEM_ALLOC_CONTIGUOUS, 
    //         sizeof(size), 
    //         (PBYTE)&size, 
    //         (PBYTE*)&vaddr, 
    //         &cbVaResult
    //     );

    //     if (success && vaddr != 0) {
    //         printf("[+] SUCCESS!\n");
    //         printf("[+] Kernel Virtual Address: 0x%llX\n", vaddr);
            
    //         // Convert to Physical Address to verify contiguity
    //         uint64_t pa = UINT64_MAX;
    //         success = VMMDLL_MemVirt2Phys(gs_initVMM, GetCurrentProcessId(), vaddr, &pa); // PID 4 is System
    //         printf("[+] Physical Start Address: 0x%llX\n", pa);
    //         printf("[+] The block spans from PA 0x%llX to 0x%llX\n", pa, pa + (1 << 30));
    //     } else {
    //         printf("[-] Allocation failed. Windows is likely too fragmented.\n");
    //         printf("[-] Advice: Reboot and run this code immediately on startup.\n");
    //     }
    // }

    inline PVOID64 freeHugePagePCILeech(PVOID addr, uint64_t size) {
    
    }
#endif /* */


} // namespace HardwareUtils


#endif // HW_UTILS_HPP
