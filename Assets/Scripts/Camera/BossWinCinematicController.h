#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class BossWinCinematicController : public IScript
    {
        ALICE_BODY(BossWinCinematicController);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;
        bool IsUiBlockingActive() const { return m_seq.running; }

    private:
        enum class Phase : std::uint8_t
        {
            Idle = 0,
            FocusIn,
            FocusHold,
            Return,
            WaitShake,
            Shake
        };

        struct CameraPose
        {
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotationRad{ 0.0f, 0.0f, 0.0f };
            float fovDeg = 60.0f;
        };

        struct MaterialState
        {
            EntityId entity = InvalidEntityId;
            float alpha = 1.0f;
            bool transparent = false;
        };

        struct ScriptState
        {
            EntityId entity = InvalidEntityId;
            std::string scriptName{};
            bool enabled = true;
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

            bool bOverrideBloomIntensity = false;
            bool bOverrideBloomThreshold = false;
            bool bOverrideBloomKnee = false;
            bool bOverrideEmissiveBloomIntensity = false;
            float bloomIntensity = 0.0f;
            float bloomThreshold = 1.0f;
            float bloomKnee = 0.5f;
            float emissiveBloomIntensity = 1.0f;
        };

        struct SequenceState
        {
            bool running = false;
            Phase phase = Phase::Idle;
            float phaseTimerSec = 0.0f;
            float totalTimerSec = 0.0f;
            CameraPose startPose{};
            CameraPose focusPose{};
        };

        bool ResolveEntities(bool logWarnings);
        bool ResolveCameraEntity();
        bool EnsurePostProcessVolume();
        void CapturePostProcessBaseline();
        void RestorePostProcessBaseline();

        bool TryGetCameraPose(CameraPose& outPose) const;
        void ApplyCameraPose(const CameraPose& pose) const;
        bool BuildFocusPose(CameraPose& outPose) const;
        void SetGameplayCameraControl(bool enable);
        void CaptureClearResultSnapshot();
        bool ShouldMainChangerHandleBossTransition() const;
        bool IsBossFadePoseReady() const;

        void DisableMainChangerIfNeeded(bool onStart);
        void RestoreOverriddenScripts();
        void TriggerBossDeathUiIfAvailable();

        void StartSequence(bool forceBossDead);
        void UpdateSequence(float deltaTime);
        void FinishSequence();
        void AbortSequence(bool restoreBoss);

        void UpdateBossFade();
        void ApplyBossAlpha(float alpha);
        void DisableBossEffectsRecursive();
        void CollectBossMaterials();
        void RestoreBossMaterials();
        void SetEntityVisible(EntityId id, bool visible);
        bool IsBossDead() const;
        void ForceBossDead();

        void ApplyPostProcess(float blurIntensity, float blurRadius, float centerX, float centerY, float bloomWeight);
        void TriggerShake();
        void AdvancePhase(Phase nextPhase);

    private:
        ALICE_PROPERTY(std::string, m_sceneManagerEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, m_mainCameraName, "MainCamera");
        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, m_bossWeaponEntityName, "BossWeapon");
        ALICE_PROPERTY(std::string, m_bossEffectPointName, "BossEffectPoint");
        ALICE_PROPERTY(std::string, m_eyeObjectName, "W_EYE");
        ALICE_PROPERTY(std::string, m_additionalBossEffectNamesCsv, "BossDeshEffect,Boss_AttackTrailVfx1,Boss_AttackTrailVfx2");

        ALICE_PROPERTY(bool, m_enableHotkeys, true);
        ALICE_PROPERTY(int, m_previewKey, static_cast<int>(KeyCode::Alpha6));
        ALICE_PROPERTY(int, m_forceKillKey, static_cast<int>(KeyCode::Alpha7));
        ALICE_PROPERTY(bool, m_autoStartOnBossDeath, true);

        ALICE_PROPERTY(bool, m_disableMainChangerOnStart, true);
        ALICE_PROPERTY(bool, m_disableMainChangerDuringSequence, true);
        ALICE_PROPERTY(std::string, m_mainChangerScriptName, "MainChangerScript");

        ALICE_PROPERTY(bool, m_disableFollowDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableFollowInputDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableLookAtDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableSpringArmDuringSequence, false);

        ALICE_PROPERTY(float, m_focusDurationSec, 1.08f);
        ALICE_PROPERTY(float, m_focusHoldSec, 4.82f);
        ALICE_PROPERTY(float, m_returnDurationSec, 1.64f);
        ALICE_PROPERTY(float, m_waitBeforeShakeSec, 0.77f);
        ALICE_PROPERTY(float, m_focusDistance, 3.66f);
        ALICE_PROPERTY(float, m_focusHeightOffset, 0.71f);
        ALICE_PROPERTY(float, m_focusSideOffset, 0.22f);
        ALICE_PROPERTY(float, m_targetLookYOffset, 0.76f);
        ALICE_PROPERTY(float, m_focusFovDeg, 36.96f);
        ALICE_PROPERTY(bool, m_useSmoothBlend, true);

        ALICE_PROPERTY(bool, m_fadeBossEnabled, true);
        ALICE_PROPERTY(float, m_bossFadeStartSec, 0.18f);
        ALICE_PROPERTY(float, m_bossFadeDurationSec, 1.6f);
        ALICE_PROPERTY(float, m_bossMinAlpha, 0.0f);
        ALICE_PROPERTY(bool, m_waitBossPoseBeforeFade, true);
        ALICE_PROPERTY(float, m_bossFadePoseWaitTimeoutSec, 4.0f);
        ALICE_PROPERTY(bool, m_showEyeDuringSequence, true);

        ALICE_PROPERTY(std::string, m_postProcessVolumeName, "BossWinCinematicVolume");
        ALICE_PROPERTY(bool, m_autoCreatePostProcessVolume, true);
        ALICE_PROPERTY(int, m_postProcessPriority, 2950);
        ALICE_PROPERTY(float, m_focusBlurPeakIntensity, 1.3f);
        ALICE_PROPERTY(float, m_focusBlurRadius, 0.27f);
        ALICE_PROPERTY(float, m_focusBlurCenterX, 0.5f);
        ALICE_PROPERTY(float, m_focusBlurCenterY, 0.5f);
        ALICE_PROPERTY(float, m_shakeBlurPeakIntensity, 1.65f);
        ALICE_PROPERTY(float, m_shakeBlurDurationSec, 0.5f);
        ALICE_PROPERTY(bool, m_enableBloomBoost, true);
        ALICE_PROPERTY(float, m_bloomBoostIntensity, 1.08f);
        ALICE_PROPERTY(float, m_bloomBoostThreshold, 0.85f);
        ALICE_PROPERTY(float, m_bloomBoostKnee, 0.48f);
        ALICE_PROPERTY(float, m_emissiveBloomBoost, 1.24f);

        ALICE_PROPERTY(float, m_shakeAmplitude, 0.08f);
        ALICE_PROPERTY(float, m_shakeFrequency, 20.33f);
        ALICE_PROPERTY(float, m_shakeDurationSec, 0.3f);
        ALICE_PROPERTY(float, m_shakeDecay, 1.74f);

        ALICE_PROPERTY(bool, m_enableSceneTransition, true);
        ALICE_PROPERTY(std::string, m_nextScenePath, "Assets/Scenes/MainGameLoopScene/ClearScene.scene");
        ALICE_PROPERTY(float, m_sceneTransitionDelaySec, 0.55f);
        ALICE_PROPERTY(bool, m_restoreBossIfNoSceneTransition, true);

        ALICE_PROPERTY(bool, m_enableLogs, false);

    private:
        EntityId m_sceneManagerEntity = InvalidEntityId;
        EntityId m_cameraEntity = InvalidEntityId;
        EntityId m_playerEntity = InvalidEntityId;
        EntityId m_bossEntity = InvalidEntityId;
        EntityId m_bossEffectPointEntity = InvalidEntityId;
        EntityId m_bossWeaponEntity = InvalidEntityId;
        EntityId m_eyeEntity = InvalidEntityId;
        EntityId m_volumeEntity = InvalidEntityId;
        std::vector<EntityId> m_additionalBossEffectEntities{};

        SequenceState m_seq{};
        bool m_prevBossDead = false;
        bool m_pendingSceneLoad = false;
        float m_pendingSceneLoadTimerSec = 0.0f;
        bool m_pendingUseFade = false;
        bool m_pendingFadeStarted = false;
        float m_pendingFadeLeadInSec = 0.0f;
        float m_pendingSceneDelayAfterFadeSec = 0.0f;
        std::string m_pendingFadeEntityName{};
        bool m_bossFadeArmed = false;
        bool m_bossFadeComplete = false;
        float m_bossFadeArmSec = 0.0f;

        bool m_controlsOverridden = false;
        bool m_hasFollow = false;
        bool m_hasLookAt = false;
        bool m_hasSpringArm = false;
        bool m_savedFollowEnabled = true;
        bool m_savedFollowInputEnabled = true;
        bool m_savedLookAtEnabled = false;
        bool m_savedSpringArmEnabled = true;

        std::vector<MaterialState> m_bossMaterials{};
        std::vector<ScriptState> m_scriptOverrides{};
        PpvBaseline m_ppvBaseline{};
    };
}
