#include "UnityVfxSpawner.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"

namespace Alice
{
    REGISTER_SCRIPT(UnityVfxSpawner);

    void UnityVfxSpawner::Start()
    {
        if (m_playOnStart)
        {
            Spawn();
        }
    }

    void UnityVfxSpawner::Update(float /*deltaTime*/)
    {
        // 현재는 별도 업데이트 로직 없음
    }

    void UnityVfxSpawner::Spawn()
    {
        if (m_effectPath.empty())
        {
            ALICE_LOG_WARN("[UnityVfxSpawner] effectPath is empty.");
            return;
        }
        SpawnInternal(m_effectPath);
    }

    EntityId UnityVfxSpawner::SpawnInternal(const std::string& path)
    {
        World* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        EntityId e = world->CreateEmpty();
        auto* tr = world->GetComponent<TransformComponent>(e);
        if (!tr)
            return InvalidEntityId;

        // 기본 로컬 변환 설정
        tr->SetPosition(m_localOffset);
        tr->SetRotation(m_localRotationDeg);
        tr->SetScale(m_localScale);

        // 부모 따라가기 옵션
        if (m_followOwner)
        {
            tr->parent = GetOwnerId();
        }
        else
        {
            if (auto* ownerTr = GetTransform())
            {
                tr->position.x += ownerTr->position.x;
                tr->position.y += ownerTr->position.y;
                tr->position.z += ownerTr->position.z;
            }
        }

        UnityVfxComponent& vfx = world->AddComponent<UnityVfxComponent>(e);
        vfx.enabled = true;
        vfx.effectPath = path;
        vfx.useMeshRenderer = m_useMeshRenderer;
        vfx.useComputeEffect = m_useComputeEffect;
        vfx.timeScale = m_timeScale;
        vfx.lifetimeScale = m_lifetimeScale;
        vfx.overrideLoop = m_overrideLoop;
        vfx.loop = m_loop;
        vfx.sizeScale = m_sizeScale;
        vfx.speedScale = m_speedScale;
        vfx.intensityScale = m_intensityScale;
        vfx.spawnRateScale = m_spawnRateScale;
        vfx.emitNewParticles = m_emitNewParticles;
        vfx.enableTrails = m_enableTrails;
        vfx.trailWidthScale = m_trailWidthScale;
        vfx.trailLifeScale = m_trailLifeScale;

        if (m_autoDestroySec > 0.0f)
        {
            world->ScheduleDelayedDestruction(e, m_autoDestroySec);
        }

        return e;
    }
}
