#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// F1/F2로 씬 전환 예시
    /// - F1: 코드 씬 "SampleScene"
    /// - F2: .scene 파일 "Assets/Scens/fbxScene.scene" (프로젝트에 존재하는 경로로 바꿔도 됨)
    class SceneSwitchExample : public IScript
    {
        ALICE_BODY(SceneSwitchExample);
    public:
        const char* GetName() const override { return "SceneSwitchExample"; }
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, targetSceneA, "");
        ALICE_PROPERTY(std::string, targetSceneB, "");
    };
}






