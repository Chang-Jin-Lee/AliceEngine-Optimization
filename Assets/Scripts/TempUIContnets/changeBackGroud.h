#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    class C_CombatSessionComponent;

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

        // 전투 세션 엔티티 이름 (광폭화 상태 조회용)
        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        
        // 레거시 값(현재 로직에서 미사용)
        ALICE_PROPERTY(float, triggerThreshold, 0.3f);
        
        // 평소 이미지 경로
        ALICE_PROPERTY(std::string, normalImagePath, "");
        
        // 쿨다운 중일 때 사용할 이미지 경로
        ALICE_PROPERTY(std::string, lowValueImagePath, "");

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        // 런타임 캐시
        UIGaugeComponent* TargetGauge = nullptr;
        UIImageComponent* TargetImage = nullptr;
        C_CombatSessionComponent* TargetSession = nullptr;
        
        // 이전 상태 추적 (불필요한 이미지 변경 방지)
        bool wasLowValue = false;
        
        void TryResolveSession();
    };
}
