#include "ClearResultStatsScript.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "MainChangerScript.h"

namespace Alice
{
    namespace
    {
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

        std::string FormatWithComma(std::int64_t value)
        {
            bool negative = value < 0;
            std::uint64_t absValue = negative
                ? static_cast<std::uint64_t>(-(value + 1)) + 1ULL
                : static_cast<std::uint64_t>(value);

            std::string digits = std::to_string(absValue);
            for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
            {
                digits.insert(static_cast<std::size_t>(i), ",");
            }
            return negative ? ("-" + digits) : digits;
        }

        std::string FormatTimeSec(float timeSec)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << std::max(0.0f, timeSec) << "s";
            return oss.str();
        }

        std::string FormatPercent(double ratio)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (std::max(0.0, ratio) * 100.0) << "%";
            return oss.str();
        }
    }

    REGISTER_SCRIPT(ClearResultStatsScript);

    void ClearResultStatsScript::Start()
    {
        m_applied = false;
        ResolveWidgets();
        ApplySnapshot();
    }

    void ClearResultStatsScript::Update(float deltaTime)
    {
        (void)deltaTime;
        if (m_applied)
            return;

        ResolveWidgets();
        ApplySnapshot();
    }

    void ClearResultStatsScript::ResolveWidgets()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto resolveText = [&](const std::string& widgetName, UITextComponent*& outText, const char* label)
            {
                outText = nullptr;
                const EntityId id = FindWidgetByName(*world, widgetName);
                if (id == InvalidEntityId)
                {
                    ALICE_LOG_WARN("[ClearResultStatsScript] %s widget not found: %s", label, widgetName.c_str());
                    return;
                }

                outText = world->GetComponent<UITextComponent>(id);
                if (!outText)
                    ALICE_LOG_WARN("[ClearResultStatsScript] %s UIText not found on widget: %s", label, widgetName.c_str());
            };

        resolveText(Get_timeWidgetName(), m_timeText, "time");
        resolveText(Get_retryWidgetName(), m_retryText, "retry");
        resolveText(Get_guardRateWidgetName(), m_guardRateText, "guard_rate");
        resolveText(Get_parryRateWidgetName(), m_parryRateText, "parry_rate");
        resolveText(Get_damagedRateWidgetName(), m_damagedRateText, "damaged_rate");
        resolveText(Get_weaponBreakWidgetName(), m_weaponBreakText, "weapon_break");
        resolveText(Get_totalWidgetName(), m_totalText, "total");
    }

    void ClearResultStatsScript::ApplySnapshot()
    {
        if (!m_timeText && !m_retryText && !m_guardRateText && !m_parryRateText
            && !m_damagedRateText && !m_weaponBreakText && !m_totalText)
        {
            return;
        }

        MainChangerScript::ClearResultSnapshot snapshot{};
        if (!MainChangerScript::GetClearResultSnapshot(snapshot))
            snapshot = {};

        const double guard = static_cast<double>(snapshot.guardCount);
        const double parry = static_cast<double>(snapshot.parryCount);
        const double damaged = static_cast<double>(snapshot.damagedCount);
        const double breakCount = static_cast<double>(snapshot.breakCount);

        const double denominator = guard + parry + damaged;
        double guardRate = 0.0;
        double parryRate = 0.0;
        double hitRate = 0.0;
        if (denominator > 0.0)
        {
            guardRate = guard / denominator;
            parryRate = parry / denominator;
            hitRate = damaged / denominator;
        }

        const double totalScore = 10000.0
            - (static_cast<double>(snapshot.timeSec) * 5.0)
            - (static_cast<double>(snapshot.retryCount) * 500.0)
            + (parryRate * 15000.0)
            + (guardRate * 5000.0)
            - (hitRate * 10000.0)
            - (breakCount * 200.0);

        if (m_timeText)
            m_timeText->text = FormatTimeSec(snapshot.timeSec);
        if (m_retryText)
            m_retryText->text = FormatWithComma(static_cast<std::int64_t>(snapshot.retryCount));
        if (m_guardRateText)
            m_guardRateText->text = FormatPercent(guardRate);
        if (m_parryRateText)
            m_parryRateText->text = FormatPercent(parryRate);
        if (m_damagedRateText)
            m_damagedRateText->text = FormatPercent(hitRate);
        if (m_weaponBreakText)
            m_weaponBreakText->text = FormatWithComma(static_cast<std::int64_t>(snapshot.breakCount));
        if (m_totalText)
            m_totalText->text = FormatWithComma(static_cast<std::int64_t>(std::llround(totalScore)));

        // Clear consumed snapshot so next clear scene entry starts from fresh defaults.
        MainChangerScript::ResetClearResultSnapshot();
        m_applied = true;
    }
}
