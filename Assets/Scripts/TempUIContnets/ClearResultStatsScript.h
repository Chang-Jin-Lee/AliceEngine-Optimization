#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UITextComponent.h"

namespace Alice
{
    class ClearResultStatsScript : public IScript
    {
        ALICE_BODY(ClearResultStatsScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, timeWidgetName, "UI_Time");
        ALICE_PROPERTY(std::string, retryWidgetName, "UI_Perry");
        ALICE_PROPERTY(std::string, guardRateWidgetName, "UI_GuardText");
        ALICE_PROPERTY(std::string, parryRateWidgetName, "UI_ParryText");
        ALICE_PROPERTY(std::string, damagedRateWidgetName, "UI_DamageRate");
        ALICE_PROPERTY(std::string, weaponBreakWidgetName, "UI_WeaponBreak");
        ALICE_PROPERTY(std::string, totalWidgetName, "UI_Total");

    private:
        void ResolveWidgets();
        void ApplySnapshot();

        bool m_applied = false;
        UITextComponent* m_timeText = nullptr;
        UITextComponent* m_retryText = nullptr;
        UITextComponent* m_guardRateText = nullptr;
        UITextComponent* m_parryRateText = nullptr;
        UITextComponent* m_damagedRateText = nullptr;
        UITextComponent* m_weaponBreakText = nullptr;
        UITextComponent* m_totalText = nullptr;
    };
}
