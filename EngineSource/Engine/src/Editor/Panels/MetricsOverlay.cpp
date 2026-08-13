#include "Editor/Panels/MetricsOverlay.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Alice
{
#if defined(ALICE_METRICS_TESTING)
    namespace
    {
        std::uint64_t MeasureRenderNanoseconds(MetricsOverlay& overlay)
        {
            const auto begin = std::chrono::steady_clock::now();
            overlay.Render(MetricsOverlayRuntimeState{ false, true, 1920, 1080 });
            const auto end = std::chrono::steady_clock::now();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
        }
    }
#endif

    void MetricsHistory::Push(float value) noexcept
    {
        m_values[m_next] = value;
        m_next = (m_next + 1) % kCapacity;
        if (m_count < kCapacity)
            ++m_count;
    }

    float MetricsHistory::ValueAtOldest(std::size_t index) const noexcept
    {
        if (index >= m_count)
            return 0.0f;

        const std::size_t oldest = m_count == kCapacity ? m_next : 0;
        return m_values[(oldest + index) % kCapacity];
    }

    MetricsSummary MetricsHistory::CalculateSummary() const noexcept
    {
        MetricsSummary summary{};
        if (m_count == 0)
            return summary;

        std::array<float, kCapacity> sorted{};
        double sum = 0.0;
        for (std::size_t index = 0; index < m_count; ++index)
        {
            sorted[index] = ValueAtOldest(index);
            sum += sorted[index];
        }

        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(m_count));
        summary.minimum = sorted[0];
        summary.average = sum / static_cast<double>(m_count);
        summary.maximum = sorted[m_count - 1];

        const std::size_t slowCount = (std::max)(
            std::size_t{ 1 }, static_cast<std::size_t>(std::ceil(m_count * 0.01)));
        double slowSum = 0.0;
        for (std::size_t index = m_count - slowCount; index < m_count; ++index)
            slowSum += sorted[index];
        summary.onePercentLow = slowSum / static_cast<double>(slowCount);
        return summary;
    }

    void MetricsOverlay::Update(
        const RenderStatsSnapshot& stats, const GpuProfiler& profiler) noexcept
    {
        bool hasPresentSample = m_lastPresentMilliseconds > 0.0f;
        if (stats.frameSerial != 0 && stats.frameSerial != m_lastStatsFrameSerial)
        {
            m_lastStatsFrameSerial = stats.frameSerial;
            m_stats = stats;
            if (stats.gpuScopesValid)
                m_scopeMilliseconds = stats.gpuScopeMilliseconds;
            else
                m_scopeMilliseconds.fill(0.0);
            if (stats.presentMs > 0.0)
            {
                m_lastPresentMilliseconds = static_cast<float>(stats.presentMs);
                hasPresentSample = true;
            }
        }

        // Update is called exactly once per rendered frame. Query results may arrive
        // several frames late, so advance the graph with the last valid value while
        // retaining a real 240-render-frame time axis.
        if (hasPresentSample)
            m_history.Push(m_lastPresentMilliseconds);

        m_lastDiscardedFrameCount = profiler.DiscardedFrameCount();
    }

    void MetricsOverlay::Render(const MetricsOverlayRuntimeState& runtimeState)
    {
        if (!m_visible || ImGui::GetCurrentContext() == nullptr)
            return;

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(
                mainViewport->WorkPos.x + mainViewport->WorkSize.x - 12.0f,
                mainViewport->WorkPos.y + 12.0f),
            ImGuiCond_Always,
            ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("##AliceMetricsOverlay", nullptr, flags))
        {
            ImGui::End();
            return;
        }
        const double presentMs = m_stats.presentMs;
        const double fps = presentMs > 0.0 ? 1000.0 / presentMs : 0.0;
        ImGui::SetWindowFontScale(1.6f);
        ImGui::Text("frame %6.2f ms  (%3.0f fps)", presentMs, fps);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Text(
            "CPU %6.2f ms   GPU %6.2f ms",
            m_stats.cpuFrameMs,
            m_scopeMilliseconds[static_cast<std::size_t>(GpuScope::Frame)]);

        ImGui::PlotLines(
            "##FrameTimeHistory",
            m_history.Data(),
            static_cast<int>(m_history.Count()),
            static_cast<int>(m_history.PlotOffset()),
            nullptr,
            0.0f,
            40.0f,
            ImVec2(-1.0f, 60.0f));

        const MetricsSummary summary = m_history.CalculateSummary();
        ImGui::Text(
            "min %.2f  avg %.2f  max %.2f  1%% low %.2f",
            summary.minimum,
            summary.average,
            summary.maximum,
            summary.onePercentLow);

        static constexpr const char* scopeNames[] = {
            "MainPass",
            "CameraPreview",
            "ComputeEffects",
            "ParticleOverlay",
            "DebugOverlay",
            "ToneMapAndUI",
            "OverlayEffects",
            "EditorDraw"
        };
        if (ImGui::BeginTable("##GpuPasses", 2, ImGuiTableFlags_SizingStretchProp))
        {
            for (std::size_t index = 1; index < GpuProfiler::kScopeCount; ++index)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(scopeNames[index - 1]);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%6.2f ms", m_scopeMilliseconds[index]);
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text(
            "draw %llu  instanced %llu",
            static_cast<unsigned long long>(m_stats.drawCalls),
            static_cast<unsigned long long>(m_stats.instancedDrawCalls));
        ImGui::Text(
            "IA %llu  PS %llu",
            static_cast<unsigned long long>(m_stats.iaPrimitives),
            static_cast<unsigned long long>(m_stats.psInvocations));
        ImGui::Text(
            "bone maps %llu  upload %.0f MB",
            static_cast<unsigned long long>(m_stats.boneCbMapCount),
            static_cast<double>(m_stats.boneCbBytesUploaded) / (1024.0 * 1024.0));
        ImGui::Text(
            "VRAM %.0f / %.0f MB  working set %.0f MB",
            m_stats.vramUsedMB,
            m_stats.vramBudgetMB,
            m_stats.workingSetMB);
        ImGui::Text(
            "[%s]  vsync %s  %ux%u  discarded %llu",
            runtimeState.legacyEnabled ? "LEGACY ON" : "CURRENT",
            runtimeState.vsyncEnabled ? "on" : "off",
            runtimeState.width,
            runtimeState.height,
            static_cast<unsigned long long>(m_lastDiscardedFrameCount));

        ImGui::End();
    }

#if defined(ALICE_METRICS_TESTING)
    std::uint64_t MetricsOverlay::MeasureAverageRenderNanosecondsForTesting(
        std::size_t iterations)
    {
        if (iterations == 0 || ImGui::GetCurrentContext() != nullptr)
            return 0;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();

        MetricsOverlay overlay;
        for (std::size_t index = 0; index < MetricsHistory::kCapacity; ++index)
            overlay.m_history.Push(16.0f + static_cast<float>(index % 5));

        for (std::size_t index = 0; index < 32; ++index)
        {
            ImGui::NewFrame();
            overlay.Render(MetricsOverlayRuntimeState{ false, true, 1920, 1080 });
            ImGui::Render();
        }

        std::uint64_t elapsed = 0;
        for (std::size_t index = 0; index < iterations; ++index)
        {
            ImGui::NewFrame();
            elapsed += MeasureRenderNanoseconds(overlay);
            ImGui::Render();
        }

        ImGui::DestroyContext();
        return elapsed / iterations;
    }
#endif
}
