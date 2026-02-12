#pragma once

#include <string>
#include <cstdint>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class FadeInOutScript;

    class MainChangerScript : public IScript
    {
        ALICE_BODY(MainChangerScript);

    public:
        struct ClearResultSnapshot
        {
            float timeSec = 0.0f;
            std::uint64_t retryCount = 0;
            std::uint64_t guardCount = 0;
            std::uint64_t parryCount = 0;
            std::uint64_t damagedCount = 0;
            std::uint64_t breakCount = 0;
        };

        void Start() override;
        void Update(float deltaTime) override;

        static void SetClearResultSnapshot(const ClearResultSnapshot& snapshot);
        static bool GetClearResultSnapshot(ClearResultSnapshot& outSnapshot);
        static void ResetClearResultSnapshot();

        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(bool, triggerOnPlayerDeath, true);
        ALICE_PROPERTY(bool, triggerOnBossDeath, false);

        // Fallback scene path used when per-target path is empty.
        ALICE_PROPERTY(std::string, scenePath, "");
        ALICE_PROPERTY(std::string, playerScenePath, "");
        ALICE_PROPERTY(std::string, bossScenePath, "");

        // Optional fade-out before scene change.
        ALICE_PROPERTY(std::string, fadeEntityName, "");
        ALICE_PROPERTY(bool, useFadeOnDeath, true);

        // Optional death UI (shown when player dies).
        ALICE_PROPERTY(std::string, deathWidgetName, "UI_Death");
        ALICE_PROPERTY(bool, showDeathOnPlayerDeath, true);
        ALICE_PROPERTY(bool, showDeathOnBossDeath, false);
        // Delay before fade starts (death effect lead time).
        ALICE_PROPERTY(float, deathEffectDelaySec, 0.0f);

        ALICE_PROPERTY(float, delaySec, 0.0f);

    private:
        float ComputeAutoDelaySec() const;

        enum class PendingStage
        {
            None,
            WaitForFade,
            WaitForScene
        };

        bool m_prevPlayerDead{ false };
        bool m_prevBossDead{ false };
        bool m_pending{ false };
        float m_pendingTimer{ 0.0f };
        float m_pendingDelay{ 0.0f };
        std::string m_pendingPath;
        PendingStage m_pendingStage{ PendingStage::None };
        EntityId m_playerId{ InvalidEntityId };
        EntityId m_bossId{ InvalidEntityId };
        EntityId m_deathWidgetId{ InvalidEntityId };
        FadeInOutScript* m_fade{ nullptr };

        static ClearResultSnapshot s_lastClearResult;
        static bool s_hasLastClearResult;
    };
}
