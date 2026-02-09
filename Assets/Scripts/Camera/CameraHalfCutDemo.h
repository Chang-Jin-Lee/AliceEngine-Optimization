#pragma once

#include <string>
#include <DirectXMath.h>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/UI/UICurveAsset.h"

namespace Alice
{
    class CameraHalfCutDemo : public IScript
    {
        ALICE_BODY(CameraHalfCutDemo);

    public:
        const char* GetName() const override { return "CameraHalfCutDemo"; }

        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        bool EnsurePostProcessVolume();
        bool ResolveTargetCamera();
        void CaptureBaseline();
        void RefreshFovCurveAsset();
        void PrepareOverrides();
        void ApplyEffectState(float timeSec);
        void ApplyPostProcess(float splitAmount, float flash01, float timeSec);
        void ApplyCameraFov(float fovDeg);
        void TriggerCameraShake();
        void TrySpawnCutVfx(float timeSec);
        void UpdateCutVfxState(float timeSec);
        EntityId SpawnCutVfx();
        void ConfigureCutVfxRecursive(EntityId id);
        void ApplyCutVfxRuntimeRecursive(EntityId id, float fade01, bool emitNewParticles, bool loop);
        void ResetAll();
        float GetTotalDuration() const;
        float EvaluateSplitAmount(float timeSec) const;
        float EvaluateFlash01(float timeSec) const;
        float EvaluateFovCurveValue(float timeSec) const;
        float EvaluateFov(float timeSec) const;

    private:
        struct PpvBaseline
        {
            bool valid{ false };

            bool bOverrideSplitAmount{ false };
            bool bOverrideSplitAngleDeg{ false };
            bool bOverrideSplitLineOffset{ false };
            bool bOverrideSplitFeather{ false };

            bool bOverrideExposure{ false };
            bool bOverrideBloomThreshold{ false };
            bool bOverrideBloomKnee{ false };
            bool bOverrideBloomIntensity{ false };
            bool bOverrideBloomGaussianIntensity{ false };
            bool bOverrideBloomRadius{ false };

            float splitAmount{ 0.0f };
            float splitAngleDeg{ 0.0f };
            float splitLineOffset{ 0.0f };
            float splitFeather{ 0.001f };
            bool bOverrideSplitFxIntensity{ false };
            bool bOverrideSplitFxWidth{ false };
            bool bOverrideSplitFxSpeed{ false };
            bool bOverrideSplitFxTimeSec{ false };
            float splitFxIntensity{ 0.0f };
            float splitFxWidth{ 0.01f };
            float splitFxSpeed{ 30.0f };
            float splitFxTimeSec{ 0.0f };

            float exposure{ 0.0f };
            float bloomThreshold{ 1.0f };
            float bloomKnee{ 0.5f };
            float bloomIntensity{ 0.0f };
            float bloomGaussianIntensity{ 1.0f };
            float bloomRadius{ 1.0f };
        };

