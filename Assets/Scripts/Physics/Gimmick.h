#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include <string>
#include <vector>
#include <cstdint>
#include <random>

#include "Runtime/ECS/Entity.h"
#include <DirectXMath.h>

namespace Alice
{
    class C_CombatSessionComponent;

    // Weapon break/assemble gimmick controller
    class Gimmick : public IScript
    {
        ALICE_BODY(Gimmick);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        bool IsLoopActive() const;
        int ConsumeShardCombinePulseCount();
        bool ConsumeEyeCombinePulse();

        ALICE_PROPERTY(std::string, m_weaponCombinedName, "Weapon(combined)");
        ALICE_PROPERTY(std::string, m_coreName, "W_Core");
        // ALICE_PROPERTY(std::string, m_tendonName, "W_Tendon"); // unused
        ALICE_PROPERTY(std::string, m_eyeName, "W_EYE");
        ALICE_PROPERTY(std::string, m_bindTargetName, "W_Target");

        ALICE_PROPERTY(float, m_breakMaxImpulse, 20.0f); // 파편 폭발 임펄스 최대값
        ALICE_PROPERTY(float, m_breakMaxSpeed, 6.0f); // 폭발 시 속도 상한(impulse/mass 클램프)
        ALICE_PROPERTY(float, m_captureCompleteDelaySec, 2.0f); // 포획 완료 후 다음 단계까지 대기 시간

        // ALICE_PROPERTY(float, m_magnetizeInterval, 1.0f); // unused: old capture interval tuning
        ALICE_PROPERTY(float, m_eyeFloatMoveSpeed, 2.0f); // 눈 이동 속도(부유 연출)
        ALICE_PROPERTY(float, m_orbitAngularSpeed, 2.0f); // 포획 후 공전 각속도
        ALICE_PROPERTY(float, m_orbitAngularBlendDuration, 0.35f); // 공전 속도 블렌딩 시간
        ALICE_PROPERTY(float, m_orbitRadiusScale, 1.0f); // 공전 반경 스케일
        ALICE_PROPERTY(float, m_orbitMinRadius, 0.2f); // 공전 최소 반경

        ALICE_PROPERTY(float, m_assembleInterval, 1.0f); // 파편 조립 시작 간격
        ALICE_PROPERTY(float, m_assembleMoveSpeed, 4.0f); // 조립 이동 기본 속도
        ALICE_PROPERTY(float, m_assembleDistanceAccel, 3.0f); // 조립 이동 거리 가속
        ALICE_PROPERTY(float, m_eyeMoveSpeed, 8.0f); // 눈 조립 이동 기본 속도
        ALICE_PROPERTY(float, m_eyeDistanceAccel, 4.0f); // 눈 조립 이동 거리 가속
        ALICE_PROPERTY(float, m_tendonVisibleDelay, 1.0f); // tendon 투명도 페이드 시간

        ALICE_PROPERTY(float, m_arriveThreshold, 0.02f); // 도착 판정 임계값
        ALICE_PROPERTY(uint32_t, m_ignoreLayersMask, 0u); // 물리 무시 레이어 마스크

