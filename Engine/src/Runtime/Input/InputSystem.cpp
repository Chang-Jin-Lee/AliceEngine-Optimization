#include "Runtime/Input/InputSystem.h"

#include <algorithm>
#include <cmath>

#include <windowsx.h>

#ifndef RID_INPUT
#define RID_INPUT 0x10000003
#endif

#pragma comment(lib, "xinput.lib")

using namespace DirectX;

namespace Alice
{
    namespace
    {
        constexpr float kMaxVibrationDurationSec = 120.0f;
        constexpr float kVibrationEpsilon = 0.0001f;
    }

    InputSystem::InputSystem() = default;

    InputSystem::~InputSystem()
    {
        StopAllGamepadVibrations();
    }

    bool InputSystem::Initialize(HWND hWnd)
    {
        m_keyboard = std::make_unique<Keyboard>();
        m_mouse = std::make_unique<Mouse>();

        m_mouse->SetWindow(hWnd);
        m_hWnd = hWnd;

        m_prevMousePos = POINT{ 0, 0 };
        m_mouseDelta = POINT{ 0, 0 };
        m_hasPrevMousePos = false;

        RAWINPUTDEVICE rid[1];
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage = 0x02;
        rid[0].dwFlags = 0;
        rid[0].hwndTarget = hWnd;

        if (RegisterRawInputDevices(rid, 1, sizeof(rid[0])))
        {
            m_useRawInput = true;
        }

        StopAllGamepadVibrations();

        return true;
    }

    void InputSystem::Update(const float& deltaTime)
    {
        m_mouseDelta.x = 0;
        m_mouseDelta.y = 0;
        m_mouseScrollDelta = 0.0f;

        if (m_keyboard && m_mouse)
        {
            m_mouseState = m_mouse->GetState();
            m_mouseTracker.Update(m_mouseState);

            m_keyboardState = m_keyboard->GetState();
            m_keyboardTracker.Update(m_keyboardState);

            if (m_isLocked && m_hWnd)
            {
                if (m_useRawInput)
                {
                    m_mouseDelta.x = m_rawInputDeltaX;
                    m_mouseDelta.y = m_rawInputDeltaY;

                    m_rawInputDeltaX = 0;
                    m_rawInputDeltaY = 0;
                }
                else
                {
                    POINT currentScreenPos;
                    ::GetCursorPos(&currentScreenPos);

                    m_mouseDelta.x = currentScreenPos.x - m_lockedPos.x;
                    m_mouseDelta.y = currentScreenPos.y - m_lockedPos.y;
                }

                ::SetCursorPos(m_lockedPos.x, m_lockedPos.y);
            }
            else
            {
                POINT current{ m_mouseState.x, m_mouseState.y };

                if (!m_hasPrevMousePos)
                {
                    m_prevMousePos = current;
                    m_hasPrevMousePos = true;
                }

                m_mouseDelta.x += current.x - m_prevMousePos.x;
                m_mouseDelta.y += current.y - m_prevMousePos.y;

                m_prevMousePos = current;
            }

            const int currentScrollWheelValue = m_mouseState.scrollWheelValue;
            m_mouseScrollDelta = static_cast<float>(currentScrollWheelValue - m_prevScrollWheelValue);
            m_prevScrollWheelValue = currentScrollWheelValue;
        }

        PollGamepads();
        UpdateGamepadVibrations(std::max(0.0f, deltaTime));
    }

    bool InputSystem::IsKeyDown(Keyboard::Keys key) const
    {
        return m_keyboardState.IsKeyDown(key);
    }

    bool InputSystem::IsKeyPressed(Keyboard::Keys key) const
    {
        return m_keyboardTracker.IsKeyPressed(key);
    }

    bool InputSystem::IsRightButtonDown() const
    {
        return m_mouseState.rightButton;
    }

    bool InputSystem::IsLeftButtonDown() const
    {
        return m_mouseState.leftButton;
    }

    bool InputSystem::IsMiddleButtonDown() const
    {
        return m_mouseState.middleButton;
    }

