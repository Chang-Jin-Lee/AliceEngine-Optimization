#include "QuitGameButtonScript.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(QuitGameButtonScript);

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
    }

    void QuitGameButtonScript::Start()
    {
        m_buttonEntityId = InvalidEntityId;
        m_button = nullptr;
        m_quitRequested = false;

        World* world = GetWorld();
        if (!world)
            return;

        EntityId root = FindRootWidgetByName(*world, Get_rootWidgetName());
        if (root == InvalidEntityId)
            root = GetOwnerId();

        const std::string buttonName = Get_buttonWidgetName();
        if (buttonName.empty())
            return;

        m_buttonEntityId = AliceUI::FindWidgetByName(*world, root, buttonName);
        if (m_buttonEntityId == InvalidEntityId)
            m_buttonEntityId = (world->GetEntityName(GetOwnerId()) == buttonName ? GetOwnerId() : InvalidEntityId);

        if (m_buttonEntityId != InvalidEntityId)
            m_button = world->GetComponent<UIButtonComponent>(m_buttonEntityId);

        if (!m_button)
        {
            ALICE_LOG_WARN("[QuitGameButtonScript] Quit button not found: %s", buttonName.c_str());
            return;
        }

        const EntityId ownerId = GetOwnerId();
        const std::uint32_t ownerGen = world->GetEntityGeneration(ownerId);
        const auto isValid = [world, ownerId, ownerGen, self = this]() -> bool
        {
            if (!world)
                return false;
            if (!world->IsEntityValid(ownerId, ownerGen))
                return false;
            const auto* scripts = world->GetScripts(ownerId);
            if (!scripts)
                return false;
            for (const auto& sc : *scripts)
            {
                if (sc.instance.get() == self)
                    return true;
            }
            return false;
        };

        m_button->AddOnReleasedSafe([this]()
        {
            RequestQuit();
        }, isValid);
    }

    void QuitGameButtonScript::Update(float /*deltaTime*/)
    {
        if (m_quitRequested || !m_button)
            return;

        if (m_button->ConsumeClick())
            RequestQuit();
    }

    void QuitGameButtonScript::OnDestroy()
    {
        if (m_button)
            m_button->ClearDelegates();
    }

    void QuitGameButtonScript::RequestQuit()
    {
        if (m_quitRequested)
            return;

        m_quitRequested = true;
        ::PostQuitMessage(0);
    }
}
