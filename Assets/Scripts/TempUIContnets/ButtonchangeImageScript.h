#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class ButtonchangeImageScript : public IScript
    {
        ALICE_BODY(ButtonchangeImageScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;



    };
}
