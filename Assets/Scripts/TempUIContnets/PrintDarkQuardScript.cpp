#include "PrintDarkQuardScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/UI/UIEmptyGaugeEffectComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/BindWidget.h"
#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(PrintDarkQuardScript);

    void PrintDarkQuardScript::Start()
    {
        m_elapsed = 0.0f;
        m_scriptElapsed = 0.0f;
        m_isShowing = false;
        m_deathTriggered = false;
        m_dieTextEntityId = InvalidEntityId;

        World* w = GetWorld();
        if (w)
            m_dieTextEntityId = AliceUI::FindWidgetByName(*w, gameObject().id(), "UI_DieText");

        auto* widget = gameObject().GetComponent<UIWidgetComponent>();
        if (widget)
            widget->visibility = AliceUI::UIVisibility::Collapsed;

        if (m_dieTextEntityId != InvalidEntityId)
        {
            auto* dieTextWidget = w->GetComponent<UIWidgetComponent>(m_dieTextEntityId);
            if (dieTextWidget)
                dieTextWidget->visibility = AliceUI::UIVisibility::Collapsed;
            auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
            if (dieText)
                dieText->color.w = 0.0f;
        }
    }

    void PrintDarkQuardScript::Update(float deltaTime)
    {
        m_scriptElapsed += (deltaTime > 0.0f ? deltaTime : 0.0f);

        auto* widget = gameObject().GetComponent<UIWidgetComponent>();
        if (!widget)
            return;

        if (!m_isShowing)
        {
            bool shouldTrigger = false;
            World* w = GetWorld();
            if (Get_triggerOnDeath() && !m_deathTriggered && w)
            {
                const std::string& healthName = Get_healthEntityName();
                if (!healthName.empty())
                {
                    GameObject healthGo = w->FindGameObject(healthName);
                    if (healthGo.IsValid())
                    {
                        if (auto* health = w->GetComponent<HealthComponent>(healthGo.id()))
                        {
                            if (health->currentHealth <= 0.0f)
                                shouldTrigger = true;
                        }
                    }
                }
            }

            if (!shouldTrigger)
            {
                auto* input = Input();
                if (input && input->GetKeyDown(static_cast<KeyCode>(m_triggerKey)))
                    shouldTrigger = true;
            }

            if (shouldTrigger)
            {
                auto* dieParams = gameObject().GetComponent<UIDieLineParamsComponent>();
                if (!dieParams)
                    dieParams = &gameObject().AddComponent<UIDieLineParamsComponent>();
                if (dieParams)
                    dieParams->startTime = m_scriptElapsed - deltaTime;

                widget->visibility = AliceUI::UIVisibility::Visible;
                if (m_dieTextEntityId != InvalidEntityId)
                {
                    World* w = GetWorld();
                    if (w)
                    {
                        auto* dieTextWidget = w->GetComponent<UIWidgetComponent>(m_dieTextEntityId);
                        if (dieTextWidget)
                            dieTextWidget->visibility = AliceUI::UIVisibility::Visible;
                        auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
                        if (dieText)
                            dieText->color.w = 0.0f;  // 보간 시작
                    }
                }
                m_isShowing = true;
                m_elapsed = 0.0f;
                if (Get_triggerOnDeath())
                    m_deathTriggered = true;
            }
            return;
        }

        m_elapsed += deltaTime;

        // DieText 알파 보간 (DieLine과 동일 타이밍, 최대 0.8)
        if (m_dieTextEntityId != InvalidEntityId)
        {
            World* w = GetWorld();
            if (w)
            {
                auto* dieParams = gameObject().GetComponent<UIDieLineParamsComponent>();
                const float phase1Dur = dieParams && dieParams->phase1Duration > 0.0f ? dieParams->phase1Duration : 1.2f;
                const float phase2End = dieParams && dieParams->phase2End > 0.0f ? dieParams->phase2End : 2.0f;
                const float phase3Dur = dieParams && dieParams->phase3Duration > 0.0f ? dieParams->phase3Duration : 1.2f;
                const float t = m_elapsed;
                float alpha = 0.0f;
                if (t < phase1Dur)
                    alpha = 0.8f * (t / phase1Dur);
                else if (t < phase2End)
                    alpha = 0.8f;
                else
                    alpha = 0.8f * (1.0f - (t - phase2End) / phase3Dur);
                alpha = std::clamp(alpha, 0.0f, 0.8f);

                auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
                if (dieText)
                    dieText->color.w = alpha;
            }
        }

        if (m_elapsed >= m_totalCycle)
        {
            widget->visibility = AliceUI::UIVisibility::Collapsed;
            if (m_dieTextEntityId != InvalidEntityId)
            {
                World* w = GetWorld();
                if (w)
                {
                    auto* dieTextWidget = w->GetComponent<UIWidgetComponent>(m_dieTextEntityId);
                    if (dieTextWidget)
                        dieTextWidget->visibility = AliceUI::UIVisibility::Collapsed;
                    auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
                    if (dieText)
                        dieText->color.w = 0.0f;
                }
            }
            m_isShowing = false;
        }
    }
}
