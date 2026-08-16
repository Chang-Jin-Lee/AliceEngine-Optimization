#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Runtime/Rendering/Metrics/GpuProfiler.h"

namespace Alice
{
    struct RenderFrameCounters
    {
        std::uint64_t drawCalls = 0;
        std::uint64_t instancedDrawCalls = 0;
        std::uint64_t boneCbMapCount = 0;
        std::uint64_t boneCbBytesUploaded = 0;

        void Reset() noexcept
        {
            *this = {};
        }

        void RecordDraw(bool instanced) noexcept
        {
            ++drawCalls;
            if (instanced)
                ++instancedDrawCalls;
        }

        void RecordBoneCbUpload(std::uint64_t bytes) noexcept
        {
            ++boneCbMapCount;
            boneCbBytesUploaded += bytes;
        }
    };

    struct RenderStatsSnapshot : RenderFrameCounters
    {
        std::uint64_t frameSerial = 0;
        std::uint64_t iaPrimitives = 0;
        std::uint64_t vsInvocations = 0;
        std::uint64_t psInvocations = 0;
        std::uint64_t cPrimitives = 0;
        double cpuFrameMs = 0.0;
        double presentMs = 0.0;
        double vramUsedMB = 0.0;
        double vramBudgetMB = 0.0;
        double workingSetMB = 0.0;
        std::array<double, GpuProfiler::kScopeCount> gpuScopeMilliseconds{};
        bool pipelineStatsValid = false;
        bool gpuScopesValid = false;
    };

    class RenderStats
    {
    public:
        static constexpr std::size_t kBufferedFrames = 4;

        RenderStats() = default;
        ~RenderStats();

        RenderStats(const RenderStats&) = delete;
        RenderStats& operator=(const RenderStats&) = delete;

        bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown() noexcept;

        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
        bool IsEnabled() const noexcept { return m_enabled; }

        bool BeginFrame(std::uint64_t frameSerial, double presentMs);
        void EndFrame();
        void Resolve(const GpuProfiler& gpuProfiler);

        const RenderStatsSnapshot& Latest() const noexcept { return m_latest; }

        void RecordDraw(bool instanced) noexcept;
        void RecordBoneCbUpload(std::uint64_t bytes) noexcept;

        void Draw(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertexLocation);
        void DrawIndexed(
            ID3D11DeviceContext* context,
            UINT indexCount,
            UINT startIndexLocation,
            INT baseVertexLocation);
        void DrawInstanced(
            ID3D11DeviceContext* context,
            UINT vertexCountPerInstance,
            UINT instanceCount,
            UINT startVertexLocation,
            UINT startInstanceLocation);
        void DrawIndexedInstanced(
            ID3D11DeviceContext* context,
            UINT indexCountPerInstance,
            UINT instanceCount,
            UINT startIndexLocation,
            INT baseVertexLocation,
            UINT startInstanceLocation);

#if defined(ALICE_METRICS_TESTING)
        bool SeedPendingFrameForTesting(std::uint64_t frameSerial) noexcept;
        void LatchGpuOutcomesForTesting(const GpuProfiler& gpuProfiler) noexcept;
        bool PendingForTesting(std::uint64_t frameSerial) const noexcept;
        bool GpuValidatedForTesting(std::uint64_t frameSerial) const noexcept;
        bool GpuScopesValidForTesting(std::uint64_t frameSerial) const noexcept;
        double GpuScopeMsForTesting(
            std::uint64_t frameSerial, GpuScope scope) const noexcept;
#endif

    private:
        struct FrameSlot
        {
            Microsoft::WRL::ComPtr<ID3D11Query> pipelineQuery;
            RenderStatsSnapshot snapshot{};
            std::int64_t cpuStartCounter = 0;
            bool pending = false;
            bool queryIssued = false;
            bool gpuValidated = false;
        };

        FrameSlot* FindFreeSlot() noexcept;
        FrameSlot* FindOldestPendingSlot() noexcept;
        void LatchGpuOutcomes(const GpuProfiler& gpuProfiler) noexcept;
        void CaptureMemory(RenderStatsSnapshot& snapshot) const noexcept;
        void EnsureQpcFrequency() noexcept;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> m_adapter;
        std::array<FrameSlot, kBufferedFrames> m_frames{};
        FrameSlot* m_recordingFrame = nullptr;
        RenderStatsSnapshot m_latest{};
        std::size_t m_nextSlot = 0;
        std::int64_t m_qpcFrequency = 0;
        bool m_initialized = false;
        bool m_enabled = false;
    };
}
