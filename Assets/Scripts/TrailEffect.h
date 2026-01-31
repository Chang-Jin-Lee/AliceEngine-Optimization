#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include <DirectXMath.h>
#include <vector>

namespace Alice
{
    class TrailEffectComponent;
    /// 트레일/리본 기반 검기 이펙트 스크립트
    /// - 무기의 루트/팁 지점을 시간에 따라 샘플링
    /// - Triangle Strip으로 리본 메쉬 생성
    /// - Age 기반 페이드 아웃 효과
    class TrailEffect : public IScript
    {
        ALICE_BODY(TrailEffect);

    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        // Inspector 속성
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_rootPoint, DirectX::XMFLOAT3(-1.0f, 1.5f, 0.0f));      // 무기 루트 위치
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_tipPoint, DirectX::XMFLOAT3(1.0f, 1.5f, 0.0f));       // 무기 팁 위치
        ALICE_PROPERTY(float, m_sampleInterval, 0.016f);    // 샘플링 간격 (초, 기본 60fps)
        ALICE_PROPERTY(int, m_maxSamples, 60);              // 최대 샘플 수 (링 버퍼 크기)
        ALICE_PROPERTY(float, m_fadeDuration, 1.0f);        // 페이드 아웃 시간 (초)
        ALICE_PROPERTY(bool, m_autoMove, true);             // 자동 이동 시뮬레이션 (테스트용)
        ALICE_PROPERTY(float, m_moveSpeed, 2.0f);           // 자동 이동 속도

    private:
        void AddTrailSample(TrailEffectComponent* effect, const DirectX::XMFLOAT3& rootPos, const DirectX::XMFLOAT3& tipPos, float currentTime);
        void UpdateTrailLength(TrailEffectComponent* effect);

        float m_currentTime;          // 현재 시간 (초)
        float m_lastSampleTime;       // 마지막 샘플링 시간
        bool m_isActive;              // 활성화 여부
        bool m_hasStarted;            // 시작 여부
        DirectX::XMFLOAT3 m_prevPosition;  // 이전 프레임의 위치 (이동량 계산용)
    };
}
