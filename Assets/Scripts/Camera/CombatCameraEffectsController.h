#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "../Combat/C_CombatContracts.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UICurveAsset.h"

namespace Alice
{
    class C_CombatSessionComponent;

    class CombatCameraEffectsController : public IScript
    {
        ALICE_BODY(CombatCameraEffectsController);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        enum class PulseKind : std::uint8_t
        {
            Impact = 0,
            Block,
            Damage,
            Break,
            Parry,
            GuardEnter,
            Groggy,
            Count
        };

        struct PulseConfig
        {
            bool enabled = false;
            float blurIntensity = 0.0f;
            float blurRadius = 0.25f;
            float fovZoomInDeg = 0.0f;
            float fovOvershootDeg = 0.0f;
            float shakeAmplitude = 0.0f;
            float shakeDurationSec = 0.0f;
            float cooldownSec = 0.0f;

            bool overrideTiming = false;
            float blurDurationSec = 0.0f;
            float fovInSec = 0.0f;
            float fovHoldSec = 0.0f;
            float fovOutSec = 0.0f;
        };

        struct ActivePulse
        {
            float elapsedSec = 0.0f;
            float totalSec = 0.0f;
            float blurDurationSec = 0.0f;

            float blurIntensity = 0.0f;
            float blurRadius = 0.25f;
            float centerX = 0.5f;
            float centerY = 0.5f;

            float fovZoomInDeg = 0.0f;
            float fovOvershootDeg = 0.0f;
            float fovInSec = 0.0f;
            float fovHoldSec = 0.0f;
            float fovOutSec = 0.0f;
        };

        struct PulseAccumulation
        {
            float fovOffsetDeg = 0.0f;
            float blurIntensity = 0.0f;
            float blurRadius = 0.25f;
            float blurCenterX = 0.5f;
            float blurCenterY = 0.5f;
        };

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

        void ResolveSessionAndActors(bool logWarnings);
        void TryBindResolveDelegate();
        void UnbindResolveDelegateSafe();
        void ResolveCameraEntity();
        bool EnsurePostProcessVolume();
        void CapturePostProcessBaseline();
        void RestorePostProcessBaseline();

        void RefreshCurveAssets();
        float EvaluatePulseEnvelope01(float t01) const;
        float EvaluateChargeEnvelope01(float t01) const;

        void UpdateStateDrivenEffects(float deltaTime);
        void UpdateRollAssist(bool dodgeActive);
        void UpdateGroggyFollowMode(float deltaTime);
        void UpdateParryPause(float deltaTime);

        PulseAccumulation UpdateActivePulses(float deltaTime);
        float EvaluatePulseFovOffsetDeg(const ActivePulse& pulse) const;

        void ApplyOutputs(float deltaTime, const PulseAccumulation& pulseAccumulation);
        void ApplyImpactBlur(float intensity, float radius, float centerX, float centerY);
        void ApplyShake(const PulseConfig& config);

        void TriggerPulse(PulseKind kind, const PulseConfig& config, const DirectX::XMFLOAT3* hitPosWS);
        DirectX::XMFLOAT2 ResolvePulseCenter(const DirectX::XMFLOAT3* hitPosWS) const;

        void TriggerImpactPulse(const DirectX::XMFLOAT3& hitPosWS, bool heavyHit, float heavyScaleOverride = 1.0f);
        void TriggerBlockPulse(const DirectX::XMFLOAT3& hitPosWS);
        void TriggerDamagePulse(const DirectX::XMFLOAT3& hitPosWS, float damage);
        void TriggerBreakPulse(const DirectX::XMFLOAT3* hitPosWS);
        void TriggerParryPulse(const DirectX::XMFLOAT3& hitPosWS);
        void TriggerGuardEnterPulse();
        void TriggerGroggyPulse(const DirectX::XMFLOAT3* hitPosWS);
        void ActivateParryPause();

        void OnCombatResolve(EntityId victimId,
                             EntityId attackerId,
                             std::uint8_t resolveResult,
                             float damage,
                             const DirectX::XMFLOAT3& hitPosWS);

