#pragma once

#include <string>
#include <vector>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class World;

    class BossUiActivationGateScript : public IScript
    {
        ALICE_BODY(BossUiActivationGateScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    public:
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, bossBrainScriptName, "C_BossBrainComponent");
        ALICE_PROPERTY(std::string, rootWidgetName, "UI_BossInfor");
        ALICE_PROPERTY(std::string, additionalWidgetNamesCsv, "");
        ALICE_PROPERTY(bool, hideOnStart, true);
        ALICE_PROPERTY(bool, rehideWhenInactive, false);

    private:
        void RefreshBindings();
        bool IsBossBrainActivated() const;
        void SetBossUiVisible(bool visible);

        static std::vector<std::string> ParseCsv(const std::string& csv);
        static EntityId FindWidgetByName(World& world, const std::string& name);

    private:
        EntityId m_bossId = InvalidEntityId;
        EntityId m_rootWidgetId = InvalidEntityId;
        std::vector<EntityId> m_extraWidgetIds{};

        bool m_uiVisible = true;
        bool m_shownOnce = false;
        bool m_bindingsDirty = true;
    };
}
