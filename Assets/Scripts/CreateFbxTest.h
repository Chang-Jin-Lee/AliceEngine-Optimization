#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class CreateFbxTest : public IScript
    {
        ALICE_BODY(CreateFbxTest);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 예시 (에디터에서 수정 가능) ---
        ALICE_PROPERTY(float, m_exampleValue, 1.0f);

        // --- 함수 리플렉션 예시 ---
        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

        int a = 1;
        std::vector<int> entityList;
    };
}
