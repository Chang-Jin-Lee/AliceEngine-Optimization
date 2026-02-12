#include "BossUiActivationGateScript.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UICommon.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include <rttr/type>
#include "C_BossBrainComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(BossUiActivationGateScript);

    namespace
    {
        bool IsWhiteSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::string Trim(const std::string& src)
        {
            size_t first = 0;
            while (first < src.size() && IsWhiteSpace(src[first]))
                ++first;
            size_t last = src.size();
            while (last > first && IsWhiteSpace(src[last - 1]))
                --last;
            return src.substr(first, last - first);
        }
    }

    void BossUiActivationGateScript::Start()
    {
        m_shownOnce = false;
        m_bindingsDirty = true;
        RefreshBindings();
        SetBossUiVisible(!Get_hideOnStart());
    }

    void BossUiActivationGateScript::Update(float deltaTime)
    {
        (void)deltaTime;

        RefreshBindings();
        const bool brainActivated = IsBossBrainActivated();
        bool desiredVisible = m_uiVisible;

        if (brainActivated)
        {
            m_shownOnce = true;
            desiredVisible = true;
        }
        else if ((Get_hideOnStart() && !m_shownOnce) || Get_rehideWhenInactive())
        {
            desiredVisible = false;
        }

        if (m_bindingsDirty || desiredVisible != m_uiVisible)
            SetBossUiVisible(desiredVisible);
    }

    void BossUiActivationGateScript::RefreshBindings()
    {
        World* world = GetWorld();
        if (!world)
            return;

        const EntityId prevBossId = m_bossId;
        const EntityId prevRootWidgetId = m_rootWidgetId;
        const std::vector<EntityId> prevExtraWidgetIds = m_extraWidgetIds;

        m_bossId = InvalidEntityId;
        if (!Get_bossEntityName().empty())
        {
            const GameObject bossGo = world->FindGameObject(Get_bossEntityName());
            if (bossGo.IsValid())
                m_bossId = bossGo.id();
        }

        m_rootWidgetId = FindWidgetByName(*world, Get_rootWidgetName());

        m_extraWidgetIds.clear();
        const auto names = ParseCsv(Get_additionalWidgetNamesCsv());
        m_extraWidgetIds.reserve(names.size());
        for (const std::string& widgetName : names)
            m_extraWidgetIds.push_back(FindWidgetByName(*world, widgetName));

        if (m_bossId != prevBossId
            || m_rootWidgetId != prevRootWidgetId
            || m_extraWidgetIds != prevExtraWidgetIds)
        {
            m_bindingsDirty = true;
        }
    }

    bool BossUiActivationGateScript::IsBossBrainActivated() const
    {
        World* world = GetWorld();
        if (!world || m_bossId == InvalidEntityId)
            return false;

        auto* scripts = world->GetScripts(m_bossId);
        if (!scripts)
            return false;

        const std::string& brainScriptName = Get_bossBrainScriptName();
        for (const auto& scriptComp : *scripts)
        {
            if (!brainScriptName.empty() && scriptComp.scriptName != brainScriptName)
                continue;
            if (!scriptComp.instance)
                continue;

            // Prefer reflected property read to avoid fragile cast behavior across reload boundaries.
            rttr::instance instance(*scriptComp.instance);
            rttr::type scriptType = instance.get_derived_type();
            if (!scriptType.is_valid())
                scriptType = instance.get_type();

            const rttr::property activatedProp = scriptType.get_property("m_brainActivated");
            if (activatedProp.is_valid())
            {
                rttr::variant activatedValue = activatedProp.get_value(instance);
                if (activatedValue.can_convert<bool>())
                    return activatedValue.get_value<bool>();
            }

            if (auto* brain = dynamic_cast<C_BossBrainComponent*>(scriptComp.instance.get()))
                return brain->Get_m_brainActivated();
        }

        return false;
    }

    void BossUiActivationGateScript::SetBossUiVisible(bool visible)
    {
        World* world = GetWorld();
        if (!world)
            return;

        const AliceUI::UIVisibility visibility = visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;

        auto setWidgetVisibility = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
                widget->visibility = visibility;
        };

        setWidgetVisibility(m_rootWidgetId);
        for (const EntityId id : m_extraWidgetIds)
            setWidgetVisibility(id);

        m_uiVisible = visible;
        m_bindingsDirty = false;
    }

    std::vector<std::string> BossUiActivationGateScript::ParseCsv(const std::string& csv)
    {
        std::vector<std::string> out;
        if (csv.empty())
            return out;

        size_t start = 0;
        while (start <= csv.size())
        {
            const size_t comma = csv.find(',', start);
            const size_t end = (comma == std::string::npos) ? csv.size() : comma;
            const std::string token = Trim(csv.substr(start, end - start));
            if (!token.empty())
                out.push_back(token);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        return out;
    }

    EntityId BossUiActivationGateScript::FindWidgetByName(World& world, const std::string& name)
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
}