        bool IsPlayerEntity(EntityId id) const { return id != InvalidEntityId && id == m_playerId; }
        bool IsBossEntity(EntityId id) const { return id != InvalidEntityId && id == m_bossId; }

    private:
        ALICE_PROPERTY(std::string, m_sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, m_cameraName, "MainCamera");
        ALICE_PROPERTY(bool, m_enableLogs, false);
        ALICE_PROPERTY(bool, m_suspendFovWhenFatalActive, true);

        ALICE_PROPERTY(std::string, m_postProcessVolumeName, "CombatCameraImpactVolume");
        ALICE_PROPERTY(bool, m_autoCreatePostProcessVolume, true);
        ALICE_PROPERTY(int, m_postProcessPriority, 2400);
        ALICE_PROPERTY(float, m_maxBlurIntensity, 3.0f);
        ALICE_PROPERTY(float, m_defaultBlurRadius, 0.25f);
        ALICE_PROPERTY(float, m_defaultBlurCenterX, 0.5f);
        ALICE_PROPERTY(float, m_defaultBlurCenterY, 0.5f);
        ALICE_PROPERTY(bool, m_useHitPosCenter, true);
        ALICE_PROPERTY(float, m_hitCenterScaleX, 0.25f);
        ALICE_PROPERTY(float, m_hitCenterScaleY, 0.18f);

        ALICE_PROPERTY(float, m_globalPulseCooldownSec, 0.02f);
        ALICE_PROPERTY(float, m_baseBlurDurationSec, 0.05f);
        ALICE_PROPERTY(float, m_baseFovInSec, 0.03f);
        ALICE_PROPERTY(float, m_baseFovHoldSec, 0.01f);
        ALICE_PROPERTY(float, m_baseFovOutSec, 0.18f);
        ALICE_PROPERTY(float, m_baseShakeFrequency, 26.0f);
        ALICE_PROPERTY(float, m_baseShakeDecay, 2.1f);

        ALICE_PROPERTY(bool, m_usePulseCurveAsset, true);
        ALICE_PROPERTY(std::string, m_pulseCurvePath, "Assets/Curves/UI/FadeInOut.uicurve");
        ALICE_PROPERTY(float, m_pulseCurveTimeScale, 1.0f);
        ALICE_PROPERTY(float, m_pulseCurveValueScale, 1.0f);
        ALICE_PROPERTY(float, m_pulseCurveValueBias, 0.0f);
        ALICE_PROPERTY(bool, m_pulseCurveClamp01, true);

        ALICE_PROPERTY(bool, m_useChargeCurveAsset, true);
        ALICE_PROPERTY(std::string, m_chargeCurvePath, "Assets/Curves/UI/Cooldown.uicurve");
        ALICE_PROPERTY(float, m_chargeCurveTimeScale, 1.0f);
        ALICE_PROPERTY(float, m_chargeCurveValueScale, 1.0f);
        ALICE_PROPERTY(float, m_chargeCurveValueBias, 0.0f);
        ALICE_PROPERTY(bool, m_chargeCurveClamp01, true);

        ALICE_PROPERTY(bool, m_impactEnabled, true);
        ALICE_PROPERTY(float, m_impactBlurIntensity, 1.0f);
        ALICE_PROPERTY(float, m_impactFovZoomInDeg, 2.8f);
        ALICE_PROPERTY(float, m_impactFovOvershootDeg, 0.6f);
        ALICE_PROPERTY(float, m_impactShakeAmplitude, 0.06f);
        ALICE_PROPERTY(float, m_impactShakeDurationSec, 0.14f);
        ALICE_PROPERTY(float, m_impactCooldownSec, 0.03f);
        ALICE_PROPERTY(float, m_impactHeavyDamageThreshold, 18.0f);
        ALICE_PROPERTY(float, m_impactHeavyBlurMul, 1.25f);
        ALICE_PROPERTY(float, m_impactHeavyFovMul, 1.3f);
        ALICE_PROPERTY(float, m_impactHeavyShakeMul, 1.35f);

