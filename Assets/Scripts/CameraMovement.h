#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
	// 방향키로 카메라를 이동시키는 스크립트
	class CameraMovement : public IScript
	{
		ALICE_BODY(CameraMovement);
	public:
		// 엔진에서 식별할 스크립트 이름
		const char* GetName() const override { return "CameraMovement"; }

		// 초기화 및 매 프레임 업데이트
		void Start() override;
		void Update(float deltaTime) override;

	private:
		// 이동 속도 (기본값 설정)
		ALICE_PROPERTY(float, m_moveSpeed, 10.0f);
	};
}