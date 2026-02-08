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
        ALICE_PROPERTY(std::string, weaponBasisEntityName, "W_Core");
        ALICE_PROPERTY(std::string, weaponSocketOwnerName, "EGO_Core");
        ALICE_PROPERTY(std::string, weaponSocketName, "CombinedSpot");

        // Prefab paths
        ALICE_PROPERTY(std::string, slashPrefabPath, "Assets/Prefabs/(01)White_Attack_SlashC.prefab");
        ALICE_PROPERTY(std::string, hitPrefabPath, "Assets/Prefabs/(01)White_HIT.prefab");

        // Pool settings
        ALICE_PROPERTY(int, slashPoolSize, 8);
        ALICE_PROPERTY(int, hitPoolSize, 8);
        ALICE_PROPERTY(bool, prewarm, true);

        // Lifetime (sec)
        ALICE_PROPERTY(float, slashLifeTimeSec, 0.8f);
        ALICE_PROPERTY(float, hitLifeTimeSec, 0.6f);
        ALICE_PROPERTY(float, slashSpawnPhase, 0.16666667f);
        ALICE_PROPERTY(float, slashAnchorPlayerYOffset, 0.6f);
        ALICE_PROPERTY(float, slashAnchorPlayerForwardOffset, 0.5f);

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
        };

        void ResolveSessionAndActors(bool logWarnings);
        void TryBindResolveDelegate();
        void UnbindResolveDelegateSafe();
        void UpdatePathCacheAndPools();
        void UpdateSocketTracking();
        void UpdateActive(float deltaTime);
        void DeactivateAllActive();
        bool ProcessSlashFromTraceSignals(bool allowSpawn);
        void CollectPlayerTraceEntities(std::vector<EntityId>& out) const;

        void SpawnSlashFromAttackWindow();
        void SpawnHitFromResolve(const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitNormal);
        void OnCombatResolve(EntityId victimId,
                             EntityId attackerId,
                             std::uint8_t resolveResult,
                             float damage,
                             const DirectX::XMFLOAT3& hitPos,
                             const DirectX::XMFLOAT3& hitNormal);

        void PrewarmSlot(int slot);
        void ClearSlot(int slot);
        EntityId Acquire(int slot);
        void Release(int slot, EntityId id);
        EntityId CreateInstance(int slot);
        EntityId EnsurePoolRoot();
        void ClearPoolRoot();

        bool TryGetTracePointWorld(DirectX::XMFLOAT3& outPos);
        bool TryGetTracePoseWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward, DirectX::XMFLOAT3& outUp);
        bool TryGetWeaponBasisPose(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot);
        bool TryGetWeaponBasisWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward);
        bool TryGetSocketWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward);
        DirectX::XMFLOAT3 GetPlayerPosition();
        DirectX::XMFLOAT3 GetPlayerForward();
        float ResolvePlayerAttackDurationSec() const;
        void ApplySpawnTransform(int slot,
                                 EntityId id,
                                 const DirectX::XMFLOAT3& anchorPos,
                                 const DirectX::XMFLOAT3& forward,
                                 const DirectX::XMFLOAT3& upHint);
        void ApplySpawnTransformFromRotation(int slot,
                                             EntityId id,
                                             const DirectX::XMFLOAT3& anchorPos,
                                             const DirectX::XMFLOAT4& worldRot);

        void SetEntityActiveRecursive(World& world, EntityId id, bool active, bool triggerOneShot) const;

        std::string GetPrefabPath(int slot) const;
        int GetPoolSizeSafe(int slot) const;
        float GetLifeTimeSafe(int slot) const;
        float GetScaleMulSafe(int slot) const;
        DirectX::XMFLOAT3 GetOffsetLocal(int slot) const;
        DirectX::XMFLOAT3 GetRotationOffsetDeg(int slot) const;

        enum : int
        {
            SlashSlot = 0,
            HitSlot = 1,
            SlotCount = 2
        };

        C_CombatSessionComponent* m_session = nullptr;
        EntityId m_playerId = InvalidEntityId;
        EntityId m_bossId = InvalidEntityId;
        EntityId m_poolRoot = InvalidEntityId;
        bool m_resolveBound = false;

        std::array<std::vector<EntityId>, SlotCount> m_pool{};
        std::array<std::vector<ActiveInstance>, SlotCount> m_active{};
        std::array<std::string, SlotCount> m_cachedPaths{};
        std::array<std::unordered_map<EntityId, DirectX::XMFLOAT3>, SlotCount> m_cachedBaseScales{};
        std::unordered_map<EntityId, std::uint32_t> m_traceAttackInstanceSeen{};

        bool m_prevPlayerHitActive = false;
        bool m_prevIsLightAttack = false;
        bool m_hasPrevSocketPos = false;
        DirectX::XMFLOAT3 m_prevSocketPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_currSocketPos{ 0.0f, 0.0f, 0.0f };
        bool m_attackTraceActive = false;
        int m_attackTraceComboIndex = -1;
        bool m_slashSpawnedThisAttack = false;
        float m_attackElapsedSec = 0.0f;
        float m_attackDurationSec = 0.0f;
        bool m_hasAttackStartPos = false;
        DirectX::XMFLOAT3 m_attackStartPos{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_attackStartUp{ 0.0f, 1.0f, 0.0f };

        bool m_warnedMissingSession = false;
        bool m_warnedMissingPlayer = false;
        bool m_warnedMissingBoss = false;
        bool m_warnedMissingTracePoint = false;
        bool m_warnedMissingWeaponBasis = false;
        bool m_warnedMissingSocketOwner = false;
        bool m_warnedMissingSocket = false;
        bool m_warnedMissingSlashPrefabPath = false;
        bool m_warnedMissingHitPrefabPath = false;
        bool m_warnedSlashInstantiateFailed = false;
        bool m_warnedHitInstantiateFailed = false;
        bool m_warnedSlashPoolCapReached = false;
        bool m_warnedHitPoolCapReached = false;
    };
}
