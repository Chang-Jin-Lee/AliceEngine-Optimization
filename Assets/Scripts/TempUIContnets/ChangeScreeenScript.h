#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UICommon.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class ChangeScreeenScript : public IScript
    {
        ALICE_BODY(ChangeScreeenScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 예시 (에디터에서 수정 가능) ---
        ALICE_PROPERTY(float, m_exampleValue, 1.0f);

        // --- UI 토글 기능 속성 ---
        ALICE_PROPERTY(std::string, m_rootWidgetName, "");
        ALICE_PROPERTY(std::string, m_settingButtonName, "UI_Setting");
        ALICE_PROPERTY(std::string, m_button1Name, "");
        ALICE_PROPERTY(std::string, m_button2Name, "");
        ALICE_PROPERTY(std::string, m_ui1Name, "");
        ALICE_PROPERTY(std::string, m_ui2Name, "");

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        void SetUIVisibility(EntityId entityId, AliceUI::UIVisibility visibility);

        UIButtonComponent* m_settingButton = nullptr;
        UIButtonComponent* m_button1 = nullptr;
        UIButtonComponent* m_button2 = nullptr;
        EntityId m_ui1EntityId = InvalidEntityId;
        EntityId m_ui2EntityId = InvalidEntityId;
        bool m_settingButtonClickedPrev = false;
        bool m_button1ClickedPrev = false;
        bool m_button2ClickedPrev = false;
        bool m_isUIOpened = false; // UI가 처음 열렸는지 여부
    };
}
