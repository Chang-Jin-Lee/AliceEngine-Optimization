#pragma once
/*
키보드/마우스 입력을 읽어서 Combat::Intent(이동·공격·가드·회피 등)로 변환.
플레이어가 조작하는 캐릭터 엔티티 (플레이어 엔티티).
*/
#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "Core/InputTypes.h"

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
        ALICE_PROPERTY(int, m_keyItem, static_cast<int>(KeyCode::Z));
        ALICE_PROPERTY(int, m_keyInteract, static_cast<int>(KeyCode::F));
        ALICE_PROPERTY(int, m_keyRage, static_cast<int>(KeyCode::E));
        ALICE_PROPERTY(bool, m_useMouseAttack, true);
        ALICE_PROPERTY(bool, m_useMouseLockOn, true);
        ALICE_PROPERTY(int, m_mouseAttackButton, static_cast<int>(MouseCode::Left));
        ALICE_PROPERTY(int, m_mouseGuardButton, static_cast<int>(MouseCode::Right));
        ALICE_PROPERTY(int, m_mouseLockOnButton, static_cast<int>(MouseCode::Middle));
        ALICE_PROPERTY(float, m_attackHoldThresholdSec, 0.35f);
        ALICE_PROPERTY(float, m_parryWindowSec, 0.5f);
        ALICE_PROPERTY(bool, m_enableLogs, false);
        /// CCT ?? ?? (m/s). Intent.move ??? ??? desiredVelocity? ???.
        ALICE_PROPERTY(float, m_moveSpeed, 5.0f);

    private:
        void EnsurePlayerComponents();

        Combat::Intent m_cached{};
        bool m_attackHeldPrev = false;
        float m_attackHeldSec = 0.0f;
        bool m_guardHeldPrev = false;
        float m_guardHeldSec = 0.0f;
    };
}
