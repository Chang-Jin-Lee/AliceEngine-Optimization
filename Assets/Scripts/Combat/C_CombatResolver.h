#pragma once
/*
* Hit 이벤트 + 공격자/피격자 스냅샷으로 데미지·스태미나·경직·애니 등 Command 리스트 생성.
* C_CombatSession이 하나 보유하고 Hit 처리 시 사용.
*/
#include "C_CombatContracts.h"
#include "C_Fighter.h"

namespace Alice::Combat
{
    class CombatResolver
    {
    public:
        ResolveOutput ResolveBatch(const std::vector<HitEvent>& hits,
                                   const FighterSnapshot& attackerSnap,
                                   const FighterSnapshot& victimSnap) const;

        ResolveOutput ResolveOne(const HitEvent& hit,
                                 const FighterSnapshot& attacker,
                                 const FighterSnapshot& victim) const;
    };
}
