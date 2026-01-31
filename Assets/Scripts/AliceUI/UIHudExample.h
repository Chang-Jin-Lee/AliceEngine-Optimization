#pragma once

#include <string>
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    class UIHudExample : public IScript
    {
    public:
        ALICE_BODY(UIHudExample);
        void Start() override;
        void Update(float deltaTime) override;

    public:
        // 인스펙터에서 설정할 루트 위젯 이름
        std::string rootWidgetName = "UI_Root";

        // BindWidget 대상 (헤더에서 선언+등록)
        ALICE_BIND_WIDGET_NAMED(UIButtonComponent*, okButton, "OK_Button");
        ALICE_BIND_WIDGET_NAMED(UITextComponent*, titleText, "Title_Text");
        ALICE_BIND_WIDGET_OPTIONAL_NAMED(UIGaugeComponent*, hpGauge, "HP_Gauge");
    };
}
