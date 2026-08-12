#include "Runtime/Rendering/Metrics/GpuProfiler.h"

#include "Runtime/Foundation/Logger.h"

#include <limits>

namespace Alice::MetricsDetail
{
    bool TryTimestampMilliseconds(
        std::uint64_t begin,
        std::uint64_t end,
        std::uint64_t frequency,
        bool disjoint,
        double& outMs) noexcept
    {
        if (disjoint || frequency == 0 || end < begin)
            return false;

        outMs = static_cast<double>(end - begin) /
            static_cast<double>(frequency) * 1000.0;
        return true;
    }
}

namespace Alice
{
    namespace
    {
        constexpr UINT kNoFlush = D3D11_ASYNC_GETDATA_DONOTFLUSH;

        bool CreateQuery(ID3D11Device* device, D3D11_QUERY type, ID3D11Query** output)
        {
            D3D11_QUERY_DESC desc{};
            desc.Query = type;
            return SUCCEEDED(device->CreateQuery(&desc, output));
        }
    }

    GpuProfiler::~GpuProfiler()
    {
        Shutdown();
    }

    bool GpuProfiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        Shutdown();
        if (!device || !context)
            return false;

        for (FrameQueries& frame : m_frames)
        {
            if (!CreateQuery(device, D3D11_QUERY_TIMESTAMP_DISJOINT,
                    frame.disjoint.ReleaseAndGetAddressOf()))
            {
                Shutdown();
                return false;
            }

            for (TimestampQueries& pair : frame.timestamps)
            {
                if (!CreateQuery(device, D3D11_QUERY_TIMESTAMP, pair.begin.ReleaseAndGetAddressOf()) ||
                    !CreateQuery(device, D3D11_QUERY_TIMESTAMP, pair.end.ReleaseAndGetAddressOf()))
                {
                    Shutdown();
                    return false;
                }
            }
        }