        ALICE_PROPERTY(float, m_capturePullBaseSpeed, 6.0f); // 포획 이동 기본 속도
        ALICE_PROPERTY(float, m_capturePullDistanceAccel, 4.0f); // 포획 이동 거리 가속
        ALICE_PROPERTY(float, m_capturePullTargetDuration, 0.0f); // 포획 단계 총 시간(0이면 자동 계산)
        ALICE_PROPERTY(float, m_capturePullMinDuration, 0.08f); // 포획 최소 시간
        ALICE_PROPERTY(float, m_capturePullMaxDuration, 0.6f); // 포획 최대 시간
        ALICE_PROPERTY(float, m_capturePullStartHeightJitter, 1.0f); // 포획 시작 구간 Y 오프셋 랜덤 범위
        ALICE_PROPERTY(float, m_captureOrbitHeightJitter, 1.0f); // 포획/공전 시 파편별 Y 오프셋 랜덤 범위
        ALICE_PROPERTY(std::string, m_enemyName, "Enemy"); // 디버그: Enemy 무기 트레이스 토글 대상
        ALICE_PROPERTY(std::string, m_guardBreakOwnerName, "Enemy"); // 가드브레이크 감지 대상
        ALICE_PROPERTY(float, m_breakScatterDurationSec, 2.0f); // 파괴 후 흩뿌려진 상태 유지 시간
        ALICE_PROPERTY(float, m_shardAssembleFinishDelaySec, 0.5f); // 파편 조립 완료 후 눈 조립 전 대기
        ALICE_PROPERTY(bool, m_enableShardTrailVfx, false); // 파편 이동 중 트레일 VFX 사용
        ALICE_PROPERTY(std::string, m_shardTrailEffectPath, "Assets/VFX/UnityExport/(Opt)Effect_06_PortalEffect_2__Smoke_1_2/effect.json");
        ALICE_PROPERTY(float, m_shardTrailEmitMinSpeed, 0.15f); // 이 속도 이상일 때만 emission
        ALICE_PROPERTY(float, m_shardTrailSpawnRateScale, 1.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_shardTrailLocalOffset, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_shardTrailLocalRotation, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_shardTrailLocalScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        // ALICE_PROPERTY(std::string, m_eyeMoveTrailEffectPath, "Assets/VFX/UnityExport/(Opt)Effect_06_PortalEffect_2__Smoke_1_2/effect.json"); // unused
        // ALICE_PROPERTY(float, m_eyeMoveTrailSizeScale, 0.025f); // unused
        // ALICE_PROPERTY(float, m_eyeMoveTrailIntensityScale, 0.0f); // unused
        // ALICE_PROPERTY(DirectX::XMFLOAT3, m_eyeMoveTrailColorTint, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)); // unused
        // ALICE_PROPERTY(DirectX::XMFLOAT3, m_eyeMoveTrailLocalScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)); // unused
        ALICE_PROPERTY(std::string, m_eyeDustEffectPath, "Assets/VFX/UnityExport/Dust_3_41ffd4a3/effect.json");
        ALICE_PROPERTY(float, m_eyeDustRiseStartHeight, 1.0f); // 눈이 이 높이 이상 떠오르면 알파 상승 시작
        ALICE_PROPERTY(float, m_eyeDustFadeInSpeed, 0.9f); // 상승 시작 후 첫 파편 발사 전 알파 증가 속도
        ALICE_PROPERTY(std::string, m_combatSessionName, "SceneManager");
        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_parryShieldVfxName, "LightShield");
        ALICE_PROPERTY(float, m_parryShieldTimeScale, 1.5f);
        ALICE_PROPERTY(float, m_parryShieldDetachDurationSec, 1.0f);

    private:
        enum class Phase
        {
            Normal = 0,
            Break,
            Magnetize,
            AssembleShards,
            AssembleEye,
            Restore
        };

        struct ShardState
        {
            EntityId id = InvalidEntityId;
            std::string name;
            bool captured = false;
            bool pulling = false;
            bool assembling = false;
            bool assembled = false;
            float orbitAngle = 0.0f;
            DirectX::XMFLOAT3 orbitAxis{ 0.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT3 orbitBaseDir{ 1.0f, 0.0f, 0.0f };
            float orbitRadius = 0.0f;
            DirectX::XMFLOAT3 pullStartPos{ 0.0f, 0.0f, 0.0f };
            float pullTimer = 0.0f;
            float pullDuration = 0.35f;
            float pullSpeed = 0.0f;
            float orbitAngularSpeed = 0.0f;
            float orbitAngularStartSpeed = 0.0f;
            float orbitBlendTimer = 0.0f;
            bool orbitBlending = false;
            float orbitHeightOffset = 0.0f;
            float pullStartHeightOffset = 0.0f;
            float assembleStartDistance = 0.0f;
            DirectX::XMFLOAT3 prevPos{ 0.0f, 0.0f, 0.0f };
            bool prevPosValid = false;
            EntityId trailVfxId = InvalidEntityId;
        };

        EntityId m_weaponCombined = InvalidEntityId;
        EntityId m_core = InvalidEntityId;
        // EntityId m_tendon = InvalidEntityId; // unused
        EntityId m_eye = InvalidEntityId;
        EntityId m_bindTarget = InvalidEntityId;

        std::vector<ShardState> m_shards;
        std::vector<size_t> m_assembleOrder;

        Phase m_phase = Phase::Normal;
        bool m_initialized = false;

