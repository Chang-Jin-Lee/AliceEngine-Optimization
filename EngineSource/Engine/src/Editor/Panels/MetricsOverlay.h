#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Runtime/Rendering/Metrics/GpuProfiler.h"
#include "Runtime/Rendering/Metrics/RenderStats.h"

namespace Alice
{
    struct MetricsSummary
    {
        double minimum = 0.0;
        double average = 0.0;
        double maximum = 0.0;
        double onePercentLow = 0.0;
    };

    class MetricsHistory
    {
    public:
        static constexpr std::size_t kCapacity = 240;

        void Push(float value) noexcept;
        std::size_t Count() const noexcept { return m_count; }
        std::size_t PlotOffset() const noexcept { return m_count == kCapacity ? m_next : 0; }
        const float* Data() const noexcept { return m_values.data(); }
        float ValueAtOldest(std::size_t index) const noexcept;
        MetricsSummary CalculateSummary() const noexcept;

    private:
        std::array<float, kCapacity> m_values{};
        std::size_t m_count = 0;
        std::size_t m_next = 0;
    };

    struct MetricsOverlayRuntimeState
    {
        bool legacyEnabled = false;
        bool vsyncEnabled = true;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    class MetricsOverlay
    {
    public:
        void SetVisible(bool visible) noexcept { m_visible = visible; }
        void ToggleVisible() noexcept { m_visible = !m_visible; }
        bool IsVisible() const noexcept { return m_visible; }

        void Update(const RenderStatsSnapshot& stats, const GpuProfiler& profiler) noexcept;
        void Render(const MetricsOverlayRuntimeState& runtimeState);

        const MetricsHistory& History() const noexcept { return m_history; }

#if defined(ALICE_METRICS_TESTING)
        static std::uint64_t MeasureAverageRenderNanosecondsForTesting(std::size_t iterations);
        double ScopeMsForTesting(GpuScope scope) const noexcept
        {
            const std::size_t index = static_cast<std::size_t>(scope);
            return index < m_scopeMilliseconds.size() ? m_scopeMilliseconds[index] : 0.0;
        }
#endif

    private:
        RenderStatsSnapshot m_stats{};
        std::array<double, GpuProfiler::kScopeCount> m_scopeMilliseconds{};
        MetricsHistory m_history;
        std::uint64_t m_lastStatsFrameSerial = 0;
        std::uint64_t m_lastDiscardedFrameCount = 0;
        float m_lastPresentMilliseconds = 0.0f;
        bool m_visible = true;
    };
}
