#pragma once
#include <imgui.h>

// ImGui 최신 API 호환성 레이어
// 최신 ImGui에서는 ImGuiKey를 그대로 쓰는게 정답.
// 구버전 함수(GetKeyIndex, CaptureKeyboardFromApp 등) 전부 제거됨.

// IsKeyPressed 호환 함수
static inline bool Alice_IsKeyPressed(ImGuiKey key, bool repeat = true)
{
    return ImGui::IsKeyPressed(key, repeat);
}

// IsKeyDown 호환 함수
static inline bool Alice_IsKeyDown(ImGuiKey key)
{
    return ImGui::IsKeyDown(key);
}

// CaptureKeyboardFromApp 대체 함수
static inline void Alice_RequestKeyboardCapture()
{
    // 최신 ImGui 방식: SetNextFrameWantCaptureKeyboard 사용
    ImGui::SetNextFrameWantCaptureKeyboard(true);
}
