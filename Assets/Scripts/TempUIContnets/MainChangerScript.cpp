#include "MainChangerScript.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UICommon.h"
#include "../Combat/C_CombatSessionComponent.h"
#include "FadeInOutScript.h"

namespace Alice
{
    REGISTER_SCRIPT(MainChangerScript);

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

        FadeInOutScript* FindFadeScript(World& world, const std::string& name)
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
                if (sc.scriptName == "FadeInOutScript" && sc.instance)
                    return static_cast<FadeInOutScript*>(sc.instance.get());
            }
            return nullptr;
        }

        EntityId FindWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }

        bool IsDead(const HealthComponent* health)
        {
            if (!health)
                return false;
            return !health->alive || health->currentHealth <= 0.0f;
        }
    }

    void MainChangerScript::Start()
    {
        m_prevPlayerDead = false;
        m_prevBossDead = false;
        m_pending = false;
        m_pendingTimer = 0.0f;
        m_pendingDelay = 0.0f;
        m_pendingPath.clear();
        m_pendingStage = PendingStage::None;

        World* world = GetWorld();
        if (world)
        {
            GameObject playerGo = world->FindGameObject(Get_playerEntityName());
            GameObject bossGo = world->FindGameObject(Get_bossEntityName());
            m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
            m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;

            m_fade = FindFadeScript(*world, Get_fadeEntityName());

            m_deathWidgetId = FindWidgetByName(*world, Get_deathWidgetName());
            if (m_deathWidgetId != InvalidEntityId)
            {
                if (auto* widget = world->GetComponent<UIWidgetComponent>(m_deathWidgetId))
                    widget->visibility = AliceUI::UIVisibility::Collapsed;
            }
            else if (!Get_deathWidgetName().empty())
            {
                ALICE_LOG_WARN("[MainChangerScript] Death widget not found: %s", Get_deathWidgetName().c_str());
            }

            if (m_playerId == InvalidEntityId)
                ALICE_LOG_WARN("[MainChangerScript] Player entity not found: %s", Get_playerEntityName().c_str());
            if (m_bossId == InvalidEntityId)
                ALICE_LOG_WARN("[MainChangerScript] Boss entity not found: %s", Get_bossEntityName().c_str());
        }
    }

    void MainChangerScript::Update(float deltaTime)
    {
        if (m_pending)
        {
            m_pendingTimer += std::max(0.0f, deltaTime);
            if (m_pendingTimer >= m_pendingDelay)
            {
                if (m_pendingStage == PendingStage::WaitForFade)
                {
                    if (Get_useFadeOnDeath() && m_fade)
                    {
                        m_fade->OnEnable();
                        m_fade->StartFadeIn();
                    }

                    float delay = Get_delaySec();
                    if (delay < 0.0f)
                        delay = ComputeAutoDelaySec();

                    m_pendingTimer = 0.0f;
                    m_pendingDelay = std::max(0.0f, delay);
                    m_pendingStage = PendingStage::WaitForScene;

                    if (m_pendingDelay > 0.0f)
                        return;
                }

                auto* scenes = Scenes();
                if (!scenes)
                {
                    ALICE_LOG_WARN("[MainChangerScript] SceneManager not available");
                    m_pending = false;
                    m_pendingStage = PendingStage::None;
                    return;
                }
                if (m_pendingPath.empty())
                {
                    ALICE_LOG_WARN("[MainChangerScript] Pending scene path is empty");
                    m_pending = false;
                    m_pendingStage = PendingStage::None;
                    return;
                }
                scenes->LoadSceneFileRequest(m_pendingPath.c_str());
                m_pending = false;
                m_pendingStage = PendingStage::None;
            }
            return;
        }

        World* world = GetWorld();
        if (!world)
            return;

        if (!Get_playerEntityName().empty())
        {
            GameObject playerGo = world->FindGameObject(Get_playerEntityName());
            m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        }
        if (!Get_bossEntityName().empty())
        {
            GameObject bossGo = world->FindGameObject(Get_bossEntityName());
            m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
        }

        bool playerDead = false;
        bool bossDead = false;

        C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
        if (session)
        {
            playerDead = (session->GetPlayerState() == Combat::ActionState::Dead);
            bossDead = (session->GetBossState() == Combat::ActionState::Dead);
        }

        // HealthComponent direct check
        if (m_playerId != InvalidEntityId)
        {
            if (auto* health = world->GetComponent<HealthComponent>(m_playerId))
                playerDead = playerDead || IsDead(health);
        }
        if (m_bossId != InvalidEntityId)
        {
            if (auto* health = world->GetComponent<HealthComponent>(m_bossId))
                bossDead = bossDead || IsDead(health);
        }

        std::string path;
        bool trigger = false;

        const bool playerTrigger = Get_triggerOnPlayerDeath() && playerDead && !m_prevPlayerDead;
        const bool bossTrigger = Get_triggerOnBossDeath() && bossDead && !m_prevBossDead;

        if (playerTrigger)
        {
            path = Get_playerScenePath();
            trigger = true;
        }
        else if (bossTrigger)
        {
            path = Get_bossScenePath();
            trigger = true;
        }

        if (trigger)
        {
            const bool showDeath =
                (playerTrigger && Get_showDeathOnPlayerDeath()) ||
                (bossTrigger && Get_showDeathOnBossDeath());
            if (showDeath && m_deathWidgetId != InvalidEntityId)
            {
                if (auto* widget = world->GetComponent<UIWidgetComponent>(m_deathWidgetId))
                    widget->visibility = AliceUI::UIVisibility::Visible;
            }

            if (path.empty())
                path = Get_scenePath();

            if (path.empty())
            {
                ALICE_LOG_WARN("[MainChangerScript] Scene path is empty");
            }
            else
            {
                const float deathDelay = std::max(0.0f, Get_deathEffectDelaySec());
                m_pendingPath = path;
                m_pendingTimer = 0.0f;
                m_pending = true;

                if (Get_useFadeOnDeath() && m_fade)
                {
                    if (deathDelay > 0.0f)
                    {
                        m_pendingStage = PendingStage::WaitForFade;
                        m_pendingDelay = deathDelay;
                    }
                    else
                    {
                        m_fade->OnEnable();
                        m_fade->StartFadeIn();

                        float delay = Get_delaySec();
                        if (delay < 0.0f)
                            delay = ComputeAutoDelaySec();

                        m_pendingStage = PendingStage::WaitForScene;
                        m_pendingDelay = std::max(0.0f, delay);
                    }
                }
                else
                {
                    m_pendingStage = PendingStage::WaitForScene;
                    m_pendingDelay = deathDelay;
                }
            }
        }

        m_prevPlayerDead = playerDead;
        m_prevBossDead = bossDead;
    }

    float MainChangerScript::ComputeAutoDelaySec() const
    {
        if (!m_fade)
            return 0.0f;

        const float speed = std::max(0.0f, m_fade->Get_fadeSpeed());
        if (speed <= 0.0f)
            return 0.0f;

        const float startAlpha = std::clamp(m_fade->Get_startAlpha(), 0.0f, 1.0f);
        return (1.0f - startAlpha) / speed;
    }
}