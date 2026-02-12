#include "PlayerRageStateIconScript.h"

#include <algorithm>

#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/UI/UIEffectComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

#include "C_CombatSessionComponent.h"

namespace Alice
{
    namespace
    {
        C_CombatSessionComponent* FindSession(World& world, const std::string& entityName)
        {
            if (entityName.empty())
                return nullptr;

            const GameObject go = world.FindGameObject(entityName);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& script : *scripts)
            {
                if (script.scriptName == "C_CombatSessionComponent" && script.instance)
                    return static_cast<C_CombatSessionComponent*>(script.instance.get());
            }

            return nullptr;
        }

        EntityId FindWidgetEntityByName(World& world, const std::string& widgetName)
        {
            if (widgetName.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string resolvedName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!resolvedName.empty() && resolvedName == widgetName)
                    return id;
            }

            return InvalidEntityId;
        }
    }

    REGISTER_SCRIPT(PlayerRageStateIconScript);

    void PlayerRageStateIconScript::Start()
    {
        m_session = nullptr;
        m_cooldownIconId = InvalidEntityId;
        m_durationIconId = InvalidEntityId;
        m_cooldownEffect = nullptr;
        m_durationEffect = nullptr;

        ResolveSession();
        ResolveIcons();
    }

    void PlayerRageStateIconScript::Update(float deltaTime)
    {
        (void)deltaTime;

        ResolveSession();
        ResolveIcons();

        bool showCooldown = false;
        bool showDuration = true;
        float cooldownFill = 1.0f;
        float durationFill = 1.0f;

        if (m_session)
        {
            const bool rageActive = m_session->IsPlayerRageActive();
            const float cooldownRemainNorm = std::clamp(m_session->GetPlayerRageCooldownNormalized(), 0.0f, 1.0f);
            cooldownFill = std::clamp(1.0f - cooldownRemainNorm, 0.0f, 1.0f);

            const float rageDurationTotal = std::max(0.0f, m_session->Get_m_rageDurationSec());
            const float rageRemainSec = std::max(0.0f, m_session->GetPlayerRageRemainingSec());

            if (rageActive)
            {
                showCooldown = false;
                showDuration = true;
                if (rageDurationTotal > 1e-6f)
                    durationFill = std::clamp(rageRemainSec / rageDurationTotal, 0.0f, 1.0f);
                else
                    durationFill = 1.0f;
            }
            else
            {
                constexpr float kReadyEpsilon = 1e-4f;
                if (cooldownFill < (1.0f - kReadyEpsilon))
                {
                    showCooldown = true;
                    showDuration = false;
                }
                else
                {
                    showCooldown = false;
                    showDuration = true;
                    durationFill = 1.0f;
                }
            }
        }

        SetIconVisible(m_cooldownIconId, showCooldown);
        SetIconVisible(m_durationIconId, showDuration);

        SetRadialFill(m_cooldownEffect, cooldownFill);
        SetRadialFill(m_durationEffect, durationFill);
    }

    void PlayerRageStateIconScript::ResolveSession()
    {
        if (m_session)
            return;

        World* world = GetWorld();
        if (!world)
            return;

        m_session = FindSession(*world, Get_sessionEntityName());
    }

    void PlayerRageStateIconScript::ResolveIcons()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto resolveSingle = [&](const std::string& widgetName, EntityId& idRef, UIEffectComponent*& effectRef)
        {
            if (idRef == InvalidEntityId)
                idRef = FindWidgetEntityByName(*world, widgetName);

            if (idRef == InvalidEntityId)
            {
                effectRef = nullptr;
                return;
            }

            if (auto* widget = world->GetComponent<UIWidgetComponent>(idRef))
            {
                widget->interactable = false;
                widget->raycastTarget = false;
            }

            effectRef = world->GetComponent<UIEffectComponent>(idRef);
            ApplyRadialDefaults(effectRef);
        };

        resolveSingle(Get_cooldownIconWidgetName(), m_cooldownIconId, m_cooldownEffect);
        resolveSingle(Get_durationIconWidgetName(), m_durationIconId, m_durationEffect);
    }

    void PlayerRageStateIconScript::ApplyRadialDefaults(UIEffectComponent* effect) const
    {
        if (!effect)
            return;

        const float inner = std::clamp(Get_radialInner(), 0.0f, 0.5f);
        const float outer = std::clamp(Get_radialOuter(), inner, 0.5f);

        effect->radialEnabled = true;
        effect->radialInner = inner;
        effect->radialOuter = outer;
        effect->radialSoftness = std::clamp(Get_radialSoftness(), 0.0f, 0.1f);
        effect->radialDim = std::clamp(Get_radialDim(), 0.0f, 1.0f);
        effect->radialClockwise = Get_radialClockwise();
    }

    void PlayerRageStateIconScript::SetIconVisible(EntityId id, bool visible) const
    {
        if (id == InvalidEntityId)
            return;

        World* world = GetWorld();
        if (!world)
            return;

        if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
            widget->visibility = visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;
    }

    void PlayerRageStateIconScript::SetRadialFill(UIEffectComponent* effect, float fill01) const
    {
        if (!effect)
            return;

        effect->radialEnabled = true;
        effect->radialFill = std::clamp(fill01, 0.0f, 1.0f);
    }
}

