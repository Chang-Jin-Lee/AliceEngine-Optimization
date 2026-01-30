#include "CharacterPhysMove.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"
#include "Core/World.h"
#include "Core/Input.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "Components/TransformComponent.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(CharacterPhysMove);

    void CharacterPhysMove::Start()
    {
        // CCT 기반 이동 초기화
        auto go = gameObject();
        if (!go.IsValid()) return;

        auto* transform = go.GetComponent<TransformComponent>();
        if (!transform)
        {
            ALICE_LOG_WARN("[CharacterPhysMove] TransformComponent not found!");
            return;
        }

        // Phy_CCTComponent 확인 및 자동 추가
        auto* cct = go.GetComponent<Phy_CCTComponent>();
        if (!cct)
        {
            ALICE_LOG_INFO("[CharacterPhysMove] Phy_CCTComponent not found. Adding it...");
            go.AddComponent<Phy_CCTComponent>();
            cct = go.GetComponent<Phy_CCTComponent>();
            
            if (!cct)
            {
                ALICE_LOG_ERRORF("[CharacterPhysMove] Failed to add Phy_CCTComponent!");
                return;
            }
        }

        // CCT 컴포넌트의 점프 속도 초기화 (스크립트의 점프 속도와 동기화)
        cct->jumpSpeed = Get_m_jumpSpeed();

        ALICE_LOG_INFO("[CharacterPhysMove] CCT 기반 이동 초기화 완료 - Move Speed: %.2f m/s, Jump Speed: %.2f m/s", 
            Get_m_moveSpeed(), Get_m_jumpSpeed());
    }

    void CharacterPhysMove::Update(float deltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid()) return;

        auto* cct = go.GetComponent<Phy_CCTComponent>();
        auto* t = go.GetComponent<TransformComponent>();
        auto anim = go.GetAnimator();
        
        if (!cct || !t) return;

        // --- 1. 입력 수집 ---
        float inputX = 0.0f; // A, D
        float inputZ = 0.0f; // W, S

        if (input->GetKey(KeyCode::W)) inputZ += 3.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 3.0f;
        if (input->GetKey(KeyCode::D)) inputX += 3.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 3.0f;

        // 입력이 없으면 이동 계산 건너뛰기
        bool hasInput = (inputX != 0.0f || inputZ != 0.0f);

        // --- 2. 카메라 기준 방향 계산 (CharacterMovement와 동일한 로직) ---
        float moveX = 0.0f;
        float moveZ = 0.0f;

        // 메인 카메라 찾기
        auto mainCamObj = GetWorld()->FindGameObject("MainCamera");

        if (hasInput && mainCamObj.IsValid())
        {
            auto* camT = mainCamObj.GetComponent<TransformComponent>();
            if (camT)
            {
                // Forward: 나(Target) - 카메라(Eye) = 화면 깊이 방향
                float fwdX = t->position.x - camT->position.x;
                float fwdZ = t->position.z - camT->position.z;

                // Y축 제거 및 정규화
                float lenFwd = std::sqrt(fwdX * fwdX + fwdZ * fwdZ);
                if (lenFwd > 0.0001f)
                {
                    fwdX /= lenFwd;
                    fwdZ /= lenFwd;
                }

                // Right: Forward의 수직 벡터 (z, -x)
                float rightX = fwdZ;
                float rightZ = -fwdX;

                // 최종 이동 벡터 합성
                moveX = (fwdX * inputZ) + (rightX * inputX);
                moveZ = (fwdZ * inputZ) + (rightZ * inputX);
            }
        }
        else if (hasInput)
        {
            // 카메라를 못 찾았을 경우 비상용 (절대좌표 이동)
            moveX = inputX;
            moveZ = inputZ;
        }

        // --- 3. CCT 기반 이동 설정 ---
        float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);

        if (moveLen > 0.0001f)
        {
            // 방향 벡터 정규화
            moveX /= moveLen;
            moveZ /= moveLen;

            // CCT의 desiredVelocity 설정 (XZ 평면만, Y는 PhysicsSystem이 중력/점프로 처리)
            // 단위: m/s (초당 미터)
            cct->desiredVelocity.x = moveX * Get_m_moveSpeed();
            cct->desiredVelocity.z = moveZ * Get_m_moveSpeed();
            cct->desiredVelocity.y = 0.0f; // Y축은 명시적으로 0으로 설정 (PhysicsSystem이 verticalVelocity로 처리)
            
            // 디버깅: 이동 속도가 설정되었는지 확인
            static int logCounter = 0;
            if (++logCounter % 60 == 0) // 1초마다 로그
            {
                ALICE_LOG_INFO("[CharacterPhysMove] desiredVelocity: (%.2f, %.2f, %.2f) m/s, controllerHandle: %p",
                    cct->desiredVelocity.x, cct->desiredVelocity.y, cct->desiredVelocity.z, cct->controllerHandle);
            }

            // [회전] 이동 방향으로 캐릭터 회전
            // CharacterMovement와 동일한 로직: atan2(x, z)로 Yaw 회전 계산
            // 이 엔진에서는 TransformComponent rotation: (Pitch, Yaw, Roll) = (X, Y, Z)
            // 수평 회전은 Yaw(Y축)이므로 Y축 회전만 설정
            // (다른 스크립트들 확인: CameraFollow, CameraController, RotateAndScale 모두 rotation.y 사용)
            float radian = std::atan2(moveX, moveZ);
            float degree = radian * (180.0f / static_cast<float>(M_PI));
            t->SetRotation(0.0f, degree + Get_m_rotationOffset(), 0.0f);

            // [애니메이션] 걷기
            if (anim.IsValid())
            {
                anim.Play(2);
            }
        }
        else
        {
            // 입력이 없으면 정지
            cct->desiredVelocity.x = 0.0f;
            cct->desiredVelocity.z = 0.0f;
            // Y축 속도는 PhysicsSystem이 유지 (중력 적용)

            // [애니메이션] 대기
            if (anim.IsValid())
            {
                anim.Play(0);
            }
        }

        // --- 4. 점프 처리 (CCT 기반) ---
        // CCT의 onGround 상태 확인 (PhysicsSystem이 매 프레임 업데이트)
        if (input->GetKeyDown(KeyCode::Space) && cct->onGround)
        {
            // CCT 컴포넌트의 jumpSpeed 사용 (인스펙터에서 조정 가능)
            cct->jumpSpeed = Get_m_jumpSpeed(); // 스크립트 값과 동기화
            cct->jumpRequested = true;
            
            // 점프 애니메이션 (선택적)
            if (anim.IsValid())
            {
                // anim.Play(3); // 점프 애니메이션 인덱스가 있으면 활성화
            }
        }

        // --- CCT 동작 방식 설명 ---
        // 1. desiredVelocity.x/z: 수평 이동 속도 (m/s) - 이 스크립트에서 설정
        // 2. jumpRequested: 점프 요청 플래그 - PhysicsSystem이 처리 후 자동으로 false로 설정
        // 3. verticalVelocity: 수직 속도 - PhysicsSystem이 중력과 점프로 자동 계산
        // 4. onGround: 지면 접촉 여부 - PhysicsSystem이 레이캐스트로 확인
        // 5. Transform.position: PhysicsSystem이 CCT의 foot position으로 자동 동기화
        // 
        // PhysicsSystem의 "6. CCT 이동 + 중력/점프 처리 + Transform 갱신" 단계에서:
        // - desiredVelocity와 verticalVelocity를 합쳐서 이동
        // - 충돌 처리 및 지면 감지
        // - Transform.position을 CCT의 foot position으로 업데이트
    }
}
