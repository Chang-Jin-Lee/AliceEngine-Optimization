#include "EffectExample.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/Components/EffectComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include <cmath>

namespace Alice
{
    REGISTER_SCRIPT(EffectExample);

    void EffectExample::Start()
    {
        auto* t = transform();
        if (!t)
        {
            t = &AddComponent<TransformComponent>();
            t->SetPosition(0.0f, 1.0f, 0.0f);
        }

        // EffectComponent 추가
        auto* effect = GetComponent<EffectComponent>();
        if (!effect)
        {
            effect = &AddComponent<EffectComponent>();
            effect->color = DirectX::XMFLOAT3(1.0f, 0.5f, 0.0f); // 오렌지 색상
            effect->size = 1.0f;
            effect->enabled = true;
            effect->alpha = 0.8f;
        }

        m_baseSize = effect->size;
        m_timeSeconds = 0.0f;

        ALICE_LOG_INFO("[EffectExample] 이펙트 컴포넌트가 추가되었습니다.");
    }

    void EffectExample::Update(float deltaTime)
    {
        m_timeSeconds += deltaTime;

        auto* t = transform();
        auto* effect = GetComponent<EffectComponent>();

        if (!t || !effect) return;

        // Y축으로 회전
        t->rotation.y += Get_m_rotationSpeed() * deltaTime;

        // 크기 펄스 효과
        float size = m_baseSize * (1.0f + Get_m_pulseAmplitude() * std::sin(m_timeSeconds * Get_m_pulseSpeed()));
        effect->size = size;

        // 색상 변경 (시간에 따라 색상이 변하는 효과)
        float hue = m_timeSeconds * 0.5f;
        effect->color.x = 0.5f + 0.5f * std::sin(hue);
        effect->color.y = 0.5f + 0.5f * std::sin(hue + 2.094f); // 120도 오프셋
        effect->color.z = 0.5f + 0.5f * std::sin(hue + 4.189f); // 240도 오프셋
    }
}
