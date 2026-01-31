#include "Runtime/Input/InputSystem.h"

#include <windowsx.h>

#ifndef RID_INPUT
#define RID_INPUT 0x10000003
#endif

using namespace DirectX;

namespace Alice
{
    InputSystem::InputSystem() = default;

    bool InputSystem::Initialize(HWND hWnd)
    {
        m_keyboard = std::make_unique<Keyboard>();
        m_mouse    = std::make_unique<Mouse>();

        m_mouse->SetWindow(hWnd);
        m_hWnd = hWnd; // 커서 제어를 위해 핸들 저장

        m_prevMousePos = POINT{ 0, 0 };
        m_mouseDelta   = POINT{ 0, 0 };
        m_hasPrevMousePos = false;

        // Raw Input 등록 (마우스)
        RAWINPUTDEVICE rid[1];
        rid[0].usUsagePage = 0x01; // Generic Desktop Controls
        rid[0].usUsage = 0x02;     // Mouse
        rid[0].dwFlags = 0;        // Default
        rid[0].hwndTarget = hWnd;

        if (RegisterRawInputDevices(rid, 1, sizeof(rid[0])))
        {
            m_useRawInput = true;
        }

        return true;
    }

    void InputSystem::Update(const float& /*deltaTime*/)
    {
        // 델타는 프레임마다 초기화합니다.
        m_mouseDelta.x = 0;
        m_mouseDelta.y = 0;
        m_mouseScrollDelta = 0.0f;

        if (!m_keyboard || !m_mouse) return;

        // DirectXTK 입력 상태 갱신
        m_mouseState = m_mouse->GetState();
        m_mouseTracker.Update(m_mouseState);

        m_keyboardState = m_keyboard->GetState();
        m_keyboardTracker.Update(m_keyboardState);

        // 마우스 잠금 상태에서 Raw Input 델타 사용
        if (m_isLocked && m_hWnd)
        {
            // Raw Input을 사용하는 경우, 하드웨어의 순수 이동량 사용
            if (m_useRawInput)
            {
                // Raw Input으로 누적된 델타 사용 (SetCursorPos의 영향을 받지 않음)
                m_mouseDelta.x = m_rawInputDeltaX;
                m_mouseDelta.y = m_rawInputDeltaY;
                
                // 프레임마다 Raw Input 델타 초기화 (다음 프레임에서 다시 누적)
                m_rawInputDeltaX = 0;
                m_rawInputDeltaY = 0;
            }
            else
            {
                // Raw Input을 사용하지 않는 경우: 기존 방식 (폴백)
                POINT currentScreenPos;
                ::GetCursorPos(&currentScreenPos);
                
                m_mouseDelta.x = currentScreenPos.x - m_lockedPos.x;
                m_mouseDelta.y = currentScreenPos.y - m_lockedPos.y;
            }

            // Lock 시점의 위치로 커서 되돌리기
            // Raw Input을 사용하므로 이 이동은 델타 값에 영향을 주지 않음
            ::SetCursorPos(m_lockedPos.x, m_lockedPos.y);
        }
        else
        {
            // 일반 모드: 클라이언트 좌표 기준 델타 계산
            POINT current{ m_mouseState.x, m_mouseState.y };

            if (!m_hasPrevMousePos)
            {
                m_prevMousePos    = current;
                m_hasPrevMousePos = true;
            }

            m_mouseDelta.x += current.x - m_prevMousePos.x;
            m_mouseDelta.y += current.y - m_prevMousePos.y;

            m_prevMousePos = current;
        }

        // 마우스 스크롤 델타 계산
        // DirectXTK의 scrollWheelValue는 누적값이므로 이전 값과의 차이를 계산합니다.
        const int currentScrollWheelValue = m_mouseState.scrollWheelValue;
        m_mouseScrollDelta = static_cast<float>(currentScrollWheelValue - m_prevScrollWheelValue);
        m_prevScrollWheelValue = currentScrollWheelValue;
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

    void InputSystem::SetCursorVisible(bool visible)
    {
        // ShowCursor는 카운터 방식이므로 강제로 상태를 맞춤
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
        m_isLocked = locked; // 잠금 상태 저장

        if (locked && m_hWnd)
        {
            // 현재 커서 위치를 '고정점'으로 저장 (Lock 시점의 위치)
            ::GetCursorPos(&m_lockedPos);

            // 윈도우 영역 안으로 커서 가두기
            RECT rect;
            ::GetClientRect(m_hWnd, &rect);
            
            // 클라이언트 영역을 스크린 좌표로 변환
            POINT pt = { rect.left, rect.top };
            POINT pt2 = { rect.right, rect.bottom };
            ::ClientToScreen(m_hWnd, &pt);
            ::ClientToScreen(m_hWnd, &pt2);
            
            RECT clipRect = { pt.x, pt.y, pt2.x, pt2.y };
            ::ClipCursor(&clipRect);

            // 델타 초기화 (순간이동으로 인한 회전 방지)
            m_mouseDelta.x = 0;
            m_mouseDelta.y = 0;
            m_rawInputDeltaX = 0;
            m_rawInputDeltaY = 0;
        }
        else
        {
            // 가두기 해제
            ::ClipCursor(nullptr);
        }
    }

    void InputSystem::ProcessRawInput(HRAWINPUT hRawInput)
    {
        if (!m_useRawInput || !hRawInput)
            return;

        UINT dwSize = 48; // 충분한 버퍼 크기
        static BYTE lpb[48];

        if (::GetRawInputData(hRawInput, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
            return;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb);
        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            // 마우스 하드웨어의 순수 이동량 누적
            // (SetCursorPos의 영향을 받지 않음)
            m_rawInputDeltaX += raw->data.mouse.lLastX;
            m_rawInputDeltaY += raw->data.mouse.lLastY;
        }
    }
}


