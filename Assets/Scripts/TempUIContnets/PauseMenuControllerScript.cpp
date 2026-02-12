#include "PauseMenuControllerScript.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(PauseMenuControllerScript);

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

    void PauseMenuControllerScript::Start()
    {
        m_rootWidgetId = InvalidEntityId;
        m_panelWidgetId = InvalidEntityId;
        m_mainMenuButtonId = InvalidEntityId;
        m_quitButtonId = InvalidEntityId;
        m_mainMenuButton = nullptr;
        m_quitButton = nullptr;
        m_paused = false;
        m_sceneChangeRequested = false;
        m_quitRequested = false;

        World* world = GetWorld();
        if (!world)
            return;

        ResolveWidgets();
        RefreshButtonPointers();
        SetPauseState(false);

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

        if (m_mainMenuButton)
        {
            m_mainMenuButton->AddOnReleasedSafe([this]()
            {
                RequestTitleScene();
            }, isValid);
        }
        else
        {
            ALICE_LOG_WARN("[PauseMenuControllerScript] Main menu button not found: %s",
                Get_mainMenuButtonWidgetName().c_str());
        }

        if (m_quitButton)
        {
            m_quitButton->AddOnReleasedSafe([this]()
            {
                RequestQuit();
            }, isValid);
        }
        else
        {
            ALICE_LOG_WARN("[PauseMenuControllerScript] Quit button not found: %s",
                Get_quitButtonWidgetName().c_str());
        }
    }

    void PauseMenuControllerScript::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        if (!input)
            return;

        if ((!m_mainMenuButton && !Get_mainMenuButtonWidgetName().empty())
            || (!m_quitButton && !Get_quitButtonWidgetName().empty()))
        {
            ResolveWidgets();
            RefreshButtonPointers();
        }

        if (!m_sceneChangeRequested && !m_quitRequested && ShouldTogglePause())
            SetPauseState(!m_paused);

        if (m_mainMenuButton && m_mainMenuButton->ConsumeClick())
            RequestTitleScene();

        if (m_quitButton && m_quitButton->ConsumeClick())
            RequestQuit();
    }

    void PauseMenuControllerScript::OnDestroy()
    {
        if (m_mainMenuButton)
            m_mainMenuButton->ClearDelegates();
        if (m_quitButton)
            m_quitButton->ClearDelegates();
    }

    void PauseMenuControllerScript::ResolveWidgets()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_rootWidgetId == InvalidEntityId)
        {
            if (!Get_rootWidgetName().empty())
                m_rootWidgetId = FindRootWidgetByName(*world, Get_rootWidgetName());

            if (m_rootWidgetId == InvalidEntityId)
                m_rootWidgetId = GetOwnerId();
        }

        if (m_rootWidgetId == InvalidEntityId)
            return;

        if (m_panelWidgetId == InvalidEntityId && !Get_panelWidgetName().empty())
            m_panelWidgetId = AliceUI::FindWidgetByName(*world, m_rootWidgetId, Get_panelWidgetName());

        if (m_mainMenuButtonId == InvalidEntityId && !Get_mainMenuButtonWidgetName().empty())
            m_mainMenuButtonId = AliceUI::FindWidgetByName(*world, m_rootWidgetId, Get_mainMenuButtonWidgetName());

        if (m_quitButtonId == InvalidEntityId && !Get_quitButtonWidgetName().empty())
            m_quitButtonId = AliceUI::FindWidgetByName(*world, m_rootWidgetId, Get_quitButtonWidgetName());
    }

    void PauseMenuControllerScript::RefreshButtonPointers()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_mainMenuButtonId != InvalidEntityId)
            m_mainMenuButton = world->GetComponent<UIButtonComponent>(m_mainMenuButtonId);
        if (m_quitButtonId != InvalidEntityId)
            m_quitButton = world->GetComponent<UIButtonComponent>(m_quitButtonId);
    }

    void PauseMenuControllerScript::SetPauseState(bool paused)
    {
        auto* input = Input();
        World* world = GetWorld();
        if (!input || !world)
            return;

        ResolveWidgets();

        m_paused = paused;
        input->StopDeltaTime(paused);

        if (paused)
        {
            input->SetCursorVisible(Get_setCursorVisibleOnPause());
            input->SetCursorLocked(Get_setCursorLockedOnPause());
        }
        else
        {
            input->SetCursorVisible(Get_setCursorVisibleOnResume());
            input->SetCursorLocked(Get_setCursorLockedOnResume());
        }

        if (m_rootWidgetId != InvalidEntityId)
        {
            SetSubtreeVisibility(*world,
                m_rootWidgetId,
                paused ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed);
        }
        else
        {
            ALICE_LOG_WARN("[PauseMenuControllerScript] Root widget not found: %s", Get_rootWidgetName().c_str());
        }
    }

    bool PauseMenuControllerScript::ShouldTogglePause() const
    {
        auto* input = Input();
        if (!input)
            return false;

        if (input->GetKeyDown(KeyCode::Escape))
            return true;

        int gamepadIndex = Get_gamepadPlayerIndex();
        if (gamepadIndex < 0)
            gamepadIndex = 0;

        if (!input->GetGamepadConnected(gamepadIndex))
            return false;

        return input->GetGamepadButtonDown(GamepadButton::Start, gamepadIndex);
    }

    void PauseMenuControllerScript::RequestTitleScene()
    {
        if (m_sceneChangeRequested || m_quitRequested)
            return;

        auto* scenes = Scenes();
        if (!scenes)
        {
            ALICE_LOG_WARN("[PauseMenuControllerScript] SceneManager not available");
            return;
        }

        const std::string path = Get_titleScenePath();
        if (path.empty())
        {
            ALICE_LOG_WARN("[PauseMenuControllerScript] Title scene path is empty");
            return;
        }

        SetPauseState(false);
        m_sceneChangeRequested = true;
        scenes->LoadSceneFileRequest(path.c_str());
    }

    void PauseMenuControllerScript::RequestQuit()
    {
        if (m_quitRequested)
            return;

        SetPauseState(false);
        m_quitRequested = true;
        ::PostQuitMessage(0);
    }

    void PauseMenuControllerScript::SetSubtreeVisibility(World& world, EntityId root, AliceUI::UIVisibility visibility)
    {
        if (root == InvalidEntityId)
            return;

        if (auto* widget = world.GetComponent<UIWidgetComponent>(root))
            widget->visibility = visibility;

        for (EntityId child : world.GetChildren(root))
            SetSubtreeVisibility(world, child, visibility);
    }
}
