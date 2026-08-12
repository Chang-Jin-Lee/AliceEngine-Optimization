#include "Runtime/Rendering/Metrics/RenderStats.h"

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/Metrics/GpuProfiler.h"

#include <Windows.h>
#include <Psapi.h>

namespace Alice
{
    namespace
    {
        constexpr double kBytesPerMiB = 1024.0 * 1024.0;
        constexpr UINT kNoFlush = D3D11_ASYNC_GETDATA_DONOTFLUSH;
    }

    RenderStats::~RenderStats()
    {
        Shutdown();
    }

    bool RenderStats::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        Shutdown();
        if (!device || !context)
            return false;

        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
        for (FrameSlot& frame : m_frames)
        {
            if (FAILED(device->CreateQuery(&desc, frame.pipelineQuery.ReleaseAndGetAddressOf())))
            {
                Shutdown();
                return false;
            }
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.ReleaseAndGetAddressOf()))))
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf())))
                adapter.As(&m_adapter);
        }

        m_context = context;
        EnsureQpcFrequency();
        m_initialized = true;
        return true;
    }

    void RenderStats::Shutdown() noexcept
    {
        m_recordingFrame = nullptr;
        m_context.Reset();
        m_adapter.Reset();
        for (FrameSlot& frame : m_frames)
            frame = {};
        m_latest = {};
        m_nextSlot = 0;
        m_qpcFrequency = 0;
        m_initialized = false;
        m_enabled = false;
    }

    bool RenderStats::BeginFrame(std::uint64_t frameSerial, double presentMs)
    {
        if (!m_enabled || m_recordingFrame)
            return false;

        FrameSlot* frame = FindFreeSlot();
        if (!frame)
            return false;

        frame->snapshot = {};
        frame->snapshot.frameSerial = frameSerial;
        frame->snapshot.presentMs = presentMs;
        frame->queryIssued = false;
        frame->gpuValidated = false;
        frame->pending = false;

        EnsureQpcFrequency();
        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);
        frame->cpuStartCounter = start.QuadPart;

        if (m_initialized && m_context && frame->pipelineQuery)
        {
            m_context->Begin(frame->pipelineQuery.Get());
            frame->queryIssued = true;
        }

        m_recordingFrame = frame;
        return true;
    }

    void RenderStats::EndFrame()
    {
        if (!m_recordingFrame)
            return;

        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        if (m_qpcFrequency > 0 && end.QuadPart >= m_recordingFrame->cpuStartCounter)
        {
            m_recordingFrame->snapshot.cpuFrameMs =
                static_cast<double>(end.QuadPart - m_recordingFrame->cpuStartCounter) /
                static_cast<double>(m_qpcFrequency) * 1000.0;
        }

        CaptureMemory(m_recordingFrame->snapshot);

        const std::size_t completedIndex =
            static_cast<std::size_t>(m_recordingFrame - m_frames.data());
        m_nextSlot = (completedIndex + 1) % kBufferedFrames;

        if (m_recordingFrame->queryIssued && m_context)
        {
            m_context->End(m_recordingFrame->pipelineQuery.Get());
            m_recordingFrame->pending = true;
        }
        else
        {
            m_recordingFrame->snapshot.pipelineStatsValid = false;
            m_latest = m_recordingFrame->snapshot;
            m_recordingFrame->pending = false;
        }

        m_recordingFrame = nullptr;
    }

    void RenderStats::Resolve(const GpuProfiler& gpuProfiler)
    {
        if (!m_enabled || !m_context)
            return;

        LatchGpuOutcomes(gpuProfiler);

        FrameSlot* frame = FindOldestPendingSlot();
        if (!frame || !frame->gpuValidated)
            return;

        D3D11_QUERY_DATA_PIPELINE_STATISTICS pipeline{};
        const HRESULT result = m_context->GetData(
            frame->pipelineQuery.Get(), &pipeline, sizeof(pipeline), kNoFlush);
        if (result == S_FALSE)
            return;
        if (FAILED(result))
        {
            ALICE_LOG_WARN("RenderStats: pipeline statistics query failed for frame %llu (HRESULT=0x%08X).",
                static_cast<unsigned long long>(frame->snapshot.frameSerial),
                static_cast<unsigned>(result));
            frame->pending = false;
            return;
        }

        frame->snapshot.iaPrimitives = pipeline.IAPrimitives;
        frame->snapshot.vsInvocations = pipeline.VSInvocations;
        frame->snapshot.psInvocations = pipeline.PSInvocations;
        frame->snapshot.cPrimitives = pipeline.CPrimitives;
        frame->snapshot.pipelineStatsValid = true;
        m_latest = frame->snapshot;
        frame->pending = false;
    }

    void RenderStats::RecordDraw(bool instanced) noexcept
    {
        if (m_enabled && m_recordingFrame)
            m_recordingFrame->snapshot.RecordDraw(instanced);
    }

    void RenderStats::RecordBoneCbUpload(std::uint64_t bytes) noexcept
    {
        if (m_enabled && m_recordingFrame)
            m_recordingFrame->snapshot.RecordBoneCbUpload(bytes);
    }

    void RenderStats::Draw(
        ID3D11DeviceContext* context, UINT vertexCount, UINT startVertexLocation)
    {
        if (!context)
            return;
        RecordDraw(false);
        context->Draw(vertexCount, startVertexLocation);
    }

    void RenderStats::DrawIndexed(
        ID3D11DeviceContext* context,
        UINT indexCount,
        UINT startIndexLocation,
        INT baseVertexLocation)
    {
        if (!context)
            return;
        RecordDraw(false);
        context->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
    }

    void RenderStats::DrawInstanced(
        ID3D11DeviceContext* context,
        UINT vertexCountPerInstance,
        UINT instanceCount,
        UINT startVertexLocation,
        UINT startInstanceLocation)
    {
        if (!context)
            return;
        RecordDraw(true);
        context->DrawInstanced(
            vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
    }

    void RenderStats::DrawIndexedInstanced(
        ID3D11DeviceContext* context,
        UINT indexCountPerInstance,
        UINT instanceCount,
        UINT startIndexLocation,
        INT baseVertexLocation,
        UINT startInstanceLocation)
    {
        if (!context)
            return;
        RecordDraw(true);
        context->DrawIndexedInstanced(
            indexCountPerInstance,
            instanceCount,
            startIndexLocation,
            baseVertexLocation,
            startInstanceLocation);
    }

    RenderStats::FrameSlot* RenderStats::FindFreeSlot() noexcept
    {
        for (std::size_t offset = 0; offset < kBufferedFrames; ++offset)
        {
            const std::size_t index = (m_nextSlot + offset) % kBufferedFrames;
            if (!m_frames[index].pending)
                return &m_frames[index];
        }
        return nullptr;
    }

    RenderStats::FrameSlot* RenderStats::FindOldestPendingSlot() noexcept
    {
        FrameSlot* oldest = nullptr;
        for (FrameSlot& frame : m_frames)
        {
            if (!frame.pending)
                continue;
            if (!oldest || frame.snapshot.frameSerial < oldest->snapshot.frameSerial)
                oldest = &frame;
        }
        return oldest;
    }

    void RenderStats::LatchGpuOutcomes(const GpuProfiler& gpuProfiler) noexcept
    {
        for (FrameSlot& frame : m_frames)
        {
            if (!frame.pending || frame.gpuValidated)
                continue;

            const GpuFrameOutcome outcome =
                gpuProfiler.FrameOutcome(frame.snapshot.frameSerial);
            if (outcome == GpuFrameOutcome::Valid)
                frame.gpuValidated = true;
            else if (outcome == GpuFrameOutcome::Discarded)
                frame.pending = false;
        }
    }

#if defined(ALICE_METRICS_TESTING)
    bool RenderStats::SeedPendingFrameForTesting(std::uint64_t frameSerial) noexcept
    {
        FrameSlot* frame = FindFreeSlot();
        if (!frame)
            return false;

        *frame = {};
        frame->snapshot.frameSerial = frameSerial;
        frame->pending = true;
        const std::size_t index = static_cast<std::size_t>(frame - m_frames.data());
        m_nextSlot = (index + 1) % kBufferedFrames;
        return true;
    }

    void RenderStats::LatchGpuOutcomesForTesting(
        const GpuProfiler& gpuProfiler) noexcept
    {
        LatchGpuOutcomes(gpuProfiler);
    }

    bool RenderStats::PendingForTesting(std::uint64_t frameSerial) const noexcept
    {
        for (const FrameSlot& frame : m_frames)
        {
            if (frame.snapshot.frameSerial == frameSerial)
                return frame.pending;
        }
        return false;
    }

    bool RenderStats::GpuValidatedForTesting(std::uint64_t frameSerial) const noexcept
    {
        for (const FrameSlot& frame : m_frames)
        {
            if (frame.snapshot.frameSerial == frameSerial)
                return frame.gpuValidated;
        }
        return false;
    }
#endif

    void RenderStats::CaptureMemory(RenderStatsSnapshot& snapshot) const noexcept
    {
        if (!m_initialized)
            return;

        if (m_adapter)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
            {
                snapshot.vramUsedMB = static_cast<double>(info.CurrentUsage) / kBytesPerMiB;
                snapshot.vramBudgetMB = static_cast<double>(info.Budget) / kBytesPerMiB;
            }
        }

        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)))
        {
            snapshot.workingSetMB = static_cast<double>(counters.WorkingSetSize) / kBytesPerMiB;
        }
    }

    void RenderStats::EnsureQpcFrequency() noexcept
    {
        if (m_qpcFrequency > 0)
            return;

        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency))
            m_qpcFrequency = frequency.QuadPart;
    }
}
