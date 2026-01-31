#include "ScytheTrailEffect.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/Components/EffectComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include <cmath>
#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(ScytheTrailEffect);

    void ScytheTrailEffect::Start()
    {
        auto* world = GetWorld();
        if (!world) return;

        auto* transform = this->transform();
        if (!transform)
        {
            transform = &AddComponent<TransformComponent>();
            // 시작점으로 초기 위치 설정
            transform->SetPosition(
                Get_m_startPoint().x,
                Get_m_startPoint().y,
                Get_m_startPoint().z
            );
        }

        // 메인 이펙트 추가
        auto* effect = GetComponent<EffectComponent>();
        if (!effect)
        {
            effect = &AddComponent<EffectComponent>();
            effect->color = Get_m_effectColor();
            effect->size = Get_m_effectSize();
            effect->enabled = true;
            effect->alpha = 0.9f;
        }

        m_currentSplineT = 0.0f;
        m_lastTrailCreationTime = 0.0f;
        m_totalTime = 0.0f;
        m_isSlashing = false;
        m_isCompleted = false;
        m_trailPoints.clear();

        ALICE_LOG_INFO("[ScytheTrailEffect] Catmull-Rom 스플라인 검기 효과 초기화 완료");
    }

    void ScytheTrailEffect::Update(float deltaTime)
    {
        auto* world = GetWorld();
        if (!world) return;

        // 이미 완료된 경우 업데이트 중지
        if (m_isCompleted)
        {
            // 궤적 포인트 업데이트 및 정리만 수행
            UpdateTrailPoints(m_totalTime);
            return;
        }

        m_totalTime += deltaTime;

        // 스플라인 위치 업데이트
        m_currentSplineT += Get_m_slashSpeed() * deltaTime;
        
        if (m_currentSplineT >= 1.0f)
        {
            // 끝점에 도달하면 완료 표시하고 더 이상 업데이트하지 않음
            m_currentSplineT = 1.0f;
            m_isCompleted = true;
            
            // 메인 이펙트 비활성화
            auto* effect = GetComponent<EffectComponent>();
            if (effect)
            {
                effect->enabled = false;
            }
        }

        // 메인 이펙트 위치 업데이트
        auto* transform = this->transform();
        if (transform && !m_isCompleted)
        {
            DirectX::XMFLOAT3 pos = CalculateCatmullRomSpline(m_currentSplineT);
            transform->SetPosition(pos.x, pos.y, pos.z);
            
            // 이동 방향 계산 (다음 위치를 향하도록)
            float nextT = m_currentSplineT + 0.01f;
            if (nextT > 1.0f) nextT = 1.0f;
            DirectX::XMFLOAT3 nextPos = CalculateCatmullRomSpline(nextT);
            
            float dx = nextPos.x - pos.x;
            float dz = nextPos.z - pos.z;
            float yaw = std::atan2(dx, dz);
            transform->rotation.y = yaw;
        }

        // 궤적 포인트 생성 (일정 간격마다, 완료 전까지)
        if (!m_isCompleted && m_totalTime - m_lastTrailCreationTime >= 0.02f)
        {
            CreateTrailPoint(m_currentSplineT);
            m_lastTrailCreationTime = m_totalTime;
        }

        // 궤적 포인트 업데이트 및 정리
        UpdateTrailPoints(m_totalTime);
    }

    void ScytheTrailEffect::OnDestroy()
    {
        CleanupTrailPoints();
    }

    DirectX::XMFLOAT3 ScytheTrailEffect::CalculateCatmullRomSpline(float t)
    {
        // DirectXMath XMVectorCatmullRom 사용
        // 4개의 제어점: p0, p1, p2, p3
        // t는 0~1 사이 값 (p1에서 p2까지 보간)
        
        using namespace DirectX;
        
        XMFLOAT3 p0 = Get_m_startPoint();
        XMFLOAT3 p1 = Get_m_controlPoint1();
        XMFLOAT3 p2 = Get_m_controlPoint2();
        XMFLOAT3 p3 = Get_m_endPoint();

        // XMVectorCatmullRom: s가 0일 때 p1, 1일 때 p2를 반환
        XMVECTOR v0 = XMLoadFloat3(&p0);
        XMVECTOR v1 = XMLoadFloat3(&p1);
        XMVECTOR v2 = XMLoadFloat3(&p2);
        XMVECTOR v3 = XMLoadFloat3(&p3);
        
        XMVECTOR result = XMVectorCatmullRom(v0, v1, v2, v3, t);
        
        XMFLOAT3 resultFloat3;
        XMStoreFloat3(&resultFloat3, result);
        return resultFloat3;
    }

    void ScytheTrailEffect::CreateTrailPoint(float splineT)
    {
        auto* world = GetWorld();
        if (!world) return;

        // 스플라인 상의 위치 계산
        DirectX::XMFLOAT3 pos = CalculateCatmullRomSpline(splineT);
        
        // 새 엔티티 생성
        EntityId trailEntity = world->CreateEntity();
        
        // Transform 추가
        auto& trailTransform = world->AddComponent<TransformComponent>(trailEntity);
        trailTransform.SetPosition(pos.x, pos.y, pos.z);
        
        // 이동 방향 계산
        float nextT = splineT + 0.01f;
        if (nextT > 1.0f) nextT = 1.0f;
        DirectX::XMFLOAT3 nextPos = CalculateCatmullRomSpline(nextT);
        
        float dx = nextPos.x - pos.x;
        float dz = nextPos.z - pos.z;
        float yaw = std::atan2(dx, dz);
        trailTransform.rotation.y = yaw;

        // EffectComponent 추가
        auto& trailEffect = world->AddComponent<EffectComponent>(trailEntity);
        trailEffect.color = Get_m_effectColor();
        trailEffect.size = Get_m_effectSize() * 0.8f; // 메인보다 약간 작게
        trailEffect.enabled = true;
        trailEffect.alpha = 0.7f;

        // PhysicsTest 스크립트 추가
        world->AddScript(trailEntity, "PhysicsTest");

        // 궤적 포인트 리스트에 추가
        TrailPoint point;
        point.entityId = trailEntity;
        point.creationTime = m_totalTime;
        point.splineT = splineT;
        m_trailPoints.push_back(point);
    }

    void ScytheTrailEffect::UpdateTrailPoints(float currentTime)
    {
        auto* world = GetWorld();
        if (!world) return;

        float fadeTime = Get_m_trailLength();
        
        // 궤적 포인트들을 시간에 따라 페이드 아웃
        for (auto it = m_trailPoints.begin(); it != m_trailPoints.end();)
        {
            float age = currentTime - it->creationTime;
            
            if (age >= fadeTime)
            {
                // 시간 초과된 포인트 제거
                world->DestroyEntity(it->entityId);
                it = m_trailPoints.erase(it);
            }
            else
            {
                // 알파 값 조정 (시간이 지날수록 투명해짐)
                auto* effect = world->GetComponent<EffectComponent>(it->entityId);
                if (effect)
                {
                    float alphaRatio = 1.0f - (age / fadeTime);
                    effect->alpha = 0.7f * alphaRatio;
                    effect->size = Get_m_effectSize() * 0.8f * (0.5f + 0.5f * alphaRatio);
                }
                ++it;
            }
        }
    }

    void ScytheTrailEffect::CleanupTrailPoints()
    {
        auto* world = GetWorld();
        if (!world) return;

        for (const auto& point : m_trailPoints)
        {
            world->DestroyEntity(point.entityId);
        }
        m_trailPoints.clear();
    }
}
