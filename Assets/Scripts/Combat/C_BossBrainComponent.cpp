#include "C_BossBrainComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/ScriptFactory.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(C_BossBrainComponent);

    void C_BossBrainComponent::Start()
    {
        m_cooldownTimer = 0.0f;
    }

    void C_BossBrainComponent::Update(float deltaTime)
    {
        m_cooldownTimer = std::max(0.0f, m_cooldownTimer - deltaTime);
    }

    void C_BossBrainComponent::OnDisable()
    {
        m_cooldownTimer = 0.0f;
    }

    Combat::Intent C_BossBrainComponent::Think(float deltaTime, EntityId targetId)
    {
        Combat::Intent intent{};

        auto* world = GetWorld();
        auto* selfTr = GetComponent<TransformComponent>();
        auto* targetTr = world ? world->GetComponent<TransformComponent>(targetId) : nullptr;
        if (!selfTr || !targetTr)
            return intent;

        const float dx = targetTr->position.x - selfTr->position.x;
        const float dz = targetTr->position.z - selfTr->position.z;
        const float dist = std::sqrt(dx * dx + dz * dz);

        if (dist <= m_attackRange && m_cooldownTimer <= 0.0f)
        {
            intent.attackPressed = true;
            m_cooldownTimer = m_attackCooldown;
        }
        else
        {
            intent.move = { (dx >= 0.0f ? 1.0f : -1.0f) * m_moveBias, 1.0f };
        }

        return intent;
    }
}
