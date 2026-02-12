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
#include "../Camera/CombatDeathFpsProduction.h"
#include "../Camera/BossWinCinematicController.h"
#include "../Combat/C_CombatSessionComponent.h"
#include <algorithm>

namespace Alice
{
    REGISTER_SCRIPT(PrintDarkQuardScript);

    namespace
    {
        constexpr float kCinematicGateFallbackSec = 0.35f;

        template <typename TScript>
        TScript* FindScriptOnEntity(World& world, const std::string& entityName, const std::string& scriptName)
        {
            if (scriptName.empty())
                return nullptr;

            if (!entityName.empty())
            {
                const GameObject go = world.FindGameObject(entityName);
                if (go.IsValid())
                {
                    auto* scripts = world.GetScripts(go.id());
                    if (scripts)
                    {
                        for (auto& sc : *scripts)
                        {
                            if (sc.scriptName != scriptName || !sc.instance)
                                continue;
                            if (auto* typed = dynamic_cast<TScript*>(sc.instance.get()))
                                return typed;
                        }
                    }
                }
            }

            for (auto& [entityId, scripts] : world.GetAllScriptsInWorld())
            {
                (void)entityId;
                for (auto& sc : scripts)
                {
                    if (sc.scriptName != scriptName || !sc.instance)
                        continue;
                    if (auto* typed = dynamic_cast<TScript*>(sc.instance.get()))
                        return typed;
                }
            }

            return nullptr;
        }

        C_CombatSessionComponent* FindCombatSession(World& world, const std::string& entityName)
        {
            if (entityName.empty())
                return nullptr;

            const GameObject go = world.FindGameObject(entityName);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName != "C_CombatSessionComponent" || !sc.instance)
                    continue;
                if (auto* session = dynamic_cast<C_CombatSessionComponent*>(sc.instance.get()))
                    return session;
            }

