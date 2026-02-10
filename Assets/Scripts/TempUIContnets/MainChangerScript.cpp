#include "MainChangerScript.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "../Combat/C_CombatSessionComponent.h"

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
    }

    void MainChangerScript::Start()
    {
        m_prevPlayerDead = false;
        m_prevBossDead = false;
        m_pending = false;
        m_pendingTimer = 0.0f;
        m_pendingPath.clear();
        
        // 플레이어와 보스 엔티티 찾기
        World* world = GetWorld();
        if (world)
        {
            GameObject playerGo = world->FindGameObject(Get_playerEntityName());
            GameObject bossGo = world->FindGameObject(Get_bossEntityName());
            m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
            m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
            
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
            const float delay = std::max(0.0f, Get_delaySec());
            m_pendingTimer += std::max(0.0f, deltaTime);
            if (m_pendingTimer >= delay)
            {
                auto* scenes = Scenes();
                if (!scenes)
                {
                    ALICE_LOG_WARN("[MainChangerScript] SceneManager not available");
                    m_pending = false;
                    return;
                }
                if (m_pendingPath.empty())
                {
                    ALICE_LOG_WARN("[MainChangerScript] Pending scene path is empty");
                    m_pending = false;
                    return;
                }
                scenes->LoadSceneFileRequest(m_pendingPath.c_str());
                m_pending = false;
            }
            return;
        }

        World* world = GetWorld();
        if (!world)
            return;

        // C_CombatSessionComponent와 직접 엔티티 체크 둘 다 사용
        bool playerDead = false;
        bool bossDead = false;

        // 방법 1: C_CombatSessionComponent를 통한 체크 (기존 방식)
        C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
        if (session)
        {
            playerDead = (session->GetPlayerState() == Combat::ActionState::Dead);
            bossDead = (session->GetBossState() == Combat::ActionState::Dead);
        }

        // 방법 2: 직접 HealthComponent 체크 (추가)
        if (m_playerId != InvalidEntityId)
        {
            if (auto* health = world->GetComponent<HealthComponent>(m_playerId))
            {
                if (health->currentHealth <= 0.0f)
                    playerDead = true;
            }
        }

        if (m_bossId != InvalidEntityId)
        {
            if (auto* health = world->GetComponent<HealthComponent>(m_bossId))
            {
                if (health->currentHealth <= 0.0f)
                    bossDead = true;
            }
        }

        std::string path;
        bool trigger = false;

        if (Get_triggerOnPlayerDeath() && playerDead && !m_prevPlayerDead)
        {
            path = Get_playerScenePath();
            trigger = true;
        }
        else if (Get_triggerOnBossDeath() && bossDead && !m_prevBossDead)
        {
            path = Get_bossScenePath();
            trigger = true;
        }

        if (trigger)
        {
            if (path.empty())
                path = Get_scenePath();

            if (path.empty())
            {
                ALICE_LOG_WARN("[MainChangerScript] Scene path is empty");
            }
            else
            {
                m_pendingPath = path;
                m_pendingTimer = 0.0f;
                m_pending = true;
            }
        }

        m_prevPlayerDead = playerDead;
        m_prevBossDead = bossDead;
    }
}
