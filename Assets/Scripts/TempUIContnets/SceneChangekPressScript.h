#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    // K 키를 누르면 씬을 전환하는 스크립트
    class SceneChangekPressScript : public IScript
    {
        ALICE_BODY(SceneChangekPressScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // 이동할 씬 경로
        ALICE_PROPERTY(std::string, targetScenePath, "Assets/Scenes/UI/DemoiScene.scene");
        
        // 키 입력 처리용 (에디터에서 설정 가능, 기본값은 K)
        ALICE_PROPERTY(int, changeKey, static_cast<int>(KeyCode::K));
    };
}
