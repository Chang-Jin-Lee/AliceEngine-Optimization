#include "WeaponScript.h"
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
    REGISTER_SCRIPT(WeaponScript);

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

    void WeaponScript::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        // UI root 찾기
        const EntityId root = SearchRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[WeaponScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // 게이지 위젯 이름으로 찾기
        const EntityId gaugeEntity = AliceUI::FindWidgetByName(*w, root, Get_gaugeWidgetName());
        if (gaugeEntity == InvalidEntityId)
        {
            ALICE_LOG_WARN("[WeaponScript] Gauge widget not found: %s", Get_gaugeWidgetName().c_str());
            return;
        }

        TargetGauge = w->GetComponent<UIGaugeComponent>(gaugeEntity);
        TargetWidget = w->GetComponent<UIWidgetComponent>(gaugeEntity);

        if (!TargetGauge || !TargetWidget)
        {
            ALICE_LOG_WARN("[WeaponScript] Gauge or Widget component not found: %s", Get_gaugeWidgetName().c_str());
            return;
        }

        // WeaponScript
        // (WeaponScript용)
        const std::string texture = Get_texturePath();
        if (!texture.empty())
        {
            TargetGauge->fillTexture = texture;
        }
        TargetGauge->fillLateTexture = "Resource/Image/Circle.png";  // 
        TargetGauge->backgroundTexture = "Resource/Image/Circle.png";
        // WeaponScript는 GaugeCustom 쉐이더 사용 (빈 영역 효과를 위해)
        TargetWidget->shaderName = "GaugeCustom";
        TargetGauge->useCustomShader = true;
        
        // FillLate 쉐이더도 GaugeCustom 사용
        TargetGauge->fillLateShaderName = "GaugeCustom";

        // FillLate 
        TargetGauge->useFillLate = true;
        TargetGauge->useBackground = true;
        TargetGauge->fillLateSmoothing = 0.0f;  // SmoothDamp에서 처리하므로 0으로 설정
        TargetGauge->fillLateValue = TargetGauge->value;
        TargetGauge->fillLateDisplayedValue = TargetGauge->value;
        fillLateVelocity = 0.0f;

        // 대상 엔티티/스크립트 찾기 및 OnValueChanged 바인딩
        GameObject go = w->FindGameObject(Get_targetEntityName());
        if (!go.IsValid())
        {
            ALICE_LOG_WARN("[WeaponScript] Target entity not found: %s", Get_targetEntityName().c_str());
            return;
        }

        auto* scripts = w->GetScripts(go.id());
        if (!scripts) return;

        for (auto& sc : *scripts)
        {
            if (sc.scriptName == Get_targetScriptName() && sc.instance)
            {
                ALICE_LOG_INFO("[WeaponScript] script on %s: %s (instance=%d)",
                    Get_targetEntityName().c_str(),
                    sc.scriptName.c_str(),
                    sc.instance ? 1 : 0);

                auto* box = static_cast<BoxDeligateScript*>(sc.instance.get());
                box->OnWeaponHPChanged.BindObject(this, &WeaponScript::changeValue);
                // 초기값 반영 (바인딩 직후 델리게이트 실행)
                box->OnWeaponHPChanged.Execute(box->Get_WeaponHP_Value());
                break;
            }
        }
    }

    void WeaponScript::changeValue(float newValue)
    {
        if (TargetGauge)
        {
            const float max = std::max(1e-6f, Get_maxValue());
            TargetGauge->value = std::clamp(newValue / max, 0.0f, 1.0f);
            nowValue = TargetGauge->value;
        }
    }

    void WeaponScript::Update(float deltaTime)
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

    float WeaponScript::SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float dt)
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

    void WeaponScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}



