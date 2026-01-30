#include "StressRigidbodySpawner.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Core/Prefab.h"
#include "Core/Input.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent.h"
#include <cmath>
#include <filesystem>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(StressRigidbodySpawner);

    void StressRigidbodySpawner::Start()
    {
        // 초기화 로직
        m_spawnedEntities.clear();
        ALICE_LOG_INFO("[StressRigidbodySpawner] Started. Prefab path: %s", m_prefabPath.c_str());
    }

    void StressRigidbodySpawner::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        auto* transform = GetTransform();
        if (!transform)
            return;

        // 숫자 키 입력 감지
        if (input->GetKeyDown(KeyCode::Alpha1))
        {
            SpawnPrefab(1);
        }
        else if (input->GetKeyDown(KeyCode::Alpha2))
        {
            SpawnPrefab(10);
        }
        else if (input->GetKeyDown(KeyCode::Alpha3))
        {
            SpawnPrefab(100);
        }
        else if (input->GetKeyDown(KeyCode::Alpha4))
        {
            SpawnPrefab(1000);
        }
        else if (input->GetKeyDown(KeyCode::Alpha0))
        {
            ClearSpawnedPrefabs();
        }
    }

    void StressRigidbodySpawner::SpawnPrefab(int count)
    {
        if (m_prefabPath.empty())
        {
            ALICE_LOG_WARN("[StressRigidbodySpawner] Prefab path is empty!");
            return;
        }

        auto* world = GetWorld();
        if (!world)
            return;

        auto* transform = GetTransform();
        if (!transform)
            return;

        // 기존 프리팹 삭제
        ClearSpawnedPrefabs();

        const float baseY = transform->position.y;
        const float spacing = 5.0f; // 프리팹 간 간격

        if (count == 1)
        {
            // 1개: 현재 Transform의 Y 좌표 기준
            std::filesystem::path prefabPath = m_prefabPath;
            EntityId entity = Prefab::InstantiateFromFile(*world, prefabPath);
            if (entity != InvalidEntityId)
            {
                if (auto* t = world->GetComponent<TransformComponent>(entity))
                {
                    t->position.x = transform->position.x;
                    t->position.y = baseY;
                    t->position.z = transform->position.z;
                }
                m_spawnedEntities.push_back(entity);
            }
        }
        else if (count == 10)
        {
            // 10개: Y 동일, X Z 간격 (3x3 + 1개, 대략 정사각형 배치)
            int gridSize = static_cast<int>(std::ceil(std::sqrt(10.0f))); // 4
            int spawned = 0;
            for (int z = 0; z < gridSize && spawned < count; ++z)
            {
                for (int x = 0; x < gridSize && spawned < count; ++x)
                {
                    std::filesystem::path prefabPath = m_prefabPath;
                    EntityId entity = Prefab::InstantiateFromFile(*world, prefabPath);
                    if (entity != InvalidEntityId)
                    {
                        if (auto* t = world->GetComponent<TransformComponent>(entity))
                        {
                            t->position.x = transform->position.x + (x - gridSize / 2.0f) * spacing;
                            t->position.y = baseY;
                            t->position.z = transform->position.z + (z - gridSize / 2.0f) * spacing;
                        }
                        m_spawnedEntities.push_back(entity);
                        ++spawned;
                    }
                }
            }
        }
        else if (count == 100)
        {
            // 100개: Y 동일, X Z 간격 (10x10 그리드)
            int gridSize = 10;
            for (int z = 0; z < gridSize; ++z)
            {
                for (int x = 0; x < gridSize; ++x)
                {
                    std::filesystem::path prefabPath = m_prefabPath;
                    EntityId entity = Prefab::InstantiateFromFile(*world, prefabPath);
                    if (entity != InvalidEntityId)
                    {
                        if (auto* t = world->GetComponent<TransformComponent>(entity))
                        {
                            t->position.x = transform->position.x + (x - gridSize / 2.0f) * spacing;
                            t->position.y = baseY;
                            t->position.z = transform->position.z + (z - gridSize / 2.0f) * spacing;
                        }
                        m_spawnedEntities.push_back(entity);
                    }
                }
            }
        }
        else if (count == 1000)
        {
            // 1000개: 100개씩 10줄 (Y 좌표 증가)
            int perRow = 100;
            int rows = 10;
            int gridSize = 10; // 10x10 = 100개씩
            
            for (int row = 0; row < rows; ++row)
            {
                float rowY = baseY + row * spacing; // Y 좌표 증가
                for (int z = 0; z < gridSize; ++z)
                {
                    for (int x = 0; x < gridSize; ++x)
                    {
                        std::filesystem::path prefabPath = m_prefabPath;
                        EntityId entity = Prefab::InstantiateFromFile(*world, prefabPath);
                        if (entity != InvalidEntityId)
                        {
                            if (auto* t = world->GetComponent<TransformComponent>(entity))
                            {
                                t->position.x = transform->position.x + (x - gridSize / 2.0f) * spacing;
                                t->position.y = rowY;
                                t->position.z = transform->position.z + (z - gridSize / 2.0f) * spacing;
                            }
                            m_spawnedEntities.push_back(entity);
                        }
                    }
                }
            }
        }

        ALICE_LOG_INFO("[StressRigidbodySpawner] Spawned %d prefabs. Total entities: %zu", count, m_spawnedEntities.size());
    }

    void StressRigidbodySpawner::ClearSpawnedPrefabs()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        for (EntityId entity : m_spawnedEntities)
        {
            if (entity != InvalidEntityId)
            {
                world->DestroyEntity(entity);
            }
        }

        ALICE_LOG_INFO("[StressRigidbodySpawner] Cleared %zu spawned prefabs", m_spawnedEntities.size());
        m_spawnedEntities.clear();
    }
}
