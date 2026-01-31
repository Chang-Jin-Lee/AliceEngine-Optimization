#pragma once
/*
키보드/마우스 입력을 읽어서 Combat::Intent(이동·공격·가드·회피 등)로 변환.
플레이어가 조작하는 캐릭터 엔티티 (플레이어 엔티티).
*/
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Input/InputTypes.h"

#include "C_CombatContracts.h"

namespace Alice
{
    class C_PlayerInputSourceComponent : public IScript
    {
        ALICE_BODY(C_PlayerInputSourceComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        Combat::Intent GetIntent(float deltaTime);

        ALICE_PROPERTY(int, m_keyForward, static_cast<int>(KeyCode::W));
        ALICE_PROPERTY(int, m_keyBackward, static_cast<int>(KeyCode::S));
        ALICE_PROPERTY(int, m_keyLeft, static_cast<int>(KeyCode::A));
        ALICE_PROPERTY(int, m_keyRight, static_cast<int>(KeyCode::D));
        ALICE_PROPERTY(int, m_keyAttack, static_cast<int>(KeyCode::J));
        ALICE_PROPERTY(int, m_keyDodge, static_cast<int>(KeyCode::Space));
        ALICE_PROPERTY(int, m_keyGuard, static_cast<int>(KeyCode::K));
        ALICE_PROPERTY(bool, m_useMouseAttack, true);
        /// CCT ?? ?? (m/s). Intent.move ??? ??? desiredVelocity? ???.
        ALICE_PROPERTY(float, m_moveSpeed, 5.0f);

    private:
        void EnsurePlayerComponents();

        Combat::Intent m_cached{};
    };
}