        ALICE_PROPERTY(bool, m_blockEnabled, true);
        ALICE_PROPERTY(float, m_blockBlurIntensity, 0.7f);
        ALICE_PROPERTY(float, m_blockFovZoomInDeg, 1.4f);
        ALICE_PROPERTY(float, m_blockFovOvershootDeg, 0.2f);
        ALICE_PROPERTY(float, m_blockShakeAmplitude, 0.05f);
        ALICE_PROPERTY(float, m_blockShakeDurationSec, 0.12f);
        ALICE_PROPERTY(float, m_blockCooldownSec, 0.04f);

        ALICE_PROPERTY(bool, m_damageEnabled, true);
        ALICE_PROPERTY(float, m_damageBlurIntensity, 0.9f);
        ALICE_PROPERTY(float, m_damageFovZoomInDeg, 2.0f);
        ALICE_PROPERTY(float, m_damageFovOvershootDeg, 0.35f);
        ALICE_PROPERTY(float, m_damageShakeAmplitude, 0.08f);
        ALICE_PROPERTY(float, m_damageShakeDurationSec, 0.18f);
        ALICE_PROPERTY(float, m_damageCooldownSec, 0.05f);
        ALICE_PROPERTY(float, m_damageHeavyThreshold, 22.0f);
        ALICE_PROPERTY(float, m_damageHeavyMul, 1.25f);

        ALICE_PROPERTY(bool, m_breakEnabled, true);
        ALICE_PROPERTY(float, m_breakBlurIntensity, 1.25f);
        ALICE_PROPERTY(float, m_breakFovZoomInDeg, -1.4f);
        ALICE_PROPERTY(float, m_breakFovOvershootDeg, 0.6f);
        ALICE_PROPERTY(float, m_breakShakeAmplitude, 0.12f);
        ALICE_PROPERTY(float, m_breakShakeDurationSec, 0.24f);
        ALICE_PROPERTY(float, m_breakCooldownSec, 0.08f);

        ALICE_PROPERTY(bool, m_parryEnabled, true);
        ALICE_PROPERTY(float, m_parryBlurIntensity, 1.35f);
        ALICE_PROPERTY(float, m_parryFovZoomInDeg, 3.4f);
        ALICE_PROPERTY(float, m_parryFovOvershootDeg, 0.8f);
        ALICE_PROPERTY(float, m_parryFovZoomScale, 1.35f);
        ALICE_PROPERTY(float, m_parryFovOvershootScale, 1.1f);
        ALICE_PROPERTY(float, m_parryShakeAmplitude, 0.09f);
        ALICE_PROPERTY(float, m_parryShakeDurationSec, 0.16f);
        ALICE_PROPERTY(float, m_parryCooldownSec, 0.08f);
        ALICE_PROPERTY(bool, m_parryOverrideTiming, true);
        ALICE_PROPERTY(float, m_parryBlurDurationSec, 0.05f);
        ALICE_PROPERTY(float, m_parryFovInSec, 0.015f);
        ALICE_PROPERTY(float, m_parryFovHoldSec, 0.08f);
        ALICE_PROPERTY(float, m_parryFovOutSec, 0.13f);
        ALICE_PROPERTY(bool, m_parryPauseEnabled, true);
        ALICE_PROPERTY(float, m_parryPauseSec, 0.055f);
        ALICE_PROPERTY(float, m_parryPauseCameraTimeScale, 0.04f);

        ALICE_PROPERTY(bool, m_guardEnterEnabled, true);
        ALICE_PROPERTY(float, m_guardEnterBlurIntensity, 0.35f);
        ALICE_PROPERTY(float, m_guardEnterFovZoomInDeg, 0.6f);
        ALICE_PROPERTY(float, m_guardEnterFovOvershootDeg, 0.0f);
        ALICE_PROPERTY(float, m_guardEnterShakeAmplitude, 0.0f);
        ALICE_PROPERTY(float, m_guardEnterShakeDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_guardEnterCooldownSec, 0.08f);

