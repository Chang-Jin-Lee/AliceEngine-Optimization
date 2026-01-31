#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include <string>
#include <vector>
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    // 프리팹을 생성/삭제하는 기믹 스크립트
    class StressRigidbodySpawner : public IScript
    {
        ALICE_BODY(StressRigidbodySpawner);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 (에디터에서 수정 가능) ---
        // 프리팹 경로 (Assets/Prefabs/... 형태)
        ALICE_PROPERTY(std::string, m_prefabPath, "");

    private:
        // 생성된 프리팹 엔티티 ID들을 저장
        std::vector<EntityId> m_spawnedEntities;
        
        // 프리팹 생성 함수들
        void SpawnPrefab(int count);
        void ClearSpawnedPrefabs();
    };
}
