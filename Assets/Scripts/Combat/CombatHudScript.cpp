#include "CombatHudScript.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "C_CombatSessionComponent.h"


namespace Alice
{
    REGISTER_SCRIPT(CombatHudScript);

    namespace
    {
        EntityId FindWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }

        UITextComponent* FindText(World& world, const std::string& name)
        {
            const EntityId id = FindWidgetByName(world, name);
            return (id != InvalidEntityId) ? world.GetComponent<UITextComponent>(id) : nullptr;
        }

        UIGaugeComponent* FindGauge(World& world, const std::string& name)
        {
            const EntityId id = FindWidgetByName(world, name);
            return (id != InvalidEntityId) ? world.GetComponent<UIGaugeComponent>(id) : nullptr;
        }
    }

    void CombatHudScript::Start()
    {
        BindWidgets();
        RefreshEntityIds();
    }

    void CombatHudScript::Update(float /*deltaTime*/)
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_playerId == InvalidEntityId || m_bossId == InvalidEntityId)
            RefreshEntityIds();

        auto* playerHealth = (m_playerId != InvalidEntityId) ? world->GetComponent<HealthComponent>(m_playerId) : nullptr;
        auto* bossHealth = (m_bossId != InvalidEntityId) ? world->GetComponent<HealthComponent>(m_bossId) : nullptr;
        if (playerHealth)
        {
            UpdateGauge(m_playerHpGauge, playerHealth->currentHealth, playerHealth->maxHealth);
            UpdateGauge(m_weaponGauge, playerHealth->weaponDurability, playerHealth->weaponDurabilityMax);
        }
        if (bossHealth)
        {
            UpdateGauge(m_bossHpGauge, bossHealth->currentHealth, bossHealth->maxHealth);
            UpdateGauge(m_bossGroggyGauge, bossHealth->groggy, bossHealth->groggyMax);
        }

        Combat::ActionState playerState = Combat::ActionState::Idle;
        Combat::ActionState bossState = Combat::ActionState::Idle;
        Combat::ActionFlags playerFlags{};
        Combat::ActionFlags bossFlags{};

        GameObject sessionGo = world->FindGameObject(Get_sessionEntityName());
        if (sessionGo.IsValid())
        {
            if (auto* scripts = world->GetScripts(sessionGo.id()))
            {
                for (auto& sc : *scripts)
                {
                    if (sc.scriptName == "C_CombatSessionComponent" && sc.instance)
                    {
                        auto* session = static_cast<C_CombatSessionComponent*>(sc.instance.get());
                        playerState = session->GetPlayerState();
                        bossState = session->GetBossState();
                        playerFlags = session->GetPlayerFlags();
                        bossFlags = session->GetBossFlags();
                        break;
                    }
                }
            }
        }

        const float playerStateInactive = (Get_playerStateFontSize() > 0.0f) ? Get_playerStateFontSize() : Get_stateTextFontSize();
        const float playerStateActive = (Get_playerActiveStateFontSize() > 0.0f) ? Get_playerActiveStateFontSize() : Get_activeStateTextFontSize();
        const float bossStateInactive = (Get_bossStateFontSize() > 0.0f) ? Get_bossStateFontSize() : Get_stateTextFontSize();
        const float bossStateActive = (Get_bossActiveStateFontSize() > 0.0f) ? Get_bossActiveStateFontSize() : Get_activeStateTextFontSize();

        const float playerWindowInactive = (Get_playerWindowFontSize() > 0.0f) ? Get_playerWindowFontSize() : Get_windowTextFontSize();
        const float playerWindowActive = (Get_playerActiveWindowFontSize() > 0.0f) ? Get_playerActiveWindowFontSize() : Get_activeWindowTextFontSize();
        const float bossWindowInactive = (Get_bossWindowFontSize() > 0.0f) ? Get_bossWindowFontSize() : Get_windowTextFontSize();
        const float bossWindowActive = (Get_bossActiveWindowFontSize() > 0.0f) ? Get_bossActiveWindowFontSize() : Get_activeWindowTextFontSize();

        const Combat::ActionState playerStates[] = {
            Combat::ActionState::Idle,
            Combat::ActionState::Move,
            Combat::ActionState::Attack,
            Combat::ActionState::Guard,
            Combat::ActionState::Dodge
        };
        const Combat::ActionState bossStates[] = {
            Combat::ActionState::Idle,
            Combat::ActionState::Move,
            Combat::ActionState::Attack,
            Combat::ActionState::Guard,
            Combat::ActionState::Dodge,
            Combat::ActionState::Groggy
        };

        const size_t playerStateCount = sizeof(playerStates) / sizeof(playerStates[0]);
        const size_t bossStateCount = sizeof(bossStates) / sizeof(bossStates[0]);
        UpdateStateTexts(m_playerStateTexts.data(), playerStates, playerStateCount, playerState, playerStateInactive, playerStateActive);
        UpdateStateTexts(m_bossStateTexts.data(), bossStates, bossStateCount, bossState, bossStateInactive, bossStateActive);
        UpdateWindowText(m_playerWindowText, playerFlags, playerWindowInactive, playerWindowActive);
        UpdateWindowText(m_bossWindowText, bossFlags, bossWindowInactive, bossWindowActive);

        if (Get_useWorldSpace())
            UpdateWorldAnchors();
    }

    void CombatHudScript::RefreshEntityIds()
    {
        World* world = GetWorld();
        if (!world)
            return;

        GameObject playerGo = world->FindGameObject(Get_playerEntityName());
        GameObject bossGo = world->FindGameObject(Get_bossEntityName());
        m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
    }

    void CombatHudScript::BindWidgets()
    {
        World* world = GetWorld();
        if (!world)
            return;

        m_playerHpGaugeId = FindWidgetByName(*world, Get_playerHpGaugeName());
        m_bossHpGaugeId = FindWidgetByName(*world, Get_bossHpGaugeName());
        m_bossGroggyGaugeId = FindWidgetByName(*world, Get_bossGroggyGaugeName());
        m_weaponGaugeId = FindWidgetByName(*world, Get_weaponGaugeName());
        m_playerHpGauge = (m_playerHpGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_playerHpGaugeId) : nullptr;
        m_bossHpGauge = (m_bossHpGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_bossHpGaugeId) : nullptr;
        m_bossGroggyGauge = (m_bossGroggyGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_bossGroggyGaugeId) : nullptr;
        m_weaponGauge = (m_weaponGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_weaponGaugeId) : nullptr;

        m_playerStateTexts = {
            FindText(*world, Get_playerStateIdleTextName()),
            FindText(*world, Get_playerStateMoveTextName()),
            FindText(*world, Get_playerStateAttackTextName()),
            FindText(*world, Get_playerStateGuardTextName()),
            FindText(*world, Get_playerStateDodgeTextName())
        };

        m_bossStateTexts = {
            FindText(*world, Get_bossStateIdleTextName()),
            FindText(*world, Get_bossStateMoveTextName()),
            FindText(*world, Get_bossStateAttackTextName()),
            FindText(*world, Get_bossStateGuardTextName()),
            FindText(*world, Get_bossStateDodgeTextName()),
            FindText(*world, Get_bossStateGroggyTextName())
        };

        m_playerWindowTextId = FindWidgetByName(*world, Get_playerWindowTextName());
        m_bossWindowTextId = FindWidgetByName(*world, Get_bossWindowTextName());
        m_playerWindowText = (m_playerWindowTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_playerWindowTextId) : nullptr;
        m_bossWindowText = (m_bossWindowTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_bossWindowTextId) : nullptr;

        m_playerStateTextIds = {
            FindWidgetByName(*world, Get_playerStateIdleTextName()),
            FindWidgetByName(*world, Get_playerStateMoveTextName()),
            FindWidgetByName(*world, Get_playerStateAttackTextName()),
            FindWidgetByName(*world, Get_playerStateGuardTextName()),
            FindWidgetByName(*world, Get_playerStateDodgeTextName())
        };

        m_bossStateTextIds = {
            FindWidgetByName(*world, Get_bossStateIdleTextName()),
            FindWidgetByName(*world, Get_bossStateMoveTextName()),
            FindWidgetByName(*world, Get_bossStateAttackTextName()),
            FindWidgetByName(*world, Get_bossStateGuardTextName()),
            FindWidgetByName(*world, Get_bossStateDodgeTextName()),
            FindWidgetByName(*world, Get_bossStateGroggyTextName())
        };
    }

    void CombatHudScript::UpdateWorldAnchors()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto ApplyWidgetCommon = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
            {
                widget->space = AliceUI::UISpace::World;
                widget->billboard = Get_worldBillboard();
            }
            const UITransformComponent* sizeSource = nullptr;
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
            {
                uiTransform->position = { 0.0f, 0.0f };
                uiTransform->useAlignment = false;
                sizeSource = uiTransform;
            }
            if (auto* transform = world->GetComponent<TransformComponent>(id))
            {
                const float current = transform->scale.x;
                const bool hasSceneScale =
                    std::abs(transform->scale.x - 1.0f) > 1e-4f ||
                    std::abs(transform->scale.y - 1.0f) > 1e-4f ||
                    std::abs(transform->scale.z - 1.0f) > 1e-4f;
                float scale = std::max(0.0001f, Get_worldScale());
                if (!hasSceneScale && Get_worldAutoScaleBySize() && sizeSource)
                {
                    const float maxSize = std::max(sizeSource->size.x, sizeSource->size.y);
                    if (maxSize > 0.0001f)
                        scale = std::max(0.0001f, Get_worldTargetSize() / maxSize);
                }
                if (!hasSceneScale)
                    transform->scale = { scale, scale, scale };
            }
        };

        auto PlaceWidgetWorld = [&](EntityId id, const DirectX::XMFLOAT3& pos)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* transform = world->GetComponent<TransformComponent>(id))
                transform->position = pos;
        };

        auto ComputeRight = [&](const TransformComponent& actor) -> DirectX::XMFLOAT3
        {
            const float yaw = actor.rotation.y;
            const float cx = std::cos(yaw);
            const float sx = std::sin(yaw);
            return DirectX::XMFLOAT3(cx, 0.0f, -sx);
        };

        const float line = std::max(0.001f, Get_worldLineSpacing());

        auto EnsureParent = [&](EntityId id, EntityId parent)
        {
            if (id == InvalidEntityId || parent == InvalidEntityId)
                return;
            if (world->GetParent(id) != parent)
                world->SetParent(id, parent, false);
        };

        auto PlaceWidgetLocal = [&](EntityId id, const DirectX::XMFLOAT3& localPos)
        {
            if (id == InvalidEntityId)
                return;
            world->SetLocalPosition(id, localPos);
        };

        auto ApplyScreenCommon = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
            {
                widget->space = AliceUI::UISpace::Screen;
                widget->billboard = false;
            }
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
            {
                uiTransform->anchorMin = { 0.5f, 0.5f };
                uiTransform->anchorMax = { 0.5f, 0.5f };
                uiTransform->useAlignment = false;
            }
            if (world->GetParent(id) != InvalidEntityId)
                world->SetParent(id, InvalidEntityId, false);
        };

        auto ApplyScreenLayout = [&](EntityId id, const DirectX::XMFLOAT2& pos)
        {
            if (id == InvalidEntityId)
                return;
            ApplyScreenCommon(id);
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
                uiTransform->position = pos;
        };

        if (m_playerId != InvalidEntityId)
        {
            if (auto* actor = world->GetComponent<TransformComponent>(m_playerId))
            {
                if (!Get_playerUseWorldSpace())
                {
                    const float baseX = Get_playerScreenPosX();
                    const float baseY = Get_playerScreenPosY();
                    const float line = std::max(1.0f, Get_playerScreenLineSpacing());
                    const EntityId playerWidgets[] = {
                        m_playerHpGaugeId,
                        m_weaponGaugeId,
                        m_playerWindowTextId,
                        m_playerStateTextIds[0],
                        m_playerStateTextIds[1],
                        m_playerStateTextIds[2],
                        m_playerStateTextIds[3],
                        m_playerStateTextIds[4]
                    };
                    for (size_t i = 0; i < std::size(playerWidgets); ++i)
                    {
                        ApplyScreenLayout(
                            playerWidgets[i],
                            { baseX, baseY + static_cast<float>(i) * line }
                        );
                    }
                }
                else
                {
                    const bool attachAsChild = Get_worldAttachAsChild();
                    DirectX::XMFLOAT3 base{};
                    if (attachAsChild)
                    {
                        base = { Get_playerRightOffset(), Get_playerHeightOffset(), 0.0f };
                    }
                    else
                    {
                        const DirectX::XMFLOAT3 right = ComputeRight(*actor);
                        base = {
                            actor->position.x + right.x * Get_playerRightOffset(),
                            actor->position.y + Get_playerHeightOffset(),
                            actor->position.z + right.z * Get_playerRightOffset()
                        };
                    }

                    float y = 0.0f;
                    const EntityId playerWidgets[] = {
                        m_playerHpGaugeId,
                        m_weaponGaugeId,
                        m_playerWindowTextId,
                        m_playerStateTextIds[0],
                        m_playerStateTextIds[1],
                        m_playerStateTextIds[2],
                        m_playerStateTextIds[3],
                        m_playerStateTextIds[4]
                    };
                    for (EntityId id : playerWidgets)
                    {
                        ApplyWidgetCommon(id);
                        if (attachAsChild)
                        {
                            EnsureParent(id, m_playerId);
                            PlaceWidgetLocal(id, { base.x, base.y + y, base.z });
                        }
                        else
                        {
                            PlaceWidgetWorld(id, { base.x, base.y + y, base.z });
                        }
                        y -= line;
                    }
                }
            }
        }

        if (m_bossId != InvalidEntityId)
        {
            if (auto* actor = world->GetComponent<TransformComponent>(m_bossId))
            {
                if (!Get_bossUseWorldSpace())
                {
                    const EntityId bossWidgets[] = {
                        m_bossHpGaugeId,
                        m_bossGroggyGaugeId,
                        m_bossWindowTextId,
                        m_bossStateTextIds[0],
                        m_bossStateTextIds[1],
                        m_bossStateTextIds[2],
                        m_bossStateTextIds[3],
                        m_bossStateTextIds[4],
                        m_bossStateTextIds[5]
                    };
                    for (EntityId id : bossWidgets)
                        ApplyScreenCommon(id);
                }
                else
                {
                    const bool attachAsChild = Get_worldAttachAsChild();
                    DirectX::XMFLOAT3 base{};
                    if (attachAsChild)
                    {
                        base = { Get_bossRightOffset(), Get_bossHeightOffset(), 0.0f };
                    }
                    else
                    {
                        const DirectX::XMFLOAT3 right = ComputeRight(*actor);
                        base = {
                            actor->position.x + right.x * Get_bossRightOffset(),
                            actor->position.y + Get_bossHeightOffset(),
                            actor->position.z + right.z * Get_bossRightOffset()
                        };
                    }

                    float y = 0.0f;
                    const EntityId bossWidgets[] = {
                        m_bossHpGaugeId,
                        m_bossGroggyGaugeId,
                        m_bossWindowTextId,
                        m_bossStateTextIds[0],
                        m_bossStateTextIds[1],
                        m_bossStateTextIds[2],
                        m_bossStateTextIds[3],
                        m_bossStateTextIds[4],
                        m_bossStateTextIds[5]
                    };
                    for (EntityId id : bossWidgets)
                    {
                        ApplyWidgetCommon(id);
                        if (attachAsChild)
                        {
                            EnsureParent(id, m_bossId);
                            PlaceWidgetLocal(id, { base.x, base.y + y, base.z });
                        }
                        else
                        {
                            PlaceWidgetWorld(id, { base.x, base.y + y, base.z });
                        }
                        y -= line;
                    }
                }
            }
        }
    }

    void CombatHudScript::UpdateGauge(UIGaugeComponent* gauge, float value, float maxValue)
    {
        if (!gauge)
            return;
        const float maxV = std::max(1e-6f, maxValue);
        gauge->normalized = true;
        gauge->minValue = 0.0f;
        gauge->maxValue = 1.0f;
        gauge->value = std::clamp(value / maxV, 0.0f, 1.0f);
    }

    void CombatHudScript::UpdateStateTexts(UITextComponent* const* texts,
                                           const Combat::ActionState* states,
                                           size_t count,
                                           Combat::ActionState state,
                                           float inactiveSize,
                                           float activeSize)
    {
        if (!texts || !states || count == 0)
            return;

        for (size_t i = 0; i < count; ++i)
        {
            auto* text = texts[i];
            if (!text)
                continue;
            const bool active = (state == states[i]);
            const float alpha = active ? Get_activeTextAlpha() : Get_inactiveTextAlpha();
            const DirectX::XMFLOAT4 baseColor = active ? Get_activeTextColor() : Get_inactiveTextColor();
            text->color = baseColor;
            text->color.w = alpha;
            const float size = active ? activeSize : inactiveSize;
            if (size > 0.0f)
                text->fontSize = size;
        }
    }

    void CombatHudScript::UpdateWindowText(UITextComponent* text, const Combat::ActionFlags& flags, float inactiveSize, float activeSize)
    {
        if (!text)
            return;
        text->text = BuildWindowLabel(flags);
        const bool active = flags.hitActive || flags.guardActive || flags.parryWindowActive || flags.invulnActive || flags.chargeActive;
        const float alpha = active ? Get_activeTextAlpha() : Get_inactiveTextAlpha();
        const DirectX::XMFLOAT4 baseColor = active ? Get_activeWindowTextColor() : Get_inactiveWindowTextColor();
        text->color = baseColor;
        text->color.w = alpha;
        const float size = active ? activeSize : inactiveSize;
        if (size > 0.0f)
            text->fontSize = size;
    }

    std::string CombatHudScript::BuildWindowLabel(const Combat::ActionFlags& flags)
    {
        std::ostringstream oss;
        bool any = false;
        if (flags.hitActive)
        {
            const int combo = std::clamp(flags.attackComboIndex, 0, 3);
            if (combo > 0)
                oss << "Attack" << combo;
            else
                oss << "Attack";
            any = true;
        }
        if (flags.guardActive)
        {
            if (any) oss << " | ";
            oss << "Guard";
            any = true;
        }
        if (flags.parryWindowActive)
        {
            if (any) oss << " | ";
            oss << "Parry";
            any = true;
        }
        if (flags.invulnActive)
        {
            if (any) oss << " | ";
            oss << "Invuln";
            any = true;
        }
        if (flags.chargeActive)
        {
            if (any) oss << " | ";
            const int level = std::clamp(flags.chargeLevel, 0, 3);
            if (level > 0)
                oss << "Charge" << level;
            else
                oss << "Charge";
            any = true;
        }
        if (!any)
            oss << "None";
        return oss.str();
    }
}
