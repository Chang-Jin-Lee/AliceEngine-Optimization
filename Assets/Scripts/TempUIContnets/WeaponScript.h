#pragma once
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Foundation/Delegate.h"
#include "Runtime/ECS/World.h"

namespace Alice
{
    // 무기 게이지 스크립트
    // 델리게이트 바인딩과 SmoothDamp를 사용하며, Egoweapon_Gauge_IN.png 이미지를 사용합니다.
    class WeaponScript : public IScript
    {
        ALICE_BODY(WeaponScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 에디터에서 설정 가능한 바인딩 대상 ---
		
        ALICE_PROPERTY(std::string, rootWidgetName, "UI_Gauge");              // UI 루트 위젯 이름
		
        ALICE_PROPERTY(std::string, gaugeWidgetName, "HP_Gauge");            // 게이지 바 entity 이름
		
        ALICE_PROPERTY(std::string, targetEntityName, "Cube_HPTest1");       //델리게이트가 있는 entity 이름

        ALICE_PROPERTY(std::string, targetScriptName, "BoxDeligateScript");  // 그 엔티티에 붙어 있는 스크립트 이름

        // HealthComponent를 직접 읽을 엔티티 이름 (설정되면 BoxDeligateScript 대신 사용)
        ALICE_PROPERTY(std::string, healthEntityName, "Player(Tia)");

        // 표시 값 (0~maxValue 정규화, changeValue 콜백으로 갱신됨)
        ALICE_PROPERTY(float, maxValue, 100.0f);

        ALICE_PROPERTY(float, nowValue, 100.0f);

        // FillLate가 Fill을 따라오는 속도 (초 단위)
        ALICE_PROPERTY(float, fillLateSmoothTime, 0.25f);

        // 사용할 이미지 텍스처 경로 (1장만 사용)
        ALICE_PROPERTY(std::string, texturePath, "Resource/Test/4_Resources/UI/StateBar/Egoweapon_Gauge_IN.png");

        // UI_Hit_VignetEffect 위젯 이름 (게이지가 threshold 이하일 때만 표시)
        ALICE_PROPERTY(std::string, vignetEffectWidgetName, "UI_Hit_VignetEffect");
        
        // 트리거 임계값 (기본값 0.3)
        ALICE_PROPERTY(float, visibilityThreshold, 0.3f);

        // Start()에서 이름으로 찾아 바인딩된 게이지 (런타임)
        UIGaugeComponent* TargetGauge = nullptr;
        UIWidgetComponent* TargetWidget = nullptr;

        // 대상 스크립트의 OnValueChanged에 바인딩되는 콜백
        void changeValue(float newValue);

        float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float dt);

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        float fillLateVelocity = 0.0f;
        UIWidgetComponent* VignetEffectWidget = nullptr;  // UI_Hit_VignetEffect 위젯
        bool wasLowValue = false;  // 이전 상태 추적
    };
}
