#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UITextComponent.h"

namespace Alice
{
    class JoyStickInputDisplay : public IScript
    {
        ALICE_BODY(JoyStickInputDisplay);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    private:
        static const char* ToButtonName(GamepadButton button);
        static std::string BuildStickLine(const char* stickName, float x, float y, float threshold);
        static std::string BuildDirectionString(float x, float y, float threshold);
        static std::string ToFixed2(float value);
        void PushEventText(const std::string& message);

        ALICE_PROPERTY(int, m_playerIndex, 0);
        ALICE_PROPERTY(std::string, m_waitingText, "조이스틱 연결 대기중");
        ALICE_PROPERTY(std::string, m_idleText, "버튼 입력 대기중");
        ALICE_PROPERTY(float, m_stickActivationThreshold, 0.15f);
        ALICE_PROPERTY(float, m_vibrationStrength, 1.0f);
        ALICE_PROPERTY(float, m_vibrationDurationSec, 0.30f);
        ALICE_PROPERTY(float, m_eventHoldSec, 1.50f);

        UITextComponent* m_targetText = nullptr;
        bool m_wasConnected = false;
        std::string m_lastEventText{};
        float m_eventRemainSec = 0.0f;
    };
}
