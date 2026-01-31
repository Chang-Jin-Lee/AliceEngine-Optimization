#pragma once
/*
* 보스 AI. 타겟(플레이어)과의 거리/각도 보고 공격·이동 등 Combat::Intent 생성.
* 보스/적 캐릭터 엔티티 (보스 엔티티).
*/
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include "C_CombatContracts.h"

namespace Alice
{
    class C_BossBrainComponent : public IScript
    {
        ALICE_BODY(C_BossBrainComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::Intent Think(float deltaTime, EntityId targetId);

        ALICE_PROPERTY(float, m_attackRange, 2.5f);
        ALICE_PROPERTY(float, m_attackCooldown, 1.0f);
        ALICE_PROPERTY(float, m_moveBias, 1.0f);

    private:
        float m_cooldownTimer = 0.0f;
    };
}
