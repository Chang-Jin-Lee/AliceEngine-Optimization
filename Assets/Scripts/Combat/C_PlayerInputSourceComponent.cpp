#include "C_PlayerInputSourceComponent.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptAPI.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Foundation/Logger.h"
#include "C_CombatSessionComponent.h"

#include <algorithm>
#include <cmath>
//TODO : Include Ȯ�� �ؾ���

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

        if (auto* driver = GetComponent<AttackDriverComponent>())
        {
            // Ensure at least one Attack clip for window evaluation.
            bool hasAttackClip = false;
            for (const auto& clip : driver->clips)
            {
                if (clip.enabled && clip.type == AttackDriverNotifyType::Attack)
                {
                    hasAttackClip = true;
                    break;
                }
            }

            if (!hasAttackClip)
            {
                AttackDriverClip clip{};
                clip.type = AttackDriverNotifyType::Attack;
                clip.source = AttackDriverClipSource::Explicit;
                clip.clipName = "swing";
                driver->clips.push_back(clip);
            }

            auto* world = GetWorld();
            if (world)
            {
                // Auto-bind trace to the owner's weapon if missing.
                const bool traceMissing = (driver->traceGuid == 0)
                    || (world->FindEntityByGuid(driver->traceGuid) == InvalidEntityId);
                if (traceMissing)
                {
                    const auto* idc = GetComponent<IDComponent>();
                    const uint64_t ownerGuid = idc ? idc->guid : 0;
                    const std::string ownerName = world->GetEntityName(id);

                    for (auto&& [traceId, trace] : world->GetComponents<WeaponTraceComponent>())
                    {
                        const bool matchGuid = (ownerGuid != 0 && trace.ownerGuid == ownerGuid);
                        const bool matchName = (!ownerName.empty() && trace.ownerNameDebug == ownerName);
                        if (!matchGuid && !matchName)
                            continue;

                        if (auto* traceIdc = world->GetComponent<IDComponent>(traceId))
                        {
                            driver->traceGuid = traceIdc->guid;
                            if (m_enableLogs)
                            {
                                ALICE_LOG_INFO("[Input] Auto-bound AttackDriver.traceGuid=%llu (trace entity=%llu)",
                                    static_cast<unsigned long long>(driver->traceGuid),
                                    static_cast<unsigned long long>(traceId));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    void C_PlayerInputSourceComponent::Start()
    {
        EnsurePlayerComponents();
    }

    void C_PlayerInputSourceComponent::Update(float deltaTime)
    {
        auto* world = GetWorld();
        // Avoid consuming one-frame input edges twice when CombatSession also queries intent.
        if (world && world->IsScriptCombatEnabled())
            return;

        m_cached = GetIntent(deltaTime);
    }

    void C_PlayerInputSourceComponent::OnDisable()
    {
        m_cached = {};
        m_attackHeldPrev = false;
        m_attackHeldSec = 0.0f;
        m_guardHeldPrev = false;
        m_guardHeldSec = 0.0f;
        m_itemHeldPrev = false;
        m_itemHeldSec = 0.0f;
        m_chargeActive = false;
        m_chargeHeldSec = 0.0f;
        m_chargeLevel = 0;
        m_combatSessionId = InvalidEntityId;
    }

    void C_PlayerInputSourceComponent::CancelCharge()
    {
        m_chargeActive = false;
        m_chargeHeldSec = 0.0f;
        m_chargeLevel = 0;
    }

    Combat::Intent C_PlayerInputSourceComponent::GetIntent(float deltaTime)
    {
        Combat::Intent intent{};
        auto* input = Input();
        if (!input)
            return intent;

        auto toKey = [](int v) { return static_cast<KeyCode>(v); };
        auto toMouse = [](int v) { return static_cast<MouseCode>(v); };
        // Xbox-style default mapping:
        // Move=LStick, Attack=RB, ChargeHeavy=RT(=Shift+Attack), Guard=LB, Heal=LT,
        // Dodge=B, Interact=A, ZoomToggle=X, Rage=Y, LockOn=RStickClick.
        const int gamepadPlayerIndex = std::clamp(Get_m_gamepadPlayerIndex(), 0, 3);
        const bool gamepadConnected = Get_m_useGamepadInput() && input->GetGamepadConnected(gamepadPlayerIndex);
        const float gamepadMoveDeadzone = std::clamp(Get_m_gamepadMoveDeadzone(), 0.0f, 0.99f);
        auto GetPadButton = [&](GamepadButton button) -> bool
            {
                return gamepadConnected && input->GetGamepadButton(button, gamepadPlayerIndex);
            };
        auto GetPadButtonDown = [&](GamepadButton button) -> bool
            {
                return gamepadConnected && input->GetGamepadButtonDown(button, gamepadPlayerIndex);
            };
        const bool ragePressedNow = input->GetKeyDown(toKey(m_keyRage));
        auto IsRageActive = [&]() -> bool
            {
                auto* world = GetWorld();
                if (!world)
                    return false;

                auto ResolveSessionFromEntity = [&](EntityId id) -> const C_CombatSessionComponent*
                    {
                        if (id == InvalidEntityId)
                            return nullptr;
                        const auto* scripts = world->GetScripts(id);
                        if (!scripts)
                            return nullptr;
                        for (const auto& sc : *scripts)
                        {
                            if (!sc.instance || sc.scriptName != "C_CombatSessionComponent")
                                continue;
                            return dynamic_cast<const C_CombatSessionComponent*>(sc.instance.get());
                        }
                        return nullptr;
                    };

                if (const auto* cached = ResolveSessionFromEntity(m_combatSessionId))
                    return cached->IsPlayerRageActive();
                m_combatSessionId = InvalidEntityId;

                for (const auto& [entityId, scripts] : world->GetAllScriptsInWorld())
                {
                    for (const auto& sc : scripts)
                    {
                        if (!sc.instance || sc.scriptName != "C_CombatSessionComponent")
                            continue;
                        if (const auto* session = dynamic_cast<const C_CombatSessionComponent*>(sc.instance.get()))
                        {
                            m_combatSessionId = entityId;
                            return session->IsPlayerRageActive();
                        }
                    }
                }
                return false;
            };
        const bool rageChargeActive = IsRageActive() || ragePressedNow;

        float x = 0.0f;
        float y = 0.0f;
        if (input->GetKey(toKey(m_keyLeft))) x -= 1.0f;
        if (input->GetKey(toKey(m_keyRight))) x += 1.0f;
        if (input->GetKey(toKey(m_keyForward))) y += 1.0f;
        if (input->GetKey(toKey(m_keyBackward))) y -= 1.0f;
        if (gamepadConnected)
        {
            float padX = input->GetGamepadLeftStickX(gamepadPlayerIndex);
            float padY = input->GetGamepadLeftStickY(gamepadPlayerIndex);
            if (std::abs(padX) < gamepadMoveDeadzone) padX = 0.0f;
            if (std::abs(padY) < gamepadMoveDeadzone) padY = 0.0f;
            x += padX;
            y += padY;
        }
        x = std::clamp(x, -1.0f, 1.0f);
        y = std::clamp(y, -1.0f, 1.0f);

        intent.move = { x, y };
        intent.runHeld = (x != 0.0f || y != 0.0f);

        const bool attackKeyDown = input->GetKeyDown(toKey(m_keyAttack));
        const bool attackMouseDown = m_useMouseAttack && input->GetMouseButtonDown(toMouse(m_mouseAttackButton));
        const bool attackPadDown = GetPadButtonDown(GamepadButton::RightShoulder);
        const bool attackTriggerDown = GetPadButtonDown(GamepadButton::RightTrigger);
        const bool attackKeyHeld = input->GetKey(toKey(m_keyAttack));
        const bool attackMouseHeld = m_useMouseAttack && input->GetMouseButton(toMouse(m_mouseAttackButton));
        const bool attackPadHeld = GetPadButton(GamepadButton::RightShoulder);
        const bool attackTriggerHeld = GetPadButton(GamepadButton::RightTrigger);
        const bool attackPressed = attackKeyDown || attackMouseDown || attackPadDown || attackTriggerDown;
        const bool attackHeld = attackKeyHeld || attackMouseHeld || attackPadHeld || attackTriggerHeld;
        const bool attackReleased = (!attackHeld && m_attackHeldPrev);

        const bool chargeModifierHeld =
            input->GetKey(toKey(m_keyChargeModifier)) || input->GetKey(toKey(m_keyChargeModifierAlt))
            || attackTriggerHeld;

        if (attackPressed && chargeModifierHeld)
        {
            m_chargeActive = true;
            m_chargeHeldSec = 0.0f;
            m_chargeLevel = 0;
        }

        if (m_chargeActive)
        {
            if (attackHeld)
                m_chargeHeldSec += deltaTime;

            float chargeStage1Sec = std::max(0.0f, m_chargeStage1Sec);
            float chargeStage2Sec = std::max(0.0f, m_chargeStage2Sec);
            float chargeStage3Sec = std::max(0.0f, m_chargeStage3Sec);
            if (rageChargeActive)
            {
                const float rageStepSec = std::max(0.0f, m_rageChargeStageStepSec);
                if (rageStepSec > 0.0f)
                {
                    chargeStage1Sec = rageStepSec;
                    chargeStage2Sec = rageStepSec * 2.0f;
                    chargeStage3Sec = rageStepSec * 3.0f;
                }
            }

            int level = 0;
            if (chargeStage1Sec > 0.0f && m_chargeHeldSec >= chargeStage1Sec)
                level = 1;
            if (chargeStage2Sec > 0.0f && m_chargeHeldSec >= chargeStage2Sec)
                level = 2;
            if (chargeStage3Sec > 0.0f && m_chargeHeldSec >= chargeStage3Sec)
                level = 3;
            m_chargeLevel = level;

            if (level >= 3)
            {
                intent.heavyAttackPressed = true;
                intent.chargeLevel = 3;
                m_chargeActive = false;
                m_chargeHeldSec = 0.0f;
                m_chargeLevel = 0;
            }
            else if (attackReleased)
            {
                if (level >= 1)
                {
                    intent.heavyAttackPressed = true;
                    intent.chargeLevel = level;
                }
                else
                {
                    intent.lightAttackPressed = true;
                    intent.chargeLevel = 0;
                }
                m_chargeActive = false;
                m_chargeHeldSec = 0.0f;
                m_chargeLevel = 0;
            }
            else
            {
                intent.chargeActive = true;
                intent.chargeHeldSec = m_chargeHeldSec;
                intent.chargeLevel = m_chargeLevel;
            }
        }
        else
        {
            if (attackPressed && !chargeModifierHeld)
                intent.lightAttackPressed = true;
        }

        intent.attackPressed = intent.lightAttackPressed || intent.heavyAttackPressed;
        intent.attackHeld = (!m_chargeActive && attackHeld && !chargeModifierHeld);
        if (attackHeld)
            m_attackHeldSec += deltaTime;
        else
            m_attackHeldSec = 0.0f;
        intent.attackHeldSec = intent.attackHeld ? m_attackHeldSec : 0.0f;

        const bool guardKeyDown = input->GetKeyDown(toKey(m_keyGuard));
        const bool guardMouseDown = m_useMouseAttack && input->GetMouseButtonDown(toMouse(m_mouseGuardButton));
        const bool guardPadDown = GetPadButtonDown(GamepadButton::LeftShoulder);
        const bool guardKeyHeld = input->GetKey(toKey(m_keyGuard));
        const bool guardMouseHeld = m_useMouseAttack && input->GetMouseButton(toMouse(m_mouseGuardButton));
        const bool guardPadHeld = GetPadButton(GamepadButton::LeftShoulder);
        const bool guardHeld = guardKeyHeld || guardMouseHeld || guardPadHeld;
        const bool guardPressed = guardKeyDown || guardMouseDown || guardPadDown;
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
        intent.parryTapWindowSec = m_parryWindowSec;

        intent.dodgePressed = input->GetKeyDown(toKey(m_keyDodge))
            || GetPadButtonDown(GamepadButton::B);
        const bool itemPressed = input->GetKeyDown(toKey(m_keyItem))
            || GetPadButtonDown(GamepadButton::LeftTrigger);
        const bool itemHeld = input->GetKey(toKey(m_keyItem))
            || GetPadButton(GamepadButton::LeftTrigger);
        const bool itemReleased = (!itemHeld && m_itemHeldPrev);
        if (itemPressed)
            m_itemHeldSec = 0.0f;
        if (itemHeld)
            m_itemHeldSec += deltaTime;
        else
            m_itemHeldSec = 0.0f;
        intent.itemPressed = itemPressed;
        intent.itemHeld = itemHeld;
        intent.itemReleased = itemReleased;
        intent.itemHeldSec = itemHeld ? m_itemHeldSec : 0.0f;
        intent.interactPressed = input->GetKeyDown(toKey(m_keyInteract))
            || GetPadButtonDown(GamepadButton::A);
        intent.zoomToggle = GetPadButtonDown(GamepadButton::X);
        intent.ragePressed = ragePressedNow || GetPadButtonDown(GamepadButton::Y);

        if (m_useMouseLockOn)
            intent.lockOnToggle = input->GetMouseButtonDown(toMouse(m_mouseLockOnButton));
        intent.lockOnToggle = intent.lockOnToggle || GetPadButtonDown(GamepadButton::RightThumb);

        m_attackHeldPrev = attackHeld;
        m_guardHeldPrev = guardHeld;
        m_itemHeldPrev = itemHeld;

        if (m_enableLogs)
        {
            const bool hasMove = (intent.move.x != 0.0f || intent.move.y != 0.0f);
            const bool anyAction = intent.attackPressed || intent.guardHeld || intent.dodgePressed
                || intent.itemPressed || intent.interactPressed || intent.ragePressed
                || intent.lockOnToggle || intent.zoomToggle;
            if (hasMove || anyAction)
            {
                ALICE_LOG_INFO("[Input] move(%.1f,%.1f) attack=%d guard=%d dodge=%d item=%d interact=%d rage=%d lockOn=%d zoom=%d",
                    intent.move.x, intent.move.y,
                    intent.attackPressed ? 1 : 0,
                    intent.guardHeld ? 1 : 0,
                    intent.dodgePressed ? 1 : 0,
                    intent.itemPressed ? 1 : 0,
                    intent.interactPressed ? 1 : 0,
                    intent.ragePressed ? 1 : 0,
                    intent.lockOnToggle ? 1 : 0,
                    intent.zoomToggle ? 1 : 0);
            }
        }

        return intent;
    }
}


