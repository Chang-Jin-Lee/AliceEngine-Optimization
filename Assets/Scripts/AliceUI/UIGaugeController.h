#pragma once

#include <string>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "AliceUI/BindWidget.h"

namespace Alice
{
    // Q/W 키로 게이지 값을 조절하는 예시
    class UIGaugeController : public IScript
    {
    public:
        ALICE_BODY(UIGaugeController);
        void Start() override;
        void Update(float deltaTime) override;

        // UI 루트 이름
        std::string rootWidgetName = "UI_Root";

        // 위젯 이름 = 변수명 (UIWidgetComponent.widgetName을 "TargetGauge"로 맞추세요)
        ALICE_BIND_WIDGET(UIGaugeComponent*, TargetGauge);
    };
}
