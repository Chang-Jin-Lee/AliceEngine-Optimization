#pragma once

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <psapi.h>

namespace Alice::Bench
{
    // 단일 스레드 벤치이므로 평범한 정수를 쓴다. 이유는 AllocCounter.cpp 상단 주석 참고.
    extern std::uint64_t g_allocCount;
    extern std::uint64_t g_allocBytes;
    extern std::int64_t  g_liveBytes;
    extern std::int64_t  g_liveAtReset;
    extern std::int64_t  g_peakLiveBytes;
    extern std::int64_t  g_minLiveBytes; // 진단용. 자기 확인 항목 2 검증에만 쓴다.

    struct AllocStats
    {
        std::uint64_t count = 0;       // 구간 내 누적 할당 횟수
        std::uint64_t bytes = 0;       // 구간 내 누적 요청 바이트 (churn. 실사용량이 아니다)
        std::int64_t  peakLive = 0;    // 구간 내 최고 실점유 바이트 (_msize 기준)
        std::int64_t  liveAtStart = 0; // 구간 시작 시점의 실점유 바이트. peakLive - liveAtStart가 순증가분이다.
    };

    inline void ResetAllocStats() noexcept
    {
        g_allocCount = 0;
        g_allocBytes = 0;
        // 구간 시작 시점의 점유량이 그 구간 peak의 하한이므로, peak을 0이 아니라
        // 현재 점유량으로 초기화한다. g_liveBytes 자체는 절대 리셋하지 않는다 -
        // 프로세스 전체의 실제 점유량을 계속 추적해야 delete 회계가 맞는다.
        g_liveAtReset = g_liveBytes;
        g_peakLiveBytes = g_liveBytes;
    }

    inline AllocStats ReadAllocStats() noexcept
    {
        AllocStats s;
        s.count = g_allocCount;
        s.bytes = g_allocBytes;
        s.peakLive = g_peakLiveBytes;
        s.liveAtStart = g_liveAtReset;
        return s;
    }

    /// QueryPerformanceCounter 기반 경과 시간.
    class ScopedTimer
    {
    public:
        ScopedTimer() noexcept
        {
            QueryPerformanceFrequency(&m_frequency);
            QueryPerformanceCounter(&m_start);
        }

        double ElapsedMs() const noexcept
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double ticks = static_cast<double>(now.QuadPart - m_start.QuadPart);
            return ticks / static_cast<double>(m_frequency.QuadPart) * 1000.0;
        }

    private:
        LARGE_INTEGER m_frequency{};
        LARGE_INTEGER m_start{};
    };

    inline std::uint64_t WorkingSetBytes() noexcept
    {
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) == 0)
            return 0;
        return static_cast<std::uint64_t>(pmc.WorkingSetSize);
    }
}
