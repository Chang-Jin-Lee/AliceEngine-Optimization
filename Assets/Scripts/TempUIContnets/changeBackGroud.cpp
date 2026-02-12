#include "changeBackGroud.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    namespace
    {
        C_CombatSessionComponent* FindSession(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;

            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "C_CombatSessionComponent" && sc.instance)
                    return static_cast<C_CombatSessionComponent*>(sc.instance.get());
            }
            return nullptr;
        }
    }

    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(PlayerGauge);

    void PlayerGauge::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        TryResolveSession();

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

    void PlayerGauge::TryResolveSession()
    {
        if (TargetSession)
            return;

        World* w = GetWorld();
        if (!w)
            return;

        TargetSession = FindSession(*w, Get_sessionEntityName());
    }

    void PlayerGauge::Update(float deltaTime)
    {
        if (!TargetImage)
            return;

        (void)deltaTime;
        TryResolveSession();

        // 체력 퍼센트와 무관하게 광폭화 쿨다운 상태만으로 이미지 결정:
        // - 쿨다운 중(cooldown > 0): 저체력 이미지
        // - 준비됨/활성화/세션 미탐색: 일반 이미지
        bool isLowValue = false;
        if (TargetSession)
        {
            const float rageCooldownRemaining = TargetSession->GetPlayerRageCooldownRemainingSec();
            const bool inGuardBreak = (TargetSession->GetPlayerState() == Combat::ActionState::GuardBreakWeak);
            const bool inWeak = TargetSession->IsPlayerWeakActive();
            isLowValue = (rageCooldownRemaining > 0.0f) || inGuardBreak || inWeak;
        }

        // 상태가 변경되었을 때만 이미지 업데이트
        if (isLowValue != wasLowValue)
        {
            if (isLowValue)
            {
                // 낮은 값일 때 이미지 변경
                if (!Get_lowValueImagePath().empty())
                {
                    TargetImage->texturePath = Get_lowValueImagePath();
                    ALICE_LOG_INFO("[PlayerGauge] Rage cooldown active. Switching to cooldown image: %s",
                        Get_lowValueImagePath().c_str());
                }
            }
            else
            {
                // 정상 값일 때 이미지 변경
                if (!Get_normalImagePath().empty())
                {
                    TargetImage->texturePath = Get_normalImagePath();
                    ALICE_LOG_INFO("[PlayerGauge] Rage cooldown ready/active(or no session). Switching to normal image: %s",
                        Get_normalImagePath().c_str());
                }
            }
            wasLowValue = isLowValue;
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
