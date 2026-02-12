#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // ClearScene에서 일정 시간 대기 후 입력으로 TitleScene 복귀를 처리한다.
    class ClearSceneReturnInputScript : public IScript
    {
        ALICE_BODY(ClearSceneReturnInputScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, targetScenePath, "Assets/Scenes/MainGameLoopScene/TitleScene.scene");
        ALICE_PROPERTY(float, inputEnableDelaySec, 5.0f);
        ALICE_PROPERTY(bool, allowSpaceKey, true);
        ALICE_PROPERTY(bool, allowEnterKey, true);
        ALICE_PROPERTY(bool, allowMouseLeftClick, true);
        ALICE_PROPERTY(bool, allowGamepadAButton, true);
        ALICE_PROPERTY(int, gamepadPlayerIndex, 0);
        ALICE_PROPERTY(float, inputCooldownSec, 0.12f);

    private:
        bool ConsumeAdvanceInput() const;

        float m_elapsedSec = 0.0f;
        float m_cooldownRemaining = 0.0f;
        bool m_sceneRequested = false;
    };
}
