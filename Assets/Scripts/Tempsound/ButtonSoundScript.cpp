#include "ButtonSoundScript.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "UISoundScript.h"

namespace Alice
{
    REGISTER_SCRIPT(ButtonSoundScript);

    namespace
    {
        EntityId FindRootWidgetByName(World& world, const std::string& name)
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

        UISoundScript* FindUISound(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;
            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;
            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;
            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "UISoundScript" && sc.instance)
                    return static_cast<UISoundScript*>(sc.instance.get());
            }
            return nullptr;
        }
    }

    void ButtonSoundScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        EntityId root = GetOwnerId();
        if (!Get_rootWidgetName().empty())
        {
            root = FindRootWidgetByName(*w, Get_rootWidgetName());
            if (root == InvalidEntityId)
            {
                ALICE_LOG_WARN("[ButtonSoundScript] Root widget not found: %s", Get_rootWidgetName().c_str());
                return;
            }
        }

        EntityId buttonEntity = root;
        if (!Get_buttonWidgetName().empty())
        {
            buttonEntity = AliceUI::FindWidgetByName(*w, root, Get_buttonWidgetName());
        }

        m_buttonEntityId = buttonEntity;
        m_button = (buttonEntity != InvalidEntityId) ? w->GetComponent<UIButtonComponent>(buttonEntity) : nullptr;
        if (!m_button)
        {
            ALICE_LOG_WARN("[ButtonSoundScript] UIButton not found: %s", Get_buttonWidgetName().c_str());
            return;
        }

        m_uiSound = FindUISound(*w, Get_uiSoundEntityName());

        const EntityId ownerId = GetOwnerId();
        const std::uint32_t ownerGen = w->GetEntityGeneration(ownerId);
        const auto isValid = [w, ownerId, ownerGen, self = this]() -> bool
        {
            if (!w)
                return false;
            if (!w->IsEntityValid(ownerId, ownerGen))
                return false;
            const auto* scripts = w->GetScripts(ownerId);
            if (!scripts)
                return false;
            for (const auto& sc : *scripts)
            {
                if (sc.instance.get() == self)
                    return true;
            }
            return false;
        };

        if (Get_enableHover())
        {
            m_button->AddOnHoveredSafe([this]()
            {
                if (m_uiSound)
                    m_uiSound->PlayHover();
            }, isValid);
        }

        if (Get_enableClick())
        {
            m_button->AddOnReleasedSafe([this]()
            {
                if (m_uiSound)
                    m_uiSound->PlayClick();
            }, isValid);
        }
    }

    void ButtonSoundScript::OnDestroy()
    {
        if (m_button)
            m_button->ClearDelegates();
    }
}