        float m_phaseTime = 0.0f;
        // float m_captureTimer = 0.0f; // unused
        float m_captureCompleteTimer = 0.0f;
        float m_assembleTimer = 0.0f;
        size_t m_nextAssembleIndex = 0;

        bool m_pendingBreakImpulse = false;
        int m_pendingShardCombinePulseCount = 0;
        bool m_pendingEyeCombinePulse = false;
        bool m_eyeArrived = false;
        float m_tendonTimer = 0.0f;
        bool m_tendonFading = false;
        bool m_magnetizeInitialized = false;
        bool m_eyeFloatAnchorValid = false;
        DirectX::XMFLOAT3 m_eyeFloatAnchor{ 0.0f, 0.0f, 0.0f };
        bool m_enemyTraceForced = false;
        bool m_guardBreakLatched = false;
        float m_assembleFinishTimer = 0.0f;
        EntityId m_eyeTrailVfx = InvalidEntityId;
        DirectX::XMFLOAT3 m_eyeTrailPrevPos{ 0.0f, 0.0f, 0.0f };
        bool m_eyeTrailPrevPosValid = false;
        EntityId m_eyeMoveTrailVfx = InvalidEntityId;
        DirectX::XMFLOAT3 m_eyeMoveTrailPrevPos{ 0.0f, 0.0f, 0.0f };
        bool m_eyeMoveTrailPrevPosValid = false;
        float m_eyeDustAlpha = 0.0f;
        bool m_eyeDustRiseTriggered = false;
        bool m_eyeDustFirstShardLaunched = false;
        EntityId m_parryShieldVfx = InvalidEntityId;
        EntityId m_parryShieldFreezeAnchor = InvalidEntityId;
        float m_parryShieldDetachTimerSec = 0.0f;
        bool m_parryShieldDetached = false;
        std::uint64_t m_prevPlayerParrySuccessCount = 0;
        bool m_hasSeenPlayerParrySuccessCount = false;
        bool m_warnedMissingCombatSession = false;
        bool m_warnedMissingPlayerEntity = false;
        bool m_warnedMissingParryShield = false;
        bool m_warnedMissingParryShieldVfx = false;

        std::mt19937 m_rng;

        void FindEntities();
        C_CombatSessionComponent* FindCombatSession();
        EntityId ResolvePlayerEntity(bool logWarnings);
        EntityId ResolveParryShieldVfxEntity(bool logWarnings);
        void TriggerParryShieldOneShot();
        void RestoreParryShieldToPlayer(bool hideAfterRestore);
        void UpdateParryShieldParryTrigger(float dt);

        void AdvancePhase();
        void EnterPhase(Phase phase);

        void DebugEnableEnemyWeaponTrace();
        bool IsGuardBroken() const;
        bool AreShardsCaptured() const;
        bool AreShardsAssembled() const;

        void UpdateMagnetize(float dt);
        void UpdateAssembleShards(float dt);
        void UpdateAssembleEye(float dt);

        void UpdateEyeFloat(float dt);
        void UpdateOrbitingShards(float dt);
        void SetupShardTrailVfx();
        void UpdateShardTrailEmission(float dt);
        void ResetShardTrailPlayback();

        void ApplyBreakImpulse();
        bool CanApplyBreakImpulse() const;

        void ResetShardState();

        void SetEnabled(EntityId id, bool enabled);
        void SetVisible(EntityId id, bool visible);
        void SetMaterialAlpha(EntityId id, float alpha);
        void SetMaterialTransparent(EntityId id, bool transparent);
        void SetColliderTrigger(EntityId id, bool trigger);
        void SetIgnoreLayers(EntityId id, uint32_t mask);
        void AddIgnoreSelfLayer(EntityId id);
        void SetRigidBodyKinematic(EntityId id, bool kinematic, bool gravityEnabled);
        void ClearRigidBodyVelocity(EntityId id);
        void TeleportRigidBody(EntityId id);

        bool MoveTowards(EntityId id, const DirectX::XMFLOAT3& targetPos,
                         const DirectX::XMFLOAT3& targetRot, const DirectX::XMFLOAT3& targetScale,
                         float speed, float dt);

        DirectX::XMFLOAT3 GetEyeWorldPosition() const;
        bool GetBindTargetWorldPose(DirectX::XMFLOAT3& outPos,
                                    DirectX::XMFLOAT3& outRot,
                                    DirectX::XMFLOAT3& outScale) const;
    };
}