        ALICE_PROPERTY(bool, m_groggyEnabled, true);
        ALICE_PROPERTY(float, m_groggyBlurIntensity, 0.95f);
        ALICE_PROPERTY(float, m_groggyFovZoomInDeg, 2.2f);
        ALICE_PROPERTY(float, m_groggyFovOvershootDeg, 0.5f);
        ALICE_PROPERTY(float, m_groggyShakeAmplitude, 0.07f);
        ALICE_PROPERTY(float, m_groggyShakeDurationSec, 0.2f);
        ALICE_PROPERTY(float, m_groggyCooldownSec, 0.15f);
        ALICE_PROPERTY(bool, m_groggyForceFollowModeEnabled, true);
        ALICE_PROPERTY(int, m_groggyFollowMode, 4);
        ALICE_PROPERTY(float, m_groggyFollowModeDurationSec, 0.9f);

        ALICE_PROPERTY(bool, m_chargeEnabled, true);
        ALICE_PROPERTY(float, m_chargeMaxZoomInDeg, 3.2f);
        ALICE_PROPERTY(float, m_chargePerLevelScale, 0.35f);
        ALICE_PROPERTY(float, m_chargeBlurIntensity, 0.45f);
        ALICE_PROPERTY(float, m_chargeBlurRadius, 0.28f);
        ALICE_PROPERTY(float, m_chargeResponseSec, 0.18f);
        ALICE_PROPERTY(float, m_chargeReleaseSec, 0.12f);
        ALICE_PROPERTY(float, m_chargeCenterX, 0.5f);
        ALICE_PROPERTY(float, m_chargeCenterY, 0.5f);
        ALICE_PROPERTY(bool, m_chargeExecuteExtraPulseEnabled, true);
        ALICE_PROPERTY(float, m_chargeExecuteExtraPulseScale, 1.2f);

        ALICE_PROPERTY(bool, m_rollAssistEnabled, true);
        ALICE_PROPERTY(float, m_rollPositionDamping, 14.0f);
        ALICE_PROPERTY(float, m_rollRotationDamping, 18.0f);

    private:
        C_CombatSessionComponent* m_session = nullptr;
        EntityId m_playerId = InvalidEntityId;
        EntityId m_bossId = InvalidEntityId;
        EntityId m_cameraEntity = InvalidEntityId;
        EntityId m_volumeEntity = InvalidEntityId;
        std::uint64_t m_resolveListenerId = 0;

        PpvBaseline m_ppvBaseline{};

        std::vector<ActivePulse> m_activePulses{};
        std::array<float, static_cast<std::size_t>(PulseKind::Count)> m_lastPulseTriggerSec{};
        float m_lastGlobalPulseTriggerSec = -1000.0f;
        float m_runtimeSec = 0.0f;
        float m_lastAppliedFovOffsetDeg = 0.0f;

        bool m_prevStateValid = false;
        Combat::ActionState m_prevPlayerState = Combat::ActionState::Idle;
        Combat::ActionState m_prevBossState = Combat::ActionState::Idle;

        bool m_prevChargeActive = false;
        float m_chargeZoomDeg = 0.0f;
        float m_chargeCurrentMaxZoomDeg = 0.0f;

        bool m_rollOverrideActive = false;
        float m_savedRollPositionDamping = 0.0f;
        float m_savedRollRotationDamping = 0.0f;

        bool m_groggyModeOverrideActive = false;
        int m_savedFollowMode = 0;
        float m_groggyModeRemainingSec = 0.0f;

        bool m_parryPauseActive = false;
        float m_savedParryCameraTimeScale = 1.0f;
        float m_parryPauseRemainingSec = 0.0f;

        UICurveAsset m_pulseCurveAsset{};
        bool m_pulseCurveLoaded = false;
        bool m_pulseCurveWarned = false;
        std::string m_loadedPulseCurvePath{};

        UICurveAsset m_chargeCurveAsset{};
        bool m_chargeCurveLoaded = false;
        bool m_chargeCurveWarned = false;
        std::string m_loadedChargeCurvePath{};
    };
}
