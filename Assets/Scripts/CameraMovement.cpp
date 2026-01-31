#pragma once
#include "CameraMovement.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Input/Input.h" // Input 클래스가 있다고 가정
#include "Runtime/Input/InputTypes.h" // MouseCode 사용을 위해
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Alice
{

    //// 엔진에 스크립트 등록
    REGISTER_SCRIPT(CameraMovement);

    void CameraMovement::Start() {  }

    void CameraMovement::Update(float DeltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        auto go = gameObject();
        auto* t = go.GetComponent<TransformComponent>();
        if (!t)
            return;     

        // --- 1. 입력 수집 (방향키) ---
        float inputX = 0.0f; // Left, Right
        float inputZ = 0.0f; // Up, Down

        // 방향키 매핑
        if (input->GetKey(KeyCode::Up))
            inputZ += 1.0f;
        if (input->GetKey(KeyCode::Down))
            inputZ -= 1.0f;
        if (input->GetKey(KeyCode::Right))
            inputX += 1.0f;
        if (input->GetKey(KeyCode::Left))
            inputX -= 1.0f;

        // --- 마우스 왼쪽 클릭 감지 및 카메라 위로 이동 ---
        if (input->GetMouseButtonDown(MouseCode::Left))
        {
            // 카메라 position을 위로 이동 (Y축 증가)
            t->position.y += 1.0f;
        }

        if (input->GetMouseButtonDown(MouseCode::Right))
        {
			t->position.y -= 1.0f;
        }

		ALICE_LOG_INFO("CameraMovement Input: X={0}, Z={1}", inputX, inputZ);

        // 입력이 없으면 연산 중단
        if (inputX == 0.0f && inputZ == 0.0f)
            return;

        // --- 2. 카메라의 현재 Y축 회전값(Yaw) 가져오기 ---
        // 카메라 자신이 바라보는 방향으로 움직여야 하므로 자신의 rotation을 사용합니다.
        float camYawRad = t->rotation.y * (static_cast<float>(M_PI) / 180.0f);

        // --- 3. 로컬 입력 벡터를 월드 좌표로 변환 ---
        // 캐릭터 이동 코드와 동일한 회전 행렬 공식 사용
        // Forward(Z)와 Right(X)를 현재 각도(Yaw)만큼 회전
        float sinY = std::sin(camYawRad);
        float cosY = std::cos(camYawRad);

        // 로컬(inputX, inputZ) -> 월드(worldX, worldZ) 변환
        float worldX = inputX * cosY + inputZ * sinY;
        float worldZ = -inputX * sinY + inputZ * cosY;

        // --- 4. 정규화 (대각선 이동 시 속도 일정하게) ---
        float len = std::sqrt(worldX * worldX + worldZ * worldZ);
        if (len > 0.0001f)
        {
            worldX /= len;
            worldZ /= len;
        }

        // --- 5. 위치 적용 ---
        // 카메라는 보통 물리(중력)를 받지 않고 둥둥 떠다니므로 Transform을 직접 수정
        t->position.x += worldX * m_moveSpeed * DeltaTime;
        t->position.z += worldZ * m_moveSpeed * DeltaTime;

        // (선택 사항) 상승/하강 기능 추가 (예: E/Q 키)
        /*
        if (input->GetKey(KeyCode::E)) t->position.y += m_moveSpeed * DeltaTime;
        if (input->GetKey(KeyCode::Q)) t->position.y -= m_moveSpeed * DeltaTime;
        */
    }

}