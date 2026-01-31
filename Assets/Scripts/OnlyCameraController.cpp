#include "OnlyCameraController.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Input/Input.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include <DirectXMath.h>
#include <algorithm> // std::clamp

using namespace DirectX;

namespace Alice
{
	REGISTER_SCRIPT(OnlyCameraController);

	void OnlyCameraController::Awake()
	{
		// 시작 시 현재 각도를 가져와서 초기화 (카메라가 튀는 것 방지)
		if (auto* tr = gameObject().GetComponent<TransformComponent>())
		{
			m_pitch = tr->rotation.x;
			m_yaw = tr->rotation.y;
		}
	}

	void OnlyCameraController::Update(float deltaTime)
	{
		auto go = gameObject();
		auto* input = Input();
		if (!go.IsValid() || !input) return;

		auto* tr = go.GetComponent<TransformComponent>();
		if (!tr) return;

		// 1. 마우스 회전 (우클릭 상태일 때만)
		if (input->GetMouseButton(MouseCode::Right))
		{
			float dx = input->GetMouseDeltaX();
			float dy = input->GetMouseDeltaY();

			m_yaw += dx * m_mouseSens;
			m_pitch += dy * m_mouseSens;

			// 고개 너무 젖혀짐 방지 (-89도 ~ 89도)
			m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
		}

		// 회전 적용 (오일러 각)
		tr->rotation = { m_pitch, m_yaw, 0.0f };

		// 2. 키보드 이동 (WASD + QE)
		float x = 0.0f, y = 0.0f, z = 0.0f;

		if (input->GetKey(KeyCode::W)) z += 1.0f; // 앞
		if (input->GetKey(KeyCode::S)) z -= 1.0f; // 뒤
		if (input->GetKey(KeyCode::D)) x += 1.0f; // 우
		if (input->GetKey(KeyCode::A)) x -= 1.0f; // 좌
		if (input->GetKey(KeyCode::Q)) y += 1.0f; // 위 (World Up)
		if (input->GetKey(KeyCode::E)) y -= 1.0f; // 아래

		// 입력이 없으면 리턴 (연산 절약)
		if (x == 0.0f && y == 0.0f && z == 0.0f) return;

		// 3. 이동 벡터 계산 (현재 카메라가 바라보는 방향 기준)
		float yawRad = XMConvertToRadians(m_yaw);
		float pitchRad = XMConvertToRadians(m_pitch);

		// 회전 행렬 생성
		XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(pitchRad, yawRad, 0.0f);

		// 로컬 방향 벡터 추출
		XMVECTOR right = rotationMatrix.r[0]; // X축
		XMVECTOR forward = rotationMatrix.r[2]; // Z축
		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // 글로벌 Y축 (Q/E 이동용)

		// 최종 이동 벡터 합성
		XMVECTOR moveDir = (right * x) + (up * y) + (forward * z);
		moveDir = XMVector3Normalize(moveDir); // 대각선 이동 시 속도 일정하게

		// 4. 위치 적용
		XMFLOAT3 move;
		XMStoreFloat3(&move, moveDir * m_moveSpeed * deltaTime);

		tr->position.x += move.x;
		tr->position.y += move.y;
		tr->position.z += move.z;
	}
}