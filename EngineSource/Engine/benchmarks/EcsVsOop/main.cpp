#include "EcsVsOop/Workload.h"

#include <cstdio>

int main()
{
    using namespace Alice::Bench;

    std::printf("EcsVsOopBench\n");
    std::printf("  sizeof(BenchTransform) = %zu\n", sizeof(BenchTransform));
    std::printf("  sizeof(BenchDecal)     = %zu\n", sizeof(BenchDecal));
    std::printf("  sizeof(BenchAnimation) = %zu\n", sizeof(BenchAnimation));
    return 0;
}
