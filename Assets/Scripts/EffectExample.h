#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 이펙트 컴포넌트 사용 예제 스크립트
    /// - EffectComponent를 오브젝트에 추가하고 애니메이션 효과를 적용합니다.
    class EffectExample : public IScript
    {
        ALICE_BODY(EffectExample);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(float, m_rotationSpeed, 1.0f);
        ALICE_PROPERTY(float, m_pulseSpeed, 2.0f);
        ALICE_PROPERTY(float, m_pulseAmplitude, 0.3f);

    private:
        float m_timeSeconds = 0.0f;
        float m_baseSize = 1.0f;
    };
}
