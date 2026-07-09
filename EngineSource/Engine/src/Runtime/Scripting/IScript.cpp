#include "Runtime/Scripting/IScript.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Scripting/ScriptInstanceTracker.h"

namespace Alice
{
    // === IScript 기본 헬퍼 구현 ===

    IScript::IScript()
    {
        ScriptInstanceTracker::OnCreated(this);
    }

    IScript::~IScript()
    {
        ScriptInstanceTracker::OnDestroyed(this);
    }

    void IScript::SetContext(World* world, EntityId entity)
    {
        m_world = world;
        m_entity = entity;
        Prefab::SetDefaultWorld(world);
    }

    void IScript::SetServices(ScriptServices* services)
    {
        m_services = services;
        ResourceManager* resources = (m_services ? m_services->resources : nullptr);
        Prefab::SetDefaultResources(resources);
    }

    bool IScript::IsGameMode() const
    {
        const ResourceManager* resources = Resources();
        return resources ? resources->IsGameMode() : false;
    }

    TransformComponent* IScript::GetTransform()
    {
        if (!m_world || m_entity == InvalidEntityId)
            return nullptr;

        return m_world->GetComponent<TransformComponent>(m_entity);
    }

    GameObject IScript::gameObject() const
    {
        return GameObject(m_world, m_entity, m_services);
    }

    GameObject* IScript::GetOwner()
    {
        // 월드 또는 엔티티가 유효하지 않으면 nullptr
        if (!m_world || m_entity == InvalidEntityId)
            return nullptr;

        // GameObject 래퍼를 한 번 생성해서 유효성 검사
        static thread_local GameObject ownerHandle;
        ownerHandle = GameObject(m_world, m_entity, m_services);

        if (!ownerHandle.IsValid())
            return nullptr;

        return &ownerHandle;
    }
}
