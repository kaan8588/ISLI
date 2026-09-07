#include "cpu_features.hpp"
#include <mutex>
#include <thread>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#else
#include <cpuid.h>
#include <immintrin.h>
#endif

static inline void cpuidCount(unsigned leaf, unsigned subleaf, unsigned& eax, unsigned& ebx, unsigned& ecx, unsigned& edx) {
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    eax = regs[0]; ebx = regs[1]; ecx = regs[2]; edx = regs[3];
#else
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
#endif
}

static inline uint64_t readXcr0() {
#if defined(_MSC_VER)
    return _xgetbv(0);
#else
    uint32_t eax, edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#endif
}

static void detectCpu(CpuFeatures& f) {
    unsigned a = 0, b = 0, c = 0, d = 0;

    // Check highest supported basic function
    cpuidCount(0, 0, a, b, c, d);
    unsigned maxLeaf = a;

    if (maxLeaf >= 1) {
        cpuidCount(1, 0, a, b, c, d);
        f.sse42 = (c & (1u << 20)) != 0;
        f.fma   = (c & (1u << 12)) != 0;
        f.avx   = (c & (1u << 28)) != 0;
        bool osxsave = (c & (1u << 27)) != 0;

        uint64_t xcr0 = 0;
        if (osxsave) {
            xcr0 = readXcr0();
        }
        f.osSavesYmm = (xcr0 & 0x6) == 0x6;
        f.osSavesZmm = (xcr0 & 0xE6) == 0xE6;
    }

    if (maxLeaf >= 7) {
        cpuidCount(7, 0, a, b, c, d);
        f.avx2     = ((b & (1u << 5)) != 0) && f.osSavesYmm;
        f.avx512f  = ((b & (1u << 16)) != 0) && f.osSavesZmm;
        f.avx512dq = ((b & (1u << 17)) != 0) && f.osSavesZmm;
        f.avx512bw = ((b & (1u << 30)) != 0) && f.osSavesZmm;
        f.avx512vl = ((b & (1u << 31)) != 0) && f.osSavesZmm;

        cpuidCount(7, 1, a, b, c, d);
        f.avx10 = ((d & (1u << 19)) != 0) && f.osSavesYmm;
        if (f.avx10) {
            cpuidCount(0x24, 0, a, b, c, d);
            f.avx10Version = b & 0xFF;
            f.avx10_512 = ((b & (1u << 18)) != 0) && f.osSavesZmm;
        }
    }

    // Processor Brand String
    cpuidCount(0x80000000, 0, a, b, c, d);
    if (a >= 0x80000004) {
        char* p = f.brand;
        for (unsigned leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuidCount(leaf, 0, a, b, c, d);
            std::memcpy(p + 0,  &a, 4);
            std::memcpy(p + 4,  &b, 4);
            std::memcpy(p + 8,  &c, 4);
            std::memcpy(p + 12, &d, 4);
            p += 16;
        }
        f.brand[48] = '\0';
    } else {
        std::strncpy(f.brand, "x86_64 Processor", sizeof(f.brand) - 1);
    }

    f.logicalCores = static_cast<int>(std::thread::hardware_concurrency());
    if (f.logicalCores < 1) f.logicalCores = 1;
    f.physicalCores = f.logicalCores > 1 ? f.logicalCores / 2 : 1;

    f.preferredVectorBytes = (f.avx512f || f.avx10_512) ? 64 : 32;
}

const CpuFeatures& cpuFeatures() {
    static CpuFeatures features;
    static std::once_flag flag;
    std::call_once(flag, []() {
        detectCpu(features);
    });
    return features;
}
