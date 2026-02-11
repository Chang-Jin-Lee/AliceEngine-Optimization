#pragma once
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    // 재사용 가능한 게이지 스크립트.
    // 에디터에서 rootWidgetName, gaugeWidgetName, targetEntityName, targetScriptName을 설정하여
    // 씬마다 이름만 바꿔서 재사용할 수 있습니다.
    class BossHPScript : public IScript
    {
        ALICE_BODY(BossHPScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 에디터에서 설정 가능한 바인딩 대상 ---
		
        ALICE_PROPERTY(std::string, rootWidgetName, "UI_Gauge");              // UI 루트 위젯 이름
		
        ALICE_PROPERTY(std::string, gaugeWidgetName, "HP_Gauge");            // 게이지 바 entity 이름
		
        ALICE_PROPERTY(std::string, targetEntityName, "Cube_HPTest1");       //델리게이트가 있는 entity 이름

        ALICE_PROPERTY(std::string, targetScriptName, "BoxDeligateScript");  // 그 엔티티에 붙어 있는 스크립트 이름

        // HealthComponent를 직접 읽을 엔티티 이름 (설정되면 BoxDeligateScript 대신 사용)
        ALICE_PROPERTY(std::string, healthEntityName, "Boss");

        // 표시 값 (0~maxValue 정규화, changeValue 콜백으로 갱신됨)
        ALICE_PROPERTY(float, maxValue, 100.0f);


        ALICE_PROPERTY(float, nowValue, 100.0f);

        // FillLate가 Fill을 따라오는 속도 (초 단위)
        ALICE_PROPERTY(float, fillLateSmoothTime, 0.25f);



        // Start()에서 이름으로 찾아 바인딩된 게이지 (런타임)
        UIGaugeComponent* TargetGauge = nullptr;
        UIWidgetComponent* TargetWidget = nullptr;

        // 대상 스크립트의 OnValueChanged에 바인딩되는 콜백
        void changeValue(float newValue);

        // 텍스처/컬러 모드 변경 적용
        void ApplyGaugeAppearance();
        float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float dt);

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        // 런타임 변경 감지용 캐시
        bool lastUseColorOnly = false;
        bool lastUseBackground = true;
        std::string lastFillTexture;
        std::string lastFillLateTexture;
        std::string lastBackgroundTexture;
        float fillLateVelocity = 0.0f;
    };
}
