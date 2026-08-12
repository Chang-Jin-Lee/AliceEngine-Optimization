#include "Runtime/Rendering/Metrics/GpuProfiler.h"
#include "Runtime/Rendering/Metrics/RenderStats.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
}

int main()
{
    double ms = -1.0;
    Check(Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 1'000'000, false, ms),
        "valid timestamp range must resolve");
    Check(std::abs(ms - 3.0) < 0.000001, "timestamp conversion must return 3 ms");

    ms = 42.0;
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 0, false, ms),
        "zero frequency must be rejected");
    Check(ms == 42.0, "rejected timestamp samples must preserve the previous output");
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(100, 3100, 1'000'000, true, ms),
        "disjoint sample must be rejected");
    Check(!Alice::MetricsDetail::TryTimestampMilliseconds(3100, 100, 1'000'000, false, ms),
        "reversed timestamp range must be rejected");

    Alice::RenderFrameCounters counters{};
    counters.RecordDraw(false);
    counters.RecordDraw(true);
    counters.RecordBoneCbUpload(65'488);
    Check(counters.drawCalls == 2, "all draw calls must be counted");
    Check(counters.instancedDrawCalls == 1, "instanced draw calls must be counted separately");
    Check(counters.boneCbMapCount == 1, "successful bone CB maps must be counted");
    Check(counters.boneCbBytesUploaded == 65'488, "uploaded bone CB bytes must accumulate");
    counters.Reset();
    Check(counters.drawCalls == 0 && counters.boneCbBytesUploaded == 0,
        "frame counter reset must clear the previous frame");

    Alice::GpuProfiler profiler;
    Check(!profiler.IsEnabled(), "a profiler without a D3D device must start disabled");
    for (std::uint8_t value = 0; value < static_cast<std::uint8_t>(Alice::GpuScope::Count); ++value)
    {
        const auto scope = static_cast<Alice::GpuScope>(value);
        Check(profiler.ScopeMs(scope) == 0.0, "unresolved GPU scopes must report zero milliseconds");
    }
    Check(profiler.ScopeMs(Alice::GpuScope::Count) == 0.0,
        "the Count sentinel must not index GPU scope storage");
    Check(profiler.ResolvedFrameSerial() == 0, "a new profiler must not report a resolved frame");
    Check(profiler.DiscardedFrameCount() == 0, "a new profiler must not report discarded frames");
    Check(profiler.FrameOutcome(42) == Alice::GpuFrameOutcome::Unavailable,
        "an unknown frame serial must report an unavailable outcome");
    Check(!profiler.BeginScope(Alice::GpuScope::Frame),
        "the implicit frame scope must not be opened explicitly");

    Alice::GpuProfiler scopeProfiler;
    scopeProfiler.BeginFrameForTesting(1);
    Check(scopeProfiler.ReserveScopeForTesting(Alice::GpuScope::MainPass),
        "the first user scope in a frame must be accepted");
    Check(!scopeProfiler.ReserveScopeForTesting(Alice::GpuScope::CameraPreview),
        "a nested user scope must be rejected");
    scopeProfiler.ReleaseScopeForTesting(Alice::GpuScope::MainPass);
    Check(scopeProfiler.ReserveScopeForTesting(Alice::GpuScope::CameraPreview),
        "a sequential user scope must be accepted after the previous scope closes");

    Alice::RenderStats stats;
    Check(!stats.IsEnabled(), "render stats must start disabled");
    stats.RecordDraw(false);
    stats.RecordBoneCbUpload(64);
    Check(stats.Latest().drawCalls == 0 && stats.Latest().boneCbBytesUploaded == 0,
        "disabled render stats must ignore counter updates");

    stats.SetEnabled(true);
    Check(stats.BeginFrame(7, 16.5), "CPU-only render stats must begin without a D3D device");
    stats.RecordDraw(false);
    stats.RecordDraw(true);
    stats.RecordBoneCbUpload(65'488);
    stats.EndFrame();
    const Alice::RenderStatsSnapshot& cpuOnly = stats.Latest();
    Check(cpuOnly.frameSerial == 7, "CPU-only stats must publish the matching frame serial");
    Check(cpuOnly.drawCalls == 2 && cpuOnly.instancedDrawCalls == 1,
        "CPU-only stats must publish draw counters");
    Check(cpuOnly.boneCbMapCount == 1 && cpuOnly.boneCbBytesUploaded == 65'488,
        "CPU-only stats must publish bone upload counters");
    Check(std::abs(cpuOnly.presentMs - 16.5) < 0.000001,
        "CPU-only stats must preserve the supplied Present interval");
    Check(cpuOnly.cpuFrameMs >= 0.0, "CPU frame duration must be non-negative");
    Check(!cpuOnly.pipelineStatsValid, "CPU-only stats must not claim pipeline query data");

    stats.SetEnabled(false);
    Check(!stats.BeginFrame(8, 20.0), "disabled render stats must not begin a frame");

    Alice::GpuProfiler gateProfiler;
    Alice::RenderStats gateStats;
    Check(gateStats.SeedPendingFrameForTesting(10), "first delayed stats frame must be seeded");
    Check(gateStats.SeedPendingFrameForTesting(11), "second delayed stats frame must be seeded");
    Check(gateStats.SeedPendingFrameForTesting(12), "discarded stats frame must be seeded");
    Check(gateStats.SeedPendingFrameForTesting(13), "pending stats frame must be seeded");

    gateProfiler.SetFrameOutcomeForTesting(10, Alice::GpuFrameOutcome::Valid);
    gateProfiler.SetFrameOutcomeForTesting(11, Alice::GpuFrameOutcome::Valid);
    gateProfiler.SetFrameOutcomeForTesting(12, Alice::GpuFrameOutcome::Discarded);
    gateProfiler.SetFrameOutcomeForTesting(13, Alice::GpuFrameOutcome::Pending);
    gateStats.LatchGpuOutcomesForTesting(gateProfiler);

    // A new four-frame cycle overwrites the profiler's small outcome cache.
    // Stats must already have consumed every terminal result, not just its oldest slot.
    gateProfiler.SetFrameOutcomeForTesting(14, Alice::GpuFrameOutcome::Pending);
    gateProfiler.SetFrameOutcomeForTesting(15, Alice::GpuFrameOutcome::Pending);
    gateProfiler.SetFrameOutcomeForTesting(16, Alice::GpuFrameOutcome::Pending);
    Check(gateStats.GpuValidatedForTesting(10),
        "the oldest valid GPU outcome must be latched while pipeline data is delayed");
    Check(gateStats.GpuValidatedForTesting(11),
        "all valid GPU outcomes must be latched before the outcome ring is overwritten");
    Check(!gateStats.PendingForTesting(12),
        "a discarded GPU outcome must release its matching stats slot");
    Check(gateStats.PendingForTesting(13) && !gateStats.GpuValidatedForTesting(13),
        "a non-terminal GPU outcome must remain pending without validation");

    return failures == 0 ? 0 : 1;
}
