#pragma once

#include <DirectXMath.h>
#include <vector>

namespace Alice
{
	/// 트레일 샘플 포인트 (무기의 루트/팁 위치 쌍)
	struct TrailSample
	{
		DirectX::XMFLOAT3 rootPos;    // 루트 위치 (무기 날 시작점)
		DirectX::XMFLOAT3 tipPos;     // 팁 위치 (무기 날 끝점)
		
		float birthTime;              // 생성 시간 (페이드 계산용)
		float length;                 // 이전 샘플까지의 누적 길이 (UV 계산용)
	};

	/// 검기 이펙트 컴포넌트 (트레일/리본 이펙트)
	struct TrailEffectComponent
	{
		DirectX::XMFLOAT3 color{ 0.8f, 0.2f, 0.9f };  // 이펙트 색상 (기본값: 보라색)
		float alpha{ 1.0f };                          // 알파 값 (전체 이펙트 알파)
		bool enabled{ true };                         // 이펙트 활성화 여부
		
		// 트레일 샘플 포인트들 (링 버퍼)
		std::vector<TrailSample> trailSamples;
		
		// 트레일 설정
		int maxSamples{ 60 };                        // 최대 샘플 수 (링 버퍼 크기)
		float sampleInterval{ 0.016f };              // 샘플링 간격 (초, 기본 60fps)
		float fadeDuration{ 1.0f };                  // 페이드 아웃 시간 (초)
		float totalLength{ 0.0f };                   // 전체 트레일 길이 (UV 계산용)
		float currentTime{ 0.0f };                   // 현재 시간 (스크립트의 m_currentTime과 동기화)
	};
}
