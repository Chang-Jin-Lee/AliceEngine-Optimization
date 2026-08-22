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
    extern std::int64_t  g_peakLiveBytes;
    extern std::int64_t  g_minLiveBytes;    // 진단용 워터마크.
    extern bool          g_trackLiveBytes;  // 메모리 전용 패스에서만 true. 시간 패스는 항상 false.

    // 시간 패스에서 재는 값. count/bytes는 평범한 정수 증가라 비용이 작아 시간 패스에서도 켜 둔다.
    // live/peak 바이트는 여기 없다 - 그건 메모리 전용 패스(main.cpp의 MeasurePeakLiveBytes)가 잰다.
    struct AllocStats
    {
        std::uint64_t count = 0; // 구간 내 누적 할당 횟수
        std::uint64_t bytes = 0; // 구간 내 누적 요청 바이트 (churn. 실사용량이 아니다)
    };

    inline void ResetAllocStats() noexcept
    {
        g_allocCount = 0;
        g_allocBytes = 0;
    }

    inline AllocStats ReadAllocStats() noexcept
    {
        AllocStats s;
        s.count = g_allocCount;
        s.bytes = g_allocBytes;
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
