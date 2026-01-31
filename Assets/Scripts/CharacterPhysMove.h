#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // CCT(Character Controller) 기반 캐릭터 이동 스크립트
    // 
    // 작동 방식:
    // 1. Phy_CCTComponent의 desiredVelocity를 설정하여 수평 이동 제어
    // 2. jumpRequested 플래그로 점프 요청
    // 3. PhysicsSystem이 CCT를 통해 실제 이동, 중력, 충돌 처리
    // 4. Transform.position은 PhysicsSystem이 CCT의 foot position으로 자동 동기화
    //
    // 특징:
    // - RigidBody와 달리 직접적인 물리 시뮬레이션이 아닌, 쿼리 기반 이동
    // - 미끄러짐 없이 정확한 캐릭터 제어 가능
    // - 계단 오르기, 경사면 처리 등 자동화
    class CharacterPhysMove : public IScript
    {
        ALICE_BODY(CharacterPhysMove);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 (에디터에서 수정 가능) ---
        ALICE_PROPERTY(float, m_moveSpeed, 5.0f);      // 수평 이동 속도 (m/s)
        ALICE_PROPERTY(float, m_jumpSpeed, 5.5f);      // 점프 초기 속도 (m/s)
        ALICE_PROPERTY(float, m_rotationOffset, 180.0f); // 회전 오프셋 (모델 방향 보정용, 도 단위)

    private:
        // 내부 상태 (리플렉션 불필요)
    };
}
