#pragma once

#include <string>
#include <cstdint>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // Sync weapon AdvancedAnimation from a source character (LateUpdate).
    class WeaponAnimationController : public IScript
    {
        ALICE_BODY(WeaponAnimationController);

    public:
        void LateUpdate(float deltaTime) override;

        // Source to follow (use guid if set, otherwise name)
        ALICE_PROPERTY(std::uint64_t, m_sourceGuid, 0);
        ALICE_PROPERTY(std::string, m_sourceEntityName, "Player(Tia)");

        // Clip name prefix remap (source -> target)
        // Example: "rig|Tia_" -> "Armature|Tia_"
        ALICE_PROPERTY(std::string, m_sourceClipPrefix, "rig|Tia_");
        ALICE_PROPERTY(std::string, m_targetClipPrefix, "Armature|Tia_");

        ALICE_PROPERTY(bool, m_syncEnabled, true);
        ALICE_PROPERTY(bool, m_logMissingOnce, true);

    private:
        EntityId ResolveSourceEntity();
        void CopyFromSource(const struct AdvancedAnimationComponent& src,
                            struct AdvancedAnimationComponent& dst);

        EntityId m_cachedSourceId = InvalidEntityId;
        bool m_loggedMissing = false;
    };
}
