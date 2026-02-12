#pragma once

#include <string>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    struct UIButtonComponent;

    // 특정 UI 버튼 클릭 시 게임 종료(데스크톱 종료) 처리
    class QuitGameButtonScript : public IScript
    {
        ALICE_BODY(QuitGameButtonScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        ALICE_PROPERTY(std::string, rootWidgetName, "UI_Title");
        ALICE_PROPERTY(std::string, buttonWidgetName, "UI_Quit");

    private:
        void RequestQuit();

        EntityId m_buttonEntityId = InvalidEntityId;
        UIButtonComponent* m_button = nullptr;
        bool m_quitRequested = false;
    };
}
