#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <directXTK/Keyboard.h>
#include <directXTK/Mouse.h>
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    /// DirectXTK Keyboard/Mouse 를 사용하는 간단한 입력 시스템입니다.
    /// - Win32 메시지를 DirectXTK 로 전달하고
    /// - 한 프레임 동안의 키/마우스 상태와 마우스 델타를 질의할 수 있게 합니다.
    class InputSystem
    {
    public:
        InputSystem();
        ~InputSystem();

        /// HWND 를 설정하고 Keyboard/Mouse 객체를 초기화합니다.
        bool Initialize(HWND hWnd);

        /// 매 프레임 한 번 호출하여
        /// DirectXTK Keyboard/Mouse 상태를 갱신하고 마우스 델타를 계산합니다.
        void Update(const float& deltaTime);

        // ---- 키보드/마우스 상태 질의 ----

        /// 지정한 키가 현재 눌려 있는지 여부를 반환합니다.
        bool IsKeyDown(DirectX::Keyboard::Keys key) const;

        /// 지정한 키가 이번 프레임에 눌렸는지 여부를 반환합니다 (이전 프레임에는 눌리지 않았고 이번 프레임에 눌림).
        bool IsKeyPressed(DirectX::Keyboard::Keys key) const;

        /// 오른쪽 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsRightButtonDown() const;

        /// 왼쪽 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsLeftButtonDown() const;

        /// 중간 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsMiddleButtonDown() const;

        /// 지정한 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsMouseButtonDown(int buttonIndex) const;

        /// 지정한 마우스 버튼이 이번 프레임에 눌렸는지 여부를 반환합니다 (이전 프레임에는 눌리지 않았고 이번 프레임에 눌림).
        bool IsMouseButtonPressed(int buttonIndex) const;

        /// 지정한 마우스 버튼이 이번 프레임에 떼졌는지 여부를 반환합니다 (이전 프레임에는 눌려있었고 이번 프레임에 떼짐).
        bool IsMouseButtonReleased(int buttonIndex) const;

        /// 마우스 현재 위치를 반환합니다 (클라이언트 좌표 기준).
        POINT GetMousePosition() const;

        /// 직전 프레임 이후 누적된 마우스 이동량을 반환합니다.
        POINT GetMouseDelta() const { return m_mouseDelta; }

        /// 직전 프레임 이후 누적된 마우스 스크롤 델타를 반환합니다.
        /// - 양수: 위로 스크롤, 음수: 아래로 스크롤
        float GetMouseScrollDelta() const { return m_mouseScrollDelta; }

        // ---- 게임패드 상태 질의 ----
        static constexpr int MaxGamepadCount = XUSER_MAX_COUNT;

        bool IsGamepadConnected(int playerIndex = 0) const;
        bool IsGamepadButtonDown(int playerIndex, GamepadButton button) const;
        bool IsGamepadButtonPressed(int playerIndex, GamepadButton button) const;
        bool IsGamepadButtonReleased(int playerIndex, GamepadButton button) const;

        float GetGamepadLeftTrigger(int playerIndex = 0) const;
        float GetGamepadRightTrigger(int playerIndex = 0) const;
        float GetGamepadLeftStickX(int playerIndex = 0) const;
        float GetGamepadLeftStickY(int playerIndex = 0) const;
        float GetGamepadRightStickX(int playerIndex = 0) const;
        float GetGamepadRightStickY(int playerIndex = 0) const;

        // ---- 게임패드 진동 ----
        // durationSec <= 0 이면 요청은 무시됩니다.
        void PlayGamepadVibration(int playerIndex,
                                  float leftMotor,
                                  float rightMotor,
                                  float durationSec,
                                  GamepadVibrationBlend blend = GamepadVibrationBlend::Max);
        void StopGamepadVibration(int playerIndex);
        void StopAllGamepadVibrations();

        // ---- 커서 제어 ----

        /// 마우스 커서를 표시하거나 숨깁니다.
        void SetCursorVisible(bool visible);

        /// 마우스 커서를 윈도우 영역에 가둡니다 (true) 또는 해제합니다 (false).
        void SetCursorLocked(bool locked);

        /// 현재 커서 잠금 상태
        bool IsCursorLocked() const { return m_isLocked; }

        /// 앱 활성 상태를 갱신합니다 (WM_ACTIVATEAPP/WM_SETFOCUS/WM_KILLFOCUS).
        void NotifyAppActivated(bool active);

        /// 현재 앱 활성 상태
        bool IsAppActive() const { return m_appActive; }

        /// 앱 비활성 이벤트를 1회 소비합니다.
        bool ConsumeAppDeactivated();

        /// 앱 활성 이벤트를 1회 소비합니다.
        bool ConsumeAppActivated();

        /// Raw Input 메시지 처리 (WM_INPUT 메시지에서 호출)
        void ProcessRawInput(HRAWINPUT hRawInput);

    private:
        struct GamepadSnapshot
        {
            bool connected = false;
            XINPUT_STATE state{};
        };

        struct TimedVibration
        {
            int playerIndex = 0;
            float leftMotor = 0.0f;
            float rightMotor = 0.0f;
            float durationSec = 0.0f;
            float elapsedSec = 0.0f;
            GamepadVibrationBlend blend = GamepadVibrationBlend::Max;
        };

        static bool IsValidGamepadIndex(int playerIndex);
        static WORD ToXInputButtonMask(GamepadButton button);
        static float Clamp01(float value);
        static float NormalizeTrigger(BYTE raw);
        static float NormalizeThumb(short raw, short deadZone);

        bool IsButtonDown(const GamepadSnapshot& snapshot, GamepadButton button) const;
        void PollGamepads();
        void UpdateGamepadVibrations(float deltaTime);
        void ApplyGamepadVibrationNow(int playerIndex, float leftMotor, float rightMotor);

        std::unique_ptr<DirectX::Keyboard> m_keyboard;
        std::unique_ptr<DirectX::Mouse>    m_mouse;

        DirectX::Keyboard::State                m_keyboardState{};
        DirectX::Keyboard::KeyboardStateTracker m_keyboardTracker{};

        DirectX::Mouse::State              m_mouseState{};
        DirectX::Mouse::ButtonStateTracker m_mouseTracker{};

        POINT m_prevMousePos{ 0, 0 };
        POINT m_mouseDelta{ 0, 0 };
        bool  m_hasPrevMousePos = false;

        int   m_prevScrollWheelValue{ 0 };
        float m_mouseScrollDelta{ 0.0f };

        HWND  m_hWnd{ nullptr }; // 커서 가두기용 윈도우 핸들
        bool  m_isLocked{ false }; // 마우스 잠금 상태 (중앙 고정 여부)
        POINT m_lockedPos{ 0, 0 }; // Lock 시점의 커서 위치 (스크린 좌표)
        bool  m_useRawInput{ false }; // Raw Input 사용 여부
        
        // Raw Input 델타 (누적값)
        int   m_rawInputDeltaX{ 0 };
        int   m_rawInputDeltaY{ 0 };

        // 앱 활성 상태/이벤트
        bool  m_appActive{ true };
        bool  m_appDeactivated{ false };
        bool  m_appActivated{ false };

        std::array<GamepadSnapshot, MaxGamepadCount> m_prevGamepads{};
        std::array<GamepadSnapshot, MaxGamepadCount> m_currGamepads{};
        std::vector<TimedVibration> m_vibrationRequests;
        std::array<std::pair<float, float>, MaxGamepadCount> m_appliedVibration{};
    };
}


