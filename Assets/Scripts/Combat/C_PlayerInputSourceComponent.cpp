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
        m_attackHeldPrev = false;
        m_attackHeldSec = 0.0f;
        m_guardHeldPrev = false;
        m_guardHeldSec = 0.0f;
    }

    Combat::Intent C_PlayerInputSourceComponent::GetIntent(float deltaTime)
    {
        Combat::Intent intent{};
        auto* input = Input();
        if (!input)
            return intent;

        auto toKey = [](int v) { return static_cast<KeyCode>(v); };
        auto toMouse = [](int v) { return static_cast<MouseCode>(v); };

        float x = 0.0f;
        float y = 0.0f;
        if (input->GetKey(toKey(m_keyLeft))) x -= 1.0f;
        if (input->GetKey(toKey(m_keyRight))) x += 1.0f;
        if (input->GetKey(toKey(m_keyForward))) y += 1.0f;
        if (input->GetKey(toKey(m_keyBackward))) y -= 1.0f;

        intent.move = { x, y };
        intent.runHeld = (x != 0.0f || y != 0.0f);

        const bool attackKeyDown = input->GetKeyDown(toKey(m_keyAttack));
        const bool attackMouseDown = m_useMouseAttack && input->GetMouseButtonDown(MouseCode::Left);
        const bool attackKeyHeld = input->GetKey(toKey(m_keyAttack));
        const bool attackMouseHeld = m_useMouseAttack && input->GetMouseButton(MouseCode::Left);
        const bool attackPressed = attackKeyDown || attackMouseDown;
        const bool attackHeld = attackKeyHeld || attackMouseHeld;
        const bool attackReleased = (!attackHeld && m_attackHeldPrev);

        if (attackPressed)
            m_attackHeldSec = 0.0f;
        if (attackHeld)
            m_attackHeldSec += deltaTime;

        if (attackReleased)
        {
            if (m_attackHeldSec >= m_attackHoldThresholdSec)
                intent.heavyAttackPressed = true;
            else
                intent.lightAttackPressed = true;
            m_attackHeldSec = 0.0f;
        }
        else if (attackPressed && !attackHeld)
        {
            intent.lightAttackPressed = true;
        }

        intent.attackPressed = attackPressed;
        intent.attackHeld = attackHeld;
        intent.attackHeldSec = attackHeld ? m_attackHeldSec : 0.0f;

        const bool guardKeyDown = input->GetKeyDown(toKey(m_keyGuard));
        const bool guardMouseDown = m_useMouseAttack && input->GetMouseButtonDown(MouseCode::Right);
        const bool guardKeyHeld = input->GetKey(toKey(m_keyGuard));
        const bool guardMouseHeld = m_useMouseAttack && input->GetMouseButton(MouseCode::Right);
        const bool guardHeld = guardKeyHeld || guardMouseHeld;
        const bool guardPressed = guardKeyDown || guardMouseDown;
        const bool guardReleased = (!guardHeld && m_guardHeldPrev);

        if (guardPressed)
            m_guardHeldSec = 0.0f;
        if (guardHeld)
            m_guardHeldSec += deltaTime;
        else
            m_guardHeldSec = 0.0f;

        intent.guardHeld = guardHeld;
        intent.guardPressed = guardPressed;
        intent.guardReleased = guardReleased;
        intent.guardHeldSec = guardHeld ? m_guardHeldSec : 0.0f;
        intent.parryWindowActive = guardHeld && (m_guardHeldSec <= m_parryWindowSec);

        intent.dodgePressed = input->GetKeyDown(toKey(m_keyDodge));
        intent.itemPressed = input->GetKeyDown(toKey(m_keyItem));
        intent.interactPressed = input->GetKeyDown(toKey(m_keyInteract));
        intent.ragePressed = input->GetKeyDown(toKey(m_keyRage));

        if (m_useMouseLockOn)
            intent.lockOnToggle = input->GetMouseButtonDown(toMouse(m_mouseLockOnButton));

        m_attackHeldPrev = attackHeld;
        m_guardHeldPrev = guardHeld;

        return intent;
    }
}
