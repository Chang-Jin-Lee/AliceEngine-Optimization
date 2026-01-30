#pragma once

#include <cstdint>

#include <DirectXMath.h>

#include "Core/Entity.h"

namespace Alice
{
    struct HealthComponent
    {
        float maxHealth = 100.0f;
        float currentHealth = 100.0f;

        float invulnDuration = 0.0f;
        float invulnRemaining = 0.0f;

        // 프레임 기반 상태 (AttackDriver가 갱신)
        bool dodgeActive = false;
        bool guardActive = false;

        // Guard 시 데미지 배율 (0.5 = 50% 피해)
        float guardDamageScale = 0.5f;

        // Groggy (boss only)
        float groggy = 0.0f;
        float groggyMax = 100.0f;
        float groggyGainScale = 1.0f; // hit damage * scale
        float groggyDuration = 1.5f;

        bool alive = true;
        uint32_t teamId = 0;

        // Runtime hit info (CombatSystem이 갱신)
        bool hitThisFrame = false;
        bool guardHitThisFrame = false;
        bool dodgeAvoidedThisFrame = false;

        float lastHitDamage = 0.0f; // 적용된 데미지 (Guard/회피 반영)
        EntityId lastHitAttacker = InvalidEntityId;
        uint32_t lastHitPart = 0;
        DirectX::XMFLOAT3 lastHitPosWS{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 lastHitNormalWS{ 0.0f, 1.0f, 0.0f };
    };
}
