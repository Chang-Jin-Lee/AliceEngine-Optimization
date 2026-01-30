#pragma once

namespace Alice
{
    enum class MouseCode
    {
        Left = 0,
        Right,
        Middle,
        Count
    };

    enum class KeyCode
    {
        // 숫자
        Alpha0, Alpha1, Alpha2, Alpha3, Alpha4,
        Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,

        // 알파벳
        A, B, C, D, E, F, G, H, I, J,
        K, L, M, N, O, P, Q, R, S, T,
        U, V, W, X, Y, Z,

        // 방향키/기본키
        Up, Down, Left, Right,
        Space, Enter, Escape, Tab, Backspace,

        // 수정키
        LeftShift, RightShift,
        LeftCtrl,  RightCtrl,
        LeftAlt,   RightAlt,

        // 기능키
        F1, F2, F3, F4, F5, F6,
        F7, F8, F9, F10, F11, F12,

        Count
    };
}