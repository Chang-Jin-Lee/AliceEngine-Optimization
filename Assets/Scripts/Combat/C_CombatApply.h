#pragma once
/*
* Resolver가 만든 Immediate Command(데미지/스태미나/캔슬/트레이스 OFF 등)를 엔진 컴포넌트에 적용.
* FSM/Resolver 로직과 엔진 적용을 분리하기 위한 브릿지 역할.
*/
#include <unordered_map>
#include <vector>

#include "Core/Entity.h"
#include "C_CombatContracts.h"

namespace Alice
{
    class World;
}

namespace Alice::Combat
{
    class CombatEventBus;
    class Fighter;

    class CombatApply
    {
    public:
        void ApplyImmediate(World& world,
                            std::unordered_map<EntityId, Fighter*>& fighters,
                            CombatEventBus& bus,
                            const std::vector<Command>& cmds,
                            bool skipDamage);
    };
}