    bool InputSystem::IsMouseButtonDown(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseState.leftButton;
        case 1: return m_mouseState.rightButton;
        case 2: return m_mouseState.middleButton;
        default: return false;
        }
    }

    bool InputSystem::IsMouseButtonPressed(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        case 1: return m_mouseTracker.rightButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        case 2: return m_mouseTracker.middleButton == DirectX::Mouse::ButtonStateTracker::PRESSED;
        default: return false;
        }
    }

    bool InputSystem::IsMouseButtonReleased(int buttonIndex) const
    {
        switch (buttonIndex)
        {
        case 0: return m_mouseTracker.leftButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        case 1: return m_mouseTracker.rightButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        case 2: return m_mouseTracker.middleButton == DirectX::Mouse::ButtonStateTracker::RELEASED;
        default: return false;
        }
    }

    POINT InputSystem::GetMousePosition() const
    {
        return POINT{ m_mouseState.x, m_mouseState.y };
    }

    bool InputSystem::IsGamepadConnected(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return false;

        return m_currGamepads[static_cast<std::size_t>(playerIndex)].connected;
    }

    bool InputSystem::IsGamepadButtonDown(int playerIndex, GamepadButton button) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return false;

        return IsButtonDown(m_currGamepads[static_cast<std::size_t>(playerIndex)], button);
    }

    bool InputSystem::IsGamepadButtonPressed(int playerIndex, GamepadButton button) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return false;

        const auto idx = static_cast<std::size_t>(playerIndex);
        const bool now = IsButtonDown(m_currGamepads[idx], button);
        const bool prev = IsButtonDown(m_prevGamepads[idx], button);
        return now && !prev;
    }

    bool InputSystem::IsGamepadButtonReleased(int playerIndex, GamepadButton button) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return false;

        const auto idx = static_cast<std::size_t>(playerIndex);
        const bool now = IsButtonDown(m_currGamepads[idx], button);
        const bool prev = IsButtonDown(m_prevGamepads[idx], button);
        return !now && prev;
    }

    float InputSystem::GetGamepadLeftTrigger(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeTrigger(pad.state.Gamepad.bLeftTrigger);
    }

    float InputSystem::GetGamepadRightTrigger(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeTrigger(pad.state.Gamepad.bRightTrigger);
    }

    float InputSystem::GetGamepadLeftStickX(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeThumb(pad.state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    }

    float InputSystem::GetGamepadLeftStickY(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeThumb(pad.state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    }

    float InputSystem::GetGamepadRightStickX(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeThumb(pad.state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    }

    float InputSystem::GetGamepadRightStickY(int playerIndex) const
    {
        if (!IsValidGamepadIndex(playerIndex))
            return 0.0f;

        const auto& pad = m_currGamepads[static_cast<std::size_t>(playerIndex)];
        if (!pad.connected)
            return 0.0f;

        return NormalizeThumb(pad.state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    }

    void InputSystem::PlayGamepadVibration(int playerIndex,
                                           float leftMotor,
                                           float rightMotor,
                                           float durationSec,
                                           GamepadVibrationBlend blend)
    {
        if (!IsValidGamepadIndex(playerIndex))
            return;

        if (durationSec <= 0.0f)
            return;

        TimedVibration req{};
        req.playerIndex = playerIndex;
        req.leftMotor = Clamp01(leftMotor);
        req.rightMotor = Clamp01(rightMotor);
        req.durationSec = std::clamp(durationSec, 0.01f, kMaxVibrationDurationSec);
        req.elapsedSec = 0.0f;
        req.blend = blend;

        m_vibrationRequests.push_back(req);
    }

    void InputSystem::StopGamepadVibration(int playerIndex)
    {
        if (!IsValidGamepadIndex(playerIndex))
            return;

        const auto endIt = std::remove_if(
            m_vibrationRequests.begin(),
            m_vibrationRequests.end(),
            [playerIndex](const TimedVibration& req)
            {
                return req.playerIndex == playerIndex;
            });

        m_vibrationRequests.erase(endIt, m_vibrationRequests.end());
        ApplyGamepadVibrationNow(playerIndex, 0.0f, 0.0f);
    }

    void InputSystem::StopAllGamepadVibrations()
    {
        m_vibrationRequests.clear();

        for (int i = 0; i < MaxGamepadCount; ++i)
            ApplyGamepadVibrationNow(i, 0.0f, 0.0f);
    }

    void InputSystem::SetCursorVisible(bool visible)
    {
        if (visible)
        {
            while (::ShowCursor(TRUE) < 0);
        }
        else
        {
            while (::ShowCursor(FALSE) >= 0);
        }
    }

    void InputSystem::SetCursorLocked(bool locked)
    {
        m_isLocked = locked;

        if (locked && m_hWnd)
        {
            ::GetCursorPos(&m_lockedPos);

            RECT rect;
            ::GetClientRect(m_hWnd, &rect);

            POINT pt = { rect.left, rect.top };
            POINT pt2 = { rect.right, rect.bottom };
            ::ClientToScreen(m_hWnd, &pt);
            ::ClientToScreen(m_hWnd, &pt2);

            RECT clipRect = { pt.x, pt.y, pt2.x, pt2.y };
            ::ClipCursor(&clipRect);

            m_mouseDelta.x = 0;
            m_mouseDelta.y = 0;
            m_rawInputDeltaX = 0;
            m_rawInputDeltaY = 0;
        }
        else
        {
            ::ClipCursor(nullptr);
        }
    }

    void InputSystem::NotifyAppActivated(bool active)
    {
        if (m_appActive == active)
            return;

        m_appActive = active;
        if (active)
        {
            m_appActivated = true;
        }
        else
        {
            m_appDeactivated = true;
            StopAllGamepadVibrations();
        }
    }

    bool InputSystem::ConsumeAppDeactivated()
    {
        const bool v = m_appDeactivated;
        m_appDeactivated = false;
        return v;
    }

    bool InputSystem::ConsumeAppActivated()
    {
        const bool v = m_appActivated;
        m_appActivated = false;
        return v;
    }

    void InputSystem::ProcessRawInput(HRAWINPUT hRawInput)
    {
        if (!m_useRawInput || !hRawInput)
            return;

        UINT dwSize = 48;
        static BYTE lpb[48];

        if (::GetRawInputData(hRawInput, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
            return;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb);
        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            m_rawInputDeltaX += raw->data.mouse.lLastX;
            m_rawInputDeltaY += raw->data.mouse.lLastY;
        }
    }

    bool InputSystem::IsValidGamepadIndex(int playerIndex)
    {
        return playerIndex >= 0 && playerIndex < MaxGamepadCount;
    }

    WORD InputSystem::ToXInputButtonMask(GamepadButton button)
    {
        switch (button)
        {
        case GamepadButton::DPadUp: return XINPUT_GAMEPAD_DPAD_UP;
        case GamepadButton::DPadDown: return XINPUT_GAMEPAD_DPAD_DOWN;
        case GamepadButton::DPadLeft: return XINPUT_GAMEPAD_DPAD_LEFT;
        case GamepadButton::DPadRight: return XINPUT_GAMEPAD_DPAD_RIGHT;
        case GamepadButton::Start: return XINPUT_GAMEPAD_START;
        case GamepadButton::Back: return XINPUT_GAMEPAD_BACK;
        case GamepadButton::LeftThumb: return XINPUT_GAMEPAD_LEFT_THUMB;
        case GamepadButton::RightThumb: return XINPUT_GAMEPAD_RIGHT_THUMB;
        case GamepadButton::LeftShoulder: return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case GamepadButton::RightShoulder: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case GamepadButton::A: return XINPUT_GAMEPAD_A;
        case GamepadButton::B: return XINPUT_GAMEPAD_B;
        case GamepadButton::X: return XINPUT_GAMEPAD_X;
        case GamepadButton::Y: return XINPUT_GAMEPAD_Y;
        default: return 0;
        }
    }

    float InputSystem::Clamp01(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    float InputSystem::NormalizeTrigger(BYTE raw)
    {
        if (raw <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            return 0.0f;

        constexpr float denom = 255.0f - static_cast<float>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        return Clamp01((static_cast<float>(raw) - static_cast<float>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) / denom);
    }

    float InputSystem::NormalizeThumb(short raw, short deadZone)
    {
        const float value = static_cast<float>(raw);
        const float dead = static_cast<float>(deadZone);

        if (value > dead)
        {
            return Clamp01((value - dead) / (32767.0f - dead));
        }

        if (value < -dead)
        {
            return -Clamp01((-value - dead) / (32768.0f - dead));
        }

        return 0.0f;
    }

    bool InputSystem::IsButtonDown(const GamepadSnapshot& snapshot, GamepadButton button) const
    {
        if (!snapshot.connected)
            return false;

        if (button == GamepadButton::LeftTrigger)
            return snapshot.state.Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

        if (button == GamepadButton::RightTrigger)
            return snapshot.state.Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

        const WORD mask = ToXInputButtonMask(button);
        return (mask != 0) && ((snapshot.state.Gamepad.wButtons & mask) != 0);
    }

    void InputSystem::PollGamepads()
    {
        m_prevGamepads = m_currGamepads;

        for (int i = 0; i < MaxGamepadCount; ++i)
        {
            XINPUT_STATE state{};
            const DWORD result = XInputGetState(static_cast<DWORD>(i), &state);

            auto& dst = m_currGamepads[static_cast<std::size_t>(i)];
            dst.connected = (result == ERROR_SUCCESS);
            dst.state = dst.connected ? state : XINPUT_STATE{};
        }
    }

    void InputSystem::UpdateGamepadVibrations(float deltaTime)
    {
        if (!m_appActive)
        {
            m_vibrationRequests.clear();
            for (int i = 0; i < MaxGamepadCount; ++i)
                ApplyGamepadVibrationNow(i, 0.0f, 0.0f);
            return;
        }

        if (!m_vibrationRequests.empty())
        {
            for (TimedVibration& req : m_vibrationRequests)
                req.elapsedSec += std::max(0.0f, deltaTime);

            m_vibrationRequests.erase(
                std::remove_if(
                    m_vibrationRequests.begin(),
                    m_vibrationRequests.end(),
                    [](const TimedVibration& req)
                    {
                        return req.elapsedSec >= req.durationSec;
                    }),
                m_vibrationRequests.end());
        }

        std::array<std::pair<float, float>, MaxGamepadCount> out{};
        std::array<bool, MaxGamepadCount> hasOverride{};

        for (const TimedVibration& req : m_vibrationRequests)
        {
            if (!IsValidGamepadIndex(req.playerIndex))
                continue;

            const std::size_t idx = static_cast<std::size_t>(req.playerIndex);
            if (req.blend == GamepadVibrationBlend::Override)
            {
                out[idx].first = Clamp01(req.leftMotor);
                out[idx].second = Clamp01(req.rightMotor);
                hasOverride[idx] = true;
            }
        }

        for (const TimedVibration& req : m_vibrationRequests)
        {
            if (!IsValidGamepadIndex(req.playerIndex))
                continue;

            const std::size_t idx = static_cast<std::size_t>(req.playerIndex);
            if (hasOverride[idx])
                continue;

            const float left = Clamp01(req.leftMotor);
            const float right = Clamp01(req.rightMotor);

            switch (req.blend)
            {
            case GamepadVibrationBlend::Add:
                out[idx].first = Clamp01(out[idx].first + left);
                out[idx].second = Clamp01(out[idx].second + right);
                break;
            case GamepadVibrationBlend::Max:
                out[idx].first = std::max(out[idx].first, left);
                out[idx].second = std::max(out[idx].second, right);
                break;
            case GamepadVibrationBlend::Override:
                break;
            default:
                break;
            }
        }

        for (int i = 0; i < MaxGamepadCount; ++i)
        {
            const auto& value = out[static_cast<std::size_t>(i)];
            ApplyGamepadVibrationNow(i, value.first, value.second);
        }
    }

    void InputSystem::ApplyGamepadVibrationNow(int playerIndex, float leftMotor, float rightMotor)
    {
        if (!IsValidGamepadIndex(playerIndex))
            return;

        leftMotor = Clamp01(leftMotor);
        rightMotor = Clamp01(rightMotor);

        auto& applied = m_appliedVibration[static_cast<std::size_t>(playerIndex)];
        if (std::fabs(applied.first - leftMotor) <= kVibrationEpsilon &&
            std::fabs(applied.second - rightMotor) <= kVibrationEpsilon)
        {
            return;
        }

        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);

        const DWORD result = XInputSetState(static_cast<DWORD>(playerIndex), &vibration);
        if (result == ERROR_SUCCESS)
        {
            applied = { leftMotor, rightMotor };
        }
        else if (leftMotor == 0.0f && rightMotor == 0.0f)
        {
            applied = { 0.0f, 0.0f };
        }
    }
}
