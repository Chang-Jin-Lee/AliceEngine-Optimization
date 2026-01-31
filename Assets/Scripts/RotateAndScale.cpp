#include "RotateAndScale.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include <cmath> // std::sin
#include <Runtime/Foundation/Logger.h>

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(RotateAndScale);

    void RotateAndScale::Start()
    {
        // Unity 의 this.transform 과 동일하게, 우선 현재 Transform 을 가져옵니다.
        auto* t = transform();

        // Transform 이 없으면 현재 게임 오브젝트에 하나 추가합니다.
        if (!t)
        {
            t = &AddComponent<TransformComponent>();
        }

        m_baseScale = (t->scale.x > 0.0f) ? t->scale.x : 1.0f;
        m_timeSeconds = 0.0f;
    }

    void RotateAndScale::Update(float deltaTime)
    {
        // 경과 시간 누적
        m_timeSeconds += deltaTime;

        if (auto* t = transform())
        {
            // (1) Y 축으로 초당 약 1라디안씩 회전
            t->rotation.y += Get_m_spinSpeed() * deltaTime;

            // (2) 시간에 따라 스케일이 0.75 ~ 1.25 배 사이에서 천천히 진동
            float s = m_baseScale * (1.0f + Get_m_pulseAmplitude() * std::sin(m_timeSeconds * Get_m_pulseSpeed()));
            t->scale.x = s;
            t->scale.y = s;
            t->scale.z = s;
        }
    }
}
