#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    // UIButtonComponent에 델리게이트를 등록해서 눌림/호버/뗌 이벤트를 출력하는 예시
    class UIButtonController : public IScript
    {
    public:
        ALICE_BODY(UIButtonController);
        void Start() override;
        void OnDestroy() override;

        // UI 루트 이름
        std::string rootWidgetName = "UI_Root";

        // 위젯 이름 = 변수명 (UIWidgetComponent.widgetName을 "TargetButton"으로 맞추세요)
        ALICE_BIND_WIDGET(UIButtonComponent*, TargetButton);
    };
}
