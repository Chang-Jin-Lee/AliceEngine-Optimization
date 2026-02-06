#pragma once

#include <array>
#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    // UIImageComponent를 이름으로 바인딩해서 키(1~6)로 텍스처를 바꾸는 예시
    class UIImageController : public IScript
    {
    public:
        ALICE_BODY(UIImageController);
        void Start() override;
        void Update(float deltaTime) override;

        // UI 루트 이름 (UIWidgetComponent.widgetName)
        std::string rootWidgetName = "UI_Root";

        // 위젯 이름 = 변수명 (UIWidgetComponent.widgetName을 "TargetImage"로 맞추세요)
        ALICE_BIND_WIDGET(UIImageComponent*, TargetImage);

    private:
        // 테스트용 이미지 목록
        std::array<std::string, 6> m_imagePaths = {
            "Resource/Image/Hanako.png",
            "Resource/Image/Hanako_Normal.png",
            "Resource/Image/Hanako_Specular.png",
            "Resource/Image/Yuuka.png",
            "Resource/Image/Yuuka_Normal.png",
            "Resource/Image/Yuuka_Specular.png"
        };
    };
}
