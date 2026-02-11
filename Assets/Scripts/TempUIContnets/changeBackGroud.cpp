#include "changeBackGroud.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(PlayerGauge);

    void PlayerGauge::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        // 게이지바 위젯 찾기
        if (!Get_gaugeWidgetName().empty())
        {
            // UI 루트 찾기 (전체 검색)
            EntityId gaugeEntity = InvalidEntityId;
            for (auto [id, widget] : w->GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? w->GetEntityName(id) : widget.widgetName;
                if (widgetName == Get_gaugeWidgetName())
                {
                    gaugeEntity = id;
                    break;
                }
            }

            if (gaugeEntity != InvalidEntityId)
            {
                TargetGauge = w->GetComponent<UIGaugeComponent>(gaugeEntity);
                if (TargetGauge)
                {
                    ALICE_LOG_INFO("[PlayerGauge] Gauge widget found: %s", Get_gaugeWidgetName().c_str());
                }
                else
                {
                    ALICE_LOG_WARN("[PlayerGauge] Gauge widget found but UIGaugeComponent not found: %s", Get_gaugeWidgetName().c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[PlayerGauge] Gauge widget not found: %s", Get_gaugeWidgetName().c_str());
            }
        }

        // 이미지 컴포넌트 위젯 찾기
        if (!Get_imageWidgetName().empty())
        {
            EntityId imageEntity = InvalidEntityId;
            for (auto [id, widget] : w->GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? w->GetEntityName(id) : widget.widgetName;
                if (widgetName == Get_imageWidgetName())
                {
                    imageEntity = id;
                    break;
                }
            }

            if (imageEntity != InvalidEntityId)
            {
                TargetImage = w->GetComponent<UIImageComponent>(imageEntity);
                if (TargetImage)
                {
                    ALICE_LOG_INFO("[PlayerGauge] Image widget found: %s", Get_imageWidgetName().c_str());
                    // 초기 이미지 설정
                    if (!Get_normalImagePath().empty())
                    {
                        TargetImage->texturePath = Get_normalImagePath();
                    }
                }
                else
                {
                    ALICE_LOG_WARN("[PlayerGauge] Image widget found but UIImageComponent not found: %s", Get_imageWidgetName().c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[PlayerGauge] Image widget not found: %s", Get_imageWidgetName().c_str());
            }
        }

        wasLowValue = false;
    }

    void PlayerGauge::Update(float deltaTime)
    {
        if (!TargetGauge || !TargetImage)
            return;

        // 정규화된 게이지 값 가져오기
        const float normalizedValue = GetNormalizedGaugeValue();
        const float threshold = Get_triggerThreshold();
        const bool isLowValue = normalizedValue <= threshold;

        // 상태가 변경되었을 때만 이미지 업데이트
        if (isLowValue != wasLowValue)
        {
            if (isLowValue)
            {
                // 낮은 값일 때 이미지 변경
                if (!Get_lowValueImagePath().empty())
                {
                    TargetImage->texturePath = Get_lowValueImagePath();
                    ALICE_LOG_INFO("[PlayerGauge] Gauge value (%.2f) <= threshold (%.2f), switching to low value image: %s",
                        normalizedValue, threshold, Get_lowValueImagePath().c_str());
                }
            }
            else
            {
                // 정상 값일 때 이미지 변경
                if (!Get_normalImagePath().empty())
                {
                    TargetImage->texturePath = Get_normalImagePath();
                    ALICE_LOG_INFO("[PlayerGauge] Gauge value (%.2f) > threshold (%.2f), switching to normal image: %s",
                        normalizedValue, threshold, Get_normalImagePath().c_str());
                }
            }
            wasLowValue = isLowValue;
        }
    }

    float PlayerGauge::GetNormalizedGaugeValue() const
    {
        if (!TargetGauge)
            return 1.0f;

        if (TargetGauge->normalized)
        {
            // 이미 정규화된 값 (0~1 범위)
            return std::clamp(TargetGauge->value, 0.0f, 1.0f);
        }
        else
        {
            // 정규화되지 않은 값이면 계산
            const float range = TargetGauge->maxValue - TargetGauge->minValue;
            if (range <= 0.0001f)
                return 1.0f;
            
            const float normalized = (TargetGauge->value - TargetGauge->minValue) / range;
            return std::clamp(normalized, 0.0f, 1.0f);
        }
    }

    void PlayerGauge::ExampleFunction()
    {
        // 리플렉션으로 등록된 함수 예시입니다.
        // 이 함수는 에디터에서 호출할 수 있습니다.
        
        // 예시: Transform 컴포넌트 가져오기
        if (auto* transform = GetComponent<TransformComponent>())
        {
            // 위치를 (0, 0, 0)으로 리셋하는 예시
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
