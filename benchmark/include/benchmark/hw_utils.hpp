#ifndef HW_UTILS_HPP
#define HW_UTILS_HPP


#include <util2/C/platform.h>
#include <cstdint>
#include <cstdio>
#if defined(UTIL2_OS_LINUX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sched.h>
#elif defined(UTIL2_OS_WINDOWS)
#   include <vmmdll.h>
#endif /* */



namespace HardwareUtils {
#if defined(UTIL2_OS_WINDOWS)
    inline VMM_HANDLE gs_initVMM = nullptr;
#endif /* */


    inline int compute_channel(uint64_t phys, int channel_bit) {
        return (phys >> channel_bit) & 1;
    }
    
#if defined(UTIL2_OS_LINUX)

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
#endif /* */


} // namespace HardwareUtils


#endif // HW_UTILS_HPP
