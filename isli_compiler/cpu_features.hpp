#ifndef ISLI_CPU_FEATURES_HPP
#define ISLI_CPU_FEATURES_HPP

#include <cstdint>
#include <cstddef>

struct CpuFeatures {
    bool sse42 = false;
    bool avx = false;
    bool avx2 = false;
    bool fma = false;
    bool avx512f = false;
    bool avx512vl = false;
    bool avx512dq = false;
    bool avx512bw = false;
    bool avx10 = false;
    int  avx10Version = 0;
    bool avx10_512 = false;
    bool osSavesYmm = false;
    bool osSavesZmm = false;

    int  logicalCores = 0;
    int  physicalCores = 0;
    int  l1dBytes = 32768;
    int  l2Bytes = 1 << 20;
    int  l3Bytes = 32 << 20;
    int  preferredVectorBytes = 32;
    char brand[49] = {0};
};

const CpuFeatures& cpuFeatures();

#endif // ISLI_CPU_FEATURES_HPP
