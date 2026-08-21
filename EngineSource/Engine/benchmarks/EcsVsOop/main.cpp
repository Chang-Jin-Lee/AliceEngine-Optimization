#include "EcsVsOop/EcsBackend.h"
#include "EcsVsOop/Equivalence.h"
#include "EcsVsOop/Measure.h"
#include "EcsVsOop/OopBackend.h"
#include "EcsVsOop/Stats.h"
#include "EcsVsOop/Workload.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Alice::Bench;
    using Alice::EntityId;
    using Alice::BenchStats::Median;
    using Alice::BenchStats::StdDev;

    constexpr int kRepeat = 11;      // 앞 2회는 워밍업으로 버린다. Global Constraints - 바꾸지 않는다.
    constexpr int kWarmup = 2;
    constexpr int kStepsPerRun = 10; // 순회 1회 측정에 포함되는 Step 횟수
    constexpr std::size_t kRandomProbes = 100000;

    const std::size_t kSweep[] = { 100, 500, 1000, 5000, 10000, 50000 };

    // Release 빌드에서만 CSV/환경 파일이 조용히 덮이는 것을 허용한다. Debug로 돌리면
    // 같은 경로에 아무 표시 없이 덮이므로, 아래 main()에서 큰 경고를 찍는다.
#ifdef NDEBUG
    constexpr bool kIsNdebugBuild = true;
#else
    constexpr bool kIsNdebugBuild = false;
