#pragma once
/*
* 프레임 단위 Hit 이벤트 수집, 지연 CombatEvent(OnHit/OnGuarded/OnParried 등) 보관·조회.
* C_CombatSession이 하나 보유하고 매 프레임 ClearFrame.
*/
#include <unordered_map>
#include <vector>

#include "C_CombatContracts.h"

namespace Alice::Combat
{
    class CombatEventBus
    {
    public:
        void ClearFrame();
        void ClearAll();

        void PushHit(const HitEvent& e);
        const std::vector<HitEvent>& Hits() const;

        void PushDeferred(const CombatEvent& e);
        const std::vector<CombatEvent>& PeekDeferred(EntityId who) const;
        void ClearDeferred(EntityId who);

    private:
        std::vector<HitEvent> m_hitEvents;
        std::unordered_map<EntityId, std::vector<CombatEvent>> m_deferredByEntity;
    };
}
