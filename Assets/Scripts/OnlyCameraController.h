#pragma once
#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
	// 디버그/편집용 초간단 자유 시점 카메라 (Fly Camera)
	class OnlyCameraController : public IScript
	{
		ALICE_BODY(OnlyCameraController);

	public:
		void Awake() override;
		void Update(float deltaTime) override;

		// 에디터에서 조절 가능한 속성
		ALICE_PROPERTY(float, m_moveSpeed, 10.0f);     // 이동 속도
		ALICE_PROPERTY(float, m_mouseSens, 0.001f);      // 마우스 감도 (회전)
		ALICE_PROPERTY(float, m_smoothTime, 10.0f);    // 회전 보간(부드러움)

	private:
		float m_yaw = 0.0f;     // 가로 회전 (Y축)
		float m_pitch = 0.0f;   // 세로 회전 (X축)
	};
}