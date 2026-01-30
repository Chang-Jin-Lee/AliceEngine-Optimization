#include "C_PlayerInputSourceComponent.h"

#include "Core/ScriptFactory.h"
#include "Core/ScriptAPI.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/HealthComponent.h"
#include "Components/AttackDriverComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(C_PlayerInputSourceComponent);

    void C_PlayerInputSourceComponent::EnsurePlayerComponents()
    {
        if (!GetWorld())
            return;

        EntityId id = GetOwnerId();
        if (id == InvalidEntityId)
            return;

        if (!GetComponent<TransformComponent>())
            AddComponent<TransformComponent>();

        if (!GetComponent<Phy_CCTComponent>())
            AddComponent<Phy_CCTComponent>();

        if (!GetComponent<HealthComponent>())
            AddComponent<HealthComponent>();

        if (!GetComponent<AttackDriverComponent>())
            AddComponent<AttackDriverComponent>();
    }

    void C_PlayerInputSourceComponent::Start()
    {
        EnsurePlayerComponents();
    }

    void C_PlayerInputSourceComponent::Update(float deltaTime)
    {
        m_cached = GetIntent(deltaTime);
    }

    void C_PlayerInputSourceComponent::OnDisable()
    {
        m_cached = {};
    }

    Combat::Intent C_PlayerInputSourceComponent::GetIntent(float /*deltaTime*/)
    {
        Combat::Intent intent{};
        if (!Input())
            return intent;

        auto toKey = [](int v) { return static_cast<KeyCode>(v); };

        float x = 0.0f;
        float y = 0.0f;
        if (Input()->GetKey(toKey(m_keyLeft))) x -= 1.0f;
        if (Input()->GetKey(toKey(m_keyRight))) x += 1.0f;
        if (Input()->GetKey(toKey(m_keyForward))) y += 1.0f;
        if (Input()->GetKey(toKey(m_keyBackward))) y -= 1.0f;

        intent.move = { x, y };
        intent.attackPressed = Input()->GetKeyDown(toKey(m_keyAttack))
            || (m_useMouseAttack && Input()->GetMouseButtonDown(MouseCode::Left));
        intent.dodgePressed = Input()->GetKeyDown(toKey(m_keyDodge));
        intent.guardHeld = Input()->GetKey(toKey(m_keyGuard))
            || (m_useMouseAttack && Input()->GetMouseButton(MouseCode::Right));

        return intent;
    }
}
