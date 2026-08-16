#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace Alice::MetricsDetail
{
    bool TryTimestampMilliseconds(
        std::uint64_t begin,
        std::uint64_t end,
        std::uint64_t frequency,
        bool disjoint,
        double& outMs) noexcept;
}

namespace Alice
{
    enum class GpuScope : std::uint8_t
    {
        Frame = 0,
        MainPass,
        CameraPreview,
        ComputeEffects,
        ParticleOverlay,
        DebugOverlay,
        ToneMapAndUI,
        OverlayEffects,
        EditorDraw,
        Count
    };

    enum class GpuFrameOutcome : std::uint8_t
    {
        Unavailable = 0,
        Pending,
        Valid,
        Discarded
    };

    class GpuProfiler
    {
    public:
        static constexpr std::size_t kBufferedFrames = 4;
        static constexpr std::size_t kScopeCount = static_cast<std::size_t>(GpuScope::Count);

        GpuProfiler() = default;
        ~GpuProfiler();

        GpuProfiler(const GpuProfiler&) = delete;
        GpuProfiler& operator=(const GpuProfiler&) = delete;

        bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown() noexcept;

        void SetEnabled(bool enabled) noexcept;
        bool IsEnabled() const noexcept;

        bool BeginFrame(std::uint64_t frameSerial);
        bool BeginScope(GpuScope scope);
        void EndScope(GpuScope scope);
        void EndFrame();
        void Resolve();

        double ScopeMs(GpuScope scope) const noexcept;
        bool LastFrameDisjoint() const noexcept { return m_lastFrameDisjoint; }
        std::uint64_t ResolvedFrameSerial() const noexcept { return m_resolvedFrameSerial; }
        std::uint64_t DiscardedFrameCount() const noexcept { return m_discardedFrameCount; }
        GpuFrameOutcome FrameOutcome(std::uint64_t frameSerial) const noexcept;
        bool TryFrameScopes(
            std::uint64_t frameSerial,
            std::array<double, kScopeCount>& outScopes) const noexcept;

#if defined(ALICE_METRICS_TESTING)
        void BeginFrameForTesting(std::uint64_t frameSerial) noexcept;
        bool ReserveScopeForTesting(GpuScope scope) noexcept;
        void ReleaseScopeForTesting(GpuScope scope) noexcept;
        void SetFrameOutcomeForTesting(
            std::uint64_t frameSerial, GpuFrameOutcome outcome) noexcept
        {
            PublishOutcome(frameSerial, outcome);
        }
        void SetResolvedScopesForTesting(
            std::uint64_t frameSerial,
            const std::array<double, kScopeCount>& scopes) noexcept
        {
            m_scopeMilliseconds = scopes;
            m_resolvedFrameSerial = frameSerial;
            PublishOutcome(frameSerial, GpuFrameOutcome::Valid);
        }
#endif

    private:
        using QueryPtr = Microsoft::WRL::ComPtr<ID3D11Query>;

        struct TimestampQueries
        {
            QueryPtr begin;
            QueryPtr end;
        };

        struct FrameQueries
        {
            QueryPtr disjoint;
            std::array<TimestampQueries, kScopeCount> timestamps{};
            std::array<bool, kScopeCount> used{};
            std::array<bool, kScopeCount> active{};
            std::uint64_t frameSerial = 0;
            bool pending = false;
        };

        struct OutcomeRecord
        {
            std::uint64_t frameSerial = 0;
            GpuFrameOutcome outcome = GpuFrameOutcome::Unavailable;
            std::array<double, kScopeCount> scopeMilliseconds{};
            bool scopesValid = false;
        };

        static bool IsUserScope(GpuScope scope) noexcept;
        static std::size_t ScopeIndex(GpuScope scope) noexcept;
        bool ReserveScope(GpuScope scope, bool logWarning);
        FrameQueries* FindFreeSlot() noexcept;
        FrameQueries* FindOldestPendingSlot() noexcept;
        void PublishOutcome(std::uint64_t frameSerial, GpuFrameOutcome outcome) noexcept;
        void DiscardSlot(FrameQueries& slot, bool disjoint) noexcept;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        std::array<FrameQueries, kBufferedFrames> m_frames{};
        std::array<OutcomeRecord, kBufferedFrames> m_outcomes{};
        std::array<double, kScopeCount> m_scopeMilliseconds{};
        FrameQueries* m_recordingFrame = nullptr;
        std::size_t m_nextSlot = 0;
        bool m_initialized = false;
        bool m_enabled = false;
        bool m_lastFrameDisjoint = false;
        std::uint64_t m_resolvedFrameSerial = 0;
        std::uint64_t m_discardedFrameCount = 0;
    };

    class ScopedGpuProfile
    {
    public:
        ScopedGpuProfile(GpuProfiler& profiler, GpuScope scope);
        ~ScopedGpuProfile();

        ScopedGpuProfile(const ScopedGpuProfile&) = delete;
        ScopedGpuProfile& operator=(const ScopedGpuProfile&) = delete;

    private:
        GpuProfiler* m_profiler = nullptr;
        GpuScope m_scope = GpuScope::Count;
    };
}

#define ALICE_GPU_SCOPE_JOIN_IMPL(a, b) a##b
#define ALICE_GPU_SCOPE_JOIN(a, b) ALICE_GPU_SCOPE_JOIN_IMPL(a, b)
#define ALICE_GPU_SCOPE(profiler, scope) \
    ::Alice::ScopedGpuProfile ALICE_GPU_SCOPE_JOIN(aliceGpuScope_, __LINE__)((profiler), (scope))
