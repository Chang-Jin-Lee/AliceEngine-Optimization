// 캐릭터를 카메라 방향 기준으로 앞뒤좌우로 움직이게 하는 스크립트
//#include "CharacterMovement.h"
// #include "Core/ScriptFactory.h"
//#include "Core/Logger.h"
//#include "Core/GameObject.h"
//#include <cmath>
//
//#ifndef M_PI
//#define M_PI 3.14159265358979323846
//#endif
//
//namespace Alice {
//    REGISTER_SCRIPT(CharacterMovement);
//
//        void CharacterMovement::Attack() {
//            // 공격 로직 구현 (로그 출력 등)
//        }
//
//        void CharacterMovement::SetSpeed(float newSpeed) {
//            Set_m_moveSpeed(newSpeed);
//        }
//
//        void CharacterMovement::Update(float DeltaTime) {
//        auto* input = Input();
//        if (!input)
//            return;
//
//        auto go = gameObject();
//        auto* t = go.GetComponent<TransformComponent>();
//        auto anim = go.GetAnimator();
//        if (!t || !anim.IsValid())
//            return;
//
//        // --- 1. 카메라 정보 가져오기 ---
//        // (월드에서 Primary 카메라를 찾는다고 가정)
//        auto* world = GetWorld();
//        EntityId camId = world ? world->GetMainCameraEntityId() : 0;
//        TransformComponent* camT =
//            (world && camId != 0) ? world->GetTransform(camId) : nullptr;
//
//        float camYawRad = 0.0f;
//        if (camT) {
//            camYawRad = camT->rotation.y * (static_cast<float>(M_PI) / 180.0f);
//        }
//
//        // --- 2. 입력 수집 (로컬 기준) ---
//        float inputX = 0.0f; // A, D (좌우)
//        float inputZ = 0.0f; // W, S (전후)
//
//        if (input->GetKey(KeyCode::W))
//            inputZ += 1.0f;
//        if (input->GetKey(KeyCode::S))
//            inputZ -= 1.0f;
//        if (input->GetKey(KeyCode::D))
//            inputX += 1.0f;
//        if (input->GetKey(KeyCode::A))
//            inputX -= 1.0f;
//
//        // --- 3. 입력 벡터를 카메라 방향으로 회전 ---
//        // 2D 회전 행렬 공식 적용:
//        // x' = x * cos - z * sin
//        // z' = x * sin + z * cos
//        // (여기서 z는 전방이므로 수학적 y축 역할)
//        float sinY = std::sin(camYawRad);
//        float cosY = std::cos(camYawRad);
//
//        float worldX = inputX * cosY + inputZ * sinY;
//        float worldZ = -inputX * sinY + inputZ * cosY;
//
//        // --- 4. 이동 및 회전 적용 ---
//        float len = std::sqrt(worldX * worldX + worldZ * worldZ);
//        if (len > 0.0001f) {
//            // 정규화
//            worldX /= len;
//            worldZ /= len;
//
//            // 캐릭터 회전: 이동하려는 월드 방향을 바라봄
//            float radian = std::atan2(worldX, worldZ);
//            float degree = radian * (180.0f / static_cast<float>(M_PI));
//            t->SetRotation(0.0f, degree,
//                0.0f); // atan2(x, z)는 북쪽이 0이므로 +180 보정 불필요할 수
//            // 있음 (좌표계 확인 필요)
//
//            anim.Play(2); // Walk
//        }
//        else {
//            anim.Play(0); // Idle
//        }
//
//        // 위치 이동
//        t->position.x += worldX * Get_m_moveSpeed() * DeltaTime;
//        t->position.z += worldZ * Get_m_moveSpeed() * DeltaTime;
//
//        // --- 5. 점프/중력 (기존 유지) ---
//        if (t->position.y <= 0.0f) {
//            t->position.y = 0.0f;
//            if (m_velY < 0.0f)
//                m_velY = 0.0f;
//            if (input->GetKeyDown(KeyCode::Space))
//                m_velY = Get_m_jumpSpeed();
//        }
//        m_velY -= Get_m_gravity() * DeltaTime;
//        t->position.y += m_velY * DeltaTime;
//        if (t->position.y < 0.0f)
//            t->position.y = 0.0f;
//    }
//} // namespace Alice

// 캐릭터를 그냥 단순히 앞뒤좌우로 움직이게 하는 스크립트
#include "CharacterMovement.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"
#include "Core/Input.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Alice
{
    REGISTER_SCRIPT(CharacterMovement);

    void CharacterMovement::Attack() { /* 공격 로직 */ }

    void CharacterMovement::Update(float DeltaTime)
    {
        auto* input = Input();
        auto go = gameObject();
        if (!input || !go.IsValid()) return;

        auto* t = go.GetComponent<TransformComponent>();
        auto anim = go.GetAnimator();
        if (!t || !anim.IsValid()) return;

        // --- 1. 입력 수집 ---
        float inputX = 0.0f; // A, D
        float inputZ = 0.0f; // W, S

        if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
        if (input->GetKey(KeyCode::D)) inputX += 1.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

        // 입력이 없으면 이동 계산 건너뛰고 중력만 처리
        bool hasInput = (inputX != 0.0f || inputZ != 0.0f);

        // --- 2. 카메라 기준 방향 계산 (핵심 로직) ---
        float moveX = 0.0f;
        float moveZ = 0.0f;

        // 메인 카메라 찾기 (태그나 이름으로 검색 가정)
        auto mainCamObj = GetWorld()->FindGameObject("MainCamera");
        //ALICE_LOG_INFO("TEST");


        if (hasInput && mainCamObj.IsValid())
        {
            auto* camT = mainCamObj.GetComponent<TransformComponent>();
            if (camT)
            {
                //ALICE_LOG_INFO("Camera Position: x={0}, y={1}, z={2}",
				//	camT->position.x, camT->position.y, camT->position.z);
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

        float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);

        if (moveLen > 0.0001f)
        {
            // 정규화
            moveX /= moveLen;
            moveZ /= moveLen;

            // [위치 이동]
            t->position.x += moveX * Get_m_moveSpeed() * DeltaTime;
            t->position.z += moveZ * Get_m_moveSpeed() * DeltaTime;

            // [회전] 이동하는 방향 바라보기
            float radian = std::atan2(moveX, moveZ);
            float degree = radian * (180.0f / static_cast<float>(M_PI));

            // [수정] 모델이 반대로 보이면 180도를 더해서 뒤집어 줍니다.
            // 만약 90도로 꺾여서 달린다면 90.0f나 -90.0f를 더해보세요.
            t->SetRotation(0.0f, degree + 180.0f, 0.0f);

            // [애니메이션] 걷기
            anim.Play(2);
        }
        else
        {
            // [애니메이션] 대기
            anim.Play(0);
        }

        // --- 4. 점프 및 중력 로직 (기존 유지) ---
        const bool grounded = (t->position.y <= 0.0f);
        if (grounded)
        {
            t->position.y = 0.0f;
            if (m_velY < 0.0f) m_velY = 0.0f;

            if (input->GetKeyDown(KeyCode::Space))
            {
                m_velY = Get_m_jumpSpeed();
                // anim.Play(3, true); // 점프
            }
        }

        m_velY -= Get_m_gravity() * DeltaTime;
        t->position.y += m_velY * DeltaTime;

        if (t->position.y < 0.0f) t->position.y = 0.0f;
    }
}