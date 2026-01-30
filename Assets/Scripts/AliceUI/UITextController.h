#pragma once

#include <string>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "AliceUI/BindWidget.h"

namespace Alice
{
    // 키 입력에 따라 텍스트를 갱신하는 예시
    class UITextController : public IScript
    {
    public:
        ALICE_BODY(UITextController);
        void Start() override;
        void Update(float deltaTime) override;

        // UI 루트 이름
        std::string rootWidgetName = "UI_Root";

        // 위젯 이름 = 변수명 (UIWidgetComponent.widgetName을 "TargetText"로 맞추세요)
        ALICE_BIND_WIDGET(UITextComponent*, TargetText);
    };
}
