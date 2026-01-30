#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// AnimBlueprint 파라미터를 입력으로 제어하는 예시
    class AnimBlueprintExample : public IScript
    {
        ALICE_BODY(AnimBlueprintExample);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    public:
        // AnimBlueprint JSON 경로 (논리 경로)
        ALICE_PROPERTY(std::string, blueprintPath,
            std::string("Assets/AnimBlueprints/Hero.animblueprint.json"));

        // 파라미터 이름 (AnimBlueprint에서 만든 이름과 동일해야 함)
        ALICE_PROPERTY(std::string, speedParam,   std::string("Speed"));
        ALICE_PROPERTY(std::string, moveParam,    std::string("IsMoving"));
        ALICE_PROPERTY(std::string, triggerParam, std::string("Fire"));

        // 이동 속도 (입력에 따라 Speed 파라미터로 전달)
        ALICE_PROPERTY(float, moveSpeed, 1.0f);
    };
}