#endif

    /// 무작위지만 재현 가능한 ID 순열
    std::vector<EntityId> ShuffledIds(std::size_t n, unsigned seed)
    {
        std::vector<EntityId> ids(n);
        for (std::size_t i = 0; i < n; ++i)
            ids[i] = static_cast<EntityId>(i + 1);
        std::mt19937 rng(seed);
        std::shuffle(ids.begin(), ids.end(), rng);
        return ids;
    }

    /// NA 문자열 렌더링. 측정하지 않은 칸에 리터럴 0을 쓰면 "0회 할당"으로 오독되므로,
    /// PowerShell에서 [double]로 캐스트 시 예외를 던지는 "NA"를 쓴다 ([double]''는 조용히 0이 된다).
    std::string AllocCell(bool has, std::uint64_t v)
    {
        return has ? std::to_string(v) : std::string("NA");
    }
    std::string AllocCellSigned(bool has, std::int64_t v)
    {
        return has ? std::to_string(v) : std::string("NA");
    }

    struct MinMax
    {
        double min = 0.0;
        double max = 0.0;
    };

    /// 수정 12: 칸 안(9개 비워밍업 표본)의 최소/최대. stdDevMs는 칸 내부 분산만 담고
    /// 칸과 칸 사이의 실행 간 드리프트는 담지 못하므로, 표본 자체의 극값을 CSV에 남긴다.
    /// 짧은 구간 측정에서 OS 스케줄링 간섭은 항상 시간을 "늘리기만" 하므로(음의 방향 간섭은 없다),
    /// minMs가 실제로 가장 편향이 적은 추정치다 - Task 9에서 median과 나란히 놓을 근거가 된다.
    MinMax ComputeMinMax(const std::vector<double>& samples)
    {
        if (samples.empty())
            return MinMax{};
        const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
        return MinMax{ *minIt, *maxIt };
    }

    struct Row
    {
        std::string backend;
        std::string op;
        std::size_t n = 0;
        double medianMs = 0.0;
        double stdDevMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        bool hasAllocStats = false;    // add 행만 true. step/random/remove는 할당을 재지 않는다.
        std::uint64_t allocCount = 0;
        std::uint64_t allocBytes = 0;      // 누적 요청 바이트 (churn). 실사용량이 아니다.
        std::int64_t peakLiveBytes = 0;    // 메모리 전용 패스(MeasurePeakLiveBytes)에서 구한 값.
        std::uint64_t workingSet = 0;
    };

    /// 수정 11-b: 메모리 전용 패스. 시간을 재지 않으므로 g_trackLiveBytes를 켜서 _msize를 써도 된다.
    /// 창(window) 안에서 백엔드를 생성-충전-파괴까지 마쳐 alloc/free 짝이 창을 넘지 않게 만든다.
    /// 따라서 창이 닫히면 g_liveBytes는 정확히 0으로 돌아와야 한다 - 0이 아니면 new/delete
    /// 회계에 버그가 있다는 뜻이고, 이 자기검증이 이 설계의 핵심 이점이다.
    /// peakLiveBytes는 Add 도중(전부 살아있는 시점)에만 최댓값에 도달하므로, 파편화 유무와
    /// 무관하게 N당 한 번만 구해 두 fragmented 행에 동일한 값을 쓴다.
    template <typename Backend>
    std::int64_t MeasurePeakLiveBytes(std::size_t n, std::int64_t& outResidual)
    {
        g_trackLiveBytes = true;
        g_liveBytes = 0;
        g_peakLiveBytes = 0;
        {
            Backend backend;
            for (std::size_t i = 0; i < n; ++i)
                backend.Add(static_cast<EntityId>(i + 1));
            // peak은 이 시점(전부 살아있는 상태)에서 최대다.
        }   // 소멸자가 전부 해제한다
        const std::int64_t peak = g_peakLiveBytes;
        outResidual = g_liveBytes;
        g_trackLiveBytes = false;
        return peak;
    }

    /// 백엔드 하나를 N개 규모로 측정한다.
    /// fragmented=true면 30%를 삭제한 뒤 재삽입해 힙을 파편화시킨 상태에서 순회를 잰다.
    /// addPeakLiveBytes는 MeasurePeakLiveBytes()가 별도 패스에서 구한 값을 그대로 받아
    /// add 행에 채우기만 한다 - 시간 패스 안에서는 _msize를 절대 부르지 않는다(수정 11).
    template <typename Backend>
    void MeasureBackend(const char* name, std::size_t n, bool fragmented, std::vector<Row>& out,
        double& outChecksum, std::int64_t addPeakLiveBytes)
    {
        std::vector<double> addSamples;
        std::vector<double> stepSamples;
        std::vector<double> removeSamples;
        std::vector<double> randomSamples;
        std::vector<std::uint64_t> addAllocCountSamples; // 수정 10: add 할당 횟수가 반복마다 결정적인지 검증

        std::uint64_t addAllocCount = 0;
        std::uint64_t addAllocBytes = 0;
        std::uint64_t peakWorkingSet = 0;
        outChecksum = 0.0;

        const std::vector<EntityId> order = ShuffledIds(n, 12345u);

        // 랜덤 접근용 조회 순서를 타이머 밖에서 미리 만든다.
        // 타이머 안에서 k % n을 돌리면 런타임 제수에 대한 64비트 나눗셈이 100,000회 걸려
        // 백엔드 조회보다 비싼 하네스 비용이 측정치의 바닥값이 되어버린다
        // (수정 전 증상: N=100과 N=500에서 ECS 값이 바이트 단위로 동일한 0.184ms였다).
        std::vector<EntityId> probe(kRandomProbes);
        for (std::size_t k = 0; k < kRandomProbes; ++k)
            probe[k] = order[k % n];

        for (int r = 0; r < kRepeat; ++r)
        {
            // 수정 5: ResetAllocStats()를 Backend backend; 보다 먼저 둔다.
            // MSVC의 unordered_map은 기본 생성 시 sentinel 노드를 할당하므로, 순서가 반대면
            // OOP 쪽 할당 1~2건이 집계에서 빠진다 - ECS 생성자는 아무것도 할당하지 않아 이 누락은
            // 비대칭이고 항상 OOP에 유리한 방향이다.
            ResetAllocStats();
            Backend backend;

            // --- 추가 ---
            // ReadAllocStats()는 push_back보다 먼저 호출해야 한다. addSamples.push_back()
            // 자체가 힙 할당을 일으킬 수 있어서, 순서를 바꾸면 그 할당이 백엔드의 할당 수에
            // 섞여 들어간다. N=100 근처에서는 ECS 쪽 총 할당이 수십 건뿐이라 단 1건의 오차도
            // 결과를 몇 퍼센트씩 흔든다.
            // 수정 11: g_trackLiveBytes는 여기서 절대 켜지 않는다 - _msize 호출은 할당 횟수에
            // 비례하는 계측 비용을 만들고, 그 축이 정확히 두 백엔드가 1600배 차이 나는 축이라
            // OOP add를 약 15% 부풀린다(라운드 1 실측: _msize 있으면 14.0ms, 없으면 12.05~12.20ms).
            {
                ScopedTimer timer;
                for (std::size_t i = 0; i < n; ++i)
                    backend.Add(static_cast<EntityId>(i + 1));
                const double elapsed = timer.ElapsedMs();
                const AllocStats a = ReadAllocStats();
                if (r >= kWarmup)
                {
                    addSamples.push_back(elapsed);
                    addAllocCount = a.count;
                    addAllocBytes = a.bytes;
                    addAllocCountSamples.push_back(a.count);
                }
            }

            // --- 파편화 (요청 시) ---
            if (fragmented)
            {
                // 계측 대상(order[0, cut))과 겹치지 않는 구간을 쓴다.
                // 겹치면 방금 append된 연속 블록을 지우는 최선 케이스를 재게 되어
                // 파편화가 오히려 빨라진다(수정 전 실측에서 OOP remove가 2.76ms에서 1.19ms로 나왔다).
                // cut = 0.3n이므로 2*cut = 0.6n <= n이 항상 성립해 범위를 벗어나지 않는다.
                const std::size_t cut = n * 3 / 10;
                for (std::size_t i = cut; i < cut * 2; ++i)
                    backend.Remove(order[i]);
                for (std::size_t i = cut; i < cut * 2; ++i)
                    backend.Add(order[i]);
            }

            // --- 순회 ---
            {
                ScopedTimer timer;
                for (int s = 0; s < kStepsPerRun; ++s)
                    backend.Step(0.016f);
                if (r >= kWarmup)
                    stepSamples.push_back(timer.ElapsedMs() / kStepsPerRun);
            }

            if (r == kRepeat - 1)
            {
                // 결과를 실제로 읽어 데드 스토어 제거를 구조적으로 막는다. 지금은 /GL이 꺼져 있어
                // 일어나지 않지만, 미래에 LTCG가 켜지면 StepDecal/StepAnimation 루프가 합법적으로
                // 사라질 수 있다 - 그러면 다른 징후 없이 수치만 몇 배 좋아진다.
                // 동시에 이 값이 두 백엔드에서 같은지 보면 N 전 구간·파편화 유무 양쪽에서
                // 동등성이 재확인된다. id 오름차순으로 더해야 두 백엔드의 부동소수점 덧셈 순서가
                // 같아져 비트 단위로 일치한다. 타이머 밖이므로 GetDecal/GetAnimation 호출은
                // 계측 오염이 아니다.
                double sum = 0.0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const EntityId id = static_cast<EntityId>(i + 1);
                    if (const BenchTransform* t = backend.GetTransform(id))
                        sum += t->position.x + t->position.y + t->rotation.y;
                    if (const BenchDecal* d = backend.GetDecal(id))
                        sum += d->lifetime + d->fade;
                    if (const BenchAnimation* a = backend.GetAnimation(id))
                        sum += a->timeSec;
                }
                outChecksum = sum;
            }

            const std::uint64_t ws = WorkingSetBytes();
            if (ws > peakWorkingSet)
                peakWorkingSet = ws;

            // --- 랜덤 접근 ---
            {
                std::uint64_t sink = 0;
                ScopedTimer timer;
                for (std::size_t k = 0; k < kRandomProbes; ++k)
                {
                    const BenchTransform* t = backend.GetTransform(probe[k]);
                    if (t != nullptr)
                        sink += static_cast<std::uint64_t>(t->position.x);
                }
                if (r >= kWarmup)
                    randomSamples.push_back(timer.ElapsedMs());
                if (sink == 0xFFFFFFFFull)
                    std::printf(" ");  // 최적화 제거 방지
            }

            // --- 삭제 (30%) ---
            {
                const std::size_t cut = n * 3 / 10;
                ScopedTimer timer;
                for (std::size_t i = 0; i < cut; ++i)
                    backend.Remove(order[i]);
                if (r >= kWarmup)
                    removeSamples.push_back(timer.ElapsedMs());
            }
        }

        // 수정 10: addAllocCount는 매 non-warmup repeat에서 덮어써 마지막 1개만 남기고 있었다.
        // 구조상 결정적이어야 하므로, 버리지 않고 9개를 전부 모아 실제로 같은지 확인한다.
        {
            bool deterministic = true;
            std::uint64_t minCount = addAllocCountSamples.empty() ? 0 : addAllocCountSamples.front();
            std::uint64_t maxCount = minCount;
            for (const std::uint64_t c : addAllocCountSamples)
            {
                minCount = (c < minCount) ? c : minCount;
                maxCount = (c > maxCount) ? c : maxCount;
                if (c != addAllocCountSamples.front())
                    deterministic = false;
            }
            const char* suffixLog = fragmented ? " (fragmented)" : "";
            if (!deterministic)
            {
                std::printf("  경고: %s add allocCount가 반복마다 다르다 N=%zu%s min=%llu max=%llu\n",
                    name, n, suffixLog,
                    static_cast<unsigned long long>(minCount), static_cast<unsigned long long>(maxCount));
            }
            else
            {
                std::printf("  %s add allocCount 결정적: N=%zu%s count=%llu (%d회 반복 전부 동일)\n",
                    name, n, suffixLog, static_cast<unsigned long long>(addAllocCount),
                    static_cast<int>(addAllocCountSamples.size()));
            }
        }

        const MinMax addMinMax = ComputeMinMax(addSamples);
        const MinMax stepMinMax = ComputeMinMax(stepSamples);
        const MinMax randomMinMax = ComputeMinMax(randomSamples);
        const MinMax removeMinMax = ComputeMinMax(removeSamples);

        const char* suffix = fragmented ? " (fragmented)" : "";
        out.push_back(Row{ name, std::string("add") + suffix, n,
            Median(addSamples), StdDev(addSamples), addMinMax.min, addMinMax.max,
            true, addAllocCount, addAllocBytes, addPeakLiveBytes, peakWorkingSet });
        out.push_back(Row{ name, std::string("step") + suffix, n,
            Median(stepSamples), StdDev(stepSamples), stepMinMax.min, stepMinMax.max,
            false, 0, 0, 0, peakWorkingSet });
        out.push_back(Row{ name, std::string("random") + suffix, n,
            Median(randomSamples), StdDev(randomSamples), randomMinMax.min, randomMinMax.max,
            false, 0, 0, 0, peakWorkingSet });
        out.push_back(Row{ name, std::string("remove") + suffix, n,
            Median(removeSamples), StdDev(removeSamples), removeMinMax.min, removeMinMax.max,
            false, 0, 0, 0, peakWorkingSet });
    }

    /// 하드웨어·빌드·실행 환경 기록. Task 9 공시와 재현성 검증의 근거 자료다.
    std::string BuildEnvironmentReport(bool affinityOk, DWORD_PTR affinityMask,
        bool priorityClassOk, bool threadPriorityOk, LONGLONG qpfFrequency)
    {
        std::ostringstream os;
        os << "=== 실행 환경 ===\n";
        os << "-- 하드웨어 (확보된 정보, 그대로 기록) --\n";
        os << "CPU: 13th Gen Intel(R) Core(TM) i9-13900KF  (Raptor Lake, P-core = Golden Cove)\n";
        os << "Cores/Threads: 24C/32T  (P-core 8 + E-core 16)\n";
        os << "L2: P-core당 2MB, 총 32MB / L3: 36MB\n";
        os << "RAM: 128 GB DDR5-4800, 4 modules\n";
        os << "OS: Windows 11 Pro 10.0.26200\n";
        os << "참고: N=50000에서도 ECS 약 4.6MB, OOP 약 16MB로 둘 다 L3(36MB) 안에 들어간다.\n";
        os << "     즉 이 스윕은 DRAM 대역폭이 아니라 캐시 계층 국소성을 재는 것이다 (Task 9 공시 사항).\n";
        os << "-- 빌드 구성 (build/EcsVsOopBench.vcxproj Release|x64에서 직접 재확인) --\n";
        os << "Configuration: Release, x64, MSVC(PlatformToolset v145)\n";
        os << "C++ 표준: C++20 (LanguageStandard=stdcpp20)\n";
        os << "최적화: /O2 (Optimization=MaxSpeed), /Ob2 (InlineFunctionExpansion=AnySuitable)\n";
        os << "런타임: /MD (RuntimeLibrary=MultiThreadedDLL)\n";
        os << "/GL 없음 (WholeProgramOptimization 미설정)\n";
        os << "/arch 미설정 -> x64 기본 SSE2 베이스라인 (EnableEnhancedInstructionSet 미설정)\n";
        os << "/fp:precise (FloatingPointModel 미설정 = MSVC 기본값)\n";
        os << "-- 구조체 크기 (모든 메모리 주장의 근거) --\n";
        os << "sizeof(BenchTransform) = " << sizeof(BenchTransform) << "\n";
        os << "sizeof(BenchDecal)     = " << sizeof(BenchDecal) << "\n";
        os << "sizeof(BenchAnimation) = " << sizeof(BenchAnimation) << "\n";
        os << "-- 반복 정책 --\n";
        os << "총 " << kRepeat << "회 반복, 앞 " << kWarmup << "회 폐기, 남은 "
           << (kRepeat - kWarmup) << "회의 중앙값(median)과 표준편차(n-1)를 쓴다. (Global Constraints - 고정값)\n";
        os << "-- 타이머 --\n";
        os << "QueryPerformanceFrequency() 실측값 = " << qpfFrequency << " Hz ("
           << (qpfFrequency > 0 ? (1e9 / static_cast<double>(qpfFrequency)) : 0.0) << " ns/tick)\n";
        os << "-- 하이브리드 CPU 대응 (P-core 8 + E-core 16) --\n";
        os << "스레드가 P-core에서 E-core로 마이그레이션하면 같은 코드가 2배 가까이 느려질 수 있고,\n";
        os << "스윕이 항상 ECS 다음 OOP 순으로 돌기 때문에 그 오차가 백엔드 차이로 오인될 수 있다.\n";
        os << "SetPriorityClass(HIGH_PRIORITY_CLASS): " << (priorityClassOk ? "성공" : "실패") << "\n";
        os << "SetThreadPriority(THREAD_PRIORITY_HIGHEST): " << (threadPriorityOk ? "성공" : "실패") << "\n";
        if (affinityOk)
            os << "SetThreadAffinityMask(logical processor 2, P-core): 성공 (이전 마스크=0x"
               << std::hex << affinityMask << std::dec << ")\n";
        else
            os << "SetThreadAffinityMask(logical processor 2, P-core): 실패 - 측정치에 코어 마이그레이션 오차가 섞일 수 있다.\n";
        os << "-- 스윕 순서 --\n";
        os << "각 N, 각 fragmented 상태에서 항상 ECS를 먼저, OOP를 나중에 측정한다.\n";
        os << "-- 빌드 구성 플래그 --\n";
        os << "NDEBUG " << (kIsNdebugBuild ? "정의됨 (Release)" : "정의되지 않음 (Debug!)") << "\n";

        // 수정 13: 고치지 못했거나 고칠 수 없는 관측 두 가지. 설명 못 하는 것을 숨기는 게 최악이므로
        // 관측 사실과 가설을 그대로 남긴다. 가설은 검증되지 않았음을 명시한다.
        os << "\n-- 설명하지 못한 관측 (수정하지 않음, 기록만 함) --\n";
        os << "[13-a] N=50000에서 실행 간 분산이 최대 약 2배로 크다 (예: ecs remove 비파편화가\n";
        os << "  한 실행에서는 0.2789ms, 다른 실행에서는 0.5638ms). minMs/maxMs 컬럼(수정 12)이\n";
        os << "  이 분산을 데이터로 남긴다. 원인은 확정하지 못했지만 유력한 가설은 다음과 같다:\n";
        os << "  스레드를 논리 프로세서 2번에 고정했는데, 이 기계는 SMT(하이퍼스레딩)가 켜져 있어\n";
        os << "  2번과 3번이 같은 물리 P-core의 두 하드웨어 스레드다. 3번에 다른 프로세스가 올라오면\n";
        os << "  같은 코어의 실행 자원(디코드/실행 포트/캐시 대역)을 나눠 쓰게 되어 처리량이 절반\n";
        os << "  가까이 떨어질 수 있다. Win32에는 형제 스레드까지 예약하는 표준적인 방법이 없으므로\n";
        os << "  이 벤치는 그 간섭을 배제하지 못한다. minMs 컬럼이 이 간섭의 영향을 가장 적게 받는\n";
        os << "  값이다 - 짧은 구간 측정에서 스케줄링 간섭은 시간을 늘리기만 하기 때문이다.\n";
        os << "  ComponentStorage::Remove()의 소스를 직접 읽어 확인했다: swap-and-pop이 pop_back()만\n";
        os << "  하고 용량을 줄이지 않으므로 힙 연산(operator new/delete)을 전혀 하지 않는다. 즉 ECS\n";
        os << "  remove의 실행 간 분산은 _msize/할당자 계측 비용으로 설명되지 않으며, 코드 결함이\n";
        os << "  아니라 위 SMT 간섭 같은 측정 잡음일 가능성이 높다는 근거가 된다.\n";
        os << "[13-b] random (fragmented)가 여전히 비파편화보다 약간 더 빠르다 (수정 4로 버그 규모의\n";
        os << "  효과(-56.7%, OOP remove 기준)는 사라졌지만, random에는 작은 잔여가 남아 여러 번\n";
        os << "  재현된다: 예) OOP N=50000 비파편화 2.4679ms -> 파편화 2.1436ms(-13.1%),\n";
        os << "  ECS 0.1711ms -> 0.1403ms(-18.0%)). 가설(검증되지 않음): 파편화 단계가 방금\n";
        os << "  order[cut..2*cut) 구간의 엔티티와 그 해시/인덱스 노드를 직전에 만졌으므로, 뒤따르는\n";
        os << "  랜덤 조회가 order[] 전체를 고르게 순회할 때 이 구간을 만나면 이미 캐시/TLB에\n";
        os << "  '뜨거운' 상태일 수 있다는 캐시 워밍 가설이다. 이 가설은 검증하지 않았다.\n";
        os << "  같은 항목에 remove (fragmented)의 OOP 잔여(-3.2%, 3.0920ms->2.9932ms)도 기록한다 -\n";
        os << "  표준편차(약 0.34~0.38ms, 상대 11~12%)에 비해 작아 노이즈 범위로 판단했다.\n";
        return os.str();
    }
}

