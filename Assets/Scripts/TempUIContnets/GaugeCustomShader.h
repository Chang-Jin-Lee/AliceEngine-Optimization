#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // 게이지용 커스텀 쉐이더를 등록하는 스크립트
    class GaugeCustomShader : public IScript
    {
        ALICE_BODY(GaugeCustomShader);

    public:
        void Start() override;
        void Update(float deltaTime) override;
    };
}
