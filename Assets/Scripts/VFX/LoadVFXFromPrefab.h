#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

#include <array>
#include <string>
#include <vector>

namespace Alice
{
    // 프리팹을 키 입력(1~9)으로 스폰하는 VFX 테스트 스크립트
    class LoadVFXFromPrefab : public IScript
    {
        ALICE_BODY(LoadVFXFromPrefab);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

        // --- Prefab Path Slots (1~9)
        ALICE_PROPERTY(std::string, m_prefabPath1, "");
        ALICE_PROPERTY(std::string, m_prefabPath2, "");
        ALICE_PROPERTY(std::string, m_prefabPath3, "");
        ALICE_PROPERTY(std::string, m_prefabPath4, "");
        ALICE_PROPERTY(std::string, m_prefabPath5, "");
        ALICE_PROPERTY(std::string, m_prefabPath6, "");
        ALICE_PROPERTY(std::string, m_prefabPath7, "");
        ALICE_PROPERTY(std::string, m_prefabPath8, "");
        ALICE_PROPERTY(std::string, m_prefabPath9, "");

        // 부모 지정 (드래그 앤 드롭으로 엔티티 이름 입력 가능)
        ALICE_PROPERTY(std::string, m_parentTargetName, "");

        // 반복 스폰 모드 (true면 키 입력 시 반복 시작/중지)
        ALICE_PROPERTY(bool, m_loop, false);
        ALICE_PROPERTY(float, m_loopInterval, 1.0f);

        // 생존 시간(초). 0 이하면 수동 토글(키 재입력 시 비활성화)
        ALICE_PROPERTY(float, m_lifeTime, 2.0f);

        // 풀 크기 (프리워밍 개수)
        ALICE_PROPERTY(int, m_poolSize, 3);
        ALICE_PROPERTY(bool, m_prewarm, true);

    private:
        struct ActiveInstance
        {
            EntityId id = InvalidEntityId;
            float remaining = 0.0f;
        };

        std::array<std::vector<EntityId>, 9> m_pool{};
        std::array<std::vector<ActiveInstance>, 9> m_active{};
        std::array<float, 9> m_loopTimers{};
        std::array<bool, 9> m_looping{};
        std::array<std::string, 9> m_cachedPaths{};

        void UpdatePathCacheAndPools();
        void HandleInput();
        void ToggleLoop(int slot);
        void SpawnOnce(int slot);
        void UpdateLooping(float deltaTime);
        void UpdateActive(float deltaTime);

        void PrewarmSlot(int slot);
        void ClearSlot(int slot);
        void DeactivateAllActive(int slot);

        EntityId Acquire(int slot);
        void Release(int slot, EntityId id);
        EntityId CreateInstance(int slot);

        EntityId ResolveParent() const;
        void SetEntityActiveRecursive(World& world, EntityId id, bool active);

        const std::string& GetPrefabPath(int slot) const;
        int GetPoolSizeSafe() const;
    };
}