            return nullptr;
        }
    }

    void PrintDarkQuardScript::Start()
    {
        m_elapsed = 0.0f;
        m_scriptElapsed = 0.0f;
        m_isShowing = false;
        m_playerDeathTriggered = false;
        m_bossDeathTriggered = false;
        m_waitPlayerDeathCinematicRelease = false;
        m_waitBossDeathCinematicRelease = false;
        m_playerDeathCinematicSawBlock = false;
        m_bossDeathCinematicSawBlock = false;
        m_playerDeathCinematicWaitSec = 0.0f;
        m_bossDeathCinematicWaitSec = 0.0f;
        m_playerDeathLatched = false;
        m_bossDeathLatched = false;
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
                bool playerDeadThisFrame = false;
                bool playerDeadBySession = false;
                if (auto* session = FindCombatSession(*w, "SceneManager"))
                    playerDeadBySession = (session->GetPlayerState() == Combat::ActionState::Dead);

                const std::string& healthName = Get_healthEntityName();
                if (!healthName.empty())
                {
                    GameObject healthGo = w->FindGameObject(healthName);
                    if (healthGo.IsValid())
                    {
                        if (auto* health = w->GetComponent<HealthComponent>(healthGo.id()))
                        {
                            playerDeadThisFrame = (!health->alive || health->currentHealth <= 0.0f);
                            if (playerDeadThisFrame)
                                m_playerDeathLatched = true;
                            const bool playerDeadNow = m_playerDeathLatched;
                            if (playerDeadNow)
                            {
                                bool hasDeathCinematicGate = false;
                                bool blockByCinematic = false;
                                if (auto* deathCine = FindScriptOnEntity<CombatDeathFpsProduction>(
                                        *w,
                                        Get_deathCinematicEntityName(),
                                        Get_deathCinematicScriptName()))
                                {
                                    hasDeathCinematicGate = deathCine->Get_m_autoStartOnPlayerDeath();
                                    blockByCinematic = deathCine->IsUiBlockingActive();
                                }

                                if (hasDeathCinematicGate)
                                {
                                    if (!m_waitPlayerDeathCinematicRelease)
                                    {
                                        m_waitPlayerDeathCinematicRelease = true;
                                        m_playerDeathCinematicSawBlock = false;
                                        m_playerDeathCinematicWaitSec = 0.0f;
                                    }
                                    else
                                    {
                                        m_playerDeathCinematicWaitSec += (deltaTime > 0.0f ? deltaTime : 0.0f);
                                    }

                                    if (blockByCinematic)
                                        m_playerDeathCinematicSawBlock = true;

                                    const bool readyAfterCinematic = (m_playerDeathCinematicSawBlock && !blockByCinematic);
                                    const bool fallbackRelease =
                                        (!m_playerDeathCinematicSawBlock && m_playerDeathCinematicWaitSec >= kCinematicGateFallbackSec);
                                    const bool canReleaseUi = (readyAfterCinematic || fallbackRelease);
                                    if (!canReleaseUi)
                                        blockByCinematic = true;
                                }

                                if (!blockByCinematic)
                                {
                                    shouldTrigger = true;
                                    triggerType = TriggerType::PlayerDeath;
                                }
                            }
                        }
                    }
                }

                if (playerDeadBySession)
                    m_playerDeathLatched = true;

                if (!m_playerDeathLatched)
                {
                    m_waitPlayerDeathCinematicRelease = false;
                    m_playerDeathCinematicSawBlock = false;
                    m_playerDeathCinematicWaitSec = 0.0f;
                }
            }

            if (!shouldTrigger && Get_triggerOnBossDeath() && !m_bossDeathTriggered && w)
            {
                bool bossDeadThisFrame = false;
                bool bossDeadBySession = false;
                if (auto* session = FindCombatSession(*w, "SceneManager"))
                    bossDeadBySession = (session->GetBossState() == Combat::ActionState::Dead);

                const std::string& bossHealthName = Get_bossHealthEntityName();
                if (!bossHealthName.empty())
                {
                    GameObject bossHealthGo = w->FindGameObject(bossHealthName);
                    if (bossHealthGo.IsValid())
                    {
                        if (auto* health = w->GetComponent<HealthComponent>(bossHealthGo.id()))
                        {
                            bossDeadThisFrame = (!health->alive || health->currentHealth <= 0.0f);
                            if (bossDeadThisFrame)
                                m_bossDeathLatched = true;
                            const bool bossDeadNow = m_bossDeathLatched;
                            if (bossDeadNow)
                            {
                                BossWinCinematicController* winCine = nullptr;
                                bool hasWinCinematicGate = false;
                                bool blockByCinematic = false;
                                if (auto* found = FindScriptOnEntity<BossWinCinematicController>(
                                        *w,
                                        Get_winCinematicEntityName(),
                                        Get_winCinematicScriptName()))
                                {
                                    winCine = found;
                                    hasWinCinematicGate = winCine->Get_m_autoStartOnBossDeath();
                                    blockByCinematic = winCine->IsUiBlockingActive();
                                }

                                if (hasWinCinematicGate)
                                {
                                    if (!m_waitBossDeathCinematicRelease)
                                    {
                                        m_waitBossDeathCinematicRelease = true;
                                        m_bossDeathCinematicSawBlock = false;
                                        m_bossDeathCinematicWaitSec = 0.0f;
                                    }
                                    else
                                    {
                                        m_bossDeathCinematicWaitSec += (deltaTime > 0.0f ? deltaTime : 0.0f);
                                    }

                                    if (blockByCinematic)
                                        m_bossDeathCinematicSawBlock = true;

                                    float fallbackAfterBlendSec = 0.0f;
                                    if (winCine)
                                    {
                                        fallbackAfterBlendSec =
                                            std::max(0.0f, winCine->Get_m_focusDurationSec()) +
                                            std::max(0.0f, winCine->Get_m_focusHoldSec()) +
                                            std::max(0.0f, winCine->Get_m_returnDurationSec()) +
                                            std::max(0.0f, winCine->Get_m_waitBeforeShakeSec()) +
                                            std::max(0.0f, winCine->Get_m_shakeBlurDurationSec()) +
                                            0.05f;
                                    }

                                    // Prefer strict "after real cinematic end"; fallback to expected blend length when detection misses.
                                    bool canReleaseUi = false;
                                    if (m_bossDeathCinematicSawBlock)
                                    {
                                        canReleaseUi = !blockByCinematic;
                                    }
                                    else if (fallbackAfterBlendSec > 0.0f
                                             && m_bossDeathCinematicWaitSec >= fallbackAfterBlendSec)
                                    {
                                        canReleaseUi = true;
                                    }
                                    if (!canReleaseUi)
                                        blockByCinematic = true;
                                }

                                if (!blockByCinematic)
                                {
                                    shouldTrigger = true;
                                    triggerType = TriggerType::BossDeath;
                                }
                            }
                        }
                    }
                }

                if (bossDeadBySession)
                    m_bossDeathLatched = true;

                if (!m_bossDeathLatched)
                {
                    m_waitBossDeathCinematicRelease = false;
                    m_bossDeathCinematicSawBlock = false;
                    m_bossDeathCinematicWaitSec = 0.0f;
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
                {
                    m_playerDeathTriggered = true;
                    m_waitPlayerDeathCinematicRelease = false;
                    m_playerDeathCinematicSawBlock = false;
                    m_playerDeathCinematicWaitSec = 0.0f;
                    m_playerDeathLatched = false;
                }
                else if (triggerType == TriggerType::BossDeath)
                {
                    m_bossDeathTriggered = true;
                    m_waitBossDeathCinematicRelease = false;
                    m_bossDeathCinematicSawBlock = false;
                    m_bossDeathCinematicWaitSec = 0.0f;
                    m_bossDeathLatched = false;
                }
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