int main()
{
    // 수정 7: 하이브리드 CPU(P-core 8 + E-core 16)에서 스레드가 E-core로 옮겨가면 같은 코드가
    // 2배 가까이 느려진다. 스윕이 ECS 다음 OOP 순으로 도므로 마이그레이션 오차가 백엔드 차이로
    // 오인된다. main() 진입 직후, 다른 어떤 측정보다 먼저 고정한다.
    // 논리 프로세서 2번(P-core, 0번은 인터럽트 처리가 몰리므로 피한다)에 고정하고 우선순위를 올린다.
    const bool priorityClassOk = SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) != 0;
    const bool threadPriorityOk = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
    const DWORD_PTR affinity = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << 2);
    const bool affinityOk = affinity != 0;
    if (!affinityOk)
        std::printf("경고: 스레드 친화도 설정 실패. 측정치에 코어 마이그레이션 오차가 섞일 수 있다.\n");

    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);

    const std::string envReport = BuildEnvironmentReport(affinityOk, affinity, priorityClassOk, threadPriorityOk, qpf.QuadPart);
    std::printf("%s\n", envReport.c_str());
    if (!kIsNdebugBuild)
    {
        std::printf("=================================================================\n");
        std::printf("경고: 이 실행 파일은 Debug 빌드다(NDEBUG 미정의). 측정치를 발표하지 마라.\n");
        std::printf("Artifacts/ecs_vs_oop.csv를 Release와 같은 경로에 조용히 덮어쓴다.\n");
        std::printf("=================================================================\n");
    }
    if (FILE* envFile = std::fopen("Artifacts/ecs_vs_oop_env.txt", "w"))
    {
        std::fprintf(envFile, "%s", envReport.c_str());
        std::fclose(envFile);
    }
    else
    {
        std::printf("환경 파일을 열 수 없다. Artifacts 디렉터리가 있는지 확인하라.\n");
    }

    // 동등성이 깨지면 성능 수치는 의미가 없으므로 먼저 검증한다.
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

    std::printf("\n");

    std::vector<Row> rows;
    for (const std::size_t n : kSweep)
    {
        // 수정 11: 메모리 전용 패스. 시간 스윕과 완전히 분리해서 돈다.
        // 창(backend 생성-충전-파괴) 안에서 alloc/free 짝이 전부 맞아야 하므로, 창이 닫히면
        // g_liveBytes가 정확히 0으로 돌아와야 한다 - 0이 아니면 new/delete 회계 버그다.
        // peak은 Add 도중(전부 살아있는 시점)에만 도달하므로 fragmented 유무와 무관하게
        // N당 한 번만 구해 두 fragmented 행에 동일한 값을 쓴다.
        std::int64_t ecsResidual = 0;
        std::int64_t oopResidual = 0;
        const std::int64_t ecsPeakLiveBytes = MeasurePeakLiveBytes<EcsBackend>(n, ecsResidual);
        const std::int64_t oopPeakLiveBytes = MeasurePeakLiveBytes<OopBackend>(n, oopResidual);
        if (ecsResidual != 0)
            std::printf("  경고: 메모리 패스 회계 버그 - ecs residual=%lld (N=%zu, 0이어야 한다)\n",
                static_cast<long long>(ecsResidual), n);
        if (oopResidual != 0)
            std::printf("  경고: 메모리 패스 회계 버그 - oop residual=%lld (N=%zu, 0이어야 한다)\n",
                static_cast<long long>(oopResidual), n);

        for (const bool fragmented : { false, true })
        {
            double ecsChecksum = 0.0;
            double oopChecksum = 0.0;
            MeasureBackend<EcsBackend>("ecs", n, fragmented, rows, ecsChecksum, ecsPeakLiveBytes);
            MeasureBackend<OopBackend>("oop", n, fragmented, rows, oopChecksum, oopPeakLiveBytes);

            // 수정 8: 마지막 repeat의 최종 상태를 id 오름차순 체크섬으로 비교한다.
            // 이는 Equivalence 검증을 N=100~50000 전 구간, 파편화 유무 양쪽으로 확장하는 효과가 있고,
            // 동시에 StepDecal/StepAnimation의 결과를 실제로 읽어 미래에 /GL이 켜지더라도
            // 데드 스토어 제거로 루프가 사라지지 않게 구조적으로 막는다.
            if (ecsChecksum != oopChecksum)
            {
                std::printf("  [FAIL] checksum 불일치 N=%zu fragmented=%s ecs=%.17g oop=%.17g\n",
                    n, fragmented ? "true" : "false", ecsChecksum, oopChecksum);
                return 1;
            }
        }
        std::printf("N=%zu 완료 (checksum ecs==oop 확인, fragmented 양쪽)\n", n);
    }

    std::printf("\n%-6s %-22s %8s %11s %9s %9s %9s %8s %13s %15s\n",
        "backend", "op", "N", "median(ms)", "sd(ms)", "min(ms)", "max(ms)", "allocs", "allocBytes", "peakLiveBytes");
    for (const Row& r : rows)
    {
        std::printf("%-6s %-22s %8zu %11.4f %9.4f %9.4f %9.4f %8s %13s %15s\n",
            r.backend.c_str(), r.op.c_str(), r.n, r.medianMs, r.stdDevMs, r.minMs, r.maxMs,
            AllocCell(r.hasAllocStats, r.allocCount).c_str(),
            AllocCell(r.hasAllocStats, r.allocBytes).c_str(),
            AllocCellSigned(r.hasAllocStats, r.peakLiveBytes).c_str());
    }

    if (FILE* f = std::fopen("Artifacts/ecs_vs_oop.csv", "w"))
    {
        std::fprintf(f, "backend,op,n,medianMs,stdDevMs,minMs,maxMs,allocCount,allocBytes,peakLiveBytes,workingSetBytes\n");
        for (const Row& r : rows)
        {
            std::fprintf(f, "%s,%s,%zu,%.6f,%.6f,%.6f,%.6f,%s,%s,%s,%llu\n",
                r.backend.c_str(), r.op.c_str(), r.n, r.medianMs, r.stdDevMs, r.minMs, r.maxMs,
                AllocCell(r.hasAllocStats, r.allocCount).c_str(),
                AllocCell(r.hasAllocStats, r.allocBytes).c_str(),
                AllocCellSigned(r.hasAllocStats, r.peakLiveBytes).c_str(),
                static_cast<unsigned long long>(r.workingSet));
        }
        std::fclose(f);
        std::printf("\nCSV: Artifacts/ecs_vs_oop.csv\n");
    }
    else
    {
        std::printf("\nCSV를 열 수 없다. Artifacts 디렉터리가 있는지 확인하라.\n");
        return 1;
    }

    // 자기 확인 (라운드 1): g_liveBytes가 실행 중 한 번이라도 음수로 내려갔다면 new/delete 회계가
    // 깨진 것이다. 라운드 2의 메모리 전용 패스가 매 (N, backend)마다 residual==0을 확인하지만,
    // 그 사이 순간적으로 음수로 내려갔다가 0으로 돌아오는 경우까지는 residual 검사로 못 잡으므로
    // 이 워터마크를 그대로 남겨 둔다.
    std::printf("\n회계 검증: g_liveBytes 최저 관측값 = %lld (%s)\n",
        static_cast<long long>(g_minLiveBytes),
        g_minLiveBytes < 0 ? "오류: 음수로 내려감 - new/delete 회계가 깨졌다" : "정상, 음수로 내려간 적 없음");

    return 0;
}
