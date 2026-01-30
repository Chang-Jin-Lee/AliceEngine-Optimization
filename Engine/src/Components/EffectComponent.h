#pragma once

#include <DirectXMath.h>

namespace Alice
{
	/// 이펙트 컴포넌트 (반달 모양 이펙트)
	struct EffectComponent
	{
		DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 0.0f };  // 이펙트 색상 (기본값: 노란색)
		float size{ 1.0f };                          // 이펙트 크기 (반지름)
		bool enabled{ true };                        // 이펙트 활성화 여부
		float alpha{ 1.0f };                         // 알파 값 (투명도)
	};
}
