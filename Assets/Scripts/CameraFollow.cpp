//#include "CameraFollow.h"
// #include "Core/ScriptFactory.h"
//#include "Core/Logger.h"
//#include "Core/GameObject.h"
//#include <algorithm> // std::clamp
//#include <cmath>     // sin, cos
//
//#ifndef M_PI
//#define M_PI 3.14159265358979323846
//#endif
//
//namespace Alice
//{
//    REGISTER_SCRIPT(CameraFollow);
//
//    ALICE_SCRIPT_REFLECT_BEGIN(CameraFollow)
//        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_distance)     // 타겟과의 거리
//        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_sensitivity)  // 마우스 회전 감도
//        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_heightOffset) // 타겟의 높이 보정 (머리 위 등)
//        ALICE_SCRIPT_REFLECT_END()
//
//        void CameraFollow::LateUpdate(float lateDeltaTime)
//    {
//        auto go = gameObject();
//        if (!go.IsValid()) return;
//
//        // 플레이어(SkinnedMesh) 찾기
//        auto target = go.FindFirstSkinnedMesh();
//        if (!target.IsValid()) return;
//
//        auto* myT = go.GetComponent<TransformComponent>();
//        auto* tT = target.GetComponent<TransformComponent>();
//        if (!myT || !tT) return;
//
//        auto* input = Input();
//
//        // 1. 마우스 우클릭 시 회전 값 갱신
//        if (input && input->GetMouseButton(1))
//        {
//            // 마우스 이동량 가져오기 (엔진 API에 따라 다를 수 있음)
//            float mouseX = input->GetMouseDeltaX();
//            float mouseY = input->GetMouseDeltaY();
//
//            m_yaw += mouseX * Get_m_sensitivity();
//            m_pitch -= mouseY * Get_m_sensitivity();
//
//            // 상하 회전 제한 (너무 위/아래로 꺾이지 않게)
//            m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
//        }
//
//        // 2. 구면 좌표계를 이용한 카메라 위치 계산
//        // Yaw/Pitch를 라디안으로 변환
//        float radYaw = m_yaw * (static_cast<float>(M_PI) / 180.0f);
//        float radPitch = m_pitch * (static_cast<float>(M_PI) / 180.0f);
//
//        // 타겟 위치 (높이 오프셋 적용)
//        float targetX = tT->position.x;
//        float targetY = tT->position.y + Get_m_heightOffset();
//        float targetZ = tT->position.z;
//
//        // 회전된 오프셋 계산 (Spherical -> Cartesian)
//        float hDist = Get_m_distance() * std::cos(radPitch); // 수평 거리
//        float offsetX = hDist * std::sin(radYaw);
//        float offsetY = Get_m_distance() * std::sin(radPitch);
//        float offsetZ = hDist * std::cos(radYaw);
//
//        // 3. 최종 위치 및 회전 적용
//        // 카메라는 타겟 뒤쪽(거리만큼 떨어진 곳)에 위치해야 하므로 뺍니다.
//        myT->position.x = targetX - offsetX;
//        myT->position.y = targetY + offsetY;
//        myT->position.z = targetZ - offsetZ;
//
//        // 카메라가 타겟을 바라보도록 회전 설정
//        myT->rotation.x = m_pitch;
//        myT->rotation.y = m_yaw;
//    }
//}



