#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // 예제용 스크립트: 키보드 입력으로 MaterialComponent를 런타임에 변경합니다.
    class MaterialAlphaExample : public IScript
    {
        ALICE_BODY(MaterialAlphaExample);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // 입력 시 변경되는 스텝 크기
        ALICE_PROPERTY(float, m_alphaStep, 0.1f);
        ALICE_PROPERTY(float, m_valueStep, 0.05f);
        ALICE_PROPERTY(bool, m_autoTransparent, true);
    };
}
