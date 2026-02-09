#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    // 게이지바 수치에 따라 배경 이미지를 변경하는 스크립트
    class PlayerGauge : public IScript
    {
        ALICE_BODY(PlayerGauge);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 (에디터에서 수정 가능) ---
        
        // 게이지바 위젯 이름
        ALICE_PROPERTY(std::string, gaugeWidgetName, "");
        
        // 이미지 컴포넌트 위젯 이름
        ALICE_PROPERTY(std::string, imageWidgetName, "");
        
        // 트리거가 될 수치 (정규화된 값, 기본값 0.3)
        ALICE_PROPERTY(float, triggerThreshold, 0.3f);
        
        // 평소 이미지 경로
        ALICE_PROPERTY(std::string, normalImagePath, "");
        
        // 바뀔 이미지 경로 (게이지바가 threshold 이하일 때)
        ALICE_PROPERTY(std::string, lowValueImagePath, "");

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        // 런타임 캐시
        UIGaugeComponent* TargetGauge = nullptr;
        UIImageComponent* TargetImage = nullptr;
        
        // 이전 상태 추적 (불필요한 이미지 변경 방지)
        bool wasLowValue = false;
        
        // 정규화된 게이지 값 계산
        float GetNormalizedGaugeValue() const;
    };
}
