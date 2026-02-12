#pragma once

#include <string>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UICommon.h"

namespace Alice
{
    struct UIButtonComponent;

    class PauseMenuControllerScript : public IScript
    {
        ALICE_BODY(PauseMenuControllerScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        ALICE_PROPERTY(std::string, rootWidgetName, "UI_PauseMenuRoot");
        ALICE_PROPERTY(std::string, panelWidgetName, "UI_PausePanel");
        ALICE_PROPERTY(std::string, mainMenuButtonWidgetName, "UI_PauseMainMenuButton");
        ALICE_PROPERTY(std::string, quitButtonWidgetName, "UI_PauseQuitButton");
        ALICE_PROPERTY(std::string, titleScenePath, "Assets/Scenes/MainGameLoopScene/TitleScene.scene");
        ALICE_PROPERTY(int, gamepadPlayerIndex, 0);

        ALICE_PROPERTY(bool, setCursorVisibleOnPause, true);
        ALICE_PROPERTY(bool, setCursorLockedOnPause, false);
        ALICE_PROPERTY(bool, setCursorVisibleOnResume, false);
        ALICE_PROPERTY(bool, setCursorLockedOnResume, true);

    private:
        void ResolveWidgets();
        void RefreshButtonPointers();
        void SetPauseState(bool paused);
        bool ShouldTogglePause() const;
        void RequestTitleScene();
        void RequestQuit();

        static void SetSubtreeVisibility(class World& world, EntityId root, AliceUI::UIVisibility visibility);

        EntityId m_rootWidgetId = InvalidEntityId;
        EntityId m_panelWidgetId = InvalidEntityId;
        EntityId m_mainMenuButtonId = InvalidEntityId;
        EntityId m_quitButtonId = InvalidEntityId;

        UIButtonComponent* m_mainMenuButton = nullptr;
        UIButtonComponent* m_quitButton = nullptr;

        bool m_paused = false;
        bool m_sceneChangeRequested = false;
        bool m_quitRequested = false;
    };
}
