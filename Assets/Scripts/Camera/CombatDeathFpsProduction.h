#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CombatDeathFpsProduction : public IScript
    {
        ALICE_BODY(CombatDeathFpsProduction);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;
        bool IsUiBlockingActive() const;

    private:
        struct PpvBaseline
        {
            bool valid = false;
            bool bOverrideImpactBlurIntensity = false;
            bool bOverrideImpactBlurRadius = false;
            bool bOverrideImpactBlurCenterX = false;
            bool bOverrideImpactBlurCenterY = false;
            float impactBlurIntensity = 0.0f;
            float impactBlurRadius = 0.25f;
            float impactBlurCenterX = 0.5f;
            float impactBlurCenterY = 0.5f;
        };

        struct ScriptState
        {
            EntityId entity = InvalidEntityId;
            std::string scriptName{};
            bool enabled = true;
        };

        struct VisibilityState
        {
            EntityId entity = InvalidEntityId;
            bool visible = true;
        };

        struct CameraPose
        {
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        };

        bool ResolveOutputCamera();
        bool ResolveFpsCamera();
        bool EnsurePostProcessVolume();
        bool ResolveActorEntities();
        bool IsPlayerDead();

        void CapturePostProcessBaseline();
        void RestorePostProcessBaseline();

        void StartSequence();
        void StopSequence(bool restoreCameraControl);
        void ApplyTimeline(float deltaTime);
        void UpdateAfterCut(float deltaTime);
        void ApplyBlur(float intensity);
        void CutToFpsCamera();
        void FollowFpsCamera();
        void CaptureFallPose();
        void ApplyFallBlend(float deltaTime);
        void ApplyLookAtBoss(float deltaTime);
        void TriggerBossCharge();
        float GetEffectiveBossChargeDelaySec() const;

        void SetGameplayCameraControl(bool enable);
        void DisableBlockingScriptsForSequence();
        void RestoreOverriddenScripts();
        void SaveVisibilityRecursive(EntityId rootId);
        void SetVisibilityRecursive(EntityId rootId, bool visible);
        void RestoreSavedVisibility();
        void RestoreIfNeeded();

    private:
        ALICE_PROPERTY(std::string, m_outputCameraName, "MainCamera");
        ALICE_PROPERTY(std::string, m_fpsCameraName, "FPSCamera");
        ALICE_PROPERTY(bool, m_autoCreateFallbackFpsCamera, true);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_fallbackFpsLocalOffset, DirectX::XMFLOAT3(-0.0435285568f, 1.05593562f, 0.233519554f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_fallbackFpsLocalRotation, DirectX::XMFLOAT3(-0.1f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_fallbackFpsLocalScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        ALICE_PROPERTY(float, m_fallbackFpsFovDeg, 62.0f);
        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_playerCoreEntityName, "EGO_Core");
        ALICE_PROPERTY(std::string, m_playerWeaponEntityName, "EGO_Blade(combined)");
        ALICE_PROPERTY(std::string, m_bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, m_bossHeadEntityName, "BossEffectPoint");
        ALICE_PROPERTY(std::string, m_bossChargeClipName, "Boss|Boss|Charge_Attack");
        ALICE_PROPERTY(std::string, m_combatSessionScriptName, "C_CombatSessionComponent");
        ALICE_PROPERTY(std::string, m_bossCombatSessionScriptName, "C_BossCombatSessionComponent");
        ALICE_PROPERTY(std::string, m_bossBrainScriptName, "C_BossBrainComponent");

        ALICE_PROPERTY(bool, m_enableHotkey, true);
        ALICE_PROPERTY(bool, m_restartOnKey, true);
        ALICE_PROPERTY(bool, m_triggerWithAlpha7, true);
        ALICE_PROPERTY(bool, m_autoStartOnPlayerDeath, false);

        ALICE_PROPERTY(bool, m_disableFollowDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableLookAtDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableSpringArmDuringSequence, false);
        ALICE_PROPERTY(bool, m_holdFpsAfterCut, true);
        ALICE_PROPERTY(bool, m_restoreGameplayCameraWhenFinished, false);
        ALICE_PROPERTY(bool, m_hidePlayerOnCut, true);
        ALICE_PROPERTY(bool, m_hidePlayerCoreOnCut, true);
        ALICE_PROPERTY(bool, m_hidePlayerWeaponOnCut, true);
        ALICE_PROPERTY(bool, m_hidePlayerImmediatelyOnDeath, true);
        ALICE_PROPERTY(bool, m_restoreVisibilityOnStop, true);
        ALICE_PROPERTY(bool, m_disableCombatSessionDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableBossCombatSessionDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableBossBrainDuringSequence, true);
        ALICE_PROPERTY(bool, m_enableFallBlend, true);
        ALICE_PROPERTY(bool, m_lockLookToBossDuringFall, true);
        ALICE_PROPERTY(bool, m_triggerBossCharge, true);

        ALICE_PROPERTY(std::string, m_postProcessVolumeName, "DeathFpsImpactVolume");
        ALICE_PROPERTY(bool, m_autoCreatePostProcessVolume, true);
        ALICE_PROPERTY(int, m_postProcessPriority, 2800);

        ALICE_PROPERTY(float, m_blurPeakIntensity, 1.9f);
        ALICE_PROPERTY(float, m_blurRadius, 0.27f);
        ALICE_PROPERTY(float, m_blurCenterX, 0.5f);
        ALICE_PROPERTY(float, m_blurCenterY, 0.5f);
        ALICE_PROPERTY(float, m_blurInSec, 0.04f);
        ALICE_PROPERTY(float, m_blurHoldSec, 0.04f);
        ALICE_PROPERTY(float, m_blurOutSec, 0.10f);

        ALICE_PROPERTY(float, m_cutAtSec, 0.05f);
        ALICE_PROPERTY(float, m_holdFpsDurationSec, 0.0f); // 0: infinite hold
        ALICE_PROPERTY(float, m_fallBlendDurationSec, 0.55f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_fallOffsetWorld, DirectX::XMFLOAT3(0.0f, -0.82f, 0.18f));
        ALICE_PROPERTY(bool, m_approachBossDuringFall, true);
        ALICE_PROPERTY(float, m_targetBossDistanceAfterFall, 3.0f);
        ALICE_PROPERTY(float, m_lookAtBossHeightOffset, 1.30f);
        ALICE_PROPERTY(float, m_lookPitchOffsetDeg, -2.0f);
        ALICE_PROPERTY(float, m_lookRotationDamping, 10.0f);
        ALICE_PROPERTY(float, m_triggerBossChargeAfterCutSec, 0.0f);
        ALICE_PROPERTY(float, m_bossChargeSpeed, 1.0f);
        ALICE_PROPERTY(float, m_bossFacingYawOffsetDeg, 180.0f);

    private:
        EntityId m_outputCameraEntity = InvalidEntityId;
        EntityId m_fpsCameraEntity = InvalidEntityId;
        EntityId m_volumeEntity = InvalidEntityId;
        EntityId m_playerEntity = InvalidEntityId;
        EntityId m_playerCoreEntity = InvalidEntityId;
        EntityId m_playerWeaponEntity = InvalidEntityId;
        EntityId m_bossEntity = InvalidEntityId;
        EntityId m_bossHeadEntity = InvalidEntityId;

        bool m_running = false;
        bool m_cutDone = false;
        bool m_prevPlayerDead = false;
        float m_elapsedSec = 0.0f;
        float m_holdFpsElapsedSec = 0.0f;
        float m_afterCutElapsedSec = 0.0f;
        bool m_bossChargeTriggered = false;
        bool m_fallPoseCaptured = false;
        CameraPose m_fallStartPose{};
        CameraPose m_fallEndPose{};

        bool m_controlsOverridden = false;
        bool m_hasFollow = false;
        bool m_hasLookAt = false;
        bool m_hasSpringArm = false;
        bool m_savedFollowEnabled = true;
        bool m_savedLookAtEnabled = false;
        bool m_savedSpringArmEnabled = true;

        std::vector<ScriptState> m_scriptOverrides{};
        std::vector<VisibilityState> m_savedVisibility{};
        PpvBaseline m_ppvBaseline{};
    };
}
