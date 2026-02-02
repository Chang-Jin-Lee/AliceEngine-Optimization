#include "BossHPScript.h"
#include <algorithm>
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "BoxDeligateScript.h"
#include "Runtime/ECS/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(BossHPScript);

    namespace
    {
        EntityId SearchRootWidgetByName(World& world, const std::string& name)
        {
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    void BossHPScript::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        // UI root 찾기
        const EntityId root = SearchRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[BossHPScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // 게이지 위젯 이름으로 찾기
        const EntityId gaugeEntity = AliceUI::FindWidgetByName(*w, root, Get_gaugeWidgetName());
        if (gaugeEntity == InvalidEntityId)
        {
            ALICE_LOG_WARN("[BossHPScript] Gauge widget not found: %s", Get_gaugeWidgetName().c_str());
            return;
        }

        TargetGauge = w->GetComponent<UIGaugeComponent>(gaugeEntity);
        TargetWidget = w->GetComponent<UIWidgetComponent>(gaugeEntity);

        if (!TargetGauge || !TargetWidget)
        {
            ALICE_LOG_WARN("[BossHPScript] Gauge or Widget component not found: %s", Get_gaugeWidgetName().c_str());
            return;
        }

        // BossHPScript는 기본 쉐이더 사용 (GaugeCustom 쉐이더 사용 안 함)
        // 기본 쉐이더는 텍스처가 없으면 색상만 사용하므로 노란색 바가 나옴
        
        // WeaponScript가 이미 Circle.png를 설정했는지 확인
        // Circle.png가 설정되어 있으면 이 게이지는 WeaponScript가 사용 중
        if (!TargetGauge->fillTexture.empty() && TargetGauge->fillTexture == "Resource/Image/Circle.png")
        {
            ALICE_LOG_WARN("[BossHPScript] Target gauge already uses Circle.png. This gauge is for WeaponScript.");
            return;
        }
        
        TargetWidget->shaderName = "Default";
        
        // FillLate 쉐이더도 기본 쉐이더 사용
        TargetGauge->fillLateShaderName = "Default";

        //  대상 엔티티/스크립트 찾기 및 OnValueChanged 바인딩
        // Boss HP gauge appearance (color-only)
        // 텍스처를 빈 문자열로 설정하여 색상만 사용
        TargetGauge->fillTexture = "";
        TargetGauge->fillLateTexture = "";  // FillLate도 텍스처 없이 색상만 사용
        TargetGauge->backgroundTexture = "";
        TargetGauge->useFillLate = true;
        TargetGauge->useBackground = true;
        TargetGauge->fillLateSmoothing = 0.0f;
        TargetGauge->fillLateValue = TargetGauge->value;
        TargetGauge->fillLateDisplayedValue = TargetGauge->value;
        fillLateVelocity = 0.0f;

        GameObject go = w->FindGameObject(Get_targetEntityName());
        if (!go.IsValid())
        {
            ALICE_LOG_WARN("[BossHPScript] Target entity not found: %s", Get_targetEntityName().c_str());
            return;
        }

        auto* scripts = w->GetScripts(go.id());
        if (!scripts) return;

        for (auto& sc : *scripts)
        {
            if (sc.scriptName == Get_targetScriptName() && sc.instance)
            {
                ALICE_LOG_INFO("[BossHPScript] script on %s: %s (instance=%d)",
                    Get_targetEntityName().c_str(),
                    sc.scriptName.c_str(),
                    sc.instance ? 1 : 0);

                auto* box = static_cast<BoxDeligateScript*>(sc.instance.get());
                box->OnBossHPChanged.BindObject(this, &BossHPScript::changeValue);
                // 초기값 반영 (바인딩 직후 델리게이트 실행)
                box->OnBossHPChanged.Execute(box->Get_BossHP_Value());
                break;
            }
        }
    }

    void BossHPScript::changeValue(float newValue)
    {
        if (TargetGauge)
        {
            const float max = std::max(1e-6f, Get_maxValue());
            TargetGauge->value = std::clamp(newValue / max, 0.0f, 1.0f);
            nowValue = TargetGauge->value;
        }
    }

    void BossHPScript::Update(float deltaTime)
    {

        // changeValue 콜백으로 이미 갱신되므로 Update에서는 추가 처리 불필요
        // (콜백 기반이 아닌 폴링이 필요하면 여기서 처리)
        if (!TargetGauge)
            return;

        if (deltaTime <= 0.0f)
            return;

        const float smoothTime = Get_fillLateSmoothTime();
        if (smoothTime <= 0.0f)
        {
            TargetGauge->fillLateValue = TargetGauge->value;
            TargetGauge->fillLateDisplayedValue = TargetGauge->value;
            fillLateVelocity = 0.0f;
            return;
        }

        const float current = TargetGauge->fillLateValue;
        const float target = TargetGauge->value;
        const float output = SmoothDamp(current, target, fillLateVelocity, smoothTime, deltaTime);
        TargetGauge->fillLateValue = output;
        TargetGauge->fillLateDisplayedValue = output;
    }

    float BossHPScript::SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float dt)
    {
        float maxSpeed = 2.0f;

        smoothTime = std::max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * dt;
        // exp 계산 대신 테일러 급수 근사
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        float change = current - target;
        float originalTarget = target;
        float maxChange = maxSpeed * smoothTime;
        change = std::clamp(change, -maxChange, maxChange);

        target = current - change;
        float temp = (currentVelocity + omega * change) * dt;
        currentVelocity = (currentVelocity - omega * temp) * exp;
        float output = target + (change + temp) * exp;
        if ((originalTarget - current > 0.0f) == (output > originalTarget))
        {
            output = originalTarget;
            currentVelocity = (dt > 0.0f) ? (output - originalTarget) / dt : 0.0f;
        }

        return output;
    }

    void BossHPScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
