#pragma once

#include <string>
#include <cstdint>

#include <DirectXMath.h>

#include "../Combat/C_CombatContracts.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class C_CombatSessionComponent;

    class CombatFatalHalfCutController : public IScript
    {
        ALICE_BODY(CombatFatalHalfCutController);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        enum class Phase : std::uint8_t
        {
            Idle = 0,
            HalfCut,
            BlendBack
        };

        struct CameraPose
        {
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotationRad{ 0.0f, 0.0f, 0.0f };
            float fovDeg = 60.0f;
        };

        struct PpvBaseline
        {
            bool valid = false;

            bool bOverrideSplitAmount = false;
            bool bOverrideSplitAngleDeg = false;
            bool bOverrideSplitLineOffset = false;
            bool bOverrideSplitFeather = false;
            bool bOverrideSplitOneSideDown = false;
            bool bOverrideSplitFxIntensity = false;
            bool bOverrideSplitFxWidth = false;
            bool bOverrideSplitFxSpeed = false;
            bool bOverrideSplitFxTimeSec = false;
            bool bOverrideSplitFxColorA = false;
            bool bOverrideSplitFxColorB = false;
            bool bOverrideExposure = false;
            bool bOverrideBloomThreshold = false;
            bool bOverrideBloomKnee = false;
            bool bOverrideBloomIntensity = false;
            bool bOverrideBloomGaussianIntensity = false;
            bool bOverrideBloomRadius = false;
            bool bOverrideImpactBlurIntensity = false;
            bool bOverrideImpactBlurRadius = false;
            bool bOverrideImpactBlurCenterX = false;
            bool bOverrideImpactBlurCenterY = false;

            float splitAmount = 0.0f;
            float splitAngleDeg = 12.0f;
            float splitLineOffset = 0.0f;
            float splitFeather = 0.002f;
            float splitOneSideDown = 0.0f;
            float splitFxIntensity = 0.0f;
            float splitFxWidth = 0.01f;
            float splitFxSpeed = 30.0f;
            float splitFxTimeSec = 0.0f;
            DirectX::XMFLOAT3 splitFxColorA{ 2.8f, 1.2f, 0.45f };
            DirectX::XMFLOAT3 splitFxColorB{ 0.10f, 1.05f, 0.95f };
            float exposure = 0.0f;
            float bloomThreshold = 1.0f;
            float bloomKnee = 0.5f;
            float bloomIntensity = 0.0f;
            float bloomGaussianIntensity = 1.0f;
            float bloomRadius = 1.0f;
            float impactBlurIntensity = 0.0f;
            float impactBlurRadius = 0.25f;
            float impactBlurCenterX = 0.5f;
            float impactBlurCenterY = 0.5f;
        };

        void ResolveSessionAndActors(bool logWarnings);
        void ResolveCameraEntity();
        bool EnsurePostProcessVolume();
        void CapturePostProcessBaseline();
        void RestorePostProcessBaseline();

        bool GetCurrentCameraPose(CameraPose& outPose) const;
        void ApplyCameraPose(const CameraPose& pose);
        bool BuildSnapPoseBehindBoss(CameraPose& outPose) const;

        void SaveAndDisableCameraControls();
        void RestoreCameraControls();

        void StartFatalSequence();
        void UpdateHalfCut(float deltaTime);
        void UpdateBlendBack(float deltaTime);
        void FinishSequence();
        void AbortSequence();

        float GetCutTotalDurationSec() const;
        float EvaluateSplitAmount(float timeSec) const;
        float EvaluateFlash01(float timeSec) const;
        float EvaluateCutWeight01(float splitAmount, float flash01) const;
        void ApplyCutPostProcess(float splitAmount, float flash01, float timeSec, float cutWeight01);

    private:
        ALICE_PROPERTY(std::string, m_sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, m_cameraName, "MainCamera");
        ALICE_PROPERTY(bool, m_enableLogs, false);

        ALICE_PROPERTY(std::string, m_postProcessVolumeName, "FatalHalfCutVolume");
        ALICE_PROPERTY(bool, m_autoCreateVolume, false);
        ALICE_PROPERTY(int, m_volumePriority, 2600);

        ALICE_PROPERTY(float, m_peakAmount, -0.12f);
        ALICE_PROPERTY(float, m_angleDeg, -40.38f);
        ALICE_PROPERTY(float, m_lineOffset, -0.11f);
        ALICE_PROPERTY(float, m_feather, 0.123f);
        ALICE_PROPERTY(float, m_attackSec, 0.08f);
        ALICE_PROPERTY(float, m_holdSec, 0.05f);
        ALICE_PROPERTY(float, m_releaseSec, 0.20f);
        ALICE_PROPERTY(bool, m_useFatalProgressStart, false);
        ALICE_PROPERTY(float, m_startFatalProgress01, 0.52210528f);
        ALICE_PROPERTY(bool, m_useFatalElapsedStart, true);
        ALICE_PROPERTY(float, m_startFatalElapsedSec, 2.85f);
        ALICE_PROPERTY(bool, m_enableSnapMove, false);
        ALICE_PROPERTY(float, m_snapAtSec, 0.10f);
        ALICE_PROPERTY(bool, m_snapAfterRecover, false);
        ALICE_PROPERTY(float, m_blendBackDurationSec, 0.10f);
        ALICE_PROPERTY(float, m_cutZoomInDeg, 7.0f);
        ALICE_PROPERTY(float, m_retriggerCooldownSec, 0.25f);

        ALICE_PROPERTY(bool, m_splitFxEnabled, true);
        ALICE_PROPERTY(float, m_splitFxIntensity, 5.5f);
        ALICE_PROPERTY(float, m_splitFxWidth, 0.088f);
        ALICE_PROPERTY(float, m_splitFxSpeed, 62.0f);
        ALICE_PROPERTY(float, m_splitFxTimeScale, 1.15f);
        ALICE_PROPERTY(bool, m_splitOneSideDown, true);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_splitFxColorA, DirectX::XMFLOAT3(2.8f, 1.2f, 0.45f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_splitFxColorB, DirectX::XMFLOAT3(0.10f, 1.05f, 0.95f));

        ALICE_PROPERTY(float, m_peakExposure, 2.99f);
        ALICE_PROPERTY(float, m_peakBloomThreshold, 0.55f);
        ALICE_PROPERTY(float, m_peakBloomKnee, 0.70f);
        ALICE_PROPERTY(float, m_peakBloomIntensity, 3.05f);
        ALICE_PROPERTY(float, m_peakBloomGaussianIntensity, 2.3f);
        ALICE_PROPERTY(float, m_peakBloomRadius, 2.03f);
        ALICE_PROPERTY(bool, m_enableFlashTone, false);

        ALICE_PROPERTY(bool, m_useImpactBlurDuringCut, true);
        ALICE_PROPERTY(float, m_peakImpactBlurIntensity, 2.2f);
        ALICE_PROPERTY(float, m_impactBlurRadius, 0.32f);
        ALICE_PROPERTY(float, m_impactBlurCenterX, 0.5f);
        ALICE_PROPERTY(float, m_impactBlurCenterY, 0.5f);

        ALICE_PROPERTY(float, m_snapBehindDistance, 2.0f);
        ALICE_PROPERTY(float, m_snapHeight, 1.35f);
        ALICE_PROPERTY(float, m_snapSideOffset, 0.0f);
        ALICE_PROPERTY(float, m_lookAtPlayerYOffset, 1.05f);
        ALICE_PROPERTY(float, m_lookAtBossYOffset, 1.2f);

        ALICE_PROPERTY(bool, m_disableFollowDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableLookAtDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableSpringArmDuringSequence, false);

    private:
        C_CombatSessionComponent* m_session = nullptr;
        EntityId m_playerId = InvalidEntityId;
        EntityId m_bossId = InvalidEntityId;
        EntityId m_cameraEntity = InvalidEntityId;
        EntityId m_volumeEntity = InvalidEntityId;

        bool m_prevFatalActive = false;
        bool m_startedThisFatal = false;
        float m_runtimeSec = 0.0f;
        float m_lastSequenceStartSec = -1000.0f;

        Phase m_phase = Phase::Idle;
        float m_phaseTimerSec = 0.0f;
        bool m_snapApplied = false;

        CameraPose m_preFatalPose{};
        CameraPose m_snapPose{};
        CameraPose m_blendFromPose{};

        bool m_controlsCaptured = false;
        bool m_savedFollowEnabled = true;
        bool m_savedLookAtEnabled = false;
        bool m_savedSpringArmEnabled = true;
        int m_savedFollowMode = 0;

        PpvBaseline m_ppvBaseline{};
    };
}
