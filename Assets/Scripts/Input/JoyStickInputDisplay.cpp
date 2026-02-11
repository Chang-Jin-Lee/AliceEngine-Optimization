#include "JoyStickInputDisplay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    REGISTER_SCRIPT(JoyStickInputDisplay);

    namespace
    {
        constexpr std::array<GamepadButton, static_cast<std::size_t>(GamepadButton::Count)> kButtonOrder = {
            GamepadButton::A,
            GamepadButton::B,
            GamepadButton::X,
            GamepadButton::Y,
            GamepadButton::LeftShoulder,
            GamepadButton::RightShoulder,
            GamepadButton::LeftTrigger,
            GamepadButton::RightTrigger,
            GamepadButton::Back,
            GamepadButton::Start,
            GamepadButton::LeftThumb,
            GamepadButton::RightThumb,
            GamepadButton::DPadUp,
            GamepadButton::DPadDown,
            GamepadButton::DPadLeft,
            GamepadButton::DPadRight,
        };
    }

    void JoyStickInputDisplay::Start()
    {
        World* world = GetWorld();
        if (world)
            m_targetText = world->GetComponent<UITextComponent>(GetOwnerId());

        if (m_targetText)
            m_targetText->text = Get_m_waitingText();

        m_wasConnected = false;
        m_lastEventText.clear();
        m_eventRemainSec = 0.0f;
    }

    void JoyStickInputDisplay::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input || !m_targetText)
            return;

        const int playerIndex = std::clamp(Get_m_playerIndex(), 0, 3);
        m_eventRemainSec = std::max(0.0f, m_eventRemainSec - std::max(0.0f, deltaTime));

        if (input->GetKeyDown(KeyCode::Alpha1))
        {
            input->PlayGamepadVibration(
                playerIndex,
                Get_m_vibrationStrength(),
                0.0f,
                Get_m_vibrationDurationSec(),
                GamepadVibrationBlend::Override);
            PushEventText("1번: 왼쪽 모터 진동");
        }

        if (input->GetKeyDown(KeyCode::Alpha2))
        {
            input->PlayGamepadVibration(
                playerIndex,
                0.0f,
                Get_m_vibrationStrength(),
                Get_m_vibrationDurationSec(),
                GamepadVibrationBlend::Override);
            PushEventText("2번: 오른쪽 모터 진동");
        }

        if (input->GetKeyDown(KeyCode::Alpha3))
        {
            input->PlayGamepadVibration(
                playerIndex,
                Get_m_vibrationStrength(),
                Get_m_vibrationStrength(),
                Get_m_vibrationDurationSec(),
                GamepadVibrationBlend::Override);
            PushEventText("3번: 양쪽 모터 진동");
        }

        const bool connected = input->GetGamepadConnected(playerIndex);
        if (!connected)
        {
            m_targetText->text = Get_m_waitingText() + "\n(키보드 1:좌진동 / 2:우진동 / 3:양쪽진동)";
            m_wasConnected = false;
            return;
        }

        if (!m_wasConnected)
        {
            m_targetText->text = Get_m_idleText();
            m_wasConnected = true;
        }

        for (const GamepadButton button : kButtonOrder)
        {
            if (!input->GetGamepadButtonDown(button, playerIndex))
                continue;

            PushEventText(std::string(ToButtonName(button)) + " 키 눌렸음");
            break;
        }

        const float leftX = input->GetGamepadLeftStickX(playerIndex);
        const float leftY = input->GetGamepadLeftStickY(playerIndex);
        const float rightX = input->GetGamepadRightStickX(playerIndex);
        const float rightY = input->GetGamepadRightStickY(playerIndex);
        const float leftTrigger = input->GetGamepadLeftTrigger(playerIndex);
        const float rightTrigger = input->GetGamepadRightTrigger(playerIndex);

        const float threshold = std::clamp(Get_m_stickActivationThreshold(), 0.0f, 0.99f);

        std::ostringstream oss;
        oss << "[Pad " << playerIndex << "] Connected\n";
        oss << "1: Left Vibration / 2: Right Vibration / 3: Both\n";
        oss << BuildStickLine("LStick", leftX, leftY, threshold) << "\n";
        oss << BuildStickLine("RStick", rightX, rightY, threshold) << "\n";
        oss << "LT=" << ToFixed2(leftTrigger) << ", RT=" << ToFixed2(rightTrigger) << "\n";
        if (m_eventRemainSec > 0.0f && !m_lastEventText.empty())
            oss << m_lastEventText;
        else
            oss << Get_m_idleText();

        m_targetText->text = oss.str();
    }

    const char* JoyStickInputDisplay::ToButtonName(GamepadButton button)
    {
        switch (button)
        {
        case GamepadButton::DPadUp: return "DPad Up";
        case GamepadButton::DPadDown: return "DPad Down";
        case GamepadButton::DPadLeft: return "DPad Left";
        case GamepadButton::DPadRight: return "DPad Right";
        case GamepadButton::Start: return "Start";
        case GamepadButton::Back: return "Back";
        case GamepadButton::LeftThumb: return "Left Stick";
        case GamepadButton::RightThumb: return "Right Stick";
        case GamepadButton::LeftShoulder: return "LB";
        case GamepadButton::RightShoulder: return "RB";
        case GamepadButton::A: return "A";
        case GamepadButton::B: return "B";
        case GamepadButton::X: return "X";
        case GamepadButton::Y: return "Y";
        case GamepadButton::LeftTrigger: return "LT";
        case GamepadButton::RightTrigger: return "RT";
        default: return "Unknown";
        }
    }

    std::string JoyStickInputDisplay::BuildStickLine(const char* stickName, float x, float y, float threshold)
    {
        const float magnitude = std::sqrt(x * x + y * y);
        const std::string dir = BuildDirectionString(x, y, threshold);

        std::ostringstream oss;
        oss << stickName << " " << dir
            << " (x=" << ToFixed2(x)
            << ", y=" << ToFixed2(y)
            << ", mag=" << ToFixed2(magnitude) << ")";
        return oss.str();
    }

    std::string JoyStickInputDisplay::BuildDirectionString(float x, float y, float threshold)
    {
        const bool up = y > threshold;
        const bool down = y < -threshold;
        const bool right = x > threshold;
        const bool left = x < -threshold;

        std::string dir;
        if (up) dir = "Up";
        else if (down) dir = "Down";

        if (right)
        {
            if (!dir.empty()) dir += " ";
            dir += "Right";
        }
        else if (left)
        {
            if (!dir.empty()) dir += " ";
            dir += "Left";
        }

        if (dir.empty())
            dir = "Idle";
        return dir;
    }

    std::string JoyStickInputDisplay::ToFixed2(float value)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    }

    void JoyStickInputDisplay::PushEventText(const std::string& message)
    {
        m_lastEventText = message;
        m_eventRemainSec = std::max(0.05f, Get_m_eventHoldSec());
    }
}
