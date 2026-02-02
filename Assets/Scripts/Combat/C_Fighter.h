#pragma once
/*
* 한 캐릭터의 전투 상태(HP, 스태미나, 팀, 상태, 플래그 등)와 BuildSensors/Snapshot 제공.
* C_CombatSession이 플레이어·보스용 Fighter 인스턴스 2개 보유.
*/
#include "C_CombatContracts.h"

namespace Alice
{
    class World;
}

namespace Alice::Combat
{
    struct FighterSnapshot
    {
        EntityId id = InvalidEntityId;
        Team team = Team::Player;
        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        float hp = 100.0f;
        float stamina = 100.0f;
        float weaponDurability = 100.0f;
        float weaponDurabilityMax = 100.0f;
        bool weakActive = false;
        bool targetInFront = true;
        bool canBeHitstunned = true;
    };

    class Fighter
    {
    public:
        EntityId id = InvalidEntityId;
        Team team = Team::Player;

        float hp = 100.0f;
        float stamina = 100.0f;
        float moveSpeed = 5.0f;
        float weaponDurability = 100.0f;
        float weaponDurabilityMax = 100.0f;
        float weakRemainingSec = 0.0f;

        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        bool canBeHitstunned = true;

        bool lastTargetInFront = true;

        Sensors BuildSensors(World& world, EntityId targetId, float dt);
        FighterSnapshot Snapshot() const;
    };
}
