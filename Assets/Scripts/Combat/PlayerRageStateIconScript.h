#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class C_CombatSessionComponent;
    struct UIEffectComponent;

    class PlayerRageStateIconScript : public IScript
    {
        ALICE_BODY(PlayerRageStateIconScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, cooldownIconWidgetName, "UI_RageCooldownIcon");
        ALICE_PROPERTY(std::string, durationIconWidgetName, "UI_RageDurationIcon");

        // Optional radial tuning (defaults match AdvancedAliceUI cooldown icon style).
        ALICE_PROPERTY(float, radialInner, 0.0f);
        ALICE_PROPERTY(float, radialOuter, 0.5f);
        ALICE_PROPERTY(float, radialSoftness, 0.01f);
        ALICE_PROPERTY(float, radialDim, 0.35f);
        ALICE_PROPERTY(bool, radialClockwise, true);

    private:
        void ResolveSession();
        void ResolveIcons();
        void ApplyRadialDefaults(UIEffectComponent* effect) const;
        void SetIconVisible(EntityId id, bool visible) const;
        void SetRadialFill(UIEffectComponent* effect, float fill01) const;

        C_CombatSessionComponent* m_session = nullptr;

        EntityId m_cooldownIconId = InvalidEntityId;
        EntityId m_durationIconId = InvalidEntityId;
        UIEffectComponent* m_cooldownEffect = nullptr;
        UIEffectComponent* m_durationEffect = nullptr;
    };
}