// 카메라가 그냥 플레이어를 따라다니는 스크립트
#include "CameraFollow.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Alice
{
    REGISTER_SCRIPT(CameraFollow);

    // 헬퍼 함수: 선형 보간 (a에서 b로 t만큼 이동)
    float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    void CameraFollow::MoveDirectly()
    {
        auto go = gameObject();

        if (!go.IsValid())
            return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid())
            return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT)  return;

        myT->position.x = tT->position.x + Get_m_offsetX();
        myT->position.y = tT->position.y + Get_m_offsetY();
        myT->position.z = tT->position.z + Get_m_offsetZ();

        // 타겟을 바라보게 회전(yaw/pitch) 맞추기
        const float dx = tT->position.x - myT->position.x;
        const float dy = tT->position.y - myT->position.y;
        const float dz = tT->position.z - myT->position.z;

        const float yaw = std::atan2(dx, dz);
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        const float pitch = -std::atan2(dy, distXZ);

        myT->rotation.x = pitch;
        myT->rotation.y = yaw;
    }

    void CameraFollow::MoveLerp(const float& lateDeltaTime)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid())
            return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT)
            return;

        // 1. 목표 위치 계산 (아직 대입하지 않음)
        float targetX = tT->position.x + Get_m_offsetX();
        float targetY = tT->position.y + Get_m_offsetY();
        float targetZ = tT->position.z + Get_m_offsetZ();

        // 2. 보간(Lerp) 적용
        // 공식: 현재위치 += (목표위치 - 현재위치) * 속도 * 시간
        // smoothSpeed가 높을수록 빠르게 달라붙고, 낮을수록 부드럽게(느리게) 따라옵니다.
        //float t = Get_m_smoothSpeed() * lateDeltaTime;
        //myT->position.x = Lerp(myT->position.x, targetX, t);
        //myT->position.y = Lerp(myT->position.y, targetY, t);
        //myT->position.z = Lerp(myT->position.z, targetZ, t);

        // 보간 안쓰고 그냥 따라붙게 하기
        float t = lateDeltaTime;
        myT->position.x = myT->position.x * lateDeltaTime;
        myT->position.y = myT->position.y * lateDeltaTime;
        myT->position.z = myT->position.z * lateDeltaTime;
    }

    void CameraFollow::MoveOrbit()
    {
        auto go = gameObject();
        auto* input = Input();
        if (!go.IsValid() || !input) return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid()) return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT) return;

        // --- 1. 회전 처리 (마우스 왼쪽 드래그) ---
        float yaw = Get_m_currentYaw();
        float pitch = Get_m_currentPitch();

        if (input->GetMouseButton(MouseCode::Left))
        {
            float sensitivity = Get_m_sensitivity();
            const float mouseFlip = Get_m_invertMouse() ? -1.0f : 1.0f;

            // 마우스 이동량만큼 회전
            yaw += mouseFlip * input->GetMouseDeltaX() * sensitivity;
            pitch += mouseFlip * input->GetMouseDeltaY() * sensitivity;

            // 상하 회전 제한 (-89 ~ 89도)
            pitch = std::clamp(pitch, -89.0f, 89.0f);

            Set_m_currentYaw(yaw);
            Set_m_currentPitch(pitch);
        }

        // --- 2. [줌인/아웃] 거리 조절 (마우스 휠 스크롤) ---
        float dist = Get_m_distance();

        // Input 클래스에 휠 스크롤 값을 가져오는 함수가 있어야 합니다.
        // (보통 휠 위로=+1.0, 아래로=-1.0 반환)
        float wheel = input->GetMouseScrollDelta();

        if (wheel != 0.0f)
        {
            // 휠 올림(+) -> 거리 감소(줌인)
            // 휠 내림(-) -> 거리 증가(줌아웃)
            dist -= wheel * Get_m_zoomSpeed();

            // 최소/최대 거리 제한 (벽 뚫기나 너무 멀어짐 방지)
            dist = std::clamp(dist, Get_m_minDistance(), Get_m_maxDistance());

            Set_m_distance(dist);
        }

        // --- 3. 위치 계산 (구면 좌표계: Distance 반영) ---
        // 타겟의 머리 높이(Pivot) 기준
        float pivotX = tT->position.x;
        float pivotY = tT->position.y + Get_m_heightOffset();
        float pivotZ = tT->position.z;

        // 각도를 라디안으로 변환
        float radYaw = yaw * (static_cast<float>(M_PI) / 180.0f);
        float radPitch = pitch * (static_cast<float>(M_PI) / 180.0f);

        // 구면 좌표계 공식에 dist(거리) 적용
        float hDist = dist * std::cos(radPitch); // 수평 거리
        float vDist = dist * std::sin(radPitch); // 수직 높이

        float offsetX = hDist * std::sin(radYaw);
        float offsetZ = hDist * std::cos(radYaw);
        float offsetY = vDist;

        // 최종 위치 적용 (Pivot - Offset)
        myT->position.x = pivotX - offsetX;
        myT->position.y = pivotY + offsetY;
        myT->position.z = pivotZ - offsetZ;

        // --- 4. 회전 계산 (LookAt: 항상 타겟 바라보기) ---
        const float dx = pivotX - myT->position.x;
        const float dy = pivotY - myT->position.y;
        const float dz = pivotZ - myT->position.z;

        const float lookYaw = std::atan2(dx, dz);
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        const float lookPitch = -std::atan2(dy, distXZ);

        myT->rotation.x = lookPitch;
        myT->rotation.y = lookYaw;
    }


    void CameraFollow::LateUpdate(float lateDeltaTime)
    {
        //MoveLerp(lateDeltaTime);
        //MoveDirectly();
        MoveOrbit();
    }
}


