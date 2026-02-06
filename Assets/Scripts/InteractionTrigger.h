#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class C_CombatSessionComponent;
    class World;

    // Trigger zone that enables interaction (F) for the player.
    // Uses a physics overlap query against this object's collider shape.
    class InteractionTrigger : public IScript
    {
        ALICE_BODY(InteractionTrigger);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        // Target player lookup.
        ALICE_PROPERTY(std::string, m_playerName, "Player");
        ALICE_PROPERTY(uint64_t, m_playerGuid, 0);

        // Combat session lookup (manager entity name).
        ALICE_PROPERTY(std::string, m_combatSessionName, "CombatSession");

        // Once interaction starts, disable this trigger permanently.
        ALICE_PROPERTY(bool, m_oneShot, true);

        // Debug logging.
        ALICE_PROPERTY(bool, m_enableLogs, false);

    private:
        EntityId ResolvePlayerId(World* world);
        C_CombatSessionComponent* ResolveCombatSession(World* world);
        bool IsPlayerInside(World* world, EntityId playerId);
        void ApplyInteractionEnabled(C_CombatSessionComponent* session, bool enabled);

        EntityId m_cachedPlayerId = InvalidEntityId;
        std::uint32_t m_cachedPlayerGen = 0;

        EntityId m_cachedSessionId = InvalidEntityId;
        std::uint32_t m_cachedSessionGen = 0;

        bool m_used = false;
        bool m_lastEnabled = false;
        bool m_warnedMissingCollider = false;
        bool m_warnedMissingSession = false;
        bool m_warnedMissingPlayer = false;
    };
}
