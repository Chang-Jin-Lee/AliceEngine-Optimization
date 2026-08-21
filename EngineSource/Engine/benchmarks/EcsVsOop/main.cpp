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
        // 비포화 구간: 세 컴포넌트 모두 선형이라 어느 쪽의 산술 오류든 값 차이로 드러난다.
        const bool ok = CheckEquivalence(1000, 4);
        std::printf("  equivalence(1000, 4, dt=0.05) [비포화] = %s\n", ok ? "PASS" : "FAIL");
        if (!ok)
            return 1;
    }

    {
        // 포화 구간: Decal 클램프와 Animation 랩어라운드(데이터 의존 분기)에서도 양쪽이 일치하는지 본다.
        // 이 구간은 값이 포화돼 검출력이 없으므로 위 호출을 대체할 수 없다.
        const bool ok = CheckEquivalence(200, 50, 0.25f);
        std::printf("  equivalence(200, 50, dt=0.25) [포화] = %s\n", ok ? "PASS" : "FAIL");
        if (!ok)
            return 1;
    }

    return 0;
}
