#pragma once

#include <string>
#include <vector>
#include <random>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UIAnimationComponent.h"
#include "Runtime/UI/UIEffectComponent.h"
#include "Runtime/UI/UIShakeComponent.h"
#include "Runtime/UI/UIHover3DComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/Components/TransformComponent.h"

namespace Alice
{
    class AdvancedUIController : public IScript
    {
    public:
        ALICE_BODY(AdvancedUIController);
        void Start() override;
        void Update(float deltaTime) override;

        std::string rootWidgetName = "UIRoot";

        ALICE_BIND_WIDGET_NAMED(UIAnimationComponent*, DropAnim, "UI_DropImage");
        ALICE_BIND_WIDGET_NAMED(UIAnimationComponent*, CooldownAnim, "UI_CooldownIcon");
        ALICE_BIND_WIDGET_NAMED(UIEffectComponent*, CooldownEffect, "UI_CooldownIcon");
        ALICE_BIND_WIDGET_NAMED(UIEffectComponent*, OutlineEffect, "UI_OutlineButton");
        ALICE_BIND_WIDGET_NAMED(UIButtonComponent*, OutlineButton, "UI_OutlineButton");
        ALICE_BIND_WIDGET_NAMED(UIShakeComponent*, ShakeComp, "UI_ShakeImage");
        ALICE_BIND_WIDGET_NAMED(UIEffectComponent*, GlowEffect, "UI_GlowGauge");
        ALICE_BIND_WIDGET_NAMED(UIGaugeComponent*, GlowGauge, "UI_GlowGauge");
        ALICE_BIND_WIDGET_NAMED(UIAnimationComponent*, FadeAnim, "UI_FadeText");
        ALICE_BIND_WIDGET_NAMED(UITextComponent*, FadeText, "UI_FadeText");

    private:
        struct DamageText
        {
            EntityId id{ InvalidEntityId };
            float age{ 0.0f };
            float lifetime{ 1.0f };
        };

        std::vector<DamageText> m_damageTexts;
        float m_gaugeTime{ 0.0f };
        bool m_outlineLocked{ false };
        bool m_glowEnabled{ true };
        std::mt19937 m_rng;

        void SpawnDamageText();
    };
}
