#pragma once
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CharacterMovement : public IScript
    {
        ALICE_BODY(CharacterMovement);

    public:
        void Update(float DeltaTime) override;

        // --- 변수 리플렉션 ---
        ALICE_PROPERTY(float, m_moveSpeed, 10.0f);
        ALICE_PROPERTY(float, m_jumpSpeed, 6.5f);
        ALICE_PROPERTY(float, m_gravity, 18.0f); // 누락된 중력 변수 추가

        // --- 함수 리플렉션 ---
        void Attack();
        ALICE_FUNC(Attack);

    private:
        float m_velY = 0.0f;
    };
}