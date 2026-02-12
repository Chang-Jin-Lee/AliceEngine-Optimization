#pragma once

#include <string>
#include <vector>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    // 클릭/스페이스/엔터 입력으로 이미지 가이드를 순차 표시하고 마지막에 씬 전환
    class GuideImageSequenceScript : public IScript
    {
        ALICE_BODY(GuideImageSequenceScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, imageWidgetName, "UI_GuideImage");
        ALICE_PROPERTY(std::string, imagePaths, "");
        ALICE_PROPERTY(int, startIndex, 0);
        ALICE_PROPERTY(std::string, targetScenePath, "Assets/Scenes/#00Combat0211.scene");

        ALICE_PROPERTY(bool, allowMouseLeftClick, true);
        ALICE_PROPERTY(bool, allowSpaceKey, true);
        ALICE_PROPERTY(bool, allowEnterKey, true);
        ALICE_PROPERTY(bool, allowEscapeKey, true);
        ALICE_PROPERTY(bool, allowGamepadAButton, true);
        ALICE_PROPERTY(int, gamepadPlayerIndex, 0);
        ALICE_PROPERTY(float, inputCooldownSec, 0.12f);

    private:
        void ResolveTarget();
        void ParsePaths();
        void ApplyCurrentImage();
        bool ConsumeAdvanceInput() const;
        void AdvanceOrSwitch();

        EntityId m_targetId = InvalidEntityId;
        UIImageComponent* m_targetImage = nullptr;
        std::vector<std::string> m_images;
        int m_currentIndex = 0;
        bool m_sceneRequested = false;
        float m_cooldownRemaining = 0.0f;
    };
}
