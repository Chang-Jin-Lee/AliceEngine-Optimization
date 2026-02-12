#include "PrintDarkQuardScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/UI/UIEmptyGaugeEffectComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UIImageComponent.h"
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
        m_playerImageEntityId = InvalidEntityId;
        m_bossImageEntityId = InvalidEntityId;
        m_activeImageEntityId = InvalidEntityId;

        World* w = GetWorld();
        if (w)
        {
            const std::string& dieTextName = Get_dieTextWidgetName();
            if (!dieTextName.empty())
                m_dieTextEntityId = AliceUI::FindWidgetByName(*w, gameObject().id(), dieTextName);

            const std::string& playerImageName = Get_playerDeathImageWidgetName();
            if (!playerImageName.empty())
                m_playerImageEntityId = AliceUI::FindWidgetByName(*w, gameObject().id(), playerImageName);

            const std::string& bossImageName = Get_bossDeathImageWidgetName();
            if (!bossImageName.empty())
                m_bossImageEntityId = AliceUI::FindWidgetByName(*w, gameObject().id(), bossImageName);
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

        if (w)
        {
            auto ResetImage = [&](EntityId id) {
                if (id == InvalidEntityId)
                    return;
                if (auto* imgWidget = w->GetComponent<UIWidgetComponent>(id))
                    imgWidget->visibility = AliceUI::UIVisibility::Collapsed;
                if (auto* img = w->GetComponent<UIImageComponent>(id))
                {
                    auto c = img->color;
                    c.w = 0.0f;
                    img->color = c;
                }
            };
            ResetImage(m_playerImageEntityId);
            ResetImage(m_bossImageEntityId);
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

                m_activeImageEntityId = InvalidEntityId;
                if (triggerType == TriggerType::BossDeath)
                    m_activeImageEntityId = m_bossImageEntityId;
                else if (triggerType == TriggerType::PlayerDeath)
                    m_activeImageEntityId = m_playerImageEntityId;
                else if (triggerType == TriggerType::Manual)
                {
                    if (m_playerImageEntityId != InvalidEntityId)
                        m_activeImageEntityId = m_playerImageEntityId;
                    else if (m_bossImageEntityId != InvalidEntityId)
                        m_activeImageEntityId = m_bossImageEntityId;
                }

                if (w)
                {
                    auto ResetImage = [&](EntityId id) {
                        if (id == InvalidEntityId)
                            return;
                        if (auto* imgWidget = w->GetComponent<UIWidgetComponent>(id))
                            imgWidget->visibility = AliceUI::UIVisibility::Collapsed;
                        if (auto* img = w->GetComponent<UIImageComponent>(id))
                        {
                            auto c = img->color;
                            c.w = 0.0f;
                            img->color = c;
                        }
                    };
                    ResetImage(m_playerImageEntityId);
                    ResetImage(m_bossImageEntityId);

                    if (m_activeImageEntityId != InvalidEntityId)
                    {
                        if (auto* imgWidget = w->GetComponent<UIWidgetComponent>(m_activeImageEntityId))
                            imgWidget->visibility = AliceUI::UIVisibility::Visible;
                        if (auto* img = w->GetComponent<UIImageComponent>(m_activeImageEntityId))
                        {
                            auto c = img->color;
                            c.w = 0.0f;
                            img->color = c;
                        }
                    }
                }

                if (m_dieTextEntityId != InvalidEntityId && m_activeImageEntityId == InvalidEntityId)
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
                else if (m_dieTextEntityId != InvalidEntityId && w)
                {
                    auto* dieTextWidget = w->GetComponent<UIWidgetComponent>(m_dieTextEntityId);
                    if (dieTextWidget)
                        dieTextWidget->visibility = AliceUI::UIVisibility::Collapsed;
                    auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
                    if (dieText)
                        dieText->color.w = 0.0f;
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
        if (m_activeImageEntityId != InvalidEntityId || m_dieTextEntityId != InvalidEntityId)
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

                if (m_activeImageEntityId != InvalidEntityId)
                {
                    auto* img = w->GetComponent<UIImageComponent>(m_activeImageEntityId);
                    if (img)
                    {
                        auto c = img->color;
                        c.w = alpha;
                        img->color = c;
                    }
                }
                else if (m_dieTextEntityId != InvalidEntityId)
                {
                    auto* dieText = w->GetComponent<UITextComponent>(m_dieTextEntityId);
                    if (dieText)
                        dieText->color.w = alpha;
                }
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
            if (World* w = GetWorld())
            {
                auto ResetImage = [&](EntityId id) {
                    if (id == InvalidEntityId)
                        return;
                    if (auto* imgWidget = w->GetComponent<UIWidgetComponent>(id))
                        imgWidget->visibility = AliceUI::UIVisibility::Collapsed;
                    if (auto* img = w->GetComponent<UIImageComponent>(id))
                    {
                        auto c = img->color;
                        c.w = 0.0f;
                        img->color = c;
                    }
                };
                ResetImage(m_playerImageEntityId);
                ResetImage(m_bossImageEntityId);
                ResetImage(m_activeImageEntityId);
            }
            m_activeImageEntityId = InvalidEntityId;
            m_isShowing = false;
        }
    }
}
