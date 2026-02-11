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
        m_playerDeathTriggered = false;
        m_bossDeathTriggered = false;
        m_dieTextEntityId = InvalidEntityId;

        World* w = GetWorld();
        if (w)
        {
            const std::string& dieTextName = Get_dieTextWidgetName();
            if (!dieTextName.empty())
                m_dieTextEntityId = AliceUI::FindWidgetByName(*w, gameObject().id(), dieTextName);
        }

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
            enum class TriggerType
            {
                None,
                PlayerDeath,
                BossDeath,
                Manual
            };

            bool shouldTrigger = false;
            TriggerType triggerType = TriggerType::None;
            World* w = GetWorld();
            if (Get_triggerOnDeath() && !m_playerDeathTriggered && w)
            {
                const std::string& healthName = Get_healthEntityName();
                if (!healthName.empty())
                {
                    GameObject healthGo = w->FindGameObject(healthName);
                    if (healthGo.IsValid())
                    {
                        if (auto* health = w->GetComponent<HealthComponent>(healthGo.id()))
                        {
                            if ((!health->alive || health->currentHealth <= 0.0f))
                            {
                                shouldTrigger = true;
                                triggerType = TriggerType::PlayerDeath;
                            }
                        }
                    }
                }
            }

            if (!shouldTrigger && Get_triggerOnBossDeath() && !m_bossDeathTriggered && w)
            {
                const std::string& bossHealthName = Get_bossHealthEntityName();
                if (!bossHealthName.empty())
                {
                    GameObject bossHealthGo = w->FindGameObject(bossHealthName);
                    if (bossHealthGo.IsValid())
                    {
                        if (auto* health = w->GetComponent<HealthComponent>(bossHealthGo.id()))
                        {
                            if ((!health->alive || health->currentHealth <= 0.0f))
                            {
                                shouldTrigger = true;
                                triggerType = TriggerType::BossDeath;
                            }
                        }
                    }
                }
            }

            if (!shouldTrigger)
            {
                auto* input = Input();
                if (input && input->GetKeyDown(static_cast<KeyCode>(m_triggerKey)))
                {
                    shouldTrigger = true;
                    triggerType = TriggerType::Manual;
                }
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
                        {
                            const std::string& playerText = Get_playerDeathText();
                            const std::string& bossText = Get_bossDeathText();
                            if (triggerType == TriggerType::BossDeath)
                            {
                                if (!bossText.empty())
                                    dieText->text = bossText;
                            }
                            else
                            {
                                if (!playerText.empty())
                                    dieText->text = playerText;
                            }
                            dieText->color.w = 0.0f;  // Start with alpha 0
                        }
                    }
                }
                m_isShowing = true;
                m_elapsed = 0.0f;
                if (triggerType == TriggerType::PlayerDeath)
                    m_playerDeathTriggered = true;
                else if (triggerType == TriggerType::BossDeath)
                    m_bossDeathTriggered = true;
            }
            return;
        }

        m_elapsed += deltaTime;

        // DieText ??곕솁 癰귣떯而?(DieLine????덉뵬 ????而? 筌ㅼ뮆? 0.8)
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
