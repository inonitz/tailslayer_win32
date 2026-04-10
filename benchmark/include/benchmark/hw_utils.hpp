#ifndef HW_UTILS_HPP
#define HW_UTILS_HPP


#include <util2/C/platform.h>
#include <vmmdll.h>
#include <cstdint>
#if defined(UTIL2_OS_LINUX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sched.h>
#elif defined(UTIL2_OS_WINDOWS)
#endif /* */


namespace HardwareUtils {
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
    inline uint64_t virt_to_phys(uint64_t vaddr) {
        
    }
#endif /* */


} // namespace HardwareUtils


#endif // HW_UTILS_HPP