    private:
        ALICE_PROPERTY(int, m_triggerKey, static_cast<int>(KeyCode::Alpha1));
        ALICE_PROPERTY(std::string, m_cameraName, "MainCamera");
        ALICE_PROPERTY(std::string, m_postProcessVolumeName, "CameraHalfCutVolume");
        ALICE_PROPERTY(bool, m_autoCreateVolume, true);
        ALICE_PROPERTY(float, m_peakAmount, 0.08f);
        ALICE_PROPERTY(float, m_angleDeg, 12.0f);
        ALICE_PROPERTY(float, m_lineOffset, 0.0f);
        ALICE_PROPERTY(float, m_feather, 0.002f);
        ALICE_PROPERTY(float, m_attackSec, 0.08f);
        ALICE_PROPERTY(float, m_holdSec, 0.05f);
        ALICE_PROPERTY(float, m_releaseSec, 0.22f);
        ALICE_PROPERTY(float, m_zoomFov, 28.0f);
        ALICE_PROPERTY(bool, m_useFovCurveAsset, true);
        ALICE_PROPERTY(std::string, m_fovCurvePath, "Assets/Curves/UI/FadeInOut.uicurve");
        ALICE_PROPERTY(float, m_fovCurveTimeScale, 1.0f);
        ALICE_PROPERTY(float, m_fovCurveValueScale, 1.0f);
        ALICE_PROPERTY(float, m_fovCurveValueBias, 0.0f);
        ALICE_PROPERTY(bool, m_fovCurveClamp01, false);
        ALICE_PROPERTY(float, m_peakExposure, 3.5f);
        ALICE_PROPERTY(float, m_peakBloomIntensity, 2.6f);
        ALICE_PROPERTY(float, m_peakBloomGaussianIntensity, 2.2f);
        ALICE_PROPERTY(float, m_peakBloomRadius, 1.8f);
        ALICE_PROPERTY(float, m_peakBloomThreshold, 0.35f);
        ALICE_PROPERTY(float, m_peakBloomKnee, 0.65f);
        ALICE_PROPERTY(bool, m_splitFxEnabled, true);
        ALICE_PROPERTY(float, m_splitFxIntensity, 1.45f);
        ALICE_PROPERTY(float, m_splitFxWidth, 0.01f);
        ALICE_PROPERTY(float, m_splitFxSpeed, 36.0f);
        ALICE_PROPERTY(float, m_splitFxTimeScale, 1.0f);
        ALICE_PROPERTY(float, m_shakeAmplitude, 0.15f);
        ALICE_PROPERTY(float, m_shakeFrequency, 30.0f);
        ALICE_PROPERTY(float, m_shakeDuration, 0.23f);
        ALICE_PROPERTY(float, m_shakeDecay, 2.2f);
        ALICE_PROPERTY(int, m_volumePriority, 500);
        ALICE_PROPERTY(bool, m_spawnCutVfx, true);
        ALICE_PROPERTY(std::string, m_cutVfxPrefabPath, "Assets/Prefabs/(04)Charging.prefab");
        ALICE_PROPERTY(float, m_cutVfxTriggerSec, 0.08f);
        ALICE_PROPERTY(bool, m_cutVfxUseAnchorMidpoint, true);
        ALICE_PROPERTY(std::string, m_cutVfxAnchorAName, "LeftCube");
        ALICE_PROPERTY(std::string, m_cutVfxAnchorBName, "RightCube");
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_cutVfxMidpointOffset, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(float, m_cutVfxForwardDistance, 9.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_cutVfxLocalOffset, DirectX::XMFLOAT3(0.0f, -0.35f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_cutVfxEulerOffsetDeg, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_cutVfxScale, DirectX::XMFLOAT3(0.75f, 0.75f, 0.75f));
        ALICE_PROPERTY(float, m_cutVfxFadeOutSec, 0.22f);
        ALICE_PROPERTY(float, m_cutVfxDespawnDelaySec, 0.8f);
        ALICE_PROPERTY(float, m_cutVfxBaseSpawnRate, 1.0f);
        ALICE_PROPERTY(float, m_cutVfxBaseAlpha, 1.0f);
        ALICE_PROPERTY(float, m_cutVfxBaseIntensity, 1.0f);
        ALICE_PROPERTY(bool, m_cutVfxForceOneShot, false);

        PpvBaseline m_ppvBaseline{};
        EntityId m_volumeEntity{ InvalidEntityId };
        EntityId m_cameraEntity{ InvalidEntityId };
        EntityId m_activeCutVfx{ InvalidEntityId };
        bool m_playing{ false };
        bool m_shakeTriggered{ false };
        bool m_vfxTriggered{ false };
        float m_elapsedSec{ 0.0f };
        float m_baseFov{ 45.0f };
        UICurveAsset m_fovCurveAsset{};
        bool m_fovCurveLoaded{ false };
        bool m_fovCurveWarned{ false };
        std::string m_loadedFovCurvePath{};
        bool m_warnedMissingVolume{ false };
    };
}
