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
        NormalBaseAlpha = 1.0f;
        CooldownBaseAlpha = 1.0f;
        wasLowValue = false;
        ImageBlendInitialized = false;

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
                NormalImage = w->GetComponent<UIImageComponent>(imageEntity);
                if (NormalImage)
                {
                    if (!Get_normalImagePath().empty())
                        NormalImage->texturePath = Get_normalImagePath();
                    NormalBaseAlpha = std::clamp(NormalImage->color.w, 0.0f, 1.0f);
                    if (NormalBaseAlpha <= 0.0f)
                        NormalBaseAlpha = 1.0f;
                    ALICE_LOG_INFO("[PlayerGauge] Open-eye image resolved: %s", openEyeWidgetName.c_str());
                }
            }
        }

        if (!CooldownImage && !closedEyeWidgetName.empty())
        {
            const EntityId imageEntity = FindWidgetEntityByName(*w, closedEyeWidgetName);
            if (imageEntity != InvalidEntityId)
            {
                CooldownImage = w->GetComponent<UIImageComponent>(imageEntity);
                if (CooldownImage)
                {
                    if (!Get_lowValueImagePath().empty())
                        CooldownImage->texturePath = Get_lowValueImagePath();
                    CooldownBaseAlpha = std::clamp(CooldownImage->color.w, 0.0f, 1.0f);
                    if (CooldownBaseAlpha <= 0.0f)
                        CooldownBaseAlpha = 1.0f;
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

        if (!ImageBlendInitialized && NormalImage && CooldownImage)
        {
            NormalImage->color.w = NormalBaseAlpha;
            CooldownImage->color.w = 0.0f;
            ImageBlendInitialized = true;
        }
    }

    void PlayerGauge::Update(float deltaTime)
    {
        (void)deltaTime;
        TryResolveSession();
        TryResolveImages();

        // 기본값(세션 미탐색): 준비됨 상태
        float openEyeAlpha = 1.0f;
        float closedEyeAlpha = 0.0f;
        bool isLowValue = false;

        if (TargetSession)
        {
            const bool inGuardBreak = (TargetSession->GetPlayerState() == Combat::ActionState::GuardBreakWeak);
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

            // 강제 불가 상태(가드브레이크/weak/무기파괴): 감은눈을 완전히 덮음
            if (inGuardBreak || inWeak || inWeaponBreak)
            {
                openEyeAlpha = 1.0f;
                closedEyeAlpha = 1.0f;
                isLowValue = true;
            }
            else
            {
                const bool isRageActive = TargetSession->IsPlayerRageActive();
                const float cooldownNormRemain =
                    std::clamp(TargetSession->GetPlayerRageCooldownNormalized(), 0.0f, 1.0f);

                // 준비됨 또는 광폭화 활성화: 항상 뜬눈
                if (isRageActive || cooldownNormRemain <= 0.0f)
                {
                    openEyeAlpha = 1.0f;
                    closedEyeAlpha = 0.0f;
                    isLowValue = false;
                }
                else
                {
                    // 쿨다운 중: 뜬눈은 항상 1.0, 감은눈만 1.0 -> 0.2로 감소
                    openEyeAlpha = 1.0f;
                    closedEyeAlpha = 0.2f + (0.8f * cooldownNormRemain);
                    isLowValue = true;
                }
            }
        }

        // 2개 위젯이 모두 있으면 알파 보간으로 처리
        if (NormalImage && CooldownImage)
        {
            NormalImage->color.w = std::clamp(NormalBaseAlpha * openEyeAlpha, 0.0f, 1.0f);
            CooldownImage->color.w = std::clamp(CooldownBaseAlpha * closedEyeAlpha, 0.0f, 1.0f);
            return;
        }

        if (!TargetImage)
            return;

        // 단일 위젯 fallback: 텍스처 스왑(체력 조건 미사용)
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