        m_context = context;
        m_initialized = true;
        return true;
    }

    void GpuProfiler::Shutdown() noexcept
    {
        m_recordingFrame = nullptr;
        m_context.Reset();
        for (FrameQueries& frame : m_frames)
            frame = {};
        for (OutcomeRecord& outcome : m_outcomes)
            outcome = {};
        m_scopeMilliseconds.fill(0.0);
        m_nextSlot = 0;
        m_initialized = false;
        m_enabled = false;
        m_lastFrameDisjoint = false;
        m_resolvedFrameSerial = 0;
        m_discardedFrameCount = 0;
    }

    void GpuProfiler::SetEnabled(bool enabled) noexcept
    {
        m_enabled = m_initialized && enabled;
    }

    bool GpuProfiler::IsEnabled() const noexcept
    {
        return m_enabled;
    }

    bool GpuProfiler::BeginFrame(std::uint64_t frameSerial)
    {
        if (!m_enabled || !m_context || m_recordingFrame)
            return false;

        FrameQueries* frame = FindFreeSlot();
        if (!frame)
            return false;

        frame->frameSerial = frameSerial;
        frame->used.fill(false);
        frame->active.fill(false);
        frame->pending = false;

        const std::size_t frameIndex = ScopeIndex(GpuScope::Frame);
        m_context->Begin(frame->disjoint.Get());
        m_context->End(frame->timestamps[frameIndex].begin.Get());
        frame->used[frameIndex] = true;
        frame->active[frameIndex] = true;
        m_recordingFrame = frame;
        PublishOutcome(frameSerial, GpuFrameOutcome::Pending);
        return true;
    }

    bool GpuProfiler::BeginScope(GpuScope scope)
    {
        if (!m_recordingFrame || !IsUserScope(scope))
            return false;

        if (!ReserveScope(scope, true))
            return false;

        m_context->End(m_recordingFrame->timestamps[ScopeIndex(scope)].begin.Get());
        return true;
    }

    bool GpuProfiler::ReserveScope(GpuScope scope, bool logWarning)
    {
        if (!m_recordingFrame || !IsUserScope(scope))
            return false;

        const std::size_t index = ScopeIndex(scope);
        if (m_recordingFrame->used[index])
        {
            if (logWarning)
            {
                ALICE_LOG_WARN("GpuProfiler: scope %u was opened more than once in frame %llu.",
                    static_cast<unsigned>(scope),
                    static_cast<unsigned long long>(m_recordingFrame->frameSerial));
            }
            return false;
        }

        for (std::size_t activeIndex = 1; activeIndex < kScopeCount; ++activeIndex)
        {
            if (!m_recordingFrame->active[activeIndex])
                continue;

            if (logWarning)
            {
                ALICE_LOG_WARN(
                    "GpuProfiler: nested scope %u was ignored while scope %u is active in frame %llu.",
                    static_cast<unsigned>(scope),
                    static_cast<unsigned>(activeIndex),
                    static_cast<unsigned long long>(m_recordingFrame->frameSerial));
            }
            return false;
        }

        m_recordingFrame->used[index] = true;
        m_recordingFrame->active[index] = true;
        return true;
    }

    void GpuProfiler::EndScope(GpuScope scope)
    {
        if (!m_recordingFrame || !IsUserScope(scope))
            return;

        const std::size_t index = ScopeIndex(scope);
        if (!m_recordingFrame->active[index])
            return;

        m_context->End(m_recordingFrame->timestamps[index].end.Get());
        m_recordingFrame->active[index] = false;
    }

    void GpuProfiler::EndFrame()
    {
        if (!m_recordingFrame || !m_context)
            return;

        for (std::size_t index = 1; index < kScopeCount; ++index)
        {
            if (!m_recordingFrame->active[index])
                continue;

            ALICE_LOG_WARN("GpuProfiler: scope %u was left open in frame %llu; closing it at frame end.",
                static_cast<unsigned>(index),
                static_cast<unsigned long long>(m_recordingFrame->frameSerial));
            m_context->End(m_recordingFrame->timestamps[index].end.Get());
            m_recordingFrame->active[index] = false;
        }

        const std::size_t frameIndex = ScopeIndex(GpuScope::Frame);
        m_context->End(m_recordingFrame->timestamps[frameIndex].end.Get());
        m_recordingFrame->active[frameIndex] = false;
        m_context->End(m_recordingFrame->disjoint.Get());
        m_recordingFrame->pending = true;

        const std::size_t completedIndex = static_cast<std::size_t>(m_recordingFrame - m_frames.data());
        m_nextSlot = (completedIndex + 1) % kBufferedFrames;
        m_recordingFrame = nullptr;
    }

    void GpuProfiler::Resolve()
    {
        if (!m_enabled || !m_context)
            return;

        FrameQueries* frame = FindOldestPendingSlot();
        if (!frame)
            return;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
        HRESULT result = m_context->GetData(
            frame->disjoint.Get(), &disjointData, sizeof(disjointData), kNoFlush);
        if (result == S_FALSE)
            return;
        if (FAILED(result))
        {
            DiscardSlot(*frame, false);
            return;
        }

        if (disjointData.Disjoint || disjointData.Frequency == 0)
        {
            DiscardSlot(*frame, true);
            return;
        }

        std::array<double, kScopeCount> resolved{};
        for (std::size_t index = 0; index < kScopeCount; ++index)
        {
            if (!frame->used[index])
                continue;

            std::uint64_t begin = 0;
            std::uint64_t end = 0;
            result = m_context->GetData(
                frame->timestamps[index].begin.Get(), &begin, sizeof(begin), kNoFlush);
            if (result == S_FALSE)
                return;
            if (FAILED(result))
            {
                DiscardSlot(*frame, false);
                return;
            }

            result = m_context->GetData(
                frame->timestamps[index].end.Get(), &end, sizeof(end), kNoFlush);
            if (result == S_FALSE)
                return;
            if (FAILED(result) || !MetricsDetail::TryTimestampMilliseconds(
                    begin, end, disjointData.Frequency, false, resolved[index]))
            {
                DiscardSlot(*frame, false);
                return;
            }
        }

        m_scopeMilliseconds = resolved;
        m_resolvedFrameSerial = frame->frameSerial;
        m_lastFrameDisjoint = false;
        PublishOutcome(frame->frameSerial, GpuFrameOutcome::Valid);
        frame->pending = false;
    }

    double GpuProfiler::ScopeMs(GpuScope scope) const noexcept
    {
        const std::size_t index = ScopeIndex(scope);
        return index < kScopeCount ? m_scopeMilliseconds[index] : 0.0;
    }

    GpuFrameOutcome GpuProfiler::FrameOutcome(std::uint64_t frameSerial) const noexcept
    {
        const OutcomeRecord& record = m_outcomes[frameSerial % kBufferedFrames];
        return record.frameSerial == frameSerial ? record.outcome : GpuFrameOutcome::Unavailable;
    }

    bool GpuProfiler::IsUserScope(GpuScope scope) noexcept
    {
        const std::size_t index = ScopeIndex(scope);
        return index > ScopeIndex(GpuScope::Frame) && index < kScopeCount;
    }

    std::size_t GpuProfiler::ScopeIndex(GpuScope scope) noexcept
    {
        return static_cast<std::size_t>(scope);
    }

    GpuProfiler::FrameQueries* GpuProfiler::FindFreeSlot() noexcept
    {
        for (std::size_t offset = 0; offset < kBufferedFrames; ++offset)
        {
            const std::size_t index = (m_nextSlot + offset) % kBufferedFrames;
            if (!m_frames[index].pending)
                return &m_frames[index];
        }
        return nullptr;
    }

    GpuProfiler::FrameQueries* GpuProfiler::FindOldestPendingSlot() noexcept
    {
        FrameQueries* oldest = nullptr;
        for (FrameQueries& frame : m_frames)
        {
            if (!frame.pending)
                continue;
            if (!oldest || frame.frameSerial < oldest->frameSerial)
                oldest = &frame;
        }
        return oldest;
    }

    void GpuProfiler::PublishOutcome(
        std::uint64_t frameSerial, GpuFrameOutcome outcome) noexcept
    {
        OutcomeRecord& record = m_outcomes[frameSerial % kBufferedFrames];
        record.frameSerial = frameSerial;
        record.outcome = outcome;
    }

    void GpuProfiler::DiscardSlot(FrameQueries& frame, bool disjoint) noexcept
    {
        PublishOutcome(frame.frameSerial, GpuFrameOutcome::Discarded);
        frame.pending = false;
        m_lastFrameDisjoint = disjoint;
        ++m_discardedFrameCount;
    }

#if defined(ALICE_METRICS_TESTING)
    void GpuProfiler::BeginFrameForTesting(std::uint64_t frameSerial) noexcept
    {
        FrameQueries& frame = m_frames[0];
        frame.frameSerial = frameSerial;
        frame.used.fill(false);
        frame.active.fill(false);
        frame.pending = false;
        m_recordingFrame = &frame;
    }

    bool GpuProfiler::ReserveScopeForTesting(GpuScope scope) noexcept
    {
        return ReserveScope(scope, false);
    }

    void GpuProfiler::ReleaseScopeForTesting(GpuScope scope) noexcept
    {
        if (!m_recordingFrame || !IsUserScope(scope))
            return;
        m_recordingFrame->active[ScopeIndex(scope)] = false;
    }
#endif

    ScopedGpuProfile::ScopedGpuProfile(GpuProfiler& profiler, GpuScope scope)
        : m_profiler(profiler.BeginScope(scope) ? &profiler : nullptr)
        , m_scope(scope)
    {
    }

    ScopedGpuProfile::~ScopedGpuProfile()
    {
        if (m_profiler)
            m_profiler->EndScope(m_scope);
    }
}
