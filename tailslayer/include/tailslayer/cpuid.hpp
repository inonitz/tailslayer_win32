#ifndef __CPUID_DEFINITION_HEADER__
#define __CPUID_DEFINITION_HEADER__
#include <util2/C/platform.h>
#include <cstdint>


#if defined(UTIL2_OS_WINDOWS)
#   include <intrin.h>
#endif


class CPUID 
{
public:
    explicit CPUID(unsigned i) {
#if defined(UTIL2_OS_WINDOWS)
        __cpuid((int *)m_regs, (int)i);
        return;
#else
        /* ECX is set to zero for CPUID function 4 */
        __asm__ volatile("cpuid" 
            : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
            : "a" (i), "c" (0)
        );
        return;
#endif
    }

  const uint32_t &EAX() const {return m_regs[0];}
  const uint32_t &EBX() const {return m_regs[1];}
  const uint32_t &ECX() const {return m_regs[2];}
  const uint32_t &EDX() const {return m_regs[3];}

private:
    uint32_t m_regs[4];
};


#endif // __CPUID_DEFINITION_HEADER__
