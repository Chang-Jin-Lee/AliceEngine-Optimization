#pragma once

#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <psapi.h>

namespace Alice::Bench
{
    extern std::atomic<std::uint64_t> g_allocCount;
    extern std::atomic<std::uint64_t> g_allocBytes;

    struct AllocStats
    {
        std::uint64_t count = 0;
        std::uint64_t bytes = 0;
    };

    inline void ResetAllocStats() noexcept
    {
        g_allocCount.store(0, std::memory_order_relaxed);
        g_allocBytes.store(0, std::memory_order_relaxed);
    }

    inline AllocStats ReadAllocStats() noexcept
    {
        AllocStats s;
        s.count = g_allocCount.load(std::memory_order_relaxed);
        s.bytes = g_allocBytes.load(std::memory_order_relaxed);
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
