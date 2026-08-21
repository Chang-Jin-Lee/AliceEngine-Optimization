#include "EcsVsOop/EcsBackend.h"
#include "EcsVsOop/Equivalence.h"
#include "EcsVsOop/Measure.h"
#include "EcsVsOop/OopBackend.h"
#include "EcsVsOop/Stats.h"
#include "EcsVsOop/Workload.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace
{
    using namespace Alice::Bench;
    using Alice::EntityId;
    using Alice::BenchStats::Median;
    using Alice::BenchStats::StdDev;

    constexpr int kRepeat = 11;      // 앞 2회는 워밍업으로 버린다
    constexpr int kWarmup = 2;
    constexpr int kStepsPerRun = 10; // 순회 1회 측정에 포함되는 Step 횟수

    const std::size_t kSweep[] = { 100, 500, 1000, 5000, 10000, 50000 };

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

    struct Row
    {
        std::string backend;
        std::string op;
        std::size_t n = 0;
        double medianMs = 0.0;
        double stdDevMs = 0.0;
        std::uint64_t allocCount = 0;
        std::uint64_t allocBytes = 0;
        std::uint64_t workingSet = 0;
    };

    /// 백엔드 하나를 N개 규모로 측정한다.
    /// fragmented=true면 30%를 삭제한 뒤 재삽입해 힙을 파편화시킨 상태에서 순회를 잰다.
    template <typename Backend>
    void MeasureBackend(const char* name, std::size_t n, bool fragmented, std::vector<Row>& out)
    {
        std::vector<double> addSamples;
        std::vector<double> stepSamples;
        std::vector<double> removeSamples;
        std::vector<double> randomSamples;

        std::uint64_t addAllocCount = 0;
        std::uint64_t addAllocBytes = 0;
        std::uint64_t peakWorkingSet = 0;

        const std::vector<EntityId> order = ShuffledIds(n, 12345u);

        for (int r = 0; r < kRepeat; ++r)
        {
            Backend backend;

            // --- 추가 ---
            // ReadAllocStats()는 push_back보다 먼저 호출해야 한다. addSamples.push_back()
            // 자체가 힙 할당을 일으킬 수 있어서, 순서를 바꾸면 그 할당이 백엔드의 할당 수에
            // 섞여 들어간다. N=100 근처에서는 ECS 쪽 총 할당이 수십 건뿐이라 단 1건의 오차도
            // 결과를 몇 퍼센트씩 흔든다.
            ResetAllocStats();
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
                }
            }

            // --- 파편화 (요청 시) ---
            if (fragmented)
            {
                const std::size_t cut = n * 3 / 10;
                for (std::size_t i = 0; i < cut; ++i)
                    backend.Remove(order[i]);
                for (std::size_t i = 0; i < cut; ++i)
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

            const std::uint64_t ws = WorkingSetBytes();
            if (ws > peakWorkingSet)
                peakWorkingSet = ws;

            // --- 랜덤 접근 ---
            {
                std::uint64_t sink = 0;
                ScopedTimer timer;
                for (int k = 0; k < 100000; ++k)
                {
                    const EntityId id = order[static_cast<std::size_t>(k) % n];
                    const BenchTransform* t = backend.GetTransform(id);
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

        const char* suffix = fragmented ? " (fragmented)" : "";
        out.push_back(Row{ name, std::string("add") + suffix, n,
            Median(addSamples), StdDev(addSamples), addAllocCount, addAllocBytes, peakWorkingSet });
        out.push_back(Row{ name, std::string("step") + suffix, n,
            Median(stepSamples), StdDev(stepSamples), 0, 0, peakWorkingSet });
        out.push_back(Row{ name, std::string("random") + suffix, n,
            Median(randomSamples), StdDev(randomSamples), 0, 0, peakWorkingSet });
        out.push_back(Row{ name, std::string("remove") + suffix, n,
            Median(removeSamples), StdDev(removeSamples), 0, 0, peakWorkingSet });
    }
}

int main()
{
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
        for (const bool fragmented : { false, true })
        {
            MeasureBackend<EcsBackend>("ecs", n, fragmented, rows);
            MeasureBackend<OopBackend>("oop", n, fragmented, rows);
        }
        std::printf("N=%zu 완료\n", n);
    }

    std::printf("\n%-6s %-22s %8s %12s %10s %12s %14s\n",
        "backend", "op", "N", "median(ms)", "sd(ms)", "allocs", "allocBytes");
    for (const Row& r : rows)
    {
        std::printf("%-6s %-22s %8zu %12.4f %10.4f %12llu %14llu\n",
            r.backend.c_str(), r.op.c_str(), r.n, r.medianMs, r.stdDevMs,
            static_cast<unsigned long long>(r.allocCount),
            static_cast<unsigned long long>(r.allocBytes));
    }

    if (FILE* f = std::fopen("Artifacts/ecs_vs_oop.csv", "w"))
    {
        std::fprintf(f, "backend,op,n,medianMs,stdDevMs,allocCount,allocBytes,workingSetBytes\n");
        for (const Row& r : rows)
        {
            std::fprintf(f, "%s,%s,%zu,%.6f,%.6f,%llu,%llu,%llu\n",
                r.backend.c_str(), r.op.c_str(), r.n, r.medianMs, r.stdDevMs,
                static_cast<unsigned long long>(r.allocCount),
                static_cast<unsigned long long>(r.allocBytes),
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

    return 0;
}
