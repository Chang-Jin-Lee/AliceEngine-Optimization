#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include <DirectXMath.h>
#include <vector>

namespace Alice
{
    /// Catmull-Rom 스플라인을 사용한 검기 이펙트 스크립트
    /// - 시작점, 끝점, 중간 control point들을 Inspector에서 설정
    /// - 쉐이더를 통해 스플라인 기반 검기 렌더링
    class ScytheTrailEffect : public IScript
    {
        ALICE_BODY(ScytheTrailEffect);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        // 스플라인 제어점들 (최소 4개 필요: 시작, control1, control2, 끝)
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_startPoint, DirectX::XMFLOAT3(-2.0f, 1.5f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_controlPoint1, DirectX::XMFLOAT3(-1.0f, 1.5f, -0.5f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_controlPoint2, DirectX::XMFLOAT3(1.0f, 1.5f, 0.5f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_endPoint, DirectX::XMFLOAT3(2.0f, 1.5f, 0.0f));
        
        // 추가 제어점들 (선택적)
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_controlPoint3, DirectX::XMFLOAT3(0.0f, 1.5f, 0.0f));
        
        // 애니메이션 속도
        ALICE_PROPERTY(float, m_slashSpeed, 2.0f);        // 검기 이동 속도
        ALICE_PROPERTY(float, m_trailLength, 0.5f);      // 궤적이 남아있는 시간 (초)
        ALICE_PROPERTY(int, m_segmentCount, 32);          // 스플라인 세그먼트 수 (더 많으면 더 부드러움)
        
        // 이펙트 속성
        ALICE_PROPERTY(float, m_effectSize, 0.8f);        // 이펙트 크기
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_effectColor, DirectX::XMFLOAT3(0.8f, 0.2f, 0.9f)); // 이펙트 색상

    private:
        struct TrailPoint
        {
            EntityId entityId;
            float creationTime;
            float splineT; // 스플라인 상의 t 값 (0~1)
        };

        // DirectXMath XMVectorCatmullRom을 사용한 스플라인 계산
        DirectX::XMFLOAT3 CalculateCatmullRomSpline(float t);
        
        void CreateTrailPoint(float splineT);
        void UpdateTrailPoints(float currentTime);
        void CleanupTrailPoints();

        std::vector<TrailPoint> m_trailPoints;
        float m_currentSplineT; // 현재 스플라인 위치 (0~1)
        float m_lastTrailCreationTime;
        float m_totalTime;
        bool m_isSlashing; // 검기를 휘두르는 중인지
        bool m_isCompleted; // 검기 효과가 완료되었는지
    };
}
