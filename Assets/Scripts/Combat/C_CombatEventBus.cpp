#include "C_CombatEventBus.h"

namespace Alice::Combat
{
    void CombatEventBus::ClearFrame()
    {
        m_hitEvents.clear();
    }

    void CombatEventBus::ClearAll()
    {
        m_hitEvents.clear();
        m_deferredByEntity.clear();
    }

    void CombatEventBus::PushHit(const HitEvent& e)
    {
        m_hitEvents.push_back(e);
    }

    const std::vector<HitEvent>& CombatEventBus::Hits() const
    {
        return m_hitEvents;
    }

    void CombatEventBus::PushDeferred(const CombatEvent& e)
    {
        m_deferredByEntity[e.subject].push_back(e);
    }

    const std::vector<CombatEvent>& CombatEventBus::PeekDeferred(EntityId who) const
    {
        static const std::vector<CombatEvent> kEmpty;
        auto it = m_deferredByEntity.find(who);
        return (it != m_deferredByEntity.end()) ? it->second : kEmpty;
    }

    void CombatEventBus::ClearDeferred(EntityId who)
    {
        m_deferredByEntity.erase(who);
    }
}
