#include "LoadVFXFromPrefab.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"

#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(LoadVFXFromPrefab);

    void LoadVFXFromPrefab::Start()
    {
        for (int i = 0; i < 9; ++i)
        {
            m_loopTimers[i] = 0.0f;
            m_looping[i] = false;
            m_cachedPaths[i] = GetPrefabPath(i);

            if (m_prewarm && !m_cachedPaths[i].empty())
                PrewarmSlot(i);
        }
    }

    void LoadVFXFromPrefab::Update(float deltaTime)
    {
        UpdatePathCacheAndPools();  
        HandleInput();
        UpdateLooping(deltaTime);
        UpdateActive(deltaTime);
    }

    void LoadVFXFromPrefab::OnDisable()
    {
        for (int i = 0; i < 9; ++i)
            DeactivateAllActive(i);
    }

    void LoadVFXFromPrefab::OnDestroy()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        for (int i = 0; i < 9; ++i)
        {
            for (const auto& active : m_active[i])
            {
                if (active.id != InvalidEntityId)
                    world->DestroyEntity(active.id);
            }
            m_active[i].clear();

            for (EntityId id : m_pool[i])
            {
                if (id != InvalidEntityId)
                    world->DestroyEntity(id);
            }
            m_pool[i].clear();
        }
    }

    void LoadVFXFromPrefab::UpdatePathCacheAndPools()
    {
        for (int i = 0; i < 9; ++i)
        {
            const std::string& current = GetPrefabPath(i);
            if (current != m_cachedPaths[i])
            {
                ClearSlot(i);
                m_cachedPaths[i] = current;
                if (m_prewarm && !current.empty())
                    PrewarmSlot(i);
            }
        }
    }

    void LoadVFXFromPrefab::HandleInput()
    {
        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(KeyCode::Alpha1)) { m_loop ? ToggleLoop(0) : SpawnOnce(0); }
        if (input->GetKeyDown(KeyCode::Alpha2)) { m_loop ? ToggleLoop(1) : SpawnOnce(1); }
        if (input->GetKeyDown(KeyCode::Alpha3)) { m_loop ? ToggleLoop(2) : SpawnOnce(2); }
        if (input->GetKeyDown(KeyCode::Alpha4)) { m_loop ? ToggleLoop(3) : SpawnOnce(3); }
        if (input->GetKeyDown(KeyCode::Alpha5)) { m_loop ? ToggleLoop(4) : SpawnOnce(4); }
        if (input->GetKeyDown(KeyCode::Alpha6)) { m_loop ? ToggleLoop(5) : SpawnOnce(5); }
        if (input->GetKeyDown(KeyCode::Alpha7)) { m_loop ? ToggleLoop(6) : SpawnOnce(6); }
        if (input->GetKeyDown(KeyCode::Alpha8)) { m_loop ? ToggleLoop(7) : SpawnOnce(7); }
        if (input->GetKeyDown(KeyCode::Alpha9)) { m_loop ? ToggleLoop(8) : SpawnOnce(8); }
    }

    void LoadVFXFromPrefab::ToggleLoop(int slot)
    {
        m_looping[slot] = !m_looping[slot];
        if (m_looping[slot])
        {
            m_loopTimers[slot] = 0.0f; // 즉시 1회 스폰
        }
    }

    void LoadVFXFromPrefab::SpawnOnce(int slot)
    {
        const std::string& path = GetPrefabPath(slot);
        if (path.empty())
        {
            ALICE_LOG_WARN("[LoadVFXFromPrefab] Slot %d path is empty.", slot + 1);
            return;
        }

        if (m_lifeTime <= 0.0f && !m_active[slot].empty())
        {
            DeactivateAllActive(slot);
            return;
        }

        EntityId id = Acquire(slot);
        if (id == InvalidEntityId)
            return;

        ActiveInstance inst;
        inst.id = id;
        inst.remaining = (m_lifeTime > 0.0f) ? m_lifeTime : -1.0f;
        m_active[slot].push_back(inst);
    }

    void LoadVFXFromPrefab::UpdateLooping(float deltaTime)
    {
        if (!m_loop)
            return;

        const float interval = (std::max)(0.01f, m_loopInterval);
        for (int i = 0; i < 9; ++i)
        {
            if (!m_looping[i])
                continue;

            m_loopTimers[i] -= deltaTime;
            if (m_loopTimers[i] <= 0.0f)
            {
                m_loopTimers[i] = interval;
                SpawnOnce(i);
            }
        }
    }

    void LoadVFXFromPrefab::UpdateActive(float deltaTime)
    {
        for (int i = 0; i < 9; ++i)
        {
            auto& list = m_active[i];
            for (size_t idx = 0; idx < list.size();)
            {
                ActiveInstance& inst = list[idx];
                if (inst.id == InvalidEntityId)
                {
                    list.erase(list.begin() + idx);
                    continue;
                }

                if (inst.remaining > 0.0f)
                {
                    inst.remaining -= deltaTime;
                    if (inst.remaining <= 0.0f)
                    {
                        Release(i, inst.id);
                        list.erase(list.begin() + idx);
                        continue;
                    }
                }

                ++idx;
            }
        }
    }

    void LoadVFXFromPrefab::PrewarmSlot(int slot)
    {
        const std::string& path = GetPrefabPath(slot);
        if (path.empty())
            return;

        const int poolSize = GetPoolSizeSafe();
        for (int i = 0; i < poolSize; ++i)
        {
            EntityId id = CreateInstance(slot);
            if (id == InvalidEntityId)
                continue;

            Release(slot, id);
        }
    }

    void LoadVFXFromPrefab::ClearSlot(int slot)
    {
        auto* world = GetWorld();
        if (!world)
        {
            m_active[slot].clear();
            m_pool[slot].clear();
            return;
        }

        for (const auto& inst : m_active[slot])
        {
            if (inst.id != InvalidEntityId)
                world->DestroyEntity(inst.id);
        }
        m_active[slot].clear();

        for (EntityId id : m_pool[slot])
        {
            if (id != InvalidEntityId)
                world->DestroyEntity(id);
        }
        m_pool[slot].clear();
        m_loopTimers[slot] = 0.0f;
        m_looping[slot] = false;
    }

    void LoadVFXFromPrefab::DeactivateAllActive(int slot)
    {
        auto* world = GetWorld();
        if (!world)
        {
            m_active[slot].clear();
            return;
        }

        for (const auto& inst : m_active[slot])
        {
            if (inst.id != InvalidEntityId)
                Release(slot, inst.id);
        }
        m_active[slot].clear();
    }

    EntityId LoadVFXFromPrefab::Acquire(int slot)
    {
        auto* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        EntityId id = InvalidEntityId;
        auto& pool = m_pool[slot];
        if (!pool.empty())
        {
            id = pool.back();
            pool.pop_back();
        }
        else
        {
            id = CreateInstance(slot);
        }

        if (id == InvalidEntityId)
            return InvalidEntityId;

        EntityId parent = ResolveParent();
        if (parent != InvalidEntityId)
            world->SetParent(id, parent, false);
        else
            world->SetParent(id, InvalidEntityId, false);

        if (auto* t = world->GetComponent<TransformComponent>(id))
        {
            t->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            t->rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            world->MarkTransformDirty(id);
        }

        SetEntityActiveRecursive(*world, id, true);
        return id;
    }

    void LoadVFXFromPrefab::Release(int slot, EntityId id)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        SetEntityActiveRecursive(*world, id, false);
        m_pool[slot].push_back(id);
    }

    EntityId LoadVFXFromPrefab::CreateInstance(int slot)
    {
        const std::string& path = GetPrefabPath(slot);
        if (path.empty())
            return InvalidEntityId;

        auto* world = GetWorld();
        if (!world)
        {
            ALICE_LOG_ERRORF("[LoadVFXFromPrefab] Instantiate failed: world is null.");
            return InvalidEntityId;
        }
        Prefab::SetDefaultWorld(world);

        EntityId id = Prefab::InstantiateFromFileAuto(path);
        if (id == InvalidEntityId)
        {
            ALICE_LOG_ERRORF("[LoadVFXFromPrefab] Instantiate failed: %s", path.c_str());
            return InvalidEntityId;
        }

        EntityId parent = ResolveParent();
        if (parent != InvalidEntityId)
            world->SetParent(id, parent, false);

        if (auto* t = world->GetComponent<TransformComponent>(id))
        {
            t->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            t->rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
            world->MarkTransformDirty(id);
        }

        return id;
    }

    EntityId LoadVFXFromPrefab::ResolveParent() const
    {
        auto* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        if (!m_parentTargetName.empty())
        {
            GameObject target = world->FindGameObject(m_parentTargetName);
            if (target.IsValid())
                return target.id();
        }

        return GetOwnerId();
    }

    void LoadVFXFromPrefab::SetEntityActiveRecursive(World& world, EntityId id, bool active)
    {
        if (id == InvalidEntityId)
            return;

        if (auto* t = world.GetComponent<TransformComponent>(id))
        {
            t->enabled = active;
            t->visible = active;
            world.MarkTransformDirty(id);
        }

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
        {
            vfx->enabled = active;
            if (active)
                vfx->playId += 1;
        }

        if (auto* ce = world.GetComponent<ComputeEffectComponent>(id))
            ce->enabled = active;

        auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityActiveRecursive(world, child, active);
    }

    const std::string& LoadVFXFromPrefab::GetPrefabPath(int slot) const
    {
        static const std::string kEmpty;
        switch (slot)
        {
        case 0: return m_prefabPath1;
        case 1: return m_prefabPath2;
        case 2: return m_prefabPath3;
        case 3: return m_prefabPath4;
        case 4: return m_prefabPath5;
        case 5: return m_prefabPath6;
        case 6: return m_prefabPath7;
        case 7: return m_prefabPath8;
        case 8: return m_prefabPath9;
        default: return kEmpty;
        }
    }

    int LoadVFXFromPrefab::GetPoolSizeSafe() const
    {
        if (m_poolSize <= 0)
            return 0;
        return m_poolSize;
    }
}
