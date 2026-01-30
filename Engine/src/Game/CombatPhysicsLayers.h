#pragma once

#include <cstdint>

#include "PhysX/Components/Phy_SettingsComponent.h"

namespace Alice::CombatPhysicsLayers
{
    constexpr uint32_t World = 0;
    constexpr uint32_t CharacterBody = 1;
    constexpr uint32_t PlayerHurtbox = 2;
    constexpr uint32_t EnemyHurtbox = 3;
    constexpr uint32_t PlayerAttackQuery = 4;
    constexpr uint32_t EnemyAttackQuery = 5;
    constexpr uint32_t Disabled = 31;

    constexpr uint32_t Bit(uint32_t index) noexcept
    {
        return (index < 32) ? (1u << index) : 0u;
    }

    constexpr uint32_t WorldBit = Bit(World);
    constexpr uint32_t CharacterBodyBit = Bit(CharacterBody);
    constexpr uint32_t PlayerHurtboxBit = Bit(PlayerHurtbox);
    constexpr uint32_t EnemyHurtboxBit = Bit(EnemyHurtbox);
    constexpr uint32_t PlayerAttackQueryBit = Bit(PlayerAttackQuery);
    constexpr uint32_t EnemyAttackQueryBit = Bit(EnemyAttackQuery);
    constexpr uint32_t DisabledBit = Bit(Disabled);

    inline uint32_t HurtboxLayerBit(uint32_t teamId) noexcept
    {
        return (teamId == 0) ? PlayerHurtboxBit : EnemyHurtboxBit;
    }

    inline uint32_t AttackQueryLayerBit(uint32_t teamId) noexcept
    {
        return (teamId == 0) ? PlayerAttackQueryBit : EnemyAttackQueryBit;
    }

    inline void ApplyDefaultCombatLayerMatrix(Phy_SettingsComponent& s)
    {
        // 기본: 전부 false로 초기화 후 필요한 것만 켬
        for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
        {
            for (int j = 0; j < MAX_PHYSICS_LAYERS; ++j)
            {
                s.layerCollideMatrix[i][j] = false;
                s.layerQueryMatrix[i][j] = false;
            }
        }

        // 레이어 이름 (최소 세트)
        s.layerNames[World] = "World";
        s.layerNames[CharacterBody] = "CharacterBody";
        s.layerNames[PlayerHurtbox] = "PlayerHurtbox";
        s.layerNames[EnemyHurtbox] = "EnemyHurtbox";
        s.layerNames[PlayerAttackQuery] = "PlayerAttackQuery";
        s.layerNames[EnemyAttackQuery] = "EnemyAttackQuery";
        s.layerNames[Disabled] = "Disabled";

        // Collide matrix (시뮬레이션/트리거 이벤트용)
        s.layerCollideMatrix[PlayerAttackQuery][EnemyHurtbox] = true;
        s.layerCollideMatrix[EnemyHurtbox][PlayerAttackQuery] = true;
        s.layerCollideMatrix[EnemyAttackQuery][PlayerHurtbox] = true;
        s.layerCollideMatrix[PlayerHurtbox][EnemyAttackQuery] = true;

        // Query matrix (레이캐스트/스윕/오버랩용)
        s.layerQueryMatrix[PlayerAttackQuery][EnemyHurtbox] = true;
        s.layerQueryMatrix[EnemyAttackQuery][PlayerHurtbox] = true;

        // Disabled 레이어는 아무것도 맞지 않도록 유지
    }
}
