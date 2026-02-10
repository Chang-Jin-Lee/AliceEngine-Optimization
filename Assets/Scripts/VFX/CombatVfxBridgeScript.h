#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class C_CombatSessionComponent;
    class World;

    class CombatVfxBridgeScript : public IScript
    {
        ALICE_BODY(CombatVfxBridgeScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

        // Entity references
        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, weaponTraceEntityName, "W_Target");
        ALICE_PROPERTY(std::string, shockWaveEntityName, "ShockWave");
        ALICE_PROPERTY(std::string, shockWavePrefabPath, "Assets/Prefabs/(00)ShockWave.prefab");
        ALICE_PROPERTY(std::string, shockBlastPrefabPath, "Assets/Prefabs/(00)WhiteShockBlast.prefab");
        ALICE_PROPERTY(std::string, guardShockPointEntityName, "PlayerEffectPoint");
        ALICE_PROPERTY(std::string, bossEffectPointEntityName, "BossEffectPoint");

        // Prefab paths
        ALICE_PROPERTY(std::string, slashPrefabPath, "Assets/Prefabs/(01)White_Attack_SlashC.prefab");
        ALICE_PROPERTY(bool, useAttackDriverSlotSignals, true);
        ALICE_PROPERTY(bool, useSlashStepPrefabs, false);
        ALICE_PROPERTY(std::string, slashStepPrefabPath1, "");
        ALICE_PROPERTY(std::string, slashStepPrefabPath2, "");
        ALICE_PROPERTY(std::string, slashStepPrefabPath3, "");
        ALICE_PROPERTY(std::string, slashStepPrefabPath4, "");
        ALICE_PROPERTY(std::string, slashStepPrefabPath5, "");
        ALICE_PROPERTY(std::string, slashStepPrefabPath6, "");
        ALICE_PROPERTY(std::string, hitPrefabPath, "Assets/Prefabs/(01)White_HIT.prefab");
        ALICE_PROPERTY(bool, useHitSlotPrefabs, false);
        ALICE_PROPERTY(std::string, hitSlotPrefabPath1, "");
        ALICE_PROPERTY(std::string, hitSlotPrefabPath2, "");
        ALICE_PROPERTY(std::string, hitSlotPrefabPath3, "");
        ALICE_PROPERTY(std::string, hitSlotPrefabPath4, "");
        ALICE_PROPERTY(std::string, hitSlotPrefabPath5, "");
        ALICE_PROPERTY(std::string, hitSlotPrefabPath6, "");
        // Shared yellow spark for player->boss hit and player parry success.
        ALICE_PROPERTY(std::string, hitOverlayPrefabPath, "");
        // White one-shot on successful guard.
        ALICE_PROPERTY(std::string, guardOverlayPrefabPath, "Assets/Prefabs/FX_Guard_White_OneShot.prefab");
        // Gray one-shot when player is hit by boss.
        ALICE_PROPERTY(std::string, damageOverlayPrefabPath, "Assets/Prefabs/FX_Hit_Gray_OneShot.prefab");
        // One-shot ring on successful guard.
        ALICE_PROPERTY(std::string, guardRingOverlayPrefabPath, "Assets/Prefabs/(05)OrangeRing.prefab");
        // Additional one-shot ring when guard breaks.
        ALICE_PROPERTY(std::string, guardBreakRingOverlayPrefabPath, "Assets/Prefabs/(05)OrangeDoubleRing.prefab");
        // One-shot ring on successful parry.
        ALICE_PROPERTY(std::string, parryRingOverlayPrefabPath, "Assets/Prefabs/(05)YellowRing.prefab");
        // One-shot ring when boss enters groggy.
        ALICE_PROPERTY(std::string, bossGroggyRingOverlayPrefabPath, "Assets/Prefabs/(05)RedRing.prefab");

        // Pool settings
        ALICE_PROPERTY(int, slashPoolSize, 3);
        ALICE_PROPERTY(int, hitPoolSize, 3);
        ALICE_PROPERTY(int, shockWavePoolSize, 6);
        ALICE_PROPERTY(int, shockBlastPoolSize, 6);
        ALICE_PROPERTY(bool, prewarm, true);

        // Lifetime (sec)
        ALICE_PROPERTY(float, slashLifeTimeSec, 0.8f);
        ALICE_PROPERTY(float, hitLifeTimeSec, 0.6f);
        ALICE_PROPERTY(float, shockWaveLifeTimeSec, 1.0f);
        ALICE_PROPERTY(float, shockBlastLifeTimeSec, 1.0f);
        ALICE_PROPERTY(float, bossGroggyRingOverlayLifeTimeSec, 6.0f);
        ALICE_PROPERTY(bool, enableHeavyFadeOut, true);
        ALICE_PROPERTY(int, heavyAttackSlotIndex, 4);
        ALICE_PROPERTY(float, heavyFadeDurationSec, 1.0f);
        ALICE_PROPERTY(float, slashSpawnPhase, 0.16666667f);
        ALICE_PROPERTY(float, slashAnchorPlayerYOffset, 0.0f);
        ALICE_PROPERTY(float, slashAnchorPlayerForwardOffset, 0.0f);
        ALICE_PROPERTY(bool, useSlashSlotTransformTuning, true);
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal1, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal2, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal3, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal4, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal5, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotAnchorOffsetLocal6, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg1, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg2, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg3, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg4, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg5, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashSlotRotationOffsetDeg6, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(float, slashSlotScaleMul1, 1.0f);
        ALICE_PROPERTY(float, slashSlotScaleMul2, 1.0f);
        ALICE_PROPERTY(float, slashSlotScaleMul3, 1.0f);
        ALICE_PROPERTY(float, slashSlotScaleMul4, 1.0f);
        ALICE_PROPERTY(float, slashSlotScaleMul5, 1.0f);
        ALICE_PROPERTY(float, slashSlotScaleMul6, 1.0f);
        ALICE_PROPERTY(bool, useHitSlotLifeTimes, true);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec1, 0.0f);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec2, 0.0f);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec3, 0.0f);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec4, 0.0f);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec5, 0.0f);
        ALICE_PROPERTY(float, hitSlotLifeTimeSec6, 0.0f);

        // Spawn tuning
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashOffsetLocal, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, slashRotationOffsetDeg, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(float, slashScaleMul, 1.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, hitOffsetLocal, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, hitRotationOffsetDeg, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(float, hitScaleMul, 1.0f);
        ALICE_PROPERTY(bool, organizePoolUnderRoot, true);
        ALICE_PROPERTY(std::string, poolRootName, "__CombatVfxPool");

    private:
        struct ActiveInstance
        {
            EntityId id = InvalidEntityId;
            float remainingSec = 0.0f;
            float totalSec = 0.0f;
            bool fadeOut = false;
            EntityId freezeAnchor = InvalidEntityId;
        };

        struct ComputeOneShotState
        {
            float baseSpawnRate = 0.0f;
            bool baseLoop = false;
            bool baseCaptured = false;
            bool armed = false;
            bool oneFrameConsumed = false;
            float elapsedSec = 0.0f;
            float emissionDurationSec = 0.0f;
        };

        void ResolveSessionAndActors(bool logWarnings);
        void TryBindResolveDelegate();
        void UnbindResolveDelegateSafe();
        bool IsSlashWindowActive();
        void DetachActiveSlashInstances();
        void FreezeSlashInstanceToWorldAnchor(ActiveInstance& inst);
        void UpdatePathCacheAndPools();
        void UpdateSocketTracking();
        void UpdateActive(float deltaTime);
        void UpdateComputeOneShotEmission(float deltaTime);
        void DeactivateAllActive();
        bool ProcessSlashFromTraceSignals(bool allowSpawn);
        void CollectPlayerTraceEntities(std::vector<EntityId>& out) const;

        void SpawnSlashFromAttackWindow(int attackSlotIndex = -1);
        void SpawnHitFromResolve(const DirectX::XMFLOAT3& hitPos,
                                 EntityId victimId,
                                 int attackSlotIndex = -1);
        enum class OverlaySpawnKind : std::uint8_t
        {
            Spark = 0,
            Guard = 1,
            Damage = 2,
            GuardRing = 3,
            GuardBreakRing = 4,
            ParryRing = 5,
            BossGroggyRing = 6
        };
        void SpawnHitOverlayAtPosition(const DirectX::XMFLOAT3& spawnPos,
                                       OverlaySpawnKind kind,
                                       int attackSlotIndex,
                                       bool applyRageTint);
        EntityId ResolveShockWaveEntity(bool logWarnings);
        EntityId ResolveGuardShockPointEntity(bool logWarnings);
        EntityId ResolveBossEffectPointEntity(bool logWarnings);
        bool TryGetGuardShockWavePosition(DirectX::XMFLOAT3& outPos);
        bool TryGetBossEffectPointPosition(DirectX::XMFLOAT3& outPos);
        void TriggerShockWaveAtPosition(const DirectX::XMFLOAT3& spawnPos,
                                        bool spawnShockWave = true,
                                        bool spawnShockBlast = true);
        void OnCombatResolve(EntityId victimId,
                             EntityId attackerId,
                             std::uint8_t resolveResult,
                             float damage,
                             const DirectX::XMFLOAT3& hitPos);

        void PrewarmSlot(int slot);
        void ClearSlot(int slot);
        EntityId Acquire(int slot, bool triggerOneShot = true);
        void Release(int slot, EntityId id);
        EntityId CreateInstance(int slot);
        EntityId EnsurePoolRoot();
        void ClearPoolRoot();

        bool TryGetTracePointWorld(DirectX::XMFLOAT3& outPos);
        bool TryGetTracePoseWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward, DirectX::XMFLOAT3& outUp);
        bool TryGetAttackDirectionWorld(DirectX::XMFLOAT3& outForward);
        int ResolveSlashSlotForAttackSlotIndex(int attackSlotIndex) const;
        int ResolveHitSlotForAttackSlotIndex(int attackSlotIndex) const;
        int ResolveCurrentAttackSlotIndexFromDriverMask() const;
        DirectX::XMFLOAT3 GetPlayerPosition();
        DirectX::XMFLOAT3 GetPlayerForward();
        float ResolvePlayerAttackDurationSec() const;
        void ApplySpawnTransformTuned(int slot,
                                      EntityId id,
                                      const DirectX::XMFLOAT3& anchorPos,
                                      const DirectX::XMFLOAT3& forward,
                                      const DirectX::XMFLOAT3& upHint,
                                      const DirectX::XMFLOAT3& localOffset,
                                      const DirectX::XMFLOAT3& rotationOffsetDeg,
                                      float scaleMul);
        void ApplySpawnTransform(int slot,
                                 EntityId id,
                                 const DirectX::XMFLOAT3& anchorPos,
                                 const DirectX::XMFLOAT3& forward,
                                 const DirectX::XMFLOAT3& upHint);

        void SetEntityActiveRecursive(World& world, EntityId id, bool active, bool triggerOneShot);
        void SetEntityColorTintRecursive(World& world, EntityId id, const DirectX::XMFLOAT3& tint) const;
        void SetEntityLoopModeRecursive(World& world, EntityId id, bool loopEnabled) const;
        void ApplyEntityAlphaFadeRecursive(World& world, int slot, EntityId id, float alphaRatio) const;
        void CacheEntityAlphaRecursive(World& world, int slot, EntityId id);
        void RestoreEntityAlphaRecursive(World& world, int slot, EntityId id) const;
        void EraseCachedAlphaRecursive(World& world, int slot, EntityId id);
        float ComputeOneShotKeepAliveSec(EntityId rootId);

        std::string GetPrefabPath(int slot) const;
        int GetPoolSizeSafe(int slot) const;
        float GetLifeTimeSafe(int slot) const;
        bool ShouldApplyRageTint(int attackSlotIndex) const;
        bool IsHeavyAttackSlotIndex(int attackSlotIndex) const;
        float ResolveSpawnLifetimeSec(int slot, int attackSlotIndex) const;
        float GetScaleMulSafe(int slot) const;
        DirectX::XMFLOAT3 GetOffsetLocal(int slot) const;
        DirectX::XMFLOAT3 GetRotationOffsetDeg(int slot) const;
        DirectX::XMFLOAT3 GetSlashSlotAnchorOffsetLocal(int attackSlotIndex) const;
        DirectX::XMFLOAT3 GetSlashSlotRotationOffsetDeg(int attackSlotIndex) const;
        float GetSlashSlotScaleMul(int attackSlotIndex) const;

        enum : int
        {
            SlashDefaultSlot = 0,
            SlashStep1Slot = 1,
            SlashStep2Slot = 2,
            SlashStep3Slot = 3,
            SlashStep4Slot = 4,
            SlashStep5Slot = 5,
            SlashStep6Slot = 6,
            HitDefaultSlot = 7,
            HitSlot = HitDefaultSlot,
            HitStep1Slot = 8,
            HitStep2Slot = 9,
            HitStep3Slot = 10,
            HitStep4Slot = 11,
            HitStep5Slot = 12,
            HitStep6Slot = 13,
            HitOverlaySparkSlot = 14,
            HitOverlayGuardSlot = 15,
            HitOverlayDamageSlot = 16,
            HitOverlayGuardRingSlot = 17,
            HitOverlayGuardBreakRingSlot = 18,
            HitOverlayParryRingSlot = 19,
            HitOverlayBossGroggyRingSlot = 20,
            ShockWaveSlot = 21,
            ShockBlastSlot = 22,
            SlotCount = 23,
            SlashStepSlotCount = 6
        };

        C_CombatSessionComponent* m_session = nullptr;
        EntityId m_playerId = InvalidEntityId;
        EntityId m_bossId = InvalidEntityId;
        EntityId m_shockWaveId = InvalidEntityId;
        EntityId m_guardShockPointId = InvalidEntityId;
        EntityId m_bossEffectPointId = InvalidEntityId;
        EntityId m_poolRoot = InvalidEntityId;
        bool m_resolveBound = false;

        std::array<std::vector<EntityId>, SlotCount> m_pool{};
        std::array<std::vector<ActiveInstance>, SlotCount> m_active{};
        std::array<std::string, SlotCount> m_cachedPaths{};
        std::array<std::unordered_map<EntityId, DirectX::XMFLOAT3>, SlotCount> m_cachedBaseScales{};
        std::array<std::unordered_map<EntityId, float>, SlotCount> m_cachedBaseAlphas{};
        std::unordered_map<EntityId, ComputeOneShotState> m_computeOneShotStates{};
        std::unordered_map<EntityId, std::uint32_t> m_traceAttackInstanceSeen{};
        std::uint32_t m_prevAttackTraceMask = 0u;
        int m_lastAttackSlotIndex = -1;
        bool m_prevSlashWindowActive = false;
        bool m_prevBossGroggy = false;

        bool m_prevPlayerHitActive = false;
        bool m_prevIsLightAttack = false;
        bool m_hasPrevSocketPos = false;
        DirectX::XMFLOAT3 m_prevSocketPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_currSocketPos{ 0.0f, 0.0f, 0.0f };
        bool m_attackTraceActive = false;
        int m_attackTraceComboIndex = -1;
        int m_slashSignalOrdinalInAttack = 0;
        bool m_slashSpawnedThisAttack = false;
        float m_attackElapsedSec = 0.0f;
        float m_attackDurationSec = 0.0f;
        bool m_hasAttackStartPos = false;
        DirectX::XMFLOAT3 m_attackStartPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_attackStartUp{ 0.0f, 1.0f, 0.0f };

        bool m_warnedMissingSession = false;
        bool m_warnedMissingPlayer = false;
        bool m_warnedMissingBoss = false;
        bool m_warnedMissingShockWave = false;
        bool m_warnedMissingGuardShockPoint = false;
        bool m_warnedMissingBossEffectPoint = false;
        bool m_warnedMissingTracePoint = false;
        bool m_warnedMissingSlashPrefabPath = false;
        bool m_warnedMissingHitPrefabPath = false;
        bool m_warnedSlashInstantiateFailed = false;
        bool m_warnedHitInstantiateFailed = false;
        bool m_warnedSlashPoolCapReached = false;
        bool m_warnedHitPoolCapReached = false;
    };
}
