#include "WeaponScript.h"
#include <algorithm>
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UICommon.h"
#include "BoxDeligateScript.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"

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
        else
        {
            // 기본 텍스처 설정
            TargetGauge->fillTexture = "Resource/Test/4_Resources/UI/StateBar/Egoweapon_Gauge_IN.png";
        }
        TargetGauge->fillLateTexture = "Resource/Test/4_Resources/UI/StateBar/Egoweapon_Gauge_IN.png";
        TargetGauge->backgroundTexture = "";
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

        // UI_Hit_VignetEffect 위젯 찾기
        if (!Get_vignetEffectWidgetName().empty())
        {
            EntityId vignetEntity = InvalidEntityId;
            for (auto [id, widget] : w->GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? w->GetEntityName(id) : widget.widgetName;
                if (widgetName == Get_vignetEffectWidgetName())
                {
                    vignetEntity = id;
                    break;
                }
            }

            if (vignetEntity != InvalidEntityId)
            {
                VignetEffectWidget = w->GetComponent<UIWidgetComponent>(vignetEntity);
                if (VignetEffectWidget)
                {
                    // 초기 상태: Collapsed로 설정
                    VignetEffectWidget->visibility = AliceUI::UIVisibility::Collapsed;
                    ALICE_LOG_INFO("[WeaponScript] VignetEffect widget found: %s", Get_vignetEffectWidgetName().c_str());
                }
                else
                {
                    ALICE_LOG_WARN("[WeaponScript] VignetEffect widget found but UIWidgetComponent not found: %s", Get_vignetEffectWidgetName().c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[WeaponScript] VignetEffect widget not found: %s", Get_vignetEffectWidgetName().c_str());
            }
        }

        wasLowValue = false;
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
        World* w = GetWorld();
        if (!w || !TargetGauge)
            return;

        // HealthComponent를 직접 읽기 (healthEntityName이 설정된 경우)
        if (!Get_healthEntityName().empty())
        {
            GameObject healthGo = w->FindGameObject(Get_healthEntityName());
            if (healthGo.IsValid())
            {
                if (auto* health = w->GetComponent<HealthComponent>(healthGo.id()))
                {
                    const float max = std::max(1e-6f, health->weaponDurabilityMax);
                    TargetGauge->value = std::clamp(health->weaponDurability / max, 0.0f, 1.0f);
                    nowValue = TargetGauge->value;
                }
            }
        }

        // UI_Hit_VignetEffect visibility 제어
        if (TargetGauge && VignetEffectWidget)
        {
            // 정규화된 게이지 값 계산
            float normalizedValue = 1.0f;
            if (TargetGauge->normalized)
            {
                normalizedValue = std::clamp(TargetGauge->value, 0.0f, 1.0f);
            }
            else
            {
                const float range = TargetGauge->maxValue - TargetGauge->minValue;
                if (range > 0.0001f)
                {
                    normalizedValue = std::clamp((TargetGauge->value - TargetGauge->minValue) / range, 0.0f, 1.0f);
                }
            }

            const float threshold = Get_visibilityThreshold();
            const bool isLowValue = normalizedValue <= threshold;

            // 상태가 변경되었을 때만 visibility 업데이트
            if (isLowValue != wasLowValue)
            {
                if (isLowValue)
                {
                    // 0.3 이하일 때 Visible
                    VignetEffectWidget->visibility = AliceUI::UIVisibility::Visible;
                    ALICE_LOG_INFO("[WeaponScript] Gauge value (%.2f) <= threshold (%.2f), setting VignetEffect to Visible",
                        normalizedValue, threshold);
                }
                else
                {
                    // 0.3 초과일 때 Collapsed
                    VignetEffectWidget->visibility = AliceUI::UIVisibility::Collapsed;
                    ALICE_LOG_INFO("[WeaponScript] Gauge value (%.2f) > threshold (%.2f), setting VignetEffect to Collapsed",
                        normalizedValue, threshold);
                }
                wasLowValue = isLowValue;
            }
        }

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



