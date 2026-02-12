#include "changeBackGroud.h"
#include <algorithm>
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

        EntityId FindWidgetEntityByName(World& world, const std::string& widgetName)
        {
            if (widgetName.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string name = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (name == widgetName)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(PlayerGauge);

    void PlayerGauge::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        TargetGauge = nullptr;
        TargetImage = nullptr;
        NormalImage = nullptr;
        CooldownImage = nullptr;
        NormalImageEntity = InvalidEntityId;
        CooldownImageEntity = InvalidEntityId;
        wasLowValue = false;
        VisibilityInitialized = false;

        TryResolveSession();

        // 게이지바 위젯 찾기
        if (!Get_gaugeWidgetName().empty())
        {
            const EntityId gaugeEntity = FindWidgetEntityByName(*w, Get_gaugeWidgetName());

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

        TryResolveImages();
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

    void PlayerGauge::TryResolveImages()
    {
        World* w = GetWorld();
        if (!w)
            return;

        std::string openEyeWidgetName = Get_normalImageWidgetName();
        if (openEyeWidgetName.empty())
            openEyeWidgetName = Get_imageWidgetName();

        std::string closedEyeWidgetName = Get_cooldownImageWidgetName();
        if (closedEyeWidgetName.empty() && !openEyeWidgetName.empty())
            closedEyeWidgetName = openEyeWidgetName + "_Cooldown";
        if (closedEyeWidgetName.empty() && !Get_imageWidgetName().empty())
            closedEyeWidgetName = Get_imageWidgetName() + "_Cooldown";

        if (!NormalImage && !openEyeWidgetName.empty())
        {
            const EntityId imageEntity = FindWidgetEntityByName(*w, openEyeWidgetName);
            if (imageEntity != InvalidEntityId)
            {
                NormalImageEntity = imageEntity;
                NormalImage = w->GetComponent<UIImageComponent>(imageEntity);
                if (NormalImage)
                {
                    if (!Get_normalImagePath().empty())
                        NormalImage->texturePath = Get_normalImagePath();
                    // Visibility 스왑 방식에서는 알파가 0이면 보이지 않으므로 고정.
                    NormalImage->color.w = 1.0f;
                    ALICE_LOG_INFO("[PlayerGauge] Open-eye image resolved: %s", openEyeWidgetName.c_str());
                }
            }
        }

        if (!CooldownImage && !closedEyeWidgetName.empty())
        {
            const EntityId imageEntity = FindWidgetEntityByName(*w, closedEyeWidgetName);
            if (imageEntity != InvalidEntityId)
            {
                CooldownImageEntity = imageEntity;
                CooldownImage = w->GetComponent<UIImageComponent>(imageEntity);
                if (CooldownImage)
                {
                    if (!Get_lowValueImagePath().empty())
                        CooldownImage->texturePath = Get_lowValueImagePath();
                    // Visibility 스왑 방식에서는 알파가 0이면 보이지 않으므로 고정.
                    CooldownImage->color.w = 1.0f;
                    ALICE_LOG_INFO("[PlayerGauge] Closed-eye image resolved: %s", closedEyeWidgetName.c_str());
                }
            }
        }

        if (!TargetImage)
        {
            TargetImage = NormalImage;
            if (!TargetImage && !Get_imageWidgetName().empty())
            {
                const EntityId imageEntity = FindWidgetEntityByName(*w, Get_imageWidgetName());
                if (imageEntity != InvalidEntityId)
                    TargetImage = w->GetComponent<UIImageComponent>(imageEntity);
            }
        }

    }

    void PlayerGauge::Update(float deltaTime)
    {
        (void)deltaTime;
        TryResolveSession();
        TryResolveImages();

        // 기본값(세션 미탐색): 정상 상태
        bool isLowValue = false;

        if (TargetSession)
        {
            const bool inWeak = TargetSession->IsPlayerWeakActive();
            bool inWeaponBreak = false;

            if (TargetGauge)
            {
                float gauge01 = std::clamp(TargetGauge->value, 0.0f, 1.0f);
                if (!TargetGauge->normalized)
                {
                    const float range = TargetGauge->maxValue - TargetGauge->minValue;
                    if (range > 1e-6f)
                        gauge01 = std::clamp((TargetGauge->value - TargetGauge->minValue) / range, 0.0f, 1.0f);
                }
                const float breakThreshold = std::clamp(Get_weaponBreakThreshold(), 0.0f, 1.0f);
                inWeaponBreak = (gauge01 <= breakThreshold);
            }

            // 단순 상태 구분:
            // 1) weak 또는 weaponBreak -> low(감은눈)
            // 2) 그 외 -> normal(뜬눈)
            isLowValue = (inWeak || inWeaponBreak);
        }

        // 2개 위젯이 모두 있으면 visibility 스왑으로 처리
        if (NormalImage && CooldownImage)
        {
            World* w = GetWorld();
            if (w && (!VisibilityInitialized || (isLowValue != wasLowValue)))
            {
                auto setVisible = [&](EntityId id, bool visible)
                {
                    if (id == InvalidEntityId)
                        return;
                    if (auto* widget = w->GetComponent<UIWidgetComponent>(id))
                        widget->visibility = visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;
                };

                // 다른 로직/기존 데이터로 알파가 0일 수 있어 매번 보정.
                if (NormalImage)
                    NormalImage->color.w = 1.0f;
                if (CooldownImage)
                    CooldownImage->color.w = 1.0f;

                setVisible(NormalImageEntity, !isLowValue);
                setVisible(CooldownImageEntity, isLowValue);
                VisibilityInitialized = true;
                wasLowValue = isLowValue;
            }
            return;
        }

        if (!TargetImage)
            return;

        // 단일 위젯 fallback: 텍스처 스왑
        if (isLowValue != wasLowValue)
        {
            if (isLowValue)
            {
                if (!Get_lowValueImagePath().empty())
                {
                    TargetImage->texturePath = Get_lowValueImagePath();
                    TargetImage->color.w = 1.0f;
                    ALICE_LOG_INFO("[PlayerGauge] Weak/weapon-break active. Switching to low image: %s",
                        Get_lowValueImagePath().c_str());
                }
            }
            else
            {
                if (!Get_normalImagePath().empty())
                {
                    TargetImage->texturePath = Get_normalImagePath();
                    TargetImage->color.w = 1.0f;
                    ALICE_LOG_INFO("[PlayerGauge] Normal state. Switching to normal image: %s",
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
