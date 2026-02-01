#include "C_BossBrainComponent.h"

#include <algorithm>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"

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

        (void)deltaTime;
        (void)targetId;

        if (m_cooldownTimer <= 0.0f)
        {
            intent.lightAttackPressed = true;
            intent.attackPressed = true;
            m_cooldownTimer = m_attackCooldown;
        }

        return intent;
    }
}
