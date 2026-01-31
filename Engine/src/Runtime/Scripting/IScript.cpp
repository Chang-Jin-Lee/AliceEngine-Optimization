#include "Runtime/Scripting/IScript.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"

namespace Alice
{
    // === IScript 기본 헬퍼 구현 ===

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
