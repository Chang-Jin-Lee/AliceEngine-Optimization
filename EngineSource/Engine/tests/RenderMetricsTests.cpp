#include "Runtime/Rendering/Metrics/GpuProfiler.h"
#include "Runtime/Rendering/Metrics/RenderStats.h"
#include "Editor/Panels/MetricsOverlay.h"
#include "Runtime/Rendering/Metrics/LegacyPathFlags.h"
#include "Runtime/Rendering/ShadowCulling.h"
#include "Runtime/Rendering/ShaderCode/DeferredShader.h"
#include "Runtime/Engine/CommandLineOptions.h"
#include "Runtime/Engine/BenchCameraTake.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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
    Alice::CommandLineOptions options{};
    std::string optionError;
    Check(Alice::ParseCommandLineOptions({}, options, optionError),
        "empty command line must preserve normal launch behavior");
    Check(!options.benchRequested && options.vsyncEnabled &&
        options.width == 1920 && options.height == 1080 &&
        options.warmupSeconds == 5.0 && options.frameStride == 1,
        "bench option defaults must match the documented capture contract");

    const std::vector<std::wstring> benchArgs{
        L"--scene=#01PrototypeMap.scene", L"--camera-replay=Bench/take.json",
        L"--legacy", L"--vsync=off", L"--duration=15", L"--warmup=5",
        L"--csv=Artifacts/bench.csv", L"--width=1280", L"--height=720"
    };
    Check(Alice::ParseCommandLineOptions(benchArgs, options, optionError),
        "documented benchmark arguments must parse");
    Check(options.benchRequested && options.legacy && !options.vsyncEnabled &&
        options.scene == std::filesystem::path("Assets/Scenes/#01PrototypeMap.scene") &&
        options.cameraReplay == std::filesystem::path("Bench/take.json") &&
        options.csvPath == std::filesystem::path("Artifacts/bench.csv") &&
        options.durationSeconds == 15.0 && options.width == 1280 && options.height == 720,
        "parsed benchmark arguments must retain their exact values");
    Check(!Alice::ParseCommandLineOptions(
        { L"--camera-record=a.json", L"--camera-replay=b.json" }, options, optionError),
        "record and replay must be mutually exclusive");
    Check(!Alice::ParseCommandLineOptions(
        { L"--csv=a.csv", L"--frames=%06d.png" }, options, optionError),
        "measurement CSV and PNG capture must be separate runs");
    Check(!Alice::ParseCommandLineOptions({ L"--frame-stride=0" }, options, optionError),
        "frame stride zero must be rejected");
    Check(!Alice::ParseCommandLineOptions({ L"--vsync=maybe" }, options, optionError),
        "unknown vsync values must be rejected");
    Check(Alice::ParseCommandLineOptions(
        { L"--frames=Artifacts/%06d.png", L"--frame-stride=3" }, options, optionError) &&
        options.framePattern == L"Artifacts/%06d.png" && options.frameStride == 3,
        "documented PNG capture arguments must parse");
    Check(!Alice::ParseCommandLineOptions(
        { L"--camera-record=Bench/take.json" }, options, optionError),
        "camera recording must require an explicit reproducible scene");
    Check(Alice::ParseCommandLineOptions(
        { L"--scene=Nested/Dense.scene", L"--camera-record=Bench/take.json" },
        options, optionError) &&
        options.scene == std::filesystem::path("Assets/Scenes/Nested/Dense.scene"),
        "recording must preserve the complete logical scene path");

    Alice::BenchCameraRecorder recorder;
    Alice::CameraTakeFrame camera0{};
    camera0.position = { 0.0f, 0.0f, 0.0f };
    camera0.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    camera0.fovY = 0.7f;
    Alice::CameraTakeFrame camera1 = camera0;
    camera1.position = { 2.0f, 4.0f, 6.0f };
    camera1.rotation = { 0.0f, 1.0f, 0.0f, 0.0f };
    camera1.fovY = 0.9f;
    recorder.AddSample(0.0, camera0);
    recorder.AddSample(2.0, camera1);
    const Alice::BenchCameraTake resampled = recorder.BuildTake("Dense.scene", 1.0);
    Check(resampled.frames.size() == 3 && resampled.FrameCount() == 3,
        "camera samples must resample onto inclusive fixed-delta slots");
    Check(std::abs(resampled.frames[1].position.x - 1.0f) < 0.0001f &&
        std::abs(resampled.frames[1].fovY - 0.8f) < 0.0001f,
        "camera position and FOV must interpolate at fixed slots");
    Check(std::abs(std::abs(resampled.frames[1].rotation.y) - 0.7071067f) < 0.0001f &&
        std::abs(std::abs(resampled.frames[1].rotation.w) - 0.7071067f) < 0.0001f,
        "camera rotation must use quaternion slerp at fixed slots");
    const std::string takeJson = Alice::SerializeBenchCameraTake(resampled);
    Alice::BenchCameraTake roundTrip{};
    std::string takeError;
    Check(Alice::DeserializeBenchCameraTake(takeJson, roundTrip, takeError) &&
        roundTrip.FrameCount() == resampled.FrameCount(),
        "camera take JSON must round-trip without losing frames");
    Check(!Alice::DeserializeBenchCameraTake(
        R"({"version":1,"scene":"Dense.scene","fixedDeltaSeconds":0.016,"frameCount":0,"frames":[]})",
        roundTrip, takeError),
        "camera takes without replay frames must be rejected");
    Alice::CameraTakeFrame replayFrame{};
    Check(roundTrip.FrameAt(2, replayFrame) &&
        std::abs(replayFrame.position.z - 6.0f) < 0.0001f &&
        !roundTrip.FrameAt(3, replayFrame),
        "camera replay must use exact frame indices and stop at frameCount");

    Alice::LegacyPathFlags& legacy = Alice::LegacyPathFlags::Get();
    Check(!legacy.AnyEnabled(), "legacy path flags must default to current behavior");
    legacy.SetAll(true);
    Check(legacy.fullBoneConstantBuffer && legacy.copyPaletteEveryFrame &&
        legacy.noCameraMatrixCache && legacy.animateWhenNotPlaying &&
        legacy.heapAllocWorldMatrix && legacy.perParticleDrawCall &&
        legacy.staticMeshThroughSkinning && legacy.outlineOnByDefault &&
        legacy.opaqueInTransparentPass,
        "SetAll(true) must enable every documented legacy path");
    Check(legacy.AnyEnabled(), "AnyEnabled must report an enabled legacy path");
    Check(legacy.AllEnabled(), "AllEnabled must report when every legacy path is enabled");
    legacy.SetAll(false);
    Check(!legacy.AnyEnabled(), "SetAll(false) must restore the current path");
    legacy.fullBoneConstantBuffer = true;
    Check(legacy.AnyEnabled() && !legacy.AllEnabled(),
        "a partial legacy selection must not appear as all paths enabled");
    legacy.SetAll(false);
    Check(Alice::LegacyPathDetail::BoneMatricesToWrite(3, false) == 3,
        "the current bone upload path must touch only the active palette");
    Check(Alice::LegacyPathDetail::BoneMatricesToWrite(3, true) == 1023,
        "the P01 legacy path must touch the full 1023-matrix constant buffer");
    Check(Alice::LegacyPathDetail::BoneUploadBytes(3, false) <
        Alice::LegacyPathDetail::BoneUploadBytes(3, true),
        "P01 legacy accounting must report more written bytes than current");
    Check(!Alice::LegacyPathDetail::ShouldEvaluateAnimation(false, true, false, false),
        "P03 current path must skip a stable stopped pose");
    Check(Alice::LegacyPathDetail::ShouldEvaluateAnimation(false, true, true, false),
        "P03 current path must reevaluate a changed stopped pose");
    Check(Alice::LegacyPathDetail::ShouldEvaluateAnimation(false, true, false, true),
        "P03 legacy path must reevaluate a stable stopped pose");
    Check(std::strstr(Alice::DeferredShader::GBufferOutlineVS,
        "input.Normal * gOutlineWidth") != nullptr,
        "the deferred static outline shader must extrude vertices by outline width");

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

    std::array<double, Alice::GpuProfiler::kScopeCount> frame10Scopes{};
    frame10Scopes[static_cast<std::size_t>(Alice::GpuScope::Frame)] = 10.25;
    frame10Scopes[static_cast<std::size_t>(Alice::GpuScope::MainPass)] = 8.5;
    gateProfiler.SetResolvedScopesForTesting(10, frame10Scopes);
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
    Check(gateStats.GpuScopesValidForTesting(10) &&
        std::abs(gateStats.GpuScopeMsForTesting(10, Alice::GpuScope::Frame) - 10.25) < 0.000001,
        "a stats slot must retain the GPU scopes from its matching frame serial");
    Check(gateStats.GpuValidatedForTesting(11),
        "all valid GPU outcomes must be latched before the outcome ring is overwritten");
    Check(!gateStats.PendingForTesting(12),
        "a discarded GPU outcome must release its matching stats slot");
    Check(gateStats.PendingForTesting(13) && !gateStats.GpuValidatedForTesting(13),
        "a non-terminal GPU outcome must remain pending without validation");

    Alice::MetricsHistory history;
    for (int value = 1; value <= 241; ++value)
        history.Push(static_cast<float>(value));
    Check(history.Count() == Alice::MetricsHistory::kCapacity,
        "metrics history must retain exactly 240 samples");
    Check(history.ValueAtOldest(0) == 2.0f && history.ValueAtOldest(239) == 241.0f,
        "metrics history must preserve chronological order after wrapping");

    Alice::MetricsHistory summaryHistory;
    for (int value = 1; value <= 200; ++value)
        summaryHistory.Push(static_cast<float>(value));
    const Alice::MetricsSummary summary = summaryHistory.CalculateSummary();
    Check(summary.minimum == 1.0f && summary.maximum == 200.0f,
        "metrics summary must report exact extrema");
    Check(std::abs(summary.average - 100.5) < 0.000001,
        "metrics summary must report the arithmetic mean");
    Check(std::abs(summary.onePercentLow - 199.5) < 0.000001,
        "1% low must be the mean of the slowest ceil(1%) frame times");

    Alice::MetricsOverlay frameOverlay;
    Alice::GpuProfiler frameProfiler;
    Alice::RenderStatsSnapshot frameSnapshot{};
    frameSnapshot.frameSerial = 1;
    frameSnapshot.presentMs = 10.0;
    frameSnapshot.gpuScopesValid = true;
    frameSnapshot.gpuScopeMilliseconds[
        static_cast<std::size_t>(Alice::GpuScope::Frame)] = 7.0;
    std::array<double, Alice::GpuProfiler::kScopeCount> newerScopes{};
    newerScopes[static_cast<std::size_t>(Alice::GpuScope::Frame)] = 99.0;
    frameProfiler.SetResolvedScopesForTesting(99, newerScopes);
    frameOverlay.Update(frameSnapshot, frameProfiler);
    Check(std::abs(frameOverlay.ScopeMsForTesting(Alice::GpuScope::Frame) - 7.0) < 0.000001,
        "the overlay must display GPU scopes coherent with the stats snapshot serial");
    frameOverlay.Update(frameSnapshot, frameProfiler);
    frameOverlay.Update(frameSnapshot, frameProfiler);
    Check(frameOverlay.History().Count() == 3,
        "metrics history must advance once per rendered frame while queries remain unresolved");
    Check(frameOverlay.History().ValueAtOldest(2) == 10.0f,
        "unresolved frames must repeat the most recent valid present time");

    frameSnapshot.frameSerial = 2;
    frameSnapshot.presentMs = 20.0;
    frameOverlay.Update(frameSnapshot, frameProfiler);
    Check(frameOverlay.History().Count() == 4 &&
        frameOverlay.History().ValueAtOldest(3) == 20.0f,
        "a newly resolved frame must replace the repeated value on that render frame");

    // --- 방향광 그림자 캐스터 컬링 ---
    // 카메라 프러스텀으로 캐스터를 버리면 화면 밖 물체의 그림자가 사라진다.
    // 판정 기준이 라이트 볼륨이라는 것을 여기서 고정한다.
    {
        using Alice::ShadowCulling::IntersectsShadowVolume;
        const Alice::ShadowCulling::OrthoVolume volume{ 50.0f, 0.01f, 300.0f };

        Check(IntersectsShadowVolume(0.0f, 0.0f, 150.0f, 1.0f, volume),
            "a caster inside the shadow volume must be drawn");

        // 이번 수정의 회귀 케이스: 카메라 뒤에 있어도 라이트 볼륨 안이면 그림자를 남겨야 한다.
        Check(IntersectsShadowVolume(-45.0f, 0.0f, 20.0f, 2.0f, volume),
            "a caster behind the camera must still be drawn when it sits inside the light volume");

        Check(!IntersectsShadowVolume(500.0f, 0.0f, 150.0f, 1.0f, volume),
            "a caster far outside the volume in X must be culled");
        Check(!IntersectsShadowVolume(0.0f, -500.0f, 150.0f, 1.0f, volume),
            "a caster far outside the volume in Y must be culled");
        Check(!IntersectsShadowVolume(0.0f, 0.0f, 400.0f, 1.0f, volume),
            "a caster beyond farZ must be culled");
        Check(!IntersectsShadowVolume(0.0f, 0.0f, -50.0f, 1.0f, volume),
            "a caster before nearZ must be culled");

        // 경계에 걸친 물체는 반지름만큼 부풀린 판정에 걸려 살아남아야 한다.
        Check(IntersectsShadowVolume(53.0f, 0.0f, 150.0f, 5.0f, volume),
            "a caster straddling the volume edge must be kept, not culled");
        Check(!IntersectsShadowVolume(53.0f, 0.0f, 150.0f, 1.0f, volume),
            "a caster clear of the edge by more than its radius must be culled");
        Check(IntersectsShadowVolume(0.0f, 0.0f, 302.0f, 5.0f, volume),
            "a caster straddling farZ must be kept");
    }

    const std::uint64_t overlayNanoseconds =
        Alice::MetricsOverlay::MeasureAverageRenderNanosecondsForTesting(10'000);
    Check(overlayNanoseconds > 0, "metrics overlay render benchmark must execute");
    Check(overlayNanoseconds < 500'000,
        "metrics overlay CPU submission must stay below 0.5 ms per frame");
    std::cout << "metrics overlay average CPU submission: "
        << overlayNanoseconds << " ns\n";

    return failures == 0 ? 0 : 1;
}
