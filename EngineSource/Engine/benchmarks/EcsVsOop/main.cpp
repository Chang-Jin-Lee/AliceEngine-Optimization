#include "EcsVsOop/Workload.h"
#include "EcsVsOop/Equivalence.h"

#include <cstdio>

int main()
{
    using namespace Alice::Bench;

    std::printf("EcsVsOopBench\n");
    std::printf("  sizeof(BenchTransform) = %zu\n", sizeof(BenchTransform));
    std::printf("  sizeof(BenchDecal)     = %zu\n", sizeof(BenchDecal));
    std::printf("  sizeof(BenchAnimation) = %zu\n", sizeof(BenchAnimation));

    {
        const bool ok = CheckEquivalence(1000, 4);
        std::printf("  equivalence(1000, 4) = %s\n", ok ? "PASS" : "FAIL");
        if (!ok)
            return 1;
    }

    return 0;
}
