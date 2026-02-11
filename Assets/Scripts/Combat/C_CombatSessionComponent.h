#pragma once
/*
* 전투 루프 총괄. 플레이어/보스 Fighter·FSM·이벤트버스·리졸버를 갖고, 매 프레임 Intent → Sensors → FSM → Command → 적용 순서로 돌림.
* 씬의 매니저용 엔티티 (빈 오브젝트 등). m_playerGuid, m_bossGuid로 플레이어/보스 엔티티를 찾음.
*/

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/Foundation/Delegate.h"
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include "C_CombatContracts.h"

namespace Alice
{
    class C_CombatSessionComponent : public IScript
    {
        ALICE_BODY(C_CombatSessionComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void PostCombatUpdate(float deltaTime) override;
        void OnEnable() override;
        void OnDisable() override;
        ~C_CombatSessionComponent() override;

        Combat::ActionState GetPlayerState() const;
        Combat::ActionState GetBossState() const;
        Combat::ActionFlags GetPlayerFlags() const;
        Combat::ActionFlags GetBossFlags() const;
        std::uint64_t GetPlayerParrySuccessCount() const;
        bool IsPlayerRageActive() const;
        float GetPlayerRageRemainingSec() const;
        float GetPlayerRageCooldownRemainingSec() const;
        // TODO: HUD/UI gauge binding should use this normalized cooldown value (0..1).
        float GetPlayerRageCooldownNormalized() const;
        bool IsFatalActive() const;
        float GetFatalProgress01() const;
        float GetFatalRemainSec() const;

        // Combat resolve delegate: called when a hit is resolved
        // Parameters: victimId, attackerId, resolveResult, damage, hitPosWS
        using FOnCombatResolved = Alice::Delegate<EntityId, EntityId, std::uint8_t, float, const DirectX::XMFLOAT3&>;
        FOnCombatResolved OnCombatResolved;
        // Single-cast Delegate safety: dedicated channel for VFX bridge subscribers.
        FOnCombatResolved OnCombatResolvedVfx;
        using FOnCombatResolvedCameraListener = std::function<void(EntityId, EntityId, std::uint8_t, float, const DirectX::XMFLOAT3&)>;
        std::uint64_t AddOnCombatResolvedCameraListener(FOnCombatResolvedCameraListener listener);
        void RemoveOnCombatResolvedCameraListener(std::uint64_t listenerId);

        // Entity resolution (GUID preferred, name fallback when enabled)
        ALICE_PROPERTY(uint64_t, m_playerGuid, 0);
        ALICE_PROPERTY(uint64_t, m_bossGuid, 0);
        ALICE_PROPERTY(bool, m_autoResolveByName, true);
        ALICE_PROPERTY(std::string, m_playerName, "Player");
        ALICE_PROPERTY(std::string, m_bossName, "Enemy");

        // Logging
        ALICE_PROPERTY(bool, m_enableLogs, false);
        ALICE_PROPERTY(bool, m_enableCombatLogs, false);

        // Combat rules
        ALICE_PROPERTY(bool, m_playerCanBeHitstunned, true);
        // ALICE_PROPERTY(bool, m_bossCanBeHitstunned, false); // unused: boss hitstun is driven by boss session flow
        ALICE_PROPERTY(std::string, m_gimmickEntityName, "W_Target");
        ALICE_PROPERTY(std::string, m_healGimmickEntityName, "Heal_EYE");
        ALICE_PROPERTY(bool, m_blockPlayerActionsDuringGimmick, true);
        ALICE_PROPERTY(float, m_guardBreakPushbackScale, 0.5f);
        ALICE_PROPERTY(float, m_guardSuccessPushbackScale, 0.1f);
        ALICE_PROPERTY(float, m_hitPushbackScale, 0.2f);
        ALICE_PROPERTY(float, m_hitInvulnSec, 0.2f);
        ALICE_PROPERTY(float, m_hitstopSec, 2.0f);
        ALICE_PROPERTY(float, m_guardBreakPushbackDurationSec, 1.0f);
        ALICE_PROPERTY(float, m_hitPushbackDurationSec, 0.3f);
        ALICE_PROPERTY(float, m_phaseHowlingPushbackScale, 1.0f);
        ALICE_PROPERTY(float, m_phaseHowlingPushbackDurationSec, 1.0f);
        ALICE_PROPERTY(bool, m_phaseHowlingForceGuard, true);
        ALICE_PROPERTY(bool, m_phaseHowlingLockInput, true);
        ALICE_PROPERTY(bool, m_playerInteractionEnabled, false);
        ALICE_PROPERTY(float, m_healStartDelaySec, 1.0f);
        ALICE_PROPERTY(float, m_healTickIntervalSec, 1.0f);
        ALICE_PROPERTY(float, m_healTransferRatio, 0.1f);
        ALICE_PROPERTY(float, m_healWeaponMinRatio, 0.1f);
        ALICE_PROPERTY(float, m_healPlayerMaxRatio, 0.9f);
        // Legacy ratio heal tuning kept for backward compatibility; fixed-tick heal logic ignores these.

        // Boss groggy tuning
        ALICE_PROPERTY(float, m_bossGroggyGainLight, 8.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainHeavy, 16.0f);
        ALICE_PROPERTY(std::string, m_bossGroggyLoopClip, "");
        ALICE_PROPERTY(float, m_parryNoDurabilitySec, 0.2f);
        ALICE_PROPERTY(float, m_chargeScale0, 1.0f);
        ALICE_PROPERTY(float, m_chargeScale1, 1.2f);
        ALICE_PROPERTY(float, m_chargeScale2, 1.4f);
        ALICE_PROPERTY(float, m_chargeScale3, 1.8f);
        ALICE_PROPERTY(float, m_lightComboWindowSec, 0.5f);
        ALICE_PROPERTY(float, m_chargeCombo2Speed, 0.7f);
        ALICE_PROPERTY(float, m_playerRageLightAttackSpeedScale, 1.5f);
        ALICE_PROPERTY(float, m_rageDurationSec, 15.0f);
        ALICE_PROPERTY(float, m_rageCooldownSec, 30.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceLight1Sec, 1.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceLight2Sec, 1.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceLight3Sec, 3.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceHeavy1Sec, 1.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceHeavy2Sec, 1.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceHeavy3Sec, 3.0f);
        ALICE_PROPERTY(float, m_rageCooldownReduceParrySec, 1.0f);
        ALICE_PROPERTY(float, m_playerDamageLight1, 100.0f);
        ALICE_PROPERTY(float, m_playerDamageLight2, 150.0f);
        ALICE_PROPERTY(float, m_playerDamageLight3, 200.0f);
        ALICE_PROPERTY(float, m_playerDamageHeavy1, 200.0f);
        ALICE_PROPERTY(float, m_playerDamageHeavy2, 300.0f);
        ALICE_PROPERTY(float, m_playerDamageHeavy3, 500.0f);
        ALICE_PROPERTY(float, m_playerDamageExecution, 1000.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainLight1, 50.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainLight2, 75.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainLight3, 100.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainHeavy1, 100.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainHeavy2, 150.0f);
        ALICE_PROPERTY(float, m_bossGroggyGainHeavy3, 200.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitLight1, 25.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitLight2, 50.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitLight3, 100.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitHeavy1, 50.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitHeavy2, 100.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitHeavy3, 150.0f);
        ALICE_PROPERTY(float, m_weaponHealOnHitExecution, 500.0f);
        ALICE_PROPERTY(float, m_healInitialAmount, 100.0f);
        ALICE_PROPERTY(float, m_healHoldTickIntervalSec, 0.5f);
        ALICE_PROPERTY(float, m_healHoldTickAmount, 50.0f);
        // Player rage trail (red) on weapon target.
        ALICE_PROPERTY(bool, m_enablePlayerRageTrailVfx, true);
        ALICE_PROPERTY(std::string, m_playerRageTrailTargetName, "W_Target");
        ALICE_PROPERTY(std::string, m_playerRageTrailChildName, "W_Target_RageTrailVfx");
        ALICE_PROPERTY(std::string, m_playerRageTrailEffectPath, "Assets/VFX/UnityExport/(Opt)Effect_06_PortalEffect_2__Smoke_1_2/effect.json");
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_playerRageTrailColorTint, DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(float, m_playerRageTrailColorScale, 5.0f);
        ALICE_PROPERTY(float, m_playerRageTrailIntensityScale, 2.0f);
        ALICE_PROPERTY(float, m_playerRageTrailSpawnRateScale, 1.5f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_playerRageTrailLocalOffset, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_playerRageTrailLocalRotation, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_playerRageTrailLocalScale, DirectX::XMFLOAT3(0.018f, 0.018f, 0.018f));
        ALICE_PROPERTY(float, m_playerRageTrailSizeScale, 0.018f);
        ALICE_PROPERTY(float, m_playerRageTrailEmitMinSpeed, 0.15f);
        ALICE_PROPERTY(float, m_trailTailOffFallbackSec, 0.25f);
        // Boss attack trail on BossWeapon.
        ALICE_PROPERTY(bool, m_enableBossAttackTrailVfx, true);
        ALICE_PROPERTY(std::string, m_bossAttackTrailTargetName, "BossWeapon");
        ALICE_PROPERTY(std::string, m_bossAttackTrailChildName, "Boss_AttackTrailVfx1");
        ALICE_PROPERTY(std::string, m_bossAttackTrailChildName2, "Boss_AttackTrailVfx2");
        ALICE_PROPERTY(float, m_bossAttackTrailTailOffSec, 3.0f);
        // Charge stage one-shot compute VFX on Player(Tia) child (e.g. "Charging").
        ALICE_PROPERTY(std::string, m_playerChargeStageVfxName, "Charging");
        ALICE_PROPERTY(float, m_playerChargeStagePulseSec, 0.05f);

        // Animation blending
        ALICE_PROPERTY(float, m_animBlendSec, 0.12f);
        ALICE_PROPERTY(float, m_bossAnimBlendSec, 0.12f);
        ALICE_PROPERTY(float, m_bossGroggyEnterBlendSec, 1.5f);
        ALICE_PROPERTY(float, m_bossGroggyRecoverBlendSec, 1.0f);
        ALICE_PROPERTY(float, m_moveBlendSpeed, 8.0f);
        ALICE_PROPERTY(float, m_playerMoveInputDamping, 14.0f);
        ALICE_PROPERTY(float, m_playerMoveYawDamping, 12.0f);

        // Default animation clips (shared fallback)
        ALICE_PROPERTY(std::string, m_idleClip, "Idle");
        ALICE_PROPERTY(std::string, m_moveClip, "Walk");
        ALICE_PROPERTY(std::string, m_lightAttackClip, "alice-Apose_arm|Swing");
        ALICE_PROPERTY(std::string, m_lightAttackClip1, "");
        ALICE_PROPERTY(std::string, m_lightAttackClip2, "");
        ALICE_PROPERTY(std::string, m_lightAttackClip3, "");
        ALICE_PROPERTY(std::string, m_heavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_heavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_dodgeClip, "");
        ALICE_PROPERTY(std::string, m_chargeEnterClip, "");
        ALICE_PROPERTY(std::string, m_chargeLoopClip, "");
        ALICE_PROPERTY(std::string, m_hitClip, "");
        ALICE_PROPERTY(std::string, m_guardBreakClip, "");
        ALICE_PROPERTY(std::string, m_fatalAttackClip, "");
        ALICE_PROPERTY(std::string, m_interactionClip, "");
        ALICE_PROPERTY(std::string, m_healLoopClip, "");
        ALICE_PROPERTY(std::string, m_guardEnterClip, "");
        ALICE_PROPERTY(std::string, m_guardLoopClip, "");
        ALICE_PROPERTY(std::string, m_guardExitClip, "");
        ALICE_PROPERTY(float, m_guardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_guardExitDurationSec, 0.0f);

        // Per-entity animation overrides (optional)
        ALICE_PROPERTY(std::string, m_playerIdleClip, "rig|Tia_IDLE");
        ALICE_PROPERTY(std::string, m_playerMoveClip, "rig|Tia_Run");
        ALICE_PROPERTY(std::string, m_playerLightAttackClip, "rig|Tia_Normal_Attack_1");
        ALICE_PROPERTY(std::string, m_playerLightAttackClip1, "rig|Tia_Normal_Attack_1");
        ALICE_PROPERTY(std::string, m_playerLightAttackClip2, "rig|Tia_Normal_Attack_2");
        ALICE_PROPERTY(std::string, m_playerLightAttackClip3, "rig|Tia_Normal_Attack_3");
        ALICE_PROPERTY(std::string, m_playerRageAttackClip, "rig|Tia_Normal_Attack_1,2,3");
        ALICE_PROPERTY(std::string, m_playerHeavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_playerHeavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_playerDodgeClip, "rig|Tia_Rolling");
        ALICE_PROPERTY(std::string, m_playerChargeEnterClip, "rig|Tia_Charging");
        ALICE_PROPERTY(std::string, m_playerChargeLoopClip, "rig|Tia_Charged");
        ALICE_PROPERTY(std::string, m_playerHitClip, "rig|Tia_Hit");
        ALICE_PROPERTY(std::string, m_playerGuardBreakClip, "rig|Tia_Guard_Brake");
        ALICE_PROPERTY(std::string, m_playerFatalAttackClip, "rig|Tia_Grogi Attack");
        ALICE_PROPERTY(std::string, m_playerInteractionClip, "rig|Tia_Interaction");
        ALICE_PROPERTY(std::string, m_playerHealLoopClip, "rig|Tia_Hill");
        ALICE_PROPERTY(std::string, m_playerGuardEnterClip, "rig|Tia_Guard");
        ALICE_PROPERTY(std::string, m_playerGuardLoopClip, "rig|Tia_Guarding");
        ALICE_PROPERTY(std::string, m_playerGuardExitClip, "rig|Tia_Guard");
        ALICE_PROPERTY(std::string, m_playerParryClip, "rig|Tia_Parrying");
        ALICE_PROPERTY(float, m_playerGuardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_playerGuardExitDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_playerGuardEnterSpeedScale, 1.5f);
        ALICE_PROPERTY(float, m_playerGuardTransitionBlendSec, 0.12f);
        ALICE_PROPERTY(float, m_playerGuardReentryDelaySec, 0.3f);
        ALICE_PROPERTY(float, m_playerParryRecoverBlendSec, 0.1f);
        ALICE_PROPERTY(float, m_bossDeathFreezeTimeSec, 3.89f);
        ALICE_PROPERTY(std::string, m_bossIdleClip, "");
        ALICE_PROPERTY(std::string, m_bossMoveClip, "");
        ALICE_PROPERTY(std::string, m_bossLightAttackClip, "");
        ALICE_PROPERTY(std::string, m_bossLightAttackClip1, "");
        ALICE_PROPERTY(std::string, m_bossLightAttackClip2, "");
        ALICE_PROPERTY(std::string, m_bossLightAttackClip3, "");
        ALICE_PROPERTY(std::string, m_bossHeavyAttackClipA, "");
        ALICE_PROPERTY(std::string, m_bossHeavyAttackClipB, "");
        ALICE_PROPERTY(std::string, m_bossDodgeClip, "");
        ALICE_PROPERTY(std::string, m_bossChargeEnterClip, "");
        ALICE_PROPERTY(std::string, m_bossChargeLoopClip, "");
        ALICE_PROPERTY(std::string, m_bossHitClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardBreakClip, "");
        ALICE_PROPERTY(std::string, m_bossInteractionClip, "");
        ALICE_PROPERTY(std::string, m_bossHealLoopClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardEnterClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardLoopClip, "");
        ALICE_PROPERTY(std::string, m_bossGuardExitClip, "");
        ALICE_PROPERTY(float, m_bossGuardEnterDurationSec, 0.0f);
        ALICE_PROPERTY(float, m_bossGuardExitDurationSec, 0.0f);

        // TODO: temp feel-tuning; move to per-attack data.
        ALICE_PROPERTY(float, m_lightAttackMoveDistance, 0.7f);
        ALICE_PROPERTY(float, m_heavyAttackMoveDistance, 1.2f);
        ALICE_PROPERTY(float, m_lightAttackMoveStartSec, 1.75f);
        ALICE_PROPERTY(float, m_heavyAttackMoveStartSec, 2.2f);
        ALICE_PROPERTY(float, m_lightAttackMoveDurationSec, 0.05f);
        ALICE_PROPERTY(float, m_heavyAttackMoveDurationSec, 0.05f);
        ALICE_PROPERTY(float, m_bossChargeFacingTrackSec, 0.6f);
        ALICE_PROPERTY(float, m_bossSoulFacingTrackSec, 0.5f);
        ALICE_PROPERTY(float, m_bossDashMoveStartSec, -1.0f);
        ALICE_PROPERTY(float, m_bossKickAttackSpeedScale, 1.5f);
        ALICE_PROPERTY(float, m_bossBoostAttackSpeedScale, 1.5f);
        ALICE_PROPERTY(std::string, m_bossBoostDashVfxName, "BossDeshEffect");
        ALICE_PROPERTY(float, m_bossGapStepDistance, 0.5f);
        ALICE_PROPERTY(float, m_bossGapAdvanceADistance, 1.0f);
        ALICE_PROPERTY(float, m_bossGapAdvanceBDistance, 0.7f);
        ALICE_PROPERTY(float, m_bossGapAdvanceCDistance, 0.8f);
        ALICE_PROPERTY(float, m_bossGapTurnAStartDeg, 65.0f);
        ALICE_PROPERTY(float, m_bossGapTurnBCStartDeg, 35.0f);
        ALICE_PROPERTY(float, m_bossGapTurnBCFollowDeg, 25.0f);
        ALICE_PROPERTY(float, m_bossGapTurnABCStartDeg, 65.0f);
        ALICE_PROPERTY(float, m_bossGapTurnABCFollow1Deg, 35.0f);
        ALICE_PROPERTY(float, m_bossGapTurnABCFollow2Deg, 25.0f);
        ALICE_PROPERTY(float, m_bossGapMinSegmentSec, 0.03f);
        ALICE_PROPERTY(bool, m_debugBossGapWarp, false);
        ALICE_PROPERTY(float, m_bossIdleFacingDamping, 6.0f);
        ALICE_PROPERTY(bool, m_debugAttackMoveTime, false);

        // Attack clip slow-motion was removed; keep commented for reference.
        // ALICE_PROPERTY(std::string, m_attackSlowClipName, "swing");
        // ALICE_PROPERTY(float, m_attackSlowSpeed, 0.7f);

        // Movement facing offset (degrees)
        ALICE_PROPERTY(float, m_rotationOffsetDeg, 180.0f);
        ALICE_PROPERTY(float, m_lockOnTargetYOffset, 1.2f);

        // Fatal attack (front stab) tuning
        ALICE_PROPERTY(float, m_fatalFrontAngleDeg, 90.0f);
        ALICE_PROPERTY(float, m_fatalDistance, 1.1f);
        ALICE_PROPERTY(float, m_fatalApproachSec, 0.25f);
        ALICE_PROPERTY(float, m_fatalHoldSec, 2.0f);
        ALICE_PROPERTY(float, m_fatalDamageScale, 1.5f);
        ALICE_PROPERTY(float, m_groggyAttackStartDelaySec, 2.8f);
        ALICE_PROPERTY(float, m_groggyAttackSfxDelaySec, 0.0f);

        void ForceReset();
        ALICE_FUNC(ForceReset);

    private:
        EntityId ResolveEntity(uint64_t guid) const;
        EntityId ResolveEntityByName(const std::string& name) const;
        EntityId m_playerRageTrailVfxId = InvalidEntityId;
        DirectX::XMFLOAT3 m_playerRageTrailPrevPos{ 0.0f, 0.0f, 0.0f };
        bool m_playerRageTrailPrevPosValid = false;
        float m_playerRageTrailTailOffRemainSec = 0.0f;
        EntityId m_bossAttackTrailVfxId = InvalidEntityId;
        EntityId m_bossAttackTrailVfxId2 = InvalidEntityId;
        float m_bossAttackTrailTailOffRemainSec = 0.0f;
        EntityId m_playerChargeStageVfxId = InvalidEntityId;
        int m_playerChargeStagePrevLevel = 0;
        float m_playerChargeStagePulseRemainSec = 0.0f;
        EntityId m_bossBoostDashVfxId = InvalidEntityId;
        bool m_bossBoostDashVfxForced = false;

        struct AnimConfig
        {
            std::string idleClip;
            std::string moveClip;
            std::string lightAttackClip;
            std::string lightAttackClip1;
            std::string lightAttackClip2;
            std::string lightAttackClip3;
            std::string rageAttackClip;
            std::string heavyAttackClipA;
            std::string heavyAttackClipB;
            std::string dodgeClip;
            std::string chargeEnterClip;
            std::string chargeLoopClip;
            std::string hitClip;
            std::string guardBreakClip;
            std::string fatalAttackClip;
            std::string interactionClip;
            std::string healLoopClip;
            std::string groggyLoopClip;
            std::string guardEnterClip;
            std::string guardLoopClip;
            std::string guardExitClip;
            float guardEnterDurationSec = 0.0f;
            float guardExitDurationSec = 0.0f;
        };

        AnimConfig BuildAnimConfig(EntityId entityId, EntityId playerId, EntityId bossId) const;

        struct SessionState;
        struct SessionStateDeleter
        {
            void operator()(SessionState* ptr) const;
        };
        std::unique_ptr<SessionState, SessionStateDeleter> m_state;
        std::uint64_t m_nextOnCombatResolvedCameraListenerId = 1;
        std::unordered_map<std::uint64_t, FOnCombatResolvedCameraListener> m_onCombatResolvedCameraListeners;
    };
}
