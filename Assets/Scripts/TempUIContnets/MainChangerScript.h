#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class MainChangerScript : public IScript
    {
        ALICE_BODY(MainChangerScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(bool, triggerOnPlayerDeath, true);
        ALICE_PROPERTY(bool, triggerOnBossDeath, false);

        // Fallback scene path used when per-target path is empty.
        ALICE_PROPERTY(std::string, scenePath, "");
        ALICE_PROPERTY(std::string, playerScenePath, "");
        ALICE_PROPERTY(std::string, bossScenePath, "");

        ALICE_PROPERTY(float, delaySec, 0.0f);

    private:
        bool m_prevPlayerDead{ false };
        bool m_prevBossDead{ false };
        bool m_pending{ false };
        float m_pendingTimer{ 0.0f };
        std::string m_pendingPath;
        EntityId m_playerId{ InvalidEntityId };
        EntityId m_bossId{ InvalidEntityId };
    };
}
