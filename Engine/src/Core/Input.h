#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

namespace Alice
{
    /// 간단한 키보드/마우스 입력 상태를 관리하는 클래스입니다.
    /// - Win32 메시지에서 전달되는 입력을 저장하고
    /// - 게임 루프에서는 현재 프레임의 상태를 질의하는 용도로 사용합니다.
    class Input
    {
    public:
        Input();

        /// 새 프레임이 시작될 때 한 번 호출하여
        /// 마우스 델타 등의 1프레임 한정 데이터를 초기화합니다.
        void NewFrame();

        // 키보드 입력 처리 -----------------------------------------
        void OnKeyDown(std::uint32_t virtualKey);
        void OnKeyUp(std::uint32_t virtualKey);

        /// 주어진 가상 키(VK_*)가 현재 눌려 있는지 여부를 반환합니다.
        bool IsKeyDown(std::uint32_t virtualKey) const;

        // 마우스 입력 처리 ------------------------------------------
        void OnMouseMove(int x, int y);
        void OnMouseDown(int buttonIndex); // 0: Left, 1: Right, 2: Middle
        void OnMouseUp(int buttonIndex);

        /// 현재 마우스 위치를 반환합니다(윈도우 클라이언트 좌표 기준).
        POINT GetMousePosition() const { return m_mousePosition; }

        /// 직전 프레임 이후부터 누적된 마우스 이동량을 반환합니다.
        POINT GetMouseDelta() const { return m_mouseDelta; }

        /// 지정한 마우스 버튼이 눌려 있는지 여부를 반환합니다.
        bool IsMouseDown(int buttonIndex) const;

    private:
        bool  m_keys[256];        // 간단히 0~255 범위를 사용하는 키 배열

        bool  m_mouseButtons[3];  // Left, Right, Middle
        POINT m_mousePosition;

        bool  m_hasPreviousPos = false;
        POINT m_prevMousePosition;
        POINT m_mouseDelta;
    };
}


