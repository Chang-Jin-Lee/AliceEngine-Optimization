#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
	/// SoundBoxComponent를 초기화하는 예시
	class SoundBoxSetupExample : public IScript
	{
		ALICE_BODY(SoundBoxSetupExample);

	public:
		void Start() override;
		void Update(float dt) override;

	public:
		// 사운드 박스용 기본 파라미터를 에디터에서 바로 조절
		ALICE_PROPERTY(std::string, soundPath, std::string("Resource/Sound/CaliforniaGirls.wav"));
		ALICE_PROPERTY(bool, loop, true);
		ALICE_PROPERTY(float, minDistance, 1.0f);
		ALICE_PROPERTY(float, maxDistance, 30.0f);

		// 박스 크기/영역 설정
		ALICE_PROPERTY(DirectX::XMFLOAT3, boundsMin, DirectX::XMFLOAT3(-5, -2, -5));
		ALICE_PROPERTY(DirectX::XMFLOAT3, boundsMax, DirectX::XMFLOAT3(5, 2, 5));

		// 에디터에서 체크하면 디버그 박스가 보이는 용도로 만들었습니다.
		// 현재는 보이지 않습니다. 
		ALICE_PROPERTY(bool, debugDraw, false);

		// 반응할 타겟 오브젝트 이름 (비워두면 기본값인 카메라/리스너에 반응)
		ALICE_PROPERTY(std::string, targetName, std::string(""));
	};
}